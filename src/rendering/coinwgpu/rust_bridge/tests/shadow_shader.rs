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

#[path = "../src/shadow_receiver.rs"]
mod shadow_receiver;

#[test]
fn eight_map_receiver_shader_validates() {
    let source = shadow_receiver::eight_map_source();
    let module = naga::front::wgsl::parse_str(&source).expect("parse eight-map receiver WGSL");
    Validator::new(ValidationFlags::all(), Capabilities::all())
        .validate(&module).expect("validate eight-map receiver WGSL");
}

#[test]
fn four_map_quality_shader_validates() {
    let source = shadow_receiver::four_map_source();
    let module = naga::front::wgsl::parse_str(&source).expect("parse quality receiver WGSL");
    Validator::new(ValidationFlags::all(), Capabilities::all())
        .validate(&module).expect("validate quality receiver WGSL");
}
