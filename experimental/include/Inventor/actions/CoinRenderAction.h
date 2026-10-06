#ifndef COIN_RENDER_ACTION_H
#define COIN_RENDER_ACTION_H

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

#include <Inventor/CoinRenderExport.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/SbColor4f.h>
#include <Inventor/SbString.h>
#include <Inventor/tools/SbPimplPtr.h>

class CoinRenderActionP;
class CoinRenderTarget;
struct CoinRenderReadbackTicket;

/**
 * Experimental Open Inventor action that renders a Coin scene through the selected backend.
 *
 * The action preserves the SoAction traversal model and captures SoState into
 * a private frame plan consumed by CoinRender. It does not add
 * symbols to libCoin. A render target must be assigned before apply().
 *
 * \ingroup coin_actions
 * \see coin_render_experimental
 * \see CoinRenderTarget
 */
class COIN_RENDER_DLL_API CoinRenderAction : public SoCallbackAction {
  typedef SoCallbackAction inherited;
  SO_ACTION_HEADER(CoinRenderAction);

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

  enum TransparencyType {
    SCREEN_DOOR = SoGLRenderAction::SCREEN_DOOR,
    ADD = SoGLRenderAction::ADD,
    DELAYED_ADD = SoGLRenderAction::DELAYED_ADD,
    SORTED_OBJECT_ADD = SoGLRenderAction::SORTED_OBJECT_ADD,
    BLEND = SoGLRenderAction::BLEND,
    DELAYED_BLEND = SoGLRenderAction::DELAYED_BLEND,
    SORTED_OBJECT_BLEND = SoGLRenderAction::SORTED_OBJECT_BLEND,
    SORTED_OBJECT_SORTED_TRIANGLE_ADD =
      SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD,
    SORTED_OBJECT_SORTED_TRIANGLE_BLEND =
      SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND,
    NONE = SoGLRenderAction::NONE,
    SORTED_LAYERS_BLEND = SoGLRenderAction::SORTED_LAYERS_BLEND
  };

  static void initClass(void);
  static SbBool isGpuBackendAvailable(void);

  /**
   * Defer the current callback path to the shared depth-tested annotation pass.
   * Returns FALSE during replay (or outside apply); callers then traverse normally.
   * Lower priorities render first, preserving traversal order for ties.
   */
  SbBool deferAnnotation(int priority = 0);
  /** Normal delayed overlay pass, before depth-tested 3D annotations.
   * A supplied path is copied; nullptr uses the current callback path.
   */
  SbBool deferOverlayPath(SoPath * path = nullptr, int priority = 0);
  /** Viewer foreground pass: preserve depth state, do not clear depth. */
  void beginForegroundPass();
  void endForegroundPass();

  CoinRenderAction(void);
  CoinRenderAction(const SbViewportRegion & viewport);
  ~CoinRenderAction(void) override;

  void setViewportRegion(const SbViewportRegion & region);
  const SbViewportRegion & getViewportRegion(void) const;

  /** Borrow a render target. Before destroying a target that will be replaced,
   * detach it with setRenderTarget(NULL) while the old target is still alive.
   */
  void setRenderTarget(CoinRenderTarget * target);
  CoinRenderTarget * getRenderTarget(void) const;

  void setBackgroundColor(const SbColor4f & color);
  const SbColor4f & getBackgroundColor(void) const;

  void setTransparencyType(TransparencyType type);
  TransparencyType getTransparencyType(void) const;

  /** Experimental peeling profile: 1..8 layers, default 4. Invalid requests
   * are rejected at apply() without changing the published frame. */
  void setSortedLayersNumPasses(int passes);
  int getSortedLayersNumPasses(void) const;
  /** Conservative per-frame attachment budget; default 256 MiB. */
  void setTransparencyBufferBudget(uint64_t bytes);
  uint64_t getTransparencyBufferBudget(void) const;

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
   * On SUCCESS, use CoinRenderTarget::pollReadback() or
   * CoinRenderTarget::cancelReadback(). The ticket does not depend on this
   * action or its target remaining alive.
   */
  void applyAsync(SoNode * root, CoinRenderReadbackTicket & outTicket);
  void apply(SoPath * path) override;
  void apply(const SoPathList & pathlist, SbBool obeysrules = FALSE) override;

#ifdef COIN_INTERNAL
  SbPimplPtr<CoinRenderActionP> & getPimpl() { return this->pimpl; }
#endif

protected:
  void beginTraversal(SoNode * root) override;

private:
  SbPimplPtr<CoinRenderActionP> pimpl;
  friend class CoinRenderActionP;
};

#endif // !COIN_RENDER_ACTION_H
