#ifndef COIN_SOWGPURUSTBACKEND_H
#define COIN_SOWGPURUSTBACKEND_H

#include "rendering/wgpu/SoWgpuBackend.h"
#include <string>

class SoWgpuRustBackend : public SoWgpuBackend {
public:
  SoWgpuRustBackend();
  virtual ~SoWgpuRustBackend();

  virtual BackendStatus prepare(SoWgpuRenderTargetP & target) override;
  virtual BackendStatus submit(const FramePlan & frame,
                               SoWgpuRenderTargetP & target) override;
  virtual void poll() override;
  virtual const std::string & getLastError() const override;

  static bool isAvailable();
  static std::string getAdapterInfo();

private:
  std::string lastError;
};

#endif // !COIN_SOWGPURUSTBACKEND_H
