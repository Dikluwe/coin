#include <Inventor/SoDB.h>
#include <Inventor/rendering/SoWgpuCapabilities.h>
#include <Inventor/rendering/SoWgpuSceneManager.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>

int main() {
  SoDB::init();
  SoWgpuRenderAction::initClass();

  CoinWgpuExperimentalCapabilities caps{};
  if (coin_wgpu_experimental_query_capabilities(
        COIN_WGPU_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps)) != 0 ||
      caps.version != COIN_WGPU_CAPABILITIES_VERSION) return 1;
  if (!caps.gpu_available && caps.backend != COIN_WGPU_EXPERIMENTAL_RECORDING)
    return 0;

  SoSeparator * root = new SoSeparator;
  root->ref();
  SoWgpuSceneManager manager(SbVec2i32(4, 4));
  manager.setSceneGraph(root);
  root->unref();
  if (manager.render() != SoWgpuRenderAction::SUCCESS) return 2;
  return manager.getRenderTarget()->getStatus() ==
    SoWgpuRenderTarget::TARGET_READY ? 0 : 3;
}
