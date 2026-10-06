// Composition point: this file connects the common target to a compiled executor.
#include "config.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderBackendRuntime.h"
#include <Inventor/rendering/CoinRenderCapabilities.h>

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
#include "rendering/coinwgpu/CoinWgpuBackend.h"
#elif defined(HAVE_COIN_DAWN) || defined(HAVE_COIN_WGPU_NATIVE)
#include "rendering/coinwgpu/CoinWgpuNativeBackend.h"
#elif defined(HAVE_COIN_BGFX)
#include "rendering/coinbgfx/CoinBgfxBackend.h"
#else
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#endif

std::unique_ptr<CoinRenderBackend>
CoinRenderTargetP::createBackend()
{
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  return std::unique_ptr<CoinRenderBackend>(new CoinWgpuBackend());
#elif defined(HAVE_COIN_DAWN) || defined(HAVE_COIN_WGPU_NATIVE)
  return std::unique_ptr<CoinRenderBackend>(new CoinWgpuNativeBackend());
#elif defined(HAVE_COIN_BGFX)
  return std::unique_ptr<CoinRenderBackend>(new CoinBgfxBackend());
#else
  return std::unique_ptr<CoinRenderBackend>(new CoinRenderCpuReferenceBackend());
#endif
}

bool
CoinRenderTargetP::isGpuBackendAvailable()
{
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  return CoinWgpuBackend::isAvailable();
#elif defined(HAVE_COIN_BGFX)
  CoinRenderCapabilities caps{};
  caps.struct_size = sizeof(caps);
  return coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
    &caps, sizeof(caps)) == 0 && caps.gpu_available;
#elif defined(HAVE_COIN_DAWN) || defined(HAVE_COIN_WGPU_NATIVE)
  return CoinWgpuNativeBackend::isAvailable();
#else
  return false;
#endif
}

CoinRenderBackendRuntime & CoinRenderTargetP::backendRuntime() {
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  return CoinWgpuBackend::runtime();
#elif defined(HAVE_COIN_BGFX)
  return CoinBgfxBackend::runtime();
#else
  // Process lifetime: targets may be destroyed during application shutdown.
  static auto * instance = new CoinRenderBackendRuntime;
  return *instance;
#endif
}

bool CoinRenderTargetP::compiledBackendInitializesCpuDepthBuffer() {
  // Implementation fact only; constructing an unprepared executor creates no device.
  static const bool value = createBackend()->initializesCpuDepthBuffer();
  return value;
}
