#ifndef COIN_RENDER_SHADOW_MULTITEXTURE_ALPHA_VIEW_PRESSURE_H
#define COIN_RENDER_SHADOW_MULTITEXTURE_ALPHA_VIEW_PRESSURE_H
#include "CoinRenderViewBudgetTest.h"
#include <Inventor/nodes/SoAnnotation.h>
namespace {
struct AlphaViewRig {
  std::unique_ptr<CoinRenderTarget>target;view_budget_test::Probe*probe=nullptr;
  CoinRenderAction action{SbViewportRegion(side,side)};
  explicit AlphaViewRig(const CoinRenderOptions&options){
    target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));if(!target)return;
    // MSAA targets default to GPU-only depth. Freeze the same output policy
    // before backend preparation that the real publications will use.
    target->setDepthReadbackEnabled(TRUE);
    auto&state=target->getPimpl().get();const auto status=state.prepareBackend();
    if(status!=CoinRenderBackendStatus::SUCCESS||!state.backend){std::cout<<"alpha_views prepare="<<int(status)<<" error="<<target->getLastError()<<'\n';return;}
    probe=new view_budget_test::Probe(std::move(state.backend));state.backend.reset(probe);
    action.setRenderTarget(target.get());action.setBackgroundColor(SbColor4f(16.f/255,24.f/255,32.f/255,1));action.setSortedLayersNumPasses(4);
  }
  ~AlphaViewRig(){action.setRenderTarget(nullptr);}
  Frame read(){Frame f;target->readbackRGBA(f.rgba);target->readbackDepth(f.depth);f.serial=target->getLastSubmissionSerial();return f;}
  bool render(SoNode*root,Frame&f,bool rtt,const char*phase){
    const auto before=target->getLastSubmissionSerial();const unsigned roots=probe->roots,producers=probe->producers;
    action.apply(root);f=read();const unsigned child=rtt&&target->getOptions().sceneTexture==COIN_RENDER_SCENE_TEXTURE_DIRECT?1:0;
    std::cout<<"alpha_views phase="<<phase<<" status="<<int(action.getLastStatus())<<" serial="<<before<<','<<f.serial<<" error="<<action.getLastError().getString()<<'\n';
    return check(action.getLastStatus()==CoinRenderAction::SUCCESS&&f.rgba.size()==size_t(side)*side*4&&f.depth.size()==size_t(side)*side&&
      f.serial==before+1+child&&probe->roots==roots+1&&probe->producers==producers+child,"view-pressure complete publication and DIRECT producer accounting");
  }
};
bool alphaViewPressure(WindowPair&h){
  if(h.caps.backend!=COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION)return false;
  auto options=h.off->getOptions();AlphaScene scene;
  scene.mode->value=SoGLRenderAction::SORTED_OBJECT_BLEND;scene.lighting->model=SoLightModel::PHONG;scene.receiverStyle->style=SoShadowStyle::SHADOWED;
  std::shared_ptr<SoSeparator>peerRoot(new SoSeparator,[](SoSeparator*p){p->unref();});peerRoot->ref();
  auto*camera=new SoOrthographicCamera;camera->position.setValue(0,0,6);camera->height=4;camera->nearDistance=1;camera->farDistance=20;peerRoot->addChild(camera);
  auto*light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;peerRoot->addChild(light);
  auto*material=new SoMaterial;material->diffuseColor.setValue(.125f,.75f,.25f);peerRoot->addChild(material);
  const auto quad=[](SoSeparator*root,float low,float high){auto*points=new SoCoordinate3;
    const SbVec3f p[]={SbVec3f(low,low,-1),SbVec3f(high,low,-1),SbVec3f(high,high,-1),SbVec3f(low,high,-1)};
    points->point.setValues(0,4,p);root->addChild(points);auto*face=new SoFaceSet;face->numVertices.set1Value(0,4);root->addChild(face);};
  quad(peerRoot.get(),-1.8f,1.8f);
  std::vector<SoAnnotation*>peerNotes,rootNotes;
  const auto notes=[&](SoSeparator*root,std::vector<SoAnnotation*>&list,unsigned count){
    while(list.size()<count){auto*note=new SoAnnotation;auto*lm=new SoLightModel;lm->model=SoLightModel::BASE_COLOR;note->addChild(lm);
      auto*color=new SoMaterial;color->diffuseColor.setValue(.125f,.25f,.875f);note->addChild(color);quad(note,-1.9f,-1.7f);root->addChild(note);list.push_back(note);}
    while(list.size()>count){root->removeChild(list.back());list.pop_back();}
  };
  bool ok=true;
  const auto inspect=[&](AlphaViewRig&rig,SoNode*root,bool focal){const auto serial=rig.target->getLastSubmissionSerial();const unsigned roots=rig.probe->roots,producers=rig.probe->producers;
    CoinRenderAction plan(SbViewportRegion(side,side));plan.setBackgroundColor(SbColor4f(16.f/255,24.f/255,32.f/255,1));plan.setSortedLayersNumPasses(4);
    plan.getPimpl()->planOnly=true;plan.getPimpl()->executionOptions=options;auto graph=std::make_shared<CoinRenderRttPlan>(options.sceneTexture);
    plan.getPimpl()->sceneTexturePlan=graph;plan.apply(root);bool captured=plan.getLastStatus()==CoinRenderAction::SUCCESS&&graph->producers.size()==size_t(focal?1:0);
    if(focal){const auto&frame=plan.getPimpl()->lastValidPlan;unsigned receivers=0;CoinRenderShadowPlan rootMaps;std::string shadowDiagnostic;
      const bool planned=coin_render_plan_shadows(frame,rootMaps,shadowDiagnostic);
      const unsigned visible=unsigned(std::count_if(rootMaps.passes.begin(),rootMaps.passes.end(),[](const CoinRenderShadowPass&p){return p.visible;}));
      captured &= planned&&visible==unsigned(scene.group->isActive.getValue()?8:0);
      for(const auto&draw:frame.draws)if(!draw.shadowLightSlot&&draw.sourceNodeId==scene.mesh->getNodeId()){++receivers;const auto&state=frame.renderStates[draw.renderStateSlot];
        for(int unit:scene.units)captured &= coin_render_texture_unit(state,size_t(unit)).enabled;
        const auto unit0=coin_render_texture_unit(state,0);captured &= unit0.imageSlot<frame.textures.size()&&frame.textures[unit0.imageSlot].producerId!=0;
        captured &= state.transparencyType==SoGLRenderAction::SORTED_OBJECT_BLEND;}
      captured &= receivers>0;}
    const auto result=captured?rig.probe->preflightRtt(*graph,plan.getPimpl()->lastValidPlan,SbVec2i32(side,side)):
      CoinRenderSubmitResult{CoinRenderBackendStatus::BACKEND_ERROR,"view-pressure plan capture lost authored resources"};
    ok &= check(captured&&rig.target->getLastSubmissionSerial()==serial&&rig.probe->roots==roots&&rig.probe->producers==producers,
      "view-pressure discovery retains authored combination and performs no submissions/publication");return result;
  };
  // Discover capacity on a disposable target, then release its high-water span.
  auto discovery=std::unique_ptr<AlphaViewRig>(new AlphaViewRig(options));if(!check(discovery->probe!=nullptr,"peer capacity discovery prepared"))return false;
  unsigned peerFit=0,peerExcess=0;std::string why;
  const bool foundPeer=view_budget_test::boundary([&](unsigned count){notes(peerRoot.get(),peerNotes,count);return inspect(*discovery,peerRoot.get(),false);},peerFit,peerExcess,why);
  ok &= check(foundPeer,"finite peer capacity discovery: "+why);if(!ok)return false;discovery.reset();
  const unsigned peerPressure=peerFit/2;ok &= check(peerPressure>16,"peer pressure is above the initial sixteen-view floor");if(!ok)return false;
  notes(peerRoot.get(),peerNotes,peerPressure);AlphaViewRig peer(options);if(!check(peer.probe!=nullptr,"fresh pressure peer prepared"))return false;
  Frame peerA;ok &= peer.render(peerRoot.get(),peerA,false,"peer pressure A");if(!ok)return false;
  const size_t center=size_t(side/2*side+side/2)*4;
  ok &= check(std::abs(int(peerA.rgba[center])-32)<=3&&std::abs(int(peerA.rgba[center+1])-191)<=3&&std::abs(int(peerA.rgba[center+2])-64)<=3,
    "pressure peer has independently known distinct green image");if(!ok)return false;
  const auto boundary=[&](bool shadows,unsigned&fit,unsigned&excess){scene.group->isActive=shadows;
    auto probeTarget=std::unique_ptr<AlphaViewRig>(new AlphaViewRig(options));if(!probeTarget->probe)return false;
    const bool found=view_budget_test::boundary([&](unsigned count){notes(scene.root,rootNotes,count);return inspect(*probeTarget,scene.root,true);},fit,excess,why);
    return found;};
  unsigned fit=0,excess=0,ordinaryFit=0,ordinaryExcess=0;
  const bool shadowBoundary=boundary(true,fit,excess),ordinaryBoundary=boundary(false,ordinaryFit,ordinaryExcess);scene.group->isActive=TRUE;
  std::cout<<"alpha_views discovered peer_fit="<<peerFit<<" peer_pressure="<<peerPressure<<" active_found="<<shadowBoundary<<" active_fit="<<fit
    <<" active_excess="<<excess<<" inactive_found="<<ordinaryBoundary<<" inactive_fit="<<ordinaryFit<<" inactive_excess="<<ordinaryExcess<<" diagnostic="<<why<<'\n';
  ok &= check(shadowBoundary&&ordinaryBoundary,"finite transparent8/8 RTT boundaries under held peer pressure: "+why);
  // Each annotation spends one view. Both boundaries are above the minimum
  // target span; the public contract adds eight map views and one collection
  // copy, independently of the implementation's reservation formula.
  ok &= check(fit>16&&ordinaryFit>fit&&ordinaryFit-fit==9,"active eight-map collection costs eight raster views plus one copy view above the allocation floor");
  if(!ok)return false;
  notes(scene.root,rootNotes,fit);AlphaViewRig target(options);if(!check(target.probe!=nullptr,"fresh execution target after discovery released"))return false;
  Frame stable;ok &= target.render(scene.root,stable,true,"last admitted8/8 transparent RTT");if(!ok)return false;
  ok &= check(alphaChanges(stable,peerA)>0,"root and peer outputs are distinguishable");
  notes(scene.root,rootNotes,excess);const auto history=target.probe->admissions.size();const auto serial=stable.serial;const unsigned roots=target.probe->roots,producers=target.probe->producers;
  const char*fault="COIN_BGFX_TEST_SHADOW_MAP_ALLOC_ONCE";const char*old=std::getenv(fault);const bool had=old!=nullptr;const std::string previous=old?old:"";
  coinRenderTestSetEnvironment(fault,"1");target.action.apply(scene.root);const Frame denied=target.read();const bool untouched=std::getenv(fault)!=nullptr;coinRenderTestSetEnvironment(fault,nullptr);
  const Frame peerAfter=peer.read();
  ok &= check(target.action.getLastStatus()==CoinRenderAction::UNSUPPORTED&&target.probe->admissions.size()==history+1&&
    view_budget_test::rejected(target.probe->admissions[history])&&target.probe->roots==roots&&target.probe->producers==producers&&untouched&&
    denied.rgba==stable.rgba&&denied.depth==stable.depth&&denied.serial==serial&&peerAfter.rgba==peerA.rgba&&peerAfter.depth==peerA.depth&&peerAfter.serial==peerA.serial,
    "insufficient shared views reject on first graph preflight before producers, preserving fault/root/peer publications");
  notes(scene.root,rootNotes,fit);Frame restored;ok &= target.render(scene.root,restored,true,"capacity recovery root A");ok &= exact(stable,restored);
  Frame peerRestored;ok &= peer.render(peerRoot.get(),peerRestored,false,"peer after root pressure");ok &= exact(peerA,peerRestored);
  Frame afterPeer;ok &= target.render(scene.root,afterPeer,true,"root after independent peer submission");ok &= exact(stable,afterPeer);
  std::cout<<"alpha_views peer_fit="<<peerFit<<" pressure="<<peerPressure<<" active_fit="<<fit<<" active_excess="<<excess<<" inactive_fit="<<ordinaryFit
    <<" collection_delta="<<ordinaryFit-fit<<" fault_preserved="<<untouched<<" route="<<int(options.sceneTexture)<<'\n';
  if(had)coinRenderTestSetEnvironment(fault,previous.c_str());return ok;
}
}
#endif
