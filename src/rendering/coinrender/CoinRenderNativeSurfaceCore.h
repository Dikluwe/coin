#ifndef COIN_RENDER_NATIVE_SURFACE_CORE_H
#define COIN_RENDER_NATIVE_SURFACE_CORE_H

#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <string>

// Mechanical descriptor validation, independent of platform/backend support.
inline std::string coin_render_surface_header_diagnostic(const CoinRenderNativeSurfaceDescriptor & d) {
  if (d.abiVersion != COIN_RENDER_NATIVE_SURFACE_ABI_VERSION)
    return "Invalid ABI version in CoinRenderNativeSurfaceDescriptor: expected 1";
  if (d.structSize != sizeof(d)) return "Invalid structSize in CoinRenderNativeSurfaceDescriptor";
  if (d.reserved != 0) return "Reserved field must be 0 in CoinRenderNativeSurfaceDescriptor";
  return {};
}
inline std::string coin_render_surface_handles_diagnostic(const CoinRenderNativeSurfaceDescriptor & d) {
  switch (d.type) {
  case COIN_RENDER_SURFACE_XLIB:
    if (!d.native.xlib.display) return "Null display pointer in Xlib surface descriptor";
    if (!d.native.xlib.window) return "Window ID must be non-zero in Xlib surface descriptor";
    break;
  case COIN_RENDER_SURFACE_WAYLAND:
    if (!d.native.wayland.display || !d.native.wayland.surface)
      return "Null wl_display or wl_surface in Wayland surface descriptor";
    break;
  case COIN_RENDER_SURFACE_WIN32:
    if (!d.native.win32.hwnd) return "Null HWND in Win32 surface descriptor";
    break;
  case COIN_RENDER_SURFACE_APPKIT_LAYER:
    if (!d.native.appkit.metalLayer || d.native.appkit.reserved)
      return "AppKit requires a non-null CAMetalLayer and null reserved pointer";
    break;
  case COIN_RENDER_SURFACE_ANDROID_NDK:
    if (!d.native.android.nativeWindow || d.native.android.reserved)
      return "Android requires a non-null ANativeWindow and null reserved pointer";
    break;
  default: return "Unknown native surface type in CoinRenderNativeSurfaceDescriptor";
  }
  return {};
}
#endif
