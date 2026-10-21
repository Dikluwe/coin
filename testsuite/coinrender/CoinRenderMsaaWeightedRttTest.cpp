#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderViewBudgetTest.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "actions/CoinRenderActionP.h"
#include "rendering/coinrender/CoinRenderBackend.h"
#include "rendering/coinrender/CoinRenderRttCore.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <limits>
#include <string>
#include <utility>
#include <vector>
namespace {
constexpr int side=32;
class CountingBackend final:public CoinRenderBackend{
public:
  explicit CountingBackend(std::unique_ptr<CoinRenderBackend> wrapped):real(std::move(wrapped)){}
  std::unique_ptr<CoinRenderBackend> real;
  mutable unsigned preflightCalls=0;
  unsigned producerSubmissions=0;
  bool isGpuBackend() const override{return real->isGpuBackend();}
  bool initializesCpuDepthBuffer() const override{return real->initializesCpuDepthBuffer();}
  bool resetOnActionDetach() const override{return real->resetOnActionDetach();}
  bool supportsOffscreenShadows() const override{return real->supportsOffscreenShadows();}
  bool supportsOpaqueBump() const override{return real->supportsOpaqueBump();}
  CoinRenderBackendStatus getStatus() const override{return real->getStatus();}
  CoinRenderBackendStatus prepare(CoinRenderTargetP &target) override{return real->prepare(target);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &frame,CoinRenderTargetP &target) override{
    return real->submit(frame,target);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &frame,CoinRenderTargetP &target,
    const CoinRenderFrameReuseDecision &reuse) override{return real->submit(frame,target,reuse);}
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan &graph,
    const CoinRenderFramePlan &root,const SbVec2i32 &size) const override{
    ++preflightCalls;return real->preflightRtt(graph,root,size);}
  CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan &frame,const SbVec2i32 &size,
    uint64_t producer,CoinRenderTargetP &target,uint64_t &token) override{
    ++producerSubmissions;return real->submitRtt(frame,size,producer,target,token);}
  void finishRtt(const std::vector<uint64_t> &tokens) override{real->finishRtt(tokens);}
  bool stagedRttRequiresPreparedParent() const override{return real->stagedRttRequiresPreparedParent();}
  bool requiresSharedRetirement() const override{return real->requiresSharedRetirement();}
  void retireLostReadbacks() override{real->retireLostReadbacks();}
  CoinRenderDeviceDomain resourceDomain() const override{return real->resourceDomain();}
  bool readbackLoad(uint64_t &count,uint64_t &bytes) const override{
    return real->readbackLoad(count,bytes);}
  void poll() override{real->poll();}
  const std::string &getLastError() const override{return real->getLastError();}
};
bool check(bool yes,const std::string & label) {
  if(!yes) std::cerr << "msaa_weighted_rtt FAIL " << label << '\n';
  return yes;
}
SoOrthographicCamera * camera() {
  auto * result=new SoOrthographicCamera;
  result->position.setValue(0,0,2);result->height=2;
  result->nearDistance=.1f;result->farDistance=10;return result;
}
SoIndexedFaceSet * face(const int32_t * indices,int count) {
  auto * result=new SoIndexedFaceSet;
  result->coordIndex.setValues(0,count,indices);return result;
}
struct Scene {
  SoSeparator * producer=new SoSeparator,*consumer=new SoSeparator;
  SoMaterial * red=new SoMaterial;
  SoAlphaTest * alpha=new SoAlphaTest;
  bool producerBlend;
  explicit Scene(bool blendProducer):producerBlend(blendProducer) {
    producer->ref();consumer->ref();
    producer->addChild(camera());
    auto *producerMode=new SoTransparencyType;
    producerMode->value=producerBlend?SoTransparencyType::SORTED_OBJECT_BLEND:SoTransparencyType::NONE;
    producer->addChild(producerMode);
    auto * unlit=new SoLightModel;unlit->model=SoLightModel::BASE_COLOR;
    producer->addChild(unlit);
    red->diffuseColor.setValue(1,0,0);
    red->transparency=producerBlend?.5f:0.f;producer->addChild(red);
    alpha->function=SoAlphaTest::ALWAYS;alpha->value=.5f;producer->addChild(alpha);
    auto * points=new SoCoordinate3;
    const SbVec3f p[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),SbVec3f(-1,.83f,0)};
    points->point.setValues(0,3,p);producer->addChild(points);
    const int32_t triangle[]={0,1,2,-1};producer->addChild(face(triangle,4));
    auto *layer=new SoSeparator;
    auto *depthPolicy=new SoDepthBuffer;
    depthPolicy->test=FALSE;depthPolicy->write=FALSE;layer->addChild(depthPolicy);
    auto *green=new SoMaterial;green->diffuseColor.setValue(0,1,0);layer->addChild(green);
    auto *layerPoints=new SoCoordinate3;
    const SbVec3f lp[]={SbVec3f(0,0,-.4f),SbVec3f(.3f,0,-.4f),
      SbVec3f(.3f,.3f,-.4f),SbVec3f(0,.3f,-.4f)};
    layerPoints->point.setValues(0,4,lp);layer->addChild(layerPoints);
    auto *layerFace=new SoFaceSet;layerFace->numVertices.set1Value(0,4);
    layer->addChild(layerFace);producer->addChild(layer);
    auto *annotation=new SoAnnotation;
    auto *yellow=new SoMaterial;yellow->diffuseColor.setValue(1,1,0);
    annotation->addChild(yellow);
    auto *notePoints=new SoCoordinate3;
    const SbVec3f np[]={SbVec3f(.55f,.55f,-.8f),SbVec3f(.85f,.55f,-.8f),
      SbVec3f(.85f,.85f,-.8f),SbVec3f(.55f,.85f,-.8f)};
    notePoints->point.setValues(0,4,np);annotation->addChild(notePoints);
    auto *noteFace=new SoFaceSet;noteFace->numVertices.set1Value(0,4);
    annotation->addChild(noteFace);producer->addChild(annotation);
    consumer->addChild(camera());
    auto *consumerMode=new SoTransparencyType;
    consumerMode->value=producerBlend?SoTransparencyType::NONE:SoTransparencyType::SORTED_OBJECT_BLEND;
    consumer->addChild(consumerMode);
    auto * consumerUnlit=new SoLightModel;
    consumerUnlit->model=SoLightModel::BASE_COLOR;consumer->addChild(consumerUnlit);
    auto * white=new SoMaterial;white->diffuseColor.setValue(1,1,1);
    white->transparency=producerBlend?0.f:.5f;
    consumer->addChild(white);
    auto * complexity=new SoComplexity;complexity->textureQuality=.1f;
    consumer->addChild(complexity);
    auto * texture=new SoSceneTexture2;
    texture->scene=producer;texture->size.setValue(32,32);
    texture->type=SoSceneTexture2::RGBA8;
    texture->model=producerBlend?SoSceneTexture2::REPLACE:SoSceneTexture2::MODULATE;
    texture->transparencyFunction=SoSceneTexture2::NONE;
    texture->backgroundColor.setValue(0,0,1,1);
    texture->wrapS=SoSceneTexture2::CLAMP;
    texture->wrapT=SoSceneTexture2::CLAMP;
    consumer->addChild(texture);
    auto * uv=new SoTextureCoordinate2;
    const SbVec2f st[]={SbVec2f(0,0),SbVec2f(1,0),SbVec2f(1,1),SbVec2f(0,1)};
    uv->point.setValues(0,4,st);consumer->addChild(uv);
    auto * quadPoints=new SoCoordinate3;
    const SbVec3f q[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),
                       SbVec3f(1,1,0),SbVec3f(-1,1,0)};
    quadPoints->point.setValues(0,4,q);consumer->addChild(quadPoints);
    const int32_t quad[]={0,1,2,3,-1};consumer->addChild(face(quad,5));
  }
  ~Scene(){consumer->unref();producer->unref();}
};
struct Image {
  std::vector<uint8_t> rgba;
  std::vector<float> depth;
  uint64_t serial=0;
  size_t partial=0;
};
bool sampleLevels(const Image & image,uint32_t samples) {
  size_t quarter=0,half=0,threeQuarter=0;
  const int fullRed=image.rgba[(size_t(24)*side+8)*4];
  if(!check(std::abs(fullRed-128)<=3,
    "independent half-alpha full-coverage RTT red witness"))return false;
  // Restrict the coverage witness to the left half, where only the triangle
  // and blue producer background are present; the green layer and yellow
  // annotation intentionally occupy different parts of the texture.
  for(size_t y=0;y<side;++y) for(size_t x=2;x<16;++x) {
    const int red=image.rgba[4*(y*side+x)];
    if(red==0 || red==fullRed) continue;
    const int covered=int(std::lround(double(red)*samples/fullRed));
    if(covered<=0 || covered>=int(samples) ||
       std::abs(red-int(std::lround(double(fullRed)*covered/samples)))>3) return false;
    quarter+=(covered==1);half+=(2*covered==int(samples));
    threeQuarter+=(samples==4 && covered==3);
  }
  std::cout << "msaa_weighted_rtt sample_levels=" << samples << " quarter=" << quarter
            << " half=" << half << " three_quarter=" << threeQuarter << '\n';
  return samples==1 ? quarter+half+threeQuarter==0 :
    samples==2 ? half>0 : quarter>0 && threeQuarter>0;
}
bool capture(Scene & scene,const CoinRenderCapabilities & caps,uint32_t samples,
             CoinRenderSceneTextureMode route,Image & image) {
  CoinRenderOptions options;
  options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  options.sampleCount=samples;options.sceneTexture=route;options.transparency=COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;
  std::unique_ptr<CoinRenderTarget> target(
    CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
  if(!check(bool(target) && target->getOptions().renderer==options.renderer &&
            target->getOptions().sceneTexture==route &&
            target->getOptions().sampleCount==samples,"RTT target retains route, API and samples")) return false;
  target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(side,side));
  action.setBackgroundColor(SbColor4f(0,0,0,0));
  action.setRenderTarget(target.get());action.apply(scene.consumer);
  target->readbackRGBA(image.rgba);target->readbackDepth(image.depth);
  image.serial=target->getLastSubmissionSerial();
  bool rendered=action.getLastStatus()==CoinRenderAction::SUCCESS &&
    image.rgba.size()==side*side*4 && image.depth.size()==side*side && image.serial>0;
  if(rendered) {
    for(size_t i=0;i<size_t(side*side);++i)
      image.partial+=(image.rgba[4*i+3]>0 && image.rgba[4*i+3]<255);
    const size_t note=(size_t(4)*side+27)*4;
    const size_t center=(size_t(13)*side+18)*4;
    const size_t main=(size_t(24)*side+8)*4;
    const size_t mainPixel=size_t(24)*side+8;
    // Weighted opacity is 1-revealage. A half-alpha producer over opaque
    // blue therefore has final alpha 1; the opposite route composites
    // a half-alpha consumer over an alpha-zero target.
    const int expectedAlpha=scene.producerBlend?255:128;
    // The consumer's z=0 quad lies two world units from an orthographic
    // camera with near=.1 and far=10, so its depth is 1.9/9.9.
    // A weighted consumer never writes depth.  Only the opaque consumer
    // publishes the quad's geometric depth; the other route keeps clear=1.
    const float expectedDepth=scene.producerBlend?
      (2.f-.1f)/(10.f-.1f):1.f;
    std::cout<<"msaa_transparency_rtt main route="
             <<(route==COIN_RENDER_SCENE_TEXTURE_DIRECT?"direct":"staged")
             <<" samples="<<samples<<" producer_blend="<<scene.producerBlend
             <<" rgba="<<int(image.rgba[main])<<','<<int(image.rgba[main+1])
             <<','<<int(image.rgba[main+2])<<','<<int(image.rgba[main+3])
             <<" depth="<<image.depth[mainPixel]
             <<" expected_alpha="<<expectedAlpha
             <<" expected_depth="<<expectedDepth<<'\n';
    rendered &= check(image.rgba[note]>100 && image.rgba[note+1]>100 &&
      image.rgba[note+2]<10 && image.rgba[center+1]>100 &&
      std::abs(int(image.rgba[main])-128)<=3 &&
      image.rgba[main+1]<5 &&
      std::abs(int(image.rgba[main+2])-(scene.producerBlend?128:0))<=3 &&
      std::abs(int(image.rgba[main+3])-expectedAlpha)<=2 &&
      std::abs(image.depth[mainPixel]-expectedDepth)<=1e-6f,
      "RTT 1:1 weighted-opacity/consumer-alpha/depth equation with annotation and layer");
  }
  std::cout << "msaa_weighted_rtt route=" << (route==COIN_RENDER_SCENE_TEXTURE_DIRECT?"direct":"staged")
            << " samples=" << samples << " status=" << int(action.getLastStatus())
            << " rgba=" << image.rgba.size() << " depth=" << image.depth.size()
            << " partial=" << image.partial << " serial=" << image.serial
            << " error=" << action.getLastError().getString() << '\n';
  action.setRenderTarget(nullptr);
  return check(rendered,"RTT producer and consumer publish resolved image");
}
bool transaction(Scene & scene,const CoinRenderCapabilities & caps,
                 CoinRenderSceneTextureMode route) {
  CoinRenderOptions options;options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  options.sampleCount=4;options.sceneTexture=route;options.transparency=COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;
  std::unique_ptr<CoinRenderTarget> target(
    CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
  if(!check(bool(target),"RTT transaction target")) return false;
  target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(side,side));
  action.setBackgroundColor(SbColor4f(0,0,0,0));action.setRenderTarget(target.get());
  auto read=[&]() {
    Image image;target->readbackRGBA(image.rgba);target->readbackDepth(image.depth);
    image.serial=target->getLastSubmissionSerial();return image;
  };
  action.apply(scene.consumer);const Image first=read();
  bool ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS && first.serial>0,
                "RTT A published");
  const uint64_t stride=caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?2:
    route==COIN_RENDER_SCENE_TEXTURE_DIRECT?2:1;
  scene.red->diffuseColor.setValue(0,1,0);
  action.apply(scene.consumer);const Image second=read();
  ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
    second.serial==first.serial+stride && first.rgba!=second.rgba,"RTT B visible revision");
  scene.red->diffuseColor.setValue(1,0,0);
  action.apply(scene.consumer);const Image restored=read();
  ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
    restored.serial==second.serial+stride && restored.rgba==first.rgba &&
    restored.depth==first.depth,"RTT A restored exactly");
  scene.alpha->value=std::numeric_limits<float>::quiet_NaN();
  action.apply(scene.consumer);const Image rejected=read();
  ok &= check(action.getLastStatus()!=CoinRenderAction::SUCCESS &&
    rejected.rgba==restored.rgba && rejected.depth==restored.depth &&
    rejected.serial==restored.serial,"invalid producer preserves consumer frame");
  scene.alpha->value=.5f;
  action.apply(scene.consumer);const Image recovered=read();
  ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
    recovered.rgba==first.rgba && recovered.depth==first.depth &&
    recovered.serial==restored.serial+stride,"RTT producer recovery exact");
  std::cout << "msaa_weighted_rtt transaction route="
            << (route==COIN_RENDER_SCENE_TEXTURE_DIRECT?"direct":"staged")
            << " valid=" << ok << " serial=" << recovered.serial << '\n';
  action.setRenderTarget(nullptr);return ok;
}
bool graphBudget(const CoinRenderCapabilities&caps,CoinRenderSceneTextureMode route){
  if(caps.backend!=COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION)return true;
  Scene scene(true);CoinRenderOptions options;options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  options.sampleCount=4;options.sceneTexture=route;options.transparency=COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
  if(!check(bool(target),"whole-graph negotiated budget target"))return false;
  target->setDepthReadbackEnabled(TRUE);CoinRenderAction action(SbViewportRegion(side,side));
  action.setRenderTarget(target.get());action.setBackgroundColor(SbColor4f(0,0,0,0));
  auto read=[&](){Image f;target->readbackRGBA(f.rgba);target->readbackDepth(f.depth);f.serial=target->getLastSubmissionSerial();return f;};
  action.apply(scene.consumer);const Image anchor=read();
  if(!check(action.getLastStatus()==CoinRenderAction::SUCCESS&&anchor.serial>0&&bool(target->getPimpl()->backend),"producer graph budget anchor"))return false;
  auto*probe=new view_budget_test::Probe(std::move(target->getPimpl()->backend));target->getPimpl()->backend.reset(probe);
  const uint64_t step=route==COIN_RENDER_SCENE_TEXTURE_DIRECT?2:1;bool ok=true;
  auto*producerMode=static_cast<SoTransparencyType*>(scene.producer->getChild(1));
  auto configure=[&](bool active){producerMode->value=active?SoTransparencyType::SORTED_OBJECT_BLEND:SoTransparencyType::NONE;
    scene.red->transparency=active?.5f:0.f;};
  std::vector<SoAnnotation*>notes;
  auto setCount=[&](unsigned count){
    while(notes.size()<count){auto*note=new SoAnnotation;auto*lm=new SoLightModel;lm->model=SoLightModel::BASE_COLOR;note->addChild(lm);
      auto*mat=new SoMaterial;mat->diffuseColor.setValue(0,1,0);note->addChild(mat);
      auto*v=new SoCoordinate3;const SbVec3f q[]={SbVec3f(-.9f,-.9f,0),SbVec3f(-.7f,-.9f,0),SbVec3f(-.7f,-.7f,0),SbVec3f(-.9f,-.7f,0)};
      v->point.setValues(0,4,q);note->addChild(v);auto*f=new SoFaceSet;f->numVertices.set1Value(0,4);note->addChild(f);
      scene.producer->addChild(note);notes.push_back(note);}
    while(notes.size()>count){scene.producer->removeChild(notes.back());notes.pop_back();}
  };
  auto inspect=[&](unsigned count,bool active){setCount(count);configure(active);
    const auto serial=target->getLastSubmissionSerial();const unsigned roots=probe->roots,producers=probe->producers;
    CoinRenderAction planOnly(SbViewportRegion(side,side));
    auto graph=std::make_shared<CoinRenderRttPlan>(route);planOnly.getPimpl()->planOnly=true;
    planOnly.getPimpl()->executionOptions=options;planOnly.getPimpl()->sceneTexturePlan=graph;planOnly.apply(scene.consumer);
    const bool captured=planOnly.getLastStatus()==CoinRenderAction::SUCCESS&&graph->producers.size()==1&&!planOnly.getPimpl()->lastValidPlan.draws.empty();
    const auto result=captured?probe->preflightRtt(*graph,planOnly.getPimpl()->lastValidPlan,SbVec2i32(side,side)):
      CoinRenderSubmitResult{CoinRenderBackendStatus::BACKEND_ERROR,"plan-only producer capture failed"};
    ok &= check(captured&&target->getLastSubmissionSerial()==serial&&probe->roots==roots&&probe->producers==producers,
      "pure graph budget capture/preflight performs no producer or root submission");
    std::cout<<"msaa_weighted_rtt preflight route="<<int(route)<<" annotations="<<count<<" active="<<active
      <<" status="<<int(result.status)<<" diagnostic="<<result.diagnostic<<'\n';return result;
  };
  unsigned fit=0,excess=0;std::string why;
  const bool foundBoundary=view_budget_test::boundary([&](unsigned n){return inspect(n,true);},fit,excess,why);
  ok &= check(foundBoundary,"finite negotiated producer budget: "+why);
  if(!ok)return false;
  const auto active=inspect(excess,true),ordinary=inspect(excess,false);configure(true);
  ok &= check(view_budget_test::rejected(active)&&ordinary.status==CoinRenderBackendStatus::SUCCESS,
    "first rejected graph fits when only producer compositor is disabled");
  if(!ok)return false;
  setCount(fit);action.apply(scene.consumer);const Image stable=read();
  ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS&&stable.serial==anchor.serial+step,"last admitted producer graph publishes once");
  if(!ok)return false;
  setCount(excess);const auto history=probe->admissions.size();const unsigned roots=probe->roots,producers=probe->producers;
  action.apply(scene.consumer);const Image rejected=read();
  std::cout<<"msaa_weighted_rtt negotiated_budget route="<<int(route)<<" accepted="<<fit<<" rejected="<<excess
    <<" status="<<int(action.getLastStatus())<<" diagnostic="<<action.getLastError().getString()<<'\n';
  ok &= check(action.getLastStatus()==CoinRenderAction::UNSUPPORTED&&probe->admissions.size()>history&&
    view_budget_test::rejected(probe->admissions[history])&&probe->roots==roots&&probe->producers==producers&&
    rejected.rgba==stable.rgba&&rejected.depth==stable.depth&&rejected.serial==stable.serial,
    "first whole-graph preflight rejects before producer loop and root publication");
  setCount(fit);action.apply(scene.consumer);const Image recovered=read();
  ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS&&recovered.rgba==stable.rgba&&recovered.depth==stable.depth&&
    recovered.serial==stable.serial+step,"negotiated producer graph budget recovers exactly");
  action.setRenderTarget(nullptr);return ok;
}

bool run() {
  CoinRenderCapabilities caps{};
  const bool queried=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
    &caps,sizeof(caps))==0;
  if(!check(queried && caps.gpu_available &&
    (caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION ||
     caps.backend==COIN_RENDER_EXPERIMENTAL_RUST) &&
    caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN,"RTT requires identified GPU")) return false;
  std::cout << "msaa_weighted_rtt backend=" << caps.backend << " renderer=" << caps.renderer
            << " vendor=" << caps.vendor_id << " adapter=" << caps.adapter_name << '\n';
  const uint32_t counts[]={1,2,4};bool ok=true;
  for(bool producerBlend:{false,true}) {
   Scene scene(producerBlend);
   std::array<Image,3> staged,direct;
   for(size_t i=0;i<3;++i) {
    const bool a=capture(scene,caps,counts[i],COIN_RENDER_SCENE_TEXTURE_STAGED,staged[i]);
    const bool b=capture(scene,caps,counts[i],COIN_RENDER_SCENE_TEXTURE_DIRECT,direct[i]);
    ok &= a && b;
    if(a && b) ok &= check(staged[i].rgba==direct[i].rgba &&
      staged[i].depth==direct[i].depth,"staged/direct resolved pixels and depth agree");
    // Alpha is full for the opaque producer and half for the blended
    // consumer even at 1x. Red encodes the authored triangle's coverage.
    if(a) ok &= check(sampleLevels(staged[i],counts[i]),
                      "RTT staged fractional red coverage resolves requested sample count");
    if(b) ok &= check(sampleLevels(direct[i],counts[i]),
                      "RTT direct fractional red coverage resolves requested sample count");
  }
  if(ok) ok &= check(staged[0].rgba!=staged[1].rgba && staged[0].rgba!=staged[2].rgba,
                     "RTT 2x/4x output must differ from 1x");
  ok &= transaction(scene,caps,COIN_RENDER_SCENE_TEXTURE_STAGED);
  ok &= transaction(scene,caps,COIN_RENDER_SCENE_TEXTURE_DIRECT);
  }
  ok &= graphBudget(caps,COIN_RENDER_SCENE_TEXTURE_STAGED);
  ok &= graphBudget(caps,COIN_RENDER_SCENE_TEXTURE_DIRECT);
  return ok;
}
}
int main(int argc,char ** argv) {
  if(argc!=2 || std::string(argv[1])!="--rtt") return 2;
  SoDB::init();CoinRenderAction::initClass();
  return run()?0:1;
}
