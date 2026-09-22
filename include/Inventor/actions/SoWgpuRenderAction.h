#ifndef COIN_SOWGPURENDERACTION_H
#define COIN_SOWGPURENDERACTION_H

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

#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/SbColor4f.h>
#include <Inventor/SbString.h>
#include <Inventor/tools/SbPimplPtr.h>

class SoWgpuRenderActionP;
class SoWgpuRenderTarget;

class COIN_DLL_API SoWgpuRenderAction : public SoCallbackAction {
  typedef SoCallbackAction inherited;
  SO_ACTION_HEADER(SoWgpuRenderAction);

public:
  enum Status {
    SUCCESS = 0,
    NO_TARGET,
    NOT_READY,
    INVALID_SCENE,
    UNSUPPORTED,
    OUT_OF_MEMORY,
    DEVICE_LOST,
    BACKEND_ERROR
  };

  static void initClass(void);
  static SbBool isGpuBackendAvailable(void);

  SoWgpuRenderAction(void);
  SoWgpuRenderAction(const SbViewportRegion & viewport);
  ~SoWgpuRenderAction(void) override;

  void setViewportRegion(const SbViewportRegion & region);
  const SbViewportRegion & getViewportRegion(void) const;

  void setRenderTarget(SoWgpuRenderTarget * target);
  SoWgpuRenderTarget * getRenderTarget(void) const;

  void setBackgroundColor(const SbColor4f & color);
  const SbColor4f & getBackgroundColor(void) const;

  Status getLastStatus(void) const;
  const SbString & getLastError(void) const;

  const SbString & getRecordingLog(void) const;

  void apply(SoNode * root) override;
  void apply(SoPath * path) override;
  void apply(const SoPathList & pathlist, SbBool obeysrules = FALSE) override;

protected:
  void beginTraversal(SoNode * root) override;

private:
  SbPimplPtr<SoWgpuRenderActionP> pimpl;
  friend class SoWgpuRenderActionP;
};

#endif // !COIN_SOWGPURENDERACTION_H
