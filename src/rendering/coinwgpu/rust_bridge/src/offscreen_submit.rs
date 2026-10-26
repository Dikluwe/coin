//! Offscreen rendering submission and readback scheduling.

use super::*;

pub(super) fn coin_wgpu_submit_internal(
    target: *mut CoinWgpuTarget,
    frame: *const CoinWgpuFrameView,
    out_ticket: *mut CoinWgpuReadbackTicket,
    out_texture: *mut u64,
    depth_history: *mut u64,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    let res = std::panic::catch_unwind(|| {
        let profile_start = std::time::Instant::now();
        let trace_phases = std::env::var_os("COIN_RENDER_TRACE_PHASES").or_else(|| std::env::var_os("COIN_WGPU_TRACE_PHASES")).is_some();
        // 1. Fault injection check
        let requested_device = if !target.is_null()
            && (target as usize) % std::mem::align_of::<CoinWgpuTarget>() == 0 {
            unsafe { (*target).device_id }
        } else { 0 };

        let fault = fault_state::submission(requested_device);
        if fault == CoinWgpuStatus::NotReady as i32 {
            set_error(error_buf, error_buf_len, "Injected NOT_READY fault");
            return CoinWgpuStatus::NotReady;
        } else if fault == CoinWgpuStatus::OutOfMemory as i32 {
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
        }

        // 2. Real device lost check
        if requested_device == 0 && DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst) {
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

        if target.is_null() || frame.is_null() {
            set_error(error_buf, error_buf_len, "Null target or frame pointer");
            return CoinWgpuStatus::InvalidArgument;
        }
        if (target as usize) % std::mem::align_of::<CoinWgpuTarget>() != 0 {
            set_error(error_buf, error_buf_len, "Misaligned target pointer");
            return CoinWgpuStatus::InvalidArgument;
        }
        if (frame as usize) % std::mem::align_of::<CoinWgpuFrameView>() != 0 {
            set_error(error_buf, error_buf_len, "Misaligned frame pointer");
            return CoinWgpuStatus::InvalidArgument;
        }

        if !out_ticket.is_null() {
            if (out_ticket as usize) % std::mem::align_of::<CoinWgpuReadbackTicket>() != 0 {
                set_error(error_buf, error_buf_len, "Misaligned readback ticket pointer");
                return CoinWgpuStatus::InvalidArgument;
            }
            let ticket = unsafe { &mut *out_ticket };
            if ticket.abi_version != COIN_WGPU_ABI_VERSION
                || ticket.struct_size as usize != std::mem::size_of::<CoinWgpuReadbackTicket>() {
                set_error(error_buf, error_buf_len, "Readback ticket ABI version or size mismatch");
                return CoinWgpuStatus::InvalidArgument;
            }
        }
        if !out_texture.is_null() {
            if !out_ticket.is_null() || (out_texture as usize) % std::mem::align_of::<u64>() != 0 {
                set_error(error_buf, error_buf_len, "Invalid RTT output token pointer");
                return CoinWgpuStatus::InvalidArgument;
            }
            unsafe { *out_texture = 0; }
        }
        let tgt = unsafe { &mut *target };
        if tgt.device_id != 0 {
            let (fault, lost, error_kind) = {
                let guard = RUNTIME_CTX.lock().unwrap();
                let Some(runtime) = guard.as_ref() else {
                    set_error(error_buf, error_buf_len, "Unknown WebGPU device");
                    return CoinWgpuStatus::InvalidArgument;
                };
                if !runtime.extra_generations.contains_key(&tgt.device_id) {
                    set_error(error_buf, error_buf_len, "Unknown or destroyed WebGPU device");
                    return CoinWgpuStatus::InvalidArgument;
                }
                if let Some(device) = runtime.extra_devices.get(&tgt.device_id) {
                    (fault_state::take_device(device),
                     device.lost.load(Ordering::SeqCst),
                     device.async_error.swap(0, Ordering::SeqCst))
                } else { (0, false, 0) }
            };
            if lost || fault == CoinWgpuStatus::DeviceLost as i32 {
                if let Ok(mut guard) = RUNTIME_CTX.lock() {
                    if let Some(runtime) = guard.as_mut() {
                        lose_extra_device(runtime, tgt.device_id);
                    }
                }
                set_error(error_buf, error_buf_len, "WebGPU device lost");
                return CoinWgpuStatus::DeviceLost;
            }
            if fault == CoinWgpuStatus::OutOfMemory as i32 || error_kind == 1 {
                set_error(error_buf, error_buf_len, "WebGPU device out of memory");
                return CoinWgpuStatus::OutOfMemory;
            }
            if fault == CoinWgpuStatus::BackendError as i32 || error_kind == 2 {
                set_error(error_buf, error_buf_len, "WebGPU device validation error");
                return CoinWgpuStatus::BackendError;
            }
            if fault == CoinWgpuStatus::NotReady as i32 {
                set_error(error_buf, error_buf_len, "WebGPU device not ready");
                return CoinWgpuStatus::NotReady;
            }
        }
        let f = unsafe { &*frame };

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
        if bump_payload.is_some() && (!out_ticket.is_null() ||
            (!out_texture.is_null() && f.texture_output_flags != 0)) {
            set_error(error_buf, error_buf_len,
                "Bump frame needs synchronous RGBA8 offscreen or direct RTT output in this profile");
            return CoinWgpuStatus::Unsupported;
        }
        // Zero is the default of existing zero-initialized private FFI pods.
        let sample_count = if tgt.sample_count == 0 { 1 } else { tgt.sample_count };
        if !matches!(sample_count, 1 | 2 | 4) {
            set_error(error_buf, error_buf_len, "MSAA sample count must be 1, 2 or 4");
            return CoinWgpuStatus::InvalidArgument;
        }

        // 4. Dimensions and buffer boundary validation
        if f.width == 0 || f.height == 0 || tgt.width == 0 || tgt.height == 0 {
            set_error(
                error_buf,
                error_buf_len,
                "Target or frame dimensions must be > 0",
            );
            return CoinWgpuStatus::InvalidArgument;
        }
        if f.width != tgt.width || f.height != tgt.height {
            set_error(
                error_buf,
                error_buf_len,
                &format!(
                    "Dimension mismatch: frame ({}x{}) != target ({}x{})",
                    f.width, f.height, tgt.width, tgt.height
                ),
            );
            return CoinWgpuStatus::InvalidArgument;
        }

        let width = f.width;
        let height = f.height;

        let unaligned_bytes = match (width as usize).checked_mul(4) {
            Some(b) => b,
            None => {
                set_error(
                    error_buf,
                    error_buf_len,
                    "Width causes overflow in byte calculation",
                );
                return CoinWgpuStatus::InvalidArgument;
            }
        };
        let bytes_per_row = match unaligned_bytes.checked_add(255)
            .map(|bytes| bytes & !255)
            .and_then(|bytes| u32::try_from(bytes).ok()) {
            Some(bytes) => bytes,
            None => {
                set_error(error_buf, error_buf_len, "Aligned color row pitch overflows u32");
                return CoinWgpuStatus::InvalidArgument;
            }
        };
        let required_target_len = match (unaligned_bytes as u64).checked_mul(height as u64) {
            Some(len) => len,
            None => {
                set_error(
                    error_buf,
                    error_buf_len,
                    "Dimensions cause overflow in target buffer length",
                );
                return CoinWgpuStatus::InvalidArgument;
            }
        };

        if out_ticket.is_null() && out_texture.is_null()
            && (tgt.color_buffer.is_null() || tgt.color_buffer_len < required_target_len) {
            set_error(
                error_buf,
                error_buf_len,
                &format!(
                    "Target color buffer too small: required {}, got {}",
                    required_target_len, tgt.color_buffer_len
                ),
            );
            return CoinWgpuStatus::InvalidArgument;
        }

        // 5. Validate the changed states before considering a camera hint.
        // All immutable pointers are ignored only if this device still owns
        // the validated base. A stale/mismatched hint takes the old full path.
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

        let owned_patch = if out_texture.is_null() && f.frame_revision != 0
            && f.texture_count == 0 && f.sampler_count == 0 {
            RUNTIME_CTX.lock().ok().and_then(|guard| {
                let runtime = guard.as_ref()?;
                let device = if tgt.device_id == 0 {
                    runtime.device_state.as_ref()
                } else {
                    runtime.extra_devices.get(&tgt.device_id)
                }?;
                camera_base(device, f, states_slice)
            })
        } else { None };

        let vertices_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.vertices.as_slice()
        } else {
            match validate_slice(f.vertices, f.vertex_count, "vertices", error_buf, error_buf_len) {
                Ok(s) => s, Err(st) => return st,
            }
        };
        let indices_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.indices.as_slice()
        } else {
            match validate_slice(f.indices, f.index_count, "indices", error_buf, error_buf_len) {
                Ok(s) => s, Err(st) => return st,
            }
        };
        let draws_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.draws.as_slice()
        } else {
            match validate_slice(f.draws, f.draw_count, "draws", error_buf, error_buf_len) {
                Ok(s) => s, Err(st) => return st,
            }
        };
        if let Err(status)=validate_multipass_draws(f,draws_slice,error_buf,error_buf_len) {return status;}
        let materials_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.materials.as_slice()
        } else {
            match validate_slice(f.materials, f.material_count, "materials", error_buf, error_buf_len) {
                Ok(s) => s, Err(st) => return st,
            }
        };

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

        if !instances_slice.is_empty() && !out_texture.is_null() {
            set_error(error_buf, error_buf_len, "Instanced frames do not support render-to-texture");
            return CoinWgpuStatus::Unsupported;
        }

        let textures_slice = if owned_patch.is_some() { &[] } else if f.texture_count > 0 {
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

        let samplers_slice = if owned_patch.is_some() { &[] } else if f.sampler_count > 0 {
            match validate_slice(f.samplers, f.sampler_count, "samplers", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            }
        } else {
            &[]
        };

        if owned_patch.is_none() {
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
        }

        // Shadow payload is optional, but a partial payload is never accepted.
        // Keep it out of the camera patch until state/geometry matching is qualified.
        // A nonzero map size identifies a pass even when Core selected no
        // casters. Such a pass still clears its moments and lights receivers.
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

        // 6. Strict validation and composition are immutable for one private CoinRenderFramePlan revision.
        // A camera hint without an owned base readmits a new snapshot only
        // after this submission validates the full geometry, even if another
        // device previously validated the same private frame revision.
        let cached_draw_order = if portable_modules.is_empty() && instances_slice.is_empty() && owned_patch.is_none() && f.camera_base_revision == 0
            && f.frame_revision != 0 {
            VALIDATED_FRAME_CACHE.lock().ok().and_then(|cache| {
                cache.as_ref().and_then(|(revision, order)| {
                    if *revision == f.frame_revision && order.len() == draws_slice.len() &&
                        order.iter().all(|item| item.draw_index < draws_slice.len()) {
                        Some(order.clone())
                    } else { None }
                })
            })
        } else {
            None
        };
        let mut fully_validated = false;
        let draw_order = if let Some(scene) = owned_patch.as_ref() {
            scene.draw_order.clone()
        } else if let Some(order) = cached_draw_order {
            order
        } else {
        for (i, draw) in draws_slice.iter().enumerate() {
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

        // Reject unsupported composition before allocating offscreen attachments.
        let computed_draw_order = match composition::order(
            vertices_slice, indices_slice, draws_slice, materials_slice, states_slice, textures_slice) {
            Ok(order) => order,
            Err((status, message)) => {
                set_error(error_buf, error_buf_len, &message);
                return status;
            }
        };
        if portable_modules.is_empty() && instances_slice.is_empty() && f.frame_revision != 0 {
            if let Ok(mut cache) = VALIDATED_FRAME_CACHE.lock() {
                *cache = Some((f.frame_revision, computed_draw_order.clone()));
            }
        }
        fully_validated = true;
        computed_draw_order
        };

        if let Err((status, message)) = peeling::validate_request_samples(
            if draw_order.iter().any(|item| item.peel) { f.sorted_layers_passes } else { 1 },
            f.transparency_reserved,
            f.transparency_budget_bytes,
            f.width,
            f.height,
            draw_order.iter().any(|item| item.peel || item.weighted),
            sample_count,
        ) {
            set_error(error_buf, error_buf_len, &message);
            return status;
        }

        if draw_order.iter().any(|item| item.weighted) {
            if let Err((status, message)) = weighted::validate_budget(f.width, f.height, sample_count, f.transparency_budget_bytes) {
                set_error(error_buf, error_buf_len, &message);
                return status;
            }
        }

        if sample_count > 1 &&
            draw_order.iter().any(|item| bump_payload.is_some() && item.blend) {
            set_error(error_buf, error_buf_len,
                "MSAA translucent bump needs a separate profile");
            return CoinWgpuStatus::Unsupported;
        }

        let pending_multipass=RUNTIME_CTX.lock().ok().and_then(|runtime|multipass_pending_bytes(runtime.as_ref()));
        let multipass_depth=tgt.depth_buffer_len>=u64::from(f.width)*u64::from(f.height) &&
            (!out_ticket.is_null() || !tgt.depth_buffer.is_null());
        if let Err((status,message))=admit_multipass_resources(f,sample_count,states_slice,textures_slice,
            &draw_order,shadow_frame.as_ref(),false,out_texture.is_null(),multipass_depth,pending_multipass) {
            set_error(error_buf,error_buf_len,&message);return status;
        }

        // 7. Initialize WebGPU Device
        if let Err(e) = init_runtime_if_needed() {
            set_error(error_buf, error_buf_len, &e);
            return CoinWgpuStatus::NotReady;
        }

        let mut guard = match RUNTIME_CTX.lock() {
            Ok(g) => g,
            Err(e) => {
                set_error(error_buf, error_buf_len, &e.to_string());
                return CoinWgpuStatus::BackendError;
            }
        };
        let runtime = guard.as_mut().unwrap();
        if !out_ticket.is_null() {
            reap_cancelled_readbacks(runtime);
            // Tickets reserve their packed public payload, independently of
            // sample attachments and the backend's aligned staging rows.
            // This is the same finite reservation used by C++ before RTT
            // producers run, and also protects direct FFI submissions.
            let depth = tgt.depth_buffer_len >= u64::from(width) * u64::from(height);
            let requested = required_target_len.checked_mul(if depth { 2 } else { 1 });
            let pending = runtime.pending_readbacks.values()
                .chain(runtime.retired_readbacks.iter()).try_fold(0u64, |bytes, job|
                    bytes.checked_add(job.ticket.color_bytes)?.checked_add(job.ticket.depth_bytes));
            if runtime.pending_readbacks.len() + runtime.retired_readbacks.len() >= 16
                || requested.zip(pending).is_none_or(|(requested, pending)|
                    requested > 128 * 1024 * 1024 || pending > 128 * 1024 * 1024 - requested) {
                set_error(error_buf, error_buf_len, "Readback budget exhausted: sixteen tickets or 128 MiB of pending output");
                return CoinWgpuStatus::NotReady;
            }
        }

        let device_state = match offscreen_device(runtime, tgt.device_id) {
            Ok(d) => d as *mut DeviceState,
            Err(e) => {
                set_error(error_buf, error_buf_len, &e);
                return CoinWgpuStatus::NotReady;
            }
        };
        let ctx = unsafe { &mut *device_state };
        if bump_payload.is_some() && (f.rasterization_policy != 0 ||
            !instances_slice.is_empty() || !instance_ranges.is_empty()) {
            set_error(error_buf, error_buf_len, "Bump requires native non-instanced geometry");
            return CoinWgpuStatus::Unsupported;
        }

        if let Some(payload) = bump_payload.as_ref() {
            if extended_bump_profile(states_slice, textures_slice, draws_slice, shadow_frame.as_ref(), payload, indices_slice) {
                if let Err((status, message)) = validate_extended_bump_profile(ctx,
                    vertices_slice, indices_slice, draws_slice, states_slice,
                    materials_slice, textures_slice, samplers_slice, payload,
                    shadow_frame.as_ref(), [f.width, f.height]) {
                    set_error(error_buf, error_buf_len, &message);
                    return status;
                }
            }
        }

        if let Err((status,message))=admit_spatial_resources(ctx,textures_slice)
            .and_then(|()|raster_image::admit(ctx,states_slice,textures_slice,samplers_slice.len())) {
            set_error(error_buf,error_buf_len,&message); return status;
        }
        if let Some(scene) = owned_patch.as_ref() {
            if scene.generation != ctx.generation {
                set_error(error_buf, error_buf_len, "Camera base belongs to a lost device generation");
                return CoinWgpuStatus::DeviceLost;
            }
            // Another submit may have replaced the device's base while the
            // incoming state was checked. Never encode against a stale GPU
            // geometry cache, even though the old CPU snapshot is still alive.
            if !ctx.validated_scene.as_ref().is_some_and(|current| Arc::ptr_eq(current, scene)) {
                set_error(error_buf, error_buf_len, "Camera base is no longer current on this device");
                return CoinWgpuStatus::NotReady;
            }
        }
        let profile_validated = std::time::Instant::now();
        let gpu_profile_requested = trace_phases
            && std::env::var("COIN_WGPU_GPU_TIMESTAMPS").as_deref() == Ok("1")
            && out_ticket.is_null() && out_texture.is_null();
        let gpu_profile_features = wgpu::Features::TIMESTAMP_QUERY
            | wgpu::Features::TIMESTAMP_QUERY_INSIDE_ENCODERS;
        let gpu_probe = (gpu_profile_requested
            && ctx.device.features().contains(gpu_profile_features))
            .then(|| GpuTimestampProbe::new(&ctx.device));
        let profile_probe_ready = trace_phases.then(std::time::Instant::now);

        if !out_texture.is_null() && ctx.rtt_textures.lock().unwrap().active.len() >= 64 {
            set_error(error_buf, error_buf_len, "Too many active RTT textures");
            return CoinWgpuStatus::OutOfMemory;
        }
        if f.texture_sampling_policy>1 || f.texture_output_flags&!7!=0 ||
           (f.texture_output_flags!=0 && out_texture.is_null()) {
            set_error(error_buf,error_buf_len,"Advanced output flags require a direct RTT destination");
            return CoinWgpuStatus::InvalidArgument;
        }
        if f.clear_color.iter().any(|v| !v.is_finite() || (f.texture_output_flags&1!=0 && v.abs()>65504.0)) {
            set_error(error_buf,error_buf_len,"Invalid RTT clear color or half range");
            return CoinWgpuStatus::InvalidArgument;
        }
        if draw_order.iter().any(|item| item.peel) && !peeling_supported(ctx, sample_count) {
            set_error(error_buf, error_buf_len, "Peeling formats cannot sample/render at the requested count");
            return CoinWgpuStatus::Unsupported;
        }
        if draw_order.iter().any(|item| item.weighted) &&
            !weighted_supported(ctx, sample_count) {
            set_error(error_buf, error_buf_len, "Weighted MSAA color formats cannot blend/sample at the requested count");
            return CoinWgpuStatus::Unsupported;
        }
        let output_format=if f.texture_output_flags&1!=0 {wgpu::TextureFormat::Rgba16Float} else {wgpu::TextureFormat::Rgba8Unorm};
        if let Err((status,message)) = portable::prepare(ctx,&portable_modules,states_slice,draws_slice,
            &draw_order,sample_count,f.texture_sampling_policy,f.rasterization_policy,
            shadow_frame.as_ref().map_or(0,|shadow|if shadow.extra.is_empty(){1}else{2}),output_format) {
            set_error(error_buf,error_buf_len,&message); return status;
        }
        let output_mips=if f.texture_output_flags&2!=0 {width.max(height).ilog2()+1} else {1};
        if sample_count > 1 {
            let required = if sample_count == 2 {
                wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X2
            } else {
                wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X4
            };
            if !ctx.adapter.get_texture_format_features(output_format).flags.contains(required) ||
                !ctx.adapter.get_texture_format_features(wgpu::TextureFormat::Depth32Float).flags.contains(required) ||
                (sample_count == 2 && !ctx.device.features().contains(
                    wgpu::Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES)) {
                set_error(error_buf, error_buf_len, "Exact MSAA sample count unavailable for color/depth on this device");
                return CoinWgpuStatus::Unsupported;
            }
        }
        if !out_texture.is_null() &&
           (width > ctx.device.limits().max_texture_dimension_2d ||
            height > ctx.device.limits().max_texture_dimension_2d) {
            set_error(error_buf,error_buf_len,"Direct RTT exceeds the adapter texture dimension");
            return CoinWgpuStatus::Unsupported;
        }
        if !ctx.adapter.get_texture_format_features(output_format).allowed_usages.contains(
            wgpu::TextureUsages::RENDER_ATTACHMENT|wgpu::TextureUsages::TEXTURE_BINDING|
              if output_mips>1 {wgpu::TextureUsages::STORAGE_BINDING} else {wgpu::TextureUsages::empty()}) {
            set_error(error_buf,error_buf_len,"Direct RTT format/mip storage unavailable");
            return CoinWgpuStatus::Unsupported;
        }
        let depth_requested = tgt.depth_buffer_len >= (width as u64) * (height as u64)
            && (!out_ticket.is_null() || !tgt.depth_buffer.is_null());
        // 8. A completed synchronous frame may lend its attachments to the
        // next synchronous frame on this device. Async and RTT submissions
        // are excluded: their textures can still be in use by the GPU.
        let attachment_cache_enabled = sample_count == 1 && out_ticket.is_null() && out_texture.is_null()
            && std::env::var("COIN_WGPU_ATTACHMENT_CACHE").as_deref() == Ok("1")
            && (width as u64).checked_mul(height as u64)
                .and_then(|pixels| pixels.checked_mul(8))
                .is_some_and(|bytes| bytes <= CachedOffscreenAttachments::MAX_BYTES);
        let cached = if sample_count == 1 && out_ticket.is_null() && out_texture.is_null() {
            ctx.cached_offscreen_attachments.take()
                .filter(|entry| attachment_cache_enabled
                    && entry.width == width && entry.height == height)
        } else { None };
        let prior_depth = if depth_history.is_null() { None } else {
            ctx.target_depth_histories.get(unsafe { &*depth_history })
                .filter(|entry| entry.width == width && entry.height == height && entry.samples == sample_count)
                .map(|entry| (entry.texture.clone(), entry.view.clone()))
        };
        let attachments_reused = cached.is_some();
        let (color_texture, color_view, cached_depth) = if let Some(entry) = cached {
            (entry.color_texture, entry.color_view, entry.depth)
        } else {
            let texture_desc = wgpu::TextureDescriptor {
                label: Some("Offscreen Color Texture"),
                size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
                mip_level_count: output_mips,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format: output_format,
                usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC | wgpu::TextureUsages::TEXTURE_BINDING | if output_mips>1 {wgpu::TextureUsages::STORAGE_BINDING} else {wgpu::TextureUsages::empty()},
                view_formats: &[],
            };
            if fault == FAULT_RTT_COLOR_ALLOC && !out_texture.is_null() {
                set_error(error_buf, error_buf_len, "Injected RTT color texture allocation failure");
                return CoinWgpuStatus::OutOfMemory;
            }
            let color_texture = ctx.device.create_texture(&texture_desc);
            if fault == FAULT_RTT_COLOR_VIEW && !out_texture.is_null() {
                set_error(error_buf, error_buf_len, "Injected RTT color view creation failure");
                return CoinWgpuStatus::OutOfMemory;
            }
            let color_view = color_texture.create_view(&wgpu::TextureViewDescriptor{mip_level_count:Some(1),..Default::default()});

            (color_texture, color_view, None)
        };
        let (depth_texture, depth_view) = if sample_count == 1 && prior_depth.is_some() {
            let (texture, view) = prior_depth.as_ref().unwrap();
            (texture.clone(), view.clone())
        } else if let Some(depth) = cached_depth {
            depth
        } else {
            let depth_desc = wgpu::TextureDescriptor {
                label: Some("Offscreen Depth Texture"),
                size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
                mip_level_count: 1,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format: wgpu::TextureFormat::Depth32Float,
                // With MSAA the render pass uses the separate multisampled
                // attachment below. Keep the 1x depth copy contract intact,
                // but do not require depth-buffer copies merely to allocate
                // the unused single-sample companion on GL adapters.
                usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING |
                    if sample_count == 1 { wgpu::TextureUsages::COPY_SRC }
                    else { wgpu::TextureUsages::empty() },
                view_formats: &[],
            };
            let depth_texture = ctx.device.create_texture(&depth_desc);
            if fault == FAULT_RTT_DEPTH_ALLOC && !out_texture.is_null() {
                set_error(error_buf, error_buf_len, "Injected RTT depth attachment failure");
                return CoinWgpuStatus::OutOfMemory;
            }
            let depth_view = depth_texture.create_view(&wgpu::TextureViewDescriptor::default());
            (depth_texture, depth_view)
        };
        let msaa_attachments = if sample_count > 1 {
            let extent = wgpu::Extent3d { width, height, depth_or_array_layers: 1 };
            let color = ctx.device.create_texture(&wgpu::TextureDescriptor {
                label: Some("Coin MSAA color"), size: extent, mip_level_count: 1,
                sample_count, dimension: wgpu::TextureDimension::D2,
                // This attachment is only rendered and resolved. Consumers
                // sample the single-sample output, as for window MSAA color.
                format: output_format, usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
                view_formats: &[],
            });
            let depth = if let Some((texture, _)) = prior_depth.as_ref() { texture.clone() }
              else { ctx.device.create_texture(&wgpu::TextureDescriptor {
                label: Some("Coin MSAA depth"), size: extent, mip_level_count: 1,
                sample_count, dimension: wgpu::TextureDimension::D2,
                format: wgpu::TextureFormat::Depth32Float,
                // Peeling samples opaque depth even for GPU-only RTT and
                // targets that do not request synchronous depth readback.
                usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING,
                view_formats: &[],
            }) };
            let color_view = color.create_view(&wgpu::TextureViewDescriptor::default());
            let depth_view = depth.create_view(&wgpu::TextureViewDescriptor::default());
            Some((color, color_view, depth, depth_view))
        } else { None };
        let profile_attachments_created = trace_phases.then(std::time::Instant::now);

        let capture_scene = !instances_slice.is_empty() || ctx.camera_snapshot_policy.capture(
            ctx.validated_scene.as_ref().map(|scene| scene.revision), f.frame_revision,
            f.camera_base_revision, owned_patch.is_some());
        let next_scene = if capture_scene && out_texture.is_null() && shadow_frame.is_none()
            && (owned_patch.is_some() || fully_validated) {
            camera_scene(ctx.generation, f, vertices_slice, indices_slice, draws_slice, materials_slice,
                states_slice, &draw_order, textures_slice, samplers_slice, instances_slice, instance_ranges, owned_patch.as_ref())
        } else { None };

        // 9. Encode draw calls with the exact same shared encode_frame
        if f.multipass_count > 1 && sample_count > 1 && fault == 307 {
            set_error(error_buf,error_buf_len,"Injected MSAA failure before multipass queue submission");
            return CoinWgpuStatus::OutOfMemory;
        }
        let encoded = encode_scene_samples(
            ctx, f, [width,height], vertices_slice, indices_slice, draws_slice, &draw_order,
            materials_slice, states_slice, &portable_modules, textures_slice, samplers_slice,
            bump_payload.as_ref(), shadow_frame.as_ref(), next_scene.as_ref().map(|s| &s.geometry),
            instances_slice, instance_ranges, gpu_probe.as_ref().map(|p| &p.queries),
            msaa_attachments.as_ref().map_or(&color_view, |a| &a.1), msaa_attachments.as_ref().map(|_| &color_view), output_format, msaa_attachments.as_ref().map_or(&depth_view, |a| &a.3), msaa_attachments.as_ref().map_or(&depth_texture, |a| &a.2), sample_count);
        let (mut cmd_buffers, pending_instances) = match encoded {
            Ok(cmd) => cmd,
            Err((status, msg)) => {
                set_error(error_buf, error_buf_len, &msg);
                return status;
            }
        };
        if prior_depth.is_none() && manager_backgrounds(draws_slice) {
            cmd_buffers.insert(0, initialize_target_depth(&ctx.device,
                msaa_attachments.as_ref().map_or(&depth_view, |a| &a.3)));
        }
        if sample_count > 1 && fault == 307 {
            set_error(error_buf, error_buf_len, "Injected MSAA failure after encoding, before queue submission");
            return CoinWgpuStatus::OutOfMemory;
        }
        let profile_draw_encoded = trace_phases.then(std::time::Instant::now);
        // Any successful full frame replaces the device's current scene even
        // when its revision is zero or snapshot admission is suspended.
        let commit_scene = true;
        let profile_encoded = std::time::Instant::now();

        if !out_texture.is_null() {
            let mut commands=cmd_buffers;
            if output_mips>1 {
                if fault==306 {set_error(error_buf,error_buf_len,"Injected RTT mip encoding failure");return CoinWgpuStatus::OutOfMemory;}
                match texture_mips::encode(&ctx.device,&color_texture,output_format,output_mips,width,height,ctx.adapter.get_info().backend==wgpu::Backend::Gl) {
                    Ok(command)=>commands.push(command),
                    Err((status,message))=>{set_error(error_buf,error_buf_len,&message);return status;}
                }
            }
            let token = NEXT_RTT_TOKEN.fetch_add(1, Ordering::SeqCst);
            if token == 0 {
                set_error(error_buf, error_buf_len, "RTT token space exhausted");
                return CoinWgpuStatus::OutOfMemory;
            }
            let serial = GLOBAL_SUBMISSION_SERIAL.fetch_add(1, Ordering::SeqCst);
            LAST_SUBMITTED_SERIAL.store(serial, Ordering::SeqCst);
            let local_serial = ctx.last_submitted_serial.fetch_add(1, Ordering::SeqCst) + 1;
            ctx.queue.submit(commands);
            let completed = ctx.completed_serial.clone();
            ctx.queue.on_submitted_work_done(move || {
                completed.fetch_max(local_serial, Ordering::SeqCst);
                GLOBAL_COMPLETED_SERIAL.fetch_max(serial, Ordering::SeqCst);
            });
            let sampling_view=color_texture.create_view(&wgpu::TextureViewDescriptor::default());
            ctx.rtt_textures.lock().unwrap().active.insert(token, RttTexture {
                texture: color_texture,
                // An opaque clear does not prove all later writes opaque:
                // legacy blend and NONE can both reduce destination alpha.
                // Captured Coin producers conservatively make no opaque claim.
                opaque: false,
                view: sampling_view,
                width,
                height,
                format: if f.texture_output_flags&1!=0 {2} else {0},
                mip_count:output_mips, cube_face_size:0, cube_mipmapped:false,
                completed_view: (f.texture_output_flags&4!=0).then(|| (
                    msaa_attachments.as_ref().map_or_else(||color_view.clone(),|a|a.1.clone()),
                    msaa_attachments.as_ref().map_or_else(||depth_view.clone(),|a|a.3.clone()),sample_count)),
            });
            tgt.submission_serial = serial;
            unsafe { *out_texture = token; }
            commit_camera_scene(ctx, next_scene, commit_scene, f.frame_revision, owned_patch.is_some(), capture_scene,
                !instances_slice.is_empty(), pending_instances);
            return CoinWgpuStatus::Ok;
        }

        // 10. Copy to staging buffer for readback
        let staging_size = (bytes_per_row as u64) * (height as u64);
        let (staging_buffer, color_staging_reused) = ctx.readback_pool
            .lock().unwrap().acquire(&ctx.device, staging_size);

        let mut copy_encoder = ctx
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("Offscreen Copy Encoder"),
            });
        if let Some(probe) = gpu_probe.as_ref() {
            copy_encoder.write_timestamp(&probe.queries, 2);
        }

        copy_encoder.copy_texture_to_buffer(
            wgpu::TexelCopyTextureInfo {
                texture: &color_texture,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            wgpu::TexelCopyBufferInfo {
                buffer: &staging_buffer,
                layout: wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(bytes_per_row),
                    rows_per_image: Some(height),
                },
            },
            wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
        );

        let depth_unpadded = width * 4;
        let depth_bytes_per_row = (depth_unpadded + 255) & !255;
        let depth_staging_size = (depth_bytes_per_row as u64) * (height as u64);
        let depth_staging_info = if depth_requested
        {
            let (dbuf, _) = ctx.readback_pool
                .lock().unwrap().acquire(&ctx.device, depth_staging_size);
            let converted = if sample_count > 1 {
                let mut converter = ctx.depth_readback.lock().unwrap();
                let converter = converter.get_or_insert_with(|| {
                    if ctx.adapter.get_info().backend == wgpu::Backend::Gl {
                        depth_transfer::DepthTransfer::new_gl(&ctx.device, false)
                    } else {
                        depth_transfer::DepthTransfer::new(&ctx.device, false)
                    }
                });
                Some(converter.encode_msaa(&ctx.device, &mut copy_encoder,
                    &msaa_attachments.as_ref().unwrap().3, width, height, sample_count))
            } else if !ctx.adapter.get_downlevel_capabilities().flags
                .contains(wgpu::DownlevelFlags::DEPTH_TEXTURE_AND_BUFFER_COPIES) {
                let mut converter = ctx.depth_readback.lock().unwrap();
                let converter = converter.get_or_insert_with(|| {
                    if ctx.adapter.get_info().backend == wgpu::Backend::Gl {
                        depth_transfer::DepthTransfer::new_gl(&ctx.device, false)
                    } else {
                        depth_transfer::DepthTransfer::new(&ctx.device, false)
                    }
                });
                Some(converter.encode(&ctx.device, &mut copy_encoder, &depth_view, width, height))
            } else { None };
            copy_encoder.copy_texture_to_buffer(
                wgpu::TexelCopyTextureInfo {
                    texture: converted.as_ref().unwrap_or(&depth_texture),
                    mip_level: 0,
                    origin: wgpu::Origin3d::ZERO,
                    aspect: if converted.is_some() { wgpu::TextureAspect::All }
                        else { wgpu::TextureAspect::DepthOnly },
                },
                wgpu::TexelCopyBufferInfo {
                    buffer: &dbuf,
                    layout: wgpu::TexelCopyBufferLayout {
                        offset: 0,
                        bytes_per_row: Some(depth_bytes_per_row),
                        rows_per_image: Some(height),
                    },
                },
                wgpu::Extent3d {
                    width,
                    height,
                    depth_or_array_layers: 1,
                },
            );
            Some((dbuf, depth_bytes_per_row))
        } else {
            None
        };

        if let Some(probe) = gpu_probe.as_ref() {
            copy_encoder.write_timestamp(&probe.queries, 3);
            copy_encoder.resolve_query_set(&probe.queries, 0..4, &probe.resolved, 0);
            copy_encoder.copy_buffer_to_buffer(
                &probe.resolved, 0, &probe.readback, 0, GpuTimestampProbe::BYTES,
            );
        }

        let sub_serial = GLOBAL_SUBMISSION_SERIAL.fetch_add(1, Ordering::SeqCst);
        LAST_SUBMITTED_SERIAL.store(sub_serial, Ordering::SeqCst);
        let local_serial = ctx.last_submitted_serial.fetch_add(1, Ordering::SeqCst) + 1;
        let profile_submit_begin = std::time::Instant::now();
        let mut commands = cmd_buffers;
        commands.push(copy_encoder.finish());
        ctx.queue.submit(commands);
        let profile_submitted = std::time::Instant::now();
        let completed = ctx.completed_serial.clone();
        ctx.queue.on_submitted_work_done(move || {
            completed.fetch_max(local_serial, Ordering::SeqCst);
            GLOBAL_COMPLETED_SERIAL.fetch_max(sub_serial, Ordering::SeqCst);
        });

        let buffer_slice = staging_buffer.slice(..);
        let (sender, receiver) = std::sync::mpsc::channel();
        buffer_slice.map_async(wgpu::MapMode::Read, move |result| {
            let _ = sender.send(result);
        });

        let (depth_sender, depth_receiver) = std::sync::mpsc::channel();
        let depth_slice_holder = depth_staging_info.as_ref().map(|(buf, _)| buf.slice(..));
        if let Some(ref dslice) = depth_slice_holder {
            dslice.map_async(wgpu::MapMode::Read, move |result| {
                let _ = depth_sender.send(result);
            });
        }

        let gpu_timestamp_receiver = gpu_probe.as_ref().map(|probe| {
            let (sender, receiver) = std::sync::mpsc::channel();
            probe.readback.slice(..).map_async(wgpu::MapMode::Read, move |result| {
                let _ = sender.send(result);
            });
            receiver
        });

        if !out_ticket.is_null() {
            let token = NEXT_READBACK_TOKEN.fetch_add(1, Ordering::SeqCst);
            let ticket = CoinWgpuReadbackTicket {
                abi_version: COIN_WGPU_ABI_VERSION,
                struct_size: std::mem::size_of::<CoinWgpuReadbackTicket>() as u32,
                token,
                generation: ctx.generation,
                submission_serial: sub_serial,
                width,
                height,
                color_format: 0,
                depth_format: if depth_staging_info.is_some() { 1 } else { 0 },
                color_row_pitch: bytes_per_row,
                depth_row_pitch: if depth_staging_info.is_some() { depth_bytes_per_row } else { 0 },
                color_bytes: required_target_len,
                depth_bytes: if depth_staging_info.is_some() {
                    (width as u64) * (height as u64) * 4
                } else { 0 },
            };
            runtime.pending_readbacks.insert(token, PendingReadback {
                ticket,
                device_id: ctx.device_id,
                color: staging_buffer.clone(),
                color_receiver: receiver,
                color_ready: None,
                depth: depth_staging_info.as_ref().map(|(buffer, _)| buffer.clone()),
                depth_receiver: if depth_staging_info.is_some() { Some(depth_receiver) } else { None },
                depth_ready: None,
                pool: ctx.readback_pool.clone(),
            });
            unsafe { *out_ticket = ticket; }
            tgt.submission_serial = sub_serial;
            commit_camera_scene(ctx, next_scene, commit_scene, f.frame_revision, owned_patch.is_some(), capture_scene,
                !instances_slice.is_empty(), pending_instances);
            return CoinWgpuStatus::Ok;
        }

        let profile_wait_begin = std::time::Instant::now();
        let _ = ctx.device.poll(wgpu::Maintain::Wait);
        let profile_wait_done = std::time::Instant::now();
        if ctx.device_id != 0 {
            if ctx.lost.swap(false, Ordering::SeqCst) {
                lose_extra_device(runtime, tgt.device_id);
                set_error(error_buf, error_buf_len, "WebGPU device lost during submit");
                return CoinWgpuStatus::DeviceLost;
            }
            let kind = ctx.async_error.swap(0, Ordering::SeqCst);
            if kind != 0 {
                set_error(error_buf, error_buf_len, "WebGPU asynchronous device error");
                return if kind == 1 { CoinWgpuStatus::OutOfMemory }
                    else { CoinWgpuStatus::BackendError };
            }
        }


        // Inject async fault if requested
        let async_fault = if ctx.device_id == 0 { fault_state::take_async() } else { 0 };
        if async_fault == CoinWgpuStatus::OutOfMemory as i32 {
            set_error(error_buf, error_buf_len, "Injected async OUT_OF_MEMORY after GPU execution");
            return CoinWgpuStatus::OutOfMemory;
        }
        if async_fault == CoinWgpuStatus::DeviceLost as i32 {
            DEVICE_LOST_OCCURRED.store(true, Ordering::SeqCst);
            if let Ok(mut lock) = LAST_ASYNC_ERROR_MSG.lock() {
                *lock = "Injected async DEVICE_LOST fault during GPU execution".to_string();
            }
        }

        // Check if device lost occurred during submit or poll
        if ctx.device_id == 0 && DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst) {
            runtime.device_state = None;
            runtime.device_generation += 1;
            let msg = LAST_ASYNC_ERROR_MSG
                .lock()
                .map(|m| m.clone())
                .unwrap_or_else(|_| "Device lost during submit".to_string());
            set_error(error_buf, error_buf_len, &msg);
            return CoinWgpuStatus::DeviceLost;
        }

        // Check uncaptured errors
        let error_kind = if ctx.device_id == 0 { LAST_ASYNC_ERROR_KIND.swap(0, Ordering::SeqCst) } else { 0 };
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

        let profile_gpu_read_begin = trace_phases.then(std::time::Instant::now);
        let timestamp_period_ns = gpu_probe.as_ref()
            .map(|_| ctx.queue.get_timestamp_period() as f64).unwrap_or(0.0);
        let mut gpu_timing_status = if gpu_profile_requested { "unsupported" } else { "disabled" };
        let mut gpu_timing_ms = None;
        if let (Some(probe), Some(timestamp_receiver)) =
            (gpu_probe.as_ref(), gpu_timestamp_receiver)
        {
            gpu_timing_status = "readback_error";
            if matches!(timestamp_receiver.recv(), Ok(Ok(()))) {
                let mapped = probe.readback.slice(..).get_mapped_range();
                if mapped.len() >= GpuTimestampProbe::BYTES as usize
                    && timestamp_period_ns.is_finite() && timestamp_period_ns > 0.0
                {
                    let ticks: [u64; 4] = std::array::from_fn(|index| {
                        let offset = index * 8;
                        u64::from_ne_bytes(mapped[offset..offset + 8].try_into().unwrap())
                    });
                    gpu_timing_status = "invalid_ticks";
                    if let (Some(render_ticks), Some(copy_ticks)) =
                        (ticks[1].checked_sub(ticks[0]), ticks[3].checked_sub(ticks[2]))
                    {
                        gpu_timing_ms = Some((
                            render_ticks as f64 * timestamp_period_ns / 1_000_000.0,
                            copy_ticks as f64 * timestamp_period_ns / 1_000_000.0,
                        ));
                        gpu_timing_status = "ok";
                    }
                } else {
                    gpu_timing_status = "invalid_ticks";
                }
                drop(mapped);
                probe.readback.unmap();
            }
        }
        let profile_publish_begin = std::time::Instant::now();
        match receiver.recv() {
            Ok(Ok(())) => {
                let data = buffer_slice.get_mapped_range();
                if (data.len() as u64) < staging_size {
                    set_error(error_buf, error_buf_len, "Mapped color staging buffer is truncated");
                    return CoinWgpuStatus::BackendError;
                }
                let profile_color_mapped = trace_phases.then(std::time::Instant::now);
                // Color-only output has no second attachment to commit
                // atomically, so publish rows directly to the caller buffer.
                let mut pending_color = depth_staging_info.as_ref()
                    .map(|_| vec![0u8; required_target_len as usize]);
                if let Some(ref mut color) = pending_color {
                    copy_color_rows(color, &data, width, height, bytes_per_row);
                } else {
                    let output = match validate_slice_mut(tgt.color_buffer, required_target_len,
                        "target color buffer", error_buf, error_buf_len) {
                        Ok(output) => output, Err(status) => return status,
                    };
                    copy_color_rows(output, &data, width, height, bytes_per_row);
                }
                let profile_color_copied = trace_phases.then(std::time::Instant::now);

                let mut pending_depth = None;
                if let (Some(ref dslice), Some((_, d_bpr))) = (&depth_slice_holder, &depth_staging_info) {
                    match depth_receiver.recv() {
                        Ok(Ok(())) => (),
                        Ok(Err(e)) => {
                            set_error(error_buf, error_buf_len, &format!("Depth buffer mapping failed: {:?}", e));
                            return CoinWgpuStatus::BackendError;
                        }
                        Err(e) => {
                            set_error(error_buf, error_buf_len, &format!("Depth mapping channel failed: {:?}", e));
                            return CoinWgpuStatus::BackendError;
                        }
                    }
                    let ddata = dslice.get_mapped_range();
                    let dfloats: &[f32] = bytemuck::cast_slice(&ddata);
                    let floats_per_row = (*d_bpr / 4) as usize;
                    let row_floats = width as usize;
                    let mut depth = vec![1.0f32; (width as usize) * (height as usize)];
                    for y in 0..height as usize {
                        let src_off = y * floats_per_row;
                        let dst_off = y * row_floats;
                        if src_off + row_floats > dfloats.len() {
                            set_error(error_buf, error_buf_len, "Mapped depth staging buffer is truncated");
                            return CoinWgpuStatus::BackendError;
                        }
                        depth[dst_off..dst_off + row_floats]
                            .copy_from_slice(&dfloats[src_off..src_off + row_floats]);
                    }
                    pending_depth = Some(depth);
                }
                let profile_depth_copied = trace_phases.then(std::time::Instant::now);

                // Publish both attachments only after every requested map succeeds.
                if let Some(color) = pending_color {
                    let out_buf = match validate_slice_mut(tgt.color_buffer, required_target_len,
                        "target color buffer", error_buf, error_buf_len) {
                        Ok(b) => b,
                        Err(st) => return st,
                    };
                    out_buf.copy_from_slice(&color);
                }
                if let Some(depth) = pending_depth {
                    let out_depth = unsafe { std::slice::from_raw_parts_mut(tgt.depth_buffer, depth.len()) };
                    out_depth.copy_from_slice(&depth);
                }
                let profile_outputs_published = trace_phases.then(std::time::Instant::now);
                drop(data);
                staging_buffer.unmap();
                if let Ok(mut pool) = ctx.readback_pool.lock() {
                    pool.recycle(staging_buffer.clone());
                    if let Some((buffer, _)) = depth_staging_info.as_ref() {
                        buffer.unmap();
                        pool.recycle(buffer.clone());
                    }
                }
                if !depth_history.is_null() {
                    let token = unsafe { &mut *depth_history };
                    if *token == 0 { *token = NEXT_DEPTH_HISTORY.fetch_add(1, Ordering::Relaxed); }
                    let (texture, view) = msaa_attachments.as_ref()
                        .map_or((&depth_texture, &depth_view), |a| (&a.2, &a.3));
                    ctx.target_depth_histories.insert(*token, TargetDepthHistory {
                        width, height, samples: sample_count, texture: texture.clone(), view: view.clone(),
                    });
                }
                tgt.submission_serial = sub_serial;
                commit_camera_scene(ctx, next_scene, commit_scene, f.frame_revision, owned_patch.is_some(), capture_scene,
                    !instances_slice.is_empty(), pending_instances);
                if attachment_cache_enabled {
                    ctx.cached_offscreen_attachments = Some(CachedOffscreenAttachments {
                        width, height, color_texture, color_view,
                        depth: depth_history.is_null().then_some((depth_texture, depth_view)),
                    });
                }
                if trace_phases {
                    let profile_done = std::time::Instant::now();
                    let info = ctx.adapter.get_info();
                    let renderer = match info.backend {
                        wgpu::Backend::Vulkan => "vulkan",
                        wgpu::Backend::Gl => "opengl",
                        wgpu::Backend::Metal => "metal",
                        wgpu::Backend::Dx12 => "dx12",
                        _ => "other",
                    };
                    // Emit only after checked GPU completion and publication.
                    eprintln!("COIN_RENDER_PHASE wgpu_offscreen renderer={} vendor_id={:#x} device_id={:#x} device_type={:?} serial={} size={}x{} adapter_name={:?}",
                        renderer, info.vendor, info.device, info.device_type,
                        sub_serial, width, height, info.name);
                    trace_owned_resources(ctx, "offscreen", width, height, staging_size,
                        if depth_requested { depth_staging_size } else { 0 });
                    eprintln!("COIN_RENDER_PHASE rust validation_ms={:.6} prepare_encode_ms={:.6} submit_ms={:.6} gpu_wait_ms={:.6} readback_publish_ms={:.6} staging_color_reused={} attachments_reused={} camera_bindings_created={} camera_bindings_reused={}",
                        (profile_validated - profile_start).as_secs_f64() * 1000.0,
                        (profile_encoded - profile_validated).as_secs_f64() * 1000.0,
                        (profile_submitted - profile_submit_begin).as_secs_f64() * 1000.0,
                        (profile_wait_done - profile_wait_begin).as_secs_f64() * 1000.0,
                        (profile_done - profile_publish_begin).as_secs_f64() * 1000.0,
                        u8::from(color_staging_reused),
                        u8::from(attachments_reused),
                        ctx.camera_bindings_created.load(Ordering::Relaxed),
                        ctx.camera_bindings_reused.load(Ordering::Relaxed));
                    let duration_ms = |start: std::time::Instant, end: std::time::Instant| {
                        (end - start).as_secs_f64() * 1000.0
                    };
                    let attachments = profile_attachments_created.unwrap();
                    let draw_encoded = profile_draw_encoded.unwrap();
                    let color_mapped = profile_color_mapped.unwrap();
                    let color_copied = profile_color_copied.unwrap();
                    let depth_copied = profile_depth_copied.unwrap();
                    let outputs_published = profile_outputs_published.unwrap();
                    eprintln!("COIN_RENDER_PHASE rust_cpu_detail total_ms={:.6} validation_ms={:.6} gpu_probe_setup_ms={:.6} attachments_ms={:.6} draw_encode_ms={:.6} scene_snapshot_ms={:.6} staging_prepare_ms={:.6} submit_ms={:.6} map_request_ms={:.6} gpu_wait_ms={:.6} post_wait_checks_ms={:.6} gpu_probe_read_ms={:.6} map_receive_ms={:.6} color_copy_ms={:.6} depth_copy_ms={:.6} output_commit_ms={:.6} recycle_ms={:.6}",
                        duration_ms(profile_start, profile_done),
                        duration_ms(profile_start, profile_validated),
                        duration_ms(profile_validated, profile_probe_ready.unwrap()),
                        duration_ms(profile_probe_ready.unwrap(), attachments),
                        duration_ms(attachments, draw_encoded),
                        duration_ms(draw_encoded, profile_encoded),
                        duration_ms(profile_encoded, profile_submit_begin),
                        duration_ms(profile_submit_begin, profile_submitted),
                        duration_ms(profile_submitted, profile_wait_begin),
                        duration_ms(profile_wait_begin, profile_wait_done),
                        duration_ms(profile_wait_done, profile_gpu_read_begin.unwrap()),
                        duration_ms(profile_gpu_read_begin.unwrap(), profile_publish_begin),
                        duration_ms(profile_publish_begin, color_mapped),
                        duration_ms(color_mapped, color_copied),
                        duration_ms(color_copied, depth_copied),
                        duration_ms(depth_copied, outputs_published),
                        duration_ms(outputs_published, profile_done));
                    if let Some((render_ms, copy_ms)) = gpu_timing_ms {
                        eprintln!("COIN_RENDER_PHASE rust_gpu status={} render_ms={:.6} copy_ms={:.6} timestamp_period_ns={:.6}",
                            gpu_timing_status, render_ms, copy_ms, timestamp_period_ns);
                    } else {
                        eprintln!("COIN_RENDER_PHASE rust_gpu status={}", gpu_timing_status);
                    }
                }
                CoinWgpuStatus::Ok
            }
            Ok(Err(buf_err)) => {
                if ctx.device_id == 0 && DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst) {
                    runtime.device_state = None;
                    runtime.device_generation += 1;
                    set_error(
                        error_buf,
                        error_buf_len,
                        "Device lost during buffer mapping",
                    );
                    CoinWgpuStatus::DeviceLost
                } else {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Buffer async mapping failed: {:?}", buf_err),
                    );
                    CoinWgpuStatus::BackendError
                }
            }
            Err(recv_err) => {
                if ctx.device_id == 0 && DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst) {
                    runtime.device_state = None;
                    runtime.device_generation += 1;
                    set_error(
                        error_buf,
                        error_buf_len,
                        "Device lost: mapping channel dropped",
                    );
                    CoinWgpuStatus::DeviceLost
                } else {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Mapping channel receive failed: {:?}", recv_err),
                    );
                    CoinWgpuStatus::BackendError
                }
            }
        }
    });

    match res {
        Ok(status) => status,
        Err(_) => {
            set_error(error_buf, error_buf_len, "Rust bridge panic caught");
            CoinWgpuStatus::BackendError
        }
    }
}
