// CoinRender BGFX Preflight responsibilities.
#include "rendering/coinbgfx/CoinBgfxBackendInternal.h"

using namespace coin_bgfx_internal;

CoinRenderSubmitResult CoinBgfxBackend::preflightRtt(const CoinRenderRttPlan& graph,
                                                     const CoinRenderFramePlan& root,
                                                     const SbVec2i32& size) const {
  uint64_t shaderBytes = 0;
  std::string shaderDiagnostic;
  if (!graph.shaderResourceBytes(root, shaderBytes, shaderDiagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, shaderDiagnostic};
  if(std::any_of(graph.producers.begin(),graph.producers.end(),[](const CoinRenderRttProducer & p){return p.cube;})) {
    uint64_t nominal=0;std::string diagnostic;
    if(!graph.cubeNominalBytes(root,this->sampleCount,nominal,diagnostic))return {CoinRenderBackendStatus::UNSUPPORTED,diagnostic};
    const auto * caps=this->initialized?bgfx::getCaps():nullptr;
    for(size_t i=0;i<graph.producers.size();++i)if(graph.producers[i].cube) {
      CoinRenderTextureImageSnapshot atlas;
      if(!CoinRenderCubeTextureCore::describe(uint32_t(graph.producers[i].size[0]),graph.requestsMips(i+1,root),atlas))
        return {CoinRenderBackendStatus::UNSUPPORTED,"Invalid cube atlas dimensions"};
      const uint64_t atlasFlags=BGFX_TEXTURE_RT|BGFX_TEXTURE_BLIT_DST|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|
        BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;
      if(caps && (atlas.width>caps->limits.maxTextureSize || atlas.height>caps->limits.maxTextureSize ||
          (graph.mode==COIN_RENDER_SCENE_TEXTURE_DIRECT && (!caps->limits.maxBlits ||
            !bgfx::isTextureValid(1,false,1,bgfx::TextureFormat::RGBA8,atlasFlags)))))
        return {CoinRenderBackendStatus::UNSUPPORTED,"Cube atlas exceeds BGFX copy/texture capabilities"};
    }
    const auto expanded=graph.inspection(root);return this->preflightRtt(expanded,root,size);
  }
  auto admission = bgfxTextureProfile(root);
  if (admission.status != CoinRenderBackendStatus::SUCCESS) return admission;
  admission = this->msaaProfile(root);
  if (admission.status != CoinRenderBackendStatus::SUCCESS) return admission;
  for (const auto & producer : graph.producers) {
    admission = bgfxTextureProfile(producer.plan);
    if (admission.status != CoinRenderBackendStatus::SUCCESS) return admission;
    admission = this->msaaProfile(producer.plan);
    if (admission.status != CoinRenderBackendStatus::SUCCESS) return admission;
    if (this->sampleCount > 1 && producer.format != CoinRenderTextureFormat::RGBA8_LINEAR)
      return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX MSAA RTT requires RGBA8 linear output"};
  }
  const auto bumpViewport=[](const CoinRenderFramePlan & frame,const SbVec2i32 & size) {
    const bool active=std::any_of(frame.bumpCoords.begin(),frame.bumpCoords.end(),[](const CoinRenderBumpCoordOverride & b){return b.imageSlot!=0;});
    if(!active)return true;
    for(const auto & draw:frame.draws) {
      if (draw.shadowLightSlot)
        continue;
      const auto & viewport=frame.viewports[frame.renderStates[draw.renderStateSlot].viewportSlot];
      if(viewport.x!=0 || viewport.y!=0 || viewport.width!=size[0] || viewport.height!=size[1])return false;
    }
    return true;
  };
  if(!bumpViewport(root,size))return {CoinRenderBackendStatus::UNSUPPORTED,"BGFX bump requires the full target viewport"};
  for(const auto & producer:graph.producers)if(!bumpViewport(producer.plan,producer.size))
    return {CoinRenderBackendStatus::UNSUPPORTED,"BGFX bump producer requires its full target viewport"};
  bool multipassProgramsNeeded = root.numPasses > 1;
  for (const auto & producer : graph.producers)
    multipassProgramsNeeded = multipassProgramsNeeded || producer.plan.numPasses > 1;
  if (this->initialized && multipassProgramsNeeded &&
      !const_cast<CoinBgfxBackend *>(this)->prepareMultipassPrograms())
    return {CoinRenderBackendStatus::BACKEND_ERROR, this->lastError};
  const auto prepareBumpFrame = [this](const CoinRenderFramePlan &frame, bool windowTarget) {
    if (!this->initialized || !bgfxFrameProgramNeeds(frame).bump)
      return true;
    auto *backend = const_cast<CoinBgfxBackend *>(this);
    if (!coin_render_uses_extended_bump(frame))
      return backend->prepareBumpPrograms();
    std::vector<CoinRenderBumpGpuVertex> stream;
    if (!coin_render_build_bump_gpu_stream(frame, stream, backend->lastError))
      return false;
    for (const auto &source : frame.draws) {
      if (source.shadowLightSlot || !source.geometry.indexCount || stream.empty())
        continue;
      if (source.geometry.firstIndex >= frame.indices.size() ||
          frame.indices[source.geometry.firstIndex] >= stream.size()) {
        backend->lastError = "Invalid bump receiver capability geometry range";
        return false;
      }
      if (!stream[frame.indices[source.geometry.firstIndex]].imageSlot)
        continue;
      const auto &state = frame.renderStates[source.renderStateSlot];
      CoinBgfxDraw draw{};
      draw.depthTest = state.depthTest;
      draw.depthWrite = state.depthWrite;
      draw.depthFunction = state.depthFunction;
      draw.colorWrite = state.colorWrite;
      draw.colorWriteMask = state.colorWriteMask;
      draw.cullMode = state.cullMode;
      draw.frontFace = state.frontFace;
      if (!backend->prepareBumpReceiverPrograms(draw, windowTarget,
                                              coinBgfxTextureFormat(frame.outputColorFormat)))
        return false;
    }
    return true;
  };
  if (!prepareBumpFrame(root, this->presentToWindow && root.numPasses == 1))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  for (const auto &producer : graph.producers)
    if (!prepareBumpFrame(producer.plan, false))
      return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  const auto prepareImages = [this](const CoinRenderFramePlan &frame) {
    return !this->initialized || !std::any_of(frame.renderStates.begin(), frame.renderStates.end(),
      [](const CoinRenderRenderStateSnapshot & state) { return state.rasterTextureSourceSlot != UINT32_MAX; }) ||
      const_cast<CoinBgfxBackend *>(this)->prepareRasterImageResources();
  };
  if (!prepareImages(root)) return {CoinRenderBackendStatus::BACKEND_ERROR, this->lastError};
  for (const auto & producer : graph.producers)
    if (!prepareImages(producer.plan)) return {CoinRenderBackendStatus::BACKEND_ERROR, this->lastError};
  const auto needsWeighted = [](const CoinRenderFramePlan & plan) {
    std::vector<CoinRenderCompositionItem> order;
    std::string diagnostic;
    if (!coin_render_composition_order(plan, order, diagnostic)) return false;
    return std::any_of(order.begin(), order.end(), [](const CoinRenderCompositionItem & item) {
      return item.blend && item.deferred && !item.additive &&
        item.transparencyStrategy == CoinRenderCompositionItem::WEIGHTED_OIT;
    });
  };
  const auto needsPeeling = [](const CoinRenderFramePlan & plan) {
    std::vector<CoinRenderCompositionItem> order;
    std::string diagnostic;
    if (!coin_render_composition_order(plan, order, diagnostic)) return false;
    return std::any_of(order.begin(), order.end(), [](const CoinRenderCompositionItem & item) {
      return item.blend && item.deferred && !item.additive &&
        item.transparencyStrategy == CoinRenderCompositionItem::SORTED_LAYERS;
    });
  };
  bool peeling = needsPeeling(root);
  for (const auto & producer : graph.producers) peeling = peeling || needsPeeling(producer.plan);
  bool weighted = needsWeighted(root);
  for (const auto & producer : graph.producers) weighted = weighted || needsWeighted(producer.plan);
  // The second graph preflight follows prepare(), when this target's sample
  // count and readback policy are known. Reject the entire graph before the
  // first producer can encode a scene or replace a cached RTT image.
  if (this->initialized) {
    std::string diagnostic;
    const auto * caps = bgfx::getCaps();
    const uint32_t outputViews = this->presentToWindow ?
      (caps && caps->rendererType == bgfx::RendererType::OpenGL ? 1 : 0) :
      (this->preparedDepthReadbackEnabled ? 2 : 1);
    uint32_t rootViews = 0, producerViews = 0, largestProducerViews = 0;
    if (!bgfxMsaaFrameViewBudget(root, size[0], size[1], outputViews, this->sampleCount, false, diagnostic, &rootViews))
      return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
    for (const auto & producer : graph.producers) {
      // STAGED renders to its own synchronous offscreen target; DIRECT resolves
      // into the producer texture without a color readback/blit view.
      const uint32_t producerOutputViews = graph.mode == COIN_RENDER_SCENE_TEXTURE_DIRECT ? 0 : 1;
      if (!bgfxMsaaFrameViewBudget(producer.plan, producer.size[0], producer.size[1],
            producerOutputViews, this->sampleCount, graph.mode == COIN_RENDER_SCENE_TEXTURE_DIRECT, diagnostic, &producerViews))
        return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
      largestProducerViews = std::max(largestProducerViews,producerViews);
    }
    if (!this->canReserveGraphViews(rootViews,largestProducerViews,
        graph.mode == COIN_RENDER_SCENE_TEXTURE_STAGED && !graph.producers.empty(),diagnostic))
      return {CoinRenderBackendStatus::UNSUPPORTED,diagnostic};
    if (peeling && !bgfxPeelingSamples(this->sampleCount, this->layout, this->viewBase, diagnostic))
      return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
    if (weighted && !bgfxWeightedSamples(this->sampleCount, this->layout, this->viewBase, diagnostic))
      return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  }
  const auto shadowMultitexturePreflight = [&](const CoinRenderFramePlan & frame) -> CoinRenderSubmitResult {
    if (!bgfxShadowMultitexture(frame)) return {};
    CoinRenderShadowPlan plan; std::string diagnostic;
    if (!coin_render_plan_shadows(frame, plan, diagnostic) || !coin_render_shadow_object_profile(frame, plan, plan.passes.size(), diagnostic))
      return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
    const bool ordinary = bgfxFrameProgramNeeds(frame).ordinary;
    const bool customDepth = this->sampleCount > 1 && std::any_of(frame.renderStates.begin(), frame.renderStates.end(), [](const CoinRenderRenderStateSnapshot & state) {
      return state.depthRange[0] != 0.0f || state.depthRange[1] != 1.0f ||
        (state.polygonOffsetEnabled && (state.polygonOffsetStyles & state.polygonOffsetPrimitiveStyle) &&
         (state.polygonOffsetFactor != 0.0f || state.polygonOffsetUnits != 0.0f || state.polygonOffsetSlopeBias != 0.0f));
    });
    if (this->initialized && ordinary &&
        (!const_cast<CoinBgfxBackend *>(this)->prepareShadowMultitextureProgram() ||
         (customDepth &&
          !const_cast<CoinBgfxBackend *>(this)->prepareShadowMultitextureProgram(true))))
      return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
    if(!coin_render_shadow_array_budget(plan,2,diagnostic))return {CoinRenderBackendStatus::UNSUPPORTED,diagnostic};
    std::vector<CoinRenderCompositionItem> order;
    if(!coin_render_composition_order(frame,order,diagnostic))return {CoinRenderBackendStatus::UNSUPPORTED,diagnostic};
    const bool peel=std::any_of(order.begin(),order.end(),[](const CoinRenderCompositionItem &item){return item.blend&&item.deferred&&!item.additive&&item.transparencyStrategy==CoinRenderCompositionItem::SORTED_LAYERS;});
    const bool weighted=std::any_of(order.begin(),order.end(),[](const CoinRenderCompositionItem &item){return item.blend&&item.deferred&&!item.additive&&item.transparencyStrategy==CoinRenderCompositionItem::WEIGHTED_OIT;});
    if (this->initialized && ordinary &&
        ((peel &&
          !const_cast<CoinBgfxBackend *>(this)->prepareShadowMultitextureProgram(false, 1)) ||
         (weighted &&
          (!const_cast<CoinBgfxBackend *>(this)->prepareShadowMultitextureProgram(false, 2) ||
           (this->sampleCount > 1 &&
            !const_cast<CoinBgfxBackend *>(this)->prepareShadowMultitextureProgram(true))))))
      return {CoinRenderBackendStatus::UNSUPPORTED,this->lastError};
    return {};
  };
  admission = shadowMultitexturePreflight(root); if (admission.status != CoinRenderBackendStatus::SUCCESS) return admission;
  for (const auto & producer : graph.producers) {
    admission = shadowMultitexturePreflight(producer.plan); if (admission.status != CoinRenderBackendStatus::SUCCESS) return admission;
  }
  // DIRECT-only allocation and mip capabilities do not constrain STAGED.
  // Both modes still reach the whole-graph module preparation below.
  if (graph.mode == COIN_RENDER_SCENE_TEXTURE_DIRECT) {
    for (size_t producerIndex=0;producerIndex<graph.producers.size();++producerIndex) {
      const auto& producer=graph.producers[producerIndex];
      const bool requestsMips=graph.requestsMips(producerIndex+1,root);
      const bool manualMips=requestsMips &&
        (!CoinRenderTextureSamplingCore::powerOfTwo(producer.size[0]) ||
         !CoinRenderTextureSamplingCore::powerOfTwo(producer.size[1]));
      const auto format=coinBgfxTextureFormat(producer.format);
      const auto * caps=sharedRuntime().references ? bgfx::getCaps() : nullptr;
      if(caps && (!(caps->formats[format]&BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
         (requestsMips && !manualMips && !(caps->formats[format]&BGFX_CAPS_FORMAT_TEXTURE_MIP_AUTOGEN))))
        return {CoinRenderBackendStatus::UNSUPPORTED,"BGFX RTT format/mip generation unavailable"};
      if(caps && manualMips &&
         !bgfx::isTextureValid(1, false, 1, format,
           BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
           BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT))
        return {CoinRenderBackendStatus::UNSUPPORTED,"BGFX NPOT mip GPU blit destination unavailable"};
      if (!producer.plan.shadowGroups.empty()) {
        CoinRenderShadowPlan shadowPlan;
        std::string diagnostic;
        if (!coin_render_plan_shadows(producer.plan, shadowPlan, diagnostic) ||
            !coin_render_shadow_object_profile(producer.plan, shadowPlan,
              shadowPlan.passes.size(), diagnostic))
          return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
        if (!bgfxShadowBatchSupported(producer.plan, shadowPlan, diagnostic))
          return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
        const bgfx::Caps * caps = sharedRuntime().references ? bgfx::getCaps() : nullptr;
        if (caps && caps->limits.maxTextureSamplers < (bgfxShadowMultitexture(producer.plan)?11:shadowPlan.passes.size() > 4 ? 16 : shadowPlan.passes.size() > 2 ? 12 : 10))
          return {CoinRenderBackendStatus::UNSUPPORTED,
                  "BGFX direct RTT shadow receiver has insufficient texture stages"};
        for (const auto & pass : shadowPlan.passes)
          if (caps && (pass.mapSize > caps->limits.maxTextureSize ||
              !(caps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
              !(caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
              !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA32F, shadowTextureFlags)))
            return {CoinRenderBackendStatus::UNSUPPORTED,
                    "BGFX direct RTT cannot render the planned shadow maps"};
      }
      std::vector<CoinRenderCompositionItem> order;
      std::string diagnostic;
      if (!coin_render_composition_order(producer.plan, order, diagnostic))
        return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
      bool peeling = false, weighted = false;
      for (const auto& item : order) {
        // The Coin mode can name peeling on an opaque draw; Core only defers
        // effective transparency. Match the execution strategy's admission.
        if (!item.blend || !item.deferred) continue;
        peeling = peeling || item.transparencyStrategy == CoinRenderCompositionItem::SORTED_LAYERS;
        weighted = weighted || item.transparencyStrategy == CoinRenderCompositionItem::WEIGHTED_OIT;
      }
      // The graph is inspected once before prepareBackend() and once after it.
      // A peer target may already own the shared runtime, but this backend's
      // capability flags are populated only when this target is prepared.
      if (this->initialized &&
          ((peeling && !this->sortedLayersSupported) ||
           (weighted && !this->weightedOitSupported)))
        return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX direct RTT transparency is unavailable"};
      auto options = producer.plan.transparency;
      if (weighted && !peeling) options.layers = 1;
      uint64_t requiredBytes = 0;
      if (!coin_render_transparency_sample_budget(producer.size[0], producer.size[1], options,
            peeling || weighted, this->sampleCount, requiredBytes, diagnostic))
        return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
    }
  }
  // Compile every consumed module before the first producer can publish or
  // submit. Lowering here owns only CPU snapshots; no texture upload, scene
  // draw, presentation or readback is issued by this preparation.
  if (this->initialized) {
    auto *backend = const_cast<CoinBgfxBackend *>(this);
    backend->releasePortablePreflightPins();
    PortablePreflightRelease releaseOnFailure{backend};
    const auto prepareModules = [&](const CoinRenderFramePlan &frame,
                                    const SbVec2i32 &extent, bool window, bgfx::TextureFormat::Enum colorFormat) {
      if (!frame.hasPortableShaderDraws()) return true;
      CoinBgfxPlan plan;
      const bool shadows = !frame.shadowGroups.empty();
      if (!CoinBgfxLowering::lowerPortablePreflight(frame, extent[0], extent[1], bgfx::getCaps()->homogeneousDepth,
                                                  plan, backend->lastError, shadows)) return false;
      CoinBgfxTransparencyStrategy strategy;
      if (!CoinBgfxLowering::selectTransparencyStrategy(plan.draws, backend->transparencyMode,
            backend->weightedOitSupported, backend->sortedLayersSupported, strategy, backend->lastError)) return false;
      if (!backend->prepareTransparencyPrograms(strategy, bgfxNeedsFixedSurfacePrograms(plan.draws)) ||
          !backend->prepareBaseProgram(plan.draws, shadows, strategy, false, window && frame.numPasses == 1,
                                      colorFormat)) return false;
      return backend->preparePortablePrograms(frame, plan.draws, shadows, strategy, window && frame.numPasses == 1, colorFormat, true);
    };
    if (!prepareModules(root, size, this->presentToWindow && !root.viewOutput, coinBgfxTextureFormat(root.outputColorFormat)))
      return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
    for (const auto &producer : graph.producers)
      if (!prepareModules(producer.plan, producer.size, false, coinBgfxTextureFormat(producer.format)))
        return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
    releaseOnFailure.owner = nullptr;
  }

  return {};
}
