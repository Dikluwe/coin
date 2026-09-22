#ifndef COIN_SOWGPUCPUREFERENCEBACKEND_H
#define COIN_SOWGPUCPUREFERENCEBACKEND_H

#include "rendering/wgpu/SoWgpuBackend.h"
#include <string>

class SoWgpuCpuReferenceBackend : public SoWgpuBackend {
public:
  SoWgpuCpuReferenceBackend();
  virtual ~SoWgpuCpuReferenceBackend();

  bool isGpuBackend() const override { return false; }
  BackendStatus getStatus() const override;
  BackendStatus prepare(SoWgpuRenderTargetP & target) override;
  SubmitResult submit(const FramePlan & frame, SoWgpuRenderTargetP & target) override;
  void poll() override {}
  const std::string & getLastError() const override { return this->lastError; }

private:
  BackendStatus status;
  std::string lastError;
};

#endif // !COIN_SOWGPUCPUREFERENCEBACKEND_H
