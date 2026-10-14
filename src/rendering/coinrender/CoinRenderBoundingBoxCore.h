#ifndef COIN_RENDER_BOUNDING_BOX_CORE_H
#define COIN_RENDER_BOUNDING_BOX_CORE_H

#include <Inventor/CoinRenderExport.h>

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderFloatCore.h"
#include <Inventor/SbBox3f.h>
#include <array>

// Six independent CCW quads, matching Coin's bounding-box cube. Keep each
// face's normal and UV, and its repeated edges/corners for polygon styles.
COIN_RENDER_DLL_API bool coin_render_bounding_box_valid(const SbBox3f & box, std::string & diagnostic);

COIN_RENDER_DLL_API bool coin_render_bounding_box_vertices(const SbBox3f & box, uint32_t material,
    std::array<CoinRenderVertexSnapshot, 24> & vertices, std::string & diagnostic);

#endif
