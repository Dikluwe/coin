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
    extra_texture_matrices: array<mat4x4<f32>, 7>,
    extra_tex_params: array<vec4<f32>, 7>,
    extra_texture_blends: array<vec4<f32>, 7>,
    texture_combines: array<array<vec4<f32>, 4>, 8>,
    composition_meta: vec4<f32>,
    peel_meta: vec4<f32>,
    shadow_model_view_projection: mat4x4<f32>,
    shadow_model_view: mat4x4<f32>,
    shadow_params: vec4<f32>, // x=receives, y=near, z=far, w=epsilon
    shadow_meta: vec4<f32>, // threshold, lighting index, kind, max shadow distance
    shadow_falloff: vec4<f32>, // Core-resolved exponential curve coefficient
    shadow_model_view_projection_second: mat4x4<f32>,
    shadow_model_view_second: mat4x4<f32>,
    shadow_params_second: vec4<f32>,
    shadow_meta_second: vec4<f32>,
    shadow_falloff_second: vec4<f32>,
    shadow_model_view_projection_third: mat4x4<f32>,
    shadow_model_view_third: mat4x4<f32>,
    shadow_params_third: vec4<f32>,
    shadow_meta_third: vec4<f32>,
    shadow_falloff_third: vec4<f32>,
    shadow_model_view_projection_fourth: mat4x4<f32>,
    shadow_model_view_fourth: mat4x4<f32>,
    shadow_params_fourth: vec4<f32>,
    shadow_meta_fourth: vec4<f32>,
    shadow_falloff_fourth: vec4<f32>,
    alpha_test: vec4<f32>, // semantic function, clamped reference
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
@group(0) @binding(4) var t_texture1: texture_2d<f32>;
@group(0) @binding(5) var s_texture1: sampler;
@group(0) @binding(6) var t_texture2: texture_2d<f32>;
@group(0) @binding(7) var s_texture2: sampler;
@group(0) @binding(8) var t_texture3: texture_2d<f32>;
@group(0) @binding(9) var s_texture3: sampler;
@group(0) @binding(10) var t_texture4: texture_2d<f32>;
@group(0) @binding(11) var s_texture4: sampler;
@group(0) @binding(12) var t_texture5: texture_2d<f32>;
@group(0) @binding(13) var s_texture5: sampler;
@group(0) @binding(14) var t_texture6: texture_2d<f32>;
@group(0) @binding(15) var s_texture6: sampler;
@group(0) @binding(16) var t_texture7: texture_2d<f32>;
@group(0) @binding(17) var s_texture7: sampler;
@group(0) @binding(20) var t_shadow: texture_2d<f32>;
@group(0) @binding(21) var t_shadow_second: texture_2d<f32>;
@group(0) @binding(22) var t_shadow_third: texture_2d<f32>;
@group(0) @binding(23) var t_shadow_fourth: texture_2d<f32>;


struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) texcoord: vec2<f32>,
    @location(3) material_slot: u32,
    @location(4) screen_space_w: f32,
    @location(5) fog_eye_depth_plus_one: f32,
    @location(6) uv1: vec2<f32>,
    @location(7) uv2: vec2<f32>,
    @location(8) uv3: vec2<f32>,
    @location(9) uv4: vec2<f32>,
    @location(10) uv5: vec2<f32>,
    @location(11) uv6: vec2<f32>,
    @location(12) uv7: vec2<f32>,

};

struct VertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) position_view: vec3<f32>,
    @location(1) normal_view: vec3<f32>,
    @location(2) diffuse_color: vec4<f32>,
    @location(3) @interpolate(flat) material_slot: u32,
    @location(4) texcoord: vec2<f32>,
    @location(5) uv1: vec2<f32>,
    @location(6) uv2: vec2<f32>,
    @location(7) uv3: vec2<f32>,
    @location(8) uv4: vec2<f32>,
    @location(9) uv5: vec2<f32>,
    @location(10) uv6: vec2<f32>,
    @location(11) uv7: vec2<f32>,
    @location(12) shadow_coord: vec4<f32>,
    @location(13) shadow_light_view: vec3<f32>,
    @location(14) shadow_coord_second: vec4<f32>,
    @location(15) shadow_light_view_second: vec3<f32>,
};

// Coin's PHONG contribution for one captured light. The first shadow profile
// uses the same formula in the fragment as the ordinary vertex path.
fn light_contribution(light: GpuLight, mat: GpuMaterial,
                      position_view: vec3<f32>, normal_view: vec3<f32>) -> vec3<f32> {
    let n = normalize(normal_view);
    let v = vec3<f32>(0.0, 0.0, 1.0);
    var to_light = vec3<f32>(0.0, 0.0, 1.0);
    var attenuation = 1.0;
    if (light.position_type.w < 0.5) {
        to_light = normalize(-light.direction_cutoff.xyz);
    } else {
        let delta = light.position_type.xyz - position_view;
        let distance = length(delta);
        if (distance <= 0.000001) { return vec3<f32>(0.0); }
        to_light = delta / distance;
        let a = light.attenuation_exponent;
        let denominator = a.z + a.y * distance + a.x * distance * distance;
        if (denominator <= 0.000001) { return vec3<f32>(0.0); }
        attenuation = 1.0 / denominator;
        if (light.position_type.w > 1.5) {
            let cone_cos = dot(normalize(light.direction_cutoff.xyz), -to_light);
            if (cone_cos < light.direction_cutoff.w) { return vec3<f32>(0.0); }
            attenuation *= pow(max(cone_cos, 0.0), a.w);
        }
    }
    let diffuse_factor = max(dot(n, to_light), 0.0);
    if (diffuse_factor <= 0.0) { return vec3<f32>(0.0); }
    let h = normalize(to_light + v);
    let shininess_exp = mat.params.x * 128.0;
    var specular_factor = 1.0;
    if (shininess_exp > 0.0) {
        specular_factor = pow(max(dot(n, h), 0.0), shininess_exp);
    }
    let light_rgb = light.color_intensity.rgb *
                    light.color_intensity.w * attenuation;
    return (mat.diffuse.rgb * diffuse_factor +
            mat.specular.rgb * specular_factor) * light_rgb;
}

// Coin PHONG is reflectance, not a request for fragment-normal interpolation
// unless the active shadow profile explicitly selects per-fragment lighting.
fn shade_vertex(mat: GpuMaterial, position_view: vec3<f32>, normal_view: vec3<f32>) -> vec4<f32> {
    if (u.light_meta.y > 0.5) {
        return vec4<f32>(0.0, 0.0, 0.0, mat.diffuse.a);
    }
    if (u.params.w < 0.5) {
        return vec4<f32>(mat.diffuse.rgb, mat.diffuse.a);
    }
    var rgb = mat.ambient.rgb * u.ambient_light.rgb + mat.emission.rgb;
    for (var i: u32 = 0u; i < 8u; i = i + 1u) {
        if (f32(i) >= u.light_meta.x) { break; }
        if ((u.shadow_params.x > 0.5 && i == u32(u.shadow_meta.y)) ||
            (u.shadow_params_second.x > 0.5 && i == u32(u.shadow_meta_second.y)) ||
            (u.shadow_params_third.x > 0.5 && i == u32(u.shadow_meta_third.y)) ||
            (u.shadow_params_fourth.x > 0.5 && i == u32(u.shadow_meta_fourth.y))
            /* COIN_EXTRA_SHADOW_OWNS_LIGHT */) { continue; }
        rgb += light_contribution(u.lights[i], mat, position_view, normal_view);
    }
    return vec4<f32>(clamp(rgb, vec3<f32>(0.0), vec3<f32>(1.0)), mat.diffuse.a);
}

// COIN_EXTRA_SHADOW_DECLARATIONS
fn vsm_shadow_factor(coord: vec4<f32>, light_view: vec3<f32>,
                     position_view: vec3<f32>, params: vec4<f32>,
                     shadow_info: vec4<f32>, falloff: vec4<f32>,
                     shadow_map: texture_2d<f32>) -> f32 {
    if (coord.w <= 0.0) { return 1.0; }
    let clip = coord.xyz / coord.w;
    let uv = vec2<f32>(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);
    if (uv.x < 0.0 || uv.x >= 1.0 || uv.y < 0.0 || uv.y >= 1.0 ||
        clip.z < 0.0 || clip.z > 1.0) { return 1.0; }
    let size = textureDimensions(shadow_map);
    let pixel = clamp(vec2<i32>(uv * vec2<f32>(size)),
                      vec2<i32>(0), vec2<i32>(size) - vec2<i32>(1));
    let map = textureLoad(shadow_map, pixel, 0).xy;
    if (map.x >= 0.9999) { return 1.0; }
    let distance = select(-light_view.z,
        length(light_view), shadow_info.z > 0.5);
    let dist = (distance - params.y) /
               (params.z - params.y);
    if (dist <= map.x) { return 1.0; }
    let variance = min(max(map.y - map.x * map.x, 0.0) + params.w, 1.0);
    let delta = map.x - dist;
    var probability = variance / (variance + delta * delta);
    probability *= smoothstep(shadow_info.x, 1.0, probability);
    if (shadow_info.z < 0.5 && shadow_info.w > 0.0) {
        let eye_z = position_view.z;
        let fade = min(1.0, exp(falloff.x * eye_z * abs(eye_z) /
                                (shadow_info.w * shadow_info.w)));
        return 1.0 - (1.0 - probability) * fade;
    }
    return probability;
}
@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let position_view = u.model_view * vec4<f32>(input.position, 1.0);
    output.clip_position =
        u.model_view_projection * vec4<f32>(input.position, 1.0);
    output.position_view = position_view.xyz;
    output.clip_position *= select(1.0, input.screen_space_w, input.screen_space_w > 0.0);
    if (input.fog_eye_depth_plus_one > 0.0) {
        output.position_view.z = 1.0 - input.fog_eye_depth_plus_one;
    }
    output.normal_view = normalize(
        (u.normal_matrix * vec4<f32>(input.normal, 0.0)).xyz);
    output.diffuse_color = shade_vertex(materials[input.material_slot],
                                       output.position_view, output.normal_view);
    output.material_slot = input.material_slot;
    output.shadow_coord = u.shadow_model_view_projection *
        vec4<f32>(input.position, 1.0);
    output.shadow_light_view = (u.shadow_model_view *
        vec4<f32>(input.position, 1.0)).xyz;
    output.shadow_coord_second = u.shadow_model_view_projection_second *
        vec4<f32>(input.position, 1.0);
    output.shadow_light_view_second = (u.shadow_model_view_second *
        vec4<f32>(input.position, 1.0)).xyz;

    if (u.tex_params.x > 0.5) {
        output.texcoord = (u.texture_matrix * vec4<f32>(input.texcoord, 0.0, 1.0)).xy;
    } else {
        output.texcoord = input.texcoord;
    }

    output.uv1 = (u.extra_texture_matrices[0] * vec4<f32>(input.uv1, 0.0, 1.0)).xy;
    output.uv2 = (u.extra_texture_matrices[1] * vec4<f32>(input.uv2, 0.0, 1.0)).xy;
    output.uv3 = (u.extra_texture_matrices[2] * vec4<f32>(input.uv3, 0.0, 1.0)).xy;
    output.uv4 = (u.extra_texture_matrices[3] * vec4<f32>(input.uv4, 0.0, 1.0)).xy;
    output.uv5 = (u.extra_texture_matrices[4] * vec4<f32>(input.uv5, 0.0, 1.0)).xy;
    output.uv6 = (u.extra_texture_matrices[5] * vec4<f32>(input.uv6, 0.0, 1.0)).xy;
    output.uv7 = (u.extra_texture_matrices[6] * vec4<f32>(input.uv7, 0.0, 1.0)).xy;
    return output;
}

// Mechanical execution of CoinRender's validated texture-combine program.
fn combine_arg(code: f32, primary: vec4<f32>, tex: vec4<f32>, constant: vec4<f32>, previous: vec4<f32>) -> vec4<f32> {
    let instruction = u32(code);
    let source = instruction % 4u;
    let operand = instruction / 4u;
    var value = previous;
    if (source == 0u) { value = primary; }
    if (source == 1u) { value = tex; }
    if (source == 2u) { value = constant; }
    if (operand >= 2u) { value = vec4<f32>(value.a); }
    if (operand == 1u || operand == 3u) { value = vec4<f32>(1.0) - value; }
    return value;
}
fn combine_op(op: f32, a: vec4<f32>, b: vec4<f32>, c: vec4<f32>) -> vec4<f32> {
    if (op < 0.5) { return a; }
    if (op < 1.5) { return a * b; }
    if (op < 2.5) { return a + b; }
    if (op < 3.5) { return a + b - vec4<f32>(0.5); }
    if (op < 4.5) { return a - b; }
    if (op < 5.5) { return a * c + b * (vec4<f32>(1.0) - c); }
    return vec4<f32>(4.0 * dot(a.rgb - vec3<f32>(0.5), b.rgb - vec3<f32>(0.5)));
}
fn texture_layer(primary: vec4<f32>, previous: vec4<f32>, tex: vec4<f32>, params: vec4<f32>, blend: vec4<f32>, unit: u32) -> vec4<f32> {
    let p = u.texture_combines[unit];
    if (p[0].x > 0.5) {
        let rgb = combine_op(p[0].y, combine_arg(p[1].x, primary, tex, p[3], previous),
            combine_arg(p[1].y, primary, tex, p[3], previous), combine_arg(p[1].z, primary, tex, p[3], previous));
        let alpha = combine_op(p[0].z, combine_arg(p[2].x, primary, tex, p[3], previous),
            combine_arg(p[2].y, primary, tex, p[3], previous), combine_arg(p[2].z, primary, tex, p[3], previous));
        let a = select(alpha.a * p[2].w, rgb.r * p[2].w, p[0].y > 6.5);
        return clamp(vec4<f32>(rgb.rgb * p[1].w, a), vec4<f32>(0.0), vec4<f32>(1.0));
    }
    if (params.y < 0.5) { return previous * tex; }
    if (params.y < 1.5) { return tex; }
    if (params.y < 2.5) { return vec4<f32>(mix(previous.rgb, tex.rgb, tex.a), previous.a); }
    return vec4<f32>(mix(previous.rgb, blend.rgb, tex.rgb), previous.a * tex.a);
}

fn alpha_test_accepts(value: f32) -> bool {
    let alpha = clamp(value, 0.0, 1.0);
    let reference = u.alpha_test.y;
    switch u32(u.alpha_test.x) {
        case 0u, 2u: { return true; }
        case 1u: { return false; }
        case 3u: { return alpha < reference; }
        case 4u: { return alpha <= reference; }
        case 5u: { return alpha == reference; }
        case 6u: { return alpha >= reference; }
        case 7u: { return alpha > reference; }
        case 8u: { return alpha != reference; }
        default: { return false; }
    }
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
    if (u.composition_meta.x > 0.5) {
        var x=u32(input.clip_position.x); var y=u32(u.composition_meta.z-input.clip_position.y);
        var rank=0u; var weight=256u;
        for(var bit=0u;bit<5u;bit+=1u) {
            let bx=x&1u;let by=y&1u;
            rank+=select(2u*bx,3u-2u*bx,by!=0u)*weight;
            x>>=1u;y>>=1u;weight>>=2u;
        }
        if(rank<u32(u.composition_meta.x)*16u){discard;}
    }
    var primary=input.diffuse_color;
    if (u.shadow_params.x > 0.5 && u.params.w > 0.5) {
        let light = u.lights[u32(u.shadow_meta.y)];
        let contribution = light_contribution(light,
            materials[input.material_slot], input.position_view, input.normal_view);
        primary = vec4<f32>(
            clamp(primary.rgb + contribution * vsm_shadow_factor(input.shadow_coord, input.shadow_light_view,
                input.position_view, u.shadow_params, u.shadow_meta,
                u.shadow_falloff, t_shadow),
                  vec3<f32>(0.0), vec3<f32>(1.0)),
            primary.a);
    }
    if (u.shadow_params_second.x > 0.5 && u.params.w > 0.5) {
        let light = u.lights[u32(u.shadow_meta_second.y)];
        let contribution = light_contribution(light,
            materials[input.material_slot], input.position_view, input.normal_view);
        primary = vec4<f32>(
            clamp(primary.rgb + contribution * vsm_shadow_factor(input.shadow_coord_second, input.shadow_light_view_second,
                input.position_view, u.shadow_params_second, u.shadow_meta_second,
                u.shadow_falloff_second, t_shadow_second),
                  vec3<f32>(0.0), vec3<f32>(1.0)), primary.a);
    }
    if (u.shadow_params_third.x > 0.5 && u.params.w > 0.5) {
        let light = u.lights[u32(u.shadow_meta_third.y)];
        let contribution = light_contribution(light,
            materials[input.material_slot], input.position_view, input.normal_view);
        let coord = u.shadow_model_view_projection_third *
            vec4<f32>(input.position_view, 1.0);
        let light_view = (u.shadow_model_view_third *
            vec4<f32>(input.position_view, 1.0)).xyz;
        primary = vec4<f32>(
            clamp(primary.rgb + contribution * vsm_shadow_factor(coord, light_view,
                input.position_view, u.shadow_params_third, u.shadow_meta_third,
                u.shadow_falloff_third, t_shadow_third),
                  vec3<f32>(0.0), vec3<f32>(1.0)), primary.a);
    }
    if (u.shadow_params_fourth.x > 0.5 && u.params.w > 0.5) {
        let light = u.lights[u32(u.shadow_meta_fourth.y)];
        let contribution = light_contribution(light,
            materials[input.material_slot], input.position_view, input.normal_view);
        let coord = u.shadow_model_view_projection_fourth *
            vec4<f32>(input.position_view, 1.0);
        let light_view = (u.shadow_model_view_fourth *
            vec4<f32>(input.position_view, 1.0)).xyz;
        primary = vec4<f32>(
            clamp(primary.rgb + contribution * vsm_shadow_factor(coord, light_view,
                input.position_view, u.shadow_params_fourth, u.shadow_meta_fourth,
                u.shadow_falloff_fourth, t_shadow_fourth),
                  vec3<f32>(0.0), vec3<f32>(1.0)), primary.a);
    }
    // COIN_EXTRA_SHADOW_SURFACE
    if(u.composition_meta.y>0.5){primary.a=1.0;}
    var base_color = primary;

    if (u.tex_params.x > 0.5) {
        let uv = select(input.texcoord, vec2<f32>(input.texcoord.x, 1.0 - input.texcoord.y), u.tex_params.z > 0.5);
        let tex_col = textureSample(t_diffuse, s_diffuse, uv);
        base_color = texture_layer(primary, base_color, tex_col, u.tex_params, u.texture_blend_color, 0u);
    }
    if (u.extra_tex_params[0].x > 0.5) {
        let params = u.extra_tex_params[0];
        let uv = select(input.uv1, vec2<f32>(input.uv1.x, 1.0 - input.uv1.y), params.z > 0.5);
        let tex = textureSample(t_texture1, s_texture1, uv);
        base_color = texture_layer(primary, base_color, tex, params, u.extra_texture_blends[0], 1u);
    }
    if (u.extra_tex_params[1].x > 0.5) {
        let params = u.extra_tex_params[1];
        let uv = select(input.uv2, vec2<f32>(input.uv2.x, 1.0 - input.uv2.y), params.z > 0.5);
        let tex = textureSample(t_texture2, s_texture2, uv);
        base_color = texture_layer(primary, base_color, tex, params, u.extra_texture_blends[1], 2u);
    }
    if (u.extra_tex_params[2].x > 0.5) {
        let params = u.extra_tex_params[2];
        let uv = select(input.uv3, vec2<f32>(input.uv3.x, 1.0 - input.uv3.y), params.z > 0.5);
        let tex = textureSample(t_texture3, s_texture3, uv);
        base_color = texture_layer(primary, base_color, tex, params, u.extra_texture_blends[2], 3u);
    }
    // COIN_UPPER_TEXTURE_UNITS_BEGIN
    if (u.extra_tex_params[3].x > 0.5) {
        let params = u.extra_tex_params[3];
        let uv = select(input.uv4, vec2<f32>(input.uv4.x, 1.0 - input.uv4.y), params.z > 0.5);
        let tex = textureSample(t_texture4, s_texture4, uv);
        base_color = texture_layer(primary, base_color, tex, params, u.extra_texture_blends[3], 4u);
    }
    if (u.extra_tex_params[4].x > 0.5) {
        let params = u.extra_tex_params[4];
        let uv = select(input.uv5, vec2<f32>(input.uv5.x, 1.0 - input.uv5.y), params.z > 0.5);
        let tex = textureSample(t_texture5, s_texture5, uv);
        base_color = texture_layer(primary, base_color, tex, params, u.extra_texture_blends[4], 5u);
    }
    if (u.extra_tex_params[5].x > 0.5) {
        let params = u.extra_tex_params[5];
        let uv = select(input.uv6, vec2<f32>(input.uv6.x, 1.0 - input.uv6.y), params.z > 0.5);
        let tex = textureSample(t_texture6, s_texture6, uv);
        base_color = texture_layer(primary, base_color, tex, params, u.extra_texture_blends[5], 6u);
    }
    if (u.extra_tex_params[6].x > 0.5) {
        let params = u.extra_tex_params[6];
        let uv = select(input.uv7, vec2<f32>(input.uv7.x, 1.0 - input.uv7.y), params.z > 0.5);
        let tex = textureSample(t_texture7, s_texture7, uv);
        base_color = texture_layer(primary, base_color, tex, params, u.extra_texture_blends[6], 7u);
    }
    // COIN_UPPER_TEXTURE_UNITS_END
    if (!alpha_test_accepts(base_color.a)) { discard; }
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

@group(0) @binding(18) var previous_depth:texture_depth_2d;
@group(0) @binding(19) var opaque_depth:texture_depth_2d;
struct PeelOutput {
    @location(0) color:vec4<f32>,
    @location(1) write_depth:f32,
    @builtin(frag_depth) depth:f32,
};
fn depth_accepts(depth:f32,stored:f32,op:u32)->bool {
    switch op {
        case 0u:{return false;} case 1u:{return true;} case 2u:{return depth<stored;}
        case 3u:{return depth<=stored;} case 4u:{return depth==stored;} case 5u:{return depth>=stored;}
        case 6u:{return depth>stored;} default:{return depth!=stored;}
    }
}
@fragment fn fs_peel(input:VertexOutput)->PeelOutput {
    var output:PeelOutput;output.color=fragment_color(input);
    let xy=vec2<i32>(input.clip_position.xy);
    let depth=clamp(mix(u.clip_meta.z,u.clip_meta.w,input.clip_position.z)+u.clip_meta.y,0.0,1.0);
    if(u.peel_meta.y>0.5 && !depth_accepts(depth,textureLoad(opaque_depth,xy,0),u32(u.peel_meta.z))){discard;}
    if(u.peel_meta.x>1.5 && depth<=textureLoad(previous_depth,xy,0)){discard;}
    output.depth=depth;output.write_depth=u.peel_meta.w;return output;
}
