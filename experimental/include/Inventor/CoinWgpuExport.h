#ifndef COIN_RENDER_LEGACY_COINWGPUEXPORT_H
#define COIN_RENDER_LEGACY_COINWGPUEXPORT_H

// Source compatibility only. Rebuild clients against CoinRender.
#if defined(COIN_WGPU_INTERNAL) && !defined(COIN_RENDER_INTERNAL)
#define COIN_RENDER_INTERNAL
#endif
#include <Inventor/CoinRenderExport.h>
#define COIN_WGPU_DLL_API COIN_RENDER_DLL_API
#define COIN_WGPU_EXPORT_H COIN_RENDER_EXPORT_H

#endif // COIN_RENDER_LEGACY_COINWGPUEXPORT_H
