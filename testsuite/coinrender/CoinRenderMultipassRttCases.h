#ifndef COIN_RENDER_MULTIPASS_RTT_CASES_H
#define COIN_RENDER_MULTIPASS_RTT_CASES_H
struct RttScene {
  struct Source {
    SoSeparator *root=new SoSeparator;
    SoMaterial *materials[4];unsigned captures=0;bool changed=false;
    static void visit(void *data,SoAction *action){if(action->isOfType(SoCallbackAction::getClassTypeId()))++static_cast<Source*>(data)->captures;}
    Source() {
      root->ref();auto *camera=new SoOrthographicCamera;
      camera->position.setValue(0,0,3);camera->height=2;camera->nearDistance=1;camera->farDistance=5;root->addChild(camera);
      auto *callback=new SoCallback;callback->setCallback(visit,this);root->addChild(callback);
      auto *model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;root->addChild(model);
      for(unsigned q=0;q<4;++q) {
        auto *piece=new SoSeparator;materials[q]=new SoMaterial;materials[q]->transparency=0;piece->addChild(materials[q]);
        const float left=(q&1)?0:-1.1f,right=(q&1)?1.1f:0,bottom=(q&2)?0:-1.1f,top=(q&2)?1.1f:0;
        const SbVec3f points[]={SbVec3f(left,bottom,0),SbVec3f(right,bottom,0),SbVec3f(right,top,0),SbVec3f(left,top,0)};
        auto *coords=new SoCoordinate3;coords->point.setValues(0,4,points);piece->addChild(coords);
        auto *face=new SoFaceSet;face->numVertices=4;piece->addChild(face);root->addChild(piece);
      }
      paint(false);
    }
    SbColor color(unsigned q)const {
      const float a[4][3]={{.75f,.25f,.5f},{.25f,.875f,.375f},{.5f,.375f,.875f},{.875f,.625f,.125f}};
      const float b[4][3]={{.25f,.625f,.875f},{.875f,.375f,.25f},{.625f,.875f,.125f},{.375f,.125f,.75f}};
      const float *p=changed?b[q]:a[q];return SbColor(p[0],p[1],p[2]);
    }
    void paint(bool value){changed=value;for(unsigned q=0;q<4;++q)materials[q]->diffuseColor.setValue(color(q));}
    ~Source(){root->unref();}
  } source;
  SoSeparator *root=new SoSeparator;
  SoSceneTexture2 *texture=new SoSceneTexture2;
  SoFaceSet *shape=new SoFaceSet;
  unsigned captures=0;
  static void visit(void *data,SoAction *action){if(action->isOfType(SoCallbackAction::getClassTypeId()))++static_cast<RttScene*>(data)->captures;}
  RttScene() {
    root->ref();auto *camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,3);camera->height=2;camera->nearDistance=1;camera->farDistance=5;camera->viewportMapping=SoCamera::LEAVE_ALONE;root->addChild(camera);
    auto *callback=new SoCallback;callback->setCallback(visit,this);root->addChild(callback);
    auto *model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;root->addChild(model);
    auto *quality=new SoComplexity;quality->textureQuality=.1f;root->addChild(quality);
    texture->scene=source.root;texture->size.setValue(32,32);texture->type=SoSceneTexture2::RGBA8;
    texture->model=SoSceneTexture2::MODULATE;texture->transparencyFunction=SoSceneTexture2::NONE;
    texture->wrapS=SoSceneTexture2::CLAMP;texture->wrapT=SoSceneTexture2::CLAMP;
    texture->backgroundColor.setValue(0,0,0,1);root->addChild(texture);
    auto *mesh=new SoSeparator;auto *coords=new SoCoordinate3;
    const SbVec3f points[]={SbVec3f(-.375f,-.75f,0),SbVec3f(.3853125f,-.75f,0),SbVec3f(.3853125f,.75f,0),SbVec3f(-.375f,.75f,0)};
    coords->point.setValues(0,4,points);mesh->addChild(coords);
    auto *uv=new SoTextureCoordinate2;const SbVec2f values[]={SbVec2f(0,0),SbVec2f(1,0),SbVec2f(1,1),SbVec2f(0,1)};
    uv->point.setValues(0,4,values);mesh->addChild(uv);shape->numVertices=4;mesh->addChild(shape);
    for(unsigned i=0;i<2;++i) {
      auto *branch=new SoSeparator;auto *translation=new SoTranslation;translation->translation.setValue(i?.5f:-.5f,0,0);branch->addChild(translation);
      auto *material=new SoMaterial;material->diffuseColor.setValue(i?.75f:.5f,i?.25f:.75f,i?.5f:.25f);material->transparency=0;branch->addChild(material);branch->addChild(mesh);root->addChild(branch);
    }
  }
  ~RttScene(){root->unref();}
};
class RttAudit:public Audit {
public:
  mutable CoinRenderRttPlan graph;unsigned producers=0;
  using Audit::Audit;
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan &g,const CoinRenderFramePlan &f,const SbVec2i32 &size)const override{graph=g.inspection(f);return real->preflightRtt(g,f,size);}
  CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan&f,const SbVec2i32&s,uint64_t id,CoinRenderTargetP&t,uint64_t&token)override{++producers;return real->submitRtt(f,s,id,t,token);}
  void finishRtt(const std::vector<uint64_t>&tokens)override{real->finishRtt(tokens);}
  void retireRtt(const std::vector<uint64_t>&tokens)override{real->retireRtt(tokens);}
  bool stagedRttRequiresPreparedParent()const override{return real->stagedRttRequiresPreparedParent();}
};
struct RttTrace {
  FILE *file=nullptr;int saved=-1;
  bool start(){std::fflush(stderr);file=std::tmpfile();if(!file)return false;saved=dup(fileno(stderr));if(saved<0)return false;return dup2(fileno(file),fileno(stderr))>=0;}
  std::string stop(){std::fflush(stderr);if(saved>=0){dup2(saved,fileno(stderr));close(saved);saved=-1;}std::string text;if(file){std::rewind(file);char bytes[4096];size_t count;while((count=std::fread(bytes,1,sizeof(bytes),file)))text.append(bytes,count);std::fclose(file);file=nullptr;}std::cerr<<text;return text;}
  ~RttTrace(){if(saved>=0||file)stop();}
};
bool producerEvents(const std::string &trace,const RttScene &scene,CoinRenderSceneTextureMode mode,unsigned expected) {
  std::istringstream lines(trace);std::string line;unsigned count=0;bool valid=true;
  while(std::getline(lines,line))if(line.find("COIN_RENDER_PHASE rtt_producer_complete ")==0) {
    unsigned long long id=0,revision=0;unsigned faces=0;char route[16]={0};
    const int fields=std::sscanf(line.c_str(),"COIN_RENDER_PHASE rtt_producer_complete producer_id=%llu source_revision=%llu route=%15s faces=%u",&id,&revision,route,&faces);
    valid&=fields==4&&id!=0&&revision==scene.texture->getNodeId()&&faces==1&&std::string(route)==(mode==COIN_RENDER_SCENE_TEXTURE_DIRECT?"direct":"staged");++count;
  }
  std::cout<<"multipass_rtt_completed count="<<count<<" expected="<<expected<<" fields="<<valid<<'\n';
  return check(valid&&count==expected,"actual producer completion/bind once per logical graph, never once per consumer sample");
}
bool rttImage(const Image &image,const RttScene &scene,int n,const char *label) {
  if(!check(image.rgba.size()==side*side*4&&image.depth.size()==side*side,std::string(label)+" complete RTT color/depth"))return false;
  unsigned bad=0,depthBad=0,outside=0,partial=0,quadrants[4]={0,0,0,0};
  for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
    double expected[4]={0,0,0,0};bool last=false,ambiguous=false;unsigned coverage=0;
    for(const auto &j:samples(n)) {
      const double px=x+1-j[0],py=y+j[1];int owner=-1;double u=0,v=0;
      for(unsigned i=0;i<2;++i){const double left=i?36:4;if(px>left&&px<left+24.33&&py>8&&py<56){owner=int(i);u=(px-left)/24.33;v=(56-py)/48;}}
      last=owner>=0;if(!last)continue;++coverage;
      // RTT quality.1 uses LINEAR. Uniform quadrant interiors are separated
      // by >2.1 source texels; no nearest-versus-linear shortcut is assumed.
      if(std::abs(u-.5)<2.1/32||std::abs(v-.5)<2.1/32){ambiguous=true;continue;}
      const unsigned q=(u>.5?1:0)+(v>.5?2:0);++quadrants[q];
      const auto source=scene.source.color(q);const double tint[2][3]={{.5,.75,.25},{.75,.25,.5}};
      for(unsigned c=0;c<3;++c)expected[c]+=source[c]*tint[owner][c]/n;
      expected[3]+=1./n;
    }
    if(ambiguous)continue;
    const size_t p=size_t(y*side+x);
    for(unsigned c=0;c<4;++c)bad+=std::abs(double(image.rgba[p*4+c])-255*expected[c])>(c==3?1:3);
    depthBad+=!std::isfinite(image.depth[p])||std::abs(image.depth[p]-(last?.5f:1.f))>1e-5;
    outside+=coverage==0;partial+=coverage>0&&coverage<unsigned(n);
  }
  std::cout<<"multipass_rtt_image "<<label<<" N="<<n<<" bad="<<bad<<" depth_bad="<<depthBad<<" partial="<<partial<<" outside="<<outside<<" quadrants="<<quadrants[0]<<','<<quadrants[1]<<','<<quadrants[2]<<','<<quadrants[3]<<'\n';
  return check(!bad&&!depthBad&&outside>256&&quadrants[0]>32&&quadrants[1]>32&&quadrants[2]>32&&quadrants[3]>32&&(n==1||partial>=32),std::string(label)+" four independent source quadrants, two shared occurrences and analytic multipass mean/LAST depth");
}
bool rttDelivery(const RttAudit &audit,const RttScene &scene,const CoinRenderOptions &options) {
  bool ok=check(audit.graph.mode==options.sceneTexture&&audit.graph.producers.size()==1,"shared RTT has one producer through exact requested route");
  if(!ok)return false;
  const auto &producer=audit.graph.producers.front();
  ok&=check(producer.sourceRevision==scene.texture->getNodeId()&&!producer.cube&&producer.size==SbVec2i32(32,32)&&producer.format==CoinRenderTextureFormat::RGBA8_LINEAR&&producer.plan.textureSamplingPolicy==options.textureSamplingPolicy,"source identity/extent/format/policy independent of consumer jitter");
  unsigned sourceIndices=0,consumerIndices=0;
  for(const auto &draw:producer.plan.draws)if(!draw.shadowLightSlot&&draw.isSceneLayer())sourceIndices+=draw.geometry.indexCount;
  for(const auto &draw:audit.last.draws)if(draw.sourceNodeId==scene.shape->getNodeId()&&!draw.shadowLightSlot&&draw.isSceneLayer())consumerIndices+=draw.geometry.indexCount;
  ok&=check(sourceIndices==24&&consumerIndices==12&&audit.last.textureSamplingPolicy==options.textureSamplingPolicy,"all source quadrants and both shared consumer occurrences delivered");
  return ok;
}
bool rttPath(const CoinRenderOptions &options,int requested) {
  RttScene scene;std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
  if(!check(bool(target),"public RTT multipass target"))return false;
  auto &p=target->getPimpl().get();
  if(!check(p.prepareBackend()==CoinRenderBackendStatus::SUCCESS&&p.backend&&p.backend->isGpuBackend(),"real RTT GPU executor"))return false;
  auto *audit=new RttAudit(std::move(p.backend));p.backend.reset(audit);target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::NONE);action.setBackgroundColor(SbColor4f(0,0,0,0));
  const int n=requested?requested:4;
  auto draw=[&](const char *label) {
    action.setNumPasses(n);const unsigned mainCaptures=scene.captures,childCaptures=scene.source.captures,producers=audit->producers;
    const auto serial=target->getLastSubmissionSerial(),source=scene.root->getNodeId(),child=scene.source.root->getNodeId();
    RttTrace trace;if(!check(trace.start(),"passive real completion trace capture"))return false;
    action.apply(scene.root);const auto events=trace.stop();
    std::cout<<"multipass_rtt_apply "<<label<<" status="<<action.getLastStatus()<<" serial="<<serial<<"->"<<target->getLastSubmissionSerial()<<" error="<<action.getLastError().getString()<<'\n';
    const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial,"one final RTT consumer publication");
    bool ok=success;
    ok&=check(scene.captures==mainCaptures+1&&scene.source.captures==childCaptures+1&&source==scene.root->getNodeId()&&child==scene.source.root->getNodeId(),"consumer and independent producer captured once, source fields intact");
    ok&=producerEvents(events,scene,options.sceneTexture,1);
    ok&=check(audit->producers==producers+(options.sceneTexture==COIN_RENDER_SCENE_TEXTURE_DIRECT?1:0),"DIRECT actual producer count; STAGED factory remains separate from root proxy");
    if(success){rttDelivery(*audit,scene,options);rttImage(read(target.get()),scene,n,label);}return ok;
  };
  draw("four-quadrant source predecessor");scene.source.paint(true);draw("independent source mutation");scene.source.paint(false);draw("source authorial restoration");
  const auto stable=read(target.get());const auto serial=target->getLastSubmissionSerial();
  const unsigned commands=audit->commands,producers=audit->producers;
  action.setNumPasses(0);RttTrace trace;check(trace.start(),"refusal passive completion capture");action.apply(scene.root);const auto events=trace.stop();
  check(action.getLastStatus()==CoinRenderAction::INVALID_SCENE&&audit->commands==commands&&audit->producers==producers&&target->getLastSubmissionSerial()==serial&&exact(stable,read(target.get())),"invalid count precedes every producer/root and preserves exact RTT publication");
  producerEvents(events,scene,options.sceneTexture,0);draw("RTT same-target recovery");
  action.setRenderTarget(nullptr);return good;
}
#endif
