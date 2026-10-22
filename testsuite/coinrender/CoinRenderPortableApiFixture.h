#ifndef COIN_RENDER_PORTABLE_API_FIXTURE_H
#define COIN_RENDER_PORTABLE_API_FIXTURE_H
#include <Inventor/SoDB.h>
#include <Inventor/SoPath.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace portable_api {
inline bool & good() { static bool value=true; return value; }
inline bool check(bool value,const std::string &label) {
  if(!value) {good()=false;std::cerr<<"portable_api FAIL "<<label<<'\n';}
  return value;
}
struct Image {
  std::vector<uint8_t> rgba;
  std::vector<float> depth;
  uint64_t serial=0;
  const uint8_t *borrow=nullptr;
  std::size_t bytes=0;
};
inline Image read(const CoinRenderTarget &target) {
  Image result;target.readbackRGBA(result.rgba);target.readbackDepth(result.depth);
  result.serial=target.getLastSubmissionSerial();result.borrow=target.borrowRGBA(result.bytes);
  return result;
}
inline bool unchanged(const CoinRenderTarget &target,const Image &before,const std::string &label) {
  const auto after=read(target);
  return check(after.rgba==before.rgba&&after.depth==before.depth&&after.serial==before.serial&&
    after.borrow==before.borrow&&after.bytes==before.bytes,label+" exact publication and borrow preserved");
}
struct Recorder : CoinRenderCpuReferenceBackend {
  unsigned submits=0,rttSubmits=0;
  CoinRenderFramePlan frame;
  std::vector<CoinRenderCompositionItem> order;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &candidate,CoinRenderTargetP &target) override {
    ++submits;frame=candidate;
    std::string error;
    if(!coin_render_composition_schedule(candidate,order,error,target.submissionPreflight(candidate)))
      return {CoinRenderBackendStatus::BACKEND_ERROR,error};
    return CoinRenderCpuReferenceBackend::submit(candidate,target);
  }
  CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan &candidate,const SbVec2i32 &size,uint64_t revision,
                                  CoinRenderTargetP &target,uint64_t &resource) override {
    ++rttSubmits;
    return CoinRenderCpuReferenceBackend::submitRtt(candidate,size,revision,target,resource);
  }
};
struct Fixture {
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderAction action;
  Recorder *recorder=nullptr;
  bool gpu;
  Fixture(bool useGpu,int width=32,int height=24):action(SbViewportRegion(width,height)),gpu(useGpu) {
    CoinRenderOptions options;
    if(gpu) {
      CoinRenderCapabilities caps{};
      if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))!=0 ||
         !caps.gpu_available || caps.renderer==COIN_RENDER_RENDERER_UNKNOWN) return;
      std::string diagnostic;options=CoinRenderDiagnosticShell::renderOptions(diagnostic);
      options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
      std::cout<<"portable_api GPU renderer="<<caps.renderer<<" adapter="<<caps.adapter_name<<'\n';
    }
    options.sampleCount=1;
    target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(width,height),options));
    if(!target) {check(false,"offscreen factory after admission");return;}
    if(!gpu) {recorder=new Recorder;target->getPimpl()->backend.reset(recorder);}
    action.setRenderTarget(target.get());action.setBackgroundColor(SbColor4f(0,0,0,1));
    action.setTransparencyType(CoinRenderAction::NONE);
  }
  ~Fixture(){action.setRenderTarget(nullptr);}
  bool apply(SoNode *root,const std::string &label) {
    action.apply(root);
    return check(action.getLastStatus()==CoinRenderAction::SUCCESS,
      label+" status="+std::to_string(action.getLastStatus())+" "+action.getLastError().getString());
  }
};
struct Quad {
  SoSeparator *root=new SoSeparator;
  SoMaterial *material=new SoMaterial;
  SoCoordinate3 *coordinates=new SoCoordinate3;
  SoFaceSet *shape=new SoFaceSet;
  Quad(float left=-.75f,float right=.75f,float bottom=-.75f,float top=.75f,float z=0) {
    root->ref();root->addChild(material);root->addChild(coordinates);root->addChild(shape);
    material->diffuseColor.setValue(1,0,0);
    const SbVec3f p[]={SbVec3f(left,bottom,z),SbVec3f(right,bottom,z),
      SbVec3f(right,top,z),SbVec3f(left,top,z)};
    coordinates->point.setValues(0,4,p);shape->numVertices=4;
  }
  ~Quad(){root->unref();}
};
struct Scene {
  SoSeparator *root=new SoSeparator;
  SoOrthographicCamera *camera=new SoOrthographicCamera;
  Scene() {
    root->ref();camera->position.setValue(0,0,3);camera->height=2;
    camera->nearDistance=1;camera->farDistance=5;
    camera->viewportMapping=SoCamera::LEAVE_ALONE;root->addChild(camera);
    auto *light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;root->addChild(light);
  }
  ~Scene(){root->unref();}
};
inline bool centerColor(const CoinRenderTarget &target,int red,int green,int blue,const std::string &label) {
  const auto image=read(target);const auto size=target.getSize();
  if(!check(image.rgba.size()==std::size_t(size[0])*size[1]*4,label+" complete RGBA"))return false;
  const std::size_t at=(std::size_t(size[1]/2)*size[0]+size[0]/2)*4;
  return check(std::abs(int(image.rgba[at])-red)<=2&&std::abs(int(image.rgba[at+1])-green)<=2&&
    std::abs(int(image.rgba[at+2])-blue)<=2,label+" analytic center RGB");
}
}
#endif
