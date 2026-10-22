// coin_point.wgsl - Gouraud-interpolated Blinn-Phong reflectance with 2D texture support for Coin3D WebGPU renderer

struct GpuLight {
    position_type: vec4<f32>,
    direction_cutoff: vec4<f32>,
    color_intensity: vec4<f32>,
    attenuation_exponent: vec4<f32>,
    ambient_color: vec4<f32>,
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
    tex_params: vec4<f32>, // x=has_texture, y=texture_model, z=direct RTT (flip V), w=texture projection policy
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
    // Preserve the standard uniform tail offset in this shadow-free profile.
    shadow_layout_padding: array<vec4<f32>, 44>,
    alpha_test: vec4<f32>, // semantic function, clamped reference
    raster_viewport: vec4<f32>,
    flat_shadow_primary: vec4<u32>,
    sampling_texture_sizes: array<vec4<f32>,8>,
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


struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) texcoord: vec4<f32>,
    @location(3) material_slot: u32,
    @location(4) screen_space_w: f32,
    @location(5) fog_eye_depth_plus_one: f32,
    @location(6) uv1: vec4<f32>,
    @location(7) uv2: vec4<f32>,
    @location(8) uv3: vec4<f32>,
    @location(9) uv4: vec4<f32>,
    @location(10) uv5: vec4<f32>,
    @location(11) uv6: vec4<f32>,
    @location(12) uv7: vec4<f32>,

};

struct VertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) position_view: vec3<f32>,
    @location(1) normal_view: vec3<f32>,
    @location(2) diffuse_color: vec4<f32>,
    @location(3) @interpolate(flat) material_slot: u32,
    @location(4) texcoord: vec4<f32>,
    @location(5) uv1: vec4<f32>,
    @location(6) uv2: vec4<f32>,
    @location(7) uv3: vec4<f32>,
    @location(8) uv4: vec4<f32>,
    @location(9) uv5: vec4<f32>,
    @location(10) uv6: vec4<f32>,
    @location(11) uv7: vec4<f32>,

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
            rgb += mat.ambient.rgb * light.ambient_color.rgb * attenuation;
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
    output.clip_position *= select(1.0, input.screen_space_w, input.screen_space_w > 0.0);
    if (input.fog_eye_depth_plus_one > 0.0) {
        output.position_view.z = 1.0 - input.fog_eye_depth_plus_one;
    }
    output.normal_view = normalize(
        (u.normal_matrix * vec4<f32>(input.normal, 0.0)).xyz);
    output.diffuse_color = shade_vertex(materials[input.material_slot],
                                       output.position_view, output.normal_view);
    output.material_slot = input.material_slot;

    if (u.tex_params.x > 0.5) {
        output.texcoord = (u.texture_matrix * input.texcoord);
    } else {
        output.texcoord = input.texcoord;
    }

    output.uv1 = (u.extra_texture_matrices[0] * input.uv1);
    output.uv2 = (u.extra_texture_matrices[1] * input.uv2);
    output.uv3 = (u.extra_texture_matrices[2] * input.uv3);
    output.uv4 = (u.extra_texture_matrices[3] * input.uv4);
    output.uv5 = (u.extra_texture_matrices[4] * input.uv5);
    output.uv6 = (u.extra_texture_matrices[5] * input.uv6);
    output.uv7 = (u.extra_texture_matrices[6] * input.uv7);
    return output;
}

// Coin's active shadow shader samples transformed ST without Q division.
// Other paths preserve Q through interpolation and keep ordinary q=1 UV fast.
fn coin_portable_lerp(a: vec4<f32>, b: vec4<f32>, w: f32) -> vec4<f32> { return a + (b-a)*w; }
fn coin_volume_index(value: i32, extent: i32, clamp_axis: bool) -> u32 {
    if clamp_axis { return u32(clamp(value, 0, extent - 1)); }
    return u32(((value % extent) + extent) % extent);
}
fn coin_volume_fetch(t: texture_2d<f32>, x: i32, y: i32, z: i32,
                     extent: vec3<u32>, columns: u32, y_base: u32, flags: u32) -> vec4<f32> {
    let ix = coin_volume_index(x, i32(extent.x), (flags & 1u) != 0u);
    let iy = coin_volume_index(y, i32(extent.y), (flags & 2u) != 0u);
    let iz = coin_volume_index(z, i32(extent.z), (flags & 4u) != 0u);
    let atlas_x = (iz % columns) * extent.x + ix;
    let atlas_y = y_base + (iz / columns) * extent.y + iy;
    return textureLoad(t, vec2<i32>(i32(atlas_x), i32(atlas_y)), 0);
}
fn coin_volume_level(t: texture_2d<f32>, coordinate: vec3<f32>, size: vec3<u32>,
                     columns: u32, flags: u32, level: u32, linear: bool) -> vec4<f32> {
    var extent = size;
    var y_base = 0u;
    for (var i = 0u; i < level; i = i + 1u) {
        y_base += extent.y * ((extent.z + columns - 1u) / columns);
        extent = max(extent >> vec3<u32>(1u), vec3<u32>(1u));
    }
    let bounded = vec3<f32>(
        select(fract(coordinate.x), clamp(coordinate.x, 0.0, 1.0), (flags & 1u) != 0u),
        select(fract(coordinate.y), clamp(coordinate.y, 0.0, 1.0), (flags & 2u) != 0u),
        select(fract(coordinate.z), clamp(coordinate.z, 0.0, 1.0), (flags & 4u) != 0u));
    if !linear {
        let p = vec3<i32>(floor(bounded * vec3<f32>(extent)));
        return coin_volume_fetch(t, p.x, p.y, p.z, extent, columns, y_base, flags);
    }
    let f = bounded * vec3<f32>(extent) - vec3<f32>(0.5);
    let p = vec3<i32>(floor(f));
    let weight = fract(f);
    let a = coin_portable_lerp(
        coin_volume_fetch(t,p.x,p.y,p.z,extent,columns,y_base,flags),
        coin_volume_fetch(t,p.x+1,p.y,p.z,extent,columns,y_base,flags),weight.x);
    let b = coin_portable_lerp(
        coin_volume_fetch(t,p.x,p.y+1,p.z,extent,columns,y_base,flags),
        coin_volume_fetch(t,p.x+1,p.y+1,p.z,extent,columns,y_base,flags),weight.x);
    let c = coin_portable_lerp(
        coin_volume_fetch(t,p.x,p.y,p.z+1,extent,columns,y_base,flags),
        coin_volume_fetch(t,p.x+1,p.y,p.z+1,extent,columns,y_base,flags),weight.x);
    let d = coin_portable_lerp(
        coin_volume_fetch(t,p.x,p.y+1,p.z+1,extent,columns,y_base,flags),
        coin_volume_fetch(t,p.x+1,p.y+1,p.z+1,extent,columns,y_base,flags),weight.x);
    return coin_portable_lerp(coin_portable_lerp(a,b,weight.y),
                              coin_portable_lerp(c,d,weight.y),weight.z);
}
fn coin_volume_sample(t: texture_2d<f32>, coordinate: vec3<f32>, supplied_size: vec4<f32>) -> vec4<f32> {
    let size = vec3<u32>(u32(supplied_size.x), u32(supplied_size.y), u32(supplied_size.w) / 16u);
    let flags = u32(supplied_size.w) & 15u;
    let spatial_filter = u32(supplied_size.z) - 4u;
    var columns = 1u;
    while columns * columns < size.z { columns = columns * 2u; }
    let has_mips = (flags & 8u) != 0u;
    if !has_mips || spatial_filter < 2u {
        return coin_volume_level(t,coordinate,size,columns,flags,0u,spatial_filter != 0u);
    }
    let footprint = vec3<f32>(size);
    let dx = length(dpdxFine(coordinate) * footprint);
    let dy = length(dpdyFine(coordinate) * footprint);
    let maximum = u32(log2(f32(max(max(size.x,size.y),size.z))));
    let lod = clamp(log2(max(max(dx,dy),0.000001)),0.0,f32(maximum));
    if lod <= 0.0 { return coin_volume_level(t,coordinate,size,columns,flags,0u,true); }
    let lo = u32(floor(lod));
    let hi = min(maximum,lo+1u);
    let spatial_linear = spatial_filter == 3u;
    return coin_portable_lerp(
        coin_volume_level(t,coordinate,size,columns,flags,lo,spatial_linear),
        coin_volume_level(t,coordinate,size,columns,flags,hi,spatial_linear),fract(lod));
}
// Six face atlas, OpenGL 2.1 table 3.19, portable X/Y/Z tie order.
fn coin_cube_chart(direction: vec3<f32>) -> vec3<f32> {
    let d=direction/max(max(abs(direction.x),abs(direction.y)),abs(direction.z));
    let a=abs(d);var c=vec2<f32>(0.0);var m=0.0;var face=0.0;
    if a.x>=a.y && a.x>=a.z {m=a.x;if d.x>=0.0 {face=0.0;c=vec2<f32>(-d.z,-d.y);}else{face=1.0;c=vec2<f32>(d.z,-d.y);}}
    else if a.y>=a.z {m=a.y;if d.y>=0.0 {face=2.0;c=vec2<f32>(d.x,d.z);}else{face=3.0;c=vec2<f32>(d.x,-d.z);}}
    else {m=a.z;if d.z>=0.0 {face=4.0;c=vec2<f32>(d.x,-d.y);}else{face=5.0;c=vec2<f32>(-d.x,-d.y);}}
    return vec3<f32>(0.5*(c/m+vec2<f32>(1.0)),face);
}
fn coin_cube_fetch(t:texture_2d<f32>,p:vec2<i32>,n:u32,face:u32,base:u32)->vec4<f32>{
    let bounded=vec2<u32>(clamp(p,vec2<i32>(0),vec2<i32>(i32(n)-1)));
    let index=base+face*n*n+bounded.y*n+bounded.x;
    let width=textureDimensions(t).x;
    return textureLoad(t,vec2<i32>(i32(index%width),i32(index/width)),0);
}
fn coin_cube_level(t:texture_2d<f32>,chart:vec3<f32>,size:u32,level:u32,linear_texel:bool)->vec4<f32>{
    var n=size;var base=0u;
    for(var i=0u;i<level;i+=1u){base+=6u*n*n;n=max(1u,n/2u);}
    let face=u32(chart.z);
    if !linear_texel {return coin_cube_fetch(t,vec2<i32>(floor(chart.xy*f32(n))),n,face,base);}
    let f=chart.xy*f32(n)-vec2<f32>(0.5);let p=vec2<i32>(floor(f));let w=fract(f);
    let a=coin_cube_fetch(t,p,n,face,base);let b=coin_cube_fetch(t,p+vec2<i32>(1,0),n,face,base);
    let c=coin_cube_fetch(t,p+vec2<i32>(0,1),n,face,base);let d=coin_cube_fetch(t,p+vec2<i32>(1,1),n,face,base);
    return mix(mix(a,b,w.x),mix(c,d,w.x),w.y);
}
fn coin_cube_sample(t:texture_2d<f32>,direction:vec3<f32>,metadata:vec4<f32>,lod:f32)->vec4<f32>{
    let chart=coin_cube_chart(direction);let n=u32(metadata.x);let cube_filter=u32(metadata.z)-8u;
    if cube_filter<2u {return coin_cube_level(t,chart,n,0u,cube_filter==1u);}
    if lod<=0.0 {return coin_cube_level(t,chart,n,0u,true);}
    let maximum=select(0.0,log2(metadata.x),metadata.y>0.5);
    let bounded=clamp(lod,0.0,maximum);let lo=u32(floor(bounded));let hi=min(lo+1u,u32(maximum));
    return mix(coin_cube_level(t,chart,n,lo,cube_filter==3u),coin_cube_level(t,chart,n,hi,cube_filter==3u),fract(bounded));
}
fn coin_cube_lod(direction:vec3<f32>,size:f32)->f32{
    let d=direction/max(max(abs(direction.x),abs(direction.y)),abs(direction.z));
    let scale=max(max(abs(direction.x),abs(direction.y)),abs(direction.z));
    let dx=dpdxFine(direction)/scale;let dy=dpdyFine(direction)/scale;let face=coin_cube_chart(d).z;
    var c=vec2<f32>(0.0);var cx=c;var cy=c;var m=0.0;var mx=0.0;var my=0.0;
    if face<1.5 {let sign=select(-1.0,1.0,face<0.5);c=vec2<f32>(-sign*d.z,-d.y);cx=vec2<f32>(-sign*dx.z,-dx.y);cy=vec2<f32>(-sign*dy.z,-dy.y);m=sign*d.x;mx=sign*dx.x;my=sign*dy.x;}
    else if face<3.5 {let sign=select(-1.0,1.0,face<2.5);c=vec2<f32>(d.x,sign*d.z);cx=vec2<f32>(dx.x,sign*dx.z);cy=vec2<f32>(dy.x,sign*dy.z);m=sign*d.y;mx=sign*dx.y;my=sign*dy.y;}
    else {let sign=select(-1.0,1.0,face<4.5);c=vec2<f32>(sign*d.x,-d.y);cx=vec2<f32>(sign*dx.x,-dx.y);cy=vec2<f32>(sign*dy.x,-dy.y);m=sign*d.z;mx=sign*dx.z;my=sign*dy.z;}
    let sx=0.5*(cx*m-c*mx)/(m*m)*size;let sy=0.5*(cy*m-c*my)/(m*m)*size;
    return log2(max(max(length(sx),length(sy)),0.000001));
}
fn coin_line_sample(t:texture_2d<f32>,s:sampler,value:vec4<f32>,params:vec4<f32>,metadata:vec4<f32>)->vec4<f32>{
    if metadata.z>=8.0{return coin_cube_sample(t,value.xyz,metadata,coin_cube_lod(value.xyz,metadata.x));}
    if metadata.z>=4.0 {
        let coordinate=select(value.xyz/value.w,value.xyz,params.w>0.5);
        return coin_volume_sample(t,coordinate,metadata);
    }
    let projected=projected_uv(value,params.w);
    let uv=select(projected,vec2<f32>(projected.x,1.0-projected.y),params.z>0.5);
    return textureSample(t,s,uv);
}
fn projected_uv(value: vec4<f32>, projection: f32) -> vec2<f32> {
    if (projection > 0.5 || value.w == 1.0) { return value.xy; }
    return value.xy / value.w;
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
    if(u.composition_meta.y>0.5){primary.a=1.0;}
    var base_color = primary;

    if (u.tex_params.x > 0.5) {
        let tex_col = coin_line_sample(t_diffuse,s_diffuse,input.texcoord,u.tex_params,u.sampling_texture_sizes[0]);
        base_color = texture_layer(primary, base_color, tex_col, u.tex_params, u.texture_blend_color, 0u);
    }
    if (u.extra_tex_params[0].x > 0.5) {
        let params = u.extra_tex_params[0];
        let tex = coin_line_sample(t_texture1,s_texture1,input.uv1,params,u.sampling_texture_sizes[1]);
        base_color = texture_layer(primary, base_color, tex, params, u.extra_texture_blends[0], 1u);
    }
    if (u.extra_tex_params[1].x > 0.5) {
        let params = u.extra_tex_params[1];
        let tex = coin_line_sample(t_texture2,s_texture2,input.uv2,params,u.sampling_texture_sizes[2]);
        base_color = texture_layer(primary, base_color, tex, params, u.extra_texture_blends[1], 2u);
    }
    if (u.extra_tex_params[2].x > 0.5) {
        let params = u.extra_tex_params[2];
        let tex = coin_line_sample(t_texture3,s_texture3,input.uv3,params,u.sampling_texture_sizes[3]);
        base_color = texture_layer(primary, base_color, tex, params, u.extra_texture_blends[2], 3u);
    }
    if (u.extra_tex_params[3].x > 0.5) {
        let params = u.extra_tex_params[3];
        let tex = coin_line_sample(t_texture4,s_texture4,input.uv4,params,u.sampling_texture_sizes[4]);
        base_color = texture_layer(primary, base_color, tex, params, u.extra_texture_blends[3], 4u);
    }
    if (u.extra_tex_params[4].x > 0.5) {
        let params = u.extra_tex_params[4];
        let tex = coin_line_sample(t_texture5,s_texture5,input.uv5,params,u.sampling_texture_sizes[5]);
        base_color = texture_layer(primary, base_color, tex, params, u.extra_texture_blends[4], 5u);
    }
    if (u.extra_tex_params[5].x > 0.5) {
        let params = u.extra_tex_params[5];
        let tex = coin_line_sample(t_texture6,s_texture6,input.uv6,params,u.sampling_texture_sizes[6]);
        base_color = texture_layer(primary, base_color, tex, params, u.extra_texture_blends[5], 6u);
    }
    if (u.extra_tex_params[6].x > 0.5) {
        let params = u.extra_tex_params[6];
        let tex = coin_line_sample(t_texture7,s_texture7,input.uv7,params,u.sampling_texture_sizes[7]);
        base_color = texture_layer(primary, base_color, tex, params, u.extra_texture_blends[6], 7u);
    }
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
