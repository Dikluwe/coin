    #[test]
    fn rejects_unbounded_and_over_budget_transport() {
        use super::validate_request;
        for layers in [0, 9, u32::MAX] {
            assert!(validate_request(layers, 0, u64::MAX, 64, 64, true).is_err());
        }
        assert!(validate_request(8, 0, u64::MAX, u32::MAX, u32::MAX, true).is_err());
        assert!(validate_request(8, 0, 0, 64, 64, true).is_err());
        assert!(validate_request(8, 1, u64::MAX, 64, 64, true).is_err());
        let physical_bytes = 64 * 64 * (8 * 13 + 4);
        assert!(validate_request(8, 0, physical_bytes - 1, 64, 64, true).is_err());
        assert!(validate_request(8, 0, physical_bytes, 64, 64, true).is_ok());
        assert!(validate_request(0, 0, 0, 64, 64, false).is_ok());
    }
    #[test]
    fn compositor_validates() {
        let module = naga::front::wgsl::parse_str(super::COMPOSITE).unwrap();
        naga::valid::Validator::new(
            naga::valid::ValidationFlags::all(),
            naga::valid::Capabilities::all(),
        )
        .validate(&module)
        .unwrap();
        super::super::shader_profile::tests::assert_glsl(
            &super::super::shader_profile::depth_load_profile(super::COMPOSITE, true), "fs_depth");
    }
