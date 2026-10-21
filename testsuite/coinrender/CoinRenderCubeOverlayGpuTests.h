// Included inside cube_overlay_test. The audit delegates every backend method;
// only root-submit count/reuse are observed, with no synthetic GPU work.
class GpuAudit:public CoinRenderBackend {
public:
  explicit GpuAudit(std::unique_ptr<CoinRenderBackend> value):inner(std::move(value)){}
  unsigned submissions=0;CoinRenderFrameReuseDecision reuse;
  bool isGpuBackend()const override{return inner->isGpuBackend();}
  bool initializesCpuDepthBuffer()const override{return inner->initializesCpuDepthBuffer();}
  bool resetOnActionDetach()const override{return inner->resetOnActionDetach();}
  bool supportsOffscreenShadows()const override{return inner->supportsOffscreenShadows();}
  bool supportsOpaqueBump()const override{return inner->supportsOpaqueBump();}
  CoinRenderBackendStatus getStatus()const override{return inner->getStatus();}
  CoinRenderBackendStatus prepare(CoinRenderTargetP &t)override{return inner->prepare(t);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,CoinRenderTargetP &t)override{++submissions;reuse={};return inner->submit(f,t);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,CoinRenderTargetP &t,const CoinRenderFrameReuseDecision &r)override{++submissions;reuse=r;return inner->submit(f,t,r);}
  CoinRenderSubmitResult submitAsync(const CoinRenderFramePlan &f,CoinRenderTargetP&t,CoinRenderReadbackTicket&ticket,const CoinRenderFrameReuseDecision&r)override{return inner->submitAsync(f,t,ticket,r);}
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan&r,const CoinRenderFramePlan&f,const SbVec2i32&s)const override{return inner->preflightRtt(r,f,s);}
  bool readbackLoad(uint64_t&a,uint64_t&b)const override{return inner->readbackLoad(a,b);}
  bool requiresSharedRetirement()const override{return inner->requiresSharedRetirement();}
  void retireLostReadbacks()override{inner->retireLostReadbacks();}
  CoinRenderDeviceDomain resourceDomain()const override{return inner->resourceDomain();}
  CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan&f,const SbVec2i32&s,uint64_t id,CoinRenderTargetP&t,uint64_t&token)override{return inner->submitRtt(f,s,id,t,token);}
  void finishRtt(const std::vector<uint64_t>&v)override{inner->finishRtt(v);}
  bool stagedRttRequiresPreparedParent()const override{return inner->stagedRttRequiresPreparedParent();}
  void poll()override{inner->poll();}
  const std::string &getLastError()const override{return inner->getLastError();}
  CoinRenderSubmitResult assembleCubeRtt(const std::array<uint64_t,6>&v,uint32_t n,bool m,CoinRenderTargetP&t,uint64_t&r)override{return inner->assembleCubeRtt(v,n,m,t,r);}
  void retireRtt(const std::vector<uint64_t>&v)override{inner->retireRtt(v);}
private:std::unique_ptr<CoinRenderBackend> inner;
};
int gpu(){
  Environment env;Scene scene;
  constexpr int side=128;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side)));
  if(!check(bool(target),"GPU target creation"))return 1;
  // createOffscreen allocates the target shell only. Construct its executor
  // through the initial factory; preparation remains owned by the first apply.
  target->getPimpl()->backend=CoinRenderTargetP::createBackend();
  if(!check(bool(target->getPimpl()->backend),"GPU executor creation"))return 1;
  if(!target->getPimpl()->backend->isGpuBackend())return 77;
  std::unique_ptr<CoinRenderTarget> literal(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),target->getOptions()));
  if(!check(bool(literal),"literal GPU target creation"))return 1;
  literal->getPimpl()->backend=CoinRenderTargetP::createBackend();
  if(!check(literal->getPimpl()->backend&&literal->getPimpl()->backend->isGpuBackend(),"same-backend literal GPU executor"))return 1;
  if(!check(target->setDepthReadbackEnabled(TRUE)&&literal->setDepthReadbackEnabled(TRUE),"explicit synchronous depth readback"))return 1;
  auto *audit=new GpuAudit(std::move(target->getPimpl()->backend));target->getPimpl()->backend.reset(audit);
  auto *literalAudit=new GpuAudit(std::move(literal->getPimpl()->backend));literal->getPimpl()->backend.reset(literalAudit);
  // Wrapping an unprepared executor must preserve its lifecycle state. The
  // first normal apply prepares it; there is no preparatory frame or submit.
  target->getPimpl()->status=CoinRenderTarget::TARGET_NOT_READY;
  literal->getPimpl()->status=CoinRenderTarget::TARGET_NOT_READY;
  CoinRenderAction action{SbViewportRegion(side,side)};action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::NONE);action.setBackgroundColor(SbColor4f(.03125f,.0625f,.09375f,1));
  // Keep both actions attached for the whole sequence: a per-frame control
  // destructor could reset its backend and invalidate the audit pointer.
  CoinRenderAction control{SbViewportRegion(side,side)};control.setRenderTarget(literal.get());control.setTransparencyType(CoinRenderAction::NONE);control.setBackgroundColor(action.getBackgroundColor());
  Reference reference;SoCallbackAction native;reference.install(native);
  const char *flags[]={"COIN_RENDER_DISABLE_TRANSLATION_OVERLAY","COIN_RENDER_DISABLE_MATERIAL_OVERLAY","COIN_RENDER_DISABLE_CUBE_OVERLAY"};
  const CoinRenderVertexSnapshot *vertices=nullptr;const uint32_t *indices=nullptr;uint64_t generation=0;
  std::vector<uint8_t> originalImage;std::vector<float> originalDepth;bool ok=true;
  for(unsigned phase=0;phase<3;++phase){
    if(phase){const bool changed=phase==1;scene.shared->width=changed?3.5f:2;scene.shared->height=changed?1.25f:3;scene.shared->depth=changed?2.5f:4;scene.unique->width=changed?1.5f:3;scene.unique->depth=changed?2.25f:1;scene.transform[0]->translation.setValue(changed?-2.25f:-3,changed?.375f:0,0);scene.material[2]->diffuseColor.setValue(changed?.875f:.5f,.375f,changed?.25f:.5f);scene.material[2]->shininess=changed?.75f:.5f;}
    reference.corners.clear();native.apply(scene.root);
    const uint64_t previous=action.getPimpl()->lastValidPlan.revision;
    action.apply(scene.root); // Exactly one apply for the current authored frame.
    if(!check(action.getLastStatus()==CoinRenderAction::SUCCESS,std::string("incremental GPU first apply: ")+action.getLastError().getString()))return 1;
    std::vector<uint8_t> pixels;std::vector<float> depth;target->readbackRGBA(pixels);target->readbackDepth(depth);
    ok &= check(audit->submissions==phase+1,"exactly one real GPU root submission per authored frame");
    auto &p=action.getPimpl();ok &= payload(p->lastValidPlan,reference,"GPU authored capture");ok &= authoredBox(p->lastValidPlan,scene);
    if(!phase){vertices=p->lastValidPlan.vertices.data();indices=p->lastValidPlan.indices.data();generation=p->translationProofGeneration;ok &= check(p->translationProofValid&&p->cubeByNode.count(scene.shared)&&p->cubeByNode.count(scene.unique),"real GPU first capture admits both cube sources");}
    else ok &= check(p->translationProofValid&&p->translationProofGeneration==generation&&p->lastValidPlan.vertices.data()==vertices&&p->lastValidPlan.indices.data()==indices&&audit->reuse.kind==CoinRenderFrameReuseKind::RESOURCE_REBUILD&&audit->reuse.baseRevision==previous,"GPU update consumes real retained overlay and correct base revision");
    // Separate target/cache keeps the control from overwriting candidate GPU
    // history. Every A/B/A transition changes source fields; disabled overlay
    // flags make the persistent control recapture each current authored frame.
    for(const auto *flag:flags)coinRenderTestSetEnvironment(flag,"1");control.apply(scene.root);
    for(const auto *flag:flags)coinRenderTestSetEnvironment(flag,"0");
    if(!check(control.getLastStatus()==CoinRenderAction::SUCCESS,std::string("literal GPU first apply: ")+control.getLastError().getString()))return 1;
    std::vector<uint8_t> expected;std::vector<float> expectedDepth;literal->readbackRGBA(expected);literal->readbackDepth(expectedDepth);
    ok &= check(literalAudit->submissions==phase+1&&!control.getPimpl()->translationProofValid&&sameExpanded(p->lastValidPlan,control.getPimpl()->lastValidPlan),"literal GPU frame has no overlay and matches complete consumed authored plan");
    if(!check(pixels.size()==size_t(side)*side*4&&expected.size()==pixels.size()&&depth.size()==size_t(side)*side&&expectedDepth.size()==depth.size(),"complete color/depth publication on first apply"))return 1;
    unsigned covered=0,maxColor=0;float maxDepth=0;
    for(size_t i=0;i<pixels.size();++i)maxColor=std::max(maxColor,unsigned(std::abs(int(pixels[i])-int(expected[i]))));
    for(size_t i=0;i<depth.size();++i){if(!check(std::isfinite(depth[i])&&std::isfinite(expectedDepth[i]),"finite GPU depth"))return 1;covered+=depth[i]<1.f;maxDepth=std::max(maxDepth,std::abs(depth[i]-expectedDepth[i]));}
    ok &= check(covered>16,"GPU publication contains visible authored geometry");
    ok &= check(pixels==expected&&depth==expectedDepth,"same-environment overlay and literal RGBA/depth are exact");
    if(!phase){originalImage=pixels;originalDepth=depth;}
    if(phase==1)ok &= check(pixels!=originalImage&&depth!=originalDepth,"first B publication changes authored color and geometry depth");
    if(phase==2)ok &= check(pixels==originalImage&&depth==originalDepth,"first restored A publication matches original color/depth exactly");
    std::cout<<"cube_overlay_gpu phase="<<phase<<" submissions="<<audit->submissions<<" control_submissions="<<literalAudit->submissions<<" covered="<<covered<<" max_rgba_error="<<maxColor<<" max_depth_error="<<maxDepth<<'\n';
  }
  return ok?0:1;
}
