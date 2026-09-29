#ifndef COIN_RENDER_SELECTION_CORE_H
#define COIN_RENDER_SELECTION_CORE_H

#include <Inventor/rendering/CoinRenderOptions.h>
#include <string>

inline bool coin_render_valid_options(const CoinRenderOptions& options, std::string& diagnostic) {
  if (options.renderer != COIN_RENDER_RENDERER_UNKNOWN &&
      options.renderer != COIN_RENDER_RENDERER_VULKAN &&
      options.renderer != COIN_RENDER_RENDERER_OPENGL &&
      options.renderer != COIN_RENDER_RENDERER_D3D12) {
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

// Pure selection: hardware facts and implementation/profile evidence are inputs.
// No probe, scene traversal, environment parsing or GPU commands occur here.
inline CoinRenderSelection coin_render_selection(uint64_t required, uint64_t implemented,
                                                 uint64_t available, uint64_t qualified,
                                                 bool requireQualified = false) {
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

inline CoinRenderSelection coin_render_requested_mechanism(CoinRenderTransparencyMode mode,
                                                           uint64_t required, bool enabled) {
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

inline const char* coin_render_selection_diagnostic(uint32_t reason) {
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

#endif
