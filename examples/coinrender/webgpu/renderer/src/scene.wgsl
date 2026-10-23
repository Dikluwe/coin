struct VertexOut {
    @builtin(position) position: vec4<f32>,
    @location(0) normal: vec3<f32>,
    @location(1) color: vec4<f32>,
};
@vertex fn vs_main(@location(0) position: vec4<f32>, @location(1) normal: vec3<f32>,
                  @location(2) color: vec4<f32>) -> VertexOut {
    var out: VertexOut;
    out.position = position;
    out.normal = normal;
    out.color = color;
    return out;
}
@fragment fn fs_main(in: VertexOut) -> @location(0) vec4<f32> {
    let light = 0.22 + 0.78 * max(dot(normalize(in.normal), normalize(vec3<f32>(0.4, 0.65, 1.0))), 0.0);
    return vec4<f32>(in.color.rgb * light, in.color.a);
}
