#include "config.h"
#include <Inventor/actions/CoinBgfxAction.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include "actions/SoSubActionP.h"

SO_ACTION_SOURCE(CoinBgfxAction);

void CoinBgfxAction::initClass() {
  if (inherited::getClassTypeId().isBad()) inherited::initClass();
  SO_ACTION_INTERNAL_INIT_CLASS(CoinBgfxAction, CoinRenderAction);
}

CoinBgfxAction::CoinBgfxAction() : inherited() {
  SO_ACTION_CONSTRUCTOR(CoinBgfxAction);
}
CoinBgfxAction::CoinBgfxAction(const SbViewportRegion & viewport)
  : inherited(viewport) {
  SO_ACTION_CONSTRUCTOR(CoinBgfxAction);
}
CoinBgfxAction::~CoinBgfxAction() = default;

SbBool CoinBgfxAction::isGpuBackendAvailable() {
  CoinRenderCapabilities caps{};
  caps.struct_size = sizeof(caps);
  return coin_render_query_capabilities(
    COIN_RENDER_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps)) == 0 && caps.gpu_available;
}
