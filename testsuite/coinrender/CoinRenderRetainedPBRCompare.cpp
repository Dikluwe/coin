#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderOptions.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoSpotLight.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoClipPlane.h>
#ifdef COIN_RETAINED_PBR_COMPARE
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#endif
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cmath>
#include <chrono>

int main(int argc,char **argv) {
  if(argc!=2){std::cerr<<"Output directory required\n";return 1;}
  SoDB::init();CoinRenderAction::initClass();
  CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(96,96),options));
  if(!target){std::cerr<<"Target unavailable\n";return 1;}
  std::ofstream manifest(std::string(argv[1])+"/frames.tsv");
  int index=0;
  for(int perspective=0;perspective<2;++perspective)for(int kind=0;kind<3;++kind)
    for(float metallic:{0.f,.5f,1.f})for(float roughness:{.045f,.35f,1.f}) {
      auto *root=new SoSeparator;root->ref();
      SoCamera * sceneCamera=nullptr;
      if(perspective){auto *camera=new SoPerspectiveCamera;camera->position.setValue(0,0,5);
        camera->nearDistance=1;camera->farDistance=10;camera->heightAngle=.65f;sceneCamera=camera;root->addChild(camera);}
      else{auto *camera=new SoOrthographicCamera;camera->position.setValue(0,0,5);
        camera->nearDistance=1;camera->farDistance=10;camera->height=3.5f;sceneCamera=camera;root->addChild(camera);}
      auto *lightModel=new SoLightModel;lightModel->model=SoLightModel::PHONG;root->addChild(lightModel);
      if(kind==0){auto *light=new SoDirectionalLight;light->direction.setValue(.3f,.1f,-1);
        light->intensity=.8f;root->addChild(light);}
      if(kind==1){auto *light=new SoPointLight;light->location.setValue(.5f,1,3);light->intensity=.8f;root->addChild(light);}
      if(kind==2){auto *light=new SoSpotLight;light->location.setValue(0,0,3);light->direction.setValue(0,0,-1);
        light->cutOffAngle=.7f;light->dropOffRate=.2f;light->intensity=.8f;root->addChild(light);}
      auto *material=new SoPBRMaterial;material->baseColor.setValue(.6f,.2f,.08f);
      material->metallic=metallic;material->roughness=roughness;
      material->emissive.setValue(kind==2?2.f:.01f,.02f,.04f);
      material->clearcoat=metallic==.5f?.6f:0;material->clearcoatRoughness=.25f;
      root->addChild(material);
      auto *transform=new SoTransform;transform->rotation.setValue(SbVec3f(0,1,0),.2f);
      transform->scaleFactor.setValue(1,.8f,1.2f);root->addChild(transform);
      auto *clip=new SoClipPlane;clip->plane.setValue(SbPlane(SbVec3f(1,0,0),-.7f));root->addChild(clip);
      auto *binding=new SoNormalBinding;binding->value=SoNormalBinding::OVERALL;root->addChild(binding);
      auto *normal=new SoNormal;normal->vector.setValue(0,0,1);root->addChild(normal);
      auto *points=new SoCoordinate3;SbVec3f p[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),SbVec3f(1,1,0),SbVec3f(-1,1,0)};
      points->point.setValues(0,4,p);root->addChild(points);auto *face=new SoFaceSet;face->numVertices=4;root->addChild(face);
#ifdef COIN_RETAINED_PBR_COMPARE
      CoinRenderRetainedRenderer renderer(SbViewportRegion(96,96));
      auto render=[&](){auto result=renderer.render(root,*target);
        if(result!=CoinRenderAction::SUCCESS)std::cerr<<renderer.getLastError().getString()<<'\n';return result;};
#else
      CoinRenderAction renderer(SbViewportRegion(96,96));renderer.setRenderTarget(target.get());
      auto render=[&](){renderer.apply(root);if(renderer.getLastStatus()!=CoinRenderAction::SUCCESS)
        std::cerr<<renderer.getLastError().getString()<<'\n';return renderer.getLastStatus();};
#endif
      const auto start=std::chrono::steady_clock::now();
      if(render()!=CoinRenderAction::SUCCESS)return 2;
      const auto first=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
      std::vector<uint8_t> pixels;std::vector<float> depth;target->readbackRGBA(pixels);target->readbackDepth(depth);
      if(pixels.size()!=96*96*4||depth.size()!=96*96)return 3;
      bool covered=false;for(float z:depth){if(!std::isfinite(z))return 4;if(z<1)covered=true;}if(!covered)return 5;
      const auto serial=target->getLastSubmissionSerial();
      if(render()!=CoinRenderAction::SUCCESS||target->getLastSubmissionSerial()<=serial)return 6;
      std::vector<uint8_t> repeated;target->readbackRGBA(repeated);if(repeated!=pixels)return 7;
#ifdef COIN_RETAINED_PBR_COMPARE
      if(renderer.getCaptureCount()!=1||renderer.getPlanBuildCount()!=1)return 8;
#endif
      const auto save=[&](const std::string & suffix,const void *data,size_t bytes){std::ofstream file(std::string(argv[1])+"/"+std::to_string(index)+suffix,std::ios::binary);file.write(static_cast<const char *>(data),bytes);};
      save(".rgba",pixels.data(),pixels.size());save(".depth",depth.data(),depth.size()*sizeof(float));
      material->roughness=1.f-roughness*.5f;
      material->baseColor.setValue(.1f,.5f,.7f);material->emissive.setValue(.5f,.25f,2);
      if(render()!=CoinRenderAction::SUCCESS)return 9;
      target->readbackRGBA(pixels);target->readbackDepth(depth);
      save("-edit.rgba",pixels.data(),pixels.size());save("-edit.depth",depth.data(),depth.size()*sizeof(float));
#ifdef COIN_RETAINED_PBR_COMPARE
      if(renderer.getCaptureCount()!=1||renderer.getPlanBuildCount()!=1)return 10;
#endif
      if(perspective)static_cast<SoPerspectiveCamera *>(sceneCamera)->heightAngle=.72f;
      else static_cast<SoOrthographicCamera *>(sceneCamera)->height=4;
      if(render()!=CoinRenderAction::SUCCESS)return 11;
      target->readbackRGBA(pixels);target->readbackDepth(depth);
      save("-projection.rgba",pixels.data(),pixels.size());save("-projection.depth",depth.data(),depth.size()*sizeof(float));
#ifdef COIN_RETAINED_PBR_COMPARE
      if(renderer.getCaptureCount()!=1||renderer.getPlanBuildCount()!=1)return 12;
      const auto previousSerial=target->getLastSubmissionSerial();
      material->anisotropy=1.5f; // Invalid factor still preserves the previous publication.
      if(renderer.render(root,*target)!=CoinRenderAction::UNSUPPORTED)return 13;
      target->readbackRGBA(repeated);
      if(repeated!=pixels||target->getLastSubmissionSerial()!=previousSerial)return 14;
      material->anisotropy=0;lightModel->model=SoLightModel::BASE_COLOR;
      if(renderer.render(root,*target)!=CoinRenderAction::UNSUPPORTED)return 15;
      target->readbackRGBA(repeated);
      if(repeated!=pixels||target->getLastSubmissionSerial()!=previousSerial)return 16;
#endif
      manifest<<index<<'\t'<<perspective<<'\t'<<kind<<'\t'<<metallic<<'\t'<<roughness<<'\t'<<first<<'\n';
      ++index;
#ifndef COIN_RETAINED_PBR_COMPARE
      renderer.setRenderTarget(nullptr);
#endif
      root->unref();
    }
  std::cout<<"PBR comparison captures: "<<index<<" PASS\n";return 0;
}
