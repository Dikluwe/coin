use super::{CoinWgpuDraw, CoinWgpuMaterial, CoinWgpuRenderState, CoinWgpuStatus,
    CoinWgpuTexture, CoinWgpuVertex};

#[derive(Clone)]
pub(super) struct CompositionItem {
    pub draw_index: usize,
    pub blend: bool,
}

// CoinRender has already resolved ordering, alpha classification and depth.
// Infra validates the private transport and executes its sequence unchanged.
pub(super) fn order(
    vertices: &[CoinWgpuVertex], indices: &[u32], draws: &[CoinWgpuDraw],
    materials: &[CoinWgpuMaterial], states: &[CoinWgpuRenderState],
    _textures: &[CoinWgpuTexture],
) -> Result<Vec<CompositionItem>, (CoinWgpuStatus, String)> {
    for (index, material) in materials.iter().enumerate() {
        let alpha = material.diffuse[3];
        let transparency = material.transparency;
        if !alpha.is_finite() || !transparency.is_finite()
            || !(0.0..=1.0).contains(&alpha) || !(0.0..=1.0).contains(&transparency)
            || (alpha + transparency - 1.0).abs() > 1.0e-5 {
            return Err((CoinWgpuStatus::InvalidArgument,
                format!("Material {} has invalid or inconsistent alpha/transparency", index)));
        }
    }
    let mut ordered = Vec::with_capacity(draws.len());
    for (draw_index, draw) in draws.iter().enumerate() {
        if draw.composition_flags > 1 || draw.clear_depth_before > 1
            || (draw.render_layer == 0 && draw.clear_depth_before != 0)
            || (draw_index > 0 && draw.render_layer < draws[draw_index - 1].render_layer) {
            return Err((CoinWgpuStatus::InvalidArgument,
                format!("Draw {} has invalid resolved composition metadata", draw_index)));
        }
        let state = states.get(draw.render_state_slot as usize).ok_or_else(||
            (CoinWgpuStatus::InvalidArgument, format!("Draw {} has invalid state", draw_index)))?;
        if state.clip_plane_count > 8 {
            return Err((CoinWgpuStatus::Unsupported, "More than eight clipping planes".into()));
        }
        for plane in &state.clip_planes[..state.clip_plane_count as usize] {
            if !plane.iter().all(|v| v.is_finite())
                || plane[..3].iter().map(|v| v * v).sum::<f32>() <= 1e-12 {
                return Err((CoinWgpuStatus::InvalidArgument, "Invalid clipping plane".into()));
            }
        }
        if state.material_slot as usize >= materials.len() {
            return Err((CoinWgpuStatus::InvalidArgument, format!("Draw {} has invalid material", draw_index)));
        }
        let first = draw.first_index as usize;
        let end = first.checked_add(draw.index_count as usize)
            .filter(|end| *end <= indices.len()).ok_or_else(||
                (CoinWgpuStatus::InvalidArgument, format!("Draw {} has invalid index range", draw_index)))?;
        for &vertex_index in &indices[first..end] {
            let vertex = vertices.get(vertex_index as usize).ok_or_else(||
                (CoinWgpuStatus::InvalidArgument, format!("Draw {} has invalid vertex", draw_index)))?;
            if vertex.material_slot as usize >= materials.len() {
                return Err((CoinWgpuStatus::InvalidArgument, format!("Draw {} has invalid vertex material", draw_index)));
            }
        }
        ordered.push(CompositionItem { draw_index, blend: draw.composition_flags & 1 != 0 });
    }
    Ok(ordered)
}

// Pass boundaries preserve overlay traversal and depth barriers. The base still
// is already resolved by the producer; blending is selected per draw.
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
                composition_flags: u32::from(alphas[i] < 1.0),
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
    fn executes_resolved_sequence_without_reclassifying_or_sorting() {
        let (draws, materials, states) =
            fixture(&[0, 0, 1, 1, 2, 2], &[0.5, 1.0, 0.5, 1.0, 0.5, 1.0]);
        let order = order(&[], &[], &draws, &materials, &states, &[]).unwrap();
        assert_eq!(
            order.iter().map(|item| item.draw_index).collect::<Vec<_>>(),
            vec![0, 1, 2, 3, 4, 5]
        );
        assert!(order[0].blend);
        assert!(!order[1].blend);
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
    fn rejects_invalid_resolved_flags_and_layer_sequence() {
        let (mut draws, materials, states) = fixture(&[1, 0], &[1.0, 1.0]);
        assert!(order(&[], &[], &draws, &materials, &states, &[]).is_err());
        draws[1].render_layer = 1;
        draws[0].composition_flags = 2;
        assert!(order(&[], &[], &draws, &materials, &states, &[]).is_err());
        draws[0].composition_flags = 1;
        let resolved = order(&[], &[], &draws, &materials, &states, &[]).unwrap();
        // Material opacity cannot override the blend decision of the producer.
        assert!(resolved[0].blend);
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
    #[test]
    fn rejects_invalid_clip_payload_before_execution() {
        let (draws, materials, mut states) = fixture(&[0], &[1.0]);
        states[0].clip_plane_count = 9;
        assert_eq!(order(&[], &[], &draws, &materials, &states, &[]).err().unwrap().0,
                   CoinWgpuStatus::Unsupported);
        states[0].clip_plane_count = 1;
        assert_eq!(order(&[], &[], &draws, &materials, &states, &[]).err().unwrap().0,
                   CoinWgpuStatus::InvalidArgument);
        states[0].clip_planes[0] = [1.0, 0.0, 0.0, f32::NAN];
        assert!(order(&[], &[], &draws, &materials, &states, &[]).is_err());
        states[0].clip_planes[0][3] = -0.25;
        assert!(order(&[], &[], &draws, &materials, &states, &[]).is_ok());
    }

}
