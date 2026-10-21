#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderFloatCore.h"

bool
coin_render_is_finite(float value)
{
  return coin_render_is_finite_inline(value);
}
