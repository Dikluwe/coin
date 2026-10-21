#ifndef COIN_RENDER_SHADOW_RTT_ATOMIC_H
#define COIN_RENDER_SHADOW_RTT_ATOMIC_H
namespace {
bool c036MultiRttAtomic(WindowPair&h){
  const auto proxies=c036Install(h);const int mode=h.off->getOptions().transparency==COIN_RENDER_TRANSPARENCY_OBJECT?SoGLRenderAction::SORTED_OBJECT_BLEND:SoGLRenderAction::SORTED_LAYERS_BLEND;
  C036Peer peer(h);if(!peer.ready||!peer.publish(h))return false;C036BudgetScene s(mode);auto&receiver=*s.receivers[0];Frame a,b,c;
  const auto frame=[&](Frame&f,const char*phase){return h.frame(receiver,f,phase,true,false,2,s.root.get());};
  if(!frame(a,"multiRTT invalid-state anchor A"))return false;const auto physical=h.lastShown;bool ok=true;
  auto*offset=new SoPolygonOffset;offset->styles=SoPolygonOffset::FILLED;offset->on=FALSE;receiver.receiver->insertChild(offset,3);
  const char*fault="COIN_BGFX_TEST_SHADOW_MAP_ALLOC_ONCE";const char*old=std::getenv(fault);const bool had=old!=nullptr;const std::string saved=old?old:"";
  const bool observeFault=h.caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION;
  const auto st=receiver.coords[7]->point[0];
  for(int invalid=0;invalid<3;++invalid){const float nan=std::numeric_limits<float>::quiet_NaN();
    if(invalid==0)receiver.depth->range=SbVec2f(nan,.8f);
    if(invalid==1){offset->on=TRUE;offset->factor=nan;}
    if(invalid==2)receiver.coords[7]->point.set1Value(0,SbVec2f(nan,0));
    if(observeFault)coinRenderTestSetEnvironment(fault,"1");
    const auto offSerial=h.off->getLastSubmissionSerial(),windowSerial=h.shown->getLastSubmissionSerial();
    const unsigned offChildren=proxies.first->producers,windowChildren=proxies.second->producers;
    std::vector<uint8_t>windowBefore;h.shown->readbackRGBA(windowBefore);
    h.offAction.apply(s.root.get());h.off->readbackRGBA(b.rgba);h.off->readbackDepth(b.depth);
    bool refused=h.offAction.getLastStatus()!=CoinRenderAction::SUCCESS&&h.off->getLastSubmissionSerial()==offSerial&&proxies.first->producers==offChildren&&b.rgba==a.rgba&&b.depth==a.depth&&h.shown->getLastSubmissionSerial()==windowSerial;
    std::cout<<"c036_invalid kind="<<invalid<<" off_status="<<int(h.offAction.getLastStatus())<<" error="<<h.offAction.getLastError().getString()<<'\n';
    h.windowAction.apply(s.root.get());std::vector<uint8_t>windowAfter;h.shown->readbackRGBA(windowAfter);
    refused &= h.windowAction.getLastStatus()!=CoinRenderAction::SUCCESS&&h.shown->getLastSubmissionSerial()==windowSerial&&proxies.second->producers==windowChildren&&windowBefore==windowAfter&&h.off->getLastSubmissionSerial()==offSerial&&ximage(h.d,h.window,h.extent)==physical;
    if(observeFault){refused &= std::getenv(fault)!=nullptr;coinRenderTestSetEnvironment(fault,nullptr);}
    refused &= peer.preserved();ok &= check(refused,"multiRTT NaN range/offset/unit7 refuses before producers and preserves target, peer, physical image and armed map fault");
    receiver.depth->range=SbVec2f(0,1);offset->on=FALSE;offset->factor=1;receiver.coords[7]->point.set1Value(0,st);
    const auto rootSerial=h.off->getLastSubmissionSerial(),shownSerial=h.shown->getLastSubmissionSerial();ok &= peer.publish(h);
    ok &= check(h.off->getLastSubmissionSerial()==rootSerial&&h.shown->getLastSubmissionSerial()==shownSerial,"peer independently publishes without changing invalid RTT roots");
    const bool restored=frame(c,"multiRTT invalid-state recovery A");ok &= restored;if(restored)ok &= exact(a,c)&&check(h.lastShown==physical,"multiRTT invalid-state same-runtime recovery restores A");
  }
  ok &= peer.preserved();if(observeFault&&had)coinRenderTestSetEnvironment(fault,saved.c_str());return ok;
}
}
#endif
