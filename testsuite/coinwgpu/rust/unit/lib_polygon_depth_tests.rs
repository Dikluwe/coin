    use super::*;

    #[test]
    fn d32_quantum_tracks_exponent_and_fractional_units() {
        for (depth, expected) in [(0.125, 2.0f32.powi(-26)),
                                  (0.5, 2.0f32.powi(-24)),
                                  (1.0, 2.0f32.powi(-23))] {
            assert_eq!(d32_depth_quantum(depth), expected);
        }
        assert_eq!(d32_depth_quantum(0.0).to_bits(), 1);
        let mut st: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        st.model_view_projection[0] = 1.0;st.model_view_projection[5] = 1.0;
        st.model_view_projection[10] = 1.0;st.model_view_projection[15] = 1.0;
        st.depth_range = [0.0, 1.0];st.polygon_offset_slope_bias = -0.01;
        let mut draw: CoinWgpuDraw = unsafe { std::mem::zeroed() };
        draw.index_count = 2;
        let mut vertices = [CoinWgpuVertex::zeroed(); 2];
        vertices[0].position[2] = 0.1;vertices[1].position[2] = 0.5;
        for units in [-3.5, 3.5] {
            st.polygon_offset_units = units;
            assert_eq!(resolved_polygon_bias(&st, &draw, &vertices, &[0,1]).unwrap(),
                -0.01 + units * 2.0f32.powi(-24));
        }
        st.depth_range = [0.0, 0.25];st.polygon_offset_units = 4.0;
        assert_eq!(resolved_polygon_bias(&st, &draw, &vertices, &[0,1]).unwrap(),
            -0.01 + 4.0 * 2.0f32.powi(-26));
        st.polygon_offset_max_depth_bits = 0.75f32.to_bits() + 1;
        assert_eq!(resolved_polygon_bias(&st, &draw, &vertices, &[0,1]).unwrap(),
            -0.01 + 4.0 * 2.0f32.powi(-24));
        st.polygon_offset_max_depth_bits = f32::from_bits(0.5f32.to_bits() - 1).to_bits() + 1;
        assert_eq!(resolved_polygon_bias(&st, &draw, &vertices, &[0,1]).unwrap(),
            -0.01 + 4.0 * 2.0f32.powi(-25));
        st.polygon_offset_factor = 1.0;
        assert!(resolved_polygon_bias(&st, &draw, &vertices, &[0,1]).is_err());
    }

    #[test]
    fn masks_signs_and_disable_preserve_pipeline_bias() {
        // FFI state contains only scalar/array numeric fields; zero is valid.
        let mut st: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        st.polygon_offset_enabled = 1;
        for primitive in [1, 2, 4] {
            st.polygon_offset_primitive_style = primitive;
            for mask in [1, 2, 4, 7] {
                st.polygon_offset_styles = mask;
                for sign in [-1.0, 1.0] {
                    st.polygon_offset_factor = sign * 2.0;
                    st.polygon_offset_units = sign * 3.6;
                    let bias = polygon_depth_bias(&st, 0).unwrap();
                    assert_eq!(bias.slope_scale, if mask & primitive != 0 { sign * 2.0 } else { 0.0 });
                    assert_eq!(bias.constant, if mask & primitive != 0 { (sign * 4.0) as i32 } else { 0 });
                }
            }
        }
        st.polygon_offset_enabled = 0;
        assert_eq!(polygon_depth_bias(&st, 0).unwrap().constant, 0);
        st.polygon_offset_factor = f32::NAN;
        assert!(polygon_depth_bias(&st, 0).is_err());
    }
