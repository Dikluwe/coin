#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/rendering/SoWgpuCapabilities.h>

#if defined(HAVE_WGPU_RUST_BRIDGE)
#include "rendering/wgpu/SoWgpuRustBackend.h"
#endif

#include <cstring>

extern "C" int32_t
coin_wgpu_experimental_query_capabilities(uint32_t target,
                                            void * output,
                                            size_t output_size)
{
  if (!output || output_size < sizeof(CoinWgpuExperimentalCapabilities)) return 2;
  if (target != COIN_WGPU_EXPERIMENTAL_OFFSCREEN &&
      target != COIN_WGPU_EXPERIMENTAL_XLIB_WINDOW) return 1;

  CoinWgpuExperimentalCapabilities result{};
  result.struct_size = sizeof(result);
  result.version = COIN_WGPU_CAPABILITIES_VERSION;
  result.target = target;

#if defined(HAVE_WGPU_RUST_BRIDGE)
  result.backend = COIN_WGPU_EXPERIMENTAL_RUST;
  result.gpu_available = SoWgpuRustBackend::isAvailable() ? 1u : 0u;
  if (result.gpu_available) {
    const std::string name = SoWgpuRustBackend::getAdapterInfo();
    std::strncpy(result.adapter_name, name.c_str(), sizeof(result.adapter_name) - 1);
  }
  result.features = COIN_WGPU_FEATURE_TRIANGLES |
                    COIN_WGPU_FEATURE_INDEXED_GEOMETRY |
                    COIN_WGPU_FEATURE_LINES_POINTS |
                    COIN_WGPU_FEATURE_TEXTURE_2D |
                    COIN_WGPU_FEATURE_LIGHTS |
                    COIN_WGPU_FEATURE_FOG |
                    COIN_WGPU_FEATURE_SORTED_ALPHA |
                    COIN_WGPU_FEATURE_COLOR_DEPTH;
  result.max_lights_per_draw = 8;
  result.max_texture_units = 1;
  if (target == COIN_WGPU_EXPERIMENTAL_OFFSCREEN) {
    result.features |= COIN_WGPU_FEATURE_ASYNC_READBACK |
                       COIN_WGPU_FEATURE_DIRECT_RTT;
    result.max_scene_texture_depth = 8;
    result.max_scene_texture_bytes_per_apply = UINT64_C(64) * 1024 * 1024;
  }
#elif defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE)
  // These backends are still spikes: isAvailable() is false and their
  // supported rendering profile is not established.
  result.backend = COIN_WGPU_EXPERIMENTAL_NATIVE_SPIKE;
  if (target == COIN_WGPU_EXPERIMENTAL_XLIB_WINDOW) return 1;
#else
  result.backend = COIN_WGPU_EXPERIMENTAL_RECORDING;
  if (target == COIN_WGPU_EXPERIMENTAL_XLIB_WINDOW) return 1;
  result.features = COIN_WGPU_FEATURE_TRIANGLES |
                    COIN_WGPU_FEATURE_INDEXED_GEOMETRY |
                    COIN_WGPU_FEATURE_LINES_POINTS |
                    COIN_WGPU_FEATURE_TEXTURE_2D |
                    COIN_WGPU_FEATURE_LIGHTS |
                    COIN_WGPU_FEATURE_FOG |
                    COIN_WGPU_FEATURE_SORTED_ALPHA |
                    COIN_WGPU_FEATURE_COLOR_DEPTH;
  result.max_lights_per_draw = 8;
  result.max_texture_units = 1;
  result.max_scene_texture_depth = 8;
  result.max_scene_texture_bytes_per_apply = UINT64_C(64) * 1024 * 1024;
#endif

#if !defined(__linux__)
  if (target == COIN_WGPU_EXPERIMENTAL_XLIB_WINDOW) return 1;
#endif

  std::memcpy(output, &result, sizeof(result));
  return 0;
}
