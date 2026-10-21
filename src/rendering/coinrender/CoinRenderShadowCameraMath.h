#ifndef COIN_RENDER_SHADOW_CAMERA_MATH_H
#define COIN_RENDER_SHADOW_CAMERA_MATH_H

#include "rendering/coinrender/CoinRenderTransformCore.h"
#include <cfloat>
#include <cmath>

// Internal validation for authored map cameras. Determinant magnitude depends
// on scene units; only zero/nonfinite determinants or a failed Coin inverse
// make an otherwise finite matrix unusable here.
namespace coin_render_shadow_camera_math {
inline bool inverse(const SbMatrix &matrix, SbMatrix &result)
{
  if (!CoinRenderTransformCore::finiteMatrix(matrix)) return false;
  const float determinant = matrix.det4();
  if (!std::isfinite(determinant) || determinant == 0.0f) return false;

  const auto m = matrix.getValue();
  if (m[0][3] == 0.0f && m[1][3] == 0.0f && m[2][3] == 0.0f && m[3][3] == 1.0f) {
    // SbMatrix::inverse's affine branch returns its input on relative
    // singularity. Reproduce that documented float-conditioning decision
    // rather than accepting the finite input as a successful inverse.
    const float terms[6] = {
      m[0][0] * m[1][1] * m[2][2],
      m[0][1] * m[1][2] * m[2][0],
      m[0][2] * m[1][0] * m[2][1],
      -m[0][2] * m[1][1] * m[2][0],
      -m[0][1] * m[1][0] * m[2][2],
      -m[0][0] * m[1][2] * m[2][1]
    };
    float positive = 0.0f, negative = 0.0f;
    for (float term : terms) {
      if (term >= 0.0f) positive += term;
      else negative += term;
    }
    const float affineDeterminant = positive + negative;
    const float relative = affineDeterminant / (positive - negative);
    if (!std::isfinite(affineDeterminant) || affineDeterminant == 0.0f ||
        !std::isfinite(relative) || std::abs(relative) < FLT_EPSILON) return false;
  } else {
    // Coin's general inverse also returns its input when no pivot can be
    // selected. A finite, nonzero det4() does not rule that failure out in
    // float elimination. Follow the SDK's pivot decisions on a private copy;
    // still obtain the actual result from SbMatrix below so valid camera
    // matrices retain its established operation order and rounding.
    SbMatrix work = matrix;
    for (int column = 0; column < 4; ++column) {
      float maximum = 0.0f;
      int pivotRow = 0;
      for (int row = column; row < 4; ++row) {
        float sum = 0.0f;
        for (int j = column; j < 4; ++j) sum += std::abs(work[row][j]);
        if (sum > 0.0f) {
          const float candidate = std::abs(work[row][column]) / sum;
          if (candidate > maximum) {
            maximum = candidate;
            pivotRow = row;
          }
        }
      }
      if (maximum == 0.0f) return false;
      if (pivotRow != column) {
        for (int j = 0; j < 4; ++j) {
          const float value = work[column][j];
          work[column][j] = work[pivotRow][j];
          work[pivotRow][j] = value;
        }
      }
      const float inversePivot = 1.0f / work[column][column];
      if (!std::isfinite(inversePivot)) return false;
      for (int j = 0; j < 4; ++j) {
        if (j != column) {
          work[column][j] = -work[column][j] * inversePivot;
          for (int row = 0; row < 4; ++row)
            if (row != column) work[row][j] += work[row][column] * work[column][j];
        }
      }
      for (int row = 0; row < 4; ++row) work[row][column] *= inversePivot;
      work[column][column] = inversePivot;
      if (!CoinRenderTransformCore::finiteMatrix(work)) return false;
    }
    // The native algorithm's final reverse column permutations cannot
    // change pivot success or finiteness, so they are unnecessary here.
  }
  result = matrix.inverse();
  return CoinRenderTransformCore::finiteMatrix(result) && result.det4() != 0.0f;
}
}
#endif
