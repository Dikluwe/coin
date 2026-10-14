#ifndef COIN_RENDER_DEPTH_CORE_H
#define COIN_RENDER_DEPTH_CORE_H

#include <Inventor/CoinRenderExport.h>
#include "rendering/coinrender/CoinRenderPlanAssemblyCore.h"
#include "rendering/coinrender/CoinRenderPolygonStyleCore.h"
#include <string>
#include <vector>

// Resolve original filled-triangle slope/maximum before executor depth formats
// supply their own quantum. Reversed ranges use the same window-space metric.
// The caller must validate the captured plan before resolving its draw ranges.
COIN_RENDER_DLL_API bool coin_render_resolve_triangle_depth(CoinRenderFramePlan &plan, std::string &diagnostic);
#endif
