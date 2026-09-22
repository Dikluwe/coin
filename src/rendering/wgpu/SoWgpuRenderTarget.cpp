#include "rendering/wgpu/SoWgpuRenderTargetP.h"
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/tools/SbPimplPtr.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

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
    this->status = SoWgpuRenderTarget::TARGET_ERROR;
    return false;
  }
  this->size = newSize;
  size_t pixelCount = static_cast<size_t>(this->size[0]) * static_cast<size_t>(this->size[1]);
  this->colorBuffer.assign(pixelCount * 4, 0);
  this->depthBuffer.assign(pixelCount, 1.0f);
  this->status = (pixelCount > 0 ? SoWgpuRenderTarget::TARGET_READY : SoWgpuRenderTarget::TARGET_NOT_READY);
  return true;
}

void
SoWgpuRenderTargetP::clear(float r, float g, float b, float a, float depthVal)
{
  uint8_t ur = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, r * 255.0f)));
  uint8_t ug = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, g * 255.0f)));
  uint8_t ub = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, b * 255.0f)));
  uint8_t ua = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, a * 255.0f)));

  size_t pixelCount = static_cast<size_t>(this->size[0]) * static_cast<size_t>(this->size[1]);
  for (size_t i = 0; i < pixelCount; ++i) {
    this->colorBuffer[i * 4 + 0] = ur;
    this->colorBuffer[i * 4 + 1] = ug;
    this->colorBuffer[i * 4 + 2] = ub;
    this->colorBuffer[i * 4 + 3] = ua;
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
      ss << "UNSUPPORTED: Draw " << i << " uses non-triangle topology. Wave 1 profile supports only triangle lists.";
      outDiagnostic = ss.str();
      return false;
    }

    const auto & rs = frame.renderStates[d.renderStateSlot];
    if (rs.lightingSlot < frame.lightingStates.size()) {
      const auto & ls = frame.lightingStates[rs.lightingSlot];
      if (ls.lights.size() > 1) {
        std::ostringstream ss;
        ss << "UNSUPPORTED: Draw " << i << " references " << ls.lights.size() << " lights. Wave 1 profile supports at most 1 directional light.";
        outDiagnostic = ss.str();
        return false;
      }
      if (ls.lights.size() == 1 && ls.lights[0].type != LightType::DIRECTIONAL) {
        std::ostringstream ss;
        ss << "UNSUPPORTED: Draw " << i << " uses non-directional light. Wave 1 profile supports only directional lights.";
        outDiagnostic = ss.str();
        return false;
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

    // Verify uniform material per draw
    if (d.geometry.vertexCount > 0) {
      uint32_t firstMat = frame.vertices[d.geometry.firstVertex].materialSlot;
      for (uint32_t v = d.geometry.firstVertex; v < d.geometry.firstVertex + d.geometry.vertexCount; ++v) {
        if (frame.vertices[v].materialSlot != firstMat) {
          std::ostringstream ss;
          ss << "UNSUPPORTED: Draw " << i << " has per-vertex materials. Wave 1 profile supports only single material per draw.";
          outDiagnostic = ss.str();
          return false;
        }
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
};

inline float edgeFunction(const SbVec2f & a, const SbVec2f & b, const SbVec2f & c) {
  return (c[0] - a[0]) * (b[1] - a[1]) - (c[1] - a[1]) * (b[0] - a[0]);
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
    const auto & mat = (rs.materialSlot < frame.materials.size() ? frame.materials[rs.materialSlot] : MaterialSnapshot{});
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
      lightDirView.setValue(l.direction[0], l.direction[1], l.direction[2]);
      // Direction in view space
      rs.view.multDirMatrix(lightDirView, lightDirView);
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
      }

      // Simple clip test (if all outside, discard)
      if (sv[0].clipPos[3] <= 0.0001f && sv[1].clipPos[3] <= 0.0001f && sv[2].clipPos[3] <= 0.0001f) {
        continue;
      }

      SbVec2f scrPos[3];
      float invW[3];
      float ndcZ[3];
      for (int k = 0; k < 3; ++k) {
        float w = (sv[k].clipPos[3] != 0.0f ? sv[k].clipPos[3] : 1.0f);
        invW[k] = 1.0f / w;
        float nx = sv[k].clipPos[0] * invW[k];
        float ny = sv[k].clipPos[1] * invW[k];
        ndcZ[k] = sv[k].clipPos[2] * invW[k];
        scrPos[k].setValue((nx + 1.0f) * 0.5f * static_cast<float>(width),
                           (1.0f - (ny + 1.0f) * 0.5f) * static_cast<float>(height));
      }

      float area = edgeFunction(scrPos[0], scrPos[1], scrPos[2]);
      if (std::abs(area) < 1e-5f) continue; // Degenerate

      int minX = std::max(0, static_cast<int>(std::floor(std::min({scrPos[0][0], scrPos[1][0], scrPos[2][0]}))));
      int maxX = std::min(width - 1, static_cast<int>(std::ceil(std::max({scrPos[0][0], scrPos[1][0], scrPos[2][0]}))));
      int minY = std::max(0, static_cast<int>(std::floor(std::min({scrPos[0][1], scrPos[1][1], scrPos[2][1]}))));
      int maxY = std::min(height - 1, static_cast<int>(std::ceil(std::max({scrPos[0][1], scrPos[1][1], scrPos[2][1]}))));

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
          if (z < 0.0f || z > 1.0f) continue; // Clip depth outside [0, 1]

          size_t pIdx = static_cast<size_t>(py) * static_cast<size_t>(width) + static_cast<size_t>(px);
          if (z >= this->depthBuffer[pIdx]) continue; // Depth test failed

          this->depthBuffer[pIdx] = z; // Depth write

          // Interpolate view position and normal
          float interpInvW = b0 * invW[0] + b1 * invW[1] + b2 * invW[2];
          SbVec3f interpNormal = (sv[0].viewNormal * (b0 * invW[0]) +
                                  sv[1].viewNormal * (b1 * invW[1]) +
                                  sv[2].viewNormal * (b2 * invW[2])) * (1.0f / interpInvW);
          interpNormal.normalize();

          SbVec3f interpViewPos = (sv[0].viewPos * (b0 * invW[0]) +
                                   sv[1].viewPos * (b1 * invW[1]) +
                                   sv[2].viewPos * (b2 * invW[2])) * (1.0f / interpInvW);

          // Blinn-Phong lighting calculation (matching WGSL shader)
          SbVec3f N = interpNormal;
          SbVec3f L = -lightDirView;
          L.normalize();
          SbVec3f V = -interpViewPos;
          V.normalize();
          SbVec3f H = L + V;
          H.normalize();

          float diffFactor = std::max(N.dot(L), 0.0f);
          float specFactor = (diffFactor > 0.0f) ? std::pow(std::max(N.dot(H), 0.0f), std::max(mat.shininess * 128.0f, 1.0f)) : 0.0f;

          float finalR = mat.ambient[0] + mat.diffuse[0] * lightCol[0] * diffFactor * lightIntensity + mat.specular[0] * specFactor * lightIntensity;
          float finalG = mat.ambient[1] + mat.diffuse[1] * lightCol[1] * diffFactor * lightIntensity + mat.specular[1] * specFactor * lightIntensity;
          float finalB = mat.ambient[2] + mat.diffuse[2] * lightCol[2] * diffFactor * lightIntensity + mat.specular[2] * specFactor * lightIntensity;

          this->colorBuffer[pIdx * 4 + 0] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, finalR * 255.0f)));
          this->colorBuffer[pIdx * 4 + 1] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, finalG * 255.0f)));
          this->colorBuffer[pIdx * 4 + 2] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, finalB * 255.0f)));
          this->colorBuffer[pIdx * 4 + 3] = 255;
        }
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
