#![allow(clippy::not_unsafe_ptr_arg_deref)]
#![allow(clippy::too_many_arguments)]
#![allow(clippy::manual_is_multiple_of)]
#![allow(clippy::if_same_then_else)]
#![allow(clippy::needless_range_loop)]
#![allow(clippy::needless_lifetimes)]
use bytemuck::{Pod, Zeroable};
use pollster::block_on;
use raw_window_handle::{RawDisplayHandle, RawWindowHandle, XlibDisplayHandle, XlibWindowHandle};
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

pub const COIN_WGPU_BRIDGE_PROTOCOL_REVISION: u32 = 31;
pub const COIN_WGPU_ABI_VERSION: u32 = COIN_WGPU_BRIDGE_PROTOCOL_REVISION;

const _: () = {
    assert!(std::mem::size_of::<CoinWgpuFrameView>() == 216);
    assert!(std::mem::size_of::<CoinWgpuShadowDraw>() == 144);
    assert!(std::mem::offset_of!(CoinWgpuFrameView, shadow_casters) == 176);
    assert!(std::mem::offset_of!(CoinWgpuFrameView, shadow_map_size) == 192);
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
}

#[repr(C)]
#[derive(Copy, Clone, Debug, Pod, Zeroable)]
pub struct CoinWgpuVertex {
    pub position: [f32; 3],
    pub normal: [f32; 3],
    pub texcoord: [f32; 2],
    pub material_slot: u32,
    pub screen_space_w: f32,
    pub fog_eye_depth_plus_one: f32,
    pub extra_texcoords: [[f32; 2]; 7],
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
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
#[derive(Copy, Clone, Debug)]
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
    st.texture_combines.iter().all(valid_texture_program)
        && st.extra_textures.iter().all(|t| t.enabled <= 1 && (t.enabled == 0
            || (t.model <= 3 && t.matrix.iter().chain(t.blend_color.iter()).all(|v| v.is_finite()))))
}

#[cfg(test)]
mod texture_payload_tests {
    use super::*;
    #[test]
    fn rejects_malformed_programs_and_extra_unit_values() {
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        assert!(valid_texture_payload(&state));
        let valid = [[1.0, 7.0, 5.0, 0.0], [0.0, 15.0, 4.0, 4.0],
            [8.0, 15.0, 11.0, 2.0], [0.2, 0.4, 0.6, 0.8]];
        state.texture_combines[7] = valid;
        assert!(valid_texture_payload(&state));
        for (r, c, value) in [(0, 0, 2.0), (0, 1, 7.5), (0, 2, 6.0),
            (1, 0, 16.0), (2, 0, 7.0), (1, 3, 3.0), (3, 0, -0.1), (3, 2, f32::NAN)] {
            state.texture_combines[7] = valid;
            state.texture_combines[7][r][c] = value;
            assert!(!valid_texture_payload(&state));
        }
        state.texture_combines[7] = valid;
        state.extra_textures[6].enabled = 1;
        assert!(valid_texture_payload(&state));
        state.extra_textures[6].matrix[12] = f32::INFINITY;
        assert!(!valid_texture_payload(&state));
    }
}

const _: () = {
    assert!(std::mem::size_of::<CoinWgpuTextureUnit>() == 96);
    assert!(std::mem::offset_of!(CoinWgpuVertex, extra_texcoords) == 44);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, extra_textures) == 1096);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, texture_combines) == 1768);
    assert!(std::mem::size_of::<CoinWgpuVertex>() == 100);
    assert!(std::mem::size_of::<CoinWgpuRenderState>() == 2280);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, polygon_offset_max_depth_bits) == 1092);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, polygon_offset_slope_bias) == 1088);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, clip_plane_count) == 956);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, clip_planes) == 960);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, polygon_offset_enabled) == 936);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, depth_test) == 916);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, depth_write) == 920);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, depth_function) == 924);
    assert!(std::mem::offset_of!(CoinWgpuRenderState, depth_range) == 928);
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

#[repr(C)]
#[derive(Copy, Clone, Debug, Pod, Zeroable)]
pub struct CoinWgpuShadowDraw {
    pub first_index: u32,
    pub index_count: u32,
    pub render_state_slot: u32,
    pub reserved: u32,
    pub model_view: [f32; 16],
    pub model_view_projection: [f32; 16],
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
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
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
    topology: u32,
    color_format: wgpu::TextureFormat,
    depth_format: wgpu::TextureFormat,
    sample_count: u32,
    cull_mode: Option<wgpu::Face>,
    front_face: wgpu::FrontFace,
    blend: bool,
    additive: bool,
    peel: bool,
    depth_write: bool,
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
    content_digest: u64,
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
    wrap_s: u32,
    wrap_t: u32,
    filter: u32,
}

#[derive(Default)]
struct SamplerCache {
    entries: HashMap<SamplerKey, wgpu::Sampler>,
}

// The offscreen camera fast path reads geometry only from this Rust-owned
// snapshot. A revision hint alone never licenses dereferencing caller-owned
// geometry without full validation. One snapshot belongs to one device
// generation and is discarded with that device on loss/destruction.
struct ValidatedGeometry {
    vertices: Vec<CoinWgpuVertex>,
    indices: Vec<u32>,
    draws: Vec<CoinWgpuDraw>,
    materials: Vec<CoinWgpuMaterial>,
    max_abs_position: f64,
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

// Device-local bindings for one validated camera scene. Only uniforms change.
struct CameraDrawBinding {
    uniform_buffer: wgpu::Buffer,
    bind_group: wgpu::BindGroup,
}

struct CameraGpuBindings {
    geometry: Arc<ValidatedGeometry>,
    materials_buffer: wgpu::Buffer,
    draws: Vec<Option<CameraDrawBinding>>,
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
struct CachedOffscreenAttachments {
    width: u32,
    height: u32,
    color_texture: wgpu::Texture,
    color_view: wgpu::TextureView,
    depth_texture: wgpu::Texture,
    depth_view: wgpu::TextureView,
}

impl CachedOffscreenAttachments {
    const MAX_BYTES: u64 = 32 * 1024 * 1024;
}

fn same_camera_independent_state(a: &CoinWgpuRenderState, b: &CoinWgpuRenderState) -> bool {
    a.light_direction == b.light_direction && a.light_color == b.light_color
        && a.light_intensity == b.light_intensity && a.has_light == b.has_light
        && a.material_slot == b.material_slot && a.cull_mode == b.cull_mode
        && a.front_face == b.front_face && a.light_model == b.light_model
        && a.texture_matrix == b.texture_matrix && a.has_texture == b.has_texture
        && a.texture_slot == b.texture_slot && a.sampler_slot == b.sampler_slot
        && a.extra_textures == b.extra_textures && a.texture_combines == b.texture_combines
        && a.texture_model == b.texture_model
        && a.texture_blend_color == b.texture_blend_color && a.viewport == b.viewport
        && a.light_count == b.light_count
        && a.ambient_light == b.ambient_light && a.fog_mode == b.fog_mode
        && a.fog_color == b.fog_color && a.fog_start == b.fog_start
        && a.depth_test == b.depth_test && a.depth_write == b.depth_write
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
        })
}

fn camera_states_match(scene: &ValidatedScene, states: &[CoinWgpuRenderState]) -> bool {
    scene.states.len() == states.len() && scene.states.iter().zip(states).all(|(old, next)| {
        if old.light_model != 0 || old.fog_mode != 0 || (old.has_texture != 0 || old.extra_textures.iter().any(|t| t.enabled != 0))
            || !same_camera_independent_state(old, next)
            || !next.model_view.iter().chain(next.model_view_projection.iter())
                .chain(next.normal_matrix.iter()).all(|x| x.is_finite())
            || !next.fog_end.is_finite() {
            return false;
        }
        // Composition preflight rejects non-finite view-space depth. Bound
        // the changed matrix against the previously validated positions.
        let m = &next.model_view;
        let bound = scene.geometry.max_abs_position
            * (f64::from(m[2]).abs() + f64::from(m[6]).abs() + f64::from(m[10]).abs())
            + f64::from(m[14]).abs();
        bound <= f64::from(f32::MAX)
    })
}

fn camera_scene_eligible(
    vertices: &[CoinWgpuVertex], indices: &[u32], draws: &[CoinWgpuDraw],
    materials: &[CoinWgpuMaterial],
    states: &[CoinWgpuRenderState], order: &[composition::CompositionItem],
    textures: &[CoinWgpuTexture], samplers: &[CoinWgpuSampler],
) -> bool {
    const MAX_OWNED_SCENE_BYTES: usize = 32 * 1024 * 1024;
    let owned_bytes = std::mem::size_of_val(vertices)
        .saturating_add(std::mem::size_of_val(indices))
        .saturating_add(std::mem::size_of_val(draws))
        .saturating_add(std::mem::size_of_val(materials))
        .saturating_add(std::mem::size_of_val(states))
        .saturating_add(std::mem::size_of_val(order));
    owned_bytes <= MAX_OWNED_SCENE_BYTES
        && textures.is_empty() && samplers.is_empty()
        && states.iter().all(|s| s.light_model == 0 && s.fog_mode == 0 && s.has_texture == 0 && s.extra_textures.iter().all(|t| t.enabled == 0))
        && materials.iter().all(|m| m.diffuse[3] == 1.0 && m.transparency == 0.0)
        && order.iter().all(|item| !item.blend)
        && vertices.iter().all(|v| v.position.iter().all(|x| x.is_finite()))
}

struct DeviceState {
    adapter: wgpu::Adapter,
    adapter_name: String,
    device: wgpu::Device,
    queue: wgpu::Queue,
    shader_module: wgpu::ShaderModule,
    line_shader_module: wgpu::ShaderModule,
    point_shader_module: wgpu::ShaderModule,
    bind_group_layout: wgpu::BindGroupLayout,
    pipeline_layout: wgpu::PipelineLayout,
    pipelines: Mutex<HashMap<PipelineKey, wgpu::RenderPipeline>>,
    annotation_depth_pipeline: Mutex<Option<wgpu::RenderPipeline>>,
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
    camera_bindings_reused: AtomicU32,
    camera_bindings_created: AtomicU32,
    validated_scene: Option<Arc<ValidatedScene>>,
    readback_pool: Arc<Mutex<ReadbackPool>>,
    cached_offscreen_attachments: Option<CachedOffscreenAttachments>,
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
    let vertex_buffers = geometry.active_entries.len() + geometry.deferred_release.len();
    let index_buffers = geometry.active_entries.values()
        .filter(|(_, entry)| entry.index_buffer.is_some()).count()
        + geometry.deferred_release.iter()
            .filter(|entry| entry.index_buffer.is_some()).count();
    let geometry_active_bytes = geometry.total_active_geometry_bytes;
    drop(geometry);
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
}

struct SurfaceRecord {
    surface: wgpu::Surface<'static>,
    native_desc: CoinWgpuNativeSurfaceDescriptor,
    config: Option<wgpu::SurfaceConfiguration>,
    framebuffer_size: (u32, u32),
    color_format: wgpu::TextureFormat,
    suspended: bool,
    needs_reconfigure: bool,
    surface_generation: u64,
    configured_device_generation: u64,
    depth_texture: Option<wgpu::Texture>,
    depth_view: Option<wgpu::TextureView>,
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
    dead_readbacks: HashMap<u64, CoinWgpuStatus>,
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
pub const FAULT_CACHE_ALLOC_FAIL: i32 = 201;
pub const FAULT_RTT_COLOR_ALLOC: i32 = 301;
pub const FAULT_RTT_COLOR_VIEW: i32 = 302;
pub const FAULT_RTT_DEPTH_ALLOC: i32 = 303;
pub const FAULT_RTT_BIND_GROUP: i32 = 304;

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
        // Xlib
        let display_ptr = desc.handle_a as *mut std::ffi::c_void;
        let window_xid = desc.handle_b;
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
    let mut guard = RUNTIME_CTX.lock().map_err(|e| e.to_string())?;
    if guard.is_none() {
        *guard = Some(RuntimeContext {
            instance: wgpu::Instance::default(),
            surfaces: HashMap::new(),
            device_state: None,
            device_generation: 0,
            pending_readbacks: HashMap::new(),
            retired_readbacks: Vec::new(),
            extra_devices: HashMap::new(),
            extra_generations: HashMap::new(),
            dead_readbacks: HashMap::new(),
        });
    }
    Ok(())
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
            if candidate.get_info().device_type == wgpu::DeviceType::Cpu { return false; }
            target_surface.map_or(true, |surface| {
                let caps = surface.get_capabilities(candidate);
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
    let required_features = if enable_timestamps { timestamp_features }
        else { wgpu::Features::empty() };
    let (device, queue) = block_on(adapter.request_device(
        &wgpu::DeviceDescriptor {
            label: Some("Coin3D WebGPU Device"),
            required_features,
            required_limits: wgpu::Limits::default(),
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
            error_kind.store(if matches!(error, wgpu::Error::OutOfMemory { .. }) { 1 } else { 2 },
                Ordering::SeqCst);
        }));
    } else {
        device.set_device_lost_callback(|reason, message| {
            DEVICE_LOST_OCCURRED.store(true, Ordering::SeqCst);
            if let Ok(mut lock) = LAST_ASYNC_ERROR_MSG.lock() {
                *lock = format!("WebGPU device lost ({:?}): {}", reason, message);
            }
        });
        device.on_uncaptured_error(Box::new(|error: wgpu::Error| {
            LAST_ASYNC_ERROR_KIND.store(
                if matches!(error, wgpu::Error::OutOfMemory { .. }) { 1 } else { 2 },
                Ordering::SeqCst);
            if let Ok(mut lock) = LAST_ASYNC_ERROR_MSG.lock() {
                *lock = format!("WebGPU uncaptured error: {}", error);
            }
        }));
    }

    let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("coin_standard.wgsl"),
        source: wgpu::ShaderSource::Wgsl(WGSL_SHADER.into()),
    });

    let line_shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("coin_line.wgsl"),
        source: wgpu::ShaderSource::Wgsl(WGSL_LINE_SHADER.into()),
    });

    let point_shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("coin_point.wgsl"),
        source: wgpu::ShaderSource::Wgsl(WGSL_POINT_SHADER.into()),
    });

    let storage_visibility = if device.limits().max_storage_buffers_per_shader_stage >= 1 {
        wgpu::ShaderStages::VERTEX_FRAGMENT
    } else {
        wgpu::ShaderStages::FRAGMENT
    };

    let mut layout_entries = vec![
        wgpu::BindGroupLayoutEntry { binding: 0, visibility: wgpu::ShaderStages::VERTEX_FRAGMENT,
            ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform,
                has_dynamic_offset: false, min_binding_size: None }, count: None },
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
                sample_type: wgpu::TextureSampleType::Depth,
                view_dimension: wgpu::TextureViewDimension::D2,
                multisampled: false,
            },
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
        shader_module: shader,
        line_shader_module: line_shader,
        point_shader_module: point_shader,
        bind_group_layout,
        pipeline_layout,
        pipelines: Mutex::new(HashMap::new()),
        annotation_depth_pipeline: Mutex::new(None),
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
        camera_bindings_reused: AtomicU32::new(0),
        camera_bindings_created: AtomicU32::new(0),
        validated_scene: None,
        readback_pool: Arc::new(Mutex::new(ReadbackPool::default())),
        cached_offscreen_attachments: None,
        rtt_textures: Mutex::new(RttRegistry::default()),
        device_id: 0,
        generation: runtime.device_generation,
        completed_serial: Arc::new(AtomicU64::new(0)),
        last_submitted_serial: AtomicU64::new(0),
        lost: lost_signal,
        async_error: error_signal,
        injected_fault: AtomicI32::new(0),
    });

    Ok(runtime.device_state.as_mut().unwrap())
}
fn get_or_init_device<'a>(
    runtime: &'a mut RuntimeContext,
    target_surface: Option<&wgpu::Surface>,
) -> Result<&'a mut DeviceState, String> {
    get_or_init_device_impl(runtime, target_surface, false, 0)

}
fn make_extra_device(id: CoinWgpuDeviceId, generation: u64) -> Result<DeviceState, String> {
    // Only the default context owns surfaces. Every extra context requests its
    // own logical Device/Queue; the physical adapter may be shared.
    let mut isolated = RuntimeContext {
        instance: wgpu::Instance::default(),
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
            runtime.dead_readbacks.insert(*token, CoinWgpuStatus::DeviceLost);
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
        runtime.extra_devices.insert(id, make_extra_device(id, generation)?);
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
        let state = match make_extra_device(id, id) {
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
    record.framebuffer_size = (width, height);
    if width == 0 || height == 0 {
        record.suspended = true;
        record.needs_reconfigure = true;
        record.depth_texture = None;
        record.depth_view = None;
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
        view_formats: vec![],
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
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Depth32Float,
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
        view_formats: &[],
    };
    let depth_texture = device_state.device.create_texture(&depth_desc);
    let depth_view = depth_texture.create_view(&wgpu::TextureViewDescriptor::default());

    record.config = Some(config);
    record.color_format = chosen_format;
    record.depth_texture = Some(depth_texture);
    record.depth_view = Some(depth_view);
    record.suspended = false;
    record.needs_reconfigure = false;
    record.configured_device_generation = device_generation;
    record.surface_generation += 1;

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
    let mut max_depth = 0.0f32;
    for index in &indices[draw.first_index as usize..(draw.first_index + draw.index_count) as usize] {
        let p = vertices[*index as usize].position;
        let m = &st.model_view_projection;
        let z = m[2]*p[0] + m[6]*p[1] + m[10]*p[2] + m[14];
        let w = m[3]*p[0] + m[7]*p[1] + m[11]*p[2] + m[15];
        if !z.is_finite() || !w.is_finite() || w <= 0.0 {
            return Err("Invalid clip depth for resolved polygon bias".to_string());
        }
        let depth = st.depth_range[0] + (z/w) * (st.depth_range[1]-st.depth_range[0]);
        max_depth = max_depth.max(depth.clamp(0.0, 1.0));
    }
    if st.polygon_offset_max_depth_bits != 0 {
        max_depth = f32::from_bits(st.polygon_offset_max_depth_bits - 1);
    }
    let bias = st.polygon_offset_slope_bias + st.polygon_offset_units * d32_depth_quantum(max_depth);
    if !bias.is_finite() { return Err("Invalid resolved polygon depth bias".to_string()); }
    Ok(bias)
}

#[cfg(test)]
mod polygon_depth_tests {
    use super::*;

    #[test]
    fn d32_quantum_tracks_exponent_and_fractional_units() {
        for (depth, expected) in [(0.125, 2.0f32.powi(-26)),
                                  (0.5, 2.0f32.powi(-24)),
                                  (1.0, 2.0f32.powi(-23))] {
            assert_eq!(d32_depth_quantum(depth), expected);
        }
        assert_eq!(d32_depth_quantum(0.0).to_bits(), 1);
        let mut st: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        st.model_view_projection[0] = 1.0;st.model_view_projection[5] = 1.0;
        st.model_view_projection[10] = 1.0;st.model_view_projection[15] = 1.0;
        st.depth_range = [0.0, 1.0];st.polygon_offset_slope_bias = -0.01;
        let mut draw: CoinWgpuDraw = unsafe { std::mem::zeroed() };
        draw.index_count = 2;
        let mut vertices = [CoinWgpuVertex::zeroed(); 2];
        vertices[0].position[2] = 0.1;vertices[1].position[2] = 0.5;
        for units in [-3.5, 3.5] {
            st.polygon_offset_units = units;
            assert_eq!(resolved_polygon_bias(&st, &draw, &vertices, &[0,1]).unwrap(),
                -0.01 + units * 2.0f32.powi(-24));
        }
        st.depth_range = [0.0, 0.25];st.polygon_offset_units = 4.0;
        assert_eq!(resolved_polygon_bias(&st, &draw, &vertices, &[0,1]).unwrap(),
            -0.01 + 4.0 * 2.0f32.powi(-26));
        st.polygon_offset_max_depth_bits = 0.75f32.to_bits() + 1;
        assert_eq!(resolved_polygon_bias(&st, &draw, &vertices, &[0,1]).unwrap(),
            -0.01 + 4.0 * 2.0f32.powi(-24));
        st.polygon_offset_max_depth_bits = f32::from_bits(0.5f32.to_bits() - 1).to_bits() + 1;
        assert_eq!(resolved_polygon_bias(&st, &draw, &vertices, &[0,1]).unwrap(),
            -0.01 + 4.0 * 2.0f32.powi(-25));
        st.polygon_offset_factor = 1.0;
        assert!(resolved_polygon_bias(&st, &draw, &vertices, &[0,1]).is_err());
    }

    #[test]
    fn masks_signs_and_disable_preserve_pipeline_bias() {
        // FFI state contains only scalar/array numeric fields; zero is valid.
        let mut st: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        st.polygon_offset_enabled = 1;
        for primitive in [1, 2, 4] {
            st.polygon_offset_primitive_style = primitive;
            for mask in [1, 2, 4, 7] {
                st.polygon_offset_styles = mask;
                for sign in [-1.0, 1.0] {
                    st.polygon_offset_factor = sign * 2.0;
                    st.polygon_offset_units = sign * 3.6;
                    let bias = polygon_depth_bias(&st, 0).unwrap();
                    assert_eq!(bias.slope_scale, if mask & primitive != 0 { sign * 2.0 } else { 0.0 });
                    assert_eq!(bias.constant, if mask & primitive != 0 { (sign * 4.0) as i32 } else { 0 });
                }
            }
        }
        st.polygon_offset_enabled = 0;
        assert_eq!(polygon_depth_bias(&st, 0).unwrap().constant, 0);
        st.polygon_offset_factor = f32::NAN;
        assert!(polygon_depth_bias(&st, 0).is_err());
    }
}

// A depth-only fullscreen triangle, clipped to the annotation viewport. Loading
// depth here preserves the rest of the target; color is never attached.
fn annotation_depth_clear_pipeline(device: &wgpu::Device) -> wgpu::RenderPipeline {
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
        multisample: wgpu::MultisampleState::default(),
        multiview: None, cache: None,
    })
}

fn get_or_create_pipeline<'a>(
    ctx: &'a DeviceState,
    topology: u32,
    color_format: wgpu::TextureFormat,
    depth_format: wgpu::TextureFormat,
    cull_face: Option<wgpu::Face>,
    front_face: wgpu::FrontFace,
    blend: bool,
    additive: bool,
    peel: bool,
    depth_write: bool,
    depth_compare: wgpu::CompareFunction,
    depth_bias: wgpu::DepthBiasState,
    resolved_depth_bias: bool,
) -> Result<wgpu::RenderPipeline, String> {
    let mut map = ctx.pipelines.lock().map_err(|e| e.to_string())?;

    // Lines and Points have culling disabled by specification
    let (primitive_topology, selected_shader, effective_cull) = match topology {
        1 => (wgpu::PrimitiveTopology::LineList, &ctx.line_shader_module, None),
        2 => (wgpu::PrimitiveTopology::PointList, &ctx.point_shader_module, None),
        _ => (wgpu::PrimitiveTopology::TriangleList, &ctx.shader_module, cull_face),
    };

    let key = PipelineKey {
        topology,
        color_format,
        depth_format,
        sample_count: 1,
        cull_mode: effective_cull,
        front_face,
        blend,
        additive,
        peel,
        depth_write,
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
                format: wgpu::VertexFormat::Float32x2,
                offset: 24,
                shader_location: 2,
            },
            wgpu::VertexAttribute {
                format: wgpu::VertexFormat::Uint32,
                offset: 32,
                shader_location: 3,
            },
            wgpu::VertexAttribute {
                format: wgpu::VertexFormat::Float32,
                offset: 36,
                shader_location: 4,
            },
            wgpu::VertexAttribute {
                format: wgpu::VertexFormat::Float32,
                offset: 40,
                shader_location: 5,
            },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x2, offset: 44, shader_location: 6 },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x2, offset: 52, shader_location: 7 },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x2, offset: 60, shader_location: 8 },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x2, offset: 68, shader_location: 9 },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x2, offset: 76, shader_location: 10 },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x2, offset: 84, shader_location: 11 },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x2, offset: 92, shader_location: 12 },
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
                    src_factor: if additive {
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
        write_mask: wgpu::ColorWrites::ALL,
    })];
    if peel {
        color_targets.push(Some(wgpu::ColorTargetState {
            format: wgpu::TextureFormat::R8Unorm,
            blend: None,
            write_mask: wgpu::ColorWrites::ALL,
        }));
    }
    let pipeline = ctx
        .device
        .create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some(pipeline_label),
            layout: Some(&ctx.pipeline_layout),
            vertex: wgpu::VertexState {
                module: selected_shader,
                entry_point: Some("vs_main"),
                buffers: &[vertex_buffer_layout],
                compilation_options: wgpu::PipelineCompilationOptions::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: selected_shader,
                entry_point: Some(if peel {
                    "fs_peel"
                } else if resolved_depth_bias {
                    "fs_depth_bias"
                } else {
                    "fs_main"
                }),
                targets: &color_targets,
                compilation_options: wgpu::PipelineCompilationOptions::default(),
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
            multisample: wgpu::MultisampleState::default(),
            multiview: None,
            cache: None,
        });

    map.insert(key, pipeline.clone());
    ctx.pipeline_compilations.fetch_add(1, Ordering::Relaxed);
    Ok(pipeline)
}

fn resolved_viewport(state: &CoinWgpuRenderState, width: u32, height: u32)
    -> Result<[i32; 4], (CoinWgpuStatus, String)> {
    let viewport = if state.viewport[2] == 0 && state.viewport[3] == 0 {
        [0, 0, width as i32, height as i32]
    } else { state.viewport };
    let right = i64::from(viewport[0]) + i64::from(viewport[2]);
    let bottom = i64::from(viewport[1]) + i64::from(viewport[3]);
    if viewport[0] < 0 || viewport[1] < 0 || viewport[2] <= 0 || viewport[3] <= 0
        || right > i64::from(width) || bottom > i64::from(height) {
        return Err((CoinWgpuStatus::InvalidArgument,
            format!("Invalid viewport {:?}", viewport)));
    }
    Ok(viewport)
}

// Pure shared command encoder function strictly common to offscreen and window targets
fn encode_frame(
    ctx: &DeviceState,
    clear_color: [f32; 4],
    target_width: u32,
    target_height: u32,
    vertices_slice: &[CoinWgpuVertex],
    indices_slice: &[u32],
    draws_slice: &[CoinWgpuDraw],
    draw_order: &[composition::CompositionItem],
    materials_slice: &[CoinWgpuMaterial],
    states_slice: &[CoinWgpuRenderState],
    textures_slice: &[CoinWgpuTexture],
    samplers_slice: &[CoinWgpuSampler],
    camera_geometry: Option<&Arc<ValidatedGeometry>>,
    timestamp_query: Option<&wgpu::QuerySet>,
    color_view: &wgpu::TextureView,
    color_format: wgpu::TextureFormat,
    depth_view: &wgpu::TextureView,
    depth_texture: &wgpu::Texture,
    peel_passes: u32,
) -> Result<wgpu::CommandBuffer, (CoinWgpuStatus, String)> {
    use wgpu::util::DeviceExt;
    if draw_order.iter().any(|item| item.peel) && !peeling::device_supported(&ctx.device) {
        return Err((
            CoinWgpuStatus::Unsupported,
            "Peeling lacks enabled device formats/attachments; no fallback was applied".into(),
        ));
    }
    // Composition was preflighted before surface acquisition or target allocation.
    // Preflight the entire lighting payload before cache mutation or command encoding.
    for (state_index, state) in states_slice.iter().enumerate() {
        resolved_viewport(state, target_width, target_height)?;
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
                .all(|v| v.is_finite());
            let kind = light.position_type[3];
            let attenuation = light.attenuation_exponent;
            if !finite || (kind != 0.0 && kind != 1.0 && kind != 2.0)
                || light.color_intensity[3] < 0.0
                || attenuation[..3].iter().any(|v| *v < 0.0)
                || (kind != 0.0 && attenuation[..3].iter().all(|v| *v == 0.0)) {
                return Err((CoinWgpuStatus::InvalidArgument,
                    format!("State {} light {} has invalid type or parameters", state_index, light_index)));
            }
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
        if t.format == 1 {
            let entry = rtt_cache.active.get(&t.content_digest).ok_or_else(|| (
                CoinWgpuStatus::InvalidArgument,
                format!("Texture {} has stale or unknown RTT token", t_idx),
            ))?;
            if entry.width != t.width || entry.height != t.height
                || t.reserved != u32::from(entry.opaque)
                || !t.pixels.is_null() || t.pixel_bytes_len != 0 {
                return Err((CoinWgpuStatus::InvalidArgument,
                    format!("Texture {} RTT token metadata mismatch", t_idx)));
            }
            continue;
        }
        if t.format != 0 {
            return Err((CoinWgpuStatus::Unsupported,
                format!("Texture {} format is unsupported", t_idx)));
        }
        let expected_bytes = match (t.width as u64).checked_mul(t.height as u64).and_then(|x| x.checked_mul(4)) {
            Some(sz) => sz,
            None => {
                return Err((
                    CoinWgpuStatus::InvalidArgument,
                    format!("Texture {} dimensions cause overflow", t_idx),
                ));
            }
        };
        if t.pixel_bytes_len != expected_bytes || t.pixels.is_null() {
            return Err((
                CoinWgpuStatus::InvalidArgument,
                format!("Texture {} pixel buffer mismatch: expected {} bytes, got {}", t_idx, expected_bytes, t.pixel_bytes_len),
            ));
        }

        let pixel_bytes = unsafe { std::slice::from_raw_parts(t.pixels, t.pixel_bytes_len as usize) };
        let key = TextureKey {
            width: t.width,
            height: t.height,
            format: t.format,
            content_digest: t.content_digest,
        };

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
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });

        ctx.queue.write_texture(
            wgpu::ImageCopyTexture {
                texture: &wgpu_tex,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            pixel_bytes,
            wgpu::ImageDataLayout {
                offset: 0,
                bytes_per_row: Some(t.width * 4),
                rows_per_image: Some(t.height),
            },
            wgpu::Extent3d {
                width: t.width,
                height: t.height,
                depth_or_array_layers: 1,
            },
        );

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
        let key = SamplerKey {
            wrap_s: s.wrap_s,
            wrap_t: s.wrap_t,
            filter: s.filter,
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
                address_mode_u: addr_u,
                address_mode_v: addr_v,
                address_mode_w: wgpu::AddressMode::Repeat,
                mag_filter: filter_mode,
                min_filter: filter_mode,
                mipmap_filter: wgpu::FilterMode::Nearest,
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

    for (v_idx, v) in vertices_slice.iter().enumerate() {
        if !v.extra_texcoords.iter().flatten().all(|x| x.is_finite())
            || !v.screen_space_w.is_finite() || v.screen_space_w < 0.0
            || !v.fog_eye_depth_plus_one.is_finite() || v.fog_eye_depth_plus_one < 0.0 {
            return Err((CoinWgpuStatus::InvalidArgument,
                format!("Vertex {} has invalid homogeneous stroke attributes", v_idx)));
        }
        if (v.material_slot as usize) >= materials_slice.len() {
            return Err((CoinWgpuStatus::InvalidArgument, format!("Vertex {} references invalid material_slot {} >= material_count {}", v_idx, v.material_slot, materials_slice.len())));
        }
    }

    let gpu_materials: Vec<GpuMaterial> = materials_slice.iter().map(|m| {
        GpuMaterial {
            ambient: m.ambient,
            diffuse: m.diffuse,
            specular: m.specular,
            emission: m.emission,
            params: [m.shininess, m.transparency, 0.0, 0.0],
        }
    }).collect();

    // WebGPU does not permit a zero-sized storage binding. This sentinel is
    // unreachable because there are no draws in a material-free frame.
    let fallback_material = [GpuMaterial {
        ambient: [0.0; 4],
        diffuse: [0.0; 4],
        specular: [0.0; 4],
        emission: [0.0; 4],
        params: [0.0; 4],
    }];
    let material_bytes: &[u8] = if gpu_materials.is_empty() {
        bytemuck::cast_slice(&fallback_material)
    } else {
        bytemuck::cast_slice(&gpu_materials)
    };

    // Keep the experiment switch process-local so the same binary can be
    // benchmarked against its previous behavior. It is opt-in, bounded and
    // applies only to the already-validated, untextured camera patch.
    static CAMERA_GPU_CACHE_ENABLED: OnceLock<bool> = OnceLock::new();
    let camera_geometry = camera_geometry.filter(|_| {
        *CAMERA_GPU_CACHE_ENABLED.get_or_init(|| {
            std::env::var("COIN_WGPU_CAMERA_BINDINGS").as_deref() == Ok("1")
        })
        && !draws_slice.is_empty()
        && !materials_slice.is_empty()
        && draws_slice.len() <= 512
        && mat_buffer_size.saturating_add(draws_slice.len()
            .saturating_mul(std::mem::size_of::<CoinWgpuUniforms>())) <= 4 * 1024 * 1024
    });
    let mut camera_bindings = ctx.camera_bindings.lock().unwrap();
    if !camera_geometry.is_some_and(|geometry| camera_bindings.as_ref()
        .is_some_and(|entry| Arc::ptr_eq(&entry.geometry, geometry))) {
        *camera_bindings = None;
    }
    let materials_buffer = if let Some(entry) = camera_bindings.as_ref() {
        entry.materials_buffer.clone()
    } else {
        let buffer = ctx.device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("Frame Materials Storage Buffer"),
            contents: material_bytes,
            usage: wgpu::BufferUsages::STORAGE,
        });
        if let Some(geometry) = camera_geometry {
            *camera_bindings = Some(CameraGpuBindings {
                geometry: geometry.clone(),
                materials_buffer: buffer.clone(),
                draws: (0..draws_slice.len()).map(|_| None).collect(),
            });
        }
        buffer
    };

    // Collect keys active in the current frame to strictly protect them from LRU eviction
    let mut frame_active_keys = std::collections::HashSet::new();
    for draw in draws_slice {
        if draw.index_count > 0 && draw.stable_node_id != 0 {
            frame_active_keys.insert((draw.stable_node_id, draw.draw_ordinal));
        }
    }

    // 2. Prepare uncached fallback buffers if there are any legacy/uncached draws
    let uncached_v_buffer = if draws_slice.iter().any(|d| d.stable_node_id == 0) && !vertices_slice.is_empty() {
        Some(
            ctx.device
                .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                    label: Some("Coin Uncached Vertex Buffer"),
                    contents: bytemuck::cast_slice(vertices_slice),
                    usage: wgpu::BufferUsages::VERTEX,
                }),
        )
    } else {
        None
    };

    let uncached_i_buffer = if draws_slice.iter().any(|d| d.stable_node_id == 0 && d.index_count > 0)
        && !indices_slice.is_empty()
    {
        Some(
            ctx.device
                .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                    label: Some("Coin Uncached Index Buffer"),
                    contents: bytemuck::cast_slice(indices_slice),
                    usage: wgpu::BufferUsages::INDEX,
                }),
        )
    } else {
        None
    };

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
            let v_buf = ctx.device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("Coin Cached Draw Vertex Buffer"),
                contents: bytemuck::cast_slice(draw_vertices),
                usage: wgpu::BufferUsages::VERTEX,
            });
            let i_buf = ctx.device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
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

    {
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
                    load: wgpu::LoadOp::Clear(1.0),
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
        peeling::Peeling::new(
            &ctx.device,
            target_width,
            target_height,
            color_format,
            peel_passes as usize,
        )
    });
    let passes = composition::passes(draw_order, draws_slice);
    let depth_clear_pipeline = if draws_slice.iter().any(|draw| draw.clear_depth_before != 0) {
        let mut cached = ctx.annotation_depth_pipeline.lock().unwrap();
        Some(
            cached
                .get_or_insert_with(|| annotation_depth_clear_pipeline(&ctx.device))
                .clone(),
        )
    } else {
        None
    };
    for (pass_index, range) in passes.iter().enumerate() {
        if let Some(item) = draw_order.get(range.start) {
            let draw = &draws_slice[item.draw_index];
            if draw.clear_depth_before != 0 {
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
        if is_peel {
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
        for peel_step in 0..if is_peel { peel_passes as usize } else { 1 } {
            let selected_color = if is_peel {
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
                resolve_target: None,
                ops: wgpu::Operations {
                    load: if is_peel {
                        wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT)
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
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("Coin Composition Pass"),
                color_attachments: &color_attachments,
                depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                    view: selected_depth,
                    depth_ops: Some(wgpu::Operations {
                        load: if is_peel {
                            wgpu::LoadOp::Clear(1.0)
                        } else {
                            wgpu::LoadOp::Load
                        },
                        store: wgpu::StoreOp::Store,
                    }),
                    stencil_ops: None,
                }),
                timestamp_writes: if !is_peel && pass_index + 1 == passes.len() {
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
                let resolved_depth_bias = st.polygon_offset_enabled != 0
                    && st.polygon_offset_styles & st.polygon_offset_primitive_style != 0
                    && (st.polygon_offset_slope_bias != 0.0
                        || (st.polygon_offset_max_depth_bits != 0
                            && st.polygon_offset_units != 0.0));
                let native_depth_bias = polygon_depth_bias(st, draw.topology).map_err(|e| {
                    (
                        CoinWgpuStatus::InvalidArgument,
                        format!("Draw {}: {}", item.draw_index, e),
                    )
                })?;
                let window_bias = if resolved_depth_bias {
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
                let pipeline = match get_or_create_pipeline(
                    ctx,
                    draw.topology,
                    if is_peel {
                        wgpu::TextureFormat::Rgba16Float
                    } else {
                        color_format
                    },
                    wgpu::TextureFormat::Depth32Float,
                    cull_face,
                    front_face,
                    item.blend && !is_peel,
                    item.additive,
                    is_peel,
                    is_peel || st.depth_write != 0,
                    if is_peel {
                        wgpu::CompareFunction::LessEqual
                    } else {
                        depth_compare
                    },
                    depth_bias,
                    resolved_depth_bias,
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
                    .iter()
                    .map(|layer| {
                        if layer.enabled == 0 {
                            return (&ctx.default_texture_view, &ctx.default_sampler);
                        }
                        let t = &textures_slice[layer.texture_slot as usize];
                        let s = &samplers_slice[layer.sampler_slot as usize];
                        let tk = TextureKey {
                            width: t.width,
                            height: t.height,
                            format: t.format,
                            content_digest: t.content_digest,
                        };
                        let sk = SamplerKey {
                            wrap_s: s.wrap_s,
                            wrap_t: s.wrap_t,
                            filter: s.filter,
                        };
                        let view = if t.format == 1 {
                            &rtt_cache
                                .active
                                .get(&t.content_digest)
                                .expect("RTT token preflighted before encoding")
                                .view
                        } else {
                            tex_cache
                                .entries
                                .get(&tk)
                                .map(|e| &e.view)
                                .unwrap_or(&ctx.default_texture_view)
                        };
                        (
                            view,
                            samp_cache.entries.get(&sk).unwrap_or(&ctx.default_sampler),
                        )
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
                        t.enabled as f32,
                        t.model as f32,
                        if t.enabled != 0 && textures_slice[t.texture_slot as usize].format == 1 {
                            1.0
                        } else {
                            0.0
                        },
                        0.0,
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

                let uniforms = CoinWgpuUniforms {
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
                        st.depth_write as f32,
                    ],
                    tex_params: [
                        if st.has_texture != 0 { 1.0 } else { 0.0 },
                        st.texture_model as f32,
                        if st.has_texture != 0
                            && textures_slice[st.texture_slot as usize].format == 1
                        {
                            1.0
                        } else {
                            0.0
                        },
                        0.0,
                    ],
                    ambient_light: st.ambient_light,
                    light_meta: [st.light_count as f32, 0.0, 0.0, 0.0],
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
                };

                if layers.iter().any(|t| t.enabled != 0)
                    && FAULT_INJECTION.load(Ordering::SeqCst) == FAULT_RTT_BIND_GROUP
                {
                    return Err((
                        CoinWgpuStatus::OutOfMemory,
                        "Injected RTT bind-group creation failure".to_string(),
                    ));
                }
                let create_binding = |persistent: bool| {
                    let u_buffer =
                        ctx.device
                            .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                                label: Some("Draw Uniform Buffer"),
                                contents: bytemuck::bytes_of(&uniforms),
                                usage: if persistent {
                                    wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST
                                } else {
                                    wgpu::BufferUsages::UNIFORM
                                },
                            });
                    let mut entries = vec![
                        wgpu::BindGroupEntry {
                            binding: 0,
                            resource: u_buffer.as_entire_binding(),
                        },
                        wgpu::BindGroupEntry {
                            binding: 1,
                            resource: materials_buffer.as_entire_binding(),
                        },
                    ];
                    for (unit, (view, sampler)) in texture_bindings.iter().enumerate() {
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
                    } else {
                        &ctx.default_depth_view
                    };
                    let opaque = if is_peel {
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
                    let bind_group = ctx.device.create_bind_group(&wgpu::BindGroupDescriptor {
                        label: Some("Draw eight-unit texture bindings"),
                        layout: &ctx.bind_group_layout,
                        entries: &entries,
                    });
                    CameraDrawBinding {
                        uniform_buffer: u_buffer,
                        bind_group,
                    }
                };
                // Queue writes are submitted before this frame's command buffer.
                // They cannot alter a preceding submission; wgpu keeps the
                // underlying resources alive while that submission is in flight.
                let transient_binding;
                let binding = if let Some(entry) = camera_bindings.as_mut() {
                    let slot = &mut entry.draws[item.draw_index];
                    if slot.is_none() {
                        *slot = Some(create_binding(true));
                        ctx.camera_bindings_created.fetch_add(1, Ordering::Relaxed);
                    } else {
                        ctx.queue.write_buffer(
                            &slot.as_ref().unwrap().uniform_buffer,
                            0,
                            bytemuck::bytes_of(&uniforms),
                        );
                        ctx.camera_bindings_reused.fetch_add(1, Ordering::Relaxed);
                    }
                    slot.as_ref().unwrap()
                } else {
                    transient_binding = create_binding(false);
                    &transient_binding
                };
                pass.set_bind_group(0, &binding.bind_group, &[]);
                let viewport = resolved_viewport(st, target_width, target_height)?;
                if !st.depth_range[0].is_finite()
                    || !st.depth_range[1].is_finite()
                    || st.depth_range[0] < 0.0
                    || st.depth_range[1] > 1.0
                    || st.depth_range[0] > st.depth_range[1]
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
                    if resolved_depth_bias || is_peel {
                        0.0
                    } else {
                        st.depth_range[0]
                    },
                    if resolved_depth_bias || is_peel {
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
                    if let Some(vb) = &uncached_v_buffer {
                        pass.set_vertex_buffer(0, vb.slice(..));
                    }
                    if let Some(ib) = &uncached_i_buffer {
                        pass.set_index_buffer(ib.slice(..), wgpu::IndexFormat::Uint32);
                    }
                    let start_idx = draw.first_index;
                    let end_idx = start_idx + draw.index_count;
                    pass.draw_indexed(start_idx..end_idx, 0, 0..1);
                }
            }
        }
        if is_peel {
            peeling.as_ref().unwrap().composite(
                &mut encoder,
                color_view,
                depth_view,
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

    Ok(encoder.finish())
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
        unsafe {
            std::ptr::write(output, result);
        }
        CoinWgpuStatus::Ok
    })
    .unwrap_or(CoinWgpuStatus::BackendError)
}

#[cfg(test)]
mod runtime_capabilities_tests {
    use super::*;
    #[test]
    fn rejects_invalid_output_without_a_probe() {
        assert_eq!(
            coin_wgpu_query_runtime_capabilities(std::ptr::null_mut(), 56),
            CoinWgpuStatus::InvalidArgument
        );
        let mut caps = CoinWgpuRuntimeCapabilities::default();
        assert_eq!(
            coin_wgpu_query_runtime_capabilities(&mut caps, 55),
            CoinWgpuStatus::InvalidArgument
        );
        assert_eq!(caps.struct_size, 0);
    }
    #[test]
    fn format_bits_keep_sampling_and_storage_access_separate() {
        let features = wgpu::TextureFormatFeatures {
            allowed_usages: wgpu::TextureUsages::RENDER_ATTACHMENT
                | wgpu::TextureUsages::STORAGE_BINDING,
            flags: wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X4
                | wgpu::TextureFormatFeatureFlags::STORAGE_READ_ONLY,
        };
        assert_eq!(portable_format_features(features), 2 | 4 | 8);
    }
}

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
            surface,
            native_desc: inf.native,
            config: None,
            framebuffer_size: (inf.width, inf.height),
            color_format: wgpu::TextureFormat::Bgra8Unorm,
            suspended: (inf.width == 0 || inf.height == 0),
            needs_reconfigure: true,
            surface_generation: 0,
            configured_device_generation: 0,
            depth_texture: None,
            depth_view: None,
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
                return CoinWgpuStatus::BackendError;
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

        if width == 0 || height == 0 {
            record.framebuffer_size = (width, height);
            record.suspended = true;
            record.needs_reconfigure = true;
            record.depth_texture = None;
            record.depth_view = None;
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

        if f.shadow_caster_count != 0 {
            set_error(error_buf, error_buf_len,
                "Shadow caster transport is present, but the wgpu VSM encoder is not connected");
            return CoinWgpuStatus::Unsupported;
        }
        if !f.shadow_casters.is_null() || f.shadow_map_size != 0 {
            set_error(error_buf, error_buf_len,
                "Shadow payload has a pointer or map size without casters");
            return CoinWgpuStatus::InvalidArgument;
        }

        // 4. Dimensions validation: zero dimensions = suspended -> NOT_READY
        if f.width == 0 || f.height == 0 {
            return CoinWgpuStatus::NotReady;
        }

        // 5. Preflight slice bounds and alignment validation BEFORE acquire
        let vertices_slice = match validate_slice(
            f.vertices,
            f.vertex_count,
            "vertices",
            error_buf,
            error_buf_len,
        ) {
            Ok(s) => s,
            Err(st) => return st,
        };
        let indices_slice = match validate_slice(
            f.indices,
            f.index_count,
            "indices",
            error_buf,
            error_buf_len,
        ) {
            Ok(s) => s,
            Err(st) => return st,
        };
        let draws_slice =
            match validate_slice(f.draws, f.draw_count, "draws", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            };
        let materials_slice = match validate_slice(
            f.materials,
            f.material_count,
            "materials",
            error_buf,
            error_buf_len,
        ) {
            Ok(s) => s,
            Err(st) => return st,
        };
        let states_slice =
            match validate_slice(f.states, f.state_count, "states", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            };

        let textures_slice = if f.texture_count > 0 {
            match validate_slice(f.textures, f.texture_count, "textures", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            }
        } else {
            &[]
        };

        let samplers_slice = if f.sampler_count > 0 {
            match validate_slice(f.samplers, f.sampler_count, "samplers", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            }
        } else {
            &[]
        };

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
        let draw_order = match composition::order(
            vertices_slice, indices_slice, draws_slice, materials_slice, states_slice, textures_slice) {
            Ok(order) => order,
            Err((status, message)) => {
                set_error(error_buf, error_buf_len, &message);
                return status;
            }
        };

        if let Err((status, message)) = peeling::validate_request(
            f.sorted_layers_passes,
            f.transparency_reserved,
            f.transparency_budget_bytes,
            f.width,
            f.height,
            draw_order.iter().any(|item| item.peel),
        ) {
            set_error(error_buf, error_buf_len, &message);
            return status;
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

        let record = runtime.surfaces.get_mut(&surface_id).unwrap();

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
            if !record.config.as_ref().is_some_and(|config|
                config.usage.contains(wgpu::TextureUsages::COPY_SRC)) {
                set_error(error_buf, error_buf_len, "Window surface does not support COPY_SRC readback");
                return CoinWgpuStatus::Unsupported;
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
        // 8. Acquire texture with strictly 1 recovery attempt
        let mut acquire_result = if fault == FAULT_SURFACE_OUTDATED_ONCE
            && FAULT_SURFACE_OUTDATED_COUNT.fetch_add(1, Ordering::SeqCst) == 0
        {
            Err(wgpu::SurfaceError::Outdated)
        } else if fault == FAULT_SURFACE_LOST_ONCE
            && FAULT_SURFACE_LOST_COUNT.fetch_add(1, Ordering::SeqCst) == 0
        {
            Err(wgpu::SurfaceError::Lost)
        } else if fault == FAULT_SURFACE_LOST_PERSISTENT {
            Err(wgpu::SurfaceError::Lost)
        } else {
            record.surface.get_current_texture()
        };

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
                    acquire_result = if fault == FAULT_SURFACE_LOST_PERSISTENT {
                        Err(wgpu::SurfaceError::Lost)
                    } else {
                        record.surface.get_current_texture()
                    };
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
                            acquire_result = if fault == FAULT_SURFACE_LOST_PERSISTENT {
                                Err(wgpu::SurfaceError::Lost)
                            } else {
                                record.surface.get_current_texture()
                            };
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

        let profile_acquired = trace_phases.then(std::time::Instant::now);
        // 9. Encode frame into acquired texture view
        let color_view = surface_texture
            .texture
            .create_view(&wgpu::TextureViewDescriptor::default());
        let depth_view = match &record.depth_view {
            Some(v) => v,
            None => {
                set_error(error_buf, error_buf_len, "Surface depth view not allocated");
                return CoinWgpuStatus::BackendError;
            }
        };

        let cmd_buffer = match encode_frame(
            dev,
            f.clear_color,
            f.width,
            f.height,
            vertices_slice,
            indices_slice,
            draws_slice,
            &draw_order,
            materials_slice,
            states_slice,
            textures_slice,
            samplers_slice,
            None,
            None,
            &color_view,
            record.color_format,
            depth_view,
            record.depth_texture.as_ref().unwrap(),
            f.sorted_layers_passes,
        ) {
            Ok(cmd) => cmd,
            Err((status, msg)) => {
                set_error(error_buf, error_buf_len, &msg);
                // Texture is dropped without calling present()
                return status;
            }
        };

        let mut capture_buffer = None;
        let mut commands = vec![cmd_buffer];
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
                    texture: &surface_texture.texture,
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
        dev.queue.submit(commands);
        let completed = dev.completed_serial.clone();
        dev.queue.on_submitted_work_done(move || {
            completed.fetch_max(local_serial, Ordering::SeqCst);
            GLOBAL_COMPLETED_SERIAL.fetch_max(sub_serial, Ordering::SeqCst);
        });
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
            let bgra = matches!(record.color_format, wgpu::TextureFormat::Bgra8Unorm |
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
        surface_texture.present();
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
            eprintln!("COIN_RENDER_PHASE rust_surface_cpu total_ms={:.6} validation_ms={:.6} acquire_ms={:.6} encode_ms={:.6} submit_present_ms={:.6} capture_requested={} capture_staging_bytes={} queue_submissions=1 command_buffers={} gpu_timing=unavailable",
                duration_ms(start, presented), duration_ms(start, validated),
                duration_ms(validated, acquired), duration_ms(acquired, encoded),
                duration_ms(encoded, presented), u8::from(!readback_rgba.is_null()),
                capture_bytes, if readback_rgba.is_null() { 1 } else { 2 });
            trace_owned_resources(dev, "window", f.width, f.height, capture_bytes, 0);
            let info = dev.adapter.get_info();
            let renderer = match info.backend {
                wgpu::Backend::Vulkan => "vulkan",
                wgpu::Backend::Gl => "opengl",
                wgpu::Backend::Metal => "metal",
                wgpu::Backend::Dx12 => "dx12",
                _ => "other",
            };
            eprintln!("COIN_RENDER_PHASE wgpu_surface renderer={} vendor_id={:#x} device_id={:#x} device_type={:?} surface={} serial={} size={}x{}",
                renderer, info.vendor, info.device, info.device_type,
                surface_id, sub_serial, f.width, f.height);
        }
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
            ticket.token = 0;
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

        if f.shadow_caster_count != 0 {
            set_error(error_buf, error_buf_len,
                "Shadow caster transport is present, but the wgpu VSM encoder is not connected");
            return CoinWgpuStatus::Unsupported;
        }
        if !f.shadow_casters.is_null() || f.shadow_map_size != 0 {
            set_error(error_buf, error_buf_len,
                "Shadow payload has a pointer or map size without casters");
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
        let states_slice =
            match validate_slice(f.states, f.state_count, "states", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            };

        let owned_patch = if out_texture.is_null() && f.camera_base_revision != 0
            && f.frame_revision != 0 && f.frame_revision != f.camera_base_revision
            && f.texture_count == 0 && f.sampler_count == 0 {
            RUNTIME_CTX.lock().ok().and_then(|guard| {
                let runtime = guard.as_ref()?;
                let device = if tgt.device_id == 0 {
                    runtime.device_state.as_ref()
                } else {
                    runtime.extra_devices.get(&tgt.device_id)
                }?;
                let scene = device.validated_scene.as_ref()?;
                let geometry = &scene.geometry;
                (scene.revision == f.camera_base_revision
                    && scene.generation == device.generation
                    && scene.width == width && scene.height == height
                    && scene.clear_color == f.clear_color
                    && geometry.vertices.len() as u64 == f.vertex_count
                    && geometry.indices.len() as u64 == f.index_count
                    && geometry.draws.len() as u64 == f.draw_count
                    && geometry.materials.len() as u64 == f.material_count
                    && camera_states_match(scene, states_slice))
                    .then(|| scene.clone())
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
        let materials_slice = if let Some(scene) = owned_patch.as_ref() {
            scene.geometry.materials.as_slice()
        } else {
            match validate_slice(f.materials, f.material_count, "materials", error_buf, error_buf_len) {
                Ok(s) => s, Err(st) => return st,
            }
        };

        let textures_slice = if owned_patch.is_some() { &[] } else if f.texture_count > 0 {
            match validate_slice(f.textures, f.texture_count, "textures", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            }
        } else {
            &[]
        };

        let samplers_slice = if owned_patch.is_some() { &[] } else if f.sampler_count > 0 {
            match validate_slice(f.samplers, f.sampler_count, "samplers", error_buf, error_buf_len) {
                Ok(s) => s,
                Err(st) => return st,
            }
        } else {
            &[]
        };

        // 6. Strict validation and composition are immutable for one private CoinRenderFramePlan revision.
        let cached_draw_order = if owned_patch.is_none() && f.frame_revision != 0 {
            VALIDATED_FRAME_CACHE.lock().ok().and_then(|cache| {
                cache.as_ref().and_then(|(revision, order)| {
                    if *revision == f.frame_revision { Some(order.clone()) } else { None }
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
        if f.frame_revision != 0 {
            if let Ok(mut cache) = VALIDATED_FRAME_CACHE.lock() {
                *cache = Some((f.frame_revision, computed_draw_order.clone()));
            }
        }
        fully_validated = true;
        computed_draw_order
        };

        if let Err((status, message)) = peeling::validate_request(
            f.sorted_layers_passes,
            f.transparency_reserved,
            f.transparency_budget_bytes,
            f.width,
            f.height,
            draw_order.iter().any(|item| item.peel),
        ) {
            set_error(error_buf, error_buf_len, &message);
            return status;
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
            if runtime.pending_readbacks.len() + runtime.retired_readbacks.len() >= 16 {
                set_error(error_buf, error_buf_len, "Too many pending readbacks");
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
        // 8. A completed synchronous frame may lend its attachments to the
        // next synchronous frame on this device. Async and RTT submissions
        // are excluded: their textures can still be in use by the GPU.
        let attachment_cache_enabled = out_ticket.is_null() && out_texture.is_null()
            && std::env::var("COIN_WGPU_ATTACHMENT_CACHE").as_deref() == Ok("1")
            && (width as u64).checked_mul(height as u64)
                .and_then(|pixels| pixels.checked_mul(8))
                .is_some_and(|bytes| bytes <= CachedOffscreenAttachments::MAX_BYTES);
        let cached = if out_ticket.is_null() && out_texture.is_null() {
            ctx.cached_offscreen_attachments.take()
                .filter(|entry| attachment_cache_enabled
                    && entry.width == width && entry.height == height)
        } else { None };
        let attachments_reused = cached.is_some();
        let (color_texture, color_view, depth_texture, depth_view) = if let Some(entry) = cached {
            (entry.color_texture, entry.color_view, entry.depth_texture, entry.depth_view)
        } else {
            let texture_desc = wgpu::TextureDescriptor {
                label: Some("Offscreen Color Texture"),
                size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
                mip_level_count: 1,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format: wgpu::TextureFormat::Rgba8Unorm,
                usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC | wgpu::TextureUsages::TEXTURE_BINDING,
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
            let color_view = color_texture.create_view(&wgpu::TextureViewDescriptor::default());

            let depth_desc = wgpu::TextureDescriptor {
                label: Some("Offscreen Depth Texture"),
                size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
                mip_level_count: 1,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format: wgpu::TextureFormat::Depth32Float,
                usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
                view_formats: &[],
            };
            let depth_texture = ctx.device.create_texture(&depth_desc);
            if fault == FAULT_RTT_DEPTH_ALLOC && !out_texture.is_null() {
                set_error(error_buf, error_buf_len, "Injected RTT depth attachment failure");
                return CoinWgpuStatus::OutOfMemory;
            }
            let depth_view = depth_texture.create_view(&wgpu::TextureViewDescriptor::default());
            (color_texture, color_view, depth_texture, depth_view)
        };
        let profile_attachments_created = trace_phases.then(std::time::Instant::now);

        // 9. Encode draw calls with the exact same shared encode_frame
        let cmd_buffer = match encode_frame(
            ctx,
            f.clear_color,
            f.width,
            f.height,
            vertices_slice,
            indices_slice,
            draws_slice,
            &draw_order,
            materials_slice,
            states_slice,
            textures_slice,
            samplers_slice,
            owned_patch.as_ref().map(|scene| &scene.geometry),
            gpu_probe.as_ref().map(|probe| &probe.queries),
            &color_view,
            wgpu::TextureFormat::Rgba8Unorm,
            &depth_view,
            &depth_texture,
            f.sorted_layers_passes,
        ) {
            Ok(cmd) => cmd,
            Err((status, msg)) => {
                set_error(error_buf, error_buf_len, &msg);
                return status;
            }
        };
        let profile_draw_encoded = trace_phases.then(std::time::Instant::now);
        if f.frame_revision != 0 && (owned_patch.is_some() || fully_validated) {
            let next_scene = if let Some(scene) = owned_patch.as_ref() {
                Some(Arc::new(ValidatedScene {
                    revision: f.frame_revision,
                    generation: ctx.generation,
                    width,
                    height,
                    clear_color: f.clear_color,
                    geometry: scene.geometry.clone(),
                    states: states_slice.to_vec(),
                    draw_order: draw_order.clone(),
                }))
            } else if camera_scene_eligible(vertices_slice, indices_slice, draws_slice,
                materials_slice,
                states_slice, &draw_order, textures_slice, samplers_slice) {
                let max_abs_position = vertices_slice.iter()
                    .flat_map(|v| v.position).map(|x| f64::from(x).abs())
                    .fold(0.0_f64, f64::max);
                Some(Arc::new(ValidatedScene {
                    revision: f.frame_revision,
                    generation: ctx.generation,
                    width,
                    height,
                    clear_color: f.clear_color,
                    geometry: Arc::new(ValidatedGeometry {
                        vertices: vertices_slice.to_vec(),
                        indices: indices_slice.to_vec(),
                        draws: draws_slice.to_vec(),
                        materials: materials_slice.to_vec(),
                        max_abs_position,
                    }),
                    states: states_slice.to_vec(),
                    draw_order: draw_order.clone(),
                }))
            } else { None };
            ctx.validated_scene = next_scene;
            if owned_patch.is_some() {
                if let Ok(mut cache) = VALIDATED_FRAME_CACHE.lock() {
                    *cache = Some((f.frame_revision, draw_order.clone()));
                }
            }
        }
        let profile_encoded = std::time::Instant::now();

        if !out_texture.is_null() {
            let token = NEXT_RTT_TOKEN.fetch_add(1, Ordering::SeqCst);
            if token == 0 {
                set_error(error_buf, error_buf_len, "RTT token space exhausted");
                return CoinWgpuStatus::OutOfMemory;
            }
            let serial = GLOBAL_SUBMISSION_SERIAL.fetch_add(1, Ordering::SeqCst);
            LAST_SUBMITTED_SERIAL.store(serial, Ordering::SeqCst);
            let local_serial = ctx.last_submitted_serial.fetch_add(1, Ordering::SeqCst) + 1;
            ctx.queue.submit([cmd_buffer]);
            let completed = ctx.completed_serial.clone();
            ctx.queue.on_submitted_work_done(move || {
                completed.fetch_max(local_serial, Ordering::SeqCst);
                GLOBAL_COMPLETED_SERIAL.fetch_max(serial, Ordering::SeqCst);
            });
            ctx.rtt_textures.lock().unwrap().active.insert(token, RttTexture {
                texture: color_texture,
                opaque: f.clear_color[3] >= 1.0,
                view: color_view,
                width,
                height,
            });
            tgt.submission_serial = serial;
            unsafe { *out_texture = token; }
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
        let depth_requested = tgt.depth_buffer_len >= (width as u64) * (height as u64)
            && (!out_ticket.is_null() || !tgt.depth_buffer.is_null());
        let depth_staging_info = if depth_requested
        {
            let (dbuf, _) = ctx.readback_pool
                .lock().unwrap().acquire(&ctx.device, depth_staging_size);
            copy_encoder.copy_texture_to_buffer(
                wgpu::TexelCopyTextureInfo {
                    texture: &depth_texture,
                    mip_level: 0,
                    origin: wgpu::Origin3d::ZERO,
                    aspect: wgpu::TextureAspect::DepthOnly,
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
        ctx.queue.submit([cmd_buffer, copy_encoder.finish()]);
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
                tgt.submission_serial = sub_serial;
                if attachment_cache_enabled {
                    ctx.cached_offscreen_attachments = Some(CachedOffscreenAttachments {
                        width, height, color_texture, color_view, depth_texture, depth_view,
                    });
                }
                if trace_phases {
                    let profile_done = std::time::Instant::now();
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
    coin_wgpu_submit_internal(target, frame, std::ptr::null_mut(), std::ptr::null_mut(), error_buf, error_buf_len)
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
    coin_wgpu_submit_internal(target, frame, std::ptr::null_mut(), out_token, error_buf, error_buf_len)
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
#[test]
fn rtt_capacity_has_an_exact_saturating_boundary() {
    assert_eq!(rtt_remaining_capacity(0), 64);
    assert_eq!(rtt_remaining_capacity(63), 1);
    assert_eq!(rtt_remaining_capacity(64), 0);
    assert_eq!(rtt_remaining_capacity(65), 0);
}

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
    coin_wgpu_submit_internal(target, frame, out_ticket, std::ptr::null_mut(), error_buf, error_buf_len)
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
    if let Some(status) = runtime.dead_readbacks.remove(&token) {
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
                if let Ok(pipelines) = dev.pipelines.lock() {
                    out.pipeline_compilations = dev.pipeline_compilations.load(Ordering::Relaxed);
                    out.pipeline_active_entries = pipelines.len() as u64;
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
mod row_copy_tests {
    use super::copy_color_rows;

    #[test]
    fn contiguous_rows_copy_exactly() {
        let src: Vec<u8> = (0..512).map(|n| (n % 251) as u8).collect();
        let mut dst = vec![0; 512];
        copy_color_rows(&mut dst, &src, 64, 2, 256);
        assert_eq!(dst, src);
    }

    #[test]
    fn padded_rows_skip_padding() {
        let mut src = vec![0xee; 1024];
        for row in 0..2 {
            for col in 0..260 {
                src[row * 512 + col] = ((row + col) % 251) as u8;
            }
        }
        let mut dst = vec![0; 520];
        copy_color_rows(&mut dst, &src, 65, 2, 512);
        for row in 0..2 {
            assert_eq!(&dst[row * 260..(row + 1) * 260],
                &src[row * 512..row * 512 + 260]);
        }
    }
}
