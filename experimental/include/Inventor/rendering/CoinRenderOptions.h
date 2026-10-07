#ifndef COIN_RENDER_OPTIONS_H
#define COIN_RENDER_OPTIONS_H

#include <Inventor/rendering/CoinRenderCapabilities.h>

enum CoinRenderSceneTextureMode {
  COIN_RENDER_SCENE_TEXTURE_STAGED = 0,
  COIN_RENDER_SCENE_TEXTURE_DIRECT = 1
};

enum CoinRenderStoredTextureColorSpace {
  COIN_RENDER_TEXTURE_LINEAR = 0,
  COIN_RENDER_TEXTURE_SRGB = 1
};

/** Immutable target configuration. Explicit options override environment defaults.
 * UNKNOWN renderer means the compiled connector's default. Extensions require
 * explicit selection; COIN preserves the captured transparency modality. */
struct CoinRenderOptions {
  CoinRenderRenderer renderer = COIN_RENDER_RENDERER_UNKNOWN;
  CoinRenderTransparencyMode transparency = COIN_RENDER_TRANSPARENCY_COIN;
  CoinRenderSceneTextureMode sceneTexture = COIN_RENDER_SCENE_TEXTURE_STAGED;
  // Explicit interpretation of stored SoTexture2 RGB, alpha remains linear.
  CoinRenderStoredTextureColorSpace storedTextureColorSpace = COIN_RENDER_TEXTURE_LINEAR;
  uint32_t maxTextureAnisotropy = 16; // q>0.85 requests this limit; no effect below it.
};

#endif
