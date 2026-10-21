#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr int side=64;
using Color=std::array<double,4>;
bool canonicalRaster=false;
CoinRenderRenderer requestedRenderer=COIN_RENDER_RENDERER_UNKNOWN;
CoinRenderOptions testOptions() {
  CoinRenderOptions options{};
  if(options.renderer==COIN_RENDER_RENDERER_UNKNOWN)
    options.renderer=requestedRenderer;
  if(canonicalRaster)
    options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
  return options;
}
struct Layer {
  double z;
  Color color;
};
bool check(bool value,const std::string & label) {
  if(!value) std::cerr << "peeling_depth_state: " << label << '\n';
  return value;
}
Color over(const Color & front,const Color & back) {
  Color out{};
  for(int c=0;c<3;++c) out[c]=front[c]*front[3]+back[c]*(1-front[3]);
  out[3]=front[3]+back[3]*(1-front[3]);
  return out;
}
double rawDepth(double z) { return (9-z)/29; }
bool passes(int fn,double fragment,double opaque) {
  switch(fn) {
  case SoDepthBuffer::NEVER: return false;
  case SoDepthBuffer::ALWAYS: return true;
  case SoDepthBuffer::LESS: return fragment<opaque;
  case SoDepthBuffer::LEQUAL: return fragment<=opaque;
  case SoDepthBuffer::EQUAL: return fragment==opaque;
  case SoDepthBuffer::GEQUAL: return fragment>=opaque;
  case SoDepthBuffer::GREATER: return fragment>opaque;
  case SoDepthBuffer::NOTEQUAL: return fragment!=opaque;
  }
  return false;
}
struct Expected {
  Color color;
  double depth;
  std::vector<int> selected;
};
Expected oracle(const std::vector<Layer> & authored,const std::vector<int> & order,
                int function,bool test,bool write,int passesCount,bool opaqueWrite,
                double nearRange,double farRange,
                Color background={{.12,.20,.30,1}}) {
  const double opaqueDepth=opaqueWrite ? rawDepth(0) : 1.;
  std::vector<int> eligible;
  for(int index:order) {
    const double d=nearRange+(farRange-nearRange)*rawDepth(authored[index].z);
    if(!test || passes(function,d,opaqueDepth)) eligible.push_back(index);
  }
  // At one depth the later fragment wins. Selection is by depth, not by
  // submission order or a fixed epsilon; alpha zero still takes one slot.
  std::stable_sort(eligible.begin(),eligible.end(),[&](int a,int b) {
    return nearRange+(farRange-nearRange)*rawDepth(authored[a].z) <
      nearRange+(farRange-nearRange)*rawDepth(authored[b].z);
  });
  std::vector<int> selected;
  for(int i:eligible) {
    if(!selected.empty() && authored[i].z==authored[selected.back()].z) selected.back()=i;
    else selected.push_back(i);
  }
  if(int(selected.size())>passesCount) selected.resize(size_t(passesCount));
  Color color=background;
  for(auto it=selected.rbegin();it!=selected.rend();++it)
    color=over(authored[*it].color,color);
  double depth=opaqueDepth;
  if(test && write && !selected.empty())
    depth=nearRange+(farRange-nearRange)*rawDepth(authored[selected.front()].z);
  return {color,depth,selected};
}
struct Scene {
  SoSeparator * root=new SoSeparator;
  SoDepthBuffer * opaqueDepth=nullptr;
  std::vector<SoCoordinate3 *> coordinates;
  std::vector<SoDepthBuffer *> states;
  std::vector<SoDepthBuffer *> stateForLayer;
  std::vector<Layer> layers;
  std::vector<int> order;
  explicit Scene(std::vector<Layer> input,std::vector<int> sequence,
                 bool withOpaque=true):layers(std::move(input)),
      order(std::move(sequence)) {
    root->ref();
    auto * camera=new SoOrthographicCamera;
    camera->height=2;camera->position.setValue(0,0,10);
    camera->nearDistance=1;camera->farDistance=30;root->addChild(camera);
    auto * lighting=new SoLightModel;lighting->model=SoLightModel::BASE_COLOR;
    root->addChild(lighting);
    if(withOpaque) {
      opaqueDepth=new SoDepthBuffer;
      addTriangle(0,{{.12,.20,.30,1}},opaqueDepth);
    }
    coordinates.resize(layers.size(),nullptr);
    stateForLayer.resize(layers.size(),nullptr);
    for(int index:order) {
      auto * state=new SoDepthBuffer;
      states.push_back(state);
      stateForLayer[index]=state;
      coordinates[index]=addTriangle(layers[index].z,layers[index].color,state);
    }
  }
  ~Scene() {root->unref();}
  SoCoordinate3 * addTriangle(double z,const Color & color,SoDepthBuffer * state) {
    auto * branch=new SoSeparator;root->addChild(branch);
    branch->addChild(state);
    auto * material=new SoMaterial;
    material->diffuseColor.setValue(float(color[0]),float(color[1]),float(color[2]));
    material->transparency=float(1-color[3]);branch->addChild(material);
    auto * points=new SoCoordinate3;
    const SbVec3f p[]={SbVec3f(-.9f,-.9f,float(z)),
      SbVec3f(.9f,-.9f,float(z)),SbVec3f(0,.9f,float(z))};
    points->point.setValues(0,3,p);branch->addChild(points);
    auto * face=new SoIndexedFaceSet;
    const int32_t indices[]={0,1,2,-1};
    face->coordIndex.setValues(0,4,indices);branch->addChild(face);
    return points;
  }
  void depthState(bool test,bool write,int function,double nearRange=0,double farRange=1) {
    for(auto * state:states) {
      state->test=test;state->write=write;state->function=function;
      state->range=SbVec2f(float(nearRange),float(farRange));
    }
  }
};
struct Runner {
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderAction action{SbViewportRegion(side,side)};
  explicit Runner(bool cpu,CoinRenderOptions options={}) {
    if(options.renderer==COIN_RENDER_RENDERER_UNKNOWN)
      options.renderer=requestedRenderer;
    target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
    if(target && requestedRenderer!=COIN_RENDER_RENDERER_UNKNOWN &&
       target->getOptions().renderer!=requestedRenderer) target.reset();
    if(cpu && target) target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
    if(target) target->setDepthReadbackEnabled(TRUE);
    action.setRenderTarget(target.get());
    action.setTransparencyType(CoinRenderAction::SORTED_LAYERS_BLEND);
  }
  bool capture(Scene & scene,int count,std::vector<uint8_t> & rgba,
               std::vector<float> & depth) {
    if(!target) return false;
    action.setSortedLayersNumPasses(count);action.apply(scene.root);
    target->readbackRGBA(rgba);target->readbackDepth(depth);
    return action.getLastStatus()==CoinRenderAction::SUCCESS &&
      rgba.size()==size_t(side*side*4) && depth.size()==size_t(side*side);
  }
};
bool match(const std::vector<uint8_t> & rgba,const std::vector<float> & depth,
           const Expected & expected,const std::string & label) {
  if(rgba.size()!=size_t(side*side*4) || depth.size()!=size_t(side*side))
    return check(false,label+" image size");
  const size_t p=size_t((side-1-24)*side+32);
  bool good=true;
  for(int c=0;c<4;++c) {
    const int wanted=int(std::lround(expected.color[c]*255));
    const int actual=rgba[p*4+c];
    good &= std::abs(actual-wanted)<=6;
    if(std::abs(actual-wanted)>6)
      std::cerr << label << " channel=" << c << " actual=" << actual
                << " expected=" << wanted << '\n';
  }
  const double delta=std::abs(depth[p]-expected.depth);
  if(delta>.003) std::cerr << label << " depth=" << depth[p]
                           << " expected=" << expected.depth << '\n';
  good &= delta<=.003;
  return check(good,label+" independent color/depth oracle");
}
bool runMatrix(Runner & gpu,Runner & cpu) {
  bool ok=true;
  const std::vector<Layer> layers={
    {2,{{.85,.10,.10,.5}}}, // in front
    {1,{{.10,.85,.10,0}}},  // invisible, but consumes a distinct layer
    {-2,{{.10,.10,.85,.75}}},// behind
    {0,{{.85,.80,.10,.25}}},// equal opaque depth
    {1.999,{{.10,.80,.80,.4}}},// close but distinct from first
    {0,{{.25,.75,.85,.3}}} // equal with 3; last submission wins
  };
  const std::array<int,8> functions={{SoDepthBuffer::NEVER,SoDepthBuffer::ALWAYS,
    SoDepthBuffer::LESS,SoDepthBuffer::LEQUAL,SoDepthBuffer::EQUAL,
    SoDepthBuffer::GEQUAL,SoDepthBuffer::GREATER,SoDepthBuffer::NOTEQUAL}};
  for(bool reverse:{false,true}) {
    std::vector<int> order={0,1,2,3,4,5};
    if(reverse) std::reverse(order.begin(),order.end());
    Scene scene(layers,order);
    for(bool opaqueWrite:{false,true}) {
      scene.opaqueDepth->write=opaqueWrite;
      for(int fn:functions) for(bool test:{false,true}) for(bool write:{false,true})
        for(int count:{1,2,4}) {
          scene.depthState(test,write,fn);
          const auto expected=oracle(layers,order,fn,test,write,count,opaqueWrite,0,1);
          const std::string label="function="+std::to_string(fn)+"/test="+
            std::to_string(test)+"/write="+std::to_string(write)+"/passes="+
            std::to_string(count)+"/reverse="+std::to_string(reverse)+
            "/opaque_write="+std::to_string(opaqueWrite);
          std::vector<uint8_t> gp,cp;std::vector<float> gd,cd;
          const bool captured=gpu.capture(scene,count,gp,gd) &&
            cpu.capture(scene,count,cp,cd);
          ok &= check(captured,label+" valid profile must publish");
          if(captured) {
            ok &= match(gp,gd,expected,label+"/GPU");
            ok &= match(cp,cd,expected,label+"/CPU");
          }
        }
    }
  }
  return ok;
}
bool stateTransitions(Runner & gpu,Runner & cpu) {
  bool ok=true;
  const std::vector<Layer> layers={{2,{{.9,.15,.1,.55}}},
    {0,{{.1,.85,.2,.45}}},{-2,{{.15,.2,.95,.65}}}};
  Scene scene(layers,{2,0,1});
  auto compare=[&](const std::string & label,bool test,bool write,int function,
                   int count,bool opaqueWrite,double nearRange,double farRange,
                   std::vector<uint8_t> * savedColor=nullptr,
                   std::vector<float> * savedDepth=nullptr) {
    scene.opaqueDepth->write=opaqueWrite;
    scene.depthState(test,write,function,nearRange,farRange);
    const auto expected=oracle(layers,scene.order,function,test,write,count,
      opaqueWrite,nearRange,farRange);
    std::vector<uint8_t> g,c;std::vector<float> gd,cd;
    const bool captured=gpu.capture(scene,count,g,gd) && cpu.capture(scene,count,c,cd);
    ok &= check(captured,label+" must publish");
    if(captured) {
      ok &= match(g,gd,expected,label+"/GPU");
      ok &= match(c,cd,expected,label+"/CPU");
      if(savedColor) *savedColor=g;
      if(savedDepth) *savedDepth=gd;
    }
    return captured;
  };
  std::vector<uint8_t> first,restored,changed;
  std::vector<float> firstDepth,restoredDepth;
  compare("mutation/initial",true,true,SoDepthBuffer::GREATER,2,true,0,1,
    &first,&firstDepth);
  compare("mutation/changed",true,true,SoDepthBuffer::LESS,2,true,0,1,
    &changed);
  compare("mutation/restored",true,true,SoDepthBuffer::GREATER,2,true,0,1,
    &restored,&restoredDepth);
  ok &= check(first==restored && firstDepth==restoredDepth && first!=changed,
    "same SoDepthBuffer node must invalidate and recover exact published frame");
  compare("range/near-far",true,true,SoDepthBuffer::LEQUAL,4,true,.2,.8);
  compare("range/reversed-test-off",false,true,SoDepthBuffer::NEVER,4,true,.8,.2);
  // Coin's SoDepthBuffer does not inspect SoNode::isOverride. Explicit
  // child fields win; ignored child fields inherit the parent's element.
  auto * parent=new SoDepthBuffer;
  parent->test=TRUE;parent->write=TRUE;
  parent->function=SoDepthBuffer::GREATER;parent->setOverride(TRUE);
  scene.root->insertChild(parent,3);
  scene.depthState(true,false,SoDepthBuffer::LESS);
  const auto localExpected=oracle(layers,scene.order,SoDepthBuffer::LESS,
    true,false,4,true,0,1);
  std::vector<uint8_t> g,c;std::vector<float> gd,cd;
  const bool captured=gpu.capture(scene,4,g,gd) && cpu.capture(scene,4,c,cd);
  ok &= check(captured,"explicit child depth state must publish under parent override");
  if(captured) {
    ok &= match(g,gd,localExpected,"explicit-child/GPU");
    ok &= match(c,cd,localExpected,"explicit-child/CPU");
  }
  for(auto * state:scene.states) {
    state->test.setIgnored(TRUE);
    state->write.setIgnored(TRUE);
    state->function.setIgnored(TRUE);
    state->range.setIgnored(TRUE);
  }
  const auto inheritedExpected=oracle(layers,scene.order,SoDepthBuffer::GREATER,
    true,true,4,true,0,1);
  const bool inherited=gpu.capture(scene,4,g,gd) && cpu.capture(scene,4,c,cd);
  ok &= check(inherited,"ignored child fields must inherit parent depth state");
  if(inherited) {
    ok &= match(g,gd,inheritedExpected,"ignored-child/GPU");
    ok &= match(c,cd,inheritedExpected,"ignored-child/CPU");
  }
  for(auto * state:scene.states) {
    state->test.setIgnored(FALSE);
    state->write.setIgnored(FALSE);
    state->function.setIgnored(FALSE);
    state->range.setIgnored(FALSE);
  }
  parent->setOverride(FALSE);
  const bool restoredLocal=gpu.capture(scene,4,g,gd) && cpu.capture(scene,4,c,cd);
  ok &= check(restoredLocal,"restoring explicit local fields must publish");
  if(restoredLocal) {
    ok &= match(g,gd,localExpected,"separator/GPU");
    ok &= match(c,cd,localExpected,"separator/CPU");
  }
  return ok;
}
bool mixedFragmentStates(Runner & gpu,Runner & cpu) {
  bool ok=true;
  const std::vector<Layer> layers={{2,{{.9,.1,.1,.5}}},
    {1,{{.1,.8,.2,.5}}},{-2,{{.1,.2,.9,.75}}}};
  const Color base={{.12,.20,.30,1}};
  for(bool reverse:{false,true}) {
    std::vector<int> order={0,1,2};
    if(reverse) std::reverse(order.begin(),order.end());
    Scene scene(layers,order);
    auto * near=scene.stateForLayer[0];
    near->test=TRUE;near->write=FALSE;near->function=SoDepthBuffer::LESS;
    auto * middle=scene.stateForLayer[1];
    middle->test=TRUE;middle->write=TRUE;middle->function=SoDepthBuffer::LEQUAL;
    auto * rejected=scene.stateForLayer[2];
    rejected->test=TRUE;rejected->write=TRUE;
    rejected->function=SoDepthBuffer::LESS;
    for(int count:{1,2,4}) {
      const Color expectedColor=count==1 ? over(layers[0].color,base) :
        over(layers[0].color,over(layers[1].color,base));
      const Expected expected={expectedColor,count==1 ? rawDepth(0) :
        rawDepth(layers[1].z),count==1 ? std::vector<int>{0} :
        std::vector<int>{0,1}};
      std::vector<uint8_t> g,c;std::vector<float> gd,cd;
      const bool captured=gpu.capture(scene,count,g,gd) &&
        cpu.capture(scene,count,c,cd);
      const std::string label="mixed/reverse="+std::to_string(reverse)+
        "/passes="+std::to_string(count);
      ok &= check(captured,label+" distinct fragment depth states must publish");
      if(captured) {
        ok &= match(g,gd,expected,label+"/GPU");
        ok &= match(c,cd,expected,label+"/CPU");
      }
    }
  }
  return ok;
}
bool annotationAndReference(Runner & gpu,Runner & cpu) {
  bool ok=true;
  const std::vector<Layer> layers={{2,{{.9,.2,.1,.5}}},
    {1,{{.1,.2,.9,.4}}}};
  Scene scene(layers,{0,1});
  scene.depthState(true,false,SoDepthBuffer::LEQUAL);
  const auto expected=oracle(layers,scene.order,SoDepthBuffer::LEQUAL,
    true,false,2,true,0,1);
  std::vector<uint8_t> g,c;std::vector<float> gd,cd;
  const bool baseline=gpu.capture(scene,2,g,gd) && cpu.capture(scene,2,c,cd);
  ok &= check(baseline,"simple peeling reference must publish");
  if(baseline) {
    ok &= match(g,gd,expected,"reference/GPU");
    ok &= match(c,cd,expected,"reference/CPU");
    SoOffscreenRenderer gl(SbViewportRegion(side,side));
    gl.setComponents(SoOffscreenRenderer::RGB_TRANSPARENCY);
    gl.getGLRenderAction()->setTransparencyType(SoGLRenderAction::SORTED_LAYERS_BLEND);
    // Legacy CoinGL counts the opaque surface in its pass budget. The
    // portable contract's two passes count transparent depths only.
    gl.getGLRenderAction()->setSortedLayersNumPasses(8);
    // SoDepthBuffer::GLRender uses the action's transparent-path write rule.
    // Keep this native reference on Coin's default depth state; the portable
    // scene above exercises explicit depth fields separately.
    Scene nativeScene(layers,{0,1});
    for(int child=3;child<nativeScene.root->getNumChildren();++child)
      static_cast<SoSeparator *>(nativeScene.root->getChild(child))->removeChild(0);
    const bool native=gl.render(nativeScene.root) && gl.getBuffer();
    const bool nativePeeling=native &&
      gl.getGLRenderAction()->getTransparencyType()==
        SoGLRenderAction::SORTED_LAYERS_BLEND;
    std::cout << "peeling_depth_state_native mode="
              << int(gl.getGLRenderAction()->getTransparencyType())
              << " native_peeling=" << nativePeeling << '\n';
    ok &= check(nativePeeling,"CoinGL sorted layers must render without fallback");
    if(nativePeeling) {
      const size_t gIndex=size_t((side-1-24)*side+32)*4;
      const size_t nIndex=size_t(24*side+32)*4;
      for(int channel=0;channel<3;++channel) {
        const int delta=std::abs(int(g[gIndex+channel])-
          int(gl.getBuffer()[nIndex+channel]));
        std::cout << "peeling_depth_state_native channel=" << channel
                  << " delta=" << delta << '\n';
        ok &= check(delta<=6,"CoinRender and CoinGL simple RGB");
      }
    }
  }
  // The annotation is spatially separate from the peeling sample. It must
  // appear in the foreground after composition without changing that sample.
  auto * annotation=new SoAnnotation;scene.root->addChild(annotation);
  auto * fg=new SoMaterial;fg->diffuseColor.setValue(1,1,0);
  fg->transparency=0;annotation->addChild(fg);
  auto * coords=new SoCoordinate3;
  const SbVec3f quad[]={SbVec3f(.4f,-.2f,-4),SbVec3f(.8f,-.2f,-4),
    SbVec3f(.8f,.2f,-4),SbVec3f(.4f,.2f,-4)};
  coords->point.setValues(0,4,quad);annotation->addChild(coords);
  auto * face=new SoIndexedFaceSet;
  const int32_t indices[]={0,1,2,-1,0,2,3,-1};
  face->coordIndex.setValues(0,8,indices);annotation->addChild(face);
  const bool annotated=gpu.capture(scene,2,g,gd) && cpu.capture(scene,2,c,cd);
  ok &= check(annotated,"annotation after peeling must publish");
  if(annotated) {
    ok &= match(g,gd,expected,"annotation center/GPU");
    ok &= match(c,cd,expected,"annotation center/CPU");
    const size_t p=size_t((side-1-32)*side+48)*4;
    const auto yellow=[&](const std::vector<uint8_t> & image) {
      return image[p]>245 && image[p+1]>245 && image[p+2]<10;
    };
    ok &= check(yellow(g) && yellow(c),"foreground annotation remains visible");
  }
  return ok;
}
bool alphaOutput(Runner & gpu,Runner & cpu) {
  bool ok=true;
  const std::vector<Layer> layers={{2,{{.9,.1,.1,0}}},
    {1,{{.1,.8,.2,.25}}},{0,{{.1,.2,.9,.6}}}};
  Scene scene(layers,{2,0,1},false);
  scene.depthState(true,false,SoDepthBuffer::LEQUAL);
  gpu.action.setBackgroundColor(SbColor4f(0,0,0,0));
  cpu.action.setBackgroundColor(SbColor4f(0,0,0,0));
  const Color clear={{0,0,0,0}};
  for(int count:{1,2,4}) {
    const auto expected=oracle(layers,scene.order,SoDepthBuffer::LEQUAL,
      true,false,count,false,0,1,clear);
    std::vector<uint8_t> g,c;std::vector<float> gd,cd;
    const bool captured=gpu.capture(scene,count,g,gd) &&
      cpu.capture(scene,count,c,cd);
    ok &= check(captured,"transparent-only alpha profile must publish");
    if(captured) {
      ok &= match(g,gd,expected,"alpha/GPU/passes="+std::to_string(count));
      ok &= match(c,cd,expected,"alpha/CPU/passes="+std::to_string(count));
    }
  }
  gpu.action.setBackgroundColor(SbColor4f(0,0,0,1));
  cpu.action.setBackgroundColor(SbColor4f(0,0,0,1));
  return ok;
}
bool rollback(Runner & runner) {
  const std::vector<Layer> layers={{2,{{.9,.2,.1,.5}}},
    {1,{{.1,.2,.9,.4}}}};
  Scene scene(layers,{0,1});
  std::vector<uint8_t> before,after;std::vector<float> depthBefore,depthAfter;
  if(!runner.capture(scene,2,before,depthBefore)) return check(false,"rollback baseline");
  const auto serial=runner.target->getLastSubmissionSerial();
  runner.action.setSortedLayersNumPasses(9);
  runner.action.apply(scene.root);
  runner.target->readbackRGBA(after);runner.target->readbackDepth(depthAfter);
  bool ok=check(runner.action.getLastStatus()==CoinRenderAction::UNSUPPORTED &&
    before==after && depthBefore==depthAfter &&
    serial==runner.target->getLastSubmissionSerial(),
    "invalid pass budget must preserve RGBA/depth/serial");
  runner.action.setSortedLayersNumPasses(2);
  const auto budget=runner.action.getTransparencyBufferBudget();
  runner.action.setTransparencyBufferBudget(1);
  runner.action.apply(scene.root);
  runner.target->readbackRGBA(after);runner.target->readbackDepth(depthAfter);
  ok &= check(runner.action.getLastStatus()==CoinRenderAction::UNSUPPORTED &&
    before==after && depthBefore==depthAfter &&
    serial==runner.target->getLastSubmissionSerial(),
    "insufficient transparency budget must preserve RGBA/depth/serial");
  runner.action.setTransparencyBufferBudget(budget);
  scene.states.front()->range=SbVec2f(
    std::numeric_limits<float>::quiet_NaN(),1);
  runner.action.apply(scene.root);
  runner.target->readbackRGBA(after);runner.target->readbackDepth(depthAfter);
  ok &= check(runner.action.getLastStatus()!=CoinRenderAction::SUCCESS &&
    before==after && depthBefore==depthAfter &&
    serial==runner.target->getLastSubmissionSerial(),
    "invalid depth range must preserve RGBA/depth/serial");
  scene.states.front()->range=SbVec2f(0,1);
  ok &= check(runner.capture(scene,2,after,depthAfter) &&
    before==after && depthBefore==depthAfter,
    "valid profile must recover exactly after rejection");
  return ok;
}
bool rtt(bool direct) {
  CoinRenderOptions options=testOptions();
  options.sceneTexture=direct ? COIN_RENDER_SCENE_TEXTURE_DIRECT :
    COIN_RENDER_SCENE_TEXTURE_STAGED;
  // Exercise an already initialized shared runtime: the first target stays
  // alive while a newly created second target performs direct RTT.
  std::unique_ptr<Runner> peer;
  std::vector<uint8_t> peerColor;
  std::vector<float> peerDepth;
  uint64_t peerSerial=0;
  Scene peerScene({},{});
  if(direct) {
    peer.reset(new Runner(false,options));
    if(!peer->target || !peer->capture(peerScene,1,peerColor,peerDepth))
      return check(false,"opaque peer must initialize shared runtime first");
    peerSerial=peer->target->getLastSubmissionSerial();
  }
  Runner runner(false,options);
  if(!runner.target) return check(false,"RTT target creation");
  std::cout << "peeling_depth_rtt_effective_raster peer="
            << (peer ? int(peer->target->getOptions().rasterizationPolicy) : -1)
            << " consumer=" << int(runner.target->getOptions().rasterizationPolicy)
            << '\n';
  auto peerIntact=[&](const std::string & phase) {
    if(!peer) return true;
    std::vector<uint8_t> color;
    std::vector<float> depth;
    peer->target->readbackRGBA(color);
    peer->target->readbackDepth(depth);
    const bool intact=color==peerColor && depth==peerDepth &&
      peer->target->getLastSubmissionSerial()==peerSerial;
    return check(intact,"opaque peer must survive "+phase);
  };
  runner.action.setSortedLayersNumPasses(4);
  const std::vector<Layer> layers={{2,{{.9,.15,.1,.5}}},
    {-2,{{.1,.2,.9,.7}}}};
  Scene producer(layers,{1,0});
  auto * consumer=new SoSeparator;consumer->ref();
  auto * camera=new SoOrthographicCamera;
  camera->position.setValue(0,0,3);camera->height=2;
  camera->nearDistance=.1f;camera->farDistance=10;consumer->addChild(camera);
  auto * light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;
  consumer->addChild(light);
  auto * quality=new SoComplexity;quality->textureQuality=.3f;
  consumer->addChild(quality);
  auto * white=new SoMaterial;white->diffuseColor.setValue(1,1,1);
  consumer->addChild(white);
  auto * texture=new SoSceneTexture2;
  texture->scene=producer.root;texture->size.setValue(side,side);
  texture->type=SoSceneTexture2::RGBA8;
  texture->model=SoSceneTexture2::REPLACE;
  texture->transparencyFunction=SoSceneTexture2::NONE;
  texture->backgroundColor.setValue(.12f,.20f,.30f,1);
  consumer->addChild(texture);
  auto * uv=new SoTextureCoordinate2;
  const SbVec2f values[]={SbVec2f(0,0),SbVec2f(1,0),
    SbVec2f(1,1),SbVec2f(0,1)};
  uv->point.setValues(0,4,values);consumer->addChild(uv);
  auto * coords=new SoCoordinate3;
  const SbVec3f quad[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),
    SbVec3f(1,1,0),SbVec3f(-1,1,0)};
  coords->point.setValues(0,4,quad);consumer->addChild(coords);
  auto * face=new SoIndexedFaceSet;
  const int32_t indices[]={0,1,2,3,-1};
  face->coordIndex.setValues(0,5,indices);consumer->addChild(face);
  auto capture=[&](std::vector<uint8_t> & color,std::vector<float> & depth) {
    runner.action.apply(consumer);
    runner.target->readbackRGBA(color);runner.target->readbackDepth(depth);
    const bool published=runner.action.getLastStatus()==CoinRenderAction::SUCCESS &&
      color.size()==size_t(side*side*4) && depth.size()==size_t(side*side);
    if(!published)
      std::cerr << "peeling_depth_rtt mode=" << (direct ? "direct" : "staged")
                << " status=" << int(runner.action.getLastStatus())
                << " color_bytes=" << color.size() << " depth_pixels=" << depth.size()
                << " diagnostic=" << runner.action.getLastError().getString() << '\n';
    return published;
  };
  bool ok=true;
  std::vector<uint8_t> lastColor,color;std::vector<float> lastDepth,depth;
  uint64_t lastSerial=0;
  for(int profile=0;profile<3;++profile) {
    const bool test=profile!=2;
    const int fn=profile==1 ? SoDepthBuffer::GREATER : SoDepthBuffer::LESS;
    producer.depthState(test,true,fn);
    const auto expected=oracle(layers,producer.order,fn,test,true,4,true,0,1);
    const bool captured=capture(color,depth);
    const std::string label=std::string(direct ? "direct" : "staged")+
      "/profile="+std::to_string(profile);
    ok &= check(captured,label+" RTT producer and consumer must publish");
    ok &= peerIntact(label);
    if(captured) {
      const size_t p=size_t((side-1-32)*side+32)*4;
      for(int c=0;c<3;++c) {
        const int wanted=int(std::lround(expected.color[c]*255));
        const int actual=color[p+c];
        if(std::abs(actual-wanted)>6)
          std::cerr << label << " channel=" << c << " actual=" << actual
                    << " expected=" << wanted << '\n';
        ok &= check(std::abs(actual-wanted)<=6,
          label+" RTT must consume producer peeling color");
      }
      const float consumerDepth=depth[p/4];
      ok &= check(std::isfinite(consumerDepth) && consumerDepth>0 &&
        consumerDepth<1,label+" consumer depth must publish");
      lastColor=color;lastDepth=depth;
      lastSerial=runner.target->getLastSubmissionSerial();
    }
  }
  producer.states.front()->range=SbVec2f(
    std::numeric_limits<float>::quiet_NaN(),1);
  runner.action.apply(consumer);
  runner.target->readbackRGBA(color);runner.target->readbackDepth(depth);
  ok &= check(runner.action.getLastStatus()!=CoinRenderAction::SUCCESS &&
    color==lastColor && depth==lastDepth &&
    lastSerial==runner.target->getLastSubmissionSerial(),
    "invalid RTT producer must preserve consumer RGBA/depth/serial");
  ok &= peerIntact("RTT rejection");
  producer.states.front()->range=SbVec2f(0,1);
  ok &= check(capture(color,depth) && color==lastColor && depth==lastDepth,
    "RTT producer must recover exact consumer frame");
  ok &= peerIntact("RTT recovery");
  consumer->unref();
  return ok;
}
} // namespace
int main(int argc,char ** argv) {
  SoDB::init();CoinRenderAction::initClass();
  std::string mode;
  for(int i=1;i<argc;++i) {
    const std::string arg=argv[i];
    if(arg=="--canonical-raster") canonicalRaster=true;
    else if(arg=="--rtt-staged" || arg=="--rtt-direct") mode=arg;
    else return 1;
  }
  CoinRenderCapabilities caps{};
  const bool available=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
    &caps,sizeof(caps))==0 && caps.gpu_available;
  std::cout << "peeling_depth_state backend=" << caps.backend
            << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
            << " device=" << caps.device_id << " adapter=" << caps.adapter_name
            << " gpu_available=" << caps.gpu_available << '\n';
  if(!available) return 77;
  if(caps.renderer==COIN_RENDER_RENDERER_UNKNOWN) return 77;
  requestedRenderer=static_cast<CoinRenderRenderer>(caps.renderer);
  if(mode=="--rtt-staged")
    return rtt(false) ? 0 : 1;
  if(mode=="--rtt-direct")
    return rtt(true) ? 0 : 1;
  Runner gpu(false,testOptions()),cpu(true,testOptions());
  if(!gpu.target || !cpu.target) return 1;
  std::cout << "peeling_depth_effective_raster GPU="
            << int(gpu.target->getOptions().rasterizationPolicy)
            << " CPU=" << int(cpu.target->getOptions().rasterizationPolicy) << '\n';
  bool ok=runMatrix(gpu,cpu);
  ok &= stateTransitions(gpu,cpu);
  ok &= mixedFragmentStates(gpu,cpu);
  ok &= annotationAndReference(gpu,cpu);
  ok &= alphaOutput(gpu,cpu);
  ok &= rollback(gpu);
  ok &= rollback(cpu);
  return ok ? 0 : 1;
}
