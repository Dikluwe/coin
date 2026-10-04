#ifndef COIN_BGFX_BACKEND_H
#define COIN_BGFX_BACKEND_H

#include "rendering/coinrender/CoinRenderBackend.h"
#include "rendering/coinbgfx/CoinBgfxLowering.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"

#include <Inventor/rendering/CoinRenderTarget.h>
#include <bgfx/bgfx.h>
#include <memory>
#include <thread>

struct CoinRenderShadowPlan;

/** Experimental BGFX renderer. Targets share an API-thread-owned device. */
class CoinBgfxBackend : public CoinRenderBackend {
public:
  CoinBgfxBackend();
  ~CoinBgfxBackend() override;
  bool isGpuBackend() const override { return true; }
  bool supportsOffscreenShadows() const override { return true; }
  uint64_t getAvailableTransparencyMechanisms(bool window = false) const {
    if (!(window ? this->windowSupported : this->offscreenSupported))
      return 0;
    return COIN_RENDER_MECHANISM_OBJECT |
           (this->sortedLayersSupported ? uint64_t(COIN_RENDER_MECHANISM_PEELING) : 0) |
           (this->weightedOitSupported ? uint64_t(COIN_RENDER_MECHANISM_WEIGHTED_OIT) : 0);
  }
  CoinRenderBackendStatus getStatus() const override { return status; }
  CoinRenderBackendStatus prepare(CoinRenderTargetP & target) override;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target) override;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
                      const CoinRenderFrameReuseDecision & reuse) override;
  CoinRenderSubmitResult submitDirectTexture(const CoinRenderFramePlan & frame,
                                   const SbVec2i32 & size,
                                   uint64_t producerKey,
                                   uint64_t & token);
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan&, const CoinRenderFramePlan&,
                                      const SbVec2i32&) const override;
  bool requiresSharedRetirement() const override { return true; }
  void retireLostReadbacks() override;
  bool readbackLoad(uint64_t&, uint64_t&) const override;
  CoinRenderDeviceDomain resourceDomain() const override;
  CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan& frame, const SbVec2i32& size,
                                   uint64_t producer, CoinRenderTargetP&,
                                   uint64_t& token) override {
    return submitDirectTexture(frame, size, producer, token);
  }
  void finishRtt(const std::vector<uint64_t>& tokens) override { finishDirectTextures(tokens); }
  bool stagedRttRequiresPreparedParent() const override { return true; }
  void releaseDirectTexture(uint64_t token);
  void finishDirectTextures(const std::vector<uint64_t> & usedTokens);
  struct AsyncEntry;
  CoinRenderSubmitResult submitAsync(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
                           CoinRenderReadbackTicket & ticket, const CoinRenderFrameReuseDecision & reuse) override;
  static CoinRenderTarget::ReadbackStatus pollReadback(const CoinRenderReadbackTicket & ticket,
    std::vector<uint8_t> & color, std::vector<float> & depth, SbString * diagnostic);
  static bool cancelReadback(const CoinRenderReadbackTicket & ticket);
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

  CoinRenderSubmitResult submitInternal(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
                              const CoinRenderFrameReuseDecision & reuse, CoinRenderReadbackTicket * ticket);
  bool resize(int width, int height);
  bool prepareFullscreenResources();
  bool prepareDepthReadbackResources();
  bool prepareShadowPrograms(size_t mapCount);
  bool prepareTransparencyPrograms(CoinBgfxTransparencyStrategy strategy);
  bool prepareShadowTransparencyPrograms();
  CoinRenderBackendStatus checkRuntimeFailure(const char * operation);
  void destroyResources();
  void shutdownRuntime();
  void destroyFrameBuffers();
  void encodeSortedLayers(const std::vector<CoinBgfxDraw> & draws,
                          bgfx::DynamicVertexBufferHandle vertices,
                          bgfx::DynamicIndexBufferHandle indices,
                          bgfx::FrameBufferHandle output,
                          const std::vector<bgfx::TextureHandle> & textures, bgfx::ViewId firstView, int width, int height,
                          const CoinRenderFramePlan & frame,
                          const CoinRenderShadowPlan & shadows,
                          const std::vector<bgfx::FrameBufferHandle> & shadowMaps,
                          const std::vector<bgfx::FrameBufferHandle> & layers,
                          bgfx::FrameBufferHandle oitBuffer);
  bool encodeOverlayLayers(const std::vector<CoinBgfxDraw> & draws,
                           bgfx::DynamicVertexBufferHandle vertices,
                           bgfx::DynamicIndexBufferHandle indices,
                           bgfx::FrameBufferHandle output,
                           const std::vector<bgfx::TextureHandle> & textures,
                           bgfx::ViewId & nextView, int width, int height,
                           const CoinRenderFramePlan & frame,
                           const CoinRenderShadowPlan & shadowPlan,
                           const std::vector<bgfx::FrameBufferHandle> & shadowMaps);
  void encodeWeightedOit(const std::vector<CoinBgfxDraw> & draws,
                         bgfx::DynamicVertexBufferHandle vertices,
                         bgfx::DynamicIndexBufferHandle indices,
                         bgfx::FrameBufferHandle output,
                         const std::vector<bgfx::TextureHandle> & textures, bgfx::ViewId firstView, int width, int height,
                          const CoinRenderFramePlan & frame,
                          const CoinRenderShadowPlan & shadows,
                          const std::vector<bgfx::FrameBufferHandle> & shadowMaps,
                          const std::vector<bgfx::FrameBufferHandle> & layers,
                          bgfx::FrameBufferHandle oitBuffer);
  void bindDrawTexture(const CoinBgfxDraw & draw,
                       const std::vector<bgfx::TextureHandle> & textures);
  void bindDrawLighting(const CoinBgfxDraw & draw, int targetHeight = 0);
  void bindShadowReceiver(const CoinRenderFramePlan & frame,
                          const CoinRenderShadowPlan & shadowPlan,
                          const std::vector<bgfx::FrameBufferHandle> & shadowMaps,
                          const CoinBgfxDraw & draw, int targetHeight);
  bool onApiThread() const;
  CoinRenderBackendStatus status;

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
  bool drawBatchingEnabled;
  bool readbackDepthEnabled = false;
  uint32_t readbackPipelineDepth;
  uint32_t readbackCursor;
  uint64_t readbackSequence;
  CoinBgfxTransparencyMode transparencyMode;
  CoinBgfxTransparencyStrategy activeTransparencyStrategy;
  bool windowSupported = false;
  bool offscreenSupported = false;
  bool weightedOitSupported;
  bool sortedLayersSupported;
  uint32_t peelPassCount = 4;
  uint64_t serial;
  uint64_t directTextureSerial;
  int width;
  int height;
  bgfx::VertexLayout layout;
  bgfx::VertexLayout compactLayout;
  bgfx::ProgramHandle program;
  bgfx::ProgramHandle shadowMomentsProgram;
  bgfx::ProgramHandle shadowReceiverProgram;
  bgfx::ProgramHandle shadowReceiverProgram4;
  bgfx::ProgramHandle shadowReceiverProgram8;
  bgfx::ProgramHandle shadowPeelProgram;
  bgfx::ProgramHandle shadowOitProgram;
  bgfx::UniformHandle shadowModelViewUniform;
  bgfx::UniformHandle shadowClipModelViewUniform;
  bgfx::UniformHandle shadowDepthUniform;
  bgfx::UniformHandle shadowQualityUniform;
  bgfx::UniformHandle shadowLightIndicesUniform;
  bgfx::UniformHandle shadowLightIndicesExtraUniform;
  bgfx::UniformHandle shadowViewToClipUniform[8];
  bgfx::UniformHandle shadowViewToLightUniform[8];
  bgfx::UniformHandle shadowParamsUniform[8];
  bgfx::UniformHandle shadowMetaUniform[8];
  bgfx::UniformHandle shadowSampler[8];
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
  bgfx::UniformHandle clipMetaUniform;
  bgfx::UniformHandle clipPlanesUniform;
  bgfx::VertexBufferHandle fullscreenVertexBuffer;
  bgfx::IndexBufferHandle fullscreenIndexBuffer;
  bgfx::UniformHandle textureSampler;
  bgfx::UniformHandle extraTextureSamplers[COIN_RENDER_MAX_TEXTURE_UNITS - 1];
  bgfx::UniformHandle fogColorModeUniform;
  bgfx::UniformHandle fogRangeUniform;
  bgfx::UniformHandle textureParamsUniform;
  bgfx::UniformHandle textureBlendUniform;
  bgfx::UniformHandle textureCombineUniform;
  bgfx::UniformHandle ambientLightUniform;
  bgfx::UniformHandle lightCountUniform;
  bgfx::UniformHandle lightPositionTypeUniform;
  bgfx::UniformHandle lightDirectionCutoffUniform;
  bgfx::UniformHandle lightColorIntensityUniform;
  bgfx::UniformHandle lightAttenuationDropUniform;
  bgfx::FrameBufferHandle frameBuffer;
  bgfx::FrameBufferHandle oitFrameBuffer;
  bgfx::FrameBufferHandle peelFrameBuffers[COIN_RENDER_MAX_PEEL_LAYERS];
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
  CoinBgfxPlan cachedPlan;
  std::vector<bgfx::TextureHandle> cachedTextures;
  bgfx::DynamicVertexBufferHandle cachedVertexBuffer;
  bgfx::DynamicIndexBufferHandle cachedIndexBuffer;
  uint32_t cachedVertexCapacity;
  bool cachedCompactVertices = false;
  uint32_t cachedIndexCapacity;
};

#endif
