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

#include <Inventor/CoinWgpuExport.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/SbColor4f.h>
#include <Inventor/SbString.h>
#include <Inventor/tools/SbPimplPtr.h>

class SoWgpuRenderActionP;
class SoWgpuRenderTarget;
struct SoWgpuReadbackTicket;

/**
 * Experimental Open Inventor action that renders a Coin scene with WebGPU.
 *
 * The action preserves the SoAction traversal model and captures SoState into
 * a private frame plan consumed by CoinWgpuExperimental. It does not add
 * symbols to libCoin. A render target must be assigned before apply().
 *
 * \ingroup coin_actions
 * \see coin_wgpu_experimental
 * \see SoWgpuRenderTarget
 */
class COIN_WGPU_DLL_API SoWgpuRenderAction : public SoCallbackAction {
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
    BACKEND_ERROR,
    SURFACE_LOST
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

  void setFastPathEnabled(SbBool enable);
  SbBool isFastPathEnabled(void) const;

  Status getLastStatus(void) const;
  const SbString & getLastError(void) const;

  const SbString & getRecordingLog(void) const;

  using SoAction::apply;

  void apply(SoNode * root) override;
  /**
   * Submits an offscreen frame without waiting for GPU readback.
   *
   * On SUCCESS, use SoWgpuRenderTarget::pollReadback() or
   * SoWgpuRenderTarget::cancelReadback(). The ticket does not depend on this
   * action or its target remaining alive.
   */
  void applyAsync(SoNode * root, SoWgpuReadbackTicket & outTicket);
  void apply(SoPath * path) override;
  void apply(const SoPathList & pathlist, SbBool obeysrules = FALSE) override;

protected:
  void beginTraversal(SoNode * root) override;

private:
  SbPimplPtr<SoWgpuRenderActionP> pimpl;
  friend class SoWgpuRenderActionP;
};

#endif // !COIN_SOWGPURENDERACTION_H
