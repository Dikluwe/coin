#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderBackend.h"

CoinRenderSubmitResult
CoinRenderBackend::submit(const CoinRenderFramePlan &frame, CoinRenderTargetP &target,
                          const CoinRenderFrameReuseDecision &)
{
  return this->submit(frame, target);
}

CoinRenderSubmitResult
CoinRenderBackend::submitAsync(const CoinRenderFramePlan &, CoinRenderTargetP &,
                               CoinRenderReadbackTicket &, const CoinRenderFrameReuseDecision &)
{
  return {CoinRenderBackendStatus::UNSUPPORTED,
          "The selected backend does not support asynchronous readback"};
}

CoinRenderSubmitResult
CoinRenderBackend::preflightRtt(const CoinRenderRttPlan &, const CoinRenderFramePlan &,
                                const SbVec2i32 &) const
{
  return {};
}

CoinRenderSubmitResult
CoinRenderBackend::submitRtt(const CoinRenderFramePlan &, const SbVec2i32 &, uint64_t,
                             CoinRenderTargetP &, uint64_t &)
{
  return {CoinRenderBackendStatus::UNSUPPORTED,
          "Direct scene texture is not implemented; no fallback was applied"};
}

CoinRenderSubmitResult CoinRenderBackend::assembleCubeRtt(const std::array<uint64_t,6>&,
    uint32_t,bool,CoinRenderTargetP&,uint64_t & token) {
  token=0;return {CoinRenderBackendStatus::UNSUPPORTED,"Direct cube RTT assembly is not implemented"};
}
