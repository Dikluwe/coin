// Moment-map pass for the CoinRenderShadowPass selected by Core.
// The planned GPU path will supply WebGPU clip conversion and the light view matrix.
struct ShadowUniforms {
    model_view_projection: mat4x4<f32>,
    model_view: mat4x4<f32>,
    near_far_kind: vec4<f32>, // x=near, y=far, z=kind, w=clip count
    clip_model_view: mat4x4<f32>,
    clip_planes: array<vec4<f32>, 8>,
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
};

@vertex
fn vs_moments(input: ShadowVertexInput) -> ShadowVertexOutput {
    var output: ShadowVertexOutput;
    output.clip_position = shadow.model_view_projection *
        vec4<f32>(input.position, 1.0);
    output.light_view_position = (shadow.model_view *
        vec4<f32>(input.position, 1.0)).xyz;
    output.clip_view_position = (shadow.clip_model_view *
        vec4<f32>(input.position, 1.0)).xyz;
    return output;
}

@fragment
fn fs_moments(input: ShadowVertexOutput) -> @location(0) vec4<f32> {
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
    return vec4<f32>(normalized, normalized * normalized, 0.0, 0.0);
}
