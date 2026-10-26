//! Frame payload validation and material packing.

use super::*;

pub(super) fn uniform_binding_bytes(_instanced: bool, _policy: SamplingPolicy) -> usize {
    std::mem::size_of::<CoinWgpuUniforms>()
}

pub(super) fn valid_alpha_test(st: &CoinWgpuRenderState) -> bool {
    st.alpha_test_function <= 8 && st.alpha_test_reference.is_finite()
        && (0.0..=1.0).contains(&st.alpha_test_reference)
}

// Validate the normalized transport program, without interpreting Coin enums.
pub(super) fn valid_texture_program(p: &[[f32; 4]; 4]) -> bool {
    if !p.iter().flatten().all(|v| v.is_finite()) { return false; }
    if p[0][0] == 0.0 { return true; }
    let integer = |v: f32, lo: f32, hi: f32| v >= lo && v <= hi && v.fract() == 0.0;
    p[0][0] == 1.0 && integer(p[0][1], 0.0, 7.0) && integer(p[0][2], 0.0, 5.0)
        && (1..=2).all(|r| [1.0, 2.0, 4.0].contains(&p[r][3])
            && (0..3).all(|c| integer(p[r][c], if r == 2 { 8.0 } else { 0.0 }, 15.0)))
        && p[3].iter().all(|v| *v >= 0.0 && *v <= 1.0)
}

pub(super) fn valid_texture_payload(st: &CoinWgpuRenderState) -> bool {
    st.flat_shadow_primary <= 1 && st.texture_projection <= 1 && st.texture_combines.iter().all(valid_texture_program)
        && st.extra_textures.iter().all(|t| t.enabled <= 1 && (t.enabled == 0
            || (t.model <= 3 && t.matrix.iter().chain(t.blend_color.iter()).all(|v| v.is_finite()))))
}

// Direct FFI callers obey the same nonsingular primitive contract as the
// common capture. Inactive units permit zeroed transport, including q=0.
pub(super) fn cube_direction_primitive_valid(d:&[[f64;3]])->bool{
    let cross=|a:[f64;3],b:[f64;3]|[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
    let dot=|a:[f64;3],b:[f64;3]|a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
    if d.len()<2{return true;}
    let scale=d.iter().flatten().fold(0.0f64,|a,b|a.max(b.abs()));
    let v:Vec<[f64;3]>=d.iter().map(|p|[p[0]/scale,p[1]/scale,p[2]/scale]).collect();
    for i in 0..v.len(){for j in i+1..v.len(){if cross(v[i],v[j])==[0.0;3]&&dot(v[i],v[j])<=0.0{return false;}}}
    if v.len()==3{
        let ab=cross(v[0],v[1]);let bc=cross(v[1],v[2]);let ca=cross(v[2],v[0]);
        let normal=if ab!=[0.0;3]{ab}else if bc!=[0.0;3]{bc}else{ca};
        if normal!=[0.0;3]&&v.iter().all(|p|dot(normal,*p)==0.0){
            let x=dot(ab,normal);let y=dot(bc,normal);let z=dot(ca,normal);
            if (x>=0.0&&y>=0.0&&z>=0.0)||(x<=0.0&&y<=0.0&&z<=0.0){return false;}
        }
    }
    true
}

// Retain the pre-cube private validator for callers without image metadata.
#[allow(dead_code)]
pub(super) fn validate_texture_coordinates(st:&CoinWgpuRenderState,topology:u32,vertices:&[CoinWgpuVertex],indices:&[u32])->Result<(),String>{
    validate_texture_coordinates_with_images(st,topology,vertices,indices,&[])
}

pub(super) fn validate_texture_coordinates_with_images(st: &CoinWgpuRenderState, topology: u32,
    vertices: &[CoinWgpuVertex], indices: &[u32], textures: &[CoinWgpuTexture]) -> Result<(), String> {
    if st.texture_projection > 1 { return Err("Invalid texture projection policy".into()); }
    // The trusted module Core validates the consumed DAG, not fixed sampling.
    if st.portable_shader_slot != 0 { return Ok(()); }
    if st.has_texture == 0 && st.extra_textures.iter().all(|t| t.enabled == 0) {
        return Ok(());
    }
    let primitive_size = match topology { 1 => 2, 2 => 1, _ => 3 };
    for unit in 0..8 {
        let matrix = if unit == 0 {
            if st.has_texture == 0 { continue; }
            &st.texture_matrix
        } else {
            let layer = &st.extra_textures[unit - 1];
            if layer.enabled == 0 { continue; }
            &layer.matrix
        };
        let slot=if unit==0{st.texture_slot}else{st.extra_textures[unit-1].texture_slot};
        let cube=textures.get(slot as usize).is_some_and(|t|t.format==12);
        let volume=textures.get(slot as usize).is_some_and(|t|t.format==11);
        for primitive in indices.chunks_exact(primitive_size) {
            let mut cube_directions=Vec::new();
            let mut previous_negative = None;
            for index in primitive {
                let vertex = vertices.get(*index as usize).ok_or_else(||
                    "Projective texture index out of bounds".to_string())?;
                let coordinate = if unit == 0 { &vertex.texcoord }
                    else { &vertex.extra_texcoords[unit - 1] };
                if !coordinate.iter().chain(matrix.iter()).all(|v| v.is_finite()) {
                    return Err("Non-finite homogeneous texture coordinate or matrix".into());
                }
                let mut transformed = [0.0_f32; 4];
                for row in 0..4 {
                    transformed[row] = matrix[row] * coordinate[0]
                        + matrix[4 + row] * coordinate[1]
                        + matrix[8 + row] * coordinate[2]
                        + matrix[12 + row] * coordinate[3];
                }
                if cube {
                    if !transformed.iter().all(|v|v.is_finite()) || transformed[..3].iter().all(|v|*v==0.0){
                        return Err("Invalid cube direction".into());
                    }
                    cube_directions.push([f64::from(transformed[0]),f64::from(transformed[1]),f64::from(transformed[2])]);
                    continue;
                }
                if st.texture_projection == 1 {
                    let axes=if volume {3} else {2};
                    if !transformed[..axes].iter().all(|v| v.is_finite()) {
                        return Err("Non-finite transformed direct ST texture coordinate".into());
                    }
                    continue;
                }
                if !transformed.iter().all(|v| v.is_finite()) {
                    return Err("Non-finite transformed homogeneous texture coordinate".into());
                }
                let q = transformed[3];
                let negative = q < 0.0;
                if q == 0.0 || previous_negative.is_some_and(|previous| previous != negative) {
                    return Err("Projective texture Q is zero or changes sign within a primitive".into());
                }
                if q.abs() < f32::MIN_POSITIVE || !(transformed[0] / q).is_finite()
                    || !(transformed[1] / q).is_finite()
                    || (volume && !(transformed[2] / q).is_finite()) {
                    return Err("Projective texture coordinate exceeds finite sampling precision".into());
                }
                previous_negative = Some(negative);
            }
            if cube && !cube_direction_primitive_valid(&cube_directions){return Err("Cube primitive crosses a zero direction".into());}
        }
    }
    Ok(())
}

pub(super) fn pack_gpu_materials(materials: &[CoinWgpuMaterial]) -> Vec<GpuMaterial> {
    materials.iter().map(|m| GpuMaterial {
        ambient: m.ambient,
        diffuse: m.diffuse,
        specular: m.specular,
        emission: m.emission,
        params: [m.shininess, m.transparency, 0.0, 0.0],
    }).collect()
}

pub(super) fn material_resource_reuse_enabled() -> bool {
    static ENABLED: OnceLock<bool> = OnceLock::new();
    *ENABLED.get_or_init(|| std::env::var("COIN_WGPU_DISABLE_MATERIAL_RESOURCE_REUSE").as_deref() != Ok("1"))
}
