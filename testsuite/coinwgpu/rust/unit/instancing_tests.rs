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
        state.color_write = 1;
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
    fn flat_shadow_primary_is_a_boolean_abi_field() {
        let (frame, vertices, draws, materials, mut states, instances, ranges) = fixture();
        let accepted = |states: &[CoinWgpuRenderState]| validate_payload(
            &frame, &vertices, &[0, 1, 2], &draws, &materials, states, &instances, &ranges);
        assert!(accepted(&states).is_ok());
        states[0].flat_shadow_primary = 2;
        assert!(accepted(&states).is_err(), "non-boolean flat-primary payload must be rejected");
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
