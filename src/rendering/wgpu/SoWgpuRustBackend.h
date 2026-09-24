#include <Inventor/CoinWgpuExport.h>
#ifndef COIN_SOWGPURUSTBACKEND_H
#define COIN_SOWGPURUSTBACKEND_H

#include "rendering/wgpu/SoWgpuBackend.h"
#include "rendering/wgpu/SoWgpuFrameReuseCore.h"
#include <memory>
#include <string>

struct SoWgpuReadbackTicket;
class SoWgpuFfiFrame;

/**
 * @brief Private connector from Coin's experimental FramePlan to wgpu-native.
 *
 * Camera-only frames may reuse Rust-owned geometry after the bridge validates
 * the base revision and device generation. Setting
 * @c COIN_WGPU_CAMERA_BINDINGS=1 additionally tests persistent material and
 * draw bindings in that path. This experiment is off by default because the
 * current Release A/B improves 512x512 camera-frame medians but has not
 * established a general advantage over GL or a 1024x1024 p95 gain.
 * Window, RTT and textured frames do not use those persistent bindings.
 */
class COIN_WGPU_DLL_API SoWgpuRustBackend : public SoWgpuBackend {
public:
  SoWgpuRustBackend();
  virtual ~SoWgpuRustBackend();

  bool isGpuBackend() const override { return true; }
  BackendStatus getStatus() const override;
  virtual BackendStatus prepare(SoWgpuRenderTargetP & target) override;
  virtual SubmitResult submit(const FramePlan & frame,
                              SoWgpuRenderTargetP & target) override;
  SubmitResult submit(const FramePlan & frame, SoWgpuRenderTargetP & target,
                      const SoWgpuFrameReuseDecision & reuse);
  SubmitResult submitAsync(const FramePlan & frame, SoWgpuRenderTargetP & target,
                           SoWgpuReadbackTicket & outTicket);
  SubmitResult submitAsync(const FramePlan & frame, SoWgpuRenderTargetP & target,
                           SoWgpuReadbackTicket & outTicket,
                           const SoWgpuFrameReuseDecision & reuse);
  virtual void poll() override;
  virtual const std::string & getLastError() const override;

  static bool isAvailable();
  static std::string getAdapterInfo();

private:
  SubmitResult submitInternal(const FramePlan & frame, SoWgpuRenderTargetP & target,
                              SoWgpuReadbackTicket * outTicket,
                              const SoWgpuFrameReuseDecision & reuse);
  BackendStatus status;
  std::string lastError;
  std::unique_ptr<SoWgpuFfiFrame> ffiFrame;
};

#endif // !COIN_SOWGPURUSTBACKEND_H
