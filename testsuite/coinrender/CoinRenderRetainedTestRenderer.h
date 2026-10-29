#ifndef COIN_RENDER_RETAINED_TEST_RENDERER_H
#define COIN_RENDER_RETAINED_TEST_RENDERER_H
#include <Inventor/rendering/CoinRenderOptions.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>

// Explicit API selection for cross-platform qualification. Never substitute a
// different API when the requested adapter is unavailable.
inline bool coinRetainedTestRenderer(CoinRenderOptions & options) {
  const char * requested = std::getenv("COIN_RETAINED_TEST_RENDERER");
  if (!requested) return true; // Preserve the existing desktop test default.
  if (!std::strcmp(requested, "opengl")) options.renderer = COIN_RENDER_RENDERER_OPENGL;
  else if (!std::strcmp(requested, "vulkan")) options.renderer = COIN_RENDER_RENDERER_VULKAN;
  else { std::fprintf(stderr, "Unknown retained test renderer: %s\n", requested); return false; }
  CoinRenderCapabilities caps{};
  const int status = coin_render_query_capabilities_for_renderer(
    COIN_RENDER_EXPERIMENTAL_OFFSCREEN, options.renderer, &caps, sizeof(caps));
  std::fprintf(stderr, "retained_test_renderer requested=%u observed=%u gpu_available=%u adapter=%s diagnostic=%s\n",
    unsigned(options.renderer), caps.renderer, caps.gpu_available, caps.adapter_name, caps.diagnostic);
  return status == 0 && caps.gpu_available && caps.renderer == unsigned(options.renderer);
}
#endif
