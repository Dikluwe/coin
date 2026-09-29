#ifndef COIN_RENDER_BACKEND_H
#define COIN_RENDER_BACKEND_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <string>

class CoinRenderTargetP;
struct CoinRenderRttPlan;

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
  virtual CoinRenderBackendStatus getStatus() const = 0;
  virtual CoinRenderBackendStatus prepare(CoinRenderTargetP & target) = 0;
  virtual CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame,
                             CoinRenderTargetP & target) = 0;
  // Inspect immutable captures without initializing a device or submitting work.
  virtual CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan&, const CoinRenderFramePlan&,
                                              const SbVec2i32&) const {
    return {};
  }
  virtual CoinRenderDeviceDomain resourceDomain() const { return {}; }
  virtual CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan&, const SbVec2i32&, uint64_t,
                                           CoinRenderTargetP&, uint64_t&) {
    return {CoinRenderBackendStatus::UNSUPPORTED,
            "Direct scene texture is not implemented; no fallback was applied"};
  }
  virtual void finishRtt(const std::vector<uint64_t>&) {}
  virtual bool stagedRttRequiresRelease() const { return false; }
  virtual void poll() = 0;
  virtual const std::string & getLastError() const = 0;
};

#endif // !COIN_RENDER_BACKEND_H
