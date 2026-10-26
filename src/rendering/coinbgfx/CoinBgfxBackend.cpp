// CoinRender BGFX Backend responsibilities.
#include "rendering/coinbgfx/CoinBgfxBackendInternal.h"

using namespace coin_bgfx_internal;

CoinBgfxBackend::CoinBgfxBackend()
  : status(CoinRenderBackendStatus::NOT_READY), viewBase(0), nativeDisplay(nullptr),
    nativeWindow(nullptr), initialized(false), presentToWindow(false),
    cameraPatchEnabled(true), drawGroupingEnabled(true), drawBatchingEnabled(true), readbackPipelineDepth(1),
    readbackCursor(0), readbackSequence(0),
    transparencyMode(CoinBgfxTransparencyMode::AUTO),
    activeTransparencyStrategy(CoinBgfxTransparencyStrategy::OBJECT),
    weightedOitSupported(false), sortedLayersSupported(false), serial(0),
    directTextureSerial(0),
    width(0), height(0), program(BGFX_INVALID_HANDLE),
    solidProgram(BGFX_INVALID_HANDLE), texture2dProgram(BGFX_INVALID_HANDLE),
    instancedProgram(BGFX_INVALID_HANDLE),
    activeProgram(BGFX_INVALID_HANDLE),
    shadowMomentsProgram(BGFX_INVALID_HANDLE),
    shadowReceiverProgram(BGFX_INVALID_HANDLE),
    shadowReceiverProgram4(BGFX_INVALID_HANDLE),
    shadowReceiverProgram8(BGFX_INVALID_HANDLE),
    shadowPeelProgram(BGFX_INVALID_HANDLE), shadowOitProgram(BGFX_INVALID_HANDLE),
    shadowModelViewUniform(BGFX_INVALID_HANDLE),
    shadowClipModelViewUniform(BGFX_INVALID_HANDLE),
    shadowDepthUniform(BGFX_INVALID_HANDLE),
    shadowQualityUniform(BGFX_INVALID_HANDLE), shadowFlatDiffuseUniform(BGFX_INVALID_HANDLE),
    shadowFlatSpecularUniform(BGFX_INVALID_HANDLE), shadowFlatParamsUniform(BGFX_INVALID_HANDLE),
    shadowLightIndicesUniform(BGFX_INVALID_HANDLE),
    shadowLightIndicesExtraUniform(BGFX_INVALID_HANDLE),
    depthReadProgram(BGFX_INVALID_HANDLE), readDepthSampler(BGFX_INVALID_HANDLE),
    depthReadFrameBuffer(BGFX_INVALID_HANDLE),
    peelNextProgram(BGFX_INVALID_HANDLE), compositeProgram(BGFX_INVALID_HANDLE),
    compositeDepthProgram(BGFX_INVALID_HANDLE),
    npotMipProgram(BGFX_INVALID_HANDLE),
    weightedOitProgram(BGFX_INVALID_HANDLE),
    weightedCompositeProgram(BGFX_INVALID_HANDLE),
    previousDepthSampler(BGFX_INVALID_HANDLE),
    opaqueDepthSampler(BGFX_INVALID_HANDLE),
    layerSampler(BGFX_INVALID_HANDLE), layerMaskSampler(BGFX_INVALID_HANDLE),
    layerDepthSampler(BGFX_INVALID_HANDLE), peelMetaUniform(BGFX_INVALID_HANDLE),
    npotMipSampler(BGFX_INVALID_HANDLE),
    npotMipExtentUniform(BGFX_INVALID_HANDLE), oitAccumSampler(BGFX_INVALID_HANDLE),
    oitRevealSampler(BGFX_INVALID_HANDLE), depthInfoUniform(BGFX_INVALID_HANDLE),
    coinDepthUniform(BGFX_INVALID_HANDLE), screenDoorUniform(BGFX_INVALID_HANDLE),
    alphaTestUniform(BGFX_INVALID_HANDLE), rasterViewportUniform(BGFX_INVALID_HANDLE),
    clipMetaUniform(BGFX_INVALID_HANDLE), clipPlanesUniform(BGFX_INVALID_HANDLE),
    textureSampler(BGFX_INVALID_HANDLE), fogColorModeUniform(BGFX_INVALID_HANDLE),
    fogRangeUniform(BGFX_INVALID_HANDLE), textureParamsUniform(BGFX_INVALID_HANDLE), samplingTextureSizesUniform(BGFX_INVALID_HANDLE),
    textureBlendUniform(BGFX_INVALID_HANDLE), textureCombineUniform(BGFX_INVALID_HANDLE),
    ambientLightUniform(BGFX_INVALID_HANDLE), lightCountUniform(BGFX_INVALID_HANDLE),
    lightPositionTypeUniform(BGFX_INVALID_HANDLE),
    lightDirectionCutoffUniform(BGFX_INVALID_HANDLE),
    lightColorIntensityUniform(BGFX_INVALID_HANDLE),
    lightAmbientColorUniform(BGFX_INVALID_HANDLE),
    instancedCameraUniform(BGFX_INVALID_HANDLE),
    lightAttenuationDropUniform(BGFX_INVALID_HANDLE),
    fullscreenVertexBuffer(BGFX_INVALID_HANDLE),
    fullscreenIndexBuffer(BGFX_INVALID_HANDLE),
    frameBuffer(BGFX_INVALID_HANDLE), windowSceneFrameBuffer(BGFX_INVALID_HANDLE),
    oitFrameBuffer(BGFX_INVALID_HANDLE),
    readbackTexture(BGFX_INVALID_HANDLE), lastPublishedSequence(0),
    defaultTexture(BGFX_INVALID_HANDLE),
    cachedRevision(0), cachedWidth(0), cachedHeight(0),
    cachedHomogeneousDepth(false), cachedVertexBuffer(BGFX_INVALID_HANDLE),
    cachedIndexBuffer(BGFX_INVALID_HANDLE), cachedVertexCapacity(0),
    cachedIndexCapacity(0)
{
  for (auto & handle : this->extraTextureSamplers) handle = BGFX_INVALID_HANDLE;
  for (int i = 0; i < 8; ++i) {
    this->shadowViewToClipUniform[i] = BGFX_INVALID_HANDLE;
    this->shadowViewToLightUniform[i] = BGFX_INVALID_HANDLE;
    this->shadowParamsUniform[i] = BGFX_INVALID_HANDLE;
    this->shadowMetaUniform[i] = BGFX_INVALID_HANDLE;
    this->shadowTexelUniform[i] = BGFX_INVALID_HANDLE;
    this->shadowSampler[i] = BGFX_INVALID_HANDLE;
  }
  for (uint8_t i = 0; i < COIN_RENDER_MAX_PEEL_LAYERS; ++i)
    this->peelFrameBuffers[i] = BGFX_INVALID_HANDLE;
  const char * disabled = std::getenv("COIN_BGFX_DISABLE_CAMERA_PATCH");
  this->cameraPatchEnabled = disabled == nullptr || std::strcmp(disabled, "1") != 0;
  const char * groupingDisabled = std::getenv("COIN_BGFX_DISABLE_DRAW_GROUPING");
  this->drawGroupingEnabled = groupingDisabled == nullptr ||
    std::strcmp(groupingDisabled, "1") != 0;
  const char * batchingDisabled = std::getenv("COIN_BGFX_DISABLE_DRAW_BATCHING");
  this->drawBatchingEnabled = batchingDisabled == nullptr ||
    std::strcmp(batchingDisabled, "1") != 0;
  const char * readbackDepth = std::getenv("COIN_BGFX_READBACK_PIPELINE_DEPTH");
  if (readbackDepth && std::strcmp(readbackDepth, "2") == 0)
    this->readbackPipelineDepth = 2;
  else if (readbackDepth && std::strcmp(readbackDepth, "3") == 0)
    this->readbackPipelineDepth = 3;
}

CoinBgfxBackend::~CoinBgfxBackend()
{
  const auto trace = [this](const char *stage) {
    bgfxTraceLifecycle(this, stage, sharedRuntime().portable.programCount.load(std::memory_order_relaxed),
        sharedRuntime().portable.uniformCount.load(std::memory_order_relaxed));
  };
  trace("backend_destructor_begin");
  this->shutdownRuntime();
  trace("backend_destructor_end");
}

CoinRenderBackendStatus
CoinBgfxBackend::checkRuntimeFailure(const char * operation)
{
  CoinBgfxCallback * cb = static_cast<CoinBgfxCallback *>(this->callback.get());
#if defined(_WIN32) && defined(COIN_WGPU_TEST_HOOKS)
  if (this->initialized && this->onApiThread() && cb && !cb->failed() &&
      bgfx::getRendererType() == bgfx::RendererType::Direct3D12 &&
      COIN_BGFX_CONSUME_TEST_FAULT("COIN_BGFX_TEST_REMOVE_NATIVE_DEVICE_ONCE")) {
    // A controlled runtime removal of this process's real D3D12 device.
    // It does not disable/reset the display adapter or emulate a driver TDR.
    auto * device = static_cast<ID3D12Device *>(bgfx::getInternalData()->context);
    ID3D12Device5 * removable = nullptr;
    const HRESULT queried = device->QueryInterface(__uuidof(ID3D12Device5),
                                                   reinterpret_cast<void **>(&removable));
    if (FAILED(queried)) {
      this->lastError = "D3D12 runtime removal interface unavailable";
      return CoinRenderBackendStatus::UNSUPPORTED;
    }
    const HRESULT before = device->GetDeviceRemovedReason();
    removable->RemoveDevice();
    const HRESULT after = device->GetDeviceRemovedReason();
    removable->Release();
    std::fprintf(stderr, "COIN_NATIVE_DEVICE_REMOVAL api=d3d12 before=0x%08lx after=0x%08lx runtime_remove=1 adapter_reset=0\n",
                 static_cast<unsigned long>(before), static_cast<unsigned long>(after));
    if (SUCCEEDED(after)) {
      this->lastError = "D3D12 RemoveDevice did not remove the device";
      return CoinRenderBackendStatus::UNSUPPORTED;
    }
    // Let BGFX observe removal itself, including its native swapchains.
    // Mirroring only the callback state can leave a removed window device
    // alive in DXGI and prevent recreation of the explicitly selected API.
    for (unsigned observe = 0; observe < 3 && !cb->failed(); ++observe)
      bgfx::frame();
    if (cb->failed()) return this->checkRuntimeFailure(operation);
    char reason[96];
    std::snprintf(reason, sizeof(reason), "Native D3D12 device removed, HRESULT 0x%08lx",
                  static_cast<unsigned long>(after));
    cb->fatal("D3D12 runtime RemoveDevice", 0, bgfx::Fatal::DeviceLost, reason);
  }
#endif
  if (cb == nullptr || !cb->failed()) return CoinRenderBackendStatus::SUCCESS;
  this->status = cb->deviceLost() ? CoinRenderBackendStatus::DEVICE_LOST :
                                   CoinRenderBackendStatus::BACKEND_ERROR;
  this->lastError = std::string(operation) + ": " + cb->diagnostic();
  return this->status;
}

void
CoinBgfxBackend::destroyResources()
{
  const auto trace = [this](const char *stage) {
    bgfxTraceLifecycle(this, stage, sharedRuntime().portable.programCount.load(std::memory_order_relaxed),
        sharedRuntime().portable.uniformCount.load(std::memory_order_relaxed));
  };
  trace("destroy_resources_begin");
  this->bumpReuseFrame.reset();
  this->samplingBoundTextureSizes.clear();
  if (!this->initialized) { trace("destroy_resources_end_uninitialized"); return; }
  this->releasePortablePreflightPins();
  // Handles are no longer trustworthy after a fatal renderer/device error.
  // bgfx::shutdown() owns their final release in that case.
  CoinBgfxCallback * cb = static_cast<CoinBgfxCallback *>(this->callback.get());
  if (cb != nullptr && cb->failed()) {
    for (auto & slot : this->readbackSlots) {
      auto entry = std::make_shared<AsyncEntry>();
      entry->slot = std::move(slot);
      failedReadbacks.push_back(entry);
    }
    this->readbackSlots.clear();
    trace("destroy_resources_end_failed_runtime");
    return;
  }
  this->destroyFrameBuffers();
  for (DirectTextureResource & resource : this->directTextures) {
    if (bgfx::isValid(resource.frameBuffer)) bgfx::destroy(resource.frameBuffer);
    if (bgfx::isValid(resource.viewSamples)) bgfx::destroy(resource.viewSamples);
  }
  for (auto handle : {this->stereoProgram, this->stereoMsaaProgram})
    if (bgfx::isValid(handle)) bgfx::destroy(handle);
  for (auto handle : this->stereoSamplers) if (bgfx::isValid(handle)) bgfx::destroy(handle);
  if (bgfx::isValid(this->stereoInfo)) bgfx::destroy(this->stereoInfo);
  if (bgfx::isValid(this->stereoViewport)) bgfx::destroy(this->stereoViewport);
  if (bgfx::isValid(this->fullscreenVertexBuffer)) bgfx::destroy(this->fullscreenVertexBuffer);
  if (bgfx::isValid(this->fullscreenIndexBuffer)) bgfx::destroy(this->fullscreenIndexBuffer);
  if (bgfx::isValid(this->previousDepthSampler)) bgfx::destroy(this->previousDepthSampler);
  if (bgfx::isValid(this->opaqueDepthSampler)) bgfx::destroy(this->opaqueDepthSampler);
  if (bgfx::isValid(this->layerSampler)) bgfx::destroy(this->layerSampler);
  if (bgfx::isValid(this->layerMaskSampler)) bgfx::destroy(this->layerMaskSampler);
  if (bgfx::isValid(this->layerDepthSampler)) bgfx::destroy(this->layerDepthSampler);
  if (bgfx::isValid(this->peelMetaUniform)) bgfx::destroy(this->peelMetaUniform);
  if (bgfx::isValid(this->npotMipSampler)) bgfx::destroy(this->npotMipSampler);
  if (bgfx::isValid(this->npotMipExtentUniform)) bgfx::destroy(this->npotMipExtentUniform);
  if (bgfx::isValid(this->depthInfoUniform)) bgfx::destroy(this->depthInfoUniform);
  if (bgfx::isValid(this->screenDoorUniform)) bgfx::destroy(this->screenDoorUniform);
  if (bgfx::isValid(this->alphaTestUniform)) bgfx::destroy(this->alphaTestUniform);
  if (bgfx::isValid(this->rasterViewportUniform)) bgfx::destroy(this->rasterViewportUniform);
  if (bgfx::isValid(this->clipMetaUniform)) bgfx::destroy(this->clipMetaUniform);
  if (bgfx::isValid(this->clipPlanesUniform)) bgfx::destroy(this->clipPlanesUniform);
  if (bgfx::isValid(this->coinDepthUniform)) bgfx::destroy(this->coinDepthUniform);
  if (bgfx::isValid(this->oitAccumSampler)) bgfx::destroy(this->oitAccumSampler);
  if (bgfx::isValid(this->oitRevealSampler)) bgfx::destroy(this->oitRevealSampler);
  if (bgfx::isValid(this->textureSampler)) bgfx::destroy(this->textureSampler);
  for (auto handle : this->extraTextureSamplers)
    if (bgfx::isValid(handle)) bgfx::destroy(handle);
  if (bgfx::isValid(this->fogColorModeUniform)) bgfx::destroy(this->fogColorModeUniform);
  if (bgfx::isValid(this->fogRangeUniform)) bgfx::destroy(this->fogRangeUniform);
  if (bgfx::isValid(this->samplingTextureSizesUniform)) bgfx::destroy(this->samplingTextureSizesUniform);
  if (bgfx::isValid(this->textureParamsUniform)) bgfx::destroy(this->textureParamsUniform);
  if (bgfx::isValid(this->textureBlendUniform)) bgfx::destroy(this->textureBlendUniform);
  if (bgfx::isValid(this->textureCombineUniform)) bgfx::destroy(this->textureCombineUniform);
  if (bgfx::isValid(this->ambientLightUniform)) bgfx::destroy(this->ambientLightUniform);
  if (bgfx::isValid(this->lightCountUniform)) bgfx::destroy(this->lightCountUniform);
  if (bgfx::isValid(this->lightPositionTypeUniform)) bgfx::destroy(this->lightPositionTypeUniform);
  if (bgfx::isValid(this->lightDirectionCutoffUniform)) bgfx::destroy(this->lightDirectionCutoffUniform);
  if (bgfx::isValid(this->lightColorIntensityUniform)) bgfx::destroy(this->lightColorIntensityUniform);
  if (bgfx::isValid(this->lightAmbientColorUniform)) bgfx::destroy(this->lightAmbientColorUniform);
  if (bgfx::isValid(this->instancedCameraUniform)) bgfx::destroy(this->instancedCameraUniform);
  if (bgfx::isValid(this->lightAttenuationDropUniform)) bgfx::destroy(this->lightAttenuationDropUniform);
  if (bgfx::isValid(this->defaultTexture)) bgfx::destroy(this->defaultTexture);
  for (bgfx::TextureHandle texture : this->cachedTextures)
    if (bgfx::isValid(texture)) bgfx::destroy(texture);
  if (bgfx::isValid(this->cachedVertexBuffer)) bgfx::destroy(this->cachedVertexBuffer);
  if (bgfx::isValid(this->cachedIndexBuffer)) bgfx::destroy(this->cachedIndexBuffer);
  if (bgfx::isValid(this->cachedInstanceBuffer)) bgfx::destroy(this->cachedInstanceBuffer);
  if (bgfx::isValid(this->peelNextProgram)) bgfx::destroy(this->peelNextProgram);
  if (bgfx::isValid(this->peelMsaaProgram)) bgfx::destroy(this->peelMsaaProgram);
  if (bgfx::isValid(this->shadowPeelMsaaProgram)) bgfx::destroy(this->shadowPeelMsaaProgram);
  if (bgfx::isValid(this->peelMsaaCompositeProgram)) bgfx::destroy(this->peelMsaaCompositeProgram);
  if (bgfx::isValid(this->peelMsaaColorSampler)) bgfx::destroy(this->peelMsaaColorSampler);
  if (bgfx::isValid(this->peelMsaaMaskSampler)) bgfx::destroy(this->peelMsaaMaskSampler);
  if (bgfx::isValid(this->peelMsaaDepthSampler)) bgfx::destroy(this->peelMsaaDepthSampler);
  if (bgfx::isValid(this->compositeProgram)) bgfx::destroy(this->compositeProgram);
  if (bgfx::isValid(this->compositeDepthProgram)) bgfx::destroy(this->compositeDepthProgram);
  for (auto handle : {this->multipassSumProgram, this->multipassMeanProgram})
    if (bgfx::isValid(handle)) bgfx::destroy(handle);
  for (auto handle : {this->multipassSceneSampler, this->multipassPreviousSampler, this->multipassInfoUniform})
    if (bgfx::isValid(handle)) bgfx::destroy(handle);
  if (bgfx::isValid(this->npotMipProgram)) bgfx::destroy(this->npotMipProgram);
  if (bgfx::isValid(this->rasterImageProgram)) bgfx::destroy(this->rasterImageProgram);
  if (bgfx::isValid(this->rasterPrimarySampler)) bgfx::destroy(this->rasterPrimarySampler);
  if (bgfx::isValid(this->rasterCoordinatesUniform)) bgfx::destroy(this->rasterCoordinatesUniform);
  if (bgfx::isValid(this->rasterExtentUniform)) bgfx::destroy(this->rasterExtentUniform);
  if (bgfx::isValid(this->msaaProgram)) bgfx::destroy(this->msaaProgram);
  if (bgfx::isValid(this->msaaColorSampler)) bgfx::destroy(this->msaaColorSampler);
  if (bgfx::isValid(this->msaaDepthSampler)) bgfx::destroy(this->msaaDepthSampler);
  if (bgfx::isValid(this->depthReadProgram)) bgfx::destroy(this->depthReadProgram);
  if (bgfx::isValid(this->readDepthSampler)) bgfx::destroy(this->readDepthSampler);
  for (auto handle : {this->bumpProgram, this->bumpNativeDepthProgram, this->bumpSampleDepthProgram,
                      this->bumpReceiverProgram, this->bumpReceiverNativeProgram,
                      this->bumpReceiverSampleProgram})
    if (bgfx::isValid(handle))
      bgfx::destroy(handle);
  for(auto handle:{this->bumpImageSampler,this->bumpBaseSampler,this->bumpParamsUniform})if(bgfx::isValid(handle))bgfx::destroy(handle);
  for (auto handle : {this->bumpLightMetaUniform,this->bumpLightVectorsUniform,
                       this->bumpLightColorsUniform,this->bumpEyeUniform,this->bumpModelViewUniform})
    if (bgfx::isValid(handle)) bgfx::destroy(handle);
  this->bumpLightMetaUniform=this->bumpLightVectorsUniform=this->bumpLightColorsUniform=
    this->bumpEyeUniform=this->bumpModelViewUniform=BGFX_INVALID_HANDLE;
  this->bumpReceiverVerifiedPipelines.clear();
  this->bumpProgram=this->bumpNativeDepthProgram=this->bumpSampleDepthProgram=BGFX_INVALID_HANDLE;
  this->bumpReceiverProgram = this->bumpReceiverNativeProgram = this->bumpReceiverSampleProgram =
      BGFX_INVALID_HANDLE;
  this->bumpImageSampler=this->bumpBaseSampler=this->bumpParamsUniform=BGFX_INVALID_HANDLE;
  trace("destroy_portable_resources_begin");
  this->portableProgramRoutes.clear();
  trace("destroy_portable_resources_end");
  if (bgfx::isValid(this->program)) bgfx::destroy(this->program);
  if (bgfx::isValid(this->solidProgram)) bgfx::destroy(this->solidProgram);
  if (bgfx::isValid(this->texture2dProgram)) bgfx::destroy(this->texture2dProgram);
  for (auto handle : {this->texture2dNativeDepthProgram, this->texture2dSampleDepthProgram})
    if (bgfx::isValid(handle)) bgfx::destroy(handle);
  this->texture2dNativeDepthProgram = this->texture2dSampleDepthProgram = BGFX_INVALID_HANDLE;
  this->activeTexture2dDepth = false;
  if (bgfx::isValid(this->instancedProgram)) bgfx::destroy(this->instancedProgram);
  if (bgfx::isValid(this->shadowMomentsProgram)) bgfx::destroy(this->shadowMomentsProgram);
  if (bgfx::isValid(this->surfaceNativeDepthProgram)) bgfx::destroy(this->surfaceNativeDepthProgram);
  if (bgfx::isValid(this->surfaceSampleDepthProgram)) bgfx::destroy(this->surfaceSampleDepthProgram);
  if (bgfx::isValid(this->shadowNativeDepthProgram)) bgfx::destroy(this->shadowNativeDepthProgram);
  if (bgfx::isValid(this->shadowSampleDepthProgram)) bgfx::destroy(this->shadowSampleDepthProgram);
  if (bgfx::isValid(this->shadowMultitexturePeelProgram)) bgfx::destroy(this->shadowMultitexturePeelProgram);
  if (bgfx::isValid(this->shadowMultitextureOitProgram)) bgfx::destroy(this->shadowMultitextureOitProgram);
  if (bgfx::isValid(this->shadowArrayTexture)) bgfx::destroy(this->shadowArrayTexture);
  if (bgfx::isValid(this->defaultShadowArrayTexture)) bgfx::destroy(this->defaultShadowArrayTexture);
  if (bgfx::isValid(this->shadowArraySampler)) bgfx::destroy(this->shadowArraySampler);
  if (bgfx::isValid(this->shadowMultitextureProgram)) bgfx::destroy(this->shadowMultitextureProgram);
  if (bgfx::isValid(this->shadowMultitextureSampleDepthProgram)) bgfx::destroy(this->shadowMultitextureSampleDepthProgram);
  if (bgfx::isValid(this->instancedNativeDepthProgram)) bgfx::destroy(this->instancedNativeDepthProgram);
  if (bgfx::isValid(this->instancedSampleDepthProgram)) bgfx::destroy(this->instancedSampleDepthProgram);
  if (bgfx::isValid(this->shadowReceiverProgram)) bgfx::destroy(this->shadowReceiverProgram);
  if (bgfx::isValid(this->shadowReceiverProgram4)) bgfx::destroy(this->shadowReceiverProgram4);
  if (bgfx::isValid(this->shadowReceiverProgram8)) bgfx::destroy(this->shadowReceiverProgram8);
  if (bgfx::isValid(this->shadowPeelProgram)) bgfx::destroy(this->shadowPeelProgram);
  if (bgfx::isValid(this->shadowOitProgram)) bgfx::destroy(this->shadowOitProgram);
  if (bgfx::isValid(this->shadowLightIndicesExtraUniform)) bgfx::destroy(this->shadowLightIndicesExtraUniform);
  if (bgfx::isValid(this->shadowModelViewUniform)) bgfx::destroy(this->shadowModelViewUniform);
  if (bgfx::isValid(this->shadowClipModelViewUniform)) bgfx::destroy(this->shadowClipModelViewUniform);
  if (bgfx::isValid(this->shadowDepthUniform)) bgfx::destroy(this->shadowDepthUniform);
  if (bgfx::isValid(this->shadowQualityUniform)) bgfx::destroy(this->shadowQualityUniform);
  if (bgfx::isValid(this->shadowFlatDiffuseUniform)) bgfx::destroy(this->shadowFlatDiffuseUniform);
  if (bgfx::isValid(this->shadowFlatSpecularUniform)) bgfx::destroy(this->shadowFlatSpecularUniform);
  if (bgfx::isValid(this->shadowFlatParamsUniform)) bgfx::destroy(this->shadowFlatParamsUniform);
  if (bgfx::isValid(this->shadowLightIndicesUniform)) bgfx::destroy(this->shadowLightIndicesUniform);
  for (int i = 0; i < 8; ++i) {
    if (bgfx::isValid(this->shadowViewToClipUniform[i])) bgfx::destroy(this->shadowViewToClipUniform[i]);
    if (bgfx::isValid(this->shadowViewToLightUniform[i])) bgfx::destroy(this->shadowViewToLightUniform[i]);
    if (bgfx::isValid(this->shadowParamsUniform[i])) bgfx::destroy(this->shadowParamsUniform[i]);
    if (bgfx::isValid(this->shadowMetaUniform[i])) bgfx::destroy(this->shadowMetaUniform[i]);
    if (bgfx::isValid(this->shadowTexelUniform[i])) bgfx::destroy(this->shadowTexelUniform[i]);
    if (bgfx::isValid(this->shadowSampler[i])) bgfx::destroy(this->shadowSampler[i]);
  }
  if (bgfx::isValid(this->weightedOitProgram)) bgfx::destroy(this->weightedOitProgram);
  if (bgfx::isValid(this->weightedCompositeProgram)) bgfx::destroy(this->weightedCompositeProgram);
  if (bgfx::isValid(this->weightedMsaaCompositeProgram)) bgfx::destroy(this->weightedMsaaCompositeProgram);
  if (bgfx::isValid(this->weightedMsaaAccumSampler)) bgfx::destroy(this->weightedMsaaAccumSampler);
  if (bgfx::isValid(this->weightedMsaaRevealSampler)) bgfx::destroy(this->weightedMsaaRevealSampler);
  trace("destroy_resources_end");
}

void
CoinBgfxBackend::shutdownRuntime()
{
  const auto trace = [this](const char *stage) {
    bgfxTraceLifecycle(this, stage, sharedRuntime().portable.programCount.load(std::memory_order_relaxed),
        sharedRuntime().portable.uniformCount.load(std::memory_order_relaxed));
  };
  trace("shutdown_runtime_begin");
  if (!this->initialized) { trace("shutdown_runtime_end_uninitialized"); return; }
  // Release this target without interrupting other windows on the shared device.
  if (!this->onApiThread()) {
    std::fprintf(stderr, "BGFX evaluation: destroy the target on its API thread; global renderer left active\n");
    trace("shutdown_runtime_end_wrong_thread");
    return;
  }
  this->destroyResources();
  SharedBgfxRuntime & runtime = sharedRuntime();
  trace("runtime_mutex_wait");
  std::lock_guard<std::mutex> guard(runtime.mutex);
  bgfxTraceLifecycle(this, "runtime_mutex_acquired", runtime.portable.programCount.load(std::memory_order_relaxed),
      runtime.portable.uniformCount.load(std::memory_order_relaxed),
      runtime.references);
  if (!static_cast<CoinBgfxCallback *>(runtime.callback.get())->failed()) {
    for (bgfx::ViewId view = this->viewBase;
         view < this->viewBase + this->viewCount; ++view) bgfx::resetView(view);
  }
  for(size_t i=this->viewBase/targetViewCount;i<(uint32_t(this->viewBase)+this->viewCount)/targetViewCount;++i) runtime.viewBlocks[i]=false;
  if (--runtime.references == 0) {
    runtime.portable.clear(!static_cast<CoinBgfxCallback *>(runtime.callback.get())->failed());
    trace("bgfx_shutdown_begin");
    bgfx::shutdown();
    trace("bgfx_shutdown_end");
    runtime.releaseNativeDisplay();
    failedReadbacks.clear();
    runtime.callback.reset();
    runtime.renderer = bgfx::RendererType::Count;
    runtime.apiThread = std::thread::id();
    runtime.viewBlocks.clear();
  } else if (!static_cast<CoinBgfxCallback *>(runtime.callback.get())->failed()) {
    // Surface destruction is queued to the render worker. Retire it before
    // the caller destroys the native window, while retaining other targets.
    // The second frame waits for consumption of the first frame's commands.
    trace("peer_frame_1_begin");
    bgfx::frame();
    trace("peer_frame_1_end");
    trace("peer_frame_2_begin");
    bgfx::frame();
    trace("peer_frame_2_end");
  }
  this->callback.reset();
  this->initialized = false;
  trace("shutdown_runtime_end");
}

bool
CoinBgfxBackend::reserveViews(uint32_t required) {
  if (required <= this->viewCount) return true;
  auto & runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
  const size_t blocks = (required + targetViewCount - 1) / targetViewCount;
  const size_t oldFirst = this->viewBase / targetViewCount, oldCount = this->viewCount / targetViewCount;
  size_t selected = runtime.viewBlocks.size();
  const auto available = [&](size_t first) {
    if (first + blocks > runtime.viewBlocks.size()) return false;
    for (size_t i=first; i<first+blocks; ++i)
      if (runtime.viewBlocks[i] && !(i>=oldFirst && i<oldFirst+oldCount)) return false;
    return true;
  };
  if (available(oldFirst)) selected = oldFirst;
  else for(size_t first=0; first<runtime.viewBlocks.size(); ++first) if(available(first)){selected=first;break;}
  if (selected == runtime.viewBlocks.size()) { this->lastError = "BGFX ordered composition has no contiguous free view span"; return false; }
  for(uint32_t view=this->viewBase; view<uint32_t(this->viewBase)+this->viewCount; ++view) bgfx::resetView(bgfx::ViewId(view));
  for(size_t i=oldFirst;i<oldFirst+oldCount;++i) runtime.viewBlocks[i]=false;
  for(size_t i=selected;i<selected+blocks;++i) runtime.viewBlocks[i]=true;
  this->viewBase=bgfx::ViewId(selected*targetViewCount);this->viewCount=uint32_t(blocks*targetViewCount);
  return true;
}

bool
CoinBgfxBackend::canReserveGraphViews(uint32_t rootViews, uint32_t producerViews,
  bool staged, std::string & diagnostic) const
{
  auto & runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
  const size_t oldFirst = this->viewBase / targetViewCount;
  const size_t oldCount = this->viewCount / targetViewCount;
  const uint32_t parentViews = staged ? rootViews : std::max(rootViews,producerViews);
  const size_t parentCount = (std::max(parentViews,this->viewCount)+targetViewCount-1)/targetViewCount;
  const size_t childCount = staged ? (std::max(uint32_t(targetViewCount),producerViews)+targetViewCount-1)/targetViewCount : 0;
  const auto owns = [&](size_t block) { return block >= oldFirst && block < oldFirst+oldCount; };
  // A STAGED producer first prepares its own backend while the live parent's
  // existing span remains owned. Probes must not move or grow that parent.
  if (childCount) {
    bool childFitsCurrent = false;
    for(size_t first=0;first+childCount<=runtime.viewBlocks.size();++first) {
      bool free=true;
      for(size_t i=first;i<first+childCount;++i) if(runtime.viewBlocks[i]) { free=false;break; }
      if(free) { childFitsCurrent=true;break; }
    }
    if(!childFitsCurrent) { diagnostic="BGFX STAGED producer has no free contiguous view span beside its live parent";return false; }
  }
  // Predict a real contiguous parent allocation, and (for STAGED) one largest
  // sequential child beside it. Other live targets remain occupied. No block
  // is retained by this inspection, including when admission succeeds.
  for(size_t first=0;first+parentCount<=runtime.viewBlocks.size();++first) {
    bool parentFits=true;
    for(size_t i=first;i<first+parentCount;++i)
      if(runtime.viewBlocks[i] && !owns(i)) { parentFits=false;break; }
    if(!parentFits) continue;
    if(!childCount) return true;
    for(size_t child=0;child+childCount<=runtime.viewBlocks.size();++child) {
      bool childFits=true;
      for(size_t i=child;i<child+childCount;++i)
        if((i>=first && i<first+parentCount) || (runtime.viewBlocks[i]&&!owns(i))) { childFits=false;break; }
      if(childFits) return true;
    }
  }
  diagnostic=staged ? "BGFX STAGED parent and largest sequential producer cannot coexist in contiguous view spans"
                    : "BGFX DIRECT graph has no free contiguous shared view span";
  return false;
}

bool
CoinBgfxBackend::onApiThread() const
{
  return this->apiThread == std::this_thread::get_id();
}

CoinRenderBackendStatus
CoinBgfxBackend::prepare(CoinRenderTargetP & target)
{
  this->preparedDepthReadbackEnabled = target.depthReadbackEnabled;
  if (this->initialized) {
    if (!this->onApiThread()) {
      this->lastError = "BGFX prepare requires the runtime API thread";
      return CoinRenderBackendStatus::UNSUPPORTED;
    }
    this->checkRuntimeFailure("BGFX shared renderer failed");
    return this->status;
  }
  this->presentToWindow = target.kind == CoinRenderTargetP::KIND_WINDOW;
#ifdef _WIN32
  if (this->presentToWindow &&
      (target.nativeDesc.type != COIN_RENDER_SURFACE_WIN32 ||
       target.nativeDesc.native.win32.hwnd == nullptr)) {
    this->lastError = "BGFX window presentation requires a valid Win32 surface";
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
#else
  if (this->presentToWindow &&
      (target.nativeDesc.type != COIN_RENDER_SURFACE_XLIB ||
       target.nativeDesc.native.xlib.display == nullptr ||
       target.nativeDesc.native.xlib.window == 0)) {
    this->lastError = "BGFX window presentation requires a valid Xlib surface";
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
#endif
  if (!target.optionsDiagnostic.empty() ||
      !coin_render_valid_options(target.options, this->lastError)) {
    if (!target.optionsDiagnostic.empty())
      this->lastError = target.optionsDiagnostic;
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
  this->sampleCount = target.options.sampleCount;
  this->portableSampling = target.options.textureSamplingPolicy == COIN_RENDER_SAMPLING_PORTABLE;
  this->canonicalCoverage = target.options.rasterizationPolicy == COIN_RENDER_RASTER_CANONICAL_8BIT;
  switch (target.options.transparency) {
  case COIN_RENDER_TRANSPARENCY_COIN:
    this->transparencyMode = CoinBgfxTransparencyMode::AUTO;
    break;
  case COIN_RENDER_TRANSPARENCY_OBJECT:
    this->transparencyMode = CoinBgfxTransparencyMode::OBJECT;
    break;
  case COIN_RENDER_TRANSPARENCY_PEELING:
    this->transparencyMode = CoinBgfxTransparencyMode::SORTED_LAYERS;
    break;
  case COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT:
    this->transparencyMode = CoinBgfxTransparencyMode::WEIGHTED_OIT;
    break;
  }
  this->activeTransparencyStrategy = CoinBgfxTransparencyStrategy::OBJECT;
  if (target.options.renderer == COIN_RENDER_RENDERER_METAL
#ifndef _WIN32
      || target.options.renderer == COIN_RENDER_RENDERER_D3D12
#endif
      ) {
    this->lastError = "Requested BGFX renderer is not supported on this platform";
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
  const bool useOpenGl = target.options.renderer == COIN_RENDER_RENDERER_OPENGL;
  bgfx::RendererType::Enum renderer =
      useOpenGl ? bgfx::RendererType::OpenGL : bgfx::RendererType::Vulkan;
#ifdef _WIN32
  if (target.options.renderer == COIN_RENDER_RENDERER_D3D12 ||
      target.options.renderer == COIN_RENDER_RENDERER_UNKNOWN)
    renderer = bgfx::RendererType::Direct3D12;
#endif
  SharedBgfxRuntime & runtime = sharedRuntime();
  std::unique_lock<std::mutex> runtimeGuard(runtime.mutex);
  if (runtime.references != 0 && runtime.apiThread != std::this_thread::get_id()) {
    this->lastError = "BGFX targets must share the runtime API thread";
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
  if (runtime.references != 0 && runtime.renderer != renderer) {
    this->lastError = "BGFX targets must share the active renderer";
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
  if (runtime.references != 0 &&
      static_cast<CoinBgfxCallback *>(runtime.callback.get())->failed()) {
    this->callback = runtime.callback;
    this->checkRuntimeFailure("BGFX shared renderer failed before target creation");
    return this->status;
  }
#if defined(__linux__) && !defined(__ANDROID__)
  if (this->presentToWindow && renderer == bgfx::RendererType::Vulkan &&
      !coin_render_x11_wsi::prepare(static_cast<Display *>(target.nativeDesc.native.xlib.display),
                                   this->lastError)) {
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
#endif
#ifdef _WIN32
  this->nativeDisplay = nullptr;
  this->nativeWindow = this->presentToWindow ? target.nativeDesc.native.win32.hwnd : nullptr;
#else
  this->nativeDisplay = this->presentToWindow ? target.nativeDesc.native.xlib.display : nullptr;
  this->nativeWindow = this->presentToWindow ? reinterpret_cast<void *>(
    static_cast<uintptr_t>(target.nativeDesc.native.xlib.window)) : nullptr;
#if defined(__linux__) && !defined(__ANDROID__)
  if (this->presentToWindow && renderer == bgfx::RendererType::OpenGL) {
    // Window creation may still be buffered on the caller's connection. The
    // renderer's separate connection must see the server resource before EGL
    // creates a surface for its XID.
    XSync(static_cast<Display *>(this->nativeDisplay), False);
  }
#endif
  if (this->presentToWindow && CoinRenderDiagnosticShell::phaseTracingEnabled()) {
    XWindowAttributes attributes;
    if (XGetWindowAttributes(static_cast<Display *>(this->nativeDisplay),
                             static_cast<Window>(target.nativeDesc.native.xlib.window), &attributes)) {
      std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_xlib visual=0x%lx depth=%d display=%s\n",
        XVisualIDFromVisual(attributes.visual), attributes.depth,
        XDisplayString(static_cast<Display *>(this->nativeDisplay)));
    }
  }
#endif
  if (runtime.references == 0) {
    runtime.portable.clear(false);
    runtime.weightedSamplesVerified = 0;
    runtime.weightedSamplesRejected = 0;
    runtime.peelingSamplesVerified = 0;
    runtime.peelingSamplesRejected = 0;
    runtime.programCommandsQueued = 0;
    runtime.programCommandsMaterialized = 0;
    bgfx::Init init;
    init.type = renderer;
    init.reset |= BGFX_RESET_MAXANISOTROPY;
    runtime.callback = std::make_shared<CoinBgfxCallback>(renderer == bgfx::RendererType::OpenGL);
    init.callback = runtime.callback.get();
    // No visible window owns the primary swapchain. Closing the first target
    // therefore cannot invalidate any other target's renderer/context.
    init.swapChain.ndt = this->nativeDisplay;
#if defined(__linux__) && !defined(__ANDROID__)
    if (renderer == bgfx::RendererType::OpenGL) {
      const char * displayName = this->nativeDisplay
        ? XDisplayString(static_cast<Display *>(this->nativeDisplay)) : nullptr;
      runtime.openGlDisplay.reset(XOpenDisplay(displayName));
      if (!runtime.openGlDisplay) {
        runtime.callback.reset();
        this->lastError = "BGFX OpenGL could not open its private X11 display connection";
        return CoinRenderBackendStatus::NOT_READY;
      }
      init.swapChain.ndt = runtime.openGlDisplay.get();
    }
#endif
    init.swapChain.nwh = nullptr;
    init.swapChain.width = 0;
    init.swapChain.height = 0;
    // Coin submits on one API thread and uses persistent buffers for geometry,
    // instances and fullscreen passes. BGFX's debug text owns separate pools.
    // Keep small, nonzero transient pools for BGFX's internal frame lifecycle;
    // the private opt-out restores its original reservations for comparisons.
    const char * disableSmallReservations =
      std::getenv("COIN_BGFX_DISABLE_SMALL_RUNTIME_RESERVATIONS");
    if (!(disableSmallReservations && std::strcmp(disableSmallReservations, "1") == 0)) {
      init.limits.maxEncoders = 1;
      init.limits.maxTransientVbSize = 64u * 1024u;
      init.limits.maxTransientIbSize = 64u * 1024u;
    }
    if (!bgfx::init(init)) {
      runtime.releaseNativeDisplay();
      runtime.callback.reset();
      this->lastError = std::string("BGFX could not initialize its headless renderer: ") + bgfx::getRendererName(renderer);
      return CoinRenderBackendStatus::NOT_READY;
    }
    // Secondary swapchains resolve Count from this exact Init descriptor in
    // Context::readSwapChain. RendererVK consumes that format without fallback.
    runtime.backbufferColorFormat = init.swapChain.formatColor;
    runtime.apiThread = std::this_thread::get_id();
    ++runtime.generation;
    runtime.renderer = renderer;
    runtime.viewBlocks.assign(bgfx::getCaps()->limits.maxViews / targetViewCount, false);
  }
  const auto freeBlock = std::find(runtime.viewBlocks.begin(), runtime.viewBlocks.end(), false);
  if (freeBlock == runtime.viewBlocks.end()) {
    this->lastError = "BGFX target view budget exhausted";
    if (runtime.references == 0) {
      runtime.portable.clear(true);
      bgfx::shutdown();
      runtime.releaseNativeDisplay();
      runtime.callback.reset();
      runtime.viewBlocks.clear();
    }
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
  this->viewBase = static_cast<bgfx::ViewId>(
    std::distance(runtime.viewBlocks.begin(), freeBlock) * targetViewCount);
  this->viewCount = targetViewCount;
  *freeBlock = true;
  ++runtime.references;
  this->callback = runtime.callback;
  runtimeGuard.unlock();
  this->initialized = true;
  this->apiThread = std::this_thread::get_id();
  if (COIN_BGFX_CONSUME_TEST_FAULT("COIN_BGFX_TEST_FAIL_PREPARE_ONCE")) {
    this->lastError = "Injected BGFX failure after runtime initialization";
    this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    return this->status;
  }
  const bgfx::Caps * caps = bgfx::getCaps();
  if (caps->rendererType != renderer) {
    this->lastError = "Requested BGFX renderer does not match the active renderer";
    this->status = CoinRenderBackendStatus::UNSUPPORTED;
    return this->status;
  }
  if (!target.capabilityProbeOnly &&
      caps->limits.maxTextureSamplers < COIN_RENDER_MAX_TEXTURE_UNITS + 2) {
    this->lastError = "BGFX renderer needs ten texture samplers for eight units and transparency";
    this->status = CoinRenderBackendStatus::UNSUPPORTED;
    return this->status;
  }
  if (CoinRenderDiagnosticShell::phaseTracingEnabled()) {
    std::fprintf(stderr,
      "COIN_RENDER_PHASE bgfx_device renderer=%s vendor_id=0x%04x device_id=0x%04x homogeneous_depth=%d\n",
      bgfx::getRendererName(renderer),
      static_cast<unsigned int>(caps->vendorId),
      static_cast<unsigned int>(caps->deviceId),
      caps->homogeneousDepth ? 1 : 0);
    std::fprintf(stderr,
      "COIN_RENDER_PHASE bgfx_formats rgba8=0x%x d24s8=0x%x d32f=0x%x readback=%d\n",
      static_cast<unsigned int>(caps->formats[bgfx::TextureFormat::RGBA8]),
      static_cast<unsigned int>(caps->formats[bgfx::TextureFormat::D24S8]),
      static_cast<unsigned int>(caps->formats[bgfx::TextureFormat::D32F]),
      bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK) ? 1 : 0);
  }
  this->sortedLayersSupported =
      caps->limits.maxFBAttachments >= 3 &&
      bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA16F, peelTextureFlags) &&
      (caps->formats[bgfx::TextureFormat::RGBA16F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) &&
      bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::R16F, peelTextureFlags) &&
      (caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) &&
      bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::D32F, peelTextureFlags);
  this->weightedOitSupported =
    (caps->supported & BGFX_CAPS_BLEND_INDEPENDENT) &&
    caps->limits.maxFBAttachments >= 3 &&
    bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA16F, peelTextureFlags) &&
    bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::R16F, peelTextureFlags);

  const bool baseSupported =
      (caps->supported & BGFX_CAPS_INDEX32) &&
      caps->limits.maxTextureSamplers >= COIN_RENDER_MAX_TEXTURE_UNITS + 2 &&
      (caps->formats[bgfx::TextureFormat::RGBA8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) &&
      ((caps->formats[bgfx::TextureFormat::D24S8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
       (caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER));
  this->windowSupported = baseSupported && (caps->supported & BGFX_CAPS_SWAP_CHAIN);
  this->offscreenSupported =
      baseSupported && bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA8,
                                            BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
  if (target.capabilityProbeOnly) {
    this->initialized = true;
    return this->status = CoinRenderBackendStatus::SUCCESS;
  }
  if (!(caps->supported & BGFX_CAPS_INDEX32) ||
      (this->presentToWindow && !(caps->supported & BGFX_CAPS_SWAP_CHAIN)) ||
      (!this->presentToWindow &&
       (!(caps->formats[bgfx::TextureFormat::RGBA8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
        (!(caps->formats[bgfx::TextureFormat::D24S8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) &&
         !(caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)) ||
        !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA8,
                              BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK)))) {
    this->lastError = "BGFX renderer lacks the required window/offscreen capabilities";
    this->status = CoinRenderBackendStatus::UNSUPPORTED;
    return this->status;
  }
  this->layout.begin()
    .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
    .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord2, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::Color1, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::Color2, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::Color3, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord3, 4, bgfx::AttribType::Float);
  this->compactLayout = this->layout;
  this->compactLayout.end();
  this->layout.add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::Bitangent, 4, bgfx::AttribType::Float)
    .end();
  this->instancedLayout.begin()
    .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
    .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
    .end();
  // BGFX's persistent instance-buffer API consumes the stride, not these
  // regular attributes. Skip the payload to avoid requiring unused bindings.
  this->instanceLayout.begin().skip(static_cast<uint8_t>(sizeof(CoinBgfxInstance))).end();
  // Select the required base fragment stage after lowering the first plan.
  // Preparing a target alone does not compile an unused general surface stage.
  this->textureSampler = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
  for (size_t unit = 1; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
    const std::string name = "s_texColor" + std::to_string(unit);
    this->extraTextureSamplers[unit - 1] = bgfx::createUniform(name.c_str(), bgfx::UniformType::Sampler);
  }
  this->fogColorModeUniform = bgfx::createUniform("u_fogColorMode", bgfx::UniformType::Vec4);
  this->fogRangeUniform = bgfx::createUniform("u_fogRange", bgfx::UniformType::Vec4);
  this->samplingTextureSizesUniform = bgfx::createUniform("u_coinSamplingSizes", bgfx::UniformType::Vec4, COIN_RENDER_MAX_TEXTURE_UNITS);
  this->textureParamsUniform = bgfx::createUniform("u_texParams", bgfx::UniformType::Vec4, COIN_RENDER_MAX_TEXTURE_UNITS);
  this->screenDoorUniform = bgfx::createUniform("u_screenDoor", bgfx::UniformType::Vec4);
  this->alphaTestUniform = bgfx::createUniform("u_alphaTest", bgfx::UniformType::Vec4);
  this->rasterViewportUniform = bgfx::createUniform("u_rasterViewport", bgfx::UniformType::Vec4);
  this->clipMetaUniform = bgfx::createUniform("u_clipMeta", bgfx::UniformType::Vec4);
  this->clipPlanesUniform = bgfx::createUniform("u_clipPlanes", bgfx::UniformType::Vec4, COIN_RENDER_MAX_CLIP_PLANES);
  this->coinDepthUniform = bgfx::createUniform("u_coinDepth", bgfx::UniformType::Vec4);
  this->textureBlendUniform = bgfx::createUniform("u_texBlend", bgfx::UniformType::Vec4, COIN_RENDER_MAX_TEXTURE_UNITS);
  this->textureCombineUniform = bgfx::createUniform("u_texCombine", bgfx::UniformType::Vec4, COIN_RENDER_MAX_TEXTURE_UNITS * 4);
  this->ambientLightUniform = bgfx::createUniform("u_ambientLight", bgfx::UniformType::Vec4);
  this->lightCountUniform = bgfx::createUniform("u_lightCount", bgfx::UniformType::Vec4);
  this->lightPositionTypeUniform = bgfx::createUniform("u_lightPositionType", bgfx::UniformType::Vec4, COIN_RENDER_MAX_LIGHTS);
  this->lightDirectionCutoffUniform = bgfx::createUniform("u_lightDirectionCutoff", bgfx::UniformType::Vec4, COIN_RENDER_MAX_LIGHTS);
  this->lightColorIntensityUniform = bgfx::createUniform("u_lightColorIntensity", bgfx::UniformType::Vec4, COIN_RENDER_MAX_LIGHTS);
  this->lightAmbientColorUniform = bgfx::createUniform("u_lightAmbientColor", bgfx::UniformType::Vec4, COIN_RENDER_MAX_LIGHTS);
  this->instancedCameraUniform = bgfx::createUniform("u_instancedCamera", bgfx::UniformType::Vec4, 6);
  this->lightAttenuationDropUniform = bgfx::createUniform("u_lightAttenuationDrop", bgfx::UniformType::Vec4, COIN_RENDER_MAX_LIGHTS);
  const uint32_t whitePixel = UINT32_C(0xffffffff);
  this->defaultTexture = bgfx::createTexture2D(1, 1, false, 1,
    bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
    bgfx::copy(&whitePixel, sizeof(whitePixel)));
  bool textureUniformsValid = bgfx::isValid(this->fogColorModeUniform) && bgfx::isValid(this->fogRangeUniform);
  for (auto handle : this->extraTextureSamplers) textureUniformsValid = textureUniformsValid && bgfx::isValid(handle);
  if (!textureUniformsValid || !bgfx::isValid(this->textureSampler) ||
      !bgfx::isValid(this->coinDepthUniform) ||
      !bgfx::isValid(this->alphaTestUniform) || !bgfx::isValid(this->rasterViewportUniform) ||
      !bgfx::isValid(this->samplingTextureSizesUniform) || !bgfx::isValid(this->textureParamsUniform) ||
      !bgfx::isValid(this->textureBlendUniform) ||
      !bgfx::isValid(this->textureCombineUniform) ||
      !bgfx::isValid(this->ambientLightUniform) ||
      !bgfx::isValid(this->clipMetaUniform) ||
      !bgfx::isValid(this->clipPlanesUniform) ||
      !bgfx::isValid(this->lightCountUniform) ||
      !bgfx::isValid(this->lightPositionTypeUniform) ||
      !bgfx::isValid(this->lightDirectionCutoffUniform) ||
      !bgfx::isValid(this->lightColorIntensityUniform) ||
      !bgfx::isValid(this->lightAmbientColorUniform) ||
      !bgfx::isValid(this->instancedCameraUniform) ||
      !bgfx::isValid(this->lightAttenuationDropUniform) ||
      !bgfx::isValid(this->defaultTexture)) {
    this->lastError = "BGFX could not allocate texture uniforms or default texture";
    this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    return this->status;
  }
  if (!this->resize(target.size[0], target.size[1])) {
    if (this->status != CoinRenderBackendStatus::DEVICE_LOST)
      this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    return this->status;
  }
  if (!this->presentToWindow && target.depthReadbackEnabled && !this->prepareDepthReadbackResources())
    return this->status;
  this->status = CoinRenderBackendStatus::SUCCESS;
  this->lastError.clear();
  return this->status;
}
