#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/SbImage.h>
#include <Inventor/sensors/SoSensorManager.h>
#include <Inventor/misc/SoChildList.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/VRMLnodes/SoVRMLNodes.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMatrixTransform.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <map>
#include <utility>
#include <string>
#include <thread>
#include <vector>
#ifdef COIN_VRML_VISUAL_X11
#include <X11/Xlib.h>
#ifdef Status
#undef Status
#endif
#endif

namespace {
constexpr int side = 96;
bool check(bool pass, const std::string &message) {
  if (!pass) std::cerr << "vrml_visual FAIL " << message << '\n';
  return pass;
}
struct Frame { std::vector<uint8_t> rgba; std::vector<float> depth; };
struct Settings { bool gpu=false; unsigned samples=1; std::string domain="offscreen", scenario="material"; };

// This observes actual backend entry, rather than treating a revision as proof.
class Audit : public CoinRenderBackend {
public:
  std::unique_ptr<CoinRenderBackend> real;
  unsigned submits=0, producers=0;
  explicit Audit(std::unique_ptr<CoinRenderBackend> value):real(std::move(value)){}
  bool isGpuBackend()const override{return real->isGpuBackend();}
  bool initializesCpuDepthBuffer()const override{return real->initializesCpuDepthBuffer();}
  bool resetOnActionDetach()const override{return real->resetOnActionDetach();}
  bool supportsOffscreenShadows()const override{return real->supportsOffscreenShadows();}
  bool supportsOpaqueBump()const override{return real->supportsOpaqueBump();}
  CoinRenderBackendStatus getStatus()const override{return real->getStatus();}
  CoinRenderBackendStatus prepare(CoinRenderTargetP&t)override{return real->prepare(t);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&f,CoinRenderTargetP&t)override{++submits;return real->submit(f,t);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&f,CoinRenderTargetP&t,const CoinRenderFrameReuseDecision&r)override{++submits;return real->submit(f,t,r);}
  CoinRenderSubmitResult submitAsync(const CoinRenderFramePlan&f,CoinRenderTargetP&t,CoinRenderReadbackTicket&ticket,const CoinRenderFrameReuseDecision&r)override{++submits;return real->submitAsync(f,t,ticket,r);}
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan&g,const CoinRenderFramePlan&f,const SbVec2i32&s)const override{return real->preflightRtt(g,f,s);}
  CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan&f,const SbVec2i32&s,uint64_t id,CoinRenderTargetP&t,uint64_t&token)override{++producers;return real->submitRtt(f,s,id,t,token);}
  void finishRtt(const std::vector<uint64_t>&v)override{real->finishRtt(v);}
  void retireRtt(const std::vector<uint64_t>&v)override{real->retireRtt(v);}
  bool stagedRttRequiresPreparedParent()const override{return real->stagedRttRequiresPreparedParent();}
  bool requiresSharedRetirement()const override{return real->requiresSharedRetirement();}
  void retireLostReadbacks()override{real->retireLostReadbacks();}
  CoinRenderDeviceDomain resourceDomain()const override{return real->resourceDomain();}
  bool readbackLoad(uint64_t&a,uint64_t&b)const override{return real->readbackLoad(a,b);}
  void configureMultipassTarget(const CoinRenderTargetP&t)override{real->configureMultipassTarget(t);}
  CoinRenderSubmitResult preflightMultipass(const CoinRenderFramePlan&f,const SbVec2i32&s)const override{return real->preflightMultipass(f,s);}
  void poll()override{real->poll();}
  const std::string&getLastError()const override{return real->getLastError();}
};

struct Scene {
  SoSeparator *root=new SoSeparator;
  SoOrthographicCamera *camera=new SoOrthographicCamera;
  SoVRMLGroup *content=new SoVRMLGroup;
  Scene(){
    root->ref();camera->height=4;camera->position.setValue(0,0,4);
    camera->nearDistance=1;camera->farDistance=9;root->addChild(camera);
    auto *environment=new SoEnvironment;environment->ambientIntensity=0;root->addChild(environment);
    auto *quality=new SoComplexity;quality->textureQuality=.1f;root->addChild(quality);
    root->addChild(content);
  }
  ~Scene(){root->unref();}
  Scene(const Scene&)=delete;
};
SoVRMLMaterial *material(const SbColor &emission=SbColor(0,0,0)) {
  auto *m=new SoVRMLMaterial;m->ambientIntensity=0;m->diffuseColor.setValue(0,0,0);
  m->specularColor.setValue(0,0,0);m->emissiveColor=emission;return m;
}
SoVRMLIndexedFaceSet *plane(float x=0, float y=0, float extent=.8f, bool explicitUV=true, bool triangles=false) {
  auto *mesh=new SoVRMLIndexedFaceSet;
  auto *coordinates=new SoVRMLCoordinate;
  auto *uv=new SoVRMLTextureCoordinate;
  // A grid makes the oracle agree on the native per-vertex light evaluation.
  const int n=8;
  for(int j=0;j<=n;++j)for(int i=0;i<=n;++i){
    coordinates->point.set1Value(j*(n+1)+i,SbVec3f(x-extent+2*extent*i/n,y-extent+2*extent*j/n,0));
    uv->point.set1Value(j*(n+1)+i,SbVec2f(-.25f+2.f*i/n,-.25f+2.f*j/n));
  }
  int k=0;for(int j=0;j<n;++j)for(int i=0;i<n;++i){
    int a=j*(n+1)+i;
    if(triangles){for(int v:{a,a+1,a+n+2,-1,a,a+n+2,a+n+1,-1})mesh->coordIndex.set1Value(k++,v);}
    else for(int v:{a,a+1,a+n+2,a+n+1,-1})mesh->coordIndex.set1Value(k++,v);
  }
  mesh->coord=coordinates;
  if(explicitUV)mesh->texCoord=uv;else{uv->ref();uv->unref();}
  return mesh;
}
SoVRMLShape *shape(SoNode *geometry,SoVRMLMaterial *m=nullptr,SoNode *texture=nullptr,SoVRMLTextureTransform *tx=nullptr) {
  auto *s=new SoVRMLShape;s->geometry=geometry;
  auto *a=new SoVRMLAppearance;a->material=m;a->texture=texture;a->textureTransform=tx;s->appearance=a;return s;
}
SoVRMLDirectionalLight *whiteLight(Scene &s) {
  auto *l=new SoVRMLDirectionalLight;l->direction.setValue(0,0,-1);l->intensity=1;s.content->insertChild(l,0);return l;
}

bool same(const Frame&a,const Frame&b,const std::string&label){return check(a.rgba==b.rgba&&a.depth==b.depth,label+" exact A/B/A color and depth");}
unsigned changes(const Frame&a,const Frame&b,int firstX=0,int lastX=side) {
  if(a.rgba.size()!=b.rgba.size())return 0;unsigned count=0;
  for(int y=0;y<side;++y)for(int x=firstX;x<lastX;++x){size_t p=size_t(y*side+x)*4;bool changed=false;
    for(int c=0;c<3;++c)changed|=std::abs(int(a.rgba[p+c])-int(b.rgba[p+c]))>4;count+=changed;}
  return count;
}
bool uniform(const Frame &f,const std::array<double,3>&rgb,const std::string &label,float expectedDepth=.375f) {
  if(f.rgba.size()!=size_t(side*side*4))return false;unsigned bad=0;
  for(int y=39;y<57;++y)for(int x=39;x<57;++x){size_t p=size_t(y*side+x);
    for(int c=0;c<3;++c)bad+=std::abs(double(f.rgba[4*p+c])-255*rgb[c])>3;
    if(!f.depth.empty())bad+=std::abs(f.depth[p]-expectedDepth)>2e-5f;
  }
  const size_t center=size_t(side/2*side+side/2);
  std::cout<<"vrml_visual analytical="<<label<<" rgb="<<unsigned(f.rgba[4*center])<<','<<unsigned(f.rgba[4*center+1])<<','<<unsigned(f.rgba[4*center+2])
    <<" depth="<<(f.depth.empty()?-1:f.depth[center])<<" expected_depth="<<expectedDepth<<" bad="<<bad<<'\n';
  return check(bad==0,label+" analytical interior RGB and geometric depth");
}
bool compareGL(const Frame &actual,const std::vector<uint8_t>&native,const std::string&label) {
  if(!check(actual.rgba.size()==size_t(side*side*4)&&native.size()==size_t(side*side*3),label+" oracle extents"))return false;
  unsigned stable=0,bad=0;int maximum=0;
  // Compare every smooth native interior, including empty space. Boundary
  // rejection depends only on the independent oracle, never the candidate.
  for(int y=1;y<side-1;++y)for(int x=1;x<side-1;++x){
    size_t p=size_t(y*side+x);bool smooth=true;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)for(int c=0;c<3;++c)
      smooth&=std::abs(int(native[p*3+c])-int(native[size_t((y+dy)*side+x+dx)*3+c]))<=2;
    if(!smooth)continue;++stable;
    for(int c=0;c<3;++c){int d=std::abs(int(actual.rgba[p*4+c])-int(native[p*3+c]));maximum=std::max(maximum,d);bad+=d>4;}
  }
  unsigned covered=0,missing=0;
  for(int y=1;y<side-1;++y)for(int x=1;x<side-1;++x){size_t p=size_t(y*side+x)*3;
    if(!(native[p]||native[p+1]||native[p+2]))continue;++covered;bool found=false;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){bool close=true;size_t q=size_t((y+dy)*side+x+dx)*4;
      for(int c=0;c<3;++c)close&=std::abs(int(native[p+c])-int(actual.rgba[q+c]))<=4;found|=close;}
    missing+=!found;
  }
  if(covered>0&&covered<400&&!check(missing==0,label+" every sparse native color has a candidate pixel"))return false;
  std::cout<<"vrml_visual oracle="<<label<<" stable="<<stable<<" mismatched_channels="<<bad<<" max="<<maximum<<'\n';
  return check(stable>=1000&&bad==0,label+" independent CoinGL color parity");
}

// Compare the current authored snapshot with one native draw. Some native
// private GL caches retain an earlier repeat flag or omit a newly added skybox
// face. A new graph makes the public current fields the independent oracle;
// the candidate graph, target, resources and A/B/A sequence remain persistent.
struct NativeSnapshot {
  SoNode *original=nullptr,*root=nullptr;
  bool valid=false;
  std::map<SoNode*,SoNode*> paired;
  explicit NativeSnapshot(SoNode *source):original(source){
    original->ref();root=original->copy(FALSE);
    if(root){root->ref();valid=copyImages(original,root);}
    if(valid)SoDB::getSensorManager()->processDelayQueue(TRUE);
  }
  ~NativeSnapshot(){if(root)root->unref();original->unref();}
  NativeSnapshot(const NativeSnapshot&)=delete;
  bool copyImages(SoNode *source,SoNode *destination){
    if(!check(source&&destination&&source->getTypeId()==destination->getTypeId(),"native snapshot preserves node types"))return false;
    const auto previous=paired.find(source);
    if(previous!=paired.end())return check(previous->second==destination,"native snapshot preserves shared occurrences");
    paired[source]=destination;
    if(source->isOfType(SoVRMLImageTexture::getClassTypeId())){
      auto *from=static_cast<SoVRMLImageTexture*>(source);
      auto *to=static_cast<SoVRMLImageTexture*>(destination);
      SbVec3s size;int components=0;const auto *pixels=from->getImage()->getValue(size,components);
      if(pixels){
        if(!check(size[0]>0&&size[1]>0&&size[2]==0&&components>=1&&components<=4,"native snapshot decoded 2D image dimensions"))return false;
        const size_t count=size_t(size[0])*size_t(size[1])*size_t(components);
        // Own the byte payload explicitly; never copy/share an SbImage pimpl.
        std::vector<uint8_t> bytes(pixels,pixels+count);SbImage decoded;
        decoded.setValue(size,components,bytes.data());to->setImage(decoded);
      }
    }
    // Full child lists include the hidden SFNode children of Shape/Appearance;
    // no SoPath tail truncation or downcast of a parent Shape is involved.
    const auto *from=source->getChildren();const auto *to=destination->getChildren();
    const int count=from?from->getLength():0;
    if(!check(count==(to?to->getLength():0),"native snapshot preserves full child list"))return false;
    for(int i=0;i<count;++i)if(!copyImages((*from)[i],(*to)[i]))return false;
    return true;
  }
};

struct Harness {
  Settings settings;
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderAction action{SbViewportRegion(side,side)};
  SoOffscreenRenderer native{SbViewportRegion(side,side)};
  Audit *audit=nullptr;
  SoSeparator *consumer=nullptr;SoSceneTexture2 *rtt=nullptr;
#ifdef COIN_VRML_VISUAL_X11
  Display *display=nullptr;Window window=0;
#endif
  explicit Harness(const Settings&s):settings(s){
    CoinRenderOptions options;options.sampleCount=s.samples;
    options.sceneTexture=s.domain=="direct"?COIN_RENDER_SCENE_TEXTURE_DIRECT:COIN_RENDER_SCENE_TEXTURE_STAGED;
#ifdef COIN_VRML_VISUAL_X11
    if(s.domain=="window"){
      display=XOpenDisplay(nullptr);if(!display)return;
      window=XCreateSimpleWindow(display,DefaultRootWindow(display),0,0,side,side,0,0,0);
      XMapWindow(display,window);XSync(display,False);
      CoinRenderNativeSurfaceDescriptor desc{};desc.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;desc.structSize=sizeof(desc);
      desc.type=COIN_RENDER_SURFACE_XLIB;desc.native.xlib.display=display;desc.native.xlib.window=window;
      target.reset(CoinRenderTarget::createWindow(desc,SbVec2i32(side,side),options));
    }else
#endif
    target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
    if(!target)return;
    auto &p=target->getPimpl().get();
    if(s.gpu){if(p.prepareBackend()!=CoinRenderBackendStatus::SUCCESS||!p.backend||!p.backend->isGpuBackend()){std::cerr<<target->getLastError()<<'\n';target.reset();return;}}
    else p.backend.reset(new CoinRenderCpuReferenceBackend);
    audit=new Audit(std::move(p.backend));p.backend.reset(audit);
    if(s.domain!="window")target->setDepthReadbackEnabled(TRUE);
    action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::BLEND);
    action.setBackgroundColor(SbColor4f(0,0,0,1));
    native.setComponents(SoOffscreenRenderer::RGB);native.setBackgroundColor(SbColor(0,0,0));
    native.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
    if(s.domain=="direct"||s.domain=="staged"){
      consumer=new SoSeparator;consumer->ref();auto *cam=new SoOrthographicCamera;
      cam->position.setValue(0,0,4);cam->height=2;cam->nearDistance=1;cam->farDistance=9;consumer->addChild(cam);
      auto *lm=new SoLightModel;lm->model=SoLightModel::BASE_COLOR;consumer->addChild(lm);
      auto *quality=new SoComplexity;quality->textureQuality=.1f;consumer->addChild(quality);
      rtt=new SoSceneTexture2;rtt->size.setValue(side,side);rtt->type=SoSceneTexture2::RGBA8;
      rtt->model=SoSceneTexture2::REPLACE;rtt->transparencyFunction=SoSceneTexture2::NONE;
      rtt->backgroundColor.setValue(0,0,0,1);
      auto *blend=new SoTransparencyType;blend->value=SoGLRenderAction::BLEND;rtt->sceneTransparencyType=blend;consumer->addChild(rtt);
      auto *uv=new SoTextureCoordinate2;const SbVec2f st[]={{0,0},{1,0},{1,1},{0,1}};uv->point.setValues(0,4,st);consumer->addChild(uv);
      auto *coords=new SoCoordinate3;const SbVec3f points[]={{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};coords->point.setValues(0,4,points);consumer->addChild(coords);
      auto *quad=new SoFaceSet;quad->numVertices=4;consumer->addChild(quad);
    }
  }
  ~Harness(){action.setRenderTarget(nullptr);target.reset();if(consumer)consumer->unref();
#ifdef COIN_VRML_VISUAL_X11
    if(window)XDestroyWindow(display,window);if(display)XCloseDisplay(display);
#endif
  }
  SoNode *entry(Scene&s){if(rtt){rtt->scene=s.root;return consumer;}return s.root;}
  bool poll(const CoinRenderReadbackTicket&ticket,Frame&f,const std::string&label){
    auto begin=std::chrono::steady_clock::now();CoinRenderTarget::ReadbackStatus status;SbString diagnostic;
    do{status=CoinRenderTarget::pollReadback(ticket,f.rgba,f.depth,&diagnostic);
      if(status!=CoinRenderTarget::READBACK_NOT_READY)break;
      target->pollDevice();std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }while(status==CoinRenderTarget::READBACK_NOT_READY);
    std::cout<<"vrml_visual poll="<<label<<" status="<<status<<" ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count()<<" diagnostic="<<diagnostic.getString()<<'\n';
    return check(status==CoinRenderTarget::READBACK_READY,label+" asynchronous completion within CTest watchdog");
  }
  bool render(Scene&s,Frame&f,const std::string&label,bool oracle=true){
    if(!check(bool(target),"target exists"))return false;
    // The application services native field sensors once before each frame.
    SoDB::getSensorManager()->processDelayQueue(TRUE);
    uint64_t before=target->getLastSubmissionSerial();
    if(settings.domain=="window"&&!target->requestWindowReadbackRGBA())return false;
    if(settings.domain=="async"){
      CoinRenderReadbackTicket ticket;action.applyAsync(entry(s),ticket);
      if(!check(action.getLastStatus()==CoinRenderAction::SUCCESS,label+" async submit "+action.getLastError().getString())||!poll(ticket,f,label))return false;
    }else{
      action.apply(entry(s));target->readbackRGBA(f.rgba);target->readbackDepth(f.depth);
    }
    std::cout<<"vrml_visual phase="<<label<<" status="<<action.getLastStatus()<<" samples="<<target->getOptions().sampleCount<<" serial="<<target->getLastSubmissionSerial()<<" diagnostic="<<action.getLastError().getString()<<'\n';
    if(!check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>before&&
       target->getOptions().sampleCount==settings.samples&&f.rgba.size()==size_t(side*side*4)&&
       (settings.domain=="window"||f.depth.size()==size_t(side*side)),label+" first application publishes requested profile"))return false;
    if(settings.gpu&&oracle){
      NativeSnapshot snapshot(s.root);
      if(!check(snapshot.valid&&native.render(snapshot.root)&&native.getBuffer(),label+" mandatory fresh native CoinGL snapshot"))return false;
      std::vector<uint8_t> reference(side*side*3);const auto *pixels=native.getBuffer();
      for(int y=0;y<side;++y)std::copy(pixels+(side-1-y)*side*3,pixels+(side-y)*side*3,reference.begin()+y*side*3);
      if(!compareGL(f,reference,label))return false;
    }
    return true;
  }
  bool aba(Scene&s,const std::string&label,const std::function<void()>&change,const std::function<void()>&restore,bool visible=true,bool rightStable=false){
    Frame a,b,back;if(!render(s,a,label+"/A"))return false;change();
    if(!render(s,b,label+"/B"))return false;restore();if(!render(s,back,label+"/A2"))return false;
    bool ok=same(a,back,label);
    ok&=check(visible?changes(a,b)>20:changes(a,b)==0,label+(visible?" visibly changes":" dormant field preserves pixels"));
    if(rightStable)ok&=check(changes(a,b,side/2,side)==0,label+" shape state restored before next occurrence");
    return ok;
  }
};

bool sampleCount(Harness&h){
  if(h.settings.samples==1||h.settings.domain!="offscreen")return true;
  Scene s;auto *rotated=new SoVRMLTransform;rotated->rotation.setValue(SbVec3f(0,0,1),.29f);
  rotated->addChild(shape(plane(),material(SbColor(1,1,1))));s.content->addChild(rotated);Frame f;
  if(!h.render(s,f,"samples/physical-resolve"))return false;
  unsigned partial=0,odd=0,empty=0,full=0,bad=0;const unsigned n=h.settings.samples;
  for(size_t p=0;p<size_t(side*side);++p){unsigned value=f.rgba[4*p];unsigned k=unsigned(std::lround(double(value)*n/255));
    bad+=k>n||std::abs(int(value)-int(std::lround(255.*k/n)))>1;
    bad+=f.rgba[4*p+1]!=value||f.rgba[4*p+2]!=value||f.rgba[4*p+3]!=255;
    partial+=k>0&&k<n;odd+=k>0&&k<n&&(k&1);empty+=k==0;full+=k==n;
    bad+=std::abs(f.depth[p]-(k?.375f:1.f))>2e-5f;
  }
  std::cout<<"vrml_visual physical_samples="<<n<<" partial="<<partial<<" odd="<<odd<<" full="<<full<<" empty="<<empty<<" bad="<<bad<<'\n';
  return check(bad==0&&partial>20&&empty>100&&full>100&&(n!=4||odd>0),"authored slanted edge proves actual requested sample resolve");
}

bool materials(Harness&h){
  Scene s;auto *m=material(SbColor(.2f,.4f,.6f));s.content->addChild(shape(plane(),m));
  Frame f;if(!h.render(s,f,"material/emissive")||!uniform(f,{{.2,.4,.6}},"emissive"))return false;
  if(!h.aba(s,"material/emissive-edit",[&]{m->emissiveColor.setValue(.7f,.1f,.3f);},[&]{m->emissiveColor.setValue(.2f,.4f,.6f);}))return false;
  if(!h.aba(s,"material/transparency",[&]{m->transparency=.5f;},[&]{m->transparency=0;}))return false;
  auto *l=whiteLight(s);m->emissiveColor.setValue(0,0,0);m->diffuseColor.setValue(.8f,.4f,.2f);
  if(!h.render(s,f,"material/diffuse")||!uniform(f,{{.8,.4,.2}},"diffuse"))return false;
  if(!h.aba(s,"material/diffuse-edit",[&]{m->diffuseColor.setValue(.1f,.7f,.3f);},[&]{m->diffuseColor.setValue(.8f,.4f,.2f);}))return false;
  m->specularColor.setValue(.25f,.25f,.25f);l->direction.setValue(-.5f,0,-1);m->shininess=.05f;
  if(!h.aba(s,"material/shininess",[&]{m->shininess=.8f;},[&]{m->shininess=.05f;}))return false;
  Scene pair;whiteLight(pair);auto *left=material(SbColor(.6f,.1f,.2f));auto *right=new SoVRMLMaterial;
  pair.content->addChild(shape(plane(-1,0,.7f),left));pair.content->addChild(shape(plane(1,0,.7f),right));
  if(!h.aba(pair,"material/occurrence-scope",[&]{left->transparency=.7f;left->diffuseColor.setValue(0,1,0);},[&]{left->transparency=0;left->diffuseColor.setValue(0,0,0);},true,true))return false;
  auto *shared=shape(plane(),right);shared->ref();
  Scene defaults;whiteLight(defaults);defaults.content->addChild(shared);
  if(!h.render(defaults,f,"material/VRML-defaults")||!uniform(f,{{.8,.8,.8}},"VRML default diffuse")){shared->unref();return false;}
  shared->appearance=nullptr;if(!h.render(defaults,f,"material/null-appearance")||!uniform(f,{{.8,.8,.8}},"null appearance native default")){shared->unref();return false;}
  shared->appearance=new SoVRMLAppearance;bool ok=h.render(defaults,f,"material/null-material")&&uniform(f,{{.8,.8,.8}},"null material native default");shared->unref();return ok;
}

std::vector<uint8_t> textureBytes(bool alternate=false){std::vector<uint8_t> bytes(4*4*4);for(int y=0;y<4;++y)for(int x=0;x<4;++x){size_t p=size_t(y*4+x)*4;
  bytes[p]=uint8_t(alternate?220-35*x:30+55*x);bytes[p+1]=uint8_t(30+55*y);bytes[p+2]=uint8_t(220-25*x-20*y);bytes[p+3]=uint8_t((x+y)%3==0?128:255);}return bytes;}
// The existing public decoded-image callback supplies deterministic local
// image data to native ImageTexture and Background, independently of codecs.
const std::string localPrefix="/dev/shm/coin-r33-local-fixture-";
SbBool localImage(const SbString&filename,SbImage *image,void*){
  const std::string name=filename.getString();if(name.find(localPrefix)!=0)return FALSE;
  const std::string suffix=name.substr(localPrefix.size());
  if(suffix=="texture-A"||suffix=="texture-B"){
    const auto bytes=textureBytes(suffix=="texture-B");image->setValue(SbVec2s(4,4),4,bytes.data());return TRUE;
  }
  const std::array<std::array<uint8_t,3>,7> colors={{{{220,30,40}},{{20,210,50}},{{30,60,220}},{{220,180,30}},{{150,30,200}},{{20,180,180}},{{80,100,120}}}};
  if(suffix.size()!=1||suffix[0]<'0'||suffix[0]>'6')return FALSE;
  auto color=colors[size_t(suffix[0]-'0')];std::vector<uint8_t> bytes;
  for(int p=0;p<16;++p){bytes.insert(bytes.end(),color.begin(),color.end());bytes.push_back(255);}
  image->setValue(SbVec2s(4,4),4,bytes.data());return TRUE;
}
struct RttImageMutation {
  const std::array<uint8_t,4> before{{192,48,80,255}},after{{32,176,224,255}};
  const std::string url=localPrefix+"between-rtt-occurrences";
  uint8_t *bytes=nullptr;
  unsigned calls=0,earlyCalls=0,mutations=0;
  bool armed=false,badOrder=false;
  explicit RttImageMutation(SoVRMLImageTexture *texture){
    SbVec2s size;int components=0;bytes=texture->getImage()->getValue(size,components);
    if(!bytes||size!=SbVec2s(1,1)||components!=4)bytes=nullptr;
    SbImage::addReadImageCB(read,this);
  }
  ~RttImageMutation(){SbImage::removeReadImageCB(read,this);if(bytes)std::copy(before.begin(),before.end(),bytes);}
  static SbBool read(const SbString&filename,SbImage *image,void *closure){
    auto &self=*static_cast<RttImageMutation*>(closure);
    if(std::string(filename.getString())!=self.url)return FALSE;
    if(!self.armed)++self.earlyCalls;
    else{
      ++self.calls;
      if(!self.mutations){
        self.badOrder=!self.bytes||!std::equal(self.before.begin(),self.before.end(),self.bytes);
        if(self.bytes){std::copy(self.after.begin(),self.after.end(),self.bytes);++self.mutations;}
      }
    }
    const uint8_t black[]={0,0,0,255};image->setValue(SbVec2s(1,1),4,black);return TRUE;
  }
};

bool rttOccurrenceSnapshots(Harness &h){
  if(h.settings.domain!="staged"&&h.settings.domain!="direct")return true;
  Scene producer,consumer;producer.camera->height=2;consumer.camera->height=2;
  auto unlitWhite=[](Scene &scene){
    auto *model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;
    scene.root->insertChild(model,scene.root->findChild(scene.content));
    auto *white=new SoMaterial;white->diffuseColor.setValue(1,1,1);
    scene.root->insertChild(white,scene.root->findChild(scene.content));
  };
  unlitWhite(producer);unlitWhite(consumer);
  auto *image=new SoVRMLImageTexture;const uint8_t first[]={192,48,80,255};image->setImage(SbImage(first,SbVec2s(1,1),4));
  producer.content->addChild(shape(plane(0,0,1),nullptr,image));
  auto *shared=new SoSceneTexture2;shared->scene=producer.root;shared->size.setValue(32,32);shared->type=SoSceneTexture2::RGBA8;
  shared->model=SoSceneTexture2::REPLACE;shared->transparencyFunction=SoSceneTexture2::NONE;
  shared->wrapS=shared->wrapT=SoSceneTexture2::CLAMP;shared->backgroundColor.setValue(0,0,0,1);
  auto panel=[&](float left,float right){
    auto *branch=new SoSeparator;branch->addChild(shared);
    auto *uv=new SoTextureCoordinate2;for(int i=0;i<4;++i)uv->point.set1Value(i,SbVec2f(.5f,.5f));branch->addChild(uv);
    auto *coords=new SoCoordinate3;const SbVec3f corners[]={{left,-.7f,0},{right,-.7f,0},{right,.7f,0},{left,.7f,0}};
    coords->point.setValues(0,4,corners);branch->addChild(coords);auto *quad=new SoFaceSet;quad->numVertices=4;branch->addChild(quad);consumer.content->addChild(branch);
  };
  panel(-.9f,-.1f);
  auto *background=new SoVRMLBackground;
  // Initialize without eagerly decoding before traversal; the local read
  // belongs between the two uses of the same producer during this one apply.
  const SbBool skyNotify=background->skyColor.enableNotify(FALSE),urlNotify=background->frontUrl.enableNotify(FALSE);
  background->skyColor.setValue(0,0,0);background->frontUrl.setValue((localPrefix+"between-rtt-occurrences").c_str());
  background->skyColor.enableNotify(skyNotify);background->frontUrl.enableNotify(urlNotify);consumer.content->addChild(background);
  panel(.1f,.9f);
  RttImageMutation mutation(image);
  if(!check(mutation.bytes!=nullptr,"same-apply RTT has decoded owned RGBA bytes"))return false;
  SoDB::getSensorManager()->processDelayQueue(TRUE);
  const auto imageId=image->getNodeId(),producerId=producer.root->getNodeId(),textureId=shared->getNodeId(),consumerId=consumer.root->getNodeId();
  const unsigned beforeSubmits=h.audit->submits,beforeProducers=h.audit->producers;
  // This graph has its own shared RTT node, so omit the ordinary wrapper.
  // The physical target and its selected STAGED/DIRECT route stay unchanged.
  auto *wrapper=h.rtt;h.rtt=nullptr;mutation.armed=true;Frame frame;
  const bool drawn=h.render(consumer,frame,"RTT/shared-producer-current-bytes-per-occurrence",false);
  mutation.armed=false;h.rtt=wrapper;
  if(!drawn)return false;
  unsigned bad=0,checked=0;int maximum=0;
  for(int which=0;which<2;++which){const auto &expected=which?mutation.after:mutation.before;
    for(int y=40;y<56;++y)for(int x=16+48*which;x<32+48*which;++x){const size_t p=size_t(y*side+x);
      for(int c=0;c<3;++c){const int delta=std::abs(int(frame.rgba[4*p+c])-int(expected[c]));maximum=std::max(maximum,delta);bad+=delta>3;}
      bad+=frame.rgba[4*p+3]!=255;bad+=std::abs(frame.depth[p]-.375f)>2e-5f;++checked;
    }
  }
  const bool ids=image->getNodeId()==imageId&&producer.root->getNodeId()==producerId&&shared->getNodeId()==textureId&&consumer.root->getNodeId()==consumerId;
  std::cout<<"vrml_visual rtt_occurrences route="<<h.settings.domain<<" callback_calls="<<mutation.calls<<" early_calls="<<mutation.earlyCalls<<" mutations="<<mutation.mutations
    <<" unchanged_node_ids="<<ids<<" root_submits="<<h.audit->submits-beforeSubmits<<" direct_producers="<<h.audit->producers-beforeProducers<<" checked="<<checked<<" bad="<<bad<<" max="<<maximum<<'\n';
  return check(mutation.calls>0&&mutation.earlyCalls==0&&mutation.mutations==1&&!mutation.badOrder&&ids&&h.audit->submits==beforeSubmits+1&&checked==512&&bad==0,
    "one apply snapshots A then B from the same RTT producer without notifications, preserving the first panel");
}

bool textures(Harness&h){
  for(bool image:{false,true})for(bool explicitUV:{false,true}){
    Scene s;whiteLight(s);auto *m=material();m->diffuseColor.setValue(1,1,1);
    auto *pixel=image?nullptr:new SoVRMLPixelTexture;auto *decoded=image?new SoVRMLImageTexture:nullptr;
    SoVRMLTexture *texture=image?static_cast<SoVRMLTexture*>(decoded):pixel;
    auto bytes=textureBytes();auto paint=[&](const std::vector<uint8_t>&b){if(pixel)pixel->image.setValue(SbVec2s(4,4),4,b.data());else decoded->setImage(SbImage(b.data(),SbVec2s(4,4),4));};paint(bytes);
    auto *tx=new SoVRMLTextureTransform;auto *mesh=plane(-1,0,.7f,explicitUV);
    s.content->addChild(shape(mesh,m,texture,tx));auto *plain=material(SbColor(.2f,.7f,.4f));s.content->addChild(shape(plane(1,0,.7f),plain));
    const std::string label=std::string(image?"ImageTexture":"PixelTexture")+(explicitUV?"/explicit":"/generated");
    if(!h.aba(s,label+"/bytes",[&]{paint(textureBytes(true));},[&]{paint(bytes);},true,true))return false;
    if(decoded){
      SbVec2s size;int components=0;auto *inPlace=decoded->getImage()->getValue(size,components);
      if(!check(inPlace&&size==SbVec2s(4,4)&&components==4,"in-place image has complete RGBA bytes"))return false;
      const auto imageRevision=decoded->getNodeId(),rootRevision=s.root->getNodeId();const auto alternate=textureBytes(true);
      if(!h.aba(s,label+"/bytes-in-place-no-notification",[&]{std::copy(alternate.begin(),alternate.end(),inPlace);},[&]{std::copy(bytes.begin(),bytes.end(),inPlace);},true,true))return false;
      if(!check(decoded->getNodeId()==imageRevision&&s.root->getNodeId()==rootRevision,"in-place A/B/A preserves node revisions without setImage or touch"))return false;
    }
    if(!h.aba(s,label+"/repeatS",[&]{texture->repeatS=FALSE;},[&]{texture->repeatS=TRUE;},explicitUV,true))return false;
    if(!h.aba(s,label+"/repeatT",[&]{texture->repeatT=FALSE;},[&]{texture->repeatT=TRUE;},explicitUV,true))return false;
    if(!h.aba(s,label+"/translation",[&]{tx->translation.setValue(.37f,.13f);},[&]{tx->translation.setValue(0,0);},true,true))return false;
    tx->center.setValue(.2f,.6f);tx->scale.setValue(.7f,1.4f);
    if(!h.aba(s,label+"/rotation-center-scale",[&]{tx->rotation=.65f;},[&]{tx->rotation=0;},true,true))return false;
    if(!h.aba(s,label+"/center",[&]{tx->center.setValue(.7f,.1f);},[&]{tx->center.setValue(.2f,.6f);},true,true))return false;
  }
  for(bool image:{false,true}){
    Scene analytic;whiteLight(analytic);auto *m=material();m->diffuseColor.setValue(.5f,.75f,.25f);
    const uint8_t texel[]={128,64,192,128};SoVRMLTexture *texture;
    if(image){auto *t=new SoVRMLImageTexture;t->setImage(SbImage(texel,SbVec2s(1,1),4));texture=t;}
    else{auto *t=new SoVRMLPixelTexture;t->image.setValue(SbVec2s(1,1),4,texel);texture=t;}
    analytic.content->addChild(shape(plane(),m,texture));Frame f;
    const double alpha=128./255.;
    if(!h.render(analytic,f,image?"ImageTexture/analytic-RGBA":"PixelTexture/analytic-RGBA")||
       !uniform(f,{{.5*(128./255)*alpha,.75*(64./255)*alpha,.25*(192./255)*alpha}},"native MODULATE color and texture alpha",.375f))return false;
  }
  Scene shared;whiteLight(shared);auto *sharedMaterial=material();sharedMaterial->diffuseColor.setValue(1,1,1);
  auto *sharedTexture=new SoVRMLPixelTexture;auto bytes=textureBytes();sharedTexture->image.setValue(SbVec2s(4,4),4,bytes.data());
  shared.content->addChild(shape(plane(-1,0,.7f),sharedMaterial,sharedTexture));shared.content->addChild(shape(plane(1,0,.7f),sharedMaterial,sharedTexture));
  Frame a,b,back;if(!h.render(shared,a,"texture/shared-A"))return false;auto altered=textureBytes(true);sharedTexture->image.setValue(SbVec2s(4,4),4,altered.data());
  if(!h.render(shared,b,"texture/shared-B"))return false;sharedTexture->image.setValue(SbVec2s(4,4),4,bytes.data());
  if(!h.render(shared,back,"texture/shared-A2")||!same(a,back,"shared texture")||!check(changes(a,b,0,side/2)>20&&changes(a,b,side/2,side)>20,"both reused texture occurrences update"))return false;
  Scene local;whiteLight(local);auto *m=material();m->diffuseColor.setValue(1,1,1);
  auto *texture=new SoVRMLImageTexture;texture->url=(localPrefix+"texture-A").c_str();
  local.content->addChild(shape(plane(),m,texture));
  if(!h.aba(local,"ImageTexture/local-library-URL",[&]{texture->url=(localPrefix+"texture-B").c_str();},[&]{texture->url=(localPrefix+"texture-A").c_str();}))return false;
  return rttOccurrenceSnapshots(h);
}

bool lights(Harness&h){
  for(int kind=0;kind<3;++kind){
    Scene s;auto *m=material();m->diffuseColor.setValue(.8f,.6f,.4f);m->ambientIntensity=.3f;
    auto *transform=new SoVRMLTransform;auto *directional=kind==0?new SoVRMLDirectionalLight:nullptr;
    auto *point=kind==1?new SoVRMLPointLight:nullptr;auto *spot=kind==2?new SoVRMLSpotLight:nullptr;
    SoVRMLLight *light=directional?static_cast<SoVRMLLight*>(directional):point?static_cast<SoVRMLLight*>(point):spot;
    if(point)point->location.setValue(0,0,2);if(spot){spot->location.setValue(0,0,2);spot->direction.setValue(0,0,-1);spot->cutOffAngle=.7f;}
    light->color.setValue(.8f,.6f,1);light->intensity=.8f;transform->addChild(light);s.content->addChild(transform);
    // GL_QUADS does not prescribe a diagonal for non-affine Gouraud colors.
    // Authored Point/Spot triangles make the interpolation contract explicit.
    auto *receiver=new SoSeparator;auto *inverse=new SoMatrixTransform;receiver->addChild(inverse);receiver->addChild(shape(plane(0,0,.8f,true,kind!=0),m));transform->addChild(receiver);
    auto place=[&](float angle,const SbVec3f&translation,const SbVec3f&scale){
      transform->rotation.setValue(SbVec3f(0,1,0),angle);transform->translation=translation;transform->scale=scale;
      SbMatrix matrix;matrix.setTransform(translation,transform->rotation.getValue(),scale);inverse->matrix=matrix.inverse();
    };
    std::string label="light/"+std::to_string(kind);
    if(directional){Frame f;if(!h.render(s,f,label+"/analytical")||!uniform(f,{{.512,.288,.32}},"directional intensity times diffuse times color"))return false;}
    if(!h.aba(s,label+"/on",[&]{light->on=FALSE;},[&]{light->on=TRUE;}))return false;
    if(!h.aba(s,label+"/intensity",[&]{light->intensity=.2f;},[&]{light->intensity=.8f;}))return false;
    if(!h.aba(s,label+"/color",[&]{light->color.setValue(.2f,1,.3f);},[&]{light->color.setValue(.8f,.6f,1);} ))return false;
    if(!h.aba(s,label+"/ambient",[&]{light->ambientIntensity=.4f;},[&]{light->ambientIntensity=0;}))return false;
    Frame diffuse,ambient,back;
    if(!h.render(s,diffuse,label+"/ambient-independent-A"))return false;
    light->intensity=0;light->ambientIntensity=1;
    if(!h.render(s,ambient,label+"/ambient-only-zero-intensity")||!uniform(ambient,{{.192,.108,.12}},"light ambient is independent of diffuse intensity"))return false;
    light->intensity=.8f;light->ambientIntensity=0;
    if(!h.render(s,back,label+"/ambient-independent-A2")||!same(diffuse,back,label+" ambient independence A/B/A")||!check(changes(diffuse,ambient)>20,label+" ambient-only witness is observable"))return false;
    if(!h.aba(s,label+"/transformed",[&]{place(.7f,SbVec3f(.4f,0,0),SbVec3f(1,1,1));},[&]{place(0,SbVec3f(0,0,0),SbVec3f(1,1,1));} ))return false;
    if(point&&!h.aba(s,label+"/native-unused-radius",[&]{point->radius=std::numeric_limits<float>::quiet_NaN();},[&]{point->radius=100;},false))return false;
    if(point&&!h.aba(s,label+"/attenuation",[&]{point->attenuation.setValue(1,.4f,.3f);},[&]{point->attenuation.setValue(1,0,0);} ))return false;
    if(spot){
      if(!h.aba(s,label+"/cutoff",[&]{spot->cutOffAngle=.12f;},[&]{spot->cutOffAngle=.7f;} ))return false;
      if(!h.aba(s,label+"/attenuation",[&]{spot->attenuation.setValue(1,.3f,.2f);},[&]{spot->attenuation.setValue(1,0,0);} ))return false;
      spot->direction.setValue(.28f,.17f,-1);spot->cutOffAngle=.4f;
      if(!h.aba(s,label+"/inclined-direction-nonuniform-scale",[&]{place(.23f,SbVec3f(.2f,-.1f,0),SbVec3f(1.8f,.6f,.8f));},[&]{place(0,SbVec3f(0,0,0),SbVec3f(1,1,1));} ))return false;
      spot->direction.setValue(0,0,-1);spot->cutOffAngle=.7f;
      if(!h.aba(s,label+"/native-unused-beam-radius",[&]{spot->beamWidth=std::numeric_limits<float>::quiet_NaN();spot->radius=std::numeric_limits<float>::quiet_NaN();},[&]{spot->beamWidth=1.5707963f;spot->radius=100;},false))return false;
    }
  }
  return true;
}

bool fog(Harness&h){
  Scene s;auto *fogNode=new SoVRMLFog;fogNode->color.setValue(.1f,.2f,.9f);fogNode->visibilityRange=8;s.content->addChild(fogNode);
  s.content->addChild(shape(plane(),material(SbColor(.8f,.4f,.2f))));Frame f;
  if(!h.render(s,f,"fog/LINEAR")||!uniform(f,{{.45,.3,.55}},"native linear fog"))return false;
  if(!h.aba(s,"fog/range",[&]{fogNode->visibilityRange=4;},[&]{fogNode->visibilityRange=8;}))return false;
  if(!h.aba(s,"fog/color",[&]{fogNode->color.setValue(.8f,.1f,.1f);},[&]{fogNode->color.setValue(.1f,.2f,.9f);} ))return false;
  if(!h.aba(s,"fog/type",[&]{fogNode->fogType="EXPONENTIAL";},[&]{fogNode->fogType="LINEAR";}))return false;
  fogNode->fogType="EXPONENTIAL";double factor=std::exp(-std::pow(2.35*4/8,2));
  if(!h.render(s,f,"fog/native-EXPONENTIAL")||!uniform(f,{{.1+.7*factor,.2+.2*factor,.9-.7*factor}},"native EXP2 fog"))return false;
  Scene scoped;auto *group=new SoSeparator;auto *localFog=new SoVRMLFog;localFog->visibilityRange=8;localFog->color.setValue(.1f,.2f,.9f);
  group->addChild(localFog);group->addChild(shape(plane(-1,0,.7f),material(SbColor(.8f,.4f,.2f))));scoped.content->addChild(group);
  scoped.content->addChild(shape(plane(1,0,.7f),material(SbColor(.2f,.7f,.4f))));
  if(!h.aba(scoped,"fog/separator-scope",[&]{localFog->color.setValue(.8f,.1f,.1f);},[&]{localFog->color.setValue(.1f,.2f,.9f);},true,true))return false;
  // A zero VRML visibilityRange uses the native camera far plane; it does not
  // invent a viewer binding or disable fog behind the application's back.
  return h.aba(s,"fog/zero-native-range",[&]{fogNode->visibilityRange=0;},[&]{fogNode->visibilityRange=8;});
}

bool background(Harness&h){
  Scene s;auto *transform=new SoVRMLTransform;auto *bg=new SoVRMLBackground;transform->addChild(bg);s.content->addChild(transform);
  bg->skyColor.setValue(.1f,.2f,.6f);s.content->addChild(shape(plane(),material(SbColor(.8f,.1f,.2f))));
  Frame f;if(!h.render(s,f,"background/single-sky")||!uniform(f,{{.8,.1,.2}},"background keeps foreground"))return false;
  Frame early,late;if(!h.render(s,early,"background/before-geometry"))return false;
  transform->ref();s.content->removeChild(transform);s.content->addChild(transform);transform->unref();
  if(!h.render(s,late,"background/after-geometry")||!same(early,late,"background preserves geometry irrespective of traversal order"))return false;
  if(!h.aba(s,"background/sky",[&]{bg->skyColor.setValue(.7f,.3f,.1f);},[&]{bg->skyColor.setValue(.1f,.2f,.6f);} ))return false;
  if(!h.aba(s,"background/ignored-translation-scale",[&]{transform->translation.setValue(90,-30,50);transform->scale.setValue(4,.2f,3);},[&]{transform->translation.setValue(0,0,0);transform->scale.setValue(1,1,1);},false))return false;
  const SbColor sky[]={{.9f,.1f,.1f},{.1f,.8f,.2f},{.1f,.2f,.9f}};const float angles[]={1.1f,2.2f};
  bg->skyColor.setValues(0,3,sky);bg->skyAngle.setValues(0,2,angles);
  const SbColor ground[]={{.6f,.2f,.7f},{.2f,.7f,.7f}};bg->groundColor.setValues(0,2,ground);bg->groundAngle.set1Value(0,1.3f);
  if(!h.aba(s,"background/ancestor-rotation",[&]{transform->rotation.setValue(SbVec3f(1,0,0),.8f);},[&]{transform->rotation.setValue(SbVec3f(1,0,0),0);} ))return false;
  if(!h.aba(s,"background/camera-orientation",[&]{s.camera->orientation.setValue(SbVec3f(1,0,0),.35f);},[&]{s.camera->orientation.setValue(SbVec3f(1,0,0),0);} ))return false;
  if(!h.aba(s,"background/gradient-edit",[&]{bg->skyColor.set1Value(1,SbColor(.7f,.2f,.8f));},[&]{bg->skyColor.set1Value(1,sky[1]);}))return false;
  SoMFString *faces[]={&bg->frontUrl,&bg->backUrl,&bg->leftUrl,&bg->rightUrl,&bg->topUrl,&bg->bottomUrl};
  for(int i=0;i<6;++i)faces[i]->setValue((localPrefix+std::to_string(i)).c_str());
  const SbVec3f axes[]={{0,1,0},{0,1,0},{0,1,0},{0,1,0},{1,0,0},{1,0,0}};
  const float angle[]={0,3.14159265f,1.57079633f,-1.57079633f,1.57079633f,-1.57079633f};
  for(int i=0;i<6;++i){s.camera->orientation.setValue(axes[i],angle[i]);
    if(!h.aba(s,"background/local-face-"+std::to_string(i),[&]{faces[i]->setValue((localPrefix+"6").c_str());},[&]{faces[i]->setValue((localPrefix+std::to_string(i)).c_str());}))return false;
  }
  return true;
}

bool geometry(Harness&h){
  // Public geometry, explicit authored attribute arrays and native group paths.
  const char *fixtures[]={
    "Shape { geometry Box { size 1.4 1.2 0.5 } }",
    "Shape { geometry Cone { bottomRadius .7 height 1.4 } }",
    "Shape { geometry Cylinder { radius .7 height 1.4 } }",
    "Shape { geometry Sphere { radius .7 } }",
    "Shape { geometry IndexedFaceSet { coord Coordinate { point [-.8 -.8 0,.8 -.8 0,.8 -.2 0,-.2 -.2 0,-.2 .8 0,-.8 .8 0] } coordIndex [0,1,2,3,4,5,-1] color Color { color [.8 .2 .1,.1 .8 .2,.2 .1 .8] } colorPerVertex FALSE colorIndex [1] normal Normal { vector [0 0 1] } normalPerVertex FALSE normalIndex [0] } }",
    "Shape { geometry IndexedLineSet { coord Coordinate { point [-.8 -.8 0,.8 .8 0,-.8 .8 0,.8 -.8 0] } coordIndex [0,1,-1,2,3,-1] color Color { color [1 .2 .1,.1 1 .2] } colorPerVertex FALSE colorIndex [0,1] } }",
    "Shape { geometry PointSet { coord Coordinate { point [-.5 -.5 0,.5 .5 0,0 0 0] } color Color { color [1 0 0,0 1 0,0 0 1] } } }",
    "Shape { geometry ElevationGrid { xDimension 3 zDimension 3 xSpacing .5 zSpacing .5 height [0,.1,0,.1,.4,.1,0,.1,0] solid FALSE } }",
    "Shape { geometry Extrusion { crossSection [-.4 -.4,.4 -.4,.4 .4,-.4 .4,-.4 -.4] spine [0 -.6 0,0 .6 0] } }",
    "Shape { geometry Text { string [\"R33\"] fontStyle FontStyle { size .7 justify [\"MIDDLE\",\"MIDDLE\"] } } }",
    "Transform { translation -.3 .2 0 rotation 0 0 1 .2 children [Collision { children [Anchor { children [Group { children [Shape { geometry Box { size 1 1 .5 } }] }] }] }] }",
    "Billboard { axisOfRotation 0 1 0 children [Shape { geometry Box { size 1.2 .8 .3 } }] }",
    "Switch { whichChoice 1 choice [Shape { geometry Box { size .2 .2 .2 } }, Shape { geometry Sphere { radius .7 } }] }",
    "LOD { range [5] level [Shape { geometry Box { size 1.2 1.2 .4 } }, Shape { geometry Sphere { radius .3 } }] }"
  };
  for(unsigned n=0;n<sizeof(fixtures)/sizeof(fixtures[0]);++n){Scene s;whiteLight(s);
    std::string input="#VRML V2.0 utf8\n";input+=fixtures[n];SoInput parser;parser.setBuffer(input.data(),input.size());
    SoNode *node=nullptr;if(!check(SoDB::read(&parser,node)&&node,"geometry fixture parser"))return false;s.content->addChild(node);
    Frame f;if(!h.render(s,f,"geometry/"+std::to_string(n)))return false;
    // The sparse line/point cases are checked against GL without imposing a
    // minimum area. Their coordinates and native primitive identity stay public.
    if(!check([&]{for(size_t p=0;p<f.rgba.size();p+=4)if(f.rgba[p]||f.rgba[p+1]||f.rgba[p+2])return true;return false;}(),"geometry has visible authored output"))return false;
  }
  Scene gridScene;auto *grid=new SoVRMLElevationGrid;grid->xDimension=3;grid->zDimension=3;grid->xSpacing=.5f;grid->zSpacing=.5f;grid->solid=FALSE;
  const float heights[]={0,.1f,0,.1f,.4f,.1f,0,.1f,0};grid->height.setValues(0,9,heights);
  auto *gridTransform=new SoVRMLTransform;gridTransform->translation.setValue(-.5f,-.3f,0);gridTransform->rotation.setValue(SbVec3f(1,0,0),.7f);
  gridTransform->addChild(shape(grid,material(SbColor(.3f,.6f,.2f))));gridScene.content->addChild(gridTransform);
  Frame finite,tail,restored;
  if(!h.render(gridScene,finite,"geometry/ElevationGrid-nine-active-heights"))return false;
  if(!check(changes(finite,Frame{std::vector<uint8_t>(finite.rgba.size(),0),{}})>20,"ElevationGrid active geometry has visible output"))return false;
  grid->height.set1Value(9,.2f);grid->height.set1Value(10,std::numeric_limits<float>::quiet_NaN());
  if(!h.render(gridScene,tail,"geometry/ElevationGrid-unused-finite-and-NaN-tail")||!same(finite,tail,"unused height tail preserves RGB and depth"))return false;
  grid->height.setNum(9);
  if(!h.render(gridScene,restored,"geometry/ElevationGrid-tail-restored")||!same(finite,restored,"height tail A/B/A"))return false;
  const auto serial=h.target->getLastSubmissionSerial();const auto submissions=h.audit->submits,producers=h.audit->producers;
  size_t borrowedSize=0;const auto *borrowed=h.target->borrowRGBA(borrowedSize);
  grid->height.setNum(8);h.action.apply(h.entry(gridScene));Frame rejected;h.target->readbackRGBA(rejected.rgba);h.target->readbackDepth(rejected.depth);
  size_t retainedSize=0;const auto *retained=h.target->borrowRGBA(retainedSize);
  if(!check(h.action.getLastStatus()!=CoinRenderAction::SUCCESS&&h.action.getLastError().getLength()>0&&h.target->getLastSubmissionSerial()==serial&&h.audit->submits==submissions&&h.audit->producers==producers&&borrowed==retained&&borrowedSize==retainedSize,"short consumed height array refuses before submission/RTT and preserves serial/borrowed buffer")||!same(restored,rejected,"short height array preserves publication"))return false;
  grid->height.setValues(0,9,heights);
  Frame recovered;if(!h.render(gridScene,recovered,"geometry/ElevationGrid-short-array-recovered")||!same(finite,recovered,"short height array recovery"))return false;
  Scene s;auto *inlined=new SoVRMLInline;inlined->setChildData(shape(plane(),material(SbColor(.3f,.6f,.2f))));s.content->addChild(inlined);
  Frame f;return h.render(s,f,"geometry/already-loaded-inline")&&uniform(f,{{.3,.6,.2}},"loaded Inline");
}

bool passive(Harness&h){
  Scene s;s.content->addChild(shape(plane(),material(SbColor(.2f,.4f,.6f))));
  auto *viewer=new SoVRMLViewpoint;auto *navigation=new SoVRMLNavigationInfo;
  s.content->addChild(viewer);s.content->addChild(navigation);
  s.content->addChild(new SoVRMLWorldInfo);s.content->addChild(new SoVRMLTouchSensor);
  s.content->addChild(new SoVRMLPlaneSensor);s.content->addChild(new SoVRMLCylinderSensor);
  s.content->addChild(new SoVRMLSphereSensor);s.content->addChild(new SoVRMLVisibilitySensor);
  s.content->addChild(new SoVRMLProximitySensor);auto *clock=new SoVRMLTimeSensor;clock->enabled=FALSE;s.content->addChild(clock);
  s.content->addChild(new SoVRMLPositionInterpolator);s.content->addChild(new SoVRMLOrientationInterpolator);
  s.content->addChild(new SoVRMLColorInterpolator);s.content->addChild(new SoVRMLNormalInterpolator);
  s.content->addChild(new SoVRMLCoordinateInterpolator);s.content->addChild(new SoVRMLScalarInterpolator);
  s.content->addChild(new SoVRMLScript);s.content->addChild(new SoVRMLAudioClip);s.content->addChild(new SoVRMLSound);
  s.content->addChild(new SoVRMLMovieTexture);
  Frame f;if(!h.render(s,f,"passive/static-snapshot")||!uniform(f,{{.2,.4,.6}},"passive nodes have no direct raster output"))return false;
  return h.aba(s,"passive/application-camera-contract",[&]{viewer->position.setValue(100,100,100);viewer->fieldOfView=.1f;navigation->headlight=FALSE;navigation->visibilityLimit=.1f;},
    [&]{viewer->position.setValue(0,0,10);viewer->fieldOfView=.785398f;navigation->headlight=TRUE;navigation->visibilityLimit=0;},false);
}

bool invalid(Harness&h){
  Scene s;auto *m=material(SbColor(.2f,.4f,.6f));auto *mesh=plane();s.content->addChild(shape(mesh,m));
  auto *coords=static_cast<SoVRMLCoordinate*>(mesh->coord.getValue());const float nan=std::numeric_limits<float>::quiet_NaN();
  auto reject=[&](const std::string&label,const std::function<void()>&breakIt,const std::function<void()>&repair){
    Frame good;if(!h.render(s,good,label+"/valid"))return false;
    uint64_t serial=h.target->getLastSubmissionSerial();unsigned submits=h.audit->submits,producers=h.audit->producers;
    breakIt();h.action.apply(h.entry(s));Frame after;h.target->readbackRGBA(after.rgba);h.target->readbackDepth(after.depth);
    bool ok=check(h.action.getLastStatus()!=CoinRenderAction::SUCCESS&&h.action.getLastError().getLength()>0&&
      h.target->getLastSubmissionSerial()==serial&&h.audit->submits==submits&&h.audit->producers==producers,label+" diagnostic refusal before backend/producers");
    ok&=same(good,after,label+" last publication retained");repair();Frame recovered;
    return h.render(s,recovered,label+"/recovered")&&same(good,recovered,label+" recovery")&&ok;
  };
  if(!reject("invalid/emissive",[&]{m->emissiveColor.setValue(nan,.4f,.6f);},[&]{m->emissiveColor.setValue(.2f,.4f,.6f);} ))return false;
  const auto old=coords->point[0];
  if(!reject("invalid/coordinate",[&]{coords->point.set1Value(0,SbVec3f(nan,0,0));},[&]{coords->point.set1Value(0,old);} ))return false;
  if(!reject("invalid/coordIndex",[&]{mesh->coordIndex.set1Value(0,1000000);},[&]{mesh->coordIndex.set1Value(0,0);} ))return false;
  auto *uv=static_cast<SoVRMLTextureCoordinate*>(mesh->texCoord.getValue());auto oldUV=uv->point[0];
  auto *texture=new SoVRMLPixelTexture;const uint8_t white[]={255,255,255,255};texture->image.setValue(SbVec2s(1,1),4,white);
  auto *appearance=static_cast<SoVRMLAppearance*>(static_cast<SoVRMLShape*>(s.content->getChild(0))->appearance.getValue());appearance->texture=texture;
  auto *tx=new SoVRMLTextureTransform;appearance->textureTransform=tx;
  if(!reject("invalid/consumed-UV",[&]{uv->point.set1Value(0,SbVec2f(nan,0));},[&]{uv->point.set1Value(0,oldUV);} ))return false;
  if(!reject("invalid/texture-transform",[&]{tx->rotation=nan;},[&]{tx->rotation=0;} ))return false;
  appearance->texture=nullptr;uv->point.set1Value(0,SbVec2f(nan,0));
  Frame idleUV;if(!h.render(s,idleUV,"invalid/unused-UV-without-texture"))return false;uv->point.set1Value(0,oldUV);
  auto *inactive=new SoVRMLSwitch;inactive->whichChoice=0;inactive->addChoice(new SoVRMLGroup);
  auto *badMesh=plane();badMesh->coordIndex.set1Value(0,1000000);inactive->addChoice(shape(badMesh,m));s.content->addChild(inactive);
  Frame inactiveFrame;if(!h.render(s,inactiveFrame,"invalid/unused-Switch-branch"))return false;
  if(!reject("invalid/consumed-Switch-branch",[&]{inactive->whichChoice=1;},[&]{inactive->whichChoice=0;} ))return false;
  auto *normal=new SoVRMLNormal;normal->vector.set1Value(0,SbVec3f(0,0,1));mesh->normal=normal;mesh->normalPerVertex=FALSE;
  for(int i=0;i<64;++i)mesh->normalIndex.set1Value(i,0);
  if(!reject("invalid/normal-index",[&]{mesh->normalIndex.set1Value(0,1000000);},[&]{mesh->normalIndex.set1Value(0,0);} ))return false;
  auto *light=new SoVRMLSpotLight;light->on=FALSE;light->direction.setValue(nan,0,0);s.content->insertChild(light,0);
  Frame valid;if(!h.render(s,valid,"invalid/unused-disabled-light"))return false;
  light->direction.setValue(0,0,-1);light->location.setValue(0,0,2);light->on=TRUE;
  if(!reject("invalid/consumed-light",[&]{light->direction.setValue(nan,0,-1);},[&]{light->direction.setValue(0,0,-1);} ))return false;
  auto *fogNode=new SoVRMLFog;fogNode->visibilityRange=8;fogNode->color.setValue(.1f,.2f,.9f);s.content->insertChild(fogNode,0);
  if(!reject("invalid/fog-range",[&]{fogNode->visibilityRange=nan;},[&]{fogNode->visibilityRange=8;} ))return false;
  if(!reject("invalid/fog-color",[&]{fogNode->color.setValue(nan,.2f,.9f);},[&]{fogNode->color.setValue(.1f,.2f,.9f);} ))return false;
  auto *background=new SoVRMLBackground;background->skyColor.setValue(.1f,.2f,.3f);s.content->addChild(background);
  if(!reject("invalid/background-color",[&]{background->skyColor.setValue(nan,.2f,.3f);},[&]{background->skyColor.setValue(.1f,.2f,.3f);} ))return false;
  if(h.settings.gpu&&h.settings.domain=="offscreen"){
    Frame stable;if(!h.render(s,stable,"invalid/live-ticket-control"))return false;
    CoinRenderReadbackTicket live;h.action.applyAsync(h.entry(s),live);
    if(!check(h.action.getLastStatus()==CoinRenderAction::SUCCESS&&live.token,"live ticket before invalid candidate"))return false;
    auto serial=h.target->getLastSubmissionSerial();auto calls=h.audit->submits;m->transparency=nan;
    h.action.apply(h.entry(s));m->transparency=0;
    if(!check(h.action.getLastStatus()!=CoinRenderAction::SUCCESS&&h.action.getLastError().getLength()>0&&h.target->getLastSubmissionSerial()==serial&&h.audit->submits==calls,"invalid keeps live async serial and refuses submission"))return false;
    Frame ticket;if(!h.poll(live,ticket,"invalid/live-ticket")||!same(stable,ticket,"live ticket"))return false;
    Frame recovered;if(!h.render(s,recovered,"invalid/live-ticket-recovered")||!same(stable,recovered,"async recovery"))return false;
  }
  return true;
}
}
int main(int argc,char **argv){
#ifdef COIN_VRML_VISUAL_X11
  if(!XInitThreads()){std::cerr<<"XInitThreads failed\n";return 1;}
#endif
  Settings settings;
  for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--gpu")settings.gpu=true;else if(arg=="--cpu")settings.gpu=false;
    else if((arg=="--scenario"||arg=="--domain"||arg=="--samples")&&i+1<argc){std::string value=argv[++i];if(arg=="--scenario")settings.scenario=value;else if(arg=="--domain")settings.domain=value;else{try{settings.samples=unsigned(std::stoul(value));}catch(...){return 2;}}}
    else return 2;
  }
  if(settings.samples!=1&&settings.samples!=2&&settings.samples!=4)return 2;
  if(settings.domain!="offscreen"&&settings.domain!="staged"&&settings.domain!="direct"&&settings.domain!="async"&&settings.domain!="window")return 2;
  if(!settings.gpu&&(settings.domain!="offscreen"||settings.samples!=1))return 2;
#ifndef COIN_VRML_VISUAL_X11
  if(settings.domain=="window")return 77;
#endif
  SoDB::init();CoinRenderAction::initClass();SoVRMLImageTexture::setDelayFetchURL(FALSE);SbImage::addReadImageCB(localImage,nullptr);
#ifndef HAVE_COIN_BGFX
  if(settings.gpu&&!CoinRenderAction::isGpuBackendAvailable())return 77;
#endif
  Harness harness(settings);if(!check(bool(harness.target),"requested target initialization"))return 1;
  if(!sampleCount(harness))return 1;
  bool ok=false;
  if(settings.scenario=="material")ok=materials(harness);else if(settings.scenario=="texture")ok=textures(harness);
  else if(settings.scenario=="lights")ok=lights(harness);else if(settings.scenario=="fog")ok=fog(harness);
  else if(settings.scenario=="background")ok=background(harness);else if(settings.scenario=="geometry")ok=geometry(harness);
  else if(settings.scenario=="invalid")ok=invalid(harness);else if(settings.scenario=="passive")ok=passive(harness);else return 2;
  return ok?0:1;
}
