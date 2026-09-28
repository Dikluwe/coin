#ifndef SOWGPUNATIVEBACKEND_H
#define SOWGPUNATIVEBACKEND_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinrender/CoinRenderBackend.h"
#include <string>

#if defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE)
#include <webgpu/webgpu.h>
#endif

class SoWgpuNativeBackend : public CoinRenderBackend {
public:
  SoWgpuNativeBackend();
  virtual ~SoWgpuNativeBackend();

  bool isGpuBackend() const override { return true; }
  CoinRenderBackendStatus getStatus() const override;
  CoinRenderBackendStatus prepare(CoinRenderTargetP & target) override;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame,
                      CoinRenderTargetP & target) override;
  void poll() override;
  const std::string & getLastError() const override { return this->lastError; }

  static bool isAvailable();

private:
  CoinRenderBackendStatus status;
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
