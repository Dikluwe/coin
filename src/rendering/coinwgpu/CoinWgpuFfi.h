#ifndef COIN_WGPU_FFI_H
#define COIN_WGPU_FFI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COIN_WGPU_BRIDGE_PROTOCOL_REVISION 70
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
#define COIN_WGPU_FAULT_SHADOW_MAP_ALLOC 305
#define COIN_WGPU_FAULT_RTT_MIP_ENCODING 306
#define COIN_WGPU_FAULT_MSAA_AFTER_ENCODE 307

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
  uint32_t renderer; /* CoinRenderRenderer; 0 selects the runtime default */
  uint32_t sample_count; /* 0/1=default single sample; 2 or 4 explicit MSAA */
} CoinWgpuSurfaceCreateInfo;

typedef struct CoinWgpuVertex {
  float position[3];
  float normal[3];
  float texcoord[4]; /* homogeneous s,t,r,q; project after fragment interpolation */
  uint32_t material_slot;
  float screen_space_w; /* zero means ordinary unexpanded geometry */
  float fog_eye_depth_plus_one; /* zero means compute from model-view */
  float extra_texcoords[7][4];
} CoinWgpuVertex;

/* Optional R26 bump stream, indexed exactly like the emitted vertex buffer. */
typedef struct CoinWgpuBumpVertex {
  float coord[4];
  float tangent_s[3];
  float tangent_t[3];
  uint32_t image_slot; /* One-based; zero means no bump for this vertex. */
} CoinWgpuBumpVertex;

typedef struct CoinWgpuBumpImage {
  uint32_t width;
  uint32_t height;
  uint32_t clamp_s;
  uint32_t clamp_t;
  const uint8_t * rgb;
  uint64_t rgb_len;
} CoinWgpuBumpImage;

typedef struct CoinWgpuDraw {
  uint32_t topology;
  uint32_t first_vertex;
  uint32_t vertex_count;
  uint32_t first_index;
  uint32_t index_count;
  uint32_t render_state_slot;
  uint64_t stable_node_id;
  uint32_t draw_ordinal;
  uint32_t composition_flags; /* 0=blend, 1=additive, 2=screen door, 3=peel, 4=weighted; bit 5=legacy producer alpha; bit 6=authored scene layer; bits 8..14=door level; 15..30=sample mask (zero=all); 31=raster layout already sampled */
  uint64_t source_revision;
  uint32_t render_layer; /* zero=base; overlays retain traversal order */
  uint32_t clear_depth_before; /* 0/1: clear only this draw's viewport */
} CoinWgpuDraw;

/* Private opaque instancing: source geometry remains in object space. */
typedef struct CoinWgpuInstance {
  float model_view[16];
  float normal_matrix[16];
  uint32_t material_slot;
  uint32_t reserved[3]; /* must be zero; CPU transport stride is 144 bytes */
} CoinWgpuInstance;

typedef struct CoinWgpuInstanceRange {
  uint32_t draw_index;
  uint32_t first_instance;
  uint32_t instance_count;
  uint32_t reserved; /* must be zero */
} CoinWgpuInstanceRange;

typedef struct CoinWgpuMaterial {
  float ambient[4];
  float diffuse[4];
  float specular[4];
  float emission[4];
  float shininess;
  float transparency;
} CoinWgpuMaterial;

#define COIN_WGPU_TEXTURE_DECLARED_OPAQUE (1u << 31)
#define COIN_WGPU_TEXTURE_VOLUME_SRGB (1u << 25)
#define COIN_WGPU_TEXTURE_CUBE_SRGB (1u << 9)

typedef struct CoinWgpuTexture {
  uint32_t width;
  uint32_t height;
  uint32_t format; /* 0=RGBA8 base; 1=RTT; 2=RGBA8 mips; 3..10=advanced 2D; 11=RGBA8 spatial atlas; 12=RGBA8 cube atlas */
  uint32_t reserved; /* bit31=SoSceneTexture2 NONE declaration; lower bits: RTT policy, or atlas (W-1)/(H-1)/(D-1) in bytes 0..2 and spatial mips bit24, sRGB bit25 (bits26..30 zero); cube log2(face) bits0..3, source components bits4..6, mips bit7, GPU token bit8, sRGB bit9 (bits10..30 zero) */
  uint64_t content_digest;
  const uint8_t * pixels;
  uint64_t pixel_bytes_len;
} CoinWgpuTexture;

typedef struct CoinWgpuSampler {
  uint32_t wrap_s; /* 0=REPEAT, 1=CLAMP_TO_EDGE */
  uint32_t wrap_t; /* 0=REPEAT, 1=CLAMP_TO_EDGE */
  uint32_t filter; /* 0=NEAREST, 1=LINEAR, 2=NEAREST_MIPMAP_LINEAR, 3=LINEAR_MIPMAP_LINEAR */
  uint32_t reserved; /* anisotropy minus one in bits0..3; spatial wrapR in bit4 */
} CoinWgpuSampler;


typedef struct CoinWgpuLight {
  float position_type[4];       /* xyz view-space position, w: 0=directional, 1=point, 2=spot */
  float direction_cutoff[4];    /* xyz view-space direction, w: cos(cutOffAngle) */
  float color_intensity[4];     /* rgb and intensity */
  float attenuation_exponent[4]; /* quadratic, linear, constant, dropOffRate * 128 */
  float ambient_color[4];       /* protocol68: color.rgb * ambientIntensity, reserved w = 0 */
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
  int32_t viewport[4]; /* In-target top-left x,y,width,height after Core projection compensation.
                           [0,0,0,0] = full target; [0,0,0,1] = empty/no draw or depth clear (protocol 46). */
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
  uint32_t alpha_test_function; /* semantic 0=None,1=Never,2=Always,3=Less,4=LEqual,5=Equal,6=GEqual,7=Greater,8=NotEqual */
  float alpha_test_reference; /* finite and clamped to [0,1] */
  uint32_t texture_projection; /* 0=project ST/Q; 1=native Coin shadow-shader direct ST */
  uint32_t color_write; /* 0=depth-only, 1=RGBA (legacy), 0x10|mask=partial RGBA mask */
  uint32_t flat_shadow_primary; /* 1=fragment gl_Color uses the state's material; vertex lighting uses vertex materials */
  uint32_t raster_texture_source_slot; /* one-based source state, zero disables */
  float raster_texture_coordinates[8][4]; /* raw persistent raster attributes */
  uint32_t portable_shader_slot; /* protocol63: one-based Core IR occurrence; zero is fixed texturing */
} CoinWgpuRenderState;

typedef struct CoinWgpuShadowDraw {
  uint32_t first_index;
  uint32_t index_count;
  uint32_t render_state_slot;
  uint32_t reserved; /* must be zero */
  float model_view[16];
  float model_view_projection[16];
  int32_t viewport[4]; /* Protocol62: clipped top-left map-pixel scissor; zero extent is empty. */
} CoinWgpuShadowDraw;

typedef struct CoinWgpuShadowReceiver {
  uint32_t receives; /* Resolved flags: receiver=1, vertex lighting=2, ordinary fragment=4, group=8. */
  int32_t lighting_index;
  float max_shadow_distance; /* negative: no directional fade */
  float distance_falloff_coefficient; /* Core-resolved Coin curve */
  float model_view[16];
  float model_view_projection[16];
} CoinWgpuShadowReceiver;

typedef struct CoinWgpuShadowPassView {
  const CoinWgpuShadowDraw * casters;
  uint64_t caster_count;
  const CoinWgpuShadowReceiver * receivers;
  uint64_t receiver_count;
  uint32_t map_size;
  uint32_t kind;
  float near_distance;
  float far_distance;
  float epsilon;
  float threshold;
} CoinWgpuShadowPassView;

/* Protocol63: typed, topologically ordered Core IR, never shader source.
   Only active op fields carry semantics. All counts/pointers/domains are checked
   before creating resources; params are dense logical f32 (at most 4 KiB). */
typedef struct CoinWgpuPortableNode {
  uint32_t op, type;
  uint32_t inputs[3];
  float value[4];
  uint32_t resource;
} CoinWgpuPortableNode;
typedef struct CoinWgpuPortableModule {
  const CoinWgpuPortableNode *nodes;
  uint64_t node_count;
  const float *parameters;
  uint64_t parameter_count;
  uint32_t output;
  uint32_t alpha_may_vary;
  const uint8_t *signature;
  uint64_t signature_bytes;
} CoinWgpuPortableModule;

typedef struct CoinWgpuPortableRange {
  double lower[4], upper[4];
} CoinWgpuPortableRange;
/* Registered by trusted C++ wiring, never supplied by a frame packet. The
   callback copies generated WGSL to caller storage and never exposes an owned
   C++ pointer. Null ranges perform structural/parameter validation only.
   V2 passes the existing State projection policy: 0 PROJECTIVE, 1 DIRECT_ST.
   Its distinct installer prevents registration of the old callback prototype. */
typedef CoinWgpuStatus (*CoinWgpuPortableCoreLowerV2)(const CoinWgpuPortableModule *,
  const CoinWgpuPortableRange *primary, const CoinWgpuPortableRange *coordinates,
  const CoinWgpuPortableRange *samples, uint32_t texture_projection,
  char *wgsl, uint64_t wgsl_capacity,
  uint64_t *wgsl_required, char *error, uint64_t error_capacity);
CoinWgpuStatus coin_wgpu_install_portable_core_v2(CoinWgpuPortableCoreLowerV2);

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
  const CoinWgpuShadowDraw * shadow_casters;
  uint64_t shadow_caster_count;
  uint32_t shadow_map_size; // 0=absent; nonzero map may have zero casters (protocol 39).
  float shadow_near_distance;
  float shadow_far_distance;
  float shadow_epsilon;
  float shadow_threshold;
  uint32_t shadow_kind; /* 0=directional axial, 1=spot radial */
  const CoinWgpuShadowReceiver * shadow_receivers;
  uint64_t shadow_receiver_count;
  const CoinWgpuShadowDraw * shadow_casters_second;
  uint64_t shadow_caster_count_second;
  uint32_t shadow_map_size_second;
  float shadow_near_distance_second;
  float shadow_far_distance_second;
  uint32_t shadow_kind_second;
  const CoinWgpuShadowReceiver * shadow_receivers_second;
  uint64_t shadow_receiver_count_second;
  const CoinWgpuShadowDraw * shadow_casters_third;
  uint64_t shadow_caster_count_third;
  uint32_t shadow_map_size_third;
  float shadow_near_distance_third;
  float shadow_far_distance_third;
  uint32_t shadow_kind_third;
  const CoinWgpuShadowReceiver * shadow_receivers_third;
  uint64_t shadow_receiver_count_third;
  const CoinWgpuShadowDraw * shadow_casters_fourth;
  uint64_t shadow_caster_count_fourth;
  uint32_t shadow_map_size_fourth;
  float shadow_near_distance_fourth;
  float shadow_far_distance_fourth;
  uint32_t shadow_kind_fourth;
  const CoinWgpuShadowReceiver * shadow_receivers_fourth;
  uint64_t shadow_receiver_count_fourth;
  float shadow_epsilon_second;
  float shadow_threshold_second;
  float shadow_epsilon_third;
  float shadow_threshold_third;
  float shadow_epsilon_fourth;
  float shadow_threshold_fourth;
  const CoinWgpuShadowPassView * extra_shadow_passes;
  uint64_t extra_shadow_pass_count;
  const CoinWgpuInstance * instances;
  uint64_t instance_count;
  const CoinWgpuInstanceRange * instance_ranges;
  uint64_t instance_range_count;
  uint32_t texture_output_flags; /* bit 0=RGBA16F direct RTT; bit 1=generate full RTT mip chain */
  uint32_t texture_sampling_policy; /* 0=native, 1=portable isotropic nearest/mip-linear */
  uint32_t rasterization_policy; /* 0=native, 1=canonical 1/256 pixel */
  const CoinWgpuBumpVertex * bump_vertices;
  uint64_t bump_vertex_count;
  const CoinWgpuBumpImage * bump_images;
  uint64_t bump_image_count;
  const CoinWgpuPortableModule *portable_modules;
  uint64_t portable_module_count;
  uint32_t multipass_count; /* 1..16, including a single unjittered pass */
  uint32_t multipass_reserved; /* must be zero */
  uint64_t multipass_budget_bytes;
  const float * multipass_matrices; /* optional sample-major/state-major MVPs; 16 floats each */
  uint64_t multipass_matrix_count; /* float count, exactly N*state_count*16 when nonzero */
  const float * multipass_clear_colors; /* optional sample-major RGBA clears */
  uint64_t multipass_clear_color_count; /* float count: zero or N*4 */
  uint64_t stereo_tokens[2];
  uint32_t stereo_mode;
  int32_t stereo_origin[2];
  int32_t stereo_extent[2];
} CoinWgpuFrameView;

/* Compile consumed modules/pipelines on the prepared default device, without
   encoding commands or acquiring/publishing a target image. */
CoinWgpuStatus coin_wgpu_preflight_multipass(uint32_t width, uint32_t height, uint32_t format, uint64_t surface);

CoinWgpuStatus coin_wgpu_prepare_portable_frame(const CoinWgpuFrameView *,
  uint32_t sample_count, char *error, size_t error_capacity);


typedef struct CoinWgpuTarget {
  uint32_t width;
  uint32_t height;
  uint8_t * color_buffer;
  uint64_t color_buffer_len;
  float * depth_buffer;
  uint64_t depth_buffer_len;
  uint64_t submission_serial;
  CoinWgpuDeviceId device_id;
  uint32_t sample_count;
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
CoinWgpuStatus coin_wgpu_preflight_cube_rtt(uint32_t face_size, uint32_t mipmapped, uint32_t samples);
CoinWgpuStatus coin_wgpu_assemble_cube_rtt(const uint64_t * faces, uint32_t face_size,
  uint32_t mipmapped, uint64_t generation, uint64_t * out_token, char * error_buf, size_t error_buf_len);

CoinWgpuStatus coin_wgpu_device_create(CoinWgpuDeviceId * out_id,
  char * error_buf, size_t error_buf_len);
CoinWgpuStatus coin_wgpu_device_destroy(CoinWgpuDeviceId id);
#ifdef COIN_WGPU_TEST_HOOKS
void coin_wgpu_inject_device_fault(CoinWgpuDeviceId id, int32_t code);
#endif

void coin_wgpu_get_adapter_info(char * buffer, size_t buffer_len);

struct CoinWgpuRuntimeCapabilities {
  uint32_t struct_size;
  uint32_t renderer, vendor_id, device_id;
  uint32_t max_framebuffer_attachments;
  uint32_t format_rgba8, format_d24s8, format_d32f, format_rgba16f, format_r16f;
  uint64_t runtime_features;
  uint64_t available_mechanisms;
};
CoinWgpuStatus coin_wgpu_query_runtime_capabilities(CoinWgpuRuntimeCapabilities* output,
                                                    size_t output_size);

// Hardware admission only; no allocation or submission. Formats use Core enum bits.
// Additive private query; no layout or existing protocol signature changes.
CoinWgpuStatus coin_wgpu_preflight_weighted(uint32_t sample_count);
CoinWgpuStatus coin_wgpu_preflight_peeling(uint32_t sample_count);
CoinWgpuStatus coin_wgpu_preflight_sample_depth(uint32_t sample_count);
CoinWgpuStatus coin_wgpu_preflight_shadow_multitexture(uint32_t sample_count);
CoinWgpuStatus coin_wgpu_preflight_bump_receiver(uint32_t samples, uint64_t vertices,
                                                 uint64_t materials, uint32_t image_axis,
                                                 uint32_t sampling_policy);
CoinWgpuStatus coin_wgpu_preflight_shadow_multitexture_alpha(uint32_t sample_count, uint32_t modes, uint32_t extent, uint32_t layers);
CoinWgpuStatus coin_wgpu_preflight_shadow_multitexture_depth(uint32_t sample_count);
CoinWgpuStatus coin_wgpu_preflight_texture_profile(uint32_t formats, uint32_t outputs, uint32_t anisotropy);
// Private pipeline/extent admission; no output image allocation or GPU submission.
CoinWgpuStatus coin_wgpu_preflight_raster_images(uint32_t outputs, uint32_t width, uint32_t height, uint64_t bytes);

CoinWgpuStatus coin_wgpu_submit(
  CoinWgpuTarget * target,
  const CoinWgpuFrameView * frame,
  char * error_buf,
  size_t error_buf_len
);

/* Target-owned synchronous depth history. The token is released on target
   resize/destruction; no connection or runtime lifetime is extended. */
CoinWgpuStatus coin_wgpu_submit_with_depth_history(CoinWgpuTarget *, const CoinWgpuFrameView *,
  uint64_t * history, char * error_buf, size_t error_buf_len);
void coin_wgpu_release_depth_history(uint64_t history);

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
uint64_t coin_wgpu_default_device_generation(void);
#if defined(__ANDROID__)
// Internal idle hook; refuses live surfaces, tickets, RTT tokens or extra devices.
CoinWgpuStatus coin_wgpu_release_idle_android_runtime(void);
#endif
void coin_wgpu_rtt_resource_counts(uint64_t * active, uint64_t * retired);
// Remaining capacity on the default device; read-only and no device initialization.
uint32_t coin_wgpu_default_rtt_capacity(void);

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

/* Compare every descriptor field with the retained job/tombstone without
   advancing mapping, consuming errors or changing its lifetime. */
CoinWgpuStatus coin_wgpu_readback_validate(
  const CoinWgpuReadbackTicket * ticket,
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

// Zero before publication and after resize; failed candidates preserve it.
uint64_t coin_wgpu_surface_submission_serial(CoinWgpuSurfaceId surface);

CoinWgpuStatus coin_wgpu_surface_submit(
  CoinWgpuSurfaceId surface,
  const CoinWgpuFrameView * frame,
  char * error_buf,
  size_t error_buf_len
);

/* Capture RGBA8 from the exact frame submitted to this window. The caller
 * supplies width*height*4 bytes; output is only committed on success. */
CoinWgpuStatus coin_wgpu_surface_submit_readback(
  CoinWgpuSurfaceId surface,
  const CoinWgpuFrameView * frame,
  uint8_t * rgba,
  size_t rgba_len,
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

int coin_wgpu_readback_resource_load(uint64_t* jobs, uint64_t* bytes);
uint64_t coin_wgpu_readback_staging_bytes(void);
void coin_wgpu_poll_device(void);

#ifdef COIN_WGPU_TEST_HOOKS
void coin_wgpu_set_cache_budget(uint64_t max_bytes, uint64_t max_stale_serials);
#endif

void coin_wgpu_trim_cache(void);

#ifdef COIN_WGPU_TEST_HOOKS
void coin_wgpu_inject_fault(int32_t fault_code);
#endif
/* Test-only deterministic fault on an offscreen submit after skipped submits. */
#ifdef COIN_WGPU_TEST_HOOKS
void coin_wgpu_inject_fault_after_submits(int32_t fault_code, uint32_t skipped_submits);
#endif

#ifdef COIN_WGPU_TEST_HOOKS
void coin_wgpu_inject_async_fault(int32_t fault_code);
#endif

#ifdef COIN_WGPU_TEST_HOOKS
void coin_wgpu_reset_context(void);
#endif

#ifdef __cplusplus
}
#endif

#endif // !COIN_WGPU_FFI_H
