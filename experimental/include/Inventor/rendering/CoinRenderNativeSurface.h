#ifndef COIN_RENDER_NATIVE_SURFACE_H
#define COIN_RENDER_NATIVE_SURFACE_H

/**************************************************************************\
 * Copyright (c) Kongsberg Oil & Gas Technologies AS
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\**************************************************************************/

#include <Inventor/SbBasic.h>
#include <stdint.h>

#define COIN_RENDER_NATIVE_SURFACE_ABI_VERSION 1

enum CoinRenderNativeSurfaceType {
  COIN_RENDER_SURFACE_XLIB = 1,
  COIN_RENDER_SURFACE_WAYLAND = 2,
  COIN_RENDER_SURFACE_WIN32 = 3,
  COIN_RENDER_SURFACE_APPKIT_LAYER = 4,
  COIN_RENDER_SURFACE_ANDROID_NDK = 5
};

/**
 * Tagged and versioned descriptor for native window presentation surfaces.
 *
 * Contract & Lifetime:
 * 1. Coin DOES NOT own the native window, display, connection or presentation layer.
 * 2. The host application must keep these handles valid until CoinRenderTarget is destroyed.
 * 3. CoinRenderTarget must be destroyed BEFORE destroying the underlying window or toolkit.
 * 4. Creation, resize, apply and destruction must occur on the thread owning the window.
 * 5. Size passed to CoinRenderTarget must be in framebuffer pixels, not logical window points.
 * 6. For Win32, hwnd is required and hinstance may be null; the caller keeps HWND alive.
 * 7. For Wayland, display and surface are required; the caller dispatches events.
 * 8. For AppKit, metalLayer is a CAMetalLayer retained by the host on the UI thread.
 * 9. For Android, nativeWindow is an ANativeWindow; release the target before
 *    APP_CMD_TERM_WINDOW returns and create a new target for each new window.
 */
struct CoinRenderNativeSurfaceDescriptor {
  uint32_t abiVersion;
  uint32_t structSize;
  uint32_t type;
  uint32_t reserved;

  union {
    struct {
      void * display;
      uint64_t window;
    } xlib;

    struct {
      void * display;
      void * surface;
    } wayland;

    struct {
      void * hinstance;
      void * hwnd;
    } win32;

    struct {
      void * metalLayer;
      void * reserved;
    } appkit;

    struct {
      void * nativeWindow;
      void * reserved;
    } android;
  } native;
};

#endif // !COIN_RENDER_NATIVE_SURFACE_H
