#ifndef COIN_RENDER_MULTIPASS_SHADOW_CASES_H
#define COIN_RENDER_MULTIPASS_SHADOW_CASES_H
struct MultipassShadowScene {
  SoSeparator *root=new SoSeparator;
  SoShadowGroup *group=new SoShadowGroup;
  SoFaceSet *receiver=new SoFaceSet;
  std::vector<SoTranslation*> moves;
  std::vector<SoCube*> casters;
  unsigned captures=0;bool moved=false;
  static void visit(void *data,SoAction *action){if(action->isOfType(SoCallbackAction::getClassTypeId()))++static_cast<MultipassShadowScene*>(data)->captures;}
  explicit MultipassShadowScene(unsigned count) {
    root->ref();auto *camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,3);camera->height=2;camera->nearDistance=1;camera->farDistance=5;camera->viewportMapping=SoCamera::LEAVE_ALONE;root->addChild(camera);
    auto *callback=new SoCallback;callback->setCallback(visit,this);root->addChild(callback);
    auto *phong=new SoLightModel;phong->model=SoLightModel::PHONG;root->addChild(phong);
    group->quality=1;group->precision=.125f;group->epsilon=0;group->threshold=0;group->intensity=1;root->addChild(group);
    const double scale=std::sqrt(6.);
    for(unsigned i=0;i<count;++i) {
      auto *map=new SoSeparator;auto *mapCamera=new SoOrthographicCamera;
      mapCamera->position.setValue(float(.01*i),0,3);mapCamera->height=2;mapCamera->nearDistance=1;mapCamera->farDistance=float(5+.125*i);map->addChild(mapCamera);
      auto *cast=new SoShadowStyle;cast->style=SoShadowStyle::CASTS_SHADOW;map->addChild(cast);
      auto *translation=new SoTranslation;translation->translation.setValue(float(.01*i-.64/scale),float(-.14/scale),.75f);map->addChild(translation);moves.push_back(translation);
      auto *cube=new SoCube;cube->width=cube->height=float(.26/scale);cube->depth=.2f;map->addChild(cube);casters.push_back(cube);
      auto *light=new SoShadowDirectionalLight;light->direction.setValue(0,0,-1);light->intensity=float(.8/count);light->on=TRUE;
      light->bboxCenter.setValue(0,0,0);light->bboxSize.setValue(2,2,4);light->shadowMapScene=map;group->addChild(light);
    }
    auto *style=new SoShadowStyle;style->style=SoShadowStyle::SHADOWED;group->addChild(style);
    auto *material=new SoMaterial;material->diffuseColor.setValue(.6f,.3f,.8f);material->ambientColor.setValue(0,0,0);material->emissiveColor.setValue(0,0,0);material->specularColor.setValue(0,0,0);material->transparency=0;group->addChild(material);
    const SbVec3f points[]={SbVec3f(-.89f,-.89f,0),SbVec3f(.89f,-.89f,0),SbVec3f(.89f,.89f,0),SbVec3f(-.89f,.89f,0)};
    auto *coordinates=new SoCoordinate3;coordinates->point.setValues(0,4,points);group->addChild(coordinates);
    auto *normals=new SoNormal;const SbVec3f values[]={SbVec3f(0,0,1),SbVec3f(0,0,1),SbVec3f(0,0,1),SbVec3f(0,0,1)};normals->vector.setValues(0,4,values);group->addChild(normals);
    receiver->numVertices=4;group->addChild(receiver);
  }
  void move(bool value) {
    moved=value;const unsigned i=unsigned(moves.size()-1);const double scale=std::sqrt(6.);
    moves[i]->translation.setValue(float(.01*i+(value?1.6:-.64)/scale),float((value?1.6:-.14)/scale),.75f);
  }
  ~MultipassShadowScene(){root->unref();}
};
bool shadowImage(const Image &image,const MultipassShadowScene &scene,int n,const char *label) {
  if(!check(image.rgba.size()==side*side*4&&image.depth.size()==side*side,std::string(label)+" full VSM multipass color/depth"))return false;
  unsigned bad=0,depthBad=0,lit=0,dark=0,partial=0,outside=0;
  const double margin=2.1*2/side;
  for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
    double expected[4]={0,0,0,0};unsigned cover=0;bool last=false,unsafe=false,allDark=true,allLit=true;
    for(const auto &j:samples(n)) {
      const double wx=2*(x+1-j[0])/side-1,wy=1-2*(y+j[1])/side;
      last=std::abs(wx)<.89&&std::abs(wy)<.89;
      if(!last){allDark=false;allLit=false;continue;}++cover;
      // Independent dual-camera derivation: authorial map camera has NDC XY
      // equal to worldXY-cameraXY, while the directional receiver lookup at
      // z0 uses worldXY/sqrt(6). Caster width .26/sqrt(6) therefore makes a
      // .26-square receiver shadow centered at(-.64,-.14), irrespective of N.
      const double distance=std::min(.13-std::abs(wx+.64),.13-std::abs(wy+.14));
      if(std::abs(distance)<=margin){unsafe=true;continue;}
      const bool shadow=distance>margin;allDark&=shadow;allLit&=!shadow;
      const double visibility=shadow?(scene.moved?1./scene.moves.size():0):1;
      const double diffuse[]={.6,.3,.8};
      for(unsigned c=0;c<3;++c)expected[c]+=.8*diffuse[c]*visibility/n;
      expected[3]+=1./n;
    }
    if(unsafe)continue;
    const size_t p=size_t(y*side+x);
    for(unsigned c=0;c<4;++c)bad+=std::abs(double(image.rgba[p*4+c])-255*expected[c])>(c==3?1:3);
    depthBad+=!std::isfinite(image.depth[p])||std::abs(image.depth[p]-(last?.5f:1.f))>1e-5;
    lit+=allLit;dark+=allDark;partial+=cover>0&&cover<unsigned(n);outside+=cover==0;
  }
  std::cout<<"multipass_shadow "<<label<<" N="<<n<<" maps="<<scene.moves.size()<<" lit="<<lit<<" shadow="<<dark<<" partial="<<partial<<" outside="<<outside<<" bad="<<bad<<" depth_bad="<<depthBad<<'\n';
  return check(!bad&&!depthBad&&lit>500&&dark>=9&&outside>128&&(n==1||partial>16),std::string(label)+" unchanged VSM lookup/casters with current jittered receiver, mean RGBA and LAST depth");
}
bool shadowDelivery(const CoinRenderFramePlan &frame,const MultipassShadowScene &scene) {
  CoinRenderShadowPlan plan;std::string error;
  if(!check(coin_render_plan_shadows(frame,plan,error),"actual VSM Core plan: "+error))return false;
  bool ok=check(plan.passes.size()==scene.moves.size()&&frame.shadowGroups.size()==1&&frame.shadowLights.size()==scene.moves.size(),"all independently authored maps remain consumed");
  for(const auto &pass:plan.passes) {
    if(!check(pass.lightSlot<scene.casters.size(),"VSM source association range")){ok=false;continue;}
    unsigned casters=0,receivers=0;
    for(unsigned index:pass.casterDraws) {
      if(!check(index<frame.draws.size(),"actual caster range")){ok=false;continue;}
      const auto &draw=frame.draws[index];casters+=draw.geometry.indexCount;
      ok&=check(draw.sourceNodeId==scene.casters[pass.lightSlot]->getNodeId()&&draw.shadowLightSlot==pass.lightSlot+1,"each unchanged map owns its original caster occurrence");
    }
    for(unsigned index:pass.receiverDraws){if(index>=frame.draws.size()){ok=false;continue;}const auto &draw=frame.draws[index];receivers+=draw.geometry.indexCount;ok&=check(draw.sourceNodeId==scene.receiver->getNodeId()&&!draw.shadowLightSlot,"VSM receiver remains actual main surface");}
    ok&=check(pass.visible&&pass.mapSize==256&&casters==36&&receivers==6,"nonempty original caster/receiver coverage, independent of multipass count");
  }
  CoinWgpuFfiFrame ffi;
  const bool packed=ffi.prepare(frame,side,side,error,false);ok&=check(packed,"real FFI VSM transport: "+error);
  return ok;
}
bool shadowPath(const CoinRenderOptions &options,int requested,unsigned count) {
  MultipassShadowScene scene(count);
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
  Audit *audit=prepare(target.get(),true);if(!audit)return false;
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::NONE);action.setBackgroundColor(SbColor4f(0,0,0,0));
  auto draw=[&](int n,const char *label){action.setNumPasses(n);const auto serial=target->getLastSubmissionSerial(),source=scene.root->getNodeId();const unsigned captures=scene.captures;action.apply(scene.root);const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial,std::string(label)+" current shadow publication: "+action.getLastError().getString());check(scene.captures==captures+1&&source==scene.root->getNodeId(),"VSM multipass capture once, no source mutation");if(success){shadowDelivery(audit->last,scene);shadowImage(read(target.get()),scene,n,label);}};
  const int n=requested?requested:4;
  draw(1,"ordinary VSM predecessor");draw(n,"VSM multipass");scene.move(true);draw(n,"last associated caster mutation");scene.move(false);draw(n,"VSM authorial restoration");
  const auto stable=read(target.get());const auto serial=target->getLastSubmissionSerial();const auto commands=audit->commands;
  action.setMultipassBufferBudget(0);action.apply(scene.root);
  check(action.getLastStatus()==CoinRenderAction::OUT_OF_MEMORY&&audit->commands==commands&&target->getLastSubmissionSerial()==serial&&exact(stable,read(target.get())),"VSM scratch budget refuses before map/root execution with exact publication");
  action.setMultipassBufferBudget(256ull*1024*1024);draw(n,"VSM same-target recovery");
  action.setRenderTarget(nullptr);return good;
}
#endif
