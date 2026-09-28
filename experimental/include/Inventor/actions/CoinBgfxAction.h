#ifndef COIN_BGFX_ACTION_H
#define COIN_BGFX_ACTION_H

#include <Inventor/actions/CoinRenderAction.h>

/** Experimental BGFX action (Vulkan/OpenGL).
 * Uses the shared Coin frame capture implementation. The distinct registered
 * action type identifies BGFX correctly; CoinRenderAction remains available
 * to the Rust/wgpu backend and to existing experimental clients.
 */
class COIN_RENDER_DLL_API CoinBgfxAction : public CoinRenderAction {
  typedef CoinRenderAction inherited;
  SO_ACTION_HEADER(CoinBgfxAction);
public:
  static void initClass();
  static SbBool isGpuBackendAvailable();
  CoinBgfxAction();
  explicit CoinBgfxAction(const SbViewportRegion & viewport);
  ~CoinBgfxAction() override;
};

#endif
