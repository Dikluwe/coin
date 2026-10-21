use super::{
    CoinWgpuDraw, CoinWgpuMaterial, CoinWgpuRenderState, CoinWgpuStatus, CoinWgpuTexture,
    CoinWgpuVertex,
};

#[derive(Clone)]
pub(super) struct CompositionItem {
    pub draw_index: usize,
    pub blend: bool,
    pub additive: bool,
    pub legacy_blend_alpha: bool,
    pub screen_door: bool,
    pub screen_door_level: u32,
    pub peel: bool,
    pub weighted: bool,
}

// CoinRender has already resolved ordering, alpha classification and depth.
// Infra validates the private transport and executes its sequence unchanged.
pub(super) fn order(
    vertices: &[CoinWgpuVertex],
    indices: &[u32],
    draws: &[CoinWgpuDraw],
    materials: &[CoinWgpuMaterial],
    states: &[CoinWgpuRenderState],
    _textures: &[CoinWgpuTexture],
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
    let authored_layer = draws.iter().find(|draw| draw.composition_flags & 64 != 0)
        .map(|draw| draw.render_layer);
    let mut ordered = Vec::with_capacity(draws.len());
    for (draw_index, draw) in draws.iter().enumerate() {
        if draw.composition_flags & !0x7f7f != 0
            || (draw.composition_flags & 32 != 0 && (draw.composition_flags & 1 == 0 || draw.composition_flags & 30 != 0))
            || (draw.composition_flags >> 8) > 64
            || (draw.composition_flags & 2 != 0 && draw.composition_flags & 1 == 0)
            || (draw.composition_flags >> 8 != 0 && draw.composition_flags & 4 == 0)
            || (draw.composition_flags & 8 != 0
                && (draw.composition_flags & 1 == 0 || (draw.render_layer != 0 && draw.composition_flags & 64 == 0)))
            || (draw.composition_flags & 4 != 0 && draw.composition_flags & 1 != 0)
            || (draw.composition_flags & 8 != 0 && draw.composition_flags & 2 != 0)
            || (draw.composition_flags & 16 != 0 &&
                (draw.composition_flags & 1 == 0 || draw.composition_flags & 14 != 0 || (draw.render_layer != 0 && draw.composition_flags & 64 == 0)))
            || (authored_layer.is_some() && draw.render_layer == 0)
            || authored_layer.is_some_and(|layer|
                (draw.render_layer == layer) != (draw.composition_flags & 64 != 0))
            || draw.clear_depth_before > 1
            || (draw.render_layer == 0 && draw.clear_depth_before != 0)
            || (draw_index > 0 && draw.render_layer < draws[draw_index - 1].render_layer)
        {
            return Err((
                CoinWgpuStatus::InvalidArgument,
                format!(
                    "Draw {} has invalid resolved composition metadata",
                    draw_index
                ),
            ));
        }
        let state = states.get(draw.render_state_slot as usize).ok_or_else(|| {
            (
                CoinWgpuStatus::InvalidArgument,
                format!("Draw {} has invalid state", draw_index),
            )
        })?;
        if state.clip_plane_count > 8 {
            return Err((
                CoinWgpuStatus::Unsupported,
                "More than eight clipping planes".into(),
            ));
        }
        for plane in &state.clip_planes[..state.clip_plane_count as usize] {
            if !plane.iter().all(|v| v.is_finite())
                || plane[..3].iter().map(|v| v * v).sum::<f32>() <= 1e-12
            {
                return Err((
                    CoinWgpuStatus::InvalidArgument,
                    "Invalid clipping plane".into(),
                ));
            }
        }
        if state.material_slot as usize >= materials.len() {
            return Err((
                CoinWgpuStatus::InvalidArgument,
                format!("Draw {} has invalid material", draw_index),
            ));
        }
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
        for &vertex_index in &indices[first..end] {
            let vertex = vertices.get(vertex_index as usize).ok_or_else(|| {
                (
                    CoinWgpuStatus::InvalidArgument,
                    format!("Draw {} has invalid vertex", draw_index),
                )
            })?;
            if vertex.material_slot as usize >= materials.len() {
                return Err((
                    CoinWgpuStatus::InvalidArgument,
                    format!("Draw {} has invalid vertex material", draw_index),
                ));
            }
        }
        ordered.push(CompositionItem {
            draw_index,
            blend: draw.composition_flags & 1 != 0,
            additive: draw.composition_flags & 2 != 0,
            legacy_blend_alpha: draw.composition_flags & 32 != 0,
            screen_door: draw.composition_flags & 4 != 0,
            screen_door_level: draw.composition_flags >> 8,
            peel: draw.composition_flags & 8 != 0,
            weighted: draw.composition_flags & 16 != 0,
        });
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
            || order[i - 1].peel != order[i].peel
            || order[i - 1].weighted != order[i].weighted
            || ((current.render_layer == 0 || current.composition_flags & 64 != 0)
                && order[i - 1].blend != order[i].blend)
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
#[path = "../../../../../testsuite/coinwgpu/rust/unit/composition_tests.rs"]
mod tests;
