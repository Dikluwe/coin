    #[test]
    fn shaders_validate() {
        for source in [
            super::COMPOSITE.to_owned(),
            format!(
                "{}{}",
                include_str!("../../../../src/rendering/coinwgpu/shaders/coin_standard.wgsl"),
                super::FRAGMENT
            ),
        ] {
            let module = naga::front::wgsl::parse_str(&source).unwrap();
            naga::valid::Validator::new(
                naga::valid::ValidationFlags::all(),
                naga::valid::Capabilities::all(),
            )
            .validate(&module)
            .unwrap();
        }
    }
