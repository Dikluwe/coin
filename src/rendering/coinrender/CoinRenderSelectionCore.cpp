#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderSelectionCore.h"
#include <cstdio>
#include <cmath>

bool coin_render_valid_pbr_effects(const CoinRenderPbrEffects & e,std::string & diagnostic) {
  const auto n=e.reflectionPlane.getNormal();
  if(!std::isfinite(n[0])||!std::isfinite(n[1])||!std::isfinite(n[2])||
     std::abs(n.sqrLength()-1.f)>.0001f||!std::isfinite(e.reflectionPlane.getDistanceFromOrigin())||
     !std::isfinite(e.reflectionPlaneTolerance)||e.reflectionPlaneTolerance<=0||
     (e.reflectionScale!=1&&e.reflectionScale!=2&&e.reflectionScale!=4)||!e.reflectionBudgetBytes) {
    diagnostic="Invalid PBR planar reflection plane/scale/budget";return false;
  }
  if(!e.resourceBudgetBytes||e.shadowMapSize<128||e.shadowMapSize>4096||(e.shadowMapSize&(e.shadowMapSize-1))||
     !std::isfinite(e.shadowBias)||e.shadowBias<0||e.shadowBias>.05f||
     !std::isfinite(e.occlusionRadius)||e.occlusionRadius<=0||e.occlusionRadius>1000000||
     !std::isfinite(e.occlusionStrength)||e.occlusionStrength<0||e.occlusionStrength>1) {
    diagnostic="Invalid PBR shadow/AO parameters";return false;
  }
  for(int i=0;i<3;++i)if(!std::isfinite(e.backgroundTop[i])||!std::isfinite(e.backgroundBottom[i])||
      e.backgroundTop[i]<0||e.backgroundTop[i]>1||e.backgroundBottom[i]<0||e.backgroundBottom[i]>1) {
    diagnostic="PBR background requires finite sRGB colors in [0,1]";return false;
  }
  return true;
}

bool
coin_render_valid_options(const CoinRenderOptions &options, std::string &diagnostic)
{
  if(options.pbrAnisotropicEnvironmentSamples>128 || !options.pbrAnisotropyCpuBudgetBytes) {diagnostic="Invalid anisotropic IBL sample bound/CPU budget";return false;}
  if(!coin_render_valid_pbr_effects(options.pbrEffects,diagnostic))return false;
  if (!std::isfinite(options.pbrExposureStops) || options.pbrExposureStops < -16.0f ||
      options.pbrExposureStops > 16.0f) {
    diagnostic = "PBR exposure must be finite in [-16,16] stops";
    return false;
  }
  if (!std::isfinite(options.pbrEnvironmentRotation) || !std::isfinite(options.pbrEnvironmentIntensity) ||
      options.pbrEnvironmentIntensity < 0 || options.pbrEnvironmentIntensity > 64 ||
      !options.pbrEnvironmentBudgetBytes || !std::isfinite(options.pbrSpecularFilterVariance) ||
      options.pbrSpecularFilterVariance < 0 || options.pbrSpecularFilterVariance > 1 ||
      !std::isfinite(options.pbrSpecularFilterThreshold) || options.pbrSpecularFilterThreshold < 0 ||
      options.pbrSpecularFilterThreshold > 1) {
    diagnostic = "Invalid PBR environment parameters/budget/quality"; return false;
  }
  if (options.sampleCount != 1 && options.sampleCount != 2 && options.sampleCount != 4) {
    diagnostic = "MSAA sample count must be 1, 2 or 4; no fallback was applied";
    return false;
  }
  if (options.rasterizationPolicy != COIN_RENDER_RASTER_NATIVE &&
      options.rasterizationPolicy != COIN_RENDER_RASTER_CANONICAL_8BIT) {
    diagnostic = "Invalid rasterization policy; no fallback was applied";
    return false;
  }
  if (options.textureSamplingPolicy != COIN_RENDER_SAMPLING_NATIVE &&
      options.textureSamplingPolicy != COIN_RENDER_SAMPLING_PORTABLE) {
    diagnostic = "Invalid texture sampling policy; no fallback was applied";
    return false;
  }
  if (uint32_t(options.storedTextureColorSpace) > 1 || !options.maxTextureAnisotropy ||
      options.maxTextureAnisotropy > 16 ||
      (options.maxTextureAnisotropy & (options.maxTextureAnisotropy - 1))) {
    diagnostic =
        "Stored texture color space must be LINEAR/SRGB and anisotropy a power of two in 1..16";
    return false;
  }

  if (options.renderer != COIN_RENDER_RENDERER_UNKNOWN &&
      options.renderer != COIN_RENDER_RENDERER_VULKAN &&
      options.renderer != COIN_RENDER_RENDERER_OPENGL &&
      options.renderer != COIN_RENDER_RENDERER_D3D12 &&
      options.renderer != COIN_RENDER_RENDERER_METAL) {
    diagnostic = "Invalid renderer option";
    return false;
  }
  if (options.transparency < COIN_RENDER_TRANSPARENCY_COIN ||
      options.transparency > COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT) {
    diagnostic = "Invalid transparency option";
    return false;
  }
  if (options.sceneTexture != COIN_RENDER_SCENE_TEXTURE_STAGED &&
      options.sceneTexture != COIN_RENDER_SCENE_TEXTURE_DIRECT) {
    diagnostic = "Invalid scene texture option";
    return false;
  }
  return true;
}

CoinRenderSelection
coin_render_selection(uint64_t required, uint64_t implemented, uint64_t available,
                      uint64_t qualified, bool requireQualified)
{
  CoinRenderSelection result{};
  result.mechanism = required;
  if (!required || (required & (required - 1)) ||
      (required & ~uint64_t(COIN_RENDER_MECHANISM_OBJECT | COIN_RENDER_MECHANISM_PEELING |
                            COIN_RENDER_MECHANISM_WEIGHTED_OIT)))
    result.reason = COIN_RENDER_SELECTION_INVALID_REQUEST;
  else if ((implemented & required) == 0)
    result.reason = COIN_RENDER_SELECTION_NOT_IMPLEMENTED;
  else if ((available & required) == 0)
    result.reason = COIN_RENDER_SELECTION_HARDWARE_UNAVAILABLE;
  else if (requireQualified && (qualified & required) == 0)
    result.reason = COIN_RENDER_SELECTION_UNQUALIFIED_PROFILE;
  else
    result.reason = COIN_RENDER_SELECTION_SUPPORTED;
  result.qualified_profile = (qualified & required) != 0;
  return result;
}

CoinRenderSelection
coin_render_requested_mechanism(CoinRenderTransparencyMode mode, uint64_t required, bool enabled)
{
  CoinRenderSelection result{};
  result.mechanism = required;
  if (mode < COIN_RENDER_TRANSPARENCY_COIN || mode > COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT) {
    result.reason = COIN_RENDER_SELECTION_INVALID_REQUEST;
    return result;
  }
  if (!enabled)
    result.mechanism = COIN_RENDER_MECHANISM_OBJECT;
  else if (mode == COIN_RENDER_TRANSPARENCY_OBJECT) {
    if (required == COIN_RENDER_MECHANISM_PEELING) {
      result.reason = COIN_RENDER_SELECTION_COIN_MODE_CONFLICT;
      return result;
    }
    result.mechanism = COIN_RENDER_MECHANISM_OBJECT;
  } else if (mode == COIN_RENDER_TRANSPARENCY_PEELING)
    result.mechanism = COIN_RENDER_MECHANISM_PEELING;
  else if (mode == COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT)
    result.mechanism = COIN_RENDER_MECHANISM_WEIGHTED_OIT;
  return result;
}

const char *
coin_render_selection_diagnostic(uint32_t reason)
{
  switch (reason) {
  case COIN_RENDER_SELECTION_SUPPORTED:
    return "";
  case COIN_RENDER_SELECTION_NOT_IMPLEMENTED:
    return "Requested transparency mechanism is not implemented; no fallback was applied";
  case COIN_RENDER_SELECTION_HARDWARE_UNAVAILABLE:
    return "Requested transparency mechanism lacks runtime hardware support; no fallback was "
           "applied";
  case COIN_RENDER_SELECTION_UNQUALIFIED_PROFILE:
    return "Requested transparency mechanism has no qualified profile for this target; no fallback "
           "was applied";
  case COIN_RENDER_SELECTION_RUNTIME_NOT_READY:
    return "Runtime probe is not ready; hardware availability is unknown";
  case COIN_RENDER_SELECTION_COIN_MODE_CONFLICT:
    return "Object mechanism cannot implement SORTED_LAYERS_BLEND; no fallback was applied";
  default:
    return "Invalid transparency mechanism request";
  }
}
