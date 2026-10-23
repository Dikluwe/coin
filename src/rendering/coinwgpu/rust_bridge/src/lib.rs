#![allow(clippy::not_unsafe_ptr_arg_deref)]
#![allow(clippy::too_many_arguments)]
#![allow(clippy::manual_is_multiple_of)]
#![allow(clippy::if_same_then_else)]
#![allow(clippy::needless_range_loop)]
#![allow(clippy::needless_lifetimes)]
mod raster_image;
mod texture_profile;
mod texture_mips;
mod surface_present;
mod multipass;
use bytemuck::{Pod, Zeroable};
use pollster::block_on;
use raw_window_handle::{RawDisplayHandle, RawWindowHandle};
#[cfg(all(unix, not(target_os = "android")))]
use raw_window_handle::{XlibDisplayHandle, XlibWindowHandle};
#[cfg(target_os = "linux")]
use raw_window_handle::{WaylandDisplayHandle, WaylandWindowHandle};
#[cfg(target_os = "android")]
use raw_window_handle::{AndroidDisplayHandle, AndroidNdkWindowHandle};
#[cfg(target_os = "windows")]
use raw_window_handle::{Win32WindowHandle, WindowsDisplayHandle};
use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, AtomicI32, AtomicU32, AtomicU64, Ordering};
use std::sync::{Arc, Mutex, OnceLock};

mod composition;
mod peeling;
mod shadow;
mod shadow_receiver;
mod bump_receiver;
mod weighted;
mod uniform_arena;
mod shader_profile;
mod portable;
pub use portable::{CoinWgpuPortableNode,CoinWgpuPortableModule,CoinWgpuPortableRange};
mod depth_transfer;
mod instancing;
#[cfg(target_os = "android")]
mod android_log;

pub const COIN_WGPU_BRIDGE_PROTOCOL_REVISION: u32 = 69;
pub const COIN_WGPU_ABI_VERSION: u32 = COIN_WGPU_BRIDGE_PROTOCOL_REVISION;

const _: () = {
    assert!(std::mem::size_of::<CoinWgpuLight>() == 80);
    assert!(std::mem::align_of::<CoinWgpuLight>() == 4);
    assert!(std::mem::offset_of!(CoinWgpuLight, ambient_color) == 64);
    assert!(std::mem::size_of::<CoinWgpuFrameView>() == 560);
    assert!(std::mem::size_of::<CoinWgpuPortableNode>() == 40);
    assert!(std::mem::size_of::<CoinWgpuPortableModule>() == 56);
    assert!(std::mem::size_of::<CoinWgpuPortableRange>() == 64);
    assert!(std::mem::size_of::<CoinWgpuBumpVertex>() == 44);
    assert!(std::mem::size_of::<CoinWgpuBumpImage>() == 32);
    assert!(std::mem::offset_of!(CoinWgpuFrameView, bump_vertices) == 464);
    assert!(std::mem::size_of::<CoinWgpuInstance>() == 144);
    assert!(std::mem::size_of::<CoinWgpuInstanceRange>() == 16);
    assert!(std::mem::offset_of!(CoinWgpuFrameView, instances) == 416);
    assert!(std::mem::offset_of!(CoinWgpuFrameView, instance_ranges) == 432);
    assert!(std::mem::size_of::<CoinWgpuShadowPassView>() == 56);
    assert!(std::mem::offset_of!(CoinWgpuFrameView, extra_shadow_passes) == 400);
    assert!(std::mem::offset_of!(CoinWgpuFrameView, shadow_epsilon_second) == 376);
    assert!(std::mem::size_of::<CoinWgpuShadowDraw>() == 160);
    assert!(std::mem::offset_of!(CoinWgpuShadowDraw, viewport) == 144);
    assert!(std::mem::size_of::<CoinWgpuShadowReceiver>() == 144);
    assert!(std::mem::offset_of!(CoinWgpuShadowReceiver, max_shadow_distance) == 8);
    assert!(std::mem::offset_of!(CoinWgpuFrameView, shadow_receivers) == 216);
    assert!(std::mem::offset_of!(CoinWgpuFrameView, shadow_casters_second) == 232);
    assert!(std::mem::offset_of!(CoinWgpuFrameView, shadow_receivers_second) == 264);
    assert!(std::mem::offset_of!(CoinWgpuFrameView, shadow_casters) == 176);
    assert!(std::mem::offset_of!(CoinWgpuFrameView, shadow_map_size) == 192);
    assert!(std::mem::offset_of!(CoinWgpuFrameView, shadow_kind) == 212);
    assert!(std::mem::size_of::<CoinWgpuSurfaceCreateInfo>() == 56);
    assert!(std::mem::offset_of!(CoinWgpuFrameView, sorted_layers_passes) == 160);
    assert!(std::mem::offset_of!(CoinWgpuFrameView, transparency_reserved) == 164);
    assert!(std::mem::offset_of!(CoinWgpuFrameView, transparency_budget_bytes) == 168);
};

pub type CoinWgpuSurfaceId = u64;
pub const COIN_WGPU_INVALID_SURFACE_ID: CoinWgpuSurfaceId = 0;
pub type CoinWgpuDeviceId = u64;

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum CoinWgpuStatus {
    Ok = 0,
    NotReady = 1,
    InvalidArgument = 2,
    Unsupported = 3,
    OutOfMemory = 4,
    DeviceLost = 5,
    BackendError = 6,
    SurfaceLost = 7,
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct CoinWgpuNativeSurfaceDescriptor {
    pub abi_version: u32,
    pub struct_size: u32,
    pub r#type: u32,
    pub reserved: u32,
    pub handle_a: u64,
    pub handle_b: u64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct CoinWgpuSurfaceCreateInfo {
    pub abi_version: u32,
    pub struct_size: u32,
    pub native: CoinWgpuNativeSurfaceDescriptor,
    pub width: u32,
    pub height: u32,
    pub renderer: u32,
    pub sample_count: u32,
}
const _: () = assert!(std::mem::size_of::<CoinWgpuSurfaceCreateInfo>() == 56);

#[repr(C)]
#[derive(Copy, Clone, Debug, Pod, Zeroable)]
pub struct CoinWgpuVertex {
    pub position: [f32; 3],
    pub normal: [f32; 3],
    pub texcoord: [f32; 4],
    pub material_slot: u32,
    pub screen_space_w: f32,
    pub fog_eye_depth_plus_one: f32,
    pub extra_texcoords: [[f32; 4]; 7],
}

#[repr(C)]
#[derive(Copy, Clone, Debug, Pod, Zeroable)]
pub struct CoinWgpuBumpVertex {
    pub coord: [f32; 4],
    pub tangent_s: [f32; 3],
    pub tangent_t: [f32; 3],
    pub image_slot: u32,
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct CoinWgpuBumpImage {
    pub width: u32,
    pub height: u32,
    pub clamp_s: u32,
    pub clamp_t: u32,
    pub rgb: *const u8,
    pub rgb_len: u64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, Pod, Zeroable)]
pub struct CoinWgpuDraw {
    pub topology: u32,
    pub first_vertex: u32,
    pub vertex_count: u32,
    pub first_index: u32,
    pub index_count: u32,
    pub render_state_slot: u32,
    pub stable_node_id: u64,
    pub draw_ordinal: u32,
    pub composition_flags: u32,
    pub source_revision: u64,
    pub render_layer: u32,
    pub clear_depth_before: u32,
}

const _: () = {
    assert!(std::mem::size_of::<CoinWgpuDraw>() == 56);
    assert!(std::mem::offset_of!(CoinWgpuDraw, composition_flags) == 36);
    assert!(std::mem::offset_of!(CoinWgpuDraw, render_layer) == 48);
    assert!(std::mem::offset_of!(CoinWgpuDraw, clear_depth_before) == 52);
};

#[repr(C)]
#[derive(Copy, Clone, Debug, Default)]
pub struct CoinWgpuCacheStats {
    pub cumulative_uploads: u64,
    pub cumulative_hits: u64,
    pub cumulative_misses: u64,
    pub cumulative_uploaded_bytes: u64,
    pub frame_uploaded_bytes: u64,
    pub frame_uploads: u64,
    pub frame_hits: u64,
    pub active_entries: u64,
    pub retired_entries: u64,
    pub completed_serial: u64,
    pub submission_serial: u64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, Default)]
pub struct CoinWgpuPerformanceStats {
    pub texture_uploads: u64,
    pub texture_hits: u64,
    pub texture_uploaded_bytes: u64,
    pub texture_evictions: u64,
    pub texture_active_entries: u64,
    pub texture_retired_entries: u64,
    pub pipeline_compilations: u64,
    pub pipeline_hits: u64,
    pub pipeline_active_entries: u64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, Pod, Zeroable)]
pub struct CoinWgpuMaterial {
    pub ambient: [f32; 4],
    pub diffuse: [f32; 4],
    pub specular: [f32; 4],
    pub emission: [f32; 4],
    pub shininess: f32,
    pub transparency: f32,
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct CoinWgpuTexture {
    pub width: u32,
    pub height: u32,
    pub format: u32, // 0 = RGBA8_UNORM bytes; 1 = private GPU RTT token
    pub reserved: u32,
    pub content_digest: u64,
    pub pixels: *const u8,
    pub pixel_bytes_len: u64,
}

// Explicit per-frame policy; never reads a process environment selector.
#[derive(Clone, Copy, Debug, PartialEq)]
enum SamplingPolicy { Native, Portable }
impl SamplingPolicy {
    fn from_raw(value: u32) -> Result<Self, (CoinWgpuStatus, String)> {
        match value { 0 => Ok(Self::Native), 1 => Ok(Self::Portable),
            _ => Err((CoinWgpuStatus::InvalidArgument, "Invalid texture sampling policy".into())) }
    }
}
fn uniform_binding_bytes(_instanced: bool, _policy: SamplingPolicy) -> usize {
    std::mem::size_of::<CoinWgpuUniforms>()
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct CoinWgpuSampler {
    pub wrap_s: u32, // 0 = REPEAT, 1 = CLAMP_TO_EDGE
    pub wrap_t: u32, // 0 = REPEAT, 1 = CLAMP_TO_EDGE
    pub filter: u32, // 0 = NEAREST, 1 = LINEAR
    pub reserved: u32,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, Pod, Zeroable)]
pub struct CoinWgpuLight {
    pub position_type: [f32; 4],
    pub direction_cutoff: [f32; 4],
    pub color_intensity: [f32; 4],
    pub attenuation_exponent: [f32; 4],
    pub ambient_color: [f32; 4], // protocol68: RGB light ambient, W reserved zero
}

#[repr(C)]
#[derive(Copy, Clone, Debug, Pod, Zeroable)]
pub struct CoinWgpuUniforms {
    pub model_view_projection: [[f32; 4]; 4],
    pub model_view: [[f32; 4]; 4],
    pub normal_matrix: [[f32; 4]; 4],
    pub material_diffuse: [f32; 4],
    pub material_ambient: [f32; 4],
    pub material_specular: [f32; 4],
    pub light_direction_intensity: [f32; 4],
    pub light_color: [f32; 4],
    pub params: [f32; 4],
    pub texture_matrix: [[f32; 4]; 4],
    pub tex_params: [f32; 4],
    pub fog_color_mode: [f32; 4],
    pub fog_range: [f32; 4],
    pub ambient_light: [f32; 4],
    pub light_meta: [f32; 4],
    pub lights: [CoinWgpuLight; 8],
    pub texture_blend_color: [f32; 4],
    pub clip_meta: [f32; 4],
    pub clip_planes: [[f32; 4]; 8],
    pub extra_texture_matrices: [[[f32; 4]; 4]; 7],
    pub extra_tex_params: [[f32; 4]; 7],
    pub extra_texture_blends: [[f32; 4]; 7],
    pub texture_combines: [[[f32; 4]; 4]; 8],
    pub composition_meta: [f32; 4],
    pub peel_meta: [f32; 4],
    pub shadow_model_view_projection: [[f32; 4]; 4],
    pub shadow_model_view: [[f32; 4]; 4],
    pub shadow_params: [f32; 4],
    pub shadow_meta: [f32; 4],
    pub shadow_falloff: [f32; 4],
    pub shadow_model_view_projection_second: [[f32; 4]; 4],
    pub shadow_model_view_second: [[f32; 4]; 4],
    pub shadow_params_second: [f32; 4],
    pub shadow_meta_second: [f32; 4],
    pub shadow_falloff_second: [f32; 4],
    pub shadow_model_view_projection_third: [[f32; 4]; 4],
    pub shadow_model_view_third: [[f32; 4]; 4],
    pub shadow_params_third: [f32; 4],
    pub shadow_meta_third: [f32; 4],
    pub shadow_falloff_third: [f32; 4],
    pub shadow_model_view_projection_fourth: [[f32; 4]; 4],
    pub shadow_model_view_fourth: [[f32; 4]; 4],
    pub shadow_params_fourth: [f32; 4],
    pub shadow_meta_fourth: [f32; 4],
    pub shadow_falloff_fourth: [f32; 4],
    pub alpha_test: [f32; 4],
    // Private GPU block, never part of the transported C ABI. Actual bound extents.
    pub raster_viewport: [f32; 4],
    pub flat_shadow_primary: [u32; 4],
    pub sampling_texture_sizes: [[f32; 4]; 8],
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct CoinWgpuTextureUnit {
    pub matrix: [f32; 16],
    pub enabled: u32,
    pub texture_slot: u32,
    pub sampler_slot: u32,
    pub model: u32,
    pub blend_color: [f32; 4],
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct CoinWgpuRenderState {
    pub model_view: [f32; 16],
    pub model_view_projection: [f32; 16],
    pub normal_matrix: [f32; 16],
    pub light_direction: [f32; 4],
    pub light_color: [f32; 4],
    pub light_intensity: f32,
    pub has_light: u32,
    pub material_slot: u32,
    pub cull_mode: u32,  // 0=None, 1=Back, 2=Front
    pub front_face: u32, // 0=Ccw, 1=Cw
    pub light_model: u32, // 0=BaseColor, 1=Phong
    pub texture_matrix: [f32; 16],
    pub has_texture: u32,
    pub texture_slot: u32,
    pub sampler_slot: u32,
    pub texture_model: u32,
    pub texture_blend_color: [f32; 4],
    pub viewport: [i32; 4],
    pub light_count: u32,
    pub ambient_light: [f32; 4],
    pub lights: [CoinWgpuLight; 8],
    pub fog_mode: u32,
    pub fog_color: [f32; 3],
    pub fog_start: f32,
    pub fog_end: f32,
    pub depth_test: u32,
    pub depth_write: u32,
    pub depth_function: u32,
    pub depth_range: [f32; 2],
    pub polygon_offset_enabled: u32,
    pub polygon_offset_factor: f32,
    pub polygon_offset_units: f32,
    pub polygon_offset_styles: u32,
    pub polygon_offset_primitive_style: u32,
    pub clip_plane_count: u32,
    pub clip_planes: [[f32; 4]; 8],
    pub polygon_offset_slope_bias: f32,
    pub polygon_offset_max_depth_bits: u32,
    pub extra_textures: [CoinWgpuTextureUnit; 7],
    pub texture_combines: [[[f32; 4]; 4]; 8],
    pub alpha_test_function: u32,
    pub alpha_test_reference: f32,
    pub texture_projection: u32,
    pub color_write: u32,
    pub flat_shadow_primary: u32,
    pub raster_texture_source_slot: u32,
    pub raster_texture_coordinates: [[f32; 4]; 8],
    pub portable_shader_slot: u32,
}

fn valid_alpha_test(st: &CoinWgpuRenderState) -> bool {
    st.alpha_test_function <= 8 && st.alpha_test_reference.is_finite()
        && (0.0..=1.0).contains(&st.alpha_test_reference)
}

// Validate the normalized transport program, without interpreting Coin enums.
fn valid_texture_program(p: &[[f32; 4]; 4]) -> bool {
    if !p.iter().flatten().all(|v| v.is_finite()) { return false; }
    if p[0][0] == 0.0 { return true; }
    let integer = |v: f32, lo: f32, hi: f32| v >= lo && v <= hi && v.fract() == 0.0;
    p[0][0] == 1.0 && integer(p[0][1], 0.0, 7.0) && integer(p[0][2], 0.0, 5.0)
        && (1..=2).all(|r| [1.0, 2.0, 4.0].contains(&p[r][3])
            && (0..3).all(|c| integer(p[r][c], if r == 2 { 8.0 } else { 0.0 }, 15.0)))
        && p[3].iter().all(|v| *v >= 0.0 && *v <= 1.0)
}

fn valid_texture_payload(st: &CoinWgpuRenderState) -> bool {
    st.flat_shadow_primary <= 1 && st.texture_projection <= 1 && st.texture_combines.iter().all(valid_texture_program)
        && st.extra_textures.iter().all(|t| t.enabled <= 1 && (t.enabled == 0
            || (t.model <= 3 && t.matrix.iter().chain(t.blend_color.iter()).all(|v| v.is_finite()))))
}

// Direct FFI callers obey the same nonsingular primitive contract as the
// common capture. Inactive units permit zeroed transport, including q=0.
fn cube_direction_primitive_valid(d:&[[f64;3]])->bool{
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
fn validate_texture_coordinates(st:&CoinWgpuRenderState,topology:u32,vertices:&[CoinWgpuVertex],indices:&[u32])->Result<(),String>{
    validate_texture_coordinates_with_images(st,topology,vertices,indices,&[])
}
fn validate_texture_coordinates_with_images(st: &CoinWgpuRenderState, topology: u32,
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

#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/lib_texture_payload_tests.rs"]
mod texture_payload_tests;

const _: () = {
    assert!(std::mem::size_of::<CoinWgpuTextureUnit>() == 96);
    assert!(std::mem::offset_of!(CoinWgpuVertex, texcoord) == 24);
    assert!(std::mem::offset_of!(CoinWgpuVertex, material_slot) == 40);
    assert!(std::mem::offset_of!(CoinWgpuVertex, screen_space_w) == 44);
    assert!(std::mem::offset_of!(CoinWgpuVertex, fog_eye_depth_plus_one) == 48);
    assert!(std::mem::offset_of!(CoinWgpuVertex, extra_texcoords) == 52);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, extra_textures) == 1224);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, texture_combines) == 1896);
    assert!(std::mem::size_of::<CoinWgpuVertex>() == 164);
    assert!(std::mem::size_of::<CoinWgpuRenderState>() == 2564);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, raster_texture_source_slot) == 2428);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, raster_texture_coordinates) == 2432);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, alpha_test_function) == 2408);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, alpha_test_reference) == 2412);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, texture_projection) == 2416);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, color_write) == 2420);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, polygon_offset_max_depth_bits) == 1220);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, polygon_offset_slope_bias) == 1216);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, clip_plane_count) == 1084);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, clip_planes) == 1088);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, polygon_offset_enabled) == 1064);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, depth_test) == 1044);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, depth_write) == 1048);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, depth_function) == 1052);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, depth_range) == 1056);
};

#[repr(C)]
#[derive(Copy, Clone, Debug, Pod, Zeroable)]
pub struct GpuMaterial {
    pub ambient: [f32; 4],
    pub diffuse: [f32; 4],
    pub specular: [f32; 4],
    pub emission: [f32; 4],
    pub params: [f32; 4], // x=shininess, y=transparency, z=0, w=0
}

fn pack_gpu_materials(materials: &[CoinWgpuMaterial]) -> Vec<GpuMaterial> {
    materials.iter().map(|m| GpuMaterial {
        ambient: m.ambient,
        diffuse: m.diffuse,
        specular: m.specular,
        emission: m.emission,
        params: [m.shininess, m.transparency, 0.0, 0.0],
    }).collect()
}

fn material_resource_reuse_enabled() -> bool {
    static ENABLED: OnceLock<bool> = OnceLock::new();
    *ENABLED.get_or_init(|| std::env::var("COIN_WGPU_DISABLE_MATERIAL_RESOURCE_REUSE").as_deref() != Ok("1"))
}

#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/lib_material_resource_tests.rs"]
mod material_resource_tests;

#[repr(C)]
#[derive(Copy, Clone, Debug, Pod, Zeroable)]
pub struct CoinWgpuShadowDraw {
    pub first_index: u32,
    pub index_count: u32,
    pub render_state_slot: u32,
    pub reserved: u32,
    pub model_view: [f32; 16],
    pub model_view_projection: [f32; 16],
    pub viewport: [i32; 4],
}

#[repr(C)]
#[derive(Copy, Clone, Debug, Pod, Zeroable)]
pub struct CoinWgpuShadowReceiver {
    pub receives: u32,
    pub lighting_index: i32,
    pub max_shadow_distance: f32,
    pub distance_falloff_coefficient: f32,
    pub model_view: [f32; 16],
    pub model_view_projection: [f32; 16],
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct CoinWgpuShadowPassView {
    pub casters: *const CoinWgpuShadowDraw,
    pub caster_count: u64,
    pub receivers: *const CoinWgpuShadowReceiver,
    pub receiver_count: u64,
    pub map_size: u32,
    pub kind: u32,
    pub near_distance: f32,
    pub far_distance: f32,
    pub epsilon: f32,
    pub threshold: f32,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, Pod, Zeroable)]
pub struct CoinWgpuInstance {
    pub model_view: [f32; 16],
    pub normal_matrix: [f32; 16],
    pub material_slot: u32,
    pub reserved: [u32; 3],
}

#[repr(C)]
#[derive(Copy, Clone, Debug, Pod, Zeroable)]
pub struct CoinWgpuInstanceRange {
    pub draw_index: u32,
    pub first_instance: u32,
    pub instance_count: u32,
    pub reserved: u32,
}

#[repr(C)]
pub struct CoinWgpuFrameView {
    pub abi_version: u32,
    pub struct_size: u32,
    pub frame_revision: u64,

    pub vertices: *const CoinWgpuVertex,
    pub vertex_count: u64,
    pub indices: *const u32,
    pub index_count: u64,
    pub draws: *const CoinWgpuDraw,
    pub draw_count: u64,

    pub materials: *const CoinWgpuMaterial,
    pub material_count: u64,
    pub states: *const CoinWgpuRenderState,
    pub state_count: u64,

    pub textures: *const CoinWgpuTexture,
    pub texture_count: u64,
    pub samplers: *const CoinWgpuSampler,
    pub sampler_count: u64,

    pub clear_color: [f32; 4],
    pub width: u32,
    pub height: u32,
    pub camera_base_revision: u64,
    pub sorted_layers_passes: u32,
    pub transparency_reserved: u32,
    pub transparency_budget_bytes: u64,
    pub shadow_casters: *const CoinWgpuShadowDraw,
    pub shadow_caster_count: u64,
    pub shadow_map_size: u32,
    pub shadow_near_distance: f32,
    pub shadow_far_distance: f32,
    pub shadow_epsilon: f32,
    pub shadow_threshold: f32,
    pub shadow_kind: u32,
    pub shadow_receivers: *const CoinWgpuShadowReceiver,
    pub shadow_receiver_count: u64,
    pub shadow_casters_second: *const CoinWgpuShadowDraw,
    pub shadow_caster_count_second: u64,
    pub shadow_map_size_second: u32,
    pub shadow_near_distance_second: f32,
    pub shadow_far_distance_second: f32,
    pub shadow_kind_second: u32,
    pub shadow_receivers_second: *const CoinWgpuShadowReceiver,
    pub shadow_receiver_count_second: u64,
    pub shadow_casters_third: *const CoinWgpuShadowDraw,
    pub shadow_caster_count_third: u64,
    pub shadow_map_size_third: u32,
    pub shadow_near_distance_third: f32,
    pub shadow_far_distance_third: f32,
    pub shadow_kind_third: u32,
    pub shadow_receivers_third: *const CoinWgpuShadowReceiver,
    pub shadow_receiver_count_third: u64,
    pub shadow_casters_fourth: *const CoinWgpuShadowDraw,
    pub shadow_caster_count_fourth: u64,
    pub shadow_map_size_fourth: u32,
    pub shadow_near_distance_fourth: f32,
    pub shadow_far_distance_fourth: f32,
    pub shadow_kind_fourth: u32,
    pub shadow_receivers_fourth: *const CoinWgpuShadowReceiver,
    pub shadow_receiver_count_fourth: u64,
    pub shadow_epsilon_second: f32,
    pub shadow_threshold_second: f32,
    pub shadow_epsilon_third: f32,
    pub shadow_threshold_third: f32,
    pub shadow_epsilon_fourth: f32,
    pub shadow_threshold_fourth: f32,
    pub extra_shadow_passes: *const CoinWgpuShadowPassView,
    pub extra_shadow_pass_count: u64,
    pub instances: *const CoinWgpuInstance,
    pub instance_count: u64,
    pub instance_ranges: *const CoinWgpuInstanceRange,
    pub instance_range_count: u64,
    pub texture_output_flags: u32,
    pub texture_sampling_policy: u32,
    pub rasterization_policy: u32,
    pub bump_vertices: *const CoinWgpuBumpVertex,
    pub bump_vertex_count: u64,
    pub bump_images: *const CoinWgpuBumpImage,
    pub bump_image_count: u64,
    pub portable_modules: *const CoinWgpuPortableModule,
    pub portable_module_count: u64,
    pub multipass_count: u32,
    pub multipass_reserved: u32,
    pub multipass_budget_bytes: u64,
    pub multipass_matrices: *const f32,
    pub multipass_matrix_count: u64,
    pub multipass_clear_colors: *const f32,
    pub multipass_clear_color_count: u64,
}

fn bump_payload_present(frame: &CoinWgpuFrameView) -> bool {
    frame.bump_vertex_count != 0 || !frame.bump_vertices.is_null() ||
        frame.bump_image_count != 0 || !frame.bump_images.is_null()
}

#[allow(dead_code)]
struct ValidatedBumpImage {
    width: u32,
    height: u32,
    clamp_s: bool,
    clamp_t: bool,
    rgb: Vec<u8>,
}

#[allow(dead_code)]
struct ValidatedBumpPayload {
    vertices: Vec<CoinWgpuBumpVertex>,
    images: Vec<ValidatedBumpImage>,
}

#[allow(dead_code)]
#[derive(Clone)]
struct GpuBumpImage {
    texture: wgpu::Texture,
    view: wgpu::TextureView,
    sampler: wgpu::Sampler,
}

#[allow(dead_code)]
#[derive(Clone)]
struct GpuBumpPayload {
    vertices: wgpu::Buffer,
    images: Vec<GpuBumpImage>,
}

#[derive(Clone)]
struct GpuBumpBaseImage {
    _texture: wgpu::Texture,
    view: wgpu::TextureView,
    sampler: wgpu::Sampler,
}

#[derive(Hash, PartialEq, Eq, Clone)]
struct BumpBaseKey {
    width: u32,
    height: u32,
    format: u32,
    digest: u64,
    wrap_s: u32,
    wrap_t: u32,
    filter: u32,
    anisotropy: u32,
}

struct BumpBaseCacheEntry {
    image: GpuBumpBaseImage,
    pixels_copy: Vec<u8>,
    last_used_serial: u64,
}

#[derive(Default)]
struct BumpBaseCache {
    entries: HashMap<BumpBaseKey, BumpBaseCacheEntry>,
    retired: Vec<(GpuBumpBaseImage, u64)>,
    uploads: u64,
    hits: u64,
}

struct GpuBumpBaseDraw {
    image_index: usize,
    uniform: wgpu::Buffer,
}

// ABI60 reserves bits 0..3 for anisotropy-minus-one and bit 4 for
// wrap-R. R addressing has no effect on a 2D base image, but remains valid.
fn bump_base_anisotropy(settings: &CoinWgpuSampler)
    -> Result<u16, (CoinWgpuStatus, String)> {
    if settings.wrap_s > 1 || settings.wrap_t > 1 || settings.filter > 3 ||
        settings.reserved & !31 != 0 {
        return Err((CoinWgpuStatus::InvalidArgument, "Invalid bump base sampler bits".into()));
    }
    texture_profile::anisotropy(settings.reserved & 15, settings.filter)
}

fn upload_bump_base_image(device: &wgpu::Device, queue: &wgpu::Queue,
    source: &CoinWgpuTexture, settings: &CoinWgpuSampler,
) -> Result<GpuBumpBaseImage, (CoinWgpuStatus, String)> {
    let anisotropy = bump_base_anisotropy(settings)?;
    let levels = texture_levels(source.width, source.height, source.format)?;
    let texture = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("Coin bump base image"),
        size: wgpu::Extent3d { width: source.width, height: source.height, depth_or_array_layers: 1 },
        mip_level_count: levels.len() as u32, sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: if matches!(source.format, 3 | 4) {
            wgpu::TextureFormat::Rgba8UnormSrgb
        } else { wgpu::TextureFormat::Rgba8Unorm },
        usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
        view_formats: &[],
    });
    let pixels = unsafe { std::slice::from_raw_parts(source.pixels, source.pixel_bytes_len as usize) };
    for (level, &(width, height, offset)) in levels.iter().enumerate() {
        let bytes = u64::from(width) * u64::from(height) * 4;
        queue.write_texture(wgpu::TexelCopyTextureInfo {
            texture: &texture, mip_level: level as u32,
            origin: wgpu::Origin3d::ZERO, aspect: wgpu::TextureAspect::All,
        }, &pixels[offset as usize..(offset + bytes) as usize],
            wgpu::TexelCopyBufferLayout { offset: 0, bytes_per_row: Some(width * 4),
                rows_per_image: Some(height) },
            wgpu::Extent3d { width, height, depth_or_array_layers: 1 });
    }
    let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
    let address = |wrap| if wrap == 0 { wgpu::AddressMode::Repeat } else { wgpu::AddressMode::ClampToEdge };
    let filter = if settings.filter == 0 { wgpu::FilterMode::Nearest } else { wgpu::FilterMode::Linear };
    let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
        address_mode_u: address(settings.wrap_s), address_mode_v: address(settings.wrap_t),
        mag_filter: filter,
        min_filter: if settings.filter == 2 { wgpu::FilterMode::Nearest } else { filter },
        mipmap_filter: if settings.filter >= 2 { wgpu::FilterMode::Linear } else { wgpu::FilterMode::Nearest },
        lod_max_clamp: if settings.filter >= 2 { 32.0 } else { 0.0 },
        anisotropy_clamp: anisotropy,
        ..Default::default()
    });
    Ok(GpuBumpBaseImage { _texture: texture, view, sampler })
}

#[cfg(test)]
thread_local! {
    static LAST_BUMP_BASE_IMAGE_UPLOADS: std::cell::Cell<usize> = const { std::cell::Cell::new(0) };
}

// One scope belongs to one immutable AA replay. Exact byte keys share uploads
// even for uncached/oversized draws and portable/bump material expansions.
// It never escapes the call, retains a device/runtime, or affects one-pass work.
#[derive(Default)]
struct MultipassUploads {
    buffers: HashMap<(u32,Vec<u8>),wgpu::Buffer>,
    bump: Option<GpuBumpPayload>,
    peeling: HashMap<(u32,u32,wgpu::TextureFormat,usize,u32),Arc<peeling::Peeling>>,
    weighted: HashMap<(u32,u32,wgpu::TextureFormat,u32),Arc<weighted::Weighted>>,
    raster: Option<Arc<HashMap<usize,raster_image::Image>>>,
}
thread_local! {
    static MULTIPASS_UPLOADS: std::cell::RefCell<Option<MultipassUploads>> = const { std::cell::RefCell::new(None) };
}
struct MultipassUploadScope(Option<MultipassUploads>);
impl MultipassUploadScope {
    fn begin() -> Self {
        Self(MULTIPASS_UPLOADS.with(|slot|slot.replace(Some(MultipassUploads::default()))))
    }
}
impl Drop for MultipassUploadScope {
    fn drop(&mut self) {
        MULTIPASS_UPLOADS.with(|slot| { slot.replace(self.0.take()); });
    }
}
fn multipass_upload_buffer(device:&wgpu::Device, descriptor:&wgpu::util::BufferInitDescriptor<'_>) -> wgpu::Buffer {
    use wgpu::util::DeviceExt;
    MULTIPASS_UPLOADS.with(|slot| {
        let mut slot=slot.borrow_mut();
        if let Some(replay)=slot.as_mut() {
            let key=(descriptor.usage.bits(),descriptor.contents.to_vec());
            replay.buffers.entry(key).or_insert_with(||device.create_buffer_init(descriptor)).clone()
        } else { device.create_buffer_init(descriptor) }
    })
}

fn upload_bump_payload(device: &wgpu::Device, queue: &wgpu::Queue,
                       payload: &ValidatedBumpPayload) -> Result<GpuBumpPayload, String> {
    if let Some(cached)=MULTIPASS_UPLOADS.with(|slot|slot.borrow().as_ref().and_then(|replay|replay.bump.clone())) {
        return Ok(cached);
    }
    if payload.vertices.is_empty() || payload.images.is_empty() {
        return Err("Empty bump GPU payload".to_string());
    }
    if payload.images.iter().any(|image|
        image.width > device.limits().max_texture_dimension_2d ||
        image.height > device.limits().max_texture_dimension_2d) {
        return Err("Bump image exceeds GPU texture dimension limit".to_string());
    }
    let vertex_bytes = bytemuck::cast_slice(&payload.vertices);
    let vertices = device.create_buffer(&wgpu::BufferDescriptor {
        label: Some("Coin bump vertex stream"), size: vertex_bytes.len() as u64,
        usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST |
            wgpu::BufferUsages::COPY_SRC,
        mapped_at_creation: false,
    });
    queue.write_buffer(&vertices, 0, vertex_bytes);
    let mut images = Vec::with_capacity(payload.images.len());
    for source in &payload.images {
        let mut rgba = Vec::with_capacity(source.rgb.len() / 3 * 4);
        for rgb in source.rgb.chunks_exact(3) {
            rgba.extend_from_slice(&[rgb[0], rgb[1], rgb[2], 255]);
        }
        let texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Coin bump normal image"),
            size: wgpu::Extent3d { width: source.width, height: source.height,
                depth_or_array_layers: 1 },
            mip_level_count: 1, sample_count: 1, dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST |
                wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        queue.write_texture(texture.as_image_copy(), &rgba,
            wgpu::TexelCopyBufferLayout { offset: 0,
                bytes_per_row: Some(source.width * 4), rows_per_image: Some(source.height) },
            texture.size());
        let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
        let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            address_mode_u: if source.clamp_s { wgpu::AddressMode::ClampToEdge }
                else { wgpu::AddressMode::Repeat },
            address_mode_v: if source.clamp_t { wgpu::AddressMode::ClampToEdge }
                else { wgpu::AddressMode::Repeat },
            mag_filter: wgpu::FilterMode::Linear,
            min_filter: wgpu::FilterMode::Linear,
            ..Default::default()
        });
        images.push(GpuBumpImage { texture, view, sampler });
    }
    let result=GpuBumpPayload { vertices, images };
    MULTIPASS_UPLOADS.with(|slot| {
        if let Some(replay)=slot.borrow_mut().as_mut() { replay.bump=Some(result.clone()); }
    });
    Ok(result)
}

#[repr(C)]
#[allow(dead_code)]
#[derive(Copy, Clone, bytemuck::Pod, bytemuck::Zeroable)]
struct BumpUniforms {
    model_view_projection: [f32; 16],
    light_position_type: [f32; 4],
    diffuse: [f32; 4],
    eye_position: [f32; 4],
    specular: [f32; 4],
    shininess: [f32; 4],
}

const _: () = assert!(std::mem::size_of::<BumpUniforms>() == 144);

#[repr(C)]
#[derive(Copy, Clone, bytemuck::Pod, bytemuck::Zeroable)]
struct BumpBaseUniforms { texture_matrix: [f32; 16] }
const _: () = assert!(std::mem::size_of::<BumpBaseUniforms>() == 64);

#[allow(dead_code)]
fn bump_uniforms_from_state(state: &CoinWgpuRenderState,
                            material: &CoinWgpuMaterial) -> Result<BumpUniforms, String> {
    if state.light_count != 1 ||
        !matches!(state.lights[0].position_type[3], 0.0 | 1.0 | 2.0) ||
        state.model_view[3] != 0.0 || state.model_view[7] != 0.0 ||
        state.model_view[11] != 0.0 || state.model_view[15] != 1.0 ||
        material.transparency != 0.0 || material.diffuse[3] != 1.0 {
        return Err("Bump lighting state is outside the initial opaque one-light profile".to_string());
    }
    let matrices_finite = state.model_view.iter().chain(state.model_view_projection.iter())
        .chain(state.normal_matrix.iter()).all(|v| v.is_finite());
    let light = &state.lights[0];
    if !matrices_finite || !light.position_type.iter().all(|v| v.is_finite()) ||
        !light.direction_cutoff.iter().all(|v| v.is_finite()) ||
        !light.color_intensity.iter().all(|v| v.is_finite()) ||
        !material.diffuse.iter().chain(material.specular.iter()).all(|v| v.is_finite()) ||
        !material.shininess.is_finite() || !(0.0..=1.0).contains(&material.shininess) {
        return Err("Nonfinite or invalid bump lighting state".to_string());
    }
    // normal_matrix is inverse(model_view linear)^T. Its transpose converts a
    // view-space direction back to the object's S/T/N input space.
    let inverse_direction = |v: [f32; 3]| -> [f32; 3] {
        let n = &state.normal_matrix;
        [n[0]*v[0] + n[1]*v[1] + n[2]*v[2],
         n[4]*v[0] + n[5]*v[1] + n[6]*v[2],
         n[8]*v[0] + n[9]*v[1] + n[10]*v[2]]
    };
    let translation = [state.model_view[12], state.model_view[13], state.model_view[14]];
    let view_light = [light.position_type[0], light.position_type[1],
                      light.position_type[2]];
    let object_light = if light.position_type[3] == 0.0 {
        inverse_direction([-light.direction_cutoff[0], -light.direction_cutoff[1],
                           -light.direction_cutoff[2]])
    } else {
        inverse_direction([view_light[0] - translation[0],
                           view_light[1] - translation[1],
                           view_light[2] - translation[2]])
    };
    let object_eye = inverse_direction([-translation[0], -translation[1], -translation[2]]);
    if !object_light.iter().chain(object_eye.iter()).all(|v| v.is_finite()) {
        return Err("Bump object-space light or eye overflows".to_string());
    }
    let intensity = light.color_intensity[3];
    if intensity < 0.0 {
        return Err("Negative bump light intensity".to_string());
    }
    let mut diffuse = [0.0; 4];
    let mut specular = material.specular;
    for axis in 0..3 {
        diffuse[axis] = material.diffuse[axis] * light.color_intensity[axis] * intensity;
    }
    diffuse[3] = 1.0;
    specular[3] = 1.0;
    if !diffuse.iter().all(|v| v.is_finite()) {
        return Err("Bump diffuse light product overflows".to_string());
    }
    Ok(BumpUniforms {
        model_view_projection: state.model_view_projection,
        light_position_type: [object_light[0], object_light[1], object_light[2],
                              light.position_type[3]],
        diffuse, eye_position: [object_eye[0], object_eye[1], object_eye[2], 1.0],
        specular, shininess: [material.shininess, 0.0, 0.0, 0.0],
    })
}

#[allow(dead_code)]
fn create_bump_pipeline(device: &wgpu::Device, color_format: wgpu::TextureFormat,
                        depth_stencil: Option<wgpu::DepthStencilState>,
                        fragment_entry: &str,
                        blend: Option<wgpu::BlendState>) -> wgpu::RenderPipeline {
    create_bump_pipeline_samples(device, color_format, depth_stencil,
        fragment_entry, blend, 1, wgpu::ColorWrites::ALL)
}

fn create_bump_pipeline_samples(device: &wgpu::Device, color_format: wgpu::TextureFormat,
                                depth_stencil: Option<wgpu::DepthStencilState>,
                                fragment_entry: &str, blend: Option<wgpu::BlendState>,
                                sample_count: u32, color_writes: wgpu::ColorWrites) -> wgpu::RenderPipeline {
    let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("coin_bump.wgsl"),
        source: wgpu::ShaderSource::Wgsl(include_str!("../../shaders/coin_bump.wgsl").into()),
    });
    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("Coin bump lighting pipeline"), layout: None,
        vertex: wgpu::VertexState {
            module: &shader, entry_point: Some("vs_main"),
            compilation_options: Default::default(),
            buffers: &[
                wgpu::VertexBufferLayout {
                    array_stride: std::mem::size_of::<CoinWgpuVertex>() as u64,
                    step_mode: wgpu::VertexStepMode::Vertex,
                    attributes: &wgpu::vertex_attr_array![0 => Float32x3, 1 => Float32x3],
                },
                wgpu::VertexBufferLayout {
                    array_stride: std::mem::size_of::<CoinWgpuBumpVertex>() as u64,
                    step_mode: wgpu::VertexStepMode::Vertex,
                    attributes: &wgpu::vertex_attr_array![2 => Float32x4, 3 => Float32x3,
                        4 => Float32x3],
                },
            ],
        },
        fragment: Some(wgpu::FragmentState {
            module: &shader, entry_point: Some(fragment_entry),
            compilation_options: Default::default(),
            targets: &[Some(wgpu::ColorTargetState {
                format: color_format,
                blend, write_mask: color_writes,
            })],
        }),
        primitive: wgpu::PrimitiveState { cull_mode: None, ..Default::default() },
        depth_stencil,
        multisample: wgpu::MultisampleState { count: sample_count, ..Default::default() },
        multiview: None, cache: None,
    })
}

fn create_bump_base_pipeline(device: &wgpu::Device, color_format: wgpu::TextureFormat,
                             depth_stencil: Option<wgpu::DepthStencilState>) -> wgpu::RenderPipeline {
    create_bump_base_pipeline_samples(device, color_format, depth_stencil, 1, wgpu::ColorWrites::ALL)
}

fn create_bump_base_pipeline_samples(device: &wgpu::Device, color_format: wgpu::TextureFormat,
                                     depth_stencil: Option<wgpu::DepthStencilState>,
                                     sample_count: u32, color_writes: wgpu::ColorWrites) -> wgpu::RenderPipeline {
    let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("coin_bump.wgsl base texture"),
        source: wgpu::ShaderSource::Wgsl(include_str!("../../shaders/coin_bump.wgsl").into()),
    });
    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("Coin bump base texture pipeline"), layout: None,
        vertex: wgpu::VertexState {
            module: &shader, entry_point: Some("vs_base"),
            compilation_options: Default::default(),
            buffers: &[
                wgpu::VertexBufferLayout {
                    array_stride: std::mem::size_of::<CoinWgpuVertex>() as u64,
                    step_mode: wgpu::VertexStepMode::Vertex,
                    attributes: &[
                        wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x3,
                            offset: 0, shader_location: 0 },
                        wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x3,
                            offset: 12, shader_location: 1 },
                        wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x4,
                            offset: 24, shader_location: 5 },
                    ],
                },
                wgpu::VertexBufferLayout {
                    array_stride: std::mem::size_of::<CoinWgpuBumpVertex>() as u64,
                    step_mode: wgpu::VertexStepMode::Vertex,
                    attributes: &wgpu::vertex_attr_array![2 => Float32x4, 3 => Float32x3,
                        4 => Float32x3],
                },
            ],
        },
        fragment: Some(wgpu::FragmentState {
            module: &shader, entry_point: Some("fs_base"),
            compilation_options: Default::default(),
            targets: &[Some(wgpu::ColorTargetState {
                format: color_format, blend: None, write_mask: color_writes,
            })],
        }),
        primitive: wgpu::PrimitiveState { cull_mode: None, ..Default::default() },
        depth_stencil,
        multisample: wgpu::MultisampleState { count: sample_count, ..Default::default() },
        multiview: None, cache: None,
    })
}

#[derive(Hash, PartialEq, Eq)]
struct BumpPipelineKey {
    color_format: wgpu::TextureFormat,
    depth_compare: wgpu::CompareFunction,
    sample_count: u32,
    color_writes: u32,
}

struct BumpPipelines {
    diffuse: wgpu::RenderPipeline,
    specular: wgpu::RenderPipeline,
    base: wgpu::RenderPipeline,
}

fn create_bump_pipelines(device: &wgpu::Device, color_format: wgpu::TextureFormat,
                         depth_compare: wgpu::CompareFunction,
                         sample_count: u32, color_writes: wgpu::ColorWrites) -> BumpPipelines {
    let depth_write = Some(wgpu::DepthStencilState {
        format: wgpu::TextureFormat::Depth32Float, depth_write_enabled: true,
        depth_compare, stencil: Default::default(), bias: Default::default(),
    });
    let depth_read = Some(wgpu::DepthStencilState {
        format: wgpu::TextureFormat::Depth32Float, depth_write_enabled: false,
        depth_compare: wgpu::CompareFunction::LessEqual,
        stencil: Default::default(), bias: Default::default(),
    });
    BumpPipelines {
        base: create_bump_base_pipeline_samples(device, color_format, depth_write.clone(), sample_count, color_writes),
        diffuse: create_bump_pipeline_samples(device, color_format, depth_write, "fs_main", None,
            sample_count, color_writes),
        specular: create_bump_pipeline_samples(device, color_format, depth_read, "fs_specular",
            Some(wgpu::BlendState {
                color: wgpu::BlendComponent { src_factor: wgpu::BlendFactor::One,
                    dst_factor: wgpu::BlendFactor::One, operation: wgpu::BlendOperation::Add },
                alpha: wgpu::BlendComponent { src_factor: wgpu::BlendFactor::One,
                    dst_factor: wgpu::BlendFactor::One, operation: wgpu::BlendOperation::Add },
            }), sample_count, color_writes),
    }
}

// Dedicated encoder for the first opaque bump profile. It is deliberately
// separate from the standard shader: that shader has no free interpolators
// for the captured S/T basis. Callers must preflight every draw before this
// function changes a target.
fn encode_bump_opaque_frame(
    device: &wgpu::Device, queue: &wgpu::Queue,
    pipeline_cache: Option<&Mutex<HashMap<BumpPipelineKey, Arc<BumpPipelines>>>>,
    base_cache: Option<(&Mutex<BumpBaseCache>, u64, u64)>,
    vertices: &[CoinWgpuVertex], indices: &[u32], draws: &[CoinWgpuDraw],
    states: &[CoinWgpuRenderState], materials: &[CoinWgpuMaterial],
    textures: &[CoinWgpuTexture], samplers: &[CoinWgpuSampler],
    payload: &ValidatedBumpPayload, color_view: &wgpu::TextureView,
    color_format: wgpu::TextureFormat, depth_view: &wgpu::TextureView,
    target_size: [u32; 2], clear_color: [f32; 4],
) -> Result<wgpu::CommandBuffer, (CoinWgpuStatus, String)> {
    encode_bump_opaque_frame_samples(device, queue, pipeline_cache, base_cache,
        vertices, indices, draws, states, materials, textures, samplers, payload,
        color_view, None, color_format, depth_view, target_size, clear_color, 1)
}

fn validate_bump_draw_profile(
    device: &wgpu::Device, vertices: &[CoinWgpuVertex], indices: &[u32],
    draws: &[CoinWgpuDraw], states: &[CoinWgpuRenderState], materials: &[CoinWgpuMaterial],
    textures: &[CoinWgpuTexture], samplers: &[CoinWgpuSampler], target_size: [u32; 2],
    consumed: &[bool],
) -> Result<(Vec<BumpUniforms>, Option<u32>), (CoinWgpuStatus, String)> {
    let unsupported = |msg: &str| (CoinWgpuStatus::Unsupported, msg.to_string());
    let mut uniforms = Vec::with_capacity(draws.len());
    let mut depth_function = None;
    if consumed.len() != draws.len() {
        return Err((CoinWgpuStatus::InvalidArgument, "Invalid bump occurrence count".into()));
    }
    for (draw, active) in draws.iter().zip(consumed) {
        let st = states.get(draw.render_state_slot as usize)
            .ok_or((CoinWgpuStatus::InvalidArgument, "Bump state slot out of bounds".into()))?;
        let material = materials.get(st.material_slot as usize)
            .ok_or((CoinWgpuStatus::InvalidArgument, "Bump material slot out of bounds".into()))?;
        if st.extra_textures.iter().any(|unit| unit.enabled != 0) ||
            st.fog_mode != 0 || st.depth_test != 1 || st.depth_write != 1 ||
            !matches!(st.depth_function, 2 | 3) || st.cull_mode != 0 ||
            (st.viewport != [0,0,0,0] &&
             st.viewport != [0,0,target_size[0] as i32,target_size[1] as i32]) ||
            draw.composition_flags != 0 ||
            draw.clear_depth_before != 0 {
            return Err(unsupported(&format!("Bump frame needs opaque, full-target depth rendering (fog={}, depth={}/{}/{}, cull={}, viewport={:?}, composition={}, clear_depth={})",
                st.fog_mode, st.depth_test, st.depth_write, st.depth_function,
                st.cull_mode, st.viewport, draw.composition_flags, draw.clear_depth_before)));
        }
        if st.has_texture != 0 {
            if st.texture_model != 0 || st.texture_projection != 0 {
                return Err(unsupported("Bump base texture requires projective MODULATE"));
            }
            let texture = textures.get(st.texture_slot as usize)
                .ok_or((CoinWgpuStatus::InvalidArgument, "Bump base texture slot out of bounds".into()))?;
            let sampler = samplers.get(st.sampler_slot as usize)
                .ok_or((CoinWgpuStatus::InvalidArgument, "Bump base sampler slot out of bounds".into()))?;
            let levels = texture_levels(texture.width, texture.height, texture.format)?;
            let byte_count = levels.last().and_then(|&(width, height, offset)|
                u64::from(width).checked_mul(u64::from(height))
                    .and_then(|pixels| pixels.checked_mul(4))
                    .and_then(|bytes| offset.checked_add(bytes)));
            if !matches!(texture.format, 0 | 2 | 3 | 4) || texture.width == 0 || texture.height == 0 ||
                texture.width > device.limits().max_texture_dimension_2d ||
                texture.height > device.limits().max_texture_dimension_2d ||
                byte_count != Some(texture.pixel_bytes_len) ||
                texture.pixel_bytes_len > 48 * 1024 * 1024 || texture.pixels.is_null() ||
                sampler.wrap_s > 1 || sampler.wrap_t > 1 || sampler.filter > 3 ||
                (sampler.filter >= 2 && !matches!(texture.format, 2 | 4)) ||
                bump_base_anisotropy(sampler).is_err() ||
                !st.texture_matrix.iter().all(|v| v.is_finite()) {
                return Err(unsupported(&format!("Bump base image or sampler is outside opaque RGBA8 profile (format={}, size={}x{}, bytes={}, wraps={}/{}, filter={}, anisotropy={})",
                    texture.format, texture.width, texture.height, texture.pixel_bytes_len,
                    sampler.wrap_s, sampler.wrap_t, sampler.filter, sampler.reserved)));
            }
            let pixels = unsafe { std::slice::from_raw_parts(texture.pixels,
                texture.pixel_bytes_len as usize) };
            if pixels.chunks_exact(4).any(|rgba| rgba[3] != 255) {
                return Err(unsupported("Bump base image alpha needs transparency composition"));
            }
            let start = draw.first_index as usize;
            let end = start + draw.index_count as usize;
            validate_texture_coordinates_with_images(st, draw.topology, vertices, &indices[start..end], textures)
                .map_err(|e| (CoinWgpuStatus::InvalidArgument, e))?;
        }
        if depth_function.is_some_and(|function| function != st.depth_function) {
            return Err(unsupported("Bump draws with different depth functions need separate pipelines"));
        }
        depth_function = Some(st.depth_function);
        if *active {
            if st.light_model != 1 { return Err(unsupported("Consumed bump requires PHONG")); }
            uniforms.push(bump_uniforms_from_state(st, material)
                .map_err(|e| (CoinWgpuStatus::Unsupported, e))?);
        }
    }
    Ok((uniforms, depth_function))
}

fn encode_bump_opaque_frame_samples(
    device: &wgpu::Device, queue: &wgpu::Queue,
    pipeline_cache: Option<&Mutex<HashMap<BumpPipelineKey, Arc<BumpPipelines>>>>,
    base_cache: Option<(&Mutex<BumpBaseCache>, u64, u64)>,
    vertices: &[CoinWgpuVertex], indices: &[u32], draws: &[CoinWgpuDraw],
    states: &[CoinWgpuRenderState], materials: &[CoinWgpuMaterial],
    textures: &[CoinWgpuTexture], samplers: &[CoinWgpuSampler],
    payload: &ValidatedBumpPayload, color_view: &wgpu::TextureView,
    resolve_view: Option<&wgpu::TextureView>, color_format: wgpu::TextureFormat,
    depth_view: &wgpu::TextureView, target_size: [u32; 2], clear_color: [f32; 4],
    sample_count: u32,
) -> Result<wgpu::CommandBuffer, (CoinWgpuStatus, String)> {
    if resolve_view.is_some() != (sample_count > 1) {
        return Err((CoinWgpuStatus::InvalidArgument,
            "Bump MSAA attachments require matching sample count and color resolve".into()));
    }
    encode_bump_opaque_run(device, queue, pipeline_cache, base_cache, vertices,
        indices, draws, states, materials, textures, samplers, payload, color_view,
        resolve_view, color_format, depth_view, target_size, clear_color, sample_count, true)
}

fn encode_bump_opaque_run(
    device: &wgpu::Device, queue: &wgpu::Queue,
    pipeline_cache: Option<&Mutex<HashMap<BumpPipelineKey, Arc<BumpPipelines>>>>,
    base_cache: Option<(&Mutex<BumpBaseCache>, u64, u64)>,
    vertices: &[CoinWgpuVertex], indices: &[u32], draws: &[CoinWgpuDraw],
    states: &[CoinWgpuRenderState], materials: &[CoinWgpuMaterial],
    textures: &[CoinWgpuTexture], samplers: &[CoinWgpuSampler],
    payload: &ValidatedBumpPayload, color_view: &wgpu::TextureView,
    resolve_view: Option<&wgpu::TextureView>, color_format: wgpu::TextureFormat,
    depth_view: &wgpu::TextureView, target_size: [u32; 2], clear_color: [f32; 4],
    sample_count: u32, clear_attachments: bool,
) -> Result<wgpu::CommandBuffer, (CoinWgpuStatus, String)> {
    use wgpu::util::DeviceExt;
    let unsupported = |msg: &str| (CoinWgpuStatus::Unsupported, msg.to_string());
    if !matches!(sample_count, 1 | 2 | 4) || (resolve_view.is_some() && sample_count == 1) {
        return Err((CoinWgpuStatus::InvalidArgument,
            "Bump MSAA attachments require matching sample count and color resolve".into()));
    }
    if draws.is_empty() || payload.vertices.len() != vertices.len() ||
        !clear_color.iter().all(|v| v.is_finite()) {
        return Err((CoinWgpuStatus::InvalidArgument,
            "Invalid bump frame geometry or clear color".into()));
    }
    let slots = bump_image_slots_for_draws(payload, indices, draws)
        .map_err(|e| (CoinWgpuStatus::InvalidArgument, e))?;
    if slots.iter().any(Option::is_none) {
        return Err(unsupported("Mixed bump and non-bump draws need the normal base pass"));
    }
    let (uniforms, depth_function) = validate_bump_draw_profile(device, vertices, indices,
        draws, states, materials, textures, samplers, target_size,
        &slots.iter().map(Option::is_some).collect::<Vec<_>>())?;
    let gpu = upload_bump_payload(device, queue, payload)
        .map_err(|e| (CoinWgpuStatus::Unsupported, e))?;
    let main_vertices = multipass_upload_buffer(device, &wgpu::util::BufferInitDescriptor {
        label: Some("Coin bump positions/normals"),
        contents: bytemuck::cast_slice(vertices), usage: wgpu::BufferUsages::VERTEX,
    });
    let index_buffer = multipass_upload_buffer(device, &wgpu::util::BufferInitDescriptor {
        label: Some("Coin bump indices"),
        contents: bytemuck::cast_slice(indices), usage: wgpu::BufferUsages::INDEX,
    });
    let depth_compare = if depth_function == Some(2) {
        wgpu::CompareFunction::Less
    } else { wgpu::CompareFunction::LessEqual };
    // Channel writes belong to each occurrence, including adjacent draws in
    // one bump run. Keep the matching auto-layout pipelines and bindgroups
    // together; a cached ALL pipeline cannot stand in for a partial mask.
    let mut pipelines = Vec::with_capacity(draws.len());
    let mut run_pipelines = HashMap::<u32, Arc<BumpPipelines>>::new();
    for draw in draws {
        let encoded = states[draw.render_state_slot as usize].color_write;
        let color_writes = if encoded == 1 { wgpu::ColorWrites::ALL }
            else { wgpu::ColorWrites::from_bits_truncate(encoded & 0x0f) };
        if let Some(pipeline) = run_pipelines.get(&color_writes.bits()) {
            pipelines.push(pipeline.clone());
            continue;
        }
        let key = BumpPipelineKey { color_format, depth_compare, sample_count,
            color_writes: color_writes.bits() };
        let pipeline = if let Some(cache) = pipeline_cache {
            let mut cache = cache.lock().map_err(|e|
                (CoinWgpuStatus::BackendError, format!("Bump pipeline cache lock failed: {e}")))?;
            cache.entry(key).or_insert_with(|| Arc::new(create_bump_pipelines(
                device, color_format, depth_compare, sample_count, color_writes))).clone()
        } else { Arc::new(create_bump_pipelines(device, color_format, depth_compare,
                                               sample_count, color_writes)) };
        run_pipelines.insert(color_writes.bits(), pipeline.clone());
        pipelines.push(pipeline);
    }
    let mut uniform_buffers = Vec::with_capacity(uniforms.len());
    for uniform in &uniforms {
        uniform_buffers.push(multipass_upload_buffer(device, &wgpu::util::BufferInitDescriptor {
            label: Some("Coin bump uniforms"), contents: bytemuck::bytes_of(uniform),
            usage: wgpu::BufferUsages::UNIFORM,
        }));
    }
    let make_binds = |specular: bool| {
        draws.iter().enumerate().map(|(i, _)| {
            let pipeline = if specular { &pipelines[i].specular } else { &pipelines[i].diffuse };
            let image = &gpu.images[slots[i].unwrap()];
            device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: Some("Coin bump draw image"), layout: &pipeline.get_bind_group_layout(0),
                entries: &[
                    wgpu::BindGroupEntry { binding: 0,
                        resource: uniform_buffers[i].as_entire_binding() },
                    wgpu::BindGroupEntry { binding: 1,
                        resource: wgpu::BindingResource::TextureView(&image.view) },
                    wgpu::BindGroupEntry { binding: 2,
                        resource: wgpu::BindingResource::Sampler(&image.sampler) },
                ],
            })
        }).collect::<Vec<_>>()
    };
    let diffuse_binds = make_binds(false);
    let specular_binds = make_binds(true);
    let mut base_resources = Vec::with_capacity(draws.len());
    let mut base_images = Vec::<GpuBumpBaseImage>::new();
    let mut image_slots = HashMap::<(u32,u32),usize>::new();
    for draw in draws {
        let st = &states[draw.render_state_slot as usize];
        if st.has_texture == 0 { base_resources.push(None); continue; }
        let key = (st.texture_slot, st.sampler_slot);
        let image_index = if let Some(&index) = image_slots.get(&key) { index } else {
            let source = &textures[st.texture_slot as usize];
            let settings = &samplers[st.sampler_slot as usize];
            let pixels = unsafe { std::slice::from_raw_parts(source.pixels, source.pixel_bytes_len as usize) };
            let image = if let Some((cache, serial, completed)) = base_cache {
                let mut cache = cache.lock().map_err(|e|
                    (CoinWgpuStatus::BackendError, format!("Bump base cache lock failed: {e}")))?;
                cache.retired.retain(|(_, retired_at)| *retired_at > completed);
                let cache_key = BumpBaseKey {
                    width: source.width, height: source.height, format: source.format,
                    digest: source.content_digest, wrap_s: settings.wrap_s,
                    wrap_t: settings.wrap_t, filter: settings.filter, anisotropy: settings.reserved & 15,
                };
                let hit = cache.entries.get_mut(&cache_key).and_then(|entry| {
                    if entry.pixels_copy == pixels {
                        entry.last_used_serial = serial;
                        Some(entry.image.clone())
                    } else { None }
                });
                if let Some(image) = hit {
                    cache.hits += 1;
                    image
                } else {
                    let image = upload_bump_base_image(device, queue, source, settings)?;
                    let old = cache.entries.insert(cache_key, BumpBaseCacheEntry {
                        image: image.clone(), pixels_copy: pixels.to_vec(),
                        last_used_serial: serial,
                    });
                    if let Some(old) = old {
                        cache.retired.push((old.image, old.last_used_serial));
                    }
                    cache.uploads += 1;
                    image
                }
            } else { upload_bump_base_image(device, queue, source, settings)? };
            let index = base_images.len();
            base_images.push(image);
            image_slots.insert(key, index);
            index
        };
        let uniform = multipass_upload_buffer(device, &wgpu::util::BufferInitDescriptor {
            label: Some("Coin bump base transform"),
            contents: bytemuck::bytes_of(&BumpBaseUniforms { texture_matrix: st.texture_matrix }),
            usage: wgpu::BufferUsages::UNIFORM,
        });
        base_resources.push(Some(GpuBumpBaseDraw { image_index, uniform }));
    }
    if let Some((cache, serial, _)) = base_cache {
        let mut cache = cache.lock().map_err(|e|
            (CoinWgpuStatus::BackendError, format!("Bump base cache lock failed: {e}")))?;
        let stale: Vec<_> = cache.entries.iter().filter(|(_, entry)|
            serial.saturating_sub(entry.last_used_serial) > 8)
            .map(|(key, _)| key.clone()).collect();
        for key in stale {
            if let Some(entry) = cache.entries.remove(&key) {
                cache.retired.push((entry.image, entry.last_used_serial));
            }
        }
    }
    #[cfg(test)]
    LAST_BUMP_BASE_IMAGE_UPLOADS.with(|count| count.set(base_images.len()));
    let base_binds = draws.iter().enumerate().map(|(i, _)| {
        base_resources[i].as_ref().map(|resource| {
            let base_pipeline = &pipelines[i].base;
            let image = &gpu.images[slots[i].unwrap()];
            let base_image = &base_images[resource.image_index];
            let group0 = device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: Some("Coin bump textured draw"), layout: &base_pipeline.get_bind_group_layout(0),
                entries: &[
                    wgpu::BindGroupEntry { binding: 0, resource: uniform_buffers[i].as_entire_binding() },
                    wgpu::BindGroupEntry { binding: 1, resource: wgpu::BindingResource::TextureView(&image.view) },
                    wgpu::BindGroupEntry { binding: 2, resource: wgpu::BindingResource::Sampler(&image.sampler) },
                ],
            });
            let group1 = device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: Some("Coin bump base draw"), layout: &base_pipeline.get_bind_group_layout(1),
                entries: &[
                    wgpu::BindGroupEntry { binding: 0, resource: resource.uniform.as_entire_binding() },
                    wgpu::BindGroupEntry { binding: 1, resource: wgpu::BindingResource::TextureView(&base_image.view) },
                    wgpu::BindGroupEntry { binding: 2, resource: wgpu::BindingResource::Sampler(&base_image.sampler) },
                ],
            });
            (group0, group1)
        })
    }).collect::<Vec<_>>();
    let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
        label: Some("Coin opaque bump frame"),
    });
    {
        let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("Coin opaque bump draws"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view: color_view, resolve_target: resolve_view,
                ops: wgpu::Operations { load: if clear_attachments { wgpu::LoadOp::Clear(wgpu::Color {
                    r: clear_color[0] as f64, g: clear_color[1] as f64,
                    b: clear_color[2] as f64, a: clear_color[3] as f64 }) } else { wgpu::LoadOp::Load },
                    store: wgpu::StoreOp::Store },
            })],
            depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                view: depth_view, depth_ops: Some(wgpu::Operations {
                    load: if clear_attachments { wgpu::LoadOp::Clear(1.0) } else { wgpu::LoadOp::Load }, store: wgpu::StoreOp::Store }),
                stencil_ops: None,
            }), timestamp_writes: None, occlusion_query_set: None,
        });
        pass.set_vertex_buffer(0, main_vertices.slice(..));
        pass.set_vertex_buffer(1, gpu.vertices.slice(..));
        pass.set_index_buffer(index_buffer.slice(..), wgpu::IndexFormat::Uint32);
        for (i, draw) in draws.iter().enumerate() {
            let range = draw.first_index..draw.first_index + draw.index_count;
            if let Some((group0, group1)) = &base_binds[i] {
                pass.set_pipeline(&pipelines[i].base);
                pass.set_bind_group(0, group0, &[]);
                pass.set_bind_group(1, group1, &[]);
            } else {
                pass.set_pipeline(&pipelines[i].diffuse);
                pass.set_bind_group(0, &diffuse_binds[i], &[]);
            }
            pass.draw_indexed(range.clone(), 0, 0..1);
            pass.set_pipeline(&pipelines[i].specular);
            pass.set_bind_group(0, &specular_binds[i], &[]);
            pass.draw_indexed(range, 0, 0..1);
        }
    }
    Ok(encoder.finish())
}

fn validate_bump_payload(
    frame: &CoinWgpuFrameView,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> Result<Option<ValidatedBumpPayload>, CoinWgpuStatus> {
    if !bump_payload_present(frame) { return Ok(None); }
    let invalid = |message: &str| {
        set_error(error_buf, error_buf_len, message);
        CoinWgpuStatus::InvalidArgument
    };
    if frame.bump_vertex_count == 0 || frame.bump_image_count == 0 ||
        frame.bump_vertex_count != frame.vertex_count || frame.bump_image_count > 4096 {
        return Err(invalid("Bump payload count does not match emitted geometry or images"));
    }
    let vertices = validate_slice(frame.bump_vertices, frame.bump_vertex_count,
        "bump vertices", error_buf, error_buf_len)?;
    let images = validate_slice(frame.bump_images, frame.bump_image_count,
        "bump images", error_buf, error_buf_len)?;
    if vertices.iter().any(|v| v.image_slot > frame.bump_image_count as u32 ||
        v.coord.iter().chain(v.tangent_s.iter()).chain(v.tangent_t.iter())
            .any(|x| !x.is_finite()) || (v.image_slot != 0 && v.coord[3] == 0.0)) {
        return Err(invalid("Invalid bump vertex slot, coordinate or tangent"));
    }
    let mut total_bytes = 0u64;
    let mut owned_images = Vec::with_capacity(images.len());
    for image in images {
        let expected = u64::from(image.width).checked_mul(u64::from(image.height))
            .and_then(|pixels| pixels.checked_mul(3));
        if image.width == 0 || image.height == 0 || image.width > 32767 ||
            image.height > 32767 || image.clamp_s > 1 || image.clamp_t > 1 ||
            expected != Some(image.rgb_len) || image.rgb_len > 48 * 1024 * 1024 ||
            image.rgb.is_null() {
            return Err(invalid("Invalid bump image dimensions, flags or bytes"));
        }
        total_bytes = total_bytes.checked_add(image.rgb_len)
            .ok_or_else(|| invalid("Bump image byte count overflow"))?;
        if total_bytes > 256 * 1024 * 1024 {
            return Err(invalid("Bump images exceed the transport budget"));
        }
        let bytes = validate_slice(image.rgb, image.rgb_len,
            "bump RGB bytes", error_buf, error_buf_len)?;
        owned_images.push(ValidatedBumpImage {
            width: image.width, height: image.height,
            clamp_s: image.clamp_s != 0, clamp_t: image.clamp_t != 0,
            rgb: bytes.to_vec(),
        });
    }
    Ok(Some(ValidatedBumpPayload { vertices: vertices.to_vec(), images: owned_images }))
}

// The bump image is bound per draw, while its slot travels per vertex in the
// captured plan. Resolve that boundary before encoding; interpolating slots
// across a primitive would silently sample the wrong normal image.
fn bump_image_slots_for_draws(payload: &ValidatedBumpPayload, indices: &[u32],
                              draws: &[CoinWgpuDraw]) -> Result<Vec<Option<usize>>, String> {
    let mut slots = Vec::with_capacity(draws.len());
    for (draw_number, draw) in draws.iter().enumerate() {
        if draw.topology != 0 || draw.index_count == 0 || draw.index_count % 3 != 0 {
            return Err(format!("Bump draw {draw_number} requires indexed triangles"));
        }
        let start = draw.first_index as usize;
        let end = start.checked_add(draw.index_count as usize)
            .ok_or_else(|| format!("Bump draw {draw_number} index range overflows"))?;
        let draw_indices = indices.get(start..end)
            .ok_or_else(|| format!("Bump draw {draw_number} index range is out of bounds"))?;
        let first = payload.vertices.get(draw_indices[0] as usize)
            .ok_or_else(|| format!("Bump draw {draw_number} vertex is out of bounds"))?
            .image_slot;
        if first as usize > payload.images.len() {
            return Err(format!("Bump draw {draw_number} image slot is out of bounds"));
        }
        for &index in draw_indices {
            let vertex = payload.vertices.get(index as usize)
                .ok_or_else(|| format!("Bump draw {draw_number} vertex is out of bounds"))?;
            if vertex.image_slot != first {
                return Err(format!("Bump draw {draw_number} mixes normal image slots"));
            }
        }
        slots.push(if first == 0 { None } else { Some(first as usize - 1) });
    }
    Ok(slots)
}

#[cfg(test)]
mod bump_abi_tests {
    use super::*;

    #[test]
    fn bump_draws_route_one_image_per_indexed_draw() {
        let vertex = |slot| CoinWgpuBumpVertex { image_slot: slot,
            coord: [0.0,0.0,0.0,1.0], tangent_s: [1.0,0.0,0.0],
            tangent_t: [0.0,1.0,0.0] };
        let payload = ValidatedBumpPayload {
            vertices: vec![vertex(1), vertex(1), vertex(1),
                           vertex(2), vertex(2), vertex(2),
                           vertex(0), vertex(0), vertex(0)],
            images: (0..2).map(|_| ValidatedBumpImage { width: 1, height: 1,
                clamp_s: false, clamp_t: false, rgb: vec![128,128,255] }).collect(),
        };
        let mut draws = [CoinWgpuDraw::zeroed(); 3];
        for (i, draw) in draws.iter_mut().enumerate() {
            draw.first_index = (i * 3) as u32;
            draw.index_count = 3;
        }
        let indices: Vec<u32> = (0..9).collect();
        assert_eq!(bump_image_slots_for_draws(&payload, &indices, &draws).unwrap(),
                   [Some(0), Some(1), None]);
        let mut mixed = indices.clone();
        mixed[2] = 3;
        assert!(bump_image_slots_for_draws(&payload, &mixed, &draws).is_err());
        draws[0].first_index = 8;
        assert!(bump_image_slots_for_draws(&payload, &indices, &draws).is_err());
    }

    #[test]
    fn bump_lighting_state_converts_view_to_object_space() {
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        state.model_view = [2.0,0.0,0.0,0.0, 0.0,3.0,0.0,0.0,
                            0.0,0.0,4.0,0.0, 10.0,20.0,30.0,1.0];
        state.normal_matrix = [0.5,0.0,0.0,0.0, 0.0,1.0/3.0,0.0,0.0,
                               0.0,0.0,0.25,0.0, 0.0,0.0,0.0,1.0];
        state.model_view_projection[15] = 1.0;
        state.light_count = 1;
        state.lights[0].position_type = [12.0,23.0,34.0,1.0];
        state.lights[0].color_intensity = [1.0,1.0,1.0,1.0];
        let mut material = CoinWgpuMaterial::zeroed();
        material.diffuse = [0.8,0.2,0.1,1.0];
        material.specular = [0.5,0.25,0.125,1.0];
        material.shininess = 0.5;
        let uniforms = bump_uniforms_from_state(&state, &material).unwrap();
        assert_eq!(uniforms.light_position_type, [1.0,1.0,1.0,1.0]);
        assert!((uniforms.eye_position[0] + 5.0).abs() < 1e-6);
        assert!((uniforms.eye_position[1] + 20.0/3.0).abs() < 1e-6);
        assert!((uniforms.eye_position[2] + 7.5).abs() < 1e-6);
        assert_eq!(uniforms.diffuse, [0.8,0.2,0.1,1.0]);
        state.lights[0].position_type = [0.0,0.0,0.0,0.0];
        state.lights[0].direction_cutoff = [0.0,0.0,-4.0,0.0];
        assert_eq!(bump_uniforms_from_state(&state, &material).unwrap()
                   .light_position_type, [0.0,0.0,1.0,0.0]);
        state.lights[0].position_type[3] = 0.5;
        assert!(bump_uniforms_from_state(&state, &material).is_err());
        state.lights[0].position_type[3] = 1.0;
        state.lights[0].color_intensity[3] = -1.0;
        assert!(bump_uniforms_from_state(&state, &material).is_err());
    }

    #[test]
    fn optional_bump_payload_cannot_be_silently_ignored() {
        let mut frame: CoinWgpuFrameView = unsafe { std::mem::zeroed() };
        assert!(!bump_payload_present(&frame));
        frame.bump_vertex_count = 1;
        assert!(bump_payload_present(&frame));
        frame.bump_vertex_count = 0;
        let vertex = CoinWgpuBumpVertex::zeroed();
        frame.bump_vertices = &vertex;
        assert!(bump_payload_present(&frame));
        frame.bump_vertices = std::ptr::null();
        frame.bump_image_count = 1;
        assert!(bump_payload_present(&frame));
    }

    #[test]
    fn bump_payload_validates_and_owns_image_bytes() {
        let mut frame: CoinWgpuFrameView = unsafe { std::mem::zeroed() };
        let vertex = CoinWgpuBumpVertex {
            coord: [0.25, 0.5, 0.0, 1.0], tangent_s: [1.0, 0.0, 0.0],
            tangent_t: [0.0, 1.0, 0.0], image_slot: 1,
        };
        let mut rgb = [128u8, 128, 255];
        let mut image = CoinWgpuBumpImage {
            width: 1, height: 1, clamp_s: 0, clamp_t: 1,
            rgb: rgb.as_ptr(), rgb_len: 3,
        };
        frame.vertex_count = 1;
        frame.bump_vertices = &vertex;
        frame.bump_vertex_count = 1;
        frame.bump_images = &image;
        frame.bump_image_count = 1;
        let owned = validate_bump_payload(&frame, std::ptr::null_mut(), 0)
            .unwrap().unwrap();
        rgb[0] = 0;
        assert_eq!(owned.vertices[0].image_slot, 1);
        assert_eq!(owned.images[0].rgb, [128, 128, 255]);
        assert!(owned.images[0].clamp_t);
        image.rgb_len = 2;
        frame.bump_images = &image;
        assert!(matches!(validate_bump_payload(&frame, std::ptr::null_mut(), 0),
            Err(CoinWgpuStatus::InvalidArgument)));
        image.rgb_len = 3;
        frame.bump_vertex_count = 2;
        assert!(matches!(validate_bump_payload(&frame, std::ptr::null_mut(), 0),
            Err(CoinWgpuStatus::InvalidArgument)));
        frame.bump_vertex_count = 1;
        let bad_slot = CoinWgpuBumpVertex { image_slot: 2, ..vertex };
        frame.bump_vertices = &bad_slot;
        assert!(matches!(validate_bump_payload(&frame, std::ptr::null_mut(), 0),
            Err(CoinWgpuStatus::InvalidArgument)));
        let bad_coord = CoinWgpuBumpVertex { coord: [0.0, 0.0, 0.0, 0.0], ..vertex };
        frame.bump_vertices = &bad_coord;
        assert!(matches!(validate_bump_payload(&frame, std::ptr::null_mut(), 0),
            Err(CoinWgpuStatus::InvalidArgument)));
        frame.bump_vertices = &vertex;
        image.clamp_s = 2;
        assert!(matches!(validate_bump_payload(&frame, std::ptr::null_mut(), 0),
            Err(CoinWgpuStatus::InvalidArgument)));
        image.clamp_s = 0;
        frame.bump_images = std::ptr::null();
        assert!(matches!(validate_bump_payload(&frame, std::ptr::null_mut(), 0),
            Err(CoinWgpuStatus::InvalidArgument)));
    }

    #[test]
    #[ignore = "requires a selected physical GPU; run with --ignored"]
    fn bump_payload_uploads_exact_vertices_and_rgb() {
        let mut frame: CoinWgpuFrameView = unsafe { std::mem::zeroed() };
        let vertex = CoinWgpuBumpVertex {
            coord: [0.25, 0.5, 0.0, 1.0], tangent_s: [1.0, 0.0, 0.0],
            tangent_t: [0.0, 1.0, 0.0], image_slot: 1,
        };
        let rgb = [128u8, 128, 255, 240, 128, 128];
        let image = CoinWgpuBumpImage {
            width: 2, height: 1, clamp_s: 1, clamp_t: 0,
            rgb: rgb.as_ptr(), rgb_len: rgb.len() as u64,
        };
        frame.vertex_count = 1;
        frame.bump_vertices = &vertex;
        frame.bump_vertex_count = 1;
        frame.bump_images = &image;
        frame.bump_image_count = 1;
        let owned = validate_bump_payload(&frame, std::ptr::null_mut(), 0)
            .unwrap().unwrap();
        let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
            backends: wgpu::Backends::from_env().unwrap_or(wgpu::Backends::all()),
            ..Default::default()
        });
        let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            compatible_surface: None, force_fallback_adapter: false,
        })).expect("physical bump upload adapter");
        eprintln!("bump_upload_adapter {:?}", adapter.get_info());
        let (device, queue) = pollster::block_on(adapter.request_device(
            &wgpu::DeviceDescriptor::default(), None)).expect("bump upload device");
        let oversized = ValidatedBumpPayload {
            vertices: owned.vertices.clone(),
            images: vec![ValidatedBumpImage {
                width: device.limits().max_texture_dimension_2d + 1,
                height: 1, clamp_s: false, clamp_t: false, rgb: Vec::new(),
            }],
        };
        assert!(upload_bump_payload(&device, &queue, &oversized).is_err());
        let gpu = upload_bump_payload(&device, &queue, &owned).expect("bump upload");
        let image_bytes = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump image readback"), size: 256,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let vertex_bytes = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump vertex readback"), size: 44,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor::default());
        encoder.copy_texture_to_buffer(gpu.images[0].texture.as_image_copy(),
            wgpu::TexelCopyBufferInfo { buffer: &image_bytes,
                layout: wgpu::TexelCopyBufferLayout { offset: 0,
                    bytes_per_row: Some(256), rows_per_image: Some(1) } },
            wgpu::Extent3d { width: 2, height: 1, depth_or_array_layers: 1 });
        encoder.copy_buffer_to_buffer(&gpu.vertices, 0, &vertex_bytes, 0, 44);
        queue.submit([encoder.finish()]);
        let map = |buffer: &wgpu::Buffer| {
            let (sender, receiver) = std::sync::mpsc::channel();
            buffer.slice(..).map_async(wgpu::MapMode::Read, move |result| {
                let _ = sender.send(result);
            });
            let _ = device.poll(wgpu::Maintain::Wait);
            receiver.recv_timeout(std::time::Duration::from_secs(20))
                .expect("bump upload readback callback").expect("bump upload readback");
            let bytes = buffer.slice(..).get_mapped_range().to_vec();
            buffer.unmap();
            bytes
        };
        let pixels = map(&image_bytes);
        let vertices = map(&vertex_bytes);
        eprintln!("bump_upload_pixels {:?}", &pixels[..8]);
        assert_eq!(&pixels[..8], &[128, 128, 255, 255, 240, 128, 128, 255]);
        assert_eq!(&vertices[..], bytemuck::bytes_of(&vertex));
    }

    #[test]
    #[ignore = "requires a selected physical GPU; run with --ignored"]
    fn bump_abi_payload_renders_with_dedicated_shader() {
        let bump_vertex = CoinWgpuBumpVertex {
            coord: [0.5, 0.5, 0.0, 1.0], tangent_s: [1.0, 0.0, 0.0],
            tangent_t: [0.0, 1.0, 0.0], image_slot: 1,
        };
        let bump_vertices = [bump_vertex, bump_vertex, bump_vertex,
            CoinWgpuBumpVertex { image_slot: 2, ..bump_vertex },
            CoinWgpuBumpVertex { image_slot: 2, ..bump_vertex },
            CoinWgpuBumpVertex { image_slot: 2, ..bump_vertex }];
        let rgb = [128u8, 128, 255];
        let inverted_rgb = [128u8, 128, 0];
        let images = [CoinWgpuBumpImage {
            width: 1, height: 1, clamp_s: 0, clamp_t: 0,
            rgb: rgb.as_ptr(), rgb_len: 3,
        }, CoinWgpuBumpImage {
            width: 1, height: 1, clamp_s: 0, clamp_t: 0,
            rgb: inverted_rgb.as_ptr(), rgb_len: 3,
        }];
        let mut frame: CoinWgpuFrameView = unsafe { std::mem::zeroed() };
        frame.vertex_count = 6;
        frame.bump_vertices = bump_vertices.as_ptr();
        frame.bump_vertex_count = 6;
        frame.bump_images = images.as_ptr();
        frame.bump_image_count = 2;
        let owned = validate_bump_payload(&frame, std::ptr::null_mut(), 0)
            .unwrap().unwrap();
        let indices = [0u32, 1, 2, 3, 4, 5];
        let draw = CoinWgpuDraw { topology: 0, first_index: 0, index_count: 3,
            ..CoinWgpuDraw::zeroed() };
        let inverted_draw = CoinWgpuDraw { first_index: 3, ..draw };
        let slots = bump_image_slots_for_draws(&owned, &indices, &[draw, inverted_draw])
            .expect("two image slots");
        assert_eq!(slots, [Some(0), Some(1)]);
        let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
            backends: wgpu::Backends::from_env().unwrap_or(wgpu::Backends::all()),
            ..Default::default()
        });
        let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            compatible_surface: None, force_fallback_adapter: false,
        })).expect("physical bump draw adapter");
        eprintln!("bump_abi_draw_adapter {:?}", adapter.get_info());
        let (device, queue) = pollster::block_on(adapter.request_device(
            &wgpu::DeviceDescriptor::default(), None)).expect("bump draw device");
        let gpu = upload_bump_payload(&device, &queue, &owned).expect("bump draw upload");
        let pipeline = create_bump_pipeline(&device, wgpu::TextureFormat::Rgba8Unorm,
                                            None, "fs_main", None);
        let base_pipeline = create_bump_base_pipeline(&device,
            wgpu::TextureFormat::Rgba8Unorm, None);
        let specular_pipeline = create_bump_pipeline(&device,
            wgpu::TextureFormat::Rgba8Unorm, None, "fs_specular", None);
        let additive_specular_pipeline = create_bump_pipeline(&device,
            wgpu::TextureFormat::Rgba8Unorm, None, "fs_specular",
            Some(wgpu::BlendState {
                color: wgpu::BlendComponent {
                    src_factor: wgpu::BlendFactor::One,
                    dst_factor: wgpu::BlendFactor::One,
                    operation: wgpu::BlendOperation::Add,
                },
                alpha: wgpu::BlendComponent {
                    src_factor: wgpu::BlendFactor::One,
                    dst_factor: wgpu::BlendFactor::One,
                    operation: wgpu::BlendOperation::Add,
                },
            }));
        let mut vertices = [CoinWgpuVertex::zeroed(); 6];
        vertices[0].position = [-1.0, -1.0, 0.0];
        vertices[1].position = [3.0, -1.0, 0.0];
        vertices[2].position = [-1.0, 3.0, 0.0];
        vertices[3] = vertices[0];
        vertices[4] = vertices[1];
        vertices[5] = vertices[2];
        for vertex in &mut vertices {
            vertex.normal = [0.0, 0.0, 1.0];
            vertex.texcoord = [0.5,0.5,0.0,1.0];
        }
        let positions = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump ABI main vertices"),
            size: std::mem::size_of_val(&vertices) as u64,
            usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        queue.write_buffer(&positions, 0, bytemuck::cast_slice(&vertices));
        let index_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump ABI indices"), size: std::mem::size_of_val(&indices) as u64,
            usage: wgpu::BufferUsages::INDEX | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        queue.write_buffer(&index_buffer, 0, bytemuck::cast_slice(&indices));
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        state.model_view = [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0,
                            0.0,0.0,1.0,0.0, 0.0,0.0,-5.0,1.0];
        state.model_view_projection = [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0,
                                       0.0,0.0,1.0,0.0, 0.0,0.0,0.0,1.0];
        state.normal_matrix = state.model_view_projection;
        state.light_model = 1; // Consumed bump uses the PHONG contract.
        state.light_count = 1;
        state.lights[0].position_type = [0.0,0.0,0.0,0.0];
        state.lights[0].direction_cutoff = [0.0,0.0,-1.0,0.0];
        state.lights[0].color_intensity = [1.0,1.0,1.0,1.0];
        let mut material = CoinWgpuMaterial::zeroed();
        material.diffuse = [0.8,0.2,0.1,1.0];
        material.specular = [0.5,0.25,0.125,1.0];
        material.shininess = 0.5;
        let uniforms = bump_uniforms_from_state(&state, &material)
            .expect("bump state uniforms");
        let uniform_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump ABI uniforms"), size: std::mem::size_of_val(&uniforms) as u64,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        queue.write_buffer(&uniform_buffer, 0, bytemuck::bytes_of(&uniforms));
        let bind = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("bump ABI image"), layout: &pipeline.get_bind_group_layout(0),
            entries: &[
                wgpu::BindGroupEntry { binding: 0, resource: uniform_buffer.as_entire_binding() },
                wgpu::BindGroupEntry { binding: 1,
                    resource: wgpu::BindingResource::TextureView(&gpu.images[0].view) },
                wgpu::BindGroupEntry { binding: 2,
                    resource: wgpu::BindingResource::Sampler(&gpu.images[0].sampler) },
            ],
        });
        let inverted_bind = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("bump ABI second image"),
            layout: &pipeline.get_bind_group_layout(0),
            entries: &[
                wgpu::BindGroupEntry { binding: 0, resource: uniform_buffer.as_entire_binding() },
                wgpu::BindGroupEntry { binding: 1,
                    resource: wgpu::BindingResource::TextureView(&gpu.images[slots[1].unwrap()].view) },
                wgpu::BindGroupEntry { binding: 2,
                    resource: wgpu::BindingResource::Sampler(&gpu.images[slots[1].unwrap()].sampler) },
            ],
        });
        let specular_bind = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("bump ABI specular image"),
            layout: &specular_pipeline.get_bind_group_layout(0),
            entries: &[
                wgpu::BindGroupEntry { binding: 0, resource: uniform_buffer.as_entire_binding() },
                wgpu::BindGroupEntry { binding: 1,
                    resource: wgpu::BindingResource::TextureView(&gpu.images[0].view) },
                wgpu::BindGroupEntry { binding: 2,
                    resource: wgpu::BindingResource::Sampler(&gpu.images[0].sampler) },
            ],
        });
        let additive_specular_bind = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("bump ABI additive specular image"),
            layout: &additive_specular_pipeline.get_bind_group_layout(0),
            entries: &[
                wgpu::BindGroupEntry { binding: 0, resource: uniform_buffer.as_entire_binding() },
                wgpu::BindGroupEntry { binding: 1,
                    resource: wgpu::BindingResource::TextureView(&gpu.images[0].view) },
                wgpu::BindGroupEntry { binding: 2,
                    resource: wgpu::BindingResource::Sampler(&gpu.images[0].sampler) },
            ],
        });
        let base_image = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("bump ABI opaque base image"),
            size: wgpu::Extent3d { width: 1, height: 1, depth_or_array_layers: 1 },
            mip_level_count: 1, sample_count: 1, dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });
        queue.write_texture(base_image.as_image_copy(), &[128,128,128,255],
            wgpu::TexelCopyBufferLayout { offset: 0, bytes_per_row: Some(4),
                rows_per_image: Some(1) }, base_image.size());
        let base_image_view = base_image.create_view(&wgpu::TextureViewDescriptor::default());
        let base_sampler = device.create_sampler(&wgpu::SamplerDescriptor::default());
        let base_uniforms = BumpBaseUniforms { texture_matrix: [
            1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0,
            0.0,0.0,1.0,0.0, 0.0,0.0,0.0,1.0,
        ] };
        let base_uniform_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump ABI base matrix"),
            size: std::mem::size_of_val(&base_uniforms) as u64,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        queue.write_buffer(&base_uniform_buffer, 0, bytemuck::bytes_of(&base_uniforms));
        let base_bump_bind = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("bump ABI base normal map"),
            layout: &base_pipeline.get_bind_group_layout(0),
            entries: &[
                wgpu::BindGroupEntry { binding: 0, resource: uniform_buffer.as_entire_binding() },
                wgpu::BindGroupEntry { binding: 1,
                    resource: wgpu::BindingResource::TextureView(&gpu.images[0].view) },
                wgpu::BindGroupEntry { binding: 2,
                    resource: wgpu::BindingResource::Sampler(&gpu.images[0].sampler) },
            ],
        });
        let base_texture_bind = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("bump ABI base color image"),
            layout: &base_pipeline.get_bind_group_layout(1),
            entries: &[
                wgpu::BindGroupEntry { binding: 0,
                    resource: base_uniform_buffer.as_entire_binding() },
                wgpu::BindGroupEntry { binding: 1,
                    resource: wgpu::BindingResource::TextureView(&base_image_view) },
                wgpu::BindGroupEntry { binding: 2,
                    resource: wgpu::BindingResource::Sampler(&base_sampler) },
            ],
        });
        let target = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("bump ABI color"),
            size: wgpu::Extent3d { width: 8, height: 8, depth_or_array_layers: 1 },
            mip_level_count: 1, sample_count: 1, dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        let target_view = target.create_view(&wgpu::TextureViewDescriptor::default());
        let readback = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump ABI color readback"), size: 8 * 256,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let specular_readback = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump ABI specular readback"), size: 8 * 256,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let composed_readback = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump ABI composed readback"), size: 8 * 256,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let base_readback = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump ABI opaque base readback"), size: 8 * 256,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let inverted_readback = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump ABI second image readback"), size: 8 * 256,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor::default());
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("bump ABI diffuse"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &target_view, resolve_target: None,
                    ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store },
                })],
                depth_stencil_attachment: None, timestamp_writes: None,
                occlusion_query_set: None,
            });
            pass.set_pipeline(&pipeline);
            pass.set_bind_group(0, &bind, &[]);
            pass.set_vertex_buffer(0, positions.slice(..));
            pass.set_vertex_buffer(1, gpu.vertices.slice(..));
            pass.set_index_buffer(index_buffer.slice(..), wgpu::IndexFormat::Uint32);
            pass.draw_indexed(draw.first_index..draw.first_index + draw.index_count, 0, 0..1);
        }
        encoder.copy_texture_to_buffer(target.as_image_copy(),
            wgpu::TexelCopyBufferInfo { buffer: &readback,
                layout: wgpu::TexelCopyBufferLayout { offset: 0,
                    bytes_per_row: Some(256), rows_per_image: Some(8) } }, target.size());
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("bump ABI specular"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &target_view, resolve_target: None,
                    ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store },
                })],
                depth_stencil_attachment: None, timestamp_writes: None,
                occlusion_query_set: None,
            });
            pass.set_pipeline(&specular_pipeline);
            pass.set_bind_group(0, &specular_bind, &[]);
            pass.set_vertex_buffer(0, positions.slice(..));
            pass.set_vertex_buffer(1, gpu.vertices.slice(..));
            pass.set_index_buffer(index_buffer.slice(..), wgpu::IndexFormat::Uint32);
            pass.draw_indexed(draw.first_index..draw.first_index + draw.index_count, 0, 0..1);
        }
        encoder.copy_texture_to_buffer(target.as_image_copy(),
            wgpu::TexelCopyBufferInfo { buffer: &specular_readback,
                layout: wgpu::TexelCopyBufferLayout { offset: 0,
                    bytes_per_row: Some(256), rows_per_image: Some(8) } }, target.size());
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("bump ABI diffuse and additive specular"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &target_view, resolve_target: None,
                    ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store },
                })],
                depth_stencil_attachment: None, timestamp_writes: None,
                occlusion_query_set: None,
            });
            pass.set_pipeline(&pipeline);
            pass.set_bind_group(0, &bind, &[]);
            pass.set_vertex_buffer(0, positions.slice(..));
            pass.set_vertex_buffer(1, gpu.vertices.slice(..));
            pass.set_index_buffer(index_buffer.slice(..), wgpu::IndexFormat::Uint32);
            pass.draw_indexed(draw.first_index..draw.first_index + draw.index_count, 0, 0..1);
            pass.set_pipeline(&additive_specular_pipeline);
            pass.set_bind_group(0, &additive_specular_bind, &[]);
            pass.draw_indexed(draw.first_index..draw.first_index + draw.index_count, 0, 0..1);
        }
        encoder.copy_texture_to_buffer(target.as_image_copy(),
            wgpu::TexelCopyBufferInfo { buffer: &composed_readback,
                layout: wgpu::TexelCopyBufferLayout { offset: 0,
                    bytes_per_row: Some(256), rows_per_image: Some(8) } }, target.size());
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("bump ABI opaque base texture"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &target_view, resolve_target: None,
                    ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store },
                })],
                depth_stencil_attachment: None, timestamp_writes: None,
                occlusion_query_set: None,
            });
            pass.set_pipeline(&base_pipeline);
            pass.set_bind_group(0, &base_bump_bind, &[]);
            pass.set_bind_group(1, &base_texture_bind, &[]);
            pass.set_vertex_buffer(0, positions.slice(..));
            pass.set_vertex_buffer(1, gpu.vertices.slice(..));
            pass.set_index_buffer(index_buffer.slice(..), wgpu::IndexFormat::Uint32);
            pass.draw_indexed(draw.first_index..draw.first_index + draw.index_count, 0, 0..1);
        }
        encoder.copy_texture_to_buffer(target.as_image_copy(),
            wgpu::TexelCopyBufferInfo { buffer: &base_readback,
                layout: wgpu::TexelCopyBufferLayout { offset: 0,
                    bytes_per_row: Some(256), rows_per_image: Some(8) } }, target.size());
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("bump ABI second normal image"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &target_view, resolve_target: None,
                    ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store },
                })],
                depth_stencil_attachment: None, timestamp_writes: None,
                occlusion_query_set: None,
            });
            pass.set_pipeline(&pipeline);
            pass.set_bind_group(0, &inverted_bind, &[]);
            pass.set_vertex_buffer(0, positions.slice(..));
            pass.set_vertex_buffer(1, gpu.vertices.slice(..));
            pass.set_index_buffer(index_buffer.slice(..), wgpu::IndexFormat::Uint32);
            pass.draw_indexed(inverted_draw.first_index..inverted_draw.first_index +
                              inverted_draw.index_count, 0, 0..1);
        }
        encoder.copy_texture_to_buffer(target.as_image_copy(),
            wgpu::TexelCopyBufferInfo { buffer: &inverted_readback,
                layout: wgpu::TexelCopyBufferLayout { offset: 0,
                    bytes_per_row: Some(256), rows_per_image: Some(8) } }, target.size());
        queue.submit([encoder.finish()]);
        let (sender, receiver) = std::sync::mpsc::channel();
        readback.slice(..).map_async(wgpu::MapMode::Read, move |result| {
            let _ = sender.send(result);
        });
        let _ = device.poll(wgpu::Maintain::Wait);
        receiver.recv_timeout(std::time::Duration::from_secs(20))
            .expect("bump ABI color callback").expect("bump ABI color readback");
        let mapped = readback.slice(..).get_mapped_range();
        let offset = 4 * 256 + 4 * 4;
        let pixel: [u8; 4] = mapped[offset..offset + 4].try_into().unwrap();
        drop(mapped);
        readback.unmap();
        eprintln!("bump_abi_draw_pixel {:?}", pixel);
        assert!(pixel[0].abs_diff(204) <= 1 && pixel[1].abs_diff(51) <= 1 &&
            pixel[2].abs_diff(26) <= 1 && pixel[3] == 255);
        let (sender, receiver) = std::sync::mpsc::channel();
        composed_readback.slice(..).map_async(wgpu::MapMode::Read, move |result| {
            let _ = sender.send(result);
        });
        let _ = device.poll(wgpu::Maintain::Wait);
        receiver.recv_timeout(std::time::Duration::from_secs(20))
            .expect("bump composed callback").expect("bump composed readback");
        let mapped = composed_readback.slice(..).get_mapped_range();
        let composed: [u8; 4] = mapped[offset..offset + 4].try_into().unwrap();
        drop(mapped);
        composed_readback.unmap();
        eprintln!("bump_abi_composed_pixel {:?}", composed);
        assert!(composed[0] == 255 && composed[1].abs_diff(115) <= 1 &&
            composed[2].abs_diff(57) <= 1 && composed[3] == 255);
        let (sender, receiver) = std::sync::mpsc::channel();
        base_readback.slice(..).map_async(wgpu::MapMode::Read, move |result| {
            let _ = sender.send(result);
        });
        let _ = device.poll(wgpu::Maintain::Wait);
        receiver.recv_timeout(std::time::Duration::from_secs(20))
            .expect("bump base callback").expect("bump base readback");
        let mapped = base_readback.slice(..).get_mapped_range();
        let base_pixel: [u8; 4] = mapped[offset..offset + 4].try_into().unwrap();
        drop(mapped);
        base_readback.unmap();
        eprintln!("bump_abi_base_pixel {:?}", base_pixel);
        assert!(base_pixel[0].abs_diff(102) <= 1 &&
            base_pixel[1].abs_diff(26) <= 1 &&
            base_pixel[2].abs_diff(13) <= 1 && base_pixel[3] == 255);
        let (sender, receiver) = std::sync::mpsc::channel();
        inverted_readback.slice(..).map_async(wgpu::MapMode::Read, move |result| {
            let _ = sender.send(result);
        });
        let _ = device.poll(wgpu::Maintain::Wait);
        receiver.recv_timeout(std::time::Duration::from_secs(20))
            .expect("bump second image callback").expect("bump second image readback");
        let mapped = inverted_readback.slice(..).get_mapped_range();
        let inverted: [u8; 4] = mapped[offset..offset + 4].try_into().unwrap();
        drop(mapped);
        inverted_readback.unmap();
        eprintln!("bump_abi_second_image_pixel {:?}", inverted);
        assert_eq!(inverted, [0,0,0,255]);
        let (sender, receiver) = std::sync::mpsc::channel();
        specular_readback.slice(..).map_async(wgpu::MapMode::Read, move |result| {
            let _ = sender.send(result);
        });
        let _ = device.poll(wgpu::Maintain::Wait);
        receiver.recv_timeout(std::time::Duration::from_secs(20))
            .expect("bump ABI specular callback").expect("bump ABI specular readback");
        let mapped = specular_readback.slice(..).get_mapped_range();
        let specular: [u8; 4] = mapped[offset..offset + 4].try_into().unwrap();
        drop(mapped);
        specular_readback.unmap();
        eprintln!("bump_abi_specular_pixel {:?}", specular);
        for (actual, expected) in specular.iter().zip([127u8,64,32,254]) {
            assert!(actual.abs_diff(expected) <= 1, "specular ABI pixel {specular:?}");
        }
        state.depth_test = 1;
        state.depth_write = 1;
        state.depth_function = 3;
        // Private ABI: 0 disables channel writes; 1 explicitly enables RGBA.
        state.color_write = 1;
        state.viewport = [0,0,8,8];
        let depth = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("bump encoder depth"),
            size: target.size(), mip_level_count: 1, sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Depth32Float,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            view_formats: &[],
        });
        let depth_view = depth.create_view(&wgpu::TextureViewDescriptor::default());
        let pipeline_cache = Mutex::new(HashMap::new());
        let read_encoder_pixel = |draw_vertices: &[CoinWgpuVertex], draws: &[CoinWgpuDraw],
                                  st: CoinWgpuRenderState,
                                  textures: &[CoinWgpuTexture], samplers: &[CoinWgpuSampler]| {
            let command = encode_bump_opaque_frame(&device, &queue, Some(&pipeline_cache), None, draw_vertices,
                &indices, draws, &[st], &[material], textures, samplers, &owned, &target_view,
                wgpu::TextureFormat::Rgba8Unorm, &depth_view, [8,8], [0.0,0.0,0.0,1.0])
                .expect("bump frame encoder");
            let output = device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("bump frame encoder readback"), size: 8 * 256,
                usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
                mapped_at_creation: false,
            });
            let mut copy = device.create_command_encoder(&wgpu::CommandEncoderDescriptor::default());
            copy.copy_texture_to_buffer(target.as_image_copy(),
                wgpu::TexelCopyBufferInfo { buffer: &output,
                    layout: wgpu::TexelCopyBufferLayout { offset: 0,
                        bytes_per_row: Some(256), rows_per_image: Some(8) } }, target.size());
            queue.submit([command, copy.finish()]);
            let (sender, receiver) = std::sync::mpsc::channel();
            output.slice(..).map_async(wgpu::MapMode::Read, move |result| {
                let _ = sender.send(result);
            });
            let _ = device.poll(wgpu::Maintain::Wait);
            receiver.recv_timeout(std::time::Duration::from_secs(20))
                .expect("bump encoder callback").expect("bump encoder readback");
            let mapped = output.slice(..).get_mapped_range();
            let pixel: [u8; 4] = mapped[offset..offset + 4].try_into().unwrap();
            drop(mapped);
            output.unmap();
            pixel
        };
        let encoded = read_encoder_pixel(&vertices, &[draw], state, &[], &[]);
        eprintln!("bump_frame_encoded_pixel {:?}", encoded);
        assert_eq!(encoded, [255,115,57,255]);
        let encoded_two = read_encoder_pixel(&vertices, &[draw, inverted_draw], state, &[], &[]);
        eprintln!("bump_frame_two_draws_pixel {:?}", encoded_two);
        assert_eq!(encoded_two, [0,0,0,255]);
        let mut textured = state;
        textured.has_texture = 1;
        textured.texture_matrix = base_uniforms.texture_matrix;
        let base_rgba = [128u8,128,128,255];
        let base_source = CoinWgpuTexture { width: 1, height: 1, format: 0, reserved: 0,
            content_digest: 0, pixels: base_rgba.as_ptr(), pixel_bytes_len: 4 };
        let base_settings = CoinWgpuSampler { wrap_s: 0, wrap_t: 0, filter: 0, reserved: 0 };
        let encoded_base = read_encoder_pixel(&vertices, &[draw], textured, &[base_source], &[base_settings]);
        eprintln!("bump_frame_textured_pixel {:?}", encoded_base);
        for (actual, expected) in encoded_base.iter().zip([229u8,90,45,255]) {
            assert!(actual.abs_diff(expected) <= 1, "textured bump encoder {encoded_base:?}");
        }
        let srgb_source = CoinWgpuTexture { format: 3, ..base_source };
        let encoded_srgb = read_encoder_pixel(&vertices, &[draw], textured, &[srgb_source], &[base_settings]);
        eprintln!("bump_frame_srgb_base_pixel {:?}", encoded_srgb);
        // 128 sRGB decodes to 0.21586 linear. The diffuse contribution
        // changes; the independently lit specular contribution does not.
        for (actual, expected) in encoded_srgb.iter().zip([171u8,75,37,255]) {
            assert!(actual.abs_diff(expected) <= 2, "sRGB bump base {encoded_srgb:?}");
        }
        let repeated_base = encode_bump_opaque_frame(&device, &queue,
            Some(&pipeline_cache), None, &vertices, &indices, &[draw,draw], &[textured],
            &[material], &[base_source], &[base_settings], &owned, &target_view,
            wgpu::TextureFormat::Rgba8Unorm, &depth_view, [8,8], [0.0,0.0,0.0,1.0])
            .expect("two draws sharing a bump base image");
        drop(repeated_base);
        LAST_BUMP_BASE_IMAGE_UPLOADS.with(|count| assert_eq!(count.get(), 1,
            "two draws sharing texture and sampler upload the base image once"));
        eprintln!("bump_frame_shared_base draws=2 image_uploads=1");
        let base_cache = Mutex::new(BumpBaseCache::default());
        for serial in 1..=2 {
            let command = encode_bump_opaque_frame(&device, &queue,
                Some(&pipeline_cache), Some((&base_cache, serial, serial - 1)),
                &vertices, &indices, &[draw], &[textured], &[material],
                &[base_source], &[base_settings], &owned, &target_view,
                wgpu::TextureFormat::Rgba8Unorm, &depth_view, [8,8], [0.0,0.0,0.0,1.0])
                .expect("stable bump base across frames");
            queue.submit([command]);
        }
        let changed_rgba = [64u8,128,128,255];
        let changed_source = CoinWgpuTexture { pixels: changed_rgba.as_ptr(), ..base_source };
        let command = encode_bump_opaque_frame(&device, &queue,
            Some(&pipeline_cache), Some((&base_cache, 3, 2)),
            &vertices, &indices, &[draw], &[textured], &[material],
            &[changed_source], &[base_settings], &owned, &target_view,
            wgpu::TextureFormat::Rgba8Unorm, &depth_view, [8,8], [0.0,0.0,0.0,1.0])
            .expect("changed bytes with unchanged digest invalidate bump base");
        queue.submit([command]);
        let _ = device.poll(wgpu::Maintain::Wait);
        let cache = base_cache.lock().unwrap();
        assert_eq!(cache.uploads, 2);
        assert_eq!(cache.hits, 1);
        eprintln!("bump_base_cross_frame_cache uploads={} hits={}", cache.uploads, cache.hits);
        drop(cache);
        let command = encode_bump_opaque_frame(&device, &queue,
            Some(&pipeline_cache), Some((&base_cache, 4, 3)),
            &vertices, &indices, &[draw], &[textured], &[material],
            &[srgb_source], &[base_settings], &owned, &target_view,
            wgpu::TextureFormat::Rgba8Unorm, &depth_view, [8,8], [0.0,0.0,0.0,1.0])
            .expect("sRGB and linear base must have different cache entries");
        queue.submit([command]);
        assert_eq!(base_cache.lock().unwrap().uploads, 3,
            "sRGB reinterpretation must upload a separate texture");
        let large_pixels = vec![128u8; 512 * 512 * 4];
        let mut large_pixels = large_pixels;
        for alpha in large_pixels.iter_mut().skip(3).step_by(4) { *alpha = 255; }
        let large_source = CoinWgpuTexture { width: 512, height: 512,
            content_digest: 0xBACE, pixels: large_pixels.as_ptr(),
            pixel_bytes_len: large_pixels.len() as u64, ..base_source };
        let timing_cache = Mutex::new(BumpBaseCache::default());
        let mut uncached_base_ms = Vec::new();
        let mut cached_base_ms = Vec::new();
        let mut serial = 1;
        for cached in [false, true, true, false] {
            for _ in 0..12 {
                let start = std::time::Instant::now();
                let cache = if cached { Some((&timing_cache, serial, serial - 1)) }
                    else { None };
                let command = encode_bump_opaque_frame(&device, &queue,
                    Some(&pipeline_cache), cache, &vertices, &indices, &[draw],
                    &[textured], &[material], &[large_source], &[base_settings],
                    &owned, &target_view, wgpu::TextureFormat::Rgba8Unorm,
                    &depth_view, [8,8], [0.0,0.0,0.0,1.0])
                    .expect("large bump base timing frame");
                queue.submit([command]);
                let _ = device.poll(wgpu::Maintain::Wait);
                let elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;
                if cached { cached_base_ms.push(elapsed_ms); }
                else { uncached_base_ms.push(elapsed_ms); }
                serial += 1;
            }
        }
        uncached_base_ms.sort_by(f64::total_cmp);
        cached_base_ms.sort_by(f64::total_cmp);
        let timing_counts = timing_cache.lock().unwrap();
        eprintln!("bump_base_cache_bench image=512x512 rgba8 samples=24 uncached_median_ms={:.6} cached_median_ms={:.6} uploads={} hits={}",
            uncached_base_ms[12], cached_base_ms[12],
            timing_counts.uploads, timing_counts.hits);
        drop(timing_counts);
        let mip_pixels = [128u8,128,128,255, 128,128,128,255,
                          128,128,128,255, 128,128,128,255, 128,128,128,255];
        let mip_source = CoinWgpuTexture { width: 2, height: 2, format: 2, reserved: 0,
            content_digest: 0, pixels: mip_pixels.as_ptr(), pixel_bytes_len: 20 };
        let mip_settings = CoinWgpuSampler { wrap_s: 0, wrap_t: 0, filter: 2, reserved: 0 };
        let encoded_mip = read_encoder_pixel(&vertices, &[draw], textured, &[mip_source], &[mip_settings]);
        eprintln!("bump_frame_mipmapped_pixel {:?}", encoded_mip);
        for (actual, expected) in encoded_mip.iter().zip([229u8,90,45,255]) {
            assert!(actual.abs_diff(expected) <= 1, "mipmapped bump encoder {encoded_mip:?}");
        }
        let srgb_mip_source = CoinWgpuTexture { format: 4, ..mip_source };
        let encoded_srgb_mip = read_encoder_pixel(&vertices, &[draw], textured,
            &[srgb_mip_source], &[mip_settings]);
        eprintln!("bump_frame_srgb_mip_pixel {:?}", encoded_srgb_mip);
        for (actual, expected) in encoded_srgb_mip.iter().zip([171u8,75,37,255]) {
            assert!(actual.abs_diff(expected) <= 2,
                "mipmapped sRGB bump base {encoded_srgb_mip:?}");
        }
        for factor in [2u32,4,8,16] {
            let anisotropic = CoinWgpuSampler { filter: 3, reserved: factor - 1,
                ..mip_settings };
            let pixel = read_encoder_pixel(&vertices, &[draw], textured,
                &[mip_source], &[anisotropic]);
            for (actual, expected) in pixel.iter().zip([229u8,90,45,255]) {
                assert!(actual.abs_diff(expected) <= 1,
                    "anisotropic bump factor {factor}: {pixel:?}");
            }
        }
        eprintln!("bump_base_anisotropy factors=2,4,8,16 pixel=[229,90,45,255]");
        let mut stripe_level = vec![0u8; 256 * 256 * 4];
        for y in 0..256 {
            for x in 0..256 {
                let offset = (y * 256 + x) * 4;
                stripe_level[offset] = if (y / 8) % 2 == 0 { 255 } else { 0 };
                stripe_level[offset + 3] = 255;
            }
        }
        let mut stripe_chain = Vec::new();
        let mut side = 256usize;
        loop {
            stripe_chain.extend_from_slice(&stripe_level);
            if side == 1 { break; }
            let next_side = side / 2;
            let mut next = vec![0u8; next_side * next_side * 4];
            for y in 0..next_side {
                for x in 0..next_side {
                    for channel in 0..4 {
                        let sum: u32 = [(0,0),(1,0),(0,1),(1,1)].iter()
                            .map(|(dx,dy)| u32::from(stripe_level[
                                (((2*y+dy)*side + 2*x+dx)*4)+channel]))
                            .sum();
                        next[(y*next_side+x)*4+channel] = ((sum+2)/4) as u8;
                    }
                }
            }
            stripe_level = next;
            side = next_side;
        }
        let stripe_source = CoinWgpuTexture { width: 256, height: 256,
            format: 2, content_digest: 0x5a71, pixels: stripe_chain.as_ptr(),
            pixel_bytes_len: stripe_chain.len() as u64, ..base_source };
        let mut patterned_vertices = vertices;
        patterned_vertices[0].texcoord = [0.0,0.0,0.0,1.0];
        patterned_vertices[1].texcoord = [2.0,0.0,0.0,1.0];
        patterned_vertices[2].texcoord = [0.0,0.25,0.0,1.0];
        let mut stripe_samples = [Vec::<u8>::new(), Vec::<u8>::new()];
        for (index, factor) in [1u32,8].iter().enumerate() {
            let settings = CoinWgpuSampler { filter: 3, reserved: factor-1,
                ..mip_settings };
            for step in 0..8 {
                let mut translated = textured;
                translated.texture_matrix[13] = step as f32 * 4.0 / 256.0;
                let pixel = read_encoder_pixel(&patterned_vertices, &[draw],
                    translated, &[stripe_source], &[settings]);
                stripe_samples[index].push(pixel[0]);
            }
        }
        eprintln!("bump_base_anisotropy_stripes factor1={:?} factor8={:?}",
            stripe_samples[0], stripe_samples[1]);
        let span = |values: &[u8]| values.iter().max().unwrap() - values.iter().min().unwrap();
        assert!(span(&stripe_samples[0]) <= 4 && span(&stripe_samples[1]) >= 64,
            "anisotropic filtering must retain the oblique stripe signal");
        for rejected in [CoinWgpuSampler { filter: 3, reserved: 2, ..mip_settings },
                         CoinWgpuSampler { filter: 2, reserved: 1, ..mip_settings }] {
            assert!(encode_bump_opaque_frame(&device, &queue,
                Some(&pipeline_cache), None, &vertices, &indices, &[draw],
                &[textured], &[material], &[mip_source], &[rejected],
                &owned, &target_view, wgpu::TextureFormat::Rgba8Unorm,
                &depth_view, [8,8], [0.0,0.0,0.0,1.0]).is_err(),
                "invalid anisotropy must reject before submission");
        }
        assert_eq!(pipeline_cache.lock().unwrap().len(), 1,
            "untextured, textured and mipmapped draws reuse one pipeline set");
        let mut no_color = state;
        no_color.color_write = 0;
        let encoded_no_color = read_encoder_pixel(&vertices, &[draw], no_color, &[], &[]);
        eprintln!("bump_frame_zero_write_pixel {:?}", encoded_no_color);
        assert_eq!(encoded_no_color, [0,0,0,255],
            "zero channel writes preserve the authorial opaque black clear");
        let encoded_restored = read_encoder_pixel(&vertices, &[draw], state, &[], &[]);
        assert_eq!(encoded_restored, [255,115,57,255],
            "restoring RGBA writes cannot reuse the zero-mask cached pipeline");
        {
            let cache = pipeline_cache.lock().unwrap();
            let all = BumpPipelineKey { color_format: wgpu::TextureFormat::Rgba8Unorm,
                depth_compare: wgpu::CompareFunction::LessEqual, sample_count: 1,
                color_writes: wgpu::ColorWrites::ALL.bits() };
            let none = BumpPipelineKey { color_format: wgpu::TextureFormat::Rgba8Unorm,
                depth_compare: wgpu::CompareFunction::LessEqual, sample_count: 1,
                color_writes: wgpu::ColorWrites::empty().bits() };
            assert!(cache.contains_key(&all) && cache.contains_key(&none),
                "color write ALL and NONE require their own exact pipeline keys");
            assert_eq!(cache.len(), 2,
                "the mask contracontrol adds only NONE to the original shared ALL pipeline");
        }
        let mut uncached_ms = Vec::new();
        let mut cached_ms = Vec::new();
        // Measure CPU encoding only. Alternating blocks limits drift, and the
        // test deliberately makes no cross-driver latency assertion.
        for cached in [false, true, true, false] {
            for _ in 0..16 {
                let start = std::time::Instant::now();
                let command = encode_bump_opaque_frame(&device, &queue,
                    if cached { Some(&pipeline_cache) } else { None }, None, &vertices,
                    &indices, &[draw], &[state], &[material], &[], &[], &owned,
                    &target_view, wgpu::TextureFormat::Rgba8Unorm, &depth_view,
                    [8,8], [0.0,0.0,0.0,1.0]).expect("bump encoder timing");
                let elapsed = start.elapsed().as_secs_f64() * 1000.0;
                drop(command);
                if cached { cached_ms.push(elapsed); } else { uncached_ms.push(elapsed); }
            }
        }
        uncached_ms.sort_by(f64::total_cmp);
        cached_ms.sort_by(f64::total_cmp);
        eprintln!("bump_pipeline_cache_bench encoding_only samples=32 uncached_median_ms={:.6} cached_median_ms={:.6}",
            uncached_ms[16], cached_ms[16]);
        let mut uncached_submit_ms = Vec::new();
        let mut cached_submit_ms = Vec::new();
        for cached in [true, false, false, true] {
            for _ in 0..16 {
                let start = std::time::Instant::now();
                let command = encode_bump_opaque_frame(&device, &queue,
                    if cached { Some(&pipeline_cache) } else { None }, None, &vertices,
                    &indices, &[draw], &[state], &[material], &[], &[], &owned,
                    &target_view, wgpu::TextureFormat::Rgba8Unorm, &depth_view,
                    [8,8], [0.0,0.0,0.0,1.0]).expect("bump submit timing");
                queue.submit([command]);
                let _ = device.poll(wgpu::Maintain::Wait);
                let elapsed = start.elapsed().as_secs_f64() * 1000.0;
                if cached { cached_submit_ms.push(elapsed); }
                else { uncached_submit_ms.push(elapsed); }
            }
        }
        uncached_submit_ms.sort_by(f64::total_cmp);
        cached_submit_ms.sort_by(f64::total_cmp);
        eprintln!("bump_pipeline_cache_bench encode_submit_wait samples=32 uncached_median_ms={:.6} cached_median_ms={:.6}",
            uncached_submit_ms[16], cached_submit_ms[16]);
        let mut emissive = material;
        emissive.emission[0] = 0.1;
        emissive.ambient[1] = 0.9;
        assert!(encode_bump_opaque_frame(&device, &queue, None, None, &vertices,
            &indices, &[draw], &[state], &[emissive], &[], &[], &owned, &target_view,
            wgpu::TextureFormat::Rgba8Unorm, &depth_view, [8,8], [0.0,0.0,0.0,1.0]).is_ok());
    }

    #[test]
    #[ignore = "requires a selected physical GPU; run with --ignored"]
    fn bump_offscreen_ffi_submission_renders_pixel() {
        let mut vertices = [CoinWgpuVertex::zeroed(); 3];
        vertices[0].position = [-1.0,-1.0,0.0];
        vertices[1].position = [3.0,-1.0,0.0];
        vertices[2].position = [-1.0,3.0,0.0];
        for vertex in &mut vertices { vertex.normal = [0.0,0.0,1.0]; }
        let indices = [0u32,1,2];
        let draw = CoinWgpuDraw { topology: 0, first_index: 0, index_count: 3,
            render_state_slot: 0, ..CoinWgpuDraw::zeroed() };
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        state.model_view = [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0,
                            0.0,0.0,1.0,0.0, 0.0,0.0,-5.0,1.0];
        state.model_view_projection = [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0,
                                       0.0,0.0,1.0,0.0, 0.0,0.0,0.0,1.0];
        state.normal_matrix = state.model_view_projection;
        state.light_model = 1; // Consumed bump uses the PHONG contract.
        state.light_count = 1;
        state.lights[0].position_type = [0.0,0.0,0.0,0.0];
        state.lights[0].direction_cutoff = [0.0,0.0,-1.0,0.0];
        state.lights[0].color_intensity = [1.0,1.0,1.0,1.0];
        state.depth_test = 1;
        state.depth_write = 1;
        state.depth_function = 3;
        // Private ABI: 0 disables channel writes; 1 explicitly enables RGBA.
        state.color_write = 1;
        state.viewport = [0,0,8,8];
        let mut material = CoinWgpuMaterial::zeroed();
        material.diffuse = [0.8,0.2,0.1,1.0];
        material.specular = [0.5,0.25,0.125,1.0];
        material.shininess = 0.5;
        let bump_vertex = CoinWgpuBumpVertex { coord: [0.5,0.5,0.0,1.0],
            tangent_s: [1.0,0.0,0.0], tangent_t: [0.0,1.0,0.0], image_slot: 1 };
        let bump_vertices = [bump_vertex; 3];
        let rgb = [128u8,128,255];
        let image = CoinWgpuBumpImage { width: 1, height: 1, clamp_s: 0,
            clamp_t: 0, rgb: rgb.as_ptr(), rgb_len: 3 };
        let mut frame: CoinWgpuFrameView = unsafe { std::mem::zeroed() };
        frame.multipass_count = 1; // ABI 69 requires an explicit single pass.
        frame.abi_version = COIN_WGPU_ABI_VERSION;
        frame.struct_size = std::mem::size_of::<CoinWgpuFrameView>() as u32;
        frame.vertices = vertices.as_ptr(); frame.vertex_count = 3;
        frame.indices = indices.as_ptr(); frame.index_count = 3;
        frame.draws = &draw; frame.draw_count = 1;
        frame.materials = &material; frame.material_count = 1;
        frame.states = &state; frame.state_count = 1;
        frame.bump_vertices = bump_vertices.as_ptr(); frame.bump_vertex_count = 3;
        frame.bump_images = &image; frame.bump_image_count = 1;
        frame.width = 8; frame.height = 8;
        frame.clear_color = [0.0,0.0,0.0,1.0];
        frame.sorted_layers_passes = 1;
        let mut pixels = [0u8; 8 * 8 * 4];
        let mut depth = [0.0f32; 8 * 8];
        let mut target = CoinWgpuTarget { width: 8, height: 8,
            color_buffer: pixels.as_mut_ptr(), color_buffer_len: pixels.len() as u64,
            depth_buffer: depth.as_mut_ptr(), depth_buffer_len: depth.len() as u64,
            submission_serial: 0, device_id: 0, sample_count: 1 };
        let mut error = [0i8; 512];
        // BASE_COLOR with consumed bump remains unsupported; correcting the
        // positive PHONG fixture must not erase this negative contract.
        state.light_model = 0;
        pixels.fill(0x65);
        depth.fill(-17.0);
        let rejected = coin_wgpu_submit(&mut target, &frame, error.as_mut_ptr(), error.len());
        assert_eq!(rejected, CoinWgpuStatus::Unsupported);
        assert_eq!(target.submission_serial, 0);
        assert!(pixels.iter().all(|&value| value == 0x65));
        assert!(depth.iter().all(|&value| value == -17.0));
        state.light_model = 1;
        let status = coin_wgpu_submit(&mut target, &frame, error.as_mut_ptr(), error.len());
        let message = unsafe { std::ffi::CStr::from_ptr(error.as_ptr()) }.to_string_lossy();
        assert_eq!(status, CoinWgpuStatus::Ok, "{message}");
        let offset = (4 * 8 + 4) * 4;
        let pixel: [u8; 4] = pixels[offset..offset+4].try_into().unwrap();
        eprintln!("bump_offscreen_ffi_pixel {:?}", pixel);
        assert_eq!(pixel, [255,115,57,255]);
        assert!(target.submission_serial > 0);
        let before_zero_write = target.submission_serial;
        state.color_write = 0;
        pixels.fill(0x65);
        let status = coin_wgpu_submit(&mut target, &frame, error.as_mut_ptr(), error.len());
        let message = unsafe { std::ffi::CStr::from_ptr(error.as_ptr()) }.to_string_lossy();
        assert_eq!(status, CoinWgpuStatus::Ok, "{message}");
        let no_color: [u8; 4] = pixels[offset..offset+4].try_into().unwrap();
        eprintln!("bump_offscreen_ffi_zero_write_pixel {:?}", no_color);
        assert_eq!(no_color, [0,0,0,255],
            "zero channel writes preserve authorial clear rather than stale color");
        assert!(target.submission_serial > before_zero_write);
        let before_write_recovery = target.submission_serial;
        state.color_write = 1;
        let status = coin_wgpu_submit(&mut target, &frame, error.as_mut_ptr(), error.len());
        let message = unsafe { std::ffi::CStr::from_ptr(error.as_ptr()) }.to_string_lossy();
        assert_eq!(status, CoinWgpuStatus::Ok, "{message}");
        let recovered: [u8; 4] = pixels[offset..offset+4].try_into().unwrap();
        assert_eq!(recovered, [255,115,57,255],
            "same target restores the complete diffuse plus specular result");
        assert!(target.submission_serial > before_write_recovery);
        let first_serial = target.submission_serial;
        state.depth_function = 2;
        pixels.fill(0);
        let status = coin_wgpu_submit(&mut target, &frame, error.as_mut_ptr(), error.len());
        let message = unsafe { std::ffi::CStr::from_ptr(error.as_ptr()) }.to_string_lossy();
        assert_eq!(status, CoinWgpuStatus::Ok, "{message}");
        let less_pixel: [u8; 4] = pixels[offset..offset+4].try_into().unwrap();
        eprintln!("bump_offscreen_ffi_less_pixel {:?}", less_pixel);
        assert_eq!(less_pixel, [255,115,57,255]);
        assert!(target.submission_serial > first_serial);
    }

    #[test]
    #[ignore = "requires a selected physical GPU; run with --ignored"]
    fn msaa_offscreen_ffi_resolves_partial_edge() {
        let mut vertices = [CoinWgpuVertex::zeroed(); 3];
        vertices[0].position = [-1.0, -1.0, 0.0];
        vertices[1].position = [1.0, -1.0, 0.0];
        vertices[2].position = [-1.0, 0.83, 0.0];
        for vertex in &mut vertices { vertex.normal = [0.0, 0.0, 1.0]; }
        let indices = [0u32, 1, 2];
        let draw = CoinWgpuDraw { topology: 0, first_index: 0, index_count: 3,
            render_state_slot: 0, ..CoinWgpuDraw::zeroed() };
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        state.model_view = [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0,
                            0.0,0.0,1.0,0.0, 0.0,0.0,-5.0,1.0];
        state.model_view_projection = [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0,
                                       0.0,0.0,1.0,0.0, 0.0,0.0,0.0,1.0];
        state.normal_matrix = state.model_view_projection;
        state.viewport = [0, 0, 32, 32];
        state.depth_test = 1;
        state.depth_write = 1;
        state.depth_function = 3;
        state.color_write = 1;
        state.polygon_offset_primitive_style = 1;
        let mut material = CoinWgpuMaterial::zeroed();
        material.diffuse = [1.0, 0.0, 0.0, 1.0];
        material.emission = [1.0, 0.0, 0.0, 1.0];
        let mut frame: CoinWgpuFrameView = unsafe { std::mem::zeroed() };
        frame.multipass_count = 1; // ABI 69 requires an explicit single pass.
        frame.abi_version = COIN_WGPU_ABI_VERSION;
        frame.struct_size = std::mem::size_of::<CoinWgpuFrameView>() as u32;
        frame.vertices = vertices.as_ptr(); frame.vertex_count = 3;
        frame.indices = indices.as_ptr(); frame.index_count = 3;
        frame.draws = &draw; frame.draw_count = 1;
        frame.materials = &material; frame.material_count = 1;
        frame.states = &state; frame.state_count = 1;
        frame.width = 32; frame.height = 32;
        frame.clear_color = [0.0, 0.0, 0.0, 0.0];
        frame.sorted_layers_passes = 1;
        let mut captures = Vec::new();
        for sample_count in [1, 2, 4] {
            let mut pixels = vec![0u8; 32 * 32 * 4];
            let mut target = CoinWgpuTarget { width: 32, height: 32,
                color_buffer: pixels.as_mut_ptr(), color_buffer_len: pixels.len() as u64,
                depth_buffer: std::ptr::null_mut(), depth_buffer_len: 0,
                submission_serial: 0, device_id: 0, sample_count };
            let mut error = [0i8; 512];
            let status = coin_wgpu_submit(&mut target, &frame, error.as_mut_ptr(), error.len());
            let message = unsafe { std::ffi::CStr::from_ptr(error.as_ptr()) }.to_string_lossy();
            assert_eq!(status, CoinWgpuStatus::Ok, "{sample_count}x: {message}");
            assert!(target.submission_serial > 0);
            let partial = pixels.chunks_exact(4).filter(|pixel| pixel[3] > 0 && pixel[3] < 255).count();
            eprintln!("msaa_offscreen sample_count={sample_count} partial_alpha={partial}");
            captures.push((pixels, partial));
        }
        assert_eq!(captures[0].1, 0);
        assert!(captures[1].1 > 0 && captures[2].1 > 0);
        assert_ne!(captures[0].0, captures[2].0);
    }
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct CoinWgpuTarget {
    pub width: u32,
    pub height: u32,
    pub color_buffer: *mut u8,
    pub color_buffer_len: u64,
    pub depth_buffer: *mut f32,
    pub depth_buffer_len: u64,
    pub submission_serial: u64,
    pub device_id: CoinWgpuDeviceId,
    pub sample_count: u32,
}
const _: () = assert!(std::mem::size_of::<CoinWgpuTarget>() == 64);

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct CoinWgpuReadbackTicket {
    pub abi_version: u32,
    pub struct_size: u32,
    pub token: u64,
    pub generation: u64,
    pub submission_serial: u64,
    pub width: u32,
    pub height: u32,
    pub color_format: u32,
    pub depth_format: u32,
    pub color_row_pitch: u32,
    pub depth_row_pitch: u32,
    pub color_bytes: u64,
    pub depth_bytes: u64,
}

static NEXT_RTT_TOKEN: AtomicU64 = AtomicU64::new(1);
static NEXT_READBACK_TOKEN: AtomicU64 = AtomicU64::new(1);
static NEXT_DEVICE_ID: AtomicU64 = AtomicU64::new(1);
static GLOBAL_SUBMISSION_SERIAL: AtomicU64 = AtomicU64::new(1);
static GLOBAL_COMPLETED_SERIAL: AtomicU64 = AtomicU64::new(0);
static LAST_SUBMITTED_SERIAL: AtomicU64 = AtomicU64::new(0);

pub struct CachedGeometryEntry {
    pub vertex_buffer: wgpu::Buffer,
    pub index_buffer: Option<wgpu::Buffer>,
    pub vertex_count: u32,
    pub index_count: u32,
    pub size_bytes: u64,
    pub last_submitted_serial: u64,
}

pub struct RetiredBuffer {
    pub vertex_buffer: wgpu::Buffer,
    pub index_buffer: Option<wgpu::Buffer>,
    pub retired_at_serial: u64,
    pub size_bytes: u64,
}

pub struct GeometryCache {
    pub active_entries: HashMap<(u64, u32), (u64, CachedGeometryEntry)>,
    pub deferred_release: Vec<RetiredBuffer>,
    pub cumulative_uploads: u64,
    pub cumulative_hits: u64,
    pub cumulative_misses: u64,
    pub cumulative_uploaded_bytes: u64,
    pub frame_uploaded_bytes: u64,
    pub frame_uploads: u64,
    pub frame_hits: u64,
    pub total_active_geometry_bytes: u64,
    pub max_active_geometry_bytes: u64,
    pub max_stale_serials: u64,
    pub cumulative_evictions: u64,
}

impl Default for GeometryCache {
    fn default() -> Self {
        Self {
            active_entries: HashMap::new(),
            deferred_release: Vec::new(),
            cumulative_uploads: 0,
            cumulative_hits: 0,
            cumulative_misses: 0,
            cumulative_uploaded_bytes: 0,
            frame_uploaded_bytes: 0,
            frame_uploads: 0,
            frame_hits: 0,
            total_active_geometry_bytes: 0,
            max_active_geometry_bytes: 128 * 1024 * 1024,
            max_stale_serials: 10,
            cumulative_evictions: 0,
        }
    }
}

impl GeometryCache {
    pub fn evict_stale(&mut self, current_serial: u64) {
        let threshold = self.max_stale_serials;
        let mut to_remove = Vec::new();
        for (&key, (_rev, entry)) in &self.active_entries {
            if current_serial > entry.last_submitted_serial + threshold {
                to_remove.push(key);
            }
        }
        for key in to_remove {
            if let Some((_rev, entry)) = self.active_entries.remove(&key) {
                self.total_active_geometry_bytes = self
                    .total_active_geometry_bytes
                    .saturating_sub(entry.size_bytes);
                self.cumulative_evictions += 1;
                self.deferred_release.push(RetiredBuffer {
                    vertex_buffer: entry.vertex_buffer,
                    index_buffer: entry.index_buffer,
                    retired_at_serial: entry.last_submitted_serial,
                    size_bytes: entry.size_bytes,
                });
            }
        }
    }

    pub fn evict_for_budget(
        &mut self,
        incoming_bytes: u64,
        frame_active_keys: &std::collections::HashSet<(u64, u32)>,
    ) {
        if self.total_active_geometry_bytes + incoming_bytes <= self.max_active_geometry_bytes {
            return;
        }

        let mut candidates: Vec<((u64, u32), u64, u64)> = self
            .active_entries
            .iter()
            .filter(|(&key, _)| !frame_active_keys.contains(&key))
            .map(|(&key, (_rev, entry))| (key, entry.last_submitted_serial, entry.size_bytes))
            .collect();

        candidates.sort_by_key(|&(_, serial, _)| serial);

        for (key, _, _) in candidates {
            if self.total_active_geometry_bytes + incoming_bytes <= self.max_active_geometry_bytes {
                break;
            }
            if let Some((_rev, entry)) = self.active_entries.remove(&key) {
                self.total_active_geometry_bytes = self
                    .total_active_geometry_bytes
                    .saturating_sub(entry.size_bytes);
                self.cumulative_evictions += 1;
                self.deferred_release.push(RetiredBuffer {
                    vertex_buffer: entry.vertex_buffer,
                    index_buffer: entry.index_buffer,
                    retired_at_serial: entry.last_submitted_serial,
                    size_bytes: entry.size_bytes,
                });
            }
        }
    }
}

static WGSL_SHADER: &str = include_str!("../../shaders/coin_standard.wgsl");
static WGSL_LINE_SHADER: &str = include_str!("../../shaders/coin_line.wgsl");
static WGSL_POINT_SHADER: &str = include_str!("../../shaders/coin_point.wgsl");

#[derive(Hash, PartialEq, Eq, Clone, Debug)]
struct PipelineKey {
    instanced: bool,
    textured: bool,
    eight_shadows: bool,
    shadow_profile: bool,
    weighted: bool,
    topology: u32,
    color_format: wgpu::TextureFormat,
    depth_format: wgpu::TextureFormat,
    sample_count: u32,
    cull_mode: Option<wgpu::Face>,
    front_face: wgpu::FrontFace,
    blend: bool,
    additive: bool,
    legacy_blend_alpha: bool,
    peel: bool,
    depth_write: bool,
    color_write: u32,
    depth_compare: wgpu::CompareFunction,
    resolved_depth_bias: bool,
    depth_bias_constant: i32,
    depth_bias_slope_bits: u32,
}

#[derive(Hash, PartialEq, Eq, Clone, Debug)]
struct TextureKey {
    width: u32,
    height: u32,
    format: u32,
    metadata: u32,
    content_digest: u64,
}

impl TextureKey {
    fn from_texture(image: &CoinWgpuTexture) -> Self {
        Self { width: image.width, height: image.height, format: image.format,
            metadata: if image.format == 11 || image.format == 12 { texture_metadata(image) } else { 0 },
            content_digest: image.content_digest }
    }
}

#[allow(dead_code)]
struct CachedTextureEntry {
    texture: wgpu::Texture,
    view: wgpu::TextureView,
    pixels_copy: Vec<u8>,
    last_submitted_serial: u64,
    size_bytes: usize,
}

#[allow(dead_code)]
struct RetiredTexture {
    texture: wgpu::Texture,
    retired_at_serial: u64,
}

#[derive(Default)]
struct TextureCache {
    entries: HashMap<TextureKey, CachedTextureEntry>,
    retired: Vec<RetiredTexture>,
    uploads: u64,
    hits: u64,
    uploaded_bytes: u64,
    evictions: u64,
}

struct RttTexture {
    #[allow(dead_code)] // The GPU handle is retained for lifetime, not read on the CPU.
    texture: wgpu::Texture,
    view: wgpu::TextureView,
    width: u32,
    height: u32,
    opaque: bool,
    format: u32,
    mip_count: u32,
    cube_face_size: u32,
    cube_mipmapped: bool,
}
struct RetiredRttTexture {
    #[allow(dead_code)] // Retirement keeps the view alive until the submission fence.
    resource: RttTexture, // Retain both texture and view until the fence.
    retired_at_serial: u64,
}


#[derive(Default)]
struct RttRegistry {
    active: HashMap<u64, RttTexture>,
    retired: Vec<RetiredRttTexture>,
}

#[derive(Hash, PartialEq, Eq, Clone, Copy, Debug)]
struct SamplerKey {
    max_anisotropy_minus_one: u32,
    wrap_s: u32,
    wrap_t: u32,
    filter: u32,
}

#[derive(Default)]
struct SamplerCache {
    entries: HashMap<SamplerKey, wgpu::Sampler>,
}

// The camera fast path reads geometry only from this Rust-owned
// snapshot. A revision hint alone never licenses dereferencing caller-owned
// geometry without full validation. One snapshot belongs to one device
// generation and is discarded with that device on loss/destruction.
struct ValidatedGeometry {
    vertices: Vec<CoinWgpuVertex>,
    indices: Vec<u32>,
    draws: Vec<CoinWgpuDraw>,
    materials: Vec<CoinWgpuMaterial>,
    max_abs_position: f64,
    opaque_batch: bool,
    instances: Vec<CoinWgpuInstance>,
    instance_ranges: Vec<CoinWgpuInstanceRange>,
}

struct ValidatedScene {
    revision: u64,
    generation: u64,
    width: u32,
    height: u32,
    clear_color: [f32; 4],
    geometry: Arc<ValidatedGeometry>,
    states: Vec<CoinWgpuRenderState>,
    draw_order: Vec<composition::CompositionItem>,
}

// Object animation changes baked geometry on each full revision. Retain one
// initial snapshot, then stop cloning until an actual camera hint asks for it.
// The decision is speculative; only a successful submission commits policy.
#[derive(Default)]
struct CameraSnapshotPolicy {
    suspended: bool,
}

impl CameraSnapshotPolicy {
    fn capture(&self, previous_revision: Option<u64>, revision: u64,
               camera_hint: u64, owned_base: bool) -> bool {
        owned_base || camera_hint != 0 || (!self.suspended &&
            !previous_revision.is_some_and(|previous| previous != revision))
    }

    fn commit(&mut self, capture: bool, snapshot: bool) {
        if snapshot { self.suspended = false; }
        else if !capture { self.suspended = true; }
    }
}

// The camera experiment retains materials; draw uniforms always use the
// shared arena so it cannot recreate one sampler descriptor table per draw.
struct CameraGpuBindings {
    geometry: Arc<ValidatedGeometry>,
    materials_buffer: wgpu::Buffer,
}

// One immutable packed frame per device generation. The private transport's
// nonzero revision names CoinWgpuFfiFrame-owned storage, as in its validation
// cache. This retains GPU buffers only; incoming slices still pass validation.
// Zero revisions and stable per-node draws keep their existing paths.
struct FrameGpuBuffers {
    revision: u64,
    geometry: Option<Arc<ValidatedGeometry>>,
    vertex_count: usize,
    index_count: usize,
    material_bytes: usize,
    vertex_buffer: wgpu::Buffer,
    index_buffer: wgpu::Buffer,
    materials_buffer: wgpu::Buffer,
    geometry_bytes: u64,
}

// Fresh immutable buffers are committed only after successful submission.
// Keeping one bounded owned payload permits exact comparison; revisions alone
// never establish equality for new instanced object/material contents.
#[derive(Clone)]
struct InstancedGpuBuffers {
    geometry: Arc<ValidatedGeometry>,
    vertex_buffer: wgpu::Buffer,
    index_buffer: wgpu::Buffer,
    materials_buffer: wgpu::Buffer,
    instances_buffer: wgpu::Buffer,
    geometry_bytes: u64,
    instance_bytes: u64,
    material_bytes: u64,
}

// Four timestamps bracket the render pass(es) and the offscreen copy. The
// buffers exist only for traced synchronous frames on timestamp-capable
// devices; normal frames allocate none of these resources.
struct GpuTimestampProbe {
    queries: wgpu::QuerySet,
    resolved: wgpu::Buffer,
    readback: wgpu::Buffer,
}

impl GpuTimestampProbe {
    const BYTES: u64 = 4 * std::mem::size_of::<u64>() as u64;

    fn new(device: &wgpu::Device) -> Self {
        let queries = device.create_query_set(&wgpu::QuerySetDescriptor {
            label: Some("Coin GPU Phase Timestamps"),
            ty: wgpu::QueryType::Timestamp,
            count: 4,
        });
        let resolved = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("Coin GPU Timestamp Resolve"),
            size: Self::BYTES,
            usage: wgpu::BufferUsages::QUERY_RESOLVE | wgpu::BufferUsages::COPY_SRC,
            mapped_at_creation: false,
        });
        let readback = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("Coin GPU Timestamp Readback"),
            size: Self::BYTES,
            usage: wgpu::BufferUsages::MAP_READ | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        Self { queries, resolved, readback }
    }
}

// One completed synchronous offscreen target per device. Async readbacks and
// direct RTT keep their own attachments so no texture is reused in flight.
// The synchronous connector owns one token per target. Keep exact depth
// samples across frames, independently of the optional device scratch cache.
struct TargetDepthHistory {
    width: u32, height: u32, samples: u32,
    texture: wgpu::Texture,
    view: wgpu::TextureView,
}
static NEXT_DEPTH_HISTORY: AtomicU64 = AtomicU64::new(1);

fn manager_backgrounds(draws: &[CoinWgpuDraw]) -> bool {
    draws.iter().find(|draw| draw.composition_flags & 64 != 0)
        .is_some_and(|scene| draws.iter().any(|draw| draw.render_layer < scene.render_layer))
}

fn initialize_target_depth(device: &wgpu::Device, view: &wgpu::TextureView) -> wgpu::CommandBuffer {
    let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
        label: Some("Coin target initial depth"),
    });
    {
        let _pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("Coin target initial depth"), color_attachments: &[],
            depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                view, depth_ops: Some(wgpu::Operations {
                    load: wgpu::LoadOp::Clear(1.0), store: wgpu::StoreOp::Store,
                }), stencil_ops: None,
            }), timestamp_writes: None, occlusion_query_set: None,
        });
    }
    encoder.finish()
}

struct CachedOffscreenAttachments {
    width: u32,
    height: u32,
    color_texture: wgpu::Texture,
    color_view: wgpu::TextureView,
    // A target-owned depth attachment must never be lent to another target.
    // Color remains eligible for the same device scratch policy.
    depth: Option<(wgpu::Texture, wgpu::TextureView)>,
}

impl CachedOffscreenAttachments {
    const MAX_BYTES: u64 = 32 * 1024 * 1024;
}

fn same_camera_independent_state(a: &CoinWgpuRenderState, b: &CoinWgpuRenderState) -> bool {
    if a.texture_projection != b.texture_projection || a.alpha_test_function != b.alpha_test_function ||
        a.alpha_test_reference.to_bits() != b.alpha_test_reference.to_bits() { return false; }
    a.light_direction == b.light_direction && a.light_color == b.light_color
        && a.light_intensity == b.light_intensity && a.has_light == b.has_light
        && a.material_slot == b.material_slot && a.flat_shadow_primary == b.flat_shadow_primary && a.cull_mode == b.cull_mode
        && a.front_face == b.front_face && a.light_model == b.light_model
        && a.texture_matrix == b.texture_matrix && a.has_texture == b.has_texture
        && a.texture_slot == b.texture_slot && a.sampler_slot == b.sampler_slot
        && a.raster_texture_source_slot == b.raster_texture_source_slot
        && a.raster_texture_coordinates == b.raster_texture_coordinates
        && a.extra_textures == b.extra_textures && a.texture_combines == b.texture_combines
        && a.texture_model == b.texture_model
        && a.texture_blend_color == b.texture_blend_color && a.viewport == b.viewport
        && a.light_count == b.light_count
        && a.ambient_light == b.ambient_light && a.fog_mode == b.fog_mode
        && a.fog_color == b.fog_color && a.fog_start == b.fog_start
        && a.depth_test == b.depth_test && a.depth_write == b.depth_write
        && a.color_write == b.color_write
        && a.depth_function == b.depth_function && a.depth_range == b.depth_range
        && a.polygon_offset_enabled == b.polygon_offset_enabled
        && a.polygon_offset_factor == b.polygon_offset_factor
        && a.polygon_offset_units == b.polygon_offset_units
        && a.polygon_offset_slope_bias == b.polygon_offset_slope_bias
        && a.polygon_offset_max_depth_bits == b.polygon_offset_max_depth_bits
        && a.polygon_offset_styles == b.polygon_offset_styles
        && a.polygon_offset_primitive_style == b.polygon_offset_primitive_style
        && a.clip_plane_count == b.clip_plane_count && a.clip_planes == b.clip_planes
        && a.lights.iter().zip(b.lights.iter()).all(|(x, y)| {
            x.position_type == y.position_type && x.direction_cutoff == y.direction_cutoff
                && x.color_intensity == y.color_intensity
                && x.attenuation_exponent == y.attenuation_exponent
                && x.ambient_color == y.ambient_color
        })
}

fn camera_states_match(scene: &ValidatedScene, states: &[CoinWgpuRenderState]) -> bool {
    scene.states.len() == states.len() && scene.states.iter().zip(states).all(|(old, next)| {
        let immutable_state_matches = if (scene.geometry.opaque_batch || !scene.geometry.instances.is_empty()) && old.light_model == 1 {
            let mut a = *old;
            let mut b = *next;
            // Camera overlays resolve these coordinates from immutable world
            // light values. Keep all light types/colors/attenuation in the key.
            for state in [&mut a, &mut b] {
                state.light_direction[..3].fill(0.0);
                for light in &mut state.lights {
                    light.position_type[..3].fill(0.0);
                    light.direction_cutoff[..3].fill(0.0);
                }
            }
            same_camera_independent_state(&a, &b)
        } else {
            old.light_model == 0 && same_camera_independent_state(old, next)
        };
        if old.fog_mode != 0 || (old.has_texture != 0 || old.extra_textures.iter().any(|t| t.enabled != 0))
            || !immutable_state_matches
            || !next.model_view.iter().chain(next.model_view_projection.iter())
                .chain(next.normal_matrix.iter()).all(|x| x.is_finite())
            || !next.fog_end.is_finite() {
            return false;
        }
        // Composition preflight rejects non-finite view-space depth. Bound
        // the changed matrix against the previously validated positions.
        if !scene.geometry.instances.is_empty() && !instancing::camera_matrices_valid(next, scene.geometry.max_abs_position) {
            return false;
        }
        let m = &next.model_view;
        let bound = scene.geometry.max_abs_position
            * (f64::from(m[2]).abs() + f64::from(m[6]).abs() + f64::from(m[10]).abs())
            + f64::from(m[14]).abs();
        bound <= f64::from(f32::MAX)
    })
}

fn opaque_camera_batch(states: &[CoinWgpuRenderState], draws: &[CoinWgpuDraw],
                       order: &[composition::CompositionItem]) -> bool {
    states.len() == 1 && draws.len() == 1 && order.len() == 1
        && matches!(states[0].light_model, 0 | 1)
        && states[0].fog_mode == 0 && states[0].clip_plane_count == 0
        && states[0].polygon_offset_enabled == 0 && states[0].has_texture == 0
        && states[0].extra_textures.iter().all(|t| t.enabled == 0)
        && draws[0].topology == 0 && draws[0].stable_node_id == 0
        && draws[0].composition_flags == 0 && draws[0].render_layer == 0
        && draws[0].clear_depth_before == 0 && draws[0].first_vertex == 0
        && draws[0].first_index == 0 && draws[0].vertex_count != 0
        && draws[0].index_count != 0 && draws[0].index_count % 3 == 0
        && !order[0].blend && !order[0].peel && !order[0].weighted
}

fn camera_scene_eligible(
    vertices: &[CoinWgpuVertex], indices: &[u32], draws: &[CoinWgpuDraw],
    materials: &[CoinWgpuMaterial],
    states: &[CoinWgpuRenderState], order: &[composition::CompositionItem],
    textures: &[CoinWgpuTexture], samplers: &[CoinWgpuSampler],
) -> bool {
    let max_owned_scene_bytes = if opaque_camera_batch(states, draws, order) {
        256 * 1024 * 1024
    } else { 32 * 1024 * 1024 };
    let owned_bytes = std::mem::size_of_val(vertices)
        .saturating_add(std::mem::size_of_val(indices))
        .saturating_add(std::mem::size_of_val(draws))
        .saturating_add(std::mem::size_of_val(materials))
        .saturating_add(std::mem::size_of_val(states))
        .saturating_add(std::mem::size_of_val(order));
    owned_bytes <= max_owned_scene_bytes
        && textures.is_empty() && samplers.is_empty()
        && states.iter().all(|s| (s.light_model == 0 || opaque_camera_batch(states, draws, order))
            && s.fog_mode == 0 && s.has_texture == 0 && s.texture_projection <= 1
            && s.flat_shadow_primary == 0
            && s.extra_textures.iter().all(|t| t.enabled == 0))
        && materials.iter().all(|m| m.diffuse[3] == 1.0 && m.transparency == 0.0)
        && order.iter().all(|item| !item.blend)
        && vertices.iter().all(|v| v.position.iter().chain(v.normal.iter()).chain(v.texcoord.iter()).all(|x| x.is_finite())
            && v.extra_texcoords.iter().flatten().all(|x| x.is_finite())
            && v.screen_space_w.is_finite() && v.screen_space_w >= 0.0
            && v.fog_eye_depth_plus_one.is_finite() && v.fog_eye_depth_plus_one >= 0.0
            && (v.material_slot as usize) < materials.len()
            && (!opaque_camera_batch(states, draws, order) ||
                (v.screen_space_w == 1.0 && v.fog_eye_depth_plus_one == 0.0
                    && v.normal.iter().all(|x| x.abs() <= 1.0e18))))
}

fn camera_base(device: &DeviceState, f: &CoinWgpuFrameView,
               states: &[CoinWgpuRenderState]) -> Option<Arc<ValidatedScene>> {
    owned_camera_base(device.validated_scene.as_ref()?, device.generation, f, states)
}

fn owned_camera_base(scene: &Arc<ValidatedScene>, generation: u64, f: &CoinWgpuFrameView,
                     states: &[CoinWgpuRenderState]) -> Option<Arc<ValidatedScene>> {
    if f.portable_module_count != 0 || f.frame_revision == 0 || f.texture_count != 0 || f.sampler_count != 0 { return None; }
    let geometry = &scene.geometry;
    if !geometry.instances.is_empty() && instancing::validate_counts(f).is_err() { return None; }
    if scene.generation != generation || scene.width != f.width || scene.height != f.height
        || scene.clear_color != f.clear_color
        || geometry.vertices.len() as u64 != f.vertex_count
        || geometry.indices.len() as u64 != f.index_count
        || geometry.draws.len() as u64 != f.draw_count
        || geometry.materials.len() as u64 != f.material_count
        || geometry.instances.len() as u64 != f.instance_count
        || geometry.instance_ranges.len() as u64 != f.instance_range_count { return None; }
    let exact_state = scene.states.len() == states.len() && scene.states.iter().zip(states).all(|(a, b)|
        same_camera_independent_state(a, b) && a.fog_end == b.fog_end
            && a.model_view == b.model_view && a.model_view_projection == b.model_view_projection
            && a.normal_matrix == b.normal_matrix);
    // A full instanced payload can change under the same caller revision.
    // Exact bytes from bounded, aligned slices are required before skipping
    // validation; only a null immutable payload directly licenses our Arc.
    let exact = f.camera_base_revision == 0 && scene.revision == f.frame_revision && exact_state
        && (geometry.instances.is_empty() || instancing::owned_payload_matches(geometry, f));
    let patch = f.camera_base_revision != 0 && f.frame_revision != f.camera_base_revision
        && scene.revision == f.camera_base_revision && camera_states_match(scene, states);
    (exact || patch).then(|| scene.clone())
}

// Build only after ordinary draw/index/composition preflight. Allocation is
// bounded and fallible: an unavailable snapshot simply keeps the full path.
fn camera_scene(generation: u64, f: &CoinWgpuFrameView,
    vertices: &[CoinWgpuVertex], indices: &[u32], draws: &[CoinWgpuDraw],
    materials: &[CoinWgpuMaterial], states: &[CoinWgpuRenderState],
    order: &[composition::CompositionItem], textures: &[CoinWgpuTexture], samplers: &[CoinWgpuSampler],
    instances: &[CoinWgpuInstance], instance_ranges: &[CoinWgpuInstanceRange],
    base: Option<&Arc<ValidatedScene>>) -> Option<Arc<ValidatedScene>> {
    if f.frame_revision == 0 && instances.is_empty() { return None; }
    fn copy<T: Clone>(source: &[T]) -> Option<Vec<T>> {
        let mut target = Vec::new();
        target.try_reserve_exact(source.len()).ok()?;
        target.extend_from_slice(source);
        Some(target)
    }
    let trace_resources = std::env::var_os("COIN_RENDER_TRACE_PHASES")
        .or_else(|| std::env::var_os("COIN_WGPU_TRACE_PHASES")).is_some();
    let profile_snapshot = trace_resources.then(std::time::Instant::now);
    let geometry = if let Some(base) = base {
        base.geometry.clone()
    } else {
        if instances.is_empty() && !camera_scene_eligible(vertices, indices, draws, materials, states, order, textures, samplers) {
            return None;
        }
        Arc::new(ValidatedGeometry {
            vertices: copy(vertices)?, indices: copy(indices)?, draws: copy(draws)?, materials: copy(materials)?,
            max_abs_position: instancing::position_bound(vertices, instances),
            opaque_batch: opaque_camera_batch(states, draws, order),
            instances: copy(instances)?, instance_ranges: copy(instance_ranges)?,
        })
    };
    let scene = Arc::new(ValidatedScene {
        revision: f.frame_revision, generation, width: f.width, height: f.height,
        clear_color: f.clear_color, geometry, states: copy(states)?, draw_order: copy(order)?,
    });
    if let Some(start) = profile_snapshot {
        // Bytes are the copied payloads, not allocator capacity. This timer
        // spans the entire scene snapshot, including bounds/states/order; it
        // is not isolated material memcpy and precedes submission success.
        eprintln!("COIN_RENDER_PHASE rust_material_snapshot source_bytes={} material_copied_bytes={} instance_copied_bytes={} geometry_reused={} snapshot_ms={} attempt=1",
            std::mem::size_of_val(materials), if base.is_some() { 0 } else { std::mem::size_of_val(materials) },
            if base.is_some() { 0 } else { std::mem::size_of_val(instances) }, base.is_some() as u32,
            start.elapsed().as_secs_f64() * 1000.0);
    }
    Some(scene)
}

fn commit_camera_scene(device: &mut DeviceState, scene: Option<Arc<ValidatedScene>>,
                       update: bool, revision: u64, owned_base: bool, capture: bool,
                       instanced: bool, pending_instances: Option<InstancedGpuBuffers>) {
    if !update { return; }
    *device.instanced_buffers.lock().unwrap() = if instanced { pending_instances } else { None };
    device.camera_snapshot_policy.commit(capture, scene.is_some());
    device.validated_scene = scene;
    if owned_base && !instanced {
        if let Ok(mut cache) = VALIDATED_FRAME_CACHE.lock() {
            if let Some(scene) = device.validated_scene.as_ref() {
                *cache = Some((revision, scene.draw_order.clone()));
            }
        }
    }
}

#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/lib_camera_scene_tests.rs"]
mod camera_scene_tests;

struct SurfaceSamplingProfile {
    policy: SamplingPolicy,
    multisampled_peel: bool,
    sample_depth: bool,
    shadow_multitexture: bool,
    bump_receiver: bool,
    shader_module: wgpu::ShaderModule,
    instanced_shader: wgpu::ShaderModule,
    instanced_bind_group_layout: wgpu::BindGroupLayout,
    instanced_pipeline_layout: wgpu::PipelineLayout,
    shadow_eight_shader: wgpu::ShaderModule,
    shadow_shader: wgpu::ShaderModule,
    shadow_eight_layout: wgpu::BindGroupLayout,
    shadow_extra_layout: wgpu::BindGroupLayout,
    shadow_eight_pipeline_layout: wgpu::PipelineLayout,
    line_shader_module: wgpu::ShaderModule,
    point_shader_module: wgpu::ShaderModule,
    untextured_shaders: Mutex<HashMap<(u32, bool, bool), wgpu::ShaderModule>>,
    bind_group_layout: wgpu::BindGroupLayout,
    pipeline_layout: wgpu::PipelineLayout,
    pipelines: Mutex<HashMap<PipelineKey, wgpu::RenderPipeline>>,
}

fn create_surface_sampling_profile(device: &wgpu::Device, adapter: &wgpu::Adapter,
                                   policy: SamplingPolicy, multisampled_peel: bool, sample_depth: bool, shadow_multitexture: bool) -> SurfaceSamplingProfile {
    create_surface_sampling_profile_impl(device, adapter, policy, multisampled_peel, sample_depth, shadow_multitexture, false, None)
}

fn create_surface_sampling_profile_impl(device: &wgpu::Device, adapter: &wgpu::Adapter,
                                   policy: SamplingPolicy, multisampled_peel: bool, sample_depth: bool, shadow_multitexture: bool, bump_receiver: bool,
                                   portable_module: Option<(&portable::Module,u32)>) -> SurfaceSamplingProfile {
    let gl_depth_loads = adapter.get_info().backend == wgpu::Backend::Gl;
    let shader_source = |source: &str| {
        let source = portable_module.map_or_else(|| source.to_owned(), |(module,_)| portable::shader_source(source,module));
        shader_profile::depth_load_profile(
            &shader_profile::sample_depth_profile(&shader_profile::peeling_msaa_profile(
                &shader_profile::texture_specialization(&shader_profile::sampling_profile(&source,
                policy != SamplingPolicy::Native)), multisampled_peel), sample_depth), gl_depth_loads)
    };
    let trace_compile = |family: &str| {
        if let Some((module,_)) = portable_module {
            portable::trace_compile(module,policy,multisampled_peel,sample_depth,shadow_multitexture,family);
        }
    };
    let module_mode = portable_module.map_or(0,|(_,mode)|mode);
    trace_compile(match module_mode { 1 => "shadow4", 2 => "shadow8", _ => "surface" });
    let main_source = if module_mode == 1 { shadow_receiver::four_map_multitexture_source() }
        else if module_mode == 2 { shadow_receiver::eight_map_multitexture_source() }
        else if bump_receiver { bump_receiver::source() }
        else { shader_profile::without_shadows(WGSL_SHADER) };
    let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("coin_standard.wgsl"),
        source: wgpu::ShaderSource::Wgsl(shader_source(
            &format!("{}{}",main_source,weighted::FRAGMENT)).into()),
    });

    let instanced_shader = if portable_module.is_some() { shader.clone() } else { device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("Coin opaque instances"),
        source: wgpu::ShaderSource::Wgsl(shader_source(
            &format!("{}{}", shader_profile::sampling_profile(&instancing::shader_source(), false), weighted::FRAGMENT)).into()),
    }) };

    let shadow_shader = if portable_module.is_some() { shader.clone() } else { device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("Coin quality shadow receivers"),
        source: wgpu::ShaderSource::Wgsl(shader_source(
            &format!("{}{}",if bump_receiver { bump_receiver::source() } else if shadow_multitexture { shadow_receiver::four_map_multitexture_source() } else { shadow_receiver::four_map_source() },weighted::FRAGMENT)).into()),
    }) };
    let shadow_eight_shader = if portable_module.is_some() { shader.clone() } else { device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("Coin eight shadow receivers"),
        source: wgpu::ShaderSource::Wgsl(shader_source(
            &format!("{}{}",if bump_receiver { bump_receiver::eight_source() } else if shadow_multitexture { shadow_receiver::eight_map_multitexture_source() } else { shadow_receiver::eight_map_source() },weighted::FRAGMENT)).into()),
    }) };
    let line_shader = if portable_module.is_some() { shader.clone() } else { device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("coin_line.wgsl"),
        source: wgpu::ShaderSource::Wgsl(shader_source(
            &format!("{}{}",WGSL_LINE_SHADER,weighted::FRAGMENT)).into()),
    }) };

    let point_shader = if portable_module.is_some() { shader.clone() } else { device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("coin_point.wgsl"),
        source: wgpu::ShaderSource::Wgsl(shader_source(
            &format!("{}{}",WGSL_POINT_SHADER,weighted::FRAGMENT)).into()),
    }) };

    let storage_visibility = if device.limits().max_storage_buffers_per_shader_stage >= 1 {
        wgpu::ShaderStages::VERTEX_FRAGMENT
    } else {
        wgpu::ShaderStages::FRAGMENT
    };

    let mut layout_entries = vec![
        wgpu::BindGroupLayoutEntry { binding: 0, visibility: wgpu::ShaderStages::VERTEX_FRAGMENT,
            ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform,
                has_dynamic_offset: true,
                min_binding_size: std::num::NonZeroU64::new(uniform_binding_bytes(false, policy) as u64) }, count: None },
        wgpu::BindGroupLayoutEntry { binding: 1, visibility: storage_visibility,
            ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Storage { read_only: true },
                has_dynamic_offset: false, min_binding_size: None }, count: None },
    ];
    for unit in 0..8 {
        layout_entries.push(wgpu::BindGroupLayoutEntry { binding: 2 + 2 * unit,
            visibility: wgpu::ShaderStages::FRAGMENT,
            ty: wgpu::BindingType::Texture {
                sample_type: wgpu::TextureSampleType::Float { filterable: true },
                view_dimension: wgpu::TextureViewDimension::D2,
                multisampled: false,
            },
            count: None,
        });
        layout_entries.push(wgpu::BindGroupLayoutEntry {
            binding: 3 + 2 * unit,
            visibility: wgpu::ShaderStages::FRAGMENT,
            ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
            count: None,
        });
    }
    for binding in [18, 19] {
        layout_entries.push(wgpu::BindGroupLayoutEntry {
            binding,
            visibility: wgpu::ShaderStages::FRAGMENT,
            ty: wgpu::BindingType::Texture {
                sample_type: if gl_depth_loads { wgpu::TextureSampleType::Float { filterable: false } }
                    else { wgpu::TextureSampleType::Depth },
                view_dimension: wgpu::TextureViewDimension::D2,
                multisampled: multisampled_peel,
            },
            count: None,
        });
    }
    layout_entries.push(wgpu::BindGroupLayoutEntry {
        binding: 20,
        visibility: wgpu::ShaderStages::FRAGMENT,
        ty: wgpu::BindingType::Texture {
            sample_type: wgpu::TextureSampleType::Float { filterable: false },
            view_dimension: if shadow_multitexture { wgpu::TextureViewDimension::D2Array } else { wgpu::TextureViewDimension::D2 },
            multisampled: false,
        },
        count: None,
    });
    if !shadow_multitexture {
    layout_entries.push(wgpu::BindGroupLayoutEntry {
        binding: 21,
        visibility: wgpu::ShaderStages::FRAGMENT,
        ty: wgpu::BindingType::Texture {
            sample_type: wgpu::TextureSampleType::Float { filterable: false },
            view_dimension: wgpu::TextureViewDimension::D2,
            multisampled: false,
        },
        count: None,
    });
    layout_entries.push(wgpu::BindGroupLayoutEntry {
        binding: 22, visibility: wgpu::ShaderStages::FRAGMENT,
        ty: wgpu::BindingType::Texture {
            sample_type: wgpu::TextureSampleType::Float { filterable: false },
            view_dimension: wgpu::TextureViewDimension::D2, multisampled: false,
        }, count: None,
    });
    layout_entries.push(wgpu::BindGroupLayoutEntry {
        binding: 23, visibility: wgpu::ShaderStages::FRAGMENT,
        ty: wgpu::BindingType::Texture {
            sample_type: wgpu::TextureSampleType::Float { filterable: false },
            view_dimension: wgpu::TextureViewDimension::D2, multisampled: false,
        }, count: None,
    });
    }
    if bump_receiver {
      layout_entries.push(wgpu::BindGroupLayoutEntry {binding:25,visibility:wgpu::ShaderStages::FRAGMENT,
        ty:wgpu::BindingType::Texture {sample_type:wgpu::TextureSampleType::Float {filterable:true},view_dimension:wgpu::TextureViewDimension::D2,multisampled:false},count:None});
      layout_entries.push(wgpu::BindGroupLayoutEntry {binding:26,visibility:wgpu::ShaderStages::FRAGMENT,
        ty:wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),count:None});
    }
    if portable_module.is_some() {
        layout_entries.push(wgpu::BindGroupLayoutEntry {
            binding: 27, visibility: wgpu::ShaderStages::FRAGMENT,
            ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform,
                has_dynamic_offset: false, min_binding_size: std::num::NonZeroU64::new(4096) },
            count: None,
        });
    }
    let bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("Coin eight-unit texture program"),
        entries: &layout_entries,
    });

    let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
        label: Some("Coin Pipeline Layout"),
        bind_group_layouts: &[&bind_group_layout],
        push_constant_ranges: &[],
    });

    let mut instanced_entries = layout_entries.clone();
    instanced_entries[0].ty = wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform,
        has_dynamic_offset: true,
        min_binding_size: std::num::NonZeroU64::new(std::mem::size_of::<CoinWgpuUniforms>() as u64) };
    instanced_entries.push(wgpu::BindGroupLayoutEntry {
        binding: 24, visibility: wgpu::ShaderStages::VERTEX,
        ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Storage { read_only: true },
            has_dynamic_offset: false, min_binding_size: std::num::NonZeroU64::new(144) },
        count: None,
    });
    let instanced_bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("Coin opaque instance bindings"), entries: &instanced_entries,
    });
    let instanced_pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
        label: Some("Coin opaque instance pipeline"), bind_group_layouts: &[&instanced_bind_group_layout],
        push_constant_ranges: &[],
    });

    let eight_entries: Vec<_> = layout_entries.iter().copied()
        .filter(|entry| shadow_multitexture || !(10..18).contains(&entry.binding)).collect();
    let shadow_eight_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("Coin bounded eight-map scene bindings"), entries: &eight_entries,
    });
    let mut extra_entries = vec![wgpu::BindGroupLayoutEntry {
        binding: 0, visibility: wgpu::ShaderStages::VERTEX_FRAGMENT,
        ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform,
            has_dynamic_offset: false, min_binding_size: None }, count: None,
    }];
    for binding in (1..5).filter(|_| !shadow_multitexture) {
        extra_entries.push(wgpu::BindGroupLayoutEntry {
            binding, visibility: wgpu::ShaderStages::FRAGMENT,
            ty: wgpu::BindingType::Texture {
                sample_type: wgpu::TextureSampleType::Float { filterable: false },
                view_dimension: wgpu::TextureViewDimension::D2, multisampled: false,
            }, count: None,
        });
    }
    let shadow_extra_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("Coin extra four shadow maps"), entries: &extra_entries,
    });
    let shadow_eight_pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
        label: Some("Coin eight shadow pipeline"),
        bind_group_layouts: &[&shadow_eight_layout, &shadow_extra_layout], push_constant_ranges: &[],
    });

    SurfaceSamplingProfile {
        policy, multisampled_peel, sample_depth, shadow_multitexture, bump_receiver,
        shader_module: shader,
        instanced_shader, instanced_bind_group_layout, instanced_pipeline_layout,
        shadow_shader, shadow_eight_shader, shadow_eight_layout, shadow_extra_layout, shadow_eight_pipeline_layout,
        line_shader_module: line_shader,
        point_shader_module: point_shader,
        bind_group_layout,
        pipeline_layout,
        pipelines: Mutex::new(HashMap::new()),
        untextured_shaders: Mutex::new(HashMap::new()),
    }
}

impl DeviceState {
    fn bump_receiver_profile(&self, policy: SamplingPolicy) -> Arc<SurfaceSamplingProfile> {
        let key = (if policy == SamplingPolicy::Native { 0 } else { 1 }, false);
        let mut cache = self.bump_receivers.lock().unwrap();
        cache.entry(key).or_insert_with(|| Arc::new(create_surface_sampling_profile_impl(
            &self.device, &self.adapter, policy, false, false, true, true, None))).clone()
    }
    fn surface_profile(&self, policy: SamplingPolicy) -> &SurfaceSamplingProfile {
        match policy {
            SamplingPolicy::Native => &self.surface_native,
            SamplingPolicy::Portable => self.surface_portable.get_or_init(||
                create_surface_sampling_profile(&self.device, &self.adapter, policy, false, false, false)),
        }
    }
    fn sample_depth_surface_profile(&self, policy: SamplingPolicy) -> &SurfaceSamplingProfile {
        let cache = match policy {
            SamplingPolicy::Native => &self.surface_depth_native,
            SamplingPolicy::Portable => &self.surface_depth_portable,
        };
        cache.get_or_init(|| create_surface_sampling_profile(&self.device, &self.adapter, policy, false, true, false))
    }
    fn shadow_multitexture_surface_profile(&self, policy: SamplingPolicy) -> &SurfaceSamplingProfile {
        let cache = match policy { SamplingPolicy::Native => &self.surface_shadow_multitexture_native,
            SamplingPolicy::Portable => &self.surface_shadow_multitexture_portable };
        cache.get_or_init(|| create_surface_sampling_profile(&self.device, &self.adapter, policy, false, false, true))
    }
    fn shadow_multitexture_depth_surface_profile(&self, policy: SamplingPolicy) -> &SurfaceSamplingProfile {
        let cache = match policy { SamplingPolicy::Native => &self.surface_shadow_multitexture_depth_native,
            SamplingPolicy::Portable => &self.surface_shadow_multitexture_depth_portable };
        cache.get_or_init(|| create_surface_sampling_profile(&self.device, &self.adapter, policy, false, true, true))
    }
    fn shadow_multitexture_peeling_surface_profile(&self, policy: SamplingPolicy) -> &SurfaceSamplingProfile {
        let cache = match policy { SamplingPolicy::Native => &self.surface_shadow_multitexture_peel_native,
            SamplingPolicy::Portable => &self.surface_shadow_multitexture_peel_portable };
        cache.get_or_init(|| create_surface_sampling_profile(&self.device, &self.adapter, policy, true, false, true))
    }
    fn peeling_surface_profile(&self, policy: SamplingPolicy) -> &SurfaceSamplingProfile {
        let cache = match policy {
            SamplingPolicy::Native => &self.surface_peel_native,
            SamplingPolicy::Portable => &self.surface_peel_portable,
        };
        cache.get_or_init(|| create_surface_sampling_profile(&self.device, &self.adapter, policy, true, false, false))
    }
}

struct DeviceState {
    adapter: wgpu::Adapter,
    adapter_name: String,
    device: wgpu::Device,
    queue: wgpu::Queue,
    surface_native: SurfaceSamplingProfile,
    surface_portable: OnceLock<SurfaceSamplingProfile>,
    surface_depth_native: OnceLock<SurfaceSamplingProfile>,
    surface_depth_portable: OnceLock<SurfaceSamplingProfile>,
    surface_peel_native: OnceLock<SurfaceSamplingProfile>,
    surface_peel_portable: OnceLock<SurfaceSamplingProfile>,
    surface_shadow_multitexture_peel_native: OnceLock<SurfaceSamplingProfile>,
    surface_shadow_multitexture_peel_portable: OnceLock<SurfaceSamplingProfile>,
    shadow_multitexture_alpha_admission: Mutex<HashMap<(u32,u32),bool>>,
    surface_shadow_multitexture_native: OnceLock<SurfaceSamplingProfile>,
    surface_shadow_multitexture_portable: OnceLock<SurfaceSamplingProfile>,
    surface_shadow_multitexture_depth_native: OnceLock<SurfaceSamplingProfile>,
    surface_shadow_multitexture_depth_portable: OnceLock<SurfaceSamplingProfile>,
    shadow_multitexture_depth_admission: [OnceLock<bool>; 3],
    shadow_multitexture_admission: [OnceLock<bool>; 3],
    annotation_depth_pipeline: Mutex<HashMap<u32, wgpu::RenderPipeline>>,
    weighted_sample_support: Mutex<HashMap<u32, bool>>,
    peeling_sample_support: Mutex<HashMap<u32, bool>>,
    raster_image_pipeline: OnceLock<raster_image::Pipeline>,
    bump_pipelines: Mutex<HashMap<BumpPipelineKey, Arc<BumpPipelines>>>,
    bump_base_cache: Mutex<BumpBaseCache>,
    bump_receivers: Mutex<HashMap<(u32,bool),Arc<SurfaceSamplingProfile>>>,
    portable_profiles: Mutex<portable::ProfileCache>,
    multipass_programs: multipass::Cache,
    depth_readback: Mutex<Option<depth_transfer::DepthTransfer>>,
    depth_snapshot: Mutex<Option<depth_transfer::DepthTransfer>>,
    pipeline_compilations: AtomicU64,
    pipeline_hits: AtomicU64,
    cache: Mutex<GeometryCache>,
    default_texture: wgpu::Texture,
    default_texture_view: wgpu::TextureView,
    default_depth_view: wgpu::TextureView,
    default_sampler: wgpu::Sampler,
    texture_cache: Mutex<TextureCache>,
    sampler_cache: Mutex<SamplerCache>,
    camera_bindings: Mutex<Option<CameraGpuBindings>>,
    frame_buffers: Mutex<Option<FrameGpuBuffers>>,
    instanced_buffers: Mutex<Option<InstancedGpuBuffers>>,
    camera_bindings_reused: AtomicU32,
    camera_bindings_created: AtomicU32,
    draw_bind_groups: AtomicU32,
    uniform_chunks: AtomicU32,
    validated_scene: Option<Arc<ValidatedScene>>,
    camera_snapshot_policy: CameraSnapshotPolicy,
    readback_pool: Arc<Mutex<ReadbackPool>>,
    cached_offscreen_attachments: Option<CachedOffscreenAttachments>,
    target_depth_histories: HashMap<u64, TargetDepthHistory>,
    rtt_textures: Mutex<RttRegistry>,
    device_id: CoinWgpuDeviceId,
    generation: u64,
    completed_serial: Arc<AtomicU64>,
    last_submitted_serial: AtomicU64,
    lost: Arc<AtomicBool>,
    async_error: Arc<AtomicU32>,
    injected_fault: AtomicI32,
}

// Diagnostic sizes cover selected bridge-owned caches and primary attachments.
// They are nominal payload bytes, not driver allocations or total GPU memory.
fn trace_owned_resources(dev: &DeviceState, target: &str, width: u32, height: u32,
                         staging_color_bytes: u64, staging_depth_bytes: u64) {
    let geometry = dev.cache.lock().unwrap();
    let retired_geometry_bytes: u64 = geometry.deferred_release.iter()
        .map(|entry| entry.size_bytes).sum();
    let mut vertex_buffers = geometry.active_entries.len() + geometry.deferred_release.len();
    let mut index_buffers = geometry.active_entries.values()
        .filter(|(_, entry)| entry.index_buffer.is_some()).count()
        + geometry.deferred_release.iter()
            .filter(|entry| entry.index_buffer.is_some()).count();
    let mut geometry_active_bytes = geometry.total_active_geometry_bytes;
    drop(geometry);
    let frame = dev.frame_buffers.lock().unwrap();
    let frame_geometry_bytes = frame.as_ref().map_or(0, |entry| entry.geometry_bytes);
    geometry_active_bytes += frame_geometry_bytes;
    vertex_buffers += usize::from(frame.is_some());
    index_buffers += usize::from(frame.is_some());
    drop(frame);
    let instances = dev.instanced_buffers.lock().unwrap();
    if let Some(entry) = instances.as_ref() {
        geometry_active_bytes += entry.geometry_bytes;
        vertex_buffers += 1; index_buffers += 1;
        eprintln!("COIN_RENDER_PHASE rust_instance_resources geometry_bytes={} instance_bytes={} material_bytes={}",
            entry.geometry_bytes, entry.instance_bytes, entry.material_bytes);
    }
    drop(instances);
    let textures = dev.texture_cache.lock().unwrap();
    let texture_payload_bytes: u64 = textures.entries.values()
        .map(|entry| entry.size_bytes as u64).sum();
    let texture_count = textures.entries.len() + textures.retired.len();
    drop(textures);
    let rtt = dev.rtt_textures.lock().unwrap();
    let rtt_count = rtt.active.len() + rtt.retired.len();
    let rtt_nominal_bytes: u64 = rtt.active.values()
        .map(|entry| u64::from(entry.width) * u64::from(entry.height) * 4).sum::<u64>()
        + rtt.retired.iter().map(|entry| u64::from(entry.resource.width)
            * u64::from(entry.resource.height) * 4).sum::<u64>();
    drop(rtt);
    let pool = dev.readback_pool.lock().unwrap();
    let staging_pool_free_bytes = pool.free_bytes;
    let staging_pool_free_buffers = pool.free_count;
    drop(pool);
    let pixels = u64::from(width) * u64::from(height);
    let attachment_count = if target == "window" { 1 } else { 2 };
    let attachment_nominal_bytes = pixels * 4 * attachment_count;
    eprintln!("COIN_RENDER_PHASE rust_resources target={} geometry_active_bytes={} geometry_retired_bytes={} vertex_buffers={} index_buffers={} texture_cache_count={} texture_payload_bytes={} rtt_texture_count={} rtt_color_nominal_bytes={} attachment_textures={} attachment_nominal_bytes={} staging_frame_color_bytes={} staging_frame_depth_bytes={} staging_pool_free_bytes={} staging_pool_free_buffers={} gpu_memory_used_bytes=unavailable framebuffer_count=unavailable",
        target, geometry_active_bytes, retired_geometry_bytes, vertex_buffers, index_buffers,
        texture_count, texture_payload_bytes, rtt_count, rtt_nominal_bytes, attachment_count,
        attachment_nominal_bytes, staging_color_bytes, staging_depth_bytes,
        staging_pool_free_bytes, staging_pool_free_buffers);
    eprintln!("COIN_RENDER_PHASE rust_draw_resources frame_geometry_bytes={} uniform_chunks={} draw_bind_groups={}",
        frame_geometry_bytes, dev.uniform_chunks.load(Ordering::Relaxed),
        dev.draw_bind_groups.load(Ordering::Relaxed));
}

struct SurfaceRecord {
    published_serial: u64,
    surface: wgpu::Surface<'static>,
    native_desc: CoinWgpuNativeSurfaceDescriptor,
    config: Option<wgpu::SurfaceConfiguration>,
    framebuffer_size: (u32, u32),
    color_format: wgpu::TextureFormat,
    sample_count: u32,
    suspended: bool,
    needs_reconfigure: bool,
    surface_generation: u64,
    configured_device_generation: u64,
    depth_texture: Option<wgpu::Texture>,
    depth_view: Option<wgpu::TextureView>,
    msaa_color_texture: Option<wgpu::Texture>,
    msaa_color_view: Option<wgpu::TextureView>,
    presentation: Option<surface_present::Presentation>,
}

fn surface_wsi_tracing_enabled() -> bool {
    static ENABLED: OnceLock<bool> = OnceLock::new();
    *ENABLED.get_or_init(|| {
        std::env::var_os("COIN_WGPU_TRACE_WSI").is_some()
            || std::env::var_os("COIN_RENDER_TRACE_PHASES").is_some()
            || std::env::var_os("COIN_WGPU_TRACE_PHASES").is_some()
    })
}

// Keep the Coin publication context adjacent to the HAL's actual WSI calls.
// An injected result never claims that vkAcquireNextImageKHR was called.
fn acquire_surface_texture(record: &SurfaceRecord, surface_id: CoinWgpuSurfaceId,
    attempt: u32, injected: Option<wgpu::SurfaceError>, trace: bool)
    -> Result<wgpu::SurfaceTexture, wgpu::SurfaceError>
{
    let source = if injected.is_some() { "injected" } else { "actual" };
    if trace {
        eprintln!("COIN_WGPU_WSI stage=get_current_texture event=begin source={} pid={} thread={:?} surface={} generation={} device_generation={} published_serial={} last_submitted_serial={} attempt={} native_type={} handle_a={:#x} handle_b={:#x}",
            source, std::process::id(), std::thread::current().id(), surface_id,
            record.surface_generation, record.configured_device_generation,
            record.published_serial, LAST_SUBMITTED_SERIAL.load(Ordering::SeqCst), attempt,
            record.native_desc.r#type, record.native_desc.handle_a, record.native_desc.handle_b);
    }
    let start = trace.then(std::time::Instant::now);
    let result = match injected {
        Some(error) => Err(error),
        None => record.surface.get_current_texture(),
    };
    if let Some(start) = start {
        let status = match &result {
            Ok(_) => "Ok",
            Err(wgpu::SurfaceError::Timeout) => "Timeout",
            Err(wgpu::SurfaceError::Outdated) => "Outdated",
            Err(wgpu::SurfaceError::Lost) => "Lost",
            Err(wgpu::SurfaceError::OutOfMemory) => "OutOfMemory",
            Err(wgpu::SurfaceError::Other) => "Other",
        };
        eprintln!("COIN_WGPU_WSI stage=get_current_texture event=end source={} pid={} thread={:?} surface={} generation={} attempt={} result={} elapsed_ms={:.6}",
            source, std::process::id(), std::thread::current().id(), surface_id,
            record.surface_generation, attempt, status, start.elapsed().as_secs_f64() * 1000.0);
    }
    result
}

type ReadbackMapResult = Result<(), wgpu::BufferAsyncError>;

// Only unmapped, completed buffers may enter this device-owned pool. Keeping
// free buffers bounded avoids turning a resize sequence into unbounded RSS.
#[derive(Default)]
struct ReadbackPool {
    free: HashMap<u64, Vec<wgpu::Buffer>>,
    free_bytes: u64,
    free_count: usize,
}

impl ReadbackPool {
    const MAX_FREE_BYTES: u64 = 16 * 1024 * 1024;
    const MAX_BUFFER_BYTES: u64 = 8 * 1024 * 1024;
    const MAX_FREE_BUFFERS: usize = 16;

    fn acquire(&mut self, device: &wgpu::Device, size: u64) -> (wgpu::Buffer, bool) {
        let reused = self.free.get_mut(&size).and_then(Vec::pop);
        if let Some(buffer) = reused {
            if self.free.get(&size).is_some_and(Vec::is_empty) {
                self.free.remove(&size);
            }
            self.free_bytes -= size;
            self.free_count -= 1;
            return (buffer, true);
        }
        (device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("Coin Readback Staging Buffer"),
            size,
            usage: wgpu::BufferUsages::MAP_READ | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        }), false)
    }

    fn recycle(&mut self, buffer: wgpu::Buffer) {
        let size = buffer.size();
        if size > Self::MAX_BUFFER_BYTES
            || self.free_bytes.saturating_add(size) > Self::MAX_FREE_BYTES
            || self.free_count >= Self::MAX_FREE_BUFFERS {
            return;
        }
        let entries = self.free.entry(size).or_default();
        if entries.len() < 2 {
            entries.push(buffer);
            self.free_bytes += size;
            self.free_count += 1;
        }
    }
}

struct PendingReadback {
    ticket: CoinWgpuReadbackTicket,
    color: wgpu::Buffer,
    color_receiver: std::sync::mpsc::Receiver<ReadbackMapResult>,
    device_id: u64,
    color_ready: Option<Result<(), String>>,
    depth: Option<wgpu::Buffer>,
    depth_receiver: Option<std::sync::mpsc::Receiver<ReadbackMapResult>>,
    depth_ready: Option<Result<(), String>>,
    pool: Arc<Mutex<ReadbackPool>>,
}

struct RuntimeContext {
    instance: wgpu::Instance,
    surfaces: HashMap<CoinWgpuSurfaceId, SurfaceRecord>,
    device_state: Option<DeviceState>,
    device_generation: u64,
    pending_readbacks: HashMap<u64, PendingReadback>,
    retired_readbacks: Vec<PendingReadback>,
    extra_devices: HashMap<u64, DeviceState>,
    extra_generations: HashMap<u64, u64>,
    dead_readbacks: HashMap<u64, (CoinWgpuReadbackTicket, CoinWgpuStatus)>,
}

fn update_readback_mapping(job: &mut PendingReadback) {
    if job.color_ready.is_none() {
        match job.color_receiver.try_recv() {
            Ok(Ok(())) => job.color_ready = Some(Ok(())),
            Ok(Err(error)) => job.color_ready = Some(Err(format!("{:?}", error))),
            Err(std::sync::mpsc::TryRecvError::Disconnected) =>
                job.color_ready = Some(Err("Color mapping channel disconnected".to_string())),
            Err(std::sync::mpsc::TryRecvError::Empty) => (),
        }
    }
    if job.depth_ready.is_none() {
        if let Some(receiver) = job.depth_receiver.as_ref() {
            match receiver.try_recv() {
                Ok(Ok(())) => job.depth_ready = Some(Ok(())),
                Ok(Err(error)) => job.depth_ready = Some(Err(format!("{:?}", error))),
                Err(std::sync::mpsc::TryRecvError::Disconnected) =>
                    job.depth_ready = Some(Err("Depth mapping channel disconnected".to_string())),
                Err(std::sync::mpsc::TryRecvError::Empty) => (),
            }
        } else {
            job.depth_ready = Some(Ok(()));
        }
    }
}

fn reap_cancelled_readbacks(runtime: &mut RuntimeContext) {
    if let Some(device) = runtime.device_state.as_ref() {
        let _ = device.device.poll(wgpu::Maintain::Poll);
    }
    for device in runtime.extra_devices.values() {
        let _ = device.device.poll(wgpu::Maintain::Poll);
    }
    runtime.retired_readbacks.retain_mut(|job| {
        update_readback_mapping(job);
        if job.color_ready.is_none() || job.depth_ready.is_none() {
            return true;
        }
        if matches!(job.color_ready.as_ref(), Some(Ok(()))) {
            job.color.unmap();
            if let Ok(mut pool) = job.pool.lock() {
                pool.recycle(job.color.clone());
            }
        }
        if matches!(job.depth_ready.as_ref(), Some(Ok(()))) {
            if let Some(buffer) = job.depth.as_ref() {
                buffer.unmap();
                if let Ok(mut pool) = job.pool.lock() {
                    pool.recycle(buffer.clone());
                }
            }
        }
        false
    });
}

static RUNTIME_CTX: Mutex<Option<RuntimeContext>> = Mutex::new(None);
static RUNTIME_EXIT_STARTED: AtomicBool = AtomicBool::new(false);

extern "C" {
    fn atexit(callback: extern "C" fn()) -> std::os::raw::c_int;
}

// Rust statics have no process-exit Drop. Register after native initialization,
// rather than at library load: exit callbacks run in reverse registration order,
// so our GPU objects must be released before the driver's own exit callbacks.
// A new device or lazy program cache may initialize another native module and
// therefore registers again. Earlier callbacks are harmless once take() is done.
fn register_runtime_exit() -> Result<(), String> {
    if unsafe { atexit(shutdown_runtime_at_exit) } == 0 { Ok(()) }
    else { Err("Cannot register WebGPU runtime process-exit cleanup".into()) }
}

extern "C" fn shutdown_runtime_at_exit() {
    // Drop directly on the thread executing process exit: no transfer to a
    // cleanup thread or native-context affinity change. The vendored wgpu-core
    // tolerates unavailable diagnostic TLS during these late resource Drops.
    // Recover a poisoned lock so an earlier failed call cannot retain a device.
    let _ = std::panic::catch_unwind(|| {
        let runtime = {
            let mut guard = RUNTIME_CTX.lock().unwrap_or_else(|error| error.into_inner());
            RUNTIME_EXIT_STARTED.store(true, Ordering::SeqCst);
            guard.take()
        };
        if let Some(mut runtime) = runtime {
            // Serialize with API users through RUNTIME_CTX above, then let GPU
            // completions run without that mutex. Callbacks use only their own
            // signals/channels. No ticket or resource is invalidated during
            // normal operation; this path is entered only by process exit.
            if let Some(state) = runtime.device_state.as_ref() {
                let _ = state.device.poll(wgpu::Maintain::Wait);
            }
            for state in runtime.extra_devices.values() {
                let _ = state.device.poll(wgpu::Maintain::Wait);
            }
            // Surface drop unconfigures presentation while devices/Instance
            // are still live. Retire mapped buffers before their owning pools
            // and cached programs, then release all logical devices.
            runtime.surfaces.clear();
            runtime.pending_readbacks.clear();
            runtime.retired_readbacks.clear();
            runtime.device_state = None;
            runtime.extra_devices.clear();
            drop(runtime);
        }
    });
}
#[cfg(test)]
fn gpu_test_instance() -> wgpu::Instance {
    static INSTANCE: std::sync::OnceLock<wgpu::Instance> = std::sync::OnceLock::new();
    let backends = wgpu::Backends::from_env().unwrap_or(wgpu::Backends::all());
    let create = || wgpu::Instance::new(&wgpu::InstanceDescriptor {
        backends, ..Default::default()
    });
    // A GL adapter's EGL context is thread-bound. The Vulkan tests can share
    // one Instance while independently requesting logical Devices in parallel.
    if backends == wgpu::Backends::GL { create() }
    else { INSTANCE.get_or_init(create).clone() }
}
#[cfg(target_os = "android")]
static ANDROID_RUNTIME_GENERATION: AtomicU64 = AtomicU64::new(0);
static VALIDATED_FRAME_CACHE: Mutex<Option<(u64, Vec<composition::CompositionItem>)>> =
    Mutex::new(None);
static NEXT_SURFACE_ID: AtomicU64 = AtomicU64::new(1);

static FAULT_INJECTION: AtomicI32 = AtomicI32::new(0);
static FAULT_INJECTION_ASYNC: AtomicI32 = AtomicI32::new(0);

static FAULT_AFTER_SUBMITS: AtomicI32 = AtomicI32::new(-1);
static FAULT_AFTER_CODE: AtomicI32 = AtomicI32::new(0);
// Fault injection codes for surface
pub const FAULT_SURFACE_TIMEOUT: i32 = 101;
pub const FAULT_SURFACE_OUTDATED_ONCE: i32 = 102;
pub const FAULT_SURFACE_LOST_ONCE: i32 = 103;
pub const FAULT_SURFACE_LOST_PERSISTENT: i32 = 104;
pub const FAULT_SURFACE_OUT_OF_MEMORY: i32 = 105;
pub const FAULT_SURFACE_OTHER: i32 = 106;
pub const FAULT_CONFIGURE_FAILURE: i32 = 107;
// Unlike 101, this reaches normal acquisition after frame validation/preparation.
pub const FAULT_SURFACE_TIMEOUT_AT_ACQUIRE: i32 = 108;
pub const FAULT_CACHE_ALLOC_FAIL: i32 = 201;
pub const FAULT_RTT_COLOR_ALLOC: i32 = 301;
pub const FAULT_RTT_COLOR_VIEW: i32 = 302;
pub const FAULT_RTT_DEPTH_ALLOC: i32 = 303;
pub const FAULT_RTT_BIND_GROUP: i32 = 304;
pub const FAULT_SHADOW_MAP_ALLOC: i32 = 305;

static FAULT_SURFACE_OUTDATED_COUNT: AtomicI32 = AtomicI32::new(0);
static FAULT_SURFACE_LOST_COUNT: AtomicI32 = AtomicI32::new(0);

// Asynchronous error reporting without deadlocking RUNTIME_CTX
static DEVICE_LOST_OCCURRED: AtomicBool = AtomicBool::new(false);
static LAST_ASYNC_ERROR_KIND: AtomicU32 = AtomicU32::new(0); // 0=None, 1=OOM, 2=BackendError
static LAST_ASYNC_ERROR_MSG: Mutex<String> = Mutex::new(String::new());

fn validate_slice<'a, T>(
    ptr: *const T,
    count: u64,
    name: &str,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> Result<&'a [T], CoinWgpuStatus> {
    if count == 0 {
        return Ok(&[]);
    }
    if ptr.is_null() {
        set_error(
            error_buf,
            error_buf_len,
            &format!("Null {} pointer with non-zero count", name),
        );
        return Err(CoinWgpuStatus::InvalidArgument);
    }
    let align = std::mem::align_of::<T>();
    if (ptr as usize) % align != 0 {
        set_error(
            error_buf,
            error_buf_len,
            &format!(
                "Misaligned {} pointer: {:#x} not aligned to {}",
                name, ptr as usize, align
            ),
        );
        return Err(CoinWgpuStatus::InvalidArgument);
    }
    let usize_count = match usize::try_from(count) {
        Ok(c) => c,
        Err(_) => {
            set_error(
                error_buf,
                error_buf_len,
                &format!("{} count {} exceeds usize range", name, count),
            );
            return Err(CoinWgpuStatus::InvalidArgument);
        }
    };
    let total_bytes = match usize_count.checked_mul(std::mem::size_of::<T>()) {
        Some(b) => b,
        None => {
            set_error(
                error_buf,
                error_buf_len,
                &format!("{} byte size overflow", name),
            );
            return Err(CoinWgpuStatus::InvalidArgument);
        }
    };
    if total_bytes > (isize::MAX as usize) {
        set_error(
            error_buf,
            error_buf_len,
            &format!(
                "{} total byte size {} exceeds isize::MAX",
                name, total_bytes
            ),
        );
        return Err(CoinWgpuStatus::InvalidArgument);
    }
    Ok(unsafe { std::slice::from_raw_parts(ptr, usize_count) })
}

fn validate_slice_mut<'a, T>(
    ptr: *mut T,
    count: u64,
    name: &str,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> Result<&'a mut [T], CoinWgpuStatus> {
    if count == 0 {
        return Ok(&mut []);
    }
    if ptr.is_null() {
        set_error(
            error_buf,
            error_buf_len,
            &format!("Null {} pointer with non-zero count", name),
        );
        return Err(CoinWgpuStatus::InvalidArgument);
    }
    let align = std::mem::align_of::<T>();
    if (ptr as usize) % align != 0 {
        set_error(
            error_buf,
            error_buf_len,
            &format!(
                "Misaligned {} pointer: {:#x} not aligned to {}",
                name, ptr as usize, align
            ),
        );
        return Err(CoinWgpuStatus::InvalidArgument);
    }
    let usize_count = match usize::try_from(count) {
        Ok(c) => c,
        Err(_) => {
            set_error(
                error_buf,
                error_buf_len,
                &format!("{} count {} exceeds usize range", name, count),
            );
            return Err(CoinWgpuStatus::InvalidArgument);
        }
    };
    let total_bytes = match usize_count.checked_mul(std::mem::size_of::<T>()) {
        Some(b) => b,
        None => {
            set_error(
                error_buf,
                error_buf_len,
                &format!("{} byte size overflow", name),
            );
            return Err(CoinWgpuStatus::InvalidArgument);
        }
    };
    if total_bytes > (isize::MAX as usize) {
        set_error(
            error_buf,
            error_buf_len,
            &format!(
                "{} total byte size {} exceeds isize::MAX",
                name, total_bytes
            ),
        );
        return Err(CoinWgpuStatus::InvalidArgument);
    }
    Ok(unsafe { std::slice::from_raw_parts_mut(ptr, usize_count) })
}

// Callers validate the mapped extent and output length first. When the WebGPU
// row pitch has no padding, one contiguous copy avoids per-row slice work.
fn copy_color_rows(dst: &mut [u8], src: &[u8], width: u32, height: u32, pitch: u32) {
    let row_bytes = width as usize * 4;
    let rows = height as usize;
    if pitch as usize == row_bytes &&
        std::env::var("COIN_WGPU_FORCE_ROW_COPY").as_deref() != Ok("1") {
        let bytes = row_bytes * rows;
        dst[..bytes].copy_from_slice(&src[..bytes]);
    } else {
        for y in 0..rows {
            let source = y * pitch as usize;
            let destination = y * row_bytes;
            dst[destination..destination + row_bytes]
                .copy_from_slice(&src[source..source + row_bytes]);
        }
    }
}

fn set_error(buf: *mut std::os::raw::c_char, buf_len: usize, msg: &str) {
    if !buf.is_null() && buf_len > 0 {
        let bytes = msg.as_bytes();
        let copy_len = bytes.len().min(buf_len - 1);
        unsafe {
            std::ptr::copy_nonoverlapping(
                bytes.as_ptr() as *const std::os::raw::c_char,
                buf,
                copy_len,
            );
            *buf.add(copy_len) = 0;
        }
    }
}

unsafe fn create_surface_from_descriptor(
    instance: &wgpu::Instance,
    desc: &CoinWgpuNativeSurfaceDescriptor,
) -> Result<wgpu::Surface<'static>, String> {
    if desc.r#type == 1 {
        #[cfg(all(unix, not(target_os = "android")))]
        {
            // Xlib
            let display_ptr = desc.handle_a as *mut std::ffi::c_void;
            let window_xid = desc.handle_b.try_into()
                .map_err(|_| "Xlib window ID exceeds the platform handle size".to_string())?;
            let nn_display = std::ptr::NonNull::new(display_ptr)
                .ok_or_else(|| "Display pointer must be non-null".to_string())?;
            let display_handle = XlibDisplayHandle::new(Some(nn_display), 0);
            let window_handle = XlibWindowHandle::new(window_xid);

            let raw_display = RawDisplayHandle::Xlib(display_handle);
            let raw_window = RawWindowHandle::Xlib(window_handle);

            let target = wgpu::SurfaceTargetUnsafe::RawHandle {
                raw_display_handle: raw_display,
                raw_window_handle: raw_window,
            };
            instance
                .create_surface_unsafe(target)
                .map_err(|e| format!("Failed to create X11 surface: {}", e))
        }
        #[cfg(not(all(unix, not(target_os = "android"))))]
        {
            Err("Xlib surface requires a Unix platform with X11".to_string())
        }
    } else if desc.r#type == 2 {
        #[cfg(target_os = "linux")]
        {
            let display = std::ptr::NonNull::new(desc.handle_a as *mut std::ffi::c_void)
                .ok_or_else(|| "wl_display must be non-null".to_string())?;
            let surface = std::ptr::NonNull::new(desc.handle_b as *mut std::ffi::c_void)
                .ok_or_else(|| "wl_surface must be non-null".to_string())?;
            let target = wgpu::SurfaceTargetUnsafe::RawHandle {
                raw_display_handle: RawDisplayHandle::Wayland(WaylandDisplayHandle::new(display)),
                raw_window_handle: RawWindowHandle::Wayland(WaylandWindowHandle::new(surface)),
            };
            instance.create_surface_unsafe(target)
                .map_err(|e| format!("Failed to create Wayland surface: {}", e))
        }
        #[cfg(not(target_os = "linux"))]
        {
            Err("Wayland surface requires Linux".to_string())
        }
    } else if desc.r#type == 3 {
        #[cfg(target_os = "windows")]
        {
            let hwnd = std::num::NonZeroIsize::new(desc.handle_b as isize)
                .ok_or_else(|| "HWND must be non-null".to_string())?;
            let mut window_handle = Win32WindowHandle::new(hwnd);
            window_handle.hinstance = std::num::NonZeroIsize::new(desc.handle_a as isize);
            let target = wgpu::SurfaceTargetUnsafe::RawHandle {
                raw_display_handle: RawDisplayHandle::Windows(WindowsDisplayHandle::new()),
                raw_window_handle: RawWindowHandle::Win32(window_handle),
            };
            instance.create_surface_unsafe(target)
                .map_err(|e| format!("Failed to create Win32 surface: {}", e))
        }
        #[cfg(not(target_os = "windows"))]
        {
            Err("Win32 surface requires Windows".to_string())
        }
    } else if desc.r#type == 4 {
        #[cfg(target_os = "macos")]
        {
            if desc.handle_a == 0 {
                return Err("CAMetalLayer must be non-null".to_string());
            }
            instance.create_surface_unsafe(wgpu::SurfaceTargetUnsafe::CoreAnimationLayer(
                desc.handle_a as *mut std::ffi::c_void,
            )).map_err(|e| format!("Failed to create CAMetalLayer surface: {}", e))
        }
        #[cfg(not(target_os = "macos"))]
        {
            Err("AppKit layer requires macOS".to_string())
        }
    } else if desc.r#type == 5 {
        #[cfg(target_os = "android")]
        {
            let window = std::ptr::NonNull::new(desc.handle_a as *mut std::ffi::c_void)
                .ok_or_else(|| "ANativeWindow must be non-null".to_string())?;
            let target = wgpu::SurfaceTargetUnsafe::RawHandle {
                raw_display_handle: RawDisplayHandle::Android(AndroidDisplayHandle::new()),
                raw_window_handle: RawWindowHandle::AndroidNdk(AndroidNdkWindowHandle::new(window)),
            };
            instance.create_surface_unsafe(target)
                .map_err(|e| format!("Failed to create Android NDK surface: {}", e))
        }
        #[cfg(not(target_os = "android"))]
        {
            Err("Android NDK surface requires Android".to_string())
        }
    } else {
        Err("Native surface platform not supported".to_string())
    }
}

fn init_runtime_if_needed() -> Result<(), String> {
    #[cfg(target_os = "android")]
    android_log::init();
    let mut guard = RUNTIME_CTX.lock().map_err(|e| e.to_string())?;
    if RUNTIME_EXIT_STARTED.load(Ordering::SeqCst) {
        return Err("WebGPU runtime is shutting down with the process".into());
    }
    if guard.is_none() {
        *guard = Some(RuntimeContext {
            instance: wgpu::Instance::new(&wgpu::InstanceDescriptor::from_env_or_default()),
            surfaces: HashMap::new(),
            device_state: None,
            device_generation: {
                #[cfg(target_os = "android")]
                { ANDROID_RUNTIME_GENERATION.load(Ordering::SeqCst) }
                #[cfg(not(target_os = "android"))]
                { 0 }
            },
            pending_readbacks: HashMap::new(),
            retired_readbacks: Vec::new(),
            extra_devices: HashMap::new(),
            extra_generations: HashMap::new(),
            dead_readbacks: HashMap::new(),
        });
        if let Err(error) = register_runtime_exit() {
            *guard = None;
            return Err(error);
        }
    }
    Ok(())
}

// NativeActivity may finish its render thread while the process stays alive.
// Drop the idle EGL/device on that thread, before it exits. Never invalidate a
// surviving target, direct RTT publication, explicit device, or readback ticket.
#[cfg(target_os = "android")]
#[no_mangle]
pub extern "C" fn coin_wgpu_release_idle_android_runtime() -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        let mut guard = match RUNTIME_CTX.lock() {
            Ok(guard) => guard,
            Err(_) => return CoinWgpuStatus::BackendError,
        };
        let Some(runtime) = guard.as_mut() else { return CoinWgpuStatus::Ok; };
        reap_cancelled_readbacks(runtime);
        if !runtime.surfaces.is_empty() || !runtime.pending_readbacks.is_empty()
            || !runtime.retired_readbacks.is_empty() || !runtime.extra_devices.is_empty()
            || !runtime.extra_generations.is_empty()
        {
            return CoinWgpuStatus::NotReady;
        }
        if let Some(device) = runtime.device_state.as_ref() {
            match device.rtt_textures.lock() {
                Ok(registry) if registry.active.is_empty() => (),
                _ => return CoinWgpuStatus::NotReady,
            }
            let _ = device.device.poll(wgpu::Maintain::Wait);
        }
        let Some(next_generation) = runtime.device_generation.checked_add(1) else {
            return CoinWgpuStatus::BackendError;
        };
        // Advance the epoch across process-local Activity lifetimes. Global
        // ticket/token/submission IDs are intentionally never reset.
        ANDROID_RUNTIME_GENERATION.store(next_generation, Ordering::SeqCst);
        *guard = None;
        DEVICE_LOST_OCCURRED.store(false, Ordering::SeqCst);
        log::info!("Released idle Android renderer; next_generation={next_generation}");
        CoinWgpuStatus::Ok
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

fn renderer_backend(renderer: u32) -> Result<Option<wgpu::Backends>, String> {
    match renderer {
        0 => Ok(None),
        1 => Ok(Some(wgpu::Backends::VULKAN)),
        2 => Ok(Some(wgpu::Backends::GL)),
        4 => Ok(Some(wgpu::Backends::DX12)),
        5 => Ok(Some(wgpu::Backends::METAL)),
        _ => Err("Invalid requested surface renderer".to_string()),
    }
}

fn adapter_renderer(backend: wgpu::Backend) -> u32 {
    match backend {
        wgpu::Backend::Vulkan => 1,
        wgpu::Backend::Gl => 2,
        wgpu::Backend::Dx12 => 4,
        wgpu::Backend::Metal => 5,
        _ => 3,
    }
}

fn get_or_init_device_impl<'a>(
    runtime: &'a mut RuntimeContext,
    target_surface: Option<&wgpu::Surface>,
    isolated: bool,
    requested_renderer: u32,
) -> Result<&'a mut DeviceState, String> {
    // Check if device loss occurred asynchronously
    if !isolated && DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst) {
        runtime.device_state = None;
        runtime.device_generation += 1;
        LAST_ASYNC_ERROR_KIND.store(0, Ordering::SeqCst);
        if let Ok(mut lock) = LAST_ASYNC_ERROR_MSG.lock() {
            lock.clear();
        }
    }

    let backend = renderer_backend(requested_renderer)?;
    if runtime.device_state.is_some() {
        if requested_renderer != 0 &&
            adapter_renderer(runtime.device_state.as_ref().unwrap().adapter.get_info().backend) != requested_renderer {
            return Err("Requested renderer does not match the active wgpu adapter; no fallback was applied".to_string());
        }
        return Ok(runtime.device_state.as_mut().unwrap());
    }

    let adapter = if let Some(backends) = backend {
        runtime.instance.enumerate_adapters(backends).into_iter().find(|candidate| {
            log::debug!("Requested renderer candidate: {:?}", candidate.get_info());
            if candidate.get_info().device_type == wgpu::DeviceType::Cpu { return false; }
            target_surface.map_or(true, |surface| {
                let caps = surface.get_capabilities(candidate);
                log::debug!("Surface capabilities: {:?}", caps);
                !caps.formats.is_empty() && !caps.present_modes.is_empty()
            })
        }).ok_or_else(|| "No physical adapter for the requested renderer and surface".to_string())?
    } else {
        block_on(runtime.instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            compatible_surface: target_surface,
            force_fallback_adapter: false,
        })).ok_or_else(|| "No compatible GPU adapter found".to_string())?
    };

    let adapter_info = adapter.get_info();
    let adapter_name = format!("{} ({:?})", adapter_info.name, adapter_info.backend);

    // Hardware timestamps are strictly opt-in and never required for normal
    // rendering. An unsupported adapter still runs the CPU phase trace.
    let timestamp_features = wgpu::Features::TIMESTAMP_QUERY
        | wgpu::Features::TIMESTAMP_QUERY_INSIDE_ENCODERS;
    let enable_timestamps = std::env::var_os("COIN_RENDER_TRACE_PHASES").or_else(|| std::env::var_os("COIN_WGPU_TRACE_PHASES")).is_some()
        && std::env::var("COIN_WGPU_GPU_TIMESTAMPS").as_deref() == Ok("1")
        && adapter.features().contains(timestamp_features);
    let mut required_features = if enable_timestamps { timestamp_features }
        else { wgpu::Features::empty() };
    if adapter.features().contains(wgpu::Features::FLOAT32_FILTERABLE) {
        required_features |= wgpu::Features::FLOAT32_FILTERABLE;
    }
    if adapter.features().contains(wgpu::Features::TEXTURE_COMPRESSION_BC) {
        required_features |= wgpu::Features::TEXTURE_COMPRESSION_BC;
    }
    // MULTISAMPLE_X2 is format-specific in wgpu. Enabling the adapter feature
    // here makes the advertised 2x capability usable by future target profiles.
    if adapter.features().contains(wgpu::Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES) {
        required_features |= wgpu::Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES;
    }
    let mut required_limits = wgpu::Limits::default();
    // Negotiate the real extended interface without excluding devices that
    // still support the established smaller profiles.
    if adapter.limits().max_inter_stage_shader_components >= 80 {
        required_limits.max_inter_stage_shader_components = 80;
    }
    let (device, queue) = block_on(adapter.request_device(
        &wgpu::DeviceDescriptor {
            label: Some("Coin3D WebGPU Device"),
            required_features,
            required_limits,
            memory_hints: wgpu::MemoryHints::Performance,
        },
        None,
    ))
    .map_err(|e| format!("Failed to create device: {}", e))?;

    let lost_signal = Arc::new(AtomicBool::new(false));
    let error_signal = Arc::new(AtomicU32::new(0));
    if isolated {
        let lost = lost_signal.clone();
        device.set_device_lost_callback(move |_, _| {
            lost.store(true, Ordering::SeqCst);
        });
        let error_kind = error_signal.clone();
        device.on_uncaptured_error(Box::new(move |error: wgpu::Error| {
            let _ = error_kind.compare_exchange(0,
                if matches!(error, wgpu::Error::OutOfMemory { .. }) { 1 } else { 2 },
                Ordering::SeqCst, Ordering::SeqCst);
        }));
    } else {
        device.set_device_lost_callback(|reason, message| {
            DEVICE_LOST_OCCURRED.store(true, Ordering::SeqCst);
            if let Ok(mut lock) = LAST_ASYNC_ERROR_MSG.lock() {
                *lock = format!("WebGPU device lost ({:?}): {}", reason, message);
            }
        });
        device.on_uncaptured_error(Box::new(|error: wgpu::Error| {
            if let Ok(mut lock) = LAST_ASYNC_ERROR_MSG.lock() {
                if LAST_ASYNC_ERROR_KIND.compare_exchange(0,
                    if matches!(error, wgpu::Error::OutOfMemory { .. }) { 1 } else { 2 },
                    Ordering::SeqCst, Ordering::SeqCst).is_ok() {
                    *lock = format!("WebGPU uncaptured error: {}", error);
                }
            }
        }));
    }

    let surface_native = create_surface_sampling_profile(&device, &adapter, SamplingPolicy::Native, false, false, false);

    if !isolated {
        DEVICE_LOST_OCCURRED.store(false, Ordering::SeqCst);
        LAST_ASYNC_ERROR_KIND.store(0, Ordering::SeqCst);
    }

    let default_texture = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("Coin Default 1x1 White Texture"),
        size: wgpu::Extent3d {
            width: 1,
            height: 1,
            depth_or_array_layers: 1,
        },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Rgba8Unorm,
        usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
        view_formats: &[],
    });
    queue.write_texture(
        wgpu::ImageCopyTexture {
            texture: &default_texture,
            mip_level: 0,
            origin: wgpu::Origin3d::ZERO,
            aspect: wgpu::TextureAspect::All,
        },
        &[255, 255, 255, 255],
        wgpu::ImageDataLayout {
            offset: 0,
            bytes_per_row: Some(4),
            rows_per_image: Some(1),
        },
        wgpu::Extent3d {
            width: 1,
            height: 1,
            depth_or_array_layers: 1,
        },
    );
    let default_texture_view = default_texture.create_view(&wgpu::TextureViewDescriptor::default());
    let default_sampler = device.create_sampler(&wgpu::SamplerDescriptor {
        label: Some("Coin Default Sampler"),
        address_mode_u: wgpu::AddressMode::Repeat,
        address_mode_v: wgpu::AddressMode::Repeat,
        address_mode_w: wgpu::AddressMode::Repeat,
        mag_filter: wgpu::FilterMode::Linear,
        min_filter: wgpu::FilterMode::Linear,
        mipmap_filter: wgpu::FilterMode::Nearest,
        ..Default::default()
    });

    let default_depth_view = device
        .create_texture(&wgpu::TextureDescriptor {
            label: Some("Coin unused depth binding"),
            size: wgpu::Extent3d {
                width: 1,
                height: 1,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Depth32Float,
            usage: wgpu::TextureUsages::TEXTURE_BINDING,
            view_formats: &[],
        })
        .create_view(&wgpu::TextureViewDescriptor::default());
    runtime.device_state = Some(DeviceState {
        adapter,
        adapter_name,
        device,
        queue,
        surface_native,
        surface_portable: OnceLock::new(),
        surface_depth_native: OnceLock::new(),
        surface_depth_portable: OnceLock::new(),
        surface_peel_native: OnceLock::new(),
        surface_peel_portable: OnceLock::new(),
        surface_shadow_multitexture_peel_native: OnceLock::new(),
        surface_shadow_multitexture_peel_portable: OnceLock::new(),
        shadow_multitexture_alpha_admission: Mutex::new(HashMap::new()),
        surface_shadow_multitexture_native: OnceLock::new(),
        surface_shadow_multitexture_portable: OnceLock::new(),
        surface_shadow_multitexture_depth_native: OnceLock::new(),
        surface_shadow_multitexture_depth_portable: OnceLock::new(),
        shadow_multitexture_depth_admission: std::array::from_fn(|_| OnceLock::new()),
        shadow_multitexture_admission: std::array::from_fn(|_| OnceLock::new()),
        annotation_depth_pipeline: Mutex::new(HashMap::new()),
        weighted_sample_support: Mutex::new(HashMap::new()),
        peeling_sample_support: Mutex::new(HashMap::new()),
        raster_image_pipeline: OnceLock::new(),
        bump_pipelines: Mutex::new(HashMap::new()),
        bump_base_cache: Mutex::new(BumpBaseCache::default()),
        bump_receivers: Mutex::new(HashMap::new()),
        portable_profiles: Mutex::new(portable::ProfileCache::default()),
        multipass_programs: multipass::Cache::default(),
        depth_readback: Mutex::new(None),
        depth_snapshot: Mutex::new(None),
        pipeline_compilations: AtomicU64::new(0),
        pipeline_hits: AtomicU64::new(0),
        cache: Mutex::new(GeometryCache::default()),
        default_texture,
        default_texture_view,
        default_depth_view,
        default_sampler,
        texture_cache: Mutex::new(TextureCache::default()),
        sampler_cache: Mutex::new(SamplerCache::default()),
        camera_bindings: Mutex::new(None),
        frame_buffers: Mutex::new(None),
        instanced_buffers: Mutex::new(None),
        camera_bindings_reused: AtomicU32::new(0),
        camera_bindings_created: AtomicU32::new(0),
        draw_bind_groups: AtomicU32::new(0),
        uniform_chunks: AtomicU32::new(0),
        validated_scene: None,
        camera_snapshot_policy: CameraSnapshotPolicy::default(),
        readback_pool: Arc::new(Mutex::new(ReadbackPool::default())),
        cached_offscreen_attachments: None,
        target_depth_histories: HashMap::new(),
        rtt_textures: Mutex::new(RttRegistry::default()),
        device_id: 0,
        generation: runtime.device_generation,
        completed_serial: Arc::new(AtomicU64::new(0)),
        last_submitted_serial: AtomicU64::new(0),
        lost: lost_signal,
        async_error: error_signal,
        injected_fault: AtomicI32::new(0),
    });

    if let Err(error) = register_runtime_exit() {
        runtime.device_state = None;
        return Err(error);
    }
    Ok(runtime.device_state.as_mut().unwrap())
}
fn get_or_init_device<'a>(
    runtime: &'a mut RuntimeContext,
    target_surface: Option<&wgpu::Surface>,
) -> Result<&'a mut DeviceState, String> {
    get_or_init_device_impl(runtime, target_surface, false, 0)

}
fn make_extra_device(runtime: &RuntimeContext, id: CoinWgpuDeviceId, generation: u64) -> Result<DeviceState, String> {
    // Only the default context owns surfaces. Every extra context requests its
    // own logical Device/Queue. Share the process runtime's Instance so device
    // recreation does not race Vulkan loader instance teardown on other threads.
    let mut isolated = RuntimeContext {
        instance: runtime.instance.clone(),
        surfaces: HashMap::new(),
        device_state: None,
        device_generation: 0,
        pending_readbacks: HashMap::new(),
        retired_readbacks: Vec::new(),
        extra_devices: HashMap::new(),
        extra_generations: HashMap::new(),
        dead_readbacks: HashMap::new(),
    };
    get_or_init_device_impl(&mut isolated, None, true, 0)?;
    let mut state = isolated.device_state.take().unwrap();
    state.device_id = id;
    state.generation = generation;
    Ok(state)
}

fn invalidate_extra_readbacks(runtime: &mut RuntimeContext, id: CoinWgpuDeviceId) {
    runtime.pending_readbacks.retain(|token, job| {
        if job.device_id == id {
            runtime.dead_readbacks.insert(*token, (job.ticket, CoinWgpuStatus::DeviceLost));
            false
        } else { true }
    });
    runtime.retired_readbacks.retain(|job| job.device_id != id);
    if runtime.dead_readbacks.len() > 1024 { runtime.dead_readbacks.clear(); }
}

fn lose_extra_device(runtime: &mut RuntimeContext, id: CoinWgpuDeviceId) {
    runtime.extra_devices.remove(&id);
    if let Some(generation) = runtime.extra_generations.get_mut(&id) {
        *generation = generation.wrapping_add(1);
    }
    invalidate_extra_readbacks(runtime, id);
}

fn offscreen_device<'a>(runtime: &'a mut RuntimeContext, id: CoinWgpuDeviceId)
    -> Result<&'a mut DeviceState, String> {
    if id == 0 {
        return get_or_init_device(runtime, None);
    }
    let generation = *runtime.extra_generations.get(&id)
        .ok_or_else(|| format!("Unknown or destroyed WebGPU device {}", id))?;
    if !runtime.extra_devices.contains_key(&id) {
        runtime.extra_devices.insert(id, make_extra_device(runtime, id, generation)?);
    }
    Ok(runtime.extra_devices.get_mut(&id).unwrap())
}

#[no_mangle]
pub extern "C" fn coin_wgpu_device_create(
    out_id: *mut u64,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if out_id.is_null() || (out_id as usize) % std::mem::align_of::<u64>() != 0 {
            set_error(error_buf, error_buf_len, "Null or misaligned device ID output");
            return CoinWgpuStatus::InvalidArgument;
        }
        unsafe { *out_id = 0; }
        if let Err(message) = init_runtime_if_needed() {
            set_error(error_buf, error_buf_len, &message);
            return CoinWgpuStatus::BackendError;
        }
        let mut guard = RUNTIME_CTX.lock().unwrap();
        let runtime = guard.as_mut().unwrap();
        let id = NEXT_DEVICE_ID.fetch_add(1, Ordering::SeqCst);
        if id == 0 {
            set_error(error_buf, error_buf_len, "WebGPU device ID space exhausted");
            return CoinWgpuStatus::OutOfMemory;
        }
        let state = match make_extra_device(runtime, id, id) {
            Ok(state) => state,
            Err(message) => {
                set_error(error_buf, error_buf_len, &message);
                return CoinWgpuStatus::NotReady;
            }
        };
        runtime.extra_generations.insert(id, id);
        runtime.extra_devices.insert(id, state);
        unsafe { *out_id = id; }
        CoinWgpuStatus::Ok
    }).unwrap_or_else(|_| {
        set_error(error_buf, error_buf_len, "Panic while creating WebGPU device");
        CoinWgpuStatus::BackendError
    })
}

#[no_mangle]
pub extern "C" fn coin_wgpu_device_destroy(id: u64) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if id == 0 { return CoinWgpuStatus::InvalidArgument; }
        let mut guard = RUNTIME_CTX.lock().unwrap();
        let Some(runtime) = guard.as_mut() else { return CoinWgpuStatus::InvalidArgument; };
        if runtime.extra_generations.remove(&id).is_none() {
            return CoinWgpuStatus::InvalidArgument;
        }
        runtime.extra_devices.remove(&id);
        invalidate_extra_readbacks(runtime, id);
        CoinWgpuStatus::Ok
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_inject_device_fault(id: u64, code: i32) {
    if id == 0 {
        FAULT_INJECTION.store(code, Ordering::SeqCst);
        return;
    }
    if let Ok(mut guard) = RUNTIME_CTX.lock() {
        if let Some(state) = guard.as_mut()
            .and_then(|runtime| runtime.extra_devices.get_mut(&id)) {
            state.injected_fault.store(code, Ordering::SeqCst);
        }
    }
}


fn configure_surface_record(
    record: &mut SurfaceRecord,
    device_state: &DeviceState,
    device_generation: u64,
    width: u32,
    height: u32,
) -> Result<(), String> {
    record.published_serial = 0;
    record.framebuffer_size = (width, height);
    if width == 0 || height == 0 {
        record.suspended = true;
        record.needs_reconfigure = true;
        record.depth_texture = None;
        record.depth_view = None;
        record.msaa_color_texture = None;
        record.msaa_color_view = None;
        record.presentation = None;
        return Ok(());
    }

    let caps = record.surface.get_capabilities(&device_state.adapter);
    if caps.formats.is_empty() {
        return Err("Surface capabilities report empty format list".to_string());
    }
    if caps.present_modes.is_empty() {
        return Err("Surface capabilities report empty present mode list".to_string());
    }

    // Negotiate format: Bgra8Unorm -> Rgba8Unorm -> first non-sRGB -> first available
    let chosen_format = if caps.formats.contains(&wgpu::TextureFormat::Bgra8Unorm) {
        wgpu::TextureFormat::Bgra8Unorm
    } else if caps.formats.contains(&wgpu::TextureFormat::Rgba8Unorm) {
        wgpu::TextureFormat::Rgba8Unorm
    } else if let Some(non_srgb) = caps.formats.iter().find(|f| !f.is_srgb()).copied() {
        non_srgb
    } else {
        caps.formats[0]
    };
    let presentation_format = if record.sample_count > 1 {
        Some(surface_present::linear_format(chosen_format)
            .ok_or("MSAA window presentation requires an RGBA8/BGRA8 surface")?)
    } else { None };
    if record.sample_count > 1 {
        let required = if record.sample_count == 2 {
            wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X2
        } else {
            wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X4
        };
        if !device_state.adapter.get_texture_format_features(wgpu::TextureFormat::Rgba8Unorm).flags.contains(required) ||
            !device_state.adapter.get_texture_format_features(wgpu::TextureFormat::Depth32Float).flags.contains(required) ||
            (record.sample_count == 2 && !device_state.device.features().contains(
                wgpu::Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES)) {
            return Err("Exact window MSAA sample count unavailable for color/depth".into());
        }
    }

    if presentation_format.is_some() &&
        !device_state.adapter.get_texture_format_features(wgpu::TextureFormat::Rgba8Unorm)
            .allowed_usages.contains(wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING) {
        return Err("MSAA window resolve requires renderable/sampleable RGBA8".into());
    }

    // The opt-in benchmark needs a non-vsync surface to compare with GLX/BGFX.
    // Refuse the campaign when the surface cannot provide one.
    let benchmark_no_vsync = std::env::var_os("COIN_RENDER_BENCH_NO_VSYNC").is_some();
    let chosen_present = if benchmark_no_vsync && caps.present_modes.contains(&wgpu::PresentMode::Immediate) {
        wgpu::PresentMode::Immediate
    } else if benchmark_no_vsync && caps.present_modes.contains(&wgpu::PresentMode::Mailbox) {
        wgpu::PresentMode::Mailbox
    } else if benchmark_no_vsync {
        return Err("Benchmark requested no-vsync, but surface supports neither Immediate nor Mailbox".to_string());
    } else if caps.present_modes.contains(&wgpu::PresentMode::AutoVsync) {
        wgpu::PresentMode::AutoVsync
    } else {
        wgpu::PresentMode::Fifo
    };

    // Negotiate alpha: Opaque if present, else first
    let chosen_alpha = if caps.alpha_modes.contains(&wgpu::CompositeAlphaMode::Opaque) {
        wgpu::CompositeAlphaMode::Opaque
    } else {
        caps.alpha_modes[0]
    };

    let config = wgpu::SurfaceConfiguration {
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT |
            (caps.usages & wgpu::TextureUsages::COPY_SRC),
        format: chosen_format,
        width,
        height,
        present_mode: chosen_present,
        alpha_mode: chosen_alpha,
        view_formats: presentation_format.filter(|format| *format != chosen_format)
            .into_iter().collect(),
        desired_maximum_frame_latency: 2,
    };

    record.surface.configure(&device_state.device, &config);

    // Create depth texture for this surface
    let depth_desc = wgpu::TextureDescriptor {
        label: Some("Window Surface Depth Texture"),
        size: wgpu::Extent3d {
            width,
            height,
            depth_or_array_layers: 1,
        },
        mip_level_count: 1,
        sample_count: record.sample_count,
        dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Depth32Float,
        // A live surface may switch from ordinary draws to peeling without
        // reconfiguration. Its opaque depth must always support the GPU snapshot.
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING |
            if record.sample_count == 1 { wgpu::TextureUsages::COPY_SRC }
            else { wgpu::TextureUsages::empty() },
        view_formats: &[],
    };
    let depth_texture = device_state.device.create_texture(&depth_desc);
    let depth_view = depth_texture.create_view(&wgpu::TextureViewDescriptor::default());
    let msaa_color_texture = if record.sample_count > 1 {
        Some(device_state.device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Window MSAA color"),
            size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
            mip_level_count: 1, sample_count: record.sample_count,
            dimension: wgpu::TextureDimension::D2, format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT, view_formats: &[],
        }))
    } else { None };
    let msaa_color_view = msaa_color_texture.as_ref().map(|texture|
        texture.create_view(&wgpu::TextureViewDescriptor::default()));

    record.presentation = presentation_format.map(|format|
        surface_present::Presentation::new(&device_state.device, width, height, format));
    record.config = Some(config);
    record.color_format = chosen_format;
    record.depth_texture = Some(depth_texture);
    record.depth_view = Some(depth_view);
    record.msaa_color_texture = msaa_color_texture;
    record.msaa_color_view = msaa_color_view;
    record.suspended = false;
    record.needs_reconfigure = false;
    record.configured_device_generation = device_generation;
    record.surface_generation += 1;

    if surface_wsi_tracing_enabled() {
        eprintln!("COIN_WGPU_WSI stage=coin_configure event=end source=actual native_type={} handle_a={:#x} handle_b={:#x} generation={} device_generation={} size={}x{} present_mode={:?} maximum_frame_latency={}",
            record.native_desc.r#type, record.native_desc.handle_a, record.native_desc.handle_b,
            record.surface_generation, device_generation, width, height, chosen_present,
            record.config.as_ref().unwrap().desired_maximum_frame_latency);
    }

    Ok(())
}

fn polygon_depth_bias(st: &CoinWgpuRenderState, topology: u32) -> Result<wgpu::DepthBiasState, String> {
    if !st.polygon_offset_factor.is_finite() || !st.polygon_offset_units.is_finite() || !st.polygon_offset_slope_bias.is_finite()
        || (st.polygon_offset_max_depth_bits != 0 &&
            !(0.0..=1.0).contains(&f32::from_bits(st.polygon_offset_max_depth_bits - 1)))
        || st.polygon_offset_styles & !7 != 0
        || !matches!(st.polygon_offset_primitive_style, 1 | 2 | 4) {
        return Err("Invalid polygon offset".to_string());
    }
    let style = match topology {
        1 => 2, 2 => 4, _ => st.polygon_offset_primitive_style,
    };
    Ok(if st.polygon_offset_enabled != 0 && st.polygon_offset_styles & style != 0 {
        wgpu::DepthBiasState {
            constant: st.polygon_offset_units.round() as i32,
            slope_scale: st.polygon_offset_factor,
            clamp: 0.0,
        }
    } else { wgpu::DepthBiasState::default() })
}

// D32Float precision is an Infra property. The Core's original-face slope
// is constant per draw; Core captures the original face maximum before masking.
fn d32_depth_quantum(max_depth: f32) -> f32 {
    let exponent = (max_depth.to_bits() >> 23) & 0xff;
    if exponent == 0 { f32::from_bits(1) }
    else { 2.0f32.powi(exponent as i32 - 127 - 23) }
}

fn resolved_polygon_bias(st: &CoinWgpuRenderState, draw: &CoinWgpuDraw,
                         vertices: &[CoinWgpuVertex], indices: &[u32]) -> Result<f32, String> {
    if st.polygon_offset_factor != 0.0 {
        return Err("Resolved polygon slope bias requires zero GPU slope factor".to_string());
    }
    let resolved_maximum = if st.polygon_offset_max_depth_bits != 0 {
        let depth = f32::from_bits(st.polygon_offset_max_depth_bits - 1);
        if !(0.0..=1.0).contains(&depth) {
            return Err("Invalid original polygon depth".to_string());
        }
        Some(depth)
    } else { None };
    let mut max_depth = 0.0f32;
    for index in &indices[draw.first_index as usize..(draw.first_index + draw.index_count) as usize] {
        let p = vertices[*index as usize].position;
        let m = &st.model_view_projection;
        let z = m[2]*p[0] + m[6]*p[1] + m[10]*p[2] + m[14];
        let w = m[3]*p[0] + m[7]*p[1] + m[11]*p[2] + m[15];
        if !z.is_finite() || !w.is_finite() {
            return Err("Invalid clip depth for resolved polygon bias".to_string());
        }
        // Core measures the clipped original face. Its retained triangle can
        // still have raw corners at or behind the eye: raster clipping handles
        // them, and they must not replace that visible-face depth metric.
        if resolved_maximum.is_some() { continue; }
        if w <= 0.0 {
            return Err("Invalid clip depth for resolved polygon bias".to_string());
        }
        let depth = st.depth_range[0] + (z/w) * (st.depth_range[1]-st.depth_range[0]);
        max_depth = max_depth.max(depth.clamp(0.0, 1.0));
    }
    if let Some(depth) = resolved_maximum {
        max_depth = depth;
    }
    let bias = st.polygon_offset_slope_bias + st.polygon_offset_units * d32_depth_quantum(max_depth);
    if !bias.is_finite() { return Err("Invalid resolved polygon depth bias".to_string()); }
    Ok(bias)
}

#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/lib_polygon_depth_tests.rs"]
mod polygon_depth_tests;

// A depth-only fullscreen triangle, clipped to the annotation viewport. Loading
// depth here preserves the rest of the target; color is never attached.
fn annotation_depth_clear_pipeline(device: &wgpu::Device, sample_count: u32) -> wgpu::RenderPipeline {
    let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("Coin Annotation Depth Clear Shader"),
        source: wgpu::ShaderSource::Wgsl(std::borrow::Cow::Borrowed(r#"
            @vertex fn vs_main(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
                let positions = array<vec2<f32>, 3>(
                    vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
                return vec4<f32>(positions[index], 1.0, 1.0);
            }
        "#)),
    });
    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("Coin Annotation Depth Clear Pipeline"),
        layout: None,
        vertex: wgpu::VertexState {
            module: &shader, entry_point: Some("vs_main"), buffers: &[],
            compilation_options: wgpu::PipelineCompilationOptions::default(),
        },
        fragment: None,
        primitive: wgpu::PrimitiveState::default(),
        depth_stencil: Some(wgpu::DepthStencilState {
            format: wgpu::TextureFormat::Depth32Float,
            depth_write_enabled: true, depth_compare: wgpu::CompareFunction::Always,
            stencil: wgpu::StencilState::default(), bias: wgpu::DepthBiasState::default(),
        }),
        multisample: wgpu::MultisampleState { count: sample_count, ..Default::default() },
        multiview: None, cache: None,
    })
}

fn get_or_create_pipeline<'a>(
    ctx: &'a DeviceState,
    profile: &SurfaceSamplingProfile,
    topology: u32,
    color_format: wgpu::TextureFormat,
    depth_format: wgpu::TextureFormat,
    sample_count: u32,
    cull_face: Option<wgpu::Face>,
    front_face: wgpu::FrontFace,
    blend: bool,
    additive: bool,
    legacy_blend_alpha: bool,
    peel: bool,
    depth_write: bool,
    color_write: u32,
    depth_compare: wgpu::CompareFunction,
    depth_bias: wgpu::DepthBiasState,
    resolved_depth_bias: bool,
    eight_shadows: bool,
    shadow_profile: bool,
    weighted: bool,
    textured: bool,
    instanced: bool,
) -> Result<wgpu::RenderPipeline, String> {
    let mut map = profile.pipelines.lock().map_err(|e| e.to_string())?;

    // Lines and Points have culling disabled by specification
    let (primitive_topology, selected_shader, effective_cull) = match topology {
        1 => (wgpu::PrimitiveTopology::LineList, &profile.line_shader_module, None),
        2 => (wgpu::PrimitiveTopology::PointList, &profile.point_shader_module, None),
        _ => (wgpu::PrimitiveTopology::TriangleList,
            if instanced { &profile.instanced_shader } else if eight_shadows { &profile.shadow_eight_shader } else if shadow_profile { &profile.shadow_shader } else { &profile.shader_module }, cull_face),
    };

    let key = PipelineKey {
        instanced, eight_shadows, shadow_profile, weighted, topology, textured,
        color_format,
        depth_format,
        sample_count,
        cull_mode: effective_cull,
        front_face,
        blend,
        additive,
        legacy_blend_alpha,
        peel,
        depth_write,
        color_write,
        depth_compare,
        resolved_depth_bias,
        depth_bias_constant: depth_bias.constant,
        depth_bias_slope_bits: depth_bias.slope_scale.to_bits(),
    };
    if let Some(p) = map.get(&key) {
        ctx.pipeline_hits.fetch_add(1, Ordering::Relaxed);
        return Ok(p.clone());
    }

    let vertex_buffer_layout = wgpu::VertexBufferLayout {
        array_stride: std::mem::size_of::<CoinWgpuVertex>() as wgpu::BufferAddress,
        step_mode: wgpu::VertexStepMode::Vertex,
        attributes: &[
            wgpu::VertexAttribute {
                format: wgpu::VertexFormat::Float32x3,
                offset: 0,
                shader_location: 0,
            },
            wgpu::VertexAttribute {
                format: wgpu::VertexFormat::Float32x3,
                offset: 12,
                shader_location: 1,
            },
            wgpu::VertexAttribute {
                format: wgpu::VertexFormat::Float32x4,
                offset: 24,
                shader_location: 2,
            },
            wgpu::VertexAttribute {
                format: wgpu::VertexFormat::Uint32,
                offset: 40,
                shader_location: 3,
            },
            wgpu::VertexAttribute {
                format: wgpu::VertexFormat::Float32,
                offset: 44,
                shader_location: 4,
            },
            wgpu::VertexAttribute {
                format: wgpu::VertexFormat::Float32,
                offset: 48,
                shader_location: 5,
            },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x4, offset: 52, shader_location: 6 },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x4, offset: 68, shader_location: 7 },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x4, offset: 84, shader_location: 8 },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x4, offset: 100, shader_location: 9 },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x4, offset: 116, shader_location: 10 },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x4, offset: 132, shader_location: 11 },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x4, offset: 148, shader_location: 12 },
        ],
    };

    let pipeline_label = match topology {
        1 => "Coin Line Render Pipeline",
        2 => "Coin Point Render Pipeline",
        _ => "Coin Standard Render Pipeline",
    };

    let mut color_targets = vec![Some(wgpu::ColorTargetState {
        format: color_format,
        blend: Some(if blend {
            wgpu::BlendState {
                color: wgpu::BlendComponent {
                    src_factor: wgpu::BlendFactor::SrcAlpha,
                    dst_factor: if additive {
                        wgpu::BlendFactor::One
                    } else {
                        wgpu::BlendFactor::OneMinusSrcAlpha
                    },
                    operation: wgpu::BlendOperation::Add,
                },
                alpha: wgpu::BlendComponent {
                    src_factor: if additive || legacy_blend_alpha {
                        wgpu::BlendFactor::SrcAlpha
                    } else {
                        wgpu::BlendFactor::One
                    },
                    dst_factor: if additive {
                        wgpu::BlendFactor::One
                    } else {
                        wgpu::BlendFactor::OneMinusSrcAlpha
                    },
                    operation: wgpu::BlendOperation::Add,
                },
            }
        } else {
            wgpu::BlendState::REPLACE
        }),
        write_mask: if color_write == 1 { wgpu::ColorWrites::ALL }
                    else { wgpu::ColorWrites::from_bits_truncate(color_write & 0x0f) },
    })];
    if weighted {
        let additive=wgpu::BlendComponent { src_factor:wgpu::BlendFactor::One,
            dst_factor:wgpu::BlendFactor::One,operation:wgpu::BlendOperation::Add };
        color_targets[0].as_mut().unwrap().blend=Some(wgpu::BlendState{color:additive,alpha:additive});
        let reveal=wgpu::BlendComponent { src_factor:wgpu::BlendFactor::Zero,
            dst_factor:wgpu::BlendFactor::OneMinusSrc,operation:wgpu::BlendOperation::Add };
        color_targets.push(Some(wgpu::ColorTargetState{format:wgpu::TextureFormat::R16Float,
            blend:Some(wgpu::BlendState{color:reveal,alpha:reveal}),write_mask:wgpu::ColorWrites::ALL}));
    }
    if peel {
        color_targets.push(Some(wgpu::ColorTargetState {
            format: wgpu::TextureFormat::R8Unorm,
            blend: None,
            write_mask: wgpu::ColorWrites::ALL,
        }));
    }
    // wgpu-hal 24's GL program cache keys module IDs/entry points but omits
    // override constants. Give textured/untextured variants distinct modules
    // so switching profiles cannot reuse the first profile's GL program.
    let selected_shader = if !profile.bump_receiver && !instanced && !textured && ctx.adapter.get_info().backend == wgpu::Backend::Gl {
        let mut profiles = profile.untextured_shaders.lock().map_err(|e| e.to_string())?;
        profiles.entry((topology, shadow_profile, eight_shadows)).or_insert_with(|| {
            let source = match topology {
                1 => WGSL_LINE_SHADER.to_owned(),
                2 => WGSL_POINT_SHADER.to_owned(),
                _ if eight_shadows => if profile.shadow_multitexture { shadow_receiver::eight_map_multitexture_source() } else { shadow_receiver::eight_map_source() },
                _ if shadow_profile => if profile.shadow_multitexture { shadow_receiver::four_map_multitexture_source() } else { shadow_receiver::four_map_source() },
                _ => shader_profile::without_shadows(WGSL_SHADER),
            };
            ctx.device.create_shader_module(wgpu::ShaderModuleDescriptor {
                label: Some("Coin untextured shader profile"),
                source: wgpu::ShaderSource::Wgsl(shader_profile::depth_load_profile(
                    &shader_profile::sample_depth_profile(&shader_profile::peeling_msaa_profile(&shader_profile::texture_specialization(
                        &shader_profile::sampling_profile(&format!("{}{}", source, weighted::FRAGMENT), profile.policy != SamplingPolicy::Native)), profile.multisampled_peel), profile.sample_depth), true).into()),
            })
        }).clone()
    } else { selected_shader.clone() };
    let constants = HashMap::from([("coin_textures_enabled".to_owned(), if textured { 1.0 } else { 0.0 })]);
    let pipeline = ctx
        .device
        .create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some(pipeline_label),
            layout: Some(if instanced { &profile.instanced_pipeline_layout } else if eight_shadows { &profile.shadow_eight_pipeline_layout } else { &profile.pipeline_layout }),
            vertex: wgpu::VertexState {
                module: &selected_shader,
                entry_point: Some("vs_main"),
                buffers: &[vertex_buffer_layout],
                compilation_options: wgpu::PipelineCompilationOptions { constants: &constants, ..Default::default() },
            },
            fragment: Some(wgpu::FragmentState {
                module: &selected_shader,
                entry_point: Some(if weighted { "fs_weighted" } else if peel {
                    "fs_peel"
                } else if resolved_depth_bias {
                    "fs_depth_bias"
                } else {
                    "fs_main"
                }),
                targets: &color_targets,
                compilation_options: wgpu::PipelineCompilationOptions { constants: &constants, ..Default::default() },
            }),
            primitive: wgpu::PrimitiveState {
                topology: primitive_topology,
                strip_index_format: None,
                front_face,
                cull_mode: effective_cull,
                polygon_mode: wgpu::PolygonMode::Fill,
                unclipped_depth: false,
                conservative: false,
            },
            depth_stencil: Some(wgpu::DepthStencilState {
                format: depth_format,
                depth_write_enabled: depth_write,
                depth_compare,
                stencil: wgpu::StencilState::default(),
                bias: depth_bias,
            }),
            multisample: wgpu::MultisampleState { count: sample_count, ..Default::default() },
            multiview: None,
            cache: None,
        });

    map.insert(key, pipeline.clone());
    ctx.pipeline_compilations.fetch_add(1, Ordering::Relaxed);
    Ok(pipeline)
}

// Transport validation only: chain generation belongs to Coin/Core.
fn texture_levels(width:u32,height:u32,format:u32)->Result<Vec<(u32,u32,u64)>,(CoinWgpuStatus,String)> {
    texture_profile::levels(width,height,format)
}

// The texture metadata declaration changes admission only: source
// alpha remains available to every texture combine, including SOURCE_ALPHA.
const TEXTURE_DECLARED_OPAQUE: u32 = 1 << 31;
const TEXTURE_VOLUME_SRGB: u32 = 1 << 25;
const TEXTURE_CUBE_SRGB: u32 = 1 << 9;

fn texture_metadata(image: &CoinWgpuTexture) -> u32 {
    image.reserved & !TEXTURE_DECLARED_OPAQUE
}

// Hardware sRGB decoding applies independently to each fetched atlas texel,
// before our explicit volume/cube spatial/LOD interpolation, and leaves alpha linear.
fn texture_gpu_format(image: &CoinWgpuTexture)
    -> Result<wgpu::TextureFormat, (CoinWgpuStatus, String)> {
    if (image.format == 11 && texture_metadata(image) & TEXTURE_VOLUME_SRGB != 0) ||
       (image.format == 12 && texture_metadata(image) & TEXTURE_CUBE_SRGB != 0) {
        Ok(wgpu::TextureFormat::Rgba8UnormSrgb)
    } else {
        Ok(texture_profile::format(image.format)?.0)
    }
}

fn validate_texture_declarations(textures: &[CoinWgpuTexture]) -> Result<(), String> {
    for image in textures {
        let allowed = match image.format {
            0 | 2..=10 => 0,
            1 => 31,
            11 => 0x03ff_ffff,
            12 => 1023,
            _ => return Err("Invalid texture format in declaration metadata".into()),
        };
        if texture_metadata(image) & !allowed != 0 {
            return Err("Texture metadata has unknown reserved bits".into());
        }
    }
    Ok(())
}

fn gpu_texture_token(image: &CoinWgpuTexture) -> bool {
    image.format==1 || (image.format==12 && texture_metadata(image)&256!=0)
}

// Format 12 keeps a face-local chain in an RGBA8 2D transport atlas.
// reserved: log2(face size)[0..3], source components[4..6], mip flag[7], GPU token[8], sRGB[9].
fn cube_atlas_metadata(t:&CoinWgpuTexture)->Result<Option<(u32,u32,bool)>,(CoinWgpuStatus,String)>{
    if t.format!=12{return Ok(None);}
    let exponent=texture_metadata(t)&15;let components=(texture_metadata(t)>>4)&7;let mips=texture_metadata(t)&128!=0;
    if texture_metadata(t) & !1023 != 0 || exponent>11 || !(1..=4).contains(&components){
        return Err((CoinWgpuStatus::InvalidArgument,"Invalid cube atlas metadata".into()));
    }
    let n=1u32<<exponent;let width=2*n;let mut size=n;let mut texels=0u64;
    loop{texels+=6*u64::from(size)*u64::from(size);if !mips||size==1{break;}size/=2;}
    let height=texels.div_ceil(u64::from(width));let bytes=u64::from(width)*height*4;
    if t.width!=width || u64::from(t.height)!=height || t.height>8192 || bytes>128*1024*1024 ||
       (if texture_metadata(t)&256!=0 {t.pixel_bytes_len!=0 || !t.pixels.is_null() || t.content_digest==0 || components!=4 || texture_metadata(t)&TEXTURE_CUBE_SRGB!=0}
        else {t.pixel_bytes_len!=bytes || t.pixels.is_null()}){
        return Err((CoinWgpuStatus::InvalidArgument,"Invalid cube atlas extent or pixels".into()));
    }
    if texture_metadata(t)&256!=0 {return Ok(Some((n,components,mips)));}
    let pixels=unsafe{std::slice::from_raw_parts(t.pixels,bytes as usize)};
    if pixels[texels as usize*4..].iter().any(|v|*v!=0){
        return Err((CoinWgpuStatus::InvalidArgument,"Nonzero cube atlas padding".into()));
    }
    Ok(Some((n,components,mips)))
}
fn validate_cube_resources(states:&[CoinWgpuRenderState],textures:&[CoinWgpuTexture],samplers:&[CoinWgpuSampler])
    ->Result<(),(CoinWgpuStatus,String)>{
    validate_spatial_resources(states,textures,samplers)?;
    for image in textures{cube_atlas_metadata(image)?;}
    if !textures.iter().any(|t|t.format==12){return Ok(());}
    for state in states{for unit in 0..8{
        let (enabled,image,sampler,model)=if unit==0{(state.has_texture,state.texture_slot,state.sampler_slot,state.texture_model)}
            else{let t=&state.extra_textures[unit-1];(t.enabled,t.texture_slot,t.sampler_slot,t.model)};
        if enabled==0{continue;}
        let image=textures.get(image as usize).ok_or_else(||(CoinWgpuStatus::InvalidArgument,"Cube image slot out of bounds".into()))?;
        if image.format!=12{continue;}
        let (_,components,mips)=cube_atlas_metadata(image)?.unwrap();
        let matrix=if unit==0{&state.texture_matrix}else{&state.extra_textures[unit-1].matrix};
        if state.texture_projection>1 || state.portable_shader_slot!=0 || !matrix.iter().all(|v|v.is_finite()) {
            return Err((CoinWgpuStatus::InvalidArgument,"Unsupported cube shader or invalid coordinate matrix".into()));
        }
        let sampler=samplers.get(sampler as usize).ok_or_else(||(CoinWgpuStatus::InvalidArgument,"Cube sampler slot out of bounds".into()))?;
        if sampler.wrap_s!=1||sampler.wrap_t!=1||sampler.reserved!=16||sampler.filter>3||model>3||(model==2&&components<3)||(sampler.filter>=2&&!mips){
            return Err((CoinWgpuStatus::InvalidArgument,"Invalid cube sampler or model".into()));
        }
    }}
    Ok(())
}

fn validate_cube_draws(states:&[CoinWgpuRenderState],textures:&[CoinWgpuTexture],vertices:&[CoinWgpuVertex],
    indices:&[u32],draws:&[CoinWgpuDraw])->Result<(),(CoinWgpuStatus,String)>{
    if !textures.iter().any(|t|t.format==11||t.format==12){return Ok(());}
    for draw in draws{
        let state=states.get(draw.render_state_slot as usize).ok_or_else(||(CoinWgpuStatus::InvalidArgument,"Cube draw state out of bounds".into()))?;
        let cube=(0..8).any(|unit|{
            let (enabled,slot)=if unit==0{(state.has_texture,state.texture_slot)}else{let t=&state.extra_textures[unit-1];(t.enabled,t.texture_slot)};
            enabled!=0&&textures.get(slot as usize).is_some_and(|t|t.format==11||t.format==12)
        });
        if !cube{continue;}
        let end=draw.first_index.checked_add(draw.index_count).ok_or_else(||(CoinWgpuStatus::InvalidArgument,"Cube draw index overflow".into()))?;
        let primitive=match draw.topology{0=>3,1=>2,2=>1,_=>return Err((CoinWgpuStatus::InvalidArgument,"Invalid cube topology".into()))};
        if draw.index_count%primitive!=0{return Err((CoinWgpuStatus::InvalidArgument,"Incomplete cube primitive".into()));}
        let range=indices.get(draw.first_index as usize..end as usize).ok_or_else(||(CoinWgpuStatus::InvalidArgument,"Cube draw indices out of bounds".into()))?;
        validate_texture_coordinates_with_images(state,draw.topology,vertices,range,textures).map_err(|message|(CoinWgpuStatus::InvalidArgument,message))?;
    }
    Ok(())
}

fn spatial_atlas_metadata(t: &CoinWgpuTexture)
    -> Result<Option<(u32,u32,u32,bool)>,(CoinWgpuStatus,String)> {
    if t.format != 11 { return Ok(None); }
    if texture_metadata(t) & !0x03ff_ffff != 0 { return Err((CoinWgpuStatus::InvalidArgument,
        "Spatial atlas metadata has reserved bits".into())); }
    let w=(texture_metadata(t) & 255)+1;
    let h=((texture_metadata(t) >> 8) & 255)+1;
    let d=((texture_metadata(t) >> 16) & 255)+1;
    let mips=texture_metadata(t) & (1<<24) != 0;
    let mut columns=1u32;
    while columns*columns<d { columns*=2; }
    let mut rows=0u32;
    let (mut lw,mut lh,mut ld)=(w,h,d);
    loop {
        rows=rows.checked_add(lh*((ld+columns-1)/columns)).ok_or_else(||
            (CoinWgpuStatus::Unsupported,"Spatial atlas row count overflow".into()))?;
        if !mips || (lw==1 && lh==1 && ld==1) { break; }
        lw=(lw/2).max(1); lh=(lh/2).max(1); ld=(ld/2).max(1);
    }
    if t.width != w*columns || t.height != rows ||
       t.width > 8192 || t.height > 8192 ||
       u64::from(t.width)*u64::from(t.height)*4 > 128*1024*1024 ||
       t.pixels.is_null() || t.pixel_bytes_len != u64::from(t.width)*u64::from(t.height)*4 {
        return Err((CoinWgpuStatus::InvalidArgument,
            "Spatial atlas physical extent or byte budget mismatch".into()));
    }
    Ok(Some((w,h,d,mips)))
}

// Validate every transported volume before target acquisition or cache changes,
// including unused resources and padding that must never become authored data.
fn validate_spatial_resources(states:&[CoinWgpuRenderState],textures:&[CoinWgpuTexture],samplers:&[CoinWgpuSampler])
    ->Result<(),(CoinWgpuStatus,String)>{
    for image in textures {
        if let Some((mut w,mut h,mut d,mips))=spatial_atlas_metadata(image)? {
            let mut columns=1;
            while columns*columns<d {columns*=2;}
            let pixels=unsafe{std::slice::from_raw_parts(image.pixels,image.pixel_bytes_len as usize)};
            let mut base=0;
            loop {
                let rows=(d+columns-1)/columns;
                for row in 0..rows {
                    let used=(d-row*columns).min(columns)*w;
                    for y in 0..h {
                        let start=((base+row*h+y)*image.width+used) as usize*4;
                        let end=((base+row*h+y+1)*image.width) as usize*4;
                        if pixels[start..end].iter().any(|v|*v!=0) {
                            return Err((CoinWgpuStatus::InvalidArgument,"Nonzero spatial atlas padding".into()));
                        }
                    }
                }
                base+=h*rows;
                if !mips || (w==1&&h==1&&d==1) {break;}
                w=(w/2).max(1);h=(h/2).max(1);d=(d/2).max(1);
            }
        }
    }
    if !textures.iter().any(|t|t.format==11) {return Ok(());}
    for sampler in samplers {
        if sampler.wrap_s>1 || sampler.wrap_t>1 || sampler.filter>3 || sampler.reserved&!31!=0 ||
           texture_profile::anisotropy(sampler.reserved&15,sampler.filter).is_err() {
            return Err((CoinWgpuStatus::InvalidArgument,"Invalid spatial frame sampler".into()));
        }
    }
    for state in states {for unit in 0..8 {
        let (enabled,image,sampler,model)=if unit==0 {(state.has_texture,state.texture_slot,state.sampler_slot,state.texture_model)}
            else {let layer=&state.extra_textures[unit-1];(layer.enabled,layer.texture_slot,layer.sampler_slot,layer.model)};
        if enabled==0 {continue;}
        let image=textures.get(image as usize).ok_or_else(||(CoinWgpuStatus::InvalidArgument,"Volume frame image slot out of bounds".into()))?;
        if image.format!=11 {continue;}
        let sampler=samplers.get(sampler as usize).ok_or_else(||(CoinWgpuStatus::InvalidArgument,"Volume sampler slot out of bounds".into()))?;
        if sampler.wrap_s>1 || sampler.wrap_t>1 || sampler.filter>3 || sampler.reserved&!16!=0 || model>3 ||
           (sampler.filter>=2 && texture_metadata(image)&(1<<24)==0) {
            return Err((CoinWgpuStatus::InvalidArgument,"Invalid volume sampler or model".into()));
        }
    }}
    Ok(())
}
fn admit_spatial_resources(ctx:&DeviceState,textures:&[CoinWgpuTexture])->Result<(),(CoinWgpuStatus,String)>{
    for image in textures.iter().filter(|image|image.format==12) {
        let (n,_,mips)=cube_atlas_metadata(image)?.unwrap();
        if image.width>ctx.device.limits().max_texture_dimension_2d || image.height>ctx.device.limits().max_texture_dimension_2d {
            return Err((CoinWgpuStatus::Unsupported,"Cube atlas exceeds GPU texture dimension limit".into()));
        }
        if gpu_texture_token(image) {
            let registry=ctx.rtt_textures.lock().unwrap();
            let entry=registry.active.get(&image.content_digest).ok_or_else(||
                (CoinWgpuStatus::InvalidArgument,"Cube token is stale or from another device/generation".into()))?;
            if entry.width!=image.width || entry.height!=image.height || entry.cube_face_size!=n ||
                entry.cube_mipmapped!=mips || entry.format!=0 || entry.mip_count!=1 {
                return Err((CoinWgpuStatus::InvalidArgument,"Cube token metadata mismatch".into()));
            }
        }
    }

    if textures.iter().any(|t|t.format==11 && (t.width>ctx.device.limits().max_texture_dimension_2d ||
        t.height>ctx.device.limits().max_texture_dimension_2d)) {
        return Err((CoinWgpuStatus::Unsupported,"Spatial atlas exceeds GPU texture dimension limit".into()));
    }
    Ok(())
}

fn resolved_viewport(state: &CoinWgpuRenderState, width: u32, height: u32)
    -> Result<[i32; 4], (CoinWgpuStatus, String)> {
    let viewport = if state.viewport[2] == 0 && state.viewport[3] == 0 {
        [0, 0, width as i32, height as i32]
    } else { state.viewport };
    // Core's empty scissor. Keep the legacy all-zero full-target default.
    if viewport == [0, 0, 0, 1] { return Ok(viewport); }
    let right = i64::from(viewport[0]) + i64::from(viewport[2]);
    let bottom = i64::from(viewport[1]) + i64::from(viewport[3]);
    if viewport[0] < 0 || viewport[1] < 0 || viewport[2] <= 0 || viewport[3] <= 0
        || right > i64::from(width) || bottom > i64::from(height) {
        return Err((CoinWgpuStatus::InvalidArgument,
            format!("Invalid viewport {:?}", viewport)));
    }
    Ok(viewport)
}

#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/lib_viewport_tests.rs"]
mod viewport_tests;

// The Coin/Core payload is immutable during one frame. Infra only validates
// its bounds and turns the captured draws into a GPU map.
struct ShadowPass<'a> {
    casters: &'a [CoinWgpuShadowDraw],
    receivers: &'a [CoinWgpuShadowReceiver],
    map_size: u32,
    near: f32,
    far: f32,
    epsilon: f32,
    threshold: f32,
    kind: u32,
}

struct ShadowFrame<'a> {
    second: Option<ShadowPass<'a>>,
    third: Option<ShadowPass<'a>>,
    fourth: Option<ShadowPass<'a>>,
    extra: Vec<ShadowPass<'a>>,
    casters: &'a [CoinWgpuShadowDraw],
    receivers: &'a [CoinWgpuShadowReceiver],
    map_size: u32,
    near: f32,
    far: f32,
    epsilon: f32,
    threshold: f32,
    kind: u32,
}

// Both the offscreen and window encoders consume the same validated VSM payload.
// Reject malformed pass lists before acquiring a swapchain texture or allocating
// offscreen attachments; the shared encoder then owns the map execution.
fn validate_shadow_frame<'a>(
    f: &CoinWgpuFrameView,
    state_count: usize,
    camera_patch: bool,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> Result<Option<ShadowFrame<'a>>, CoinWgpuStatus> {
    let shadow_frame = if f.shadow_map_size == 0 {
            if f.shadow_map_size_second != 0 || f.shadow_map_size_third != 0 ||
                f.shadow_map_size_fourth != 0 || f.extra_shadow_pass_count != 0 ||
                !f.extra_shadow_passes.is_null() || !f.shadow_casters.is_null() || f.shadow_caster_count != 0 || f.shadow_kind != 0
                || !f.shadow_receivers.is_null() || f.shadow_receiver_count != 0
                || !f.shadow_casters_second.is_null() || f.shadow_caster_count_second != 0
                || !f.shadow_receivers_second.is_null() || f.shadow_receiver_count_second != 0 ||
            !f.shadow_casters_third.is_null() || f.shadow_caster_count_third != 0 ||
            !f.shadow_receivers_third.is_null() || f.shadow_receiver_count_third != 0 ||
            !f.shadow_casters_fourth.is_null() || f.shadow_caster_count_fourth != 0 ||
            !f.shadow_receivers_fourth.is_null() || f.shadow_receiver_count_fourth != 0 {
                set_error(error_buf, error_buf_len, "Shadow payload has data without a map");
                return Err(CoinWgpuStatus::InvalidArgument);
            }
            None
        } else {
            if camera_patch || f.camera_base_revision != 0
                || f.shadow_receiver_count != state_count as u64
                || f.shadow_kind > 1
                || !f.shadow_map_size.is_power_of_two() || f.shadow_map_size > 2048
                || !f.shadow_near_distance.is_finite()
                || !f.shadow_far_distance.is_finite()
                || (f.shadow_kind == 1 && f.shadow_near_distance <= 0.0)
                || f.shadow_far_distance <= f.shadow_near_distance
                || !f.shadow_epsilon.is_finite() || f.shadow_epsilon < 0.0
                || !f.shadow_threshold.is_finite() || !(0.0..1.0).contains(&f.shadow_threshold) {
                set_error(error_buf, error_buf_len, "Invalid or unsupported shadow frame parameters");
                return Err(CoinWgpuStatus::InvalidArgument);
            }
            let casters = match validate_slice(f.shadow_casters, f.shadow_caster_count,
                "shadow casters", error_buf, error_buf_len) {
                Ok(s) => s, Err(st) => return Err(st),
            };
            let receivers = match validate_slice(f.shadow_receivers, f.shadow_receiver_count,
                "shadow receivers", error_buf, error_buf_len) {
                Ok(s) => s, Err(st) => return Err(st),
            };
            let second = if f.shadow_map_size_second == 0 {
                if !f.shadow_casters_second.is_null() || f.shadow_caster_count_second != 0 ||
                    !f.shadow_receivers_second.is_null() ||
                    f.shadow_receiver_count_second != 0 || f.shadow_map_size_second != 0 ||
                    f.shadow_kind_second != 0 || f.shadow_epsilon_second != 0.0 ||
                    f.shadow_threshold_second != 0.0 {
                    set_error(error_buf, error_buf_len, "Partial second shadow pass");
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                None
            } else {
                if f.shadow_receiver_count_second != state_count as u64 ||
                    f.shadow_kind_second > 1 ||
                    !f.shadow_map_size_second.is_power_of_two() || f.shadow_map_size_second > 2048 ||
                    !f.shadow_near_distance_second.is_finite() ||
                    !f.shadow_far_distance_second.is_finite() ||
                    (f.shadow_kind_second == 1 && f.shadow_near_distance_second <= 0.0) ||
                    f.shadow_far_distance_second <= f.shadow_near_distance_second ||
                    !f.shadow_epsilon_second.is_finite() || f.shadow_epsilon_second < 0.0 ||
                    !f.shadow_threshold_second.is_finite() ||
                    !(0.0..1.0).contains(&f.shadow_threshold_second) {
                    set_error(error_buf, error_buf_len, "Invalid second shadow pass parameters");
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                let second_casters = match validate_slice(f.shadow_casters_second,
                    f.shadow_caster_count_second, "second shadow casters", error_buf, error_buf_len) {
                    Ok(s) => s, Err(st) => return Err(st),
                };
                let second_receivers = match validate_slice(f.shadow_receivers_second,
                    f.shadow_receiver_count_second, "second shadow receivers", error_buf, error_buf_len) {
                    Ok(s) => s, Err(st) => return Err(st),
                };
                Some(ShadowPass {
                    casters: second_casters, receivers: second_receivers,
                    map_size: f.shadow_map_size_second, near: f.shadow_near_distance_second,
                    far: f.shadow_far_distance_second,
                    epsilon: f.shadow_epsilon_second, threshold: f.shadow_threshold_second,
                    kind: f.shadow_kind_second,
                })
            };
            let third = if f.shadow_map_size_third == 0 {
                if !f.shadow_casters_third.is_null() || f.shadow_caster_count_third != 0 ||
                    !f.shadow_receivers_third.is_null() ||
                    f.shadow_receiver_count_third != 0 || f.shadow_map_size_third != 0 ||
                    f.shadow_kind_third != 0 || f.shadow_epsilon_third != 0.0 ||
                    f.shadow_threshold_third != 0.0 {
                    set_error(error_buf, error_buf_len, "Partial third shadow pass");
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                None
            } else {
                if second.is_none() ||
                    f.shadow_receiver_count_third != state_count as u64 ||
                    f.shadow_kind_third > 1 ||
                    !f.shadow_map_size_third.is_power_of_two() || f.shadow_map_size_third > 2048 ||
                    !f.shadow_near_distance_third.is_finite() ||
                    !f.shadow_far_distance_third.is_finite() ||
                    (f.shadow_kind_third == 1 && f.shadow_near_distance_third <= 0.0) ||
                    f.shadow_far_distance_third <= f.shadow_near_distance_third ||
                    !f.shadow_epsilon_third.is_finite() || f.shadow_epsilon_third < 0.0 ||
                    !f.shadow_threshold_third.is_finite() ||
                    !(0.0..1.0).contains(&f.shadow_threshold_third) {
                    set_error(error_buf, error_buf_len, "Invalid third shadow pass parameters");
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                let pass_casters = match validate_slice(f.shadow_casters_third,
                    f.shadow_caster_count_third, "third shadow casters", error_buf, error_buf_len) {
                    Ok(s) => s, Err(st) => return Err(st),
                };
                let pass_receivers = match validate_slice(f.shadow_receivers_third,
                    f.shadow_receiver_count_third, "third shadow receivers", error_buf, error_buf_len) {
                    Ok(s) => s, Err(st) => return Err(st),
                };
                Some(ShadowPass {
                    casters: pass_casters, receivers: pass_receivers,
                    map_size: f.shadow_map_size_third, near: f.shadow_near_distance_third,
                    far: f.shadow_far_distance_third,
                    epsilon: f.shadow_epsilon_third, threshold: f.shadow_threshold_third,
                    kind: f.shadow_kind_third,
                })
            };
            let fourth = if f.shadow_map_size_fourth == 0 {
                if !f.shadow_casters_fourth.is_null() || f.shadow_caster_count_fourth != 0 ||
                    !f.shadow_receivers_fourth.is_null() ||
                    f.shadow_receiver_count_fourth != 0 || f.shadow_map_size_fourth != 0 ||
                    f.shadow_kind_fourth != 0 || f.shadow_epsilon_fourth != 0.0 ||
                    f.shadow_threshold_fourth != 0.0 {
                    set_error(error_buf, error_buf_len, "Partial fourth shadow pass");
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                None
            } else {
                if third.is_none() ||
                    f.shadow_receiver_count_fourth != state_count as u64 ||
                    f.shadow_kind_fourth > 1 ||
                    !f.shadow_map_size_fourth.is_power_of_two() || f.shadow_map_size_fourth > 2048 ||
                    !f.shadow_near_distance_fourth.is_finite() ||
                    !f.shadow_far_distance_fourth.is_finite() ||
                    (f.shadow_kind_fourth == 1 && f.shadow_near_distance_fourth <= 0.0) ||
                    f.shadow_far_distance_fourth <= f.shadow_near_distance_fourth ||
                    !f.shadow_epsilon_fourth.is_finite() || f.shadow_epsilon_fourth < 0.0 ||
                    !f.shadow_threshold_fourth.is_finite() ||
                    !(0.0..1.0).contains(&f.shadow_threshold_fourth) {
                    set_error(error_buf, error_buf_len, "Invalid fourth shadow pass parameters");
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                let pass_casters = match validate_slice(f.shadow_casters_fourth,
                    f.shadow_caster_count_fourth, "fourth shadow casters", error_buf, error_buf_len) {
                    Ok(s) => s, Err(st) => return Err(st),
                };
                let pass_receivers = match validate_slice(f.shadow_receivers_fourth,
                    f.shadow_receiver_count_fourth, "fourth shadow receivers", error_buf, error_buf_len) {
                    Ok(s) => s, Err(st) => return Err(st),
                };
                Some(ShadowPass {
                    casters: pass_casters, receivers: pass_receivers,
                    map_size: f.shadow_map_size_fourth, near: f.shadow_near_distance_fourth,
                    far: f.shadow_far_distance_fourth,
                    epsilon: f.shadow_epsilon_fourth, threshold: f.shadow_threshold_fourth,
                    kind: f.shadow_kind_fourth,
                })
            };
            let extra_views = if f.extra_shadow_pass_count == 0 {
                if !f.extra_shadow_passes.is_null() {
                    set_error(error_buf, error_buf_len, "Partial extra shadow pass list");
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                &[][..]
            } else {
                if fourth.is_none() || f.extra_shadow_pass_count > 4 {
                    set_error(error_buf, error_buf_len, "Extra shadow passes require four base maps and at most four additions");
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                match validate_slice(f.extra_shadow_passes, f.extra_shadow_pass_count,
                    "extra shadow passes", error_buf, error_buf_len) {
                    Ok(s) => s, Err(st) => return Err(st),
                }
            };
            let mut extra = Vec::with_capacity(extra_views.len());
            for (slot, pass) in extra_views.iter().enumerate() {
                if pass.receiver_count != state_count as u64 ||
                    pass.kind > 1 || !pass.map_size.is_power_of_two() ||
                    pass.map_size > 2048 || !pass.near_distance.is_finite() ||
                    !pass.far_distance.is_finite() || (pass.kind == 1 && pass.near_distance <= 0.0) ||
                    pass.far_distance <= pass.near_distance ||
                    !pass.epsilon.is_finite() || pass.epsilon < 0.0 ||
                    !pass.threshold.is_finite() || !(0.0..1.0).contains(&pass.threshold) {
                    set_error(error_buf, error_buf_len, &format!("Invalid extra shadow pass {slot}"));
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                let casters = match validate_slice(pass.casters, pass.caster_count,
                    "extra shadow casters", error_buf, error_buf_len) {
                    Ok(s) => s, Err(st) => return Err(st),
                };
                let receivers = match validate_slice(pass.receivers, pass.receiver_count,
                    "extra shadow receivers", error_buf, error_buf_len) {
                    Ok(s) => s, Err(st) => return Err(st),
                };
                if receivers.iter().any(|r| !r.model_view.iter()
                    .chain(r.model_view_projection.iter()).all(|v| v.is_finite())) {
                    set_error(error_buf, error_buf_len, "Invalid extra shadow casters or receiver matrices");
                    return Err(CoinWgpuStatus::InvalidArgument);
                }
                extra.push(ShadowPass { casters, receivers,
                    map_size: pass.map_size, near: pass.near_distance,
                    far: pass.far_distance, epsilon: pass.epsilon,
                    threshold: pass.threshold, kind: pass.kind });
            }
            Some(ShadowFrame {
                second, third, fourth, extra,
                casters, receivers, map_size: f.shadow_map_size,
                near: f.shadow_near_distance, far: f.shadow_far_distance,
                epsilon: f.shadow_epsilon, threshold: f.shadow_threshold, kind: f.shadow_kind,
            })
        };

    if let Some(frame) = shadow_frame.as_ref() {
        let passes = frame.second.iter().chain(frame.third.iter()).chain(frame.fourth.iter())
            .chain(frame.extra.iter());
        for (casters, size) in std::iter::once((frame.casters, frame.map_size))
            .chain(passes.map(|pass| (pass.casters, pass.map_size))) {
            if let Err(message) = shadow::validate_caster_viewports(casters, size) {
                set_error(error_buf, error_buf_len, &message);
                return Err(CoinWgpuStatus::InvalidArgument);
            }
        }
    }
    Ok(shadow_frame)
}

fn peeling_supported(ctx: &DeviceState, samples: u32) -> bool {
    let mut cache = ctx.peeling_sample_support.lock().unwrap();
    *cache.entry(samples).or_insert_with(|| peeling::adapter_supported(&ctx.adapter, &ctx.device, samples))
}

fn weighted_supported(ctx: &DeviceState, samples: u32) -> bool {
    let mut cached = ctx.weighted_sample_support.lock().unwrap();
    *cached.entry(samples).or_insert_with(||
        weighted::adapter_supported(&ctx.adapter, &ctx.device, samples))
}

fn custom_shadow_sample_depth(state: &CoinWgpuRenderState) -> bool {
    state.depth_range != [0.0, 1.0] ||
        (state.polygon_offset_enabled != 0 && state.polygon_offset_styles & state.polygon_offset_primitive_style != 0 &&
         (state.polygon_offset_factor != 0.0 || state.polygon_offset_units != 0.0 || state.polygon_offset_slope_bias != 0.0))
}

fn shadow_multitexture_depth_supported(ctx: &DeviceState) -> bool {
    shadow_multitexture_supported(ctx) &&
        ctx.adapter.get_downlevel_capabilities().flags.contains(wgpu::DownlevelFlags::MULTISAMPLED_SHADING)
}

// Shared admission for the extended receiver. Window submission must run this
// before acquiring a WSI image: dropping rejected Vulkan acquisitions can
// exhaust the swapchain even though no queue submission or serial was issued.
fn textures_for_shadow(state:&CoinWgpuRenderState,textures:&[CoinWgpuTexture])->bool {
    textures.get(state.texture_slot as usize).is_some_and(|image|matches!(image.format,11|12))
}

fn validate_shadow_multitexture_profile(
    ctx: &DeviceState,
    textures_slice: &[CoinWgpuTexture],
    states_slice: &[CoinWgpuRenderState],
    draws_slice: &[CoinWgpuDraw],
    draw_order: &[composition::CompositionItem],
    shadow_frame: Option<&ShadowFrame>,
    sample_count: u32,
    rasterization_policy: u32,
) -> Result<bool, (CoinWgpuStatus, String)> {
    // Inherited raster source states are compute inputs, not surface draws.
    let shadow_multitexture = shadow_frame.is_some() && draws_slice.iter()
        .filter_map(|draw| states_slice.get(draw.render_state_slot as usize))
        .any(|state| state.extra_textures.iter().any(|unit| unit.enabled != 0) ||
            (state.has_texture!=0 && textures_for_shadow(state,textures_slice)));
    if shadow_multitexture && sample_count > 1 && rasterization_policy != 0 {
        return Err((CoinWgpuStatus::Unsupported, "Shadow multitexture MSAA requires native rasterization".into()));
    }
    if shadow_multitexture && sample_count > 1 && states_slice.iter().any(custom_shadow_sample_depth) &&
        !shadow_multitexture_depth_supported(ctx) {
        return Err((CoinWgpuStatus::Unsupported, "Shadow multitexture custom MSAA depth requires 80 interstage components and sample shading".into()));
    }
    if shadow_multitexture && !shadow_multitexture_supported(ctx) {
        return Err((CoinWgpuStatus::Unsupported, "Shadow multitexture requires 80 interstage components, 11 sampled textures and 8 samplers".into()));
    }
    if shadow_multitexture {
        let shadow = shadow_frame.unwrap();
        let count = 1 + usize::from(shadow.second.is_some()) + usize::from(shadow.third.is_some()) +
            usize::from(shadow.fourth.is_some()) + shadow.extra.len();
        let extent = [shadow.second.as_ref(),shadow.third.as_ref(),shadow.fourth.as_ref()].into_iter()
            .flatten().chain(shadow.extra.iter()).map(|pass| pass.map_size).fold(shadow.map_size,u32::max);
        let source_bytes=std::iter::once(shadow.map_size).chain(
            [shadow.second.as_ref(),shadow.third.as_ref(),shadow.fourth.as_ref()].into_iter().flatten()
                .chain(shadow.extra.iter()).map(|pass|pass.map_size)).map(|size|u64::from(size)*u64::from(size)*20).sum::<u64>();
        let array_bytes=u64::from(extent)*u64::from(extent)*16*u64::from(shadow::array_layer_count(count as u32));
        if source_bytes+array_bytes>128*1024*1024 {
            return Err((CoinWgpuStatus::Unsupported,"Shadow source attachments and moment array exceed 128 MiB per apply".into()));
        }
        let modes = 1 | if draw_order.iter().any(|item| item.weighted) { 2 } else { 0 } |
            if draw_order.iter().any(|item| item.peel) { 4 } else { 0 };
        if !shadow_array_supported(ctx,extent,count as u32) || !shadow_multitexture_alpha_supported(ctx,sample_count,modes) {
            return Err((CoinWgpuStatus::Unsupported,"Complete shadow multitexture composition pipeline or moment array unavailable".into()));
        }
    }
    Ok(shadow_multitexture)
}

// The extended profile is selected only for an actual directional bump receiver
// or for directional bump plus a texture cascade. Legacy unit-zero bump retains
// its original two-pass renderer and exact cache semantics.
fn extended_bump_profile(
    states: &[CoinWgpuRenderState],
    textures: &[CoinWgpuTexture],
    draws: &[CoinWgpuDraw],
    shadow: Option<&ShadowFrame>, payload: &ValidatedBumpPayload, indices: &[u32],
) -> bool {
    let consumed = match bump_consumption_for_draws(payload, indices, draws) {
        Ok(consumed) => consumed,
        Err(_) => return true, // Full admission reports the malformed occurrence.
    };
    shadow.is_some()
        || draws
            .iter()
            .zip(consumed)
            .filter_map(|(draw, active)| states.get(draw.render_state_slot as usize).map(|state| (state, active)))
            .any(|(state, active)| {
                if (active && state.light_count > 1) || state.extra_textures.iter().any(|unit| unit.enabled != 0) {
                    return true;
                }
                state.has_texture != 0
                    && (state.texture_model != 0
                        || state.texture_projection != 0
                        || state.texture_combines[0][0][0] != 0.0
                        || textures
                            .get(state.texture_slot as usize)
                            .is_some_and(|image| !matches!(image.format, 0 | 2 | 3 | 4)))
            })
}

fn bump_consumption_for_draws(payload: &ValidatedBumpPayload, indices: &[u32],
    draws: &[CoinWgpuDraw]) -> Result<Vec<bool>, String>
{
    draws.iter().map(|draw| {
        let first = draw.first_index as usize;
        let end = first.checked_add(draw.index_count as usize)
            .ok_or("Bump occurrence index range overflow")?;
        let range = indices.get(first..end).ok_or("Bump occurrence index range is invalid")?;
        let mut consumed = false;
        for &index in range {
            let vertex = payload.vertices.get(index as usize).ok_or("Bump occurrence vertex is invalid")?;
            if vertex.image_slot as usize > payload.images.len() {
                return Err("Bump occurrence image slot is invalid".into());
            }
            consumed |= vertex.image_slot != 0;
        }
        Ok(consumed)
    }).collect()
}

// No device, allocation or encoder is needed to reject malformed consumed
// lighting. Ordinary occurrences retain the common surface lighting contract.
fn validate_bump_lighting_occurrences(payload: &ValidatedBumpPayload,
    vertices: &[CoinWgpuVertex], indices: &[u32], draws: &[CoinWgpuDraw],
    states: &[CoinWgpuRenderState], materials: &[CoinWgpuMaterial],
    textures: &[CoinWgpuTexture], shadow: Option<&ShadowFrame>)
    -> Result<(), (CoinWgpuStatus, String)>
{
    let consumed = bump_consumption_for_draws(payload, indices, draws)
        .map_err(|message| (CoinWgpuStatus::InvalidArgument, message))?;
    if payload.vertices.len() != vertices.len() {
        return Err((CoinWgpuStatus::InvalidArgument, "Invalid bump stream extent".into()));
    }
    let extended = extended_bump_profile(states, textures, draws, shadow, payload, indices);
    for (draw, active) in draws.iter().zip(consumed) {
        if !active { continue; }
        let state = states.get(draw.render_state_slot as usize)
            .ok_or((CoinWgpuStatus::InvalidArgument, "Invalid bump lighting state slot".into()))?;
        if state.light_model != 1 || state.light_count == 0 ||
            state.light_count > (if extended { 8 } else { 1 }) {
            return Err((CoinWgpuStatus::Unsupported,
                "Consumed bump requires PHONG and qualified lighting".into()));
        }
        let material = materials.get(state.material_slot as usize)
            .ok_or((CoinWgpuStatus::InvalidArgument, "Invalid bump lighting material slot".into()))?;
        for light in &state.lights[..state.light_count as usize] {
            if shadow.is_some() && light.position_type[3] != 0.0 {
                return Err((CoinWgpuStatus::Unsupported, "Bump shadows require directional lights".into()));
            }
            let mut one = *state;
            one.light_count = 1;
            one.lights[0] = *light;
            bump_uniforms_from_state(&one, material)
                .map_err(|message| (CoinWgpuStatus::Unsupported, message))?;
        }
    }
    Ok(())
}

// Respect the authored RTT NONE declaration before inspecting image alpha.
// Otherwise check authored texels, never zero-filled physical atlas padding.
fn bump_receiver_texture_opaque(
    ctx: &DeviceState,
    image: &CoinWgpuTexture,
) -> Result<bool, (CoinWgpuStatus, String)> {
    let declared_opaque = image.reserved & TEXTURE_DECLARED_OPAQUE != 0;
    if gpu_texture_token(image) {
        let registry = ctx.rtt_textures.lock().unwrap();
        let entry = registry.active.get(&image.content_digest).ok_or((
            CoinWgpuStatus::InvalidArgument,
            "Bump receiver RTT token is stale or from another device/generation".into(),
        ))?;
        // Token metadata can validate a positive claim, but an unset claim
        // does not establish opacity. NONE is the explicit author override;
        // otherwise only the owning registry may prove the resource opaque.
        return Ok(declared_opaque || entry.opaque);
    }
    if image.pixels.is_null() {
        return Err((
            CoinWgpuStatus::InvalidArgument,
            "Null bump receiver texture pixels".into(),
        ));
    }
    let pixels =
        unsafe { std::slice::from_raw_parts(image.pixels, image.pixel_bytes_len as usize) };
    let alpha = |pixel: usize| pixels.get(pixel * 4 + 3).is_some_and(|value| *value == 255);
    if let Some((mut n, _, mips)) = cube_atlas_metadata(image)? {
        let mut texels = 0usize;
        loop {
            texels += 6 * n as usize * n as usize;
            if !mips || n == 1 { break; }
            n /= 2;
        }
        return Ok(declared_opaque || (0..texels).all(alpha));
    }
    if let Some((mut width, mut height, mut depth, mips)) = spatial_atlas_metadata(image)? {
        if declared_opaque {
            return Ok(true);
        }
        let columns = image.width / width;
        let mut base = 0;
        loop {
            for z in 0..depth {
                for y in 0..height {
                    for x in 0..width {
                        let pixel = (((base + z / columns * height + y) * image.width) +
                            z % columns * width + x) as usize;
                        if !alpha(pixel) { return Ok(false); }
                    }
                }
            }
            base += height * depth.div_ceil(columns);
            if !mips || (width == 1 && height == 1 && depth == 1) { break; }
            width = (width / 2).max(1);
            height = (height / 2).max(1);
            depth = (depth / 2).max(1);
        }
        return Ok(true);
    }
    let levels = texture_levels(image.width, image.height, image.format)?;
    let &(width, height, offset) = levels.last().ok_or((
        CoinWgpuStatus::InvalidArgument,
        "Empty bump texture levels".into(),
    ))?;
    let format = texture_gpu_format(image)?;
    if offset.checked_add(texture_profile::level_bytes(width, height, format))
        != Some(image.pixel_bytes_len)
    {
        return Err((
            CoinWgpuStatus::InvalidArgument,
            "Bump receiver texture byte count mismatch".into(),
        ));
    }
    if declared_opaque {
        return Ok(true);
    }
    match image.format {
        0 | 2 | 3 | 4 => Ok((0..pixels.len() / 4).all(alpha)),
        5 | 6 => Ok(pixels
            .chunks_exact(8)
            .take(image.width as usize * image.height as usize)
            .all(|pixel| u16::from_le_bytes([pixel[6], pixel[7]]) >= 0x3c00)),
        7..=10 => {
            let blocks = (image.width + 3) / 4;
            for y in 0..image.height {
                for x in 0..image.width {
                    let offset = ((y / 4 * blocks + x / 4) * 16) as usize;
                    let block = pixels.get(offset..offset + 16).ok_or((
                        CoinWgpuStatus::InvalidArgument,
                        "Short BC3 bump base".into(),
                    ))?;
                    let bits = block[2..8]
                        .iter()
                        .enumerate()
                        .fold(0u64, |bits, (i, b)| bits | (u64::from(*b) << (8 * i)));
                    let code = (bits >> (3 * ((y % 4) * 4 + x % 4))) & 7;
                    let a0 = u32::from(block[0]);
                    let a1 = u32::from(block[1]);
                    let value = match code {
                        0 => a0,
                        1 => a1,
                        7 if a0 <= a1 => 255,
                        6 if a0 <= a1 => 0,
                        _ if a0 > a1 => ((8 - code as u32) * a0 + (code as u32 - 1) * a1) / 7,
                        _ => ((6 - code as u32) * a0 + (code as u32 - 1) * a1) / 5,
                    };
                    if value != 255 {
                        return Ok(false);
                    }
                }
            }
            Ok(true)
        }
        _ => Err((
            CoinWgpuStatus::InvalidArgument,
            "Invalid bump receiver texture format".into(),
        )),
    }
}

fn validate_extended_bump_profile(
    ctx: &DeviceState,
    vertices: &[CoinWgpuVertex],
    indices: &[u32],
    draws: &[CoinWgpuDraw],
    states: &[CoinWgpuRenderState],
    materials: &[CoinWgpuMaterial],
    textures: &[CoinWgpuTexture],
    samplers: &[CoinWgpuSampler],
    payload: &ValidatedBumpPayload,
    shadow: Option<&ShadowFrame>,
    target_size: [u32; 2],
) -> Result<(), (CoinWgpuStatus, String)> {
    let unsupported = |message: &str| (CoinWgpuStatus::Unsupported, message.to_owned());
    if let Some(shadow) = shadow {
        if shadow.kind != 0 || shadow.second.iter().chain(shadow.third.iter())
            .chain(shadow.fourth.iter()).chain(shadow.extra.iter()).any(|pass|pass.kind!=0)
        {
            return Err(unsupported("Bump shadows require directional maps"));
        }
    }
    if ctx.device.limits().max_inter_stage_shader_components < 80
        || ctx.device.limits().max_sampled_textures_per_shader_stage < 12
        || ctx.device.limits().max_samplers_per_shader_stage < 9
        || ctx.device.limits().max_storage_buffers_per_shader_stage < 1
    {
        return Err(unsupported("Bump receiver needs 80 varying components, 12 texture bindings, nine samplers and vertex storage"));
    }
    admit_spatial_resources(ctx, textures)?;
    {
        let registry = ctx.rtt_textures.lock().unwrap();
        for image in textures.iter().filter(|image| image.format == 1) {
            let entry = registry.active.get(&image.content_digest).ok_or((
                CoinWgpuStatus::InvalidArgument,
                "Bump receiver RTT token is stale or from another device/generation".into(),
            ))?;
            if entry.width != image.width
                || entry.height != image.height
                || entry.cube_face_size != 0
                || texture_metadata(image) & !31 != 0
                || (texture_metadata(image) & 1 != 0 && !entry.opaque)
                || texture_metadata(image) >> 2 != entry.format
                || !image.pixels.is_null()
                || image.pixel_bytes_len != 0
                || (texture_metadata(image) & 2 != 0
                    && entry.mip_count <= 1
                    && (image.width > 1 || image.height > 1))
            {
                return Err((
                    CoinWgpuStatus::InvalidArgument,
                    "Bump receiver RTT token metadata mismatch".into(),
                ));
            }
        }
    }
    let mut largest_draw = 0usize;
    for draw in draws {
        let start = draw.first_index as usize;
        let end = start
            .checked_add(draw.index_count as usize)
            .filter(|end| *end <= indices.len())
            .ok_or_else(|| unsupported("Bump receiver draw indices out of bounds"))?;
        let unique: std::collections::HashSet<_> = indices[start..end].iter().copied().collect();
        largest_draw = largest_draw.max(unique.len());
    }
    let material_count = largest_draw
        .checked_add(1)
        .and_then(|count| count.checked_mul(std::mem::size_of::<GpuMaterial>()))
        .ok_or_else(|| unsupported("Bump receiver material stream overflow"))?;
    if material_count as u64 > u64::from(ctx.device.limits().max_storage_buffer_binding_size)
        || material_count as u64 > ctx.device.limits().max_buffer_size
    {
        return Err(unsupported(
            "Bump receiver corner stream exceeds the device buffer binding limit",
        ));
    }
    if payload.vertices.len() != vertices.len()
        || payload.images.iter().any(|image| {
            image.width > ctx.device.limits().max_texture_dimension_2d
                || image.height > ctx.device.limits().max_texture_dimension_2d
        })
    {
        return Err(unsupported(
            "Bump receiver vertex count or normal image extent is invalid",
        ));
    }
    bump_image_slots_for_draws(payload, indices, draws)
        .map_err(|message| (CoinWgpuStatus::InvalidArgument, message))?;
    let consumed = bump_consumption_for_draws(payload, indices, draws)
        .map_err(|message| (CoinWgpuStatus::InvalidArgument, message))?;
    for (draw, active) in draws.iter().zip(consumed) {
        let state = states
            .get(draw.render_state_slot as usize)
            .ok_or_else(|| unsupported("Bump receiver state slot out of bounds"))?;
        if draw.topology != 0
            || draw.render_layer != 0
            || draw.composition_flags != 0
            || draw.clear_depth_before != 0
            || (active && (state.light_model != 1 || state.light_count == 0 || state.light_count > 8
                || state.lights[..state.light_count.min(8) as usize].iter()
                    .any(|light| !matches!(light.position_type[3],0.0|1.0|2.0) ||
                        (shadow.is_some() && light.position_type[3]!=0.0))))
            || state.fog_mode != 0
            || state.depth_test != 1
            || state.depth_write != 1
            || state.cull_mode != 0
            || !matches!(state.depth_function, 2 | 3)
            || state.depth_range != [0.0, 1.0]
            || state.polygon_offset_enabled != 0
            || state.clip_plane_count != 0
            || state.alpha_test_function != 0
            || (state.viewport != [0, 0, 0, 0]
                && state.viewport != [0, 0, target_size[0] as i32, target_size[1] as i32])
        {
            return Err(unsupported("Bump receiver requires opaque one-directional-light triangles and default full-target depth"));
        }
        let end = draw.first_index as usize + draw.index_count as usize;
        let draw_indices = &indices[draw.first_index as usize..end];
        validate_texture_coordinates_with_images(
            state,
            draw.topology,
            vertices,
            draw_indices,
            textures,
        )
        .map_err(|message| (CoinWgpuStatus::InvalidArgument, message))?;
        for triangle in draw_indices.chunks_exact(3) {
            let first = &payload.vertices[triangle[0] as usize];
            if first.image_slot == 0 {
                continue;
            }
            for &index in triangle {
                if state.light_count > 1 {
                    let corner_material = &materials[vertices[index as usize].material_slot as usize];
                    if corner_material.transparency != 0.0 || corner_material.diffuse[3] != 1.0 ||
                        !corner_material.shininess.is_finite() ||
                        !(0.0..=1.0).contains(&corner_material.shininess) ||
                        state.lights[..state.light_count as usize].iter().any(|light| {
                            (0..3).any(|channel| !(corner_material.diffuse[channel] *
                                light.color_intensity[channel] * light.color_intensity[3]).is_finite())
                        }) {
                        return Err(unsupported("Invalid multilight bump corner material product"));
                    }
                }
                let coord = payload.vertices[index as usize].coord;
                if coord[3].abs() < f32::MIN_POSITIVE
                    || coord[3].is_sign_negative() != first.coord[3].is_sign_negative()
                    || !(coord[0] / coord[3]).is_finite()
                    || !(coord[1] / coord[3]).is_finite()
                {
                    return Err(unsupported("Invalid bump receiver projected Q"));
                }
            }
        }
        let material = materials
            .get(state.material_slot as usize)
            .ok_or_else(|| unsupported("Bump receiver material slot out of bounds"))?;
        for index in 0..(if active { state.light_count as usize } else { 0 }) {
            let mut one=*state;
            one.light_count=1;
            one.lights[0]=state.lights[index];
            bump_uniforms_from_state(&one, material)
                .map_err(|message| (CoinWgpuStatus::Unsupported, message))?;
        }
        let layers: [CoinWgpuTextureUnit; 8] = std::array::from_fn(|unit| {
            if unit == 0 {
                CoinWgpuTextureUnit {
                    matrix: state.texture_matrix,
                    enabled: state.has_texture,
                    texture_slot: state.texture_slot,
                    sampler_slot: state.sampler_slot,
                    model: state.texture_model,
                    blend_color: state.texture_blend_color,
                }
            } else {
                state.extra_textures[unit - 1]
            }
        });
        for layer in layers.iter().filter(|layer| layer.enabled != 0) {
            let image = textures
                .get(layer.texture_slot as usize)
                .ok_or_else(|| unsupported("Bump receiver texture slot out of bounds"))?;
            if active && (!matches!(image.format, 0 | 1 | 2 | 3 | 4 | 11 | 12) ||
                (image.format == 1 && texture_metadata(image) >> 2 != 0)) {
                return Err(unsupported("Bump receiver bases require RGBA8 linear/sRGB sources"));
            }
            if !bump_receiver_texture_opaque(ctx, image)? {
                return Err(unsupported("Bump receiver textures must be opaque"));
            }
        }
    }
    validate_spatial_resources(states, textures, samplers)?;
    Ok(())
}

// GPU material storage is indexed per corner only in the dedicated receiver.
// Its five vec4s preserve the original single-light packing. Multi-DIR packs
// coord, per-corner diffuse/specular/shininess and S/T/N in the same 80 bytes.
// The VS projects each legacy L and E before interpolation. All thirteen
// geometry attributes and the private STRQ transport remain intact.
fn encode_extended_bump_draw(
    ctx: &DeviceState,
    vertices: &[CoinWgpuVertex],
    indices: &[u32],
    draw: &CoinWgpuDraw,
    states: &[CoinWgpuRenderState],
    materials: &[CoinWgpuMaterial],
    textures: &[CoinWgpuTexture],
    samplers: &[CoinWgpuSampler],
    payload: &ValidatedBumpPayload,
    image: &GpuBumpImage,
    shadow: Option<&ShadowFrame>,
    color_view: &wgpu::TextureView,
    resolve_view: Option<&wgpu::TextureView>,
    color_format: wgpu::TextureFormat,
    depth_view: &wgpu::TextureView,
    depth_texture: &wgpu::Texture,
    target_size: [u32; 2],
    clear_color: [f32; 4],
    sample_count: u32,
    sampling_policy: u32,
    clear: bool,
    shared_shadow: Option<&(Vec<shadow::ShadowMap>, shadow::ShadowArray)>,
) -> Result<(wgpu::CommandBuffer, Option<InstancedGpuBuffers>), (CoinWgpuStatus, String)> {
    let state = &states[draw.render_state_slot as usize];
    let mut first_light=*state;
    first_light.light_count=1;
    let uniform = bump_uniforms_from_state(&first_light, &materials[state.material_slot as usize])
        .map_err(|message| (CoinWgpuStatus::Unsupported, message))?;
    let source_material = materials[state.material_slot as usize];
    let mut geometry = Vec::new();
    let mut compact_indices = Vec::with_capacity(draw.index_count as usize);
    let mut remap = HashMap::new();
    let mut cpu_materials = vec![source_material];
    let mut gpu_materials = pack_gpu_materials(&cpu_materials);
    let normalize = |mut vector: [f32; 3]| {
        let square = vector.iter().map(|x| x * x).sum::<f32>();
        if square > 0.0 {
            for value in &mut vector {
                *value /= square.sqrt();
            }
        }
        vector
    };
    let dot = |a: [f32; 3], b: [f32; 3]| a.iter().zip(b).map(|(a, b)| a * b).sum::<f32>();
    let end = draw.first_index as usize + draw.index_count as usize;
    for &index in &indices[draw.first_index as usize..end] {
        if let Some(&mapped) = remap.get(&index) {
            compact_indices.push(mapped);
            continue;
        }
        let mapped = geometry.len() as u32;
        remap.insert(index, mapped);
        compact_indices.push(mapped);
        let mut vertex = vertices[index as usize];
        let corner_material=materials[vertex.material_slot as usize];
        vertex.material_slot = gpu_materials.len() as u32;
        let corner = &payload.vertices[index as usize];
        let light = normalize(std::array::from_fn(|axis| {
            uniform.light_position_type[axis] -
              if uniform.light_position_type[3] != 0.0 { vertex.position[axis] } else { 0.0 }
        }));
        let eye = normalize(std::array::from_fn(|axis| {
            uniform.eye_position[axis] - vertex.position[axis]
        }));
        let tangent_light = [
            dot(corner.tangent_s, light),
            dot(corner.tangent_t, light),
            dot(vertex.normal, light),
        ];
        let tangent_eye = [
            dot(corner.tangent_s, eye),
            dot(corner.tangent_t, eye),
            dot(vertex.normal, eye),
        ];
        if state.light_count>1 {
            // Same five vec4s per corner: coord, per-corner material and the
            // original S/T/N basis. No new vertex attribute or wire field.
            gpu_materials.push(GpuMaterial {
                ambient:corner.coord,
                diffuse:[corner_material.diffuse[0],corner_material.diffuse[1],
                         corner_material.diffuse[2],corner_material.shininess],
                specular:[corner_material.specular[0],corner_material.specular[1],
                          corner_material.specular[2],vertex.normal[0]],
                emission:[corner.tangent_s[0],corner.tangent_s[1],corner.tangent_s[2],vertex.normal[1]],
                params:[corner.tangent_t[0],corner.tangent_t[1],corner.tangent_t[2],vertex.normal[2]],
            });
        } else {
        gpu_materials.push(GpuMaterial {
            ambient: corner.coord,
            diffuse: uniform.diffuse,
            specular: uniform.specular,
            emission: [tangent_light[0], tangent_light[1], tangent_light[2], 0.0],
            params: [
                uniform.shininess[0],
                tangent_eye[0],
                tangent_eye[1],
                tangent_eye[2],
            ],
        });
        }
        cpu_materials.push(if state.light_count>1 {corner_material}else{source_material});
        geometry.push(vertex);
    }
    let mut draw = *draw;
    draw.stable_node_id = 0;
    draw.first_vertex = 0;
    draw.vertex_count = geometry.len() as u32;
    draw.first_index = 0;
    let mut states = states.to_vec();
    for state in &mut states {
        state.material_slot = 0;
    }
    let order = composition::order(
        &geometry,
        &compact_indices,
        &[draw],
        &cpu_materials,
        &states,
        textures,
    )?;
    encode_frame_impl(
        ctx,
        0,
        clear_color,
        target_size[0],
        target_size[1],
        &geometry,
        &compact_indices,
        &[draw],
        &order,
        &cpu_materials,
        &states,
        &[],
        textures,
        samplers,
        shadow,
        None,
        &[],
        &[],
        None,
        color_view,
        resolve_view,
        color_format,
        depth_view,
        depth_texture,
        sample_count,
        0,
        sampling_policy,
        0,
        clear,
        Some((&image.view, &image.sampler, &gpu_materials)),
        shared_shadow,
    )
}

// Opaque runs share attachments and one public submission, while each encoder
// owns its uniforms and geometry buffers. The inactive runs use the established
// Phong encoder, including its base texture and depth semantics.
fn encode_mixed_bump_frame(
    ctx: &DeviceState,
    pipeline_cache: Option<&Mutex<HashMap<BumpPipelineKey, Arc<BumpPipelines>>>>,
    base_cache: Option<(&Mutex<BumpBaseCache>, u64, u64)>,
    vertices: &[CoinWgpuVertex],
    indices: &[u32],
    draws: &[CoinWgpuDraw],
    states: &[CoinWgpuRenderState],
    portable_modules: &[portable::Module],
    materials: &[CoinWgpuMaterial],
    textures: &[CoinWgpuTexture],
    samplers: &[CoinWgpuSampler],
    payload: &ValidatedBumpPayload,
    shadow_frame: Option<&ShadowFrame>,
    color_view: &wgpu::TextureView,
    resolve_view: Option<&wgpu::TextureView>,
    color_format: wgpu::TextureFormat,
    depth_view: &wgpu::TextureView,
    target_size: [u32; 2],
    clear_color: [f32; 4],
    sample_count: u32,
    depth_texture: &wgpu::Texture,
    sampling_policy: u32,
    outer_shadow: Option<&(Vec<shadow::ShadowMap>, shadow::ShadowArray)>,
) -> Result<(Vec<wgpu::CommandBuffer>, Option<InstancedGpuBuffers>), (CoinWgpuStatus, String)> {
    if !matches!(sample_count, 1 | 2 | 4)
        || resolve_view.is_some() != (sample_count > 1)
        || draws.is_empty()
        || payload.vertices.len() != vertices.len()
        || !clear_color.iter().all(|value| value.is_finite())
    {
        return Err((
            CoinWgpuStatus::InvalidArgument,
            "Invalid mixed bump attachments or geometry".into(),
        ));
    }
    let slots = bump_image_slots_for_draws(payload, indices, draws)
        .map_err(|message| (CoinWgpuStatus::InvalidArgument, message))?;
    // Validate the complete profile before any run allocates/uploads resources.
    // Lighting requirements belong only to active normal-map consumers;
    // ordinary runs retain their own BASE_COLOR/Phong surface executor.
    let extended = extended_bump_profile(states, textures, draws, shadow_frame, payload, indices);
    if extended {
        validate_extended_bump_profile(
            ctx,
            vertices,
            indices,
            draws,
            states,
            materials,
            textures,
            samplers,
            payload,
            shadow_frame,
            target_size,
        )?;
    } else {
        validate_bump_draw_profile(
            &ctx.device,
            vertices,
            indices,
            draws,
            states,
            materials,
            textures,
            samplers,
            target_size,
            &slots.iter().map(Option::is_some).collect::<Vec<_>>(),
        )?;
    }
    let extended_gpu = if extended {
        Some(
            upload_bump_payload(&ctx.device, &ctx.queue, payload)
                .map_err(|message| (CoinWgpuStatus::Unsupported, message))?,
        )
    } else {
        None
    };
    if payload.images.iter().any(|image| {
        image.width > ctx.device.limits().max_texture_dimension_2d
            || image.height > ctx.device.limits().max_texture_dimension_2d
    }) {
        return Err((
            CoinWgpuStatus::Unsupported,
            "Bump image exceeds GPU texture dimension limit".into(),
        ));
    }
    let mut commands = Vec::new();
    // One geometric caster pass per frame, shared by every ordered surface run.
    // Cloned handles retain the same allocations through the single queue submit.
    let owned_shadow = if extended && outer_shadow.is_none() {
        if let Some(shadow) = shadow_frame {
            let mut encoder = ctx
                .device
                .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                    label: Some("Coin shared bump receiver moments"),
                });
            let mut maps=Vec::new();
            maps.push(shadow::encode_moments(&ctx.device,&mut encoder,vertices,indices,
                shadow.casters,states,shadow.map_size,shadow.near,shadow.far,shadow.kind,false)
                .map_err(|message|(CoinWgpuStatus::InvalidArgument,message))?);
            for pass in shadow.second.iter().chain(shadow.third.iter())
                .chain(shadow.fourth.iter()).chain(shadow.extra.iter()) {
                maps.push(shadow::encode_moments(&ctx.device,&mut encoder,vertices,indices,
                    pass.casters,states,pass.map_size,pass.near,pass.far,pass.kind,false)
                    .map_err(|message|(CoinWgpuStatus::InvalidArgument,message))?);
            }
            let references:Vec<_>=maps.iter().collect();
            let array=shadow::encode_array(&ctx.device,&mut encoder,&references)
                .map_err(|message|(CoinWgpuStatus::Unsupported,message))?;
            commands.push(encoder.finish());
            Some((maps, array))
        } else {
            None
        }
    } else {
        None
    };
    let shared_shadow = outer_shadow.or(owned_shadow.as_ref());
    let mut start = 0;
    while start < draws.len() {
        let active = slots[start].is_some();
        let mut end = start + 1;
        while end < draws.len() && slots[end].is_some() == active && !(extended && active) {
            end += 1;
        }
        let run = &draws[start..end];
        let resolve = if end == draws.len() {
            resolve_view
        } else {
            None
        };
        if active && extended {
            let image = &extended_gpu.as_ref().unwrap().images[slots[start].unwrap()];
            commands.push(
                encode_extended_bump_draw(
                    ctx,
                    vertices,
                    indices,
                    &draws[start],
                    states,
                    materials,
                    textures,
                    samplers,
                    payload,
                    image,
                    shadow_frame,
                    color_view,
                    resolve,
                    color_format,
                    depth_view,
                    depth_texture,
                    target_size,
                    clear_color,
                    sample_count,
                    sampling_policy,
                    start == 0,
                    shared_shadow,
                )?
                .0,
            );
        } else if active {
            commands.push(encode_bump_opaque_run(
                &ctx.device,
                &ctx.queue,
                pipeline_cache,
                base_cache,
                vertices,
                indices,
                run,
                states,
                materials,
                textures,
                samplers,
                payload,
                color_view,
                resolve,
                color_format,
                depth_view,
                target_size,
                clear_color,
                sample_count,
                start == 0,
            )?);
        } else {
            let order = composition::order(vertices, indices, run, materials, states, textures)?;
            // Revision zero and no camera/instance proof keep per-run material
            // and uniform allocations independent before the single submit.
            let (command, _) = encode_frame_impl(
                ctx,
                0,
                clear_color,
                target_size[0],
                target_size[1],
                vertices,
                indices,
                run,
                &order,
                materials,
                states,
                portable_modules,
                textures,
                samplers,
                shadow_frame,
                None,
                &[],
                &[],
                None,
                color_view,
                resolve,
                color_format,
                depth_view,
                depth_texture,
                sample_count,
                0,
                sampling_policy,
                0,
                start == 0,
                None,
                shared_shadow,
            )?;
            commands.push(command);
        }
        start = end;
    }
    Ok((commands, None))
}

// Private multipass metadata is validated before any adapter acquisition,
// resource/cache mutation or producer command. It never dereferences a payload
// until the common pointer/count/alignment proof succeeds.
fn validate_multipass_metadata(frame:&CoinWgpuFrameView,
    error:*mut std::os::raw::c_char, error_len:usize) -> Result<(),CoinWgpuStatus> {
    let invalid=|message:&str| {set_error(error,error_len,message);CoinWgpuStatus::InvalidArgument};
    if !(1..=16).contains(&frame.multipass_count) || frame.multipass_reserved!=0 {
        return Err(invalid("Invalid multipass count or reserved field"));
    }
    if frame.multipass_matrix_count==0 {
        if !frame.multipass_matrices.is_null() {return Err(invalid("Unexpected multipass matrix pointer"));}
    } else {
        let expected=frame.state_count.checked_mul(u64::from(frame.multipass_count)).and_then(|n|n.checked_mul(16));
        if frame.multipass_count==1 || expected!=Some(frame.multipass_matrix_count) {
            return Err(invalid("Invalid multipass matrix payload count"));
        }
        let values=validate_slice(frame.multipass_matrices,frame.multipass_matrix_count,"multipass matrices",error,error_len)?;
        if values.iter().any(|value|!value.is_finite()) {return Err(invalid("Non-finite multipass matrix payload"));}
    }
    if frame.multipass_clear_color_count==0 {
        if !frame.multipass_clear_colors.is_null() { return Err(invalid("Unexpected snapshot clear pointer")); }
    } else {
        if frame.multipass_clear_color_count!=u64::from(frame.multipass_count)*4 {
            return Err(invalid("Invalid snapshot clear payload count"));
        }
        let values=validate_slice(frame.multipass_clear_colors,frame.multipass_clear_color_count,
            "snapshot clears",error,error_len)?;
        if values.iter().any(|value|!value.is_finite()) { return Err(invalid("Non-finite snapshot clear color")); }
    }

    if frame.multipass_count>1 {
        let minimum=multipass::bytes(frame.width,frame.height)
            .and_then(|n|frame.multipass_matrix_count.checked_mul(4).and_then(|m|n.checked_add(m)));
        if minimum.is_none_or(|bytes|bytes>frame.multipass_budget_bytes) {
            set_error(error,error_len,"Multipass accumulation and projection payload exceed budget");
            return Err(CoinWgpuStatus::OutOfMemory);
        }
    }
    Ok(())
}

// Validate the resolved draw source: either structurally checked caller slices
// or the immutable snapshot proven by camera_base. Camera patches intentionally
// omit caller geometry while retaining the snapshot's counts and draw masks.
fn validate_multipass_draws(frame:&CoinWgpuFrameView, draws:&[CoinWgpuDraw],
    error:*mut std::os::raw::c_char, error_len:usize) -> Result<(),CoinWgpuStatus> {
    let invalid=|message:&str| {set_error(error,error_len,message);CoinWgpuStatus::InvalidArgument};
    if !(1..=16).contains(&frame.multipass_count) || draws.len() as u64!=frame.draw_count {
        return Err(invalid("Invalid multipass draw count"));
    }
    for draw in draws {
        let mask=(draw.composition_flags>>15)&0xffff;
        if mask>>frame.multipass_count!=0 || (frame.multipass_count>1 &&
            draw.composition_flags&0x80000000!=0 && frame.multipass_matrix_count==0) {
            return Err(invalid("Invalid multipass draw mask or missing raster matrices"));
        }
    }
    Ok(())
}

// Raw ABI callers receive the same simultaneous-resource admission as the
// C++ connector. All slices used here were structurally validated by ingress.
fn multipass_pending_bytes(runtime: Option<&RuntimeContext>) -> Option<u64> {
    runtime.map_or(Some(0), |runtime| runtime.pending_readbacks.values()
        .chain(runtime.retired_readbacks.iter()).try_fold(0u64,|sum,job|
            sum.checked_add(job.color.size())?.checked_add(job.depth.as_ref().map_or(0,|depth|depth.size()))))
}
fn admit_multipass_resources(frame:&CoinWgpuFrameView, samples:u32,
    states:&[CoinWgpuRenderState], textures:&[CoinWgpuTexture],
    order:&[composition::CompositionItem], shadow:Option<&ShadowFrame>,
    window:bool, readback:bool, depth:bool, pending:Option<u64>)
    -> Result<(),(CoinWgpuStatus,String)> {
    if frame.multipass_count==1 {return Ok(());}
    let bytes=(|| -> Option<u64> {
        let width=u64::from(frame.width); let height=u64::from(frame.height);
        let mut total=pending?;
        let mut charge=|w:u64,h:u64,b:u64,n:u64| -> Option<()> {
            total=total.checked_add(w.checked_mul(h)?.checked_mul(b)?.checked_mul(n)?)?;
            Some(())
        };
        let color=if frame.texture_output_flags&1!=0 {8}else{4};
        charge(width,height,32,1)?;
        charge(width,height,color+4,u64::from(samples))?;
        if samples>1 {charge(width,height,color+4,1)?;}
        charge(frame.multipass_matrix_count,1,4,1)?;
        if window {charge(width,height,4,1)?;}
        if readback {
            let pitch=width.checked_mul(4)?.checked_add(255)? & !255;
            charge(pitch,height,1,if depth {2}else{1})?;
            if depth && samples>1 {charge(width,height,4,1)?;}
        }
        if frame.texture_output_flags&2!=0 {
            let mut w=width;let mut h=height;
            while w>1 || h>1 {w=(w/2).max(1);h=(h/2).max(1);charge(w,h,color,1)?;}
        }
        if order.iter().any(|item|item.peel) {
            charge(width,height,4,u64::from(samples))?;
            charge(width,height,13,u64::from(samples).checked_mul(u64::from(frame.sorted_layers_passes))?)?;
        }
        if order.iter().any(|item|item.weighted) {charge(width,height,10,u64::from(samples))?;}
        if let Some(shadow)=shadow {
            let sizes:Vec<_>=std::iter::once(shadow.map_size).chain(shadow.second.iter()
                .chain(shadow.third.iter()).chain(shadow.fourth.iter()).chain(shadow.extra.iter())
                .map(|pass|pass.map_size)).collect();
            for &size in &sizes {charge(u64::from(size),u64::from(size),20,1)?;}
            let size=u64::from(*sizes.iter().max()?);
            charge(size,size,16,u64::from(shadow::array_layer_count(sizes.len() as u32)))?;
        }
        let mut tokens=std::collections::HashSet::new();
        for image in textures.iter().filter(|image|gpu_texture_token(image)) {
            if !tokens.insert(image.content_digest) {continue;}
            let format=if image.format==1 {texture_metadata(image)>>2}else{0};
            let bpp=if format==2 {8}else{4};
            charge(u64::from(image.width),u64::from(image.height),bpp,1)?;
            if image.format==1 && texture_metadata(image)&2!=0 {
                let mut w=u64::from(image.width);let mut h=u64::from(image.height);
                while w>1 || h>1 {w=(w/2).max(1);h=(h/2).max(1);charge(w,h,bpp,1)?;}
            }
        }
        for state in states.iter().filter(|state|state.raster_texture_source_slot!=0) {
            let image=textures.get(state.texture_slot as usize)?;
            charge(u64::from(image.width),u64::from(image.height),16,1)?;
        }
        Some(total)
    })();
    if bytes.is_none_or(|bytes|bytes>frame.multipass_budget_bytes) {
        Err((CoinWgpuStatus::OutOfMemory,"Multipass simultaneous scene, composition, shadow, RTT and staging resources exceed budget".into()))
    } else {Ok(())}
}

// Pure shared command encoder function strictly common to offscreen and window targets
fn encode_scene_samples(
    ctx: &DeviceState, frame: &CoinWgpuFrameView, size: [u32;2],
    vertices: &[CoinWgpuVertex], indices: &[u32], draws: &[CoinWgpuDraw],
    order: &[composition::CompositionItem], materials: &[CoinWgpuMaterial],
    states: &[CoinWgpuRenderState], modules: &[portable::Module],
    textures: &[CoinWgpuTexture], samplers: &[CoinWgpuSampler],
    bump: Option<&ValidatedBumpPayload>, shadow: Option<&ShadowFrame>,
    camera_geometry: Option<&Arc<ValidatedGeometry>>,
    instances: &[CoinWgpuInstance], instance_ranges: &[CoinWgpuInstanceRange],
    timestamp: Option<&wgpu::QuerySet>, color: &wgpu::TextureView,
    resolved: Option<&wgpu::TextureView>, format: wgpu::TextureFormat,
    depth: &wgpu::TextureView, depth_texture: &wgpu::Texture, samples: u32,
) -> Result<(Vec<wgpu::CommandBuffer>,Option<InstancedGpuBuffers>),(CoinWgpuStatus,String)> {
    let count=frame.multipass_count;
    if !(1..=16).contains(&count) || frame.multipass_reserved!=0 {
        return Err((CoinWgpuStatus::InvalidArgument,"Invalid multipass request".into()));
    }
    if bump.is_some() && (!instances.is_empty() || !instance_ranges.is_empty()
        || order.iter().any(|item|item.peel||item.weighted) || frame.rasterization_policy!=0) {
        return Err((CoinWgpuStatus::Unsupported,"Bump frame cannot combine instancing or composition".into()));
    }
    let matrix_count=(states.len() as u64).checked_mul(u64::from(count)).and_then(|n|n.checked_mul(16))
        .ok_or((CoinWgpuStatus::InvalidArgument,"Multipass matrix count overflows".into()))?;
    let matrices=if frame.multipass_matrix_count==0 {
        if !frame.multipass_matrices.is_null() {
            return Err((CoinWgpuStatus::InvalidArgument,"Unexpected multipass matrix pointer".into()));
        }
        &[][..]
    } else {
        if count==1 || frame.multipass_matrix_count!=matrix_count {
            return Err((CoinWgpuStatus::InvalidArgument,"Invalid multipass matrix payload count".into()));
        }
        validate_slice(frame.multipass_matrices,frame.multipass_matrix_count,
            "multipass matrices",std::ptr::null_mut(),0)
            .map_err(|status|(status,"Invalid multipass matrix pointer/count/alignment".into()))?
    };
    if matrices.iter().any(|value|!value.is_finite()) || draws.iter().any(|draw|
        ((draw.composition_flags>>15)&0xffff)>>count != 0) {
        return Err((CoinWgpuStatus::InvalidArgument,"Invalid multipass matrices or sample mask".into()));
    }
    let clear_colors = if frame.multipass_clear_color_count == 0 {
        if !frame.multipass_clear_colors.is_null() {
            return Err((CoinWgpuStatus::InvalidArgument,"Unexpected snapshot clear pointer".into()));
        }
        &[][..]
    } else {
        if frame.multipass_clear_color_count != u64::from(count)*4 {
            return Err((CoinWgpuStatus::InvalidArgument,"Invalid snapshot clear count".into()));
        }
        validate_slice(frame.multipass_clear_colors,frame.multipass_clear_color_count,
            "snapshot clears",std::ptr::null_mut(),0)
            .map_err(|status|(status,"Invalid snapshot clear pointer/count/alignment".into()))?
    };
    if clear_colors.iter().any(|value|!value.is_finite()) {
        return Err((CoinWgpuStatus::InvalidArgument,"Non-finite snapshot clear color".into()));
    }
    let sample_states = |sample:u32| {
        if matrices.is_empty() { multipass::jitter_states(states,count,sample) }
        else {
            let mut output=states.to_vec();
            for (slot,state) in output.iter_mut().enumerate() {
                let first=(sample as usize*states.len()+slot)*16;
                state.model_view_projection.copy_from_slice(&matrices[first..first+16]);
            }
            output
        }
    };
    let encode = |sample_states:&[CoinWgpuRenderState], sample_draws:&[CoinWgpuDraw],
                  sample_order:&[composition::CompositionItem], shared, single:bool, sample_clear:[f32;4]| {
        if let Some(payload)=bump {
            encode_mixed_bump_frame(ctx,Some(&ctx.bump_pipelines),
                Some((&ctx.bump_base_cache,ctx.last_submitted_serial.load(Ordering::SeqCst)+1,
                    ctx.completed_serial.load(Ordering::SeqCst))),vertices,indices,sample_draws,sample_states,
                modules,materials,textures,samplers,payload,shadow,color,resolved,format,depth,
                size,sample_clear,samples,depth_texture,frame.texture_sampling_policy,shared)
        } else {
            encode_frame_impl(ctx,if single{frame.frame_revision}else{0},sample_clear,
                size[0],size[1],vertices,indices,sample_draws,sample_order,materials,sample_states,modules,textures,
                samplers,shadow,if single{camera_geometry}else{None},instances,instance_ranges,
                if single{timestamp}else{None},color,resolved,format,depth,depth_texture,samples,
                frame.sorted_layers_passes,frame.texture_sampling_policy,frame.rasterization_policy,
                true,None,shared).map(|(command,instances)|(vec![command],instances))
        }
    };
    if count==1 {return encode(states,draws,order,None,true,frame.clear_color);}
    let required=multipass::bytes(size[0],size[1])
        .ok_or((CoinWgpuStatus::OutOfMemory,"Multipass accumulation extent overflows".into()))?;
    if required>frame.multipass_budget_bytes {
        return Err((CoinWgpuStatus::OutOfMemory,"Multipass accumulation exceeds its buffer budget".into()));
    }
    let usage=wgpu::TextureUsages::STORAGE_BINDING|wgpu::TextureUsages::TEXTURE_BINDING;
    if !ctx.adapter.get_texture_format_features(wgpu::TextureFormat::Rgba32Float).allowed_usages.contains(usage) {
        return Err((CoinWgpuStatus::Unsupported,"Multipass RGBA32F accumulation unavailable".into()));
    }
    // Validate every translated matrix before issuing any sample commands.
    for sample in 0..count {
        if sample_states(sample).iter()
            .any(|state|state.model_view_projection.iter().any(|value|!value.is_finite())) {
            return Err((CoinWgpuStatus::InvalidArgument,"Multipass jitter projection overflows".into()));
        }
    }
    let _uploads=MultipassUploadScope::begin();
    let programs=ctx.multipass_programs.get(&ctx.device,format)
        .map_err(|error|(CoinWgpuStatus::BackendError,error))?;
    let accumulator=multipass::Accumulation::new(&ctx.device,size[0],size[1],count,programs);
    let shared_shadow=if let Some(shadow)=shadow {
        if FAULT_INJECTION.load(Ordering::SeqCst)==FAULT_SHADOW_MAP_ALLOC {
            FAULT_INJECTION.store(0,Ordering::SeqCst);
            return Err((CoinWgpuStatus::OutOfMemory,"Injected shadow-map attachment allocation failure".into()));
        }
        let mut encoder=ctx.device.create_command_encoder(&Default::default());
        let mut maps=Vec::new();
        maps.push(shadow::encode_moments(&ctx.device,&mut encoder,vertices,indices,
            shadow.casters,states,shadow.map_size,shadow.near,shadow.far,shadow.kind,
            frame.rasterization_policy==1).map_err(|error|(CoinWgpuStatus::InvalidArgument,error))?);
        for pass in shadow.second.iter().chain(shadow.third.iter()).chain(shadow.fourth.iter()).chain(shadow.extra.iter()) {
            maps.push(shadow::encode_moments(&ctx.device,&mut encoder,vertices,indices,
                pass.casters,states,pass.map_size,pass.near,pass.far,pass.kind,
                frame.rasterization_policy==1).map_err(|error|(CoinWgpuStatus::InvalidArgument,error))?);
        }
        let refs:Vec<_>=maps.iter().collect();
        let array=shadow::encode_array(&ctx.device,&mut encoder,&refs)
            .map_err(|error|(CoinWgpuStatus::Unsupported,error))?;
        ctx.queue.submit([encoder.finish()]);
        Some((maps,array))
    } else {None};
    let sample_view=resolved.unwrap_or(color);
    for sample in 0..count {
        let states_for_sample=sample_states(sample);
        let selected_draws:Vec<_>=draws.iter().filter_map(|draw| {
            let mask=(draw.composition_flags>>15)&0xffff;
            if mask==0 || mask&(1u32<<sample)!=0 {Some(*draw)}
            else if draw.clear_depth_before!=0 || draw.composition_flags&64!=0 {
                let mut barrier=*draw;
                barrier.index_count=0; barrier.vertex_count=0;
                Some(barrier)
            } else {None}
        }).collect();
        let selected_order=composition::order(vertices,indices,&selected_draws,materials,&states_for_sample,textures)?;
        let sample_clear=if clear_colors.is_empty(){frame.clear_color}else{
            let index=sample as usize*4;
            [clear_colors[index],clear_colors[index+1],clear_colors[index+2],clear_colors[index+3]]
        };
        let (mut commands,_)=encode(&states_for_sample,&selected_draws,&selected_order,shared_shadow.as_ref(),false,sample_clear)?;
        commands.push(accumulator.accumulate(&ctx.device,sample_view,sample));
        // A later encoder may update retained uniform buffers. Ordered private
        // submits make those writes follow this sample's draw commands; none
        // acquires/presents, publishes a serial or performs a readback.
        ctx.queue.submit(commands);
    }
    Ok((vec![accumulator.resolve(&ctx.device,sample_view)],None))
}

fn encode_frame(
    ctx: &DeviceState,
    frame_revision: u64,
    clear_color: [f32; 4],
    target_width: u32,
    target_height: u32,
    vertices_slice: &[CoinWgpuVertex],
    indices_slice: &[u32],
    draws_slice: &[CoinWgpuDraw],
    draw_order: &[composition::CompositionItem],
    materials_slice: &[CoinWgpuMaterial],
    states_slice: &[CoinWgpuRenderState],
    portable_modules: &[portable::Module],
    textures_slice: &[CoinWgpuTexture],
    samplers_slice: &[CoinWgpuSampler],
    shadow_frame: Option<&ShadowFrame>,
    camera_geometry: Option<&Arc<ValidatedGeometry>>,
    instances_slice: &[CoinWgpuInstance],
    instance_ranges: &[CoinWgpuInstanceRange],
    timestamp_query: Option<&wgpu::QuerySet>,
    color_view: &wgpu::TextureView,
    resolve_view: Option<&wgpu::TextureView>,
    color_format: wgpu::TextureFormat,
    depth_view: &wgpu::TextureView,
    depth_texture: &wgpu::Texture,
    sample_count: u32,
    peel_passes: u32,
    requested_sampling_policy: u32,
    rasterization_policy: u32,
    clear_attachments: bool,
) -> Result<(wgpu::CommandBuffer, Option<InstancedGpuBuffers>), (CoinWgpuStatus, String)> {
    encode_frame_impl(
        ctx,
        frame_revision,
        clear_color,
        target_width,
        target_height,
        vertices_slice,
        indices_slice,
        draws_slice,
        draw_order,
        materials_slice,
        states_slice,
        portable_modules,
        textures_slice,
        samplers_slice,
        shadow_frame,
        camera_geometry,
        instances_slice,
        instance_ranges,
        timestamp_query,
        color_view,
        resolve_view,
        color_format,
        depth_view,
        depth_texture,
        sample_count,
        peel_passes,
        requested_sampling_policy,
        rasterization_policy,
        clear_attachments,
        None,
        None,
    )
}

fn encode_frame_impl(
    ctx: &DeviceState,
    frame_revision: u64,
    clear_color: [f32; 4],
    target_width: u32,
    target_height: u32,
    vertices_slice: &[CoinWgpuVertex],
    indices_slice: &[u32],
    draws_slice: &[CoinWgpuDraw],
    draw_order: &[composition::CompositionItem],
    materials_slice: &[CoinWgpuMaterial],
    states_slice: &[CoinWgpuRenderState],
    portable_modules: &[portable::Module],
    textures_slice: &[CoinWgpuTexture],
    samplers_slice: &[CoinWgpuSampler],
    shadow_frame: Option<&ShadowFrame>,
    camera_geometry: Option<&Arc<ValidatedGeometry>>,
    instances_slice: &[CoinWgpuInstance],
    instance_ranges: &[CoinWgpuInstanceRange],
    timestamp_query: Option<&wgpu::QuerySet>,
    color_view: &wgpu::TextureView,
    resolve_view: Option<&wgpu::TextureView>,
    color_format: wgpu::TextureFormat,
    depth_view: &wgpu::TextureView,
    depth_texture: &wgpu::Texture,
    sample_count: u32,
    peel_passes: u32,
    requested_sampling_policy: u32,
    rasterization_policy: u32,
    clear_attachments: bool,
    bump_binding: Option<(&wgpu::TextureView,&wgpu::Sampler,&[GpuMaterial])>,
    shared_bump_shadow: Option<&(Vec<shadow::ShadowMap>, shadow::ShadowArray)>,
) -> Result<(wgpu::CommandBuffer, Option<InstancedGpuBuffers>), (CoinWgpuStatus, String)> {
    use wgpu::util::DeviceExt;
    admit_spatial_resources(ctx,textures_slice)?;
    validate_cube_resources(states_slice,textures_slice,samplers_slice)?;
    validate_cube_draws(states_slice,textures_slice,vertices_slice,indices_slice,draws_slice)?;
    raster_image::admit(ctx,states_slice,textures_slice,samplers_slice.len())?;
    if !states_slice.iter().all(valid_alpha_test) {
        return Err((CoinWgpuStatus::InvalidArgument, "Invalid alpha comparison function or reference".into()));
    }
    if states_slice.iter().any(|st| st.texture_projection > 1) {
        return Err((CoinWgpuStatus::InvalidArgument, "Invalid texture projection policy".into()));
    }
    if rasterization_policy > 1 {
        return Err((CoinWgpuStatus::InvalidArgument, "Invalid rasterization policy".into()));
    }
    if rasterization_policy == 1 && draws_slice.iter().any(|draw| draw.topology != 0) {
        return Err((CoinWgpuStatus::Unsupported, "Canonical coverage requires triangle surfaces".into()));
    }
    if sample_count > 1 && (rasterization_policy == 1 || states_slice.iter().any(|st| st.polygon_offset_enabled != 0 || st.depth_range[0] > st.depth_range[1])) &&
        !ctx.adapter.get_downlevel_capabilities().flags.contains(wgpu::DownlevelFlags::MULTISAMPLED_SHADING) {
        return Err((CoinWgpuStatus::Unsupported, "Custom MSAA depth requires sample-frequency shading".into()));
    }
    let instanced = !instances_slice.is_empty();
    let requested_policy = SamplingPolicy::from_raw(requested_sampling_policy)?;
    // Non-textured/instanced frames retain the original native prefix and pipelines.
    let policy = if !portable_modules.is_empty() { requested_policy }
        else if instanced || !samplers_slice.iter().any(|s| s.filter == 2) {
        SamplingPolicy::Native
    } else { requested_policy };
    let shadow_multitexture = validate_shadow_multitexture_profile(ctx, textures_slice, states_slice, draws_slice,
        draw_order, shadow_frame, sample_count, rasterization_policy)? || bump_binding.is_some() ||
        (!portable_modules.is_empty() && shadow_frame.is_some());
    let bump_profile = bump_binding.map(|_| ctx.bump_receiver_profile(policy));
    let profile = if let Some(p)=bump_profile.as_ref(){p.as_ref()} else if shadow_multitexture { ctx.shadow_multitexture_surface_profile(policy) } else { ctx.surface_profile(policy) };

    if instanced && (ctx.device.limits().max_storage_buffers_per_shader_stage < 2
        || instancing::gpu_bytes(instances_slice.len())
            > u64::from(ctx.device.limits().max_storage_buffer_binding_size)) {
        return Err((CoinWgpuStatus::Unsupported,
            "Instancing exceeds enabled vertex storage buffer limits".into()));
    }
    if draw_order.iter().any(|item| item.peel) && !peeling_supported(ctx, sample_count) {
        return Err((
            CoinWgpuStatus::Unsupported,
            "Peeling lacks enabled device formats/attachments; no fallback was applied".into(),
        ));
    }
    if draw_order.iter().any(|item| item.weighted) && !weighted_supported(ctx, sample_count) {
        return Err((CoinWgpuStatus::Unsupported,"Weighted OIT lacks enabled formats/attachments; no fallback was applied".into()));
    }
    // Composition was preflighted before surface acquisition or target allocation.
    if let Some(shadow) = shadow_frame {
        let features = ctx.adapter.get_texture_format_features(wgpu::TextureFormat::Rgba32Float);
        if !features.allowed_usages.contains(
            wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING)
            || shadow.map_size > ctx.device.limits().max_texture_dimension_2d {
            return Err((CoinWgpuStatus::Unsupported,
                "Rgba32Float shadow maps are unavailable on this device".into()));
        }
        if shadow.receivers.len() != states_slice.len()
            || draws_slice.iter().any(|draw| draw.render_state_slot as usize >= shadow.receivers.len()) {
            return Err((CoinWgpuStatus::InvalidArgument,
                "Shadow receivers do not match render states or composition".into()));
        }
        if let Some(second) = &shadow.second {
            if second.map_size > ctx.device.limits().max_texture_dimension_2d ||
                second.receivers.len() != states_slice.len() {
                return Err((CoinWgpuStatus::InvalidArgument,
                    "Second shadow pass does not match render states or device limits".into()));
            }
        }
        if let Some(pass) = &shadow.third {
            if pass.map_size > ctx.device.limits().max_texture_dimension_2d ||
                pass.receivers.len() != states_slice.len() {
                return Err((CoinWgpuStatus::InvalidArgument,
                    "third shadow pass does not match render states or device limits".into()));
            }
        }
        if let Some(pass) = &shadow.fourth {
            if pass.map_size > ctx.device.limits().max_texture_dimension_2d ||
                pass.receivers.len() != states_slice.len() {
                return Err((CoinWgpuStatus::InvalidArgument,
                    "fourth shadow pass does not match render states or device limits".into()));
            }
        }
        for (slot, pass) in shadow.extra.iter().enumerate() {
            if pass.map_size > ctx.device.limits().max_texture_dimension_2d ||
                pass.receivers.len() != states_slice.len() {
                return Err((CoinWgpuStatus::InvalidArgument,
                    format!("extra shadow pass {slot} does not match render states or device limits")));
            }
            for (state_slot, receiver) in pass.receivers.iter().enumerate() {
                if receiver.receives & !15 != 0 ||
                    (receiver.receives & 1 != 0 && (receiver.lighting_index < 0 ||
                     receiver.lighting_index as u32 >= states_slice[state_slot].light_count)) ||
                    !receiver.max_shadow_distance.is_finite() ||
                    !receiver.distance_falloff_coefficient.is_finite() ||
                    !receiver.model_view.iter().chain(receiver.model_view_projection.iter())
                        .all(|v| v.is_finite()) {
                    return Err((CoinWgpuStatus::InvalidArgument,
                        format!("Invalid extra shadow receiver {slot}:{state_slot}")));
                }
            }
        }
        for (slot, receiver) in shadow.receivers.iter().enumerate() {
            if receiver.receives & !15 != 0
                || !receiver.max_shadow_distance.is_finite()
                || !receiver.distance_falloff_coefficient.is_finite()
                || (receiver.max_shadow_distance > 0.0 &&
                    receiver.distance_falloff_coefficient <= 0.0)
                || !receiver.model_view.iter().chain(receiver.model_view_projection.iter())
                    .all(|v| v.is_finite())
                || (receiver.receives & 1 != 0 && (receiver.lighting_index < 0
                    || receiver.lighting_index as u32 >= states_slice[slot].light_count)) {
                return Err((CoinWgpuStatus::InvalidArgument,
                    format!("Invalid shadow receiver {slot}")));
            }
        }
        if let Some(second) = &shadow.second {
            for (slot, receiver) in second.receivers.iter().enumerate() {
                if receiver.receives & !15 != 0 ||
                    (receiver.receives & 1 != 0 && (receiver.lighting_index < 0 ||
                     receiver.lighting_index as u32 >= states_slice[slot].light_count)) ||
                    !receiver.model_view.iter().chain(receiver.model_view_projection.iter())
                        .all(|v| v.is_finite()) {
                    return Err((CoinWgpuStatus::InvalidArgument,
                        format!("Invalid second shadow receiver {slot}")));
                }
            }
        }
        if let Some(pass) = &shadow.third {
            for (slot, receiver) in pass.receivers.iter().enumerate() {
                if receiver.receives & !15 != 0 ||
                    (receiver.receives & 1 != 0 && (receiver.lighting_index < 0 ||
                     receiver.lighting_index as u32 >= states_slice[slot].light_count)) ||
                    !receiver.max_shadow_distance.is_finite() ||
                    !receiver.distance_falloff_coefficient.is_finite() ||
                    !receiver.model_view.iter().chain(receiver.model_view_projection.iter())
                        .all(|v| v.is_finite()) {
                    return Err((CoinWgpuStatus::InvalidArgument,
                        format!("Invalid third shadow receiver {slot}")));
                }
            }
        }
        if let Some(pass) = &shadow.fourth {
            for (slot, receiver) in pass.receivers.iter().enumerate() {
                if receiver.receives & !15 != 0 ||
                    (receiver.receives & 1 != 0 && (receiver.lighting_index < 0 ||
                     receiver.lighting_index as u32 >= states_slice[slot].light_count)) ||
                    !receiver.max_shadow_distance.is_finite() ||
                    !receiver.distance_falloff_coefficient.is_finite() ||
                    !receiver.model_view.iter().chain(receiver.model_view_projection.iter())
                        .all(|v| v.is_finite()) {
                    return Err((CoinWgpuStatus::InvalidArgument,
                        format!("Invalid fourth shadow receiver {slot}")));
                }
            }
        }
    }
    // Preflight the entire lighting payload before cache mutation or command encoding.
    for (state_index, state) in states_slice.iter().enumerate() {
        resolved_viewport(state, target_width, target_height)?;
        for unit in 0..8 {
            let (enabled,image,sampler)=if unit==0 {(state.has_texture,state.texture_slot,state.sampler_slot)}
                else {let t=&state.extra_textures[unit-1];(t.enabled,t.texture_slot,t.sampler_slot)};
            if enabled==0 {continue;}
            let image=textures_slice.get(image as usize).ok_or_else(||(CoinWgpuStatus::InvalidArgument,"P07 image slot out of bounds".into()))?;
            let sampler=samplers_slice.get(sampler as usize).ok_or_else(||(CoinWgpuStatus::InvalidArgument,"P07 sampler slot out of bounds".into()))?;
            if sampler.filter>=2 && !(if image.format==1 {texture_metadata(image)&2!=0}
                else if image.format==11 {texture_metadata(image)&(1<<24)!=0}
                else if image.format==12 {texture_metadata(image)&128!=0}
                else {texture_profile::format(image.format)?.1}) {
                return Err((CoinWgpuStatus::Unsupported,"P07 mip sampler requires complete stored-image chain".into()));
            }
        }
        if state.light_count > 8 {
            return Err((CoinWgpuStatus::Unsupported,
                format!("State {} has more than eight active lights", state_index)));
        }
        if !state.ambient_light.iter().all(|v| v.is_finite())
            || !state.normal_matrix.iter().all(|v| v.is_finite()) {
            return Err((CoinWgpuStatus::InvalidArgument,
                format!("State {} has non-finite ambient or normal matrix", state_index)));
        }
        if state.fog_mode > 3 || !state.fog_color.iter().all(|v| v.is_finite())
            || !state.fog_start.is_finite() || !state.fog_end.is_finite()
            || (state.fog_mode != 0 && state.fog_end <= 0.0)
            || (state.fog_mode == 1 && state.fog_end <= state.fog_start) {
            return Err((CoinWgpuStatus::InvalidArgument,
                format!("State {} has invalid fog parameters", state_index)));
        }
        for (light_index, light) in state.lights[..state.light_count as usize].iter().enumerate() {
            let finite = light.position_type.iter().chain(light.direction_cutoff.iter())
                .chain(light.color_intensity.iter()).chain(light.attenuation_exponent.iter())
                .chain(light.ambient_color.iter()).all(|v| v.is_finite());
            let kind = light.position_type[3];
            let attenuation = light.attenuation_exponent;
            if !finite || (kind != 0.0 && kind != 1.0 && kind != 2.0)
                || light.color_intensity[3] < 0.0 || light.ambient_color[3] != 0.0
                || attenuation[3] < 0.0
                || attenuation[..3].iter().any(|v| *v < 0.0)
                || (kind != 0.0 && attenuation[..3].iter().all(|v| *v == 0.0)) {
                return Err((CoinWgpuStatus::InvalidArgument,
                    format!("State {} light {} has invalid type or parameters", state_index, light_index)));
            }
        }
    }


    for image in textures_slice {
        if gpu_texture_token(image) {continue;}
        let format=texture_gpu_format(image)?;
        if format.is_compressed() && !ctx.device.features().contains(wgpu::Features::TEXTURE_COMPRESSION_BC) {
            return Err((CoinWgpuStatus::Unsupported,"BC compression unavailable on selected adapter".into()));
        }
        let features=ctx.adapter.get_texture_format_features(format);
        if !features.allowed_usages.contains(wgpu::TextureUsages::TEXTURE_BINDING|wgpu::TextureUsages::COPY_DST) {
            return Err((CoinWgpuStatus::Unsupported,"Texture format unavailable on selected adapter".into()));
        }
    }
    for sampler in samplers_slice {
        let max=texture_profile::anisotropy(sampler.reserved & 15,sampler.filter)?;
        if max>1 && !ctx.adapter.get_downlevel_capabilities().flags.contains(wgpu::DownlevelFlags::ANISOTROPIC_FILTERING) {
            return Err((CoinWgpuStatus::Unsupported,"Anisotropic filtering unavailable".into()));
        }
    }
    ctx.camera_bindings_reused.store(0, Ordering::Relaxed);
    ctx.camera_bindings_created.store(0, Ordering::Relaxed);
    // 1. Process pending GPU completion events and lock geometry cache
    let _ = ctx.device.poll(wgpu::Maintain::Poll);
    let mut cache = ctx.cache.lock().unwrap();

    // Reset per-frame telemetry counters
    cache.frame_uploaded_bytes = 0;
    cache.frame_uploads = 0;
    cache.frame_hits = 0;

    // Drain safely retired buffers whose work on GPU has completed
    let completed_serial = ctx.completed_serial.load(Ordering::SeqCst);
    cache.deferred_release.retain(|retired| {
        retired.retired_at_serial > completed_serial
    });

    let mut tex_cache = ctx.texture_cache.lock().unwrap();
    let mut samp_cache = ctx.sampler_cache.lock().unwrap();
    let mut rtt_cache = ctx.rtt_textures.lock().unwrap();
    rtt_cache.retired.retain(|retired| retired.retired_at_serial > completed_serial);

    // Drain safely retired textures whose work on GPU has completed
    tex_cache.retired.retain(|retired| {
        retired.retired_at_serial > completed_serial
    });

    let current_submission_serial = ctx.last_submitted_serial.load(Ordering::SeqCst) + 1;

    // Upload and cache frame textures
    for (t_idx, t) in textures_slice.iter().enumerate() {
        if t.width == 0 || t.height == 0 || t.width > 8192 || t.height > 8192 {
            return Err((
                CoinWgpuStatus::InvalidArgument,
                format!("Texture {} dimensions invalid: {}x{}", t_idx, t.width, t.height),
            ));
        }
        if gpu_texture_token(t) {
            let entry = rtt_cache.active.get(&t.content_digest).ok_or_else(|| (
                CoinWgpuStatus::InvalidArgument,
                format!("Texture {} has stale or unknown RTT token", t_idx),
            ))?;
            let metadata_mismatch=if t.format==12 {
                let (n,_,mips)=cube_atlas_metadata(t)?.unwrap();
                entry.cube_face_size!=n || entry.cube_mipmapped!=mips || entry.format!=0 || entry.mip_count!=1
            } else {
                entry.cube_face_size!=0 || texture_metadata(t)&!31!=0 || (texture_metadata(t)&1!=0 && !entry.opaque) ||
                (texture_metadata(t)>>2)!=entry.format || (texture_metadata(t)&2!=0 && entry.mip_count<=1 && (t.width>1 || t.height>1))
            };
            if entry.width != t.width || entry.height != t.height || metadata_mismatch
                || !t.pixels.is_null() || t.pixel_bytes_len != 0 {
                return Err((CoinWgpuStatus::InvalidArgument,
                    format!("Texture {} RTT token metadata mismatch", t_idx)));
            }
            continue;
        }
        spatial_atlas_metadata(t)?;
        cube_atlas_metadata(t)?;
        let levels = texture_levels(t.width,t.height,t.format)?;
        let &(last_width,last_height,last_offset)=levels.last().unwrap();
        let gpu_format=texture_gpu_format(t)?;
        let expected_bytes=last_offset+texture_profile::level_bytes(last_width,last_height,gpu_format);
        if t.pixel_bytes_len != expected_bytes || t.pixels.is_null() {
            return Err((
                CoinWgpuStatus::InvalidArgument,
                format!("Texture {} pixel buffer mismatch: expected {} bytes, got {}", t_idx, expected_bytes, t.pixel_bytes_len),
            ));
        }

        let pixel_bytes = unsafe { std::slice::from_raw_parts(t.pixels, t.pixel_bytes_len as usize) };
        if gpu_format==wgpu::TextureFormat::Rgba16Float && pixel_bytes.chunks_exact(2).any(|p| u16::from_le_bytes([p[0],p[1]])&0x7c00==0x7c00) {
            return Err((CoinWgpuStatus::InvalidArgument,"Non-finite half texture payload".into()));
        }
        let key = TextureKey::from_texture(t);

        if let Some(entry) = tex_cache.entries.get_mut(&key) {
            if entry.pixels_copy == pixel_bytes {
                entry.last_submitted_serial = current_submission_serial;
                tex_cache.hits += 1;
                continue;
            }
        }

        let wgpu_tex = ctx.device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Coin Cached 2D Texture"),
            size: wgpu::Extent3d {
                width: t.width,
                height: t.height,
                depth_or_array_layers: 1,
            },
            mip_level_count: levels.len() as u32,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: gpu_format,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });

        for (level,&(level_width,level_height,offset)) in levels.iter().enumerate() {
        ctx.queue.write_texture(
            wgpu::ImageCopyTexture {
                texture: &wgpu_tex,
                mip_level: level as u32,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            &pixel_bytes[offset as usize..(offset+texture_profile::level_bytes(level_width,level_height,gpu_format)) as usize],
            wgpu::ImageDataLayout {
                offset: 0,
                bytes_per_row: Some(level_width.div_ceil(gpu_format.block_dimensions().0)*gpu_format.block_copy_size(None).unwrap()),
                rows_per_image: Some(level_height.div_ceil(gpu_format.block_dimensions().1)),
            },
            wgpu::Extent3d {
                width: level_width.div_ceil(gpu_format.block_dimensions().0)*gpu_format.block_dimensions().0,
                height: level_height.div_ceil(gpu_format.block_dimensions().1)*gpu_format.block_dimensions().1,
                depth_or_array_layers: 1,
            },
        );

        }
        let view = wgpu_tex.create_view(&wgpu::TextureViewDescriptor::default());
        tex_cache.uploads += 1;
        tex_cache.uploaded_bytes += pixel_bytes.len() as u64;
        let old = tex_cache.entries.insert(
            key,
            CachedTextureEntry {
                texture: wgpu_tex,
                view,
                pixels_copy: pixel_bytes.to_vec(),
                last_submitted_serial: current_submission_serial,
                size_bytes: pixel_bytes.len(),
            },
        );
        if let Some(old_entry) = old {
            tex_cache.retired.push(RetiredTexture {
                texture: old_entry.texture,
                retired_at_serial: current_submission_serial,
            });
        }
    }

    // Retain recent textures for reuse, but retire images no longer referenced by
    // the scene. The serial ties release to GPU completion, not CPU traversal.
    const MAX_STALE_TEXTURE_SERIALS: u64 = 8;
    let stale_textures: Vec<TextureKey> = tex_cache
        .entries
        .iter()
        .filter(|(_, entry)| {
            current_submission_serial.saturating_sub(entry.last_submitted_serial)
                > MAX_STALE_TEXTURE_SERIALS
        })
        .map(|(key, _)| key.clone())
        .collect();
    for key in stale_textures {
        if let Some(entry) = tex_cache.entries.remove(&key) {
            tex_cache.evictions += 1;
            tex_cache.retired.push(RetiredTexture {
                texture: entry.texture,
                retired_at_serial: entry.last_submitted_serial,
            });
        }
    }

    // Upload and cache frame samplers
    for s in samplers_slice {
        if s.filter>3 || s.wrap_s>1 || s.wrap_t>1 || s.reserved & !31 != 0 ||
            texture_profile::anisotropy(s.reserved & 15,s.filter).is_err() {
            return Err((CoinWgpuStatus::InvalidArgument,"Invalid P07 sampler".into()));
        }
        let key = SamplerKey {
            wrap_s: s.wrap_s,
            wrap_t: s.wrap_t,
            filter: s.filter,
            max_anisotropy_minus_one: s.reserved,
        };
        if !samp_cache.entries.contains_key(&key) {
            let addr_u = match s.wrap_s {
                0 => wgpu::AddressMode::Repeat,
                1 => wgpu::AddressMode::ClampToEdge,
                _ => wgpu::AddressMode::ClampToEdge,
            };
            let addr_v = match s.wrap_t {
                0 => wgpu::AddressMode::Repeat,
                1 => wgpu::AddressMode::ClampToEdge,
                _ => wgpu::AddressMode::ClampToEdge,
            };
            let filter_mode = match s.filter {
                0 => wgpu::FilterMode::Nearest,
                1 => wgpu::FilterMode::Linear,
                _ => wgpu::FilterMode::Linear,
            };
            let samp = ctx.device.create_sampler(&wgpu::SamplerDescriptor {
                label: Some("Coin Cached Sampler"),
                anisotropy_clamp: texture_profile::anisotropy(s.reserved & 15,s.filter)?,
                lod_max_clamp: if s.filter>=2 {32.0} else {0.0},
                address_mode_u: addr_u,
                address_mode_v: addr_v,
                address_mode_w: wgpu::AddressMode::Repeat,
                mag_filter: if s.filter>=2 {wgpu::FilterMode::Linear} else {filter_mode},
                min_filter: if s.filter==2 {wgpu::FilterMode::Nearest} else {filter_mode},
                mipmap_filter: if s.filter>=2 {wgpu::FilterMode::Linear} else {wgpu::FilterMode::Nearest},
                ..Default::default()
            });
            samp_cache.entries.insert(key, samp);
        }
    }

    // Evict stale entries unreferenced beyond threshold
    cache.evict_stale(ctx.last_submitted_serial.load(Ordering::SeqCst));

    // A clear-only pass has no draw/material. A draw still requires material slots.
    if materials_slice.is_empty() && !draws_slice.is_empty() {
        return Err((
            CoinWgpuStatus::InvalidArgument,
            "Frame with draws requires material_count > 0".to_string(),
        ));
    }

    let mat_buffer_size = match materials_slice.len().checked_mul(std::mem::size_of::<GpuMaterial>()) {
        Some(sz) => sz,
        None => {
            return Err((CoinWgpuStatus::InvalidArgument, "Overflow in material buffer size".to_string()));
        }
    };

    if mat_buffer_size as u64 > ctx.device.limits().max_storage_buffer_binding_size as u64 {
        return Err((CoinWgpuStatus::Unsupported, "Material buffer exceeds max_storage_buffer_binding_size".to_string()));
    }

    // An owned scene already validated this immutable vertex payload. Camera
    // frames update only states; scanning a million vertices would defeat reuse.
    let unchecked_vertices = if camera_geometry.is_none() { vertices_slice } else { &[] };
    for (v_idx, v) in unchecked_vertices.iter().enumerate() {
        if !v.texcoord.iter().chain(v.extra_texcoords.iter().flatten()).all(|x| x.is_finite())
            || !v.screen_space_w.is_finite() || v.screen_space_w < 0.0
            || !v.fog_eye_depth_plus_one.is_finite() || v.fog_eye_depth_plus_one < 0.0 {
            return Err((CoinWgpuStatus::InvalidArgument,
                format!("Vertex {} has invalid homogeneous texture/stroke attributes", v_idx)));
        }
        if (v.material_slot as usize) >= materials_slice.len() {
            return Err((CoinWgpuStatus::InvalidArgument, format!("Vertex {} references invalid material_slot {} >= material_count {}", v_idx, v.material_slot, materials_slice.len())));
        }
    }

    let trace_materials = std::env::var_os("COIN_RENDER_TRACE_PHASES")
        .or_else(|| std::env::var_os("COIN_WGPU_TRACE_PHASES")).is_some();
    let reuse_materials = material_resource_reuse_enabled();
    // The ablation restores eager packing at its original position, before
    // resource-cache locks or equality checks, as well as coupled GPU misses.
    let profile_eager_pack = trace_materials.then(std::time::Instant::now);
    let literal_gpu_materials = if reuse_materials { None }
        else { Some(pack_gpu_materials(materials_slice)) };
    let eager_pack_ms = profile_eager_pack.map(|start| start.elapsed().as_secs_f64() * 1000.0);
    // Keep the required GPU length independent of whether packing is needed.
    // A clear-only frame still binds the same one-element zero sentinel.
    let material_byte_len = mat_buffer_size.max(std::mem::size_of::<GpuMaterial>());
    let geometry_bytes = (std::mem::size_of_val(vertices_slice) as u64)
        .saturating_add(std::mem::size_of_val(indices_slice) as u64);
    let max_buffer_size = ctx.device.limits().max_buffer_size;
    let split_uncached = draws_slice.iter().any(|draw| draw.stable_node_id == 0 && draw.index_count > 0)
        && ((std::mem::size_of_val(vertices_slice) as u64) > max_buffer_size
            || (std::mem::size_of_val(indices_slice) as u64) > max_buffer_size);
    if split_uncached && trace_materials {
        eprintln!("COIN_RENDER_PHASE rust_uncached_split vertex_bytes={} index_bytes={} max_buffer_size={} draws={}",
            std::mem::size_of_val(vertices_slice), std::mem::size_of_val(indices_slice),
            max_buffer_size, draws_slice.iter().filter(|draw| draw.stable_node_id == 0 && draw.index_count > 0).count());
    }
    let retain_frame = !split_uncached && !instanced && frame_revision != 0 && !vertices_slice.is_empty()
        && !indices_slice.is_empty() && !draws_slice.is_empty()
        && draws_slice.iter().all(|draw| draw.stable_node_id == 0)
        && geometry_bytes.saturating_add(material_byte_len as u64) <= 256 * 1024 * 1024;
    let mut frame_buffers = ctx.frame_buffers.lock().unwrap();
    if !retain_frame || !frame_buffers.as_ref().is_some_and(|entry|
        (entry.revision == frame_revision || camera_geometry.is_some_and(|geometry|
            entry.geometry.as_ref().is_some_and(|cached| Arc::ptr_eq(cached, geometry))))
            && entry.vertex_count == vertices_slice.len()
            && entry.index_count == indices_slice.len() && entry.material_bytes == material_byte_len) {
        *frame_buffers = None;
    }
    let frame_buffer_hit = frame_buffers.is_some();

    // Keep the experiment switch process-local so the same binary can be
    // benchmarked against its previous behavior. It is opt-in, bounded and
    // applies only to the already-validated, untextured camera patch.
    static CAMERA_GPU_CACHE_ENABLED: OnceLock<bool> = OnceLock::new();
    let camera_binding_geometry = camera_geometry.filter(|_| shadow_frame.is_none()).filter(|_| {
        *CAMERA_GPU_CACHE_ENABLED.get_or_init(|| {
            std::env::var("COIN_WGPU_CAMERA_BINDINGS").as_deref() == Ok("1")
        })
        && !draws_slice.is_empty()
        && !materials_slice.is_empty()
        && draws_slice.len() <= 512
        && mat_buffer_size.saturating_add(draws_slice.len()
            .saturating_mul(uniform_binding_bytes(instanced, policy))) <= 4 * 1024 * 1024
    });
    let mut camera_bindings = ctx.camera_bindings.lock().unwrap();
    if !camera_binding_geometry.is_some_and(|geometry| camera_bindings.as_ref()
        .is_some_and(|entry| Arc::ptr_eq(&entry.geometry, geometry))) {
        *camera_bindings = None;
    }
    let previous_instances = ctx.instanced_buffers.lock().unwrap().clone();
    let owned_instance_hit = instanced && previous_instances.as_ref().is_some_and(|entry|
        camera_geometry.is_some_and(|geometry| Arc::ptr_eq(geometry, &entry.geometry)));
    let geometry_hit = owned_instance_hit || (instanced && previous_instances.as_ref().is_some_and(|entry|
        instancing::same_geometry(&entry.geometry, vertices_slice, indices_slice)));
    let profile_payload_compare = trace_materials.then(std::time::Instant::now);
    let (instance_hit, material_hit) = if owned_instance_hit { (true, true) }
        else if geometry_hit { previous_instances.as_ref().map(|entry|
            instancing::payload_hits(&entry.geometry, instances_slice, materials_slice, reuse_materials)).unwrap_or((false, false)) }
        else { (false, false) };
    let payload_compare_ms = profile_payload_compare.map(|start| start.elapsed().as_secs_f64() * 1000.0);
    // payload_hit retains its legacy meaning in the diagnostic event. Resource
    // decisions now use their own proof; optout restores the coupled behavior.
    let payload_hit = instance_hit && material_hit;
    let material_buffer_hit = material_hit || frame_buffers.is_some() || camera_bindings.is_some();
    let profile_material_pack = trace_materials.then(std::time::Instant::now);
    let gpu_materials = if let Some((_,_,values))=bump_binding {values.to_vec()} else if let Some(literal) = literal_gpu_materials { literal }
    else if !material_buffer_hit {
        pack_gpu_materials(materials_slice)
    } else { Vec::new() };
    let material_pack_ms = if reuse_materials {
        profile_material_pack.map(|start| start.elapsed().as_secs_f64() * 1000.0)
    } else { eager_pack_ms };
    // WebGPU does not permit a zero-sized storage binding. This sentinel is
    // unreachable because there are no draws in a material-free frame. A hit
    // never consumes material_bytes, so its empty scratch Vec is harmless.
    let fallback_material = [GpuMaterial::zeroed()];
    let material_bytes: &[u8] = if gpu_materials.is_empty() {
        bytemuck::cast_slice(&fallback_material)
    } else { bytemuck::cast_slice(&gpu_materials) };
    let profile_material_resources = trace_materials.then(std::time::Instant::now);
    let instances_buffer = if !instanced { None } else if instance_hit {
        Some(previous_instances.as_ref().unwrap().instances_buffer.clone())
    } else {
        let gpu_instances = instancing::pack_gpu(instances_slice);
        Some(multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
            label: Some("Coin compact affine instance storage"), contents: bytemuck::cast_slice(&gpu_instances),
            usage: wgpu::BufferUsages::STORAGE,
        }))
    };
    let materials_buffer = if material_hit {
        previous_instances.as_ref().unwrap().materials_buffer.clone()
    } else if let Some(entry) = frame_buffers.as_ref() {
        entry.materials_buffer.clone()
    } else if let Some(entry) = camera_bindings.as_ref() {
        ctx.camera_bindings_reused.fetch_add(1, Ordering::Relaxed);
        entry.materials_buffer.clone()
    } else {
        debug_assert_eq!(material_bytes.len(), material_byte_len);
        let buffer = multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
            label: Some("Frame Materials Storage Buffer"),
            contents: material_bytes,
            usage: wgpu::BufferUsages::STORAGE,
        });
        if let Some(geometry) = camera_binding_geometry {
            *camera_bindings = Some(CameraGpuBindings {
                geometry: geometry.clone(),
                materials_buffer: buffer.clone(),
            });
            ctx.camera_bindings_created.fetch_add(1, Ordering::Relaxed);
        }
        buffer
    };
    if let Some(start) = profile_material_resources {
        eprintln!("COIN_RENDER_PHASE rust_material_resources count={} source_bytes={} gpu_bytes={} packed_count={} packed_bytes={} payload_compare_ms={} pack_ms={} resource_ms={} instance_hit={} material_hit={} material_buffer_hit={} instance_allocations={} material_allocations={} instance_uploaded_bytes={} material_uploaded_bytes={} reuse_enabled={} attempt=1",
            materials_slice.len(), std::mem::size_of_val(materials_slice), material_byte_len,
            gpu_materials.len(), std::mem::size_of_val(gpu_materials.as_slice()),
            payload_compare_ms.unwrap(), material_pack_ms.unwrap(), start.elapsed().as_secs_f64() * 1000.0,
            instance_hit as u32, material_hit as u32, material_buffer_hit as u32,
            (instanced && !instance_hit) as u32, (!material_buffer_hit) as u32,
            if instanced && !instance_hit { instancing::gpu_bytes(instances_slice.len()) } else { 0 },
            if material_buffer_hit { 0 } else { material_byte_len }, reuse_materials as u32);
    }

    // Collect keys active in the current frame to strictly protect them from LRU eviction
    let mut frame_active_keys = std::collections::HashSet::new();
    for draw in draws_slice {
        if draw.index_count > 0 && draw.stable_node_id != 0 {
            frame_active_keys.insert((draw.stable_node_id, draw.draw_ordinal));
        }
    }

    // 2. Prepare uncached fallback buffers if there are any legacy/uncached draws
    let uncached_v_buffer = if split_uncached {
        None
    } else if geometry_hit {
        Some(previous_instances.as_ref().unwrap().vertex_buffer.clone())
    } else if let Some(entry) = frame_buffers.as_ref() {
        Some(entry.vertex_buffer.clone())
    } else if draws_slice.iter().any(|d| d.stable_node_id == 0) && !vertices_slice.is_empty() {
        Some(
            multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
                    label: Some("Coin Uncached Vertex Buffer"),
                    contents: bytemuck::cast_slice(vertices_slice),
                    usage: wgpu::BufferUsages::VERTEX,
                }),
        )
    } else {
        None
    };

    let uncached_i_buffer = if split_uncached {
        None
    } else if geometry_hit {
        Some(previous_instances.as_ref().unwrap().index_buffer.clone())
    } else if let Some(entry) = frame_buffers.as_ref() {
        Some(entry.index_buffer.clone())
    } else if draws_slice.iter().any(|d| d.stable_node_id == 0 && d.index_count > 0)
        && !indices_slice.is_empty()
    {
        Some(
            multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
                    label: Some("Coin Uncached Index Buffer"),
                    contents: bytemuck::cast_slice(indices_slice),
                    usage: wgpu::BufferUsages::INDEX,
                }),
        )
    } else {
        None
    };

    // A frame can exceed WebGPU's per-buffer limit even though each draw is
    // small. Keep the shared-buffer path for smaller frames and rebase only
    // the oversized frame's uncached draws into bounded buffers.
    let mut split_uncached_buffers: Vec<Option<(wgpu::Buffer, wgpu::Buffer)>> =
        if split_uncached { (0..draws_slice.len()).map(|_| None).collect() }
        else { Vec::new() };
    if split_uncached {
        for (draw_index, draw) in draws_slice.iter().enumerate() {
            if draw.stable_node_id != 0 || draw.index_count == 0 { continue; }
            let first_vertex = draw.first_vertex as usize;
            let vertex_end = first_vertex + draw.vertex_count as usize;
            let first_index = draw.first_index as usize;
            let index_end = first_index + draw.index_count as usize;
            let draw_vertices = &vertices_slice[first_vertex..vertex_end];
            let vertex_bytes = std::mem::size_of_val(draw_vertices) as u64;
            let index_bytes = (draw.index_count as u64) * std::mem::size_of::<u32>() as u64;
            if vertex_bytes > max_buffer_size || index_bytes > max_buffer_size {
                return Err((CoinWgpuStatus::Unsupported,
                    format!("Draw {} exceeds device max_buffer_size of {} bytes (vertices {}, indices {})",
                        draw_index, max_buffer_size, vertex_bytes, index_bytes)));
            }
            let local_indices: Vec<u32> = indices_slice[first_index..index_end].iter()
                .map(|&index| index.checked_sub(draw.first_vertex)
                    .filter(|&local| local < draw.vertex_count)
                    .ok_or_else(|| (CoinWgpuStatus::Unsupported,
                        format!("Draw {} references vertices outside its range; cannot split oversized frame", draw_index))))
                .collect::<Result<_, _>>()?;
            let vertex_buffer = multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
                label: Some("Coin Split Uncached Vertex Buffer"),
                contents: bytemuck::cast_slice(draw_vertices),
                usage: wgpu::BufferUsages::VERTEX,
            });
            let index_buffer = multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
                label: Some("Coin Split Uncached Index Buffer"),
                contents: bytemuck::cast_slice(&local_indices),
                usage: wgpu::BufferUsages::INDEX,
            });
            split_uncached_buffers[draw_index] = Some((vertex_buffer, index_buffer));
        }
    }

    if frame_buffer_hit {
        cache.cumulative_hits += 1;
        cache.frame_hits += 1;
    } else if retain_frame {
        *frame_buffers = Some(FrameGpuBuffers {
            revision: frame_revision, geometry: camera_geometry.cloned(), vertex_count: vertices_slice.len(),
            index_count: indices_slice.len(), material_bytes: material_byte_len,
            vertex_buffer: uncached_v_buffer.as_ref().unwrap().clone(),
            index_buffer: uncached_i_buffer.as_ref().unwrap().clone(),
            materials_buffer: materials_buffer.clone(), geometry_bytes,
        });
        cache.cumulative_uploads += 1;
        cache.frame_uploads += 1;
        cache.cumulative_uploaded_bytes += geometry_bytes;
        cache.frame_uploaded_bytes += geometry_bytes;
    }

    let pending_instances = if instanced {
        let geometry = camera_geometry.ok_or_else(|| (CoinWgpuStatus::OutOfMemory,
            "Cannot retain bounded validated instance payload".to_string()))?;
        let instance_bytes = instancing::gpu_bytes(instances_slice.len());
        let uploaded_instances = if instance_hit { 0 } else { instance_bytes };
        let uploaded_materials = if material_buffer_hit { 0 } else { material_byte_len as u64 };
        let uploaded = (if geometry_hit { 0 } else { geometry_bytes }) + uploaded_instances + uploaded_materials;
        if geometry_hit { cache.frame_hits += 1; cache.cumulative_hits += 1; }
        if uploaded != 0 {
            cache.frame_uploads += 1; cache.cumulative_uploads += 1;
            cache.frame_uploaded_bytes += uploaded; cache.cumulative_uploaded_bytes += uploaded;
        }
        if std::env::var_os("COIN_RENDER_TRACE_PHASES").or_else(|| std::env::var_os("COIN_WGPU_TRACE_PHASES")).is_some() {
            eprintln!("COIN_RENDER_PHASE rust_instances count={} ranges={} geometry_bytes={} instance_bytes={} material_bytes={} geometry_hit={} payload_hit={} instance_hit={} material_hit={} instance_uploaded_bytes={} material_uploaded_bytes={} uploaded_bytes={}",
                instances_slice.len(), instance_ranges.len(), geometry_bytes, instance_bytes,
                material_byte_len, geometry_hit as u32, payload_hit as u32, instance_hit as u32,
                material_hit as u32, uploaded_instances, uploaded_materials, uploaded);
        }
        Some(InstancedGpuBuffers { geometry: geometry.clone(),
            vertex_buffer: uncached_v_buffer.as_ref().unwrap().clone(),
            index_buffer: uncached_i_buffer.as_ref().unwrap().clone(),
            materials_buffer: materials_buffer.clone(),
            instances_buffer: instances_buffer.as_ref().unwrap().clone(),
            geometry_bytes, instance_bytes, material_bytes: material_byte_len as u64,
        })
    } else { None };

    // Temporary storage for oversized draws exceeding cache budget
    let mut oversized_entries: HashMap<(u64, u32), CachedGeometryEntry> = HashMap::new();

    // 3. Update cache for cached draws (stable_node_id != 0)
    for draw in draws_slice {
        if draw.index_count == 0 || draw.stable_node_id == 0 {
            continue;
        }

        let key = (draw.stable_node_id, draw.draw_ordinal);
        let hit = if let Some((rev, _entry)) = cache.active_entries.get(&key) {
            *rev == draw.source_revision
        } else {
            false
        };

        if hit {
            cache.cumulative_hits += 1;
            cache.frame_hits += 1;
            if let Some((_rev, entry)) = cache.active_entries.get_mut(&key) {
                entry.last_submitted_serial = current_submission_serial;
            }
        } else {
            // Check for simulated allocation failure (transactional rollback test)
            let fault = FAULT_INJECTION.load(Ordering::SeqCst);
            if fault == FAULT_CACHE_ALLOC_FAIL {
                FAULT_INJECTION.store(0, Ordering::SeqCst);
                return Err((
                    CoinWgpuStatus::OutOfMemory,
                    "Simulated cache buffer allocation failure (OOM)".to_string(),
                ));
            }

            // Extract vertices for this draw
            let start_v = draw.first_vertex as usize;
            let end_v = start_v + draw.vertex_count as usize;
            if end_v > vertices_slice.len() {
                return Err((
                    CoinWgpuStatus::InvalidArgument,
                    format!(
                        "Cached draw vertex range out of bounds: {}..{} > {}",
                        start_v,
                        end_v,
                        vertices_slice.len()
                    ),
                ));
            }
            let draw_vertices = &vertices_slice[start_v..end_v];
            let v_size = (draw_vertices.len() * std::mem::size_of::<CoinWgpuVertex>()) as u64;

            // Extract and rebase indices for this draw
            let start_i = draw.first_index as usize;
            let end_i = start_i + draw.index_count as usize;
            if end_i > indices_slice.len() {
                return Err((
                    CoinWgpuStatus::InvalidArgument,
                    format!(
                        "Cached draw index range out of bounds: {}..{} > {}",
                        start_i,
                        end_i,
                        indices_slice.len()
                    ),
                ));
            }
            let raw_indices = &indices_slice[start_i..end_i];
            let local_indices: Vec<u32> = raw_indices
                .iter()
                .map(|&idx| idx.saturating_sub(draw.first_vertex))
                .collect();
            let i_size = (local_indices.len() * std::mem::size_of::<u32>()) as u64;
            let total_bytes = v_size + i_size;

            // Evict inactive entries to accommodate incoming bytes if needed
            cache.evict_for_budget(total_bytes, &frame_active_keys);

            // Transactional allocation: allocate buffers before touching existing cache entry
            let v_buf = multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
                label: Some("Coin Cached Draw Vertex Buffer"),
                contents: bytemuck::cast_slice(draw_vertices),
                usage: wgpu::BufferUsages::VERTEX,
            });
            let i_buf = multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
                label: Some("Coin Cached Draw Index Buffer"),
                contents: bytemuck::cast_slice(&local_indices),
                usage: wgpu::BufferUsages::INDEX,
            });

            // Evict previous revision safely if it existed, recording its true last_submitted_serial
            if let Some((_old_rev, old_entry)) = cache.active_entries.remove(&key) {
                cache.total_active_geometry_bytes = cache
                    .total_active_geometry_bytes
                    .saturating_sub(old_entry.size_bytes);
                cache.deferred_release.push(RetiredBuffer {
                    vertex_buffer: old_entry.vertex_buffer,
                    index_buffer: old_entry.index_buffer,
                    retired_at_serial: old_entry.last_submitted_serial,
                    size_bytes: old_entry.size_bytes,
                });
            }

            cache.cumulative_misses += 1;
            cache.cumulative_uploads += 1;
            cache.frame_uploads += 1;
            cache.cumulative_uploaded_bytes += total_bytes;
            cache.frame_uploaded_bytes += total_bytes;

            let new_entry = CachedGeometryEntry {
                vertex_buffer: v_buf,
                index_buffer: Some(i_buf),
                vertex_count: draw.vertex_count,
                index_count: draw.index_count,
                size_bytes: total_bytes,
                last_submitted_serial: current_submission_serial,
            };

            if total_bytes > cache.max_active_geometry_bytes {
                // Oversized geometry larger than entire budget: render in current frame but do not retain
                oversized_entries.insert(key, new_entry);
            } else {
                cache.total_active_geometry_bytes += total_bytes;
                cache.active_entries.insert(key, (draw.source_revision, new_entry));
            }
        }
    }

    // 4. Encode base composition followed by immediate annotation layers.
    let mut encoder = ctx
        .device
        .create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("Coin Frame Encoder"),
        });
    let raster_images = if let Some(images)=MULTIPASS_UPLOADS.with(|slot|
        slot.borrow().as_ref().and_then(|replay|replay.raster.clone())) {images}
    else {
        let images=Arc::new(raster_image::encode(ctx, &mut encoder, states_slice,
            textures_slice, samplers_slice, &tex_cache, &samp_cache, &rtt_cache)?);
        MULTIPASS_UPLOADS.with(|slot| {
            if let Some(replay)=slot.borrow_mut().as_mut() {replay.raster=Some(images.clone());}
        });
        images
    };
    let shadow_map = if let Some((maps, _)) = shared_bump_shadow { maps.first().cloned() } else if let Some(shadow) = shadow_frame {
        if FAULT_INJECTION.load(Ordering::SeqCst) == FAULT_SHADOW_MAP_ALLOC {
            FAULT_INJECTION.store(0, Ordering::SeqCst);
            return Err((CoinWgpuStatus::OutOfMemory,
                "Injected shadow-map attachment allocation failure".into()));
        }
        Some(shadow::encode_moments(&ctx.device, &mut encoder, vertices_slice,
            indices_slice, shadow.casters, states_slice, shadow.map_size, shadow.near, shadow.far, shadow.kind, rasterization_policy == 1)
            .map_err(|msg| (CoinWgpuStatus::InvalidArgument, msg))?)
    } else { None };
    let shadow_map_second = if let Some((maps,_))=shared_bump_shadow { maps.get(1).cloned() } else if let Some(second) = shadow_frame.and_then(|shadow| shadow.second.as_ref()) {
        Some(shadow::encode_moments(&ctx.device, &mut encoder, vertices_slice,
            indices_slice, second.casters, states_slice, second.map_size, second.near, second.far, second.kind, rasterization_policy == 1)
            .map_err(|msg| (CoinWgpuStatus::InvalidArgument, msg))?)
    } else { None };

    let shadow_map_third = if let Some((maps,_))=shared_bump_shadow { maps.get(2).cloned() } else if let Some(pass) = shadow_frame.and_then(|shadow| shadow.third.as_ref()) {
        Some(shadow::encode_moments(&ctx.device, &mut encoder, vertices_slice,
            indices_slice, pass.casters, states_slice, pass.map_size, pass.near, pass.far, pass.kind, rasterization_policy == 1)
            .map_err(|msg| (CoinWgpuStatus::InvalidArgument, msg))?)
    } else { None };
    let shadow_map_fourth = if let Some((maps,_))=shared_bump_shadow { maps.get(3).cloned() } else if let Some(pass) = shadow_frame.and_then(|shadow| shadow.fourth.as_ref()) {
        Some(shadow::encode_moments(&ctx.device, &mut encoder, vertices_slice,
            indices_slice, pass.casters, states_slice, pass.map_size, pass.near, pass.far, pass.kind, rasterization_policy == 1)
            .map_err(|msg| (CoinWgpuStatus::InvalidArgument, msg))?)
    } else { None };
    let mut extra_shadow_maps = Vec::new();
    if let Some((maps,_))=shared_bump_shadow {
        extra_shadow_maps.extend(maps.iter().skip(4).cloned());
    } else if let Some(shadow) = shadow_frame {
        for pass in &shadow.extra {
            extra_shadow_maps.push(shadow::encode_moments(&ctx.device, &mut encoder,
                vertices_slice, indices_slice, pass.casters, states_slice,
                pass.map_size, pass.near, pass.far, pass.kind, rasterization_policy == 1)
                .map_err(|msg| (CoinWgpuStatus::InvalidArgument, msg))?);
        }
    }
    let shadow_array = if shadow_multitexture && shared_bump_shadow.is_some() {
        Some(shared_bump_shadow.unwrap().1.clone())
    } else if shadow_multitexture {
        let maps: Vec<&shadow::ShadowMap> = [shadow_map.as_ref(), shadow_map_second.as_ref(),
            shadow_map_third.as_ref(), shadow_map_fourth.as_ref()].into_iter().flatten()
            .chain(extra_shadow_maps.iter()).collect();
        Some(shadow::encode_array(&ctx.device, &mut encoder, &maps)
            .map_err(|message| (CoinWgpuStatus::Unsupported, message))?)
    } else { None };
    let load_initial_depth = manager_backgrounds(draws_slice);
    if clear_attachments {
        let _clear = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("Coin initial attachments clear"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view: color_view,
                resolve_target: None,
                ops: wgpu::Operations {
                    load: wgpu::LoadOp::Clear(wgpu::Color {
                        r: clear_color[0] as f64,
                        g: clear_color[1] as f64,
                        b: clear_color[2] as f64,
                        a: clear_color[3] as f64,
                    }),
                    store: wgpu::StoreOp::Store,
                },
            })],
            depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                view: depth_view,
                depth_ops: Some(wgpu::Operations {
                    load: if load_initial_depth { wgpu::LoadOp::Load } else { wgpu::LoadOp::Clear(1.0) },
                    store: wgpu::StoreOp::Store,
                }),
                stencil_ops: None,
            }),
            timestamp_writes: timestamp_query.map(|query_set| wgpu::RenderPassTimestampWrites {
                query_set,
                beginning_of_pass_write_index: Some(0),
                end_of_pass_write_index: None,
            }),
            occlusion_query_set: None,
        });
    }
    let peeling = draw_order.iter().any(|item| item.peel).then(|| {
        MULTIPASS_UPLOADS.with(|slot| {
            let mut slot=slot.borrow_mut();
            let create=||Arc::new(peeling::Peeling::new_samples(&ctx.device,target_width,target_height,
                color_format,peel_passes as usize,ctx.adapter.get_info().backend==wgpu::Backend::Gl,sample_count));
            if let Some(replay)=slot.as_mut() {
                replay.peeling.entry((target_width,target_height,color_format,peel_passes as usize,sample_count))
                    .or_insert_with(create).clone()
            } else {create()}
        })
    });
    let weighted=draw_order.iter().any(|item|item.weighted).then(|| {
        MULTIPASS_UPLOADS.with(|slot| {
            let mut slot=slot.borrow_mut();
            let create=||Arc::new(weighted::Weighted::new_samples(&ctx.device,target_width,target_height,color_format,sample_count));
            if let Some(replay)=slot.as_mut() {
                replay.weighted.entry((target_width,target_height,color_format,sample_count)).or_insert_with(create).clone()
            } else {create()}
        })
    });
    let passes = composition::passes(draw_order, draws_slice);
    let uniform_records = draw_order.iter().try_fold(0usize, |count, item| {
        count.checked_add(if item.peel { peel_passes as usize } else { 1 })
    }).ok_or_else(|| (CoinWgpuStatus::InvalidArgument, "Draw uniform count overflow".into()))?;
    let uniform_size = uniform_binding_bytes(instanced, policy);
    let sampling_audit = std::env::var_os("COIN_SAMPLING_AUDIT").is_some();
    let mut uniform_arena = uniform_arena::UniformArena::new(&ctx.device, uniform_records, uniform_size);
    // Resources are stable while the texture/RTT caches are locked. A disabled
    // unit always binds the same defaults; unused eight-shadow units are omitted.
    let mut draw_bindings: HashMap<(usize, [(u32, u32); 8], usize, u32), wgpu::BindGroup> = HashMap::new();
    // A distinct immutable buffer for each occurrence prevents later runs from
    // overwriting parameters referenced by an earlier command in one submit.
    let mut module_buffers = Vec::with_capacity(portable_modules.len());
    for module in portable_modules {
        let mut parameters = [0.0f32;1024];
        parameters[..module.parameters.len()].copy_from_slice(&module.parameters);
        module_buffers.push(multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
            label: Some("Coin portable shader occurrence parameters"),
            contents: bytemuck::cast_slice(&parameters), usage: wgpu::BufferUsages::UNIFORM,
        }));
    }
    let depth_clear_pipeline = if draws_slice.iter().any(|draw| draw.clear_depth_before != 0) {
        let mut cached = ctx.annotation_depth_pipeline.lock().unwrap();
        Some(
            cached
                .entry(sample_count)
                .or_insert_with(|| annotation_depth_clear_pipeline(&ctx.device, sample_count))
                .clone(),
        )
    } else {
        None
    };
    for (pass_index, range) in passes.iter().enumerate() {
        if let Some(item) = draw_order.get(range.start) {
            let draw = &draws_slice[item.draw_index];
            // A scene's opaque/deferred sorting may move its captured barrier.
            // Execute it once at layer entry, using the original clear viewport.
            let entering_layer = pass_index == 0 || passes.get(pass_index - 1)
                .and_then(|previous| draw_order.get(previous.start))
                .is_some_and(|previous| draws_slice[previous.draw_index].render_layer != draw.render_layer);
            let barrier = entering_layer.then(|| draws_slice.iter().find(|candidate|
                candidate.render_layer == draw.render_layer && candidate.clear_depth_before != 0)).flatten();
            if let Some(draw) = barrier.filter(|barrier|
                states_slice[barrier.render_state_slot as usize].viewport != [0, 0, 0, 1]) {
                let viewport = resolved_viewport(
                    &states_slice[draw.render_state_slot as usize],
                    target_width,
                    target_height,
                )?;
                let mut clear_pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                    label: Some("Coin Annotation Depth Clear"),
                    color_attachments: &[],
                    depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                        view: depth_view,
                        depth_ops: Some(wgpu::Operations {
                            load: wgpu::LoadOp::Load,
                            store: wgpu::StoreOp::Store,
                        }),
                        stencil_ops: None,
                    }),
                    timestamp_writes: None,
                    occlusion_query_set: None,
                });
                clear_pass.set_pipeline(depth_clear_pipeline.as_ref().unwrap());
                clear_pass.set_scissor_rect(
                    viewport[0] as u32,
                    viewport[1] as u32,
                    viewport[2] as u32,
                    viewport[3] as u32,
                );
                clear_pass.draw(0..3, 0..1);
            }
        }
        let is_peel = draw_order.get(range.start).is_some_and(|item| item.peel);
        let is_weighted = draw_order.get(range.start).is_some_and(|item|item.weighted);
        let sample_depth = peeling.is_some() && sample_count > 1;
        let profile = if sample_depth {
            if shadow_multitexture { ctx.shadow_multitexture_peeling_surface_profile(policy) }
            else { ctx.peeling_surface_profile(policy) }
        } else { profile };
        if is_peel {
            if sample_count > 1 {
                peeling.as_ref().unwrap().snapshot_depth(&ctx.device, &mut encoder, depth_view);
            } else if ctx.adapter.get_info().backend == wgpu::Backend::Gl {
                let mut converter = ctx.depth_snapshot.lock().unwrap();
                let converter = converter.get_or_insert_with(|| depth_transfer::DepthTransfer::new_gl(&ctx.device, true));
                converter.copy_depth(&ctx.device, &mut encoder, depth_view, &peeling.as_ref().unwrap().opaque_view);
            } else {
                encoder.copy_texture_to_texture(
                    wgpu::TexelCopyTextureInfo {
                        texture: depth_texture,
                        mip_level: 0,
                        origin: wgpu::Origin3d::ZERO,
                        aspect: wgpu::TextureAspect::DepthOnly,
                    },
                    wgpu::TexelCopyTextureInfo {
                        texture: &peeling.as_ref().unwrap().opaque,
                        mip_level: 0,
                        origin: wgpu::Origin3d::ZERO,
                        aspect: wgpu::TextureAspect::DepthOnly,
                    },
                    wgpu::Extent3d {
                        width: target_width,
                        height: target_height,
                        depth_or_array_layers: 1,
                    },
                );
            }
        }
        for peel_step in 0..if is_peel { peel_passes as usize } else { 1 } {
            let selected_color = if is_weighted { &weighted.as_ref().unwrap().accumulation } else if is_peel {
                &peeling.as_ref().unwrap().colors[peel_step]
            } else {
                color_view
            };
            let selected_depth = if is_peel {
                &peeling.as_ref().unwrap().depths[peel_step]
            } else {
                depth_view
            };
            let mut color_attachments = vec![Some(wgpu::RenderPassColorAttachment {
                view: selected_color,
                // Resolve together with the final composed draw. A separate
                // empty pass can discard the previously rendered MSAA color
                // on Vulkan implementations even when its load op is Load.
                resolve_target: if !is_peel && !is_weighted &&
                    pass_index + 1 == passes.len() && peel_step == 0 {
                    resolve_view
                } else { None },
                ops: wgpu::Operations {
                    load: if is_peel || is_weighted {
                        wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT)
                    } else if sample_count > 1 && pass_index == 0 && clear_attachments {
                        wgpu::LoadOp::Clear(wgpu::Color {
                            r: clear_color[0] as f64, g: clear_color[1] as f64,
                            b: clear_color[2] as f64, a: clear_color[3] as f64,
                        })
                    } else {
                        wgpu::LoadOp::Load
                    },
                    store: wgpu::StoreOp::Store,
                },
            })];
            if is_peel {
                color_attachments.push(Some(wgpu::RenderPassColorAttachment {
                    view: &peeling.as_ref().unwrap().masks[peel_step],
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT),
                        store: wgpu::StoreOp::Store,
                    },
                }));
            }
            if is_weighted {
                color_attachments.push(Some(wgpu::RenderPassColorAttachment {
                    view:&weighted.as_ref().unwrap().revealage,resolve_target:None,
                    ops:wgpu::Operations{load:wgpu::LoadOp::Clear(wgpu::Color::WHITE),store:wgpu::StoreOp::Store},
                }));
            }
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("Coin Composition Pass"),
                color_attachments: &color_attachments,
                depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                    view: selected_depth,
                    depth_ops: Some(wgpu::Operations {
                        load: if is_peel || (sample_count > 1 && pass_index == 0 && clear_attachments && !load_initial_depth) {
                            wgpu::LoadOp::Clear(1.0)
                        } else {
                            wgpu::LoadOp::Load
                        },
                        store: wgpu::StoreOp::Store,
                    }),
                    stencil_ops: None,
                }),
                timestamp_writes: if !is_peel && !is_weighted && pass_index + 1 == passes.len() {
                    timestamp_query.map(|query_set| wgpu::RenderPassTimestampWrites {
                        query_set,
                        beginning_of_pass_write_index: None,
                        end_of_pass_write_index: Some(1),
                    })
                } else {
                    None
                },
                occlusion_query_set: None,
            });

            for item in &draw_order[range.clone()] {
                let draw = &draws_slice[item.draw_index];
                if draw.index_count == 0 {
                    continue;
                }
                let st = &states_slice[draw.render_state_slot as usize];
                if st.viewport == [0, 0, 0, 1] { continue; }
                let mat = &materials_slice[st.material_slot as usize];

                let cull_face = match st.cull_mode {
                    0 => None,
                    2 => Some(wgpu::Face::Front),
                    _ => Some(wgpu::Face::Back),
                };
                let front_face = match st.front_face {
                    1 => wgpu::FrontFace::Cw,
                    _ => wgpu::FrontFace::Ccw,
                };

                let depth_compare = if st.depth_test == 0 {
                    wgpu::CompareFunction::Always
                } else {
                    match st.depth_function {
                        0 => wgpu::CompareFunction::Never,
                        1 => wgpu::CompareFunction::Always,
                        3 => wgpu::CompareFunction::LessEqual,
                        4 => wgpu::CompareFunction::Equal,
                        5 => wgpu::CompareFunction::GreaterEqual,
                        6 => wgpu::CompareFunction::Greater,
                        7 => wgpu::CompareFunction::NotEqual,
                        _ => wgpu::CompareFunction::Less,
                    }
                };
                let resolved_offset = st.polygon_offset_enabled != 0
                    && st.polygon_offset_styles & st.polygon_offset_primitive_style != 0
                    && (st.polygon_offset_slope_bias != 0.0
                        || (st.polygon_offset_max_depth_bits != 0
                            && st.polygon_offset_units != 0.0));
                // WebGPU viewport intervals must be ascending. The existing
                // fragment-depth path maps a reversed Coin interval without
                // changing clipping or projection.
                // Deferred selectors/weighted fragments map custom intervals in
                // the shader. Coplanar 1x writers must use that same expression,
                // since native viewport mapping can round it differently. At the
                // default 0..1 interval with zero bias, mix(0,1,FragCoord.z) is
                // exactly FragCoord.z; retain ordinary early depth there. MSAA
                // weighted keeps its existing shared sample-depth delivery.
                let shared_shadow_shader_depth = shadow_multitexture && draw_order.iter()
                    .any(|item| (sample_count > 1 && item.weighted) ||
                        (sample_count == 1 && st.depth_range != [0.0,1.0] && (item.weighted || item.peel)));
                let resolved_depth_bias = shared_shadow_shader_depth || sample_depth || resolved_offset || st.depth_range[0] > st.depth_range[1] || rasterization_policy == 1 ||
                    (shadow_multitexture && sample_count > 1 && custom_shadow_sample_depth(st));
                // Ordinary MSAA uses hardware depth and must not introduce a
                // sample-interpolated module on devices that lack sample shading.
                // Only the fragment-depth entry point needs the private profile;
                // peeling already selected its multisampled-resource layout above.
                let profile = if sample_count > 1 && !sample_depth && (!is_weighted || shadow_multitexture) && resolved_depth_bias {
                    if shadow_multitexture { ctx.shadow_multitexture_depth_surface_profile(policy) }
                    else { ctx.sample_depth_surface_profile(policy) }
                } else { profile };
                let map_mode = shadow_frame.map_or(0, |shadow| if shadow.extra.is_empty() {1} else {2});
                let module_profile = if st.portable_shader_slot != 0 {
                    let module = portable_modules.get(st.portable_shader_slot as usize-1)
                        .ok_or_else(|| (CoinWgpuStatus::InvalidArgument,"Invalid portable shader draw occurrence".into()))?;
                    Some(portable::profile(ctx,module,profile,sample_count,map_mode)?)
                } else { None };
                let profile = module_profile.as_ref().map_or(profile,|profile|profile.as_ref());
                let native_depth_bias = polygon_depth_bias(st, draw.topology).map_err(|e| {
                    (
                        CoinWgpuStatus::InvalidArgument,
                        format!("Draw {}: {}", item.draw_index, e),
                    )
                })?;
                let window_bias = if resolved_offset {
                    resolved_polygon_bias(st, draw, vertices_slice, indices_slice).map_err(|e| {
                        (
                            CoinWgpuStatus::InvalidArgument,
                            format!("Draw {}: {}", item.draw_index, e),
                        )
                    })?
                } else {
                    0.0
                };
                let depth_bias = if resolved_depth_bias {
                    wgpu::DepthBiasState::default()
                } else {
                    native_depth_bias
                };
                let eight_shadows = shadow_frame.is_some_and(|s| !s.extra.is_empty());
                if bump_binding.is_some() && trace_materials {
                    eprintln!("COIN_RENDER_PHASE wgpu_bump_run_profile render_state_slot={} light_model={} light_count={} profile_bump_receiver={} shadow_frame={} bump_binding={} portable_shader_slot={} material_slot={}",
                        draw.render_state_slot, st.light_model, st.light_count,
                        profile.bump_receiver, shadow_frame.is_some(), bump_binding.is_some(),
                        st.portable_shader_slot, st.material_slot);
                }
                let pipeline = match get_or_create_pipeline(
                    ctx, profile,
                    draw.topology,
                    if is_peel || is_weighted {
                        wgpu::TextureFormat::Rgba16Float
                    } else {
                        color_format
                    },
                    wgpu::TextureFormat::Depth32Float,
                    sample_count,
                    cull_face,
                    front_face,
                    item.blend && !is_peel && !is_weighted,
                    item.additive,
                    item.legacy_blend_alpha,
                    is_peel,
                    is_peel || (!is_weighted && st.depth_test != 0 && st.depth_write != 0),
                    st.color_write,
                    if is_peel {
                        wgpu::CompareFunction::LessEqual
                    } else {
                        depth_compare
                    },
                    depth_bias,
                    resolved_depth_bias,
                    eight_shadows,
                    shadow_frame.is_some(),
                    is_weighted,
                    st.portable_shader_slot != 0 || bump_binding.is_some() || st.has_texture != 0 || st.extra_textures.iter().any(|unit| unit.enabled != 0),
                    instanced,
                ) {
                    Ok(p) => p,
                    Err(e) => return Err((CoinWgpuStatus::BackendError, e)),
                };
                pass.set_pipeline(&pipeline);

                let mut mv: [[f32; 4]; 4] = [[0.0; 4]; 4];
                let mut mvp: [[f32; 4]; 4] = [[0.0; 4]; 4];
                let mut nm: [[f32; 4]; 4] = [[0.0; 4]; 4];
                for c in 0..4 {
                    for r in 0..4 {
                        mv[c][r] = st.model_view[c * 4 + r];
                        mvp[c][r] = st.model_view_projection[c * 4 + r];
                        nm[c][r] = st.normal_matrix[c * 4 + r];
                    }
                }

                let layers: [CoinWgpuTextureUnit; 8] = std::array::from_fn(|unit| {
                    if unit == 0 {
                        CoinWgpuTextureUnit {
                            matrix: st.texture_matrix,
                            enabled: st.has_texture,
                            texture_slot: st.texture_slot,
                            sampler_slot: st.sampler_slot,
                            model: st.texture_model,
                            blend_color: st.texture_blend_color,
                        }
                    } else {
                        st.extra_textures[unit - 1]
                    }
                });
                let texture_bindings: Vec<_> = layers
                    .iter().enumerate()
                    .map(|(unit, layer)| {
                        if unit == 0 {
                            if let Some(image) = raster_images.get(&(draw.render_state_slot as usize)) {
                                return (&image.view, &image.sampler,
                                    [image.width as f32, image.height as f32, 0.0, 3.0]);
                            }
                        }
                        if layer.enabled == 0 {
                            return (&ctx.default_texture_view, &ctx.default_sampler, [1.0, 1.0, 0.0, 0.0]);
                        }
                        let t = &textures_slice[layer.texture_slot as usize];
                        let s = &samplers_slice[layer.sampler_slot as usize];
                        let tk = TextureKey::from_texture(t);
                        let sk = SamplerKey {
                            wrap_s: s.wrap_s,
                            wrap_t: s.wrap_t,
                            filter: s.filter,
            max_anisotropy_minus_one: s.reserved,
                        };
                        let (view, width, height) = if gpu_texture_token(t) {
                            let resource = rtt_cache.active.get(&t.content_digest)
                                .expect("RTT token preflighted before encoding");
                            (&resource.view, if policy == SamplingPolicy::Portable {resource.width} else {1}, if policy == SamplingPolicy::Portable {resource.height} else {1})
                        } else if let Some(entry) = tex_cache.entries.get(&tk) {
                            (&entry.view, if policy == SamplingPolicy::Portable {entry.texture.width()} else {1}, if policy == SamplingPolicy::Portable {entry.texture.height()} else {1})
                        } else { (&ctx.default_texture_view, 1, 1) };
                        let supplied = if t.format == 12 {
                            let (n,_,mips)=cube_atlas_metadata(t).expect("cube atlas preflighted").unwrap();
                            [n as f32,if mips{1.0}else{0.0},(8+s.filter) as f32,0.0]
                        } else if t.format == 11 {
                            let (w,h,d,mips)=spatial_atlas_metadata(t).expect("spatial atlas preflighted")
                                .expect("spatial atlas format");
                            let flags=s.wrap_s+2*s.wrap_t+4*((s.reserved>>4)&1)+
                                if mips {8} else {0};
                            [w as f32,h as f32,(4+s.filter) as f32,(d*16+flags) as f32]
                        } else {
                            [width as f32,height as f32,s.filter as f32,
                             (s.wrap_s+2*s.wrap_t) as f32]
                        };
                        (view, samp_cache.entries.get(&sk).unwrap_or(&ctx.default_sampler),supplied)
                    })
                    .collect();
                let extra_texture_matrices = std::array::from_fn(|unit| {
                    std::array::from_fn(|c| {
                        std::array::from_fn(|row| st.extra_textures[unit].matrix[c * 4 + row])
                    })
                });
                let extra_tex_params = std::array::from_fn(|unit| {
                    let t = st.extra_textures[unit];
                    [
                        if t.enabled != 0 { t.enabled as f32 } else { 0.0 },
                        t.model as f32,
                        if t.enabled != 0 && textures_slice[t.texture_slot as usize].format == 1 {
                            1.0
                        } else {
                            0.0
                        },
                        st.texture_projection as f32,
                    ]
                });
                let extra_texture_blends =
                    std::array::from_fn(|unit| st.extra_textures[unit].blend_color);

                let mut tex_mat: [[f32; 4]; 4] = [[0.0; 4]; 4];
                for c in 0..4 {
                    for r in 0..4 {
                        tex_mat[c][r] = st.texture_matrix[c * 4 + r];
                    }
                }

                let receiver = shadow_frame.map(|shadow| {
                    (&shadow.receivers[draw.render_state_slot as usize], shadow)
                });
                let receiver_second = shadow_frame.and_then(|shadow| shadow.second.as_ref())
                    .map(|second| (&second.receivers[draw.render_state_slot as usize], second));
                let receiver_third = shadow_frame.and_then(|shadow| shadow.third.as_ref())
                    .map(|pass| (&pass.receivers[draw.render_state_slot as usize], pass));
                let receiver_fourth = shadow_frame.and_then(|shadow| shadow.fourth.as_ref())
                    .map(|pass| (&pass.receivers[draw.render_state_slot as usize], pass));
                let shadow_matrix = |flat: &[f32; 16]| -> [[f32; 4]; 4] {
                    std::array::from_fn(|col| std::array::from_fn(|row| flat[col * 4 + row]))
                };
                let quality_flags = shadow_frame.map_or(0, |shadow| {
                    let slot = draw.render_state_slot as usize;
                    let mut flags = shadow.receivers[slot].receives;
                    for pass in shadow.second.iter().chain(shadow.third.iter()).chain(shadow.fourth.iter()).chain(shadow.extra.iter()) {
                        flags |= pass.receivers[slot].receives;
                    }
                    flags
                });
                let uniforms = CoinWgpuUniforms {
                    alpha_test: [st.alpha_test_function as f32, st.alpha_test_reference, 0.0, 0.0],
                    raster_viewport: if rasterization_policy == 1 {
                        let viewport = resolved_viewport(st, target_width, target_height)?;
                        [viewport[0] as f32, (target_height as i32 - viewport[1] - viewport[3]) as f32,
                         viewport[2] as f32, viewport[3] as f32]
                    } else { [0.0; 4] },
                    sampling_texture_sizes: std::array::from_fn(|unit| texture_bindings[unit].2),
                    flat_shadow_primary: [st.material_slot, st.flat_shadow_primary, 0, 0],
                    model_view_projection: mvp,
                    model_view: mv,
                    normal_matrix: nm,
                    material_diffuse: mat.diffuse,
                    material_ambient: mat.ambient,
                    material_specular: mat.specular,
                    light_direction_intensity: [
                        st.light_direction[0],
                        st.light_direction[1],
                        st.light_direction[2],
                        st.light_intensity,
                    ],
                    light_color: st.light_color,
                    params: [
                        mat.shininess,
                        0.0,
                        if st.has_light != 0 { 1.0 } else { 0.0 },
                        st.light_model as f32,
                    ],
                    texture_matrix: tex_mat,
                    extra_texture_matrices,
                    extra_tex_params,
                    extra_texture_blends,
                    texture_combines: st.texture_combines,
                    composition_meta: [
                        item.screen_door_level as f32,
                        if item.screen_door { 1.0 } else { 0.0 },
                        target_height as f32,
                        0.0,
                    ],
                    peel_meta: [
                        if is_peel { (peel_step + 1) as f32 } else { 0.0 },
                        st.depth_test as f32,
                        st.depth_function as f32,
                        if st.depth_test != 0 && st.depth_write != 0 { 1.0 } else { 0.0 },
                    ],
                    tex_params: [
                        if st.has_texture != 0 { st.has_texture as f32 } else { 0.0 },
                        st.texture_model as f32,
                        if st.has_texture != 0
                            && textures_slice[st.texture_slot as usize].format == 1
                        {
                            1.0
                        } else {
                            0.0
                        },
                        st.texture_projection as f32,
                    ],
                    ambient_light: st.ambient_light,
                    light_meta: [st.light_count as f32, 0.0,
                        ((quality_flags & 4) != 0) as u32 as f32, ((quality_flags & 8) != 0) as u32 as f32],
                    texture_blend_color: st.texture_blend_color,
                    lights: st.lights,
                    clip_meta: [
                        st.clip_plane_count as f32,
                        window_bias,
                        st.depth_range[0],
                        st.depth_range[1],
                    ],
                    clip_planes: st.clip_planes,
                    fog_color_mode: [
                        st.fog_color[0],
                        st.fog_color[1],
                        st.fog_color[2],
                        st.fog_mode as f32,
                    ],
                    fog_range: [st.fog_start, st.fog_end, 0.0, 0.0],
                    shadow_model_view_projection: receiver.map_or(
                        [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                         [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                        |(r, _)| shadow_matrix(&r.model_view_projection)),
                    shadow_model_view: receiver.map_or(
                        [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                         [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                        |(r, _)| shadow_matrix(&r.model_view)),
                    shadow_params: receiver.map_or([0.0, 0.0, 1.0, 0.0],
                        |(r, shadow)| [(r.receives & 1) as f32, shadow.near,
                            shadow.far, shadow.epsilon]),
                    shadow_meta: receiver.map_or([0.0; 4], |(r, shadow)|
                        [shadow.threshold, r.lighting_index as f32, shadow.kind as f32,
                         r.max_shadow_distance]),
                    shadow_falloff: receiver.map_or([0.0; 4], |(r, map)|
                        [r.distance_falloff_coefficient, ((r.receives & 2) != 0) as u32 as f32, map.map_size as f32, 0.0]),
                    shadow_model_view_projection_second: receiver_second.map_or(
                        [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                         [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                        |(r, _)| shadow_matrix(&r.model_view_projection)),
                    shadow_model_view_second: receiver_second.map_or(
                        [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                         [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                        |(r, _)| shadow_matrix(&r.model_view)),
                    shadow_params_second: receiver_second.map_or([0.0, 0.0, 1.0, 0.0],
                        |(r, second)| [(r.receives & 1) as f32, second.near,
                            second.far, second.epsilon]),
                    shadow_meta_second: receiver_second.map_or([0.0; 4], |(r, second)|
                        [second.threshold, r.lighting_index as f32,
                         second.kind as f32, r.max_shadow_distance]),
                    shadow_falloff_second: receiver_second.map_or([0.0; 4], |(r, map)|
                        [r.distance_falloff_coefficient, ((r.receives & 2) != 0) as u32 as f32, map.map_size as f32, 0.0]),
                    shadow_model_view_projection_third: receiver_third.map_or(
                        [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                         [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                        |(r, _)| shadow_matrix(&r.model_view_projection)),
                    shadow_model_view_third: receiver_third.map_or(
                        [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                         [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                        |(r, _)| shadow_matrix(&r.model_view)),
                    shadow_params_third: receiver_third.map_or([0.0, 0.0, 1.0, 0.0],
                        |(r, pass)| [(r.receives & 1) as f32, pass.near,
                            pass.far, pass.epsilon]),
                    shadow_meta_third: receiver_third.map_or([0.0; 4], |(r, pass)|
                        [pass.threshold, r.lighting_index as f32,
                         pass.kind as f32, r.max_shadow_distance]),
                    shadow_falloff_third: receiver_third.map_or([0.0; 4], |(r, map)|
                        [r.distance_falloff_coefficient, ((r.receives & 2) != 0) as u32 as f32, map.map_size as f32, 0.0]),
                    shadow_model_view_projection_fourth: receiver_fourth.map_or(
                        [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                         [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                        |(r, _)| shadow_matrix(&r.model_view_projection)),
                    shadow_model_view_fourth: receiver_fourth.map_or(
                        [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                         [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                        |(r, _)| shadow_matrix(&r.model_view)),
                    shadow_params_fourth: receiver_fourth.map_or([0.0, 0.0, 1.0, 0.0],
                        |(r, pass)| [(r.receives & 1) as f32, pass.near,
                            pass.far, pass.epsilon]),
                    shadow_meta_fourth: receiver_fourth.map_or([0.0; 4], |(r, pass)|
                        [pass.threshold, r.lighting_index as f32,
                         pass.kind as f32, r.max_shadow_distance]),
                    shadow_falloff_fourth: receiver_fourth.map_or([0.0; 4], |(r, map)|
                        [r.distance_falloff_coefficient, ((r.receives & 2) != 0) as u32 as f32, map.map_size as f32, 0.0]),
                };

                let extra_passes = shadow_frame.map_or(&[][..], |shadow| shadow.extra.as_slice());
                let extra_receivers: Vec<&CoinWgpuShadowReceiver> = extra_passes.iter()
                    .map(|p| &p.receivers[draw.render_state_slot as usize]).collect();
                if layers.iter().any(|t| t.enabled != 0)
                    && FAULT_INJECTION.load(Ordering::SeqCst) == FAULT_RTT_BIND_GROUP
                {
                    return Err((
                        CoinWgpuStatus::OutOfMemory,
                        "Injected RTT bind-group creation failure".to_string(),
                    ));
                }
                let base_shadow_views = [
                    shadow_map.as_ref().map_or(&ctx.default_texture_view, |map| &map.view),
                    shadow_map_second.as_ref().map_or(&ctx.default_texture_view, |map| &map.view),
                    shadow_map_third.as_ref().map_or(&ctx.default_texture_view, |map| &map.view),
                    shadow_map_fourth.as_ref().map_or(&ctx.default_texture_view, |map| &map.view),
                ];
                let extra_shadow_views: [&wgpu::TextureView; 4] = std::array::from_fn(|slot|
                    extra_shadow_maps.get(slot).map_or(&ctx.default_texture_view,
                        |map| &map.view));
                let create_binding = |u_buffer: &wgpu::Buffer,
                    shadow_views: [&wgpu::TextureView; 4]| {
                    let mut entries = vec![
                        wgpu::BindGroupEntry {
                            binding: 0,
                            resource: wgpu::BindingResource::Buffer(wgpu::BufferBinding {
                                buffer: u_buffer, offset: 0,
                                size: std::num::NonZeroU64::new(uniform_size as u64),
                            }),
                        },
                        wgpu::BindGroupEntry {
                            binding: 1,
                            resource: materials_buffer.as_entire_binding(),
                        },
                    ];
                    for (unit, (view, sampler, _size)) in texture_bindings.iter().enumerate() {
                        if eight_shadows && !profile.shadow_multitexture && unit >= 4 { continue; }
                        entries.push(wgpu::BindGroupEntry {
                            binding: 2 + 2 * unit as u32,
                            resource: wgpu::BindingResource::TextureView(view),
                        });
                        entries.push(wgpu::BindGroupEntry {
                            binding: 3 + 2 * unit as u32,
                            resource: wgpu::BindingResource::Sampler(sampler),
                        });
                    }
                    let previous = if is_peel && peel_step > 0 {
                        &peeling.as_ref().unwrap().depths[peel_step - 1]
                    } else if sample_depth {
                        &peeling.as_ref().unwrap().opaque_view
                    } else {
                        &ctx.default_depth_view
                    };
                    let opaque = if is_peel || sample_depth {
                        &peeling.as_ref().unwrap().opaque_view
                    } else {
                        &ctx.default_depth_view
                    };
                    entries.push(wgpu::BindGroupEntry {
                        binding: 18,
                        resource: wgpu::BindingResource::TextureView(previous),
                    });
                    entries.push(wgpu::BindGroupEntry {
                        binding: 19,
                        resource: wgpu::BindingResource::TextureView(opaque),
                    });
                    if profile.shadow_multitexture {
                        entries.push(wgpu::BindGroupEntry { binding: 20,
                            resource: wgpu::BindingResource::TextureView(&shadow_array.as_ref().unwrap().view) });
                    } else {
                    entries.push(wgpu::BindGroupEntry {
                        binding: 20,
                        resource: wgpu::BindingResource::TextureView(shadow_views[0]),
                    });
                    entries.push(wgpu::BindGroupEntry {
                        binding: 21,
                        resource: wgpu::BindingResource::TextureView(shadow_views[1]),
                    });
                    entries.push(wgpu::BindGroupEntry {
                        binding: 22,
                        resource: wgpu::BindingResource::TextureView(shadow_views[2]),
                    });
                    entries.push(wgpu::BindGroupEntry {
                        binding: 23,
                        resource: wgpu::BindingResource::TextureView(shadow_views[3]),
                    });
                    }
                    if let Some((image,sampler,_))=bump_binding {
                      entries.push(wgpu::BindGroupEntry {binding:25,resource:wgpu::BindingResource::TextureView(image)});
                      entries.push(wgpu::BindGroupEntry {binding:26,resource:wgpu::BindingResource::Sampler(sampler)});
                    }
                    if st.portable_shader_slot != 0 {
                        entries.push(wgpu::BindGroupEntry { binding: 27,
                            resource: module_buffers[st.portable_shader_slot as usize-1].as_entire_binding() });
                    }
                    if let Some(buffer) = instances_buffer.as_ref() {
                        entries.push(wgpu::BindGroupEntry { binding: 24,
                            resource: buffer.as_entire_binding() });
                    }
                    ctx.device.create_bind_group(&wgpu::BindGroupDescriptor {
                        label: Some("Coin shared draw texture bindings"),
                        layout: if instanced { &profile.instanced_bind_group_layout } else if eight_shadows { &profile.shadow_eight_layout } else { &profile.bind_group_layout },
                        entries: &entries,
                    })
                };
                if eight_shadows {
                    #[repr(C)]
                    #[derive(Copy, Clone, Pod, Zeroable)]
                    struct ExtraReceiver {
                        view_to_clip: [[f32; 4]; 4], view_to_light: [[f32; 4]; 4],
                        params: [f32; 4], meta: [f32; 4], falloff: [f32; 4],
                    }
                    let mut receivers = [ExtraReceiver::zeroed(); 4];
                    for (slot, (p, r)) in extra_passes.iter().zip(&extra_receivers).enumerate() {
                        receivers[slot] = ExtraReceiver {
                            view_to_clip: shadow_matrix(&r.model_view_projection),
                            view_to_light: shadow_matrix(&r.model_view),
                            params: [(r.receives & 1) as f32, p.near, p.far, p.epsilon],
                            meta: [p.threshold, r.lighting_index as f32, p.kind as f32, r.max_shadow_distance],
                            falloff: [r.distance_falloff_coefficient, ((r.receives & 2) != 0) as u32 as f32, p.map_size as f32, 0.0],
                        };
                    }
                    let buffer = multipass_upload_buffer(&ctx.device, &wgpu::util::BufferInitDescriptor {
                        label: Some("Coin extra shadow receivers"), contents: bytemuck::cast_slice(&receivers),
                        usage: wgpu::BufferUsages::UNIFORM,
                    });
                    let mut entries = vec![wgpu::BindGroupEntry { binding: 0, resource: buffer.as_entire_binding() }];
                    for (slot, view) in extra_shadow_views.iter().enumerate().filter(|_| !profile.shadow_multitexture) {
                        entries.push(wgpu::BindGroupEntry { binding: slot as u32 + 1,
                            resource: wgpu::BindingResource::TextureView(view) });
                    }
                    let binding = ctx.device.create_bind_group(&wgpu::BindGroupDescriptor {
                        label: Some("Coin extra shadow receiver binding"), layout: &profile.shadow_extra_layout, entries: &entries,
                    });
                    pass.set_bind_group(1, &binding, &[]);
                }
                // Queue writes are submitted before this frame's command buffer.
                // They cannot alter a preceding submission; wgpu keeps the
                // underlying resources alive while that submission is in flight.
                if sampling_audit {
                    let hash=bytemuck::bytes_of(&uniforms)[..uniform_size].iter()
                        .fold(0xcbf29ce484222325u64,|h,b|(h^u64::from(*b)).wrapping_mul(0x100000001b3));
                    eprintln!("COIN_SAMPLING_AUDIT mode={:?} revision={} instanced={} instances={} draws={} texture_units={} uniform_bytes={} uniform_hash={:016x}",
                        policy,frame_revision,instanced,instances_slice.len(),draws_slice.len(),
                        layers.iter().filter(|x|x.enabled!=0).count(),uniform_size,hash);
                }
                let (chunk, uniform_offset) = uniform_arena.push(&ctx.device, &uniforms);
                let units = std::array::from_fn(|unit| {
                    let layer = &layers[unit];
                    if layer.enabled == 0 || (eight_shadows && !profile.shadow_multitexture && unit >= 4) {
                        (u32::MAX, u32::MAX)
                    } else { (layer.texture_slot, layer.sampler_slot) }
                });
                let key = (chunk, units, if is_peel { peel_step } else { usize::MAX }, st.portable_shader_slot);
                let binding = draw_bindings.entry(key).or_insert_with(|| {
                    create_binding(uniform_arena.buffer(chunk), base_shadow_views)
                });
                pass.set_bind_group(0, &*binding, &[uniform_offset]);
                let viewport = resolved_viewport(st, target_width, target_height)?;
                if !st.depth_range[0].is_finite()
                    || !st.depth_range[1].is_finite()
                    || st.depth_range[0] < 0.0
                    || st.depth_range[0] > 1.0
                    || st.depth_range[1] < 0.0
                    || st.depth_range[1] > 1.0
                {
                    return Err((
                        CoinWgpuStatus::InvalidArgument,
                        format!(
                            "Draw {} has invalid depth range {:?}",
                            item.draw_index, st.depth_range
                        ),
                    ));
                }
                pass.set_viewport(
                    viewport[0] as f32,
                    viewport[1] as f32,
                    viewport[2] as f32,
                    viewport[3] as f32,
                    if resolved_depth_bias || is_peel || is_weighted {
                        0.0
                    } else {
                        st.depth_range[0]
                    },
                    if resolved_depth_bias || is_peel || is_weighted {
                        1.0
                    } else {
                        st.depth_range[1]
                    },
                );
                pass.set_scissor_rect(
                    viewport[0] as u32,
                    viewport[1] as u32,
                    viewport[2] as u32,
                    viewport[3] as u32,
                );

                if draw.stable_node_id != 0 {
                    let key = (draw.stable_node_id, draw.draw_ordinal);
                    if let Some((_rev, entry)) = cache.active_entries.get(&key) {
                        pass.set_vertex_buffer(0, entry.vertex_buffer.slice(..));
                        if let Some(ref ib) = entry.index_buffer {
                            pass.set_index_buffer(ib.slice(..), wgpu::IndexFormat::Uint32);
                            pass.draw_indexed(0..entry.index_count, 0, 0..1);
                        } else {
                            pass.draw(0..entry.vertex_count, 0..1);
                        }
                    } else if let Some(entry) = oversized_entries.get(&key) {
                        pass.set_vertex_buffer(0, entry.vertex_buffer.slice(..));
                        if let Some(ref ib) = entry.index_buffer {
                            pass.set_index_buffer(ib.slice(..), wgpu::IndexFormat::Uint32);
                            pass.draw_indexed(0..entry.index_count, 0, 0..1);
                        } else {
                            pass.draw(0..entry.vertex_count, 0..1);
                        }
                    }
                } else {
                    let instance_range = if instanced {
                        let range = &instance_ranges[item.draw_index];
                        range.first_instance..range.first_instance + range.instance_count
                    } else { 0..1 };
                    if split_uncached {
                        if let Some((vb, ib)) = &split_uncached_buffers[item.draw_index] {
                            pass.set_vertex_buffer(0, vb.slice(..));
                            pass.set_index_buffer(ib.slice(..), wgpu::IndexFormat::Uint32);
                            pass.draw_indexed(0..draw.index_count, 0, instance_range);
                        }
                    } else {
                        if let Some(vb) = &uncached_v_buffer {
                            pass.set_vertex_buffer(0, vb.slice(..));
                        }
                        if let Some(ib) = &uncached_i_buffer {
                            pass.set_index_buffer(ib.slice(..), wgpu::IndexFormat::Uint32);
                        }
                        let start_idx = draw.first_index;
                        let end_idx = start_idx + draw.index_count;
                        pass.draw_indexed(start_idx..end_idx, 0, instance_range);
                    }
                }

            }
        }
        if is_weighted {
            weighted.as_ref().unwrap().composite_samples(&mut encoder, color_view,
                if pass_index + 1 == passes.len() { resolve_view } else { None },
                if pass_index + 1 == passes.len() { timestamp_query } else { None });
        }
        if is_peel {
            peeling.as_ref().unwrap().composite_samples(
                &mut encoder,
                color_view,
                depth_view,
                if pass_index + 1 == passes.len() { resolve_view } else { None },
                if pass_index + 1 == passes.len() {
                    timestamp_query
                } else {
                    None
                },
            );
        }
    }

    // Retire any oversized buffers immediately after pass completion
    for (_, entry) in oversized_entries {
        cache.deferred_release.push(RetiredBuffer {
            vertex_buffer: entry.vertex_buffer,
            index_buffer: entry.index_buffer,
            retired_at_serial: current_submission_serial,
            size_bytes: entry.size_bytes,
        });
    }

    uniform_arena.upload(&ctx.queue);
    ctx.draw_bind_groups.store(draw_bindings.len() as u32, Ordering::Relaxed);
    ctx.uniform_chunks.store(uniform_arena.chunk_count() as u32, Ordering::Relaxed);
    if passes.is_empty() {
      if let Some(resolve_view) = resolve_view {
        let _resolve = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("Coin empty-frame MSAA color resolve"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view: color_view,
                resolve_target: Some(resolve_view),
                ops: wgpu::Operations { load: wgpu::LoadOp::Load, store: wgpu::StoreOp::Discard },
            })],
            depth_stencil_attachment: None,
            timestamp_writes: None,
            occlusion_query_set: None,
        });
      }
    }
    Ok((encoder.finish(), pending_instances))
}

#[no_mangle]
pub extern "C" fn coin_wgpu_is_available() -> i32 {
    let res = std::panic::catch_unwind(|| {
        let fault = FAULT_INJECTION.load(Ordering::SeqCst);
        if fault == CoinWgpuStatus::NotReady as i32 {
            return 0;
        }
        if init_runtime_if_needed().is_err() {
            return 0;
        }
        let mut guard = match RUNTIME_CTX.lock() {
            Ok(g) => g,
            Err(_) => return 0,
        };
        if let Some(runtime) = guard.as_mut() {
            if get_or_init_device(runtime, None).is_ok() {
                1
            } else {
                0
            }
        } else {
            0
        }
    });
    res.unwrap_or(0)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_get_adapter_info(buffer: *mut std::os::raw::c_char, buffer_len: usize) {
    let _ = std::panic::catch_unwind(|| {
        if buffer.is_null() || buffer_len == 0 {
            return;
        }
        if init_runtime_if_needed().is_ok() {
            if let Ok(mut guard) = RUNTIME_CTX.lock() {
                if let Some(runtime) = guard.as_mut() {
                    if let Ok(dev) = get_or_init_device(runtime, None) {
                        set_error(buffer, buffer_len, &dev.adapter_name);
                        return;
                    }
                }
            }
        }
        set_error(buffer, buffer_len, "None");
    });
}

#[repr(C)]
#[derive(Default)]
pub struct CoinWgpuRuntimeCapabilities {
    pub struct_size: u32,
    pub renderer: u32,
    pub vendor_id: u32,
    pub device_id: u32,
    pub max_framebuffer_attachments: u32,
    pub format_rgba8: u32,
    pub format_d24s8: u32,
    pub format_d32f: u32,
    pub format_rgba16f: u32,
    pub format_r16f: u32,
    pub runtime_features: u64,
    pub available_mechanisms: u64,
}

const _: () = {
    assert!(std::mem::size_of::<CoinWgpuRuntimeCapabilities>() == 56);
    assert!(std::mem::offset_of!(CoinWgpuRuntimeCapabilities, runtime_features) == 40);
};

fn portable_format_features(features: wgpu::TextureFormatFeatures) -> u32 {
    let mut result = 0;
    if features
        .allowed_usages
        .contains(wgpu::TextureUsages::TEXTURE_BINDING)
    {
        result |= 1;
    }
    if features
        .allowed_usages
        .contains(wgpu::TextureUsages::RENDER_ATTACHMENT)
    {
        result |= 2;
    }
    if features
        .flags
        .contains(wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X4)
    {
        result |= 4;
    }
    if features
        .flags
        .contains(wgpu::TextureFormatFeatureFlags::STORAGE_READ_ONLY)
    {
        result |= 8;
    }
    if features.flags.intersects(
        wgpu::TextureFormatFeatureFlags::STORAGE_WRITE_ONLY
            | wgpu::TextureFormatFeatureFlags::STORAGE_READ_WRITE,
    ) {
        result |= 16;
    }
    result
}

// Read-only epoch of the existing default device. Zero means unavailable;
// this query never creates or reconstructs a device.
#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_multipass(width:u32,height:u32,format:u32,surface:u64) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        let guard=RUNTIME_CTX.lock().unwrap();
        let Some(ctx)=guard.as_ref().and_then(|runtime|runtime.device_state.as_ref()) else {
            return CoinWgpuStatus::NotReady;
        };
        let usage=wgpu::TextureUsages::STORAGE_BINDING|wgpu::TextureUsages::TEXTURE_BINDING;
        if width>ctx.device.limits().max_texture_dimension_2d || height>ctx.device.limits().max_texture_dimension_2d ||
            !ctx.adapter.get_texture_format_features(wgpu::TextureFormat::Rgba32Float).allowed_usages.contains(usage) {
            return CoinWgpuStatus::Unsupported;
        }
        let output=if surface!=0 {
            let Some(record)=guard.as_ref().unwrap().surfaces.get(&surface) else {return CoinWgpuStatus::InvalidArgument;};
            if record.presentation.is_some() {wgpu::TextureFormat::Rgba8Unorm} else {record.color_format}
        } else {
            match format {0=>wgpu::TextureFormat::Rgba8Unorm,1=>wgpu::TextureFormat::Rgba8UnormSrgb,
                2=>wgpu::TextureFormat::Rgba16Float,_=>return CoinWgpuStatus::Unsupported}
        };
        ctx.device.push_error_scope(wgpu::ErrorFilter::Validation);
        let programs=ctx.multipass_programs.get(&ctx.device,output);
        if pollster::block_on(ctx.device.pop_error_scope()).is_some() {
            return CoinWgpuStatus::Unsupported;
        }
        if programs.is_err() { return CoinWgpuStatus::BackendError; }
        CoinWgpuStatus::Ok
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_default_device_generation() -> u64 {
    std::panic::catch_unwind(|| {
        let guard = match RUNTIME_CTX.lock() {
            Ok(g) => g,
            Err(_) => return 0,
        };
        if DEVICE_LOST_OCCURRED.load(Ordering::SeqCst) {
            return 0;
        }
        guard
            .as_ref()
            .and_then(|rt| rt.device_state.as_ref())
            .map_or(0, |device| device.generation.checked_add(1).unwrap_or(0))
    })
    .unwrap_or(0)
}

fn shadow_multitexture_supported(ctx: &DeviceState) -> bool {
    let limits = ctx.device.limits();
    limits.max_inter_stage_shader_components >= 80 && limits.max_sampled_textures_per_shader_stage >= 11 && limits.max_samplers_per_shader_stage >= 8
}

fn shadow_array_supported(ctx: &DeviceState,extent:u32,layers:u32)->bool {
    extent>0 && layers>0 && extent<=ctx.device.limits().max_texture_dimension_2d &&
        shadow::array_layer_count(layers)<=ctx.device.limits().max_texture_array_layers &&
        ctx.adapter.get_texture_format_features(wgpu::TextureFormat::Rgba32Float).allowed_usages
            .contains(wgpu::TextureUsages::TEXTURE_BINDING|wgpu::TextureUsages::COPY_DST|wgpu::TextureUsages::COPY_SRC|wgpu::TextureUsages::RENDER_ATTACHMENT)
}

// Pure admission: materialize each selected complete program before producers
// or WSI acquisition. No output commands are submitted by these probes.
fn shadow_multitexture_alpha_supported(ctx:&DeviceState,samples:u32,modes:u32)->bool {
    if !matches!(samples,1|2|4) || !shadow_multitexture_supported(ctx) { return false; }
    if modes&2!=0 && !weighted_supported(ctx,samples) || modes&4!=0 && !peeling_supported(ctx,samples) { return false; }
    if samples>1 && modes&6!=0 && !shadow_multitexture_depth_supported(ctx) { return false; }
    let mut cache=ctx.shadow_multitexture_alpha_admission.lock().unwrap();
    *cache.entry((samples,modes)).or_insert_with(|| {
        ctx.device.push_error_scope(wgpu::ErrorFilter::Validation);
        let mut valid=true;
        for policy in [SamplingPolicy::Native,SamplingPolicy::Portable] {
            for eight in [false,true] {
                for mode in [1u32,2,4] {
                    if modes&mode==0 {continue;}
                    let profile=if mode==4 && samples>1 {ctx.shadow_multitexture_peeling_surface_profile(policy)}
                        else if mode==2 && samples>1 {ctx.shadow_multitexture_depth_surface_profile(policy)}
                        else {ctx.shadow_multitexture_surface_profile(policy)};
                    valid &= get_or_create_pipeline(ctx,profile,0,if mode==1 {wgpu::TextureFormat::Rgba8Unorm} else {wgpu::TextureFormat::Rgba16Float},
                        wgpu::TextureFormat::Depth32Float,samples,None,wgpu::FrontFace::Ccw,
                        mode==1,false,false,mode==4,true,1,wgpu::CompareFunction::LessEqual,
                        wgpu::DepthBiasState::default(),(mode!=1 && samples>1) || (samples==1 && modes&6!=0),
                        eight,true,mode==2,true,false).is_ok();
                }
            }
        }
        block_on(ctx.device.pop_error_scope()).is_none() && valid
    })
}

// Append-only private admission; the public options and frame-view layout are intact.
#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_bump_receiver(
    samples: u32,
    vertices: u64,
    materials: u64,
    image_axis: u32,
    sampling_policy: u32,
) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if !matches!(samples, 1 | 2 | 4) || sampling_policy > 1 {
            return CoinWgpuStatus::InvalidArgument;
        }
        let guard = RUNTIME_CTX.lock().unwrap();
        let Some(ctx) = guard
            .as_ref()
            .and_then(|runtime| runtime.device_state.as_ref())
        else {
            return CoinWgpuStatus::NotReady;
        };
        let limits = ctx.device.limits();
        let bytes = vertices
            .checked_add(materials)
            .and_then(|count| count.checked_mul(std::mem::size_of::<GpuMaterial>() as u64));
        if limits.max_inter_stage_shader_components < 80
            || limits.max_vertex_attributes < 13
            || limits.max_sampled_textures_per_shader_stage < 12
            || limits.max_samplers_per_shader_stage < 9
            || limits.max_storage_buffers_per_shader_stage < 1
            || image_axis > limits.max_texture_dimension_2d
            || bytes.is_none_or(|bytes| {
                bytes > u64::from(limits.max_storage_buffer_binding_size)
                    || bytes > limits.max_buffer_size
            })
        {
            return CoinWgpuStatus::Unsupported;
        }
        if samples > 1 {
            let required = if samples == 2 {
                wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X2
            } else {
                wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X4
            };
            if (samples == 2
                && !ctx
                    .device
                    .features()
                    .contains(wgpu::Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES))
                || [
                    wgpu::TextureFormat::Rgba8Unorm,
                    wgpu::TextureFormat::Depth32Float,
                ]
                .into_iter()
                .any(|format| {
                    !ctx.adapter
                        .get_texture_format_features(format)
                        .flags
                        .contains(required)
                })
            {
                return CoinWgpuStatus::Unsupported;
            }
        }
        ctx.device.push_error_scope(wgpu::ErrorFilter::Validation);
        let policy = if sampling_policy == 0 {
            SamplingPolicy::Native
        } else {
            SamplingPolicy::Portable
        };
        let profile = ctx.bump_receiver_profile(policy);
        let valid = get_or_create_pipeline(
            ctx,
            &profile,
            0,
            wgpu::TextureFormat::Rgba8Unorm,
            wgpu::TextureFormat::Depth32Float,
            samples,
            None,
            wgpu::FrontFace::Ccw,
            false,
            false,
            false,
            false,
            true,
            0xf,
            wgpu::CompareFunction::Less,
            wgpu::DepthBiasState::default(),
            false,
            false,
            true,
            false,
            true,
            false,
        )
        .is_ok();
        if block_on(ctx.device.pop_error_scope()).is_none() && valid {
            CoinWgpuStatus::Ok
        } else {
            CoinWgpuStatus::Unsupported
        }
    })
    .unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_shadow_multitexture_alpha(samples:u32,modes:u32,extent:u32,layers:u32)->CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if modes&!7!=0 || modes==0 || !matches!(samples,1|2|4) {return CoinWgpuStatus::InvalidArgument;}
        let guard=RUNTIME_CTX.lock().unwrap();
        let Some(ctx)=guard.as_ref().and_then(|runtime|runtime.device_state.as_ref()) else {return CoinWgpuStatus::NotReady;};
        if shadow_array_supported(ctx,extent,layers) && shadow_multitexture_alpha_supported(ctx,samples,modes) {
            CoinWgpuStatus::Ok
        } else {CoinWgpuStatus::Unsupported}
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_shadow_multitexture(sample_count: u32) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        let guard = RUNTIME_CTX.lock().unwrap();
        let Some(runtime) = guard.as_ref() else { return CoinWgpuStatus::NotReady; };
        let Some(ctx) = runtime.device_state.as_ref() else { return CoinWgpuStatus::NotReady; };
        if !shadow_multitexture_supported(ctx) { return CoinWgpuStatus::Unsupported; }
        let slot = match sample_count { 1 => 0, 2 => 1, 4 => 2, _ => return CoinWgpuStatus::InvalidArgument };
        if sample_count == 2 && !ctx.device.features().contains(wgpu::Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES) {
            return CoinWgpuStatus::Unsupported;
        }
        if sample_count > 1 {
            let required = if sample_count == 2 { wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X2 }
                else { wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X4 };
            if [wgpu::TextureFormat::Rgba8Unorm, wgpu::TextureFormat::Depth32Float].into_iter().any(|format|
                !ctx.adapter.get_texture_format_features(format).flags.contains(required)) {
                return CoinWgpuStatus::Unsupported;
            }
        }
        let admitted = *ctx.shadow_multitexture_admission[slot].get_or_init(|| {
            ctx.device.push_error_scope(wgpu::ErrorFilter::Validation);
            let mut valid = true;
            for policy in [SamplingPolicy::Native, SamplingPolicy::Portable] {
                let profile = ctx.shadow_multitexture_surface_profile(policy);
                for eight in [false, true] {
                    valid &= get_or_create_pipeline(ctx, profile, 0, wgpu::TextureFormat::Rgba8Unorm,
                        wgpu::TextureFormat::Depth32Float, sample_count, None, wgpu::FrontFace::Ccw,
                        false, false, false, false, true, 1, wgpu::CompareFunction::Less,
                        wgpu::DepthBiasState::default(), false, eight, true, false, true, false).is_ok();
                }
            }
            block_on(ctx.device.pop_error_scope()).is_none() && valid
        });
        if admitted { CoinWgpuStatus::Ok } else { CoinWgpuStatus::Unsupported }

    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_shadow_multitexture_depth(sample_count: u32) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        let guard = RUNTIME_CTX.lock().unwrap();
        let Some(runtime) = guard.as_ref() else { return CoinWgpuStatus::NotReady; };
        let Some(ctx) = runtime.device_state.as_ref() else { return CoinWgpuStatus::NotReady; };
        if !shadow_multitexture_depth_supported(ctx) { return CoinWgpuStatus::Unsupported; }
        let slot = match sample_count { 1 => 0, 2 => 1, 4 => 2, _ => return CoinWgpuStatus::InvalidArgument };
        if sample_count == 2 && !ctx.device.features().contains(wgpu::Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES) {
            return CoinWgpuStatus::Unsupported;
        }
        if sample_count > 1 {
            let required = if sample_count == 2 { wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X2 }
                else { wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X4 };
            if [wgpu::TextureFormat::Rgba8Unorm, wgpu::TextureFormat::Depth32Float].into_iter().any(|format|
                !ctx.adapter.get_texture_format_features(format).flags.contains(required)) {
                return CoinWgpuStatus::Unsupported;
            }
        }
        let admitted = *ctx.shadow_multitexture_depth_admission[slot].get_or_init(|| {
            ctx.device.push_error_scope(wgpu::ErrorFilter::Validation);
            let mut valid = true;
            for policy in [SamplingPolicy::Native, SamplingPolicy::Portable] {
                let profile = ctx.shadow_multitexture_depth_surface_profile(policy);
                for eight in [false, true] {
                    valid &= get_or_create_pipeline(ctx, profile, 0, wgpu::TextureFormat::Rgba8Unorm,
                        wgpu::TextureFormat::Depth32Float, sample_count, None, wgpu::FrontFace::Ccw,
                        false, false, false, false, true, 1, wgpu::CompareFunction::Less,
                        wgpu::DepthBiasState::default(), true, eight, true, false, true, false).is_ok();
                }
            }
            block_on(ctx.device.pop_error_scope()).is_none() && valid
        });
        if admitted { CoinWgpuStatus::Ok } else { CoinWgpuStatus::Unsupported }

    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_sample_depth(samples: u32) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if !matches!(samples, 1 | 2 | 4) { return CoinWgpuStatus::InvalidArgument; }
        let guard = RUNTIME_CTX.lock().unwrap();
        let Some(runtime) = guard.as_ref() else { return CoinWgpuStatus::NotReady; };
        let Some(ctx) = runtime.device_state.as_ref() else { return CoinWgpuStatus::NotReady; };
        if samples == 1 || ctx.adapter.get_downlevel_capabilities().flags.contains(wgpu::DownlevelFlags::MULTISAMPLED_SHADING) {
            CoinWgpuStatus::Ok
        } else { CoinWgpuStatus::Unsupported }
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_peeling(samples: u32) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if !matches!(samples, 1 | 2 | 4) { return CoinWgpuStatus::InvalidArgument; }
        let guard = RUNTIME_CTX.lock().unwrap();
        let Some(runtime) = guard.as_ref() else { return CoinWgpuStatus::NotReady; };
        let Some(ctx) = runtime.device_state.as_ref() else { return CoinWgpuStatus::NotReady; };
        if peeling_supported(ctx, samples) { CoinWgpuStatus::Ok } else { CoinWgpuStatus::Unsupported }
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_weighted(samples: u32) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if !matches!(samples, 1 | 2 | 4) { return CoinWgpuStatus::InvalidArgument; }
        let mut guard = RUNTIME_CTX.lock().unwrap();
        let Some(runtime) = guard.as_mut() else { return CoinWgpuStatus::NotReady; };
        let Some(ctx) = runtime.device_state.as_ref() else { return CoinWgpuStatus::NotReady; };
        if weighted_supported(ctx, samples) {
            CoinWgpuStatus::Ok
        } else { CoinWgpuStatus::Unsupported }
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_raster_images(outputs:u32,width:u32,height:u32,bytes:u64)->CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if outputs==0 {return CoinWgpuStatus::Ok;}
        if outputs>4096 || width==0 || height==0 || bytes>128*1024*1024 {return CoinWgpuStatus::Unsupported;}
        let guard=match RUNTIME_CTX.lock(){Ok(g)=>g,Err(_)=>return CoinWgpuStatus::BackendError};
        let Some(ctx)=guard.as_ref().and_then(|runtime|runtime.device_state.as_ref()) else {return CoinWgpuStatus::NotReady;};
        if width>ctx.device.limits().max_texture_dimension_2d || height>ctx.device.limits().max_texture_dimension_2d {
            return CoinWgpuStatus::Unsupported;
        }
        match raster_image::prepare(ctx) {Ok(_)=>CoinWgpuStatus::Ok,Err((status,_))=>status}
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_texture_profile(formats:u32, outputs:u32, anisotropy:u32)->CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if formats&!31!=0 || outputs&!15!=0 || !anisotropy.is_power_of_two() || anisotropy>16 {
            return CoinWgpuStatus::InvalidArgument;
        }
        if init_runtime_if_needed().is_err() {return CoinWgpuStatus::BackendError;}
        let mut guard=match RUNTIME_CTX.lock(){Ok(g)=>g,Err(_)=>return CoinWgpuStatus::BackendError};
        let runtime=match guard.as_mut(){Some(r)=>r,None=>return CoinWgpuStatus::NotReady};
        let ctx=match get_or_init_device(runtime,None){Ok(d)=>d,Err(_)=>return CoinWgpuStatus::NotReady};
        if anisotropy>1 && !ctx.adapter.get_downlevel_capabilities().flags.contains(wgpu::DownlevelFlags::ANISOTROPIC_FILTERING) {
            return CoinWgpuStatus::Unsupported;
        }
        let kinds=[wgpu::TextureFormat::Rgba8Unorm,wgpu::TextureFormat::Rgba8UnormSrgb,
            wgpu::TextureFormat::Rgba16Float,wgpu::TextureFormat::Bc3RgbaUnorm,wgpu::TextureFormat::Bc3RgbaUnormSrgb];
        for (i,format) in kinds.into_iter().enumerate() {
            if formats&(1<<i)==0 {continue;}
            if format.is_compressed() && !ctx.device.features().contains(wgpu::Features::TEXTURE_COMPRESSION_BC) {return CoinWgpuStatus::Unsupported;}
            let facts=ctx.adapter.get_texture_format_features(format);
            if !facts.allowed_usages.contains(wgpu::TextureUsages::TEXTURE_BINDING|wgpu::TextureUsages::COPY_DST) {return CoinWgpuStatus::Unsupported;}
        }
        for (bit,mipbit,format) in [(1,4,wgpu::TextureFormat::Rgba8Unorm),(2,8,wgpu::TextureFormat::Rgba16Float)] {
            if outputs&(bit|mipbit)==0 {continue;}
            let usage=wgpu::TextureUsages::RENDER_ATTACHMENT|wgpu::TextureUsages::TEXTURE_BINDING|
                if outputs&mipbit!=0 {wgpu::TextureUsages::STORAGE_BINDING} else {wgpu::TextureUsages::empty()};
            if !ctx.adapter.get_texture_format_features(format).allowed_usages.contains(usage) {return CoinWgpuStatus::Unsupported;}
        }
        CoinWgpuStatus::Ok
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_query_runtime_capabilities(
    output: *mut CoinWgpuRuntimeCapabilities,
    output_size: usize,
) -> CoinWgpuStatus {
    if output.is_null() || output_size != std::mem::size_of::<CoinWgpuRuntimeCapabilities>() {
        return CoinWgpuStatus::InvalidArgument;
    }
    std::panic::catch_unwind(|| {
        if FAULT_INJECTION.load(Ordering::SeqCst) == CoinWgpuStatus::NotReady as i32 {
            return CoinWgpuStatus::NotReady;
        }
        if init_runtime_if_needed().is_err() {
            return CoinWgpuStatus::BackendError;
        }
        let mut guard = match RUNTIME_CTX.lock() {
            Ok(g) => g,
            Err(_) => return CoinWgpuStatus::BackendError,
        };
        let runtime = match guard.as_mut() {
            Some(r) => r,
            None => return CoinWgpuStatus::NotReady,
        };
        let dev = match get_or_init_device(runtime, None) {
            Ok(d) => d,
            Err(_) => return CoinWgpuStatus::NotReady,
        };
        let info = dev.adapter.get_info();
        let limits = dev.adapter.limits();
        let format = |f| portable_format_features(dev.adapter.get_texture_format_features(f));
        let mut result = CoinWgpuRuntimeCapabilities {
            struct_size: output_size as u32,
            renderer: adapter_renderer(info.backend),
            vendor_id: info.vendor,
            device_id: info.device,
            max_framebuffer_attachments: limits.max_color_attachments,
            format_rgba8: format(wgpu::TextureFormat::Rgba8Unorm),
            // Depth24PlusStencil8 is an abstract format; it does not certify D24S8.
            format_d24s8: 0,
            format_d32f: format(wgpu::TextureFormat::Depth32Float),
            format_rgba16f: format(wgpu::TextureFormat::Rgba16Float),
            format_r16f: format(wgpu::TextureFormat::R16Float),
            ..Default::default()
        };
        if limits.max_color_attachments > 1 {
            result.runtime_features |= 1 | 2;
        }
        if limits.max_compute_workgroups_per_dimension > 0 {
            result.runtime_features |= 4;
        }
        if dev
            .adapter
            .features()
            .contains(wgpu::Features::TIMESTAMP_QUERY)
        {
            result.runtime_features |= 8;
        }
        result.available_mechanisms = 1;
        // Executable formats use the enabled device feature set, not optional
        // adapter capabilities which this device did not request.
        if peeling::device_supported(&dev.device) {
            result.available_mechanisms |= 2;
        }
        if weighted::device_supported(&dev.device) { result.available_mechanisms |= 4; }
        unsafe {
            std::ptr::write(output, result);
        }
        CoinWgpuStatus::Ok
    })
    .unwrap_or(CoinWgpuStatus::BackendError)
}

#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/lib_runtime_capabilities_tests.rs"]
mod runtime_capabilities_tests;

#[no_mangle]
pub extern "C" fn coin_wgpu_inject_fault(fault_code: i32) {
    FAULT_INJECTION.store(fault_code, Ordering::SeqCst);
}
#[no_mangle]
pub extern "C" fn coin_wgpu_inject_fault_after_submits(fault_code: i32, skipped_submits: u32) {
    let remaining = if fault_code == 0 { -1 } else { skipped_submits.min(i32::MAX as u32) as i32 };
    FAULT_AFTER_CODE.store(fault_code, Ordering::SeqCst);
    FAULT_AFTER_SUBMITS.store(remaining, Ordering::SeqCst);
}

#[no_mangle]
pub extern "C" fn coin_wgpu_inject_async_fault(fault_code: i32) {
    FAULT_INJECTION_ASYNC.store(fault_code, Ordering::SeqCst);
}

#[no_mangle]
pub extern "C" fn coin_wgpu_reset_context() {
    let _ = std::panic::catch_unwind(|| {
        if let Ok(mut guard) = RUNTIME_CTX.lock() {
            if let Some(runtime) = guard.as_mut() {
                runtime.device_state = None;
                runtime.surfaces.clear();
                runtime.pending_readbacks.retain(|_, job| job.device_id != 0);
                runtime.retired_readbacks.retain(|job| job.device_id != 0);
                runtime.device_generation += 1;
            }
        }
        LAST_SUBMITTED_SERIAL.store(0, Ordering::SeqCst);
        GLOBAL_COMPLETED_SERIAL.store(0, Ordering::SeqCst);
        DEVICE_LOST_OCCURRED.store(false, Ordering::SeqCst);
        LAST_ASYNC_ERROR_KIND.store(0, Ordering::SeqCst);
        FAULT_SURFACE_OUTDATED_COUNT.store(0, Ordering::SeqCst);
        FAULT_SURFACE_LOST_COUNT.store(0, Ordering::SeqCst);
        if let Ok(mut lock) = LAST_ASYNC_ERROR_MSG.lock() {
            lock.clear();
        }
    });
}

#[no_mangle]
pub extern "C" fn coin_wgpu_surface_create(
    info: *const CoinWgpuSurfaceCreateInfo,
    out_surface: *mut CoinWgpuSurfaceId,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    let res = std::panic::catch_unwind(|| {
        if info.is_null() || out_surface.is_null() {
            set_error(error_buf, error_buf_len, "Null info or out_surface pointer");
            return CoinWgpuStatus::InvalidArgument;
        }

        let inf = unsafe { &*info };
        if inf.abi_version != COIN_WGPU_ABI_VERSION {
            set_error(
                error_buf,
                error_buf_len,
                &format!(
                    "ABI mismatch: expected {}, got {}",
                    COIN_WGPU_ABI_VERSION, inf.abi_version
                ),
            );
            return CoinWgpuStatus::InvalidArgument;
        }
        if inf.struct_size as usize != std::mem::size_of::<CoinWgpuSurfaceCreateInfo>() {
            set_error(
                error_buf,
                error_buf_len,
                "Struct size mismatch for CoinWgpuSurfaceCreateInfo",
            );
            return CoinWgpuStatus::InvalidArgument;
        }
        if inf.native.abi_version != COIN_WGPU_ABI_VERSION {
            set_error(
                error_buf,
                error_buf_len,
                &format!(
                    "ABI mismatch for native descriptor: expected {}, got {}",
                    COIN_WGPU_ABI_VERSION, inf.native.abi_version
                ),
            );
            return CoinWgpuStatus::InvalidArgument;
        }
        if inf.native.struct_size as usize != std::mem::size_of::<CoinWgpuNativeSurfaceDescriptor>()
        {
            set_error(
                error_buf,
                error_buf_len,
                "Struct size mismatch for CoinWgpuNativeSurfaceDescriptor",
            );
            return CoinWgpuStatus::InvalidArgument;
        }
        if renderer_backend(inf.renderer).is_err() {
            set_error(error_buf, error_buf_len, "Invalid requested surface renderer");
            return CoinWgpuStatus::InvalidArgument;
        }
        let sample_count = if inf.sample_count == 0 { 1 } else { inf.sample_count };
        if !matches!(sample_count, 1 | 2 | 4) {
            set_error(error_buf, error_buf_len, "Window MSAA sample count must be 1, 2 or 4");
            return CoinWgpuStatus::InvalidArgument;
        }
        if inf.native.reserved != 0 {
            set_error(
                error_buf,
                error_buf_len,
                "Reserved field in native descriptor must be 0",
            );
            return CoinWgpuStatus::InvalidArgument;
        }

        if let Err(e) = init_runtime_if_needed() {
            set_error(error_buf, error_buf_len, &e);
            return CoinWgpuStatus::BackendError;
        }

        let mut guard = match RUNTIME_CTX.lock() {
            Ok(g) => g,
            Err(e) => {
                set_error(error_buf, error_buf_len, &e.to_string());
                return CoinWgpuStatus::BackendError;
            }
        };
        let runtime = guard.as_mut().unwrap();

        #[cfg(target_os = "android")]
        if runtime.device_state.is_none() && runtime.surfaces.is_empty()
            && runtime.extra_devices.is_empty() && runtime.pending_readbacks.is_empty()
            && runtime.retired_readbacks.is_empty()
        {
            if let Ok(Some(backends)) = renderer_backend(inf.renderer) {
                // ANativeWindow admits one native graphics connection. Creating
                // EGL and Vulkan surfaces for it at once can yield EGL_BAD_ALLOC.
                // Narrow only an unused runtime; never replace a live device.
                let mut descriptor = wgpu::InstanceDescriptor::from_env_or_default();
                descriptor.backends = backends;
                runtime.instance = wgpu::Instance::new(&descriptor);
            }
        }

        // Create surface from native handles
        let surface =
            match unsafe { create_surface_from_descriptor(&runtime.instance, &inf.native) } {
                Ok(s) => s,
                Err(e) => {
                    set_error(error_buf, error_buf_len, &e);
                    return CoinWgpuStatus::Unsupported;
                }
            };

        // Select the requested API with this surface before fixing the shared device.
        let dev_generation = runtime.device_generation;
        let device_state = match get_or_init_device_impl(runtime, Some(&surface), false, inf.renderer) {
            Ok(d) => d,
            Err(e) => {
                set_error(error_buf, error_buf_len, &e);
                return if inf.renderer == 0 { CoinWgpuStatus::NotReady }
                       else { CoinWgpuStatus::Unsupported };
            }
        };

        // Check if surface is compatible with current adapter
        let caps = surface.get_capabilities(&device_state.adapter);
        if caps.formats.is_empty() || caps.present_modes.is_empty() {
            set_error(
                error_buf,
                error_buf_len,
                &format!(
                    "Surface is not compatible with adapter {}",
                    device_state.adapter_name
                ),
            );
            return CoinWgpuStatus::Unsupported;
        }

        let mut record = SurfaceRecord {
            published_serial: 0,
            surface,
            native_desc: inf.native,
            config: None,
            framebuffer_size: (inf.width, inf.height),
            color_format: wgpu::TextureFormat::Bgra8Unorm,
            sample_count,
            suspended: (inf.width == 0 || inf.height == 0),
            needs_reconfigure: true,
            surface_generation: 0,
            configured_device_generation: 0,
            depth_texture: None,
            depth_view: None,
            msaa_color_texture: None,
            msaa_color_view: None,
            presentation: None,
        };

        if inf.width > 0 && inf.height > 0 {
            if let Err(e) = configure_surface_record(
                &mut record,
                device_state,
                dev_generation,
                inf.width,
                inf.height,
            ) {
                set_error(error_buf, error_buf_len, &e);
                return if e.contains("MSAA") { CoinWgpuStatus::Unsupported }
                       else { CoinWgpuStatus::BackendError };
            }
        }

        let surface_id = NEXT_SURFACE_ID.fetch_add(1, Ordering::SeqCst);
        runtime.surfaces.insert(surface_id, record);
        unsafe {
            *out_surface = surface_id;
        }

        CoinWgpuStatus::Ok
    });

    res.unwrap_or_else(|_| {
        set_error(
            error_buf,
            error_buf_len,
            "Rust bridge panic caught in coin_wgpu_surface_create",
        );
        CoinWgpuStatus::BackendError
    })
}

#[no_mangle]
pub extern "C" fn coin_wgpu_surface_resize(
    surface_id: CoinWgpuSurfaceId,
    width: u32,
    height: u32,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    let res = std::panic::catch_unwind(|| {
        if surface_id == COIN_WGPU_INVALID_SURFACE_ID {
            set_error(error_buf, error_buf_len, "Invalid surface ID");
            return CoinWgpuStatus::InvalidArgument;
        }

        let mut guard = match RUNTIME_CTX.lock() {
            Ok(g) => g,
            Err(e) => {
                set_error(error_buf, error_buf_len, &e.to_string());
                return CoinWgpuStatus::BackendError;
            }
        };
        let runtime = match guard.as_mut() {
            Some(r) => r,
            None => {
                set_error(error_buf, error_buf_len, "Runtime context uninitialized");
                return CoinWgpuStatus::NotReady;
            }
        };

        let record = match runtime.surfaces.get_mut(&surface_id) {
            Some(r) => r,
            None => {
                set_error(error_buf, error_buf_len, "Surface ID not found");
                return CoinWgpuStatus::InvalidArgument;
            }
        };

        record.published_serial = 0;
        if width == 0 || height == 0 {
            record.framebuffer_size = (width, height);
            record.suspended = true;
            record.needs_reconfigure = true;
            record.depth_texture = None;
            record.depth_view = None;
            record.msaa_color_texture = None;
            record.msaa_color_view = None;
            record.presentation = None;
            return CoinWgpuStatus::Ok;
        }

        record.framebuffer_size = (width, height);
        record.suspended = false;
        record.needs_reconfigure = true;

        // If device is already initialized, we can reconfigure immediately
        if let Some(dev) = &runtime.device_state {
            let dev_gen = runtime.device_generation;
            if let Err(e) = configure_surface_record(record, dev, dev_gen, width, height) {
                set_error(error_buf, error_buf_len, &e);
                return CoinWgpuStatus::BackendError;
            }
        }

        CoinWgpuStatus::Ok
    });

    res.unwrap_or_else(|_| {
        set_error(
            error_buf,
            error_buf_len,
            "Rust bridge panic caught in coin_wgpu_surface_resize",
        );
        CoinWgpuStatus::BackendError
    })
}

#[no_mangle]
pub extern "C" fn coin_wgpu_surface_destroy(
    surface_id: CoinWgpuSurfaceId,
    _error_buf: *mut std::os::raw::c_char,
    _error_buf_len: usize,
) -> CoinWgpuStatus {
    let res = std::panic::catch_unwind(|| {
        if surface_id == COIN_WGPU_INVALID_SURFACE_ID {
            return CoinWgpuStatus::Ok; // Idempotent
        }

        if let Ok(mut guard) = RUNTIME_CTX.lock() {
            if let Some(runtime) = guard.as_mut() {
                runtime.surfaces.remove(&surface_id);
            }
        }
        CoinWgpuStatus::Ok
    });

    res.unwrap_or(CoinWgpuStatus::Ok)
}

fn coin_wgpu_surface_submit_internal(
    surface_id: CoinWgpuSurfaceId,
    frame: *const CoinWgpuFrameView,
    readback_rgba: *mut u8,
    readback_len: usize,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    let res = std::panic::catch_unwind(|| {
        let trace_phases = std::env::var_os("COIN_RENDER_TRACE_PHASES")
            .or_else(|| std::env::var_os("COIN_WGPU_TRACE_PHASES")).is_some();
        let profile_start = trace_phases.then(std::time::Instant::now);
        // 1. Fault injection check
        let fault = FAULT_INJECTION.load(Ordering::SeqCst);
        if fault == CoinWgpuStatus::NotReady as i32 {
            set_error(error_buf, error_buf_len, "Injected NOT_READY fault");
            return CoinWgpuStatus::NotReady;
        } else if fault == CoinWgpuStatus::OutOfMemory as i32
            || fault == FAULT_SURFACE_OUT_OF_MEMORY
        {
            set_error(error_buf, error_buf_len, "Injected OUT_OF_MEMORY fault");
            return CoinWgpuStatus::OutOfMemory;
        } else if fault == CoinWgpuStatus::DeviceLost as i32 {
            if let Ok(mut guard) = RUNTIME_CTX.lock() {
                if let Some(rt) = guard.as_mut() {
                    rt.device_state = None;
                    rt.device_generation += 1;
                }
            }
            DEVICE_LOST_OCCURRED.store(false, Ordering::SeqCst);
            set_error(error_buf, error_buf_len, "Injected DEVICE_LOST fault");
            return CoinWgpuStatus::DeviceLost;
        } else if fault == FAULT_SURFACE_TIMEOUT {
            set_error(error_buf, error_buf_len, "Injected SURFACE_TIMEOUT fault");
            return CoinWgpuStatus::NotReady;
        } else if fault == FAULT_SURFACE_OTHER {
            set_error(error_buf, error_buf_len, "Injected SURFACE_OTHER fault");
            return CoinWgpuStatus::BackendError;
        }

        // 2. Real device lost check
        if DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst) {
            if let Ok(mut guard) = RUNTIME_CTX.lock() {
                if let Some(rt) = guard.as_mut() {
                    rt.device_state = None;
                    rt.device_generation += 1;
                }
            }
            let msg = LAST_ASYNC_ERROR_MSG
                .lock()
                .map(|m| m.clone())
                .unwrap_or_else(|_| "Device lost".to_string());
            set_error(error_buf, error_buf_len, &msg);
            return CoinWgpuStatus::DeviceLost;
        }

        if surface_id == COIN_WGPU_INVALID_SURFACE_ID || frame.is_null() {
            set_error(
                error_buf,
                error_buf_len,
                "Invalid surface ID or null frame pointer",
            );
            return CoinWgpuStatus::InvalidArgument;
        }
        if (frame as usize) % std::mem::align_of::<CoinWgpuFrameView>() != 0 {
            set_error(error_buf, error_buf_len, "Misaligned frame pointer");
            return CoinWgpuStatus::InvalidArgument;
        }

        let f = unsafe { &*frame };
        if f.texture_output_flags!=0 || f.texture_sampling_policy>1 {
            set_error(error_buf,error_buf_len,"Surface submits do not accept RTT output flags");
            return CoinWgpuStatus::InvalidArgument;
        }


        // 3. ABI version and struct size validation
        if f.abi_version != COIN_WGPU_ABI_VERSION {
            set_error(
                error_buf,
                error_buf_len,
                &format!(
                    "ABI version mismatch: expected {}, got {}",
                    COIN_WGPU_ABI_VERSION, f.abi_version
                ),
            );
            return CoinWgpuStatus::InvalidArgument;
        }
        if f.struct_size as usize != std::mem::size_of::<CoinWgpuFrameView>() {
            set_error(
                error_buf,
                error_buf_len,
                &format!(
                    "Struct size mismatch: expected {}, got {}",
                    std::mem::size_of::<CoinWgpuFrameView>(),
                    f.struct_size
                ),
            );
            return CoinWgpuStatus::InvalidArgument;
        }

        if let Err(status)=validate_multipass_metadata(f,error_buf,error_buf_len) {return status;}
        let portable_modules = match portable::modules(f) {
            Ok(modules) => modules,
            Err((status,message)) => { set_error(error_buf,error_buf_len,&message); return status; }
        };
        if !portable_modules.is_empty() && (f.instance_count != 0 || f.instance_range_count != 0) {
            set_error(error_buf,error_buf_len,"Portable shader modules require ordinary non-instanced vertex delivery");
            return CoinWgpuStatus::Unsupported;
        }
        let bump_payload = match validate_bump_payload(f, error_buf, error_buf_len) {
            Ok(payload) => payload,
            Err(status) => return status,
        };

        // 4. Dimensions validation: zero dimensions = suspended -> NOT_READY
        if f.width == 0 || f.height == 0 {
            return CoinWgpuStatus::NotReady;
        }

        if let Err((status, message)) = instancing::validate_counts(f) {
            set_error(error_buf, error_buf_len, &message); return status;
        }
        let states_slice =
            match validate_slice(f.states, f.state_count, "states", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            };
        if !states_slice.iter().all(valid_alpha_test) {
            set_error(error_buf, error_buf_len, "Invalid alpha comparison function or reference");
            return CoinWgpuStatus::InvalidArgument;
        }
        let owned_patch = RUNTIME_CTX.lock().ok().and_then(|guard| {
            let runtime = guard.as_ref()?;
            if !runtime.surfaces.contains_key(&surface_id) { return None; }
            camera_base(runtime.device_state.as_ref()?, f, states_slice)
        });

        // 5. Preflight slice bounds and alignment validation BEFORE acquire
        let vertices_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.vertices.as_slice()
        } else { match validate_slice(
            f.vertices,
            f.vertex_count,
            "vertices",
            error_buf,
            error_buf_len,
        ) {
            Ok(s) => s,
            Err(st) => return st,
        } };
        let indices_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.indices.as_slice()
        } else { match validate_slice(
            f.indices,
            f.index_count,
            "indices",
            error_buf,
            error_buf_len,
        ) {
            Ok(s) => s,
            Err(st) => return st,
        } };
        let draws_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.draws.as_slice()
        } else { match validate_slice(f.draws, f.draw_count, "draws", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            } };
        if let Err(status)=validate_multipass_draws(f,draws_slice,error_buf,error_buf_len) {return status;}
        let materials_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.materials.as_slice()
        } else { match validate_slice(
            f.materials,
            f.material_count,
            "materials",
            error_buf,
            error_buf_len,
        ) {
            Ok(s) => s,
            Err(st) => return st,
        } };

        let instances_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.instances.as_slice()
        } else { match validate_slice(f.instances, f.instance_count, "instances", error_buf, error_buf_len) {
            Ok(s) => s, Err(st) => return st,
        } };
        let instance_ranges = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.instance_ranges.as_slice()
        } else { match validate_slice(f.instance_ranges, f.instance_range_count, "instance ranges", error_buf, error_buf_len) {
            Ok(s) => s, Err(st) => return st,
        } };
        if owned_patch.is_none() {
            if let Err((status, message)) = instancing::validate_payload(f, vertices_slice, indices_slice,
                draws_slice, materials_slice, states_slice, instances_slice, instance_ranges) {
                set_error(error_buf, error_buf_len, &message); return status;
            }
        }

        let textures_slice = if f.texture_count > 0 {
            match validate_slice(f.textures, f.texture_count, "textures", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            }
        } else {
            &[]
        };

        if let Err(message) = validate_texture_declarations(textures_slice) {
            set_error(error_buf, error_buf_len, &message);
            return CoinWgpuStatus::InvalidArgument;
        }

        let samplers_slice = if f.sampler_count > 0 {
            match validate_slice(f.samplers, f.sampler_count, "samplers", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            }
        } else {
            &[]
        };

        if let Err((status,message)) = portable::validate_bump_occurrences(states_slice,draws_slice,indices_slice,
            bump_payload.as_ref().map_or(&[],|payload|payload.vertices.as_slice())) {
            set_error(error_buf,error_buf_len,&message);return status;
        }
        if let Err((status,message)) = portable::validate_draws(&portable_modules,vertices_slice,indices_slice,
            draws_slice,materials_slice,states_slice,textures_slice) {
            set_error(error_buf,error_buf_len,&message);return status;
        }
        if let Err((status,message)) = validate_cube_resources(states_slice,textures_slice,samplers_slice)
            .and_then(|()|validate_cube_draws(states_slice,textures_slice,vertices_slice,indices_slice,draws_slice))
            .and_then(|()|raster_image::validate_with_images(states_slice,textures_slice,samplers_slice.len())) {
            set_error(error_buf,error_buf_len,&message);return status;
        }
        let unchecked_draws = if owned_patch.is_some() { &[] } else { draws_slice };
        for (i, draw) in unchecked_draws.iter().enumerate() {
            let idx_end = match draw.first_index.checked_add(draw.index_count) {
                Some(end) => end as usize,
                None => {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Draw {}: first_index + index_count overflow", i),
                    );
                    return CoinWgpuStatus::InvalidArgument;
                }
            };
            if idx_end > indices_slice.len() {
                set_error(
                    error_buf,
                    error_buf_len,
                    &format!("Draw {}: index range exceeds indices length", i),
                );
                return CoinWgpuStatus::InvalidArgument;
            }
            let vtx_end = match draw.first_vertex.checked_add(draw.vertex_count) {
                Some(end) => end as usize,
                None => {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Draw {}: first_vertex + vertex_count overflow", i),
                    );
                    return CoinWgpuStatus::InvalidArgument;
                }
            };
            if vtx_end > vertices_slice.len() {
                set_error(
                    error_buf,
                    error_buf_len,
                    &format!("Draw {}: vertex range exceeds vertices length", i),
                );
                return CoinWgpuStatus::InvalidArgument;
            }
            for idx in (draw.first_index as usize)..idx_end {
                let v_idx = indices_slice[idx] as usize;
                if v_idx >= vertices_slice.len() {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Draw {}: index {} references vertex out of bounds", i, idx),
                    );
                    return CoinWgpuStatus::InvalidArgument;
                }
            }
            if (draw.render_state_slot as usize) >= states_slice.len() {
                set_error(
                    error_buf,
                    error_buf_len,
                    &format!("Draw {}: render_state_slot out of bounds", i),
                );
                return CoinWgpuStatus::InvalidArgument;
            }
            let st = &states_slice[draw.render_state_slot as usize];
            if (st.material_slot as usize) >= materials_slice.len() {
                set_error(
                    error_buf,
                    error_buf_len,
                    &format!("Draw {}: material_slot out of bounds", i),
                );
                return CoinWgpuStatus::InvalidArgument;
            }
            if !valid_texture_payload(st) {
                set_error(error_buf, error_buf_len, "Invalid normalized texture program or extra unit state");
                return CoinWgpuStatus::InvalidArgument;
            }
            if let Err(message) = validate_texture_coordinates_with_images(st, draw.topology, vertices_slice,
                &indices_slice[draw.first_index as usize..idx_end], textures_slice) {
                set_error(error_buf, error_buf_len, &format!("Draw {i}: {message}"));
                return CoinWgpuStatus::InvalidArgument;
            }
            for layer in &st.extra_textures {
                if layer.enabled != 0 && ((layer.texture_slot as usize) >= textures_slice.len() ||
                    (layer.sampler_slot as usize) >= samplers_slice.len()) {
                    set_error(error_buf, error_buf_len, "Extra texture or sampler out of bounds");
                    return CoinWgpuStatus::InvalidArgument;
                }
            }
            if st.has_texture != 0 {
                if (st.texture_slot as usize) >= textures_slice.len() {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Draw {}: texture_slot out of bounds", i),
                    );
                    return CoinWgpuStatus::InvalidArgument;
                }
                if (st.sampler_slot as usize) >= samplers_slice.len() {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Draw {}: sampler_slot out of bounds", i),
                    );
                    return CoinWgpuStatus::InvalidArgument;
                }
            }
            if st.cull_mode > 2 {
                set_error(
                    error_buf,
                    error_buf_len,
                    &format!("Draw {}: invalid cull_mode", i),
                );
                return CoinWgpuStatus::InvalidArgument;
            }
            if st.front_face > 1 {
                set_error(
                    error_buf,
                    error_buf_len,
                    &format!("Draw {}: invalid front_face", i),
                );
                return CoinWgpuStatus::InvalidArgument;
            }
        }

        // Reject unsupported composition before acquiring a swapchain texture.
        let draw_order = if let Some(scene) = owned_patch.as_ref() {
            scene.draw_order.clone()
        } else { match composition::order(
            vertices_slice, indices_slice, draws_slice, materials_slice, states_slice, textures_slice) {
            Ok(order) => order,
            Err((status, message)) => {
                set_error(error_buf, error_buf_len, &message);
                return status;
            }
        } };

        if let Err((status, message)) = peeling::validate_request(
            if draw_order.iter().any(|item| item.peel) { f.sorted_layers_passes } else { 1 },
            f.transparency_reserved,
            f.transparency_budget_bytes,
            f.width,
            f.height,
            draw_order.iter().any(|item| item.peel || item.weighted),
        ) {
            set_error(error_buf, error_buf_len, &message);
            return status;
        }

        let shadow_frame = match validate_shadow_frame(f, states_slice.len(),
            owned_patch.is_some(), error_buf, error_buf_len) {
            Ok(shadow) => shadow,
            Err(status) => return status,
        };

        if let Some(payload) = bump_payload.as_ref() {
            if let Err((status, message)) = validate_bump_lighting_occurrences(payload,
                vertices_slice, indices_slice, draws_slice, states_slice, materials_slice,
                textures_slice, shadow_frame.as_ref()) {
                set_error(error_buf, error_buf_len, &message);
                return status;
            }
        }

        // 6. Lock RuntimeContext and resolve surface
        let mut guard = match RUNTIME_CTX.lock() {
            Ok(g) => g,
            Err(e) => {
                set_error(error_buf, error_buf_len, &e.to_string());
                return CoinWgpuStatus::BackendError;
            }
        };
        let runtime = match guard.as_mut() {
            Some(r) => r,
            None => {
                set_error(error_buf, error_buf_len, "Runtime context uninitialized");
                return CoinWgpuStatus::NotReady;
            }
        };

        if !runtime.surfaces.contains_key(&surface_id) {
            set_error(error_buf, error_buf_len, "Surface ID not found in registry");
            return CoinWgpuStatus::InvalidArgument;
        }

        if let Err((status,message))=admit_multipass_resources(f,
            runtime.surfaces.get(&surface_id).unwrap().sample_count,states_slice,textures_slice,
            &draw_order,shadow_frame.as_ref(),true,!readback_rgba.is_null(),false,
            multipass_pending_bytes(Some(runtime))) {
            set_error(error_buf,error_buf_len,&message);return status;
        }

        // Ensure device is ready
        let target_surface_ptr: *const wgpu::Surface = runtime
            .surfaces
            .get(&surface_id)
            .map(|s| &s.surface as *const wgpu::Surface)
            .unwrap_or(std::ptr::null());
        let target_surface_ref = unsafe { target_surface_ptr.as_ref() };
        let dev_generation = runtime.device_generation;
        let device_state = match get_or_init_device(runtime, target_surface_ref) {
            Ok(d) => d as *mut DeviceState,
            Err(e) => {
                set_error(error_buf, error_buf_len, &e);
                return CoinWgpuStatus::NotReady;
            }
        };
        let dev = unsafe { &mut *device_state };
        if bump_payload.is_some() && (f.rasterization_policy != 0 ||
            !instances_slice.is_empty() || !instance_ranges.is_empty()) {
            set_error(error_buf, error_buf_len, "Bump requires native non-instanced geometry");
            return CoinWgpuStatus::Unsupported;
        }

        if let Some(payload) = bump_payload.as_ref() {
            if extended_bump_profile(states_slice, textures_slice, draws_slice, shadow_frame.as_ref(), payload, indices_slice) {
                if let Err((status, message)) = validate_extended_bump_profile(dev,
                    vertices_slice, indices_slice, draws_slice, states_slice,
                    materials_slice, textures_slice, samplers_slice, payload,
                    shadow_frame.as_ref(), [f.width, f.height]) {
                    set_error(error_buf, error_buf_len, &message);
                    return status;
                }
            }
        }


        if let Some(scene) = owned_patch.as_ref() {
            if scene.generation != dev.generation {
                set_error(error_buf, error_buf_len, "Camera base belongs to a lost device generation");
                return CoinWgpuStatus::DeviceLost;
            }
            if !dev.validated_scene.as_ref().is_some_and(|current| Arc::ptr_eq(current, scene)) {
                set_error(error_buf, error_buf_len, "Camera base is no longer current on this device");
                return CoinWgpuStatus::NotReady;
            }
        }
        let capture_scene = !instances_slice.is_empty() || dev.camera_snapshot_policy.capture(
            dev.validated_scene.as_ref().map(|scene| scene.revision), f.frame_revision,
            f.camera_base_revision, owned_patch.is_some());
        let next_scene = if capture_scene && shadow_frame.is_none() {
            camera_scene(dev.generation, f, vertices_slice, indices_slice, draws_slice,
                materials_slice, states_slice, &draw_order, textures_slice, samplers_slice,
                instances_slice, instance_ranges, owned_patch.as_ref())
        } else { None };

        if let Err((status,message))=admit_spatial_resources(dev,textures_slice)
            .and_then(|()|raster_image::admit(dev,states_slice,textures_slice,samplers_slice.len())) {
            set_error(error_buf,error_buf_len,&message); return status;
        }
        let record = runtime.surfaces.get_mut(&surface_id).unwrap();
        if let Err((status, message)) = validate_shadow_multitexture_profile(dev, textures_slice, states_slice, draws_slice,
            &draw_order, shadow_frame.as_ref(), record.sample_count, f.rasterization_policy) {
            set_error(error_buf, error_buf_len, &message);
            return status;
        }
        if draw_order.iter().any(|item| item.peel) {
            if let Err((status, message)) = peeling::validate_request_samples(f.sorted_layers_passes,
                f.transparency_reserved, f.transparency_budget_bytes, f.width, f.height, true, record.sample_count) {
                set_error(error_buf, error_buf_len, &message); return status;
            }
            if !peeling_supported(dev, record.sample_count) {
                set_error(error_buf, error_buf_len, "Peeling formats cannot sample/render at the requested count");
                return CoinWgpuStatus::Unsupported;
            }
        }
        if draw_order.iter().any(|item| item.weighted) {
            if let Err((status, message)) = weighted::validate_budget(f.width, f.height, record.sample_count, f.transparency_budget_bytes) {
                set_error(error_buf, error_buf_len, &message);
                return status;
            }
        }
        if draw_order.iter().any(|item| item.weighted) &&
            !weighted_supported(dev, record.sample_count) {
            set_error(error_buf, error_buf_len, "Weighted MSAA color formats cannot blend/sample at the requested count");
            return CoinWgpuStatus::Unsupported;
        }
        if record.sample_count > 1 &&
            draw_order.iter().any(|item| bump_payload.is_some() && item.blend) {
            set_error(error_buf, error_buf_len,
                "Window MSAA translucent bump needs a separate profile");
            return CoinWgpuStatus::Unsupported;
        }

        // 7. Reconfigure if device generation changed or size changed
        if record.suspended {
            return CoinWgpuStatus::NotReady;
        }

        if record.configured_device_generation != dev_generation {
            match unsafe { create_surface_from_descriptor(&runtime.instance, &record.native_desc) }
            {
                Ok(new_surf) => {
                    record.surface = new_surf;
                }
                Err(e) => {
                    set_error(error_buf, error_buf_len, &e);
                    return CoinWgpuStatus::SurfaceLost;
                }
            }
            if let Err(e) = configure_surface_record(record, dev, dev_generation, f.width, f.height)
            {
                set_error(error_buf, error_buf_len, &e);
                return CoinWgpuStatus::BackendError;
            }
        } else if record.needs_reconfigure || record.framebuffer_size != (f.width, f.height) {
            if let Err(e) = configure_surface_record(record, dev, dev_generation, f.width, f.height)
            {
                set_error(error_buf, error_buf_len, &e);
                return CoinWgpuStatus::BackendError;
            }
        }

        if !readback_rgba.is_null() {
            let required = (f.width as usize).checked_mul(f.height as usize)
                .and_then(|pixels| pixels.checked_mul(4));
            if required != Some(readback_len) {
                set_error(error_buf, error_buf_len, "Window RGBA output size mismatch");
                return CoinWgpuStatus::InvalidArgument;
            }
            if !matches!(record.color_format, wgpu::TextureFormat::Bgra8Unorm |
                wgpu::TextureFormat::Rgba8Unorm |
                wgpu::TextureFormat::Bgra8UnormSrgb |
                wgpu::TextureFormat::Rgba8UnormSrgb) {
                set_error(error_buf, error_buf_len, "Window surface format is not RGBA8/BGRA8");
                return CoinWgpuStatus::Unsupported;
            }
        }

        let profile_validated = trace_phases.then(std::time::Instant::now);
        if let Err((status,message)) = portable::prepare(dev,&portable_modules,states_slice,draws_slice,
            &draw_order,record.sample_count,f.texture_sampling_policy,f.rasterization_policy,
            shadow_frame.as_ref().map_or(0,|shadow|if shadow.extra.is_empty(){1}else{2}),
            if record.presentation.is_some() {wgpu::TextureFormat::Rgba8Unorm} else {record.color_format}) {
            set_error(error_buf,error_buf_len,&message); return status;
        }

        // 8. Acquire texture with strictly 1 recovery attempt
        let injected_acquire = if fault == FAULT_SURFACE_TIMEOUT_AT_ACQUIRE {
            Some(wgpu::SurfaceError::Timeout)
        } else if fault == FAULT_SURFACE_OUTDATED_ONCE
            && FAULT_SURFACE_OUTDATED_COUNT.fetch_add(1, Ordering::SeqCst) == 0
        {
            Some(wgpu::SurfaceError::Outdated)
        } else if fault == FAULT_SURFACE_LOST_ONCE
            && FAULT_SURFACE_LOST_COUNT.fetch_add(1, Ordering::SeqCst) == 0
        {
            Some(wgpu::SurfaceError::Lost)
        } else if fault == FAULT_SURFACE_LOST_PERSISTENT {
            Some(wgpu::SurfaceError::Lost)
        } else {
            None
        };
        let trace_wsi = trace_phases || surface_wsi_tracing_enabled();
        let mut acquire_result = acquire_surface_texture(record, surface_id, 1,
            injected_acquire, trace_wsi);

        if let Err(err) = &acquire_result {
            match err {
                wgpu::SurfaceError::Timeout => {
                    set_error(error_buf, error_buf_len, "Surface acquire timeout");
                    return CoinWgpuStatus::NotReady;
                }
                wgpu::SurfaceError::OutOfMemory => {
                    set_error(error_buf, error_buf_len, "Surface acquire out of memory");
                    return CoinWgpuStatus::OutOfMemory;
                }
                wgpu::SurfaceError::Other => {
                    set_error(
                        error_buf,
                        error_buf_len,
                        "Surface acquire failed with internal error",
                    );
                    return CoinWgpuStatus::BackendError;
                }
                wgpu::SurfaceError::Outdated => {
                    // Reconfigure 1x and reacquire
                    if let Err(e) =
                        configure_surface_record(record, dev, dev_generation, f.width, f.height)
                    {
                        set_error(error_buf, error_buf_len, &e);
                        return CoinWgpuStatus::SurfaceLost;
                    }
                    acquire_result = acquire_surface_texture(record, surface_id, 2,
                        (fault == FAULT_SURFACE_LOST_PERSISTENT).then_some(wgpu::SurfaceError::Lost),
                        trace_wsi);
                }
                wgpu::SurfaceError::Lost => {
                    // Recreate surface 1x and reacquire
                    match unsafe {
                        create_surface_from_descriptor(&runtime.instance, &record.native_desc)
                    } {
                        Ok(new_surf) => {
                            record.surface = new_surf;
                            if let Err(e) = configure_surface_record(
                                record,
                                dev,
                                dev_generation,
                                f.width,
                                f.height,
                            ) {
                                set_error(error_buf, error_buf_len, &e);
                                return CoinWgpuStatus::SurfaceLost;
                            }
                            acquire_result = acquire_surface_texture(record, surface_id, 2,
                                (fault == FAULT_SURFACE_LOST_PERSISTENT).then_some(wgpu::SurfaceError::Lost),
                                trace_wsi);
                        }
                        Err(e) => {
                            set_error(error_buf, error_buf_len, &e);
                            return CoinWgpuStatus::SurfaceLost;
                        }
                    }
                }
            }
        }

        // Evaluate second acquire result with strict precedence
        let surface_texture = match acquire_result {
            Ok(st) => st,
            Err(wgpu::SurfaceError::OutOfMemory) => {
                set_error(
                    error_buf,
                    error_buf_len,
                    "Surface acquire out of memory on retry",
                );
                return CoinWgpuStatus::OutOfMemory;
            }
            Err(wgpu::SurfaceError::Timeout) => {
                set_error(error_buf, error_buf_len, "Surface acquire timeout on retry");
                return CoinWgpuStatus::NotReady;
            }
            Err(wgpu::SurfaceError::Other) => {
                set_error(
                    error_buf,
                    error_buf_len,
                    "Surface acquire other error on retry",
                );
                return CoinWgpuStatus::BackendError;
            }
            Err(wgpu::SurfaceError::Outdated) | Err(wgpu::SurfaceError::Lost) => {
                set_error(
                    error_buf,
                    error_buf_len,
                    "Surface acquire failed persistently (surface lost)",
                );
                return CoinWgpuStatus::SurfaceLost;
            }
        };

        // Dedicated synchronous diagnostic. Ordinary window timings allocate
        // no queries/readback and never wait for this sampling audit.
        let audit_probe = (trace_phases && std::env::var_os("COIN_SAMPLING_AUDIT").is_some()
            && std::env::var("COIN_WGPU_GPU_TIMESTAMPS").as_deref()==Ok("1")
            && dev.device.features().contains(wgpu::Features::TIMESTAMP_QUERY | wgpu::Features::TIMESTAMP_QUERY_INSIDE_ENCODERS))
            .then(|| GpuTimestampProbe::new(&dev.device));
        let profile_acquired = trace_phases.then(std::time::Instant::now);
        // 9. Encode frame into acquired texture view
        let color_view = surface_texture
            .texture
            .create_view(&wgpu::TextureViewDescriptor::default());
        // Capture the same image that is presented even when WSI lacks COPY_SRC.
        // MSAA already has a private resolved image; ordinary 1x creates one
        // only for this requested readback, preserving the surface scene format.
        let capture_presentation = if record.presentation.is_none() && (f.multipass_count > 1 ||
            (!readback_rgba.is_null() && !record.config.as_ref().unwrap().usage.contains(wgpu::TextureUsages::COPY_SRC))) {
            Some(surface_present::Presentation::with_source_format(&dev.device,
                f.width, f.height, record.color_format, record.color_format))
        } else { None };
        let presentation = record.presentation.as_ref().or(capture_presentation.as_ref());
        let resolved_view = presentation.map_or(&color_view, |p| &p.view);
        let capture_texture = presentation.map_or(&surface_texture.texture, |p| &p.texture);
        let capture_format = if record.presentation.is_some() {
            wgpu::TextureFormat::Rgba8Unorm
        } else { record.color_format };
        let scene_format = if record.presentation.is_some() {
            wgpu::TextureFormat::Rgba8Unorm
        } else { record.color_format };
        let depth_view = match &record.depth_view {
            Some(v) => v,
            None => {
                set_error(error_buf, error_buf_len, "Surface depth view not allocated");
                return CoinWgpuStatus::BackendError;
            }
        };

        if f.multipass_count > 1 && record.sample_count > 1 && fault == 307 {
            set_error(error_buf,error_buf_len,"Injected MSAA failure before multipass queue submission");
            drop(color_view); drop(surface_texture);
            record.surface.configure(&dev.device,record.config.as_ref().unwrap());
            return CoinWgpuStatus::OutOfMemory;
        }
        let encoded = encode_scene_samples(
            dev, f, [f.width,f.height], vertices_slice, indices_slice, draws_slice, &draw_order,
            materials_slice, states_slice, &portable_modules, textures_slice, samplers_slice,
            bump_payload.as_ref(), shadow_frame.as_ref(), next_scene.as_ref().map(|s| &s.geometry),
            instances_slice, instance_ranges, audit_probe.as_ref().map(|p| &p.queries),
            record.msaa_color_view.as_ref().unwrap_or(resolved_view), record.msaa_color_view.as_ref().map(|_| resolved_view), scene_format, depth_view, record.depth_texture.as_ref().unwrap(), record.sample_count);
        let (cmd_buffers, pending_instances) = match encoded {
            Ok(cmd) => cmd,
            Err((status, msg)) => {
                set_error(error_buf, error_buf_len, &msg);
                // Vulkan's discarded surface image is not returned to its swapchain.
                // Release every reference before recreating the same configuration,
                // so any rejected MSAA encoder (including shadow allocation
                // failures) cannot exhaust subsequent acquisitions. Known profile
                // rejections are checked before acquisition above.
                if record.sample_count > 1 {
                    drop(color_view);
                    drop(surface_texture);
                    record.surface.configure(&dev.device, record.config.as_ref().unwrap());
                }
                return status;
            }
        };

        if record.sample_count > 1 && fault == 307 {
            set_error(error_buf, error_buf_len,
                "Injected MSAA failure after encoding, before queue submission");
            drop(cmd_buffers);
            drop(color_view);
            drop(surface_texture);
            record.surface.configure(&dev.device, record.config.as_ref().unwrap());
            return CoinWgpuStatus::OutOfMemory;
        }

        let mut capture_buffer = None;
        let mut commands = cmd_buffers;
        if record.published_serial == 0 && manager_backgrounds(draws_slice) {
            commands.insert(0, initialize_target_depth(&dev.device, depth_view));
        }
        if let Some(presentation) = presentation {
            let target = surface_texture.texture.create_view(&wgpu::TextureViewDescriptor {
                format: Some(presentation.format), ..Default::default()
            });
            commands.push(presentation.encode(&dev.device, &target));
        }
        if !readback_rgba.is_null() {
            let bytes_per_row = (f.width * 4 + 255) & !255;
            let staging = dev.device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("Window RGBA capture"),
                size: u64::from(bytes_per_row) * u64::from(f.height),
                usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
                mapped_at_creation: false,
            });
            let mut copy = dev.device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("Window RGBA capture copy"),
            });
            copy.copy_texture_to_buffer(
                wgpu::TexelCopyTextureInfo {
                    texture: capture_texture,
                    mip_level: 0,
                    origin: wgpu::Origin3d::ZERO,
                    aspect: wgpu::TextureAspect::All,
                },
                wgpu::TexelCopyBufferInfo {
                    buffer: &staging,
                    layout: wgpu::TexelCopyBufferLayout {
                        offset: 0,
                        bytes_per_row: Some(bytes_per_row),
                        rows_per_image: Some(f.height),
                    },
                },
                wgpu::Extent3d { width: f.width, height: f.height, depth_or_array_layers: 1 },
            );
            commands.push(copy.finish());
            capture_buffer = Some((staging, bytes_per_row));
        }
        let profile_encoded = trace_phases.then(std::time::Instant::now);
        // 10. Submit and Present
        let sub_serial = GLOBAL_SUBMISSION_SERIAL.fetch_add(1, Ordering::SeqCst);
        LAST_SUBMITTED_SERIAL.store(sub_serial, Ordering::SeqCst);
        let local_serial = dev.last_submitted_serial.fetch_add(1, Ordering::SeqCst) + 1;
        if let Some(probe)=audit_probe.as_ref() {
            let mut resolve=dev.device.create_command_encoder(&wgpu::CommandEncoderDescriptor{label:Some("Coin sampling audit resolve")});
            resolve.resolve_query_set(&probe.queries,0..2,&probe.resolved,0);
            resolve.copy_buffer_to_buffer(&probe.resolved,0,&probe.readback,0,16);
            commands.push(resolve.finish());
        }
        let command_buffers_count=commands.len();
        if trace_wsi {
            eprintln!("COIN_WGPU_WSI stage=coin_submit event=begin source=actual surface={} generation={} serial={} local_serial={} previous_published_serial={}",
                surface_id, record.surface_generation, sub_serial, local_serial, record.published_serial);
        }
        dev.queue.submit(commands);
        let profile_submitted = trace_phases.then(std::time::Instant::now);
        let completed = dev.completed_serial.clone();
        dev.queue.on_submitted_work_done(move || {
            completed.fetch_max(local_serial, Ordering::SeqCst);
            GLOBAL_COMPLETED_SERIAL.fetch_max(sub_serial, Ordering::SeqCst);
        });
        let profile_notified = trace_phases.then(std::time::Instant::now);
        let mut captured_rgba = None;
        if let Some((staging, bytes_per_row)) = capture_buffer {
            let slice = staging.slice(..);
            let (sender, receiver) = std::sync::mpsc::channel();
            slice.map_async(wgpu::MapMode::Read, move |result| {
                let _ = sender.send(result);
            });
            let _ = dev.device.poll(wgpu::Maintain::Wait);
            if !matches!(receiver.try_recv(), Ok(Ok(()))) {
                set_error(error_buf, error_buf_len, "Window RGBA capture map failed");
                return CoinWgpuStatus::BackendError;
            }
            let mapped = slice.get_mapped_range();
            let mut pixels = vec![0u8; readback_len];
            let bgra = matches!(capture_format, wgpu::TextureFormat::Bgra8Unorm |
                wgpu::TextureFormat::Bgra8UnormSrgb);
            for y in 0..f.height as usize {
                let source = &mapped[y * bytes_per_row as usize..y * bytes_per_row as usize + f.width as usize * 4];
                let destination = &mut pixels[y * f.width as usize * 4..(y + 1) * f.width as usize * 4];
                if bgra {
                    for (dst, src) in destination.chunks_exact_mut(4).zip(source.chunks_exact(4)) {
                        dst.copy_from_slice(&[src[2], src[1], src[0], src[3]]);
                    }
                } else {
                    destination.copy_from_slice(source);
                }
            }
            drop(mapped);
            staging.unmap();
            captured_rgba = Some(pixels);
        }
        if let Some(probe)=audit_probe.as_ref() {
            let (sender,receiver)=std::sync::mpsc::channel();
            probe.readback.slice(..16).map_async(wgpu::MapMode::Read,move|r|{let _=sender.send(r);});
            let _=dev.device.poll(wgpu::Maintain::Wait);
            if matches!(receiver.try_recv(),Ok(Ok(()))) {
                let mapped=probe.readback.slice(..16).get_mapped_range();
                let a=u64::from_ne_bytes(mapped[..8].try_into().unwrap());
                let b=u64::from_ne_bytes(mapped[8..16].try_into().unwrap());
                if let Some(ticks)=b.checked_sub(a) {
                    eprintln!("COIN_SAMPLING_AUDIT_GPU status=ok render_ms={:.6} period_ns={:.6}",ticks as f64*dev.queue.get_timestamp_period() as f64/1_000_000.,dev.queue.get_timestamp_period());
                } else {eprintln!("COIN_SAMPLING_AUDIT_GPU status=invalid_ticks");}
                drop(mapped);probe.readback.unmap();
            } else {eprintln!("COIN_SAMPLING_AUDIT_GPU status=map_failed");}
        }
        let profile_present_begin = trace_phases.then(std::time::Instant::now);
        if trace_wsi {
            eprintln!("COIN_WGPU_WSI stage=coin_present event=begin source=actual surface={} generation={} serial={}",
                surface_id, record.surface_generation, sub_serial);
        }
        surface_texture.present();
        if trace_wsi {
            // This is a return from wgpu, not evidence of scanout/display completion.
            eprintln!("COIN_WGPU_WSI stage=coin_present event=returned source=actual surface={} generation={} serial={} display_completion=unknown",
                surface_id, record.surface_generation, sub_serial);
        }
        let profile_presented = trace_phases.then(std::time::Instant::now);

        // Check if device lost or async errors occurred during submit/present
        if DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst) {
            runtime.device_state = None;
            runtime.device_generation += 1;
            let msg = LAST_ASYNC_ERROR_MSG
                .lock()
                .map(|m| m.clone())
                .unwrap_or_else(|_| "Device lost during present".to_string());
            set_error(error_buf, error_buf_len, &msg);
            return CoinWgpuStatus::DeviceLost;
        }

        let error_kind = LAST_ASYNC_ERROR_KIND.swap(0, Ordering::SeqCst);
        if error_kind == 1 {
            let msg = LAST_ASYNC_ERROR_MSG
                .lock()
                .map(|m| m.clone())
                .unwrap_or_else(|_| "Out of memory".to_string());
            set_error(error_buf, error_buf_len, &msg);
            return CoinWgpuStatus::OutOfMemory;
        } else if error_kind == 2 {
            let msg = LAST_ASYNC_ERROR_MSG
                .lock()
                .map(|m| m.clone())
                .unwrap_or_else(|_| "Backend validation error".to_string());
            set_error(error_buf, error_buf_len, &msg);
            return CoinWgpuStatus::BackendError;
        }

        commit_camera_scene(dev, next_scene, true, f.frame_revision, owned_patch.is_some(), capture_scene,
            !instances_slice.is_empty(), pending_instances);

        if let Some(pixels) = captured_rgba {
            unsafe { std::ptr::copy_nonoverlapping(pixels.as_ptr(), readback_rgba, pixels.len()); }
        }

        if trace_phases {
            let duration_ms = |start: std::time::Instant, end: std::time::Instant| {
                (end - start).as_secs_f64() * 1000.0
            };
            let (start, validated, acquired, encoded, presented) =
                (profile_start.unwrap(), profile_validated.unwrap(),
                 profile_acquired.unwrap(), profile_encoded.unwrap(),
                 profile_presented.unwrap());
            let capture_bytes = if readback_rgba.is_null() { 0 } else {
                u64::from((f.width * 4 + 255) & !255) * u64::from(f.height)
            };
            eprintln!("COIN_RENDER_PHASE rust_surface_cpu total_ms={:.6} validation_ms={:.6} acquire_ms={:.6} encode_ms={:.6} submit_present_ms={:.6} queue_submit_ms={:.6} completion_registration_ms={:.6} capture_readback_ms={:.6} present_ms={:.6} capture_requested={} capture_staging_bytes={} queue_submissions=1 command_buffers={} gpu_timing={}",
                duration_ms(start, presented), duration_ms(start, validated),
                duration_ms(validated, acquired), duration_ms(acquired, encoded),
                duration_ms(encoded, presented),
                duration_ms(encoded, profile_submitted.unwrap()),
                duration_ms(profile_submitted.unwrap(), profile_notified.unwrap()),
                duration_ms(profile_notified.unwrap(), profile_present_begin.unwrap()),
                duration_ms(profile_present_begin.unwrap(), presented),
                u8::from(!readback_rgba.is_null()),
                capture_bytes, command_buffers_count, if audit_probe.is_some() {"sampling_audit"} else {"unavailable"});
            trace_owned_resources(dev, "window", f.width, f.height, capture_bytes, 0);
            let info = dev.adapter.get_info();
            let renderer = match info.backend {
                wgpu::Backend::Vulkan => "vulkan",
                wgpu::Backend::Gl => "opengl",
                wgpu::Backend::Metal => "metal",
                wgpu::Backend::Dx12 => "dx12",
                _ => "other",
            };
            eprintln!("COIN_RENDER_PHASE wgpu_surface renderer={} vendor_id={:#x} device_id={:#x} device_type={:?} surface={} serial={} size={}x{} adapter_name={:?}",
                renderer, info.vendor, info.device, info.device_type,
                surface_id, sub_serial, f.width, f.height, info.name);
        }
        record.published_serial = sub_serial;
        CoinWgpuStatus::Ok
    });

    res.unwrap_or_else(|_| {
        set_error(
            error_buf,
            error_buf_len,
            "Rust bridge panic caught in coin_wgpu_surface_submit",
        );
        CoinWgpuStatus::BackendError
    })
}

// Per-surface publication, independent of other targets and rejected work.
#[no_mangle]
pub extern "C" fn coin_wgpu_surface_submission_serial(surface_id: CoinWgpuSurfaceId) -> u64 {
    RUNTIME_CTX.lock().ok().and_then(|guard| {
        guard.as_ref().and_then(|runtime| runtime.surfaces.get(&surface_id))
            .map(|record| record.published_serial)
    }).unwrap_or(0)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_surface_submit(
    surface_id: CoinWgpuSurfaceId,
    frame: *const CoinWgpuFrameView,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    coin_wgpu_surface_submit_internal(surface_id, frame, std::ptr::null_mut(), 0,
        error_buf, error_buf_len)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_surface_submit_readback(
    surface_id: CoinWgpuSurfaceId,
    frame: *const CoinWgpuFrameView,
    rgba: *mut u8,
    rgba_len: usize,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    if rgba.is_null() {
        set_error(error_buf, error_buf_len, "Window RGBA output pointer is null");
        return CoinWgpuStatus::InvalidArgument;
    }
    coin_wgpu_surface_submit_internal(surface_id, frame, rgba, rgba_len,
        error_buf, error_buf_len)
}

fn coin_wgpu_submit_internal(
    target: *mut CoinWgpuTarget,
    frame: *const CoinWgpuFrameView,
    out_ticket: *mut CoinWgpuReadbackTicket,
    out_texture: *mut u64,
    depth_history: *mut u64,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    let res = std::panic::catch_unwind(|| {
        let profile_start = std::time::Instant::now();
        let trace_phases = std::env::var_os("COIN_RENDER_TRACE_PHASES").or_else(|| std::env::var_os("COIN_WGPU_TRACE_PHASES")).is_some();
        // 1. Fault injection check
        let requested_device = if !target.is_null()
            && (target as usize) % std::mem::align_of::<CoinWgpuTarget>() == 0 {
            unsafe { (*target).device_id }
        } else { 0 };

        let delayed = requested_device == 0 && FAULT_AFTER_SUBMITS.fetch_update(
            Ordering::SeqCst, Ordering::SeqCst,
            |remaining| if remaining >= 0 { Some(remaining - 1) } else { None },
        ).ok() == Some(0);
        let fault = if requested_device != 0 { 0 } else if delayed {
            FAULT_AFTER_CODE.swap(0, Ordering::SeqCst)
        } else {
            FAULT_INJECTION.load(Ordering::SeqCst)
        };
        if fault == CoinWgpuStatus::NotReady as i32 {
            set_error(error_buf, error_buf_len, "Injected NOT_READY fault");
            return CoinWgpuStatus::NotReady;
        } else if fault == CoinWgpuStatus::OutOfMemory as i32 {
            set_error(error_buf, error_buf_len, "Injected OUT_OF_MEMORY fault");
            return CoinWgpuStatus::OutOfMemory;
        } else if fault == CoinWgpuStatus::DeviceLost as i32 {
            if let Ok(mut guard) = RUNTIME_CTX.lock() {
                if let Some(rt) = guard.as_mut() {
                    rt.device_state = None;
                    rt.device_generation += 1;
                }
            }
            DEVICE_LOST_OCCURRED.store(false, Ordering::SeqCst);
            set_error(error_buf, error_buf_len, "Injected DEVICE_LOST fault");
            return CoinWgpuStatus::DeviceLost;
        }

        // 2. Real device lost check
        if requested_device == 0 && DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst) {
            if let Ok(mut guard) = RUNTIME_CTX.lock() {
                if let Some(rt) = guard.as_mut() {
                    rt.device_state = None;
                    rt.device_generation += 1;
                }
            }
            let msg = LAST_ASYNC_ERROR_MSG
                .lock()
                .map(|m| m.clone())
                .unwrap_or_else(|_| "Device lost".to_string());
            set_error(error_buf, error_buf_len, &msg);
            return CoinWgpuStatus::DeviceLost;
        }

        if target.is_null() || frame.is_null() {
            set_error(error_buf, error_buf_len, "Null target or frame pointer");
            return CoinWgpuStatus::InvalidArgument;
        }
        if (target as usize) % std::mem::align_of::<CoinWgpuTarget>() != 0 {
            set_error(error_buf, error_buf_len, "Misaligned target pointer");
            return CoinWgpuStatus::InvalidArgument;
        }
        if (frame as usize) % std::mem::align_of::<CoinWgpuFrameView>() != 0 {
            set_error(error_buf, error_buf_len, "Misaligned frame pointer");
            return CoinWgpuStatus::InvalidArgument;
        }

        if !out_ticket.is_null() {
            if (out_ticket as usize) % std::mem::align_of::<CoinWgpuReadbackTicket>() != 0 {
                set_error(error_buf, error_buf_len, "Misaligned readback ticket pointer");
                return CoinWgpuStatus::InvalidArgument;
            }
            let ticket = unsafe { &mut *out_ticket };
            if ticket.abi_version != COIN_WGPU_ABI_VERSION
                || ticket.struct_size as usize != std::mem::size_of::<CoinWgpuReadbackTicket>() {
                set_error(error_buf, error_buf_len, "Readback ticket ABI version or size mismatch");
                return CoinWgpuStatus::InvalidArgument;
            }
        }
        if !out_texture.is_null() {
            if !out_ticket.is_null() || (out_texture as usize) % std::mem::align_of::<u64>() != 0 {
                set_error(error_buf, error_buf_len, "Invalid RTT output token pointer");
                return CoinWgpuStatus::InvalidArgument;
            }
            unsafe { *out_texture = 0; }
        }
        let tgt = unsafe { &mut *target };
        if tgt.device_id != 0 {
            let (fault, lost, error_kind) = {
                let guard = RUNTIME_CTX.lock().unwrap();
                let Some(runtime) = guard.as_ref() else {
                    set_error(error_buf, error_buf_len, "Unknown WebGPU device");
                    return CoinWgpuStatus::InvalidArgument;
                };
                if !runtime.extra_generations.contains_key(&tgt.device_id) {
                    set_error(error_buf, error_buf_len, "Unknown or destroyed WebGPU device");
                    return CoinWgpuStatus::InvalidArgument;
                }
                if let Some(device) = runtime.extra_devices.get(&tgt.device_id) {
                    (device.injected_fault.swap(0, Ordering::SeqCst),
                     device.lost.load(Ordering::SeqCst),
                     device.async_error.swap(0, Ordering::SeqCst))
                } else { (0, false, 0) }
            };
            if lost || fault == CoinWgpuStatus::DeviceLost as i32 {
                if let Ok(mut guard) = RUNTIME_CTX.lock() {
                    if let Some(runtime) = guard.as_mut() {
                        lose_extra_device(runtime, tgt.device_id);
                    }
                }
                set_error(error_buf, error_buf_len, "WebGPU device lost");
                return CoinWgpuStatus::DeviceLost;
            }
            if fault == CoinWgpuStatus::OutOfMemory as i32 || error_kind == 1 {
                set_error(error_buf, error_buf_len, "WebGPU device out of memory");
                return CoinWgpuStatus::OutOfMemory;
            }
            if fault == CoinWgpuStatus::BackendError as i32 || error_kind == 2 {
                set_error(error_buf, error_buf_len, "WebGPU device validation error");
                return CoinWgpuStatus::BackendError;
            }
            if fault == CoinWgpuStatus::NotReady as i32 {
                set_error(error_buf, error_buf_len, "WebGPU device not ready");
                return CoinWgpuStatus::NotReady;
            }
        }
        let f = unsafe { &*frame };

        // 3. ABI version and struct size validation
        if f.abi_version != COIN_WGPU_ABI_VERSION {
            set_error(
                error_buf,
                error_buf_len,
                &format!(
                    "ABI version mismatch: expected {}, got {}",
                    COIN_WGPU_ABI_VERSION, f.abi_version
                ),
            );
            return CoinWgpuStatus::InvalidArgument;
        }
        if f.struct_size as usize != std::mem::size_of::<CoinWgpuFrameView>() {
            set_error(
                error_buf,
                error_buf_len,
                &format!(
                    "Struct size mismatch: expected {}, got {}",
                    std::mem::size_of::<CoinWgpuFrameView>(),
                    f.struct_size
                ),
            );
            return CoinWgpuStatus::InvalidArgument;
        }

        if let Err(status)=validate_multipass_metadata(f,error_buf,error_buf_len) {return status;}
        let portable_modules = match portable::modules(f) {
            Ok(modules) => modules,
            Err((status,message)) => { set_error(error_buf,error_buf_len,&message); return status; }
        };
        if !portable_modules.is_empty() && (f.instance_count != 0 || f.instance_range_count != 0) {
            set_error(error_buf,error_buf_len,"Portable shader modules require ordinary non-instanced vertex delivery");
            return CoinWgpuStatus::Unsupported;
        }
        let bump_payload = match validate_bump_payload(f, error_buf, error_buf_len) {
            Ok(payload) => payload,
            Err(status) => return status,
        };
        if bump_payload.is_some() && (!out_ticket.is_null() ||
            (!out_texture.is_null() && f.texture_output_flags != 0)) {
            set_error(error_buf, error_buf_len,
                "Bump frame needs synchronous RGBA8 offscreen or direct RTT output in this profile");
            return CoinWgpuStatus::Unsupported;
        }
        // Zero is the default of existing zero-initialized private FFI pods.
        let sample_count = if tgt.sample_count == 0 { 1 } else { tgt.sample_count };
        if !matches!(sample_count, 1 | 2 | 4) {
            set_error(error_buf, error_buf_len, "MSAA sample count must be 1, 2 or 4");
            return CoinWgpuStatus::InvalidArgument;
        }

        // 4. Dimensions and buffer boundary validation
        if f.width == 0 || f.height == 0 || tgt.width == 0 || tgt.height == 0 {
            set_error(
                error_buf,
                error_buf_len,
                "Target or frame dimensions must be > 0",
            );
            return CoinWgpuStatus::InvalidArgument;
        }
        if f.width != tgt.width || f.height != tgt.height {
            set_error(
                error_buf,
                error_buf_len,
                &format!(
                    "Dimension mismatch: frame ({}x{}) != target ({}x{})",
                    f.width, f.height, tgt.width, tgt.height
                ),
            );
            return CoinWgpuStatus::InvalidArgument;
        }

        let width = f.width;
        let height = f.height;

        let unaligned_bytes = match (width as usize).checked_mul(4) {
            Some(b) => b,
            None => {
                set_error(
                    error_buf,
                    error_buf_len,
                    "Width causes overflow in byte calculation",
                );
                return CoinWgpuStatus::InvalidArgument;
            }
        };
        let bytes_per_row = match unaligned_bytes.checked_add(255)
            .map(|bytes| bytes & !255)
            .and_then(|bytes| u32::try_from(bytes).ok()) {
            Some(bytes) => bytes,
            None => {
                set_error(error_buf, error_buf_len, "Aligned color row pitch overflows u32");
                return CoinWgpuStatus::InvalidArgument;
            }
        };
        let required_target_len = match (unaligned_bytes as u64).checked_mul(height as u64) {
            Some(len) => len,
            None => {
                set_error(
                    error_buf,
                    error_buf_len,
                    "Dimensions cause overflow in target buffer length",
                );
                return CoinWgpuStatus::InvalidArgument;
            }
        };

        if out_ticket.is_null() && out_texture.is_null()
            && (tgt.color_buffer.is_null() || tgt.color_buffer_len < required_target_len) {
            set_error(
                error_buf,
                error_buf_len,
                &format!(
                    "Target color buffer too small: required {}, got {}",
                    required_target_len, tgt.color_buffer_len
                ),
            );
            return CoinWgpuStatus::InvalidArgument;
        }

        // 5. Validate the changed states before considering a camera hint.
        // All immutable pointers are ignored only if this device still owns
        // the validated base. A stale/mismatched hint takes the old full path.
        if let Err((status, message)) = instancing::validate_counts(f) {
            set_error(error_buf, error_buf_len, &message); return status;
        }
        let states_slice =
            match validate_slice(f.states, f.state_count, "states", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            };

        if !states_slice.iter().all(valid_alpha_test) {
            set_error(error_buf, error_buf_len, "Invalid alpha comparison function or reference");
            return CoinWgpuStatus::InvalidArgument;
        }

        let owned_patch = if out_texture.is_null() && f.frame_revision != 0
            && f.texture_count == 0 && f.sampler_count == 0 {
            RUNTIME_CTX.lock().ok().and_then(|guard| {
                let runtime = guard.as_ref()?;
                let device = if tgt.device_id == 0 {
                    runtime.device_state.as_ref()
                } else {
                    runtime.extra_devices.get(&tgt.device_id)
                }?;
                camera_base(device, f, states_slice)
            })
        } else { None };

        let vertices_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.vertices.as_slice()
        } else {
            match validate_slice(f.vertices, f.vertex_count, "vertices", error_buf, error_buf_len) {
                Ok(s) => s, Err(st) => return st,
            }
        };
        let indices_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.indices.as_slice()
        } else {
            match validate_slice(f.indices, f.index_count, "indices", error_buf, error_buf_len) {
                Ok(s) => s, Err(st) => return st,
            }
        };
        let draws_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.draws.as_slice()
        } else {
            match validate_slice(f.draws, f.draw_count, "draws", error_buf, error_buf_len) {
                Ok(s) => s, Err(st) => return st,
            }
        };
        if let Err(status)=validate_multipass_draws(f,draws_slice,error_buf,error_buf_len) {return status;}
        let materials_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.materials.as_slice()
        } else {
            match validate_slice(f.materials, f.material_count, "materials", error_buf, error_buf_len) {
                Ok(s) => s, Err(st) => return st,
            }
        };

        let instances_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.instances.as_slice()
        } else { match validate_slice(f.instances, f.instance_count, "instances", error_buf, error_buf_len) {
            Ok(s) => s, Err(st) => return st,
        } };
        let instance_ranges = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.instance_ranges.as_slice()
        } else { match validate_slice(f.instance_ranges, f.instance_range_count, "instance ranges", error_buf, error_buf_len) {
            Ok(s) => s, Err(st) => return st,
        } };
        if owned_patch.is_none() {
            if let Err((status, message)) = instancing::validate_payload(f, vertices_slice, indices_slice,
                draws_slice, materials_slice, states_slice, instances_slice, instance_ranges) {
                set_error(error_buf, error_buf_len, &message); return status;
            }
        }

        if !instances_slice.is_empty() && !out_texture.is_null() {
            set_error(error_buf, error_buf_len, "Instanced frames do not support render-to-texture");
            return CoinWgpuStatus::Unsupported;
        }

        let textures_slice = if owned_patch.is_some() { &[] } else if f.texture_count > 0 {
            match validate_slice(f.textures, f.texture_count, "textures", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            }
        } else {
            &[]
        };

        if let Err(message) = validate_texture_declarations(textures_slice) {
            set_error(error_buf, error_buf_len, &message);
            return CoinWgpuStatus::InvalidArgument;
        }

        let samplers_slice = if owned_patch.is_some() { &[] } else if f.sampler_count > 0 {
            match validate_slice(f.samplers, f.sampler_count, "samplers", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            }
        } else {
            &[]
        };

        if owned_patch.is_none() {
            if let Err((status,message)) = portable::validate_bump_occurrences(states_slice,draws_slice,indices_slice,
                bump_payload.as_ref().map_or(&[],|payload|payload.vertices.as_slice())) {
                set_error(error_buf,error_buf_len,&message);return status;
            }
            if let Err((status,message)) = portable::validate_draws(&portable_modules,vertices_slice,indices_slice,
                draws_slice,materials_slice,states_slice,textures_slice) {
                set_error(error_buf,error_buf_len,&message);return status;
            }
            if let Err((status,message)) = validate_cube_resources(states_slice,textures_slice,samplers_slice)
                .and_then(|()|validate_cube_draws(states_slice,textures_slice,vertices_slice,indices_slice,draws_slice))
            .and_then(|()|raster_image::validate_with_images(states_slice,textures_slice,samplers_slice.len())) {
                set_error(error_buf,error_buf_len,&message);return status;
            }
        }

        // Shadow payload is optional, but a partial payload is never accepted.
        // Keep it out of the camera patch until state/geometry matching is qualified.
        // A nonzero map size identifies a pass even when Core selected no
        // casters. Such a pass still clears its moments and lights receivers.
        let shadow_frame = match validate_shadow_frame(f, states_slice.len(),
            owned_patch.is_some(), error_buf, error_buf_len) {
            Ok(shadow) => shadow,
            Err(status) => return status,
        };

        if let Some(payload) = bump_payload.as_ref() {
            if let Err((status, message)) = validate_bump_lighting_occurrences(payload,
                vertices_slice, indices_slice, draws_slice, states_slice, materials_slice,
                textures_slice, shadow_frame.as_ref()) {
                set_error(error_buf, error_buf_len, &message);
                return status;
            }
        }

        // 6. Strict validation and composition are immutable for one private CoinRenderFramePlan revision.
        // A camera hint without an owned base readmits a new snapshot only
        // after this submission validates the full geometry, even if another
        // device previously validated the same private frame revision.
        let cached_draw_order = if portable_modules.is_empty() && instances_slice.is_empty() && owned_patch.is_none() && f.camera_base_revision == 0
            && f.frame_revision != 0 {
            VALIDATED_FRAME_CACHE.lock().ok().and_then(|cache| {
                cache.as_ref().and_then(|(revision, order)| {
                    if *revision == f.frame_revision && order.len() == draws_slice.len() &&
                        order.iter().all(|item| item.draw_index < draws_slice.len()) {
                        Some(order.clone())
                    } else { None }
                })
            })
        } else {
            None
        };
        let mut fully_validated = false;
        let draw_order = if let Some(scene) = owned_patch.as_ref() {
            scene.draw_order.clone()
        } else if let Some(order) = cached_draw_order {
            order
        } else {
        for (i, draw) in draws_slice.iter().enumerate() {
            let idx_end = match draw.first_index.checked_add(draw.index_count) {
                Some(end) => end as usize,
                None => {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Draw {}: first_index + index_count overflow", i),
                    );
                    return CoinWgpuStatus::InvalidArgument;
                }
            };
            if idx_end > indices_slice.len() {
                set_error(
                    error_buf,
                    error_buf_len,
                    &format!("Draw {}: index range exceeds indices length", i),
                );
                return CoinWgpuStatus::InvalidArgument;
            }
            let vtx_end = match draw.first_vertex.checked_add(draw.vertex_count) {
                Some(end) => end as usize,
                None => {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Draw {}: first_vertex + vertex_count overflow", i),
                    );
                    return CoinWgpuStatus::InvalidArgument;
                }
            };
            if vtx_end > vertices_slice.len() {
                set_error(
                    error_buf,
                    error_buf_len,
                    &format!("Draw {}: vertex range exceeds vertices length", i),
                );
                return CoinWgpuStatus::InvalidArgument;
            }
            for idx in (draw.first_index as usize)..idx_end {
                let v_idx = indices_slice[idx] as usize;
                if v_idx >= vertices_slice.len() {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Draw {}: index {} references vertex out of bounds", i, idx),
                    );
                    return CoinWgpuStatus::InvalidArgument;
                }
            }
            if (draw.render_state_slot as usize) >= states_slice.len() {
                set_error(
                    error_buf,
                    error_buf_len,
                    &format!("Draw {}: render_state_slot out of bounds", i),
                );
                return CoinWgpuStatus::InvalidArgument;
            }
            let st = &states_slice[draw.render_state_slot as usize];
            if (st.material_slot as usize) >= materials_slice.len() {
                set_error(
                    error_buf,
                    error_buf_len,
                    &format!("Draw {}: material_slot out of bounds", i),
                );
                return CoinWgpuStatus::InvalidArgument;
            }
            if !valid_texture_payload(st) {
                set_error(error_buf, error_buf_len, "Invalid normalized texture program or extra unit state");
                return CoinWgpuStatus::InvalidArgument;
            }
            if let Err(message) = validate_texture_coordinates_with_images(st, draw.topology, vertices_slice,
                &indices_slice[draw.first_index as usize..idx_end], textures_slice) {
                set_error(error_buf, error_buf_len, &format!("Draw {i}: {message}"));
                return CoinWgpuStatus::InvalidArgument;
            }
            for layer in &st.extra_textures {
                if layer.enabled != 0 && ((layer.texture_slot as usize) >= textures_slice.len() ||
                    (layer.sampler_slot as usize) >= samplers_slice.len()) {
                    set_error(error_buf, error_buf_len, "Extra texture or sampler out of bounds");
                    return CoinWgpuStatus::InvalidArgument;
                }
            }
            if st.has_texture != 0 {
                if (st.texture_slot as usize) >= textures_slice.len() {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Draw {}: texture_slot out of bounds", i),
                    );
                    return CoinWgpuStatus::InvalidArgument;
                }
                if (st.sampler_slot as usize) >= samplers_slice.len() {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Draw {}: sampler_slot out of bounds", i),
                    );
                    return CoinWgpuStatus::InvalidArgument;
                }
            }
            if st.cull_mode > 2 {
                set_error(
                    error_buf,
                    error_buf_len,
                    &format!("Draw {}: invalid cull_mode", i),
                );
                return CoinWgpuStatus::InvalidArgument;
            }
            if st.front_face > 1 {
                set_error(
                    error_buf,
                    error_buf_len,
                    &format!("Draw {}: invalid front_face", i),
                );
                return CoinWgpuStatus::InvalidArgument;
            }
        }

        // Reject unsupported composition before allocating offscreen attachments.
        let computed_draw_order = match composition::order(
            vertices_slice, indices_slice, draws_slice, materials_slice, states_slice, textures_slice) {
            Ok(order) => order,
            Err((status, message)) => {
                set_error(error_buf, error_buf_len, &message);
                return status;
            }
        };
        if portable_modules.is_empty() && instances_slice.is_empty() && f.frame_revision != 0 {
            if let Ok(mut cache) = VALIDATED_FRAME_CACHE.lock() {
                *cache = Some((f.frame_revision, computed_draw_order.clone()));
            }
        }
        fully_validated = true;
        computed_draw_order
        };

        if let Err((status, message)) = peeling::validate_request_samples(
            if draw_order.iter().any(|item| item.peel) { f.sorted_layers_passes } else { 1 },
            f.transparency_reserved,
            f.transparency_budget_bytes,
            f.width,
            f.height,
            draw_order.iter().any(|item| item.peel || item.weighted),
            sample_count,
        ) {
            set_error(error_buf, error_buf_len, &message);
            return status;
        }

        if draw_order.iter().any(|item| item.weighted) {
            if let Err((status, message)) = weighted::validate_budget(f.width, f.height, sample_count, f.transparency_budget_bytes) {
                set_error(error_buf, error_buf_len, &message);
                return status;
            }
        }

        if sample_count > 1 &&
            draw_order.iter().any(|item| bump_payload.is_some() && item.blend) {
            set_error(error_buf, error_buf_len,
                "MSAA translucent bump needs a separate profile");
            return CoinWgpuStatus::Unsupported;
        }

        let pending_multipass=RUNTIME_CTX.lock().ok().and_then(|runtime|multipass_pending_bytes(runtime.as_ref()));
        let multipass_depth=tgt.depth_buffer_len>=u64::from(f.width)*u64::from(f.height) &&
            (!out_ticket.is_null() || !tgt.depth_buffer.is_null());
        if let Err((status,message))=admit_multipass_resources(f,sample_count,states_slice,textures_slice,
            &draw_order,shadow_frame.as_ref(),false,out_texture.is_null(),multipass_depth,pending_multipass) {
            set_error(error_buf,error_buf_len,&message);return status;
        }

        // 7. Initialize WebGPU Device
        if let Err(e) = init_runtime_if_needed() {
            set_error(error_buf, error_buf_len, &e);
            return CoinWgpuStatus::NotReady;
        }

        let mut guard = match RUNTIME_CTX.lock() {
            Ok(g) => g,
            Err(e) => {
                set_error(error_buf, error_buf_len, &e.to_string());
                return CoinWgpuStatus::BackendError;
            }
        };
        let runtime = guard.as_mut().unwrap();
        if !out_ticket.is_null() {
            reap_cancelled_readbacks(runtime);
            // Tickets reserve their packed public payload, independently of
            // sample attachments and the backend's aligned staging rows.
            // This is the same finite reservation used by C++ before RTT
            // producers run, and also protects direct FFI submissions.
            let depth = tgt.depth_buffer_len >= u64::from(width) * u64::from(height);
            let requested = required_target_len.checked_mul(if depth { 2 } else { 1 });
            let pending = runtime.pending_readbacks.values()
                .chain(runtime.retired_readbacks.iter()).try_fold(0u64, |bytes, job|
                    bytes.checked_add(job.ticket.color_bytes)?.checked_add(job.ticket.depth_bytes));
            if runtime.pending_readbacks.len() + runtime.retired_readbacks.len() >= 16
                || requested.zip(pending).is_none_or(|(requested, pending)|
                    requested > 128 * 1024 * 1024 || pending > 128 * 1024 * 1024 - requested) {
                set_error(error_buf, error_buf_len, "Readback budget exhausted: sixteen tickets or 128 MiB of pending output");
                return CoinWgpuStatus::NotReady;
            }
        }

        let device_state = match offscreen_device(runtime, tgt.device_id) {
            Ok(d) => d as *mut DeviceState,
            Err(e) => {
                set_error(error_buf, error_buf_len, &e);
                return CoinWgpuStatus::NotReady;
            }
        };
        let ctx = unsafe { &mut *device_state };
        if bump_payload.is_some() && (f.rasterization_policy != 0 ||
            !instances_slice.is_empty() || !instance_ranges.is_empty()) {
            set_error(error_buf, error_buf_len, "Bump requires native non-instanced geometry");
            return CoinWgpuStatus::Unsupported;
        }

        if let Some(payload) = bump_payload.as_ref() {
            if extended_bump_profile(states_slice, textures_slice, draws_slice, shadow_frame.as_ref(), payload, indices_slice) {
                if let Err((status, message)) = validate_extended_bump_profile(ctx,
                    vertices_slice, indices_slice, draws_slice, states_slice,
                    materials_slice, textures_slice, samplers_slice, payload,
                    shadow_frame.as_ref(), [f.width, f.height]) {
                    set_error(error_buf, error_buf_len, &message);
                    return status;
                }
            }
        }

        if let Err((status,message))=admit_spatial_resources(ctx,textures_slice)
            .and_then(|()|raster_image::admit(ctx,states_slice,textures_slice,samplers_slice.len())) {
            set_error(error_buf,error_buf_len,&message); return status;
        }
        if let Some(scene) = owned_patch.as_ref() {
            if scene.generation != ctx.generation {
                set_error(error_buf, error_buf_len, "Camera base belongs to a lost device generation");
                return CoinWgpuStatus::DeviceLost;
            }
            // Another submit may have replaced the device's base while the
            // incoming state was checked. Never encode against a stale GPU
            // geometry cache, even though the old CPU snapshot is still alive.
            if !ctx.validated_scene.as_ref().is_some_and(|current| Arc::ptr_eq(current, scene)) {
                set_error(error_buf, error_buf_len, "Camera base is no longer current on this device");
                return CoinWgpuStatus::NotReady;
            }
        }
        let profile_validated = std::time::Instant::now();
        let gpu_profile_requested = trace_phases
            && std::env::var("COIN_WGPU_GPU_TIMESTAMPS").as_deref() == Ok("1")
            && out_ticket.is_null() && out_texture.is_null();
        let gpu_profile_features = wgpu::Features::TIMESTAMP_QUERY
            | wgpu::Features::TIMESTAMP_QUERY_INSIDE_ENCODERS;
        let gpu_probe = (gpu_profile_requested
            && ctx.device.features().contains(gpu_profile_features))
            .then(|| GpuTimestampProbe::new(&ctx.device));
        let profile_probe_ready = trace_phases.then(std::time::Instant::now);

        if !out_texture.is_null() && ctx.rtt_textures.lock().unwrap().active.len() >= 64 {
            set_error(error_buf, error_buf_len, "Too many active RTT textures");
            return CoinWgpuStatus::OutOfMemory;
        }
        if f.texture_sampling_policy>1 || f.texture_output_flags&!3!=0 ||
           (f.texture_output_flags!=0 && out_texture.is_null()) {
            set_error(error_buf,error_buf_len,"Advanced output flags require a direct RTT destination");
            return CoinWgpuStatus::InvalidArgument;
        }
        if f.clear_color.iter().any(|v| !v.is_finite() || (f.texture_output_flags&1!=0 && v.abs()>65504.0)) {
            set_error(error_buf,error_buf_len,"Invalid RTT clear color or half range");
            return CoinWgpuStatus::InvalidArgument;
        }
        if draw_order.iter().any(|item| item.peel) && !peeling_supported(ctx, sample_count) {
            set_error(error_buf, error_buf_len, "Peeling formats cannot sample/render at the requested count");
            return CoinWgpuStatus::Unsupported;
        }
        if draw_order.iter().any(|item| item.weighted) &&
            !weighted_supported(ctx, sample_count) {
            set_error(error_buf, error_buf_len, "Weighted MSAA color formats cannot blend/sample at the requested count");
            return CoinWgpuStatus::Unsupported;
        }
        let output_format=if f.texture_output_flags&1!=0 {wgpu::TextureFormat::Rgba16Float} else {wgpu::TextureFormat::Rgba8Unorm};
        if let Err((status,message)) = portable::prepare(ctx,&portable_modules,states_slice,draws_slice,
            &draw_order,sample_count,f.texture_sampling_policy,f.rasterization_policy,
            shadow_frame.as_ref().map_or(0,|shadow|if shadow.extra.is_empty(){1}else{2}),output_format) {
            set_error(error_buf,error_buf_len,&message); return status;
        }
        let output_mips=if f.texture_output_flags&2!=0 {width.max(height).ilog2()+1} else {1};
        if sample_count > 1 {
            let required = if sample_count == 2 {
                wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X2
            } else {
                wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X4
            };
            if !ctx.adapter.get_texture_format_features(output_format).flags.contains(required) ||
                !ctx.adapter.get_texture_format_features(wgpu::TextureFormat::Depth32Float).flags.contains(required) ||
                (sample_count == 2 && !ctx.device.features().contains(
                    wgpu::Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES)) {
                set_error(error_buf, error_buf_len, "Exact MSAA sample count unavailable for color/depth on this device");
                return CoinWgpuStatus::Unsupported;
            }
        }
        if !out_texture.is_null() &&
           (width > ctx.device.limits().max_texture_dimension_2d ||
            height > ctx.device.limits().max_texture_dimension_2d) {
            set_error(error_buf,error_buf_len,"Direct RTT exceeds the adapter texture dimension");
            return CoinWgpuStatus::Unsupported;
        }
        if !ctx.adapter.get_texture_format_features(output_format).allowed_usages.contains(
            wgpu::TextureUsages::RENDER_ATTACHMENT|wgpu::TextureUsages::TEXTURE_BINDING|
              if output_mips>1 {wgpu::TextureUsages::STORAGE_BINDING} else {wgpu::TextureUsages::empty()}) {
            set_error(error_buf,error_buf_len,"Direct RTT format/mip storage unavailable");
            return CoinWgpuStatus::Unsupported;
        }
        let depth_requested = tgt.depth_buffer_len >= (width as u64) * (height as u64)
            && (!out_ticket.is_null() || !tgt.depth_buffer.is_null());
        // 8. A completed synchronous frame may lend its attachments to the
        // next synchronous frame on this device. Async and RTT submissions
        // are excluded: their textures can still be in use by the GPU.
        let attachment_cache_enabled = sample_count == 1 && out_ticket.is_null() && out_texture.is_null()
            && std::env::var("COIN_WGPU_ATTACHMENT_CACHE").as_deref() == Ok("1")
            && (width as u64).checked_mul(height as u64)
                .and_then(|pixels| pixels.checked_mul(8))
                .is_some_and(|bytes| bytes <= CachedOffscreenAttachments::MAX_BYTES);
        let cached = if sample_count == 1 && out_ticket.is_null() && out_texture.is_null() {
            ctx.cached_offscreen_attachments.take()
                .filter(|entry| attachment_cache_enabled
                    && entry.width == width && entry.height == height)
        } else { None };
        let prior_depth = if depth_history.is_null() { None } else {
            ctx.target_depth_histories.get(unsafe { &*depth_history })
                .filter(|entry| entry.width == width && entry.height == height && entry.samples == sample_count)
                .map(|entry| (entry.texture.clone(), entry.view.clone()))
        };
        let attachments_reused = cached.is_some();
        let (color_texture, color_view, cached_depth) = if let Some(entry) = cached {
            (entry.color_texture, entry.color_view, entry.depth)
        } else {
            let texture_desc = wgpu::TextureDescriptor {
                label: Some("Offscreen Color Texture"),
                size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
                mip_level_count: output_mips,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format: output_format,
                usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC | wgpu::TextureUsages::TEXTURE_BINDING | if output_mips>1 {wgpu::TextureUsages::STORAGE_BINDING} else {wgpu::TextureUsages::empty()},
                view_formats: &[],
            };
            if fault == FAULT_RTT_COLOR_ALLOC && !out_texture.is_null() {
                set_error(error_buf, error_buf_len, "Injected RTT color texture allocation failure");
                return CoinWgpuStatus::OutOfMemory;
            }
            let color_texture = ctx.device.create_texture(&texture_desc);
            if fault == FAULT_RTT_COLOR_VIEW && !out_texture.is_null() {
                set_error(error_buf, error_buf_len, "Injected RTT color view creation failure");
                return CoinWgpuStatus::OutOfMemory;
            }
            let color_view = color_texture.create_view(&wgpu::TextureViewDescriptor{mip_level_count:Some(1),..Default::default()});

            (color_texture, color_view, None)
        };
        let (depth_texture, depth_view) = if sample_count == 1 && prior_depth.is_some() {
            let (texture, view) = prior_depth.as_ref().unwrap();
            (texture.clone(), view.clone())
        } else if let Some(depth) = cached_depth {
            depth
        } else {
            let depth_desc = wgpu::TextureDescriptor {
                label: Some("Offscreen Depth Texture"),
                size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
                mip_level_count: 1,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format: wgpu::TextureFormat::Depth32Float,
                // With MSAA the render pass uses the separate multisampled
                // attachment below. Keep the 1x depth copy contract intact,
                // but do not require depth-buffer copies merely to allocate
                // the unused single-sample companion on GL adapters.
                usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING |
                    if sample_count == 1 { wgpu::TextureUsages::COPY_SRC }
                    else { wgpu::TextureUsages::empty() },
                view_formats: &[],
            };
            let depth_texture = ctx.device.create_texture(&depth_desc);
            if fault == FAULT_RTT_DEPTH_ALLOC && !out_texture.is_null() {
                set_error(error_buf, error_buf_len, "Injected RTT depth attachment failure");
                return CoinWgpuStatus::OutOfMemory;
            }
            let depth_view = depth_texture.create_view(&wgpu::TextureViewDescriptor::default());
            (depth_texture, depth_view)
        };
        let msaa_attachments = if sample_count > 1 {
            let extent = wgpu::Extent3d { width, height, depth_or_array_layers: 1 };
            let color = ctx.device.create_texture(&wgpu::TextureDescriptor {
                label: Some("Coin MSAA color"), size: extent, mip_level_count: 1,
                sample_count, dimension: wgpu::TextureDimension::D2,
                format: output_format, usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
                view_formats: &[],
            });
            let depth = if let Some((texture, _)) = prior_depth.as_ref() { texture.clone() }
              else { ctx.device.create_texture(&wgpu::TextureDescriptor {
                label: Some("Coin MSAA depth"), size: extent, mip_level_count: 1,
                sample_count, dimension: wgpu::TextureDimension::D2,
                format: wgpu::TextureFormat::Depth32Float,
                // Peeling samples opaque depth even for GPU-only RTT and
                // targets that do not request synchronous depth readback.
                usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING,
                view_formats: &[],
            }) };
            let color_view = color.create_view(&wgpu::TextureViewDescriptor::default());
            let depth_view = depth.create_view(&wgpu::TextureViewDescriptor::default());
            Some((color, color_view, depth, depth_view))
        } else { None };
        let profile_attachments_created = trace_phases.then(std::time::Instant::now);

        let capture_scene = !instances_slice.is_empty() || ctx.camera_snapshot_policy.capture(
            ctx.validated_scene.as_ref().map(|scene| scene.revision), f.frame_revision,
            f.camera_base_revision, owned_patch.is_some());
        let next_scene = if capture_scene && out_texture.is_null() && shadow_frame.is_none()
            && (owned_patch.is_some() || fully_validated) {
            camera_scene(ctx.generation, f, vertices_slice, indices_slice, draws_slice, materials_slice,
                states_slice, &draw_order, textures_slice, samplers_slice, instances_slice, instance_ranges, owned_patch.as_ref())
        } else { None };

        // 9. Encode draw calls with the exact same shared encode_frame
        if f.multipass_count > 1 && sample_count > 1 && fault == 307 {
            set_error(error_buf,error_buf_len,"Injected MSAA failure before multipass queue submission");
            return CoinWgpuStatus::OutOfMemory;
        }
        let encoded = encode_scene_samples(
            ctx, f, [width,height], vertices_slice, indices_slice, draws_slice, &draw_order,
            materials_slice, states_slice, &portable_modules, textures_slice, samplers_slice,
            bump_payload.as_ref(), shadow_frame.as_ref(), next_scene.as_ref().map(|s| &s.geometry),
            instances_slice, instance_ranges, gpu_probe.as_ref().map(|p| &p.queries),
            msaa_attachments.as_ref().map_or(&color_view, |a| &a.1), msaa_attachments.as_ref().map(|_| &color_view), output_format, msaa_attachments.as_ref().map_or(&depth_view, |a| &a.3), msaa_attachments.as_ref().map_or(&depth_texture, |a| &a.2), sample_count);
        let (mut cmd_buffers, pending_instances) = match encoded {
            Ok(cmd) => cmd,
            Err((status, msg)) => {
                set_error(error_buf, error_buf_len, &msg);
                return status;
            }
        };
        if prior_depth.is_none() && manager_backgrounds(draws_slice) {
            cmd_buffers.insert(0, initialize_target_depth(&ctx.device,
                msaa_attachments.as_ref().map_or(&depth_view, |a| &a.3)));
        }
        if sample_count > 1 && fault == 307 {
            set_error(error_buf, error_buf_len, "Injected MSAA failure after encoding, before queue submission");
            return CoinWgpuStatus::OutOfMemory;
        }
        let profile_draw_encoded = trace_phases.then(std::time::Instant::now);
        // Any successful full frame replaces the device's current scene even
        // when its revision is zero or snapshot admission is suspended.
        let commit_scene = true;
        let profile_encoded = std::time::Instant::now();

        if !out_texture.is_null() {
            let mut commands=cmd_buffers;
            if output_mips>1 {
                if fault==306 {set_error(error_buf,error_buf_len,"Injected RTT mip encoding failure");return CoinWgpuStatus::OutOfMemory;}
                match texture_mips::encode(&ctx.device,&color_texture,output_format,output_mips,width,height,ctx.adapter.get_info().backend==wgpu::Backend::Gl) {
                    Ok(command)=>commands.push(command),
                    Err((status,message))=>{set_error(error_buf,error_buf_len,&message);return status;}
                }
            }
            let token = NEXT_RTT_TOKEN.fetch_add(1, Ordering::SeqCst);
            if token == 0 {
                set_error(error_buf, error_buf_len, "RTT token space exhausted");
                return CoinWgpuStatus::OutOfMemory;
            }
            let serial = GLOBAL_SUBMISSION_SERIAL.fetch_add(1, Ordering::SeqCst);
            LAST_SUBMITTED_SERIAL.store(serial, Ordering::SeqCst);
            let local_serial = ctx.last_submitted_serial.fetch_add(1, Ordering::SeqCst) + 1;
            ctx.queue.submit(commands);
            let completed = ctx.completed_serial.clone();
            ctx.queue.on_submitted_work_done(move || {
                completed.fetch_max(local_serial, Ordering::SeqCst);
                GLOBAL_COMPLETED_SERIAL.fetch_max(serial, Ordering::SeqCst);
            });
            let sampling_view=color_texture.create_view(&wgpu::TextureViewDescriptor::default());
            ctx.rtt_textures.lock().unwrap().active.insert(token, RttTexture {
                texture: color_texture,
                // An opaque clear does not prove all later writes opaque:
                // legacy blend and NONE can both reduce destination alpha.
                // Captured Coin producers conservatively make no opaque claim.
                opaque: false,
                view: sampling_view,
                width,
                height,
                format: if f.texture_output_flags&1!=0 {2} else {0},
                mip_count:output_mips, cube_face_size:0, cube_mipmapped:false,
            });
            tgt.submission_serial = serial;
            unsafe { *out_texture = token; }
            commit_camera_scene(ctx, next_scene, commit_scene, f.frame_revision, owned_patch.is_some(), capture_scene,
                !instances_slice.is_empty(), pending_instances);
            return CoinWgpuStatus::Ok;
        }

        // 10. Copy to staging buffer for readback
        let staging_size = (bytes_per_row as u64) * (height as u64);
        let (staging_buffer, color_staging_reused) = ctx.readback_pool
            .lock().unwrap().acquire(&ctx.device, staging_size);

        let mut copy_encoder = ctx
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("Offscreen Copy Encoder"),
            });
        if let Some(probe) = gpu_probe.as_ref() {
            copy_encoder.write_timestamp(&probe.queries, 2);
        }

        copy_encoder.copy_texture_to_buffer(
            wgpu::TexelCopyTextureInfo {
                texture: &color_texture,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            wgpu::TexelCopyBufferInfo {
                buffer: &staging_buffer,
                layout: wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(bytes_per_row),
                    rows_per_image: Some(height),
                },
            },
            wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
        );

        let depth_unpadded = width * 4;
        let depth_bytes_per_row = (depth_unpadded + 255) & !255;
        let depth_staging_size = (depth_bytes_per_row as u64) * (height as u64);
        let depth_staging_info = if depth_requested
        {
            let (dbuf, _) = ctx.readback_pool
                .lock().unwrap().acquire(&ctx.device, depth_staging_size);
            let converted = if sample_count > 1 {
                let mut converter = ctx.depth_readback.lock().unwrap();
                let converter = converter.get_or_insert_with(|| {
                    if ctx.adapter.get_info().backend == wgpu::Backend::Gl {
                        depth_transfer::DepthTransfer::new_gl(&ctx.device, false)
                    } else {
                        depth_transfer::DepthTransfer::new(&ctx.device, false)
                    }
                });
                Some(converter.encode_msaa(&ctx.device, &mut copy_encoder,
                    &msaa_attachments.as_ref().unwrap().3, width, height, sample_count))
            } else if !ctx.adapter.get_downlevel_capabilities().flags
                .contains(wgpu::DownlevelFlags::DEPTH_TEXTURE_AND_BUFFER_COPIES) {
                let mut converter = ctx.depth_readback.lock().unwrap();
                let converter = converter.get_or_insert_with(|| {
                    if ctx.adapter.get_info().backend == wgpu::Backend::Gl {
                        depth_transfer::DepthTransfer::new_gl(&ctx.device, false)
                    } else {
                        depth_transfer::DepthTransfer::new(&ctx.device, false)
                    }
                });
                Some(converter.encode(&ctx.device, &mut copy_encoder, &depth_view, width, height))
            } else { None };
            copy_encoder.copy_texture_to_buffer(
                wgpu::TexelCopyTextureInfo {
                    texture: converted.as_ref().unwrap_or(&depth_texture),
                    mip_level: 0,
                    origin: wgpu::Origin3d::ZERO,
                    aspect: if converted.is_some() { wgpu::TextureAspect::All }
                        else { wgpu::TextureAspect::DepthOnly },
                },
                wgpu::TexelCopyBufferInfo {
                    buffer: &dbuf,
                    layout: wgpu::TexelCopyBufferLayout {
                        offset: 0,
                        bytes_per_row: Some(depth_bytes_per_row),
                        rows_per_image: Some(height),
                    },
                },
                wgpu::Extent3d {
                    width,
                    height,
                    depth_or_array_layers: 1,
                },
            );
            Some((dbuf, depth_bytes_per_row))
        } else {
            None
        };

        if let Some(probe) = gpu_probe.as_ref() {
            copy_encoder.write_timestamp(&probe.queries, 3);
            copy_encoder.resolve_query_set(&probe.queries, 0..4, &probe.resolved, 0);
            copy_encoder.copy_buffer_to_buffer(
                &probe.resolved, 0, &probe.readback, 0, GpuTimestampProbe::BYTES,
            );
        }

        let sub_serial = GLOBAL_SUBMISSION_SERIAL.fetch_add(1, Ordering::SeqCst);
        LAST_SUBMITTED_SERIAL.store(sub_serial, Ordering::SeqCst);
        let local_serial = ctx.last_submitted_serial.fetch_add(1, Ordering::SeqCst) + 1;
        let profile_submit_begin = std::time::Instant::now();
        let mut commands = cmd_buffers;
        commands.push(copy_encoder.finish());
        ctx.queue.submit(commands);
        let profile_submitted = std::time::Instant::now();
        let completed = ctx.completed_serial.clone();
        ctx.queue.on_submitted_work_done(move || {
            completed.fetch_max(local_serial, Ordering::SeqCst);
            GLOBAL_COMPLETED_SERIAL.fetch_max(sub_serial, Ordering::SeqCst);
        });

        let buffer_slice = staging_buffer.slice(..);
        let (sender, receiver) = std::sync::mpsc::channel();
        buffer_slice.map_async(wgpu::MapMode::Read, move |result| {
            let _ = sender.send(result);
        });

        let (depth_sender, depth_receiver) = std::sync::mpsc::channel();
        let depth_slice_holder = depth_staging_info.as_ref().map(|(buf, _)| buf.slice(..));
        if let Some(ref dslice) = depth_slice_holder {
            dslice.map_async(wgpu::MapMode::Read, move |result| {
                let _ = depth_sender.send(result);
            });
        }

        let gpu_timestamp_receiver = gpu_probe.as_ref().map(|probe| {
            let (sender, receiver) = std::sync::mpsc::channel();
            probe.readback.slice(..).map_async(wgpu::MapMode::Read, move |result| {
                let _ = sender.send(result);
            });
            receiver
        });

        if !out_ticket.is_null() {
            let token = NEXT_READBACK_TOKEN.fetch_add(1, Ordering::SeqCst);
            let ticket = CoinWgpuReadbackTicket {
                abi_version: COIN_WGPU_ABI_VERSION,
                struct_size: std::mem::size_of::<CoinWgpuReadbackTicket>() as u32,
                token,
                generation: ctx.generation,
                submission_serial: sub_serial,
                width,
                height,
                color_format: 0,
                depth_format: if depth_staging_info.is_some() { 1 } else { 0 },
                color_row_pitch: bytes_per_row,
                depth_row_pitch: if depth_staging_info.is_some() { depth_bytes_per_row } else { 0 },
                color_bytes: required_target_len,
                depth_bytes: if depth_staging_info.is_some() {
                    (width as u64) * (height as u64) * 4
                } else { 0 },
            };
            runtime.pending_readbacks.insert(token, PendingReadback {
                ticket,
                device_id: ctx.device_id,
                color: staging_buffer.clone(),
                color_receiver: receiver,
                color_ready: None,
                depth: depth_staging_info.as_ref().map(|(buffer, _)| buffer.clone()),
                depth_receiver: if depth_staging_info.is_some() { Some(depth_receiver) } else { None },
                depth_ready: None,
                pool: ctx.readback_pool.clone(),
            });
            unsafe { *out_ticket = ticket; }
            tgt.submission_serial = sub_serial;
            commit_camera_scene(ctx, next_scene, commit_scene, f.frame_revision, owned_patch.is_some(), capture_scene,
                !instances_slice.is_empty(), pending_instances);
            return CoinWgpuStatus::Ok;
        }

        let profile_wait_begin = std::time::Instant::now();
        let _ = ctx.device.poll(wgpu::Maintain::Wait);
        let profile_wait_done = std::time::Instant::now();
        if ctx.device_id != 0 {
            if ctx.lost.swap(false, Ordering::SeqCst) {
                lose_extra_device(runtime, tgt.device_id);
                set_error(error_buf, error_buf_len, "WebGPU device lost during submit");
                return CoinWgpuStatus::DeviceLost;
            }
            let kind = ctx.async_error.swap(0, Ordering::SeqCst);
            if kind != 0 {
                set_error(error_buf, error_buf_len, "WebGPU asynchronous device error");
                return if kind == 1 { CoinWgpuStatus::OutOfMemory }
                    else { CoinWgpuStatus::BackendError };
            }
        }


        // Inject async fault if requested
        let async_fault = if ctx.device_id == 0 { FAULT_INJECTION_ASYNC.swap(0, Ordering::SeqCst) } else { 0 };
        if async_fault == CoinWgpuStatus::OutOfMemory as i32 {
            set_error(error_buf, error_buf_len, "Injected async OUT_OF_MEMORY after GPU execution");
            return CoinWgpuStatus::OutOfMemory;
        }
        if async_fault == CoinWgpuStatus::DeviceLost as i32 {
            DEVICE_LOST_OCCURRED.store(true, Ordering::SeqCst);
            if let Ok(mut lock) = LAST_ASYNC_ERROR_MSG.lock() {
                *lock = "Injected async DEVICE_LOST fault during GPU execution".to_string();
            }
        }

        // Check if device lost occurred during submit or poll
        if ctx.device_id == 0 && DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst) {
            runtime.device_state = None;
            runtime.device_generation += 1;
            let msg = LAST_ASYNC_ERROR_MSG
                .lock()
                .map(|m| m.clone())
                .unwrap_or_else(|_| "Device lost during submit".to_string());
            set_error(error_buf, error_buf_len, &msg);
            return CoinWgpuStatus::DeviceLost;
        }

        // Check uncaptured errors
        let error_kind = if ctx.device_id == 0 { LAST_ASYNC_ERROR_KIND.swap(0, Ordering::SeqCst) } else { 0 };
        if error_kind == 1 {
            let msg = LAST_ASYNC_ERROR_MSG
                .lock()
                .map(|m| m.clone())
                .unwrap_or_else(|_| "Out of memory".to_string());
            set_error(error_buf, error_buf_len, &msg);
            return CoinWgpuStatus::OutOfMemory;
        } else if error_kind == 2 {
            let msg = LAST_ASYNC_ERROR_MSG
                .lock()
                .map(|m| m.clone())
                .unwrap_or_else(|_| "Backend validation error".to_string());
            set_error(error_buf, error_buf_len, &msg);
            return CoinWgpuStatus::BackendError;
        }

        let profile_gpu_read_begin = trace_phases.then(std::time::Instant::now);
        let timestamp_period_ns = gpu_probe.as_ref()
            .map(|_| ctx.queue.get_timestamp_period() as f64).unwrap_or(0.0);
        let mut gpu_timing_status = if gpu_profile_requested { "unsupported" } else { "disabled" };
        let mut gpu_timing_ms = None;
        if let (Some(probe), Some(timestamp_receiver)) =
            (gpu_probe.as_ref(), gpu_timestamp_receiver)
        {
            gpu_timing_status = "readback_error";
            if matches!(timestamp_receiver.recv(), Ok(Ok(()))) {
                let mapped = probe.readback.slice(..).get_mapped_range();
                if mapped.len() >= GpuTimestampProbe::BYTES as usize
                    && timestamp_period_ns.is_finite() && timestamp_period_ns > 0.0
                {
                    let ticks: [u64; 4] = std::array::from_fn(|index| {
                        let offset = index * 8;
                        u64::from_ne_bytes(mapped[offset..offset + 8].try_into().unwrap())
                    });
                    gpu_timing_status = "invalid_ticks";
                    if let (Some(render_ticks), Some(copy_ticks)) =
                        (ticks[1].checked_sub(ticks[0]), ticks[3].checked_sub(ticks[2]))
                    {
                        gpu_timing_ms = Some((
                            render_ticks as f64 * timestamp_period_ns / 1_000_000.0,
                            copy_ticks as f64 * timestamp_period_ns / 1_000_000.0,
                        ));
                        gpu_timing_status = "ok";
                    }
                } else {
                    gpu_timing_status = "invalid_ticks";
                }
                drop(mapped);
                probe.readback.unmap();
            }
        }
        let profile_publish_begin = std::time::Instant::now();
        match receiver.recv() {
            Ok(Ok(())) => {
                let data = buffer_slice.get_mapped_range();
                if (data.len() as u64) < staging_size {
                    set_error(error_buf, error_buf_len, "Mapped color staging buffer is truncated");
                    return CoinWgpuStatus::BackendError;
                }
                let profile_color_mapped = trace_phases.then(std::time::Instant::now);
                // Color-only output has no second attachment to commit
                // atomically, so publish rows directly to the caller buffer.
                let mut pending_color = depth_staging_info.as_ref()
                    .map(|_| vec![0u8; required_target_len as usize]);
                if let Some(ref mut color) = pending_color {
                    copy_color_rows(color, &data, width, height, bytes_per_row);
                } else {
                    let output = match validate_slice_mut(tgt.color_buffer, required_target_len,
                        "target color buffer", error_buf, error_buf_len) {
                        Ok(output) => output, Err(status) => return status,
                    };
                    copy_color_rows(output, &data, width, height, bytes_per_row);
                }
                let profile_color_copied = trace_phases.then(std::time::Instant::now);

                let mut pending_depth = None;
                if let (Some(ref dslice), Some((_, d_bpr))) = (&depth_slice_holder, &depth_staging_info) {
                    match depth_receiver.recv() {
                        Ok(Ok(())) => (),
                        Ok(Err(e)) => {
                            set_error(error_buf, error_buf_len, &format!("Depth buffer mapping failed: {:?}", e));
                            return CoinWgpuStatus::BackendError;
                        }
                        Err(e) => {
                            set_error(error_buf, error_buf_len, &format!("Depth mapping channel failed: {:?}", e));
                            return CoinWgpuStatus::BackendError;
                        }
                    }
                    let ddata = dslice.get_mapped_range();
                    let dfloats: &[f32] = bytemuck::cast_slice(&ddata);
                    let floats_per_row = (*d_bpr / 4) as usize;
                    let row_floats = width as usize;
                    let mut depth = vec![1.0f32; (width as usize) * (height as usize)];
                    for y in 0..height as usize {
                        let src_off = y * floats_per_row;
                        let dst_off = y * row_floats;
                        if src_off + row_floats > dfloats.len() {
                            set_error(error_buf, error_buf_len, "Mapped depth staging buffer is truncated");
                            return CoinWgpuStatus::BackendError;
                        }
                        depth[dst_off..dst_off + row_floats]
                            .copy_from_slice(&dfloats[src_off..src_off + row_floats]);
                    }
                    pending_depth = Some(depth);
                }
                let profile_depth_copied = trace_phases.then(std::time::Instant::now);

                // Publish both attachments only after every requested map succeeds.
                if let Some(color) = pending_color {
                    let out_buf = match validate_slice_mut(tgt.color_buffer, required_target_len,
                        "target color buffer", error_buf, error_buf_len) {
                        Ok(b) => b,
                        Err(st) => return st,
                    };
                    out_buf.copy_from_slice(&color);
                }
                if let Some(depth) = pending_depth {
                    let out_depth = unsafe { std::slice::from_raw_parts_mut(tgt.depth_buffer, depth.len()) };
                    out_depth.copy_from_slice(&depth);
                }
                let profile_outputs_published = trace_phases.then(std::time::Instant::now);
                drop(data);
                staging_buffer.unmap();
                if let Ok(mut pool) = ctx.readback_pool.lock() {
                    pool.recycle(staging_buffer.clone());
                    if let Some((buffer, _)) = depth_staging_info.as_ref() {
                        buffer.unmap();
                        pool.recycle(buffer.clone());
                    }
                }
                if !depth_history.is_null() {
                    let token = unsafe { &mut *depth_history };
                    if *token == 0 { *token = NEXT_DEPTH_HISTORY.fetch_add(1, Ordering::Relaxed); }
                    let (texture, view) = msaa_attachments.as_ref()
                        .map_or((&depth_texture, &depth_view), |a| (&a.2, &a.3));
                    ctx.target_depth_histories.insert(*token, TargetDepthHistory {
                        width, height, samples: sample_count, texture: texture.clone(), view: view.clone(),
                    });
                }
                tgt.submission_serial = sub_serial;
                commit_camera_scene(ctx, next_scene, commit_scene, f.frame_revision, owned_patch.is_some(), capture_scene,
                    !instances_slice.is_empty(), pending_instances);
                if attachment_cache_enabled {
                    ctx.cached_offscreen_attachments = Some(CachedOffscreenAttachments {
                        width, height, color_texture, color_view,
                        depth: depth_history.is_null().then_some((depth_texture, depth_view)),
                    });
                }
                if trace_phases {
                    let profile_done = std::time::Instant::now();
                    let info = ctx.adapter.get_info();
                    let renderer = match info.backend {
                        wgpu::Backend::Vulkan => "vulkan",
                        wgpu::Backend::Gl => "opengl",
                        wgpu::Backend::Metal => "metal",
                        wgpu::Backend::Dx12 => "dx12",
                        _ => "other",
                    };
                    // Emit only after checked GPU completion and publication.
                    eprintln!("COIN_RENDER_PHASE wgpu_offscreen renderer={} vendor_id={:#x} device_id={:#x} device_type={:?} serial={} size={}x{} adapter_name={:?}",
                        renderer, info.vendor, info.device, info.device_type,
                        sub_serial, width, height, info.name);
                    trace_owned_resources(ctx, "offscreen", width, height, staging_size,
                        if depth_requested { depth_staging_size } else { 0 });
                    eprintln!("COIN_RENDER_PHASE rust validation_ms={:.6} prepare_encode_ms={:.6} submit_ms={:.6} gpu_wait_ms={:.6} readback_publish_ms={:.6} staging_color_reused={} attachments_reused={} camera_bindings_created={} camera_bindings_reused={}",
                        (profile_validated - profile_start).as_secs_f64() * 1000.0,
                        (profile_encoded - profile_validated).as_secs_f64() * 1000.0,
                        (profile_submitted - profile_submit_begin).as_secs_f64() * 1000.0,
                        (profile_wait_done - profile_wait_begin).as_secs_f64() * 1000.0,
                        (profile_done - profile_publish_begin).as_secs_f64() * 1000.0,
                        u8::from(color_staging_reused),
                        u8::from(attachments_reused),
                        ctx.camera_bindings_created.load(Ordering::Relaxed),
                        ctx.camera_bindings_reused.load(Ordering::Relaxed));
                    let duration_ms = |start: std::time::Instant, end: std::time::Instant| {
                        (end - start).as_secs_f64() * 1000.0
                    };
                    let attachments = profile_attachments_created.unwrap();
                    let draw_encoded = profile_draw_encoded.unwrap();
                    let color_mapped = profile_color_mapped.unwrap();
                    let color_copied = profile_color_copied.unwrap();
                    let depth_copied = profile_depth_copied.unwrap();
                    let outputs_published = profile_outputs_published.unwrap();
                    eprintln!("COIN_RENDER_PHASE rust_cpu_detail total_ms={:.6} validation_ms={:.6} gpu_probe_setup_ms={:.6} attachments_ms={:.6} draw_encode_ms={:.6} scene_snapshot_ms={:.6} staging_prepare_ms={:.6} submit_ms={:.6} map_request_ms={:.6} gpu_wait_ms={:.6} post_wait_checks_ms={:.6} gpu_probe_read_ms={:.6} map_receive_ms={:.6} color_copy_ms={:.6} depth_copy_ms={:.6} output_commit_ms={:.6} recycle_ms={:.6}",
                        duration_ms(profile_start, profile_done),
                        duration_ms(profile_start, profile_validated),
                        duration_ms(profile_validated, profile_probe_ready.unwrap()),
                        duration_ms(profile_probe_ready.unwrap(), attachments),
                        duration_ms(attachments, draw_encoded),
                        duration_ms(draw_encoded, profile_encoded),
                        duration_ms(profile_encoded, profile_submit_begin),
                        duration_ms(profile_submit_begin, profile_submitted),
                        duration_ms(profile_submitted, profile_wait_begin),
                        duration_ms(profile_wait_begin, profile_wait_done),
                        duration_ms(profile_wait_done, profile_gpu_read_begin.unwrap()),
                        duration_ms(profile_gpu_read_begin.unwrap(), profile_publish_begin),
                        duration_ms(profile_publish_begin, color_mapped),
                        duration_ms(color_mapped, color_copied),
                        duration_ms(color_copied, depth_copied),
                        duration_ms(depth_copied, outputs_published),
                        duration_ms(outputs_published, profile_done));
                    if let Some((render_ms, copy_ms)) = gpu_timing_ms {
                        eprintln!("COIN_RENDER_PHASE rust_gpu status={} render_ms={:.6} copy_ms={:.6} timestamp_period_ns={:.6}",
                            gpu_timing_status, render_ms, copy_ms, timestamp_period_ns);
                    } else {
                        eprintln!("COIN_RENDER_PHASE rust_gpu status={}", gpu_timing_status);
                    }
                }
                CoinWgpuStatus::Ok
            }
            Ok(Err(buf_err)) => {
                if ctx.device_id == 0 && DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst) {
                    runtime.device_state = None;
                    runtime.device_generation += 1;
                    set_error(
                        error_buf,
                        error_buf_len,
                        "Device lost during buffer mapping",
                    );
                    CoinWgpuStatus::DeviceLost
                } else {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Buffer async mapping failed: {:?}", buf_err),
                    );
                    CoinWgpuStatus::BackendError
                }
            }
            Err(recv_err) => {
                if ctx.device_id == 0 && DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst) {
                    runtime.device_state = None;
                    runtime.device_generation += 1;
                    set_error(
                        error_buf,
                        error_buf_len,
                        "Device lost: mapping channel dropped",
                    );
                    CoinWgpuStatus::DeviceLost
                } else {
                    set_error(
                        error_buf,
                        error_buf_len,
                        &format!("Mapping channel receive failed: {:?}", recv_err),
                    );
                    CoinWgpuStatus::BackendError
                }
            }
        }
    });

    match res {
        Ok(status) => status,
        Err(_) => {
            set_error(error_buf, error_buf_len, "Rust bridge panic caught");
            CoinWgpuStatus::BackendError
        }
    }
}

#[no_mangle]
pub extern "C" fn coin_wgpu_submit(
    target: *mut CoinWgpuTarget,
    frame: *const CoinWgpuFrameView,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    coin_wgpu_submit_internal(target, frame, std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut(), error_buf, error_buf_len)
}


#[no_mangle]
pub extern "C" fn coin_wgpu_submit_with_depth_history(
    target: *mut CoinWgpuTarget, frame: *const CoinWgpuFrameView, history: *mut u64,
    error_buf: *mut std::os::raw::c_char, error_buf_len: usize,
) -> CoinWgpuStatus {
    if history.is_null() || (history as usize) % std::mem::align_of::<u64>() != 0 {
        set_error(error_buf, error_buf_len, "Null or misaligned target depth history token");
        return CoinWgpuStatus::InvalidArgument;
    }
    coin_wgpu_submit_internal(target, frame, std::ptr::null_mut(), std::ptr::null_mut(),
        history, error_buf, error_buf_len)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_release_depth_history(history: u64) {
    if let Ok(mut guard) = RUNTIME_CTX.lock() {
        if let Some(runtime) = guard.as_mut() {
            if let Some(device) = runtime.device_state.as_mut() {
                device.target_depth_histories.remove(&history);
            }
            for device in runtime.extra_devices.values_mut() {
                device.target_depth_histories.remove(&history);
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn coin_wgpu_submit_texture(
    target: *mut CoinWgpuTarget,
    frame: *const CoinWgpuFrameView,
    out_token: *mut u64,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    if out_token.is_null() || (out_token as usize) % std::mem::align_of::<u64>() != 0 {
        set_error(error_buf, error_buf_len, "Null or misaligned RTT output token pointer");
        return CoinWgpuStatus::InvalidArgument;
    }
    unsafe { *out_token = 0; }
    coin_wgpu_submit_internal(target, frame, std::ptr::null_mut(), out_token, std::ptr::null_mut(), error_buf, error_buf_len)
}

fn cube_rtt_layout(n:u32,mips:bool)->Result<(u32,u32),(CoinWgpuStatus,String)> {
    if n==0 || n>2048 || !n.is_power_of_two(){return Err((CoinWgpuStatus::InvalidArgument,"Invalid cube RTT face extent".into()));}
    let width=2*n;let mut extent=n;let mut texels=0u64;
    loop {texels+=6*u64::from(extent)*u64::from(extent);if !mips||extent==1{break;}extent/=2;}
    let height=texels.div_ceil(u64::from(width));
    if height>8192 || u64::from(width)*height*4>128*1024*1024 {
        return Err((CoinWgpuStatus::Unsupported,"Cube RTT atlas exceeds 128 MiB".into()));
    }
    Ok((width,height as u32))
}
fn admit_cube_rtt(ctx:&DeviceState,n:u32,mips:bool,samples:u32)->Result<(u32,u32),(CoinWgpuStatus,String)> {
    let (width,height)=cube_rtt_layout(n,mips)?;
    if !matches!(samples,1|2|4) || width>ctx.device.limits().max_texture_dimension_2d || height>ctx.device.limits().max_texture_dimension_2d {
        return Err((CoinWgpuStatus::Unsupported,"Cube RTT samples or atlas extent unavailable".into()));
    }
    let color=ctx.adapter.get_texture_format_features(wgpu::TextureFormat::Rgba8Unorm);
    let depth=ctx.adapter.get_texture_format_features(wgpu::TextureFormat::Depth32Float);
    let required=if samples==2 {wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X2}
        else if samples==4 {wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X4} else {wgpu::TextureFormatFeatureFlags::empty()};
    if !color.allowed_usages.contains(wgpu::TextureUsages::RENDER_ATTACHMENT|wgpu::TextureUsages::TEXTURE_BINDING|
        wgpu::TextureUsages::COPY_SRC|wgpu::TextureUsages::COPY_DST) ||
       !color.flags.contains(required) || !depth.flags.contains(required) {
        return Err((CoinWgpuStatus::Unsupported,"Cube RTT color/depth copy or sample format unavailable".into()));
    }
    Ok((width,height))
}
#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_cube_rtt(n:u32,mips:u32,samples:u32)->CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if mips>1{return CoinWgpuStatus::InvalidArgument;}
        let Ok(guard)=RUNTIME_CTX.lock() else{return CoinWgpuStatus::BackendError;};
        let Some(ctx)=guard.as_ref().and_then(|runtime|runtime.device_state.as_ref()) else{return CoinWgpuStatus::NotReady;};
        match admit_cube_rtt(ctx,n,mips!=0,samples){Ok(_)=>CoinWgpuStatus::Ok,Err((status,_))=>status}
    }).unwrap_or(CoinWgpuStatus::BackendError)
}
#[no_mangle]
pub extern "C" fn coin_wgpu_assemble_cube_rtt(faces:*const u64,n:u32,mips:u32,generation:u64,
    out_token:*mut u64,error_buf:*mut std::os::raw::c_char,error_buf_len:usize)->CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if out_token.is_null() || (out_token as usize)%std::mem::align_of::<u64>()!=0 || mips>1 {
            set_error(error_buf,error_buf_len,"Invalid cube RTT output pointer or mip flag");return CoinWgpuStatus::InvalidArgument;
        }
        unsafe{*out_token=0;}
        let faces=match validate_slice(faces,6,"cube RTT face tokens",error_buf,error_buf_len){Ok(v)=>v,Err(status)=>return status};
        let Ok(mut guard)=RUNTIME_CTX.lock() else{set_error(error_buf,error_buf_len,"Cube RTT runtime lock failed");return CoinWgpuStatus::BackendError;};
        let Some(ctx)=guard.as_mut().and_then(|runtime|runtime.device_state.as_mut()) else{set_error(error_buf,error_buf_len,"Cube RTT default device is not prepared");return CoinWgpuStatus::NotReady;};
        // The public connector domain uses raw generation + 1, preserving
        // zero as unavailable while accepting the first raw generation (0).
        // Match the existing query's checked epoch conversion on overflow.
        if generation==0 || ctx.generation.checked_add(1)!=Some(generation) {
            set_error(error_buf,error_buf_len,"Cube RTT belongs to another device generation");return CoinWgpuStatus::DeviceLost;
        }
        let (width,height)=match admit_cube_rtt(ctx,n,mips!=0,1){Ok(v)=>v,Err((status,message))=>{set_error(error_buf,error_buf_len,&message);return status;}};
        let mut registry=ctx.rtt_textures.lock().unwrap();
        if registry.active.len()>=64{set_error(error_buf,error_buf_len,"Cube RTT atlas exceeds retained texture capacity");return CoinWgpuStatus::OutOfMemory;}
        let mut sources=Vec::with_capacity(6);
        let levels=if mips!=0 {n.ilog2()+1}else{1};
        for (face,&token) in faces.iter().enumerate() {
            if token==0 || faces[..face].contains(&token) {
                set_error(error_buf,error_buf_len,"Cube RTT faces alias or have a zero token");return CoinWgpuStatus::InvalidArgument;
            }
            let Some(source)=registry.active.get(&token) else {
                set_error(error_buf,error_buf_len,"Cube RTT face token is stale or belongs to another device");return CoinWgpuStatus::InvalidArgument;
            };
            if source.width!=n || source.height!=n || source.format!=0 || source.cube_face_size!=0 || source.mip_count<levels {
                set_error(error_buf,error_buf_len,"Cube RTT face token metadata mismatch");return CoinWgpuStatus::InvalidArgument;
            }
            sources.push(source.texture.clone());
        }
        let texture=ctx.device.create_texture(&wgpu::TextureDescriptor {
            label:Some("Coin cube RTT canonical atlas"),size:wgpu::Extent3d{width,height,depth_or_array_layers:1},
            mip_level_count:1,sample_count:1,dimension:wgpu::TextureDimension::D2,format:wgpu::TextureFormat::Rgba8Unorm,
            usage:wgpu::TextureUsages::RENDER_ATTACHMENT|wgpu::TextureUsages::TEXTURE_BINDING|wgpu::TextureUsages::COPY_SRC|wgpu::TextureUsages::COPY_DST,
            view_formats:&[],
        });
        let view=texture.create_view(&Default::default());
        let mut encoder=ctx.device.create_command_encoder(&wgpu::CommandEncoderDescriptor{label:Some("Coin six-face cube RTT assembly")});
        {
            let _clear=encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label:Some("Coin cube atlas zero padding"),
                color_attachments:&[Some(wgpu::RenderPassColorAttachment{view:&view,resolve_target:None,
                    ops:wgpu::Operations{load:wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT),store:wgpu::StoreOp::Store}})],
                depth_stencil_attachment:None,timestamp_writes:None,occlusion_query_set:None,
            });
        }
        let mut level_offset=0u64;let mut extent=n;
        for level in 0..levels {
            for (face,source) in sources.iter().enumerate() {
                for y in 0..extent {
                    let mut x=0;
                    while x<extent {
                        let linear=level_offset+face as u64*u64::from(extent)*u64::from(extent)+u64::from(y)*u64::from(extent)+u64::from(x);
                        let dx=(linear%u64::from(width)) as u32;let dy=(linear/u64::from(width)) as u32;
                        let count=(extent-x).min(width-dx);
                        encoder.copy_texture_to_texture(
                            wgpu::TexelCopyTextureInfo{texture:source,mip_level:level,origin:wgpu::Origin3d{x,y:extent-1-y,z:0},aspect:wgpu::TextureAspect::All},
                            wgpu::TexelCopyTextureInfo{texture:&texture,mip_level:0,origin:wgpu::Origin3d{x:dx,y:dy,z:0},aspect:wgpu::TextureAspect::All},
                            wgpu::Extent3d{width:count,height:1,depth_or_array_layers:1});
                        x+=count;
                    }
                }
            }
            level_offset+=6*u64::from(extent)*u64::from(extent);extent=(extent/2).max(1);
        }
        let token=NEXT_RTT_TOKEN.fetch_add(1,Ordering::SeqCst);
        if token==0{set_error(error_buf,error_buf_len,"Cube RTT token space exhausted");return CoinWgpuStatus::OutOfMemory;}
        let serial=GLOBAL_SUBMISSION_SERIAL.fetch_add(1,Ordering::SeqCst);
        LAST_SUBMITTED_SERIAL.store(serial,Ordering::SeqCst);
        let local_serial=ctx.last_submitted_serial.fetch_add(1,Ordering::SeqCst)+1;
        ctx.queue.submit([encoder.finish()]);
        let completed=ctx.completed_serial.clone();
        ctx.queue.on_submitted_work_done(move||{completed.fetch_max(local_serial,Ordering::SeqCst);GLOBAL_COMPLETED_SERIAL.fetch_max(serial,Ordering::SeqCst);});
        registry.active.insert(token,RttTexture{texture,view,width,height,opaque:false,format:0,mip_count:1,cube_face_size:n,cube_mipmapped:mips!=0});
        unsafe{*out_token=token;}CoinWgpuStatus::Ok
    }).unwrap_or_else(|_|{set_error(error_buf,error_buf_len,"Cube RTT assembly panicked");CoinWgpuStatus::BackendError})
}

#[no_mangle]
pub extern "C" fn coin_wgpu_release_texture(token: u64) {
    let _ = std::panic::catch_unwind(|| {
        if token == 0 { return; }
        if let Ok(mut guard) = RUNTIME_CTX.lock() {
            if let Some(runtime) = guard.as_mut() {
                if let Some(ctx) = runtime.device_state.as_mut() {
                    if let Ok(mut textures) = ctx.rtt_textures.lock() {
                        if let Some(entry) = textures.active.remove(&token) {
                            let serial = ctx.last_submitted_serial.load(Ordering::SeqCst);
                            if ctx.completed_serial.load(Ordering::SeqCst) < serial {
                                textures.retired.push(RetiredRttTexture {
                                    resource: entry,
                                    retired_at_serial: serial,
                                });
                            }
                        }
                    }
                }
                for ctx in runtime.extra_devices.values() {
                    if let Ok(mut textures) = ctx.rtt_textures.lock() {
                        if let Some(entry) = textures.active.remove(&token) {
                            let serial = ctx.last_submitted_serial.load(Ordering::SeqCst);
                            if ctx.completed_serial.load(Ordering::SeqCst) < serial {
                                textures.retired.push(RetiredRttTexture {
                                    resource: entry,
                                    retired_at_serial: serial,
                                });
                            }
                            break;
                        }
                    }
                }
            }
        }
    });
}

#[no_mangle]
pub extern "C" fn coin_wgpu_readback_resource_load(jobs: *mut u64, bytes: *mut u64) -> i32 {
    if jobs.is_null() || bytes.is_null() || (jobs as usize) % 8 != 0 || (bytes as usize) % 8 != 0 {
        return 0;
    }
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let mut guard = match RUNTIME_CTX.lock() {
            Ok(value) => value,
            Err(_) => return 0,
        };
        let (count, payload) = if let Some(runtime) = guard.as_mut() {
            reap_cancelled_readbacks(runtime);
            let count = runtime.pending_readbacks.len() + runtime.retired_readbacks.len();
            let payload = runtime
                .pending_readbacks
                .values()
                .chain(runtime.retired_readbacks.iter())
                .map(|job| job.ticket.color_bytes + job.ticket.depth_bytes)
                .sum::<u64>();
            (count as u64, payload)
        } else {
            (0, 0)
        };
        unsafe {
            *jobs = count;
            *bytes = payload;
        }
        1
    }))
    .unwrap_or(0)
}

// Physical staging remains a separate charge for the multipass resource
// budget; the public queue limit above counts packed ticket payloads.
#[no_mangle]
pub extern "C" fn coin_wgpu_readback_staging_bytes() -> u64 {
    std::panic::catch_unwind(|| {
        let mut guard = RUNTIME_CTX.lock().ok()?;
        if let Some(runtime) = guard.as_mut() {
            reap_cancelled_readbacks(runtime);
        }
        multipass_pending_bytes(guard.as_ref())
    }).ok().flatten().unwrap_or(u64::MAX)
}

fn rtt_remaining_capacity(active: usize) -> u32 {
    64usize.saturating_sub(active) as u32
}

// Captured C++ actions use the default device. Other explicit devices have
// independent capacities, and a lost default device will be reconstructed.
#[no_mangle]
pub extern "C" fn coin_wgpu_default_rtt_capacity() -> u32 {
    std::panic::catch_unwind(|| {
        if DEVICE_LOST_OCCURRED.load(Ordering::Acquire) {
            return 64;
        }
        let Ok(guard) = RUNTIME_CTX.lock() else {
            return 0;
        };
        let Some(ctx) = guard
            .as_ref()
            .and_then(|runtime| runtime.device_state.as_ref())
        else {
            return 64;
        };
        let Ok(textures) = ctx.rtt_textures.lock() else {
            return 0;
        };
        rtt_remaining_capacity(textures.active.len())
    })
    .unwrap_or(0)
}

#[cfg(test)]
include!("../../../../../testsuite/coinwgpu/rust/unit/lib_rtt_capacity_test.rs");

#[no_mangle]
pub extern "C" fn coin_wgpu_rtt_resource_counts(active: *mut u64, retired: *mut u64) {
    if (!active.is_null() && (active as usize) % std::mem::align_of::<u64>() != 0)
        || (!retired.is_null() && (retired as usize) % std::mem::align_of::<u64>() != 0) {
        return;
    }
    let (a, r) = std::panic::catch_unwind(|| {
        let guard = RUNTIME_CTX.lock().ok()?;
        let runtime = guard.as_ref()?;
        let mut counts = (0, 0);
        for ctx in runtime.device_state.iter().chain(runtime.extra_devices.values()) {
            let textures = ctx.rtt_textures.lock().ok()?;
            counts.0 += textures.active.len() as u64;
            counts.1 += textures.retired.len() as u64;
        }
        Some(counts)
    }).ok().flatten().unwrap_or((0, 0));
    if !active.is_null() { unsafe { *active = a; } }
    if !retired.is_null() { unsafe { *retired = r; } }
}
#[no_mangle]
pub extern "C" fn coin_wgpu_submit_async(
    target: *mut CoinWgpuTarget,
    frame: *const CoinWgpuFrameView,
    out_ticket: *mut CoinWgpuReadbackTicket,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    if out_ticket.is_null() {
        set_error(error_buf, error_buf_len, "Null async readback ticket pointer");
        return CoinWgpuStatus::InvalidArgument;
    }
    coin_wgpu_submit_internal(target, frame, out_ticket, std::ptr::null_mut(), std::ptr::null_mut(), error_buf, error_buf_len)
}

fn check_readback_ready(
    runtime: &mut RuntimeContext,
    token: u64,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    if token == 0 {
        set_error(error_buf, error_buf_len, "Invalid readback token");
        return CoinWgpuStatus::InvalidArgument;
    }
    reap_cancelled_readbacks(runtime);
    if let Some((_, status)) = runtime.dead_readbacks.remove(&token) {
        set_error(error_buf, error_buf_len, "Readback device was destroyed or lost");
        return status;
    }
    if !runtime.pending_readbacks.contains_key(&token) {
        set_error(error_buf, error_buf_len, "Unknown or cancelled readback token");
        return CoinWgpuStatus::InvalidArgument;
    }
    let owner = runtime.pending_readbacks.get(&token).unwrap().device_id;
    let injected_fault = if owner == 0 { FAULT_INJECTION_ASYNC.swap(0, Ordering::SeqCst) }
        else { 0 };
    if injected_fault == CoinWgpuStatus::BackendError as i32 {
        if let Some(job) = runtime.pending_readbacks.remove(&token) {
            runtime.retired_readbacks.push(job);
        }
        set_error(error_buf, error_buf_len, "Injected asynchronous map failure");
        return CoinWgpuStatus::BackendError;
    }
    if injected_fault == CoinWgpuStatus::DeviceLost as i32
        || (owner == 0 && DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst)) {
        runtime.pending_readbacks.retain(|_, job| job.device_id != 0);
        runtime.retired_readbacks.retain(|job| job.device_id != 0);
        runtime.device_state = None;
        runtime.device_generation += 1;
        set_error(error_buf, error_buf_len, "Device lost during asynchronous readback");
        return CoinWgpuStatus::DeviceLost;
    }
    if let Some(device) = runtime.device_state.as_ref() {
        let _ = device.device.poll(wgpu::Maintain::Poll);
    }
    let generation = if owner == 0 {
        runtime.device_generation
    } else {
        let Some(device) = runtime.extra_devices.get(&owner) else {
            runtime.pending_readbacks.remove(&token);
            set_error(error_buf, error_buf_len, "Readback device was destroyed");
            return CoinWgpuStatus::DeviceLost;
        };
        if device.lost.load(Ordering::SeqCst) {
            lose_extra_device(runtime, owner);
            set_error(error_buf, error_buf_len, "Readback device was lost");
            return CoinWgpuStatus::DeviceLost;
        }
        let _ = device.device.poll(wgpu::Maintain::Poll);
        device.generation
    };
    let job = runtime.pending_readbacks.get_mut(&token).unwrap();
    if job.ticket.generation != generation {
        runtime.pending_readbacks.remove(&token);
        set_error(error_buf, error_buf_len, "Readback generation is stale");
        return CoinWgpuStatus::DeviceLost;
    }
    update_readback_mapping(job);
    if job.color_ready.is_none() || job.depth_ready.is_none() {
        return CoinWgpuStatus::NotReady;
    }
    if let Some(message) = job.color_ready.as_ref().and_then(|result| result.as_ref().err())
        .or_else(|| job.depth_ready.as_ref().and_then(|result| result.as_ref().err())) {
        let message = message.clone();
        if let Some(failed) = runtime.pending_readbacks.remove(&token) {
            if matches!(failed.color_ready.as_ref(), Some(Ok(()))) {
                failed.color.unmap();
            }
            if matches!(failed.depth_ready.as_ref(), Some(Ok(()))) {
                if let Some(buffer) = failed.depth.as_ref() {
                    buffer.unmap();
                }
            }
        }
        set_error(error_buf, error_buf_len, &format!("Readback mapping failed: {}", message));
        return CoinWgpuStatus::BackendError;
    }
    CoinWgpuStatus::Ok
}

// Read-only identity admission precedes query/poll/cancel in the public C++
// runtime. A forged descriptor must not advance mapping, consume injected
// errors, cancel staging, or remove a device-loss tombstone.
#[no_mangle]
pub extern "C" fn coin_wgpu_readback_validate(
    ticket: *const CoinWgpuReadbackTicket,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if ticket.is_null() || (ticket as usize) % std::mem::align_of::<CoinWgpuReadbackTicket>() != 0 {
            set_error(error_buf, error_buf_len, "Missing or misaligned readback ticket");
            return CoinWgpuStatus::InvalidArgument;
        }
        let supplied = unsafe { &*ticket };
        let guard = match RUNTIME_CTX.lock() {
            Ok(guard) => guard,
            Err(_) => return CoinWgpuStatus::BackendError,
        };
        let stored = guard.as_ref().and_then(|runtime|
            runtime.pending_readbacks.get(&supplied.token).map(|job| &job.ticket)
                .or_else(|| runtime.dead_readbacks.get(&supplied.token).map(|(ticket, _)| ticket)));
        if supplied.token == 0 || stored != Some(supplied) {
            set_error(error_buf, error_buf_len, "Invalid or consumed asynchronous readback ticket identity");
            return CoinWgpuStatus::InvalidArgument;
        }
        CoinWgpuStatus::Ok
    }).unwrap_or_else(|_| {
        set_error(error_buf, error_buf_len, "Panic during readback identity validation");
        CoinWgpuStatus::BackendError
    })
}

#[no_mangle]
pub extern "C" fn coin_wgpu_readback_query(
    token: u64,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        let mut guard = match RUNTIME_CTX.lock() {
            Ok(guard) => guard,
            Err(_) => return CoinWgpuStatus::BackendError,
        };
        let runtime = match guard.as_mut() {
            Some(runtime) => runtime,
            None => return CoinWgpuStatus::InvalidArgument,
        };
        check_readback_ready(runtime, token, error_buf, error_buf_len)
    }).unwrap_or_else(|_| {
        set_error(error_buf, error_buf_len, "Panic during asynchronous readback query");
        CoinWgpuStatus::BackendError
    })
}

#[no_mangle]
pub extern "C" fn coin_wgpu_readback_poll(
    token: u64,
    color_buffer: *mut u8,
    color_buffer_len: u64,
    depth_buffer: *mut f32,
    depth_buffer_len: u64,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        let mut guard = match RUNTIME_CTX.lock() {
            Ok(guard) => guard,
            Err(_) => return CoinWgpuStatus::BackendError,
        };
        let runtime = match guard.as_mut() {
            Some(runtime) => runtime,
            None => return CoinWgpuStatus::InvalidArgument,
        };
        let readiness = check_readback_ready(runtime, token, error_buf, error_buf_len);
        if readiness != CoinWgpuStatus::Ok {
            return readiness;
        }
        let ticket = runtime.pending_readbacks.get(&token).unwrap().ticket;
        if color_buffer.is_null() || color_buffer_len < ticket.color_bytes
            || (ticket.depth_bytes != 0 && (depth_buffer.is_null()
                || depth_buffer_len < ticket.depth_bytes / 4
                || (depth_buffer as usize) % std::mem::align_of::<f32>() != 0)) {
            set_error(error_buf, error_buf_len, "Readback output buffers are missing or too small");
            return CoinWgpuStatus::InvalidArgument;
        }
        if ticket.depth_bytes != 0 {
            let color_start = color_buffer as usize;
            let depth_start = depth_buffer as usize;
            let color_end = color_start.checked_add(ticket.color_bytes as usize);
            let depth_end = depth_start.checked_add(ticket.depth_bytes as usize);
            if color_end.is_none() || depth_end.is_none()
                || (color_start < depth_end.unwrap() && depth_start < color_end.unwrap()) {
                set_error(error_buf, error_buf_len, "Readback output buffers overlap or overflow");
                return CoinWgpuStatus::InvalidArgument;
            }
        }
        let color_output = match validate_slice_mut(color_buffer, ticket.color_bytes,
            "readback color buffer", error_buf, error_buf_len) {
            Ok(output) => output,
            Err(status) => return status,
        };
        let mut depth_output = None;
        if ticket.depth_bytes != 0 {
            depth_output = Some(match validate_slice_mut(depth_buffer, ticket.depth_bytes / 4,
                "readback depth buffer", error_buf, error_buf_len) {
                Ok(output) => output,
                Err(status) => return status,
            });
        }
        let job = runtime.pending_readbacks.remove(&token).unwrap();
        let color_view = job.color.slice(..).get_mapped_range();
        if (ticket.color_row_pitch as u64) * (ticket.height as u64) > color_view.len() as u64 {
            set_error(error_buf, error_buf_len, "Mapped color readback is truncated");
            return CoinWgpuStatus::BackendError;
        }
        let mut color = job.depth.as_ref().map(|_| vec![0u8; ticket.color_bytes as usize]);
        if let Some(ref mut pending) = color {
            copy_color_rows(pending, &color_view, ticket.width, ticket.height,
                ticket.color_row_pitch);
        } else {
            copy_color_rows(color_output, &color_view, ticket.width, ticket.height,
                ticket.color_row_pitch);
        }
        drop(color_view);
        job.color.unmap();
        if let Ok(mut pool) = job.pool.lock() {
            pool.recycle(job.color.clone());
        }

        let mut depth = None;
        if let Some(buffer) = job.depth.as_ref() {
            let depth_view = buffer.slice(..).get_mapped_range();
            let mapped: &[f32] = bytemuck::cast_slice(&depth_view);
            let row_floats = ticket.width as usize;
            let pitch_floats = ticket.depth_row_pitch as usize / 4;
            let mut pixels = vec![1.0f32; (ticket.depth_bytes / 4) as usize];
            for y in 0..ticket.height as usize {
                let source = y * pitch_floats;
                let destination = y * row_floats;
                if source + row_floats > mapped.len() {
                    set_error(error_buf, error_buf_len, "Mapped depth readback is truncated");
                    return CoinWgpuStatus::BackendError;
                }
                pixels[destination..destination + row_floats]
                    .copy_from_slice(&mapped[source..source + row_floats]);
            }
            drop(depth_view);
            buffer.unmap();
            if let Ok(mut pool) = job.pool.lock() {
                pool.recycle(buffer.clone());
            }
            depth = Some(pixels);
        }

        if let Some(color) = color {
            color_output.copy_from_slice(&color);
        }
        if let (Some(pixels), Some(output)) = (depth, depth_output) {
            output.copy_from_slice(&pixels);
        }
        CoinWgpuStatus::Ok
    }).unwrap_or_else(|_| {
        set_error(error_buf, error_buf_len, "Panic during asynchronous readback");
        CoinWgpuStatus::BackendError
    })
}

#[no_mangle]
pub extern "C" fn coin_wgpu_readback_cancel(token: u64) -> CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        let mut guard = match RUNTIME_CTX.lock() {
            Ok(guard) => guard,
            Err(_) => return CoinWgpuStatus::BackendError,
        };
        if let Some(runtime) = guard.as_mut() {
            if let Some(job) = runtime.pending_readbacks.remove(&token) {
                runtime.retired_readbacks.push(job);
                reap_cancelled_readbacks(runtime);
                return CoinWgpuStatus::Ok;
            }
        }
        CoinWgpuStatus::InvalidArgument
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_get_cache_stats(stats: *mut CoinWgpuCacheStats) {
    let _ = std::panic::catch_unwind(|| {
        if stats.is_null() {
            return;
        }
        let mut out = CoinWgpuCacheStats::default();
        if let Ok(guard) = RUNTIME_CTX.lock() {
            if let Some(runtime) = guard.as_ref() {
                if let Some(dev) = &runtime.device_state {
                    if let Ok(cache) = dev.cache.lock() {
                        out.cumulative_uploads = cache.cumulative_uploads;
                        out.cumulative_hits = cache.cumulative_hits;
                        out.cumulative_misses = cache.cumulative_misses;
                        out.cumulative_uploaded_bytes = cache.cumulative_uploaded_bytes;
                        out.frame_uploaded_bytes = cache.frame_uploaded_bytes;
                        out.frame_uploads = cache.frame_uploads;
                        out.frame_hits = cache.frame_hits;
                        out.active_entries = cache.active_entries.len() as u64;
                        out.retired_entries = cache.deferred_release.len() as u64;
                    }
                }
            }
        }
        out.completed_serial = GLOBAL_COMPLETED_SERIAL.load(Ordering::SeqCst);
        out.submission_serial = LAST_SUBMITTED_SERIAL.load(Ordering::SeqCst);
        unsafe {
            *stats = out;
        }
    });
}

#[no_mangle]
pub extern "C" fn coin_wgpu_get_performance_stats(stats: *mut CoinWgpuPerformanceStats) {
    let _ = std::panic::catch_unwind(|| {
        if stats.is_null() {
            return;
        }
        let mut out = CoinWgpuPerformanceStats::default();
        if let Ok(guard) = RUNTIME_CTX.lock() {
            if let Some(dev) = guard.as_ref().and_then(|runtime| runtime.device_state.as_ref()) {
                if let Ok(cache) = dev.texture_cache.lock() {
                    out.texture_uploads = cache.uploads;
                    out.texture_hits = cache.hits;
                    out.texture_uploaded_bytes = cache.uploaded_bytes;
                    out.texture_evictions = cache.evictions;
                    out.texture_active_entries = cache.entries.len() as u64;
                    out.texture_retired_entries = cache.retired.len() as u64;
                }
                if let Ok(pipelines) = dev.surface_native.pipelines.lock() {
                    out.pipeline_compilations = dev.pipeline_compilations.load(Ordering::Relaxed);
                    out.pipeline_active_entries = pipelines.len() as u64;
                }
                if let Some(profile) = dev.surface_portable.get() {
                    if let Ok(pipelines) = profile.pipelines.lock() {
                        out.pipeline_active_entries += pipelines.len() as u64;
                    }
                }
                out.pipeline_hits = dev.pipeline_hits.load(Ordering::Relaxed);
            }
        }
        unsafe {
            *stats = out;
        }
    });
}

#[no_mangle]
pub extern "C" fn coin_wgpu_poll_device() {
    let _ = std::panic::catch_unwind(|| {
        if let Ok(mut guard) = RUNTIME_CTX.lock() {
            if let Some(runtime) = guard.as_mut() {
                reap_cancelled_readbacks(runtime);
                if let Some(dev) = &runtime.device_state {
                    let _ = dev.device.poll(wgpu::Maintain::Poll);
                    let completed = dev.completed_serial.load(Ordering::SeqCst);
                    if let Ok(mut textures) = dev.texture_cache.lock() {
                        textures.retired.retain(|entry| entry.retired_at_serial > completed);
                    }
                    if let Ok(mut textures) = dev.rtt_textures.lock() {
                        textures.retired.retain(|entry| entry.retired_at_serial > completed);
                    }
                }
                for dev in runtime.extra_devices.values() {
                    let _ = dev.device.poll(wgpu::Maintain::Poll);
                    let completed = dev.completed_serial.load(Ordering::SeqCst);
                    if let Ok(mut cache) = dev.cache.lock() {
                        cache.deferred_release.retain(|entry| entry.retired_at_serial > completed);
                    }
                    if let Ok(mut textures) = dev.texture_cache.lock() {
                        textures.retired.retain(|entry| entry.retired_at_serial > completed);
                    }
                    if let Ok(mut textures) = dev.rtt_textures.lock() {
                        textures.retired.retain(|entry| entry.retired_at_serial > completed);
                    }
                }
            }
        }
    });
}

#[no_mangle]
pub extern "C" fn coin_wgpu_set_cache_budget(max_bytes: u64, max_stale_serials: u64) {
    let _ = std::panic::catch_unwind(|| {
        if let Ok(guard) = RUNTIME_CTX.lock() {
            if let Some(runtime) = guard.as_ref() {
                if let Some(dev) = &runtime.device_state {
                    if let Ok(mut cache) = dev.cache.lock() {
                        cache.max_active_geometry_bytes = max_bytes;
                        cache.max_stale_serials = max_stale_serials;
                    }
                }
            }
        }
    });
}

#[no_mangle]
pub extern "C" fn coin_wgpu_trim_cache() {
    let _ = std::panic::catch_unwind(|| {
        if let Ok(guard) = RUNTIME_CTX.lock() {
            if let Some(runtime) = guard.as_ref() {
                if let Some(dev) = &runtime.device_state {
                    if let Ok(mut cache) = dev.cache.lock() {
                        let last_serial = dev.last_submitted_serial.load(Ordering::SeqCst);
                        let mut to_remove = Vec::new();
                        for (&key, (_rev, entry)) in &cache.active_entries {
                            if entry.last_submitted_serial < last_serial {
                                to_remove.push(key);
                            }
                        }
                        for key in to_remove {
                            if let Some((_rev, entry)) = cache.active_entries.remove(&key) {
                                cache.total_active_geometry_bytes = cache
                                    .total_active_geometry_bytes
                                    .saturating_sub(entry.size_bytes);
                                cache.cumulative_evictions += 1;
                                cache.deferred_release.push(RetiredBuffer {
                                    vertex_buffer: entry.vertex_buffer,
                                    index_buffer: entry.index_buffer,
                                    retired_at_serial: entry.last_submitted_serial,
                                    size_bytes: entry.size_bytes,
                                });
                            }
                        }
                    }
                }
            }
        }
    });
}

#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/lib_row_copy_tests.rs"]
mod row_copy_tests;
