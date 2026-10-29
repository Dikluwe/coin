#ifndef COIN_RENDER_PBR_GALLERY_SCENES_H
#define COIN_RENDER_PBR_GALLERY_SCENES_H
// Real BGFX PBR examples. Compile with COIN_PBR_GALLERY_CURRENT for the independent callback control.
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderOptions.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#ifndef COIN_PBR_GALLERY_CURRENT
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#endif
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoCylinder.h>
#include <Inventor/nodes/SoCone.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>

namespace {
const int width=1200, height=840;
void require(bool value,const char * reason) { if(!value) throw std::runtime_error(reason); }
void save(const std::string & path,const void * data,size_t bytes) {
  std::ofstream file(path,std::ios::binary);file.write(static_cast<const char *>(data),bytes);
  require(bool(file),"Could not write gallery output");
}
void light(SoSeparator * root,const SbVec3f & direction,const SbColor & color,float intensity) {
  auto * node=new SoDirectionalLight;node->direction=direction;node->color=color;
  node->intensity=intensity;root->addChild(node);
}
SoPBRMaterial * material(const SbColor & color,float metallic,float roughness,float coat=0) {
  auto * node=new SoPBRMaterial;node->baseColor=color;node->metallic=metallic;
  node->roughness=roughness;node->clearcoat=coat;node->clearcoatRoughness=.15f;return node;
}
SoSeparator * scene(bool cad, bool direct=true) {
  auto * root=new SoSeparator;
  if(cad) {
    auto * camera=new SoPerspectiveCamera;camera->position.setValue(5,4,13);
    camera->pointAt(SbVec3f(0,0,0));camera->heightAngle=.65f;
    camera->nearDistance=1;camera->farDistance=40;root->addChild(camera);
  } else {
    auto * camera=new SoOrthographicCamera;camera->position.setValue(0,0,20);
    camera->height=8.8f;camera->nearDistance=1;camera->farDistance=40;root->addChild(camera);
  }
  auto * model=new SoLightModel;model->model=SoLightModel::PHONG;root->addChild(model);
  auto * quality=new SoComplexity;quality->value=1.f;root->addChild(quality);
  if(direct) {
  light(root,SbVec3f(-.35f,-.55f,-1),SbColor(1,.92f,.82f),1);
  light(root,SbVec3f(.8f,-.1f,-.6f),SbColor(.55f,.72f,1),.8f);
  light(root,SbVec3f(.1f,.9f,-.35f),SbColor(1,1,1),.6f);
  }
  if(!cad) {
    auto * sphere=new SoSphere;sphere->radius=.88f;
    const float roughness[]={.08f,.25f,.5f,.85f};
    for(int row=0;row<3;++row)for(int column=0;column<4;++column) {
      auto * part=new SoSeparator;auto * position=new SoTranslation;
      position->translation.setValue((column-1.5f)*2.8f,(1-row)*2.6f,0);part->addChild(position);
      const SbColor color=row==0?SbColor(.025f,.18f,.55f):row==1?SbColor(1,.71f,.29f):SbColor(.55f,.025f,.018f);
      part->addChild(material(color,row==1?1:0,roughness[column],row==2?1:0));
      part->addChild(sphere);root->addChild(part);
    }
  } else {
    for(int i=0;i<4;++i) {
      auto * part=new SoSeparator;auto * transform=new SoTransform;
      transform->translation.setValue((i-1.5f)*2.4f,0,0);transform->rotation.setValue(SbVec3f(0,1,0),.25f);
      part->addChild(transform);
      if(i==0) {
        part->addChild(material(SbColor(.955f,.638f,.538f),1,.5f));
        auto * cube=new SoCube;cube->width=1.7f;cube->height=1.7f;cube->depth=1.7f;part->addChild(cube);
      } else if(i==1) {
        part->addChild(material(SbColor(.025f,.18f,.55f),0,.3f));
        auto * cylinder=new SoCylinder;cylinder->radius=.8f;cylinder->height=2;part->addChild(cylinder);
      } else if(i==2) {
        part->addChild(material(SbColor(.55f,.025f,.018f),0,.3f,1));
        auto * sphere=new SoSphere;sphere->radius=1;part->addChild(sphere);
      } else {
        part->addChild(material(SbColor(.8f,.82f,.85f),1,.6f));
        auto * cone=new SoCone;cone->bottomRadius=.9f;cone->height=2;part->addChild(cone);
      }
      root->addChild(part);
    }
  }
  return root;
}
}

#endif
