#ifndef COIN_RENDER_TEXTURE_COORDINATE_CORE_H
#define COIN_RENDER_TEXTURE_COORDINATE_CORE_H

#include <Inventor/CoinRenderExport.h>

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderFloatCore.h"
#include <Inventor/SbVec4f.h>
#include <cmath>
#include <limits>

// Pure transport/transform of the captured homogeneous coordinate. Keep Q
// through clipping and perspective interpolation, then divide when sampling.
COIN_RENDER_DLL_API SbVec4f coin_render_texture_coordinate(const CoinRenderVertexSnapshot & vertex,
                                             size_t unit);

COIN_RENDER_DLL_API SbVec4f coin_render_transformed_texture_coordinate(
  const CoinRenderVertexSnapshot & vertex, size_t unit, const SbMatrix & matrix);

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

COIN_RENDER_DLL_API SbVec2f coin_render_project_texture_coordinate(const SbVec3f & coordinate,
                                                      CoinRenderTextureProjection policy);

COIN_RENDER_DLL_API bool coin_render_validate_texture_primitive(
  const CoinRenderFramePlan & frame, const CoinRenderRenderStateSnapshot & state,
  const uint32_t * indices, size_t count, std::string & diagnostic);

// Strokes must be checked before their unit-fragment expansion can hide a
// singularity between two original endpoints. Filled geometry is checked by
// ordinary FramePlan validation after capture; no extra surface scan here.
COIN_RENDER_DLL_API bool coin_render_validate_stroke_texture_coordinates(
  const CoinRenderFramePlan & frame, std::string & diagnostic);

#endif
