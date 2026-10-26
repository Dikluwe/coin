//! Typed module transport adapter. Coin semantics, including finite-operation
//! and alpha proofs, stay in the trusted C++ Core callback. No source string
//! from a frame packet is accepted as shader code.
use super::*;
use std::os::raw::c_char;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct CoinWgpuPortableNode {
    pub op: u32,
    pub value_type: u32,
    pub inputs: [u32; 3],
    pub value: [f32; 4],
    pub resource: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct CoinWgpuPortableModule {
    pub nodes: *const CoinWgpuPortableNode,
    pub node_count: u64,
    pub parameters: *const f32,
    pub parameter_count: u64,
    pub output: u32,
    pub alpha_may_vary: u32,
    pub signature: *const u8,
    pub signature_bytes: u64,
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct CoinWgpuPortableRange {
    pub lower: [f64; 4],
    pub upper: [f64; 4],
}
pub type CoreLowerV2 = unsafe extern "C" fn(*const CoinWgpuPortableModule,
    *const CoinWgpuPortableRange, *const CoinWgpuPortableRange, *const CoinWgpuPortableRange, u32,
    *mut c_char, u64, *mut u64, *mut c_char, u64) -> CoinWgpuStatus;
static CORE: OnceLock<CoreLowerV2> = OnceLock::new();

#[no_mangle]
pub extern "C" fn coin_wgpu_install_portable_core_v2(callback: Option<CoreLowerV2>) -> CoinWgpuStatus {
    let Some(callback) = callback else { return CoinWgpuStatus::InvalidArgument; };
    match CORE.set(callback) {
        Ok(()) => CoinWgpuStatus::Ok,
        Err(callback) if CORE.get().is_some_and(|current| *current as usize == callback as usize) => CoinWgpuStatus::Ok,
        Err(_) => CoinWgpuStatus::InvalidArgument,
    }
}

pub(super) struct Module<'a> {
    pub packet: &'a CoinWgpuPortableModule,
    pub source: String,
    pub signature: Vec<u8>,
    pub parameters: Vec<f32>,
    pub texture_mask: u32,
    pub coordinate_mask: u32,
}
const SIGNATURE_BYTES: u64 = 65536;
const FUNCTION_BYTES: u64 = 262144;
const RESOURCE_BYTES: u64 = 32 * 1024 * 1024;
type Error = (CoinWgpuStatus, String);
fn invalid(message: &str) -> Error { (CoinWgpuStatus::InvalidArgument, message.into()) }
fn slice<'a, T>(pointer: *const T, count: u64, label: &str) -> Result<&'a [T], Error> {
    let mut error = [0 as c_char; 512];
    validate_slice(pointer, count, label, error.as_mut_ptr(), error.len()).map_err(|status|
        (status, unsafe { std::ffi::CStr::from_ptr(error.as_ptr()) }.to_string_lossy().into_owned()))
}
fn call(packet: &CoinWgpuPortableModule, ranges: Option<(&CoinWgpuPortableRange,
    &[CoinWgpuPortableRange;8], &[CoinWgpuPortableRange;8], u32)>, output: &mut [u8]) -> Result<usize, Error>
{
    let core = CORE.get().ok_or((CoinWgpuStatus::Unsupported,
        "Portable shader Core lowering callback is unavailable".into()))?;
    let mut error = [0 as c_char; 512];
    let mut required = 0u64;
    let (primary, coordinates, samples, projection) = ranges.map_or((std::ptr::null(), std::ptr::null(), std::ptr::null(), 0),
        |(p,c,s,projection)| (p as *const _, c.as_ptr(), s.as_ptr(), projection));
    let result = unsafe { core(packet, primary, coordinates, samples, projection,
        if output.is_empty() { std::ptr::null_mut() } else { output.as_mut_ptr().cast() },
        output.len() as u64, &mut required, error.as_mut_ptr(), error.len() as u64) };
    if result != CoinWgpuStatus::Ok {
        return Err((result, unsafe { std::ffi::CStr::from_ptr(error.as_ptr()) }.to_string_lossy().into_owned()));
    }
    // required includes the NUL; the envelope covers the function alone.
    if required == 0 || required > FUNCTION_BYTES + 1 {
        return Err(invalid("Portable shader Core function exceeds 262144 byte source budget or has invalid length"));
    }
    usize::try_from(required).map_err(|_| invalid("Portable shader Core returned an invalid source length"))
}

pub(super) fn modules<'a>(frame: &'a CoinWgpuFrameView) -> Result<Vec<Module<'a>>, Error> {
    if frame.portable_module_count > RESOURCE_BYTES / 64 {
        return Err((CoinWgpuStatus::OutOfMemory, "Portable shader module table exceeds 32 MiB resource budget".into()));
    }
    let packets = slice(frame.portable_modules, frame.portable_module_count, "portable modules")?;
    let mut total = 0u64;
    // Admit the whole table before dereferencing signatures, invoking Core,
    // generating source or allocating a per-module owned payload.
    for packet in packets {
        if packet.node_count == 0 || packet.node_count > 256 || packet.parameter_count > 1024 ||
           packet.alpha_may_vary > 1 || packet.output as u64 >= packet.node_count ||
           packet.signature_bytes == 0 || packet.signature_bytes > SIGNATURE_BYTES {
            return Err(invalid("Invalid portable shader packet domains"));
        }
        let charge = packet.node_count.checked_mul(40)
            .and_then(|n| packet.parameter_count.checked_mul(4).and_then(|p| n.checked_add(p)))
            .and_then(|n| n.checked_add(packet.signature_bytes))
            .and_then(|n| n.checked_add(64))
            .ok_or_else(|| invalid("Portable shader module resource arithmetic overflow"))?;
        total = total.checked_add(charge).filter(|n| *n <= RESOURCE_BYTES)
            .ok_or((CoinWgpuStatus::OutOfMemory, "Portable shader module table exceeds 32 MiB resource budget".into()))?;
    }
    let mut result = Vec::with_capacity(packets.len());
    for packet in packets {
        let nodes = slice(packet.nodes, packet.node_count, "portable IR nodes")?;
        let parameters = slice(packet.parameters, packet.parameter_count, "portable parameters")?;
        let signature = slice(packet.signature, packet.signature_bytes, "portable signature")?;
        // Core checks the exact canonical signature against these statements,
        // the dense parameter payload, all operators and structural alpha.
        let size = call(packet, None, &mut [])?;
        let mut bytes = vec![0u8; size];
        if call(packet, None, &mut bytes)? != size || bytes.last() != Some(&0) {
            return Err(invalid("Portable shader Core returned an inconsistent source size"));
        }
        bytes.pop();
        let source = String::from_utf8(bytes).map_err(|_| invalid("Portable shader Core emitted invalid UTF-8"))?;
        let texture_mask = nodes.iter().filter(|node| node.op == 12).fold(0, |mask,node| mask | (1 << node.resource));
        let coordinate_mask = nodes.iter().filter(|node| node.op == 2).fold(0,|mask,node| mask | (1 << node.resource));
        result.push(Module { packet, source, coordinate_mask, signature: signature.to_vec(), parameters: parameters.to_vec(), texture_mask });
    }
    Ok(result)
}

fn matrix(matrix: &[f32;16], value: &[f32;4]) -> [f64;4] {
    // Evaluate the same row-vector/column-major f32 multiply used by Coin and
    // vertex delivery, then widen the resulting f32 endpoints for Core bounds.
    std::array::from_fn(|c| (value[0]*matrix[c] + value[1]*matrix[4+c] +
        value[2]*matrix[8+c] + value[3]*matrix[12+c]) as f64)
}
pub(super) fn validate_draws(modules: &[Module], vertices: &[CoinWgpuVertex], indices: &[u32],
    draws: &[CoinWgpuDraw], materials: &[CoinWgpuMaterial], states: &[CoinWgpuRenderState],
    textures: &[CoinWgpuTexture]) -> Result<(), Error>
{
    for state in states {
        if state.portable_shader_slot as usize > modules.len() {
            return Err(invalid("Portable shader state slot is invalid"));
        }
    }
    for draw in draws {
        let state = states.get(draw.render_state_slot as usize).ok_or_else(|| invalid("Invalid portable draw state"))?;
        if state.portable_shader_slot == 0 || draw.index_count == 0 { continue; }
        if draw.topology != 0 || state.raster_texture_source_slot != 0 {
            return Err((CoinWgpuStatus::Unsupported, "Portable modules require triangle surfaces".into()));
        }
        let module = &modules[state.portable_shader_slot as usize-1];
        let end = draw.first_index.checked_add(draw.index_count).ok_or_else(|| invalid("Portable draw index overflow"))? as usize;
        let selected = indices.get(draw.first_index as usize..end).ok_or_else(|| invalid("Portable draw indices out of range"))?;
        let empty = CoinWgpuPortableRange { lower: [f64::INFINITY;4], upper: [f64::NEG_INFINITY;4] };
        let mut coordinates = [empty;8];
        let mut samples = [CoinWgpuPortableRange { lower: [0.0;4], upper: [1.0;4] };8];
        let mut primary = if state.light_model == 0 { empty } else {
            CoinWgpuPortableRange { lower: [0.0;4], upper: [1.0;4] }
        };
        for unit in 0..8 {
            if module.texture_mask & (1 << unit) == 0 { continue; }
            let (enabled,slot) = if unit == 0 { (state.has_texture,state.texture_slot) }
                else { let layer = &state.extra_textures[unit-1]; (layer.enabled,layer.texture_slot) };
            let texture = textures.get(slot as usize).filter(|_| enabled == 1)
                .ok_or_else(|| invalid("Portable SAMPLE_2D unit is absent"))?;
            if matches!(texture.format,11|12) { return Err(invalid("Portable SAMPLE_2D cannot sample a spatial atlas")); }
            if matches!(texture.format,5|6) {
                samples[unit] = CoinWgpuPortableRange { lower: [-65504.0;4], upper: [65504.0;4] };
            }
        }
        for index in selected {
            let vertex = vertices.get(*index as usize).ok_or_else(|| invalid("Portable draw vertex out of range"))?;
            if state.light_model == 0 {
                let material = materials.get(vertex.material_slot as usize).ok_or_else(|| invalid("Portable material slot out of range"))?;
                for c in 0..4 {
                    let value = if c == 3 { 1.0 - material.transparency as f64 } else { material.diffuse[c] as f64 };
                    primary.lower[c] = primary.lower[c].min(value);
                    primary.upper[c] = primary.upper[c].max(value);
                }
            }
            for unit in 0..8 {
                let transformed = if unit == 0 { matrix(&state.texture_matrix,&vertex.texcoord) }
                    else { matrix(&state.extra_textures[unit-1].matrix,&vertex.extra_texcoords[unit-1]) };
                if module.coordinate_mask & (1 << unit) != 0 && !transformed.iter().all(|value|value.is_finite()) {
                    return Err(invalid("Portable shader TEXCOORD transform is nonfinite"));
                }
                for c in 0..4 {
                    coordinates[unit].lower[c] = coordinates[unit].lower[c].min(transformed[c]);
                    coordinates[unit].upper[c] = coordinates[unit].upper[c].max(transformed[c]);
                }
            }
        }
        call(module.packet,Some((&primary,&coordinates,&samples,state.texture_projection)),&mut [])?;
    }
    Ok(())
}

pub(super) fn validate_bump_occurrences(states: &[CoinWgpuRenderState], draws: &[CoinWgpuDraw],
    indices: &[u32], bump: &[CoinWgpuBumpVertex]) -> Result<(),Error>
{
    if bump.is_empty() { return Ok(()); }
    for draw in draws {
        let state = states.get(draw.render_state_slot as usize).ok_or_else(||invalid("Invalid portable bump state"))?;
        if state.portable_shader_slot == 0 { continue; }
        let end = draw.first_index.checked_add(draw.index_count).ok_or_else(||invalid("Portable bump index overflow"))? as usize;
        for index in indices.get(draw.first_index as usize..end).ok_or_else(||invalid("Invalid portable bump range"))? {
            if bump.get(*index as usize).ok_or_else(||invalid("Invalid portable bump vertex"))?.image_slot != 0 {
                return Err((CoinWgpuStatus::Unsupported,"Portable modules do not yet adapt active bump receiver attributes".into()));
            }
        }
    }
    Ok(())
}

pub(super) fn shader_source(source: &str, module: &Module) -> String {
    let mut helpers = String::from("@group(0) @binding(27) var<uniform> coinPortableData: array<vec4<f32>,256>;\nfn coinPortableUniform(i:u32)->f32 { return coinPortableData[i/4u][i%4u]; }\n");
    for unit in 0..8 {
        if module.texture_mask & (1 << unit) == 0 { continue; }
        let texture = if unit == 0 { "t_diffuse".into() } else { format!("t_texture{unit}") };
        let sampler = if unit == 0 { "s_diffuse".into() } else { format!("s_texture{unit}") };
        let params = if unit == 0 { "u.tex_params".into() } else { format!("u.extra_tex_params[{}]",unit-1) };
        helpers.push_str(&format!("fn coinPortableSample{unit}(coord:vec4<f32>)->vec4<f32>{{return coin_sample_surface({texture},{sampler},coord,{params},u.sampling_texture_sizes[{unit}]);}}\n"));
    }
    helpers.push_str(&module.source);
    let mut result = source.to_owned();
    let start = result.find("    var base_color = primary;").expect("portable surface owner");
    let end = result[start..].find("    // COIN_UPPER_TEXTURE_UNITS_END").map(|at|start+at)
        .or_else(|| result[start..].find("    let alpha_function").map(|at|start+at))
        .expect("portable alpha owner");
    let packed = source.contains("shadow_uv0:vec4<f32>");
    let mut coordinates = vec!["input.texcoord".to_owned()];
    for unit in 1..8 {
        if packed {
            let fields: Vec<_> = (3*(unit-1)..3*unit).map(|index|
                format!("input.shadow_uv{}.{}",index/4,["x","y","z","w"][index%4])).collect();
            coordinates.push(format!("vec4<f32>({},{},input.shadow_vertex{}.w,{})",
                fields[0],fields[1],unit-1,fields[2]));
        } else { coordinates.push(format!("input.uv{unit}")); }
    }
    let primary = if source.contains("var specularColor=") {
        "vec4<f32>(clamp(primary.rgb+specularColor,vec3<f32>(0.0),vec3<f32>(1.0)),primary.a)"
    } else { "primary" };
    result.replace_range(start..end,&format!("    var base_color = coinPortableModule({primary},{});\n",coordinates.join(",")));
    result = result.replace("return apply_fog(vec4<f32>(clamp(base_color.rgb+specularColor,vec3<f32>(0.0),vec3<f32>(1.0)),base_color.a), input.normal_view.w);",
        "return apply_fog(base_color, input.normal_view.w);");
    result = result.replace("if (u.tex_params.x > 0.5) {\n        output.texcoord = u.texture_matrix * input.texcoord;\n    } else {\n        output.texcoord = input.texcoord;\n    }", "output.texcoord = u.texture_matrix * input.texcoord;");
    format!("{helpers}\n{result}")
}


pub(super) fn trace_compile(module: &Module, policy: SamplingPolicy, peel: bool,
    depth: bool, shadows: bool, family: &str)
{
    if std::env::var_os("COIN_RENDER_TRACE_PHASES").is_none() { return; }
    let id = module.signature.iter().fold(0xcbf29ce484222325u64,
        |value,byte| (value ^ *byte as u64).wrapping_mul(0x100000001b3));
    eprintln!("COIN_RENDER_PHASE portable_shader_compile module={id:016x} profile=wgpu_{policy:?}_{peel}_{depth}_{shadows}_{family}");
    use std::io::Write;
    let _ = std::io::stderr().flush();
}
struct ProfileEntry {
    profile: Arc<SurfaceSamplingProfile>,
    bytes: usize,
    last_serial: u64,
}
#[derive(Default)]
pub(super) struct ProfileCache {
    active: HashMap<Vec<u8>,ProfileEntry>,
    retired: Vec<(u64,ProfileEntry)>,
    bytes: usize,
}
pub(super) fn profile(ctx: &DeviceState, module: &Module,
    base: &SurfaceSamplingProfile, samples: u32, map_mode: u32) -> Result<Arc<SurfaceSamplingProfile>,Error>
{
    let mut key = module.signature.clone();
    key.extend_from_slice(&[u8::from(base.policy == SamplingPolicy::Portable),
        u8::from(base.multisampled_peel),u8::from(base.sample_depth),u8::from(base.shadow_multitexture)]);
    key.extend_from_slice(&samples.to_le_bytes());
    key.extend_from_slice(&map_mode.to_le_bytes());
    let mut cache = ctx.portable_profiles.lock().unwrap();
    let completed = ctx.completed_serial.load(Ordering::SeqCst);
    let mut released = 0;
    cache.retired.retain(|(serial,entry)| {
        if *serial <= completed { released += entry.bytes; false } else { true }
    });
    cache.bytes -= released;
    let submitted = ctx.last_submitted_serial.load(Ordering::SeqCst).saturating_add(1);
    if let Some(entry) = cache.active.get_mut(&key) {
        entry.last_serial = submitted;
        return Ok(entry.profile.clone());
    }
    ctx.device.push_error_scope(wgpu::ErrorFilter::OutOfMemory);
    ctx.device.push_error_scope(wgpu::ErrorFilter::Validation);
    let compiled = Arc::new(create_surface_sampling_profile_impl(&ctx.device,&ctx.adapter,
        base.policy,base.multisampled_peel,base.sample_depth,base.shadow_multitexture,false,Some((module,map_mode))));
    let validation = block_on(ctx.device.pop_error_scope());
    let memory = block_on(ctx.device.pop_error_scope());
    if let Some(error) = validation.or(memory) {
        return Err((CoinWgpuStatus::BackendError,format!("Portable shader profile creation failed: {error}")));
    }
    // Nominal owned source/key bytes are measured. Opaque driver allocations
    // are not reported as if they were known GPU memory sizes.
    let bytes = key.len().checked_add(module.source.len().checked_mul(3).ok_or_else(|| invalid("Portable shader source size overflow"))?)
        .ok_or_else(|| invalid("Portable shader cache size overflow"))?;
    cache.bytes = cache.bytes.checked_add(bytes).ok_or_else(|| invalid("Portable shader cache accounting overflow"))?;
    cache.active.insert(key.clone(),ProfileEntry { profile: compiled.clone(), bytes, last_serial: submitted });
    const BUDGET: usize = 32*1024*1024;
    while cache.bytes > BUDGET {
        let oldest = cache.active.iter().filter(|(candidate,_)| candidate.as_slice() != key.as_slice())
            .min_by_key(|(_,entry)|entry.last_serial).map(|(candidate,_)|candidate.clone());
        let Some(oldest) = oldest else { break; };
        let entry = cache.active.remove(&oldest).unwrap();
        // Previous queue uses retire at their existing completion fence.
        // Unsubmitted command buffers independently own their pipeline handles.
        if entry.last_serial > completed { cache.retired.push((entry.last_serial,entry)); }
        else { cache.bytes -= entry.bytes; }
    }
    Ok(compiled)
}
pub(super) fn prepare(ctx: &DeviceState, modules: &[Module], states: &[CoinWgpuRenderState],
    draws: &[CoinWgpuDraw], order: &[composition::CompositionItem], samples: u32,
    requested_policy: u32, rasterization_policy: u32, map_mode: u32,
    color_format: wgpu::TextureFormat) -> Result<(),Error>
{
    if modules.is_empty() { return Ok(()); }
    let policy = SamplingPolicy::from_raw(requested_policy)?;
    let sample_depth = samples > 1 && order.iter().any(|item|item.peel);
    let shadows = map_mode != 0;
    for item in order {
        let draw = draws.get(item.draw_index).ok_or_else(|| invalid("Invalid portable prepare draw"))?;
        let state = states.get(draw.render_state_slot as usize).ok_or_else(|| invalid("Invalid portable prepare state"))?;
        if state.portable_shader_slot == 0 || draw.index_count == 0 || state.viewport == [0,0,0,1] { continue; }
        let module = modules.get(state.portable_shader_slot as usize-1).ok_or_else(|| invalid("Invalid portable prepare module slot"))?;
        let resolved_offset = state.polygon_offset_enabled != 0 &&
            state.polygon_offset_styles & state.polygon_offset_primitive_style != 0 &&
            (state.polygon_offset_slope_bias != 0.0 ||
             (state.polygon_offset_max_depth_bits != 0 && state.polygon_offset_units != 0.0));
        let shared_depth = shadows && order.iter().any(|item|
            (samples > 1 && item.weighted) || (samples == 1 && state.depth_range != [0.0,1.0] && (item.weighted || item.peel)));
        let resolved_depth = shared_depth || sample_depth || resolved_offset ||
            state.depth_range[0] > state.depth_range[1] || rasterization_policy == 1 ||
            (shadows && samples > 1 && custom_shadow_sample_depth(state));
        let base = if sample_depth {
            if shadows { ctx.shadow_multitexture_peeling_surface_profile(policy) } else { ctx.peeling_surface_profile(policy) }
        } else if samples > 1 && (!item.weighted || shadows) && resolved_depth {
            if shadows { ctx.shadow_multitexture_depth_surface_profile(policy) } else { ctx.sample_depth_surface_profile(policy) }
        } else if shadows { ctx.shadow_multitexture_surface_profile(policy) } else { ctx.surface_profile(policy) };
        let profile = profile(ctx,module,base,samples,map_mode)?;
        let compare = if item.peel { wgpu::CompareFunction::LessEqual }
            else if state.depth_test == 0 { wgpu::CompareFunction::Always }
            else { match state.depth_function {
                0 => wgpu::CompareFunction::Never, 1 => wgpu::CompareFunction::Always,
                3 => wgpu::CompareFunction::LessEqual, 4 => wgpu::CompareFunction::Equal,
                5 => wgpu::CompareFunction::GreaterEqual, 6 => wgpu::CompareFunction::Greater,
                7 => wgpu::CompareFunction::NotEqual, _ => wgpu::CompareFunction::Less,
            }};
        let bias = if resolved_depth { wgpu::DepthBiasState::default() }
            else { polygon_depth_bias(state,draw.topology).map_err(|error|invalid(&error))? };
        ctx.device.push_error_scope(wgpu::ErrorFilter::OutOfMemory);
        ctx.device.push_error_scope(wgpu::ErrorFilter::Validation);
        let result = get_or_create_pipeline(ctx,&profile,draw.topology,
            if item.peel || item.weighted { wgpu::TextureFormat::Rgba16Float } else { color_format },
            wgpu::TextureFormat::Depth32Float,samples,
            match state.cull_mode {0=>None,2=>Some(wgpu::Face::Front),_=>Some(wgpu::Face::Back)},
            if state.front_face == 1 {wgpu::FrontFace::Cw} else {wgpu::FrontFace::Ccw},
            item.blend && !item.peel && !item.weighted,item.additive,item.legacy_blend_alpha,
            item.peel,item.peel || (!item.weighted && state.depth_test != 0 && state.depth_write != 0),
            state.color_write,compare,bias,resolved_depth,map_mode == 2,shadows,item.weighted,true,false);
        let validation = block_on(ctx.device.pop_error_scope());
        let memory = block_on(ctx.device.pop_error_scope());
        result.map_err(|error|(CoinWgpuStatus::BackendError,error))?;
        if let Some(error) = validation.or(memory) {
            return Err((CoinWgpuStatus::BackendError,format!("Portable shader pipeline preparation failed: {error}")));
        }
    }
    Ok(())
}

/// Trusted capture preflight: no command encoder, target acquisition, upload,
/// scene submission or publication. The actual entry point repeats profile
/// preparation for its exact destination format/device before acquiring it.
#[no_mangle]
pub extern "C" fn coin_wgpu_prepare_portable_frame(frame: *const CoinWgpuFrameView, samples: u32,
    error_buf: *mut c_char, error_len: usize) -> CoinWgpuStatus
{
    let result = std::panic::catch_unwind(|| -> Result<(),Error> {
        if !matches!(samples,1|2|4) { return Err(invalid("Invalid portable preparation sample count")); }
        let f = slice(frame,1,"portable frame")?.first().ok_or_else(||invalid("Portable frame is absent"))?;
        if f.abi_version != COIN_WGPU_ABI_VERSION || f.struct_size as usize != std::mem::size_of::<CoinWgpuFrameView>() {
            return Err(invalid("Portable preparation frame ABI mismatch"));
        }
        let modules = modules(f)?;
        if modules.is_empty() { return Ok(()); }
        if f.instance_count != 0 || f.instance_range_count != 0 {
            return Err((CoinWgpuStatus::Unsupported,"Portable modules require non-instanced delivery".into()));
        }
        let vertices = slice(f.vertices,f.vertex_count,"portable vertices")?;
        let indices = slice(f.indices,f.index_count,"portable indices")?;
        let draws = slice(f.draws,f.draw_count,"portable draws")?;
        let states = slice(f.states,f.state_count,"portable states")?;
        let materials = slice(f.materials,f.material_count,"portable materials")?;
        let textures = slice(f.textures,f.texture_count,"portable textures")?;
        validate_draws(&modules,vertices,indices,draws,materials,states,textures)?;
        validate_bump_occurrences(states,draws,indices,slice(f.bump_vertices,f.bump_vertex_count,"portable bump vertices")?)?;
        let order = composition::order(vertices,indices,draws,materials,states,textures)?;
        let shadow = validate_shadow_frame(f,states.len(),false,error_buf,error_len)
            .map_err(|status|(status,"Invalid portable shadow frame".into()))?;
        init_runtime_if_needed().map_err(|error|(CoinWgpuStatus::NotReady,error))?;
        let mut guard = RUNTIME_CTX.lock().map_err(|error|(CoinWgpuStatus::BackendError,error.to_string()))?;
        let runtime = guard.as_mut().ok_or((CoinWgpuStatus::NotReady,"Runtime context unavailable".into()))?;
        let ctx = offscreen_device(runtime,0).map_err(|error|(CoinWgpuStatus::NotReady,error))?;
        prepare(ctx,&modules,states,draws,&order,samples,f.texture_sampling_policy,f.rasterization_policy,
            shadow.as_ref().map_or(0,|shadow|if shadow.extra.is_empty(){1}else{2}),
            if f.texture_output_flags&1 != 0 {wgpu::TextureFormat::Rgba16Float} else {wgpu::TextureFormat::Rgba8Unorm})
    });
    match result {
        Ok(Ok(())) => CoinWgpuStatus::Ok,
        Ok(Err((status,message))) => {set_error(error_buf,error_len,&message);status},
        Err(_) => {set_error(error_buf,error_len,"Portable shader preparation failed");CoinWgpuStatus::BackendError},
    }
}

#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/portable_c_char_tests.rs"]
mod portable_c_char_tests;
