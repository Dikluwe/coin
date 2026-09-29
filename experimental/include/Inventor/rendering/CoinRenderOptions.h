#ifndef COIN_RENDER_OPTIONS_H
#define COIN_RENDER_OPTIONS_H

#include <Inventor/rendering/CoinRenderCapabilities.h>

enum CoinRenderSceneTextureMode {
  COIN_RENDER_SCENE_TEXTURE_STAGED = 0,
  COIN_RENDER_SCENE_TEXTURE_DIRECT = 1
};

/** Immutable target configuration. Explicit options override environment defaults.
 * UNKNOWN renderer means the compiled connector's default. Extensions require
 * explicit selection; COIN preserves the captured transparency modality. */
struct CoinRenderOptions {
  CoinRenderRenderer renderer = COIN_RENDER_RENDERER_UNKNOWN;
  CoinRenderTransparencyMode transparency = COIN_RENDER_TRANSPARENCY_COIN;
  CoinRenderSceneTextureMode sceneTexture = COIN_RENDER_SCENE_TEXTURE_STAGED;
};

#endif
