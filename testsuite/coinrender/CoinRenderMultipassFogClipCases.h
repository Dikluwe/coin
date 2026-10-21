#ifndef COIN_RENDER_MULTIPASS_FOG_CLIP_CASES_H
#define COIN_RENDER_MULTIPASS_FOG_CLIP_CASES_H
double multipassFogFactor(int mode) {
  const double distance=3,visibility=8;
  if(mode==SoEnvironment::HAZE)return 1-distance/visibility;
  if(mode==SoEnvironment::FOG)return std::exp(-5.545*distance/visibility);
  if(mode==SoEnvironment::SMOKE)return std::exp(-std::pow(2.35*distance/visibility,2));
  return 1;
}
bool fogClipImage(const Image &image,int mode,int n,bool clipped,const char *label) {
  if(!check(image.rgba.size()==side*side*4&&image.depth.size()==side*side,std::string(label)+" complete fog/clip result"))return false;
  const double factor=multipassFogFactor(mode),diffuse[]={.25,.5,.75},fog[]={.75,.25,.125};
  unsigned bad=0,depthBad=0,partial=0,full=0,outside=0;
  for(int y=16;y<48;++y)for(int x=0;x<side;++x) {
    unsigned covered=0;bool last=false;
    for(const auto &j:samples(n)) {const double original=x+1-j[0];last=original>(clipped?24.32:8)&&original<32.33;covered+=last;}
    const double fraction=double(covered)/n;const size_t p=size_t(y*side+x);
    for(unsigned c=0;c<3;++c)bad+=std::abs(double(image.rgba[p*4+c])-255*(diffuse[c]*factor+fog[c]*(1-factor))*fraction)>3;
    bad+=std::abs(double(image.rgba[p*4+3])-255*fraction)>1;
    depthBad+=!std::isfinite(image.depth[p])||std::abs(image.depth[p]-(last?.5f:1.f))>1e-5;
    partial+=covered>0&&covered<unsigned(n);full+=covered==unsigned(n);outside+=covered==0;
  }
  std::cout<<"multipass_fog_clip "<<label<<" mode="<<mode<<" N="<<n<<" factor="<<factor<<" bad="<<bad<<" depth_bad="<<depthBad<<" partial="<<partial<<" full="<<full<<" outside="<<outside<<'\n';
  return check(!bad&&!depthBad&&full>128&&outside>512&&(n==1||partial>=32),std::string(label)+" independent eye-depth fog before RGBA average, projected world clip and LAST depth");
}
bool fogClipDelivery(const CoinRenderFramePlan &frame,int mode,bool clipped) {
  CoinRenderFogMode expected=CoinRenderFogMode::NONE;
  if(mode==SoEnvironment::HAZE)expected=CoinRenderFogMode::HAZE;
  else if(mode==SoEnvironment::FOG)expected=CoinRenderFogMode::FOG;
  else if(mode==SoEnvironment::SMOKE)expected=CoinRenderFogMode::SMOKE;
  bool ok=true;unsigned indices=0;
  for(const auto &draw:frame.draws)if(draw.isSceneLayer()&&!draw.shadowLightSlot) {
    if(!check(draw.renderStateSlot<frame.renderStates.size(),"fog/clip draw actual state")){ok=false;continue;}
    const auto &s=frame.renderStates[draw.renderStateSlot];indices+=draw.geometry.indexCount;
    ok&=check(s.fogMode==expected&&s.fogEnd==8&&s.fogColor[0]==.75f&&s.fogColor[1]==.25f&&s.fogColor[2]==.125f&&s.clipPlanesWorld.size()==size_t(clipped?1:0),"authorial fog and effective clip retained for every sample");
  }
  CoinWgpuFfiFrame ffi;std::string error;const bool packed=ffi.prepare(frame,side,side,error,false);ok&=check(packed,"fog/clip actual FFI: "+error);
  if(packed)for(uint64_t d=0;d<ffi.getView().draw_count;++d){const auto &draw=ffi.getView().draws[d];if(draw.render_state_slot>=ffi.getView().state_count){ok=false;continue;}const auto &s=ffi.getView().states[draw.render_state_slot];ok&=check(s.fog_mode==uint32_t(expected)&&s.fog_end==8&&s.clip_plane_count==unsigned(clipped?1:0),"FFI exact consumed fog and clip state");}
  return check(indices==6,"one actual clipped/fogged quad")&&ok;
}
bool fogClipPath(bool gpu,const CoinRenderOptions &options,int requested) {
  Scene scene;auto *environment=new SoEnvironment;
  environment->ambientIntensity=0;environment->fogVisibility=8;environment->fogColor.setValue(.75f,.25f,.125f);environment->fogType=SoEnvironment::NONE;scene.root->insertChild(environment,1);
  auto *clip=new SoClipPlane;clip->plane=SbPlane(SbVec3f(1,0,0),-.24f);scene.root->insertChild(clip,scene.root->getNumChildren()-2);
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));Audit *audit=prepare(target.get(),gpu);if(!audit)return false;
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::NONE);action.setBackgroundColor(SbColor4f(0,0,0,0));
  auto draw=[&](int n,const char *label){action.setNumPasses(n);const auto serial=target->getLastSubmissionSerial(),source=scene.root->getNodeId();const unsigned callbacks=scene.captures;action.apply(scene.root);const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial,std::string(label)+" valid fog/clip publication: "+action.getLastError().getString());check(callbacks+1==scene.captures&&source==scene.root->getNodeId(),"fog/clip does not multiply traversal or mutate source");if(success){fogClipDelivery(audit->last,environment->fogType.getValue(),clip->on.getValue());fogClipImage(read(target.get()),environment->fogType.getValue(),n,clip->on.getValue(),label);}};
  const int n=requested?requested:3;
  draw(1,"one-pass world clip predecessor");
  for(int mode:{SoEnvironment::NONE,SoEnvironment::HAZE,SoEnvironment::FOG,SoEnvironment::SMOKE,SoEnvironment::NONE}){environment->fogType=mode;draw(n,"fog mode transition/restoration");}
  clip->on=FALSE;draw(n,"clip disabled");clip->on=TRUE;draw(n,"clip restored");
  action.setRenderTarget(nullptr);return good;
}
#endif
