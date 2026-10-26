#ifndef COIN_BGFX_BACKEND_INTERNAL_H
#define COIN_BGFX_BACKEND_INTERNAL_H

#include "rendering/coinrender/CoinRenderTransformCore.h"
#include "rendering/coinrender/CoinRenderBumpMapCore.h"
#include "rendering/coinrender/CoinRenderCubeTextureCore.h"
#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinbgfx/CoinBgfxBackend.h"
#include "rendering/coinrender/CoinRenderResourceCore.h"
#include "rendering/coinbgfx/CoinBgfxLowering.h"
#include "rendering/coinbgfx/CoinBgfxProgramCache.h"
#include "rendering/coinbgfx/CoinBgfxShaderCompiler.h"
#include "rendering/coinbgfx/CoinBgfxProgramSelection.h"
#include "rendering/coinrender/CoinRenderImageCore.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderSelectionCore.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderRttCore.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderAlphaTestCore.h"

#include "rendering/coinrender/CoinRenderMultipassCore.h"
#include "rendering/coinrender/CoinRenderShadowCore.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <map>
#include <new>
#include <unordered_set>
#ifndef _WIN32
#include <X11/Xlib.h>
#include "rendering/coinrender/CoinRenderX11Wsi.h"
#include <dlfcn.h>
#endif
#ifdef _WIN32
#include <d3d12.h>
#endif

#include <utility>


struct CoinBgfxBackend::AsyncEntry {
  ReadbackSlot slot;
  CoinRenderReadbackTicket ticket;
  bool bottomLeft = false;
};

namespace coin_bgfx_internal {

class CoinBgfxCallback : public bgfx::CallbackI {
public:
  explicit CoinBgfxCallback(bool openGl) : fatalCode(-1) {
    this->fatalMessage[0] = 0;
    this->glAdapterDiagnostics = openGl && std::getenv("COIN_BGFX_TRACE_GL_ADAPTER");
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
  void traceGlAdapterOnce() {
#ifndef _WIN32
    if (!this->glAdapterDiagnostics || this->glAdapterPrinted.exchange(true)) return;
    // Program-cache callbacks run inside renderer_gl program creation with its
    // context current, including surfaceless/offscreen EGL. Qt GLX inventory
    // may describe another provider. This diagnostic only reads driver strings.
    void* library = dlopen("libOpenGL.so.0", RTLD_LAZY | RTLD_LOCAL);
    bool printed = false;
    if (library) {
      using GetString = const unsigned char* (*)(unsigned int);
      const auto getString = reinterpret_cast<GetString>(dlsym(library, "glGetString"));
      if (getString) {
        const auto* vendor = getString(0x1f00);
        const auto* renderer = getString(0x1f01);
        const auto* version = getString(0x1f02);
        if (vendor && renderer && version) {
          std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_gl_adapter vendor=%s renderer=%s version=%s\n",
                       vendor, renderer, version);
          printed = true;
        }
      }
      dlclose(library);
    }
    if (!printed) this->glAdapterPrinted.store(false);
#endif
  }
  uint32_t cacheReadSize(uint64_t key) override {
    this->traceGlAdapterOnce();
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
    this->traceGlAdapterOnce();
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
    if (this->screenshotPixels.empty()) {
      this->screenshotError = "BGFX window screenshot incomplete: actual=" + std::to_string(width) + "x" + std::to_string(height) +
        " expected=" + std::to_string(this->screenshotWidth) + "x" + std::to_string(this->screenshotHeight) +
        " format=" + std::to_string(format) + " pitch=" + std::to_string(pitch) + " bytes=" + std::to_string(bytes);
    }
    this->screenshotPending = false;
    this->screenshotReady = true;
    this->screenshotCondition.notify_all();
  }

  std::string beginScreenshot(uint32_t width, uint32_t height)
  {
    std::lock_guard<std::mutex> guard(this->screenshotMutex);
    this->screenshotWidth = width; this->screenshotHeight = height;
    this->screenshotPixels.clear();this->screenshotError.clear();
    this->screenshotReady = false; this->screenshotPending = true;
    this->screenshotName = "coin-window-rgba-" + std::to_string(++this->screenshotSerial);
    return this->screenshotName;
  }

  void cancelScreenshot()
  {
    std::lock_guard<std::mutex> guard(this->screenshotMutex);
    this->screenshotPending = false;
  }

  bool takeScreenshot(std::vector<uint8_t> & output, std::string & error)
  {
    std::unique_lock<std::mutex> guard(this->screenshotMutex);
    if (!this->screenshotCondition.wait_for(guard, std::chrono::seconds(5),
          [this] { return this->screenshotReady; })) {
      this->screenshotPending = false;error = "BGFX window screenshot timed out";
      return false;
    }
    if (this->screenshotPixels.empty()) {error = this->screenshotError; return false;}
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

#ifdef COIN_WGPU_TEST_HOOKS
  void inject(bgfx::Fatal::Enum code, const char * message)
  {
    this->fatal("Coin fault injection", 0, code, message);
  }

private:
#endif
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
  bool glAdapterDiagnostics = false;
  std::atomic<bool> glAdapterPrinted{false};
  bool screenshotReady = false;
  uint64_t screenshotSerial = 0;
  std::string screenshotName;
  std::string screenshotError;
};

struct BgfxPortableProgram {
  size_t pins = 0;
  bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
  size_t bytes = 0;
  uint64_t used = 0;
};

struct BgfxPortableProgramCache {
  // API-thread owned, like all other BGFX resources. No target, scene or
  // runtime reference is retained by the cache. Counts alone are atomic for
  // opt-in lifecycle observations from an uninitialized probe's destructor.
  std::map<std::string, BgfxPortableProgram> programs;
  bgfx::UniformHandle uniforms[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
  bgfx::UniformHandle probeControl = BGFX_INVALID_HANDLE;
  size_t bytes = 0;
  uint64_t use = 0;
  std::atomic<size_t> programCount{0};
  std::atomic<unsigned> uniformCount{0};

  void clear(bool releaseHandles) {
    if (releaseHandles)
      for (const auto &entry : this->programs)
        if (bgfx::isValid(entry.second.program)) bgfx::destroy(entry.second.program);
    this->programs.clear();
    for (auto &uniform : this->uniforms) {
      if (releaseHandles && bgfx::isValid(uniform)) bgfx::destroy(uniform);
      uniform = BGFX_INVALID_HANDLE;
    }
    if (releaseHandles && bgfx::isValid(this->probeControl)) bgfx::destroy(this->probeControl);
    this->probeControl = BGFX_INVALID_HANDLE;
    this->bytes = 0;
    this->use = 0;
    this->programCount.store(0, std::memory_order_relaxed);
    this->uniformCount.store(0, std::memory_order_relaxed);
  }
};

struct SharedBgfxRuntime {
  std::mutex mutex;
  std::thread::id apiThread;
  std::shared_ptr<bgfx::CallbackI> callback;
  bgfx::RendererType::Enum renderer = bgfx::RendererType::Count;
  bgfx::TextureFormat::Enum backbufferColorFormat = bgfx::TextureFormat::Count;
  unsigned int references = 0;
  BgfxPortableProgramCache portable;
#if defined(__linux__) && !defined(__ANDROID__)
  // eglInitialize does not acquire a reference to an existing EGLDisplay.
  // Give the GL worker its own native connection so eglTerminate cannot
  // invalidate the application's contexts on its caller-owned connection.
  std::unique_ptr<Display, int (*)(Display *)> openGlDisplay{nullptr, XCloseDisplay};
#endif
  void releaseNativeDisplay() {
#if defined(__linux__) && !defined(__ANDROID__)
    // The render worker and all EGL objects must already be gone.
    this->openGlDisplay.reset();
#endif
  }
  uint64_t generation = 0;
  // Written only on the shared API thread. Program handles are allocated
  // immediately, while native GL compilation executes later on the worker.
  uint32_t weightedSamplesVerified = 0;
  uint32_t weightedSamplesRejected = 0;
  uint32_t peelingSamplesVerified = 0;
  uint32_t peelingSamplesRejected = 0;
  uint64_t programCommandsQueued = 0;
  uint64_t programCommandsMaterialized = 0;
  std::vector<bool> viewBlocks;
};

#ifdef COIN_WGPU_TEST_HOOKS
struct SubmitCountdownFault {
  std::mutex mutex;
  bool armed=false;
  uint64_t remaining=0;
};
#endif

struct LogicalDrawStats {
  uint32_t opaqueDraws = 0;
  uint32_t transparentDraws = 0;
  uint32_t pipelineChanges = 0;
  uint32_t materialChanges = 0;
  uint32_t textureChanges = 0;
  uint32_t lightingChanges = 0;
  bool opaqueOrderChanged = false;
};

struct LostReadback {
  CoinRenderReadbackTicket ticket;
  std::thread::id thread;
};

struct BgfxFrameProgramNeeds {
  bool ordinary = false;
  bool bump = false;
};

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
template <typename Items, typename Deferred, typename Additive>
static uint64_t bgfxDeferredViewCount(const Items & items, bool object, uint32_t sourceViews,
                                     Deferred deferred, Additive additive) {
  if (object) return 1;
  uint64_t views = 0; bool open = false, previousAdd = false;
  for (const auto & item : items) {
    if (!deferred(item)) continue;
    const bool add = additive(item);
    if (!open || previousAdd != add) { views += add ? 1 : sourceViews; open = true; previousAdd = add; }
  }
  return views;
}
template<typename Value>
static uint64_t bgfxVectorBytes(const std::vector<Value> & values)
{
  return uint64_t(values.size())*sizeof(Value);
}

constexpr bgfx::ViewId targetViewCount = 16;
const uint64_t peelTextureFlags = BGFX_TEXTURE_RT |
  BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
  BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
const uint64_t shadowTextureFlags = BGFX_TEXTURE_RT |
  BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
  BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
void bgfxTraceLifecycle(const void *backend, const char *stage, size_t programs,
                        unsigned uniforms, int64_t references = -1);
bgfx::TextureFormat::Enum coinBgfxTextureFormat(CoinRenderTextureFormat f);
uint64_t coinBgfxTextureFlags(CoinRenderTextureFormat f);
const bgfx::Memory * uploadVertices(CoinBgfxPlan & plan);
bool bgfxShadowBatchSupported(const CoinRenderFramePlan & frame,
                              const CoinRenderShadowPlan & plan,
                              std::string & diagnostic);
SharedBgfxRuntime & sharedRuntime();
#ifdef COIN_WGPU_TEST_HOOKS
bool consumeTestFault(const char * name);
SubmitCountdownFault & submitCountdownFault();
bool injectOutOfMemoryAfterSubmits();
void logicalSubmitSucceeded();
#define COIN_BGFX_CONSUME_TEST_FAULT(name) (::coin_bgfx_internal::consumeTestFault(name))
#define COIN_BGFX_TEST_OUT_OF_MEMORY() (::coin_bgfx_internal::injectOutOfMemoryAfterSubmits())
#define COIN_BGFX_TEST_SUBMIT_SUCCEEDED() (::coin_bgfx_internal::logicalSubmitSucceeded())
#else
// Discard fault arguments as well as helper calls, including in Debug builds.
#define COIN_BGFX_CONSUME_TEST_FAULT(name) false
#define COIN_BGFX_TEST_OUT_OF_MEMORY() false
#define COIN_BGFX_TEST_SUBMIT_SUCCEEDED() ((void)0)
#endif
uint64_t drawState(const CoinBgfxDraw & draw);
bool samePipelineState(const CoinBgfxDraw & lhs,
                       const CoinBgfxDraw & rhs);
bool sameTextureState(const CoinBgfxDraw & lhs,
                      const CoinBgfxDraw & rhs);
bool sameLightingState(const CoinBgfxDraw & lhs,
                       const CoinBgfxDraw & rhs);
LogicalDrawStats logicalDrawStats(
  const std::vector<CoinBgfxDraw> & original,
  const std::vector<CoinBgfxDraw> & encoded);
void copyLogicalDrawStats(const LogicalDrawStats & source,
                          bool groupingEnabled,
                          CoinBgfxPhaseSample & destination);
uint32_t pooledCapacity(size_t required);
bool setDrawScissor(const CoinBgfxDraw & draw, int targetWidth, int targetHeight);
uint64_t peelDrawState(const CoinBgfxDraw & draw, bool depthOnly);
uint32_t peelFixedSampleMask(uint32_t samples, uint32_t sample);
uint64_t peelSelectionState(const CoinBgfxDraw &draw);
bgfx::ProgramHandle createLayerProgram(const uint8_t * vertexData,
                                       uint32_t vertexBytes,
                                       const uint8_t * fragmentData,
                                       uint32_t fragmentBytes);
bool captureGpuPasses(uint32_t submittedFrame, CoinBgfxPhaseSample & sample);
std::map<uint64_t, std::shared_ptr<CoinBgfxBackend::AsyncEntry>> & asyncEntries();
bool sameTicket(const CoinRenderReadbackTicket & a, const CoinRenderReadbackTicket & b);
void releaseAsync(uint64_t token, bool failed = false);
bgfx::ProgramHandle createRasterImageProgram();
uint64_t weightedTextureFlags(uint32_t samples);
bool bgfxWeightedSamples(uint32_t samples, const bgfx::VertexLayout & layout,
                        bgfx::ViewId view, std::string & diagnostic, bool verify = true);
bool bgfxSampleInterpolationSupported(uint32_t samples, std::string & diagnostic);
bool bgfxExplicitSurfaceSamples();
bgfx::ProgramHandle bgfxTracePreparedProgram(bgfx::ProgramHandle program, const char *role,
                                             uint32_t samples);
bool bgfxNeedsFixedSurfacePrograms(const std::vector<CoinBgfxDraw> &draws);
BgfxFrameProgramNeeds bgfxFrameProgramNeeds(const CoinRenderFramePlan &frame);
bool bgfxShadowMultitexture(const CoinRenderFramePlan & frame);
bool bgfxCustomSampleDepth(const CoinBgfxDraw & draw);
bool bgfxPeelingSamples(uint32_t samples, const bgfx::VertexLayout & layout,
                        bgfx::ViewId view, std::string & diagnostic, bool verify = true);
CoinRenderSubmitResult bgfxRasterImageProfile(const CoinRenderFramePlan & frame);
CoinRenderSubmitResult bgfxTextureProfile(const CoinRenderFramePlan & frame);
bool bgfxCompositionViewBudget(const CoinRenderFramePlan & frame,
  const std::vector<CoinBgfxDraw> & draws, CoinBgfxTransparencyStrategy strategy,
  size_t shadowViews, bool direct, int width, int height,
  uint32_t outputViews, bool multisampled, std::string & diagnostic, uint32_t * requiredViews = nullptr);
bool bgfxMsaaFrameViewBudget(const CoinRenderFramePlan & frame,
  int width, int height, uint32_t outputViews, uint32_t samples, bool direct,
  std::string & diagnostic, uint32_t * requiredViews = nullptr);
void bgfxSplitSamplerViews(CoinBgfxPlan & plan);
bool bgfxBumpSnapshotFits(const CoinRenderFramePlan & frame);
bool bgfxSameSourceTextures(const CoinRenderFramePlan & frame,
                                  const CoinBgfxPlan & plan);
bool bgfxSameBumpTextures(const std::vector<CoinBgfxTexture> & a,
                                 const std::vector<CoinBgfxTexture> & b);
extern std::map<uint64_t, LostReadback> lostReadbacks;
extern uint64_t nextTicketToken;
extern std::vector<std::shared_ptr<CoinBgfxBackend::AsyncEntry>> failedReadbacks;

}

// Direct3D12 consumes the DXBC shader binaries generated by shaderc.
#ifdef _WIN32
#define COIN_BGFX_SHADER_DATA(name) (bgfx::getRendererType() == bgfx::RendererType::Direct3D12 ? name##_dx11 : bgfx::getRendererType() == bgfx::RendererType::OpenGL ? name##_glsl : name##_spirv)
#define COIN_BGFX_SHADER_SIZE(name) (bgfx::getRendererType() == bgfx::RendererType::Direct3D12 ? sizeof(name##_dx11) : bgfx::getRendererType() == bgfx::RendererType::OpenGL ? sizeof(name##_glsl) : sizeof(name##_spirv))
#else
#define COIN_BGFX_SHADER_DATA(name) (bgfx::getRendererType() == bgfx::RendererType::OpenGL ? name##_glsl : name##_spirv)
#define COIN_BGFX_SHADER_SIZE(name) (bgfx::getRendererType() == bgfx::RendererType::OpenGL ? sizeof(name##_glsl) : sizeof(name##_spirv))
#endif

#define COIN_BGFX_SURFACE_DATA(name) (this->portableSampling ? COIN_BGFX_SHADER_DATA(name##_portable) : COIN_BGFX_SHADER_DATA(name))
#define COIN_BGFX_SURFACE_SIZE(name) (this->portableSampling ? COIN_BGFX_SHADER_SIZE(name##_portable) : COIN_BGFX_SHADER_SIZE(name))

#endif
