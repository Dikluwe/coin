#ifndef COIN_RENDER_ALPHA_TEST_CAPTURE_H
#define COIN_RENDER_ALPHA_TEST_CAPTURE_H

#include <Inventor/CoinRenderExport.h>
// Wiring: Coin's lazy element keeps GL function tokens so separator push/pop
// and callbacks see its native state. Only semantic values leave this adapter.
#include "rendering/coinrender/CoinRenderAlphaTestCore.h"
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/system/gl.h>

COIN_RENDER_DLL_API bool coin_render_capture_alpha_node(const SoAlphaTest & node, SoState * state);
COIN_RENDER_DLL_API bool coin_render_snapshot_alpha_test(SoState * state,
                                            CoinRenderAlphaTestFunction & function,
                                            float & reference);
#endif
