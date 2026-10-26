#![allow(clippy::not_unsafe_ptr_arg_deref)]
#![allow(clippy::too_many_arguments)]
#![allow(clippy::manual_is_multiple_of)]
#![allow(clippy::if_same_then_else)]
#![allow(clippy::needless_range_loop)]
#![allow(clippy::needless_lifetimes)]
mod frame_validation;
use frame_validation::*;
mod bump_encoding;
use bump_encoding::*;
mod camera_cache;
use camera_cache::*;
mod runtime;
pub use runtime::*;
mod ffi_validation;
use ffi_validation::*;
#[cfg(any(test, feature = "test-hooks"))]
mod diagnostics;
#[cfg(any(test, feature = "test-hooks"))]
pub use diagnostics::*;
mod fault_state;
mod pipelines;
use pipelines::*;
mod texture_validation;
use texture_validation::*;
mod shadow_encoding;
use shadow_encoding::*;
mod multipass_encoding;
use multipass_encoding::*;
mod frame_encoding;
use frame_encoding::*;
mod capabilities;
pub use capabilities::*;
mod surface_api;
pub use surface_api::*;
mod offscreen_submit;
use offscreen_submit::*;
mod texture_api;
pub use texture_api::*;
mod readback_api;
pub use readback_api::*;

mod raster_image;
mod texture_profile;
mod texture_mips;
mod surface_present;
mod multipass;
mod stereo;
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
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
#[cfg(any(test, feature = "test-hooks"))]
use std::sync::atomic::AtomicI32;

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

pub const COIN_WGPU_BRIDGE_PROTOCOL_REVISION: u32 = 70;
pub const COIN_WGPU_ABI_VERSION: u32 = COIN_WGPU_BRIDGE_PROTOCOL_REVISION;

const _: () = {
    assert!(std::mem::size_of::<CoinWgpuLight>() == 80);
    assert!(std::mem::align_of::<CoinWgpuLight>() == 4);
    assert!(std::mem::offset_of!(CoinWgpuLight, ambient_color) == 64);
    assert!(std::mem::size_of::<CoinWgpuFrameView>() == 600);
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
    pub stereo_tokens: [u64;2],
    pub stereo_mode: u32,
    pub stereo_origin: [i32;2],
    pub stereo_extent: [i32;2],
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

#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/lib_bump_abi_tests.rs"]
mod bump_abi_tests;

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
    // View-owned attachments retain the exact samples until final composition.
    completed_view: Option<(wgpu::TextureView,wgpu::TextureView,u32)>,
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
    #[cfg(any(test, feature = "test-hooks"))]
    injected_fault: AtomicI32,
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

static RUNTIME_CTX: Mutex<Option<RuntimeContext>> = Mutex::new(None);
static RUNTIME_EXIT_STARTED: AtomicBool = AtomicBool::new(false);

extern "C" {
    fn atexit(callback: extern "C" fn()) -> std::os::raw::c_int;
}

#[cfg(target_os = "android")]
static ANDROID_RUNTIME_GENERATION: AtomicU64 = AtomicU64::new(0);
static VALIDATED_FRAME_CACHE: Mutex<Option<(u64, Vec<composition::CompositionItem>)>> =
    Mutex::new(None);
static NEXT_SURFACE_ID: AtomicU64 = AtomicU64::new(1);

#[cfg(any(test, feature = "test-hooks"))]
static FAULT_INJECTION: AtomicI32 = AtomicI32::new(0);
#[cfg(any(test, feature = "test-hooks"))]
static FAULT_INJECTION_ASYNC: AtomicI32 = AtomicI32::new(0);

#[cfg(any(test, feature = "test-hooks"))]
static FAULT_AFTER_SUBMITS: AtomicI32 = AtomicI32::new(-1);
#[cfg(any(test, feature = "test-hooks"))]
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

#[cfg(any(test, feature = "test-hooks"))]
static FAULT_SURFACE_OUTDATED_COUNT: AtomicI32 = AtomicI32::new(0);
#[cfg(any(test, feature = "test-hooks"))]
static FAULT_SURFACE_LOST_COUNT: AtomicI32 = AtomicI32::new(0);

// Asynchronous error reporting without deadlocking RUNTIME_CTX
static DEVICE_LOST_OCCURRED: AtomicBool = AtomicBool::new(false);
static LAST_ASYNC_ERROR_KIND: AtomicU32 = AtomicU32::new(0); // 0=None, 1=OOM, 2=BackendError
static LAST_ASYNC_ERROR_MSG: Mutex<String> = Mutex::new(String::new());

#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/lib_polygon_depth_tests.rs"]
mod polygon_depth_tests;

// The texture metadata declaration changes admission only: source
// alpha remains available to every texture combine, including SOURCE_ALPHA.
const TEXTURE_DECLARED_OPAQUE: u32 = 1 << 31;
const TEXTURE_VOLUME_SRGB: u32 = 1 << 25;
const TEXTURE_CUBE_SRGB: u32 = 1 << 9;

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

#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/lib_runtime_capabilities_tests.rs"]
mod runtime_capabilities_tests;

#[cfg(test)]
include!("../../../../../testsuite/coinwgpu/rust/unit/lib_rtt_capacity_test.rs");

#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/lib_row_copy_tests.rs"]
mod row_copy_tests;

#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/lib_negotiated_device_limits_tests.rs"]
mod negotiated_device_limits_tests;
