#ifndef COIN_RENDER_CAPABILITIES_H
#define COIN_RENDER_CAPABILITIES_H

#include <Inventor/CoinRenderExport.h>
#include <stddef.h>
#include <stdint.h>

#define COIN_RENDER_CAPABILITIES_VERSION 3u

#ifdef __cplusplus
extern "C" {
#endif

enum CoinRenderBackendKind {
  COIN_RENDER_EXPERIMENTAL_RECORDING = 0,
  COIN_RENDER_EXPERIMENTAL_RUST = 1,
  COIN_RENDER_EXPERIMENTAL_NATIVE_SPIKE = 2,
  COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION = 3
};

enum CoinRenderTargetKind {
  COIN_RENDER_EXPERIMENTAL_OFFSCREEN = 0,
  COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW = 1
};

enum CoinRenderFeature {
  COIN_RENDER_FEATURE_TRIANGLES = UINT64_C(1) << 0,
  COIN_RENDER_FEATURE_INDEXED_GEOMETRY = UINT64_C(1) << 1,
  COIN_RENDER_FEATURE_LINES_POINTS = UINT64_C(1) << 2,
  COIN_RENDER_FEATURE_TEXTURE_2D = UINT64_C(1) << 3,
  COIN_RENDER_FEATURE_LIGHTS = UINT64_C(1) << 4,
  COIN_RENDER_FEATURE_FOG = UINT64_C(1) << 5,
  COIN_RENDER_FEATURE_SORTED_ALPHA = UINT64_C(1) << 6,
  COIN_RENDER_FEATURE_COLOR_DEPTH = UINT64_C(1) << 7,
  COIN_RENDER_FEATURE_ASYNC_READBACK = UINT64_C(1) << 8,
  COIN_RENDER_FEATURE_DIRECT_RTT = UINT64_C(1) << 9,
  COIN_RENDER_FEATURE_CLIP_PLANES = UINT64_C(1) << 10 /* up to eight active planes */
};

enum CoinRenderProbeStatus {
  COIN_RENDER_PROBE_NOT_RUN = 0,
  COIN_RENDER_PROBE_AVAILABLE = 1,
  COIN_RENDER_PROBE_UNAVAILABLE = 2,
  COIN_RENDER_PROBE_BUSY = 3,
  COIN_RENDER_PROBE_ERROR = 4
};

enum CoinRenderRenderer {
  COIN_RENDER_RENDERER_UNKNOWN = 0,
  COIN_RENDER_RENDERER_VULKAN = 1,
  COIN_RENDER_RENDERER_OPENGL = 2,
  COIN_RENDER_RENDERER_OTHER = 3,
  COIN_RENDER_RENDERER_D3D12 = 4
};

enum CoinRenderTransparencyMode {
  COIN_RENDER_TRANSPARENCY_COIN = 0,
  COIN_RENDER_TRANSPARENCY_OBJECT = 1,
  COIN_RENDER_TRANSPARENCY_PEELING = 2,
  COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT = 3
};

enum CoinRenderMechanism {
  COIN_RENDER_MECHANISM_OBJECT = UINT64_C(1) << 0,
  COIN_RENDER_MECHANISM_PEELING = UINT64_C(1) << 1,
  COIN_RENDER_MECHANISM_WEIGHTED_OIT = UINT64_C(1) << 2
};

enum CoinRenderQualifiedProfile {
  COIN_RENDER_PROFILE_P08_MULTITEXTURE = UINT64_C(1) << 0,
  COIN_RENDER_PROFILE_P09_TRANSPARENCY = UINT64_C(1) << 1,
  COIN_RENDER_PROFILE_P10_PEELING = UINT64_C(1) << 2,
  COIN_RENDER_PROFILE_P10_BGFX_WEIGHTED_OIT = UINT64_C(1) << 3
};

enum CoinRenderHardwareFact {
  COIN_RENDER_HARDWARE_RENDERER = UINT64_C(1) << 0,
  COIN_RENDER_HARDWARE_ADAPTER_IDS = UINT64_C(1) << 1,
  COIN_RENDER_HARDWARE_FORMATS = UINT64_C(1) << 2,
  COIN_RENDER_HARDWARE_LIMITS = UINT64_C(1) << 3,
  COIN_RENDER_HARDWARE_RUNTIME_FEATURES = UINT64_C(1) << 4
};

enum CoinRenderKnownFormat {
  COIN_RENDER_KNOWN_RGBA8 = UINT64_C(1) << 0,
  COIN_RENDER_KNOWN_D24S8 = UINT64_C(1) << 1,
  COIN_RENDER_KNOWN_D32F = UINT64_C(1) << 2,
  COIN_RENDER_KNOWN_RGBA16F = UINT64_C(1) << 3,
  COIN_RENDER_KNOWN_R16F = UINT64_C(1) << 4
};

enum CoinRenderSelectionReason {
  COIN_RENDER_SELECTION_SUPPORTED = 0,
  COIN_RENDER_SELECTION_INVALID_REQUEST = 1,
  COIN_RENDER_SELECTION_NOT_IMPLEMENTED = 2,
  COIN_RENDER_SELECTION_HARDWARE_UNAVAILABLE = 3,
  COIN_RENDER_SELECTION_UNQUALIFIED_PROFILE = 4,
  COIN_RENDER_SELECTION_COIN_MODE_CONFLICT = 5,
  COIN_RENDER_SELECTION_RUNTIME_NOT_READY = 6
};

typedef struct CoinRenderSelection {
  uint64_t mechanism;
  uint32_t reason;
  uint32_t qualified_profile;
} CoinRenderSelection;

enum CoinRenderRuntimeFeature {
  COIN_RENDER_RUNTIME_MRT = UINT64_C(1) << 0,
  COIN_RENDER_RUNTIME_INDEPENDENT_BLEND = UINT64_C(1) << 1,
  COIN_RENDER_RUNTIME_COMPUTE = UINT64_C(1) << 2,
  COIN_RENDER_RUNTIME_TIMESTAMPS = UINT64_C(1) << 3
};

enum CoinRenderFormatFeature {
  COIN_RENDER_FORMAT_TEXTURE_2D = UINT32_C(1) << 0,
  COIN_RENDER_FORMAT_FRAMEBUFFER = UINT32_C(1) << 1,
  COIN_RENDER_FORMAT_FRAMEBUFFER_MSAA = UINT32_C(1) << 2,
  COIN_RENDER_FORMAT_IMAGE_READ = UINT32_C(1) << 3,
  COIN_RENDER_FORMAT_IMAGE_WRITE = UINT32_C(1) << 4
};

/**
 * Versioned capabilities returned by the experimental CoinRender module.
 *
 * Initialize access through coin_render_query_capabilities()
 * instead of assuming that a compiled feature has a runtime adapter.
 *
 * \see coin_render_experimental
 */
typedef struct CoinRenderCapabilities {
  uint32_t struct_size;
  uint32_t version;
  uint32_t backend;
  uint32_t target;
  uint32_t gpu_available;
  uint32_t max_lights_per_draw;
  uint32_t max_texture_units;
  uint32_t max_scene_texture_depth;
  uint64_t features;
  uint64_t max_scene_texture_bytes_per_apply;
  char adapter_name[128]; /* empty when no runtime GPU adapter is available */
  /* Version 2 fields. The complete version 1 layout remains the prefix. */
  uint32_t probe_status;
  uint32_t renderer;
  uint32_t vendor_id;
  uint32_t device_id;
  uint32_t max_framebuffer_attachments;
  uint32_t format_rgba8;
  uint32_t format_d24s8;
  uint32_t format_d32f;
  uint32_t format_rgba16f;
  uint32_t format_r16f;
  uint64_t runtime_features;
  char diagnostic[256];
  /* Version 3: facts are distinct from executable mechanisms and tested profiles.
   * Profile qualification is bounded source/test evidence, not certification
   * of the current physical device or every Coin state combination. */
  uint64_t known_hardware_facts;
  uint64_t known_formats; /* a zero format field is unknown unless its bit is set */
  uint64_t implemented_mechanisms;
  uint64_t available_mechanisms;
  uint64_t qualified_profile_mechanisms;
  uint64_t qualified_profiles;
  uint32_t max_peel_layers;
  uint32_t reserved;
} CoinRenderCapabilities;

/**
 * Queries compiled capabilities and runtime GPU availability.
 *
 * \return 0 on success, 1 for an unsupported target, or 2 for invalid output.
 * Feature bits describe the compiled profile, not runtime availability. Version 2 performs a
 * runtime probe and reports renderer, adapter IDs, selected format flags and
 * runtime feature bits. A caller compiled with the exact version 1 struct size
 * is still accepted and receives the compatible prefix with version set to 1.
 * Exact version 2 size is also accepted. Version 3 separates known hardware
 * facts, implemented/available mechanisms, and bounded qualified profiles.
 * Window queries probe an adapter, not a real presentation surface.
 * This is an experimental contract, not an addition to libCoin's public ABI.
 */
COIN_RENDER_DLL_API int32_t coin_render_query_capabilities(
  uint32_t target, void * output, size_t output_size);

/** Probe with an explicit renderer, independent of environment options.
 * BGFX chooses that renderer; wgpu reports its active adapter and rejects a
 * mismatch. UNKNOWN means the connector default. Return codes match query. */
COIN_RENDER_DLL_API int32_t coin_render_query_capabilities_for_renderer(
    uint32_t target, enum CoinRenderRenderer renderer, void* output, size_t output_size);

/** Pure selection; never initializes a runtime or selects a fallback. */
COIN_RENDER_DLL_API CoinRenderSelection
coin_render_select_mechanism(const CoinRenderCapabilities* capabilities, uint64_t mechanism,
                             uint32_t require_qualified_profile);

#ifdef __cplusplus
}
#endif

#endif // COIN_RENDER_CAPABILITIES_H
