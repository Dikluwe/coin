#ifndef COIN_RENDER_LIGHTING_CORE_H
#define COIN_RENDER_LIGHTING_CORE_H

#include <Inventor/CoinRenderExport.h>

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <Inventor/SbVec4f.h>

// Snapshot-only Gouraud lighting, shared by CPU rasterization and polygon styles.
COIN_RENDER_DLL_API SbVec4f coin_render_shade_vertex(const CoinRenderMaterialSnapshot& material,
                                        const SbVec3f& vPos, const SbVec3f& n,
                                        const CoinRenderLightingSnapshot& lighting,
                                        const CoinRenderRenderStateSnapshot& rs);

#endif
