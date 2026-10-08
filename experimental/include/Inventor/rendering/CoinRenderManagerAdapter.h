#ifndef COIN_RENDER_MANAGER_ADAPTER_H
#define COIN_RENDER_MANAGER_ADAPTER_H

#include <Inventor/CoinRenderExport.h>
#include <Inventor/SbBasic.h>
#include <Inventor/SbString.h>
#include <Inventor/SbVec2i32.h>
#include <Inventor/actions/CoinRenderAction.h>

class SoNode;
class SoRenderManager;
class CoinRenderSceneManager;
struct CoinRenderOptions;
struct CoinRenderNativeSurfaceDescriptor;

/**
 * Adapter that renders the public state of an SoRenderManager through CoinRender.
 *
 * A Qt/FreeCAD viewport keeps owning its native window and SoRenderManager.
 * The owner updates the source viewport region before forwarding framebuffer
 * resize events to resize(). render() runs source prepareFrame/finishFrame,
 * including sensors, callbacks, autoclip and animation, without applying a GL
 * render action. Destroy the adapter before the native window or display.
 *
 * The adapter intentionally has no Qt dependency. Framebuffer sizes are in
 * device pixels, so Qt callers must apply devicePixelRatio().
 */
class COIN_RENDER_DLL_API CoinRenderManagerAdapter {
public:
  CoinRenderManagerAdapter(SoRenderManager & source,
                             const SbVec2i32 & offscreenSize);
  CoinRenderManagerAdapter(SoRenderManager & source,
                             const CoinRenderNativeSurfaceDescriptor & nativeWindow,
                             const SbVec2i32 & framebufferSize);
  // Hosts select target policy explicitly at adapter construction.
  CoinRenderManagerAdapter(SoRenderManager & source, const SbVec2i32 & offscreenSize,
                          const CoinRenderOptions & options);
  CoinRenderManagerAdapter(SoRenderManager & source,
                          const CoinRenderNativeSurfaceDescriptor & nativeWindow,
                          const SbVec2i32 & framebufferSize, const CoinRenderOptions & options);
  ~CoinRenderManagerAdapter();

  CoinRenderManagerAdapter(const CoinRenderManagerAdapter &) = delete;
  CoinRenderManagerAdapter & operator=(const CoinRenderManagerAdapter &) = delete;

  // Use a host-composed root (for example model + HUD overlays). Passing
  // nullptr restores the SoRenderManager scene graph.
  void setSceneGraphOverride(SoNode * sceneGraph);

  SbBool syncFromRenderManager();
  SbBool resize(const SbVec2i32 & framebufferSize);
  CoinRenderAction::Status render();

  CoinRenderSceneManager * getSceneManager() const;
  CoinRenderAction::Status getLastStatus() const;
  const SbString & getLastError() const;

private:
  struct P;
  P * pimpl;
};

#endif // COIN_RENDER_MANAGER_ADAPTER_H
