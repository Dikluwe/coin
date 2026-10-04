#include "rendering/coinrender/CoinRenderBackendRuntime.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <iostream>

#define CHECK(c) do { if (!(c)) { std::cerr << "Failed: " << #c << " at " << __LINE__ << '\n'; return 1; } } while (0)

struct Counts { int destroyedSurface = 0, destroyedExecutor = 0; };
class IndependentRuntime : public CoinRenderBackendRuntime {
public:
  explicit IndependentRuntime(Counts & c) : counts(c) {}
  Counts & counts;
  bool surfaceHadExecutor = false;
  bool supportsWindowTargets() const override { return true; }
  std::string surfaceTypeDiagnostic(uint32_t type) const override {
    return type == COIN_RENDER_SURFACE_XLIB ? "" : "Independent runtime rejected surface type";
  }
  void destroySurface(CoinRenderTargetP & target) override {
    if (target.surfaceId) {
      surfaceHadExecutor = target.backend.get() != nullptr;
      ++counts.destroyedSurface; target.surfaceId = 0;
    }
  }
  bool cacheTelemetry(CoinRenderCacheTelemetry & t) const override {
    t.completedSerial = 42; return true;
  }
};
class IndependentExecutor : public CoinRenderBackend {
public:
  explicit IndependentExecutor(Counts & c) : counts(c) {}
  ~IndependentExecutor() override { ++counts.destroyedExecutor; }
  Counts & counts;
  bool cpuDepth = true, detach = false;
  std::string error;
  bool isGpuBackend() const override { return false; }
  bool initializesCpuDepthBuffer() const override { return cpuDepth; }
  bool resetOnActionDetach() const override { return detach; }
  CoinRenderBackendStatus getStatus() const override { return CoinRenderBackendStatus::SUCCESS; }
  CoinRenderBackendStatus prepare(CoinRenderTargetP &) override { return CoinRenderBackendStatus::SUCCESS; }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &, CoinRenderTargetP &) override { return {}; }
  void poll() override {}
  const std::string & getLastError() const override { return error; }
};

int main() {
  Counts counts;
  IndependentRuntime runtime(counts);
  CoinRenderNativeSurfaceDescriptor desc{};
  desc.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  desc.structSize = sizeof(desc);
  desc.type = COIN_RENDER_SURFACE_XLIB;
  desc.native.xlib.display = reinterpret_cast<void *>(0x1234);
  desc.native.xlib.window = 7;
  {
    CoinRenderTargetP target;
    target.runtime = &runtime;
    desc.abiVersion = 999;
    CHECK(!target.initWindow(desc, SbVec2i32(16,16)));
    CHECK(target.lastError.find("ABI version") != std::string::npos);
    desc.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
    desc.native.xlib.display = nullptr;
    CHECK(!target.initWindow(desc, SbVec2i32(16,16)));
    CHECK(target.lastError.find("Null display") != std::string::npos);
    desc.native.xlib.display = reinterpret_cast<void *>(0x1234);
    desc.type = COIN_RENDER_SURFACE_WAYLAND;
    CHECK(!target.initWindow(desc, SbVec2i32(16,16)));
    CHECK(target.lastError == "Independent runtime rejected surface type");
    desc.type = COIN_RENDER_SURFACE_XLIB;
    CHECK(!target.initWindow(desc, SbVec2i32(-1,16)));
    CHECK(target.initWindow(desc, SbVec2i32(0,16)));
    CHECK(target.suspended && target.status == CoinRenderTarget::TARGET_NOT_READY);
    CHECK(target.resize(SbVec2i32(16,16)));
    CHECK(!target.suspended && target.needsReconfigure);
    target.backend.reset(new IndependentExecutor(counts));
    target.surfaceId = 91;
  }
  CHECK(counts.destroyedSurface == 1 && runtime.surfaceHadExecutor);
  CHECK(counts.destroyedExecutor == 1);
  CHECK(desc.native.xlib.window == 7 && desc.native.xlib.display == reinterpret_cast<void *>(0x1234));

  {
    CoinRenderTargetP target;
    auto * executor = new IndependentExecutor(counts);
    target.backend.reset(executor);
    CHECK(target.resize(SbVec2i32(16,16)) && target.depthBuffer.size() == 256);
    executor->cpuDepth = false;
    CHECK(target.resize(SbVec2i32(16,16)) && target.depthBuffer.empty());
    const uint64_t generation = target.resourceGeneration;
    target.detachedFromAction();
    CHECK(target.backend.get() == executor && target.resourceGeneration == generation);
    executor->detach = true;
    target.detachedFromAction();
    CHECK(!target.backend && target.resourceGeneration == generation + 1);
    CHECK(counts.destroyedExecutor == 2);
  }

  // Public per-target facade must use its runtime even without a prepared executor.
  CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(16,16));
  target->getPimpl()->runtime = &runtime;
  CoinRenderCacheTelemetry telemetry;
  CHECK(target->getCacheTelemetry(telemetry) && telemetry.completedSerial == 42);
  delete target;
  CHECK(counts.destroyedSurface == 1);
  std::cout << "Independent runtime surface lifetime, descriptor validation, telemetry, depth and detach passed\n";
  return 0;
}
