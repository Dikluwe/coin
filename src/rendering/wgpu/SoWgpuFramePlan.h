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

// Contract: Light direction and position are strictly in View Space (camera space).
// Backends consume direction/position directly without applying camera viewMatrix again.
struct LightSourceSnapshot {
  LightType type = LightType::DIRECTIONAL;
  float color[3] = {1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  float direction[3] = {0.0f, 0.0f, -1.0f}; // View space direction
  float position[3] = {0.0f, 0.0f, 0.0f};   // View space position
};

struct LightingSnapshot {
  std::vector<LightSourceSnapshot> lights;
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
