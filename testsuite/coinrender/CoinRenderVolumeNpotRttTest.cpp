#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoTexture3.h>
#include <Inventor/nodes/SoTextureCoordinate3.h>
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
#include "CoinRenderVolumeSrgbOracle.h"

namespace {
bool srgb=false;
const std::array<uint8_t,3> encoded={{96,160,224}};
unsigned char resultByte(unsigned char primary,unsigned c){return srgb?uint8_t(std::lround(primary*volume_srgb_oracle::decode(encoded[c]))):primary;}

constexpr int side=64;
bool check(bool good,const char * label) {
  if(!good) std::cerr << "CoinRenderVolumeNpotRttTest: " << label << '\n';
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
    volume=new SoTexture3;volume->model=SoTexture3::MODULATE;
    volume->wrapS=SoTexture3::CLAMP;volume->wrapT=SoTexture3::CLAMP;volume->wrapR=SoTexture3::CLAMP;
    setVolume(false);producer->addChild(volume);
    auto* uvw=new SoTextureCoordinate3;const SbVec3f selected(.5f/3,.5f/5,6.5f/7),samples[]={selected,selected,selected,selected};uvw->point.setValues(0,4,samples);producer->addChild(uvw);
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
  void setVolume(bool invalid) {
    if(invalid){std::vector<uint8_t> bad(257*4,255);volume->images.setValue(SbVec3s(257,1,1),4,bad.data());return;}
    std::vector<uint8_t> data(3*5*7*4,0);for(size_t a=3;a<data.size();a+=4)data[a]=255;
    for(int c=0;c<4;++c)data[size_t(6*5*3)*4+c]=srgb&&c<3?encoded[c]:255;
    volume->images.setValue(SbVec3s(3,5,7),4,data.data());
  }
  SoTexture3* volume=nullptr;
  SoSeparator * producer=nullptr,*consumer=nullptr,*lowerRight=nullptr;
  SoSceneTexture2 * texture=nullptr;
  SoTextureMatrixTransform * matrix=nullptr;
  SoImage * image=nullptr;
};
struct Frame {std::vector<uint8_t> rgba;std::vector<float> depth;uint64_t serial=0;};
bool caseOne(bool direct,bool portable) {
  CoinRenderCapabilities caps{};
  const bool available=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
    &caps,sizeof(caps))==0 && caps.gpu_available &&
    caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN;
  if(!check(available,"real GPU adapter and renderer required")) return false;
  const auto requestedRenderer=static_cast<CoinRenderRenderer>(caps.renderer);
  CoinRenderOptions options{};if(srgb)options.storedTextureColorSpace=COIN_RENDER_TEXTURE_SRGB;if(portable)options.textureSamplingPolicy=COIN_RENDER_SAMPLING_PORTABLE;
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
  const unsigned char red[]={resultByte(220,0),resultByte(30,1),resultByte(40,2)},green[]={resultByte(30,0),resultByte(210,1),resultByte(50,2)},blue[]={resultByte(40,0),resultByte(50,1),resultByte(230,2)};
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
  const unsigned char changed[]={resultByte(240,0),resultByte(130,1),resultByte(20,2)};
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
  Frame volumeStable,volumeBad,volumeRecovered;
  const bool captured=capture(volumeStable,"NPOT producer stable");scene.setVolume(true);action.apply(scene.consumer);
  target->readbackRGBA(volumeBad.rgba);target->readbackDepth(volumeBad.depth);volumeBad.serial=target->getLastSubmissionSerial();
  ok &=check(captured&&action.getLastStatus()!=CoinRenderAction::SUCCESS&&volumeBad.rgba==volumeStable.rgba&&volumeBad.depth==volumeStable.depth&&volumeBad.serial==volumeStable.serial,"NPOT invalid producer preserves RTT publication");
  scene.setVolume(false);ok &=check(capture(volumeRecovered,"NPOT producer recovered")&&volumeRecovered.rgba==volumeStable.rgba&&volumeRecovered.depth==volumeStable.depth&&volumeRecovered.serial>volumeStable.serial,"NPOT RTT producer recovers after failed transaction");
  action.setRenderTarget(nullptr);
  return ok;
}
}
int main(int argc,char ** argv) {
  bool direct=false,portable=false;
  for(int index=1;index<argc;++index) {
    const std::string option=argv[index];
    if(option=="--srgb")srgb=true;
    else if(option=="--portable-sampling")portable=true;
    else if(option=="--direct") direct=true;
    else if(option=="--staged") direct=false;
    else {std::cerr << "Usage: CoinRenderVolumeNpotRttTest --staged|--direct\n";return 2;}
  }
  SoDB::init();CoinRenderAction::initClass();
  return caseOne(direct,portable)?0:1;
}
