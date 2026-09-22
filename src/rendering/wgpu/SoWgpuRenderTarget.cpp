#include "rendering/wgpu/SoWgpuRenderTargetP.h"
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/tools/SbPimplPtr.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <vector>

SoWgpuRenderTargetP::SoWgpuRenderTargetP(const SbVec2i32 & sz)
  : status(SoWgpuRenderTarget::TARGET_READY),
    size(sz)
{
  this->resize(sz);
}

SoWgpuRenderTargetP::~SoWgpuRenderTargetP()
{
}

bool
SoWgpuRenderTargetP::resize(const SbVec2i32 & newSize)
{
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

bool
SoWgpuRenderTargetP::validateProfile(const FramePlan & frame, std::string & outDiagnostic)
{
  for (size_t i = 0; i < frame.draws.size(); ++i) {
    const auto & d = frame.draws[i];
    if (d.topology != PrimitiveTopology::TRIANGLE_LIST) {
      std::ostringstream ss;
      ss << "UNSUPPORTED: Draw " << i << " topology is not TRIANGLE_LIST. Wave 1 profile requires TRIANGLE_LIST.";
      outDiagnostic = ss.str();
      return false;
    }

    if (d.renderStateSlot >= frame.renderStates.size()) {
      outDiagnostic = "Invalid renderStateSlot in draw packet";
      return false;
    }

    const auto & rs = frame.renderStates[d.renderStateSlot];
    if (rs.lightingSlot < frame.lightingStates.size()) {
      const auto & ls = frame.lightingStates[rs.lightingSlot];
      for (size_t l = 0; l < ls.lights.size(); ++l) {
        if (ls.lights[l].type != LightType::DIRECTIONAL) {
          std::ostringstream ss;
          ss << "UNSUPPORTED: Light " << l << " is not directional. Wave 1 profile supports only directional lights.";
          outDiagnostic = ss.str();
          return false;
        }
      }
    }

    if (rs.materialSlot < frame.materials.size()) {
      const auto & mat = frame.materials[rs.materialSlot];
      if (mat.transparency > 0.001f) {
        std::ostringstream ss;
        ss << "UNSUPPORTED: Draw " << i << " has transparency=" << mat.transparency << ". Wave 1 profile supports only opaque objects.";
        outDiagnostic = ss.str();
        return false;
      }
    }
  }
  return true;
}

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
  if (std::abs(area) < 1e-5f) return; // Degenerate

  int minX = std::max(0, static_cast<int>(std::floor(std::min({scrPos[0][0], scrPos[1][0], scrPos[2][0]}))));
  int maxX = std::min(width - 1, static_cast<int>(std::ceil(std::max({scrPos[0][0], scrPos[1][0], scrPos[2][0]}))));
  int minY = std::max(0, static_cast<int>(std::floor(std::min({scrPos[0][1], scrPos[1][1], scrPos[2][1]}))));
  int maxY = std::min(height - 1, static_cast<int>(std::ceil(std::max({scrPos[0][1], scrPos[1][1], scrPos[2][1]}))));

  const auto & mat0 = (sv[0]->materialSlot < frame.materials.size() ? frame.materials[sv[0]->materialSlot] : MaterialSnapshot{});
  const auto & mat1 = (sv[1]->materialSlot < frame.materials.size() ? frame.materials[sv[1]->materialSlot] : MaterialSnapshot{});
  const auto & mat2 = (sv[2]->materialSlot < frame.materials.size() ? frame.materials[sv[2]->materialSlot] : MaterialSnapshot{});
  bool sameMat = (sv[0]->materialSlot == sv[1]->materialSlot && sv[1]->materialSlot == sv[2]->materialSlot);

  for (int py = minY; py <= maxY; ++py) {
    for (int px = minX; px <= maxX; ++px) {
      SbVec2f p(static_cast<float>(px) + 0.5f, static_cast<float>(py) + 0.5f);
      float w0 = edgeFunction(scrPos[1], scrPos[2], p);
      float w1 = edgeFunction(scrPos[2], scrPos[0], p);
      float w2 = edgeFunction(scrPos[0], scrPos[1], p);

      bool inside = (area > 0) ? (w0 >= 0 && w1 >= 0 && w2 >= 0) : (w0 <= 0 && w1 <= 0 && w2 <= 0);
      if (!inside) continue;

      float b0 = w0 / area;
      float b1 = w1 / area;
      float b2 = w2 / area;

      float z = b0 * ndcZ[0] + b1 * ndcZ[1] + b2 * ndcZ[2];
      if (z < 0.0f || z > 1.0f) continue;

      size_t pIdx = static_cast<size_t>(py) * static_cast<size_t>(width) + static_cast<size_t>(px);
      if (pIdx >= depthBuffer.size() || z >= depthBuffer[pIdx]) continue;

      depthBuffer[pIdx] = z;

      float interpInvW = b0 * invW[0] + b1 * invW[1] + b2 * invW[2];
      if (std::abs(interpInvW) < 1e-7f) continue;
      float invInterp = 1.0f / interpInvW;

      SbVec3f interpNormal = (sv[0]->viewNormal * (b0 * invW[0]) +
                              sv[1]->viewNormal * (b1 * invW[1]) +
                              sv[2]->viewNormal * (b2 * invW[2])) * invInterp;
      interpNormal.normalize();

      SbVec3f interpViewPos = (sv[0]->viewPos * (b0 * invW[0]) +
                               sv[1]->viewPos * (b1 * invW[1]) +
                               sv[2]->viewPos * (b2 * invW[2])) * invInterp;

      float diff[3], amb[3], spec[3], shin;
      if (sameMat) {
        diff[0] = mat0.diffuse[0]; diff[1] = mat0.diffuse[1]; diff[2] = mat0.diffuse[2];
        amb[0] = mat0.ambient[0]; amb[1] = mat0.ambient[1]; amb[2] = mat0.ambient[2];
        spec[0] = mat0.specular[0]; spec[1] = mat0.specular[1]; spec[2] = mat0.specular[2];
        shin = mat0.shininess;
      } else {
        float w0w = b0 * invW[0] * invInterp;
        float w1w = b1 * invW[1] * invInterp;
        float w2w = b2 * invW[2] * invInterp;
        diff[0] = mat0.diffuse[0] * w0w + mat1.diffuse[0] * w1w + mat2.diffuse[0] * w2w;
        diff[1] = mat0.diffuse[1] * w0w + mat1.diffuse[1] * w1w + mat2.diffuse[1] * w2w;
        diff[2] = mat0.diffuse[2] * w0w + mat1.diffuse[2] * w1w + mat2.diffuse[2] * w2w;
        amb[0] = mat0.ambient[0] * w0w + mat1.ambient[0] * w0w + mat2.ambient[0] * w2w;
        amb[1] = mat0.ambient[1] * w0w + mat1.ambient[1] * w0w + mat2.ambient[1] * w2w;
        amb[2] = mat0.ambient[2] * w0w + mat1.ambient[2] * w0w + mat2.ambient[2] * w2w;
        spec[0] = mat0.specular[0] * w0w + mat1.specular[0] * w0w + mat2.specular[0] * w2w;
        spec[1] = mat0.specular[1] * w0w + mat1.specular[1] * w0w + mat2.specular[1] * w2w;
        spec[2] = mat0.specular[2] * w0w + mat1.specular[2] * w0w + mat2.specular[2] * w2w;
        shin = mat0.shininess * w0w + mat1.shininess * w1w + mat2.shininess * w2w;
      }

      SbVec3f N = interpNormal;
      SbVec3f L = -lightDirView;
      L.normalize();
      SbVec3f V = -interpViewPos;
      V.normalize();
      SbVec3f H = L + V;
      H.normalize();

      float diffFactor = std::max(N.dot(L), 0.0f);
      float specFactor = (diffFactor > 0.0f) ? std::pow(std::max(N.dot(H), 0.0f), std::max(shin * 128.0f, 1.0f)) : 0.0f;

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

bool
SoWgpuRenderTargetP::executeFrame(const FramePlan & frame, std::string & outError)
{
  if (!this->validateProfile(frame, outError)) {
    return false;
  }

  if (this->size[0] <= 0 || this->size[1] <= 0) {
    outError = "Invalid target size (must be > 0)";
    return false;
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

    // Directional light direction in view space (already transformed to view space by builder)
    SbVec3f lightDirView(0.0f, 0.0f, 1.0f);
    float lightIntensity = 1.0f;
    SbColor lightCol(1.0f, 1.0f, 1.0f);
    bool hasLight = (!ls.lights.empty());
    if (hasLight) {
      const auto & l = ls.lights[0];
      lightDirView.setValue(l.direction[0], l.direction[1], l.direction[2]);
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

      // Near plane clipping in clip space: W_clip >= NEAR_CLIP
      const float NEAR_CLIP = 0.0001f;
      auto isInside = [NEAR_CLIP](const ShadedVertex & v) {
        return v.clipPos[3] >= NEAR_CLIP;
      };

      std::vector<ShadedVertex> inPoly = { sv[0], sv[1], sv[2] };
      std::vector<ShadedVertex> outPoly;

      for (size_t k = 0; k < inPoly.size(); ++k) {
        const auto & cur = inPoly[k];
        const auto & next = inPoly[(k + 1) % inPoly.size()];
        bool curIn = isInside(cur);
        bool nextIn = isInside(next);

        if (curIn && nextIn) {
          outPoly.push_back(next);
        } else if (curIn && !nextIn) {
          float denom = next.clipPos[3] - cur.clipPos[3];
          float t = (std::abs(denom) > 1e-7f ? (NEAR_CLIP - cur.clipPos[3]) / denom : 0.0f);
          outPoly.push_back(interpolateVertex(cur, next, t));
        } else if (!curIn && nextIn) {
          float denom = next.clipPos[3] - cur.clipPos[3];
          float t = (std::abs(denom) > 1e-7f ? (NEAR_CLIP - cur.clipPos[3]) / denom : 0.0f);
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
                          this->depthBuffer, this->colorBuffer);
      }
    }
  }

  return true;
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

SoWgpuRenderTarget::Status
SoWgpuRenderTarget::getStatus(void) const
{
  return this->pimpl->status;
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
