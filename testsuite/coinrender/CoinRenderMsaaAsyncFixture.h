#ifndef COIN_RENDER_MSAA_ASYNC_FIXTURE_H
#define COIN_RENDER_MSAA_ASYNC_FIXTURE_H
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
namespace MsaaAsync {
constexpr int side=32;
using Action=CoinRenderAction;
using Ticket=CoinRenderReadbackTicket;
using Target=CoinRenderTarget;
inline void require(bool value,const std::string &why) {if(!value)throw std::runtime_error(why);}
inline SoOrthographicCamera *camera() {
  auto *c=new SoOrthographicCamera;c->position.setValue(0,0,2);
  c->height=2;c->nearDistance=1;c->farDistance=3;return c;
}
struct Scene {
  SoSeparator *root=new SoSeparator;SoMaterial *material=new SoMaterial;
  SoCoordinate3 *coordinates=new SoCoordinate3;SoAlphaTest *alpha=new SoAlphaTest;
  Scene() {
    root->ref();root->addChild(camera());
    auto *lm=new SoLightModel;lm->model=SoLightModel::BASE_COLOR;root->addChild(lm);
    material->diffuseColor.setValue(1,0,0);root->addChild(material);
    alpha->function=SoAlphaTest::ALWAYS;root->addChild(alpha);root->addChild(coordinates);setZ(0);
    auto *face=new SoIndexedFaceSet;const int32_t ix[]={0,1,2,-1};
    face->coordIndex.setValues(0,4,ix);root->addChild(face);
  }
  void setZ(float z) {const SbVec3f p[]={SbVec3f(-1,-1,z),SbVec3f(1,-1,z),SbVec3f(-1,.83f,z)};
    coordinates->point.setValues(0,3,p);}
  void version(unsigned i) {
    material->diffuseColor.setValue((i%3)==0?1.f:.125f,(i%3)==1?1.f:.25f,(i%3)==2?1.f:.375f);
    setZ(float(int(i%5)-2)*.125f);
  }
  ~Scene(){root->unref();}
};
struct Frame {std::vector<uint8_t> rgba;std::vector<float> depth;};
inline bool same(const Frame&a,const Frame&b){return a.rgba==b.rgba&&a.depth==b.depth;}
inline Frame read(Target&t){Frame f;t.readbackRGBA(f.rgba);t.readbackDepth(f.depth);return f;}
inline bool sameTicket(const Ticket&a,const Ticket&b) {
  return a.token==b.token&&a.generation==b.generation&&a.submissionSerial==b.submissionSerial&&
    a.width==b.width&&a.height==b.height&&a.colorFormat==b.colorFormat&&a.depthFormat==b.depthFormat&&
    a.colorRowPitch==b.colorRowPitch&&a.depthRowPitch==b.depthRowPitch&&
    a.colorBytes==b.colorBytes&&a.depthBytes==b.depthBytes;
}
inline CoinRenderOptions options(unsigned samples,bool direct=false) {
  CoinRenderOptions o;o.sampleCount=samples;
  o.sceneTexture=direct?COIN_RENDER_SCENE_TEXTURE_DIRECT:COIN_RENDER_SCENE_TEXTURE_STAGED;
  const char *renderer=std::getenv("COIN_BGFX_RENDERER");
  if(renderer&&std::string(renderer)=="vulkan")o.renderer=COIN_RENDER_RENDERER_VULKAN;
  if(renderer&&std::string(renderer)=="opengl")o.renderer=COIN_RENDER_RENDERER_OPENGL;return o;
}
struct Harness {
  std::unique_ptr<Target> target;Action action;Ticket lastIssued;
  Harness(const CoinRenderOptions&o,int width=side,int height=side)
    :target(Target::createOffscreen(SbVec2i32(width,height),o)),action(SbViewportRegion(width,height)) {
    require(bool(target),"create public offscreen target");
    require(target->getOptions().sampleCount==o.sampleCount,"retain exact sample count");
    action.setRenderTarget(target.get());action.setBackgroundColor(SbColor4f(0,0,0,0));
    action.setTransparencyType(Action::NONE);
  }
  ~Harness(){action.setRenderTarget(nullptr);}
  void depth(bool yes){require(target->setDepthReadbackEnabled(yes?TRUE:FALSE),"depth policy");}
  Frame sync(SoNode*root) {
    const uint64_t priorPublication=target->getLastSubmissionSerial();
    action.apply(root);require(action.getLastStatus()==Action::SUCCESS,
      std::string("sync failed: ")+action.getLastError().getString());
    require(target->getLastSubmissionSerial()==priorPublication+1,
      "one target-scoped publication per successful synchronous frame, including RTT producer graphs");
    Frame f=read(*target);require(f.rgba.size()==size_t(target->getSize()[0])*target->getSize()[1]*4,
      "complete synchronous color");return f;
  }
  Ticket submit(SoNode*root) {
    const uint64_t priorPublication=target->getLastSubmissionSerial();
    Ticket t;action.applyAsync(root,t);
    require(action.getLastStatus()==Action::SUCCESS&&t.token!=0,
      std::string("async failed: ")+action.getLastError().getString());
    // Target publication and native queue/ticket identity are different
    // domains. Producer work can advance the queue several times while one
    // accepted public frame advances this target's publication exactly once.
    require(target->getLastSubmissionSerial()==priorPublication+1,
      "one target-scoped publication per accepted asynchronous frame");
    require(t.submissionSerial!=0,"ticket retains a real nonzero native submission serial");
    if(lastIssued.token&&lastIssued.generation==t.generation)
      require(t.submissionSerial>lastIssued.submissionSerial,
        "native ticket serials are strictly ordered within the same generation, independent of public publication numbers");
    require(t.width==uint32_t(target->getSize()[0])&&t.height==uint32_t(target->getSize()[1])&&
      t.colorFormat==0&&t.colorRowPitch>=t.width*4&&t.colorBytes==uint64_t(t.width)*t.height*4,
      "ticket color metadata");
    require(target->isDepthReadbackEnabled()?
      t.depthFormat==1&&t.depthRowPitch>=t.width*4&&t.depthBytes==uint64_t(t.width)*t.height*4:
      t.depthFormat==0&&t.depthRowPitch==0&&t.depthBytes==0,"ticket depth metadata");
    const Frame hidden=read(*target);require(hidden.rgba.empty()&&hidden.depth.empty(),"async hides synchronous output");
    lastIssued=t;return t;
  }
};
inline Frame poll(const Ticket&ticket) {
  Frame output;output.rgba={19,31};output.depth={-7.f,-9.f};const Frame sentinel=output;SbString diagnostic;
  // The CTest process watchdog owns the 120/180 second deadline. A cold
  // driver may compile shaders during polling; do not impose a shorter one.
  const auto started=std::chrono::steady_clock::now();
  for(unsigned attempt=0;;++attempt) {
    const auto before=std::chrono::steady_clock::now();
    std::cerr<<"msaa_async poll_begin token="<<ticket.token<<" serial="<<ticket.submissionSerial
      <<" attempt="<<attempt<<" elapsed_ms="<<std::chrono::duration<double,std::milli>(before-started).count()<<'\n';
    const auto status=Target::pollReadback(ticket,output.rgba,output.depth,&diagnostic);
    const auto after=std::chrono::steady_clock::now();
    std::cerr<<"msaa_async poll_end token="<<ticket.token<<" attempt="<<attempt
      <<" status="<<int(status)<<" poll_ms="<<std::chrono::duration<double,std::milli>(after-before).count()
      <<" elapsed_ms="<<std::chrono::duration<double,std::milli>(after-started).count()
      <<" diagnostic="<<diagnostic.getString()<<'\n';
    if(status==Target::READBACK_READY) {
      require(output.rgba.size()==ticket.colorBytes&&output.depth.size()*sizeof(float)==ticket.depthBytes,
        "poll publishes complete outputs together");return output;
    }
    require(same(output,sentinel),"unsuccessful poll changed caller output");
    require(status==Target::READBACK_NOT_READY,std::string("poll failed: ")+diagnostic.getString());
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
inline void invalid(const Ticket&t) {
  Frame sentinel;sentinel.rgba={17,23};sentinel.depth={-1.f,-2.f};Frame actual=sentinel;
  require(Target::pollReadback(t,actual.rgba,actual.depth)==Target::READBACK_INVALID_TICKET,"invalid ticket accepted");
  require(same(actual,sentinel),"invalid ticket changed output");
}
inline void coverage(const Frame&f,unsigned n,float plane=.5f) {
  require(f.rgba.size()==side*side*4&&f.depth.size()==side*side,"oracle image extent");
  unsigned empty=0,whole=0,partial=0,odd=0;
  for(size_t p=0;p<f.depth.size();++p) {
    const unsigned a=f.rgba[4*p+3];const int k=int(std::lround(double(a)*n/255));
    require(k>=0&&k<=int(n)&&std::abs(double(a)-255.*k/n)<=1,"color is k/N mean");
    require(std::abs(int(f.rgba[4*p])-int(a))<=1&&f.rgba[4*p+1]==0&&f.rgba[4*p+2]==0,
      "resolve preserves red coverage");
    require(std::isfinite(f.depth[p])&&std::abs(f.depth[p]-(a?plane:1.f))<=1e-6f,
      "minimum sample depth follows every covered edge, background is one");
    empty+=a==0;whole+=a==255;partial+=a>0&&a<255;odd+=(k&1)&&a>0&&a<255;
  }
  require(empty>0&&whole>0,"oracle has interior and background");
  require(n==1?partial==0:partial>0,"requested samples produce actual partial coverage");
  require(n!=4||odd>0,"4x requires quarter or three-quarter witness (reject 2x fallback)");
  std::cout<<"msaa_async samples="<<n<<" whole="<<whole<<" partial="<<partial<<" odd="<<odd<<'\n';
}
inline SoSeparator *quad() {
  auto *r=new SoSeparator;auto *coords=new SoCoordinate3;
  const SbVec3f p[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),SbVec3f(1,1,0),SbVec3f(-1,1,0)};
  coords->point.setValues(0,4,p);r->addChild(coords);auto *uv=new SoTextureCoordinate2;
  const SbVec2f t[]={SbVec2f(0,0),SbVec2f(1,0),SbVec2f(1,1),SbVec2f(0,1)};
  uv->point.setValues(0,4,t);r->addChild(uv);auto *face=new SoIndexedFaceSet;
  const int32_t ix[]={0,1,2,3,-1};face->coordIndex.setValues(0,5,ix);face->textureCoordIndex.setValues(0,5,ix);
  r->addChild(face);return r;
}
struct RttScene {
  Scene producer;SoSeparator *root=new SoSeparator;SoSceneTexture2 *texture=new SoSceneTexture2;
  RttScene() {
    root->ref();root->addChild(camera());auto *lm=new SoLightModel;lm->model=SoLightModel::BASE_COLOR;
    root->addChild(lm);auto *m=new SoMaterial;m->diffuseColor.setValue(1,1,1);root->addChild(m);
    texture->scene=producer.root;texture->size.setValue(side,side);texture->type=SoSceneTexture2::RGBA8;
    texture->backgroundColor.setValue(0,0,0,0);texture->model=SoSceneTexture2::REPLACE;
    root->addChild(texture);root->addChild(quad());
  }
  ~RttScene(){root->unref();}
};
}
#endif
