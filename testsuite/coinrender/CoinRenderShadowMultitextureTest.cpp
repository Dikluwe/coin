#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderMsaaShadowFixture.h"
#include "CoinRenderSamplingTestOptions.h"
#include "rendering/coinrender/CoinRenderBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderRttCore.h"
#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoQuadMesh.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCombine.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <cstring>
#include <limits>
#include <set>
using msaa_shadow_test::Frame;
using msaa_shadow_test::side;
namespace {
bool check(bool yes,const std::string &label){
  if(!yes)std::cerr<<"shadow_multitexture FAIL "<<label<<'\n';return yes;
}
const int bindings[]={SoMaterialBinding::OVERALL,SoMaterialBinding::PER_PART,
  SoMaterialBinding::PER_PART_INDEXED,SoMaterialBinding::PER_FACE,
  SoMaterialBinding::PER_FACE_INDEXED,SoMaterialBinding::PER_VERTEX,
  SoMaterialBinding::PER_VERTEX_INDEXED};
struct Scene:msaa_shadow_test::Scene {
  SoTexture2 *images[8]{};SoTextureCoordinate2 *coords[8]{};
  SoTextureMatrixTransform *matrices[8]{};SoSwitch *stages[8]{};
  SoTextureCombine *combine[8]{};SoSwitch *combines[8]{};
  SoMaterialBinding *materialBinding=new SoMaterialBinding;
  SoNormalBinding *normalBinding=new SoNormalBinding;
  SoCoordinate3 *positions=new SoCoordinate3;SoNormal *normalData=new SoNormal;
  SoQuadMesh *mesh=new SoQuadMesh;SoShadowStyle *receiverStyle=new SoShadowStyle;
  std::vector<int> units;
  Scene(int maps,int textures,float quality,bool dir):msaa_shadow_test::Scene(maps,dir){
    group->quality=quality;receiverMaterial->ref();receiver->removeAllChildren();
    receiverStyle->style=SoShadowStyle::SHADOWED;receiver->addChild(receiverStyle);
    const SbColor colors[]={SbColor(.72f,.75f,.78f),SbColor(.8f,.48f,.6f),
      SbColor(.45f,.82f,.56f),SbColor(.58f,.53f,.85f),SbColor(.8f,.73f,.45f),
      SbColor(.6f,.8f,.7f),SbColor(.8f,.55f,.7f),SbColor(.5f,.65f,.85f),SbColor(.8f,.8f,.7f)};
    receiverMaterial->diffuseColor.setValues(0,9,colors);receiver->addChild(receiverMaterial);receiverMaterial->unref();
    receiver->addChild(materialBinding);
    const SbVec3f normal[]={SbVec3f(0,0,1),SbVec3f(.7f,0,1),SbVec3f(0,.7f,1),
      SbVec3f(-.7f,0,1),SbVec3f(0,-.7f,1),SbVec3f(.6f,.6f,1),
      SbVec3f(-.6f,.6f,1),SbVec3f(.6f,-.6f,1),SbVec3f(-.6f,-.6f,1)};
    normalData->vector.setValues(0,9,normal);receiver->addChild(normalData);receiver->addChild(normalBinding);
    auto *qualityNode=new SoComplexity;qualityNode->textureQuality=.1f;receiver->addChild(qualityNode);
    const SbVec3f p[]={SbVec3f(-1.75f,-1.75f,-1),SbVec3f(0,-1.75f,-.91f),SbVec3f(1.75f,-1.75f,-.98f),
      SbVec3f(-1.75f,0,-.96f),SbVec3f(0,0,-1.07f),SbVec3f(1.75f,0,-.94f),
      SbVec3f(-1.75f,1.75f,-1.02f),SbVec3f(0,1.75f,-.93f),SbVec3f(1.75f,1.75f,-1)};
    positions->point.setValues(0,9,p);receiver->addChild(positions);
    units=textures==2?std::vector<int>{0,7}:textures==4?std::vector<int>{0,2,3,7}:std::vector<int>{0,1,2,3,4,5,6,7};
    for(int unit:units){
      stages[unit]=new SoSwitch;stages[unit]->whichChild=SO_SWITCH_ALL;receiver->addChild(stages[unit]);
      auto *selected=new SoTextureUnit;selected->unit=unit;stages[unit]->addChild(selected);
      images[unit]=new SoTexture2;images[unit]->model=SoTexture2::MODULATE;
      images[unit]->wrapS=unit%2?SoTexture2::CLAMP:SoTexture2::REPEAT;
      images[unit]->wrapT=unit%2?SoTexture2::REPEAT:SoTexture2::CLAMP;
      stages[unit]->addChild(images[unit]);
      coords[unit]=new SoTextureCoordinate2;
      for(int y=0;y<3;++y)for(int x=0;x<3;++x)
        coords[unit]->point.set1Value(y*3+x,SbVec2f(.03f*unit+.7f*x/2.f,.04f*unit+.6f*y/2.f));
      stages[unit]->addChild(coords[unit]);
      matrices[unit]=new SoTextureMatrixTransform;SbMatrix matrix=SbMatrix::identity();
      matrix[0][3]=.025f*(unit+1);matrix[1][3]=-.01f*unit;
      matrices[unit]->matrix=matrix;stages[unit]->addChild(matrices[unit]);
      combines[unit]=new SoSwitch;combines[unit]->whichChild=SO_SWITCH_NONE;
      combine[unit]=new SoTextureCombine;combine[unit]->rgbOperation=SoTextureCombine::ADD;
      combine[unit]->alphaOperation=SoTextureCombine::REPLACE;
      const int rgb[]={SoTextureCombine::PREVIOUS,SoTextureCombine::TEXTURE};
      combine[unit]->rgbSource.setValues(0,2,rgb);
      combine[unit]->alphaSource.set1Value(0,SoTextureCombine::PREVIOUS);
      combines[unit]->addChild(combine[unit]);stages[unit]->addChild(combines[unit]);
    }
    mesh->verticesPerRow=3;mesh->verticesPerColumn=3;receiver->addChild(mesh);
    materialBinding->value=SoMaterialBinding::PER_FACE;normalBinding->value=SoNormalBinding::PER_VERTEX;
    white();
  }
  std::array<uint8_t,3> color(int unit)const{return {{uint8_t(224-3*unit),uint8_t(240-2*unit),uint8_t(231-unit)}};}
  void image(int unit,const std::array<uint8_t,3> &rgb){images[unit]->image.setValue(SbVec2s(1,1),3,rgb.data());}
  void white(){for(int u:units){image(u,{{255,255,255}});images[u]->model=SoTexture2::MODULATE;combines[u]->whichChild=SO_SWITCH_NONE;}}
  void model(int kind){
    for(int u:units){image(u,color(u));images[u]->model=kind==0?SoTexture2::MODULATE:
      kind==1?SoTexture2::REPLACE:kind==2?SoTexture2::DECAL:kind==3?SoTexture2::BLEND:SoTexture2::MODULATE;
      images[u]->blendColor.setValue(.1f+.07f*u,.8f-.05f*u,.25f+.04f*u);combines[u]->whichChild=SO_SWITCH_NONE;}
    if(kind==4){const int last=units.back();image(last,{{31,47,61}});combines[last]->whichChild=SO_SWITCH_ALL;}
    if(kind==5)for(int u:units){const auto rgb=color(u);
      const uint8_t rgba[]={rgb[0],rgb[1],rgb[2],uint8_t(64+16*u)};
      images[u]->image.setValue(SbVec2s(1,1),4,rgba);images[u]->model=SoTexture2::DECAL;
    }
  }
  void uv(int unit,float s,float t){for(int i=0;i<9;++i)coords[unit]->point.set1Value(i,SbVec2f(s,t));}
  void pattern(int unit){const uint8_t pixels[]={240,30,60,40,220,70,60,80,235,230,205,45};
    images[unit]->image.setValue(SbVec2s(2,2),3,pixels);images[unit]->model=SoTexture2::REPLACE;}
};
class Recorder:public CoinRenderBackend {
public:
  CoinRenderFramePlan plan;std::string error;uint64_t serial=0;
  bool isGpuBackend()const override{return true;}
  bool supportsOffscreenShadows()const override{return true;}
  CoinRenderBackendStatus getStatus()const override{return CoinRenderBackendStatus::SUCCESS;}
  CoinRenderBackendStatus prepare(CoinRenderTargetP&)override{return CoinRenderBackendStatus::SUCCESS;}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &frame,CoinRenderTargetP&)override{
    plan=frame;return {CoinRenderBackendStatus::SUCCESS,"",++serial};}
  void poll()override{}const std::string &getLastError()const override{return error;}
};
class Proxy:public CoinRenderBackend {
public:
  std::unique_ptr<CoinRenderBackend> real;CoinRenderFramePlan plan;
  mutable unsigned preflight=0;unsigned producers=0;
  mutable bool producerPlanPoliciesMatch=true;mutable size_t declaredProducers=0;
  CoinRenderTextureSamplingPolicy producerSamplingPolicy=COIN_RENDER_SAMPLING_NATIVE;
  explicit Proxy(std::unique_ptr<CoinRenderBackend> p):real(std::move(p)){}
  bool isGpuBackend()const override{return real->isGpuBackend();}
  bool initializesCpuDepthBuffer()const override{return real->initializesCpuDepthBuffer();}
  bool resetOnActionDetach()const override{return real->resetOnActionDetach();}
  bool supportsOffscreenShadows()const override{return real->supportsOffscreenShadows();}
  bool supportsOpaqueBump()const override{return real->supportsOpaqueBump();}
  CoinRenderBackendStatus getStatus()const override{return real->getStatus();}
  CoinRenderBackendStatus prepare(CoinRenderTargetP&t)override{return real->prepare(t);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&f,CoinRenderTargetP&t)override{plan=f;return real->submit(f,t);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&f,CoinRenderTargetP&t,const CoinRenderFrameReuseDecision&r)override{plan=f;return real->submit(f,t,r);}
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan&g,const CoinRenderFramePlan&f,const SbVec2i32&s)const override{
    ++preflight;declaredProducers=g.producers.size();
    for(const auto &producer:g.producers)producerPlanPoliciesMatch &= producer.plan.textureSamplingPolicy==samplingTestPolicy;
    return real->preflightRtt(g,f,s);}
  CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan&f,const SbVec2i32&s,uint64_t id,CoinRenderTargetP&t,uint64_t&token)override{
    ++producers;producerSamplingPolicy=f.textureSamplingPolicy;return real->submitRtt(f,s,id,t,token);}
  void finishRtt(const std::vector<uint64_t>&t)override{real->finishRtt(t);}
  bool stagedRttRequiresPreparedParent()const override{return real->stagedRttRequiresPreparedParent();}
  bool requiresSharedRetirement()const override{return real->requiresSharedRetirement();}
  void retireLostReadbacks()override{real->retireLostReadbacks();}
  CoinRenderDeviceDomain resourceDomain()const override{return real->resourceDomain();}
  bool readbackLoad(uint64_t&a,uint64_t&b)const override{return real->readbackLoad(a,b);}
  void poll()override{real->poll();}const std::string&getLastError()const override{return real->getLastError();}
};
struct Harness {
  std::unique_ptr<CoinRenderTarget> target;CoinRenderAction action{SbViewportRegion(side,side)};Proxy *proxy=nullptr;
  uint64_t serial=0;
  explicit Harness(const CoinRenderCapabilities&caps,CoinRenderSceneTextureMode route=COIN_RENDER_SCENE_TEXTURE_STAGED){auto o=samplingTestOptions();o.sceneTexture=route;o.renderer=static_cast<CoinRenderRenderer>(caps.renderer);o.sampleCount=1;
    target.reset(samplingTestOffscreen(SbVec2i32(side,side),o));
    if(target){
      // The backend is lazy. Prepare it through the existing Infra API before
      // wrapping it; this initializes no frame and publishes no serial.
      auto &state=target->getPimpl().get();
      const auto prepared=state.prepareBackend();
      if(!check(prepared==CoinRenderBackendStatus::SUCCESS&&state.backend&&state.backend->isGpuBackend(),
        "real GPU backend prepared before proxy installation")){
        std::cerr<<"shadow_multitexture setup error="<<target->getLastError()<<'\n';
        target.reset();return;
      }
      proxy=new Proxy(std::move(state.backend));state.backend.reset(proxy);
      target->setDepthReadbackEnabled(TRUE);action.setRenderTarget(target.get());
    }
    action.setBackgroundColor(SbColor4f(0,0,0,0));
  }
  ~Harness(){action.setRenderTarget(nullptr);}
  bool render(SoNode*root,Frame&f,const std::string&phase){
    if(!check(bool(target),"physical target exists"))return false;
    action.apply(root);target->readbackRGBA(f.rgba);target->readbackDepth(f.depth);f.serial=target->getLastSubmissionSerial();
    const bool good=target->getOptions().textureSamplingPolicy==samplingTestPolicy&&
      action.getLastStatus()==CoinRenderAction::SUCCESS&&f.rgba.size()==size_t(side*side*4)&&
      f.depth.size()==size_t(side*side)&&f.serial==(serial?serial+1:f.serial)&&f.serial>0;
    std::cout<<"shadow_multitexture phase="<<phase<<" status="<<int(action.getLastStatus())<<" serial="<<f.serial
      <<" error="<<action.getLastError().getString()<<'\n';serial=f.serial;return check(good,phase);}
};
std::string label(const Scene&s){return "maps="+std::to_string(s.slots)+" units="+std::to_string(s.units.size())+
  " quality="+std::to_string(s.group->quality.getValue())+" directional="+std::to_string(s.directional);}
bool exact(const Frame&a,const Frame&b){return check(a.rgba==b.rgba&&a.depth==b.depth,"same-environment A/B/A exact RGBA/depth");}
unsigned difference(const Frame&a,const Frame&b){return msaa_shadow_test::differentRgb(a,b,12,116);}
bool numerical(const Scene&s,const Frame&base,const Frame&actual,int kind){
  if(!check(base.rgba.size()==actual.rgba.size()&&base.depth.size()==actual.depth.size(),"numeric oracle complete buffers"))return false;
  unsigned checked=0,bad=0;int maxError=0;
  for(int y=12;y<116;++y)for(int x=12;x<116;++x){const size_t p=size_t(y*side+x);
    if(base.rgba[4*p+3]!=255)continue;
    for(int c=0;c<3;++c){double previous=base.rgba[4*p+c]/255.;
      for(int u:s.units){const double tex=(kind==4&&u==s.units.back()?std::array<uint8_t,3>{{31,47,61}}:s.color(u))[c]/255.;
        if(kind==0||kind==4){if(kind==4&&u==s.units.back())previous=std::min(1.,previous+tex);else previous*=tex;}
        else if(kind==1||kind==2)previous=tex;
        else if(kind==5){const double alpha=(64+16*u)/255.;previous=previous*(1-alpha)+tex*alpha;}
        else{const double blend=c==0?.1+.07*u:c==1?.8-.05*u:.25+.04*u;previous=previous*(1-tex)+blend*tex;}}
      const int delta=std::abs(int(actual.rgba[4*p+c])-int(std::lround(previous*255.)));
      maxError=std::max(maxError,delta);bad+=(delta>3);}
    bad+=(actual.rgba[4*p+3]!=255);bad+=(actual.depth[p]!=base.depth[p]);++checked;
  }
  std::cout<<"shadow_multitexture numeric "<<label(s)<<" model="<<kind<<" checked="<<checked<<" bad="<<bad<<" max_rgb="<<maxError<<'\n';
  return check(checked>2000&&bad==0,"independent per-pixel texture composition, unchanged geometry/depth/alpha");
}
bool composition(const CoinRenderCapabilities&caps){bool ok=true;Harness h(caps);
  for(bool dir:{false,true})for(float quality:{.2f,1.f})for(int maps:{1,2,4,8})for(int textures:{2,4,8}){
    Scene s(maps,textures,quality,dir);Frame base,frame,restored;
    if(!h.render(s.root,base,label(s)+" white A")){ok=false;continue;}
    for(int model=0;model<6;++model){s.model(model);const bool rendered=h.render(s.root,frame,label(s)+" model="+std::to_string(model));
      ok &= rendered;if(rendered)ok &= numerical(s,base,frame,model);}
    s.white();const bool back=h.render(s.root,restored,label(s)+" white A restored");ok &= back;if(back)ok &= exact(base,restored);
  }return ok;
}
bool bindingsMatrix(const CoinRenderCapabilities&caps){bool ok=true;Harness h(caps);
  for(float quality:{.2f,1.f})for(int material:bindings)for(int normal:bindings){
    Scene s(4,8,quality,false);s.materialBinding->value=material;s.normalBinding->value=normal;
    Frame base,colored,restored;const std::string name=label(s)+" material="+std::to_string(material)+" normal="+std::to_string(normal);
    if(!h.render(s.root,base,name+" white A")){ok=false;continue;}s.model(0);
    const bool rendered=h.render(s.root,colored,name+" colored B");ok &= rendered;if(rendered)ok &= numerical(s,base,colored,0);
    s.white();
    if(material!=SoMaterialBinding::OVERALL){
      SbColor saved[9];for(int i=0;i<9;++i){saved[i]=s.receiverMaterial->diffuseColor[i];s.receiverMaterial->diffuseColor.set1Value(i,saved[0]);}
      const bool uniform=h.render(s.root,colored,name+" material array uniform, binding unchanged");
      ok &= uniform;if(uniform)ok &= check(difference(base,colored)>25,"non-OVERALL material binding affects actual shadowed shading");
      s.receiverMaterial->diffuseColor.setValues(0,9,saved);
    }
    if(normal!=SoNormalBinding::OVERALL){
      SbVec3f saved[9];for(int i=0;i<9;++i){saved[i]=s.normalData->vector[i];s.normalData->vector.set1Value(i,saved[0]);}
      const bool uniform=h.render(s.root,colored,name+" normal array uniform, binding unchanged");
      ok &= uniform;if(uniform)ok &= check(difference(base,colored)>25,"non-OVERALL normal binding affects actual shadowed lighting");
      s.normalData->vector.setValues(0,9,saved);
    }
    const bool back=h.render(s.root,restored,name+" restored A");ok &= back;if(back)ok &= exact(base,restored);
  }return ok;
}
bool expectedColor(const Frame&f,const std::array<uint8_t,3>&color){unsigned bad=0,checked=0;
  for(int y=20;y<108;++y)for(int x=20;x<108;++x){const size_t p=size_t(y*side+x);if(f.rgba[4*p+3]!=255)continue;
    for(int c=0;c<3;++c)bad+=std::abs(int(f.rgba[4*p+c])-int(color[c]))>2;++checked;}
  return check(checked>2000&&bad==0,"isolated image/UV/matrix/sampler has independent exact texel color");}
bool projectionPolicy(const Scene&s,const CoinRenderFramePlan&frame,CoinRenderTextureProjection expected){
  CoinWgpuFfiFrame ffi;std::string diagnostic;
  if(!ffi.prepare(frame,side,side,diagnostic,false))return check(false,"projection FFI prepare: "+diagnostic);
  const auto &view=ffi.getView();bool ok=check(frame.shadowLights.size()==size_t(s.slots),
    "projection control retains all authored shadow maps");unsigned receivers=0;
  for(const auto &draw:frame.draws){if(draw.shadowLightSlot||draw.sourceNodeId!=s.mesh->getNodeId())continue;
    if(!check(draw.renderStateSlot<frame.renderStates.size()&&draw.renderStateSlot<view.state_count,
      "projection receiver has valid Core/FFI state")){ok=false;continue;}
    const auto &state=frame.renderStates[draw.renderStateSlot];++receivers;
    ok &= check(state.textureProjection==expected&&state.shadowStyle==uint32_t(s.receiverStyle->style.getValue())&&
      view.states[draw.renderStateSlot].texture_projection==uint32_t(expected),
      "authored shadow style selects explicit Core/FFI texture projection policy");
  }
  return check(receivers>0,"projection control includes the authored receiver")&&ok;
}
bool slots(const CoinRenderCapabilities&caps){bool ok=true;Harness h(caps);
  for(bool dir:{false,true})for(float quality:{.2f,1.f})for(int maps:{1,2,4,8}){
    Scene s(maps,8,quality,dir);Frame base,moved,back;
    if(!h.render(s.root,base,label(s)+" all maps A")){ok=false;continue;}
    for(int map=0;map<maps;++map){const float old=s.casterPositions[size_t(map)]->translation.getValue()[0];
      s.casterX(map,old+.65f);const bool shifted=h.render(s.root,moved,label(s)+" map="+std::to_string(map)+" B");s.casterX(map,old);
      const bool restored=h.render(s.root,back,label(s)+" map restored A");ok &= shifted&&restored;
      if(shifted&&restored)ok &= check(difference(base,moved)>12,"every independently moved shadow map contributes")&&exact(base,back);}
    for(int unit:{0,3,7}){s.white();s.pattern(unit);s.uv(unit,.125f,.125f);s.matrices[unit]->matrix=SbMatrix::identity();
      Frame a,b,a2;if(!h.render(s.root,a,label(s)+" unit="+std::to_string(unit)+" texel A")){ok=false;continue;}
      ok &= expectedColor(a,{{240,30,60}});
      s.uv(unit,.75f,.125f);const bool uv=h.render(s.root,b,"independent UV B");ok &= uv;if(uv)ok &= expectedColor(b,{{40,220,70}})&&check(difference(a,b)>1000,"unit UV independently reaches GPU");
      // Shadow receivers deliberately use direct ST, preserving Coin semantics:
      // Q=2 must keep (.75,.125) green. Only NO_SHADOWING selects ST/Q.
      ok &= projectionPolicy(s,h.proxy->plan,CoinRenderTextureProjection::DIRECT_ST);
      SbMatrix projective=SbMatrix::identity();projective[3][3]=2.f;s.matrices[unit]->matrix=projective;
      Frame divided,qRestored;const bool q=h.render(s.root,divided,"shadow receiver direct ST with Q=2");ok &= q;
      if(q){ok &= expectedColor(divided,{{40,220,70}});ok &= projectionPolicy(s,h.proxy->plan,CoinRenderTextureProjection::DIRECT_ST);}
      if(uv&&q)ok &= exact(b,divided);
      s.matrices[unit]->matrix=SbMatrix::identity();
      const bool qBack=h.render(s.root,qRestored,"shadow receiver Q=1 restored");ok &= qBack;
      if(uv&&qBack)ok &= exact(b,qRestored);
      // Change only the receiver's public shadow style. Casters/maps remain.
      // (.75,.125,2) projects to (.375,.0625), safely inside the red texel;
      // losing Q gives the green texel and is therefore independently visible.
      s.receiverStyle->style=SoShadowStyle::NO_SHADOWING;
      Frame projectiveA,projectiveB,projectiveBack;
      const bool pa=h.render(s.root,projectiveA,"non-shadow receiver projective Q=1 A");ok &= pa;
      if(pa){ok &= expectedColor(projectiveA,{{40,220,70}});ok &= projectionPolicy(s,h.proxy->plan,CoinRenderTextureProjection::PROJECTIVE);}
      s.matrices[unit]->matrix=projective;
      const bool pb=h.render(s.root,projectiveB,"non-shadow receiver projective Q=2 B");ok &= pb;
      if(pb){ok &= expectedColor(projectiveB,{{240,30,60}});ok &= projectionPolicy(s,h.proxy->plan,CoinRenderTextureProjection::PROJECTIVE);}
      if(pa&&pb)ok &= check(difference(projectiveA,projectiveB)>1000&&projectiveA.depth==projectiveB.depth,
        "projective homogeneous division changes texel without changing geometry");
      s.matrices[unit]->matrix=SbMatrix::identity();
      const bool pr=h.render(s.root,projectiveBack,"non-shadow receiver projective Q=1 restored A");ok &= pr;
      if(pr)ok &= projectionPolicy(s,h.proxy->plan,CoinRenderTextureProjection::PROJECTIVE);
      if(pa&&pr)ok &= exact(projectiveA,projectiveBack);
      s.receiverStyle->style=SoShadowStyle::SHADOWED;
      s.uv(unit,.125f,.125f);SbMatrix matrix=SbMatrix::identity();matrix[3][0]=.75f;s.matrices[unit]->matrix=matrix;
      const bool transformed=h.render(s.root,b,"independent texture matrix B");ok &= transformed;if(transformed){ok &= expectedColor(b,{{40,220,70}});ok &= projectionPolicy(s,h.proxy->plan,CoinRenderTextureProjection::DIRECT_ST);}
      s.matrices[unit]->matrix=SbMatrix::identity();const bool restored=h.render(s.root,a2,"unit UV/matrix A restored");ok &= restored;if(restored)ok &= exact(a,a2);
      s.uv(unit,1.125f,.125f);s.images[unit]->wrapS=SoTexture2::REPEAT;
      const bool repeated=h.render(s.root,a,"unit repeat A");ok &= repeated;if(repeated)ok &= expectedColor(a,{{240,30,60}});
      s.images[unit]->wrapS=SoTexture2::CLAMP;const bool clamped=h.render(s.root,b,"unit clamp B");ok &= clamped;if(clamped)ok &= expectedColor(b,{{40,220,70}});
      s.images[unit]->wrapS=SoTexture2::REPEAT;const bool repeatBack=h.render(s.root,a2,"unit repeat A restored");ok &= repeatBack;if(repeatBack)ok &= exact(a,a2);
      s.image(unit,{{20,200,220}});const bool image=h.render(s.root,b,"unit image B");ok &= image;if(image)ok &= expectedColor(b,{{20,200,220}});
      s.pattern(unit);const bool imageBack=h.render(s.root,a2,"unit image A restored");ok &= imageBack;if(imageBack)ok &= exact(a,a2);
    }
  }return ok;
}
float halfWeight(float ratio){
#if defined(HAVE_ILOGB)
  const int exponent=std::ilogb(ratio);
#elif defined(HAVE__LOGB)
  const int exponent=int(std::logb(ratio));
#else
  const int exponent=0;(void)ratio;
#endif
  if(exponent< -16)return 0;if(exponent>=16)return .5f;
  const double p=std::sqrt(std::ldexp(.75,exponent));return float(.5*p/(1+p));
}
bool payload(const Scene&s,const CoinRenderFramePlan&frame,const SoSceneTexture2*rtt0=nullptr,bool rttDirect=false){
  bool ok=check(frame.shadowLights.size()==size_t(s.slots),"Core retains every authored shadow slot");
  ok &= check(frame.textureSamplingPolicy==samplingTestPolicy,"Core captures selected sampling policy");
  CoinWgpuFfiFrame ffi;std::string diagnostic;
  if(!ffi.prepare(frame,side,side,diagnostic,false))return check(false,"FFI prepares complete multimap/multitexture frame: "+diagnostic);
  const auto &view=ffi.getView();unsigned receivers=0,checkedCorners=0,checkedCenters=0;
  std::set<uint32_t> materials;
  for(const auto &draw:frame.draws){if(draw.shadowLightSlot||draw.sourceNodeId!=s.mesh->getNodeId())continue;
    ++receivers;const auto &state=frame.renderStates[draw.renderStateSlot];
    ok &= check(state.lightModel==CoinRenderLightModel::PHONG&&state.shadowGroupSlot>0,
      "explicit PHONG model and active shadow group survive capture");
    if(!check(draw.renderStateSlot<view.state_count,"receiver state reaches FFI")){ok=false;continue;}
    const auto &packed=view.states[draw.renderStateSlot];ok &= check(packed.light_model==1,"FFI preserves PHONG rather than baked BaseColor");
    std::set<uint32_t> images;std::set<std::string> authoredImages;
    for(int unit:s.units){const auto tex=coin_render_texture_unit(state,size_t(unit));
      ok &= check(tex.enabled&&tex.imageSlot<frame.textures.size()&&tex.samplerSlot<frame.samplers.size(),"unit image/sampler indices remain independently valid");
      if(!tex.enabled||tex.imageSlot>=frame.textures.size()||tex.samplerSlot>=frame.samplers.size())continue;
      const auto &sampler=frame.samplers[tex.samplerSlot];
      const bool rttUnit=rtt0&&unit==0;
      const auto expectedWrap=[&](bool t){return (t?rtt0->wrapT.getValue():rtt0->wrapS.getValue())==SoSceneTexture2::REPEAT?0u:1u;};
      if(rttUnit){float authoredQuality=-1;for(int child=0;child<s.receiver->getNumChildren();++child){const auto*node=s.receiver->getChild(child);
          if(node->isOfType(SoComplexity::getClassTypeId()))authoredQuality=static_cast<const SoComplexity*>(node)->textureQuality.getValue();}
        std::cout<<"rtt_payload sampler direct="<<rttDirect<<" actual_wrap="<<uint32_t(sampler.wrapS)<<','<<uint32_t(sampler.wrapT)
          <<" expected_wrap="<<expectedWrap(false)<<','<<expectedWrap(true)<<" node_wrap_enums="<<rtt0->wrapS.getValue()<<','<<rtt0->wrapT.getValue()
          <<" actual_filter="<<uint32_t(sampler.filter)<<" expected_filter="<<uint32_t(CoinRenderTextureFilter::LINEAR)<<" authored_texture_quality="<<authoredQuality<<'\n';}

      ok &= check(rttUnit?(uint32_t(sampler.wrapS)==expectedWrap(false)&&uint32_t(sampler.wrapT)==expectedWrap(true)&&
          sampler.filter==CoinRenderTextureFilter::LINEAR):
        (uint32_t(sampler.wrapS)==uint32_t(unit%2)&&uint32_t(sampler.wrapT)==uint32_t(!(unit%2))),
        rttUnit?"unit0 sampler matches effective authored RTT wrap and linear FBO sampling":"Core retains different authored S/T wrap per unit");
      if(tex.samplerSlot<view.sampler_count){const auto &gpu=view.samplers[tex.samplerSlot];
        ok &= check(gpu.wrap_s==uint32_t(sampler.wrapS)&&gpu.wrap_t==uint32_t(sampler.wrapT)&&gpu.filter==uint32_t(sampler.filter),
          "FFI sampler descriptor matches the source unit");}
      else ok &= check(false,"FFI includes each sampler table entry");
      images.insert(tex.imageSlot);const auto &image=frame.textures[tex.imageSlot];
      if(rttUnit){
        std::cout<<"rtt_payload image direct="<<rttDirect<<" producer="<<image.producerId<<" gpu_token="<<image.gpuToken<<" gpu_opaque="<<image.gpuOpaque
          <<" cpu_bytes="<<image.pixelsRgba.size()<<" components="<<image.components<<" expected_components=4 extent="<<image.width<<','<<image.height
          <<" format="<<uint32_t(image.format)<<" alpha_policy="<<image.sceneTransparencyFunction<<" expected_alpha_policy="<<rtt0->transparencyFunction.getValue()
          <<" expected_token_nonzero="<<rttDirect<<" gpu_opaque_is_conservative=1"<<'\n';
        authoredImages.insert("rtt:"+std::to_string(rtt0->getNodeId()));const SbVec2s size=rtt0->size.getValue();
        ok &= check(image.producerId==0&&image.width==uint32_t(size[0])&&image.height==uint32_t(size[1])&&image.components==4&&
          image.format==CoinRenderTextureFormat::RGBA8_LINEAR&&!image.volumeDepth&&!image.cubeFaceSize&&!image.mipmapped&&
          image.sceneTransparencyFunction==rtt0->transparencyFunction.getValue()&&tex.model==CoinRenderTextureModel::MODULATE,
          "unit0 uses resolved authored RGBA8 RTT extent/alpha policy/model rather than superseded static pixels");
        if(rttDirect)ok &= check(image.gpuToken!=0&&image.pixelsRgba.empty(),
          "DIRECT unit0 keeps the producer GPU token without CPU image substitution");
        else{bool white=image.gpuToken==0&&image.pixelsRgba.size()==size_t(size[0])*size[1]*4;
          for(uint8_t byte:image.pixelsRgba)white &= byte==255;
          ok &= check(white,"STAGED unit0 carries the actual opaque white producer image including alpha");}
      }else{
      SbVec2s authoredSize;int components=0;
      const auto*source=s.images[unit]->image.getValue(authoredSize,components);
      std::vector<uint8_t> authoredRgba;
      if(source&&components>=1&&components<=4)for(int p=0;p<int(authoredSize[0])*int(authoredSize[1]);++p){
        const auto*pixel=source+size_t(p)*components;
        authoredRgba.push_back(pixel[0]);authoredRgba.push_back(components<3?pixel[0]:pixel[1]);
        authoredRgba.push_back(components<3?pixel[0]:pixel[2]);
        authoredRgba.push_back(components==2?pixel[1]:components==4?pixel[3]:255);
      }
      std::string key=std::to_string(authoredSize[0])+"x"+std::to_string(authoredSize[1])+":";
      if(!authoredRgba.empty())key.append(reinterpret_cast<const char*>(authoredRgba.data()),authoredRgba.size());
      authoredImages.insert(key);
      ok &= check(!authoredRgba.empty()&&image.width==uint32_t(authoredSize[0])&&image.height==uint32_t(authoredSize[1])&&
        image.pixelsRgba==authoredRgba,"Core unit image bytes match the actual authored unit, including unit 7");
      }
      const uint32_t imageSlot=unit?packed.extra_textures[unit-1].texture_slot:packed.texture_slot;
      const uint32_t samplerSlot=unit?packed.extra_textures[unit-1].sampler_slot:packed.sampler_slot;
      const uint32_t model=unit?packed.extra_textures[unit-1].model:packed.texture_model;
      const uint32_t enabled=unit?packed.extra_textures[unit-1].enabled:packed.has_texture;
      ok &= check(enabled&&imageSlot==tex.imageSlot&&samplerSlot==tex.samplerSlot&&model==uint32_t(tex.model),
        "FFI unit enabled/image/sampler/model match Core slots");
      const float *matrix=unit?packed.extra_textures[unit-1].matrix:packed.texture_matrix;
      for(int row=0;row<4;++row)for(int col=0;col<4;++col)
        ok &= check(matrix[row*4+col]==tex.matrix[row][col],"FFI matrix is delivered without unit alias");
      if(tex.imageSlot<view.texture_count){const auto &gpu=view.textures[tex.imageSlot];
        if(rttUnit)std::cout<<"rtt_payload ffi direct="<<rttDirect<<" format="<<gpu.format<<" reserved="<<gpu.reserved
          <<" digest="<<gpu.content_digest<<" token="<<image.gpuToken<<" has_pixels="<<(gpu.pixels!=nullptr)<<" pixel_bytes="<<gpu.pixel_bytes_len
          <<" expected_direct_format=1 expected_direct_opaque_flag="<<image.gpuOpaque<<" expected_direct_pixels=0 expected_direct_len=0"<<'\n';

        if(rttUnit&&rttDirect)ok &= check(gpu.format==1&&gpu.content_digest==image.gpuToken&&gpu.pixels==nullptr&&gpu.pixel_bytes_len==0&&
          bool(gpu.reserved&1u)==image.gpuOpaque,"FFI DIRECT unit0 transports exact GPU token and conservative opaque flag, not CPU image bytes");
        else ok &= check(gpu.pixel_bytes_len==image.pixelsRgba.size()&&gpu.pixels&&
          std::memcmp(gpu.pixels,image.pixelsRgba.data(),image.pixelsRgba.size())==0,"FFI owns deterministic image bytes");}
      else ok &= check(false,"FFI includes every image table entry");
    }
    ok &= check(images.size()>=authoredImages.size()&&images.size()<=s.units.size(),"distinct authored images remain distinct slots; identical content may share storage");
    const auto testVertex=[&](const CoinRenderVertexSnapshot&v,const float weights[4],const int indices[4]){
      bool valid=true;
      for(int unit:s.units){SbVec2f authored(0,0);for(int k=0;k<4;++k)authored+=s.coords[unit]->point[indices[k]]*weights[k];
        SbVec4f expected; s.matrices[unit]->matrix.getValue().multVecMatrix(SbVec4f(authored[0],authored[1],0,1),expected);
        const float *st=unit?v.extraTexcoords[unit-1]:v.texcoord;
        SbVec4f actual;coin_render_texture_unit(state,size_t(unit)).matrix.multVecMatrix(SbVec4f(st[0],st[1],v.textureR[unit],v.textureQ[unit]),actual);
        for(int c=0;c<4;++c)valid &= std::abs(actual[c]-expected[c])<=.002f;
      }return check(valid,"all authored ST/R/Q channels independently reach captured corners/precise centers");};
    for(uint32_t index=draw.geometry.firstVertex;index<draw.geometry.firstVertex+draw.geometry.vertexCount;++index){
      const auto &v=frame.vertices[index];materials.insert(v.materialSlot);
      for(int corner=0;corner<9;++corner){const auto p=s.positions->point[corner];
        if(std::abs(v.position[0]-p[0])<1e-5f&&std::abs(v.position[1]-p[1])<1e-5f&&std::abs(v.position[2]-p[2])<1e-5f){
          const float w[]={1,0,0,0};const int ix[]={corner,corner,corner,corner};ok &= testVertex(v,w,ix);++checkedCorners;}}
      for(int row=0;row<2;++row)for(int col=0;col<2;++col){const int ix[]={row*3+col,(row+1)*3+col,row*3+col+1,(row+1)*3+col+1};
        SbVec3f centre(0,0,0);for(int k:ix)centre+=s.positions->point[k];centre*=.25f;
        if(std::abs(v.position[0]-centre[0])>1e-5f||std::abs(v.position[1]-centre[1])>1e-5f||std::abs(v.position[2]-centre[2])>1e-5f)continue;
        const float a=halfWeight((s.positions->point[ix[0]]-centre).sqrLength()/(s.positions->point[ix[3]]-centre).sqrLength());
        const float b=halfWeight((s.positions->point[ix[1]]-centre).sqrLength()/(s.positions->point[ix[2]]-centre).sqrLength());
        const float weights[]={a,b,.5f-b,.5f-a};ok &= testVertex(v,weights,ix);++checkedCenters;
      }
    }
  }
  ok &= check(receivers>0&&checkedCorners>0,"receiver geometry/corner delivery measured explicitly");
  const char *precise=std::getenv("COIN_QUADMESH_PRECISE_LIGHTING");
  const bool fanEnabled=precise&&std::atoi(precise)>0;
  if(fanEnabled&&s.materialBinding->value.getValue()!=SoMaterialBinding::PER_VERTEX&&
     s.materialBinding->value.getValue()!=SoMaterialBinding::PER_VERTEX_INDEXED)
    ok &= check(checkedCenters>=4,"active precise fan retains four independently checked centers");
  const auto &shadow=ffi.getShadowFrame();const unsigned packedMaps=1+unsigned(shadow.hasSecond)+unsigned(shadow.hasThird)+unsigned(shadow.hasFourth)+unsigned(shadow.extra.size());
  ok &= check(packedMaps==unsigned(s.slots)&&!shadow.casters.empty(),"FFI transports the complete shadow pass family");
  for(size_t i=0;i<view.vertex_count;++i){bool found=false;
    for(const auto &v:frame.vertices){bool match=true;
      for(int unit:s.units){const float *src=unit?v.extraTexcoords[unit-1]:v.texcoord;const float *dst=unit?view.vertices[i].extra_texcoords[unit-1]:view.vertices[i].texcoord;
        match &= src[0]==dst[0]&&src[1]==dst[1]&&v.textureR[unit]==dst[2]&&v.textureQ[unit]==dst[3];}
      if(match){found=true;break;}}
    ok &= check(found,"every emitted FFI vertex preserves one whole multiunit homogeneous tuple");
  }
  std::cout<<"shadow_multitexture transport "<<label(s)<<" source_vertices="<<frame.vertices.size()<<" ffi_vertices="<<view.vertex_count
    <<" receiver_draws="<<receivers<<" precise_enabled="<<fanEnabled<<" corners="<<checkedCorners<<" centers="<<checkedCenters<<" materials="<<materials.size()
    <<" ffi_maps="<<packedMaps<<" ffi_states="<<view.state_count<<'\n';return ok;
}
bool transport(){bool ok=true;
  std::unique_ptr<CoinRenderTarget> target(samplingTestOffscreen(SbVec2i32(side,side)));
  if(!check(bool(target),"CPU delivery target exists"))return false;
  auto *record=new Recorder;target->getPimpl()->backend.reset(record);CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());
  for(bool dir:{false,true})for(float q:{.2f,1.f})for(int maps:{1,2,4,8})for(int textures:{2,4,8}){
    Scene s(maps,textures,q,dir);s.model(0);action.apply(s.root);
    if(!check(action.getLastStatus()==CoinRenderAction::SUCCESS&&record->plan.draws.size()>0,"record Core delivery without executing GPU")){ok=false;continue;}
    ok &= payload(s,record->plan);const auto before=record->plan;action.apply(s.root);
    ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS&&before.hasSamePayload(record->plan),"repeated capture has deterministic owned payload");
  }action.setRenderTarget(nullptr);return ok;
}
bool smoke(const CoinRenderCapabilities&caps){bool ok=true;Harness h(caps);
  for(bool dir:{false,true})for(float q:{.2f,1.f}){Scene s(8,8,q,dir);Frame a,b,c;
    if(!h.render(s.root,a,label(s)+" smoke A")){ok=false;continue;}s.model(0);
    const bool rendered=h.render(s.root,b,"smoke B");ok &= rendered;if(rendered){ok &= numerical(s,a,b,0);ok &= payload(s,h.proxy->plan);}
    s.white();const bool restored=h.render(s.root,c,"smoke restored A");ok &= restored;if(restored)ok &= exact(a,c);
  }return ok;
}
SoSeparator *producer(SoMaterial **authoredMaterial=nullptr){auto *root=new SoSeparator;auto *camera=new SoOrthographicCamera;
  camera->position.setValue(0,0,3);camera->height=2;camera->nearDistance=1;camera->farDistance=10;root->addChild(camera);
  auto *light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;root->addChild(light);
  auto *material=new SoMaterial;material->diffuseColor.setValue(1,1,1);if(authoredMaterial)*authoredMaterial=material;root->addChild(material);auto *cube=new SoCube;root->addChild(cube);return root;}
bool rollback(const CoinRenderCapabilities&caps){bool ok=true;
  for(auto route:{COIN_RENDER_SCENE_TEXTURE_STAGED,COIN_RENDER_SCENE_TEXTURE_DIRECT})
  for(bool dir:{false,true})for(float q:{.2f,1.f}){
    const bool direct=route==COIN_RENDER_SCENE_TEXTURE_DIRECT;
    Harness h(caps,route);Scene s(8,8,q,dir);Frame base,stable,recovered;
    if(!h.render(s.root,base,"rollback initial A")){ok=false;continue;}
    SoMaterial *producerMaterial=nullptr;
    auto *rtt=new SoSceneTexture2;rtt->scene=producer(&producerMaterial);rtt->size.setValue(32,32);s.stages[0]->insertChild(rtt,2);
    // Staged BGFX uses a separate producer serial; wgpu has one device ledger.
    const uint64_t step=direct||caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?2:1;
    auto renderGraph=[&](Frame&f,const char*phase){
      const uint64_t before=h.target->getLastSubmissionSerial();h.action.apply(s.root);h.target->readbackRGBA(f.rgba);h.target->readbackDepth(f.depth);f.serial=h.target->getLastSubmissionSerial();
      h.serial=f.serial;std::cout<<"shadow_multitexture graph phase="<<phase<<" route="<<(direct?"DIRECT":"STAGED")<<" status="<<int(h.action.getLastStatus())<<" serial="<<before<<','<<f.serial
        <<" preflight="<<h.proxy->preflight<<" producers="<<h.proxy->producers<<" error="<<h.action.getLastError().getString()<<'\n';
      return check(h.action.getLastStatus()==CoinRenderAction::SUCCESS&&f.rgba.size()==size_t(side*side*4)&&f.depth.size()==size_t(side*side)&&f.serial==before+step,"RTT graph publishes producer/root exactly once");};
    if(!renderGraph(stable,"valid graph A")){ok=false;continue;}
    ok &= check(h.proxy->preflight>0&&h.proxy->declaredProducers>0&&h.proxy->producerPlanPoliciesMatch&&
      h.proxy->plan.textureSamplingPolicy==samplingTestPolicy,
      "RTT graph preflight observes selected sampling policy in all producer/root captures");
    if(direct)ok &= check(h.proxy->producers>0&&h.proxy->producerSamplingPolicy==samplingTestPolicy,
      "DIRECT execution reaches the counted producer with selected sampling policy");
    // STAGED producers execute through a different backend. Their deterministic
    // visible contribution, not this root proxy's submitRtt count, observes work.
    producerMaterial->diffuseColor.setValue(.4f,.7f,.9f);Frame tinted;
    const bool changed=renderGraph(tinted,"producer color B");ok &= changed;
    if(changed){unsigned checked=0,bad=0;const double tint[]={.4,.7,.9};
      for(int y=12;y<116;++y)for(int x=12;x<116;++x){const size_t p=size_t(y*side+x);
        if(stable.rgba[4*p+3]!=255)continue;
        for(int c=0;c<3;++c)bad+=std::abs(int(tinted.rgba[4*p+c])-int(std::lround(stable.rgba[4*p+c]*tint[c])))>3;
        bad+=tinted.rgba[4*p+3]!=255;bad+=tinted.depth[p]!=stable.depth[p];++checked;}
      ok &= check(checked>2000&&bad==0&&difference(stable,tinted)>1000,
        "RTT producer contributes its independent constant color to the shadowed texture cascade");
    }
    producerMaterial->diffuseColor.setValue(1,1,1);
    const bool colorRestored=renderGraph(recovered,"producer color restored A");ok &= colorRestored;
    if(colorRestored)ok &= exact(stable,recovered);
    for(int fault=0;fault<3;++fault){const auto old=s.coords[7]->point[0];SoTextureUnit *badUnit=nullptr;SoTexture2 *badImage=nullptr;SoAlphaTest *badAlpha=nullptr;
      if(fault==0)s.coords[7]->point.set1Value(0,SbVec2f(std::numeric_limits<float>::quiet_NaN(),0));
      if(fault==1){badUnit=new SoTextureUnit;badUnit->unit=8;badImage=new SoTexture2;const uint8_t white[]={255,255,255};badImage->image.setValue(SbVec2s(1,1),3,white);
        s.receiver->insertChild(badUnit,s.receiver->getNumChildren()-1);s.receiver->insertChild(badImage,s.receiver->getNumChildren()-1);}
      if(fault==2){badAlpha=new SoAlphaTest;badAlpha->function=SoAlphaTest::GREATER;badAlpha->value=std::numeric_limits<float>::quiet_NaN();s.receiver->insertChild(badAlpha,s.receiver->getNumChildren()-1);}
      const uint64_t serial=h.target->getLastSubmissionSerial();const unsigned submissions=h.proxy->producers;
      h.action.apply(s.root);Frame refused;h.target->readbackRGBA(refused.rgba);h.target->readbackDepth(refused.depth);refused.serial=h.target->getLastSubmissionSerial();
      std::cout<<"shadow_multitexture reject fault="<<fault<<" route="<<(direct?"DIRECT":"STAGED")<<" status="<<int(h.action.getLastStatus())<<" producers="<<submissions<<','<<h.proxy->producers
        <<" serial="<<serial<<','<<refused.serial<<" error="<<h.action.getLastError().getString()<<'\n';
      ok &= check(h.action.getLastStatus()!=CoinRenderAction::SUCCESS&&(!direct||h.proxy->producers==submissions)&&refused.serial==serial&&
        refused.rgba==stable.rgba&&refused.depth==stable.depth,
        direct?"invalid DIRECT graph rejected before any counted producer with atomic buffers/serial":
        "invalid STAGED graph preserves root buffers/serial; producer counter is outside this backend");
      if(fault==0)s.coords[7]->point.set1Value(0,old);if(badImage)s.receiver->removeChild(badImage);if(badUnit)s.receiver->removeChild(badUnit);if(badAlpha)s.receiver->removeChild(badAlpha);
      const bool back=renderGraph(recovered,"recovered graph A");ok &= back;if(back)ok &= exact(stable,recovered);
    }
  }return ok;
}
bool campaign(const CoinRenderCapabilities&caps){
  // Keep one prepared native runtime alive while the existing independent
  // modes create/release their own targets. The anchor never submits a frame.
  Harness anchor(caps);
  if(!check(bool(anchor.target)&&anchor.proxy&&anchor.proxy->real,
    "campaign GPU anchor prepared without publication"))return false;
  bool ok=true;
  std::cout<<"shadow_multitexture campaign mode=smoke\n";ok &= smoke(caps);
  std::cout<<"shadow_multitexture campaign mode=composition\n";ok &= composition(caps);
  std::cout<<"shadow_multitexture campaign mode=bindings\n";ok &= bindingsMatrix(caps);
  std::cout<<"shadow_multitexture campaign mode=slots\n";ok &= slots(caps);
  std::cout<<"shadow_multitexture campaign mode=rollback\n";ok &= rollback(caps);
  ok &= check(anchor.target->getLastSubmissionSerial()==0,
    "campaign runtime anchor never publishes a frame");
  return ok;
}
}
// Share the authored fixture and independent numeric oracles with the X11 test.
#ifndef COIN_RENDER_SHADOW_MULTITEXTURE_FIXTURE_ONLY
int main(int argc,char**argv){
  configureSamplingTest(argc,argv);std::string mode;
  for(int i=1;i<argc;++i){const std::string argument=argv[i];
    if(argument=="--portable-sampling")continue;
    if(!mode.empty())return 2;mode=argument;
  }
  if(mode!="--composition"&&mode!="--bindings"&&mode!="--slots"&&mode!="--transport"&&mode!="--rollback"&&mode!="--smoke"&&mode!="--campaign")return 2;
  SoDB::init();CoinRenderAction::initClass();if(mode=="--transport")return transport()?0:1;
  const auto caps=msaa_shadow_test::capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN);
  if(!caps.gpu_available||caps.renderer==COIN_RENDER_RENDERER_UNKNOWN)return 77;
  if(!check(caps.max_texture_units>=8&&caps.max_lights_per_draw>=8,"reported logical texture/light capacities cover the requested profile"))return 1;
  return (mode=="--campaign"?campaign(caps):mode=="--composition"?composition(caps):mode=="--bindings"?bindingsMatrix(caps):mode=="--slots"?slots(caps):
    mode=="--smoke"?smoke(caps):rollback(caps))?0:1;
}

#endif
