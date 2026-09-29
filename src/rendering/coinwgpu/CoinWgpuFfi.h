#ifndef COIN_WGPU_FFI_H
#define COIN_WGPU_FFI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COIN_WGPU_BRIDGE_PROTOCOL_REVISION 28
#define COIN_WGPU_FFI_MAX_LIGHTS 8
#define COIN_WGPU_ABI_VERSION COIN_WGPU_BRIDGE_PROTOCOL_REVISION

typedef uint64_t CoinWgpuSurfaceId;
#define COIN_WGPU_INVALID_SURFACE_ID UINT64_C(0)
typedef uint64_t CoinWgpuDeviceId; /* zero selects the default device */

typedef enum CoinWgpuStatus {
  COIN_WGPU_OK = 0,
  COIN_WGPU_NOT_READY = 1,
  COIN_WGPU_INVALID_ARGUMENT = 2,
  COIN_WGPU_UNSUPPORTED = 3,
  COIN_WGPU_OUT_OF_MEMORY = 4,
  COIN_WGPU_DEVICE_LOST = 5,
  COIN_WGPU_BACKEND_ERROR = 6,
  COIN_WGPU_SURFACE_LOST = 7
} CoinWgpuStatus;

/* Private test-only faults for RTT resource creation and binding. */
#define COIN_WGPU_FAULT_RTT_COLOR_ALLOC 301
#define COIN_WGPU_FAULT_RTT_COLOR_VIEW 302
#define COIN_WGPU_FAULT_RTT_DEPTH_ALLOC 303
#define COIN_WGPU_FAULT_RTT_BIND_GROUP 304

typedef enum CoinWgpuNativeSurfaceType {
  COIN_WGPU_NATIVE_XLIB = 1,
  COIN_WGPU_NATIVE_WAYLAND = 2,
  COIN_WGPU_NATIVE_WIN32 = 3,
  COIN_WGPU_NATIVE_APPKIT_LAYER = 4
} CoinWgpuNativeSurfaceType;

typedef struct CoinWgpuNativeSurfaceDescriptor {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t type;
  uint32_t reserved;
  uint64_t handle_a;
  uint64_t handle_b;
} CoinWgpuNativeSurfaceDescriptor;

typedef struct CoinWgpuSurfaceCreateInfo {
  uint32_t abi_version;
  uint32_t struct_size;
  CoinWgpuNativeSurfaceDescriptor native;
  uint32_t width;
  uint32_t height;
} CoinWgpuSurfaceCreateInfo;

typedef struct CoinWgpuVertex {
  float position[3];
  float normal[3];
  float texcoord[2];
  uint32_t material_slot;
  float screen_space_w; /* zero means ordinary unexpanded geometry */
  float fog_eye_depth_plus_one; /* zero means compute from model-view */
  float extra_texcoords[7][2];
} CoinWgpuVertex;

typedef struct CoinWgpuDraw {
  uint32_t topology;
  uint32_t first_vertex;
  uint32_t vertex_count;
  uint32_t first_index;
  uint32_t index_count;
  uint32_t render_state_slot;
  uint64_t stable_node_id;
  uint32_t draw_ordinal;
  uint32_t composition_flags; /* 0=blend, 1=additive, 2=screen door, 3=peel; bits 8..14=door level */
  uint64_t source_revision;
  uint32_t render_layer; /* zero=base; overlays retain traversal order */
  uint32_t clear_depth_before; /* 0/1: clear only this draw's viewport */
} CoinWgpuDraw;

typedef struct CoinWgpuMaterial {
  float ambient[4];
  float diffuse[4];
  float specular[4];
  float emission[4];
  float shininess;
  float transparency;
} CoinWgpuMaterial;

typedef struct CoinWgpuTexture {
  uint32_t width;
  uint32_t height;
  uint32_t format; /* 0=RGBA8_UNORM bytes; 1=private GPU RTT token */
  uint32_t reserved;
  uint64_t content_digest;
  const uint8_t * pixels;
  uint64_t pixel_bytes_len;
} CoinWgpuTexture;

typedef struct CoinWgpuSampler {
  uint32_t wrap_s; /* 0=REPEAT, 1=CLAMP_TO_EDGE */
  uint32_t wrap_t; /* 0=REPEAT, 1=CLAMP_TO_EDGE */
  uint32_t filter; /* 0=NEAREST, 1=LINEAR */
  uint32_t reserved;
} CoinWgpuSampler;


typedef struct CoinWgpuLight {
  float position_type[4];       /* xyz view-space position, w: 0=directional, 1=point, 2=spot */
  float direction_cutoff[4];    /* xyz view-space direction, w: cos(cutOffAngle) */
  float color_intensity[4];     /* rgb and intensity */
  float attenuation_exponent[4]; /* quadratic, linear, constant, dropOffRate * 128 */
} CoinWgpuLight;

typedef struct CoinWgpuTextureUnit {
  float matrix[16];
  uint32_t enabled, texture_slot, sampler_slot, model;
  float blend_color[4];
} CoinWgpuTextureUnit;

typedef struct CoinWgpuRenderState {
  float model_view[16];
  float model_view_projection[16];
  float normal_matrix[16];
  float light_direction[4];
  float light_color[4];
  float light_intensity;
  uint32_t has_light;
  uint32_t material_slot;
  uint32_t cull_mode;  /* 0=None, 1=Back, 2=Front */
  uint32_t front_face; /* 0=Ccw, 1=Cw */
  uint32_t light_model; /* 0=BaseColor, 1=Phong */
  float texture_matrix[16];
  uint32_t has_texture;
  uint32_t texture_slot;
  uint32_t sampler_slot;
  uint32_t texture_model;
  float texture_blend_color[4];
  int32_t viewport[4]; /* WebGPU top-left x, y, width, height */
  uint32_t light_count;
  float ambient_light[4];
  CoinWgpuLight lights[COIN_WGPU_FFI_MAX_LIGHTS];
  uint32_t fog_mode; /* 0=None, 1=Haze, 2=Fog, 3=Smoke */
  float fog_color[3];
  float fog_start;
  float fog_end;
  uint32_t depth_test;
  uint32_t depth_write;
  uint32_t depth_function; /* 0=Never, 1=Always, 2=Less, 3=LessEqual, 4=Equal, 5=GreaterEqual, 6=Greater, 7=NotEqual */
  float depth_range[2];
  uint32_t polygon_offset_enabled;
  float polygon_offset_factor;
  float polygon_offset_units;
  uint32_t polygon_offset_styles;
  uint32_t polygon_offset_primitive_style;
  uint32_t clip_plane_count;
  float clip_planes[8][4]; /* CoinRender-resolved eye-space equations; keep dot >= 0 */
  float polygon_offset_slope_bias; /* CoinRender-resolved original-face window depth bias */
  uint32_t polygon_offset_max_depth_bits; /* 0: absent; IEEE-754 maximum depth bits + 1 */
  CoinWgpuTextureUnit extra_textures[7];
  float texture_combines[8][4][4];
} CoinWgpuRenderState;

typedef struct CoinWgpuFrameView {
  uint32_t abi_version;
  uint32_t struct_size;
  uint64_t frame_revision;

  const CoinWgpuVertex * vertices;
  uint64_t vertex_count;
  const uint32_t * indices;
  uint64_t index_count;
  const CoinWgpuDraw * draws;
  uint64_t draw_count;

  const CoinWgpuMaterial * materials;
  uint64_t material_count;
  const CoinWgpuRenderState * states;
  uint64_t state_count;

  const CoinWgpuTexture * textures;
  uint64_t texture_count;
  const CoinWgpuSampler * samplers;
  uint64_t sampler_count;

  float clear_color[4];
  uint32_t width;
  uint32_t height;
  /* Nonzero only when the private C++ packer actually reused this base. */
  uint64_t camera_base_revision;
  uint32_t sorted_layers_passes;
  uint32_t transparency_reserved;
  uint64_t transparency_budget_bytes;
} CoinWgpuFrameView;

typedef struct CoinWgpuTarget {
  uint32_t width;
  uint32_t height;
  uint8_t * color_buffer;
  uint64_t color_buffer_len;
  float * depth_buffer;
  uint64_t depth_buffer_len;
  uint64_t submission_serial;
  CoinWgpuDeviceId device_id;
} CoinWgpuTarget;

typedef struct CoinWgpuReadbackTicket {
  uint32_t abi_version;
  uint32_t struct_size;
  uint64_t token;
  uint64_t generation;
  uint64_t submission_serial;
  uint32_t width;
  uint32_t height;
  uint32_t color_format; /* 0=RGBA8_UNORM */
  uint32_t depth_format; /* 1=DEPTH32_FLOAT, 0=no depth */
  uint32_t color_row_pitch;
  uint32_t depth_row_pitch;
  uint64_t color_bytes;
  uint64_t depth_bytes;
} CoinWgpuReadbackTicket;

typedef struct CoinWgpuCacheStats {
  uint64_t cumulative_uploads;
  uint64_t cumulative_hits;
  uint64_t cumulative_misses;
  uint64_t cumulative_uploaded_bytes;
  uint64_t frame_uploaded_bytes;
  uint64_t frame_uploads;
  uint64_t frame_hits;
  uint64_t active_entries;
  uint64_t retired_entries;
  uint64_t completed_serial;
  uint64_t submission_serial;
} CoinWgpuCacheStats;

typedef struct CoinWgpuPerformanceStats {
  uint64_t texture_uploads;
  uint64_t texture_hits;
  uint64_t texture_uploaded_bytes;
  uint64_t texture_evictions;
  uint64_t texture_active_entries;
  uint64_t texture_retired_entries;
  uint64_t pipeline_compilations;
  uint64_t pipeline_hits;
  uint64_t pipeline_active_entries;
} CoinWgpuPerformanceStats;

int32_t coin_wgpu_is_available(void);

CoinWgpuStatus coin_wgpu_device_create(CoinWgpuDeviceId * out_id,
  char * error_buf, size_t error_buf_len);
CoinWgpuStatus coin_wgpu_device_destroy(CoinWgpuDeviceId id);
void coin_wgpu_inject_device_fault(CoinWgpuDeviceId id, int32_t code);

void coin_wgpu_get_adapter_info(char * buffer, size_t buffer_len);

CoinWgpuStatus coin_wgpu_submit(
  CoinWgpuTarget * target,
  const CoinWgpuFrameView * frame,
  char * error_buf,
  size_t error_buf_len
);

/* Render an offscreen pass to a sampleable texture without CPU readback.
   The token belongs to one device generation and must be released after
   its consumers are submitted. */
CoinWgpuStatus coin_wgpu_submit_texture(
  CoinWgpuTarget * target,
  const CoinWgpuFrameView * frame,
  uint64_t * out_token,
  char * error_buf,
  size_t error_buf_len
);

void coin_wgpu_release_texture(uint64_t token);
void coin_wgpu_rtt_resource_counts(uint64_t * active, uint64_t * retired);

/* Async offscreen submission keeps staging buffers in the bridge until poll/cancel.
   Target output pointers are ignored; width/height and depth_buffer_len select
   attachments (depth_buffer_len is a count of floats). The ticket reports
   generation, serial, RGBA8/depth32 formats, pitches and packed output sizes.
   poll returns NOT_READY without blocking and publishes both attachments only
   on OK. No callback retains target/frame pointers. */
CoinWgpuStatus coin_wgpu_submit_async(
  CoinWgpuTarget * target,
  const CoinWgpuFrameView * frame,
  CoinWgpuReadbackTicket * out_ticket,
  char * error_buf,
  size_t error_buf_len
);

/* Nonblocking readiness check; OK means mapping is complete but does not
   consume the token or publish output. */
CoinWgpuStatus coin_wgpu_readback_query(
  uint64_t token,
  char * error_buf,
  size_t error_buf_len
);

CoinWgpuStatus coin_wgpu_readback_poll(
  uint64_t token,
  uint8_t * color_buffer,
  uint64_t color_buffer_len,
  float * depth_buffer,
  uint64_t depth_buffer_len,
  char * error_buf,
  size_t error_buf_len
);

CoinWgpuStatus coin_wgpu_readback_cancel(uint64_t token);

CoinWgpuStatus coin_wgpu_surface_create(
  const CoinWgpuSurfaceCreateInfo * info,
  CoinWgpuSurfaceId * out_surface,
  char * error_buf,
  size_t error_buf_len
);

CoinWgpuStatus coin_wgpu_surface_resize(
  CoinWgpuSurfaceId surface,
  uint32_t width,
  uint32_t height,
  char * error_buf,
  size_t error_buf_len
);

CoinWgpuStatus coin_wgpu_surface_submit(
  CoinWgpuSurfaceId surface,
  const CoinWgpuFrameView * frame,
  char * error_buf,
  size_t error_buf_len
);

CoinWgpuStatus coin_wgpu_surface_destroy(
  CoinWgpuSurfaceId surface,
  char * error_buf,
  size_t error_buf_len
);

void coin_wgpu_get_cache_stats(CoinWgpuCacheStats * stats);
void coin_wgpu_get_performance_stats(CoinWgpuPerformanceStats * stats);

void coin_wgpu_poll_device(void);

void coin_wgpu_set_cache_budget(uint64_t max_bytes, uint64_t max_stale_serials);

void coin_wgpu_trim_cache(void);

void coin_wgpu_inject_fault(int32_t fault_code);
/* Test-only deterministic fault on an offscreen submit after skipped submits. */
void coin_wgpu_inject_fault_after_submits(int32_t fault_code, uint32_t skipped_submits);

void coin_wgpu_inject_async_fault(int32_t fault_code);

void coin_wgpu_reset_context(void);

#ifdef __cplusplus
}
#endif

#endif // !COIN_WGPU_FFI_H
