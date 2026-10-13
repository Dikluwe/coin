#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderPbrEnvironment.h"
#include "actions/CoinRenderActionP.h"
#include <Inventor/SoDB.h>
#include <Inventor/C/tidbits.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoCallback.h>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <iostream>
#ifdef _WIN32
#include <io.h>
#define pbr_dup _dup
#define pbr_dup2 _dup2
#define pbr_close _close
#define pbr_fileno _fileno
#else
#include <unistd.h>
#define pbr_dup dup
#define pbr_dup2 dup2
#define pbr_close close
#define pbr_fileno fileno
#endif
#define CHECK(x,m) do{if(!(x)){std::fprintf(stderr,"FAIL: %s (%d)\n",m,__LINE__);return 1;}}while(0)
class PrimitiveCountCube:public SoCube {
  SO_NODE_HEADER(PrimitiveCountCube);
public:
  static int count;
  static void initClass(){SO_NODE_INIT_CLASS(PrimitiveCountCube,SoCube,"Cube");}
  PrimitiveCountCube(){SO_NODE_CONSTRUCTOR(PrimitiveCountCube);}
protected:
  ~PrimitiveCountCube() override=default;
  void generatePrimitives(SoAction * a) override{++count;SoCube::generatePrimitives(a);}
};
SO_NODE_SOURCE(PrimitiveCountCube);int PrimitiveCountCube::count=0;
class PublicationBackend:public CoinRenderBackend {
public:
  bool fail=false;std::string error;
  bool isGpuBackend() const override{return false;}
  bool supportsPbr() const override{return true;}
  CoinRenderBackendStatus getStatus() const override{return CoinRenderBackendStatus::SUCCESS;}
  CoinRenderBackendStatus prepare(CoinRenderTargetP &) override{return CoinRenderBackendStatus::SUCCESS;}
  void poll() override{}
  const std::string & getLastError() const override{return error;}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & f,CoinRenderTargetP & t) override {
    if(fail)return {CoinRenderBackendStatus::UNSUPPORTED,"controlled candidate rejection"};
    t.colorBuffer.assign(size_t(t.size[0])*t.size[1]*4,uint8_t(f.materials[0].pbr.roughness*200));
    t.depthBuffer.assign(size_t(t.size[0])*t.size[1],.5f);return {};
  }
};
static SoSeparator * scene(SoPBRMaterial * a,SoPBRMaterial * b=nullptr) {
  auto * root=new SoSeparator;root->ref();auto * camera=new SoOrthographicCamera;camera->position=SbVec3f(0,0,5);camera->height=4;camera->nearDistance=.1f;camera->farDistance=10;root->addChild(camera);
  auto * light=new SoDirectionalLight;root->addChild(light);
  auto * first=new SoSeparator;first->addChild(a);first->addChild(new PrimitiveCountCube);root->addChild(first);
  if(b){auto * second=new SoSeparator;second->addChild(b);auto * translation=new SoTranslation;translation->translation=SbVec3f(2.2f,0,0);second->addChild(translation);second->addChild(new PrimitiveCountCube);root->addChild(second);}
  return root;
}
static bool fullOracle(CoinRenderAction & candidate,SoNode * root) {
  CoinRenderAction reference(SbViewportRegion(64,64));reference.apply(root);
  return reference.getLastStatus()==CoinRenderAction::SUCCESS&&
    candidate.getPimpl()->lastValidPlan.hasSamePayload(reference.getPimpl()->lastValidPlan);
}
static void observe(void * data,SoAction *){++*static_cast<int *>(data);}
int main(int argc,char ** argv) {
  SoDB::init();CoinRenderAction::initClass();PrimitiveCountCube::initClass();
  auto * material=new SoPBRMaterial;auto * root=scene(material);
  CoinRenderAction action(SbViewportRegion(64,64));action.apply(root);
  CHECK(action.getLastStatus()==CoinRenderAction::SUCCESS&&PrimitiveCountCube::count>0,"native primitive source is observable");
  const auto * vertices=action.getPimpl()->lastValidPlan.vertices.data();const auto * indices=action.getPimpl()->lastValidPlan.indices.data();
  for(int field=0;field<4;++field) {
    const int before=PrimitiveCountCube::count;
    if(field==0)material->roughness=.7f;else if(field==1)material->metallic=.9f;
    else if(field==2)material->baseColor=SbColor(.7f,.3f,.1f);else material->emissive=SbColor(2,4,.5f);
    action.apply(root);
    CHECK(action.getLastStatus()==CoinRenderAction::SUCCESS&&PrimitiveCountCube::count==before,"qualified factor update performs no primitive traversal");
    CHECK(action.getPimpl()->lastValidPlan.vertices.data()==vertices&&action.getPimpl()->lastValidPlan.indices.data()==indices,"factor patch preserves geometry allocations");
    CHECK(fullOracle(action,root),"factor patch matches independent full Coin traversal payload");
  }
  material->roughness=std::numeric_limits<float>::quiet_NaN();const auto previous=action.getPimpl()->lastValidPlan;
  action.apply(root);CHECK(action.getLastStatus()==CoinRenderAction::INVALID_SCENE&&action.getPimpl()->lastValidPlan.hasSamePayload(previous),"invalid factor preserves captured publication");
  material->roughness=.5f;action.apply(root);CHECK(action.getLastStatus()==CoinRenderAction::SUCCESS&&fullOracle(action,root),"repair preserves proof and correct source semantics");
  material->setOverride(TRUE);int before=PrimitiveCountCube::count;action.apply(root);
  CHECK(action.getLastStatus()==CoinRenderAction::SUCCESS&&PrimitiveCountCube::count>before&&fullOracle(action,root),"unnotified override change revokes source proof");material->setOverride(FALSE);
  action.apply(root);material->roughness=.6f;coin_setenv("COIN_RENDER_DISABLE_PBR_MATERIAL_OVERLAY","1",1);before=PrimitiveCountCube::count;action.apply(root);coin_unsetenv("COIN_RENDER_DISABLE_PBR_MATERIAL_OVERLAY");
  CHECK(action.getLastStatus()==CoinRenderAction::SUCCESS&&PrimitiveCountCube::count>before,"explicit cache optout traverses geometry");
  auto * b=new SoPBRMaterial;b->roughness=.8f;auto * two=scene(new SoPBRMaterial,b);CoinRenderAction pair(SbViewportRegion(64,64));pair.apply(two);
  before=PrimitiveCountCube::count;b->roughness=.9f;pair.apply(two);
  CHECK(pair.getLastStatus()==CoinRenderAction::SUCCESS&&PrimitiveCountCube::count==before&&fullOracle(pair,two),"unique source among multiple materials is patched");
  auto * duplicateA=new SoPBRMaterial;auto * duplicateB=new SoPBRMaterial;auto * ambiguous=scene(duplicateA,duplicateB);CoinRenderAction duplicates(SbViewportRegion(64,64));duplicates.apply(ambiguous);
  before=PrimitiveCountCube::count;duplicateA->roughness=.7f;duplicates.apply(ambiguous);
  CHECK(duplicates.getLastStatus()==CoinRenderAction::SUCCESS&&PrimitiveCountCube::count>before&&fullOracle(duplicates,ambiguous),"ambiguous interned sources fall back to full capture");
  int callbackCalls=0;auto * callback=new SoCallback;callback->setCallback(observe,&callbackCalls);root->addChild(callback);action.apply(root);before=PrimitiveCountCube::count;material->roughness=.55f;action.apply(root);
  CHECK(PrimitiveCountCube::count>before&&callbackCalls==2,"callback-bearing scene cannot skip observable traversal");root->removeChild(callback);
  auto * rollbackMaterial=new SoPBRMaterial;auto * rollbackRoot=scene(rollbackMaterial);
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64,64)));
  auto * publication=new PublicationBackend;target->getPimpl()->backend.reset(publication);
  CoinRenderAction transactional(SbViewportRegion(64,64));transactional.setRenderTarget(target.get());transactional.apply(rollbackRoot);
  CHECK(transactional.getLastStatus()==CoinRenderAction::SUCCESS,"controlled initial publication");
  const auto captured=transactional.getPimpl()->lastValidPlan;std::vector<uint8_t> pixels;target->readbackRGBA(pixels);const auto serial=target->getLastSubmissionSerial();
  publication->fail=true;rollbackMaterial->roughness=.7f;transactional.apply(rollbackRoot);std::vector<uint8_t> after;target->readbackRGBA(after);
  CHECK(transactional.getLastStatus()==CoinRenderAction::UNSUPPORTED&&after==pixels&&target->getLastSubmissionSerial()==serial&&transactional.getPimpl()->lastValidPlan.hasSamePayload(captured),"rejected factor candidate rolls back plan and preserves publication");
  publication->fail=false;transactional.apply(rollbackRoot);CHECK(transactional.getLastStatus()==CoinRenderAction::SUCCESS&&fullOracle(transactional,rollbackRoot),"factor retry commits the qualified source only after success");
  if(argc>1&&std::string(argv[1])=="--gpu") {
    CoinRenderCapabilities caps{};coin_render_query_capabilities_for_renderer(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,COIN_RENDER_RENDERER_VULKAN,&caps,sizeof(caps));
    CHECK(caps.features&COIN_RENDER_FEATURE_PBR_OPAQUE,"GPU profile advertises opaque PBR");
    CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;
    std::unique_ptr<CoinRenderTarget> gpu(CoinRenderTarget::createOffscreen(SbVec2i32(64,64),options));
    CoinRenderAction render(SbViewportRegion(64,64));render.setRenderTarget(gpu.get());render.apply(rollbackRoot);CHECK(render.getLastStatus()==CoinRenderAction::SUCCESS,"native GPU first frame");
    coin_setenv("COIN_RENDER_TRACE_PHASES","1",1);std::ostringstream trace;auto * old=std::cerr.rdbuf(trace.rdbuf());before=PrimitiveCountCube::count;
    FILE * capture=std::tmpfile();CHECK(capture,"temporary GPU diagnostics file");const int saved=pbr_dup(pbr_fileno(stderr));
    CHECK(saved>=0&&pbr_dup2(pbr_fileno(capture),pbr_fileno(stderr))>=0,"capture native diagnostics descriptor");
    rollbackMaterial->baseColor=SbColor(.6f,.2f,.1f);rollbackMaterial->roughness=.35f;render.apply(rollbackRoot);
    std::fflush(stderr);pbr_dup2(saved,pbr_fileno(stderr));pbr_close(saved);std::rewind(capture);
    char block[1024];size_t length;while((length=std::fread(block,1,sizeof(block),capture)))trace.write(block,length);std::fclose(capture);
    std::cerr.rdbuf(old);coin_unsetenv("COIN_RENDER_TRACE_PHASES");
    CHECK(render.getLastStatus()==CoinRenderAction::SUCCESS&&PrimitiveCountCube::count==before,"native GPU factor edit avoids source traversal");
    CHECK(trace.str().find("material_patch=1 material_patch_ranges=0 material_patch_vertices=0")!=std::string::npos,"native GPU uniform edit reuses geometry without vertex uploads");
    std::printf("GPU material-only trace: %s\n",trace.str().c_str());
  }
  rollbackRoot->unref();ambiguous->unref();two->unref();root->unref();
  std::printf("PBR material cache: four factors/HDR, exact full-traversal oracle, ambiguity, override, optout, callbacks and transactional retry PASS\n");return 0;
}
