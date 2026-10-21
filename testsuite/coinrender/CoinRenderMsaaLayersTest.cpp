#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderViewBudgetTest.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoInteraction.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/SoBoxHighlightRenderAction.h>
#include <Inventor/actions/SoLineHighlightRenderAction.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSelection.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <limits>
#include <string>
#include <utility>
#include <vector>
namespace {
constexpr int side=32;
class LayerViewport : public SoNode {
  SO_NODE_HEADER(LayerViewport);
public:
  static void initClass() {SO_NODE_INIT_CLASS(LayerViewport,SoNode,"Node");}
  LayerViewport() {SO_NODE_CONSTRUCTOR(LayerViewport);}
  bool enabled=false;
  SbViewportRegion region;
  void callback(SoCallbackAction *action) override {
    if(enabled)SoViewportRegionElement::set(action->getState(),region);
  }
  void GLRender(SoGLRenderAction *action) override {
    if(enabled)SoViewportRegionElement::set(action->getState(),region);
  }
protected:
  ~LayerViewport() override=default;
};
SO_NODE_SOURCE(LayerViewport);
bool check(bool good,const char *label) {
  if(!good)std::cerr << "msaa_layers FAIL " << label << '\n';
  return good;
}
CoinRenderCapabilities capabilities() {
  CoinRenderCapabilities caps{};
  const bool queried=coin_render_query_capabilities(
    COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))==0;
  std::cout << "msaa_layers backend=" << caps.backend
            << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
            << " adapter=" << caps.adapter_name << '\n';
  if(!queried)caps.gpu_available=false;
  return caps;
}
CoinRenderOptions options(const CoinRenderCapabilities &caps,uint32_t count) {
  CoinRenderOptions result;
  result.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  result.sampleCount=count;return result;
}
SoOrthographicCamera *camera() {
  auto *result=new SoOrthographicCamera;
  result->position.setValue(0,0,2);result->height=2;
  result->nearDistance=1;result->farDistance=3;return result;
}
struct Scene {
  SoSeparator *base=new SoSeparator,*layer=new SoSeparator;
  SoMaterial *baseMaterial=new SoMaterial,*layerMaterial=new SoMaterial;
  SoCoordinate3 *layerPoints=new SoCoordinate3;
  LayerViewport *viewport=new LayerViewport;
  Scene() {
    base->ref();layer->ref();
    base->addChild(camera());
    auto *baseLight=new SoLightModel;baseLight->model=SoLightModel::BASE_COLOR;
    base->addChild(baseLight);
    baseMaterial->diffuseColor.setValue(1,0,0);base->addChild(baseMaterial);
    auto *trianglePoints=new SoCoordinate3;
    const SbVec3f tri[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),
                         SbVec3f(-1,.83f,0)};
    trianglePoints->point.setValues(0,3,tri);base->addChild(trianglePoints);
    auto *triangle=new SoIndexedFaceSet;
    const int32_t ix[]={0,1,2,-1};triangle->coordIndex.setValues(0,4,ix);
    base->addChild(triangle);
    auto *annotation=new SoAnnotation;
    auto *noteColor=new SoMaterial;
    noteColor->diffuseColor.setValue(1,1,0);annotation->addChild(noteColor);
    auto *notePoints=new SoCoordinate3;
    const SbVec3f note[]={SbVec3f(.74f,.74f,-.8f),
      SbVec3f(.98f,.74f,-.8f),SbVec3f(.98f,.98f,-.8f),
      SbVec3f(.74f,.98f,-.8f)};
    notePoints->point.setValues(0,4,note);annotation->addChild(notePoints);
    auto *noteFace=new SoFaceSet;noteFace->numVertices.set1Value(0,4);
    annotation->addChild(noteFace);base->addChild(annotation);
    viewport->region=SbViewportRegion(side,side);
    viewport->region.setViewportPixels(16,0,16,side);
    layer->addChild(viewport);
    layer->addChild(camera());
    auto *layerLight=new SoLightModel;layerLight->model=SoLightModel::BASE_COLOR;
    layer->addChild(layerLight);
    layerMaterial->diffuseColor.setValue(0,1,0);layer->addChild(layerMaterial);
    layer->addChild(layerPoints);
    auto *quad=new SoFaceSet;quad->numVertices.set1Value(0,4);
    layer->addChild(quad);
    setZ(-.5f);
  }
  void setZ(float z) {
    const SbVec3f p[]={SbVec3f(-.7f,-.7f,z),SbVec3f(.7f,-.7f,z),
                       SbVec3f(.7f,.7f,z),SbVec3f(-.7f,.7f,z)};
    layerPoints->point.setValues(0,4,p);
  }
  ~Scene(){base->unref();layer->unref();}
};
struct Frame {std::vector<uint8_t>rgba;std::vector<float>depth;uint64_t serial=0;};
bool capture(CoinRenderManagerAdapter &adapter,Frame &frame,const char *phase) {
  const auto status=adapter.render();
  auto *target=adapter.getSceneManager()->getRenderTarget();
  target->readbackRGBA(frame.rgba);target->readbackDepth(frame.depth);
  frame.serial=target->getLastSubmissionSerial();
  const bool good=status==CoinRenderAction::SUCCESS &&
    frame.rgba.size()==size_t(side)*side*4 &&
    frame.depth.size()==size_t(side)*side && frame.serial>0;
  std::cout << "msaa_layers phase=" << phase << " status=" << int(status)
            << " rgba=" << frame.rgba.size() << " depth=" << frame.depth.size()
            << " serial=" << frame.serial;
  if(!good)std::cout << " error=" << adapter.getLastError().getString();
  std::cout << '\n';return check(good,phase);
}
size_t at(int x,int y){return size_t(y)*side+x;}
bool pixel(const Frame &frame,int x,int y,int r,int g,float depth,
           uint32_t samples) {
  const size_t p=at(x,y);
  return std::abs(int(frame.rgba[4*p])-r)<=2 &&
    std::abs(int(frame.rgba[4*p+1])-g)<=2 &&
    (samples==1?std::abs(frame.depth[p]-depth)<=1e-6f:
                frame.depth[p]==depth);
}
bool coverage(const Frame &frame,uint32_t samples) {
  size_t quarter=0,half=0,three=0;
  for(size_t i=0;i<frame.depth.size();++i) {
    const int alpha=frame.rgba[4*i+3];
    if(alpha==0 || alpha==255)continue;
    const int count=int(std::lround(double(alpha)*samples/255));
    if(count<=0 || count>=int(samples) ||
       std::abs(alpha-int(std::lround(255.0*count/samples)))>1)
      return false;
    quarter+=(count==1);half+=(count*2==int(samples));
    three+=(samples==4 && count==3);
  }
  std::cout << "msaa_layers coverage samples=" << samples
            << " quarter=" << quarter << " half=" << half
            << " threequarter=" << three << '\n';
  return samples==1?quarter+half+three==0:
    samples==2?half>0:quarter+three>0;
}
bool layerProfile(const CoinRenderCapabilities &caps,uint32_t samples) {
  Scene scene;SoRenderManager manager;
  manager.setSceneGraph(scene.base);
  manager.setViewportRegion(SbViewportRegion(side,side));
  manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.setBackgroundColor(SbColor4f(0,0,0,0));
  CoinRenderManagerAdapter adapter(manager,SbVec2i32(side,side),options(caps,samples));
  auto *target=adapter.getSceneManager()->getRenderTarget();
  if(!check(target && target->getOptions().renderer==caps.renderer &&
     target->getOptions().sampleCount==samples,"actual API and sample count"))
    return false;
  target->setDepthReadbackEnabled(TRUE);
  Frame base,rear,front,zoff,cleared,restored,partial;
  bool ok=capture(adapter,base,"base");
  const unsigned zon=SoRenderManager::Superimposition::AUTOREDRAW |
    SoRenderManager::Superimposition::ZBUFFERON;
  auto *layer=manager.addSuperimposition(scene.layer,zon);
  ok &= capture(adapter,rear,"rear-ZON");
  scene.setZ(.5f);ok &= capture(adapter,front,"front-ZON");
  manager.removeSuperimposition(layer);
  scene.setZ(-.5f);
  layer=manager.addSuperimposition(scene.layer,
    SoRenderManager::Superimposition::AUTOREDRAW);
  ok &= capture(adapter,zoff,"rear-ZOFF");
  manager.removeSuperimposition(layer);
  layer=manager.addSuperimposition(scene.layer,
    zon|SoRenderManager::Superimposition::CLEARZBUFFER);
  ok &= capture(adapter,cleared,"rear-CLEARZ");
  layer->setEnabled(FALSE);
  ok &= capture(adapter,restored,"layer-disabled");
  manager.removeSuperimposition(layer);
  scene.viewport->enabled=true;
  layer=manager.addSuperimposition(scene.layer,
    zon|SoRenderManager::Superimposition::CLEARZBUFFER);
  ok &= capture(adapter,partial,"partial-CLEARZ");
  manager.removeSuperimposition(layer);
  scene.viewport->enabled=false;
  if(ok) {
    // x=8,y=24 is well inside the authored base triangle and the layer quad.
    ok &= check(pixel(base,8,24,255,0,.5f,samples) &&
      pixel(rear,8,24,255,0,.5f,samples) &&
      pixel(front,8,24,0,255,.25f,samples) &&
      pixel(zoff,8,24,0,255,.5f,samples) &&
      pixel(cleared,8,24,0,255,.75f,samples),
      "analytic interior RGB and final minimum-sample depth by layer mode");
    ok &= check(pixel(rear,8,24,255,0,.5f,samples) &&
      base.rgba==restored.rgba && base.depth==restored.depth,
      "rear occlusion at covered witness and disabled-layer A/B/A exact");
    const size_t note=at(29,3);
    ok &= check(base.rgba[4*note]>250 && base.rgba[4*note+1]>250 &&
      base.rgba[4*note+2]<5 && base.depth[note]==1.f &&
      cleared.rgba[4*note]==base.rgba[4*note] &&
      cleared.depth[note]==base.depth[note],
      "annotation remains visible without writing depth outside the layer");
    for(const auto witness:{std::pair<int,int>(8,24),std::pair<int,int>(20,20)}) {
      const int x=witness.first,y=witness.second;
      const size_t p=at(x,y);
      std::cout << "msaa_layers partial_witness samples=" << samples
                << " x=" << x << " y=" << y << " rgba="
                << int(partial.rgba[4*p]) << ',' << int(partial.rgba[4*p+1])
                << ',' << int(partial.rgba[4*p+2]) << ',' << int(partial.rgba[4*p+3])
                << " depth=" << partial.depth[p] << '\n';
    }
    ok &= check(pixel(partial,8,24,255,0,.5f,samples) &&
      pixel(partial,20,20,0,255,.75f,samples),
      "partial viewport clears all depth samples only inside its extent");
    ok &= check(coverage(base,samples),"base edge resolves requested samples");
    ok &= check(base.serial+1==rear.serial && rear.serial+1==front.serial &&
      front.serial+1==zoff.serial && zoff.serial+1==cleared.serial &&
      cleared.serial+1==restored.serial &&
      restored.serial+1==partial.serial,
      "one publication per manager frame");
  }
  manager.setSceneGraph(nullptr);return ok;
}
bool backgroundProfile(const CoinRenderCapabilities &caps,uint32_t samples) {
  Scene scene,blue;
  blue.layerMaterial->diffuseColor.setValue(0,0,1);
  blue.setZ(.5f);
  SoRenderManager manager;
  manager.setSceneGraph(scene.base);
  manager.setViewportRegion(SbViewportRegion(side,side));
  manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.setBackgroundColor(SbColor4f(0,0,0,0));
  const unsigned backgroundFlags=SoRenderManager::Superimposition::AUTOREDRAW |
    SoRenderManager::Superimposition::ZBUFFERON |
    SoRenderManager::Superimposition::BACKGROUND;
  auto *background=manager.addSuperimposition(blue.layer,backgroundFlags);
  CoinRenderManagerAdapter adapter(manager,SbVec2i32(side,side),options(caps,samples));
  auto *target=adapter.getSceneManager()->getRenderTarget();
  if(!check(target && target->getOptions().renderer==caps.renderer &&
     target->getOptions().sampleCount==samples,"background target API/count")) {
    manager.removeSuperimposition(background);
    manager.setSceneGraph(nullptr);return false;
  }
  target->setDepthReadbackEnabled(TRUE);
  Frame base,withForeground,disabled,restored;
  bool ok=capture(adapter,base,"background then depth-clearing base");
  auto *foreground=manager.addSuperimposition(scene.layer,
    SoRenderManager::Superimposition::AUTOREDRAW);
  ok &= capture(adapter,withForeground,"background/base/foreground ZOFF");
  foreground->setEnabled(FALSE);
  ok &= capture(adapter,disabled,"foreground disabled over background");
  manager.removeSuperimposition(foreground);
  ok &= capture(adapter,restored,"foreground removed over background");
  if(ok) {
    const size_t outer=at(24,20);
    std::cout << "msaa_layers background samples=" << samples
              << " outside_rgba=" << int(base.rgba[4*outer]) << ','
              << int(base.rgba[4*outer+1]) << ','
              << int(base.rgba[4*outer+2]) << ','
              << int(base.rgba[4*outer+3])
              << " outside_depth=" << base.depth[outer] << '\n';
    ok &= check(pixel(base,8,24,255,0,.5f,samples) &&
      pixel(base,24,20,0,0,1.f,samples) &&
      base.rgba[4*outer+2]>250 &&
      pixel(withForeground,8,24,0,255,.5f,samples),
      "background blue survives outside base; base clears depth; ZOFF preserves depth");
    ok &= check(base.rgba==disabled.rgba && base.depth==disabled.depth &&
      base.rgba==restored.rgba && base.depth==restored.depth &&
      base.serial+1==withForeground.serial &&
      withForeground.serial+1==disabled.serial &&
      disabled.serial+1==restored.serial,
      "background frame restores exactly after disabling/removing foreground");
  }
  manager.removeSuperimposition(background);
  manager.setSceneGraph(nullptr);return ok;
}
bool cpuCaptureOneSample() {
  Scene scene;SoRenderManager manager;
  manager.setSceneGraph(scene.base);
  manager.setViewportRegion(SbViewportRegion(side,side));
  manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.setBackgroundColor(SbColor4f(0,0,0,0));
  CoinRenderOptions selected;selected.sampleCount=1;
  CoinRenderManagerAdapter adapter(manager,SbVec2i32(side,side),selected);
  auto *target=adapter.getSceneManager()->getRenderTarget();
  target->setDepthReadbackEnabled(TRUE);
  target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
  Frame base,layered;
  bool ok=capture(adapter,base,"CPU capture 1x");
  const unsigned flags=SoRenderManager::Superimposition::AUTOREDRAW |
    SoRenderManager::Superimposition::ZBUFFERON |
    SoRenderManager::Superimposition::CLEARZBUFFER;
  auto *layer=manager.addSuperimposition(scene.layer,flags);
  ok &= capture(adapter,layered,"CPU layer 1x");
  if(ok)ok &= check(pixel(base,8,24,255,0,.5f,1) &&
    pixel(layered,8,24,0,255,.75f,1) &&
    layered.serial==base.serial+1,
    "CPU captures one-sample annotation/layer semantics without claiming MSAA raster");
  manager.removeSuperimposition(layer);manager.setSceneGraph(nullptr);return ok;
}
bool rejectionProfile(const CoinRenderCapabilities &caps) {
  Scene base;SoRenderManager manager;
  manager.setSceneGraph(base.base);
  manager.setViewportRegion(SbViewportRegion(side,side));
  manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.setBackgroundColor(SbColor4f(0,0,0,0));
  CoinRenderManagerAdapter adapter(manager,SbVec2i32(side,side),options(caps,4));
  auto *target=adapter.getSceneManager()->getRenderTarget();
  if(!check(target && target->getOptions().sampleCount==4 &&
      target->getOptions().renderer==caps.renderer,"rejection GPU target"))
    return false;
  target->setDepthReadbackEnabled(TRUE);
  Frame stable;bool ok=capture(adapter,stable,"rejection anchor");
  auto rejected=[&](const char *label) {
    const auto status=adapter.render();
    Frame after;
    target->readbackRGBA(after.rgba);target->readbackDepth(after.depth);
    after.serial=target->getLastSubmissionSerial();
    std::cout << "msaa_layers rejected=" << label << " status=" << int(status)
              << " error=" << adapter.getLastError().getString()
              << " serial=" << after.serial << '\n';
    return check(status==CoinRenderAction::UNSUPPORTED &&
      after.rgba==stable.rgba && after.depth==stable.depth &&
      after.serial==stable.serial,"unsupported layer preserves publication");
  };
  auto *alphaLayer=manager.addSuperimposition(base.layer,
    SoRenderManager::Superimposition::AUTOREDRAW);
  manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
  base.layerMaterial->transparency=.5f;
  Frame blended;
  ok &= capture(adapter,blended,"effective BLEND alpha .5 supported");
  if(blended.rgba.size()==size_t(side)*side*4 &&
     blended.depth.size()==size_t(side)*side){
    const size_t p=at(8,24);
    std::cout << "msaa_layers blend_witness rgba="
              << int(blended.rgba[4*p]) << ',' << int(blended.rgba[4*p+1])
              << ',' << int(blended.rgba[4*p+2]) << ','
              << int(blended.rgba[4*p+3]) << " depth=" << blended.depth[p] << '\n';
    ok &= check(std::abs(int(blended.rgba[4*p])-128)<=3 &&
      std::abs(int(blended.rgba[4*p+1])-128)<=3 &&
      blended.depth[p]==stable.depth[p] && blended.serial==stable.serial+1,
      "translucent foreground blends per sample and publishes one frame");
  }
  base.layerMaterial->transparency=0.f;
  manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::SCREEN_DOOR);
  manager.removeSuperimposition(alphaLayer);
  Frame restoredAnchor;
  ok &= capture(adapter,restoredAnchor,"opaque anchor restored after BLEND");
  ok &= check(restoredAnchor.rgba==stable.rgba &&
    restoredAnchor.depth==stable.depth &&
    restoredAnchor.serial==blended.serial+1,
    "removing translucent layer restores exact anchor and one publication");
  stable=restoredAnchor;
  if(caps.backend!=COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION) {
    manager.setSceneGraph(nullptr);
    return ok;
  }
  if(!check(bool(target->getPimpl()->backend),"budget anchor has a prepared backend")){manager.setSceneGraph(nullptr);return false;}
  auto*probe=new view_budget_test::Probe(std::move(target->getPimpl()->backend));target->getPimpl()->backend.reset(probe);
  std::vector<std::unique_ptr<Scene>>scenes;std::vector<SoRenderManager::Superimposition*>layers;
  auto setCount=[&](unsigned count){
    while(layers.size()<count){scenes.emplace_back(new Scene);scenes.back()->setZ(.5f);
      layers.push_back(manager.addSuperimposition(scenes.back()->layer,SoRenderManager::Superimposition::AUTOREDRAW));}
    while(layers.size()>count){manager.removeSuperimposition(layers.back());layers.pop_back();scenes.pop_back();}
  };
  auto inspect=[&](unsigned count){setCount(count);
    const auto result=view_budget_test::managerPlan(*probe,*target,[&](){return adapter.render();},ok);
    std::cout<<"msaa_layers negotiated_preflight layers="<<count<<" status="<<int(result.status)<<" diagnostic="<<result.diagnostic<<'\n';return result;
  };
  unsigned fit=0,excess=0;std::string why;
  const bool foundBoundary=view_budget_test::boundary(inspect,fit,excess,why);
  ok &= check(foundBoundary,("finite negotiated manager view boundary: "+why).c_str());
  if(ok){
    setCount(fit);Frame admitted;const auto status=adapter.render();target->readbackRGBA(admitted.rgba);target->readbackDepth(admitted.depth);admitted.serial=target->getLastSubmissionSerial();
    ok &= check(status==CoinRenderAction::SUCCESS&&admitted.rgba.size()==size_t(side)*side*4&&admitted.depth.size()==size_t(side)*side&&admitted.serial==stable.serial+1,
      "last negotiated view count publishes one new complete frame");stable=admitted;
    if(ok){setCount(excess);const auto rejectedStatus=adapter.render();Frame rejected;
      target->readbackRGBA(rejected.rgba);target->readbackDepth(rejected.depth);rejected.serial=target->getLastSubmissionSerial();
      const std::string diagnostic=adapter.getLastError().getString();
      std::cout<<"msaa_layers view_budget accepted_layers="<<fit<<" rejected_layers="<<excess<<" status="<<int(rejectedStatus)<<" error="<<diagnostic<<'\n';
      ok &= check(rejectedStatus==CoinRenderAction::UNSUPPORTED&&(diagnostic.find("view")!=std::string::npos||diagnostic.find("budget")!=std::string::npos)&&
        rejected.rgba==stable.rgba&&rejected.depth==stable.depth&&rejected.serial==stable.serial,"negotiated BGFX view excess rejects atomically");
      setCount(fit);Frame recovered;ok &= capture(adapter,recovered,"negotiated view budget recovery")&&
        check(recovered.rgba==stable.rgba&&recovered.depth==stable.depth&&recovered.serial==stable.serial+1,"last negotiated view count recovers exactly");
    }
  }
  setCount(0);
  manager.setSceneGraph(nullptr);return ok;
}
template<class NativeAction>
struct CountHighlight : NativeAction {
  int calls=0;
  void apply(SoNode *node) override {++calls;NativeAction::apply(node);}
};
template<class NativeAction>
bool highlightProfile(const CoinRenderCapabilities &caps,uint32_t samples,
                      const char *kind) {
  Scene scene;
  // Reparent the authored face into a selection without changing its geometry.
  auto *selected=new SoSeparator;
  selected->addChild(scene.base->getChild(2));
  selected->addChild(scene.base->getChild(3));
  selected->addChild(scene.base->getChild(4));
  SoNode *annotation=scene.base->getChild(5);
  annotation->ref();
  while(scene.base->getNumChildren()>2)scene.base->removeChild(2);
  auto *selection=new SoSelection;
  selection->addChild(selected);scene.base->addChild(selection);
  scene.base->addChild(annotation);annotation->unref();
  selection->select(selected);
  scene.baseMaterial->diffuseColor.setValue(0,0,1);
  CountHighlight<NativeAction> highlight;
  highlight.setColor(SbColor(1,0,0));highlight.setLineWidth(3);
  SoRenderManager manager;
  manager.setViewportRegion(SbViewportRegion(side,side));
  manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.setBackgroundColor(SbColor4f(0,0,0,0));
  manager.setSceneGraph(scene.base);manager.setGLRenderAction(&highlight);
  CoinRenderManagerAdapter adapter(manager,SbVec2i32(side,side),options(caps,samples));
  auto *target=adapter.getSceneManager()->getRenderTarget();
  if(!check(target && target->getOptions().renderer==caps.renderer &&
     target->getOptions().sampleCount==samples,"highlight target/API/samples"))
    return false;
  target->setDepthReadbackEnabled(TRUE);
  Frame first,layered,cyan,restored;
  bool ok=capture(adapter,first,"highlight red");
  auto *layer=manager.addSuperimposition(scene.layer,
    SoRenderManager::Superimposition::AUTOREDRAW);
  scene.setZ(.5f);ok &= capture(adapter,layered,"highlight foreground");
  highlight.setColor(SbColor(0,1,1));
  ok &= capture(adapter,cyan,"highlight cyan");
  highlight.setColor(SbColor(1,0,0));
  ok &= capture(adapter,restored,"highlight red restored");
  manager.removeSuperimposition(layer);
  if(ok) {
    size_t red=0,blue=0,green=0,cyanPixels=0;
    for(size_t p=0;p<size_t(side)*side;++p) {
      const size_t i=4*p;
      red+=(first.rgba[i]>first.rgba[i+1]+70 && first.rgba[i]>120);
      blue+=(first.rgba[i+2]>first.rgba[i]+70 && first.rgba[i+2]>120);
      green+=(layered.rgba[i+1]>layered.rgba[i]+70 &&
              layered.rgba[i+1]>layered.rgba[i+2]+70);
      cyanPixels+=(cyan.rgba[i+1]>120 && cyan.rgba[i+2]>120 &&
                   cyan.rgba[i]<60);
    }
    std::cout << "msaa_layers highlight=" << kind << " samples=" << samples
              << " red=" << red << " blue=" << blue
              << " green=" << green << " cyan=" << cyanPixels << '\n';
    ok &= check(red>8 && blue>8 && green>8 && cyanPixels>8,
      "highlight and foreground remain visible with independent colors");
    ok &= check(layered.rgba==restored.rgba && layered.depth==restored.depth &&
      first.serial+1==layered.serial && layered.serial+1==cyan.serial &&
      cyan.serial+1==restored.serial,
      "foreground-preserving highlight style A/B/A and one publication per frame");
    ok &= check(coverage(first,samples),"highlighted base retains sample coverage");
  }
  ok &= check(highlight.calls==0,"manager never applies native highlight GL action");
  manager.setGLRenderAction(nullptr);manager.setSceneGraph(nullptr);
  return ok;
}
bool run(const std::string &mode) {
  if(mode=="--cpu-1x")return cpuCaptureOneSample();
  const auto caps=capabilities();
  if(!check(caps.gpu_available && caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN &&
    (caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION ||
     caps.backend==COIN_RENDER_EXPERIMENTAL_RUST),
    "real identified GPU required"))return false;
  if(mode=="--offscreen") {
    Scene anchorScene;SoRenderManager anchorManager;
    anchorManager.setSceneGraph(anchorScene.base);
    anchorManager.setViewportRegion(SbViewportRegion(side,side));
    anchorManager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
    anchorManager.setBackgroundColor(SbColor4f(0,0,0,0));
    CoinRenderManagerAdapter anchor(anchorManager,SbVec2i32(side,side),options(caps,1));
    auto *anchorTarget=anchor.getSceneManager()->getRenderTarget();
    bool ok=check(anchorTarget && anchorTarget->getOptions().renderer==caps.renderer &&
      anchorTarget->getOptions().sampleCount==1,
      "live one-sample target anchors shared runtime");
    if(ok) {
      anchorTarget->setDepthReadbackEnabled(TRUE);
      Frame initial;
      ok &= capture(anchor,initial,"live 1x runtime anchor");
      if(ok)ok &= check(pixel(initial,8,24,255,0,.5f,1),
        "live one-sample anchor renders before sample-count alternation");
    }
    if(ok)for(uint32_t samples:{1u,2u,4u,2u,1u}) {
      const bool current=layerProfile(caps,samples);
      ok &= current;
    }
    if(ok)for(uint32_t samples:{1u,2u,4u}) {
      const bool current=backgroundProfile(caps,samples);
      ok &= current;
    }
    anchorManager.setSceneGraph(nullptr);
    return ok;
  }
  if(mode=="--manager-highlight") {
    bool ok=true;
    for(uint32_t samples:{2u,4u}) {
      ok &= highlightProfile<SoBoxHighlightRenderAction>(caps,samples,"box");
      ok &= highlightProfile<SoLineHighlightRenderAction>(caps,samples,"line");
    }
    return ok;
  }
  if(mode=="--rejections")return rejectionProfile(caps);
  return false;
}
}
int main(int argc,char **argv) {
  if(argc!=2)return 2;
  SoDB::init();CoinRenderAction::initClass();
  SoInteraction::init();
  if(SoBoxHighlightRenderAction::getClassTypeId().isBad())
    SoBoxHighlightRenderAction::initClass();
  if(SoLineHighlightRenderAction::getClassTypeId().isBad())
    SoLineHighlightRenderAction::initClass();
  LayerViewport::initClass();
  return run(argv[1])?0:1;
}
