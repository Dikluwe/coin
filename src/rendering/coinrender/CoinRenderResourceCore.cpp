#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderResourceCore.h"

bool
coin_render_readback_admitted(uint64_t jobs, uint64_t bytes, uint64_t requested)
{
  const uint64_t budget = UINT64_C(128) * 1024 * 1024;
  return jobs < 16 && requested <= budget && bytes <= budget - requested;
}
