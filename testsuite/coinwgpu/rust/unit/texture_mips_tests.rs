    #[test]
    fn both_output_shaders_validate() {
        for storage in ["rgba8unorm", "rgba16float"] {
            let module = naga::front::wgsl::parse_str(&super::shader_source(storage)).unwrap();
            naga::valid::Validator::new(
                naga::valid::ValidationFlags::all(),
                naga::valid::Capabilities::all(),
            )
            .validate(&module)
            .unwrap();
        }
    }
