#ifndef COIN_SOWGPURUSTBACKEND_H
#define COIN_SOWGPURUSTBACKEND_H

#include "rendering/wgpu/SoWgpuBackend.h"
#include <string>

class SoWgpuRustBackend : public SoWgpuBackend {
public:
  SoWgpuRustBackend();
  virtual ~SoWgpuRustBackend();

  bool isGpuBackend() const override { return true; }
  BackendStatus getStatus() const override;
  virtual BackendStatus prepare(SoWgpuRenderTargetP & target) override;
  virtual SubmitResult submit(const FramePlan & frame,
                              SoWgpuRenderTargetP & target) override;
  virtual void poll() override;
  virtual const std::string & getLastError() const override;

  static bool isAvailable();
  static std::string getAdapterInfo();

private:
  BackendStatus status;
  std::string lastError;
};

#endif // !COIN_SOWGPURUSTBACKEND_H
