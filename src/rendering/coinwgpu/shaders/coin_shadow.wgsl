// Moment-map pass for the CoinRenderShadowPass selected by Core.
// The planned GPU path will supply WebGPU clip conversion and the light view matrix.
struct ShadowUniforms {
    model_view_projection: mat4x4<f32>,
    model_view: mat4x4<f32>,
    near_far_kind: vec4<f32>, // x=near, y=far, z=kind, w=clip count
    clip_model_view: mat4x4<f32>,
    clip_planes: array<vec4<f32>, 8>,
    raster_viewport: vec4<f32>,
    alpha_test: vec4<f32>,
};

@group(0) @binding(0)
var<uniform> shadow: ShadowUniforms;

struct ShadowVertexInput {
    @location(0) position: vec3<f32>,
};

struct ShadowVertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) light_view_position: vec3<f32>,
    @location(1) clip_view_position: vec3<f32>,
    @location(2) captured_depth: vec2<f32>,
};

fn coin_shadow_coverage(clip: vec4<f32>) -> vec4<f32> {
    if shadow.raster_viewport.z <= 0.0 || clip.w <= 0.000001 { return clip; }
    let ndc = clip.xy / clip.w;
    let window = (ndc + vec2<f32>(1.0)) * 0.5 * shadow.raster_viewport.zw;
    if any(abs(window) > vec2<f32>(32768.0)) { return clip; }
    let snapped = (floor(window * 128.0) + vec2<f32>(0.5)) / 128.0;
    let xy = (snapped * 2.0 / shadow.raster_viewport.zw - vec2<f32>(1.0)) * clip.w;
    return vec4<f32>(xy, clip.zw);
}

@vertex
fn vs_moments(input: ShadowVertexInput) -> ShadowVertexOutput {
    var output: ShadowVertexOutput;
    output.clip_position = shadow.model_view_projection *
        vec4<f32>(input.position, 1.0);
    output.clip_position = coin_shadow_coverage(output.clip_position);
    output.captured_depth = output.clip_position.zw;
    if (shadow.raster_viewport.z > 0.0) { output.clip_position.z = 0.0; }
    output.light_view_position = (shadow.model_view *
        vec4<f32>(input.position, 1.0)).xyz;
    output.clip_view_position = (shadow.clip_model_view *
        vec4<f32>(input.position, 1.0)).xyz;
    return output;
}

struct ShadowFragmentOutput {
    @location(0) color: vec4<f32>,
    @builtin(frag_depth) depth: f32,
};
@fragment
fn fs_moments(input: ShadowVertexOutput) -> ShadowFragmentOutput {
    var output: ShadowFragmentOutput;
    var depth = input.clip_position.z;
    if (shadow.raster_viewport.z > 0.0) {
        depth = input.captured_depth.x / input.captured_depth.y;
        if (depth < 0.0 || depth > 1.0) { discard; }
    }
    output.depth = depth;
    for (var i = 0u; i < 8u; i += 1u) {
        if (f32(i) >= shadow.near_far_kind.w) { break; }
        if (dot(shadow.clip_planes[i], vec4<f32>(input.clip_view_position, 1.0)) < 0.0) {
            discard;
        }
    }
    // Coin/GL uses radial distance for spots and axial depth for directionals.
    let distance = select(-input.light_view_position.z,
        length(input.light_view_position), shadow.near_far_kind.z > 0.5);
    let normalized = (distance - shadow.near_far_kind.x) /
        (shadow.near_far_kind.y - shadow.near_far_kind.x);
    // Coin's floating moment target tests signed shader alpha; keep the
    // undisturbed RG moments used by the qualified receiver.
    let signed_alpha = 2.0 * fract(64.0 * normalized * normalized) - 1.0;
    let function = u32(shadow.alpha_test.x);
    let reference = shadow.alpha_test.y;
    var passes = function == 0u || function == 2u;
    switch function {
        case 1u: { passes = false; }
        case 3u: { passes = signed_alpha < reference; }
        case 4u: { passes = signed_alpha <= reference; }
        case 5u: { passes = signed_alpha == reference; }
        case 6u: { passes = signed_alpha >= reference; }
        case 7u: { passes = signed_alpha > reference; }
        case 8u: { passes = signed_alpha != reference; }
        default: {}
    }
    if (!passes) { discard; }
    output.color = vec4<f32>(normalized, normalized * normalized, 0.0, 0.0);
    return output;
}
