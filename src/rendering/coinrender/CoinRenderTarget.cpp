#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderReadbackCore.h"
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderSelectionCore.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
#include "rendering/coinwgpu/CoinWgpuBackend.h"
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#elif defined(HAVE_COIN_DAWN) || defined(HAVE_COIN_WGPU_NATIVE)
#include "rendering/coinwgpu/CoinWgpuNativeBackend.h"
#elif defined(HAVE_COIN_BGFX)
#include "rendering/coinbgfx/CoinBgfxBackend.h"
#endif

#include <Inventor/SbMatrix.h>
#include <Inventor/SbVec2f.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbVec4f.h>
#include <Inventor/SbColor.h>

#include <vector>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <limits>
#include <cstring>
#include <new>
#include <cstdlib>
#include <atomic>
#include <mutex>
#include "rendering/coinrender/CoinRenderResourceCore.h"
namespace {
struct TargetRegistry {
  std::mutex mutex;
  std::vector<CoinRenderTargetP*> targets;
};
TargetRegistry& targetRegistry() {
  static auto* value = new TargetRegistry;
  return *value;
}
} // namespace

static_assert(sizeof(CoinRenderNativeSurfaceDescriptor) >= 32, "CoinRenderNativeSurfaceDescriptor size check");

static bool
prepareCpuDepthBuffer(const CoinRenderTargetP * target)
{
#if defined(HAVE_COIN_BGFX)
  // BGFX fills depth from the GPU; never synthesize a CPU depth result.
  // A target can still explicitly use the CPU reference backend in the same
  // binary, so decide from the prepared backend rather than the build alone.
  static const bool diagnosticFill = CoinRenderDiagnosticShell::diagnosticCpuDepthFill();
  if (diagnosticFill) return true;
  return target != NULL && target->backend.get() != NULL &&
         dynamic_cast<CoinBgfxBackend *>(target->backend.get()) == NULL;
#else
  (void)target;
  return true;
#endif
}

// CoinRenderTargetP private implementation

uint64_t CoinRenderTargetP::allocateResourceOwnerId() {
  static std::atomic<uint64_t> nextOwner{1};
  return nextOwner.fetch_add(1);
}

CoinRenderTargetP::CoinRenderTargetP(const SbVec2i32 & sz)
  : kind(KIND_OFFSCREEN),
    status(CoinRenderTarget::TARGET_READY),
    size(sz),
    generation(0),
    surfaceId(0),
    suspended(false),
    needsReconfigure(false),
    lastError("")
{
  this->resourceOwnerId = allocateResourceOwnerId();
  this->options = CoinRenderDiagnosticShell::renderOptions(this->optionsDiagnostic);
  this->resize(sz);
  auto& registry = targetRegistry();
  std::lock_guard<std::mutex> guard(registry.mutex);
  registry.targets.push_back(this);
}

CoinRenderTargetP::~CoinRenderTargetP()
{
  {
    auto& registry = targetRegistry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    registry.targets.erase(std::remove(registry.targets.begin(), registry.targets.end(), this),
                           registry.targets.end());
  }
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  if (this->surfaceId != 0) {
    char errBuf[256] = {0};
    coin_wgpu_surface_destroy(this->surfaceId, errBuf, sizeof(errBuf));
    this->surfaceId = 0;
  }
#endif
  this->backend.reset();
}

bool
CoinRenderTargetP::initWindow(const CoinRenderNativeSurfaceDescriptor & desc, const SbVec2i32 & fbSize)
{
  this->kind = KIND_WINDOW;
  this->nativeDesc = desc;
  this->surfaceId = 0;
  this->colorBuffer.clear();
  this->depthBuffer.clear();

#if !defined(HAVE_COIN_WGPU_RUST_BRIDGE) && !defined(HAVE_COIN_BGFX)
  this->status = CoinRenderTarget::TARGET_ERROR;
  this->lastError = "Native window surface targets require the RUST_BRIDGE or BGFX backend.";
  return false;
#else
  if (desc.abiVersion != COIN_RENDER_NATIVE_SURFACE_ABI_VERSION) {
    this->status = CoinRenderTarget::TARGET_ERROR;
    this->lastError = "Invalid ABI version in CoinRenderNativeSurfaceDescriptor: expected 1";
    return false;
  }
  if (desc.structSize != sizeof(CoinRenderNativeSurfaceDescriptor)) {
    this->status = CoinRenderTarget::TARGET_ERROR;
    this->lastError = "Invalid structSize in CoinRenderNativeSurfaceDescriptor";
    return false;
  }
  if (desc.reserved != 0) {
    this->status = CoinRenderTarget::TARGET_ERROR;
    this->lastError = "Reserved field must be 0 in CoinRenderNativeSurfaceDescriptor";
    return false;
  }

  if (desc.type == COIN_RENDER_SURFACE_XLIB) {
    if (desc.native.xlib.display == nullptr) {
      this->status = CoinRenderTarget::TARGET_ERROR;
      this->lastError = "Null display pointer in Xlib surface descriptor";
      return false;
    }
    if (desc.native.xlib.window == 0) {
      this->status = CoinRenderTarget::TARGET_ERROR;
      this->lastError = "Window ID must be non-zero in Xlib surface descriptor";
      return false;
    }
  } else if (desc.type == COIN_RENDER_SURFACE_WAYLAND) {
    this->status = CoinRenderTarget::TARGET_ERROR;
    this->lastError = "Wayland surface descriptor not supported in Onda 1B";
    return false;
  } else if (desc.type == COIN_RENDER_SURFACE_WIN32) {
    this->status = CoinRenderTarget::TARGET_ERROR;
    this->lastError = "Win32 surface descriptor not supported in Onda 1B";
    return false;
  } else if (desc.type == COIN_RENDER_SURFACE_APPKIT_LAYER) {
    this->status = CoinRenderTarget::TARGET_ERROR;
    this->lastError = "AppKit layer surface descriptor not supported in Onda 1B";
    return false;
  } else {
    this->status = CoinRenderTarget::TARGET_ERROR;
    this->lastError = "Unknown native surface type in CoinRenderNativeSurfaceDescriptor";
    return false;
  }

  if (fbSize[0] < 0 || fbSize[1] < 0) {
    this->status = CoinRenderTarget::TARGET_ERROR;
    this->lastError = "Framebuffer dimensions must not be negative";
    return false;
  }

  if (fbSize[0] == 0 || fbSize[1] == 0) {
    this->size = fbSize;
    this->suspended = true;
    this->needsReconfigure = true;
    this->status = CoinRenderTarget::TARGET_NOT_READY;
    return true;
  }

  this->size = fbSize;
  this->status = CoinRenderTarget::TARGET_READY;
  this->lastError.clear();
  return true;
#endif
}

bool
CoinRenderTargetP::resize(const SbVec2i32 & newSize)
{
  ++this->resourceGeneration;
  this->borrowedReadbackValid = false;
  this->windowReadbackRequested = false;
  this->synchronousReadbackValid = false;
  this->colorBuffer.clear();
  this->lastValidatedPlanRevision = 0;
  std::vector<uint8_t>().swap(this->spareColorBuffer);
  std::vector<float>().swap(this->spareDepthBuffer);
  if (this->kind == KIND_WINDOW) {
    if (newSize[0] < 0 || newSize[1] < 0) {
      this->size = SbVec2i32(0, 0);
      this->status = CoinRenderTarget::TARGET_ERROR;
      this->lastError = "Negative window dimensions are invalid";
      return false;
    }
    if (newSize[0] == 0 || newSize[1] == 0) {
      this->size = newSize;
      this->suspended = true;
      this->status = CoinRenderTarget::TARGET_NOT_READY;
      this->lastError = "Window minimized (zero size)";
      return true;
    }
    this->size = newSize;
    this->suspended = false;
    this->needsReconfigure = true;
    this->status = CoinRenderTarget::TARGET_READY;
    this->lastError.clear();
    return true;
  }

  // Offscreen target
  if (newSize[0] < 0 || newSize[1] < 0) {
    this->size = SbVec2i32(0, 0);
    this->colorBuffer.clear();
    this->depthBuffer.clear();
    this->status = CoinRenderTarget::TARGET_ERROR;
    this->lastError = "Negative offscreen dimensions are invalid";
    return false;
  }
  if (newSize[0] == 0 || newSize[1] == 0) {
    this->size = newSize;
    this->colorBuffer.clear();
    this->depthBuffer.clear();
    this->status = CoinRenderTarget::TARGET_NOT_READY;
    this->lastError.clear();
    return true;
  }

  // B06: Overflow protection in framebuffer dimensions and buffer allocation
  uint64_t w = static_cast<uint64_t>(newSize[0]);
  uint64_t h = static_cast<uint64_t>(newSize[1]);
  const uint64_t MAX_DIM = 16384;
  if (w > MAX_DIM || h > MAX_DIM || (w > SIZE_MAX / h) || (w * h > (SIZE_MAX / 4))) {
    this->size = SbVec2i32(0, 0);
    this->colorBuffer.clear();
    this->depthBuffer.clear();
    this->status = CoinRenderTarget::TARGET_ERROR;
    this->lastError = "Target dimensions exceed maximum limits or overflow calculation";
    return false;
  }
  if (this->directTextureOutput) {
    this->colorBuffer.clear();
    this->depthBuffer.clear();
  } else {
    size_t pixelCount = static_cast<size_t>(w * h);
    try {
      this->colorBuffer.assign(pixelCount * 4, 0);
      if (prepareCpuDepthBuffer(this)) {
        this->depthBuffer.assign(pixelCount, 1.0f);
      } else {
        this->depthBuffer.clear();
      }
    } catch (const std::bad_alloc &) {
      this->size = SbVec2i32(0, 0);
      this->colorBuffer.clear();
      this->depthBuffer.clear();
      this->status = CoinRenderTarget::TARGET_ERROR;
      this->lastError = "Out of memory allocating target buffers";
      return false;
    }
  }
  this->size = newSize;
  this->status = CoinRenderTarget::TARGET_READY;
  this->lastError.clear();
  return true;
}

void
CoinRenderTargetP::clear(float r, float g, float b, float a, float depthVal)
{
  uint8_t ur = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, r * 255.0f)));
  uint8_t ug = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, g * 255.0f)));
  uint8_t ub = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, b * 255.0f)));
  uint8_t ua = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, a * 255.0f)));

  size_t pixelCount = this->depthBuffer.size();
  for (size_t i = 0; i < pixelCount; ++i) {
    size_t cIdx = i * 4;
    if (cIdx + 3 < this->colorBuffer.size()) {
      this->colorBuffer[cIdx + 0] = ur;
      this->colorBuffer[cIdx + 1] = ug;
      this->colorBuffer[cIdx + 2] = ub;
      this->colorBuffer[cIdx + 3] = ua;
    }
    this->depthBuffer[i] = depthVal;
  }
}

void
CoinRenderTargetP::readbackRGBA(std::vector<uint8_t> & outRgba) const
{
  if (this->synchronousReadbackValid) outRgba = this->colorBuffer;
  else outRgba.clear();
}

void
CoinRenderTargetP::readbackDepth(std::vector<float> & outDepth) const
{
  if (this->synchronousReadbackValid && this->depthReadbackEnabled) outDepth = this->depthBuffer;
  else outDepth.clear();
}

CoinRenderFrameExecutionResult CoinRenderTargetP::validateProfile(const CoinRenderFramePlan& frame,
                                                                  const SbVec2i32& targetSize,
                                                                  bool deferUnresolvedAlpha) {
  std::string planDiag;
  if (!frame.isValid(&planDiag)) {
    return CoinRenderFrameExecutionResult{CoinRenderBackendStatus::BACKEND_ERROR, "Invalid CoinRenderFramePlan: " + planDiag};
  }
  std::vector<CoinRenderCompositionItem> compositionOrder;
  if (!coin_render_composition_order(frame, compositionOrder, planDiag, deferUnresolvedAlpha)) {
    return CoinRenderFrameExecutionResult{CoinRenderBackendStatus::UNSUPPORTED, planDiag};
  }

  for (size_t i = 0; i < frame.draws.size(); ++i) {
    const auto & d = frame.draws[i];
    if (d.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST &&
        d.topology != CoinRenderPrimitiveTopology::LINE_LIST &&
        d.topology != CoinRenderPrimitiveTopology::POINT_LIST) {
      std::ostringstream ss;
      ss << "UNSUPPORTED: Draw " << i << " topology is unsupported.";
      return CoinRenderFrameExecutionResult{CoinRenderBackendStatus::UNSUPPORTED, ss.str()};
    }

    if (d.renderStateSlot >= frame.renderStates.size()) {
      return CoinRenderFrameExecutionResult{CoinRenderBackendStatus::BACKEND_ERROR, "Invalid renderStateSlot in draw packet"};
    }

    const auto & rs = frame.renderStates[d.renderStateSlot];
    if (d.topology == CoinRenderPrimitiveTopology::LINE_LIST) {
      if (rs.lineWidth > 1.0f + 1e-4f) {
        std::ostringstream ss;
        ss << "UNSUPPORTED: Line width " << rs.lineWidth << " > 1.0 is unsupported in native line-list topology.";
        return CoinRenderFrameExecutionResult{CoinRenderBackendStatus::UNSUPPORTED, ss.str()};
      }
    } else if (d.topology == CoinRenderPrimitiveTopology::POINT_LIST) {
      if (rs.pointSize > 1.0f + 1e-4f) {
        std::ostringstream ss;
        ss << "UNSUPPORTED: Point size " << rs.pointSize << " > 1.0 is unsupported in native point-list topology.";
        return CoinRenderFrameExecutionResult{CoinRenderBackendStatus::UNSUPPORTED, ss.str()};
      }
    }

    if (rs.lightingSlot < frame.lightingStates.size()) {
      const auto & ls = frame.lightingStates[rs.lightingSlot];
      if (ls.lights.size() > COIN_RENDER_MAX_LIGHTS) {
        std::ostringstream ss;
        ss << "UNSUPPORTED: More than eight active lights (" << ls.lights.size() << ") in draw.";
        return CoinRenderFrameExecutionResult{CoinRenderBackendStatus::UNSUPPORTED, ss.str()};
      }
      for (size_t l = 0; l < ls.lights.size(); ++l) {
        if (ls.lights[l].type != CoinRenderLightType::DIRECTIONAL &&
            ls.lights[l].type != CoinRenderLightType::POINT &&
            ls.lights[l].type != CoinRenderLightType::SPOT) {
          std::ostringstream ss;
          ss << "UNSUPPORTED: Light " << l << " has unsupported type.";
          return CoinRenderFrameExecutionResult{CoinRenderBackendStatus::UNSUPPORTED, ss.str()};
        }
      }
    }

    // Origins may be negative (e.g. a NaviCube larger than its window).
    // Backends retain the original projection and clip rasterization to the target.
    if (rs.viewportSlot < frame.viewports.size()) {
      const auto & vp = frame.viewports[rs.viewportSlot];
      if (vp.width <= 0 || vp.height <= 0) {
        std::ostringstream ss;
        ss << "Invalid viewport for draw " << i << ": (" << vp.x << "," << vp.y << " " << vp.width << "x" << vp.height << ")";
        return CoinRenderFrameExecutionResult{CoinRenderBackendStatus::BACKEND_ERROR, ss.str()};
      }
    }

    // Safe bounds validation against 32-bit overflow (B05)
    const auto & geom = d.geometry;
    const size_t totalIndices = frame.indices.size();
    if (geom.firstIndex > totalIndices || geom.indexCount > (totalIndices - geom.firstIndex)) {
      return CoinRenderFrameExecutionResult{CoinRenderBackendStatus::BACKEND_ERROR, "Draw index range out of bounds in geometry"};
    }

  }

  return CoinRenderFrameExecutionResult(CoinRenderBackendStatus::SUCCESS, "");
}

bool
CoinRenderTargetP::validateProfile(const CoinRenderFramePlan & frame, std::string & outDiagnostic)
{
  SbVec2i32 sz(640, 480);
  if (!frame.viewports.empty()) {
    sz.setValue(frame.viewports[0].width, frame.viewports[0].height);
  }
  CoinRenderFrameExecutionResult res = validateProfile(frame, sz);
  if (res.status != CoinRenderBackendStatus::SUCCESS) {
    outDiagnostic = res.diagnostic;
    return false;
  }
  return true;
}

void
CoinRenderTargetP::detachedFromAction()
{
#if defined(HAVE_COIN_BGFX)
  // Preserve the existing target-switch policy of the shared BGFX runtime.
  if (dynamic_cast<CoinBgfxBackend *>(this->backend.get())) {
    this->backend.reset();
    ++this->resourceGeneration;
  }
#endif
}

std::unique_ptr<CoinRenderBackend> CoinRenderTargetP::createBackend() {
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  return std::unique_ptr<CoinRenderBackend>(new CoinWgpuBackend());
#elif defined(HAVE_COIN_DAWN) || defined(HAVE_COIN_WGPU_NATIVE)
  return std::unique_ptr<CoinRenderBackend>(new CoinWgpuNativeBackend());
#elif defined(HAVE_COIN_BGFX)
  return std::unique_ptr<CoinRenderBackend>(new CoinBgfxBackend());
#else
  return std::unique_ptr<CoinRenderBackend>(new CoinRenderCpuReferenceBackend());
#endif
}

CoinRenderBackendStatus CoinRenderTargetP::prepareBackend() {
  if (!this->backend)
    this->backend = createBackend();
  const auto result = this->backend->prepare(*this);
  if (result == CoinRenderBackendStatus::SUCCESS) {
    const auto domain = this->backend->resourceDomain();
    if (preparedDomain.device && (preparedDomain.device != domain.device ||
                                  preparedDomain.generation != domain.generation)) {
      ++resourceGeneration;
    }
    preparedDomain = domain;
  }
  return result;
}

CoinRenderSubmitResult CoinRenderTargetP::preflightSubmission(bool asynchronous) {
  if (!asynchronous)
    return {};
  if (kind != KIND_OFFSCREEN)
    return {CoinRenderBackendStatus::UNSUPPORTED, "Async readback requires an offscreen target"};
  if (suspended || size[0] <= 0 || size[1] <= 0)
    return {CoinRenderBackendStatus::NOT_READY, "Target is suspended or has zero size"};
  auto temporary = backend ? std::unique_ptr<CoinRenderBackend>() : createBackend();
  auto* selected = backend ? backend.get() : temporary.get();
  uint64_t jobs = 0, bytes = 0;
  if (!selected->readbackLoad(jobs, bytes))
    return {CoinRenderBackendStatus::UNSUPPORTED,
            "Backend cannot report asynchronous readback capacity"};
  const uint64_t requested = uint64_t(size[0]) * size[1] * (depthReadbackEnabled ? 8 : 4);
  if (!coin_render_readback_admitted(jobs, bytes, requested))
    return {CoinRenderBackendStatus::NOT_READY,
            "Readback budget exhausted: sixteen tickets or 128 MiB of pending output"};
  return {};
}

void CoinRenderTargetP::deviceLost() {
  if (backend && backend->requiresSharedRetirement()) {
    backend->retireLostReadbacks();
    auto& registry = targetRegistry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    for (auto* peer : registry.targets) {
      if (!peer->backend || !peer->backend->requiresSharedRetirement())
        continue;
      ++peer->generation;
      ++peer->resourceGeneration;
      peer->status = CoinRenderTarget::TARGET_LOST;
      peer->lastError = "Shared rendering device lost; resources retired";
      peer->backend.reset();
    }
  } else {
    ++generation;
    ++resourceGeneration;
    backend.reset();
    status = CoinRenderTarget::TARGET_LOST;
  }
}

namespace {
// Backends receive candidate buffers. A failed submission restores the exact
// previous allocations, including borrowed pointers, without copying pixels.
class ReadbackPublication {
public:
  explicit ReadbackPublication(CoinRenderTargetP& value) : target(value) {}
  void prepare() {
    if (target.directTextureOutput || (target.kind == CoinRenderTargetP::KIND_WINDOW &&
        !target.windowReadbackRequested)) return;
    // Allocate before swapping so bad_alloc cannot disturb published storage.
    const size_t colorBytes = target.kind == CoinRenderTargetP::KIND_WINDOW
      ? size_t(target.size[0]) * size_t(target.size[1]) * 4u
      : target.colorBuffer.size();
    target.spareColorBuffer.resize(colorBytes);
    target.spareDepthBuffer.resize(target.depthBuffer.size());
    target.colorBuffer.swap(target.spareColorBuffer);
    target.depthBuffer.swap(target.spareDepthBuffer);
    active = true;
  }
  ~ReadbackPublication() {
    if (active && !committed) {
      target.colorBuffer.swap(target.spareColorBuffer);
      target.depthBuffer.swap(target.spareDepthBuffer);
    }
  }
  void commit() { committed = true; }

private:
  CoinRenderTargetP& target;
  bool active = false, committed = false;
};
} // namespace

CoinRenderFrameExecutionResult
CoinRenderTargetP::executeFrame(const CoinRenderFramePlan & frame)
{
  return this->executeFrameInternal(frame, NULL,
    CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD, 0));
}

CoinRenderFrameExecutionResult
CoinRenderTargetP::executeFrame(const CoinRenderFramePlan & frame,
                                  const CoinRenderFrameReuseDecision & reuse)
{
  return this->executeFrameInternal(frame, NULL, reuse);
}

CoinRenderFrameExecutionResult
CoinRenderTargetP::executeFrameAsync(const CoinRenderFramePlan & frame,
                                       CoinRenderReadbackTicket & outTicket)
{
  outTicket = CoinRenderReadbackTicket{};
  return this->executeFrameInternal(frame, &outTicket,
    CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD, 0));
}

CoinRenderFrameExecutionResult
CoinRenderTargetP::executeFrameAsync(const CoinRenderFramePlan & frame,
                                       CoinRenderReadbackTicket & outTicket,
                                       const CoinRenderFrameReuseDecision & reuse)
{
  outTicket = CoinRenderReadbackTicket{};
  return this->executeFrameInternal(frame, &outTicket, reuse);
}

CoinRenderFrameExecutionResult
CoinRenderTargetP::executeFrameInternal(const CoinRenderFramePlan & frame,
                                          CoinRenderReadbackTicket * outTicket,
                                          const CoinRenderFrameReuseDecision & reuse)
{
  const bool captureWindow = this->kind == KIND_WINDOW && this->windowReadbackRequested;
  struct RequestReset {
    bool & request;
    ~RequestReset() { request = false; }
  } requestReset{this->windowReadbackRequested};
  if (!this->optionsDiagnostic.empty() ||
      !coin_render_valid_options(this->options, this->lastError))
    return CoinRenderFrameExecutionResult(
        CoinRenderBackendStatus::UNSUPPORTED,
        this->optionsDiagnostic.empty() ? this->lastError : this->optionsDiagnostic);
  if (outTicket && this->kind != KIND_OFFSCREEN) {
    return CoinRenderFrameExecutionResult(CoinRenderBackendStatus::UNSUPPORTED,
                                "applyAsync() requires an offscreen target");
  }
#if !defined(HAVE_COIN_WGPU_RUST_BRIDGE) && !defined(HAVE_COIN_BGFX)
  if (outTicket) {
    return CoinRenderFrameExecutionResult(CoinRenderBackendStatus::UNSUPPORTED,
                                "Asynchronous readback requires the Rust bridge backend");
  }
#endif
  if (this->suspended || this->size[0] <= 0 || this->size[1] <= 0) {
    this->status = CoinRenderTarget::TARGET_NOT_READY;
    this->lastError = "Target is suspended or has zero size";
    return CoinRenderFrameExecutionResult(CoinRenderBackendStatus::NOT_READY, this->lastError);
  }

  // Core/Wiring prove that camera_patch preserves the validated geometry,
  // draw structure and target viewport. A different base (including resize,
  // which clears lastValidatedPlanRevision) takes the full validation path.
  const bool validatedCameraPatch =
    reuse.kind == CoinRenderFrameReuseKind::CAMERA_PATCH &&
    reuse.baseRevision != 0 &&
    reuse.baseRevision == this->lastValidatedPlanRevision &&
    frame.revision != 0 &&
    frame.revision != reuse.baseRevision;
  if (!validatedCameraPatch &&
      (frame.revision == 0 || frame.revision != this->lastValidatedPlanRevision)) {
    CoinRenderFrameExecutionResult val = this->validateProfile(frame, this->size);
    if (val.status != CoinRenderBackendStatus::SUCCESS) {
      this->lastError = val.diagnostic;
      return val;
    }
  }
  const auto admission = preflightSubmission(outTicket != NULL);
  if (admission.status != CoinRenderBackendStatus::SUCCESS) {
    lastError = admission.diagnostic;
    return admission;
  }
  ReadbackPublication publication(*this);
  try {
    publication.prepare();
  } catch (const std::bad_alloc&) {
    return {CoinRenderBackendStatus::OUT_OF_MEMORY, "Cannot allocate candidate readback buffers"};
  }
  CoinRenderReadbackTicket candidateTicket{};
  CoinRenderReadbackTicket* submitTicket = outTicket ? &candidateTicket : NULL;

  if (this->status == CoinRenderTarget::TARGET_LOST ||
      this->status == CoinRenderTarget::TARGET_SURFACE_LOST ||
      this->status == CoinRenderTarget::TARGET_NOT_READY ||
      !this->backend) {
    CoinRenderBackendStatus prep = this->prepareBackend();
    if (prep != CoinRenderBackendStatus::SUCCESS) {
      std::string lastErr = this->backend->getLastError();
      this->lastError = lastErr;
      if (prep == CoinRenderBackendStatus::NOT_READY) {
        this->status = CoinRenderTarget::TARGET_NOT_READY;
      } else {
        this->status = CoinRenderTarget::TARGET_ERROR;
      }
      if (prep == CoinRenderBackendStatus::DEVICE_LOST)
        deviceLost();
      else
        this->backend.reset();
      this->lastError = lastErr;
      return CoinRenderFrameExecutionResult(prep, lastErr);
    }
    this->status = CoinRenderTarget::TARGET_READY;
  }

  if (prepareCpuDepthBuffer(this) && this->kind == KIND_OFFSCREEN &&
      !this->directTextureOutput) {
    size_t pixelCount = static_cast<size_t>(this->size[0] * this->size[1]);
    if (this->depthBuffer.size() != pixelCount) {
      this->depthBuffer.assign(pixelCount, 1.0f);
    }
  }

  CoinRenderSubmitResult res;
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  if (outTicket) {
    CoinWgpuBackend * rust = dynamic_cast<CoinWgpuBackend *>(this->backend.get());
    res = rust ? rust->submitAsync(frame, *this, *submitTicket, reuse)
               : CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED,
                                        "Asynchronous readback requires the Rust bridge backend");
  } else if (CoinWgpuBackend * rust =
               dynamic_cast<CoinWgpuBackend *>(this->backend.get())) {
    res = rust->submit(frame, *this, reuse);
  } else
#endif
  {
#if defined(HAVE_COIN_BGFX)
    if (CoinBgfxBackend * bgfx =
          dynamic_cast<CoinBgfxBackend *>(this->backend.get())) {
      res = outTicket ? bgfx->submitAsync(frame, *this, *submitTicket, reuse)
                      : bgfx->submit(frame, *this, reuse);
    } else
#endif
      res = outTicket ? CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, "The selected backend does not support asynchronous readback")
                      : this->backend->submit(frame, *this);
  }
  if (res.status == CoinRenderBackendStatus::SUCCESS &&
      (this->kind == KIND_OFFSCREEN || captureWindow) && !this->directTextureOutput) {
    const uint64_t pixels = uint64_t(this->size[0]) * this->size[1];
    const bool complete =
        outTicket
            ? coin_render_complete_readback_ticket(candidateTicket, this->size,
                                                   this->depthReadbackEnabled, res.submissionSerial)
            : this->colorBuffer.size() == pixels * 4 &&
                  (captureWindow || !this->depthReadbackEnabled ||
                   this->depthBuffer.size() == pixels);
    if (!complete)
      res = {CoinRenderBackendStatus::BACKEND_ERROR,
             "Backend returned an incomplete readback result"};
  }
  if (res.status != CoinRenderBackendStatus::SUCCESS) {
    if (candidateTicket.token)
      CoinRenderTarget::cancelReadback(candidateTicket);
    std::string lastErr = res.diagnostic.empty() ? (this->backend ? this->backend->getLastError() : std::string()) : res.diagnostic;
    this->lastError = lastErr;
    if (res.status == CoinRenderBackendStatus::DEVICE_LOST) {
      if (lastErr.empty()) lastErr = "Rendering device lost during frame submission";
      this->lastError = lastErr;
      deviceLost();
      this->lastError = lastErr;
      return CoinRenderSubmitResult(CoinRenderBackendStatus::DEVICE_LOST, lastErr, res.submissionSerial);
    } else if (res.status == CoinRenderBackendStatus::NOT_READY) {
      this->status = CoinRenderTarget::TARGET_NOT_READY;
      return CoinRenderSubmitResult(CoinRenderBackendStatus::NOT_READY, lastErr, res.submissionSerial);
    } else if (res.status == CoinRenderBackendStatus::SURFACE_LOST) {
      this->status = CoinRenderTarget::TARGET_SURFACE_LOST;
      return CoinRenderSubmitResult(CoinRenderBackendStatus::SURFACE_LOST, lastErr, res.submissionSerial);
    } else if (res.status == CoinRenderBackendStatus::OUT_OF_MEMORY) {
      this->status = CoinRenderTarget::TARGET_ERROR;
      return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, lastErr, res.submissionSerial);
    } else if (res.status == CoinRenderBackendStatus::UNSUPPORTED) {
      // A rejected frame does not invalidate the prepared GPU target or the
      // last published image. The next supported request may use this target.
      this->status = CoinRenderTarget::TARGET_READY;
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, lastErr, res.submissionSerial);
    } else {
      this->status = CoinRenderTarget::TARGET_ERROR;
      return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, lastErr, res.submissionSerial);
    }
  }

  const auto completedDomain = backend->resourceDomain();
  if (preparedDomain.device && (preparedDomain.device != completedDomain.device ||
                                preparedDomain.generation != completedDomain.generation))
    ++resourceGeneration;
  preparedDomain = completedDomain;
  if (this->kind == KIND_WINDOW && !captureWindow) {
    this->colorBuffer.clear();
    this->depthBuffer.clear();
  }
  publication.commit();
  if (outTicket)
    *outTicket = candidateTicket;
  this->lastValidatedPlanRevision = frame.revision;
  this->lastSubmissionSerial = res.submissionSerial;
  this->synchronousReadbackValid = (outTicket == NULL &&
    (this->kind == KIND_OFFSCREEN || captureWindow) && !this->directTextureOutput);
  this->borrowedReadbackValid = this->synchronousReadbackValid &&
    this->kind == KIND_OFFSCREEN;
  this->status = CoinRenderTarget::TARGET_READY;
  this->lastError.clear();
  return res;
}

// Public CoinRenderTarget class implementation

CoinRenderTarget::CoinRenderTarget(void)
{
}

CoinRenderTarget::~CoinRenderTarget(void)
{
}

CoinRenderTarget *
CoinRenderTarget::createOffscreen(const SbVec2i32 & size)
{
  CoinRenderTarget * target = new CoinRenderTarget();
  target->pimpl->resize(size);
  return target;
}

CoinRenderTarget *
CoinRenderTarget::createWindow(const CoinRenderNativeSurfaceDescriptor & descriptor,
                                 const SbVec2i32 & framebufferSize)
{
  CoinRenderTarget * target = new CoinRenderTarget();
  target->pimpl->initWindow(descriptor, framebufferSize);
  return target;
}

CoinRenderTarget* CoinRenderTarget::createOffscreen(const SbVec2i32& size,
                                                    const CoinRenderOptions& options) {
  CoinRenderTarget* target = new CoinRenderTarget();
  target->pimpl->options = options;
  target->pimpl->optionsDiagnostic.clear();
  target->pimpl->resize(size);
  return target;
}

CoinRenderTarget*
CoinRenderTarget::createWindow(const CoinRenderNativeSurfaceDescriptor& descriptor,
                               const SbVec2i32& size, const CoinRenderOptions& options) {
  CoinRenderTarget* target = new CoinRenderTarget();
  target->pimpl->options = options;
  target->pimpl->optionsDiagnostic.clear();
  if (!coin_render_valid_options(options, target->pimpl->lastError)) {
    target->pimpl->status = TARGET_ERROR;
    return target;
  }
  target->pimpl->initWindow(descriptor, size);
  return target;
}

const CoinRenderOptions& CoinRenderTarget::getOptions(void) const { return this->pimpl->options; }

CoinRenderTarget::Status
CoinRenderTarget::getStatus(void) const
{
  return this->pimpl->status;
}

const char *
CoinRenderTarget::getLastError(void) const
{
  return this->pimpl->lastError.c_str();
}

const SbVec2i32 &
CoinRenderTarget::getSize(void) const
{
  return this->pimpl->size;
}

SbBool
CoinRenderTarget::requestWindowReadbackRGBA(void)
{
  if (this->pimpl->kind != CoinRenderTargetP::KIND_WINDOW ||
      this->pimpl->status != TARGET_READY || this->pimpl->suspended ||
      this->pimpl->size[0] <= 0 || this->pimpl->size[1] <= 0) {
    this->pimpl->lastError = "Window readback requires a ready window target";
    return FALSE;
  }
  const uint64_t requested = uint64_t(this->pimpl->size[0]) *
    uint64_t(this->pimpl->size[1]) * 4u;
  if (!coin_render_readback_admitted(0, 0, requested)) {
    this->pimpl->lastError = "Window RGBA capture exceeds the 128 MiB readback budget";
    return FALSE;
  }
  this->pimpl->windowReadbackRequested = true;
  this->pimpl->lastError.clear();
  return TRUE;
}

void
CoinRenderTarget::readbackRGBA(std::vector<uint8_t> & outPixels) const
{
  this->pimpl->readbackRGBA(outPixels);
}

const uint8_t *
CoinRenderTarget::borrowRGBA(std::size_t & byteCount) const
{
  byteCount = 0;
  if (this->pimpl->kind != CoinRenderTargetP::KIND_OFFSCREEN ||
      !this->pimpl->synchronousReadbackValid ||
      !this->pimpl->borrowedReadbackValid ||
      this->pimpl->colorBuffer.empty()) {
    return NULL;
  }
  byteCount = this->pimpl->colorBuffer.size();
  return this->pimpl->colorBuffer.data();
}

SbBool
CoinRenderTarget::setDepthReadbackEnabled(SbBool enabled)
{
  if (this->pimpl->kind != CoinRenderTargetP::KIND_OFFSCREEN ||
      this->pimpl->directTextureOutput) {
    this->pimpl->lastError = "Depth readback policy requires a CPU-readable offscreen target";
    return FALSE;
  }
  const bool requested = enabled != FALSE;
  if (this->pimpl->depthReadbackEnabled != requested) {
    this->pimpl->depthReadbackEnabled = requested;
    this->pimpl->synchronousReadbackValid = false;
    this->pimpl->borrowedReadbackValid = false;
  }
  this->pimpl->lastError.clear();
  return TRUE;
}

SbBool
CoinRenderTarget::isDepthReadbackEnabled(void) const
{
  return this->pimpl->kind == CoinRenderTargetP::KIND_OFFSCREEN &&
         !this->pimpl->directTextureOutput && this->pimpl->depthReadbackEnabled
           ? TRUE : FALSE;
}

void
CoinRenderTarget::readbackDepth(std::vector<float> & outDepth) const
{
  this->pimpl->readbackDepth(outDepth);
}

CoinRenderTarget::ReadbackStatus
CoinRenderTarget::pollReadback(const CoinRenderReadbackTicket & ticket,
                                  std::vector<uint8_t> & outColor,
                                  std::vector<float> & outDepth,
                                  SbString * diagnostic)
{
  if (diagnostic) *diagnostic = "";
#if defined(HAVE_COIN_BGFX)
  return CoinBgfxBackend::pollReadback(ticket, outColor, outDepth, diagnostic);
#elif defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  const uint64_t pixels = uint64_t(ticket.width) * uint64_t(ticket.height);
  const uint64_t bytes = pixels * 4;
  const uint64_t rowPitch = (uint64_t(ticket.width) * 4 + 255) & ~uint64_t(255);
  if (ticket.token == 0 || ticket.width == 0 || ticket.height == 0 ||
      ticket.width > 16384 || ticket.height > 16384 ||
      ticket.colorFormat != 0 || ticket.colorBytes != bytes ||
      ticket.colorRowPitch != rowPitch ||
      (ticket.depthFormat != 0 && ticket.depthFormat != 1) ||
      (ticket.depthFormat == 0 && (ticket.depthBytes != 0 || ticket.depthRowPitch != 0)) ||
      (ticket.depthFormat == 1 && (ticket.depthBytes != bytes ||
          ticket.depthRowPitch != rowPitch))) {
    if (diagnostic) *diagnostic = "Invalid asynchronous readback ticket";
    return READBACK_INVALID_TICKET;
  }

  char error[512] = {0};
  const CoinWgpuStatus readiness =
    coin_wgpu_readback_query(ticket.token, error, sizeof(error));
  if (readiness != COIN_WGPU_OK) {
    if (diagnostic && error[0]) *diagnostic = error;
    switch (readiness) {
      case COIN_WGPU_NOT_READY: return READBACK_NOT_READY;
      case COIN_WGPU_INVALID_ARGUMENT: return READBACK_INVALID_TICKET;
      case COIN_WGPU_DEVICE_LOST: return READBACK_DEVICE_LOST;
      case COIN_WGPU_UNSUPPORTED: return READBACK_UNSUPPORTED;
      default: return READBACK_ERROR;
    }
  }

  std::vector<uint8_t> color;
  std::vector<float> depth;
  try {
    color.resize(static_cast<size_t>(ticket.colorBytes));
    if (ticket.depthFormat == 1) {
      depth.resize(static_cast<size_t>(ticket.depthBytes / 4));
    }
  } catch (const std::exception &) {
    if (diagnostic) *diagnostic = "Cannot allocate asynchronous readback outputs";
    return READBACK_ERROR;
  }
  error[0] = 0;
  const CoinWgpuStatus status = coin_wgpu_readback_poll(ticket.token,
    color.data(), color.size(), depth.empty() ? NULL : depth.data(),
    depth.size(), error, sizeof(error));
  if (status == COIN_WGPU_OK) {
    outColor.swap(color);
    outDepth.swap(depth);
    return READBACK_READY;
  }
  if (diagnostic && error[0]) *diagnostic = error;
  switch (status) {
    case COIN_WGPU_NOT_READY: return READBACK_NOT_READY;
    case COIN_WGPU_INVALID_ARGUMENT: return READBACK_INVALID_TICKET;
    case COIN_WGPU_DEVICE_LOST: return READBACK_DEVICE_LOST;
    case COIN_WGPU_UNSUPPORTED: return READBACK_UNSUPPORTED;
    default: return READBACK_ERROR;
  }
#else
  (void)ticket;
  (void)outColor;
  (void)outDepth;
  if (diagnostic) *diagnostic = "Asynchronous readback requires the Rust bridge backend";
  return READBACK_UNSUPPORTED;
#endif
}

SbBool
CoinRenderTarget::cancelReadback(const CoinRenderReadbackTicket & ticket)
{
#if defined(HAVE_COIN_BGFX)
  return CoinBgfxBackend::cancelReadback(ticket) ? TRUE : FALSE;
#elif defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  return ticket.token != 0 && coin_wgpu_readback_cancel(ticket.token) == COIN_WGPU_OK
    ? TRUE : FALSE;
#else
  (void)ticket;
  return FALSE;
#endif
}

uint64_t
CoinRenderTarget::getLastSubmissionSerial(void) const
{
  return this->pimpl->lastSubmissionSerial;
}

SbBool
CoinRenderTarget::resize(const SbVec2i32 & size)
{
  return this->pimpl->resize(size) ? TRUE : FALSE;
}

SbBool
CoinRenderTarget::getCacheTelemetry(CoinRenderCacheTelemetry & outTelemetry) const
{
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  CoinWgpuCacheStats stats;
  coin_wgpu_get_cache_stats(&stats);
  outTelemetry.cumulativeUploads = stats.cumulative_uploads;
  outTelemetry.cumulativeHits = stats.cumulative_hits;
  outTelemetry.cumulativeMisses = stats.cumulative_misses;
  outTelemetry.cumulativeUploadedBytes = stats.cumulative_uploaded_bytes;
  outTelemetry.frameUploadedBytes = stats.frame_uploaded_bytes;
  outTelemetry.frameUploads = stats.frame_uploads;
  outTelemetry.frameHits = stats.frame_hits;
  outTelemetry.activeEntries = stats.active_entries;
  outTelemetry.retiredEntries = stats.retired_entries;
  outTelemetry.completedSerial = stats.completed_serial;
  outTelemetry.submissionSerial = stats.submission_serial;
  return TRUE;
#else
  return FALSE;
#endif
}

void
CoinRenderTarget::pollDevice(void)
{
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  coin_wgpu_poll_device();
#endif
}
