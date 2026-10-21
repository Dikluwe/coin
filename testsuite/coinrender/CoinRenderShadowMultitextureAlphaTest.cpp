#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#define COIN_RENDER_SHADOW_MULTITEXTURE_WINDOW_FIXTURE_ONLY
#include "CoinRenderShadowMultitextureWindowTest.cpp"
#include <Inventor/nodes/SoTransparencyType.h>
#include "CoinRenderTestEnvironment.h"

namespace {
bool alphaRtt=false;
const std::array<double,3> alphaBackground={{16.,24.,32.}};
struct AlphaScene:Scene {
  SoTransparencyType*mode=new SoTransparencyType;
  SoDepthBuffer*depth=new SoDepthBuffer;
  SoLightModel*lighting=new SoLightModel;SoMaterial*rttMaterial=nullptr;SoSceneTexture2*rttTexture=nullptr;
  std::unique_ptr<msaa_shadow_test::Scene>rttShadow;
  explicit AlphaScene(int maps=8,int textures=8,bool dir=false,float quality=1.f):Scene(maps,textures,quality,dir){
    receiver->insertChild(mode,0);receiver->insertChild(depth,1);receiver->insertChild(lighting,2);
    lighting->model=SoLightModel::BASE_COLOR;receiverStyle->style=SoShadowStyle::NO_SHADOWING;
    materialBinding->value=SoMaterialBinding::OVERALL;normalBinding->value=SoNormalBinding::OVERALL;
    normalData->vector.setValue(0,0,1);receiverMaterial->diffuseColor.setValue(.5f,.5f,.5f);
    receiverMaterial->transparency=.5f;depth->write=FALSE;depth->function=SoDepthBuffer::LEQUAL;
    for(int i=0;i<9;++i){auto p=positions->point[i];p[2]=-1;positions->point.set1Value(i,p);}
    white();sourceAlpha(128);
    if(alphaRtt){rttShadow.reset(new msaa_shadow_test::Scene(1,false));
      static_cast<SoLightModel*>(rttShadow->root->getChild(1))->model=SoLightModel::BASE_COLOR;
      static_cast<SoShadowStyle*>(rttShadow->receiver->getChild(0))->style=SoShadowStyle::NO_SHADOWING;
      rttMaterial=rttShadow->receiverMaterial;rttMaterial->diffuseColor.setValue(1,1,1);
      auto*rtt=new SoSceneTexture2;rttTexture=rtt;rtt->scene=rttShadow->root;rtt->size.setValue(32,32);
      rtt->type=SoSceneTexture2::RGBA8;rtt->model=SoSceneTexture2::MODULATE;rtt->transparencyFunction=SoSceneTexture2::NONE;
      rtt->backgroundColor.setValue(1,1,1,1);stages[0]->insertChild(rtt,2);}
  }
  void sourceAlpha(uint8_t alpha){const uint8_t pixel[]={64,96,128,alpha};images[7]->image.setValue(SbVec2s(1,1),4,pixel);}
};
bool alphaDelivery(const AlphaScene&s,const CoinRenderFramePlan&frame){
  bool ok=check(frame.shadowLights.size()==size_t(s.slots),"alpha path retains all real shadow maps");
  CoinWgpuFfiFrame ffi;std::string why;ok &= check(ffi.prepare(frame,side,side,why,false),"alpha Core/FFI prepare: "+why);
  unsigned receivers=0;
  for(const auto&draw:frame.draws){if(draw.shadowLightSlot||draw.sourceNodeId!=s.mesh->getNodeId())continue;
    ++receivers;if(draw.renderStateSlot>=frame.renderStates.size())return check(false,"receiver render state exists");
    const auto&state=frame.renderStates[draw.renderStateSlot];
    ok &= check(state.depthTest&&!state.depthWrite,"transparent receiver preserves explicit depth test/write");
    for(int unit:s.units){const auto tex=coin_render_texture_unit(state,size_t(unit));
      ok &= check(tex.enabled&&tex.imageSlot<frame.textures.size()&&tex.samplerSlot<frame.samplers.size(),
        "alpha receiver retains each authored texture/sampler, including unit7");
      if(tex.enabled&&tex.imageSlot<frame.textures.size()&&unit==7){const auto&image=frame.textures[tex.imageSlot];
        ok &= check(image.pixelsRgba.size()==4&&image.pixelsRgba[0]==64&&image.pixelsRgba[1]==96&&image.pixelsRgba[2]==128&&image.pixelsRgba[3]==128,
          "unit7 RGBA payload retains its independent authored alpha");}
    }
  }
  for(const auto&light:frame.shadowLights)ok &= check(light.enabled&&light.shadowEligible&&light.mapSceneCaptured,
    "alpha combination captures every real map instead of silently disabling shadows");
  return check(receivers>0,"captured alpha receiver exists")&&ok;
}
// These pixels lie strictly inside the flat constant receiver, away from
// geometry edges. Attachment quantization uses the existing three-byte gate;
// no sample pattern or raster output is compared between machines.
bool alphaMath(const Frame&f,const std::vector<uint8_t>&physical,const std::vector<uint8_t>&readback,
               int extent,bool additive,double materialAlpha=.5){
  if(!check(f.rgba.size()==size_t(extent)*extent*4&&f.depth.size()==size_t(extent)*extent&&physical.size()==f.rgba.size(),
    "alpha arithmetic sees complete attachment and physical frame"))return false;
  const double alpha=materialAlpha*128./255.;const double source[]={32.,48.,64.};unsigned bad=0,count=0;
  for(int y=extent/4;y<3*extent/4;++y)for(int x=extent/4;x<3*extent/4;++x){const size_t p=size_t(y*extent+x);++count;
    for(int c=0;c<3;++c){const int expected=int(std::lround(source[c]*alpha+alphaBackground[c]*(additive?1.:1.-alpha)));
      bad+=std::abs(int(f.rgba[4*p+c])-expected)>3;bad+=std::abs(int(physical[4*p+c])-expected)>3;
      if(!readback.empty())bad+=std::abs(int(readback[4*p+c])-expected)>3;
    }
    bad+=std::abs(int(f.rgba[4*p+3])-255)>1;bad+=f.depth[p]!=1.f;
  }
  std::cout<<"shadow_multitexture_alpha math pixels="<<count<<" wrong_channels_or_depth="<<bad<<" additive="<<additive<<'\n';
  return check(count>0&&bad==0,"analytic material times unit7 texture alpha, blend operator and preserved clear depth");
}
bool alphaFrame(WindowPair&h,AlphaScene&s,Frame&f,const std::string&phase,bool readback=false){
  return h.frame(s,f,phase,readback,false,alphaRtt?1:0);
}
bool alphaOffscreenAlpha(WindowPair&h,AlphaScene&s,bool additive){
  h.offAction.setBackgroundColor(SbColor4f(16.f/255,24.f/255,32.f/255,0));
  const auto before=h.off->getLastSubmissionSerial();const unsigned roots=h.offProxy->submits;
  h.offAction.apply(s.root);Frame f;h.off->readbackRGBA(f.rgba);h.off->readbackDepth(f.depth);f.serial=h.off->getLastSubmissionSerial();
  const unsigned step=1+((h.caps.backend==COIN_RENDER_EXPERIMENTAL_RUST||h.off->getOptions().sceneTexture==COIN_RENDER_SCENE_TEXTURE_DIRECT)&&alphaRtt?1:0);
  const uint64_t expected=h.caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?h.latest+step:before+step;
  bool ok=check(h.offAction.getLastStatus()==CoinRenderAction::SUCCESS&&f.serial==expected&&h.offProxy->submits==roots+1&&
    f.rgba.size()==size_t(side)*side*4&&f.depth.size()==size_t(side)*side,"zero-clear-alpha offscreen publication");
  if(ok){const double alpha=.5*128./255.;const int expectedAlpha=int(std::lround(255.*(additive?alpha*alpha:alpha)));unsigned bad=0;
    for(int y=side/4;y<3*side/4;++y)for(int x=side/4;x<3*side/4;++x){const size_t p=size_t(y*side+x);
      bad+=std::abs(int(f.rgba[4*p+3])-expectedAlpha)>3;bad+=f.depth[p]!=1.f;}
    ok &= check(bad==0,additive?"zero-clear alpha ADD is As squared":"zero-clear alpha source-over is As");}
  h.publishedOff=f.serial;h.latest=f.serial;h.offAction.setBackgroundColor(SbColor4f(16.f/255,24.f/255,32.f/255,1));return ok;
}
bool alphaSmoke(WindowPair&h){
  AlphaScene s;Frame a,b,restored;bool ok=true;
  h.offAction.setBackgroundColor(SbColor4f(16.f/255,24.f/255,32.f/255,1));
  h.windowAction.setBackgroundColor(SbColor4f(16.f/255,24.f/255,32.f/255,1));
  // Sorted-layers selection is explicit; the target option selects which
  // compositor implements it. One layer has the same source-over equation.
  const bool oit=h.off->getOptions().transparency!=COIN_RENDER_TRANSPARENCY_OBJECT;
  const std::vector<int> modes=oit?std::vector<int>{SoGLRenderAction::SORTED_LAYERS_BLEND}:
    std::vector<int>{SoGLRenderAction::BLEND,SoGLRenderAction::ADD,SoGLRenderAction::DELAYED_BLEND,
      SoGLRenderAction::DELAYED_ADD,SoGLRenderAction::SORTED_OBJECT_BLEND,SoGLRenderAction::SORTED_OBJECT_ADD,
      SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND,SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD};
  for(int mode:modes){s.mode->value=mode;const bool additive=mode==SoGLRenderAction::ADD||mode==SoGLRenderAction::DELAYED_ADD||
      mode==SoGLRenderAction::SORTED_OBJECT_ADD||mode==SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD;
    const bool first=alphaFrame(h,s,a,"alpha analytic mode="+std::to_string(mode),false);
    ok &= first;if(!first)continue;
    ok &= alphaDelivery(s,h.offProxy->plan)&&alphaDelivery(s,h.windowProxy->plan);
    ok &= alphaMath(a,h.lastShown,h.lastReadback,h.extent,additive);
    // Distinguish material and unit7 alpha from a hard-coded opacity; returning
    // to A must restore the full frame in this same runtime and connection.
    s.receiverMaterial->transparency=0.f;const bool changed=alphaFrame(h,s,b,"independent material alpha B",true);ok &= changed;
    if(changed)ok &= alphaMath(b,h.lastShown,h.lastReadback,h.extent,additive,1.)&&
      check(difference(a,b)>100,"material alpha independently changes many covered RGB pixels");
    s.receiverMaterial->transparency=.5f;const bool recovered=alphaFrame(h,s,restored,"alpha A restored",true);ok &= recovered;
    if(recovered)ok &= exact(a,restored)&&alphaMath(restored,h.lastShown,h.lastReadback,h.extent,additive);
    ok &= alphaOffscreenAlpha(h,s,additive);
  }
  return ok;
}
}
// Campaign keeps the same runtime, native connection and pair throughout.
#include "CoinRenderShadowMultitextureAlphaCampaign.h"
#include "CoinRenderShadowMultitextureAlphaViewPressure.h"
#ifndef COIN_RENDER_SHADOW_ALPHA_FIXTURE_ONLY
int main(int argc,char**argv){
  configureSamplingTest(argc,argv);CoinRenderTransparencyMode mechanism=COIN_RENDER_TRANSPARENCY_OBJECT;bool smoke=false,campaignMode=false,stipple=false,viewPressure=false,mapGroups=false,mapDomain=false,mapsSelected=false;int selectedMaps=1;CoinRenderSceneTextureMode route=COIN_RENDER_SCENE_TEXTURE_STAGED;
  for(int i=1;i<argc;++i){const std::string arg=argv[i];if(arg=="--portable-sampling")continue;
    if(arg=="--map-domain"){mapDomain=true;continue;}
    if(arg=="--maps"){if(++i==argc)return 2;const std::string count=argv[i];
      if(count!="1"&&count!="2"&&count!="4"&&count!="6"&&count!="8")return 2;selectedMaps=std::stoi(count);mapsSelected=true;continue;}
    if(arg=="--map-groups"){mapGroups=true;continue;}
    if(arg=="--view-pressure"){viewPressure=true;continue;}
    if(arg=="--stipple"){stipple=true;continue;}
    if(arg=="--campaign"){campaignMode=true;continue;}
    if(arg=="--rtt-staged"||arg=="--rtt-direct"){alphaRtt=true;route=arg=="--rtt-direct"?COIN_RENDER_SCENE_TEXTURE_DIRECT:COIN_RENDER_SCENE_TEXTURE_STAGED;continue;}
    if(arg=="--smoke"){smoke=true;continue;}if(arg=="--samples"){if(++i==argc)return 2;const std::string n=argv[i];
      if(n!="1"&&n!="2"&&n!="4")return 2;windowSamples=unsigned(std::stoi(n));continue;}
    if(arg=="--mechanism"){if(++i==argc)return 2;const std::string n=argv[i];
      if(n=="object")mechanism=COIN_RENDER_TRANSPARENCY_OBJECT;else if(n=="peeling")mechanism=COIN_RENDER_TRANSPARENCY_PEELING;
      else if(n=="weighted")mechanism=COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;else return 2;continue;}return 2;
  }
  if(!smoke&&!campaignMode&&!stipple&&!viewPressure&&!mapGroups&&!mapDomain)return 2;
  if(mapsSelected&&!mapGroups&&!mapDomain)return 2;
  if(mapGroups&&alphaRtt)return 2;
  if(viewPressure){if(mechanism!=COIN_RENDER_TRANSPARENCY_OBJECT)return 2;alphaRtt=true;}
  if(stipple&&mechanism!=COIN_RENDER_TRANSPARENCY_OBJECT)return 2;if(!XInitThreads())return 1;SoDB::init();CoinRenderAction::initClass();
  const auto caps=msaa_shadow_test::capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW);
  if(!caps.gpu_available||caps.renderer==COIN_RENDER_RENDERER_UNKNOWN)return 77;
  if(viewPressure&&caps.backend!=COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION)return 77;
  WindowPair h(caps,0,nullptr,mechanism,route);if(!h.ready)return 1;
  h.offAction.setSortedLayersNumPasses(4);h.windowAction.setSortedLayersNumPasses(4);
  if(viewPressure)return alphaViewPressure(h)?0:1;
  if(mapGroups||mapDomain){h.offAction.setBackgroundColor(SbColor4f(16.f/255,24.f/255,32.f/255,1));
    h.windowAction.setBackgroundColor(SbColor4f(16.f/255,24.f/255,32.f/255,1));
    bool ok=alphaDomain(h,selectedMaps);if(mapGroups)ok &= alphaMixedGroups(h);return ok?0:1;}
  if(stipple){h.offAction.setBackgroundColor(SbColor4f(16.f/255,24.f/255,32.f/255,1));
    h.windowAction.setBackgroundColor(SbColor4f(16.f/255,24.f/255,32.f/255,1));return alphaOpaqueAndScreen(h)?0:1;}
  bool ok=alphaSmoke(h);if(campaignMode)ok &= alphaCampaign(h);return ok?0:1;
}

#endif
