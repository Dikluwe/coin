#ifndef COIN_RENDER_SAMPLING_CORE_H
#define COIN_RENDER_SAMPLING_CORE_H
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <algorithm>

inline bool coin_render_valid_sampling(const CoinRenderFramePlan& frame, std::string& diagnostic) {
  if (frame.textureSamplingPolicy != COIN_RENDER_SAMPLING_NATIVE &&
      frame.textureSamplingPolicy != COIN_RENDER_SAMPLING_PORTABLE) {
    diagnostic = "Invalid texture sampling policy; no fallback was applied";
    return false;
  }
  if (frame.textureSamplingPolicy == COIN_RENDER_SAMPLING_NATIVE) return true;
  // Most frames have no incompatible sampler (including untextured instancing).
  // Inspect the small sampler table first; walk per-draw states only if an
  // incompatible entry must be checked for an active reference.
  if (std::none_of(frame.samplers.begin(), frame.samplers.end(), [](const CoinRenderSamplerSnapshot& sampler) {
        return sampler.filter == CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR && sampler.maxAnisotropy != 1;
      })) return true;
  for (const auto& state : frame.renderStates) for (size_t unit=0; unit<COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
    const auto layer = coin_render_texture_unit(state, unit);
    if (!layer.enabled || layer.samplerSlot >= frame.samplers.size()) continue;
    const auto& sampler = frame.samplers[layer.samplerSlot];
    if (sampler.filter == CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR && sampler.maxAnisotropy != 1) {
      diagnostic = "Portable nearest/mip-linear requires isotropic anisotropy 1; no fallback was applied";
      return false;
    }
  }
  return true;
}
#endif
