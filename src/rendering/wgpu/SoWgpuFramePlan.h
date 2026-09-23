#ifndef COIN_SOWGPUFRAMEPLAN_H
#define COIN_SOWGPUFRAMEPLAN_H

#include <Inventor/SbColor4f.h>
#include <Inventor/SbColor.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbVec2f.h>
#include <Inventor/SbVec2i32.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/misc/SoBase.h>

#include <vector>
#include <string>
#include <cstdint>
#include <cmath>

struct VertexSnapshot {
  float position[3] = {0.0f, 0.0f, 0.0f};
  float normal[3] = {0.0f, 0.0f, 1.0f};
  float texcoord[2] = {0.0f, 0.0f};
  uint32_t materialSlot = 0;
};

enum class PrimitiveTopology : uint32_t {
  TRIANGLE_LIST = 0,
  LINE_LIST = 1,
  POINT_LIST = 2
};

struct GeometryRange {
  uint32_t firstVertex = 0;
  uint32_t vertexCount = 0;
  uint32_t firstIndex = 0;
  uint32_t indexCount = 0;
};

struct DrawPacket {
  PrimitiveTopology topology = PrimitiveTopology::TRIANGLE_LIST;
  GeometryRange geometry = {};
  uint32_t renderStateSlot = 0;
  uint32_t frameNodeOrdinal = 0; // estável apenas dentro do frame/log
  SbUniqueId sourceNodeId = 0;   // cache/invalidação; não entra no golden log
  uint64_t stableNodeId = 0;
  uint32_t drawOrdinal = 0;
  uint64_t sourceRevision = 0;
};

struct MaterialSnapshot {
  float ambient[4] = {0.2f, 0.2f, 0.2f, 1.0f};
  float diffuse[4] = {0.8f, 0.8f, 0.8f, 1.0f};
  float specular[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float emission[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float shininess = 0.2f;
  float transparency = 0.0f;
};

enum class LightType : uint32_t {
  DIRECTIONAL = 0,
  POINT = 1,
  SPOT = 2
};

static const size_t COIN_WGPU_MAX_LIGHTS = 8;

// Contract: Light direction and position are strictly in View Space (camera space).
// Backends consume direction/position directly without applying camera viewMatrix again.
struct LightSourceSnapshot {
  LightType type = LightType::DIRECTIONAL;
  float color[3] = {1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  float direction[3] = {0.0f, 0.0f, -1.0f}; // View space direction
  float position[3] = {0.0f, 0.0f, 0.0f};   // View space position
  float cutOffAngle = 0.785398163f;
  float dropOffRate = 0.0f;
  float attenuation[3] = {0.0f, 0.0f, 1.0f}; // quadratic, linear, constant at light traversal
};

struct LightingSnapshot {
  std::vector<LightSourceSnapshot> lights;
  float ambientIntensity = 0.2f;
  float ambientColor[3] = {1.0f, 1.0f, 1.0f};
};

struct CameraSnapshot {
  SbMatrix viewMatrix = SbMatrix::identity();
  SbMatrix projectionMatrixCoin = SbMatrix::identity();
  bool isPerspective = true;
  float nearDistance = 0.1f;
  float farDistance = 100.0f;
  float focalDistance = 5.0f;
  float aspectRatio = 1.0f;
};

struct ViewportSnapshot {
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 640;
  int32_t height = 480;
};

enum class CullMode : uint32_t {
  NONE = 0,
  BACK = 1,
  FRONT = 2
};

enum class FrontFace : uint32_t {
  CCW = 0,
  CW = 1
};

enum class LightModel : uint32_t {
  BASE_COLOR = 0,
  PHONG = 1
};

enum class TextureWrap : uint32_t {
  REPEAT = 0,
  CLAMP = 1
};

enum class TextureFilter : uint32_t {
  NEAREST = 0,
  LINEAR = 1
};

enum class TextureModel : uint32_t {
  MODULATE = 0,
  REPLACE = 1,
  DECAL = 2,
  BLEND = 3
};

struct TextureImageSnapshot {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t components = 4; // Canonical RGBA8Unorm
  uint64_t contentDigest = 0;
  std::vector<uint8_t> pixelsRgba;
};

struct SamplerSnapshot {
  TextureWrap wrapS = TextureWrap::REPEAT;
  TextureWrap wrapT = TextureWrap::REPEAT;
  TextureFilter filter = TextureFilter::LINEAR;
};

enum class FogMode : uint32_t {
  NONE = 0,
  HAZE = 1,
  FOG = 2,
  SMOKE = 3
};

struct RenderStateSnapshot {
  SbMatrix model = SbMatrix::identity();
  SbMatrix view = SbMatrix::identity();
  SbMatrix projectionCoin = SbMatrix::identity();
  uint32_t materialSlot = 0;
  uint32_t lightingSlot = 0;
  uint32_t cameraSlot = 0;
  uint32_t viewportSlot = 0;
  CullMode cullMode = CullMode::BACK;
  FrontFace frontFace = FrontFace::CCW;
  LightModel lightModel = LightModel::PHONG;
  float lineWidth = 1.0f;
  float pointSize = 1.0f;
  SbMatrix textureMatrix = SbMatrix::identity();
  bool hasTexture = false;
  uint32_t textureImageSlot = 0;
  uint32_t samplerSlot = 0;
  TextureModel textureModel = TextureModel::MODULATE;
  FogMode fogMode = FogMode::NONE;
  float fogColor[3] = {1.0f, 1.0f, 1.0f};
  float fogStart = 0.0f;
  float fogEnd = 10.0f;
};

struct FramePlan {
  SbColor4f clearColor = SbColor4f(0.0f, 0.0f, 0.0f, 1.0f);
  std::vector<VertexSnapshot> vertices;
  std::vector<uint32_t> indices;
  std::vector<MaterialSnapshot> materials;
  std::vector<LightingSnapshot> lightingStates;
  std::vector<CameraSnapshot> cameras;
  std::vector<ViewportSnapshot> viewports;
  std::vector<RenderStateSnapshot> renderStates;
  std::vector<TextureImageSnapshot> textures;
  std::vector<SamplerSnapshot> samplers;
  std::vector<DrawPacket> draws;

  inline bool isValid(std::string * outDiagnostic = nullptr) const {
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

    // Validate clear color
    for (int i = 0; i < 4; ++i) {
      if (!isFiniteF(clearColor[i])) {
        if (outDiagnostic) *outDiagnostic = "Invalid clearColor (NaN or inf)";
        return false;
      }
    }

    // Validate vertices
    const size_t numVertices = vertices.size();
    for (size_t i = 0; i < numVertices; ++i) {
      const auto & v = vertices[i];
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
      if (materials.empty() || v.materialSlot >= materials.size()) {
        if (outDiagnostic) *outDiagnostic = "Vertex references out-of-range material slot";
        return false;
      }
    }

    // Validate indices
    const size_t numIndices = indices.size();
    for (size_t i = 0; i < numIndices; ++i) {
      if (indices[i] >= numVertices) {
        if (outDiagnostic) *outDiagnostic = "Index out of range of vertex buffer";
        return false;
      }
    }

    // Validate cameras
    for (size_t i = 0; i < cameras.size(); ++i) {
      const auto & c = cameras[i];
      if (!isMatrixFinite(c.viewMatrix) || !isMatrixFinite(c.projectionMatrixCoin)) {
        if (outDiagnostic) *outDiagnostic = "Camera matrix contains non-finite values";
        return false;
      }
      if (!isFiniteF(c.nearDistance) || !isFiniteF(c.farDistance) || c.farDistance <= c.nearDistance) {
        if (outDiagnostic) *outDiagnostic = "Camera clip planes are invalid or inverted";
        return false;
      }
    }

    // Validate textures
    for (size_t i = 0; i < textures.size(); ++i) {
      const auto & tex = textures[i];
      if (tex.width == 0 || tex.height == 0) {
        if (outDiagnostic) *outDiagnostic = "Texture contains zero width or height";
        return false;
      }
      if (tex.width > 8192 || tex.height > 8192) {
        if (outDiagnostic) *outDiagnostic = "Texture dimensions exceed 8192 limit";
        return false;
      }
      uint64_t expectedBytes = static_cast<uint64_t>(tex.width) * static_cast<uint64_t>(tex.height) * 4ULL;
      if (tex.pixelsRgba.size() != expectedBytes) {
        if (outDiagnostic) *outDiagnostic = "Texture pixel buffer size mismatch";
        return false;
      }
    }

    // The Coin 4 experimental module has a fixed, explicit per-draw light budget.
    for (size_t i = 0; i < lightingStates.size(); ++i) {
      const LightingSnapshot & ls = lightingStates[i];
      if (ls.lights.size() > COIN_WGPU_MAX_LIGHTS) {
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
        const LightSourceSnapshot & l = ls.lights[j];
        if (l.type != LightType::DIRECTIONAL && l.type != LightType::POINT && l.type != LightType::SPOT) {
          if (outDiagnostic) *outDiagnostic = "Unsupported light type";
          return false;
        }
        if (!isFiniteF(l.intensity) || l.intensity < 0.0f ||
            !isFiniteF(l.cutOffAngle) || !isFiniteF(l.dropOffRate)) {
          if (outDiagnostic) *outDiagnostic = "Invalid light intensity or cone";
          return false;
        }
        bool anyAttenuation = false;
        for (int c = 0; c < 3; ++c) {
          if (!isFiniteF(l.color[c]) || !isFiniteF(l.direction[c]) || !isFiniteF(l.position[c]) ||
              !isFiniteF(l.attenuation[c]) || l.attenuation[c] < 0.0f) {
            if (outDiagnostic) *outDiagnostic = "Invalid light vector, color or attenuation";
            return false;
          }
          anyAttenuation = anyAttenuation || l.attenuation[c] > 0.0f;
        }
        if (l.type != LightType::DIRECTIONAL && !anyAttenuation) {
          if (outDiagnostic) *outDiagnostic = "Degenerate positional light attenuation";
          return false;
        }
        if (l.type != LightType::POINT) {
          const float dirLengthSq = l.direction[0] * l.direction[0] +
            l.direction[1] * l.direction[1] + l.direction[2] * l.direction[2];
          if (dirLengthSq <= 1.0e-12f) {
            if (outDiagnostic) *outDiagnostic = "Zero light direction";
            return false;
          }
        }
        if (l.type == LightType::SPOT &&
            (l.cutOffAngle < 0.0f || l.cutOffAngle > 1.570796327f ||
             l.dropOffRate < 0.0f || l.dropOffRate > 1.0f)) {
          if (outDiagnostic) *outDiagnostic = "Spot cone outside supported range";
          return false;
        }
      }
    }

    // Validate render states textureMatrix and slots
    for (size_t i = 0; i < renderStates.size(); ++i) {
      const auto & rs = renderStates[i];
      if (rs.fogMode != FogMode::NONE && rs.fogMode != FogMode::HAZE &&
          rs.fogMode != FogMode::FOG && rs.fogMode != FogMode::SMOKE) {
        if (outDiagnostic) *outDiagnostic = "Unsupported fog mode";
        return false;
      }
      if (!isFiniteF(rs.fogStart) || !isFiniteF(rs.fogEnd) ||
          (rs.fogMode != FogMode::NONE && rs.fogEnd <= 0.0f) ||
          (rs.fogMode == FogMode::HAZE && rs.fogEnd <= rs.fogStart)) {
        if (outDiagnostic) *outDiagnostic = "Invalid fog range";
        return false;
      }
      for (int c = 0; c < 3; ++c) {
        if (!isFiniteF(rs.fogColor[c])) {
          if (outDiagnostic) *outDiagnostic = "Invalid fog color";
          return false;
        }
      }
      if (rs.hasTexture) {
        if (!isMatrixFinite(rs.textureMatrix)) {
          if (outDiagnostic) *outDiagnostic = "RenderState contains non-finite texture matrix";
          return false;
        }
        if (rs.textureImageSlot >= textures.size()) {
          if (outDiagnostic) *outDiagnostic = "RenderState references out-of-bounds texture image slot";
          return false;
        }
        if (rs.samplerSlot >= samplers.size()) {
          if (outDiagnostic) *outDiagnostic = "RenderState references out-of-bounds sampler slot";
          return false;
        }
      }
    }

    // Validate draws
    for (size_t i = 0; i < draws.size(); ++i) {
      const auto & d = draws[i];
      if (d.renderStateSlot >= renderStates.size()) {
        if (outDiagnostic) *outDiagnostic = "Draw references invalid renderStateSlot";
        return false;
      }
      const auto & geom = d.geometry;

      // Safe subtraction validation against 32-bit overflow (B05)
      if (geom.firstVertex > numVertices || geom.vertexCount > (numVertices - geom.firstVertex)) {
        if (outDiagnostic) *outDiagnostic = "Draw vertex range out of bounds";
        return false;
      }
      if (geom.firstIndex > numIndices || geom.indexCount > (numIndices - geom.firstIndex)) {
        if (outDiagnostic) *outDiagnostic = "Draw index range out of bounds";
        return false;
      }

      if (d.topology == PrimitiveTopology::TRIANGLE_LIST && (geom.indexCount % 3 != 0)) {
        if (outDiagnostic) *outDiagnostic = "Triangle list index count is not multiple of 3";
        return false;
      }
      if (d.topology == PrimitiveTopology::LINE_LIST && (geom.indexCount % 2 != 0)) {
        if (outDiagnostic) *outDiagnostic = "Line list index count is not multiple of 2";
        return false;
      }

      const auto & rs = renderStates[d.renderStateSlot];
      if (!isMatrixFinite(rs.model) || !isMatrixFinite(rs.view) || !isMatrixFinite(rs.projectionCoin)) {
        if (outDiagnostic) *outDiagnostic = "RenderState matrix contains non-finite values";
        return false;
      }
      if (materials.empty() || rs.materialSlot >= materials.size()) {
        if (outDiagnostic) *outDiagnostic = "RenderState materialSlot out of range";
        return false;
      }
      if (lightingStates.empty() || rs.lightingSlot >= lightingStates.size()) {
        if (outDiagnostic) *outDiagnostic = "RenderState lightingSlot out of range";
        return false;
      }
      if (d.topology == PrimitiveTopology::TRIANGLE_LIST && rs.lightModel == LightModel::PHONG) {
        const SbMatrix modelView = rs.model * rs.view;
        const float determinant = modelView.det4();
        if (!isFiniteF(determinant) || std::abs(determinant) <= 1.0e-12f) {
          if (outDiagnostic) *outDiagnostic = "Singular model-view normal matrix";
          return false;
        }
      }

      if (cameras.empty() || rs.cameraSlot >= cameras.size()) {
        if (outDiagnostic) *outDiagnostic = "RenderState cameraSlot out of range";
        return false;
      }
      if (viewports.empty() || rs.viewportSlot >= viewports.size()) {
        if (outDiagnostic) *outDiagnostic = "RenderState viewportSlot out of range";
        return false;
      }
    }

    return true;
  }
};

#endif // !COIN_SOWGPUFRAMEPLAN_H
