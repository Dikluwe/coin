struct Camera { vp: mat4x4<f32>, view: mat4x4<f32>, light0: vec4<f32>, light1: vec4<f32> };
@group(0) @binding(0) var<uniform> camera: Camera;
struct Out { @builtin(position) position: vec4<f32>, @location(0) normal: vec3<f32>, @location(1) color: vec4<f32> };
@vertex fn vs_main(@location(0) position: vec3<f32>, @location(1) normal: vec3<f32>,
    @location(2) m0: vec4<f32>, @location(3) m1: vec4<f32>, @location(4) m2: vec4<f32>,
    @location(5) m3: vec4<f32>, @location(6) color: vec4<f32>) -> Out {
    let model = mat4x4<f32>(m0,m1,m2,m3);
    // Cofactors give inverse-transpose normals up to the positive determinant;
    // normalization removes that common factor for nonuniform positive scales.
    let normalMatrix = mat3x3<f32>(cross(m1.xyz,m2.xyz),cross(m2.xyz,m0.xyz),cross(m0.xyz,m1.xyz));
    var out: Out;
    out.position = camera.vp * model * vec4<f32>(position,1.0);
    out.position.z = 0.5 * (out.position.z + out.position.w);
    out.normal = (camera.view * vec4<f32>(normalize(normalMatrix * normal),0.0)).xyz;
    out.color = color;
    return out;
}
@fragment fn fs_main(in: Out) -> @location(0) vec4<f32> {
    let n = normalize(in.normal);
    let light = 0.2 + camera.light0.w * max(dot(n,normalize(-camera.light0.xyz)),0.0)
        + camera.light1.w * max(dot(n,normalize(-camera.light1.xyz)),0.0);
    return vec4<f32>(in.color.rgb * light,in.color.a);
}
