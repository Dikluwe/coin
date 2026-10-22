// The bounded shadow profile has no extra scene texture units. Its pipeline
// reclaims four of those bindings to keep eight maps within WebGPU's limits.
pub(crate) fn eight_map_source() -> String { eight_map_profile(false) }
pub(crate) fn eight_map_multitexture_source() -> String { eight_map_profile(true) }
fn eight_map_profile(multitexture: bool) -> String {
    let source = include_str!("../../shaders/coin_standard.wgsl");
    let (head, rest) = source
        .split_once("// COIN_UPPER_TEXTURE_UNITS_BEGIN")
        .unwrap();
    let (_, tail) = rest.split_once("// COIN_UPPER_TEXTURE_UNITS_END").unwrap();
    let mut source = if multitexture { source.to_owned() } else { format!("{head}{tail}") };
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
            materials[input.material_slot], input.position_view, input.normal_view.xyz);
            specularPart=shadow_specular(light,materials[input.material_slot],input.position_view,input.normal_view.xyz); }}
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
    let source = quality_source(source, true, multitexture);
    if multitexture { array_moments_source(source) } else { source }
}

pub(crate) fn four_map_source() -> String {
    quality_source(
        include_str!("../../shaders/coin_standard.wgsl").to_owned(),
        false, false,
    )
}

// The legacy shadow profile reclaims extra scene interpolators for independent
// light contributions. The extended profile adds separate packed STQ varyings;
// both profiles preserve Gouraud contributions and never interpolate visibility.
pub(crate) fn four_map_multitexture_source() -> String { array_moments_source(quality_source(include_str!("../../shaders/coin_standard.wgsl").to_owned(), false, true)) }

fn quality_source(mut source: String, eight: bool, multitexture: bool) -> String {
    let start = source.find("struct VertexOutput {").unwrap();
    let end = source[start..].find("\n};").unwrap() + start + 3;
    let mut output = String::from("struct VertexOutput {\n@builtin(position) clip_position:vec4<f32>,\n@location(0) position_view:vec3<f32>,\n@location(1) normal_view:vec4<f32>,\n@location(2) diffuse_color:vec4<f32>,\n@location(3) @interpolate(flat) material_slot:u32,\n@location(4) texcoord:vec4<f32>,\n@location(13) position_model:vec4<f32>,\n");
    for slot in 0..8 {
        let kind=if multitexture {"vec4"} else {"vec3"};
        output += &format!("@location({}) shadow_vertex{}:{kind}<f32>,\n", slot + 5, slot);
    }
    if multitexture { for slot in 0..6 { output += &format!("@location({}) shadow_uv{}:vec4<f32>,\n", slot + 14, slot); } }
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
    // Remove only unsupported extra scene texture units. Keep the standard
    // alpha test after unit 0 so receivers compare their final surface alpha
    // before writing color or depth.
    let end = source[start..].find("    if (!alpha_test_accepts(base_color.a))").unwrap() + start;
    if !multitexture { source.replace_range(start..end, ""); }
    let start = source.find("    output.shadow_coord =").unwrap();
    let end = source[start..].find("    if (u.tex_params.x").unwrap() + start;
    let mut vertex = String::from("    output.position_model = vec4<f32>(input.position, 0.0);\n");
    let suffixes = ["", "_second", "_third", "_fourth"];
    for slot in 0..8 {
        vertex += &format!("output.shadow_vertex{slot}={}<f32>(0.0);\n",if multitexture {"vec4"} else {"vec3"});
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
        let contribution=format!("light_contribution(u.lights[u32({info}.y)],materials[input.material_slot],output.position_view,output.normal_view.xyz)");
        let contribution=if multitexture {format!("vec4<f32>({contribution},0.0)")} else {contribution};
        vertex += &format!("if ({params}.x>0.5 && {falloff}.y>0.5) {{ output.shadow_vertex{slot}={contribution}; }}\n");
    }
    source.replace_range(start..end, &vertex);
    source = source.replace(
        "output.material_slot = input.material_slot;",
        "output.material_slot = select(input.material_slot, u.flat_shadow_primary.x, u.flat_shadow_primary.y != 0u);",
    );
    // Positional Coin GLSL lights use the local eye; directional half vectors
    // retain the fixed-function convention.
    source=source.replace("let v = vec3<f32>(0.0, 0.0, 1.0);", "var v=vec3<f32>(0.0,0.0,1.0); if (u.light_meta.w>0.5 && light.position_type.w>0.5) { v=normalize(-position_view); }");
    source = source.replace(
        "if (f32(i) >= u.light_meta.x) { break; }",
        "if (f32(i) >= u.light_meta.x || u.light_meta.z>0.5) { break; }",
    );
    for slot in 0..4 {
        let suffix = suffixes[slot];
        let original=format!("let light = u.lights[u32(u.shadow_meta{suffix}.y)];\n        let contribution = light_contribution(light,\n            materials[input.material_slot], input.position_view, input.normal_view.xyz);");
        let replacement=format!("let light = u.lights[u32(u.shadow_meta{suffix}.y)];\n        var contribution=input.shadow_vertex{slot};\n        var specularPart=vec3<f32>(0.0);\n        if (u.shadow_falloff{suffix}.y<0.5) {{ contribution=light_contribution(light,materials[input.material_slot],input.position_view,input.normal_view.xyz); specularPart=shadow_specular(light,materials[input.material_slot],input.position_view,input.normal_view.xyz); }}");
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
            "(u.shadow_model_view_projection_second * vec4<f32>(input.position_model.xyz,1.0))",
        )
        .replace(
            "input.shadow_light_view_second",
            "(u.shadow_model_view_second * vec4<f32>(input.position_model.xyz,1.0)).xyz",
        )
        .replace(
            "input.shadow_coord",
            "(u.shadow_model_view_projection * vec4<f32>(input.position_model.xyz,1.0))",
        )
        .replace(
            "input.shadow_light_view",
            "(u.shadow_model_view * vec4<f32>(input.position_model.xyz,1.0)).xyz",
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
    ordinary+=") {continue;} let spec=shadow_specular(u.lights[i],materials[input.material_slot],input.position_view,input.normal_view.xyz); primary=vec4<f32>(primary.rgb+light_contribution(u.lights[i],materials[input.material_slot],input.position_view,input.normal_view.xyz)-spec,primary.a); specularColor+=spec; } }\n";
    source = source.replace(
        "var primary=input.diffuse_color;",
        &format!("var primary=input.diffuse_color;\nvar specularColor=vec3<f32>(0.0);\n{ordinary}"),
    );
    source=source.replace("return apply_fog(base_color, input.normal_view.w);", "return apply_fog(vec4<f32>(clamp(base_color.rgb+specularColor,vec3<f32>(0.0),vec3<f32>(1.0)),base_color.a), input.normal_view.w);");
    if multitexture {
        // Reuse the fourth lanes of the eight independent RGB lighting
        // contributions for R. Full STRQ still fits the negotiated 20 varying
        // locations / 80 components, including sample-depth specialization.
        for slot in 0..8 {
            source=source.replace(&format!("input.shadow_vertex{slot}"),
                    &format!("input.shadow_vertex{slot}.xyz"));
        }
        let mut vertex = String::new();
        let mut fields = Vec::new();
        for unit in 1..8 {
            vertex += &format!("let shadow_uv{unit} = u.extra_texture_matrices[{}] * input.uv{unit};\noutput.shadow_vertex{}.w=shadow_uv{unit}.z;\n", unit-1, unit-1);
            for component in ["x", "y", "w"] { fields.push(format!("shadow_uv{unit}.{component}")); }
        }
        while fields.len() < 24 { fields.push("0.0".to_owned()); }
        for slot in 0..6 { vertex += &format!("output.shadow_uv{slot}=vec4<f32>({});\n", fields[4*slot..4*slot+4].join(",")); }
        let vs = source.find("fn vs_main(").unwrap();
        let end = vs + source[vs..].find("return output;").unwrap();
        source.insert_str(end, &vertex);
        let fragment = source.find("fn fragment_color(").unwrap();
        let mut body = source[fragment..].to_owned();
        for unit in 1..8 {
            let fields: Vec<_> = (3*(unit-1)..3*unit).map(|k| format!("input.shadow_uv{}.{}", k/4, ["x","y","z","w"][k%4])).collect();
            body = body.replace(&format!("input.uv{unit}"), &format!("vec4<f32>({},{},input.shadow_vertex{}.w,{})", fields[0],fields[1],unit-1,fields[2]));
        }
        source.replace_range(fragment.., &body);
    }
    source += "\nfn shadow_specular(light:GpuLight,material:GpuMaterial,position:vec3<f32>,normal:vec3<f32>)->vec3<f32> { var spec=material; spec.diffuse=vec4<f32>(0.0); spec.ambient=vec4<f32>(0.0); return light_contribution(light,spec,position,normal); }\n";
    source
}

// One unfiltered array binding leaves the two peeling depths and all eight
// independently filtered scene textures available together. Each layer keeps
// its original map extent; unused padding is never sampled.
fn array_moments_source(source: String) -> String {
    let mut source = source.lines().filter(|line| {
        !(line.contains("var t_shadow") || line.contains("var t_extra_shadow"))
    }).collect::<Vec<_>>().join("\n");
    source = format!("@group(0) @binding(20) var t_shadow_array: texture_2d_array<f32>;\n{source}");
    source = source.replace("shadow_map: texture_2d<f32>", "shadow_layer: i32")
        .replace("let size = textureDimensions(shadow_map);", "let size = vec2<u32>(u32(falloff.z));");
    for point in ["p00", "p10", "p01", "p11"] {
        source = source.replace(&format!("textureLoad(shadow_map, {point}, 0)"),
            &format!("textureLoad(t_shadow_array, {point}, shadow_layer, 0)"));
    }
    for (name, slot) in [("t_shadow_second",1),("t_shadow_third",2),("t_shadow_fourth",3),
        ("t_extra_shadow0",4),("t_extra_shadow1",5),("t_extra_shadow2",6),("t_extra_shadow3",7),("t_shadow",0)] {
        source = source.replace(&format!(", {name})"), &format!(", {slot})"))
            .replace(&format!(",{name})"), &format!(",{slot})"));
    }
    source
}
