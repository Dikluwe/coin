#ifndef COIN_RENDER_SHADOW_CORE_H
#define COIN_RENDER_SHADOW_CORE_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <cmath>
#include <utility>
#include <string>
#include <vector>

struct CoinRenderShadowPass {
  uint32_t groupSlot = 0;
  uint32_t lightSlot = 0;
  uint32_t mapSize = 0;
  std::vector<uint32_t> casterDraws;
  std::vector<uint32_t> receiverDraws;
};

struct CoinRenderShadowPlan {
  std::vector<CoinRenderShadowPass> passes;
  uint64_t attachmentBytes = 0;
};

// Pure Core planning over immutable captures. Resources and GPU passes belong
// to CoinBgfx/CoinWgpu. The current executors deliberately reject this plan.
inline bool
coin_render_plan_shadows(const CoinRenderFramePlan & frame,
                         CoinRenderShadowPlan & output, std::string & diagnostic)
{
  CoinRenderShadowPlan candidate;
  for (size_t g = 0; g < frame.shadowGroups.size(); ++g) {
    const auto & group = frame.shadowGroups[g];
    if (!group.sourceRevision || group.nested ||
        !std::isfinite(group.precision) || group.precision <= 0.0f ||
        group.precision > 1.0f || !std::isfinite(group.quality) ||
        !std::isfinite(group.intensity) || !std::isfinite(group.epsilon) ||
        !std::isfinite(group.threshold) || !std::isfinite(group.smoothBorder) ||
        !std::isfinite(group.visibilityNearRadius) ||
        !std::isfinite(group.visibilityRadius)) {
      diagnostic = "Unsupported or invalid SoShadowGroup parameters/nesting";
      return false;
    }
    uint32_t mapSize = 1;
    const uint32_t requested = static_cast<uint32_t>(std::ceil(group.precision * 2048.0f));
    while (mapSize < requested) mapSize <<= 1;
    for (size_t l = 0; l < frame.shadowLights.size(); ++l) {
      const auto & light = frame.shadowLights[l];
      if (light.groupSlot != g + 1 || !light.enabled) continue;
      if (light.hasCustomScene ||
          (light.type != CoinRenderLightType::DIRECTIONAL &&
           light.type != CoinRenderLightType::SPOT)) {
        diagnostic = "Shadow light requires a supported spot/directional scene";
        return false;
      }
      CoinRenderShadowPass pass;
      pass.groupSlot = static_cast<uint32_t>(g + 1);
      pass.lightSlot = static_cast<uint32_t>(l);
      pass.mapSize = mapSize;
      for (size_t d = 0; d < frame.draws.size(); ++d) {
        const auto & draw = frame.draws[d];
        if (draw.renderStateSlot >= frame.renderStates.size()) {
          diagnostic = "Shadow draw references an invalid render state";
          return false;
        }
        const auto & state = frame.renderStates[draw.renderStateSlot];
        if (state.shadowGroupSlot != pass.groupSlot) continue;
        if ((state.shadowStyle & 1u) != 0)
          pass.casterDraws.push_back(static_cast<uint32_t>(d));
        if ((state.shadowStyle & 2u) != 0)
          pass.receiverDraws.push_back(static_cast<uint32_t>(d));
      }
      const uint64_t bytes = uint64_t(mapSize) * mapSize * 16u; // RGBA32F moments.
      const uint64_t budget = uint64_t(128) * 1024 * 1024;
      if (bytes > budget || candidate.attachmentBytes > budget - bytes) {
        diagnostic = "Shadow maps exceed 128 MiB per apply";
        return false;
      }
      candidate.attachmentBytes += bytes;
      candidate.passes.push_back(std::move(pass));
    }
  }
  for (const auto & light : frame.shadowLights)
    if (light.groupSlot == 0 || light.groupSlot > frame.shadowGroups.size()) {
      diagnostic = "Shadow light references an invalid group";
      return false;
    }
  output = std::move(candidate);
  diagnostic.clear();
  return true;
}

#endif // COIN_RENDER_SHADOW_CORE_H
