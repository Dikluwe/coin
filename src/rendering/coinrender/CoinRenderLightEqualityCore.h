#ifndef COIN_RENDER_LIGHT_EQUALITY_CORE_H
#define COIN_RENDER_LIGHT_EQUALITY_CORE_H

#include <Inventor/CoinRenderExport.h>

#include "rendering/coinrender/CoinRenderFramePlan.h"

COIN_RENDER_DLL_API bool
coin_render_same_lights(const std::vector<CoinRenderLightSourceSnapshot> & a,
                        const std::vector<CoinRenderLightSourceSnapshot> & b);

#endif
