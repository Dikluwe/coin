//! FFI slice validation, error reporting and readback row copies.

use super::*;

pub(super) fn validate_slice<'a, T>(
    ptr: *const T,
    count: u64,
    name: &str,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> Result<&'a [T], CoinWgpuStatus> {
    if count == 0 {
        return Ok(&[]);
    }
    if ptr.is_null() {
        set_error(
            error_buf,
            error_buf_len,
            &format!("Null {} pointer with non-zero count", name),
        );
        return Err(CoinWgpuStatus::InvalidArgument);
    }
    let align = std::mem::align_of::<T>();
    if (ptr as usize) % align != 0 {
        set_error(
            error_buf,
            error_buf_len,
            &format!(
                "Misaligned {} pointer: {:#x} not aligned to {}",
                name, ptr as usize, align
            ),
        );
        return Err(CoinWgpuStatus::InvalidArgument);
    }
    let usize_count = match usize::try_from(count) {
        Ok(c) => c,
        Err(_) => {
            set_error(
                error_buf,
                error_buf_len,
                &format!("{} count {} exceeds usize range", name, count),
            );
            return Err(CoinWgpuStatus::InvalidArgument);
        }
    };
    let total_bytes = match usize_count.checked_mul(std::mem::size_of::<T>()) {
        Some(b) => b,
        None => {
            set_error(
                error_buf,
                error_buf_len,
                &format!("{} byte size overflow", name),
            );
            return Err(CoinWgpuStatus::InvalidArgument);
        }
    };
    if total_bytes > (isize::MAX as usize) {
        set_error(
            error_buf,
            error_buf_len,
            &format!(
                "{} total byte size {} exceeds isize::MAX",
                name, total_bytes
            ),
        );
        return Err(CoinWgpuStatus::InvalidArgument);
    }
    Ok(unsafe { std::slice::from_raw_parts(ptr, usize_count) })
}

pub(super) fn validate_slice_mut<'a, T>(
    ptr: *mut T,
    count: u64,
    name: &str,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> Result<&'a mut [T], CoinWgpuStatus> {
    if count == 0 {
        return Ok(&mut []);
    }
    if ptr.is_null() {
        set_error(
            error_buf,
            error_buf_len,
            &format!("Null {} pointer with non-zero count", name),
        );
        return Err(CoinWgpuStatus::InvalidArgument);
    }
    let align = std::mem::align_of::<T>();
    if (ptr as usize) % align != 0 {
        set_error(
            error_buf,
            error_buf_len,
            &format!(
                "Misaligned {} pointer: {:#x} not aligned to {}",
                name, ptr as usize, align
            ),
        );
        return Err(CoinWgpuStatus::InvalidArgument);
    }
    let usize_count = match usize::try_from(count) {
        Ok(c) => c,
        Err(_) => {
            set_error(
                error_buf,
                error_buf_len,
                &format!("{} count {} exceeds usize range", name, count),
            );
            return Err(CoinWgpuStatus::InvalidArgument);
        }
    };
    let total_bytes = match usize_count.checked_mul(std::mem::size_of::<T>()) {
        Some(b) => b,
        None => {
            set_error(
                error_buf,
                error_buf_len,
                &format!("{} byte size overflow", name),
            );
            return Err(CoinWgpuStatus::InvalidArgument);
        }
    };
    if total_bytes > (isize::MAX as usize) {
        set_error(
            error_buf,
            error_buf_len,
            &format!(
                "{} total byte size {} exceeds isize::MAX",
                name, total_bytes
            ),
        );
        return Err(CoinWgpuStatus::InvalidArgument);
    }
    Ok(unsafe { std::slice::from_raw_parts_mut(ptr, usize_count) })
}

// Callers validate the mapped extent and output length first. When the WebGPU
// row pitch has no padding, one contiguous copy avoids per-row slice work.
pub(super) fn copy_color_rows(dst: &mut [u8], src: &[u8], width: u32, height: u32, pitch: u32) {
    let row_bytes = width as usize * 4;
    let rows = height as usize;
    if pitch as usize == row_bytes &&
        std::env::var("COIN_WGPU_FORCE_ROW_COPY").as_deref() != Ok("1") {
        let bytes = row_bytes * rows;
        dst[..bytes].copy_from_slice(&src[..bytes]);
    } else {
        for y in 0..rows {
            let source = y * pitch as usize;
            let destination = y * row_bytes;
            dst[destination..destination + row_bytes]
                .copy_from_slice(&src[source..source + row_bytes]);
        }
    }
}

pub(super) fn set_error(buf: *mut std::os::raw::c_char, buf_len: usize, msg: &str) {
    if !buf.is_null() && buf_len > 0 {
        let bytes = msg.as_bytes();
        let copy_len = bytes.len().min(buf_len - 1);
        unsafe {
            std::ptr::copy_nonoverlapping(
                bytes.as_ptr() as *const std::os::raw::c_char,
                buf,
                copy_len,
            );
            *buf.add(copy_len) = 0;
        }
    }
}
