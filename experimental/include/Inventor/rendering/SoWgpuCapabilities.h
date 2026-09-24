#ifndef COIN_SOWGPUCAPABILITIES_H
#define COIN_SOWGPUCAPABILITIES_H

#include <Inventor/CoinWgpuExport.h>
#include <stddef.h>
#include <stdint.h>

#define COIN_WGPU_CAPABILITIES_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

enum CoinWgpuExperimentalBackend {
  COIN_WGPU_EXPERIMENTAL_RECORDING = 0,
  COIN_WGPU_EXPERIMENTAL_RUST = 1,
  COIN_WGPU_EXPERIMENTAL_NATIVE_SPIKE = 2
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
} CoinWgpuExperimentalCapabilities;

/* Returns 0 on success, 1 for an unsupported target, 2 for invalid output.
 * Feature bits describe the compiled profile; gpu_available reports runtime
 * adapter availability separately. This is an experimental module contract,
 * not an addition to libCoin's public ABI. */
COIN_WGPU_DLL_API int32_t coin_wgpu_experimental_query_capabilities(
  uint32_t target, void * output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif // COIN_SOWGPUCAPABILITIES_H
