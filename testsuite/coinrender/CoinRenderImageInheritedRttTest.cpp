#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr int side=64;
bool check(bool good,const char * label) {
  if(!good) std::cerr << "CoinRenderImageInheritedRttTest: " << label << '\n';
  return good;
}
SoSeparator * square(float x0,float y0,float x1,float y1,const SbColor & color) {
  auto * group=new SoSeparator;
  auto * material=new SoMaterial;material->diffuseColor.setValue(color);group->addChild(material);
  auto * coords=new SoCoordinate3;
  const SbVec3f vertices[]={{x0,y0,0},{x1,y0,0},{x1,y1,0},{x0,y1,0}};
  coords->point.setValues(0,4,vertices);group->addChild(coords);
  auto * face=new SoFaceSet;face->numVertices.set1Value(0,4);group->addChild(face);
  return group;
}
struct Scene {
  Scene() {
    producer=new SoSeparator;producer->ref();
    auto * camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,3);camera->height=2;
    camera->nearDistance=.1f;camera->farDistance=10;producer->addChild(camera);
    auto * light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;producer->addChild(light);
    producer->addChild(square(-1,-1,0,0,SbColor(220.f/255,30.f/255,40.f/255)));
    lowerRight=square(0,-1,1,0,SbColor(30.f/255,210.f/255,50.f/255));
    producer->addChild(lowerRight);
    producer->addChild(square(-1,0,0,1,SbColor(40.f/255,50.f/255,230.f/255)));
    producer->addChild(square(0,0,1,1,SbColor(210.f/255,180.f/255,30.f/255)));
    consumer=new SoSeparator;consumer->ref();
    auto * viewer=new SoOrthographicCamera;
    viewer->position.setValue(0,0,10);viewer->height=side;
    viewer->nearDistance=1;viewer->farDistance=30;consumer->addChild(viewer);
    auto * unlit=new SoLightModel;unlit->model=SoLightModel::BASE_COLOR;consumer->addChild(unlit);
    auto * quality=new SoComplexity;quality->textureQuality=.1f;consumer->addChild(quality);
    texture=new SoSceneTexture2;
    texture->scene=producer;texture->size.setValue(32,32);
    texture->type=SoSceneTexture2::RGBA8;
    texture->model=SoSceneTexture2::REPLACE;
    texture->transparencyFunction=SoSceneTexture2::NONE;
    texture->backgroundColor.setValue(0,0,0,1);
    texture->wrapS=SoSceneTexture2::CLAMP;
    texture->wrapT=SoSceneTexture2::CLAMP;
    consumer->addChild(texture);
    matrix=new SoTextureMatrixTransform;consumer->addChild(matrix);
    image=new SoImage;image->horAlignment=SoImage::CENTER;image->vertAlignment=SoImage::HALF;
    std::vector<unsigned char> white(16*16*3,255);
    image->image.setValue(SbVec2s(16,16),3,white.data());consumer->addChild(image);
  }
  ~Scene(){consumer->unref();producer->unref();}
  void coordinate(float s,float t,float q=1) {
    SbMatrix value=SbMatrix::identity();
    value[3][0]=s;value[3][1]=t;value[3][3]=q;
    matrix->matrix=value;
  }
  SoSeparator * producer=nullptr,*consumer=nullptr,*lowerRight=nullptr;
  SoSceneTexture2 * texture=nullptr;
  SoTextureMatrixTransform * matrix=nullptr;
  SoImage * image=nullptr;
};
struct Frame {std::vector<uint8_t> rgba;std::vector<float> depth;uint64_t serial=0;};
bool caseOne(bool direct) {
  CoinRenderCapabilities caps{};
  const bool available=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
    &caps,sizeof(caps))==0 && caps.gpu_available &&
    caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN;
  if(!check(available,"real GPU adapter and renderer required")) return false;
  const auto requestedRenderer=static_cast<CoinRenderRenderer>(caps.renderer);
  CoinRenderOptions options{};
  if(options.renderer==COIN_RENDER_RENDERER_UNKNOWN) options.renderer=requestedRenderer;
  options.sceneTexture=direct?COIN_RENDER_SCENE_TEXTURE_DIRECT:COIN_RENDER_SCENE_TEXTURE_STAGED;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
  if(!check(bool(target) && target->getOptions().renderer==requestedRenderer,
    "RTT destination uses queried renderer")) return false;
  target->setDepthReadbackEnabled(TRUE);
  Scene scene;
  CoinRenderAction action(SbViewportRegion(side,side));
  action.setRenderTarget(target.get());
  action.setBackgroundColor(SbColor4f(0,0,0,1));
  std::cout << "image_inherited_rtt direct=" << direct << " backend=" << caps.backend
            << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
            << " adapter=" << caps.adapter_name << " gpu_available=" << available << '\n';
  if(!check(available,"real GPU adapter required")) {action.setRenderTarget(nullptr);return false;}
  const auto capture=[&](Frame & frame,const char * label) {
    action.apply(scene.consumer);
    target->readbackRGBA(frame.rgba);target->readbackDepth(frame.depth);
    frame.serial=target->getLastSubmissionSerial();
    const bool good=action.getLastStatus()==CoinRenderAction::SUCCESS &&
      frame.rgba.size()==size_t(side*side*4) && frame.depth.size()==size_t(side*side);
    if(!good) std::cerr << label << " status=" << int(action.getLastStatus())
                        << " error=" << action.getLastError().getString() << '\n';
    return check(good,label);
  };
  const auto center=[](const Frame & frame,const unsigned char rgb[3],const char * label) {
    const size_t at=(size_t(32)*side+32)*4;
    if(frame.rgba.size()<at+4) {
      std::cerr << label << " center unavailable rgba_bytes=" << frame.rgba.size() << '\n';
      return false;
    }
    bool good=true;
    for(int channel=0;channel<3;++channel)
      good &= std::abs(int(frame.rgba[at+channel])-int(rgb[channel]))<=3;
    std::cout << label << " center_rgb=" << int(frame.rgba[at]) << ','
              << int(frame.rgba[at+1]) << ',' << int(frame.rgba[at+2])
              << " expected_rgb=" << int(rgb[0]) << ',' << int(rgb[1]) << ',' << int(rgb[2])
              << " within3=" << good << '\n';
    return good;
  };
  const unsigned char red[]={220,30,40},green[]={30,210,50},blue[]={40,50,230};
  Frame lowerLeft,lowerRight,upperLeft,rejected,recovered,mutated,restoredProducer;
  scene.coordinate(0,0);
  const bool firstCaptured=capture(lowerLeft,"RTT lower-left");
  bool ok=firstCaptured && center(lowerLeft,red,"RTT lower-left") &&
    check(lowerLeft.serial>0,"RTT first publication advances serial");
  scene.coordinate(.75f,0);
  const bool secondCaptured=capture(lowerRight,"RTT lower-right");
  ok &= secondCaptured && center(lowerRight,green,"RTT lower-right") &&
        (!firstCaptured || lowerRight.serial>lowerLeft.serial);
  scene.coordinate(0,.75f);
  const bool thirdCaptured=capture(upperLeft,"RTT upper-left");
  ok &= thirdCaptured && center(upperLeft,blue,"RTT upper-left") &&
        (!secondCaptured || upperLeft.serial>lowerRight.serial);
  scene.coordinate(.75f,0,2);
  const bool fourthCaptured=capture(recovered,"RTT projective Q");
  ok &= fourthCaptured && center(recovered,red,"RTT projective Q");
  const Frame stable=recovered;
  SbMatrix bad=SbMatrix::identity();bad[3][0]=.75f;bad[3][3]=0;
  scene.matrix->matrix=bad;
  action.apply(scene.consumer);
  target->readbackRGBA(rejected.rgba);target->readbackDepth(rejected.depth);
  rejected.serial=target->getLastSubmissionSerial();
  ok &= check(fourthCaptured && action.getLastStatus()==CoinRenderAction::INVALID_SCENE &&
    rejected.rgba==stable.rgba && rejected.depth==stable.depth && rejected.serial==stable.serial,
    "invalid Q preserves RTT RGBA, depth and serial");
  scene.coordinate(.75f,0,2);
  const bool recoveredCaptured=capture(recovered,"RTT recovered");
  ok &= recoveredCaptured && fourthCaptured && recovered.rgba==stable.rgba &&
        recovered.depth==stable.depth && recovered.serial>stable.serial;
  auto * material=static_cast<SoMaterial *>(scene.lowerRight->getChild(0));
  material->diffuseColor.setValue(240.f/255,130.f/255,20.f/255);
  scene.coordinate(.75f,0);
  const unsigned char changed[]={240,130,20};
  const bool mutationCaptured=capture(mutated,"RTT producer mutation");
  ok &= mutationCaptured && center(mutated,changed,"RTT producer mutation") &&
        (!secondCaptured || mutated.rgba!=lowerRight.rgba) &&
        (!recoveredCaptured || mutated.serial>recovered.serial);
  material->diffuseColor.setValue(30.f/255,210.f/255,50.f/255);
  const bool restoredCaptured=capture(restoredProducer,"RTT producer restored");
  ok &= check(restoredCaptured && secondCaptured && mutationCaptured &&
    restoredProducer.rgba==lowerRight.rgba &&
    restoredProducer.depth==lowerRight.depth &&
    restoredProducer.serial>mutated.serial,
    "RTT producer restoration recovers exact RGBA and depth with a new serial");
  action.setRenderTarget(nullptr);
  return ok;
}
}
int main(int argc,char ** argv) {
  bool direct=false;
  for(int index=1;index<argc;++index) {
    const std::string option=argv[index];
    if(option=="--direct") direct=true;
    else if(option=="--staged") direct=false;
    else {std::cerr << "Usage: CoinRenderImageInheritedRttTest --staged|--direct\n";return 2;}
  }
  SoDB::init();CoinRenderAction::initClass();
  return caseOne(direct)?0:1;
}
