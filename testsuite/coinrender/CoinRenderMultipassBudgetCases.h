#ifndef COIN_RENDER_MULTIPASS_BUDGET_CASES_H
#define COIN_RENDER_MULTIPASS_BUDGET_CASES_H
bool resourceBudgetPath(const CoinRenderOptions &options,bool withShadows){
  // Sizes/formats below are authored inputs, not a call into the accounting
  // under test. Count only an indisputable live subset, leaving any duplicate,
  // MSAA resolve, alignment, RTT depth/staging or safety storage uncharged.
  const uint64_t minimum=uint64_t(side)*side*(16+8)+(withShadows?uint64_t(256)*256*16:uint64_t(512)*512*4);
  if(!positiveBudgetControl(true,options,3,minimum-1))return false;
  MultipassShadowScene shadows(1);RttScene rtt;rtt.texture->size.setValue(512,512);
  SoSeparator *root=withShadows?shadows.root:rtt.root;
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
  if(!check(bool(target),"simultaneous budget public target"))return false;
  auto &p=target->getPimpl().get();if(!check(p.prepareBackend()==CoinRenderBackendStatus::SUCCESS&&p.backend&&p.backend->isGpuBackend(),"simultaneous budget real GPU executor"))return false;
  auto *audit=new RttAudit(std::move(p.backend));p.backend.reset(audit);target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::NONE);action.setBackgroundColor(SbColor4f(0,0,0,0));action.setNumPasses(3);
  auto draw=[&](const char *label){const auto serial=target->getLastSubmissionSerial();action.apply(root);const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial,std::string(label)+" full valid resource graph: "+action.getLastError().getString());if(success){if(withShadows){shadowDelivery(audit->last,shadows);shadowImage(read(target.get()),shadows,3,label);}else rttImage(read(target.get()),rtt,3,label);}return success;};
  if(!draw("simultaneous resource predecessor")){action.setRenderTarget(nullptr);return false;}
  const auto stable=read(target.get());const auto serial=target->getLastSubmissionSerial();const unsigned commands=audit->commands,producers=audit->producers;
  // VSM moments are RGBA32F256x256; RTT RGBA8512x512 is sampled by
  // both shared occurrences. Either image coexists with the root+AA sum.
  action.setMultipassBufferBudget(minimum-1);RttTrace trace;check(trace.start(),"passive simultaneous budget producer observation");action.apply(root);const auto events=trace.stop();
  std::cout<<"multipass_simultaneous feature="<<(withShadows?"VSM":"RTT")<<" route="<<options.sceneTexture<<" nominal_minimum="<<minimum<<" budget="<<minimum-1<<" status="<<action.getLastStatus()<<" error="<<action.getLastError().getString()<<'\n';
  check(action.getLastStatus()==CoinRenderAction::OUT_OF_MEMORY&&audit->commands==commands&&audit->producers==producers&&target->getLastSubmissionSerial()==serial&&exact(stable,read(target.get()))&&events.find("COIN_RENDER_PHASE rtt_producer_complete ")==std::string::npos,"live VSM/RTT attachments counted with AA before any producer/root, exact previous color/depth/serial");
  action.setMultipassBufferBudget(UINT64_MAX);draw("all-bit budget recovery without accounting overflow");
  action.setMultipassBufferBudget(0);action.setNumPasses(1);action.apply(root);check(action.getLastStatus()==CoinRenderAction::SUCCESS,"legacy one-pass resource graph remains legal at zero multipass budget");
  action.setRenderTarget(nullptr);return good;
}
#endif
