#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinrender/SoWgpuBgfxBackend.h"
#include "rendering/coinrender/SoWgpuBgfxCore.h"
#include "rendering/coinrender/CoinRenderImageCore.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderTargetP.h"

#include "coin_bgfx_fs_depth_readback_glsl.h"
#include "coin_bgfx_fs_depth_readback_spirv.h"
#include "coin_bgfx_vs_glsl.h"
#include "coin_bgfx_fs_glsl.h"
#include "coin_bgfx_vs_spirv.h"
#include "coin_bgfx_fs_spirv.h"
#include "coin_bgfx_fs_peel_next_glsl.h"
#include "coin_bgfx_fs_peel_next_spirv.h"
#include "coin_bgfx_fs_composite_glsl.h"
#include "coin_bgfx_fs_composite_spirv.h"
#include "coin_bgfx_fs_weighted_oit_glsl.h"
#include "coin_bgfx_fs_weighted_oit_spirv.h"
#include "coin_bgfx_fs_weighted_composite_glsl.h"
#include "coin_bgfx_fs_weighted_composite_spirv.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <map>
#include <X11/Xlib.h>
#include <utility>

namespace {
// Isolated view blocks prevent per-window state from aliasing. API calls stay
// on one thread; BGFX owns its render worker.
constexpr bgfx::ViewId targetViewCount = 16;
struct SharedBgfxRuntime;
SharedBgfxRuntime & sharedRuntime();

class CoinBgfxCallback : public bgfx::CallbackI {
public:
  CoinBgfxCallback() : fatalCode(-1) { this->fatalMessage[0] = 0; }

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
  uint32_t cacheReadSize(uint64_t) override { return 0; }
  bool cacheRead(uint64_t, void *, uint32_t) override { return false; }
  void cacheWrite(uint64_t, const void *, uint32_t) override {}
  void screenShot(const char *, uint32_t, uint32_t, uint32_t,
                  bgfx::TextureFormat::Enum, const void *, uint32_t, bool) override {}
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
  std::atomic<int> fatalCode;
  mutable std::mutex messageMutex;
  char fatalMessage[512];
};

struct SharedBgfxRuntime {
  std::mutex mutex;
  std::thread::id apiThread;
  std::shared_ptr<bgfx::CallbackI> callback;
  bgfx::RendererType::Enum renderer = bgfx::RendererType::Count;
  unsigned int references = 0;
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
  // The BGFX evaluation is Linux-only. Consuming the flag makes recovery
  // deterministic: the replacement backend must not inherit the same fault.
  unsetenv(name);
  return true;
}

uint64_t drawState(const SoWgpuBgfxDraw & draw)
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
  if (draw.depthWrite) state |= BGFX_STATE_WRITE_Z;
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

bool samePipelineState(const SoWgpuBgfxDraw & lhs,
                       const SoWgpuBgfxDraw & rhs)
{
  return drawState(lhs) == drawState(rhs) &&
    std::memcmp(lhs.viewport, rhs.viewport, sizeof(lhs.viewport)) == 0;
}

bool sameTextureState(const SoWgpuBgfxDraw & lhs,
                      const SoWgpuBgfxDraw & rhs)
{
  return std::memcmp(lhs.extraTextures, rhs.extraTextures, sizeof(lhs.extraTextures)) == 0 &&
    lhs.hasTexture == rhs.hasTexture &&
    lhs.textureSlot == rhs.textureSlot && lhs.textureModel == rhs.textureModel &&
    lhs.wrapS == rhs.wrapS && lhs.wrapT == rhs.wrapT && lhs.filter == rhs.filter &&
    std::memcmp(lhs.textureBlendColor, rhs.textureBlendColor,
                sizeof(lhs.textureBlendColor)) == 0;
}

bool sameLightingState(const SoWgpuBgfxDraw & lhs,
                       const SoWgpuBgfxDraw & rhs)
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
  const std::vector<SoWgpuBgfxDraw> & original,
  const std::vector<SoWgpuBgfxDraw> & encoded)
{
  LogicalDrawStats stats;
  std::vector<uint32_t> originalOpaque;
  std::vector<uint32_t> encodedOpaque;
  const SoWgpuBgfxDraw * previous = nullptr;
  for (const SoWgpuBgfxDraw & draw : original) {
    if (!draw.blend) originalOpaque.push_back(draw.firstIndex);
  }
  for (const SoWgpuBgfxDraw & draw : encoded) {
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
                          SoWgpuBgfxPhaseSample & destination)
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
  uint32_t capacity = 256;
  while (capacity < required && capacity <= UINT32_MAX / 2u)
    capacity *= 2u;
  return capacity < required ? static_cast<uint32_t>(required) : capacity;
}

bool setDrawScissor(const SoWgpuBgfxDraw & draw, int targetWidth, int targetHeight)
{
  int32_t clipped[4];
  if (!SoWgpuBgfxCore::clipViewport(draw.viewport, targetWidth, targetHeight, clipped))
    return false;
  const int32_t top = targetHeight - clipped[1] - clipped[3];
  bgfx::setScissor(static_cast<uint16_t>(clipped[0]),
                   static_cast<uint16_t>(top),
                   static_cast<uint16_t>(clipped[2]),
                   static_cast<uint16_t>(clipped[3]));
  return true;
}
const uint8_t peelPasses = 4;
const uint64_t peelTextureFlags = BGFX_TEXTURE_RT |
  BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
  BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;

uint64_t peelDrawState(const SoWgpuBgfxDraw & draw, bool depthOnly)
{
  uint64_t state = drawState(draw);
  state &= ~(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
             BGFX_STATE_WRITE_Z | BGFX_STATE_BLEND_MASK);
  state |= BGFX_STATE_WRITE_Z;
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

bool captureGpuPasses(uint32_t submittedFrame, SoWgpuBgfxPhaseSample & sample)
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

struct SoWgpuBgfxBackend::AsyncEntry {
  ReadbackSlot slot;
  CoinRenderReadbackTicket ticket;
  bool bottomLeft = false;
};
namespace {
std::map<uint64_t, std::shared_ptr<SoWgpuBgfxBackend::AsyncEntry>> & asyncEntries()
{
  static auto * entries = new std::map<uint64_t, std::shared_ptr<SoWgpuBgfxBackend::AsyncEntry>>;
  return *entries;
}
uint64_t nextTicketToken = 1;
std::vector<std::shared_ptr<SoWgpuBgfxBackend::AsyncEntry>> failedReadbacks;
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
SoWgpuBgfxBackend::SoWgpuBgfxBackend()
  : status(CoinRenderBackendStatus::NOT_READY), viewBase(0), nativeDisplay(nullptr),
    nativeWindow(nullptr), initialized(false), presentToWindow(false),
    cameraPatchEnabled(true), drawGroupingEnabled(true), readbackPipelineDepth(1),
    readbackCursor(0), readbackSequence(0),
    transparencyMode(SoWgpuBgfxTransparencyMode::AUTO),
    activeTransparencyStrategy(SoWgpuBgfxTransparencyStrategy::OBJECT),
    weightedOitSupported(false), sortedLayersSupported(false), serial(0),
    directTextureSerial(0),
    width(0), height(0), program(BGFX_INVALID_HANDLE),
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
    textureSampler(BGFX_INVALID_HANDLE), fogColorModeUniform(BGFX_INVALID_HANDLE),
    fogRangeUniform(BGFX_INVALID_HANDLE), textureParamsUniform(BGFX_INVALID_HANDLE),
    textureBlendUniform(BGFX_INVALID_HANDLE),
    ambientLightUniform(BGFX_INVALID_HANDLE), lightCountUniform(BGFX_INVALID_HANDLE),
    lightPositionTypeUniform(BGFX_INVALID_HANDLE),
    lightDirectionCutoffUniform(BGFX_INVALID_HANDLE),
    lightColorIntensityUniform(BGFX_INVALID_HANDLE),
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
  for (uint8_t i = 0; i < peelPasses; ++i) this->peelFrameBuffers[i] = BGFX_INVALID_HANDLE;
  const char * disabled = std::getenv("COIN_BGFX_DISABLE_CAMERA_PATCH");
  this->cameraPatchEnabled = disabled == nullptr || std::strcmp(disabled, "1") != 0;
  const char * groupingDisabled = std::getenv("COIN_BGFX_DISABLE_DRAW_GROUPING");
  this->drawGroupingEnabled = groupingDisabled == nullptr ||
    std::strcmp(groupingDisabled, "1") != 0;
  const char * readbackDepth = std::getenv("COIN_BGFX_READBACK_PIPELINE_DEPTH");
  if (readbackDepth && std::strcmp(readbackDepth, "2") == 0)
    this->readbackPipelineDepth = 2;
  else if (readbackDepth && std::strcmp(readbackDepth, "3") == 0)
    this->readbackPipelineDepth = 3;
}

SoWgpuBgfxBackend::~SoWgpuBgfxBackend()
{
  this->shutdownRuntime();
}

CoinRenderBackendStatus
SoWgpuBgfxBackend::checkRuntimeFailure(const char * operation)
{
  CoinBgfxCallback * cb = static_cast<CoinBgfxCallback *>(this->callback.get());
  if (cb == nullptr || !cb->failed()) return CoinRenderBackendStatus::SUCCESS;
  this->status = cb->deviceLost() ? CoinRenderBackendStatus::DEVICE_LOST :
                                   CoinRenderBackendStatus::BACKEND_ERROR;
  this->lastError = std::string(operation) + ": " + cb->diagnostic();
  return this->status;
}

void
SoWgpuBgfxBackend::destroyResources()
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
  if (bgfx::isValid(this->ambientLightUniform)) bgfx::destroy(this->ambientLightUniform);
  if (bgfx::isValid(this->lightCountUniform)) bgfx::destroy(this->lightCountUniform);
  if (bgfx::isValid(this->lightPositionTypeUniform)) bgfx::destroy(this->lightPositionTypeUniform);
  if (bgfx::isValid(this->lightDirectionCutoffUniform)) bgfx::destroy(this->lightDirectionCutoffUniform);
  if (bgfx::isValid(this->lightColorIntensityUniform)) bgfx::destroy(this->lightColorIntensityUniform);
  if (bgfx::isValid(this->lightAttenuationDropUniform)) bgfx::destroy(this->lightAttenuationDropUniform);
  if (bgfx::isValid(this->defaultTexture)) bgfx::destroy(this->defaultTexture);
  for (bgfx::TextureHandle texture : this->cachedTextures)
    if (bgfx::isValid(texture)) bgfx::destroy(texture);
  if (bgfx::isValid(this->cachedVertexBuffer)) bgfx::destroy(this->cachedVertexBuffer);
  if (bgfx::isValid(this->cachedIndexBuffer)) bgfx::destroy(this->cachedIndexBuffer);
  if (bgfx::isValid(this->peelNextProgram)) bgfx::destroy(this->peelNextProgram);
  if (bgfx::isValid(this->compositeProgram)) bgfx::destroy(this->compositeProgram);
  if (bgfx::isValid(this->depthReadProgram)) bgfx::destroy(this->depthReadProgram);
  if (bgfx::isValid(this->readDepthSampler)) bgfx::destroy(this->readDepthSampler);
  if (bgfx::isValid(this->program)) bgfx::destroy(this->program);
  if (bgfx::isValid(this->weightedOitProgram)) bgfx::destroy(this->weightedOitProgram);
  if (bgfx::isValid(this->weightedCompositeProgram)) bgfx::destroy(this->weightedCompositeProgram);
}

void
SoWgpuBgfxBackend::shutdownRuntime()
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
SoWgpuBgfxBackend::onApiThread() const
{
  return this->apiThread == std::this_thread::get_id();
}

CoinRenderBackendStatus
SoWgpuBgfxBackend::prepare(CoinRenderTargetP & target)
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
  if (this->presentToWindow &&
      (target.nativeDesc.type != COIN_RENDER_SURFACE_XLIB ||
       target.nativeDesc.native.xlib.display == nullptr ||
       target.nativeDesc.native.xlib.window == 0)) {
    this->lastError = "BGFX window presentation requires a valid Xlib surface";
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
  const char * transparencyMode = std::getenv("COIN_BGFX_TRANSPARENCY");
  if (transparencyMode == nullptr || std::strcmp(transparencyMode, "auto") == 0) {
    this->transparencyMode = SoWgpuBgfxTransparencyMode::AUTO;
  } else if (std::strcmp(transparencyMode, "object") == 0) {
    this->transparencyMode = SoWgpuBgfxTransparencyMode::OBJECT;
  } else if (std::strcmp(transparencyMode, "sorted_layers") == 0) {
    this->transparencyMode = SoWgpuBgfxTransparencyMode::SORTED_LAYERS;
  } else if (std::strcmp(transparencyMode, "weighted_oit") == 0) {
    this->transparencyMode = SoWgpuBgfxTransparencyMode::WEIGHTED_OIT;
  } else {
    this->lastError = "COIN_BGFX_TRANSPARENCY must be auto, object, weighted_oit, or sorted_layers";
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
  if (this->transparencyMode == SoWgpuBgfxTransparencyMode::WEIGHTED_OIT)
    this->activeTransparencyStrategy = SoWgpuBgfxTransparencyStrategy::WEIGHTED_OIT;
  else if (this->transparencyMode == SoWgpuBgfxTransparencyMode::SORTED_LAYERS)
    this->activeTransparencyStrategy = SoWgpuBgfxTransparencyStrategy::SORTED_LAYERS;
  else
    this->activeTransparencyStrategy = SoWgpuBgfxTransparencyStrategy::OBJECT;
  const char * rendererFlag = std::getenv("COIN_BGFX_RENDERER");
  const bool useOpenGl = rendererFlag != nullptr && std::strcmp(rendererFlag, "opengl") == 0;
  if (rendererFlag != nullptr && !useOpenGl && std::strcmp(rendererFlag, "vulkan") != 0) {
    this->lastError = "COIN_BGFX_RENDERER must be opengl or vulkan";
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
  const bgfx::RendererType::Enum renderer =
    useOpenGl ? bgfx::RendererType::OpenGL : bgfx::RendererType::Vulkan;
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
  if (runtime.references == 0) {
    bgfx::Init init;
    init.type = renderer;
    runtime.callback = std::make_shared<CoinBgfxCallback>();
    init.callback = runtime.callback.get();
    // No visible window owns the primary swapchain. Closing the first target
    // therefore cannot invalidate any other target's renderer/context.
    init.swapChain.ndt = this->nativeDisplay;
    init.swapChain.nwh = nullptr;
    init.swapChain.width = 0;
    init.swapChain.height = 0;
    if (!bgfx::init(init)) {
      runtime.callback.reset();
      this->lastError = useOpenGl ? "BGFX could not initialize its headless OpenGL renderer" :
                                   "BGFX could not initialize its headless Vulkan renderer";
      return CoinRenderBackendStatus::NOT_READY;
    }
    runtime.apiThread = std::this_thread::get_id();
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
  if (caps->limits.maxTextureSamplers < COIN_RENDER_MAX_TEXTURE_UNITS + 2) {
    this->lastError = "BGFX renderer needs ten texture samplers for eight units and transparency";
    this->status = CoinRenderBackendStatus::UNSUPPORTED;
    return this->status;
  }
  if (CoinRenderDiagnosticShell::phaseTracingEnabled()) {
    std::fprintf(stderr,
      "COIN_RENDER_PHASE bgfx_device renderer=%s vendor_id=0x%04x device_id=0x%04x homogeneous_depth=%d\n",
      useOpenGl ? "opengl" : "vulkan",
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
  if (caps->rendererType != renderer ||
      !(caps->supported & BGFX_CAPS_INDEX32) ||
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
  this->sortedLayersSupported =
    (caps->formats[bgfx::TextureFormat::RGBA8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) &&
    (caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) &&
    bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::D32F, peelTextureFlags);
  this->weightedOitSupported =
    (caps->supported & BGFX_CAPS_BLEND_INDEPENDENT) &&
    caps->limits.maxFBAttachments >= 3 &&
    bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA16F, peelTextureFlags) &&
    bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::R16F, peelTextureFlags);

  if (this->transparencyMode == SoWgpuBgfxTransparencyMode::SORTED_LAYERS && !this->sortedLayersSupported) {
    this->lastError = "BGFX sorted layers requires sampleable D32F and RGBA8 render targets";
    this->status = CoinRenderBackendStatus::UNSUPPORTED;
    return this->status;
  }
  if (this->transparencyMode == SoWgpuBgfxTransparencyMode::WEIGHTED_OIT && !this->weightedOitSupported) {
    this->lastError = "BGFX weighted OIT requires independent blending and sampleable RGBA16F/R16F render targets";
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
    .add(bgfx::Attrib::TexCoord3, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)
    .end();
  bgfx::ShaderHandle vs = bgfx::createShader(useOpenGl ?
    bgfx::copy(coin_bgfx_vs_glsl, sizeof(coin_bgfx_vs_glsl)) :
    bgfx::copy(coin_bgfx_vs_spirv, sizeof(coin_bgfx_vs_spirv)));
  bgfx::ShaderHandle fs = bgfx::createShader(useOpenGl ?
    bgfx::copy(coin_bgfx_fs_glsl, sizeof(coin_bgfx_fs_glsl)) :
    bgfx::copy(coin_bgfx_fs_spirv, sizeof(coin_bgfx_fs_spirv)));
  if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
    if (bgfx::isValid(vs)) bgfx::destroy(vs);
    if (bgfx::isValid(fs)) bgfx::destroy(fs);
    this->lastError = "BGFX failed to create its renderer-specific shaders";
    this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    return this->status;
  }
  this->program = bgfx::createProgram(vs, fs, true);
  if (!bgfx::isValid(this->program)) {
    this->lastError = "BGFX failed to link its base-color shader program";
    this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    return this->status;
  }
  this->textureSampler = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
  for (size_t unit = 1; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
    const std::string name = "s_texColor" + std::to_string(unit);
    this->extraTextureSamplers[unit - 1] = bgfx::createUniform(name.c_str(), bgfx::UniformType::Sampler);
  }
  this->fogColorModeUniform = bgfx::createUniform("u_fogColorMode", bgfx::UniformType::Vec4);
  this->fogRangeUniform = bgfx::createUniform("u_fogRange", bgfx::UniformType::Vec4);
  this->textureParamsUniform = bgfx::createUniform("u_texParams", bgfx::UniformType::Vec4, COIN_RENDER_MAX_TEXTURE_UNITS);
  this->screenDoorUniform = bgfx::createUniform("u_screenDoor", bgfx::UniformType::Vec4);
  this->coinDepthUniform = bgfx::createUniform("u_coinDepth", bgfx::UniformType::Vec4);
  this->textureBlendUniform = bgfx::createUniform("u_texBlend", bgfx::UniformType::Vec4, COIN_RENDER_MAX_TEXTURE_UNITS);
  this->ambientLightUniform = bgfx::createUniform("u_ambientLight", bgfx::UniformType::Vec4);
  this->lightCountUniform = bgfx::createUniform("u_lightCount", bgfx::UniformType::Vec4);
  this->lightPositionTypeUniform = bgfx::createUniform("u_lightPositionType", bgfx::UniformType::Vec4, COIN_RENDER_MAX_LIGHTS);
  this->lightDirectionCutoffUniform = bgfx::createUniform("u_lightDirectionCutoff", bgfx::UniformType::Vec4, COIN_RENDER_MAX_LIGHTS);
  this->lightColorIntensityUniform = bgfx::createUniform("u_lightColorIntensity", bgfx::UniformType::Vec4, COIN_RENDER_MAX_LIGHTS);
  this->lightAttenuationDropUniform = bgfx::createUniform("u_lightAttenuationDrop", bgfx::UniformType::Vec4, COIN_RENDER_MAX_LIGHTS);
  const uint32_t whitePixel = UINT32_C(0xffffffff);
  this->defaultTexture = bgfx::createTexture2D(1, 1, false, 1,
    bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
    bgfx::copy(&whitePixel, sizeof(whitePixel)));
  bool textureUniformsValid = bgfx::isValid(this->fogColorModeUniform) && bgfx::isValid(this->fogRangeUniform);
  for (auto handle : this->extraTextureSamplers) textureUniformsValid = textureUniformsValid && bgfx::isValid(handle);
  if (!textureUniformsValid || !bgfx::isValid(this->textureSampler) ||
      !bgfx::isValid(this->coinDepthUniform) ||
      !bgfx::isValid(this->textureParamsUniform) ||
      !bgfx::isValid(this->textureBlendUniform) ||
      !bgfx::isValid(this->ambientLightUniform) ||
      !bgfx::isValid(this->lightCountUniform) ||
      !bgfx::isValid(this->lightPositionTypeUniform) ||
      !bgfx::isValid(this->lightDirectionCutoffUniform) ||
      !bgfx::isValid(this->lightColorIntensityUniform) ||
      !bgfx::isValid(this->lightAttenuationDropUniform) ||
      !bgfx::isValid(this->defaultTexture)) {
    this->lastError = "BGFX could not allocate texture uniforms or default texture";
    this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    return this->status;
  }
  const bool buildSortedLayers = this->sortedLayersSupported &&
    (this->transparencyMode == SoWgpuBgfxTransparencyMode::AUTO ||
     this->transparencyMode == SoWgpuBgfxTransparencyMode::SORTED_LAYERS);
  const bool buildWeightedOit = this->weightedOitSupported &&
    (this->transparencyMode == SoWgpuBgfxTransparencyMode::AUTO ||
     this->transparencyMode == SoWgpuBgfxTransparencyMode::WEIGHTED_OIT);
  if (!this->presentToWindow || buildSortedLayers || buildWeightedOit) {
    this->depthInfoUniform = bgfx::createUniform("u_depthInfo", bgfx::UniformType::Vec4);
    SoWgpuBgfxVertex fullscreen[3] = {};
    fullscreen[0].position[0] = -1.0f; fullscreen[0].position[1] = -1.0f;
    fullscreen[1].position[0] =  3.0f; fullscreen[1].position[1] = -1.0f;
    fullscreen[2].position[0] = -1.0f; fullscreen[2].position[1] =  3.0f;
    const uint16_t indices[3] = {0, 1, 2};
    this->fullscreenVertexBuffer = bgfx::createVertexBuffer(
      bgfx::copy(fullscreen, sizeof(fullscreen)), this->layout);
    this->fullscreenIndexBuffer = bgfx::createIndexBuffer(bgfx::copy(indices, sizeof(indices)));
    if (!bgfx::isValid(this->depthInfoUniform) ||
        !bgfx::isValid(this->fullscreenVertexBuffer) ||
        !bgfx::isValid(this->fullscreenIndexBuffer)) {
      this->lastError = "BGFX could not allocate shared transparency resources";
      this->status = CoinRenderBackendStatus::BACKEND_ERROR; return this->status;
    }
  }

  if (!this->presentToWindow) {
    this->depthReadProgram = createLayerProgram(
      useOpenGl ? coin_bgfx_vs_glsl : coin_bgfx_vs_spirv,
      useOpenGl ? sizeof(coin_bgfx_vs_glsl) : sizeof(coin_bgfx_vs_spirv),
      useOpenGl ? coin_bgfx_fs_depth_readback_glsl : coin_bgfx_fs_depth_readback_spirv,
      useOpenGl ? sizeof(coin_bgfx_fs_depth_readback_glsl) : sizeof(coin_bgfx_fs_depth_readback_spirv));
    this->readDepthSampler = bgfx::createUniform("s_readDepth", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->depthReadProgram) || !bgfx::isValid(this->readDepthSampler)) {
      this->lastError = "BGFX depth readback shader allocation failed";
      return this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    }
  }
  if (buildSortedLayers) {
    const uint8_t * vertexShader = useOpenGl ? coin_bgfx_vs_glsl : coin_bgfx_vs_spirv;
    const uint32_t vertexBytes = useOpenGl ? sizeof(coin_bgfx_vs_glsl) : sizeof(coin_bgfx_vs_spirv);
    this->peelNextProgram = createLayerProgram(vertexShader, vertexBytes,
      useOpenGl ? coin_bgfx_fs_peel_next_glsl : coin_bgfx_fs_peel_next_spirv,
      useOpenGl ? sizeof(coin_bgfx_fs_peel_next_glsl) : sizeof(coin_bgfx_fs_peel_next_spirv));
    this->compositeProgram = createLayerProgram(vertexShader, vertexBytes,
      useOpenGl ? coin_bgfx_fs_composite_glsl : coin_bgfx_fs_composite_spirv,
      useOpenGl ? sizeof(coin_bgfx_fs_composite_glsl) : sizeof(coin_bgfx_fs_composite_spirv));
    this->previousDepthSampler = bgfx::createUniform("s_prevDepth", bgfx::UniformType::Sampler);
    this->previousColorSampler = bgfx::createUniform("s_prevColor", bgfx::UniformType::Sampler);
    this->layerSampler = bgfx::createUniform("s_layer", bgfx::UniformType::Sampler);
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
      return this->status;
    }
  }
  if (buildWeightedOit) {
    const uint8_t * vertexShader = useOpenGl ? coin_bgfx_vs_glsl : coin_bgfx_vs_spirv;
    const uint32_t vertexBytes = useOpenGl ? sizeof(coin_bgfx_vs_glsl) : sizeof(coin_bgfx_vs_spirv);
    this->weightedOitProgram = createLayerProgram(vertexShader, vertexBytes,
      useOpenGl ? coin_bgfx_fs_weighted_oit_glsl : coin_bgfx_fs_weighted_oit_spirv,
      useOpenGl ? sizeof(coin_bgfx_fs_weighted_oit_glsl) : sizeof(coin_bgfx_fs_weighted_oit_spirv));
    this->weightedCompositeProgram = createLayerProgram(vertexShader, vertexBytes,
      useOpenGl ? coin_bgfx_fs_weighted_composite_glsl : coin_bgfx_fs_weighted_composite_spirv,
      useOpenGl ? sizeof(coin_bgfx_fs_weighted_composite_glsl) : sizeof(coin_bgfx_fs_weighted_composite_spirv));
    this->oitAccumSampler = bgfx::createUniform("s_oitAccum", bgfx::UniformType::Sampler);
    this->oitRevealSampler = bgfx::createUniform("s_oitReveal", bgfx::UniformType::Sampler);
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
      return this->status;
    }
  }
  if (!this->resize(target.size[0], target.size[1])) {
    if (this->status != CoinRenderBackendStatus::DEVICE_LOST)
      this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    return this->status;
  }
  this->status = CoinRenderBackendStatus::SUCCESS;
  this->lastError.clear();
  return this->status;
}

void
SoWgpuBgfxBackend::destroyFrameBuffers()
{
  for (uint8_t pass = 0; pass < peelPasses; ++pass) {
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
SoWgpuBgfxBackend::resize(int newWidth, int newHeight)
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
    this->depthReadFrameBuffer = bgfx::createFrameBuffer(
      static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
      bgfx::TextureFormat::R32F, peelTextureFlags);
    if (!bgfx::isValid(this->depthReadFrameBuffer)) {
      this->lastError = "BGFX sampleable depth/R32F readback requires unsupported GPU formats";
      return false;
    }
    const size_t readbackBytes = static_cast<size_t>(newWidth) * newHeight * 4u;
    this->readbackSlots.resize(this->readbackPipelineDepth);
    for (ReadbackSlot & slot : this->readbackSlots) {
      slot.texture = bgfx::createTexture2D(static_cast<uint16_t>(newWidth),
        static_cast<uint16_t>(newHeight), false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
      slot.depthTexture = bgfx::createTexture2D(static_cast<uint16_t>(newWidth),
        static_cast<uint16_t>(newHeight), false, 1, bgfx::TextureFormat::R32F,
        BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
      slot.depth.resize(readbackBytes / 4);
      slot.pixels.resize(readbackBytes);
      if (!bgfx::isValid(slot.texture) || !bgfx::isValid(slot.depthTexture)) {
        this->lastError = "BGFX could not allocate the offscreen readback staging ring";
        this->destroyFrameBuffers();
        return false;
      }
    }
  }
  if (this->activeTransparencyStrategy == SoWgpuBgfxTransparencyStrategy::SORTED_LAYERS) {
    for (uint8_t pass = 0; pass < peelPasses; ++pass) {
      bgfx::TextureHandle color = bgfx::createTexture2D(
        static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
        false, 1, bgfx::TextureFormat::RGBA8, peelTextureFlags);
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
  if (this->activeTransparencyStrategy == SoWgpuBgfxTransparencyStrategy::WEIGHTED_OIT) {
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
SoWgpuBgfxBackend::bindDrawTexture(
  const SoWgpuBgfxDraw & draw, const std::vector<bgfx::TextureHandle> & textures)
{
  float params[COIN_RENDER_MAX_TEXTURE_UNITS][4] = {};
  float blend[COIN_RENDER_MAX_TEXTURE_UNITS][4] = {};
  for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
    SoWgpuBgfxDraw::TextureLayer layer;
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
    std::memcpy(blend[unit], layer.blendColor, sizeof(layer.blendColor));
    bgfx::setTexture(static_cast<uint8_t>(unit + 2),
      unit == 0 ? this->textureSampler : this->extraTextureSamplers[unit - 1], texture, flags);
  }
  bgfx::setUniform(this->textureParamsUniform, params, COIN_RENDER_MAX_TEXTURE_UNITS);
  bgfx::setUniform(this->textureBlendUniform, blend, COIN_RENDER_MAX_TEXTURE_UNITS);
}


void
SoWgpuBgfxBackend::bindDrawLighting(const SoWgpuBgfxDraw & draw, int targetHeight)
{
  const float door[4] = {draw.screenDoor[0], float(targetHeight > 0 ? targetHeight : this->height),
    bgfx::getCaps()->originBottomLeft ? 1.0f : 0.0f, draw.screenDoor[3]};
  bgfx::setUniform(this->screenDoorUniform, door);
  // Use two D24 LSBs on GL to survive the gl_FragCoord-to-gl_FragDepth
  // floating-point round trip; other renderers retain the one-LSB contract.
  // Driver-specific native polygon offset resolution remains approximate.
  const bgfx::RendererType::Enum renderer = bgfx::getRendererType();
  const float unitScale =
    (renderer == bgfx::RendererType::OpenGL ||
     renderer == bgfx::RendererType::OpenGLES)
      ? 1.0f / 8388608.0f : 1.0f / 16777216.0f;
  const float depth[4] = {draw.depthRange[0], draw.depthRange[1],
    draw.polygonOffsetFactor, draw.polygonOffsetUnits * unitScale};
  bgfx::setUniform(this->coinDepthUniform, depth);
  bgfx::setUniform(this->fogColorModeUniform, draw.fogColorMode);
  bgfx::setUniform(this->fogRangeUniform, draw.fogRange);
  bgfx::setUniform(this->ambientLightUniform, draw.ambientLight);
  bgfx::setUniform(this->lightCountUniform, draw.lightCount);
  bgfx::setUniform(this->lightPositionTypeUniform, draw.lightPositionType, COIN_RENDER_MAX_LIGHTS);
  bgfx::setUniform(this->lightDirectionCutoffUniform, draw.lightDirectionCutoff, COIN_RENDER_MAX_LIGHTS);
  bgfx::setUniform(this->lightColorIntensityUniform, draw.lightColorIntensity, COIN_RENDER_MAX_LIGHTS);
  bgfx::setUniform(this->lightAttenuationDropUniform, draw.lightAttenuationDrop, COIN_RENDER_MAX_LIGHTS);
}

void
SoWgpuBgfxBackend::encodeSortedLayers(const std::vector<SoWgpuBgfxDraw> & draws,
                                      bgfx::DynamicVertexBufferHandle vertices,
                                      bgfx::DynamicIndexBufferHandle indices,
                                      bgfx::FrameBufferHandle output,
                                      const std::vector<bgfx::TextureHandle> & textures)
{
  const float transparentBlack[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const float depthInfo[4] = {1.0f / float(this->width),
                              1.0f / float(this->height), 0.0f, 0.0f};
  bgfx::setPaletteColor(1, transparentBlack);
  for (uint8_t pass = 0; pass < peelPasses; ++pass) {
    const bgfx::ViewId view = this->viewBase + pass + 1;
    bgfx::setViewName(view, "transparent_accumulation");
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(view, 0, 0, static_cast<uint16_t>(this->width),
                      static_cast<uint16_t>(this->height));
    bgfx::setViewFrameBuffer(view, this->peelFrameBuffers[pass]);
    bgfx::setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.0f, 0, 1);
    bgfx::setViewTransform(view, nullptr, nullptr);
    bgfx::touch(view);
    // Opaque geometry supplies the occlusion depth in every peel pass.
    for (const SoWgpuBgfxDraw & draw : draws) {
      if (draw.renderLayer != 0 || (draw.blend && draw.deferred)) continue;
      bgfx::setTransform(draw.mvp);
      bgfx::setVertexBuffer(0, vertices);
      bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
      bgfx::setState(peelDrawState(draw, true));
      if (!setDrawScissor(draw, this->width, this->height)) continue;
      this->bindDrawTexture(draw, textures);
      this->bindDrawLighting(draw);
      bgfx::submit(view, this->program);

    }
    for (const SoWgpuBgfxDraw & draw : draws) {
      if (draw.renderLayer != 0 || !draw.blend || !draw.deferred || draw.additive) continue;
      bgfx::setTransform(draw.mvp);
      bgfx::setVertexBuffer(0, vertices);
      bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
      bgfx::setState(peelDrawState(draw, false));
      if (!setDrawScissor(draw, this->width, this->height)) continue;
      if (pass != 0) {
        bgfx::setUniform(this->depthInfoUniform, depthInfo);
        bgfx::setTexture(0, this->previousDepthSampler,
                         bgfx::getTexture(this->peelFrameBuffers[pass - 1], 1));
        bgfx::setTexture(1, this->previousColorSampler,
                         bgfx::getTexture(this->peelFrameBuffers[pass - 1], 0));
      }
      this->bindDrawTexture(draw, textures);
      this->bindDrawLighting(draw);
      bgfx::submit(view, pass == 0 ? this->program : this->peelNextProgram);
    }
  }

  const bgfx::ViewId compositeView = this->viewBase + peelPasses + 1;
  bgfx::setViewName(compositeView, "fullscreen_composition");
  bgfx::setViewMode(compositeView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(compositeView, 0, 0, static_cast<uint16_t>(this->width),
                    static_cast<uint16_t>(this->height));
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
  for (int pass = int(peelPasses) - 1; pass >= 0; --pass) {
    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
    bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
    bgfx::setUniform(this->depthInfoUniform, depthInfo);
    bgfx::setTexture(0, this->layerSampler,
                     bgfx::getTexture(this->peelFrameBuffers[pass], 0));
    bgfx::setState(blendState);
    bgfx::submit(compositeView, this->compositeProgram);
  }
}

void
SoWgpuBgfxBackend::encodeWeightedOit(const std::vector<SoWgpuBgfxDraw> & draws,
                                     bgfx::DynamicVertexBufferHandle vertices,
                                     bgfx::DynamicIndexBufferHandle indices,
                                     bgfx::FrameBufferHandle output,
                                     const std::vector<bgfx::TextureHandle> & textures)
{
  const bgfx::ViewId oitView = this->viewBase + 1;
  bgfx::setViewName(oitView, "transparent_accumulation");
  const float transparentBlack[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const float opaqueWhite[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  const float depthInfo[4] = {1.0f / float(this->width),
                              1.0f / float(this->height), 0.0f, 0.0f};
  bgfx::setPaletteColor(1, transparentBlack);
  bgfx::setPaletteColor(2, opaqueWhite);
  bgfx::setViewMode(oitView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(oitView, 0, 0, static_cast<uint16_t>(this->width),
                    static_cast<uint16_t>(this->height));
  bgfx::setViewFrameBuffer(oitView, this->oitFrameBuffer);
  bgfx::setViewClear(oitView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                     1.0f, 0, 1, 2);
  bgfx::setViewTransform(oitView, nullptr, nullptr);
  bgfx::touch(oitView);

  // Rebuild only opaque depth so transparent fragments behind opaque Coin
  // geometry cannot contribute to either accumulation attachment.
  for (const SoWgpuBgfxDraw & draw : draws) {
    if (draw.renderLayer != 0 || (draw.blend && draw.deferred)) continue;
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vertices);
    bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
    bgfx::setState(peelDrawState(draw, true));
    if (!setDrawScissor(draw, this->width, this->height)) continue;
    this->bindDrawTexture(draw, textures);
    this->bindDrawLighting(draw);
    bgfx::submit(oitView, this->program);

  }

  for (const SoWgpuBgfxDraw & draw : draws) {
    if (draw.renderLayer != 0 || !draw.blend) continue;
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
    if (!setDrawScissor(draw, this->width, this->height)) continue;
    this->bindDrawTexture(draw, textures);
    this->bindDrawLighting(draw);
    bgfx::submit(oitView, this->weightedOitProgram);

  }

  const bgfx::ViewId compositeView = this->viewBase + 2;
  bgfx::setViewName(compositeView, "fullscreen_composition");
  bgfx::setViewMode(compositeView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(compositeView, 0, 0, static_cast<uint16_t>(this->width),
                    static_cast<uint16_t>(this->height));
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
                   bgfx::getTexture(this->oitFrameBuffer, 0));
  bgfx::setTexture(1, this->oitRevealSampler,
                   bgfx::getTexture(this->oitFrameBuffer, 1));
  bgfx::setState(blendState);
  bgfx::submit(compositeView, this->weightedCompositeProgram);
}

bool
SoWgpuBgfxBackend::encodeOverlayLayers(
  const std::vector<SoWgpuBgfxDraw> & draws,
  bgfx::DynamicVertexBufferHandle vertices,
  bgfx::DynamicIndexBufferHandle indices,
  bgfx::FrameBufferHandle output,
  const std::vector<bgfx::TextureHandle> & textures,
  bgfx::ViewId & nextView)
{
  uint32_t encodedLayer = 0;
  for (const SoWgpuBgfxDraw & layerDraw : draws) {
    if (layerDraw.renderLayer == 0 || layerDraw.renderLayer == encodedLayer) continue;
    encodedLayer = layerDraw.renderLayer;
    if (nextView > this->viewBase + targetViewCount - 3) {
      this->lastError = "BGFX overlay layer count exceeds available view IDs";
      return false;
    }
    const SoWgpuBgfxDraw * barrier = nullptr;
    for (const SoWgpuBgfxDraw & draw : draws) {
      if (draw.renderLayer == encodedLayer && draw.clearDepthBefore) {
        barrier = &draw;
        break;
      }
    }
    int32_t clipped[4];
    if (barrier && SoWgpuBgfxCore::clipViewport(barrier->viewport, this->width,
                                               this->height, clipped)) {
      const bgfx::ViewId clearView = nextView++;
      const int32_t top = this->height - clipped[1] - clipped[3];
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
    bgfx::setViewRect(overlayView, 0, 0, static_cast<uint16_t>(this->width),
                      static_cast<uint16_t>(this->height));
    bgfx::setViewFrameBuffer(overlayView, output);
    bgfx::setViewClear(overlayView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(overlayView, nullptr, nullptr);
    bgfx::touch(overlayView);
    for (const SoWgpuBgfxDraw & draw : draws) {
      if (draw.renderLayer != encodedLayer || false) continue;
      bgfx::setTransform(draw.mvp);
      bgfx::setVertexBuffer(0, vertices);
      bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
      bgfx::setState(drawState(draw));
      if (!setDrawScissor(draw, this->width, this->height)) continue;
      this->bindDrawTexture(draw, textures);
      this->bindDrawLighting(draw);
      bgfx::submit(overlayView, this->program);
    }
  }
  return true;
}

CoinRenderSubmitResult
SoWgpuBgfxBackend::submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target)
{
  return this->submit(frame, target, CoinRenderFrameReuseDecision());
}

CoinRenderSubmitResult
SoWgpuBgfxBackend::submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
                          const CoinRenderFrameReuseDecision & reuse)
{
  return this->submitInternal(frame, target, reuse, nullptr);
}

CoinRenderSubmitResult
SoWgpuBgfxBackend::submitAsync(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
  CoinRenderReadbackTicket & ticket, const CoinRenderFrameReuseDecision & reuse)
{
  ticket = CoinRenderReadbackTicket{};
  return this->submitInternal(frame, target, reuse, &ticket);
}

CoinRenderSubmitResult
SoWgpuBgfxBackend::submitInternal(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
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
    this->lastError = "BGFX supports Xlib presentation or offscreen readback";
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  if (outTicket) {
    auto & runtime = sharedRuntime();
    std::lock_guard<std::mutex> guard(runtime.mutex);
    uint64_t pendingBytes = 0;
    for (const auto & entry : asyncEntries())
      pendingBytes += entry.second->ticket.colorBytes + entry.second->ticket.depthBytes;
    const uint64_t requested = uint64_t(target.size[0]) * target.size[1] * (target.depthReadbackEnabled ? 8 : 4);
    if (this->presentToWindow || asyncEntries().size() >= 16 ||
        requested > UINT64_C(128)*1024*1024 || pendingBytes > UINT64_C(128)*1024*1024 - requested)
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
  const bool cacheDimensionsMatch =
    target.size[0] == this->cachedWidth &&
    target.size[1] == this->cachedHeight &&
    homogeneousDepth == this->cachedHomogeneousDepth;
  const bool cacheHit = frame.revision != 0 &&
    frame.revision == this->cachedRevision && cacheDimensionsMatch;
  const bool cameraPatchEligible =
    this->cameraPatchEnabled &&
    reuse.kind == CoinRenderFrameReuseKind::CAMERA_PATCH &&
    reuse.baseRevision != 0 && reuse.baseRevision == this->cachedRevision &&
    frame.revision != 0 && frame.revision != this->cachedRevision &&
    cacheDimensionsMatch &&
    (this->cachedPlan.draws.empty() ||
      (bgfx::isValid(this->cachedVertexBuffer) &&
       bgfx::isValid(this->cachedIndexBuffer)));
  std::vector<SoWgpuBgfxDraw> cameraDraws;
  const bool cameraPatchUsed = cameraPatchEligible &&
    SoWgpuBgfxCore::patchCamera(frame, target.size[0], target.size[1],
                                homogeneousDepth, this->cachedPlan,
                                cameraDraws, this->lastError);
  SoWgpuBgfxPlan freshPlan;
  const SoWgpuBgfxPlan * plan = &this->cachedPlan;
  if (!cacheHit && !cameraPatchUsed) {
    if (!SoWgpuBgfxCore::lower(frame, target.size[0], target.size[1],
                              homogeneousDepth, freshPlan, this->lastError)) {
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    }
    plan = &freshPlan;
  }
  std::vector<SoWgpuBgfxVertexRange> materialRanges;
  const bool materialPatchEligible = !cacheHit && !cameraPatchUsed &&
    reuse.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD &&
    reuse.baseRevision != 0 && reuse.baseRevision == this->cachedRevision &&
    cacheDimensionsMatch && bgfx::isValid(this->cachedVertexBuffer) &&
    bgfx::isValid(this->cachedIndexBuffer);
  const bool materialPatchUsed = materialPatchEligible &&
    SoWgpuBgfxCore::materialPatchRanges(this->cachedPlan, freshPlan,
                                        materialRanges);
  const std::vector<SoWgpuBgfxDraw> & strategyDraws =
    cameraPatchUsed ? cameraDraws : plan->draws;
  SoWgpuBgfxTransparencyStrategy selectedStrategy;
  if (!SoWgpuBgfxCore::selectTransparencyStrategy(strategyDraws,
        this->transparencyMode, this->weightedOitSupported,
        this->sortedLayersSupported, selectedStrategy, this->lastError)) {
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  if (selectedStrategy != this->activeTransparencyStrategy) {
    // Keep the native surface when opaque-only frames choose OBJECT or later
    // frames require OIT/peeling. BGFX creates resources before retiring old
    // ones: recreating a swapchain on the same X window here would race its
    // old EGL surface and fail with EGL_BAD_ALLOC.
    const bgfx::FrameBufferHandle windowOutput = this->frameBuffer;
    if (this->presentToWindow) this->frameBuffer = BGFX_INVALID_HANDLE;
    this->destroyFrameBuffers();
    if (this->presentToWindow) this->frameBuffer = windowOutput;
    this->activeTransparencyStrategy = selectedStrategy;
  }
  const Clock::time_point lowered = Clock::now();
  if (!this->resize(target.size[0], target.size[1])) {
    const CoinRenderBackendStatus resizeStatus = this->status == CoinRenderBackendStatus::SUCCESS ?
      CoinRenderBackendStatus::OUT_OF_MEMORY : this->status;
    return CoinRenderSubmitResult(resizeStatus, this->lastError);
  }
  if (plan->vertices.size() > std::numeric_limits<uint32_t>::max() / sizeof(SoWgpuBgfxVertex) ||
      plan->indices.size() > std::numeric_limits<uint32_t>::max() / sizeof(uint32_t)) {
    this->lastError = "BGFX geometry exceeds buffer size limits";
    return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, this->lastError);
  }
  const size_t geometryBytes = plan->vertices.size() * sizeof(SoWgpuBgfxVertex) +
                               plan->indices.size() * sizeof(uint32_t) +
                               plan->draws.size() * sizeof(SoWgpuBgfxDraw);
  bgfx::DynamicVertexBufferHandle vb = this->cachedVertexBuffer;
  bgfx::DynamicIndexBufferHandle ib = this->cachedIndexBuffer;
  bool retained = cacheHit || cameraPatchUsed || materialPatchUsed;
  bool geometryBufferReused = retained;
  uint32_t materialPatchVertices = 0;
  if (materialPatchUsed) {
    for (const SoWgpuBgfxVertexRange & range : materialRanges) {
      materialPatchVertices += range.count;
      bgfx::update(vb, range.first, bgfx::copy(
        freshPlan.vertices.data() + range.first,
        range.count * static_cast<uint32_t>(sizeof(SoWgpuBgfxVertex))));
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
        this->cachedVertexCapacity >= plan->vertices.size();
      const bool indexPoolHit = bgfx::isValid(ib) &&
        this->cachedIndexCapacity >= plan->indices.size();
      geometryBufferReused = vertexPoolHit && indexPoolHit;
      if (!vertexPoolHit) {
        if (bgfx::isValid(vb)) bgfx::destroy(vb);
        this->cachedVertexCapacity = pooledCapacity(plan->vertices.size());
        vb = bgfx::createDynamicVertexBuffer(this->cachedVertexCapacity,
                                             this->layout);
        this->cachedVertexBuffer = vb;
      }
      if (!indexPoolHit) {
        if (bgfx::isValid(ib)) bgfx::destroy(ib);
        this->cachedIndexCapacity = pooledCapacity(plan->indices.size());
        ib = bgfx::createDynamicIndexBuffer(this->cachedIndexCapacity,
                                             BGFX_BUFFER_INDEX32);
        this->cachedIndexBuffer = ib;
      }
      if (!bgfx::isValid(vb) || !bgfx::isValid(ib)) {
        this->lastError = "BGFX geometry upload failed";
        return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, this->lastError);
      }
      bgfx::update(vb, 0, bgfx::copy(plan->vertices.data(),
        static_cast<uint32_t>(plan->vertices.size() * sizeof(SoWgpuBgfxVertex))));
      bgfx::update(ib, 0, bgfx::copy(plan->indices.data(),
        static_cast<uint32_t>(plan->indices.size() * sizeof(uint32_t))));
    }
    for (bgfx::TextureHandle texture : this->cachedTextures)
      if (bgfx::isValid(texture)) bgfx::destroy(texture);
    this->cachedTextures.clear();
    this->cachedRevision = 0;
    if (frame.revision != 0 && geometryBytes <= 32u * 1024u * 1024u) {
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
  for (const SoWgpuBgfxTexture & texture : plan->textures)
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
    const SoWgpuBgfxTexture & texture = plan->textures[textureIndex];
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
  bgfx::setViewName(this->viewBase, "opaque");
  bgfx::setViewMode(this->viewBase, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(this->viewBase, 0, 0, static_cast<uint16_t>(this->width),
                    static_cast<uint16_t>(this->height));
  const bgfx::FrameBufferHandle windowFrameBuffer = this->frameBuffer;
  bgfx::setViewFrameBuffer(this->viewBase, this->presentToWindow ? windowFrameBuffer : this->frameBuffer);
  // The packed clear API quantizes Coin's float color before the GL clear.
  // Keep the float until the renderer converts it to its target format.
  bgfx::setPaletteColor(0, plan->clearColor);
  bgfx::setViewClear(this->viewBase, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.0f, 0, 0);
  bgfx::setViewTransform(this->viewBase, nullptr, nullptr);
  bgfx::touch(this->viewBase);
  const std::vector<SoWgpuBgfxDraw> & sourceDraws =
    cameraPatchUsed ? cameraDraws : plan->draws;
  std::vector<SoWgpuBgfxDraw> groupedDraws;
  if (this->drawGroupingEnabled)
    SoWgpuBgfxCore::groupOpaqueDraws(sourceDraws, groupedDraws);
  const std::vector<SoWgpuBgfxDraw> & draws =
    this->drawGroupingEnabled ? groupedDraws : sourceDraws;
  const LogicalDrawStats drawStats = logicalDrawStats(sourceDraws, draws);
  const bool useSortedLayers =
    selectedStrategy == SoWgpuBgfxTransparencyStrategy::SORTED_LAYERS;
  const bool useWeightedOit =
    selectedStrategy == SoWgpuBgfxTransparencyStrategy::WEIGHTED_OIT;
  bgfx::ViewId nextView = this->viewBase + 2;
  for (const SoWgpuBgfxDraw & draw : draws) {
    if (draw.renderLayer != 0 || (draw.blend && draw.deferred)) continue;
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vb);
    bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
    bgfx::setState(drawState(draw));
    if (!setDrawScissor(draw, this->width, this->height)) continue;
    this->bindDrawTexture(draw, textures);
    this->bindDrawLighting(draw);
    bgfx::submit(this->viewBase, this->program);

  }
  if (!useSortedLayers && !useWeightedOit) {
    const bgfx::ViewId transparentView = this->viewBase + 1;
    bgfx::setViewName(transparentView, "transparent_accumulation");
    bgfx::setViewMode(transparentView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(transparentView, 0, 0,
                      static_cast<uint16_t>(this->width),
                      static_cast<uint16_t>(this->height));
    bgfx::setViewFrameBuffer(transparentView,
      this->presentToWindow ? windowFrameBuffer : this->frameBuffer);
    bgfx::setViewClear(transparentView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(transparentView, nullptr, nullptr);
    for (const SoWgpuBgfxDraw & draw : draws) {
      if (draw.renderLayer != 0 || !draw.blend || !draw.deferred) continue;
      bgfx::setTransform(draw.mvp);
      bgfx::setVertexBuffer(0, vb);
      bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
      bgfx::setState(drawState(draw));
      if (!setDrawScissor(draw, this->width, this->height)) continue;
      this->bindDrawTexture(draw, textures);
      this->bindDrawLighting(draw);
      bgfx::submit(transparentView, this->program);
    }
  } else if (useSortedLayers) {
    nextView = this->viewBase + peelPasses + 2;
    this->encodeSortedLayers(draws, vb, ib,
      this->presentToWindow ? windowFrameBuffer : this->frameBuffer,
      textures);
  } else if (useWeightedOit) {
    nextView = this->viewBase + 3;
    this->encodeWeightedOit(draws, vb, ib,
      this->presentToWindow ? windowFrameBuffer : this->frameBuffer,
      textures);
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
        this->bindDrawTexture(draw, textures); this->bindDrawLighting(draw);
        bgfx::submit(nextView, this->program);
      }
      ++nextView;
    }
  }
  if (!this->encodeOverlayLayers(draws, vb, ib,
        this->presentToWindow ? windowFrameBuffer : this->frameBuffer,
        textures, nextView)) {
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
    const uint32_t submittedFrame = bgfx::frame();
    runtimeStatus = this->checkRuntimeFailure("BGFX frame submission failed");
    if (runtimeStatus != CoinRenderBackendStatus::SUCCESS)
      return CoinRenderSubmitResult(runtimeStatus, this->lastError);
    const Clock::time_point submitted = Clock::now();
    SoWgpuBgfxPhaseSample sample;
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
    target.colorBuffer.clear();
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
      sample.vertices = plan->vertices.size();
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
    target.colorBuffer.clear(); target.depthBuffer.clear();
    target.needsReconfigure = false;
    *outTicket = ticket;
    const auto asyncStatus = this->checkRuntimeFailure("BGFX asynchronous submission failed");
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
  SoWgpuBgfxPhaseSample gpuSample;
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
  if (!publishSlot && this->lastPublishedReadback.empty()) {
    this->lastError = "BGFX readback did not complete within sixteen frames";
    return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  }
  const Clock::time_point framesCompleted = Clock::now();
  // GL readback follows the framebuffer's bottom-left origin; Vulkan's
  // readback in this profile already matches the target's top-left RGBA view.
  // The row-swap is symmetric, so the shared mechanical image helper applies.
  uint32_t readbackLatencyFrames = 0;
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
    this->lastPublishedDepth = publishSlot->depth;
    this->lastPublishedReadback = publishSlot->pixels;
    this->lastPublishedSequence = publishSlot->sequence;
    publishSlot->pending = false;
  }
  target.colorBuffer = this->lastPublishedReadback;
  readbackLatencyFrames = static_cast<uint32_t>(
    this->readbackSequence - this->lastPublishedSequence);
  const Clock::time_point readComplete = Clock::now();
  const Clock::time_point gpuDrainBegin = Clock::now();
  uint32_t gpuQueryFrames = 0;
  for (int attempts = 0;
       traceGpu && gpuSample.gpuFrameMs < 0.0 && attempts < 4; ++attempts) {
    bgfx::frame();
    ++gpuQueryFrames;
    captureGpuFrame();
  }
  runtimeStatus = this->checkRuntimeFailure("BGFX profiling drain failed");
  if (runtimeStatus != CoinRenderBackendStatus::SUCCESS)
    return CoinRenderSubmitResult(runtimeStatus, this->lastError);
  const Clock::time_point gpuDrainComplete = Clock::now();
  if (cameraPatchUsed) {
    this->cachedPlan.draws.swap(cameraDraws);
    this->cachedRevision = frame.revision;
  }
  if (target.depthReadbackEnabled) target.depthBuffer = this->lastPublishedDepth;
  else target.depthBuffer.clear();
  if (tracePhases) {
    const auto ms = [](Clock::time_point a, Clock::time_point b) {
      return std::chrono::duration<double, std::milli>(b - a).count();
    };
    SoWgpuBgfxPhaseSample sample = gpuSample;
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
    sample.rowFlipMs = ms(framesCompleted, readComplete);
    sample.vertices = plan->vertices.size();
    sample.draws = plan->draws.size();
    sample.readWaitFrames = readWaitFrames;
    sample.readbackPipelineDepth = this->readbackPipelineDepth;
    sample.readbackLatencyFrames = readbackLatencyFrames;
    sample.readbackGpuStagingBytes = static_cast<uint64_t>(bytes) *
      this->readbackSlots.size();
    sample.readbackCpuStagingBytes = sample.readbackGpuStagingBytes;
    sample.readbackPublishedBytes = this->lastPublishedReadback.size();
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
    std::fprintf(stderr, "%s\n", CoinRenderDiagnosticShell::formatBgfxPhase(sample).c_str());
  }
  this->lastError.clear();
  return CoinRenderSubmitResult(CoinRenderBackendStatus::SUCCESS, "", ++this->serial);
}

CoinRenderSubmitResult
SoWgpuBgfxBackend::submitDirectTexture(const CoinRenderFramePlan & frame,
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
  SoWgpuBgfxPlan plan;
  if (!SoWgpuBgfxCore::lower(frame, size[0], size[1],
        bgfx::getCaps()->homogeneousDepth, plan, this->lastError)) {
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  SoWgpuBgfxTransparencyStrategy strategy;
  if (!SoWgpuBgfxCore::selectTransparencyStrategy(plan.draws,
        SoWgpuBgfxTransparencyMode::OBJECT, this->weightedOitSupported,
        this->sortedLayersSupported, strategy, this->lastError) ||
      strategy != SoWgpuBgfxTransparencyStrategy::OBJECT) {
    this->lastError = "BGFX direct RTT currently requires object transparency";
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }

  DirectTextureResource * cachedResource = NULL;
  for (DirectTextureResource & resource : this->directTextures) {
    if (resource.producerKey == producerKey) {
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

  bgfx::VertexBufferHandle vb = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ib = BGFX_INVALID_HANDLE;
  if (!plan.draws.empty()) {
    vb = bgfx::createVertexBuffer(bgfx::copy(plan.vertices.data(),
      static_cast<uint32_t>(plan.vertices.size() * sizeof(SoWgpuBgfxVertex))),
      this->layout);
    ib = bgfx::createIndexBuffer(bgfx::copy(plan.indices.data(),
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
    const SoWgpuBgfxTexture & source = plan.textures[i];
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

  bgfx::setPaletteColor(0, plan.clearColor);
  bgfx::setViewName(this->viewBase, "rtt_opaque");
  bgfx::setViewMode(this->viewBase, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(this->viewBase, 0, 0, static_cast<uint16_t>(size[0]),
                    static_cast<uint16_t>(size[1]));
  bgfx::setViewFrameBuffer(this->viewBase, output);
  bgfx::setViewClear(this->viewBase, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.0f, 0, 0);
  bgfx::touch(this->viewBase);
  for (const SoWgpuBgfxDraw & draw : plan.draws) {
    if (draw.blend && draw.deferred) continue;
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vb);
    bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
    bgfx::setState(drawState(draw));
    if (!setDrawScissor(draw, size[0], size[1])) continue;
    this->bindDrawTexture(draw, textures);
    this->bindDrawLighting(draw, size[1]);
    bgfx::submit(this->viewBase, this->program);
  }
  bgfx::setViewName(this->viewBase + 1, "rtt_transparent");
  bgfx::setViewMode(this->viewBase + 1, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(this->viewBase + 1, 0, 0, static_cast<uint16_t>(size[0]),
                    static_cast<uint16_t>(size[1]));
  bgfx::setViewFrameBuffer(this->viewBase + 1, output);
  bgfx::setViewClear(this->viewBase + 1, BGFX_CLEAR_NONE);
  for (const SoWgpuBgfxDraw & draw : plan.draws) {
    if (!draw.blend || !draw.deferred) continue;
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vb);
    bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
    bgfx::setState(drawState(draw));
    if (!setDrawScissor(draw, size[0], size[1])) continue;
    this->bindDrawTexture(draw, textures);
    this->bindDrawLighting(draw, size[1]);
    bgfx::submit(this->viewBase + 1, this->program);
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
SoWgpuBgfxBackend::releaseDirectTexture(uint64_t token)
{
  for (std::vector<DirectTextureResource>::iterator it = this->directTextures.begin();
       it != this->directTextures.end(); ++it) {
    if (it->token != token) continue;
    it->inUse = false;
    return;
  }
}

void
SoWgpuBgfxBackend::finishDirectTextures(const std::vector<uint64_t> & usedTokens)
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
SoWgpuBgfxBackend::pollReadback(const CoinRenderReadbackTicket & ticket,
  std::vector<uint8_t> & color, std::vector<float> & depth, SbString * diagnostic)
{
  auto & runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
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
SoWgpuBgfxBackend::cancelReadback(const CoinRenderReadbackTicket & ticket)
{
  auto & runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
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

void
SoWgpuBgfxBackend::poll()
{
  // Synchronous readback in submit() already advances BGFX frames.
}
