#ifndef COIN_SOWGPUBGFXBACKEND_H
#define COIN_SOWGPUBGFXBACKEND_H

#include "rendering/wgpu/SoWgpuBackend.h"
#include "rendering/wgpu/SoWgpuBgfxCore.h"
#include "rendering/wgpu/SoWgpuFrameReuseCore.h"

#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <bgfx/bgfx.h>
#include <memory>
#include <thread>

/** Experimental BGFX renderer. Targets share an API-thread-owned device. */
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
  SubmitResult submitDirectTexture(const FramePlan & frame,
                                   const SbVec2i32 & size,
                                   uint64_t producerKey,
                                   uint64_t & token);
  void releaseDirectTexture(uint64_t token);
  void finishDirectTextures(const std::vector<uint64_t> & usedTokens);
  struct AsyncEntry;
  SubmitResult submitAsync(const FramePlan & frame, SoWgpuRenderTargetP & target,
                           SoWgpuReadbackTicket & ticket, const SoWgpuFrameReuseDecision & reuse);
  static SoWgpuRenderTarget::ReadbackStatus pollReadback(const SoWgpuReadbackTicket & ticket,
    std::vector<uint8_t> & color, std::vector<float> & depth, SbString * diagnostic);
  static bool cancelReadback(const SoWgpuReadbackTicket & ticket);
  void poll() override;
  const std::string & getLastError() const override { return lastError; }

private:
  struct ReadbackSlot {
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depthTexture = BGFX_INVALID_HANDLE;
    std::vector<float> depth;
    std::vector<uint8_t> pixels;
    uint32_t readyFrame = 0;
    uint64_t sequence = 0;
    bool pending = false;
  };
  struct DirectTextureResource {
    uint64_t token = 0;
    uint64_t producerKey = 0;
    int width = 0;
    int height = 0;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    bool inUse = false;
  };

  SubmitResult submitInternal(const FramePlan & frame, SoWgpuRenderTargetP & target,
                              const SoWgpuFrameReuseDecision & reuse, SoWgpuReadbackTicket * ticket);
  bool resize(int width, int height);
  BackendStatus checkRuntimeFailure(const char * operation);
  void destroyResources();
  void shutdownRuntime();
  void destroyFrameBuffers();
  void encodeSortedLayers(const std::vector<SoWgpuBgfxDraw> & draws,
                          bgfx::DynamicVertexBufferHandle vertices,
                          bgfx::DynamicIndexBufferHandle indices,
                          bgfx::FrameBufferHandle output,
                          const std::vector<bgfx::TextureHandle> & textures);
  bool encodeOverlayLayers(const std::vector<SoWgpuBgfxDraw> & draws,
                           bgfx::DynamicVertexBufferHandle vertices,
                           bgfx::DynamicIndexBufferHandle indices,
                           bgfx::FrameBufferHandle output,
                           const std::vector<bgfx::TextureHandle> & textures,
                           bgfx::ViewId & nextView);
  void encodeWeightedOit(const std::vector<SoWgpuBgfxDraw> & draws,
                         bgfx::DynamicVertexBufferHandle vertices,
                         bgfx::DynamicIndexBufferHandle indices,
                         bgfx::FrameBufferHandle output,
                         const std::vector<bgfx::TextureHandle> & textures);
  void bindDrawTexture(const SoWgpuBgfxDraw & draw,
                       const std::vector<bgfx::TextureHandle> & textures);
  void bindDrawLighting(const SoWgpuBgfxDraw & draw, int targetHeight = 0);
  bool onApiThread() const;
  BackendStatus status;

  std::string lastError;
  std::thread::id apiThread;
  std::shared_ptr<bgfx::CallbackI> callback;
  bgfx::ViewId viewBase;
  void * nativeDisplay;
  void * nativeWindow;
  bool initialized;
  bool presentToWindow;
  bool cameraPatchEnabled;
  bool drawGroupingEnabled;
  bool readbackDepthEnabled = false;
  uint32_t readbackPipelineDepth;
  uint32_t readbackCursor;
  uint64_t readbackSequence;
  SoWgpuBgfxTransparencyMode transparencyMode;
  SoWgpuBgfxTransparencyStrategy activeTransparencyStrategy;
  bool weightedOitSupported;
  bool sortedLayersSupported;
  uint64_t serial;
  uint64_t directTextureSerial;
  int width;
  int height;
  bgfx::VertexLayout layout;
  bgfx::ProgramHandle program;
  bgfx::ProgramHandle depthReadProgram;
  bgfx::UniformHandle readDepthSampler;
  bgfx::FrameBufferHandle depthReadFrameBuffer;
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
  bgfx::UniformHandle coinDepthUniform;
  bgfx::UniformHandle screenDoorUniform;
  bgfx::VertexBufferHandle fullscreenVertexBuffer;
  bgfx::IndexBufferHandle fullscreenIndexBuffer;
  bgfx::UniformHandle textureSampler;
  bgfx::UniformHandle extraTextureSamplers[COIN_WGPU_MAX_TEXTURE_UNITS - 1];
  bgfx::UniformHandle fogColorModeUniform;
  bgfx::UniformHandle fogRangeUniform;
  bgfx::UniformHandle textureParamsUniform;
  bgfx::UniformHandle textureBlendUniform;
  bgfx::UniformHandle ambientLightUniform;
  bgfx::UniformHandle lightCountUniform;
  bgfx::UniformHandle lightPositionTypeUniform;
  bgfx::UniformHandle lightDirectionCutoffUniform;
  bgfx::UniformHandle lightColorIntensityUniform;
  bgfx::UniformHandle lightAttenuationDropUniform;
  bgfx::FrameBufferHandle frameBuffer;
  bgfx::FrameBufferHandle oitFrameBuffer;
  bgfx::FrameBufferHandle peelFrameBuffers[4];
  bgfx::TextureHandle readbackTexture;
  std::vector<ReadbackSlot> readbackSlots;
  std::vector<uint8_t> lastPublishedReadback;
  std::vector<float> lastPublishedDepth;
  uint64_t lastPublishedSequence;
  std::vector<DirectTextureResource> directTextures;
  uint64_t cachedRevision;
  int cachedWidth;
  int cachedHeight;
  bool cachedHomogeneousDepth;
  bgfx::TextureHandle defaultTexture;
  SoWgpuBgfxPlan cachedPlan;
  std::vector<bgfx::TextureHandle> cachedTextures;
  bgfx::DynamicVertexBufferHandle cachedVertexBuffer;
  bgfx::DynamicIndexBufferHandle cachedIndexBuffer;
  uint32_t cachedVertexCapacity;
  uint32_t cachedIndexCapacity;
};

#endif
