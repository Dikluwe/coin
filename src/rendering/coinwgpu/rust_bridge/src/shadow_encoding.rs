//! Shadow and extended bump frame validation and encoding.

use super::*;

// Both the offscreen and window encoders consume the same validated VSM payload.
// Reject malformed pass lists before acquiring a swapchain texture or allocating
// offscreen attachments; the shared encoder then owns the map execution.
pub(super) fn validate_shadow_frame<'a>(
    f: &CoinWgpuFrameView,
    state_count: usize,
    camera_patch: bool,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> Result<Option<ShadowFrame<'a>>, CoinWgpuStatus> {
    let shadow_frame = if f.shadow_map_size == 0 {
            if f.shadow_map_size_second != 0 || f.shadow_map_size_third != 0 ||
                f.shadow_map_size_fourth != 0 || f.extra_shadow_pass_count != 0 ||
                !f.extra_shadow_passes.is_null() || !f.shadow_casters.is_null() || f.shadow_caster_count != 0 || f.shadow_kind != 0
                || !f.shadow_receivers.is_null() || f.shadow_receiver_count != 0
                || !f.shadow_casters_second.is_null() || f.shadow_caster_count_second != 0
                || !f.shadow_receivers_second.is_null() || f.shadow_receiver_count_second != 0 ||
            !f.shadow_casters_third.is_null() || f.shadow_caster_count_third != 0 ||
            !f.shadow_receivers_third.is_null() || f.shadow_receiver_count_third != 0 ||
            !f.shadow_casters_fourth.is_null() || f.shadow_caster_count_fourth != 0 ||
            !f.shadow_receivers_fourth.is_null() || f.shadow_receiver_count_fourth != 0 {
                set_error(error_buf, error_buf_len, "Shadow payload has data without a map");
                return Err(CoinWgpuStatus::InvalidArgument);
            }
            None
        } else {
            if camera_patch || f.camera_base_revision != 0
                || f.shadow_receiver_count != state_count as u64
                || f.shadow_kind > 1
                || !f.shadow_map_size.is_power_of_two() || f.shadow_map_size > 2048
                || !f.shadow_near_distance.is_finite()
                || !f.shadow_far_distance.is_finite()
                || (f.shadow_kind == 1 && f.shadow_near_distance <= 0.0)
                || f.shadow_far_distance <= f.shadow_near_distance
                || !f.shadow_epsilon.is_finite() || f.shadow_epsilon < 0.0
                || !f.shadow_threshold.is_finite() || !(0.0..1.0).contains(&f.shadow_threshold) {
                set_error(error_buf, error_buf_len, "Invalid or unsupported shadow frame parameters");
                return Err(CoinWgpuStatus::InvalidArgument);
            }
            let casters = match validate_slice(f.shadow_casters, f.shadow_caster_count,
                "shadow casters", error_buf, error_buf_len) {
                Ok(s) => s, Err(st) => return Err(st),
            };
            let receivers = match validate_slice(f.shadow_receivers, f.shadow_receiver_count,
                "shadow receivers", error_buf, error_buf_len) {
                Ok(s) => s, Err(st) => return Err(st),
            };
            let second = if f.shadow_map_size_second == 0 {
                if !f.shadow_casters_second.is_null() || f.shadow_caster_count_second != 0 ||
                    !f.shadow_receivers_second.is_null() ||
                    f.shadow_receiver_count_second != 0 || f.shadow_map_size_second != 0 ||
                    f.shadow_kind_second != 0 || f.shadow_epsilon_second != 0.0 ||
                    f.shadow_threshold_second != 0.0 {
                    set_error(error_buf, error_buf_len, "Partial second shadow pass");
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                None
            } else {
                if f.shadow_receiver_count_second != state_count as u64 ||
                    f.shadow_kind_second > 1 ||
                    !f.shadow_map_size_second.is_power_of_two() || f.shadow_map_size_second > 2048 ||
                    !f.shadow_near_distance_second.is_finite() ||
                    !f.shadow_far_distance_second.is_finite() ||
                    (f.shadow_kind_second == 1 && f.shadow_near_distance_second <= 0.0) ||
                    f.shadow_far_distance_second <= f.shadow_near_distance_second ||
                    !f.shadow_epsilon_second.is_finite() || f.shadow_epsilon_second < 0.0 ||
                    !f.shadow_threshold_second.is_finite() ||
                    !(0.0..1.0).contains(&f.shadow_threshold_second) {
                    set_error(error_buf, error_buf_len, "Invalid second shadow pass parameters");
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                let second_casters = match validate_slice(f.shadow_casters_second,
                    f.shadow_caster_count_second, "second shadow casters", error_buf, error_buf_len) {
                    Ok(s) => s, Err(st) => return Err(st),
                };
                let second_receivers = match validate_slice(f.shadow_receivers_second,
                    f.shadow_receiver_count_second, "second shadow receivers", error_buf, error_buf_len) {
                    Ok(s) => s, Err(st) => return Err(st),
                };
                Some(ShadowPass {
                    casters: second_casters, receivers: second_receivers,
                    map_size: f.shadow_map_size_second, near: f.shadow_near_distance_second,
                    far: f.shadow_far_distance_second,
                    epsilon: f.shadow_epsilon_second, threshold: f.shadow_threshold_second,
                    kind: f.shadow_kind_second,
                })
            };
            let third = if f.shadow_map_size_third == 0 {
                if !f.shadow_casters_third.is_null() || f.shadow_caster_count_third != 0 ||
                    !f.shadow_receivers_third.is_null() ||
                    f.shadow_receiver_count_third != 0 || f.shadow_map_size_third != 0 ||
                    f.shadow_kind_third != 0 || f.shadow_epsilon_third != 0.0 ||
                    f.shadow_threshold_third != 0.0 {
                    set_error(error_buf, error_buf_len, "Partial third shadow pass");
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                None
            } else {
                if second.is_none() ||
                    f.shadow_receiver_count_third != state_count as u64 ||
                    f.shadow_kind_third > 1 ||
                    !f.shadow_map_size_third.is_power_of_two() || f.shadow_map_size_third > 2048 ||
                    !f.shadow_near_distance_third.is_finite() ||
                    !f.shadow_far_distance_third.is_finite() ||
                    (f.shadow_kind_third == 1 && f.shadow_near_distance_third <= 0.0) ||
                    f.shadow_far_distance_third <= f.shadow_near_distance_third ||
                    !f.shadow_epsilon_third.is_finite() || f.shadow_epsilon_third < 0.0 ||
                    !f.shadow_threshold_third.is_finite() ||
                    !(0.0..1.0).contains(&f.shadow_threshold_third) {
                    set_error(error_buf, error_buf_len, "Invalid third shadow pass parameters");
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                let pass_casters = match validate_slice(f.shadow_casters_third,
                    f.shadow_caster_count_third, "third shadow casters", error_buf, error_buf_len) {
                    Ok(s) => s, Err(st) => return Err(st),
                };
                let pass_receivers = match validate_slice(f.shadow_receivers_third,
                    f.shadow_receiver_count_third, "third shadow receivers", error_buf, error_buf_len) {
                    Ok(s) => s, Err(st) => return Err(st),
                };
                Some(ShadowPass {
                    casters: pass_casters, receivers: pass_receivers,
                    map_size: f.shadow_map_size_third, near: f.shadow_near_distance_third,
                    far: f.shadow_far_distance_third,
                    epsilon: f.shadow_epsilon_third, threshold: f.shadow_threshold_third,
                    kind: f.shadow_kind_third,
                })
            };
            let fourth = if f.shadow_map_size_fourth == 0 {
                if !f.shadow_casters_fourth.is_null() || f.shadow_caster_count_fourth != 0 ||
                    !f.shadow_receivers_fourth.is_null() ||
                    f.shadow_receiver_count_fourth != 0 || f.shadow_map_size_fourth != 0 ||
                    f.shadow_kind_fourth != 0 || f.shadow_epsilon_fourth != 0.0 ||
                    f.shadow_threshold_fourth != 0.0 {
                    set_error(error_buf, error_buf_len, "Partial fourth shadow pass");
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                None
            } else {
                if third.is_none() ||
                    f.shadow_receiver_count_fourth != state_count as u64 ||
                    f.shadow_kind_fourth > 1 ||
                    !f.shadow_map_size_fourth.is_power_of_two() || f.shadow_map_size_fourth > 2048 ||
                    !f.shadow_near_distance_fourth.is_finite() ||
                    !f.shadow_far_distance_fourth.is_finite() ||
                    (f.shadow_kind_fourth == 1 && f.shadow_near_distance_fourth <= 0.0) ||
                    f.shadow_far_distance_fourth <= f.shadow_near_distance_fourth ||
                    !f.shadow_epsilon_fourth.is_finite() || f.shadow_epsilon_fourth < 0.0 ||
                    !f.shadow_threshold_fourth.is_finite() ||
                    !(0.0..1.0).contains(&f.shadow_threshold_fourth) {
                    set_error(error_buf, error_buf_len, "Invalid fourth shadow pass parameters");
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                let pass_casters = match validate_slice(f.shadow_casters_fourth,
                    f.shadow_caster_count_fourth, "fourth shadow casters", error_buf, error_buf_len) {
                    Ok(s) => s, Err(st) => return Err(st),
                };
                let pass_receivers = match validate_slice(f.shadow_receivers_fourth,
                    f.shadow_receiver_count_fourth, "fourth shadow receivers", error_buf, error_buf_len) {
                    Ok(s) => s, Err(st) => return Err(st),
                };
                Some(ShadowPass {
                    casters: pass_casters, receivers: pass_receivers,
                    map_size: f.shadow_map_size_fourth, near: f.shadow_near_distance_fourth,
                    far: f.shadow_far_distance_fourth,
                    epsilon: f.shadow_epsilon_fourth, threshold: f.shadow_threshold_fourth,
                    kind: f.shadow_kind_fourth,
                })
            };
            let extra_views = if f.extra_shadow_pass_count == 0 {
                if !f.extra_shadow_passes.is_null() {
                    set_error(error_buf, error_buf_len, "Partial extra shadow pass list");
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                &[][..]
            } else {
                if fourth.is_none() || f.extra_shadow_pass_count > 4 {
                    set_error(error_buf, error_buf_len, "Extra shadow passes require four base maps and at most four additions");
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                match validate_slice(f.extra_shadow_passes, f.extra_shadow_pass_count,
                    "extra shadow passes", error_buf, error_buf_len) {
                    Ok(s) => s, Err(st) => return Err(st),
                }
            };
            let mut extra = Vec::with_capacity(extra_views.len());
            for (slot, pass) in extra_views.iter().enumerate() {
                if pass.receiver_count != state_count as u64 ||
                    pass.kind > 1 || !pass.map_size.is_power_of_two() ||
                    pass.map_size > 2048 || !pass.near_distance.is_finite() ||
                    !pass.far_distance.is_finite() || (pass.kind == 1 && pass.near_distance <= 0.0) ||
                    pass.far_distance <= pass.near_distance ||
                    !pass.epsilon.is_finite() || pass.epsilon < 0.0 ||
                    !pass.threshold.is_finite() || !(0.0..1.0).contains(&pass.threshold) {
                    set_error(error_buf, error_buf_len, &format!("Invalid extra shadow pass {slot}"));
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                let casters = match validate_slice(pass.casters, pass.caster_count,
                    "extra shadow casters", error_buf, error_buf_len) {
                    Ok(s) => s, Err(st) => return Err(st),
                };
                let receivers = match validate_slice(pass.receivers, pass.receiver_count,
                    "extra shadow receivers", error_buf, error_buf_len) {
                    Ok(s) => s, Err(st) => return Err(st),
                };
                if receivers.iter().any(|r| !r.model_view.iter()
                    .chain(r.model_view_projection.iter()).all(|v| v.is_finite())) {
                    set_error(error_buf, error_buf_len, "Invalid extra shadow casters or receiver matrices");
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                extra.push(ShadowPass { casters, receivers,
                    map_size: pass.map_size, near: pass.near_distance,
                    far: pass.far_distance, epsilon: pass.epsilon,
                    threshold: pass.threshold, kind: pass.kind });
            }
            Some(ShadowFrame {
                second, third, fourth, extra,
                casters, receivers, map_size: f.shadow_map_size,
                near: f.shadow_near_distance, far: f.shadow_far_distance,
                epsilon: f.shadow_epsilon, threshold: f.shadow_threshold, kind: f.shadow_kind,
            })
        };

    if let Some(frame) = shadow_frame.as_ref() {
        let passes = frame.second.iter().chain(frame.third.iter()).chain(frame.fourth.iter())
            .chain(frame.extra.iter());
        for (casters, size) in std::iter::once((frame.casters, frame.map_size))
            .chain(passes.map(|pass| (pass.casters, pass.map_size))) {
            if let Err(message) = shadow::validate_caster_viewports(casters, size) {
                set_error(error_buf, error_buf_len, &message);
                return Err(CoinWgpuStatus::InvalidArgument);
            }
        }
    }
    Ok(shadow_frame)
}

pub(super) fn peeling_supported(ctx: &DeviceState, samples: u32) -> bool {
    let mut cache = ctx.peeling_sample_support.lock().unwrap();
    *cache.entry(samples).or_insert_with(|| peeling::adapter_supported(&ctx.adapter, &ctx.device, samples))
}

pub(super) fn weighted_supported(ctx: &DeviceState, samples: u32) -> bool {
    let mut cached = ctx.weighted_sample_support.lock().unwrap();
    *cached.entry(samples).or_insert_with(||
        weighted::adapter_supported(&ctx.adapter, &ctx.device, samples))
}

pub(super) fn custom_shadow_sample_depth(state: &CoinWgpuRenderState) -> bool {
    state.depth_range != [0.0, 1.0] ||
        (state.polygon_offset_enabled != 0 && state.polygon_offset_styles & state.polygon_offset_primitive_style != 0 &&
         (state.polygon_offset_factor != 0.0 || state.polygon_offset_units != 0.0 || state.polygon_offset_slope_bias != 0.0))
}

pub(super) fn shadow_multitexture_depth_supported(ctx: &DeviceState) -> bool {
    shadow_multitexture_supported(ctx) &&
        ctx.adapter.get_downlevel_capabilities().flags.contains(wgpu::DownlevelFlags::MULTISAMPLED_SHADING)
}

// Shared admission for the extended receiver. Window submission must run this
// before acquiring a WSI image: dropping rejected Vulkan acquisitions can
// exhaust the swapchain even though no queue submission or serial was issued.
pub(super) fn textures_for_shadow(state:&CoinWgpuRenderState,textures:&[CoinWgpuTexture])->bool {
    textures.get(state.texture_slot as usize).is_some_and(|image|matches!(image.format,11|12))
}

pub(super) fn validate_shadow_multitexture_profile(
    ctx: &DeviceState,
    textures_slice: &[CoinWgpuTexture],
    states_slice: &[CoinWgpuRenderState],
    draws_slice: &[CoinWgpuDraw],
    draw_order: &[composition::CompositionItem],
    shadow_frame: Option<&ShadowFrame>,
    sample_count: u32,
    rasterization_policy: u32,
) -> Result<bool, (CoinWgpuStatus, String)> {
    // Inherited raster source states are compute inputs, not surface draws.
    let shadow_multitexture = shadow_frame.is_some() && draws_slice.iter()
        .filter_map(|draw| states_slice.get(draw.render_state_slot as usize))
        .any(|state| state.extra_textures.iter().any(|unit| unit.enabled != 0) ||
            (state.has_texture!=0 && textures_for_shadow(state,textures_slice)));
    if shadow_multitexture && sample_count > 1 && rasterization_policy != 0 {
        return Err((CoinWgpuStatus::Unsupported, "Shadow multitexture MSAA requires native rasterization".into()));
    }
    if shadow_multitexture && sample_count > 1 && states_slice.iter().any(custom_shadow_sample_depth) &&
        !shadow_multitexture_depth_supported(ctx) {
        return Err((CoinWgpuStatus::Unsupported, "Shadow multitexture custom MSAA depth requires 80 interstage components and sample shading".into()));
    }
    if shadow_multitexture && !shadow_multitexture_supported(ctx) {
        return Err((CoinWgpuStatus::Unsupported, "Shadow multitexture requires 80 interstage components, 11 sampled textures and 8 samplers".into()));
    }
    if shadow_multitexture {
        let shadow = shadow_frame.unwrap();
        let count = 1 + usize::from(shadow.second.is_some()) + usize::from(shadow.third.is_some()) +
            usize::from(shadow.fourth.is_some()) + shadow.extra.len();
        let extent = [shadow.second.as_ref(),shadow.third.as_ref(),shadow.fourth.as_ref()].into_iter()
            .flatten().chain(shadow.extra.iter()).map(|pass| pass.map_size).fold(shadow.map_size,u32::max);
        let source_bytes=std::iter::once(shadow.map_size).chain(
            [shadow.second.as_ref(),shadow.third.as_ref(),shadow.fourth.as_ref()].into_iter().flatten()
                .chain(shadow.extra.iter()).map(|pass|pass.map_size)).map(|size|u64::from(size)*u64::from(size)*20).sum::<u64>();
        let array_bytes=u64::from(extent)*u64::from(extent)*16*u64::from(shadow::array_layer_count(count as u32));
        if source_bytes+array_bytes>128*1024*1024 {
            return Err((CoinWgpuStatus::Unsupported,"Shadow source attachments and moment array exceed 128 MiB per apply".into()));
        }
        let modes = 1 | if draw_order.iter().any(|item| item.weighted) { 2 } else { 0 } |
            if draw_order.iter().any(|item| item.peel) { 4 } else { 0 };
        if !shadow_array_supported(ctx,extent,count as u32) || !shadow_multitexture_alpha_supported(ctx,sample_count,modes) {
            return Err((CoinWgpuStatus::Unsupported,"Complete shadow multitexture composition pipeline or moment array unavailable".into()));
        }
    }
    Ok(shadow_multitexture)
}

// The extended profile is selected only for an actual directional bump receiver
// or for directional bump plus a texture cascade. Legacy unit-zero bump retains
// its original two-pass renderer and exact cache semantics.
pub(super) fn extended_bump_profile(
    states: &[CoinWgpuRenderState],
    textures: &[CoinWgpuTexture],
    draws: &[CoinWgpuDraw],
    shadow: Option<&ShadowFrame>, payload: &ValidatedBumpPayload, indices: &[u32],
) -> bool {
    let consumed = match bump_consumption_for_draws(payload, indices, draws) {
        Ok(consumed) => consumed,
        Err(_) => return true, // Full admission reports the malformed occurrence.
    };
    shadow.is_some()
        || draws
            .iter()
            .zip(consumed)
            .filter_map(|(draw, active)| states.get(draw.render_state_slot as usize).map(|state| (state, active)))
            .any(|(state, active)| {
                if (active && state.light_count > 1) || state.extra_textures.iter().any(|unit| unit.enabled != 0) {
                    return true;
                }
                state.has_texture != 0
                    && (state.texture_model != 0
                        || state.texture_projection != 0
                        || state.texture_combines[0][0][0] != 0.0
                        || textures
                            .get(state.texture_slot as usize)
                            .is_some_and(|image| !matches!(image.format, 0 | 2 | 3 | 4)))
            })
}

pub(super) fn bump_consumption_for_draws(payload: &ValidatedBumpPayload, indices: &[u32],
    draws: &[CoinWgpuDraw]) -> Result<Vec<bool>, String>
{
    draws.iter().map(|draw| {
        let first = draw.first_index as usize;
        let end = first.checked_add(draw.index_count as usize)
            .ok_or("Bump occurrence index range overflow")?;
        let range = indices.get(first..end).ok_or("Bump occurrence index range is invalid")?;
        let mut consumed = false;
        for &index in range {
            let vertex = payload.vertices.get(index as usize).ok_or("Bump occurrence vertex is invalid")?;
            if vertex.image_slot as usize > payload.images.len() {
                return Err("Bump occurrence image slot is invalid".into());
            }
            consumed |= vertex.image_slot != 0;
        }
        Ok(consumed)
    }).collect()
}

// No device, allocation or encoder is needed to reject malformed consumed
// lighting. Ordinary occurrences retain the common surface lighting contract.
pub(super) fn validate_bump_lighting_occurrences(payload: &ValidatedBumpPayload,
    vertices: &[CoinWgpuVertex], indices: &[u32], draws: &[CoinWgpuDraw],
    states: &[CoinWgpuRenderState], materials: &[CoinWgpuMaterial],
    textures: &[CoinWgpuTexture], shadow: Option<&ShadowFrame>)
    -> Result<(), (CoinWgpuStatus, String)>
{
    let consumed = bump_consumption_for_draws(payload, indices, draws)
        .map_err(|message| (CoinWgpuStatus::InvalidArgument, message))?;
    if payload.vertices.len() != vertices.len() {
        return Err((CoinWgpuStatus::InvalidArgument, "Invalid bump stream extent".into()));
    }
    let extended = extended_bump_profile(states, textures, draws, shadow, payload, indices);
    for (draw, active) in draws.iter().zip(consumed) {
        if !active { continue; }
        let state = states.get(draw.render_state_slot as usize)
            .ok_or((CoinWgpuStatus::InvalidArgument, "Invalid bump lighting state slot".into()))?;
        if state.light_model != 1 || state.light_count == 0 ||
            state.light_count > (if extended { 8 } else { 1 }) {
            return Err((CoinWgpuStatus::Unsupported,
                "Consumed bump requires PHONG and qualified lighting".into()));
        }
        let material = materials.get(state.material_slot as usize)
            .ok_or((CoinWgpuStatus::InvalidArgument, "Invalid bump lighting material slot".into()))?;
        for light in &state.lights[..state.light_count as usize] {
            if shadow.is_some() && light.position_type[3] != 0.0 {
                return Err((CoinWgpuStatus::Unsupported, "Bump shadows require directional lights".into()));
            }
            let mut one = *state;
            one.light_count = 1;
            one.lights[0] = *light;
            bump_uniforms_from_state(&one, material)
                .map_err(|message| (CoinWgpuStatus::Unsupported, message))?;
        }
    }
    Ok(())
}

// Respect the authored RTT NONE declaration before inspecting image alpha.
// Otherwise check authored texels, never zero-filled physical atlas padding.
pub(super) fn bump_receiver_texture_opaque(
    ctx: &DeviceState,
    image: &CoinWgpuTexture,
) -> Result<bool, (CoinWgpuStatus, String)> {
    let declared_opaque = image.reserved & TEXTURE_DECLARED_OPAQUE != 0;
    if gpu_texture_token(image) {
        let registry = ctx.rtt_textures.lock().unwrap();
        let entry = registry.active.get(&image.content_digest).ok_or((
            CoinWgpuStatus::InvalidArgument,
            "Bump receiver RTT token is stale or from another device/generation".into(),
        ))?;
        // Token metadata can validate a positive claim, but an unset claim
        // does not establish opacity. NONE is the explicit author override;
        // otherwise only the owning registry may prove the resource opaque.
        return Ok(declared_opaque || entry.opaque);
    }
    if image.pixels.is_null() {
        return Err((
            CoinWgpuStatus::InvalidArgument,
            "Null bump receiver texture pixels".into(),
        ));
    }
    let pixels =
        unsafe { std::slice::from_raw_parts(image.pixels, image.pixel_bytes_len as usize) };
    let alpha = |pixel: usize| pixels.get(pixel * 4 + 3).is_some_and(|value| *value == 255);
    if let Some((mut n, _, mips)) = cube_atlas_metadata(image)? {
        let mut texels = 0usize;
        loop {
            texels += 6 * n as usize * n as usize;
            if !mips || n == 1 { break; }
            n /= 2;
        }
        return Ok(declared_opaque || (0..texels).all(alpha));
    }
    if let Some((mut width, mut height, mut depth, mips)) = spatial_atlas_metadata(image)? {
        if declared_opaque {
            return Ok(true);
        }
        let columns = image.width / width;
        let mut base = 0;
        loop {
            for z in 0..depth {
                for y in 0..height {
                    for x in 0..width {
                        let pixel = (((base + z / columns * height + y) * image.width) +
                            z % columns * width + x) as usize;
                        if !alpha(pixel) { return Ok(false); }
                    }
                }
            }
            base += height * depth.div_ceil(columns);
            if !mips || (width == 1 && height == 1 && depth == 1) { break; }
            width = (width / 2).max(1);
            height = (height / 2).max(1);
            depth = (depth / 2).max(1);
        }
        return Ok(true);
    }
    let levels = texture_levels(image.width, image.height, image.format)?;
    let &(width, height, offset) = levels.last().ok_or((
        CoinWgpuStatus::InvalidArgument,
        "Empty bump texture levels".into(),
    ))?;
    let format = texture_gpu_format(image)?;
    if offset.checked_add(texture_profile::level_bytes(width, height, format))
        != Some(image.pixel_bytes_len)
    {
        return Err((
            CoinWgpuStatus::InvalidArgument,
            "Bump receiver texture byte count mismatch".into(),
        ));
    }
    if declared_opaque {
        return Ok(true);
    }
    match image.format {
        0 | 2 | 3 | 4 => Ok((0..pixels.len() / 4).all(alpha)),
        5 | 6 => Ok(pixels
            .chunks_exact(8)
            .take(image.width as usize * image.height as usize)
            .all(|pixel| u16::from_le_bytes([pixel[6], pixel[7]]) >= 0x3c00)),
        7..=10 => {
            let blocks = (image.width + 3) / 4;
            for y in 0..image.height {
                for x in 0..image.width {
                    let offset = ((y / 4 * blocks + x / 4) * 16) as usize;
                    let block = pixels.get(offset..offset + 16).ok_or((
                        CoinWgpuStatus::InvalidArgument,
                        "Short BC3 bump base".into(),
                    ))?;
                    let bits = block[2..8]
                        .iter()
                        .enumerate()
                        .fold(0u64, |bits, (i, b)| bits | (u64::from(*b) << (8 * i)));
                    let code = (bits >> (3 * ((y % 4) * 4 + x % 4))) & 7;
                    let a0 = u32::from(block[0]);
                    let a1 = u32::from(block[1]);
                    let value = match code {
                        0 => a0,
                        1 => a1,
                        7 if a0 <= a1 => 255,
                        6 if a0 <= a1 => 0,
                        _ if a0 > a1 => ((8 - code as u32) * a0 + (code as u32 - 1) * a1) / 7,
                        _ => ((6 - code as u32) * a0 + (code as u32 - 1) * a1) / 5,
                    };
                    if value != 255 {
                        return Ok(false);
                    }
                }
            }
            Ok(true)
        }
        _ => Err((
            CoinWgpuStatus::InvalidArgument,
            "Invalid bump receiver texture format".into(),
        )),
    }
}

pub(super) fn validate_extended_bump_profile(
    ctx: &DeviceState,
    vertices: &[CoinWgpuVertex],
    indices: &[u32],
    draws: &[CoinWgpuDraw],
    states: &[CoinWgpuRenderState],
    materials: &[CoinWgpuMaterial],
    textures: &[CoinWgpuTexture],
    samplers: &[CoinWgpuSampler],
    payload: &ValidatedBumpPayload,
    shadow: Option<&ShadowFrame>,
    target_size: [u32; 2],
) -> Result<(), (CoinWgpuStatus, String)> {
    let unsupported = |message: &str| (CoinWgpuStatus::Unsupported, message.to_owned());
    if let Some(shadow) = shadow {
        if shadow.kind != 0 || shadow.second.iter().chain(shadow.third.iter())
            .chain(shadow.fourth.iter()).chain(shadow.extra.iter()).any(|pass|pass.kind!=0)
        {
            return Err(unsupported("Bump shadows require directional maps"));
        }
    }
    if ctx.device.limits().max_inter_stage_shader_components < 80
        || ctx.device.limits().max_sampled_textures_per_shader_stage < 12
        || ctx.device.limits().max_samplers_per_shader_stage < 9
        || ctx.device.limits().max_storage_buffers_per_shader_stage < 1
    {
        return Err(unsupported("Bump receiver needs 80 varying components, 12 texture bindings, nine samplers and vertex storage"));
    }
    admit_spatial_resources(ctx, textures)?;
    {
        let registry = ctx.rtt_textures.lock().unwrap();
        for image in textures.iter().filter(|image| image.format == 1) {
            let entry = registry.active.get(&image.content_digest).ok_or((
                CoinWgpuStatus::InvalidArgument,
                "Bump receiver RTT token is stale or from another device/generation".into(),
            ))?;
            if entry.width != image.width
                || entry.height != image.height
                || entry.cube_face_size != 0
                || texture_metadata(image) & !31 != 0
                || (texture_metadata(image) & 1 != 0 && !entry.opaque)
                || texture_metadata(image) >> 2 != entry.format
                || !image.pixels.is_null()
                || image.pixel_bytes_len != 0
                || (texture_metadata(image) & 2 != 0
                    && entry.mip_count <= 1
                    && (image.width > 1 || image.height > 1))
            {
                return Err((
                    CoinWgpuStatus::InvalidArgument,
                    "Bump receiver RTT token metadata mismatch".into(),
                ));
            }
        }
    }
    let mut largest_draw = 0usize;
    for draw in draws {
        let start = draw.first_index as usize;
        let end = start
            .checked_add(draw.index_count as usize)
            .filter(|end| *end <= indices.len())
            .ok_or_else(|| unsupported("Bump receiver draw indices out of bounds"))?;
        let unique: std::collections::HashSet<_> = indices[start..end].iter().copied().collect();
        largest_draw = largest_draw.max(unique.len());
    }
    let material_count = largest_draw
        .checked_add(1)
        .and_then(|count| count.checked_mul(std::mem::size_of::<GpuMaterial>()))
        .ok_or_else(|| unsupported("Bump receiver material stream overflow"))?;
    if material_count as u64 > u64::from(ctx.device.limits().max_storage_buffer_binding_size)
        || material_count as u64 > ctx.device.limits().max_buffer_size
    {
        return Err(unsupported(
            "Bump receiver corner stream exceeds the device buffer binding limit",
        ));
    }
    if payload.vertices.len() != vertices.len()
        || payload.images.iter().any(|image| {
            image.width > ctx.device.limits().max_texture_dimension_2d
                || image.height > ctx.device.limits().max_texture_dimension_2d
        })
    {
        return Err(unsupported(
            "Bump receiver vertex count or normal image extent is invalid",
        ));
    }
    bump_image_slots_for_draws(payload, indices, draws)
        .map_err(|message| (CoinWgpuStatus::InvalidArgument, message))?;
    let consumed = bump_consumption_for_draws(payload, indices, draws)
        .map_err(|message| (CoinWgpuStatus::InvalidArgument, message))?;
    for (draw, active) in draws.iter().zip(consumed) {
        let state = states
            .get(draw.render_state_slot as usize)
            .ok_or_else(|| unsupported("Bump receiver state slot out of bounds"))?;
        if draw.topology != 0
            || draw.render_layer != 0
            || draw.composition_flags != 0
            || draw.clear_depth_before != 0
            || (active && (state.light_model != 1 || state.light_count == 0 || state.light_count > 8
                || state.lights[..state.light_count.min(8) as usize].iter()
                    .any(|light| !matches!(light.position_type[3],0.0|1.0|2.0) ||
                        (shadow.is_some() && light.position_type[3]!=0.0))))
            || state.fog_mode != 0
            || state.depth_test != 1
            || state.depth_write != 1
            || state.cull_mode != 0
            || !matches!(state.depth_function, 2 | 3)
            || state.depth_range != [0.0, 1.0]
            || state.polygon_offset_enabled != 0
            || state.clip_plane_count != 0
            || state.alpha_test_function != 0
            || (state.viewport != [0, 0, 0, 0]
                && state.viewport != [0, 0, target_size[0] as i32, target_size[1] as i32])
        {
            return Err(unsupported("Bump receiver requires opaque one-directional-light triangles and default full-target depth"));
        }
        let end = draw.first_index as usize + draw.index_count as usize;
        let draw_indices = &indices[draw.first_index as usize..end];
        validate_texture_coordinates_with_images(
            state,
            draw.topology,
            vertices,
            draw_indices,
            textures,
        )
        .map_err(|message| (CoinWgpuStatus::InvalidArgument, message))?;
        for triangle in draw_indices.chunks_exact(3) {
            let first = &payload.vertices[triangle[0] as usize];
            if first.image_slot == 0 {
                continue;
            }
            for &index in triangle {
                if state.light_count > 1 {
                    let corner_material = &materials[vertices[index as usize].material_slot as usize];
                    if corner_material.transparency != 0.0 || corner_material.diffuse[3] != 1.0 ||
                        !corner_material.shininess.is_finite() ||
                        !(0.0..=1.0).contains(&corner_material.shininess) ||
                        state.lights[..state.light_count as usize].iter().any(|light| {
                            (0..3).any(|channel| !(corner_material.diffuse[channel] *
                                light.color_intensity[channel] * light.color_intensity[3]).is_finite())
                        }) {
                        return Err(unsupported("Invalid multilight bump corner material product"));
                    }
                }
                let coord = payload.vertices[index as usize].coord;
                if coord[3].abs() < f32::MIN_POSITIVE
                    || coord[3].is_sign_negative() != first.coord[3].is_sign_negative()
                    || !(coord[0] / coord[3]).is_finite()
                    || !(coord[1] / coord[3]).is_finite()
                {
                    return Err(unsupported("Invalid bump receiver projected Q"));
                }
            }
        }
        let material = materials
            .get(state.material_slot as usize)
            .ok_or_else(|| unsupported("Bump receiver material slot out of bounds"))?;
        for index in 0..(if active { state.light_count as usize } else { 0 }) {
            let mut one=*state;
            one.light_count=1;
            one.lights[0]=state.lights[index];
            bump_uniforms_from_state(&one, material)
                .map_err(|message| (CoinWgpuStatus::Unsupported, message))?;
        }
        let layers: [CoinWgpuTextureUnit; 8] = std::array::from_fn(|unit| {
            if unit == 0 {
                CoinWgpuTextureUnit {
                    matrix: state.texture_matrix,
                    enabled: state.has_texture,
                    texture_slot: state.texture_slot,
                    sampler_slot: state.sampler_slot,
                    model: state.texture_model,
                    blend_color: state.texture_blend_color,
                }
            } else {
                state.extra_textures[unit - 1]
            }
        });
        for layer in layers.iter().filter(|layer| layer.enabled != 0) {
            let image = textures
                .get(layer.texture_slot as usize)
                .ok_or_else(|| unsupported("Bump receiver texture slot out of bounds"))?;
            if active && (!matches!(image.format, 0 | 1 | 2 | 3 | 4 | 11 | 12) ||
                (image.format == 1 && texture_metadata(image) >> 2 != 0)) {
                return Err(unsupported("Bump receiver bases require RGBA8 linear/sRGB sources"));
            }
            if !bump_receiver_texture_opaque(ctx, image)? {
                return Err(unsupported("Bump receiver textures must be opaque"));
            }
        }
    }
    validate_spatial_resources(states, textures, samplers)?;
    Ok(())
}

// GPU material storage is indexed per corner only in the dedicated receiver.
// Its five vec4s preserve the original single-light packing. Multi-DIR packs
// coord, per-corner diffuse/specular/shininess and S/T/N in the same 80 bytes.
// The VS projects each legacy L and E before interpolation. All thirteen
// geometry attributes and the private STRQ transport remain intact.
pub(super) fn encode_extended_bump_draw(
    ctx: &DeviceState,
    vertices: &[CoinWgpuVertex],
    indices: &[u32],
    draw: &CoinWgpuDraw,
    states: &[CoinWgpuRenderState],
    materials: &[CoinWgpuMaterial],
    textures: &[CoinWgpuTexture],
    samplers: &[CoinWgpuSampler],
    payload: &ValidatedBumpPayload,
    image: &GpuBumpImage,
    shadow: Option<&ShadowFrame>,
    color_view: &wgpu::TextureView,
    resolve_view: Option<&wgpu::TextureView>,
    color_format: wgpu::TextureFormat,
    depth_view: &wgpu::TextureView,
    depth_texture: &wgpu::Texture,
    target_size: [u32; 2],
    clear_color: [f32; 4],
    sample_count: u32,
    sampling_policy: u32,
    clear: bool,
    shared_shadow: Option<&(Vec<shadow::ShadowMap>, shadow::ShadowArray)>,
) -> Result<(wgpu::CommandBuffer, Option<InstancedGpuBuffers>), (CoinWgpuStatus, String)> {
    let state = &states[draw.render_state_slot as usize];
    let mut first_light=*state;
    first_light.light_count=1;
    let uniform = bump_uniforms_from_state(&first_light, &materials[state.material_slot as usize])
        .map_err(|message| (CoinWgpuStatus::Unsupported, message))?;
    let source_material = materials[state.material_slot as usize];
    let mut geometry = Vec::new();
    let mut compact_indices = Vec::with_capacity(draw.index_count as usize);
    let mut remap = HashMap::new();
    let mut cpu_materials = vec![source_material];
    let mut gpu_materials = pack_gpu_materials(&cpu_materials);
    let normalize = |mut vector: [f32; 3]| {
        let square = vector.iter().map(|x| x * x).sum::<f32>();
        if square > 0.0 {
            for value in &mut vector {
                *value /= square.sqrt();
            }
        }
        vector
    };
    let dot = |a: [f32; 3], b: [f32; 3]| a.iter().zip(b).map(|(a, b)| a * b).sum::<f32>();
    let end = draw.first_index as usize + draw.index_count as usize;
    for &index in &indices[draw.first_index as usize..end] {
        if let Some(&mapped) = remap.get(&index) {
            compact_indices.push(mapped);
            continue;
        }
        let mapped = geometry.len() as u32;
        remap.insert(index, mapped);
        compact_indices.push(mapped);
        let mut vertex = vertices[index as usize];
        let corner_material=materials[vertex.material_slot as usize];
        vertex.material_slot = gpu_materials.len() as u32;
        let corner = &payload.vertices[index as usize];
        let light = normalize(std::array::from_fn(|axis| {
            uniform.light_position_type[axis] -
              if uniform.light_position_type[3] != 0.0 { vertex.position[axis] } else { 0.0 }
        }));
        let eye = normalize(std::array::from_fn(|axis| {
            uniform.eye_position[axis] - vertex.position[axis]
        }));
        let tangent_light = [
            dot(corner.tangent_s, light),
            dot(corner.tangent_t, light),
            dot(vertex.normal, light),
        ];
        let tangent_eye = [
            dot(corner.tangent_s, eye),
            dot(corner.tangent_t, eye),
            dot(vertex.normal, eye),
        ];
        if state.light_count>1 {
            // Same five vec4s per corner: coord, per-corner material and the
            // original S/T/N basis. No new vertex attribute or wire field.
            gpu_materials.push(GpuMaterial {
                ambient:corner.coord,
                diffuse:[corner_material.diffuse[0],corner_material.diffuse[1],
                         corner_material.diffuse[2],corner_material.shininess],
                specular:[corner_material.specular[0],corner_material.specular[1],
                          corner_material.specular[2],vertex.normal[0]],
                emission:[corner.tangent_s[0],corner.tangent_s[1],corner.tangent_s[2],vertex.normal[1]],
                params:[corner.tangent_t[0],corner.tangent_t[1],corner.tangent_t[2],vertex.normal[2]],
            });
        } else {
        gpu_materials.push(GpuMaterial {
            ambient: corner.coord,
            diffuse: uniform.diffuse,
            specular: uniform.specular,
            emission: [tangent_light[0], tangent_light[1], tangent_light[2], 0.0],
            params: [
                uniform.shininess[0],
                tangent_eye[0],
                tangent_eye[1],
                tangent_eye[2],
            ],
        });
        }
        cpu_materials.push(if state.light_count>1 {corner_material}else{source_material});
        geometry.push(vertex);
    }
    let mut draw = *draw;
    draw.stable_node_id = 0;
    draw.first_vertex = 0;
    draw.vertex_count = geometry.len() as u32;
    draw.first_index = 0;
    let mut states = states.to_vec();
    for state in &mut states {
        state.material_slot = 0;
    }
    let order = composition::order(
        &geometry,
        &compact_indices,
        &[draw],
        &cpu_materials,
        &states,
        textures,
    )?;
    encode_frame_impl(
        ctx,
        0,
        clear_color,
        target_size[0],
        target_size[1],
        &geometry,
        &compact_indices,
        &[draw],
        &order,
        &cpu_materials,
        &states,
        &[],
        textures,
        samplers,
        shadow,
        None,
        &[],
        &[],
        None,
        color_view,
        resolve_view,
        color_format,
        depth_view,
        depth_texture,
        sample_count,
        0,
        sampling_policy,
        0,
        clear,
        Some((&image.view, &image.sampler, &gpu_materials)),
        shared_shadow,
    )
}

// Opaque runs share attachments and one public submission, while each encoder
// owns its uniforms and geometry buffers. The inactive runs use the established
// Phong encoder, including its base texture and depth semantics.
pub(super) fn encode_mixed_bump_frame(
    ctx: &DeviceState,
    pipeline_cache: Option<&Mutex<HashMap<BumpPipelineKey, Arc<BumpPipelines>>>>,
    base_cache: Option<(&Mutex<BumpBaseCache>, u64, u64)>,
    vertices: &[CoinWgpuVertex],
    indices: &[u32],
    draws: &[CoinWgpuDraw],
    states: &[CoinWgpuRenderState],
    portable_modules: &[portable::Module],
    materials: &[CoinWgpuMaterial],
    textures: &[CoinWgpuTexture],
    samplers: &[CoinWgpuSampler],
    payload: &ValidatedBumpPayload,
    shadow_frame: Option<&ShadowFrame>,
    color_view: &wgpu::TextureView,
    resolve_view: Option<&wgpu::TextureView>,
    color_format: wgpu::TextureFormat,
    depth_view: &wgpu::TextureView,
    target_size: [u32; 2],
    clear_color: [f32; 4],
    sample_count: u32,
    depth_texture: &wgpu::Texture,
    sampling_policy: u32,
    outer_shadow: Option<&(Vec<shadow::ShadowMap>, shadow::ShadowArray)>,
) -> Result<(Vec<wgpu::CommandBuffer>, Option<InstancedGpuBuffers>), (CoinWgpuStatus, String)> {
    if !matches!(sample_count, 1 | 2 | 4)
        || resolve_view.is_some() != (sample_count > 1)
        || draws.is_empty()
        || payload.vertices.len() != vertices.len()
        || !clear_color.iter().all(|value| value.is_finite())
    {
        return Err((
            CoinWgpuStatus::InvalidArgument,
            "Invalid mixed bump attachments or geometry".into(),
        ));
    }
    let slots = bump_image_slots_for_draws(payload, indices, draws)
        .map_err(|message| (CoinWgpuStatus::InvalidArgument, message))?;
    // Validate the complete profile before any run allocates/uploads resources.
    // Lighting requirements belong only to active normal-map consumers;
    // ordinary runs retain their own BASE_COLOR/Phong surface executor.
    let extended = extended_bump_profile(states, textures, draws, shadow_frame, payload, indices);
    if extended {
        validate_extended_bump_profile(
            ctx,
            vertices,
            indices,
            draws,
            states,
            materials,
            textures,
            samplers,
            payload,
            shadow_frame,
            target_size,
        )?;
    } else {
        validate_bump_draw_profile(
            &ctx.device,
            vertices,
            indices,
            draws,
            states,
            materials,
            textures,
            samplers,
            target_size,
            &slots.iter().map(Option::is_some).collect::<Vec<_>>(),
        )?;
    }
    let extended_gpu = if extended {
        Some(
            upload_bump_payload(&ctx.device, &ctx.queue, payload)
                .map_err(|message| (CoinWgpuStatus::Unsupported, message))?,
        )
    } else {
        None
    };
    if payload.images.iter().any(|image| {
        image.width > ctx.device.limits().max_texture_dimension_2d
            || image.height > ctx.device.limits().max_texture_dimension_2d
    }) {
        return Err((
            CoinWgpuStatus::Unsupported,
            "Bump image exceeds GPU texture dimension limit".into(),
        ));
    }
    let mut commands = Vec::new();
    // One geometric caster pass per frame, shared by every ordered surface run.
    // Cloned handles retain the same allocations through the single queue submit.
    let owned_shadow = if extended && outer_shadow.is_none() {
        if let Some(shadow) = shadow_frame {
            let mut encoder = ctx
                .device
                .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                    label: Some("Coin shared bump receiver moments"),
                });
            let mut maps=Vec::new();
            maps.push(shadow::encode_moments(&ctx.device,&mut encoder,vertices,indices,
                shadow.casters,states,shadow.map_size,shadow.near,shadow.far,shadow.kind,false)
                .map_err(|message|(CoinWgpuStatus::InvalidArgument,message))?);
            for pass in shadow.second.iter().chain(shadow.third.iter())
                .chain(shadow.fourth.iter()).chain(shadow.extra.iter()) {
                maps.push(shadow::encode_moments(&ctx.device,&mut encoder,vertices,indices,
                    pass.casters,states,pass.map_size,pass.near,pass.far,pass.kind,false)
                    .map_err(|message|(CoinWgpuStatus::InvalidArgument,message))?);
            }
            let references:Vec<_>=maps.iter().collect();
            let array=shadow::encode_array(&ctx.device,&mut encoder,&references)
                .map_err(|message|(CoinWgpuStatus::Unsupported,message))?;
            commands.push(encoder.finish());
            Some((maps, array))
        } else {
            None
        }
    } else {
        None
    };
    let shared_shadow = outer_shadow.or(owned_shadow.as_ref());
    let mut start = 0;
    while start < draws.len() {
        let active = slots[start].is_some();
        let mut end = start + 1;
        while end < draws.len() && slots[end].is_some() == active && !(extended && active) {
            end += 1;
        }
        let run = &draws[start..end];
        let resolve = if end == draws.len() {
            resolve_view
        } else {
            None
        };
        if active && extended {
            let image = &extended_gpu.as_ref().unwrap().images[slots[start].unwrap()];
            commands.push(
                encode_extended_bump_draw(
                    ctx,
                    vertices,
                    indices,
                    &draws[start],
                    states,
                    materials,
                    textures,
                    samplers,
                    payload,
                    image,
                    shadow_frame,
                    color_view,
                    resolve,
                    color_format,
                    depth_view,
                    depth_texture,
                    target_size,
                    clear_color,
                    sample_count,
                    sampling_policy,
                    start == 0,
                    shared_shadow,
                )?
                .0,
            );
        } else if active {
            commands.push(encode_bump_opaque_run(
                &ctx.device,
                &ctx.queue,
                pipeline_cache,
                base_cache,
                vertices,
                indices,
                run,
                states,
                materials,
                textures,
                samplers,
                payload,
                color_view,
                resolve,
                color_format,
                depth_view,
                target_size,
                clear_color,
                sample_count,
                start == 0,
            )?);
        } else {
            let order = composition::order(vertices, indices, run, materials, states, textures)?;
            // Revision zero and no camera/instance proof keep per-run material
            // and uniform allocations independent before the single submit.
            let (command, _) = encode_frame_impl(
                ctx,
                0,
                clear_color,
                target_size[0],
                target_size[1],
                vertices,
                indices,
                run,
                &order,
                materials,
                states,
                portable_modules,
                textures,
                samplers,
                shadow_frame,
                None,
                &[],
                &[],
                None,
                color_view,
                resolve,
                color_format,
                depth_view,
                depth_texture,
                sample_count,
                0,
                sampling_policy,
                0,
                start == 0,
                None,
                shared_shadow,
            )?;
            commands.push(command);
        }
        start = end;
    }
    Ok((commands, None))
}
