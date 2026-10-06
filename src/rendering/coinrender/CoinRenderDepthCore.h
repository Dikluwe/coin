#ifndef COIN_RENDER_DEPTH_CORE_H
#define COIN_RENDER_DEPTH_CORE_H
#include "rendering/coinrender/CoinRenderPlanAssemblyCore.h"
#include "rendering/coinrender/CoinRenderPolygonStyleCore.h"
#include <limits>
#include <string>
#include <vector>

// Resolve original filled-triangle slope/maximum before executor depth formats
// supply their own quantum. Reversed ranges use the same window-space metric.
// The caller must validate the captured plan before resolving its draw ranges.
inline bool coin_render_resolve_triangle_depth(CoinRenderFramePlan &plan, std::string &diagnostic) {
  const auto needsResolution = [&](const CoinRenderDrawPacket &draw) {
    const auto &state = plan.renderStates[draw.renderStateSlot];
    return draw.topology == CoinRenderPrimitiveTopology::TRIANGLE_LIST &&
           state.polygonOffsetEnabled && state.polygonOffsetPrimitiveStyle == 1 &&
           (state.polygonOffsetStyles & 1) && state.polygonOffsetMaxDepth < 0 &&
           (state.polygonOffsetFactor != 0 || state.polygonOffsetUnits != 0);
  };
  if (std::none_of(plan.draws.begin(), plan.draws.end(), needsResolution))
    return true; // Preserve storage and skip work for the ordinary capture path.
  std::vector<CoinRenderDrawPacket> draws;
  draws.reserve(plan.draws.capacity());
  CoinRenderPlanAssemblyCore::StateIndex states;
  for (const auto &draw : plan.draws) {
    const auto source = plan.renderStates[draw.renderStateSlot];
    if (draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST ||
        !source.polygonOffsetEnabled || source.polygonOffsetPrimitiveStyle != 1 ||
        !(source.polygonOffsetStyles & 1) || source.polygonOffsetMaxDepth >= 0 ||
        (source.polygonOffsetFactor == 0 && source.polygonOffsetUnits == 0)) {
      draws.push_back(draw);
      continue;
    }
    const auto &viewport = plan.viewports[source.viewportSlot];
    bool pendingClear = draw.clearDepthBefore;
    for (uint32_t offset = 0; offset < draw.geometry.indexCount; offset += 3) {
      std::vector<CoinRenderVertexSnapshot> ring;
      for (uint32_t i = 0; i < 3; ++i)
        ring.push_back(plan.vertices[plan.indices[draw.geometry.firstIndex + offset + i]]);
      auto clipState = source;
      clipState.lightModel = CoinRenderLightModel::BASE_COLOR;
      clipState.cullMode = CoinRenderCullMode::NONE;
      std::vector<CoinRenderPolygonStyleVertex> clipped;
      if (!coin_render_resolve_polygon_style(ring, clipState, plan.materials,
                                             plan.lightingStates[source.lightingSlot], clipped,
                                             diagnostic))
        return false;
      if (clipped.size() < 3)
        continue;
      auto resolved = source;
      const SbMatrix mvp = source.model * source.view * source.projectionCoin;
      std::vector<SbVec3d> window;
      double maximum = 0;
      for (const auto &point : clipped) {
        SbVec4f clip;
        mvp.multVecMatrix(SbVec4f(point.vertex.position[0], point.vertex.position[1],
                                  point.vertex.position[2], 1),
                          clip);
        if (!std::isfinite(clip[3]) || clip[3] <= 1e-6f) {
          diagnostic = "Invalid triangle offset projection";
          return false;
        }
        const double depth =
            source.depthRange[0] +
            (double(clip[2]) / clip[3] * .5 + .5) * (source.depthRange[1] - source.depthRange[0]);
        window.emplace_back(double(clip[0]) / clip[3] * viewport.width * .5,
                            double(clip[1]) / clip[3] * viewport.height * .5, depth);
        maximum = std::max(maximum, std::max(0.0, std::min(1.0, depth)));
      }
      double determinant = 0, dx = 0, dy = 0;
      for (size_t i = 1; i + 1 < window.size(); ++i) {
        const auto a = window[i] - window[0], b = window[i + 1] - window[0];
        const double det = a[0] * b[1] - a[1] * b[0];
        if (std::abs(det) > std::abs(determinant)) {
          determinant = det;
          dx = (a[2] * b[1] - a[1] * b[2]) / det;
          dy = (a[0] * b[2] - a[2] * b[0]) / det;
        }
      }
      if (determinant == 0)
        continue;
      const double bias = source.polygonOffsetSlopeBias +
                          source.polygonOffsetFactor * std::max(std::abs(dx), std::abs(dy));
      if (!std::isfinite(bias) || std::abs(bias) > std::numeric_limits<float>::max() ||
          !std::isfinite(source.polygonOffsetUnits)) {
        diagnostic = "Invalid triangle offset bias";
        return false;
      }
      resolved.polygonOffsetSlopeBias = float(bias);
      resolved.polygonOffsetFactor = 0;
      resolved.polygonOffsetMaxDepth = float(maximum);
      auto item = draw;
      item.geometry.firstIndex += offset;
      item.geometry.indexCount = 3;
      // The captured stable hint identifies the whole original range.
      item.stableNodeId = 0;
      item.sourceRevision = 0;
      item.drawOrdinal = 0;
      item.renderStateSlot = CoinRenderPlanAssemblyCore::state(plan, states, resolved);
      item.clearDepthBefore = pendingClear;
      pendingClear = false;
      draws.push_back(item);
    }
    if (pendingClear) {
      auto empty = draw;
      empty.geometry.indexCount = 0;
      draws.push_back(empty);
    }
  }
  plan.draws.swap(draws);
  return true;
}
#endif
