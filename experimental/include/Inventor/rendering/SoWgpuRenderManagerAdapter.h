#ifndef COIN_SOWGPURENDERMANAGERADAPTER_H
#define COIN_SOWGPURENDERMANAGERADAPTER_H

#include <Inventor/CoinWgpuExport.h>
#include <Inventor/SbBasic.h>
#include <Inventor/SbString.h>
#include <Inventor/SbVec2i32.h>
#include <Inventor/actions/SoWgpuRenderAction.h>

class SoNode;
class SoRenderManager;
class SoWgpuSceneManager;
struct SoWgpuNativeSurfaceDescriptor;

/**
 * Adapter that renders the public state of an SoRenderManager through WGPU.
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
class COIN_WGPU_DLL_API SoWgpuRenderManagerAdapter {
public:
  SoWgpuRenderManagerAdapter(SoRenderManager & source,
                             const SbVec2i32 & offscreenSize);
  SoWgpuRenderManagerAdapter(SoRenderManager & source,
                             const SoWgpuNativeSurfaceDescriptor & nativeWindow,
                             const SbVec2i32 & framebufferSize);
  ~SoWgpuRenderManagerAdapter();

  SoWgpuRenderManagerAdapter(const SoWgpuRenderManagerAdapter &) = delete;
  SoWgpuRenderManagerAdapter & operator=(const SoWgpuRenderManagerAdapter &) = delete;

  // Use a host-composed root (for example model + HUD overlays). Passing
  // nullptr restores the SoRenderManager scene graph.
  void setSceneGraphOverride(SoNode * sceneGraph);

  SbBool syncFromRenderManager();
  SbBool resize(const SbVec2i32 & framebufferSize);
  SoWgpuRenderAction::Status render();

  SoWgpuSceneManager * getSceneManager() const;
  SoWgpuRenderAction::Status getLastStatus() const;
  const SbString & getLastError() const;

private:
  struct P;
  P * pimpl;
};

#endif // COIN_SOWGPURENDERMANAGERADAPTER_H
