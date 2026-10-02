// The bounded shadow profile has no extra scene texture units. Its pipeline
// reclaims four of those bindings to keep eight maps within WebGPU's limits.
pub(crate) fn eight_map_source() -> String {
    let source = include_str!("../../shaders/coin_standard.wgsl");
    let (head, rest) = source.split_once("// COIN_UPPER_TEXTURE_UNITS_BEGIN").unwrap();
    let (_, tail) = rest.split_once("// COIN_UPPER_TEXTURE_UNITS_END").unwrap();
    let mut source = format!("{head}{tail}");
    let mut declarations = String::from(r#"
struct ExtraShadowReceiver {
    view_to_clip: mat4x4<f32>,
    view_to_light: mat4x4<f32>,
    params: vec4<f32>,
    info: vec4<f32>,
    falloff: vec4<f32>,
};
@group(1) @binding(0) var<uniform> extra_shadows: array<ExtraShadowReceiver, 4>;
fn coinExtraShadowOwnsLight(index: u32) -> bool {
    for (var slot = 0u; slot < 4u; slot += 1u) {
        if (extra_shadows[slot].params.x > 0.5 &&
            index == u32(extra_shadows[slot].info.y)) { return true; }
    }
    return false;
}
"#);
    let mut surface = String::new();
    for slot in 0..4 {
        declarations += &format!("@group(1) @binding({}) var t_extra_shadow{}: texture_2d<f32>;\n", slot + 1, slot);
        surface += &format!(r#"
    if (extra_shadows[{slot}].params.x > 0.5 && u.params.w > 0.5) {{
        let receiver = extra_shadows[{slot}];
        let light = u.lights[u32(receiver.info.y)];
        let contribution = light_contribution(light,
            materials[input.material_slot], input.position_view, input.normal_view);
        let coord = receiver.view_to_clip * vec4<f32>(input.position_view, 1.0);
        let light_view = (receiver.view_to_light * vec4<f32>(input.position_view, 1.0)).xyz;
        primary = vec4<f32>(clamp(primary.rgb + contribution *
            vsm_shadow_factor(coord, light_view, input.position_view,
                receiver.params, receiver.info, receiver.falloff, t_extra_shadow{slot}),
            vec3<f32>(0.0), vec3<f32>(1.0)), primary.a);
    }}
"#);
    }
    source = source.replace("// COIN_EXTRA_SHADOW_DECLARATIONS", &declarations)
        .replace("/* COIN_EXTRA_SHADOW_OWNS_LIGHT */", "|| coinExtraShadowOwnsLight(i)")
        .replace("// COIN_EXTRA_SHADOW_SURFACE", &surface);
    source
}
