#ifndef COIN_SOWGPUSCENEMANAGER_H
#define COIN_SOWGPUSCENEMANAGER_H

#include <Inventor/CoinWgpuExport.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/SbVec2i32.h>
#include <Inventor/SbColor4f.h>
#include <Inventor/SbString.h>

class SoNode;
class SoWgpuRenderTarget;
struct SoWgpuNativeSurfaceDescriptor;
struct SoWgpuReadbackTicket;

/** Small experimental scene/target owner, not a SoRenderManager replacement.
 * The caller owns the native window and handles its events. For a window,
 * destroy this manager before destroying the native window/display. Call
 * SoDB::init() and SoWgpuRenderAction::initClass() before construction.
 *
 * \see coin_wgpu_experimental
 */
class COIN_WGPU_DLL_API SoWgpuSceneManager {
public:
  explicit SoWgpuSceneManager(const SbVec2i32 & offscreenSize);
  SoWgpuSceneManager(const SoWgpuNativeSurfaceDescriptor & nativeWindow,
                     const SbVec2i32 & framebufferSize);
  ~SoWgpuSceneManager();

  SoWgpuSceneManager(const SoWgpuSceneManager &) = delete;
  SoWgpuSceneManager & operator=(const SoWgpuSceneManager &) = delete;

  void setSceneGraph(SoNode * root);
  SoNode * getSceneGraph() const;
  SoWgpuRenderTarget * getRenderTarget() const;
  void setBackgroundColor(const SbColor4f & color);
  SbBool resize(const SbVec2i32 & framebufferSize);
  SoWgpuRenderAction::Status render();
  SoWgpuRenderAction::Status renderAsync(SoWgpuReadbackTicket & ticket);
  SoWgpuRenderAction::Status getLastStatus() const;
  const SbString & getLastError() const;

private:
  struct P;
  P * pimpl;
};

#endif // COIN_SOWGPUSCENEMANAGER_H
