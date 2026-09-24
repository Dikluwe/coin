use super::{
    CoinWgpuDraw, CoinWgpuMaterial, CoinWgpuRenderState, CoinWgpuStatus, CoinWgpuTexture,
    CoinWgpuVertex,
};
use std::cmp::Ordering;

pub(super) struct CompositionItem {
    pub draw_index: usize,
    pub blend: bool,
    eye_depth: f32,
}

// Mirrors the private C++ FramePlan contract. Sort is stable, so equal-depth
// transparent objects retain traversal order. This is object/draw sorting,
// not triangle sorting and not order-independent transparency.
pub(super) fn order(
    vertices: &[CoinWgpuVertex],
    indices: &[u32],
    draws: &[CoinWgpuDraw],
    materials: &[CoinWgpuMaterial],
    states: &[CoinWgpuRenderState],
    textures: &[CoinWgpuTexture],
) -> Result<Vec<CompositionItem>, (CoinWgpuStatus, String)> {
    for (index, material) in materials.iter().enumerate() {
        let alpha = material.diffuse[3];
        let transparency = material.transparency;
        if !alpha.is_finite()
            || !transparency.is_finite()
            || !(0.0..=1.0).contains(&alpha)
            || !(0.0..=1.0).contains(&transparency)
            || (alpha + transparency - 1.0).abs() > 1.0e-5
        {
            return Err((
                CoinWgpuStatus::InvalidArgument,
                format!(
                    "Material {} has invalid or inconsistent alpha/transparency",
                    index
                ),
            ));
        }
    }
    let mut ordered = Vec::with_capacity(draws.len());
    let mut texture_alpha_cache = vec![None; textures.len()];
    for (draw_index, draw) in draws.iter().enumerate() {
        let state = states.get(draw.render_state_slot as usize).ok_or_else(|| {
            (
                CoinWgpuStatus::InvalidArgument,
                format!("Draw {} has invalid state", draw_index),
            )
        })?;
        let material = materials.get(state.material_slot as usize).ok_or_else(|| {
            (
                CoinWgpuStatus::InvalidArgument,
                format!("Draw {} has invalid material", draw_index),
            )
        })?;
        let first = draw.first_index as usize;
        let end = first
            .checked_add(draw.index_count as usize)
            .filter(|end| *end <= indices.len())
            .ok_or_else(|| {
                (
                    CoinWgpuStatus::InvalidArgument,
                    format!("Draw {} has invalid index range", draw_index),
                )
            })?;
        let alpha = material.diffuse[3];
        let mut depth_sum = 0.0_f64;
        for &vertex_index in &indices[first..end] {
            let vertex = vertices.get(vertex_index as usize).ok_or_else(|| {
                (
                    CoinWgpuStatus::InvalidArgument,
                    format!("Draw {} has invalid vertex", draw_index),
                )
            })?;
            let vertex_material =
                materials
                    .get(vertex.material_slot as usize)
                    .ok_or_else(|| {
                        (
                            CoinWgpuStatus::InvalidArgument,
                            format!("Draw {} has invalid vertex material", draw_index),
                        )
                    })?;
            if (vertex_material.diffuse[3] - alpha).abs() > 1.0e-6 {
                return Err((
                    CoinWgpuStatus::Unsupported,
                    format!("Draw {} has mixed per-vertex material alpha", draw_index),
                ));
            }
            let m = &state.model_view;
            let p = vertex.position;
            let view_z = p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14];
            if !view_z.is_finite() {
                return Err((
                    CoinWgpuStatus::InvalidArgument,
                    format!("Draw {} has non-finite eye depth", draw_index),
                ));
            }
            depth_sum += f64::from(-view_z);
        }
        let mut texture_alpha = false;
        if state.has_texture != 0 {
            let slot = state.texture_slot as usize;
            let texture = textures.get(slot).ok_or_else(|| {
                (
                    CoinWgpuStatus::InvalidArgument,
                    format!("Draw {} has invalid texture", draw_index),
                )
            })?;
            if let Some(cached) = texture_alpha_cache[slot] {
                texture_alpha = cached;
            } else {
                let expected = (texture.width as u64)
                    .checked_mul(texture.height as u64)
                    .and_then(|count| count.checked_mul(4))
                    .ok_or_else(|| {
                        (
                            CoinWgpuStatus::InvalidArgument,
                            format!("Draw {} texture size overflows", draw_index),
                        )
                    })?;
                if texture.width == 0 || texture.height == 0
                    || texture.width > 8192 || texture.height > 8192 {
                    return Err((CoinWgpuStatus::InvalidArgument,
                        format!("Draw {} texture dimensions are invalid", draw_index)));
                }
                if texture.format == 1 {
                    if texture.content_digest == 0 || !texture.pixels.is_null()
                        || texture.pixel_bytes_len != 0 {
                        return Err((CoinWgpuStatus::InvalidArgument,
                            format!("Draw {} RTT token is invalid", draw_index)));
                    }
                    texture_alpha = texture.reserved == 0; // Opaque child clear is proven by producer.
                } else if texture.format == 0 {
                    if texture.pixel_bytes_len != expected || texture.pixels.is_null() {
                        return Err((CoinWgpuStatus::InvalidArgument,
                            format!("Draw {} texture pixels are invalid", draw_index)));
                    }
                    let pixels =
                        unsafe { std::slice::from_raw_parts(texture.pixels, expected as usize) };
                    texture_alpha = pixels.chunks_exact(4).any(|pixel| pixel[3] != 255);
                } else {
                    return Err((CoinWgpuStatus::Unsupported,
                        format!("Draw {} texture format is unsupported", draw_index)));
                }
                texture_alpha_cache[slot] = Some(texture_alpha);
            }
        }
        let blend = alpha < 1.0 || texture_alpha;
        if blend && draw.topology != 0 {
            return Err((
                CoinWgpuStatus::Unsupported,
                format!("Draw {} is a transparent line or point", draw_index),
            ));
        }
        let eye_depth = if end > first {
            (depth_sum / (end - first) as f64) as f32
        } else {
            0.0
        };
        if !eye_depth.is_finite() {
            return Err((
                CoinWgpuStatus::InvalidArgument,
                format!("Draw {} has non-finite eye depth", draw_index),
            ));
        }
        ordered.push(CompositionItem {
            draw_index,
            blend,
            eye_depth,
        });
    }
    ordered.sort_by(|a, b| match (a.blend, b.blend) {
        (false, true) => Ordering::Less,
        (true, false) => Ordering::Greater,
        (true, true) => b
            .eye_depth
            .partial_cmp(&a.eye_depth)
            .unwrap_or(Ordering::Equal),
        (false, false) => Ordering::Equal,
    });
    Ok(ordered)
}
