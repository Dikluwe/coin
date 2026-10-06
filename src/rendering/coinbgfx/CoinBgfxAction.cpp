#include "config.h"
#include <Inventor/actions/CoinBgfxAction.h>
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
  return inherited::isGpuBackendAvailable();
}
