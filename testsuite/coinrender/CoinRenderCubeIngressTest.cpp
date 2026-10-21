#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTextureCoordinate3.h>
#include <Inventor/nodes/SoTextureCubeMap.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
bool check(bool good,const std::string & name) {
  if(!good) std::cerr << "cube_ingress FAIL " << name << '\n';
  return good;
}
class Capture : public CoinRenderCpuReferenceBackend {
public:
  CoinRenderFramePlan plan;
  bool captured=false;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame,
                                CoinRenderTargetP & target) override {
    plan=frame;
    captured=true;
    return CoinRenderCpuReferenceBackend::submit(frame,target);
  }
};
struct Scene {
  SoSeparator * root=new SoSeparator;
  Scene() {
    root->ref();
    auto * camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,3);camera->height=2;
    camera->nearDistance=.1f;camera->farDistance=10;
    root->addChild(camera);
    auto * lighting=new SoLightModel;lighting->model=SoLightModel::BASE_COLOR;
    root->addChild(lighting);
    auto * material=new SoMaterial;material->diffuseColor.setValue(1,1,1);
    root->addChild(material);
    auto * quality=new SoComplexity;quality->textureQuality=.1f;
    root->addChild(quality);
    auto * cube=new SoTextureCubeMap;
    cube->model=SoTextureCubeMap::REPLACE;
    std::array<std::array<uint8_t,16>,6> faces;
    for(int face=0;face<6;++face) for(int texel=0;texel<4;++texel) {
      faces[face][texel*4]=uint8_t(23+face*29);
      faces[face][texel*4+1]=uint8_t(37+face*17);
      faces[face][texel*4+2]=uint8_t(61+face*13);
      faces[face][texel*4+3]=255;
    }
    cube->imagePosX.setValue(SbVec2s(2,2),4,faces[0].data());
    cube->imageNegX.setValue(SbVec2s(2,2),4,faces[1].data());
    cube->imagePosY.setValue(SbVec2s(2,2),4,faces[2].data());
    cube->imageNegY.setValue(SbVec2s(2,2),4,faces[3].data());
    cube->imagePosZ.setValue(SbVec2s(2,2),4,faces[4].data());
    cube->imageNegZ.setValue(SbVec2s(2,2),4,faces[5].data());
    root->addChild(cube);
    auto * vertices=new SoCoordinate3;
    const SbVec3f points[]={SbVec3f(-.7f,-.7f,0),SbVec3f(.7f,-.7f,0),
      SbVec3f(.7f,.7f,0),SbVec3f(-.7f,.7f,0)};
    vertices->point.setValues(0,4,points);root->addChild(vertices);
    auto * coordinates=new SoTextureCoordinate3;
    const SbVec3f rays[]={SbVec3f(1,0,0),SbVec3f(1,0,0),
      SbVec3f(1,0,0),SbVec3f(1,0,0)};
    coordinates->point.setValues(0,4,rays);root->addChild(coordinates);
    auto * shape=new SoIndexedFaceSet;
    const int32_t indices[]={0,1,2,3,-1};
    shape->coordIndex.setValues(0,5,indices);root->addChild(shape);
  }
  ~Scene() {root->unref();}
};
bool ingress() {
  Scene scene;
  std::unique_ptr<CoinRenderTarget> capturedTarget(
    CoinRenderTarget::createOffscreen(SbVec2i32(64,64)));
  if(!check(bool(capturedTarget),"capture target created")) return false;
  auto * capture=new Capture;
  capturedTarget->getPimpl()->backend.reset(capture);
  CoinRenderAction action(SbViewportRegion(64,64));
  action.setRenderTarget(capturedTarget.get());
  action.apply(scene.root);
  const bool captured=capture->captured &&
    action.getLastStatus()==CoinRenderAction::SUCCESS;
  CoinRenderFramePlan good=capture->plan;
  action.setRenderTarget(nullptr);
  if(!check(captured,"valid cube scene captured and executed")) return false;
  if(!check(good.textures.size()==1 && good.textures[0].width>0 &&
    good.textures[0].height>0 && !good.textures[0].pixelsRgba.empty(),
    "one physical cube atlas captured")) return false;
  CoinRenderTargetP direct(SbVec2i32(64,64));
  direct.backend.reset(new CoinRenderCpuReferenceBackend);
  direct.depthReadbackEnabled=true;
  good.revision=101;
  const CoinRenderSubmitResult initial=direct.executeFrame(good);
  if(!check(initial.status==CoinRenderBackendStatus::SUCCESS &&
    direct.colorBuffer.size()==64u*64u*4u && direct.depthBuffer.size()==64u*64u,
    "valid direct CPU submission published")) return false;
  const size_t center=(32u*64u+32u)*4u;
  if(!check(direct.colorBuffer[center]==23 && direct.colorBuffer[center+1]==37 &&
    direct.colorBuffer[center+2]==61,"cube texel visibly sampled")) return false;
  auto reject=[&](const std::string & name,const CoinRenderFramePlan & broken) {
    const std::vector<uint8_t> rgba=direct.colorBuffer;
    const std::vector<float> depth=direct.depthBuffer;
    const uint64_t serial=direct.lastSubmissionSerial;
    // Direct backend ingress bypasses action and TargetP preflight deliberately.
    const CoinRenderSubmitResult result=direct.backend->submit(broken,direct);
    const bool atomic=result.status!=CoinRenderBackendStatus::SUCCESS &&
      direct.colorBuffer==rgba && direct.depthBuffer==depth &&
      direct.lastSubmissionSerial==serial;
    std::cout << "cube_ingress " << name << " rejected="
              << (result.status!=CoinRenderBackendStatus::SUCCESS)
              << " atomic=" << atomic << " diagnostic=" << result.diagnostic << '\n';
    return check(atomic,name+" rejected before direct CPU publication");
  };
  bool ok=true;
  CoinRenderFramePlan broken=good;
  broken.textures[0].width+=1;
  ok &= reject("physical_width",broken);
  broken=good;broken.textures[0].height+=1;
  ok &= reject("physical_height",broken);
  broken=good;broken.textures[0].pixelsRgba.pop_back();
  ok &= reject("truncated_atlas",broken);
  broken=good;broken.textures[0].components=5;
  ok &= reject("invalid_components",broken);
  const std::vector<uint8_t> expectedRgba=direct.colorBuffer;
  const std::vector<float> expectedDepth=direct.depthBuffer;
  const uint64_t serial=direct.lastSubmissionSerial;
  const CoinRenderSubmitResult recovery=direct.executeFrame(good);
  ok &= check(recovery.status==CoinRenderBackendStatus::SUCCESS &&
    direct.colorBuffer==expectedRgba && direct.depthBuffer==expectedDepth &&
    direct.lastSubmissionSerial>serial,"valid same-revision plan recovers exactly");
  return ok;
}
}
int main() {
  SoDB::init();CoinRenderAction::initClass();
  return ingress()?0:1;
}
