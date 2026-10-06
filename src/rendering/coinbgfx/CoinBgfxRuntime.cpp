#include "rendering/coinbgfx/CoinBgfxBackend.h"
#include "rendering/coinrender/CoinRenderBackendRuntime.h"

namespace {
class CoinBgfxRuntime : public CoinRenderBackendRuntime {
public:
  bool supportsWindowTargets() const override { return true; }
  std::string surfaceTypeDiagnostic(uint32_t type) const override {
    switch (type) {
    case COIN_RENDER_SURFACE_XLIB: return {};
    case COIN_RENDER_SURFACE_WAYLAND: return "Wayland surface requires the Linux Rust bridge";
    case COIN_RENDER_SURFACE_WIN32:
#if defined(_WIN32)
      return {};
#else
      return "Win32 surface requires a Windows render backend";
#endif
    case COIN_RENDER_SURFACE_APPKIT_LAYER: return "AppKit layer requires the macOS Rust bridge";
    case COIN_RENDER_SURFACE_ANDROID_NDK: return "Android NDK surface requires the Android Rust bridge";
    default: return "Unknown native surface type in CoinRenderNativeSurfaceDescriptor";
    }
  }
  CoinRenderTarget::ReadbackStatus pollReadback(const CoinRenderReadbackTicket & ticket,
      std::vector<uint8_t> & color, std::vector<float> & depth, SbString * diagnostic) override {
    return CoinBgfxBackend::pollReadback(ticket, color, depth, diagnostic);
  }
  bool cancelReadback(const CoinRenderReadbackTicket & ticket) override {
    return CoinBgfxBackend::cancelReadback(ticket);
  }
};
}
CoinRenderBackendRuntime & CoinBgfxBackend::runtime() {
  // Process lifetime: targets may be destroyed during application shutdown.
  static auto * instance = new CoinBgfxRuntime;
  return *instance;
}
