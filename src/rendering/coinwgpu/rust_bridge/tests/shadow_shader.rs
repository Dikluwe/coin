use naga::valid::{Capabilities, ValidationFlags, Validator};

#[test]
fn coin_shadow_moments_shader_validates() {
    let source = include_str!("../../shaders/coin_shadow.wgsl");
    let module = naga::front::wgsl::parse_str(source).expect("parse shadow WGSL");
    Validator::new(ValidationFlags::all(), Capabilities::all())
        .validate(&module).expect("validate shadow WGSL");
    assert!(module.entry_points.iter().any(|entry|
        entry.stage == naga::ShaderStage::Vertex && entry.name == "vs_moments"));
    assert!(module.entry_points.iter().any(|entry|
        entry.stage == naga::ShaderStage::Fragment && entry.name == "fs_moments"));
}
