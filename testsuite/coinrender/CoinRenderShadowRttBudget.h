#ifndef COIN_RENDER_SHADOW_RTT_BUDGET_H
#define COIN_RENDER_SHADOW_RTT_BUDGET_H
namespace {
struct C036BudgetScene {
  C036Producer a{{{.75f,.875f,1.f}},.75f,32,32},b{{{.5f,.75f,.875f}},.5f,48,32};
  AlphaScene producer{1,8,false,1.f};
  std::vector<std::unique_ptr<AlphaScene>>receivers;
  std::shared_ptr<SoSeparator>root{new SoSeparator,[](SoSeparator*p){p->unref();}};
  explicit C036BudgetScene(int mode){root->ref();
    // Producer1 has a genuine VSM allocation: an armed allocation fault is a
    // witness that graph admission happened before either producer executed.
    producer.mode->value=SoGLRenderAction::NONE;producer.depth->write=TRUE;
    producer.receiverMaterial->diffuseColor.setValue(.75f,.875f,1);producer.receiverMaterial->transparency=.25f;
    for(int unit:producer.units){const uint8_t white[]={255,255,255,255};producer.images[unit]->image.setValue(SbVec2s(1,1),4,white);}
    a.texture->scene=producer.root;
    for(int i=0;i<6;++i){receivers.emplace_back(new AlphaScene(1,8,i==5,i==5?.2f:1.f));auto&s=*receivers.back();
      s.group->precision=.0625f;s.mode->value=mode;s.receiverMaterial->diffuseColor.setValue(1,1,1);s.receiverMaterial->transparency=0;
      for(int unit:s.units){const uint8_t white[]={255,255,255,255};s.images[unit]->image.setValue(SbVec2s(1,1),4,white);}
      s.stages[0]->insertChild(a.texture,2);s.stages[3]->insertChild(a.texture,2);s.stages[7]->insertChild(b.texture,2);
      if(i==0)root->addChild(s.root->getChild(0));root->addChild(s.group);
    }
  }
};
bool c036HeterogeneousBudget(WindowPair&h){
  const auto proxies=c036Install(h);const int mode=h.off->getOptions().transparency==COIN_RENDER_TRANSPARENCY_OBJECT?SoGLRenderAction::SORTED_OBJECT_BLEND:SoGLRenderAction::SORTED_LAYERS_BLEND;
  C036Peer peer(h);if(!peer.ready||!peer.publish(h))return false;C036BudgetScene s(mode);Frame a,b,c;
  const auto frame=[&](Frame&f,const std::string&phase){return h.frame(*s.receivers[0],f,phase,true,false,2,s.root.get());};
  if(!frame(a,"heterogeneous budget six128 anchor A"))return false;bool ok=true;const auto physical=h.lastShown;
  s.receivers[0]->group->precision=.5f;
  CoinRenderAction capture(SbViewportRegion(side,side));capture.setSortedLayersNumPasses(4);capture.getPimpl()->planOnly=true;
  capture.getPimpl()->executionOptions=h.off->getOptions();auto graph=std::make_shared<CoinRenderRttPlan>(h.off->getOptions().sceneTexture);
  capture.getPimpl()->sceneTexturePlan=graph;capture.apply(s.root.get());CoinRenderShadowPlan maps;std::string why;
  bool planned=capture.getLastStatus()==CoinRenderAction::SUCCESS&&coin_render_plan_shadows(capture.getPimpl()->lastValidPlan,maps,why);
  unsigned small=0,large=0;uint64_t sources=0;for(const auto&p:maps.passes){small+=p.mapSize==128;large+=p.mapSize==1024;sources+=uint64_t(p.mapSize)*p.mapSize*20u;}
  // Independent physical accounting: source RGBA32F+D32F and a normalized
  // RGBA32F array. The type-padded wgpu collection has7layers here; BGFX6.
  const bool bgfx=h.caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION;
  const uint64_t layers=bgfx?6:7,bytes=sources+layers*1024u*1024u*16u,cap=UINT64_C(128)*1024*1024;
  std::cout<<"c036_heterogeneous source_bytes="<<sources<<" collection_layers="<<layers<<" total_bytes="<<bytes<<" cap="<<cap<<" diagnostic="<<why<<'\n';
  ok &= check(planned&&maps.passes.size()==6&&small==5&&large==1&&graph->producers.size()==2&&sources==UINT64_C(22609920)&&((bytes<=cap)==bgfx),
    "six-map boundary retains one1024 five128 and two real producers with backend physical padding");
  if(!ok)return false;
  const char*fault="COIN_BGFX_TEST_SHADOW_MAP_ALLOC_ONCE";const char*old=std::getenv(fault);const bool had=old!=nullptr;const std::string saved=old?old:"";
  const auto refuse=[&](const char*phase){if(bgfx)coinRenderTestSetEnvironment(fault,"1");
    const uint64_t offSerial=h.off->getLastSubmissionSerial(),windowSerial=h.shown->getLastSubmissionSerial();
    const unsigned offProducers=proxies.first->producers,windowProducers=proxies.second->producers;
    const auto offBefore=a.rgba;const auto depthBefore=a.depth;std::vector<uint8_t>windowBefore;h.shown->readbackRGBA(windowBefore);
    h.offAction.apply(s.root.get());h.off->readbackRGBA(b.rgba);h.off->readbackDepth(b.depth);
    const auto memory=[](CoinRenderAction&action){std::string e=action.getLastError().getString();std::transform(e.begin(),e.end(),e.begin(),[](unsigned char c){return char(std::tolower(c));});
      return action.getLastStatus()==CoinRenderAction::UNSUPPORTED&&e.find("shadow")!=std::string::npos&&(e.find("budget")!=std::string::npos||e.find("memory")!=std::string::npos||e.find("128")!=std::string::npos);};
    bool refused=memory(h.offAction)&&h.off->getLastSubmissionSerial()==offSerial&&b.rgba==offBefore&&b.depth==depthBefore&&proxies.first->producers==offProducers&&h.shown->getLastSubmissionSerial()==windowSerial;
    std::cout<<"c036_budget phase="<<phase<<" off_status="<<int(h.offAction.getLastStatus())<<" error="<<h.offAction.getLastError().getString()<<'\n';
    h.windowAction.apply(s.root.get());std::vector<uint8_t>windowAfter;h.shown->readbackRGBA(windowAfter);
    refused &= memory(h.windowAction)&&h.shown->getLastSubmissionSerial()==windowSerial&&proxies.second->producers==windowProducers&&windowAfter==windowBefore&&h.off->getLastSubmissionSerial()==offSerial;
    const auto shown=ximage(h.d,h.window,h.extent);refused &= shown==physical;
    if(bgfx){refused &= std::getenv(fault)!=nullptr;coinRenderTestSetEnvironment(fault,nullptr);}
    refused &= peer.preserved();return check(refused,"heterogeneous physical budget refuses before producers/publication; target, peer and armed allocation fault survive");};
  if(bgfx){const bool accepted=frame(b,"one1024 five128 fits physical BGFX budget");ok &= accepted;if(accepted){ok &= exact(a,b);a=b;}
    s.receivers[1]->group->precision=.5f;ok &= refuse("two1024 four128 exceeds BGFX physical budget");
  }else ok &= refuse("one1024 five128 exceeds type-padded wgpu physical budget");
  const auto rootSerial=h.off->getLastSubmissionSerial(),shownSerial=h.shown->getLastSubmissionSerial();
  ok &= peer.publish(h);h.off->readbackRGBA(b.rgba);h.off->readbackDepth(b.depth);
  ok &= check(h.off->getLastSubmissionSerial()==rootSerial&&h.shown->getLastSubmissionSerial()==shownSerial&&a.rgba==b.rgba&&a.depth==b.depth,
    "independent peer resubmission leaves both RTT roots untouched");
  for(auto&receiver:s.receivers)receiver->group->precision=.0625f;
  const bool restored=frame(c,"heterogeneous budget recovery A");ok &= restored;if(restored)ok &= exact(a,c)&&check(h.lastShown==physical,"heterogeneous refusal restores physical A on the same targets");
  ok &= peer.preserved();if(bgfx&&had)coinRenderTestSetEnvironment(fault,saved.c_str());return ok;
}
}
#endif
