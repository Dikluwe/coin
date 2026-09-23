#ifndef COIN_SOWGPUNATIVESURFACE_H
#define COIN_SOWGPUNATIVESURFACE_H

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

#define COIN_WGPU_NATIVE_SURFACE_ABI_VERSION 1

enum SoWgpuNativeSurfaceType {
  COIN_WGPU_SURFACE_XLIB = 1,
  COIN_WGPU_SURFACE_WAYLAND = 2,
  COIN_WGPU_SURFACE_WIN32 = 3,
  COIN_WGPU_SURFACE_APPKIT_LAYER = 4
};

/**
 * Tagged and versioned descriptor for native window presentation surfaces.
 *
 * Contract & Lifetime:
 * 1. Coin DOES NOT own the native window, display, connection or presentation layer.
 * 2. The host application must keep these handles valid until SoWgpuRenderTarget is destroyed.
 * 3. SoWgpuRenderTarget must be destroyed BEFORE destroying the underlying window or toolkit.
 * 4. Creation, resize, apply and destruction must occur on the thread owning the window.
 * 5. Size passed to SoWgpuRenderTarget must be in framebuffer pixels, not logical window points.
 */
struct SoWgpuNativeSurfaceDescriptor {
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
  } native;
};

#endif // !COIN_SOWGPUNATIVESURFACE_H
