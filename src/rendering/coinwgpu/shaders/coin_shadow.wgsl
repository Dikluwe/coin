// Moment-map pass for the CoinRenderShadowPass selected by Core.
// The planned GPU path will supply WebGPU clip conversion and the light view matrix.
struct ShadowUniforms {
    model_view_projection: mat4x4<f32>,
    model_view: mat4x4<f32>,
    near_far_kind: vec4<f32>, // x=near, y=far, z=0 directional / 1 spot
};

@group(0) @binding(0)
var<uniform> shadow: ShadowUniforms;

struct ShadowVertexInput {
    @location(0) position: vec3<f32>,
};

struct ShadowVertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) light_view_position: vec3<f32>,
};

@vertex
fn vs_moments(input: ShadowVertexInput) -> ShadowVertexOutput {
    var output: ShadowVertexOutput;
    output.clip_position = shadow.model_view_projection *
        vec4<f32>(input.position, 1.0);
    output.light_view_position = (shadow.model_view *
        vec4<f32>(input.position, 1.0)).xyz;
    return output;
}

@fragment
fn fs_moments(input: ShadowVertexOutput) -> @location(0) vec4<f32> {
    // Coin/GL uses radial distance for spots and axial depth for directionals.
    let distance = select(-input.light_view_position.z,
        length(input.light_view_position), shadow.near_far_kind.z > 0.5);
    let normalized = (distance - shadow.near_far_kind.x) /
        (shadow.near_far_kind.y - shadow.near_far_kind.x);
    return vec4<f32>(normalized, normalized * normalized, 0.0, 0.0);
}
