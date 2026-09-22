#ifndef SOWGPUNATIVEBACKEND_H
#define SOWGPUNATIVEBACKEND_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuBackend.h"
#include <string>

#if defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE)
#include <webgpu/webgpu.h>
#endif

class SoWgpuNativeBackend : public SoWgpuBackend {
public:
  SoWgpuNativeBackend();
  virtual ~SoWgpuNativeBackend();

  BackendStatus prepare(SoWgpuRenderTargetP & target) override;
  BackendStatus submit(const FramePlan & frame,
                       SoWgpuRenderTargetP & target) override;
  void poll() override;
  const std::string & getLastError() const override { return this->lastError; }

private:
  std::string lastError;

#if defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE)
  WGPUInstance instance;
  WGPUAdapter adapter;
  WGPUDevice device;
  WGPUQueue queue;
  bool isReady;
#endif
};

#endif // !SOWGPUNATIVEBACKEND_H
