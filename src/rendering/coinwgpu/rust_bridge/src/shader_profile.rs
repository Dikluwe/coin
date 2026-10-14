// Native compilation removes portable functions and its uniform appendix.
// Both vertex and fragment stages therefore use the original binding prefix.
pub(super) fn sampling_profile(source: &str, portable: bool) -> String {
    // include_str! preserves Git checkout line endings on Windows.
    // Canonicalize before removing the portable-only uniform appendix.
    let canonical = source.replace("\r\n", "\n");
    let source = canonical.as_str();
    if portable || !source.contains("fn coin_portable_sample(") { return source.to_owned(); }
    let mut source = source.to_owned();
    let start = source.find("fn coin_portable_lerp(").unwrap();
    let end = source[start..].find("fn projected_uv(").unwrap() + start;
    source.replace_range(start..end, "");
    source = source.replace("    sampling_texture_sizes: array<vec4<f32>, 8>,\n", "");
    source = source.replace(", u.tex_params.x, u.sampling_texture_sizes[0]", "");
    for unit in 1..8 { source = source.replace(&format!(", params.x, u.sampling_texture_sizes[{unit}]"), ""); }
    source.replace("coin_portable_sample(", "textureSample(")
}

// Keep the transported uniform ABI intact. Specialization only removes work
// whose absence is already known from the validated draw/active shadow profile.
pub(super) fn texture_specialization(source: &str) -> String {
    format!("override coin_textures_enabled: bool = true;\n{}", source
        .replace("if (u.tex_params.x > 0.5)",
            "if (coin_textures_enabled && u.tex_params.x > 0.5)")
        .replace("if (u.extra_tex_params[",
            "if (coin_textures_enabled && u.extra_tex_params["))
}

// wgpu permits non-filterable float bindings for depth views. Naga 24's GLSL
// writer supports texelFetch through this type, but rejects depth textureLoad.
pub(super) fn depth_load_profile(source: &str, gl: bool) -> String {
    if !gl { return source.to_owned(); }
    let mut source = source.to_owned();
    for name in ["previous_depth", "opaque_depth", "layer_depth"] {
        for space in ["", " "] {
            source = source.replace(&format!("var {name}:{space}texture_depth_2d;"),
                &format!("var {name}: texture_2d<f32>;"));
        }
        source = source.replace(&format!("textureLoad({name},xy,0)"),
            &format!("textureLoad({name},xy,0).r"));
    }
    source
}

pub(super) fn without_shadows(source: &str) -> String {
    let mut source = source.to_owned();
    let output_start = source.find("struct VertexOutput {").unwrap();
    let output_end = output_start + source[output_start..].find("\n};").unwrap() + 3;
    let output = source[output_start..output_end].lines()
        .filter(|line| !line.contains("shadow_")).collect::<Vec<_>>().join("\n");
    source.replace_range(output_start..output_end, &output);
    let start = source.find("        if ((u.shadow_params.x").unwrap();
    let end = start + source[start..].find("{ continue; }").unwrap() + "{ continue; }".len();
    source.replace_range(start..end, "");
    let start = source.find("    output.shadow_coord =").unwrap();
    let end = start + source[start..].find("    if (u.tex_params.x").unwrap();
    source.replace_range(start..end, "");
    let start = source.find("    if (u.shadow_params.x > 0.5 && u.params.w > 0.5)").unwrap();
    let end = start + source[start..].find("    // COIN_EXTRA_SHADOW_SURFACE").unwrap();
    source.replace_range(start..end, "");
    source
}

#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/shader_profile_tests.rs"]
pub(super) mod tests;
