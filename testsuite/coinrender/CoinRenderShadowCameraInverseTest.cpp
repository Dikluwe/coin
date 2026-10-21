#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinrender/CoinRenderShadowCameraMath.h"
#include "rendering/coinrender/CoinRenderShadowCore.h"
#include <Inventor/SoDB.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

namespace {
bool check(bool value, const std::string &message) {
  if (!value)
    std::cerr << "shadow_camera_inverse: " << message << '\n';
  return value;
}

// A mathematical witness, independent of the production admission predicate.
// Products are accumulated in double. The bound accounts only for float input
// rounding in a four-term dot product; it is neither a pixel nor a scene bound.
bool inverseWitness(const SbMatrix &matrix, const SbMatrix &candidate,
                    double &maximumResidual) {
  maximumResidual = 0;
  bool valid = true;
  for (int order = 0; order < 2; ++order) {
    const auto &left = order ? candidate : matrix;
    const auto &right = order ? matrix : candidate;
    for (int row = 0; row < 4; ++row)
      for (int col = 0; col < 4; ++col) {
        double product = 0, magnitude = 0;
        for (int k = 0; k < 4; ++k) {
          const double term = double(left[row][k]) * right[k][col];
          product += term;
          magnitude += std::abs(term);
        }
        const double identity = row == col ? 1 : 0;
        const double residual = std::abs(product - identity);
        maximumResidual = std::max(maximumResidual, residual);
        const double bound =
            32 * std::numeric_limits<float>::epsilon() * (magnitude + identity);
        valid &= std::isfinite(product) && std::isfinite(magnitude) &&
                 residual <= bound;
      }
  }
  return valid;
}

SbMatrix diagonal(float x, float y, float z, float w = 1) {
  return SbMatrix(x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, w);
}

SbMatrix failedPivotCamera() {
  // Exact IEEE floats supplied by the finite affine-view/frustum witness.
  // Integer bits keep this source valid under the repository's C++11 mode.
  const uint32_t words[16] = {
      0x3fd34dfcu, 0x3f37addfu, 0x00000000u, 0x00000000u,
      0xbf37addfu, 0x3fd34dfcu, 0x00000000u, 0x00000000u,
      0x3cfdf3b6u, 0x00000000u, 0xbf9f1b08u, 0xbf800000u,
      0xcfc9f9e4u, 0xcf62cfd6u, 0xcf61bfffu, 0xcf359d7fu};
  float values[4][4];
  std::memcpy(values, words, sizeof(values));
  return SbMatrix(values);
}

CoinRenderFramePlan rasterFrame(const SbMatrix &combined,
                                const SbVec3f &anchor = SbVec3f(0, 0, 0)) {
  CoinRenderFramePlan frame;
  frame.materials.resize(1);
  frame.shadowLights.resize(1);
  frame.shadowLights[0].mapSceneCaptured = true;
  frame.shadowLights[0].groupSlot = 1;
  CoinRenderViewportSnapshot viewport;
  viewport.width = viewport.height = 64;
  frame.viewports.push_back(viewport);
  CoinRenderRenderStateSnapshot state;
  state.shadowGroupSlot = 1;
  state.shadowMapSize = 64;
  state.shadowMapCameraOverride = true;
  state.projectionCoin =
      combined; // The Core boundary consumes view*projection.
  frame.renderStates.push_back(state);
  CoinRenderRasterOccurrence marker;
  marker.kind = CoinRenderRasterOccurrence::MARKER;
  marker.shadowLightSlot = 1;
  marker.anchorWorld = anchor;
  marker.originalWidth = marker.originalHeight = 9;
  for (int row = 0; row < 9; ++row)
    marker.runs.emplace_back(0, float(row), 9);
  frame.rasterOccurrences.push_back(marker);
  return frame;
}

CoinRenderShadowPlan shadowPlan() {
  CoinRenderShadowPlan plan;
  CoinRenderShadowPass pass;
  pass.groupSlot = 1;
  pass.mapSize = 64;
  plan.passes.push_back(pass);
  return plan;
}

bool coreAccepts(const SbMatrix &matrix, const SbVec3f &anchor,
                 const std::string &label) {
  auto frame = rasterFrame(matrix, anchor);
  std::string error;
  const bool accepted =
      coin_render_materialize_raster_casters(frame, shadowPlan(), error);
  bool ok = check(
      accepted && error.empty() && frame.draws.size() == 1 &&
          frame.vertices.size() == 36 && frame.indices.size() == 54,
      label + " real Core materializes a nonempty nine-row caster: " + error);
  if (!accepted)
    return false;
  SbVec3f ndcAnchor;
  matrix.multVecMatrix(anchor, ndcAnchor);
  const double left = ndcAnchor[0] - 8. / 64, right = ndcAnchor[0] + 10. / 64;
  const double bottom = ndcAnchor[1] - 8. / 64, top = ndcAnchor[1] + 10. / 64;
  for (const auto &vertex : frame.vertices) {
    SbVec3f recovered;
    matrix.multVecMatrix(SbVec3f(vertex.position), recovered);
    ok &=
        check(std::isfinite(recovered[0]) && std::isfinite(recovered[1]) &&
                  std::isfinite(recovered[2]) && recovered[0] >= left - 1e-6 &&
                  recovered[0] <= right + 1e-6 &&
                  recovered[1] >= bottom - 1e-6 && recovered[1] <= top + 1e-6 &&
                  std::abs(recovered[2] - ndcAnchor[2]) < 1e-6,
              label + " emitted world vertices preserve the authored marker "
                      "footprint/depth");
  }
  return ok;
}

bool coreRejects(const SbMatrix &matrix, const std::string &label) {
  auto frame = rasterFrame(matrix);
  const auto before = frame;
  std::string error;
  const bool accepted =
      coin_render_materialize_raster_casters(frame, shadowPlan(), error);
  // NaN is not equal to itself: compare this invalid input's exact bits,
  // then use the existing payload equality on the remaining finite fields.
  auto normalizedAfter = frame, normalizedBefore = before;
  const bool matrixUnchanged =
      frame.renderStates.size() == 1 &&
      std::memcmp(frame.renderStates[0].projectionCoin.getValue(),
                  before.renderStates[0].projectionCoin.getValue(),
                  16 * sizeof(float)) == 0;
  if (frame.renderStates.size() == 1)
    normalizedAfter.renderStates[0].projectionCoin = SbMatrix::identity();
  normalizedBefore.renderStates[0].projectionCoin = SbMatrix::identity();
  bool ok =
      check(!accepted && !error.empty() && matrixUnchanged &&
                normalizedAfter.hasSamePayload(normalizedBefore) &&
                frame.vertices.empty() && frame.indices.empty() &&
                frame.draws.empty(),
            label + " real Core refuses before publishing caster payload");
  // Recover this same frame rather than replacing it or bypassing the Core.
  frame.renderStates[0].projectionCoin = SbMatrix::identity();
  error.clear();
  ok &= check(
      coin_render_materialize_raster_casters(frame, shadowPlan(), error) &&
          error.empty() && frame.draws.size() == 1 &&
          frame.vertices.size() == 36,
      label + " corrected camera recovers on the same frame");
  return ok;
}

bool positive(const SbMatrix &matrix, const SbMatrix &analyticInverse,
              const SbVec3f &anchor, const std::string &label) {
  double residual = 0;
  bool ok = check(inverseWitness(matrix, analyticInverse, residual),
                  label + " independently authored inverse is valid");
  SbMatrix actual;
  ok &= check(coin_render_shadow_camera_math::inverse(matrix, actual) &&
                  inverseWitness(matrix, actual, residual),
              label + " admits a genuine finite inverse");
  return coreAccepts(matrix, anchor, label) && ok;
}
} // namespace

int main() {
  SoDB::init();
  bool ok = true;
  const auto failed = failedPivotCamera();
  const auto coinResult = failed.inverse();
  double residual = 0;
  ok &= check(std::isfinite(failed.det4()) && failed.det4() != 0 &&
                  !inverseWitness(failed, coinResult, residual) &&
                  residual > 1e10,
              "finite nonzero determinant does not prove Coin pivot success");
  std::cout << "failed_pivot determinant=" << failed.det4()
            << " two_sided_residual=" << residual << '\n';
  SbMatrix unused;
  ok &= check(!coin_render_shadow_camera_math::inverse(failed, unused),
              "failed non-affine Coin inverse must be refused");
  ok &= coreRejects(failed, "finite failed-pivot camera");

  ok &= positive(SbMatrix::identity(), SbMatrix::identity(), SbVec3f(0, 0, 0),
                 "identity self-inverse");
  const auto halfTurn = diagonal(-1, -1, 1);
  ok &= positive(halfTurn, halfTurn, SbVec3f(0, 0, 0), "affine self-inverse");
  const auto projectiveInvolution = diagonal(1, 1, 1, -1);
  ok &= positive(
      projectiveInvolution, projectiveInvolution, SbVec3f(0, 0, 0),
      "non-affine self-inverse must not be rejected for returning itself");
  for (float scale : {1e-5f, 1e5f, std::ldexp(1.f, -16), std::ldexp(1.f, 16)})
    ok &= positive(diagonal(scale, scale, scale),
                   diagonal(1 / scale, 1 / scale, 1 / scale), SbVec3f(0, 0, 0),
                   "finite uniform scale=" + std::to_string(scale));
  const float widthScale = std::ldexp(1.f, -24);
  const SbMatrix wide(widthScale, 0, 0, 0, 0, widthScale, 0, 0, 0, 0, -11.f / 9,
                      -1, 0, 0, -20.f / 9, 0);
  const SbMatrix wideInverse(1 / widthScale, 0, 0, 0, 0, 1 / widthScale, 0, 0,
                             0, 0, 0, -9.f / 20, 0, 0, -1, 11.f / 20);
  ok &= check(wide.det4() != 0 && std::abs(wide.det4()) < 1e-12,
              "wide frustum really has a small nonzero determinant");
  ok &= positive(wide, wideInverse, SbVec3f(0, 0, -5),
                 "wide finite perspective near1/far10");
  for (float bad : {0.f, std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::denorm_min()}) {
    const auto invalid = diagonal(bad, 1, 1);
    ok &= check(!coin_render_shadow_camera_math::inverse(invalid, unused),
                "singular/nonfinite/overflow inverse remains invalid");
    ok &= coreRejects(invalid, "invalid inverse domain");
  }
  return ok ? 0 : 1;
}
