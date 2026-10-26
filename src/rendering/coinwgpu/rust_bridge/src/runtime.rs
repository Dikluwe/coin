//! GPU devices, runtime lifetime and native surface configuration.

use super::*;

pub(super) fn create_surface_sampling_profile(device: &wgpu::Device, adapter: &wgpu::Adapter,
                                   policy: SamplingPolicy, multisampled_peel: bool, sample_depth: bool, shadow_multitexture: bool) -> SurfaceSamplingProfile {
    create_surface_sampling_profile_impl(device, adapter, policy, multisampled_peel, sample_depth, shadow_multitexture, false, None)
}

pub(super) fn create_surface_sampling_profile_impl(device: &wgpu::Device, adapter: &wgpu::Adapter,
                                   policy: SamplingPolicy, multisampled_peel: bool, sample_depth: bool, shadow_multitexture: bool, bump_receiver: bool,
                                   portable_module: Option<(&portable::Module,u32)>) -> SurfaceSamplingProfile {
    let gl_depth_loads = adapter.get_info().backend == wgpu::Backend::Gl;
    let shader_source = |source: &str| {
        let source = portable_module.map_or_else(|| source.to_owned(), |(module,_)| portable::shader_source(source,module));
        shader_profile::depth_load_profile(
            &shader_profile::sample_depth_profile(&shader_profile::peeling_msaa_profile(
                &shader_profile::texture_specialization(&shader_profile::sampling_profile(&source,
                policy != SamplingPolicy::Native)), multisampled_peel), sample_depth), gl_depth_loads)
    };
    let trace_compile = |family: &str| {
        if let Some((module,_)) = portable_module {
            portable::trace_compile(module,policy,multisampled_peel,sample_depth,shadow_multitexture,family);
        }
    };
    let module_mode = portable_module.map_or(0,|(_,mode)|mode);
    trace_compile(match module_mode { 1 => "shadow4", 2 => "shadow8", _ => "surface" });
    let main_source = if module_mode == 1 { shadow_receiver::four_map_multitexture_source() }
        else if module_mode == 2 { shadow_receiver::eight_map_multitexture_source() }
        else if bump_receiver { bump_receiver::source() }
        else { shader_profile::without_shadows(WGSL_SHADER) };
    let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("coin_standard.wgsl"),
        source: wgpu::ShaderSource::Wgsl(shader_source(
            &format!("{}{}",main_source,weighted::FRAGMENT)).into()),
    });

    let instanced_shader = if portable_module.is_some() { shader.clone() } else { device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("Coin opaque instances"),
        source: wgpu::ShaderSource::Wgsl(shader_source(
            &format!("{}{}", shader_profile::sampling_profile(&instancing::shader_source(), false), weighted::FRAGMENT)).into()),
    }) };

    let shadow_shader = if portable_module.is_some() { shader.clone() } else { device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("Coin quality shadow receivers"),
        source: wgpu::ShaderSource::Wgsl(shader_source(
            &format!("{}{}",if bump_receiver { bump_receiver::source() } else if shadow_multitexture { shadow_receiver::four_map_multitexture_source() } else { shadow_receiver::four_map_source() },weighted::FRAGMENT)).into()),
    }) };
    let shadow_eight_shader = if portable_module.is_some() { shader.clone() } else { device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("Coin eight shadow receivers"),
        source: wgpu::ShaderSource::Wgsl(shader_source(
            &format!("{}{}",if bump_receiver { bump_receiver::eight_source() } else if shadow_multitexture { shadow_receiver::eight_map_multitexture_source() } else { shadow_receiver::eight_map_source() },weighted::FRAGMENT)).into()),
    }) };
    let line_shader = if portable_module.is_some() { shader.clone() } else { device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("coin_line.wgsl"),
        source: wgpu::ShaderSource::Wgsl(shader_source(
            &format!("{}{}",WGSL_LINE_SHADER,weighted::FRAGMENT)).into()),
    }) };

    let point_shader = if portable_module.is_some() { shader.clone() } else { device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("coin_point.wgsl"),
        source: wgpu::ShaderSource::Wgsl(shader_source(
            &format!("{}{}",WGSL_POINT_SHADER,weighted::FRAGMENT)).into()),
    }) };

    let storage_visibility = if device.limits().max_storage_buffers_per_shader_stage >= 1 {
        wgpu::ShaderStages::VERTEX_FRAGMENT
    } else {
        wgpu::ShaderStages::FRAGMENT
    };

    let mut layout_entries = vec![
        wgpu::BindGroupLayoutEntry { binding: 0, visibility: wgpu::ShaderStages::VERTEX_FRAGMENT,
            ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform,
                has_dynamic_offset: true,
                min_binding_size: std::num::NonZeroU64::new(uniform_binding_bytes(false, policy) as u64) }, count: None },
        wgpu::BindGroupLayoutEntry { binding: 1, visibility: storage_visibility,
            ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Storage { read_only: true },
                has_dynamic_offset: false, min_binding_size: None }, count: None },
    ];
    for unit in 0..8 {
        layout_entries.push(wgpu::BindGroupLayoutEntry { binding: 2 + 2 * unit,
            visibility: wgpu::ShaderStages::FRAGMENT,
            ty: wgpu::BindingType::Texture {
                sample_type: wgpu::TextureSampleType::Float { filterable: true },
                view_dimension: wgpu::TextureViewDimension::D2,
                multisampled: false,
            },
            count: None,
        });
        layout_entries.push(wgpu::BindGroupLayoutEntry {
            binding: 3 + 2 * unit,
            visibility: wgpu::ShaderStages::FRAGMENT,
            ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
            count: None,
        });
    }
    for binding in [18, 19] {
        layout_entries.push(wgpu::BindGroupLayoutEntry {
            binding,
            visibility: wgpu::ShaderStages::FRAGMENT,
            ty: wgpu::BindingType::Texture {
                sample_type: if gl_depth_loads { wgpu::TextureSampleType::Float { filterable: false } }
                    else { wgpu::TextureSampleType::Depth },
                view_dimension: wgpu::TextureViewDimension::D2,
                multisampled: multisampled_peel,
            },
            count: None,
        });
    }
    layout_entries.push(wgpu::BindGroupLayoutEntry {
        binding: 20,
        visibility: wgpu::ShaderStages::FRAGMENT,
        ty: wgpu::BindingType::Texture {
            sample_type: wgpu::TextureSampleType::Float { filterable: false },
            view_dimension: if shadow_multitexture { wgpu::TextureViewDimension::D2Array } else { wgpu::TextureViewDimension::D2 },
            multisampled: false,
        },
        count: None,
    });
    if !shadow_multitexture {
    layout_entries.push(wgpu::BindGroupLayoutEntry {
        binding: 21,
        visibility: wgpu::ShaderStages::FRAGMENT,
        ty: wgpu::BindingType::Texture {
            sample_type: wgpu::TextureSampleType::Float { filterable: false },
            view_dimension: wgpu::TextureViewDimension::D2,
            multisampled: false,
        },
        count: None,
    });
    layout_entries.push(wgpu::BindGroupLayoutEntry {
        binding: 22, visibility: wgpu::ShaderStages::FRAGMENT,
        ty: wgpu::BindingType::Texture {
            sample_type: wgpu::TextureSampleType::Float { filterable: false },
            view_dimension: wgpu::TextureViewDimension::D2, multisampled: false,
        }, count: None,
    });
    layout_entries.push(wgpu::BindGroupLayoutEntry {
        binding: 23, visibility: wgpu::ShaderStages::FRAGMENT,
        ty: wgpu::BindingType::Texture {
            sample_type: wgpu::TextureSampleType::Float { filterable: false },
            view_dimension: wgpu::TextureViewDimension::D2, multisampled: false,
        }, count: None,
    });
    }
    if bump_receiver {
      layout_entries.push(wgpu::BindGroupLayoutEntry {binding:25,visibility:wgpu::ShaderStages::FRAGMENT,
        ty:wgpu::BindingType::Texture {sample_type:wgpu::TextureSampleType::Float {filterable:true},view_dimension:wgpu::TextureViewDimension::D2,multisampled:false},count:None});
      layout_entries.push(wgpu::BindGroupLayoutEntry {binding:26,visibility:wgpu::ShaderStages::FRAGMENT,
        ty:wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),count:None});
    }
    if portable_module.is_some() {
        layout_entries.push(wgpu::BindGroupLayoutEntry {
            binding: 27, visibility: wgpu::ShaderStages::FRAGMENT,
            ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform,
                has_dynamic_offset: false, min_binding_size: std::num::NonZeroU64::new(4096) },
            count: None,
        });
    }
    let bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("Coin eight-unit texture program"),
        entries: &layout_entries,
    });

    let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
        label: Some("Coin Pipeline Layout"),
        bind_group_layouts: &[&bind_group_layout],
        push_constant_ranges: &[],
    });

    let mut instanced_entries = layout_entries.clone();
    instanced_entries[0].ty = wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform,
        has_dynamic_offset: true,
        min_binding_size: std::num::NonZeroU64::new(std::mem::size_of::<CoinWgpuUniforms>() as u64) };
    instanced_entries.push(wgpu::BindGroupLayoutEntry {
        binding: 24, visibility: wgpu::ShaderStages::VERTEX,
        ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Storage { read_only: true },
            has_dynamic_offset: false, min_binding_size: std::num::NonZeroU64::new(144) },
        count: None,
    });
    let instanced_bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("Coin opaque instance bindings"), entries: &instanced_entries,
    });
    let instanced_pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
        label: Some("Coin opaque instance pipeline"), bind_group_layouts: &[&instanced_bind_group_layout],
        push_constant_ranges: &[],
    });

    let eight_entries: Vec<_> = layout_entries.iter().copied()
        .filter(|entry| shadow_multitexture || !(10..18).contains(&entry.binding)).collect();
    let shadow_eight_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("Coin bounded eight-map scene bindings"), entries: &eight_entries,
    });
    let mut extra_entries = vec![wgpu::BindGroupLayoutEntry {
        binding: 0, visibility: wgpu::ShaderStages::VERTEX_FRAGMENT,
        ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform,
            has_dynamic_offset: false, min_binding_size: None }, count: None,
    }];
    for binding in (1..5).filter(|_| !shadow_multitexture) {
        extra_entries.push(wgpu::BindGroupLayoutEntry {
            binding, visibility: wgpu::ShaderStages::FRAGMENT,
            ty: wgpu::BindingType::Texture {
                sample_type: wgpu::TextureSampleType::Float { filterable: false },
                view_dimension: wgpu::TextureViewDimension::D2, multisampled: false,
            }, count: None,
        });
    }
    let shadow_extra_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("Coin extra four shadow maps"), entries: &extra_entries,
    });
    let shadow_eight_pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
        label: Some("Coin eight shadow pipeline"),
        bind_group_layouts: &[&shadow_eight_layout, &shadow_extra_layout], push_constant_ranges: &[],
    });

    SurfaceSamplingProfile {
        policy, multisampled_peel, sample_depth, shadow_multitexture, bump_receiver,
        shader_module: shader,
        instanced_shader, instanced_bind_group_layout, instanced_pipeline_layout,
        shadow_shader, shadow_eight_shader, shadow_eight_layout, shadow_extra_layout, shadow_eight_pipeline_layout,
        line_shader_module: line_shader,
        point_shader_module: point_shader,
        bind_group_layout,
        pipeline_layout,
        pipelines: Mutex::new(HashMap::new()),
        untextured_shaders: Mutex::new(HashMap::new()),
    }
}

// Diagnostic sizes cover selected bridge-owned caches and primary attachments.
// They are nominal payload bytes, not driver allocations or total GPU memory.
pub(super) fn trace_owned_resources(dev: &DeviceState, target: &str, width: u32, height: u32,
                         staging_color_bytes: u64, staging_depth_bytes: u64) {
    let geometry = dev.cache.lock().unwrap();
    let retired_geometry_bytes: u64 = geometry.deferred_release.iter()
        .map(|entry| entry.size_bytes).sum();
    let mut vertex_buffers = geometry.active_entries.len() + geometry.deferred_release.len();
    let mut index_buffers = geometry.active_entries.values()
        .filter(|(_, entry)| entry.index_buffer.is_some()).count()
        + geometry.deferred_release.iter()
            .filter(|entry| entry.index_buffer.is_some()).count();
    let mut geometry_active_bytes = geometry.total_active_geometry_bytes;
    drop(geometry);
    let frame = dev.frame_buffers.lock().unwrap();
    let frame_geometry_bytes = frame.as_ref().map_or(0, |entry| entry.geometry_bytes);
    geometry_active_bytes += frame_geometry_bytes;
    vertex_buffers += usize::from(frame.is_some());
    index_buffers += usize::from(frame.is_some());
    drop(frame);
    let instances = dev.instanced_buffers.lock().unwrap();
    if let Some(entry) = instances.as_ref() {
        geometry_active_bytes += entry.geometry_bytes;
        vertex_buffers += 1; index_buffers += 1;
        eprintln!("COIN_RENDER_PHASE rust_instance_resources geometry_bytes={} instance_bytes={} material_bytes={}",
            entry.geometry_bytes, entry.instance_bytes, entry.material_bytes);
    }
    drop(instances);
    let textures = dev.texture_cache.lock().unwrap();
    let texture_payload_bytes: u64 = textures.entries.values()
        .map(|entry| entry.size_bytes as u64).sum();
    let texture_count = textures.entries.len() + textures.retired.len();
    drop(textures);
    let rtt = dev.rtt_textures.lock().unwrap();
    let rtt_count = rtt.active.len() + rtt.retired.len();
    let rtt_nominal_bytes: u64 = rtt.active.values()
        .map(|entry| u64::from(entry.width) * u64::from(entry.height) * 4).sum::<u64>()
        + rtt.retired.iter().map(|entry| u64::from(entry.resource.width)
            * u64::from(entry.resource.height) * 4).sum::<u64>();
    drop(rtt);
    let pool = dev.readback_pool.lock().unwrap();
    let staging_pool_free_bytes = pool.free_bytes;
    let staging_pool_free_buffers = pool.free_count;
    drop(pool);
    let pixels = u64::from(width) * u64::from(height);
    let attachment_count = if target == "window" { 1 } else { 2 };
    let attachment_nominal_bytes = pixels * 4 * attachment_count;
    eprintln!("COIN_RENDER_PHASE rust_resources target={} geometry_active_bytes={} geometry_retired_bytes={} vertex_buffers={} index_buffers={} texture_cache_count={} texture_payload_bytes={} rtt_texture_count={} rtt_color_nominal_bytes={} attachment_textures={} attachment_nominal_bytes={} staging_frame_color_bytes={} staging_frame_depth_bytes={} staging_pool_free_bytes={} staging_pool_free_buffers={} gpu_memory_used_bytes=unavailable framebuffer_count=unavailable",
        target, geometry_active_bytes, retired_geometry_bytes, vertex_buffers, index_buffers,
        texture_count, texture_payload_bytes, rtt_count, rtt_nominal_bytes, attachment_count,
        attachment_nominal_bytes, staging_color_bytes, staging_depth_bytes,
        staging_pool_free_bytes, staging_pool_free_buffers);
    eprintln!("COIN_RENDER_PHASE rust_draw_resources frame_geometry_bytes={} uniform_chunks={} draw_bind_groups={}",
        frame_geometry_bytes, dev.uniform_chunks.load(Ordering::Relaxed),
        dev.draw_bind_groups.load(Ordering::Relaxed));
}

pub(super) fn surface_wsi_tracing_enabled() -> bool {
    static ENABLED: OnceLock<bool> = OnceLock::new();
    *ENABLED.get_or_init(|| {
        std::env::var_os("COIN_WGPU_TRACE_WSI").is_some()
            || std::env::var_os("COIN_RENDER_TRACE_PHASES").is_some()
            || std::env::var_os("COIN_WGPU_TRACE_PHASES").is_some()
    })
}

// Keep the Coin publication context adjacent to the HAL's actual WSI calls.
// An injected result never claims that vkAcquireNextImageKHR was called.
pub(super) fn acquire_surface_texture(record: &SurfaceRecord, surface_id: CoinWgpuSurfaceId,
    attempt: u32, injected: Option<wgpu::SurfaceError>, trace: bool)
    -> Result<wgpu::SurfaceTexture, wgpu::SurfaceError>
{
    let source = if injected.is_some() { "injected" } else { "actual" };
    if trace {
        eprintln!("COIN_WGPU_WSI stage=get_current_texture event=begin source={} pid={} thread={:?} surface={} generation={} device_generation={} published_serial={} last_submitted_serial={} attempt={} native_type={} handle_a={:#x} handle_b={:#x}",
            source, std::process::id(), std::thread::current().id(), surface_id,
            record.surface_generation, record.configured_device_generation,
            record.published_serial, LAST_SUBMITTED_SERIAL.load(Ordering::SeqCst), attempt,
            record.native_desc.r#type, record.native_desc.handle_a, record.native_desc.handle_b);
    }
    let start = trace.then(std::time::Instant::now);
    let result = match injected {
        Some(error) => Err(error),
        None => record.surface.get_current_texture(),
    };
    if let Some(start) = start {
        let status = match &result {
            Ok(_) => "Ok",
            Err(wgpu::SurfaceError::Timeout) => "Timeout",
            Err(wgpu::SurfaceError::Outdated) => "Outdated",
            Err(wgpu::SurfaceError::Lost) => "Lost",
            Err(wgpu::SurfaceError::OutOfMemory) => "OutOfMemory",
            Err(wgpu::SurfaceError::Other) => "Other",
        };
        eprintln!("COIN_WGPU_WSI stage=get_current_texture event=end source={} pid={} thread={:?} surface={} generation={} attempt={} result={} elapsed_ms={:.6}",
            source, std::process::id(), std::thread::current().id(), surface_id,
            record.surface_generation, attempt, status, start.elapsed().as_secs_f64() * 1000.0);
    }
    result
}

pub(super) fn update_readback_mapping(job: &mut PendingReadback) {
    if job.color_ready.is_none() {
        match job.color_receiver.try_recv() {
            Ok(Ok(())) => job.color_ready = Some(Ok(())),
            Ok(Err(error)) => job.color_ready = Some(Err(format!("{:?}", error))),
            Err(std::sync::mpsc::TryRecvError::Disconnected) =>
                job.color_ready = Some(Err("Color mapping channel disconnected".to_string())),
            Err(std::sync::mpsc::TryRecvError::Empty) => (),
        }
    }
    if job.depth_ready.is_none() {
        if let Some(receiver) = job.depth_receiver.as_ref() {
            match receiver.try_recv() {
                Ok(Ok(())) => job.depth_ready = Some(Ok(())),
                Ok(Err(error)) => job.depth_ready = Some(Err(format!("{:?}", error))),
                Err(std::sync::mpsc::TryRecvError::Disconnected) =>
                    job.depth_ready = Some(Err("Depth mapping channel disconnected".to_string())),
                Err(std::sync::mpsc::TryRecvError::Empty) => (),
            }
        } else {
            job.depth_ready = Some(Ok(()));
        }
    }
}

pub(super) fn reap_cancelled_readbacks(runtime: &mut RuntimeContext) {
    if let Some(device) = runtime.device_state.as_ref() {
        let _ = device.device.poll(wgpu::Maintain::Poll);
    }
    for device in runtime.extra_devices.values() {
        let _ = device.device.poll(wgpu::Maintain::Poll);
    }
    runtime.retired_readbacks.retain_mut(|job| {
        update_readback_mapping(job);
        if job.color_ready.is_none() || job.depth_ready.is_none() {
            return true;
        }
        if matches!(job.color_ready.as_ref(), Some(Ok(()))) {
            job.color.unmap();
            if let Ok(mut pool) = job.pool.lock() {
                pool.recycle(job.color.clone());
            }
        }
        if matches!(job.depth_ready.as_ref(), Some(Ok(()))) {
            if let Some(buffer) = job.depth.as_ref() {
                buffer.unmap();
                if let Ok(mut pool) = job.pool.lock() {
                    pool.recycle(buffer.clone());
                }
            }
        }
        false
    });
}

// Rust statics have no process-exit Drop. Register after native initialization,
// rather than at library load: exit callbacks run in reverse registration order,
// so our GPU objects must be released before the driver's own exit callbacks.
// A new device or lazy program cache may initialize another native module and
// therefore registers again. Earlier callbacks are harmless once take() is done.
pub(super) fn register_runtime_exit() -> Result<(), String> {
    if unsafe { atexit(shutdown_runtime_at_exit) } == 0 { Ok(()) }
    else { Err("Cannot register WebGPU runtime process-exit cleanup".into()) }
}

pub(super) extern "C" fn shutdown_runtime_at_exit() {
    // Drop directly on the thread executing process exit: no transfer to a
    // cleanup thread or native-context affinity change. The vendored wgpu-core
    // tolerates unavailable diagnostic TLS during these late resource Drops.
    // Recover a poisoned lock so an earlier failed call cannot retain a device.
    let _ = std::panic::catch_unwind(|| {
        let runtime = {
            let mut guard = RUNTIME_CTX.lock().unwrap_or_else(|error| error.into_inner());
            RUNTIME_EXIT_STARTED.store(true, Ordering::SeqCst);
            guard.take()
        };
        if let Some(mut runtime) = runtime {
            // Serialize with API users through RUNTIME_CTX above, then let GPU
            // completions run without that mutex. Callbacks use only their own
            // signals/channels. No ticket or resource is invalidated during
            // normal operation; this path is entered only by process exit.
            if let Some(state) = runtime.device_state.as_ref() {
                let _ = state.device.poll(wgpu::Maintain::Wait);
            }
            for state in runtime.extra_devices.values() {
                let _ = state.device.poll(wgpu::Maintain::Wait);
            }
            // Surface drop unconfigures presentation while devices/Instance
            // are still live. Retire mapped buffers before their owning pools
            // and cached programs, then release all logical devices.
            runtime.surfaces.clear();
            runtime.pending_readbacks.clear();
            runtime.retired_readbacks.clear();
            runtime.device_state = None;
            runtime.extra_devices.clear();
            drop(runtime);
        }
    });
}

#[cfg(test)]
pub(super) fn gpu_test_instance() -> wgpu::Instance {
    static INSTANCE: std::sync::OnceLock<wgpu::Instance> = std::sync::OnceLock::new();
    let backends = wgpu::Backends::from_env().unwrap_or(wgpu::Backends::all());
    let create = || wgpu::Instance::new(&wgpu::InstanceDescriptor {
        backends, ..Default::default()
    });
    // A GL adapter's EGL context is thread-bound. The Vulkan tests can share
    // one Instance while independently requesting logical Devices in parallel.
    if backends == wgpu::Backends::GL { create() }
    else { INSTANCE.get_or_init(create).clone() }
}

pub(super) unsafe fn create_surface_from_descriptor(
    instance: &wgpu::Instance,
    desc: &CoinWgpuNativeSurfaceDescriptor,
) -> Result<wgpu::Surface<'static>, String> {
    if desc.r#type == 1 {
        #[cfg(all(unix, not(target_os = "android")))]
        {
            // Xlib
            let display_ptr = desc.handle_a as *mut std::ffi::c_void;
            let window_xid = desc.handle_b.try_into()
                .map_err(|_| "Xlib window ID exceeds the platform handle size".to_string())?;
            let nn_display = std::ptr::NonNull::new(display_ptr)
                .ok_or_else(|| "Display pointer must be non-null".to_string())?;
            let display_handle = XlibDisplayHandle::new(Some(nn_display), 0);
            let window_handle = XlibWindowHandle::new(window_xid);

            let raw_display = RawDisplayHandle::Xlib(display_handle);
            let raw_window = RawWindowHandle::Xlib(window_handle);

            let target = wgpu::SurfaceTargetUnsafe::RawHandle {
                raw_display_handle: raw_display,
                raw_window_handle: raw_window,
            };
            instance
                .create_surface_unsafe(target)
                .map_err(|e| format!("Failed to create X11 surface: {}", e))
        }
        #[cfg(not(all(unix, not(target_os = "android"))))]
        {
            Err("Xlib surface requires a Unix platform with X11".to_string())
        }
    } else if desc.r#type == 2 {
        #[cfg(target_os = "linux")]
        {
            let display = std::ptr::NonNull::new(desc.handle_a as *mut std::ffi::c_void)
                .ok_or_else(|| "wl_display must be non-null".to_string())?;
            let surface = std::ptr::NonNull::new(desc.handle_b as *mut std::ffi::c_void)
                .ok_or_else(|| "wl_surface must be non-null".to_string())?;
            let target = wgpu::SurfaceTargetUnsafe::RawHandle {
                raw_display_handle: RawDisplayHandle::Wayland(WaylandDisplayHandle::new(display)),
                raw_window_handle: RawWindowHandle::Wayland(WaylandWindowHandle::new(surface)),
            };
            instance.create_surface_unsafe(target)
                .map_err(|e| format!("Failed to create Wayland surface: {}", e))
        }
        #[cfg(not(target_os = "linux"))]
        {
            Err("Wayland surface requires Linux".to_string())
        }
    } else if desc.r#type == 3 {
        #[cfg(target_os = "windows")]
        {
            let hwnd = std::num::NonZeroIsize::new(desc.handle_b as isize)
                .ok_or_else(|| "HWND must be non-null".to_string())?;
            let mut window_handle = Win32WindowHandle::new(hwnd);
            window_handle.hinstance = std::num::NonZeroIsize::new(desc.handle_a as isize);
            let target = wgpu::SurfaceTargetUnsafe::RawHandle {
                raw_display_handle: RawDisplayHandle::Windows(WindowsDisplayHandle::new()),
                raw_window_handle: RawWindowHandle::Win32(window_handle),
            };
            instance.create_surface_unsafe(target)
                .map_err(|e| format!("Failed to create Win32 surface: {}", e))
        }
        #[cfg(not(target_os = "windows"))]
        {
            Err("Win32 surface requires Windows".to_string())
        }
    } else if desc.r#type == 4 {
        #[cfg(target_os = "macos")]
        {
            if desc.handle_a == 0 {
                return Err("CAMetalLayer must be non-null".to_string());
            }
            instance.create_surface_unsafe(wgpu::SurfaceTargetUnsafe::CoreAnimationLayer(
                desc.handle_a as *mut std::ffi::c_void,
            )).map_err(|e| format!("Failed to create CAMetalLayer surface: {}", e))
        }
        #[cfg(not(target_os = "macos"))]
        {
            Err("AppKit layer requires macOS".to_string())
        }
    } else if desc.r#type == 5 {
        #[cfg(target_os = "android")]
        {
            let window = std::ptr::NonNull::new(desc.handle_a as *mut std::ffi::c_void)
                .ok_or_else(|| "ANativeWindow must be non-null".to_string())?;
            let target = wgpu::SurfaceTargetUnsafe::RawHandle {
                raw_display_handle: RawDisplayHandle::Android(AndroidDisplayHandle::new()),
                raw_window_handle: RawWindowHandle::AndroidNdk(AndroidNdkWindowHandle::new(window)),
            };
            instance.create_surface_unsafe(target)
                .map_err(|e| format!("Failed to create Android NDK surface: {}", e))
        }
        #[cfg(not(target_os = "android"))]
        {
            Err("Android NDK surface requires Android".to_string())
        }
    } else {
        Err("Native surface platform not supported".to_string())
    }
}

pub(super) fn init_runtime_if_needed() -> Result<(), String> {
    #[cfg(target_os = "android")]
    android_log::init();
    let mut guard = RUNTIME_CTX.lock().map_err(|e| e.to_string())?;
    if RUNTIME_EXIT_STARTED.load(Ordering::SeqCst) {
        return Err("WebGPU runtime is shutting down with the process".into());
    }
    if guard.is_none() {
        *guard = Some(RuntimeContext {
            instance: wgpu::Instance::new(&wgpu::InstanceDescriptor::from_env_or_default()),
            surfaces: HashMap::new(),
            device_state: None,
            device_generation: {
                #[cfg(target_os = "android")]
                { ANDROID_RUNTIME_GENERATION.load(Ordering::SeqCst) }
                #[cfg(not(target_os = "android"))]
                { 0 }
            },
            pending_readbacks: HashMap::new(),
            retired_readbacks: Vec::new(),
            extra_devices: HashMap::new(),
            extra_generations: HashMap::new(),
            dead_readbacks: HashMap::new(),
        });
        if let Err(error) = register_runtime_exit() {
            *guard = None;
            return Err(error);
        }
    }
    Ok(())
}

// NativeActivity may finish its render thread while the process stays alive.
// Drop the idle EGL/device on that thread, before it exits. Never invalidate a
// surviving target, direct RTT publication, explicit device, or readback ticket.
#[cfg(target_os = "android")]
#[no_mangle]
pub extern "C" fn coin_wgpu_release_idle_android_runtime() -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        let mut guard = match RUNTIME_CTX.lock() {
            Ok(guard) => guard,
            Err(_) => return CoinWgpuStatus::BackendError,
        };
        let Some(runtime) = guard.as_mut() else { return CoinWgpuStatus::Ok; };
        reap_cancelled_readbacks(runtime);
        if !runtime.surfaces.is_empty() || !runtime.pending_readbacks.is_empty()
            || !runtime.retired_readbacks.is_empty() || !runtime.extra_devices.is_empty()
            || !runtime.extra_generations.is_empty()
        {
            return CoinWgpuStatus::NotReady;
        }
        if let Some(device) = runtime.device_state.as_ref() {
            match device.rtt_textures.lock() {
                Ok(registry) if registry.active.is_empty() => (),
                _ => return CoinWgpuStatus::NotReady,
            }
            let _ = device.device.poll(wgpu::Maintain::Wait);
        }
        let Some(next_generation) = runtime.device_generation.checked_add(1) else {
            return CoinWgpuStatus::BackendError;
        };
        // Advance the epoch across process-local Activity lifetimes. Global
        // ticket/token/submission IDs are intentionally never reset.
        ANDROID_RUNTIME_GENERATION.store(next_generation, Ordering::SeqCst);
        *guard = None;
        DEVICE_LOST_OCCURRED.store(false, Ordering::SeqCst);
        log::info!("Released idle Android renderer; next_generation={next_generation}");
        CoinWgpuStatus::Ok
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

pub(super) fn renderer_backend(renderer: u32) -> Result<Option<wgpu::Backends>, String> {
    match renderer {
        0 => Ok(None),
        1 => Ok(Some(wgpu::Backends::VULKAN)),
        2 => Ok(Some(wgpu::Backends::GL)),
        4 => Ok(Some(wgpu::Backends::DX12)),
        5 => Ok(Some(wgpu::Backends::METAL)),
        _ => Err("Invalid requested surface renderer".to_string()),
    }
}

pub(super) fn adapter_renderer(backend: wgpu::Backend) -> u32 {
    match backend {
        wgpu::Backend::Vulkan => 1,
        wgpu::Backend::Gl => 2,
        wgpu::Backend::Dx12 => 4,
        wgpu::Backend::Metal => 5,
        _ => 3,
    }
}

pub(super) fn negotiated_device_limits(adapter_limits: &wgpu::Limits) -> wgpu::Limits {
    let mut limits = wgpu::Limits::default();
    // GLES adapters may expose fewer storage bindings than the default profile.
    limits.max_storage_buffers_per_shader_stage = limits.max_storage_buffers_per_shader_stage
        .min(adapter_limits.max_storage_buffers_per_shader_stage);
    // Negotiate the real extended interface without excluding devices that
    // still support the established smaller profiles.
    if adapter_limits.max_inter_stage_shader_components >= 80 {
        limits.max_inter_stage_shader_components = 80;
    }
    limits
}

pub(super) fn get_or_init_device_impl<'a>(
    runtime: &'a mut RuntimeContext,
    target_surface: Option<&wgpu::Surface>,
    isolated: bool,
    requested_renderer: u32,
) -> Result<&'a mut DeviceState, String> {
    // Check if device loss occurred asynchronously
    if !isolated && DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst) {
        runtime.device_state = None;
        runtime.device_generation += 1;
        LAST_ASYNC_ERROR_KIND.store(0, Ordering::SeqCst);
        if let Ok(mut lock) = LAST_ASYNC_ERROR_MSG.lock() {
            lock.clear();
        }
    }

    let backend = renderer_backend(requested_renderer)?;
    if runtime.device_state.is_some() {
        if requested_renderer != 0 &&
            adapter_renderer(runtime.device_state.as_ref().unwrap().adapter.get_info().backend) != requested_renderer {
            return Err("Requested renderer does not match the active wgpu adapter; no fallback was applied".to_string());
        }
        return Ok(runtime.device_state.as_mut().unwrap());
    }

    let adapter = if let Some(backends) = backend {
        runtime.instance.enumerate_adapters(backends).into_iter().find(|candidate| {
            log::debug!("Requested renderer candidate: {:?}", candidate.get_info());
            if candidate.get_info().device_type == wgpu::DeviceType::Cpu { return false; }
            target_surface.map_or(true, |surface| {
                let caps = surface.get_capabilities(candidate);
                log::debug!("Surface capabilities: {:?}", caps);
                !caps.formats.is_empty() && !caps.present_modes.is_empty()
            })
        }).ok_or_else(|| "No physical adapter for the requested renderer and surface".to_string())?
    } else {
        block_on(runtime.instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            compatible_surface: target_surface,
            force_fallback_adapter: false,
        })).ok_or_else(|| "No compatible GPU adapter found".to_string())?
    };

    let adapter_info = adapter.get_info();
    let adapter_name = format!("{} ({:?})", adapter_info.name, adapter_info.backend);

    // Hardware timestamps are strictly opt-in and never required for normal
    // rendering. An unsupported adapter still runs the CPU phase trace.
    let timestamp_features = wgpu::Features::TIMESTAMP_QUERY
        | wgpu::Features::TIMESTAMP_QUERY_INSIDE_ENCODERS;
    let enable_timestamps = std::env::var_os("COIN_RENDER_TRACE_PHASES").or_else(|| std::env::var_os("COIN_WGPU_TRACE_PHASES")).is_some()
        && std::env::var("COIN_WGPU_GPU_TIMESTAMPS").as_deref() == Ok("1")
        && adapter.features().contains(timestamp_features);
    let mut required_features = if enable_timestamps { timestamp_features }
        else { wgpu::Features::empty() };
    if adapter.features().contains(wgpu::Features::FLOAT32_FILTERABLE) {
        required_features |= wgpu::Features::FLOAT32_FILTERABLE;
    }
    if adapter.features().contains(wgpu::Features::TEXTURE_COMPRESSION_BC) {
        required_features |= wgpu::Features::TEXTURE_COMPRESSION_BC;
    }
    // MULTISAMPLE_X2 is format-specific in wgpu. Enabling the adapter feature
    // here makes the advertised 2x capability usable by future target profiles.
    if adapter.features().contains(wgpu::Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES) {
        required_features |= wgpu::Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES;
    }
    let required_limits = negotiated_device_limits(&adapter.limits());
    let (device, queue) = block_on(adapter.request_device(
        &wgpu::DeviceDescriptor {
            label: Some("Coin3D WebGPU Device"),
            required_features,
            required_limits,
            memory_hints: wgpu::MemoryHints::Performance,
        },
        None,
    ))
    .map_err(|e| format!("Failed to create device: {}", e))?;

    let lost_signal = Arc::new(AtomicBool::new(false));
    let error_signal = Arc::new(AtomicU32::new(0));
    if isolated {
        let lost = lost_signal.clone();
        device.set_device_lost_callback(move |_, _| {
            lost.store(true, Ordering::SeqCst);
        });
        let error_kind = error_signal.clone();
        device.on_uncaptured_error(Box::new(move |error: wgpu::Error| {
            let _ = error_kind.compare_exchange(0,
                if matches!(error, wgpu::Error::OutOfMemory { .. }) { 1 } else { 2 },
                Ordering::SeqCst, Ordering::SeqCst);
        }));
    } else {
        device.set_device_lost_callback(|reason, message| {
            DEVICE_LOST_OCCURRED.store(true, Ordering::SeqCst);
            if let Ok(mut lock) = LAST_ASYNC_ERROR_MSG.lock() {
                *lock = format!("WebGPU device lost ({:?}): {}", reason, message);
            }
        });
        device.on_uncaptured_error(Box::new(|error: wgpu::Error| {
            if let Ok(mut lock) = LAST_ASYNC_ERROR_MSG.lock() {
                if LAST_ASYNC_ERROR_KIND.compare_exchange(0,
                    if matches!(error, wgpu::Error::OutOfMemory { .. }) { 1 } else { 2 },
                    Ordering::SeqCst, Ordering::SeqCst).is_ok() {
                    *lock = format!("WebGPU uncaptured error: {}", error);
                }
            }
        }));
    }

    let surface_native = create_surface_sampling_profile(&device, &adapter, SamplingPolicy::Native, false, false, false);

    if !isolated {
        DEVICE_LOST_OCCURRED.store(false, Ordering::SeqCst);
        LAST_ASYNC_ERROR_KIND.store(0, Ordering::SeqCst);
    }

    let default_texture = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("Coin Default 1x1 White Texture"),
        size: wgpu::Extent3d {
            width: 1,
            height: 1,
            depth_or_array_layers: 1,
        },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Rgba8Unorm,
        usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
        view_formats: &[],
    });
    queue.write_texture(
        wgpu::ImageCopyTexture {
            texture: &default_texture,
            mip_level: 0,
            origin: wgpu::Origin3d::ZERO,
            aspect: wgpu::TextureAspect::All,
        },
        &[255, 255, 255, 255],
        wgpu::ImageDataLayout {
            offset: 0,
            bytes_per_row: Some(4),
            rows_per_image: Some(1),
        },
        wgpu::Extent3d {
            width: 1,
            height: 1,
            depth_or_array_layers: 1,
        },
    );
    let default_texture_view = default_texture.create_view(&wgpu::TextureViewDescriptor::default());
    let default_sampler = device.create_sampler(&wgpu::SamplerDescriptor {
        label: Some("Coin Default Sampler"),
        address_mode_u: wgpu::AddressMode::Repeat,
        address_mode_v: wgpu::AddressMode::Repeat,
        address_mode_w: wgpu::AddressMode::Repeat,
        mag_filter: wgpu::FilterMode::Linear,
        min_filter: wgpu::FilterMode::Linear,
        mipmap_filter: wgpu::FilterMode::Nearest,
        ..Default::default()
    });

    let default_depth_view = device
        .create_texture(&wgpu::TextureDescriptor {
            label: Some("Coin unused depth binding"),
            size: wgpu::Extent3d {
                width: 1,
                height: 1,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Depth32Float,
            usage: wgpu::TextureUsages::TEXTURE_BINDING,
            view_formats: &[],
        })
        .create_view(&wgpu::TextureViewDescriptor::default());
    runtime.device_state = Some(DeviceState {
        adapter,
        adapter_name,
        device,
        queue,
        surface_native,
        surface_portable: OnceLock::new(),
        surface_depth_native: OnceLock::new(),
        surface_depth_portable: OnceLock::new(),
        surface_peel_native: OnceLock::new(),
        surface_peel_portable: OnceLock::new(),
        surface_shadow_multitexture_peel_native: OnceLock::new(),
        surface_shadow_multitexture_peel_portable: OnceLock::new(),
        shadow_multitexture_alpha_admission: Mutex::new(HashMap::new()),
        surface_shadow_multitexture_native: OnceLock::new(),
        surface_shadow_multitexture_portable: OnceLock::new(),
        surface_shadow_multitexture_depth_native: OnceLock::new(),
        surface_shadow_multitexture_depth_portable: OnceLock::new(),
        shadow_multitexture_depth_admission: std::array::from_fn(|_| OnceLock::new()),
        shadow_multitexture_admission: std::array::from_fn(|_| OnceLock::new()),
        annotation_depth_pipeline: Mutex::new(HashMap::new()),
        weighted_sample_support: Mutex::new(HashMap::new()),
        peeling_sample_support: Mutex::new(HashMap::new()),
        raster_image_pipeline: OnceLock::new(),
        bump_pipelines: Mutex::new(HashMap::new()),
        bump_base_cache: Mutex::new(BumpBaseCache::default()),
        bump_receivers: Mutex::new(HashMap::new()),
        portable_profiles: Mutex::new(portable::ProfileCache::default()),
        multipass_programs: multipass::Cache::default(),
        depth_readback: Mutex::new(None),
        depth_snapshot: Mutex::new(None),
        pipeline_compilations: AtomicU64::new(0),
        pipeline_hits: AtomicU64::new(0),
        cache: Mutex::new(GeometryCache::default()),
        default_texture,
        default_texture_view,
        default_depth_view,
        default_sampler,
        texture_cache: Mutex::new(TextureCache::default()),
        sampler_cache: Mutex::new(SamplerCache::default()),
        camera_bindings: Mutex::new(None),
        frame_buffers: Mutex::new(None),
        instanced_buffers: Mutex::new(None),
        camera_bindings_reused: AtomicU32::new(0),
        camera_bindings_created: AtomicU32::new(0),
        draw_bind_groups: AtomicU32::new(0),
        uniform_chunks: AtomicU32::new(0),
        validated_scene: None,
        camera_snapshot_policy: CameraSnapshotPolicy::default(),
        readback_pool: Arc::new(Mutex::new(ReadbackPool::default())),
        cached_offscreen_attachments: None,
        target_depth_histories: HashMap::new(),
        rtt_textures: Mutex::new(RttRegistry::default()),
        device_id: 0,
        generation: runtime.device_generation,
        completed_serial: Arc::new(AtomicU64::new(0)),
        last_submitted_serial: AtomicU64::new(0),
        lost: lost_signal,
        async_error: error_signal,
        #[cfg(any(test, feature = "test-hooks"))]
        injected_fault: AtomicI32::new(0),
    });

    if let Err(error) = register_runtime_exit() {
        runtime.device_state = None;
        return Err(error);
    }
    Ok(runtime.device_state.as_mut().unwrap())
}

pub(super) fn get_or_init_device<'a>(
    runtime: &'a mut RuntimeContext,
    target_surface: Option<&wgpu::Surface>,
) -> Result<&'a mut DeviceState, String> {
    get_or_init_device_impl(runtime, target_surface, false, 0)

}

pub(super) fn make_extra_device(runtime: &RuntimeContext, id: CoinWgpuDeviceId, generation: u64) -> Result<DeviceState, String> {
    // Only the default context owns surfaces. Every extra context requests its
    // own logical Device/Queue. Share the process runtime's Instance so device
    // recreation does not race Vulkan loader instance teardown on other threads.
    let mut isolated = RuntimeContext {
        instance: runtime.instance.clone(),
        surfaces: HashMap::new(),
        device_state: None,
        device_generation: 0,
        pending_readbacks: HashMap::new(),
        retired_readbacks: Vec::new(),
        extra_devices: HashMap::new(),
        extra_generations: HashMap::new(),
        dead_readbacks: HashMap::new(),
    };
    get_or_init_device_impl(&mut isolated, None, true, 0)?;
    let mut state = isolated.device_state.take().unwrap();
    state.device_id = id;
    state.generation = generation;
    Ok(state)
}

pub(super) fn invalidate_extra_readbacks(runtime: &mut RuntimeContext, id: CoinWgpuDeviceId) {
    runtime.pending_readbacks.retain(|token, job| {
        if job.device_id == id {
            runtime.dead_readbacks.insert(*token, (job.ticket, CoinWgpuStatus::DeviceLost));
            false
        } else { true }
    });
    runtime.retired_readbacks.retain(|job| job.device_id != id);
    if runtime.dead_readbacks.len() > 1024 { runtime.dead_readbacks.clear(); }
}

pub(super) fn lose_extra_device(runtime: &mut RuntimeContext, id: CoinWgpuDeviceId) {
    runtime.extra_devices.remove(&id);
    if let Some(generation) = runtime.extra_generations.get_mut(&id) {
        *generation = generation.wrapping_add(1);
    }
    invalidate_extra_readbacks(runtime, id);
}

pub(super) fn offscreen_device<'a>(runtime: &'a mut RuntimeContext, id: CoinWgpuDeviceId)
    -> Result<&'a mut DeviceState, String> {
    if id == 0 {
        return get_or_init_device(runtime, None);
    }
    let generation = *runtime.extra_generations.get(&id)
        .ok_or_else(|| format!("Unknown or destroyed WebGPU device {}", id))?;
    if !runtime.extra_devices.contains_key(&id) {
        runtime.extra_devices.insert(id, make_extra_device(runtime, id, generation)?);
    }
    Ok(runtime.extra_devices.get_mut(&id).unwrap())
}

#[no_mangle]
pub extern "C" fn coin_wgpu_device_create(
    out_id: *mut u64,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if out_id.is_null() || (out_id as usize) % std::mem::align_of::<u64>() != 0 {
            set_error(error_buf, error_buf_len, "Null or misaligned device ID output");
            return CoinWgpuStatus::InvalidArgument;
        }
        unsafe { *out_id = 0; }
        if let Err(message) = init_runtime_if_needed() {
            set_error(error_buf, error_buf_len, &message);
            return CoinWgpuStatus::BackendError;
        }
        let mut guard = RUNTIME_CTX.lock().unwrap();
        let runtime = guard.as_mut().unwrap();
        let id = NEXT_DEVICE_ID.fetch_add(1, Ordering::SeqCst);
        if id == 0 {
            set_error(error_buf, error_buf_len, "WebGPU device ID space exhausted");
            return CoinWgpuStatus::OutOfMemory;
        }
        let state = match make_extra_device(runtime, id, id) {
            Ok(state) => state,
            Err(message) => {
                set_error(error_buf, error_buf_len, &message);
                return CoinWgpuStatus::NotReady;
            }
        };
        runtime.extra_generations.insert(id, id);
        runtime.extra_devices.insert(id, state);
        unsafe { *out_id = id; }
        CoinWgpuStatus::Ok
    }).unwrap_or_else(|_| {
        set_error(error_buf, error_buf_len, "Panic while creating WebGPU device");
        CoinWgpuStatus::BackendError
    })
}

#[no_mangle]
pub extern "C" fn coin_wgpu_device_destroy(id: u64) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if id == 0 { return CoinWgpuStatus::InvalidArgument; }
        let mut guard = RUNTIME_CTX.lock().unwrap();
        let Some(runtime) = guard.as_mut() else { return CoinWgpuStatus::InvalidArgument; };
        if runtime.extra_generations.remove(&id).is_none() {
            return CoinWgpuStatus::InvalidArgument;
        }
        runtime.extra_devices.remove(&id);
        invalidate_extra_readbacks(runtime, id);
        CoinWgpuStatus::Ok
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

pub(super) fn configure_surface_record(
    record: &mut SurfaceRecord,
    device_state: &DeviceState,
    device_generation: u64,
    width: u32,
    height: u32,
) -> Result<(), String> {
    record.published_serial = 0;
    record.framebuffer_size = (width, height);
    if width == 0 || height == 0 {
        record.suspended = true;
        record.needs_reconfigure = true;
        record.depth_texture = None;
        record.depth_view = None;
        record.msaa_color_texture = None;
        record.msaa_color_view = None;
        record.presentation = None;
        return Ok(());
    }

    let caps = record.surface.get_capabilities(&device_state.adapter);
    if caps.formats.is_empty() {
        return Err("Surface capabilities report empty format list".to_string());
    }
    if caps.present_modes.is_empty() {
        return Err("Surface capabilities report empty present mode list".to_string());
    }

    // Negotiate format: Bgra8Unorm -> Rgba8Unorm -> first non-sRGB -> first available
    let chosen_format = if caps.formats.contains(&wgpu::TextureFormat::Bgra8Unorm) {
        wgpu::TextureFormat::Bgra8Unorm
    } else if caps.formats.contains(&wgpu::TextureFormat::Rgba8Unorm) {
        wgpu::TextureFormat::Rgba8Unorm
    } else if let Some(non_srgb) = caps.formats.iter().find(|f| !f.is_srgb()).copied() {
        non_srgb
    } else {
        caps.formats[0]
    };
    let presentation_format = if record.sample_count > 1 {
        Some(surface_present::linear_format(chosen_format)
            .ok_or("MSAA window presentation requires an RGBA8/BGRA8 surface")?)
    } else { None };
    if record.sample_count > 1 {
        let required = if record.sample_count == 2 {
            wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X2
        } else {
            wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X4
        };
        if !device_state.adapter.get_texture_format_features(wgpu::TextureFormat::Rgba8Unorm).flags.contains(required) ||
            !device_state.adapter.get_texture_format_features(wgpu::TextureFormat::Depth32Float).flags.contains(required) ||
            (record.sample_count == 2 && !device_state.device.features().contains(
                wgpu::Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES)) {
            return Err("Exact window MSAA sample count unavailable for color/depth".into());
        }
    }

    if presentation_format.is_some() &&
        !device_state.adapter.get_texture_format_features(wgpu::TextureFormat::Rgba8Unorm)
            .allowed_usages.contains(wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING) {
        return Err("MSAA window resolve requires renderable/sampleable RGBA8".into());
    }

    // The opt-in benchmark needs a non-vsync surface to compare with GLX/BGFX.
    // Refuse the campaign when the surface cannot provide one.
    let benchmark_no_vsync = std::env::var_os("COIN_RENDER_BENCH_NO_VSYNC").is_some();
    let chosen_present = if benchmark_no_vsync && caps.present_modes.contains(&wgpu::PresentMode::Immediate) {
        wgpu::PresentMode::Immediate
    } else if benchmark_no_vsync && caps.present_modes.contains(&wgpu::PresentMode::Mailbox) {
        wgpu::PresentMode::Mailbox
    } else if benchmark_no_vsync {
        return Err("Benchmark requested no-vsync, but surface supports neither Immediate nor Mailbox".to_string());
    } else if caps.present_modes.contains(&wgpu::PresentMode::AutoVsync) {
        wgpu::PresentMode::AutoVsync
    } else {
        wgpu::PresentMode::Fifo
    };

    // Negotiate alpha: Opaque if present, else first
    let chosen_alpha = if caps.alpha_modes.contains(&wgpu::CompositeAlphaMode::Opaque) {
        wgpu::CompositeAlphaMode::Opaque
    } else {
        caps.alpha_modes[0]
    };

    let config = wgpu::SurfaceConfiguration {
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT |
            (caps.usages & wgpu::TextureUsages::COPY_SRC),
        format: chosen_format,
        width,
        height,
        present_mode: chosen_present,
        alpha_mode: chosen_alpha,
        view_formats: presentation_format.filter(|format| *format != chosen_format)
            .into_iter().collect(),
        desired_maximum_frame_latency: 2,
    };

    record.surface.configure(&device_state.device, &config);

    // Create depth texture for this surface
    let depth_desc = wgpu::TextureDescriptor {
        label: Some("Window Surface Depth Texture"),
        size: wgpu::Extent3d {
            width,
            height,
            depth_or_array_layers: 1,
        },
        mip_level_count: 1,
        sample_count: record.sample_count,
        dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Depth32Float,
        // A live surface may switch from ordinary draws to peeling without
        // reconfiguration. Its opaque depth must always support the GPU snapshot.
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING |
            if record.sample_count == 1 { wgpu::TextureUsages::COPY_SRC }
            else { wgpu::TextureUsages::empty() },
        view_formats: &[],
    };
    let depth_texture = device_state.device.create_texture(&depth_desc);
    let depth_view = depth_texture.create_view(&wgpu::TextureViewDescriptor::default());
    let msaa_color_texture = if record.sample_count > 1 {
        Some(device_state.device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Window MSAA color"),
            size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
            mip_level_count: 1, sample_count: record.sample_count,
            dimension: wgpu::TextureDimension::D2, format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT, view_formats: &[],
        }))
    } else { None };
    let msaa_color_view = msaa_color_texture.as_ref().map(|texture|
        texture.create_view(&wgpu::TextureViewDescriptor::default()));

    record.presentation = presentation_format.map(|format|
        surface_present::Presentation::new(&device_state.device, width, height, format));
    record.config = Some(config);
    record.color_format = chosen_format;
    record.depth_texture = Some(depth_texture);
    record.depth_view = Some(depth_view);
    record.msaa_color_texture = msaa_color_texture;
    record.msaa_color_view = msaa_color_view;
    record.suspended = false;
    record.needs_reconfigure = false;
    record.configured_device_generation = device_generation;
    record.surface_generation += 1;

    if surface_wsi_tracing_enabled() {
        eprintln!("COIN_WGPU_WSI stage=coin_configure event=end source=actual native_type={} handle_a={:#x} handle_b={:#x} generation={} device_generation={} size={}x{} present_mode={:?} maximum_frame_latency={}",
            record.native_desc.r#type, record.native_desc.handle_a, record.native_desc.handle_b,
            record.surface_generation, device_generation, width, height, chosen_present,
            record.config.as_ref().unwrap().desired_maximum_frame_latency);
    }

    Ok(())
}
