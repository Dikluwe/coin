#ifndef COIN_RENDER_MULTIPASS_TRANSPARENCY_CASES_H
#define COIN_RENDER_MULTIPASS_TRANSPARENCY_CASES_H
bool transparentImage(const Image &image,const Scene &scene,int n,double opacity,bool accepted,unsigned m,const char *label){
  if(!check(image.rgba.size()==side*side*4&&image.depth.size()==side*side,std::string(label)+" full transparent color/depth"))return false;
  unsigned bad=0,depthBad=0,partial=0,full=0;
  if(m>1){
    const unsigned levels=unsigned(n)*m;unsigned odd=0,beyond=0;
    const auto color=scene.material->diffuseColor[0];
    for(int y=0;y<side;++y)for(int x=0;x<side;++x){const size_t p=size_t(y*side+x);double best=1e9;unsigned coverage=0;
      for(unsigned k=0;k<=levels;++k){double error=0;for(unsigned c=0;c<3;++c)error=std::max(error,std::abs(double(image.rgba[4*p+c])-255*color[c]*opacity*k/levels));if(error<best){best=error;coverage=k;}}
      bad+=best>3;bad+=std::abs(int(image.rgba[4*p+3])-255)>1;depthBad+=!std::isfinite(image.depth[p])||std::abs(image.depth[p]-1.f)>1e-5;
      if(!accepted)bad+=coverage!=0;
      partial+=coverage>0&&coverage<levels;odd+=(coverage&1)!=0;
      if(coverage>0&&coverage<levels){double nearest=1;for(int j=0;j<=n;++j)nearest=std::min(nearest,std::abs(double(coverage)/levels-double(j)/n));beyond+=nearest>1.5/255;}
      if(x>=12&&x<24&&y>=20&&y<44){full+=coverage==levels;bad+=coverage!=(accepted?levels:0);}
      if(x<2)bad+=coverage!=0;
    }
    std::cout<<"multipass_transparency_msaa "<<label<<" N="<<n<<" M="<<m<<" opacity="<<opacity<<" bad="<<bad<<" depth_bad="<<depthBad<<" partial="<<partial<<" odd="<<odd<<" beyond_1x="<<beyond<<" full="<<full<<'\n';
    return check(!bad&&!depthBad&&(!accepted||(full>=192&&partial>4&&beyond>4&&(m!=4||odd>4))),std::string(label)+" actual M*N transparent phase coverage, source-over alpha, alpha-test and disabled depth writes");
  }
  for(int y=16;y<48;++y)for(int x=0;x<side;++x){
    unsigned covered=0;for(const auto &j:samples(n)){const double px=x+1-j[0];covered+=accepted&&px>8&&px<scene.edge;}
    const size_t p=size_t(y*side+x);const auto color=scene.material->diffuseColor[0];
    if(m==1||!accepted||x>=12&&x<24||x<6||x>34){
      for(unsigned c=0;c<3;++c)bad+=std::abs(double(image.rgba[4*p+c])-255*color[c]*opacity*covered/n)>3;
    }else{
      // Device sample positions are not fixed by this contract. Enumerate
      // exact integer M*N coverage fractions, retaining the original RGB3
      // gate. Interior/clear geometry remains closed-form above.
      double best=1e9;unsigned actual=0;
      for(unsigned k=0;k<=unsigned(n)*m;++k){double error=0;for(unsigned c=0;c<3;++c)error=std::max(error,std::abs(double(image.rgba[4*p+c])-255*color[c]*opacity*k/(n*m)));if(error<best){best=error;actual=k;}}
      bad+=best>3;partial+=actual>0&&actual<unsigned(n)*m;
    }
    bad+=std::abs(int(image.rgba[4*p+3])-255)>1;
    depthBad+=!std::isfinite(image.depth[p])||std::abs(image.depth[p]-1.f)>1e-5;
    partial+=covered>0&&covered<unsigned(n);full+=covered==unsigned(n);
  }
  std::cout<<"multipass_transparency "<<label<<" N="<<n<<" opacity="<<opacity<<" accepted="<<accepted<<" bad="<<bad<<" depth_bad="<<depthBad<<" partial="<<partial<<" full="<<full<<'\n';
  return check(!bad&&!depthBad&&(!accepted||(full>300&&(n==1||partial>=32))),std::string(label)+" per-sample alpha test, source-over/weighted composition then linear mean and disabled depth writes");
}
bool positiveBudgetControl(bool gpu,const CoinRenderOptions &requestedOptions,int n,uint64_t budget){
  CoinRenderOptions options=requestedOptions;options.transparency=COIN_RENDER_TRANSPARENCY_COIN;
  Scene scene;if(options.sampleCount>1)msaaGeometry(scene);
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));auto *audit=prepare(target.get(),gpu);if(!audit)return false;
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::NONE);action.setBackgroundColor(SbColor4f(0,0,0,0));action.setNumPasses(n);action.setMultipassBufferBudget(budget);action.apply(scene.root);
  const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"positive opaque control admits the identical AA budget without the extra live feature: "+std::string(action.getLastError().getString()));
  if(success){if(options.sampleCount>1)msaaImage(read(target.get()),n,options.sampleCount,scene.material->diffuseColor[0],"positive opaque budget control");else imageValid(read(target.get()),n,scene.edge,"positive opaque budget control");}
  action.setRenderTarget(nullptr);return success;
}
// Discover only the public resource-admission boundary. Pixel values never
// select the budget: the original image oracles remain fixed. Every probe is
// offscreen and starts from an already published equivalent opaque result.
bool opaqueBudgetFrontier(bool gpu,const CoinRenderOptions &requestedOptions,int n,uint64_t extra,uint64_t &combinedBudget){
  CoinRenderOptions options=requestedOptions;options.transparency=COIN_RENDER_TRANSPARENCY_COIN;
  Scene scene;if(options.sampleCount>1)msaaGeometry(scene);
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));auto *audit=prepare(target.get(),gpu);if(!audit)return false;
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::NONE);action.setBackgroundColor(SbColor4f(0,0,0,0));action.setNumPasses(n);
  const uint64_t upper=uint64_t(256)*1024*1024;action.setMultipassBufferBudget(upper);action.apply(scene.root);
  if(!check(action.getLastStatus()==CoinRenderAction::SUCCESS,"opaque frontier starts with a published valid control: "+std::string(action.getLastError().getString())))return false;
  if(options.sampleCount>1)msaaImage(read(target.get()),n,options.sampleCount,scene.material->diffuseColor[0],"opaque frontier predecessor");else imageValid(read(target.get()),n,scene.edge,"opaque frontier predecessor");
  auto probe=[&](uint64_t budget,bool &admitted){
    const auto before=read(target.get());const auto serial=target->getLastSubmissionSerial();const unsigned commands=audit->commands;action.setMultipassBufferBudget(budget);action.apply(scene.root);const auto after=read(target.get());const auto status=action.getLastStatus();admitted=status==CoinRenderAction::SUCCESS;
    if(admitted)return check(target->getLastSubmissionSerial()>serial&&exact(before,after),"admitted opaque budget preserves its analytically validated current color/depth");
    return check(status==CoinRenderAction::OUT_OF_MEMORY&&action.getLastError().getLength()>0&&audit->commands==commands&&target->getLastSubmissionSerial()==serial&&exact(before,after),"opaque budget boundary refusal is OOM before commands with exact publication retained");
  };
  bool admitted=false;
  if(!probe(upper,admitted)||!check(admitted,"bounded upper budget admits an already-published equivalent opaque state"))return false;
  if(!probe(0,admitted)||!check(!admitted,"N>1 opaque control rejects zero budget"))return false;
  uint64_t low=0,high=upper;unsigned probes=0;
  while(high-low>1&&probes<32){const uint64_t mid=low+(high-low)/2;if(!probe(mid,admitted))return false;if(admitted)high=mid;else low=mid;++probes;}
  if(!check(high-low==1,"bounded integer budget search converges without image calibration"))return false;
  if(!probe(high-1,admitted)||!check(!admitted,"one byte below opaque frontier refuses"))return false;
  if(!probe(high,admitted)||!check(admitted,"exact opaque frontier admits in the same published state"))return false;
  if(!check(extra>0&&high<=UINT64_MAX-(extra-1),"opaque frontier plus physical feature minimum has checked arithmetic"))return false;
  combinedBudget=high+extra-1;
  if(!probe(combinedBudget,admitted)||!check(admitted,"same-state opaque control admits the exact budget used for the OIT refusal"))return false;
  if(options.sampleCount>1)msaaImage(read(target.get()),n,options.sampleCount,scene.material->diffuseColor[0],"same-state positive OIT budget control");else imageValid(read(target.get()),n,scene.edge,"same-state positive feature budget control");
  std::cout<<"multipass_opaque_budget_frontier minimum="<<high<<" physical_extra="<<extra<<" feature_budget="<<combinedBudget<<" probes="<<probes<<'\n';
  action.setRenderTarget(nullptr);return good;
}
bool transparencyPath(bool gpu,const CoinRenderOptions &options,int requested){
  const int n=requested?requested:4;
  const bool weighted=options.transparency==COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;
  const int layers=weighted?2:8;
  const uint64_t pixels=uint64_t(side)*side;
  const uint64_t minimum=pixels*(16+8*options.sampleCount+(weighted?10*options.sampleCount:(uint64_t(layers)*13+4)*options.sampleCount));
  const bool budgetCase=!weighted||options.sampleCount==4;
  uint64_t budget=minimum-1;
  if(weighted&&options.sampleCount==4){
    // The physical OIT minimum is RGBA16F + R16F for every real MSAA
    // sample. The opaque frontier already includes its AA/output/staging.
    const uint64_t extra=pixels*10*options.sampleCount;
    if(!opaqueBudgetFrontier(gpu,options,n,extra,budget))return false;
  }else if(budgetCase&&!positiveBudgetControl(gpu,options,n,budget))return false;
  Scene scene;if(options.sampleCount>1)msaaGeometry(scene);scene.material->transparency=.5f;
  auto *depth=new SoDepthBuffer;depth->test=TRUE;depth->write=FALSE;scene.root->insertChild(depth,scene.root->getNumChildren()-1);
  auto *alpha=new SoAlphaTest;alpha->function=SoAlphaTest::GREATER;alpha->value=.3f;scene.root->insertChild(alpha,scene.root->getNumChildren()-1);
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));auto *audit=prepare(target.get(),gpu);if(!audit)return false;
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::SORTED_LAYERS_BLEND);action.setSortedLayersNumPasses(layers);action.setBackgroundColor(SbColor4f(0,0,0,1));
  auto draw=[&](int n,const char *label){action.setNumPasses(n);const auto serial=target->getLastSubmissionSerial(),source=scene.root->getNodeId();const unsigned captures=scene.captures;action.apply(scene.root);const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial,std::string(label)+" current transparent publication: "+action.getLastError().getString());check(scene.captures==captures+1&&source==scene.root->getNodeId()&&action.getSortedLayersNumPasses()==layers,"one transparent capture, independent fixed peeling layer count");if(success)transparentImage(read(target.get()),scene,n,1-scene.material->transparency[0],1-scene.material->transparency[0]>.3f,options.sampleCount,label);};
  draw(1,"transparent predecessor");draw(n,"transparent multipass");scene.material->transparency=.75f;draw(n,"alpha test discards before accumulation");scene.material->transparency=.25f;draw(n,"new live alpha before accumulation");scene.material->transparency=.5f;draw(n,"transparent authorial restoration");
  const auto stable=read(target.get());const auto serial=target->getLastSubmissionSerial();const unsigned commands=audit->commands;
  // Nominal simultaneously-live minimum: float32 RGBA AA sum + root RGBA8
  // and D32 + OIT RGBA16F/R16F, or8 peeled RGBA16F/D32/R8 plus opaque D32.
  // Row alignment/staging/extra safety copies can only raise this minimum.
  if(budgetCase){
  action.setMultipassBufferBudget(budget);action.apply(scene.root);
  std::cout<<"multipass_simultaneous transparency="<<options.transparency<<" nominal_minimum="<<minimum<<" budget="<<budget<<" status="<<action.getLastStatus()<<" error="<<action.getLastError().getString()<<'\n';
  check(action.getLastStatus()==CoinRenderAction::OUT_OF_MEMORY&&audit->commands==commands&&target->getLastSubmissionSerial()==serial&&exact(stable,read(target.get())),"simultaneous OIT/peeling buffers cannot be omitted from multipass budget; refusal before commands and exact prior result");
  action.setMultipassBufferBudget(256ull*1024*1024);draw(n,"transparent same-target budget recovery");}
  action.setRenderTarget(nullptr);return good;
}
#endif
