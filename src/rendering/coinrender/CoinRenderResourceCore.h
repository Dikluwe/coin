#ifndef COIN_RENDER_RESOURCE_CORE_H
#define COIN_RENDER_RESOURCE_CORE_H

#include <Inventor/CoinRenderExport.h>
#include <cstdint>
// Nominal packed output admission; physical staging memory belongs to Infra.
COIN_RENDER_DLL_API bool coin_render_readback_admitted(uint64_t jobs, uint64_t bytes, uint64_t requested);
#endif
