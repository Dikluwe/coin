#ifndef COIN_RENDER_TRANSFORM_CORE_H
#define COIN_RENDER_TRANSFORM_CORE_H
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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
  // A camera view may rotate/translate, but scale, shear and reflections need
  // the ordinary capture/packing path. This is a qualification tolerance, not
  // an approximate comparison of scene changes.
  static bool rigidViewMatrix(const SbMatrix & matrix) {
    if (!finiteMatrix(matrix) || matrix[0][3] != 0.0f ||
        matrix[1][3] != 0.0f || matrix[2][3] != 0.0f ||
        matrix[3][3] != 1.0f) return false;
    const double tolerance = 2.0e-5;
    for (int row = 0; row < 3; ++row) {
      for (int other = row; other < 3; ++other) {
        double dot = 0.0;
        for (int column = 0; column < 3; ++column)
          dot += double(matrix[row][column]) * matrix[other][column];
        if (std::abs(dot - (row == other ? 1.0 : 0.0)) > tolerance)
          return false;
      }
    }
    const float determinant = matrix.det4();
    return std::isfinite(determinant) &&
      std::abs(double(determinant) - 1.0) <= 4.0e-5;
  }
  // Reusing float eye-space values is unsafe at very large coordinates: an
  // anchor at 1e8 can erase a unit-sized feature before a delta brings it near
  // the eye. Keep this optional path within a conservative precision domain.
  static bool cameraReuseView(const SbMatrix & matrix) {
    if (!rigidViewMatrix(matrix)) return false;
    for (int column = 0; column < 3; ++column)
      if (std::abs(matrix[3][column]) > 32768.0f) return false;
    return true;
  }
  // Geometry baked in anchorView stays immutable. Derive every camera update
  // from that anchor, rather than accumulating deltas between successive views.
  // Coin uses row vectors: p * anchorView * delta == p * currentView.
  static bool cameraDelta(const SbMatrix & anchorView, const SbMatrix & currentView,
                          SbMatrix & delta, SbMatrix & normalDelta) {
    if (!cameraReuseView(anchorView) || !cameraReuseView(currentView)) return false;
    SbMatrix candidate;
    if (std::memcmp(anchorView.getValue(), currentView.getValue(),
                    sizeof(float) * 12) == 0) {
      // With unchanged orientation the exact delta is a translation. Avoid
      // introducing a residual rotation through inverse/multiply roundoff.
      candidate = SbMatrix::identity();
      for (int column = 0; column < 3; ++column)
        candidate[3][column] = currentView[3][column] - anchorView[3][column];
    } else {
      candidate = anchorView.inverse() * currentView;
    }
    if (!finiteMatrix(candidate)) return false;
    const SbMatrix candidateNormal = normalMatrix(candidate);
    if (!finiteMatrix(candidateNormal)) return false;
    delta = candidate;
    normalDelta = candidateNormal;
    return true;
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
  // APIs requiring an in-target viewport use the intersection as their native
  // viewport and compensate projection in Core. Window coordinates and depth
  // stay those of the original Coin viewport; an empty intersection is a no-op.
  static SbMatrix clippedViewportTransform(const CoinRenderViewportSnapshot & viewport,
                                            int width, int height, int32_t clipped[4]) {
    const int32_t original[4] = {viewport.x, viewport.y, viewport.width, viewport.height};
    if (!clipViewport(original, width, height, clipped)) {
      clipped[0] = clipped[1] = clipped[2] = clipped[3] = 0;
      return SbMatrix::identity();
    }
    auto relative = viewport;
    relative.x -= clipped[0]; relative.y -= clipped[1];
    return viewportTransform(relative, clipped[2], clipped[3]);
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
