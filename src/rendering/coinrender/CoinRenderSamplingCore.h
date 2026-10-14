#ifndef COIN_RENDER_SAMPLING_CORE_H
#define COIN_RENDER_SAMPLING_CORE_H

#include <Inventor/CoinRenderExport.h>
#include "rendering/coinrender/CoinRenderFramePlan.h"

COIN_RENDER_DLL_API bool coin_render_valid_sampling(const CoinRenderFramePlan& frame, std::string& diagnostic);
#endif
