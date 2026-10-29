#include "config.h"
#include <Inventor/SoDB.h>
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoCube.h>
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinbgfx/CoinBgfxBackend.h"
#include <fstream>
#include <iostream>
#include <memory>
#include <cstdio>
#include <vector>
int main(int argc,char **argv) {
  if(argc!=3)return 1;
  SoDB::init();
  const std::string path=std::string(argv[2])+"/invalid.cibl";
  {std::ofstream out(path,std::ios::binary);out<<"invalid IBL";}
  CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;options.pbrEnvironmentBundle=path.c_str();
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(96,96),options));
  if(!target)return 2;
  auto *root=new SoSeparator;root->ref();
  auto *camera=new SoOrthographicCamera;camera->position.setValue(0,0,5);camera->height=3;
  camera->nearDistance=1;camera->farDistance=10;root->addChild(camera);
  auto *model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;root->addChild(model);root->addChild(new SoCube);
  CoinRenderRetainedRenderer renderer(SbViewportRegion(96,96));
  bool ok=renderer.render(root,*target)==CoinRenderAction::SUCCESS;
  std::vector<uint8_t> previous,current;target->readbackRGBA(previous);
  const auto serial=target->getLastSubmissionSerial();
  model->model=SoLightModel::PHONG;root->insertChild(new SoPBRMaterial,2);
  ok &= renderer.render(root,*target)==CoinRenderAction::UNSUPPORTED;
  target->readbackRGBA(current);
  ok &= current==previous && target->getLastSubmissionSerial()==serial && !target->getPimpl()->pbrEnvironmentSnapshot;
  {std::ifstream input(argv[1],std::ios::binary);std::ofstream output(path,std::ios::binary);output<<input.rdbuf();ok &= bool(output);}
  ok &= renderer.render(root,*target)==CoinRenderAction::SUCCESS;
  ok &= bool(target->getPimpl()->pbrEnvironmentSnapshot);
  ok &= static_cast<CoinBgfxBackend *>(target->getPimpl()->backend.get())->getPbrEnvironmentUploadCount()==1;
  ok &= std::remove(path.c_str())==0;
  const auto captures=renderer.getCaptureCount(),plans=renderer.getPlanBuildCount();
  ok &= renderer.render(root,*target)==CoinRenderAction::SUCCESS;
  ok &= renderer.getCaptureCount()==captures && renderer.getPlanBuildCount()==plans;
  options.pbrEnvironmentBundle=argv[1];options.pbrEnvironmentBudgetBytes=1;
  std::unique_ptr<CoinRenderTarget> limited(CoinRenderTarget::createOffscreen(SbVec2i32(96,96),options));
  ok &= bool(limited);
  if(limited){ok &= renderer.render(root,*limited)==CoinRenderAction::UNSUPPORTED;ok &= limited->getLastSubmissionSerial()==0;}
  root->unref();
  std::cout<<"IBL admission, retry, frozen source and GPU budget: "<<(ok?"PASS":"FAIL")<<'\n';return ok?0:1;
}
