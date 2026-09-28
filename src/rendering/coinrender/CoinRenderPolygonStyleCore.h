#ifndef COIN_RENDER_POLYGON_STYLE_CORE_H
#define COIN_RENDER_POLYGON_STYLE_CORE_H

#include "rendering/coinrender/CoinRenderClipCore.h"
#include "rendering/coinrender/CoinRenderLightingCore.h"

struct CoinRenderPolygonStyleVertex {
  CoinRenderVertexSnapshot vertex;
  CoinRenderMaterialSnapshot material;
};

// Original convex polygon ring; no scene traversal, Coin elements or GPU calls.
// Outputs a clipped, culled ring with Gouraud colors baked before interpolation.
inline bool coin_render_resolve_polygon_style(
    const std::vector<CoinRenderVertexSnapshot>& ring, const CoinRenderRenderStateSnapshot& state,
    const std::vector<CoinRenderMaterialSnapshot>& materials,
    const CoinRenderLightingSnapshot& lighting, std::vector<CoinRenderPolygonStyleVertex>& output,
    std::string& diagnostic) {
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
  if (normal.normalize() == 0)
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
  // Clip the original polygon, creating complete cut edges. Clipping its
  // triangulation separately would split those edges and duplicate points.
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
      if ((previousDistance < 0) != (currentDistance < 0))
        clipped.push_back(interpolate(previous, current,
                                      previousDistance / (previousDistance - currentDistance)));
      if (currentDistance >= 0)
        clipped.push_back(current);
      previous = current;
      previousDistance = currentDistance;
    }
    output.swap(clipped);
  }
  // Remove coincident vertices introduced when a plane passes through a corner.
  for (size_t i = 0; i < output.size() && output.size() > 1;) {
    const size_t next = (i + 1) % output.size();
    if (SbVec3f(output[i].vertex.position) == SbVec3f(output[next].vertex.position))
      output.erase(output.begin() + next);
    else
      ++i;
  }
  if (output.size() < 3) {
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
    CoinRenderPolygonStyleResult& result, std::string& diagnostic) {
  result = CoinRenderPolygonStyleResult{};
  if (style == CoinRenderPolygonStyle::LINES && state.linePattern != 0xffffu &&
      state.linePattern != 0) {
    diagnostic =
        "UNSUPPORTED: patterned polygon boundaries require continuous Coin stipple semantics";
    return false;
  }
  const uint32_t primitiveStyle = style == CoinRenderPolygonStyle::LINES ? 2u : 4u;
  if (state.polygonOffsetEnabled && (state.polygonOffsetStyles & primitiveStyle) &&
      state.polygonOffsetFactor != 0) {
    diagnostic =
        "UNSUPPORTED: styled polygon slope offset requires the original polygon depth gradient";
    return false;
  }
  if (!coin_render_resolve_polygon_style(ring, state, materials, lighting, result.vertices,
                                         diagnostic))
    return false;
  const bool lines = style == CoinRenderPolygonStyle::LINES;
  result.topology =
      lines ? CoinRenderPrimitiveTopology::LINE_LIST : CoinRenderPrimitiveTopology::POINT_LIST;
  result.state = state;
  result.state.lightModel = CoinRenderLightModel::BASE_COLOR;
  result.state.cullMode = CoinRenderCullMode::NONE;
  result.state.clipPlanesWorld.clear();
  result.state.polygonOffsetPrimitiveStyle = lines ? 2u : 4u;
  for (uint32_t i = 0; i < result.vertices.size(); ++i) {
    result.indices.push_back(i);
    if (lines)
      result.indices.push_back((i + 1) % result.vertices.size());
  }
  return true;
}
#endif
