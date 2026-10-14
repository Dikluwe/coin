#ifndef COIN_RENDER_TEXTURE_ALPHA_CORE_H
#define COIN_RENDER_TEXTURE_ALPHA_CORE_H

#include <Inventor/CoinRenderExport.h>

#include "rendering/coinrender/CoinRenderTextureFormatCore.h"
#include <Inventor/nodes/SoSceneTexture2.h>

// Coin image flags decide scheduling/casting independently of sampled alpha.
// Shared by composition and shadow planning, including unresolved RTT tokens.
COIN_RENDER_DLL_API bool coin_render_scene_texture_policy_supported(int32_t policy);
COIN_RENDER_DLL_API bool coin_render_scene_texture_forces_transparency(int32_t policy);
COIN_RENDER_DLL_API bool coin_render_texture_has_transparency(const CoinRenderTextureImageSnapshot & image);

#endif
