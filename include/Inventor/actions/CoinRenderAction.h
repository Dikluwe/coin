#ifndef COIN_RENDER_ACTION_H
#define COIN_RENDER_ACTION_H
#define COIN_RENDER_ACTION_OPTIONS_VERSION 1

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
class SoShaderProgram;
struct CoinRenderShaderModule;
struct CoinRenderReadbackTicket;

/**
 * Experimental Open Inventor action that renders a Coin scene through the selected backend.
 *
 * The action preserves the SoAction traversal model and captures SoState into
 * a private frame plan consumed by CoinRender. Its private VRML background
 * bridge is compiled into libCoin when CoinRender is enabled; rebuild libCoin
 * and CoinRender together when changing this internal bridge. Synchronous
 * apply() without a target records the captured frame. Asynchronous apply
 * requires an offscreen target.
 *
 * VRML97 capture follows native Coin's static raster semantics: Shape and
 * Appearance state, local decoded ImageTexture/PixelTexture images, texture
 * transforms, native lights, fog, and background sky/ground/local faces.
 * Captures read current VRML state and image bytes on every apply; mutable
 * image storage and background generation are not proved fresh by node IDs.
 * MovieTexture has no native visual implementation. Viewpoint and
 * NavigationInfo leave the application camera/viewer unchanged, as in native
 * GLRender. Audio, sensors, interpolators, Script and metadata can supply
 * snapshots through the existing library but have no direct raster output;
 * this action does not introduce a viewer event or audio runtime.
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
    SURFACE_LOST,
    ABORTED
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

  enum TransparentDelayedObjectRenderType { ONE_PASS, NONSOLID_SEPARATE_BACKFACE_PASS };
  /** Two-sided object transparency: backs before fronts for each sorted object;
   * unsorted delayed lists render all backs before all fronts. SOLID objects
   * retain one front pass. Opaque, immediate and OIT rendering are unchanged.
   */
  void setTransparentDelayedObjectRenderType(TransparentDelayedObjectRenderType type);
  TransparentDelayedObjectRenderType getTransparentDelayedObjectRenderType() const;
  enum AbortCode { CONTINUE, ABORT, PRUNE, DELAY };
  typedef AbortCode CoinRenderAbortCB(void * userdata);
  typedef void CoinRenderPreRenderCB(void * userdata, CoinRenderAction * action);
  enum SortedObjectOrderStrategy {
    BBOX_CENTER, BBOX_CLOSEST_CORNER, BBOX_FARTHEST_CORNER, CUSTOM_CALLBACK
  };
  typedef float CoinRenderSortedObjectOrderCB(void * userdata, CoinRenderAction * action);

  enum class DrawMode { SCENE, WIREFRAME, POINTS, BOUNDING_BOX, WIREFRAME_OVERLAY, HIDDEN_LINE, SHADED_HIDDEN_LINES };

  static void initClass(void);
  static SbBool isGpuBackendAvailable(void);

  /** Explicit host delegation from a SoText2/SoImage subclass callback.
   * Prepare fields using callback state first, then call this instead of the
   * inherited primitive callback. Observes its primitives once in a local state
   * scope; does not invoke GL or reenter the subclass callback.
   * FALSE outside apply; failure during capture rejects the candidate frame.
   */
  SbBool captureScreenContent(SoNode * node);

  /** Reject the candidate from a host callback when its semantics exceed the
   * portable profile. Inert outside apply; preserves the previous publication.
   */
  SbBool rejectUnsupported(const SbString & reason);
  /** Reject malformed authored state during capture without publishing it. */
  SbBool rejectInvalidScene(const SbString & reason);

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
  /** A host overlay may clear depth in the viewport of its first draw.
   * Foreground scopes can nest; closing one restores its enclosing pass.
   */
  void beginForegroundPass(SbBool clearDepth);
  void endForegroundPass();
  /** Manager adapter only: mark an owned separator as a foreground pass.
   * The action borrows the pointer for the next apply; the scene owns it.
   */
  void clearManagerLayerBoundaries();
  void addManagerLayerBoundary(SoNode * separator, SbBool clearDepth,
                               int transparencyType = -1);

  CoinRenderAction(void);
  CoinRenderAction(const SbViewportRegion & viewport);
  ~CoinRenderAction(void) override;

  void setViewportRegion(const SbViewportRegion & region);
  const SbViewportRegion & getViewportRegion(void) const;

  /** Check each traversed node with its current path. PRUNE skips the subtree;
   * DELAY copies its path and replays it once as an overlay. During replay,
   * DELAY continues normally. ABORT discards the candidate before submission.
   * abortNow() is inert outside apply and returns TRUE for PRUNE/DELAY/ABORT.
   */
  void setAbortCallback(CoinRenderAbortCB * callback, void * userdata);
  void getAbortCallback(CoinRenderAbortCB * & callback, void * & userdata) const;
  SbBool abortNow();
  SbBool addDelayedPath(SoPath * path);
  SbBool isRenderingDelayedPaths() const;
  /** Run once before capture for each valid outer apply, including cache hits.
   * A registration snapshot is used: list edits take effect next apply.
   * Removal erases matching callback/userdata pairs; NULL callbacks are ignored.
   */
  void addPreRenderCallback(CoinRenderPreRenderCB * callback, void * userdata);
  void removePreRenderCallback(CoinRenderPreRenderCB * callback, void * userdata);
  /** Normalized bottom-left canvas update rectangle, default (0,0)/(1,1).
   * Validated at apply: finite coordinates in [0,1], origin+size <= 1.
   * A partial update clips clear and rendering to its viewport intersection,
   * preserving previous exterior color/depth (initially background/depth 1).
   * Pixels follow native truncation: int(origin*canvas), int(size*canvas),
   * independently, followed by viewport intersection. Initial profile:
   * synchronous offscreen RGBA8, one sample/pass, ordinary object rendering.
   * Spatial shaders, peeling and stippled transparent partial updates are
   * rejected before producer execution; use a full update for these profiles.
   * Empty intersections succeed without traversal, producers or publication.
   */
  void setUpdateArea(const SbVec2f & origin, const SbVec2f & size);
  /** Recreate traversal state and revoke capture proofs on the next apply.
   * Target pixels, readback tickets and backend resources remain published.
   * During traversal the reset is deferred until that apply unwinds safely.
   */
  void invalidateState() override;
  void getUpdateArea(SbVec2f & origin, SbVec2f & size) const;
  /** Object transparency sorting uses native camera-plane distance, descending,
   * with stable occurrence ties. Custom callbacks run once per sorted transparent
   * occurrence with its current path and must return a finite distance.
   * Other transparency algorithms do not consume this strategy.
   */
  void setSortedObjectOrderStrategy(SortedObjectOrderStrategy strategy,
    CoinRenderSortedObjectOrderCB * callback = nullptr, void * userdata = nullptr);
  /** Portable area coverage for unlit, untextured lines and points. Lines use
   * continuous-width rectangles and points continuous-diameter discs; coverage
   * multiplies alpha before blending. This is independent of MSAA/multipass.
   * Initial profile is mono synchronous offscreen RGBA8 at one sample/pass.
   * Triangles keep their normal coverage; FALSE restores native stroke rules.
   */
  void setSmoothing(SbBool enabled);
  SbBool isSmoothing() const;
  /** Default FALSE: transparent delayed/sorted objects suppress depth writes
   * unless an authored SoDepthBuffer overrides them, as in native Coin.
   * TRUE keeps the normal captured depth-write default; depth tests are unchanged.
   */
  void setDelayedObjDepthWrite(SbBool enabled);
  SbBool getDelayedObjDepthWrite() const;

  /** Borrow a render target. Before destroying a target that will be replaced,
   * detach it with setRenderTarget(NULL) while the old target is still alive.
   */
  void setRenderTarget(CoinRenderTarget * target);
  CoinRenderTarget * getRenderTarget(void) const;

  void setBackgroundColor(const SbColor4f & color);
  const SbColor4f & getBackgroundColor(void) const;

  void setTransparencyType(TransparencyType type);
  TransparencyType getTransparencyType(void) const;
  void setDrawMode(DrawMode mode);
  DrawMode getDrawMode(void) const;
  void setOverlayColor(const SbColor4f & color);
  const SbColor4f & getOverlayColor(void) const;
  typedef void CoinRenderPassCB(void * userdata, CoinRenderAction * action);
  /** A callback requests distinct scene snapshots per sample. It runs N-1 times
   * between captures, seeing the previous sample index. Nodes see their current
   * index. All snapshots validate before submission; GPU output is their mean,
   * with the last sample's depth. Readback during callbacks is the previous
   * publication; this callback never exposes an implicit GL context or presents
   * intermediate images. Initial profile: opaque geometry and local RGBA2D,
   * mono synchronous offscreen RGBA8, one hardware sample.
   */
  void setPassCallback(CoinRenderPassCB * callback, void * userdata);
  int getCurPass() const;
  /** Select one external jitter sample without averaging or pass callbacks.
   * Requires 0 <= pass < total <= 16. setNumPasses restores internal sampling.
   */
  void setCurPass(int pass, int total);
  // Requested multipass count: 1..16, default 1. Validation occurs at render/apply.
  void setNumPasses(int passes);
  int getNumPasses() const;
  // Extra multipass attachments/accumulation budget, default 256 MiB.
  void setMultipassBufferBudget(uint64_t bytes);
  uint64_t getMultipassBufferBudget() const;
  void setTexturesEnabled(SbBool enabled);
  SbBool isTexturesEnabled(void) const;

  /** Experimental peeling profile: 1..8 layers, default 4. Invalid requests
   * are rejected at apply() without changing the published frame. */
  void setSortedLayersNumPasses(int passes);
  int getSortedLayersNumPasses(void) const;
  /** Conservative per-frame attachment budget; default 256 MiB. */
  void setTransparencyBufferBudget(uint64_t bytes);
  uint64_t getTransparencyBufferBudget(void) const;

  SbBool setPortableShaderModule(SoShaderProgram *program, const CoinRenderShaderModule &module);
  void clearPortableShaderModule(SoShaderProgram *program);
  void clearPortableShaderModules();

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
