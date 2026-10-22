#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoInteraction.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoBoxHighlightRenderAction.h>
#include <Inventor/actions/SoLineHighlightRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoSelection.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
namespace {
using A=CoinRenderAction;
constexpr int side=64;
class PartialViewport : public SoNode {
  SO_NODE_HEADER(PartialViewport);
public:
  static void initClass(){SO_NODE_INIT_CLASS(PartialViewport,SoNode,"Node");}
  PartialViewport(){SO_NODE_CONSTRUCTOR(PartialViewport);}
  bool enabled=false;
  SbViewportRegion region;
  void callback(SoCallbackAction *action) override {
    if(enabled)SoViewportRegionElement::set(action->getState(),region);
  }
  void GLRender(SoGLRenderAction *action) override {
    if(enabled)SoViewportRegionElement::set(action->getState(),region);
  }
protected:
  ~PartialViewport() override=default;
};
SO_NODE_SOURCE(PartialViewport);
bool check(bool yes,const char *label) {if(!yes)std::cerr<<"msaa_transparency FAIL "<<label<<'\n';return yes;}
CoinRenderCapabilities capabilities(){
  CoinRenderCapabilities caps{};
  const bool available=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
    &caps,sizeof(caps))==0;
  if(!available)caps.gpu_available=false;
  std::cout<<"msaa_transparency backend="<<caps.backend<<" renderer="<<caps.renderer
           <<" vendor="<<caps.vendor_id<<" adapter="<<caps.adapter_name<<'\n';
  return caps;
}
SoOrthographicCamera *camera(){auto *node=new SoOrthographicCamera;
  node->position.setValue(0,0,2);node->height=2;
  node->nearDistance=1;node->farDistance=3;return node;}
struct Scene {
  SoSeparator *root=new SoSeparator;
  SoMaterial *red=nullptr,*green=nullptr;
  SoSeparator *nearGroup=nullptr,*farGroup=nullptr;
  Scene(){
    root->ref();root->addChild(camera());
    auto *unlit=new SoLightModel;unlit->model=SoLightModel::BASE_COLOR;root->addChild(unlit);
    red=quad(.5f,SbColor(1,0,0),&nearGroup);
    green=quad(-.5f,SbColor(0,1,0),&farGroup);
    quad(-.8f,SbColor(0,0,1),nullptr);
  }
  SoMaterial *quad(float z,const SbColor &color,SoSeparator **groupOut){
    auto *group=new SoSeparator;root->addChild(group);
    if(groupOut)*groupOut=group;
    auto *material=new SoMaterial;material->diffuseColor=color;group->addChild(material);
    auto *coords=new SoCoordinate3;
    const SbVec3f vertices[]={SbVec3f(-.75f,-.75f,z),SbVec3f(.75f,-.75f,z),
      SbVec3f(.75f,.75f,z),SbVec3f(-.75f,.75f,z)};
    coords->point.setValues(0,4,vertices);group->addChild(coords);
    auto *face=new SoIndexedFaceSet;
    const int32_t indices[]={0,1,2,3,-1};face->coordIndex.setValues(0,5,indices);
    group->addChild(face);return material;
  }
  ~Scene(){root->unref();}
};
struct Frame {std::vector<uint8_t>rgba;std::vector<float>depth;uint64_t serial=0;};
struct Harness {
  std::unique_ptr<CoinRenderTarget> target;
  A action;
  Harness(const CoinRenderCapabilities &caps,uint32_t samples,bool cpu)
    : target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),[&]{
        CoinRenderOptions opt;opt.sampleCount=samples;
        if(!cpu)opt.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
        return opt;}())), action(SbViewportRegion(side,side)) {
    target->setDepthReadbackEnabled(TRUE);
    if(cpu)target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
    action.setRenderTarget(target.get());action.setBackgroundColor(SbColor4f(0,0,0,1));
  }
  ~Harness(){action.setRenderTarget(nullptr);}
  bool render(SoNode *root,A::TransparencyType mode,Frame &out,const char *phase){
    action.setTransparencyType(mode);action.apply(root);
    target->readbackRGBA(out.rgba);target->readbackDepth(out.depth);
    out.serial=target->getLastSubmissionSerial();
    const bool good=action.getLastStatus()==A::SUCCESS &&
      out.rgba.size()==size_t(side)*side*4 && out.depth.size()==size_t(side)*side &&
      out.serial>0;
    std::cout<<"msaa_transparency phase="<<phase<<" mode="<<int(mode)
             <<" status="<<int(action.getLastStatus())<<" serial="<<out.serial
             <<" error="<<action.getLastError().getString()<<'\n';
    return check(good,"frame publishes on identified target");
  }
};
size_t index(int x,int y){return size_t(y)*side+x;}
bool rgbAt(const Frame &frame,int x,int y,const std::array<float,3> &want,int tolerance=3){
  const size_t p=index(x,y);bool good=true;
  for(size_t c=0;c<3;++c)good &= std::abs(int(frame.rgba[4*p+c])-
    int(std::lround(want[c]*255)))<=tolerance;
  if(!good)std::cerr<<"msaa_transparency witness="<<x<<','<<y<<" actual="
    <<int(frame.rgba[4*p])<<','<<int(frame.rgba[4*p+1])<<','<<int(frame.rgba[4*p+2])
    <<" expected="<<want[0]*255<<','<<want[1]*255<<','<<want[2]*255<<'\n';
  return good;
}
bool depthAt(const Frame &frame,int x,int y,float expected,uint32_t samples){
  const float actual=frame.depth[index(x,y)];
  const bool good=samples==1?std::abs(actual-expected)<1e-6f:actual==expected;
  if(!good)std::cerr<<"msaa_transparency depth="<<x<<','<<y
    <<" actual="<<actual<<" expected="<<expected<<" samples="<<samples<<'\n';
  return good;
}
struct Mode {A::TransparencyType value;const char *name;bool immediate;bool additive;};
const Mode modes[]={
  {A::BLEND,"blend",true,false},{A::ADD,"add",true,true},
  {A::DELAYED_BLEND,"delayed-blend",false,false},
  {A::DELAYED_ADD,"delayed-add",false,true},
  {A::SORTED_OBJECT_BLEND,"sorted-blend",false,false},
  {A::SORTED_OBJECT_ADD,"sorted-add",false,true},
  {A::SORTED_OBJECT_SORTED_TRIANGLE_BLEND,"sorted-triangle-blend",false,false},
  {A::SORTED_OBJECT_SORTED_TRIANGLE_ADD,"sorted-triangle-add",false,true},
};
std::array<float,3> expected(const Mode &mode,float alpha){
  if(alpha==1.f)return {{1,0,0}};
  if(mode.immediate)return {{alpha,0,0}};
  if(mode.additive)return {{alpha,alpha,1}};
  if(mode.value==A::DELAYED_BLEND)
    return {{alpha*(1-alpha),alpha,(1-alpha)*(1-alpha)}};
  return {{alpha,alpha*(1-alpha),(1-alpha)*(1-alpha)}};
}
bool coreCase(const CoinRenderCapabilities &caps,uint32_t samples,bool cpu){
  Harness run(caps,samples,cpu);Scene scene;
  if(!cpu && !check(run.target->getOptions().renderer==caps.renderer &&
    run.target->getOptions().sampleCount==samples,"real GPU API and sample count"))return false;
  bool ok=true;
  for(const auto &mode:modes)for(float alpha:{0.f,.25f,.5f,.75f,1.f}){
    scene.red->transparency=1-alpha;scene.green->transparency=1-alpha;
    Frame first;if(!run.render(scene.root,mode.value,first,mode.name)){ok=false;continue;}
    const auto want=expected(mode,alpha);
    const float wantDepth=(mode.immediate || alpha==1.f)? .25f : .9f;
    ok &= check(rgbAt(first,40,31,want) &&
      depthAt(first,40,31,wantDepth,samples) &&
      first.rgba[4*index(40,31)+3]==255,
      "per-sample interior source/destination equation and depth policy");
    // Sorting must use geometric depth, independent of the two translucent
    // objects' traversal order; immediate modes are intentionally excluded.
    if((mode.value==A::SORTED_OBJECT_BLEND ||
        mode.value==A::SORTED_OBJECT_ADD ||
        mode.value==A::SORTED_OBJECT_SORTED_TRIANGLE_BLEND ||
        mode.value==A::SORTED_OBJECT_SORTED_TRIANGLE_ADD) && alpha==.5f){
      scene.farGroup->ref();
      scene.root->removeChild(scene.farGroup);
      scene.root->insertChild(scene.farGroup,2);
      Frame inverse;
      ok &= run.render(scene.root,mode.value,inverse,"inverse traversal") &&
        check(rgbAt(inverse,40,31,want) && inverse.serial==first.serial+1,
          "sorted result is invariant to traversal inversion");
      scene.root->removeChild(scene.farGroup);
      scene.root->insertChild(scene.farGroup,3);
      scene.farGroup->unref();
    }
  }
  return ok;
}
bool edgeCoverage(const CoinRenderCapabilities &caps,uint32_t samples,bool cpu){
  Harness run(caps,samples,cpu);
  run.action.setBackgroundColor(SbColor4f(0,0,0,0));
  auto *root=new SoSeparator;root->ref();root->addChild(camera());
  auto *unlit=new SoLightModel;unlit->model=SoLightModel::BASE_COLOR;root->addChild(unlit);
  auto *material=new SoMaterial;material->diffuseColor.setValue(1,0,0);
  material->transparency=.5f;root->addChild(material);
  auto *points=new SoCoordinate3;
  const SbVec3f vertices[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),SbVec3f(-1,.83f,0)};
  points->point.setValues(0,3,vertices);root->addChild(points);
  auto *face=new SoIndexedFaceSet;const int32_t indices[]={0,1,2,-1};
  face->coordIndex.setValues(0,4,indices);root->addChild(face);
  Frame frame;bool ok=run.render(root,A::BLEND,frame,"per-sample blend coverage");
  if(ok){
    const int fullRed=frame.rgba[4*index(8,48)];
    const int fullAlpha=frame.rgba[4*index(8,48)+3];
    size_t quarter=0,half=0,three=0;
    for(size_t p=0;p<frame.depth.size();++p){
      const int red=frame.rgba[4*p],alpha=frame.rgba[4*p+3];
      if(red==0 || red==fullRed)continue;
      const int k=int(std::lround(double(red)*samples/fullRed));
      if(k>0 && k<int(samples)){
        ok &= check(std::abs(red-int(std::lround(double(fullRed)*k/samples)))<=3 &&
          std::abs(alpha-int(std::lround(double(fullAlpha)*k/samples)))<=3 &&
          frame.depth[p]==.5f,
          "blend resolves RGB/alpha per covered sample and minimum depth");
        quarter+=(k==1);half+=(2*k==int(samples));three+=(samples==4 && k==3);
      }
    }
    std::cout<<"msaa_transparency coverage samples="<<samples<<" full_rgb="<<fullRed
             <<" full_alpha="<<fullAlpha<<" quarter="<<quarter
             <<" half="<<half<<" threequarter="<<three<<'\n';
    ok &= check(std::abs(fullRed-128)<=2 && std::abs(fullAlpha-128)<=2 &&
      (samples==1?quarter+half+three==0:
       samples==2?half>0:quarter>0 && three>0),
      "translucent source alpha is distinct from requested k/N coverage");
  }
  root->unref();return ok;
}
bool textureCase(const CoinRenderCapabilities &caps,uint32_t samples,bool cpu){
  Harness run(caps,samples,cpu);
  run.action.setBackgroundColor(SbColor4f(0,0,0,0));
  auto *root=new SoSeparator;root->ref();root->addChild(camera());
  auto *unlit=new SoLightModel;unlit->model=SoLightModel::BASE_COLOR;root->addChild(unlit);
  auto *quality=new SoComplexity;quality->textureQuality=.1f;root->addChild(quality);
  auto *material=new SoMaterial;material->diffuseColor.setValue(1,0,0);
  material->transparency=.5f;root->addChild(material);
  auto *texture=new SoTexture2;
  const uint8_t texels[]={255,128,0,128, 32,255,64,64,
                          20,30,255,255, 255,255,255,0};
  texture->image.setValue(SbVec2s(2,2),4,texels);root->addChild(texture);
  auto *uv=new SoTextureCoordinate2;root->addChild(uv);
  auto *coords=new SoCoordinate3;
  const SbVec3f vertices[]={SbVec3f(-.75f,-.75f,0),SbVec3f(.75f,-.75f,0),
    SbVec3f(.75f,.75f,0),SbVec3f(-.75f,.75f,0)};
  coords->point.setValues(0,4,vertices);root->addChild(coords);
  auto *face=new SoIndexedFaceSet;const int32_t indices[]={0,1,2,3,-1};
  face->coordIndex.setValues(0,5,indices);root->addChild(face);
  bool ok=true;
  for(const auto model:{SoTexture2::MODULATE,SoTexture2::REPLACE})
    for(const auto sample:{SbVec2f(.25f,.25f),SbVec2f(.75f,.25f)}){
      texture->model=model;
      const SbVec2f values[]={sample,sample,sample,sample};
      uv->point.setValues(0,4,values);
      Frame frame;ok &= run.render(root,A::BLEND,frame,"RGBA texture model/texel");
      if(frame.rgba.size()!=size_t(side)*side*4)continue;
      const bool first=sample[0]<.5f;
      const float ta=first?128.f/255:64.f/255;
      const std::array<float,3> tc=first?
        std::array<float,3>{{1,128.f/255,0}}:
        std::array<float,3>{{32.f/255,1,64.f/255}};
      const float alpha=ta*(model==SoTexture2::MODULATE?.5f:1.f);
      const std::array<float,3> want=model==SoTexture2::MODULATE?
        std::array<float,3>{{tc[0]*alpha,0,0}}:
        std::array<float,3>{{tc[0]*alpha,tc[1]*alpha,tc[2]*alpha}};
      ok &= check(rgbAt(frame,32,32,want) &&
        std::abs(int(frame.rgba[4*index(32,32)+3])-int(std::lround(alpha*255)))<=3,
        "MODULATE/REPLACE RGBA texel and material alpha stay distinct");
    }
  root->unref();return ok;
}
bool fogClipCase(const CoinRenderCapabilities &caps,uint32_t samples,bool cpu){
  Harness run(caps,samples,cpu);
  auto *root=new SoSeparator;root->ref();root->addChild(camera());
  auto *unlit=new SoLightModel;unlit->model=SoLightModel::BASE_COLOR;root->addChild(unlit);
  auto *fog=new SoEnvironment;fog->fogType=SoEnvironment::NONE;
  fog->fogColor.setValue(0,0,1);fog->fogVisibility=4;root->addChild(fog);
  auto *clip=new SoClipPlane;clip->plane=SbPlane(SbVec3f(1,0,0),0);
  clip->on=FALSE;root->addChild(clip);
  auto *material=new SoMaterial;material->diffuseColor.setValue(1,0,0);
  material->transparency=.5f;root->addChild(material);
  auto *coords=new SoCoordinate3;
  const SbVec3f quad[]={SbVec3f(-.75f,-.75f,-.5f),SbVec3f(.75f,-.75f,-.5f),
    SbVec3f(.75f,.75f,-.5f),SbVec3f(-.75f,.75f,-.5f)};
  coords->point.setValues(0,4,quad);root->addChild(coords);
  auto *face=new SoIndexedFaceSet;const int32_t indices[]={0,1,2,3,-1};
  face->coordIndex.setValues(0,5,indices);root->addChild(face);
  Frame plain,fogged,clipped,restored;
  bool ok=run.render(root,A::BLEND,plain,"translucent fog/clip plain");
  fog->fogType=SoEnvironment::HAZE;
  ok &= run.render(root,A::BLEND,fogged,"translucent fog active");
  clip->on=TRUE;
  ok &= run.render(root,A::BLEND,clipped,"translucent fog plus clip");
  clip->on=FALSE;fog->fogType=SoEnvironment::NONE;
  ok &= run.render(root,A::BLEND,restored,"translucent fog/clip restored");
  if(ok){
    const size_t left=index(16,32),right=index(48,32);
    std::cout<<"msaa_transparency fog_clip samples="<<samples
             <<" plain_right="<<int(plain.rgba[4*right])<<','
             <<int(plain.rgba[4*right+1])<<','<<int(plain.rgba[4*right+2])
             <<" fog_right="<<int(fogged.rgba[4*right])<<','
             <<int(fogged.rgba[4*right+1])<<','<<int(fogged.rgba[4*right+2])
             <<" clip_left="<<int(clipped.rgba[4*left])<<','
             <<int(clipped.rgba[4*left+1])<<','<<int(clipped.rgba[4*left+2])
             <<" clip_right="<<int(clipped.rgba[4*right])<<','
             <<int(clipped.rgba[4*right+1])<<','<<int(clipped.rgba[4*right+2])
             <<'\n';
    const bool own=plain.rgba[4*left]>120 && plain.rgba[4*right]>120 &&
      fogged.rgba[4*right]<plain.rgba[4*right]-10 &&
      fogged.rgba[4*right+2]>plain.rgba[4*right+2]+10 &&
      clipped.rgba[4*left]==0 && clipped.rgba[4*right]>30 &&
      plain.rgba==restored.rgba && plain.depth==restored.depth &&
      plain.serial+1==fogged.serial && fogged.serial+1==clipped.serial &&
      clipped.serial+1==restored.serial;
    ok &= check(own,"BLEND fog visibly changes color, clip removes one half, A/B/A exact");
  }
  // In the 1x profile the CPU backend is the portable numerical oracle.
  // For MSAA, compare only fully covered interior pixels to the same CPU
  // material/fog/clip state; edge coverage has its own k/N oracle.
  if(ok && !cpu){
    Harness oracle(caps,1,true);Frame reference;
    fog->fogType=SoEnvironment::HAZE;clip->on=TRUE;
    ok &= oracle.render(root,A::BLEND,reference,"CPU fog/clip oracle");
    for(const size_t p:{index(16,32),index(48,32)})
      for(size_t c=0;c<4;++c)
        ok &= check(std::abs(int(clipped.rgba[4*p+c])-int(reference.rgba[4*p+c]))<=3,
          "MSAA interior fog/clip RGBA agrees with independent CPU 1x oracle");
  }
  root->unref();return ok;
}
bool layerCase(const CoinRenderCapabilities &caps,uint32_t samples){
  auto *base=new SoSeparator;base->ref();base->addChild(camera());
  auto *unlit=new SoLightModel;unlit->model=SoLightModel::BASE_COLOR;base->addChild(unlit);
  auto *red=new SoMaterial;red->diffuseColor.setValue(1,0,0);base->addChild(red);
  auto *baseCoords=new SoCoordinate3;
  const SbVec3f triangle[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),SbVec3f(-1,.83f,0)};
  baseCoords->point.setValues(0,3,triangle);base->addChild(baseCoords);
  auto *baseFace=new SoIndexedFaceSet;const int32_t tri[]={0,1,2,-1};
  baseFace->coordIndex.setValues(0,4,tri);base->addChild(baseFace);
  auto *note=new SoAnnotation;
  auto *yellow=new SoMaterial;yellow->diffuseColor.setValue(1,1,0);note->addChild(yellow);
  auto *noteCoords=new SoCoordinate3;
  const SbVec3f noteP[]={SbVec3f(.74f,.74f,-.8f),SbVec3f(.98f,.74f,-.8f),
    SbVec3f(.98f,.98f,-.8f),SbVec3f(.74f,.98f,-.8f)};
  noteCoords->point.setValues(0,4,noteP);note->addChild(noteCoords);
  auto *noteFace=new SoIndexedFaceSet;const int32_t quad[]={0,1,2,3,-1};
  noteFace->coordIndex.setValues(0,5,quad);note->addChild(noteFace);base->addChild(note);
  auto *layer=new SoSeparator;layer->ref();auto *vp=new PartialViewport;
  vp->region=SbViewportRegion(side,side);vp->region.setViewportPixels(32,0,32,side);
  layer->addChild(vp);layer->addChild(camera());
  auto *layerUnlit=new SoLightModel;layerUnlit->model=SoLightModel::BASE_COLOR;
  layer->addChild(layerUnlit);
  auto *green=new SoMaterial;green->diffuseColor.setValue(0,1,0);
  green->transparency=.5f;layer->addChild(green);
  auto *layerCoords=new SoCoordinate3;
  const SbVec3f greenP[]={SbVec3f(-.7f,-.7f,-.5f),SbVec3f(.7f,-.7f,-.5f),
    SbVec3f(.7f,.7f,-.5f),SbVec3f(-.7f,.7f,-.5f)};
  layerCoords->point.setValues(0,4,greenP);layer->addChild(layerCoords);
  auto *layerFace=new SoIndexedFaceSet;layerFace->coordIndex.setValues(0,5,quad);
  layer->addChild(layerFace);
  SoRenderManager manager;manager.setSceneGraph(base);
  manager.setViewportRegion(SbViewportRegion(side,side));
  manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.setBackgroundColor(SbColor4f(0,0,0,1));
  manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
  CoinRenderOptions options;options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  options.sampleCount=samples;
  CoinRenderManagerAdapter adapter(manager,SbVec2i32(side,side),options);
  auto *target=adapter.getSceneManager()->getRenderTarget();
  target->setDepthReadbackEnabled(TRUE);
  auto capture=[&](Frame &out,const char *phase){
    const auto status=adapter.render();
    target->readbackRGBA(out.rgba);target->readbackDepth(out.depth);
    out.serial=target->getLastSubmissionSerial();
    std::cout<<"msaa_transparency layer_phase="<<phase<<" samples="<<samples
             <<" status="<<int(status)<<" serial="<<out.serial
             <<" error="<<adapter.getLastError().getString()<<'\n';
    return check(status==A::SUCCESS && out.rgba.size()==size_t(side)*side*4 &&
      out.depth.size()==size_t(side)*side && out.serial>0,"manager layered frame");
  };
  bool ok=check(target && target->getOptions().renderer==caps.renderer &&
    target->getOptions().sampleCount==samples,"manager actual API/count");
  Frame bare,zoff,disabled,restored,partial;
  if(ok)ok &= capture(bare,"bare");
  auto *foreground=manager.addSuperimposition(layer,
    SoRenderManager::Superimposition::AUTOREDRAW);
  if(ok)ok &= capture(zoff,"translucent foreground ZOFF");
  foreground->setEnabled(FALSE);
  if(ok)ok &= capture(disabled,"foreground disabled");
  manager.removeSuperimposition(foreground);
  if(ok)ok &= capture(restored,"foreground removed");
  vp->enabled=true;
  foreground=manager.addSuperimposition(layer,
    SoRenderManager::Superimposition::AUTOREDRAW |
    SoRenderManager::Superimposition::ZBUFFERON |
    SoRenderManager::Superimposition::CLEARZBUFFER);
  if(ok)ok &= capture(partial,"partial clear and translucent layer");
  manager.removeSuperimposition(foreground);
  if(ok){
    const size_t noteIndex=index(58,6);
    ok &= check(rgbAt(bare,16,48,{{1,0,0}}) && depthAt(bare,16,48,.5f,samples) &&
      rgbAt(zoff,16,48,{{.5f,.5f,0}}) && depthAt(zoff,16,48,.5f,samples) &&
      bare.rgba==disabled.rgba && bare.depth==disabled.depth &&
      bare.rgba==restored.rgba && bare.depth==restored.depth &&
      bare.rgba[4*noteIndex]>245 && bare.rgba[4*noteIndex+1]>245,
      "annotation, translucent ZOFF and exact manager A/B/A");
    // The half-width viewport has aspect .5; ADJUST_CAMERA widens world
    // Y. Pixel (40,40) is inside its green quad, while (16,48) is outside.
    ok &= check(rgbAt(partial,16,48,{{1,0,0}}) &&
      depthAt(partial,16,48,.5f,samples) &&
      partial.rgba[4*index(40,40)+1]>80 &&
      partial.depth[index(40,40)]<1.f &&
      bare.serial+1==zoff.serial && zoff.serial+1==disabled.serial &&
      disabled.serial+1==restored.serial && restored.serial+1==partial.serial,
      "partial clear preserves outside and publishes once per layer frame");
  }
  manager.setSceneGraph(nullptr);layer->unref();base->unref();return ok;
}
bool triangleSortingCase(const CoinRenderCapabilities &caps,uint32_t samples){
  Harness run(caps,samples,false);
  auto *root=new SoSeparator;root->ref();root->addChild(camera());
  auto *unlit=new SoLightModel;unlit->model=SoLightModel::BASE_COLOR;root->addChild(unlit);
  auto *shape=new SoSeparator;root->addChild(shape);
  auto *material=new SoMaterial;material->transparency=.5f;shape->addChild(material);
  auto *binding=new SoMaterialBinding;binding->value=SoMaterialBinding::PER_FACE;
  shape->addChild(binding);
  auto *coords=new SoCoordinate3;
  const SbVec3f points[]={SbVec3f(-.75f,-.75f,.5f),SbVec3f(.75f,-.75f,.5f),
    SbVec3f(0,.75f,.5f),SbVec3f(-.75f,-.75f,-.5f),
    SbVec3f(.75f,-.75f,-.5f),SbVec3f(0,.75f,-.5f)};
  coords->point.setValues(0,6,points);shape->addChild(coords);
  auto *faces=new SoIndexedFaceSet;shape->addChild(faces);
  auto *background=new SoSeparator;root->addChild(background);
  auto *blue=new SoMaterial;blue->diffuseColor.setValue(0,0,1);background->addChild(blue);
  auto *backCoords=new SoCoordinate3;
  const SbVec3f back[]={SbVec3f(-.9f,-.9f,-.8f),SbVec3f(.9f,-.9f,-.8f),
    SbVec3f(.9f,.9f,-.8f),SbVec3f(-.9f,.9f,-.8f)};
  backCoords->point.setValues(0,4,back);background->addChild(backCoords);
  auto *backFace=new SoIndexedFaceSet;const int32_t quad[]={0,1,2,3,-1};
  backFace->coordIndex.setValues(0,5,quad);background->addChild(backFace);
  bool ok=true;
  for(const auto mode:{A::SORTED_OBJECT_SORTED_TRIANGLE_BLEND,
                       A::SORTED_OBJECT_SORTED_TRIANGLE_ADD}){
    Frame first,reverse;
    const SbColor colors[]={SbColor(1,0,0),SbColor(0,1,0)};
    material->diffuseColor.setValues(0,2,colors);
    const int32_t initial[]={0,1,2,-1,3,4,5,-1};
    faces->coordIndex.setValues(0,8,initial);
    ok &= run.render(root,mode,first,"one object/two depths near-first");
    const SbColor swapped[]={SbColor(0,1,0),SbColor(1,0,0)};
    material->diffuseColor.setValues(0,2,swapped);
    const int32_t inverse[]={3,4,5,-1,0,1,2,-1};
    faces->coordIndex.setValues(0,8,inverse);
    ok &= run.render(root,mode,reverse,"one object/two depths far-first");
    if(first.rgba.size()==size_t(side)*side*4 &&
       reverse.rgba.size()==first.rgba.size()){
      const auto want=mode==A::SORTED_OBJECT_SORTED_TRIANGLE_ADD?
        std::array<float,3>{{.5f,.5f,1.f}}:
        std::array<float,3>{{.5f,.25f,.25f}};
      ok &= check(rgbAt(first,32,32,want) &&
        rgbAt(reverse,32,32,want) &&
        first.rgba==reverse.rgba && first.depth==reverse.depth &&
        reverse.serial==first.serial+1,
        "per-triangle sorting within one object ignores authored index order");
    }
  }
  root->unref();return ok;
}
bool rejectionCase(const CoinRenderCapabilities &caps,uint32_t samples){
  Harness run(caps,samples,false);Scene scene;
  scene.red->transparency=.5f;scene.green->transparency=.5f;
  Frame stable;
  if(!run.render(scene.root,A::BLEND,stable,"valid translucent rejection anchor"))return false;
  bool ok=check(rgbAt(stable,40,31,{{.5f,0,0}}),
    "rejection anchor is visibly translucent, not empty or opaque");
  auto refused=[&](A::TransparencyType mode,const char *label){
    run.action.setTransparencyType(mode);run.action.apply(scene.root);
    Frame after;run.target->readbackRGBA(after.rgba);
    run.target->readbackDepth(after.depth);
    after.serial=run.target->getLastSubmissionSerial();
    std::cout<<"msaa_transparency reject="<<label
             <<" status="<<int(run.action.getLastStatus())
             <<" error="<<run.action.getLastError().getString()<<'\n';
    return check(run.action.getLastStatus()!=A::SUCCESS &&
      after.rgba==stable.rgba && after.depth==stable.depth &&
      after.serial==stable.serial,
      "unsupported profile refuses before RGBA/depth/serial publication");
  };
  // Keep the original target's stable serial for its transactional checks.
  // wgpu allocates subsequent serials globally across targets, while BGFX
  // advances each target's backend counter independently.
  uint64_t latestDeviceSerial=stable.serial;
  {
    CoinRenderOptions peelingOptions;
    peelingOptions.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
    peelingOptions.sampleCount=samples;
    peelingOptions.transparency=COIN_RENDER_TRANSPARENCY_PEELING;
    std::unique_ptr<CoinRenderTarget> peeling(CoinRenderTarget::createOffscreen(
      SbVec2i32(side,side),peelingOptions));
    ok &= check(bool(peeling),"MSAA peeling target exists");
    if(peeling){
      peeling->setDepthReadbackEnabled(TRUE);
      A peelAction(SbViewportRegion(side,side));
      peelAction.setRenderTarget(peeling.get());
      peelAction.setTransparencyType(A::SORTED_LAYERS_BLEND);
      peelAction.setSortedLayersNumPasses(2);
      peelAction.setBackgroundColor(SbColor4f(0,0,0,1));
      peelAction.apply(scene.root);
      Frame peeled;peeling->readbackRGBA(peeled.rgba);
      peeling->readbackDepth(peeled.depth);
      peeled.serial=peeling->getLastSubmissionSerial();
      std::cout<<"msaa_transparency peeling_status="<<int(peelAction.getLastStatus())
        <<" serial="<<peeled.serial<<" error="<<peelAction.getLastError().getString()<<'\n';
      ok &= check(peelAction.getLastStatus()==A::SUCCESS && peeled.serial>0 &&
        peeled.rgba.size()==size_t(side)*side*4 &&
        peeled.depth.size()==size_t(side)*side &&
        rgbAt(peeled,32,32,{{.5f,.25f,.25f}}) &&
        depthAt(peeled,32,32,.9f,samples),
        "two nearest transparent depths compose over opaque blue");
      if(caps.backend==COIN_RENDER_EXPERIMENTAL_RUST)
        latestDeviceSerial=peeled.serial;
      peelAction.setRenderTarget(nullptr);
    }
  }
  CoinRenderOptions weightedOptions;
  weightedOptions.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  weightedOptions.sampleCount=samples;
  weightedOptions.transparency=COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;
  std::unique_ptr<CoinRenderTarget> weighted(
    CoinRenderTarget::createOffscreen(SbVec2i32(side,side),weightedOptions));
  if(weighted){
    weighted->setDepthReadbackEnabled(TRUE);
    A weightedAction(SbViewportRegion(side,side));
    weightedAction.setRenderTarget(weighted.get());
    weightedAction.setTransparencyType(A::DELAYED_BLEND);
    weightedAction.apply(scene.root);
    Frame weightedFrame;weighted->readbackRGBA(weightedFrame.rgba);
    weighted->readbackDepth(weightedFrame.depth);
    weightedFrame.serial=weighted->getLastSubmissionSerial();
    const float wr=.5f*std::min(8.f,.5f*8+.01f)*
      std::min(16.f,std::pow(1.f-.25f,3.f)*16.f+.1f);
    const float wg=.5f*std::min(8.f,.5f*8+.01f)*
      std::min(16.f,std::pow(1.f-.75f,3.f)*16.f+.1f);
    const std::array<float,3> weightedRgb={{.75f*wr/(wr+wg),
      .75f*wg/(wr+wg),.25f}};
    std::cout<<"msaa_transparency weighted_status="<<int(weightedAction.getLastStatus())
             <<" serial="<<weightedFrame.serial
             <<" error="<<weightedAction.getLastError().getString()<<'\n';
    ok &= check(weightedAction.getLastStatus()==A::SUCCESS &&
      weightedFrame.rgba.size()==size_t(side)*side*4 &&
      weightedFrame.depth.size()==size_t(side)*side &&
      weightedFrame.serial>0 && rgbAt(weightedFrame,40,31,weightedRgb) &&
      depthAt(weightedFrame,40,31,.9f,samples),
      "weighted OIT with real alpha .5 composes numerically over opaque blue");
    latestDeviceSerial=weightedFrame.serial;
    weightedAction.setRenderTarget(nullptr);
  }
  if(caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION){
    CoinRenderOptions oversizedOptions;
    oversizedOptions.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
    oversizedOptions.sampleCount=4;
    std::unique_ptr<CoinRenderTarget> oversized(
      CoinRenderTarget::createOffscreen(SbVec2i32(2049,2049),oversizedOptions));
    ok &= check(bool(oversized),"BGFX oversized target exists for budget preflight");
    if(oversized){
      A oversizedAction(SbViewportRegion(2049,2049));
      oversizedAction.setRenderTarget(oversized.get());
      oversizedAction.setTransparencyType(A::BLEND);
      oversizedAction.apply(scene.root);
      const std::string error=oversizedAction.getLastError().getString();
      std::cout<<"msaa_transparency bgfx_oversized_status="
               <<int(oversizedAction.getLastStatus())<<" error="<<error<<'\n';
      ok &= check(oversizedAction.getLastStatus()==A::UNSUPPORTED &&
        oversized->getLastSubmissionSerial()==0 &&
        (error.find("128")!=std::string::npos ||
         error.find("budget")!=std::string::npos),
        "BGFX 2049-square translucent 4x exceeds 128 MiB before publication");
      oversizedAction.setRenderTarget(nullptr);
    }
  }
  auto *imageGroup=new SoSeparator;
  auto *translation=new SoTranslation;
  translation->translation.setValue(0,0,.75f);
  imageGroup->addChild(translation);
  auto *image=new SoImage;const uint8_t pixel[]={255,255,255,255};
  image->image.setValue(SbVec2s(1,1),4,pixel);
  imageGroup->addChild(image);scene.root->addChild(imageGroup);
  {
    Frame withImage,withoutImage;
    const bool appeared=run.render(scene.root,A::BLEND,withImage,
      "opaque SoImage over translucent geometry");
    size_t white=0,changed=0;bool exterior=true;
    if(appeared && withImage.rgba.size()==stable.rgba.size() &&
       withImage.depth.size()==stable.depth.size()){
      for(size_t p=0;p<withImage.depth.size();++p){
        const bool different=!std::equal(withImage.rgba.begin()+4*p,
          withImage.rgba.begin()+4*p+4,stable.rgba.begin()+4*p);
        const bool whitePixel=withImage.rgba[4*p]==255 &&
          withImage.rgba[4*p+1]==255 && withImage.rgba[4*p+2]==255;
        white+=whitePixel;changed+=different;
        if(!different)exterior &= withImage.depth[p]==stable.depth[p];
      }
    }
    std::cout<<"msaa_transparency image white="<<white
             <<" changed="<<changed<<" exterior_depth="<<exterior<<'\n';
    const uint64_t expectedImageSerial=caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?
      latestDeviceSerial+1:stable.serial+1;
    std::cout<<"msaa_transparency image_serial="<<withImage.serial
             <<" expected="<<expectedImageSerial
             <<" original_target_anchor="<<stable.serial<<'\n';
    ok &= check(appeared && white==1 && changed==1 && exterior &&
      withImage.serial==expectedImageSerial,
      "opaque raster pixel appears once over BLEND and leaves exterior intact");
    scene.root->removeChild(imageGroup);
    if(run.render(scene.root,A::BLEND,withoutImage,
                  "raster removed exactly")){
      ok &= check(withoutImage.rgba==stable.rgba &&
        withoutImage.depth==stable.depth &&
        withoutImage.serial==withImage.serial+1,
        "raster removal restores RGBA/depth and publishes once");
      stable=withoutImage;
    } else ok=false;
  }
  auto *bump=new SoBumpMap;const uint8_t normal[]={128,128,255};
  bump->image.setValue(SbVec2s(1,1),3,normal);
  scene.root->insertChild(bump,scene.root->getNumChildren()-1);
  ok &= refused(A::BLEND,"active bump with translucent geometry");
  scene.root->removeChild(bump);
  // Valid BLEND plus active maps has its own PHONG receiver and per-sample
  // oracle in CoinRenderMsaaShadowTest. The old BASE_COLOR cube did not
  // establish a valid receiver and cannot serve as an MSAA refusal gate.
  Frame recovered;
  ok &= run.render(scene.root,A::BLEND,recovered,"recovered translucent frame") &&
    check(recovered.rgba==stable.rgba && recovered.depth==stable.depth &&
      recovered.serial==stable.serial+1,
      "exact translucent recovery after unsupported combinations");
  return ok;
}
template<class NativeAction> struct CountHighlight : NativeAction {
  int calls=0;
  void apply(SoNode *node) override {++calls;NativeAction::apply(node);}
};
template<class NativeAction>
bool highlightCase(const CoinRenderCapabilities &caps,uint32_t samples,
                   const char *label){
  Scene scene;scene.red->transparency=.5f;
  scene.nearGroup->ref();scene.root->removeChild(scene.nearGroup);
  auto *selection=new SoSelection;selection->addChild(scene.nearGroup);
  scene.root->insertChild(selection,2);selection->select(scene.nearGroup);
  scene.nearGroup->unref();
  CountHighlight<NativeAction> highlight;
  highlight.setColor(SbColor(0,1,1));highlight.setLineWidth(3);
  SoRenderManager manager;manager.setViewportRegion(SbViewportRegion(side,side));
  manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.setBackgroundColor(SbColor4f(0,0,0,1));
  manager.setSceneGraph(scene.root);manager.setGLRenderAction(&highlight);
  manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
  CoinRenderOptions options;options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  options.sampleCount=samples;
  CoinRenderManagerAdapter adapter(manager,SbVec2i32(side,side),options);
  auto *target=adapter.getSceneManager()->getRenderTarget();
  bool ok=check(target && target->getOptions().renderer==caps.renderer &&
    target->getOptions().sampleCount==samples,"highlight actual API/count");
  if(ok)target->setDepthReadbackEnabled(TRUE);
  auto capture=[&](Frame &out,const char *phase){
    const auto status=adapter.render();
    target->readbackRGBA(out.rgba);target->readbackDepth(out.depth);
    out.serial=target->getLastSubmissionSerial();
    std::cout<<"msaa_transparency highlight="<<label<<" phase="<<phase
             <<" status="<<int(status)<<" error="<<adapter.getLastError().getString()<<'\n';
    return check(status==A::SUCCESS && out.rgba.size()==size_t(side)*side*4 &&
      out.depth.size()==size_t(side)*side,"highlight publishes one full frame");
  };
  Frame selected,unselected,changed,restored;
  if(ok)ok &= capture(selected,"cyan selected");
  selection->deselectAll();if(ok)ok &= capture(unselected,"bare translucent object");
  selection->select(scene.nearGroup);highlight.setColor(SbColor(1,1,0));
  if(ok)ok &= capture(changed,"yellow selected");
  highlight.setColor(SbColor(0,1,1));if(ok)ok &= capture(restored,"cyan restored");
  if(ok){
    size_t stroke=0,yellow=0,base=0,fullCyan=0,halfCyan=0;
    for(size_t p=0;p<size_t(side)*side;++p){
      const size_t i=4*p;
      stroke+=(selected.rgba[i+1]>unselected.rgba[i+1]+20 &&
               selected.rgba[i+2]>unselected.rgba[i+2]+20);
      yellow+=(changed.rgba[i]>selected.rgba[i]+20 &&
               changed.rgba[i+2]+20<selected.rgba[i+2] &&
               changed.rgba[i+1]>80);
      base+=(unselected.rgba[i]>30 && unselected.rgba[i]<250);
      if(selected.rgba[i+1]>unselected.rgba[i+1]+20 &&
         selected.rgba[i+2]>unselected.rgba[i+2]+20){
        fullCyan+=(selected.rgba[i+1]>220 && selected.rgba[i+2]>220);
        halfCyan+=(selected.rgba[i+1]>105 && selected.rgba[i+1]<155 &&
                   selected.rgba[i+2]>105 && selected.rgba[i+2]<155);
      }
    }
    std::cout<<"msaa_transparency highlight="<<label<<" samples="<<samples
             <<" cyan_stroke="<<stroke<<" yellow_stroke="<<yellow
             <<" translucent_base="<<base<<" full_cyan="<<fullCyan
             <<" half_cyan="<<halfCyan<<'\n';
    const char *separate=std::getenv("COIN_SEPARATE_DIFFUSE_TRANSPARENCY_OVERRIDE");
    const bool separateAlpha=separate && std::string(separate)=="1";
    const bool line=std::string(label)=="line";
    ok &= check(stroke>8 && yellow>8 && base>8 &&
      (line && separateAlpha?halfCyan>8:fullCyan>8) &&
      selected.rgba==restored.rgba && selected.depth==restored.depth &&
      selected.serial+1==unselected.serial &&
      unselected.serial+1==changed.serial &&
      changed.serial+1==restored.serial && highlight.calls==0,
      "translucent selected object retains BOX/LINE stroke, A/B/A and one publication");
  }
  manager.setGLRenderAction(nullptr);manager.setSceneGraph(nullptr);return ok;
}
bool run(const std::string &mode){
  const auto caps=capabilities();
  if(mode=="--cpu-1x")return coreCase(caps,1,true) &&
    edgeCoverage(caps,1,true) && textureCase(caps,1,true) &&
    fogClipCase(caps,1,true);
  if(!check(caps.gpu_available && caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN &&
    (caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION ||
     caps.backend==COIN_RENDER_EXPERIMENTAL_RUST),"identified physical GPU required"))return false;
  if(mode=="--offscreen"){
    Scene anchorScene;Harness anchor(caps,1,false);Frame anchorFrame;
    bool ok=anchor.render(anchorScene.root,A::BLEND,anchorFrame,"live runtime anchor");
    for(uint32_t samples:{1u,2u,4u,2u,1u}){
      const bool current=coreCase(caps,samples,false) &&
        edgeCoverage(caps,samples,false) && textureCase(caps,samples,false) &&
        fogClipCase(caps,samples,false);
      ok &= current;
    }
    return ok;
  }
  if(mode=="--layers"){
    bool ok=true;for(uint32_t samples:{1u,2u,4u}) {
      const bool current=layerCase(caps,samples);ok &= current;
    }
    return ok;
  }
  if(mode=="--triangle-sorting") {
    bool ok=true;
    for(uint32_t samples:{1u,2u,4u}) {
      const bool current=triangleSortingCase(caps,samples);ok &= current;
    }
    return ok;
  }
  if(mode=="--rejections")return rejectionCase(caps,4);
  if(mode=="--highlight"){
    bool ok=true;for(uint32_t samples:{1u,2u,4u}){
      ok &= highlightCase<SoBoxHighlightRenderAction>(caps,samples,"box");
      ok &= highlightCase<SoLineHighlightRenderAction>(caps,samples,"line");
    }return ok;
  }
  return false;
}
}
int main(int argc,char **argv){if(argc!=2)return 2;
  SoDB::init();SoInteraction::init();A::initClass();PartialViewport::initClass();
  return run(argv[1])?0:1;}
