#ifndef COIN_RENDER_BUMP_PROFILE_FIXTURE_H
#define COIN_RENDER_BUMP_PROFILE_FIXTURE_H
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoRotation.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoBumpMapCoordinate.h>
#include <Inventor/nodes/SoBumpMapTransform.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include "CoinRenderSamplingTestOptions.h"
#include "rendering/coinrender/CoinRenderBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderRttCore.h"
#include "rendering/coinrender/CoinRenderBumpMapCore.h"
#include <array>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <memory>
#include <limits>
namespace bump_profile {
constexpr unsigned side=96;
inline bool check(bool v,const std::string&s){if(!v)std::cerr<<"bump_profile FAIL "<<s<<'\n';return v;}
using RGB=std::array<double,3>;
inline RGB normalized(RGB a){double l=std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);for(auto&v:a)v=l?v/l:0;return a;}
inline double dot(RGB a,RGB b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline double decode(double x){return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4);}
struct Frame{std::vector<uint8_t>rgba;std::vector<float>depth;uint64_t serial=0;};
struct Scene{
 SoSeparator*root=new SoSeparator;SoOrthographicCamera*camera=new SoOrthographicCamera;
 SoDirectionalLight*light=new SoDirectionalLight;SoMaterial*material=new SoMaterial;
 SoBumpMap*bump=new SoBumpMap;SoBumpMapCoordinate*uv=new SoBumpMapCoordinate;
 SoBumpMapTransform*matrix=new SoBumpMapTransform;SoTexture2*base=nullptr;
 SoComplexity*quality=new SoComplexity;SoTextureCoordinate2*baseUv=nullptr;
 SoCoordinate3*positions=new SoCoordinate3;SoIndexedFaceSet*mesh=new SoIndexedFaceSet;
 std::array<uint8_t,4>baseColor{{255,255,255,255}};std::array<uint8_t,3>normal{{204,128,230}};RGB lightVector{{.6,0,.8}},diffuse{{.8,.4,.2}};
 explicit Scene(bool textured=false){root->ref();camera->position.setValue(0,0,4);camera->height=4;camera->nearDistance=1;camera->farDistance=9;root->addChild(camera);
 root->addChild(light);direction(lightVector);material->ambientColor.setValue(0,0,0);material->emissiveColor.setValue(0,0,0);material->specularColor.setValue(0,0,0);root->addChild(material);setDiffuse(diffuse);
 quality->textureQuality=.1f;root->addChild(quality);{base=new SoTexture2;root->addChild(base);setBase(textured?std::array<uint8_t,4>{{128,192,64,255}}:std::array<uint8_t,4>{{255,255,255,255}});auto*st=new SoTextureCoordinate2;baseUv=st;const SbVec2f t[]={SbVec2f(0,0),SbVec2f(1,0),SbVec2f(1,1),SbVec2f(0,1)};st->point.setValues(0,4,t);root->addChild(st);}
 root->addChild(bump);setNormal(normal);root->addChild(uv);root->addChild(matrix);
 const SbVec2f t[]={SbVec2f(0,0),SbVec2f(1,0),SbVec2f(1,1),SbVec2f(0,1)};uv->point.setValues(0,4,t);
 auto*n=new SoNormal;n->vector.setValue(0,0,1);root->addChild(n);auto*nb=new SoNormalBinding;nb->value=SoNormalBinding::OVERALL;root->addChild(nb);
 // Axis aligned UV mapping keeps signed legacy S=+X,T=-Y.
 const SbVec3f p[]={SbVec3f(-1.4f,-1.3f,0),SbVec3f(1.3f,-1.3f,0),SbVec3f(1.3f,1.35f,0),SbVec3f(-1.4f,1.35f,0)};
 positions->point.setValues(0,4,p);root->addChild(positions);const int32_t ix[]={0,1,2,-1,0,2,3,-1};mesh->coordIndex.setValues(0,8,ix);root->addChild(mesh);}
 ~Scene(){root->unref();}Scene(const Scene&)=delete;
 void direction(RGB v){lightVector=v;light->direction.setValue(float(-v[0]),float(-v[1]),float(-v[2]));}
 void setNormal(std::array<uint8_t,3>v){normal=v;bump->image.setValue(SbVec2s(1,1),3,v.data());}
 void setDiffuse(RGB v){diffuse=v;material->diffuseColor.setValue(float(v[0]),float(v[1]),float(v[2]));}
 void setBase(std::array<uint8_t,4>v){baseColor=v;base->image.setValue(SbVec2s(1,1),4,v.data());base->model=SoTexture2::MODULATE;}
 RGB expected(bool srgb=false)const{RGB n{{normal[0]*2./255-1,normal[1]*2./255-1,normal[2]*2./255-1}};auto l=normalized(lightVector);l[1]=-l[1];double f=std::max(0.,std::min(1.,dot(n,l)));RGB out;for(unsigned c=0;c<3;++c){double b=base?baseColor[c]/255.:1;if(srgb)b=decode(b);out[c]=255*diffuse[c]*f*b;}return out;}
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
  CoinRenderSubmitResult submitAsync(const CoinRenderFramePlan&f,CoinRenderTargetP&t,CoinRenderReadbackTicket&ticket,const CoinRenderFrameReuseDecision&r)override{plan=f;return real->submitAsync(f,t,ticket,r);}
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

struct Harness{
 std::unique_ptr<CoinRenderTarget>target;CoinRenderAction action{SbViewportRegion(side,side)};Proxy*proxy=nullptr;
 Harness(unsigned samples=1,bool srgb=false,CoinRenderSceneTextureMode route=COIN_RENDER_SCENE_TEXTURE_STAGED){auto o=samplingTestOptions();o.sampleCount=samples;o.sceneTexture=route;if(srgb)o.storedTextureColorSpace=COIN_RENDER_TEXTURE_SRGB;target.reset(samplingTestOffscreen(SbVec2i32(side,side),o));if(target){auto&p=target->getPimpl().get();if(p.prepareBackend()!=CoinRenderBackendStatus::SUCCESS||!p.backend||!p.backend->isGpuBackend()){target.reset();return;}proxy=new Proxy(std::move(p.backend));p.backend.reset(proxy);target->setDepthReadbackEnabled(TRUE);action.setRenderTarget(target.get());}action.setBackgroundColor(SbColor4f(0,0,0,0));action.setTransparencyType(CoinRenderAction::NONE);}
 ~Harness(){action.setRenderTarget(nullptr);}
 bool render(SoNode*root,Frame&f,const std::string&label){if(!check(bool(target),"real GPU target"))return false;auto before=target->getLastSubmissionSerial();action.apply(root);target->readbackRGBA(f.rgba);target->readbackDepth(f.depth);f.serial=target->getLastSubmissionSerial();std::cout<<"bump_profile "<<label<<" status="<<action.getLastStatus()<<" serial="<<f.serial<<" error="<<action.getLastError().getString()<<'\n';return check(action.getLastStatus()==CoinRenderAction::SUCCESS&&f.serial>before&&f.rgba.size()==side*side*4&&f.depth.size()==side*side,label);}
 bool reject(SoNode*root,const Frame&stable,const std::string&label){auto serial=target->getLastSubmissionSerial();auto producers=proxy->producers;action.apply(root);Frame f;target->readbackRGBA(f.rgba);target->readbackDepth(f.depth);return check(action.getLastStatus()!=CoinRenderAction::SUCCESS&&action.getLastError().getLength()>0&&target->getLastSubmissionSerial()==serial&&proxy->producers==producers&&f.rgba==stable.rgba&&f.depth==stable.depth,label+" atomic diagnostic refusal");}
};
struct RttScene{
 Scene producer;SoSeparator*root=new SoSeparator;SoSceneTexture2*texture=new SoSceneTexture2;
 RttScene(){root->ref();auto*camera=new SoOrthographicCamera;camera->position.setValue(0,0,4);camera->height=2;camera->nearDistance=1;camera->farDistance=9;root->addChild(camera);auto*lm=new SoLightModel;lm->model=SoLightModel::BASE_COLOR;root->addChild(lm);auto*m=new SoMaterial;m->diffuseColor.setValue(1,1,1);root->addChild(m);texture->scene=producer.root;texture->size.setValue(64,64);texture->type=SoSceneTexture2::RGBA8;texture->transparencyFunction=SoSceneTexture2::NONE;texture->model=SoSceneTexture2::MODULATE;root->addChild(texture);auto*uv=new SoTextureCoordinate2;for(int i=0;i<4;++i)uv->point.set1Value(i,SbVec2f(.5,.5));root->addChild(uv);auto*r=new SoRotation;r->rotation.setValue(SbVec3f(0,0,1),.17f);root->addChild(r);auto*p=new SoCoordinate3;const SbVec3f pos[]={SbVec3f(-.8,-.8,0),SbVec3f(.8,-.8,0),SbVec3f(.8,.8,0),SbVec3f(-.8,.8,0)};p->point.setValues(0,4,pos);root->addChild(p);auto*mesh=new SoIndexedFaceSet;const int32_t ix[]={0,1,2,-1,0,2,3,-1};mesh->coordIndex.setValues(0,8,ix);root->addChild(mesh);}
 ~RttScene(){root->unref();}
};
inline bool color(const Frame&f,RGB e,const std::string&label,unsigned w=side,unsigned h=side){if(!check(f.rgba.size()==size_t(w)*h*4,"complete oracle image"))return false;unsigned bad=0;int maximum=0;for(unsigned y=h*3/8;y<h*5/8;++y)for(unsigned x=w*3/8;x<w*5/8;++x){auto i=(size_t(y)*w+x)*4;for(unsigned c=0;c<3;++c){int d=std::abs(int(f.rgba[i+c])-int(std::lround(e[c])));maximum=std::max(maximum,d);bad+=d>3;}bad+=f.rgba[i+3]!=255;}auto i=(size_t(h/2)*w+w/2)*4;std::cout<<"bump_oracle "<<label<<" expected="<<e[0]<<','<<e[1]<<','<<e[2]<<" actual="<<unsigned(f.rgba[i])<<','<<unsigned(f.rgba[i+1])<<','<<unsigned(f.rgba[i+2])<<" bad="<<bad<<" max="<<maximum<<'\n';return check(bad==0,label+" independently decoded normal DOT3 RGB/opaque alpha");}
inline bool exact(const Frame&a,const Frame&b){return check(a.rgba==b.rgba&&a.depth==b.depth,"same environment exact A/B/A color/depth");}
inline void sloped(Scene&s){auto*r=new SoRotation;r->rotation.setValue(SbVec3f(0,0,1),.23f);s.root->insertChild(r,s.root->findChild(s.positions));const auto l=s.lightVector;double a=.23;s.light->direction.setValue(float(-(std::cos(a)*l[0]-std::sin(a)*l[1])),float(-(std::sin(a)*l[0]+std::cos(a)*l[1])),float(-l[2]));}
inline bool coverage(const Frame&f,RGB e,unsigned n){unsigned empty=0,full=0,partial=0,odd=0,bad=0;float plane=f.depth[side*side/2+side/2];for(unsigned p=0;p<side*side;++p){unsigned a=f.rgba[p*4+3],k=unsigned(std::lround(a*n/255.));if(!a){++empty;bad+=f.depth[p]!=1;continue;}full+=k==n;partial+=k>0&&k<n;odd+=k<n&&(k&1);bad+=k>n||std::abs(int(a)-int(std::lround(255.*k/n)))>1;for(unsigned c=0;c<3;++c)bad+=std::abs(int(f.rgba[p*4+c])-int(std::lround(e[c]*k/n)))>3;bad+=std::abs(f.depth[p]-plane)>1e-6;}
 std::cout<<"bump_coverage samples="<<n<<" empty="<<empty<<" full="<<full<<" partial="<<partial<<" odd="<<odd<<" bad="<<bad<<'\n';return check(plane>0&&plane<1&&empty>100&&full>100&&bad==0&&(n==1||partial>0)&&(n!=4||odd>0),"actual sample coverage, resolved RGB/alpha and sample depth");}
}
#endif
