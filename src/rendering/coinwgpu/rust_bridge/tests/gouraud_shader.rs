use naga::valid::{Capabilities, ValidationFlags, Validator};

#[test]
fn standard_shader_preserves_vertex_lighting_outside_shadows() {
    let source = include_str!("../../shaders/coin_standard.wgsl");
    let module = naga::front::wgsl::parse_str(source).expect("parse standard WGSL");
    Validator::new(ValidationFlags::all(), Capabilities::all())
        .validate(&module).expect("validate standard WGSL");
    let vertex = source.split("fn vs_main").nth(1).unwrap()
        .split("fn apply_fog").next().unwrap();
    let fragment = source.split("fn fragment_color").nth(1).unwrap();
    assert!(vertex.contains("shade_vertex(materials[input.material_slot]"));
    assert!(fragment.contains("var primary=input.diffuse_color"));
    assert!(!fragment.contains("shade_vertex"));
    assert!(fragment.contains("if (u.shadow_params.x > 0.5 && u.params.w > 0.5)"));
    assert!(fragment.contains("materials[input.material_slot]"));
    assert!(source.contains("clamp(rgb, vec3<f32>(0.0), vec3<f32>(1.0))"));
}

#[test]
fn all_polygon_bias_shader_variants_validate() {
    for source in [include_str!("../../shaders/coin_standard.wgsl"),
                   include_str!("../../shaders/coin_line.wgsl"),
                   include_str!("../../shaders/coin_point.wgsl")] {
        let module = naga::front::wgsl::parse_str(source).expect("parse WGSL");
        Validator::new(ValidationFlags::all(), Capabilities::all())
            .validate(&module).expect("validate WGSL depth output variant");
        assert!(module.entry_points.iter().any(|entry|
            entry.stage == naga::ShaderStage::Fragment && entry.name == "fs_depth_bias"));
    }
}
