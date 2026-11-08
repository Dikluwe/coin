#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoComplexity.h>
#include <memory>
#include <fstream>
#include <iostream>
#include <vector>
int main(int argc,char **argv){
 if(argc!=3){std::cerr<<"Usage: render OUTPUT_DIRECTORY STUDIO.cibl\n";return 4;}
 SoDB::init();CoinRenderAction::initClass();
 for(int i=0;i<8;i++){
  CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;
  if(i==7)options.pbrEnvironmentBundle=argv[2];
  auto target=std::unique_ptr<CoinRenderTarget>(CoinRenderTarget::createOffscreen(SbVec2i32(360,320),options));
  if(!target)return 1;
  auto root=new SoSeparator;root->ref();
  auto camera=new SoOrthographicCamera;camera->position.setValue(0,0,5);camera->height=2.65;camera->nearDistance=1;camera->farDistance=10;root->addChild(camera);
  auto model=new SoLightModel;model->model=i==0?SoLightModel::BASE_COLOR:SoLightModel::PHONG;root->addChild(model);
  auto quality=new SoComplexity;quality->value=1;root->addChild(quality);
  auto light=new SoDirectionalLight;light->direction.setValue(-.35,-.55,-1);light->intensity=1;root->addChild(light);
  if(i<2){auto mat=new SoMaterial;mat->diffuseColor.setValue(.025,.18,.55);mat->specularColor.setValue(.6,.6,.6);mat->shininess=.65;root->addChild(mat);}
  else {auto mat=new SoPBRMaterial;mat->baseColor=i<4?SbColor(.025,.18,.55):SbColor(1,.71,.29);mat->metallic=i<4?0:1;mat->roughness=i==3||i==5?.75:(i>=6?.2:.12);root->addChild(mat);}
  root->addChild(new SoSphere);
  CoinRenderAction action(SbViewportRegion(360,320));action.setRenderTarget(target.get());action.apply(root);
  if(action.getLastStatus()!=CoinRenderAction::SUCCESS){std::cerr<<action.getLastError().getString();return 2;}
  std::vector<uint8_t> data;target->readbackRGBA(data);if(data.size()!=360*320*4)return 3;
  std::ofstream out(std::string(argv[1])+"/"+std::to_string(i)+".rgba",std::ios::binary);out.write((char*)data.data(),data.size());
  action.setRenderTarget(nullptr);root->unref();std::cout<<i<<" OK\n";
 }
}
