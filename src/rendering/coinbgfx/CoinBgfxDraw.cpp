// CoinRender BGFX Draw responsibilities.
#include "rendering/coinbgfx/CoinBgfxBackendInternal.h"
#include "coin_bgfx_vs_glsl.h"
#include "coin_bgfx_vs_spirv.h"
#include "coin_bgfx_fs_texture2d_native_depth_glsl.h"
#include "coin_bgfx_fs_texture2d_native_depth_spirv.h"
#include "coin_bgfx_fs_texture2d_native_depth_portable_glsl.h"
#include "coin_bgfx_fs_texture2d_native_depth_portable_spirv.h"
#include "coin_bgfx_fs_texture2d_sample_depth_glsl.h"
#include "coin_bgfx_fs_texture2d_sample_depth_spirv.h"
#include "coin_bgfx_fs_texture2d_sample_depth_portable_glsl.h"
#include "coin_bgfx_fs_texture2d_sample_depth_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_texture2d_native_depth_dx11.h"
#include "coin_bgfx_fs_texture2d_native_depth_portable_dx11.h"
#include "coin_bgfx_fs_texture2d_sample_depth_dx11.h"
#include "coin_bgfx_fs_texture2d_sample_depth_portable_dx11.h"
#endif
#include "coin_bgfx_vs_instanced_color_glsl.h"
#include "coin_bgfx_vs_instanced_color_spirv.h"
#include "coin_bgfx_vs_shadow_receiver_glsl.h"
#include "coin_bgfx_vs_shadow_receiver_spirv.h"
#include "coin_bgfx_fs_instanced_sample_depth_glsl.h"
#include "coin_bgfx_fs_instanced_sample_depth_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_instanced_sample_depth_dx11.h"
#endif
#include "coin_bgfx_fs_surface_native_depth_glsl.h"
#include "coin_bgfx_fs_surface_native_depth_portable_glsl.h"
#include "coin_bgfx_fs_surface_native_depth_spirv.h"
#include "coin_bgfx_fs_surface_native_depth_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_surface_native_depth_dx11.h"
#include "coin_bgfx_fs_surface_native_depth_portable_dx11.h"
#endif
#include "coin_bgfx_fs_surface_sample_depth_glsl.h"
#include "coin_bgfx_fs_surface_sample_depth_portable_glsl.h"
#include "coin_bgfx_fs_surface_sample_depth_spirv.h"
#include "coin_bgfx_fs_surface_sample_depth_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_surface_sample_depth_dx11.h"
#include "coin_bgfx_fs_surface_sample_depth_portable_dx11.h"
#endif
#include "coin_bgfx_fs_shadow_native_depth_glsl.h"
#include "coin_bgfx_fs_shadow_native_depth_portable_glsl.h"
#include "coin_bgfx_fs_shadow_native_depth_spirv.h"
#include "coin_bgfx_fs_shadow_native_depth_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_shadow_native_depth_dx11.h"
#include "coin_bgfx_fs_shadow_native_depth_portable_dx11.h"
#endif
#include "coin_bgfx_fs_shadow_sample_depth_glsl.h"
#include "coin_bgfx_fs_shadow_sample_depth_portable_glsl.h"
#include "coin_bgfx_fs_shadow_sample_depth_spirv.h"
#include "coin_bgfx_fs_shadow_sample_depth_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_shadow_sample_depth_dx11.h"
#include "coin_bgfx_fs_shadow_sample_depth_portable_dx11.h"
#endif
#include "coin_bgfx_fs_instanced_native_depth_glsl.h"
#include "coin_bgfx_fs_instanced_native_depth_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_instanced_native_depth_dx11.h"
#endif
#ifdef _WIN32
#include "coin_bgfx_vs_dx11.h"
#include "coin_bgfx_vs_instanced_color_dx11.h"
#include "coin_bgfx_vs_shadow_receiver_dx11.h"
#endif

using namespace coin_bgfx_internal;

void
CoinBgfxBackend::bindDrawTexture(
  const CoinBgfxDraw & draw, const std::vector<bgfx::TextureHandle> & textures,
  bool shadowSurface, bool generalSurface, bool shadowMultitexture)
{
  const uint32_t declaredUnits = draw.portableShaderSlot ? COIN_RENDER_MAX_TEXTURE_UNITS : shadowMultitexture ? COIN_RENDER_MAX_TEXTURE_UNITS : shadowSurface ? 1 : generalSurface ? COIN_RENDER_MAX_TEXTURE_UNITS : this->activeTextureUnitCount;
  float params[COIN_RENDER_MAX_TEXTURE_UNITS][4] = {};
  float blend[COIN_RENDER_MAX_TEXTURE_UNITS][4] = {};
  float sizes[COIN_RENDER_MAX_TEXTURE_UNITS][4] = {};
  const bool portableEnabled=this->portableSampling;
  for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
    CoinBgfxDraw::TextureLayer layer;
    if (unit == 0) {
      layer.enabled = draw.hasTexture; layer.slot = draw.textureSlot;
      layer.model = draw.textureModel; layer.wrapS = draw.wrapS; layer.wrapT = draw.wrapT;
      layer.wrapR = draw.wrapR;
      layer.filter = draw.filter; layer.maxAnisotropy = draw.maxAnisotropy;
      layer.cubeFaceSize = draw.cubeFaceSize; layer.cubeMipmapped = draw.cubeMipmapped;
      layer.volumeWidth = draw.volumeWidth; layer.volumeHeight = draw.volumeHeight;
      layer.volumeDepth = draw.volumeDepth; layer.volumeMipmapped = draw.volumeMipmapped;
      std::memcpy(layer.blendColor, draw.textureBlendColor, sizeof(layer.blendColor));
    } else layer = draw.extraTextures[unit - 1];
    bgfx::TextureHandle texture = this->defaultTexture;
    if (layer.enabled && layer.slot < textures.size() && bgfx::isValid(textures[layer.slot]))
      texture = textures[layer.slot];
    if (unit == 0) {
      const auto output = this->rasterImageOutputs.find(draw.renderStateSlot);
      if (output != this->rasterImageOutputs.end()) texture = output->second;
    }
    if(portableEnabled && layer.enabled) {
      const auto extent=this->samplingBoundTextureSizes.find(texture.idx);
      if(extent!=this->samplingBoundTextureSizes.end()) {sizes[unit][0]=float(extent->second.first);sizes[unit][1]=float(extent->second.second);}
    }
    uint32_t flags = BGFX_SAMPLER_NONE;
    if (layer.wrapS == CoinRenderTextureWrap::CLAMP) flags |= BGFX_SAMPLER_U_CLAMP;
    if (layer.wrapT == CoinRenderTextureWrap::CLAMP) flags |= BGFX_SAMPLER_V_CLAMP;
    if (layer.filter == CoinRenderTextureFilter::NEAREST)
      flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
    if(layer.filter == CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR) flags |= BGFX_SAMPLER_MIN_POINT;
    if(layer.maxAnisotropy>1)flags|=BGFX_SAMPLER_MIN_ANISOTROPIC|BGFX_SAMPLER_MAG_ANISOTROPIC;
    if(layer.volumeDepth || layer.cubeFaceSize) flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
    bool directRenderTarget = false;
    if (layer.enabled) for (const auto & resource : this->directTextures) {
      const auto direct = bgfx::getTexture(resource.frameBuffer, 0);
      if (bgfx::isValid(direct) && direct.idx == texture.idx) {
        directRenderTarget = true; sizes[unit][0] = float(resource.width); sizes[unit][1] = float(resource.height);
      }
    }
    params[unit][0] = layer.enabled ? 1.0f : 0.0f;
    sizes[unit][2] = float(layer.filter);
    sizes[unit][3] = float(unsigned(layer.wrapS) + 2 * unsigned(layer.wrapT));
    if(layer.volumeDepth) {
      sizes[unit][0] = float(layer.volumeWidth);
      sizes[unit][1] = float(layer.volumeHeight);
      sizes[unit][2] = float(4 + unsigned(layer.filter));
      sizes[unit][3] = float(layer.volumeDepth * 16 + unsigned(layer.wrapS) +
        2 * unsigned(layer.wrapT) + 4 * unsigned(layer.wrapR) +
        (layer.volumeMipmapped ? 8 : 0));
    }

    if (layer.cubeFaceSize) {
      sizes[unit][0] = float(layer.cubeFaceSize); sizes[unit][1] = layer.cubeMipmapped ? 1.0f : 0.0f;
      sizes[unit][2] = float(8 + unsigned(layer.filter)); sizes[unit][3] = 0;
    }


    params[unit][1] = static_cast<float>(layer.model);
    params[unit][2] = directRenderTarget && !layer.cubeFaceSize && !bgfx::getCaps()->originBottomLeft ? 1.0f : 0.0f;
    params[unit][3] = static_cast<float>(draw.textureProjection);
    std::memcpy(blend[unit], layer.blendColor, sizeof(layer.blendColor));
    // Bind only resources declared by the stage that will execute this draw.
    // Recycled native program slots can retain metadata for absent samplers;
    // supplying those undeclared bindings can corrupt its descriptor writes.
    // The general stage still declares and receives all eight scene units.
    if (unit < declaredUnits)
      bgfx::setTexture(static_cast<uint8_t>(unit + (shadowMultitexture ? 0 : 2)),
        unit == 0 ? this->textureSampler : this->extraTextureSamplers[unit - 1], texture, 0, 1, 0,
        (layer.volumeDepth || layer.cubeFaceSize) ? 1 :
          (CoinRenderTextureSamplingCore::mipFilter(layer.filter) ? UINT8_MAX : 1), flags);
  }
  bgfx::setUniform(this->samplingTextureSizesUniform, sizes, COIN_RENDER_MAX_TEXTURE_UNITS);
  bgfx::setUniform(this->textureParamsUniform, params, COIN_RENDER_MAX_TEXTURE_UNITS);
  bgfx::setUniform(this->textureBlendUniform, blend, COIN_RENDER_MAX_TEXTURE_UNITS);
  bgfx::setUniform(this->textureCombineUniform, draw.textureCombines, COIN_RENDER_MAX_TEXTURE_UNITS * 4);
}

bool
CoinBgfxBackend::prepareSurfaceDepthPrograms(const std::vector<CoinBgfxDraw> & draws,
  bool hasShadows, bool instanced, bool texture2d)
{
  if (this->sampleCount == 1 || draws.empty()) return true;
  if (!bgfx::isValid(this->depthInfoUniform))
    this->depthInfoUniform = bgfx::createUniform("u_depthInfo", bgfx::UniformType::Vec4);
  const bool custom = this->shadowMultitextureSampleDepthActive || bgfxExplicitSurfaceSamples() || std::any_of(draws.begin(), draws.end(), bgfxCustomSampleDepth);
  if (custom && !bgfxSampleInterpolationSupported(this->sampleCount, this->lastError)) return false;
  if (instanced) {
    if (bgfxExplicitSurfaceSamples()) {
      if (!bgfx::isValid(this->instancedSampleDepthProgram))
        this->instancedSampleDepthProgram = bgfxTracePreparedProgram(
            createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs_instanced_color),
                               COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_instanced_color),
                               COIN_BGFX_SHADER_DATA(coin_bgfx_fs_instanced_sample_depth),
                               COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_instanced_sample_depth)),
            hasShadows ? "shadow_receiver" : "general_surface", this->sampleCount);
      return bgfx::isValid(this->instancedSampleDepthProgram);
    }
    if (!bgfx::isValid(this->instancedNativeDepthProgram))
      this->instancedNativeDepthProgram = bgfxTracePreparedProgram(
          createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs_instanced_color),
                             COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_instanced_color),
                             COIN_BGFX_SHADER_DATA(coin_bgfx_fs_instanced_native_depth),
                             COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_instanced_native_depth)),
          hasShadows ? "shadow_receiver" : "general_surface", this->sampleCount);
    return bgfx::isValid(this->instancedNativeDepthProgram);
  }
  bgfx::ProgramHandle & native = hasShadows ? this->shadowNativeDepthProgram :
    texture2d ? this->texture2dNativeDepthProgram : this->surfaceNativeDepthProgram;
  const bool nativeNeeded =
      !this->shadowMultitextureSampleDepthActive && !bgfxExplicitSurfaceSamples() &&
      std::any_of(draws.begin(), draws.end(),
                  [](const CoinBgfxDraw &draw) { return !bgfxCustomSampleDepth(draw); });
  if (nativeNeeded && !bgfx::isValid(native))
    native = bgfxTracePreparedProgram(
        createLayerProgram(hasShadows ? COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver)
                                      : COIN_BGFX_SHADER_DATA(coin_bgfx_vs),
                           hasShadows ? COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver)
                                      : COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
                           hasShadows ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_native_depth)
                           : texture2d ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_texture2d_native_depth)
                                       : COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_surface_native_depth),
                           hasShadows ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_native_depth)
                           : texture2d ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_texture2d_native_depth)
                                       : COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_surface_native_depth)),
        hasShadows ? "shadow_receiver" : "general_surface", this->sampleCount);
  bgfx::ProgramHandle & sampled = hasShadows ? this->shadowSampleDepthProgram :
    texture2d ? this->texture2dSampleDepthProgram : this->surfaceSampleDepthProgram;
  if (custom && !bgfx::isValid(sampled))
    sampled = bgfxTracePreparedProgram(
        createLayerProgram(hasShadows ? COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver)
                                      : COIN_BGFX_SHADER_DATA(coin_bgfx_vs),
                           hasShadows ? COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver)
                                      : COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
                           hasShadows ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_sample_depth)
                           : texture2d ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_texture2d_sample_depth)
                                       : COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_surface_sample_depth),
                           hasShadows ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_sample_depth)
                           : texture2d ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_texture2d_sample_depth)
                                       : COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_surface_sample_depth)),
        hasShadows ? "shadow_receiver" : "general_surface", this->sampleCount);
  if ((nativeNeeded && !bgfx::isValid(native)) || (custom && !bgfx::isValid(sampled)) ||
      !bgfx::isValid(this->depthInfoUniform)) {
    this->lastError = "BGFX MSAA surface-depth program allocation failed";
    return false;
  }
  return true;
}

void
CoinBgfxBackend::submitSurfaceDraw(bgfx::ViewId view, const CoinBgfxDraw & draw,
  const std::vector<bgfx::TextureHandle> & textures, const CoinRenderFramePlan & frame,
  const CoinRenderShadowPlan & shadowPlan,
  const std::vector<bgfx::FrameBufferHandle> & shadowMaps,
  int width, int height, bgfx::TextureHandle sampleDepth)
{
  if (draw.bumpSlot && draw.bumpExtended) {
    this->bindDrawTexture(draw, textures, true, this->sampleCount > 1, true);
    this->bindShadowReceiver(frame, shadowPlan, shadowMaps, draw, height, 8);
    const auto &state=frame.renderStates[draw.renderStateSlot];
    const auto &lights=frame.lightingStates[state.lightingSlot].lights;
    const SbMatrix modelView=state.model*state.view;
    const auto inverseDirection=CoinRenderTransformCore::normalMatrix(modelView).transpose();
    float directions[8][4]={},colors[8][4]={};
    for (size_t i=0;i<lights.size();++i) {
      SbVec3f direction;
      const bool positional = lights[i].type != CoinRenderLightType::DIRECTIONAL;
      inverseDirection.multDirMatrix(positional ? SbVec3f(lights[i].position) -
        SbVec3f(modelView[3][0],modelView[3][1],modelView[3][2]) : -SbVec3f(lights[i].direction),direction);
      if (!positional) direction.normalize();
      directions[i][3] = positional ? 1.0f : 0.0f;
      direction.getValue(directions[i][0],directions[i][1],directions[i][2]);
      std::copy_n(lights[i].color,3,colors[i]);
      colors[i][3]=lights[i].intensity;
    }
    SbVec3f eye;
    inverseDirection.multDirMatrix(-SbVec3f(modelView[3][0],modelView[3][1],modelView[3][2]),eye);
    const float eyeValues[4]={eye[0],eye[1],eye[2],0};
    const float bumpMeta[4]={float(lights.size()),0,0,0};
    bgfx::setUniform(this->bumpLightMetaUniform,bumpMeta);
    bgfx::setUniform(this->bumpLightVectorsUniform,directions,8);
    bgfx::setUniform(this->bumpLightColorsUniform,colors,8);
    bgfx::setUniform(this->bumpEyeUniform,eyeValues);
    bgfx::setUniform(this->bumpModelViewUniform,modelView.getValue());
    bgfx::setTexture(10, this->shadowArraySampler,
                     this->shadowArrayActive ? this->shadowArrayTexture
                                             : this->defaultShadowArrayTexture);
    const uint32_t flags = (draw.bumpClampS ? BGFX_SAMPLER_U_CLAMP : 0) |
                           (draw.bumpClampT ? BGFX_SAMPLER_V_CLAMP : 0) | BGFX_SAMPLER_MIP_POINT;
    bgfx::setTexture(8, this->bumpImageSampler, textures[draw.bumpTextureSlot], 0, 1, 0, 1, flags);
    const float info[4] = {1.0f / width, 1.0f / height, 0, 0};
    bgfx::setUniform(this->depthInfoUniform, info);
    const auto program = this->sampleCount == 1         ? this->bumpReceiverProgram
                         : bgfxExplicitSurfaceSamples() ? this->bumpReceiverSampleProgram
                                                        : this->bumpReceiverNativeProgram;
    this->submitPortableDraw(view, program, draw, frame, PortableFamily::BASE);
    return;
  }
  if(draw.bumpSlot) {
    const uint32_t normalFlags=(draw.bumpClampS?BGFX_SAMPLER_U_CLAMP:0) |
      (draw.bumpClampT?BGFX_SAMPLER_V_CLAMP:0) | BGFX_SAMPLER_MIP_POINT;
    bgfx::setTexture(0,this->bumpImageSampler,textures[draw.bumpTextureSlot],0,1,0,1,normalFlags);
    uint32_t baseFlags=BGFX_SAMPLER_MIP_POINT;
    if(draw.wrapS==CoinRenderTextureWrap::CLAMP)baseFlags|=BGFX_SAMPLER_U_CLAMP;
    if(draw.wrapT==CoinRenderTextureWrap::CLAMP)baseFlags|=BGFX_SAMPLER_V_CLAMP;
    if(draw.filter==CoinRenderTextureFilter::NEAREST)baseFlags|=BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT;
    if(draw.filter==CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR)baseFlags|=BGFX_SAMPLER_MIN_POINT;
    if(CoinRenderTextureSamplingCore::mipFilter(draw.filter))baseFlags&=~BGFX_SAMPLER_MIP_POINT;
    if(draw.maxAnisotropy>1)baseFlags|=BGFX_SAMPLER_MIN_ANISOTROPIC|BGFX_SAMPLER_MAG_ANISOTROPIC;
    bgfx::setTexture(1,this->bumpBaseSampler,draw.hasTexture?textures[draw.textureSlot]:this->defaultTexture,
      0,1,0,CoinRenderTextureSamplingCore::mipFilter(draw.filter)?UINT8_MAX:1,baseFlags);
    this->bindDrawLighting(draw,height,width);
    const float info[4]={1.0f/width,1.0f/height,0,0};
    if(bgfx::isValid(this->depthInfoUniform))bgfx::setUniform(this->depthInfoUniform,info);
    float params[4]={draw.hasTexture?1.0f:0.0f,0,0,0};
    bgfx::setUniform(this->bumpParamsUniform,params);
    const auto program=this->sampleCount==1?this->bumpProgram:
      bgfxExplicitSurfaceSamples()?this->bumpSampleDepthProgram:this->bumpNativeDepthProgram;
    this->submitPortableDraw(view,program, draw, frame, PortableFamily::BASE,0,BGFX_DISCARD_NONE);
    params[1]=1;bgfx::setUniform(this->bumpParamsUniform,params);
    // The additive pass changes lighting, not the authored channel mask.
    // Keep its depth read-only: the first pass already wrote geometry depth.
    const uint64_t colorWrites = drawState(draw) & (BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::setState(colorWrites | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_MSAA |
      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_ONE));
    this->submitPortableDraw(view,program, draw, frame, PortableFamily::BASE);
    return;
  }
  const bool hasShadows = !shadowPlan.passes.empty();
  const bool perSampleDepth = this->sampleCount > 1 && bgfx::isValid(sampleDepth);
  const bool customDepth = this->sampleCount > 1 && (this->shadowMultitextureSampleDepthActive || bgfxExplicitSurfaceSamples() || bgfxCustomSampleDepth(draw));
  const bool instanced = !hasShadows && bgfx::isValid(this->activeProgram) &&
    this->activeProgram.idx == this->instancedProgram.idx;
  const bool texture2dDepth = !draw.portableShaderSlot && this->activeTexture2dDepth && !hasShadows && !instanced && !perSampleDepth;
  if (!draw.portableShaderSlot && this->sampleCount > 1 && CoinRenderDiagnosticShell::phaseTracingEnabled())
    std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_surface_depth_program family=%s mode=%s samples=%u\n",
      texture2dDepth ? "texture2d" : "general", customDepth || perSampleDepth ? "sample" : "native", this->sampleCount);
  const bool multitexture = hasShadows && this->shadowArrayActive;
  this->bindDrawTexture(draw, textures, hasShadows, this->sampleCount > 1 && !instanced && !texture2dDepth, multitexture);
  if (hasShadows)
    this->bindShadowReceiver(frame, shadowPlan, shadowMaps, draw, height, multitexture || this->sampleCount > 1 ? 8 : 0);
  else this->bindDrawLighting(draw, height, width);
  if (multitexture && !perSampleDepth) {
    if (!customDepth) { this->submitPortableDraw(view, this->shadowMultitextureProgram, draw, frame, PortableFamily::SHADOW_BASE); return; }
    const bool sampleInvocation = bgfxExplicitSurfaceSamples();
    const auto program = sampleInvocation ? this->shadowMultitextureProgram : this->shadowMultitextureSampleDepthProgram;
    const uint32_t draws = sampleInvocation ? 1 : this->sampleCount;
    for (uint32_t sample = 0; sample < draws; ++sample) {
      const float info[4] = {1.0f / float(width), 1.0f / float(height), float(sample), 0};
      bgfx::setUniform(this->depthInfoUniform, info);
      bgfx::setSampleMask(sampleInvocation ? UINT32_MAX : (1u << sample));
      this->submitPortableDraw(view, program, draw, frame, sampleInvocation ? PortableFamily::SHADOW_BASE : PortableFamily::SHADOW_SAMPLE, 0, sample + 1 == draws ? BGFX_DISCARD_ALL : BGFX_DISCARD_NONE);
    }
    bgfx::setSampleMask();
    return;
  }
  if (!perSampleDepth) {
    if (this->sampleCount == 1) {
      this->submitPortableDraw(view, hasShadows ? (shadowPlan.passes.size() > 4 ? this->shadowReceiverProgram8 :
        shadowPlan.passes.size() > 2 ? this->shadowReceiverProgram4 : this->shadowReceiverProgram) : this->activeProgram, draw, frame, hasShadows ? PortableFamily::SHADOW_BASE : PortableFamily::BASE);
    } else if (!customDepth) {
      // No fragment-depth output: retain hardware depth at each covered sample.
      this->submitPortableDraw(view, hasShadows ? this->shadowNativeDepthProgram :
        instanced ? this->instancedNativeDepthProgram :
        texture2dDepth ? this->texture2dNativeDepthProgram : this->surfaceNativeDepthProgram, draw, frame, hasShadows ? PortableFamily::SHADOW_BASE : PortableFamily::NATIVE_DEPTH);
    } else {
      const auto program = hasShadows ? this->shadowSampleDepthProgram :
        instanced ? this->instancedSampleDepthProgram :
        texture2dDepth ? this->texture2dSampleDepthProgram : this->surfaceSampleDepthProgram;
      const bool sampleInvocation = bgfx::getRendererType() == bgfx::RendererType::OpenGL ||
        bgfx::getRendererType() == bgfx::RendererType::OpenGLES;
      const uint32_t draws = sampleInvocation ? 1 : this->sampleCount;
      for (uint32_t sample = 0; sample < draws; ++sample) {
        const float info[4] = {1.0f / float(width), 1.0f / float(height), float(sample), 0};
        bgfx::setUniform(this->depthInfoUniform, info);
        bgfx::setSampleMask(sampleInvocation ? UINT32_MAX : (1u << sample));
        this->submitPortableDraw(view, program, draw, frame, hasShadows ? PortableFamily::SHADOW_SAMPLE : PortableFamily::SAMPLE_DEPTH, 0, sample + 1 == draws ? BGFX_DISCARD_ALL : BGFX_DISCARD_NONE);
      }
      bgfx::setSampleMask();
    }
    return;
  }
  // Use the selector's exact depth expression also for every opaque/immediate
  // writer in this frame. Native opaque depth (or pixel-center shader depth)
  // can round differently and breaks coplanar EQUAL/LEQUAL at individual samples.
  // The scratch depth is a distinct attachment, never the output being written;
  // these disabled selection tests perform no reads from its contents.
  const float meta[4] = {0, 0, 0, 0};
  bgfx::setUniform(this->peelMetaUniform, meta);
  bgfx::setTexture(multitexture?8:0, this->previousDepthSampler, sampleDepth);
  bgfx::setTexture(multitexture?9:1, this->opaqueDepthSampler, sampleDepth);
  const auto program = multitexture ? this->shadowMultitexturePeelProgram : hasShadows ? this->shadowPeelMsaaProgram : this->peelMsaaProgram;
  for (uint32_t sample = 0; sample < this->sampleCount; ++sample) {
    const float info[4] = {1.0f / float(width), 1.0f / float(height), float(sample), 0};
    bgfx::setUniform(this->depthInfoUniform, info);
    bgfx::setSampleMask(peelFixedSampleMask(this->sampleCount, sample));
    this->submitPortableDraw(view, program, draw, frame, hasShadows ? PortableFamily::SHADOW_PEEL : PortableFamily::PEEL, 0, sample + 1 == this->sampleCount ? BGFX_DISCARD_ALL : BGFX_DISCARD_NONE);
  }
  bgfx::setSampleMask();
}

bgfx::ViewId
CoinBgfxBackend::encodeDeferredRuns(const std::vector<CoinBgfxDraw> & draws,
  bgfx::DynamicVertexBufferHandle vertices, bgfx::DynamicIndexBufferHandle indices,
  bgfx::FrameBufferHandle output, const std::vector<bgfx::TextureHandle> & textures,
  bgfx::ViewId next, int width, int height, const CoinRenderFramePlan & frame,
  const CoinRenderShadowPlan & shadows, const std::vector<bgfx::FrameBufferHandle> & maps,
  CoinBgfxTransparencyStrategy strategy, const std::vector<bgfx::FrameBufferHandle> & layers,
  bgfx::FrameBufferHandle oit, bgfx::TextureHandle sampleDepth)
{
  std::vector<CoinBgfxDraw> run, depthContext;
  const bool weighted = strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT;
  const auto encodeDirect = [&](const std::vector<CoinBgfxDraw> & ordered) {
      bgfx::setViewName(next,"ordered_transparency");bgfx::setViewMode(next,bgfx::ViewMode::Sequential);
      bgfx::setViewRect(next,0,0,uint16_t(width),uint16_t(height));bgfx::setViewFrameBuffer(next,output);
      bgfx::setViewClear(next,BGFX_CLEAR_NONE);bgfx::setViewTransform(next,nullptr,nullptr);
      for(const auto & draw:ordered) {
        if(!draw.isSceneLayer() || !draw.blend || !draw.deferred || !setDrawScissor(draw,width,height))continue;
        bgfx::setTransform(draw.mvp);bgfx::setVertexBuffer(0,vertices);bgfx::setIndexBuffer(indices,draw.firstIndex,draw.indexCount);
        bgfx::setState(drawState(draw)|(this->sampleCount>1?BGFX_STATE_MSAA:0));
        this->submitSurfaceDraw(next,draw,textures,frame,shadows,maps,width,height,sampleDepth);
        if (weighted && draw.depthTest && draw.depthWrite) depthContext.push_back(draw);
      }
      ++next;
  };
  // Ordinary object composition already has a final common order. Iterate
  // it directly, without copying draw payloads into a temporary run.
  if (strategy == CoinBgfxTransparencyStrategy::OBJECT) { encodeDirect(draws); return next; }
  // Weighted accumulation reconstructs depth in its private attachments. Keep
  // every preceding depth writer separate from the current run's color inputs.
  if (weighted) for (const auto & draw : draws)
    if (draw.isSceneLayer() && !(draw.blend && draw.deferred) && draw.depthTest && draw.depthWrite)
      depthContext.push_back(draw);
  const auto flush = [&]() {
    if (run.empty()) return;
    if (!run.front().additive && strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS) {
      this->encodeSortedLayers(run,vertices,indices,output,textures,next,width,height,frame,shadows,maps,layers,oit);
      next += bgfx::ViewId(layers.size()+1);
    } else if (!run.front().additive && strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT) {
      this->encodeWeightedOit(run,vertices,indices,output,textures,next,width,height,frame,shadows,maps,{},oit,depthContext);
      next += 2;
    } else {
      encodeDirect(run);
    }
    run.clear();
  };
  for(const auto & draw:draws) {
    if(!draw.isSceneLayer()||!draw.blend||!draw.deferred)continue;
    if(!run.empty() && strategy!=CoinBgfxTransparencyStrategy::OBJECT && run.back().additive!=draw.additive)flush();
    run.push_back(draw);
  }
  flush();return next;
}

void
CoinBgfxBackend::bindDrawLighting(const CoinBgfxDraw & draw, int targetHeight, int targetWidth)
{
  const float door[4] = {draw.screenDoor[0], float(targetHeight > 0 ? targetHeight : this->height),
    bgfx::getCaps()->originBottomLeft ? 1.0f : 0.0f, draw.screenDoor[3]};
  bgfx::setUniform(this->screenDoorUniform, door);
  const float raster[4] = {0.0f, 0.0f,
    this->canonicalCoverage ? float(targetWidth > 0 ? targetWidth : this->width) : 0.0f,
    this->canonicalCoverage ? float(targetHeight > 0 ? targetHeight : this->height) : 0.0f};
  bgfx::setUniform(this->rasterViewportUniform, raster);
  const float alphaTest[4] = {static_cast<float>(draw.alphaTestFunction), draw.alphaTestReference, 0.0f, 0.0f};
  bgfx::setUniform(this->alphaTestUniform, alphaTest);
  bgfx::setUniform(this->clipMetaUniform, draw.clipMeta);
  bgfx::setUniform(this->clipPlanesUniform, draw.clipPlanes, COIN_RENDER_MAX_CLIP_PLANES);
  // Use two D24 LSBs on GL to survive the gl_FragCoord-to-gl_FragDepth
  // floating-point round trip; other renderers retain the one-LSB contract.
  // Driver-specific native polygon offset resolution remains approximate.
  const bgfx::RendererType::Enum renderer = bgfx::getRendererType();
  const float unitScale =
    (renderer == bgfx::RendererType::OpenGL ||
     renderer == bgfx::RendererType::OpenGLES)
      ? 1.0f / 8388608.0f : 1.0f / 16777216.0f;
  const float depth[4] = {draw.depthRange[0], draw.depthRange[1],
    draw.polygonOffsetFactor, draw.polygonOffsetUnits * unitScale + draw.polygonOffsetSlopeBias};
  bgfx::setUniform(this->coinDepthUniform, depth);
  bgfx::setUniform(this->fogColorModeUniform, draw.fogColorMode);
  bgfx::setUniform(this->fogRangeUniform, draw.fogRange);
  bgfx::setUniform(this->ambientLightUniform, draw.ambientLight);
  bgfx::setUniform(this->lightCountUniform, draw.lightCount);
  bgfx::setUniform(this->lightPositionTypeUniform, draw.lightPositionType, COIN_RENDER_MAX_LIGHTS);
  bgfx::setUniform(this->lightDirectionCutoffUniform, draw.lightDirectionCutoff, COIN_RENDER_MAX_LIGHTS);
  bgfx::setUniform(this->lightColorIntensityUniform, draw.lightColorIntensity, COIN_RENDER_MAX_LIGHTS);
  bgfx::setUniform(this->lightAmbientColorUniform, draw.lightAmbientColor, COIN_RENDER_MAX_LIGHTS);
  bgfx::setUniform(this->lightAttenuationDropUniform, draw.lightAttenuationDrop, COIN_RENDER_MAX_LIGHTS);
  if (draw.instanceCount)
    bgfx::setUniform(this->instancedCameraUniform, draw.instanceCamera, 6);
}

void
CoinBgfxBackend::encodeSortedLayers(const std::vector<CoinBgfxDraw> & draws,
                                      bgfx::DynamicVertexBufferHandle vertices,
                                      bgfx::DynamicIndexBufferHandle indices,
                                      bgfx::FrameBufferHandle output,
                                      const std::vector<bgfx::TextureHandle> & textures,
  bgfx::ViewId firstView, int width, int height, const CoinRenderFramePlan & frame,
  const CoinRenderShadowPlan & shadows, const std::vector<bgfx::FrameBufferHandle> & shadowMaps,
  const std::vector<bgfx::FrameBufferHandle> & layers, bgfx::FrameBufferHandle oitBuffer)
{
  const bool hasShadows = !shadows.passes.empty();
  const float transparentBlack[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const float depthInfo[4] = {1.0f / float(width),
                              1.0f / float(height), 0.0f, 0.0f};
  const bgfx::TextureHandle opaqueDepth = bgfx::getTexture(output, 1);
  bgfx::setPaletteColor(1, transparentBlack);
  for (uint8_t pass = 0; pass < layers.size(); ++pass) {
    const bgfx::ViewId view = firstView + pass;
    bgfx::setViewName(view, "transparent_accumulation");
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(view, 0, 0, static_cast<uint16_t>(width),
                      static_cast<uint16_t>(height));
    bgfx::setViewFrameBuffer(view, layers[pass]);
    bgfx::setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.0f, 0, 1, 1);
    bgfx::setViewTransform(view, nullptr, nullptr);
    bgfx::touch(view);
    for (uint32_t sample = 0; sample < this->sampleCount; ++sample) {
      const float selectionInfo[4] = {depthInfo[0], depthInfo[1], float(sample), 0};
      for (const CoinBgfxDraw & draw : draws) {
        if (!draw.isSceneLayer() || !draw.blend || !draw.deferred || draw.additive) continue;
        bgfx::setTransform(draw.mvp);
        bgfx::setVertexBuffer(0, vertices);
        bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
        bgfx::setSampleMask(peelFixedSampleMask(this->sampleCount, sample));
        bgfx::setState(peelSelectionState(draw) | (this->sampleCount > 1 ? BGFX_STATE_MSAA : 0));
        if (!setDrawScissor(draw, width, height)) continue;
        const float peelMeta[4] = {float(pass), draw.depthTest ? 1.0f : 0.0f,
          float(draw.depthFunction), draw.depthTest && draw.depthWrite ? 1.0f : 0.0f};
        bgfx::setUniform(this->depthInfoUniform, selectionInfo);
        bgfx::setUniform(this->peelMetaUniform, peelMeta);
        bgfx::setTexture(this->shadowArrayActive?8:0, this->previousDepthSampler,
          pass == 0 ? opaqueDepth : bgfx::getTexture(layers[pass - 1], 2));
        bgfx::setTexture(this->shadowArrayActive?9:1, this->opaqueDepthSampler, opaqueDepth);
        this->bindDrawTexture(draw, textures, hasShadows,false,hasShadows&&this->shadowArrayActive);
        if (hasShadows) this->bindShadowReceiver(frame, shadows, shadowMaps, draw, height, 8);
        else this->bindDrawLighting(draw, height);
        this->submitPortableDraw(view, hasShadows&&this->shadowArrayActive ? this->shadowMultitexturePeelProgram : this->sampleCount > 1 ? (hasShadows ? this->shadowPeelMsaaProgram : this->peelMsaaProgram) :
          (hasShadows ? this->shadowPeelProgram : this->peelNextProgram), draw, frame, hasShadows ? PortableFamily::SHADOW_PEEL : PortableFamily::PEEL);
      }
    }
    bgfx::setSampleMask();
  }

  const bgfx::ViewId compositeView = firstView + layers.size();
  bgfx::setViewName(compositeView, "fullscreen_composition");
  bgfx::setViewMode(compositeView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(compositeView, 0, 0, static_cast<uint16_t>(width),
                    static_cast<uint16_t>(height));
  bgfx::setViewFrameBuffer(compositeView, output);
  bgfx::setViewClear(compositeView, BGFX_CLEAR_NONE);
  bgfx::setViewTransform(compositeView, nullptr, nullptr);
  bgfx::touch(compositeView);
  const float identity[16] = {1, 0, 0, 0,
                              0, 1, 0, 0,
                              0, 0, 1, 0,
                              0, 0, 0, 1};
  const uint64_t blendState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
    BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,
      BGFX_STATE_BLEND_INV_SRC_ALPHA, BGFX_STATE_BLEND_ONE,
      BGFX_STATE_BLEND_INV_SRC_ALPHA);
  for (int pass = int(layers.size()) - 1; pass >= 0; --pass) {
    if (this->sampleCount > 1) {
      for (uint32_t sample = 0; sample < this->sampleCount; ++sample) {
        for (unsigned depthPass = 0; depthPass < 2; ++depthPass) {
          const float info[4] = {depthInfo[0], depthInfo[1], depthPass ? 2.0f : 1.0f, float(sample)};
          bgfx::setTransform(identity);
          bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
          bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
          bgfx::setUniform(this->depthInfoUniform, info);
          bgfx::setTexture(0, this->peelMsaaColorSampler, bgfx::getTexture(layers[pass], 0));
          bgfx::setTexture(1, this->peelMsaaMaskSampler, bgfx::getTexture(layers[pass], 1));
          bgfx::setTexture(2, this->peelMsaaDepthSampler, bgfx::getTexture(layers[pass], 2));
          bgfx::setSampleMask(peelFixedSampleMask(this->sampleCount, sample));
          bgfx::setState((depthPass ? BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_ALWAYS : blendState) | BGFX_STATE_MSAA);
          bgfx::submit(compositeView, this->peelMsaaCompositeProgram);
        }
      }
      bgfx::setSampleMask();
      continue;
    }
    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
    bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
    bgfx::setUniform(this->depthInfoUniform, depthInfo);
    bgfx::setTexture(0, this->layerSampler,
                     bgfx::getTexture(layers[pass], 0));
    bgfx::setState(blendState);
    bgfx::submit(compositeView, this->compositeProgram);

    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
    bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
    bgfx::setUniform(this->depthInfoUniform, depthInfo);
    bgfx::setTexture(0, this->layerMaskSampler, bgfx::getTexture(layers[pass], 1));
    bgfx::setTexture(1, this->layerDepthSampler, bgfx::getTexture(layers[pass], 2));
    bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_ALWAYS);
    bgfx::submit(compositeView, this->compositeDepthProgram);
  }
}

void
CoinBgfxBackend::encodeWeightedOit(const std::vector<CoinBgfxDraw> & draws,
                                     bgfx::DynamicVertexBufferHandle vertices,
                                     bgfx::DynamicIndexBufferHandle indices,
                                     bgfx::FrameBufferHandle output,
                                     const std::vector<bgfx::TextureHandle> & textures,
  bgfx::ViewId firstView, int width, int height, const CoinRenderFramePlan & frame,
  const CoinRenderShadowPlan & shadows, const std::vector<bgfx::FrameBufferHandle> & shadowMaps,
  const std::vector<bgfx::FrameBufferHandle> & layers, bgfx::FrameBufferHandle oitBuffer,
  const std::vector<CoinBgfxDraw> & depthContext)
{
  const bool hasShadows = !shadows.passes.empty();
  const bgfx::ViewId oitView = firstView;
  bgfx::setViewName(oitView, "transparent_accumulation");
  const float transparentBlack[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const float opaqueWhite[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  const float depthInfo[4] = {1.0f / float(width),
                              1.0f / float(height), 0.0f, 0.0f};
  bgfx::setPaletteColor(1, transparentBlack);
  bgfx::setPaletteColor(2, opaqueWhite);
  bgfx::setViewMode(oitView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(oitView, 0, 0, static_cast<uint16_t>(width),
                    static_cast<uint16_t>(height));
  bgfx::setViewFrameBuffer(oitView, oitBuffer);
  bgfx::setViewClear(oitView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                     1.0f, 0, 1, 2);
  bgfx::setViewTransform(oitView, nullptr, nullptr);
  bgfx::touch(oitView);

  // Rebuild the already-published depth history: immediate geometry followed
  // by explicit depth writers in preceding additive runs. These draws never
  // enter this run's accumulation/color loops.
  for (const CoinBgfxDraw & draw : depthContext) {
    if (!draw.isSceneLayer() || !draw.depthTest || !draw.depthWrite)
      continue;
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vertices);
    bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
    bgfx::setState(peelDrawState(draw, true) | (this->sampleCount > 1 ? BGFX_STATE_MSAA : 0));
    if (!setDrawScissor(draw, width, height)) continue;
    this->submitSurfaceDraw(oitView, draw, textures, frame, CoinRenderShadowPlan(), {},
      width, height, bgfx::TextureHandle BGFX_INVALID_HANDLE);

  }

  for (const CoinBgfxDraw & draw : draws) {
    if (!draw.isSceneLayer() || !draw.blend || !draw.deferred || draw.additive)
      continue;
    uint64_t state = drawState(draw);
    state &= ~(BGFX_STATE_WRITE_Z | BGFX_STATE_BLEND_MASK);
    state |= BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
             BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE) |
             BGFX_STATE_BLEND_INDEPENDENT;
    const uint32_t independentBlend = BGFX_STATE_BLEND_FUNC_RT_1(
      BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_INV_SRC_COLOR);
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vertices);
    bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
    bgfx::setState(state | (this->sampleCount > 1 ? BGFX_STATE_MSAA : 0), independentBlend);
    if (!setDrawScissor(draw, width, height)) continue;
    this->bindDrawTexture(draw, textures, hasShadows,false,hasShadows&&this->shadowArrayActive);
    if (hasShadows) this->bindShadowReceiver(frame, shadows, shadowMaps, draw, height, 8);
    else this->bindDrawLighting(draw, height);
    if(hasShadows&&this->shadowArrayActive) {
      const bool sampleInvocation=bgfxExplicitSurfaceSamples();
      const uint32_t samples=this->sampleCount>1&&!sampleInvocation?this->sampleCount:1;
      for(uint32_t sample=0;sample<samples;++sample) {
        const float info[4]={1.0f/float(width),1.0f/float(height),float(sample),0};
        bgfx::setUniform(this->depthInfoUniform,info);
        bgfx::setSampleMask(samples>1?1u<<sample:UINT32_MAX);
        this->submitPortableDraw(oitView,this->shadowMultitextureOitProgram, draw, frame, PortableFamily::SHADOW_OIT,0,sample+1==samples?BGFX_DISCARD_ALL:BGFX_DISCARD_NONE);
      }
      bgfx::setSampleMask();
    } else this->submitPortableDraw(oitView, hasShadows ? this->shadowOitProgram : this->weightedOitProgram, draw, frame, hasShadows ? PortableFamily::SHADOW_OIT : PortableFamily::OIT);

  }

  const bgfx::ViewId compositeView = firstView + 1;
  bgfx::setViewName(compositeView, "fullscreen_composition");
  bgfx::setViewMode(compositeView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(compositeView, 0, 0, static_cast<uint16_t>(width),
                    static_cast<uint16_t>(height));
  bgfx::setViewFrameBuffer(compositeView, output);
  bgfx::setViewClear(compositeView, BGFX_CLEAR_NONE);
  bgfx::setViewTransform(compositeView, nullptr, nullptr);
  bgfx::touch(compositeView);
  const float identity[16] = {1, 0, 0, 0,
                              0, 1, 0, 0,
                              0, 0, 1, 0,
                              0, 0, 0, 1};
  const uint64_t blendState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
    BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,
      BGFX_STATE_BLEND_INV_SRC_ALPHA, BGFX_STATE_BLEND_ONE,
      BGFX_STATE_BLEND_INV_SRC_ALPHA);
  for (uint32_t sample = 0; sample < this->sampleCount; ++sample) {
    const float info[4] = {depthInfo[0], depthInfo[1], 1.0f, float(sample)};
    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
    bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
    bgfx::setUniform(this->depthInfoUniform, info);
    bgfx::setTexture(0, this->sampleCount > 1 ? this->weightedMsaaAccumSampler : this->oitAccumSampler,
                     bgfx::getTexture(oitBuffer, 0));
    bgfx::setTexture(1, this->sampleCount > 1 ? this->weightedMsaaRevealSampler : this->oitRevealSampler,
                     bgfx::getTexture(oitBuffer, 1));
    bgfx::setSampleMask(this->sampleCount > 1 ? 1u << sample : UINT32_MAX);
    bgfx::setState(blendState | (this->sampleCount > 1 ? BGFX_STATE_MSAA : 0));
    bgfx::submit(compositeView, this->sampleCount > 1 ? this->weightedMsaaCompositeProgram : this->weightedCompositeProgram);
  }
  bgfx::setSampleMask();
}

bool
CoinBgfxBackend::encodeOverlayLayers(
  const std::vector<CoinBgfxDraw> & draws,
  bgfx::DynamicVertexBufferHandle vertices,
  bgfx::DynamicIndexBufferHandle indices,
  bgfx::FrameBufferHandle output,
  const std::vector<bgfx::TextureHandle> & textures,
  bgfx::ViewId & nextView, int width, int height,
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadowPlan,
  const std::vector<bgfx::FrameBufferHandle> & shadowMaps, bool beforeScene,
  bgfx::TextureHandle sampleDepth)
{
  const bool hasShadows = !shadowPlan.passes.empty();
  uint32_t sceneLayer = 0;
  for (const auto & draw : draws) if (draw.authoredScene) { sceneLayer = draw.renderLayer; break; }
  uint32_t encodedLayer = 0;
  for (const CoinBgfxDraw & layerDraw : draws) {
    if (layerDraw.renderLayer == 0 || layerDraw.renderLayer == encodedLayer ||
        (beforeScene ? layerDraw.renderLayer > sceneLayer : layerDraw.renderLayer <= sceneLayer)) continue;
    encodedLayer = layerDraw.renderLayer;
    const CoinBgfxDraw * barrier = nullptr;
    for (const CoinBgfxDraw & draw : draws) {
      if (draw.renderLayer == encodedLayer && draw.clearDepthBefore) {
        barrier = &draw;
        break;
      }
    }
    int32_t clipped[4];
    if (barrier && CoinBgfxLowering::clipViewport(barrier->viewport, width,
                                               height, clipped)) {
      const bgfx::ViewId clearView = nextView++;
      const int32_t top = height - clipped[1] - clipped[3];
      bgfx::setViewName(clearView, "overlay_depth_clear");
      bgfx::setViewMode(clearView, bgfx::ViewMode::Sequential);
      bgfx::setViewRect(clearView, static_cast<uint16_t>(clipped[0]),
                        static_cast<uint16_t>(top),
                        static_cast<uint16_t>(clipped[2]),
                        static_cast<uint16_t>(clipped[3]));
      bgfx::setViewFrameBuffer(clearView, output);
      bgfx::setViewClear(clearView, BGFX_CLEAR_DEPTH, 1.0f, 0);
      bgfx::setViewTransform(clearView, nullptr, nullptr);
      bgfx::touch(clearView);
    }
    // The authored scene has its own opaque/deferred passes after this barrier.
    if (layerDraw.isSceneLayer()) continue;
    // Overlays are immediate traversals: transparent depth-writing geometry
    // may intentionally occlude later opaque or textured primitives. A split
    // into opaque/transparent views would undo the stable per-layer ordering.
    const bgfx::ViewId overlayView = nextView++;
    bgfx::setViewName(overlayView, "overlay_immediate");
    bgfx::setViewMode(overlayView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(overlayView, 0, 0, static_cast<uint16_t>(width),
                      static_cast<uint16_t>(height));
    bgfx::setViewFrameBuffer(overlayView, output);
    bgfx::setViewClear(overlayView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(overlayView, nullptr, nullptr);
    bgfx::touch(overlayView);
    for (const CoinBgfxDraw & draw : draws) {
      if (draw.renderLayer != encodedLayer || !draw.indexCount) continue;
      bgfx::setTransform(draw.mvp);
      bgfx::setVertexBuffer(0, vertices);
      bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
      bgfx::setState(drawState(draw) | (this->sampleCount > 1 ? BGFX_STATE_MSAA : 0));
      if (!setDrawScissor(draw, width, height)) continue;
      this->submitSurfaceDraw(overlayView, draw, textures, frame, shadowPlan, shadowMaps,
        width, height, sampleDepth);

    }
  }
  return true;
}

void
CoinBgfxBackend::bindShadowReceiver(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadowPlan,
  const std::vector<bgfx::FrameBufferHandle> & shadowMaps,
  const CoinBgfxDraw & original, int targetHeight, unsigned declaredShadowSlots)
{
  // Every sampler declared by the active program needs its own typed binding,
  // including dynamically inactive shadow slots. Otherwise a fresh GL program
  // can leave a sampler2D at unit zero, aliasing peeling's sampler2DMS there and
  // invalidating the whole draw. Ordinary receivers declare 2/4/8 slots;
  // transparency receivers always declare eight. Never bind absent stages.
  const unsigned shadowSlots = declaredShadowSlots ? declaredShadowSlots :
    (shadowPlan.passes.size() > 4 ? 8u : shadowPlan.passes.size() > 2 ? 4u : 2u);
  for (unsigned slot = 0; !this->shadowArrayActive && slot < shadowSlots; ++slot)
    bgfx::setTexture(static_cast<uint8_t>(8 + slot), this->shadowSampler[slot], this->defaultTexture);
  CoinBgfxDraw shaded = original;
  shaded.lightCount[1] = 0.0f;
  float indices[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
  const auto & state = frame.renderStates[original.renderStateSlot];
  const auto & flatMaterial = frame.materials[state.materialSlot];
  const float flatParams[4] = {flatMaterial.shininess,
    state.flatShadowPrimary ? 1.0f : 0.0f, 0.0f, 0.0f};
  bgfx::setUniform(this->shadowFlatDiffuseUniform, flatMaterial.diffuse);
  bgfx::setUniform(this->shadowFlatSpecularUniform, flatMaterial.specular);
  bgfx::setUniform(this->shadowFlatParamsUniform, flatParams);
  const uint32_t flags = coin_render_shadow_shading_flags(frame, state);
  const float quality[4] = {float((flags & COIN_RENDER_SHADOW_VERTEX_LIGHTING) != 0),
    float((flags & COIN_RENDER_SHADOW_ORDINARY_FRAGMENT) != 0),
    float((flags & COIN_RENDER_SHADOW_GROUP_LIGHTING) != 0), 0};
  bgfx::setUniform(this->shadowQualityUniform, quality);
  if (state.lightingSlot < frame.lightingStates.size()) {
    const auto & lights = frame.lightingStates[state.lightingSlot].lights;
    for (size_t i = 0; i < lights.size() && i < COIN_RENDER_MAX_LIGHTS; ++i)
      if (coin_render_shadow_suppresses_ordinary_light(frame, state, lights[i]))
        shaded.lightColorIntensity[i][3] = 0.0f;
  }
  const SbMatrix clipConversion(
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.5f, 0.0f,
    0.0f, 0.0f, 0.5f, 1.0f);
  for (size_t slot = 0; slot < shadowPlan.passes.size(); ++slot) {
    const size_t passSlot = slot;
    const auto & pass = shadowPlan.passes[passSlot];
    if (state.shadowGroupSlot != pass.groupSlot) continue;
    const int32_t index = pass.lightingIndexByState[original.renderStateSlot];
    if (index < 0) continue;
    // Unshadowed shapes retain ordinary lighting; only receivers sample VSM.
    if ((state.shadowStyle & 2u) == 0) continue;
    indices[slot] = static_cast<float>(index);
    const SbMatrix observerView = state.rasterShadowAnchorValid &&
      state.cameraSlot < frame.cameras.size()
        ? frame.cameras[state.cameraSlot].viewMatrix : state.view;
    const SbMatrix viewToLight = observerView.inverse() * pass.view;
    // Receiver clip tests use [0,1] independently of the rasterizer's clip domain.
    // This matrix samples the map; caster rasterization keeps its native projection.
    const SbMatrix projection = pass.projectionCoin * clipConversion;
    const SbMatrix viewToClip = viewToLight * projection;
    const float params[4] = {pass.nearDistance, pass.vsmFarDistance,
                             pass.epsilon, pass.threshold};
    const float meta[4] = {
      frame.shadowLights[pass.lightSlot].type == CoinRenderLightType::SPOT ? 1.0f : 0.0f,
      pass.maxShadowDistance, pass.distanceFalloffCoefficient,
      bgfx::getCaps()->originBottomLeft ? 1.0f : 0.0f};
    bgfx::setUniform(this->shadowViewToLightUniform[slot], viewToLight.getValue());
    bgfx::setUniform(this->shadowViewToClipUniform[slot], viewToClip.getValue());
    bgfx::setUniform(this->shadowParamsUniform[slot], params);
    bgfx::setUniform(this->shadowMetaUniform[slot], meta);
    const float texel[4] = {1.0f / float(pass.mapSize), 1.0f / float(pass.mapSize),
      1.0f / float(this->shadowArraySize), float(slot)};
    bgfx::setUniform(this->shadowTexelUniform[slot], texel);
    if (!this->shadowArrayActive) bgfx::setTexture(static_cast<uint8_t>(8 + slot), this->shadowSampler[slot],
      bgfx::getTexture(shadowMaps[passSlot], 0));
  }
  if (this->shadowArrayActive) bgfx::setTexture(10,this->shadowArraySampler,this->shadowArrayTexture);
  bgfx::setUniform(this->shadowLightIndicesUniform, indices);
  bgfx::setUniform(this->shadowLightIndicesExtraUniform, indices + 4);
  this->bindDrawLighting(shaded, targetHeight);
}
