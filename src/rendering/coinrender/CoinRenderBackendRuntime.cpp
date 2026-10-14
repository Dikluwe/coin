#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderBackendRuntime.h"

std::string
CoinRenderBackendRuntime::surfaceTypeDiagnostic(uint32_t) const
{
  return "Native window surface targets require the RUST_BRIDGE or BGFX backend.";
}

CoinRenderTarget::ReadbackStatus
CoinRenderBackendRuntime::pollReadback(const CoinRenderReadbackTicket &, std::vector<uint8_t> &,
                                       std::vector<float> &, SbString *diagnostic)
{
  if (diagnostic)
    *diagnostic = "The selected backend does not support asynchronous readback";
  return CoinRenderTarget::READBACK_UNSUPPORTED;
}
