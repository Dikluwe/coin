#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderPolygonStyleCore.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <utility>

CoinRenderMaterialSnapshot
coin_render_bake_vertex_material(const CoinRenderVertexSnapshot &provoking,
                                 const CoinRenderMaterialSnapshot &material,
                                 const CoinRenderRenderStateSnapshot &state,
                                 const CoinRenderLightingSnapshot &lighting)
{
  const SbMatrix modelView = state.model * state.view;
  auto baked = material;
  SbVec3f eye(0, 0, 0), normal(0, 0, 1);
  if (state.lightModel == CoinRenderLightModel::PHONG &&
      (!std::isfinite(modelView.det4()) || std::abs(modelView.det4()) <= 1e-12f)) {
    baked.diffuse[0] = std::numeric_limits<float>::quiet_NaN();
    return baked; // Frame validation rejects the source before publication.
  }
  modelView.multVecMatrix(SbVec3f(provoking.position), eye);
  if (state.lightModel == CoinRenderLightModel::PHONG)
    modelView.inverse().transpose().multDirMatrix(SbVec3f(provoking.normal), normal);
  normal.normalize();
  const SbVec4f color = coin_render_shade_vertex(material, eye, normal, lighting, state);
  for (int c = 0; c < 4; ++c)
    baked.diffuse[c] = color[c];
  return baked;
}

bool
coin_render_resolve_polygon_style(const std::vector<CoinRenderVertexSnapshot> &ring,
                                  const CoinRenderRenderStateSnapshot &state,
                                  const std::vector<CoinRenderMaterialSnapshot> &materials,
                                  const CoinRenderLightingSnapshot &lighting,
                                  std::vector<CoinRenderPolygonStyleVertex> &output,
                                  std::string &diagnostic, bool preserveDegenerateContour,
                                  const bool *boundaryEdges)
{
  output.clear();
  if (ring.size() < 3) {
    diagnostic = "Polygon style requires at least three vertices";
    return false;
  }
  float equations[COIN_RENDER_MAX_CLIP_PLANES][4] = {};
  if (!coin_render_clip_equations(state, equations, diagnostic))
    return false;
  for (const auto &vertex : ring)
    for (int c = 0; c < 3; ++c)
      if (!std::isfinite(vertex.position[c]) || !std::isfinite(vertex.normal[c])) {
        diagnostic = "Invalid styled polygon vertex";
        return false;
      }
  // Preserve the authored boundary, including reflex corners. Clipping below
  // accepts a connected contour; a plane that separates it into multiple
  // components must be handled as multiple rings before it can be emitted.
  SbVec3f normal(0, 0, 0);
  for (size_t i = 0; i < ring.size(); ++i) {
    const SbVec3f a(ring[i].position), b(ring[(i + 1) % ring.size()].position);
    const SbVec3f origin(ring[0].position);
    normal += (a - origin).cross(b - origin);
  }
  if (normal.normalize() == 0 && !preserveDegenerateContour)
    return true;
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
  for (const auto &vertex : ring) {
    if (vertex.materialSlot >= materials.size()) {
      diagnostic = "Invalid styled polygon material";
      return false;
    }
    CoinRenderPolygonStyleVertex item;
    item.vertex = vertex;
    if (boundaryEdges)
      item.boundaryEdge = boundaryEdges[output.size()];
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
  auto interpolate = [](const CoinRenderPolygonStyleVertex &a,
                        const CoinRenderPolygonStyleVertex &b, float t) {
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
    auto distance = [&](const CoinRenderPolygonStyleVertex &item) {
      if (plane < 6) {
        SbVec4f clip;
        mvp.multVecMatrix(
            SbVec4f(item.vertex.position[0], item.vertex.position[1], item.vertex.position[2], 1),
            clip);
        return clip[3] + (plane % 2 == 0 ? clip[plane / 2] : -clip[plane / 2]);
      }
      SbVec3f eye;
      modelView.multVecMatrix(SbVec3f(item.vertex.position), eye);
      const float *equation = equations[plane - 6];
      return equation[0] * eye[0] + equation[1] * eye[1] + equation[2] * eye[2] + equation[3];
    };
    std::vector<CoinRenderPolygonStyleVertex> clipped;
    auto previous = output.back();
    float previousDistance = distance(previous);
    size_t crossings = 0;
    for (const auto &current : output) {
      const float currentDistance = distance(current);
      if (!std::isfinite(previousDistance) || !std::isfinite(currentDistance)) {
        diagnostic = "Invalid styled polygon clip coordinates";
        return false;
      }
      if ((previousDistance < 0) != (currentDistance < 0)) {
        ++crossings;
        auto intersection =
            interpolate(previous, current, previousDistance / (previousDistance - currentDistance));
        intersection.boundaryEdge = previousDistance < 0 ? previous.boundaryEdge : true;
        clipped.push_back(intersection);
      }
      if (currentDistance >= 0)
        clipped.push_back(current);
      previous = current;
      previousDistance = currentDistance;
    }
    if (crossings > 2) {
      diagnostic = "UNSUPPORTED: clipped concave polygon has disconnected contours";
      return false;
    }
    output.swap(clipped);
  }
  // Remove only identical coincident vertices introduced when a clip plane
  // passes through a corner. Authored occurrences at the same position may
  // carry different outgoing material or texture coordinates.
  auto sameFloats = [](const float *a, const float *b, size_t count) {
    for (size_t c = 0; c < count; ++c)
      if (a[c] != b[c]) return false;
    return true;
  };
  auto sameAttributes = [&](const CoinRenderPolygonStyleVertex &a,
                            const CoinRenderPolygonStyleVertex &b) {
    const auto &va = a.vertex;
    const auto &vb = b.vertex;
    const auto &ma = a.material;
    const auto &mb = b.material;
    return a.boundaryEdge == b.boundaryEdge &&
           va.materialSlot == vb.materialSlot &&
           va.screenSpaceW == vb.screenSpaceW &&
           va.fogEyeDepth == vb.fogEyeDepth &&
           sameFloats(va.normal, vb.normal, 3) &&
           sameFloats(va.texcoord, vb.texcoord, 2) &&
           sameFloats(&va.extraTexcoords[0][0], &vb.extraTexcoords[0][0],
                      (COIN_RENDER_MAX_TEXTURE_UNITS - 1) * 2) &&
           sameFloats(va.textureR, vb.textureR, COIN_RENDER_MAX_TEXTURE_UNITS) &&
           sameFloats(va.textureQ, vb.textureQ, COIN_RENDER_MAX_TEXTURE_UNITS) &&
           sameFloats(ma.ambient, mb.ambient, 4) &&
           sameFloats(ma.diffuse, mb.diffuse, 4) &&
           sameFloats(ma.specular, mb.specular, 4) &&
           sameFloats(ma.emission, mb.emission, 4) &&
           ma.shininess == mb.shininess &&
           ma.transparency == mb.transparency;
  };
  for (size_t i = 0; !preserveDegenerateContour && i < output.size() && output.size() > 1;) {
    const size_t next = (i + 1) % output.size();
    if (SbVec3f(output[i].vertex.position) == SbVec3f(output[next].vertex.position) &&
        sameAttributes(output[i], output[next]))
      output.erase(output.begin() + next);
    else
      ++i;
  }
  if (output.empty())
    return true;
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

static bool
coin_render_resolve_styled_triangle_boundaries(
    const std::vector<CoinRenderVertexSnapshot> &triangles,
    const CoinRenderRenderStateSnapshot &state,
    const std::vector<CoinRenderMaterialSnapshot> &materials,
    const CoinRenderLightingSnapshot &lighting, CoinRenderPolygonStyle style,
    const CoinRenderViewportSnapshot &viewport, bool resolveOffset,
    CoinRenderPolygonStyleResult &result, std::string &diagnostic)
{
  if (triangles.empty() || triangles.size() % 3 != 0) {
    diagnostic = "UNSUPPORTED: original polygon triangulation is unavailable";
    return false;
  }
  struct Edge {
    CoinRenderPolygonStyleVertex first, second;
    float slopeBias;
    float maximumDepth;
    Edge(const CoinRenderPolygonStyleVertex &a,
         const CoinRenderPolygonStyleVertex &b, float slope, float maximum)
        : first(a), second(b), slopeBias(slope), maximumDepth(maximum) {}
  };
  std::vector<Edge> rawBoundary;
  auto samePoint = [](const CoinRenderPolygonStyleVertex &a,
                      const CoinRenderPolygonStyleVertex &b) {
    const SbVec3f pa(a.vertex.position), pb(b.vertex.position);
    const float scale = std::max(1.0f, std::max(pa.length(), pb.length()));
    return (pa - pb).length() <= 2e-6f * scale;
  };
  for (size_t i = 0; i < triangles.size(); i += 3) {
    const std::vector<CoinRenderVertexSnapshot> triangle(
        triangles.begin() + i, triangles.begin() + i + 3);
    std::vector<CoinRenderPolygonStyleVertex> clipped;
    if (!coin_render_resolve_polygon_style(triangle, state, materials, lighting,
                                           clipped, diagnostic))
      return false;
    float slopeBias = 0, maximumDepth = 0;
    if (resolveOffset && !clipped.empty()) {
      if (viewport.width <= 0 || viewport.height <= 0) {
        diagnostic = "Invalid viewport for polygon slope offset";
        return false;
      }
      const SbMatrix mvp = state.model * state.view * state.projectionCoin;
      std::vector<SbVec3d> window;
      window.reserve(clipped.size());
      for (const auto &point : clipped) {
        SbVec4f clip;
        mvp.multVecMatrix(SbVec4f(point.vertex.position[0], point.vertex.position[1],
                                  point.vertex.position[2], 1), clip);
        if (!std::isfinite(clip[3]) || clip[3] <= 1e-6f) {
          diagnostic = "Invalid triangle offset projection";
          return false;
        }
        const double depth = state.depthRange[0] +
            (double(clip[2]) / clip[3] * .5 + .5) *
            (state.depthRange[1] - state.depthRange[0]);
        window.emplace_back(double(clip[0]) / clip[3] * viewport.width * .5,
                            double(clip[1]) / clip[3] * viewport.height * .5, depth);
        maximumDepth = std::max(maximumDepth,
                                float(std::max(0.0, std::min(1.0, depth))));
      }
      double determinant = 0, dx = 0, dy = 0;
      for (size_t j = 1; j + 1 < window.size(); ++j) {
        const SbVec3d a = window[j] - window[0];
        const SbVec3d b = window[j + 1] - window[0];
        const double det = a[0] * b[1] - a[1] * b[0];
        if (std::abs(det) > std::abs(determinant)) {
          determinant = det;
          dx = (a[2] * b[1] - a[1] * b[2]) / det;
          dy = (a[0] * b[2] - a[2] * b[0]) / det;
        }
      }
      if (determinant == 0) {
        diagnostic = "UNSUPPORTED: polygon slope offset has zero projected triangle area";
        return false;
      }
      const double bias = state.polygonOffsetFactor *
                          std::max(std::abs(dx), std::abs(dy));
      if (!std::isfinite(bias) || std::abs(bias) > std::numeric_limits<float>::max()) {
        diagnostic = "Invalid polygon slope offset";
        return false;
      }
      slopeBias = float(bias);
    }
    if (resolveOffset && style == CoinRenderPolygonStyle::POINTS) {
      // Polygon-mode points are emitted for every tessellated triangle.
      // Shared original corners therefore have several depth candidates;
      // depth testing selects a different owner for positive/negative bias.
      for (const auto &point : clipped) {
        result.indices.push_back(static_cast<uint32_t>(result.vertices.size()));
        result.vertices.push_back(point);
        result.primitiveSlopeBias.push_back(slopeBias);
        result.primitiveMaximumDepth.push_back(maximumDepth);
      }
      continue;
    }
    for (size_t j = 0; j < clipped.size(); ++j) {
      Edge edge{clipped[j], clipped[(j + 1) % clipped.size()], slopeBias,
                maximumDepth};
      if (samePoint(edge.first, edge.second))
        continue;
      rawBoundary.push_back(edge);
    }
  }
  // Use a conservative global x window only to find candidates.  The original
  // scale-relative predicate remains authoritative, including collisions and
  // nearly coincident geometry.  Choosing the lowest active index preserves
  // std::find_if's first-match behavior.
  float boundaryScale = 1.0f;
  for (const Edge & edge : rawBoundary) {
    boundaryScale = std::max(boundaryScale,
      std::max(SbVec3f(edge.first.vertex.position).length(),
               SbVec3f(edge.second.vertex.position).length()));
  }
  const float boundaryTolerance = 2e-6f * boundaryScale;
  std::vector<Edge> boundary;
  boundary.reserve(rawBoundary.size());
  std::vector<bool> boundaryActive;
  typedef std::multimap<float, size_t> EdgeIndex;
  EdgeIndex reverseByEnd;
  std::vector<EdgeIndex::iterator> reverseEntries;
  for (const Edge & edge : rawBoundary) {
    const float x = edge.first.vertex.position[0];
    size_t reverseSlot = size_t(-1);
    for (auto candidate = reverseByEnd.lower_bound(x - boundaryTolerance);
         candidate != reverseByEnd.end() && candidate->first <= x + boundaryTolerance;
         ++candidate) {
      const size_t slot = candidate->second;
      if (boundaryActive[slot] && samePoint(edge.first, boundary[slot].second) &&
          samePoint(edge.second, boundary[slot].first) && slot < reverseSlot)
        reverseSlot = slot;
    }
    if (reverseSlot != size_t(-1)) {
      boundaryActive[reverseSlot] = false;
      reverseByEnd.erase(reverseEntries[reverseSlot]);
    } else {
      const size_t slot = boundary.size();
      boundary.push_back(edge);
      boundaryActive.push_back(true);
      reverseEntries.push_back(reverseByEnd.emplace(edge.second.vertex.position[0], slot));
    }
  }
  // The tessellator may return triangles in arbitrary order. Trace each
  // surviving directed boundary separately so line stipple advances around
  // a contour and never jumps between disconnected clipped components.
  std::vector<Edge> ordered;
  ordered.reserve(boundary.size());
  EdgeIndex forwardByStart;
  std::vector<EdgeIndex::iterator> forwardEntries(boundary.size());
  size_t activeEdges = 0;
  for (size_t i = 0; i < boundary.size(); ++i) if (boundaryActive[i]) {
    forwardEntries[i] = forwardByStart.emplace(boundary[i].first.vertex.position[0], i);
    ++activeEdges;
  }
  while (activeEdges) {
    size_t slot = boundary.size();
    while (slot && !boundaryActive[slot - 1]) --slot;
    if (!slot) break;
    --slot;
    ordered.push_back(boundary[slot]);
    boundaryActive[slot] = false;
    forwardByStart.erase(forwardEntries[slot]);
    --activeEdges;
    const auto start = ordered.back().first;
    while (!samePoint(ordered.back().second, start)) {
      const float x = ordered.back().second.vertex.position[0];
      size_t nextSlot = size_t(-1);
      for (auto candidate = forwardByStart.lower_bound(x - boundaryTolerance);
           candidate != forwardByStart.end() && candidate->first <= x + boundaryTolerance;
           ++candidate) {
        const size_t candidateSlot = candidate->second;
        if (boundaryActive[candidateSlot] &&
            samePoint(ordered.back().second, boundary[candidateSlot].first) &&
            candidateSlot < nextSlot) nextSlot = candidateSlot;
      }
      if (nextSlot == size_t(-1)) {
        diagnostic = "UNSUPPORTED: clipped polygon boundary is not closed";
        return false;
      }
      ordered.push_back(boundary[nextSlot]);
      boundaryActive[nextSlot] = false;
      forwardByStart.erase(forwardEntries[nextSlot]);
      --activeEdges;
    }
  }
  for (const Edge &edge : ordered) {
    if (style == CoinRenderPolygonStyle::LINES) {
      const uint32_t first = static_cast<uint32_t>(result.vertices.size());
      result.vertices.push_back(edge.first);
      result.vertices.push_back(edge.second);
      result.indices.push_back(first);
      result.indices.push_back(first + 1);
      if (resolveOffset) {
        result.primitiveSlopeBias.push_back(edge.slopeBias);
        result.primitiveMaximumDepth.push_back(edge.maximumDepth);
      }
    } else {
      for (const auto *point : {&edge.first, &edge.second}) {
        const auto duplicate = std::find_if(result.vertices.begin(), result.vertices.end(),
            [&](const CoinRenderPolygonStyleVertex &other) {
              if (!samePoint(*point, other) ||
                  point->vertex.materialSlot != other.vertex.materialSlot ||
                  SbVec3f(point->vertex.normal) != SbVec3f(other.vertex.normal) ||
                  SbVec2f(point->vertex.texcoord) != SbVec2f(other.vertex.texcoord))
                return false;
              return std::memcmp(point->vertex.extraTexcoords,
                                 other.vertex.extraTexcoords,
                                 sizeof(point->vertex.extraTexcoords)) == 0;
            });
        if (duplicate == result.vertices.end()) {
          result.indices.push_back(static_cast<uint32_t>(result.vertices.size()));
          result.vertices.push_back(*point);
          if (resolveOffset) {
            result.primitiveSlopeBias.push_back(edge.slopeBias);
            result.primitiveMaximumDepth.push_back(edge.maximumDepth);
          }
        }
      }
    }
  }
  return true;
}

bool
coin_render_prepare_polygon_style(const std::vector<CoinRenderVertexSnapshot> &ring,
                                  const CoinRenderRenderStateSnapshot &state,
                                  const std::vector<CoinRenderMaterialSnapshot> &materials,
                                  const CoinRenderLightingSnapshot &lighting,
                                  CoinRenderPolygonStyle style,
                                  CoinRenderPolygonStyleResult &result, std::string &diagnostic,
                                  const CoinRenderViewportSnapshot &viewport,
                                  bool preserveDegenerateContour,
                                  const std::vector<CoinRenderVertexSnapshot> *triangles,
                                  const CoinRenderFramePlan *frame)
{
  result = CoinRenderPolygonStyleResult{};
  if (style == CoinRenderPolygonStyle::LINES) {
    if (!coin_render_validate_texture_coordinates(
            state, ring.size(),
            [&](size_t i) -> const CoinRenderVertexSnapshot & { return ring[i]; }, diagnostic, frame))
      return false;
  } else {
    for (const auto &vertex : ring)
      if (!coin_render_validate_texture_coordinates(
              state, 1, [&](size_t) -> const CoinRenderVertexSnapshot & { return vertex; },
              diagnostic, frame))
        return false;
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
      for (size_t index : indices[triangle])
        primitive.push_back(ring[index]);
      std::vector<CoinRenderPolygonStyleVertex> clipped;
      if (!coin_render_resolve_polygon_style(primitive, state, materials, lighting, clipped,
                                             diagnostic, true, edges[triangle]))
        return false;
      for (const auto &vertex : clipped)
        if (vertex.boundaryEdge)
          result.vertices.push_back(vertex);
    }
  } else if (!coin_render_resolve_polygon_style(ring, state, materials, lighting, result.vertices,
                                                diagnostic, preserveDegenerateContour)) {
    if (diagnostic != "UNSUPPORTED: clipped concave polygon has disconnected contours" ||
        !triangles)
      return false;
    diagnostic.clear();
    // The failed connected-ring clip leaves its previous vertices in output.
    // Boundary reconstruction must start empty: POINTS deduplicates against
    // vertices it has already indexed, not against that abandoned ring.
    result.vertices.clear();
    result.indices.clear();
    result.primitiveSlopeBias.clear();
    result.primitiveMaximumDepth.clear();
    if (!coin_render_resolve_styled_triangle_boundaries(*triangles, state, materials,
                                                         lighting, style, viewport, false,
                                                         result, diagnostic))
      return false;
  }
  const bool offsetActive = state.polygonOffsetEnabled &&
      (state.polygonOffsetStyles & primitiveStyle) && state.polygonOffsetFactor != 0;
  bool triangleOffset = false;
  if (offsetActive && triangles && !triangles->empty() && ring.size() >= 3) {
    const SbVec3f origin(ring[0].position);
    SbVec3f normal(0, 0, 0);
    double extent = 0;
    for (size_t i = 1; i < ring.size(); ++i) {
      const SbVec3f delta = SbVec3f(ring[i].position) - origin;
      extent = std::max(extent, double(delta.length()));
      if (i + 1 < ring.size())
        normal += delta.cross(SbVec3f(ring[i + 1].position) - origin);
    }
    if (normal.normalize() != 0) {
      for (const auto &vertex : ring)
        if (std::abs((SbVec3f(vertex.position) - origin).dot(normal)) > 1e-5 * extent) {
          triangleOffset = true;
          break;
        }
    }
    if (triangleOffset) {
      result.vertices.clear();
      result.indices.clear();
      result.primitiveSlopeBias.clear();
      result.primitiveMaximumDepth.clear();
      if (!coin_render_resolve_styled_triangle_boundaries(*triangles, state, materials,
                                                           lighting, style, viewport, true,
                                                           result, diagnostic))
        return false;
    }
  }
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
  if (!triangleOffset && state.polygonOffsetEnabled && (state.polygonOffsetStyles & primitiveStyle) &&
      !result.vertices.empty()) {
    const SbMatrix mvp = state.model * state.view * state.projectionCoin;
    float maximum = 0;
    for (const auto &item : result.vertices) {
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
  if (!triangleOffset && state.polygonOffsetEnabled && (state.polygonOffsetStyles & primitiveStyle) &&
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
    for (const auto &vertex : ring) {
      if (std::abs((SbVec3f(vertex.position) - origin).dot(normal)) > 1e-5 * extent) {
        diagnostic = "UNSUPPORTED: polygon slope offset requires a planar original face";
        return false;
      }
    }
    const SbMatrix mvp = state.model * state.view * state.projectionCoin;
    std::vector<SbVec3d> window;
    for (const auto &vertex : result.vertices) {
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
  if (triangleOffset)
    result.state.polygonOffsetFactor = 0;
  const bool generateRingIndices = result.indices.empty();
  for (uint32_t i = 0; generateRingIndices && i < result.vertices.size(); ++i) {
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
