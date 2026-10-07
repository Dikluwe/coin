//! Bounded private opaque profile. No expanded vertices or mutable GPU rewrites.
use super::*;

pub(super) const MAX_GEOMETRY_BYTES: u64 = 8 * 1024 * 1024;
pub(super) const MAX_INSTANCE_COUNT: u64 = 1_048_576;
pub(super) const MAX_INSTANCE_BYTES: u64 = 160 * 1024 * 1024;
pub(super) const MAX_MATERIAL_BYTES: u64 = 32 * 1024 * 1024;

// The public/private CPU transport remains 144 bytes (ABI 49). Only affine
// rows needed by the vertex shader are uploaded, without quantizing floats.
#[repr(C)]
#[derive(Clone, Copy, bytemuck::Pod, bytemuck::Zeroable)]
pub(super) struct GpuInstance {
    model_rows: [[f32; 4]; 3],
    normal_row0: [f32; 3],
    material_slot: u32,
    normal_row1: [f32; 4],
    normal_row2: [f32; 4],
}
const _: () = {
    assert!(std::mem::size_of::<GpuInstance>() == 96);
    assert!(std::mem::offset_of!(GpuInstance, material_slot) == 60);
};
pub(super) fn gpu_bytes(count: usize) -> u64 {
    count as u64 * std::mem::size_of::<GpuInstance>() as u64
}
pub(super) fn pack_gpu(instances: &[CoinWgpuInstance]) -> Vec<GpuInstance> {
    instances.iter().map(|instance| {
        let row = |matrix: &[f32; 16], r: usize| [matrix[r], matrix[4+r], matrix[8+r], matrix[12+r]];
        GpuInstance {
            model_rows: std::array::from_fn(|r| row(&instance.model_view, r)),
            normal_row0: [instance.normal_matrix[0], instance.normal_matrix[4], instance.normal_matrix[8]],
            material_slot: instance.material_slot,
            normal_row1: [instance.normal_matrix[1], instance.normal_matrix[5], instance.normal_matrix[9], 0.0],
            normal_row2: [instance.normal_matrix[2], instance.normal_matrix[6], instance.normal_matrix[10], 0.0],
        }
    }).collect()
}

fn profile_transport_supported(f: &CoinWgpuFrameView) -> bool {
    f.texture_count == 0 && f.sampler_count == 0 && f.transparency_reserved == 0
        && f.shadow_map_size == 0 && f.shadow_map_size_second == 0
        && f.shadow_map_size_third == 0 && f.shadow_map_size_fourth == 0
        && f.shadow_kind == 0 && f.shadow_kind_second == 0
        && f.shadow_kind_third == 0 && f.shadow_kind_fourth == 0
        && f.shadow_caster_count == 0 && f.shadow_receiver_count == 0
        && f.shadow_caster_count_second == 0 && f.shadow_receiver_count_second == 0
        && f.shadow_caster_count_third == 0 && f.shadow_receiver_count_third == 0
        && f.shadow_caster_count_fourth == 0 && f.shadow_receiver_count_fourth == 0
        && f.extra_shadow_pass_count == 0 && f.extra_shadow_passes.is_null()
        && f.shadow_casters.is_null() && f.shadow_receivers.is_null()
        && f.shadow_casters_second.is_null() && f.shadow_receivers_second.is_null()
        && f.shadow_casters_third.is_null() && f.shadow_receivers_third.is_null()
        && f.shadow_casters_fourth.is_null() && f.shadow_receivers_fourth.is_null()
}

pub(super) fn validate_counts(f: &CoinWgpuFrameView) -> Result<(), (CoinWgpuStatus, String)> {
    if f.instance_count == 0 && f.instance_range_count == 0 { return Ok(()); }
    // This runs on static/camera hits too, before any owned-base decision.
    if !profile_transport_supported(f) {
        return Err((CoinWgpuStatus::Unsupported,
            "Instancing excludes texture, shadow and transparency transport".into()));
    }
    if f.instance_count == 0 || f.instance_range_count == 0 || f.instance_range_count > 128
        || f.instance_range_count != f.draw_count || f.state_count != 1 {
        return Err((CoinWgpuStatus::InvalidArgument,
            "Instancing requires nonempty instances and one contiguous range per draw (at most 128), with one common state".into()));
    }
    let geometry = f.vertex_count.checked_mul(std::mem::size_of::<CoinWgpuVertex>() as u64)
        .and_then(|bytes| f.index_count.checked_mul(4).and_then(|indices| bytes.checked_add(indices)));
    let instance_bytes = f.instance_count.checked_mul(std::mem::size_of::<CoinWgpuInstance>() as u64);
    let material_bytes = f.material_count.checked_mul(std::mem::size_of::<CoinWgpuMaterial>() as u64);
    if !geometry.is_some_and(|bytes| bytes != 0 && bytes <= MAX_GEOMETRY_BYTES)
        || !instance_bytes.is_some_and(|bytes| bytes <= MAX_INSTANCE_BYTES)
        || !material_bytes.is_some_and(|bytes| bytes <= MAX_MATERIAL_BYTES)
        || f.instance_count > MAX_INSTANCE_COUNT {
        return Err((CoinWgpuStatus::Unsupported,
            "Instancing exceeds bounded canonical geometry, instance or material payload limits".into()));
    }
    Ok(())
}

pub(super) fn validate_payload(f: &CoinWgpuFrameView, vertices: &[CoinWgpuVertex],
    indices: &[u32], draws: &[CoinWgpuDraw], materials: &[CoinWgpuMaterial],
    states: &[CoinWgpuRenderState], instances: &[CoinWgpuInstance], ranges: &[CoinWgpuInstanceRange],
) -> Result<(), (CoinWgpuStatus, String)> {
    if instances.is_empty() && ranges.is_empty() { return Ok(()); }
    if states.len() != 1 || draws.is_empty() || indices.is_empty() || vertices.is_empty()
        || materials.is_empty() || ranges.len() != draws.len() {
        return Err((CoinWgpuStatus::InvalidArgument, "Partial instanced payload".into()));
    }
    let s = &states[0];
    if !profile_transport_supported(f)
        || s.has_texture != 0 || s.extra_textures.iter().any(|t| t.enabled != 0)
        || s.fog_mode != 0 || s.clip_plane_count != 0 || s.polygon_offset_enabled != 0
        || !matches!(s.light_model, 0 | 1) {
        return Err((CoinWgpuStatus::Unsupported,
            "Instancing supports only opaque triangles without textures, fog, clipping, shadows or polygon offset".into()));
    }
    if !s.model_view.iter().chain(s.model_view_projection.iter()).chain(s.normal_matrix.iter()).all(|v| v.is_finite()) {
        return Err((CoinWgpuStatus::InvalidArgument, "Non-finite common instance matrices".into()));
    }
    for (i, m) in materials.iter().enumerate() {
        if !m.ambient.iter().chain(m.diffuse.iter()).chain(m.specular.iter()).chain(m.emission.iter())
            .chain([&m.shininess, &m.transparency]).all(|v| v.is_finite())
            || m.diffuse[3] != 1.0 || m.transparency != 0.0 {
            return Err((CoinWgpuStatus::InvalidArgument, format!("Instance material {i} is non-finite or not opaque")));
        }
    }
    for (i, v) in vertices.iter().enumerate() {
        if !v.position.iter().chain(v.normal.iter()).chain(v.texcoord.iter())
            .chain(v.extra_texcoords.iter().flatten()).all(|v| v.is_finite())
            || v.screen_space_w != 1.0 || v.fog_eye_depth_plus_one != 0.0
            || v.material_slot as usize >= materials.len() {
            return Err((CoinWgpuStatus::InvalidArgument, format!("Invalid canonical instance vertex {i}")));
        }
    }
    let position = vertices.iter().flat_map(|v| v.position).map(|v| f64::from(v).abs())
        .fold(0.0, f64::max);
    let normal = vertices.iter().flat_map(|v| v.normal).map(|v| f64::from(v).abs())
        .fold(0.0, f64::max);
    for (i, instance) in instances.iter().enumerate() {
        if instance.reserved != [0; 3] || instance.material_slot as usize >= materials.len()
            || !instance.model_view.iter().chain(instance.normal_matrix.iter()).all(|v| v.is_finite())
            || instance.model_view[3] != 0.0 || instance.model_view[7] != 0.0
            || instance.model_view[11] != 0.0 || instance.model_view[15] != 1.0
            || (0..3).any(|r| matrix_bound(&instance.model_view, position, r, true) > 1.0e30
                || matrix_bound(&instance.normal_matrix, normal, r, false) > 1.0e18) {
            return Err((CoinWgpuStatus::InvalidArgument, format!("Invalid instance {i}: matrices, material or reserved fields")));
        }
    }
    let eye_bound = position_bound(vertices, instances);
    if (0..4).any(|r| matrix_bound(&s.model_view, eye_bound, r, true) > f64::from(f32::MAX)
        || matrix_bound(&s.model_view_projection, eye_bound, r, true) > f64::from(f32::MAX)
        || matrix_bound(&s.normal_matrix, 1.0e18, r, false) > f64::from(f32::MAX)) {
        return Err((CoinWgpuStatus::InvalidArgument, "Common matrices overflow instance positions/normals".into()));
    }
    let mut next = 0_u32;
    for (i, range) in ranges.iter().enumerate() {
        let draw = &draws[i];
        if range.reserved != 0 || range.draw_index as usize != i || range.first_instance != next
            || range.instance_count == 0 || draw.topology != 0 || draw.stable_node_id != 0
            || draw.composition_flags != 0 || draw.render_state_slot != 0
            || draw.render_layer != 0 || draw.clear_depth_before != 0
            || draw.index_count == 0 || draw.index_count % 3 != 0 || draw.vertex_count == 0 {
            return Err((CoinWgpuStatus::InvalidArgument, format!("Invalid instanced draw/range {i}")));
        }
        next = next.checked_add(range.instance_count).ok_or_else(||
            (CoinWgpuStatus::InvalidArgument, "Instance range overflow".to_string()))?;
        if next as usize > instances.len() {
            return Err((CoinWgpuStatus::InvalidArgument, "Instance range exceeds instance_count".into()));
        }
    }
    if next as usize != instances.len() {
        return Err((CoinWgpuStatus::InvalidArgument, "Instance ranges must cover the complete payload".into()));
    }
    Ok(())
}

fn matrix_bound(matrix: &[f32; 16], value: f64, row: usize, translation: bool) -> f64 {
    value * (f64::from(matrix[row]).abs() + f64::from(matrix[4 + row]).abs()
        + f64::from(matrix[8 + row]).abs())
        + if translation { f64::from(matrix[12 + row]).abs() } else { 0.0 }
}

pub(super) fn camera_matrices_valid(s: &CoinWgpuRenderState, position: f64) -> bool {
    (0..4).all(|r| matrix_bound(&s.model_view, position, r, true) <= f64::from(f32::MAX)
        && matrix_bound(&s.model_view_projection, position, r, true) <= f64::from(f32::MAX)
        && matrix_bound(&s.normal_matrix, 1.0e18, r, false) <= f64::from(f32::MAX))
}

pub(super) fn position_bound(vertices: &[CoinWgpuVertex], instances: &[CoinWgpuInstance]) -> f64 {
    let position = vertices.iter().flat_map(|v| v.position).map(|v| f64::from(v).abs()).fold(0.0, f64::max);
    if instances.is_empty() { return position; }
    instances.iter().flat_map(|i| (0..3).map(move |r| matrix_bound(&i.model_view, position, r, true)))
        .fold(0.0, f64::max)
}

pub(super) fn same_geometry(previous: &ValidatedGeometry, vertices: &[CoinWgpuVertex], indices: &[u32]) -> bool {
    bytemuck::cast_slice::<_, u8>(&previous.vertices) == bytemuck::cast_slice::<_, u8>(vertices)
        && previous.indices == indices
}

// Both resources are immutable and belong to the last successful submission.
// Their exact byte proofs are independent: a material change cannot make an
// equal instance buffer stale, and a model change cannot alter material bytes.
// The caller still requires the existing validated instanced geometry profile.
pub(super) fn payload_hits(previous: &ValidatedGeometry, instances: &[CoinWgpuInstance],
                          materials: &[CoinWgpuMaterial], separate: bool) -> (bool, bool) {
    let instance_hit = bytemuck::cast_slice::<_, u8>(&previous.instances)
        == bytemuck::cast_slice::<_, u8>(instances);
    if separate {
        let material_hit = bytemuck::cast_slice::<_, u8>(&previous.materials)
            == bytemuck::cast_slice::<_, u8>(materials);
        (instance_hit, material_hit)
    } else {
        // Keep the literal short-circuit and coupled resource invalidation.
        let hit = instance_hit && bytemuck::cast_slice::<_, u8>(&previous.materials)
            == bytemuck::cast_slice::<_, u8>(materials);
        (hit, hit)
    }
}

pub(super) fn owned_payload_matches(previous: &ValidatedGeometry, f: &CoinWgpuFrameView) -> bool {
    if validate_counts(f).is_err() { return false; }
    if f.vertices.is_null() && f.indices.is_null() && f.draws.is_null()
        && f.materials.is_null() && f.instances.is_null() && f.instance_ranges.is_null() { return true; }
    fn matches<T: Pod>(owned: &[T], pointer: *const T, count: u64) -> bool {
        if owned.len() as u64 != count { return false; }
        let incoming = match validate_slice(pointer, count, "static instance payload", std::ptr::null_mut(), 0) {
            Ok(slice) => slice, Err(_) => return false,
        };
        bytemuck::cast_slice::<_, u8>(owned) == bytemuck::cast_slice::<_, u8>(incoming)
    }
    matches(&previous.vertices, f.vertices, f.vertex_count)
        && matches(&previous.indices, f.indices, f.index_count)
        && matches(&previous.draws, f.draws, f.draw_count)
        && matches(&previous.materials, f.materials, f.material_count)
        && matches(&previous.instances, f.instances, f.instance_count)
        && matches(&previous.instance_ranges, f.instance_ranges, f.instance_range_count)
}

// Specialize the existing PHONG implementation, keeping its lighting arithmetic
// and fragment behavior. The common matrices transform already eye-space
// instance positions when a camera overlay is used.
pub(super) fn shader_source() -> String {
    let mut source = shader_profile::without_shadows(WGSL_SHADER);
    source.insert_str(0, "struct CoinInstance {\n    model_rows: array<vec4<f32>, 3>,\n    normal_row0: vec3<f32>,\n    material_slot: u32,\n    normal_row1: vec4<f32>,\n    normal_row2: vec4<f32>,\n};\n@group(0) @binding(24) var<storage, read> instances: array<CoinInstance>;\n");
    let old = "fn vs_main(input: VertexInput) -> VertexOutput {";
    assert_eq!(source.matches(old).count(), 1);
    source = source.replace(old, "fn vs_main(input: VertexInput, @builtin(instance_index) instance_index: u32) -> VertexOutput {\n    let instance = instances[instance_index];\n    let a = instance.model_rows[0]; let b = instance.model_rows[1]; let c = instance.model_rows[2];\n    let model = mat4x4<f32>(vec4<f32>(a.x,b.x,c.x,0.0),vec4<f32>(a.y,b.y,c.y,0.0),vec4<f32>(a.z,b.z,c.z,0.0),vec4<f32>(a.w,b.w,c.w,1.0));\n    let n0 = instance.normal_row0; let n1 = instance.normal_row1.xyz; let n2 = instance.normal_row2.xyz;\n    let normal = mat4x4<f32>(vec4<f32>(n0.x,n1.x,n2.x,0.0),vec4<f32>(n0.y,n1.y,n2.y,0.0),vec4<f32>(n0.z,n1.z,n2.z,0.0),vec4<f32>(0.0,0.0,0.0,1.0));\n    let instance_position = model * vec4<f32>(input.position, 1.0);\n    let instance_normal = normal * vec4<f32>(input.normal, 0.0);");
    let start = source.find("fn vs_main(").unwrap();
    let end = start + source[start..].find("\n}").unwrap() + 2;
    let vertex = source[start..end].replace("u.model_view * vec4<f32>(input.position, 1.0)", "u.model_view * instance_position")
        .replace("u.model_view_projection * vec4<f32>(input.position, 1.0)", "u.model_view_projection * instance_position")
        .replace("u.normal_matrix * vec4<f32>(input.normal, 0.0)", "u.normal_matrix * vec4<f32>(instance_normal.xyz, 0.0)")
        .replace("materials[input.material_slot]", "materials[instance.material_slot]")
        .replace("output.material_slot = input.material_slot;", "output.material_slot = instance.material_slot;");
    source.replace_range(start..end, &vertex);
    source
}

#[cfg(test)]
mod tests {
    use super::*;
    use naga::valid::{Capabilities, ValidationFlags, Validator};

    fn fixture() -> (CoinWgpuFrameView, [CoinWgpuVertex; 3], [CoinWgpuDraw; 1],
        [CoinWgpuMaterial; 1], [CoinWgpuRenderState; 1], [CoinWgpuInstance; 2], [CoinWgpuInstanceRange; 1]) {
        let mut frame: CoinWgpuFrameView = unsafe { std::mem::zeroed() };
        frame.frame_revision = 1; frame.width = 16; frame.height = 16;
        frame.vertex_count = 3; frame.index_count = 3; frame.draw_count = 1;
        frame.material_count = 1; frame.state_count = 1;
        frame.instance_count = 2; frame.instance_range_count = 1;
        let mut vertices = [CoinWgpuVertex::zeroed(); 3];
        for v in &mut vertices { v.screen_space_w = 1.0; v.normal[2] = 1.0; }
        let mut draw = CoinWgpuDraw::zeroed(); draw.index_count = 3; draw.vertex_count = 3;
        let mut material = CoinWgpuMaterial::zeroed(); material.diffuse[3] = 1.0;
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        state.light_model = 1;
        let mut instance = CoinWgpuInstance::zeroed();
        for i in 0..4 {
            instance.model_view[i*5] = 1.0; instance.normal_matrix[i*5] = 1.0;
            state.model_view[i*5] = 1.0; state.model_view_projection[i*5] = 1.0; state.normal_matrix[i*5] = 1.0;
        }
        (frame, vertices, [draw], [material], [state], [instance; 2],
            [CoinWgpuInstanceRange { draw_index: 0, first_instance: 0, instance_count: 2, reserved: 0 }])
    }

    #[test]
    fn gpu_rows_preserve_affine_transforms_normal_and_integer_material() {
        let mut source = CoinWgpuInstance::zeroed();
        source.model_view = [2.0,3.0,4.0,0.0, 5.0,6.0,7.0,0.0, 8.0,9.0,10.0,0.0, 11.0,12.0,13.0,1.0];
        source.normal_matrix = [0.25,0.5,0.75,0.0, 1.0,1.25,1.5,0.0, 1.75,2.0,2.25,0.0, 0.0,0.0,0.0,1.0];
        source.material_slot = 0xff00_1234;
        let gpu = pack_gpu(&[source]);
        assert_eq!(gpu[0].model_rows[0], [2.0,5.0,8.0,11.0]);
        assert_eq!(gpu[0].model_rows[1], [3.0,6.0,9.0,12.0]);
        assert_eq!(gpu[0].model_rows[2], [4.0,7.0,10.0,13.0]);
        assert_eq!(gpu[0].normal_row0, [0.25,1.0,1.75]);
        assert_eq!(gpu[0].normal_row1, [0.5,1.25,2.0,0.0]);
        assert_eq!(gpu[0].normal_row2, [0.75,1.5,2.25,0.0]);
        assert_eq!(gpu[0].material_slot, source.material_slot);
        assert_eq!(gpu_bytes(1_000_001), 96_000_096);
        let (mut frame, ..) = fixture();
        frame.instance_count = MAX_INSTANCE_COUNT;
        assert!(validate_counts(&frame).is_ok());
        frame.instance_count += 1;
        assert!(validate_counts(&frame).is_err());
    }

    #[test]
    fn homogeneous_coordinates_are_part_of_owned_geometry_and_admission() {
        let (frame, mut vertices, draws, materials, states, instances, ranges) = fixture();
        let order = [composition::CompositionItem { draw_index: 0, blend: false, additive: false, legacy_blend_alpha: false,
            screen_door: false, screen_door_level: 0, peel: false, weighted: false }];
        let base = camera_scene(7, &frame, &vertices, &[0, 1, 2], &draws, &materials,
            &states, &order, &[], &[], &instances, &ranges, None).unwrap();
        assert!(same_geometry(&base.geometry, &vertices, &[0, 1, 2]));
        vertices[0].texcoord[2] = 0.25;
        assert!(!same_geometry(&base.geometry, &vertices, &[0, 1, 2]));
        vertices[0].texcoord[2] = 0.0;
        vertices[0].extra_texcoords[6][3] = 2.0;
        assert!(!same_geometry(&base.geometry, &vertices, &[0, 1, 2]));
        assert!(validate_payload(&frame, &vertices, &[0, 1, 2], &draws, &materials,
            &states, &instances, &ranges).is_ok());
        vertices[0].extra_texcoords[6][3] = f32::INFINITY;
        assert!(validate_payload(&frame, &vertices, &[0, 1, 2], &draws, &materials,
            &states, &instances, &ranges).is_err());
        vertices[0].extra_texcoords[6][3] = 0.0;
        vertices[0].texcoord[2] = f32::NAN;
        assert!(validate_payload(&frame, &vertices, &[0, 1, 2], &draws, &materials,
            &states, &instances, &ranges).is_err());
    }

    #[test]
    fn rejects_partial_overflow_nonfinite_slots_and_noncontiguous_ranges() {
        let (mut frame, vertices, draws, mut materials, states, mut instances, mut ranges) = fixture();
        let valid = |instances: &[CoinWgpuInstance], ranges: &[CoinWgpuInstanceRange], materials: &[CoinWgpuMaterial]| {
            validate_payload(&frame, &vertices, &[0,1,2], &draws, materials, &states, instances, ranges)
        };
        assert!(valid(&instances, &ranges, &materials).is_ok());
        instances[0].reserved[1] = 1; assert!(valid(&instances, &ranges, &materials).is_err());
        instances[0].reserved[1] = 0;
        instances[1].material_slot = 1; assert!(valid(&instances, &ranges, &materials).is_err());
        instances[1].material_slot = 0;
        instances[1].normal_matrix[0] = f32::NAN; assert!(valid(&instances, &ranges, &materials).is_err());
        instances[1].normal_matrix[0] = 1.0;
        ranges[0].first_instance = 1; assert!(valid(&instances, &ranges, &materials).is_err());
        ranges[0].first_instance = 0;
        ranges[0].instance_count = 1; assert!(valid(&instances, &ranges, &materials).is_err());
        ranges[0].instance_count = 2;
        ranges[0].draw_index = 1; assert!(valid(&instances, &ranges, &materials).is_err());
        ranges[0].draw_index = 0;
        materials[0].ambient[0] = f32::INFINITY; assert!(valid(&instances, &ranges, &materials).is_err());
        assert!(validate_counts(&frame).is_ok());
        frame.instance_count = u64::MAX;
        assert_eq!(validate_counts(&frame).unwrap_err().0, CoinWgpuStatus::Unsupported);
        frame.instance_count = 2; frame.instance_range_count = 0;
        assert_eq!(validate_counts(&frame).unwrap_err().0, CoinWgpuStatus::InvalidArgument);
    }

    #[test]
    fn bounded_owned_scene_compares_full_bytes_and_camera_reuses_its_arc() {
        let (mut frame, mut vertices, draws, materials, mut states, mut instances, ranges) = fixture();
        instances[0].model_view[12] = 2.0;
        let order = [composition::CompositionItem { draw_index: 0, blend: false, additive: false, legacy_blend_alpha: false,
            screen_door: false, screen_door_level: 0, peel: false, weighted: false }];
        let base = camera_scene(7, &frame, &vertices, &[0,1,2], &draws, &materials,
            &states, &order, &[], &[], &instances, &ranges, None).unwrap();
        assert_eq!(base.geometry.max_abs_position, 2.0, "camera bound uses transformed eye-space payload");
        frame.vertices = vertices.as_ptr();
        // The index pointer below must have storage alive while exact comparisons run.
        let indices = [0_u32,1,2]; frame.indices = indices.as_ptr();
        frame.draws = draws.as_ptr(); frame.materials = materials.as_ptr();
        frame.instances = instances.as_ptr(); frame.instance_ranges = ranges.as_ptr();
        assert!(owned_camera_base(&base, 7, &frame, &states).is_some());
        frame.shadow_receiver_count_second = 1;
        assert!(validate_counts(&frame).is_err());
        assert!(owned_camera_base(&base, 7, &frame, &states).is_none());
        frame.shadow_receiver_count_second = 0;
        instances[0].model_view[12] = 3.0;
        assert!(owned_camera_base(&base, 7, &frame, &states).is_none(), "same revision is no proof of new contents");
        instances[0].model_view[12] = 2.0;
        vertices[0].position[0] = -0.0;
        assert!(owned_camera_base(&base, 7, &frame, &states).is_none(), "geometry must compare exact bytes");
        frame.vertices = std::ptr::null(); frame.indices = std::ptr::null(); frame.draws = std::ptr::null();
        frame.materials = std::ptr::null(); frame.instances = std::ptr::null(); frame.instance_ranges = std::ptr::null();
        frame.camera_base_revision = 1; frame.frame_revision = 2;
        states[0].model_view[12] = 0.125; states[0].model_view_projection[12] = 0.125;
        let owned = owned_camera_base(&base, 7, &frame, &states).unwrap();
        let next = camera_scene(7, &frame, &owned.geometry.vertices, &owned.geometry.indices,
            &owned.geometry.draws, &owned.geometry.materials, &states, &order, &[], &[],
            &owned.geometry.instances, &owned.geometry.instance_ranges, Some(&owned)).unwrap();
        assert!(Arc::ptr_eq(&base.geometry, &next.geometry));
        assert!(owned_camera_base(&base, 8, &frame, &states).is_none());
        states[0].model_view_projection[0] = f32::MAX;
        assert!(owned_camera_base(&base, 7, &frame, &states).is_none(), "camera projection cannot overflow owned positions");
    }

    #[test]
    fn material_and_instance_resources_compare_independent_owned_bytes() {
        let (mut frame, vertices, draws, materials, states, instances, ranges) = fixture();
        let order = [composition::CompositionItem { draw_index: 0, blend: false, additive: false, legacy_blend_alpha: false,
            screen_door: false, screen_door_level: 0, peel: false, weighted: false }];
        let base = camera_scene(7, &frame, &vertices, &[0, 1, 2], &draws, &materials,
            &states, &order, &[], &[], &instances, &ranges, None).unwrap();
        let owned = &base.geometry;
        assert_eq!(payload_hits(owned, &instances, &materials, true), (true, true));
        assert_eq!(payload_hits(owned, &instances, &materials, false), (true, true));
        let mut updated_materials = materials;
        updated_materials[0].diffuse[0] = 0.25;
        assert_eq!(payload_hits(owned, &instances, &updated_materials, true), (true, false));
        assert_eq!(payload_hits(owned, &instances, &updated_materials, false), (false, false));
        let mut updated_instances = instances;
        updated_instances[1].model_view[12] = 2.0;
        assert_eq!(payload_hits(owned, &updated_instances, &materials, true), (false, true));
        assert_eq!(payload_hits(owned, &updated_instances, &materials, false), (false, false));
        assert_eq!(payload_hits(owned, &updated_instances, &updated_materials, true), (false, false));
        // Exact source bytes include signed zeros, slots and all fields.
        updated_materials = materials;
        updated_materials[0].emission[2] = -0.0;
        assert_eq!(payload_hits(owned, &instances, &updated_materials, true), (true, false));
        updated_instances = instances;
        updated_instances[1].normal_matrix[1] = -0.0;
        assert_eq!(payload_hits(owned, &updated_instances, &materials, true), (false, true));
        updated_instances = instances;
        updated_instances[1].material_slot = 1;
        assert_eq!(payload_hits(owned, &updated_instances, &materials, true), (false, true));
        assert!(validate_payload(&frame, &vertices, &[0, 1, 2], &draws, &materials,
            &states, &updated_instances, &ranges).is_err(), "resource equality never replaces payload admission");
        assert_eq!(payload_hits(owned, &instances[..1], &materials, true), (false, true));
        assert_eq!(payload_hits(owned, &instances, &[], true), (true, false));
        // A late, unused material participates in proof just like any slot.
        // Mutating caller memory cannot change the successful owned snapshot.
        let mut many_materials = vec![materials[0]; 4097];
        frame.material_count = many_materials.len() as u64;
        let many = camera_scene(7, &frame, &vertices, &[0, 1, 2], &draws, &many_materials,
            &states, &order, &[], &[], &instances, &ranges, None).unwrap();
        many_materials[4096].ambient[3] = 0.5;
        assert_eq!(payload_hits(&many.geometry, &instances, &many_materials, true), (true, false));
        many_materials[4096].ambient[3] = 0.0;
        assert_eq!(payload_hits(&many.geometry, &instances, &many_materials, true), (true, true));
        assert_eq!(many.geometry.materials[4096].ambient[3].to_bits(), 0.0f32.to_bits());
        frame.material_count = 1;
        updated_instances[1].material_slot = 0;
        updated_instances[1].model_view[12] = 2.0;
        let next = camera_scene(7, &frame, &vertices, &[0, 1, 2], &draws, &materials,
            &states, &order, &[], &[], &updated_instances, &ranges, None).unwrap();
        assert_eq!(payload_hits(owned, &updated_instances, &materials, true), (false, true));
        assert_eq!(payload_hits(&next.geometry, &updated_instances, &materials, true), (true, true));
    }

    #[test]
    fn shader_layout_matches_private_instance_abi_and_validates() {
        for gl in [false, true] {
            let source = shader_profile::depth_load_profile(&shader_profile::texture_specialization(
                &format!("{}{}", shader_source(), weighted::FRAGMENT)), gl);
            let module = naga::front::wgsl::parse_str(&source).expect("parse instance shader");
            Validator::new(ValidationFlags::all(), Capabilities::all())
                .validate(&module).expect("validate instance shader");
            let instance = module.types.iter().find(|(_, t)| t.name.as_deref() == Some("CoinInstance")).unwrap().1;
            match &instance.inner {
                naga::TypeInner::Struct { members, span } => {
                    assert_eq!(*span, 96);
                    assert_eq!(members[2].offset, 60);
                    assert_eq!(members[4].offset, 80);
                }
                _ => panic!("instance must be a WGSL struct"),
            }
            assert!(module.entry_points.iter().any(|p| p.name == "vs_main"));
            assert!(source.contains("u.model_view_projection * instance_position"));
            assert!(source.contains("materials[instance.material_slot]"));
        }
    }
}
