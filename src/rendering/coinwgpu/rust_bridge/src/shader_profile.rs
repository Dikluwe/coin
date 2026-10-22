// Spatial atlas sampling needs the same per-unit metadata in both policies.
// Native 2D keeps its hardware sampling path; volume uses explicit 3D loads.
pub(super) fn sampling_profile(source: &str, portable: bool) -> String {
    let canonical = canonical_depth_profile(&source.replace("\r\n", "\n"));
    if portable { canonical }
    else { canonical.replace("if supplied_size.z != 2.0", "if true") }
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
        for space in ["", " "] {
            source = source.replace(&format!("var {name}:{space}texture_depth_multisampled_2d;"),
                &format!("var {name}: texture_multisampled_2d<f32>;"));
        }
        for sample in ["0", "i32(sample_index)"] {
            source = source.replace(&format!("textureLoad({name},xy,{sample})"),
                &format!("textureLoad({name},xy,{sample}).r"));
        }
    }
    source
}

// Only the private peeling layout changes. SampleIndex requests sample-frequency
// depth/interpolation; previous and opaque comparisons keep each sample distinct.
pub(super) fn peeling_msaa_profile(source: &str, enabled: bool) -> String {
    if !enabled { return source.to_owned(); }
    let mut source = source.to_owned();
    for name in ["previous_depth", "opaque_depth"] {
        for space in ["", " "] {
            source = source.replace(&format!("var {name}:{space}texture_depth_2d;"),
                &format!("var {name}: texture_depth_multisampled_2d;"));
        }
        source = source.replace(&format!("textureLoad({name},xy,0)"),
            &format!("textureLoad({name},xy,i32(sample_index))"));
    }
    source = sample_depth_profile(&source, true);
    source.replace("fn fs_peel(input:VertexOutput)",
        "fn fs_peel(input:VertexOutput,@builtin(sample_index) sample_index:u32)")
}

// Independent of multisampled resource bindings: custom opaque depth also
// needs sample-frequency evaluation outside transparency frames.
pub(super) fn sample_depth_profile(source: &str, enabled: bool) -> String {
    if !enabled { return source.to_owned(); }
    let mut source = source.to_owned();
    // SampleIndex alone does not make the fragment position's Z a portable
    // sample-depth source on all native backends. Carry the original clip
    // pair through an explicitly sample-interpolated varying instead. Keep
    // the existing surface/color/lighting interpolation qualifiers unchanged.
    // The extended shadow interface has one live component in shadow_uv5
    // (unit seven Q), while model-position W is unused. Preserve Q's ordinary
    // perspective interpolation there and reuse location 19 for sample clip Z/W.
    // This keeps the established 80-component admission for the combined profile.
    if source.contains("@location(19) shadow_uv5:vec4<f32>,") &&
        !source.contains("coin_bump_coord:") {
        source = source.replace("@location(19) shadow_uv5:vec4<f32>,", "")
            .replace("output.shadow_uv5=vec4<f32>(shadow_uv7.w,0.0,0.0,0.0);",
                "output.position_model.w=shadow_uv7.w;")
            .replace("input.shadow_uv5.x", "input.position_model.w")
            // sampling_profile has already appended canonical clip-W storage.
            // This profile replaces coin_fragment_depth with coin_sample_clip,
            // so that old storage is redundant and would overwrite unit seven Q.
            .replace("output.position_model.w = output.clip_position.w;", "");
    }
    if source.contains("struct VertexOutput {") && !source.contains("coin_sample_clip:") {
        let start = source.find("struct VertexOutput {").unwrap();
        let end = start + source[start..].find("\n};").unwrap();
        let output = &source[start..end];
        let slot = (0..16).rev().find(|slot| !output.contains(&format!("@location({slot})")))
            .or_else(|| (16..32).find(|slot| !output.contains(&format!("@location({slot})"))))
            .expect("Private sample-depth profile requires a free varying location");
        source.insert_str(end, &format!("\n    @location({slot}) @interpolate(perspective, sample) coin_sample_clip: vec2<f32>,"));
        let vertex = source.find("fn vs_main(").unwrap();
        let tail = vertex + source[vertex..].find("return output;").unwrap();
        // Canonical depth may neutralize clip Z just before return. Capture
        // before that operation; native mode retains its original clip Z/W.
        let capture = source[vertex..tail].find("output.position_view.w = output.clip_position.z;")
            .map_or(tail, |offset| vertex + offset);
        source.insert_str(capture, "output.coin_sample_clip = output.clip_position.zw;\n    ");
        if let Some(start) = source.find("fn coin_fragment_depth(input: VertexOutput) -> f32 {") {
            let end = start + source[start..].find("\n}\n").unwrap() + 3;
            source.replace_range(start..end,
                "fn coin_fragment_depth(input: VertexOutput) -> f32 {\n    return input.coin_sample_clip.x / input.coin_sample_clip.y;\n}\n");
        } else {
            // Stroke profiles have no canonical spare-lane depth helper.
            source = source.replace("input.clip_position.z", "coin_fragment_depth(input)");
            let fragment = source.find("@fragment").unwrap();
            source.insert_str(fragment,
                "fn coin_fragment_depth(input: VertexOutput) -> f32 {\n    return input.coin_sample_clip.x / input.coin_sample_clip.y;\n}\n");
        }
    }
    // Both the opaque writer and selector must use shader depth evaluated
    // at the same sample. Hardware interpolation/quantization of opaque depth
    // need not round identically to a later fragment-depth expression.
    source = source.replace("fn fs_depth_bias(input: VertexOutput) -> DepthBiasOutput {",
        "fn fs_depth_bias(input: VertexOutput, @builtin(sample_index) sample_index: u32) -> DepthBiasOutput {\n    // Statically consume SampleIndex: merely declaring an unused builtin may\n    // lose sample-frequency execution during backend dead-input elimination.\n    // This private profile is admitted only for N=2/4, so no valid sample is discarded.\n    if (sample_index >= 4u) { discard; }");
    source = source.replace("fn fs_weighted(input: VertexOutput) -> WeightedOutput {",
        "fn fs_weighted(input: VertexOutput, @builtin(sample_index) sample_index: u32) -> WeightedOutput {\n    if (sample_index >= 4u) { discard; }");
    source
}

pub(super) fn without_shadows(source: &str) -> String {
    let mut source = source.to_owned();
    let output_start = source.find("struct VertexOutput {").unwrap();
    let output_end = output_start + source[output_start..].find("\n};").unwrap() + 3;
    // Canonical depth stores clip W in this spare fourth lane even when the
    // scene has no shadow receivers. Retain just that varying for the standard
    // shader; the shadow sampling/lighting paths below are still removed.
    let output = source[output_start..output_end].lines()
        .filter(|line| !line.contains("shadow_") || line.contains("shadow_light_view_second:"))
        .collect::<Vec<_>>().join("\n");
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

// Keep Z clipping out of the hardware clipper in the canonical profile. Carry
// the original clip pair in spare lanes, retaining the existing 16 locations.
fn canonical_depth_profile(source: &str) -> String {
    if !source.contains("struct VertexOutput {") || !source.contains("raster_viewport:") || source.contains("fn coin_fragment_depth(") {
        return source.to_owned();
    }
    // Line/point uniforms share the standard tail, but their varyings have
    // no spare shadow lane. Presence of a uniform alone cannot select this
    // standard/shadow surface transformation.
    let clip_w_field = if source.contains("position_model:vec4<f32>") {
        "position_model"
    } else if source.contains("shadow_light_view_second:") {
        "shadow_light_view_second"
    } else {
        return source.to_owned();
    };
    let mut source = source.replace("@location(0) position_view: vec3<f32>", "@location(0) position_view: vec4<f32>")
        .replace("@location(0) position_view:vec3<f32>", "@location(0) position_view:vec4<f32>");
    // The normal's fourth lane carries raster fog depth. Canonical depth
    // therefore uses a spare fourth lane of an existing shadow varying for
    // clip W, while position_view.w carries clip Z.
    for variable in ["input", "output"] {
        source = source.replace(&format!("{variable}.position_view"),
                                &format!("{variable}.position_view.xyz"));
    }
    source = source.replace("output.position_view.xyz.", "output.position_view.")
        .replace("input.position_view.xyz.z", "input.position_view.z")
        .replace("input.position_view.xyz.w", "input.position_view.w");
    let lhs = "output.position_view.xyz =";
    while let Some(start) = source.find(lhs) {
        let end = start + source[start..].find(';').unwrap();
        let expression = source[start+lhs.len()..end].trim().to_owned();
        source.replace_range(start..=end,
            &format!("output.position_view = vec4<f32>({expression}, 0.0);"));
    }
    source = source.replace("input.clip_position.z", "coin_fragment_depth(input)");
    let vertex = source.find("fn vs_main(").unwrap();
    let end = vertex + source[vertex..].find("return output;").unwrap();
    source.insert_str(end, &format!("output.position_view.w = output.clip_position.z;\n    output.{clip_w_field}.w = output.clip_position.w;\n    if (u.raster_viewport.z > 0.0) {{ output.clip_position.z = 0.0; }}\n    "));
    let fragment = source.find("fn fragment_color(").unwrap();
    source.insert_str(fragment, &format!("fn coin_fragment_depth(input: VertexOutput) -> f32 {{\n    if (u.raster_viewport.z > 0.0) {{ return input.position_view.w / input.{clip_w_field}.w; }}\n    return input.clip_position.z;\n}}\n"));
    let signature = "fn fragment_color(input: VertexOutput) -> vec4<f32> {";
    source = source.replace(signature, &format!("{signature}\n    if (u.raster_viewport.z > 0.0) {{ let z = coin_fragment_depth(input); if (z < 0.0 || z > 1.0) {{ discard; }} }}"));
    source
}
