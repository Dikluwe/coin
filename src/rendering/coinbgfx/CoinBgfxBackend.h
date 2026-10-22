#ifndef COIN_BGFX_BACKEND_H
#define COIN_BGFX_BACKEND_H

#include "rendering/coinrender/CoinRenderBackend.h"
#include "rendering/coinbgfx/CoinBgfxLowering.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"

#include <Inventor/rendering/CoinRenderTarget.h>
#include <bgfx/bgfx.h>
#include <memory>
#include <map>
#include <utility>
#include <thread>

struct CoinRenderShadowPlan;

/** Experimental BGFX renderer. Targets share an API-thread-owned device. */
class CoinBgfxBackend : public CoinRenderBackend {
public:
  static CoinRenderBackendRuntime & runtime();
  CoinBgfxBackend();
  ~CoinBgfxBackend() override;
  bool initializesCpuDepthBuffer() const override { return false; }
  bool resetOnActionDetach() const override { return true; }
  bool isGpuBackend() const override { return true; }
  void configureMultipassTarget(const CoinRenderTargetP &) override;
  bool multipassOutputBytes(const CoinRenderTargetP &, bool, uint64_t &) const override;
  CoinRenderSubmitResult preflightMultipassGraph(const CoinRenderRttPlan &,
    const CoinRenderFramePlan &, const SbVec2i32 &) const override;
  CoinRenderSubmitResult preflightMultipass(const CoinRenderFramePlan &, const SbVec2i32 &) const override;
  bool supportsOffscreenShadows() const override { return true; }
  bool supportsOpaqueBump() const override { return true; }
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
  CoinRenderSubmitResult assembleCubeRtt(const std::array<uint64_t,6>&, uint32_t, bool,
                                         CoinRenderTargetP&, uint64_t&) override;
  void finishRtt(const std::vector<uint64_t>& tokens) override { finishDirectTextures(tokens); }
  void retireRtt(const std::vector<uint64_t>& tokens) override;
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
  bool portableSampling = false; // Immutable policy of the owning target.
  struct ReadbackSlot {
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depthTexture = BGFX_INVALID_HANDLE;
    std::vector<float> depth;
    std::vector<uint8_t> pixels;
    uint32_t readyFrame = 0;
    uint64_t sequence = 0;
    bool depthRequested = false;
    bool pending = false;
  };
  struct DirectTextureResource {
    uint64_t token = 0;
    uint64_t producerKey = 0;
    int width = 0;
    int height = 0;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    CoinRenderTextureFormat format=CoinRenderTextureFormat::RGBA8_LINEAR;
    bool mipmapped=false;
    uint32_t cubeFaceSize=0;
    bool cubeMipmapped=false;
    bool depthSampleable=false;
    bool inUse = false;
  };

  CoinRenderSubmitResult submitInternal(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
                              const CoinRenderFrameReuseDecision & reuse, CoinRenderReadbackTicket * ticket);
  bool resize(int width, int height);
  bool prepareBaseProgram(const std::vector<CoinBgfxDraw> & draws,
                          bool hasShadows,
                          CoinBgfxTransparencyStrategy strategy,
                          bool instanced = false, bool windowTarget = false,
                          bgfx::TextureFormat::Enum colorFormat = bgfx::TextureFormat::RGBA8);
  bool preparePortablePrograms(const CoinRenderFramePlan &, const std::vector<CoinBgfxDraw> &,
                               bool hasShadows, CoinBgfxTransparencyStrategy,
                               bool windowTarget, bgfx::TextureFormat::Enum colorFormat, bool pinPreflight = false);
  void releasePortablePreflightPins();
  struct PortablePreflightRelease {
    CoinBgfxBackend *owner;
    ~PortablePreflightRelease() { if (owner) owner->releasePortablePreflightPins(); }
  };
  std::vector<std::string> portablePreflightPins;
  enum class PortableFamily : uint8_t {
    BASE, NATIVE_DEPTH, SAMPLE_DEPTH, SHADOW_BASE, SHADOW_SAMPLE,
    PEEL, SHADOW_PEEL, OIT, SHADOW_OIT
  };
  void submitPortableDraw(bgfx::ViewId, bgfx::ProgramHandle, const CoinBgfxDraw &,
                          const CoinRenderFramePlan &, PortableFamily family = PortableFamily::BASE,
                          uint32_t depth = 0, uint8_t discard = BGFX_DISCARD_ALL);
  // Routes belong to one target's current base executors. Compiled module
  // programs and uniform banks belong to the existing shared device runtime.
  std::map<std::pair<std::string, PortableFamily>, std::string> portableProgramRoutes;
  bool prepareFullscreenResources();
  bool prepareMultipassPrograms();
  bool prepareNpotMipProgram();
  bool prepareRasterImageResources();
  CoinRenderSubmitResult encodeRasterImages(const CoinRenderFramePlan & frame,
    const std::vector<bgfx::TextureHandle> & textures,
    std::vector<bgfx::TextureHandle> & owned);
  bool prepareDepthReadbackResources(bool staging = true);
  CoinRenderSubmitResult msaaProfile(const CoinRenderFramePlan & frame) const;
  CoinRenderSubmitResult prepareMsaa(int width, int height);
  void encodeMsaaResolve(bgfx::ViewId view, bgfx::FrameBufferHandle output,
                         int width, int height, float mode);
  uint32_t sampleCount = 1;
  bgfx::FrameBufferHandle msaaFrameBuffer = BGFX_INVALID_HANDLE;
  int msaaWidth = 0, msaaHeight = 0;
  bgfx::ProgramHandle msaaProgram = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle msaaColorSampler = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle msaaDepthSampler = BGFX_INVALID_HANDLE;

  bool prepareShadowResources();
  bool prepareShadowPrograms(size_t mapCount, bool receivers = true);
  bool prepareShadowArray(const CoinRenderShadowPlan & plan, bool enabled);
  void encodeShadowArray(const CoinRenderShadowPlan & plan,
    const std::vector<bgfx::FrameBufferHandle> & maps, bgfx::ViewId view);
  bool prepareShadowMultitextureResources();
  bool verifyShadowMultitextureProgram(bgfx::ProgramHandle program, bool &verified,
                                       unsigned composition, bool bump, uint64_t surfaceState = 0,
                                       bgfx::TextureFormat::Enum colorFormat = bgfx::TextureFormat::RGBA8,
                                       bgfx::TextureFormat::Enum depthFormat = bgfx::TextureFormat::Count,
                                       bool portable = false);
  bool prepareShadowMultitextureProgram(bool customSampleDepth = false, unsigned composition = 0);
  bool prepareTransparencyPrograms(CoinBgfxTransparencyStrategy strategy, bool surfaceExecutors = true);
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
                           const std::vector<bgfx::FrameBufferHandle> & shadowMaps,
                           bool beforeScene = false,
                           bgfx::TextureHandle sampleDepth = BGFX_INVALID_HANDLE);
  void encodeWeightedOit(const std::vector<CoinBgfxDraw> & draws,
                         bgfx::DynamicVertexBufferHandle vertices,
                         bgfx::DynamicIndexBufferHandle indices,
                         bgfx::FrameBufferHandle output,
                         const std::vector<bgfx::TextureHandle> & textures, bgfx::ViewId firstView, int width, int height,
                          const CoinRenderFramePlan & frame,
                          const CoinRenderShadowPlan & shadows,
                          const std::vector<bgfx::FrameBufferHandle> & shadowMaps,
                          const std::vector<bgfx::FrameBufferHandle> & layers,
                          bgfx::FrameBufferHandle oitBuffer,
                          const std::vector<CoinBgfxDraw> & depthContext);
  bgfx::ViewId encodeDeferredRuns(const std::vector<CoinBgfxDraw> & draws,
    bgfx::DynamicVertexBufferHandle vertices, bgfx::DynamicIndexBufferHandle indices,
    bgfx::FrameBufferHandle output, const std::vector<bgfx::TextureHandle> & textures,
    bgfx::ViewId next, int width, int height, const CoinRenderFramePlan & frame,
    const CoinRenderShadowPlan & shadows, const std::vector<bgfx::FrameBufferHandle> & maps,
    CoinBgfxTransparencyStrategy strategy, const std::vector<bgfx::FrameBufferHandle> & layers,
    bgfx::FrameBufferHandle oit, bgfx::TextureHandle sampleDepth);
  void bindDrawTexture(const CoinBgfxDraw & draw,
                       const std::vector<bgfx::TextureHandle> & textures,
                       bool shadowSurface = false, bool generalSurface = false, bool shadowMultitexture = false);
  bool prepareSurfaceDepthPrograms(const std::vector<CoinBgfxDraw> & draws, bool hasShadows, bool instanced, bool texture2d = false);
  void submitSurfaceDraw(bgfx::ViewId view, const CoinBgfxDraw & draw,
                         const std::vector<bgfx::TextureHandle> & textures,
                         const CoinRenderFramePlan & frame,
                         const CoinRenderShadowPlan & shadowPlan,
                         const std::vector<bgfx::FrameBufferHandle> & shadowMaps,
                         int width, int height, bgfx::TextureHandle sampleDepth);
  void bindDrawLighting(const CoinBgfxDraw & draw, int targetHeight = 0, int targetWidth = 0);
  void bindShadowReceiver(const CoinRenderFramePlan & frame,
                          const CoinRenderShadowPlan & shadowPlan,
                          const std::vector<bgfx::FrameBufferHandle> & shadowMaps,
                          const CoinBgfxDraw & draw, int targetHeight,
                          unsigned declaredShadowSlots = 0);
  bool onApiThread() const;
  CoinRenderBackendStatus status;

  std::string lastError;
  std::thread::id apiThread;
  std::shared_ptr<bgfx::CallbackI> callback;
  bgfx::ViewId viewBase;
  uint32_t viewCount = 16;
  bool reserveViews(uint32_t required);
  bool canReserveGraphViews(uint32_t rootViews, uint32_t producerViews, bool staged, std::string & diagnostic) const;
  void * nativeDisplay;
  void * nativeWindow;
  bool initialized;
  bool presentToWindow;
  bool cameraPatchEnabled;
  bool drawGroupingEnabled;
  bool drawBatchingEnabled;
  bool readbackDepthEnabled = false;
  bool preparedDepthReadbackEnabled = false;
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
  bgfx::VertexLayout instancedLayout;
  bgfx::VertexLayout instanceLayout;
  bool prepareBumpResources();
  bool prepareBumpPrograms();
  bool prepareBumpReceiverPrograms(const CoinBgfxDraw &draw, bool windowTarget,
                                   bgfx::TextureFormat::Enum colorFormat = bgfx::TextureFormat::RGBA8);
  bgfx::ProgramHandle bumpProgram=BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle bumpNativeDepthProgram=BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle bumpSampleDepthProgram=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle bumpImageSampler=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle bumpLightMetaUniform=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle bumpLightVectorsUniform=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle bumpLightColorsUniform=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle bumpEyeUniform=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle bumpModelViewUniform=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle bumpBaseSampler=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle bumpParamsUniform=BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle program;
  bgfx::ProgramHandle solidProgram;
  bgfx::ProgramHandle texture2dProgram;
  bgfx::ProgramHandle instancedProgram;
  bgfx::ProgramHandle activeProgram; // Alias selected for the current submission.
  bgfx::ProgramHandle shadowMomentsProgram;
  bgfx::ProgramHandle texture2dNativeDepthProgram = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle texture2dSampleDepthProgram = BGFX_INVALID_HANDLE;
  bool activeTexture2dDepth = false; // Selected per complete ordinary draw set.
  bgfx::ProgramHandle surfaceNativeDepthProgram = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle surfaceSampleDepthProgram = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle shadowNativeDepthProgram = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle shadowSampleDepthProgram = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle shadowArrayTexture = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle defaultShadowArrayTexture = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle shadowArraySampler = BGFX_INVALID_HANDLE;
  uint32_t shadowArraySize = 1;
  bool shadowArrayActive = false;
  bool shadowMultitextureSampleDepthActive = false;
  bgfx::ProgramHandle shadowMultitexturePeelProgram = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle shadowMultitextureOitProgram = BGFX_INVALID_HANDLE;
  bool shadowMultitexturePeelVerified = false;
  bool shadowMultitextureOitVerified = false;
  bgfx::ProgramHandle bumpReceiverProgram = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle bumpReceiverNativeProgram = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle bumpReceiverSampleProgram = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle shadowMultitextureProgram = BGFX_INVALID_HANDLE;
  // Verification belongs to the complete consumed pipeline, not only its shader.
  std::map<std::pair<uint64_t, uint32_t>, bool> bumpReceiverVerifiedPipelines;
  bool shadowMultitextureVerified = false;
  bgfx::ProgramHandle shadowMultitextureSampleDepthProgram = BGFX_INVALID_HANDLE;
  bool shadowMultitextureSampleDepthVerified = false;
  bgfx::ProgramHandle instancedNativeDepthProgram = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle instancedSampleDepthProgram = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle shadowReceiverProgram;
  bgfx::ProgramHandle shadowReceiverProgram4;
  bgfx::ProgramHandle shadowReceiverProgram8;
  bgfx::ProgramHandle shadowPeelProgram;
  bgfx::ProgramHandle shadowOitProgram;
  bgfx::UniformHandle shadowModelViewUniform;
  bgfx::UniformHandle shadowClipModelViewUniform;
  bgfx::UniformHandle shadowDepthUniform;
  bgfx::UniformHandle shadowQualityUniform;
  bgfx::UniformHandle shadowFlatDiffuseUniform;
  bgfx::UniformHandle shadowFlatSpecularUniform;
  bgfx::UniformHandle shadowFlatParamsUniform;
  bgfx::UniformHandle shadowLightIndicesUniform;
  bgfx::UniformHandle shadowLightIndicesExtraUniform;
  bgfx::UniformHandle shadowViewToClipUniform[8];
  bgfx::UniformHandle shadowViewToLightUniform[8];
  bgfx::UniformHandle shadowParamsUniform[8];
  bgfx::UniformHandle shadowMetaUniform[8];
  bgfx::UniformHandle shadowTexelUniform[8];
  bgfx::UniformHandle shadowSampler[8];
  bgfx::ProgramHandle depthReadProgram;
  bgfx::UniformHandle readDepthSampler;
  bgfx::FrameBufferHandle depthReadFrameBuffer;
  bgfx::ProgramHandle peelNextProgram;
  bgfx::ProgramHandle peelMsaaProgram = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle shadowPeelMsaaProgram = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle peelMsaaCompositeProgram = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle peelMsaaColorSampler = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle peelMsaaMaskSampler = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle peelMsaaDepthSampler = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle compositeProgram;
  bgfx::ProgramHandle compositeDepthProgram;
  bgfx::ProgramHandle multipassSumProgram = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle multipassMeanProgram = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle multipassSceneSampler = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle multipassPreviousSampler = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle multipassInfoUniform = BGFX_INVALID_HANDLE;
  bool multipassSceneRequired = false;
  bgfx::ProgramHandle npotMipProgram;
  bgfx::ProgramHandle rasterImageProgram = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle rasterPrimarySampler = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle rasterCoordinatesUniform = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle rasterExtentUniform = BGFX_INVALID_HANDLE;
  std::map<uint32_t, bgfx::TextureHandle> rasterImageOutputs;
  bgfx::ProgramHandle weightedOitProgram;
  bgfx::ProgramHandle weightedCompositeProgram;
  bgfx::ProgramHandle weightedMsaaCompositeProgram = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle weightedMsaaAccumSampler = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle weightedMsaaRevealSampler = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle previousDepthSampler;
  bgfx::UniformHandle opaqueDepthSampler;
  bgfx::UniformHandle layerSampler;
  bgfx::UniformHandle layerMaskSampler;
  bgfx::UniformHandle layerDepthSampler;
  bgfx::UniformHandle peelMetaUniform;
  bgfx::UniformHandle npotMipSampler;
  bgfx::UniformHandle npotMipExtentUniform;
  bgfx::UniformHandle oitAccumSampler;
  bgfx::UniformHandle oitRevealSampler;
  bgfx::UniformHandle depthInfoUniform;
  bgfx::UniformHandle coinDepthUniform;
  bgfx::UniformHandle screenDoorUniform;
  bgfx::UniformHandle alphaTestUniform;
  bgfx::UniformHandle rasterViewportUniform;
  bool canonicalCoverage = false;
  bgfx::UniformHandle clipMetaUniform;
  bgfx::UniformHandle clipPlanesUniform;
  bgfx::VertexBufferHandle fullscreenVertexBuffer;
  bgfx::IndexBufferHandle fullscreenIndexBuffer;
  // Scene samplers declared by the selected base stage; shadow stages use unit zero.
  uint32_t activeTextureUnitCount = COIN_RENDER_MAX_TEXTURE_UNITS;
  bgfx::UniformHandle textureSampler;
  bgfx::UniformHandle extraTextureSamplers[COIN_RENDER_MAX_TEXTURE_UNITS - 1];
  bgfx::UniformHandle fogColorModeUniform;
  bgfx::UniformHandle fogRangeUniform;
  bgfx::UniformHandle textureParamsUniform;
  std::map<uint16_t, std::pair<uint32_t, uint32_t>> samplingBoundTextureSizes;
  bgfx::UniformHandle samplingTextureSizesUniform;
  bgfx::UniformHandle textureBlendUniform;
  bgfx::UniformHandle textureCombineUniform;
  bgfx::UniformHandle ambientLightUniform;
  bgfx::UniformHandle lightCountUniform;
  bgfx::UniformHandle lightPositionTypeUniform;
  bgfx::UniformHandle lightDirectionCutoffUniform;
  bgfx::UniformHandle lightColorIntensityUniform;
  bgfx::UniformHandle instancedCameraUniform;
  bgfx::UniformHandle lightAttenuationDropUniform;
  bgfx::FrameBufferHandle frameBuffer;
  bgfx::FrameBufferHandle windowSceneFrameBuffer;
  bgfx::FrameBufferHandle oitFrameBuffer;
  bgfx::FrameBufferHandle peelFrameBuffers[COIN_RENDER_MAX_PEEL_LAYERS];
  bgfx::TextureHandle readbackTexture;
  std::vector<ReadbackSlot> readbackSlots;
  std::vector<uint8_t> lastPublishedReadback;
  std::vector<float> lastPublishedDepth;
  uint64_t lastPublishedSequence;
  std::vector<DirectTextureResource> directTextures;
  uint64_t cachedRevision;
  int cachedPassCount = 1;
  uint64_t cachedPassBudget = UINT64_C(256) * 1024 * 1024;
  int cachedWidth;
  int cachedHeight;
  bool cachedHomogeneousDepth;
  bgfx::TextureHandle defaultTexture;
  CoinBgfxPlan cachedPlan;
  // One bounded source snapshot proves bump reuse independently of revisions.
  std::unique_ptr<CoinRenderFramePlan> bumpReuseFrame;
  std::vector<bgfx::TextureHandle> cachedTextures;
  bgfx::DynamicVertexBufferHandle cachedVertexBuffer;
  bgfx::DynamicIndexBufferHandle cachedIndexBuffer;
  bgfx::DynamicVertexBufferHandle cachedInstanceBuffer = BGFX_INVALID_HANDLE;
  uint32_t cachedInstanceCapacity = 0;
  uint32_t cachedVertexCapacity;
  bool cachedCompactVertices = false;
  bool cachedInstancedVertices = false;
  uint32_t cachedIndexCapacity;
};

#endif
