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

// SoWgpuRenderTargetP private implementation

SoWgpuRenderTargetP::SoWgpuRenderTargetP(const SbVec2i32 & sz)
  : kind(KIND_OFFSCREEN),
    status(SoWgpuRenderTarget::TARGET_READY),
    size(0, 0),
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
  if (desc.platform == SO_WGPU_SURFACE_PLATFORM_NONE || desc.window_handle == 0) {
    this->status = SoWgpuRenderTarget::TARGET_ERROR;
    this->lastError = "Invalid native surface descriptor: missing platform or window handle";
    return false;
  }
  if (fbSize[0] <= 0 || fbSize[1] <= 0) {
    this->size = fbSize;
    this->status = SoWgpuRenderTarget::TARGET_NOT_READY;
    this->lastError = "Window surface framebuffer size is not ready (zero or negative)";
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

FrameExecutionResult
SoWgpuRenderTargetP::validateProfile(const FramePlan & frame, const SbVec2i32 & targetSize)
{
  std::string planDiag;
  if (!frame.isValid(&planDiag)) {
    return FrameExecutionResult{BackendStatus::BACKEND_ERROR, "Invalid FramePlan: " + planDiag};
  }

  for (size_t i = 0; i < frame.draws.size(); ++i) {
    const auto & d = frame.draws[i];
    if (d.topology != PrimitiveTopology::TRIANGLE_LIST) {
      std::ostringstream ss;
      ss << "UNSUPPORTED: Draw " << i << " topology is not TRIANGLE_LIST. Wave 1 profile requires TRIANGLE_LIST.";
      return FrameExecutionResult{BackendStatus::UNSUPPORTED, ss.str()};
    }

    if (d.renderStateSlot >= frame.renderStates.size()) {
      return FrameExecutionResult{BackendStatus::BACKEND_ERROR, "Invalid renderStateSlot in draw packet"};
    }

    const auto & rs = frame.renderStates[d.renderStateSlot];
    if (rs.lightingSlot < frame.lightingStates.size()) {
      const auto & ls = frame.lightingStates[rs.lightingSlot];
      if (ls.lights.size() > 1) {
        std::ostringstream ss;
        ss << "UNSUPPORTED: Multiple lights (" << ls.lights.size() << ") present. Wave 1 profile supports at most 1 directional light.";
        return FrameExecutionResult{BackendStatus::UNSUPPORTED, ss.str()};
      }
      for (size_t l = 0; l < ls.lights.size(); ++l) {
        if (ls.lights[l].type != LightType::DIRECTIONAL) {
          std::ostringstream ss;
          ss << "UNSUPPORTED: Light " << l << " is not directional. Wave 1 profile supports only directional lights.";
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

  BackendStatus res = this->backend->submit(frame, *this);
  if (res != BackendStatus::SUCCESS) {
    std::string lastErr = this->backend ? this->backend->getLastError() : std::string();
    this->lastError = lastErr;
    if (res == BackendStatus::DEVICE_LOST) {
      if (lastErr.empty()) lastErr = "WebGPU device lost during frame submission";
      this->lastError = lastErr;
      this->generation++;
      this->backend.reset();
      this->status = SoWgpuRenderTarget::TARGET_LOST;
      return FrameExecutionResult(BackendStatus::DEVICE_LOST, lastErr);
    } else if (res == BackendStatus::NOT_READY) {
      this->status = SoWgpuRenderTarget::TARGET_NOT_READY;
      return FrameExecutionResult(BackendStatus::NOT_READY, lastErr);
    } else if (res == BackendStatus::SURFACE_LOST) {
      this->status = SoWgpuRenderTarget::TARGET_SURFACE_LOST;
      return FrameExecutionResult(BackendStatus::SURFACE_LOST, lastErr);
    } else if (res == BackendStatus::OUT_OF_MEMORY) {
      this->status = SoWgpuRenderTarget::TARGET_ERROR;
      return FrameExecutionResult(BackendStatus::OUT_OF_MEMORY, lastErr);
    } else if (res == BackendStatus::UNSUPPORTED) {
      this->status = SoWgpuRenderTarget::TARGET_ERROR;
      return FrameExecutionResult(BackendStatus::UNSUPPORTED, lastErr);
    } else {
      this->status = SoWgpuRenderTarget::TARGET_ERROR;
      return FrameExecutionResult(BackendStatus::BACKEND_ERROR, lastErr);
    }
  }

  this->status = SoWgpuRenderTarget::TARGET_READY;
  this->lastError.clear();
  return FrameExecutionResult(BackendStatus::SUCCESS, "");
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

SbBool
SoWgpuRenderTarget::resize(const SbVec2i32 & newSize)
{
  return this->pimpl->resize(newSize) ? TRUE : FALSE;
}

void
SoWgpuRenderTarget::readbackRGBA(std::vector<uint8_t> & outRgba) const
{
  this->pimpl->readbackRGBA(outRgba);
}

uint32_t
SoWgpuRenderTarget::getGeneration(void) const
{
  return this->pimpl->generation;
}

SbBool
SoWgpuRenderTarget::isWindowTarget(void) const
{
  return this->pimpl->kind == SoWgpuRenderTargetP::KIND_WINDOW ? TRUE : FALSE;
}

const SoWgpuNativeSurfaceDescriptor &
SoWgpuRenderTarget::getNativeSurfaceDescriptor(void) const
{
  return this->pimpl->nativeDesc;
}
