#ifndef COIN_WGPU_SHADOW_FRAME_H
#define COIN_WGPU_SHADOW_FRAME_H

#include "rendering/coinrender/CoinRenderShadowCore.h"

#include <cstring>
#include <string>
#include <vector>

// Infra transport for the Core-selected casters. These draws refer to the
// original frame vertex/index arrays, before composition reorders main draws.
struct CoinWgpuShadowDraw {
  uint32_t firstIndex = 0;
  uint32_t indexCount = 0;
  uint32_t renderStateSlot = 0;
  float modelView[16] = {};
  float modelViewProjection[16] = {};
};

struct CoinWgpuShadowFrame {
  uint32_t mapSize = 0;
  float nearDistance = 0.0f;
  float farDistance = 0.0f;
  std::vector<CoinWgpuShadowDraw> casters;

  bool prepare(const CoinRenderFramePlan & frame, std::string & diagnostic)
  {
    CoinWgpuShadowFrame candidate;
    if (frame.shadowGroups.empty()) {
      *this = std::move(candidate);
      diagnostic.clear();
      return true;
    }
    CoinRenderShadowPlan plan;
    if (!coin_render_plan_shadows(frame, plan, diagnostic) ||
        !coin_render_shadow_single_spot_opaque_profile(frame, plan, diagnostic))
      return false;
    const auto & pass = plan.passes[0];
    candidate.mapSize = pass.mapSize;
    candidate.nearDistance = pass.nearDistance;
    candidate.farDistance = pass.farDistance;
    const SbMatrix clipConversion(
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.5f, 0.0f,
      0.0f, 0.0f, 0.5f, 1.0f);
    const SbMatrix projectionWgpu = pass.projectionCoin * clipConversion;
    for (const uint32_t drawSlot : pass.casterDraws) {
      if (drawSlot >= frame.draws.size()) {
        diagnostic = "Shadow caster references an invalid draw";
        return false;
      }
      const auto & draw = frame.draws[drawSlot];
      const uint64_t indexEnd =
        uint64_t(draw.geometry.firstIndex) + draw.geometry.indexCount;
      const uint64_t vertexEnd =
        uint64_t(draw.geometry.firstVertex) + draw.geometry.vertexCount;
      if (draw.renderStateSlot >= frame.renderStates.size() ||
          draw.geometry.indexCount == 0 ||
          draw.geometry.indexCount % 3 != 0 ||
          indexEnd > frame.indices.size() ||
          vertexEnd > frame.vertices.size()) {
        diagnostic = "Shadow caster has an invalid triangle range";
        return false;
      }
      for (uint64_t i = draw.geometry.firstIndex; i < indexEnd; ++i) {
        const uint32_t index = frame.indices[static_cast<size_t>(i)];
        if (index < draw.geometry.firstVertex || index >= vertexEnd) {
          diagnostic = "Shadow caster index is outside its vertex range";
          return false;
        }
      }
      CoinWgpuShadowDraw packed;
      packed.firstIndex = draw.geometry.firstIndex;
      packed.indexCount = draw.geometry.indexCount;
      packed.renderStateSlot = draw.renderStateSlot;
      const SbMatrix modelView = frame.renderStates[draw.renderStateSlot].model *
                                 pass.view;
      const SbMatrix mvp = modelView * projectionWgpu;
      std::memcpy(packed.modelView, modelView.getValue(), sizeof(packed.modelView));
      std::memcpy(packed.modelViewProjection, mvp.getValue(),
                  sizeof(packed.modelViewProjection));
      for (const float value : packed.modelView)
        if (!std::isfinite(value)) {
          diagnostic = "Shadow caster has a non-finite model-view matrix";
          return false;
        }
      for (const float value : packed.modelViewProjection)
        if (!std::isfinite(value)) {
          diagnostic = "Shadow caster has a non-finite projection";
          return false;
        }
      candidate.casters.push_back(packed);
    }
    *this = std::move(candidate);
    diagnostic.clear();
    return true;
  }
};

#endif // COIN_WGPU_SHADOW_FRAME_H
