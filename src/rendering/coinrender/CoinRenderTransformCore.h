#ifndef COIN_RENDER_TRANSFORM_CORE_H
#define COIN_RENDER_TRANSFORM_CORE_H
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <algorithm>
#include <cmath>

// Coin-native transforms over captured values. GPU packing and API conventions
// select parameters; this Core does not know BGFX, wgpu, actions or devices.
class CoinRenderTransformCore {
public:
  static bool finiteMatrix(const SbMatrix & matrix) {
    const auto values = matrix.getValue();
    for (int row = 0; row < 4; ++row)
      for (int column = 0; column < 4; ++column)
        if (!std::isfinite(values[row][column])) return false;
    return true;
  }
  // Coin projection is [-1,1]. A zero-to-one depth API needs conversion once.
  static SbMatrix projection(const SbMatrix & coin, bool homogeneousDepth) {
    if (homogeneousDepth) return coin;
    const SbMatrix conversion(1,0,0,0, 0,1,0,0, 0,0,0.5f,0, 0,0,0.5f,1);
    return coin * conversion;
  }
  static SbMatrix normalMatrix(const SbMatrix & modelView) {
    const float determinant = modelView.det4();
    return std::abs(determinant) > 1.0e-12f
      ? modelView.inverse().transpose() : SbMatrix::identity();
  }
  // Already validated positive target dimensions. Preserve viewport projection;
  // clipping changes only scissor bounds, never this transform.
  static SbMatrix viewportTransform(const CoinRenderViewportSnapshot & viewport,
                                     int width, int height) {
    const float sx = static_cast<float>(viewport.width) / static_cast<float>(width);
    const float sy = static_cast<float>(viewport.height) / static_cast<float>(height);
    const float tx = (2.0f * viewport.x + viewport.width) / static_cast<float>(width) - 1.0f;
    const float ty = (2.0f * viewport.y + viewport.height) / static_cast<float>(height) - 1.0f;
    return SbMatrix(sx,0,0,0, 0,sy,0,0, 0,0,1,0, tx,ty,0,1);
  }
  // Coin bottom-left coordinates. Empty intersection leaves the output intact.
  static bool clipViewport(const int32_t viewport[4], int width, int height,
                            int32_t clipped[4]) {
    const int64_t left = std::max<int64_t>(0, viewport[0]);
    const int64_t bottom = std::max<int64_t>(0, viewport[1]);
    const int64_t right = std::min<int64_t>(width, int64_t(viewport[0]) + viewport[2]);
    const int64_t top = std::min<int64_t>(height, int64_t(viewport[1]) + viewport[3]);
    if (right <= left || top <= bottom) return false;
    clipped[0] = static_cast<int32_t>(left); clipped[1] = static_cast<int32_t>(bottom);
    clipped[2] = static_cast<int32_t>(right - left); clipped[3] = static_cast<int32_t>(top - bottom);
    return true;
  }
};
#endif
