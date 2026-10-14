    use super::*;
    fn projective_fixture() -> (CoinWgpuRenderState, [CoinWgpuVertex; 3]) {
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        state.has_texture = 1;
        for i in 0..4 { state.texture_matrix[i * 5] = 1.0; }
        let mut vertices = [CoinWgpuVertex::zeroed(); 3];
        for vertex in &mut vertices { vertex.texcoord = [0.5, 0.25, 0.0, 1.0]; }
        (state, vertices)
    }

    #[test]
    fn rejects_singular_crossing_nonfinite_and_overflowing_projective_coordinates() {
        let (mut state, mut vertices) = projective_fixture();
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_ok());
        vertices[1].texcoord[3] = 0.0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err());
        vertices[1].texcoord[3] = -1.0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err());
        assert!(validate_texture_coordinates(&state, 1, &vertices, &[0, 1]).is_err());
        assert!(validate_texture_coordinates(&state, 2, &vertices, &[0, 1, 2]).is_ok(),
            "each point owns its own q sign");
        for vertex in &mut vertices { vertex.texcoord[3] = -2.0; }
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_ok());
        vertices[0].texcoord[3] = f32::MIN_POSITIVE * 0.5;
        assert!(validate_texture_coordinates(&state, 2, &vertices, &[0]).is_err());
        vertices[0].texcoord = [f32::MAX, 0.25, 0.0, 0.5];
        assert!(validate_texture_coordinates(&state, 2, &vertices, &[0]).is_err());
        vertices[0].texcoord = [f32::MAX, 0.25, 0.0, 1.0];
        state.texture_matrix[0] = 2.0;
        assert!(validate_texture_coordinates(&state, 2, &vertices, &[0]).is_err());
        vertices[0].texcoord = [0.5, 0.25, f32::NAN, 1.0];
        assert!(validate_texture_coordinates(&state, 2, &vertices, &[0]).is_err());
        vertices[0] = CoinWgpuVertex::zeroed();
        state.has_texture = 0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_ok(),
            "inactive unit zero q is valid dormant transport");
    }

    #[test]
    fn transforms_r_into_q_and_validates_upper_unit_independently() {
        let (mut state, mut vertices) = projective_fixture();
        // q' = r + q. Raw positive q cannot qualify a zero/crossing transformed q.
        state.texture_matrix[11] = 1.0;
        vertices[1].texcoord[2] = -1.0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err());
        vertices[1].texcoord[2] = -2.0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err());
        for vertex in &mut vertices { vertex.texcoord[2] = -3.0; }
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_ok());
        state.has_texture = 0;
        let upper = &mut state.extra_textures[6];
        upper.enabled = 1;
        for i in 0..4 { upper.matrix[i * 5] = 1.0; }
        upper.matrix[11] = 1.0;
        for vertex in &mut vertices { vertex.extra_texcoords[6] = [0.5, 0.25, 0.0, 1.0]; }
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_ok());
        vertices[2].extra_texcoords[6][2] = -1.0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err());
        state.extra_textures[6].enabled = 0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_ok());
    }

    #[test]
    fn direct_st_policy_permits_singular_q_but_requires_finite_st_and_raw_payload() {
        let (mut state, mut vertices) = projective_fixture();
        vertices[0].texcoord[3] = 0.0;
        vertices[1].texcoord[3] = -1.0;
        vertices[2].texcoord[3] = f32::MIN_POSITIVE * 0.5;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err());
        let projective = state;
        state.texture_projection = 1;
        assert!(valid_texture_payload(&state));
        assert!(!same_camera_independent_state(&projective, &state),
            "projection policy participates in state reuse even without a shadow map");
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_ok());
        // Native direct ST does not sample R/Q, so a finite input whose Q row
        // overflows remains valid when the transformed S/T rows stay finite.
        vertices[0].texcoord = [0.5, 0.25, f32::MAX, 1.0];
        state.texture_matrix[11] = 2.0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_ok());
        vertices[0].texcoord[0] = f32::MAX;
        state.texture_matrix[0] = 2.0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err());
        vertices[0].texcoord = [0.5, 0.25, f32::NAN, 1.0];
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err());
        vertices[0].texcoord = [0.5, 0.25, 0.0, 1.0];
        state.has_texture = 0;
        state.extra_textures[6].enabled = 1;
        for i in 0..4 { state.extra_textures[6].matrix[i * 5] = 1.0; }
        for vertex in &mut vertices { vertex.extra_texcoords[6] = [0.5, 0.25, 0.0, 0.0]; }
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_ok());
        state.texture_projection = 0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err());
        state.texture_projection = 2;
        state.extra_textures[6].enabled = 0;
        assert!(!valid_texture_payload(&state));
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err(),
            "unknown projection policy is rejected even without active texture units");
    }

    #[test]
    fn rejects_malformed_programs_and_extra_unit_values() {
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        assert!(valid_texture_payload(&state));
        let valid = [[1.0, 7.0, 5.0, 0.0], [0.0, 15.0, 4.0, 4.0],
            [8.0, 15.0, 11.0, 2.0], [0.2, 0.4, 0.6, 0.8]];
        state.texture_combines[7] = valid;
        assert!(valid_texture_payload(&state));
        for (r, c, value) in [(0, 0, 2.0), (0, 1, 7.5), (0, 2, 6.0),
            (1, 0, 16.0), (2, 0, 7.0), (1, 3, 3.0), (3, 0, -0.1), (3, 2, f32::NAN)] {
            state.texture_combines[7] = valid;
            state.texture_combines[7][r][c] = value;
            assert!(!valid_texture_payload(&state));
        }
        state.texture_combines[7] = valid;
        state.extra_textures[6].enabled = 1;
        assert!(valid_texture_payload(&state));
        state.extra_textures[6].matrix[12] = f32::INFINITY;
        assert!(!valid_texture_payload(&state));
    }
