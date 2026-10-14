    use super::*;
    use naga::valid::{Capabilities, ValidationFlags, Validator};

    pub(crate) fn assert_glsl(source: &str, entry: &str) {
        let module = naga::front::wgsl::parse_str(source).expect("parse GLSL profile");
        let info = Validator::new(ValidationFlags::all(), Capabilities::all())
            .validate(&module).expect("validate GLSL profile");
        let mut output = String::new();
        let options = naga::back::glsl::Options::default();
        let pipeline = naga::back::glsl::PipelineOptions {
            shader_stage: naga::ShaderStage::Fragment, entry_point: entry.into(), multiview: None,
        };
        naga::back::glsl::Writer::new(&mut output, &module, &info, &options, &pipeline,
            naga::proc::BoundsCheckPolicies::default()).expect("GLSL writer")
            .write().expect("translate GLSL profile");
    }

    #[test]
    fn sampling_profiles_preserve_uniform_abi_with_lf_and_crlf_inputs() {
        for source in [include_str!("../../../../src/rendering/coinwgpu/shaders/coin_standard.wgsl").to_owned(),
            super::super::shadow_receiver::four_map_source(),
            super::super::shadow_receiver::eight_map_source(),
            super::super::instancing::shader_source()] {
            let lf = source.replace("\r\n", "\n");
            for input in [lf.clone(), lf.replace("\n", "\r\n")] {
                for portable in [false, true] {
                    let profile = sampling_profile(&input, portable);
                    let module = naga::front::wgsl::parse_str(&profile).expect("sampling line-ending profile");
                    Validator::new(ValidationFlags::all(), Capabilities::all())
                        .validate(&module).expect("sampling uniform contract");
                    let uniform = module.types.iter().find(|(_,ty)|ty.name.as_deref()==Some("Uniforms")).unwrap().1;
                    if let naga::TypeInner::Struct {span,..}=uniform.inner {
                        assert_eq!(span, if portable {3168} else {3040});
                    } else {panic!("uniform is not a struct");}
                }
            }
        }
    }

    #[test]
    fn native_profiles_restore_uniform_layout_and_all_fragment_contracts() {
        for source in [without_shadows(include_str!("../../../../src/rendering/coinwgpu/shaders/coin_standard.wgsl")),
            super::super::shadow_receiver::four_map_source(), super::super::shadow_receiver::eight_map_source(),
            super::super::instancing::shader_source()] {
            let source = texture_specialization(&format!("{}{}", sampling_profile(&source, false), super::super::weighted::FRAGMENT));
            assert!(!source.contains("sampling_texture_sizes") && !source.contains("fn coin_portable_") && !source.contains("dpdxFine"));
            let module = naga::front::wgsl::parse_str(&source).expect("native profile parse");
            Validator::new(ValidationFlags::all(), Capabilities::all()).validate(&module).expect("native profile validate");
            let uniform = module.types.iter().find(|(_,ty)|ty.name.as_deref()==Some("Uniforms")).unwrap().1;
            if let naga::TypeInner::Struct {span,..}=uniform.inner {
                assert_eq!(span as usize,super::super::NATIVE_UNIFORM_BYTES);
            } else {panic!("uniform is not a struct");}
            // Naga's standalone GLSL writer needs resolved overrides, as wgpu
            // supplies them during pipeline compilation. Qualify both profiles.
            for enabled in ["true","false"] {
                let resolved=source.replace("override coin_textures_enabled: bool = true;",
                    &format!("const coin_textures_enabled: bool = {enabled};"));
                for entry in ["fs_main","fs_peel","fs_weighted","fs_depth_bias"] {assert_glsl(&depth_load_profile(&resolved,true),entry);}
            }
        }
    }

    #[test]
    fn gl_peeling_profiles_translate_to_glsl() {
        for source in [without_shadows(include_str!("../../../../src/rendering/coinwgpu/shaders/coin_standard.wgsl")),
            super::super::shadow_receiver::four_map_source(),
            super::super::shadow_receiver::eight_map_source(),
            include_str!("../../../../src/rendering/coinwgpu/shaders/coin_line.wgsl").to_owned(),
            include_str!("../../../../src/rendering/coinwgpu/shaders/coin_point.wgsl").to_owned()] {
            assert_glsl(&depth_load_profile(&source, true), "fs_peel");
        }
    }

    #[test]
    fn all_specialized_profiles_preserve_valid_entry_points() {
        let standard = include_str!("../../../../src/rendering/coinwgpu/shaders/coin_standard.wgsl");
        for source in [without_shadows(standard), super::super::shadow_receiver::four_map_source(),
            super::super::shadow_receiver::eight_map_source(),
            include_str!("../../../../src/rendering/coinwgpu/shaders/coin_line.wgsl").to_owned(),
            include_str!("../../../../src/rendering/coinwgpu/shaders/coin_point.wgsl").to_owned()] {
            let source = texture_specialization(&format!("{}{}", source, super::super::weighted::FRAGMENT));
            let module = naga::front::wgsl::parse_str(&source).expect("parse specialized profile");
            Validator::new(ValidationFlags::all(), Capabilities::all())
                .validate(&module).expect("validate specialized profile");
            for entry in ["vs_main", "fs_main", "fs_peel", "fs_weighted", "fs_depth_bias"] {
                assert!(module.entry_points.iter().any(|point| point.name == entry));
            }
        }
    }
