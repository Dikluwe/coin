#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderAlphaTestCore.h"
#include <algorithm>
#include <cmath>

bool
coin_render_alpha_test_valid(CoinRenderAlphaTestFunction function, float reference)
{
  return static_cast<uint32_t>(function) <= 8 && std::isfinite(reference) && reference >= 0.0f &&
         reference <= 1.0f;
}

bool
coin_render_alpha_test_active(CoinRenderAlphaTestFunction function)
{
  return function != CoinRenderAlphaTestFunction::NONE &&
         function != CoinRenderAlphaTestFunction::ALWAYS;
}

bool
coin_render_alpha_test_pass(CoinRenderAlphaTestFunction function, float reference, float alpha)
{
  alpha = std::max(0.0f, std::min(1.0f, alpha));
  switch (function) {
  case CoinRenderAlphaTestFunction::NONE:
  case CoinRenderAlphaTestFunction::ALWAYS:
    return true;
  case CoinRenderAlphaTestFunction::NEVER:
    return false;
  case CoinRenderAlphaTestFunction::LESS:
    return alpha < reference;
  case CoinRenderAlphaTestFunction::LEQUAL:
    return alpha <= reference;
  case CoinRenderAlphaTestFunction::EQUAL:
    return alpha == reference;
  case CoinRenderAlphaTestFunction::GEQUAL:
    return alpha >= reference;
  case CoinRenderAlphaTestFunction::GREATER:
    return alpha > reference;
  case CoinRenderAlphaTestFunction::NOTEQUAL:
    return alpha != reference;
  }
  return false;
}
