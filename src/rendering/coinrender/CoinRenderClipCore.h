#ifndef COIN_RENDER_CLIP_CORE_H
#define COIN_RENDER_CLIP_CORE_H

#include <Inventor/CoinRenderExport.h>
#include "rendering/coinrender/CoinRenderFloatCore.h"

#include "rendering/coinrender/CoinRenderFramePlan.h"

// Coin supplies world-space planes at node traversal. Keep n.p - distance >= 0.
// This Core consumes snapshots only; no actions, SoState or GPU operations.
COIN_RENDER_DLL_API bool coin_render_clip_equations(const CoinRenderRenderStateSnapshot & state,
                                       float equations[COIN_RENDER_MAX_CLIP_PLANES][4],
                                       std::string & diagnostic);

COIN_RENDER_DLL_API bool coin_render_clip_point(const CoinRenderRenderStateSnapshot & state,
                                   const CoinRenderVertexSnapshot & vertex);

// Parametric interval of the original segment, before screen-space expansion.
COIN_RENDER_DLL_API bool coin_render_clip_segment(const CoinRenderRenderStateSnapshot & state,
                                     const CoinRenderVertexSnapshot & a,
                                     const CoinRenderVertexSnapshot & b,
                                     float & first, float & last);

COIN_RENDER_DLL_API CoinRenderVertexSnapshot coin_render_clip_interpolate(
  const CoinRenderVertexSnapshot & a, const CoinRenderVertexSnapshot & b,
  float t, uint32_t materialSlot);
#endif
