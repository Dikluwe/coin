//! Per-submission image composition. Outputs are never retained across revisions.
//! Payload pixels remain PRIMARY through all eight texture-combine stages.
use super::*;
use wgpu::util::DeviceExt;

pub(super) struct Image {
    // Keep the storage resource alive until its consuming submission owns it.
    _texture: wgpu::Texture,
    pub view: wgpu::TextureView,
    pub sampler: wgpu::Sampler,
    pub width: u32,
    pub height: u32,
}

fn layer(state: &CoinWgpuRenderState, unit: usize) -> CoinWgpuTextureUnit {
    if unit == 0 {
        CoinWgpuTextureUnit { matrix: state.texture_matrix, enabled: state.has_texture,
            texture_slot: state.texture_slot, sampler_slot: state.sampler_slot,
            model: state.texture_model, blend_color: state.texture_blend_color }
    } else { state.extra_textures[unit - 1] }
}

fn texture_view<'a>(texture: &CoinWgpuTexture, cache: &'a TextureCache,
                    rtt: &'a RttRegistry) -> Result<&'a wgpu::TextureView, (CoinWgpuStatus,String)> {
    let view = if gpu_texture_token(texture) {
        rtt.active.get(&texture.content_digest).map(|entry| &entry.view)
    } else {
        cache.entries.get(&TextureKey { width: texture.width, height: texture.height,
            format: texture.format, content_digest: texture.content_digest }).map(|entry| &entry.view)
    };
    view.ok_or_else(|| (CoinWgpuStatus::InvalidArgument, "Missing inherited image GPU texture".into()))
}

// Structural ingress validation runs before adapter lookup or cache mutation.
#[allow(dead_code)]
pub(super) fn validate(states:&[CoinWgpuRenderState],texture_count:usize,sampler_count:usize)->Result<(),(CoinWgpuStatus,String)>{
    validate_impl(states,None,texture_count,sampler_count)
}
pub(super) fn validate_with_images(states:&[CoinWgpuRenderState],textures:&[CoinWgpuTexture],sampler_count:usize)->Result<(),(CoinWgpuStatus,String)>{
    validate_impl(states,Some(textures),textures.len(),sampler_count)
}
fn validate_impl(states:&[CoinWgpuRenderState],textures:Option<&[CoinWgpuTexture]>,texture_count:usize,sampler_count:usize)->Result<(),(CoinWgpuStatus,String)>{
    if states.iter().filter(|state|state.raster_texture_source_slot!=0).count()>4096 {
        return Err((CoinWgpuStatus::Unsupported,"Inherited images exceed 4096 output resources".into()));
    }
    for state in states.iter().filter(|state|state.raster_texture_source_slot!=0) {
        let source=states.get(state.raster_texture_source_slot as usize-1).ok_or_else(||
            (CoinWgpuStatus::InvalidArgument,"Invalid inherited image source state".into()))?;
        if source.raster_texture_source_slot!=0 || state.has_texture!=1 ||
           state.texture_slot as usize>=texture_count || state.sampler_slot as usize>=sampler_count ||
           source.texture_projection>1 {
            return Err((CoinWgpuStatus::InvalidArgument,"Invalid inherited image source or payload".into()));
        }
        for unit in 0..8 {
            let raw=state.raster_texture_coordinates[unit];
            let layer=layer(source,unit);
            if !raw.iter().all(|v|v.is_finite()) || layer.enabled>1 {
                return Err((CoinWgpuStatus::InvalidArgument,"Invalid inherited raster attributes".into()));
            }
            if layer.enabled==0 {continue;}
            if layer.texture_slot as usize>=texture_count || layer.sampler_slot as usize>=sampler_count ||
               !layer.matrix.iter().all(|v|v.is_finite()) {
                return Err((CoinWgpuStatus::InvalidArgument,"Invalid inherited texture resources or matrix".into()));
            }
            let transformed: [f32;4]=std::array::from_fn(|row|
                (0..4).map(|col|layer.matrix[col*4+row]*raw[col]).sum());
            if !transformed.iter().all(|v|v.is_finite()) ||
               (textures.is_some_and(|images|images[layer.texture_slot as usize].format==12) && transformed[..3].iter().all(|v|*v==0.0)) ||
               (!textures.is_some_and(|images|images[layer.texture_slot as usize].format==12) && source.texture_projection==0 && (transformed[3]==0.0 ||
                !(0..3).all(|c|(transformed[c]/transformed[3]).is_finite()))) {
                return Err((CoinWgpuStatus::InvalidArgument,"Invalid projected raster coordinate".into()));
            }
        }
    }
    Ok(())
}

pub(super) struct Pipeline {
    layout: wgpu::BindGroupLayout,
    pipeline: wgpu::ComputePipeline,
}

fn create_pipeline(device: &wgpu::Device) -> Pipeline {
    let shader=device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label:Some("Coin inherited image RGBA32F"),
        source:wgpu::ShaderSource::Wgsl(include_str!("../../shaders/coin_raster_image.wgsl").into()) });
    let mut layout_entries=vec![
        wgpu::BindGroupLayoutEntry { binding:0, visibility:wgpu::ShaderStages::COMPUTE,
            ty:wgpu::BindingType::Buffer { ty:wgpu::BufferBindingType::Uniform,
                has_dynamic_offset:false,min_binding_size:std::num::NonZeroU64::new(1024) },count:None },
        wgpu::BindGroupLayoutEntry { binding:1, visibility:wgpu::ShaderStages::COMPUTE,
            ty:wgpu::BindingType::Texture { sample_type:wgpu::TextureSampleType::Float{filterable:false},
                view_dimension:wgpu::TextureViewDimension::D2,multisampled:false },count:None },
        wgpu::BindGroupLayoutEntry { binding:2, visibility:wgpu::ShaderStages::COMPUTE,
            ty:wgpu::BindingType::StorageTexture { access:wgpu::StorageTextureAccess::WriteOnly,
                format:wgpu::TextureFormat::Rgba32Float,view_dimension:wgpu::TextureViewDimension::D2 },count:None }];
    for unit in 0..8 {
        layout_entries.push(wgpu::BindGroupLayoutEntry { binding:3+unit*2,visibility:wgpu::ShaderStages::COMPUTE,
            ty:wgpu::BindingType::Texture { sample_type:wgpu::TextureSampleType::Float{filterable:true},
                view_dimension:wgpu::TextureViewDimension::D2,multisampled:false },count:None });
        layout_entries.push(wgpu::BindGroupLayoutEntry { binding:4+unit*2,visibility:wgpu::ShaderStages::COMPUTE,
            ty:wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),count:None });
    }
    let layout=device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label:Some("Coin image prepass layout"),entries:&layout_entries });
    let pipeline_layout=device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
        label:Some("Coin image prepass pipeline layout"),bind_group_layouts:&[&layout],push_constant_ranges:&[] });
    let pipeline=device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
        label:Some("Coin image prepass"),layout:Some(&pipeline_layout),module:&shader,
        entry_point:Some("main"),compilation_options:Default::default(),cache:None });
    Pipeline { layout, pipeline }
}

// Resource/pipeline admission is shared by graph preflight, WSI acquisition
// and encoding. It allocates no image and submits no producer or scene work.
pub(super) fn prepare(ctx: &DeviceState) -> Result<&Pipeline,(CoinWgpuStatus,String)> {
    let usages=wgpu::TextureUsages::STORAGE_BINDING|wgpu::TextureUsages::TEXTURE_BINDING;
    if !ctx.device.features().contains(wgpu::Features::FLOAT32_FILTERABLE) ||
       !ctx.adapter.get_texture_format_features(wgpu::TextureFormat::Rgba32Float)
           .allowed_usages.contains(usages) || ctx.device.limits().max_sampled_textures_per_shader_stage < 9 ||
       ctx.device.limits().max_samplers_per_shader_stage < 8 ||
       ctx.device.limits().max_storage_textures_per_shader_stage < 1 {
        return Err((CoinWgpuStatus::Unsupported,"Inherited image texturing requires sampleable RGBA32F storage, nine textures and eight samplers".into()));
    }
    if ctx.raster_image_pipeline.get().is_none() {
        ctx.device.push_error_scope(wgpu::ErrorFilter::Validation);
        let pipeline=create_pipeline(&ctx.device);
        if let Some(error)=block_on(ctx.device.pop_error_scope()) {
            return Err((CoinWgpuStatus::Unsupported,format!("Inherited image compute pipeline unavailable: {error}")));
        }
        let _=ctx.raster_image_pipeline.set(pipeline);
        ctx.pipeline_compilations.fetch_add(1,Ordering::Relaxed);
    }
    Ok(ctx.raster_image_pipeline.get().unwrap())
}

pub(super) fn admit(ctx: &DeviceState, states:&[CoinWgpuRenderState], textures:&[CoinWgpuTexture],
    sampler_count:usize) -> Result<(),(CoinWgpuStatus,String)> {
    validate_with_images(states,textures,sampler_count)?;
    let mut bytes=0u64;
    let mut needed=false;
    for state in states.iter().filter(|state|state.raster_texture_source_slot!=0) {
        needed=true;
        let payload=&textures[state.texture_slot as usize];
        let allocation=u64::from(payload.width).checked_mul(u64::from(payload.height))
            .and_then(|pixels|pixels.checked_mul(16))
            .ok_or_else(||(CoinWgpuStatus::OutOfMemory,"Inherited image allocation overflow".into()))?;
        bytes=bytes.checked_add(allocation)
            .ok_or_else(||(CoinWgpuStatus::OutOfMemory,"Inherited image allocation overflow".into()))?;
        if bytes>128*1024*1024 || payload.width==0 || payload.height==0 ||
           payload.width>ctx.device.limits().max_texture_dimension_2d ||
           payload.height>ctx.device.limits().max_texture_dimension_2d {
            return Err((CoinWgpuStatus::Unsupported,"Inherited image outputs exceed 128 MiB or device extents".into()));
        }
    }
    if needed {prepare(ctx)?;}
    Ok(())
}

pub(super) fn encode(ctx: &DeviceState, encoder: &mut wgpu::CommandEncoder,
    states: &[CoinWgpuRenderState], textures: &[CoinWgpuTexture], samplers: &[CoinWgpuSampler],
    cache: &TextureCache, _sampler_cache: &SamplerCache, rtt: &RttRegistry)
    -> Result<HashMap<usize,Image>,(CoinWgpuStatus,String)> {
    admit(ctx,states,textures,samplers.len())?;
    let mut output=HashMap::new();
    if !states.iter().any(|state| state.raster_texture_source_slot != 0) { return Ok(output); }
    let usages=wgpu::TextureUsages::STORAGE_BINDING|wgpu::TextureUsages::TEXTURE_BINDING;
    let device=&ctx.device;
    let cached=prepare(ctx)?;
    ctx.pipeline_hits.fetch_add(1,Ordering::Relaxed);
    let layout=&cached.layout;
    let pipeline=&cached.pipeline;
    for (slot,state) in states.iter().enumerate().filter(|(_,state)|state.raster_texture_source_slot!=0) {
        let source=&states[state.raster_texture_source_slot as usize-1];
        let payload=&textures[state.texture_slot as usize];
        let mut data=vec![0f32;256];
        let mut views=Vec::with_capacity(8);
        let mut source_samplers=Vec::with_capacity(8);
        for unit in 0..8 {
            let layer=layer(source,unit);
            let raw=state.raster_texture_coordinates[unit];
            if !raw.iter().all(|value|value.is_finite()) {
                return Err((CoinWgpuStatus::InvalidArgument,"Non-finite image raster coordinate".into()));
            }
            for row in 0..4 { data[unit*4+row]=(0..4).map(|col|layer.matrix[col*4+row]*raw[col]).sum(); }
            data[32+unit*4]=layer.enabled as f32;
            data[33+unit*4]=layer.model as f32;
            data[35+unit*4]=source.texture_projection as f32;
            data[64+unit*4..68+unit*4].copy_from_slice(&layer.blend_color);
            for row in 0..4 {
                data[128+unit*16+row*4..132+unit*16+row*4].copy_from_slice(&source.texture_combines[unit][row]);
            }
            if layer.enabled==0 {
                views.push(&ctx.default_texture_view);
                source_samplers.push(ctx.default_sampler.clone());
                continue;
            }
            if !data[unit*4..unit*4+4].iter().all(|value|value.is_finite()) ||
                (source.texture_projection==0 && data[unit*4+3]==0.0 &&
                 textures.get(layer.texture_slot as usize).map_or(true,|t|t.format!=12)) {
                return Err((CoinWgpuStatus::InvalidArgument,"Invalid projected image raster coordinate".into()));
            }
            let texture=textures.get(layer.texture_slot as usize).ok_or_else(||
                (CoinWgpuStatus::InvalidArgument,"Invalid inherited image texture slot".into()))?;
            let sampler=samplers.get(layer.sampler_slot as usize).ok_or_else(||
                (CoinWgpuStatus::InvalidArgument,"Invalid inherited image sampler slot".into()))?;
            views.push(texture_view(texture,cache,rtt)?);
            data[34+unit*4]=if texture.format==1 {1.0}else{0.0};
            let metadata=if texture.format==12 {
                let (n,_,mips)=cube_atlas_metadata(texture)?.unwrap();
                [n as f32,if mips{1.0}else{0.0},(8+sampler.filter) as f32,0.0]
            }else if texture.format==11 {
                let (w,h,d,_)=spatial_atlas_metadata(texture)?.ok_or_else(||
                    (CoinWgpuStatus::InvalidArgument,"Missing spatial image metadata".into()))?;
                [w as f32,h as f32,(4+sampler.filter) as f32,
                 (d*16+sampler.wrap_s+2*sampler.wrap_t+4*((sampler.reserved>>4)&1)) as f32]
            }else{[texture.width as f32,texture.height as f32,sampler.filter as f32,0.0]};
            data[96+unit*4..100+unit*4].copy_from_slice(&metadata);
            let filter=if sampler.filter==0 {wgpu::FilterMode::Nearest}else{wgpu::FilterMode::Linear};
            source_samplers.push(device.create_sampler(&wgpu::SamplerDescriptor {
                label:Some("Coin constant raster sampler"),
                address_mode_u:if sampler.wrap_s==0 {wgpu::AddressMode::Repeat}else{wgpu::AddressMode::ClampToEdge},
                address_mode_v:if sampler.wrap_t==0 {wgpu::AddressMode::Repeat}else{wgpu::AddressMode::ClampToEdge},
                mag_filter:filter,min_filter:filter,mipmap_filter:wgpu::FilterMode::Nearest,
                ..Default::default() }));
        }
        let texture=device.create_texture(&wgpu::TextureDescriptor {
            label:Some("Coin image RGBA32F output"),size:wgpu::Extent3d {
                width:payload.width,height:payload.height,depth_or_array_layers:1 },
            mip_level_count:1,sample_count:1,dimension:wgpu::TextureDimension::D2,
            format:wgpu::TextureFormat::Rgba32Float,usage:usages,view_formats:&[] });
        let view=texture.create_view(&Default::default());
        let uniform=device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label:Some("Coin image prepass constants"),contents:bytemuck::cast_slice(&data),usage:wgpu::BufferUsages::UNIFORM });
        let mut entries=vec![wgpu::BindGroupEntry { binding:0,resource:uniform.as_entire_binding() },
            wgpu::BindGroupEntry { binding:1,resource:wgpu::BindingResource::TextureView(texture_view(payload,cache,rtt)?) },
            wgpu::BindGroupEntry { binding:2,resource:wgpu::BindingResource::TextureView(&view) }];
        for unit in 0..8 { entries.push(wgpu::BindGroupEntry {binding:3+unit as u32*2,
            resource:wgpu::BindingResource::TextureView(views[unit])});
            entries.push(wgpu::BindGroupEntry {binding:4+unit as u32*2,
                resource:wgpu::BindingResource::Sampler(&source_samplers[unit])}); }
        let bindings=device.create_bind_group(&wgpu::BindGroupDescriptor {
            label:Some("Coin image prepass bindings"),layout,entries:&entries });
        { let mut pass=encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
            label:Some("Coin image inherited texture composition"),timestamp_writes:None });
          pass.set_pipeline(pipeline);pass.set_bind_group(0,&bindings,&[]);
          pass.dispatch_workgroups(payload.width.div_ceil(8),payload.height.div_ceil(8),1); }
        let sampler=device.create_sampler(&wgpu::SamplerDescriptor {
            label:Some("Coin image float consumption"),address_mode_u:wgpu::AddressMode::ClampToEdge,
            address_mode_v:wgpu::AddressMode::ClampToEdge,mag_filter:wgpu::FilterMode::Nearest,
            min_filter:wgpu::FilterMode::Nearest,..Default::default() });
        output.insert(slot,Image {_texture:texture,view,sampler,width:payload.width,height:payload.height});
    }
    Ok(output)
}
