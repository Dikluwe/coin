#ifndef COIN_RENDER_SCENE_MANAGER_H
#define COIN_RENDER_SCENE_MANAGER_H

#include <Inventor/CoinRenderExport.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/SbVec2i32.h>
#include <Inventor/SbColor4f.h>
#include <Inventor/SbString.h>

class SoNode;
class SbViewportRegion;
class CoinRenderTarget;
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
  SbBool resize(const SbVec2i32 & framebufferSize);
  CoinRenderAction::Status render();
  CoinRenderAction::Status renderAsync(CoinRenderReadbackTicket & ticket);
  CoinRenderAction::Status getLastStatus() const;
  const SbString & getLastError() const;

private:
  struct P;
  P * pimpl;
};

#endif // COIN_RENDER_SCENE_MANAGER_H
