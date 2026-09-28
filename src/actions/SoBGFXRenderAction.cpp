#include "config.h"
#include <Inventor/actions/SoBGFXRenderAction.h>
#include <Inventor/rendering/SoWgpuCapabilities.h>
#include "actions/SoSubActionP.h"

SO_ACTION_SOURCE(SoBGFXRenderAction);

void SoBGFXRenderAction::initClass() {
  if (inherited::getClassTypeId().isBad()) inherited::initClass();
  SO_ACTION_INTERNAL_INIT_CLASS(SoBGFXRenderAction, SoWgpuRenderAction);
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
  CoinWgpuExperimentalCapabilities caps{};
  caps.struct_size = sizeof(caps);
  return coin_wgpu_experimental_query_capabilities(
    COIN_WGPU_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps)) == 0 && caps.gpu_available;
}
