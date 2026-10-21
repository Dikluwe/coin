//! Private transport and GPU format facts, no Coin traversal or semantic policy.
use crate::CoinWgpuStatus;
pub fn format(code: u32) -> Result<(wgpu::TextureFormat, bool), (CoinWgpuStatus, String)> {
    use wgpu::TextureFormat as F;
    let value = match code {
        0 => (F::Rgba8Unorm, false),
        2 => (F::Rgba8Unorm, true),
        3 => (F::Rgba8UnormSrgb, false),
        4 => (F::Rgba8UnormSrgb, true),
        5 => (F::Rgba16Float, false),
        6 => (F::Rgba16Float, true),
        7 => (F::Bc3RgbaUnorm, false),
        8 => (F::Bc3RgbaUnorm, true),
        9 => (F::Bc3RgbaUnormSrgb, false),
        10 => (F::Bc3RgbaUnormSrgb, true),
        12 => (F::Rgba8Unorm, false), // Six cube faces and their face-local mip chain.
        11 => (F::Rgba8Unorm, false), // Explicitly packed 3D atlas; no hardware 2D mips.
        _ => {
            return Err((
                CoinWgpuStatus::InvalidArgument,
                "Invalid advanced texture format".into(),
            ))
        }
    };
    Ok(value)
}
pub fn level_bytes(w: u32, h: u32, f: wgpu::TextureFormat) -> u64 {
    let (bw, bh) = f.block_dimensions();
    u64::from(w.div_ceil(bw))
        * u64::from(h.div_ceil(bh))
        * u64::from(f.block_copy_size(None).unwrap())
}
pub fn levels(w: u32, h: u32, code: u32) -> Result<Vec<(u32, u32, u64)>, (CoinWgpuStatus, String)> {
    let (f, mips) = format(code)?;
    if w == 0 || h == 0 || w > 8192 || h > 8192 || (f.is_compressed() && (w % 4 != 0 || h % 4 != 0))
    {
        return Err((
            CoinWgpuStatus::InvalidArgument,
            "Invalid advanced texture extent".into(),
        ));
    }
    let (mut w, mut h, mut offset) = (w, h, 0u64);
    let mut result = Vec::new();
    loop {
        result.push((w, h, offset));
        offset += level_bytes(w, h, f);
        if !mips || (w == 1 && h == 1) {
            break;
        }
        w = (w / 2).max(1);
        h = (h / 2).max(1);
    }
    if offset > 128 * 1024 * 1024 {
        return Err((
            CoinWgpuStatus::Unsupported,
            "Texture chain exceeds 128 MiB".into(),
        ));
    }
    Ok(result)
}
pub fn anisotropy(reserved: u32, filter: u32) -> Result<u16, (CoinWgpuStatus, String)> {
    let value = reserved.checked_add(1).unwrap_or(0);
    if !value.is_power_of_two() || value > 16 || (value > 1 && filter != 3) {
        return Err((
            CoinWgpuStatus::InvalidArgument,
            "Anisotropy requires 1..16 and linear mip filters".into(),
        ));
    }
    Ok(value as u16)
}
#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/texture_profile_tests.rs"]
mod tests;
