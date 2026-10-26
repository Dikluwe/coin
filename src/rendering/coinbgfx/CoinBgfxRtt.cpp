// CoinRender BGFX Rtt responsibilities.
#include "rendering/coinbgfx/CoinBgfxBackendInternal.h"

using namespace coin_bgfx_internal;

CoinRenderSubmitResult
CoinBgfxBackend::submitDirectTexture(const CoinRenderFramePlan & frame,
                                       const SbVec2i32 & size,
                                       uint64_t producerKey,
                                       uint64_t & token)
{
  if (!CoinRenderCubeTextureCore::validate(frame, this->lastError) ||
      !CoinRenderTextureSamplingCore::validVolumeFrame(frame, this->lastError))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  token = 0;
  const auto outputFormat=coinBgfxTextureFormat(frame.outputColorFormat);
  const auto * runtimeCaps=bgfx::getCaps();
  if(!runtimeCaps) return {CoinRenderBackendStatus::NOT_READY,"BGFX direct RTT runtime unavailable"};
  const bool manualMips = frame.outputMipmaps &&
    (!CoinRenderTextureSamplingCore::powerOfTwo(size[0]) ||
     !CoinRenderTextureSamplingCore::powerOfTwo(size[1]));
  const char * timestampFlag = std::getenv("COIN_WGPU_GPU_TIMESTAMPS");
  const bool traceNpotGpu = manualMips &&
    CoinRenderDiagnosticShell::phaseTracingEnabled() && timestampFlag &&
    std::strcmp(timestampFlag, "1") == 0;
  if(frame.outputMipmaps && !manualMips &&
     !(runtimeCaps->formats[outputFormat]&BGFX_CAPS_FORMAT_TEXTURE_MIP_AUTOGEN))
    return {CoinRenderBackendStatus::UNSUPPORTED,"BGFX format lacks direct GPU mip generation"};
  if(manualMips &&
     (!(runtimeCaps->formats[outputFormat]&BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
      !bgfx::isTextureValid(1, false, 1, outputFormat,
        BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT)))
    return {CoinRenderBackendStatus::UNSUPPORTED,"BGFX format lacks NPOT GPU area-reduction attachments"};

  if (!this->initialized || !this->onApiThread() ||
      producerKey == 0 || size[0] <= 0 || size[1] <= 0 ||
      (!frame.viewOutput && (size[0] > CoinRenderRttPlan::maxExtent || size[1] > CoinRenderRttPlan::maxExtent))) {
    return CoinRenderSubmitResult(CoinRenderBackendStatus::NOT_READY,
                        "BGFX direct RTT requires a prepared backend and valid extent");
  }
  for(const auto & image:frame.textures)if(image.cubeFaceSize && image.gpuToken) {
    if(!CoinRenderCubeTextureCore::valid(image))return {CoinRenderBackendStatus::UNSUPPORTED,"Invalid GPU cube atlas metadata"};
    const auto found=std::find_if(this->directTextures.begin(),this->directTextures.end(),[&image](const DirectTextureResource & resource) {
      return resource.token==image.gpuToken && resource.inUse && resource.width==int(image.width) &&
        resource.height==int(image.height) && resource.cubeFaceSize==image.cubeFaceSize &&
        resource.cubeMipmapped==image.cubeMipmapped && resource.format==image.format;
    });
    if(found==this->directTextures.end())return {CoinRenderBackendStatus::UNSUPPORTED,"GPU cube token belongs to another owner/generation or has mismatched metadata"};
  }
  const CoinRenderBackendStatus initialRuntimeStatus = this->checkRuntimeFailure("BGFX shared renderer failed before direct RTT");
  if (initialRuntimeStatus != CoinRenderBackendStatus::SUCCESS)
    return CoinRenderSubmitResult(initialRuntimeStatus, this->lastError);
  if(injectOutOfMemoryAfterSubmits()) {
    this->lastError="Injected BGFX out of memory after successful logical submissions";
    return {CoinRenderBackendStatus::OUT_OF_MEMORY,this->lastError};
  }
  if (size[0] > runtimeCaps->limits.maxTextureSize ||
      size[1] > runtimeCaps->limits.maxTextureSize)
    return {CoinRenderBackendStatus::UNSUPPORTED,
            "BGFX direct RTT exceeds the adapter texture dimension"};
  if (traceNpotGpu) bgfx::setDebug(BGFX_DEBUG_PROFILER);
  const auto textureAdmission = bgfxTextureProfile(frame);
  if (textureAdmission.status != CoinRenderBackendStatus::SUCCESS) return textureAdmission;
  const auto msaaAdmission = this->msaaProfile(frame);
  if (msaaAdmission.status != CoinRenderBackendStatus::SUCCESS) return msaaAdmission;
  if (std::any_of(frame.renderStates.begin(), frame.renderStates.end(), [](const CoinRenderRenderStateSnapshot & state) {
        return state.rasterTextureSourceSlot != UINT32_MAX;
      }) && !this->prepareRasterImageResources())
    return {CoinRenderBackendStatus::BACKEND_ERROR, this->lastError};
  const bool hasShadows = !frame.shadowGroups.empty();
  CoinRenderShadowPlan shadowPlan;
  if (hasShadows) {
    if (!coin_render_plan_shadows(frame, shadowPlan, this->lastError) ||
        !coin_render_shadow_object_profile(frame, shadowPlan,
          shadowPlan.passes.size(), this->lastError))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    if (!bgfxShadowBatchSupported(frame, shadowPlan, this->lastError))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    const bgfx::Caps * caps = bgfx::getCaps();
    if (caps && caps->limits.maxTextureSamplers < (bgfxShadowMultitexture(frame)?11:shadowPlan.passes.size() > 4 ? 16 : shadowPlan.passes.size() > 2 ? 12 : 10))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED,
        "BGFX direct RTT shadow receiver has insufficient texture stages");
    for (const auto & pass : shadowPlan.passes)
      if (pass.mapSize > caps->limits.maxTextureSize ||
          !(caps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
          !(caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
          !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA32F, shadowTextureFlags))
        return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED,
          "BGFX direct RTT cannot render the planned shadow maps");
    if (bgfxShadowMultitexture(frame) && !coin_render_shadow_array_budget(shadowPlan,2,this->lastError))
      return {CoinRenderBackendStatus::UNSUPPORTED,this->lastError};
    if (!this->prepareShadowPrograms(shadowPlan.passes.size(),
                                     !bgfxShadowMultitexture(frame) && this->sampleCount == 1 &&
              bgfxFrameProgramNeeds(frame).ordinary))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  }
  CoinBgfxPlan plan;
  if (!CoinBgfxLowering::lower(frame, size[0], size[1],
        bgfx::getCaps()->homogeneousDepth, plan, this->lastError, hasShadows)) {
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  bgfxSplitSamplerViews(plan);
  CoinBgfxTransparencyStrategy strategy;
  if (!CoinBgfxLowering::selectTransparencyStrategy(plan.draws,
        CoinBgfxTransparencyMode::AUTO, this->weightedOitSupported,
        this->sortedLayersSupported, strategy, this->lastError))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  uint32_t requiredViews = 0;
  if (!bgfxCompositionViewBudget(frame, plan.draws, strategy,
        shadowPlan.passes.size(), true, size[0], size[1],
        0, this->sampleCount > 1, this->lastError, &requiredViews) || !this->reserveViews(requiredViews))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  if (!this->prepareTransparencyPrograms(strategy, bgfxNeedsFixedSurfacePrograms(plan.draws)))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  if (!this->prepareBaseProgram(plan.draws, hasShadows, strategy, false, false,
                                coinBgfxTextureFormat(frame.outputColorFormat)))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  if (!this->preparePortablePrograms(frame, plan.draws, hasShadows, strategy, false,
        coinBgfxTextureFormat(frame.outputColorFormat)))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  if (manualMips && !this->prepareNpotMipProgram())
    return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  if (hasShadows && !bgfxShadowMultitexture(frame) && strategy != CoinBgfxTransparencyStrategy::OBJECT &&
      !this->prepareShadowTransparencyPrograms())
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  // A producer's temporary MSAA attachments must not replace the root
  // target's previous per-sample depth. They have independent extents/lifetime.
  struct RootMsaaScope {
    CoinBgfxBackend * owner;
    bgfx::FrameBufferHandle buffer;
    int width, height;
    bool active;
    explicit RootMsaaScope(CoinBgfxBackend * backend, bool viewOutput) : owner(backend),
      buffer(backend->msaaFrameBuffer), width(backend->msaaWidth), height(backend->msaaHeight),
      active((viewOutput || backend->managerDepthMode) && backend->sampleCount > 1) {
      if (active) {
        owner->msaaFrameBuffer = BGFX_INVALID_HANDLE;
        owner->msaaWidth = owner->msaaHeight = 0;
      }
    }
    ~RootMsaaScope() {
      if (active) {
        if (bgfx::isValid(owner->msaaFrameBuffer)) bgfx::destroy(owner->msaaFrameBuffer);
        owner->msaaFrameBuffer = buffer;
        owner->msaaWidth = width;
        owner->msaaHeight = height;
      }
    }
  } rootMsaaScope(this, frame.viewOutput);
  const auto msaaPreparation = this->prepareMsaa(size[0], size[1]);
  if (msaaPreparation.status != CoinRenderBackendStatus::SUCCESS) return msaaPreparation;
  auto allocationOptions = frame.transparency;
  if (strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT) allocationOptions.layers = 1;
  uint64_t requiredBytes = 0;
  if (!coin_render_transparency_sample_budget(size[0], size[1], allocationOptions,
        strategy != CoinBgfxTransparencyStrategy::OBJECT, this->sampleCount, requiredBytes, this->lastError))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  if (strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT && this->sampleCount > 1 &&
      !bgfxWeightedSamples(this->sampleCount, this->layout, this->viewBase, this->lastError))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  if (strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS && this->sampleCount > 1 &&
      !bgfxPeelingSamples(this->sampleCount, this->layout, this->viewBase, this->lastError))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  // Private attachments let the producer reuse the same compositor without
  // changing the consumer target's dimensions or persistent resources.
  struct TransparencyAttachments {
    std::vector<bgfx::FrameBufferHandle> layers;
    bgfx::FrameBufferHandle oit = BGFX_INVALID_HANDLE;
    ~TransparencyAttachments() {
      for (auto layer : layers) bgfx::destroy(layer);
      if (bgfx::isValid(oit)) bgfx::destroy(oit);
    }
  } transparency;
  const unsigned count = strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS ?
    frame.transparency.layers : strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT ? 1 : 0;
  for (unsigned layer = 0; layer < count; ++layer) {
    const bool weighted = strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT;
    bgfx::TextureHandle attachments[3] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
    const uint64_t flags = weightedTextureFlags(this->sampleCount);
    attachments[0] = bgfx::createTexture2D(size[0], size[1], false, 1,
      bgfx::TextureFormat::RGBA16F, flags);
    if (weighted) attachments[1] = bgfx::createTexture2D(size[0], size[1], false, 1,
      bgfx::TextureFormat::R16F, flags);
    if (!weighted) attachments[1] = bgfx::createTexture2D(size[0], size[1], false, 1,
      bgfx::TextureFormat::R16F, flags);
    const unsigned depthSlot = 2;
    attachments[depthSlot] = bgfx::createTexture2D(size[0], size[1], false, 1,
      bgfx::TextureFormat::D32F, flags);
    bool valid = true;
    for (unsigned i = 0; i <= depthSlot; ++i) valid = valid && bgfx::isValid(attachments[i]);
    bgfx::FrameBufferHandle buffer = BGFX_INVALID_HANDLE;
    if (valid) buffer = bgfx::createFrameBuffer(depthSlot + 1, attachments, true);
    if (!bgfx::isValid(buffer)) {
      for (unsigned i = 0; i <= depthSlot; ++i)
        if (bgfx::isValid(attachments[i])) bgfx::destroy(attachments[i]);
      return {CoinRenderBackendStatus::OUT_OF_MEMORY, "BGFX direct RTT transparency allocation failed"};
    }
    if (weighted) transparency.oit = buffer;
    else transparency.layers.push_back(buffer);
  }

  const auto multipassAdmission = this->preflightMultipass(frame, size);
  if (multipassAdmission.status != CoinRenderBackendStatus::SUCCESS) return multipassAdmission;
  if (frame.numPasses > 1 && !this->prepareMultipassPrograms())
    return {CoinRenderBackendStatus::BACKEND_ERROR, this->lastError};
  DirectTextureResource * cachedResource = NULL;
  for (DirectTextureResource & resource : this->directTextures) {
    if (resource.producerKey == producerKey && !resource.inUse) {
      cachedResource = &resource;
      break;
    }
  }
  const bool replaceCachedOutput = cachedResource &&
    (cachedResource->width != size[0] || cachedResource->height != size[1] ||
     cachedResource->format != frame.outputColorFormat ||
     cachedResource->mipmapped != frame.outputMipmaps ||
     (strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
      !cachedResource->depthSampleable));
  bgfx::FrameBufferHandle output = BGFX_INVALID_HANDLE;
  if (cachedResource && !manualMips && !replaceCachedOutput)
    output = cachedResource->frameBuffer;
  const bool newOutput = !bgfx::isValid(output);
  if (newOutput) {
    const bgfx::Caps * caps = bgfx::getCaps();
    const bgfx::TextureFormat::Enum depthFormat =
      !frame.viewOutput && strategy != CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
      (caps->formats[bgfx::TextureFormat::D24S8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)
        ? bgfx::TextureFormat::D24S8 : bgfx::TextureFormat::D32F;
    bgfx::TextureHandle color = bgfx::createTexture2D(
      static_cast<uint16_t>(size[0]), static_cast<uint16_t>(size[1]), frame.outputMipmaps, 1,
      coinBgfxTextureFormat(frame.outputColorFormat), peelTextureFlags);
    bgfx::TextureHandle depth = bgfx::createTexture2D(
      static_cast<uint16_t>(size[0]), static_cast<uint16_t>(size[1]), false, 1,
      depthFormat, (frame.viewOutput || strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS)
                       ? peelTextureFlags : BGFX_TEXTURE_RT_WRITE_ONLY);
    if (bgfx::isValid(color) && bgfx::isValid(depth)) {
      if (manualMips) {
        bgfx::Attachment attachments[2];
        attachments[0].init(color, bgfx::Access::Write, 0, 1, 0,
                            BGFX_ATTACHMENT_NONE);
        attachments[1].init(depth, bgfx::Access::Write, 0, 1, 0,
                            BGFX_ATTACHMENT_NONE);
        output = bgfx::createFrameBuffer(2, attachments, true);
      } else {
        const bgfx::TextureHandle attachments[2] = {color, depth};
        output = bgfx::createFrameBuffer(2, attachments, true);
      }
    }
    if (!bgfx::isValid(color) || !bgfx::isValid(depth) || !bgfx::isValid(output)) {
      if (bgfx::isValid(output)) bgfx::destroy(output);
      else {
        if (bgfx::isValid(color)) bgfx::destroy(color);
        if (bgfx::isValid(depth)) bgfx::destroy(depth);
      }
      return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY,
                          "BGFX could not allocate direct RTT framebuffer");
    }
  }
  if (strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
      !bgfx::isValid(bgfx::getTexture(output, 1))) {
    if (newOutput) bgfx::destroy(output);
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED,
      "BGFX direct sorted layers requires a sampleable opaque depth attachment");
  }

  bgfx::DynamicVertexBufferHandle vb = BGFX_INVALID_HANDLE;
  bgfx::DynamicIndexBufferHandle ib = BGFX_INVALID_HANDLE;
  if (!plan.vertices.empty() && !plan.indices.empty()) {
    vb = bgfx::createDynamicVertexBuffer(bgfx::copy(plan.vertices.data(),
      static_cast<uint32_t>(plan.vertices.size() * sizeof(CoinBgfxVertex))),
      this->layout);
    ib = bgfx::createDynamicIndexBuffer(bgfx::copy(plan.indices.data(),
      static_cast<uint32_t>(plan.indices.size() * sizeof(uint32_t))),
      BGFX_BUFFER_INDEX32);
  }
  if ((!plan.vertices.empty() && !plan.indices.empty()) && (!bgfx::isValid(vb) || !bgfx::isValid(ib))) {
    if (bgfx::isValid(vb)) bgfx::destroy(vb);
    if (bgfx::isValid(ib)) bgfx::destroy(ib);
    if (newOutput) bgfx::destroy(output);
    return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY,
                        "BGFX could not allocate direct RTT geometry");
  }

  std::vector<bgfx::TextureHandle> textures(plan.textures.size(), BGFX_INVALID_HANDLE);
  std::vector<bool> owned(plan.textures.size(), false);
  for (size_t i = 0; i < plan.textures.size(); ++i) {
    const CoinBgfxTexture & source = plan.textures[i];
    if (source.gpuCopySourceSlot != SIZE_MAX) {
      textures[i] = bgfx::createTexture2D(static_cast<uint16_t>(source.width),
        static_cast<uint16_t>(source.height), false, 1,
        coinBgfxTextureFormat(source.format),
        coinBgfxTextureFlags(source.format) | BGFX_TEXTURE_BLIT_DST);
      owned[i] = true;
    } else if (source.gpuToken != 0) {
      for (const DirectTextureResource & resource : this->directTextures) {
        if (resource.token == source.gpuToken && resource.format == source.format &&
            resource.cubeFaceSize==source.cubeFaceSize && resource.cubeMipmapped==source.cubeMipmapped &&
            (!source.mipmapped || resource.mipmapped) && resource.width == int(source.width) && resource.height == int(source.height))
          textures[i] = bgfx::getTexture(resource.frameBuffer, 0);
      }
    } else {
      textures[i] = bgfx::createTexture2D(static_cast<uint16_t>(source.width),
        static_cast<uint16_t>(source.height), source.mipmapped, 1, coinBgfxTextureFormat(source.format),
        coinBgfxTextureFlags(source.format), bgfx::copy(source.pixelsRgba.data(),
          static_cast<uint32_t>(source.pixelsRgba.size())));
      owned[i] = true;
    }
    if (!bgfx::isValid(textures[i])) {
      for (size_t j = 0; j < textures.size(); ++j)
        if (owned[j] && bgfx::isValid(textures[j])) bgfx::destroy(textures[j]);
      if (bgfx::isValid(vb)) bgfx::destroy(vb);
      if (bgfx::isValid(ib)) bgfx::destroy(ib);
      if (newOutput) bgfx::destroy(output);
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED,
                          "BGFX direct RTT references an invalid producer texture");
    }
  }

  if(this->portableSampling) for(size_t i=0;i<textures.size();++i)
    this->samplingBoundTextureSizes[textures[i].idx]={plan.textures[i].width,plan.textures[i].height};
  for (size_t i = 0; i < plan.textures.size(); ++i) {
    const size_t sourceSlot = plan.textures[i].gpuCopySourceSlot;
    if (sourceSlot == SIZE_MAX) continue;
    bgfx::TextureRegion destination, source;
    destination.handle = textures[i];
    source.handle = textures[sourceSlot];
    bgfx::blit(this->viewBase, destination, source);
  }
  const auto cleanupDirect = [&]() {
    for (size_t i = 0; i < textures.size(); ++i)
      if (owned[i] && bgfx::isValid(textures[i])) bgfx::destroy(textures[i]);
    if (bgfx::isValid(vb)) bgfx::destroy(vb);
    if (bgfx::isValid(ib)) bgfx::destroy(ib);
    if (newOutput) bgfx::destroy(output);
  };
  if (!this->prepareShadowArray(shadowPlan,bgfxShadowMultitexture(frame))) {
    cleanupDirect(); return {CoinRenderBackendStatus::UNSUPPORTED,this->lastError};
  }
  struct RasterImageOwner {
    std::vector<bgfx::TextureHandle> textures;
    ~RasterImageOwner() { for (auto handle : textures) if (bgfx::isValid(handle)) bgfx::destroy(handle); }
  } rasterImages;
  const auto rasterResult = this->encodeRasterImages(frame, textures, rasterImages.textures);
  if (rasterResult.status != CoinRenderBackendStatus::SUCCESS) { cleanupDirect(); return rasterResult; }
  const bgfx::ViewId rasterViewOffset = rasterImages.textures.empty() ? 0 : 1;
  struct ShadowMapOwner {
    CoinBgfxCallback * callback;
    std::vector<bgfx::FrameBufferHandle> buffers;
    ~ShadowMapOwner() {
      if (callback && callback->failed()) return;
      for (auto handle : buffers)
        if (bgfx::isValid(handle)) bgfx::destroy(handle);
    }
  } shadowMaps{static_cast<CoinBgfxCallback *>(this->callback.get()), {}};
  if (hasShadows) {
    if (consumeTestFault("COIN_BGFX_TEST_SHADOW_MAP_ALLOC_ONCE")) {
      cleanupDirect();
      this->lastError = "Injected BGFX shadow-map allocation failure";
      return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, this->lastError);
    }
    for (const auto & pass : shadowPlan.passes) {
      const uint16_t size = static_cast<uint16_t>(pass.mapSize);
      bgfx::TextureHandle moments = bgfx::createTexture2D(size, size, false, 1,
        bgfx::TextureFormat::RGBA32F, shadowTextureFlags);
      bgfx::TextureHandle depth = bgfx::createTexture2D(size, size, false, 1,
        bgfx::TextureFormat::D32F, BGFX_TEXTURE_RT_WRITE_ONLY);
      const bgfx::TextureHandle attachments[2] = {moments, depth};
      bgfx::FrameBufferHandle output = BGFX_INVALID_HANDLE;
      if (bgfx::isValid(moments) && bgfx::isValid(depth))
        output = bgfx::createFrameBuffer(2, attachments, true);
      if (!bgfx::isValid(output)) {
        if (bgfx::isValid(moments)) bgfx::destroy(moments);
        if (bgfx::isValid(depth)) bgfx::destroy(depth);
        cleanupDirect();
        this->lastError = "BGFX shadow-map attachment allocation failed";
        return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, this->lastError);
      }
      shadowMaps.buffers.push_back(output);
    }
    const SbMatrix clipConversion(
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.5f, 0.0f,
      0.0f, 0.0f, 0.5f, 1.0f);
    const float white[4] = {1, 1, 1, 1};
    bgfx::setPaletteColor(3, white);
    for (size_t slot = 0; slot < shadowPlan.passes.size(); ++slot) {
      const auto & pass = shadowPlan.passes[slot];
      const bgfx::ViewId mapView = this->viewBase + rasterViewOffset + static_cast<bgfx::ViewId>(slot);
      bgfx::setViewName(mapView, "shadow_moments");
      bgfx::setViewMode(mapView, bgfx::ViewMode::Sequential);
      bgfx::setViewRect(mapView, 0, 0, static_cast<uint16_t>(pass.mapSize),
                        static_cast<uint16_t>(pass.mapSize));
      bgfx::setViewFrameBuffer(mapView, shadowMaps.buffers[slot]);
      bgfx::setViewClear(mapView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.0f, 0, 3);
      bgfx::setViewTransform(mapView, nullptr, nullptr);
      bgfx::touch(mapView);
      const SbMatrix projection = bgfx::getCaps()->homogeneousDepth ? pass.projectionCoin :
        pass.projectionCoin * clipConversion;
      const float depthParams[4] = {pass.nearDistance, pass.vsmFarDistance,
        frame.shadowLights[pass.lightSlot].type == CoinRenderLightType::SPOT ? 1.0f : 0.0f, 0.0f};
      std::unordered_set<uint32_t> casterSlots(pass.casterDraws.begin(), pass.casterDraws.end());
      for (const CoinBgfxDraw & draw : plan.shadowDraws) {
        if (!casterSlots.count(draw.sourceDrawSlot)) continue;
        const auto & state = frame.renderStates[draw.renderStateSlot];
        const SbMatrix modelView = state.model *
          (state.shadowMapCameraOverride ? state.view : pass.view);
        const SbMatrix casterProjection = state.shadowMapCameraOverride ?
          (bgfx::getCaps()->homogeneousDepth ? state.projectionCoin :
           state.projectionCoin * clipConversion) : projection;
        SbMatrix viewportTransform;
        int32_t scissor[4];
        if (!coin_render_shadow_caster_viewport(frame, state, pass.mapSize, viewportTransform,
                                                scissor, this->lastError))
          return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
        if (!scissor[2] || !scissor[3])
          continue;
        bgfx::setScissor(uint16_t(scissor[0]),
                         uint16_t(int32_t(pass.mapSize) - scissor[1] - scissor[3]),
                         uint16_t(scissor[2]), uint16_t(scissor[3]));
        const SbMatrix mvp = modelView * casterProjection * viewportTransform;
        bgfx::setTransform(mvp.getValue());
        bgfx::setVertexBuffer(0, vb);
        bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
        bgfx::setUniform(this->shadowModelViewUniform, modelView.getValue());
        const SbMatrix clipModelView = state.model * state.view;
        bgfx::setUniform(this->shadowClipModelViewUniform, clipModelView.getValue());
        bgfx::setUniform(this->clipMetaUniform, draw.clipMeta);
        bgfx::setUniform(this->clipPlanesUniform, draw.clipPlanes, COIN_RENDER_MAX_CLIP_PLANES);
        bgfx::setUniform(this->shadowDepthUniform, depthParams);
        const float casterAlphaTest[4] = {static_cast<float>(state.alphaTestFunction),
          state.alphaTestReference, 0.0f, 0.0f};
        bgfx::setUniform(this->alphaTestUniform, casterAlphaTest);
        const float raster[4] = {0.0f, 0.0f, this->canonicalCoverage ? float(pass.mapSize) : 0.0f,
                                 this->canonicalCoverage ? float(pass.mapSize) : 0.0f};
        bgfx::setUniform(this->rasterViewportUniform, raster);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
          BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
        bgfx::submit(mapView, this->shadowMomentsProgram);
      }
      bgfx::setScissor();
    }
  }
  // Private floating sums never share an attachment with the scene: every
  // sample resolves first, then reads the preceding sum and writes the other.
  struct MultipassTargets {
    CoinBgfxCallback * callback;
    bgfx::FrameBufferHandle sums[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
    explicit MultipassTargets(CoinBgfxCallback * value) : callback(value) {}
    ~MultipassTargets() {
      if (callback && callback->failed()) return;
      for (auto handle : sums) if (bgfx::isValid(handle)) bgfx::destroy(handle);
    }
  } accumulation(static_cast<CoinBgfxCallback *>(this->callback.get()));
  if (frame.numPasses > 1) {
    for (unsigned int i = 0; i < 2; ++i) {
      accumulation.sums[i] = bgfx::createFrameBuffer(static_cast<uint16_t>(size[0]), static_cast<uint16_t>(size[1]),
        bgfx::TextureFormat::RGBA32F, BGFX_TEXTURE_RT |
          BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
          BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
      if (!bgfx::isValid(accumulation.sums[i])) {
        cleanupDirect();
        return {CoinRenderBackendStatus::OUT_OF_MEMORY,
          "BGFX multipass float attachment allocation failed"};
      }
    }
  }
  const bgfx::FrameBufferHandle sceneOutput = this->sampleCount > 1 ?
    this->msaaFrameBuffer : output;
  bgfx::ViewId opaqueView = this->viewBase + rasterViewOffset + static_cast<bgfx::ViewId>(shadowPlan.passes.size());
  if (this->shadowArrayActive) this->encodeShadowArray(shadowPlan,shadowMaps.buffers,opaqueView++);
  bgfx::ViewId nextView = opaqueView;
  for (int pass = 0; pass < frame.numPasses; ++pass) {
    opaqueView = nextView;
    std::vector<CoinBgfxDraw> jitteredDraws;
    if (frame.numPasses > 1) {
      jitteredDraws = plan.draws;
      for (CoinBgfxDraw & draw : jitteredDraws) {
        if (draw.multipassRasterFixed) continue;
        const auto & state = frame.renderStates[draw.renderStateSlot];
        const auto & viewport = frame.viewports[state.viewportSlot];
        SbMatrix shifted;
        CoinRenderMultipassCore::projection(state.projectionCoin, frame.numPasses, pass, viewport, shifted);
        const SbMatrix projection = CoinRenderTransformCore::projection(shifted, bgfx::getCaps()->homogeneousDepth);
        const SbMatrix mvp = state.model * state.view * projection *
          CoinRenderTransformCore::viewportTransform(viewport, size[0], size[1]);
        std::memcpy(draw.mvp, mvp.getValue(), sizeof(draw.mvp));
      }
      // Retain layer barriers even when this sample has no pixels from the
      // bitmap which originally established them. They submit no geometry.
      for (CoinBgfxDraw & draw : jitteredDraws)
        if (draw.multipassMask && !(draw.multipassMask & (1u << pass)) &&
            (draw.clearDepthBefore || draw.authoredScene)) draw.indexCount = 0;
      jitteredDraws.erase(std::remove_if(jitteredDraws.begin(), jitteredDraws.end(),
        [pass](const CoinBgfxDraw & draw) { return draw.multipassMask && !(draw.multipassMask & (1u << pass)) &&
          !draw.clearDepthBefore && !draw.authoredScene; }), jitteredDraws.end());
    }
    const auto & sampleDraws = frame.numPasses > 1 ? jitteredDraws : plan.draws;
  const uint8_t clearIndex = frame.passClearColors.empty() ? 0 : uint8_t(pass);
  const float * sampleClear = frame.passClearColors.empty() ? plan.clearColor : frame.passClearColors[pass].getValue();
  bgfx::setPaletteColor(clearIndex, sampleClear);
  bgfx::setViewName(opaqueView, "rtt_opaque");
  bgfx::setViewMode(opaqueView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(opaqueView, 0, 0, static_cast<uint16_t>(size[0]),
                    static_cast<uint16_t>(size[1]));
  bgfx::setViewFrameBuffer(opaqueView, sceneOutput);
  bgfx::setViewClear(opaqueView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.0f, 0, clearIndex);
  bgfx::touch(opaqueView);
  if (frame.stereoMode) {
    if (!this->encodeStereoViews(frame, sceneOutput, opaqueView, size[0], size[1])) {
      cleanupDirect();
      return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
    }
    ++opaqueView;
    bgfx::setViewName(opaqueView, "stereo_view_base");
    bgfx::setViewMode(opaqueView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(opaqueView, 0, 0, size[0], size[1]);
    bgfx::setViewFrameBuffer(opaqueView, sceneOutput);
    bgfx::setViewClear(opaqueView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(opaqueView, nullptr, nullptr);
  }
  const bgfx::TextureHandle surfaceSampleDepth = strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS && this->sampleCount > 1 ?
    bgfx::getTexture(transparency.layers[0], 2) : bgfx::TextureHandle BGFX_INVALID_HANDLE;
  if (std::any_of(sampleDraws.begin(), sampleDraws.end(), [](const CoinBgfxDraw & draw) { return draw.authoredScene; })) {
    ++opaqueView;
    this->encodeOverlayLayers(sampleDraws, vb, ib, sceneOutput, textures, opaqueView,
      size[0], size[1], frame, shadowPlan, shadowMaps.buffers, true, surfaceSampleDepth);
    bgfx::setViewName(opaqueView, "rtt_authored_scene");
    bgfx::setViewMode(opaqueView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(opaqueView, 0, 0, size[0], size[1]);
    bgfx::setViewFrameBuffer(opaqueView, sceneOutput);
    bgfx::setViewClear(opaqueView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(opaqueView, nullptr, nullptr);
    bgfx::touch(opaqueView);
  }
  for (const CoinBgfxDraw & draw : sampleDraws) {
    if (!draw.indexCount || !draw.isSceneLayer() || (draw.blend && draw.deferred)) continue;
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vb);
    bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
    bgfx::setState(drawState(draw) | (this->sampleCount > 1 ? BGFX_STATE_MSAA : 0));
    if (!setDrawScissor(draw, size[0], size[1])) continue;
    this->submitSurfaceDraw(opaqueView, draw, textures, frame, shadowPlan, shadowMaps.buffers,
      size[0], size[1], surfaceSampleDepth);
  }

  nextView = this->encodeDeferredRuns(sampleDraws,vb,ib,sceneOutput,textures,opaqueView+1,
    size[0],size[1],frame,shadowPlan,shadowMaps.buffers,strategy,transparency.layers,transparency.oit,surfaceSampleDepth);

  if (!this->encodeOverlayLayers(sampleDraws, vb, ib, sceneOutput, textures, nextView,
                                size[0], size[1], frame, shadowPlan, shadowMaps.buffers, false, surfaceSampleDepth)) {
    cleanupDirect();
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  if (this->sampleCount > 1)
    this->encodeMsaaResolve(nextView++, output, size[0], size[1], 1.0f);
    if (frame.numPasses > 1) {
      const bgfx::ViewId sumView = nextView++;
      bgfx::setViewName(sumView, "multipass_sum");
      bgfx::setViewMode(sumView, bgfx::ViewMode::Sequential);
      bgfx::setViewRect(sumView, 0, 0, size[0], size[1]);
      bgfx::setViewFrameBuffer(sumView, accumulation.sums[pass & 1]);
      bgfx::setViewClear(sumView, BGFX_CLEAR_NONE);
      bgfx::setViewTransform(sumView, nullptr, nullptr);
      const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
      const float info[4] = {1.0f / size[0], 1.0f / size[1],
        pass ? 1.0f : 0.0f, 1.0f / frame.numPasses};
      const uint32_t flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
      bgfx::setTransform(identity);
      bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
      bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
      bgfx::setUniform(this->multipassInfoUniform, info);
      bgfx::setTexture(0, this->multipassSceneSampler, bgfx::getTexture(output, 0), flags);
      // Bind a valid texture even when the branch does not consume a prior sum.
      bgfx::setTexture(1, this->multipassPreviousSampler,
        pass ? bgfx::getTexture(accumulation.sums[(pass - 1) & 1], 0) :
          bgfx::getTexture(output, 0), flags);
      bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
      bgfx::submit(sumView, this->multipassSumProgram);
    }
  }
  if (frame.numPasses > 1) {
    const bgfx::ViewId meanView = nextView++;
    bgfx::setViewName(meanView, "multipass_mean");
    bgfx::setViewMode(meanView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(meanView, 0, 0, size[0], size[1]);
    bgfx::setViewFrameBuffer(meanView, output);
    bgfx::setViewClear(meanView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(meanView, nullptr, nullptr);
    const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const float info[4] = {1.0f / size[0], 1.0f / size[1],
      0.0f, 1.0f / frame.numPasses};
    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
    bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
    bgfx::setUniform(this->multipassInfoUniform, info);
    bgfx::setTexture(0, this->multipassSceneSampler,
      bgfx::getTexture(accumulation.sums[(frame.numPasses - 1) & 1], 0),
      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
      BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(meanView, this->multipassMeanProgram);
  }
  bgfx::frame(BGFX_FRAME_FLUSH);
  if (manualMips) {
    auto runtimeStatus = this->checkRuntimeFailure("BGFX direct RTT base render failed");
    if (runtimeStatus != CoinRenderBackendStatus::SUCCESS) {
      cleanupDirect();
      return CoinRenderSubmitResult(runtimeStatus, this->lastError);
    }
    const bgfx::TextureHandle color = bgfx::getTexture(output, 0);
    int sourceWidth = size[0], sourceHeight = size[1];
    uint16_t level = 1;
    std::map<uint32_t, unsigned> mipFrames;
    std::map<unsigned, double> mipGpuMilliseconds;
    std::map<unsigned, double> mipFrameMilliseconds;
    const auto captureMipGpu = [&]() {
      const bgfx::Stats * stats = bgfx::getStats();
      if (!stats || stats->gpuTimerFreq <= 0) return;
      const double millisecondsPerTick = 1000.0 / double(stats->gpuTimerFreq);
      const auto frame = mipFrames.find(stats->gpuFrameNum);
      if (frame != mipFrames.end() && stats->gpuTimeEnd > stats->gpuTimeBegin)
        mipFrameMilliseconds.emplace(frame->second,
          double(stats->gpuTimeEnd - stats->gpuTimeBegin) * millisecondsPerTick);
      for (uint16_t index = 0; index < stats->numViews; ++index) {
        const bgfx::ViewStats & view = stats->viewStats[index];
        if (view.view != this->viewBase + 1) continue;
        // BGFX can reuse the view name before delayed timestamps arrive.
        // The submitted frame number identifies this reduction level.
        const auto found = mipFrames.find(view.gpuFrameNum);
        if (found == mipFrames.end() ||
            view.gpuTimeEnd <= view.gpuTimeBegin) continue;
        mipGpuMilliseconds.emplace(found->second,
          double(view.gpuTimeEnd - view.gpuTimeBegin) *
          millisecondsPerTick);
      }
    };
    if (traceNpotGpu) {
      // Isolate timer queries from the base scene and its shadow/OIT views.
      for (unsigned settle = 0; settle < 8; ++settle) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        bgfx::frame(BGFX_FRAME_FLUSH);
      }
    }
    while (sourceWidth > 1 || sourceHeight > 1) {
      const int destinationWidth = std::max(1, sourceWidth / 2);
      const int destinationHeight = std::max(1, sourceHeight / 2);
      // Fault after one completed level: a partially generated replacement
      // must never displace an older published texture or advance the serial.
      // A one-level chain instead fails before its only allocation.
      if ((level == 2 || (level == 1 && destinationWidth == 1 &&
                          destinationHeight == 1)) &&
          consumeTestFault("COIN_BGFX_TEST_NPOT_MIP_ALLOC_ONCE")) {
        cleanupDirect();
        return {CoinRenderBackendStatus::OUT_OF_MEMORY,
                "Injected BGFX NPOT mip allocation failure"};
      }
      const uint64_t scratchFlags = BGFX_TEXTURE_BLIT_DST |
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
      bgfx::TextureHandle scratch = bgfx::createTexture2D(
        static_cast<uint16_t>(sourceWidth), static_cast<uint16_t>(sourceHeight),
        false, 1, outputFormat, scratchFlags);
      bgfx::FrameBufferHandle mipBuffer = BGFX_INVALID_HANDLE;
      if (bgfx::isValid(scratch)) {
        bgfx::Attachment attachment;
        attachment.init(color, bgfx::Access::Write, 0, 1, level,
                        BGFX_ATTACHMENT_NONE);
        mipBuffer = bgfx::createFrameBuffer(1, &attachment, false);
      }
      if (!bgfx::isValid(scratch) || !bgfx::isValid(mipBuffer)) {
        if (bgfx::isValid(scratch)) bgfx::destroy(scratch);
        if (bgfx::isValid(mipBuffer)) bgfx::destroy(mipBuffer);
        cleanupDirect();
        return {CoinRenderBackendStatus::OUT_OF_MEMORY,
                "BGFX could not allocate NPOT mip GPU scratch or attachment"};
      }
      const bgfx::ViewId copyView = this->viewBase;
      const bgfx::ViewId reduceView = this->viewBase + 1;
      bgfx::resetView(copyView);
      bgfx::resetView(reduceView);
      bgfx::TextureRegion source, destination;
      source.handle = color;
      source.mip = static_cast<uint8_t>(level - 1);
      destination.handle = scratch;
      bgfx::blit(copyView, destination, source);
      bgfx::setViewName(reduceView, "rtt_npot_area_mip");
      bgfx::setViewMode(reduceView, bgfx::ViewMode::Sequential);
      bgfx::setViewRect(reduceView, 0, 0,
        static_cast<uint16_t>(destinationWidth),
        static_cast<uint16_t>(destinationHeight));
      bgfx::setViewFrameBuffer(reduceView, mipBuffer);
      bgfx::setViewClear(reduceView, BGFX_CLEAR_NONE);
      bgfx::setViewTransform(reduceView, nullptr, nullptr);
      const float extent[4] = {float(sourceWidth), float(sourceHeight),
                               float(destinationWidth), float(destinationHeight)};
      bgfx::setUniform(this->npotMipExtentUniform, extent);
      bgfx::setTexture(0, this->npotMipSampler, scratch);
      bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
      bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
      bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
      bgfx::submit(reduceView, this->npotMipProgram);
      const uint32_t mipFrame = bgfx::frame(BGFX_FRAME_FLUSH);
      if (traceNpotGpu) {
        mipFrames.emplace(mipFrame, unsigned(level));
        std::fprintf(stderr, "COIN_RENDER_PHASE rtt_npot_mip_frame level=%u frame=%u reduce_view=%u\n",
                     unsigned(level), mipFrame, unsigned(reduceView));
        captureMipGpu();
        // BGFX retains only the newest timer result per view. Resolve each
        // submitted reduction before reusing its view for another mip level.
        // This serialization belongs only to the opt-in GPU cost trace.
        for (unsigned drain = 0; drain < 16 &&
             (mipGpuMilliseconds.count(level) == 0 ||
              mipFrameMilliseconds.count(level) == 0); ++drain) {
          captureMipGpu();
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          bgfx::resetView(reduceView);
          bgfx::setViewName(reduceView, "rtt_npot_level_drain");
          bgfx::setViewFrameBuffer(reduceView, mipBuffer);
          bgfx::setViewRect(reduceView, 0, 0, 1, 1);
          bgfx::setViewClear(reduceView, BGFX_CLEAR_NONE);
          bgfx::touch(reduceView);
          bgfx::frame(BGFX_FRAME_FLUSH);
          captureMipGpu();
        }
      }
      runtimeStatus = this->checkRuntimeFailure("BGFX NPOT mip area reduction failed");
      bgfx::destroy(mipBuffer);
      bgfx::destroy(scratch);
      if (runtimeStatus != CoinRenderBackendStatus::SUCCESS) {
        cleanupDirect();
        return CoinRenderSubmitResult(runtimeStatus, this->lastError);
      }
      sourceWidth = destinationWidth;
      sourceHeight = destinationHeight;
      ++level;
    }
    for (int attempts = 0; traceNpotGpu &&
         (mipGpuMilliseconds.size() < mipFrames.size() ||
          mipFrameMilliseconds.size() < mipFrames.size()) && attempts < 8;
         ++attempts) {
      // Keep a view in each drain frame: BGFX exposes delayed per-view
      // timestamps through the current frame's view-stat array.
      bgfx::resetView(this->viewBase + 1);
      bgfx::setViewName(this->viewBase + 1, "rtt_npot_mip_drain");
      bgfx::setViewFrameBuffer(this->viewBase + 1, output);
      bgfx::setViewRect(this->viewBase + 1, 0, 0, 1, 1);
      bgfx::setViewClear(this->viewBase + 1, BGFX_CLEAR_NONE);
      bgfx::touch(this->viewBase + 1);
      bgfx::frame(BGFX_FRAME_FLUSH);
      captureMipGpu();
      runtimeStatus = this->checkRuntimeFailure("BGFX NPOT mip GPU timestamp drain failed");
      if (runtimeStatus != CoinRenderBackendStatus::SUCCESS) {
        cleanupDirect();
        return CoinRenderSubmitResult(runtimeStatus, this->lastError);
      }
    }
    if (traceNpotGpu) {
      double areaMs = 0.0, frameMs = 0.0;
      for (const auto & entry : mipGpuMilliseconds) areaMs += entry.second;
      for (const auto & entry : mipFrameMilliseconds) frameMs += entry.second;
      std::fprintf(stderr,
        "COIN_RENDER_PHASE rtt_npot_mips_gpu levels=%zu area_resolved=%zu area_ms=",
        mipFrames.size(), mipGpuMilliseconds.size());
      if (mipGpuMilliseconds.size() == mipFrames.size())
        std::fprintf(stderr, "%.6f", areaMs);
      else
        std::fprintf(stderr, "unavailable");
      std::fprintf(stderr, " frame_resolved=%zu frame_ms=",
                   mipFrameMilliseconds.size());
      if (mipFrameMilliseconds.size() == mipFrames.size())
        std::fprintf(stderr, "%.6f", frameMs);
      else
        std::fprintf(stderr, "unavailable");
      for (const auto & entry : mipGpuMilliseconds)
        std::fprintf(stderr, " level%u_ms=%.6f", entry.first, entry.second);
      std::fprintf(stderr, "\n");
    }
    bgfx::resetView(this->viewBase);
    bgfx::resetView(this->viewBase + 1);
  }
  for (size_t i = 0; i < textures.size(); ++i)
    if (owned[i] && bgfx::isValid(textures[i])) bgfx::destroy(textures[i]);
  if (bgfx::isValid(vb)) bgfx::destroy(vb);
  if (bgfx::isValid(ib)) bgfx::destroy(ib);
  if (!cachedResource) {
    DirectTextureResource resource;
    resource.token = ++this->directTextureSerial;
    resource.producerKey = producerKey;
    resource.width = size[0];
    resource.height = size[1];
    resource.format=frame.outputColorFormat; resource.mipmapped=frame.outputMipmaps;
    resource.frameBuffer = output;
    resource.depthSampleable = frame.viewOutput || strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS;
    resource.inUse = true;
    this->directTextures.push_back(resource);
    token = resource.token;
  } else {
    if (cachedResource->frameBuffer.idx != output.idx &&
        bgfx::isValid(cachedResource->frameBuffer))
      bgfx::destroy(cachedResource->frameBuffer);
    cachedResource->width = size[0];
    cachedResource->height = size[1];
    cachedResource->format=frame.outputColorFormat;cachedResource->mipmapped=frame.outputMipmaps;
    cachedResource->frameBuffer = output;
    if (newOutput)
      cachedResource->depthSampleable =
        frame.viewOutput || strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS;
    cachedResource->inUse = true;
    token = cachedResource->token;
  }
  if (frame.viewOutput && this->sampleCount > 1) {
    for (auto & view : this->directTextures) if (view.token == token) {
      if (bgfx::isValid(view.viewSamples)) bgfx::destroy(view.viewSamples);
      view.viewSamples = this->msaaFrameBuffer;
      this->msaaFrameBuffer = BGFX_INVALID_HANDLE;
      this->msaaWidth = this->msaaHeight = 0;
    }
  }
  logicalSubmitSucceeded();
  return CoinRenderSubmitResult(CoinRenderBackendStatus::SUCCESS, "", ++this->serial);
}

CoinRenderSubmitResult CoinBgfxBackend::assembleCubeRtt(const std::array<uint64_t,6> & faces,
    uint32_t n,bool mips,CoinRenderTargetP &,uint64_t & token)
{
  token=0;CoinRenderTextureImageSnapshot description;
  if(!this->initialized || !this->onApiThread())return {CoinRenderBackendStatus::NOT_READY,"Cube assembly needs the live BGFX API thread"};
  if(!CoinRenderCubeTextureCore::describe(n,mips,description))return {CoinRenderBackendStatus::UNSUPPORTED,"Invalid cube atlas layout"};
  const auto * caps=bgfx::getCaps();
  const uint64_t flags=BGFX_TEXTURE_RT|BGFX_TEXTURE_BLIT_DST|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|
    BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;
  if(!caps || !caps->limits.maxBlits ||
     description.width>caps->limits.maxTextureSize || description.height>caps->limits.maxTextureSize ||
     !bgfx::isTextureValid(1,false,1,bgfx::TextureFormat::RGBA8,flags))
    return {CoinRenderBackendStatus::UNSUPPORTED,"BGFX cube atlas clear/copy extent or format unavailable"};
  std::array<bgfx::TextureHandle,6> sources;
  for(size_t face=0;face<6;++face) {
    sources[face]=BGFX_INVALID_HANDLE;
    for(size_t previous=0;previous<face;++previous)if(faces[previous]==faces[face])
      return {CoinRenderBackendStatus::UNSUPPORTED,"Cube faces alias the same GPU resource"};
    for(const auto & resource:this->directTextures)
      if(resource.token==faces[face] && resource.inUse && !resource.cubeFaceSize &&
         resource.width==int(n) && resource.height==int(n) && resource.format==CoinRenderTextureFormat::RGBA8_LINEAR &&
         (!mips||resource.mipmapped))sources[face]=bgfx::getTexture(resource.frameBuffer,0);
    if(!bgfx::isValid(sources[face]))return {CoinRenderBackendStatus::UNSUPPORTED,"Cube face token is stale, mismatched or from another owner"};
  }
  const auto color=bgfx::createTexture2D(uint16_t(description.width),uint16_t(description.height),false,1,bgfx::TextureFormat::RGBA8,flags);
  bgfx::FrameBufferHandle output=BGFX_INVALID_HANDLE;
  if(bgfx::isValid(color))output=bgfx::createFrameBuffer(1,&color,true);
  if(!bgfx::isValid(output)) {
    if(bgfx::isValid(color))bgfx::destroy(color);
    return {CoinRenderBackendStatus::OUT_OF_MEMORY,"Cannot allocate cube atlas candidate"};
  }
  // Clear in its own ordered resource frame: BGFX executes blits before the
  // draws/clear of a view, so a same-view clear would erase copied face data.
  bgfx::resetView(this->viewBase);bgfx::setViewFrameBuffer(this->viewBase,output);
  bgfx::setViewRect(this->viewBase,0,0,uint16_t(description.width),uint16_t(description.height));
  bgfx::setViewClear(this->viewBase,BGFX_CLEAR_COLOR,0);bgfx::touch(this->viewBase);bgfx::frame(BGFX_FRAME_FLUSH);
  bgfx::resetView(this->viewBase);
  uint64_t levelOffset=0;uint32_t extent=n,level=0,blits=0;
  for(;;) {
    for(uint32_t face=0;face<6;++face)for(uint32_t y=0;y<extent;++y) {
      uint32_t x=0;
      while(x<extent) {
        const uint64_t linear=levelOffset+uint64_t(face)*extent*extent+uint64_t(y)*extent+x;
        const uint32_t dx=uint32_t(linear%description.width),dy=uint32_t(linear/description.width);
        const uint32_t count=std::min(extent-x,description.width-dx);
        const uint32_t sy=caps->originBottomLeft?y:extent-1-y;
        bgfx::TextureRegion destination,source;
        destination.init(color,uint16_t(dx),uint16_t(dy),uint16_t(count),1);
        destination.depth=1;
        source.init(sources[face],uint16_t(x),uint16_t(sy),uint16_t(count),1);
        source.mip=uint8_t(level);source.depth=1;
        bgfx::blit(this->viewBase,destination,source);
        x+=count;
        if(++blits==caps->limits.maxBlits){bgfx::frame(BGFX_FRAME_FLUSH);blits=0;}
      }
    }
    levelOffset+=6*uint64_t(extent)*extent;
    if(!mips||extent==1)break;extent/=2;++level;
  }
  if(blits)bgfx::frame(BGFX_FRAME_FLUSH);
  bgfx::frame(BGFX_FRAME_FLUSH);
  const auto status=this->checkRuntimeFailure("BGFX cube atlas assembly failed");
  if(status!=CoinRenderBackendStatus::SUCCESS){bgfx::destroy(output);return {status,this->lastError};}
  DirectTextureResource resource;resource.token=++this->directTextureSerial;
  resource.width=description.width;resource.height=description.height;resource.frameBuffer=output;
  resource.cubeFaceSize=n;resource.cubeMipmapped=mips;resource.inUse=true;
  this->directTextures.push_back(resource);token=resource.token;
  return {};
}

void
CoinBgfxBackend::retireRtt(const std::vector<uint64_t> & tokens)
{
  if (!this->initialized || !this->onApiThread()) return;
  if (this->checkRuntimeFailure("BGFX shared renderer failed before candidate retirement") !=
      CoinRenderBackendStatus::SUCCESS) return;
  for (auto it=this->directTextures.begin();it!=this->directTextures.end();) {
    if (std::find(tokens.begin(),tokens.end(),it->token)==tokens.end()) { ++it; continue; }
    // bgfx::destroy queues ordered destruction and defers handle reuse. The
    // framebuffer owns its attachments; no wait/frame or other token changes.
    if (bgfx::isValid(it->frameBuffer)) bgfx::destroy(it->frameBuffer);
    if (bgfx::isValid(it->viewSamples)) bgfx::destroy(it->viewSamples);
    it=this->directTextures.erase(it);
  }
}

void
CoinBgfxBackend::releaseDirectTexture(uint64_t token)
{
  for (std::vector<DirectTextureResource>::iterator it = this->directTextures.begin();
       it != this->directTextures.end(); ++it) {
    if (it->token != token) continue;
    it->inUse = false;
    return;
  }
}

void
CoinBgfxBackend::finishDirectTextures(const std::vector<uint64_t> & usedTokens)
{
  if (!this->initialized || !this->onApiThread()) return;
  // finishRtt ends the whole transaction, including STAGED rollback before
  // the root submit. Subset retirement must keep the graph's pins intact.
  this->releasePortablePreflightPins();
  if (this->checkRuntimeFailure("BGFX shared renderer failed before RTT retirement") !=
      CoinRenderBackendStatus::SUCCESS) return;
  for (std::vector<DirectTextureResource>::iterator it = this->directTextures.begin();
       it != this->directTextures.end();) {
    if (std::find(usedTokens.begin(), usedTokens.end(), it->token) == usedTokens.end()) {
      if (bgfx::isValid(it->frameBuffer)) bgfx::destroy(it->frameBuffer);
    if (bgfx::isValid(it->viewSamples)) bgfx::destroy(it->viewSamples);
      it = this->directTextures.erase(it);
    } else {
      it->inUse = false;
      ++it;
    }
  }
}
