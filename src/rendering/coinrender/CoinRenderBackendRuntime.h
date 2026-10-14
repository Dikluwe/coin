#ifndef COIN_RENDER_BACKEND_RUNTIME_H
#define COIN_RENDER_BACKEND_RUNTIME_H

#include <Inventor/CoinRenderExport.h>

#include <Inventor/rendering/CoinRenderTarget.h>
#include <string>

class CoinRenderTargetP;

// Private Infra contract. The compiled runtime outlives targets and their tickets.
// Implementations do not own the host's native window/display handles.
class COIN_RENDER_DLL_API CoinRenderBackendRuntime {
public:
  virtual ~CoinRenderBackendRuntime() {}
  virtual bool supportsWindowTargets() const { return false; }
  virtual std::string surfaceTypeDiagnostic(uint32_t) const;
  virtual void destroySurface(CoinRenderTargetP &) {}
  virtual CoinRenderTarget::ReadbackStatus pollReadback(const CoinRenderReadbackTicket &,
      std::vector<uint8_t> &, std::vector<float> &, SbString * diagnostic);
  virtual bool cancelReadback(const CoinRenderReadbackTicket &) { return false; }
  virtual bool cacheTelemetry(CoinRenderCacheTelemetry &) const { return false; }
  virtual void pollDevice() {}
  // Called only after the last registered target releases all backend objects.
  virtual void releaseIdleDevice() {}
};

#endif
