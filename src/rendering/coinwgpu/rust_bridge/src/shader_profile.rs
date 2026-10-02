// Keep the transported uniform ABI intact. Specialization only removes work
// whose absence is already known from the validated draw/active shadow profile.
pub(super) fn texture_specialization(source: &str) -> String {
    format!("override coin_textures_enabled: bool = true;\n{}", source
        .replace("if (u.tex_params.x > 0.5)",
            "if (coin_textures_enabled && u.tex_params.x > 0.5)")
        .replace("if (u.extra_tex_params[",
            "if (coin_textures_enabled && u.extra_tex_params["))
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
mod tests {
    use super::*;
    use naga::valid::{Capabilities, ValidationFlags, Validator};

    #[test]
    fn all_specialized_profiles_preserve_valid_entry_points() {
        let standard = include_str!("../../shaders/coin_standard.wgsl");
        for source in [without_shadows(standard), super::super::shadow_receiver::four_map_source(),
            super::super::shadow_receiver::eight_map_source(),
            include_str!("../../shaders/coin_line.wgsl").to_owned(),
            include_str!("../../shaders/coin_point.wgsl").to_owned()] {
            let source = texture_specialization(&format!("{}{}", source, super::super::weighted::FRAGMENT));
            let module = naga::front::wgsl::parse_str(&source).expect("parse specialized profile");
            Validator::new(ValidationFlags::all(), Capabilities::all())
                .validate(&module).expect("validate specialized profile");
            for entry in ["vs_main", "fs_main", "fs_peel", "fs_weighted", "fs_depth_bias"] {
                assert!(module.entry_points.iter().any(|point| point.name == entry));
            }
        }
    }
}
