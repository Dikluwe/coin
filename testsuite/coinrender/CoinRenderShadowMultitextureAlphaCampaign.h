#ifndef COIN_RENDER_SHADOW_MULTITEXTURE_ALPHA_CAMPAIGN_H
#define COIN_RENDER_SHADOW_MULTITEXTURE_ALPHA_CAMPAIGN_H
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/SbPlane.h>
#include "rendering/coinrender/CoinRenderShadowCore.h"
#include "actions/CoinRenderActionP.h"
#include <cctype>
namespace {
unsigned alphaChanges(const Frame&a,const Frame&b){
  if(a.rgba.size()!=b.rgba.size())return 0;unsigned changed=0;
  for(size_t i=0;i<a.rgba.size();i+=4)for(int c=0;c<3;++c)changed+=std::abs(int(a.rgba[i+c])-int(b.rgba[i+c]))>3;
  return changed;
}
// Alpha scaling changes background transmittance; source tint keeps it fixed.
// A=As*S+(1-As)*D, so tint B=k*A+(1-k)*(1-As)*D.
// For alpha scaling instead, B=k*A+(1-k)*D.
bool alphaScale(const Frame&a,const Frame&b,double scale,int channel=-1,double fixedSourceAlpha=-1.){
  if(a.rgba.size()!=b.rgba.size()||a.depth!=b.depth)return false;unsigned checked=0,bad=0;
  for(int y=side/4;y<3*side/4;++y)for(int x=side/4;x<3*side/4;++x){const size_t p=size_t(y*side+x);++checked;
    for(int c=0;c<3;++c){const double k=channel<0||channel==c?scale:1.;
      const double backgroundTerm=alphaBackground[c]*(fixedSourceAlpha<0?1.:1.-fixedSourceAlpha);
      const int expected=int(std::lround(backgroundTerm+(a.rgba[4*p+c]-backgroundTerm)*k));
      const bool wrong=std::abs(int(b.rgba[4*p+c])-expected)>3;
      if(wrong&&bad==0){std::cout<<"alpha_scale first x="<<x<<" y="<<y<<" channel="<<c
        <<" A="<<int(a.rgba[4*p+c])<<" B="<<int(b.rgba[4*p+c])<<" expected="<<expected
        <<" scale="<<scale<<" selected_channel="<<channel<<" Aalpha="<<int(a.rgba[4*p+3])<<" Balpha="<<int(b.rgba[4*p+3])
        <<" Adepth="<<a.depth[p]<<" Bdepth="<<b.depth[p]<<'\n';
        for(int diagnosticChannel=0;diagnosticChannel<3;++diagnosticChannel){const double diagnosticScale=channel<0||channel==diagnosticChannel?scale:1.;
          std::cout<<"alpha_scale RGB channel="<<diagnosticChannel<<" A="<<int(a.rgba[4*p+diagnosticChannel])<<" actual="<<int(b.rgba[4*p+diagnosticChannel])
            <<" expected="<<int(std::lround(alphaBackground[diagnosticChannel]*(fixedSourceAlpha<0?1.:1.-fixedSourceAlpha)+(a.rgba[4*p+diagnosticChannel]-alphaBackground[diagnosticChannel]*(fixedSourceAlpha<0?1.:1.-fixedSourceAlpha))*diagnosticScale))<<'\n';}}
      bad+=wrong;}
    bad+=std::abs(int(b.rgba[4*p+3])-255)>1;
  }
  return check(checked>0&&bad==0,"independent alpha/texture cascade follows analytical source-over scaling and preserves depth");
}
void alphaMapDiagnostic(const CoinRenderFramePlan&frame,const std::string&phase,int extent){
  CoinRenderShadowPlan plan;std::string why;const bool valid=coin_render_plan_shadows(frame,plan,why);
  std::cout<<"alpha_maps phase="<<phase<<" extent="<<extent<<" core_plan_valid="<<valid<<" logical_bytes="<<plan.attachmentBytes<<" diagnostic="<<why<<'\n';
  for(const auto&pass:plan.passes)std::cout<<"alpha_maps resolved_core light="<<pass.lightSlot<<" group="<<pass.groupSlot
    <<" map_extent="<<pass.mapSize<<" visible="<<pass.visible<<'\n';
}
void alphaFrameDiagnostic(const Frame&a,const Frame&b,const std::string&phase,int extent){
  unsigned changedAny=0,changedAboveGate=0,wrongDepth=0;int maxRgb=0;double maxDepth=0;
  const size_t colors=std::min(a.rgba.size(),b.rgba.size()),depths=std::min(a.depth.size(),b.depth.size());
  for(size_t i=0;i+3<colors;i+=4)for(int c=0;c<3;++c){const int delta=std::abs(int(a.rgba[i+c])-int(b.rgba[i+c]));changedAny+=delta>0;changedAboveGate+=delta>3;maxRgb=std::max(maxRgb,delta);}
  float minA=std::numeric_limits<float>::infinity(),maxA=-minA,minB=minA,maxB=-minA;
  for(size_t p=0;p<depths;++p){if(std::isfinite(a.depth[p])){minA=std::min(minA,a.depth[p]);maxA=std::max(maxA,a.depth[p]);}
    if(std::isfinite(b.depth[p])){minB=std::min(minB,b.depth[p]);maxB=std::max(maxB,b.depth[p]);}
    maxDepth=std::max(maxDepth,std::abs(double(a.depth[p])-b.depth[p]));if(a.depth[p]!=b.depth[p]){
      if(wrongDepth==0)std::cout<<"alpha_frame first_depth phase="<<phase<<" x="<<p%extent<<" y="<<p/extent
        <<" A="<<std::hexfloat<<a.depth[p]<<" B="<<b.depth[p]<<std::defaultfloat<<'\n';++wrongDepth;}}
  std::cout<<"alpha_frame phase="<<phase<<" extent="<<extent<<" valid_region=entire_attachment RGB_bytes="<<a.rgba.size()<<','<<b.rgba.size()
    <<" RGB_changed_any="<<changedAny<<" RGB_changed_gt3="<<changedAboveGate<<" max_RGB_delta="<<maxRgb
    <<" depth_count="<<a.depth.size()<<','<<b.depth.size()<<" depth_different="<<wrongDepth<<" max_depth_delta="<<maxDepth
    <<" depth_A_minmax="<<std::hexfloat<<minA<<','<<maxA<<" depth_B_minmax="<<minB<<','<<maxB<<std::defaultfloat<<'\n';
}
bool alphaDomain(WindowPair&h,int selectedMaps=0){
  bool ok=true;const bool oit=h.off->getOptions().transparency!=COIN_RENDER_TRANSPARENCY_OBJECT;
  // Orthogonal focal cells avoid repeating all historical binding/model sweeps.
  const int mapCounts[]={1,2,4,8,6},textureCounts[]={2,4,8,8,8};
  for(unsigned cell=0;cell<5;++cell){if(selectedMaps>0&&mapCounts[cell]!=selectedMaps)continue;AlphaScene s(mapCounts[cell],textureCounts[cell],cell%2,cell%2?.2f:1.f);
    s.mode->value=oit?SoGLRenderAction::SORTED_LAYERS_BLEND:SoGLRenderAction::SORTED_OBJECT_BLEND;
    s.receiverStyle->style=SoShadowStyle::SHADOWED;s.lighting->model=SoLightModel::PHONG;
    s.receiverMaterial->diffuseColor.setValue(1,1,1);s.receiverMaterial->transparency=.125f;
    for(int u:s.units){const uint8_t pixel[]={255,255,255,uint8_t(u==7?192:255)};
      s.images[u]->image.setValue(SbVec2s(1,1),4,pixel);}
    Frame a,b,c;const bool first=alphaFrame(h,s,a,"PHONG domain A "+label(s));ok &= first;if(!first)continue;
    ok &= payload(s,h.offProxy->plan,s.rttTexture,h.off->getOptions().sceneTexture==COIN_RENDER_SCENE_TEXTURE_DIRECT)&&
      payload(s,h.windowProxy->plan,s.rttTexture,h.shown->getOptions().sceneTexture==COIN_RENDER_SCENE_TEXTURE_DIRECT);
    for(int unit:s.units){if(alphaRtt&&unit==0){s.rttMaterial->diffuseColor.setValue(.5f,1,1);
      const bool changed=alphaFrame(h,s,b,"RTT producer independent tint");ok &= changed;if(changed)ok &= alphaScale(a,b,.5,0,.875*192./255.)&&check(alphaChanges(a,b)>0,"RTT producer visibly contributes to the transparent receiver");
      s.rttMaterial->diffuseColor.setValue(1,1,1);const bool restored=alphaFrame(h,s,c,"RTT producer tint A restored");ok &= restored;if(restored)ok &= exact(a,c);continue;}const uint8_t tinted[]={128,255,255,uint8_t(unit==7?192:255)};
      s.images[unit]->image.setValue(SbVec2s(1,1),4,tinted);const bool changed=alphaFrame(h,s,b,"independent texture "+std::to_string(unit));ok &= changed;
      if(changed)ok &= alphaScale(a,b,128./255.,0,.875*192./255.)&&check(alphaChanges(a,b)>0,"each texture contributes to transparent PHONG output");
      const uint8_t white[]={255,255,255,uint8_t(unit==7?192:255)};s.images[unit]->image.setValue(SbVec2s(1,1),4,white);
      const bool restored=alphaFrame(h,s,c,"texture A restored");ok &= restored;if(restored)ok &= exact(a,c);
    }
    const uint8_t halfAlpha[]={255,255,255,96};s.images[7]->image.setValue(SbVec2s(1,1),4,halfAlpha);
    const bool alphaB=alphaFrame(h,s,b,"independent unit7 alpha B",true);ok &= alphaB;
    if(alphaB)ok &= alphaScale(a,b,.5)&&check(alphaChanges(a,b)>0,"unit7 alpha is independent of material opacity");
    const uint8_t original[]={255,255,255,192};s.images[7]->image.setValue(SbVec2s(1,1),4,original);
    const bool back=alphaFrame(h,s,c,"unit7 alpha A restored",true);ok &= back;if(back)ok &= exact(a,c);
    // Every independently authored shadow scene must update, including when
    // its neighbors remain unchanged. Never use a cross-adapter image oracle.
    for(int map=0;map<s.slots;++map){const auto old=s.casterPositions[map]->translation.getValue();
      s.casterX(map,std::abs(old[0])>.3f?0.f:1.5f);const bool changed=alphaFrame(h,s,b,"caster map B "+std::to_string(map));ok &= changed;
      if(changed){alphaFrameDiagnostic(a,b,"caster "+label(s)+" map="+std::to_string(map),h.extent);alphaMapDiagnostic(h.offProxy->plan,"caster B",h.extent);
        ok &= check(alphaChanges(a,b)>0&&a.depth==b.depth,"each real map changes PHONG color without changing receiver depth");}
      s.casterPositions[map]->translation=old;const bool restored=alphaFrame(h,s,c,"caster map A restored");ok &= restored;if(restored)ok &= exact(a,c);
    }
  }
  return ok;
}
bool alphaOpaqueAndScreen(WindowPair&h){
  if(h.off->getOptions().transparency!=COIN_RENDER_TRANSPARENCY_OBJECT)return true;
  AlphaScene s;Frame a,b,c;s.sourceAlpha(255);s.mode->value=SoGLRenderAction::NONE;s.receiverMaterial->transparency=0;s.depth->write=TRUE;
  auto*camera=static_cast<SoOrthographicCamera*>(s.root->getChild(0));camera->position.setValue(0,0,4);camera->nearDistance=1;camera->farDistance=17;
  for(int i=0;i<9;++i){auto p=s.positions->point[i];p[2]=-5.f;s.positions->point.set1Value(i,p);}
  bool ok=alphaFrame(h,s,a,"NONE opaque geometry control",true);if(!ok)return false;
  unsigned bad=0;for(int y=side/4;y<3*side/4;++y)for(int x=side/4;x<3*side/4;++x){const size_t p=size_t(y*side+x);
    for(int k=0;k<3;++k){const int rgb[]={32,48,64};bad+=std::abs(int(a.rgba[4*p+k])-rgb[k])>3;}
    bad+=a.rgba[4*p+3]!=255;bad+=a.depth[p]!=.5f;}
  ok &= check(bad==0,"NONE keeps known opaque RGB/alpha and geometrical receiver depth");
  // NONE writes its sampled RGB and authored alpha without blending. A
  // transparent clear makes partial/zero source alpha observable; even alpha0
  // must retain the explicitly requested geometric depth write.
  for(float alpha:{.5f,0.f}){
    s.receiverMaterial->transparency=1.f-alpha;
    h.offAction.setBackgroundColor(SbColor4f(16.f/255,24.f/255,32.f/255,0));
    const auto before=h.off->getLastSubmissionSerial();const unsigned roots=h.offProxy->submits;
    h.offAction.apply(s.root);Frame witness;h.off->readbackRGBA(witness.rgba);h.off->readbackDepth(witness.depth);witness.serial=h.off->getLastSubmissionSerial();
    const unsigned step=1+((h.caps.backend==COIN_RENDER_EXPERIMENTAL_RUST||h.off->getOptions().sceneTexture==COIN_RENDER_SCENE_TEXTURE_DIRECT)&&alphaRtt?1:0);
    const uint64_t expected=h.caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?h.latest+step:before+step;
    const bool published=h.offAction.getLastStatus()==CoinRenderAction::SUCCESS&&witness.serial==expected&&h.offProxy->submits==roots+1&&
      witness.rgba.size()==size_t(side)*side*4&&witness.depth.size()==size_t(side)*side;
    ok &= check(published,"NONE partial/zero alpha publishes one complete offscreen frame");
    if(published){unsigned wrong=0;const int expectedAlpha=int(std::lround(255.*alpha));
      for(int y=side/4;y<3*side/4;++y)for(int x=side/4;x<3*side/4;++x){const size_t p=size_t(y*side+x);
        for(int channel=0;channel<3;++channel){const int rgb[]={32,48,64};wrong+=std::abs(int(witness.rgba[4*p+channel])-rgb[channel])>3;}
        wrong+=std::abs(int(witness.rgba[4*p+3])-expectedAlpha)>3;wrong+=witness.depth[p]!=.5f;}
      ok &= check(wrong==0,"NONE conserves sampled RGB/source alpha and writes depth even for alpha0");}
    std::cout<<"shadow_multitexture_alpha NONE_alpha="<<alpha<<" status="<<int(h.offAction.getLastStatus())
      <<" serial="<<before<<','<<witness.serial<<" error="<<h.offAction.getLastError().getString()<<'\n';
    h.publishedOff=witness.serial;h.latest=witness.serial;
  }
  h.offAction.setBackgroundColor(SbColor4f(16.f/255,24.f/255,32.f/255,1));
  s.mode->value=SoGLRenderAction::SCREEN_DOOR;s.receiverMaterial->transparency=.5f;s.depth->write=FALSE;
  const bool screen=alphaFrame(h,s,b,"SCREEN_DOOR half material coverage",true);ok &= screen;
  if(screen){const auto classify=[&](const std::vector<uint8_t>&pixels,const char*surface,bool report=true){unsigned source=0,background=0,wrong=0;
    if(pixels.size()!=b.rgba.size()){if(report)std::cout<<"alpha_screen surface="<<surface<<" bytes="<<pixels.size()<<" expected_bytes="<<b.rgba.size()<<'\n';return false;}
    for(int y=side/4;y<3*side/4;++y)for(int x=side/4;x<3*side/4;++x){const size_t p=size_t(y*side+x);bool src=true,bg=true;
      for(int k=0;k<3;++k){const int rgb[]={32,48,64};src &= std::abs(int(pixels[4*p+k])-rgb[k])<=3;
        bg &= std::abs(int(pixels[4*p+k])-int(alphaBackground[k]))<=3;}
      if(report&&!src&&!bg&&wrong==0)std::cout<<"alpha_screen first surface="<<surface<<" x="<<x<<" y="<<y
        <<" RGBA="<<int(pixels[4*p])<<','<<int(pixels[4*p+1])<<','<<int(pixels[4*p+2])<<','<<int(pixels[4*p+3])
        <<" expected_source_RGB=32,48,64 expected_background_RGB=16,24,32 samples="<<windowSamples<<'\n';
      source+=src;background+=bg;wrong+=!src&&!bg;}
    if(report)std::cout<<"alpha_screen surface="<<surface<<" source="<<source<<" background="<<background<<" wrong="<<wrong<<'\n';
    return source>0&&background>0&&wrong==0;};
    // Evaluate every surface and depth independently so a first failure cannot
    // hide whether the other surfaces exhibit the same sample resolve.
    const bool attachment=classify(b.rgba,"attachment"),readback=classify(h.lastReadback,"readback");
    // The generic smooth-interior observer deliberately excludes stipple.
    // Wait for this frame's binary source/background witness at the physical
    // surface; never resubmit to make an asynchronous presentation visible.
    const auto started=std::chrono::steady_clock::now(),until=started+std::chrono::seconds(5);
    unsigned polls=0;bool physicalReady=false;
    do{++polls;h.lastShown=ximage(h.d,h.window,h.extent);
      if(classify(h.lastShown,"physical-poll",false)){physicalReady=true;break;}
      if(std::chrono::steady_clock::now()>=until)break;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }while(true);
    const double latencyMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    std::cout<<"alpha_screen physical_ready="<<physicalReady<<" polls="<<polls<<" latency_ms="<<latencyMs
      <<" published_window_serial="<<h.publishedWindow<<'\n';
    const bool physical=classify(h.lastShown,"physical")&&physicalReady;
    unsigned depthWrong=0,nonfiniteDepth=0;float minimumDepth=std::numeric_limits<float>::infinity(),maximumDepth=-std::numeric_limits<float>::infinity();
    for(size_t p=0;p<b.depth.size();++p){const float value=b.depth[p];nonfiniteDepth+=!std::isfinite(value);
      if(std::isfinite(value)){minimumDepth=std::min(minimumDepth,value);maximumDepth=std::max(maximumDepth,value);}
      if(value!=1.f){if(depthWrong==0)std::cout<<"alpha_screen first_depth x="<<p%side<<" y="<<p/side<<" actual="<<value<<" expected=1\n";++depthWrong;}}
    std::cout<<"alpha_screen depth_count="<<b.depth.size()<<" wrong="<<depthWrong<<" nonfinite="<<nonfiniteDepth
      <<" minimum="<<minimumDepth<<" maximum="<<maximumDepth<<'\n';
    ok &= check(attachment&&physical&&readback&&depthWrong==0,
      "SCREEN_DOOR exposes both source/background in attachment/physical/readback without blending or depth writes");
  }
  s.mode->value=SoGLRenderAction::NONE;s.receiverMaterial->transparency=0;s.depth->write=TRUE;
  const bool restored=alphaFrame(h,s,c,"NONE restored after stipple",true);ok &= restored;if(restored)ok &= exact(a,c);return ok;
}
bool alphaCoverage(WindowPair&h){
  if(windowSamples==1)return true;AlphaScene s;s.receiverMaterial->diffuseColor.setValue(1,1,1);s.receiverMaterial->transparency=.5f;
  const uint8_t white[]={255,255,255,255};s.images[7]->image.setValue(SbVec2s(1,1),4,white);
  s.mode->value=h.off->getOptions().transparency==COIN_RENDER_TRANSPARENCY_OBJECT?SoGLRenderAction::BLEND:SoGLRenderAction::SORTED_LAYERS_BLEND;
  for(int x=0;x<3;++x){auto p=s.positions->point[6+x];p[1]=1.31f+.22f*x;s.positions->point.set1Value(6+x,p);}
  Frame f;if(!alphaFrame(h,s,f,"transparent physical MSAA edge witness",true))return false;
  const auto levels=[&](const std::vector<uint8_t>&pixels){std::set<int>found;unsigned bad=0;
    for(size_t p=0;p<pixels.size();p+=4){const double fraction=(pixels[p]-alphaBackground[0])/(.5*(255.-alphaBackground[0]));
      const int k=int(std::lround(fraction*windowSamples));if(k<=0||k>=int(windowSamples))continue;found.insert(k);
      for(int c=0;c<3;++c){const double solid=.5*255.+.5*alphaBackground[c];
        const int expected=int(std::lround(alphaBackground[c]+(solid-alphaBackground[c])*k/windowSamples));
        bad+=std::abs(int(pixels[p+c])-expected)>3;}
    }
    const bool witnesses=windowSamples==2?found.count(1)>0:found.count(1)>0&&found.count(3)>0;
    return check(witnesses&&bad==0,"transparent attachment/physical/readback resolve contains actual N-sample edge levels");};
  return levels(f.rgba)&&levels(h.lastShown)&&levels(h.lastReadback);
}
bool alphaAtomic(WindowPair&h){
  AlphaScene s;s.mode->value=h.off->getOptions().transparency==COIN_RENDER_TRANSPARENCY_OBJECT?SoGLRenderAction::BLEND:SoGLRenderAction::SORTED_LAYERS_BLEND;
  Frame a,b;bool ok=alphaFrame(h,s,a,"atomic transparent A",true);if(!ok)return false;
  const auto physical=h.lastShown;const auto old=s.coords[7]->point[0];
  const char*fault="COIN_BGFX_TEST_SHADOW_MAP_ALLOC_ONCE";const char*oldFault=std::getenv(fault);const bool hadFault=oldFault!=nullptr;const std::string savedFault=oldFault?oldFault:"";
  const bool observeChild=alphaRtt&&h.caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION;
  for(int invalid=0;invalid<3;++invalid){SoAlphaTest*badAlpha=nullptr;SoTextureUnit*badUnit=nullptr;SoTexture2*badImage=nullptr;
    if(invalid==0)s.coords[7]->point.set1Value(0,SbVec2f(std::numeric_limits<float>::quiet_NaN(),0));
    if(invalid==1){badAlpha=new SoAlphaTest;badAlpha->function=SoAlphaTest::GREATER;badAlpha->value=std::numeric_limits<float>::quiet_NaN();s.receiver->insertChild(badAlpha,0);}
    if(invalid==2){badUnit=new SoTextureUnit;badUnit->unit=8;badImage=new SoTexture2;const uint8_t white[]={255,255,255};badImage->image.setValue(SbVec2s(1,1),3,white);
      s.receiver->insertChild(badUnit,s.receiver->getNumChildren()-1);s.receiver->insertChild(badImage,s.receiver->getNumChildren()-1);}
    if(observeChild)coinRenderTestSetEnvironment(fault,"1");
    const auto serial=h.off->getLastSubmissionSerial();const auto producers=h.offProxy->producers;h.offAction.apply(s.root);
    h.off->readbackRGBA(b.rgba);h.off->readbackDepth(b.depth);b.serial=h.off->getLastSubmissionSerial();
    ok &= check(h.offAction.getLastStatus()!=CoinRenderAction::SUCCESS&&b.serial==serial&&h.offProxy->producers==producers&&a.rgba==b.rgba&&a.depth==b.depth,
      "invalid transparent scene refuses atomically before DIRECT producer and root publication");
    ok &= h.refuse(s,"transparent invalid input "+std::to_string(invalid),true);
    if(observeChild){ok &= check(std::getenv(fault)!=nullptr,"invalid consumer refuses before the first real-map RTT producer consumes its fault");coinRenderTestSetEnvironment(fault,nullptr);}
    s.coords[7]->point.set1Value(0,old);if(badAlpha)s.receiver->removeChild(badAlpha);if(badImage)s.receiver->removeChild(badImage);if(badUnit)s.receiver->removeChild(badUnit);
    const bool restored=alphaFrame(h,s,b,"transparent invalid recovery",true);ok &= restored;
    if(restored)ok &= exact(a,b)&&check(h.lastShown==physical,"transparent invalid recovery restores physical frame");
  }
  if(!alphaRtt){for(int extent:{160,96,128}){ok &= h.resize(extent);ok &= alphaFrame(h,s,b,"transparent resize/single-apply",true);}
    ok &= exact(a,b)&&check(h.lastShown==physical,"transparent resize restores A exactly");}
  ok &= alphaFrame(h,s,b,"transparent GPU-only after explicit request");
  if(observeChild&&hadFault)coinRenderTestSetEnvironment(fault,savedFault.c_str());return ok;
}
bool alphaOrderOcclusion(WindowPair&h){
  AlphaScene s,front;const auto mechanism=h.off->getOptions().transparency;
  const int mode=mechanism==COIN_RENDER_TRANSPARENCY_OBJECT?SoGLRenderAction::SORTED_OBJECT_BLEND:SoGLRenderAction::SORTED_LAYERS_BLEND;
  s.mode->value=front.mode->value=mode;
  s.receiverMaterial->transparency=front.receiverMaterial->transparency=.25f;
  // Remove the duplicate RTT producer from the second layer: this case tests
  // two differently colored sampled receivers sharing the original map group.
  if(alphaRtt){auto*node=front.stages[0]->getChild(2);front.stages[0]->removeChild(node);}
  const uint8_t frontPixel[]={128,64,32,128};front.images[7]->image.setValue(SbVec2s(1,1),4,frontPixel);
  auto z=[](AlphaScene&scene,float value){for(int i=0;i<9;++i){auto p=scene.positions->point[i];p[2]=value;scene.positions->point.set1Value(i,p);}};
  // Power-of-two depth span makes the flat depth witnesses exactly representable.
  auto*camera=static_cast<SoOrthographicCamera*>(s.root->getChild(0));camera->position.setValue(0,0,4);camera->nearDistance=1;camera->farDistance=17;
  z(s,-6);z(front,-1);s.group->addChild(front.receiver);
  auto*cover=new SoSwitch;cover->whichChild=SO_SWITCH_NONE;auto*opaque=new SoSeparator;cover->addChild(opaque);
  auto*style=new SoShadowStyle;style->style=SoShadowStyle::NO_SHADOWING;opaque->addChild(style);
  auto*light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;opaque->addChild(light);
  auto*type=new SoTransparencyType;type->value=SoGLRenderAction::NONE;opaque->addChild(type);
  auto*depth=new SoDepthBuffer;depth->write=TRUE;depth->function=SoDepthBuffer::LEQUAL;opaque->addChild(depth);
  auto*material=new SoMaterial;material->diffuseColor.setValue(.25f,.5f,.125f);opaque->addChild(material);
  auto*points=new SoCoordinate3;const SbVec3f corners[]={SbVec3f(-1.75f,-1.75f,.25f),SbVec3f(-.25f,-1.75f,.25f),SbVec3f(-.25f,1.75f,.25f),SbVec3f(-1.75f,1.75f,.25f)};
  points->point.setValues(0,4,corners);opaque->addChild(points);auto*face=new SoFaceSet;face->numVertices.set1Value(0,4);opaque->addChild(face);s.group->addChild(cover);
  const double alpha=.75*128./255.;
  const auto math=[&](const Frame&frame,bool swapped,bool occluded){
    unsigned bad=0;const double back[]={32.,48.,64.},near[]={64.,32.,16.};
    for(int y=side/4;y<3*side/4;++y)for(int x=side/4;x<3*side/4;++x){if(x>side*3/8&&x<side*5/8)continue;const size_t p=size_t(y*side+x);
      const bool covered=occluded&&x<=side*3/8;
      for(int c=0;c<3;++c){double expected;
        if(covered){const double rgb[]={64.,128.,32.};expected=rgb[c];}
        else if(mechanism==COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT){
          const auto weight=[&](double d){return std::min(8.,alpha*8.+.01)*std::min(16.,std::pow(1.-d,3.)*16.+.1);};
          const double w0=weight((swapped?4.:9.)/16.),w1=weight((swapped?9.:4.)/16.),reveal=(1-alpha)*(1-alpha);
          expected=(back[c]*w0+near[c]*w1)/(w0+w1)*(1-reveal)+alphaBackground[c]*reveal;
        }else{const double far=swapped?near[c]:back[c],close=swapped?back[c]:near[c];
          expected=close*alpha+(far*alpha+alphaBackground[c]*(1-alpha))*(1-alpha);}
        bad+=std::abs(int(frame.rgba[4*p+c])-int(std::lround(expected)))>3;
        bad+=std::abs(int(h.lastShown[4*p+c])-int(std::lround(expected)))>3;
      }
      bad+=std::abs(int(frame.rgba[4*p+3])-255)>1;bad+=frame.depth[p]!=(covered?.171875f:1.f);
    }
    return check(bad==0,"two sampled layers obey analytical compositor/depth order and opaque foreground geometry");};
  Frame a,b,c;bool ok=alphaFrame(h,s,a,"two-layer sorted/compositor A",true);if(!ok)return false;ok &= math(a,false,false);
  // Reversing authoring does not alter sorted/independent composition.
  front.receiver->ref();s.group->removeChild(front.receiver);s.group->insertChild(front.receiver,s.slots);front.receiver->unref();
  const bool permuted=alphaFrame(h,s,b,"authored order permuted",true);ok &= permuted;if(permuted)ok &= exact(a,b)&&math(b,false,false);
  z(s,-1);z(front,-6);const bool reordered=alphaFrame(h,s,b,"depth order swapped",true);ok &= reordered;
  if(reordered)ok &= math(b,true,false)&&check(alphaChanges(a,b)>0,"different layer depth changes noncommutative source-over or depth-weighted contribution");
  z(s,-6);z(front,-1);const bool restored=alphaFrame(h,s,c,"depth order A restored",true);ok &= restored;if(restored)ok &= exact(a,c);
  cover->whichChild=SO_SWITCH_ALL;const bool occluded=alphaFrame(h,s,b,"partial opaque foreground",true);ok &= occluded;if(occluded)ok &= math(b,false,true);
  cover->whichChild=SO_SWITCH_NONE;const bool uncovered=alphaFrame(h,s,c,"opaque occluder disabled A",true);ok &= uncovered;if(uncovered)ok &= exact(a,c);return ok;
}
bool alphaModelsAndState(WindowPair&h){
  AlphaScene s;s.mode->value=h.off->getOptions().transparency==COIN_RENDER_TRANSPARENCY_OBJECT?SoGLRenderAction::BLEND:SoGLRenderAction::SORTED_LAYERS_BLEND;
  Frame a,b,c;bool ok=true;
  if(!alphaRtt){ok &= alphaFrame(h,s,a,"texture cascade white/alpha control",true);}
  if(!alphaRtt)for(int kind=0;kind<6;++kind){s.model(kind);const bool rendered=alphaFrame(h,s,b,"transparent texture cascade model="+std::to_string(kind),true);ok &= rendered;if(!rendered)continue;
    double rgb[]={.5,.5,.5};for(int u:s.units)for(int channel=0;channel<3;++channel){
      const double tex=(kind==4&&u==s.units.back()?std::array<uint8_t,3>{{31,47,61}}:s.color(u))[channel]/255.;
      if(kind==0||kind==4){if(kind==4&&u==s.units.back())rgb[channel]=std::min(1.,rgb[channel]+tex);else rgb[channel]*=tex;}
      else if(kind==1||kind==2)rgb[channel]=tex;
      else if(kind==5){const double alpha=(64+16*u)/255.;rgb[channel]=rgb[channel]*(1-alpha)+tex*alpha;}
      else{const double blend=channel==0?.1+.07*u:channel==1?.8-.05*u:.25+.04*u;rgb[channel]=rgb[channel]*(1-tex)+blend*tex;}}
    // RGB REPLACE preserves previous alpha; all six authored cascades keep material alpha.
    const double alpha=.5;unsigned bad=0;
    for(int y=side/4;y<3*side/4;++y)for(int x=side/4;x<3*side/4;++x){const size_t p=size_t(y*side+x);
      for(int channel=0;channel<3;++channel){const int expected=int(std::lround(rgb[channel]*255.*alpha+alphaBackground[channel]*(1-alpha)));
        const bool attachmentWrong=std::abs(int(b.rgba[4*p+channel])-expected)>3;
        const bool physicalWrong=std::abs(int(h.lastShown[4*p+channel])-expected)>3;
        if((attachmentWrong||physicalWrong)&&bad==0){std::cout<<"alpha_model first kind="<<kind<<" authored_model="<<s.images[7]->model.getValue()
          <<" x="<<x<<" y="<<y<<" channel="<<channel<<" source_rgb="<<rgb[channel]*255.<<" source_alpha="<<alpha
          <<" expected="<<expected<<" actual="<<int(b.rgba[4*p+channel])<<" physical="<<int(h.lastShown[4*p+channel])
          <<" attachment_alpha="<<int(b.rgba[4*p+3])<<" depth="<<b.depth[p]<<'\n';
          for(int diagnosticChannel=0;diagnosticChannel<3;++diagnosticChannel)std::cout<<"alpha_model RGB channel="<<diagnosticChannel
            <<" actual="<<int(b.rgba[4*p+diagnosticChannel])<<" physical="<<int(h.lastShown[4*p+diagnosticChannel])
            <<" expected="<<int(std::lround(rgb[diagnosticChannel]*255.*alpha+alphaBackground[diagnosticChannel]*(1-alpha)))<<'\n';}
        bad+=attachmentWrong;bad+=physicalWrong;}
      bad+=b.depth[p]!=1.f;bad+=std::abs(int(b.rgba[4*p+3])-255)>1;}
    ok &= check(bad==0,"texture model/cascade preserves its independent RGB and alpha equation under composition");
    if(kind==1){
      // Contrast the three-component REPLACE case with authored RGBA. Added
      // canonical opaque storage alpha is not a substitute for source format.
      const auto color=s.color(7);const uint8_t rgba[]={color[0],color[1],color[2],64};
      s.images[7]->image.setValue(SbVec2s(1,1),4,rgba);
      const bool renderedRgba=alphaFrame(h,s,b,"REPLACE RGBA replaces previous material alpha",true);ok &= renderedRgba;
      if(renderedRgba){const double rgbaAlpha=64./255.;unsigned wrong=0;
        for(int y=side/4;y<3*side/4;++y)for(int x=side/4;x<3*side/4;++x){const size_t p=size_t(y*side+x);
          for(int channel=0;channel<3;++channel){const int expected=int(std::lround(color[channel]*rgbaAlpha+alphaBackground[channel]*(1-rgbaAlpha)));
            wrong+=std::abs(int(b.rgba[4*p+channel])-expected)>3;wrong+=std::abs(int(h.lastShown[4*p+channel])-expected)>3;}
          wrong+=b.depth[p]!=1.f;wrong+=std::abs(int(b.rgba[4*p+3])-255)>1;}
        ok &= check(wrong==0,"RGBA REPLACE substitutes authored texture alpha while RGB REPLACE preserves material alpha");}
    }
    s.white();s.sourceAlpha(128);const bool restored=alphaFrame(h,s,c,"texture model/cascade A restored",true);ok &= restored;if(restored)ok &= exact(a,c);
  }
  s.white();s.sourceAlpha(128);const bool first=alphaFrame(h,s,a,"clip/fog/AlphaTest A",true);ok &= first;if(!first)return false;
  auto*test=new SoAlphaTest;test->function=SoAlphaTest::GREATER;test->value=.5f;s.receiver->insertChild(test,0);
  const bool discarded=alphaFrame(h,s,b,"final texture/material alpha rejected by AlphaTest",true);ok &= discarded;
  if(discarded){unsigned bad=0;for(int y=side/4;y<3*side/4;++y)for(int x=side/4;x<3*side/4;++x){const size_t p=size_t(y*side+x);
    for(int channel=0;channel<3;++channel)bad+=std::abs(int(b.rgba[4*p+channel])-int(alphaBackground[channel]))>1;bad+=b.depth[p]!=1.f;}
    ok &= check(bad==0,"AlphaTest sees final cascaded alpha and discards color/depth before composition");}
  s.receiver->removeChild(test);auto*clip=new SoClipPlane;clip->plane=SbPlane(SbVec3f(1,0,0),0);s.receiver->insertChild(clip,0);
  const bool clipped=alphaFrame(h,s,b,"transparent geometric half clip",true);ok &= clipped;
  if(clipped){unsigned bad=0;for(int y=side/4;y<3*side/4;++y)for(int x=side/4;x<3*side/4;++x){if(x==side/2)continue;const size_t p=size_t(y*side+x);
    for(int channel=0;channel<3;++channel){const int expected=x<side/2?int(alphaBackground[channel]):int(a.rgba[4*p+channel]);bad+=std::abs(int(b.rgba[4*p+channel])-expected)>3;}
    bad+=b.depth[p]!=1.f;}ok &= check(bad==0,"clip removes only the rejected geometric half before alpha composition");}
  s.receiver->removeChild(clip);auto*fog=new SoEnvironment;fog->fogType=SoEnvironment::HAZE;fog->fogVisibility=14;fog->fogColor.setValue(0,0,1);s.receiver->insertChild(fog,0);
  const bool fogged=alphaFrame(h,s,b,"transparent HAZE before alpha composition",true);ok &= fogged;
  if(fogged){unsigned bad=0;const double alpha=.5*128./255.;const double source[]={32.,48.,64.};
    for(int y=side/4;y<3*side/4;++y)for(int x=side/4;x<3*side/4;++x){const size_t p=size_t(y*side+x);
      for(int channel=0;channel<3;++channel){const double foggedSource=.5*source[channel]+(channel==2?127.5:0.);
        const int expected=int(std::lround(foggedSource*alpha+alphaBackground[channel]*(1-alpha)));bad+=std::abs(int(b.rgba[4*p+channel])-expected)>3;}
      bad+=b.depth[p]!=1.f;}ok &= check(bad==0,"linear fog at eye distance7/visibility14 precedes source-over alpha");}
  s.receiver->removeChild(fog);const bool restored=alphaFrame(h,s,c,"clip/fog/AlphaTest A restored",true);ok &= restored;if(restored)ok &= exact(a,c);return ok;
}
bool alphaMixedGroups(WindowPair&h){
  if(alphaRtt)return true; // The graph controls qualify one consumer group.
  AlphaScene s(1,4,false,1.f),right(1,4,true,.2f);
  const int mode=h.off->getOptions().transparency==COIN_RENDER_TRANSPARENCY_OBJECT?SoGLRenderAction::SORTED_OBJECT_BLEND:SoGLRenderAction::SORTED_LAYERS_BLEND;
  s.group->precision=.0625f;right.group->precision=.125f;
  for(AlphaScene*scene:{&s,&right}){scene->mode->value=mode;scene->receiverStyle->style=SoShadowStyle::SHADOWED;
    scene->lighting->model=SoLightModel::PHONG;scene->receiverMaterial->diffuseColor.setValue(1,1,1);scene->receiverMaterial->transparency=.125f;
    const uint8_t white[]={255,255,255,192};scene->images[7]->image.setValue(SbVec2s(1,1),4,white);
    for(int i=0;i<9;++i){auto p=scene->positions->point[i];p[0]=.5f*p[0]+(scene==&s?-.9f:.9f);scene->positions->point.set1Value(i,p);}}
  s.casterX(0,-.8f);right.casterX(0,.8f);
  auto*leftSwitch=new SoSwitch;leftSwitch->whichChild=SO_SWITCH_ALL;leftSwitch->addChild(s.group);s.root->removeChild(s.group);s.root->addChild(leftSwitch);
  auto*rightSwitch=new SoSwitch;rightSwitch->whichChild=SO_SWITCH_NONE;rightSwitch->addChild(right.group);s.root->addChild(rightSwitch);
  Frame leftOnly,rightOnly,both,changed,restored;
  bool ok=alphaFrame(h,s,leftOnly,"isolated small-precision group",true);alphaMapDiagnostic(h.offProxy->plan,"isolated left",h.extent);
  leftSwitch->whichChild=SO_SWITCH_NONE;rightSwitch->whichChild=SO_SWITCH_ALL;
  ok &= alphaFrame(h,s,rightOnly,"isolated large-precision group",true);alphaMapDiagnostic(h.offProxy->plan,"isolated right",h.extent);
  leftSwitch->whichChild=SO_SWITCH_ALL;const bool rendered=alphaFrame(h,s,both,"coexisting groups with different precision/quality",true);ok &= rendered;
  if(rendered){unsigned bad=0,badRgb=0,badAlpha=0,badDepth=0,checked=0;int maxRgb=0,maxAlpha=0;double maxDepth=0;
    float minReference=std::numeric_limits<float>::infinity(),maxReference=-minReference,minActual=minReference,maxActual=-minReference;
    alphaMapDiagnostic(h.offProxy->plan,"coexisting groups",h.extent);
    for(int y=side/4;y<3*side/4;++y)for(int x=side/8;x<7*side/8;++x){if(std::abs(x-side/2)<4)continue;const auto&reference=x<side/2?leftOnly:rightOnly;const size_t p=size_t(y*side+x);
      if(reference.rgba.size()!=both.rgba.size()||reference.depth.size()!=both.depth.size()){++bad;continue;}++checked;
      for(int c=0;c<4;++c){const int delta=std::abs(int(both.rgba[4*p+c])-int(reference.rgba[4*p+c]));
        if(c<3){maxRgb=std::max(maxRgb,delta);badRgb+=delta>3;}else{maxAlpha=std::max(maxAlpha,delta);badAlpha+=delta>3;}
        if(delta>3&&bad==0)std::cout<<"alpha_mixed first_color x="<<x<<" y="<<y<<" channel="<<c<<" reference_group="<<(x<side/2?"left":"right")
          <<" expected="<<int(reference.rgba[4*p+c])<<" actual="<<int(both.rgba[4*p+c])<<" expected_RGBA="<<int(reference.rgba[4*p])<<','<<int(reference.rgba[4*p+1])<<','<<int(reference.rgba[4*p+2])<<','<<int(reference.rgba[4*p+3])
          <<" actual_RGBA="<<int(both.rgba[4*p])<<','<<int(both.rgba[4*p+1])<<','<<int(both.rgba[4*p+2])<<','<<int(both.rgba[4*p+3])<<'\n';bad+=delta>3;}
      minReference=std::min(minReference,reference.depth[p]);maxReference=std::max(maxReference,reference.depth[p]);
      minActual=std::min(minActual,both.depth[p]);maxActual=std::max(maxActual,both.depth[p]);
      const bool wrongDepth=both.depth[p]!=reference.depth[p];maxDepth=std::max(maxDepth,std::abs(double(both.depth[p])-reference.depth[p]));
      if(wrongDepth&&badDepth==0)std::cout<<"alpha_mixed first_depth x="<<x<<" y="<<y<<" expected="<<std::hexfloat<<reference.depth[p]<<" actual="<<both.depth[p]<<std::defaultfloat<<'\n';
      badDepth+=wrongDepth;bad+=wrongDepth;}
    std::cout<<"alpha_mixed extent="<<h.extent<<" valid_y="<<side/4<<':'<<3*side/4<<" valid_x="<<side/8<<':'<<7*side/8<<" exclude_abs_xminus_center_lt4=1 checked="<<checked
      <<" bad_RGB="<<badRgb<<" max_RGB_delta="<<maxRgb<<" bad_alpha="<<badAlpha<<" max_alpha_delta="<<maxAlpha<<" bad_depth="<<badDepth<<" max_depth_delta="<<maxDepth<<" reference_depth_minmax="<<std::hexfloat<<minReference<<','<<maxReference
      <<" actual_depth_minmax="<<minActual<<','<<maxActual<<std::defaultfloat<<'\n';
    ok &= check(bad==0,"each coexisting group's color/depth matches its own local isolated precision control");
    const auto&plan=h.offProxy->plan;bool small=false,large=false;for(const auto&group:plan.shadowGroups){small|=group.precision==.0625f&&group.quality==1.f;large|=group.precision==.125f&&group.quality==.2f;}
    ok &= check(small&&large&&plan.shadowLights.size()==2&&plan.shadowLights[0].customSceneNodeId!=plan.shadowLights[1].customSceneNodeId,
      "distinct authored group precision/quality and source-map identity survive capture");
  }
  for(AlphaScene*scene:{&s,&right}){const auto original=scene->casterPositions[0]->translation.getValue();scene->casterX(0,scene==&s?.8f:-.8f);
    const bool moved=alphaFrame(h,s,changed,"mixed-group independent caster B",true);ok &= moved;if(moved)ok &= check(alphaChanges(both,changed)>0,"coexisting map updates independently");
    scene->casterPositions[0]->translation=original;const bool back=alphaFrame(h,s,restored,"mixed-group caster A restored",true);ok &= back;if(back)ok &= exact(both,restored);}
  return ok;
}
bool alphaMapMemoryBudget(WindowPair&h){
  AlphaScene s;s.mode->value=h.off->getOptions().transparency==COIN_RENDER_TRANSPARENCY_OBJECT?SoGLRenderAction::SORTED_OBJECT_BLEND:SoGLRenderAction::SORTED_LAYERS_BLEND;
  s.lighting->model=SoLightModel::PHONG;s.receiverStyle->style=SoShadowStyle::SHADOWED;
  Frame a,b;bool ok=alphaFrame(h,s,a,"shadow collection memory anchor A",true);if(!ok)return false;
  const auto physical=h.lastShown;const float precision=s.group->precision.getValue();s.group->precision=.5f;
  // The old logical map budget admits eight 1024-square RGBA32F sources
  // exactly (128MiB). An additional physical sampling collection cannot be
  // free: even excluding depth attachments it exceeds the same 128MiB cap.
  CoinRenderAction capture(SbViewportRegion(side,side));capture.setSortedLayersNumPasses(4);
  capture.getPimpl()->planOnly=true;capture.getPimpl()->executionOptions=h.off->getOptions();
  auto graph=std::make_shared<CoinRenderRttPlan>(h.off->getOptions().sceneTexture);
  capture.getPimpl()->sceneTexturePlan=graph;capture.apply(s.root);
  CoinRenderShadowPlan maps;std::string diagnostic;
  const bool captured=capture.getLastStatus()==CoinRenderAction::SUCCESS&&graph->producers.size()==size_t(alphaRtt?1:0)&&
    coin_render_plan_shadows(capture.getPimpl()->lastValidPlan,maps,diagnostic);
  uint64_t sourceBytes=0;unsigned visible=0;
  if(captured)for(const auto&map:maps.passes)if(map.visible){++visible;sourceBytes+=uint64_t(map.mapSize)*map.mapSize*16u;ok &= check(map.mapSize==1024,"authored half precision resolves the budget witness at1024");}
  ok &= check(captured&&visible==8&&sourceBytes==UINT64_C(128)*1024*1024&&maps.attachmentBytes==sourceBytes,
    "budget witness retains eight real maps and fits the old source-only logical cap: "+diagnostic);
  if(!ok){s.group->precision=precision;return false;}
  const auto memoryRefusal=[](CoinRenderAction&action){std::string error=action.getLastError().getString();
    std::transform(error.begin(),error.end(),error.begin(),[](unsigned char c){return char(std::tolower(c));});
    return action.getLastStatus()==CoinRenderAction::UNSUPPORTED&&error.find("shadow")!=std::string::npos&&
      (error.find("budget")!=std::string::npos||error.find("memory")!=std::string::npos||error.find("128")!=std::string::npos);};
  const char*fault="COIN_BGFX_TEST_SHADOW_MAP_ALLOC_ONCE";const char*old=std::getenv(fault);const bool had=old!=nullptr;const std::string previous=old?old:"";
  const bool observeFault=h.caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION;
  if(observeFault)coinRenderTestSetEnvironment(fault,"1");
  const auto serial=h.off->getLastSubmissionSerial();const unsigned rootCalls=h.offProxy->submits,producers=h.offProxy->producers;
  h.offAction.apply(s.root);h.off->readbackRGBA(b.rgba);h.off->readbackDepth(b.depth);b.serial=h.off->getLastSubmissionSerial();
  ok &= check(memoryRefusal(h.offAction)&&b.serial==serial&&a.rgba==b.rgba&&a.depth==b.depth&&
    h.offProxy->producers==producers&&(h.offProxy->submits==rootCalls||h.offProxy->submits==rootCalls+1)&&
    (!observeFault||std::getenv(fault)!=nullptr),
    "physical shadow collection budget refuses before map/producers, preserving attachment RGBA/depth/serial/fault");
  std::cout<<"shadow_multitexture_alpha collection_budget source_bytes="<<sourceBytes<<" offscreen_status="<<int(h.offAction.getLastStatus())
    <<" offscreen_error="<<h.offAction.getLastError().getString()<<" cpu_submit_entries="<<h.offProxy->submits-rootCalls<<'\n';
  ok &= h.refuse(s,"physical shadow collection exceeds128MiB",true,true);
  ok &= check(memoryRefusal(h.windowAction)&&(!observeFault||std::getenv(fault)!=nullptr),
    "window rejects specifically the collection memory cap and preserves the map/producer allocation fault");
  std::cout<<"shadow_multitexture_alpha collection_budget window_status="<<int(h.windowAction.getLastStatus())
    <<" window_error="<<h.windowAction.getLastError().getString()<<'\n';
  if(observeFault)coinRenderTestSetEnvironment(fault,nullptr);
  s.group->precision=precision;const bool restored=alphaFrame(h,s,b,"shadow collection budget recovery A",true);ok &= restored;
  if(restored)ok &= exact(a,b)&&check(h.lastShown==physical,"collection budget recovery restores physical A");
  if(observeFault&&had)coinRenderTestSetEnvironment(fault,previous.c_str());return ok;
}
bool alphaCampaign(WindowPair&h){bool ok=alphaDomain(h);ok &= alphaMixedGroups(h);ok &= alphaOrderOcclusion(h);ok &= alphaModelsAndState(h);ok &= alphaOpaqueAndScreen(h);ok &= alphaCoverage(h);ok &= alphaAtomic(h);ok &= alphaMapMemoryBudget(h);return ok;}
}
#endif
