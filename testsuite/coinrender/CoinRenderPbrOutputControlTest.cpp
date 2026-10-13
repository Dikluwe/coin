#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderPbrEnvironment.h"
#include "actions/CoinRenderActionP.h"
#include <Inventor/SoDB.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <cstdio>
#include <limits>
#define CHECK(x,m) do{if(!(x)){std::fprintf(stderr,"FAIL: %s (%d)\n",m,__LINE__);return 1;}}while(0)
int main(int argc,char ** argv) {
  SoDB::init();CoinRenderAction::initClass();
  SbString path=argc>1?argv[1]:"/tmp/coin-pbr-output.cibl",error;CoinRenderPbrEnvironment environment;
  CHECK(environment.bake(environment.WHITE,8,32,32,512,environment,error)&&environment.write(path,error),"small versioned white environment");
  CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;options.pbrEnvironmentBundle=path;
  auto * root=new SoSeparator;root->ref();auto * camera=new SoOrthographicCamera;camera->height=3;camera->position=SbVec3f(0,0,4);root->addChild(camera);
  auto * material=new SoPBRMaterial;material->baseColor=SbColor(1,1,1);material->metallic=1;material->roughness=.8f;root->addChild(material);root->addChild(new SoCube);
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(32,32),options));
  CoinRenderAction action(SbViewportRegion(32,32));action.setRenderTarget(target.get());action.apply(root);
  CHECK(action.getLastStatus()==CoinRenderAction::SUCCESS,"first public PBR output frame");
  const auto revision=action.getPimpl()->lastValidPlan.revision;const auto * geometry=action.getPimpl()->lastValidPlan.vertices.data();
  SbList<SbVec3f> first,bright,exposed;CHECK(target->getPimpl()->backend->readSceneLinear(first),"first HDR radiance");
  const auto serial=target->getLastSubmissionSerial();std::vector<uint8_t> pixels,after;target->readbackRGBA(pixels);
  const float nan=std::numeric_limits<float>::quiet_NaN();
  CHECK(!target->setPbrExposureStops(nan)&&!target->setPbrExposureStops(17)&&!target->setPbrEnvironmentParameters(nan,1)&&
    !target->setPbrEnvironmentParameters(0,65)&&target->getOptions().pbrExposureStops==0&&target->getOptions().pbrEnvironmentIntensity==1,
    "invalid public scalars leave configuration intact");
  target->readbackRGBA(after);CHECK(after==pixels&&target->getLastSubmissionSerial()==serial,"invalid controls preserve published frame");
  CHECK(target->setPbrEnvironmentParameters(.5f,2),"public environment controls");action.apply(root);
  CHECK(action.getLastStatus()==CoinRenderAction::SUCCESS&&action.getPimpl()->lastValidPlan.revision==revision&&
    action.getPimpl()->lastValidPlan.vertices.data()==geometry,"environment scalars reuse the captured plan and geometry");
  CHECK(target->getPimpl()->backend->readSceneLinear(bright)&&std::abs(bright[16*32+16][0]-first[16*32+16][0]*2)<.003&&bright[16*32+16][0]>1.99f,"public intensity doubles indirect HDR above display white");
  target->readbackRGBA(pixels);CHECK(target->setPbrExposureStops(-1),"public exposure control");action.apply(root);target->readbackRGBA(after);
  CHECK(action.getLastStatus()==CoinRenderAction::SUCCESS&&after!=pixels&&action.getPimpl()->lastValidPlan.revision==revision&&
    action.getPimpl()->lastValidPlan.vertices.data()==geometry,"exposure changes only output and preserves capture");
  CHECK(target->getPimpl()->backend->readSceneLinear(exposed)&&exposed[16*32+16]==bright[16*32+16],"exposure leaves scene-linear radiance intact");
  const auto identity=target->getPimpl()->pbrEnvironmentSnapshot->identity;
  CHECK(target->resize(SbVec2i32(0,0)),"offscreen suspension");action.apply(root);target->readbackRGBA(after);
  CHECK(action.getLastStatus()==CoinRenderAction::NOT_READY&&after.empty(),"suspension exposes no stale frame");
  CHECK(target->resize(SbVec2i32(48,48)),"resume resized target");action.setViewportRegion(SbViewportRegion(48,48));action.apply(root);target->readbackRGBA(after);
  CHECK(action.getLastStatus()==CoinRenderAction::SUCCESS&&after.size()==48u*48u*4u&&target->getPimpl()->pbrEnvironmentSnapshot->identity==identity,"resume reconstructs attachments and retains environment content");
  std::remove(path.getString());root->unref();std::printf("PBR public controls: finite validation, publication, plan reuse, indirect HDR, output exposure, suspend/resume and environment identity PASS\n");return 0;
}
