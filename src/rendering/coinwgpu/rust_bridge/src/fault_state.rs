//! Injection reads collapse to zero in production; mutable test state and all
//! setters are compiled only for unit tests or the explicit test-hooks feature.

use super::*;

pub(super) fn current() -> i32 {
    #[cfg(any(test, feature = "test-hooks"))]
    { FAULT_INJECTION.load(Ordering::SeqCst) }
    #[cfg(not(any(test, feature = "test-hooks")))]
    { 0 }
}

pub(super) fn clear() {
    #[cfg(any(test, feature = "test-hooks"))]
    FAULT_INJECTION.store(0, Ordering::SeqCst);
}

pub(super) fn take_async() -> i32 {
    #[cfg(any(test, feature = "test-hooks"))]
    { FAULT_INJECTION_ASYNC.swap(0, Ordering::SeqCst) }
    #[cfg(not(any(test, feature = "test-hooks")))]
    { 0 }
}

pub(super) fn first_outdated() -> bool {
    #[cfg(any(test, feature = "test-hooks"))]
    { FAULT_SURFACE_OUTDATED_COUNT.fetch_add(1, Ordering::SeqCst) == 0 }
    #[cfg(not(any(test, feature = "test-hooks")))]
    { false }
}

pub(super) fn first_lost() -> bool {
    #[cfg(any(test, feature = "test-hooks"))]
    { FAULT_SURFACE_LOST_COUNT.fetch_add(1, Ordering::SeqCst) == 0 }
    #[cfg(not(any(test, feature = "test-hooks")))]
    { false }
}

pub(super) fn take_device(_device: &DeviceState) -> i32 {
    #[cfg(any(test, feature = "test-hooks"))]
    { _device.injected_fault.swap(0, Ordering::SeqCst) }
    #[cfg(not(any(test, feature = "test-hooks")))]
    { 0 }
}

pub(super) fn submission(_requested_device: CoinWgpuDeviceId) -> i32 {
    #[cfg(any(test, feature = "test-hooks"))]
    {
        let delayed = _requested_device == 0 && FAULT_AFTER_SUBMITS.fetch_update(
            Ordering::SeqCst, Ordering::SeqCst,
            |remaining| if remaining >= 0 { Some(remaining - 1) } else { None },
        ).ok() == Some(0);
        if _requested_device != 0 { 0 } else if delayed {
            FAULT_AFTER_CODE.swap(0, Ordering::SeqCst)
        } else {
            current()
        }
    }
    #[cfg(not(any(test, feature = "test-hooks")))]
    { 0 }
}
