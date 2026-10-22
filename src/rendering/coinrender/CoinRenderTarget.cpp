#include "rendering/coinrender/CoinRenderShaderModuleCore.h"
#include "rendering/coinrender/CoinRenderMultipassCore.h"
#include "rendering/coinrender/CoinRenderSamplingCore.h"
#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"
#include "rendering/coinrender/CoinRenderCubeTextureCore.h"
#include "rendering/coinrender/CoinRenderBumpMapCore.h"
#include <utility>
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

#include "rendering/coinrender/CoinRenderBackendRuntime.h"
#include "rendering/coinrender/CoinRenderNativeSurfaceCore.h"

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
#include <chrono>
#include <cstdio>
#include <cctype>
#include "rendering/coinrender/CoinRenderResourceCore.h"
#include "rendering/coinrender/CoinRenderLegacyColorCore.h"
#include "glue/simage_wrapper.h"
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
  const bool initialize = target->backend
    ? target->backend->initializesCpuDepthBuffer()
    : CoinRenderTargetP::compiledBackendInitializesCpuDepthBuffer();
  if (initialize) return true;
  static const bool diagnosticFill = CoinRenderDiagnosticShell::diagnosticCpuDepthFill();
  return diagnosticFill;
}

// CoinRenderTargetP private implementation

uint64_t CoinRenderTargetP::allocateResourceOwnerId() {
  static std::atomic<uint64_t> nextOwner{1};
  return nextOwner.fetch_add(1);
}

CoinRenderTargetP::CoinRenderTargetP(const SbVec2i32 & sz)
  : runtime(&backendRuntime()),
    kind(KIND_OFFSCREEN),
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
#if defined(__ANDROID__)
  // Serialize the idle transition with new target registration. The offscreen
  // peer keeps this registry nonempty during window TERM/INIT cycles.
  auto& registry = targetRegistry();
  std::lock_guard<std::mutex> guard(registry.mutex);
  registry.targets.erase(std::remove(registry.targets.begin(), registry.targets.end(), this),
                         registry.targets.end());
  this->runtime->destroySurface(*this);
  this->backend.reset();
  if (registry.targets.empty()) this->runtime->releaseIdleDevice();
#else
  {
    auto& registry = targetRegistry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    registry.targets.erase(std::remove(registry.targets.begin(), registry.targets.end(), this),
                           registry.targets.end());
  }
  this->runtime->destroySurface(*this);
  this->backend.reset();
#endif
}

bool
CoinRenderTargetP::initWindow(const CoinRenderNativeSurfaceDescriptor & desc, const SbVec2i32 & fbSize)
{
  this->kind = KIND_WINDOW;
  this->nativeDesc = desc;
  this->surfaceId = 0;
  this->colorBuffer.clear();
  this->depthBuffer.clear();

  std::string diagnostic;
  if (!this->runtime->supportsWindowTargets())
    diagnostic = "Native window surface targets require the RUST_BRIDGE or BGFX backend.";
  else {
    diagnostic = coin_render_surface_header_diagnostic(desc);
    if (diagnostic.empty()) diagnostic = this->runtime->surfaceTypeDiagnostic(desc.type);
    if (diagnostic.empty()) diagnostic = coin_render_surface_handles_diagnostic(desc);
  }
  if (!diagnostic.empty()) {
    this->status = CoinRenderTarget::TARGET_ERROR;
    this->lastError = diagnostic;
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
}

bool
CoinRenderTargetP::resize(const SbVec2i32 & newSize)
{
  // Hosts repeat the current extent before every redraw. Preserve a healthy
  // window publication and its resource generation on these no-op requests.
  if (this->kind == KIND_WINDOW && this->status == CoinRenderTarget::TARGET_READY &&
      !this->suspended && newSize == this->size) return true;
  ++this->resourceGeneration;
  this->lastSubmissionSerial = 0;
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

namespace {
const char * canonicalCoverageDiagnostic(const CoinRenderFramePlan & frame) {
  if (frame.rasterizationPolicy == COIN_RENDER_RASTER_CANONICAL_8BIT &&
      std::any_of(frame.draws.begin(), frame.draws.end(),
        [](const CoinRenderDrawPacket & draw) { return draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST; }))
    return "Canonical coverage requires triangle surfaces";
  return nullptr;
}
}

CoinRenderFrameExecutionResult CoinRenderTargetP::validateProfile(const CoinRenderFramePlan& frame,
                                                                  const SbVec2i32& targetSize,
                                                                  bool deferUnresolvedAlpha) {
  return validateProfileInternal(frame, targetSize, deferUnresolvedAlpha, nullptr);
}

const CoinRenderFramePreflight *
CoinRenderTargetP::submissionPreflight(const CoinRenderFramePlan & frame) const {
  return activePreflight && activePreflight->compositionFor(frame) ? activePreflight : nullptr;
}

CoinRenderFrameExecutionResult CoinRenderTargetP::validateProfileInternal(
    const CoinRenderFramePlan & frame, const SbVec2i32 & targetSize,
    bool deferUnresolvedAlpha, CoinRenderFramePreflight * preflight,
    const CoinRenderFramePreflight * capturedPreflight, bool allowCompositionBorrow,
    CoinRenderCompositionTransferTrace * transfers) {
  std::string volumeDiagnostic;
  if (!coin_render_validate_extended_bump(frame, volumeDiagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, volumeDiagnostic};
  if (!CoinRenderCubeTextureCore::validate(frame, volumeDiagnostic) ||
      !CoinRenderTextureSamplingCore::validVolumeFrame(frame, volumeDiagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, volumeDiagnostic};
  uint64_t shaderBytes = 0;
  std::string shaderDiagnostic;
  if (!coin_render_shader_resource_bytes(frame, shaderBytes, shaderDiagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, shaderDiagnostic};
  // The same admission applies to root targets and direct RTT producers.
  if (const char * diagnostic = canonicalCoverageDiagnostic(frame))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  std::string planDiag;
  const auto * capturedOrder = !deferUnresolvedAlpha && capturedPreflight
    ? capturedPreflight->compositionFor(frame) : nullptr;
  if (!capturedOrder && !frame.isValid(&planDiag)) {
    return CoinRenderFrameExecutionResult{CoinRenderBackendStatus::BACKEND_ERROR, "Invalid CoinRenderFramePlan: " + planDiag};
  }
  std::vector<CoinRenderCompositionItem> compositionOrder;
  bool opaqueIdentity = false;
  if (!capturedOrder &&
      !coin_render_composition_order(frame, compositionOrder, planDiag, deferUnresolvedAlpha,
                                    preflight ? &opaqueIdentity : nullptr)) {
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

  if (preflight) {
    const auto qualifyBegin = transfers ? transfers->begin() : CoinRenderCompositionTransferTrace::Clock::time_point{};
    const bool loan = capturedOrder && allowCompositionBorrow && coin_render_composition_borrow_enabled() &&
      capturedPreflight->opaqueCompositionFor(frame);
    if (transfers) transfers->qualifyMs += transfers->elapsed(qualifyBegin);
    // Only executeFrameInternal can request this loan. No persistent receipt
    // gains a lender pointer, and activation is rechecked after admission.
    if (loan) return CoinRenderFrameExecutionResult(CoinRenderBackendStatus::SUCCESS, "");
    const auto copyBegin = transfers ? transfers->begin() : CoinRenderCompositionTransferTrace::Clock::time_point{};
    if (capturedOrder) {
      preflight->order = *capturedOrder;
      if (transfers) transfers->copiedItems = capturedOrder->size();
      opaqueIdentity = capturedPreflight->opaqueCompositionFor(frame) != nullptr;
    } else {
      preflight->order = std::move(compositionOrder);
      if (transfers) transfers->computedItems = preflight->order.size();
    }
    if (transfers) transfers->copyMs += transfers->elapsed(copyBegin);
    preflight->frame = &frame;
    preflight->revision = frame.revision;
    preflight->transparency = frame.transparency;
    preflight->opaqueIdentity = opaqueIdentity;
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
  if (this->backend && this->backend->resetOnActionDetach()) {
    this->backend.reset();
    ++this->resourceGeneration;
  }
}

bool
CoinRenderTargetP::supportsOffscreenShadows(bool asynchronous) const
{
  if ((this->kind != KIND_OFFSCREEN && this->kind != KIND_WINDOW) ||
      this->directTextureOutput || asynchronous)
    return false;
  if (this->backend) return this->backend->supportsOffscreenShadows();
  // An unprepared target uses the compiled connector's implementation facts.
  // Constructing a connector does not create GPU resources or probe hardware.
  const auto candidate = createBackend();
  return candidate->supportsOffscreenShadows();
}

bool
CoinRenderTargetP::supportsOpaqueBump(bool asynchronous) const
{
  if ((this->kind != KIND_OFFSCREEN && this->kind != KIND_WINDOW) ||
      this->directTextureOutput || asynchronous)
    return false;
  if (this->backend) return this->backend->supportsOpaqueBump();
  const auto candidate = createBackend();
  return candidate->supportsOpaqueBump();
}

bool
CoinRenderTargetP::supportsPortableShaderModules() const
{
  if (this->backend) return this->backend->isGpuBackend();
  // Lazy targets have no executor yet. Inspect the compiled connector without
  // initializing it, matching the existing shadow/bump capability queries.
  // Do not store it here: normal prepare must still initialize the real target.
  const auto candidate = createBackend();
  return candidate && candidate->isGpuBackend();
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
CoinRenderTargetP::executeFrame(const CoinRenderFramePlan & frame,
                                  const CoinRenderFrameReuseDecision & reuse,
                                  const CoinRenderFramePreflight * capturedPreflight)
{
  return this->executeFrameInternal(frame, NULL, reuse, capturedPreflight);
}

CoinRenderFrameExecutionResult
CoinRenderTargetP::executeFrameAsync(const CoinRenderFramePlan & frame,
                                       CoinRenderReadbackTicket & outTicket)
{
  return this->executeFrameInternal(frame, &outTicket,
    CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD, 0));
}

CoinRenderFrameExecutionResult
CoinRenderTargetP::executeFrameAsync(const CoinRenderFramePlan & frame,
                                       CoinRenderReadbackTicket & outTicket,
                                       const CoinRenderFrameReuseDecision & reuse)
{
  return this->executeFrameInternal(frame, &outTicket, reuse);
}

CoinRenderFrameExecutionResult
CoinRenderTargetP::executeFrameAsync(const CoinRenderFramePlan & frame,
                                       CoinRenderReadbackTicket & outTicket,
                                       const CoinRenderFrameReuseDecision & reuse,
                                       const CoinRenderFramePreflight * capturedPreflight)
{
  return this->executeFrameInternal(frame, &outTicket, reuse, capturedPreflight);
}

CoinRenderSubmitResult
CoinRenderTargetP::preflightMultipass(const CoinRenderFramePlan & frame,
    bool asynchronous, const CoinRenderRttPlan * graph) {
  if (frame.numPasses == 1) return {};
  std::unique_ptr<CoinRenderBackend> inspector;
  CoinRenderBackend * selected = this->backend.get();
  if (!selected) { inspector = createBackend(); selected = inspector.get(); }
  selected->configureMultipassTarget(*this);
  return selected->preflightMultipassOutput(frame, *this, asynchronous, graph);
}

CoinRenderFrameExecutionResult
CoinRenderTargetP::executeFrameInternal(const CoinRenderFramePlan & frame,
                                          CoinRenderReadbackTicket * outTicket,
                                          const CoinRenderFrameReuseDecision & reuse,
                                          const CoinRenderFramePreflight * capturedPreflight)
{
  using PhaseClock = std::chrono::steady_clock;
  const auto phaseBegin = PhaseClock::now();
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
  // Private revisions are hints, not proof that a transported atlas is intact.
  if (!CoinRenderCubeTextureCore::validate(frame, this->lastError) ||
      !CoinRenderTextureSamplingCore::validVolumeFrame(frame, this->lastError))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  const auto outputAdmission = this->preflightMultipass(frame, outTicket != nullptr);
  if (outputAdmission.status != CoinRenderBackendStatus::SUCCESS) return outputAdmission;
  if (!CoinRenderMultipassCore::projectionsFinite(frame))
    return {CoinRenderBackendStatus::UNSUPPORTED, "Multipass translated projection is non-finite or invalid"};
  if (frame.numPasses > 1 && frame.multipassBufferBudget == 0)
    return {CoinRenderBackendStatus::OUT_OF_MEMORY, "Multipass attachment budget is zero"};
  if (this->backend) {
    const auto admission = this->backend->preflightMultipass(frame, this->size);
    if (admission.status != CoinRenderBackendStatus::SUCCESS) return admission;
  }
  if (frame.hasPortableShaderDraws() && !this->supportsPortableShaderModules())
    return {CoinRenderBackendStatus::UNSUPPORTED,
            "Portable shader module execution is unavailable in this backend"};
  if (this->options.sampleCount > 1) {
#if !defined(HAVE_COIN_WGPU_RUST_BRIDGE) && !defined(HAVE_COIN_BGFX)
    return {CoinRenderBackendStatus::UNSUPPORTED,
            "MSAA target requires the wgpu Rust bridge or BGFX in this profile"};
#endif
    if (this->kind != KIND_OFFSCREEN && this->kind != KIND_WINDOW)
      return {CoinRenderBackendStatus::UNSUPPORTED,
              "MSAA currently requires an offscreen or window target"};
  }
  if (frame.rasterizationPolicy != this->options.rasterizationPolicy)
    return {CoinRenderBackendStatus::UNSUPPORTED, "Frame rasterization policy differs from the immutable target policy"};
  if (const char * diagnostic = canonicalCoverageDiagnostic(frame))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  if (frame.textureSamplingPolicy != this->options.textureSamplingPolicy ||
      !coin_render_valid_sampling(frame, this->lastError)) {
    if (frame.textureSamplingPolicy != this->options.textureSamplingPolicy)
      this->lastError = "Frame sampling policy differs from the immutable target policy";
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  }
  if (outTicket && this->kind != KIND_OFFSCREEN) {
    return CoinRenderFrameExecutionResult(CoinRenderBackendStatus::UNSUPPORTED,
                                "applyAsync() requires an offscreen target");
  }
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
  CoinRenderFramePreflight preflight;
  CoinRenderCompositionTransferTrace compositionTransfers("target_composition_copy", "target");
  // A capture receipt skips only the two common passes. Target profile and
  // submission checks still run, and a mismatched receipt validates in full.
  // Texture declarations and their sampler/matrix bindings remain untrusted
  // across private revision reuse, including ordinary stored 2D sources.
  if (capturedPreflight || !frame.textures.empty() || !frame.bumpImages.empty() ||
      !frame.bumpCoords.empty() || !frame.bumpTransforms.empty() || !frame.bumpTangents.empty() || (!validatedCameraPatch &&
      (frame.revision == 0 || frame.revision != this->lastValidatedPlanRevision))) {
    CoinRenderFrameExecutionResult val =
      validateProfileInternal(frame, this->size, false, &preflight, capturedPreflight, true, &compositionTransfers);
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
  struct PreflightScope {
    const CoinRenderFramePreflight * & active;
    const CoinRenderFramePreflight * previous;
    ~PreflightScope() { active = previous; }
  } preflightScope{this->activePreflight, this->activePreflight};
  this->activePreflight = preflight.compositionFor(frame) ? &preflight : nullptr;
  const auto loanBegin = compositionTransfers.begin();
  if (!this->activePreflight && coin_render_composition_borrow_enabled() && capturedPreflight &&
      capturedPreflight->opaqueCompositionFor(frame)) {
    this->activePreflight = capturedPreflight;
    compositionTransfers.borrowedItems = capturedPreflight->opaqueCompositionFor(frame)->size();
  }
  compositionTransfers.qualifyMs += compositionTransfers.elapsed(loanBegin);
  const auto phaseValidated = PhaseClock::now();
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

  // Check the executor actually prepared above as well as its earlier pure
  // implementation fact. A CPU/recording executor must never ignore the AST.
  if (frame.hasPortableShaderDraws() && (!this->backend || !this->backend->isGpuBackend()))
    return {CoinRenderBackendStatus::UNSUPPORTED,
            "Prepared backend does not execute portable shader modules"};

  if (prepareCpuDepthBuffer(this) && this->kind == KIND_OFFSCREEN &&
      !this->directTextureOutput) {
    size_t pixelCount = static_cast<size_t>(this->size[0] * this->size[1]);
    if (this->depthBuffer.size() != pixelCount) {
      this->depthBuffer.assign(pixelCount, 1.0f);
    }
  }

  const auto multipassAdmission = this->backend->preflightMultipass(frame, this->size);
  if (multipassAdmission.status != CoinRenderBackendStatus::SUCCESS) return multipassAdmission;
  const auto phasePrepared = PhaseClock::now();
  CoinRenderSubmitResult res = outTicket
    ? this->backend->submitAsync(frame, *this, *submitTicket, reuse)
    : this->backend->submit(frame, *this, reuse);
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
      // The injected MSAA failure occurs after encoding but before queue
      // submission. No device resource or published readback was changed, so
      // the same target can accept a new frame once the fault is cleared.
      // Keep genuine allocation failures on the existing terminal path.
      this->status = lastErr == "Injected MSAA failure after encoding, before queue submission"
        ? CoinRenderTarget::TARGET_READY : CoinRenderTarget::TARGET_ERROR;
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
  if (CoinRenderDiagnosticShell::phaseTracingEnabled()) {
    const auto now = PhaseClock::now();
    const auto ms = [](PhaseClock::time_point a, PhaseClock::time_point b) {
      return std::chrono::duration<double, std::milli>(b - a).count();
    };
    std::fprintf(stderr, "COIN_RENDER_PHASE target validation_ms=%.6f prepare_ms=%.6f submit_ms=%.6f\n",
      ms(phaseBegin, phaseValidated), ms(phaseValidated, phasePrepared), ms(phasePrepared, now));
  }
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
  if (options.sampleCount > 1) target->pimpl->depthReadbackEnabled = false;
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

SbBool
CoinRenderTarget::readbackLegacyComponents(CoinRenderLegacyComponents components,
                                           std::vector<uint8_t> & outPixels) const
{
  outPixels.clear();
  const CoinRenderTargetP & target = this->pimpl.get();
  if (target.kind != CoinRenderTargetP::KIND_OFFSCREEN ||
      !target.synchronousReadbackValid || target.directTextureOutput ||
      target.size[0] <= 0 || target.size[1] <= 0 ||
      uint64_t(target.colorBuffer.size()) !=
        uint64_t(target.size[0]) * uint64_t(target.size[1]) * 4u)
    return FALSE;
  return coin_render_convert_legacy_components(target.colorBuffer.data(),
    static_cast<uint32_t>(target.size[0]), static_cast<uint32_t>(target.size[1]),
    components, outPixels) ? TRUE : FALSE;
}

SbBool
CoinRenderTarget::encodeLegacyRGB(CoinRenderLegacyComponents components,
                                  std::vector<uint8_t> & outBytes) const
{
  outBytes.clear();
  std::vector<uint8_t> pixels;
  if (!this->readbackLegacyComponents(components, pixels)) return FALSE;
  const SbVec2i32 & size = this->pimpl->size;
  return coin_render_encode_legacy_rgb(pixels.data(),
    static_cast<uint32_t>(size[0]), static_cast<uint32_t>(size[1]),
    components, outBytes) ? TRUE : FALSE;
}

SbBool
CoinRenderTarget::encodeLegacyPostScript(CoinRenderLegacyComponents components,
                                         const SbVec2f & printSize, float dpi,
                                         std::vector<uint8_t> & outBytes) const
{
  outBytes.clear();
  std::vector<uint8_t> pixels;
  if (!this->readbackLegacyComponents(components, pixels)) return FALSE;
  const SbVec2i32 & size = this->pimpl->size;
  return coin_render_encode_legacy_postscript(pixels.data(),
    static_cast<uint32_t>(size[0]), static_cast<uint32_t>(size[1]),
    components, printSize, dpi, outBytes) ? TRUE : FALSE;
}

SbBool
CoinRenderTarget::writeLegacyImageFile(const SbString & filename,
                                       const SbString & extension,
                                       CoinRenderLegacyComponents components) const
{
  if (!filename.getLength() || !extension.getLength()) return FALSE;
  std::string codecName = extension.getString();
  std::transform(codecName.begin(), codecName.end(), codecName.begin(),
    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const bool tiffLA = components == CoinRenderLegacyComponents::LUMINANCE_TRANSPARENCY &&
    (codecName == "tiff" || codecName == "tif");
  const simage_wrapper_t * codec = simage_wrapper();
  if (!codec->available || !codec->versionMatchesAtLeast(1, 1, 0) ||
      !codec->simage_check_save_supported || !codec->simage_save_image ||
      !codec->simage_check_save_supported(extension.getString())) return FALSE;
  std::vector<uint8_t> pixels;
  if (!this->readbackLegacyComponents(components, pixels)) return FALSE;
  const SbVec2i32 & size = this->pimpl->size;
  if (tiffLA) {
    // simage's TIFF writer accepts two-channel LA, but treats its alpha bytes
    // as grayscale pixels. Store equivalent RGBA instead to preserve both
    // channels in a TIFF that its reader can round-trip.
    std::vector<uint8_t> rgba(pixels.size() * 2);
    for (size_t pixel = 0; pixel < pixels.size() / 2; ++pixel) {
      rgba[pixel * 4] = rgba[pixel * 4 + 1] = rgba[pixel * 4 + 2] = pixels[pixel * 2];
      rgba[pixel * 4 + 3] = pixels[pixel * 2 + 1];
    }
    return codec->simage_save_image(filename.getString(), rgba.data(),
      size[0], size[1], 4, extension.getString()) ? TRUE : FALSE;
  }
  return codec->simage_save_image(filename.getString(), pixels.data(),
    size[0], size[1], static_cast<int>(components), extension.getString()) ? TRUE : FALSE;
}

SbBool
CoinRenderTarget::readbackCenterSampledRGBA(unsigned int scale,
                                             std::vector<uint8_t> & outPixels) const
{
  outPixels.clear();
  const SbVec2i32 & size = this->pimpl->size;
  if (this->pimpl->kind != CoinRenderTargetP::KIND_OFFSCREEN ||
      !this->pimpl->synchronousReadbackValid ||
      this->pimpl->directTextureOutput ||
      scale == 0 || (scale & 1u) == 0 ||
      size[0] <= 0 || size[1] <= 0 ||
      uint64_t(size[0]) % scale != 0 || uint64_t(size[1]) % scale != 0 ||
      this->pimpl->colorBuffer.size() !=
        uint64_t(size[0]) * uint64_t(size[1]) * 4u) return FALSE;

  const size_t width = size_t(size[0]) / scale;
  const size_t height = size_t(size[1]) / scale;
  outPixels.resize(width * height * 4u);
  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width; ++x) {
      const size_t source = ((y * scale + scale / 2u) * size_t(size[0]) +
                             x * scale + scale / 2u) * 4u;
      const size_t destination = (y * width + x) * 4u;
      for (size_t channel = 0; channel < 4; ++channel) {
        outPixels[destination + channel] = this->pimpl->colorBuffer[source + channel];
      }
    }
  }
  return TRUE;
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
  return CoinRenderTargetP::backendRuntime().pollReadback(ticket, outColor, outDepth, diagnostic);
}

SbBool
CoinRenderTarget::cancelReadback(const CoinRenderReadbackTicket & ticket)
{
  return CoinRenderTargetP::backendRuntime().cancelReadback(ticket) ? TRUE : FALSE;
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
  return this->pimpl->runtime->cacheTelemetry(outTelemetry) ? TRUE : FALSE;
}

void
CoinRenderTarget::pollDevice(void)
{
  CoinRenderTargetP::backendRuntime().pollDevice();
}
