#ifndef COIN_RENDER_ALPHA_TEST_CORE_H
#define COIN_RENDER_ALPHA_TEST_CORE_H
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <algorithm>
#include <cmath>

inline bool coin_render_alpha_test_valid(CoinRenderAlphaTestFunction function, float reference) {
  return static_cast<uint32_t>(function) <= 8 && std::isfinite(reference) &&
    reference >= 0.0f && reference <= 1.0f;
}
inline bool coin_render_alpha_test_active(CoinRenderAlphaTestFunction function) {
  return function != CoinRenderAlphaTestFunction::NONE && function != CoinRenderAlphaTestFunction::ALWAYS;
}
inline bool coin_render_alpha_test_pass(CoinRenderAlphaTestFunction function, float reference, float alpha) {
  alpha = std::max(0.0f, std::min(1.0f, alpha));
  switch (function) {
  case CoinRenderAlphaTestFunction::NONE:
  case CoinRenderAlphaTestFunction::ALWAYS: return true;
  case CoinRenderAlphaTestFunction::NEVER: return false;
  case CoinRenderAlphaTestFunction::LESS: return alpha < reference;
  case CoinRenderAlphaTestFunction::LEQUAL: return alpha <= reference;
  case CoinRenderAlphaTestFunction::EQUAL: return alpha == reference;
  case CoinRenderAlphaTestFunction::GEQUAL: return alpha >= reference;
  case CoinRenderAlphaTestFunction::GREATER: return alpha > reference;
  case CoinRenderAlphaTestFunction::NOTEQUAL: return alpha != reference;
  }
  return false;
}
#endif
