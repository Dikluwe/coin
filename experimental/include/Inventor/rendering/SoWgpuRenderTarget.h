#ifndef COIN_SOWGPURENDERTARGET_H
#define COIN_SOWGPURENDERTARGET_H
#include <Inventor/CoinWgpuExport.h>

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
#include <vector>
#include <cstdint>
#include <cstddef>
#include <Inventor/SbVec2i32.h>
#include <Inventor/SbString.h>
#include <Inventor/tools/SbPimplPtr.h>
#include <Inventor/rendering/SoWgpuNativeSurface.h>

class SoWgpuRenderTargetP;
class SoWgpuRenderActionP;

/**
 * Resource-cache counters for the most recent and cumulative submissions.
 *
 * Values are snapshots and do not transfer ownership of backend resources.
 */
struct SoWgpuCacheTelemetry {
  uint64_t cumulativeUploads = 0;
  uint64_t cumulativeHits = 0;
  uint64_t cumulativeMisses = 0;
  uint64_t cumulativeUploadedBytes = 0;
  uint64_t frameUploadedBytes = 0;
  uint64_t frameUploads = 0;
  uint64_t frameHits = 0;
  uint64_t activeEntries = 0;
  uint64_t retiredEntries = 0;
  uint64_t completedSerial = 0;
  uint64_t submissionSerial = 0;
};

/**
 * Opaque experimental handle for an asynchronous offscreen readback.
 *
 * A ticket remains valid independently of the action and target lifetime.
 * Successful polling or explicit cancellation consumes it.
 */
struct SoWgpuReadbackTicket {
  uint64_t token = 0;
  uint64_t generation = 0;
  uint64_t submissionSerial = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t colorFormat = 0; // RGBA8_UNORM
  uint32_t depthFormat = 0; // 0=none, 1=DEPTH32_FLOAT
  uint32_t colorRowPitch = 0;
  uint32_t depthRowPitch = 0;
  uint64_t colorBytes = 0;
  uint64_t depthBytes = 0;
};

/**
 * Experimental offscreen or native-window destination for WebGPU rendering.
 *
 * The target owns backend attachments and presentation state, but never owns
 * native window handles supplied through SoWgpuNativeSurfaceDescriptor.
 *
 * \see coin_wgpu_experimental
 * \see SoWgpuRenderAction
 */
class COIN_WGPU_DLL_API SoWgpuRenderTarget {
public:
  typedef struct ::SoWgpuCacheTelemetry SoWgpuCacheTelemetry;
  enum Status {
    TARGET_READY = 0,
    TARGET_NOT_READY,
    TARGET_LOST,
    TARGET_ERROR,
    TARGET_SURFACE_LOST
  };

  enum ReadbackStatus {
    READBACK_READY = 0,
    READBACK_NOT_READY,
    READBACK_INVALID_TICKET,
    READBACK_DEVICE_LOST,
    READBACK_ERROR,
    READBACK_UNSUPPORTED
  };

  static SoWgpuRenderTarget * createOffscreen(const SbVec2i32 & size);
  static SoWgpuRenderTarget * createWindow(
    const SoWgpuNativeSurfaceDescriptor & descriptor,
    const SbVec2i32 & framebufferSize);

  ~SoWgpuRenderTarget(void);

  Status getStatus(void) const;
  const char * getLastError(void) const;
  const SbVec2i32 & getSize(void) const;
  SbBool resize(const SbVec2i32 & size);
  /**
   * Request a depth readback on offscreen renders. Enabled by default.
   * Disabling it leaves depth testing intact, but readbackDepth() returns
   * an empty vector and async tickets omit depth. Changing the mode
   * invalidates synchronous readback until the next successful render.
   * Window targets do not support this setting.
   */
  SbBool setDepthReadbackEnabled(SbBool enabled);
  SbBool isDepthReadbackEnabled(void) const;
  void readbackRGBA(std::vector<uint8_t> & outPixels) const;
  /**
   * Borrow the last synchronous RGBA buffer without copying it.
   * Returns NULL and sets byteCount to zero when no synchronous pixels are
   * available. The pointer belongs to this target; it is invalidated by the
   * next render, resize, output-policy change or target destruction. Copy it
   * if pixels must outlive that operation. This is an experimental API only.
   */
  const uint8_t * borrowRGBA(std::size_t & byteCount) const;
  void readbackDepth(std::vector<float> & outDepth) const;
  /**
   * Polls a ticket produced by SoWgpuRenderAction::applyAsync().
   *
   * The original action and target need not remain alive. Outputs change
   * together only on READBACK_READY, which also consumes the ticket.
   */
  static ReadbackStatus pollReadback(const SoWgpuReadbackTicket & ticket,
                                     std::vector<uint8_t> & outColor,
                                     std::vector<float> & outDepth,
                                     SbString * diagnostic = nullptr);
  static SbBool cancelReadback(const SoWgpuReadbackTicket & ticket);
  uint64_t getLastSubmissionSerial(void) const;
  SbBool getCacheTelemetry(SoWgpuCacheTelemetry & outTelemetry) const;
  void pollDevice(void);

private:
  SoWgpuRenderTarget(void);
  SbPimplPtr<SoWgpuRenderTargetP> pimpl;
  friend class SoWgpuRenderAction;
  friend class SoWgpuRenderActionP;
  friend class SoWgpuRenderTargetP;
#ifdef COIN_INTERNAL
public:
  SbPimplPtr<SoWgpuRenderTargetP> & getPimpl(void) { return this->pimpl; }
  const SbPimplPtr<SoWgpuRenderTargetP> & getPimpl(void) const { return this->pimpl; }
#endif
};

#endif // !COIN_SOWGPURENDERTARGET_H
