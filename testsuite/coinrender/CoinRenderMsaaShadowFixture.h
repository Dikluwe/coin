#ifndef COIN_RENDER_MSAA_SHADOW_FIXTURE_H
#define COIN_RENDER_MSAA_SHADOW_FIXTURE_H

#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace msaa_shadow_test {
constexpr int side=128;
inline bool check(bool yes,const std::string &label){
  if(!yes)std::cerr<<"msaa_shadow FAIL "<<label<<'\n';
  return yes;
}
inline CoinRenderCapabilities capabilities(uint32_t kind){
  CoinRenderCapabilities result{};
  if(coin_render_query_capabilities(kind,&result,sizeof(result))!=0)
    result.gpu_available=false;
  std::cout<<"msaa_shadow backend="<<result.backend
           <<" renderer="<<result.renderer<<" vendor="<<result.vendor_id
           <<" adapter="<<result.adapter_name
           <<" gpu="<<result.gpu_available<<'\n';
  return result;
}
inline CoinRenderOptions options(const CoinRenderCapabilities &caps,unsigned samples,
  CoinRenderSceneTextureMode route=COIN_RENDER_SCENE_TEXTURE_STAGED){
  CoinRenderOptions result;result.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  result.sampleCount=samples;result.sceneTexture=route;return result;
}
struct Frame{
  std::vector<uint8_t> rgba;
  std::vector<float> depth;
  uint64_t serial=0;
};
inline size_t at(int x,int y,int width=side){return size_t(y)*width+x;}
struct Scene {
  SoSeparator *root=new SoSeparator;
  SoShadowGroup *group=new SoShadowGroup;
  SoSeparator *receiver=nullptr;
  SoMaterial *receiverMaterial=new SoMaterial;
  std::vector<SoShadowSpotLight *> spots;
  std::vector<SoShadowDirectionalLight *> directionals;
  std::vector<SoTranslation *> casterPositions;
  std::vector<SoSeparator *> mapScenes;
  int slots;
  bool directional;
  explicit Scene(int count=1,bool useDirectional=false):slots(count),directional(useDirectional){
    root->ref();
    auto *camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,6);camera->height=4;
    camera->nearDistance=1;camera->farDistance=20;root->addChild(camera);
    auto *model=new SoLightModel;model->model=SoLightModel::PHONG;
    root->addChild(model);
    group->quality=.2f;group->precision=.125f;root->addChild(group);
    for(int i=0;i<slots;++i){
      const float x=slots==1?0.f:(float(i)-float(slots-1)/2.f)*.34f;
      auto *map=new SoSeparator;
      mapScenes.push_back(map);
      auto *style=new SoShadowStyle;style->style=SoShadowStyle::CASTS_SHADOW;
      map->addChild(style);
      auto *position=new SoTranslation;
      position->translation.setValue(x,0,.35f);
      map->addChild(position);casterPositions.push_back(position);
      auto *cube=new SoCube;cube->width=.48f;cube->height=.48f;
      cube->depth=.48f;map->addChild(cube);
      if(directional){
        auto *light=new SoShadowDirectionalLight;
        light->direction.setValue(0,0,-1);
        light->bboxCenter.setValue(0,0,0);light->bboxSize.setValue(4,4,8);
        light->intensity=.6f/float(slots);light->shadowMapScene=map;
        directionals.push_back(light);group->addChild(light);
      }else{
        auto *light=new SoShadowSpotLight;
        light->location.setValue(0,0,5);light->direction.setValue(0,0,-1);
        light->cutOffAngle=.6f;light->nearDistance=1;light->farDistance=10;
        light->intensity=.6f/float(slots);light->shadowMapScene=map;
        spots.push_back(light);group->addChild(light);
      }
    }
    receiver=new SoSeparator;group->addChild(receiver);
    auto *style=new SoShadowStyle;style->style=SoShadowStyle::SHADOWED;
    receiver->addChild(style);
    receiverMaterial->diffuseColor.setValue(.72f,.75f,.78f);
    receiverMaterial->ambientColor.setValue(.18f,.18f,.18f);
    receiverMaterial->specularColor.setValue(0,0,0);
    receiver->addChild(receiverMaterial);
    auto *coords=new SoCoordinate3;
    // The sloped upper-left edge exposes k/N coverage levels independently
    // of the shadow map, while the centre remains a fully covered witness.
    const SbVec3f corners[]={SbVec3f(-1.8f,-1.8f,-1),
      SbVec3f(1.8f,-1.8f,-1),SbVec3f(1.8f,1.8f,-1),
      SbVec3f(-1.8f,1.4f,-1)};
    coords->point.setValues(0,4,corners);receiver->addChild(coords);
    auto *face=new SoFaceSet;face->numVertices.set1Value(0,4);
    receiver->addChild(face);
  }
  ~Scene(){root->unref();}
  void lightOn(int slot,bool enabled){
    if(directional)directionals[size_t(slot)]->on=enabled;
    else spots[size_t(slot)]->on=enabled;
  }
  void casterX(int slot,float x){
    casterPositions[size_t(slot)]->translation.setValue(x,0,.35f);
  }
};
struct Harness {
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderAction action{SbViewportRegion(side,side)};
  Harness(const CoinRenderCapabilities &caps,unsigned samples){
    target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options(caps,samples)));
    if(target){target->setDepthReadbackEnabled(TRUE);action.setRenderTarget(target.get());}
    action.setBackgroundColor(SbColor4f(0,0,0,0));
  }
  ~Harness(){action.setRenderTarget(nullptr);}
  bool render(SoNode *scene,Frame &frame,const char *label,
              CoinRenderAction::TransparencyType transparency=CoinRenderAction::NONE){
    if(!check(bool(target),"offscreen target exists"))return false;
    action.setTransparencyType(transparency);action.apply(scene);
    target->readbackRGBA(frame.rgba);target->readbackDepth(frame.depth);
    frame.serial=target->getLastSubmissionSerial();
    std::cout<<"msaa_shadow phase="<<label
             <<" status="<<int(action.getLastStatus())
             <<" rgba="<<frame.rgba.size()<<" depth="<<frame.depth.size()
             <<" serial="<<frame.serial
             <<" error="<<action.getLastError().getString()<<'\n';
    return check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
      frame.rgba.size()==size_t(side)*side*4 &&
      frame.depth.size()==size_t(side)*side && frame.serial>0,label);
  }
};
inline unsigned differentRgb(const Frame &a,const Frame &b,int x0=8,int x1=120){
  if(a.rgba.size()!=b.rgba.size() || a.rgba.size()!=size_t(side)*side*4)return 0;
  unsigned changed=0;
  for(int y=8;y<120;++y)for(int x=x0;x<x1;++x){
    const size_t i=at(x,y)*4;
    int delta=0;for(int c=0;c<3;++c)
      delta+=std::abs(int(a.rgba[i+c])-int(b.rgba[i+c]));
    changed+=(delta>9);
  }
  return changed;
}
}
#endif
