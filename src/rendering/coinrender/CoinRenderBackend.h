#ifndef COIN_RENDER_BACKEND_H
#define COIN_RENDER_BACKEND_H
#include <array>

#include <Inventor/CoinRenderExport.h>

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

class COIN_RENDER_DLL_API CoinRenderBackend {
public:
  virtual ~CoinRenderBackend() {}
  virtual bool isGpuBackend() const = 0;
  virtual bool initializesCpuDepthBuffer() const { return true; }
  virtual bool resetOnActionDetach() const { return false; }
  // Implementation fact, independent of runtime GPU availability. Core still
  // validates the captured shadow profile; Target checks the output policy.
  virtual bool supportsOffscreenShadows() const { return false; }
  virtual bool supportsOpaqueBump() const { return false; }
  virtual CoinRenderBackendStatus getStatus() const = 0;
  virtual CoinRenderBackendStatus prepare(CoinRenderTargetP & target) = 0;
  virtual CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame,
                             CoinRenderTargetP & target) = 0;
  // Core decides reuse; each executor consumes the same decision or submits afresh.
  virtual CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame,
                                        CoinRenderTargetP & target,
                                        const CoinRenderFrameReuseDecision &);
  virtual CoinRenderSubmitResult submitAsync(const CoinRenderFramePlan &,
                                             CoinRenderTargetP &,
                                             CoinRenderReadbackTicket &,
                                             const CoinRenderFrameReuseDecision &);
  // Inspect immutable captures without initializing a device or submitting work.
  virtual CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan&, const CoinRenderFramePlan&,
                                              const SbVec2i32&) const;
  // Facts only: count and nominal packed bytes; admission policy is Core.
  virtual bool readbackLoad(uint64_t&, uint64_t&) const { return false; }
  // Single-runtime Infra: all registered users must release a lost generation.
  virtual bool requiresSharedRetirement() const { return false; }
  virtual void retireLostReadbacks() {}
  virtual CoinRenderDeviceDomain resourceDomain() const { return {}; }
  virtual CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan&, const SbVec2i32&, uint64_t,
                                           CoinRenderTargetP&, uint64_t&);
  virtual void finishRtt(const std::vector<uint64_t>&) {}
  virtual bool stagedRttRequiresPreparedParent() const { return false; }
  virtual void poll() = 0;
  virtual const std::string & getLastError() const = 0;
  virtual CoinRenderSubmitResult assembleCubeRtt(const std::array<uint64_t,6>&, uint32_t,
                                                bool, CoinRenderTargetP&, uint64_t&);
  // Retire only these owned candidates after their last queued use. Distinct
  // from finishRtt, which may also prune/recycle the backend's retained cache.
  virtual void retireRtt(const std::vector<uint64_t>&) {}
  // Copy immutable target facts into an unprepared executor without creating
  // a device. In particular, first-frame admission must see the real MSAA count.
  virtual void configureMultipassTarget(const CoinRenderTargetP &) {}
  virtual bool multipassOutputBytes(const CoinRenderTargetP &, bool asynchronous, uint64_t &) const;
  CoinRenderSubmitResult preflightMultipassOutput(const CoinRenderFramePlan &,
    const CoinRenderTargetP &, bool asynchronous, const CoinRenderRttPlan * = nullptr) const;
  virtual CoinRenderSubmitResult preflightMultipassGraph(const CoinRenderRttPlan &,
    const CoinRenderFramePlan &, const SbVec2i32 &) const;
  virtual CoinRenderSubmitResult preflightMultipass(const CoinRenderFramePlan &,
                                                    const SbVec2i32 &) const;
};

#endif // !COIN_RENDER_BACKEND_H
