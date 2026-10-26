//! Explicit test controls and runtime reset.

use super::*;

#[no_mangle]
pub extern "C" fn coin_wgpu_inject_device_fault(id: u64, code: i32) {
    if id == 0 {
        FAULT_INJECTION.store(code, Ordering::SeqCst);
        return;
    }
    if let Ok(mut guard) = RUNTIME_CTX.lock() {
        if let Some(state) = guard.as_mut()
            .and_then(|runtime| runtime.extra_devices.get_mut(&id)) {
            state.injected_fault.store(code, Ordering::SeqCst);
        }
    }
}

#[no_mangle]
pub extern "C" fn coin_wgpu_inject_fault(fault_code: i32) {
    FAULT_INJECTION.store(fault_code, Ordering::SeqCst);
}

#[no_mangle]
pub extern "C" fn coin_wgpu_inject_fault_after_submits(fault_code: i32, skipped_submits: u32) {
    let remaining = if fault_code == 0 { -1 } else { skipped_submits.min(i32::MAX as u32) as i32 };
    FAULT_AFTER_CODE.store(fault_code, Ordering::SeqCst);
    FAULT_AFTER_SUBMITS.store(remaining, Ordering::SeqCst);
}

#[no_mangle]
pub extern "C" fn coin_wgpu_inject_async_fault(fault_code: i32) {
    FAULT_INJECTION_ASYNC.store(fault_code, Ordering::SeqCst);
}

#[no_mangle]
pub extern "C" fn coin_wgpu_reset_context() {
    let _ = std::panic::catch_unwind(|| {
        if let Ok(mut guard) = RUNTIME_CTX.lock() {
            if let Some(runtime) = guard.as_mut() {
                runtime.device_state = None;
                runtime.surfaces.clear();
                runtime.pending_readbacks.retain(|_, job| job.device_id != 0);
                runtime.retired_readbacks.retain(|job| job.device_id != 0);
                runtime.device_generation += 1;
            }
        }
        LAST_SUBMITTED_SERIAL.store(0, Ordering::SeqCst);
        GLOBAL_COMPLETED_SERIAL.store(0, Ordering::SeqCst);
        DEVICE_LOST_OCCURRED.store(false, Ordering::SeqCst);
        LAST_ASYNC_ERROR_KIND.store(0, Ordering::SeqCst);
        FAULT_SURFACE_OUTDATED_COUNT.store(0, Ordering::SeqCst);
        FAULT_SURFACE_LOST_COUNT.store(0, Ordering::SeqCst);
        if let Ok(mut lock) = LAST_ASYNC_ERROR_MSG.lock() {
            lock.clear();
        }
    });
}

#[no_mangle]
pub extern "C" fn coin_wgpu_set_cache_budget(max_bytes: u64, max_stale_serials: u64) {
    let _ = std::panic::catch_unwind(|| {
        if let Ok(guard) = RUNTIME_CTX.lock() {
            if let Some(runtime) = guard.as_ref() {
                if let Some(dev) = &runtime.device_state {
                    if let Ok(mut cache) = dev.cache.lock() {
                        cache.max_active_geometry_bytes = max_bytes;
                        cache.max_stale_serials = max_stale_serials;
                    }
                }
            }
        }
    });
}
