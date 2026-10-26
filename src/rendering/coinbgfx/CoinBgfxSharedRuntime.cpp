// CoinRender BGFX Runtime responsibilities.
#include "rendering/coinbgfx/CoinBgfxBackendInternal.h"
#include "coin_bgfx_fs_weighted_composite_msaa_glsl.h"
#include "coin_bgfx_fs_weighted_composite_msaa_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_weighted_composite_msaa_dx11.h"
#endif
#include "coin_bgfx_cs_raster_image_glsl.h"
#include "coin_bgfx_cs_raster_image_spirv.h"
#include "coin_bgfx_vs_glsl.h"
#include "coin_bgfx_vs_spirv.h"
#include "coin_bgfx_fs_peel_msaa_composite_glsl.h"
#include "coin_bgfx_fs_peel_msaa_composite_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_peel_msaa_composite_dx11.h"
#include "coin_bgfx_cs_raster_image_dx11.h"
#include "coin_bgfx_vs_dx11.h"
#endif

using namespace coin_bgfx_internal;

namespace coin_bgfx_internal {

void bgfxTraceLifecycle(const void *backend, const char *stage, size_t programs,
                        unsigned uniforms, int64_t references)
{
  if (!CoinRenderDiagnosticShell::phaseTracingEnabled()) return;
  const auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  std::fprintf(stderr,
      "COIN_RENDER_PHASE bgfx_lifecycle backend=%p stage=%s time_ns=%lld portable_programs=%zu portable_uniforms=%u runtime_refs=%lld\n",
      backend, stage, static_cast<long long>(timestamp), programs, uniforms,
      static_cast<long long>(references));
  std::fflush(stderr);
}

bgfx::TextureFormat::Enum coinBgfxTextureFormat(CoinRenderTextureFormat f) {
  if(f==CoinRenderTextureFormat::RGBA16_FLOAT)return bgfx::TextureFormat::RGBA16F;
  if(CoinRenderTextureFormatCore::compressed(f))return bgfx::TextureFormat::BC3;
  return bgfx::TextureFormat::RGBA8;
}

uint64_t coinBgfxTextureFlags(CoinRenderTextureFormat f) {
  return CoinRenderTextureFormatCore::srgb(f)?BGFX_TEXTURE_SRGB:BGFX_TEXTURE_NONE;
}

const bgfx::Memory * uploadVertices(CoinBgfxPlan & plan)
{
  if (plan.usesInstancing)
    return uploadVertexVector(plan.instancedVertices, plan.uploadedVertexCount, false);
  if (plan.usesCompactVertices)
    return uploadVertexVector(plan.packedVertices, plan.uploadedVertexCount, true);
  return uploadVertexVector(plan.vertices, plan.uploadedVertexCount, false);
}

bool bgfxShadowBatchSupported(const CoinRenderFramePlan & frame,
                              const CoinRenderShadowPlan & plan,
                              std::string & diagnostic)
{
  if (plan.passes.size() > 8) {
    diagnostic = "BGFX shadow receiver supports at most eight maps";
    return false;
  }
  for (const auto & draw : frame.draws) {
    if (draw.renderStateSlot >= frame.renderStates.size()) {
      diagnostic = "BGFX shadow draw references an invalid render-state slot";
      return false;
    }
    const auto & state = frame.renderStates[draw.renderStateSlot];
    if (!coin_render_alpha_test_valid(state.alphaTestFunction, state.alphaTestReference)) {
      diagnostic = "BGFX shadow draw has invalid alpha comparison state";
      return false;
    }
  }
  return true;
}

SharedBgfxRuntime & sharedRuntime()
{
  // Process lifetime: even a target destroyed on the wrong thread cannot leave
  // BGFX holding a dangling callback. Such targets diagnose the contract error.
  static SharedBgfxRuntime * runtime = new SharedBgfxRuntime;
  return *runtime;
}

#ifdef COIN_WGPU_TEST_HOOKS
bool consumeTestFault(const char * name)
{
  const char * value = std::getenv(name);
  if (value == nullptr || std::strcmp(value, "1") != 0) return false;
  // Consuming the flag makes recovery
  // deterministic: the replacement backend must not inherit the same fault.
#ifdef _WIN32
  _putenv_s(name, "");
#else
  unsetenv(name);
#endif
  return true;
}

SubmitCountdownFault & submitCountdownFault() {
  static SubmitCountdownFault fault;return fault;
}

bool injectOutOfMemoryAfterSubmits() {
  auto & fault=submitCountdownFault();std::lock_guard<std::mutex> guard(fault.mutex);
  const char * name="COIN_BGFX_TEST_OUT_OF_MEMORY_AFTER_SUBMITS";
  const char * value=std::getenv(name);
  if(value) {
    // Consume even malformed/overflowed values. A new setting replaces the
    // previous arm; it cannot accidentally become a persistent fail-next flag.
    bool valid=*value!='\0';uint64_t count=0;
    for(const unsigned char * digit=reinterpret_cast<const unsigned char *>(value);*digit;++digit) {
      if(*digit<'0'||*digit>'9' || count>(std::numeric_limits<uint64_t>::max()-uint64_t(*digit-'0'))/10) {
        valid=false;break;
      }
      count=count*10+uint64_t(*digit-'0');
    }
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
    fault.armed=valid;fault.remaining=valid?count:0;
  }
  if(!fault.armed || fault.remaining)return false;
  fault.armed=false;return true;
}

void logicalSubmitSucceeded() {
  auto & fault=submitCountdownFault();std::lock_guard<std::mutex> guard(fault.mutex);
  if(fault.armed && fault.remaining)--fault.remaining;
}

#endif

uint64_t drawState(const CoinBgfxDraw & draw)
{
  const uint8_t mask = draw.colorWrite ? draw.colorWriteMask : 0;
  uint64_t state = (mask & 1 ? BGFX_STATE_WRITE_R : 0) |
                   (mask & 2 ? BGFX_STATE_WRITE_G : 0) |
                   (mask & 4 ? BGFX_STATE_WRITE_B : 0) |
                   (mask & 8 ? BGFX_STATE_WRITE_A : 0);
  if (draw.depthTest) {
    switch (draw.depthFunction) {
    case CoinRenderDepthFunction::NEVER: state |= BGFX_STATE_DEPTH_TEST_NEVER; break;
    case CoinRenderDepthFunction::ALWAYS: state |= BGFX_STATE_DEPTH_TEST_ALWAYS; break;
    case CoinRenderDepthFunction::LEQUAL: state |= BGFX_STATE_DEPTH_TEST_LEQUAL; break;
    case CoinRenderDepthFunction::EQUAL: state |= BGFX_STATE_DEPTH_TEST_EQUAL; break;
    case CoinRenderDepthFunction::GEQUAL: state |= BGFX_STATE_DEPTH_TEST_GEQUAL; break;
    case CoinRenderDepthFunction::GREATER: state |= BGFX_STATE_DEPTH_TEST_GREATER; break;
    case CoinRenderDepthFunction::NOTEQUAL: state |= BGFX_STATE_DEPTH_TEST_NOTEQUAL; break;
    case CoinRenderDepthFunction::LESS: default: state |= BGFX_STATE_DEPTH_TEST_LESS; break;
    }
  }
  if (draw.depthTest && draw.depthWrite) state |= BGFX_STATE_WRITE_Z;
  if (draw.blend && draw.legacyBlendAlpha && !draw.additive) {
    state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
  } else if (draw.blend && draw.additive) {
    state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
  } else if (draw.blend) {
    state |= BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,
      BGFX_STATE_BLEND_INV_SRC_ALPHA, BGFX_STATE_BLEND_ONE,
      BGFX_STATE_BLEND_INV_SRC_ALPHA);
  }
  // Coin.s frontFace denotes the visible winding; BGFX state denotes the
  // winding to discard. BGFX accounts for each renderer's target origin.
  if (draw.cullMode == CoinRenderCullMode::BACK) {
    state |= draw.frontFace == CoinRenderFrontFace::CCW ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;
  } else if (draw.cullMode == CoinRenderCullMode::FRONT) {
    state |= draw.frontFace == CoinRenderFrontFace::CCW ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
  }
  return state;
}

bool samePipelineState(const CoinBgfxDraw & lhs,
                       const CoinBgfxDraw & rhs)
{
  return drawState(lhs) == drawState(rhs) &&
    lhs.alphaTestFunction == rhs.alphaTestFunction &&
    std::memcmp(&lhs.alphaTestReference, &rhs.alphaTestReference, sizeof(lhs.alphaTestReference)) == 0 &&
    std::memcmp(lhs.viewport, rhs.viewport, sizeof(lhs.viewport)) == 0;
}

bool sameTextureState(const CoinBgfxDraw & lhs,
                      const CoinBgfxDraw & rhs)
{
  return std::memcmp(lhs.textureCombines, rhs.textureCombines, sizeof(lhs.textureCombines)) == 0 &&
    lhs.textureProjection == rhs.textureProjection &&
    std::memcmp(lhs.extraTextures, rhs.extraTextures, sizeof(lhs.extraTextures)) == 0 &&
    lhs.hasTexture == rhs.hasTexture &&
    lhs.textureSlot == rhs.textureSlot && lhs.textureModel == rhs.textureModel &&
    lhs.wrapS == rhs.wrapS && lhs.wrapT == rhs.wrapT && lhs.filter == rhs.filter && lhs.maxAnisotropy == rhs.maxAnisotropy &&
    std::memcmp(lhs.textureBlendColor, rhs.textureBlendColor,
                sizeof(lhs.textureBlendColor)) == 0;
}

bool sameLightingState(const CoinBgfxDraw & lhs,
                       const CoinBgfxDraw & rhs)
{
  return std::memcmp(lhs.fogColorMode, rhs.fogColorMode, sizeof(lhs.fogColorMode)) == 0 &&
    std::memcmp(lhs.fogRange, rhs.fogRange, sizeof(lhs.fogRange)) == 0 &&
    std::memcmp(lhs.ambientLight, rhs.ambientLight,
                     sizeof(lhs.ambientLight)) == 0 &&
    std::memcmp(lhs.lightCount, rhs.lightCount, sizeof(lhs.lightCount)) == 0 &&
    std::memcmp(lhs.lightPositionType, rhs.lightPositionType,
                sizeof(lhs.lightPositionType)) == 0 &&
    std::memcmp(lhs.lightDirectionCutoff, rhs.lightDirectionCutoff,
                sizeof(lhs.lightDirectionCutoff)) == 0 &&
    std::memcmp(lhs.lightColorIntensity, rhs.lightColorIntensity,
                sizeof(lhs.lightColorIntensity)) == 0 &&
    std::memcmp(lhs.lightAmbientColor, rhs.lightAmbientColor,
                sizeof(lhs.lightAmbientColor)) == 0 &&
    std::memcmp(lhs.lightAttenuationDrop, rhs.lightAttenuationDrop,
                sizeof(lhs.lightAttenuationDrop)) == 0;
}

LogicalDrawStats logicalDrawStats(
  const std::vector<CoinBgfxDraw> & original,
  const std::vector<CoinBgfxDraw> & encoded)
{
  LogicalDrawStats stats;
  std::vector<uint32_t> originalOpaque;
  std::vector<uint32_t> encodedOpaque;
  const CoinBgfxDraw * previous = nullptr;
  for (const CoinBgfxDraw & draw : original) {
    if (!draw.blend) originalOpaque.push_back(draw.firstIndex);
  }
  for (const CoinBgfxDraw & draw : encoded) {
    if (draw.blend) ++stats.transparentDraws;
    else {
      ++stats.opaqueDraws;
      encodedOpaque.push_back(draw.firstIndex);
    }
    if (!previous || !samePipelineState(*previous, draw)) ++stats.pipelineChanges;
    if (!previous || previous->materialSignature != draw.materialSignature)
      ++stats.materialChanges;
    if (!previous || !sameTextureState(*previous, draw)) ++stats.textureChanges;
    if (!previous || !sameLightingState(*previous, draw)) ++stats.lightingChanges;
    previous = &draw;
  }
  stats.opaqueOrderChanged = originalOpaque != encodedOpaque;
  return stats;
}

void copyLogicalDrawStats(const LogicalDrawStats & source,
                          bool groupingEnabled,
                          CoinBgfxPhaseSample & destination)
{
  destination.opaqueDraws = source.opaqueDraws;
  destination.transparentDraws = source.transparentDraws;
  destination.logicalPipelineChanges = source.pipelineChanges;
  destination.logicalMaterialChanges = source.materialChanges;
  destination.logicalTextureChanges = source.textureChanges;
  destination.logicalLightingChanges = source.lightingChanges;
  destination.opaqueGroupingEnabled = groupingEnabled;
  destination.opaqueOrderChanged = source.opaqueOrderChanged;
}

uint32_t pooledCapacity(size_t required)
{
  // Bound slack for large uploads instead of reserving the next power of two.
  if (required > 65536) {
    const uint64_t aligned = (uint64_t(required) + 65535u) & ~UINT64_C(65535);
    return aligned <= UINT32_MAX ? static_cast<uint32_t>(aligned) : static_cast<uint32_t>(required);
  }
  uint32_t capacity = 256;
  while (capacity < required && capacity <= UINT32_MAX / 2u)
    capacity *= 2u;
  return capacity < required ? static_cast<uint32_t>(required) : capacity;
}

bool setDrawScissor(const CoinBgfxDraw & draw, int targetWidth, int targetHeight)
{
  int32_t clipped[4];
  if (!CoinBgfxLowering::clipViewport(draw.viewport, targetWidth, targetHeight, clipped))
    return false;
  const int32_t top = targetHeight - clipped[1] - clipped[3];
  bgfx::setScissor(static_cast<uint16_t>(clipped[0]),
                   static_cast<uint16_t>(top),
                   static_cast<uint16_t>(clipped[2]),
                   static_cast<uint16_t>(clipped[3]));
  return true;
}

uint64_t peelDrawState(const CoinBgfxDraw & draw, bool depthOnly)
{
  uint64_t state = drawState(draw);
  state &= ~(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
             BGFX_STATE_WRITE_Z | BGFX_STATE_BLEND_MASK);
  if (!depthOnly || (draw.depthTest && draw.depthWrite)) state |= BGFX_STATE_WRITE_Z;
  if (!depthOnly) state |= BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
  return state;
}

uint32_t peelFixedSampleMask(uint32_t samples, uint32_t sample)
{
  const auto renderer = bgfx::getRendererType();
  // GL's MRT clear is itself a draw. Shader sample coverage expresses the
  // exact same per-draw mask without leaving GL_SAMPLE_MASK enabled at clear.
  return samples == 1 || renderer == bgfx::RendererType::OpenGL || renderer == bgfx::RendererType::OpenGLES
    ? UINT32_MAX : 1u << sample;
}

uint64_t peelSelectionState(const CoinBgfxDraw &draw)
{
  // The attachment selects the next nearest distinct depth. The captured
  // depth function is evaluated against the real opaque depth in the shader.
  uint64_t state = drawState(draw);
  state &= ~(BGFX_STATE_DEPTH_TEST_MASK | BGFX_STATE_WRITE_RGB |
             BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_BLEND_MASK);
  return state | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_WRITE_Z |
         BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
}

bgfx::ProgramHandle createLayerProgram(const uint8_t * vertexData,
                                       uint32_t vertexBytes,
                                       const uint8_t * fragmentData,
                                       uint32_t fragmentBytes)
{
  bgfx::ShaderHandle vs = bgfx::createShader(bgfx::copy(vertexData, vertexBytes));
  bgfx::ShaderHandle fs = bgfx::createShader(bgfx::copy(fragmentData, fragmentBytes));
  if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
    if (bgfx::isValid(vs)) bgfx::destroy(vs);
    if (bgfx::isValid(fs)) bgfx::destroy(fs);
    return BGFX_INVALID_HANDLE;
  }
  const auto program = bgfx::createProgram(vs, fs, true);
  if (bgfx::isValid(program)) ++sharedRuntime().programCommandsQueued;
  return program;
}

bool captureGpuPasses(uint32_t submittedFrame, CoinBgfxPhaseSample & sample)
{
  const bgfx::Stats * stats = bgfx::getStats();
  if (!stats) return false;
  sample.gpuResourceStatsAvailable = true;
  if (stats->numDraw > sample.gpuDrawSubmits)
    sample.gpuDrawSubmits = stats->numDraw;
  sample.gpuMemoryUsedBytes = stats->gpuMemoryUsed >= 0 ? stats->gpuMemoryUsed : -1;
  sample.textureMemoryUsedBytes = stats->textureMemoryUsed >= 0 ?
    stats->textureMemoryUsed : -1;
  sample.renderTargetMemoryUsedBytes = stats->rtMemoryUsed >= 0 ?
    stats->rtMemoryUsed : -1;
  sample.gpuVertexBuffers = stats->numVertexBuffers;
  sample.gpuIndexBuffers = stats->numIndexBuffers;
  sample.gpuTextures = stats->numTextures;
  sample.gpuFrameBuffers = stats->numFrameBuffers;
  sample.gpuPrograms = stats->numPrograms;
  if (stats->gpuTimerFreq <= 0) return false;
  const double gpuMilliseconds = 1000.0 / double(stats->gpuTimerFreq);
  const bool targetFrame = stats->gpuFrameNum == submittedFrame &&
    stats->gpuTimeEnd > stats->gpuTimeBegin;
  if (targetFrame) {
    sample.gpuFrameMs = double(stats->gpuTimeEnd - stats->gpuTimeBegin) *
                        gpuMilliseconds;
  }
  if (stats->cpuTimerFreq > 0) {
    sample.gpuWaitMs = double(stats->waitRender + stats->waitSubmit) *
                       1000.0 / double(stats->cpuTimerFreq);
  }
  const auto add = [](double & total, double value) {
    if (total < 0.0) total = 0.0;
    total += value;
  };
  const bool needPasses = sample.gpuOpaqueMs < 0.0 &&
    sample.gpuTransparentMs < 0.0 && sample.gpuCompositeMs < 0.0 &&
    sample.gpuBlitReadbackMs < 0.0;
  for (uint16_t index = 0; needPasses && index < stats->numViews; ++index) {
    const bgfx::ViewStats & view = stats->viewStats[index];
    if (view.gpuTimeEnd <= view.gpuTimeBegin) continue;
    if (sample.gpuPassFrame == 0) sample.gpuPassFrame = view.gpuFrameNum;
    if (view.gpuFrameNum != sample.gpuPassFrame) continue;
    const double elapsed = double(view.gpuTimeEnd - view.gpuTimeBegin) *
                           gpuMilliseconds;
    if (std::strcmp(view.name, "opaque") == 0) {
      add(sample.gpuOpaqueMs, elapsed);
    } else if (std::strncmp(view.name, "transparent_accumulation", 24) == 0) {
      add(sample.gpuTransparentMs, elapsed);
    } else if (std::strcmp(view.name, "fullscreen_composition") == 0) {
      add(sample.gpuCompositeMs, elapsed);
    } else if (std::strcmp(view.name, "blit_readback") == 0) {
      add(sample.gpuBlitReadbackMs, elapsed);
    }
  }
  return targetFrame;
}

std::map<uint64_t, std::shared_ptr<CoinBgfxBackend::AsyncEntry>> & asyncEntries()
{
  static auto * entries = new std::map<uint64_t, std::shared_ptr<CoinBgfxBackend::AsyncEntry>>;
  return *entries;
}

bool sameTicket(const CoinRenderReadbackTicket & a, const CoinRenderReadbackTicket & b)
{
  return a.token == b.token && a.generation == b.generation &&
    a.submissionSerial == b.submissionSerial && a.width == b.width && a.height == b.height &&
    a.colorFormat == b.colorFormat && a.depthFormat == b.depthFormat &&
    a.colorRowPitch == b.colorRowPitch && a.depthRowPitch == b.depthRowPitch &&
    a.colorBytes == b.colorBytes && a.depthBytes == b.depthBytes;
}

void releaseAsync(uint64_t token, bool failed)
{
  auto & entries = asyncEntries();
  auto entry = entries.at(token); // Keep CPU buffers alive through shutdown.
  auto & runtime = sharedRuntime();
  if (!failed) {
    bgfx::destroy(entry->slot.texture);
    if (bgfx::isValid(entry->slot.depthTexture)) bgfx::destroy(entry->slot.depthTexture);
  }
  if (failed) failedReadbacks.push_back(entry);
  entries.erase(token);
  if (--runtime.references == 0) {
    const auto *callback = static_cast<CoinBgfxCallback *>(runtime.callback.get());
    runtime.portable.clear(!failed && callback && !callback->failed());
    bgfx::shutdown();
    runtime.releaseNativeDisplay();
    failedReadbacks.clear();
    runtime.callback.reset(); runtime.renderer = bgfx::RendererType::Count;
    runtime.apiThread = std::thread::id(); runtime.viewBlocks.clear();
  }
}

bgfx::ProgramHandle createRasterImageProgram()
{
  const auto shader = bgfx::createShader(bgfx::copy(
    COIN_BGFX_SHADER_DATA(coin_bgfx_cs_raster_image), COIN_BGFX_SHADER_SIZE(coin_bgfx_cs_raster_image)));
  if (!bgfx::isValid(shader)) return BGFX_INVALID_HANDLE;
  const auto program = bgfx::createProgram(shader, true);
  if (bgfx::isValid(program)) ++sharedRuntime().programCommandsQueued;
  return program;
}

uint64_t weightedTextureFlags(uint32_t samples)
{
  return samples == 1 ? peelTextureFlags : BGFX_TEXTURE_MSAA_SAMPLE |
    (samples == 2 ? BGFX_TEXTURE_RT_MSAA_X2 : BGFX_TEXTURE_RT_MSAA_X4);
}

bool bgfxWeightedSamples(uint32_t samples, const bgfx::VertexLayout & layout,
                        bgfx::ViewId view, std::string & diagnostic, bool verify)
{
  const auto * caps = bgfx::getCaps();
  const uint64_t flags = weightedTextureFlags(samples);
  if (!caps || !(caps->supported & BGFX_CAPS_BLEND_INDEPENDENT) || caps->limits.maxFBAttachments < 3) {
    diagnostic = "Weighted OIT requires independent blending and three attachments";
    return false;
  }
  for (auto format : {bgfx::TextureFormat::RGBA16F, bgfx::TextureFormat::R16F})
    if (!(caps->formats[format] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
        (samples > 1 && !(caps->formats[format] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA)) ||
        !bgfx::isTextureValid(1, false, 1, format, flags)) {
      diagnostic = "Weighted OIT color formats cannot be blended/sampled at the requested count";
      return false;
    }
  if (!verify || samples == 1 || (sharedRuntime().weightedSamplesVerified & (1u << samples))) return true;
  if (sharedRuntime().weightedSamplesRejected & (1u << samples)) {
    diagnostic = "BGFX did not allocate the exact weighted accumulation/revealage sample count";
    return false;
  }
  if (!(caps->formats[bgfx::TextureFormat::R32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
      !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::R32F,
                            BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST)) {
    diagnostic = "Weighted MSAA exact sample-count probe unavailable";
    return false;
  }
  const auto accum = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA16F, flags);
  const auto reveal = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::R16F, flags);
  const auto result = bgfx::createFrameBuffer(1, 1, bgfx::TextureFormat::R32F, peelTextureFlags);
  const auto staging = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::R32F,
    BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
  const auto a = bgfx::createUniform("s_oitAccumMS", bgfx::UniformType::Sampler);
  const auto r = bgfx::createUniform("s_oitRevealMS", bgfx::UniformType::Sampler);
  const auto info = bgfx::createUniform("u_depthInfo", bgfx::UniformType::Vec4);
  const auto program = createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
    COIN_BGFX_SHADER_DATA(coin_bgfx_fs_weighted_composite_msaa), COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_weighted_composite_msaa));
  CoinBgfxVertex vertices[3] = {};
  vertices[0].position[0] = -1; vertices[0].position[1] = -1;
  vertices[1].position[0] = 3; vertices[1].position[1] = -1;
  vertices[2].position[0] = -1; vertices[2].position[1] = 3;
  const auto vb = bgfx::createVertexBuffer(bgfx::copy(vertices, sizeof(vertices)), layout);
  const auto release = [&]() {
    if (bgfx::isValid(vb)) bgfx::destroy(vb);
    if (bgfx::isValid(program)) bgfx::destroy(program);
    if (bgfx::isValid(a)) bgfx::destroy(a);
    if (bgfx::isValid(r)) bgfx::destroy(r);
    if (bgfx::isValid(info)) bgfx::destroy(info);
    if (bgfx::isValid(accum)) bgfx::destroy(accum);
    if (bgfx::isValid(reveal)) bgfx::destroy(reveal);
    if (bgfx::isValid(result)) bgfx::destroy(result);
    if (bgfx::isValid(staging)) bgfx::destroy(staging);
  };
  if (!bgfx::isValid(accum) || !bgfx::isValid(reveal) || !bgfx::isValid(result) ||
      !bgfx::isValid(staging) || !bgfx::isValid(a) || !bgfx::isValid(r) ||
      !bgfx::isValid(info) || !bgfx::isValid(program) || !bgfx::isValid(vb)) {
    release(); diagnostic = "Weighted MSAA sample-count probe allocation failed"; return false;
  }
  bgfx::frame(BGFX_FRAME_FLUSH);
  sharedRuntime().programCommandsMaterialized = sharedRuntime().programCommandsQueued;
  bgfx::resetView(view); bgfx::resetView(view + 1);
  bgfx::setViewName(view, "weighted_msaa_sample_count");
  bgfx::setViewRect(view, 0, 0, 1, 1);
  bgfx::setViewFrameBuffer(view, result);
  const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  const float params[4] = {1,1,0,float(samples)};
  bgfx::setTransform(identity);
  bgfx::setUniform(info, params);
  bgfx::setTexture(0, a, accum); bgfx::setTexture(1, r, reveal);
  bgfx::setVertexBuffer(0, vb);
  bgfx::setSampleMask();
  bgfx::setState(BGFX_STATE_WRITE_R);
  bgfx::submit(view, program);
  bgfx::TextureRegion dst, src;
  dst.handle = staging; src.handle = bgfx::getTexture(result);
  bgfx::blit(view + 1, dst, src);
  float effective = -1;
  bgfx::TextureRegion read; read.handle = staging;
  const uint32_t ready = bgfx::read(read, &effective);
  uint32_t completed = bgfx::frame();
  while (static_cast<int32_t>(completed - ready) < 0) completed = bgfx::frame();
  bgfx::resetView(view); bgfx::resetView(view + 1);
  release();
  if (effective != float(samples)) {
    sharedRuntime().weightedSamplesRejected |= 1u << samples;
    diagnostic = "BGFX did not allocate the exact weighted accumulation/revealage sample count";
    return false;
  }
  sharedRuntime().weightedSamplesVerified |= 1u << samples;
  if (CoinRenderDiagnosticShell::phaseTracingEnabled())
    std::fprintf(stderr, "COIN_RENDER_PHASE weighted_msaa requested=%u effective=%.0f scene_draws=0\n", samples, effective);
  return true;
}

bool bgfxSampleInterpolationSupported(uint32_t samples, std::string & diagnostic)
{
  const auto * caps = bgfx::getCaps();
  if (samples > 1 && caps && caps->rendererType == bgfx::RendererType::Vulkan) {
#if defined(COIN_BGFX_SAMPLE_INTERPOLATION_EXTENSION_VERSION) && COIN_BGFX_SAMPLE_INTERPOLATION_EXTENSION_VERSION == 1 && BGFX_API_VERSION == UINT32_C(161) && defined(COIN_BGFX_CAPS_SAMPLE_INTERPOLATION)
    if (!(caps->supported & COIN_BGFX_CAPS_SAMPLE_INTERPOLATION)) {
      diagnostic = "BGFX Vulkan sample interpolation is not enabled on this runtime/device";
      return false;
    }
#else
    diagnostic = "BGFX Vulkan SDK lacks the controlled sample interpolation extension";
    return false;
#endif
  }
  return true;
}

bool bgfxExplicitSurfaceSamples()
{
  return bgfx::getRendererType() == bgfx::RendererType::OpenGL;
}

bgfx::ProgramHandle bgfxTracePreparedProgram(bgfx::ProgramHandle program, const char *role,
                                             uint32_t samples)
{
  if (bgfx::isValid(program) && CoinRenderDiagnosticShell::phaseTracingEnabled())
    std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_program_prepare role=%s samples=%u\n", role,
                 samples);
  return program;
}

bool bgfxNeedsFixedSurfacePrograms(const std::vector<CoinBgfxDraw> &draws)
{
  return std::any_of(draws.begin(), draws.end(), [](const CoinBgfxDraw &draw) {
    return draw.indexCount && !draw.portableShaderSlot && !draw.bumpSlot;
  });
}

BgfxFrameProgramNeeds bgfxFrameProgramNeeds(const CoinRenderFramePlan &frame)
{
  BgfxFrameProgramNeeds needs;
  for (const auto &draw : frame.draws) {
    if (draw.shadowLightSlot || !draw.geometry.indexCount)
      continue;
    if (draw.geometry.firstIndex >= frame.indices.size()) {
      needs.ordinary = true; // Invalid geometry remains the admission validator's responsibility.
      continue;
    }
    if (draw.renderStateSlot < frame.renderStates.size() &&
        frame.renderStates[draw.renderStateSlot].portableShaderSlot)
      continue;
    const uint32_t vertex = frame.indices[draw.geometry.firstIndex];
    const auto coord =
        std::lower_bound(frame.bumpCoords.begin(), frame.bumpCoords.end(), vertex,
                         [](const CoinRenderBumpCoordOverride &candidate, uint32_t index) {
                           return candidate.vertexIndex < index;
                         });
    if (coord != frame.bumpCoords.end() && coord->vertexIndex == vertex && coord->imageSlot)
      needs.bump = true;
    else
      needs.ordinary = true;
  }
  return needs;
}

bool bgfxShadowMultitexture(const CoinRenderFramePlan & frame)
{
  // Raster inherited source states feed a compute prepass, not the surface
  // shader. Only states actually referenced by geometry select its layout.
  return !frame.shadowGroups.empty() && std::any_of(frame.draws.begin(), frame.draws.end(), [&frame](const CoinRenderDrawPacket & draw) {
    if (draw.renderStateSlot >= frame.renderStates.size()) return false;
    const auto & state = frame.renderStates[draw.renderStateSlot];
    return state.portableShaderSlot || !frame.bumpImages.empty() ||
           (state.hasTexture && state.textureImageSlot < frame.textures.size() &&
            (frame.textures[state.textureImageSlot].volumeDepth ||
             frame.textures[state.textureImageSlot].cubeFaceSize)) ||
           std::any_of(std::begin(state.extraTextures), std::end(state.extraTextures),
                       [](const CoinRenderTextureUnitSnapshot &unit) { return unit.enabled; });
  });
}

bool bgfxCustomSampleDepth(const CoinBgfxDraw & draw)
{
  return draw.depthRange[0] != 0.0f || draw.depthRange[1] != 1.0f ||
    draw.polygonOffsetFactor != 0.0f || draw.polygonOffsetUnits != 0.0f ||
    draw.polygonOffsetSlopeBias != 0.0f;
}

bool bgfxPeelingSamples(uint32_t samples, const bgfx::VertexLayout & layout,
                        bgfx::ViewId view, std::string & diagnostic, bool verify)
{
  const auto * caps = bgfx::getCaps();
  if (!bgfxSampleInterpolationSupported(samples, diagnostic)) return false;
  const uint64_t flags = weightedTextureFlags(samples);
  if (!caps || caps->limits.maxFBAttachments < 3) {
    diagnostic = "Peeling requires three attachments";
    return false;
  }
  for (auto format : {bgfx::TextureFormat::RGBA16F, bgfx::TextureFormat::R16F, bgfx::TextureFormat::D32F})
    if (!(caps->formats[format] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
        (samples > 1 && !(caps->formats[format] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA)) ||
        !bgfx::isTextureValid(1, false, 1, format, flags)) {
      diagnostic = "Peeling color/depth/mask formats cannot render/sample at the requested count";
      return false;
    }
  if (!verify || samples == 1 || (sharedRuntime().peelingSamplesVerified & (1u << samples))) return true;
  if (sharedRuntime().peelingSamplesRejected & (1u << samples)) {
    diagnostic = "BGFX did not allocate the exact peeling color/depth/mask sample count";
    return false;
  }
  if (!(caps->formats[bgfx::TextureFormat::R32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
      !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::R32F,
                            BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST)) {
    diagnostic = "Peeling MSAA exact sample-count probe unavailable";
    return false;
  }
  const auto accum = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA16F, flags);
  const auto reveal = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::R16F, flags);
  const auto depth = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::D32F, flags);
  const auto result = bgfx::createFrameBuffer(1, 1, bgfx::TextureFormat::R32F, peelTextureFlags);
  const auto staging = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::R32F,
    BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
  const auto a = bgfx::createUniform("s_peelColorMS", bgfx::UniformType::Sampler);
  const auto r = bgfx::createUniform("s_peelMaskMS", bgfx::UniformType::Sampler);
  const auto d = bgfx::createUniform("s_peelDepthMS", bgfx::UniformType::Sampler);
  const auto info = bgfx::createUniform("u_depthInfo", bgfx::UniformType::Vec4);
  const auto program = createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
    COIN_BGFX_SHADER_DATA(coin_bgfx_fs_peel_msaa_composite), COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_peel_msaa_composite));
  CoinBgfxVertex vertices[3] = {};
  vertices[0].position[0] = -1; vertices[0].position[1] = -1;
  vertices[1].position[0] = 3; vertices[1].position[1] = -1;
  vertices[2].position[0] = -1; vertices[2].position[1] = 3;
  const auto vb = bgfx::createVertexBuffer(bgfx::copy(vertices, sizeof(vertices)), layout);
  const auto release = [&]() {
    if (bgfx::isValid(vb)) bgfx::destroy(vb);
    if (bgfx::isValid(program)) bgfx::destroy(program);
    if (bgfx::isValid(a)) bgfx::destroy(a);
    if (bgfx::isValid(r)) bgfx::destroy(r);
    if (bgfx::isValid(d)) bgfx::destroy(d);
    if (bgfx::isValid(depth)) bgfx::destroy(depth);
    if (bgfx::isValid(info)) bgfx::destroy(info);
    if (bgfx::isValid(accum)) bgfx::destroy(accum);
    if (bgfx::isValid(reveal)) bgfx::destroy(reveal);
    if (bgfx::isValid(result)) bgfx::destroy(result);
    if (bgfx::isValid(staging)) bgfx::destroy(staging);
  };
  if (!bgfx::isValid(depth) || !bgfx::isValid(d) || !bgfx::isValid(accum) || !bgfx::isValid(reveal) || !bgfx::isValid(result) ||
      !bgfx::isValid(staging) || !bgfx::isValid(a) || !bgfx::isValid(r) ||
      !bgfx::isValid(info) || !bgfx::isValid(program) || !bgfx::isValid(vb)) {
    release(); diagnostic = "Peeling MSAA sample-count probe allocation failed"; return false;
  }
  bgfx::frame(BGFX_FRAME_FLUSH);
  sharedRuntime().programCommandsMaterialized = sharedRuntime().programCommandsQueued;
  bgfx::resetView(view); bgfx::resetView(view + 1);
  bgfx::setViewName(view, "peeling_msaa_sample_count");
  bgfx::setViewRect(view, 0, 0, 1, 1);
  bgfx::setViewFrameBuffer(view, result);
  const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  const float params[4] = {1,1,0,float(samples)};
  bgfx::setTransform(identity);
  bgfx::setUniform(info, params);
  bgfx::setTexture(0, a, accum); bgfx::setTexture(1, r, reveal); bgfx::setTexture(2, d, depth);
  bgfx::setVertexBuffer(0, vb);
  bgfx::setSampleMask();
  bgfx::setState(BGFX_STATE_WRITE_R);
  bgfx::submit(view, program);
  bgfx::TextureRegion dst, src;
  dst.handle = staging; src.handle = bgfx::getTexture(result);
  bgfx::blit(view + 1, dst, src);
  float effective = -1;
  bgfx::TextureRegion read; read.handle = staging;
  const uint32_t ready = bgfx::read(read, &effective);
  uint32_t completed = bgfx::frame();
  while (static_cast<int32_t>(completed - ready) < 0) completed = bgfx::frame();
  bgfx::resetView(view); bgfx::resetView(view + 1);
  release();
  if (effective != float(samples)) {
    sharedRuntime().peelingSamplesRejected |= 1u << samples;
    diagnostic = "BGFX did not allocate the exact peeling color/depth/mask sample count";
    return false;
  }
  sharedRuntime().peelingSamplesVerified |= 1u << samples;
  if (CoinRenderDiagnosticShell::phaseTracingEnabled())
    std::fprintf(stderr, "COIN_RENDER_PHASE peeling_msaa requested=%u effective=%.0f scene_draws=0\n", samples, effective);
  return true;
}

CoinRenderSubmitResult bgfxRasterImageProfile(const CoinRenderFramePlan & frame)
{
  const auto * caps = sharedRuntime().references ? bgfx::getCaps() : nullptr;
  uint64_t bytes = 0;
  size_t outputs = 0;
  for (const auto & state : frame.renderStates) {
    if (state.rasterTextureSourceSlot == UINT32_MAX) continue;
    if (++outputs > 4096 || state.textureImageSlot >= frame.textures.size())
      return {CoinRenderBackendStatus::UNSUPPORTED, "Inherited images exceed output count or reference invalid payload"};
    const auto & image = frame.textures[state.textureImageSlot];
    const uint64_t pixels = uint64_t(image.width) * image.height;
    if (!image.width || !image.height || pixels > (128ull * 1024 * 1024 - bytes) / 16)
      return {CoinRenderBackendStatus::UNSUPPORTED, "Inherited image outputs exceed 128 MiB or have zero extent"};
    bytes += pixels * 16;
    if (caps && (image.width > caps->limits.maxTextureSize || image.height > caps->limits.maxTextureSize))
      return {CoinRenderBackendStatus::UNSUPPORTED, "Inherited image output exceeds adapter extent"};
  }
  if (!outputs || !caps) return {};
  const uint64_t flags = BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP |
    BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
  if (!(caps->supported & BGFX_CAPS_COMPUTE) || caps->limits.maxTextureSamplers < 10 ||
      !(caps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE) ||
      !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA32F, flags))
    return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX inherited images require RGBA32F compute storage and nine prepass textures"};
  return {};
}

CoinRenderSubmitResult bgfxTextureProfile(const CoinRenderFramePlan & frame) {
  std::string bumpDiagnostic;
  if (!coin_render_shader_execution_profile(frame,bumpDiagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED,bumpDiagnostic};
  if(!CoinBgfxLowering::validateBump(frame,bumpDiagnostic))return {CoinRenderBackendStatus::UNSUPPORTED,bumpDiagnostic};
  const auto rasterAdmission = bgfxRasterImageProfile(frame);
  if (rasterAdmission.status != CoinRenderBackendStatus::SUCCESS) return rasterAdmission;
  std::string cubeDiagnostic;
  if (!CoinRenderCubeTextureCore::validate(frame, cubeDiagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, cubeDiagnostic};
  const auto * caps = sharedRuntime().references ? bgfx::getCaps() : nullptr;
  for (const auto & sampler : frame.samplers)
    if (sampler.maxAnisotropy != 1 && sampler.maxAnisotropy != 16)
      return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX exposes native maximum anisotropy only (option 16); factors 2/4/8 are unavailable"};
  if (!caps) return {}; // Selected-adapter admission follows prepare.
  const bool extendedBump = coin_render_uses_extended_bump(frame);
  if (extendedBump && (caps->limits.maxTextureSamplers < 11 ||
                       caps->limits.maxVertexAttributes < 15 || caps->limits.maxTextureLayers < 2))
    return {CoinRenderBackendStatus::UNSUPPORTED,
            "Bump receiver requires fifteen vertex attributes, eleven texture stages and two array "
            "layers"};
  for(const auto & image:frame.bumpImages)if(image.width>caps->limits.maxTextureSize || image.height>caps->limits.maxTextureSize)
    return {CoinRenderBackendStatus::UNSUPPORTED,"BGFX bump image exceeds selected GPU texture dimension limit"};
  if(caps->rendererType==bgfx::RendererType::OpenGL && !frame.textures.empty()) {
    // This GL connector changes the object's mip range. A mixed binding gets
    // an independent base-level object after lowering.
    std::vector<uint8_t> usage(frame.textures.size(),0);
    for(const auto & state:frame.renderStates)for(size_t unit=0;unit<COIN_RENDER_MAX_TEXTURE_UNITS;++unit) {
      const auto layer=coin_render_texture_unit(state,unit);
      if(!layer.enabled || layer.imageSlot>=usage.size() || layer.samplerSlot>=frame.samplers.size())continue;
      usage[layer.imageSlot]|=CoinRenderTextureSamplingCore::mipFilter(frame.samplers[layer.samplerSlot].filter)?2:1;
      if(usage[layer.imageSlot]==3 &&
          (frame.textures[layer.imageSlot].gpuToken != 0 ||
           frame.textures[layer.imageSlot].producerId != 0) &&
          !bgfx::isTextureValid(1, false, 1,
            coinBgfxTextureFormat(frame.textures[layer.imageSlot].format),
            coinBgfxTextureFlags(frame.textures[layer.imageSlot].format) | BGFX_TEXTURE_BLIT_DST))
        return {CoinRenderBackendStatus::UNSUPPORTED,"BGFX OpenGL cannot create an independent base view for this direct texture format"};
    }
  }
  for (const auto & image : frame.textures) {
    const auto format = coinBgfxTextureFormat(image.format);
    if (!CoinRenderTextureFormatCore::valid(image.format) ||
        !(caps->formats[format] & (CoinRenderTextureFormatCore::srgb(image.format) ? BGFX_CAPS_FORMAT_TEXTURE_2D_SRGB : BGFX_CAPS_FORMAT_TEXTURE_2D)) ||
        image.width > caps->limits.maxTextureSize || image.height > caps->limits.maxTextureSize ||
        !bgfx::isTextureValid(1, false, 1, format, coinBgfxTextureFlags(image.format)))
      return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX texture format or extent unavailable"};
  }
  return {};
}

bool bgfxCompositionViewBudget(const CoinRenderFramePlan & frame,
  const std::vector<CoinBgfxDraw> & draws, CoinBgfxTransparencyStrategy strategy,
  size_t shadowViews, bool direct, int width, int height,
  uint32_t outputViews, bool multisampled, std::string & diagnostic, uint32_t * requiredViews)
{
  (void)direct;
  const bool images = std::any_of(frame.renderStates.begin(), frame.renderStates.end(),
    [](const CoinRenderRenderStateSnapshot & state) { return state.rasterTextureSourceSlot != UINT32_MAX; });
  const uint64_t fixedViews = (images ? 1 : 0) + shadowViews + (shadowViews && bgfxShadowMultitexture(frame) ? 1 : 0);
  uint64_t views = fixedViews;
  views += 1 + bgfxDeferredViewCount(draws, strategy == CoinBgfxTransparencyStrategy::OBJECT,
    strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS ? frame.transparency.layers + 1 : 2,
    [](const CoinBgfxDraw & draw) { return draw.isSceneLayer() && draw.blend && draw.deferred; },
    [](const CoinBgfxDraw & draw) { return draw.additive; });
  std::map<uint32_t, const CoinBgfxDraw *> layers;
  for (const auto & draw : draws) {
    if (draw.renderLayer) {
      auto inserted = layers.emplace(draw.renderLayer, nullptr);
      if (draw.clearDepthBefore && !inserted.first->second) inserted.first->second = &draw;
    }
  }

  for (const auto & layer : layers) {
    ++views; // Overlay draw view, or initial frame clear for a remapped authored scene.
    int32_t clipped[4];
    if (layer.second && CoinBgfxLowering::clipViewport(layer.second->viewport, width, height, clipped))
      ++views;
  }
  views += (multisampled ? 1 : 0) + (frame.stereoMode ? 1 : 0);
  if (frame.numPasses > 1)
    views = fixedViews + (views - fixedViews + 1) * uint64_t(frame.numPasses) + 1;
  views += outputViews;
  const uint32_t maximumViews = bgfx::getCaps() ? bgfx::getCaps()->limits.maxViews : targetViewCount;
  if (requiredViews) *requiredViews = uint32_t(views);
  if (views > maximumViews) {
    diagnostic = "BGFX composition, clears, resolve and output exceed available view IDs";
    return false;
  }
  return true;
}

bool bgfxMsaaFrameViewBudget(const CoinRenderFramePlan & frame,
  int width, int height, uint32_t outputViews, uint32_t samples, bool direct,
  std::string & diagnostic, uint32_t * requiredViews)
{
  (void)direct;
  const auto * targetCaps = bgfx::getCaps();
  if (width <= 0 || height <= 0 || (targetCaps &&
      (uint32_t(width) > targetCaps->limits.maxTextureSize || uint32_t(height) > targetCaps->limits.maxTextureSize))) {
    diagnostic = "BGFX graph target extent exceeds attachment capabilities";
    return false;
  }
  if (samples > 1 && uint64_t(width) * uint64_t(height) * (8ull * samples) > 128ull * 1024 * 1024) {
    diagnostic = "BGFX graph MSAA attachments exceed 128 MiB";
    return false;
  }
  std::vector<CoinRenderCompositionItem> order;
  if (!coin_render_composition_order(frame, order, diagnostic)) return false;
  const bool weighted = std::any_of(order.begin(), order.end(), [](const CoinRenderCompositionItem & item) {
    return item.blend && item.deferred && !item.additive &&
      item.transparencyStrategy == CoinRenderCompositionItem::WEIGHTED_OIT;
  });
  const bool peeling = std::any_of(order.begin(), order.end(), [](const CoinRenderCompositionItem & item) {
    return item.blend && item.deferred && !item.additive &&
      item.transparencyStrategy == CoinRenderCompositionItem::SORTED_LAYERS;
  });
  if (weighted || peeling) {
    auto options = frame.transparency;
    if (!peeling) options.layers = 1;
    uint64_t required = 0;
    if (!coin_render_transparency_sample_budget(width, height, options, true, samples, required, diagnostic))
      return false;
  }
  std::map<uint32_t, const CoinRenderViewportSnapshot *> layers;
  for (const auto & item : order) {
    const auto & draw = frame.draws[item.drawIndex];
    if (!draw.renderLayer) continue;
    auto inserted = layers.emplace(draw.renderLayer, nullptr);
    if (draw.clearDepthBefore && !inserted.first->second)
      inserted.first->second = &frame.viewports[frame.renderStates[draw.renderStateSlot].viewportSlot];
  }
  CoinRenderShadowPlan shadows;
  if (!frame.shadowGroups.empty()) {
    if (!coin_render_plan_shadows(frame, shadows, diagnostic) ||
        !coin_render_shadow_object_profile(frame, shadows, shadows.passes.size(), diagnostic) ||
        !bgfxShadowBatchSupported(frame, shadows, diagnostic)) return false;
    const auto * caps = bgfx::getCaps();
    const uint32_t stages = bgfxShadowMultitexture(frame) ? 11 : (weighted || peeling) ? 16 : shadows.passes.size() > 4 ? 16 : shadows.passes.size() > 2 ? 12 : 10;
    if (caps && caps->limits.maxTextureSamplers < stages) {
      diagnostic = "BGFX MSAA shadow receiver has insufficient texture stages";
      return false;
    }
    for (const auto & pass : shadows.passes) {
      if (caps && (pass.mapSize > caps->limits.maxTextureSize ||
          !(caps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
          !(caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
          !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA32F, shadowTextureFlags))) {
        diagnostic = "BGFX MSAA cannot render the planned shadow maps";
        return false;
      }
    }
  }
  if (bgfxShadowMultitexture(frame) && !coin_render_shadow_array_budget(shadows,2,diagnostic))return false;
  if (bgfxShadowMultitexture(frame) && !shadows.passes.empty() && targetCaps) {
    uint32_t extent=1; for (const auto & pass:shadows.passes) extent=std::max(extent,pass.mapSize);
    const auto layers=uint16_t(std::max<size_t>(2,shadows.passes.size()));
    if (extent>targetCaps->limits.maxTextureSize || layers>targetCaps->limits.maxTextureLayers ||
        !bgfx::isTextureValid(1,false,layers,bgfx::TextureFormat::RGBA32F,BGFX_TEXTURE_BLIT_DST)) {
      diagnostic="Shadow moment array extent/layers/format unavailable"; return false;
    }
  }
  // VSM maps remain single-sample; only the final scene is multisampled.
  const bool images = std::any_of(frame.renderStates.begin(), frame.renderStates.end(),
    [](const CoinRenderRenderStateSnapshot & state) { return state.rasterTextureSourceSlot != UINT32_MAX; });
  uint64_t views = (images ? 1 : 0) + shadows.passes.size() +
    (!shadows.passes.empty() && bgfxShadowMultitexture(frame) ? 1 : 0) +
    1 + bgfxDeferredViewCount(order, !weighted && !peeling,
      peeling ? frame.transparency.layers + 1 : 2,
      [&](const CoinRenderCompositionItem & item) { return frame.draws[item.drawIndex].isSceneLayer() && item.blend && item.deferred; },
      [](const CoinRenderCompositionItem & item) { return item.additive; }) +
    (samples > 1 ? 1 : 0) + (frame.stereoMode ? 1 : 0) + outputViews;
  for (const auto & layer : layers) {
    ++views;
    if (layer.second) {
      const auto & v = *layer.second;
      const int32_t viewport[4] = {v.x, v.y, v.width, v.height};
      int32_t clipped[4];
      if (CoinBgfxLowering::clipViewport(viewport, width, height, clipped)) ++views;
    }
  }
  if (frame.numPasses > 1) {
    const uint64_t fixedViews = (images ? 1 : 0) + shadows.passes.size() +
      (!shadows.passes.empty() && bgfxShadowMultitexture(frame) ? 1 : 0);
    views = fixedViews + (views - fixedViews - outputViews + 1) *
      uint64_t(frame.numPasses) + 1 + outputViews;
  }
  const uint32_t maximumViews = targetCaps ? targetCaps->limits.maxViews : targetViewCount;
  if (requiredViews) *requiredViews = uint32_t(views);
  if (views > maximumViews) {
    diagnostic = "BGFX composition, clears, resolve and output exceed available view IDs";
    return false;
  }
  return true;
}

void bgfxSplitSamplerViews(CoinBgfxPlan & plan) {
  const auto * caps = bgfx::getCaps();
  if (!caps || caps->rendererType != bgfx::RendererType::OpenGL) return;
  const size_t originalCount = plan.textures.size();
  std::vector<uint8_t> usage(originalCount, 0);
  const auto mark = [&](const CoinBgfxDraw & draw) {
    if (draw.hasTexture && draw.textureSlot < originalCount)
      usage[draw.textureSlot] |= CoinRenderTextureSamplingCore::mipFilter(draw.filter) ? 2 : 1;
    for (const auto & layer : draw.extraTextures)
      if (layer.enabled && layer.slot < originalCount)
        usage[layer.slot] |= CoinRenderTextureSamplingCore::mipFilter(layer.filter) ? 2 : 1;
  };
  for (const auto & draw : plan.draws) mark(draw);
  for (const auto & draw : plan.shadowDraws) mark(draw);
  std::vector<size_t> baseSlots(originalCount, SIZE_MAX);
  for (size_t slot = 0; slot < originalCount; ++slot) {
    if (usage[slot] != 3) continue;
    const auto & source = plan.textures[slot];
    if(source.cubeFaceSize || source.volumeDepth)continue;
    CoinBgfxTexture base;
    base.format = source.format;
    base.width = source.width;
    base.height = source.height;
    if (source.gpuToken != 0) base.gpuCopySourceSlot = slot;
    else {
      const size_t baseBytes = CoinRenderTextureFormatCore::levelBytes(
        source.width, source.height, source.format);
      if (baseBytes > source.pixelsRgba.size()) continue; // Existing upload validation rejects it.
      base.pixelsRgba.assign(source.pixelsRgba.begin(),
        source.pixelsRgba.begin() + baseBytes);
    }
    baseSlots[slot] = plan.textures.size();
    plan.textures.push_back(std::move(base));
  }
  const auto redirect = [&](CoinBgfxDraw & draw) {
    if (draw.hasTexture && draw.textureSlot < originalCount &&
        !CoinRenderTextureSamplingCore::mipFilter(draw.filter) &&
        baseSlots[draw.textureSlot] != SIZE_MAX)
      draw.textureSlot = static_cast<uint32_t>(baseSlots[draw.textureSlot]);
    for (auto & layer : draw.extraTextures)
      if (layer.enabled && layer.slot < originalCount &&
          !CoinRenderTextureSamplingCore::mipFilter(layer.filter) &&
          baseSlots[layer.slot] != SIZE_MAX)
        layer.slot = static_cast<uint32_t>(baseSlots[layer.slot]);
  };
  for (auto & draw : plan.draws) redirect(draw);
  for (auto & draw : plan.shadowDraws) redirect(draw);
}

bool bgfxBumpSnapshotFits(const CoinRenderFramePlan & frame)
{
  // Cache admission never limits rendering. Retain at most the last small
  // source payload; large scenes still reuse their image objects below.
  if(!frame.shadowGroups.empty() || !frame.shadowLights.empty() || !frame.rasterOccurrences.empty())return false;
  uint64_t bytes=sizeof(frame);
  bytes+=bgfxVectorBytes(frame.vertices)+bgfxVectorBytes(frame.indices)+bgfxVectorBytes(frame.materials)+
    bgfxVectorBytes(frame.bumpCoords)+bgfxVectorBytes(frame.bumpTangents)+bgfxVectorBytes(frame.bumpTransforms)+
    bgfxVectorBytes(frame.bumpImages)+bgfxVectorBytes(frame.lightingStates)+bgfxVectorBytes(frame.cameras)+
    bgfxVectorBytes(frame.viewports)+bgfxVectorBytes(frame.renderStates)+bgfxVectorBytes(frame.textures)+
    bgfxVectorBytes(frame.samplers)+bgfxVectorBytes(frame.draws);
  for(const auto & image:frame.bumpImages)bytes+=bgfxVectorBytes(image.rgb);
  for(const auto & image:frame.textures)bytes+=bgfxVectorBytes(image.pixelsRgba)+bgfxVectorBytes(image.mipmapsRgba);
  for(const auto & state:frame.lightingStates)bytes+=bgfxVectorBytes(state.lights);
  for(const auto & state:frame.renderStates)bytes+=bgfxVectorBytes(state.clipPlanesWorld);
  return bytes<=8u*1024u*1024u;
}

bool bgfxSameSourceTextures(const CoinRenderFramePlan & frame,
                                  const CoinBgfxPlan & plan)
{
  if (frame.textures.size() > plan.textures.size()) return false;
  for (size_t i = 0; i < frame.textures.size(); ++i) {
    const auto & source = frame.textures[i];
    const auto & cached = plan.textures[i];
    if (source.producerId || source.format != cached.format ||
        source.width != cached.width || source.height != cached.height ||
        source.cubeFaceSize != cached.cubeFaceSize ||
        source.cubeSourceComponents != cached.cubeSourceComponents ||
        source.cubeMipmapped != cached.cubeMipmapped ||
        source.volumeWidth != cached.volumeWidth || source.volumeHeight != cached.volumeHeight ||
        source.volumeDepth != cached.volumeDepth || source.volumeColumns != cached.volumeColumns ||
        source.volumeSourceComponents != cached.volumeSourceComponents ||
        source.volumeMipmapped != cached.volumeMipmapped || source.mipmapped != cached.mipmapped ||
        source.gpuToken != cached.gpuToken ||
        source.pixelsRgba.size() + source.mipmapsRgba.size() != cached.pixelsRgba.size() ||
        !std::equal(source.pixelsRgba.begin(),source.pixelsRgba.end(),cached.pixelsRgba.begin()) ||
        !std::equal(source.mipmapsRgba.begin(),source.mipmapsRgba.end(),
                    cached.pixelsRgba.begin()+source.pixelsRgba.size()))
      return false;
  }
  return true;
}

bool bgfxSameBumpTextures(const std::vector<CoinBgfxTexture> & a,
                                 const std::vector<CoinBgfxTexture> & b)
{
  if(a.size()!=b.size())return false;
  for(size_t i=0;i<a.size();++i) {
    const auto & x=a[i];const auto & y=b[i];
    if(x.width!=y.width || x.height!=y.height || x.format!=y.format || x.mipmapped!=y.mipmapped ||
       x.gpuToken!=y.gpuToken || x.gpuCopySourceSlot!=y.gpuCopySourceSlot ||
       x.cubeFaceSize!=y.cubeFaceSize || x.cubeSourceComponents!=y.cubeSourceComponents || x.cubeMipmapped!=y.cubeMipmapped ||
       x.volumeWidth!=y.volumeWidth || x.volumeHeight!=y.volumeHeight || x.volumeDepth!=y.volumeDepth ||
       x.volumeColumns!=y.volumeColumns || x.volumeSourceComponents!=y.volumeSourceComponents ||
       x.volumeMipmapped!=y.volumeMipmapped || x.pixelsRgba!=y.pixelsRgba)return false;
  }
  return true;
}

std::map<uint64_t, LostReadback> lostReadbacks;
uint64_t nextTicketToken = 1;
std::vector<std::shared_ptr<CoinBgfxBackend::AsyncEntry>> failedReadbacks;
}
