#include "rendering/wgpu/SoWgpuCpuReferenceBackend.h"
#include <atomic>
#include "rendering/wgpu/SoWgpuRenderTargetP.h"
#include <Inventor/SbMatrix.h>
#include <Inventor/SbVec2f.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbVec4f.h>

#include <vector>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <new>

namespace {

struct ShadedVertex {
  SbVec4f clipPos;
  SbVec3f viewPos;
  SbVec3f viewNormal;
  uint32_t materialSlot;
};

inline float edgeFunction(const SbVec2f & a, const SbVec2f & b, const SbVec2f & c) {
  return (c[0] - a[0]) * (b[1] - a[1]) - (c[1] - a[1]) * (b[0] - a[0]);
}

inline ShadedVertex interpolateVertex(const ShadedVertex & a, const ShadedVertex & b, float t) {
  ShadedVertex out;
  out.clipPos = a.clipPos + (b.clipPos - a.clipPos) * t;
  out.viewPos = a.viewPos + (b.viewPos - a.viewPos) * t;
  out.viewNormal = a.viewNormal + (b.viewNormal - a.viewNormal) * t;
  out.viewNormal.normalize();
  out.materialSlot = (t < 0.5f ? a.materialSlot : b.materialSlot);
  return out;
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
        if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
      } else {
        if (w0 > 0.0f || w1 > 0.0f || w2 > 0.0f) continue;
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

      // Perspective-correct barycentric interpolation
      float pNormW = l0 * invW[0] + l1 * invW[1] + l2 * invW[2];
      float oneOverNormW = (pNormW > 1e-9f ? 1.0f / pNormW : 1.0f);

      float b0 = (l0 * invW[0]) * oneOverNormW;
      float b1 = (l1 * invW[1]) * oneOverNormW;
      float b2 = (l2 * invW[2]) * oneOverNormW;

      SbVec3f n = sv0.viewNormal * b0 + sv1.viewNormal * b1 + sv2.viewNormal * b2;
      n.normalize();

      SbVec3f vPos = sv0.viewPos * b0 + sv1.viewPos * b1 + sv2.viewPos * b2;
      SbVec3f viewDir = -vPos;
      viewDir.normalize();

      // B03: Material properties interpolated per-vertex
      const auto & m0 = (sv0.materialSlot < frame.materials.size() ? frame.materials[sv0.materialSlot] : MaterialSnapshot{});
      const auto & m1 = (sv1.materialSlot < frame.materials.size() ? frame.materials[sv1.materialSlot] : MaterialSnapshot{});
      const auto & m2 = (sv2.materialSlot < frame.materials.size() ? frame.materials[sv2.materialSlot] : MaterialSnapshot{});

      SbVec3f amb(
        m0.ambient[0] * b0 + m1.ambient[0] * b1 + m2.ambient[0] * b2,
        m0.ambient[1] * b0 + m1.ambient[1] * b1 + m2.ambient[1] * b2,
        m0.ambient[2] * b0 + m1.ambient[2] * b1 + m2.ambient[2] * b2
      );

      SbVec3f diff(
        m0.diffuse[0] * b0 + m1.diffuse[0] * b1 + m2.diffuse[0] * b2,
        m0.diffuse[1] * b0 + m1.diffuse[1] * b1 + m2.diffuse[1] * b2,
        m0.diffuse[2] * b0 + m1.diffuse[2] * b1 + m2.diffuse[2] * b2
      );

      SbVec3f spec(
        m0.specular[0] * b0 + m1.specular[0] * b1 + m2.specular[0] * b2,
        m0.specular[1] * b0 + m1.specular[1] * b1 + m2.specular[1] * b2,
        m0.specular[2] * b0 + m1.specular[2] * b1 + m2.specular[2] * b2
      );

      float shininess = m0.shininess * b0 + m1.shininess * b1 + m2.shininess * b2;

      float diffFactor = std::max(0.0f, n.dot(lightDirView));
      SbVec3f h = lightDirView + viewDir;
      h.normalize();
      float specFactor = (diffFactor > 0.0f ? std::pow(std::max(0.0f, n.dot(h)), std::max(1.0f, shininess * 128.0f)) : 0.0f);

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

SoWgpuCpuReferenceBackend::SoWgpuCpuReferenceBackend()
  : status(BackendStatus::SUCCESS)
{
}

SoWgpuCpuReferenceBackend::~SoWgpuCpuReferenceBackend()
{
}

BackendStatus
SoWgpuCpuReferenceBackend::getStatus() const
{
  return this->status;
}

BackendStatus
SoWgpuCpuReferenceBackend::prepare(SoWgpuRenderTargetP & target)
{
  if (target.size[0] <= 0 || target.size[1] <= 0) {
    this->status = BackendStatus::NOT_READY;
    this->lastError = "Target has invalid dimensions";
    return BackendStatus::NOT_READY;
  }
  this->status = BackendStatus::SUCCESS;
  this->lastError.clear();
  return BackendStatus::SUCCESS;
}

SubmitResult
SoWgpuCpuReferenceBackend::submit(const FramePlan & frame, SoWgpuRenderTargetP & target)
{
  try {
    int width = target.size[0];
    int height = target.size[1];

    if (width <= 0 || height <= 0) {
      this->status = BackendStatus::NOT_READY;
      this->lastError = "Target size is zero or negative";
      return SubmitResult(BackendStatus::NOT_READY, this->lastError);
    }

    target.clear(frame.clearColor[0], frame.clearColor[1], frame.clearColor[2], frame.clearColor[3], 1.0f);

    for (size_t dIdx = 0; dIdx < frame.draws.size(); ++dIdx) {
      const auto & draw = frame.draws[dIdx];
// Process supported topologies
      if (draw.renderStateSlot >= frame.renderStates.size()) {
        continue;
      }
      const auto & rs = frame.renderStates[draw.renderStateSlot];

      SbMatrix modelView = rs.model * rs.view;
      SbMatrix normalMatrix = modelView.inverse().transpose();

      // Clip space conversion from Coin [-1, 1] to WebGPU [0, 1] depth
      SbMatrix C(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        0.0f, 0.0f, 0.5f, 1.0f
      );
      SbMatrix projWgpu = rs.projectionCoin * C;
      SbMatrix mvpWgpu = modelView * projWgpu;

      // B04: Directional light direction in view space (already in view space in FramePlan)
      SbVec3f lightDirView(0.0f, 0.0f, 1.0f);
      float lightIntensity = 1.0f;
      SbColor lightCol(1.0f, 1.0f, 1.0f);
      if (rs.lightingSlot < frame.lightingStates.size() && !frame.lightingStates[rs.lightingSlot].lights.empty()) {
        const auto & l = frame.lightingStates[rs.lightingSlot].lights[0];
        lightDirView.setValue(-l.direction[0], -l.direction[1], -l.direction[2]);
        lightDirView.normalize();
        lightIntensity = l.intensity;
        lightCol.setValue(l.color[0], l.color[1], l.color[2]);
      }

      // Safe bounds validation against 32-bit overflow (B05)
      const size_t totalIndices = frame.indices.size();
      if (draw.geometry.firstIndex > totalIndices || draw.geometry.indexCount > (totalIndices - draw.geometry.firstIndex)) {
        continue;
      }

      if (draw.topology == PrimitiveTopology::TRIANGLE_LIST) {
        uint32_t endIdx = draw.geometry.firstIndex + draw.geometry.indexCount;
        for (uint32_t idx = draw.geometry.firstIndex; idx + 2 < endIdx; idx += 3) {
        uint32_t i0 = frame.indices[idx];
        uint32_t i1 = frame.indices[idx + 1];
        uint32_t i2 = frame.indices[idx + 2];

        if (i0 >= frame.vertices.size() || i1 >= frame.vertices.size() || i2 >= frame.vertices.size()) {
          continue;
        }

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

        // B09: Robust frustum clipping against eye near and near depth plane
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
                            target.depthBuffer, target.colorBuffer);
        }
      }
      } else if (draw.topology == PrimitiveTopology::LINE_LIST) {
        uint32_t endIdx = draw.geometry.firstIndex + draw.geometry.indexCount;
        for (uint32_t idx = draw.geometry.firstIndex; idx + 1 < endIdx; idx += 2) {
          uint32_t i0 = frame.indices[idx];
          uint32_t i1 = frame.indices[idx + 1];
          if (i0 >= frame.vertices.size() || i1 >= frame.vertices.size()) continue;

          const VertexSnapshot & v0 = frame.vertices[i0];
          const VertexSnapshot & v1 = frame.vertices[i1];

          SbVec4f clip0, clip1;
          mvpWgpu.multVecMatrix(SbVec4f(v0.position[0], v0.position[1], v0.position[2], 1.0f), clip0);
          mvpWgpu.multVecMatrix(SbVec4f(v1.position[0], v1.position[1], v1.position[2], 1.0f), clip1);

          if (clip0[3] < 1e-5f || clip1[3] < 1e-5f) continue;

          float x0 = (clip0[0] / clip0[3] + 1.0f) * 0.5f * width;
          float y0 = (1.0f - clip0[1] / clip0[3]) * 0.5f * height;
          float z0 = clip0[2] / clip0[3];

          float x1 = (clip1[0] / clip1[3] + 1.0f) * 0.5f * width;
          float y1 = (1.0f - clip1[1] / clip1[3]) * 0.5f * height;
          float z1 = clip1[2] / clip1[3];

          const auto & m0 = frame.materials[v0.materialSlot < frame.materials.size() ? v0.materialSlot : 0];
          const auto & m1 = frame.materials[v1.materialSlot < frame.materials.size() ? v1.materialSlot : 0];

          float dx = x1 - x0;
          float dy = y1 - y0;
          float dist = std::max(std::abs(dx), std::abs(dy));
          int steps = std::max(1, static_cast<int>(std::ceil(dist)));

          for (int s = 0; s <= steps; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(steps);
            int px = static_cast<int>(std::round(x0 + t * dx));
            int py = static_cast<int>(std::round(y0 + t * dy));
            float z = z0 + t * (z1 - z0);

            if (px >= 0 && px < width && py >= 0 && py < height && z >= 0.0f && z <= 1.0f) {
              size_t pIdx = py * width + px;
              if (z <= target.depthBuffer[pIdx]) {
                target.depthBuffer[pIdx] = z;
                float r = (1.0f - t) * (m0.diffuse[0] + m0.ambient[0]) + t * (m1.diffuse[0] + m1.ambient[0]);
                float g = (1.0f - t) * (m0.diffuse[1] + m0.ambient[1]) + t * (m1.diffuse[1] + m1.ambient[1]);
                float b = (1.0f - t) * (m0.diffuse[2] + m0.ambient[2]) + t * (m1.diffuse[2] + m1.ambient[2]);
                size_t cIdx = pIdx * 4;
                if (cIdx + 3 < target.colorBuffer.size()) {
                  target.colorBuffer[cIdx + 0] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, r * 255.0f)));
                  target.colorBuffer[cIdx + 1] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, g * 255.0f)));
                  target.colorBuffer[cIdx + 2] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, b * 255.0f)));
                  target.colorBuffer[cIdx + 3] = 255;
                }
              }
            }
          }
        }
      } else if (draw.topology == PrimitiveTopology::POINT_LIST) {
        uint32_t endIdx = draw.geometry.firstIndex + draw.geometry.indexCount;
        for (uint32_t idx = draw.geometry.firstIndex; idx < endIdx; ++idx) {
          uint32_t i0 = frame.indices[idx];
          if (i0 >= frame.vertices.size()) continue;

          const VertexSnapshot & v0 = frame.vertices[i0];
          SbVec4f clip0;
          mvpWgpu.multVecMatrix(SbVec4f(v0.position[0], v0.position[1], v0.position[2], 1.0f), clip0);
          if (clip0[3] < 1e-5f) continue;

          int px = static_cast<int>(std::round((clip0[0] / clip0[3] + 1.0f) * 0.5f * width));
          int py = static_cast<int>(std::round((1.0f - clip0[1] / clip0[3]) * 0.5f * height));
          float z = clip0[2] / clip0[3];

          if (px >= 0 && px < width && py >= 0 && py < height && z >= 0.0f && z <= 1.0f) {
            size_t pIdx = py * width + px;
            if (z <= target.depthBuffer[pIdx]) {
              target.depthBuffer[pIdx] = z;
              const auto & m0 = frame.materials[v0.materialSlot < frame.materials.size() ? v0.materialSlot : 0];
              float r = m0.diffuse[0] + m0.ambient[0];
              float g = m0.diffuse[1] + m0.ambient[1];
              float b = m0.diffuse[2] + m0.ambient[2];
              size_t cIdx = pIdx * 4;
              if (cIdx + 3 < target.colorBuffer.size()) {
                target.colorBuffer[cIdx + 0] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, r * 255.0f)));
                target.colorBuffer[cIdx + 1] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, g * 255.0f)));
                target.colorBuffer[cIdx + 2] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, b * 255.0f)));
                target.colorBuffer[cIdx + 3] = 255;
              }
            }
          }
        }
      }
    }

    target.status = SoWgpuRenderTarget::TARGET_READY;
    this->status = BackendStatus::SUCCESS;
    this->lastError.clear();
    static std::atomic<uint64_t> globalCpuSerial(1);
  return SubmitResult(BackendStatus::SUCCESS, "", globalCpuSerial.fetch_add(1));
  } catch (const std::bad_alloc &) {
    this->status = BackendStatus::OUT_OF_MEMORY;
    this->lastError = "Out of memory during software rasterization";
    target.status = SoWgpuRenderTarget::TARGET_ERROR;
    return SubmitResult(BackendStatus::OUT_OF_MEMORY, this->lastError);
  }
}
