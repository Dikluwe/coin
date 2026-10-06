#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinbgfx/CoinBgfxBackend.h"
#include "rendering/coinrender/CoinRenderResourceCore.h"
#include "rendering/coinbgfx/CoinBgfxLowering.h"
#include "rendering/coinbgfx/CoinBgfxProgramCache.h"
#include "rendering/coinbgfx/CoinBgfxProgramSelection.h"
#include "rendering/coinrender/CoinRenderImageCore.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderSelectionCore.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderRttCore.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderAlphaTestCore.h"

#include "coin_bgfx_fs_depth_readback_glsl.h"
#include "coin_bgfx_fs_depth_readback_spirv.h"
#include "coin_bgfx_vs_glsl.h"
#include "coin_bgfx_fs_glsl.h"
#include "coin_bgfx_vs_spirv.h"
#include "coin_bgfx_fs_spirv.h"
#include "coin_bgfx_fs_solid_color_glsl.h"
#include "coin_bgfx_fs_solid_color_spirv.h"
#include "coin_bgfx_vs_instanced_color_glsl.h"
#include "coin_bgfx_vs_instanced_color_spirv.h"
#include "coin_bgfx_fs_instanced_color_glsl.h"
#include "coin_bgfx_fs_instanced_color_spirv.h"
#include "coin_bgfx_fs_peel_next_glsl.h"
#include "coin_bgfx_fs_peel_next_spirv.h"
#include "coin_bgfx_fs_composite_glsl.h"
#include "coin_bgfx_fs_composite_spirv.h"
#include "coin_bgfx_fs_weighted_oit_glsl.h"
#include "coin_bgfx_fs_weighted_oit_spirv.h"
#include "coin_bgfx_fs_weighted_composite_glsl.h"
#include "coin_bgfx_fs_weighted_composite_spirv.h"
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
#include "rendering/coinrender/CoinRenderShadowCore.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <map>
#include <new>
#ifndef _WIN32
#include <X11/Xlib.h>
#endif
#ifdef _WIN32
#include "coin_bgfx_fs_depth_readback_dx11.h"
#include "coin_bgfx_vs_dx11.h"
#include "coin_bgfx_fs_dx11.h"
#include "coin_bgfx_fs_solid_color_dx11.h"
#include "coin_bgfx_vs_instanced_color_dx11.h"
#include "coin_bgfx_fs_instanced_color_dx11.h"
#include "coin_bgfx_fs_peel_next_dx11.h"
#include "coin_bgfx_fs_composite_dx11.h"
#include "coin_bgfx_fs_weighted_oit_dx11.h"
#include "coin_bgfx_fs_weighted_composite_dx11.h"
#include "coin_bgfx_vs_shadow_moments_dx11.h"
#include "coin_bgfx_fs_shadow_moments_dx11.h"
#include "coin_bgfx_vs_shadow_receiver_dx11.h"
#include "coin_bgfx_fs_shadow_receiver_dx11.h"
#include "coin_bgfx_fs_shadow_receiver4_dx11.h"
#include "coin_bgfx_fs_shadow_receiver8_dx11.h"
#include "coin_bgfx_fs_shadow_peel_dx11.h"
#include "coin_bgfx_fs_shadow_oit_dx11.h"
#endif

#include <utility>

namespace {
// Isolated view blocks prevent per-window state from aliasing. API calls stay
// on one thread; BGFX owns its render worker.
constexpr bgfx::ViewId targetViewCount = 16;
struct SharedBgfxRuntime;
SharedBgfxRuntime & sharedRuntime();

template <typename Vertex>
void releaseUploadVertices(void *, void * owner)
{
  // BGFX may invoke this on its render worker, including during shutdown.
  // The owner is independent of the target and its reusable plan.
  delete static_cast<std::vector<Vertex> *>(owner);
}

template <typename Vertex>
const bgfx::Memory * uploadVertexVector(std::vector<Vertex> & vertices,
                                       size_t & uploadedCount, bool transfer)
{
  const uint32_t bytes = static_cast<uint32_t>(
    vertices.size() * sizeof(Vertex));
  // These vertices already exceed the CPU geometry retention budget. Transfer
  // their allocation instead of copying it only to discard the original.
  if (transfer || bytes > 32u * 1024u * 1024u) {
    auto * owner = new (std::nothrow) std::vector<Vertex>;
    if (owner) {
      uploadedCount = vertices.size();
      owner->swap(vertices);
      return bgfx::makeRef(owner->data(), bytes, releaseUploadVertices<Vertex>, owner);
    }
  }
  return bgfx::copy(vertices.data(), bytes);
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
  // The receiver surface implements alpha testing, but the moments stage has
  // no material/texture alpha. Decline the combined profile before GPU work.
  for (const auto & draw : frame.draws) {
    if (draw.renderStateSlot >= frame.renderStates.size()) {
      diagnostic = "BGFX shadow draw references an invalid render-state slot";
      return false;
    }
    if (coin_render_alpha_test_active(frame.renderStates[draw.renderStateSlot].alphaTestFunction)) {
      diagnostic = "BGFX alpha test with shadow maps requires an alpha-tested caster contract";
      return false;
    }
  }
  return true;
}

class CoinBgfxCallback : public bgfx::CallbackI {
public:
  explicit CoinBgfxCallback(bool openGl) : fatalCode(-1) {
    this->fatalMessage[0] = 0;
    const char * disabled = std::getenv("COIN_BGFX_DISABLE_PROGRAM_CACHE");
    this->programCacheEnabled = openGl && !(disabled && std::strcmp(disabled, "1") == 0);
  }

  void fatal(const char * filePath, uint16_t line, bgfx::Fatal::Enum code,
             const char * message) override
  {
    {
      std::lock_guard<std::mutex> guard(this->messageMutex);
      std::snprintf(this->fatalMessage, sizeof(this->fatalMessage),
        "BGFX fatal %d at %s:%u: %s", static_cast<int>(code),
        filePath ? filePath : "unknown", static_cast<unsigned int>(line),
        message ? message : "no diagnostic");
    }
    this->fatalCode.store(static_cast<int>(code), std::memory_order_release);
  }

  void traceVargs(const char * filePath, uint16_t line, const char * format,
                  va_list args) override
  {
    std::fprintf(stderr, "BGFX %s:%u: ", filePath ? filePath : "unknown",
                 static_cast<unsigned int>(line));
    std::vfprintf(stderr, format, args);
  }

  void profilerBegin(const char *, uint32_t, const char *, uint16_t) override {}
  void profilerBeginLiteral(const char *, uint32_t, const char *, uint16_t) override {}
  void profilerEnd() override {}
  uint32_t cacheReadSize(uint64_t key) override {
    auto * cache = this->programCacheEnabled ? programCache() : nullptr;
    return cache ? cache->size(key) : 0;
  }
  bool cacheRead(uint64_t key, void * data, uint32_t bytes) override {
    auto * cache = this->programCacheEnabled ? programCache() : nullptr;
    if (!cache || !cache->read(key, data, bytes)) return false;
    ++this->programCacheHits;
    return true;
  }
  void cacheWrite(uint64_t key, const void * data, uint32_t bytes) override {
    auto * cache = this->programCacheEnabled ? programCache() : nullptr;
    if (cache && cache->write(key, data, bytes)) ++this->programCacheWrites;
  }
  void traceProgramCache() const {
    std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_program_cache enabled=%d hits=%u writes=%u\n",
      this->programCacheEnabled ? 1 : 0, this->programCacheHits.load(), this->programCacheWrites.load());
  }
  void screenShot(const char * name, uint32_t width, uint32_t height,
                  uint32_t pitch, bgfx::TextureFormat::Enum format,
                  const void * pixels, uint32_t bytes, bool bottomLeft) override
  {
    std::lock_guard<std::mutex> guard(this->screenshotMutex);
    if (!name || !this->screenshotPending || name != this->screenshotName) return;
    const bool validFormat = format == bgfx::TextureFormat::BGRA8 ||
      format == bgfx::TextureFormat::RGBA8;
    // D3D12 footprints may omit trailing padding after the final row.
    const bool validRows = height > 0 &&
      uint64_t(pitch) * (height - 1u) + uint64_t(width) * 4u <= bytes;
    if (validFormat && pixels && width == this->screenshotWidth &&
        height == this->screenshotHeight && pitch >= width * 4u &&
        validRows) {
      try {
        this->screenshotPixels.resize(size_t(width) * height * 4u);
        const uint8_t * source = static_cast<const uint8_t *>(pixels);
        for (uint32_t y = 0; y < height; ++y) {
          const uint8_t * row = source + size_t(bottomLeft ? height - 1 - y : y) * pitch;
          uint8_t * output = this->screenshotPixels.data() + size_t(y) * width * 4u;
          for (uint32_t x = 0; x < width; ++x) {
            const uint8_t * pixel = row + size_t(x) * 4u;
            output[x * 4u] = pixel[format == bgfx::TextureFormat::BGRA8 ? 2 : 0];
            output[x * 4u + 1] = pixel[1];
            output[x * 4u + 2] = pixel[format == bgfx::TextureFormat::BGRA8 ? 0 : 2];
            output[x * 4u + 3] = pixel[3];
          }
        }
      } catch (const std::bad_alloc &) { this->screenshotPixels.clear(); }
    }
    this->screenshotPending = false;
    this->screenshotReady = true;
    this->screenshotCondition.notify_all();
  }

  std::string beginScreenshot(uint32_t width, uint32_t height)
  {
    std::lock_guard<std::mutex> guard(this->screenshotMutex);
    this->screenshotWidth = width; this->screenshotHeight = height;
    this->screenshotPixels.clear();
    this->screenshotReady = false; this->screenshotPending = true;
    this->screenshotName = "coin-window-rgba-" + std::to_string(++this->screenshotSerial);
    return this->screenshotName;
  }

  void cancelScreenshot()
  {
    std::lock_guard<std::mutex> guard(this->screenshotMutex);
    this->screenshotPending = false;
  }

  bool takeScreenshot(std::vector<uint8_t> & output)
  {
    std::unique_lock<std::mutex> guard(this->screenshotMutex);
    if (!this->screenshotCondition.wait_for(guard, std::chrono::seconds(5),
          [this] { return this->screenshotReady; })) {
      this->screenshotPending = false;
      return false;
    }
    if (this->screenshotPixels.empty()) return false;
    output.swap(this->screenshotPixels);
    return true;
  }
  void captureBegin(uint32_t, uint32_t, uint32_t,
                    bgfx::TextureFormat::Enum, bool) override {}
  void captureEnd() override {}
  void captureFrame(const void *, uint32_t) override {}

  bool failed() const
  {
    return this->fatalCode.load(std::memory_order_acquire) >= 0;
  }

  bool deviceLost() const
  {
    return this->fatalCode.load(std::memory_order_acquire) ==
      static_cast<int>(bgfx::Fatal::DeviceLost);
  }

  std::string diagnostic() const
  {
    std::lock_guard<std::mutex> guard(this->messageMutex);
    return this->fatalMessage;
  }

  void inject(bgfx::Fatal::Enum code, const char * message)
  {
    this->fatal("Coin fault injection", 0, code, message);
  }

private:
  static CoinBgfxProgramCache * programCache() {
    // Like the shared BGFX runtime, this bounded cache lives for the process.
    // It never holds renderer handles, callbacks, targets or disk files.
    static CoinBgfxProgramCache * cache = new (std::nothrow) CoinBgfxProgramCache;
    return cache;
  }
  bool programCacheEnabled = false;
  std::atomic<unsigned int> programCacheHits{0}, programCacheWrites{0};
  std::atomic<int> fatalCode;
  mutable std::mutex messageMutex;
  char fatalMessage[512];
  std::mutex screenshotMutex;
  std::condition_variable screenshotCondition;
  std::vector<uint8_t> screenshotPixels;
  uint32_t screenshotWidth = 0;
  uint32_t screenshotHeight = 0;
  bool screenshotPending = false;
  bool screenshotReady = false;
  uint64_t screenshotSerial = 0;
  std::string screenshotName;
};

struct SharedBgfxRuntime {
  std::mutex mutex;
  std::thread::id apiThread;
  std::shared_ptr<bgfx::CallbackI> callback;
  bgfx::RendererType::Enum renderer = bgfx::RendererType::Count;
  unsigned int references = 0;
  uint64_t generation = 0;
  std::vector<bool> viewBlocks;
};

SharedBgfxRuntime & sharedRuntime()
{
  // Process lifetime: even a target destroyed on the wrong thread cannot leave
  // BGFX holding a dangling callback. Such targets diagnose the contract error.
  static SharedBgfxRuntime * runtime = new SharedBgfxRuntime;
  return *runtime;
}

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

uint64_t drawState(const CoinBgfxDraw & draw)
{
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
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
  if (draw.blend && draw.additive) {
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

struct LogicalDrawStats {
  uint32_t opaqueDraws = 0;
  uint32_t transparentDraws = 0;
  uint32_t pipelineChanges = 0;
  uint32_t materialChanges = 0;
  uint32_t textureChanges = 0;
  uint32_t lightingChanges = 0;
  bool opaqueOrderChanged = false;
};

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
    lhs.wrapS == rhs.wrapS && lhs.wrapT == rhs.wrapT && lhs.filter == rhs.filter &&
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
const uint64_t peelTextureFlags = BGFX_TEXTURE_RT |
  BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
  BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
const uint64_t shadowTextureFlags = BGFX_TEXTURE_RT |
  BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

uint64_t peelDrawState(const CoinBgfxDraw & draw, bool depthOnly)
{
  uint64_t state = drawState(draw);
  state &= ~(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
             BGFX_STATE_WRITE_Z | BGFX_STATE_BLEND_MASK);
  if (!depthOnly || (draw.depthTest && draw.depthWrite)) state |= BGFX_STATE_WRITE_Z;
  if (!depthOnly) state |= BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
  return state;
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
  return bgfx::createProgram(vs, fs, true);
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
}

struct CoinBgfxBackend::AsyncEntry {
  ReadbackSlot slot;
  CoinRenderReadbackTicket ticket;
  bool bottomLeft = false;
};
namespace {
std::map<uint64_t, std::shared_ptr<CoinBgfxBackend::AsyncEntry>> & asyncEntries()
{
  static auto * entries = new std::map<uint64_t, std::shared_ptr<CoinBgfxBackend::AsyncEntry>>;
  return *entries;
}
struct LostReadback {
  CoinRenderReadbackTicket ticket;
  std::thread::id thread;
};
std::map<uint64_t, LostReadback> lostReadbacks;
uint64_t nextTicketToken = 1;
std::vector<std::shared_ptr<CoinBgfxBackend::AsyncEntry>> failedReadbacks;
bool sameTicket(const CoinRenderReadbackTicket & a, const CoinRenderReadbackTicket & b)
{
  return a.token == b.token && a.generation == b.generation &&
    a.submissionSerial == b.submissionSerial && a.width == b.width && a.height == b.height &&
    a.colorFormat == b.colorFormat && a.depthFormat == b.depthFormat &&
    a.colorRowPitch == b.colorRowPitch && a.depthRowPitch == b.depthRowPitch &&
    a.colorBytes == b.colorBytes && a.depthBytes == b.depthBytes;
}
// Caller holds the runtime mutex and has retired the GPU writes.
void releaseAsync(uint64_t token, bool failed = false)
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
    bgfx::shutdown();
    failedReadbacks.clear();
    runtime.callback.reset(); runtime.renderer = bgfx::RendererType::Count;
    runtime.apiThread = std::thread::id(); runtime.viewBlocks.clear();
  }
}
}

// Direct3D12 consumes the DXBC shader binaries generated by shaderc.
#ifdef _WIN32
#define COIN_BGFX_SHADER_DATA(name) (bgfx::getRendererType() == bgfx::RendererType::Direct3D12 ? name##_dx11 : bgfx::getRendererType() == bgfx::RendererType::OpenGL ? name##_glsl : name##_spirv)
#define COIN_BGFX_SHADER_SIZE(name) (bgfx::getRendererType() == bgfx::RendererType::Direct3D12 ? sizeof(name##_dx11) : bgfx::getRendererType() == bgfx::RendererType::OpenGL ? sizeof(name##_glsl) : sizeof(name##_spirv))
#else
#define COIN_BGFX_SHADER_DATA(name) (bgfx::getRendererType() == bgfx::RendererType::OpenGL ? name##_glsl : name##_spirv)
#define COIN_BGFX_SHADER_SIZE(name) (bgfx::getRendererType() == bgfx::RendererType::OpenGL ? sizeof(name##_glsl) : sizeof(name##_spirv))
#endif

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
    solidProgram(BGFX_INVALID_HANDLE), instancedProgram(BGFX_INVALID_HANDLE),
    activeProgram(BGFX_INVALID_HANDLE),
    shadowMomentsProgram(BGFX_INVALID_HANDLE),
    shadowReceiverProgram(BGFX_INVALID_HANDLE),
    shadowReceiverProgram4(BGFX_INVALID_HANDLE),
    shadowReceiverProgram8(BGFX_INVALID_HANDLE),
    shadowPeelProgram(BGFX_INVALID_HANDLE), shadowOitProgram(BGFX_INVALID_HANDLE),
    shadowModelViewUniform(BGFX_INVALID_HANDLE),
    shadowClipModelViewUniform(BGFX_INVALID_HANDLE),
    shadowDepthUniform(BGFX_INVALID_HANDLE),
    shadowQualityUniform(BGFX_INVALID_HANDLE), shadowLightIndicesUniform(BGFX_INVALID_HANDLE),
    shadowLightIndicesExtraUniform(BGFX_INVALID_HANDLE),
    depthReadProgram(BGFX_INVALID_HANDLE), readDepthSampler(BGFX_INVALID_HANDLE),
    depthReadFrameBuffer(BGFX_INVALID_HANDLE),
    peelNextProgram(BGFX_INVALID_HANDLE), compositeProgram(BGFX_INVALID_HANDLE),
    weightedOitProgram(BGFX_INVALID_HANDLE),
    weightedCompositeProgram(BGFX_INVALID_HANDLE),
    previousDepthSampler(BGFX_INVALID_HANDLE),
    previousColorSampler(BGFX_INVALID_HANDLE),
    layerSampler(BGFX_INVALID_HANDLE), oitAccumSampler(BGFX_INVALID_HANDLE),
    oitRevealSampler(BGFX_INVALID_HANDLE), depthInfoUniform(BGFX_INVALID_HANDLE),
    coinDepthUniform(BGFX_INVALID_HANDLE), screenDoorUniform(BGFX_INVALID_HANDLE),
    alphaTestUniform(BGFX_INVALID_HANDLE),
    clipMetaUniform(BGFX_INVALID_HANDLE), clipPlanesUniform(BGFX_INVALID_HANDLE),
    textureSampler(BGFX_INVALID_HANDLE), fogColorModeUniform(BGFX_INVALID_HANDLE),
    fogRangeUniform(BGFX_INVALID_HANDLE), textureParamsUniform(BGFX_INVALID_HANDLE),
    textureBlendUniform(BGFX_INVALID_HANDLE), textureCombineUniform(BGFX_INVALID_HANDLE),
    ambientLightUniform(BGFX_INVALID_HANDLE), lightCountUniform(BGFX_INVALID_HANDLE),
    lightPositionTypeUniform(BGFX_INVALID_HANDLE),
    lightDirectionCutoffUniform(BGFX_INVALID_HANDLE),
    lightColorIntensityUniform(BGFX_INVALID_HANDLE),
    instancedCameraUniform(BGFX_INVALID_HANDLE),
    lightAttenuationDropUniform(BGFX_INVALID_HANDLE),
    fullscreenVertexBuffer(BGFX_INVALID_HANDLE),
    fullscreenIndexBuffer(BGFX_INVALID_HANDLE),
    frameBuffer(BGFX_INVALID_HANDLE), oitFrameBuffer(BGFX_INVALID_HANDLE),
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

bool
CoinBgfxBackend::prepareShadowPrograms(size_t mapCount)
{
  const bool extended = mapCount > 2;
  const bool eight = mapCount > 4;
  const auto createExtended = [&]() {
    return createLayerProgram(
      COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver),
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_shadow_receiver4),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_shadow_receiver4));
  };
  const auto createEight = [&]() {
    return createLayerProgram(
      COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver),
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_shadow_receiver8),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_shadow_receiver8));
  };
  if (bgfx::isValid(this->shadowMomentsProgram) &&
      bgfx::isValid(this->shadowReceiverProgram)) {
    if (!extended || (bgfx::isValid(this->shadowReceiverProgram4) &&
                      (!eight || bgfx::isValid(this->shadowReceiverProgram8)))) return true;
    if (!bgfx::isValid(this->shadowReceiverProgram4)) this->shadowReceiverProgram4 = createExtended();
    if (eight && !bgfx::isValid(this->shadowReceiverProgram8)) this->shadowReceiverProgram8 = createEight();
    if (bgfx::isValid(this->shadowReceiverProgram4) && (!eight || bgfx::isValid(this->shadowReceiverProgram8))) return true;
    this->lastError = "BGFX extended shadow shader allocation failed";
    return false;
  }
  this->shadowMomentsProgram = createLayerProgram(
    COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_moments),
    COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_moments),
    COIN_BGFX_SHADER_DATA(coin_bgfx_fs_shadow_moments),
    COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_shadow_moments));
  this->shadowReceiverProgram = createLayerProgram(
    COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver),
    COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver),
    COIN_BGFX_SHADER_DATA(coin_bgfx_fs_shadow_receiver),
    COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_shadow_receiver));
  if (extended) {
    this->shadowReceiverProgram4 = createExtended();
    if (eight) this->shadowReceiverProgram8 = createEight();
  }
  this->shadowModelViewUniform = bgfx::createUniform("u_shadowModelView", bgfx::UniformType::Mat4);
  this->shadowClipModelViewUniform = bgfx::createUniform("u_shadowClipModelView", bgfx::UniformType::Mat4);
  this->shadowDepthUniform = bgfx::createUniform("u_shadowDepth", bgfx::UniformType::Vec4);
  this->shadowQualityUniform = bgfx::createUniform("u_shadowQuality", bgfx::UniformType::Vec4);
  this->shadowLightIndicesUniform = bgfx::createUniform("u_shadowLightIndices", bgfx::UniformType::Vec4);
  this->shadowLightIndicesExtraUniform = bgfx::createUniform("u_shadowLightIndicesExtra", bgfx::UniformType::Vec4);
  bool valid = bgfx::isValid(this->shadowLightIndicesExtraUniform) && bgfx::isValid(this->shadowMomentsProgram) &&
    bgfx::isValid(this->shadowReceiverProgram) &&
    (!extended || (bgfx::isValid(this->shadowReceiverProgram4) && (!eight || bgfx::isValid(this->shadowReceiverProgram8)))) &&
    bgfx::isValid(this->shadowModelViewUniform) &&
    bgfx::isValid(this->shadowClipModelViewUniform) &&
    bgfx::isValid(this->shadowDepthUniform) &&
    bgfx::isValid(this->shadowLightIndicesUniform) && bgfx::isValid(this->shadowQualityUniform);
  for (int i = 0; i < 8; ++i) {
    const std::string suffix = std::to_string(i);
    this->shadowViewToClipUniform[i] = bgfx::createUniform(
      ("u_shadowViewToClip" + suffix).c_str(), bgfx::UniformType::Mat4);
    this->shadowViewToLightUniform[i] = bgfx::createUniform(
      ("u_shadowViewToLight" + suffix).c_str(), bgfx::UniformType::Mat4);
    this->shadowParamsUniform[i] = bgfx::createUniform(
      ("u_shadowParams" + suffix).c_str(), bgfx::UniformType::Vec4);
    this->shadowMetaUniform[i] = bgfx::createUniform(
      ("u_shadowMeta" + suffix).c_str(), bgfx::UniformType::Vec4);
    this->shadowSampler[i] = bgfx::createUniform(
      ("s_shadow" + suffix).c_str(), bgfx::UniformType::Sampler);
    valid = valid && bgfx::isValid(this->shadowViewToClipUniform[i]) &&
      bgfx::isValid(this->shadowViewToLightUniform[i]) &&
      bgfx::isValid(this->shadowParamsUniform[i]) &&
      bgfx::isValid(this->shadowMetaUniform[i]) &&
      bgfx::isValid(this->shadowSampler[i]);
  }
  if (!valid) this->lastError = "BGFX shadow shader or uniform allocation failed";
  return valid;
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
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_shadow_peel),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_shadow_peel));
  if (!bgfx::isValid(this->shadowOitProgram))
    this->shadowOitProgram = createLayerProgram(
      COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver),
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_shadow_oit),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_shadow_oit));
  const bool valid = bgfx::isValid(this->shadowPeelProgram) && bgfx::isValid(this->shadowOitProgram);
  if (!valid) this->lastError = "Shadow transparency shader allocation failed";
  return valid;
}

CoinBgfxBackend::~CoinBgfxBackend()
{
  this->shutdownRuntime();
}

CoinRenderBackendStatus
CoinBgfxBackend::checkRuntimeFailure(const char * operation)
{
  CoinBgfxCallback * cb = static_cast<CoinBgfxCallback *>(this->callback.get());
  if (cb == nullptr || !cb->failed()) return CoinRenderBackendStatus::SUCCESS;
  this->status = cb->deviceLost() ? CoinRenderBackendStatus::DEVICE_LOST :
                                   CoinRenderBackendStatus::BACKEND_ERROR;
  this->lastError = std::string(operation) + ": " + cb->diagnostic();
  return this->status;
}

void
CoinBgfxBackend::destroyResources()
{
  if (!this->initialized) return;
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
    return;
  }
  this->destroyFrameBuffers();
  for (DirectTextureResource & resource : this->directTextures)
    if (bgfx::isValid(resource.frameBuffer)) bgfx::destroy(resource.frameBuffer);
  if (bgfx::isValid(this->fullscreenVertexBuffer)) bgfx::destroy(this->fullscreenVertexBuffer);
  if (bgfx::isValid(this->fullscreenIndexBuffer)) bgfx::destroy(this->fullscreenIndexBuffer);
  if (bgfx::isValid(this->previousDepthSampler)) bgfx::destroy(this->previousDepthSampler);
  if (bgfx::isValid(this->previousColorSampler)) bgfx::destroy(this->previousColorSampler);
  if (bgfx::isValid(this->layerSampler)) bgfx::destroy(this->layerSampler);
  if (bgfx::isValid(this->depthInfoUniform)) bgfx::destroy(this->depthInfoUniform);
  if (bgfx::isValid(this->screenDoorUniform)) bgfx::destroy(this->screenDoorUniform);
  if (bgfx::isValid(this->alphaTestUniform)) bgfx::destroy(this->alphaTestUniform);
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
  if (bgfx::isValid(this->textureParamsUniform)) bgfx::destroy(this->textureParamsUniform);
  if (bgfx::isValid(this->textureBlendUniform)) bgfx::destroy(this->textureBlendUniform);
  if (bgfx::isValid(this->textureCombineUniform)) bgfx::destroy(this->textureCombineUniform);
  if (bgfx::isValid(this->ambientLightUniform)) bgfx::destroy(this->ambientLightUniform);
  if (bgfx::isValid(this->lightCountUniform)) bgfx::destroy(this->lightCountUniform);
  if (bgfx::isValid(this->lightPositionTypeUniform)) bgfx::destroy(this->lightPositionTypeUniform);
  if (bgfx::isValid(this->lightDirectionCutoffUniform)) bgfx::destroy(this->lightDirectionCutoffUniform);
  if (bgfx::isValid(this->lightColorIntensityUniform)) bgfx::destroy(this->lightColorIntensityUniform);
  if (bgfx::isValid(this->instancedCameraUniform)) bgfx::destroy(this->instancedCameraUniform);
  if (bgfx::isValid(this->lightAttenuationDropUniform)) bgfx::destroy(this->lightAttenuationDropUniform);
  if (bgfx::isValid(this->defaultTexture)) bgfx::destroy(this->defaultTexture);
  for (bgfx::TextureHandle texture : this->cachedTextures)
    if (bgfx::isValid(texture)) bgfx::destroy(texture);
  if (bgfx::isValid(this->cachedVertexBuffer)) bgfx::destroy(this->cachedVertexBuffer);
  if (bgfx::isValid(this->cachedIndexBuffer)) bgfx::destroy(this->cachedIndexBuffer);
  if (bgfx::isValid(this->cachedInstanceBuffer)) bgfx::destroy(this->cachedInstanceBuffer);
  if (bgfx::isValid(this->peelNextProgram)) bgfx::destroy(this->peelNextProgram);
  if (bgfx::isValid(this->compositeProgram)) bgfx::destroy(this->compositeProgram);
  if (bgfx::isValid(this->depthReadProgram)) bgfx::destroy(this->depthReadProgram);
  if (bgfx::isValid(this->readDepthSampler)) bgfx::destroy(this->readDepthSampler);
  if (bgfx::isValid(this->program)) bgfx::destroy(this->program);
  if (bgfx::isValid(this->solidProgram)) bgfx::destroy(this->solidProgram);
  if (bgfx::isValid(this->instancedProgram)) bgfx::destroy(this->instancedProgram);
  if (bgfx::isValid(this->shadowMomentsProgram)) bgfx::destroy(this->shadowMomentsProgram);
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
  if (bgfx::isValid(this->shadowLightIndicesUniform)) bgfx::destroy(this->shadowLightIndicesUniform);
  for (int i = 0; i < 8; ++i) {
    if (bgfx::isValid(this->shadowViewToClipUniform[i])) bgfx::destroy(this->shadowViewToClipUniform[i]);
    if (bgfx::isValid(this->shadowViewToLightUniform[i])) bgfx::destroy(this->shadowViewToLightUniform[i]);
    if (bgfx::isValid(this->shadowParamsUniform[i])) bgfx::destroy(this->shadowParamsUniform[i]);
    if (bgfx::isValid(this->shadowMetaUniform[i])) bgfx::destroy(this->shadowMetaUniform[i]);
    if (bgfx::isValid(this->shadowSampler[i])) bgfx::destroy(this->shadowSampler[i]);
  }
  if (bgfx::isValid(this->weightedOitProgram)) bgfx::destroy(this->weightedOitProgram);
  if (bgfx::isValid(this->weightedCompositeProgram)) bgfx::destroy(this->weightedCompositeProgram);
}

void
CoinBgfxBackend::shutdownRuntime()
{
  if (!this->initialized) return;
  // Release this target without interrupting other windows on the shared device.
  if (!this->onApiThread()) {
    std::fprintf(stderr, "BGFX evaluation: destroy the target on its API thread; global renderer left active\n");
    return;
  }
  this->destroyResources();
  SharedBgfxRuntime & runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
  if (!static_cast<CoinBgfxCallback *>(runtime.callback.get())->failed()) {
    for (bgfx::ViewId view = this->viewBase;
         view < this->viewBase + targetViewCount; ++view) bgfx::resetView(view);
  }
  runtime.viewBlocks[this->viewBase / targetViewCount] = false;
  if (--runtime.references == 0) {
    bgfx::shutdown();
    failedReadbacks.clear();
    runtime.callback.reset();
    runtime.renderer = bgfx::RendererType::Count;
    runtime.apiThread = std::thread::id();
    runtime.viewBlocks.clear();
  } else if (!static_cast<CoinBgfxCallback *>(runtime.callback.get())->failed()) {
    // Surface destruction is queued to the render worker. Retire it before
    // the caller destroys the native window, while retaining other targets.
    // The second frame waits for consumption of the first frame's commands.
    bgfx::frame();
    bgfx::frame();
  }
  this->callback.reset();
  this->initialized = false;
}

bool
CoinBgfxBackend::onApiThread() const
{
  return this->apiThread == std::this_thread::get_id();
}

CoinRenderBackendStatus
CoinBgfxBackend::prepare(CoinRenderTargetP & target)
{
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
#ifdef _WIN32
  this->nativeDisplay = nullptr;
  this->nativeWindow = this->presentToWindow ? target.nativeDesc.native.win32.hwnd : nullptr;
#else
  this->nativeDisplay = this->presentToWindow ? target.nativeDesc.native.xlib.display : nullptr;
  this->nativeWindow = this->presentToWindow ? reinterpret_cast<void *>(
    static_cast<uintptr_t>(target.nativeDesc.native.xlib.window)) : nullptr;
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
    bgfx::Init init;
    init.type = renderer;
    runtime.callback = std::make_shared<CoinBgfxCallback>(renderer == bgfx::RendererType::OpenGL);
    init.callback = runtime.callback.get();
    // No visible window owns the primary swapchain. Closing the first target
    // therefore cannot invalidate any other target's renderer/context.
    init.swapChain.ndt = this->nativeDisplay;
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
      runtime.callback.reset();
      this->lastError = std::string("BGFX could not initialize its headless renderer: ") + bgfx::getRendererName(renderer);
      return CoinRenderBackendStatus::NOT_READY;
    }
    runtime.apiThread = std::this_thread::get_id();
    ++runtime.generation;
    runtime.renderer = renderer;
    runtime.viewBlocks.assign(bgfx::getCaps()->limits.maxViews / targetViewCount, false);
  }
  const auto freeBlock = std::find(runtime.viewBlocks.begin(), runtime.viewBlocks.end(), false);
  if (freeBlock == runtime.viewBlocks.end()) {
    this->lastError = "BGFX target view budget exhausted";
    if (runtime.references == 0) {
      bgfx::shutdown();
      runtime.callback.reset();
      runtime.viewBlocks.clear();
    }
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
  this->viewBase = static_cast<bgfx::ViewId>(
    std::distance(runtime.viewBlocks.begin(), freeBlock) * targetViewCount);
  *freeBlock = true;
  ++runtime.references;
  this->callback = runtime.callback;
  runtimeGuard.unlock();
  this->initialized = true;
  this->apiThread = std::this_thread::get_id();
  if (consumeTestFault("COIN_BGFX_TEST_FAIL_PREPARE_ONCE")) {
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
      caps->limits.maxFBAttachments >= 2 &&
      bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA16F, peelTextureFlags) &&
      (caps->formats[bgfx::TextureFormat::RGBA16F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) &&
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
    .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord1, 3, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord2, 3, bgfx::AttribType::Float)
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
  this->textureParamsUniform = bgfx::createUniform("u_texParams", bgfx::UniformType::Vec4, COIN_RENDER_MAX_TEXTURE_UNITS);
  this->screenDoorUniform = bgfx::createUniform("u_screenDoor", bgfx::UniformType::Vec4);
  this->alphaTestUniform = bgfx::createUniform("u_alphaTest", bgfx::UniformType::Vec4);
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
      !bgfx::isValid(this->alphaTestUniform) ||
      !bgfx::isValid(this->textureParamsUniform) ||
      !bgfx::isValid(this->textureBlendUniform) ||
      !bgfx::isValid(this->textureCombineUniform) ||
      !bgfx::isValid(this->ambientLightUniform) ||
      !bgfx::isValid(this->clipMetaUniform) ||
      !bgfx::isValid(this->clipPlanesUniform) ||
      !bgfx::isValid(this->lightCountUniform) ||
      !bgfx::isValid(this->lightPositionTypeUniform) ||
      !bgfx::isValid(this->lightDirectionCutoffUniform) ||
      !bgfx::isValid(this->lightColorIntensityUniform) ||
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

bool
CoinBgfxBackend::prepareBaseProgram(const std::vector<CoinBgfxDraw> & draws,
                                    bool hasShadows,
                                    CoinBgfxTransparencyStrategy strategy,
                                    bool instanced)
{
  this->activeProgram = BGFX_INVALID_HANDLE;
  if (draws.empty()) return true;
  const char * disableSolid = std::getenv("COIN_BGFX_DISABLE_SOLID_PROGRAM");
  const bool solid = !(disableSolid && std::strcmp(disableSolid, "1") == 0) &&
    coin_bgfx_solid_program(draws, hasShadows, strategy);
  bgfx::ProgramHandle & selected = instanced ? this->instancedProgram :
    solid ? this->solidProgram : this->program;
  if (!bgfx::isValid(selected)) {
    selected = createLayerProgram(
      instanced ? COIN_BGFX_SHADER_DATA(coin_bgfx_vs_instanced_color) : COIN_BGFX_SHADER_DATA(coin_bgfx_vs),
      instanced ? COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_instanced_color) : COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
      instanced ? COIN_BGFX_SHADER_DATA(coin_bgfx_fs_instanced_color) :
        solid ? COIN_BGFX_SHADER_DATA(coin_bgfx_fs_solid_color) : COIN_BGFX_SHADER_DATA(coin_bgfx_fs),
      instanced ? COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_instanced_color) :
        solid ? COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_solid_color) : COIN_BGFX_SHADER_SIZE(coin_bgfx_fs));
    if (!bgfx::isValid(selected)) {
      this->lastError = instanced ? "BGFX failed to create its instanced shader program" :
        solid ? "BGFX failed to create its solid-color shader program" :
                                "BGFX failed to create its general surface shader program";
      this->status = CoinRenderBackendStatus::BACKEND_ERROR;
      return false;
    }
  }
  this->activeProgram = selected;
  if (CoinRenderDiagnosticShell::phaseTracingEnabled())
    std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_base_program solid=%d instancing=%d\n",
      solid ? 1 : 0, instanced ? 1 : 0);
  return true;
}

bool
CoinBgfxBackend::prepareFullscreenResources()
{
  if (!bgfx::isValid(this->depthInfoUniform))
    this->depthInfoUniform = bgfx::createUniform("u_depthInfo", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->fullscreenVertexBuffer)) {
    CoinBgfxVertex fullscreen[3] = {};
    fullscreen[0].position[0] = -1.0f; fullscreen[0].position[1] = -1.0f;
    fullscreen[1].position[0] = 3.0f; fullscreen[1].position[1] = -1.0f;
    fullscreen[2].position[0] = -1.0f; fullscreen[2].position[1] = 3.0f;
    this->fullscreenVertexBuffer = bgfx::createVertexBuffer(bgfx::copy(fullscreen, sizeof(fullscreen)), this->layout);
  }
  if (!bgfx::isValid(this->fullscreenIndexBuffer)) {
    const uint16_t indices[3] = {0, 1, 2};
    this->fullscreenIndexBuffer = bgfx::createIndexBuffer(bgfx::copy(indices, sizeof(indices)));
  }
  if (!bgfx::isValid(this->depthInfoUniform) || !bgfx::isValid(this->fullscreenVertexBuffer) ||
      !bgfx::isValid(this->fullscreenIndexBuffer)) {
    this->lastError = "BGFX could not allocate shared transparency resources";
    this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    return false;
  }
  return true;
}

bool
CoinBgfxBackend::prepareDepthReadbackResources()
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
CoinBgfxBackend::prepareTransparencyPrograms(CoinBgfxTransparencyStrategy strategy)
{
  if (strategy != CoinBgfxTransparencyStrategy::OBJECT && !this->prepareFullscreenResources()) return false;
  if (strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS) {
    const uint8_t * vertexShader = COIN_BGFX_SHADER_DATA(coin_bgfx_vs);
    const uint32_t vertexBytes = COIN_BGFX_SHADER_SIZE(coin_bgfx_vs);
    if (!bgfx::isValid(this->peelNextProgram)) this->peelNextProgram = createLayerProgram(vertexShader, vertexBytes,
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_peel_next),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_peel_next));
    if (!bgfx::isValid(this->compositeProgram)) this->compositeProgram = createLayerProgram(vertexShader, vertexBytes,
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_composite),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_composite));
    if (!bgfx::isValid(this->previousDepthSampler)) this->previousDepthSampler = bgfx::createUniform("s_prevDepth", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->previousColorSampler)) this->previousColorSampler = bgfx::createUniform("s_prevColor", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->layerSampler)) this->layerSampler = bgfx::createUniform("s_layer", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->peelNextProgram) ||
        !bgfx::isValid(this->compositeProgram) ||
        !bgfx::isValid(this->previousDepthSampler) ||
        !bgfx::isValid(this->previousColorSampler) ||
        !bgfx::isValid(this->layerSampler) ||
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
    if (!bgfx::isValid(this->weightedOitProgram)) this->weightedOitProgram = createLayerProgram(vertexShader, vertexBytes,
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_weighted_oit),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_weighted_oit));
    if (!bgfx::isValid(this->weightedCompositeProgram)) this->weightedCompositeProgram = createLayerProgram(vertexShader, vertexBytes,
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_weighted_composite),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_weighted_composite));
    if (!bgfx::isValid(this->oitAccumSampler)) this->oitAccumSampler = bgfx::createUniform("s_oitAccum", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->oitRevealSampler)) this->oitRevealSampler = bgfx::createUniform("s_oitReveal", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->weightedOitProgram) ||
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
CoinBgfxBackend::destroyFrameBuffers()
{
  for (uint8_t pass = 0; pass < COIN_RENDER_MAX_PEEL_LAYERS; ++pass) {
    if (bgfx::isValid(this->peelFrameBuffers[pass]))
      bgfx::destroy(this->peelFrameBuffers[pass]);
    this->peelFrameBuffers[pass] = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(this->oitFrameBuffer)) bgfx::destroy(this->oitFrameBuffer);
  this->oitFrameBuffer = BGFX_INVALID_HANDLE;
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
  if (bgfx::isValid(this->depthReadFrameBuffer)) bgfx::destroy(this->depthReadFrameBuffer);
  this->depthReadFrameBuffer = BGFX_INVALID_HANDLE;
  this->lastPublishedDepth.clear();
  this->readbackSlots.clear();
  this->lastPublishedReadback.clear();
  this->readbackCursor = 0;
  this->readbackSequence = 0;
  this->lastPublishedSequence = 0;
  if (bgfx::isValid(this->frameBuffer)) bgfx::destroy(this->frameBuffer);
  this->readbackTexture = BGFX_INVALID_HANDLE;
  this->frameBuffer = BGFX_INVALID_HANDLE;
  this->width = this->height = 0;
}

bool
CoinBgfxBackend::resize(int newWidth, int newHeight)
{
  if (newWidth == this->width && newHeight == this->height) return true;
  if (newWidth <= 0 || newHeight <= 0 || newWidth > 16384 || newHeight > 16384) {
    this->lastError = "Invalid BGFX target dimensions";
    return false;
  }
  if (consumeTestFault("COIN_BGFX_TEST_DEVICE_LOST_ON_RESIZE_ONCE")) {
    static_cast<CoinBgfxCallback *>(this->callback.get())->inject(
      bgfx::Fatal::DeviceLost, "injected device loss during resize");
    this->checkRuntimeFailure("BGFX resize failed");
    return false;
  }
  // Preserve the native framebuffer handle across resize; only its surface
  // attachments are resized. OIT/peel attachments remain private to this target.
  const bgfx::FrameBufferHandle windowOutput = this->presentToWindow ?
    this->frameBuffer : bgfx::FrameBufferHandle BGFX_INVALID_HANDLE;
  if (this->presentToWindow) this->frameBuffer = BGFX_INVALID_HANDLE;
  this->destroyFrameBuffers();
  if (this->presentToWindow) this->frameBuffer = windowOutput;
  const bgfx::Caps * caps = bgfx::getCaps();
  const bgfx::TextureFormat::Enum depthFormat =
    (caps->formats[bgfx::TextureFormat::D24S8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)
      ? bgfx::TextureFormat::D24S8 : bgfx::TextureFormat::D32F;
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
  if (this->activeTransparencyStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS) {
    for (uint8_t pass = 0; pass < this->peelPassCount; ++pass) {
      bgfx::TextureHandle color =
          bgfx::createTexture2D(static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
                                false, 1, bgfx::TextureFormat::RGBA16F, peelTextureFlags);
      bgfx::TextureHandle depth = bgfx::createTexture2D(
        static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
        false, 1, bgfx::TextureFormat::D32F, peelTextureFlags);
      if (!bgfx::isValid(color) || !bgfx::isValid(depth)) {
        if (bgfx::isValid(color)) bgfx::destroy(color);
        if (bgfx::isValid(depth)) bgfx::destroy(depth);
        this->lastError = "BGFX could not allocate sorted-layer textures";
        this->destroyFrameBuffers();
        return false;
      }
      const bgfx::TextureHandle attachments[2] = {color, depth};
      this->peelFrameBuffers[pass] = bgfx::createFrameBuffer(2, attachments, true);
      if (!bgfx::isValid(this->peelFrameBuffers[pass])) {
        bgfx::destroy(color);
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
      false, 1, bgfx::TextureFormat::RGBA16F, peelTextureFlags);
    bgfx::TextureHandle reveal = bgfx::createTexture2D(
      static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
      false, 1, bgfx::TextureFormat::R16F, peelTextureFlags);
    bgfx::TextureHandle depth = bgfx::createTexture2D(
      static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),

      false, 1, depthFormat, BGFX_TEXTURE_RT_WRITE_ONLY);
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
  return this->checkRuntimeFailure("BGFX resize failed") == CoinRenderBackendStatus::SUCCESS;
}
void
CoinBgfxBackend::bindDrawTexture(
  const CoinBgfxDraw & draw, const std::vector<bgfx::TextureHandle> & textures)
{
  float params[COIN_RENDER_MAX_TEXTURE_UNITS][4] = {};
  float blend[COIN_RENDER_MAX_TEXTURE_UNITS][4] = {};
  for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
    CoinBgfxDraw::TextureLayer layer;
    if (unit == 0) {
      layer.enabled = draw.hasTexture; layer.slot = draw.textureSlot;
      layer.model = draw.textureModel; layer.wrapS = draw.wrapS; layer.wrapT = draw.wrapT;
      layer.filter = draw.filter;
      std::memcpy(layer.blendColor, draw.textureBlendColor, sizeof(layer.blendColor));
    } else layer = draw.extraTextures[unit - 1];
    bgfx::TextureHandle texture = this->defaultTexture;
    if (layer.enabled && layer.slot < textures.size() && bgfx::isValid(textures[layer.slot]))
      texture = textures[layer.slot];
    uint32_t flags = BGFX_SAMPLER_NONE;
    if (layer.wrapS == CoinRenderTextureWrap::CLAMP) flags |= BGFX_SAMPLER_U_CLAMP;
    if (layer.wrapT == CoinRenderTextureWrap::CLAMP) flags |= BGFX_SAMPLER_V_CLAMP;
    if (layer.filter == CoinRenderTextureFilter::NEAREST)
      flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
    bool directRenderTarget = false;
    if (layer.enabled) for (const auto & resource : this->directTextures) {
      const auto direct = bgfx::getTexture(resource.frameBuffer, 0);
      if (bgfx::isValid(direct) && direct.idx == texture.idx) directRenderTarget = true;
    }
    params[unit][0] = layer.enabled ? 1.0f : 0.0f;
    params[unit][1] = static_cast<float>(layer.model);
    params[unit][2] = directRenderTarget && !bgfx::getCaps()->originBottomLeft ? 1.0f : 0.0f;
    params[unit][3] = static_cast<float>(draw.textureProjection);
    std::memcpy(blend[unit], layer.blendColor, sizeof(layer.blendColor));
    bgfx::setTexture(static_cast<uint8_t>(unit + 2),
      unit == 0 ? this->textureSampler : this->extraTextureSamplers[unit - 1], texture, flags);
  }
  bgfx::setUniform(this->textureParamsUniform, params, COIN_RENDER_MAX_TEXTURE_UNITS);
  bgfx::setUniform(this->textureBlendUniform, blend, COIN_RENDER_MAX_TEXTURE_UNITS);
  bgfx::setUniform(this->textureCombineUniform, draw.textureCombines, COIN_RENDER_MAX_TEXTURE_UNITS * 4);
}


void
CoinBgfxBackend::bindDrawLighting(const CoinBgfxDraw & draw, int targetHeight)
{
  const float door[4] = {draw.screenDoor[0], float(targetHeight > 0 ? targetHeight : this->height),
    bgfx::getCaps()->originBottomLeft ? 1.0f : 0.0f, draw.screenDoor[3]};
  bgfx::setUniform(this->screenDoorUniform, door);
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
  bgfx::setPaletteColor(1, transparentBlack);
  for (uint8_t pass = 0; pass < layers.size(); ++pass) {
    const bgfx::ViewId view = firstView + pass;
    bgfx::setViewName(view, "transparent_accumulation");
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(view, 0, 0, static_cast<uint16_t>(width),
                      static_cast<uint16_t>(height));
    bgfx::setViewFrameBuffer(view, layers[pass]);
    bgfx::setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.0f, 0, 1);
    bgfx::setViewTransform(view, nullptr, nullptr);
    bgfx::touch(view);
    // Opaque geometry supplies the occlusion depth in every peel pass.
    for (const CoinBgfxDraw & draw : draws) {
      if (draw.renderLayer != 0 || (draw.blend && draw.deferred) || !draw.depthTest || !draw.depthWrite)
        continue;
      bgfx::setTransform(draw.mvp);
      bgfx::setVertexBuffer(0, vertices);
      bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
      bgfx::setState(peelDrawState(draw, true));
      if (!setDrawScissor(draw, width, height)) continue;
      this->bindDrawTexture(draw, textures);
      this->bindDrawLighting(draw);
      bgfx::submit(view, this->activeProgram);

    }
    for (const CoinBgfxDraw & draw : draws) {
      if (draw.renderLayer != 0 || !draw.blend || !draw.deferred || draw.additive) continue;
      bgfx::setTransform(draw.mvp);
      bgfx::setVertexBuffer(0, vertices);
      bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
      bgfx::setState(peelDrawState(draw, false));
      if (!setDrawScissor(draw, width, height)) continue;
      if (pass != 0) {
        bgfx::setUniform(this->depthInfoUniform, depthInfo);
        bgfx::setTexture(0, this->previousDepthSampler,
                         bgfx::getTexture(layers[pass - 1], 1));
        bgfx::setTexture(1, this->previousColorSampler,
                         bgfx::getTexture(layers[pass - 1], 0));
      }
      this->bindDrawTexture(draw, textures);
      if (hasShadows) this->bindShadowReceiver(frame, shadows, shadowMaps, draw, height);
      else this->bindDrawLighting(draw, height);
      const auto receiver = shadows.passes.size() > 4 ? this->shadowReceiverProgram8 :
        shadows.passes.size() > 2 ? this->shadowReceiverProgram4 : this->shadowReceiverProgram;
      bgfx::submit(view, hasShadows ? (pass == 0 ? receiver : this->shadowPeelProgram) :
        (pass == 0 ? this->activeProgram : this->peelNextProgram));
    }
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
    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
    bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
    bgfx::setUniform(this->depthInfoUniform, depthInfo);
    bgfx::setTexture(0, this->layerSampler,
                     bgfx::getTexture(layers[pass], 0));
    bgfx::setState(blendState);
    bgfx::submit(compositeView, this->compositeProgram);
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
  const std::vector<bgfx::FrameBufferHandle> & layers, bgfx::FrameBufferHandle oitBuffer)
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

  // Rebuild only opaque depth so transparent fragments behind opaque Coin
  // geometry cannot contribute to either accumulation attachment.
  for (const CoinBgfxDraw & draw : draws) {
    if (draw.renderLayer != 0 || (draw.blend && draw.deferred) || !draw.depthTest || !draw.depthWrite)
      continue;
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vertices);
    bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
    bgfx::setState(peelDrawState(draw, true));
    if (!setDrawScissor(draw, width, height)) continue;
    this->bindDrawTexture(draw, textures);
    this->bindDrawLighting(draw);
    bgfx::submit(oitView, this->activeProgram);

  }

  for (const CoinBgfxDraw & draw : draws) {
    if (draw.renderLayer != 0 || !draw.blend || !draw.deferred || draw.additive)
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
    bgfx::setState(state, independentBlend);
    if (!setDrawScissor(draw, width, height)) continue;
    this->bindDrawTexture(draw, textures);
    if (hasShadows) this->bindShadowReceiver(frame, shadows, shadowMaps, draw, height);
    else this->bindDrawLighting(draw, height);
    bgfx::submit(oitView, hasShadows ? this->shadowOitProgram : this->weightedOitProgram);

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
  bgfx::setTransform(identity);
  bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
  bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
  bgfx::setUniform(this->depthInfoUniform, depthInfo);
  bgfx::setTexture(0, this->oitAccumSampler,
                   bgfx::getTexture(oitBuffer, 0));
  bgfx::setTexture(1, this->oitRevealSampler,
                   bgfx::getTexture(oitBuffer, 1));
  bgfx::setState(blendState);
  bgfx::submit(compositeView, this->weightedCompositeProgram);
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
  const std::vector<bgfx::FrameBufferHandle> & shadowMaps)
{
  const bool hasShadows = !shadowPlan.passes.empty();
  uint32_t encodedLayer = 0;
  for (const CoinBgfxDraw & layerDraw : draws) {
    if (layerDraw.renderLayer == 0 || layerDraw.renderLayer == encodedLayer) continue;
    encodedLayer = layerDraw.renderLayer;
    if (nextView > this->viewBase + targetViewCount - 3) {
      this->lastError = "BGFX overlay layer count exceeds available view IDs";
      return false;
    }
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
      if (draw.renderLayer != encodedLayer) continue;
      bgfx::setTransform(draw.mvp);
      bgfx::setVertexBuffer(0, vertices);
      bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
      bgfx::setState(drawState(draw));
      if (!setDrawScissor(draw, width, height)) continue;
      this->bindDrawTexture(draw, textures);
      if (hasShadows) this->bindShadowReceiver(frame, shadowPlan, shadowMaps, draw, height);
      else this->bindDrawLighting(draw, height);
      bgfx::submit(overlayView, hasShadows ? (shadowPlan.passes.size() > 4 ? this->shadowReceiverProgram8 : shadowPlan.passes.size() > 2 ?
        this->shadowReceiverProgram4 : this->shadowReceiverProgram) : this->activeProgram);

    }
  }
  return true;
}

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

void
CoinBgfxBackend::bindShadowReceiver(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadowPlan,
  const std::vector<bgfx::FrameBufferHandle> & shadowMaps,
  const CoinBgfxDraw & original, int targetHeight)
{
  CoinBgfxDraw shaded = original;
  shaded.lightCount[1] = 0.0f;
  float indices[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
  const auto & state = frame.renderStates[original.renderStateSlot];
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
    const SbMatrix viewToLight = state.view.inverse() * pass.view;
    const SbMatrix projection = bgfx::getCaps()->homogeneousDepth ? pass.projectionCoin :
      pass.projectionCoin * clipConversion;
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
    bgfx::setTexture(static_cast<uint8_t>(8 + slot), this->shadowSampler[slot],
      bgfx::getTexture(shadowMaps[passSlot], 0));
  }
  bgfx::setUniform(this->shadowLightIndicesUniform, indices);
  bgfx::setUniform(this->shadowLightIndicesExtraUniform, indices + 4);
  this->bindDrawLighting(shaded, targetHeight);
}

CoinRenderSubmitResult
CoinBgfxBackend::submitInternal(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
  const CoinRenderFrameReuseDecision & reuse, CoinRenderReadbackTicket * outTicket)
{
  if (!this->initialized || this->status != CoinRenderBackendStatus::SUCCESS || !this->onApiThread()) {
    this->lastError = "BGFX submission requires a prepared backend on its API thread";
    return CoinRenderSubmitResult(CoinRenderBackendStatus::NOT_READY, this->lastError);
  }
  const CoinRenderBackendStatus initialRuntimeStatus = this->checkRuntimeFailure("BGFX shared renderer failed before submission");
  if (initialRuntimeStatus != CoinRenderBackendStatus::SUCCESS)
    return CoinRenderSubmitResult(initialRuntimeStatus, this->lastError);
  if ((target.kind == CoinRenderTargetP::KIND_WINDOW) != this->presentToWindow ||
      target.directTextureOutput) {
    this->lastError = "BGFX supports native window presentation or offscreen readback";
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  const bool hasShadows = !frame.shadowGroups.empty();
  CoinRenderShadowPlan shadowPlan;
  if (hasShadows) {
    if (this->presentToWindow || outTicket || target.directTextureOutput) {
      this->lastError = "BGFX opaque shadows require synchronous offscreen output";
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    }
    if (!coin_render_plan_shadows(frame, shadowPlan, this->lastError) ||
        !coin_render_shadow_object_profile(frame, shadowPlan,
          shadowPlan.passes.size(), this->lastError))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    if (!bgfxShadowBatchSupported(frame, shadowPlan, this->lastError))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    const bgfx::Caps * caps = bgfx::getCaps();
    if (caps->limits.maxTextureSamplers < (shadowPlan.passes.size() > 4 ? 16 : shadowPlan.passes.size() > 2 ? 12 : 10)) {
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
    if (!this->prepareShadowPrograms(shadowPlan.passes.size()))
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
  const bool instancingEnabled = !hasShadows && this->drawBatchingEnabled &&
    !(disableInstancing && std::strcmp(disableInstancing, "1") == 0) &&
    instanceCaps &&
    caps->limits.maxInstanceData >= 10 && caps->limits.maxVertexAttributes >= 12;
  const bool cacheDimensionsMatch =
    target.size[0] == this->cachedWidth &&
    target.size[1] == this->cachedHeight &&
    homogeneousDepth == this->cachedHomogeneousDepth;
  const bool cacheHit = !hasShadows && frame.revision != 0 &&
    frame.revision == this->cachedRevision && cacheDimensionsMatch &&
    (!this->cachedPlan.usesInstancing || instancingEnabled);
  const char * disableInstancedCameraPatch = std::getenv("COIN_BGFX_DISABLE_INSTANCED_CAMERA_PATCH");
  const bool instancedCameraPatchEnabled = instancingEnabled &&
    !(disableInstancedCameraPatch && std::strcmp(disableInstancedCameraPatch, "1") == 0) &&
    bgfx::isValid(this->cachedInstanceBuffer);
  const bool cameraPatchEligible =
    !hasShadows && this->cameraPatchEnabled &&
    (!this->cachedPlan.usesInstancing || instancedCameraPatchEnabled) &&
    reuse.kind == CoinRenderFrameReuseKind::CAMERA_PATCH &&
    reuse.baseRevision != 0 && reuse.baseRevision == this->cachedRevision &&
    frame.revision != 0 && frame.revision != this->cachedRevision &&
    cacheDimensionsMatch &&
    (this->cachedPlan.draws.empty() ||
      (bgfx::isValid(this->cachedVertexBuffer) &&
       bgfx::isValid(this->cachedIndexBuffer)));
  std::vector<CoinBgfxDraw> cameraDraws;
  const bool cameraPatchUsed = cameraPatchEligible &&
    CoinBgfxLowering::patchCamera(frame, target.size[0], target.size[1],
                                homogeneousDepth, this->cachedPlan,
                                cameraDraws, this->lastError);
  if (tracePhases && cameraPatchEligible && this->cachedPlan.usesInstancing && !cameraPatchUsed)
    std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_instanced_camera_patch_declined reason=%s\n", this->lastError.c_str());
  CoinBgfxPlan freshPlan;
  const CoinBgfxPlan * plan = &this->cachedPlan;
  if (!cacheHit && !cameraPatchUsed) {
    const char * disableCompact = std::getenv("COIN_BGFX_DISABLE_COMPACT_VERTICES");
    const bool compact = bgfx::getCaps()->rendererType == bgfx::RendererType::OpenGL &&
      !(disableCompact && std::strcmp(disableCompact, "1") == 0);
    const bool instanced = instancingEnabled && CoinBgfxLowering::lowerInstanced(
      frame, target.size[0], target.size[1], homogeneousDepth, freshPlan,
      this->lastError, target.submissionPreflight(frame));
    if (!instanced && CoinRenderDiagnosticShell::phaseTracingEnabled())
      std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_instancing_declined enabled=%d max_instance_data=%u max_vertex_attributes=%u reason=%s\n",
        instancingEnabled ? 1 : 0, static_cast<unsigned int>(caps->limits.maxInstanceData),
        static_cast<unsigned int>(caps->limits.maxVertexAttributes), this->lastError.c_str());
    if (!instanced && !CoinBgfxLowering::lower(frame, target.size[0], target.size[1],
                              homogeneousDepth, freshPlan, this->lastError, hasShadows,
                              this->drawBatchingEnabled, target.submissionPreflight(frame), compact)) {
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    }
    plan = &freshPlan;
  }
  std::vector<CoinBgfxVertexRange> materialRanges;
  const bool materialPatchEligible = !cacheHit && !cameraPatchUsed &&
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
  if (!this->prepareTransparencyPrograms(selectedStrategy))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  if (!this->prepareBaseProgram(strategyDraws, hasShadows, selectedStrategy, plan->usesInstancing))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  if (hasShadows && selectedStrategy != CoinBgfxTransparencyStrategy::OBJECT &&
      !this->prepareShadowTransparencyPrograms())
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  CoinRenderTransparencyOptions allocationOptions = frame.transparency;
  if (selectedStrategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT)
    allocationOptions.layers = 1;
  uint64_t requiredBytes = 0;
  if (!coin_render_transparency_budget(target.size[0], target.size[1], allocationOptions,
                                       selectedStrategy != CoinBgfxTransparencyStrategy::OBJECT,
                                       requiredBytes, this->lastError))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  if (selectedStrategy != this->activeTransparencyStrategy ||
      (selectedStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
       this->peelPassCount != frame.transparency.layers)) {
    // Keep the native surface when opaque-only frames choose OBJECT or later
    // frames require OIT/peeling. BGFX creates resources before retiring old
    // ones: recreating a swapchain on the same X window here would race its
    // old EGL surface and fail with EGL_BAD_ALLOC.
    const bgfx::FrameBufferHandle windowOutput = this->frameBuffer;
    if (this->presentToWindow) this->frameBuffer = BGFX_INVALID_HANDLE;
    this->destroyFrameBuffers();
    if (this->presentToWindow) this->frameBuffer = windowOutput;
    this->activeTransparencyStrategy = selectedStrategy;
    this->peelPassCount = frame.transparency.layers;
  }
  const Clock::time_point lowered = Clock::now();
  if (!this->resize(target.size[0], target.size[1])) {
    const CoinRenderBackendStatus resizeStatus = this->status == CoinRenderBackendStatus::SUCCESS ?
      CoinRenderBackendStatus::OUT_OF_MEMORY : this->status;
    return CoinRenderSubmitResult(resizeStatus, this->lastError);
  }
  if (!this->presentToWindow && target.depthReadbackEnabled && !this->prepareDepthReadbackResources())
    return CoinRenderSubmitResult(this->status, this->lastError);
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
    this->cachedWidth = target.size[0];
    this->cachedHeight = target.size[1];
    this->cachedHomogeneousDepth = homogeneousDepth;
    plan = &this->cachedPlan;
  }
  if (!retained) {
    if (!plan->draws.empty()) {
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
    for (bgfx::TextureHandle texture : this->cachedTextures)
      if (bgfx::isValid(texture)) bgfx::destroy(texture);
    this->cachedTextures.clear();
    this->cachedRevision = 0;
    if (frame.revision != 0 && CoinBgfxLowering::retainForReuse(freshPlan)) {
      this->cachedPlan = std::move(freshPlan);
      this->cachedRevision = frame.revision;
      this->cachedWidth = target.size[0];
      this->cachedHeight = target.size[1];
      this->cachedHomogeneousDepth = homogeneousDepth;
      plan = &this->cachedPlan;
      retained = true;
    }
  }

  std::vector<bgfx::TextureHandle> textures(
    plan->textures.size(), BGFX_INVALID_HANDLE);
  bool hasDirectTextures = false;
  for (const CoinBgfxTexture & texture : plan->textures)
    if (texture.gpuToken != 0) hasDirectTextures = true;
  std::vector<bool> textureOwned(plan->textures.size(), false);
  bool textureCacheHit = !hasDirectTextures &&
    (cacheHit || cameraPatchUsed || materialPatchUsed) &&
    this->cachedTextures.size() == plan->textures.size();
  for (bgfx::TextureHandle texture : this->cachedTextures) {
    if (!bgfx::isValid(texture)) textureCacheHit = false;
  }
  if (textureCacheHit) textures = this->cachedTextures;
  for (size_t textureIndex = 0;
       !textureCacheHit && textureIndex < plan->textures.size(); ++textureIndex) {
    const CoinBgfxTexture & texture = plan->textures[textureIndex];
    if (texture.gpuToken != 0) {
      for (const DirectTextureResource & resource : this->directTextures) {
        if (resource.token == texture.gpuToken) {
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
          static_cast<size_t>(texture.width) * texture.height * 4u) {
      this->lastError = "BGFX texture dimensions or RGBA payload are invalid";
      for (size_t i = 0; i < textures.size(); ++i)
        if (textureOwned[i] && bgfx::isValid(textures[i])) bgfx::destroy(textures[i]);
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    }
    textures[textureIndex] = bgfx::createTexture2D(
      static_cast<uint16_t>(texture.width),
      static_cast<uint16_t>(texture.height), false, 1,
      bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
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

  const Clock::time_point uploaded = Clock::now();
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
      const bgfx::ViewId mapView = this->viewBase + static_cast<bgfx::ViewId>(slot);
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
      for (const CoinBgfxDraw & draw : plan->shadowDraws) {
        if (std::find(pass.casterDraws.begin(), pass.casterDraws.end(),
                      draw.sourceDrawSlot) == pass.casterDraws.end()) continue;
        const auto & state = frame.renderStates[draw.renderStateSlot];
        const SbMatrix modelView = state.model * pass.view;
        const SbMatrix mvp = modelView * projection;
        bgfx::setTransform(mvp.getValue());
        bgfx::setVertexBuffer(0, vb);
        bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
        bgfx::setUniform(this->shadowModelViewUniform, modelView.getValue());
        const SbMatrix clipModelView = state.model * state.view;
        bgfx::setUniform(this->shadowClipModelViewUniform, clipModelView.getValue());
        bgfx::setUniform(this->clipMetaUniform, draw.clipMeta);
        bgfx::setUniform(this->clipPlanesUniform, draw.clipPlanes, COIN_RENDER_MAX_CLIP_PLANES);
        bgfx::setUniform(this->shadowDepthUniform, depthParams);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
          BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
        bgfx::submit(mapView, this->shadowMomentsProgram);
      }
    }
  }
  const bgfx::ViewId opaqueView = this->viewBase + (hasShadows ? static_cast<bgfx::ViewId>(shadowPlan.passes.size()) : 0);
  bgfx::setViewName(opaqueView, "opaque");
  bgfx::setViewMode(opaqueView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(opaqueView, 0, 0, static_cast<uint16_t>(this->width),
                    static_cast<uint16_t>(this->height));
  const bgfx::FrameBufferHandle windowFrameBuffer = this->frameBuffer;
  bgfx::setViewFrameBuffer(opaqueView, this->presentToWindow ? windowFrameBuffer : this->frameBuffer);
  // The packed clear API quantizes Coin's float color before the GL clear.
  // Keep the float until the renderer converts it to its target format.
  bgfx::setPaletteColor(0, plan->clearColor);
  bgfx::setViewClear(opaqueView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.0f, 0, 0);
  bgfx::setViewTransform(opaqueView, nullptr, nullptr);
  bgfx::touch(opaqueView);
  const std::vector<CoinBgfxDraw> & sourceDraws =
    cameraPatchUsed ? cameraDraws : plan->draws;
  std::vector<CoinBgfxDraw> groupedDraws;
  const bool groupDraws = this->drawGroupingEnabled && !hasShadows && !plan->usesInstancing;
  if (groupDraws)
    CoinBgfxLowering::groupOpaqueDraws(sourceDraws, groupedDraws);
  const std::vector<CoinBgfxDraw> & draws =
    groupDraws ? groupedDraws : sourceDraws;
  const LogicalDrawStats drawStats = logicalDrawStats(sourceDraws, draws);
  const bool useSortedLayers =
    selectedStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS;
  const bool useWeightedOit =
    selectedStrategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT;
  bgfx::ViewId nextView = opaqueView + 2;
  for (const CoinBgfxDraw & draw : draws) {
    if (draw.renderLayer != 0 || (draw.blend && draw.deferred)) continue;
    // A skipped draw has no submit to discard its instance binding. Cull it
    // before encoding any geometry so later fullscreen passes stay ordinary.
    if (!setDrawScissor(draw, this->width, this->height)) continue;
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vb);
    bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
    if (plan->usesInstancing)
      bgfx::setInstanceDataBuffer(instanceBuffer, draw.firstInstance, draw.instanceCount);
    bgfx::setState(drawState(draw));
    this->bindDrawTexture(draw, textures);
    if (hasShadows) this->bindShadowReceiver(frame, shadowPlan, shadowMaps.buffers, draw, this->height);
    else this->bindDrawLighting(draw);
    bgfx::submit(opaqueView, hasShadows ? (shadowPlan.passes.size() > 4 ? this->shadowReceiverProgram8 : shadowPlan.passes.size() > 2 ?
      this->shadowReceiverProgram4 : this->shadowReceiverProgram) : this->activeProgram);

  }

  if (!useSortedLayers && !useWeightedOit) {
    const bgfx::ViewId transparentView = opaqueView + 1;
    bgfx::setViewName(transparentView, "transparent_accumulation");
    bgfx::setViewMode(transparentView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(transparentView, 0, 0,
                      static_cast<uint16_t>(this->width),
                      static_cast<uint16_t>(this->height));
    bgfx::setViewFrameBuffer(transparentView,
      this->presentToWindow ? windowFrameBuffer : this->frameBuffer);
    bgfx::setViewClear(transparentView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(transparentView, nullptr, nullptr);
    for (const CoinBgfxDraw & draw : draws) {
      if (draw.renderLayer != 0 || !draw.blend || !draw.deferred) continue;
      bgfx::setTransform(draw.mvp);
      bgfx::setVertexBuffer(0, vb);
      bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
      bgfx::setState(drawState(draw));
      if (!setDrawScissor(draw, this->width, this->height)) continue;
      this->bindDrawTexture(draw, textures);
      if (hasShadows) this->bindShadowReceiver(frame, shadowPlan, shadowMaps.buffers, draw, this->height);
      else this->bindDrawLighting(draw);
      bgfx::submit(transparentView, hasShadows ? (shadowPlan.passes.size() > 4 ? this->shadowReceiverProgram8 : shadowPlan.passes.size() > 2 ?
        this->shadowReceiverProgram4 : this->shadowReceiverProgram) : this->activeProgram);
    }
  } else if (useSortedLayers) {
    nextView = opaqueView + this->peelPassCount + 2;
    this->encodeSortedLayers(draws, vb, ib,
      this->presentToWindow ? windowFrameBuffer : this->frameBuffer,
      textures, opaqueView + 1, this->width, this->height, frame, shadowPlan, shadowMaps.buffers,
      std::vector<bgfx::FrameBufferHandle>(this->peelFrameBuffers, this->peelFrameBuffers + this->peelPassCount), this->oitFrameBuffer);
  } else if (useWeightedOit) {
    nextView = opaqueView + 3;
    this->encodeWeightedOit(draws, vb, ib,
      this->presentToWindow ? windowFrameBuffer : this->frameBuffer,
      textures, opaqueView + 1, this->width, this->height, frame, shadowPlan, shadowMaps.buffers,
      {}, this->oitFrameBuffer);
  }
  if (useSortedLayers || useWeightedOit) {
    bool hasAdditive = false;
    for (const auto & draw : draws) hasAdditive = hasAdditive ||
      (draw.renderLayer == 0 && draw.blend && draw.deferred && draw.additive);
    if (hasAdditive) {
      bgfx::setViewName(nextView, "additive_transparency");
      bgfx::setViewMode(nextView, bgfx::ViewMode::Sequential);
      bgfx::setViewRect(nextView, 0, 0, this->width, this->height);
      bgfx::setViewFrameBuffer(nextView, this->frameBuffer);
      bgfx::setViewClear(nextView, BGFX_CLEAR_NONE);
      bgfx::setViewTransform(nextView, nullptr, nullptr);
      for (const auto & draw : draws) {
        if (draw.renderLayer != 0 || !draw.blend || !draw.deferred || !draw.additive) continue;
        bgfx::setTransform(draw.mvp); bgfx::setVertexBuffer(0, vb);
        bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
        bgfx::setState(drawState(draw));
        if (!setDrawScissor(draw, this->width, this->height)) continue;
        this->bindDrawTexture(draw, textures);
        if (hasShadows) this->bindShadowReceiver(frame, shadowPlan, shadowMaps.buffers, draw, this->height);
        else this->bindDrawLighting(draw);
        bgfx::submit(nextView, hasShadows ? (shadowPlan.passes.size() > 4 ? this->shadowReceiverProgram8 :
          shadowPlan.passes.size() > 2 ? this->shadowReceiverProgram4 : this->shadowReceiverProgram) : this->activeProgram);
      }
      ++nextView;
    }
  }
  if (!this->encodeOverlayLayers(draws, vb, ib,
        this->presentToWindow ? windowFrameBuffer : this->frameBuffer,
        textures, nextView, this->width, this->height, frame, shadowPlan, shadowMaps.buffers)) {
    destroyTextures();
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  const Clock::time_point drawsEncoded = Clock::now();
  if (consumeTestFault("COIN_BGFX_TEST_DEVICE_LOST_ON_SUBMIT_ONCE")) {
    static_cast<CoinBgfxCallback *>(this->callback.get())->inject(
      bgfx::Fatal::DeviceLost, "injected device loss during submission");
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
    runtimeStatus = this->checkRuntimeFailure("BGFX frame submission failed");
    if (runtimeStatus != CoinRenderBackendStatus::SUCCESS) {
      screenshotCallback->cancelScreenshot();
      return CoinRenderSubmitResult(runtimeStatus, this->lastError);
    }
    const Clock::time_point submitted = Clock::now();
    if (target.windowReadbackRequested &&
        !screenshotCallback->takeScreenshot(target.colorBuffer)) {
      this->lastError = "BGFX window screenshot unavailable or incomplete";
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
      std::fprintf(stderr, "%s\n", CoinRenderDiagnosticShell::formatBgfxPhase(sample).c_str());
    }
    this->lastError.clear();
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
    if (target.depthReadbackEnabled && bgfx::getCaps()->originBottomLeft)
      for (int y = 0; y < this->height / 2; ++y)
        std::swap_ranges(oldest->depth.begin() + y * this->width, oldest->depth.begin() + (y + 1) * this->width,
                         oldest->depth.begin() + (this->height - y - 1) * this->width);
    this->lastPublishedDepth = oldest->depth;
    this->lastPublishedReadback = oldest->pixels;
    this->lastPublishedSequence = oldest->sequence;
    oldest->pending = false;
    writeSlot = oldest;
  }
  if (target.depthReadbackEnabled) {
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
    if (consumeTestFault("COIN_BGFX_TEST_DEVICE_LOST_AFTER_ASYNC_ONCE"))
      static_cast<CoinBgfxCallback*>(this->callback.get())
          ->inject(bgfx::Fatal::DeviceLost, "injected device loss after async ticket allocation");
    const auto asyncStatus = this->checkRuntimeFailure("BGFX asynchronous submission failed");
    // Return the private allocation on failure so the common owner can cancel it.
    *outTicket = ticket;
    if (asyncStatus == CoinRenderBackendStatus::SUCCESS) {
      target.colorBuffer.clear();
      target.depthBuffer.clear();
      target.needsReconfigure = false;
    }
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
  ReadbackSlot * publishSlot = NULL;
  const auto selectReady = [&]() {
    ReadbackSlot * selected = static_cast<ReadbackSlot *>(NULL);
    for (ReadbackSlot & slot : this->readbackSlots) {
      if (slot.pending && slot.readyFrame <= completedFrame &&
          (!selected || slot.sequence < selected->sequence)) selected = &slot;
    }
    return selected;
  };
  publishSlot = selectReady();
  bool readbackBootstrap = false;
  if (!publishSlot && (this->readbackPipelineDepth == 1 ||
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
  if (!publishSlot && (this->readbackPipelineDepth == 1 || this->lastPublishedReadback.empty())) {
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
    if (target.depthReadbackEnabled && bgfx::getCaps()->originBottomLeft)
      for (int y = 0; y < this->height / 2; ++y)
        std::swap_ranges(publishSlot->depth.begin() + y * this->width, publishSlot->depth.begin() + (y + 1) * this->width,
                         publishSlot->depth.begin() + (this->height - y - 1) * this->width);
    if (this->readbackPipelineDepth == 1) {
      // The synchronous contract publishes this frame before returning. Its
      // target storage can be recycled without maintaining a duplicate image.
      target.colorBuffer.swap(publishSlot->pixels);
      if (target.depthReadbackEnabled) target.depthBuffer.swap(publishSlot->depth);
    } else {
      this->lastPublishedDepth = publishSlot->depth;
      this->lastPublishedReadback = publishSlot->pixels;
    }
    this->lastPublishedSequence = publishSlot->sequence;
    publishSlot->pending = false;
  }
  if (this->readbackPipelineDepth != 1) target.colorBuffer = this->lastPublishedReadback;
  readbackLatencyFrames = static_cast<uint32_t>(
    this->readbackSequence - this->lastPublishedSequence);
  const Clock::time_point readbackNormalized = Clock::now();
  if (cameraPatchUsed) {
    this->cachedPlan.draws.swap(cameraDraws);
    this->cachedRevision = frame.revision;
  }
  if (target.depthReadbackEnabled && this->readbackPipelineDepth != 1)
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
    std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_geometry compact_vertices=%d vertex_stride=%u instancing=%d instances=%zu instance_stride=%u instance_buffer_capacity=%u\n",
      plan->usesCompactVertices ? 1 : 0,
      static_cast<unsigned int>(plan->usesInstancing ? sizeof(CoinBgfxInstancedVertex) :
        plan->usesCompactVertices ? sizeof(CoinBgfxVertexPrefix) : sizeof(CoinBgfxVertex)),
      plan->usesInstancing ? 1 : 0, plan->instances.size(),
      static_cast<unsigned int>(sizeof(CoinBgfxInstance)), this->cachedInstanceCapacity);
    std::fprintf(stderr, "%s\n", CoinRenderDiagnosticShell::formatBgfxPhase(sample).c_str());
  }
  this->lastError.clear();
  return CoinRenderSubmitResult(CoinRenderBackendStatus::SUCCESS, "", ++this->serial);
}

CoinRenderDeviceDomain CoinBgfxBackend::resourceDomain() const {
  if (!this->initialized || !this->onApiThread() ||
      static_cast<CoinBgfxCallback*>(this->callback.get())->failed())
    return {};
  CoinRenderDeviceDomain result;
  result.device = 1; // One BGFX runtime per process; generation distinguishes recreation.
  result.generation = sharedRuntime().generation;
  return result;
}

CoinRenderSubmitResult
CoinBgfxBackend::submitDirectTexture(const CoinRenderFramePlan & frame,
                                       const SbVec2i32 & size,
                                       uint64_t producerKey,
                                       uint64_t & token)
{
  token = 0;
  if (!this->initialized || !this->onApiThread() || this->presentToWindow ||
      producerKey == 0 || size[0] <= 0 || size[1] <= 0 ||
      size[0] > 2048 || size[1] > 2048) {
    return CoinRenderSubmitResult(CoinRenderBackendStatus::NOT_READY,
                        "BGFX direct RTT requires a prepared offscreen backend");
  }
  const CoinRenderBackendStatus initialRuntimeStatus = this->checkRuntimeFailure("BGFX shared renderer failed before direct RTT");
  if (initialRuntimeStatus != CoinRenderBackendStatus::SUCCESS)
    return CoinRenderSubmitResult(initialRuntimeStatus, this->lastError);
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
    if (caps->limits.maxTextureSamplers < (shadowPlan.passes.size() > 4 ? 16 : shadowPlan.passes.size() > 2 ? 12 : 10))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED,
        "BGFX direct RTT shadow receiver has insufficient texture stages");
    for (const auto & pass : shadowPlan.passes)
      if (pass.mapSize > caps->limits.maxTextureSize ||
          !(caps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
          !(caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
          !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA32F, shadowTextureFlags))
        return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED,
          "BGFX direct RTT cannot render the planned shadow maps");
    if (!this->prepareShadowPrograms(shadowPlan.passes.size()))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  }
  CoinBgfxPlan plan;
  if (!CoinBgfxLowering::lower(frame, size[0], size[1],
        bgfx::getCaps()->homogeneousDepth, plan, this->lastError, hasShadows)) {
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  CoinBgfxTransparencyStrategy strategy;
  if (!CoinBgfxLowering::selectTransparencyStrategy(plan.draws,
        CoinBgfxTransparencyMode::AUTO, this->weightedOitSupported,
        this->sortedLayersSupported, strategy, this->lastError))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  if (!this->prepareTransparencyPrograms(strategy))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  if (!this->prepareBaseProgram(plan.draws, hasShadows, strategy))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  if (hasShadows && strategy != CoinBgfxTransparencyStrategy::OBJECT &&
      !this->prepareShadowTransparencyPrograms())
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  auto allocationOptions = frame.transparency;
  if (strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT) allocationOptions.layers = 1;
  uint64_t requiredBytes = 0;
  if (!coin_render_transparency_budget(size[0], size[1], allocationOptions,
        strategy != CoinBgfxTransparencyStrategy::OBJECT, requiredBytes, this->lastError))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
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
    attachments[0] = bgfx::createTexture2D(size[0], size[1], false, 1,
      bgfx::TextureFormat::RGBA16F, peelTextureFlags);
    if (weighted) attachments[1] = bgfx::createTexture2D(size[0], size[1], false, 1,
      bgfx::TextureFormat::R16F, peelTextureFlags);
    const unsigned depthSlot = weighted ? 2 : 1;
    attachments[depthSlot] = bgfx::createTexture2D(size[0], size[1], false, 1,
      bgfx::TextureFormat::D32F, peelTextureFlags);
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

  DirectTextureResource * cachedResource = NULL;
  for (DirectTextureResource & resource : this->directTextures) {
    if (resource.producerKey == producerKey && !resource.inUse) {
      cachedResource = &resource;
      break;
    }
  }
  if (cachedResource &&
      (cachedResource->width != size[0] || cachedResource->height != size[1])) {
    if (bgfx::isValid(cachedResource->frameBuffer))
      bgfx::destroy(cachedResource->frameBuffer);
    cachedResource->frameBuffer = BGFX_INVALID_HANDLE;
  }
  bgfx::FrameBufferHandle output = BGFX_INVALID_HANDLE;
  if (cachedResource) output = cachedResource->frameBuffer;
  const bool newOutput = !bgfx::isValid(output);
  if (newOutput) {
    const bgfx::Caps * caps = bgfx::getCaps();
    const bgfx::TextureFormat::Enum depthFormat =
      (caps->formats[bgfx::TextureFormat::D24S8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)
        ? bgfx::TextureFormat::D24S8 : bgfx::TextureFormat::D32F;
    bgfx::TextureHandle color = bgfx::createTexture2D(
      static_cast<uint16_t>(size[0]), static_cast<uint16_t>(size[1]), false, 1,
      bgfx::TextureFormat::RGBA8, peelTextureFlags);
    bgfx::TextureHandle depth = bgfx::createTexture2D(
      static_cast<uint16_t>(size[0]), static_cast<uint16_t>(size[1]), false, 1,
      depthFormat, BGFX_TEXTURE_RT_WRITE_ONLY);
    const bgfx::TextureHandle attachments[2] = {color, depth};
    output = bgfx::createFrameBuffer(2, attachments, true);
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

  bgfx::DynamicVertexBufferHandle vb = BGFX_INVALID_HANDLE;
  bgfx::DynamicIndexBufferHandle ib = BGFX_INVALID_HANDLE;
  if (!plan.draws.empty()) {
    vb = bgfx::createDynamicVertexBuffer(bgfx::copy(plan.vertices.data(),
      static_cast<uint32_t>(plan.vertices.size() * sizeof(CoinBgfxVertex))),
      this->layout);
    ib = bgfx::createDynamicIndexBuffer(bgfx::copy(plan.indices.data(),
      static_cast<uint32_t>(plan.indices.size() * sizeof(uint32_t))),
      BGFX_BUFFER_INDEX32);
  }
  if ((!plan.draws.empty()) && (!bgfx::isValid(vb) || !bgfx::isValid(ib))) {
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
    if (source.gpuToken != 0) {
      for (const DirectTextureResource & resource : this->directTextures) {
        if (resource.token == source.gpuToken)
          textures[i] = bgfx::getTexture(resource.frameBuffer, 0);
      }
    } else {
      textures[i] = bgfx::createTexture2D(static_cast<uint16_t>(source.width),
        static_cast<uint16_t>(source.height), false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_NONE, bgfx::copy(source.pixelsRgba.data(),
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

  const auto cleanupDirect = [&]() {
    for (size_t i = 0; i < textures.size(); ++i)
      if (owned[i] && bgfx::isValid(textures[i])) bgfx::destroy(textures[i]);
    if (bgfx::isValid(vb)) bgfx::destroy(vb);
    if (bgfx::isValid(ib)) bgfx::destroy(ib);
    if (newOutput) bgfx::destroy(output);
  };
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
      const bgfx::ViewId mapView = this->viewBase + static_cast<bgfx::ViewId>(slot);
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
      for (const CoinBgfxDraw & draw : plan.shadowDraws) {
        if (std::find(pass.casterDraws.begin(), pass.casterDraws.end(),
                      draw.sourceDrawSlot) == pass.casterDraws.end()) continue;
        const auto & state = frame.renderStates[draw.renderStateSlot];
        const SbMatrix modelView = state.model * pass.view;
        const SbMatrix mvp = modelView * projection;
        bgfx::setTransform(mvp.getValue());
        bgfx::setVertexBuffer(0, vb);
        bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
        bgfx::setUniform(this->shadowModelViewUniform, modelView.getValue());
        const SbMatrix clipModelView = state.model * state.view;
        bgfx::setUniform(this->shadowClipModelViewUniform, clipModelView.getValue());
        bgfx::setUniform(this->clipMetaUniform, draw.clipMeta);
        bgfx::setUniform(this->clipPlanesUniform, draw.clipPlanes, COIN_RENDER_MAX_CLIP_PLANES);
        bgfx::setUniform(this->shadowDepthUniform, depthParams);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
          BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
        bgfx::submit(mapView, this->shadowMomentsProgram);
      }
    }
  }
  const bgfx::ViewId opaqueView = this->viewBase + static_cast<bgfx::ViewId>(shadowPlan.passes.size());
  bgfx::setPaletteColor(0, plan.clearColor);
  bgfx::setViewName(opaqueView, "rtt_opaque");
  bgfx::setViewMode(opaqueView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(opaqueView, 0, 0, static_cast<uint16_t>(size[0]),
                    static_cast<uint16_t>(size[1]));
  bgfx::setViewFrameBuffer(opaqueView, output);
  bgfx::setViewClear(opaqueView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.0f, 0, 0);
  bgfx::touch(opaqueView);
  for (const CoinBgfxDraw & draw : plan.draws) {
    if (draw.renderLayer != 0 || (draw.blend && draw.deferred)) continue;
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vb);
    bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
    bgfx::setState(drawState(draw));
    if (!setDrawScissor(draw, size[0], size[1])) continue;
    this->bindDrawTexture(draw, textures);
    if (hasShadows) this->bindShadowReceiver(frame, shadowPlan, shadowMaps.buffers, draw, size[1]);
    else this->bindDrawLighting(draw, size[1]);
    bgfx::submit(opaqueView, hasShadows ? (shadowPlan.passes.size() > 4 ? this->shadowReceiverProgram8 : shadowPlan.passes.size() > 2 ?
      this->shadowReceiverProgram4 : this->shadowReceiverProgram) : this->activeProgram);
  }

  bgfx::ViewId nextView = opaqueView + 1;
  if (strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS) {
    this->encodeSortedLayers(plan.draws, vb, ib, output, textures, nextView,
      size[0], size[1], frame, shadowPlan, shadowMaps.buffers, transparency.layers, transparency.oit);
    nextView += frame.transparency.layers + 1;
  } else if (strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT) {
    this->encodeWeightedOit(plan.draws, vb, ib, output, textures, nextView,
      size[0], size[1], frame, shadowPlan, shadowMaps.buffers, {}, transparency.oit);
    nextView += 2;
  }
  const bgfx::ViewId transparentView = nextView++;
  bgfx::setViewName(transparentView, "rtt_transparent");
  bgfx::setViewMode(transparentView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(transparentView, 0, 0, static_cast<uint16_t>(size[0]),
                    static_cast<uint16_t>(size[1]));
  bgfx::setViewFrameBuffer(transparentView, output);
  bgfx::setViewClear(transparentView, BGFX_CLEAR_NONE);
  for (const CoinBgfxDraw & draw : plan.draws) {
    if (draw.renderLayer != 0 || !draw.blend || !draw.deferred ||
        (strategy != CoinBgfxTransparencyStrategy::OBJECT && !draw.additive)) continue;
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vb);
    bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
    bgfx::setState(drawState(draw));
    if (!setDrawScissor(draw, size[0], size[1])) continue;
    this->bindDrawTexture(draw, textures);
    if (hasShadows) this->bindShadowReceiver(frame, shadowPlan, shadowMaps.buffers, draw, size[1]);
    else this->bindDrawLighting(draw, size[1]);
    bgfx::submit(transparentView, hasShadows ? (shadowPlan.passes.size() > 4 ? this->shadowReceiverProgram8 : shadowPlan.passes.size() > 2 ?
      this->shadowReceiverProgram4 : this->shadowReceiverProgram) : this->activeProgram);
  }

  if (!this->encodeOverlayLayers(plan.draws, vb, ib, output, textures, nextView,
                                size[0], size[1], frame, shadowPlan, shadowMaps.buffers)) {
    cleanupDirect();
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  bgfx::frame();
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
    resource.frameBuffer = output;
    resource.inUse = true;
    this->directTextures.push_back(resource);
    token = resource.token;
  } else {
    cachedResource->width = size[0];
    cachedResource->height = size[1];
    cachedResource->frameBuffer = output;
    cachedResource->inUse = true;
    token = cachedResource->token;
  }
  return CoinRenderSubmitResult(CoinRenderBackendStatus::SUCCESS, "", ++this->serial);
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
  if (this->checkRuntimeFailure("BGFX shared renderer failed before RTT retirement") !=
      CoinRenderBackendStatus::SUCCESS) return;
  for (std::vector<DirectTextureResource>::iterator it = this->directTextures.begin();
       it != this->directTextures.end();) {
    if (std::find(usedTokens.begin(), usedTokens.end(), it->token) == usedTokens.end()) {
      if (bgfx::isValid(it->frameBuffer)) bgfx::destroy(it->frameBuffer);
      it = this->directTextures.erase(it);
    } else {
      it->inUse = false;
      ++it;
    }
  }
}

CoinRenderTarget::ReadbackStatus
CoinBgfxBackend::pollReadback(const CoinRenderReadbackTicket & ticket,
  std::vector<uint8_t> & color, std::vector<float> & depth, SbString * diagnostic)
{
  auto & runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
  auto lost = lostReadbacks.find(ticket.token);
  if (lost != lostReadbacks.end() && sameTicket(lost->second.ticket, ticket)) {
    if (lost->second.thread != std::this_thread::get_id())
      return CoinRenderTarget::READBACK_ERROR;
    if (diagnostic)
      *diagnostic = "Readback belongs to a retired device generation";
    return CoinRenderTarget::READBACK_DEVICE_LOST;
  }
  auto found = asyncEntries().find(ticket.token);
  if (found == asyncEntries().end() || !sameTicket(found->second->ticket, ticket)) {
    if (diagnostic) *diagnostic = "Invalid or consumed BGFX readback ticket";
    return CoinRenderTarget::READBACK_INVALID_TICKET;
  }
  if (runtime.apiThread != std::this_thread::get_id()) {
    if (diagnostic) *diagnostic = "BGFX readback must be polled on its API thread";
    return CoinRenderTarget::READBACK_ERROR;
  }
  auto * cb = static_cast<CoinBgfxCallback *>(runtime.callback.get());
  if (cb->failed()) {
    if (diagnostic) *diagnostic = cb->diagnostic().c_str();
    return cb->deviceLost() ? CoinRenderTarget::READBACK_DEVICE_LOST : CoinRenderTarget::READBACK_ERROR;
  }
  const uint32_t completed = bgfx::frame();
  auto & entry = *found->second;
  if (static_cast<int32_t>(completed - entry.slot.readyFrame) < 0)
    return CoinRenderTarget::READBACK_NOT_READY;
  if (entry.bottomLeft) {
    CoinRenderImageCore::flipRgba8Rows(entry.slot.pixels, SbVec2i32(ticket.width, ticket.height));
    for (uint32_t y = 0; !entry.slot.depth.empty() && y < ticket.height / 2; ++y)
      std::swap_ranges(entry.slot.depth.begin() + y * ticket.width,
        entry.slot.depth.begin() + (y + 1) * ticket.width,
        entry.slot.depth.begin() + (ticket.height - y - 1) * ticket.width);
  }
  color.swap(entry.slot.pixels); depth.swap(entry.slot.depth);
  releaseAsync(ticket.token);
  return CoinRenderTarget::READBACK_READY;
}

bool
CoinBgfxBackend::cancelReadback(const CoinRenderReadbackTicket & ticket)
{
  auto & runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
  auto lost = lostReadbacks.find(ticket.token);
  if (lost != lostReadbacks.end() && sameTicket(lost->second.ticket, ticket)) {
    if (lost->second.thread != std::this_thread::get_id())
      return false;
    lostReadbacks.erase(lost);
    return true;
  }
  auto found = asyncEntries().find(ticket.token);
  if (found == asyncEntries().end() || !sameTicket(found->second->ticket, ticket) ||
      runtime.apiThread != std::this_thread::get_id()) return false;
  auto * cb = static_cast<CoinBgfxCallback *>(runtime.callback.get());
  if (cb->failed()) {
    // Shutdown is the only safe retirement fence after device loss.
    releaseAsync(ticket.token, true);
    return true;
  }
  uint32_t completed = bgfx::frame();
  for (int attempts = 0; static_cast<int32_t>(completed - found->second->slot.readyFrame) < 0 && attempts < 16; ++attempts)
    completed = bgfx::frame();
  if (static_cast<int32_t>(completed - found->second->slot.readyFrame) < 0) return false;
  releaseAsync(ticket.token);
  return true;
}

bool CoinBgfxBackend::readbackLoad(uint64_t& jobs, uint64_t& bytes) const {
  auto& runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
  jobs = asyncEntries().size();
  bytes = 0;
  for (const auto& item : asyncEntries())
    bytes += item.second->ticket.colorBytes + item.second->ticket.depthBytes;
  return true;
}
void CoinBgfxBackend::retireLostReadbacks() {
  auto& runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
  while (!asyncEntries().empty()) {
    const auto entry = asyncEntries().begin()->second;
    lostReadbacks.emplace(entry->ticket.token, LostReadback{entry->ticket, runtime.apiThread});
    releaseAsync(entry->ticket.token, true);
  }
}
void
CoinBgfxBackend::poll()
{
  // Synchronous readback in submit() already advances BGFX frames.
}

CoinRenderSubmitResult CoinBgfxBackend::preflightRtt(const CoinRenderRttPlan& graph,
                                                     const CoinRenderFramePlan&,
                                                     const SbVec2i32&) const {
  if (graph.mode != COIN_RENDER_SCENE_TEXTURE_DIRECT)
    return {};
  for (const auto& producer : graph.producers) {
    if (!producer.plan.shadowGroups.empty()) {
      CoinRenderShadowPlan shadowPlan;
      std::string diagnostic;
      if (!coin_render_plan_shadows(producer.plan, shadowPlan, diagnostic) ||
          !coin_render_shadow_object_profile(producer.plan, shadowPlan,
            shadowPlan.passes.size(), diagnostic))
        return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
      if (!bgfxShadowBatchSupported(producer.plan, shadowPlan, diagnostic))
        return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
      const bgfx::Caps * caps = bgfx::getCaps();
      if (caps->limits.maxTextureSamplers < (shadowPlan.passes.size() > 4 ? 16 : shadowPlan.passes.size() > 2 ? 12 : 10))
        return {CoinRenderBackendStatus::UNSUPPORTED,
                "BGFX direct RTT shadow receiver has insufficient texture stages"};
      for (const auto & pass : shadowPlan.passes)
        if (pass.mapSize > caps->limits.maxTextureSize ||
            !(caps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
            !(caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
            !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA32F, shadowTextureFlags))
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
    if ((peeling && !this->sortedLayersSupported) || (weighted && !this->weightedOitSupported))
      return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX direct RTT transparency is unavailable"};
    auto options = producer.plan.transparency;
    if (weighted && !peeling) options.layers = 1;
    uint64_t requiredBytes = 0;
    if (!coin_render_transparency_budget(producer.size[0], producer.size[1], options,
          peeling || weighted, requiredBytes, diagnostic))
      return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  }
  return {};
}
