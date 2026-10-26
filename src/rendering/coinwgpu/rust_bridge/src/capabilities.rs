//! Runtime capability queries and render feature preflight.

use super::*;

#[no_mangle]
pub extern "C" fn coin_wgpu_is_available() -> i32 {
    let res = std::panic::catch_unwind(|| {
        let fault = fault_state::current();
        if fault == CoinWgpuStatus::NotReady as i32 {
            return 0;
        }
        if init_runtime_if_needed().is_err() {
            return 0;
        }
        let mut guard = match RUNTIME_CTX.lock() {
            Ok(g) => g,
            Err(_) => return 0,
        };
        if let Some(runtime) = guard.as_mut() {
            if get_or_init_device(runtime, None).is_ok() {
                1
            } else {
                0
            }
        } else {
            0
        }
    });
    res.unwrap_or(0)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_get_adapter_info(buffer: *mut std::os::raw::c_char, buffer_len: usize) {
    let _ = std::panic::catch_unwind(|| {
        if buffer.is_null() || buffer_len == 0 {
            return;
        }
        if init_runtime_if_needed().is_ok() {
            if let Ok(mut guard) = RUNTIME_CTX.lock() {
                if let Some(runtime) = guard.as_mut() {
                    if let Ok(dev) = get_or_init_device(runtime, None) {
                        set_error(buffer, buffer_len, &dev.adapter_name);
                        return;
                    }
                }
            }
        }
        set_error(buffer, buffer_len, "None");
    });
}

pub(super) fn portable_format_features(features: wgpu::TextureFormatFeatures) -> u32 {
    let mut result = 0;
    if features
        .allowed_usages
        .contains(wgpu::TextureUsages::TEXTURE_BINDING)
    {
        result |= 1;
    }
    if features
        .allowed_usages
        .contains(wgpu::TextureUsages::RENDER_ATTACHMENT)
    {
        result |= 2;
    }
    if features
        .flags
        .contains(wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X4)
    {
        result |= 4;
    }
    if features
        .flags
        .contains(wgpu::TextureFormatFeatureFlags::STORAGE_READ_ONLY)
    {
        result |= 8;
    }
    if features.flags.intersects(
        wgpu::TextureFormatFeatureFlags::STORAGE_WRITE_ONLY
            | wgpu::TextureFormatFeatureFlags::STORAGE_READ_WRITE,
    ) {
        result |= 16;
    }
    result
}

// Read-only epoch of the existing default device. Zero means unavailable;
// this query never creates or reconstructs a device.
#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_multipass(width:u32,height:u32,format:u32,surface:u64) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        let guard=RUNTIME_CTX.lock().unwrap();
        let Some(ctx)=guard.as_ref().and_then(|runtime|runtime.device_state.as_ref()) else {
            return CoinWgpuStatus::NotReady;
        };
        let usage=wgpu::TextureUsages::STORAGE_BINDING|wgpu::TextureUsages::TEXTURE_BINDING;
        if width>ctx.device.limits().max_texture_dimension_2d || height>ctx.device.limits().max_texture_dimension_2d ||
            !ctx.adapter.get_texture_format_features(wgpu::TextureFormat::Rgba32Float).allowed_usages.contains(usage) {
            return CoinWgpuStatus::Unsupported;
        }
        let output=if surface!=0 {
            let Some(record)=guard.as_ref().unwrap().surfaces.get(&surface) else {return CoinWgpuStatus::InvalidArgument;};
            if record.presentation.is_some() {wgpu::TextureFormat::Rgba8Unorm} else {record.color_format}
        } else {
            match format {0=>wgpu::TextureFormat::Rgba8Unorm,1=>wgpu::TextureFormat::Rgba8UnormSrgb,
                2=>wgpu::TextureFormat::Rgba16Float,_=>return CoinWgpuStatus::Unsupported}
        };
        ctx.device.push_error_scope(wgpu::ErrorFilter::Validation);
        let programs=ctx.multipass_programs.get(&ctx.device,output);
        if pollster::block_on(ctx.device.pop_error_scope()).is_some() {
            return CoinWgpuStatus::Unsupported;
        }
        if programs.is_err() { return CoinWgpuStatus::BackendError; }
        CoinWgpuStatus::Ok
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_default_device_generation() -> u64 {
    std::panic::catch_unwind(|| {
        let guard = match RUNTIME_CTX.lock() {
            Ok(g) => g,
            Err(_) => return 0,
        };
        if DEVICE_LOST_OCCURRED.load(Ordering::SeqCst) {
            return 0;
        }
        guard
            .as_ref()
            .and_then(|rt| rt.device_state.as_ref())
            .map_or(0, |device| device.generation.checked_add(1).unwrap_or(0))
    })
    .unwrap_or(0)
}

pub(super) fn shadow_multitexture_supported(ctx: &DeviceState) -> bool {
    let limits = ctx.device.limits();
    limits.max_inter_stage_shader_components >= 80 && limits.max_sampled_textures_per_shader_stage >= 11 && limits.max_samplers_per_shader_stage >= 8
}

pub(super) fn shadow_array_supported(ctx: &DeviceState,extent:u32,layers:u32)->bool {
    extent>0 && layers>0 && extent<=ctx.device.limits().max_texture_dimension_2d &&
        shadow::array_layer_count(layers)<=ctx.device.limits().max_texture_array_layers &&
        ctx.adapter.get_texture_format_features(wgpu::TextureFormat::Rgba32Float).allowed_usages
            .contains(wgpu::TextureUsages::TEXTURE_BINDING|wgpu::TextureUsages::COPY_DST|wgpu::TextureUsages::COPY_SRC|wgpu::TextureUsages::RENDER_ATTACHMENT)
}

// Pure admission: materialize each selected complete program before producers
// or WSI acquisition. No output commands are submitted by these probes.
pub(super) fn shadow_multitexture_alpha_supported(ctx:&DeviceState,samples:u32,modes:u32)->bool {
    if !matches!(samples,1|2|4) || !shadow_multitexture_supported(ctx) { return false; }
    if modes&2!=0 && !weighted_supported(ctx,samples) || modes&4!=0 && !peeling_supported(ctx,samples) { return false; }
    if samples>1 && modes&6!=0 && !shadow_multitexture_depth_supported(ctx) { return false; }
    let mut cache=ctx.shadow_multitexture_alpha_admission.lock().unwrap();
    *cache.entry((samples,modes)).or_insert_with(|| {
        ctx.device.push_error_scope(wgpu::ErrorFilter::Validation);
        let mut valid=true;
        for policy in [SamplingPolicy::Native,SamplingPolicy::Portable] {
            for eight in [false,true] {
                for mode in [1u32,2,4] {
                    if modes&mode==0 {continue;}
                    let profile=if mode==4 && samples>1 {ctx.shadow_multitexture_peeling_surface_profile(policy)}
                        else if mode==2 && samples>1 {ctx.shadow_multitexture_depth_surface_profile(policy)}
                        else {ctx.shadow_multitexture_surface_profile(policy)};
                    valid &= get_or_create_pipeline(ctx,profile,0,if mode==1 {wgpu::TextureFormat::Rgba8Unorm} else {wgpu::TextureFormat::Rgba16Float},
                        wgpu::TextureFormat::Depth32Float,samples,None,wgpu::FrontFace::Ccw,
                        mode==1,false,false,mode==4,true,1,wgpu::CompareFunction::LessEqual,
                        wgpu::DepthBiasState::default(),(mode!=1 && samples>1) || (samples==1 && modes&6!=0),
                        eight,true,mode==2,true,false).is_ok();
                }
            }
        }
        block_on(ctx.device.pop_error_scope()).is_none() && valid
    })
}

// Append-only private admission; the public options and frame-view layout are intact.
#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_bump_receiver(
    samples: u32,
    vertices: u64,
    materials: u64,
    image_axis: u32,
    sampling_policy: u32,
) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if !matches!(samples, 1 | 2 | 4) || sampling_policy > 1 {
            return CoinWgpuStatus::InvalidArgument;
        }
        let guard = RUNTIME_CTX.lock().unwrap();
        let Some(ctx) = guard
            .as_ref()
            .and_then(|runtime| runtime.device_state.as_ref())
        else {
            return CoinWgpuStatus::NotReady;
        };
        let limits = ctx.device.limits();
        let bytes = vertices
            .checked_add(materials)
            .and_then(|count| count.checked_mul(std::mem::size_of::<GpuMaterial>() as u64));
        if limits.max_inter_stage_shader_components < 80
            || limits.max_vertex_attributes < 13
            || limits.max_sampled_textures_per_shader_stage < 12
            || limits.max_samplers_per_shader_stage < 9
            || limits.max_storage_buffers_per_shader_stage < 1
            || image_axis > limits.max_texture_dimension_2d
            || bytes.is_none_or(|bytes| {
                bytes > u64::from(limits.max_storage_buffer_binding_size)
                    || bytes > limits.max_buffer_size
            })
        {
            return CoinWgpuStatus::Unsupported;
        }
        if samples > 1 {
            let required = if samples == 2 {
                wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X2
            } else {
                wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X4
            };
            if (samples == 2
                && !ctx
                    .device
                    .features()
                    .contains(wgpu::Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES))
                || [
                    wgpu::TextureFormat::Rgba8Unorm,
                    wgpu::TextureFormat::Depth32Float,
                ]
                .into_iter()
                .any(|format| {
                    !ctx.adapter
                        .get_texture_format_features(format)
                        .flags
                        .contains(required)
                })
            {
                return CoinWgpuStatus::Unsupported;
            }
        }
        ctx.device.push_error_scope(wgpu::ErrorFilter::Validation);
        let policy = if sampling_policy == 0 {
            SamplingPolicy::Native
        } else {
            SamplingPolicy::Portable
        };
        let profile = ctx.bump_receiver_profile(policy);
        let valid = get_or_create_pipeline(
            ctx,
            &profile,
            0,
            wgpu::TextureFormat::Rgba8Unorm,
            wgpu::TextureFormat::Depth32Float,
            samples,
            None,
            wgpu::FrontFace::Ccw,
            false,
            false,
            false,
            false,
            true,
            0xf,
            wgpu::CompareFunction::Less,
            wgpu::DepthBiasState::default(),
            false,
            false,
            true,
            false,
            true,
            false,
        )
        .is_ok();
        if block_on(ctx.device.pop_error_scope()).is_none() && valid {
            CoinWgpuStatus::Ok
        } else {
            CoinWgpuStatus::Unsupported
        }
    })
    .unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_shadow_multitexture_alpha(samples:u32,modes:u32,extent:u32,layers:u32)->CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if modes&!7!=0 || modes==0 || !matches!(samples,1|2|4) {return CoinWgpuStatus::InvalidArgument;}
        let guard=RUNTIME_CTX.lock().unwrap();
        let Some(ctx)=guard.as_ref().and_then(|runtime|runtime.device_state.as_ref()) else {return CoinWgpuStatus::NotReady;};
        if shadow_array_supported(ctx,extent,layers) && shadow_multitexture_alpha_supported(ctx,samples,modes) {
            CoinWgpuStatus::Ok
        } else {CoinWgpuStatus::Unsupported}
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_shadow_multitexture(sample_count: u32) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        let guard = RUNTIME_CTX.lock().unwrap();
        let Some(runtime) = guard.as_ref() else { return CoinWgpuStatus::NotReady; };
        let Some(ctx) = runtime.device_state.as_ref() else { return CoinWgpuStatus::NotReady; };
        if !shadow_multitexture_supported(ctx) { return CoinWgpuStatus::Unsupported; }
        let slot = match sample_count { 1 => 0, 2 => 1, 4 => 2, _ => return CoinWgpuStatus::InvalidArgument };
        if sample_count == 2 && !ctx.device.features().contains(wgpu::Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES) {
            return CoinWgpuStatus::Unsupported;
        }
        if sample_count > 1 {
            let required = if sample_count == 2 { wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X2 }
                else { wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X4 };
            if [wgpu::TextureFormat::Rgba8Unorm, wgpu::TextureFormat::Depth32Float].into_iter().any(|format|
                !ctx.adapter.get_texture_format_features(format).flags.contains(required)) {
                return CoinWgpuStatus::Unsupported;
            }
        }
        let admitted = *ctx.shadow_multitexture_admission[slot].get_or_init(|| {
            ctx.device.push_error_scope(wgpu::ErrorFilter::Validation);
            let mut valid = true;
            for policy in [SamplingPolicy::Native, SamplingPolicy::Portable] {
                let profile = ctx.shadow_multitexture_surface_profile(policy);
                for eight in [false, true] {
                    valid &= get_or_create_pipeline(ctx, profile, 0, wgpu::TextureFormat::Rgba8Unorm,
                        wgpu::TextureFormat::Depth32Float, sample_count, None, wgpu::FrontFace::Ccw,
                        false, false, false, false, true, 1, wgpu::CompareFunction::Less,
                        wgpu::DepthBiasState::default(), false, eight, true, false, true, false).is_ok();
                }
            }
            block_on(ctx.device.pop_error_scope()).is_none() && valid
        });
        if admitted { CoinWgpuStatus::Ok } else { CoinWgpuStatus::Unsupported }

    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_shadow_multitexture_depth(sample_count: u32) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        let guard = RUNTIME_CTX.lock().unwrap();
        let Some(runtime) = guard.as_ref() else { return CoinWgpuStatus::NotReady; };
        let Some(ctx) = runtime.device_state.as_ref() else { return CoinWgpuStatus::NotReady; };
        if !shadow_multitexture_depth_supported(ctx) { return CoinWgpuStatus::Unsupported; }
        let slot = match sample_count { 1 => 0, 2 => 1, 4 => 2, _ => return CoinWgpuStatus::InvalidArgument };
        if sample_count == 2 && !ctx.device.features().contains(wgpu::Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES) {
            return CoinWgpuStatus::Unsupported;
        }
        if sample_count > 1 {
            let required = if sample_count == 2 { wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X2 }
                else { wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X4 };
            if [wgpu::TextureFormat::Rgba8Unorm, wgpu::TextureFormat::Depth32Float].into_iter().any(|format|
                !ctx.adapter.get_texture_format_features(format).flags.contains(required)) {
                return CoinWgpuStatus::Unsupported;
            }
        }
        let admitted = *ctx.shadow_multitexture_depth_admission[slot].get_or_init(|| {
            ctx.device.push_error_scope(wgpu::ErrorFilter::Validation);
            let mut valid = true;
            for policy in [SamplingPolicy::Native, SamplingPolicy::Portable] {
                let profile = ctx.shadow_multitexture_depth_surface_profile(policy);
                for eight in [false, true] {
                    valid &= get_or_create_pipeline(ctx, profile, 0, wgpu::TextureFormat::Rgba8Unorm,
                        wgpu::TextureFormat::Depth32Float, sample_count, None, wgpu::FrontFace::Ccw,
                        false, false, false, false, true, 1, wgpu::CompareFunction::Less,
                        wgpu::DepthBiasState::default(), true, eight, true, false, true, false).is_ok();
                }
            }
            block_on(ctx.device.pop_error_scope()).is_none() && valid
        });
        if admitted { CoinWgpuStatus::Ok } else { CoinWgpuStatus::Unsupported }

    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_sample_depth(samples: u32) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if !matches!(samples, 1 | 2 | 4) { return CoinWgpuStatus::InvalidArgument; }
        let guard = RUNTIME_CTX.lock().unwrap();
        let Some(runtime) = guard.as_ref() else { return CoinWgpuStatus::NotReady; };
        let Some(ctx) = runtime.device_state.as_ref() else { return CoinWgpuStatus::NotReady; };
        if samples == 1 || ctx.adapter.get_downlevel_capabilities().flags.contains(wgpu::DownlevelFlags::MULTISAMPLED_SHADING) {
            CoinWgpuStatus::Ok
        } else { CoinWgpuStatus::Unsupported }
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_peeling(samples: u32) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if !matches!(samples, 1 | 2 | 4) { return CoinWgpuStatus::InvalidArgument; }
        let guard = RUNTIME_CTX.lock().unwrap();
        let Some(runtime) = guard.as_ref() else { return CoinWgpuStatus::NotReady; };
        let Some(ctx) = runtime.device_state.as_ref() else { return CoinWgpuStatus::NotReady; };
        if peeling_supported(ctx, samples) { CoinWgpuStatus::Ok } else { CoinWgpuStatus::Unsupported }
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_weighted(samples: u32) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if !matches!(samples, 1 | 2 | 4) { return CoinWgpuStatus::InvalidArgument; }
        let mut guard = RUNTIME_CTX.lock().unwrap();
        let Some(runtime) = guard.as_mut() else { return CoinWgpuStatus::NotReady; };
        let Some(ctx) = runtime.device_state.as_ref() else { return CoinWgpuStatus::NotReady; };
        if weighted_supported(ctx, samples) {
            CoinWgpuStatus::Ok
        } else { CoinWgpuStatus::Unsupported }
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_raster_images(outputs:u32,width:u32,height:u32,bytes:u64)->CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if outputs==0 {return CoinWgpuStatus::Ok;}
        if outputs>4096 || width==0 || height==0 || bytes>128*1024*1024 {return CoinWgpuStatus::Unsupported;}
        let guard=match RUNTIME_CTX.lock(){Ok(g)=>g,Err(_)=>return CoinWgpuStatus::BackendError};
        let Some(ctx)=guard.as_ref().and_then(|runtime|runtime.device_state.as_ref()) else {return CoinWgpuStatus::NotReady;};
        if width>ctx.device.limits().max_texture_dimension_2d || height>ctx.device.limits().max_texture_dimension_2d {
            return CoinWgpuStatus::Unsupported;
        }
        match raster_image::prepare(ctx) {Ok(_)=>CoinWgpuStatus::Ok,Err((status,_))=>status}
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_texture_profile(formats:u32, outputs:u32, anisotropy:u32)->CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if formats&!31!=0 || outputs&!15!=0 || !anisotropy.is_power_of_two() || anisotropy>16 {
            return CoinWgpuStatus::InvalidArgument;
        }
        if init_runtime_if_needed().is_err() {return CoinWgpuStatus::BackendError;}
        let mut guard=match RUNTIME_CTX.lock(){Ok(g)=>g,Err(_)=>return CoinWgpuStatus::BackendError};
        let runtime=match guard.as_mut(){Some(r)=>r,None=>return CoinWgpuStatus::NotReady};
        let ctx=match get_or_init_device(runtime,None){Ok(d)=>d,Err(_)=>return CoinWgpuStatus::NotReady};
        if anisotropy>1 && !ctx.adapter.get_downlevel_capabilities().flags.contains(wgpu::DownlevelFlags::ANISOTROPIC_FILTERING) {
            return CoinWgpuStatus::Unsupported;
        }
        let kinds=[wgpu::TextureFormat::Rgba8Unorm,wgpu::TextureFormat::Rgba8UnormSrgb,
            wgpu::TextureFormat::Rgba16Float,wgpu::TextureFormat::Bc3RgbaUnorm,wgpu::TextureFormat::Bc3RgbaUnormSrgb];
        for (i,format) in kinds.into_iter().enumerate() {
            if formats&(1<<i)==0 {continue;}
            if format.is_compressed() && !ctx.device.features().contains(wgpu::Features::TEXTURE_COMPRESSION_BC) {return CoinWgpuStatus::Unsupported;}
            let facts=ctx.adapter.get_texture_format_features(format);
            if !facts.allowed_usages.contains(wgpu::TextureUsages::TEXTURE_BINDING|wgpu::TextureUsages::COPY_DST) {return CoinWgpuStatus::Unsupported;}
        }
        for (bit,mipbit,format) in [(1,4,wgpu::TextureFormat::Rgba8Unorm),(2,8,wgpu::TextureFormat::Rgba16Float)] {
            if outputs&(bit|mipbit)==0 {continue;}
            let usage=wgpu::TextureUsages::RENDER_ATTACHMENT|wgpu::TextureUsages::TEXTURE_BINDING|
                if outputs&mipbit!=0 {wgpu::TextureUsages::STORAGE_BINDING} else {wgpu::TextureUsages::empty()};
            if !ctx.adapter.get_texture_format_features(format).allowed_usages.contains(usage) {return CoinWgpuStatus::Unsupported;}
        }
        CoinWgpuStatus::Ok
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_query_runtime_capabilities(
    output: *mut CoinWgpuRuntimeCapabilities,
    output_size: usize,
) -> CoinWgpuStatus {
    if output.is_null() || output_size != std::mem::size_of::<CoinWgpuRuntimeCapabilities>() {
        return CoinWgpuStatus::InvalidArgument;
    }
    std::panic::catch_unwind(|| {
        if fault_state::current() == CoinWgpuStatus::NotReady as i32 {
            return CoinWgpuStatus::NotReady;
        }
        if init_runtime_if_needed().is_err() {
            return CoinWgpuStatus::BackendError;
        }
        let mut guard = match RUNTIME_CTX.lock() {
            Ok(g) => g,
            Err(_) => return CoinWgpuStatus::BackendError,
        };
        let runtime = match guard.as_mut() {
            Some(r) => r,
            None => return CoinWgpuStatus::NotReady,
        };
        let dev = match get_or_init_device(runtime, None) {
            Ok(d) => d,
            Err(_) => return CoinWgpuStatus::NotReady,
        };
        let info = dev.adapter.get_info();
        let limits = dev.adapter.limits();
        let format = |f| portable_format_features(dev.adapter.get_texture_format_features(f));
        let mut result = CoinWgpuRuntimeCapabilities {
            struct_size: output_size as u32,
            renderer: adapter_renderer(info.backend),
            vendor_id: info.vendor,
            device_id: info.device,
            max_framebuffer_attachments: limits.max_color_attachments,
            format_rgba8: format(wgpu::TextureFormat::Rgba8Unorm),
            // Depth24PlusStencil8 is an abstract format; it does not certify D24S8.
            format_d24s8: 0,
            format_d32f: format(wgpu::TextureFormat::Depth32Float),
            format_rgba16f: format(wgpu::TextureFormat::Rgba16Float),
            format_r16f: format(wgpu::TextureFormat::R16Float),
            ..Default::default()
        };
        if limits.max_color_attachments > 1 {
            result.runtime_features |= 1 | 2;
        }
        if limits.max_compute_workgroups_per_dimension > 0 {
            result.runtime_features |= 4;
        }
        if dev
            .adapter
            .features()
            .contains(wgpu::Features::TIMESTAMP_QUERY)
        {
            result.runtime_features |= 8;
        }
        result.available_mechanisms = 1;
        // Executable formats use the enabled device feature set, not optional
        // adapter capabilities which this device did not request.
        if peeling::device_supported(&dev.device) {
            result.available_mechanisms |= 2;
        }
        if weighted::device_supported(&dev.device) { result.available_mechanisms |= 4; }
        unsafe {
            std::ptr::write(output, result);
        }
        CoinWgpuStatus::Ok
    })
    .unwrap_or(CoinWgpuStatus::BackendError)
}
