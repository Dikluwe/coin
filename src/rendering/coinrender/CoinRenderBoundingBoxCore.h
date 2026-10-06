#ifndef COIN_RENDER_BOUNDING_BOX_CORE_H
#define COIN_RENDER_BOUNDING_BOX_CORE_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderFloatCore.h"
#include <Inventor/SbBox3f.h>
#include <array>

// Six independent CCW quads, matching Coin's bounding-box cube. Keep each
// face's normal and UV, and its repeated edges/corners for polygon styles.
inline bool coin_render_bounding_box_valid(const SbBox3f & box, std::string & diagnostic) {
  for (int c = 0; c < 3; ++c) {
    const float lo = box.getMin()[c], hi = box.getMax()[c];
    if (!coin_render_is_finite(lo) || !coin_render_is_finite(hi) ||
        (!box.isEmpty() && (!coin_render_is_finite((lo + hi) * .5f) ||
                           !coin_render_is_finite(hi - lo)))) {
      diagnostic = "Non-finite shape bounding box, center or extent";
      return false;
    }
  }
  return true;
}

inline bool coin_render_bounding_box_vertices(const SbBox3f & box, uint32_t material,
    std::array<CoinRenderVertexSnapshot, 24> & vertices, std::string & diagnostic) {
  if (!coin_render_bounding_box_valid(box, diagnostic)) return false;
  static const int corners[6][4] = {
    {0, 1, 3, 2}, {5, 4, 6, 7}, {1, 5, 7, 3},
    {4, 0, 2, 6}, {4, 5, 1, 0}, {2, 3, 7, 6}
  };
  static const float normals[6][3] = {
    {0, 0, 1}, {0, 0, -1}, {-1, 0, 0},
    {1, 0, 0}, {0, 1, 0}, {0, -1, 0}
  };
  static const float uv[4][2] = {{1, 1}, {0, 1}, {0, 0}, {1, 0}};
  for (size_t face = 0; face < 6; ++face)
    for (size_t corner = 0; corner < 4; ++corner) {
      auto & vertex = vertices[face * 4 + corner];
      vertex = CoinRenderVertexSnapshot{};
      const int index = corners[face][corner];
      for (int c = 0; c < 3; ++c) {
        vertex.position[c] = (index & (1 << c)) ? box.getMin()[c] : box.getMax()[c];
        vertex.normal[c] = normals[face][c];
      }
      vertex.texcoord[0] = uv[corner][0]; vertex.texcoord[1] = uv[corner][1];
      vertex.materialSlot = material;
    }
  return true;
}

#endif
