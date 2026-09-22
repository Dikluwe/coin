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

struct FrameExecutionResult {
  BackendStatus status;
  std::string diagnostic;

  FrameExecutionResult()
    : status(BackendStatus::SUCCESS), diagnostic("")
  {
  }

  FrameExecutionResult(BackendStatus s, const std::string & d)
    : status(s), diagnostic(d)
  {
  }
};

class SoWgpuBackend {
public:
  virtual ~SoWgpuBackend() {}
  virtual BackendStatus prepare(SoWgpuRenderTargetP & target) = 0;
  virtual BackendStatus submit(const FramePlan & frame,
                               SoWgpuRenderTargetP & target) = 0;
  virtual void poll() = 0;
  virtual const std::string & getLastError() const = 0;
};

#endif // !COIN_SOWGPUBACKEND_H
