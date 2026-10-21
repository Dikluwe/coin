#ifndef COIN_WGPU_PORTABLE_CORE_H
#define COIN_WGPU_PORTABLE_CORE_H
#include "CoinWgpuFfi.h"
#include <Inventor/CoinRenderExport.h>
// The callback has static code lifetime inside CoinRender (Rust is statically
// linked into that same library). Repeated registration is thread-safe and
// accepts only the same address. No device or scene is retained.
COIN_RENDER_DLL_API CoinWgpuStatus coin_wgpu_install_cpp_portable_core();
#endif
