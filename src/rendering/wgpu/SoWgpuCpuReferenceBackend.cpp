#include "rendering/wgpu/SoWgpuCpuReferenceBackend.h"
#include <atomic>
#include "rendering/wgpu/SoWgpuComposition.h"
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
  SbVec4f litColor;
  SbVec2f texCoord;
  SbVec2f extraTexcoords[COIN_WGPU_MAX_TEXTURE_UNITS - 1];
};

inline float edgeFunction(const SbVec2f & a, const SbVec2f & b, const SbVec2f & c) {
  return (c[0] - a[0]) * (b[1] - a[1]) - (c[1] - a[1]) * (b[0] - a[0]);
}

inline ShadedVertex interpolateVertex(const ShadedVertex & a, const ShadedVertex & b, float t) {
  ShadedVertex out;
  out.clipPos = a.clipPos + (b.clipPos - a.clipPos) * t;
  out.viewPos = a.viewPos + (b.viewPos - a.viewPos) * t;
  out.litColor = a.litColor + (b.litColor - a.litColor) * t;
  out.texCoord = a.texCoord + (b.texCoord - a.texCoord) * t;
  for (size_t u = 0; u < COIN_WGPU_MAX_TEXTURE_UNITS - 1; ++u)
    out.extraTexcoords[u] = a.extraTexcoords[u] + (b.extraTexcoords[u] - a.extraTexcoords[u]) * t;
  return out;
}

inline SbVec4f sampleTexture(const TextureImageSnapshot & tex, const SamplerSnapshot & samp, float u, float v) {
  if (tex.width == 0 || tex.height == 0 || tex.pixelsRgba.empty()) {
    return SbVec4f(1.0f, 1.0f, 1.0f, 1.0f);
  }

  // Wrap U
  float uCoord = u;
  if (samp.wrapS == TextureWrap::REPEAT) {
    uCoord = uCoord - std::floor(uCoord);
  } else {
    uCoord = std::max(0.0f, std::min(1.0f, uCoord));
  }

  // Wrap V
  float vCoord = v;
  if (samp.wrapT == TextureWrap::REPEAT) {
    vCoord = vCoord - std::floor(vCoord);
  } else {
    vCoord = std::max(0.0f, std::min(1.0f, vCoord));
  }

  float fx = uCoord * static_cast<float>(tex.width) - 0.5f;
  float fy = vCoord * static_cast<float>(tex.height) - 0.5f;
  int x0 = static_cast<int>(std::floor(fx));
  int y0 = static_cast<int>(std::floor(fy));
  int x1 = x0 + 1;
  int y1 = y0 + 1;
  float wx = fx - std::floor(fx);
  float wy = fy - std::floor(fy);

  auto fetchPixel = [&](int x, int y) -> SbVec4f {
    if (samp.wrapS == TextureWrap::REPEAT) {
      x = ((x % static_cast<int>(tex.width)) + tex.width) % tex.width;
    } else {
      x = std::max(0, std::min(static_cast<int>(tex.width) - 1, x));
    }
    if (samp.wrapT == TextureWrap::REPEAT) {
      y = ((y % static_cast<int>(tex.height)) + tex.height) % tex.height;
    } else {
      y = std::max(0, std::min(static_cast<int>(tex.height) - 1, y));
    }
    size_t idx = (static_cast<size_t>(y) * tex.width + static_cast<size_t>(x)) * 4;
    return SbVec4f(
      static_cast<float>(tex.pixelsRgba[idx + 0]) / 255.0f,
      static_cast<float>(tex.pixelsRgba[idx + 1]) / 255.0f,
      static_cast<float>(tex.pixelsRgba[idx + 2]) / 255.0f,
      static_cast<float>(tex.pixelsRgba[idx + 3]) / 255.0f
    );
  };

  if (samp.filter == TextureFilter::NEAREST) {
    int nx = static_cast<int>(std::floor(uCoord * static_cast<float>(tex.width)));
    int ny = static_cast<int>(std::floor(vCoord * static_cast<float>(tex.height)));
    return fetchPixel(nx, ny);
  }

  SbVec4f p00 = fetchPixel(x0, y0);
  SbVec4f p10 = fetchPixel(x1, y0);
  SbVec4f p01 = fetchPixel(x0, y1);
  SbVec4f p11 = fetchPixel(x1, y1);

  SbVec4f top = p00 * (1.0f - wx) + p10 * wx;
  SbVec4f bot = p01 * (1.0f - wx) + p11 * wx;
  return top * (1.0f - wy) + bot * wy;
}

inline void applyFog(const RenderStateSnapshot & rs, float eyeDepth,
                     float & red, float & green, float & blue) {
  if (rs.fogMode == FogMode::NONE) return;
  const float distance = std::max(0.0f, eyeDepth);
  float factor = 1.0f;
  if (rs.fogMode == FogMode::HAZE) {
    factor = (rs.fogEnd - distance) / (rs.fogEnd - rs.fogStart);
  } else if (rs.fogMode == FogMode::FOG) {
    factor = std::exp(-5.545f * distance / rs.fogEnd);
  } else {
    const float x = 2.35f * distance / rs.fogEnd;
    factor = std::exp(-(x * x));
  }
  factor = std::max(0.0f, std::min(1.0f, factor));
  red = rs.fogColor[0] * (1.0f - factor) + red * factor;
  green = rs.fogColor[1] * (1.0f - factor) + green * factor;
  blue = rs.fogColor[2] * (1.0f - factor) + blue * factor;
}

inline void writePixel(std::vector<uint8_t> & color, size_t offset,
                       float red, float green, float blue, float alpha, bool blend) {
  if (offset + 3 >= color.size()) return;
  auto clamp01 = [](float value) { return std::max(0.0f, std::min(1.0f, value)); };
  red = clamp01(red); green = clamp01(green); blue = clamp01(blue); alpha = clamp01(alpha);
  if (blend) {
    const float inverse = 1.0f - alpha;
    red = red * alpha + (color[offset] / 255.0f) * inverse;
    green = green * alpha + (color[offset + 1] / 255.0f) * inverse;
    blue = blue * alpha + (color[offset + 2] / 255.0f) * inverse;
    alpha += (color[offset + 3] / 255.0f) * inverse;
  }
  color[offset] = static_cast<uint8_t>(clamp01(red) * 255.0f);
  color[offset + 1] = static_cast<uint8_t>(clamp01(green) * 255.0f);
  color[offset + 2] = static_cast<uint8_t>(clamp01(blue) * 255.0f);
  color[offset + 3] = static_cast<uint8_t>(clamp01(alpha) * 255.0f);
}


static SbVec4f shadeVertex(const MaterialSnapshot & material,
                          const SbVec3f & vPos, const SbVec3f & n,
                          const LightingSnapshot & lighting, const RenderStateSnapshot & rs) {
  const SbVec3f viewDir(0.0f, 0.0f, 1.0f);
  const SbVec3f amb(material.ambient[0], material.ambient[1], material.ambient[2]);
  const SbVec3f diff(material.diffuse[0], material.diffuse[1], material.diffuse[2]);
  const SbVec3f spec(material.specular[0], material.specular[1], material.specular[2]);
  const SbVec3f emiss(material.emission[0], material.emission[1], material.emission[2]);
  float finalR = 0.0f;
  float finalG = 0.0f;
  float finalB = 0.0f;

  if (rs.lightModel == LightModel::BASE_COLOR) {
    finalR = diff[0];
    finalG = diff[1];
    finalB = diff[2];
  } else {
    finalR = amb[0] * lighting.ambientColor[0] * lighting.ambientIntensity + emiss[0];
    finalG = amb[1] * lighting.ambientColor[1] * lighting.ambientIntensity + emiss[1];
    finalB = amb[2] * lighting.ambientColor[2] * lighting.ambientIntensity + emiss[2];
    for (size_t lightIndex = 0; lightIndex < lighting.lights.size(); ++lightIndex) {
      const LightSourceSnapshot & light = lighting.lights[lightIndex];
      SbVec3f toLight;
      float attenuation = 1.0f;
      if (light.type == LightType::DIRECTIONAL) {
        toLight.setValue(-light.direction[0], -light.direction[1], -light.direction[2]);
        toLight.normalize();
      } else {
        SbVec3f lightPos(light.position[0], light.position[1], light.position[2]);
        SbVec3f delta = lightPos - vPos;
        const float distance = delta.length();
        if (distance <= 1.0e-6f) continue;
        toLight = delta / distance;
        const float denominator = light.attenuation[2] +
          light.attenuation[1] * distance +
          light.attenuation[0] * distance * distance;
        if (denominator <= 1.0e-6f) continue;
        attenuation = 1.0f / denominator;
        if (light.type == LightType::SPOT) {
          SbVec3f lightDirection(light.direction[0], light.direction[1], light.direction[2]);
          lightDirection.normalize();
          const float coneCos = lightDirection.dot(-toLight);
          if (coneCos < std::cos(light.cutOffAngle)) continue;
          attenuation *= std::pow(std::max(coneCos, 0.0f), light.dropOffRate * 128.0f);
        }
      }
      const float diffuseFactor = std::max(0.0f, n.dot(toLight));
      if (diffuseFactor <= 0.0f) continue;
      SbVec3f halfVector = toLight + viewDir;
      halfVector.normalize();
      const float exponent = material.shininess * 128.0f;
      const float specularFactor = exponent > 0.0f
        ? std::pow(std::max(n.dot(halfVector), 0.0f), exponent) : 1.0f;
      const float strength = light.intensity * attenuation;
      finalR += (diff[0] * diffuseFactor + spec[0] * specularFactor) * light.color[0] * strength;
      finalG += (diff[1] * diffuseFactor + spec[1] * specularFactor) * light.color[1] * strength;
      finalB += (diff[2] * diffuseFactor + spec[2] * specularFactor) * light.color[2] * strength;
    }
  }


  // GL clamps primary colors before interpolation, including clipped vertices.
  return SbVec4f(std::max(0.0f, std::min(1.0f, finalR)),
                 std::max(0.0f, std::min(1.0f, finalG)),
                 std::max(0.0f, std::min(1.0f, finalB)), material.diffuse[3]);
}

static void rasterizeTriangle(const ShadedVertex & sv0, const ShadedVertex & sv1, const ShadedVertex & sv2,
                              int width, int height,
                              const RenderStateSnapshot & rs,
                              const FramePlan & frame,
                              bool blend,
                              std::vector<float> & depthBuffer, std::vector<uint8_t> & colorBuffer)
{
  SbVec2f scrPos[3];
  float invW[3];
  float ndcZ[3];
  const ShadedVertex * sv[3] = { &sv0, &sv1, &sv2 };
  const ViewportSnapshot & viewport = frame.viewports[rs.viewportSlot];
  const int viewportTop = height - viewport.y - viewport.height;


  for (int k = 0; k < 3; ++k) {
    float w = (sv[k]->clipPos[3] > 1e-6f ? sv[k]->clipPos[3] : 1e-6f);
    invW[k] = 1.0f / w;
    float nx = sv[k]->clipPos[0] * invW[k];
    float ny = sv[k]->clipPos[1] * invW[k];
    ndcZ[k] = sv[k]->clipPos[2] * invW[k];
    scrPos[k].setValue(viewport.x + (nx + 1.0f) * 0.5f * viewport.width,
                       viewportTop + (1.0f - (ny + 1.0f) * 0.5f) * viewport.height);
  }

  float area = edgeFunction(scrPos[0], scrPos[1], scrPos[2]);
  float orientArea = (rs.frontFace == FrontFace::CW ? -area : area);
  if (rs.cullMode == CullMode::BACK && orientArea <= 0.0f) return;
  if (rs.cullMode == CullMode::FRONT && orientArea >= 0.0f) return;
  if (std::abs(area) < 1e-5f) return; // Degenerate

  int minX = std::max(viewport.x, static_cast<int>(std::floor(std::min({scrPos[0][0], scrPos[1][0], scrPos[2][0]}))));
  int maxX = std::min(viewport.x + viewport.width - 1, static_cast<int>(std::ceil(std::max({scrPos[0][0], scrPos[1][0], scrPos[2][0]}))));
  int minY = std::max(viewportTop, static_cast<int>(std::floor(std::min({scrPos[0][1], scrPos[1][1], scrPos[2][1]}))));
  int maxY = std::min(viewportTop + viewport.height - 1, static_cast<int>(std::ceil(std::max({scrPos[0][1], scrPos[1][1], scrPos[2][1]}))));

  float invArea = 1.0f / area;
  minX = std::max(0, minX);
  maxX = std::min(width - 1, maxX);
  minY = std::max(0, minY);
  maxY = std::min(height - 1, maxY);

  for (int py = minY; py <= maxY; ++py) {
    for (int px = minX; px <= maxX; ++px) {
      SbVec2f p(static_cast<float>(px) + 0.5f, static_cast<float>(py) + 0.5f);
      float w0 = edgeFunction(scrPos[1], scrPos[2], p);
      float w1 = edgeFunction(scrPos[2], scrPos[0], p);
      float w2 = edgeFunction(scrPos[0], scrPos[1], p);

      // A shared edge belongs to exactly one triangle. Inclusive edges on
      // both triangles double-blend expanded transparent lines and points.
      auto covered = [&](float edge, const SbVec2f & first, const SbVec2f & second) {
        const SbVec2f & a = area < 0 ? first : second;
        const SbVec2f & b = area < 0 ? second : first;
        const float signedEdge = area > 0 ? edge : -edge;
        if (signedEdge != 0.0f) return signedEdge > 0.0f;
        const float dy = b[1] - a[1], dx = b[0] - a[0];
        return dy < 0.0f || (dy == 0.0f && dx > 0.0f);
      };
      if (!covered(w0, scrPos[1], scrPos[2]) ||
          !covered(w1, scrPos[2], scrPos[0]) ||
          !covered(w2, scrPos[0], scrPos[1])) continue;

      float l0 = w0 * invArea;
      float l1 = w1 * invArea;
      float l2 = w2 * invArea;

      float zVal = l0 * ndcZ[0] + l1 * ndcZ[1] + l2 * ndcZ[2];
      if (zVal < 0.0f || zVal > 1.0f) continue;

      size_t pIdx = static_cast<size_t>(py * width + px);
      if (pIdx >= depthBuffer.size()) continue;

      // Depth test LessEqual
      if (zVal > depthBuffer[pIdx]) continue;
      if (!blend) depthBuffer[pIdx] = zVal;

      // Perspective-correct barycentric interpolation
      float pNormW = l0 * invW[0] + l1 * invW[1] + l2 * invW[2];
      float oneOverNormW = (pNormW > 1e-9f ? 1.0f / pNormW : 1.0f);

      float b0 = (l0 * invW[0]) * oneOverNormW;
      float b1 = (l1 * invW[1]) * oneOverNormW;
      float b2 = (l2 * invW[2]) * oneOverNormW;

      const SbVec3f vPos = sv0.viewPos * b0 + sv1.viewPos * b1 + sv2.viewPos * b2;
      const SbVec4f color = sv0.litColor * b0 + sv1.litColor * b1 + sv2.litColor * b2;
      float finalR = color[0], finalG = color[1], finalB = color[2];
      float sourceAlpha = color[3];
      for (size_t unit = 0; unit < COIN_WGPU_MAX_TEXTURE_UNITS; ++unit) {
        const TextureUnitSnapshot layer = coin_wgpu_texture_unit(rs, unit);
        if (!layer.enabled) continue;
        const SbVec2f tc = unit == 0 ? sv0.texCoord * b0 + sv1.texCoord * b1 + sv2.texCoord * b2 :
          sv0.extraTexcoords[unit - 1] * b0 + sv1.extraTexcoords[unit - 1] * b1 + sv2.extraTexcoords[unit - 1] * b2;
        SbVec4f texCol = sampleTexture(frame.textures[layer.imageSlot], frame.samplers[layer.samplerSlot], tc[0], tc[1]);
        if (layer.model == TextureModel::REPLACE) {
          finalR = texCol[0]; finalG = texCol[1]; finalB = texCol[2];
          sourceAlpha = texCol[3];
        } else if (layer.model == TextureModel::DECAL) {
          finalR = finalR * (1.0f - texCol[3]) + texCol[0] * texCol[3];
          finalG = finalG * (1.0f - texCol[3]) + texCol[1] * texCol[3];
          finalB = finalB * (1.0f - texCol[3]) + texCol[2] * texCol[3];
        } else if (layer.model == TextureModel::BLEND) {
          finalR = finalR * (1.0f - texCol[0]) + layer.blendColor[0] * texCol[0];
          finalG = finalG * (1.0f - texCol[1]) + layer.blendColor[1] * texCol[1];
          finalB = finalB * (1.0f - texCol[2]) + layer.blendColor[2] * texCol[2];
          sourceAlpha *= texCol[3];
        } else {
          finalR *= texCol[0]; finalG *= texCol[1]; finalB *= texCol[2];
          sourceAlpha *= texCol[3];
        }
      }

      applyFog(rs, -vPos[2], finalR, finalG, finalB);
      size_t cIdx = pIdx * 4;
      if (cIdx + 3 < colorBuffer.size()) {
        if (blend) {
          if (sourceAlpha <= 0.0f) continue;
          const float invAlpha = 1.0f - sourceAlpha;
          const float dstR = colorBuffer[cIdx + 0] / 255.0f;
          const float dstG = colorBuffer[cIdx + 1] / 255.0f;
          const float dstB = colorBuffer[cIdx + 2] / 255.0f;
          const float dstA = colorBuffer[cIdx + 3] / 255.0f;
          finalR = std::max(0.0f, std::min(1.0f, finalR)) * sourceAlpha + dstR * invAlpha;
          finalG = std::max(0.0f, std::min(1.0f, finalG)) * sourceAlpha + dstG * invAlpha;
          finalB = std::max(0.0f, std::min(1.0f, finalB)) * sourceAlpha + dstB * invAlpha;
          colorBuffer[cIdx + 3] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, (sourceAlpha + dstA * invAlpha) * 255.0f)));
        } else colorBuffer[cIdx + 3] = 255;
        colorBuffer[cIdx + 0] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, finalR * 255.0f)));
        colorBuffer[cIdx + 1] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, finalG * 255.0f)));
        colorBuffer[cIdx + 2] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, finalB * 255.0f)));
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

    std::vector<SoWgpuCompositionItem> order;
    std::string compositionError;
    if (!coin_wgpu_composition_order(frame, order, compositionError)) {
      this->status = BackendStatus::UNSUPPORTED;
      this->lastError = compositionError;
      return SubmitResult(BackendStatus::UNSUPPORTED, compositionError);
    }

    target.clear(frame.clearColor[0], frame.clearColor[1], frame.clearColor[2], frame.clearColor[3], 1.0f);

    uint32_t activeLayer = UINT32_MAX;
    for (size_t dIdx = 0; dIdx < order.size(); ++dIdx) {
      const auto & draw = frame.draws[order[dIdx].drawIndex];
      if (draw.renderLayer != activeLayer) {
        activeLayer = draw.renderLayer;
        const DrawPacket * barrier = nullptr;
        for (const DrawPacket & candidate : frame.draws) {
          if (candidate.renderLayer == activeLayer && candidate.clearDepthBefore) {
            barrier = &candidate;
            break;
          }
        }
        if (barrier) {
          const RenderStateSnapshot & barrierState = frame.renderStates[barrier->renderStateSlot];
          const ViewportSnapshot & clearViewport = frame.viewports[barrierState.viewportSlot];
          const int64_t clearTop = int64_t(height) - clearViewport.y - clearViewport.height;
          const int left = static_cast<int>(std::max<int64_t>(0, std::min<int64_t>(width, clearViewport.x)));
          const int right = static_cast<int>(std::max<int64_t>(0, std::min<int64_t>(width,
            int64_t(clearViewport.x) + clearViewport.width)));
          const int top = static_cast<int>(std::max<int64_t>(0, std::min<int64_t>(height, clearTop)));
          const int bottom = static_cast<int>(std::max<int64_t>(0, std::min<int64_t>(height,
            clearTop + clearViewport.height)));
          for (int y = top; y < bottom; ++y) {
            const size_t begin = static_cast<size_t>(y) * width + left;
            std::fill(target.depthBuffer.begin() + begin,
                      target.depthBuffer.begin() + begin + right - left, 1.0f);
          }
        }
      }
      const bool blend = order[dIdx].blend;
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

      const LightingSnapshot & lighting = frame.lightingStates[rs.lightingSlot];
      const ViewportSnapshot & viewport = frame.viewports[rs.viewportSlot];
      const int viewportTop = height - viewport.y - viewport.height;

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
          sv[k].clipPos *= rawV[k]->screenSpaceW;

          SbVec3f objP3(rawV[k]->position[0], rawV[k]->position[1], rawV[k]->position[2]);
          modelView.multVecMatrix(objP3, sv[k].viewPos);
          if (rawV[k]->fogEyeDepth >= 0) sv[k].viewPos[2] = -rawV[k]->fogEyeDepth;

          SbVec3f objN3(rawV[k]->normal[0], rawV[k]->normal[1], rawV[k]->normal[2]);
          SbVec3f viewNormal;
          normalMatrix.multDirMatrix(objN3, viewNormal);
          viewNormal.normalize();
          const auto & material = rawV[k]->materialSlot < frame.materials.size()
            ? frame.materials[rawV[k]->materialSlot] : MaterialSnapshot{};
          sv[k].litColor = shadeVertex(material, sv[k].viewPos, viewNormal, lighting, rs);
          if (rs.hasTexture) {
            SbVec4f tc4(rawV[k]->texcoord[0], rawV[k]->texcoord[1], 0.0f, 1.0f);
            SbVec4f tcTrans;
            rs.textureMatrix.multVecMatrix(tc4, tcTrans);
            sv[k].texCoord.setValue(tcTrans[0], tcTrans[1]);
          } else {
            sv[k].texCoord.setValue(rawV[k]->texcoord[0], rawV[k]->texcoord[1]);
          }
          for (size_t unit = 1; unit < COIN_WGPU_MAX_TEXTURE_UNITS; ++unit) {
            const auto & layer = rs.extraTextures[unit - 1];
            SbVec4f uv;
            layer.matrix.multVecMatrix(SbVec4f(rawV[k]->extraTexcoords[unit - 1][0],
                                              rawV[k]->extraTexcoords[unit - 1][1], 0, 1), uv);
            sv[k].extraTexcoords[unit - 1].setValue(uv[0], uv[1]);
          }
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
                            rs,
                            frame, blend,
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
          SbVec4f view0, view1;
          modelView.multVecMatrix(SbVec4f(v0.position[0], v0.position[1], v0.position[2], 1.0f), view0);
          modelView.multVecMatrix(SbVec4f(v1.position[0], v1.position[1], v1.position[2], 1.0f), view1);

          if (clip0[3] < 1e-5f || clip1[3] < 1e-5f) continue;

          float x0 = viewport.x + (clip0[0] / clip0[3] + 1.0f) * 0.5f * viewport.width;
          float y0 = viewportTop + (1.0f - clip0[1] / clip0[3]) * 0.5f * viewport.height;
          float z0 = clip0[2] / clip0[3];

          float x1 = viewport.x + (clip1[0] / clip1[3] + 1.0f) * 0.5f * viewport.width;
          float y1 = viewportTop + (1.0f - clip1[1] / clip1[3]) * 0.5f * viewport.height;
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

            if (px >= 0 && px < width && py >= 0 && py < height &&
                px >= viewport.x && px < viewport.x + viewport.width &&
                py >= viewportTop && py < viewportTop + viewport.height && z >= 0.0f && z <= 1.0f) {
              size_t pIdx = py * width + px;
              if (z <= target.depthBuffer[pIdx]) {
                if (!blend) target.depthBuffer[pIdx] = z;
                float r = (rs.lightModel == LightModel::BASE_COLOR)
                  ? ((1.0f - t) * m0.diffuse[0] + t * m1.diffuse[0])
                  : ((1.0f - t) * (m0.diffuse[0] + m0.ambient[0] + m0.emission[0]) + t * (m1.diffuse[0] + m1.ambient[0] + m1.emission[0]));
                float g = (rs.lightModel == LightModel::BASE_COLOR)
                  ? ((1.0f - t) * m0.diffuse[1] + t * m1.diffuse[1])
                  : ((1.0f - t) * (m0.diffuse[1] + m0.ambient[1] + m0.emission[1]) + t * (m1.diffuse[1] + m1.ambient[1] + m1.emission[1]));
                float b = (rs.lightModel == LightModel::BASE_COLOR)
                  ? ((1.0f - t) * m0.diffuse[2] + t * m1.diffuse[2])
                  : ((1.0f - t) * (m0.diffuse[2] + m0.ambient[2] + m0.emission[2]) + t * (m1.diffuse[2] + m1.ambient[2] + m1.emission[2]));
                applyFog(rs, -((1.0f - t) * view0[2] + t * view1[2]), r, g, b);
                const float alpha = (1.0f - t) * m0.diffuse[3] + t * m1.diffuse[3];
                writePixel(target.colorBuffer, pIdx * 4, r, g, b, alpha, blend);
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
          SbVec4f view0;
          modelView.multVecMatrix(SbVec4f(v0.position[0], v0.position[1], v0.position[2], 1.0f), view0);
          if (clip0[3] < 1e-5f) continue;

          int px = static_cast<int>(std::round(viewport.x + (clip0[0] / clip0[3] + 1.0f) * 0.5f * viewport.width));
          int py = static_cast<int>(std::round(viewportTop + (1.0f - clip0[1] / clip0[3]) * 0.5f * viewport.height));
          float z = clip0[2] / clip0[3];

          if (px >= 0 && px < width && py >= 0 && py < height &&
                px >= viewport.x && px < viewport.x + viewport.width &&
              py >= viewportTop && py < viewportTop + viewport.height && z >= 0.0f && z <= 1.0f) {
            size_t pIdx = py * width + px;
            if (z <= target.depthBuffer[pIdx]) {
              if (!blend) target.depthBuffer[pIdx] = z;
              const auto & m0 = frame.materials[v0.materialSlot < frame.materials.size() ? v0.materialSlot : 0];
              float r = (rs.lightModel == LightModel::BASE_COLOR)
                ? m0.diffuse[0]
                : (m0.diffuse[0] + m0.ambient[0] + m0.emission[0]);
              float g = (rs.lightModel == LightModel::BASE_COLOR)
                ? m0.diffuse[1]
                : (m0.diffuse[1] + m0.ambient[1] + m0.emission[1]);
              float b = (rs.lightModel == LightModel::BASE_COLOR)
                ? m0.diffuse[2]
                : (m0.diffuse[2] + m0.ambient[2] + m0.emission[2]);
              applyFog(rs, -view0[2], r, g, b);
              writePixel(target.colorBuffer, pIdx * 4, r, g, b, m0.diffuse[3], blend);
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
