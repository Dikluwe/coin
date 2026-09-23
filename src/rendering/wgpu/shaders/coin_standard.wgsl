// coin_standard.wgsl - Standard Blinn-Phong shader with 2D texture support for Coin3D WebGPU renderer

struct GpuLight {
    position_type: vec4<f32>,
    direction_cutoff: vec4<f32>,
    color_intensity: vec4<f32>,
    attenuation_exponent: vec4<f32>,
};

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
    tex_params: vec4<f32>, // x=has_texture, y=texture_model, z=unused, w=unused
    ambient_light: vec4<f32>,
    light_meta: vec4<f32>,
    lights: array<GpuLight, 8>,
};

struct GpuMaterial {
    ambient: vec4<f32>,
    diffuse: vec4<f32>,
    specular: vec4<f32>,
    emission: vec4<f32>,
    params: vec4<f32>, // x=shininess, y=transparency, z=unused, w=unused
};

@group(0) @binding(0)
var<uniform> u: Uniforms;

@group(0) @binding(1)
var<storage, read> materials: array<GpuMaterial>;

@group(0) @binding(2)
var t_diffuse: texture_2d<f32>;

@group(0) @binding(3)
var s_diffuse: sampler;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) texcoord: vec2<f32>,
    @location(3) material_slot: u32,
};

struct VertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) position_view: vec3<f32>,
    @location(1) normal_view: vec3<f32>,
    @location(2) diffuse_color: vec4<f32>,
    @location(3) @interpolate(flat) material_slot: u32,
    @location(4) texcoord: vec2<f32>,
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
    output.diffuse_color = materials[input.material_slot].diffuse;
    output.material_slot = input.material_slot;

    if (u.tex_params.x > 0.5) {
        output.texcoord = (u.texture_matrix * vec4<f32>(input.texcoord, 0.0, 1.0)).xy;
    } else {
        output.texcoord = input.texcoord;
    }

    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let mat = materials[input.material_slot];
    var base_color: vec4<f32>;

    // LightModel::BASE_COLOR (u.params.w < 0.5):
    // Pure diffuse color without light influence and without emission
    if (u.params.w < 0.5) {
        base_color = vec4<f32>(input.diffuse_color.rgb, input.diffuse_color.a);
    } else {
        // Coin's global ambient and emission apply even when no light is active.
        let n = normalize(input.normal_view);
        let v = normalize(-input.position_view);
        var rgb = mat.ambient.rgb * u.ambient_light.rgb + mat.emission.rgb;
        for (var i: u32 = 0u; i < 8u; i = i + 1u) {
            if (f32(i) >= u.light_meta.x) { break; }
            let light = u.lights[i];
            var to_light = vec3<f32>(0.0, 0.0, 1.0);
            var attenuation = 1.0;
            if (light.position_type.w < 0.5) {
                to_light = normalize(-light.direction_cutoff.xyz);
            } else {
                let delta = light.position_type.xyz - input.position_view;
                let distance = length(delta);
                if (distance <= 0.000001) { continue; }
                to_light = delta / distance;
                let a = light.attenuation_exponent;
                let denominator = a.z + a.y * distance + a.x * distance * distance;
                if (denominator <= 0.000001) { continue; }
                attenuation = 1.0 / denominator;
                if (light.position_type.w > 1.5) {
                    let cone_cos = dot(normalize(light.direction_cutoff.xyz), -to_light);
                    if (cone_cos < light.direction_cutoff.w) { continue; }
                    attenuation *= pow(max(cone_cos, 0.0), a.w);
                }
            }
            let diffuse_factor = max(dot(n, to_light), 0.0);
            if (diffuse_factor <= 0.0) { continue; }
            let h = normalize(to_light + v);
            let shininess_exp = mat.params.x * 128.0;
            var specular_factor = 1.0;
            if (shininess_exp > 0.0) {
                specular_factor = pow(max(dot(n, h), 0.0001), shininess_exp);
            }
            let light_rgb = light.color_intensity.rgb *
                            light.color_intensity.w * attenuation;
            rgb += (input.diffuse_color.rgb * diffuse_factor +
                    mat.specular.rgb * specular_factor) * light_rgb;
        }
        base_color = vec4<f32>(rgb, input.diffuse_color.a);
    }

    if (u.tex_params.x > 0.5) {
        let tex_col = textureSample(t_diffuse, s_diffuse, input.texcoord);
        // MODULATE: multiply base color by texture sample
        return vec4<f32>(base_color.rgb * tex_col.rgb, base_color.a * tex_col.a);
    } else {
        return base_color;
    }
}
