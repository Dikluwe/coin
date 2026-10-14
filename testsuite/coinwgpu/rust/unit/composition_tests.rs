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
        draws[0].composition_flags = 33;
        let resolved = order(&[], &[], &draws, &materials, &states, &[]).unwrap();
        assert!(resolved[0].blend && resolved[0].legacy_blend_alpha);
    }
    #[test]
    fn validates_new_resolved_mechanisms_without_coin_interpretation() {
        let (mut draws, materials, states) = fixture(&[0, 0, 0, 0], &[0.5, 0.5, 0.5, 0.5]);
        draws[0].composition_flags = 3;
        draws[1].composition_flags = 9;
        draws[2].composition_flags = 0x4004;
        draws[3].composition_flags = 17;
        let resolved = order(&[], &[], &draws, &materials, &states, &[]).unwrap();
        assert!(resolved[0].additive);
        assert!(resolved[1].peel);
        assert_eq!(resolved[2].screen_door_level, 64);
        assert!(resolved[3].weighted);
        assert_eq!(passes(&resolved, &draws), vec![0..1, 1..2, 2..3, 3..4]);
        for flags in [32, 35, 41, 49, 2, 8, 5, 11, 16, 19, 21, 25, 0x4104, 0x100, 0x80, 0x8000] {
            draws[0].composition_flags = flags;
            assert!(
                order(&[], &[], &draws, &materials, &states, &[]).is_err(),
                "flags={flags}"
            );
        }
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
        assert_eq!(
            order(&[], &[], &draws, &materials, &states, &[])
                .err()
                .unwrap()
                .0,
            CoinWgpuStatus::Unsupported
        );
        states[0].clip_plane_count = 1;
        assert_eq!(
            order(&[], &[], &draws, &materials, &states, &[])
                .err()
                .unwrap()
                .0,
            CoinWgpuStatus::InvalidArgument
        );
        states[0].clip_planes[0] = [1.0, 0.0, 0.0, f32::NAN];
        assert!(order(&[], &[], &draws, &materials, &states, &[]).is_err());
        states[0].clip_planes[0][3] = -0.25;
        assert!(order(&[], &[], &draws, &materials, &states, &[]).is_ok());
    }
