#include <Inventor/CoinWgpuExport.h>
#ifndef COIN_SOWGPURUSTBACKEND_H
#define COIN_SOWGPURUSTBACKEND_H

#include "rendering/wgpu/SoWgpuBackend.h"
#include <memory>
#include <string>

struct SoWgpuReadbackTicket;
class SoWgpuFfiFrame;

class COIN_WGPU_DLL_API SoWgpuRustBackend : public SoWgpuBackend {
public:
  SoWgpuRustBackend();
  virtual ~SoWgpuRustBackend();

  bool isGpuBackend() const override { return true; }
  BackendStatus getStatus() const override;
  virtual BackendStatus prepare(SoWgpuRenderTargetP & target) override;
  virtual SubmitResult submit(const FramePlan & frame,
                              SoWgpuRenderTargetP & target) override;
  SubmitResult submitAsync(const FramePlan & frame, SoWgpuRenderTargetP & target,
                           SoWgpuReadbackTicket & outTicket);
  virtual void poll() override;
  virtual const std::string & getLastError() const override;

  static bool isAvailable();
  static std::string getAdapterInfo();

private:
  SubmitResult submitInternal(const FramePlan & frame, SoWgpuRenderTargetP & target,
                              SoWgpuReadbackTicket * outTicket);
  BackendStatus status;
  std::string lastError;
  std::unique_ptr<SoWgpuFfiFrame> ffiFrame;
};

#endif // !COIN_SOWGPURUSTBACKEND_H
