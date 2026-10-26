//! Readback tickets, cache statistics and device polling.

use super::*;

#[no_mangle]
pub extern "C" fn coin_wgpu_readback_resource_load(jobs: *mut u64, bytes: *mut u64) -> i32 {
    if jobs.is_null() || bytes.is_null() || (jobs as usize) % 8 != 0 || (bytes as usize) % 8 != 0 {
        return 0;
    }
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let mut guard = match RUNTIME_CTX.lock() {
            Ok(value) => value,
            Err(_) => return 0,
        };
        let (count, payload) = if let Some(runtime) = guard.as_mut() {
            reap_cancelled_readbacks(runtime);
            let count = runtime.pending_readbacks.len() + runtime.retired_readbacks.len();
            let payload = runtime
                .pending_readbacks
                .values()
                .chain(runtime.retired_readbacks.iter())
                .map(|job| job.ticket.color_bytes + job.ticket.depth_bytes)
                .sum::<u64>();
            (count as u64, payload)
        } else {
            (0, 0)
        };
        unsafe {
            *jobs = count;
            *bytes = payload;
        }
        1
    }))
    .unwrap_or(0)
}

// Physical staging remains a separate charge for the multipass resource
// budget; the public queue limit above counts packed ticket payloads.
#[no_mangle]
pub extern "C" fn coin_wgpu_readback_staging_bytes() -> u64 {
    std::panic::catch_unwind(|| {
        let mut guard = RUNTIME_CTX.lock().ok()?;
        if let Some(runtime) = guard.as_mut() {
            reap_cancelled_readbacks(runtime);
        }
        multipass_pending_bytes(guard.as_ref())
    }).ok().flatten().unwrap_or(u64::MAX)
}

pub(super) fn rtt_remaining_capacity(active: usize) -> u32 {
    64usize.saturating_sub(active) as u32
}

// Captured C++ actions use the default device. Other explicit devices have
// independent capacities, and a lost default device will be reconstructed.
#[no_mangle]
pub extern "C" fn coin_wgpu_default_rtt_capacity() -> u32 {
    std::panic::catch_unwind(|| {
        if DEVICE_LOST_OCCURRED.load(Ordering::Acquire) {
            return 64;
        }
        let Ok(guard) = RUNTIME_CTX.lock() else {
            return 0;
        };
        let Some(ctx) = guard
            .as_ref()
            .and_then(|runtime| runtime.device_state.as_ref())
        else {
            return 64;
        };
        let Ok(textures) = ctx.rtt_textures.lock() else {
            return 0;
        };
        rtt_remaining_capacity(textures.active.len())
    })
    .unwrap_or(0)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_rtt_resource_counts(active: *mut u64, retired: *mut u64) {
    if (!active.is_null() && (active as usize) % std::mem::align_of::<u64>() != 0)
        || (!retired.is_null() && (retired as usize) % std::mem::align_of::<u64>() != 0) {
        return;
    }
    let (a, r) = std::panic::catch_unwind(|| {
        let guard = RUNTIME_CTX.lock().ok()?;
        let runtime = guard.as_ref()?;
        let mut counts = (0, 0);
        for ctx in runtime.device_state.iter().chain(runtime.extra_devices.values()) {
            let textures = ctx.rtt_textures.lock().ok()?;
            counts.0 += textures.active.len() as u64;
            counts.1 += textures.retired.len() as u64;
        }
        Some(counts)
    }).ok().flatten().unwrap_or((0, 0));
    if !active.is_null() { unsafe { *active = a; } }
    if !retired.is_null() { unsafe { *retired = r; } }
}

#[no_mangle]
pub extern "C" fn coin_wgpu_submit_async(
    target: *mut CoinWgpuTarget,
    frame: *const CoinWgpuFrameView,
    out_ticket: *mut CoinWgpuReadbackTicket,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    if out_ticket.is_null() {
        set_error(error_buf, error_buf_len, "Null async readback ticket pointer");
        return CoinWgpuStatus::InvalidArgument;
    }
    coin_wgpu_submit_internal(target, frame, out_ticket, std::ptr::null_mut(), std::ptr::null_mut(), error_buf, error_buf_len)
}

pub(super) fn check_readback_ready(
    runtime: &mut RuntimeContext,
    token: u64,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    if token == 0 {
        set_error(error_buf, error_buf_len, "Invalid readback token");
        return CoinWgpuStatus::InvalidArgument;
    }
    reap_cancelled_readbacks(runtime);
    if let Some((_, status)) = runtime.dead_readbacks.remove(&token) {
        set_error(error_buf, error_buf_len, "Readback device was destroyed or lost");
        return status;
    }
    if !runtime.pending_readbacks.contains_key(&token) {
        set_error(error_buf, error_buf_len, "Unknown or cancelled readback token");
        return CoinWgpuStatus::InvalidArgument;
    }
    let owner = runtime.pending_readbacks.get(&token).unwrap().device_id;
    let injected_fault = if owner == 0 { fault_state::take_async() }
        else { 0 };
    if injected_fault == CoinWgpuStatus::BackendError as i32 {
        if let Some(job) = runtime.pending_readbacks.remove(&token) {
            runtime.retired_readbacks.push(job);
        }
        set_error(error_buf, error_buf_len, "Injected asynchronous map failure");
        return CoinWgpuStatus::BackendError;
    }
    if injected_fault == CoinWgpuStatus::DeviceLost as i32
        || (owner == 0 && DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst)) {
        runtime.pending_readbacks.retain(|_, job| job.device_id != 0);
        runtime.retired_readbacks.retain(|job| job.device_id != 0);
        runtime.device_state = None;
        runtime.device_generation += 1;
        set_error(error_buf, error_buf_len, "Device lost during asynchronous readback");
        return CoinWgpuStatus::DeviceLost;
    }
    if let Some(device) = runtime.device_state.as_ref() {
        let _ = device.device.poll(wgpu::Maintain::Poll);
    }
    let generation = if owner == 0 {
        runtime.device_generation
    } else {
        let Some(device) = runtime.extra_devices.get(&owner) else {
            runtime.pending_readbacks.remove(&token);
            set_error(error_buf, error_buf_len, "Readback device was destroyed");
            return CoinWgpuStatus::DeviceLost;
        };
        if device.lost.load(Ordering::SeqCst) {
            lose_extra_device(runtime, owner);
            set_error(error_buf, error_buf_len, "Readback device was lost");
            return CoinWgpuStatus::DeviceLost;
        }
        let _ = device.device.poll(wgpu::Maintain::Poll);
        device.generation
    };
    let job = runtime.pending_readbacks.get_mut(&token).unwrap();
    if job.ticket.generation != generation {
        runtime.pending_readbacks.remove(&token);
        set_error(error_buf, error_buf_len, "Readback generation is stale");
        return CoinWgpuStatus::DeviceLost;
    }
    update_readback_mapping(job);
    if job.color_ready.is_none() || job.depth_ready.is_none() {
        return CoinWgpuStatus::NotReady;
    }
    if let Some(message) = job.color_ready.as_ref().and_then(|result| result.as_ref().err())
        .or_else(|| job.depth_ready.as_ref().and_then(|result| result.as_ref().err())) {
        let message = message.clone();
        if let Some(failed) = runtime.pending_readbacks.remove(&token) {
            if matches!(failed.color_ready.as_ref(), Some(Ok(()))) {
                failed.color.unmap();
            }
            if matches!(failed.depth_ready.as_ref(), Some(Ok(()))) {
                if let Some(buffer) = failed.depth.as_ref() {
                    buffer.unmap();
                }
            }
        }
        set_error(error_buf, error_buf_len, &format!("Readback mapping failed: {}", message));
        return CoinWgpuStatus::BackendError;
    }
    CoinWgpuStatus::Ok
}

// Read-only identity admission precedes query/poll/cancel in the public C++
// runtime. A forged descriptor must not advance mapping, consume injected
// errors, cancel staging, or remove a device-loss tombstone.
#[no_mangle]
pub extern "C" fn coin_wgpu_readback_validate(
    ticket: *const CoinWgpuReadbackTicket,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if ticket.is_null() || (ticket as usize) % std::mem::align_of::<CoinWgpuReadbackTicket>() != 0 {
            set_error(error_buf, error_buf_len, "Missing or misaligned readback ticket");
            return CoinWgpuStatus::InvalidArgument;
        }
        let supplied = unsafe { &*ticket };
        let guard = match RUNTIME_CTX.lock() {
            Ok(guard) => guard,
            Err(_) => return CoinWgpuStatus::BackendError,
        };
        let stored = guard.as_ref().and_then(|runtime|
            runtime.pending_readbacks.get(&supplied.token).map(|job| &job.ticket)
                .or_else(|| runtime.dead_readbacks.get(&supplied.token).map(|(ticket, _)| ticket)));
        if supplied.token == 0 || stored != Some(supplied) {
            set_error(error_buf, error_buf_len, "Invalid or consumed asynchronous readback ticket identity");
            return CoinWgpuStatus::InvalidArgument;
        }
        CoinWgpuStatus::Ok
    }).unwrap_or_else(|_| {
        set_error(error_buf, error_buf_len, "Panic during readback identity validation");
        CoinWgpuStatus::BackendError
    })
}

#[no_mangle]
pub extern "C" fn coin_wgpu_readback_query(
    token: u64,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        let mut guard = match RUNTIME_CTX.lock() {
            Ok(guard) => guard,
            Err(_) => return CoinWgpuStatus::BackendError,
        };
        let runtime = match guard.as_mut() {
            Some(runtime) => runtime,
            None => return CoinWgpuStatus::InvalidArgument,
        };
        check_readback_ready(runtime, token, error_buf, error_buf_len)
    }).unwrap_or_else(|_| {
        set_error(error_buf, error_buf_len, "Panic during asynchronous readback query");
        CoinWgpuStatus::BackendError
    })
}

#[no_mangle]
pub extern "C" fn coin_wgpu_readback_poll(
    token: u64,
    color_buffer: *mut u8,
    color_buffer_len: u64,
    depth_buffer: *mut f32,
    depth_buffer_len: u64,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        let mut guard = match RUNTIME_CTX.lock() {
            Ok(guard) => guard,
            Err(_) => return CoinWgpuStatus::BackendError,
        };
        let runtime = match guard.as_mut() {
            Some(runtime) => runtime,
            None => return CoinWgpuStatus::InvalidArgument,
        };
        let readiness = check_readback_ready(runtime, token, error_buf, error_buf_len);
        if readiness != CoinWgpuStatus::Ok {
            return readiness;
        }
        let ticket = runtime.pending_readbacks.get(&token).unwrap().ticket;
        if color_buffer.is_null() || color_buffer_len < ticket.color_bytes
            || (ticket.depth_bytes != 0 && (depth_buffer.is_null()
                || depth_buffer_len < ticket.depth_bytes / 4
                || (depth_buffer as usize) % std::mem::align_of::<f32>() != 0)) {
            set_error(error_buf, error_buf_len, "Readback output buffers are missing or too small");
            return CoinWgpuStatus::InvalidArgument;
        }
        if ticket.depth_bytes != 0 {
            let color_start = color_buffer as usize;
            let depth_start = depth_buffer as usize;
            let color_end = color_start.checked_add(ticket.color_bytes as usize);
            let depth_end = depth_start.checked_add(ticket.depth_bytes as usize);
            if color_end.is_none() || depth_end.is_none()
                || (color_start < depth_end.unwrap() && depth_start < color_end.unwrap()) {
                set_error(error_buf, error_buf_len, "Readback output buffers overlap or overflow");
                return CoinWgpuStatus::InvalidArgument;
            }
        }
        let color_output = match validate_slice_mut(color_buffer, ticket.color_bytes,
            "readback color buffer", error_buf, error_buf_len) {
            Ok(output) => output,
            Err(status) => return status,
        };
        let mut depth_output = None;
        if ticket.depth_bytes != 0 {
            depth_output = Some(match validate_slice_mut(depth_buffer, ticket.depth_bytes / 4,
                "readback depth buffer", error_buf, error_buf_len) {
                Ok(output) => output,
                Err(status) => return status,
            });
        }
        let job = runtime.pending_readbacks.remove(&token).unwrap();
        let color_view = job.color.slice(..).get_mapped_range();
        if (ticket.color_row_pitch as u64) * (ticket.height as u64) > color_view.len() as u64 {
            set_error(error_buf, error_buf_len, "Mapped color readback is truncated");
            return CoinWgpuStatus::BackendError;
        }
        let mut color = job.depth.as_ref().map(|_| vec![0u8; ticket.color_bytes as usize]);
        if let Some(ref mut pending) = color {
            copy_color_rows(pending, &color_view, ticket.width, ticket.height,
                ticket.color_row_pitch);
        } else {
            copy_color_rows(color_output, &color_view, ticket.width, ticket.height,
                ticket.color_row_pitch);
        }
        drop(color_view);
        job.color.unmap();
        if let Ok(mut pool) = job.pool.lock() {
            pool.recycle(job.color.clone());
        }

        let mut depth = None;
        if let Some(buffer) = job.depth.as_ref() {
            let depth_view = buffer.slice(..).get_mapped_range();
            let mapped: &[f32] = bytemuck::cast_slice(&depth_view);
            let row_floats = ticket.width as usize;
            let pitch_floats = ticket.depth_row_pitch as usize / 4;
            let mut pixels = vec![1.0f32; (ticket.depth_bytes / 4) as usize];
            for y in 0..ticket.height as usize {
                let source = y * pitch_floats;
                let destination = y * row_floats;
                if source + row_floats > mapped.len() {
                    set_error(error_buf, error_buf_len, "Mapped depth readback is truncated");
                    return CoinWgpuStatus::BackendError;
                }
                pixels[destination..destination + row_floats]
                    .copy_from_slice(&mapped[source..source + row_floats]);
            }
            drop(depth_view);
            buffer.unmap();
            if let Ok(mut pool) = job.pool.lock() {
                pool.recycle(buffer.clone());
            }
            depth = Some(pixels);
        }

        if let Some(color) = color {
            color_output.copy_from_slice(&color);
        }
        if let (Some(pixels), Some(output)) = (depth, depth_output) {
            output.copy_from_slice(&pixels);
        }
        CoinWgpuStatus::Ok
    }).unwrap_or_else(|_| {
        set_error(error_buf, error_buf_len, "Panic during asynchronous readback");
        CoinWgpuStatus::BackendError
    })
}

#[no_mangle]
pub extern "C" fn coin_wgpu_readback_cancel(token: u64) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        let mut guard = match RUNTIME_CTX.lock() {
            Ok(guard) => guard,
            Err(_) => return CoinWgpuStatus::BackendError,
        };
        if let Some(runtime) = guard.as_mut() {
            if let Some(job) = runtime.pending_readbacks.remove(&token) {
                runtime.retired_readbacks.push(job);
                reap_cancelled_readbacks(runtime);
                return CoinWgpuStatus::Ok;
            }
        }
        CoinWgpuStatus::InvalidArgument
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_get_cache_stats(stats: *mut CoinWgpuCacheStats) {
    let _ = std::panic::catch_unwind(|| {
        if stats.is_null() {
            return;
        }
        let mut out = CoinWgpuCacheStats::default();
        if let Ok(guard) = RUNTIME_CTX.lock() {
            if let Some(runtime) = guard.as_ref() {
                if let Some(dev) = &runtime.device_state {
                    if let Ok(cache) = dev.cache.lock() {
                        out.cumulative_uploads = cache.cumulative_uploads;
                        out.cumulative_hits = cache.cumulative_hits;
                        out.cumulative_misses = cache.cumulative_misses;
                        out.cumulative_uploaded_bytes = cache.cumulative_uploaded_bytes;
                        out.frame_uploaded_bytes = cache.frame_uploaded_bytes;
                        out.frame_uploads = cache.frame_uploads;
                        out.frame_hits = cache.frame_hits;
                        out.active_entries = cache.active_entries.len() as u64;
                        out.retired_entries = cache.deferred_release.len() as u64;
                    }
                }
            }
        }
        out.completed_serial = GLOBAL_COMPLETED_SERIAL.load(Ordering::SeqCst);
        out.submission_serial = LAST_SUBMITTED_SERIAL.load(Ordering::SeqCst);
        unsafe {
            *stats = out;
        }
    });
}

#[no_mangle]
pub extern "C" fn coin_wgpu_get_performance_stats(stats: *mut CoinWgpuPerformanceStats) {
    let _ = std::panic::catch_unwind(|| {
        if stats.is_null() {
            return;
        }
        let mut out = CoinWgpuPerformanceStats::default();
        if let Ok(guard) = RUNTIME_CTX.lock() {
            if let Some(dev) = guard.as_ref().and_then(|runtime| runtime.device_state.as_ref()) {
                if let Ok(cache) = dev.texture_cache.lock() {
                    out.texture_uploads = cache.uploads;
                    out.texture_hits = cache.hits;
                    out.texture_uploaded_bytes = cache.uploaded_bytes;
                    out.texture_evictions = cache.evictions;
                    out.texture_active_entries = cache.entries.len() as u64;
                    out.texture_retired_entries = cache.retired.len() as u64;
                }
                if let Ok(pipelines) = dev.surface_native.pipelines.lock() {
                    out.pipeline_compilations = dev.pipeline_compilations.load(Ordering::Relaxed);
                    out.pipeline_active_entries = pipelines.len() as u64;
                }
                if let Some(profile) = dev.surface_portable.get() {
                    if let Ok(pipelines) = profile.pipelines.lock() {
                        out.pipeline_active_entries += pipelines.len() as u64;
                    }
                }
                out.pipeline_hits = dev.pipeline_hits.load(Ordering::Relaxed);
            }
        }
        unsafe {
            *stats = out;
        }
    });
}

#[no_mangle]
pub extern "C" fn coin_wgpu_poll_device() {
    let _ = std::panic::catch_unwind(|| {
        if let Ok(mut guard) = RUNTIME_CTX.lock() {
            if let Some(runtime) = guard.as_mut() {
                reap_cancelled_readbacks(runtime);
                if let Some(dev) = &runtime.device_state {
                    let _ = dev.device.poll(wgpu::Maintain::Poll);
                    let completed = dev.completed_serial.load(Ordering::SeqCst);
                    if let Ok(mut textures) = dev.texture_cache.lock() {
                        textures.retired.retain(|entry| entry.retired_at_serial > completed);
                    }
                    if let Ok(mut textures) = dev.rtt_textures.lock() {
                        textures.retired.retain(|entry| entry.retired_at_serial > completed);
                    }
                }
                for dev in runtime.extra_devices.values() {
                    let _ = dev.device.poll(wgpu::Maintain::Poll);
                    let completed = dev.completed_serial.load(Ordering::SeqCst);
                    if let Ok(mut cache) = dev.cache.lock() {
                        cache.deferred_release.retain(|entry| entry.retired_at_serial > completed);
                    }
                    if let Ok(mut textures) = dev.texture_cache.lock() {
                        textures.retired.retain(|entry| entry.retired_at_serial > completed);
                    }
                    if let Ok(mut textures) = dev.rtt_textures.lock() {
                        textures.retired.retain(|entry| entry.retired_at_serial > completed);
                    }
                }
            }
        }
    });
}

#[no_mangle]
pub extern "C" fn coin_wgpu_trim_cache() {
    let _ = std::panic::catch_unwind(|| {
        if let Ok(guard) = RUNTIME_CTX.lock() {
            if let Some(runtime) = guard.as_ref() {
                if let Some(dev) = &runtime.device_state {
                    if let Ok(mut cache) = dev.cache.lock() {
                        let last_serial = dev.last_submitted_serial.load(Ordering::SeqCst);
                        let mut to_remove = Vec::new();
                        for (&key, (_rev, entry)) in &cache.active_entries {
                            if entry.last_submitted_serial < last_serial {
                                to_remove.push(key);
                            }
                        }
                        for key in to_remove {
                            if let Some((_rev, entry)) = cache.active_entries.remove(&key) {
                                cache.total_active_geometry_bytes = cache
                                    .total_active_geometry_bytes
                                    .saturating_sub(entry.size_bytes);
                                cache.cumulative_evictions += 1;
                                cache.deferred_release.push(RetiredBuffer {
                                    vertex_buffer: entry.vertex_buffer,
                                    index_buffer: entry.index_buffer,
                                    retired_at_serial: entry.last_submitted_serial,
                                    size_bytes: entry.size_bytes,
                                });
                            }
                        }
                    }
                }
            }
        }
    });
}
