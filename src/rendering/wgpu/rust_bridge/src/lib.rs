#![allow(clippy::not_unsafe_ptr_arg_deref)]
#![allow(clippy::too_many_arguments)]
#![allow(clippy::manual_is_multiple_of)]
#![allow(clippy::if_same_then_else)]
#![allow(clippy::needless_range_loop)]
#![allow(clippy::needless_lifetimes)]
use bytemuck::{Pod, Zeroable};
use pollster::block_on;
use raw_window_handle::{RawDisplayHandle, RawWindowHandle, XlibDisplayHandle, XlibWindowHandle};
use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, AtomicI32, AtomicU32, AtomicU64, Ordering};
use std::sync::Mutex;

pub const COIN_WGPU_BRIDGE_PROTOCOL_REVISION: u32 = 7;
pub const COIN_WGPU_ABI_VERSION: u32 = COIN_WGPU_BRIDGE_PROTOCOL_REVISION;

pub type CoinWgpuSurfaceId = u64;
pub const COIN_WGPU_INVALID_SURFACE_ID: CoinWgpuSurfaceId = 0;

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
}

#[repr(C)]
#[derive(Copy, Clone, Debug, Pod, Zeroable)]
pub struct CoinWgpuVertex {
    pub position: [f32; 3],
    pub normal: [f32; 3],
    pub texcoord: [f32; 2],
    pub material_slot: u32,
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
    pub reserved: u32,
    pub source_revision: u64,
}

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
    pub format: u32, // 0 = RGBA8_UNORM
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
    pub ambient_light: [f32; 4],
    pub light_meta: [f32; 4],
    pub lights: [CoinWgpuLight; 8],
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
    pub light_count: u32,
    pub ambient_light: [f32; 4],
    pub lights: [CoinWgpuLight; 8],
}

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
pub struct CoinWgpuFrameView {
    pub abi_version: u32,
    pub struct_size: u32,

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
}

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
    cache: Mutex<GeometryCache>,
    default_texture: wgpu::Texture,
    default_texture_view: wgpu::TextureView,
    default_sampler: wgpu::Sampler,
    texture_cache: Mutex<TextureCache>,
    sampler_cache: Mutex<SamplerCache>,
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

struct RuntimeContext {
    instance: wgpu::Instance,
    surfaces: HashMap<CoinWgpuSurfaceId, SurfaceRecord>,
    device_state: Option<DeviceState>,
    device_generation: u64,
}

static RUNTIME_CTX: Mutex<Option<RuntimeContext>> = Mutex::new(None);
static NEXT_SURFACE_ID: AtomicU64 = AtomicU64::new(1);

static FAULT_INJECTION: AtomicI32 = AtomicI32::new(0);
static FAULT_INJECTION_ASYNC: AtomicI32 = AtomicI32::new(0);

// Fault injection codes for surface
pub const FAULT_SURFACE_TIMEOUT: i32 = 101;
pub const FAULT_SURFACE_OUTDATED_ONCE: i32 = 102;
pub const FAULT_SURFACE_LOST_ONCE: i32 = 103;
pub const FAULT_SURFACE_LOST_PERSISTENT: i32 = 104;
pub const FAULT_SURFACE_OUT_OF_MEMORY: i32 = 105;
pub const FAULT_SURFACE_OTHER: i32 = 106;
pub const FAULT_CONFIGURE_FAILURE: i32 = 107;
pub const FAULT_CACHE_ALLOC_FAIL: i32 = 201;

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
    } else {
        Err("Native surface platform not supported in Onda 1B".to_string())
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
        });
    }
    Ok(())
}

fn get_or_init_device<'a>(
    runtime: &'a mut RuntimeContext,
    target_surface: Option<&wgpu::Surface>,
) -> Result<&'a mut DeviceState, String> {
    // Check if device loss occurred asynchronously
    if DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst) {
        runtime.device_state = None;
        runtime.device_generation += 1;
        LAST_ASYNC_ERROR_KIND.store(0, Ordering::SeqCst);
        if let Ok(mut lock) = LAST_ASYNC_ERROR_MSG.lock() {
            lock.clear();
        }
    }

    if runtime.device_state.is_some() {
        return Ok(runtime.device_state.as_mut().unwrap());
    }

    let adapter = block_on(
        runtime
            .instance
            .request_adapter(&wgpu::RequestAdapterOptions {
                power_preference: wgpu::PowerPreference::HighPerformance,
                compatible_surface: target_surface,
                force_fallback_adapter: false,
            }),
    )
    .ok_or_else(|| "No compatible GPU adapter found".to_string())?;

    let adapter_info = adapter.get_info();
    let adapter_name = format!("{} ({:?})", adapter_info.name, adapter_info.backend);

    let (device, queue) = block_on(adapter.request_device(
        &wgpu::DeviceDescriptor {
            label: Some("Coin3D WebGPU Device"),
            required_features: wgpu::Features::empty(),
            required_limits: wgpu::Limits::default(),
            memory_hints: wgpu::MemoryHints::Performance,
        },
        None,
    ))
    .map_err(|e| format!("Failed to create device: {}", e))?;

    device.set_device_lost_callback(|reason, message| {
        DEVICE_LOST_OCCURRED.store(true, Ordering::SeqCst);
        if let Ok(mut lock) = LAST_ASYNC_ERROR_MSG.lock() {
            *lock = format!("WebGPU device lost ({:?}): {}", reason, message);
        }
    });

    device.on_uncaptured_error(Box::new(|error: wgpu::Error| {
        match error {
            wgpu::Error::OutOfMemory { .. } => {
                LAST_ASYNC_ERROR_KIND.store(1, Ordering::SeqCst);
            }
            _ => {
                LAST_ASYNC_ERROR_KIND.store(2, Ordering::SeqCst);
            }
        }
        if let Ok(mut lock) = LAST_ASYNC_ERROR_MSG.lock() {
            *lock = format!("WebGPU uncaptured error: {}", error);
        }
    }));

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

    let bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("Coin Uniform & Material BindGroupLayout"),
        entries: &[
            wgpu::BindGroupLayoutEntry {
                binding: 0,
                visibility: wgpu::ShaderStages::VERTEX_FRAGMENT,
                ty: wgpu::BindingType::Buffer {
                    ty: wgpu::BufferBindingType::Uniform,
                    has_dynamic_offset: false,
                    min_binding_size: None,
                },
                count: None,
            },
            wgpu::BindGroupLayoutEntry {
                binding: 1,
                visibility: storage_visibility,
                ty: wgpu::BindingType::Buffer {
                    ty: wgpu::BufferBindingType::Storage { read_only: true },
                    has_dynamic_offset: false,
                    min_binding_size: None,
                },
                count: None,
            },
            wgpu::BindGroupLayoutEntry {
                binding: 2,
                visibility: wgpu::ShaderStages::FRAGMENT,
                ty: wgpu::BindingType::Texture {
                    sample_type: wgpu::TextureSampleType::Float { filterable: true },
                    view_dimension: wgpu::TextureViewDimension::D2,
                    multisampled: false,
                },
                count: None,
            },
            wgpu::BindGroupLayoutEntry {
                binding: 3,
                visibility: wgpu::ShaderStages::FRAGMENT,
                ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                count: None,
            },
        ],
    });

    let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
        label: Some("Coin Pipeline Layout"),
        bind_group_layouts: &[&bind_group_layout],
        push_constant_ranges: &[],
    });

    DEVICE_LOST_OCCURRED.store(false, Ordering::SeqCst);
    LAST_ASYNC_ERROR_KIND.store(0, Ordering::SeqCst);

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
        cache: Mutex::new(GeometryCache::default()),
        default_texture,
        default_texture_view,
        default_sampler,
        texture_cache: Mutex::new(TextureCache::default()),
        sampler_cache: Mutex::new(SamplerCache::default()),
    });

    Ok(runtime.device_state.as_mut().unwrap())
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

    // Negotiate present mode: AutoVsync -> Fifo
    let chosen_present = if caps.present_modes.contains(&wgpu::PresentMode::AutoVsync) {
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
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
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
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
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

fn get_or_create_pipeline<'a>(
    ctx: &'a DeviceState,
    topology: u32,
    color_format: wgpu::TextureFormat,
    depth_format: wgpu::TextureFormat,
    cull_face: Option<wgpu::Face>,
    front_face: wgpu::FrontFace,
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
    };
    if let Some(p) = map.get(&key) {
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
        ],
    };

    let pipeline_label = match topology {
        1 => "Coin Line Render Pipeline",
        2 => "Coin Point Render Pipeline",
        _ => "Coin Standard Render Pipeline",
    };

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
                entry_point: Some("fs_main"),
                targets: &[Some(wgpu::ColorTargetState {
                    format: color_format,
                    blend: Some(wgpu::BlendState::REPLACE),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
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
                depth_write_enabled: true,
                depth_compare: wgpu::CompareFunction::LessEqual,
                stencil: wgpu::StencilState::default(),
                bias: wgpu::DepthBiasState::default(),
            }),
            multisample: wgpu::MultisampleState::default(),
            multiview: None,
            cache: None,
        });

    map.insert(key, pipeline.clone());
    Ok(pipeline)
}

// Pure shared command encoder function strictly common to offscreen and window targets
fn encode_frame(
    ctx: &DeviceState,
    clear_color: [f32; 4],
    vertices_slice: &[CoinWgpuVertex],
    indices_slice: &[u32],
    draws_slice: &[CoinWgpuDraw],
    materials_slice: &[CoinWgpuMaterial],
    states_slice: &[CoinWgpuRenderState],
    textures_slice: &[CoinWgpuTexture],
    samplers_slice: &[CoinWgpuSampler],
    color_view: &wgpu::TextureView,
    color_format: wgpu::TextureFormat,
    depth_view: &wgpu::TextureView,
) -> Result<wgpu::CommandBuffer, (CoinWgpuStatus, String)> {
    use wgpu::util::DeviceExt;
    // Preflight the entire lighting payload before cache mutation or command encoding.
    for (state_index, state) in states_slice.iter().enumerate() {
        if state.light_count > 8 {
            return Err((CoinWgpuStatus::Unsupported,
                format!("State {} has more than eight active lights", state_index)));
        }
        if !state.ambient_light.iter().all(|v| v.is_finite())
            || !state.normal_matrix.iter().all(|v| v.is_finite()) {
            return Err((CoinWgpuStatus::InvalidArgument,
                format!("State {} has non-finite ambient or normal matrix", state_index)));
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


    // 1. Process pending GPU completion events and lock geometry cache
    let _ = ctx.device.poll(wgpu::Maintain::Poll);
    let mut cache = ctx.cache.lock().unwrap();

    // Reset per-frame telemetry counters
    cache.frame_uploaded_bytes = 0;
    cache.frame_uploads = 0;
    cache.frame_hits = 0;

    // Drain safely retired buffers whose work on GPU has completed
    let completed_serial = GLOBAL_COMPLETED_SERIAL.load(Ordering::SeqCst);
    cache.deferred_release.retain(|retired| {
        retired.retired_at_serial > completed_serial
    });

    let mut tex_cache = ctx.texture_cache.lock().unwrap();
    let mut samp_cache = ctx.sampler_cache.lock().unwrap();

    // Drain safely retired textures whose work on GPU has completed
    tex_cache.retired.retain(|retired| {
        retired.retired_at_serial > completed_serial
    });

    let current_submission_serial = GLOBAL_SUBMISSION_SERIAL.load(Ordering::SeqCst);

    // Upload and cache frame textures
    for (t_idx, t) in textures_slice.iter().enumerate() {
        if t.width == 0 || t.height == 0 || t.width > 8192 || t.height > 8192 {
            return Err((
                CoinWgpuStatus::InvalidArgument,
                format!("Texture {} dimensions invalid: {}x{}", t_idx, t.width, t.height),
            ));
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
    cache.evict_stale(LAST_SUBMITTED_SERIAL.load(Ordering::SeqCst));

    // 1b. Material storage buffer and slot validation
    if materials_slice.is_empty() {
        return Err((CoinWgpuStatus::InvalidArgument, "Frame material_count must be > 0".to_string()));
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

    let materials_buffer = ctx.device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
        label: Some("Frame Materials Storage Buffer"),
        contents: bytemuck::cast_slice(&gpu_materials),
        usage: wgpu::BufferUsages::STORAGE,
    });

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

    // 4. Begin render pass
    let mut encoder = ctx
        .device
        .create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("Coin Frame Encoder"),
        });

    {
        let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("Coin Main Pass"),
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
            timestamp_writes: None,
            occlusion_query_set: None,
        });

        for draw in draws_slice {
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

            let pipeline = match get_or_create_pipeline(
                ctx,
                draw.topology,
                color_format,
                wgpu::TextureFormat::Depth32Float,
                cull_face,
                front_face,
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

            let (tex_view, samp) = if st.has_texture != 0
                && (st.texture_slot as usize) < textures_slice.len()
                && (st.sampler_slot as usize) < samplers_slice.len()
            {
                let t = &textures_slice[st.texture_slot as usize];
                let s = &samplers_slice[st.sampler_slot as usize];
                let t_key = TextureKey {
                    width: t.width,
                    height: t.height,
                    format: t.format,
                    content_digest: t.content_digest,
                };
                let s_key = SamplerKey {
                    wrap_s: s.wrap_s,
                    wrap_t: s.wrap_t,
                    filter: s.filter,
                };
                let view_ref = tex_cache.entries.get(&t_key).map(|e| &e.view).unwrap_or(&ctx.default_texture_view);
                let samp_ref = samp_cache.entries.get(&s_key).unwrap_or(&ctx.default_sampler);
                (view_ref, samp_ref)
            } else {
                (&ctx.default_texture_view, &ctx.default_sampler)
            };

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
                tex_params: [
                    if st.has_texture != 0 { 1.0 } else { 0.0 },
                    st.texture_model as f32,
                    0.0,
                    0.0,
                ],
                ambient_light: st.ambient_light,
                light_meta: [st.light_count as f32, 0.0, 0.0, 0.0],
                lights: st.lights,
            };

            let u_buffer = ctx
                .device
                .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                    label: Some("Draw Uniform Buffer"),
                    contents: bytemuck::bytes_of(&uniforms),
                    usage: wgpu::BufferUsages::UNIFORM,
                });

            let bind_group = ctx.device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: Some("Draw Bind Group"),
                layout: &ctx.bind_group_layout,
                entries: &[
                    wgpu::BindGroupEntry {
                        binding: 0,
                        resource: u_buffer.as_entire_binding(),
                    },
                    wgpu::BindGroupEntry {
                        binding: 1,
                        resource: materials_buffer.as_entire_binding(),
                    },
                    wgpu::BindGroupEntry {
                        binding: 2,
                        resource: wgpu::BindingResource::TextureView(tex_view),
                    },
                    wgpu::BindGroupEntry {
                        binding: 3,
                        resource: wgpu::BindingResource::Sampler(samp),
                    },
                ],
            });

            pass.set_bind_group(0, &bind_group, &[]);

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

#[no_mangle]
pub extern "C" fn coin_wgpu_inject_fault(fault_code: i32) {
    FAULT_INJECTION.store(fault_code, Ordering::SeqCst);
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

        // Initialize device if not yet created, passing this surface as compatible
        let dev_generation = runtime.device_generation;
        let device_state = match get_or_init_device(runtime, Some(&surface)) {
            Ok(d) => d,
            Err(e) => {
                set_error(error_buf, error_buf_len, &e);
                return CoinWgpuStatus::NotReady;
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

#[no_mangle]
pub extern "C" fn coin_wgpu_surface_submit(
    surface_id: CoinWgpuSurfaceId,
    frame: *const CoinWgpuFrameView,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    let res = std::panic::catch_unwind(|| {
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
            vertices_slice,
            indices_slice,
            draws_slice,
            materials_slice,
            states_slice,
            textures_slice,
            samplers_slice,
            &color_view,
            record.color_format,
            depth_view,
        ) {
            Ok(cmd) => cmd,
            Err((status, msg)) => {
                set_error(error_buf, error_buf_len, &msg);
                // Texture is dropped without calling present()
                return status;
            }
        };

        // 10. Submit and Present
        let sub_serial = GLOBAL_SUBMISSION_SERIAL.fetch_add(1, Ordering::SeqCst);
        LAST_SUBMITTED_SERIAL.store(sub_serial, Ordering::SeqCst);
        dev.queue.submit(std::iter::once(cmd_buffer));
        dev.queue.on_submitted_work_done(move || {
            GLOBAL_COMPLETED_SERIAL.store(sub_serial, Ordering::SeqCst);
        });
        surface_texture.present();

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
pub extern "C" fn coin_wgpu_submit(
    target: *mut CoinWgpuTarget,
    frame: *const CoinWgpuFrameView,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    let res = std::panic::catch_unwind(|| {
        // 1. Fault injection check
        let fault = FAULT_INJECTION.load(Ordering::SeqCst);
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

        let tgt = unsafe { &mut *target };
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
        let bytes_per_row = ((unaligned_bytes + 255) & !255) as u32;
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

        if tgt.color_buffer.is_null() || tgt.color_buffer_len < required_target_len {
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

        // 5. Slice bounds and alignment validation
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

        // 6. Strict validation of each draw packet against buffers and states
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

        let device_state = match get_or_init_device(runtime, None) {
            Ok(d) => d as *mut DeviceState,
            Err(e) => {
                set_error(error_buf, error_buf_len, &e);
                return CoinWgpuStatus::NotReady;
            }
        };
        let ctx = unsafe { &mut *device_state };

        // 8. Create target textures for offscreen
        let texture_desc = wgpu::TextureDescriptor {
            label: Some("Offscreen Color Texture"),
            size: wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        };
        let color_texture = ctx.device.create_texture(&texture_desc);
        let color_view = color_texture.create_view(&wgpu::TextureViewDescriptor::default());

        let depth_desc = wgpu::TextureDescriptor {
            label: Some("Offscreen Depth Texture"),
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
        let depth_texture = ctx.device.create_texture(&depth_desc);
        let depth_view = depth_texture.create_view(&wgpu::TextureViewDescriptor::default());

        // 9. Encode draw calls with the exact same shared encode_frame
        let cmd_buffer = match encode_frame(
            ctx,
            f.clear_color,
            vertices_slice,
            indices_slice,
            draws_slice,
            materials_slice,
            states_slice,
            textures_slice,
            samplers_slice,
            &color_view,
            wgpu::TextureFormat::Rgba8Unorm,
            &depth_view,
        ) {
            Ok(cmd) => cmd,
            Err((status, msg)) => {
                set_error(error_buf, error_buf_len, &msg);
                return status;
            }
        };

        // 10. Copy to staging buffer for readback
        let staging_size = (bytes_per_row as u64) * (height as u64);
        let staging_buffer = ctx.device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("Staging Buffer"),
            size: staging_size,
            usage: wgpu::BufferUsages::MAP_READ | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });

        let mut copy_encoder = ctx
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("Offscreen Copy Encoder"),
            });

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
        let depth_staging_info = if !tgt.depth_buffer.is_null()
            && tgt.depth_buffer_len >= (width as u64) * (height as u64)
        {
            let dbuf = ctx.device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("Offscreen Depth Staging Buffer"),
                size: depth_staging_size,
                usage: wgpu::BufferUsages::MAP_READ | wgpu::BufferUsages::COPY_DST,
                mapped_at_creation: false,
            });
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

        let sub_serial = GLOBAL_SUBMISSION_SERIAL.fetch_add(1, Ordering::SeqCst);
        LAST_SUBMITTED_SERIAL.store(sub_serial, Ordering::SeqCst);
        ctx.queue.submit([cmd_buffer, copy_encoder.finish()]);
        ctx.queue.on_submitted_work_done(move || {
            GLOBAL_COMPLETED_SERIAL.store(sub_serial, Ordering::SeqCst);
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

        let _ = ctx.device.poll(wgpu::Maintain::Wait);

        // Inject async fault if requested
        let async_fault = FAULT_INJECTION_ASYNC.swap(0, Ordering::SeqCst);
        if async_fault == CoinWgpuStatus::DeviceLost as i32 {
            DEVICE_LOST_OCCURRED.store(true, Ordering::SeqCst);
            if let Ok(mut lock) = LAST_ASYNC_ERROR_MSG.lock() {
                *lock = "Injected async DEVICE_LOST fault during GPU execution".to_string();
            }
        }

        // Check if device lost occurred during submit or poll
        if DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst) {
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

        match receiver.recv() {
            Ok(Ok(())) => {
                let data = buffer_slice.get_mapped_range();
                let row_bytes = (width * 4) as usize;
                let out_buf = match validate_slice_mut(
                    tgt.color_buffer,
                    required_target_len,
                    "target color buffer",
                    error_buf,
                    error_buf_len,
                ) {
                    Ok(b) => b,
                    Err(st) => return st,
                };
                for y in 0..height as usize {
                    let src_offset = y * bytes_per_row as usize;
                    let dst_offset = y * row_bytes;
                    if dst_offset + row_bytes <= out_buf.len()
                        && src_offset + row_bytes <= data.len()
                    {
                        out_buf[dst_offset..dst_offset + row_bytes]
                            .copy_from_slice(&data[src_offset..src_offset + row_bytes]);
                    }
                }

                if let (Some(ref dslice), Some((_, d_bpr))) = (&depth_slice_holder, &depth_staging_info) {
                    if let Ok(Ok(())) = depth_receiver.recv() {
                        let ddata = dslice.get_mapped_range();
                        let dfloats: &[f32] = bytemuck::cast_slice(&ddata);
                        let floats_per_row = (*d_bpr / 4) as usize;
                        let row_floats = width as usize;
                        let out_depth = unsafe { std::slice::from_raw_parts_mut(tgt.depth_buffer, (width * height) as usize) };
                        for y in 0..height as usize {
                            let src_off = y * floats_per_row;
                            let dst_off = y * row_floats;
                            if dst_off + row_floats <= out_depth.len() && src_off + row_floats <= dfloats.len() {
                                out_depth[dst_off..dst_off + row_floats]
                                    .copy_from_slice(&dfloats[src_off..src_off + row_floats]);
                            }
                        }
                    }
                }

                tgt.submission_serial = sub_serial;
                CoinWgpuStatus::Ok
            }
            Ok(Err(buf_err)) => {
                if DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst) {
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
                if DEVICE_LOST_OCCURRED.swap(false, Ordering::SeqCst) {
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
pub extern "C" fn coin_wgpu_poll_device() {
    let _ = std::panic::catch_unwind(|| {
        if let Ok(guard) = RUNTIME_CTX.lock() {
            if let Some(runtime) = guard.as_ref() {
                if let Some(dev) = &runtime.device_state {
                    let _ = dev.device.poll(wgpu::Maintain::Poll);
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
                        let last_serial = LAST_SUBMITTED_SERIAL.load(Ordering::SeqCst);
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
