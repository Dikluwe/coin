// Native compilation removes portable functions and its uniform appendix.
// Both vertex and fragment stages therefore use the original binding prefix.
pub(super) fn sampling_profile(source: &str, portable: bool) -> String {
    if portable || !source.contains("fn coin_portable_sample(") { return source.to_owned(); }
    let mut source = source.to_owned();
    let start = source.find("fn coin_portable_lerp(").unwrap();
    let end = source[start..].find("fn projected_uv(").unwrap() + start;
    source.replace_range(start..end, "");
    source = source.replace("    sampling_texture_sizes: array<vec4<f32>, 8>,\n", "");
    source = source.replace(", u.tex_params.x, u.sampling_texture_sizes[0]", "");
    for unit in 1..8 { source = source.replace(&format!(", params.x, u.sampling_texture_sizes[{unit}]"), ""); }
    source.replace("coin_portable_sample(", "textureSample(")
}

// Keep the transported uniform ABI intact. Specialization only removes work
// whose absence is already known from the validated draw/active shadow profile.
pub(super) fn texture_specialization(source: &str) -> String {
    format!("override coin_textures_enabled: bool = true;\n{}", source
        .replace("if (u.tex_params.x > 0.5)",
            "if (coin_textures_enabled && u.tex_params.x > 0.5)")
        .replace("if (u.extra_tex_params[",
            "if (coin_textures_enabled && u.extra_tex_params["))
}

// wgpu permits non-filterable float bindings for depth views. Naga 24's GLSL
// writer supports texelFetch through this type, but rejects depth textureLoad.
pub(super) fn depth_load_profile(source: &str, gl: bool) -> String {
    if !gl { return source.to_owned(); }
    let mut source = source.to_owned();
    for name in ["previous_depth", "opaque_depth", "layer_depth"] {
        for space in ["", " "] {
            source = source.replace(&format!("var {name}:{space}texture_depth_2d;"),
                &format!("var {name}: texture_2d<f32>;"));
        }
        source = source.replace(&format!("textureLoad({name},xy,0)"),
            &format!("textureLoad({name},xy,0).r"));
    }
    source
}

pub(super) fn without_shadows(source: &str) -> String {
    let mut source = source.to_owned();
    let output_start = source.find("struct VertexOutput {").unwrap();
    let output_end = output_start + source[output_start..].find("\n};").unwrap() + 3;
    let output = source[output_start..output_end].lines()
        .filter(|line| !line.contains("shadow_")).collect::<Vec<_>>().join("\n");
    source.replace_range(output_start..output_end, &output);
    let start = source.find("        if ((u.shadow_params.x").unwrap();
    let end = start + source[start..].find("{ continue; }").unwrap() + "{ continue; }".len();
    source.replace_range(start..end, "");
    let start = source.find("    output.shadow_coord =").unwrap();
    let end = start + source[start..].find("    if (u.tex_params.x").unwrap();
    source.replace_range(start..end, "");
    let start = source.find("    if (u.shadow_params.x > 0.5 && u.params.w > 0.5)").unwrap();
    let end = start + source[start..].find("    // COIN_EXTRA_SHADOW_SURFACE").unwrap();
    source.replace_range(start..end, "");
    source
}

#[cfg(test)]
pub(super) mod tests {
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
    fn native_profiles_restore_uniform_layout_and_all_fragment_contracts() {
        for source in [without_shadows(include_str!("../../shaders/coin_standard.wgsl")),
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
        for source in [without_shadows(include_str!("../../shaders/coin_standard.wgsl")),
            super::super::shadow_receiver::four_map_source(),
            super::super::shadow_receiver::eight_map_source(),
            include_str!("../../shaders/coin_line.wgsl").to_owned(),
            include_str!("../../shaders/coin_point.wgsl").to_owned()] {
            assert_glsl(&depth_load_profile(&source, true), "fs_peel");
        }
    }

    #[test]
    fn all_specialized_profiles_preserve_valid_entry_points() {
        let standard = include_str!("../../shaders/coin_standard.wgsl");
        for source in [without_shadows(standard), super::super::shadow_receiver::four_map_source(),
            super::super::shadow_receiver::eight_map_source(),
            include_str!("../../shaders/coin_line.wgsl").to_owned(),
            include_str!("../../shaders/coin_point.wgsl").to_owned()] {
            let source = texture_specialization(&format!("{}{}", source, super::super::weighted::FRAGMENT));
            let module = naga::front::wgsl::parse_str(&source).expect("parse specialized profile");
            Validator::new(ValidationFlags::all(), Capabilities::all())
                .validate(&module).expect("validate specialized profile");
            for entry in ["vs_main", "fs_main", "fs_peel", "fs_weighted", "fs_depth_bias"] {
                assert!(module.entry_points.iter().any(|point| point.name == entry));
            }
        }
    }
}
