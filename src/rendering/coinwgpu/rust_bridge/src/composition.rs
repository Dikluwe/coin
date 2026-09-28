use super::{
    CoinWgpuDraw, CoinWgpuMaterial, CoinWgpuRenderState, CoinWgpuStatus, CoinWgpuTexture,
    CoinWgpuVertex,
};
use std::cmp::Ordering;

#[derive(Clone)]
pub(super) struct CompositionItem {
    pub draw_index: usize,
    pub blend: bool,
    eye_depth: f32,
}

// Mirrors the private C++ CoinRenderFramePlan contract. Sort is stable, so equal-depth
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
        if draw.clear_depth_before > 1 || (draw.render_layer == 0 && draw.clear_depth_before != 0) {
            return Err((
                CoinWgpuStatus::InvalidArgument,
                format!("Draw {} has invalid annotation depth barrier", draw_index),
            ));
        }
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
        let mut material_alpha = alpha < 1.0;
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
            material_alpha = material_alpha || vertex_material.diffuse[3] < 1.0;
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
                if texture.width == 0
                    || texture.height == 0
                    || texture.width > 8192
                    || texture.height > 8192
                {
                    return Err((
                        CoinWgpuStatus::InvalidArgument,
                        format!("Draw {} texture dimensions are invalid", draw_index),
                    ));
                }
                if texture.format == 1 {
                    if texture.content_digest == 0
                        || !texture.pixels.is_null()
                        || texture.pixel_bytes_len != 0
                    {
                        return Err((
                            CoinWgpuStatus::InvalidArgument,
                            format!("Draw {} RTT token is invalid", draw_index),
                        ));
                    }
                    texture_alpha = texture.reserved == 0; // Opaque child clear is proven by producer.
                } else if texture.format == 0 {
                    if texture.pixel_bytes_len != expected || texture.pixels.is_null() {
                        return Err((
                            CoinWgpuStatus::InvalidArgument,
                            format!("Draw {} texture pixels are invalid", draw_index),
                        ));
                    }
                    let pixels =
                        unsafe { std::slice::from_raw_parts(texture.pixels, expected as usize) };
                    texture_alpha = pixels.chunks_exact(4).any(|pixel| pixel[3] != 255);
                } else {
                    return Err((
                        CoinWgpuStatus::Unsupported,
                        format!("Draw {} texture format is unsupported", draw_index),
                    ));
                }
                texture_alpha_cache[slot] = Some(texture_alpha);
            }
        }
        if state.texture_model == 2 {
            texture_alpha = false;
        }
        if state.has_texture != 0 && state.texture_model == 1 {
            material_alpha = false;
        }
        let blend = material_alpha || texture_alpha;
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
    ordered.sort_by(|a, b| {
        let layer_a = draws[a.draw_index].render_layer;
        let layer_b = draws[b.draw_index].render_layer;
        if layer_a != layer_b {
            return layer_a.cmp(&layer_b);
        }
        if layer_a != 0 {
            return Ordering::Equal;
        }
        match (a.blend, b.blend) {
            (false, true) => Ordering::Less,
            (true, false) => Ordering::Greater,
            (true, true) => b
                .eye_depth
                .partial_cmp(&a.eye_depth)
                .unwrap_or(Ordering::Equal),
            (false, false) => Ordering::Equal,
        }
    });
    Ok(ordered)
}

// Pass boundaries preserve overlay traversal and depth barriers. The base still
// uses the existing opaque/transparent profile; annotation blending is per draw.
pub(super) fn passes(
    order: &[CompositionItem],
    draws: &[CoinWgpuDraw],
) -> Vec<std::ops::Range<usize>> {
    let mut passes = Vec::new();
    let mut start = 0;
    for i in 1..order.len() {
        let previous = &draws[order[i - 1].draw_index];
        let current = &draws[order[i].draw_index];
        if previous.render_layer != current.render_layer
            || current.clear_depth_before != 0
            || (current.render_layer == 0 && order[i - 1].blend != order[i].blend)
        {
            passes.push(start..i);
            start = i;
        }
    }
    // Even an empty frame must clear the attachments.
    passes.push(start..order.len());
    passes
}

#[cfg(test)]
mod tests {
    use super::*;
    fn fixture(
        layers: &[u32],
        alphas: &[f32],
    ) -> (
        Vec<CoinWgpuDraw>,
        Vec<CoinWgpuMaterial>,
        Vec<CoinWgpuRenderState>,
    ) {
        let draws = layers
            .iter()
            .enumerate()
            .map(|(i, &layer)| CoinWgpuDraw {
                render_state_slot: i as u32,
                render_layer: layer,
                ..unsafe { std::mem::zeroed() }
            })
            .collect();
        let materials = alphas
            .iter()
            .map(|&alpha| CoinWgpuMaterial {
                diffuse: [1.0, 1.0, 1.0, alpha],
                transparency: 1.0 - alpha,
                ..unsafe { std::mem::zeroed() }
            })
            .collect();
        let states = (0..layers.len())
            .map(|i| CoinWgpuRenderState {
                material_slot: i as u32,
                ..unsafe { std::mem::zeroed() }
            })
            .collect();
        (draws, materials, states)
    }
    #[test]
    fn base_sorts_but_overlays_preserve_mixed_traversal() {
        let (draws, materials, states) =
            fixture(&[2, 0, 1, 0, 1, 2], &[0.5, 0.5, 0.5, 1.0, 1.0, 1.0]);
        let order = order(&[], &[], &draws, &materials, &states, &[]).unwrap();
        assert_eq!(
            order.iter().map(|item| item.draw_index).collect::<Vec<_>>(),
            vec![3, 1, 2, 4, 0, 5]
        );
        assert_eq!(passes(&order, &draws), vec![0..1, 1..2, 2..4, 4..6]);
    }
    #[test]
    fn depth_barrier_splits_even_within_one_overlay_layer() {
        let (mut draws, materials, states) = fixture(&[1, 1, 1], &[1.0, 0.5, 1.0]);
        draws[1].clear_depth_before = 1;
        let order = order(&[], &[], &draws, &materials, &states, &[]).unwrap();
        assert_eq!(passes(&order, &draws), vec![0..1, 1..3]);
    }
    #[test]
    fn rejects_invalid_barriers_and_clears_empty_frame() {
        let (mut draws, materials, states) = fixture(&[0], &[1.0]);
        draws[0].clear_depth_before = 1;
        assert!(order(&[], &[], &draws, &materials, &states, &[]).is_err());
        draws[0].render_layer = 1;
        draws[0].clear_depth_before = 2;
        assert!(order(&[], &[], &draws, &materials, &states, &[]).is_err());
        assert_eq!(passes(&[], &[]), vec![0..0]);
    }
}
