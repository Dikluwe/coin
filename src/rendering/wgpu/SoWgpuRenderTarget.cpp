#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuRenderTargetP.h"
#if defined(HAVE_WGPU_RUST_BRIDGE)
#include "rendering/wgpu/SoWgpuRustBackend.h"
#include "rendering/wgpu/coin_wgpu_ffi.h"
#elif defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE)
#include "rendering/wgpu/SoWgpuNativeBackend.h"
#endif

#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/tools/SbPimplPtr.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <vector>

static_assert(sizeof(SoWgpuNativeSurfaceDescriptor) >= 32, "SoWgpuNativeSurfaceDescriptor size check");

SoWgpuRenderTargetP::SoWgpuRenderTargetP(const SbVec2i32 & sz)
  : kind(KIND_OFFSCREEN),
    status(SoWgpuRenderTarget::TARGET_READY),
    size(sz),
    generation(0),
    surfaceId(0),
    suspended(false),
    needsReconfigure(false)
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
#endif

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
  this->suspended = false;
  this->needsReconfigure = false;
  this->status = SoWgpuRenderTarget::TARGET_READY;
  return true;
}

bool
SoWgpuRenderTargetP::resize(const SbVec2i32 & newSize)
{
  if (this->kind == KIND_WINDOW) {
    if (newSize[0] < 0 || newSize[1] < 0) {
      this->lastError = "Resize dimensions must not be negative";
      this->status = SoWgpuRenderTarget::TARGET_ERROR;
      return false;
    }
    if (newSize[0] == 0 || newSize[1] == 0) {
      this->size = newSize;
      this->suspended = true;
      this->needsReconfigure = true;
      this->status = SoWgpuRenderTarget::TARGET_NOT_READY;
      return true;
    }
    if (newSize == this->size && !this->needsReconfigure && !this->suspended) {
      return true;
    }
    this->size = newSize;
    this->suspended = false;
    this->needsReconfigure = true;
    this->status = SoWgpuRenderTarget::TARGET_READY;
    return true;
  }

  // Offscreen target
  if (newSize[0] < 0 || newSize[1] < 0) {
    this->size = SbVec2i32(0, 0);
    this->colorBuffer.clear();
    this->depthBuffer.clear();
    this->status = SoWgpuRenderTarget::TARGET_ERROR;
    return false;
  }
  if (newSize[0] == 0 || newSize[1] == 0) {
    this->size = newSize;
    this->colorBuffer.clear();
    this->depthBuffer.clear();
    this->status = SoWgpuRenderTarget::TARGET_NOT_READY;
    return true;
  }
  uint64_t w = static_cast<uint64_t>(newSize[0]);
  uint64_t h = static_cast<uint64_t>(newSize[1]);
  const uint64_t MAX_DIM = 16384;
  if (w > MAX_DIM || h > MAX_DIM || (w * h > (std::numeric_limits<size_t>::max() / 4))) {
    this->size = SbVec2i32(0, 0);
    this->colorBuffer.clear();
    this->depthBuffer.clear();
    this->status = SoWgpuRenderTarget::TARGET_ERROR;
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
    return false;
  }
  this->size = newSize;
  this->status = SoWgpuRenderTarget::TARGET_READY;
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

    // Check all materials referenced by vertices in this draw
    const auto & geom = d.geometry;
    for (uint32_t idx = geom.firstIndex; idx < geom.firstIndex + geom.indexCount; ++idx) {
      if (idx < frame.indices.size()) {
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
          if (mSlot != rs.materialSlot) {
            return FrameExecutionResult{BackendStatus::UNSUPPORTED,
              "UNSUPPORTED: Per-vertex material slots differing from draw render state material are unsupported in Wave 1A profile."};
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

#if !defined(HAVE_WGPU_RUST_BRIDGE) && !defined(HAVE_WGPU_DAWN) && !defined(HAVE_WGPU_NATIVE)
namespace {

struct ShadedVertex {
  SbVec4f clipPos;
  SbVec3f viewPos;
  SbVec3f viewNormal;
  uint32_t materialSlot;
};

static ShadedVertex interpolateVertex(const ShadedVertex & a, const ShadedVertex & b, float t)
{
  ShadedVertex out;
  out.clipPos = a.clipPos + (b.clipPos - a.clipPos) * t;
  out.viewPos = a.viewPos + (b.viewPos - a.viewPos) * t;
  out.viewNormal = a.viewNormal + (b.viewNormal - a.viewNormal) * t;
  out.viewNormal.normalize();
  out.materialSlot = (t < 0.5f ? a.materialSlot : b.materialSlot);
  return out;
}

inline float edgeFunction(const SbVec2f & a, const SbVec2f & b, const SbVec2f & c) {
  return (c[0] - a[0]) * (b[1] - a[1]) - (c[1] - a[1]) * (b[0] - a[0]);
}

static void rasterizeTriangle(const ShadedVertex & sv0, const ShadedVertex & sv1, const ShadedVertex & sv2,
                              int width, int height,
                              const SbVec3f & lightDirView, float lightIntensity, const SbColor & lightCol,
                              const FramePlan & frame,
                              CullMode cullMode, FrontFace frontFace,
                              std::vector<float> & depthBuffer, std::vector<uint8_t> & colorBuffer)
{
  SbVec2f scrPos[3];
  float invW[3];
  float ndcZ[3];
  const ShadedVertex * sv[3] = { &sv0, &sv1, &sv2 };

  for (int k = 0; k < 3; ++k) {
    float w = (sv[k]->clipPos[3] > 1e-6f ? sv[k]->clipPos[3] : 1e-6f);
    invW[k] = 1.0f / w;
    float nx = sv[k]->clipPos[0] * invW[k];
    float ny = sv[k]->clipPos[1] * invW[k];
    ndcZ[k] = sv[k]->clipPos[2] * invW[k];
    scrPos[k].setValue((nx + 1.0f) * 0.5f * static_cast<float>(width),
                       (1.0f - (ny + 1.0f) * 0.5f) * static_cast<float>(height));
  }

  float area = edgeFunction(scrPos[0], scrPos[1], scrPos[2]);
  float orientArea = (frontFace == FrontFace::CW ? -area : area);
  if (cullMode == CullMode::BACK && orientArea <= 0.0f) return;
  if (cullMode == CullMode::FRONT && orientArea >= 0.0f) return;
  if (std::abs(area) < 1e-5f) return; // Degenerate

  int minX = std::max(0, static_cast<int>(std::floor(std::min({scrPos[0][0], scrPos[1][0], scrPos[2][0]}))));
  int maxX = std::min(width - 1, static_cast<int>(std::ceil(std::max({scrPos[0][0], scrPos[1][0], scrPos[2][0]}))));
  int minY = std::max(0, static_cast<int>(std::floor(std::min({scrPos[0][1], scrPos[1][1], scrPos[2][1]}))));
  int maxY = std::min(height - 1, static_cast<int>(std::ceil(std::max({scrPos[0][1], scrPos[1][1], scrPos[2][1]}))));

  float invArea = 1.0f / area;

  for (int py = minY; py <= maxY; ++py) {
    for (int px = minX; px <= maxX; ++px) {
      SbVec2f p(static_cast<float>(px) + 0.5f, static_cast<float>(py) + 0.5f);
      float w0 = edgeFunction(scrPos[1], scrPos[2], p);
      float w1 = edgeFunction(scrPos[2], scrPos[0], p);
      float w2 = edgeFunction(scrPos[0], scrPos[1], p);

      if (area > 0) {
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
      } else {
        if (w0 > 0 || w1 > 0 || w2 > 0) continue;
      }

      float l0 = w0 * invArea;
      float l1 = w1 * invArea;
      float l2 = w2 * invArea;

      float zVal = l0 * ndcZ[0] + l1 * ndcZ[1] + l2 * ndcZ[2];
      if (zVal < 0.0f || zVal > 1.0f) continue;

      size_t pIdx = static_cast<size_t>(py * width + px);
      if (pIdx >= depthBuffer.size()) continue;

      // Depth test LessEqual
      if (zVal > depthBuffer[pIdx]) continue;
      depthBuffer[pIdx] = zVal;

      // Perspective-correct normal
      float pNormW = l0 * invW[0] + l1 * invW[1] + l2 * invW[2];
      float oneOverNormW = (pNormW > 1e-9f ? 1.0f / pNormW : 1.0f);

      SbVec3f n = (sv0.viewNormal * (l0 * invW[0]) +
                   sv1.viewNormal * (l1 * invW[1]) +
                   sv2.viewNormal * (l2 * invW[2])) * oneOverNormW;
      n.normalize();

      uint32_t mSlot = sv0.materialSlot;
      const auto & mat = (mSlot < frame.materials.size() ? frame.materials[mSlot] : MaterialSnapshot{});

      SbVec3f amb(mat.ambient[0], mat.ambient[1], mat.ambient[2]);
      SbVec3f diff(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]);
      SbVec3f spec(mat.specular[0], mat.specular[1], mat.specular[2]);

      float diffFactor = std::max(0.0f, n.dot(lightDirView));

      SbVec3f vPos = (sv0.viewPos * (l0 * invW[0]) +
                      sv1.viewPos * (l1 * invW[1]) +
                      sv2.viewPos * (l2 * invW[2])) * oneOverNormW;
      SbVec3f viewDir = -vPos;
      viewDir.normalize();

      SbVec3f h = lightDirView + viewDir;
      h.normalize();
      float specFactor = (diffFactor > 0.0f ? std::pow(std::max(0.0f, n.dot(h)), std::max(1.0f, mat.shininess * 128.0f)) : 0.0f);

      float finalR = amb[0] + diff[0] * lightCol[0] * diffFactor * lightIntensity + spec[0] * specFactor * lightIntensity;
      float finalG = amb[1] + diff[1] * lightCol[1] * diffFactor * lightIntensity + spec[1] * specFactor * lightIntensity;
      float finalB = amb[2] + diff[2] * lightCol[2] * diffFactor * lightIntensity + spec[2] * specFactor * lightIntensity;

      size_t cIdx = pIdx * 4;
      if (cIdx + 3 < colorBuffer.size()) {
        colorBuffer[cIdx + 0] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, finalR * 255.0f)));
        colorBuffer[cIdx + 1] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, finalG * 255.0f)));
        colorBuffer[cIdx + 2] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, finalB * 255.0f)));
        colorBuffer[cIdx + 3] = 255;
      }
    }
  }
}

} // namespace
#endif

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

#if defined(HAVE_WGPU_RUST_BRIDGE)
  if (this->status == SoWgpuRenderTarget::TARGET_LOST ||
      this->status == SoWgpuRenderTarget::TARGET_SURFACE_LOST ||
      this->status == SoWgpuRenderTarget::TARGET_NOT_READY ||
      !this->backend) {
    if (!this->backend) {
      this->backend = std::unique_ptr<SoWgpuBackend>(new SoWgpuRustBackend());
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

#elif defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE)
  if (this->status == SoWgpuRenderTarget::TARGET_LOST ||
      this->status == SoWgpuRenderTarget::TARGET_SURFACE_LOST ||
      this->status == SoWgpuRenderTarget::TARGET_NOT_READY ||
      !this->backend) {
    if (!this->backend) {
      this->backend = std::unique_ptr<SoWgpuBackend>(new SoWgpuNativeBackend());
    }
    BackendStatus prep = this->backend->prepare(*this);
    if (prep != BackendStatus::SUCCESS) {
      std::string lastErr = this->backend->getLastError();
      this->status = SoWgpuRenderTarget::TARGET_NOT_READY;
      this->backend.reset();
      return FrameExecutionResult(prep, lastErr);
    }
    this->status = SoWgpuRenderTarget::TARGET_READY;
  }

  BackendStatus res = this->backend->submit(frame, *this);
  if (res != BackendStatus::SUCCESS) {
    if (res == BackendStatus::DEVICE_LOST) {
      this->generation++;
      this->backend.reset();
      this->status = SoWgpuRenderTarget::TARGET_ERROR;
      return FrameExecutionResult(BackendStatus::DEVICE_LOST, "Device lost during frame submission");
    } else if (res == BackendStatus::OUT_OF_MEMORY) {
      this->status = SoWgpuRenderTarget::TARGET_ERROR;
      return FrameExecutionResult(BackendStatus::OUT_OF_MEMORY, this->backend->getLastError());
    } else if (res == BackendStatus::UNSUPPORTED) {
      return FrameExecutionResult(BackendStatus::UNSUPPORTED, this->backend->getLastError());
    } else {
      this->status = SoWgpuRenderTarget::TARGET_ERROR;
      return FrameExecutionResult(BackendStatus::BACKEND_ERROR, this->backend->getLastError());
    }
  }

  this->status = SoWgpuRenderTarget::TARGET_READY;
  return FrameExecutionResult(BackendStatus::SUCCESS, "");

#else
  // Reference CPU software rasterizer (RECORDING mode)
  if (this->kind == KIND_WINDOW) {
    this->status = SoWgpuRenderTarget::TARGET_ERROR;
    this->lastError = "Native window surface targets are not supported by the RECORDING backend; RUST_BRIDGE backend is required.";
    return FrameExecutionResult(BackendStatus::UNSUPPORTED, this->lastError);
  }

  this->clear(frame.clearColor[0], frame.clearColor[1], frame.clearColor[2], frame.clearColor[3], 1.0f);

  int width = this->size[0];
  int height = this->size[1];

  for (size_t dIdx = 0; dIdx < frame.draws.size(); ++dIdx) {
    const auto & draw = frame.draws[dIdx];
    const auto & rs = frame.renderStates[draw.renderStateSlot];
    const auto & ls = (rs.lightingSlot < frame.lightingStates.size() ? frame.lightingStates[rs.lightingSlot] : LightingSnapshot{});

    // Compute transformations
    SbMatrix modelView = rs.model * rs.view;
    SbMatrix normalMatrix = modelView.inverse().transpose();

    // Clip space conversion from Coin [-1, 1] to WebGPU [0, 1]
    SbMatrix C(
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.5f, 0.0f,
      0.0f, 0.0f, 0.5f, 1.0f
    );
    SbMatrix projWgpu = rs.projectionCoin * C;
    SbMatrix mvpWgpu = modelView * projWgpu;

    // Directional light direction in view space
    SbVec3f lightDirView(0.0f, 0.0f, 1.0f);
    float lightIntensity = 1.0f;
    SbColor lightCol(1.0f, 1.0f, 1.0f);
    bool hasLight = (!ls.lights.empty());
    if (hasLight) {
      const auto & l = ls.lights[0];
      // Light incidence vector points FROM surface TO light source (-direction)
      lightDirView.setValue(-l.direction[0], -l.direction[1], -l.direction[2]);
      lightDirView.normalize();
      lightIntensity = l.intensity;
      lightCol.setValue(l.color[0], l.color[1], l.color[2]);
    }

    // Shading per-triangle
    for (uint32_t idx = draw.geometry.firstIndex; idx + 2 < draw.geometry.firstIndex + draw.geometry.indexCount; idx += 3) {
      uint32_t i0 = frame.indices[idx];
      uint32_t i1 = frame.indices[idx + 1];
      uint32_t i2 = frame.indices[idx + 2];

      const VertexSnapshot & v0 = frame.vertices[i0];
      const VertexSnapshot & v1 = frame.vertices[i1];
      const VertexSnapshot & v2 = frame.vertices[i2];

      ShadedVertex sv[3];
      const VertexSnapshot * rawV[3] = { &v0, &v1, &v2 };

      for (int k = 0; k < 3; ++k) {
        SbVec4f objPos(rawV[k]->position[0], rawV[k]->position[1], rawV[k]->position[2], 1.0f);
        mvpWgpu.multVecMatrix(objPos, sv[k].clipPos);

        SbVec3f objP3(rawV[k]->position[0], rawV[k]->position[1], rawV[k]->position[2]);
        modelView.multVecMatrix(objP3, sv[k].viewPos);

        SbVec3f objN3(rawV[k]->normal[0], rawV[k]->normal[1], rawV[k]->normal[2]);
        normalMatrix.multDirMatrix(objN3, sv[k].viewNormal);
        sv[k].viewNormal.normalize();

        sv[k].materialSlot = rawV[k]->materialSlot;
      }

      // Frustum clipping
      const float EYE_NEAR = 1e-5f;
      auto isInsideEye = [EYE_NEAR](const ShadedVertex & v) {
        return v.clipPos[3] >= EYE_NEAR;
      };

      std::vector<ShadedVertex> inPoly = { sv[0], sv[1], sv[2] };
      std::vector<ShadedVertex> eyeClipped;

      for (size_t k = 0; k < inPoly.size(); ++k) {
        const auto & cur = inPoly[k];
        const auto & next = inPoly[(k + 1) % inPoly.size()];
        bool curIn = isInsideEye(cur);
        bool nextIn = isInsideEye(next);

        if (curIn && nextIn) {
          eyeClipped.push_back(next);
        } else if (curIn && !nextIn) {
          float denom = next.clipPos[3] - cur.clipPos[3];
          float t = (std::abs(denom) > 1e-7f ? (EYE_NEAR - cur.clipPos[3]) / denom : 0.0f);
          eyeClipped.push_back(interpolateVertex(cur, next, t));
        } else if (!curIn && nextIn) {
          float denom = next.clipPos[3] - cur.clipPos[3];
          float t = (std::abs(denom) > 1e-7f ? (EYE_NEAR - cur.clipPos[3]) / denom : 0.0f);
          eyeClipped.push_back(interpolateVertex(cur, next, t));
          eyeClipped.push_back(next);
        }
      }

      if (eyeClipped.size() < 3) continue;

      auto isInsideNear = [](const ShadedVertex & v) {
        return v.clipPos[2] >= 0.0f;
      };

      std::vector<ShadedVertex> outPoly;
      for (size_t k = 0; k < eyeClipped.size(); ++k) {
        const auto & cur = eyeClipped[k];
        const auto & next = eyeClipped[(k + 1) % eyeClipped.size()];
        bool curIn = isInsideNear(cur);
        bool nextIn = isInsideNear(next);

        if (curIn && nextIn) {
          outPoly.push_back(next);
        } else if (curIn && !nextIn) {
          float denom = next.clipPos[2] - cur.clipPos[2];
          float t = (std::abs(denom) > 1e-7f ? (0.0f - cur.clipPos[2]) / denom : 0.0f);
          outPoly.push_back(interpolateVertex(cur, next, t));
        } else if (!curIn && nextIn) {
          float denom = next.clipPos[2] - cur.clipPos[2];
          float t = (std::abs(denom) > 1e-7f ? (0.0f - cur.clipPos[2]) / denom : 0.0f);
          outPoly.push_back(interpolateVertex(cur, next, t));
          outPoly.push_back(next);
        }
      }

      if (outPoly.size() < 3) continue;

      for (size_t tIdx = 1; tIdx + 1 < outPoly.size(); ++tIdx) {
        rasterizeTriangle(outPoly[0], outPoly[tIdx], outPoly[tIdx + 1],
                          width, height,
                          lightDirView, lightIntensity, lightCol,
                          frame,
                          rs.cullMode, rs.frontFace,
                          this->depthBuffer, this->colorBuffer);
      }
    }
  }
  this->status = SoWgpuRenderTarget::TARGET_READY;
  this->lastError.clear();
  return FrameExecutionResult(BackendStatus::SUCCESS, "");
#endif
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
SoWgpuRenderTarget::resize(const SbVec2i32 & size)
{
  return this->pimpl->resize(size) ? TRUE : FALSE;
}
