#ifndef COIN_RENDER_MULTIPASS_RTT_SHADOW_CASES_H
#define COIN_RENDER_MULTIPASS_RTT_SHADOW_CASES_H
// Combined texture/map filtering exclusions need a larger authored shadow
// interior than the standalone VSM fixture. This is its receiver-space half
// extent; the native dual-camera mapping scales caster dimensions by sqrt(6).
const double multipassRttShadowHalfExtent=.19;
bool rttShadowImage(const Image &image,const MultipassShadowScene &shadow,const RttScene &rtt,int n,const char *label){
  if(!check(image.rgba.size()==side*side*4&&image.depth.size()==side*side,std::string(label)+" complete RTT+VSM result"))return false;
  unsigned bad=0,depthBad=0,lit=0,dark=0,partial=0,quadrants[4]={0,0,0,0};
  for(int y=0;y<side;++y)for(int x=0;x<side;++x){double expected[4]={0,0,0,0};unsigned cover=0;bool last=false,unsafe=false,allDark=true,allLit=true;
    for(const auto &j:samples(n)){const double wx=2*(x+1-j[0])/side-1,wy=1-2*(y+j[1])/side;last=std::abs(wx)<.89&&std::abs(wy)<.89;if(!last){allDark=allLit=false;continue;}++cover;
      const double u=(wx+.89)/1.78,v=(wy+.89)/1.78;
      const double distance=std::min(multipassRttShadowHalfExtent-std::abs(wx+.64),multipassRttShadowHalfExtent-std::abs(wy+.14));
      if(std::abs(u-.5)<2.1/32||std::abs(v-.5)<2.1/32||std::abs(distance)<=2.1*2/side){unsafe=true;continue;}
      const bool occluded=distance>2.1*2/side;allDark&=occluded;allLit&=!occluded;
      const double visibility=occluded?(shadow.moved?1:0):1;
      const unsigned q=(u>.5?1:0)+(v>.5?2:0);++quadrants[q];const auto source=rtt.source.color(q);const double tint[]={.6,.3,.8};
      for(unsigned c=0;c<3;++c)expected[c]+=.8*tint[c]*source[c]*visibility/n;expected[3]+=1./n;
    }
    if(unsafe)continue;const size_t p=size_t(y*side+x);for(unsigned c=0;c<4;++c)bad+=std::abs(double(image.rgba[4*p+c])-255*expected[c])>(c==3?1:3);
    depthBad+=!std::isfinite(image.depth[p])||std::abs(image.depth[p]-(last?.5f:1.f))>1e-5;lit+=allLit;dark+=allDark;partial+=cover>0&&cover<unsigned(n);
  }
  std::cout<<"multipass_rtt_vsm "<<label<<" N="<<n<<" bad="<<bad<<" depth_bad="<<depthBad<<" lit="<<lit<<" dark="<<dark<<" partial="<<partial<<" quadrants="<<quadrants[0]<<','<<quadrants[1]<<','<<quadrants[2]<<','<<quadrants[3]<<'\n';
  return check(!bad&&!depthBad&&lit>300&&dark>=9&&quadrants[0]>32&&quadrants[1]>32&&quadrants[2]>32&&quadrants[3]>32&&(n==1||partial>16),std::string(label)+" current four-quadrant RTT multiplied by effective VSM shadow, per-sample mean and LAST depth");
}
bool rttShadowPath(const CoinRenderOptions &options,int requested){
  MultipassShadowScene shadow(1);RttScene rtt;
  shadow.casters[0]->width=shadow.casters[0]->height=float(2*multipassRttShadowHalfExtent/std::sqrt(6.));
  auto *quality=new SoComplexity;quality->textureQuality=.1f;shadow.group->insertChild(quality,0);
  const int receiver=shadow.group->findChild(shadow.receiver);
  auto *uv=new SoTextureCoordinate2;const SbVec2f values[]={SbVec2f(0,0),SbVec2f(1,0),SbVec2f(1,1),SbVec2f(0,1)};uv->point.setValues(0,4,values);shadow.group->insertChild(uv,receiver);shadow.group->insertChild(rtt.texture,receiver);
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));if(!check(bool(target),"combined RTT+VSM public target"))return false;
  auto &p=target->getPimpl().get();if(!check(p.prepareBackend()==CoinRenderBackendStatus::SUCCESS&&p.backend&&p.backend->isGpuBackend(),"combined RTT+VSM GPU executor"))return false;
  auto *audit=new RttAudit(std::move(p.backend));p.backend.reset(audit);target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::NONE);action.setBackgroundColor(SbColor4f(0,0,0,0));
  auto draw=[&](int n,const char *label){action.setNumPasses(n);const auto serial=target->getLastSubmissionSerial(),revision=shadow.root->getNodeId(),source=rtt.source.root->getNodeId();const unsigned main=shadow.captures,child=rtt.source.captures;
    RttTrace trace;check(trace.start(),"combined passive producer observation");action.apply(shadow.root);const auto events=trace.stop();
    const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial,std::string(label)+" combined current publication: "+action.getLastError().getString());
    check(shadow.captures==main+1&&rtt.source.captures==child+1&&shadow.root->getNodeId()==revision&&rtt.source.root->getNodeId()==source,"combined consumer/producer captures once with both authorial sources intact");producerEvents(events,rtt,options.sceneTexture,1);
    if(success){check(audit->graph.mode==options.sceneTexture&&audit->graph.producers.size()==1,"combined exact requested RTT route and unique producer");shadowDelivery(audit->last,shadow);rttShadowImage(read(target.get()),shadow,rtt,n,label);}};
  const int n=requested?requested:4;draw(1,"combined predecessor");draw(n,"RTT+VSM multipass");rtt.source.paint(true);draw(n,"RTT current source mutation under VSM");shadow.move(true);draw(n,"effective caster mutation over current RTT");rtt.source.paint(false);shadow.move(false);draw(n,"RTT/VSM authorial restoration");
  const auto stable=read(target.get());const auto serial=target->getLastSubmissionSerial();const unsigned commands=audit->commands,producers=audit->producers;
  action.setMultipassBufferBudget(0);RttTrace trace;check(trace.start(),"combined refusal observation");action.apply(shadow.root);const auto events=trace.stop();
  check(action.getLastStatus()==CoinRenderAction::OUT_OF_MEMORY&&audit->commands==commands&&audit->producers==producers&&target->getLastSubmissionSerial()==serial&&exact(stable,read(target.get())),"combined invalid budget precedes VSM/RTT/root execution and preserves exact final result");producerEvents(events,rtt,options.sceneTexture,0);
  action.setMultipassBufferBudget(256ull*1024*1024);draw(n,"combined same-target recovery");action.setRenderTarget(nullptr);return good;
}
#endif
