#ifndef COIN_RENDER_LIGHT_EQUALITY_CORE_H
#define COIN_RENDER_LIGHT_EQUALITY_CORE_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <cstring>

inline bool
coin_render_same_lights(const std::vector<CoinRenderLightSourceSnapshot> & a,
                        const std::vector<CoinRenderLightSourceSnapshot> & b)
{
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const auto & x = a[i];
    const auto & y = b[i];
    if (x.sourceRevision != y.sourceRevision || x.type != y.type ||
        x.hasAuthoredSource != y.hasAuthoredSource ||
        x.intensity != y.intensity || x.cutOffAngle != y.cutOffAngle ||
        x.dropOffRate != y.dropOffRate ||
        std::memcmp(x.sourceModel.getValue(), y.sourceModel.getValue(), sizeof(float) * 16) != 0 ||
        std::memcmp(x.sourceWorldModel.getValue(), y.sourceWorldModel.getValue(), sizeof(float) * 16) != 0 ||
        std::memcmp(x.authoredDirection, y.authoredDirection, sizeof(x.authoredDirection)) != 0 ||
        std::memcmp(x.authoredPosition, y.authoredPosition, sizeof(x.authoredPosition)) != 0 ||
        std::memcmp(x.color, y.color, sizeof(x.color)) != 0 ||
        std::memcmp(x.direction, y.direction, sizeof(x.direction)) != 0 ||
        std::memcmp(x.position, y.position, sizeof(x.position)) != 0 ||
        std::memcmp(x.attenuation, y.attenuation, sizeof(x.attenuation)) != 0)
      return false;
  }
  return true;
}

#endif
