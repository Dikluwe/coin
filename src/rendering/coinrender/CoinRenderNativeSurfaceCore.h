#ifndef COIN_RENDER_NATIVE_SURFACE_CORE_H
#define COIN_RENDER_NATIVE_SURFACE_CORE_H

#include <Inventor/CoinRenderExport.h>

#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <string>

// Mechanical descriptor validation, independent of platform/backend support.
COIN_RENDER_DLL_API std::string coin_render_surface_header_diagnostic(const CoinRenderNativeSurfaceDescriptor & d);
COIN_RENDER_DLL_API std::string coin_render_surface_handles_diagnostic(const CoinRenderNativeSurfaceDescriptor & d);
#endif
