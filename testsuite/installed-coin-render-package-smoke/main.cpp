#include <Inventor/SoDB.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>

int main() {
  SoDB::init();
  CoinRenderAction::initClass();

  CoinRenderCapabilities caps{};
  if (coin_render_query_capabilities(
        COIN_RENDER_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps)) != 0 ||
      caps.version != COIN_RENDER_CAPABILITIES_VERSION) return 1;
  if (!caps.gpu_available && caps.backend != COIN_RENDER_EXPERIMENTAL_RECORDING)
    return 0;

  SoSeparator * root = new SoSeparator;
  root->ref();
  CoinRenderSceneManager manager(SbVec2i32(4, 4));
  manager.setSceneGraph(root);
  root->unref();
  if (manager.render() != CoinRenderAction::SUCCESS) return 2;
  return manager.getRenderTarget()->getStatus() ==
    CoinRenderTarget::TARGET_READY ? 0 : 3;
}
