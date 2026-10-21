#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderMsaaShadowFixture.h"
#include "CoinRenderViewBudgetTest.h"
#include "CoinRenderTestEnvironment.h"
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include "actions/CoinRenderActionP.h"
#include "rendering/coinrender/CoinRenderBackend.h"
#include "rendering/coinrender/CoinRenderRttCore.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <limits>
using namespace msaa_shadow_test;
namespace {
constexpr int extent=64;
struct Result{
  std::vector<uint8_t> rgba;
  std::vector<float> depth;
  uint64_t serial=0;
};
SoOrthographicCamera *camera(){
  auto *result=new SoOrthographicCamera;
  result->position.setValue(0,0,2);result->height=2;
  result->nearDistance=1;result->farDistance=3;return result;
}
void quad(SoSeparator *root,bool textureCoordinates){
  if(textureCoordinates){
    auto *uv=new SoTextureCoordinate2;
    const SbVec2f values[]={SbVec2f(0,0),SbVec2f(1,0),
      SbVec2f(1,1),SbVec2f(0,1)};
    uv->point.setValues(0,4,values);root->addChild(uv);
  }
  auto *coords=new SoCoordinate3;
  const SbVec3f corners[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),
    SbVec3f(1,1,0),SbVec3f(-1,1,0)};
  coords->point.setValues(0,4,corners);root->addChild(coords);
  auto *face=new SoFaceSet;face->numVertices.set1Value(0,4);
  root->addChild(face);
}
struct Pair{
  Scene shadow;
  SoSeparator *source=new SoSeparator,*consumer=new SoSeparator;
  SoSceneTexture2 *texture=new SoSceneTexture2;
  bool producerShadow;
  explicit Pair(bool shadowProducer):shadow(1,false),producerShadow(shadowProducer){
    source->ref();consumer->ref();
    if(producerShadow){
      texture->scene=shadow.root;
      consumer->addChild(camera());
      auto *unlit=new SoLightModel;unlit->model=SoLightModel::BASE_COLOR;
      consumer->addChild(unlit);
      auto *white=new SoMaterial;white->diffuseColor.setValue(1,1,1);
      consumer->addChild(white);
      auto *quality=new SoComplexity;quality->textureQuality=.1f;
      consumer->addChild(quality);
      texture->size.setValue(extent,extent);texture->type=SoSceneTexture2::RGBA8;
      texture->model=SoSceneTexture2::REPLACE;
      texture->backgroundColor.setValue(0,0,0,0);
      texture->wrapS=SoSceneTexture2::CLAMP;
      texture->wrapT=SoSceneTexture2::CLAMP;
      consumer->addChild(texture);quad(consumer,true);
    }else{
      source->addChild(camera());
      auto *unlit=new SoLightModel;unlit->model=SoLightModel::BASE_COLOR;
      source->addChild(unlit);
      auto *material=new SoMaterial;
      material->diffuseColor.setValue(.9f,.65f,.3f);source->addChild(material);
      quad(source,false);
      texture->scene=source;
      auto *quality=new SoComplexity;quality->textureQuality=.1f;
      shadow.root->insertChild(quality,2);
      texture->size.setValue(extent,extent);texture->type=SoSceneTexture2::RGBA8;
      texture->model=SoSceneTexture2::MODULATE;
      texture->backgroundColor.setValue(0,0,0,0);
      texture->wrapS=SoSceneTexture2::CLAMP;
      texture->wrapT=SoSceneTexture2::CLAMP;
      shadow.receiver->insertChild(texture,1);
      auto *uv=new SoTextureCoordinate2;
      const SbVec2f values[]={SbVec2f(0,0),SbVec2f(1,0),
        SbVec2f(1,1),SbVec2f(0,1)};
      uv->point.setValues(0,4,values);
      shadow.receiver->insertChild(uv,3);
    }
  }
  SoNode *root(){return producerShadow?static_cast<SoNode *>(consumer):
    static_cast<SoNode *>(shadow.root);}
  ~Pair(){consumer->unref();source->unref();}
};
bool render(Pair &pair,const CoinRenderCapabilities &caps,unsigned samples,
            CoinRenderSceneTextureMode route,Result &out,const char *label){
  auto selected=options(caps,samples,route);
  std::unique_ptr<CoinRenderTarget> target(
    CoinRenderTarget::createOffscreen(SbVec2i32(extent,extent),selected));
  if(!check(target && target->getOptions().renderer==caps.renderer &&
    target->getOptions().sampleCount==samples &&
    target->getOptions().sceneTexture==route,
    "RTT target identifies renderer/route/sample count"))return false;
  target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(extent,extent));
  action.setBackgroundColor(SbColor4f(0,0,0,0));
  action.setRenderTarget(target.get());action.apply(pair.root());
  target->readbackRGBA(out.rgba);target->readbackDepth(out.depth);
  out.serial=target->getLastSubmissionSerial();
  std::cout<<"msaa_shadow_rtt phase="<<label
           <<" producer_shadow="<<pair.producerShadow
           <<" route="<<(route==COIN_RENDER_SCENE_TEXTURE_DIRECT?"direct":"staged")
           <<" samples="<<samples<<" status="<<int(action.getLastStatus())
           <<" serial="<<out.serial
           <<" error="<<action.getLastError().getString()<<'\n';
  action.setRenderTarget(nullptr);
  return check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
    out.rgba.size()==size_t(extent)*extent*4 &&
    out.depth.size()==size_t(extent)*extent && out.serial>0,
    "RTT producer/consumer publishes one resolved frame");
}
unsigned changed(const Result &a,const Result &b){
  if(a.rgba.size()!=b.rgba.size())return 0;
  unsigned pixels=0;
  for(size_t i=0;i<a.rgba.size();i+=4){
    int loss=0;for(int c=0;c<3;++c)
      loss+=std::abs(int(a.rgba[i+c])-int(b.rgba[i+c]));
    pixels+=(loss>9);
  }
  return pixels;
}
bool transaction(Pair &pair,const CoinRenderCapabilities &caps,unsigned samples,
                 CoinRenderSceneTextureMode route){
  const auto selected=options(caps,samples,route);
  std::unique_ptr<CoinRenderTarget> target(
    CoinRenderTarget::createOffscreen(SbVec2i32(extent,extent),selected));
  if(!check(bool(target),"transaction RTT target"))return false;
  target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(extent,extent));
  action.setBackgroundColor(SbColor4f(0,0,0,0));
  action.setRenderTarget(target.get());
  auto capture=[&](Result &result){
    target->readbackRGBA(result.rgba);target->readbackDepth(result.depth);
    result.serial=target->getLastSubmissionSerial();
  };
  action.apply(pair.root());Result anchor;capture(anchor);
  bool ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
    anchor.rgba.size()==size_t(extent)*extent*4 &&
    anchor.depth.size()==size_t(extent)*extent && anchor.serial>0,
    "valid RTT anchor published before invalid producer state");
  auto *invalid=new SoAlphaTest;
  invalid->function=SoAlphaTest::GREATER;
  invalid->value=std::numeric_limits<float>::quiet_NaN();
  SoSeparator *invalidRoot=pair.producerShadow?pair.shadow.root:
    static_cast<SoSeparator *>(pair.root());
  invalidRoot->addChild(invalid);
  action.apply(pair.root());Result rejected;capture(rejected);
  std::cout<<"msaa_shadow_rtt invalid producer_shadow="<<pair.producerShadow
    <<" route="<<(route==COIN_RENDER_SCENE_TEXTURE_DIRECT?"direct":"staged")
    <<" samples="<<samples<<" status="<<int(action.getLastStatus())
    <<" serial="<<rejected.serial
    <<" error="<<action.getLastError().getString()<<'\n';
  ok &= check(action.getLastStatus()!=CoinRenderAction::SUCCESS &&
    rejected.rgba==anchor.rgba && rejected.depth==anchor.depth &&
    rejected.serial==anchor.serial,
    "invalid RTT producer/consumer preserves published consumer frame");
  invalidRoot->removeChild(invalid);
  action.apply(pair.root());Result restored;capture(restored);
  // BGFX STAGED owns a separate producer backend; DIRECT shares the target.
  // wgpu uses a device-global serial for both producer and consumer routes.
  const uint64_t expectedAdvance=
    caps.backend==COIN_RENDER_EXPERIMENTAL_RUST ||
    route==COIN_RENDER_SCENE_TEXTURE_DIRECT ? 2u : 1u;
  std::cout<<"msaa_shadow_rtt recovery route="
    <<(route==COIN_RENDER_SCENE_TEXTURE_DIRECT?"direct":"staged")
    <<" producer_shadow="<<pair.producerShadow
    <<" anchor_serial="<<anchor.serial
    <<" restored_serial="<<restored.serial
    <<" status="<<int(action.getLastStatus())
    <<" rgba_equal="<<(restored.rgba==anchor.rgba)
    <<" depth_equal="<<(restored.depth==anchor.depth)<<'\n';
  ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
    restored.rgba==anchor.rgba && restored.depth==anchor.depth &&
    restored.serial==anchor.serial+expectedAdvance,
    "RTT recovery publishes one root frame and one DIRECT producer token");
  action.setRenderTarget(nullptr);return ok;
}
using GraphBudgetProbe=view_budget_test::Probe;
bool graphBudget(const CoinRenderCapabilities &caps,
                 CoinRenderSceneTextureMode route){
  if(caps.backend!=COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION)return true;
  // Negotiate the actual whole-graph boundary. BGFX ViewId is 16 bits;
  // this finite API domain is a search guard, not a target capacity claim.
  constexpr unsigned viewIdDomain=unsigned(std::numeric_limits<uint16_t>::max())+1u;
  Pair pair(false);
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(
    SbVec2i32(extent,extent),options(caps,4,route)));
  if(!check(bool(target),"RTT graph budget physical target"))return false;
  auto&state=target->getPimpl().get();
  if(!check(state.prepareBackend()==CoinRenderBackendStatus::SUCCESS&&bool(state.backend),"RTT graph budget backend prepared"))return false;
  auto*probe=new GraphBudgetProbe(std::move(state.backend));state.backend.reset(probe);
  target->setDepthReadbackEnabled(TRUE);CoinRenderAction action(SbViewportRegion(extent,extent));
  action.setBackgroundColor(SbColor4f(0,0,0,0));action.setRenderTarget(target.get());
  auto capture=[&](Result&result){action.apply(pair.root());target->readbackRGBA(result.rgba);
    target->readbackDepth(result.depth);result.serial=target->getLastSubmissionSerial();return action.getLastStatus();};
  const uint64_t expectedAdvance=route==COIN_RENDER_SCENE_TEXTURE_DIRECT?2u:1u;
  Result anchor;auto status=capture(anchor);
  bool ok=check(status==CoinRenderAction::SUCCESS&&anchor.serial>0&&
    anchor.rgba.size()==size_t(extent)*extent*4&&anchor.depth.size()==size_t(extent)*extent,
    "RTT shadow producer/consumer budget anchor published");
  if(!ok)return false;
  std::vector<SoAnnotation*>annotations;
  auto setCount=[&](unsigned count){
    auto*root=static_cast<SoSeparator*>(pair.root());
    while(annotations.size()<count){auto*annotation=new SoAnnotation;
      auto*unlit=new SoLightModel;unlit->model=SoLightModel::BASE_COLOR;annotation->addChild(unlit);
      auto*color=new SoMaterial;color->diffuseColor.setValue(0,1,0);annotation->addChild(color);quad(annotation,false);
      root->addChild(annotation);annotations.push_back(annotation);}
    while(annotations.size()>count){root->removeChild(annotations.back());annotations.pop_back();}
  };
  auto inspect=[&](unsigned count,bool shadowsActive){
    setCount(count);pair.shadow.group->isActive=shadowsActive;
    const auto serial=target->getLastSubmissionSerial();const unsigned roots=probe->roots,producers=probe->producers;
    CoinRenderAction planOnly(SbViewportRegion(extent,extent));auto graph=std::make_shared<CoinRenderRttPlan>(route);
    planOnly.getPimpl()->planOnly=true;planOnly.getPimpl()->executionOptions=options(caps,4,route);
    planOnly.getPimpl()->sceneTexturePlan=graph;planOnly.apply(pair.root());
    const bool captured=planOnly.getLastStatus()==CoinRenderAction::SUCCESS&&graph->producers.size()==1&&
      !planOnly.getPimpl()->lastValidPlan.draws.empty();
    const auto admission=captured?probe->preflightRtt(*graph,planOnly.getPimpl()->lastValidPlan,SbVec2i32(extent,extent)):
      CoinRenderSubmitResult{CoinRenderBackendStatus::BACKEND_ERROR,"RTT plan-only capture failed"};
    ok &= check(captured&&target->getLastSubmissionSerial()==serial&&probe->roots==roots&&probe->producers==producers,
      "plan-only captures one producer and preflight performs no root/producer publication");
    std::cout<<"msaa_shadow_rtt graph_preflight route="<<int(route)<<" annotations="<<count<<" shadows="<<shadowsActive
      <<" status="<<int(admission.status)<<" diagnostic="<<admission.diagnostic<<'\n';return admission;
  };
  auto viewRejection=[](const CoinRenderSubmitResult&result){return result.status==CoinRenderBackendStatus::UNSUPPORTED&&
    (result.diagnostic.find("view")!=std::string::npos||result.diagnostic.find("budget")!=std::string::npos);};
  unsigned low=0,high=1;bool exhausted=false;
  const auto initial=inspect(0,true);ok &= check(initial.status==CoinRenderBackendStatus::SUCCESS,"zero-annotation graph passes pure budget preflight");
  if(!ok)return false;
  while(high<=viewIdDomain){const auto admission=inspect(high,true);if(!ok)return false;
    if(admission.status==CoinRenderBackendStatus::SUCCESS){low=high;if(high==viewIdDomain)break;high=std::min(viewIdDomain,high*2u);continue;}
    if(!check(viewRejection(admission),"search rejects specifically at graph views/budget, not an unrelated failure"))return false;
    exhausted=true;break;
  }
  if(!check(exhausted&&low>0&&high<=viewIdDomain,"RTT shadows and annotations reach a finite negotiated view boundary"))return false;
  while(high-low>1){const unsigned mid=low+(high-low)/2;const auto admission=inspect(mid,true);if(!ok)return false;
    if(admission.status==CoinRenderBackendStatus::SUCCESS)low=mid;
    else{if(!check(viewRejection(admission),"binary boundary has a view/budget rejection"))return false;high=mid;}}
  const auto withShadow=inspect(high,true),withoutShadow=inspect(high,false);pair.shadow.group->isActive=TRUE;
  ok &= check(ok&&viewRejection(withShadow)&&withoutShadow.status==CoinRenderBackendStatus::SUCCESS,
    "first rejected graph fits when only the consumer shadow map is disabled");
  if(!ok)return false;
  // Execute only adjacent boundary controls, not each search candidate.
  setCount(low);Result stable;status=capture(stable);
  ok &= check(status==CoinRenderAction::SUCCESS&&stable.serial==anchor.serial+expectedAdvance,
    "last admitted graph publishes one root and DIRECT producer when selected");
  if(!ok)return false;
  setCount(high);const unsigned roots=probe->roots,producers=probe->producers,preflights=probe->preflights;const auto history=probe->admissions.size();
  Result rejected;status=capture(rejected);const std::string error=action.getLastError().getString();
  std::cout<<"msaa_shadow_rtt budget route="<<int(route)<<" accepted="<<low<<" rejected="<<high
    <<" status="<<int(status)<<" error="<<error<<'\n';
  ok &= check(status==CoinRenderAction::UNSUPPORTED&&
    (error.find("view")!=std::string::npos||error.find("budget")!=std::string::npos)&&
    rejected.rgba==stable.rgba&&rejected.depth==stable.depth&&rejected.serial==stable.serial&&
    probe->preflights==preflights+1&&probe->admissions.size()==history+1&&viewRejection(probe->admissions[history])&&probe->roots==roots&&probe->producers==producers,
    "whole-graph view rejection preserves RGBA/depth/serial and stops before root or DIRECT producer submission");
  setCount(low);Result restored;status=capture(restored);
  std::cout<<"msaa_shadow_rtt budget_recovery route="<<int(route)<<" anchor_serial="<<stable.serial
    <<" restored_serial="<<restored.serial<<" status="<<int(status)<<" rgba_equal="<<(restored.rgba==stable.rgba)
    <<" depth_equal="<<(restored.depth==stable.depth)<<'\n';
  ok &= check(status==CoinRenderAction::SUCCESS&&restored.rgba==stable.rgba&&restored.depth==stable.depth&&
    restored.serial==stable.serial+expectedAdvance,"negotiated graph view boundary recovers exactly");
  action.setRenderTarget(nullptr);return ok;
}

bool stagedCoexistence(const CoinRenderCapabilities&caps){
  if(caps.backend!=COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION)return true;
  // Producer zero is deliberately small but executes one real map. Its
  // existing one-shot allocation fault witnesses whether STAGED reached it.
  Pair pair(true);
  std::shared_ptr<SoSeparator>large(new SoSeparator,[](SoSeparator*p){p->unref();});large->ref();
  large->addChild(camera());auto*model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;large->addChild(model);
  auto*material=new SoMaterial;material->diffuseColor.setValue(.25f,.5f,.75f);large->addChild(material);quad(large.get(),false);
  auto*second=new SoSeparator;auto*image=new SoSceneTexture2;image->scene=large.get();image->size.setValue(extent,extent);
  image->type=SoSceneTexture2::RGBA8;image->model=SoSceneTexture2::REPLACE;image->backgroundColor.setValue(0,0,0,1);
  second->addChild(image);quad(second,true);pair.consumer->addChild(second);
  auto makeTarget=[&](CoinRenderSceneTextureMode route){return std::unique_ptr<CoinRenderTarget>(
    CoinRenderTarget::createOffscreen(SbVec2i32(extent,extent),options(caps,4,route)));};
  auto wrap=[&](CoinRenderTarget&t){t.setDepthReadbackEnabled(TRUE);auto&state=t.getPimpl().get();
    if(state.prepareBackend()!=CoinRenderBackendStatus::SUCCESS||!state.backend)return static_cast<GraphBudgetProbe*>(nullptr);
    auto*p=new GraphBudgetProbe(std::move(state.backend));state.backend.reset(p);return p;};
  auto read=[](CoinRenderTarget&t){Result r;t.readbackRGBA(r.rgba);t.readbackDepth(r.depth);r.serial=t.getLastSubmissionSerial();return r;};
  // Keep only a small live anchor across target destruction. A discovery target
  // must release its negotiated high-water span before the pressure target is
  // created; retained reservations must not supply the alleged coexistence gap.
  auto anchorTarget=makeTarget(COIN_RENDER_SCENE_TEXTURE_STAGED);
  if(!check(anchorTarget&&wrap(*anchorTarget),"coexistence runtime anchor prepared"))return false;
  CoinRenderAction anchorAction(SbViewportRegion(extent,extent));anchorAction.setRenderTarget(anchorTarget.get());
  anchorAction.setBackgroundColor(SbColor4f(0,0,0,0));anchorAction.apply(pair.root());
  if(!check(anchorAction.getLastStatus()==CoinRenderAction::SUCCESS&&anchorTarget->getLastSubmissionSerial()>0,
    "small two-producer runtime anchor published"))return false;
  std::vector<SoAnnotation*>parentNotes,producerNotes;
  auto setNotes=[&](SoSeparator*root,std::vector<SoAnnotation*>&notes,unsigned count){
    while(notes.size()<count){auto*n=new SoAnnotation;auto*lm=new SoLightModel;lm->model=SoLightModel::BASE_COLOR;n->addChild(lm);
      auto*m=new SoMaterial;m->diffuseColor.setValue(0,1,0);n->addChild(m);quad(n,false);root->addChild(n);notes.push_back(n);}
    while(notes.size()>count){root->removeChild(notes.back());notes.pop_back();}
  };
  auto workload=[&](unsigned parent,unsigned producer){setNotes(pair.consumer,parentNotes,parent);setNotes(large.get(),producerNotes,producer);};
  bool ok=true;
  auto inspect=[&](CoinRenderTarget&target,GraphBudgetProbe&probe,unsigned parent,unsigned producer){
    workload(parent,producer);const auto serial=target.getLastSubmissionSerial();const unsigned roots=probe.roots,producers=probe.producers;
    CoinRenderAction plan(SbViewportRegion(extent,extent));auto g=std::make_shared<CoinRenderRttPlan>(target.getOptions().sceneTexture);
    plan.getPimpl()->planOnly=true;plan.getPimpl()->executionOptions=target.getOptions();plan.getPimpl()->sceneTexturePlan=g;plan.apply(pair.root());
    const bool captured=plan.getLastStatus()==CoinRenderAction::SUCCESS&&g->producers.size()==2&&
      g->producers[0].plan.shadowLights.size()==1&&g->producers[1].plan.shadowLights.empty()&&
      (producer==0||std::count_if(g->producers[1].plan.draws.begin(),g->producers[1].plan.draws.end(),[](const CoinRenderDrawPacket&d){return !d.shadowLightSlot;})>
        std::count_if(g->producers[0].plan.draws.begin(),g->producers[0].plan.draws.end(),[](const CoinRenderDrawPacket&d){return !d.shadowLightSlot;}));
    ok &= check(captured,"coexistence graph orders a small real-map producer before the independently sized producer");
    if(!captured)return CoinRenderSubmitResult{CoinRenderBackendStatus::BACKEND_ERROR,"coexistence capture failed"};
    const auto r=probe.preflightRtt(*g,plan.getPimpl()->lastValidPlan,SbVec2i32(extent,extent));
    ok &= check(target.getLastSubmissionSerial()==serial&&probe.roots==roots&&probe.producers==producers,
      "coexistence discovery preflight does not submit or publish");
    std::cout<<"msaa_shadow_rtt coexistence_preflight route="<<int(g->mode)<<" parent="<<parent<<" producer2="<<producer
      <<" status="<<int(r.status)<<" diagnostic="<<r.diagnostic<<'\n';return r;
  };
  auto discovery=makeTarget(COIN_RENDER_SCENE_TEXTURE_DIRECT);
  auto*discoveryProbe=discovery?wrap(*discovery):nullptr;
  if(!check(discoveryProbe!=nullptr,"DIRECT discovery target prepared"))return false;
  unsigned fit=0,excess=0;std::string why;
  const bool foundBoundary=view_budget_test::boundary([&](unsigned n){return inspect(*discovery,*discoveryProbe,n,n);},fit,excess,why);
  ok &= check(foundBoundary,"finite individual/shared-max DIRECT boundary: "+why);if(!ok)return false;
  const unsigned pressure=fit*3u/4u;
  ok &= check(pressure>0&&pressure<fit&&inspect(*discovery,*discoveryProbe,pressure,pressure).status==CoinRenderBackendStatus::SUCCESS,
    "both large subplans fit DIRECT safely below the measured individual boundary");
  if(!ok)return false;
  discovery.reset();discoveryProbe=nullptr;
  auto staged=makeTarget(COIN_RENDER_SCENE_TEXTURE_STAGED);auto*stagedProbe=staged?wrap(*staged):nullptr;
  if(!check(stagedProbe!=nullptr,"fresh STAGED pressure target prepared after discovery release"))return false;
  const auto rejected=inspect(*staged,*stagedProbe,pressure,pressure);
  const auto reduced=inspect(*staged,*stagedProbe,pressure,0);
  ok &= check(view_budget_test::rejected(rejected)&&reduced.status==CoinRenderBackendStatus::SUCCESS,
    "STAGED rejects coexistence of individually admitted large spans; reducing only producer2 fits the unchanged large parent");
  if(!ok)return false;
  CoinRenderAction action(SbViewportRegion(extent,extent));action.setRenderTarget(staged.get());action.setBackgroundColor(SbColor4f(0,0,0,0));
  workload(pressure,0);action.apply(pair.root());const Result stable=read(*staged);
  if(!check(action.getLastStatus()==CoinRenderAction::SUCCESS&&stable.serial>0,"large parent with simple producer2 publishes"))return false;
  workload(pressure,pressure);const auto history=stagedProbe->admissions.size();const unsigned roots=stagedProbe->roots,producers=stagedProbe->producers;
  const char*fault="COIN_BGFX_TEST_SHADOW_MAP_ALLOC_ONCE";const char*old=std::getenv(fault);const std::string previous=old?old:"";const bool had=old!=nullptr;
  coinRenderTestSetEnvironment(fault,"1");action.apply(pair.root());const Result denied=read(*staged);
  const bool untouched=std::getenv(fault)!=nullptr;coinRenderTestSetEnvironment(fault,nullptr);
  ok &= check(action.getLastStatus()==CoinRenderAction::UNSUPPORTED&&stagedProbe->admissions.size()==history+1&&
    view_budget_test::rejected(stagedProbe->admissions[history])&&stagedProbe->roots==roots&&stagedProbe->producers==producers&&untouched&&
    denied.serial==stable.serial&&denied.rgba==stable.rgba&&denied.depth==stable.depth,
    "STAGED first preflight preserves the first producer map fault, RGBA/depth/serial and all submissions");
  workload(pressure,0);action.apply(pair.root());const Result restored=read(*staged);
  ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS&&restored.serial==stable.serial+1&&restored.rgba==stable.rgba&&restored.depth==stable.depth,
    "reducing only producer2 recovers the exact frame on the same STAGED target");
  action.setRenderTarget(nullptr);staged.reset();stagedProbe=nullptr;
  // Drop the pressure reservation before constructing the DIRECT positive
  // control. Full-screen opaque parent annotations select the first texture,
  // so changing only the hidden second producer preserves consumer geometry
  // and color while both producer submissions must still be observed.
  auto direct=makeTarget(COIN_RENDER_SCENE_TEXTURE_DIRECT);auto*directProbe=direct?wrap(*direct):nullptr;
  if(!check(directProbe!=nullptr,"fresh DIRECT positive control prepared")){if(had)coinRenderTestSetEnvironment(fault,previous.c_str());return false;}
  ok &= check(inspect(*direct,*directProbe,pressure,pressure).status==CoinRenderBackendStatus::SUCCESS,
    "DIRECT still admits both large subplans after the STAGED target is released");
  CoinRenderAction directAction(SbViewportRegion(extent,extent));directAction.setRenderTarget(direct.get());
  directAction.setBackgroundColor(SbColor4f(0,0,0,0));const unsigned directRoots=directProbe->roots,directProducers=directProbe->producers;
  directAction.apply(pair.root());const Result shared=read(*direct);
  bool equivalent=shared.rgba.size()==stable.rgba.size()&&shared.depth==stable.depth;
  if(equivalent)for(size_t i=0;i<shared.rgba.size();++i)equivalent &= std::abs(int(shared.rgba[i])-int(stable.rgba[i]))<=3;
  ok &= check(directAction.getLastStatus()==CoinRenderAction::SUCCESS&&shared.serial>0&&directProbe->roots==directRoots+1&&
    directProbe->producers==directProducers+2&&equivalent,"DIRECT publishes both large producers and consumer with the same visible color/depth");
  std::cout<<"msaa_shadow_rtt coexistence direct_fit="<<fit<<" direct_excess="<<excess<<" pressure="<<pressure<<" fault_preserved="<<untouched
    <<" direct_status="<<int(directAction.getLastStatus())<<" direct_error="<<directAction.getLastError().getString()<<'\n';
  if(had)coinRenderTestSetEnvironment(fault,previous.c_str());
  directAction.setRenderTarget(nullptr);anchorAction.setRenderTarget(nullptr);return ok;
}

bool run(){
  const auto caps=capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN);
  if(!check(caps.gpu_available && caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN &&
     (caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION ||
      caps.backend==COIN_RENDER_EXPERIMENTAL_RUST),
      "RTT uses identified physical GPU"))return false;
  bool ok=true;
  for(bool producerShadow:{true,false})for(unsigned samples:{1u,2u,4u}){
    Pair pair(producerShadow);
    Result staged,direct,movedStaged,movedDirect,restoredStaged,restoredDirect;
    const bool a=render(pair,caps,samples,COIN_RENDER_SCENE_TEXTURE_STAGED,
      staged,"caster A staged");
    const bool b=render(pair,caps,samples,COIN_RENDER_SCENE_TEXTURE_DIRECT,
      direct,"caster A direct");
    if(!(a&&b)){ok=false;continue;}
    pair.shadow.casterX(0,.7f);
    const bool c=render(pair,caps,samples,COIN_RENDER_SCENE_TEXTURE_STAGED,
      movedStaged,"caster B staged");
    const bool d=render(pair,caps,samples,COIN_RENDER_SCENE_TEXTURE_DIRECT,
      movedDirect,"caster B direct");
    pair.shadow.casterX(0,0.f);
    const bool e=render(pair,caps,samples,COIN_RENDER_SCENE_TEXTURE_STAGED,
      restoredStaged,"caster A restored staged");
    const bool f=render(pair,caps,samples,COIN_RENDER_SCENE_TEXTURE_DIRECT,
      restoredDirect,"caster A restored direct");
    if(!(c&&d&&e&&f)){ok=false;continue;}
    std::cout<<"msaa_shadow_rtt producer_shadow="<<producerShadow
             <<" samples="<<samples<<" staged_changed="
             <<changed(staged,movedStaged)
             <<" direct_changed="<<changed(direct,movedDirect)<<'\n';
    ok &= check(changed(staged,movedStaged)>5 &&
      changed(direct,movedDirect)>5 &&
      staged.rgba==restoredStaged.rgba &&
      staged.depth==restoredStaged.depth &&
      direct.rgba==restoredDirect.rgba &&
      direct.depth==restoredDirect.depth,
      "RTT real shadow moves and restores exactly in producer or consumer");
    size_t mismatch=0;for(size_t i=0;i<staged.rgba.size();++i)
      mismatch+=(std::abs(int(staged.rgba[i])-int(direct.rgba[i]))>3);
    ok &= check(mismatch==0 && staged.depth==direct.depth,
      "RTT DIRECT/STAGED 1:1 resolved RGBA/depth agree");
    ok &= transaction(pair,caps,samples,COIN_RENDER_SCENE_TEXTURE_STAGED);
    ok &= transaction(pair,caps,samples,COIN_RENDER_SCENE_TEXTURE_DIRECT);
  }
  ok &= graphBudget(caps,COIN_RENDER_SCENE_TEXTURE_STAGED);
  ok &= graphBudget(caps,COIN_RENDER_SCENE_TEXTURE_DIRECT);
  ok &= stagedCoexistence(caps);
  return ok;
}
}
int main(int argc,char **argv){
  if(argc!=2 || std::string(argv[1])!="--rtt")return 2;
  SoDB::init();CoinRenderAction::initClass();return run()?0:1;
}
