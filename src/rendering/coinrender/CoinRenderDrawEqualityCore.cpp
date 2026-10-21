#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderDrawEqualityCore.h"
#include <cstring>

bool
coin_render_same_draw(const CoinRenderDrawPacket &a, const CoinRenderDrawPacket &b)
{
  return a.topology == b.topology && a.geometry.firstVertex == b.geometry.firstVertex &&
         a.geometry.vertexCount == b.geometry.vertexCount &&
         a.geometry.firstIndex == b.geometry.firstIndex &&
         a.geometry.indexCount == b.geometry.indexCount && a.renderStateSlot == b.renderStateSlot &&
         a.shadowLightSlot == b.shadowLightSlot &&
         a.rasterShadowCaster == b.rasterShadowCaster &&
         a.multipassMask == b.multipassMask && a.multipassRasterFixed == b.multipassRasterFixed &&
         a.frameNodeOrdinal == b.frameNodeOrdinal &&
         a.sourceNodeId == b.sourceNodeId && a.stableNodeId == b.stableNodeId &&
         a.drawOrdinal == b.drawOrdinal && a.sourceRevision == b.sourceRevision &&
         a.hasSortingCenter == b.hasSortingCenter &&
         std::memcmp(a.sortingCenterWorld, b.sortingCenterWorld, sizeof(a.sortingCenterWorld)) ==
             0 &&
         a.renderLayer == b.renderLayer && a.authoredScene == b.authoredScene && a.clearDepthBefore == b.clearDepthBefore &&
         a.lineStripId == b.lineStripId;
}

bool
coin_render_same_draws(const std::vector<CoinRenderDrawPacket> &a,
                       const std::vector<CoinRenderDrawPacket> &b)
{
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (!coin_render_same_draw(a[i], b[i]))
      return false;
  return true;
}
