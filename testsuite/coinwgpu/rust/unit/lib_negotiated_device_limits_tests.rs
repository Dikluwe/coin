use super::negotiated_device_limits;

    #[test]
    fn storage_buffer_request_respects_adapter_and_default_ceiling() {
        let baseline = wgpu::Limits::default();
        assert_eq!(baseline.max_storage_buffers_per_shader_stage, 8);
        for available in [0, 1, 4, 7, 8, 9, u32::MAX] {
            let mut adapter = baseline.clone();
            adapter.max_storage_buffers_per_shader_stage = available;
            let actual = negotiated_device_limits(&adapter);
            let mut expected = baseline.clone();
            expected.max_storage_buffers_per_shader_stage = available.min(8);
            assert_eq!(actual, expected, "adapter storage buffer limit {available}");
            assert_eq!(adapter.max_storage_buffers_per_shader_stage, available);
        }
    }

    #[test]
    fn inter_stage_elevation_keeps_existing_eighty_component_threshold() {
        for available in [0, 60, 79, 80, 81, u32::MAX] {
            let mut adapter = wgpu::Limits::default();
            adapter.max_storage_buffers_per_shader_stage = 4;
            adapter.max_inter_stage_shader_components = available;
            let mut expected = wgpu::Limits::default();
            expected.max_storage_buffers_per_shader_stage = 4;
            if available >= 80 {
                expected.max_inter_stage_shader_components = 80;
            }
            assert_eq!(negotiated_device_limits(&adapter), expected,
                "adapter inter-stage component limit {available}");
        }
    }

    #[test]
    fn unrelated_adapter_limits_do_not_replace_required_defaults() {
        let mut adapter = wgpu::Limits::default();
        adapter.max_storage_buffers_per_shader_stage = 4;
        adapter.max_inter_stage_shader_components = 80;
        adapter.max_bind_groups = 1;
        adapter.max_texture_dimension_2d = 64;
        adapter.max_uniform_buffers_per_shader_stage = 1;
        adapter.min_uniform_buffer_offset_alignment = 1024;
        let mut expected = wgpu::Limits::default();
        expected.max_storage_buffers_per_shader_stage = 4;
        expected.max_inter_stage_shader_components = 80;
        assert_eq!(negotiated_device_limits(&adapter), expected);
    }
