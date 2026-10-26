//! Camera snapshot reuse and target depth initialization.

use super::*;

pub(super) fn manager_backgrounds(draws: &[CoinWgpuDraw]) -> bool {
    draws.iter().find(|draw| draw.composition_flags & 64 != 0)
        .is_some_and(|scene| draws.iter().any(|draw| draw.render_layer < scene.render_layer))
}

pub(super) fn initialize_target_depth(device: &wgpu::Device, view: &wgpu::TextureView) -> wgpu::CommandBuffer {
    let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
        label: Some("Coin target initial depth"),
    });
    {
        let _pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("Coin target initial depth"), color_attachments: &[],
            depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                view, depth_ops: Some(wgpu::Operations {
                    load: wgpu::LoadOp::Clear(1.0), store: wgpu::StoreOp::Store,
                }), stencil_ops: None,
            }), timestamp_writes: None, occlusion_query_set: None,
        });
    }
    encoder.finish()
}

pub(super) fn same_camera_independent_state(a: &CoinWgpuRenderState, b: &CoinWgpuRenderState) -> bool {
    if a.texture_projection != b.texture_projection || a.alpha_test_function != b.alpha_test_function ||
        a.alpha_test_reference.to_bits() != b.alpha_test_reference.to_bits() { return false; }
    a.light_direction == b.light_direction && a.light_color == b.light_color
        && a.light_intensity == b.light_intensity && a.has_light == b.has_light
        && a.material_slot == b.material_slot && a.flat_shadow_primary == b.flat_shadow_primary && a.cull_mode == b.cull_mode
        && a.front_face == b.front_face && a.light_model == b.light_model
        && a.texture_matrix == b.texture_matrix && a.has_texture == b.has_texture
        && a.texture_slot == b.texture_slot && a.sampler_slot == b.sampler_slot
        && a.raster_texture_source_slot == b.raster_texture_source_slot
        && a.raster_texture_coordinates == b.raster_texture_coordinates
        && a.extra_textures == b.extra_textures && a.texture_combines == b.texture_combines
        && a.texture_model == b.texture_model
        && a.texture_blend_color == b.texture_blend_color && a.viewport == b.viewport
        && a.light_count == b.light_count
        && a.ambient_light == b.ambient_light && a.fog_mode == b.fog_mode
        && a.fog_color == b.fog_color && a.fog_start == b.fog_start
        && a.depth_test == b.depth_test && a.depth_write == b.depth_write
        && a.color_write == b.color_write
        && a.depth_function == b.depth_function && a.depth_range == b.depth_range
        && a.polygon_offset_enabled == b.polygon_offset_enabled
        && a.polygon_offset_factor == b.polygon_offset_factor
        && a.polygon_offset_units == b.polygon_offset_units
        && a.polygon_offset_slope_bias == b.polygon_offset_slope_bias
        && a.polygon_offset_max_depth_bits == b.polygon_offset_max_depth_bits
        && a.polygon_offset_styles == b.polygon_offset_styles
        && a.polygon_offset_primitive_style == b.polygon_offset_primitive_style
        && a.clip_plane_count == b.clip_plane_count && a.clip_planes == b.clip_planes
        && a.lights.iter().zip(b.lights.iter()).all(|(x, y)| {
            x.position_type == y.position_type && x.direction_cutoff == y.direction_cutoff
                && x.color_intensity == y.color_intensity
                && x.attenuation_exponent == y.attenuation_exponent
                && x.ambient_color == y.ambient_color
        })
}

pub(super) fn camera_states_match(scene: &ValidatedScene, states: &[CoinWgpuRenderState]) -> bool {
    scene.states.len() == states.len() && scene.states.iter().zip(states).all(|(old, next)| {
        let immutable_state_matches = if (scene.geometry.opaque_batch || !scene.geometry.instances.is_empty()) && old.light_model == 1 {
            let mut a = *old;
            let mut b = *next;
            // Camera overlays resolve these coordinates from immutable world
            // light values. Keep all light types/colors/attenuation in the key.
            for state in [&mut a, &mut b] {
                state.light_direction[..3].fill(0.0);
                for light in &mut state.lights {
                    light.position_type[..3].fill(0.0);
                    light.direction_cutoff[..3].fill(0.0);
                }
            }
            same_camera_independent_state(&a, &b)
        } else {
            old.light_model == 0 && same_camera_independent_state(old, next)
        };
        if old.fog_mode != 0 || (old.has_texture != 0 || old.extra_textures.iter().any(|t| t.enabled != 0))
            || !immutable_state_matches
            || !next.model_view.iter().chain(next.model_view_projection.iter())
                .chain(next.normal_matrix.iter()).all(|x| x.is_finite())
            || !next.fog_end.is_finite() {
            return false;
        }
        // Composition preflight rejects non-finite view-space depth. Bound
        // the changed matrix against the previously validated positions.
        if !scene.geometry.instances.is_empty() && !instancing::camera_matrices_valid(next, scene.geometry.max_abs_position) {
            return false;
        }
        let m = &next.model_view;
        let bound = scene.geometry.max_abs_position
            * (f64::from(m[2]).abs() + f64::from(m[6]).abs() + f64::from(m[10]).abs())
            + f64::from(m[14]).abs();
        bound <= f64::from(f32::MAX)
    })
}

pub(super) fn opaque_camera_batch(states: &[CoinWgpuRenderState], draws: &[CoinWgpuDraw],
                       order: &[composition::CompositionItem]) -> bool {
    states.len() == 1 && draws.len() == 1 && order.len() == 1
        && matches!(states[0].light_model, 0 | 1)
        && states[0].fog_mode == 0 && states[0].clip_plane_count == 0
        && states[0].polygon_offset_enabled == 0 && states[0].has_texture == 0
        && states[0].extra_textures.iter().all(|t| t.enabled == 0)
        && draws[0].topology == 0 && draws[0].stable_node_id == 0
        && draws[0].composition_flags == 0 && draws[0].render_layer == 0
        && draws[0].clear_depth_before == 0 && draws[0].first_vertex == 0
        && draws[0].first_index == 0 && draws[0].vertex_count != 0
        && draws[0].index_count != 0 && draws[0].index_count % 3 == 0
        && !order[0].blend && !order[0].peel && !order[0].weighted
}

pub(super) fn camera_scene_eligible(
    vertices: &[CoinWgpuVertex], indices: &[u32], draws: &[CoinWgpuDraw],
    materials: &[CoinWgpuMaterial],
    states: &[CoinWgpuRenderState], order: &[composition::CompositionItem],
    textures: &[CoinWgpuTexture], samplers: &[CoinWgpuSampler],
) -> bool {
    let max_owned_scene_bytes = if opaque_camera_batch(states, draws, order) {
        256 * 1024 * 1024
    } else { 32 * 1024 * 1024 };
    let owned_bytes = std::mem::size_of_val(vertices)
        .saturating_add(std::mem::size_of_val(indices))
        .saturating_add(std::mem::size_of_val(draws))
        .saturating_add(std::mem::size_of_val(materials))
        .saturating_add(std::mem::size_of_val(states))
        .saturating_add(std::mem::size_of_val(order));
    owned_bytes <= max_owned_scene_bytes
        && textures.is_empty() && samplers.is_empty()
        && states.iter().all(|s| (s.light_model == 0 || opaque_camera_batch(states, draws, order))
            && s.fog_mode == 0 && s.has_texture == 0 && s.texture_projection <= 1
            && s.flat_shadow_primary == 0
            && s.extra_textures.iter().all(|t| t.enabled == 0))
        && materials.iter().all(|m| m.diffuse[3] == 1.0 && m.transparency == 0.0)
        && order.iter().all(|item| !item.blend)
        && vertices.iter().all(|v| v.position.iter().chain(v.normal.iter()).chain(v.texcoord.iter()).all(|x| x.is_finite())
            && v.extra_texcoords.iter().flatten().all(|x| x.is_finite())
            && v.screen_space_w.is_finite() && v.screen_space_w >= 0.0
            && v.fog_eye_depth_plus_one.is_finite() && v.fog_eye_depth_plus_one >= 0.0
            && (v.material_slot as usize) < materials.len()
            && (!opaque_camera_batch(states, draws, order) ||
                (v.screen_space_w == 1.0 && v.fog_eye_depth_plus_one == 0.0
                    && v.normal.iter().all(|x| x.abs() <= 1.0e18))))
}

pub(super) fn camera_base(device: &DeviceState, f: &CoinWgpuFrameView,
               states: &[CoinWgpuRenderState]) -> Option<Arc<ValidatedScene>> {
    owned_camera_base(device.validated_scene.as_ref()?, device.generation, f, states)
}

pub(super) fn owned_camera_base(scene: &Arc<ValidatedScene>, generation: u64, f: &CoinWgpuFrameView,
                     states: &[CoinWgpuRenderState]) -> Option<Arc<ValidatedScene>> {
    if f.portable_module_count != 0 || f.frame_revision == 0 || f.texture_count != 0 || f.sampler_count != 0 { return None; }
    let geometry = &scene.geometry;
    if !geometry.instances.is_empty() && instancing::validate_counts(f).is_err() { return None; }
    if scene.generation != generation || scene.width != f.width || scene.height != f.height
        || scene.clear_color != f.clear_color
        || geometry.vertices.len() as u64 != f.vertex_count
        || geometry.indices.len() as u64 != f.index_count
        || geometry.draws.len() as u64 != f.draw_count
        || geometry.materials.len() as u64 != f.material_count
        || geometry.instances.len() as u64 != f.instance_count
        || geometry.instance_ranges.len() as u64 != f.instance_range_count { return None; }
    let exact_state = scene.states.len() == states.len() && scene.states.iter().zip(states).all(|(a, b)|
        same_camera_independent_state(a, b) && a.fog_end == b.fog_end
            && a.model_view == b.model_view && a.model_view_projection == b.model_view_projection
            && a.normal_matrix == b.normal_matrix);
    // A full instanced payload can change under the same caller revision.
    // Exact bytes from bounded, aligned slices are required before skipping
    // validation; only a null immutable payload directly licenses our Arc.
    let exact = f.camera_base_revision == 0 && scene.revision == f.frame_revision && exact_state
        && (geometry.instances.is_empty() || instancing::owned_payload_matches(geometry, f));
    let patch = f.camera_base_revision != 0 && f.frame_revision != f.camera_base_revision
        && scene.revision == f.camera_base_revision && camera_states_match(scene, states);
    (exact || patch).then(|| scene.clone())
}

// Build only after ordinary draw/index/composition preflight. Allocation is
// bounded and fallible: an unavailable snapshot simply keeps the full path.
pub(super) fn camera_scene(generation: u64, f: &CoinWgpuFrameView,
    vertices: &[CoinWgpuVertex], indices: &[u32], draws: &[CoinWgpuDraw],
    materials: &[CoinWgpuMaterial], states: &[CoinWgpuRenderState],
    order: &[composition::CompositionItem], textures: &[CoinWgpuTexture], samplers: &[CoinWgpuSampler],
    instances: &[CoinWgpuInstance], instance_ranges: &[CoinWgpuInstanceRange],
    base: Option<&Arc<ValidatedScene>>) -> Option<Arc<ValidatedScene>> {
    if f.frame_revision == 0 && instances.is_empty() { return None; }
    fn copy<T: Clone>(source: &[T]) -> Option<Vec<T>> {
        let mut target = Vec::new();
        target.try_reserve_exact(source.len()).ok()?;
        target.extend_from_slice(source);
        Some(target)
    }
    let trace_resources = std::env::var_os("COIN_RENDER_TRACE_PHASES")
        .or_else(|| std::env::var_os("COIN_WGPU_TRACE_PHASES")).is_some();
    let profile_snapshot = trace_resources.then(std::time::Instant::now);
    let geometry = if let Some(base) = base {
        base.geometry.clone()
    } else {
        if instances.is_empty() && !camera_scene_eligible(vertices, indices, draws, materials, states, order, textures, samplers) {
            return None;
        }
        Arc::new(ValidatedGeometry {
            vertices: copy(vertices)?, indices: copy(indices)?, draws: copy(draws)?, materials: copy(materials)?,
            max_abs_position: instancing::position_bound(vertices, instances),
            opaque_batch: opaque_camera_batch(states, draws, order),
            instances: copy(instances)?, instance_ranges: copy(instance_ranges)?,
        })
    };
    let scene = Arc::new(ValidatedScene {
        revision: f.frame_revision, generation, width: f.width, height: f.height,
        clear_color: f.clear_color, geometry, states: copy(states)?, draw_order: copy(order)?,
    });
    if let Some(start) = profile_snapshot {
        // Bytes are the copied payloads, not allocator capacity. This timer
        // spans the entire scene snapshot, including bounds/states/order; it
        // is not isolated material memcpy and precedes submission success.
        eprintln!("COIN_RENDER_PHASE rust_material_snapshot source_bytes={} material_copied_bytes={} instance_copied_bytes={} geometry_reused={} snapshot_ms={} attempt=1",
            std::mem::size_of_val(materials), if base.is_some() { 0 } else { std::mem::size_of_val(materials) },
            if base.is_some() { 0 } else { std::mem::size_of_val(instances) }, base.is_some() as u32,
            start.elapsed().as_secs_f64() * 1000.0);
    }
    Some(scene)
}

pub(super) fn commit_camera_scene(device: &mut DeviceState, scene: Option<Arc<ValidatedScene>>,
                       update: bool, revision: u64, owned_base: bool, capture: bool,
                       instanced: bool, pending_instances: Option<InstancedGpuBuffers>) {
    if !update { return; }
    *device.instanced_buffers.lock().unwrap() = if instanced { pending_instances } else { None };
    device.camera_snapshot_policy.commit(capture, scene.is_some());
    device.validated_scene = scene;
    if owned_base && !instanced {
        if let Ok(mut cache) = VALIDATED_FRAME_CACHE.lock() {
            if let Some(scene) = device.validated_scene.as_ref() {
                *cache = Some((revision, scene.draw_order.clone()));
            }
        }
    }
}
