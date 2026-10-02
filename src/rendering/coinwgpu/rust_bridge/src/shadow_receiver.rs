// The bounded shadow profile has no extra scene texture units. Its pipeline
// reclaims four of those bindings to keep eight maps within WebGPU's limits.
pub(crate) fn eight_map_source() -> String {
    let source = include_str!("../../shaders/coin_standard.wgsl");
    let (head, rest) = source
        .split_once("// COIN_UPPER_TEXTURE_UNITS_BEGIN")
        .unwrap();
    let (_, tail) = rest.split_once("// COIN_UPPER_TEXTURE_UNITS_END").unwrap();
    let mut source = format!("{head}{tail}");
    let mut declarations = String::from(
        r#"
struct ExtraShadowReceiver {
    view_to_clip: mat4x4<f32>,
    view_to_light: mat4x4<f32>,
    params: vec4<f32>,
    info: vec4<f32>,
    falloff: vec4<f32>,
};
@group(1) @binding(0) var<uniform> extra_shadows: array<ExtraShadowReceiver, 4>;
fn coinExtraShadowOwnsLight(index: u32) -> bool {
    for (var slot = 0u; slot < 4u; slot += 1u) {
        if (extra_shadows[slot].params.x > 0.5 &&
            index == u32(extra_shadows[slot].info.y)) { return true; }
    }
    return false;
}
"#,
    );
    let mut surface = String::new();
    for slot in 0..4 {
        declarations += &format!(
            "@group(1) @binding({}) var t_extra_shadow{}: texture_2d<f32>;\n",
            slot + 1,
            slot
        );
        surface += &format!(
            r#"
    if (extra_shadows[{slot}].params.x > 0.5 && u.params.w > 0.5) {{
        let receiver = extra_shadows[{slot}];
        let light = u.lights[u32(receiver.info.y)];
        var contribution=input.shadow_vertex{vertex_slot};
        var specularPart=vec3<f32>(0.0);
        if (receiver.falloff.y<0.5) {{ contribution=light_contribution(light,
            materials[input.material_slot], input.position_view, input.normal_view);
            specularPart=shadow_specular(light,materials[input.material_slot],input.position_view,input.normal_view); }}
        let coord = receiver.view_to_clip * vec4<f32>(input.position_view, 1.0);
        let light_view = (receiver.view_to_light * vec4<f32>(input.position_view, 1.0)).xyz;
        primary = vec4<f32>(clamp(primary.rgb + (contribution-specularPart) *
            vsm_shadow_factor(coord, light_view, input.position_view,
                receiver.params, receiver.info, receiver.falloff, t_extra_shadow{slot}),
            vec3<f32>(0.0), vec3<f32>(1.0)), primary.a);
        specularColor+=specularPart*vsm_shadow_factor(coord,light_view,input.position_view,
            receiver.params,receiver.info,receiver.falloff,t_extra_shadow{slot});
    }}
"#,
            vertex_slot = slot + 4
        );
    }
    source = source
        .replace("// COIN_EXTRA_SHADOW_DECLARATIONS", &declarations)
        .replace(
            "/* COIN_EXTRA_SHADOW_OWNS_LIGHT */",
            "|| coinExtraShadowOwnsLight(i)",
        )
        .replace("// COIN_EXTRA_SHADOW_SURFACE", &surface);
    quality_source(source, true)
}

pub(crate) fn four_map_source() -> String {
    quality_source(
        include_str!("../../shaders/coin_standard.wgsl").to_owned(),
        false,
    )
}

// The shadow profile forbids extra scene units. Reclaim their interpolators
// for independent light contributions; never interpolate shadow visibility.
fn quality_source(mut source: String, eight: bool) -> String {
    let start = source.find("struct VertexOutput {").unwrap();
    let end = source[start..].find("\n};").unwrap() + start + 3;
    let mut output = String::from("struct VertexOutput {\n@builtin(position) clip_position:vec4<f32>,\n@location(0) position_view:vec3<f32>,\n@location(1) normal_view:vec3<f32>,\n@location(2) diffuse_color:vec4<f32>,\n@location(3) @interpolate(flat) material_slot:u32,\n@location(4) texcoord:vec2<f32>,\n@location(13) position_model:vec3<f32>,\n");
    for slot in 0..8 {
        output += &format!("@location({}) shadow_vertex{}:vec3<f32>,\n", slot + 5, slot);
    }
    output += "};";
    source.replace_range(start..end, &output);
    source = source
        .lines()
        .filter(|line| !line.trim_start().starts_with("output.uv"))
        .collect::<Vec<_>>()
        .join("\n");
    let start = source
        .find("    if (u.extra_tex_params[0].x > 0.5)")
        .unwrap();
    let end = source[start..].find("    return apply_fog").unwrap() + start;
    source.replace_range(start..end, "");
    let start = source.find("    output.shadow_coord =").unwrap();
    let end = source[start..].find("    if (u.tex_params.x").unwrap() + start;
    let mut vertex = String::from("    output.position_model = input.position;\n");
    let suffixes = ["", "_second", "_third", "_fourth"];
    for slot in 0..8 {
        vertex += &format!("output.shadow_vertex{slot}=vec3<f32>(0.0);\n");
        let (params, info, falloff) = if slot < 4 {
            (
                format!("u.shadow_params{}", suffixes[slot]),
                format!("u.shadow_meta{}", suffixes[slot]),
                format!("u.shadow_falloff{}", suffixes[slot]),
            )
        } else if eight {
            (
                format!("extra_shadows[{}].params", slot - 4),
                format!("extra_shadows[{}].info", slot - 4),
                format!("extra_shadows[{}].falloff", slot - 4),
            )
        } else {
            continue;
        };
        vertex += &format!("if ({params}.x>0.5 && {falloff}.y>0.5) {{ output.shadow_vertex{slot}=light_contribution(u.lights[u32({info}.y)],materials[input.material_slot],output.position_view,output.normal_view); }}\n");
    }
    source.replace_range(start..end, &vertex);
    // Positional Coin GLSL lights use the local eye; directional half vectors
    // retain the fixed-function convention.
    source=source.replace("let v = vec3<f32>(0.0, 0.0, 1.0);", "var v=vec3<f32>(0.0,0.0,1.0); if (u.light_meta.w>0.5 && light.position_type.w>0.5) { v=normalize(-position_view); }");
    source = source.replace(
        "if (f32(i) >= u.light_meta.x) { break; }",
        "if (f32(i) >= u.light_meta.x || u.light_meta.z>0.5) { break; }",
    );
    for slot in 0..4 {
        let suffix = suffixes[slot];
        let original=format!("let light = u.lights[u32(u.shadow_meta{suffix}.y)];\n        let contribution = light_contribution(light,\n            materials[input.material_slot], input.position_view, input.normal_view);");
        let replacement=format!("let light = u.lights[u32(u.shadow_meta{suffix}.y)];\n        var contribution=input.shadow_vertex{slot};\n        var specularPart=vec3<f32>(0.0);\n        if (u.shadow_falloff{suffix}.y<0.5) {{ contribution=light_contribution(light,materials[input.material_slot],input.position_view,input.normal_view); specularPart=shadow_specular(light,materials[input.material_slot],input.position_view,input.normal_view); }}");
        assert!(source.contains(&original));
        source = source.replace(&original, &replacement);
        let start = source
            .find(&format!(
                "    if (u.shadow_params{suffix}.x > 0.5 && u.params.w > 0.5)"
            ))
            .unwrap();
        let end = start + source[start..].find("\n    }").unwrap();
        let (coord, light_view) = if slot < 2 {
            (
                format!("input.shadow_coord{suffix}"),
                format!("input.shadow_light_view{suffix}"),
            )
        } else {
            ("coord".to_owned(), "light_view".to_owned())
        };
        source.insert_str(end,&format!("\n        specularColor+=specularPart*vsm_shadow_factor({coord},{light_view},input.position_view,u.shadow_params{suffix},u.shadow_meta{suffix},u.shadow_falloff{suffix},t_shadow{suffix});"));
    }
    source = source.replace(
        "primary.rgb + contribution *",
        "primary.rgb + (contribution-specularPart) *",
    );
    source = source
        .replace(
            "input.shadow_coord_second",
            "(u.shadow_model_view_projection_second * vec4<f32>(input.position_model,1.0))",
        )
        .replace(
            "input.shadow_light_view_second",
            "(u.shadow_model_view_second * vec4<f32>(input.position_model,1.0)).xyz",
        )
        .replace(
            "input.shadow_coord",
            "(u.shadow_model_view_projection * vec4<f32>(input.position_model,1.0))",
        )
        .replace(
            "input.shadow_light_view",
            "(u.shadow_model_view * vec4<f32>(input.position_model,1.0)).xyz",
        );
    let mut ordinary=String::from("if(u.light_meta.z>0.5 && u.params.w>0.5) { for(var i=0u;i<8u;i+=1u) { if(f32(i)>=u.light_meta.x){break;} if(");
    for slot in 0..4 {
        if slot > 0 {
            ordinary += " || ";
        }
        ordinary += &format!(
            "(u.shadow_params{}.x>0.5 && i==u32(u.shadow_meta{}.y))",
            suffixes[slot], suffixes[slot]
        );
    }
    if eight {
        ordinary += " || coinExtraShadowOwnsLight(i)";
    }
    ordinary+=") {continue;} let spec=shadow_specular(u.lights[i],materials[input.material_slot],input.position_view,input.normal_view); primary=vec4<f32>(primary.rgb+light_contribution(u.lights[i],materials[input.material_slot],input.position_view,input.normal_view)-spec,primary.a); specularColor+=spec; } }\n";
    source = source.replace(
        "var primary=input.diffuse_color;",
        &format!("var primary=input.diffuse_color;\nvar specularColor=vec3<f32>(0.0);\n{ordinary}"),
    );
    source=source.replace("return apply_fog(base_color, -input.position_view.z);", "return apply_fog(vec4<f32>(clamp(base_color.rgb+specularColor,vec3<f32>(0.0),vec3<f32>(1.0)),base_color.a), -input.position_view.z);");
    source += "\nfn shadow_specular(light:GpuLight,material:GpuMaterial,position:vec3<f32>,normal:vec3<f32>)->vec3<f32> { var spec=material; spec.diffuse=vec4<f32>(0.0); return light_contribution(light,spec,position,normal); }\n";
    source
}
