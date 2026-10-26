//! Validated frame preparation and render command encoding.

use super::*;

pub(super) fn encode_frame(
    ctx: &DeviceState,
    frame_revision: u64,
    clear_color: [f32; 4],
    target_width: u32,
    target_height: u32,
    vertices_slice: &[CoinWgpuVertex],
    indices_slice: &[u32],
    draws_slice: &[CoinWgpuDraw],
    draw_order: &[composition::CompositionItem],
    materials_slice: &[CoinWgpuMaterial],
    states_slice: &[CoinWgpuRenderState],
    portable_modules: &[portable::Module],
    textures_slice: &[CoinWgpuTexture],
    samplers_slice: &[CoinWgpuSampler],
    shadow_frame: Option<&ShadowFrame>,
    camera_geometry: Option<&Arc<ValidatedGeometry>>,
    instances_slice: &[CoinWgpuInstance],
    instance_ranges: &[CoinWgpuInstanceRange],
    timestamp_query: Option<&wgpu::QuerySet>,
    color_view: &wgpu::TextureView,
    resolve_view: Option<&wgpu::TextureView>,
    color_format: wgpu::TextureFormat,
    depth_view: &wgpu::TextureView,
    depth_texture: &wgpu::Texture,
    sample_count: u32,
    peel_passes: u32,
    requested_sampling_policy: u32,
    rasterization_policy: u32,
    clear_attachments: bool,
) -> Result<(wgpu::CommandBuffer, Option<InstancedGpuBuffers>), (CoinWgpuStatus, String)> {
    encode_frame_impl(
        ctx,
        frame_revision,
        clear_color,
        target_width,
        target_height,
        vertices_slice,
        indices_slice,
        draws_slice,
        draw_order,
        materials_slice,
        states_slice,
        portable_modules,
        textures_slice,
        samplers_slice,
        shadow_frame,
        camera_geometry,
        instances_slice,
        instance_ranges,
        timestamp_query,
        color_view,
        resolve_view,
        color_format,
        depth_view,
        depth_texture,
        sample_count,
        peel_passes,
        requested_sampling_policy,
        rasterization_policy,
        clear_attachments,
        None,
        None,
    )
}

pub(super) fn encode_frame_impl(
    ctx: &DeviceState,
    frame_revision: u64,
    clear_color: [f32; 4],
    target_width: u32,
    target_height: u32,
    vertices_slice: &[CoinWgpuVertex],
    indices_slice: &[u32],
    draws_slice: &[CoinWgpuDraw],
    draw_order: &[composition::CompositionItem],
    materials_slice: &[CoinWgpuMaterial],
    states_slice: &[CoinWgpuRenderState],
    portable_modules: &[portable::Module],
    textures_slice: &[CoinWgpuTexture],
    samplers_slice: &[CoinWgpuSampler],
    shadow_frame: Option<&ShadowFrame>,
    camera_geometry: Option<&Arc<ValidatedGeometry>>,
    instances_slice: &[CoinWgpuInstance],
    instance_ranges: &[CoinWgpuInstanceRange],
    timestamp_query: Option<&wgpu::QuerySet>,
    color_view: &wgpu::TextureView,
    resolve_view: Option<&wgpu::TextureView>,
    color_format: wgpu::TextureFormat,
    depth_view: &wgpu::TextureView,
    depth_texture: &wgpu::Texture,
    sample_count: u32,
    peel_passes: u32,
    requested_sampling_policy: u32,
    rasterization_policy: u32,
    clear_attachments: bool,
    bump_binding: Option<(&wgpu::TextureView,&wgpu::Sampler,&[GpuMaterial])>,
    shared_bump_shadow: Option<&(Vec<shadow::ShadowMap>, shadow::ShadowArray)>,
) -> Result<(wgpu::CommandBuffer, Option<InstancedGpuBuffers>), (CoinWgpuStatus, String)> {
    use wgpu::util::DeviceExt;
    admit_spatial_resources(ctx,textures_slice)?;
    validate_cube_resources(states_slice,textures_slice,samplers_slice)?;
    validate_cube_draws(states_slice,textures_slice,vertices_slice,indices_slice,draws_slice)?;
    raster_image::admit(ctx,states_slice,textures_slice,samplers_slice.len())?;
    if !states_slice.iter().all(valid_alpha_test) {
        return Err((CoinWgpuStatus::InvalidArgument, "Invalid alpha comparison function or reference".into()));
    }
    if states_slice.iter().any(|st| st.texture_projection > 1) {
        return Err((CoinWgpuStatus::InvalidArgument, "Invalid texture projection policy".into()));
    }
    if rasterization_policy > 1 {
        return Err((CoinWgpuStatus::InvalidArgument, "Invalid rasterization policy".into()));
    }
    if rasterization_policy == 1 && draws_slice.iter().any(|draw| draw.topology != 0) {
        return Err((CoinWgpuStatus::Unsupported, "Canonical coverage requires triangle surfaces".into()));
    }
    if sample_count > 1 && (rasterization_policy == 1 || states_slice.iter().any(|st| st.polygon_offset_enabled != 0 || st.depth_range[0] > st.depth_range[1])) &&
        !ctx.adapter.get_downlevel_capabilities().flags.contains(wgpu::DownlevelFlags::MULTISAMPLED_SHADING) {
        return Err((CoinWgpuStatus::Unsupported, "Custom MSAA depth requires sample-frequency shading".into()));
    }
    let instanced = !instances_slice.is_empty();
    let requested_policy = SamplingPolicy::from_raw(requested_sampling_policy)?;
    // Non-textured/instanced frames retain the original native prefix and pipelines.
    let policy = if !portable_modules.is_empty() { requested_policy }
        else if instanced || !samplers_slice.iter().any(|s| s.filter == 2) {
        SamplingPolicy::Native
    } else { requested_policy };
    let shadow_multitexture = validate_shadow_multitexture_profile(ctx, textures_slice, states_slice, draws_slice,
        draw_order, shadow_frame, sample_count, rasterization_policy)? || bump_binding.is_some() ||
        (!portable_modules.is_empty() && shadow_frame.is_some());
    let bump_profile = bump_binding.map(|_| ctx.bump_receiver_profile(policy));
    let profile = if let Some(p)=bump_profile.as_ref(){p.as_ref()} else if shadow_multitexture { ctx.shadow_multitexture_surface_profile(policy) } else { ctx.surface_profile(policy) };

    if instanced && (ctx.device.limits().max_storage_buffers_per_shader_stage < 2
        || instancing::gpu_bytes(instances_slice.len())
            > u64::from(ctx.device.limits().max_storage_buffer_binding_size)) {
        return Err((CoinWgpuStatus::Unsupported,
            "Instancing exceeds enabled vertex storage buffer limits".into()));
    }
    if draw_order.iter().any(|item| item.peel) && !peeling_supported(ctx, sample_count) {
        return Err((
            CoinWgpuStatus::Unsupported,
            "Peeling lacks enabled device formats/attachments; no fallback was applied".into(),
        ));
    }
    if draw_order.iter().any(|item| item.weighted) && !weighted_supported(ctx, sample_count) {
        return Err((CoinWgpuStatus::Unsupported,"Weighted OIT lacks enabled formats/attachments; no fallback was applied".into()));
    }
    // Composition was preflighted before surface acquisition or target allocation.
    if let Some(shadow) = shadow_frame {
        let features = ctx.adapter.get_texture_format_features(wgpu::TextureFormat::Rgba32Float);
        if !features.allowed_usages.contains(
            wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING)
            || shadow.map_size > ctx.device.limits().max_texture_dimension_2d {
            return Err((CoinWgpuStatus::Unsupported,
                "Rgba32Float shadow maps are unavailable on this device".into()));
        }
        if shadow.receivers.len() != states_slice.len()
            || draws_slice.iter().any(|draw| draw.render_state_slot as usize >= shadow.receivers.len()) {
            return Err((CoinWgpuStatus::InvalidArgument,
                "Shadow receivers do not match render states or composition".into()));
        }
        if let Some(second) = &shadow.second {
            if second.map_size > ctx.device.limits().max_texture_dimension_2d ||
                second.receivers.len() != states_slice.len() {
                return Err((CoinWgpuStatus::InvalidArgument,
                    "Second shadow pass does not match render states or device limits".into()));
            }
        }
        if let Some(pass) = &shadow.third {
            if pass.map_size > ctx.device.limits().max_texture_dimension_2d ||
                pass.receivers.len() != states_slice.len() {
                return Err((CoinWgpuStatus::InvalidArgument,
                    "third shadow pass does not match render states or device limits".into()));
            }
        }
        if let Some(pass) = &shadow.fourth {
            if pass.map_size > ctx.device.limits().max_texture_dimension_2d ||
                pass.receivers.len() != states_slice.len() {
                return Err((CoinWgpuStatus::InvalidArgument,
                    "fourth shadow pass does not match render states or device limits".into()));
            }
        }
        for (slot, pass) in shadow.extra.iter().enumerate() {
            if pass.map_size > ctx.device.limits().max_texture_dimension_2d ||
                pass.receivers.len() != states_slice.len() {
                return Err((CoinWgpuStatus::InvalidArgument,
                    format!("extra shadow pass {slot} does not match render states or device limits")));
            }
            for (state_slot, receiver) in pass.receivers.iter().enumerate() {
                if receiver.receives & !15 != 0 ||
                    (receiver.receives & 1 != 0 && (receiver.lighting_index < 0 ||
                     receiver.lighting_index as u32 >= states_slice[state_slot].light_count)) ||
                    !receiver.max_shadow_distance.is_finite() ||
                    !receiver.distance_falloff_coefficient.is_finite() ||
                    !receiver.model_view.iter().chain(receiver.model_view_projection.iter())
                        .all(|v| v.is_finite()) {
                    return Err((CoinWgpuStatus::InvalidArgument,
                        format!("Invalid extra shadow receiver {slot}:{state_slot}")));
                }
            }
        }
        for (slot, receiver) in shadow.receivers.iter().enumerate() {
            if receiver.receives & !15 != 0
                || !receiver.max_shadow_distance.is_finite()
                || !receiver.distance_falloff_coefficient.is_finite()
                || (receiver.max_shadow_distance > 0.0 &&
                    receiver.distance_falloff_coefficient <= 0.0)
                || !receiver.model_view.iter().chain(receiver.model_view_projection.iter())
                    .all(|v| v.is_finite())
                || (receiver.receives & 1 != 0 && (receiver.lighting_index < 0
                    || receiver.lighting_index as u32 >= states_slice[slot].light_count)) {
                return Err((CoinWgpuStatus::InvalidArgument,
                    format!("Invalid shadow receiver {slot}")));
            }
        }
        if let Some(second) = &shadow.second {
            for (slot, receiver) in second.receivers.iter().enumerate() {
                if receiver.receives & !15 != 0 ||
                    (receiver.receives & 1 != 0 && (receiver.lighting_index < 0 ||
                     receiver.lighting_index as u32 >= states_slice[slot].light_count)) ||
                    !receiver.model_view.iter().chain(receiver.model_view_projection.iter())
                        .all(|v| v.is_finite()) {
                    return Err((CoinWgpuStatus::InvalidArgument,
                        format!("Invalid second shadow receiver {slot}")));
                }
            }
        }
        if let Some(pass) = &shadow.third {
            for (slot, receiver) in pass.receivers.iter().enumerate() {
                if receiver.receives & !15 != 0 ||
                    (receiver.receives & 1 != 0 && (receiver.lighting_index < 0 ||
                     receiver.lighting_index as u32 >= states_slice[slot].light_count)) ||
                    !receiver.max_shadow_distance.is_finite() ||
                    !receiver.distance_falloff_coefficient.is_finite() ||
                    !receiver.model_view.iter().chain(receiver.model_view_projection.iter())
                        .all(|v| v.is_finite()) {
                    return Err((CoinWgpuStatus::InvalidArgument,
                        format!("Invalid third shadow receiver {slot}")));
                }
            }
        }
        if let Some(pass) = &shadow.fourth {
            for (slot, receiver) in pass.receivers.iter().enumerate() {
                if receiver.receives & !15 != 0 ||
                    (receiver.receives & 1 != 0 && (receiver.lighting_index < 0 ||
                     receiver.lighting_index as u32 >= states_slice[slot].light_count)) ||
                    !receiver.max_shadow_distance.is_finite() ||
                    !receiver.distance_falloff_coefficient.is_finite() ||
                    !receiver.model_view.iter().chain(receiver.model_view_projection.iter())
                        .all(|v| v.is_finite()) {
                    return Err((CoinWgpuStatus::InvalidArgument,
                        format!("Invalid fourth shadow receiver {slot}")));
                }
            }
        }
    }
    // Preflight the entire lighting payload before cache mutation or command encoding.
    for (state_index, state) in states_slice.iter().enumerate() {
        resolved_viewport(state, target_width, target_height)?;
        for unit in 0..8 {
            let (enabled,image,sampler)=if unit==0 {(state.has_texture,state.texture_slot,state.sampler_slot)}
                else {let t=&state.extra_textures[unit-1];(t.enabled,t.texture_slot,t.sampler_slot)};
            if enabled==0 {continue;}
            let image=textures_slice.get(image as usize).ok_or_else(||(CoinWgpuStatus::InvalidArgument,"P07 image slot out of bounds".into()))?;
            let sampler=samplers_slice.get(sampler as usize).ok_or_else(||(CoinWgpuStatus::InvalidArgument,"P07 sampler slot out of bounds".into()))?;
            if sampler.filter>=2 && !(if image.format==1 {texture_metadata(image)&2!=0}
                else if image.format==11 {texture_metadata(image)&(1<<24)!=0}
                else if image.format==12 {texture_metadata(image)&128!=0}
                else {texture_profile::format(image.format)?.1}) {
                return Err((CoinWgpuStatus::Unsupported,"P07 mip sampler requires complete stored-image chain".into()));
            }
        }
        if state.light_count > 8 {
            return Err((CoinWgpuStatus::Unsupported,
                format!("State {} has more than eight active lights", state_index)));
        }
        if !state.ambient_light.iter().all(|v| v.is_finite())
            || !state.normal_matrix.iter().all(|v| v.is_finite()) {
            return Err((CoinWgpuStatus::InvalidArgument,
                format!("State {} has non-finite ambient or normal matrix", state_index)));
        }
        if state.fog_mode > 3 || !state.fog_color.iter().all(|v| v.is_finite())
            || !state.fog_start.is_finite() || !state.fog_end.is_finite()
            || (state.fog_mode != 0 && state.fog_end <= 0.0)
            || (state.fog_mode == 1 && state.fog_end <= state.fog_start) {
            return Err((CoinWgpuStatus::InvalidArgument,
                format!("State {} has invalid fog parameters", state_index)));
        }
        for (light_index, light) in state.lights[..state.light_count as usize].iter().enumerate() {
            let finite = light.position_type.iter().chain(light.direction_cutoff.iter())
                .chain(light.color_intensity.iter()).chain(light.attenuation_exponent.iter())
                .chain(light.ambient_color.iter()).all(|v| v.is_finite());
            let kind = light.position_type[3];
            let attenuation = light.attenuation_exponent;
            if !finite || (kind != 0.0 && kind != 1.0 && kind != 2.0)
                || light.color_intensity[3] < 0.0 || light.ambient_color[3] != 0.0
                || attenuation[3] < 0.0
                || attenuation[..3].iter().any(|v| *v < 0.0)
                || (kind != 0.0 && attenuation[..3].iter().all(|v| *v == 0.0)) {
                return Err((CoinWgpuStatus::InvalidArgument,
                    format!("State {} light {} has invalid type or parameters", state_index, light_index)));
            }
        }
    }


    for image in textures_slice {
        if gpu_texture_token(image) {continue;}
        let format=texture_gpu_format(image)?;
        if format.is_compressed() && !ctx.device.features().contains(wgpu::Features::TEXTURE_COMPRESSION_BC) {
            return Err((CoinWgpuStatus::Unsupported,"BC compression unavailable on selected adapter".into()));
        }
        let features=ctx.adapter.get_texture_format_features(format);
        if !features.allowed_usages.contains(wgpu::TextureUsages::TEXTURE_BINDING|wgpu::TextureUsages::COPY_DST) {
            return Err((CoinWgpuStatus::Unsupported,"Texture format unavailable on selected adapter".into()));
        }
    }
    for sampler in samplers_slice {
        let max=texture_profile::anisotropy(sampler.reserved & 15,sampler.filter)?;
        if max>1 && !ctx.adapter.get_downlevel_capabilities().flags.contains(wgpu::DownlevelFlags::ANISOTROPIC_FILTERING) {
            return Err((CoinWgpuStatus::Unsupported,"Anisotropic filtering unavailable".into()));
        }
    }
    ctx.camera_bindings_reused.store(0, Ordering::Relaxed);
    ctx.camera_bindings_created.store(0, Ordering::Relaxed);
    // 1. Process pending GPU completion events and lock geometry cache
    let _ = ctx.device.poll(wgpu::Maintain::Poll);
    let mut cache = ctx.cache.lock().unwrap();

    // Reset per-frame telemetry counters
    cache.frame_uploaded_bytes = 0;
    cache.frame_uploads = 0;
    cache.frame_hits = 0;

    // Drain safely retired buffers whose work on GPU has completed
    let completed_serial = ctx.completed_serial.load(Ordering::SeqCst);
    cache.deferred_release.retain(|retired| {
        retired.retired_at_serial > completed_serial
    });

    let mut tex_cache = ctx.texture_cache.lock().unwrap();
    let mut samp_cache = ctx.sampler_cache.lock().unwrap();
    let mut rtt_cache = ctx.rtt_textures.lock().unwrap();
    rtt_cache.retired.retain(|retired| retired.retired_at_serial > completed_serial);

    // Drain safely retired textures whose work on GPU has completed
    tex_cache.retired.retain(|retired| {
        retired.retired_at_serial > completed_serial
    });

    let current_submission_serial = ctx.last_submitted_serial.load(Ordering::SeqCst) + 1;

    // Upload and cache frame textures
    for (t_idx, t) in textures_slice.iter().enumerate() {
        if t.width == 0 || t.height == 0 || t.width > 8192 || t.height > 8192 {
            return Err((
                CoinWgpuStatus::InvalidArgument,
                format!("Texture {} dimensions invalid: {}x{}", t_idx, t.width, t.height),
            ));
        }
        if gpu_texture_token(t) {
            let entry = rtt_cache.active.get(&t.content_digest).ok_or_else(|| (
                CoinWgpuStatus::InvalidArgument,
                format!("Texture {} has stale or unknown RTT token", t_idx),
            ))?;
            let metadata_mismatch=if t.format==12 {
                let (n,_,mips)=cube_atlas_metadata(t)?.unwrap();
                entry.cube_face_size!=n || entry.cube_mipmapped!=mips || entry.format!=0 || entry.mip_count!=1
            } else {
                entry.cube_face_size!=0 || texture_metadata(t)&!31!=0 || (texture_metadata(t)&1!=0 && !entry.opaque) ||
                (texture_metadata(t)>>2)!=entry.format || (texture_metadata(t)&2!=0 && entry.mip_count<=1 && (t.width>1 || t.height>1))
            };
            if entry.width != t.width || entry.height != t.height || metadata_mismatch
                || !t.pixels.is_null() || t.pixel_bytes_len != 0 {
                return Err((CoinWgpuStatus::InvalidArgument,
                    format!("Texture {} RTT token metadata mismatch", t_idx)));
            }
            continue;
        }
        spatial_atlas_metadata(t)?;
        cube_atlas_metadata(t)?;
        let levels = texture_levels(t.width,t.height,t.format)?;
        let &(last_width,last_height,last_offset)=levels.last().unwrap();
        let gpu_format=texture_gpu_format(t)?;
        let expected_bytes=last_offset+texture_profile::level_bytes(last_width,last_height,gpu_format);
        if t.pixel_bytes_len != expected_bytes || t.pixels.is_null() {
            return Err((
                CoinWgpuStatus::InvalidArgument,
                format!("Texture {} pixel buffer mismatch: expected {} bytes, got {}", t_idx, expected_bytes, t.pixel_bytes_len),
            ));
        }

        let pixel_bytes = unsafe { std::slice::from_raw_parts(t.pixels, t.pixel_bytes_len as usize) };
        if gpu_format==wgpu::TextureFormat::Rgba16Float && pixel_bytes.chunks_exact(2).any(|p| u16::from_le_bytes([p[0],p[1]])&0x7c00==0x7c00) {
            return Err((CoinWgpuStatus::InvalidArgument,"Non-finite half texture payload".into()));
        }
        let key = TextureKey::from_texture(t);

        if let Some(entry) = tex_cache.entries.get_mut(&key) {
            if entry.pixels_copy == pixel_bytes {
                entry.last_submitted_serial = current_submission_serial;
                tex_cache.hits += 1;
                continue;
            }
        }

        let wgpu_tex = ctx.device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Coin Cached 2D Texture"),
            size: wgpu::Extent3d {
                width: t.width,
                height: t.height,
                depth_or_array_layers: 1,
            },
            mip_level_count: levels.len() as u32,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: gpu_format,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });

        for (level,&(level_width,level_height,offset)) in levels.iter().enumerate() {
        ctx.queue.write_texture(
            wgpu::ImageCopyTexture {
                texture: &wgpu_tex,
                mip_level: level as u32,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            &pixel_bytes[offset as usize..(offset+texture_profile::level_bytes(level_width,level_height,gpu_format)) as usize],
            wgpu::ImageDataLayout {
                offset: 0,
                bytes_per_row: Some(level_width.div_ceil(gpu_format.block_dimensions().0)*gpu_format.block_copy_size(None).unwrap()),
                rows_per_image: Some(level_height.div_ceil(gpu_format.block_dimensions().1)),
            },
            wgpu::Extent3d {
                width: level_width.div_ceil(gpu_format.block_dimensions().0)*gpu_format.block_dimensions().0,
                height: level_height.div_ceil(gpu_format.block_dimensions().1)*gpu_format.block_dimensions().1,
                depth_or_array_layers: 1,
            },
        );

        }
        let view = wgpu_tex.create_view(&wgpu::TextureViewDescriptor::default());
        tex_cache.uploads += 1;
        tex_cache.uploaded_bytes += pixel_bytes.len() as u64;
        let old = tex_cache.entries.insert(
            key,
            CachedTextureEntry {
                texture: wgpu_tex,
                view,
                pixels_copy: pixel_bytes.to_vec(),
                last_submitted_serial: current_submission_serial,
                size_bytes: pixel_bytes.len(),
            },
        );
        if let Some(old_entry) = old {
            tex_cache.retired.push(RetiredTexture {
                texture: old_entry.texture,
                retired_at_serial: current_submission_serial,
            });
        }
    }

    // Retain recent textures for reuse, but retire images no longer referenced by
    // the scene. The serial ties release to GPU completion, not CPU traversal.
    const MAX_STALE_TEXTURE_SERIALS: u64 = 8;
    let stale_textures: Vec<TextureKey> = tex_cache
        .entries
        .iter()
        .filter(|(_, entry)| {
            current_submission_serial.saturating_sub(entry.last_submitted_serial)
                > MAX_STALE_TEXTURE_SERIALS
        })
        .map(|(key, _)| key.clone())
        .collect();
    for key in stale_textures {
        if let Some(entry) = tex_cache.entries.remove(&key) {
            tex_cache.evictions += 1;
            tex_cache.retired.push(RetiredTexture {
                texture: entry.texture,
                retired_at_serial: entry.last_submitted_serial,
            });
        }
    }

    // Upload and cache frame samplers
    for s in samplers_slice {
        if s.filter>3 || s.wrap_s>1 || s.wrap_t>1 || s.reserved & !31 != 0 ||
            texture_profile::anisotropy(s.reserved & 15,s.filter).is_err() {
            return Err((CoinWgpuStatus::InvalidArgument,"Invalid P07 sampler".into()));
        }
        let key = SamplerKey {
            wrap_s: s.wrap_s,
            wrap_t: s.wrap_t,
            filter: s.filter,
            max_anisotropy_minus_one: s.reserved,
        };
        if !samp_cache.entries.contains_key(&key) {
            let addr_u = match s.wrap_s {
                0 => wgpu::AddressMode::Repeat,
                1 => wgpu::AddressMode::ClampToEdge,
                _ => wgpu::AddressMode::ClampToEdge,
            };
            let addr_v = match s.wrap_t {
                0 => wgpu::AddressMode::Repeat,
                1 => wgpu::AddressMode::ClampToEdge,
                _ => wgpu::AddressMode::ClampToEdge,
            };
            let filter_mode = match s.filter {
                0 => wgpu::FilterMode::Nearest,
                1 => wgpu::FilterMode::Linear,
                _ => wgpu::FilterMode::Linear,
            };
            let samp = ctx.device.create_sampler(&wgpu::SamplerDescriptor {
                label: Some("Coin Cached Sampler"),
                anisotropy_clamp: texture_profile::anisotropy(s.reserved & 15,s.filter)?,
                lod_max_clamp: if s.filter>=2 {32.0} else {0.0},
                address_mode_u: addr_u,
                address_mode_v: addr_v,
                address_mode_w: wgpu::AddressMode::Repeat,
                mag_filter: if s.filter>=2 {wgpu::FilterMode::Linear} else {filter_mode},
                min_filter: if s.filter==2 {wgpu::FilterMode::Nearest} else {filter_mode},
                mipmap_filter: if s.filter>=2 {wgpu::FilterMode::Linear} else {wgpu::FilterMode::Nearest},
                ..Default::default()
            });
            samp_cache.entries.insert(key, samp);
        }
    }

    // Evict stale entries unreferenced beyond threshold
    cache.evict_stale(ctx.last_submitted_serial.load(Ordering::SeqCst));

    // A clear-only pass has no draw/material. A draw still requires material slots.
    if materials_slice.is_empty() && !draws_slice.is_empty() {
        return Err((
            CoinWgpuStatus::InvalidArgument,
            "Frame with draws requires material_count > 0".to_string(),
        ));
    }

    let mat_buffer_size = match materials_slice.len().checked_mul(std::mem::size_of::<GpuMaterial>()) {
        Some(sz) => sz,
        None => {
            return Err((CoinWgpuStatus::InvalidArgument, "Overflow in material buffer size".to_string()));
        }
    };

    if mat_buffer_size as u64 > ctx.device.limits().max_storage_buffer_binding_size as u64 {
        return Err((CoinWgpuStatus::Unsupported, "Material buffer exceeds max_storage_buffer_binding_size".to_string()));
    }

    // An owned scene already validated this immutable vertex payload. Camera
    // frames update only states; scanning a million vertices would defeat reuse.
    let unchecked_vertices = if camera_geometry.is_none() { vertices_slice } else { &[] };
    for (v_idx, v) in unchecked_vertices.iter().enumerate() {
        if !v.texcoord.iter().chain(v.extra_texcoords.iter().flatten()).all(|x| x.is_finite())
            || !v.screen_space_w.is_finite() || v.screen_space_w < 0.0
            || !v.fog_eye_depth_plus_one.is_finite() || v.fog_eye_depth_plus_one < 0.0 {
            return Err((CoinWgpuStatus::InvalidArgument,
                format!("Vertex {} has invalid homogeneous texture/stroke attributes", v_idx)));
        }
        if (v.material_slot as usize) >= materials_slice.len() {
            return Err((CoinWgpuStatus::InvalidArgument, format!("Vertex {} references invalid material_slot {} >= material_count {}", v_idx, v.material_slot, materials_slice.len())));
        }
    }

    let trace_materials = std::env::var_os("COIN_RENDER_TRACE_PHASES")
        .or_else(|| std::env::var_os("COIN_WGPU_TRACE_PHASES")).is_some();
    let reuse_materials = material_resource_reuse_enabled();
    // The ablation restores eager packing at its original position, before
    // resource-cache locks or equality checks, as well as coupled GPU misses.
    let profile_eager_pack = trace_materials.then(std::time::Instant::now);
    let literal_gpu_materials = if reuse_materials { None }
        else { Some(pack_gpu_materials(materials_slice)) };
    let eager_pack_ms = profile_eager_pack.map(|start| start.elapsed().as_secs_f64() * 1000.0);
    // Keep the required GPU length independent of whether packing is needed.
    // A clear-only frame still binds the same one-element zero sentinel.
    let material_byte_len = mat_buffer_size.max(std::mem::size_of::<GpuMaterial>());
    let geometry_bytes = (std::mem::size_of_val(vertices_slice) as u64)
        .saturating_add(std::mem::size_of_val(indices_slice) as u64);
    let max_buffer_size = ctx.device.limits().max_buffer_size;
    let split_uncached = draws_slice.iter().any(|draw| draw.stable_node_id == 0 && draw.index_count > 0)
        && ((std::mem::size_of_val(vertices_slice) as u64) > max_buffer_size
            || (std::mem::size_of_val(indices_slice) as u64) > max_buffer_size);
    if split_uncached && trace_materials {
        eprintln!("COIN_RENDER_PHASE rust_uncached_split vertex_bytes={} index_bytes={} max_buffer_size={} draws={}",
            std::mem::size_of_val(vertices_slice), std::mem::size_of_val(indices_slice),
            max_buffer_size, draws_slice.iter().filter(|draw| draw.stable_node_id == 0 && draw.index_count > 0).count());
    }
    let retain_frame = !split_uncached && !instanced && frame_revision != 0 && !vertices_slice.is_empty()
        && !indices_slice.is_empty() && !draws_slice.is_empty()
        && draws_slice.iter().all(|draw| draw.stable_node_id == 0)
        && geometry_bytes.saturating_add(material_byte_len as u64) <= 256 * 1024 * 1024;
    let mut frame_buffers = ctx.frame_buffers.lock().unwrap();
    if !retain_frame || !frame_buffers.as_ref().is_some_and(|entry|
        (entry.revision == frame_revision || camera_geometry.is_some_and(|geometry|
            entry.geometry.as_ref().is_some_and(|cached| Arc::ptr_eq(cached, geometry))))
            && entry.vertex_count == vertices_slice.len()
            && entry.index_count == indices_slice.len() && entry.material_bytes == material_byte_len) {
        *frame_buffers = None;
    }
    let frame_buffer_hit = frame_buffers.is_some();

    // Keep the experiment switch process-local so the same binary can be
    // benchmarked against its previous behavior. It is opt-in, bounded and
    // applies only to the already-validated, untextured camera patch.
    static CAMERA_GPU_CACHE_ENABLED: OnceLock<bool> = OnceLock::new();
    let camera_binding_geometry = camera_geometry.filter(|_| shadow_frame.is_none()).filter(|_| {
        *CAMERA_GPU_CACHE_ENABLED.get_or_init(|| {
            std::env::var("COIN_WGPU_CAMERA_BINDINGS").as_deref() == Ok("1")
        })
        && !draws_slice.is_empty()
        && !materials_slice.is_empty()
        && draws_slice.len() <= 512
        && mat_buffer_size.saturating_add(draws_slice.len()
            .saturating_mul(uniform_binding_bytes(instanced, policy))) <= 4 * 1024 * 1024
    });
    let mut camera_bindings = ctx.camera_bindings.lock().unwrap();
    if !camera_binding_geometry.is_some_and(|geometry| camera_bindings.as_ref()
        .is_some_and(|entry| Arc::ptr_eq(&entry.geometry, geometry))) {
        *camera_bindings = None;
    }
    let previous_instances = ctx.instanced_buffers.lock().unwrap().clone();
    let owned_instance_hit = instanced && previous_instances.as_ref().is_some_and(|entry|
        camera_geometry.is_some_and(|geometry| Arc::ptr_eq(geometry, &entry.geometry)));
    let geometry_hit = owned_instance_hit || (instanced && previous_instances.as_ref().is_some_and(|entry|
        instancing::same_geometry(&entry.geometry, vertices_slice, indices_slice)));
    let profile_payload_compare = trace_materials.then(std::time::Instant::now);
    let (instance_hit, material_hit) = if owned_instance_hit { (true, true) }
        else if geometry_hit { previous_instances.as_ref().map(|entry|
            instancing::payload_hits(&entry.geometry, instances_slice, materials_slice, reuse_materials)).unwrap_or((false, false)) }
        else { (false, false) };
    let payload_compare_ms = profile_payload_compare.map(|start| start.elapsed().as_secs_f64() * 1000.0);
    // payload_hit retains its legacy meaning in the diagnostic event. Resource
    // decisions now use their own proof; optout restores the coupled behavior.
    let payload_hit = instance_hit && material_hit;
    let material_buffer_hit = material_hit || frame_buffers.is_some() || camera_bindings.is_some();
    let profile_material_pack = trace_materials.then(std::time::Instant::now);
    let gpu_materials = if let Some((_,_,values))=bump_binding {values.to_vec()} else if let Some(literal) = literal_gpu_materials { literal }
    else if !material_buffer_hit {
        pack_gpu_materials(materials_slice)
    } else { Vec::new() };
    let material_pack_ms = if reuse_materials {
        profile_material_pack.map(|start| start.elapsed().as_secs_f64() * 1000.0)
    } else { eager_pack_ms };
    // WebGPU does not permit a zero-sized storage binding. This sentinel is
    // unreachable because there are no draws in a material-free frame. A hit
    // never consumes material_bytes, so its empty scratch Vec is harmless.
    let fallback_material = [GpuMaterial::zeroed()];
    let material_bytes: &[u8] = if gpu_materials.is_empty() {
        bytemuck::cast_slice(&fallback_material)
    } else { bytemuck::cast_slice(&gpu_materials) };
    let profile_material_resources = trace_materials.then(std::time::Instant::now);
    let instances_buffer = if !instanced { None } else if instance_hit {
        Some(previous_instances.as_ref().unwrap().instances_buffer.clone())
    } else {
        let gpu_instances = instancing::pack_gpu(instances_slice);
        Some(multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
            label: Some("Coin compact affine instance storage"), contents: bytemuck::cast_slice(&gpu_instances),
            usage: wgpu::BufferUsages::STORAGE,
        }))
    };
    let materials_buffer = if material_hit {
        previous_instances.as_ref().unwrap().materials_buffer.clone()
    } else if let Some(entry) = frame_buffers.as_ref() {
        entry.materials_buffer.clone()
    } else if let Some(entry) = camera_bindings.as_ref() {
        ctx.camera_bindings_reused.fetch_add(1, Ordering::Relaxed);
        entry.materials_buffer.clone()
    } else {
        debug_assert_eq!(material_bytes.len(), material_byte_len);
        let buffer = multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
            label: Some("Frame Materials Storage Buffer"),
            contents: material_bytes,
            usage: wgpu::BufferUsages::STORAGE,
        });
        if let Some(geometry) = camera_binding_geometry {
            *camera_bindings = Some(CameraGpuBindings {
                geometry: geometry.clone(),
                materials_buffer: buffer.clone(),
            });
            ctx.camera_bindings_created.fetch_add(1, Ordering::Relaxed);
        }
        buffer
    };
    if let Some(start) = profile_material_resources {
        eprintln!("COIN_RENDER_PHASE rust_material_resources count={} source_bytes={} gpu_bytes={} packed_count={} packed_bytes={} payload_compare_ms={} pack_ms={} resource_ms={} instance_hit={} material_hit={} material_buffer_hit={} instance_allocations={} material_allocations={} instance_uploaded_bytes={} material_uploaded_bytes={} reuse_enabled={} attempt=1",
            materials_slice.len(), std::mem::size_of_val(materials_slice), material_byte_len,
            gpu_materials.len(), std::mem::size_of_val(gpu_materials.as_slice()),
            payload_compare_ms.unwrap(), material_pack_ms.unwrap(), start.elapsed().as_secs_f64() * 1000.0,
            instance_hit as u32, material_hit as u32, material_buffer_hit as u32,
            (instanced && !instance_hit) as u32, (!material_buffer_hit) as u32,
            if instanced && !instance_hit { instancing::gpu_bytes(instances_slice.len()) } else { 0 },
            if material_buffer_hit { 0 } else { material_byte_len }, reuse_materials as u32);
    }

    // Collect keys active in the current frame to strictly protect them from LRU eviction
    let mut frame_active_keys = std::collections::HashSet::new();
    for draw in draws_slice {
        if draw.index_count > 0 && draw.stable_node_id != 0 {
            frame_active_keys.insert((draw.stable_node_id, draw.draw_ordinal));
        }
    }

    // 2. Prepare uncached fallback buffers if there are any legacy/uncached draws
    let uncached_v_buffer = if split_uncached {
        None
    } else if geometry_hit {
        Some(previous_instances.as_ref().unwrap().vertex_buffer.clone())
    } else if let Some(entry) = frame_buffers.as_ref() {
        Some(entry.vertex_buffer.clone())
    } else if draws_slice.iter().any(|d| d.stable_node_id == 0) && !vertices_slice.is_empty() {
        Some(
            multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
                    label: Some("Coin Uncached Vertex Buffer"),
                    contents: bytemuck::cast_slice(vertices_slice),
                    usage: wgpu::BufferUsages::VERTEX,
                }),
        )
    } else {
        None
    };

    let uncached_i_buffer = if split_uncached {
        None
    } else if geometry_hit {
        Some(previous_instances.as_ref().unwrap().index_buffer.clone())
    } else if let Some(entry) = frame_buffers.as_ref() {
        Some(entry.index_buffer.clone())
    } else if draws_slice.iter().any(|d| d.stable_node_id == 0 && d.index_count > 0)
        && !indices_slice.is_empty()
    {
        Some(
            multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
                    label: Some("Coin Uncached Index Buffer"),
                    contents: bytemuck::cast_slice(indices_slice),
                    usage: wgpu::BufferUsages::INDEX,
                }),
        )
    } else {
        None
    };

    // A frame can exceed WebGPU's per-buffer limit even though each draw is
    // small. Keep the shared-buffer path for smaller frames and rebase only
    // the oversized frame's uncached draws into bounded buffers.
    let mut split_uncached_buffers: Vec<Option<(wgpu::Buffer, wgpu::Buffer)>> =
        if split_uncached { (0..draws_slice.len()).map(|_| None).collect() }
        else { Vec::new() };
    if split_uncached {
        for (draw_index, draw) in draws_slice.iter().enumerate() {
            if draw.stable_node_id != 0 || draw.index_count == 0 { continue; }
            let first_vertex = draw.first_vertex as usize;
            let vertex_end = first_vertex + draw.vertex_count as usize;
            let first_index = draw.first_index as usize;
            let index_end = first_index + draw.index_count as usize;
            let draw_vertices = &vertices_slice[first_vertex..vertex_end];
            let vertex_bytes = std::mem::size_of_val(draw_vertices) as u64;
            let index_bytes = (draw.index_count as u64) * std::mem::size_of::<u32>() as u64;
            if vertex_bytes > max_buffer_size || index_bytes > max_buffer_size {
                return Err((CoinWgpuStatus::Unsupported,
                    format!("Draw {} exceeds device max_buffer_size of {} bytes (vertices {}, indices {})",
                        draw_index, max_buffer_size, vertex_bytes, index_bytes)));
            }
            let local_indices: Vec<u32> = indices_slice[first_index..index_end].iter()
                .map(|&index| index.checked_sub(draw.first_vertex)
                    .filter(|&local| local < draw.vertex_count)
                    .ok_or_else(|| (CoinWgpuStatus::Unsupported,
                        format!("Draw {} references vertices outside its range; cannot split oversized frame", draw_index))))
                .collect::<Result<_, _>>()?;
            let vertex_buffer = multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
                label: Some("Coin Split Uncached Vertex Buffer"),
                contents: bytemuck::cast_slice(draw_vertices),
                usage: wgpu::BufferUsages::VERTEX,
            });
            let index_buffer = multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
                label: Some("Coin Split Uncached Index Buffer"),
                contents: bytemuck::cast_slice(&local_indices),
                usage: wgpu::BufferUsages::INDEX,
            });
            split_uncached_buffers[draw_index] = Some((vertex_buffer, index_buffer));
        }
    }

    if frame_buffer_hit {
        cache.cumulative_hits += 1;
        cache.frame_hits += 1;
    } else if retain_frame {
        *frame_buffers = Some(FrameGpuBuffers {
            revision: frame_revision, geometry: camera_geometry.cloned(), vertex_count: vertices_slice.len(),
            index_count: indices_slice.len(), material_bytes: material_byte_len,
            vertex_buffer: uncached_v_buffer.as_ref().unwrap().clone(),
            index_buffer: uncached_i_buffer.as_ref().unwrap().clone(),
            materials_buffer: materials_buffer.clone(), geometry_bytes,
        });
        cache.cumulative_uploads += 1;
        cache.frame_uploads += 1;
        cache.cumulative_uploaded_bytes += geometry_bytes;
        cache.frame_uploaded_bytes += geometry_bytes;
    }

    let pending_instances = if instanced {
        let geometry = camera_geometry.ok_or_else(|| (CoinWgpuStatus::OutOfMemory,
            "Cannot retain bounded validated instance payload".to_string()))?;
        let instance_bytes = instancing::gpu_bytes(instances_slice.len());
        let uploaded_instances = if instance_hit { 0 } else { instance_bytes };
        let uploaded_materials = if material_buffer_hit { 0 } else { material_byte_len as u64 };
        let uploaded = (if geometry_hit { 0 } else { geometry_bytes }) + uploaded_instances + uploaded_materials;
        if geometry_hit { cache.frame_hits += 1; cache.cumulative_hits += 1; }
        if uploaded != 0 {
            cache.frame_uploads += 1; cache.cumulative_uploads += 1;
            cache.frame_uploaded_bytes += uploaded; cache.cumulative_uploaded_bytes += uploaded;
        }
        if std::env::var_os("COIN_RENDER_TRACE_PHASES").or_else(|| std::env::var_os("COIN_WGPU_TRACE_PHASES")).is_some() {
            eprintln!("COIN_RENDER_PHASE rust_instances count={} ranges={} geometry_bytes={} instance_bytes={} material_bytes={} geometry_hit={} payload_hit={} instance_hit={} material_hit={} instance_uploaded_bytes={} material_uploaded_bytes={} uploaded_bytes={}",
                instances_slice.len(), instance_ranges.len(), geometry_bytes, instance_bytes,
                material_byte_len, geometry_hit as u32, payload_hit as u32, instance_hit as u32,
                material_hit as u32, uploaded_instances, uploaded_materials, uploaded);
        }
        Some(InstancedGpuBuffers { geometry: geometry.clone(),
            vertex_buffer: uncached_v_buffer.as_ref().unwrap().clone(),
            index_buffer: uncached_i_buffer.as_ref().unwrap().clone(),
            materials_buffer: materials_buffer.clone(),
            instances_buffer: instances_buffer.as_ref().unwrap().clone(),
            geometry_bytes, instance_bytes, material_bytes: material_byte_len as u64,
        })
    } else { None };

    // Temporary storage for oversized draws exceeding cache budget
    let mut oversized_entries: HashMap<(u64, u32), CachedGeometryEntry> = HashMap::new();

    // 3. Update cache for cached draws (stable_node_id != 0)
    for draw in draws_slice {
        if draw.index_count == 0 || draw.stable_node_id == 0 {
            continue;
        }

        let key = (draw.stable_node_id, draw.draw_ordinal);
        let hit = if let Some((rev, _entry)) = cache.active_entries.get(&key) {
            *rev == draw.source_revision
        } else {
            false
        };

        if hit {
            cache.cumulative_hits += 1;
            cache.frame_hits += 1;
            if let Some((_rev, entry)) = cache.active_entries.get_mut(&key) {
                entry.last_submitted_serial = current_submission_serial;
            }
        } else {
            // Check for simulated allocation failure (transactional rollback test)
            let fault = fault_state::current();
            if fault == FAULT_CACHE_ALLOC_FAIL {
                fault_state::clear();
                return Err((
                    CoinWgpuStatus::OutOfMemory,
                    "Simulated cache buffer allocation failure (OOM)".to_string(),
                ));
            }

            // Extract vertices for this draw
            let start_v = draw.first_vertex as usize;
            let end_v = start_v + draw.vertex_count as usize;
            if end_v > vertices_slice.len() {
                return Err((
                    CoinWgpuStatus::InvalidArgument,
                    format!(
                        "Cached draw vertex range out of bounds: {}..{} > {}",
                        start_v,
                        end_v,
                        vertices_slice.len()
                    ),
                ));
            }
            let draw_vertices = &vertices_slice[start_v..end_v];
            let v_size = (draw_vertices.len() * std::mem::size_of::<CoinWgpuVertex>()) as u64;

            // Extract and rebase indices for this draw
            let start_i = draw.first_index as usize;
            let end_i = start_i + draw.index_count as usize;
            if end_i > indices_slice.len() {
                return Err((
                    CoinWgpuStatus::InvalidArgument,
                    format!(
                        "Cached draw index range out of bounds: {}..{} > {}",
                        start_i,
                        end_i,
                        indices_slice.len()
                    ),
                ));
            }
            let raw_indices = &indices_slice[start_i..end_i];
            let local_indices: Vec<u32> = raw_indices
                .iter()
                .map(|&idx| idx.saturating_sub(draw.first_vertex))
                .collect();
            let i_size = (local_indices.len() * std::mem::size_of::<u32>()) as u64;
            let total_bytes = v_size + i_size;

            // Evict inactive entries to accommodate incoming bytes if needed
            cache.evict_for_budget(total_bytes, &frame_active_keys);

            // Transactional allocation: allocate buffers before touching existing cache entry
            let v_buf = multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
                label: Some("Coin Cached Draw Vertex Buffer"),
                contents: bytemuck::cast_slice(draw_vertices),
                usage: wgpu::BufferUsages::VERTEX,
            });
            let i_buf = multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
                label: Some("Coin Cached Draw Index Buffer"),
                contents: bytemuck::cast_slice(&local_indices),
                usage: wgpu::BufferUsages::INDEX,
            });

            // Evict previous revision safely if it existed, recording its true last_submitted_serial
            if let Some((_old_rev, old_entry)) = cache.active_entries.remove(&key) {
                cache.total_active_geometry_bytes = cache
                    .total_active_geometry_bytes
                    .saturating_sub(old_entry.size_bytes);
                cache.deferred_release.push(RetiredBuffer {
                    vertex_buffer: old_entry.vertex_buffer,
                    index_buffer: old_entry.index_buffer,
                    retired_at_serial: old_entry.last_submitted_serial,
                    size_bytes: old_entry.size_bytes,
                });
            }

            cache.cumulative_misses += 1;
            cache.cumulative_uploads += 1;
            cache.frame_uploads += 1;
            cache.cumulative_uploaded_bytes += total_bytes;
            cache.frame_uploaded_bytes += total_bytes;

            let new_entry = CachedGeometryEntry {
                vertex_buffer: v_buf,
                index_buffer: Some(i_buf),
                vertex_count: draw.vertex_count,
                index_count: draw.index_count,
                size_bytes: total_bytes,
                last_submitted_serial: current_submission_serial,
            };

            if total_bytes > cache.max_active_geometry_bytes {
                // Oversized geometry larger than entire budget: render in current frame but do not retain
                oversized_entries.insert(key, new_entry);
            } else {
                cache.total_active_geometry_bytes += total_bytes;
                cache.active_entries.insert(key, (draw.source_revision, new_entry));
            }
        }
    }

    // 4. Encode base composition followed by immediate annotation layers.
    let mut encoder = ctx
        .device
        .create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("Coin Frame Encoder"),
        });
    let raster_images = if let Some(images)=MULTIPASS_UPLOADS.with(|slot|
        slot.borrow().as_ref().and_then(|replay|replay.raster.clone())) {images}
    else {
        let images=Arc::new(raster_image::encode(ctx, &mut encoder, states_slice,
            textures_slice, samplers_slice, &tex_cache, &samp_cache, &rtt_cache)?);
        MULTIPASS_UPLOADS.with(|slot| {
            if let Some(replay)=slot.borrow_mut().as_mut() {replay.raster=Some(images.clone());}
        });
        images
    };
    let shadow_map = if let Some((maps, _)) = shared_bump_shadow { maps.first().cloned() } else if let Some(shadow) = shadow_frame {
        if fault_state::current() == FAULT_SHADOW_MAP_ALLOC {
            fault_state::clear();
            return Err((CoinWgpuStatus::OutOfMemory,
                "Injected shadow-map attachment allocation failure".into()));
        }
        Some(shadow::encode_moments(&ctx.device, &mut encoder, vertices_slice,
            indices_slice, shadow.casters, states_slice, shadow.map_size, shadow.near, shadow.far, shadow.kind, rasterization_policy == 1)
            .map_err(|msg| (CoinWgpuStatus::InvalidArgument, msg))?)
    } else { None };
    let shadow_map_second = if let Some((maps,_))=shared_bump_shadow { maps.get(1).cloned() } else if let Some(second) = shadow_frame.and_then(|shadow| shadow.second.as_ref()) {
        Some(shadow::encode_moments(&ctx.device, &mut encoder, vertices_slice,
            indices_slice, second.casters, states_slice, second.map_size, second.near, second.far, second.kind, rasterization_policy == 1)
            .map_err(|msg| (CoinWgpuStatus::InvalidArgument, msg))?)
    } else { None };

    let shadow_map_third = if let Some((maps,_))=shared_bump_shadow { maps.get(2).cloned() } else if let Some(pass) = shadow_frame.and_then(|shadow| shadow.third.as_ref()) {
        Some(shadow::encode_moments(&ctx.device, &mut encoder, vertices_slice,
            indices_slice, pass.casters, states_slice, pass.map_size, pass.near, pass.far, pass.kind, rasterization_policy == 1)
            .map_err(|msg| (CoinWgpuStatus::InvalidArgument, msg))?)
    } else { None };
    let shadow_map_fourth = if let Some((maps,_))=shared_bump_shadow { maps.get(3).cloned() } else if let Some(pass) = shadow_frame.and_then(|shadow| shadow.fourth.as_ref()) {
        Some(shadow::encode_moments(&ctx.device, &mut encoder, vertices_slice,
            indices_slice, pass.casters, states_slice, pass.map_size, pass.near, pass.far, pass.kind, rasterization_policy == 1)
            .map_err(|msg| (CoinWgpuStatus::InvalidArgument, msg))?)
    } else { None };
    let mut extra_shadow_maps = Vec::new();
    if let Some((maps,_))=shared_bump_shadow {
        extra_shadow_maps.extend(maps.iter().skip(4).cloned());
    } else if let Some(shadow) = shadow_frame {
        for pass in &shadow.extra {
            extra_shadow_maps.push(shadow::encode_moments(&ctx.device, &mut encoder,
                vertices_slice, indices_slice, pass.casters, states_slice,
                pass.map_size, pass.near, pass.far, pass.kind, rasterization_policy == 1)
                .map_err(|msg| (CoinWgpuStatus::InvalidArgument, msg))?);
        }
    }
    let shadow_array = if shadow_multitexture && shared_bump_shadow.is_some() {
        Some(shared_bump_shadow.unwrap().1.clone())
    } else if shadow_multitexture {
        let maps: Vec<&shadow::ShadowMap> = [shadow_map.as_ref(), shadow_map_second.as_ref(),
            shadow_map_third.as_ref(), shadow_map_fourth.as_ref()].into_iter().flatten()
            .chain(extra_shadow_maps.iter()).collect();
        Some(shadow::encode_array(&ctx.device, &mut encoder, &maps)
            .map_err(|message| (CoinWgpuStatus::Unsupported, message))?)
    } else { None };
    let load_initial_depth = manager_backgrounds(draws_slice);
    if clear_attachments {
        let _clear = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("Coin initial attachments clear"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view: color_view,
                resolve_target: None,
                ops: wgpu::Operations {
                    load: wgpu::LoadOp::Clear(wgpu::Color {
                        r: clear_color[0] as f64,
                        g: clear_color[1] as f64,
                        b: clear_color[2] as f64,
                        a: clear_color[3] as f64,
                    }),
                    store: wgpu::StoreOp::Store,
                },
            })],
            depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                view: depth_view,
                depth_ops: Some(wgpu::Operations {
                    load: if load_initial_depth { wgpu::LoadOp::Load } else { wgpu::LoadOp::Clear(1.0) },
                    store: wgpu::StoreOp::Store,
                }),
                stencil_ops: None,
            }),
            timestamp_writes: timestamp_query.map(|query_set| wgpu::RenderPassTimestampWrites {
                query_set,
                beginning_of_pass_write_index: Some(0),
                end_of_pass_write_index: None,
            }),
            occlusion_query_set: None,
        });
    }
    let peeling = draw_order.iter().any(|item| item.peel).then(|| {
        MULTIPASS_UPLOADS.with(|slot| {
            let mut slot=slot.borrow_mut();
            let create=||Arc::new(peeling::Peeling::new_samples(&ctx.device,target_width,target_height,
                color_format,peel_passes as usize,ctx.adapter.get_info().backend==wgpu::Backend::Gl,sample_count));
            if let Some(replay)=slot.as_mut() {
                replay.peeling.entry((target_width,target_height,color_format,peel_passes as usize,sample_count))
                    .or_insert_with(create).clone()
            } else {create()}
        })
    });
    let weighted=draw_order.iter().any(|item|item.weighted).then(|| {
        MULTIPASS_UPLOADS.with(|slot| {
            let mut slot=slot.borrow_mut();
            let create=||Arc::new(weighted::Weighted::new_samples(&ctx.device,target_width,target_height,color_format,sample_count));
            if let Some(replay)=slot.as_mut() {
                replay.weighted.entry((target_width,target_height,color_format,sample_count)).or_insert_with(create).clone()
            } else {create()}
        })
    });
    let passes = composition::passes(draw_order, draws_slice);
    let uniform_records = draw_order.iter().try_fold(0usize, |count, item| {
        count.checked_add(if item.peel { peel_passes as usize } else { 1 })
    }).ok_or_else(|| (CoinWgpuStatus::InvalidArgument, "Draw uniform count overflow".into()))?;
    let uniform_size = uniform_binding_bytes(instanced, policy);
    let sampling_audit = std::env::var_os("COIN_SAMPLING_AUDIT").is_some();
    let mut uniform_arena = uniform_arena::UniformArena::new(&ctx.device, uniform_records, uniform_size);
    // Resources are stable while the texture/RTT caches are locked. A disabled
    // unit always binds the same defaults; unused eight-shadow units are omitted.
    let mut draw_bindings: HashMap<(usize, [(u32, u32); 8], usize, u32), wgpu::BindGroup> = HashMap::new();
    // A distinct immutable buffer for each occurrence prevents later runs from
    // overwriting parameters referenced by an earlier command in one submit.
    let mut module_buffers = Vec::with_capacity(portable_modules.len());
    for module in portable_modules {
        let mut parameters = [0.0f32;1024];
        parameters[..module.parameters.len()].copy_from_slice(&module.parameters);
        module_buffers.push(multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
            label: Some("Coin portable shader occurrence parameters"),
            contents: bytemuck::cast_slice(&parameters), usage: wgpu::BufferUsages::UNIFORM,
        }));
    }
    let depth_clear_pipeline = if draws_slice.iter().any(|draw| draw.clear_depth_before != 0) {
        let mut cached = ctx.annotation_depth_pipeline.lock().unwrap();
        Some(
            cached
                .entry(sample_count)
                .or_insert_with(|| annotation_depth_clear_pipeline(&ctx.device, sample_count))
                .clone(),
        )
    } else {
        None
    };
    for (pass_index, range) in passes.iter().enumerate() {
        if let Some(item) = draw_order.get(range.start) {
            let draw = &draws_slice[item.draw_index];
            // A scene's opaque/deferred sorting may move its captured barrier.
            // Execute it once at layer entry, using the original clear viewport.
            let entering_layer = pass_index == 0 || passes.get(pass_index - 1)
                .and_then(|previous| draw_order.get(previous.start))
                .is_some_and(|previous| draws_slice[previous.draw_index].render_layer != draw.render_layer);
            let barrier = entering_layer.then(|| draws_slice.iter().find(|candidate|
                candidate.render_layer == draw.render_layer && candidate.clear_depth_before != 0)).flatten();
            if let Some(draw) = barrier.filter(|barrier|
                states_slice[barrier.render_state_slot as usize].viewport != [0, 0, 0, 1]) {
                let viewport = resolved_viewport(
                    &states_slice[draw.render_state_slot as usize],
                    target_width,
                    target_height,
                )?;
                let mut clear_pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                    label: Some("Coin Annotation Depth Clear"),
                    color_attachments: &[],
                    depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                        view: depth_view,
                        depth_ops: Some(wgpu::Operations {
                            load: wgpu::LoadOp::Load,
                            store: wgpu::StoreOp::Store,
                        }),
                        stencil_ops: None,
                    }),
                    timestamp_writes: None,
                    occlusion_query_set: None,
                });
                clear_pass.set_pipeline(depth_clear_pipeline.as_ref().unwrap());
                clear_pass.set_scissor_rect(
                    viewport[0] as u32,
                    viewport[1] as u32,
                    viewport[2] as u32,
                    viewport[3] as u32,
                );
                clear_pass.draw(0..3, 0..1);
            }
        }
        let is_peel = draw_order.get(range.start).is_some_and(|item| item.peel);
        let is_weighted = draw_order.get(range.start).is_some_and(|item|item.weighted);
        let sample_depth = peeling.is_some() && sample_count > 1;
        let profile = if sample_depth {
            if shadow_multitexture { ctx.shadow_multitexture_peeling_surface_profile(policy) }
            else { ctx.peeling_surface_profile(policy) }
        } else { profile };
        if is_peel {
            if sample_count > 1 {
                peeling.as_ref().unwrap().snapshot_depth(&ctx.device, &mut encoder, depth_view);
            } else if ctx.adapter.get_info().backend == wgpu::Backend::Gl {
                let mut converter = ctx.depth_snapshot.lock().unwrap();
                let converter = converter.get_or_insert_with(|| depth_transfer::DepthTransfer::new_gl(&ctx.device, true));
                converter.copy_depth(&ctx.device, &mut encoder, depth_view, &peeling.as_ref().unwrap().opaque_view);
            } else {
                encoder.copy_texture_to_texture(
                    wgpu::TexelCopyTextureInfo {
                        texture: depth_texture,
                        mip_level: 0,
                        origin: wgpu::Origin3d::ZERO,
                        aspect: wgpu::TextureAspect::DepthOnly,
                    },
                    wgpu::TexelCopyTextureInfo {
                        texture: &peeling.as_ref().unwrap().opaque,
                        mip_level: 0,
                        origin: wgpu::Origin3d::ZERO,
                        aspect: wgpu::TextureAspect::DepthOnly,
                    },
                    wgpu::Extent3d {
                        width: target_width,
                        height: target_height,
                        depth_or_array_layers: 1,
                    },
                );
            }
        }
        for peel_step in 0..if is_peel { peel_passes as usize } else { 1 } {
            let selected_color = if is_weighted { &weighted.as_ref().unwrap().accumulation } else if is_peel {
                &peeling.as_ref().unwrap().colors[peel_step]
            } else {
                color_view
            };
            let selected_depth = if is_peel {
                &peeling.as_ref().unwrap().depths[peel_step]
            } else {
                depth_view
            };
            let mut color_attachments = vec![Some(wgpu::RenderPassColorAttachment {
                view: selected_color,
                // Resolve together with the final composed draw. A separate
                // empty pass can discard the previously rendered MSAA color
                // on Vulkan implementations even when its load op is Load.
                resolve_target: if !is_peel && !is_weighted &&
                    pass_index + 1 == passes.len() && peel_step == 0 {
                    resolve_view
                } else { None },
                ops: wgpu::Operations {
                    load: if is_peel || is_weighted {
                        wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT)
                    } else if sample_count > 1 && pass_index == 0 && clear_attachments {
                        wgpu::LoadOp::Clear(wgpu::Color {
                            r: clear_color[0] as f64, g: clear_color[1] as f64,
                            b: clear_color[2] as f64, a: clear_color[3] as f64,
                        })
                    } else {
                        wgpu::LoadOp::Load
                    },
                    store: wgpu::StoreOp::Store,
                },
            })];
            if is_peel {
                color_attachments.push(Some(wgpu::RenderPassColorAttachment {
                    view: &peeling.as_ref().unwrap().masks[peel_step],
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT),
                        store: wgpu::StoreOp::Store,
                    },
                }));
            }
            if is_weighted {
                color_attachments.push(Some(wgpu::RenderPassColorAttachment {
                    view:&weighted.as_ref().unwrap().revealage,resolve_target:None,
                    ops:wgpu::Operations{load:wgpu::LoadOp::Clear(wgpu::Color::WHITE),store:wgpu::StoreOp::Store},
                }));
            }
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("Coin Composition Pass"),
                color_attachments: &color_attachments,
                depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                    view: selected_depth,
                    depth_ops: Some(wgpu::Operations {
                        load: if is_peel || (sample_count > 1 && pass_index == 0 && clear_attachments && !load_initial_depth) {
                            wgpu::LoadOp::Clear(1.0)
                        } else {
                            wgpu::LoadOp::Load
                        },
                        store: wgpu::StoreOp::Store,
                    }),
                    stencil_ops: None,
                }),
                timestamp_writes: if !is_peel && !is_weighted && pass_index + 1 == passes.len() {
                    timestamp_query.map(|query_set| wgpu::RenderPassTimestampWrites {
                        query_set,
                        beginning_of_pass_write_index: None,
                        end_of_pass_write_index: Some(1),
                    })
                } else {
                    None
                },
                occlusion_query_set: None,
            });

            for item in &draw_order[range.clone()] {
                let draw = &draws_slice[item.draw_index];
                if draw.index_count == 0 {
                    continue;
                }
                let st = &states_slice[draw.render_state_slot as usize];
                if st.viewport == [0, 0, 0, 1] { continue; }
                let mat = &materials_slice[st.material_slot as usize];

                let cull_face = match st.cull_mode {
                    0 => None,
                    2 => Some(wgpu::Face::Front),
                    _ => Some(wgpu::Face::Back),
                };
                let front_face = match st.front_face {
                    1 => wgpu::FrontFace::Cw,
                    _ => wgpu::FrontFace::Ccw,
                };

                let depth_compare = if st.depth_test == 0 {
                    wgpu::CompareFunction::Always
                } else {
                    match st.depth_function {
                        0 => wgpu::CompareFunction::Never,
                        1 => wgpu::CompareFunction::Always,
                        3 => wgpu::CompareFunction::LessEqual,
                        4 => wgpu::CompareFunction::Equal,
                        5 => wgpu::CompareFunction::GreaterEqual,
                        6 => wgpu::CompareFunction::Greater,
                        7 => wgpu::CompareFunction::NotEqual,
                        _ => wgpu::CompareFunction::Less,
                    }
                };
                let resolved_offset = st.polygon_offset_enabled != 0
                    && st.polygon_offset_styles & st.polygon_offset_primitive_style != 0
                    && (st.polygon_offset_slope_bias != 0.0
                        || (st.polygon_offset_max_depth_bits != 0
                            && st.polygon_offset_units != 0.0));
                // WebGPU viewport intervals must be ascending. The existing
                // fragment-depth path maps a reversed Coin interval without
                // changing clipping or projection.
                // Deferred selectors/weighted fragments map custom intervals in
                // the shader. Coplanar 1x writers must use that same expression,
                // since native viewport mapping can round it differently. At the
                // default 0..1 interval with zero bias, mix(0,1,FragCoord.z) is
                // exactly FragCoord.z; retain ordinary early depth there. MSAA
                // weighted keeps its existing shared sample-depth delivery.
                let shared_shadow_shader_depth = shadow_multitexture && draw_order.iter()
                    .any(|item| (sample_count > 1 && item.weighted) ||
                        (sample_count == 1 && st.depth_range != [0.0,1.0] && (item.weighted || item.peel)));
                let resolved_depth_bias = shared_shadow_shader_depth || sample_depth || resolved_offset || st.depth_range[0] > st.depth_range[1] || rasterization_policy == 1 ||
                    (shadow_multitexture && sample_count > 1 && custom_shadow_sample_depth(st));
                // Ordinary MSAA uses hardware depth and must not introduce a
                // sample-interpolated module on devices that lack sample shading.
                // Only the fragment-depth entry point needs the private profile;
                // peeling already selected its multisampled-resource layout above.
                let profile = if sample_count > 1 && !sample_depth && (!is_weighted || shadow_multitexture) && resolved_depth_bias {
                    if shadow_multitexture { ctx.shadow_multitexture_depth_surface_profile(policy) }
                    else { ctx.sample_depth_surface_profile(policy) }
                } else { profile };
                let map_mode = shadow_frame.map_or(0, |shadow| if shadow.extra.is_empty() {1} else {2});
                let module_profile = if st.portable_shader_slot != 0 {
                    let module = portable_modules.get(st.portable_shader_slot as usize-1)
                        .ok_or_else(|| (CoinWgpuStatus::InvalidArgument,"Invalid portable shader draw occurrence".into()))?;
                    Some(portable::profile(ctx,module,profile,sample_count,map_mode)?)
                } else { None };
                let profile = module_profile.as_ref().map_or(profile,|profile|profile.as_ref());
                let native_depth_bias = polygon_depth_bias(st, draw.topology).map_err(|e| {
                    (
                        CoinWgpuStatus::InvalidArgument,
                        format!("Draw {}: {}", item.draw_index, e),
                    )
                })?;
                let window_bias = if resolved_offset {
                    resolved_polygon_bias(st, draw, vertices_slice, indices_slice).map_err(|e| {
                        (
                            CoinWgpuStatus::InvalidArgument,
                            format!("Draw {}: {}", item.draw_index, e),
                        )
                    })?
                } else {
                    0.0
                };
                let depth_bias = if resolved_depth_bias {
                    wgpu::DepthBiasState::default()
                } else {
                    native_depth_bias
                };
                let eight_shadows = shadow_frame.is_some_and(|s| !s.extra.is_empty());
                if bump_binding.is_some() && trace_materials {
                    eprintln!("COIN_RENDER_PHASE wgpu_bump_run_profile render_state_slot={} light_model={} light_count={} profile_bump_receiver={} shadow_frame={} bump_binding={} portable_shader_slot={} material_slot={}",
                        draw.render_state_slot, st.light_model, st.light_count,
                        profile.bump_receiver, shadow_frame.is_some(), bump_binding.is_some(),
                        st.portable_shader_slot, st.material_slot);
                }
                let pipeline = match get_or_create_pipeline(
                    ctx, profile,
                    draw.topology,
                    if is_peel || is_weighted {
                        wgpu::TextureFormat::Rgba16Float
                    } else {
                        color_format
                    },
                    wgpu::TextureFormat::Depth32Float,
                    sample_count,
                    cull_face,
                    front_face,
                    item.blend && !is_peel && !is_weighted,
                    item.additive,
                    item.legacy_blend_alpha,
                    is_peel,
                    is_peel || (!is_weighted && st.depth_test != 0 && st.depth_write != 0),
                    st.color_write,
                    if is_peel {
                        wgpu::CompareFunction::LessEqual
                    } else {
                        depth_compare
                    },
                    depth_bias,
                    resolved_depth_bias,
                    eight_shadows,
                    shadow_frame.is_some(),
                    is_weighted,
                    st.portable_shader_slot != 0 || bump_binding.is_some() || st.has_texture != 0 || st.extra_textures.iter().any(|unit| unit.enabled != 0),
                    instanced,
                ) {
                    Ok(p) => p,
                    Err(e) => return Err((CoinWgpuStatus::BackendError, e)),
                };
                pass.set_pipeline(&pipeline);

                let mut mv: [[f32; 4]; 4] = [[0.0; 4]; 4];
                let mut mvp: [[f32; 4]; 4] = [[0.0; 4]; 4];
                let mut nm: [[f32; 4]; 4] = [[0.0; 4]; 4];
                for c in 0..4 {
                    for r in 0..4 {
                        mv[c][r] = st.model_view[c * 4 + r];
                        mvp[c][r] = st.model_view_projection[c * 4 + r];
                        nm[c][r] = st.normal_matrix[c * 4 + r];
                    }
                }

                let layers: [CoinWgpuTextureUnit; 8] = std::array::from_fn(|unit| {
                    if unit == 0 {
                        CoinWgpuTextureUnit {
                            matrix: st.texture_matrix,
                            enabled: st.has_texture,
                            texture_slot: st.texture_slot,
                            sampler_slot: st.sampler_slot,
                            model: st.texture_model,
                            blend_color: st.texture_blend_color,
                        }
                    } else {
                        st.extra_textures[unit - 1]
                    }
                });
                let texture_bindings: Vec<_> = layers
                    .iter().enumerate()
                    .map(|(unit, layer)| {
                        if unit == 0 {
                            if let Some(image) = raster_images.get(&(draw.render_state_slot as usize)) {
                                return (&image.view, &image.sampler,
                                    [image.width as f32, image.height as f32, 0.0, 3.0]);
                            }
                        }
                        if layer.enabled == 0 {
                            return (&ctx.default_texture_view, &ctx.default_sampler, [1.0, 1.0, 0.0, 0.0]);
                        }
                        let t = &textures_slice[layer.texture_slot as usize];
                        let s = &samplers_slice[layer.sampler_slot as usize];
                        let tk = TextureKey::from_texture(t);
                        let sk = SamplerKey {
                            wrap_s: s.wrap_s,
                            wrap_t: s.wrap_t,
                            filter: s.filter,
            max_anisotropy_minus_one: s.reserved,
                        };
                        let (view, width, height) = if gpu_texture_token(t) {
                            let resource = rtt_cache.active.get(&t.content_digest)
                                .expect("RTT token preflighted before encoding");
                            (&resource.view, if policy == SamplingPolicy::Portable {resource.width} else {1}, if policy == SamplingPolicy::Portable {resource.height} else {1})
                        } else if let Some(entry) = tex_cache.entries.get(&tk) {
                            (&entry.view, if policy == SamplingPolicy::Portable {entry.texture.width()} else {1}, if policy == SamplingPolicy::Portable {entry.texture.height()} else {1})
                        } else { (&ctx.default_texture_view, 1, 1) };
                        let supplied = if t.format == 12 {
                            let (n,_,mips)=cube_atlas_metadata(t).expect("cube atlas preflighted").unwrap();
                            [n as f32,if mips{1.0}else{0.0},(8+s.filter) as f32,0.0]
                        } else if t.format == 11 {
                            let (w,h,d,mips)=spatial_atlas_metadata(t).expect("spatial atlas preflighted")
                                .expect("spatial atlas format");
                            let flags=s.wrap_s+2*s.wrap_t+4*((s.reserved>>4)&1)+
                                if mips {8} else {0};
                            [w as f32,h as f32,(4+s.filter) as f32,(d*16+flags) as f32]
                        } else {
                            [width as f32,height as f32,s.filter as f32,
                             (s.wrap_s+2*s.wrap_t) as f32]
                        };
                        (view, samp_cache.entries.get(&sk).unwrap_or(&ctx.default_sampler),supplied)
                    })
                    .collect();
                let extra_texture_matrices = std::array::from_fn(|unit| {
                    std::array::from_fn(|c| {
                        std::array::from_fn(|row| st.extra_textures[unit].matrix[c * 4 + row])
                    })
                });
                let extra_tex_params = std::array::from_fn(|unit| {
                    let t = st.extra_textures[unit];
                    [
                        if t.enabled != 0 { t.enabled as f32 } else { 0.0 },
                        t.model as f32,
                        if t.enabled != 0 && textures_slice[t.texture_slot as usize].format == 1 {
                            1.0
                        } else {
                            0.0
                        },
                        st.texture_projection as f32,
                    ]
                });
                let extra_texture_blends =
                    std::array::from_fn(|unit| st.extra_textures[unit].blend_color);

                let mut tex_mat: [[f32; 4]; 4] = [[0.0; 4]; 4];
                for c in 0..4 {
                    for r in 0..4 {
                        tex_mat[c][r] = st.texture_matrix[c * 4 + r];
                    }
                }

                let receiver = shadow_frame.map(|shadow| {
                    (&shadow.receivers[draw.render_state_slot as usize], shadow)
                });
                let receiver_second = shadow_frame.and_then(|shadow| shadow.second.as_ref())
                    .map(|second| (&second.receivers[draw.render_state_slot as usize], second));
                let receiver_third = shadow_frame.and_then(|shadow| shadow.third.as_ref())
                    .map(|pass| (&pass.receivers[draw.render_state_slot as usize], pass));
                let receiver_fourth = shadow_frame.and_then(|shadow| shadow.fourth.as_ref())
                    .map(|pass| (&pass.receivers[draw.render_state_slot as usize], pass));
                let shadow_matrix = |flat: &[f32; 16]| -> [[f32; 4]; 4] {
                    std::array::from_fn(|col| std::array::from_fn(|row| flat[col * 4 + row]))
                };
                let quality_flags = shadow_frame.map_or(0, |shadow| {
                    let slot = draw.render_state_slot as usize;
                    let mut flags = shadow.receivers[slot].receives;
                    for pass in shadow.second.iter().chain(shadow.third.iter()).chain(shadow.fourth.iter()).chain(shadow.extra.iter()) {
                        flags |= pass.receivers[slot].receives;
                    }
                    flags
                });
                let uniforms = CoinWgpuUniforms {
                    alpha_test: [st.alpha_test_function as f32, st.alpha_test_reference, 0.0, 0.0],
                    raster_viewport: if rasterization_policy == 1 {
                        let viewport = resolved_viewport(st, target_width, target_height)?;
                        [viewport[0] as f32, (target_height as i32 - viewport[1] - viewport[3]) as f32,
                         viewport[2] as f32, viewport[3] as f32]
                    } else { [0.0; 4] },
                    sampling_texture_sizes: std::array::from_fn(|unit| texture_bindings[unit].2),
                    flat_shadow_primary: [st.material_slot, st.flat_shadow_primary, 0, 0],
                    model_view_projection: mvp,
                    model_view: mv,
                    normal_matrix: nm,
                    material_diffuse: mat.diffuse,
                    material_ambient: mat.ambient,
                    material_specular: mat.specular,
                    light_direction_intensity: [
                        st.light_direction[0],
                        st.light_direction[1],
                        st.light_direction[2],
                        st.light_intensity,
                    ],
                    light_color: st.light_color,
                    params: [
                        mat.shininess,
                        0.0,
                        if st.has_light != 0 { 1.0 } else { 0.0 },
                        st.light_model as f32,
                    ],
                    texture_matrix: tex_mat,
                    extra_texture_matrices,
                    extra_tex_params,
                    extra_texture_blends,
                    texture_combines: st.texture_combines,
                    composition_meta: [
                        item.screen_door_level as f32,
                        if item.screen_door { 1.0 } else { 0.0 },
                        target_height as f32,
                        0.0,
                    ],
                    peel_meta: [
                        if is_peel { (peel_step + 1) as f32 } else { 0.0 },
                        st.depth_test as f32,
                        st.depth_function as f32,
                        if st.depth_test != 0 && st.depth_write != 0 { 1.0 } else { 0.0 },
                    ],
                    tex_params: [
                        if st.has_texture != 0 { st.has_texture as f32 } else { 0.0 },
                        st.texture_model as f32,
                        if st.has_texture != 0
                            && textures_slice[st.texture_slot as usize].format == 1
                        {
                            1.0
                        } else {
                            0.0
                        },
                        st.texture_projection as f32,
                    ],
                    ambient_light: st.ambient_light,
                    light_meta: [st.light_count as f32, 0.0,
                        ((quality_flags & 4) != 0) as u32 as f32, ((quality_flags & 8) != 0) as u32 as f32],
                    texture_blend_color: st.texture_blend_color,
                    lights: st.lights,
                    clip_meta: [
                        st.clip_plane_count as f32,
                        window_bias,
                        st.depth_range[0],
                        st.depth_range[1],
                    ],
                    clip_planes: st.clip_planes,
                    fog_color_mode: [
                        st.fog_color[0],
                        st.fog_color[1],
                        st.fog_color[2],
                        st.fog_mode as f32,
                    ],
                    fog_range: [st.fog_start, st.fog_end, 0.0, 0.0],
                    shadow_model_view_projection: receiver.map_or(
                        [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                         [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                        |(r, _)| shadow_matrix(&r.model_view_projection)),
                    shadow_model_view: receiver.map_or(
                        [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                         [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                        |(r, _)| shadow_matrix(&r.model_view)),
                    shadow_params: receiver.map_or([0.0, 0.0, 1.0, 0.0],
                        |(r, shadow)| [(r.receives & 1) as f32, shadow.near,
                            shadow.far, shadow.epsilon]),
                    shadow_meta: receiver.map_or([0.0; 4], |(r, shadow)|
                        [shadow.threshold, r.lighting_index as f32, shadow.kind as f32,
                         r.max_shadow_distance]),
                    shadow_falloff: receiver.map_or([0.0; 4], |(r, map)|
                        [r.distance_falloff_coefficient, ((r.receives & 2) != 0) as u32 as f32, map.map_size as f32, 0.0]),
                    shadow_model_view_projection_second: receiver_second.map_or(
                        [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                         [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                        |(r, _)| shadow_matrix(&r.model_view_projection)),
                    shadow_model_view_second: receiver_second.map_or(
                        [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                         [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                        |(r, _)| shadow_matrix(&r.model_view)),
                    shadow_params_second: receiver_second.map_or([0.0, 0.0, 1.0, 0.0],
                        |(r, second)| [(r.receives & 1) as f32, second.near,
                            second.far, second.epsilon]),
                    shadow_meta_second: receiver_second.map_or([0.0; 4], |(r, second)|
                        [second.threshold, r.lighting_index as f32,
                         second.kind as f32, r.max_shadow_distance]),
                    shadow_falloff_second: receiver_second.map_or([0.0; 4], |(r, map)|
                        [r.distance_falloff_coefficient, ((r.receives & 2) != 0) as u32 as f32, map.map_size as f32, 0.0]),
                    shadow_model_view_projection_third: receiver_third.map_or(
                        [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                         [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                        |(r, _)| shadow_matrix(&r.model_view_projection)),
                    shadow_model_view_third: receiver_third.map_or(
                        [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                         [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                        |(r, _)| shadow_matrix(&r.model_view)),
                    shadow_params_third: receiver_third.map_or([0.0, 0.0, 1.0, 0.0],
                        |(r, pass)| [(r.receives & 1) as f32, pass.near,
                            pass.far, pass.epsilon]),
                    shadow_meta_third: receiver_third.map_or([0.0; 4], |(r, pass)|
                        [pass.threshold, r.lighting_index as f32,
                         pass.kind as f32, r.max_shadow_distance]),
                    shadow_falloff_third: receiver_third.map_or([0.0; 4], |(r, map)|
                        [r.distance_falloff_coefficient, ((r.receives & 2) != 0) as u32 as f32, map.map_size as f32, 0.0]),
                    shadow_model_view_projection_fourth: receiver_fourth.map_or(
                        [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                         [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                        |(r, _)| shadow_matrix(&r.model_view_projection)),
                    shadow_model_view_fourth: receiver_fourth.map_or(
                        [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                         [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                        |(r, _)| shadow_matrix(&r.model_view)),
                    shadow_params_fourth: receiver_fourth.map_or([0.0, 0.0, 1.0, 0.0],
                        |(r, pass)| [(r.receives & 1) as f32, pass.near,
                            pass.far, pass.epsilon]),
                    shadow_meta_fourth: receiver_fourth.map_or([0.0; 4], |(r, pass)|
                        [pass.threshold, r.lighting_index as f32,
                         pass.kind as f32, r.max_shadow_distance]),
                    shadow_falloff_fourth: receiver_fourth.map_or([0.0; 4], |(r, map)|
                        [r.distance_falloff_coefficient, ((r.receives & 2) != 0) as u32 as f32, map.map_size as f32, 0.0]),
                };

                let extra_passes = shadow_frame.map_or(&[][..], |shadow| shadow.extra.as_slice());
                let extra_receivers: Vec<&CoinWgpuShadowReceiver> = extra_passes.iter()
                    .map(|p| &p.receivers[draw.render_state_slot as usize]).collect();
                if layers.iter().any(|t| t.enabled != 0)
                    && fault_state::current() == FAULT_RTT_BIND_GROUP
                {
                    return Err((
                        CoinWgpuStatus::OutOfMemory,
                        "Injected RTT bind-group creation failure".to_string(),
                    ));
                }
                let base_shadow_views = [
                    shadow_map.as_ref().map_or(&ctx.default_texture_view, |map| &map.view),
                    shadow_map_second.as_ref().map_or(&ctx.default_texture_view, |map| &map.view),
                    shadow_map_third.as_ref().map_or(&ctx.default_texture_view, |map| &map.view),
                    shadow_map_fourth.as_ref().map_or(&ctx.default_texture_view, |map| &map.view),
                ];
                let extra_shadow_views: [&wgpu::TextureView; 4] = std::array::from_fn(|slot|
                    extra_shadow_maps.get(slot).map_or(&ctx.default_texture_view,
                        |map| &map.view));
                let create_binding = |u_buffer: &wgpu::Buffer,
                    shadow_views: [&wgpu::TextureView; 4]| {
                    let mut entries = vec![
                        wgpu::BindGroupEntry {
                            binding: 0,
                            resource: wgpu::BindingResource::Buffer(wgpu::BufferBinding {
                                buffer: u_buffer, offset: 0,
                                size: std::num::NonZeroU64::new(uniform_size as u64),
                            }),
                        },
                        wgpu::BindGroupEntry {
                            binding: 1,
                            resource: materials_buffer.as_entire_binding(),
                        },
                    ];
                    for (unit, (view, sampler, _size)) in texture_bindings.iter().enumerate() {
                        if eight_shadows && !profile.shadow_multitexture && unit >= 4 { continue; }
                        entries.push(wgpu::BindGroupEntry {
                            binding: 2 + 2 * unit as u32,
                            resource: wgpu::BindingResource::TextureView(view),
                        });
                        entries.push(wgpu::BindGroupEntry {
                            binding: 3 + 2 * unit as u32,
                            resource: wgpu::BindingResource::Sampler(sampler),
                        });
                    }
                    let previous = if is_peel && peel_step > 0 {
                        &peeling.as_ref().unwrap().depths[peel_step - 1]
                    } else if sample_depth {
                        &peeling.as_ref().unwrap().opaque_view
                    } else {
                        &ctx.default_depth_view
                    };
                    let opaque = if is_peel || sample_depth {
                        &peeling.as_ref().unwrap().opaque_view
                    } else {
                        &ctx.default_depth_view
                    };
                    entries.push(wgpu::BindGroupEntry {
                        binding: 18,
                        resource: wgpu::BindingResource::TextureView(previous),
                    });
                    entries.push(wgpu::BindGroupEntry {
                        binding: 19,
                        resource: wgpu::BindingResource::TextureView(opaque),
                    });
                    if profile.shadow_multitexture {
                        entries.push(wgpu::BindGroupEntry { binding: 20,
                            resource: wgpu::BindingResource::TextureView(&shadow_array.as_ref().unwrap().view) });
                    } else {
                    entries.push(wgpu::BindGroupEntry {
                        binding: 20,
                        resource: wgpu::BindingResource::TextureView(shadow_views[0]),
                    });
                    entries.push(wgpu::BindGroupEntry {
                        binding: 21,
                        resource: wgpu::BindingResource::TextureView(shadow_views[1]),
                    });
                    entries.push(wgpu::BindGroupEntry {
                        binding: 22,
                        resource: wgpu::BindingResource::TextureView(shadow_views[2]),
                    });
                    entries.push(wgpu::BindGroupEntry {
                        binding: 23,
                        resource: wgpu::BindingResource::TextureView(shadow_views[3]),
                    });
                    }
                    if let Some((image,sampler,_))=bump_binding {
                      entries.push(wgpu::BindGroupEntry {binding:25,resource:wgpu::BindingResource::TextureView(image)});
                      entries.push(wgpu::BindGroupEntry {binding:26,resource:wgpu::BindingResource::Sampler(sampler)});
                    }
                    if st.portable_shader_slot != 0 {
                        entries.push(wgpu::BindGroupEntry { binding: 27,
                            resource: module_buffers[st.portable_shader_slot as usize-1].as_entire_binding() });
                    }
                    if let Some(buffer) = instances_buffer.as_ref() {
                        entries.push(wgpu::BindGroupEntry { binding: 24,
                            resource: buffer.as_entire_binding() });
                    }
                    ctx.device.create_bind_group(&wgpu::BindGroupDescriptor {
                        label: Some("Coin shared draw texture bindings"),
                        layout: if instanced { &profile.instanced_bind_group_layout } else if eight_shadows { &profile.shadow_eight_layout } else { &profile.bind_group_layout },
                        entries: &entries,
                    })
                };
                if eight_shadows {
                    #[repr(C)]
                    #[derive(Copy, Clone, Pod, Zeroable)]
                    struct ExtraReceiver {
                        view_to_clip: [[f32; 4]; 4], view_to_light: [[f32; 4]; 4],
                        params: [f32; 4], meta: [f32; 4], falloff: [f32; 4],
                    }
                    let mut receivers = [ExtraReceiver::zeroed(); 4];
                    for (slot, (p, r)) in extra_passes.iter().zip(&extra_receivers).enumerate() {
                        receivers[slot] = ExtraReceiver {
                            view_to_clip: shadow_matrix(&r.model_view_projection),
                            view_to_light: shadow_matrix(&r.model_view),
                            params: [(r.receives & 1) as f32, p.near, p.far, p.epsilon],
                            meta: [p.threshold, r.lighting_index as f32, p.kind as f32, r.max_shadow_distance],
                            falloff: [r.distance_falloff_coefficient, ((r.receives & 2) != 0) as u32 as f32, p.map_size as f32, 0.0],
                        };
                    }
                    let buffer = multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
                        label: Some("Coin extra shadow receivers"), contents: bytemuck::cast_slice(&receivers),
                        usage: wgpu::BufferUsages::UNIFORM,
                    });
                    let mut entries = vec![wgpu::BindGroupEntry { binding: 0, resource: buffer.as_entire_binding() }];
                    for (slot, view) in extra_shadow_views.iter().enumerate().filter(|_| !profile.shadow_multitexture) {
                        entries.push(wgpu::BindGroupEntry { binding: slot as u32 + 1,
                            resource: wgpu::BindingResource::TextureView(view) });
                    }
                    let binding = ctx.device.create_bind_group(&wgpu::BindGroupDescriptor {
                        label: Some("Coin extra shadow receiver binding"), layout: &profile.shadow_extra_layout, entries: &entries,
                    });
                    pass.set_bind_group(1, &binding, &[]);
                }
                // Queue writes are submitted before this frame's command buffer.
                // They cannot alter a preceding submission; wgpu keeps the
                // underlying resources alive while that submission is in flight.
                if sampling_audit {
                    let hash=bytemuck::bytes_of(&uniforms)[..uniform_size].iter()
                        .fold(0xcbf29ce484222325u64,|h,b|(h^u64::from(*b)).wrapping_mul(0x100000001b3));
                    eprintln!("COIN_SAMPLING_AUDIT mode={:?} revision={} instanced={} instances={} draws={} texture_units={} uniform_bytes={} uniform_hash={:016x}",
                        policy,frame_revision,instanced,instances_slice.len(),draws_slice.len(),
                        layers.iter().filter(|x|x.enabled!=0).count(),uniform_size,hash);
                }
                let (chunk, uniform_offset) = uniform_arena.push(&ctx.device, &uniforms);
                let units = std::array::from_fn(|unit| {
                    let layer = &layers[unit];
                    if layer.enabled == 0 || (eight_shadows && !profile.shadow_multitexture && unit >= 4) {
                        (u32::MAX, u32::MAX)
                    } else { (layer.texture_slot, layer.sampler_slot) }
                });
                let key = (chunk, units, if is_peel { peel_step } else { usize::MAX }, st.portable_shader_slot);
                let binding = draw_bindings.entry(key).or_insert_with(|| {
                    create_binding(uniform_arena.buffer(chunk), base_shadow_views)
                });
                pass.set_bind_group(0, &*binding, &[uniform_offset]);
                let viewport = resolved_viewport(st, target_width, target_height)?;
                if !st.depth_range[0].is_finite()
                    || !st.depth_range[1].is_finite()
                    || st.depth_range[0] < 0.0
                    || st.depth_range[0] > 1.0
                    || st.depth_range[1] < 0.0
                    || st.depth_range[1] > 1.0
                {
                    return Err((
                        CoinWgpuStatus::InvalidArgument,
                        format!(
                            "Draw {} has invalid depth range {:?}",
                            item.draw_index, st.depth_range
                        ),
                    ));
                }
                pass.set_viewport(
                    viewport[0] as f32,
                    viewport[1] as f32,
                    viewport[2] as f32,
                    viewport[3] as f32,
                    if resolved_depth_bias || is_peel || is_weighted {
                        0.0
                    } else {
                        st.depth_range[0]
                    },
                    if resolved_depth_bias || is_peel || is_weighted {
                        1.0
                    } else {
                        st.depth_range[1]
                    },
                );
                pass.set_scissor_rect(
                    viewport[0] as u32,
                    viewport[1] as u32,
                    viewport[2] as u32,
                    viewport[3] as u32,
                );

                if draw.stable_node_id != 0 {
                    let key = (draw.stable_node_id, draw.draw_ordinal);
                    if let Some((_rev, entry)) = cache.active_entries.get(&key) {
                        pass.set_vertex_buffer(0, entry.vertex_buffer.slice(..));
                        if let Some(ref ib) = entry.index_buffer {
                            pass.set_index_buffer(ib.slice(..), wgpu::IndexFormat::Uint32);
                            pass.draw_indexed(0..entry.index_count, 0, 0..1);
                        } else {
                            pass.draw(0..entry.vertex_count, 0..1);
                        }
                    } else if let Some(entry) = oversized_entries.get(&key) {
                        pass.set_vertex_buffer(0, entry.vertex_buffer.slice(..));
                        if let Some(ref ib) = entry.index_buffer {
                            pass.set_index_buffer(ib.slice(..), wgpu::IndexFormat::Uint32);
                            pass.draw_indexed(0..entry.index_count, 0, 0..1);
                        } else {
                            pass.draw(0..entry.vertex_count, 0..1);
                        }
                    }
                } else {
                    let instance_range = if instanced {
                        let range = &instance_ranges[item.draw_index];
                        range.first_instance..range.first_instance + range.instance_count
                    } else { 0..1 };
                    if split_uncached {
                        if let Some((vb, ib)) = &split_uncached_buffers[item.draw_index] {
                            pass.set_vertex_buffer(0, vb.slice(..));
                            pass.set_index_buffer(ib.slice(..), wgpu::IndexFormat::Uint32);
                            pass.draw_indexed(0..draw.index_count, 0, instance_range);
                        }
                    } else {
                        if let Some(vb) = &uncached_v_buffer {
                            pass.set_vertex_buffer(0, vb.slice(..));
                        }
                        if let Some(ib) = &uncached_i_buffer {
                            pass.set_index_buffer(ib.slice(..), wgpu::IndexFormat::Uint32);
                        }
                        let start_idx = draw.first_index;
                        let end_idx = start_idx + draw.index_count;
                        pass.draw_indexed(start_idx..end_idx, 0, instance_range);
                    }
                }

            }
        }
        if is_weighted {
            weighted.as_ref().unwrap().composite_samples(&mut encoder, color_view,
                if pass_index + 1 == passes.len() { resolve_view } else { None },
                if pass_index + 1 == passes.len() { timestamp_query } else { None });
        }
        if is_peel {
            peeling.as_ref().unwrap().composite_samples(
                &mut encoder,
                color_view,
                depth_view,
                if pass_index + 1 == passes.len() { resolve_view } else { None },
                if pass_index + 1 == passes.len() {
                    timestamp_query
                } else {
                    None
                },
            );
        }
    }

    // Retire any oversized buffers immediately after pass completion
    for (_, entry) in oversized_entries {
        cache.deferred_release.push(RetiredBuffer {
            vertex_buffer: entry.vertex_buffer,
            index_buffer: entry.index_buffer,
            retired_at_serial: current_submission_serial,
            size_bytes: entry.size_bytes,
        });
    }

    uniform_arena.upload(&ctx.queue);
    ctx.draw_bind_groups.store(draw_bindings.len() as u32, Ordering::Relaxed);
    ctx.uniform_chunks.store(uniform_arena.chunk_count() as u32, Ordering::Relaxed);
    if passes.is_empty() {
      if let Some(resolve_view) = resolve_view {
        let _resolve = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("Coin empty-frame MSAA color resolve"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view: color_view,
                resolve_target: Some(resolve_view),
                ops: wgpu::Operations { load: wgpu::LoadOp::Load, store: wgpu::StoreOp::Store },
            })],
            depth_stencil_attachment: None,
            timestamp_writes: None,
            occlusion_query_set: None,
        });
      }
    }
    Ok((encoder.finish(), pending_instances))
}
