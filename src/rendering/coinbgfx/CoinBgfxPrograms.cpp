// CoinRender BGFX Programs responsibilities.
#include "rendering/coinbgfx/CoinBgfxBackendInternal.h"
#include "coin_bgfx_fs_bump_receiver_portable_glsl.h"
#include "coin_bgfx_fs_bump_receiver_native_depth_portable_glsl.h"
#include "coin_bgfx_fs_bump_receiver_sample_depth_portable_glsl.h"
#include "coin_bgfx_fs_bump_receiver_portable_spirv.h"
#include "coin_bgfx_fs_bump_receiver_native_depth_portable_spirv.h"
#include "coin_bgfx_fs_bump_receiver_sample_depth_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_bump_receiver_portable_dx11.h"
#include "coin_bgfx_fs_bump_receiver_native_depth_portable_dx11.h"
#include "coin_bgfx_fs_bump_receiver_sample_depth_portable_dx11.h"
#endif
#include "coin_bgfx_vs_bump_receiver_glsl.h"
#include "coin_bgfx_fs_bump_receiver_glsl.h"
#include "coin_bgfx_fs_bump_receiver_native_depth_glsl.h"
#include "coin_bgfx_fs_bump_receiver_sample_depth_glsl.h"
#include "coin_bgfx_vs_bump_receiver_spirv.h"
#include "coin_bgfx_fs_bump_receiver_spirv.h"
#include "coin_bgfx_fs_bump_receiver_native_depth_spirv.h"
#include "coin_bgfx_fs_bump_receiver_sample_depth_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_vs_bump_receiver_dx11.h"
#include "coin_bgfx_fs_bump_receiver_dx11.h"
#include "coin_bgfx_fs_bump_receiver_native_depth_dx11.h"
#include "coin_bgfx_fs_bump_receiver_sample_depth_dx11.h"
#endif
#include "coin_bgfx_vs_bump_glsl.h"
#include "coin_bgfx_fs_bump_glsl.h"
#include "coin_bgfx_fs_bump_native_depth_glsl.h"
#include "coin_bgfx_fs_bump_sample_depth_glsl.h"
#include "coin_bgfx_vs_bump_spirv.h"
#include "coin_bgfx_fs_bump_spirv.h"
#include "coin_bgfx_fs_bump_native_depth_spirv.h"
#include "coin_bgfx_fs_bump_sample_depth_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_vs_bump_dx11.h"
#include "coin_bgfx_fs_bump_dx11.h"
#include "coin_bgfx_fs_bump_native_depth_dx11.h"
#include "coin_bgfx_fs_bump_sample_depth_dx11.h"
#endif
#include "coin_bgfx_vs_glsl.h"
#include "coin_bgfx_fs_glsl.h"
#include "coin_bgfx_vs_spirv.h"
#include "coin_bgfx_fs_spirv.h"
#include "coin_bgfx_fs_solid_color_glsl.h"
#include "coin_bgfx_fs_texture2d_glsl.h"
#include "coin_bgfx_fs_texture2d_portable_glsl.h"
#include "coin_bgfx_fs_solid_color_spirv.h"
#include "coin_bgfx_fs_texture2d_spirv.h"
#include "coin_bgfx_fs_texture2d_portable_spirv.h"
#include "coin_bgfx_vs_instanced_color_glsl.h"
#include "coin_bgfx_vs_instanced_color_spirv.h"
#include "coin_bgfx_fs_instanced_color_glsl.h"
#include "coin_bgfx_fs_instanced_color_spirv.h"
#include "coin_bgfx_vs_shadow_multitexture_glsl.h"
#include "coin_bgfx_vs_shadow_multitexture_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_vs_shadow_multitexture_dx11.h"
#endif
#include "coin_bgfx_fs_portable_glsl.h"
#include "coin_bgfx_fs_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_portable_dx11.h"
#endif
#ifdef _WIN32
#include "coin_bgfx_vs_dx11.h"
#include "coin_bgfx_fs_dx11.h"
#include "coin_bgfx_fs_solid_color_dx11.h"
#include "coin_bgfx_fs_texture2d_dx11.h"
#include "coin_bgfx_fs_texture2d_portable_dx11.h"
#include "coin_bgfx_vs_instanced_color_dx11.h"
#include "coin_bgfx_fs_instanced_color_dx11.h"
#endif

using namespace coin_bgfx_internal;

bool CoinBgfxBackend::prepareBumpResources()
{
  const auto *caps = bgfx::getCaps();
  if (!caps || caps->limits.maxTextureSamplers < 2 || caps->limits.maxVertexAttributes < 8 ||
      !(caps->formats[bgfx::TextureFormat::RGBA8] & BGFX_CAPS_FORMAT_TEXTURE_2D)) {
    this->lastError =
        "BGFX bump needs two fragment samplers, eight active vertex attributes and RGBA8 sampling";
    return false;
  }
  if(!bgfx::isValid(this->depthInfoUniform))this->depthInfoUniform=bgfx::createUniform("u_depthInfo",bgfx::UniformType::Vec4);
  if(!bgfx::isValid(this->bumpImageSampler))this->bumpImageSampler=bgfx::createUniform("s_bumpImage",bgfx::UniformType::Sampler);
  if(!bgfx::isValid(this->bumpBaseSampler))this->bumpBaseSampler=bgfx::createUniform("s_bumpBase",bgfx::UniformType::Sampler);
  if(!bgfx::isValid(this->bumpParamsUniform))this->bumpParamsUniform=bgfx::createUniform("u_bumpParams",bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->depthInfoUniform) || !bgfx::isValid(this->bumpImageSampler) ||
      !bgfx::isValid(this->bumpBaseSampler) || !bgfx::isValid(this->bumpParamsUniform)) {
    this->lastError = "BGFX bump uniform allocation failed";
    return false;
  }
  return true;
}

bool CoinBgfxBackend::prepareBumpPrograms()
{
  if (!this->prepareBumpResources())
    return false;
  auto &program = this->sampleCount == 1         ? this->bumpProgram
                  : bgfxExplicitSurfaceSamples() ? this->bumpSampleDepthProgram
                                                 : this->bumpNativeDepthProgram;
  if (!bgfx::isValid(program))
    program = bgfxTracePreparedProgram(
        createLayerProgram(
            COIN_BGFX_SHADER_DATA(coin_bgfx_vs_bump), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_bump),
            this->sampleCount == 1         ? COIN_BGFX_SHADER_DATA(coin_bgfx_fs_bump)
            : bgfxExplicitSurfaceSamples() ? COIN_BGFX_SHADER_DATA(coin_bgfx_fs_bump_sample_depth)
                                           : COIN_BGFX_SHADER_DATA(coin_bgfx_fs_bump_native_depth),
            this->sampleCount == 1         ? COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_bump)
            : bgfxExplicitSurfaceSamples() ? COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_bump_sample_depth)
                                           : COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_bump_native_depth)),
        "legacy_bump", this->sampleCount);
  if (!bgfx::isValid(program)) {
    this->lastError = "BGFX bump pipeline resources unavailable";
    return false;
  }
  return true;
}

bool CoinBgfxBackend::prepareBumpReceiverPrograms(const CoinBgfxDraw &draw, bool windowTarget,
                                                   bgfx::TextureFormat::Enum colorFormat)
{
  if (!bgfx::isValid(this->bumpLightMetaUniform))
    this->bumpLightMetaUniform=bgfx::createUniform("u_coinBumpLightMeta",bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->bumpLightVectorsUniform))
    this->bumpLightVectorsUniform=bgfx::createUniform("u_coinBumpLightVectors",bgfx::UniformType::Vec4,8);
  if (!bgfx::isValid(this->bumpLightColorsUniform))
    this->bumpLightColorsUniform=bgfx::createUniform("u_coinBumpLightColors",bgfx::UniformType::Vec4,8);
  if (!bgfx::isValid(this->bumpEyeUniform))
    this->bumpEyeUniform=bgfx::createUniform("u_coinBumpEye",bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->bumpModelViewUniform))
    this->bumpModelViewUniform=bgfx::createUniform("u_coinBumpModelView",bgfx::UniformType::Mat4);
  if (!bgfx::isValid(this->bumpLightMetaUniform) || !bgfx::isValid(this->bumpLightVectorsUniform) ||
      !bgfx::isValid(this->bumpLightColorsUniform) || !bgfx::isValid(this->bumpEyeUniform) ||
      !bgfx::isValid(this->bumpModelViewUniform)) {
    this->lastError="Bump directional uniform allocation failed";
    return false;
  }

  if (bgfx::getCaps()->limits.maxVertexAttributes < 15) {
    this->lastError = "Bump receiver requires fifteen vertex attributes";
    return false;
  }
  if (!this->prepareBumpResources() || !this->prepareShadowMultitextureResources())
    return false;
  if (this->sampleCount > 1 && bgfxExplicitSurfaceSamples() &&
      !bgfxSampleInterpolationSupported(this->sampleCount, this->lastError))
    return false;
  auto &program = this->sampleCount == 1         ? this->bumpReceiverProgram
                  : bgfxExplicitSurfaceSamples() ? this->bumpReceiverSampleProgram
                                                 : this->bumpReceiverNativeProgram;
  if (!bgfx::isValid(program))
    program = bgfxTracePreparedProgram(
        createLayerProgram(
            COIN_BGFX_SHADER_DATA(coin_bgfx_vs_bump_receiver),
            COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_bump_receiver),
            this->sampleCount == 1 ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_bump_receiver)
            : bgfxExplicitSurfaceSamples()
                ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_bump_receiver_sample_depth)
                : COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_bump_receiver_native_depth),
            this->sampleCount == 1 ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_bump_receiver)
            : bgfxExplicitSurfaceSamples()
                ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_bump_receiver_sample_depth)
                : COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_bump_receiver_native_depth)),
        "bump_receiver", this->sampleCount);
  if (!bgfx::isValid(program)) {
    this->lastError = "Bump receiver pipeline unavailable";
    return false;
  }
  const auto *caps = bgfx::getCaps();
  const auto depthFormat = this->sampleCount > 1 ? bgfx::TextureFormat::D32F :
    (caps->formats[bgfx::TextureFormat::D24S8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)
      ? bgfx::TextureFormat::D24S8 : bgfx::TextureFormat::D32F;
  if (this->sampleCount > 1)
    colorFormat = bgfx::TextureFormat::RGBA8;
  else if (windowTarget && this->presentToWindow &&
           caps->rendererType != bgfx::RendererType::OpenGL &&
           !bgfx::isValid(this->windowSceneFrameBuffer) && !this->multipassSceneRequired)
    colorFormat = sharedRuntime().backbufferColorFormat;
  const uint64_t state = drawState(draw);
  const uint32_t formats = uint32_t(colorFormat) | (uint32_t(depthFormat) << 16);
  bool &verified = this->bumpReceiverVerifiedPipelines[std::make_pair(state, formats)];
  return this->verifyShadowMultitextureProgram(program, verified, 0, true,
                                              state, colorFormat, depthFormat);
}

bool CoinBgfxBackend::prepareBaseProgram(const std::vector<CoinBgfxDraw> &inputDraws,
                                         bool hasShadows, CoinBgfxTransparencyStrategy strategy,
                                         bool instanced, bool windowTarget,
                                         bgfx::TextureFormat::Enum colorFormat)
{
  const bool legacyBump =
      std::any_of(inputDraws.begin(), inputDraws.end(),
                  [](const CoinBgfxDraw &draw) { return draw.bumpSlot && !draw.bumpExtended; });
  const bool receiverBump =
      std::any_of(inputDraws.begin(), inputDraws.end(),
                  [](const CoinBgfxDraw &draw) { return draw.bumpSlot && draw.bumpExtended; });
  if (legacyBump && !this->prepareBumpPrograms())
    return false;
  if (receiverBump)
    for (const auto &draw : inputDraws)
      if (draw.bumpSlot && draw.bumpExtended &&
          !this->prepareBumpReceiverPrograms(draw, windowTarget, colorFormat))
        return false;
  const bool shadowArray =
      hasShadows && std::any_of(inputDraws.begin(), inputDraws.end(), [](const CoinBgfxDraw &draw) {
        return draw.portableShaderSlot || draw.bumpExtended || (draw.hasTexture && (draw.volumeDepth || draw.cubeFaceSize)) ||
               std::any_of(std::begin(draw.extraTextures), std::end(draw.extraTextures),
                           [](const CoinBgfxDraw::TextureLayer &layer) { return layer.enabled; });
      });
  // Bump and adapted draws select their own executors in submitSurfaceDraw.
  // Only ordinary occurrences require the fixed surface programs.
  std::vector<CoinBgfxDraw> draws;
  for (const auto &draw : inputDraws)
    if (!draw.bumpSlot && !draw.portableShaderSlot)
      draws.push_back(draw);
  this->activeProgram = BGFX_INVALID_HANDLE;
  this->activeTexture2dDepth = false;
  this->activeTextureUnitCount = 0;
  this->shadowMultitextureSampleDepthActive = hasShadows && this->sampleCount > 1 &&
    strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT && shadowArray;
  if (draws.empty()) {
    if (hasShadows && !this->prepareShadowMultitextureResources()) return false;
    // Sample-depth uniforms are resources, not a reason to create a fixed
    // surface executor. The existing MSAA attachment/probe remains unchanged.
    if (this->sampleCount > 1 && !this->prepareFullscreenResources()) return false;
    return true;
  }
  if (shadowArray && (!this->prepareShadowMultitextureProgram() ||
                      (this->sampleCount > 1 &&
                       (this->shadowMultitextureSampleDepthActive ||
                        std::any_of(draws.begin(), draws.end(), bgfxCustomSampleDepth)) &&
                       !this->prepareShadowMultitextureProgram(true)) ||
                      (strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
                       !this->prepareShadowMultitextureProgram(false, 1)) ||
                      (strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT &&
                       !this->prepareShadowMultitextureProgram(false, 2))))
    return false;
  if (hasShadows && strategy == CoinBgfxTransparencyStrategy::OBJECT) {
    if (!shadowArray && !this->prepareSurfaceDepthPrograms(draws, true, false))
      return false;
    this->activeTextureUnitCount = COIN_RENDER_MAX_TEXTURE_UNITS;
    return true;
  }
  const char * disableSolid = std::getenv("COIN_BGFX_DISABLE_SOLID_PROGRAM");
  const bool solid = !(disableSolid && std::strcmp(disableSolid, "1") == 0) &&
    coin_bgfx_solid_program(draws, hasShadows, strategy);
  const char * disableTexture2d = std::getenv("COIN_BGFX_DISABLE_TEXTURE2D_PROGRAM");
  const bool texture2d = !instanced &&
    !(disableTexture2d && std::strcmp(disableTexture2d, "1") == 0) &&
    coin_bgfx_texture2d_program(draws, hasShadows, strategy);
  if (texture2d && this->sampleCount > 1) {
    // These draws never submit the single-sample activeProgram. Prepare only
    // their eligible depth family; keep mixed native/custom depth per draw.
    if (!this->prepareSurfaceDepthPrograms(draws, false, false, true))
      return false;
    this->activeTexture2dDepth = true;
    this->activeTextureUnitCount = 1;
    if (CoinRenderDiagnosticShell::phaseTracingEnabled())
      std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_base_program solid=0 instancing=0 texture2d=1\n");
    return true;
  }
  bgfx::ProgramHandle & selected = instanced ? this->instancedProgram :
    solid ? this->solidProgram : texture2d ? this->texture2dProgram : this->program;
  if (!bgfx::isValid(selected)) {
    selected = bgfxTracePreparedProgram(
        createLayerProgram(instanced ? COIN_BGFX_SHADER_DATA(coin_bgfx_vs_instanced_color)
                                     : COIN_BGFX_SHADER_DATA(coin_bgfx_vs),
                           instanced ? COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_instanced_color)
                                     : COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
                           instanced   ? COIN_BGFX_SHADER_DATA(coin_bgfx_fs_instanced_color)
                           : solid     ? COIN_BGFX_SHADER_DATA(coin_bgfx_fs_solid_color)
                           : texture2d ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_texture2d)
                                       : COIN_BGFX_SURFACE_DATA(coin_bgfx_fs),
                           instanced   ? COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_instanced_color)
                           : solid     ? COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_solid_color)
                           : texture2d ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_texture2d)
                                       : COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs)),
        "general_surface", this->sampleCount);
    if (!bgfx::isValid(selected)) {
      this->lastError = instanced ? "BGFX failed to create its instanced shader program" :
        solid ? "BGFX failed to create its solid-color shader program" :
        texture2d ? "BGFX failed to create its base 2D texture shader program" :
                    "BGFX failed to create its general surface shader program";
      this->status = CoinRenderBackendStatus::BACKEND_ERROR;
      return false;
    }
  }
  if (!this->prepareSurfaceDepthPrograms(
          draws,
          hasShadows &&
              !std::any_of(draws.begin(), draws.end(),
                           [](const CoinBgfxDraw &draw) {
                             return draw.bumpExtended ||
                                    (draw.hasTexture && (draw.volumeDepth || draw.cubeFaceSize)) ||
                                    std::any_of(std::begin(draw.extraTextures),
                                                std::end(draw.extraTextures),
                                                [](const CoinBgfxDraw::TextureLayer &layer) {
                                                  return layer.enabled;
                                                });
                           }),
          instanced))
    return false;
  if (hasShadows && strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT &&
      !this->prepareSurfaceDepthPrograms(draws, false, false)) return false;
  this->activeProgram = selected;
  this->activeTextureUnitCount = instanced || solid ? 0 :
    texture2d ? 1 : COIN_RENDER_MAX_TEXTURE_UNITS;
  if (CoinRenderDiagnosticShell::phaseTracingEnabled())
    std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_base_program solid=%d instancing=%d texture2d=%d\n",
      solid ? 1 : 0, instanced ? 1 : 0, texture2d ? 1 : 0);
  return true;
}

bool CoinBgfxBackend::preparePortablePrograms(const CoinRenderFramePlan &frame,
  const std::vector<CoinBgfxDraw> &draws, bool hasShadows, CoinBgfxTransparencyStrategy strategy,
  bool windowTarget, bgfx::TextureFormat::Enum colorFormat, bool pinPreflight)
{
  if (std::none_of(draws.begin(), draws.end(), [](const CoinBgfxDraw &draw) { return draw.portableShaderSlot != 0; }))
    return true;
  auto &runtime = sharedRuntime();
  auto &cache = runtime.portable;
  const auto renderer = bgfx::getRendererType();
  if (renderer != bgfx::RendererType::Vulkan && renderer != bgfx::RendererType::OpenGL) {
    this->lastError = "Portable BGFX modules require an installed compiler profile for this renderer";
    return false;
  }
  for (auto &uniform : cache.uniforms)
    if (!bgfx::isValid(uniform)) {
      const unsigned bank = &uniform == &cache.uniforms[0] ? 0 : 1;
      uniform = bgfx::createUniform(bank ? "u_portable1" : "u_portable0", bgfx::UniformType::Vec4, 128);
      if (!bgfx::isValid(uniform)) { this->lastError = "Portable shader uniform bank allocation failed"; return false; }
      cache.uniformCount.fetch_add(1, std::memory_order_relaxed);
    }
  if (!bgfx::isValid(cache.probeControl)) {
    cache.probeControl = bgfx::createUniform("u_portableProbe", bgfx::UniformType::Vec4);
    if (!bgfx::isValid(cache.probeControl)) {
      this->lastError = "Portable shader capability control allocation failed";
      return false;
    }
    cache.uniformCount.fetch_add(1, std::memory_order_relaxed);
  }
  ++cache.use;
  this->portableProgramRoutes.clear();
  const std::string profile = renderer == bgfx::RendererType::Vulkan ? "spirv" : "330";
  // The module replaces fixed texturing and its only sampling operation is
  // SAMPLE_2D, whose resource domain is checked by Core before preparation.
  // Retain the shared P07 implementation, omitting unused spatial lowering.
  const std::string defines = frame.textureSamplingPolicy == COIN_RENDER_SAMPLING_PORTABLE ?
    "COIN_TEXTURE2D_SURFACE=1;COIN_PORTABLE_SAMPLING_SHADER=1" : "COIN_TEXTURE2D_SURFACE=1";
  const auto prepare = [&](const CoinRenderShaderInstance &instance, PortableFamily family,
                            const uint8_t *vertexData, uint32_t vertexBytes, const char *fragment) {
    const std::string identity = coin_render_shader_key(instance.module, instance.ir);
    const std::string fragmentName = fragment;
    const bool sampleShader = fragmentName.find("sample_depth") != std::string::npos ||
      fragmentName.find("peel_msaa") != std::string::npos || fragmentName.find("peel_next_msaa") != std::string::npos;
    const std::string compilerProfile = renderer == bgfx::RendererType::OpenGL && sampleShader ? "430" : profile;
    const auto targetFormat = windowTarget ? runtime.backbufferColorFormat : colorFormat;
    const std::string key = identity + "/" + compilerProfile + "/" + defines + "/" + fragment + "/" +
      std::to_string(runtime.generation) + "/" + std::to_string(unsigned(targetFormat)) + "/" + std::to_string(this->sampleCount);
    auto cached = cache.programs.find(key);
    if (cached == cache.programs.end()) {
      std::vector<uint8_t> fragmentBytes;
      if (!coin_bgfx_compile_portable_fragment(instance.ir, fragment, compilerProfile, defines, fragmentBytes, this->lastError))
        return false;
      uint64_t id = UINT64_C(14695981039346656037);
      for (unsigned char byte : key) id = (id ^ byte) * UINT64_C(1099511628211);
      if (CoinRenderDiagnosticShell::phaseTracingEnabled()) {
        std::fprintf(stderr, "COIN_RENDER_PHASE portable_shader_compile module=%016llx profile=bgfx_%s_%s_%u\n",
          static_cast<unsigned long long>(id), compilerProfile.c_str(), fragment, this->sampleCount);
        std::fflush(stderr);
      }
      const auto program = createLayerProgram(vertexData, vertexBytes, fragmentBytes.data(), static_cast<uint32_t>(fragmentBytes.size()));
      if (!bgfx::isValid(program)) { this->lastError = "Portable BGFX shader program allocation failed"; return false; }
      if (hasShadows) {
        const unsigned composition = family == PortableFamily::SHADOW_PEEL ? 1 :
          family == PortableFamily::SHADOW_OIT ? 2 : 0;
        bool verified = false;
        const bool valid = this->verifyShadowMultitextureProgram(program, verified, composition, false,
          0, bgfx::TextureFormat::RGBA8, bgfx::TextureFormat::Count, true);
        if (!valid) { bgfx::destroy(program); return false; }
      }
      BgfxPortableProgram entry;
      entry.program = program;
      entry.bytes = fragmentBytes.size() + key.size() + sizeof(BgfxPortableProgram);
      entry.used = cache.use;
      cache.bytes += entry.bytes;
      cached = cache.programs.emplace(key, entry).first;
      cache.programCount.store(cache.programs.size(), std::memory_order_relaxed);
    }
    cached->second.used = cache.use;
    // A parent graph's prepared variants must survive its sequential STAGED
    // children and peers until the root has finished encoding its submission.
    if (pinPreflight && std::find(this->portablePreflightPins.begin(),
          this->portablePreflightPins.end(), key) == this->portablePreflightPins.end()) {
      this->portablePreflightPins.push_back(key);
      ++cached->second.pins;
    }
    this->portableProgramRoutes[std::make_pair(identity, family)] = key;
    return true;
  };
  for (const auto &draw : draws) {
    if (!draw.portableShaderSlot) continue;
    if (draw.portableShaderSlot > frame.portableShaders.size() || draw.bumpSlot) {
      this->lastError = "Portable shader module occurrence has incompatible bump attributes";
      return false;
    }
    const auto &instance = frame.portableShaders[draw.portableShaderSlot - 1];
    const auto surface = [&](PortableFamily family, const char *fragment) {
      return prepare(instance, family, COIN_BGFX_SHADER_DATA(coin_bgfx_vs), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs), fragment);
    };
    const auto shadow = [&](PortableFamily family, const char *fragment) {
      return prepare(instance, family, COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_multitexture),
        COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_multitexture), fragment);
    };
    const bool customDepth = this->sampleCount > 1 &&
      (this->shadowMultitextureSampleDepthActive || bgfxExplicitSurfaceSamples() || bgfxCustomSampleDepth(draw));
    const bool deferred = draw.blend && draw.deferred && !draw.additive;
    const bool peel = strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
      (deferred || this->sampleCount > 1);
    const bool oit = strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT && deferred;
    // MSAA peeling uses its depth selector for immediate writers too; other
    // immediate/additive writers retain the normal surface depth family.
    const bool base = !oit && !(peel && (deferred || this->sampleCount > 1));
    if (hasShadows) {
      if (base) {
        const char *fragment = this->sampleCount == 1 ? "fs_shadow_multitexture.sc" :
          bgfxExplicitSurfaceSamples() ? "fs_shadow_multitexture_sample_depth.sc" :
          "fs_shadow_multitexture_native_depth.sc";
        // Vulkan custom-depth draws use the separate sampled family.
        if ((!customDepth || bgfxExplicitSurfaceSamples()) &&
            !shadow(PortableFamily::SHADOW_BASE, fragment)) return false;
        if (customDepth && !bgfxExplicitSurfaceSamples() &&
            !shadow(PortableFamily::SHADOW_SAMPLE, "fs_shadow_multitexture_sample_depth.sc")) return false;
      }
      if (peel && !shadow(PortableFamily::SHADOW_PEEL, this->sampleCount > 1 ?
            "fs_shadow_multitexture_peel_msaa.sc" : "fs_shadow_multitexture_peel.sc")) return false;
      if (oit && !shadow(PortableFamily::SHADOW_OIT, this->sampleCount > 1 ?
            "fs_shadow_multitexture_oit_sample_depth.sc" : "fs_shadow_multitexture_oit.sc")) return false;
    } else {
      if (base) {
        if (this->sampleCount == 1) {
          if (!surface(PortableFamily::BASE, "fs_base_color.sc")) return false;
        } else if (customDepth) {
          if (!surface(PortableFamily::SAMPLE_DEPTH, "fs_surface_sample_depth.sc")) return false;
        } else if (!surface(PortableFamily::NATIVE_DEPTH, "fs_surface_native_depth.sc")) return false;
      }
      if (peel && !surface(PortableFamily::PEEL, this->sampleCount > 1 ?
            "fs_peel_next_msaa.sc" : "fs_peel_next.sc")) return false;
      if (oit && !surface(PortableFamily::OIT, "fs_weighted_oit.sc")) return false;
    }
  }
  // Soft cache budget: entries needed by this frame remain live. Destroying an
  // older BGFX handle queues retirement behind its already-submitted uses.
  const size_t budget = 32u * 1024u * 1024u;
  while (cache.bytes > budget) {
    auto oldest = cache.programs.end();
    for (auto it = cache.programs.begin(); it != cache.programs.end(); ++it)
      if (!it->second.pins && it->second.used != cache.use &&
          (oldest == cache.programs.end() || it->second.used < oldest->second.used)) oldest = it;
    if (oldest == cache.programs.end()) break;
    bgfx::destroy(oldest->second.program);
    cache.bytes -= oldest->second.bytes;
    cache.programs.erase(oldest);
    cache.programCount.store(cache.programs.size(), std::memory_order_relaxed);
  }
  return true;
}

void CoinBgfxBackend::releasePortablePreflightPins()
{
  auto &programs = sharedRuntime().portable.programs;
  for (const auto &key : this->portablePreflightPins) {
    const auto entry = programs.find(key);
    if (entry != programs.end() && entry->second.pins) --entry->second.pins;
  }
  this->portablePreflightPins.clear();
}

void CoinBgfxBackend::submitPortableDraw(bgfx::ViewId view, bgfx::ProgramHandle base,
  const CoinBgfxDraw &draw, const CoinRenderFramePlan &frame, PortableFamily family,
  uint32_t depth, uint8_t discard)
{
  if (!draw.portableShaderSlot) { bgfx::submit(view, base, depth, discard); return; }
  auto &cache = sharedRuntime().portable;
  const auto &instance = frame.portableShaders[draw.portableShaderSlot - 1];
  const auto route = this->portableProgramRoutes.find(std::make_pair(
    coin_render_shader_key(instance.module, instance.ir), family));
  if (route == this->portableProgramRoutes.end()) {
    this->lastError = "Portable shader variant was not prepared before submission";
    this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    return;
  }
  const auto program = cache.programs.find(route->second);
  if (program == cache.programs.end()) {
    this->lastError = "Portable shader prepared variant was retired prematurely";
    this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    return;
  }
  float parameters[1024] = {};
  std::copy(instance.parameters.begin(), instance.parameters.end(), parameters);
  const float authored[4] = {};
  bgfx::setUniform(cache.probeControl, authored);
  bgfx::setUniform(cache.uniforms[0], parameters, 128);
  bgfx::setUniform(cache.uniforms[1], parameters + 512, 128);
  if (CoinRenderDiagnosticShell::phaseTracingEnabled()) {
    const char *name = "unknown";
    switch (family) {
    case PortableFamily::BASE: name = "base"; break;
    case PortableFamily::NATIVE_DEPTH: name = "native_depth"; break;
    case PortableFamily::SAMPLE_DEPTH: name = "sample_depth"; break;
    case PortableFamily::SHADOW_BASE: name = "shadow_base"; break;
    case PortableFamily::SHADOW_SAMPLE: name = "shadow_sample"; break;
    case PortableFamily::PEEL: name = "peel"; break;
    case PortableFamily::SHADOW_PEEL: name = "shadow_peel"; break;
    case PortableFamily::OIT: name = "oit"; break;
    case PortableFamily::SHADOW_OIT: name = "shadow_oit"; break;
    }
    std::fprintf(stderr,
      "COIN_RENDER_PHASE bgfx_portable_program_submit family=%s program=%u samples=%u\n",
      name, unsigned(program->second.program.idx), this->sampleCount);
    std::fflush(stderr);
  }
  bgfx::submit(view, program->second.program, depth, discard);
}
