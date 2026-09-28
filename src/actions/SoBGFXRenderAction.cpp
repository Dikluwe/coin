#include "config.h"
#include <Inventor/actions/SoBGFXRenderAction.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include "actions/SoSubActionP.h"

SO_ACTION_SOURCE(SoBGFXRenderAction);

void SoBGFXRenderAction::initClass() {
  if (inherited::getClassTypeId().isBad()) inherited::initClass();
  SO_ACTION_INTERNAL_INIT_CLASS(SoBGFXRenderAction, CoinRenderAction);
}

SoBGFXRenderAction::SoBGFXRenderAction() : inherited() {
  SO_ACTION_CONSTRUCTOR(SoBGFXRenderAction);
}
SoBGFXRenderAction::SoBGFXRenderAction(const SbViewportRegion & viewport)
  : inherited(viewport) {
  SO_ACTION_CONSTRUCTOR(SoBGFXRenderAction);
}
SoBGFXRenderAction::~SoBGFXRenderAction() = default;

SbBool SoBGFXRenderAction::isGpuBackendAvailable() {
  CoinRenderCapabilities caps{};
  caps.struct_size = sizeof(caps);
  return coin_render_query_capabilities(
    COIN_RENDER_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps)) == 0 && caps.gpu_available;
}
