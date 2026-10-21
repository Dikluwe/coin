#ifndef COIN_RENDER_TEXT2_SHADOW_CAPTURE_H
#define COIN_RENDER_TEXT2_SHADOW_CAPTURE_H

#include "rendering/coinrender/CoinRenderText2Capture.h"

// Internal capture path: retain glyph coverage outside the observer viewport
// so shadow-map cameras can project it independently. The exported Text2
// capture signature and ordinary behavior remain unchanged.
COIN_RENDER_DLL_API bool coin_render_capture_text2_for_shadow(const SoText2 & node, SoState * state,
                                          CoinRenderText2Raster & output,
                                          CoinRenderText2Capture::Status & status,
                                          std::string & diagnostic);

#endif
