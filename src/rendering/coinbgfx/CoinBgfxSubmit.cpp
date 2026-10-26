// CoinRender BGFX Submit responsibilities.
#include "rendering/coinbgfx/CoinBgfxBackendInternal.h"

using namespace coin_bgfx_internal;

CoinRenderSubmitResult
CoinBgfxBackend::submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target)
{
  return this->submit(frame, target, CoinRenderFrameReuseDecision());
}

CoinRenderSubmitResult
CoinBgfxBackend::submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
                          const CoinRenderFrameReuseDecision & reuse)
{
  return this->submitInternal(frame, target, reuse, nullptr);
}

CoinRenderSubmitResult
CoinBgfxBackend::submitAsync(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
  CoinRenderReadbackTicket & ticket, const CoinRenderFrameReuseDecision & reuse)
{
  ticket = CoinRenderReadbackTicket{};
  return this->submitInternal(frame, target, reuse, &ticket);
}

CoinRenderSubmitResult
CoinBgfxBackend::submitInternal(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
  const CoinRenderFrameReuseDecision & reuse, CoinRenderReadbackTicket * outTicket)
{
  if (!this->initialized || this->status != CoinRenderBackendStatus::SUCCESS || !this->onApiThread()) {
    this->lastError = "BGFX submission requires a prepared backend on its API thread";
    return CoinRenderSubmitResult(CoinRenderBackendStatus::NOT_READY, this->lastError);
  }
  if (!CoinRenderCubeTextureCore::validate(frame, this->lastError) ||
      !CoinRenderTextureSamplingCore::validVolumeFrame(frame, this->lastError))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  PortablePreflightRelease releasePreflight{this};
  for(const auto & image:frame.textures)if(image.cubeFaceSize && image.gpuToken) {
    if(!CoinRenderCubeTextureCore::valid(image))return {CoinRenderBackendStatus::UNSUPPORTED,"Invalid GPU cube atlas metadata"};
    const auto found=std::find_if(this->directTextures.begin(),this->directTextures.end(),[&image](const DirectTextureResource & resource) {
      return resource.token==image.gpuToken && resource.inUse && resource.width==int(image.width) &&
        resource.height==int(image.height) && resource.cubeFaceSize==image.cubeFaceSize &&
        resource.cubeMipmapped==image.cubeMipmapped && resource.format==image.format;
    });
    if(found==this->directTextures.end())return {CoinRenderBackendStatus::UNSUPPORTED,"GPU cube token belongs to another owner/generation or has mismatched metadata"};
  }
  const CoinRenderBackendStatus initialRuntimeStatus = this->checkRuntimeFailure("BGFX shared renderer failed before submission");
  if (initialRuntimeStatus != CoinRenderBackendStatus::SUCCESS)
    return CoinRenderSubmitResult(initialRuntimeStatus, this->lastError);
  if(COIN_BGFX_TEST_OUT_OF_MEMORY()) {
    this->lastError="Injected BGFX out of memory after successful logical submissions";
    return {CoinRenderBackendStatus::OUT_OF_MEMORY,this->lastError};
  }
  if ((target.kind == CoinRenderTargetP::KIND_WINDOW) != this->presentToWindow ||
      target.directTextureOutput) {
    this->lastError = "BGFX supports native window presentation or offscreen readback";
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  const auto multipassAdmission = this->preflightMultipass(frame,target.size);
  if (multipassAdmission.status != CoinRenderBackendStatus::SUCCESS) return multipassAdmission;
  if (frame.stereoMode && !this->prepareStereoPrograms())
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  this->managerDepthMode = this->managerDepthMode || frame.managerFrame;
  const bool needPrivateWindow = frame.numPasses > 1 || this->managerDepthMode;
  const bool releaseMultipassWindow = this->presentToWindow && this->multipassSceneRequired && !needPrivateWindow;
  this->multipassSceneRequired = needPrivateWindow;
  const auto textureAdmission = bgfxTextureProfile(frame);
  if (textureAdmission.status != CoinRenderBackendStatus::SUCCESS) return textureAdmission;
  const auto msaaAdmission = this->msaaProfile(frame);
  if (msaaAdmission.status != CoinRenderBackendStatus::SUCCESS) return msaaAdmission;
  if (releaseMultipassWindow) {
    const bgfx::FrameBufferHandle nativeOutput = this->frameBuffer;
    this->frameBuffer = BGFX_INVALID_HANDLE;
    this->destroyFrameBuffers();
    this->frameBuffer = nativeOutput;
  }
  if (std::any_of(frame.renderStates.begin(), frame.renderStates.end(), [](const CoinRenderRenderStateSnapshot & state) {
        return state.rasterTextureSourceSlot != UINT32_MAX;
      }) && !this->prepareRasterImageResources())
    return {CoinRenderBackendStatus::BACKEND_ERROR, this->lastError};
  const bool hasShadows = !frame.shadowGroups.empty();
  CoinRenderShadowPlan shadowPlan;
  if (hasShadows) {
    if (outTicket || target.directTextureOutput) {
      this->lastError = "BGFX opaque shadows require synchronous output";
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    }
    if (!coin_render_plan_shadows(frame, shadowPlan, this->lastError) ||
        !coin_render_shadow_object_profile(frame, shadowPlan,
          shadowPlan.passes.size(), this->lastError))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    if (!bgfxShadowBatchSupported(frame, shadowPlan, this->lastError))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    const bgfx::Caps * caps = bgfx::getCaps();
    if (caps && caps->limits.maxTextureSamplers < (bgfxShadowMultitexture(frame)?11:shadowPlan.passes.size() > 4 ? 16 : shadowPlan.passes.size() > 2 ? 12 : 10)) {
      this->lastError = "BGFX shadow receiver needs one texture stage per shadow pass";
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    }
    for (const auto & pass : shadowPlan.passes) {
      if (pass.mapSize > caps->limits.maxTextureSize ||
          !(caps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
          !(caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
          !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA32F, shadowTextureFlags)) {
        this->lastError = "BGFX cannot render RGBA32F/D32F shadow maps at the planned size";
        return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
      }
    }
    if (bgfxShadowMultitexture(frame) && !coin_render_shadow_array_budget(shadowPlan,2,this->lastError))
      return {CoinRenderBackendStatus::UNSUPPORTED,this->lastError};
    if (!this->prepareShadowPrograms(shadowPlan.passes.size(),
                                     !bgfxShadowMultitexture(frame) && this->sampleCount == 1 &&
              bgfxFrameProgramNeeds(frame).ordinary))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  }
  if (outTicket) {
    auto & runtime = sharedRuntime();
    std::lock_guard<std::mutex> guard(runtime.mutex);
    uint64_t pendingBytes = 0;
    for (const auto & entry : asyncEntries())
      pendingBytes += entry.second->ticket.colorBytes + entry.second->ticket.depthBytes;
    const uint64_t requested = uint64_t(target.size[0]) * target.size[1] * (target.depthReadbackEnabled ? 8 : 4);
    if (this->presentToWindow ||
        !coin_render_readback_admitted(asyncEntries().size(), pendingBytes, requested))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::NOT_READY,
        "BGFX async readback requires offscreen output, at most sixteen tickets and 128 MiB of pending payloads");
  }
  if (!outTicket && this->readbackDepthEnabled != target.depthReadbackEnabled) {
    for (auto & slot : this->readbackSlots) {
      if (slot.pending) {
        uint32_t completed = bgfx::frame();
        while (static_cast<int32_t>(completed - slot.readyFrame) < 0) completed = bgfx::frame();
      }
      slot.pending = false;
    }
    this->lastPublishedReadback.clear(); this->lastPublishedDepth.clear();
    this->lastPublishedSequence = 0;
    this->readbackDepthEnabled = target.depthReadbackEnabled;
  }
  typedef std::chrono::steady_clock Clock;
  const Clock::time_point begin = Clock::now();
  const bool tracePhases = CoinRenderDiagnosticShell::phaseTracingEnabled();
  const char * gpuTimestampFlag = std::getenv("COIN_WGPU_GPU_TIMESTAMPS");
  const bool traceGpu = tracePhases && gpuTimestampFlag != nullptr &&
                        std::strcmp(gpuTimestampFlag, "1") == 0;
  if (traceGpu) bgfx::setDebug(BGFX_DEBUG_PROFILER);
  const bool homogeneousDepth = bgfx::getCaps()->homogeneousDepth;
  const bgfx::Caps * caps = bgfx::getCaps();
  const char * disableInstancing = std::getenv("COIN_BGFX_DISABLE_INSTANCING");
  // Recent BGFX versions expose this through maxInstanceData; older versions
  // additionally require the explicit capability bit.
#ifdef BGFX_CAPS_INSTANCING
  const bool instanceCaps = (caps->supported & BGFX_CAPS_INSTANCING) != 0;
#else
  const bool instanceCaps = true;
#endif
  const bool instancingEnabled = frame.numPasses == 1 && !hasShadows && this->drawBatchingEnabled &&
    !(disableInstancing && std::strcmp(disableInstancing, "1") == 0) &&
    instanceCaps &&
    caps->limits.maxInstanceData >= 10 && caps->limits.maxVertexAttributes >= 12;
  const bool cacheDimensionsMatch =
    this->cachedPassCount == frame.numPasses && this->cachedPassBudget == frame.multipassBufferBudget &&
    target.size[0] == this->cachedWidth &&
    target.size[1] == this->cachedHeight &&
    homogeneousDepth == this->cachedHomogeneousDepth;
  const bool activeBump=std::any_of(frame.bumpCoords.begin(),frame.bumpCoords.end(),[](const CoinRenderBumpCoordOverride & b){return b.imageSlot!=0;});
  const bool cachedBump=std::any_of(this->cachedPlan.draws.begin(),this->cachedPlan.draws.end(),[](const CoinBgfxDraw & draw){return draw.bumpSlot!=0;});
  const bool bumpPayloadHit=activeBump && cachedBump && this->bumpReuseFrame &&
    frame.hasSamePayload(*this->bumpReuseFrame) && bgfx::isValid(this->cachedVertexBuffer) &&
    bgfx::isValid(this->cachedIndexBuffer);
  const bool portableFrame = !frame.portableShaders.empty() || std::any_of(this->cachedPlan.draws.begin(), this->cachedPlan.draws.end(),
    [](const CoinBgfxDraw &draw) { return draw.portableShaderSlot != 0; });
  const bool sourceTexturesMatch = bgfxSameSourceTextures(frame, this->cachedPlan);
  // Public Wiring supplies a Core payload comparison. Raw private ingress
  // with only a repeated revision must repack textured vertex/sampler state.
  const bool texturedReuse = bumpPayloadHit ||
    (frame.textures.empty() && this->cachedPlan.textures.empty()) ||
    (reuse.kind == CoinRenderFrameReuseKind::REUSE && reuse.baseRevision == this->cachedRevision);
  const bool cacheHit = frame.numPasses == 1 && !portableFrame && !hasShadows && cacheDimensionsMatch &&
    sourceTexturesMatch && texturedReuse &&
    (bumpPayloadHit || (!activeBump && !cachedBump && frame.revision!=0 && frame.revision==this->cachedRevision)) &&
    (!this->cachedPlan.usesInstancing ||
      (instancingEnabled && bgfx::isValid(this->cachedInstanceBuffer)));
  // Invalidate before any changed GPU payload can displace the old anchor.
  if(!cacheHit)this->bumpReuseFrame.reset();
  bool bumpCacheCandidate=false;
  const auto rememberBump=[&]() {
    if(!activeBump || !bumpCacheCandidate) {this->bumpReuseFrame.reset();return;}
    this->cachedRevision=frame.revision;
    if(this->bumpReuseFrame || !bgfxBumpSnapshotFits(frame))return;
    try { this->bumpReuseFrame.reset(new CoinRenderFramePlan(frame)); }
    catch(const std::bad_alloc &) { this->bumpReuseFrame.reset(); }
  };
  const char * disableInstancedCameraPatch = std::getenv("COIN_BGFX_DISABLE_INSTANCED_CAMERA_PATCH");
  const bool instancedCameraPatchEnabled = instancingEnabled &&
    !(disableInstancedCameraPatch && std::strcmp(disableInstancedCameraPatch, "1") == 0) &&
    bgfx::isValid(this->cachedInstanceBuffer);
  const bool cameraPatchEligible = frame.numPasses == 1 &&
    !portableFrame && !activeBump && !cachedBump && !hasShadows && this->cameraPatchEnabled &&
    sourceTexturesMatch &&
    (!this->cachedPlan.usesInstancing || instancedCameraPatchEnabled) &&
    reuse.kind == CoinRenderFrameReuseKind::CAMERA_PATCH &&
    reuse.baseRevision != 0 && reuse.baseRevision == this->cachedRevision &&
    frame.revision != 0 && frame.revision != this->cachedRevision &&
    cacheDimensionsMatch &&
    (this->cachedPlan.draws.empty() ||
      (bgfx::isValid(this->cachedVertexBuffer) &&
       bgfx::isValid(this->cachedIndexBuffer)));
  std::vector<CoinBgfxDraw> cameraDraws;
  std::vector<CoinBgfxInstance> cameraInstances;
  const bool cameraPatchUsed = cameraPatchEligible &&
    CoinBgfxLowering::patchCamera(frame, target.size[0], target.size[1],
                                homogeneousDepth, this->cachedPlan,
                                cameraDraws, this->lastError, &cameraInstances);
  if (tracePhases && cameraPatchEligible && this->cachedPlan.usesInstancing && !cameraPatchUsed)
    std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_instanced_camera_patch_declined reason=%s\n", this->lastError.c_str());
  CoinBgfxPlan freshPlan;
  const CoinBgfxPlan * plan = &this->cachedPlan;
  if (!cacheHit && !cameraPatchUsed) {
    const char * disableCompact = std::getenv("COIN_BGFX_DISABLE_COMPACT_VERTICES");
    const bool compact = bgfx::getCaps()->rendererType == bgfx::RendererType::OpenGL &&
      !(disableCompact && std::strcmp(disableCompact, "1") == 0);
    const bool instanced = !activeBump && instancingEnabled && CoinBgfxLowering::lowerInstanced(
      frame, target.size[0], target.size[1], homogeneousDepth, freshPlan,
      this->lastError, target.submissionPreflight(frame),
      // Match the general path's composed model*view before vertex evaluation.
      // Split model/view transforms move boundary coverage on OpenGL/NVIDIA
      // and Vulkan/AMD. Keep instancing, with instance-buffer camera patches.
      false);
    if (!instanced && CoinRenderDiagnosticShell::phaseTracingEnabled())
      std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_instancing_declined enabled=%d max_instance_data=%u max_vertex_attributes=%u reason=%s\n",
        instancingEnabled ? 1 : 0, static_cast<unsigned int>(caps->limits.maxInstanceData),
        static_cast<unsigned int>(caps->limits.maxVertexAttributes), this->lastError.c_str());
    if (!instanced && !CoinBgfxLowering::lower(frame, target.size[0], target.size[1],
                              homogeneousDepth, freshPlan, this->lastError, hasShadows,
                              this->drawBatchingEnabled && frame.numPasses == 1, target.submissionPreflight(frame), compact)) {
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    }
    bgfxSplitSamplerViews(freshPlan);
    plan = &freshPlan;
  }
  std::vector<CoinBgfxVertexRange> materialRanges;
  const bool materialPatchEligible = frame.numPasses == 1 && !portableFrame && !activeBump && !cachedBump && !cacheHit && !cameraPatchUsed &&
    !this->cachedPlan.usesInstancing && !freshPlan.usesInstancing &&
    reuse.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD &&
    reuse.baseRevision != 0 && reuse.baseRevision == this->cachedRevision &&
    cacheDimensionsMatch && bgfx::isValid(this->cachedVertexBuffer) &&
    bgfx::isValid(this->cachedIndexBuffer);
  const bool materialPatchUsed = materialPatchEligible &&
    CoinBgfxLowering::materialPatchRanges(this->cachedPlan, freshPlan,
                                        materialRanges);
  const std::vector<CoinBgfxDraw> & strategyDraws =
    cameraPatchUsed ? cameraDraws : plan->draws;
  CoinBgfxTransparencyStrategy selectedStrategy;
  if (!CoinBgfxLowering::selectTransparencyStrategy(strategyDraws,
        this->transparencyMode, this->weightedOitSupported,
        this->sortedLayersSupported, selectedStrategy, this->lastError)) {
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  const bool rebuildSceneTarget = selectedStrategy != this->activeTransparencyStrategy ||
    (selectedStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
     this->peelPassCount != frame.transparency.layers) ||
    this->width != target.size[0] || this->height != target.size[1];
  const bool presentSceneTarget = this->presentToWindow && this->multipassSceneRequired ? true : rebuildSceneTarget ?
    (this->presentToWindow &&
     (bgfx::isValid(this->frameBuffer) ||
      (this->sampleCount > 1 && caps->rendererType == bgfx::RendererType::OpenGL)) &&
     (caps->rendererType == bgfx::RendererType::OpenGL ||
      selectedStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS)) :
    bgfx::isValid(this->windowSceneFrameBuffer);
  const uint32_t outputViews = this->presentToWindow ? (presentSceneTarget ? 1 : 0) :
    (target.depthReadbackEnabled ? 2 : 1);
  uint32_t requiredViews = 0;
  if (!bgfxCompositionViewBudget(frame, strategyDraws, selectedStrategy,
        shadowPlan.passes.size(), false, target.size[0], target.size[1],
        outputViews, this->sampleCount > 1, this->lastError, &requiredViews) || !this->reserveViews(requiredViews))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  if (frame.numPasses > 1 && !this->prepareMultipassPrograms())
    return {CoinRenderBackendStatus::BACKEND_ERROR,this->lastError};
  if (!this->prepareTransparencyPrograms(selectedStrategy, bgfxNeedsFixedSurfacePrograms(strategyDraws)))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  if (!this->prepareBaseProgram(strategyDraws, hasShadows, selectedStrategy, plan->usesInstancing,
                                this->presentToWindow && frame.numPasses == 1, coinBgfxTextureFormat(frame.outputColorFormat)))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  if (!this->preparePortablePrograms(frame, strategyDraws, hasShadows, selectedStrategy,
        this->presentToWindow && frame.numPasses == 1, coinBgfxTextureFormat(frame.outputColorFormat)))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  if (hasShadows && !bgfxShadowMultitexture(frame) && selectedStrategy != CoinBgfxTransparencyStrategy::OBJECT &&
      !this->prepareShadowTransparencyPrograms())
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  CoinRenderTransparencyOptions allocationOptions = frame.transparency;
  if (selectedStrategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT)
    allocationOptions.layers = 1;
  uint64_t requiredBytes = 0;
  if (!coin_render_transparency_sample_budget(target.size[0], target.size[1], allocationOptions,
                                       selectedStrategy != CoinBgfxTransparencyStrategy::OBJECT,
                                       this->sampleCount, requiredBytes, this->lastError))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  if (selectedStrategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT && this->sampleCount > 1 &&
      !bgfxWeightedSamples(this->sampleCount, this->layout, this->viewBase, this->lastError))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  if (selectedStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS && this->sampleCount > 1 &&
      !bgfxPeelingSamples(this->sampleCount, this->layout, this->viewBase, this->lastError))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  if (selectedStrategy != this->activeTransparencyStrategy ||
      (selectedStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
       this->peelPassCount != frame.transparency.layers)) {
    // Keep the native surface when opaque-only frames choose OBJECT or later
    // frames require OIT/peeling. BGFX creates resources before retiring old
    // ones: recreating a swapchain on the same X window here would race its
    // old EGL surface and fail with EGL_BAD_ALLOC.
    this->destroyFrameBuffers(true);
    this->activeTransparencyStrategy = selectedStrategy;
    this->peelPassCount = frame.transparency.layers;
  }
  const Clock::time_point lowered = Clock::now();
  if (!this->resize(target.size[0], target.size[1], frame.numPasses > 1)) {
    const CoinRenderBackendStatus resizeStatus = this->status == CoinRenderBackendStatus::SUCCESS ?
      CoinRenderBackendStatus::OUT_OF_MEMORY : this->status;
    return CoinRenderSubmitResult(resizeStatus, this->lastError);
  }
  if (!this->presentToWindow && target.depthReadbackEnabled && !this->prepareDepthReadbackResources(!outTicket))
    return CoinRenderSubmitResult(this->status, this->lastError);
  const auto msaaPreparation = this->prepareMsaa(this->width, this->height);
  if (msaaPreparation.status != CoinRenderBackendStatus::SUCCESS) return msaaPreparation;
  if (this->presentToWindow && caps->rendererType == bgfx::RendererType::OpenGL) {
    // Prepare the image program before the first dispatch, alongside all
    // surface/shadow/compositor programs prepared above. This boundary must
    // precede geometry, texture-copy, clear and image-prepass encoding.
    const bool inheritedImages = std::any_of(frame.renderStates.begin(), frame.renderStates.end(),
      [](const CoinRenderRenderStateSnapshot & state) {
        return state.rasterTextureSourceSlot != UINT32_MAX;
      });
    if (inheritedImages && !bgfx::isValid(this->rasterImageProgram))
      this->rasterImageProgram = createRasterImageProgram();
    if (inheritedImages && !bgfx::isValid(this->rasterImageProgram))
      return {CoinRenderBackendStatus::BACKEND_ERROR, "BGFX image prepass program allocation failed"};
    auto & runtime = sharedRuntime();
    if (runtime.programCommandsMaterialized != runtime.programCommandsQueued) {
      // Execute resource commands without presenting or encoding a scene.
      // The final frame() below waits for this resource frame to finish before
      // it queues the one scene submission. Thus cold shader compilation is
      // charged to apply(), rather than continuing after SUCCESS and delaying
      // the first physical frame (or timing out a requested screenshot).
      bgfx::frame(BGFX_FRAME_FLUSH);
      runtime.programCommandsMaterialized = runtime.programCommandsQueued;
      const auto resourceStatus = this->checkRuntimeFailure("BGFX program preparation failed");
      if (resourceStatus != CoinRenderBackendStatus::SUCCESS)
        return {resourceStatus, this->lastError};
      if (tracePhases)
        std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_program_resource_frame programs=%llu scene_draws=0\n",
          static_cast<unsigned long long>(runtime.programCommandsMaterialized));
    }
  }
  if (plan->vertices.size() > std::numeric_limits<uint32_t>::max() / sizeof(CoinBgfxVertex) ||
      plan->packedVertices.size() > std::numeric_limits<uint32_t>::max() / sizeof(CoinBgfxVertexPrefix) ||
      plan->instancedVertices.size() > std::numeric_limits<uint32_t>::max() / sizeof(CoinBgfxInstancedVertex) ||
      plan->instances.size() > std::numeric_limits<uint32_t>::max() / sizeof(CoinBgfxInstance) ||
      plan->indices.size() > std::numeric_limits<uint32_t>::max() / sizeof(uint32_t)) {
    this->lastError = "BGFX geometry exceeds buffer size limits";
    return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, this->lastError);
  }
  bgfx::DynamicVertexBufferHandle vb = this->cachedVertexBuffer;
  bgfx::DynamicIndexBufferHandle ib = this->cachedIndexBuffer;
  bgfx::DynamicVertexBufferHandle instanceBuffer = this->cachedInstanceBuffer;
  const bool bumpTextureReuse=activeBump && cachedBump &&
    this->cachedTextures.size()==plan->textures.size() &&
    std::all_of(this->cachedTextures.begin(),this->cachedTextures.end(),[](bgfx::TextureHandle t){return bgfx::isValid(t);}) &&
    bgfxSameBumpTextures(this->cachedPlan.textures,plan->textures);
  bool retained = cacheHit || cameraPatchUsed || materialPatchUsed;
  bool geometryBufferReused = retained;
  uint32_t materialPatchVertices = 0;
  if (materialPatchUsed) {
    for (const CoinBgfxVertexRange & range : materialRanges) {
      materialPatchVertices += range.count;
      bgfx::update(vb, range.first, bgfx::copy(
        freshPlan.vertices.data() + range.first,
        range.count * static_cast<uint32_t>(sizeof(CoinBgfxVertex))));
    }
    this->cachedPlan = std::move(freshPlan);
    this->cachedRevision = frame.revision;
    this->cachedPassCount = frame.numPasses;
    this->cachedPassBudget = frame.multipassBufferBudget;
    this->cachedWidth = target.size[0];
    this->cachedHeight = target.size[1];
    this->cachedHomogeneousDepth = homogeneousDepth;
    plan = &this->cachedPlan;
  }
  if (cameraPatchUsed && !cameraInstances.empty())
    bgfx::update(instanceBuffer, 0, bgfx::copy(cameraInstances.data(),
      static_cast<uint32_t>(cameraInstances.size() * sizeof(CoinBgfxInstance))));
  if (!retained) {
    if (plan->vertexCount() && !plan->indices.empty()) {
      const bool vertexPoolHit = bgfx::isValid(vb) &&
        this->cachedCompactVertices == plan->usesCompactVertices &&
        this->cachedInstancedVertices == plan->usesInstancing &&
        this->cachedVertexCapacity >= plan->vertexCount();
      const bool indexPoolHit = bgfx::isValid(ib) &&
        this->cachedIndexCapacity >= plan->indices.size();
      const bool instancePoolHit = !plan->usesInstancing ||
        (bgfx::isValid(instanceBuffer) && this->cachedInstanceCapacity >= plan->instances.size());
      geometryBufferReused = vertexPoolHit && indexPoolHit && instancePoolHit;
      if (!vertexPoolHit) {
        if (bgfx::isValid(vb)) bgfx::destroy(vb);
        this->cachedVertexCapacity = pooledCapacity(plan->vertexCount());
        vb = bgfx::createDynamicVertexBuffer(this->cachedVertexCapacity,
          plan->usesInstancing ? this->instancedLayout :
            plan->usesCompactVertices ? this->compactLayout : this->layout);
        this->cachedVertexBuffer = vb;
        this->cachedCompactVertices = plan->usesCompactVertices;
        this->cachedInstancedVertices = plan->usesInstancing;
      }
      if (!indexPoolHit) {
        if (bgfx::isValid(ib)) bgfx::destroy(ib);
        this->cachedIndexCapacity = pooledCapacity(plan->indices.size());
        ib = bgfx::createDynamicIndexBuffer(this->cachedIndexCapacity,
                                             BGFX_BUFFER_INDEX32);
        this->cachedIndexBuffer = ib;
      }
      if (!instancePoolHit) {
        if (bgfx::isValid(instanceBuffer)) bgfx::destroy(instanceBuffer);
        this->cachedInstanceCapacity = pooledCapacity(plan->instances.size());
        instanceBuffer = bgfx::createDynamicVertexBuffer(this->cachedInstanceCapacity, this->instanceLayout);
        this->cachedInstanceBuffer = instanceBuffer;
      }
      if (!bgfx::isValid(vb) || !bgfx::isValid(ib) ||
          (plan->usesInstancing && !bgfx::isValid(instanceBuffer))) {
        this->lastError = "BGFX geometry upload failed";
        return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, this->lastError);
      }
      bgfx::update(vb, 0, uploadVertices(freshPlan));
      bgfx::update(ib, 0, bgfx::copy(plan->indices.data(),
        static_cast<uint32_t>(plan->indices.size() * sizeof(uint32_t))));
      if (plan->usesInstancing)
        bgfx::update(instanceBuffer, 0, bgfx::copy(plan->instances.data(),
          static_cast<uint32_t>(plan->instances.size() * sizeof(CoinBgfxInstance))));
    }
    if(!bumpTextureReuse) {
      for (bgfx::TextureHandle texture : this->cachedTextures)
        if (bgfx::isValid(texture)) bgfx::destroy(texture);
      this->cachedTextures.clear();
    }
    this->cachedRevision = 0;
    if ((frame.revision != 0 || activeBump) && CoinBgfxLowering::retainForReuse(freshPlan,
          32u * 1024u * 1024u, 128u * 1024u * 1024u, true)) {
      this->cachedPlan = std::move(freshPlan);
      this->cachedRevision = frame.revision;
      this->cachedPassCount = frame.numPasses;
    this->cachedPassBudget = frame.multipassBufferBudget;
    this->cachedWidth = target.size[0];
      this->cachedHeight = target.size[1];
      this->cachedHomogeneousDepth = homogeneousDepth;
      plan = &this->cachedPlan;
      retained = true;
    }
  }

  bumpCacheCandidate=activeBump && retained && plan==&this->cachedPlan;
  std::vector<bgfx::TextureHandle> textures(
    plan->textures.size(), BGFX_INVALID_HANDLE);
  bool hasDirectTextures = false;
  for (const CoinBgfxTexture & texture : plan->textures)
    if (texture.gpuToken != 0) hasDirectTextures = true;
  std::vector<bool> textureOwned(plan->textures.size(), false);
  bool textureCacheHit = !hasDirectTextures &&
    (bumpTextureReuse || cacheHit || cameraPatchUsed || materialPatchUsed) &&
    this->cachedTextures.size() == plan->textures.size();
  for (bgfx::TextureHandle texture : this->cachedTextures) {
    if (!bgfx::isValid(texture)) textureCacheHit = false;
  }
  if (textureCacheHit) textures = this->cachedTextures;
  for (size_t textureIndex = 0;
       !textureCacheHit && textureIndex < plan->textures.size(); ++textureIndex) {
    const CoinBgfxTexture & texture = plan->textures[textureIndex];
    if (texture.gpuCopySourceSlot != SIZE_MAX) {
      textures[textureIndex] = bgfx::createTexture2D(
        static_cast<uint16_t>(texture.width), static_cast<uint16_t>(texture.height),
        false, 1, coinBgfxTextureFormat(texture.format),
        coinBgfxTextureFlags(texture.format) | BGFX_TEXTURE_BLIT_DST);
      if (!bgfx::isValid(textures[textureIndex])) {
        this->lastError = "BGFX direct texture base-view allocation failed";
        for (size_t i = 0; i < textures.size(); ++i)
          if (textureOwned[i] && bgfx::isValid(textures[i])) bgfx::destroy(textures[i]);
        return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, this->lastError);
      }
      textureOwned[textureIndex] = true;
      continue;
    }
    if (texture.gpuToken != 0) {
      for (const DirectTextureResource & resource : this->directTextures) {
        if (resource.token == texture.gpuToken && resource.format == texture.format &&
            resource.cubeFaceSize==texture.cubeFaceSize && resource.cubeMipmapped==texture.cubeMipmapped &&
            (!texture.mipmapped || resource.mipmapped) && resource.width == int(texture.width) && resource.height == int(texture.height)) {
          textures[textureIndex] = bgfx::getTexture(resource.frameBuffer, 0);
          break;
        }
      }
      if (!bgfx::isValid(textures[textureIndex])) {
        this->lastError = "BGFX direct texture token is missing or expired";
        return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
      }
      continue;
    }
    if (texture.width == 0 || texture.height == 0 ||
        texture.width > UINT16_MAX || texture.height > UINT16_MAX ||
        texture.pixelsRgba.size() !=
          CoinRenderTextureFormatCore::levelBytes(texture.width,texture.height,texture.format) +
          (texture.mipmapped ? CoinRenderTextureSamplingCore::mipBytes(texture.width,texture.height,texture.format) : 0)) {
      this->lastError = "BGFX texture dimensions or RGBA payload are invalid";
      for (size_t i = 0; i < textures.size(); ++i)
        if (textureOwned[i] && bgfx::isValid(textures[i])) bgfx::destroy(textures[i]);
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    }
    textures[textureIndex] = bgfx::createTexture2D(
      static_cast<uint16_t>(texture.width),
      static_cast<uint16_t>(texture.height), texture.mipmapped, 1,
      coinBgfxTextureFormat(texture.format), coinBgfxTextureFlags(texture.format),
      bgfx::copy(texture.pixelsRgba.data(),
                 static_cast<uint32_t>(texture.pixelsRgba.size())));
    if (!bgfx::isValid(textures[textureIndex])) {
      this->lastError = "BGFX texture upload failed";
      for (size_t i = 0; i < textures.size(); ++i)
        if (textureOwned[i] && bgfx::isValid(textures[i])) bgfx::destroy(textures[i]);
      return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, this->lastError);
    }
    textureOwned[textureIndex] = true;
  }
  if(this->portableSampling) for(size_t i=0;i<textures.size();++i)
    this->samplingBoundTextureSizes[textures[i].idx]={plan->textures[i].width,plan->textures[i].height};
  if (!textureCacheHit) for (size_t i = 0; i < plan->textures.size(); ++i) {
    const size_t sourceSlot = plan->textures[i].gpuCopySourceSlot;
    if (sourceSlot == SIZE_MAX) continue;
    bgfx::TextureRegion destination, source;
    destination.handle = textures[i];
    source.handle = textures[sourceSlot];
    bgfx::blit(this->viewBase, destination, source);
  }
  bool texturesRetained = textureCacheHit;
  if (!textureCacheHit && retained && !hasDirectTextures) {
    this->cachedTextures = textures;
    texturesRetained = true;
  }
  const auto destroyTextures = [&]() {
    if (texturesRetained) return;
    for (size_t i = 0; i < textures.size(); ++i)
      if (textureOwned[i] && bgfx::isValid(textures[i])) bgfx::destroy(textures[i]);
  };

  if (!this->prepareShadowArray(shadowPlan,bgfxShadowMultitexture(frame))) {
    destroyTextures(); return {CoinRenderBackendStatus::UNSUPPORTED,this->lastError};
  }
  struct RasterImageOwner {
    std::vector<bgfx::TextureHandle> textures;
    ~RasterImageOwner() { for (auto handle : textures) if (bgfx::isValid(handle)) bgfx::destroy(handle); }
  } rasterImages;
  const auto rasterResult = this->encodeRasterImages(frame, textures, rasterImages.textures);
  if (rasterResult.status != CoinRenderBackendStatus::SUCCESS) { destroyTextures(); return rasterResult; }
  const bgfx::ViewId rasterViewOffset = rasterImages.textures.empty() ? 0 : 1;
  const Clock::time_point uploaded = Clock::now();
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
      accumulation.sums[i] = bgfx::createFrameBuffer(this->width, this->height,
        bgfx::TextureFormat::RGBA32F, BGFX_TEXTURE_RT |
          BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
          BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
      if (!bgfx::isValid(accumulation.sums[i])) {
        destroyTextures();
        return {CoinRenderBackendStatus::OUT_OF_MEMORY,
          "BGFX multipass float attachment allocation failed"};
      }
    }
  }
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
    if (COIN_BGFX_CONSUME_TEST_FAULT("COIN_BGFX_TEST_SHADOW_MAP_ALLOC_ONCE")) {
      destroyTextures();
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
        destroyTextures();
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
      const SbMatrix projection = homogeneousDepth ? pass.projectionCoin :
        pass.projectionCoin * clipConversion;
      const float depthParams[4] = {pass.nearDistance, pass.vsmFarDistance,
        frame.shadowLights[pass.lightSlot].type == CoinRenderLightType::SPOT ? 1.0f : 0.0f, 0.0f};
      std::unordered_set<uint32_t> casterSlots(pass.casterDraws.begin(), pass.casterDraws.end());
      for (const CoinBgfxDraw & draw : plan->shadowDraws) {
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
  bgfx::ViewId opaqueView = this->viewBase + rasterViewOffset + (hasShadows ? static_cast<bgfx::ViewId>(shadowPlan.passes.size()) : 0);
  if (this->shadowArrayActive) this->encodeShadowArray(shadowPlan,shadowMaps.buffers,opaqueView++);
  const bgfx::FrameBufferHandle resolvedOutput = bgfx::isValid(this->windowSceneFrameBuffer)
    ? this->windowSceneFrameBuffer : this->frameBuffer;
  const bgfx::FrameBufferHandle sceneOutput = this->sampleCount > 1 ?
    this->msaaFrameBuffer : resolvedOutput;
  if (selectedStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
      !bgfx::isValid(bgfx::getTexture(sceneOutput, 1))) {
    destroyTextures();
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED,
      "BGFX sorted layers requires a sampleable opaque depth attachment");
  }
  const std::vector<CoinBgfxDraw> & sourceDraws =
    cameraPatchUsed ? cameraDraws : plan->draws;
  std::vector<CoinBgfxDraw> groupedDraws;
  const bool groupDraws = frame.numPasses == 1 && this->drawGroupingEnabled && !activeBump && !hasShadows && !plan->usesInstancing;
  if (groupDraws)
    CoinBgfxLowering::groupOpaqueDraws(sourceDraws, groupedDraws);
  const std::vector<CoinBgfxDraw> & baseDraws =
    groupDraws ? groupedDraws : sourceDraws;
  const LogicalDrawStats drawStats = logicalDrawStats(sourceDraws, baseDraws);
  const bool useSortedLayers =
    selectedStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS;
  const bgfx::TextureHandle surfaceSampleDepth = useSortedLayers && this->sampleCount > 1 ?
    bgfx::getTexture(this->peelFrameBuffers[0], 2) : bgfx::TextureHandle BGFX_INVALID_HANDLE;
  bgfx::ViewId nextView = opaqueView;
  for (int pass = 0; pass < frame.numPasses; ++pass) {
    opaqueView = nextView;
    std::vector<CoinBgfxDraw> jitteredDraws;
    if (frame.numPasses > 1) {
      jitteredDraws = baseDraws;
      for (CoinBgfxDraw & draw : jitteredDraws) {
        if (draw.multipassRasterFixed) continue;
        const auto & state = frame.renderStates[draw.renderStateSlot];
        const auto & viewport = frame.viewports[state.viewportSlot];
        SbMatrix shifted;
        CoinRenderMultipassCore::projection(state.projectionCoin, frame.numPasses, pass, viewport, shifted);
        const SbMatrix projection = CoinRenderTransformCore::projection(shifted, homogeneousDepth);
        const SbMatrix mvp = state.model * state.view * projection *
          CoinRenderTransformCore::viewportTransform(viewport, this->width, this->height);
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
    const std::vector<CoinBgfxDraw> & draws = frame.numPasses > 1 ? jitteredDraws : baseDraws;
  bgfx::setViewName(opaqueView, "opaque");
  bgfx::setViewMode(opaqueView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(opaqueView, 0, 0, static_cast<uint16_t>(this->width),
                    static_cast<uint16_t>(this->height));
  bgfx::setViewFrameBuffer(opaqueView, sceneOutput);
  // The packed clear API quantizes Coin's float color before the GL clear.
  // Keep the float until the renderer converts it to its target format.
  const uint8_t clearIndex = frame.passClearColors.empty() ? 0 : uint8_t(pass);
  const float * sampleClear = frame.passClearColors.empty() ? plan->clearColor : frame.passClearColors[pass].getValue();
  bgfx::setPaletteColor(clearIndex, sampleClear);
  const auto scene = std::find_if(draws.begin(), draws.end(),
    [](const CoinBgfxDraw & draw) { return draw.authoredScene; });
  const bool backgroundDepthHistory = this->sceneDepthInitialized && target.lastSubmissionSerial != 0 &&
    scene != draws.end() && std::any_of(draws.begin(), draws.end(),
      [&](const CoinBgfxDraw & draw) { return draw.renderLayer < scene->renderLayer; });
  bgfx::setViewClear(opaqueView, BGFX_CLEAR_COLOR | (backgroundDepthHistory ? 0 : BGFX_CLEAR_DEPTH), 1.0f, 0, clearIndex);
  bgfx::setViewTransform(opaqueView, nullptr, nullptr);
  bgfx::touch(opaqueView);
  if (frame.stereoMode) {
    if (!this->encodeStereoViews(frame, sceneOutput, opaqueView, this->width, this->height)) {
      destroyTextures();
      return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
    }
    ++opaqueView;
    bgfx::setViewName(opaqueView, "post_stereo_foreground");
    bgfx::setViewMode(opaqueView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(opaqueView, 0, 0, this->width, this->height);
    bgfx::setViewFrameBuffer(opaqueView, sceneOutput);
    bgfx::setViewClear(opaqueView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(opaqueView, nullptr, nullptr);
  }
  if (std::any_of(draws.begin(), draws.end(), [](const CoinBgfxDraw & draw) { return draw.authoredScene; })) {
    // The original view clears the frame once; backgrounds execute before the
    // authored scene and its depth barrier without clearing their color.
    ++opaqueView;
    this->encodeOverlayLayers(draws, vb, ib, sceneOutput, textures, opaqueView,
      this->width, this->height, frame, shadowPlan, shadowMaps.buffers, true, surfaceSampleDepth);
    bgfx::setViewName(opaqueView, "authored_scene");
    bgfx::setViewMode(opaqueView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(opaqueView, 0, 0, this->width, this->height);
    bgfx::setViewFrameBuffer(opaqueView, sceneOutput);
    bgfx::setViewClear(opaqueView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(opaqueView, nullptr, nullptr);
    bgfx::touch(opaqueView);
  }
  nextView = opaqueView + 2;
  for (const CoinBgfxDraw & draw : draws) {
    if (!draw.indexCount || !draw.isSceneLayer() || (draw.blend && draw.deferred)) continue;
    // A skipped draw has no submit to discard its instance binding. Cull it
    // before encoding any geometry so later fullscreen passes stay ordinary.
    if (!setDrawScissor(draw, this->width, this->height)) continue;
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vb);
    bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
    if (plan->usesInstancing)
      bgfx::setInstanceDataBuffer(instanceBuffer, draw.firstInstance, draw.instanceCount);
    bgfx::setState(drawState(draw) | (this->sampleCount > 1 ? BGFX_STATE_MSAA : 0));
    this->submitSurfaceDraw(opaqueView, draw, textures, frame, shadowPlan, shadowMaps.buffers,
      this->width, this->height, surfaceSampleDepth);

  }

  nextView = this->encodeDeferredRuns(draws,vb,ib,sceneOutput,textures,opaqueView+1,
    this->width,this->height,frame,shadowPlan,shadowMaps.buffers,selectedStrategy,
    std::vector<bgfx::FrameBufferHandle>(this->peelFrameBuffers,this->peelFrameBuffers+this->peelPassCount),this->oitFrameBuffer,surfaceSampleDepth);
  if (!this->encodeOverlayLayers(draws, vb, ib,
        sceneOutput,
        textures, nextView, this->width, this->height, frame, shadowPlan, shadowMaps.buffers, false, surfaceSampleDepth)) {
    destroyTextures();
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  if (this->sampleCount > 1)
    this->encodeMsaaResolve(nextView++, resolvedOutput, this->width, this->height, 1.0f);
    if (frame.numPasses > 1) {
      const bgfx::ViewId sumView = nextView++;
      bgfx::setViewName(sumView, "multipass_sum");
      bgfx::setViewMode(sumView, bgfx::ViewMode::Sequential);
      bgfx::setViewRect(sumView, 0, 0, this->width, this->height);
      bgfx::setViewFrameBuffer(sumView, accumulation.sums[pass & 1]);
      bgfx::setViewClear(sumView, BGFX_CLEAR_NONE);
      bgfx::setViewTransform(sumView, nullptr, nullptr);
      const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
      const float info[4] = {1.0f / this->width, 1.0f / this->height,
        pass ? 1.0f : 0.0f, 1.0f / frame.numPasses};
      const uint32_t flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
      bgfx::setTransform(identity);
      bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
      bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
      bgfx::setUniform(this->multipassInfoUniform, info);
      bgfx::setTexture(0, this->multipassSceneSampler, bgfx::getTexture(resolvedOutput, 0), flags);
      // Bind a valid texture even when the branch does not consume a prior sum.
      bgfx::setTexture(1, this->multipassPreviousSampler,
        pass ? bgfx::getTexture(accumulation.sums[(pass - 1) & 1], 0) :
          bgfx::getTexture(resolvedOutput, 0), flags);
      bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
      bgfx::submit(sumView, this->multipassSumProgram);
    }
  }
  if (frame.numPasses > 1) {
    const bgfx::ViewId meanView = nextView++;
    bgfx::setViewName(meanView, "multipass_mean");
    bgfx::setViewMode(meanView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(meanView, 0, 0, this->width, this->height);
    bgfx::setViewFrameBuffer(meanView, resolvedOutput);
    bgfx::setViewClear(meanView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(meanView, nullptr, nullptr);
    const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const float info[4] = {1.0f / this->width, 1.0f / this->height,
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
  if (bgfx::isValid(this->windowSceneFrameBuffer)) {
    const bgfx::ViewId presentView = nextView++;
    bgfx::setViewName(presentView, "window_scene_present");
    bgfx::setViewMode(presentView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(presentView, 0, 0, this->width, this->height);
    bgfx::setViewFrameBuffer(presentView, this->frameBuffer);
    bgfx::setViewClear(presentView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(presentView, nullptr, nullptr);
    const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const float pixelSize[4] = {1.0f / this->width, 1.0f / this->height, 0, 0};
    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
    bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
    bgfx::setUniform(this->depthInfoUniform, pixelSize);
    bgfx::setTexture(0, this->layerSampler,
      bgfx::getTexture(this->windowSceneFrameBuffer, 0),
      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
      BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(presentView, this->compositeProgram);
    if (tracePhases)
      std::fprintf(stderr,
        "COIN_RENDER_PHASE bgfx_window_scene_present target=%p generation=%llu serial=%llu size=%dx%d native_fbo=%u scene_fbo=%u scene_color=%u present_program=%u first_view=%u present_view=%u end_view=%u reserved_views=%u planned_views=%u\n",
        static_cast<void *>(this), static_cast<unsigned long long>(target.resourceGeneration),
        static_cast<unsigned long long>(target.lastSubmissionSerial), this->width, this->height,
        static_cast<unsigned int>(this->frameBuffer.idx),
        static_cast<unsigned int>(this->windowSceneFrameBuffer.idx),
        static_cast<unsigned int>(bgfx::getTexture(this->windowSceneFrameBuffer, 0).idx),
        static_cast<unsigned int>(this->compositeProgram.idx),
        static_cast<unsigned int>(this->viewBase), static_cast<unsigned int>(presentView),
        static_cast<unsigned int>(nextView), this->viewCount, requiredViews);
  }
  const Clock::time_point drawsEncoded = Clock::now();
  if (COIN_BGFX_CONSUME_TEST_FAULT("COIN_BGFX_TEST_DEVICE_LOST_ON_SUBMIT_ONCE")) {
#ifdef COIN_WGPU_TEST_HOOKS
    static_cast<CoinBgfxCallback *>(this->callback.get())->inject(
      bgfx::Fatal::DeviceLost, "injected device loss during submission");
#endif
  }
  CoinRenderBackendStatus runtimeStatus = this->checkRuntimeFailure("BGFX submission failed");
  if (runtimeStatus != CoinRenderBackendStatus::SUCCESS)
    return CoinRenderSubmitResult(runtimeStatus, this->lastError);
  if (this->presentToWindow) {
    CoinBgfxCallback * screenshotCallback =
      static_cast<CoinBgfxCallback *>(this->callback.get());
    if (target.windowReadbackRequested) {
      const std::string requestName =
        screenshotCallback->beginScreenshot(this->width, this->height);
      bgfx::requestScreenShot(this->frameBuffer, requestName.c_str());
    }
    const uint32_t submittedFrame = bgfx::frame();
    if (target.windowReadbackRequested) {
      // frame() waits for its predecessor, then releases the submitted scene
      // to the renderer thread. The renderer performs synchronous native
      // screenshot readback before signalling that scene's completion. Fence
      // it with an empty resource frame so cold program/pipeline preparation
      // and rendering finish inside apply(), before the callback deadline.
      // FLUSH suppresses presentation of this empty frame; no scene is replayed.
      bgfx::frame(BGFX_FRAME_FLUSH);
    }
    runtimeStatus = this->checkRuntimeFailure("BGFX frame submission failed");
    if (runtimeStatus != CoinRenderBackendStatus::SUCCESS) {
      screenshotCallback->cancelScreenshot();
      return CoinRenderSubmitResult(runtimeStatus, this->lastError);
    }
    const Clock::time_point submitted = Clock::now();
    if (target.windowReadbackRequested &&
        !screenshotCallback->takeScreenshot(target.colorBuffer, this->lastError)) {
      if(this->lastError.empty()) this->lastError = "BGFX window screenshot unavailable or incomplete";
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    }
    CoinBgfxPhaseSample sample;
    sample.gpuTimingRequested = traceGpu;
    const Clock::time_point gpuDrainBegin = Clock::now();
    uint32_t gpuQueryFrames = 0;
    for (int attempts = 0;
         traceGpu && !captureGpuPasses(submittedFrame, sample) && attempts < 4;
         ++attempts) {
      bgfx::frame();
      ++gpuQueryFrames;
    }
    runtimeStatus = this->checkRuntimeFailure("BGFX profiling drain failed");
    if (runtimeStatus != CoinRenderBackendStatus::SUCCESS)
      return CoinRenderSubmitResult(runtimeStatus, this->lastError);
    const Clock::time_point gpuDrainComplete = Clock::now();
    destroyTextures();
    if (cameraPatchUsed) {
      this->cachedPlan.draws.swap(cameraDraws);
      if (!cameraInstances.empty() && !this->cachedPlan.instances.empty())
        this->cachedPlan.instances.swap(cameraInstances);
      this->cachedRevision = frame.revision;
    }
    if (!target.windowReadbackRequested) target.colorBuffer.clear();
    target.depthBuffer.clear();
    target.needsReconfigure = false;
    if (tracePhases) {
      const auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
      };
      sample.lowerMs = ms(begin, lowered);
      sample.uploadMs = ms(lowered, uploaded);
      sample.encodeMs = ms(uploaded, drawsEncoded);
      sample.drawEncodeMs = sample.encodeMs;
      sample.submitFrameMs = ms(drawsEncoded, submitted);
      sample.gpuQueryDrainMs = ms(gpuDrainBegin, gpuDrainComplete);
      sample.gpuQueryFrames = gpuQueryFrames;
      sample.vertices = plan->vertexCount();
      sample.draws = plan->draws.size();
      sample.resourceCacheHit = cacheHit || cameraPatchUsed;
      sample.textureCacheHit = textureCacheHit;
      sample.geometryBufferReused = geometryBufferReused;
      sample.vertexBufferCapacity = this->cachedVertexCapacity;
      sample.indexBufferCapacity = this->cachedIndexCapacity;
      sample.cameraPatchUsed = cameraPatchUsed;
      sample.materialPatchUsed = materialPatchUsed;
      sample.materialPatchRanges = static_cast<uint32_t>(materialRanges.size());
      sample.materialPatchVertices = materialPatchVertices;
      sample.readbackPublishedBytes = target.windowReadbackRequested
        ? target.colorBuffer.size() : 0;
      copyLogicalDrawStats(drawStats, this->drawGroupingEnabled, sample);
      std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_geometry compact_vertices=%d vertex_stride=%u instancing=%d instances=%zu world_space=%d instance_stride=%u instance_buffer_capacity=%u\n",
        plan->usesCompactVertices ? 1 : 0,
        static_cast<unsigned int>(plan->usesInstancing ? sizeof(CoinBgfxInstancedVertex) :
          plan->usesCompactVertices ? sizeof(CoinBgfxVertexPrefix) : sizeof(CoinBgfxVertex)),
        plan->usesInstancing ? 1 : 0, plan->instanceCount(), plan->instancedWorldSpace ? 1 : 0,
        static_cast<unsigned int>(sizeof(CoinBgfxInstance)), this->cachedInstanceCapacity);
      std::fprintf(stderr, "%s\n", CoinRenderDiagnosticShell::formatBgfxPhase(sample).c_str());
    }
    this->lastError.clear();
    rememberBump();
    this->sceneDepthInitialized = true;
    COIN_BGFX_TEST_SUBMIT_SUCCEEDED();
    return CoinRenderSubmitResult(CoinRenderBackendStatus::SUCCESS, "", ++this->serial);
  }
  if (this->readbackSlots.empty()) {
    this->lastError = "BGFX readback staging ring is not allocated";
    return CoinRenderSubmitResult(CoinRenderBackendStatus::NOT_READY, this->lastError);
  }
  std::shared_ptr<AsyncEntry> asyncEntry;
  ReadbackSlot * writeSlot = NULL;
  if (outTicket) {
    try {
      asyncEntry = std::make_shared<AsyncEntry>();
      asyncEntry->slot.pixels.resize(size_t(this->width) * this->height * 4);
      if (target.depthReadbackEnabled) asyncEntry->slot.depth.resize(size_t(this->width) * this->height);
    } catch (const std::bad_alloc &) {
      bgfx::frame(); destroyTextures();
      return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, "BGFX async CPU staging allocation failed");
    }
    auto & slot = asyncEntry->slot;
    slot.texture = bgfx::createTexture2D(this->width, this->height, false, 1,
      bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    if (target.depthReadbackEnabled) slot.depthTexture = bgfx::createTexture2D(this->width, this->height, false, 1,
      bgfx::TextureFormat::R32F, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    if (!bgfx::isValid(slot.texture) || (target.depthReadbackEnabled && !bgfx::isValid(slot.depthTexture))) {
      if (bgfx::isValid(slot.texture)) bgfx::destroy(slot.texture);
      if (bgfx::isValid(slot.depthTexture)) bgfx::destroy(slot.depthTexture);
      destroyTextures();
      bgfx::frame();
      return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, "BGFX async staging allocation failed");
    }
    auto & ticket = asyncEntry->ticket;
    ticket.token = nextTicketToken++;
    ticket.generation = target.generation;
    ticket.submissionSerial = ++this->serial;
    ticket.width = this->width; ticket.height = this->height;
    ticket.colorRowPitch = this->width * 4; ticket.colorBytes = slot.pixels.size();
    if (target.depthReadbackEnabled) {
      ticket.depthFormat = 1; ticket.depthRowPitch = ticket.colorRowPitch; ticket.depthBytes = slot.depth.size() * 4;
    }
    asyncEntry->bottomLeft = bgfx::getCaps()->originBottomLeft;
    try {
      auto & runtime = sharedRuntime();
      std::lock_guard<std::mutex> guard(runtime.mutex);
      asyncEntries().emplace(ticket.token, asyncEntry);
      ++runtime.references;
    } catch (const std::bad_alloc &) {
      bgfx::destroy(slot.texture);
      if (bgfx::isValid(slot.depthTexture)) bgfx::destroy(slot.depthTexture);
      bgfx::frame(); destroyTextures();
      return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, "BGFX async ticket allocation failed");
    }
    writeSlot = &slot;
  }
  uint32_t preBlitWaitFrames = 0;
  for (size_t offset = 0; !writeSlot && offset < this->readbackSlots.size(); ++offset) {
    ReadbackSlot & candidate = this->readbackSlots[
      (this->readbackCursor + offset) % this->readbackSlots.size()];
    if (!candidate.pending) {
      writeSlot = &candidate;
      this->readbackCursor = static_cast<uint32_t>(
        (&candidate - this->readbackSlots.data() + 1) % this->readbackSlots.size());
      break;
    }
  }
  if (!writeSlot) {
    ReadbackSlot * oldest = &this->readbackSlots[0];
    for (ReadbackSlot & slot : this->readbackSlots) {
      if (slot.sequence < oldest->sequence) oldest = &slot;
    }
    uint32_t completed = bgfx::frame();
    ++preBlitWaitFrames;
    for (int attempts = 0; completed < oldest->readyFrame && attempts < 16;
         ++attempts) {
      completed = bgfx::frame();
      ++preBlitWaitFrames;
    }
    runtimeStatus = this->checkRuntimeFailure("BGFX readback backpressure failed");
    if (runtimeStatus != CoinRenderBackendStatus::SUCCESS)
      return CoinRenderSubmitResult(runtimeStatus, this->lastError);
    if (completed < oldest->readyFrame) {
      this->lastError = "BGFX readback pipeline backpressure exceeded sixteen frames";
      return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
    }
    if (bgfx::getCaps()->rendererType == bgfx::RendererType::OpenGL &&
        !CoinRenderImageCore::flipRgba8Rows(oldest->pixels, target.size)) {
      this->lastError = "BGFX OpenGL returned an invalid pipelined readback size";
      return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
    }
    if (oldest->depthRequested && bgfx::getCaps()->originBottomLeft)
      for (int y = 0; y < this->height / 2; ++y)
        std::swap_ranges(oldest->depth.begin() + y * this->width, oldest->depth.begin() + (y + 1) * this->width,
                         oldest->depth.begin() + (this->height - y - 1) * this->width);
    if (oldest->depthRequested) this->lastPublishedDepth = oldest->depth;
    else this->lastPublishedDepth.clear();
    this->lastPublishedReadback = oldest->pixels;
    this->lastPublishedSequence = oldest->sequence;
    oldest->pending = false;
    writeSlot = oldest;
  }
  if (target.depthReadbackEnabled) {
    if (this->sampleCount > 1) {
      this->encodeMsaaResolve(nextView++, this->depthReadFrameBuffer,
                              this->width, this->height, 2.0f);
    } else {
      bgfx::setViewName(nextView, "depth_readback_conversion");
      bgfx::setViewRect(nextView, 0, 0, this->width, this->height);
      bgfx::setViewFrameBuffer(nextView, this->depthReadFrameBuffer);
      bgfx::setViewClear(nextView, BGFX_CLEAR_NONE);
      bgfx::setViewTransform(nextView, nullptr, nullptr);
      const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
      const float info[4] = {1.0f / this->width, 1.0f / this->height, 0, 0};
      bgfx::setTransform(identity);
      bgfx::setUniform(this->depthInfoUniform, info);
      bgfx::setTexture(0, this->readDepthSampler, bgfx::getTexture(this->frameBuffer, 1));
      bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
      bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
      bgfx::setState(BGFX_STATE_WRITE_R);
      bgfx::submit(nextView++, this->depthReadProgram);
    }
    bgfx::TextureRegion depthDst, depthSrc;
    depthDst.handle = writeSlot->depthTexture;
    depthSrc.handle = bgfx::getTexture(this->depthReadFrameBuffer);
    bgfx::blit(nextView, depthDst, depthSrc);
  }
  bgfx::TextureRegion destination;
  destination.handle = writeSlot->texture;
  bgfx::TextureRegion source;
  source.handle = bgfx::getTexture(this->frameBuffer);
  const bgfx::ViewId blitView = nextView;
  bgfx::setViewName(blitView, "blit_readback");
  bgfx::blit(blitView, destination, source);
  const Clock::time_point encoded = Clock::now();
  const size_t bytes = static_cast<size_t>(this->width) * static_cast<size_t>(this->height) * 4;
  // A synchronous publication exchanges this storage with the target's
  // previous image. Reestablish its size before handing the pointer to BGFX.
  writeSlot->pixels.resize(bytes);
  writeSlot->depthRequested = target.depthReadbackEnabled;
  if (writeSlot->depthRequested)
    writeSlot->depth.resize(static_cast<size_t>(this->width) * this->height);
  bgfx::TextureRegion readRegion;
  readRegion.handle = writeSlot->texture;
  writeSlot->sequence = ++this->readbackSequence;
  writeSlot->readyFrame = bgfx::read(readRegion, writeSlot->pixels.data());
  if (target.depthReadbackEnabled) {
    readRegion.handle = writeSlot->depthTexture;
    writeSlot->readyFrame = std::max(writeSlot->readyFrame, bgfx::read(readRegion, writeSlot->depth.data()));
  }
  writeSlot->pending = true;
  if (outTicket) {
    auto & ticket = asyncEntry->ticket;
    bgfx::frame(); // Submit only; polling advances completion without a wait loop.
    destroyTextures();
#ifdef COIN_WGPU_TEST_HOOKS
    if (COIN_BGFX_CONSUME_TEST_FAULT("COIN_BGFX_TEST_DEVICE_LOST_AFTER_ASYNC_ONCE"))
      static_cast<CoinBgfxCallback*>(this->callback.get())
          ->inject(bgfx::Fatal::DeviceLost, "injected device loss after async ticket allocation");
#endif
    const auto asyncStatus = this->checkRuntimeFailure("BGFX asynchronous submission failed");
    // Return the private allocation on failure so the common owner can cancel it.
    *outTicket = ticket;
    if (asyncStatus == CoinRenderBackendStatus::SUCCESS) {
      target.colorBuffer.clear();
      target.depthBuffer.clear();
      target.needsReconfigure = false;
    }
    if(asyncStatus==CoinRenderBackendStatus::SUCCESS) {this->sceneDepthInitialized = true;rememberBump();COIN_BGFX_TEST_SUBMIT_SUCCEEDED();}
    return CoinRenderSubmitResult(asyncStatus, this->lastError, ticket.submissionSerial);
  }
  const Clock::time_point readRequested = Clock::now();
  uint32_t completedFrame = bgfx::frame();
  runtimeStatus = this->checkRuntimeFailure("BGFX frame submission failed");
  if (runtimeStatus != CoinRenderBackendStatus::SUCCESS)
    return CoinRenderSubmitResult(runtimeStatus, this->lastError);
  uint32_t readWaitFrames = preBlitWaitFrames + 1;
  const uint32_t submittedFrame = completedFrame;
  const Clock::time_point submitted = Clock::now();
  destroyTextures();
  CoinBgfxPhaseSample gpuSample;
  gpuSample.gpuTimingRequested = traceGpu;
  const auto captureGpuFrame = [&]() {
    if (!traceGpu) return;
    captureGpuPasses(submittedFrame, gpuSample);
  };
  captureGpuFrame();
  // A reusable STAGED producer must read its own execution even if the
  // ordinary target policy allows a pipelined predecessor. Keep the actual
  // configured slot count and the existing bounded completion wait unchanged.
  const bool currentReadback = this->readbackPipelineDepth == 1 || target.requiresCurrentReadback;
  ReadbackSlot * publishSlot = NULL;
  const auto selectReady = [&]() {
    ReadbackSlot * selected = static_cast<ReadbackSlot *>(NULL);
    for (ReadbackSlot & slot : this->readbackSlots) {
      if (slot.pending && (!target.requiresCurrentReadback || slot.sequence == writeSlot->sequence) &&
          slot.readyFrame <= completedFrame &&
          (!selected || slot.sequence < selected->sequence)) selected = &slot;
    }
    return selected;
  };
  publishSlot = selectReady();
  bool readbackBootstrap = false;
  if (!publishSlot && (currentReadback ||
                       this->lastPublishedReadback.empty())) {
    readbackBootstrap = true;
    for (int attempts = 0;
         !publishSlot && attempts < 16; ++attempts) {
      completedFrame = bgfx::frame();
      ++readWaitFrames;
      captureGpuFrame();
      publishSlot = selectReady();
    }
    runtimeStatus = this->checkRuntimeFailure("BGFX readback wait failed");
    if (runtimeStatus != CoinRenderBackendStatus::SUCCESS)
      return CoinRenderSubmitResult(runtimeStatus, this->lastError);
  }
  if (!publishSlot && (currentReadback || this->lastPublishedReadback.empty())) {
    this->lastError = "BGFX readback did not complete within sixteen frames";
    return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  }
  const Clock::time_point framesCompleted = Clock::now();
  uint32_t readbackLatencyFrames = 0;
  const Clock::time_point readComplete = Clock::now();
  const Clock::time_point gpuDrainBegin = Clock::now();
  uint32_t gpuQueryFrames = 0;
  for (int attempts = 0; traceGpu && gpuSample.gpuFrameMs < 0.0 && attempts < 4; ++attempts) {
    bgfx::frame();
    ++gpuQueryFrames;
    captureGpuFrame();
  }
  runtimeStatus = this->checkRuntimeFailure("BGFX profiling drain failed");
  if (runtimeStatus != CoinRenderBackendStatus::SUCCESS)
    return CoinRenderSubmitResult(runtimeStatus, this->lastError);
  const Clock::time_point gpuDrainComplete = Clock::now();
  // GL readback follows the framebuffer's bottom-left origin; Vulkan's
  // readback in this profile already matches the target's top-left RGBA view.
  // The row-swap is symmetric, so the shared mechanical image helper applies.
  if (publishSlot) {
    if (bgfx::getCaps()->rendererType == bgfx::RendererType::OpenGL &&
        !CoinRenderImageCore::flipRgba8Rows(publishSlot->pixels, target.size)) {
      this->lastError = "BGFX OpenGL returned an invalid RGBA readback size";
      return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
    }
    if (publishSlot->depthRequested && bgfx::getCaps()->originBottomLeft)
      for (int y = 0; y < this->height / 2; ++y)
        std::swap_ranges(publishSlot->depth.begin() + y * this->width, publishSlot->depth.begin() + (y + 1) * this->width,
                         publishSlot->depth.begin() + (this->height - y - 1) * this->width);
    if (currentReadback) {
      // The synchronous contract publishes this frame before returning. Its
      // target storage can be recycled without maintaining a duplicate image.
      target.colorBuffer.swap(publishSlot->pixels);
      if (publishSlot->depthRequested) target.depthBuffer.swap(publishSlot->depth);
    } else {
      if (publishSlot->depthRequested) this->lastPublishedDepth = publishSlot->depth;
      else this->lastPublishedDepth.clear();
      this->lastPublishedReadback = publishSlot->pixels;
    }
    this->lastPublishedSequence = publishSlot->sequence;
    publishSlot->pending = false;
  }
  if (!currentReadback) target.colorBuffer = this->lastPublishedReadback;
  readbackLatencyFrames = static_cast<uint32_t>(
    this->readbackSequence - this->lastPublishedSequence);
  const Clock::time_point readbackNormalized = Clock::now();
  if (cameraPatchUsed) {
    this->cachedPlan.draws.swap(cameraDraws);
    if (!cameraInstances.empty() && !this->cachedPlan.instances.empty())
      this->cachedPlan.instances.swap(cameraInstances);
    this->cachedRevision = frame.revision;
  }
  if (target.depthReadbackEnabled && !currentReadback)
    target.depthBuffer = this->lastPublishedDepth;
  else if (!target.depthReadbackEnabled) target.depthBuffer.clear();
  if (tracePhases) {
    const auto ms = [](Clock::time_point a, Clock::time_point b) {
      return std::chrono::duration<double, std::milli>(b - a).count();
    };
    CoinBgfxPhaseSample sample = gpuSample;
    sample.lowerMs = ms(begin, lowered);
    sample.uploadMs = ms(lowered, uploaded);
    sample.encodeMs = ms(uploaded, encoded);
    sample.drawEncodeMs = ms(uploaded, drawsEncoded);
    sample.blitEncodeMs = ms(drawsEncoded, encoded);
    sample.gpuQueryDrainMs = ms(gpuDrainBegin, gpuDrainComplete);
    sample.gpuQueryFrames = gpuQueryFrames;
    sample.readRequestMs = ms(encoded, readRequested);
    sample.submitFrameMs = ms(readRequested, submitted);
    sample.readWaitMs = ms(submitted, readComplete);
    sample.frameWaitMs = ms(submitted, framesCompleted);
    sample.rowFlipMs = ms(gpuDrainComplete, readbackNormalized);
    sample.vertices = plan->vertexCount();
    sample.draws = plan->draws.size();
    sample.readWaitFrames = readWaitFrames;
    sample.readbackPipelineDepth = this->readbackPipelineDepth;
    sample.readbackLatencyFrames = readbackLatencyFrames;
    sample.readbackGpuStagingBytes = static_cast<uint64_t>(bytes) *
      this->readbackSlots.size();
    sample.readbackCpuStagingBytes = sample.readbackGpuStagingBytes;
    sample.readbackPublishedBytes = target.colorBuffer.size();
    sample.readbackPipelineBytes = sample.readbackGpuStagingBytes +
      sample.readbackCpuStagingBytes;
    sample.readbackBootstrap = readbackBootstrap;
    sample.resourceCacheHit = cacheHit || cameraPatchUsed;
    sample.textureCacheHit = textureCacheHit;
    sample.geometryBufferReused = geometryBufferReused;
    sample.vertexBufferCapacity = this->cachedVertexCapacity;
    sample.indexBufferCapacity = this->cachedIndexCapacity;
    sample.cameraPatchUsed = cameraPatchUsed;
    sample.materialPatchUsed = materialPatchUsed;
    sample.materialPatchRanges = static_cast<uint32_t>(materialRanges.size());
    sample.materialPatchVertices = materialPatchVertices;
    copyLogicalDrawStats(drawStats, this->drawGroupingEnabled, sample);
    static_cast<CoinBgfxCallback *>(this->callback.get())->traceProgramCache();
    std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_geometry compact_vertices=%d vertex_stride=%u instancing=%d instances=%zu world_space=%d instance_stride=%u instance_buffer_capacity=%u\n",
      plan->usesCompactVertices ? 1 : 0,
      static_cast<unsigned int>(plan->usesInstancing ? sizeof(CoinBgfxInstancedVertex) :
        plan->usesCompactVertices ? sizeof(CoinBgfxVertexPrefix) : sizeof(CoinBgfxVertex)),
      plan->usesInstancing ? 1 : 0, plan->instanceCount(), plan->instancedWorldSpace ? 1 : 0,
      static_cast<unsigned int>(sizeof(CoinBgfxInstance)), this->cachedInstanceCapacity);
    std::fprintf(stderr, "%s\n", CoinRenderDiagnosticShell::formatBgfxPhase(sample).c_str());
  }
  this->lastError.clear();
  rememberBump();
  this->sceneDepthInitialized = true;
  COIN_BGFX_TEST_SUBMIT_SUCCEEDED();
  return CoinRenderSubmitResult(CoinRenderBackendStatus::SUCCESS, "", ++this->serial);
}
