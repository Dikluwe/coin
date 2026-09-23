#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuRenderTargetP.h"
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/rendering/SoWgpuNativeSurface.h>
#include "rendering/wgpu/SoWgpuFramePlan.h"
#include "rendering/wgpu/SoWgpuCpuReferenceBackend.h"

#if defined(HAVE_WGPU_RUST_BRIDGE)
#include "rendering/wgpu/SoWgpuRustBackend.h"
#include "rendering/wgpu/coin_wgpu_ffi.h"
#elif defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE)
#include "rendering/wgpu/SoWgpuNativeBackend.h"
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

static_assert(sizeof(SoWgpuNativeSurfaceDescriptor) >= 32, "SoWgpuNativeSurfaceDescriptor size check");

// SoWgpuRenderTargetP private implementation

SoWgpuRenderTargetP::SoWgpuRenderTargetP(const SbVec2i32 & sz)
  : kind(KIND_OFFSCREEN),
    status(SoWgpuRenderTarget::TARGET_READY),
    size(sz),
    generation(0),
    surfaceId(0),
    suspended(false),
    needsReconfigure(false),
    lastError("")
{
  this->resize(sz);
}

SoWgpuRenderTargetP::~SoWgpuRenderTargetP()
{
#if defined(HAVE_WGPU_RUST_BRIDGE)
  if (this->surfaceId != 0) {
    char errBuf[256] = {0};
    coin_wgpu_surface_destroy(this->surfaceId, errBuf, sizeof(errBuf));
    this->surfaceId = 0;
  }
#endif
  this->backend.reset();
}

bool
SoWgpuRenderTargetP::initWindow(const SoWgpuNativeSurfaceDescriptor & desc, const SbVec2i32 & fbSize)
{
  this->kind = KIND_WINDOW;
  this->nativeDesc = desc;
  this->surfaceId = 0;
  this->colorBuffer.clear();
  this->depthBuffer.clear();

#if !defined(HAVE_WGPU_RUST_BRIDGE)
  this->status = SoWgpuRenderTarget::TARGET_ERROR;
  this->lastError = "Native window surface targets are not supported by the RECORDING backend; RUST_BRIDGE backend is required.";
  return false;
#else
  if (desc.abiVersion != COIN_WGPU_NATIVE_SURFACE_ABI_VERSION) {
    this->status = SoWgpuRenderTarget::TARGET_ERROR;
    this->lastError = "Invalid ABI version in SoWgpuNativeSurfaceDescriptor: expected 1";
    return false;
  }
  if (desc.structSize != sizeof(SoWgpuNativeSurfaceDescriptor)) {
    this->status = SoWgpuRenderTarget::TARGET_ERROR;
    this->lastError = "Invalid structSize in SoWgpuNativeSurfaceDescriptor";
    return false;
  }
  if (desc.reserved != 0) {
    this->status = SoWgpuRenderTarget::TARGET_ERROR;
    this->lastError = "Reserved field must be 0 in SoWgpuNativeSurfaceDescriptor";
    return false;
  }

  if (desc.type == COIN_WGPU_SURFACE_XLIB) {
    if (desc.native.xlib.display == nullptr) {
      this->status = SoWgpuRenderTarget::TARGET_ERROR;
      this->lastError = "Null display pointer in Xlib surface descriptor";
      return false;
    }
    if (desc.native.xlib.window == 0) {
      this->status = SoWgpuRenderTarget::TARGET_ERROR;
      this->lastError = "Window ID must be non-zero in Xlib surface descriptor";
      return false;
    }
  } else if (desc.type == COIN_WGPU_SURFACE_WAYLAND) {
    this->status = SoWgpuRenderTarget::TARGET_ERROR;
    this->lastError = "Wayland surface descriptor not supported in Onda 1B";
    return false;
  } else if (desc.type == COIN_WGPU_SURFACE_WIN32) {
    this->status = SoWgpuRenderTarget::TARGET_ERROR;
    this->lastError = "Win32 surface descriptor not supported in Onda 1B";
    return false;
  } else if (desc.type == COIN_WGPU_SURFACE_APPKIT_LAYER) {
    this->status = SoWgpuRenderTarget::TARGET_ERROR;
    this->lastError = "AppKit layer surface descriptor not supported in Onda 1B";
    return false;
  } else {
    this->status = SoWgpuRenderTarget::TARGET_ERROR;
    this->lastError = "Unknown native surface type in SoWgpuNativeSurfaceDescriptor";
    return false;
  }

  if (fbSize[0] < 0 || fbSize[1] < 0) {
    this->status = SoWgpuRenderTarget::TARGET_ERROR;
    this->lastError = "Framebuffer dimensions must not be negative";
    return false;
  }

  if (fbSize[0] == 0 || fbSize[1] == 0) {
    this->size = fbSize;
    this->suspended = true;
    this->needsReconfigure = true;
    this->status = SoWgpuRenderTarget::TARGET_NOT_READY;
    return true;
  }

  this->size = fbSize;
  this->status = SoWgpuRenderTarget::TARGET_READY;
  this->lastError.clear();
  return true;
#endif
}

bool
SoWgpuRenderTargetP::resize(const SbVec2i32 & newSize)
{
  if (this->kind == KIND_WINDOW) {
    if (newSize[0] < 0 || newSize[1] < 0) {
      this->size = SbVec2i32(0, 0);
      this->status = SoWgpuRenderTarget::TARGET_ERROR;
      this->lastError = "Negative window dimensions are invalid";
      return false;
    }
    if (newSize[0] == 0 || newSize[1] == 0) {
      this->size = newSize;
      this->suspended = true;
      this->status = SoWgpuRenderTarget::TARGET_NOT_READY;
      this->lastError = "Window minimized (zero size)";
      return true;
    }
    this->size = newSize;
    this->suspended = false;
    this->needsReconfigure = true;
    this->status = SoWgpuRenderTarget::TARGET_READY;
    this->lastError.clear();
    return true;
  }

  // Offscreen target
  if (newSize[0] < 0 || newSize[1] < 0) {
    this->size = SbVec2i32(0, 0);
    this->colorBuffer.clear();
    this->depthBuffer.clear();
    this->status = SoWgpuRenderTarget::TARGET_ERROR;
    this->lastError = "Negative offscreen dimensions are invalid";
    return false;
  }
  if (newSize[0] == 0 || newSize[1] == 0) {
    this->size = newSize;
    this->colorBuffer.clear();
    this->depthBuffer.clear();
    this->status = SoWgpuRenderTarget::TARGET_NOT_READY;
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
    this->status = SoWgpuRenderTarget::TARGET_ERROR;
    this->lastError = "Target dimensions exceed maximum limits or overflow calculation";
    return false;
  }
  size_t pixelCount = static_cast<size_t>(w * h);
  try {
    this->colorBuffer.assign(pixelCount * 4, 0);
    this->depthBuffer.assign(pixelCount, 1.0f);
  } catch (const std::bad_alloc &) {
    this->size = SbVec2i32(0, 0);
    this->colorBuffer.clear();
    this->depthBuffer.clear();
    this->status = SoWgpuRenderTarget::TARGET_ERROR;
    this->lastError = "Out of memory allocating target buffers";
    return false;
  }
  this->size = newSize;
  this->status = SoWgpuRenderTarget::TARGET_READY;
  this->lastError.clear();
  return true;
}

void
SoWgpuRenderTargetP::clear(float r, float g, float b, float a, float depthVal)
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
SoWgpuRenderTargetP::readbackRGBA(std::vector<uint8_t> & outRgba) const
{
  outRgba = this->colorBuffer;
}

void
SoWgpuRenderTargetP::readbackDepth(std::vector<float> & outDepth) const
{
  outDepth = this->depthBuffer;
}

FrameExecutionResult
SoWgpuRenderTargetP::validateProfile(const FramePlan & frame, const SbVec2i32 & targetSize)
{
  std::string planDiag;
  if (!frame.isValid(&planDiag)) {
    return FrameExecutionResult{BackendStatus::BACKEND_ERROR, "Invalid FramePlan: " + planDiag};
  }

  for (size_t i = 0; i < frame.draws.size(); ++i) {
    const auto & d = frame.draws[i];
    if (d.topology != PrimitiveTopology::TRIANGLE_LIST &&
        d.topology != PrimitiveTopology::LINE_LIST &&
        d.topology != PrimitiveTopology::POINT_LIST) {
      std::ostringstream ss;
      ss << "UNSUPPORTED: Draw " << i << " topology is unsupported.";
      return FrameExecutionResult{BackendStatus::UNSUPPORTED, ss.str()};
    }

    if (d.renderStateSlot >= frame.renderStates.size()) {
      return FrameExecutionResult{BackendStatus::BACKEND_ERROR, "Invalid renderStateSlot in draw packet"};
    }

    const auto & rs = frame.renderStates[d.renderStateSlot];
    if (d.topology == PrimitiveTopology::LINE_LIST) {
      if (rs.lineWidth > 1.0f + 1e-4f) {
        std::ostringstream ss;
        ss << "UNSUPPORTED: Line width " << rs.lineWidth << " > 1.0 is unsupported in native line-list topology.";
        return FrameExecutionResult{BackendStatus::UNSUPPORTED, ss.str()};
      }
    } else if (d.topology == PrimitiveTopology::POINT_LIST) {
      if (rs.pointSize > 1.0f + 1e-4f) {
        std::ostringstream ss;
        ss << "UNSUPPORTED: Point size " << rs.pointSize << " > 1.0 is unsupported in native point-list topology.";
        return FrameExecutionResult{BackendStatus::UNSUPPORTED, ss.str()};
      }
    }

    if (rs.lightingSlot < frame.lightingStates.size()) {
      const auto & ls = frame.lightingStates[rs.lightingSlot];
      if (ls.lights.size() > COIN_WGPU_MAX_LIGHTS) {
        std::ostringstream ss;
        ss << "UNSUPPORTED: More than eight active lights (" << ls.lights.size() << ") in draw.";
        return FrameExecutionResult{BackendStatus::UNSUPPORTED, ss.str()};
      }
      for (size_t l = 0; l < ls.lights.size(); ++l) {
        if (ls.lights[l].type != LightType::DIRECTIONAL &&
            ls.lights[l].type != LightType::POINT &&
            ls.lights[l].type != LightType::SPOT) {
          std::ostringstream ss;
          ss << "UNSUPPORTED: Light " << l << " has unsupported type.";
          return FrameExecutionResult{BackendStatus::UNSUPPORTED, ss.str()};
        }
      }
    }

    if (rs.materialSlot < frame.materials.size()) {
      const auto & mat = frame.materials[rs.materialSlot];
      if (mat.transparency > 0.001f) {
        std::ostringstream ss;
        ss << "UNSUPPORTED: Draw " << i << " has transparency=" << mat.transparency << ". Wave 1 profile supports only opaque objects.";
        return FrameExecutionResult{BackendStatus::UNSUPPORTED, ss.str()};
      }
    }

    // Check viewport coverage: Wave 1A supports only single viewport covering entire target
    if (rs.viewportSlot < frame.viewports.size()) {
      const auto & vp = frame.viewports[rs.viewportSlot];
      if (vp.x != 0 || vp.y != 0 || vp.width != targetSize[0] || vp.height != targetSize[1]) {
        std::ostringstream ss;
        ss << "UNSUPPORTED: Draw " << i << " viewport (" << vp.x << "," << vp.y << " " << vp.width << "x" << vp.height
           << ") does not cover entire target (" << targetSize[0] << "x" << targetSize[1] << ").";
        return FrameExecutionResult{BackendStatus::UNSUPPORTED, ss.str()};
      }
    }

    // Safe bounds validation against 32-bit overflow (B05)
    const auto & geom = d.geometry;
    const size_t totalIndices = frame.indices.size();
    if (geom.firstIndex > totalIndices || geom.indexCount > (totalIndices - geom.firstIndex)) {
      return FrameExecutionResult{BackendStatus::BACKEND_ERROR, "Draw index range out of bounds in geometry"};
    }

    // Check materials referenced by vertices in this draw (B03: PER_VERTEX supported)
    uint32_t endIdx = geom.firstIndex + geom.indexCount;
    for (uint32_t idx = geom.firstIndex; idx < endIdx; ++idx) {
      uint32_t vIdx = frame.indices[idx];
      if (vIdx < frame.vertices.size()) {
        uint32_t mSlot = frame.vertices[vIdx].materialSlot;
        if (mSlot < frame.materials.size()) {
          if (frame.materials[mSlot].transparency > 0.001f) {
            std::ostringstream ss;
            ss << "UNSUPPORTED: Draw " << i << " references vertex with material transparency="
               << frame.materials[mSlot].transparency << ". Wave 1 profile supports only opaque objects.";
            return FrameExecutionResult{BackendStatus::UNSUPPORTED, ss.str()};
          }
        }
      }
    }
  }

  return FrameExecutionResult(BackendStatus::SUCCESS, "");
}

bool
SoWgpuRenderTargetP::validateProfile(const FramePlan & frame, std::string & outDiagnostic)
{
  SbVec2i32 sz(640, 480);
  if (!frame.viewports.empty()) {
    sz.setValue(frame.viewports[0].width, frame.viewports[0].height);
  }
  FrameExecutionResult res = validateProfile(frame, sz);
  if (res.status != BackendStatus::SUCCESS) {
    outDiagnostic = res.diagnostic;
    return false;
  }
  return true;
}

FrameExecutionResult
SoWgpuRenderTargetP::executeFrame(const FramePlan & frame)
{
  if (this->suspended || this->size[0] <= 0 || this->size[1] <= 0) {
    this->status = SoWgpuRenderTarget::TARGET_NOT_READY;
    this->lastError = "Target is suspended or has zero size";
    return FrameExecutionResult(BackendStatus::NOT_READY, this->lastError);
  }

  FrameExecutionResult val = this->validateProfile(frame, this->size);
  if (val.status != BackendStatus::SUCCESS) {
    this->lastError = val.diagnostic;
    return val;
  }

  if (this->status == SoWgpuRenderTarget::TARGET_LOST ||
      this->status == SoWgpuRenderTarget::TARGET_SURFACE_LOST ||
      this->status == SoWgpuRenderTarget::TARGET_NOT_READY ||
      !this->backend) {
    if (!this->backend) {
#if defined(HAVE_WGPU_RUST_BRIDGE)
      this->backend = std::unique_ptr<SoWgpuBackend>(new SoWgpuRustBackend());
#elif defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE)
      this->backend = std::unique_ptr<SoWgpuBackend>(new SoWgpuNativeBackend());
#else
      this->backend = std::unique_ptr<SoWgpuBackend>(new SoWgpuCpuReferenceBackend());
#endif
    }
    BackendStatus prep = this->backend->prepare(*this);
    if (prep != BackendStatus::SUCCESS) {
      std::string lastErr = this->backend->getLastError();
      this->lastError = lastErr;
      if (prep == BackendStatus::NOT_READY) {
        this->status = SoWgpuRenderTarget::TARGET_NOT_READY;
      } else {
        this->status = SoWgpuRenderTarget::TARGET_ERROR;
      }
      this->backend.reset();
      return FrameExecutionResult(prep, lastErr);
    }
    this->status = SoWgpuRenderTarget::TARGET_READY;
  }

  if (this->kind == KIND_OFFSCREEN) {
    size_t pixelCount = static_cast<size_t>(this->size[0] * this->size[1]);
    if (this->depthBuffer.size() != pixelCount) {
      this->depthBuffer.assign(pixelCount, 1.0f);
    }
  }

  SubmitResult res = this->backend->submit(frame, *this);
  if (res.status != BackendStatus::SUCCESS) {
    std::string lastErr = res.diagnostic.empty() ? (this->backend ? this->backend->getLastError() : std::string()) : res.diagnostic;
    this->lastError = lastErr;
    if (res.status == BackendStatus::DEVICE_LOST) {
      if (lastErr.empty()) lastErr = "WebGPU device lost during frame submission";
      this->lastError = lastErr;
      this->generation++;
      this->backend.reset();
      this->status = SoWgpuRenderTarget::TARGET_LOST;
      return SubmitResult(BackendStatus::DEVICE_LOST, lastErr, res.submissionSerial);
    } else if (res.status == BackendStatus::NOT_READY) {
      this->status = SoWgpuRenderTarget::TARGET_NOT_READY;
      return SubmitResult(BackendStatus::NOT_READY, lastErr, res.submissionSerial);
    } else if (res.status == BackendStatus::SURFACE_LOST) {
      this->status = SoWgpuRenderTarget::TARGET_SURFACE_LOST;
      return SubmitResult(BackendStatus::SURFACE_LOST, lastErr, res.submissionSerial);
    } else if (res.status == BackendStatus::OUT_OF_MEMORY) {
      this->status = SoWgpuRenderTarget::TARGET_ERROR;
      return SubmitResult(BackendStatus::OUT_OF_MEMORY, lastErr, res.submissionSerial);
    } else if (res.status == BackendStatus::UNSUPPORTED) {
      this->status = SoWgpuRenderTarget::TARGET_ERROR;
      return SubmitResult(BackendStatus::UNSUPPORTED, lastErr, res.submissionSerial);
    } else {
      this->status = SoWgpuRenderTarget::TARGET_ERROR;
      return SubmitResult(BackendStatus::BACKEND_ERROR, lastErr, res.submissionSerial);
    }
  }

  this->lastSubmissionSerial = res.submissionSerial;
  this->status = SoWgpuRenderTarget::TARGET_READY;
  this->lastError.clear();
  return res;
}

// Public SoWgpuRenderTarget class implementation

SoWgpuRenderTarget::SoWgpuRenderTarget(void)
{
}

SoWgpuRenderTarget::~SoWgpuRenderTarget(void)
{
}

SoWgpuRenderTarget *
SoWgpuRenderTarget::createOffscreen(const SbVec2i32 & size)
{
  SoWgpuRenderTarget * target = new SoWgpuRenderTarget();
  target->pimpl->resize(size);
  return target;
}

SoWgpuRenderTarget *
SoWgpuRenderTarget::createWindow(const SoWgpuNativeSurfaceDescriptor & descriptor,
                                 const SbVec2i32 & framebufferSize)
{
  SoWgpuRenderTarget * target = new SoWgpuRenderTarget();
  target->pimpl->initWindow(descriptor, framebufferSize);
  return target;
}

SoWgpuRenderTarget::Status
SoWgpuRenderTarget::getStatus(void) const
{
  return this->pimpl->status;
}

const char *
SoWgpuRenderTarget::getLastError(void) const
{
  return this->pimpl->lastError.c_str();
}

const SbVec2i32 &
SoWgpuRenderTarget::getSize(void) const
{
  return this->pimpl->size;
}

void
SoWgpuRenderTarget::readbackRGBA(std::vector<uint8_t> & outPixels) const
{
  this->pimpl->readbackRGBA(outPixels);
}

void
SoWgpuRenderTarget::readbackDepth(std::vector<float> & outDepth) const
{
  this->pimpl->readbackDepth(outDepth);
}

uint64_t
SoWgpuRenderTarget::getLastSubmissionSerial(void) const
{
  return this->pimpl->lastSubmissionSerial;
}

SbBool
SoWgpuRenderTarget::resize(const SbVec2i32 & size)
{
  return this->pimpl->resize(size) ? TRUE : FALSE;
}

SbBool
SoWgpuRenderTarget::getCacheTelemetry(SoWgpuCacheTelemetry & outTelemetry) const
{
#if defined(HAVE_WGPU_RUST_BRIDGE)
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
SoWgpuRenderTarget::pollDevice(void)
{
#if defined(HAVE_WGPU_RUST_BRIDGE)
  coin_wgpu_poll_device();
#endif
}
