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
mod tests {
    use super::*;
    #[test]
    fn npot_complete_chain() {
        assert_eq!(
            levels(3, 5, 2).unwrap(),
            vec![(3, 5, 0), (1, 2, 60), (1, 1, 68)]
        );
        assert_eq!(
            levels(4, 2, 2).unwrap(),
            vec![(4, 2, 0), (2, 1, 32), (1, 1, 40)]
        );
    }
    #[test]
    fn format_layouts_and_alignment() {
        assert_eq!(
            levels(4, 4, 8).unwrap(),
            vec![(4, 4, 0), (2, 2, 16), (1, 1, 32)]
        );
        assert_eq!(levels(1, 1, 5).unwrap(), vec![(1, 1, 0)]);
        assert_eq!(level_bytes(1, 1, wgpu::TextureFormat::Rgba16Float), 8);
        assert!(levels(3, 4, 7).is_err());
        assert!(levels(0, 1, 0).is_err());
        assert!(levels(8192, 8192, 5).is_err());
        assert!(format(11).is_err());
    }
    #[test]
    fn anisotropy_is_explicit_and_validated() {
        for n in [1, 2, 4, 8, 16] {
            assert_eq!(anisotropy(n - 1, 3).unwrap(), n as u16);
        }
        for n in [0u32, 3, 32] {
            assert!(anisotropy(n.wrapping_sub(1), 3).is_err());
        }
        assert!(anisotropy(15, 2).is_err());
    }
}
