#ifndef COIN_RENDER_OPTIONS_H
#define COIN_RENDER_OPTIONS_H

#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/SbString.h>
#include <Inventor/SbColor.h>
#include <Inventor/SbPlane.h>

// Executor effects for the opaque, single-camera Vulkan PBR profile.
struct CoinRenderPbrEffects {
  // One authored planar reflection; smooth by default, optional GGX proxy filter.
  bool planarReflection = false;
  bool roughPlanarReflection = false; // finite proxy, isotropic untextured roughness
  SbPlane reflectionPlane = SbPlane(SbVec3f(0,0,1),0);
  uint32_t reflectionScale = 2; // 1, 2 or 4; physical output size divided by scale.
  float reflectionPlaneTolerance = .0001f; // authored scene units
  uint64_t reflectionBudgetBytes = uint64_t(64)*1024*1024;
  bool shadows = false, ambientOcclusion = false, background = false;
  uint32_t shadowMapSize = 1024; // power of two, 128..4096
  float shadowBias = .0004f;
  float occlusionRadius = 20.f, occlusionStrength = .85f; // scene units, [0,1]
  // Resident and replacement depth FBO attachments; excludes HDR/IBL/geometry.
  uint64_t resourceBudgetBytes = uint64_t(128)*1024*1024;
  // sRGB colors; output background is independent of scene exposure.
  SbColor backgroundTop = SbColor(.22f,.27f,.34f);
  SbColor backgroundBottom = SbColor(.68f,.71f,.75f);
  bool operator==(const CoinRenderPbrEffects & b) const {
    return planarReflection==b.planarReflection && roughPlanarReflection==b.roughPlanarReflection && reflectionPlane==b.reflectionPlane &&
      reflectionScale==b.reflectionScale && reflectionPlaneTolerance==b.reflectionPlaneTolerance &&
      reflectionBudgetBytes==b.reflectionBudgetBytes && shadows==b.shadows && ambientOcclusion==b.ambientOcclusion && background==b.background &&
      shadowMapSize==b.shadowMapSize && shadowBias==b.shadowBias && occlusionRadius==b.occlusionRadius &&
      occlusionStrength==b.occlusionStrength && resourceBudgetBytes==b.resourceBudgetBytes &&
      backgroundTop==b.backgroundTop && backgroundBottom==b.backgroundBottom;
  }
};

enum CoinRenderSceneTextureMode {
  COIN_RENDER_SCENE_TEXTURE_STAGED = 0,
  COIN_RENDER_SCENE_TEXTURE_DIRECT = 1
};

enum CoinRenderStoredTextureColorSpace {
  COIN_RENDER_TEXTURE_LINEAR = 0,
  COIN_RENDER_TEXTURE_SRGB = 1
};

enum CoinRenderRasterizationPolicy {
  COIN_RENDER_RASTER_NATIVE = 0,
  COIN_RENDER_RASTER_CANONICAL_8BIT = 1
};

/** Initial target configuration. Explicit options override environment defaults.
 * PBR output exposure can also be changed through the target setter.
 * UNKNOWN renderer means the compiled connector's default. Extensions require
 * explicit selection; COIN preserves the captured transparency modality. */
struct CoinRenderOptions {
  CoinRenderRenderer renderer = COIN_RENDER_RENDERER_UNKNOWN;
  CoinRenderTransparencyMode transparency = COIN_RENDER_TRANSPARENCY_COIN;
  CoinRenderSceneTextureMode sceneTexture = COIN_RENDER_SCENE_TEXTURE_STAGED;
  // Explicit interpretation of stored SoTexture2/SoTexture3/SoTextureCubeMap RGB, alpha remains linear.
  CoinRenderStoredTextureColorSpace storedTextureColorSpace = COIN_RENDER_TEXTURE_LINEAR;
  uint32_t maxTextureAnisotropy = 16; // q>0.85 requests this limit; no effect below it.
  // Native is the default. Portable reconstructs nearest/mip-linear using
  // original UV derivatives, POT active-mip centering, and two NPOT centers.
  // Active nearest/mip-linear samplers with anisotropy >1 are rejected.
  CoinRenderTextureSamplingPolicy textureSamplingPolicy = COIN_RENDER_SAMPLING_NATIVE;
  // Explicit vertex snap to odd 1/256 window coordinates (1/128 pixel step).
  // Half-step cells round toward +infinity; horizontal/vertical edges avoid
  // pixel centers, where native GL and Vulkan fill conventions differ.
  // Retains original Z/W for fragment clipping/depth. Native remains default.
  CoinRenderRasterizationPolicy rasterizationPolicy = COIN_RENDER_RASTER_NATIVE;
  // Native RGBA8 BGFX/wgpu profiles: 1 (default), 2 or 4 samples,
  // with opaque or common object transparency. Offscreen,
  // window and RTT output have explicit backend/profile limits; combinations
  // without a supported resolve path report UNSUPPORTED.
  uint32_t sampleCount = 1;
  // PBR output exposure in stops [-16,16]. Does not change scene radiance.
  float pbrExposureStops = 0.0f;
  // Immutable .cibl source per target. Empty keeps direct lighting only.
  SbString pbrEnvironmentBundle;
  float pbrEnvironmentRotation = 0.0f; // radians around world +Z
  float pbrEnvironmentIntensity = 1.0f; // [0,64]
  uint64_t pbrEnvironmentBudgetBytes = uint64_t(128) * 1024 * 1024;
  bool pbrEnergyCompensation = true;
  // Same bounded VNDF source-radiance integration as codex/pbr-clean.
  // Zero explicitly selects the older bent-reflection diagnostic profile.
  uint32_t pbrAnisotropicEnvironmentSamples = 16;
  SbString pbrAnisotropyProducts; // Optional immutable .cani; otherwise prepare once.
  uint64_t pbrAnisotropyCpuBudgetBytes = uint64_t(32)*1024*1024;
  float pbrSpecularFilterVariance = 0.15f;
  float pbrSpecularFilterThreshold = 0.10f;
  CoinRenderPbrEffects pbrEffects;
};

#endif
