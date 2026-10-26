// CoinRender BGFX Shadow responsibilities.
#include "rendering/coinbgfx/CoinBgfxBackendInternal.h"
#include "coin_bgfx_fs_msaa_resolve_glsl.h"
#include "coin_bgfx_fs_msaa_resolve_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_msaa_resolve_dx11.h"
#endif
#include "coin_bgfx_vs_glsl.h"
#include "coin_bgfx_vs_spirv.h"
#include "coin_bgfx_fs_shadow_peel_msaa_glsl.h"
#include "coin_bgfx_fs_shadow_peel_msaa_spirv.h"
#include "coin_bgfx_fs_shadow_peel_msaa_portable_glsl.h"
#include "coin_bgfx_fs_shadow_peel_msaa_portable_spirv.h"
#include "coin_bgfx_fs_composite_glsl.h"
#include "coin_bgfx_fs_composite_spirv.h"
#include "coin_bgfx_vs_shadow_moments_glsl.h"
#include "coin_bgfx_vs_shadow_moments_spirv.h"
#include "coin_bgfx_fs_shadow_moments_glsl.h"
#include "coin_bgfx_fs_shadow_moments_spirv.h"
#include "coin_bgfx_vs_shadow_receiver_glsl.h"
#include "coin_bgfx_vs_shadow_receiver_spirv.h"
#include "coin_bgfx_fs_shadow_receiver_glsl.h"
#include "coin_bgfx_fs_shadow_receiver_spirv.h"
#include "coin_bgfx_fs_shadow_receiver4_glsl.h"
#include "coin_bgfx_fs_shadow_receiver4_spirv.h"
#include "coin_bgfx_fs_shadow_receiver8_glsl.h"
#include "coin_bgfx_fs_shadow_receiver8_spirv.h"
#include "coin_bgfx_fs_shadow_peel_glsl.h"
#include "coin_bgfx_fs_shadow_peel_spirv.h"
#include "coin_bgfx_fs_shadow_oit_glsl.h"
#include "coin_bgfx_fs_shadow_oit_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_peel_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_peel_portable_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_peel_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_peel_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_shadow_multitexture_peel_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_peel_portable_dx11.h"
#endif
#include "coin_bgfx_fs_shadow_multitexture_peel_msaa_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_peel_msaa_portable_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_peel_msaa_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_peel_msaa_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_shadow_multitexture_peel_msaa_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_peel_msaa_portable_dx11.h"
#endif
#include "coin_bgfx_fs_shadow_multitexture_oit_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_oit_portable_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_oit_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_oit_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_shadow_multitexture_oit_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_oit_portable_dx11.h"
#endif
#include "coin_bgfx_fs_shadow_multitexture_oit_sample_depth_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_oit_sample_depth_portable_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_oit_sample_depth_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_oit_sample_depth_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_shadow_multitexture_oit_sample_depth_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_oit_sample_depth_portable_dx11.h"
#endif
#include "coin_bgfx_vs_shadow_multitexture_glsl.h"
#include "coin_bgfx_vs_shadow_multitexture_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_native_depth_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_native_depth_portable_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_sample_depth_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_sample_depth_portable_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_native_depth_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_native_depth_portable_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_sample_depth_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_sample_depth_portable_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_portable_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_vs_shadow_multitexture_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_native_depth_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_native_depth_portable_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_sample_depth_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_sample_depth_portable_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_portable_dx11.h"
#endif
#include "coin_bgfx_fs_shadow_receiver_portable_glsl.h"
#include "coin_bgfx_fs_shadow_receiver4_portable_glsl.h"
#include "coin_bgfx_fs_shadow_receiver8_portable_glsl.h"
#include "coin_bgfx_fs_shadow_peel_portable_glsl.h"
#include "coin_bgfx_fs_shadow_oit_portable_glsl.h"
#include "coin_bgfx_fs_shadow_receiver_portable_spirv.h"
#include "coin_bgfx_fs_shadow_receiver4_portable_spirv.h"
#include "coin_bgfx_fs_shadow_receiver8_portable_spirv.h"
#include "coin_bgfx_fs_shadow_peel_portable_spirv.h"
#include "coin_bgfx_fs_shadow_oit_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_shadow_receiver_portable_dx11.h"
#include "coin_bgfx_fs_shadow_receiver4_portable_dx11.h"
#include "coin_bgfx_fs_shadow_receiver8_portable_dx11.h"
#include "coin_bgfx_fs_shadow_peel_portable_dx11.h"
#include "coin_bgfx_fs_shadow_oit_portable_dx11.h"
#endif
#ifdef _WIN32
#include "coin_bgfx_fs_shadow_peel_msaa_dx11.h"
#include "coin_bgfx_fs_shadow_peel_msaa_portable_dx11.h"
#include "coin_bgfx_vs_dx11.h"
#include "coin_bgfx_fs_composite_dx11.h"
#include "coin_bgfx_vs_shadow_moments_dx11.h"
#include "coin_bgfx_fs_shadow_moments_dx11.h"
#include "coin_bgfx_vs_shadow_receiver_dx11.h"
#include "coin_bgfx_fs_shadow_receiver_dx11.h"
#include "coin_bgfx_fs_shadow_receiver4_dx11.h"
#include "coin_bgfx_fs_shadow_receiver8_dx11.h"
#include "coin_bgfx_fs_shadow_peel_dx11.h"
#include "coin_bgfx_fs_shadow_oit_dx11.h"
#endif

using namespace coin_bgfx_internal;

bool CoinBgfxBackend::prepareShadowResources()
{
  if (!bgfx::isValid(this->shadowModelViewUniform))
    this->shadowModelViewUniform =
        bgfx::createUniform("u_shadowModelView", bgfx::UniformType::Mat4);
  if (!bgfx::isValid(this->shadowClipModelViewUniform))
    this->shadowClipModelViewUniform =
        bgfx::createUniform("u_shadowClipModelView", bgfx::UniformType::Mat4);
  if (!bgfx::isValid(this->shadowDepthUniform))
    this->shadowDepthUniform = bgfx::createUniform("u_shadowDepth", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->shadowQualityUniform))
    this->shadowQualityUniform = bgfx::createUniform("u_shadowQuality", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->shadowFlatDiffuseUniform))
    this->shadowFlatDiffuseUniform =
        bgfx::createUniform("u_shadowFlatDiffuse", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->shadowFlatSpecularUniform))
    this->shadowFlatSpecularUniform =
        bgfx::createUniform("u_shadowFlatSpecular", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->shadowFlatParamsUniform))
    this->shadowFlatParamsUniform =
        bgfx::createUniform("u_shadowFlatParams", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->shadowLightIndicesUniform))
    this->shadowLightIndicesUniform =
        bgfx::createUniform("u_shadowLightIndices", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->shadowLightIndicesExtraUniform))
    this->shadowLightIndicesExtraUniform =
        bgfx::createUniform("u_shadowLightIndicesExtra", bgfx::UniformType::Vec4);
  bool valid =
      bgfx::isValid(this->shadowLightIndicesExtraUniform) &&
      bgfx::isValid(this->shadowModelViewUniform) &&
      bgfx::isValid(this->shadowClipModelViewUniform) && bgfx::isValid(this->shadowDepthUniform) &&
      bgfx::isValid(this->shadowLightIndicesUniform) && bgfx::isValid(this->shadowQualityUniform) &&
      bgfx::isValid(this->shadowFlatDiffuseUniform) &&
      bgfx::isValid(this->shadowFlatSpecularUniform) &&
      bgfx::isValid(this->shadowFlatParamsUniform);
  for (int i = 0; i < 8; ++i) {
    const std::string suffix = std::to_string(i);
    if (!bgfx::isValid(this->shadowViewToClipUniform[i]))
      this->shadowViewToClipUniform[i] =
          bgfx::createUniform(("u_shadowViewToClip" + suffix).c_str(), bgfx::UniformType::Mat4);
    if (!bgfx::isValid(this->shadowViewToLightUniform[i]))
      this->shadowViewToLightUniform[i] =
          bgfx::createUniform(("u_shadowViewToLight" + suffix).c_str(), bgfx::UniformType::Mat4);
    if (!bgfx::isValid(this->shadowParamsUniform[i]))
      this->shadowParamsUniform[i] =
          bgfx::createUniform(("u_shadowParams" + suffix).c_str(), bgfx::UniformType::Vec4);
    if (!bgfx::isValid(this->shadowMetaUniform[i]))
      this->shadowMetaUniform[i] =
          bgfx::createUniform(("u_shadowMeta" + suffix).c_str(), bgfx::UniformType::Vec4);
    if (!bgfx::isValid(this->shadowTexelUniform[i]))
      this->shadowTexelUniform[i] =
          bgfx::createUniform(("u_shadowTexel" + suffix).c_str(), bgfx::UniformType::Vec4);
    if (!bgfx::isValid(this->shadowSampler[i]))
      this->shadowSampler[i] =
          bgfx::createUniform(("s_shadow" + suffix).c_str(), bgfx::UniformType::Sampler);
    valid = valid && bgfx::isValid(this->shadowViewToClipUniform[i]) &&
      bgfx::isValid(this->shadowViewToLightUniform[i]) &&
      bgfx::isValid(this->shadowParamsUniform[i]) &&
      bgfx::isValid(this->shadowMetaUniform[i]) &&
      bgfx::isValid(this->shadowTexelUniform[i]) &&
      bgfx::isValid(this->shadowSampler[i]);
  }
  if (!valid) this->lastError = "BGFX shadow shader or uniform allocation failed";
  return valid;
}

bool CoinBgfxBackend::prepareShadowPrograms(size_t mapCount, bool receivers)
{
  if (!this->prepareShadowResources())
    return false;
  const bool extended = mapCount > 2;
  const bool eight = mapCount > 4;
  const auto createExtended = [&]() {
    return bgfxTracePreparedProgram(
        createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver),
                           COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver),
                           COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_receiver4),
                           COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_receiver4)),
        "shadow_receiver", this->sampleCount);
  };
  const auto createEight = [&]() {
    return bgfxTracePreparedProgram(
        createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver),
                           COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver),
                           COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_receiver8),
                           COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_receiver8)),
        "shadow_receiver", this->sampleCount);
  };
  if (!bgfx::isValid(this->shadowMomentsProgram)) {
    this->shadowMomentsProgram =
        createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_moments),
                           COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_moments),
                           COIN_BGFX_SHADER_DATA(coin_bgfx_fs_shadow_moments),
                           COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_shadow_moments));
  }
  if (receivers) {
    if (!extended && !bgfx::isValid(this->shadowReceiverProgram)) {
      this->shadowReceiverProgram = bgfxTracePreparedProgram(
          createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver),
                             COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver),
                             COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_receiver),
                             COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_receiver)),
          "shadow_receiver", this->sampleCount);
    }
    if (extended && !eight && !bgfx::isValid(this->shadowReceiverProgram4))
      this->shadowReceiverProgram4 = createExtended();
    if (eight && !bgfx::isValid(this->shadowReceiverProgram8))
      this->shadowReceiverProgram8 = createEight();
  }
  const bool valid = bgfx::isValid(this->shadowMomentsProgram) &&
                     (!receivers || bgfx::isValid(eight      ? this->shadowReceiverProgram8
                                                  : extended ? this->shadowReceiverProgram4
                                                             : this->shadowReceiverProgram));
  if (!valid)
    this->lastError = "BGFX shadow shader allocation failed";
  return valid;
}

bool
CoinBgfxBackend::prepareShadowArray(const CoinRenderShadowPlan & plan, bool enabled)
{
  this->shadowArrayActive = false;
  if (!enabled || plan.passes.empty()) return true;
  uint32_t extent = 1;
  for (const auto & pass : plan.passes) extent = std::max(extent, pass.mapSize);
  // BGFX distinguishes a 2D array from an ordinary 2D image by layer count.
  const uint16_t layers = uint16_t(std::max<size_t>(2, plan.passes.size()));
  const uint64_t flags = BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_MIN_POINT |
    BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
  if (extent > bgfx::getCaps()->limits.maxTextureSize || layers > bgfx::getCaps()->limits.maxTextureLayers ||
      !bgfx::isTextureValid(1,false,layers,bgfx::TextureFormat::RGBA32F,flags)) {
    this->lastError = "Shadow moment array extent/layers/format unavailable"; return false;
  }
  const auto texture = bgfx::createTexture2D(uint16_t(extent),uint16_t(extent),false,layers,
    bgfx::TextureFormat::RGBA32F,flags);
  if (!bgfx::isValid(texture)) { this->lastError = "Shadow moment array allocation failed"; return false; }
  if (bgfx::isValid(this->shadowArrayTexture)) bgfx::destroy(this->shadowArrayTexture);
  this->shadowArrayTexture = texture; this->shadowArraySize = extent; this->shadowArrayActive = true;
  return true;
}

void
CoinBgfxBackend::encodeShadowArray(const CoinRenderShadowPlan & plan,
  const std::vector<bgfx::FrameBufferHandle> & maps, bgfx::ViewId view)
{
  if (!this->shadowArrayActive) return;
  bgfx::setViewName(view,"shadow_moment_array_copy");
  for (size_t slot=0;slot<plan.passes.size();++slot) {
    bgfx::TextureRegion destination, source;
    destination.handle=this->shadowArrayTexture; destination.z=uint16_t(slot);
    destination.width=destination.height=uint16_t(plan.passes[slot].mapSize); destination.depth=1;
    source.handle=bgfx::getTexture(maps[slot],0);
    source.width=source.height=destination.width; source.depth=1;
    bgfx::blit(view,destination,source);
  }
}

bool CoinBgfxBackend::prepareShadowMultitextureResources()
{
  if (bgfx::getCaps()->limits.maxTextureSamplers < 11 || bgfx::getCaps()->limits.maxTextureLayers < 2) {
    this->lastError = "Shadow multitexture requires eleven texture stages and 2D array sampling"; return false;
  }
  if (!this->prepareShadowResources() || !this->prepareFullscreenResources())
    return false;
  if (!bgfx::isValid(this->shadowArraySampler))
    this->shadowArraySampler = bgfx::createUniform("s_shadowArray", bgfx::UniformType::Sampler);
  if (!bgfx::isValid(this->defaultShadowArrayTexture)) {
    const float white[8] = {1,1,1,1,1,1,1,1};
    this->defaultShadowArrayTexture = bgfx::createTexture2D(1,1,false,2,
      bgfx::TextureFormat::RGBA32F, BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT,
      bgfx::copy(white,sizeof(white)));
  }
  if (!bgfx::isValid(this->shadowArraySampler) || !bgfx::isValid(this->defaultShadowArrayTexture)) {
    this->lastError = "Shadow array capability resource allocation failed"; return false;
  }
  return true;
}

bool CoinBgfxBackend::prepareShadowMultitextureProgram(bool customSampleDepth, unsigned composition)
{
  const bool separateSampleProgram =
      this->sampleCount > 1 && customSampleDepth && !bgfxExplicitSurfaceSamples();
  auto &program = composition == 1        ? this->shadowMultitexturePeelProgram
                  : composition == 2      ? this->shadowMultitextureOitProgram
                  : separateSampleProgram ? this->shadowMultitextureSampleDepthProgram
                                          : this->shadowMultitextureProgram;
  auto &verified = composition == 1        ? this->shadowMultitexturePeelVerified
                   : composition == 2      ? this->shadowMultitextureOitVerified
                   : separateSampleProgram ? this->shadowMultitextureSampleDepthVerified
                                           : this->shadowMultitextureVerified;
  if (verified)
    return true;
  if (!this->prepareShadowMultitextureResources())
    return false;
  // Preserve the established ordinary-surface MSAA contract: OpenGL evaluates
  // clip depth at the covered sample, Vulkan default keeps hardware depth.
  const bool explicitSamples = this->sampleCount > 1 && (composition || customSampleDepth || bgfxExplicitSurfaceSamples());
  if (explicitSamples && !bgfxSampleInterpolationSupported(this->sampleCount, this->lastError)) return false;
  if (!bgfx::isValid(program))
    program = bgfxTracePreparedProgram(
        createLayerProgram(
            COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_multitexture),
            COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_multitexture),
            composition == 1
                ? (this->sampleCount > 1
                       ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_multitexture_peel_msaa)
                       : COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_multitexture_peel))
            : composition == 2
                ? (this->sampleCount > 1
                       ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_multitexture_oit_sample_depth)
                       : COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_multitexture_oit))
            : explicitSamples
                ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_multitexture_sample_depth)
            : this->sampleCount > 1
                ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_multitexture_native_depth)
                : COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_multitexture),
            composition == 1
                ? (this->sampleCount > 1
                       ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_multitexture_peel_msaa)
                       : COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_multitexture_peel))
            : composition == 2
                ? (this->sampleCount > 1
                       ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_multitexture_oit_sample_depth)
                       : COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_multitexture_oit))
            : explicitSamples
                ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_multitexture_sample_depth)
            : this->sampleCount > 1
                ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_multitexture_native_depth)
                : COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_multitexture)),
        "shadow_multitexture", this->sampleCount);
  if (!bgfx::isValid(program)) { this->lastError = "Shadow multitexture program allocation failed"; return false; }
  return this->verifyShadowMultitextureProgram(program, verified, composition, false);
}

bool CoinBgfxBackend::verifyShadowMultitextureProgram(bgfx::ProgramHandle program, bool &verified,
                                                      unsigned composition, bool bump, uint64_t surfaceState,
                                                      bgfx::TextureFormat::Enum colorFormat,
                                                      bgfx::TextureFormat::Enum depthFormat, bool portable)
{
  if (verified)
    return true;
  // BGFX exposes no varying-component limit. A private draw/readback proves
  // the actual complete program/pipeline before any RTT producer is encoded.
  const bool resolveProbe = composition || (bump && this->sampleCount > 1);
  const uint64_t probeFlags = peelTextureFlags | (this->sampleCount == 2 ? BGFX_TEXTURE_RT_MSAA_X2 :
    this->sampleCount == 4 ? BGFX_TEXTURE_RT_MSAA_X4 : 0);
  bgfx::FrameBufferHandle output=BGFX_INVALID_HANDLE, sampledDepth=BGFX_INVALID_HANDLE;
  bgfx::TextureHandle probeColor=BGFX_INVALID_HANDLE, probeAuxiliary=BGFX_INVALID_HANDLE, probeDepth=BGFX_INVALID_HANDLE;
  bgfx::VertexBufferHandle probeVertices=BGFX_INVALID_HANDLE;
  bgfx::FrameBufferHandle resolvedOutput=BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle resolveProgram=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle resolveColorSampler=BGFX_INVALID_HANDLE,resolveDepthSampler=BGFX_INVALID_HANDLE;
  const auto releaseResolver=[&]() {
    if(bgfx::isValid(resolvedOutput))bgfx::destroy(resolvedOutput);
    if(bgfx::isValid(resolveProgram))bgfx::destroy(resolveProgram);
    if(bgfx::isValid(resolveColorSampler))bgfx::destroy(resolveColorSampler);
    if(bgfx::isValid(resolveDepthSampler))bgfx::destroy(resolveDepthSampler);
  };
  if (composition) {
    // Match the actual peeling/weighted MRT exactly. In particular these
    // attachments are sampleable MSAA images, without automatic resolve targets.
    // Mixed resolved RGBA8/R8 targets are a different framebuffer contract.
    const uint64_t mrtFlags=weightedTextureFlags(this->sampleCount);
    const auto color=probeColor=bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA16F,mrtFlags);
    const auto auxiliary=probeAuxiliary=bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::R16F,mrtFlags);
    const auto depth=probeDepth=bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::D32F,mrtFlags);
    const bgfx::TextureHandle attachments[]={color,auxiliary,depth};
    if(bgfx::isValid(color)&&bgfx::isValid(auxiliary)&&bgfx::isValid(depth)) output=bgfx::createFrameBuffer(3,attachments,true);
    if(!bgfx::isValid(output)) for(const auto handle:attachments) if(bgfx::isValid(handle))bgfx::destroy(handle);
    sampledDepth=bgfx::createFrameBuffer(1,1,bgfx::TextureFormat::D32F,weightedTextureFlags(this->sampleCount));
    CoinBgfxVertex vertices[3]={};
    vertices[0].position[0]=-1;vertices[0].position[1]=-1;
    vertices[1].position[0]=3;vertices[1].position[1]=-1;
    vertices[2].position[0]=-1;vertices[2].position[1]=3;
    for(auto &vertex:vertices){vertex.color[3]=1;vertex.material[2]=1;vertex.material[3]=1;}
    probeVertices=bgfx::createVertexBuffer(bgfx::copy(vertices,sizeof(vertices)),this->layout);
    if(!bgfx::isValid(this->previousDepthSampler))this->previousDepthSampler=bgfx::createUniform("s_prevDepth",bgfx::UniformType::Sampler);
    if(!bgfx::isValid(this->opaqueDepthSampler))this->opaqueDepthSampler=bgfx::createUniform("s_opaqueDepth",bgfx::UniformType::Sampler);
    if(!bgfx::isValid(this->peelMetaUniform))this->peelMetaUniform=bgfx::createUniform("u_peelMeta",bgfx::UniformType::Vec4);
  } else if (bump) {
    // Match the consumed attachment formats and sampleable MSAA contract.
    // A color-only/autoresolved probe would compile a second heavyweight PSO.
    const uint64_t flags = this->sampleCount > 1 ? weightedTextureFlags(this->sampleCount) : peelTextureFlags;
    probeColor = bgfx::createTexture2D(1, 1, false, 1, colorFormat, flags);
    probeDepth = bgfx::createTexture2D(1, 1, false, 1, depthFormat, flags);
    const bgfx::TextureHandle attachments[] = {probeColor, probeDepth};
    if (bgfx::isValid(probeColor) && bgfx::isValid(probeDepth))
      output = bgfx::createFrameBuffer(2, attachments, true);
    if (!bgfx::isValid(output))
      for (auto handle : attachments) if (bgfx::isValid(handle)) bgfx::destroy(handle);
  } else output=bgfx::createFrameBuffer(1,1,bgfx::TextureFormat::RGBA8,probeFlags);
  if (resolveProbe) {
    // Resolve the private sampleable attachments (or convert composition MRT).
    // The existing readback stays single-sample and the blit stays same-format.
    resolvedOutput=bgfx::createFrameBuffer(1,1,bgfx::TextureFormat::RGBA8,peelTextureFlags);
    resolveColorSampler=bgfx::createUniform(this->sampleCount>1?"s_msaaColor":"s_layer",bgfx::UniformType::Sampler);
    if(this->sampleCount>1)resolveDepthSampler=bgfx::createUniform("s_msaaDepth",bgfx::UniformType::Sampler);
    resolveProgram=createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs),COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
      this->sampleCount>1?COIN_BGFX_SHADER_DATA(coin_bgfx_fs_msaa_resolve):COIN_BGFX_SHADER_DATA(coin_bgfx_fs_composite),
      this->sampleCount>1?COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_msaa_resolve):COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_composite));
  }

  const auto staging = bgfx::createTexture2D(1, 1, false, 1, (bump && !resolveProbe) ? colorFormat : bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
  if (!bgfx::isValid(output) || !bgfx::isValid(staging) || (composition && (!bgfx::isValid(sampledDepth) || !bgfx::isValid(probeVertices))) ||
      (resolveProbe && (!bgfx::isValid(resolvedOutput) || !bgfx::isValid(resolveProgram) ||
       !bgfx::isValid(resolveColorSampler) || (this->sampleCount>1&&!bgfx::isValid(resolveDepthSampler))))) {
    if (bgfx::isValid(output)) bgfx::destroy(output); if (bgfx::isValid(staging)) bgfx::destroy(staging);
    if(bgfx::isValid(sampledDepth))bgfx::destroy(sampledDepth);
    if(bgfx::isValid(probeVertices))bgfx::destroy(probeVertices);
    releaseResolver();
    this->lastError = "Shadow multitexture probe allocation failed: output="+std::to_string(output.idx)+
      " sampledDepth="+std::to_string(sampledDepth.idx)+" probeVertices="+std::to_string(probeVertices.idx)+
      " staging="+std::to_string(staging.idx)+" color="+std::to_string(probeColor.idx)+
      " auxiliary="+std::to_string(probeAuxiliary.idx)+" depth="+std::to_string(probeDepth.idx)+
      " resolvedOutput="+std::to_string(resolvedOutput.idx)+" resolveProgram="+std::to_string(resolveProgram.idx)+
      " resolveColorSampler="+std::to_string(resolveColorSampler.idx)+" resolveDepthSampler="+std::to_string(resolveDepthSampler.idx)+
      " samples="+std::to_string(this->sampleCount)+" composition="+std::to_string(composition);
    return false;
  }
  if (bump) {
    CoinBgfxVertex vertices[3] = {};
    vertices[0].position[0] = -1;
    vertices[0].position[1] = -1;
    vertices[1].position[0] = 3;
    vertices[1].position[1] = -1;
    vertices[2].position[0] = -1;
    vertices[2].position[1] = 3;
    for (auto &vertex : vertices) {
      vertex.material[0] = 1; // Defined zero specular: pow(0, 64), never pow(0, 0).
      vertex.material[1] = 1;
    }
    probeVertices = bgfx::createVertexBuffer(bgfx::copy(vertices, sizeof(vertices)), this->layout);
    if (!bgfx::isValid(probeVertices)) {
      bgfx::destroy(output);
      bgfx::destroy(staging);
      releaseResolver();
      this->lastError = "Bump receiver capability geometry allocation failed";
      return false;
    }
  }
  bgfx::frame(BGFX_FRAME_FLUSH);
  sharedRuntime().programCommandsMaterialized = sharedRuntime().programCommandsQueued;
  const bgfx::ViewId view = this->viewBase;
  bgfx::resetView(view); bgfx::resetView(view + 1); bgfx::resetView(view + 2);
  bgfx::setViewName(view, "shadow_multitexture_capability"); bgfx::setViewRect(view,0,0,1,1);
  // Every enabled channel differs between the bump probe's clear (white,
  // alpha zero) and shader result (black, alpha one). Disabled channels must
  // retain their clear value, exactly as on the consumed surface pipeline.
  const uint32_t clearColor = bump ? UINT32_C(0xffffff00) : UINT32_C(0xff00ffff);
  bgfx::setViewFrameBuffer(view, output);
  bgfx::setViewClear(view, BGFX_CLEAR_COLOR | ((composition || bump)?BGFX_CLEAR_DEPTH:0), clearColor);
  bgfx::setViewTransform(view,nullptr,nullptr); bgfx::setScissor();
  const float identity[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  const float zero[4] = {}, absent[4] = {-1,-1,-1,-1};
  CoinBgfxDraw draw{}; this->bindDrawLighting(draw,1,1); this->bindDrawTexture(draw,{},true,false,true);
  bgfx::setUniform(this->shadowQualityUniform,zero); bgfx::setUniform(this->shadowFlatParamsUniform,zero);
  bgfx::setUniform(this->shadowFlatDiffuseUniform,zero); bgfx::setUniform(this->shadowFlatSpecularUniform,zero);
  bgfx::setUniform(this->shadowLightIndicesUniform,absent); bgfx::setUniform(this->shadowLightIndicesExtraUniform,absent);
  for(unsigned slot=0;slot<8;++slot) {
    bgfx::setUniform(this->shadowParamsUniform[slot],zero); bgfx::setUniform(this->shadowMetaUniform[slot],zero); bgfx::setUniform(this->shadowTexelUniform[slot],zero);
    bgfx::setUniform(this->shadowViewToClipUniform[slot],identity); bgfx::setUniform(this->shadowViewToLightUniform[slot],identity);

  }
  bgfx::setTexture(10,this->shadowArraySampler,this->defaultShadowArrayTexture);
  if (bump)
    bgfx::setTexture(8, this->bumpImageSampler, this->defaultTexture);
  const float sampleInfo[4] = {1,1,0,0}; bgfx::setUniform(this->depthInfoUniform,sampleInfo);
  if(composition==1) {
    bgfx::setUniform(this->peelMetaUniform,zero);
    bgfx::setTexture(8,this->previousDepthSampler,bgfx::getTexture(sampledDepth));
    bgfx::setTexture(9,this->opaqueDepthSampler,bgfx::getTexture(sampledDepth));
  }
  bgfx::setTransform(identity);
  bgfx::setVertexBuffer(0, (composition || bump) ? probeVertices : this->fullscreenVertexBuffer);
  bgfx::setIndexBuffer(this->fullscreenIndexBuffer, 0, 3);
  bgfx::setSampleMask();
  bgfx::setState((bump ? surfaceState : BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A) |
                 (this->sampleCount > 1 ? BGFX_STATE_MSAA : 0));
  const uint32_t draws=composition && this->sampleCount>1 && (composition==1 || !bgfxExplicitSurfaceSamples()) ? this->sampleCount : 1;
  for(uint32_t sample=0;sample<draws;++sample) {
    const float info[4]={1,1,float(sample),0}; bgfx::setUniform(this->depthInfoUniform,info);
    if(draws>1)bgfx::setSampleMask(composition==1?peelFixedSampleMask(this->sampleCount,sample):1u<<sample);
    if (portable) {
      // This is the actual linked module: the runtime branch only makes the
      // capability draw's expected output independent of authored parameters.
      const float control[4] = {1, composition ? 1.0f : 0.0f, 0, 0};
      bgfx::setUniform(sharedRuntime().portable.probeControl, control);
    }
    bgfx::submit(view,program,0,sample+1==draws?BGFX_DISCARD_ALL:BGFX_DISCARD_NONE);
  }
  bgfx::setSampleMask();
  if(resolveProbe) {
    bgfx::setViewName(view+1,"shadow_multitexture_capability_resolve");
    bgfx::setViewRect(view+1,0,0,1,1);bgfx::setViewFrameBuffer(view+1,resolvedOutput);
    bgfx::setViewTransform(view+1,nullptr,nullptr);bgfx::setViewClear(view+1,BGFX_CLEAR_NONE);
    const float info[4]={1,1,1,float(this->sampleCount)};
    bgfx::setUniform(this->depthInfoUniform,info);
    bgfx::setTexture(0,resolveColorSampler,probeColor);
    if(this->sampleCount>1)bgfx::setTexture(1,resolveDepthSampler,probeDepth);
    bgfx::setTransform(identity);bgfx::setVertexBuffer(0,this->fullscreenVertexBuffer);
    bgfx::setIndexBuffer(this->fullscreenIndexBuffer,0,3);
    bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);
    bgfx::submit(view+1,resolveProgram);
  }
  bgfx::TextureRegion dst,src; dst.handle=staging;
  src.handle=bgfx::getTexture(resolveProbe?resolvedOutput:output);
  bgfx::blit(view+(resolveProbe?2:1),dst,src);
  uint32_t pixel=UINT32_MAX;bgfx::TextureRegion read;read.handle=staging;const uint32_t ready=bgfx::read(read,&pixel);
  uint32_t complete=bgfx::frame();while(static_cast<int32_t>(complete-ready)<0)complete=bgfx::frame();
  bgfx::resetView(view);bgfx::resetView(view+1);bgfx::resetView(view+2);bgfx::destroy(output);bgfx::destroy(staging);
  releaseResolver();
  if(bgfx::isValid(sampledDepth))bgfx::destroy(sampledDepth);
  if(bgfx::isValid(probeVertices))bgfx::destroy(probeVertices);
  uint32_t expectedPixel = composition ? UINT32_C(0xff000000) : 0;
  if (bump) {
    uint8_t expected[4] = {
      uint8_t(surfaceState & BGFX_STATE_WRITE_R ? 0 : 255),
      uint8_t(surfaceState & BGFX_STATE_WRITE_G ? 0 : 255),
      uint8_t(surfaceState & BGFX_STATE_WRITE_B ? 0 : 255),
      uint8_t(surfaceState & BGFX_STATE_WRITE_A ? 255 : 0)
    };
    // Direct readback retains the image's storage order. The MSAA resolver
    // instead writes RGBA8, regardless of the source attachment format.
    if (!resolveProbe && colorFormat == bgfx::TextureFormat::BGRA8)
      std::swap(expected[0], expected[2]);
    std::memcpy(&expectedPixel, expected, sizeof(expectedPixel));
  }
  if (this->checkRuntimeFailure("Shadow multitexture capability probe failed") !=
          CoinRenderBackendStatus::SUCCESS || pixel != expectedPixel) {
    if(this->lastError.empty())this->lastError="Shadow multitexture program failed its complete pipeline draw";
    return false;
  }
  verified=true; return true;
}

bool
CoinBgfxBackend::prepareShadowTransparencyPrograms()
{
  if (bgfx::getCaps()->limits.maxTextureSamplers < 16) {
    this->lastError = "Shadow peeling/OIT requires sixteen texture stages";
    return false;
  }
  if (!bgfx::isValid(this->shadowPeelProgram))
    this->shadowPeelProgram = createLayerProgram(
      COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver),
      COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_peel),
      COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_peel));
  if (!bgfx::isValid(this->shadowOitProgram))
    this->shadowOitProgram = createLayerProgram(
      COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver),
      COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_oit),
      COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_oit));
  if (this->sampleCount > 1 && !bgfx::isValid(this->shadowPeelMsaaProgram))
    this->shadowPeelMsaaProgram = createLayerProgram(
      COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver),
      COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_peel_msaa), COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_peel_msaa));
  const bool valid = bgfx::isValid(this->shadowPeelProgram) && bgfx::isValid(this->shadowOitProgram) &&
    (this->sampleCount == 1 || bgfx::isValid(this->shadowPeelMsaaProgram));
  if (!valid) this->lastError = "Shadow transparency shader allocation failed";
  return valid;
}
