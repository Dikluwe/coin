//! Native surface lifecycle, submission and presentation.

use super::*;

#[no_mangle]
pub extern "C" fn coin_wgpu_surface_create(
    info: *const CoinWgpuSurfaceCreateInfo,
    out_surface: *mut CoinWgpuSurfaceId,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    let res = std::panic::catch_unwind(|| {
        if info.is_null() || out_surface.is_null() {
            set_error(error_buf, error_buf_len, "Null info or out_surface pointer");
            return CoinWgpuStatus::InvalidArgument;
        }

        let inf = unsafe { &*info };
        if inf.abi_version != COIN_WGPU_ABI_VERSION {
            set_error(
                error_buf,
                error_buf_len,
                &format!(
                    "ABI mismatch: expected {}, got {}",
                    COIN_WGPU_ABI_VERSION, inf.abi_version
                ),
            );
            return CoinWgpuStatus::InvalidArgument;
        }
        if inf.struct_size as usize != std::mem::size_of::<CoinWgpuSurfaceCreateInfo>() {
            set_error(
                error_buf,
                error_buf_len,
                "Struct size mismatch for CoinWgpuSurfaceCreateInfo",
            );
            return CoinWgpuStatus::InvalidArgument;
        }
        if inf.native.abi_version != COIN_WGPU_ABI_VERSION {
            set_error(
                error_buf,
                error_buf_len,
                &format!(
                    "ABI mismatch for native descriptor: expected {}, got {}",
                    COIN_WGPU_ABI_VERSION, inf.native.abi_version
                ),
            );
            return CoinWgpuStatus::InvalidArgument;
        }
        if inf.native.struct_size as usize != std::mem::size_of::<CoinWgpuNativeSurfaceDescriptor>()
        {
            set_error(
                error_buf,
                error_buf_len,
                "Struct size mismatch for CoinWgpuNativeSurfaceDescriptor",
            );
            return CoinWgpuStatus::InvalidArgument;
        }
        if renderer_backend(inf.renderer).is_err() {
            set_error(error_buf, error_buf_len, "Invalid requested surface renderer");
            return CoinWgpuStatus::InvalidArgument;
        }
        let sample_count = if inf.sample_count == 0 { 1 } else { inf.sample_count };
        if !matches!(sample_count, 1 | 2 | 4) {
            set_error(error_buf, error_buf_len, "Window MSAA sample count must be 1, 2 or 4");
            return CoinWgpuStatus::InvalidArgument;
        }
        if inf.native.reserved != 0 {
            set_error(
                error_buf,
                error_buf_len,
                "Reserved field in native descriptor must be 0",
            );
            return CoinWgpuStatus::InvalidArgument;
        }

        if let Err(e) = init_runtime_if_needed() {
            set_error(error_buf, error_buf_len, &e);
            return CoinWgpuStatus::BackendError;
        }

        let mut guard = match RUNTIME_CTX.lock() {
            Ok(g) => g,
            Err(e) => {
                set_error(error_buf, error_buf_len, &e.to_string());
                return CoinWgpuStatus::BackendError;
            }
        };
        let runtime = guard.as_mut().unwrap();

        #[cfg(target_os = "android")]
        if runtime.device_state.is_none() && runtime.surfaces.is_empty()
            && runtime.extra_devices.is_empty() && runtime.pending_readbacks.is_empty()
            && runtime.retired_readbacks.is_empty()
        {
            if let Ok(Some(backends)) = renderer_backend(inf.renderer) {
                // ANativeWindow admits one native graphics connection. Creating
                // EGL and Vulkan surfaces for it at once can yield EGL_BAD_ALLOC.
                // Narrow only an unused runtime; never replace a live device.
                let mut descriptor = wgpu::InstanceDescriptor::from_env_or_default();
                descriptor.backends = backends;
                runtime.instance = wgpu::Instance::new(&descriptor);
            }
        }

        // Create surface from native handles
        let surface =
            match unsafe { create_surface_from_descriptor(&runtime.instance, &inf.native) } {
                Ok(s) => s,
                Err(e) => {
                    set_error(error_buf, error_buf_len, &e);
                    return CoinWgpuStatus::Unsupported;
                }
            };

        // Select the requested API with this surface before fixing the shared device.
        let dev_generation = runtime.device_generation;
        let device_state = match get_or_init_device_impl(runtime, Some(&surface), false, inf.renderer) {
            Ok(d) => d,
            Err(e) => {
                set_error(error_buf, error_buf_len, &e);
                return if inf.renderer == 0 { CoinWgpuStatus::NotReady }
                       else { CoinWgpuStatus::Unsupported };
            }
        };

        // Check if surface is compatible with current adapter
        let caps = surface.get_capabilities(&device_state.adapter);
        if caps.formats.is_empty() || caps.present_modes.is_empty() {
            set_error(
                error_buf,
                error_buf_len,
                &format!(
                    "Surface is not compatible with adapter {}",
                    device_state.adapter_name
                ),
            );
            return CoinWgpuStatus::Unsupported;
        }

        let mut record = SurfaceRecord {
            published_serial: 0,
            surface,
            native_desc: inf.native,
            config: None,
            framebuffer_size: (inf.width, inf.height),
            color_format: wgpu::TextureFormat::Bgra8Unorm,
            sample_count,
            suspended: (inf.width == 0 || inf.height == 0),
            needs_reconfigure: true,
            surface_generation: 0,
            configured_device_generation: 0,
            depth_texture: None,
            depth_view: None,
            msaa_color_texture: None,
            msaa_color_view: None,
            presentation: None,
        };

        if inf.width > 0 && inf.height > 0 {
            if let Err(e) = configure_surface_record(
                &mut record,
                device_state,
                dev_generation,
                inf.width,
                inf.height,
            ) {
                set_error(error_buf, error_buf_len, &e);
                return if e.contains("MSAA") { CoinWgpuStatus::Unsupported }
                       else { CoinWgpuStatus::BackendError };
            }
        }

        let surface_id = NEXT_SURFACE_ID.fetch_add(1, Ordering::SeqCst);
        runtime.surfaces.insert(surface_id, record);
        unsafe {
            *out_surface = surface_id;
        }

        CoinWgpuStatus::Ok
    });

    res.unwrap_or_else(|_| {
        set_error(
            error_buf,
            error_buf_len,
            "Rust bridge panic caught in coin_wgpu_surface_create",
        );
        CoinWgpuStatus::BackendError
    })
}

#[no_mangle]
pub extern "C" fn coin_wgpu_surface_resize(
    surface_id: CoinWgpuSurfaceId,
    width: u32,
    height: u32,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    let res = std::panic::catch_unwind(|| {
        if surface_id == COIN_WGPU_INVALID_SURFACE_ID {
            set_error(error_buf, error_buf_len, "Invalid surface ID");
            return CoinWgpuStatus::InvalidArgument;
        }

        let mut guard = match RUNTIME_CTX.lock() {
            Ok(g) => g,
            Err(e) => {
                set_error(error_buf, error_buf_len, &e.to_string());
                return CoinWgpuStatus::BackendError;
            }
        };
        let runtime = match guard.as_mut() {
            Some(r) => r,
            None => {
                set_error(error_buf, error_buf_len, "Runtime context uninitialized");
                return CoinWgpuStatus::NotReady;
            }
        };

        let record = match runtime.surfaces.get_mut(&surface_id) {
            Some(r) => r,
            None => {
                set_error(error_buf, error_buf_len, "Surface ID not found");
                return CoinWgpuStatus::InvalidArgument;
            }
        };

        record.published_serial = 0;
        if width == 0 || height == 0 {
            record.framebuffer_size = (width, height);
            record.suspended = true;
            record.needs_reconfigure = true;
            record.depth_texture = None;
            record.depth_view = None;
            record.msaa_color_texture = None;
            record.msaa_color_view = None;
            record.presentation = None;
            return CoinWgpuStatus::Ok;
        }

        record.framebuffer_size = (width, height);
        record.suspended = false;
        record.needs_reconfigure = true;

        // If device is already initialized, we can reconfigure immediately
        if let Some(dev) = &runtime.device_state {
            let dev_gen = runtime.device_generation;
            if let Err(e) = configure_surface_record(record, dev, dev_gen, width, height) {
                set_error(error_buf, error_buf_len, &e);
                return CoinWgpuStatus::BackendError;
            }
        }

        CoinWgpuStatus::Ok
    });

    res.unwrap_or_else(|_| {
        set_error(
            error_buf,
            error_buf_len,
            "Rust bridge panic caught in coin_wgpu_surface_resize",
        );
        CoinWgpuStatus::BackendError
    })
}

#[no_mangle]
pub extern "C" fn coin_wgpu_surface_destroy(
    surface_id: CoinWgpuSurfaceId,
    _error_buf: *mut std::os::raw::c_char,
    _error_buf_len: usize,
) -> CoinWgpuStatus {
    let res = std::panic::catch_unwind(|| {
        if surface_id == COIN_WGPU_INVALID_SURFACE_ID {
            return CoinWgpuStatus::Ok; // Idempotent
        }

        if let Ok(mut guard) = RUNTIME_CTX.lock() {
            if let Some(runtime) = guard.as_mut() {
                runtime.surfaces.remove(&surface_id);
            }
        }
        CoinWgpuStatus::Ok
    });

    res.unwrap_or(CoinWgpuStatus::Ok)
}

pub(super) fn coin_wgpu_surface_submit_internal(
    surface_id: CoinWgpuSurfaceId,
    frame: *const CoinWgpuFrameView,
    readback_rgba: *mut u8,
    readback_len: usize,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    let res = std::panic::catch_unwind(|| {
        let trace_phases = std::env::var_os("COIN_RENDER_TRACE_PHASES")
            .or_else(|| std::env::var_os("COIN_WGPU_TRACE_PHASES")).is_some();
        let profile_start = trace_phases.then(std::time::Instant::now);
        // 1. Fault injection check
        let fault = fault_state::current();
        if fault == CoinWgpuStatus::NotReady as i32 {
            set_error(error_buf, error_buf_len, "Injected NOT_READY fault");
            return CoinWgpuStatus::NotReady;
        } else if fault == CoinWgpuStatus::OutOfMemory as i32
            || fault == FAULT_SURFACE_OUT_OF_MEMORY
        {
            set_error(error_buf, error_buf_len, "Injected OUT_OF_MEMORY fault");
            return CoinWgpuStatus::OutOfMemory;
        } else if fault == CoinWgpuStatus::DeviceLost as i32 {
            if let Ok(mut guard) = RUNTIME_CTX.lock() {
                if let Some(rt) = guard.as_mut() {
                    rt.device_state = None;
                    rt.device_generation += 1;
                }
            }
            DEVICE_LOST_OCCURRED.store(false, Ordering::SeqCst);
            set_error(error_buf, error_buf_len, "Injected DEVICE_LOST fault");
            return CoinWgpuStatus::DeviceLost;
        } else if fault == FAULT_SURFACE_TIMEOUT {
            set_error(error_buf, error_buf_len, "Injected SURFACE_TIMEOUT fault");
            return CoinWgpuStatus::NotReady;
        } else if fault == FAULT_SURFACE_OTHER {
            set_error(error_buf, error_buf_len, "Injected SURFACE_OTHER fault");
            return CoinWgpuStatus::BackendError;
        }

        // 2. Real device lost check
        if DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst) {
            if let Ok(mut guard) = RUNTIME_CTX.lock() {
                if let Some(rt) = guard.as_mut() {
                    rt.device_state = None;
                    rt.device_generation += 1;
                }
            }
            let msg = LAST_ASYNC_ERROR_MSG
                .lock()
                .map(|m| m.clone())
                .unwrap_or_else(|_| "Device lost".to_string());
            set_error(error_buf, error_buf_len, &msg);
            return CoinWgpuStatus::DeviceLost;
        }

        if surface_id == COIN_WGPU_INVALID_SURFACE_ID || frame.is_null() {
            set_error(
                error_buf,
                error_buf_len,
                "Invalid surface ID or null frame pointer",
            );
            return CoinWgpuStatus::InvalidArgument;
        }
        if (frame as usize) % std::mem::align_of::<CoinWgpuFrameView>() != 0 {
            set_error(error_buf, error_buf_len, "Misaligned frame pointer");
            return CoinWgpuStatus::InvalidArgument;
        }

        let f = unsafe { &*frame };
        if f.texture_output_flags!=0 || f.texture_sampling_policy>1 {
            set_error(error_buf,error_buf_len,"Surface submits do not accept RTT output flags");
            return CoinWgpuStatus::InvalidArgument;
        }


        // 3. ABI version and struct size validation
        if f.abi_version != COIN_WGPU_ABI_VERSION {
            set_error(
                error_buf,
                error_buf_len,
                &format!(
                    "ABI version mismatch: expected {}, got {}",
                    COIN_WGPU_ABI_VERSION, f.abi_version
                ),
            );
            return CoinWgpuStatus::InvalidArgument;
        }
        if f.struct_size as usize != std::mem::size_of::<CoinWgpuFrameView>() {
            set_error(
                error_buf,
                error_buf_len,
                &format!(
                    "Struct size mismatch: expected {}, got {}",
                    std::mem::size_of::<CoinWgpuFrameView>(),
                    f.struct_size
                ),
            );
            return CoinWgpuStatus::InvalidArgument;
        }

        if let Err(status)=validate_multipass_metadata(f,error_buf,error_buf_len) {return status;}
        let portable_modules = match portable::modules(f) {
            Ok(modules) => modules,
            Err((status,message)) => { set_error(error_buf,error_buf_len,&message); return status; }
        };
        if !portable_modules.is_empty() && (f.instance_count != 0 || f.instance_range_count != 0) {
            set_error(error_buf,error_buf_len,"Portable shader modules require ordinary non-instanced vertex delivery");
            return CoinWgpuStatus::Unsupported;
        }
        let bump_payload = match validate_bump_payload(f, error_buf, error_buf_len) {
            Ok(payload) => payload,
            Err(status) => return status,
        };

        // 4. Dimensions validation: zero dimensions = suspended -> NOT_READY
        if f.width == 0 || f.height == 0 {
            return CoinWgpuStatus::NotReady;
        }

        if let Err((status, message)) = instancing::validate_counts(f) {
            set_error(error_buf, error_buf_len, &message); return status;
        }
        let states_slice =
            match validate_slice(f.states, f.state_count, "states", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            };
        if !states_slice.iter().all(valid_alpha_test) {
            set_error(error_buf, error_buf_len, "Invalid alpha comparison function or reference");
            return CoinWgpuStatus::InvalidArgument;
        }
        let owned_patch = RUNTIME_CTX.lock().ok().and_then(|guard| {
            let runtime = guard.as_ref()?;
            if !runtime.surfaces.contains_key(&surface_id) { return None; }
            camera_base(runtime.device_state.as_ref()?, f, states_slice)
        });

        // 5. Preflight slice bounds and alignment validation BEFORE acquire
        let vertices_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.vertices.as_slice()
        } else { match validate_slice(
            f.vertices,
            f.vertex_count,
            "vertices",
            error_buf,
            error_buf_len,
        ) {
            Ok(s) => s,
            Err(st) => return st,
        } };
        let indices_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.indices.as_slice()
        } else { match validate_slice(
            f.indices,
            f.index_count,
            "indices",
            error_buf,
            error_buf_len,
        ) {
            Ok(s) => s,
            Err(st) => return st,
        } };
        let draws_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.draws.as_slice()
        } else { match validate_slice(f.draws, f.draw_count, "draws", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            } };
        if let Err(status)=validate_multipass_draws(f,draws_slice,error_buf,error_buf_len) {return status;}
        let materials_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.materials.as_slice()
        } else { match validate_slice(
            f.materials,
            f.material_count,
            "materials",
            error_buf,
            error_buf_len,
        ) {
            Ok(s) => s,
            Err(st) => return st,
        } };

        let instances_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.instances.as_slice()
        } else { match validate_slice(f.instances, f.instance_count, "instances", error_buf, error_buf_len) {
            Ok(s) => s, Err(st) => return st,
        } };
        let instance_ranges = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.instance_ranges.as_slice()
        } else { match validate_slice(f.instance_ranges, f.instance_range_count, "instance ranges", error_buf, error_buf_len) {
            Ok(s) => s, Err(st) => return st,
        } };
        if owned_patch.is_none() {
            if let Err((status, message)) = instancing::validate_payload(f, vertices_slice, indices_slice,
                draws_slice, materials_slice, states_slice, instances_slice, instance_ranges) {
                set_error(error_buf, error_buf_len, &message); return status;
            }
        }

        let textures_slice = if f.texture_count > 0 {
            match validate_slice(f.textures, f.texture_count, "textures", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            }
        } else {
            &[]
        };

        if let Err(message) = validate_texture_declarations(textures_slice) {
            set_error(error_buf, error_buf_len, &message);
            return CoinWgpuStatus::InvalidArgument;
        }

        let samplers_slice = if f.sampler_count > 0 {
            match validate_slice(f.samplers, f.sampler_count, "samplers", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            }
        } else {
            &[]
        };

        if let Err((status,message)) = portable::validate_bump_occurrences(states_slice,draws_slice,indices_slice,
            bump_payload.as_ref().map_or(&[],|payload|payload.vertices.as_slice())) {
            set_error(error_buf,error_buf_len,&message);return status;
        }
        if let Err((status,message)) = portable::validate_draws(&portable_modules,vertices_slice,indices_slice,
            draws_slice,materials_slice,states_slice,textures_slice) {
            set_error(error_buf,error_buf_len,&message);return status;
        }
        if let Err((status,message)) = validate_cube_resources(states_slice,textures_slice,samplers_slice)
            .and_then(|()|validate_cube_draws(states_slice,textures_slice,vertices_slice,indices_slice,draws_slice))
            .and_then(|()|raster_image::validate_with_images(states_slice,textures_slice,samplers_slice.len())) {
            set_error(error_buf,error_buf_len,&message);return status;
        }
        let unchecked_draws = if owned_patch.is_some() { &[] } else { draws_slice };
        for (i, draw) in unchecked_draws.iter().enumerate() {
            let idx_end = match draw.first_index.checked_add(draw.index_count) {
                Some(end) => end as usize,
                None => {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Draw {}: first_index + index_count overflow", i),
                    );
                    return CoinWgpuStatus::InvalidArgument;
                }
            };
            if idx_end > indices_slice.len() {
                set_error(
                    error_buf,
                    error_buf_len,
                    &format!("Draw {}: index range exceeds indices length", i),
                );
                return CoinWgpuStatus::InvalidArgument;
            }
            let vtx_end = match draw.first_vertex.checked_add(draw.vertex_count) {
                Some(end) => end as usize,
                None => {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Draw {}: first_vertex + vertex_count overflow", i),
                    );
                    return CoinWgpuStatus::InvalidArgument;
                }
            };
            if vtx_end > vertices_slice.len() {
                set_error(
                    error_buf,
                    error_buf_len,
                    &format!("Draw {}: vertex range exceeds vertices length", i),
                );
                return CoinWgpuStatus::InvalidArgument;
            }
            for idx in (draw.first_index as usize)..idx_end {
                let v_idx = indices_slice[idx] as usize;
                if v_idx >= vertices_slice.len() {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Draw {}: index {} references vertex out of bounds", i, idx),
                    );
                    return CoinWgpuStatus::InvalidArgument;
                }
            }
            if (draw.render_state_slot as usize) >= states_slice.len() {
                set_error(
                    error_buf,
                    error_buf_len,
                    &format!("Draw {}: render_state_slot out of bounds", i),
                );
                return CoinWgpuStatus::InvalidArgument;
            }
            let st = &states_slice[draw.render_state_slot as usize];
            if (st.material_slot as usize) >= materials_slice.len() {
                set_error(
                    error_buf,
                    error_buf_len,
                    &format!("Draw {}: material_slot out of bounds", i),
                );
                return CoinWgpuStatus::InvalidArgument;
            }
            if !valid_texture_payload(st) {
                set_error(error_buf, error_buf_len, "Invalid normalized texture program or extra unit state");
                return CoinWgpuStatus::InvalidArgument;
            }
            if let Err(message) = validate_texture_coordinates_with_images(st, draw.topology, vertices_slice,
                &indices_slice[draw.first_index as usize..idx_end], textures_slice) {
                set_error(error_buf, error_buf_len, &format!("Draw {i}: {message}"));
                return CoinWgpuStatus::InvalidArgument;
            }
            for layer in &st.extra_textures {
                if layer.enabled != 0 && ((layer.texture_slot as usize) >= textures_slice.len() ||
                    (layer.sampler_slot as usize) >= samplers_slice.len()) {
                    set_error(error_buf, error_buf_len, "Extra texture or sampler out of bounds");
                    return CoinWgpuStatus::InvalidArgument;
                }
            }
            if st.has_texture != 0 {
                if (st.texture_slot as usize) >= textures_slice.len() {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Draw {}: texture_slot out of bounds", i),
                    );
                    return CoinWgpuStatus::InvalidArgument;
                }
                if (st.sampler_slot as usize) >= samplers_slice.len() {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Draw {}: sampler_slot out of bounds", i),
                    );
                    return CoinWgpuStatus::InvalidArgument;
                }
            }
            if st.cull_mode > 2 {
                set_error(
                    error_buf,
                    error_buf_len,
                    &format!("Draw {}: invalid cull_mode", i),
                );
                return CoinWgpuStatus::InvalidArgument;
            }
            if st.front_face > 1 {
                set_error(
                    error_buf,
                    error_buf_len,
                    &format!("Draw {}: invalid front_face", i),
                );
                return CoinWgpuStatus::InvalidArgument;
            }
        }

        // Reject unsupported composition before acquiring a swapchain texture.
        let draw_order = if let Some(scene) = owned_patch.as_ref() {
            scene.draw_order.clone()
        } else { match composition::order(
            vertices_slice, indices_slice, draws_slice, materials_slice, states_slice, textures_slice) {
            Ok(order) => order,
            Err((status, message)) => {
                set_error(error_buf, error_buf_len, &message);
                return status;
            }
        } };

        if let Err((status, message)) = peeling::validate_request(
            if draw_order.iter().any(|item| item.peel) { f.sorted_layers_passes } else { 1 },
            f.transparency_reserved,
            f.transparency_budget_bytes,
            f.width,
            f.height,
            draw_order.iter().any(|item| item.peel || item.weighted),
        ) {
            set_error(error_buf, error_buf_len, &message);
            return status;
        }

        let shadow_frame = match validate_shadow_frame(f, states_slice.len(),
            owned_patch.is_some(), error_buf, error_buf_len) {
            Ok(shadow) => shadow,
            Err(status) => return status,
        };

        if let Some(payload) = bump_payload.as_ref() {
            if let Err((status, message)) = validate_bump_lighting_occurrences(payload,
                vertices_slice, indices_slice, draws_slice, states_slice, materials_slice,
                textures_slice, shadow_frame.as_ref()) {
                set_error(error_buf, error_buf_len, &message);
                return status;
            }
        }

        // 6. Lock RuntimeContext and resolve surface
        let mut guard = match RUNTIME_CTX.lock() {
            Ok(g) => g,
            Err(e) => {
                set_error(error_buf, error_buf_len, &e.to_string());
                return CoinWgpuStatus::BackendError;
            }
        };
        let runtime = match guard.as_mut() {
            Some(r) => r,
            None => {
                set_error(error_buf, error_buf_len, "Runtime context uninitialized");
                return CoinWgpuStatus::NotReady;
            }
        };

        if !runtime.surfaces.contains_key(&surface_id) {
            set_error(error_buf, error_buf_len, "Surface ID not found in registry");
            return CoinWgpuStatus::InvalidArgument;
        }

        if let Err((status,message))=admit_multipass_resources(f,
            runtime.surfaces.get(&surface_id).unwrap().sample_count,states_slice,textures_slice,
            &draw_order,shadow_frame.as_ref(),true,!readback_rgba.is_null(),false,
            multipass_pending_bytes(Some(runtime))) {
            set_error(error_buf,error_buf_len,&message);return status;
        }

        // Ensure device is ready
        let target_surface_ptr: *const wgpu::Surface = runtime
            .surfaces
            .get(&surface_id)
            .map(|s| &s.surface as *const wgpu::Surface)
            .unwrap_or(std::ptr::null());
        let target_surface_ref = unsafe { target_surface_ptr.as_ref() };
        let dev_generation = runtime.device_generation;
        let device_state = match get_or_init_device(runtime, target_surface_ref) {
            Ok(d) => d as *mut DeviceState,
            Err(e) => {
                set_error(error_buf, error_buf_len, &e);
                return CoinWgpuStatus::NotReady;
            }
        };
        let dev = unsafe { &mut *device_state };
        if bump_payload.is_some() && (f.rasterization_policy != 0 ||
            !instances_slice.is_empty() || !instance_ranges.is_empty()) {
            set_error(error_buf, error_buf_len, "Bump requires native non-instanced geometry");
            return CoinWgpuStatus::Unsupported;
        }

        if let Some(payload) = bump_payload.as_ref() {
            if extended_bump_profile(states_slice, textures_slice, draws_slice, shadow_frame.as_ref(), payload, indices_slice) {
                if let Err((status, message)) = validate_extended_bump_profile(dev,
                    vertices_slice, indices_slice, draws_slice, states_slice,
                    materials_slice, textures_slice, samplers_slice, payload,
                    shadow_frame.as_ref(), [f.width, f.height]) {
                    set_error(error_buf, error_buf_len, &message);
                    return status;
                }
            }
        }


        if let Some(scene) = owned_patch.as_ref() {
            if scene.generation != dev.generation {
                set_error(error_buf, error_buf_len, "Camera base belongs to a lost device generation");
                return CoinWgpuStatus::DeviceLost;
            }
            if !dev.validated_scene.as_ref().is_some_and(|current| Arc::ptr_eq(current, scene)) {
                set_error(error_buf, error_buf_len, "Camera base is no longer current on this device");
                return CoinWgpuStatus::NotReady;
            }
        }
        let capture_scene = !instances_slice.is_empty() || dev.camera_snapshot_policy.capture(
            dev.validated_scene.as_ref().map(|scene| scene.revision), f.frame_revision,
            f.camera_base_revision, owned_patch.is_some());
        let next_scene = if capture_scene && shadow_frame.is_none() {
            camera_scene(dev.generation, f, vertices_slice, indices_slice, draws_slice,
                materials_slice, states_slice, &draw_order, textures_slice, samplers_slice,
                instances_slice, instance_ranges, owned_patch.as_ref())
        } else { None };

        if let Err((status,message))=admit_spatial_resources(dev,textures_slice)
            .and_then(|()|raster_image::admit(dev,states_slice,textures_slice,samplers_slice.len())) {
            set_error(error_buf,error_buf_len,&message); return status;
        }
        let record = runtime.surfaces.get_mut(&surface_id).unwrap();
        if let Err((status, message)) = validate_shadow_multitexture_profile(dev, textures_slice, states_slice, draws_slice,
            &draw_order, shadow_frame.as_ref(), record.sample_count, f.rasterization_policy) {
            set_error(error_buf, error_buf_len, &message);
            return status;
        }
        if draw_order.iter().any(|item| item.peel) {
            if let Err((status, message)) = peeling::validate_request_samples(f.sorted_layers_passes,
                f.transparency_reserved, f.transparency_budget_bytes, f.width, f.height, true, record.sample_count) {
                set_error(error_buf, error_buf_len, &message); return status;
            }
            if !peeling_supported(dev, record.sample_count) {
                set_error(error_buf, error_buf_len, "Peeling formats cannot sample/render at the requested count");
                return CoinWgpuStatus::Unsupported;
            }
        }
        if draw_order.iter().any(|item| item.weighted) {
            if let Err((status, message)) = weighted::validate_budget(f.width, f.height, record.sample_count, f.transparency_budget_bytes) {
                set_error(error_buf, error_buf_len, &message);
                return status;
            }
        }
        if draw_order.iter().any(|item| item.weighted) &&
            !weighted_supported(dev, record.sample_count) {
            set_error(error_buf, error_buf_len, "Weighted MSAA color formats cannot blend/sample at the requested count");
            return CoinWgpuStatus::Unsupported;
        }
        if record.sample_count > 1 &&
            draw_order.iter().any(|item| bump_payload.is_some() && item.blend) {
            set_error(error_buf, error_buf_len,
                "Window MSAA translucent bump needs a separate profile");
            return CoinWgpuStatus::Unsupported;
        }

        // 7. Reconfigure if device generation changed or size changed
        if record.suspended {
            return CoinWgpuStatus::NotReady;
        }

        if record.configured_device_generation != dev_generation {
            match unsafe { create_surface_from_descriptor(&runtime.instance, &record.native_desc) }
            {
                Ok(new_surf) => {
                    record.surface = new_surf;
                }
                Err(e) => {
                    set_error(error_buf, error_buf_len, &e);
                    return CoinWgpuStatus::SurfaceLost;
                }
            }
            if let Err(e) = configure_surface_record(record, dev, dev_generation, f.width, f.height)
            {
                set_error(error_buf, error_buf_len, &e);
                return CoinWgpuStatus::BackendError;
            }
        } else if record.needs_reconfigure || record.framebuffer_size != (f.width, f.height) {
            if let Err(e) = configure_surface_record(record, dev, dev_generation, f.width, f.height)
            {
                set_error(error_buf, error_buf_len, &e);
                return CoinWgpuStatus::BackendError;
            }
        }

        if !readback_rgba.is_null() {
            let required = (f.width as usize).checked_mul(f.height as usize)
                .and_then(|pixels| pixels.checked_mul(4));
            if required != Some(readback_len) {
                set_error(error_buf, error_buf_len, "Window RGBA output size mismatch");
                return CoinWgpuStatus::InvalidArgument;
            }
            if !matches!(record.color_format, wgpu::TextureFormat::Bgra8Unorm |
                wgpu::TextureFormat::Rgba8Unorm |
                wgpu::TextureFormat::Bgra8UnormSrgb |
                wgpu::TextureFormat::Rgba8UnormSrgb) {
                set_error(error_buf, error_buf_len, "Window surface format is not RGBA8/BGRA8");
                return CoinWgpuStatus::Unsupported;
            }
        }

        let profile_validated = trace_phases.then(std::time::Instant::now);
        if let Err((status,message)) = portable::prepare(dev,&portable_modules,states_slice,draws_slice,
            &draw_order,record.sample_count,f.texture_sampling_policy,f.rasterization_policy,
            shadow_frame.as_ref().map_or(0,|shadow|if shadow.extra.is_empty(){1}else{2}),
            if record.presentation.is_some() {wgpu::TextureFormat::Rgba8Unorm} else {record.color_format}) {
            set_error(error_buf,error_buf_len,&message); return status;
        }

        // 8. Acquire texture with strictly 1 recovery attempt
        let injected_acquire = if fault == FAULT_SURFACE_TIMEOUT_AT_ACQUIRE {
            Some(wgpu::SurfaceError::Timeout)
        } else if fault == FAULT_SURFACE_OUTDATED_ONCE
            && fault_state::first_outdated()
        {
            Some(wgpu::SurfaceError::Outdated)
        } else if fault == FAULT_SURFACE_LOST_ONCE
            && fault_state::first_lost()
        {
            Some(wgpu::SurfaceError::Lost)
        } else if fault == FAULT_SURFACE_LOST_PERSISTENT {
            Some(wgpu::SurfaceError::Lost)
        } else {
            None
        };
        let trace_wsi = trace_phases || surface_wsi_tracing_enabled();
        let mut acquire_result = acquire_surface_texture(record, surface_id, 1,
            injected_acquire, trace_wsi);

        if let Err(err) = &acquire_result {
            match err {
                wgpu::SurfaceError::Timeout => {
                    set_error(error_buf, error_buf_len, "Surface acquire timeout");
                    return CoinWgpuStatus::NotReady;
                }
                wgpu::SurfaceError::OutOfMemory => {
                    set_error(error_buf, error_buf_len, "Surface acquire out of memory");
                    return CoinWgpuStatus::OutOfMemory;
                }
                wgpu::SurfaceError::Other => {
                    set_error(
                        error_buf,
                        error_buf_len,
                        "Surface acquire failed with internal error",
                    );
                    return CoinWgpuStatus::BackendError;
                }
                wgpu::SurfaceError::Outdated => {
                    // Reconfigure 1x and reacquire
                    if let Err(e) =
                        configure_surface_record(record, dev, dev_generation, f.width, f.height)
                    {
                        set_error(error_buf, error_buf_len, &e);
                        return CoinWgpuStatus::SurfaceLost;
                    }
                    acquire_result = acquire_surface_texture(record, surface_id, 2,
                        (fault == FAULT_SURFACE_LOST_PERSISTENT).then_some(wgpu::SurfaceError::Lost),
                        trace_wsi);
                }
                wgpu::SurfaceError::Lost => {
                    // Recreate surface 1x and reacquire
                    match unsafe {
                        create_surface_from_descriptor(&runtime.instance, &record.native_desc)
                    } {
                        Ok(new_surf) => {
                            record.surface = new_surf;
                            if let Err(e) = configure_surface_record(
                                record,
                                dev,
                                dev_generation,
                                f.width,
                                f.height,
                            ) {
                                set_error(error_buf, error_buf_len, &e);
                                return CoinWgpuStatus::SurfaceLost;
                            }
                            acquire_result = acquire_surface_texture(record, surface_id, 2,
                                (fault == FAULT_SURFACE_LOST_PERSISTENT).then_some(wgpu::SurfaceError::Lost),
                                trace_wsi);
                        }
                        Err(e) => {
                            set_error(error_buf, error_buf_len, &e);
                            return CoinWgpuStatus::SurfaceLost;
                        }
                    }
                }
            }
        }

        // Evaluate second acquire result with strict precedence
        let surface_texture = match acquire_result {
            Ok(st) => st,
            Err(wgpu::SurfaceError::OutOfMemory) => {
                set_error(
                    error_buf,
                    error_buf_len,
                    "Surface acquire out of memory on retry",
                );
                return CoinWgpuStatus::OutOfMemory;
            }
            Err(wgpu::SurfaceError::Timeout) => {
                set_error(error_buf, error_buf_len, "Surface acquire timeout on retry");
                return CoinWgpuStatus::NotReady;
            }
            Err(wgpu::SurfaceError::Other) => {
                set_error(
                    error_buf,
                    error_buf_len,
                    "Surface acquire other error on retry",
                );
                return CoinWgpuStatus::BackendError;
            }
            Err(wgpu::SurfaceError::Outdated) | Err(wgpu::SurfaceError::Lost) => {
                set_error(
                    error_buf,
                    error_buf_len,
                    "Surface acquire failed persistently (surface lost)",
                );
                return CoinWgpuStatus::SurfaceLost;
            }
        };

        // Dedicated synchronous diagnostic. Ordinary window timings allocate
        // no queries/readback and never wait for this sampling audit.
        let audit_probe = (trace_phases && std::env::var_os("COIN_SAMPLING_AUDIT").is_some()
            && std::env::var("COIN_WGPU_GPU_TIMESTAMPS").as_deref()==Ok("1")
            && dev.device.features().contains(wgpu::Features::TIMESTAMP_QUERY | wgpu::Features::TIMESTAMP_QUERY_INSIDE_ENCODERS))
            .then(|| GpuTimestampProbe::new(&dev.device));
        let profile_acquired = trace_phases.then(std::time::Instant::now);
        // 9. Encode frame into acquired texture view
        let color_view = surface_texture
            .texture
            .create_view(&wgpu::TextureViewDescriptor::default());
        // Capture the same image that is presented even when WSI lacks COPY_SRC.
        // MSAA already has a private resolved image; ordinary 1x creates one
        // only for this requested readback, preserving the surface scene format.
        let capture_presentation = if record.presentation.is_none() && (f.multipass_count > 1 ||
            (!readback_rgba.is_null() && !record.config.as_ref().unwrap().usage.contains(wgpu::TextureUsages::COPY_SRC))) {
            Some(surface_present::Presentation::with_source_format(&dev.device,
                f.width, f.height, record.color_format, record.color_format))
        } else { None };
        let presentation = record.presentation.as_ref().or(capture_presentation.as_ref());
        let resolved_view = presentation.map_or(&color_view, |p| &p.view);
        let capture_texture = presentation.map_or(&surface_texture.texture, |p| &p.texture);
        let capture_format = if record.presentation.is_some() {
            wgpu::TextureFormat::Rgba8Unorm
        } else { record.color_format };
        let scene_format = if record.presentation.is_some() {
            wgpu::TextureFormat::Rgba8Unorm
        } else { record.color_format };
        let depth_view = match &record.depth_view {
            Some(v) => v,
            None => {
                set_error(error_buf, error_buf_len, "Surface depth view not allocated");
                return CoinWgpuStatus::BackendError;
            }
        };

        if f.multipass_count > 1 && record.sample_count > 1 && fault == 307 {
            set_error(error_buf,error_buf_len,"Injected MSAA failure before multipass queue submission");
            drop(color_view); drop(surface_texture);
            record.surface.configure(&dev.device,record.config.as_ref().unwrap());
            return CoinWgpuStatus::OutOfMemory;
        }
        let encoded = encode_scene_samples(
            dev, f, [f.width,f.height], vertices_slice, indices_slice, draws_slice, &draw_order,
            materials_slice, states_slice, &portable_modules, textures_slice, samplers_slice,
            bump_payload.as_ref(), shadow_frame.as_ref(), next_scene.as_ref().map(|s| &s.geometry),
            instances_slice, instance_ranges, audit_probe.as_ref().map(|p| &p.queries),
            record.msaa_color_view.as_ref().unwrap_or(resolved_view), record.msaa_color_view.as_ref().map(|_| resolved_view), scene_format, depth_view, record.depth_texture.as_ref().unwrap(), record.sample_count);
        let (cmd_buffers, pending_instances) = match encoded {
            Ok(cmd) => cmd,
            Err((status, msg)) => {
                set_error(error_buf, error_buf_len, &msg);
                // Vulkan's discarded surface image is not returned to its swapchain.
                // Release every reference before recreating the same configuration,
                // so any rejected MSAA encoder (including shadow allocation
                // failures) cannot exhaust subsequent acquisitions. Known profile
                // rejections are checked before acquisition above.
                if record.sample_count > 1 {
                    drop(color_view);
                    drop(surface_texture);
                    record.surface.configure(&dev.device, record.config.as_ref().unwrap());
                }
                return status;
            }
        };

        if record.sample_count > 1 && fault == 307 {
            set_error(error_buf, error_buf_len,
                "Injected MSAA failure after encoding, before queue submission");
            drop(cmd_buffers);
            drop(color_view);
            drop(surface_texture);
            record.surface.configure(&dev.device, record.config.as_ref().unwrap());
            return CoinWgpuStatus::OutOfMemory;
        }

        let mut capture_buffer = None;
        let mut commands = cmd_buffers;
        if record.published_serial == 0 && manager_backgrounds(draws_slice) {
            commands.insert(0, initialize_target_depth(&dev.device, depth_view));
        }
        if let Some(presentation) = presentation {
            let target = surface_texture.texture.create_view(&wgpu::TextureViewDescriptor {
                format: Some(presentation.format), ..Default::default()
            });
            commands.push(presentation.encode(&dev.device, &target));
        }
        if !readback_rgba.is_null() {
            let bytes_per_row = (f.width * 4 + 255) & !255;
            let staging = dev.device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("Window RGBA capture"),
                size: u64::from(bytes_per_row) * u64::from(f.height),
                usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
                mapped_at_creation: false,
            });
            let mut copy = dev.device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("Window RGBA capture copy"),
            });
            copy.copy_texture_to_buffer(
                wgpu::TexelCopyTextureInfo {
                    texture: capture_texture,
                    mip_level: 0,
                    origin: wgpu::Origin3d::ZERO,
                    aspect: wgpu::TextureAspect::All,
                },
                wgpu::TexelCopyBufferInfo {
                    buffer: &staging,
                    layout: wgpu::TexelCopyBufferLayout {
                        offset: 0,
                        bytes_per_row: Some(bytes_per_row),
                        rows_per_image: Some(f.height),
                    },
                },
                wgpu::Extent3d { width: f.width, height: f.height, depth_or_array_layers: 1 },
            );
            commands.push(copy.finish());
            capture_buffer = Some((staging, bytes_per_row));
        }
        let profile_encoded = trace_phases.then(std::time::Instant::now);
        // 10. Submit and Present
        let sub_serial = GLOBAL_SUBMISSION_SERIAL.fetch_add(1, Ordering::SeqCst);
        LAST_SUBMITTED_SERIAL.store(sub_serial, Ordering::SeqCst);
        let local_serial = dev.last_submitted_serial.fetch_add(1, Ordering::SeqCst) + 1;
        if let Some(probe)=audit_probe.as_ref() {
            let mut resolve=dev.device.create_command_encoder(&wgpu::CommandEncoderDescriptor{label:Some("Coin sampling audit resolve")});
            resolve.resolve_query_set(&probe.queries,0..2,&probe.resolved,0);
            resolve.copy_buffer_to_buffer(&probe.resolved,0,&probe.readback,0,16);
            commands.push(resolve.finish());
        }
        let command_buffers_count=commands.len();
        if trace_wsi {
            eprintln!("COIN_WGPU_WSI stage=coin_submit event=begin source=actual surface={} generation={} serial={} local_serial={} previous_published_serial={}",
                surface_id, record.surface_generation, sub_serial, local_serial, record.published_serial);
        }
        dev.queue.submit(commands);
        let profile_submitted = trace_phases.then(std::time::Instant::now);
        let completed = dev.completed_serial.clone();
        dev.queue.on_submitted_work_done(move || {
            completed.fetch_max(local_serial, Ordering::SeqCst);
            GLOBAL_COMPLETED_SERIAL.fetch_max(sub_serial, Ordering::SeqCst);
        });
        let profile_notified = trace_phases.then(std::time::Instant::now);
        let mut captured_rgba = None;
        if let Some((staging, bytes_per_row)) = capture_buffer {
            let slice = staging.slice(..);
            let (sender, receiver) = std::sync::mpsc::channel();
            slice.map_async(wgpu::MapMode::Read, move |result| {
                let _ = sender.send(result);
            });
            let _ = dev.device.poll(wgpu::Maintain::Wait);
            if !matches!(receiver.try_recv(), Ok(Ok(()))) {
                set_error(error_buf, error_buf_len, "Window RGBA capture map failed");
                return CoinWgpuStatus::BackendError;
            }
            let mapped = slice.get_mapped_range();
            let mut pixels = vec![0u8; readback_len];
            let bgra = matches!(capture_format, wgpu::TextureFormat::Bgra8Unorm |
                wgpu::TextureFormat::Bgra8UnormSrgb);
            for y in 0..f.height as usize {
                let source = &mapped[y * bytes_per_row as usize..y * bytes_per_row as usize + f.width as usize * 4];
                let destination = &mut pixels[y * f.width as usize * 4..(y + 1) * f.width as usize * 4];
                if bgra {
                    for (dst, src) in destination.chunks_exact_mut(4).zip(source.chunks_exact(4)) {
                        dst.copy_from_slice(&[src[2], src[1], src[0], src[3]]);
                    }
                } else {
                    destination.copy_from_slice(source);
                }
            }
            drop(mapped);
            staging.unmap();
            captured_rgba = Some(pixels);
        }
        if let Some(probe)=audit_probe.as_ref() {
            let (sender,receiver)=std::sync::mpsc::channel();
            probe.readback.slice(..16).map_async(wgpu::MapMode::Read,move|r|{let _=sender.send(r);});
            let _=dev.device.poll(wgpu::Maintain::Wait);
            if matches!(receiver.try_recv(),Ok(Ok(()))) {
                let mapped=probe.readback.slice(..16).get_mapped_range();
                let a=u64::from_ne_bytes(mapped[..8].try_into().unwrap());
                let b=u64::from_ne_bytes(mapped[8..16].try_into().unwrap());
                if let Some(ticks)=b.checked_sub(a) {
                    eprintln!("COIN_SAMPLING_AUDIT_GPU status=ok render_ms={:.6} period_ns={:.6}",ticks as f64*dev.queue.get_timestamp_period() as f64/1_000_000.,dev.queue.get_timestamp_period());
                } else {eprintln!("COIN_SAMPLING_AUDIT_GPU status=invalid_ticks");}
                drop(mapped);probe.readback.unmap();
            } else {eprintln!("COIN_SAMPLING_AUDIT_GPU status=map_failed");}
        }
        let profile_present_begin = trace_phases.then(std::time::Instant::now);
        if trace_wsi {
            eprintln!("COIN_WGPU_WSI stage=coin_present event=begin source=actual surface={} generation={} serial={}",
                surface_id, record.surface_generation, sub_serial);
        }
        surface_texture.present();
        if trace_wsi {
            // This is a return from wgpu, not evidence of scanout/display completion.
            eprintln!("COIN_WGPU_WSI stage=coin_present event=returned source=actual surface={} generation={} serial={} display_completion=unknown",
                surface_id, record.surface_generation, sub_serial);
        }
        let profile_presented = trace_phases.then(std::time::Instant::now);

        // Check if device lost or async errors occurred during submit/present
        if DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst) {
            runtime.device_state = None;
            runtime.device_generation += 1;
            let msg = LAST_ASYNC_ERROR_MSG
                .lock()
                .map(|m| m.clone())
                .unwrap_or_else(|_| "Device lost during present".to_string());
            set_error(error_buf, error_buf_len, &msg);
            return CoinWgpuStatus::DeviceLost;
        }

        let error_kind = LAST_ASYNC_ERROR_KIND.swap(0, Ordering::SeqCst);
        if error_kind == 1 {
            let msg = LAST_ASYNC_ERROR_MSG
                .lock()
                .map(|m| m.clone())
                .unwrap_or_else(|_| "Out of memory".to_string());
            set_error(error_buf, error_buf_len, &msg);
            return CoinWgpuStatus::OutOfMemory;
        } else if error_kind == 2 {
            let msg = LAST_ASYNC_ERROR_MSG
                .lock()
                .map(|m| m.clone())
                .unwrap_or_else(|_| "Backend validation error".to_string());
            set_error(error_buf, error_buf_len, &msg);
            return CoinWgpuStatus::BackendError;
        }

        commit_camera_scene(dev, next_scene, true, f.frame_revision, owned_patch.is_some(), capture_scene,
            !instances_slice.is_empty(), pending_instances);

        if let Some(pixels) = captured_rgba {
            unsafe { std::ptr::copy_nonoverlapping(pixels.as_ptr(), readback_rgba, pixels.len()); }
        }

        if trace_phases {
            let duration_ms = |start: std::time::Instant, end: std::time::Instant| {
                (end - start).as_secs_f64() * 1000.0
            };
            let (start, validated, acquired, encoded, presented) =
                (profile_start.unwrap(), profile_validated.unwrap(),
                 profile_acquired.unwrap(), profile_encoded.unwrap(),
                 profile_presented.unwrap());
            let capture_bytes = if readback_rgba.is_null() { 0 } else {
                u64::from((f.width * 4 + 255) & !255) * u64::from(f.height)
            };
            eprintln!("COIN_RENDER_PHASE rust_surface_cpu total_ms={:.6} validation_ms={:.6} acquire_ms={:.6} encode_ms={:.6} submit_present_ms={:.6} queue_submit_ms={:.6} completion_registration_ms={:.6} capture_readback_ms={:.6} present_ms={:.6} capture_requested={} capture_staging_bytes={} queue_submissions=1 command_buffers={} gpu_timing={}",
                duration_ms(start, presented), duration_ms(start, validated),
                duration_ms(validated, acquired), duration_ms(acquired, encoded),
                duration_ms(encoded, presented),
                duration_ms(encoded, profile_submitted.unwrap()),
                duration_ms(profile_submitted.unwrap(), profile_notified.unwrap()),
                duration_ms(profile_notified.unwrap(), profile_present_begin.unwrap()),
                duration_ms(profile_present_begin.unwrap(), presented),
                u8::from(!readback_rgba.is_null()),
                capture_bytes, command_buffers_count, if audit_probe.is_some() {"sampling_audit"} else {"unavailable"});
            trace_owned_resources(dev, "window", f.width, f.height, capture_bytes, 0);
            let info = dev.adapter.get_info();
            let renderer = match info.backend {
                wgpu::Backend::Vulkan => "vulkan",
                wgpu::Backend::Gl => "opengl",
                wgpu::Backend::Metal => "metal",
                wgpu::Backend::Dx12 => "dx12",
                _ => "other",
            };
            eprintln!("COIN_RENDER_PHASE wgpu_surface renderer={} vendor_id={:#x} device_id={:#x} device_type={:?} surface={} serial={} size={}x{} adapter_name={:?}",
                renderer, info.vendor, info.device, info.device_type,
                surface_id, sub_serial, f.width, f.height, info.name);
        }
        record.published_serial = sub_serial;
        CoinWgpuStatus::Ok
    });

    res.unwrap_or_else(|_| {
        set_error(
            error_buf,
            error_buf_len,
            "Rust bridge panic caught in coin_wgpu_surface_submit",
        );
        CoinWgpuStatus::BackendError
    })
}

// Per-surface publication, independent of other targets and rejected work.
#[no_mangle]
pub extern "C" fn coin_wgpu_surface_submission_serial(surface_id: CoinWgpuSurfaceId) -> u64 {
    RUNTIME_CTX.lock().ok().and_then(|guard| {
        guard.as_ref().and_then(|runtime| runtime.surfaces.get(&surface_id))
            .map(|record| record.published_serial)
    }).unwrap_or(0)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_surface_submit(
    surface_id: CoinWgpuSurfaceId,
    frame: *const CoinWgpuFrameView,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    coin_wgpu_surface_submit_internal(surface_id, frame, std::ptr::null_mut(), 0,
        error_buf, error_buf_len)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_surface_submit_readback(
    surface_id: CoinWgpuSurfaceId,
    frame: *const CoinWgpuFrameView,
    rgba: *mut u8,
    rgba_len: usize,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    if rgba.is_null() {
        set_error(error_buf, error_buf_len, "Window RGBA output pointer is null");
        return CoinWgpuStatus::InvalidArgument;
    }
    coin_wgpu_surface_submit_internal(surface_id, frame, rgba, rgba_len,
        error_buf, error_buf_len)
}
