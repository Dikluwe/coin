// Image PRIMARY and up to eight inherited constant raster samples.
// This pass has no shadows, alpha test, depth, material lighting or fog.
struct RasterUniforms {
    coordinates: array<vec4<f32>,8>,
    params: array<vec4<f32>,8>,
    blends: array<vec4<f32>,8>,
    sizes: array<vec4<f32>,8>,
    texture_combines: array<array<vec4<f32>,4>,8>,
};
@group(0) @binding(0) var<uniform> u: RasterUniforms;
@group(0) @binding(1) var payload: texture_2d<f32>;
@group(0) @binding(2) var output_image: texture_storage_2d<rgba32float,write>;
@group(0) @binding(3) var image0: texture_2d<f32>;
@group(0) @binding(4) var sampler0: sampler;
@group(0) @binding(5) var image1: texture_2d<f32>;
@group(0) @binding(6) var sampler1: sampler;
@group(0) @binding(7) var image2: texture_2d<f32>;
@group(0) @binding(8) var sampler2: sampler;
@group(0) @binding(9) var image3: texture_2d<f32>;
@group(0) @binding(10) var sampler3: sampler;
@group(0) @binding(11) var image4: texture_2d<f32>;
@group(0) @binding(12) var sampler4: sampler;
@group(0) @binding(13) var image5: texture_2d<f32>;
@group(0) @binding(14) var sampler5: sampler;
@group(0) @binding(15) var image6: texture_2d<f32>;
@group(0) @binding(16) var sampler6: sampler;
@group(0) @binding(17) var image7: texture_2d<f32>;
@group(0) @binding(18) var sampler7: sampler;
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

fn coin_portable_lerp(a:vec4<f32>,b:vec4<f32>,weight:f32)->vec4<f32>{return a+(b-a)*weight;}
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

fn raster_sample(t:texture_2d<f32>,s:sampler,unit:u32)->vec4<f32>{
    let coordinate=u.coordinates[unit];
    let params=u.params[unit];
    let supplied=u.sizes[unit];
    if supplied.z>=8.0 {return coin_cube_sample(t,coordinate.xyz,supplied,0.0);}
    let projected=select(coordinate.xyz/coordinate.w,coordinate.xyz,params.w>0.5);
    if supplied.z>=4.0 {
        let size=vec3<u32>(u32(supplied.x),u32(supplied.y),u32(supplied.w)/16u);
        var columns=1u;
        while columns*columns<size.z {columns=columns*2u;}
        return coin_volume_level(t,projected,size,columns,u32(supplied.w)&7u,0u,supplied.z!=4.0);
    }
    let uv=select(projected.xy,vec2<f32>(projected.x,1.0-projected.y),params.z>0.5);
    return textureSampleLevel(t,s,uv,0.0);
}
@compute @workgroup_size(8,8)
fn main(@builtin(global_invocation_id) id:vec3<u32>){
    let extent=textureDimensions(payload);
    if any(id.xy>=extent){return;}
    let primary=textureLoad(payload,vec2<i32>(id.xy),0);
    var color=primary;
    if u.params[0].x>0.5 {color=texture_layer(primary,color,raster_sample(image0,sampler0,0u),u.params[0],u.blends[0],0u);}
    if u.params[1].x>0.5 {color=texture_layer(primary,color,raster_sample(image1,sampler1,1u),u.params[1],u.blends[1],1u);}
    if u.params[2].x>0.5 {color=texture_layer(primary,color,raster_sample(image2,sampler2,2u),u.params[2],u.blends[2],2u);}
    if u.params[3].x>0.5 {color=texture_layer(primary,color,raster_sample(image3,sampler3,3u),u.params[3],u.blends[3],3u);}
    if u.params[4].x>0.5 {color=texture_layer(primary,color,raster_sample(image4,sampler4,4u),u.params[4],u.blends[4],4u);}
    if u.params[5].x>0.5 {color=texture_layer(primary,color,raster_sample(image5,sampler5,5u),u.params[5],u.blends[5],5u);}
    if u.params[6].x>0.5 {color=texture_layer(primary,color,raster_sample(image6,sampler6,6u),u.params[6],u.blends[6],6u);}
    if u.params[7].x>0.5 {color=texture_layer(primary,color,raster_sample(image7,sampler7,7u),u.params[7],u.blends[7],7u);}
    textureStore(output_image,vec2<i32>(id.xy),color);
}
