#include <Inventor/CoinRenderExport.h>
#ifndef COIN_RENDER_CPU_REFERENCE_BACKEND_H
#define COIN_RENDER_CPU_REFERENCE_BACKEND_H

#include "rendering/coinrender/CoinRenderBackend.h"
#include <string>

class COIN_RENDER_DLL_API CoinRenderCpuReferenceBackend : public CoinRenderBackend {
public:
  // Internal deterministic fault observation. State belongs to the calling
  // thread and spans fresh backend instances; no target is changed by arming.
  // -1 disarms and retains the observation; nonnegative values reset it.
  static void setSubmissionFaultCountdown(int64_t successfulBeforeFailure);
  static uint64_t submissionFaultSuccessCount();

  CoinRenderCpuReferenceBackend();
  virtual ~CoinRenderCpuReferenceBackend();

  bool isGpuBackend() const override { return false; }
  CoinRenderBackendStatus getStatus() const override;
  CoinRenderBackendStatus prepare(CoinRenderTargetP & target) override;
  CoinRenderSubmitResult preflightMultipass(const CoinRenderFramePlan &, const SbVec2i32 &) const override;
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan &, const CoinRenderFramePlan &,
                                     const SbVec2i32 &) const override;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target) override;
  void poll() override {}
  const std::string & getLastError() const override { return this->lastError; }

private:
  CoinRenderSubmitResult renderSingle(const CoinRenderFramePlan &,
    const std::vector<CoinRenderRenderStateSnapshot> &, CoinRenderTargetP &, int sample);
  CoinRenderBackendStatus status;
  std::string lastError;
};

#endif // !COIN_RENDER_CPU_REFERENCE_BACKEND_H
