#ifndef COIN_SOWGPURENDERTARGET_H
#define COIN_SOWGPURENDERTARGET_H

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
#include <Inventor/SbVec2i32.h>
#include <Inventor/tools/SbPimplPtr.h>
#include <Inventor/rendering/SoWgpuNativeSurface.h>

class SoWgpuRenderTargetP;
class SoWgpuRenderActionP;

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

class COIN_DLL_API SoWgpuRenderTarget {
public:
  typedef struct ::SoWgpuCacheTelemetry SoWgpuCacheTelemetry;
  enum Status {
    TARGET_READY = 0,
    TARGET_NOT_READY,
    TARGET_LOST,
    TARGET_ERROR,
    TARGET_SURFACE_LOST
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
  void readbackRGBA(std::vector<uint8_t> & outPixels) const;
  void readbackDepth(std::vector<float> & outDepth) const;
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
