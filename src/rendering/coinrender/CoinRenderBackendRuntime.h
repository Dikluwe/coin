#ifndef COIN_RENDER_BACKEND_RUNTIME_H
#define COIN_RENDER_BACKEND_RUNTIME_H

#include <Inventor/rendering/CoinRenderTarget.h>
#include <string>

class CoinRenderTargetP;

// Private Infra contract. The compiled runtime outlives targets and their tickets.
// Implementations do not own the host's native window/display handles.
class CoinRenderBackendRuntime {
public:
  virtual ~CoinRenderBackendRuntime() {}
  virtual bool supportsWindowTargets() const { return false; }
  virtual std::string surfaceTypeDiagnostic(uint32_t) const {
    return "Native window surface targets require the RUST_BRIDGE or BGFX backend.";
  }
  virtual void destroySurface(CoinRenderTargetP &) {}
  virtual CoinRenderTarget::ReadbackStatus pollReadback(const CoinRenderReadbackTicket &,
      std::vector<uint8_t> &, std::vector<float> &, SbString * diagnostic) {
    if (diagnostic) *diagnostic = "The selected backend does not support asynchronous readback";
    return CoinRenderTarget::READBACK_UNSUPPORTED;
  }
  virtual bool cancelReadback(const CoinRenderReadbackTicket &) { return false; }
  virtual bool cacheTelemetry(CoinRenderCacheTelemetry &) const { return false; }
  virtual void pollDevice() {}
};

#endif
