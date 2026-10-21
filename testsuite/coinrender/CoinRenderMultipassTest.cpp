#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/nodes/SoFont.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoFragmentShader.h>
#include <Inventor/nodes/SoShaderParameter.h>
#include <Inventor/nodes/SoShaderProgram.h>
#include <Inventor/rendering/CoinRenderShaderModule.h>
#include <Inventor/nodes/SoMarkerSet.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include "rendering/coinrender/CoinRenderShadowCore.h"
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTranslation.h>
#include "rendering/coinrender/CoinRenderRttCore.h"
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#ifdef COIN_MULTIPASS_X11
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#endif
#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <unistd.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
const int side=64;
bool good=true;
bool check(bool condition,const std::string &label) {
  if(!condition) {good=false;std::cerr<<"multipass FAIL "<<label<<'\n';}
  return condition;
}
// Public Coin/OpenGL Red Book sampling contract, in pixel coordinates. This
// oracle neither calls coin_viewvolume_jitter nor reads a captured projection.
// Counts between the published tables consume the first N entries, not a
// renormalized subset. N=1 never indexes a jitter table.
using Sample=std::array<double,2>;
std::vector<Sample> samples(int n) {
  static const Sample j2[]={{{.25,.77}},{{.75,.25}}};
  static const Sample j3[]={{{.5033922635,.8317967229}},{{.7806016275,.2504380877}},{{.2261828938,.4131553612}}};
  static const Sample j4[]={{{.375,.25}},{{.125,.75}},{{.875,.25}},{{.625,.75}}};
  static const Sample j5[]={{{.5,.5}},{{.3,.1}},{{.7,.9}},{{.9,.3}},{{.1,.7}}};
  static const Sample j6[]={{{.4646464646,.4646464646}},{{.1313131313,.7979797979}},{{.5353535353,.8686868686}},{{.8686868686,.5353535353}},{{.7979797979,.1313131313}},{{.2020202020,.2020202020}}};
  static const Sample j8[]={{{.5625,.4375}},{{.0625,.9375}},{{.3125,.6875}},{{.6875,.8125}},{{.8125,.1875}},{{.9375,.5625}},{{.4375,.0625}},{{.1875,.3125}}};
  static const Sample j9[]={{{.5,.5}},{{.1666666666,.9444444444}},{{.5,.1666666666}},{{.5,.8333333333}},{{.1666666666,.2777777777}},{{.8333333333,.3888888888}},{{.1666666666,.6111111111}},{{.8333333333,.7222222222}},{{.8333333333,.0555555555}}};
  static const Sample j12[]={{{.4166666666,.625}},{{.9166666666,.875}},{{.25,.375}},{{.4166666666,.125}},{{.75,.125}},{{.0833333333,.125}},{{.75,.625}},{{.25,.875}},{{.5833333333,.375}},{{.9166666666,.375}},{{.0833333333,.625}},{{.583333333,.875}}};
  static const Sample j16[]={{{.375,.4375}},{{.625,.0625}},{{.875,.1875}},{{.125,.0625}},{{.375,.6875}},{{.875,.4375}},{{.625,.5625}},{{.375,.9375}},{{.625,.3125}},{{.125,.5625}},{{.125,.8125}},{{.375,.1875}},{{.875,.9375}},{{.875,.6875}},{{.125,.3125}},{{.625,.8125}}};
  if(n==1)return {Sample{{.5,.5}}};
  const Sample *p=n==2?j2:n==3?j3:n==4?j4:n==5?j5:n==6?j6:n<=8?j8:n==9?j9:n<=12?j12:j16;
  return std::vector<Sample>(p,p+n);
}
struct Scene {
  SoSeparator *root=new SoSeparator;
  SoOrthographicCamera *camera=new SoOrthographicCamera;
  SoCoordinate3 *coordinates=new SoCoordinate3;
  SoMaterial *material=new SoMaterial;
  unsigned captures=0;
  double edge=32.33;
  static void callback(void *data,SoAction *action) {
    if(action->isOfType(SoCallbackAction::getClassTypeId()))
      ++static_cast<Scene*>(data)->captures;
  }
  Scene() {
    root->ref();
    camera->position.setValue(0,0,3);camera->height=2;
    camera->nearDistance=1;camera->farDistance=5;
    camera->viewportMapping=SoCamera::LEAVE_ALONE;root->addChild(camera);
    auto *visit=new SoCallback;visit->setCallback(callback,this);root->addChild(visit);
    auto *model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;root->addChild(model);
    material->diffuseColor.setValue(.25f,.5f,.75f);
    material->transparency=0;root->addChild(material);
    root->addChild(coordinates);
    auto *face=new SoFaceSet;face->numVertices=4;root->addChild(face);
    move(edge);
  }
  void move(double pixel) {
    edge=pixel;
    const float right=float(pixel/32-1);
    const SbVec3f points[]={SbVec3f(-.75f,-.75f,0),SbVec3f(right,-.75f,0),SbVec3f(right,.75f,0),SbVec3f(-.75f,.75f,0)};
    coordinates->point.setValues(0,4,points);
  }
  ~Scene(){root->unref();}
};
struct Image {std::vector<uint8_t> rgba;std::vector<float> depth;};
Image read(CoinRenderTarget *target) {
  Image image;target->readbackRGBA(image.rgba);target->readbackDepth(image.depth);return image;
}
bool exact(const Image&a,const Image&b){return a.rgba==b.rgba&&a.depth==b.depth;}
// Only rows well inside the quad are compared. The vertical edge is at32.33,
// at least .0174 pixels from every relevant sample boundary in these tables.
// No implementation-specific MSAA locations or silhouette equality is used.
bool imageValid(const Image &image,int n,double edge,const char *label) {
  if(!check(image.rgba.size()==side*side*4&&image.depth.size()==side*side,std::string(label)+" complete current color/depth"))return false;
  const auto offsets=samples(n);
  unsigned bad=0,depthBad=0,partial=0,bright=0;
  for(int y=16;y<48;++y)for(int x=0;x<side;++x) {
    unsigned covered=0;bool last=false;
    for(const auto &j:offsets) {
      // clip translation +(j-.5) pixels, sample at the pixel center.
      const double px=x+.5-(j[0]-.5);
      last=px>8&&px<edge;covered+=last;
    }
    const size_t p=size_t(y*side+x);
    const double fraction=double(covered)/n;
    const double color[]={.25,.5,.75,1};
    for(unsigned c=0;c<4;++c)
      bad+=std::abs(double(image.rgba[p*4+c])-255*color[c]*fraction)>(c==3?1:3);
    depthBad+=!std::isfinite(image.depth[p])||std::abs(image.depth[p]-(last?.5f:1.f))>1e-5f;
    partial+=covered>0&&covered<unsigned(n);
    bright+=covered==unsigned(n)&&x>=12&&x<24;
  }
  std::cout<<"multipass_image "<<label<<" n="<<n<<" bad="<<bad<<" depth_bad="<<depthBad<<" partial="<<partial<<" bright="<<bright<<'\n';
  return check(!bad&&!depthBad&&bright>300&&(n==1||partial>=32),std::string(label)+" analytic linear RGBA mean, undimmed interior and LAST depth");
}
class Audit:public CoinRenderBackend {
public:
  std::unique_ptr<CoinRenderBackend> real;
  unsigned commands=0;
  CoinRenderFramePlan last;
  explicit Audit(std::unique_ptr<CoinRenderBackend> p):real(std::move(p)){}
  bool isGpuBackend()const override{return real->isGpuBackend();}
  bool initializesCpuDepthBuffer()const override{return real->initializesCpuDepthBuffer();}
  bool supportsOffscreenShadows()const override{return real->supportsOffscreenShadows();}
  bool supportsOpaqueBump()const override{return real->supportsOpaqueBump();}
  bool resetOnActionDetach()const override{return real->resetOnActionDetach();}
  CoinRenderBackendStatus getStatus()const override{return real->getStatus();}
  CoinRenderBackendStatus prepare(CoinRenderTargetP&t)override{return real->prepare(t);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&f,CoinRenderTargetP&t)override{++commands;last=f;return real->submit(f,t);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&f,CoinRenderTargetP&t,const CoinRenderFrameReuseDecision&r)override{++commands;last=f;return real->submit(f,t,r);}
#ifndef COIN_MULTIPASS_LEGACY_MANAGER
  void configureMultipassTarget(const CoinRenderTargetP&t)override{real->configureMultipassTarget(t);}
  bool multipassOutputBytes(const CoinRenderTargetP&t,bool asynchronous,uint64_t&bytes)const override{return real->multipassOutputBytes(t,asynchronous,bytes);}
  CoinRenderSubmitResult preflightMultipass(const CoinRenderFramePlan&f,const SbVec2i32&s)const override{return real->preflightMultipass(f,s);}
  CoinRenderSubmitResult preflightMultipassGraph(const CoinRenderRttPlan&g,const CoinRenderFramePlan&f,const SbVec2i32&s)const override{return real->preflightMultipassGraph(g,f,s);}
#endif
  CoinRenderSubmitResult submitAsync(const CoinRenderFramePlan&f,CoinRenderTargetP&t,CoinRenderReadbackTicket&ticket,const CoinRenderFrameReuseDecision&r)override{++commands;last=f;return real->submitAsync(f,t,ticket,r);}
  bool requiresSharedRetirement()const override{return real->requiresSharedRetirement();}
  CoinRenderDeviceDomain resourceDomain()const override{return real->resourceDomain();}
  bool readbackLoad(uint64_t&a,uint64_t&b)const override{return real->readbackLoad(a,b);}
  void retireLostReadbacks()override{real->retireLostReadbacks();}
  void poll()override{real->poll();}
  const std::string&getLastError()const override{return real->getLastError();}
};
Audit *prepare(CoinRenderTarget *target,bool gpu) {
  if(!target)return nullptr;
  if(!gpu)target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
  auto &p=target->getPimpl().get();
  if(!check(p.prepareBackend()==CoinRenderBackendStatus::SUCCESS&&p.backend&&p.backend->isGpuBackend()==gpu,"explicit requested CPU/GPU executor"))return nullptr;
  auto *audit=new Audit(std::move(p.backend));p.backend.reset(audit);
  target->setDepthReadbackEnabled(TRUE);return audit;
}
#ifndef COIN_MULTIPASS_LEGACY_MANAGER
bool transportMatrices(const CoinRenderFramePlan &frame,int n,uint64_t budget){
  CoinWgpuFfiFrame ffi;std::string error;
  if(!check(ffi.prepare(frame,side,side,error,false),"actual AA private transport: "+error))return false;
  const auto &v=ffi.getView();
  bool ok=check(v.abi_version==65&&v.struct_size==544&&v.multipass_count==unsigned(n)&&v.multipass_reserved==0&&v.multipass_budget_bytes==budget,"exact AA count, full budget and reserved-zero ABI65 header");
  if(n==1)return check(v.multipass_matrices==nullptr&&v.multipass_matrix_count==0,"one-pass FFI has no jitter table")&&ok;
  const uint64_t floats=uint64_t(n)*v.state_count*16;
  if(!check(v.state_count>0&&v.states&&v.multipass_matrices&&v.multipass_matrix_count==floats,"exact sample-major/state-major owned matrix float count"))return false;
  unsigned bad=0;const auto offsets=samples(n);
  for(int pass=0;pass<n;++pass)for(uint64_t state=0;state<v.state_count;++state){
    const auto &base=v.states[state];
    if(!check(base.viewport[2]>0&&base.viewport[3]>0,"actual occurrence has a positive pixel viewport")){ok=false;continue;}
    const double dx=2*(offsets[pass][0]-.5)/base.viewport[2],dy=2*(offsets[pass][1]-.5)/base.viewport[3];
    const float *actual=v.multipass_matrices+(uint64_t(pass)*v.state_count+state)*16;
    for(unsigned column=0;column<4;++column)for(unsigned row=0;row<4;++row){
      const unsigned i=column*4+row;
      // Clip translation acts on X/W and Y/W after the full MVP. Z and W
      // are unchanged; translation accumulates from the original state for
      // each sample, never from its predecessor.
      const double expected=base.model_view_projection[i]+(row==0?dx:row==1?dy:0)*base.model_view_projection[column*4+3];
      bad+=!std::isfinite(actual[i])||std::abs(actual[i]-expected)>2e-6;
    }
  }
  std::cout<<"multipass_ffi N="<<n<<" states="<<v.state_count<<" matrix_floats="<<v.multipass_matrix_count<<" bad="<<bad<<'\n';
  return check(!bad,"all samples use original per-occurrence MVP plus native clip translation, preserved Z/W")&&ok;
}
bool api() {
  CoinRenderAction action(SbViewportRegion(side,side));
  check(action.getNumPasses()==1&&action.getMultipassBufferBudget()==256ull*1024*1024,"action documented defaults");
  CoinRenderSceneManager manager(SbVec2i32(side,side));
  check(manager.getNumPasses()==1&&manager.getMultipassBufferBudget()==256ull*1024*1024,"scene-manager documented defaults");
  for(int n=-1;n<=17;++n) {
    action.setNumPasses(n);manager.setNumPasses(n);
    check(action.getNumPasses()==n&&manager.getNumPasses()==n,"setters retain exact request without clamping");
  }
  for(int n:{std::numeric_limits<int>::min(),std::numeric_limits<int>::max()}){
    action.setNumPasses(n);manager.setNumPasses(n);
    check(action.getNumPasses()==n&&manager.getNumPasses()==n,"extreme int requests retained without overflow/clamping");
  }
  for(uint64_t budget:{uint64_t(0),uint64_t(1),uint64_t(256)*1024*1024,UINT64_MAX}) {
    action.setMultipassBufferBudget(budget);manager.setMultipassBufferBudget(budget);
    check(action.getMultipassBufferBudget()==budget&&manager.getMultipassBufferBudget()==budget,"budget round trip all bits");
  }
  return good;
}
bool actionPath(bool gpu,const CoinRenderOptions &options,int requested) {
  Scene scene;
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
  Audit *audit=prepare(target.get(),gpu);if(!audit)return false;
  CoinRenderAction action(SbViewportRegion(side,side));
  action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::NONE);
  action.setBackgroundColor(SbColor4f(0,0,0,0));
  bool captured=false;uint64_t capturedRoot=0,capturedBudget=0;int capturedPasses=0;
  auto draw=[&](int n,const char *label) {
    action.setNumPasses(n);const auto serial=target->getLastSubmissionSerial();
    const unsigned captures=scene.captures;const auto revision=scene.root->getNodeId();
    // INITIAL Action preserves a captured plan for the same root revision and
    // action configuration. A node SoCallback does not disable that cache;
    // externally registered action callbacks do. Distinguish exact reuse from
    // a logical capture update instead of forcing traversal to make a counter.
    const bool reuseExact=captured&&capturedRoot==revision&&capturedPasses==n&&capturedBudget==action.getMultipassBufferBudget();
    action.apply(scene.root);
    std::cout<<"multipass_apply "<<label<<" n="<<n<<" status="<<action.getLastStatus()<<" serial="<<serial<<"->"<<target->getLastSubmissionSerial()<<" error="<<action.getLastError().getString()<<'\n';
    bool ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial,"one complete new publication");
    const unsigned expectedCaptures=reuseExact?0:1;
    ok&=check(scene.captures==captures+expectedCaptures&&scene.root->getNodeId()==revision,"one capture per logical update or zero on exact immutable-plan reuse, never N traversals");
    std::cout<<"multipass_capture exact_reuse="<<reuseExact<<" expected="<<expectedCaptures<<" actual="<<scene.captures-captures<<'\n';
    if(action.getLastStatus()==CoinRenderAction::SUCCESS){captured=true;capturedRoot=revision;capturedPasses=n;capturedBudget=action.getMultipassBufferBudget();}
    if(ok){ok&=imageValid(read(target.get()),n,scene.edge,label);ok&=transportMatrices(audit->last,n,action.getMultipassBufferBudget());}
    return ok;
  };
  if(requested)draw(requested,"requested");
  else for(int n=1;n<=16;++n)draw(n,"all native counts");
  draw(2,"stable before refusal");
  draw(2,"exact immutable-plan reuse without authorial update");
  const Image stable=read(target.get());const auto serial=target->getLastSubmissionSerial();
  auto refuse=[&](int n,CoinRenderAction::Status expected,const char *label) {
    action.setNumPasses(n);const unsigned before=audit->commands;action.apply(scene.root);
    check(action.getLastStatus()==expected&&action.getLastError().getLength()>0&&audit->commands==before&&target->getLastSubmissionSerial()==serial&&exact(stable,read(target.get())),std::string(label)+" refuses before work with exact publication preserved");
  };
  refuse(0,CoinRenderAction::INVALID_SCENE,"zero passes");
  refuse(-1,CoinRenderAction::INVALID_SCENE,"negative passes");
  refuse(std::numeric_limits<int>::min(),CoinRenderAction::INVALID_SCENE,"minimum signed count");
  refuse(17,CoinRenderAction::UNSUPPORTED,"finite count capability");
  refuse(std::numeric_limits<int>::max(),CoinRenderAction::UNSUPPORTED,"maximum signed count");
  action.setMultipassBufferBudget(0);
  refuse(2,CoinRenderAction::OUT_OF_MEMORY,"zero scratch budget");
  action.setMultipassBufferBudget(1);
  refuse(2,CoinRenderAction::OUT_OF_MEMORY,"positive one-byte budget cannot hold a64x64 accumulation result");
  action.setMultipassBufferBudget(0);
  draw(1,"zero budget needs no multipass scratch");
  action.setMultipassBufferBudget(256ull*1024*1024);
  scene.move(40.33);draw(3,"authored edge mutation");
  scene.move(32.33);draw(2,"authorial A/B/A recovery");
  action.setRenderTarget(nullptr);
  return good;
}
#endif
bool managerPath(bool gpu,const CoinRenderOptions &options,int n) {
  Scene scene;SoRenderManager manager;
  manager.setSceneGraph(scene.root);manager.setViewportRegion(SbViewportRegion(side,side));
  manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.setRenderMode(SoRenderManager::AS_IS);manager.setStereoMode(SoRenderManager::MONO);
  manager.setBackgroundColor(SbColor4f(0,0,0,0));
  manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::NONE);
  manager.getGLRenderAction()->setNumPasses(n);
  CoinRenderManagerAdapter adapter(manager,SbVec2i32(side,side),options);
  auto *target=adapter.getSceneManager()->getRenderTarget();
  Audit *audit=prepare(target,gpu);if(!audit)return false;
  const unsigned before=scene.captures;const auto revision=scene.root->getNodeId();
  const auto status=adapter.render();
  std::cout<<"multipass_manager requested="<<n<<" status="<<status<<" serial="<<target->getLastSubmissionSerial()<<" error="<<adapter.getLastError().getString()<<'\n';
  if(!check(status==CoinRenderAction::SUCCESS&&scene.captures==before+1&&revision==scene.root->getNodeId(),"manager one current immutable capture"))return false;
#ifdef COIN_MULTIPASS_LEGACY_MANAGER
  // A baseline proof, not a link failure or a claim that this is correct AA.
  // All usual numerical color/depth gates remain; only their expected N is1.
  check(n>1,"legacy control asks for actual multipass");
  imageValid(read(target),1,scene.edge,"baseline manager ignores native request");
  unsigned requestedCoverage=0;for(const auto &j:samples(n))requestedCoverage+=(33-j[0]<scene.edge);
  check(requestedCoverage>0&&requestedCoverage<unsigned(n),"requested multipass would visibly differ from single-pass at column32");
#else
  check(adapter.getSceneManager()->getNumPasses()==n,"manager forwards exact native request");
  imageValid(read(target),n,scene.edge,"manager real multipass");
  const Image stable=read(target);const auto serial=target->getLastSubmissionSerial();
  manager.getGLRenderAction()->setPassUpdate(TRUE);
  const unsigned commands=audit->commands;
  const auto refused=adapter.render();
  if(n>1)check(refused==CoinRenderAction::UNSUPPORTED&&audit->commands==commands&&target->getLastSubmissionSerial()==serial&&exact(stable,read(target)),"native passUpdate refuses without publishing intermediate images");
  else{check(refused==CoinRenderAction::SUCCESS,"one-pass native passUpdate remains legal");imageValid(read(target),1,scene.edge,"one-pass passUpdate retains native behavior");}
  manager.getGLRenderAction()->setPassUpdate(FALSE);
  check(adapter.render()==CoinRenderAction::SUCCESS,"manager recovery after passUpdate refusal");
  imageValid(read(target),n,scene.edge,"manager recovery current result");
#endif
  return good;
}
#ifndef COIN_MULTIPASS_LEGACY_MANAGER
#include "CoinRenderMultipassViewportCases.h"
#include "CoinRenderMultipassMsaaCases.h"
#include "CoinRenderMultipassRttCases.h"
#include "CoinRenderMultipassShadowCases.h"
#include "CoinRenderMultipassTicketCases.h"
#include "CoinRenderMultipassFogClipCases.h"
#include "CoinRenderMultipassRasterCases.h"
#include "CoinRenderMultipassModuleCases.h"
#include "CoinRenderMultipassText2Cases.h"
#include "CoinRenderMultipassTransparencyCases.h"
#include "CoinRenderMultipassBudgetCases.h"
#include "CoinRenderMultipassBumpCases.h"
#include "CoinRenderMultipassLayerCases.h"
#include "CoinRenderMultipassRttShadowCases.h"
#include "CoinRenderMultipassPrecisionCases.h"
#ifdef COIN_MULTIPASS_X11
#include "CoinRenderMultipassWindowCases.h"
#endif
#endif
}
int main(int argc,char **argv) {
  bool gpu=false,manager=false,core=false,viewports=false,window=false,msaa=false,rtt=false,portable=false,direct=false,shadows=false,tickets=false,fogClip=false,raster=false,module=false,text2=false,crop=false,transparency=false,weighted=false,budget=false,bump=false,layers=false,annotation=false,precision=false;int passes=0;unsigned sampleCount=1,maps=1;
  for(int i=1;i<argc;++i) {
    const std::string a=argv[i];
    if(a=="--window"){window=true;gpu=true;}else if(a=="--gpu")gpu=true;else if(a=="--cpu")gpu=false;
    else if(a=="--precision")precision=true;else if(a=="--bump"){bump=true;gpu=true;}else if(a=="--annotations"){layers=true;annotation=true;}else if(a=="--layers")layers=true;else if(a=="--budget"){budget=true;gpu=true;}else if(a=="--weighted"){weighted=true;transparency=true;}else if(a=="--transparency")transparency=true;else if(a=="--crop"){viewports=true;crop=true;}else if(a=="--text2")text2=true;else if(a=="--module"){module=true;gpu=true;}else if(a=="--raster")raster=true;else if(a=="--fog-clip")fogClip=true;else if(a=="--tickets"){tickets=true;gpu=true;}else if(a=="--shadows"){shadows=true;gpu=true;}else if(a=="--maps"&&i+1<argc)maps=unsigned(std::stoi(argv[++i]));else if(a=="--rtt"){rtt=true;gpu=true;}else if(a=="--direct")direct=true;else if(a=="--staged")direct=false;else if(a=="--portable-sampling")portable=true;else if(a=="--msaa")msaa=true;else if(a=="--samples"&&i+1<argc)sampleCount=unsigned(std::stoi(argv[++i]));else if(a=="--viewports")viewports=true;else if(a=="--core")core=true;else if(a=="--manager")manager=true;
    else if(a=="--passes"&&i+1<argc)passes=std::stoi(argv[++i]);else return 2;
  }
  if(passes<0||passes>16||(sampleCount!=1&&sampleCount!=2&&sampleCount!=4))return 2;
  if(maps!=1&&maps!=4&&maps!=8)return 2;
  if(msaa&&(!gpu||sampleCount==1))return 2;
  if(sampleCount>1&&!msaa)return 2;
  #ifdef COIN_MULTIPASS_X11
  if(window)XInitThreads();
  #endif
  if(rtt||module||budget)setenv("COIN_RENDER_TRACE_PHASES","1",1);
  SoDB::init();CoinRenderAction::initClass();
#ifndef COIN_MULTIPASS_LEGACY_MANAGER
  if(core)return api()?0:1;
#endif
  CoinRenderOptions options;options.sampleCount=sampleCount;
  options.textureSamplingPolicy=portable?COIN_RENDER_SAMPLING_PORTABLE:COIN_RENDER_SAMPLING_NATIVE;
  if(weighted)options.transparency=COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;
  options.sceneTexture=direct?COIN_RENDER_SCENE_TEXTURE_DIRECT:COIN_RENDER_SCENE_TEXTURE_STAGED;
  if(gpu) {
    CoinRenderCapabilities caps{};
    if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))||!caps.gpu_available)return 77;
    options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
    std::cout<<"multipass_gpu backend="<<caps.backend<<" renderer="<<caps.renderer<<" adapter="<<caps.adapter_name<<'\n';
  }
#ifdef COIN_MULTIPASS_LEGACY_MANAGER
  return managerPath(gpu,options,passes?passes:4)?0:1;
#else
  if(window) {
#ifdef COIN_MULTIPASS_X11
    return windowPath(options,passes);
#else
    return 77;
#endif
  }
  if(precision)return precisionPath(gpu,options)?0:1;
  if(rtt&&shadows)return rttShadowPath(options,passes)?0:1;
  if(layers)return layerPath(gpu,options,passes,annotation)?0:1;
  if(bump)return bumpPath(options,passes)?0:1;
  if(budget)return resourceBudgetPath(options,shadows)?0:1;
  if(transparency)return transparencyPath(gpu,options,passes)?0:1;
  if(text2)return text2Path(gpu,options,passes)?0:1;
  if(module)return modulePath(options,passes)?0:1;
  if(raster)return rasterPath(gpu,options,passes)?0:1;
  if(fogClip)return fogClipPath(gpu,options,passes)?0:1;
  if(tickets)return ticketPath(options)?0:1;
  if(shadows)return shadowPath(options,passes,maps)?0:1;
  if(rtt)return rttPath(options,passes)?0:1;
  if(msaa)return msaaPath(options,passes)?0:1;
  return (viewports?viewportPath(gpu,options,passes,crop):manager?managerPath(gpu,options,passes?passes:4):actionPath(gpu,options,passes))?0:1;
#endif
}
