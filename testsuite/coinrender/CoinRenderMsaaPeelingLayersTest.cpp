#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/SbPlane.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include "CoinRenderMsaaShadowFixture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>
namespace {
using A=CoinRenderAction;
constexpr int S=64;
using RGB=std::array<float,3>;
struct Frame {std::vector<uint8_t> rgba;std::vector<float> depth;uint64_t serial=0;};
bool gate(bool yes,const std::string &why) {if(!yes)std::cerr<<"msaa_peeling_layers FAIL "<<why<<'\n';return yes;}
class PartialViewport:public SoNode{
  SO_NODE_HEADER(PartialViewport);
public:
  static void initClass(){SO_NODE_INIT_CLASS(PartialViewport,SoNode,"Node");}
  PartialViewport(){SO_NODE_CONSTRUCTOR(PartialViewport);}
  bool enabled=false;SbViewportRegion region;
  void callback(SoCallbackAction *a) override {if(enabled)SoViewportRegionElement::set(a->getState(),region);}
  void GLRender(SoGLRenderAction *a) override {if(enabled)SoViewportRegionElement::set(a->getState(),region);}
protected:~PartialViewport() override=default;
};
SO_NODE_SOURCE(PartialViewport);
size_t at(int x,int y){return size_t(y)*S+x;}
SoOrthographicCamera *camera(){auto *c=new SoOrthographicCamera;c->position.setValue(0,0,2);c->height=2;c->nearDistance=1;c->farDistance=3;return c;}
SoSeparator *shape(float z,const SbColor &color,float alpha,bool triangle=false){
  auto *g=new SoSeparator;auto *m=new SoMaterial;m->diffuseColor=color;m->transparency=1-alpha;g->addChild(m);
  auto *v=new SoCoordinate3;
  const SbVec3f quad[]={SbVec3f(-1,-1,z),SbVec3f(1,-1,z),SbVec3f(1,1,z),SbVec3f(-1,1,z)};
  const SbVec3f tri[]={SbVec3f(-1,-1,z),SbVec3f(1,-1,z),SbVec3f(-1,.83f,z)};
  v->point.setValues(0,triangle?3:4,triangle?tri:quad);g->addChild(v);
  auto *f=new SoIndexedFaceSet;const int32_t qi[]={0,1,2,3,-1},ti[]={0,1,2,-1};
  f->coordIndex.setValues(0,triangle?4:5,triangle?ti:qi);g->addChild(f);return g;
}
struct Fixture {
  SoSeparator *root=new SoSeparator;
  SoSeparator *opaque=nullptr,*red=nullptr,*cyan=nullptr;
  SoMaterial *redMaterial=nullptr,*cyanMaterial=nullptr;
  Fixture(bool opaqueTriangle){root->ref();root->addChild(camera());
    auto *lm=new SoLightModel;lm->model=SoLightModel::BASE_COLOR;root->addChild(lm);
    if(opaqueTriangle){opaque=shape(0,SbColor(0,1,0),1,true);root->addChild(opaque);}
    red=shape(-.5f,SbColor(1,0,0),.5f);redMaterial=static_cast<SoMaterial*>(red->getChild(0));root->addChild(red);
  }
  void two(){cyan=shape(-.25f,SbColor(0,1,1),.25f);cyanMaterial=static_cast<SoMaterial*>(cyan->getChild(0));root->addChild(cyan);}
  ~Fixture(){root->unref();}
};
CoinRenderCapabilities capabilities(){CoinRenderCapabilities c{};
  const bool queried=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&c,sizeof(c))==0;
  if(!queried)c.gpu_available=false;
  std::cout<<"msaa_peeling_layers backend="<<c.backend<<" renderer="<<c.renderer
           <<" vendor="<<c.vendor_id<<" adapter="<<c.adapter_name<<'\n';return c;}
struct Harness {
  std::unique_ptr<CoinRenderTarget> target;A action;
  Harness(const CoinRenderCapabilities &caps,uint32_t samples)
    :target(CoinRenderTarget::createOffscreen(SbVec2i32(S,S),[&]{CoinRenderOptions o;
      o.renderer=static_cast<CoinRenderRenderer>(caps.renderer);o.sampleCount=samples;
      o.transparency=COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;return o;}())),action(SbViewportRegion(S,S)){
    if(target){target->setDepthReadbackEnabled(TRUE);action.setRenderTarget(target.get());}
    action.setBackgroundColor(SbColor4f(0,0,1,1));action.setTransparencyType(A::SORTED_OBJECT_BLEND);
  }
  ~Harness(){action.setRenderTarget(nullptr);}
  bool capture(SoNode *root,Frame &f,const std::string &tag){
    if(!target)return gate(false,"target "+tag);
    action.apply(root);target->readbackRGBA(f.rgba);target->readbackDepth(f.depth);
    f.serial=target->getLastSubmissionSerial();
    std::cout<<"msaa_peeling_layers phase="<<tag<<" status="<<int(action.getLastStatus())
             <<" serial="<<f.serial<<" bytes="<<f.rgba.size()
             <<" error="<<action.getLastError().getString()<<'\n';
    return gate(action.getLastStatus()==A::SUCCESS&&f.rgba.size()==S*S*4&&f.depth.size()==S*S&&f.serial>0,
      "complete frame "+tag);
  }
};
bool pixel(const Frame &f,int x,int y,const RGB &rgb,float alpha,float depth,int tol=3){
  const size_t p=at(x,y);if(f.rgba.size()!=S*S*4||f.depth.size()!=S*S)return false;
  bool ok=true;for(int c=0;c<3;++c)ok&=std::abs(int(f.rgba[4*p+c])-int(std::lround(rgb[c]*255)))<=tol;
  ok&=std::abs(int(f.rgba[4*p+3])-int(std::lround(alpha*255)))<=tol;
  ok&=std::abs(f.depth[p]-depth)<=1e-6f;
  if(!ok)std::cerr<<"msaa_peeling_layers FAIL pixel="<<x<<','<<y<<" rgba="<<int(f.rgba[4*p])<<','<<int(f.rgba[4*p+1])<<','<<int(f.rgba[4*p+2])<<','<<int(f.rgba[4*p+3])<<" depth="<<f.depth[p]
    <<" expected="<<rgb[0]*255<<','<<rgb[1]*255<<','<<rgb[2]*255<<','<<alpha*255<<" depth="<<depth<<'\n';
  return ok;
}
float weight(float alpha,float depth){return std::min(8.f,alpha*8.f+.01f)*std::min(16.f,std::pow(1.f-depth,3.f)*16.f+.1f);}
std::array<float,4> weightedTwo(float a,float b){
  const float wa=a*weight(a,.75f),wb=b*weight(b,.625f),sum=wa+wb;
  const float opacity=1-(1-a)*(1-b);
  const float r=sum>0?wa/sum*opacity:0,g=sum>0?wb/sum*opacity:0;
  return {{r,g,g+1-opacity,1}};
}
bool coverage(const CoinRenderCapabilities &caps,uint32_t n){
  Harness run(caps,n);Fixture scene(true);bool ok=gate(run.target&&run.target->getOptions().renderer==caps.renderer&&
    run.target->getOptions().sampleCount==n,"identified GPU and requested samples");
  Frame opaque,mixed,restored;scene.red->ref();scene.root->removeChild(scene.red);
  ok&=run.capture(scene.root,opaque,"opaque coverage control");scene.root->addChild(scene.red);
  ok&=run.capture(scene.root,mixed,"weighted behind partial opaque");
  scene.root->removeChild(scene.red);scene.red->unref();ok&=run.capture(scene.root,restored,"opaque A/B/A");
  if(!ok)return false;
  size_t quarter=0,half=0,three=0,interior=0;
  for(int y=3;y<S-3;++y)for(int x=3;x<S-3;++x){const size_t p=at(x,y);
    const int green=opaque.rgba[4*p+1];const int k=int(std::lround(double(green)*n/255));
    if(k<0||k>int(n)||std::abs(green-int(std::lround(double(k)*255/n)))>2)continue;
    const RGB want={{.5f*(1.f-float(k)/n),float(k)/n,.5f*(1.f-float(k)/n)}};
    const float depth=k? .5f:1.f;
    if(k==0||k==int(n))++interior;
    else {quarter+=(n==4&&k==1);half+=(2*k==int(n));three+=(n==4&&k==3);}
    // Only these interior pixels have the translucent full-screen rectangle
    // in every sample. The opaque control determines k independently.
    ok&=pixel(mixed,x,y,want,1,depth,3);
  }
  std::cout<<"msaa_peeling_layers occlusion samples="<<n<<" interior="<<interior
           <<" quarter="<<quarter<<" half="<<half<<" threequarter="<<three<<'\n';
  ok&=gate(interior>500&&(n==1?true:n==2?half>0:quarter>0&&three>0),
    "sample fractions expose actual 2x/4x opaque occlusion, not a 1x resolve");
  ok&=gate(opaque.rgba==restored.rgba&&opaque.depth==restored.depth&&
    opaque.serial+1==mixed.serial&&mixed.serial+1==restored.serial,
    "A/B/A exact and one publication per apply");return ok;
}
bool numericOrder(const CoinRenderCapabilities &caps,uint32_t n){
  Harness run(caps,n);Fixture s(false);s.two();Frame first,reverse,mutated,again,zero,opaque;
  bool ok=run.capture(s.root,first,"two weighted depths");
  const auto want=weightedTwo(.5f,.25f);
  ok&=pixel(first,32,32,{{want[0],want[1],want[2]}},want[3],1.f);
  s.cyan->ref();s.root->removeChild(s.cyan);s.root->insertChild(s.cyan,2);
  ok&=run.capture(s.root,reverse,"reverse transparent traversal");
  ok&=gate(first.rgba==reverse.rgba&&first.depth==reverse.depth&&reverse.serial==first.serial+1,
    "weighted sum/revealage independent of traversal order");
  s.cyanMaterial->transparency=.5f;
  ok&=run.capture(s.root,mutated,"alpha B");
  const auto changed=weightedTwo(.5f,.5f);
  ok&=pixel(mutated,32,32,{{changed[0],changed[1],changed[2]}},changed[3],1.f);
  s.cyanMaterial->transparency=.75f;
  ok&=run.capture(s.root,again,"alpha A restored");
  ok&=gate(mutated.rgba!=reverse.rgba&&again.rgba==first.rgba&&again.depth==first.depth&&
    reverse.serial+1==mutated.serial&&mutated.serial+1==again.serial,
    "weighted alpha mutation and exact A/B/A");
  s.cyanMaterial->transparency=1.f;
  ok&=run.capture(s.root,zero,"zero-alpha still occupies weighted pass");
  const auto zeroWant=weightedTwo(.5f,0.f);
  ok&=pixel(zero,32,32,{{zeroWant[0],zeroWant[1],zeroWant[2]}},1.f,1.f);
  s.cyanMaterial->transparency=0.f;
  ok&=run.capture(s.root,opaque,"alpha-one opaque occludes weighted fragment");
  ok&=pixel(opaque,32,32,{{0,1,1}},1.f,.625f);
  s.cyanMaterial->transparency=.75f;
  Frame last;ok&=run.capture(s.root,last,"weighted alpha restored after opaque");
  ok&=gate(last.rgba==first.rgba&&last.depth==first.depth&&
    zero.serial==again.serial+1&&opaque.serial==zero.serial+1&&last.serial==opaque.serial+1,
    "alpha zero/one transitions preserve distinct weighted and opaque depth");
  s.cyan->unref();return ok;
}
bool layers(const CoinRenderCapabilities &caps,uint32_t n){
  auto *base=new SoSeparator;base->ref();base->addChild(camera());
  auto *lm=new SoLightModel;lm->model=SoLightModel::BASE_COLOR;base->addChild(lm);
  base->addChild(shape(0,SbColor(1,0,0),1,true));
  // The base pass itself must execute peeling OIT.  The cyan quad is behind
  // the opaque triangle, so the triangle occludes it while pixels outside
  // the triangle reveal half cyan over the opaque blue background.
  auto *cyanBase=shape(-.5f,SbColor(0,1,1),.5f);
  auto *magentaBase=shape(-.75f,SbColor(1,0,1),.25f);
  base->addChild(cyanBase);base->addChild(magentaBase);
  auto *note=new SoAnnotation;
  auto *nm=new SoMaterial;nm->diffuseColor.setValue(1,1,0);note->addChild(nm);
  auto *nv=new SoCoordinate3;const SbVec3f noteVertices[]={
    SbVec3f(.74f,.74f,-.8f),SbVec3f(.98f,.74f,-.8f),
    SbVec3f(.98f,.98f,-.8f),SbVec3f(.74f,.98f,-.8f)};
  nv->point.setValues(0,4,noteVertices);note->addChild(nv);
  auto *nf=new SoIndexedFaceSet;const int32_t ni[]={0,1,2,3,-1};
  nf->coordIndex.setValues(0,5,ni);note->addChild(nf);
  base->addChild(note);
  auto *front=new SoSeparator;front->ref();auto *viewport=new PartialViewport;
  viewport->region=SbViewportRegion(S,S);viewport->region.setViewportPixels(S/2,0,S/2,S);
  front->addChild(viewport);front->addChild(camera());
  auto *frontLm=new SoLightModel;frontLm->model=SoLightModel::BASE_COLOR;
  front->addChild(frontLm);front->addChild(shape(-.5f,SbColor(0,1,0),.5f));
  auto *background=new SoSeparator;background->ref();background->addChild(camera());
  auto *backgroundLm=new SoLightModel;backgroundLm->model=SoLightModel::BASE_COLOR;
  background->addChild(backgroundLm);background->addChild(shape(.5f,SbColor(0,0,1),1));
  SoRenderManager manager;manager.setSceneGraph(base);
  manager.setViewportRegion(SbViewportRegion(S,S));
  manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.setBackgroundColor(SbColor4f(0,0,0,1));
  manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::SORTED_LAYERS_BLEND);
  manager.getGLRenderAction()->setSortedLayersNumPasses(2);
  auto *backgroundLayer=manager.addSuperimposition(background,
    SoRenderManager::Superimposition::AUTOREDRAW|
    SoRenderManager::Superimposition::ZBUFFERON|
    SoRenderManager::Superimposition::BACKGROUND);
  CoinRenderOptions options;options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  options.sampleCount=n;options.transparency=COIN_RENDER_TRANSPARENCY_PEELING;
  CoinRenderManagerAdapter adapter(manager,SbVec2i32(S,S),options);
  auto *target=adapter.getSceneManager()->getRenderTarget();
  if(!gate(target&&target->getOptions().renderer==caps.renderer&&
    target->getOptions().sampleCount==n,"peeling manager real GPU/count")){
    manager.removeSuperimposition(backgroundLayer);manager.setSceneGraph(nullptr);
    background->unref();front->unref();base->unref();return false;}
  target->setDepthReadbackEnabled(TRUE);
  auto capture=[&](Frame &f,const char *label){
    const auto status=adapter.render();target->readbackRGBA(f.rgba);target->readbackDepth(f.depth);
    f.serial=target->getLastSubmissionSerial();
    std::cout<<"msaa_peeling layers="<<label<<" samples="<<n<<" status="<<int(status)
             <<" serial="<<f.serial<<" error="<<adapter.getLastError().getString()<<'\n';
    return gate(status==A::SUCCESS&&f.rgba.size()==S*S*4&&f.depth.size()==S*S&&f.serial>0,
      "peeling manager publishes complete frame");
  };
  Frame bare,reversed,orderRestored,zoff,disabled,restored,partial;
  bool ok=capture(bare,"two peeling depths+annotation");
  magentaBase->ref();base->removeChild(magentaBase);
  base->insertChild(magentaBase,3);
  ok&=capture(reversed,"reverse peeling base traversal");
  base->removeChild(magentaBase);base->insertChild(magentaBase,4);
  magentaBase->unref();
  ok&=capture(orderRestored,"restore peeling base traversal");
  auto *layer=manager.addSuperimposition(front,SoRenderManager::Superimposition::AUTOREDRAW);
  ok&=capture(zoff,"peeling foreground ZOFF");
  layer->setEnabled(FALSE);ok&=capture(disabled,"foreground disabled");
  manager.removeSuperimposition(layer);ok&=capture(restored,"foreground removed");
  viewport->enabled=true;
  layer=manager.addSuperimposition(front,
    SoRenderManager::Superimposition::AUTOREDRAW|
    SoRenderManager::Superimposition::ZBUFFERON|
    SoRenderManager::Superimposition::CLEARZBUFFER);
  ok&=capture(partial,"peeling partial clear");manager.removeSuperimposition(layer);
  if(ok){
    ok&=pixel(bare,16,48,{{1,0,0}},1,.5f);
    // Near cyan alpha .5 lies at depth .75; far magenta alpha .25 at
    // depth .875. Peeling composes cyan over magenta over opaque blue.
    // Traversal-order OBJECT gives R=.25/G=.375 instead of R=.125/G=.5.
    const RGB peelingOutside={{.125f,.5f,1.f}};
    ok&=pixel(bare,48,16,peelingOutside,1,1.f);
    ok&=gate(bare.rgba==reversed.rgba&&bare.depth==reversed.depth&&
      bare.rgba==orderRestored.rgba&&bare.depth==orderRestored.depth,
      "two peeling base depths sort geometrically, independent of traversal order");
    ok&=pixel(zoff,16,48,{{.5f,.5f,0}},1,.5f);
    ok&=pixel(partial,16,48,{{1,0,0}},1,.5f);
    // CLEARZ erases earlier depth only in this viewport.  The foreground
    // is an ordinary manager layer and writes its own z=-.5 depth afterward.
    ok&=pixel(partial,40,44,{{.5f,.5f,0}},1,.75f);
    auto diagnose=[&](const Frame &other,const char *label){
      if(bare.rgba==other.rgba&&bare.depth==other.depth)return;
      for(size_t p=0;p<size_t(S)*S;++p){
        if(bare.depth[p]==other.depth[p]&&
           std::equal(bare.rgba.begin()+4*p,bare.rgba.begin()+4*p+4,
                      other.rgba.begin()+4*p))continue;
        std::cerr<<"msaa_peeling layers_diff="<<label<<" xy="<<p%S<<','<<p/S
          <<" bare_rgba="<<int(bare.rgba[4*p])<<','<<int(bare.rgba[4*p+1])
          <<','<<int(bare.rgba[4*p+2])<<','<<int(bare.rgba[4*p+3])
          <<" other_rgba="<<int(other.rgba[4*p])<<','<<int(other.rgba[4*p+1])
          <<','<<int(other.rgba[4*p+2])<<','<<int(other.rgba[4*p+3])
          <<" bare_depth="<<bare.depth[p]<<" other_depth="<<other.depth[p]<<'\n';
        break;
      }
    };
    diagnose(disabled,"disabled");diagnose(restored,"removed");
    const size_t notePixel=at(58,6);
    std::cout<<"msaa_peeling layers_note_rgba="<<int(bare.rgba[4*notePixel])
      <<','<<int(bare.rgba[4*notePixel+1])<<','<<int(bare.rgba[4*notePixel+2])
      <<','<<int(bare.rgba[4*notePixel+3])<<" depth="<<bare.depth[notePixel]
      <<" serials="<<bare.serial<<','<<reversed.serial<<','<<orderRestored.serial
      <<','<<zoff.serial<<','<<disabled.serial<<','<<restored.serial<<','
      <<partial.serial<<'\n';
    ok&=gate(bare.rgba==disabled.rgba&&bare.depth==disabled.depth,
      "disabled foreground restores base RGBA/depth exactly");
    ok&=gate(bare.rgba==restored.rgba&&bare.depth==restored.depth,
      "removed foreground restores base RGBA/depth exactly");
    ok&=gate(bare.rgba[4*notePixel]>240&&bare.rgba[4*notePixel+1]>240,
      "annotation stays visible above peeling base");
    ok&=gate(bare.serial+1==reversed.serial&&reversed.serial+1==orderRestored.serial&&
      orderRestored.serial+1==zoff.serial&&zoff.serial+1==disabled.serial&&
      disabled.serial+1==restored.serial&&restored.serial+1==partial.serial,
      "ZOFF, partial CLEARZ and manager A/B/A serials exact");
  }
  manager.removeSuperimposition(backgroundLayer);manager.setSceneGraph(nullptr);
  background->unref();front->unref();base->unref();return ok;
}
}
int main(int argc,char **argv){
  if(argc!=2 || std::string(argv[1])!="--layers")return 2;
  SoDB::init();A::initClass();PartialViewport::initClass();
  const CoinRenderCapabilities caps=capabilities();
  if(!gate(caps.gpu_available&&caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN&&
    (caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION||
     caps.backend==COIN_RENDER_EXPERIMENTAL_RUST),"physical GPU required"))return 1;
  bool ok=true;for(uint32_t n:{1u,2u,4u})ok&=layers(caps,n);
  return ok?0:1;
}
