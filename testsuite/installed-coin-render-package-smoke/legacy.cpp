#include <Inventor/SoDB.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/rendering/SoWgpuSceneManager.h>
#include <Inventor/rendering/SoWgpuRenderManagerAdapter.h>
#include <Inventor/rendering/SoWgpuCapabilities.h>
#include <type_traits>

static_assert(std::is_same<SoWgpuRenderAction, CoinRenderAction>::value, "legacy action forwards");
static_assert(std::is_same<SoWgpuRenderTarget, CoinRenderTarget>::value, "legacy target forwards");
static_assert(std::is_same<CoinWgpuExperimentalCapabilities, CoinRenderCapabilities>::value,
              "legacy capabilities forward");
int main() {
  SoDB::init();
  SoWgpuRenderAction::initClass();
  SoWgpuRenderAction action;
  action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
  return action.getRenderTarget() == nullptr ? 0 : 1;
}
