#ifndef COIN_RENDER_TEXTURE_COORDINATE_CORE_H
#define COIN_RENDER_TEXTURE_COORDINATE_CORE_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderFloatCore.h"
#include <Inventor/SbVec4f.h>
#include <cmath>
#include <limits>

// Pure transport/transform of the captured homogeneous coordinate. Keep Q
// through clipping and perspective interpolation, then divide when sampling.
inline SbVec4f coin_render_texture_coordinate(const CoinRenderVertexSnapshot & vertex,
                                             size_t unit) {
  const float * st = unit ? vertex.extraTexcoords[unit - 1] : vertex.texcoord;
  return SbVec4f(st[0], st[1], vertex.textureR[unit], vertex.textureQ[unit]);
}

inline SbVec4f coin_render_transformed_texture_coordinate(
  const CoinRenderVertexSnapshot & vertex, size_t unit, const SbMatrix & matrix) {
  SbVec4f transformed;
  matrix.multVecMatrix(coin_render_texture_coordinate(vertex, unit), transformed);
  return transformed;
}

template <typename VertexAt>
inline bool coin_render_validate_texture_coordinates(
  const CoinRenderRenderStateSnapshot & state, size_t count,
  VertexAt vertexAt, std::string & diagnostic) {
  const bool direct = state.textureProjection == CoinRenderTextureProjection::DIRECT_ST;
  for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
    if (!coin_render_texture_unit_enabled(state, unit)) continue;
    const auto layer = coin_render_texture_unit(state, unit);
    bool negative = false;
    for (size_t i = 0; i < count; ++i) {
      const SbVec4f coordinate =
        coin_render_transformed_texture_coordinate(vertexAt(i), unit, layer.matrix);
      for (int c = 0; c < (direct ? 2 : 4); ++c) {
        if (!coin_render_is_finite(coordinate[c])) {
          diagnostic = "Non-finite transformed homogeneous texture coordinate";
          return false;
        }
      }
      if (direct) continue;
      const float q = coordinate[3];
      if (q == 0 || (i && (q < 0) != negative)) {
        diagnostic = "Projective texture Q is zero or changes sign within a primitive";
        return false;
      }
      if (std::abs(q) < std::numeric_limits<float>::min() ||
          !coin_render_is_finite(coordinate[0] / q) || !coin_render_is_finite(coordinate[1] / q)) {
        diagnostic = "Projective texture coordinate exceeds finite sampling precision";
        return false;
      }
      negative = q < 0;
    }
  }
  return true;
}

inline SbVec2f coin_render_project_texture_coordinate(const SbVec3f & coordinate,
                                                      CoinRenderTextureProjection policy) {
  if (policy == CoinRenderTextureProjection::DIRECT_ST || coordinate[2] == 1)
    return SbVec2f(coordinate[0], coordinate[1]);
  return SbVec2f(coordinate[0] / coordinate[2], coordinate[1] / coordinate[2]);
}

inline bool coin_render_validate_texture_primitive(
  const CoinRenderFramePlan & frame, const CoinRenderRenderStateSnapshot & state,
  const uint32_t * indices, size_t count, std::string & diagnostic) {
  for (size_t i = 0; i < count; ++i)
    if (indices[i] >= frame.vertices.size()) return true; // Ordinary validation owns bounds.
  return coin_render_validate_texture_coordinates(state, count,
    [&](size_t i) -> const CoinRenderVertexSnapshot & { return frame.vertices[indices[i]]; }, diagnostic);
}

// Strokes must be checked before their unit-fragment expansion can hide a
// singularity between two original endpoints. Filled geometry is checked by
// ordinary FramePlan validation after capture; no extra surface scan here.
inline bool coin_render_validate_stroke_texture_coordinates(
  const CoinRenderFramePlan & frame, std::string & diagnostic) {
  if (frame.textures.empty()) return true;
  for (const auto & draw : frame.draws) {
    if (draw.topology == CoinRenderPrimitiveTopology::TRIANGLE_LIST ||
        draw.renderStateSlot >= frame.renderStates.size() ||
        draw.geometry.firstIndex > frame.indices.size() ||
        draw.geometry.indexCount > frame.indices.size() - draw.geometry.firstIndex) continue;
    const size_t primitiveSize = draw.topology == CoinRenderPrimitiveTopology::LINE_LIST ? 2 : 1;
    for (size_t i = 0; i + primitiveSize <= draw.geometry.indexCount; i += primitiveSize)
      if (!coin_render_validate_texture_primitive(frame, frame.renderStates[draw.renderStateSlot],
          frame.indices.data() + draw.geometry.firstIndex + i, primitiveSize, diagnostic)) return false;
  }
  return true;
}

#endif
