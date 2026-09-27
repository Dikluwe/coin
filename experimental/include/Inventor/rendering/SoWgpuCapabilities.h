#ifndef COIN_SOWGPUCAPABILITIES_H
#define COIN_SOWGPUCAPABILITIES_H

#include <Inventor/CoinWgpuExport.h>
#include <stddef.h>
#include <stdint.h>

#define COIN_WGPU_CAPABILITIES_VERSION 2u

#ifdef __cplusplus
extern "C" {
#endif

enum CoinWgpuExperimentalBackend {
  COIN_WGPU_EXPERIMENTAL_RECORDING = 0,
  COIN_WGPU_EXPERIMENTAL_RUST = 1,
  COIN_WGPU_EXPERIMENTAL_NATIVE_SPIKE = 2,
  COIN_WGPU_EXPERIMENTAL_BGFX_EVALUATION = 3
};

enum CoinWgpuExperimentalTarget {
  COIN_WGPU_EXPERIMENTAL_OFFSCREEN = 0,
  COIN_WGPU_EXPERIMENTAL_XLIB_WINDOW = 1
};

enum CoinWgpuExperimentalFeature {
  COIN_WGPU_FEATURE_TRIANGLES = UINT64_C(1) << 0,
  COIN_WGPU_FEATURE_INDEXED_GEOMETRY = UINT64_C(1) << 1,
  COIN_WGPU_FEATURE_LINES_POINTS = UINT64_C(1) << 2,
  COIN_WGPU_FEATURE_TEXTURE_2D = UINT64_C(1) << 3,
  COIN_WGPU_FEATURE_LIGHTS = UINT64_C(1) << 4,
  COIN_WGPU_FEATURE_FOG = UINT64_C(1) << 5,
  COIN_WGPU_FEATURE_SORTED_ALPHA = UINT64_C(1) << 6,
  COIN_WGPU_FEATURE_COLOR_DEPTH = UINT64_C(1) << 7,
  COIN_WGPU_FEATURE_ASYNC_READBACK = UINT64_C(1) << 8,
  COIN_WGPU_FEATURE_DIRECT_RTT = UINT64_C(1) << 9
};

enum CoinWgpuExperimentalProbeStatus {
  COIN_WGPU_PROBE_NOT_RUN = 0,
  COIN_WGPU_PROBE_AVAILABLE = 1,
  COIN_WGPU_PROBE_UNAVAILABLE = 2,
  COIN_WGPU_PROBE_BUSY = 3,
  COIN_WGPU_PROBE_ERROR = 4
};

enum CoinWgpuExperimentalRenderer {
  COIN_WGPU_RENDERER_UNKNOWN = 0,
  COIN_WGPU_RENDERER_VULKAN = 1,
  COIN_WGPU_RENDERER_OPENGL = 2,
  COIN_WGPU_RENDERER_OTHER = 3
};

enum CoinWgpuExperimentalRuntimeFeature {
  COIN_WGPU_RUNTIME_MRT = UINT64_C(1) << 0,
  COIN_WGPU_RUNTIME_INDEPENDENT_BLEND = UINT64_C(1) << 1,
  COIN_WGPU_RUNTIME_COMPUTE = UINT64_C(1) << 2,
  COIN_WGPU_RUNTIME_TIMESTAMPS = UINT64_C(1) << 3
};

enum CoinWgpuExperimentalFormatFeature {
  COIN_WGPU_FORMAT_TEXTURE_2D = UINT32_C(1) << 0,
  COIN_WGPU_FORMAT_FRAMEBUFFER = UINT32_C(1) << 1,
  COIN_WGPU_FORMAT_FRAMEBUFFER_MSAA = UINT32_C(1) << 2,
  COIN_WGPU_FORMAT_IMAGE_READ = UINT32_C(1) << 3,
  COIN_WGPU_FORMAT_IMAGE_WRITE = UINT32_C(1) << 4
};

/**
 * Versioned capabilities returned by the experimental WebGPU module.
 *
 * Initialize access through coin_wgpu_experimental_query_capabilities()
 * instead of assuming that a compiled feature has a runtime adapter.
 *
 * \see coin_wgpu_experimental
 */
typedef struct CoinWgpuExperimentalCapabilities {
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
} CoinWgpuExperimentalCapabilities;

/**
 * Queries compiled capabilities and runtime GPU availability.
 *
 * \return 0 on success, 1 for an unsupported target, or 2 for invalid output.
 * Feature bits describe the compiled profile. Version 2 additionally performs a
 * runtime probe and reports renderer, adapter IDs, selected format flags and
 * runtime feature bits. A caller compiled with the exact version 1 struct size
 * is still accepted and receives the compatible prefix with version set to 1.
 * This is an experimental contract, not an addition to libCoin's public ABI.
 */
COIN_WGPU_DLL_API int32_t coin_wgpu_experimental_query_capabilities(
  uint32_t target, void * output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif // COIN_SOWGPUCAPABILITIES_H
