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
  float position[3];
  float normal[3];
  float texcoord[2];
  uint32_t materialSlot;
};

enum class PrimitiveTopology : uint32_t {
  TRIANGLE_LIST = 0,
  LINE_LIST = 1,
  POINT_LIST = 2
};

struct GeometryRange {
  uint32_t firstVertex;
  uint32_t vertexCount;
  uint32_t firstIndex;
  uint32_t indexCount;
};

struct DrawPacket {
  PrimitiveTopology topology;
  GeometryRange geometry;
  uint32_t renderStateSlot;
  uint32_t frameNodeOrdinal; // estável apenas dentro do frame/log
  SbUniqueId sourceNodeId;   // cache/invalidação; não entra no golden log
};

struct MaterialSnapshot {
  float ambient[4];
  float diffuse[4];
  float specular[4];
  float emission[4];
  float shininess;
  float transparency;
};

enum class LightType : uint32_t {
  DIRECTIONAL = 0,
  POINT = 1,
  SPOT = 2
};

struct LightSourceSnapshot {
  LightType type;
  float color[3];
  float intensity;
  float direction[3];
  float position[3];
};

struct LightingSnapshot {
  std::vector<LightSourceSnapshot> lights;
};

struct CameraSnapshot {
  SbMatrix viewMatrix;
  SbMatrix projectionMatrixCoin;
  bool isPerspective;
  float nearDistance;
  float farDistance;
  float focalDistance;
  float aspectRatio;
};

struct ViewportSnapshot {
  int32_t x;
  int32_t y;
  int32_t width;
  int32_t height;
};

struct RenderStateSnapshot {
  SbMatrix model;
  SbMatrix view;
  SbMatrix projectionCoin;
  uint32_t materialSlot;
  uint32_t lightingSlot;
  uint32_t cameraSlot;
  uint32_t viewportSlot;
};

struct FramePlan {
  SbColor4f clearColor;
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

    // Validate clear color
    for (int i = 0; i < 4; ++i) {
      if (!isFiniteF(clearColor[i])) {
        if (outDiagnostic) *outDiagnostic = "Invalid clearColor (NaN or inf)";
        return false;
      }
    }

    // Validate vertices
    for (size_t i = 0; i < vertices.size(); ++i) {
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
      if (v.materialSlot >= materials.size() && !materials.empty()) {
        if (outDiagnostic) *outDiagnostic = "Vertex references out-of-range material slot";
        return false;
      }
    }

    // Validate indices
    for (size_t i = 0; i < indices.size(); ++i) {
      if (indices[i] >= vertices.size()) {
        if (outDiagnostic) *outDiagnostic = "Index out of range of vertex buffer";
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
      if (geom.firstVertex + geom.vertexCount > vertices.size()) {
        if (outDiagnostic) *outDiagnostic = "Draw vertex range out of bounds";
        return false;
      }
      if (geom.firstIndex + geom.indexCount > indices.size()) {
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
      if (rs.materialSlot >= materials.size() && !materials.empty()) {
        if (outDiagnostic) *outDiagnostic = "RenderState materialSlot out of range";
        return false;
      }
      if (rs.lightingSlot >= lightingStates.size() && !lightingStates.empty()) {
        if (outDiagnostic) *outDiagnostic = "RenderState lightingSlot out of range";
        return false;
      }
      if (rs.cameraSlot >= cameras.size() && !cameras.empty()) {
        if (outDiagnostic) *outDiagnostic = "RenderState cameraSlot out of range";
        return false;
      }
      if (rs.viewportSlot >= viewports.size() && !viewports.empty()) {
        if (outDiagnostic) *outDiagnostic = "RenderState viewportSlot out of range";
        return false;
      }
    }

    return true;
  }
};

#endif // !COIN_SOWGPUFRAMEPLAN_H
