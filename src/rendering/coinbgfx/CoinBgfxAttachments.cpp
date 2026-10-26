// CoinRender BGFX Attachments responsibilities.
#include "rendering/coinbgfx/CoinBgfxBackendInternal.h"
#include "coin_bgfx_fs_msaa_resolve_glsl.h"
#include "coin_bgfx_fs_msaa_resolve_spirv.h"
#include "coin_bgfx_fs_weighted_composite_msaa_glsl.h"
#include "coin_bgfx_fs_weighted_composite_msaa_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_msaa_resolve_dx11.h"
#include "coin_bgfx_fs_weighted_composite_msaa_dx11.h"
#endif
#include "coin_bgfx_fs_depth_readback_glsl.h"
#include "coin_bgfx_fs_depth_readback_spirv.h"
#include "coin_bgfx_fs_mip_area_glsl.h"
#include "coin_bgfx_fs_mip_area_spirv.h"
#include "coin_bgfx_vs_glsl.h"
#include "coin_bgfx_vs_spirv.h"
#include "coin_bgfx_fs_peel_next_msaa_glsl.h"
#include "coin_bgfx_fs_peel_next_msaa_spirv.h"
#include "coin_bgfx_fs_peel_msaa_composite_glsl.h"
#include "coin_bgfx_fs_peel_msaa_composite_spirv.h"
#include "coin_bgfx_fs_peel_next_msaa_portable_glsl.h"
#include "coin_bgfx_fs_peel_next_msaa_portable_spirv.h"
#include "coin_bgfx_fs_peel_next_glsl.h"
#include "coin_bgfx_fs_peel_next_spirv.h"
#include "coin_bgfx_fs_composite_glsl.h"
#include "coin_bgfx_fs_composite_spirv.h"
#include "coin_bgfx_fs_composite_depth_glsl.h"
#include "coin_bgfx_fs_composite_depth_spirv.h"
#include "coin_bgfx_fs_weighted_oit_glsl.h"
#include "coin_bgfx_fs_weighted_oit_spirv.h"
#include "coin_bgfx_fs_weighted_composite_glsl.h"
#include "coin_bgfx_fs_weighted_composite_spirv.h"
#include "coin_bgfx_fs_peel_next_portable_glsl.h"
#include "coin_bgfx_fs_weighted_oit_portable_glsl.h"
#include "coin_bgfx_fs_peel_next_portable_spirv.h"
#include "coin_bgfx_fs_weighted_oit_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_peel_next_portable_dx11.h"
#include "coin_bgfx_fs_weighted_oit_portable_dx11.h"
#endif
#ifdef _WIN32
#include "coin_bgfx_fs_peel_next_msaa_dx11.h"
#include "coin_bgfx_fs_peel_msaa_composite_dx11.h"
#include "coin_bgfx_fs_peel_next_msaa_portable_dx11.h"
#include "coin_bgfx_fs_depth_readback_dx11.h"
#include "coin_bgfx_fs_mip_area_dx11.h"
#include "coin_bgfx_vs_dx11.h"
#include "coin_bgfx_fs_peel_next_dx11.h"
#include "coin_bgfx_fs_composite_dx11.h"
#include "coin_bgfx_fs_composite_depth_dx11.h"
#include "coin_bgfx_fs_weighted_oit_dx11.h"
#include "coin_bgfx_fs_weighted_composite_dx11.h"
#endif

using namespace coin_bgfx_internal;

bool
CoinBgfxBackend::prepareNpotMipProgram()
{
  if (!this->prepareFullscreenResources()) return false;
  if (!bgfx::isValid(this->npotMipProgram))
    this->npotMipProgram = createLayerProgram(
      COIN_BGFX_SHADER_DATA(coin_bgfx_vs),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_mip_area),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_mip_area));
  if (!bgfx::isValid(this->npotMipSampler))
    this->npotMipSampler = bgfx::createUniform("s_mipSource", bgfx::UniformType::Sampler);
  if (!bgfx::isValid(this->npotMipExtentUniform))
    this->npotMipExtentUniform = bgfx::createUniform("u_mipExtent", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->npotMipProgram) ||
      !bgfx::isValid(this->npotMipSampler) ||
      !bgfx::isValid(this->npotMipExtentUniform)) {
    this->lastError = "BGFX could not prepare the NPOT area reduction program";
    return false;
  }
  return true;
}

CoinRenderSubmitResult
CoinBgfxBackend::msaaProfile(const CoinRenderFramePlan & frame) const
{
  if (this->sampleCount == 1) return {};
  if (this->sampleCount != 2 && this->sampleCount != 4)
    return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX MSAA requires exactly 1, 2 or 4 samples"};
  if (frame.outputColorFormat != CoinRenderTextureFormat::RGBA8_LINEAR ||
      frame.rasterizationPolicy != COIN_RENDER_RASTER_NATIVE)
    return {CoinRenderBackendStatus::UNSUPPORTED,
      "BGFX MSAA requires native RGBA8 linear geometry"};
  std::vector<CoinRenderCompositionItem> order;
  std::string diagnostic;
  const bool customSampleDepth = std::any_of(frame.renderStates.begin(), frame.renderStates.end(), [](const CoinRenderRenderStateSnapshot & state) {
    return state.depthRange[0] != 0.0f || state.depthRange[1] != 1.0f ||
      (state.polygonOffsetEnabled && (state.polygonOffsetStyles & state.polygonOffsetPrimitiveStyle) &&
       (state.polygonOffsetFactor != 0.0f || state.polygonOffsetUnits != 0.0f || state.polygonOffsetSlopeBias != 0.0f));
  });
  if (customSampleDepth && this->initialized && !bgfxSampleInterpolationSupported(this->sampleCount, diagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  if (!coin_render_composition_order(frame, order, diagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};

  const bool weighted = std::any_of(order.begin(), order.end(), [](const CoinRenderCompositionItem & item) {
    return item.blend && item.deferred && !item.additive && !item.screenDoor &&
      item.transparencyStrategy == CoinRenderCompositionItem::WEIGHTED_OIT;
  });
  if (weighted && this->initialized &&
      !bgfxWeightedSamples(this->sampleCount, this->layout, this->viewBase, diagnostic, false))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  const bool peeling = std::any_of(order.begin(), order.end(), [](const CoinRenderCompositionItem & item) {
    return item.blend && item.deferred && !item.additive &&
      item.transparencyStrategy == CoinRenderCompositionItem::SORTED_LAYERS;
  });
  if (peeling && this->initialized &&
      !bgfxPeelingSamples(this->sampleCount, this->layout, this->viewBase, diagnostic, false))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  return {};
}

void
CoinBgfxBackend::encodeMsaaResolve(bgfx::ViewId view, bgfx::FrameBufferHandle output,
                                  int width, int height, float mode)
{
  // View ids are reused by RTT producers and the parent. Remove their prior
  // framebuffer/clear state before any sampling of those same attachments.
  bgfx::resetView(view);
  bgfx::setViewName(view, mode < .5f ? "msaa_sample_count" :
    mode < 1.5f ? "msaa_color_resolve" : "msaa_depth_min_resolve");
  bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(view, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
  bgfx::setViewFrameBuffer(view, output);
  bgfx::setViewClear(view, BGFX_CLEAR_NONE);
  bgfx::setViewTransform(view, nullptr, nullptr);
  const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  const float info[4] = {1.0f / width, 1.0f / height, mode, float(this->sampleCount)};
  bgfx::setTransform(identity);
  bgfx::setUniform(this->depthInfoUniform, info);
  bgfx::setTexture(0, this->msaaColorSampler, bgfx::getTexture(this->msaaFrameBuffer, 0));
  bgfx::setTexture(1, this->msaaDepthSampler, bgfx::getTexture(this->msaaFrameBuffer, 1));
  bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
  bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
  bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
  bgfx::submit(view, this->msaaProgram);
}

CoinRenderSubmitResult
CoinBgfxBackend::prepareMsaa(int width, int height)
{
  if (this->sampleCount == 1) return {};
  const auto * caps = bgfx::getCaps();
  if (!caps || (caps->rendererType != bgfx::RendererType::Vulkan &&
                caps->rendererType != bgfx::RendererType::OpenGL))
    return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX MSAA sample-count validation requires Vulkan or OpenGL"};
  const uint64_t flags = BGFX_TEXTURE_MSAA_SAMPLE |
    (this->sampleCount == 2 ? BGFX_TEXTURE_RT_MSAA_X2 : BGFX_TEXTURE_RT_MSAA_X4);
  for (auto format : {bgfx::TextureFormat::RGBA8, bgfx::TextureFormat::D32F})
    if (!(caps->formats[format] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA) ||
        !bgfx::isTextureValid(1, false, 1, format, flags))
      return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX MSAA needs sampleable RGBA8 and D32F attachments"};
  if (!(caps->formats[bgfx::TextureFormat::R32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
      !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::R32F,
                            BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK))
    return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX MSAA requires exact R32F probe/depth readback"};
  if (width <= 0 || height <= 0 || width > caps->limits.maxTextureSize ||
      height > caps->limits.maxTextureSize ||
      uint64_t(width) * uint64_t(height) * 8 * this->sampleCount > 128ull * 1024 * 1024)
    return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX MSAA attachments exceed dimensions or 128 MiB"};
  if (bgfx::isValid(this->msaaFrameBuffer) && this->msaaWidth == width && this->msaaHeight == height)
    return {};
  if (!this->prepareFullscreenResources())
    return {CoinRenderBackendStatus::BACKEND_ERROR, this->lastError};
  // Raw GLSL has no shaderc uniform table: ProgramGL discovers uniforms at
  // link time, so their registry entries must precede the program command.
  if (!bgfx::isValid(this->msaaColorSampler))
    this->msaaColorSampler = bgfx::createUniform("s_msaaColor", bgfx::UniformType::Sampler);
  if (!bgfx::isValid(this->msaaDepthSampler))
    this->msaaDepthSampler = bgfx::createUniform("s_msaaDepth", bgfx::UniformType::Sampler);
  if (!bgfx::isValid(this->msaaProgram))
    this->msaaProgram = createLayerProgram(
      COIN_BGFX_SHADER_DATA(coin_bgfx_vs), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_msaa_resolve), COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_msaa_resolve));
  if (!bgfx::isValid(this->msaaProgram) || !bgfx::isValid(this->msaaColorSampler) ||
      !bgfx::isValid(this->msaaDepthSampler))
    return {CoinRenderBackendStatus::BACKEND_ERROR, "BGFX MSAA resolve program allocation failed"};
  if (bgfx::isValid(this->msaaFrameBuffer)) bgfx::destroy(this->msaaFrameBuffer);
  this->msaaFrameBuffer = BGFX_INVALID_HANDLE;
  this->msaaWidth = this->msaaHeight = 0;
  bgfx::TextureHandle color = bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::RGBA8, flags);
  bgfx::TextureHandle depth = bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::D32F, flags);
  if (bgfx::isValid(color) && bgfx::isValid(depth)) {
    const bgfx::TextureHandle attachments[2] = {color, depth};
    this->msaaFrameBuffer = bgfx::createFrameBuffer(2, attachments, true);
  }
  if (!bgfx::isValid(this->msaaFrameBuffer)) {
    if (bgfx::isValid(color)) bgfx::destroy(color);
    if (bgfx::isValid(depth)) bgfx::destroy(depth);
    return {CoinRenderBackendStatus::OUT_OF_MEMORY, "BGFX MSAA framebuffer allocation failed"};
  }
  const auto probe = bgfx::createFrameBuffer(1, 1, bgfx::TextureFormat::R32F, peelTextureFlags);
  const auto staging = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::R32F,
    BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
  if (!bgfx::isValid(probe) || !bgfx::isValid(staging)) {
    if (bgfx::isValid(probe)) bgfx::destroy(probe);
    if (bgfx::isValid(staging)) bgfx::destroy(staging);
    bgfx::destroy(this->msaaFrameBuffer); this->msaaFrameBuffer = BGFX_INVALID_HANDLE;
    return {CoinRenderBackendStatus::OUT_OF_MEMORY, "BGFX MSAA sample-count probe allocation failed"};
  }
  // Materialize all programs before the probe, and before any scene command.
  // A rejected effective count has never encoded or published scene geometry.
  bgfx::frame(BGFX_FRAME_FLUSH);
  sharedRuntime().programCommandsMaterialized = sharedRuntime().programCommandsQueued;
  this->encodeMsaaResolve(this->viewBase, probe, 1, 1, 0.0f);
  bgfx::resetView(this->viewBase + 1);
  bgfx::TextureRegion destination, source;
  destination.handle = staging; source.handle = bgfx::getTexture(probe);
  bgfx::blit(this->viewBase + 1, destination, source);
  float effective = -1.0f;
  bgfx::TextureRegion read;
  read.handle = staging;
  const uint32_t ready = bgfx::read(read, &effective);
  uint32_t completed = bgfx::frame();
  while (static_cast<int32_t>(completed - ready) < 0) completed = bgfx::frame();
  bgfx::resetView(this->viewBase); bgfx::resetView(this->viewBase + 1);
  bgfx::destroy(probe); bgfx::destroy(staging);
  const auto runtimeStatus = this->checkRuntimeFailure("BGFX MSAA sample-count verification failed");
  if (runtimeStatus != CoinRenderBackendStatus::SUCCESS || effective != float(this->sampleCount)) {
    bgfx::destroy(this->msaaFrameBuffer); this->msaaFrameBuffer = BGFX_INVALID_HANDLE;
    if (runtimeStatus != CoinRenderBackendStatus::SUCCESS) return {runtimeStatus, this->lastError};
    return {CoinRenderBackendStatus::UNSUPPORTED,
      "BGFX did not allocate the exact requested RGBA8/D32F MSAA sample count"};
  }
  this->msaaWidth = width; this->msaaHeight = height;
  if (CoinRenderDiagnosticShell::phaseTracingEnabled())
    std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_msaa requested=%u effective=%.0f width=%d height=%d\n",
      this->sampleCount, effective, width, height);
  return {};
}

bool
CoinBgfxBackend::prepareDepthReadbackResources(bool staging)
{
  if (!this->prepareFullscreenResources()) return false;
  if (!bgfx::isValid(this->depthReadProgram))
    this->depthReadProgram = createLayerProgram(
      COIN_BGFX_SHADER_DATA(coin_bgfx_vs), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_depth_readback), COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_depth_readback));
  if (!bgfx::isValid(this->readDepthSampler))
    this->readDepthSampler = bgfx::createUniform("s_readDepth", bgfx::UniformType::Sampler);
  if (!bgfx::isValid(this->depthReadFrameBuffer))
    this->depthReadFrameBuffer = bgfx::createFrameBuffer(
      static_cast<uint16_t>(this->width), static_cast<uint16_t>(this->height),
      bgfx::TextureFormat::R32F, peelTextureFlags);
  if (!bgfx::isValid(this->depthReadProgram) || !bgfx::isValid(this->readDepthSampler) ||
      !bgfx::isValid(this->depthReadFrameBuffer)) {
    this->lastError = "BGFX depth readback resource allocation failed";
    this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    return false;
  }
  for (ReadbackSlot & slot : this->readbackSlots) {
    // Async tickets own their staging. In particular they must not resize a
    // pending ring slot's CPU destination while BGFX still writes into it.
    if (!staging || slot.pending) continue;
    if (!bgfx::isValid(slot.depthTexture))
      slot.depthTexture = bgfx::createTexture2D(this->width, this->height, false, 1,
        bgfx::TextureFormat::R32F, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    if (!bgfx::isValid(slot.depthTexture)) {
      this->lastError = "BGFX depth readback staging allocation failed";
      this->status = CoinRenderBackendStatus::OUT_OF_MEMORY;
      return false;
    }
    slot.depth.resize(size_t(this->width) * this->height);
  }
  return true;
}

bool
CoinBgfxBackend::prepareTransparencyPrograms(CoinBgfxTransparencyStrategy strategy, bool surfaceExecutors)
{
  if (strategy != CoinBgfxTransparencyStrategy::OBJECT && !this->prepareFullscreenResources()) return false;
  if (this->presentToWindow &&
      (this->multipassSceneRequired || bgfx::getCaps()->rendererType == bgfx::RendererType::OpenGL ||
       strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS)) {
    if (!this->prepareFullscreenResources()) return false;
    if (!bgfx::isValid(this->compositeProgram))
      this->compositeProgram = createLayerProgram(
        COIN_BGFX_SHADER_DATA(coin_bgfx_vs), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
        COIN_BGFX_SHADER_DATA(coin_bgfx_fs_composite), COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_composite));
    if (!bgfx::isValid(this->layerSampler))
      this->layerSampler = bgfx::createUniform("s_layer", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->compositeProgram) || !bgfx::isValid(this->layerSampler)) {
      this->lastError = "BGFX OpenGL could not prepare window presentation";
      return false;
    }
  }
  if (strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS) {
    if (this->sampleCount > 1) {
      if (!bgfx::isValid(this->peelMsaaColorSampler)) this->peelMsaaColorSampler = bgfx::createUniform("s_peelColorMS", bgfx::UniformType::Sampler);
      if (!bgfx::isValid(this->peelMsaaMaskSampler)) this->peelMsaaMaskSampler = bgfx::createUniform("s_peelMaskMS", bgfx::UniformType::Sampler);
      if (!bgfx::isValid(this->peelMsaaDepthSampler)) this->peelMsaaDepthSampler = bgfx::createUniform("s_peelDepthMS", bgfx::UniformType::Sampler);
      if (surfaceExecutors && !bgfx::isValid(this->peelMsaaProgram)) this->peelMsaaProgram = createLayerProgram(
        COIN_BGFX_SHADER_DATA(coin_bgfx_vs), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
        COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_peel_next_msaa), COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_peel_next_msaa));
      if (!bgfx::isValid(this->peelMsaaCompositeProgram)) this->peelMsaaCompositeProgram = createLayerProgram(
        COIN_BGFX_SHADER_DATA(coin_bgfx_vs), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
        COIN_BGFX_SHADER_DATA(coin_bgfx_fs_peel_msaa_composite), COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_peel_msaa_composite));
      if ((surfaceExecutors && !bgfx::isValid(this->peelMsaaProgram)) || !bgfx::isValid(this->peelMsaaCompositeProgram) ||
          !bgfx::isValid(this->peelMsaaColorSampler) || !bgfx::isValid(this->peelMsaaMaskSampler) || !bgfx::isValid(this->peelMsaaDepthSampler)) {
        this->lastError = "BGFX MSAA peeling program allocation failed";
        return false;
      }
    }
    const uint8_t * vertexShader = COIN_BGFX_SHADER_DATA(coin_bgfx_vs);
    const uint32_t vertexBytes = COIN_BGFX_SHADER_SIZE(coin_bgfx_vs);
    if (surfaceExecutors && !bgfx::isValid(this->peelNextProgram)) this->peelNextProgram = createLayerProgram(vertexShader, vertexBytes,
      COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_peel_next),
      COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_peel_next));
    if (!bgfx::isValid(this->compositeProgram)) this->compositeProgram = createLayerProgram(vertexShader, vertexBytes,
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_composite),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_composite));
    if (!bgfx::isValid(this->compositeDepthProgram)) this->compositeDepthProgram = createLayerProgram(vertexShader, vertexBytes,
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_composite_depth),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_composite_depth));
    if (!bgfx::isValid(this->previousDepthSampler)) this->previousDepthSampler = bgfx::createUniform("s_prevDepth", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->opaqueDepthSampler)) this->opaqueDepthSampler = bgfx::createUniform("s_opaqueDepth", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->layerSampler)) this->layerSampler = bgfx::createUniform("s_layer", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->layerMaskSampler)) this->layerMaskSampler = bgfx::createUniform("s_layerMask", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->layerDepthSampler)) this->layerDepthSampler = bgfx::createUniform("s_layerDepth", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->peelMetaUniform)) this->peelMetaUniform = bgfx::createUniform("u_peelMeta", bgfx::UniformType::Vec4);
    if ((surfaceExecutors && !bgfx::isValid(this->peelNextProgram)) ||
        !bgfx::isValid(this->compositeProgram) ||
        !bgfx::isValid(this->compositeDepthProgram) ||
        !bgfx::isValid(this->previousDepthSampler) ||
        !bgfx::isValid(this->opaqueDepthSampler) ||
        !bgfx::isValid(this->layerSampler) ||
        !bgfx::isValid(this->layerMaskSampler) ||
        !bgfx::isValid(this->layerDepthSampler) ||
        !bgfx::isValid(this->peelMetaUniform) ||
        !bgfx::isValid(this->depthInfoUniform) ||
        !bgfx::isValid(this->fullscreenVertexBuffer) ||
        !bgfx::isValid(this->fullscreenIndexBuffer)) {
      this->lastError = "BGFX could not allocate sorted-layers programs and uniforms";
      this->status = CoinRenderBackendStatus::BACKEND_ERROR;
      return false;
    }
  }
  if (strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT) {
    const uint8_t * vertexShader = COIN_BGFX_SHADER_DATA(coin_bgfx_vs);
    const uint32_t vertexBytes = COIN_BGFX_SHADER_SIZE(coin_bgfx_vs);
    if (this->sampleCount > 1) {
      if (!bgfx::isValid(this->weightedMsaaAccumSampler))
        this->weightedMsaaAccumSampler = bgfx::createUniform("s_oitAccumMS", bgfx::UniformType::Sampler);
      if (!bgfx::isValid(this->weightedMsaaRevealSampler))
        this->weightedMsaaRevealSampler = bgfx::createUniform("s_oitRevealMS", bgfx::UniformType::Sampler);
      if (!bgfx::isValid(this->weightedMsaaCompositeProgram))
        this->weightedMsaaCompositeProgram = createLayerProgram(vertexShader, vertexBytes,
          COIN_BGFX_SHADER_DATA(coin_bgfx_fs_weighted_composite_msaa),
          COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_weighted_composite_msaa));
      if (!bgfx::isValid(this->weightedMsaaCompositeProgram) ||
          !bgfx::isValid(this->weightedMsaaAccumSampler) || !bgfx::isValid(this->weightedMsaaRevealSampler)) {
        this->lastError = "BGFX could not allocate per-sample weighted composition";
        return false;
      }
    }
    if (surfaceExecutors && !bgfx::isValid(this->weightedOitProgram)) this->weightedOitProgram = createLayerProgram(vertexShader, vertexBytes,
      COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_weighted_oit),
      COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_weighted_oit));
    if (!bgfx::isValid(this->weightedCompositeProgram)) this->weightedCompositeProgram = createLayerProgram(vertexShader, vertexBytes,
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_weighted_composite),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_weighted_composite));
    if (!bgfx::isValid(this->oitAccumSampler)) this->oitAccumSampler = bgfx::createUniform("s_oitAccum", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->oitRevealSampler)) this->oitRevealSampler = bgfx::createUniform("s_oitReveal", bgfx::UniformType::Sampler);
    if ((surfaceExecutors && !bgfx::isValid(this->weightedOitProgram)) ||
        !bgfx::isValid(this->weightedCompositeProgram) ||
        !bgfx::isValid(this->oitAccumSampler) ||
        !bgfx::isValid(this->oitRevealSampler) ||
        !bgfx::isValid(this->depthInfoUniform) ||
        !bgfx::isValid(this->fullscreenVertexBuffer) ||
        !bgfx::isValid(this->fullscreenIndexBuffer)) {
      char detail[192];
      std::snprintf(detail, sizeof(detail),
        "BGFX weighted-OIT resources failed: draw=%d composite=%d accum=%d reveal=%d depthInfo=%d vb=%d ib=%d",
        bgfx::isValid(this->weightedOitProgram) ? 1 : 0,
        bgfx::isValid(this->weightedCompositeProgram) ? 1 : 0,
        bgfx::isValid(this->oitAccumSampler) ? 1 : 0,
        bgfx::isValid(this->oitRevealSampler) ? 1 : 0,
        bgfx::isValid(this->depthInfoUniform) ? 1 : 0,
        bgfx::isValid(this->fullscreenVertexBuffer) ? 1 : 0,
        bgfx::isValid(this->fullscreenIndexBuffer) ? 1 : 0);
      this->lastError = detail;
      this->status = CoinRenderBackendStatus::BACKEND_ERROR;
      return false;
    }
  }
  return true;
}

void
CoinBgfxBackend::destroyFrameBuffers(bool keepOutput)
{
  for (uint8_t pass = 0; pass < COIN_RENDER_MAX_PEEL_LAYERS; ++pass) {
    if (bgfx::isValid(this->peelFrameBuffers[pass]))
      bgfx::destroy(this->peelFrameBuffers[pass]);
    this->peelFrameBuffers[pass] = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(this->oitFrameBuffer)) bgfx::destroy(this->oitFrameBuffer);
  this->oitFrameBuffer = BGFX_INVALID_HANDLE;
  // Transparency policy changes do not replace the target's attachments.
  if (keepOutput) return;
  this->sceneDepthInitialized = false;
  if (bgfx::isValid(this->readbackTexture)) bgfx::destroy(this->readbackTexture);
  for (ReadbackSlot & slot : this->readbackSlots) {
    // CPU destinations must survive queued GPU writes, including resize.
    if (slot.pending) {
      uint32_t completed = bgfx::frame();
      while (static_cast<int32_t>(completed - slot.readyFrame) < 0)
        completed = bgfx::frame();
    }
    if (bgfx::isValid(slot.texture)) bgfx::destroy(slot.texture);
    if (bgfx::isValid(slot.depthTexture)) bgfx::destroy(slot.depthTexture);
  }
  if (bgfx::isValid(this->msaaFrameBuffer)) bgfx::destroy(this->msaaFrameBuffer);
  this->msaaFrameBuffer = BGFX_INVALID_HANDLE;
  this->msaaWidth = this->msaaHeight = 0;
  if (bgfx::isValid(this->depthReadFrameBuffer)) bgfx::destroy(this->depthReadFrameBuffer);
  this->depthReadFrameBuffer = BGFX_INVALID_HANDLE;
  this->lastPublishedDepth.clear();
  this->readbackSlots.clear();
  this->lastPublishedReadback.clear();
  this->readbackCursor = 0;
  this->readbackSequence = 0;
  this->lastPublishedSequence = 0;
  if (bgfx::isValid(this->frameBuffer)) bgfx::destroy(this->frameBuffer);
  if (bgfx::isValid(this->windowSceneFrameBuffer)) bgfx::destroy(this->windowSceneFrameBuffer);
  this->readbackTexture = BGFX_INVALID_HANDLE;
  this->frameBuffer = BGFX_INVALID_HANDLE;
  this->windowSceneFrameBuffer = BGFX_INVALID_HANDLE;
  this->width = this->height = 0;
}

bool
CoinBgfxBackend::resize(int newWidth, int newHeight, bool multipassFrame)
{
  const bool keepOutput = newWidth == this->width && newHeight == this->height &&
      !(this->presentToWindow && this->multipassSceneRequired && !bgfx::isValid(this->windowSceneFrameBuffer));
  const bool transparencyReady =
    (this->activeTransparencyStrategy != CoinBgfxTransparencyStrategy::WEIGHTED_OIT ||
      bgfx::isValid(this->oitFrameBuffer)) &&
    (this->activeTransparencyStrategy != CoinBgfxTransparencyStrategy::SORTED_LAYERS ||
      (this->peelPassCount && bgfx::isValid(this->peelFrameBuffers[this->peelPassCount - 1])));
  if (keepOutput && transparencyReady) return true;
  if (newWidth <= 0 || newHeight <= 0 || newWidth > 16384 || newHeight > 16384) {
    this->lastError = "Invalid BGFX target dimensions";
    return false;
  }
  if (consumeTestFault("COIN_BGFX_TEST_DEVICE_LOST_ON_RESIZE_ONCE")) {
#ifdef COIN_WGPU_TEST_HOOKS
    static_cast<CoinBgfxCallback *>(this->callback.get())->inject(
      bgfx::Fatal::DeviceLost, "injected device loss during resize");
#endif
    this->checkRuntimeFailure("BGFX resize failed");
    return false;
  }
  const bool existingWindow = this->presentToWindow && bgfx::isValid(this->frameBuffer);
  const bool glWindowMsaa = this->presentToWindow && this->sampleCount > 1 &&
    bgfx::getCaps()->rendererType == bgfx::RendererType::OpenGL;
  // Resolve multisample textures in the producing context. Only the resolved
  // single-sample color crosses into the secondary native-window context.
  const bool prepareWindowScene = this->presentToWindow &&
    (this->multipassSceneRequired || ((existingWindow || glWindowMsaa) &&
    (bgfx::getCaps()->rendererType == bgfx::RendererType::OpenGL ||
     this->activeTransparencyStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS)));
  if (glWindowMsaa && uint64_t(newWidth) * uint64_t(newHeight) *
        (8ull * this->sampleCount + 8ull) > 128ull * 1024 * 1024) {
    this->lastError = "BGFX MSAA and window resolve attachments exceed 128 MiB";
    return false;
  }
  // Preserve the native framebuffer handle across resize; only its surface
  // attachments are resized. OIT/peel attachments remain private to this target.
  const bgfx::FrameBufferHandle windowOutput = this->presentToWindow ?
    this->frameBuffer : bgfx::FrameBufferHandle BGFX_INVALID_HANDLE;
  if (this->presentToWindow && !keepOutput) this->frameBuffer = BGFX_INVALID_HANDLE;
  this->destroyFrameBuffers(keepOutput);
  if (this->presentToWindow && !keepOutput) this->frameBuffer = windowOutput;
  const bgfx::Caps * caps = bgfx::getCaps();
  const bgfx::TextureFormat::Enum depthFormat =
    !this->managerDepthMode && this->activeTransparencyStrategy != CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
    (caps->formats[bgfx::TextureFormat::D24S8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)
      ? bgfx::TextureFormat::D24S8 : bgfx::TextureFormat::D32F;
  if (!keepOutput) {
    if (this->presentToWindow) {
      bgfx::SwapChain swapChain;
      swapChain.ndt = this->nativeDisplay;
      swapChain.nwh = this->nativeWindow;
      swapChain.width = static_cast<uint32_t>(newWidth);
      swapChain.height = static_cast<uint32_t>(newHeight);
      // Let BGFX select the native surface format. Vulkan Xlib commonly uses
      // BGRA8; forcing RGBA8 reverses channels on drivers with a BGRA-only WSI.
      swapChain.formatColor = bgfx::TextureFormat::Count;
      swapChain.formatDepthStencil = depthFormat;
      if (bgfx::isValid(this->frameBuffer)) {
        bgfx::updateSwapChain(this->frameBuffer, swapChain);
      } else {
        this->frameBuffer = bgfx::createFrameBuffer(swapChain);
        if (!bgfx::isValid(this->frameBuffer)) {
          this->lastError = "BGFX could not create the target window swapchain";
          return false;
        }
      }
      if (prepareWindowScene) {
        bgfx::TextureHandle color = bgfx::createTexture2D(
          static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
          false, 1, bgfx::TextureFormat::RGBA8, peelTextureFlags);
        bgfx::TextureHandle depth = bgfx::createTexture2D(
          static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
          false, 1, depthFormat, peelTextureFlags);
        if (!bgfx::isValid(color) || !bgfx::isValid(depth)) {
          if (bgfx::isValid(color)) bgfx::destroy(color);
          if (bgfx::isValid(depth)) bgfx::destroy(depth);
          this->lastError = "BGFX OpenGL could not allocate the window scene target";
          return false;
        }
        const bgfx::TextureHandle attachments[2] = {color, depth};
        this->windowSceneFrameBuffer = bgfx::createFrameBuffer(2, attachments, true);
        if (!bgfx::isValid(this->windowSceneFrameBuffer)) {
          bgfx::destroy(color); bgfx::destroy(depth);
          this->lastError = "BGFX OpenGL could not create the window scene target";
          return false;
        }
      }
    } else {
      bgfx::TextureHandle color = bgfx::createTexture2D(
        static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
        false, 1, bgfx::TextureFormat::RGBA8, peelTextureFlags);
      bgfx::TextureHandle depth = bgfx::createTexture2D(
        static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
        false, 1, depthFormat, peelTextureFlags);
      if (!bgfx::isValid(color) || !bgfx::isValid(depth)) {
        if (bgfx::isValid(color)) bgfx::destroy(color);
        if (bgfx::isValid(depth)) bgfx::destroy(depth);
        this->lastError = "BGFX could not allocate offscreen color/depth textures";
        return false;
      }
      const bgfx::TextureHandle attachments[2] = {color, depth};
      this->frameBuffer = bgfx::createFrameBuffer(2, attachments, true);
      if (!bgfx::isValid(this->frameBuffer)) {
        bgfx::destroy(color);
        bgfx::destroy(depth);
        this->lastError = "BGFX could not create an offscreen color/depth framebuffer";
        return false;
      }
      const size_t readbackBytes = static_cast<size_t>(newWidth) * newHeight * 4u;
      this->readbackSlots.resize(this->readbackPipelineDepth);
      for (ReadbackSlot & slot : this->readbackSlots) {
        slot.texture = bgfx::createTexture2D(static_cast<uint16_t>(newWidth),
          static_cast<uint16_t>(newHeight), false, 1, bgfx::TextureFormat::RGBA8,
          BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        slot.pixels.resize(readbackBytes);
        if (!bgfx::isValid(slot.texture)) {
          this->lastError = "BGFX could not allocate the offscreen readback staging ring";
          this->destroyFrameBuffers();
          return false;
        }
      }
    }
  }
  if (this->activeTransparencyStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS) {
    for (uint8_t pass = 0; pass < this->peelPassCount; ++pass) {
      bgfx::TextureHandle color =
          bgfx::createTexture2D(static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
                                false, 1, bgfx::TextureFormat::RGBA16F, weightedTextureFlags(this->sampleCount));
      bgfx::TextureHandle mask = bgfx::createTexture2D(
        static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
        false, 1, bgfx::TextureFormat::R16F, weightedTextureFlags(this->sampleCount));
      bgfx::TextureHandle depth = bgfx::createTexture2D(
        static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
        false, 1, bgfx::TextureFormat::D32F, weightedTextureFlags(this->sampleCount));
      if (!bgfx::isValid(color) || !bgfx::isValid(mask) || !bgfx::isValid(depth)) {
        if (bgfx::isValid(color)) bgfx::destroy(color);
        if (bgfx::isValid(mask)) bgfx::destroy(mask);
        if (bgfx::isValid(depth)) bgfx::destroy(depth);
        this->lastError = "BGFX could not allocate sorted-layer textures";
        this->destroyFrameBuffers();
        return false;
      }
      const bgfx::TextureHandle attachments[3] = {color, mask, depth};
      this->peelFrameBuffers[pass] = bgfx::createFrameBuffer(3, attachments, true);
      if (!bgfx::isValid(this->peelFrameBuffers[pass])) {
        bgfx::destroy(color);
        bgfx::destroy(mask);
        bgfx::destroy(depth);
        this->lastError = "BGFX could not create sorted-layer framebuffer";
        this->destroyFrameBuffers();
        return false;
      }
    }
  }
  if (this->activeTransparencyStrategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT) {
    bgfx::TextureHandle accum = bgfx::createTexture2D(
      static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
      false, 1, bgfx::TextureFormat::RGBA16F, weightedTextureFlags(this->sampleCount));
    bgfx::TextureHandle reveal = bgfx::createTexture2D(
      static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
      false, 1, bgfx::TextureFormat::R16F, weightedTextureFlags(this->sampleCount));
    bgfx::TextureHandle depth = bgfx::createTexture2D(
      static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),

      false, 1, this->sampleCount > 1 ? bgfx::TextureFormat::D32F : depthFormat,
      this->sampleCount > 1 ? weightedTextureFlags(this->sampleCount) : BGFX_TEXTURE_RT_WRITE_ONLY);
    if (!bgfx::isValid(accum) || !bgfx::isValid(reveal) || !bgfx::isValid(depth)) {
      if (bgfx::isValid(accum)) bgfx::destroy(accum);
      if (bgfx::isValid(reveal)) bgfx::destroy(reveal);
      if (bgfx::isValid(depth)) bgfx::destroy(depth);
      this->lastError = "BGFX could not allocate weighted-OIT textures";
      this->destroyFrameBuffers();
      return false;
    }
    const bgfx::TextureHandle attachments[3] = {accum, reveal, depth};
    this->oitFrameBuffer = bgfx::createFrameBuffer(3, attachments, true);
    if (!bgfx::isValid(this->oitFrameBuffer)) {
      bgfx::destroy(accum);
      bgfx::destroy(reveal);
      bgfx::destroy(depth);
      this->lastError = "BGFX could not create weighted-OIT framebuffer";
      this->destroyFrameBuffers();
      return false;
    }
  }
  this->width = newWidth;
  this->height = newHeight;
  // Persistent manager depth requires private attachments, but does not
  // require a separate presentation of the native drawable. Keep the existing
  // preparation boundary only for the paths which already needed it.
  const bool prepareDrawable = multipassFrame ||
    caps->rendererType == bgfx::RendererType::OpenGL ||
    this->activeTransparencyStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS;
  if (prepareWindowScene && existingWindow && prepareDrawable) {
    // Commit native drawable reconfiguration before the scene is rendered.
    // A touch presents no scene geometry and leaves public submission serials
    // unchanged. In particular, it does not retry a lost scene submission.
    bgfx::resetView(this->viewBase);
    bgfx::setViewName(this->viewBase, "window_resize_prepare");
    bgfx::setViewRect(this->viewBase, 0, 0, newWidth, newHeight);
    bgfx::setViewFrameBuffer(this->viewBase, this->frameBuffer);
    bgfx::setViewClear(this->viewBase, BGFX_CLEAR_NONE);
    bgfx::touch(this->viewBase);
    bgfx::frame();
    bgfx::resetView(this->viewBase);
    if (CoinRenderDiagnosticShell::phaseTracingEnabled())
      std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_window_resize_prepare size=%dx%d scene_draws=0\n",newWidth,newHeight);
  }
  return this->checkRuntimeFailure("BGFX resize failed") == CoinRenderBackendStatus::SUCCESS;
}
