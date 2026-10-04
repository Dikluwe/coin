#include <Inventor/CoinRenderExport.h>
#ifndef COIN_WGPU_BACKEND_H
#define COIN_WGPU_BACKEND_H

#include "rendering/coinrender/CoinRenderBackend.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include <memory>
#include <string>

struct CoinRenderReadbackTicket;
class CoinWgpuFfiFrame;

/**
 * @brief Private connector from Coin's experimental CoinRenderFramePlan to wgpu-native.
 *
 * Camera-only frames may reuse Rust-owned geometry after the bridge validates
 * the base revision and device generation. Setting
 * @c COIN_WGPU_CAMERA_BINDINGS=1 additionally tests persistent material and
 * draw bindings in that path. This experiment is off by default because the
 * current Release A/B improves 512x512 camera-frame medians but has not
 * established a general advantage over GL or a 1024x1024 p95 gain.
 * Window, RTT and textured frames do not use those persistent bindings.
 */
class COIN_RENDER_DLL_API CoinWgpuBackend : public CoinRenderBackend {
public:
  static CoinRenderBackendRuntime & runtime();
  CoinWgpuBackend();
  virtual ~CoinWgpuBackend();

  bool isGpuBackend() const override { return true; }
  bool supportsOffscreenShadows() const override { return true; }
  CoinRenderBackendStatus getStatus() const override;
  virtual CoinRenderBackendStatus prepare(CoinRenderTargetP & target) override;
  virtual CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame,
                              CoinRenderTargetP & target) override;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
                      const CoinRenderFrameReuseDecision & reuse) override;
  CoinRenderSubmitResult submitAsync(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
                           CoinRenderReadbackTicket & outTicket);
  CoinRenderSubmitResult submitAsync(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
                           CoinRenderReadbackTicket & outTicket,
                           const CoinRenderFrameReuseDecision & reuse) override;
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan&, const CoinRenderFramePlan&,
                                      const SbVec2i32&) const override;
  bool readbackLoad(uint64_t&, uint64_t&) const override;
  CoinRenderDeviceDomain resourceDomain() const override;
  CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan&, const SbVec2i32&, uint64_t,
                                   CoinRenderTargetP&, uint64_t&) override;
  void finishRtt(const std::vector<uint64_t>&) override;
  virtual void poll() override;
  virtual const std::string & getLastError() const override;

  static bool isAvailable();
  static std::string getAdapterInfo();

private:
  CoinRenderSubmitResult submitInternal(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
                              CoinRenderReadbackTicket * outTicket,
                              const CoinRenderFrameReuseDecision & reuse);
  CoinRenderBackendStatus status;
  std::string lastError;
  std::unique_ptr<CoinWgpuFfiFrame> ffiFrame;
};

#endif // !COIN_WGPU_BACKEND_H
