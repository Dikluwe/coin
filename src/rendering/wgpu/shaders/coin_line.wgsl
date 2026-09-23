// coin_line.wgsl - Dedicated line shader for Coin3D WebGPU renderer

struct Uniforms {
    model_view_projection: mat4x4<f32>,
    model_view: mat4x4<f32>,
    normal_matrix: mat4x4<f32>,
    material_diffuse: vec4<f32>,
    material_ambient: vec4<f32>,
    material_specular: vec4<f32>,
    light_direction_intensity: vec4<f32>,
    light_color: vec4<f32>,
    params: vec4<f32>, // x=shininess, y=headlight, z=light_enabled, w=light_model
    texture_matrix: mat4x4<f32>,
    tex_params: vec4<f32>,
    fog_color_mode: vec4<f32>,
    fog_range: vec4<f32>,
};

struct GpuMaterial {
    ambient: vec4<f32>,
    diffuse: vec4<f32>,
    specular: vec4<f32>,
    emission: vec4<f32>,
    params: vec4<f32>,
};

@group(0) @binding(0)
var<uniform> u: Uniforms;

@group(0) @binding(1)
var<storage, read> materials: array<GpuMaterial>;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) texcoord: vec2<f32>,
    @location(3) material_slot: u32,
};

struct VertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) eye_depth: f32,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.clip_position = u.model_view_projection * vec4<f32>(input.position, 1.0);
    output.eye_depth = -(u.model_view * vec4<f32>(input.position, 1.0)).z;
    let mat = materials[input.material_slot];
    var col: vec3<f32>;
    if (u.params.w < 0.5) {
        col = mat.diffuse.rgb;
    } else {
        col = mat.diffuse.rgb + mat.ambient.rgb + mat.emission.rgb;
    }
    output.color = vec4<f32>(clamp(col, vec3<f32>(0.0), vec3<f32>(1.0)), mat.diffuse.a);
    return output;
}

fn apply_fog(color: vec4<f32>, eye_depth: f32) -> vec4<f32> {
    let mode = u.fog_color_mode.w;
    if (mode < 0.5) { return color; }
    let distance = max(eye_depth, 0.0);
    let end_distance = u.fog_range.y;
    var factor = 1.0;
    if (mode < 1.5) {
        factor = clamp((end_distance - distance) /
                       (end_distance - u.fog_range.x), 0.0, 1.0);
    } else if (mode < 2.5) {
        factor = clamp(exp(-5.545 * distance / end_distance), 0.0, 1.0);
    } else {
        let x = 2.35 * distance / end_distance;
        factor = clamp(exp(-(x * x)), 0.0, 1.0);
    }
    return vec4<f32>(mix(u.fog_color_mode.rgb, color.rgb, factor), color.a);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    return apply_fog(input.color, input.eye_depth);
}
