#ifndef COIN_BGFX_ACTION_H
#define COIN_BGFX_ACTION_H

#include <Inventor/actions/CoinRenderAction.h>

/** Compatibility action retaining the registered CoinBgfxAction type.
 * New integrations use CoinRenderAction with the selected target backend.
 * Capture, GPU availability and execution are inherited from the common action.
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
