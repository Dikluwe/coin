#ifndef COIN_SOWGPUBACKEND_H
#define COIN_SOWGPUBACKEND_H

#include "rendering/wgpu/SoWgpuFramePlan.h"
#include <string>

class SoWgpuRenderTargetP;

enum class BackendStatus {
  SUCCESS = 0,
  NOT_READY,
  UNSUPPORTED,
  OUT_OF_MEMORY,
  DEVICE_LOST,
  BACKEND_ERROR,
  SURFACE_LOST
};

struct SubmitResult {
  BackendStatus status{BackendStatus::SUCCESS};
  std::string diagnostic;
  uint64_t submissionSerial{0};

  SubmitResult() : status(BackendStatus::SUCCESS), diagnostic(""), submissionSerial(0) {}
  SubmitResult(BackendStatus s, const std::string & d = "", uint64_t serial = 0)
    : status(s), diagnostic(d), submissionSerial(serial) {}
};

typedef SubmitResult FrameExecutionResult;

class SoWgpuBackend {
public:
  virtual ~SoWgpuBackend() {}
  virtual bool isGpuBackend() const = 0;
  virtual BackendStatus getStatus() const = 0;
  virtual BackendStatus prepare(SoWgpuRenderTargetP & target) = 0;
  virtual SubmitResult submit(const FramePlan & frame,
                             SoWgpuRenderTargetP & target) = 0;
  virtual void poll() = 0;
  virtual const std::string & getLastError() const = 0;
};

#endif // !COIN_SOWGPUBACKEND_H
