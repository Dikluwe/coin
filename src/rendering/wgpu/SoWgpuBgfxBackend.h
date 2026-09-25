#ifndef COIN_SOWGPUBGFXBACKEND_H
#define COIN_SOWGPUBGFXBACKEND_H

#include "rendering/wgpu/SoWgpuBackend.h"
#include "rendering/wgpu/SoWgpuBgfxCore.h"
#include "rendering/wgpu/SoWgpuFrameReuseCore.h"

#include <bgfx/bgfx.h>
#include <thread>

/** Experimental BGFX/Vulkan Infra. One process-wide BGFX instance is used. */
class SoWgpuBgfxBackend : public SoWgpuBackend {
public:
  SoWgpuBgfxBackend();
  ~SoWgpuBgfxBackend() override;
  bool isGpuBackend() const override { return true; }
  BackendStatus getStatus() const override { return status; }
  BackendStatus prepare(SoWgpuRenderTargetP & target) override;
  SubmitResult submit(const FramePlan & frame, SoWgpuRenderTargetP & target) override;
  SubmitResult submit(const FramePlan & frame, SoWgpuRenderTargetP & target,
                      const SoWgpuFrameReuseDecision & reuse);
  void poll() override;
  const std::string & getLastError() const override { return lastError; }

private:
  bool resize(int width, int height);
  void destroyFrameBuffers();
  bool onApiThread() const;

  BackendStatus status;
  std::string lastError;
  std::thread::id apiThread;
  bool initialized;
  bool cameraPatchEnabled;
  uint64_t serial;
  int width;
  int height;
  bgfx::VertexLayout layout;
  bgfx::ProgramHandle program;
  bgfx::FrameBufferHandle frameBuffer;
  bgfx::TextureHandle readbackTexture;
  uint64_t cachedRevision;
  int cachedWidth;
  int cachedHeight;
  bool cachedHomogeneousDepth;
  SoWgpuBgfxPlan cachedPlan;
  bgfx::VertexBufferHandle cachedVertexBuffer;
  bgfx::IndexBufferHandle cachedIndexBuffer;
};

#endif
