#ifndef COIN_SOWGPUBGFXBACKEND_H
#define COIN_SOWGPUBGFXBACKEND_H

#include "rendering/wgpu/SoWgpuBackend.h"
#include "rendering/wgpu/SoWgpuBgfxCore.h"
#include "rendering/wgpu/SoWgpuFrameReuseCore.h"

#include <bgfx/bgfx.h>
#include <thread>

/** Experimental BGFX/Vulkan or OpenGL Infra. One process-wide instance is used. */
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
  enum class TransparencyMode {
    OBJECT,
    SORTED_LAYERS,
    WEIGHTED_OIT
  };

  bool resize(int width, int height);
  void destroyFrameBuffers();
  void encodeSortedLayers(const std::vector<SoWgpuBgfxDraw> & draws,
                          bgfx::VertexBufferHandle vertices,
                          bgfx::IndexBufferHandle indices,
                          bgfx::FrameBufferHandle output);
  void encodeWeightedOit(const std::vector<SoWgpuBgfxDraw> & draws,
                         bgfx::VertexBufferHandle vertices,
                         bgfx::IndexBufferHandle indices,
                         bgfx::FrameBufferHandle output);
  bool onApiThread() const;
  BackendStatus status;

  std::string lastError;
  std::thread::id apiThread;
  bool initialized;
  bool presentToWindow;
  bool cameraPatchEnabled;
  TransparencyMode transparencyMode;
  uint64_t serial;
  int width;
  int height;
  bgfx::VertexLayout layout;
  bgfx::ProgramHandle program;
  bgfx::ProgramHandle peelNextProgram;
  bgfx::ProgramHandle compositeProgram;
  bgfx::ProgramHandle weightedOitProgram;
  bgfx::ProgramHandle weightedCompositeProgram;
  bgfx::UniformHandle previousDepthSampler;
  bgfx::UniformHandle previousColorSampler;
  bgfx::UniformHandle layerSampler;
  bgfx::UniformHandle oitAccumSampler;
  bgfx::UniformHandle oitRevealSampler;
  bgfx::UniformHandle depthInfoUniform;
  bgfx::VertexBufferHandle fullscreenVertexBuffer;
  bgfx::IndexBufferHandle fullscreenIndexBuffer;
  bgfx::FrameBufferHandle frameBuffer;
  bgfx::FrameBufferHandle oitFrameBuffer;
  bgfx::FrameBufferHandle peelFrameBuffers[4];
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
