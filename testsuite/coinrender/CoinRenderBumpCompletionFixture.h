#ifndef COIN_RENDER_BUMP_COMPLETION_FIXTURE_H
#define COIN_RENDER_BUMP_COMPLETION_FIXTURE_H
#include "CoinRenderBumpProfileFixture.h"
#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include <Inventor/nodes/SoSpotLight.h>
#include <Inventor/nodes/SoScale.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoTexture3.h>
#include <Inventor/nodes/SoTextureCubeMap.h>
#include <Inventor/nodes/SoTextureCoordinate3.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTextureCombine.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <cstring>
namespace bump_completion {
using namespace bump_profile;
inline double unit(double v) {return std::max(0.,std::min(1.,v));}
struct Lamp {SoLight *node=nullptr; int kind=0; RGB vector{},color{}; double power=0;};
struct Scene : bump_profile::Scene {
  std::vector<Lamp> lamps;
  RGB spec{{.025,.045,.035}}, shift{{0,0,0}}, scale{{1,1,1}};
  double angle=0, shine=.23;
  RGB prior{{1,1,1}},blend{{.21,.43,.67}}; int baseModel=0;
  RGB cameraPosition{{0,0,4}};
  SoTranslation *translation=new SoTranslation;
  SoRotation *rotation=new SoRotation;
  SoScale *scaling=new SoScale;
  explicit Scene(unsigned count=8,bool directional=false,bool spotLast=false):bump_profile::Scene(true) {
    root->removeChild(light);light=nullptr;
    for(unsigned i=0;i<count;++i) {
      Lamp l;l.kind=directional?0:((i==count-1||(i>0&&i%3==0))?1:0);if(spotLast&&i==count-1)l.kind=2;
      l.vector=l.kind?RGB{{-.65+.19*i,.43-.11*i,1.35+.13*i}}:normalized({{.11+.04*i,-.28+.06*i,1}});
      l.color={{.31+.07*(i%4),.72-.08*(i%5),.24+.09*(i%6)}};l.power=.85/count;
      if(l.kind==1){auto*n=new SoPointLight;n->location.setValue(l.vector[0],l.vector[1],l.vector[2]);l.node=n;}
      else if(l.kind==2){auto*n=new SoSpotLight;n->location.setValue(l.vector[0],l.vector[1],l.vector[2]);n->direction.setValue(1,0,0);n->cutOffAngle=.06f;n->dropOffRate=.95f;l.node=n;}
      else {auto*n=new SoDirectionalLight;n->direction.setValue(-l.vector[0],-l.vector[1],-l.vector[2]);l.node=n;}
      l.node->color.setValue(l.color[0],l.color[1],l.color[2]);l.node->intensity=l.power;root->insertChild(l.node,1+i);lamps.push_back(l);
    }
    root->insertChild(translation,root->findChild(material));root->insertChild(rotation,root->findChild(material));root->insertChild(scaling,root->findChild(material));
    setDiffuse({{.62,.47,.73}});setBase({{173,219,131,255}});setNormal({{153,108,241}});setSpec(spec);
  }
  void setSpec(RGB value){spec=value;material->specularColor.setValue(value[0],value[1],value[2]);material->shininess=shine;}
  void transform(bool enabled){shift=enabled?RGB{{.17,-.13,.21}}:RGB{{0,0,0}};scale=enabled?RGB{{.73,.91,1.37}}:RGB{{1,1,1}};angle=enabled?.19:0;cameraPosition=enabled?RGB{{.23,-.16,4}}:RGB{{0,0,4}};translation->translation.setValue(shift[0],shift[1],shift[2]);rotation->rotation.setValue(SbVec3f(0,0,1),angle);scaling->scaleFactor.setValue(scale[0],scale[1],scale[2]);camera->position.setValue(cameraPosition[0],cameraPosition[1],cameraPosition[2]);}
  RGB object(RGB p,bool point)const {if(point)for(int c=0;c<3;++c)p[c]-=shift[c];const double cs=std::cos(angle),sn=std::sin(angle);return {{(cs*p[0]+sn*p[1])/scale[0],(-sn*p[0]+cs*p[1])/scale[1],p[2]/scale[2]}};}
  void locate(unsigned i,RGB value){lamps[i].vector=value;if(lamps[i].kind==1)static_cast<SoPointLight*>(lamps[i].node)->location.setValue(value[0],value[1],value[2]);else if(lamps[i].kind==2)static_cast<SoSpotLight*>(lamps[i].node)->location.setValue(value[0],value[1],value[2]);else static_cast<SoDirectionalLight*>(lamps[i].node)->direction.setValue(-value[0],-value[1],-value[2]);}
};
inline RGB expected(const Scene&s,double x,double y,RGB base) {
  const double u=(x+1.4)/2.7,v=(y+1.3)/2.65;unsigned ids[3];double w[3];
  if(v<=u){ids[0]=0;ids[1]=1;ids[2]=2;w[0]=1-u;w[1]=u-v;w[2]=v;}else{ids[0]=0;ids[1]=2;ids[2]=3;w[0]=1-v;w[1]=u;w[2]=v-u;}
  const RGB eye=s.object(s.cameraPosition,true),n{{s.normal[0]*2./255-1,s.normal[1]*2./255-1,s.normal[2]*2./255-1}};
  RGB diffuse{},specular{};
  for(const auto&lamp:s.lamps){if(!lamp.node->on.getValue())continue;RGB l{},e{};const auto source=s.object(lamp.vector,lamp.kind!=0);
    for(unsigned k=0;k<3;++k){const auto p=s.positions->point[ids[k]];RGB a=source,b=eye;for(int c=0;c<3;++c){if(lamp.kind)a[c]-=p[c];b[c]-=p[c];}a=normalized(a);b=normalized(b);a[1]=-a[1];b[1]=-b[1];for(int c=0;c<3;++c){l[c]+=w[k]*a[c];e[c]+=w[k]*b[c];}}
    const double d=unit(dot(n,normalized(l)));const RGB half=normalized({{l[0]+e[0],l[1]+e[1],l[2]+e[2]}});const double q=std::pow(unit(dot(n,half)),s.shine*64);
    for(int c=0;c<3;++c){diffuse[c]+=s.diffuse[c]*lamp.color[c]*lamp.power*d;specular[c]+=s.spec[c]*q;}
  }
  RGB out;for(int c=0;c<3;++c){double d=unit(diffuse[c])*s.prior[c];d=s.baseModel==1?base[c]:s.baseModel==2?d*(1-base[c])+s.blend[c]*base[c]:s.baseModel==3?unit(d+base[c]):d*base[c];out[c]=255*unit(d+specular[c]);}return out;
}
template<class Sample>bool oracle(const Scene&s,const Frame&f,Sample sample,unsigned extent=side,bool depth=true,bool report=true){
  if(!check(f.rgba.size()==size_t(extent)*extent*4&&(!depth||f.depth.size()==size_t(extent)*extent),"complete mixed-light image"))return false;
  unsigned count=0,bad=0;int maximum=0;
  for(unsigned y=8;y<extent-8;++y)for(unsigned x=8;x<extent-8;++x){RGB world{{s.cameraPosition[0]+(x+.5)*4/extent-2,s.cameraPosition[1]+2-(y+.5)*4/extent, s.shift[2]}};const auto p=s.object(world,true);if(p[0]<-1.20||p[0]>1.10||p[1]<-1.10||p[1]>1.15)continue;const auto e=expected(s,p[0],p[1],sample(p[0],p[1]));size_t i=size_t(y)*extent+x;
    for(int c=0;c<3;++c){const int delta=std::abs(int(f.rgba[4*i+c])-int(std::lround(e[c])));maximum=std::max(maximum,delta);if(delta>3&&bad++<4&&report)std::cerr<<"mixed_first xy="<<x<<','<<y<<" c="<<c<<" expected="<<e[c]<<" actual="<<int(f.rgba[4*i+c])<<'\n';}bad+=f.rgba[4*i+3]!=255;if(depth)bad+=std::abs(f.depth[i]-(s.cameraPosition[2]-s.shift[2]-1)/8)>2e-5;++count;
  }
  if(report)std::cout<<"completion_oracle lights="<<s.lamps.size()<<" pixels="<<count<<" bad="<<bad<<" max="<<maximum<<'\n';return !report?(count>150&&bad==0):check(count>150&&bad==0,"independent object/TBN per-vertex normalized vectors, interpolation and all-light sum");
}
inline RGB base(const Scene&s,bool srgb){RGB value;for(int c=0;c<3;++c)value[c]=srgb?decode(s.baseColor[c]/255.):s.baseColor[c]/255.;return value;}
inline bool oracle(const Scene&s,const Frame&f,bool srgb=false){return oracle(s,f,[&](double,double){return base(s,srgb);});}
class CompleteProxy:public bump_profile::Proxy {
public:
  using bump_profile::Proxy::Proxy;
  bool failAssembly=false;uint64_t failedCandidate=0;std::vector<uint64_t> retired;
  CoinRenderSubmitResult assembleCubeRtt(const std::array<uint64_t,6>&faces,uint32_t n,bool mips,CoinRenderTargetP&t,uint64_t&token)override{auto r=real->assembleCubeRtt(faces,n,mips,t,token);if(failAssembly&&token){failedCandidate=token;return {CoinRenderBackendStatus::OUT_OF_MEMORY,"independent failure after real returned cube candidate",0};}return r;}
  void retireRtt(const std::vector<uint64_t>&tokens)override{retired.insert(retired.end(),tokens.begin(),tokens.end());real->retireRtt(tokens);}
  void finishRtt(const std::vector<uint64_t>&tokens)override{retired.insert(retired.end(),tokens.begin(),tokens.end());real->finishRtt(tokens);}
};
inline CompleteProxy* complete(Harness&h){auto*p=new CompleteProxy(std::move(h.proxy->real));h.target->getPimpl()->backend.reset(p);h.proxy=p;return p;}
// A capture-only backend records traversal. It is never used as a producer or
// pixel reference and never claims a GPU execution pass.
struct Capture:CoinRenderBackend{CoinRenderFramePlan plan;std::string error;bool isGpuBackend()const override{return true;}bool supportsOpaqueBump()const override{return true;}CoinRenderBackendStatus getStatus()const override{return CoinRenderBackendStatus::SUCCESS;}CoinRenderBackendStatus prepare(CoinRenderTargetP&)override{return CoinRenderBackendStatus::SUCCESS;}CoinRenderSubmitResult submit(const CoinRenderFramePlan&f,CoinRenderTargetP&)override{plan=f;return {CoinRenderBackendStatus::SUCCESS,"",1};}void poll()override{}const std::string&getLastError()const override{return error;}};
inline bool capture(Scene&s,CoinRenderFramePlan&plan){std::unique_ptr<CoinRenderTarget>t(samplingTestOffscreen(SbVec2i32(side,side)));if(!t)return false;auto*c=new Capture;t->getPimpl()->backend.reset(c);CoinRenderAction a(SbViewportRegion(side,side));a.setTransparencyType(CoinRenderAction::NONE);a.setRenderTarget(t.get());a.apply(s.root);plan=c->plan;a.setRenderTarget(nullptr);return check(a.getLastStatus()==CoinRenderAction::SUCCESS&&!plan.draws.empty(),"device-free capture records actual occurrences");}
}
#endif
