// coin_standard.wgsl - Gouraud-interpolated Blinn-Phong reflectance with 2D texture support for Coin3D WebGPU renderer

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
    tex_params: vec4<f32>, // x=has_texture, y=texture_model, z=direct RTT (flip V)
    fog_color_mode: vec4<f32>, // rgb and mode
    fog_range: vec4<f32>, // x=start, y=end
    ambient_light: vec4<f32>,
    light_meta: vec4<f32>,
    lights: array<GpuLight, 8>,
    texture_blend_color: vec4<f32>,
    clip_meta: vec4<f32>,
    clip_planes: array<vec4<f32>, 8>,
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

// Coin PHONG is reflectance, not a request for fragment-normal interpolation.
fn shade_vertex(mat: GpuMaterial, position_view: vec3<f32>, normal_view: vec3<f32>) -> vec4<f32> {
    var base_color: vec4<f32>;

    // CoinRenderLightModel::BASE_COLOR (u.params.w < 0.5):
    // Pure diffuse color without light influence and without emission
    if (u.params.w < 0.5) {
        base_color = vec4<f32>(mat.diffuse.rgb, mat.diffuse.a);
    } else {
        // Coin's global ambient and emission apply even when no light is active.
        let n = normalize(normal_view);
        // Same infinite-viewer contract as Coin/GL, CPU and BGFX.
        let v = vec3<f32>(0.0, 0.0, 1.0);
        var rgb = mat.ambient.rgb * u.ambient_light.rgb + mat.emission.rgb;
        for (var i: u32 = 0u; i < 8u; i = i + 1u) {
            if (f32(i) >= u.light_meta.x) { break; }
            let light = u.lights[i];
            var to_light = vec3<f32>(0.0, 0.0, 1.0);
            var attenuation = 1.0;
            if (light.position_type.w < 0.5) {
                to_light = normalize(-light.direction_cutoff.xyz);
            } else {
                let delta = light.position_type.xyz - position_view;
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
                specular_factor = pow(max(dot(n, h), 0.0), shininess_exp);
            }
            let light_rgb = light.color_intensity.rgb *
                            light.color_intensity.w * attenuation;
            rgb += (mat.diffuse.rgb * diffuse_factor +
                    mat.specular.rgb * specular_factor) * light_rgb;
        }
        base_color = vec4<f32>(clamp(rgb, vec3<f32>(0.0), vec3<f32>(1.0)), mat.diffuse.a);
    }

    return base_color;
}

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let position_view = u.model_view * vec4<f32>(input.position, 1.0);
    output.clip_position =
        u.model_view_projection * vec4<f32>(input.position, 1.0);
    output.position_view = position_view.xyz;
    output.normal_view = normalize(
        (u.normal_matrix * vec4<f32>(input.normal, 0.0)).xyz);
    output.diffuse_color = shade_vertex(materials[input.material_slot],
                                       output.position_view, output.normal_view);
    output.material_slot = input.material_slot;

    if (u.tex_params.x > 0.5) {
        output.texcoord = (u.texture_matrix * vec4<f32>(input.texcoord, 0.0, 1.0)).xy;
    } else {
        output.texcoord = input.texcoord;
    }

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

fn fragment_color(input: VertexOutput) -> vec4<f32> {
    for (var i = 0u; i < 8u; i += 1u) {
        if (f32(i) >= u.clip_meta.x) { break; }
        if (dot(u.clip_planes[i], vec4<f32>(input.position_view, 1.0)) < 0.0) { discard; }
    }
    var base_color = input.diffuse_color;

    if (u.tex_params.x > 0.5) {
        let uv = select(input.texcoord, vec2<f32>(input.texcoord.x, 1.0 - input.texcoord.y), u.tex_params.z > 0.5);
        let tex_col = textureSample(t_diffuse, s_diffuse, uv);
        let model = u.tex_params.y;
        if (model < 0.5) {
            base_color = base_color * tex_col;
        } else if (model < 1.5) {
            base_color = tex_col;
        } else if (model < 2.5) {
            base_color = vec4<f32>(mix(base_color.rgb, tex_col.rgb, tex_col.a),
                                   base_color.a);
        } else {
            base_color = vec4<f32>(mix(base_color.rgb,
                                       u.texture_blend_color.rgb,
                                       tex_col.rgb),
                                   base_color.a * tex_col.a);
        }
    }
    return apply_fog(base_color, -input.position_view.z);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    return fragment_color(input);
}

struct DepthBiasOutput {
    @location(0) color: vec4<f32>,
    @builtin(frag_depth) depth: f32,
};

// The Core supplies the original polygon slope, not the expanded stroke slope.
// Use a full hardware depth range; map Coin's range here before applying bias.
@fragment
fn fs_depth_bias(input: VertexOutput) -> DepthBiasOutput {
    var output: DepthBiasOutput;
    output.color = fragment_color(input);
    output.depth = clamp(mix(u.clip_meta.z, u.clip_meta.w, input.clip_position.z)
                         + u.clip_meta.y, 0.0, 1.0);
    return output;
}
