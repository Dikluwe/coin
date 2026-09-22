// coin_standard.wgsl - Standard Blinn-Phong shader for Coin3D WebGPU renderer

struct Uniforms {
    model_view_projection: mat4x4<f32>,
    model_view: mat4x4<f32>,
    normal_matrix: mat4x4<f32>,
    material_diffuse: vec4<f32>,
    material_ambient: vec4<f32>,
    material_specular: vec4<f32>,
    light_direction_intensity: vec4<f32>,
    light_color: vec4<f32>,
    params: vec4<f32>, // x=shininess, y=headlight, z=light_enabled
};

@group(0) @binding(0)
var<uniform> u: Uniforms;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
};

struct VertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) position_view: vec3<f32>,
    @location(1) normal_view: vec3<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let position_view = u.model_view * vec4<f32>(input.position, 1.0);
    output.clip_position =
        u.model_view_projection * vec4<f32>(input.position, 1.0);
    output.position_view = position_view.xyz;
    output.normal_view = normalize(
        (u.normal_matrix * vec4<f32>(input.normal, 0.0)).xyz);
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let n = normalize(input.normal_view);
    var l = vec3<f32>(0.0, 0.0, 1.0);
    if (u.params.y <= 0.5 && u.params.z > 0.5) {
        l = normalize(u.light_direction_intensity.xyz);
    }
    let v = normalize(-input.position_view);
    let h = normalize(l + v);

    var diffuse_factor = 0.0;
    if (u.params.y > 0.5 || u.params.z > 0.5) {
        diffuse_factor = max(dot(n, l), 0.0);
    }
    let specular_factor = select(
        0.0,
        pow(max(dot(n, h), 0.0), max(u.params.x, 1.0)),
        diffuse_factor > 0.0);

    let ambient = u.material_ambient.rgb;
    let diffuse = u.material_diffuse.rgb * u.light_color.rgb *
                  diffuse_factor * u.light_direction_intensity.w;
    let specular = u.material_specular.rgb * u.light_color.rgb *
                   specular_factor * u.light_direction_intensity.w;
    return vec4<f32>(ambient + diffuse + specular,
                     u.material_diffuse.a);
}
