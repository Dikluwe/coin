#include "rendering/coinwgpu/CoinWgpuBackend.h"
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#include "rendering/coinrender/CoinRenderBackendRuntime.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <exception>

namespace {
class CoinWgpuRuntime : public CoinRenderBackendRuntime {
public:
  bool supportsWindowTargets() const override { return true; }
  std::string surfaceTypeDiagnostic(uint32_t type) const override {
    switch (type) {
    case COIN_RENDER_SURFACE_XLIB: return {};
    case COIN_RENDER_SURFACE_WAYLAND:
#if defined(__linux__)
      return {};
#else
      return "Wayland surface requires the Linux Rust bridge";
#endif
    case COIN_RENDER_SURFACE_WIN32:
#if defined(_WIN32)
      return {};
#else
      return "Win32 surface requires a Windows render backend";
#endif
    case COIN_RENDER_SURFACE_APPKIT_LAYER:
#if defined(__APPLE__)
      return {};
#else
      return "AppKit layer requires the macOS Rust bridge";
#endif
    case COIN_RENDER_SURFACE_ANDROID_NDK:
#if defined(__ANDROID__)
      return {};
#else
      return "Android NDK surface requires the Android Rust bridge";
#endif
    default: return "Unknown native surface type in CoinRenderNativeSurfaceDescriptor";
    }
  }
  void destroySurface(CoinRenderTargetP & target) override {
    if (target.surfaceId) {
      char error[256] = {0};
      coin_wgpu_surface_destroy(target.surfaceId, error, sizeof(error));
      target.surfaceId = 0;
    }
  }
  CoinRenderTarget::ReadbackStatus pollReadback(const CoinRenderReadbackTicket & ticket,
      std::vector<uint8_t> & outColor, std::vector<float> & outDepth,
      SbString * diagnostic) override {

    const uint64_t pixels = uint64_t(ticket.width) * uint64_t(ticket.height);
    const uint64_t bytes = pixels * 4;
    const uint64_t rowPitch = (uint64_t(ticket.width) * 4 + 255) & ~uint64_t(255);
    if (ticket.token == 0 || ticket.width == 0 || ticket.height == 0 ||
        ticket.width > 16384 || ticket.height > 16384 ||
        ticket.colorFormat != 0 || ticket.colorBytes != bytes ||
        ticket.colorRowPitch != rowPitch ||
        (ticket.depthFormat != 0 && ticket.depthFormat != 1) ||
        (ticket.depthFormat == 0 && (ticket.depthBytes != 0 || ticket.depthRowPitch != 0)) ||
        (ticket.depthFormat == 1 && (ticket.depthBytes != bytes ||
            ticket.depthRowPitch != rowPitch))) {
      if (diagnostic) *diagnostic = "Invalid asynchronous readback ticket";
      return CoinRenderTarget::READBACK_INVALID_TICKET;
    }

    char error[512] = {0};
    const CoinWgpuStatus readiness =
      coin_wgpu_readback_query(ticket.token, error, sizeof(error));
    if (readiness != COIN_WGPU_OK) {
      if (diagnostic && error[0]) *diagnostic = error;
      switch (readiness) {
        case COIN_WGPU_NOT_READY: return CoinRenderTarget::READBACK_NOT_READY;
        case COIN_WGPU_INVALID_ARGUMENT: return CoinRenderTarget::READBACK_INVALID_TICKET;
        case COIN_WGPU_DEVICE_LOST: return CoinRenderTarget::READBACK_DEVICE_LOST;
        case COIN_WGPU_UNSUPPORTED: return CoinRenderTarget::READBACK_UNSUPPORTED;
        default: return CoinRenderTarget::READBACK_ERROR;
      }
    }

    std::vector<uint8_t> color;
    std::vector<float> depth;
    try {
      color.resize(static_cast<size_t>(ticket.colorBytes));
      if (ticket.depthFormat == 1) {
        depth.resize(static_cast<size_t>(ticket.depthBytes / 4));
      }
    } catch (const std::exception &) {
      if (diagnostic) *diagnostic = "Cannot allocate asynchronous readback outputs";
      return CoinRenderTarget::READBACK_ERROR;
    }
    error[0] = 0;
    const CoinWgpuStatus status = coin_wgpu_readback_poll(ticket.token,
      color.data(), color.size(), depth.empty() ? NULL : depth.data(),
      depth.size(), error, sizeof(error));
    if (status == COIN_WGPU_OK) {
      outColor.swap(color);
      outDepth.swap(depth);
      return CoinRenderTarget::READBACK_READY;
    }
    if (diagnostic && error[0]) *diagnostic = error;
    switch (status) {
      case COIN_WGPU_NOT_READY: return CoinRenderTarget::READBACK_NOT_READY;
      case COIN_WGPU_INVALID_ARGUMENT: return CoinRenderTarget::READBACK_INVALID_TICKET;
      case COIN_WGPU_DEVICE_LOST: return CoinRenderTarget::READBACK_DEVICE_LOST;
      case COIN_WGPU_UNSUPPORTED: return CoinRenderTarget::READBACK_UNSUPPORTED;
      default: return CoinRenderTarget::READBACK_ERROR;
    }
  }
  bool cancelReadback(const CoinRenderReadbackTicket & ticket) override {
    return ticket.token && coin_wgpu_readback_cancel(ticket.token) == COIN_WGPU_OK;
  }
  bool cacheTelemetry(CoinRenderCacheTelemetry & outTelemetry) const override {
    CoinWgpuCacheStats stats;
    coin_wgpu_get_cache_stats(&stats);
    outTelemetry.cumulativeUploads = stats.cumulative_uploads;
    outTelemetry.cumulativeHits = stats.cumulative_hits;
    outTelemetry.cumulativeMisses = stats.cumulative_misses;
    outTelemetry.cumulativeUploadedBytes = stats.cumulative_uploaded_bytes;
    outTelemetry.frameUploadedBytes = stats.frame_uploaded_bytes;
    outTelemetry.frameUploads = stats.frame_uploads;
    outTelemetry.frameHits = stats.frame_hits;
    outTelemetry.activeEntries = stats.active_entries;
    outTelemetry.retiredEntries = stats.retired_entries;
    outTelemetry.completedSerial = stats.completed_serial;
    outTelemetry.submissionSerial = stats.submission_serial;
    return true;
  }
  void pollDevice() override { coin_wgpu_poll_device(); }
};
}
CoinRenderBackendRuntime & CoinWgpuBackend::runtime() {
  // Process lifetime: targets may be destroyed during application shutdown.
  static auto * instance = new CoinWgpuRuntime;
  return *instance;
}
