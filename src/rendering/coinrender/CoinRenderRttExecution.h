#ifndef COIN_RENDER_RTT_EXECUTION_H
#define COIN_RENDER_RTT_EXECUTION_H

#include "rendering/coinrender/CoinRenderRttCore.h"
#include "rendering/coinrender/CoinRenderBackend.h"
#include <memory>

class CoinRenderTarget;
struct CoinRenderReadbackTicket;

// Execution lifetime covers all producer and consumer submissions. Backends
// retain GPU work after submission as required by their own fences/queues.
class COIN_RENDER_DLL_API CoinRenderRttExecution {
public:
  CoinRenderRttExecution(CoinRenderTargetP* target, const CoinRenderOptions& options);
  ~CoinRenderRttExecution();
  CoinRenderRttExecution(const CoinRenderRttExecution&) = delete;
  CoinRenderRttExecution& operator=(const CoinRenderRttExecution&) = delete;
  CoinRenderSubmitResult prepare(const CoinRenderRttPlan&, const CoinRenderFramePlan&,
                                 CoinRenderFramePlan& resolvedRoot);

private:
  CoinRenderTargetP* target;
  CoinRenderOptions options;
  uint64_t owner = 0;
  CoinRenderBackend* directBackend = nullptr;
  CoinRenderRttResources resources;
  std::vector<uint64_t> tokens;
  CoinRenderResourceStamp stamp() const;
};
#endif
