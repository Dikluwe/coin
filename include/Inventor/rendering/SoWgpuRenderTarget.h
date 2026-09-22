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
 * \"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
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
#include <Inventor/SbVec2i32.h>
#include <Inventor/tools/SbPimplPtr.h>

class SoWgpuRenderTargetP;
class SoWgpuRenderActionP;

class COIN_DLL_API SoWgpuRenderTarget {
public:
  enum Status {
    TARGET_READY = 0,
    TARGET_NOT_READY,
    TARGET_LOST,
    TARGET_ERROR
  };

  static SoWgpuRenderTarget * createOffscreen(const SbVec2i32 & size);
  ~SoWgpuRenderTarget(void);

  Status getStatus(void) const;
  const SbVec2i32 & getSize(void) const;
  SbBool resize(const SbVec2i32 & size);

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
