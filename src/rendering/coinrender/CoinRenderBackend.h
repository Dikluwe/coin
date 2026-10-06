#ifndef COIN_RENDER_BACKEND_H
#define COIN_RENDER_BACKEND_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <string>

class CoinRenderTargetP;
class CoinRenderRttPlan;
class CoinRenderBackendRuntime;
struct CoinRenderFrameReuseDecision;
struct CoinRenderReadbackTicket;

struct CoinRenderDeviceDomain {
  uint64_t device = 0;
  uint64_t generation = 0;
};

enum class CoinRenderBackendStatus {
  SUCCESS = 0,
  NOT_READY,
  UNSUPPORTED,
  OUT_OF_MEMORY,
  DEVICE_LOST,
  BACKEND_ERROR,
  SURFACE_LOST
};

struct CoinRenderSubmitResult {
  CoinRenderBackendStatus status{CoinRenderBackendStatus::SUCCESS};
  std::string diagnostic;
  uint64_t submissionSerial{0};

  CoinRenderSubmitResult() : status(CoinRenderBackendStatus::SUCCESS), diagnostic(""), submissionSerial(0) {}
  CoinRenderSubmitResult(CoinRenderBackendStatus s, const std::string & d = "", uint64_t serial = 0)
    : status(s), diagnostic(d), submissionSerial(serial) {}
};

typedef CoinRenderSubmitResult CoinRenderFrameExecutionResult;

class CoinRenderBackend {
public:
  virtual ~CoinRenderBackend() {}
  virtual bool isGpuBackend() const = 0;
  virtual bool initializesCpuDepthBuffer() const { return true; }
  virtual bool resetOnActionDetach() const { return false; }
  // Implementation fact, independent of runtime GPU availability. Core still
  // validates the captured shadow profile; Target checks the output policy.
  virtual bool supportsOffscreenShadows() const { return false; }
  virtual CoinRenderBackendStatus getStatus() const = 0;
  virtual CoinRenderBackendStatus prepare(CoinRenderTargetP & target) = 0;
  virtual CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame,
                             CoinRenderTargetP & target) = 0;
  // Core decides reuse; each executor consumes the same decision or submits afresh.
  virtual CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame,
                                        CoinRenderTargetP & target,
                                        const CoinRenderFrameReuseDecision &) {
    return this->submit(frame, target);
  }
  virtual CoinRenderSubmitResult submitAsync(const CoinRenderFramePlan &,
                                             CoinRenderTargetP &,
                                             CoinRenderReadbackTicket &,
                                             const CoinRenderFrameReuseDecision &) {
    return {CoinRenderBackendStatus::UNSUPPORTED,
            "The selected backend does not support asynchronous readback"};
  }
  // Inspect immutable captures without initializing a device or submitting work.
  virtual CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan&, const CoinRenderFramePlan&,
                                              const SbVec2i32&) const {
    return {};
  }
  // Facts only: count and nominal packed bytes; admission policy is Core.
  virtual bool readbackLoad(uint64_t&, uint64_t&) const { return false; }
  // Single-runtime Infra: all registered users must release a lost generation.
  virtual bool requiresSharedRetirement() const { return false; }
  virtual void retireLostReadbacks() {}
  virtual CoinRenderDeviceDomain resourceDomain() const { return {}; }
  virtual CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan&, const SbVec2i32&, uint64_t,
                                           CoinRenderTargetP&, uint64_t&) {
    return {CoinRenderBackendStatus::UNSUPPORTED,
            "Direct scene texture is not implemented; no fallback was applied"};
  }
  virtual void finishRtt(const std::vector<uint64_t>&) {}
  virtual bool stagedRttRequiresPreparedParent() const { return false; }
  virtual void poll() = 0;
  virtual const std::string & getLastError() const = 0;
};

#endif // !COIN_RENDER_BACKEND_H
