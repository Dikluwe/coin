#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderTextureCoordinateCore.h"

SbVec4f
coin_render_texture_coordinate(const CoinRenderVertexSnapshot &vertex, size_t unit)
{
  const float *st = unit ? vertex.extraTexcoords[unit - 1] : vertex.texcoord;
  return SbVec4f(st[0], st[1], vertex.textureR[unit], vertex.textureQ[unit]);
}

SbVec4f
coin_render_transformed_texture_coordinate(const CoinRenderVertexSnapshot &vertex, size_t unit,
                                           const SbMatrix &matrix)
{
  SbVec4f transformed;
  matrix.multVecMatrix(coin_render_texture_coordinate(vertex, unit), transformed);
  return transformed;
}

SbVec2f
coin_render_project_texture_coordinate(const SbVec3f &coordinate,
                                       CoinRenderTextureProjection policy)
{
  if (policy == CoinRenderTextureProjection::DIRECT_ST || coordinate[2] == 1)
    return SbVec2f(coordinate[0], coordinate[1]);
  return SbVec2f(coordinate[0] / coordinate[2], coordinate[1] / coordinate[2]);
}

bool
coin_render_validate_texture_primitive(const CoinRenderFramePlan &frame,
                                       const CoinRenderRenderStateSnapshot &state,
                                       const uint32_t *indices, size_t count,
                                       std::string &diagnostic)
{
  for (size_t i = 0; i < count; ++i)
    if (indices[i] >= frame.vertices.size())
      return true; // Ordinary validation owns bounds.
  return coin_render_validate_texture_coordinates(
      state, count,
      [&](size_t i) -> const CoinRenderVertexSnapshot & { return frame.vertices[indices[i]]; },
      diagnostic);
}

bool
coin_render_validate_stroke_texture_coordinates(const CoinRenderFramePlan &frame,
                                                std::string &diagnostic)
{
  if (frame.textures.empty())
    return true;
  for (const auto &draw : frame.draws) {
    if (draw.topology == CoinRenderPrimitiveTopology::TRIANGLE_LIST ||
        draw.renderStateSlot >= frame.renderStates.size() ||
        draw.geometry.firstIndex > frame.indices.size() ||
        draw.geometry.indexCount > frame.indices.size() - draw.geometry.firstIndex)
      continue;
    const size_t primitiveSize = draw.topology == CoinRenderPrimitiveTopology::LINE_LIST ? 2 : 1;
    for (size_t i = 0; i + primitiveSize <= draw.geometry.indexCount; i += primitiveSize)
      if (!coin_render_validate_texture_primitive(
              frame, frame.renderStates[draw.renderStateSlot],
              frame.indices.data() + draw.geometry.firstIndex + i, primitiveSize, diagnostic))
        return false;
  }
  return true;
}
