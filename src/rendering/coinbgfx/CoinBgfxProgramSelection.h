#ifndef COIN_BGFX_PROGRAM_SELECTION_H
#define COIN_BGFX_PROGRAM_SELECTION_H

#include "rendering/coinbgfx/CoinBgfxLowering.h"


// The solid fragment stage preserves the base vertex stage's material/lighting
// and the shared window-depth contract. Qualify every draw before omitting any
// fragment surface operation; mixed or advanced plans keep the general stage.
bool
coin_bgfx_solid_program(const std::vector<CoinBgfxDraw> & draws,
                        bool hasShadows,
                        CoinBgfxTransparencyStrategy strategy);

// Uses the shared surface shader with only base 2D sampling retained. The
// remaining material, fragment and fixed-function operations stay intact.
bool
coin_bgfx_texture2d_program(const std::vector<CoinBgfxDraw> & draws,
                            bool hasShadows,
                            CoinBgfxTransparencyStrategy strategy);

#endif
