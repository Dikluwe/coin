#ifndef COIN_RENDER_POLYGON_STYLE_CORE_H
#define COIN_RENDER_POLYGON_STYLE_CORE_H

#include "rendering/coinrender/CoinRenderClipCore.h"
#include "rendering/coinrender/CoinRenderLightingCore.h"
#include "rendering/coinrender/CoinRenderTextureCoordinateCore.h"
#include <Inventor/SbVec3d.h>
#include <limits>

struct CoinRenderPolygonStyleVertex {
  CoinRenderVertexSnapshot vertex;
  CoinRenderMaterialSnapshot material;
  bool boundaryEdge = true; // Boundary flag of the outgoing edge.
};

// Original convex polygon ring; no scene traversal, Coin elements or GPU calls.
// Outputs a clipped, culled ring with Gouraud colors baked before interpolation.
inline bool coin_render_resolve_polygon_style(
    const std::vector<CoinRenderVertexSnapshot>& ring, const CoinRenderRenderStateSnapshot& state,
    const std::vector<CoinRenderMaterialSnapshot>& materials,
    const CoinRenderLightingSnapshot& lighting, std::vector<CoinRenderPolygonStyleVertex>& output,
    std::string& diagnostic, bool preserveDegenerateContour = false,
    const bool * boundaryEdges = nullptr) {
  output.clear();
  if (ring.size() < 3) {
    diagnostic = "Polygon style requires at least three vertices";
    return false;
  }
  float equations[COIN_RENDER_MAX_CLIP_PLANES][4] = {};
  if (!coin_render_clip_equations(state, equations, diagnostic))
    return false;
  for (const auto& vertex : ring)
    for (int c = 0; c < 3; ++c)
      if (!std::isfinite(vertex.position[c]) || !std::isfinite(vertex.normal[c])) {
        diagnostic = "Invalid styled polygon vertex";
        return false;
      }
  // Convexity is a contract of this profile. Reject concave rings rather than
  // join disjoint clipped components or expose tessellation diagonals.
  SbVec3f normal(0, 0, 0);
  for (size_t i = 0; i < ring.size(); ++i) {
    const SbVec3f a(ring[i].position), b(ring[(i + 1) % ring.size()].position);
    const SbVec3f origin(ring[0].position);
    normal += (a - origin).cross(b - origin);
  }
  if (normal.normalize() == 0 && !preserveDegenerateContour)
    return true;
  for (size_t i = 0; i < ring.size(); ++i) {
    const SbVec3f a(ring[i].position), b(ring[(i + 1) % ring.size()].position),
        c(ring[(i + 2) % ring.size()].position);
    if ((b - a).cross(c - b).dot(normal) < -1e-6f * (b - a).length() * (c - b).length()) {
      diagnostic = "UNSUPPORTED: polygon styles currently require convex contours";
      return false;
    }
  }
  const SbMatrix modelView = state.model * state.view;
  const SbMatrix mvp = modelView * state.projectionCoin;
  if (state.lightModel == CoinRenderLightModel::PHONG &&
      (!std::isfinite(modelView.det4()) || std::abs(modelView.det4()) <= 1e-12f)) {
    diagnostic = "Invalid model-view matrix for styled polygon lighting";
    return false;
  }
  const SbMatrix normals = state.lightModel == CoinRenderLightModel::PHONG
                               ? modelView.inverse().transpose()
                               : SbMatrix::identity();
  for (const auto& vertex : ring) {
    if (vertex.materialSlot >= materials.size()) {
      diagnostic = "Invalid styled polygon material";
      return false;
    }
    CoinRenderPolygonStyleVertex item;
    item.vertex = vertex;
    if (boundaryEdges) item.boundaryEdge = boundaryEdges[output.size()];
    item.material = materials[vertex.materialSlot];
    SbVec3f eye, n;
    modelView.multVecMatrix(SbVec3f(vertex.position), eye);
    normals.multDirMatrix(SbVec3f(vertex.normal), n);
    n.normalize();
    const SbVec4f color = coin_render_shade_vertex(item.material, eye, n, lighting, state);
    for (int c = 0; c < 4; ++c)
      item.material.diffuse[c] = color[c];
    output.push_back(item);
  }
  auto interpolate = [](const CoinRenderPolygonStyleVertex& a,
                        const CoinRenderPolygonStyleVertex& b, float t) {
    CoinRenderPolygonStyleVertex out = a;
    out.vertex = coin_render_clip_interpolate(a.vertex, b.vertex, t, 0);
    for (int c = 0; c < 4; ++c) {
      out.material.ambient[c] =
          a.material.ambient[c] + (b.material.ambient[c] - a.material.ambient[c]) * t;
      out.material.diffuse[c] =
          a.material.diffuse[c] + (b.material.diffuse[c] - a.material.diffuse[c]) * t;
      out.material.specular[c] =
          a.material.specular[c] + (b.material.specular[c] - a.material.specular[c]) * t;
      out.material.emission[c] =
          a.material.emission[c] + (b.material.emission[c] - a.material.emission[c]) * t;
    }
    out.material.shininess =
        a.material.shininess + (b.material.shininess - a.material.shininess) * t;
    out.material.transparency =
        a.material.transparency + (b.material.transparency - a.material.transparency) * t;
    return out;
  };
  // Ordinary styles clip the original ring. Native GL_QUADS point capture
  // supplies triangles with boundary flags to retain clip-created points.
  for (size_t plane = 0; plane < 6 + state.clipPlanesWorld.size() && !output.empty(); ++plane) {
    auto distance = [&](const CoinRenderPolygonStyleVertex& item) {
      if (plane < 6) {
        SbVec4f clip;
        mvp.multVecMatrix(
            SbVec4f(item.vertex.position[0], item.vertex.position[1], item.vertex.position[2], 1),
            clip);
        return clip[3] + (plane % 2 == 0 ? clip[plane / 2] : -clip[plane / 2]);
      }
      SbVec3f eye;
      modelView.multVecMatrix(SbVec3f(item.vertex.position), eye);
      const float* equation = equations[plane - 6];
      return equation[0] * eye[0] + equation[1] * eye[1] + equation[2] * eye[2] + equation[3];
    };
    std::vector<CoinRenderPolygonStyleVertex> clipped;
    auto previous = output.back();
    float previousDistance = distance(previous);
    for (const auto& current : output) {
      const float currentDistance = distance(current);
      if (!std::isfinite(previousDistance) || !std::isfinite(currentDistance)) {
        diagnostic = "Invalid styled polygon clip coordinates";
        return false;
      }
      if ((previousDistance < 0) != (currentDistance < 0)) {
        auto intersection = interpolate(previous, current,
          previousDistance / (previousDistance - currentDistance));
        intersection.boundaryEdge = previousDistance < 0 ? previous.boundaryEdge : true;
        clipped.push_back(intersection);
      }
      if (currentDistance >= 0)
        clipped.push_back(current);
      previous = current;
      previousDistance = currentDistance;
    }
    output.swap(clipped);
  }
  // Remove coincident vertices introduced when a plane passes through a corner.
  for (size_t i = 0; !preserveDegenerateContour && i < output.size() && output.size() > 1;) {
    const size_t next = (i + 1) % output.size();
    if (SbVec3f(output[i].vertex.position) == SbVec3f(output[next].vertex.position))
      output.erase(output.begin() + next);
    else
      ++i;
  }
  if (output.empty()) return true;
  if (output.size() < 3 && !(preserveDegenerateContour && boundaryEdges)) {
    output.clear();
    return true;
  }
  double area = 0;
  SbVec3f first, previous;
  for (size_t i = 0; i < output.size(); ++i) {
    SbVec4f clip;
    mvp.multVecMatrix(SbVec4f(output[i].vertex.position[0], output[i].vertex.position[1],
                              output[i].vertex.position[2], 1),
                      clip);
    if (!std::isfinite(clip[3]) || clip[3] <= 1e-6f) {
      diagnostic = "UNSUPPORTED: styled polygon reaches the projective singularity";
      return false;
    }
    SbVec3f current(clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3]);
    if (i == 0)
      first = current;
    else
      area += double(previous[0]) * current[1] - double(current[0]) * previous[1];
    previous = current;
  }
  area += double(previous[0]) * first[1] - double(first[0]) * previous[1];
  const double oriented = state.frontFace == CoinRenderFrontFace::CW ? -area : area;
  // POINT/LINE rasterization still has boundaries when the projected area is
  // zero and culling is disabled. Zero area is back-facing in the GL contract.
  if ((state.cullMode == CoinRenderCullMode::BACK && oriented <= 0) ||
      (state.cullMode == CoinRenderCullMode::FRONT && oriented > 0))
    output.clear();
  return true;
}
enum class CoinRenderPolygonStyle { LINES, POINTS };
struct CoinRenderPolygonStyleResult {
  std::vector<CoinRenderPolygonStyleVertex> vertices;
  std::vector<uint32_t> indices;
  CoinRenderRenderStateSnapshot state;
  CoinRenderPrimitiveTopology topology = CoinRenderPrimitiveTopology::LINE_LIST;
};
inline bool coin_render_prepare_polygon_style(
    const std::vector<CoinRenderVertexSnapshot>& ring, const CoinRenderRenderStateSnapshot& state,
    const std::vector<CoinRenderMaterialSnapshot>& materials,
    const CoinRenderLightingSnapshot& lighting, CoinRenderPolygonStyle style,
    CoinRenderPolygonStyleResult& result, std::string& diagnostic,
    const CoinRenderViewportSnapshot& viewport, bool preserveDegenerateContour = false) {
  result = CoinRenderPolygonStyleResult{};
  if (style == CoinRenderPolygonStyle::LINES) {
    if (!coin_render_validate_texture_coordinates(state, ring.size(),
        [&](size_t i) -> const CoinRenderVertexSnapshot & { return ring[i]; }, diagnostic)) return false;
  } else {
    for (const auto & vertex : ring)
      if (!coin_render_validate_texture_coordinates(state, 1,
          [&](size_t) -> const CoinRenderVertexSnapshot & { return vertex; }, diagnostic)) return false;
  }
  const uint32_t primitiveStyle = style == CoinRenderPolygonStyle::LINES ? 2u : 4u;
  if (preserveDegenerateContour && style == CoinRenderPolygonStyle::POINTS && ring.size() == 4) {
    // Qualified native GL_QUADS point raster: two triangles preserve original
    // boundary flags, while clip-created edges acquire their own point starts.
    // This retains a cut vertex on the implicit diagonal without exposing
    // that diagonal in ordinary line or uncut point rasterization.
    static const size_t indices[2][3] = {{0, 1, 2}, {0, 2, 3}};
    static const bool edges[2][3] = {{true, true, false}, {false, true, true}};
    for (size_t triangle = 0; triangle < 2; ++triangle) {
      std::vector<CoinRenderVertexSnapshot> primitive;
      for (size_t index : indices[triangle]) primitive.push_back(ring[index]);
      std::vector<CoinRenderPolygonStyleVertex> clipped;
      if (!coin_render_resolve_polygon_style(primitive, state, materials, lighting, clipped,
          diagnostic, true, edges[triangle])) return false;
      for (const auto & vertex : clipped)
        if (vertex.boundaryEdge) result.vertices.push_back(vertex);
    }
  } else if (!coin_render_resolve_polygon_style(ring, state, materials, lighting, result.vertices,
                                                diagnostic, preserveDegenerateContour)) return false;
  const bool lines = style == CoinRenderPolygonStyle::LINES;
  result.topology =
      lines ? CoinRenderPrimitiveTopology::LINE_LIST : CoinRenderPrimitiveTopology::POINT_LIST;
  result.state = state;
  result.state.lightModel = CoinRenderLightModel::BASE_COLOR;
  result.state.cullMode = CoinRenderCullMode::NONE;
  result.state.clipPlanesWorld.clear();
  result.state.polygonOffsetPrimitiveStyle = lines ? 2u : 4u;
  result.state.polygonLinePattern = lines;
  // Stipple can remove the deepest corner. Preserve the original face metric;
  // the GPU backend owns the depth format's quantum.
  if (state.polygonOffsetEnabled && (state.polygonOffsetStyles & primitiveStyle) &&
      !result.vertices.empty()) {
    const SbMatrix mvp = state.model * state.view * state.projectionCoin;
    float maximum = 0;
    for (const auto& item : result.vertices) {
      SbVec4f clip;
      mvp.multVecMatrix(
          SbVec4f(item.vertex.position[0], item.vertex.position[1], item.vertex.position[2], 1),
          clip);
      const float depth = state.depthRange[0] + (clip[2] / clip[3] * .5f + .5f) *
                                                    (state.depthRange[1] - state.depthRange[0]);
      if (!std::isfinite(depth)) {
        diagnostic = "Invalid original polygon depth";
        return false;
      }
      maximum = std::max(maximum, std::max(0.0f, std::min(1.0f, depth)));
    }
    result.state.polygonOffsetMaxDepth = maximum;
  }
  // The expanded strokes have another depth gradient. Resolve the original
  // planar face in window coordinates, keeping the bias out of vertex clipping.
  if (state.polygonOffsetEnabled && (state.polygonOffsetStyles & primitiveStyle) &&
      state.polygonOffsetFactor != 0 && !result.vertices.empty()) {
    if (viewport.width <= 0 || viewport.height <= 0) {
      diagnostic = "Invalid viewport for polygon slope offset";
      return false;
    }
    const SbVec3f origin(ring[0].position);
    SbVec3f normal(0, 0, 0);
    double extent = 0;
    for (size_t i = 1; i < ring.size(); ++i) {
      const SbVec3f delta = SbVec3f(ring[i].position) - origin;
      extent = std::max(extent, double(delta.length()));
      if (i + 1 < ring.size())
        normal += delta.cross(SbVec3f(ring[i + 1].position) - origin);
    }
    normal.normalize();
    for (const auto& vertex : ring) {
      if (std::abs((SbVec3f(vertex.position) - origin).dot(normal)) > 1e-5 * extent) {
        diagnostic = "UNSUPPORTED: polygon slope offset requires a planar original face";
        return false;
      }
    }
    const SbMatrix mvp = state.model * state.view * state.projectionCoin;
    std::vector<SbVec3d> window;
    for (const auto& vertex : result.vertices) {
      SbVec4f clip;
      mvp.multVecMatrix(SbVec4f(vertex.vertex.position[0], vertex.vertex.position[1],
                                vertex.vertex.position[2], 1),
                        clip);
      window.push_back(
          SbVec3d(double(clip[0]) / clip[3] * viewport.width * .5,
                  double(clip[1]) / clip[3] * viewport.height * .5,
                  double(clip[2]) / clip[3] * .5 * (state.depthRange[1] - state.depthRange[0])));
    }
    // Choose the largest determinant to avoid a nearly collinear corner triple.
    double determinant = 0, dx = 0, dy = 0;
    for (size_t i = 1; i + 1 < window.size(); ++i) {
      const SbVec3d a = window[i] - window[0], b = window[i + 1] - window[0];
      const double det = a[0] * b[1] - a[1] * b[0];
      if (std::abs(det) > std::abs(determinant)) {
        determinant = det;
        dx = (a[2] * b[1] - a[1] * b[2]) / det;
        dy = (a[0] * b[2] - a[2] * b[0]) / det;
      }
    }
    if (determinant == 0) {
      diagnostic = "UNSUPPORTED: polygon slope offset has zero projected area";
      return false;
    }
    const double bias = state.polygonOffsetFactor * std::max(std::abs(dx), std::abs(dy));
    if (!std::isfinite(bias) || std::abs(bias) > std::numeric_limits<float>::max()) {
      diagnostic = "Invalid polygon slope offset";
      return false;
    }
    result.state.polygonOffsetSlopeBias = float(bias);
    result.state.polygonOffsetFactor = 0;
  }
  for (uint32_t i = 0; i < result.vertices.size(); ++i) {
    if (lines) {
      // Coin emits triangles/quads separately, and larger rings as GL_POLYGON.
      // Their first boundary differs with the provoking-vertex convention.
      result.indices.push_back((i + (ring.size() > 4 ? 0 : result.vertices.size() - 1)) %
                               result.vertices.size());
      result.indices.push_back((i + (ring.size() > 4 ? 1 : 0)) % result.vertices.size());
    } else {
      result.indices.push_back(i);
    }
  }
  return true;
}
#endif
