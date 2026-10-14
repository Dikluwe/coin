#ifndef COIN_RENDER_LEGACY_SOWGPURENDERACTION_H
#define COIN_RENDER_LEGACY_SOWGPURENDERACTION_H

// Source compatibility only. Rebuild clients against CoinRender.
#include <Inventor/actions/CoinRenderAction.h>
#define COIN_WGPU_DLL_API COIN_RENDER_DLL_API
typedef CoinRenderReadbackTicket SoWgpuReadbackTicket;
typedef CoinRenderAction SoWgpuRenderAction;
typedef CoinRenderActionP SoWgpuRenderActionP;
typedef CoinRenderTarget SoWgpuRenderTarget;

#endif // COIN_RENDER_LEGACY_SOWGPURENDERACTION_H
