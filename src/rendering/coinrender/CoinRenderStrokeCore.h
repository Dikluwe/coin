#ifndef COIN_RENDER_STROKE_CORE_H
#define COIN_RENDER_STROKE_CORE_H

#include <Inventor/CoinRenderExport.h>
#include "rendering/coinrender/CoinRenderPlanAssemblyCore.h"
#include "rendering/coinrender/CoinRenderPolygonStyleCore.h"
#include "rendering/coinrender/CoinRenderTextureCoordinateCore.h"

#include "rendering/coinrender/CoinRenderClipCore.h"
#include "rendering/coinrender/CoinRenderLineStippleCore.h"
#include <unordered_map>

// Mechanical expansion of captured strokes, shared by every GPU backend.
COIN_RENDER_DLL_API bool coin_render_expand_strokes(CoinRenderFramePlan& plan, std::string& diagnostic);

#endif
