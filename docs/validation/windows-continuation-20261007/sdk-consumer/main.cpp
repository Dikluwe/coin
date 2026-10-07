#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/system/gl.h>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

static void inspectGl(void *, SoAction * action) {
  if (!action->isOfType(SoGLRenderAction::getClassTypeId())) return;
  GLint coords=0, samplers=0;
  glGetIntegerv(GL_MAX_TEXTURE_COORDS_ARB, &coords);
  glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS_ARB, &samplers);
  std::cout << "CoinGL renderer=" << glGetString(GL_RENDERER)
            << " texture_coords=" << coords << " fragment_samplers=" << samplers << '\n';
}

int main(int argc, char ** argv) {
  if (argc != 2) return 2;
  CoinRenderRenderer renderer;
  if (!std::strcmp(argv[1], "d3d12")) renderer=COIN_RENDER_RENDERER_D3D12;
  else if (!std::strcmp(argv[1], "vulkan")) renderer=COIN_RENDER_RENDERER_VULKAN;
  else if (!std::strcmp(argv[1], "opengl")) renderer=COIN_RENDER_RENDERER_OPENGL;
  else return 2;
  SoDB::init(); CoinRenderAction::initClass();
  CoinRenderCapabilities caps{};
  if (coin_render_query_capabilities_for_renderer(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
      renderer, &caps, sizeof(caps)) != 0 || !caps.gpu_available || caps.renderer != renderer) return 3;
  CoinRenderOptions options; options.renderer=renderer;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(32,32), options));
  if (!target || target->getStatus()!=CoinRenderTarget::TARGET_READY) return 4;
  auto * root=new SoSeparator; root->ref();
  auto * camera=new SoOrthographicCamera; camera->position.setValue(0,0,4); camera->height=3;
  root->addChild(camera);
  auto * model=new SoLightModel; model->model=SoLightModel::BASE_COLOR; root->addChild(model);
  auto * material=new SoMaterial; material->diffuseColor.setValue(1,0,0); root->addChild(material);
  auto * callback=new SoCallback; callback->setCallback(inspectGl); root->addChild(callback);
  root->addChild(new SoCube);
  CoinRenderAction action(SbViewportRegion(32,32)); action.setRenderTarget(target.get()); action.apply(root);
  std::vector<uint8_t> pixels; target->readbackRGBA(pixels);
  bool ok=action.getLastStatus()==CoinRenderAction::SUCCESS && pixels.size()==32*32*4;
  const size_t center=(16*32+16)*4;
  if (ok) ok=pixels[center]>200 && pixels[center+1]<5 && pixels[center+2]<5;
  SoOffscreenRenderer legacy(SbViewportRegion(32,32));
  ok=legacy.render(root) && legacy.getBuffer() && ok;
  std::cout << "installed_sdk renderer=" << caps.renderer << " vendor_id=0x" << std::hex
            << caps.vendor_id << " device_id=0x" << caps.device_id << std::dec
            << " adapter=" << caps.adapter_name << " pixels_ok=" << ok << '\n';
  root->unref();
  return ok ? 0 : 5;
}
