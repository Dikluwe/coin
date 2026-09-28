#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderClipCore.h"
#include "rendering/coinrender/CoinRenderStateCore.h"

#include <cmath>
#include <cstring>

namespace {
template <typename T>
bool
samePlainSnapshots(const std::vector<T> & a, const std::vector<T> & b)
{
  // Padding may cause a conservative miss, but never makes distinct captured
  // bytes compare equal.
  return a.size() == b.size() &&
    (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}

bool
sameCameras(const std::vector<CoinRenderCameraSnapshot> & a,
            const std::vector<CoinRenderCameraSnapshot> & b)
{
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const CoinRenderCameraSnapshot & x = a[i];
    const CoinRenderCameraSnapshot & y = b[i];
    if (std::memcmp(x.viewMatrix.getValue(), y.viewMatrix.getValue(), sizeof(float) * 16) != 0 ||
        std::memcmp(x.projectionMatrixCoin.getValue(), y.projectionMatrixCoin.getValue(), sizeof(float) * 16) != 0 ||
        x.isPerspective != y.isPerspective || x.nearDistance != y.nearDistance ||
        x.farDistance != y.farDistance || x.focalDistance != y.focalDistance ||
        x.aspectRatio != y.aspectRatio) return false;
  }
  return true;
}
}

bool
CoinRenderFramePlan::hasSamePayload(const CoinRenderFramePlan & other) const
{
  for (int i = 0; i < 4; ++i) {
    if (this->clearColor[i] != other.clearColor[i]) return false;
  }
  if (!samePlainSnapshots(this->vertices, other.vertices) ||
      this->indices != other.indices ||
      !samePlainSnapshots(this->materials, other.materials) ||
      !sameCameras(this->cameras, other.cameras) ||
      !samePlainSnapshots(this->viewports, other.viewports) ||
      this->renderStates.size() != other.renderStates.size() ||
      !samePlainSnapshots(this->samplers, other.samplers) ||
      !samePlainSnapshots(this->draws, other.draws) ||
      this->lightingStates.size() != other.lightingStates.size() ||
      this->textures.size() != other.textures.size()) return false;
  for (size_t i = 0; i < this->renderStates.size(); ++i) {
    const auto & a = this->renderStates[i];
    const auto & b = other.renderStates[i];
    if (!coin_render_same_state_except_camera(a, b) ||
        a.view != b.view || a.projectionCoin != b.projectionCoin) return false;
  }
  for (size_t i = 0; i < this->lightingStates.size(); ++i) {
    const CoinRenderLightingSnapshot & x = this->lightingStates[i];
    const CoinRenderLightingSnapshot & y = other.lightingStates[i];
    if (x.ambientIntensity != y.ambientIntensity ||
        std::memcmp(x.ambientColor, y.ambientColor, sizeof(x.ambientColor)) != 0 ||
        !samePlainSnapshots(x.lights, y.lights)) return false;
  }
  for (size_t i = 0; i < this->textures.size(); ++i) {
    const CoinRenderTextureImageSnapshot & x = this->textures[i];
    const CoinRenderTextureImageSnapshot & y = other.textures[i];
    if (x.width != y.width || x.height != y.height ||
        x.components != y.components || x.contentDigest != y.contentDigest ||
        x.gpuToken != y.gpuToken || x.gpuOpaque != y.gpuOpaque ||
        x.pixelsRgba != y.pixelsRgba) return false;
  }
  return true;
}

bool
CoinRenderFramePlan::isValid(std::string * outDiagnostic) const
{
  auto isFiniteF = [](float v) { return std::isfinite(v); };

  auto isMatrixFinite = [&](const SbMatrix & m) {
    const float (*mat)[4] = m.getValue();
    for (int r = 0; r < 4; ++r) {
      for (int c = 0; c < 4; ++c) {
        if (!isFiniteF(mat[r][c])) return false;
      }
    }
    return true;
  };

  for (int i = 0; i < 4; ++i) {
    if (!isFiniteF(this->clearColor[i])) {
      if (outDiagnostic) *outDiagnostic = "Invalid clearColor (NaN or inf)";
      return false;
    }
  }

  for (size_t i = 0; i < this->materials.size(); ++i) {
    const CoinRenderMaterialSnapshot & m = this->materials[i];
    if (!isFiniteF(m.transparency) || m.transparency < 0.0f || m.transparency > 1.0f ||
        !isFiniteF(m.diffuse[3]) || m.diffuse[3] < 0.0f || m.diffuse[3] > 1.0f) {
      if (outDiagnostic) *outDiagnostic = "Material has invalid transparency or diffuse alpha";
      return false;
    }
    if (std::abs(m.diffuse[3] + m.transparency - 1.0f) > 1.0e-5f) {
      if (outDiagnostic) *outDiagnostic = "Material alpha and transparency are inconsistent";
      return false;
    }
  }

  const size_t numVertices = this->vertices.size();
  for (size_t i = 0; i < numVertices; ++i) {
    const CoinRenderVertexSnapshot & v = this->vertices[i];
    for (int k = 0; k < 3; ++k) {
      if (!isFiniteF(v.position[k]) || !isFiniteF(v.normal[k])) {
        if (outDiagnostic) *outDiagnostic = "Vertex contains non-finite position or normal";
        return false;
      }
    }
    for (int k = 0; k < 2; ++k) {
      if (!isFiniteF(v.texcoord[k])) {
        if (outDiagnostic) *outDiagnostic = "Vertex contains non-finite texcoord";
        return false;
      }
    }
    if (!isFiniteF(v.screenSpaceW) || v.screenSpaceW <= 0.0f ||
        !isFiniteF(v.fogEyeDepth)) {
      if (outDiagnostic) *outDiagnostic = "Invalid expanded primitive attributes";
      return false;
    }
    for (size_t u = 0; u < COIN_RENDER_MAX_TEXTURE_UNITS - 1; ++u)
      for (int c = 0; c < 2; ++c)
        if (!isFiniteF(v.extraTexcoords[u][c])) {
          if (outDiagnostic) *outDiagnostic = "Non-finite multitexture coordinate";
          return false;
        }
    if (this->materials.empty() || v.materialSlot >= this->materials.size()) {
      if (outDiagnostic) *outDiagnostic = "Vertex references out-of-range material slot";
      return false;
    }
  }

  const size_t numIndices = this->indices.size();
  for (size_t i = 0; i < numIndices; ++i) {
    if (this->indices[i] >= numVertices) {
      if (outDiagnostic) *outDiagnostic = "Index out of range of vertex buffer";
      return false;
    }
  }

  for (size_t i = 0; i < this->cameras.size(); ++i) {
    const CoinRenderCameraSnapshot & c = this->cameras[i];
    if (!isMatrixFinite(c.viewMatrix) || !isMatrixFinite(c.projectionMatrixCoin)) {
      if (outDiagnostic) *outDiagnostic = "Camera matrix contains non-finite values";
      return false;
    }
    if (!isFiniteF(c.nearDistance) || !isFiniteF(c.farDistance) || c.farDistance <= c.nearDistance) {
      if (outDiagnostic) *outDiagnostic = "Camera clip planes are invalid or inverted";
      return false;
    }
  }

  for (size_t i = 0; i < this->textures.size(); ++i) {
    const CoinRenderTextureImageSnapshot & tex = this->textures[i];
    if (tex.width == 0 || tex.height == 0) {
      if (outDiagnostic) *outDiagnostic = "Texture contains zero width or height";
      return false;
    }
    if (tex.width > 8192 || tex.height > 8192) {
      if (outDiagnostic) *outDiagnostic = "Texture dimensions exceed 8192 limit";
      return false;
    }
    const uint64_t expectedBytes = static_cast<uint64_t>(tex.width) *
      static_cast<uint64_t>(tex.height) * 4ULL;
    if (tex.gpuToken == 0 && tex.pixelsRgba.size() != expectedBytes) {
      if (outDiagnostic) *outDiagnostic = "Texture pixel buffer size mismatch";
      return false;
    }
    if (tex.gpuToken != 0 && !tex.pixelsRgba.empty()) {
      if (outDiagnostic) *outDiagnostic = "GPU texture must not carry CPU pixels";
      return false;
    }
  }

  for (size_t i = 0; i < this->lightingStates.size(); ++i) {
    const CoinRenderLightingSnapshot & ls = this->lightingStates[i];
    if (ls.lights.size() > COIN_RENDER_MAX_LIGHTS) {
      if (outDiagnostic) *outDiagnostic = "More than eight active lights";
      return false;
    }
    if (!isFiniteF(ls.ambientIntensity) || ls.ambientIntensity < 0.0f) {
      if (outDiagnostic) *outDiagnostic = "Invalid ambient intensity";
      return false;
    }
    for (int c = 0; c < 3; ++c) {
      if (!isFiniteF(ls.ambientColor[c])) {
        if (outDiagnostic) *outDiagnostic = "Invalid ambient color";
        return false;
      }
    }
    for (size_t j = 0; j < ls.lights.size(); ++j) {
      const CoinRenderLightSourceSnapshot & light = ls.lights[j];
      if (light.type != CoinRenderLightType::DIRECTIONAL &&
          light.type != CoinRenderLightType::POINT && light.type != CoinRenderLightType::SPOT) {
        if (outDiagnostic) *outDiagnostic = "Unsupported light type";
        return false;
      }
      if (!isFiniteF(light.intensity) || light.intensity < 0.0f ||
          !isFiniteF(light.cutOffAngle) || !isFiniteF(light.dropOffRate)) {
        if (outDiagnostic) *outDiagnostic = "Invalid light intensity or cone";
        return false;
      }
      bool anyAttenuation = false;
      for (int c = 0; c < 3; ++c) {
        if (!isFiniteF(light.color[c]) || !isFiniteF(light.direction[c]) ||
            !isFiniteF(light.position[c]) || !isFiniteF(light.attenuation[c]) ||
            light.attenuation[c] < 0.0f) {
          if (outDiagnostic) *outDiagnostic = "Invalid light vector, color or attenuation";
          return false;
        }
        anyAttenuation = anyAttenuation || light.attenuation[c] > 0.0f;
      }
      if (light.type != CoinRenderLightType::DIRECTIONAL && !anyAttenuation) {
        if (outDiagnostic) *outDiagnostic = "Degenerate positional light attenuation";
        return false;
      }
      if (light.type != CoinRenderLightType::POINT) {
        const float dirLengthSq = light.direction[0] * light.direction[0] +
          light.direction[1] * light.direction[1] +
          light.direction[2] * light.direction[2];
        if (dirLengthSq <= 1.0e-12f) {
          if (outDiagnostic) *outDiagnostic = "Zero light direction";
          return false;
        }
      }
      if (light.type == CoinRenderLightType::SPOT &&
          (light.cutOffAngle < 0.0f || light.cutOffAngle > 1.570796327f ||
           light.dropOffRate < 0.0f || light.dropOffRate > 1.0f)) {
        if (outDiagnostic) *outDiagnostic = "Spot cone outside supported range";
        return false;
      }
    }
  }

  for (size_t i = 0; i < this->renderStates.size(); ++i) {
    const CoinRenderRenderStateSnapshot & state = this->renderStates[i];
    float clipEquations[COIN_RENDER_MAX_CLIP_PLANES][4] = {};
    std::string clipDiagnostic;
    if (!coin_render_clip_equations(state, clipEquations, clipDiagnostic)) {
      if (outDiagnostic) *outDiagnostic = clipDiagnostic;
      return false;
    }
    if (state.viewportSlot >= this->viewports.size()) {
      if (outDiagnostic) *outDiagnostic = "RenderState references out-of-bounds viewport slot";
      return false;
    }
    if (state.depthFunction != CoinRenderDepthFunction::NEVER &&
        state.depthFunction != CoinRenderDepthFunction::ALWAYS &&
        state.depthFunction != CoinRenderDepthFunction::LESS &&
        state.depthFunction != CoinRenderDepthFunction::LEQUAL &&
        state.depthFunction != CoinRenderDepthFunction::EQUAL &&
        state.depthFunction != CoinRenderDepthFunction::GEQUAL &&
        state.depthFunction != CoinRenderDepthFunction::GREATER &&
        state.depthFunction != CoinRenderDepthFunction::NOTEQUAL) {
      if (outDiagnostic) *outDiagnostic = "Unsupported depth comparison function";
      return false;
    }
    if (!isFiniteF(state.polygonOffsetFactor) || !isFiniteF(state.polygonOffsetUnits) ||
        !isFiniteF(state.polygonOffsetSlopeBias) ||
        (state.polygonOffsetStyles & ~7u) != 0 ||
        (state.polygonOffsetPrimitiveStyle != 1 &&
         state.polygonOffsetPrimitiveStyle != 2 && state.polygonOffsetPrimitiveStyle != 4)) {
      if (outDiagnostic) *outDiagnostic = "Invalid polygon offset";
      return false;
    }
    if (!isFiniteF(state.depthRange[0]) || !isFiniteF(state.depthRange[1]) ||
        state.depthRange[0] < 0.0f || state.depthRange[1] > 1.0f ||
        state.depthRange[0] > state.depthRange[1]) {
      if (outDiagnostic) *outDiagnostic = "Invalid depth range";
      return false;
    }
    const CoinRenderViewportSnapshot & viewport = this->viewports[state.viewportSlot];
    if (viewport.width <= 0 || viewport.height <= 0) {
      if (outDiagnostic) *outDiagnostic = "Viewport has invalid origin or extent";
      return false;
    }
    if (!isFiniteF(state.lineWidth) || state.lineWidth <= 0.0f ||
        !isFiniteF(state.pointSize) || state.pointSize <= 0.0f ||
        state.linePatternScaleFactor < 1) {
      if (outDiagnostic) *outDiagnostic = "Invalid line or point style";
      return false;
    }
    if (state.fogMode != CoinRenderFogMode::NONE && state.fogMode != CoinRenderFogMode::HAZE &&
        state.fogMode != CoinRenderFogMode::FOG && state.fogMode != CoinRenderFogMode::SMOKE) {
      if (outDiagnostic) *outDiagnostic = "Unsupported fog mode";
      return false;
    }
    if (!isFiniteF(state.fogStart) || !isFiniteF(state.fogEnd) ||
        (state.fogMode != CoinRenderFogMode::NONE && state.fogEnd <= 0.0f) ||
        (state.fogMode == CoinRenderFogMode::HAZE && state.fogEnd <= state.fogStart)) {
      if (outDiagnostic) *outDiagnostic = "Invalid fog range";
      return false;
    }
    for (int c = 0; c < 3; ++c) {
      if (!isFiniteF(state.fogColor[c])) {
        if (outDiagnostic) *outDiagnostic = "Invalid fog color";
        return false;
      }
    }
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      const CoinRenderTextureUnitSnapshot tex = coin_render_texture_unit(state, unit);
      if (!tex.enabled) continue;
      if (tex.model != CoinRenderTextureModel::MODULATE &&
          tex.model != CoinRenderTextureModel::REPLACE &&
          tex.model != CoinRenderTextureModel::DECAL &&
          tex.model != CoinRenderTextureModel::BLEND) {
        if (outDiagnostic) *outDiagnostic = "RenderState contains unsupported texture model";
        return false;
      }
      for (int c = 0; c < 4; ++c) {
        if (!isFiniteF(tex.blendColor[c])) {
          if (outDiagnostic) *outDiagnostic = "RenderState contains invalid texture blend color";
          return false;
        }
      }

      if (!isMatrixFinite(tex.matrix)) {
        if (outDiagnostic) *outDiagnostic = "RenderState contains non-finite texture matrix";
        return false;
      }
      if (tex.imageSlot >= this->textures.size()) {
        if (outDiagnostic) *outDiagnostic = "RenderState references out-of-bounds texture image slot";
        return false;
      }
      if (tex.samplerSlot >= this->samplers.size()) {
        if (outDiagnostic) *outDiagnostic = "RenderState references out-of-bounds sampler slot";
        return false;
      }
    }
  }

  for (size_t i = 0; i < this->draws.size(); ++i) {
    const CoinRenderDrawPacket & draw = this->draws[i];
    if (draw.hasSortingCenter &&
        (!isFiniteF(draw.sortingCenterWorld[0]) || !isFiniteF(draw.sortingCenterWorld[1]) || !isFiniteF(draw.sortingCenterWorld[2]))) {
      if (outDiagnostic) *outDiagnostic = "Non-finite object sorting center";
      return false;
    }
    if (draw.clearDepthBefore && draw.renderLayer == 0) {
      if (outDiagnostic) *outDiagnostic = "Base layer cannot clear depth before a draw";
      return false;
    }
    if (draw.renderStateSlot >= this->renderStates.size()) {
      if (outDiagnostic) *outDiagnostic = "Draw references invalid renderStateSlot";
      return false;
    }
    const CoinRenderGeometryRange & geometry = draw.geometry;
    if (geometry.firstVertex > numVertices ||
        geometry.vertexCount > (numVertices - geometry.firstVertex)) {
      if (outDiagnostic) *outDiagnostic = "Draw vertex range out of bounds";
      return false;
    }
    if (geometry.firstIndex > numIndices ||
        geometry.indexCount > (numIndices - geometry.firstIndex)) {
      if (outDiagnostic) *outDiagnostic = "Draw index range out of bounds";
      return false;
    }
    if (draw.topology == CoinRenderPrimitiveTopology::TRIANGLE_LIST &&
        (geometry.indexCount % 3 != 0)) {
      if (outDiagnostic) *outDiagnostic = "Triangle list index count is not multiple of 3";
      return false;
    }
    if (draw.topology == CoinRenderPrimitiveTopology::LINE_LIST &&
        (geometry.indexCount % 2 != 0)) {
      if (outDiagnostic) *outDiagnostic = "Line list index count is not multiple of 2";
      return false;
    }

    const CoinRenderRenderStateSnapshot & state = this->renderStates[draw.renderStateSlot];
    if (!isMatrixFinite(state.model) || !isMatrixFinite(state.view) ||
        !isMatrixFinite(state.projectionCoin)) {
      if (outDiagnostic) *outDiagnostic = "RenderState matrix contains non-finite values";
      return false;
    }
    if (this->materials.empty() || state.materialSlot >= this->materials.size()) {
      if (outDiagnostic) *outDiagnostic = "RenderState materialSlot out of range";
      return false;
    }
    if (this->lightingStates.empty() || state.lightingSlot >= this->lightingStates.size()) {
      if (outDiagnostic) *outDiagnostic = "RenderState lightingSlot out of range";
      return false;
    }
    if (this->cameras.empty() || state.cameraSlot >= this->cameras.size()) {
      if (outDiagnostic) *outDiagnostic = "RenderState cameraSlot out of range";
      return false;
    }
    if (this->viewports.empty() || state.viewportSlot >= this->viewports.size()) {
      if (outDiagnostic) *outDiagnostic = "RenderState viewportSlot out of range";
      return false;
    }
  }

  return true;
}
