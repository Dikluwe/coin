#ifndef COIN_RENDER_BACKEND_H
#define COIN_RENDER_BACKEND_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <string>

class CoinRenderTargetP;

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
  virtual void poll() = 0;
  virtual const std::string & getLastError() const = 0;
};

#endif // !COIN_RENDER_BACKEND_H
