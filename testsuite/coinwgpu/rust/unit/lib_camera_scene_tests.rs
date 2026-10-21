    use super::*;

    #[test]
    fn snapshot_policy_suspends_objects_and_readmits_a_camera_transactionally() {
        let mut policy = CameraSnapshotPolicy::default();
        assert!(policy.capture(None, 1, 0, false));
        policy.commit(true, true); // initial eligible frame
        assert!(policy.capture(Some(1), 1, 0, true)); // static reuse
        assert!(!policy.capture(Some(1), 2, 0, false)); // object rebuild candidate
        // A failed candidate does not commit; the old owned base still works.
        assert!(!policy.suspended);
        assert!(policy.capture(Some(1), 3, 1, true));
        policy.commit(false, false); // successful object rebuild, old base cleared
        assert!(policy.suspended);
        assert!(!policy.capture(None, 3, 0, false));
        assert!(policy.capture(None, 4, 3, false)); // camera hint needs full validation
        assert!(policy.suspended); // failed validation leaves it suspended
        policy.commit(true, true);
        assert!(!policy.suspended);
        assert!(policy.capture(Some(4), 5, 4, true));
        assert!(!policy.capture(Some(5), 6, 0, false));
    }

    fn phong_state() -> CoinWgpuRenderState {
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        state.color_write = 1;
        state.light_model = 1;
        state.light_count = 1;
        state.lights[0].position_type = [0.0, 0.0, 3.0, 1.0];
        state.lights[0].color_intensity = [1.0, 0.5, 0.25, 1.0];
        state.lights[0].attenuation_exponent = [0.0, 0.0, 1.0, 0.0];
        for i in 0..4 {
            state.model_view[i*5] = 1.0;
            state.model_view_projection[i*5] = 1.0;
            state.normal_matrix[i*5] = 1.0;
        }
        state
    }

    #[test]
    fn phong_camera_scene_owns_and_preserves_geometry_arc() {
        let mut frame: CoinWgpuFrameView = unsafe { std::mem::zeroed() };
        frame.frame_revision = 1; frame.width = 64; frame.height = 64;
        frame.vertex_count = 3; frame.index_count = 3; frame.draw_count = 1;
        frame.material_count = 1; frame.state_count = 1;
        let mut vertices = [CoinWgpuVertex::zeroed(); 3];
        for vertex in &mut vertices { vertex.screen_space_w = 1.0; vertex.normal[2] = 1.0; }
        let mut draw: CoinWgpuDraw = unsafe { std::mem::zeroed() };
        draw.vertex_count = 3; draw.index_count = 3;
        let mut material: CoinWgpuMaterial = unsafe { std::mem::zeroed() };
        material.diffuse[3] = 1.0;
        let order = [composition::CompositionItem { draw_index: 0, blend: false,
            additive: false, legacy_blend_alpha: false, screen_door: false, screen_door_level: 0, peel: false, weighted: false }];
        let mut state = phong_state();
        let base = camera_scene(7, &frame, &vertices, &[0,1,2], &[draw], &[material],
            &[state], &order, &[], &[], &[], &[], None).expect("validated opaque PHONG snapshot");
        assert!(base.geometry.opaque_batch);
        vertices[0].position[0] = 100.0;
        assert_eq!(base.geometry.vertices[0].position[0], 0.0, "must own caller payload");

        // The same resolver/builder is used by both native surface and
        // offscreen submission. Incoming immutable pointers intentionally stay
        // null: only a matching device generation/base may license the Arc.
        let mut current = base.clone();
        for revision in 2..5 {
            frame.camera_base_revision = revision - 1;
            frame.frame_revision = revision;
            state.model_view[12] = revision as f32 * 0.1;
            state.model_view_projection[12] = state.model_view[12];
            state.lights[0].position_type[0] = revision as f32;
            let owned = owned_camera_base(&current, 7, &frame, &[state])
                .expect("PHONG camera with resolved light coordinates");
            let next = camera_scene(7, &frame, &owned.geometry.vertices, &owned.geometry.indices,
                &owned.geometry.draws, &owned.geometry.materials, &[state], &order,
                &[], &[], &[], &[], Some(&owned)).unwrap();
            assert!(Arc::ptr_eq(&base.geometry, &next.geometry));
            assert_eq!(next.states[0].lights[0].position_type[0], revision as f32);
            current = next;
        }
        frame.camera_base_revision = current.revision; frame.frame_revision = current.revision + 1;
        assert!(owned_camera_base(&current, 7, &frame, &[state]).is_some());
        assert!(owned_camera_base(&current, 8, &frame, &[state]).is_none(), "device generation cannot alias");
        frame.camera_base_revision = 1; frame.frame_revision = 5;
        state.lights[0].color_intensity[0] = 0.25;
        assert!(owned_camera_base(&base, 7, &frame, &[state]).is_none(), "light color is immutable");
        state = phong_state(); state.normal_matrix[0] = f32::NAN;
        assert!(owned_camera_base(&base, 7, &frame, &[state]).is_none());
        state = phong_state(); state.fog_mode = 1;
        assert!(owned_camera_base(&base, 7, &frame, &[state]).is_none());
        state = phong_state(); frame.vertex_count = 4;
        assert!(owned_camera_base(&base, 7, &frame, &[state]).is_none());
    }
