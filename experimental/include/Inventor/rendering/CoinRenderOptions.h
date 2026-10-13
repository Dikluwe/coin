#ifndef COIN_RENDER_OPTIONS_H
#define COIN_RENDER_OPTIONS_H

#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/SbString.h>

enum CoinRenderSceneTextureMode {
  COIN_RENDER_SCENE_TEXTURE_STAGED = 0,
  COIN_RENDER_SCENE_TEXTURE_DIRECT = 1
};

enum CoinRenderStoredTextureColorSpace {
  COIN_RENDER_TEXTURE_LINEAR = 0,
  COIN_RENDER_TEXTURE_SRGB = 1
};

/** Structural target configuration. Exposure and environment scalar parameters
 * may subsequently change through the target's explicit setters.
 * Explicit options override environment defaults.
 * UNKNOWN renderer means the compiled connector's default. Extensions require
 * explicit selection; COIN preserves the captured transparency modality. */
struct CoinRenderOptions {
  CoinRenderRenderer renderer = COIN_RENDER_RENDERER_UNKNOWN;
  CoinRenderTransparencyMode transparency = COIN_RENDER_TRANSPARENCY_COIN;
  CoinRenderSceneTextureMode sceneTexture = COIN_RENDER_SCENE_TEXTURE_STAGED;
  // Explicit interpretation of stored SoTexture2 RGB, alpha remains linear.
  CoinRenderStoredTextureColorSpace storedTextureColorSpace = COIN_RENDER_TEXTURE_LINEAR;
  uint32_t maxTextureAnisotropy = 16; // q>0.85 requests this limit; no effect below it.
  // Native is the default. Portable reconstructs nearest/mip-linear using
  // original UV derivatives, POT active-mip centering, and two NPOT centers.
  // Active nearest/mip-linear samplers with anisotropy >1 are rejected.
  CoinRenderTextureSamplingPolicy textureSamplingPolicy = COIN_RENDER_SAMPLING_NATIVE;
  // PBR output exposure in stops. Legacy-only frames keep their existing output.
  float pbrExposureStops = 0.0f;
  // Immutable versioned .cibl bundle; empty disables IBL. World Z is up.
  SbString pbrEnvironmentBundle;
  float pbrEnvironmentRotation = 0.0f; // radians around world +Z
  float pbrEnvironmentIntensity = 1.0f;
  // Quality profile with a configured environment. False retains the M3 oracle.
  bool pbrEnergyCompensation = true;
  // Screen-space normal variance broadens the same GGX width for direct/IBL.
  float pbrSpecularFilterVariance = 0.15f;
  float pbrSpecularFilterThreshold = 0.10f; // zero disables filtering

};

#endif
