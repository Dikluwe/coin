#ifndef COIN_RENDER_SCENE_MANAGER_H
#define COIN_RENDER_SCENE_MANAGER_H

#include <Inventor/CoinRenderExport.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/SbVec2i32.h>
#include <Inventor/SbColor4f.h>
#include <Inventor/SbString.h>

class SoNode;
class SoCamera;
class SbViewportRegion;
class CoinRenderTarget;
struct CoinRenderOptions;
struct CoinRenderNativeSurfaceDescriptor;
struct CoinRenderReadbackTicket;

/** Small experimental scene/target owner, not a SoRenderManager replacement.
 * The caller owns the native window and handles its events. For a window,
 * destroy this manager before destroying the native window/display. Call
 * SoDB::init() and CoinRenderAction::initClass() before construction.
 *
 * \see coin_render_experimental
 */
class COIN_RENDER_DLL_API CoinRenderSceneManager {
public:
  explicit CoinRenderSceneManager(const SbVec2i32 & offscreenSize);
  CoinRenderSceneManager(const CoinRenderNativeSurfaceDescriptor & nativeWindow,
                     const SbVec2i32 & framebufferSize);
  // Explicit immutable options are forwarded unchanged to the owned target.
  CoinRenderSceneManager(const SbVec2i32 & offscreenSize, const CoinRenderOptions & options);
  CoinRenderSceneManager(const CoinRenderNativeSurfaceDescriptor & nativeWindow,
                        const SbVec2i32 & framebufferSize, const CoinRenderOptions & options);
  ~CoinRenderSceneManager();

  CoinRenderSceneManager(const CoinRenderSceneManager &) = delete;
  CoinRenderSceneManager & operator=(const CoinRenderSceneManager &) = delete;

  void setSceneGraph(SoNode * root);
  SoNode * getSceneGraph() const;
  CoinRenderTarget * getRenderTarget() const;
  void setViewportRegion(const SbViewportRegion & viewport);
  void setBackgroundColor(const SbColor4f & color);
  void setTransparencyType(CoinRenderAction::TransparencyType type);
  CoinRenderAction::TransparencyType getTransparencyType() const;
  void setDrawMode(CoinRenderAction::DrawMode mode);
  CoinRenderAction::DrawMode getDrawMode() const;
  void setOverlayColor(const SbColor4f & color);
  const SbColor4f & getOverlayColor() const;
  // Requested multipass count: 1..16, default 1. Validation occurs at render/apply.
  void setNumPasses(int passes);
  int getNumPasses() const;
  // Extra multipass attachments/accumulation budget, default 256 MiB.
  void setMultipassBufferBudget(uint64_t bytes);
  uint64_t getMultipassBufferBudget() const;
  void setTexturesEnabled(SbBool enabled);
  SbBool isTexturesEnabled() const;
  void clearManagerLayerBoundaries();
  void addManagerLayerBoundary(SoNode * separator, SbBool clearDepth,
                               int transparencyType = -1);
  SbBool resize(const SbVec2i32 & framebufferSize);
  CoinRenderAction::Status render();
  // Manager stereo: capture both eyes and submit one composed frame.
  CoinRenderAction::Status renderAnaglyph(SoCamera * camera, float stereoOffset);
  CoinRenderAction::Status renderAsync(CoinRenderReadbackTicket & ticket);
  CoinRenderAction::Status getLastStatus() const;
  const SbString & getLastError() const;

private:
  struct P;
  P * pimpl;
};

#endif // COIN_RENDER_SCENE_MANAGER_H
