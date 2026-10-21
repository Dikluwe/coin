// R26 legacy bump lighting passes. A dedicated shader keeps the standard shader's
// sixteen interpolator locations available for multitexture and shadows.
struct BumpUniforms {
    model_view_projection: mat4x4<f32>,
    light_position_type: vec4<f32>, // Object-space vector or position; w=1 for point.
    diffuse: vec4<f32>,
    eye_position: vec4<f32>, // Object-space projection point from CoinGL.
    specular: vec4<f32>,
    shininess: vec4<f32>, // x=Coin shininess; legacy exponent is x*64.
};

@group(0) @binding(0) var<uniform> bump: BumpUniforms;
@group(0) @binding(1) var bump_image: texture_2d<f32>;
@group(0) @binding(2) var bump_sampler: sampler;

struct BumpBaseUniforms {
    texture_matrix: mat4x4<f32>,
};
@group(1) @binding(0) var<uniform> bump_base: BumpBaseUniforms;
@group(1) @binding(1) var base_image: texture_2d<f32>;
@group(1) @binding(2) var base_sampler: sampler;

struct BumpInput {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) coord: vec4<f32>,
    @location(3) tangent_s: vec3<f32>,
    @location(4) tangent_t: vec3<f32>,
};

struct BumpOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) coord: vec4<f32>,
    @location(1) tangent_light: vec3<f32>,
    @location(2) tangent_eye: vec3<f32>,
};

fn project_bump(input: BumpInput) -> BumpOutput {
    var output: BumpOutput;
    output.clip_position = bump.model_view_projection * vec4<f32>(input.position, 1.0);
    output.coord = input.coord;
    var light = bump.light_position_type.xyz;
    if (bump.light_position_type.w > 0.5) {
        light -= input.position;
    }
    let length_squared = dot(light, light);
    if (length_squared > 0.0) {
        light *= inverseSqrt(length_squared);
    }
    output.tangent_light = vec3<f32>(
        dot(input.tangent_s, light),
        dot(input.tangent_t, light),
        dot(input.normal, light));
    var eye = bump.eye_position.xyz - input.position;
    let eye_length_squared = dot(eye, eye);
    if (eye_length_squared > 0.0) {
        eye *= inverseSqrt(eye_length_squared);
    }
    output.tangent_eye = vec3<f32>(
        dot(input.tangent_s, eye),
        dot(input.tangent_t, eye),
        dot(input.normal, eye));
    return output;
}

@vertex
fn vs_main(input: BumpInput) -> BumpOutput {
    return project_bump(input);
}

struct BumpBaseInput {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) coord: vec4<f32>,
    @location(3) tangent_s: vec3<f32>,
    @location(4) tangent_t: vec3<f32>,
    @location(5) base_coord: vec4<f32>,
};

struct BumpBaseOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) coord: vec4<f32>,
    @location(1) tangent_light: vec3<f32>,
    @location(2) tangent_eye: vec3<f32>,
    @location(3) base_coord: vec4<f32>,
};

@vertex
fn vs_base(input: BumpBaseInput) -> BumpBaseOutput {
    let core = project_bump(BumpInput(input.position, input.normal, input.coord,
                                      input.tangent_s, input.tangent_t));
    return BumpBaseOutput(core.clip_position, core.coord, core.tangent_light,
                          core.tangent_eye, bump_base.texture_matrix * input.base_coord);
}

fn diffuse_factor(coord: vec4<f32>, tangent_light_input: vec3<f32>) -> f32 {
    let uv = coord.xy / coord.w;
    let encoded_normal = textureSample(bump_image, bump_sampler, uv).rgb;
    let light_length_squared = dot(tangent_light_input, tangent_light_input);
    var tangent_light = vec3<f32>(0.0);
    if (light_length_squared > 0.0) {
        tangent_light = tangent_light_input * inverseSqrt(light_length_squared);
    }
    return clamp(dot(encoded_normal * 2.0 - vec3<f32>(1.0),
                     tangent_light), 0.0, 1.0);
}

@fragment
fn fs_main(input: BumpOutput) -> @location(0) vec4<f32> {
    return vec4<f32>(bump.diffuse.rgb * diffuse_factor(input.coord, input.tangent_light),
                     bump.diffuse.a);
}

@fragment
fn fs_base(input: BumpBaseOutput) -> @location(0) vec4<f32> {
    let uv = input.base_coord.xy / input.base_coord.w;
    let base_color = textureSample(base_image, base_sampler, uv).rgb;
    return vec4<f32>(bump.diffuse.rgb * base_color *
                     diffuse_factor(input.coord, input.tangent_light), 1.0);
}

@fragment
fn fs_specular(input: BumpOutput) -> @location(0) vec4<f32> {
    let uv = input.coord.xy / input.coord.w;
    let normal = textureSample(bump_image, bump_sampler, uv).rgb * 2.0 - vec3<f32>(1.0);
    let half_vector = input.tangent_light + input.tangent_eye;
    let half_length_squared = dot(half_vector, half_vector);
    var normalized_half = vec3<f32>(0.0);
    if (half_length_squared > 0.0) {
        normalized_half = half_vector * inverseSqrt(half_length_squared);
    }
    let factor = pow(clamp(dot(normal, normalized_half), 0.0, 1.0),
                     bump.shininess.x * 64.0);
    return bump.specular * factor;
}
