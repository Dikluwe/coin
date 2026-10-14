#ifndef COIN_RENDER_DRAW_EQUALITY_CORE_H
#define COIN_RENDER_DRAW_EQUALITY_CORE_H

#include <Inventor/CoinRenderExport.h>

#include "rendering/coinrender/CoinRenderFramePlan.h"

// Captured fields define the payload. Object padding is compiler-dependent
// and may differ between two full captures of the same scene.
COIN_RENDER_DLL_API bool coin_render_same_draw(const CoinRenderDrawPacket & a,
                                  const CoinRenderDrawPacket & b);

COIN_RENDER_DLL_API bool coin_render_same_draws(const std::vector<CoinRenderDrawPacket> & a,
                                   const std::vector<CoinRenderDrawPacket> & b);
#endif
