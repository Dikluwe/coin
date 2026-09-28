#ifndef COIN_SOBGFXRENDERACTION_H
#define COIN_SOBGFXRENDERACTION_H

#include <Inventor/actions/CoinRenderAction.h>

/** Experimental BGFX action (Vulkan/OpenGL).
 * Uses the shared Coin frame capture implementation. The distinct registered
 * action type identifies BGFX correctly; CoinRenderAction remains available
 * to the Rust/wgpu backend and to existing experimental clients.
 */
class COIN_RENDER_DLL_API SoBGFXRenderAction : public CoinRenderAction {
  typedef CoinRenderAction inherited;
  SO_ACTION_HEADER(SoBGFXRenderAction);
public:
  static void initClass();
  static SbBool isGpuBackendAvailable();
  SoBGFXRenderAction();
  explicit SoBGFXRenderAction(const SbViewportRegion & viewport);
  ~SoBGFXRenderAction() override;
};

#endif
