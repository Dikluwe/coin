#ifndef COIN_RENDER_ALPHA_TEST_CORE_H
#define COIN_RENDER_ALPHA_TEST_CORE_H

#include <Inventor/CoinRenderExport.h>
#include "rendering/coinrender/CoinRenderFramePlan.h"

COIN_RENDER_DLL_API bool coin_render_alpha_test_valid(CoinRenderAlphaTestFunction function, float reference);
COIN_RENDER_DLL_API bool coin_render_alpha_test_active(CoinRenderAlphaTestFunction function);
COIN_RENDER_DLL_API bool coin_render_alpha_test_pass(CoinRenderAlphaTestFunction function, float reference, float alpha);
#endif
