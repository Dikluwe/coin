#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderViewBudgetTest.h"
#include "CoinRenderMsaaShadowFixture.h"
#include <Inventor/SbPlane.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <limits>
using namespace msaa_shadow_test;
namespace {
class PartialViewport:public SoNode{
  SO_NODE_HEADER(PartialViewport);
public:
  static void initClass(){SO_NODE_INIT_CLASS(PartialViewport,SoNode,"Node");}
  PartialViewport(){SO_NODE_CONSTRUCTOR(PartialViewport);}
  bool enabled=false;
  SbViewportRegion region;
  void callback(SoCallbackAction *action) override{
    if(enabled)SoViewportRegionElement::set(action->getState(),region);
  }
  void GLRender(SoGLRenderAction *action) override{
    if(enabled)SoViewportRegionElement::set(action->getState(),region);
  }
protected:~PartialViewport() override=default;
};
SO_NODE_SOURCE(PartialViewport);
bool requireGpu(const CoinRenderCapabilities &caps){
  return check(caps.gpu_available && caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN &&
    (caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION ||
     caps.backend==COIN_RENDER_EXPERIMENTAL_RUST),
    "actual identified BGFX/wgpu GPU required");
}
bool depthAndCoverage(const Frame &frame,unsigned samples){
  if(!check(frame.rgba.size()==size_t(side)*side*4 &&
     frame.depth.size()==size_t(side)*side,"complete frame for coverage"))return false;
  const float expectedDepth=6.f/19.f;
  const size_t centre=at(64,64);
  bool ok=check(std::abs(frame.depth[centre]-expectedDepth)<1e-6f &&
    frame.rgba[4*centre+3]==255,
    "opaque shadowed receiver has analytic depth and full centre coverage");
  size_t quarter=0,half=0,threeQuarter=0,nonzero=0;
  for(size_t p=0;p<frame.depth.size();++p){
    const int alpha=frame.rgba[4*p+3];nonzero+=(alpha>0);
    if(alpha==0 || alpha==255)continue;
    const int k=int(std::lround(double(alpha)*samples/255.));
    ok &= check(k>0 && k<int(samples) &&
      std::abs(alpha-int(std::lround(255.*k/samples)))<=2 &&
      std::abs(frame.depth[p]-expectedDepth)<1e-6f,
      "resolved alpha follows k/N and partial pixel depth is minimum sample");
    quarter+=(samples==4 && k==1);
    half+=(2*k==int(samples));
    threeQuarter+=(samples==4 && k==3);
  }
  std::cout<<"msaa_shadow samples="<<samples<<" nonzero="<<nonzero
           <<" quarter="<<quarter<<" half="<<half
           <<" three_quarter="<<threeQuarter<<'\n';
  ok &= check(nonzero>1000 &&
    (samples==1?quarter+half+threeQuarter==0:
     samples==2?half>0:quarter>0 && threeQuarter>0),
    "real 1/2/4x shadowed receiver silhouette distinguishes sample count");
  return ok;
}
bool offscreen(const CoinRenderCapabilities &caps){
  Scene scene(1,false);Harness liveOne(caps,1);
  if(!check(liveOne.target &&
     liveOne.target->getOptions().renderer==caps.renderer,
     "1x anchor target keeps requested API alive"))return false;
  Frame anchor;
  if(!liveOne.render(scene.root,anchor,"live one-sample shadow anchor"))return false;
  bool ok=depthAndCoverage(anchor,1);
  std::array<uint8_t,4> centre{{anchor.rgba[4*at(64,64)],
    anchor.rgba[4*at(64,64)+1],anchor.rgba[4*at(64,64)+2],
    anchor.rgba[4*at(64,64)+3]}};
  for(unsigned samples:{1u,2u,4u,2u,1u}){
    Harness harness(caps,samples);
    if(!check(harness.target &&
       harness.target->getOptions().renderer==caps.renderer &&
       harness.target->getOptions().sampleCount==samples,
       "sample-count-specific physical target")){ok=false;continue;}
    Frame cast,noShadow,moved,restored;
    if(!harness.render(scene.root,cast,"shadowed receiver")){ok=false;continue;}
    scene.group->isActive=FALSE;
    const bool bare=harness.render(scene.root,noShadow,"same light without active map");
    scene.group->isActive=TRUE;
    scene.casterX(0,.75f);
    const bool shifted=harness.render(scene.root,moved,"caster shifted A/B");
    scene.casterX(0,0.f);
    const bool again=harness.render(scene.root,restored,"caster restored A/B/A");
    if(!(bare && shifted && again)){ok=false;continue;}
    const unsigned shadowPixels=differentRgb(noShadow,cast);
    const unsigned shiftedPixels=differentRgb(cast,moved);
    std::cout<<"msaa_shadow offscreen samples="<<samples
             <<" map_vs_inactive="<<shadowPixels
             <<" caster_shift="<<shiftedPixels<<'\n';
    ok &= check(shadowPixels>25 && shiftedPixels>25 &&
      cast.rgba==restored.rgba && cast.depth==restored.depth &&
      cast.serial+1==noShadow.serial &&
      noShadow.serial+1==moved.serial &&
      moved.serial+1==restored.serial,
      "real caster/light effect, exact A/B/A and one publication per apply");
    ok &= depthAndCoverage(cast,samples);
    for(unsigned c=0;c<4;++c)
      ok &= check(std::abs(int(cast.rgba[4*at(64,64)+c])-int(centre[c]))<=3,
        "fully covered shadow centre matches 1x oracle across MSAA counts");
    auto *invalid=new SoAlphaTest;
    invalid->function=SoAlphaTest::GREATER;
    invalid->value=std::numeric_limits<float>::quiet_NaN();
    scene.root->addChild(invalid);harness.action.apply(scene.root);
    Frame rejected;harness.target->readbackRGBA(rejected.rgba);
    harness.target->readbackDepth(rejected.depth);
    rejected.serial=harness.target->getLastSubmissionSerial();
    std::cout<<"msaa_shadow invalid samples="<<samples
             <<" status="<<int(harness.action.getLastStatus())
             <<" error="<<harness.action.getLastError().getString()<<'\n';
    ok &= check(harness.action.getLastStatus()!=CoinRenderAction::SUCCESS &&
      rejected.rgba==restored.rgba && rejected.depth==restored.depth &&
      rejected.serial==restored.serial,
      "invalid shadow frame preserves RGBA/depth/serial before submission");
    scene.root->removeChild(invalid);
    Frame recovered;
    ok &= harness.render(scene.root,recovered,"shadow after invalid state") &&
      check(recovered.rgba==cast.rgba && recovered.depth==cast.depth &&
        recovered.serial==restored.serial+1,
        "valid shadow recovers exactly after invalid input");
  }
  return ok;
}
bool slots(const CoinRenderCapabilities &caps){
  bool ok=true;
  for(bool directional:{false,true})for(int count:{1,2,4,8}){
    Scene scene(count,directional);Harness harness(caps,4);
    Frame all;
    if(!harness.render(scene.root,all,"all shadow maps active")){ok=false;continue;}
    for(int slot=0;slot<count;++slot){
      Frame moved,restored;
      const float original=count==1?0.f:
        (float(slot)-float(count-1)/2.f)*.34f;
      scene.casterX(slot,original+.65f);
      const bool movedOk=harness.render(scene.root,moved,"one map caster moved");
      scene.casterX(slot,original);
      const bool restoredOk=harness.render(scene.root,restored,"one map caster restored");
      if(!(movedOk && restoredOk)){ok=false;continue;}
      const unsigned changed=differentRgb(all,moved);
      std::cout<<"msaa_shadow slot_directional="<<directional
               <<" maps="<<count<<" slot="<<slot
               <<" changed="<<changed<<'\n';
      ok &= check(changed>5 && all.rgba==restored.rgba &&
        all.depth==restored.depth && moved.serial==all.serial+1 &&
        restored.serial==moved.serial+1,
        "each individual map contributes distinct caster footprint and restores");
      all=restored;
    }
  }
  // Divide one light's intensity among coincident maps. Their independent
  // contributions must sum to the same interior result, including map 7.
  for(bool directional:{false,true}){
    Frame one;
    for(int count:{1,2,4,8}){
      Scene scene(count,directional);
      for(int slot=0;slot<count;++slot)scene.casterX(slot,0.f);
      Harness harness(caps,4);Frame frame;
      if(!harness.render(scene.root,frame,"coincident casters, divided light")){
        ok=false;continue;
      }
      if(count==1){one=frame;continue;}
      for(const size_t p:{at(64,64),at(90,64)}){
        std::cout<<"msaa_shadow divide directional="<<directional
          <<" maps="<<count<<" pixel="<<p<<" RGB="
          <<int(frame.rgba[4*p])<<','<<int(frame.rgba[4*p+1])<<','
          <<int(frame.rgba[4*p+2])<<'\n';
        for(int channel=0;channel<4;++channel)
          ok &= check(std::abs(int(frame.rgba[4*p+channel])-
            int(one.rgba[4*p+channel]))<=3,
            "coincident map contributions equal one unsplit light at interior");
        ok &= check(frame.depth[p]==one.depth[p],
          "map count never changes receiver depth at interior");
      }
    }
  }
  return ok;
}
bool blend(const CoinRenderCapabilities &caps){
  bool ok=true;
  struct Mode{CoinRenderAction::TransparencyType type;const char *name;
    bool immediate,additive;};
  const Mode modes[]={
    {CoinRenderAction::BLEND,"blend",true,false},
    {CoinRenderAction::ADD,"add",true,true},
    {CoinRenderAction::DELAYED_BLEND,"delayed-blend",false,false},
    {CoinRenderAction::DELAYED_ADD,"delayed-add",false,true},
    {CoinRenderAction::SORTED_OBJECT_BLEND,"sorted-blend",false,false},
    {CoinRenderAction::SORTED_OBJECT_ADD,"sorted-add",false,true},
    {CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND,
      "sorted-triangle-blend",false,false},
    {CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD,
      "sorted-triangle-add",false,true}
  };
  for(const auto &mode:modes){
    Scene scene(1,false);scene.receiverMaterial->transparency=.5f;
    std::array<uint8_t,4> oneCentre{{0,0,0,0}};
    for(unsigned samples:{1u,2u,4u}){
      Harness harness(caps,samples);Frame cast,bare,restored;
      const bool a=harness.render(scene.root,cast,mode.name,mode.type);
      scene.group->isActive=FALSE;
      const bool b=harness.render(scene.root,bare,"same mode without map",mode.type);
      scene.group->isActive=TRUE;
      const bool c=harness.render(scene.root,restored,
        "same mode map restored",mode.type);
      if(!(a&&b&&c)){ok=false;continue;}
      const size_t centre=at(64,64);
      const int fullAlpha=cast.rgba[4*centre+3];
      size_t quarter=0,half=0,three=0;
      for(size_t p=0;p<cast.depth.size();++p){
        const int alpha=cast.rgba[4*p+3];
        if(alpha==0 || alpha==fullAlpha)continue;
        const int k=int(std::lround(double(alpha)*samples/fullAlpha));
        ok &= check(k>0 && k<int(samples) &&
          std::abs(alpha-int(std::lround(double(fullAlpha)*k/samples)))<=3,
          "translucent shadow edge alpha follows covered sample count");
        quarter+=(samples==4 && k==1);
        half+=(2*k==int(samples));
        three+=(samples==4 && k==3);
      }
      const int wantAlpha=mode.additive?64:128;
      const float wantDepth=mode.immediate?6.f/19.f:1.f;
      std::cout<<"msaa_shadow mode="<<mode.name<<" samples="<<samples
        <<" full_alpha="<<fullAlpha<<" expected_alpha="<<wantAlpha
        <<" depth="<<cast.depth[centre]<<" expected_depth="<<wantDepth
        <<" quarter="<<quarter<<" half="<<half<<" threequarter="<<three
        <<" map_delta="<<differentRgb(cast,bare)<<'\n';
      ok &= check(std::abs(fullAlpha-wantAlpha)<=2 &&
        std::abs(cast.depth[centre]-wantDepth)<1e-6f &&
        differentRgb(cast,bare)>25 &&
        cast.rgba==restored.rgba && cast.depth==restored.depth &&
        cast.serial+1==bare.serial && bare.serial+1==restored.serial &&
        (samples==1?quarter+half+three==0:
         samples==2?half>0:quarter>0 && three>0),
        "all eight OBJECT modes preserve shadow, alpha equation, depth and A/B/A");
      if(samples==1)for(unsigned component=0;component<4;++component)
        oneCentre[component]=cast.rgba[4*centre+component];
      else for(unsigned component=0;component<4;++component)
        ok &= check(std::abs(int(cast.rgba[4*centre+component])-
          int(oneCentre[component]))<=3,
          "full-coverage shadow/transparency equals its 1x interior oracle");
    }
  }
  return ok;
}
bool layers(const CoinRenderCapabilities &caps){
  bool ok=true;
  PartialViewport::initClass();
  for(unsigned samples:{1u,2u,4u}){
    Scene scene(1,false);
    auto *annotation=new SoAnnotation;
    auto *noteStyle=new SoShadowStyle;
    noteStyle->style=SoShadowStyle::NO_SHADOWING;
    annotation->addChild(noteStyle);
    auto *noteModel=new SoLightModel;noteModel->model=SoLightModel::BASE_COLOR;
    annotation->addChild(noteModel);
    auto *noteColor=new SoMaterial;noteColor->diffuseColor.setValue(1,1,0);
    annotation->addChild(noteColor);
    auto *noteCoords=new SoCoordinate3;
    const SbVec3f note[]={SbVec3f(-1.7f,1.5f,-1),SbVec3f(-1.3f,1.5f,-1),
      SbVec3f(-1.3f,1.8f,-1),SbVec3f(-1.7f,1.8f,-1)};
    noteCoords->point.setValues(0,4,note);annotation->addChild(noteCoords);
    auto *noteFace=new SoFaceSet;noteFace->numVertices.set1Value(0,4);
    annotation->addChild(noteFace);scene.group->addChild(annotation);
    SoRenderManager manager;manager.setSceneGraph(scene.root);
    manager.setViewportRegion(SbViewportRegion(side,side));
    manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
    manager.setBackgroundColor(SbColor4f(0,0,0,0));
    CoinRenderManagerAdapter adapter(manager,SbVec2i32(side,side),options(caps,samples));
    auto *target=adapter.getSceneManager()->getRenderTarget();
    if(!check(target && target->getOptions().renderer==caps.renderer &&
      target->getOptions().sampleCount==samples,"manager physical sample count")){
      manager.setSceneGraph(nullptr);return false;
    }
    target->setDepthReadbackEnabled(TRUE);
    auto capture=[&](Frame &frame,const char *label){
      const auto status=adapter.render();
      target->readbackRGBA(frame.rgba);target->readbackDepth(frame.depth);
      frame.serial=target->getLastSubmissionSerial();
      std::cout<<"msaa_shadow layers samples="<<samples<<" phase="<<label
        <<" status="<<int(status)<<" serial="<<frame.serial
        <<" error="<<adapter.getLastError().getString()<<'\n';
      return check(status==CoinRenderAction::SUCCESS &&
        frame.rgba.size()==size_t(side)*side*4 &&
        frame.depth.size()==size_t(side)*side,label);
    };
    auto *layer=new SoSeparator;layer->ref();
    auto *viewport=new PartialViewport;
    viewport->region=SbViewportRegion(side,side);
    viewport->region.setViewportPixels(side/2,0,side/2,side);
    layer->addChild(viewport);
    auto *camera=new SoOrthographicCamera;camera->position.setValue(0,0,6);
    camera->height=4;camera->nearDistance=1;camera->farDistance=20;
    layer->addChild(camera);
    auto *unlit=new SoLightModel;unlit->model=SoLightModel::BASE_COLOR;
    layer->addChild(unlit);
    auto *green=new SoMaterial;green->diffuseColor.setValue(0,1,0);
    layer->addChild(green);
    auto *points=new SoCoordinate3;
    const SbVec3f quad[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),
      SbVec3f(1,1,0),SbVec3f(-1,1,0)};
    points->point.setValues(0,4,quad);layer->addChild(points);
    auto *face=new SoFaceSet;face->numVertices.set1Value(0,4);layer->addChild(face);
    auto *background=new SoSeparator;background->ref();
    auto *backCamera=new SoOrthographicCamera;
    backCamera->position.setValue(0,0,6);backCamera->height=4;
    backCamera->nearDistance=1;backCamera->farDistance=20;
    background->addChild(backCamera);
    auto *backModel=new SoLightModel;backModel->model=SoLightModel::BASE_COLOR;
    background->addChild(backModel);
    auto *blue=new SoMaterial;blue->diffuseColor.setValue(0,0,1);
    background->addChild(blue);
    auto *backPoints=new SoCoordinate3;
    const SbVec3f backQuad[]={SbVec3f(-1.9f,-1.9f,0),
      SbVec3f(1.9f,-1.9f,0),SbVec3f(1.9f,1.9f,0),
      SbVec3f(-1.9f,1.9f,0)};
    backPoints->point.setValues(0,4,backQuad);
    background->addChild(backPoints);
    auto *backFace=new SoFaceSet;backFace->numVertices.set1Value(0,4);
    background->addChild(backFace);
    Frame base,withBackground,front,zoff,partial,restored;
    const bool baseOk=capture(base,"shadow base with annotation");
    auto *backLayer=manager.addSuperimposition(background,
      SoRenderManager::Superimposition::AUTOREDRAW |
      SoRenderManager::Superimposition::BACKGROUND |
      SoRenderManager::Superimposition::ZBUFFERON);
    const bool backOk=capture(withBackground,"background before shadow base");
    const unsigned zon=SoRenderManager::Superimposition::AUTOREDRAW |
      SoRenderManager::Superimposition::ZBUFFERON;
    auto *super=manager.addSuperimposition(layer,zon);
    const bool frontOk=capture(front,"ZBUFFERON foreground");
    manager.removeSuperimposition(super);
    super=manager.addSuperimposition(layer,
      SoRenderManager::Superimposition::AUTOREDRAW);
    const bool zoffOk=capture(zoff,"ZBUFFEROFF foreground");
    manager.removeSuperimposition(super);
    viewport->enabled=true;
    super=manager.addSuperimposition(layer,zon |
      SoRenderManager::Superimposition::CLEARZBUFFER);
    const bool partialOk=capture(partial,"partial CLEARZBUFFER foreground");
    manager.removeSuperimposition(super);viewport->enabled=false;
    manager.removeSuperimposition(backLayer);
    const bool restoreOk=capture(restored,"shadow layers restored");
    if(baseOk&&backOk&&frontOk&&zoffOk&&partialOk&&restoreOk){
      // Observer covers world [-2,2] with top-left readback origin.
      const size_t left=at(48,64),right=at(88,64),note=at(16,12);
      std::cout<<"msaa_shadow layer_witness samples="<<samples
        <<" baseL="<<int(base.rgba[4*left])<<" baseR="<<int(base.rgba[4*right])
        <<" partialL="<<int(partial.rgba[4*left])
        <<" partialR="<<int(partial.rgba[4*right])<<'\n';
      ok &= check(base.rgba==restored.rgba && base.depth==restored.depth &&
        base.serial+1==withBackground.serial &&
        withBackground.serial+1==front.serial && front.serial+1==zoff.serial &&
        zoff.serial+1==partial.serial && partial.serial+1==restored.serial,
        "manager shadow/annotation A/B/A and one publication each");
      const size_t outside=at(8,8);
      for(const auto p:{left,right,note,outside})
        std::cout<<"msaa_shadow layers_point samples="<<samples<<" pixel="<<p
          <<" base="<<int(base.rgba[4*p])<<','<<int(base.rgba[4*p+1])
          <<','<<int(base.rgba[4*p+2])<<" background="
          <<int(withBackground.rgba[4*p])<<','
          <<int(withBackground.rgba[4*p+1])<<','
          <<int(withBackground.rgba[4*p+2])
          <<" front="<<int(front.rgba[4*p])<<','
          <<int(front.rgba[4*p+1])<<','<<int(front.rgba[4*p+2])
          <<" partial="<<int(partial.rgba[4*p])<<','
          <<int(partial.rgba[4*p+1])<<','
          <<int(partial.rgba[4*p+2])
          <<" depth="<<base.depth[p]<<','<<front.depth[p]<<','
          <<partial.depth[p]<<'\n';
      ok &= check(withBackground.rgba[4*outside+2]>200 &&
        withBackground.rgba[4*left]==base.rgba[4*left] &&
        withBackground.depth[left]==base.depth[left],
        "BACKGROUND is visible outside receiver and base clears its depth");
      ok &= check(base.rgba[4*note]>200 && base.rgba[4*note+1]>200 &&
        base.depth[note]==1.f,"annotation visible without depth write");
      ok &= check(front.rgba[4*right+1]>200 &&
        std::abs(front.depth[right]-5.f/19.f)<1e-6f &&
        zoff.rgba[4*right+1]>200 &&
        zoff.depth[right]==base.depth[right],
        "ZBUFFERON writes analytic depth and ZBUFFEROFF changes only color");
      ok &= check(partial.rgba[4*left]==base.rgba[4*left] &&
        partial.depth[left]==base.depth[left] &&
        partial.rgba[4*right+1]>200,
        "partial clear preserves outside RGBA/depth and updates inside");
    }else ok=false;
    background->unref();layer->unref();manager.setSceneGraph(nullptr);
  }
  return ok;
}
bool rejections(const CoinRenderCapabilities &caps){
  bool ok=true;
  // A shadow group with no effective light must retain its ordinary 1x path.
  Scene noMaps(1,false);noMaps.lightOn(0,false);
  Harness one(caps,1);Frame plain,again;
  ok &= one.render(noMaps.root,plain,"1x inactive light in shadow group");
  noMaps.group->isActive=FALSE;
  ok &= one.render(noMaps.root,again,"1x inactive group without maps");
  ok &= check(plain.rgba==again.rgba && plain.depth==again.depth &&
    plain.serial+1==again.serial,
    "no effective maps preserve the valid 1x receiver and one publication");
  Scene active(1,false);active.receiverMaterial->transparency=.5f;
  Harness four(caps,4);Frame stable;
  ok &= four.render(active.root,stable,
    "valid shadow plus common BLEND before invalid profiles",
    CoinRenderAction::BLEND);
  auto rejected=[&](CoinRenderAction::TransparencyType mode,const char *label){
    four.action.setTransparencyType(mode);four.action.apply(active.root);
    Frame after;four.target->readbackRGBA(after.rgba);
    four.target->readbackDepth(after.depth);
    after.serial=four.target->getLastSubmissionSerial();
    std::cout<<"msaa_shadow rejection="<<label
      <<" status="<<int(four.action.getLastStatus())
      <<" error="<<four.action.getLastError().getString()
      <<" serial="<<after.serial<<'\n';
    return check(four.action.getLastStatus()!=CoinRenderAction::SUCCESS &&
      after.rgba==stable.rgba && after.depth==stable.depth &&
      after.serial==stable.serial,
      "unsupported shadow/MSAA profile preserves RGBA/depth/serial");
  };
  Frame peeling,returned;
  four.action.setSortedLayersNumPasses(2);
  ok &= four.render(active.root,peeling,
    "peeling with effective shadow and alpha .5",
    CoinRenderAction::SORTED_LAYERS_BLEND);
  size_t peelingColored=0;
  for(size_t p=0;p<peeling.rgba.size()/4;++p)
    peelingColored+=(peeling.rgba[4*p] || peeling.rgba[4*p+1] ||
      peeling.rgba[4*p+2]);
  ok &= check(peeling.rgba.size()==stable.rgba.size() &&
    peeling.depth.size()==stable.depth.size() && peeling.serial==stable.serial+1 &&
    peelingColored>1000,
    "effective shadow plus one transparent layer publishes a visible frame");
  ok &= four.render(active.root,returned,"ordinary shadow BLEND restored",
    CoinRenderAction::BLEND);
  ok &= check(returned.rgba==stable.rgba && returned.depth==stable.depth &&
    returned.serial==peeling.serial+1,
    "return from peeling restores ordinary shadow exactly");
  stable=returned;
  auto *bump=new SoBumpMap;const uint8_t normal[]={128,128,255};
  bump->image.setValue(SbVec2s(1,1),3,normal);
  active.root->insertChild(bump,2);
  ok &= rejected(CoinRenderAction::BLEND,"bump plus spot shadow and alpha BLEND remains outside opaque directional profile");
  active.root->removeChild(bump);
  CoinRenderReadbackTicket ticket{};
  four.action.setTransparencyType(CoinRenderAction::BLEND);
  four.action.applyAsync(active.root,ticket);
  Frame afterTicket;four.target->readbackRGBA(afterTicket.rgba);
  four.target->readbackDepth(afterTicket.depth);
  afterTicket.serial=four.target->getLastSubmissionSerial();
  ok &= check(four.action.getLastStatus()!=CoinRenderAction::SUCCESS &&
    ticket.token==0 && afterTicket.rgba==stable.rgba &&
    afterTicket.depth==stable.depth && afterTicket.serial==stable.serial,
    "async ticket with active MSAA shadow refuses without publication");
  Frame recovered;
  ok &= four.render(active.root,recovered,"shadow after invalid profiles",
    CoinRenderAction::BLEND) &&
    check(recovered.rgba==stable.rgba && recovered.depth==stable.depth &&
      recovered.serial==stable.serial+1,
      "valid shadow BLEND recovers exactly after rejections");
  CoinRenderOptions weightedOptions=options(caps,4);
  weightedOptions.transparency=COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;
  std::unique_ptr<CoinRenderTarget> weighted(CoinRenderTarget::createOffscreen(
    SbVec2i32(side,side),weightedOptions));
  if(check(bool(weighted),"weighted shadow target")){
    weighted->setDepthReadbackEnabled(TRUE);
    CoinRenderAction action(SbViewportRegion(side,side));
    action.setBackgroundColor(SbColor4f(0,0,0,0));
    action.setRenderTarget(weighted.get());
    action.setTransparencyType(CoinRenderAction::BLEND);
    action.apply(active.root);Frame anchor;
    weighted->readbackRGBA(anchor.rgba);weighted->readbackDepth(anchor.depth);
    anchor.serial=weighted->getLastSubmissionSerial();
    ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
      anchor.serial>0,"weighted target admits OBJECT shadow control");
    action.setTransparencyType(CoinRenderAction::DELAYED_BLEND);
    action.apply(active.root);Frame composed;
    weighted->readbackRGBA(composed.rgba);weighted->readbackDepth(composed.depth);
    composed.serial=weighted->getLastSubmissionSerial();
    active.group->isActive=FALSE;
    action.apply(active.root);Frame withoutMap;
    weighted->readbackRGBA(withoutMap.rgba);weighted->readbackDepth(withoutMap.depth);
    withoutMap.serial=weighted->getLastSubmissionSerial();
    active.group->isActive=TRUE;
    action.apply(active.root);Frame restoredWeighted;
    weighted->readbackRGBA(restoredWeighted.rgba);
    weighted->readbackDepth(restoredWeighted.depth);
    restoredWeighted.serial=weighted->getLastSubmissionSerial();
    std::cout<<"msaa_shadow weighted status="<<int(action.getLastStatus())
      <<" serial="<<composed.serial
      <<" error="<<action.getLastError().getString()<<'\n';
    ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
      composed.rgba.size()==anchor.rgba.size() &&
      composed.depth.size()==anchor.depth.size() &&
      composed.serial==anchor.serial+1 &&
      differentRgb(composed,withoutMap)>25 &&
      restoredWeighted.rgba==composed.rgba &&
      restoredWeighted.depth==composed.depth &&
      withoutMap.serial==composed.serial+1 &&
      restoredWeighted.serial==withoutMap.serial+1,
      "weighted OIT with active MSAA shadow changes a real map and restores A/B/A");
    action.setRenderTarget(nullptr);
  }else ok=false;
  if(caps.backend!=COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION)return ok;
  Scene scene(8,false);SoRenderManager manager;
  manager.setSceneGraph(scene.root);
  manager.setViewportRegion(SbViewportRegion(side,side));
  manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.setBackgroundColor(SbColor4f(0,0,0,0));
  CoinRenderManagerAdapter adapter(manager,SbVec2i32(side,side),options(caps,4));
  auto *target=adapter.getSceneManager()->getRenderTarget();
  if(!check(target && target->getOptions().renderer==caps.renderer &&
    target->getOptions().sampleCount==4,"BGFX budget target identity")){
    manager.setSceneGraph(nullptr);return false;
  }
  target->setDepthReadbackEnabled(TRUE);
  auto capture=[&](Frame &frame){
    const auto status=adapter.render();
    target->readbackRGBA(frame.rgba);target->readbackDepth(frame.depth);
    frame.serial=target->getLastSubmissionSerial();return status;
  };
  Frame budgetStable;auto status=capture(budgetStable);
  ok &= check(status==CoinRenderAction::SUCCESS && budgetStable.serial>0 &&
    budgetStable.rgba.size()==size_t(side)*side*4 &&
    budgetStable.depth.size()==size_t(side)*side,
    "eight-map MSAA frame publishes before view boundary");
  if(!check(bool(target->getPimpl()->backend),"budget anchor has a prepared backend")){manager.setSceneGraph(nullptr);return false;}
  auto*probe=new view_budget_test::Probe(std::move(target->getPimpl()->backend));target->getPimpl()->backend.reset(probe);
  std::vector<SoRenderManager::Superimposition*>layers;std::vector<SoSeparator*>graphs;
  auto setCount=[&](unsigned count){
    while(layers.size()<count){auto*graph=new SoSeparator;graph->ref();graphs.push_back(graph);
      auto*camera=new SoOrthographicCamera;camera->position.setValue(0,0,6);camera->height=4;camera->nearDistance=1;camera->farDistance=20;graph->addChild(camera);
      auto*unlit=new SoLightModel;unlit->model=SoLightModel::BASE_COLOR;graph->addChild(unlit);
      auto*mat=new SoMaterial;mat->diffuseColor.setValue(0,1,0);graph->addChild(mat);
      auto*coord=new SoCoordinate3;const SbVec3f q[]={SbVec3f(-.1f,-.1f,0),SbVec3f(.1f,-.1f,0),SbVec3f(.1f,.1f,0),SbVec3f(-.1f,.1f,0)};
      coord->point.setValues(0,4,q);graph->addChild(coord);auto*face=new SoFaceSet;face->numVertices.set1Value(0,4);graph->addChild(face);
      layers.push_back(manager.addSuperimposition(graph,SoRenderManager::Superimposition::AUTOREDRAW));}
    while(layers.size()>count){manager.removeSuperimposition(layers.back());layers.pop_back();graphs.back()->unref();graphs.pop_back();}
  };
  auto inspect=[&](unsigned count,bool shadows){setCount(count);scene.group->isActive=shadows;
    const auto result=view_budget_test::managerPlan(*probe,*target,[&](){return adapter.render();},ok);
    std::cout<<"msaa_shadow negotiated_preflight layers="<<count<<" shadows="<<shadows<<" status="<<int(result.status)<<" diagnostic="<<result.diagnostic<<'\n';return result;
  };
  unsigned fit=0,excess=0;std::string why;
  const bool foundBoundary=view_budget_test::boundary([&](unsigned n){return inspect(n,true);},fit,excess,why);
  ok &= check(foundBoundary,"finite negotiated shadow/layer view boundary: "+why);
  if(ok){const auto active=inspect(excess,true),ordinary=inspect(excess,false);scene.group->isActive=TRUE;
    ok &= check(view_budget_test::rejected(active)&&ordinary.status==CoinRenderBackendStatus::SUCCESS,"removing only shadow maps admits the first rejected manager frame");}
  if(ok){setCount(fit);Frame admitted;status=capture(admitted);
    ok &= check(status==CoinRenderAction::SUCCESS&&admitted.serial==budgetStable.serial+1,"last admitted shadow/layer count publishes once");budgetStable=admitted;
    if(ok){setCount(excess);Frame rejected;status=capture(rejected);const std::string error=adapter.getLastError().getString();
      std::cout<<"msaa_shadow budget accepted="<<fit<<" rejected="<<excess<<" status="<<int(status)<<" error="<<error<<'\n';
      ok &= check(status==CoinRenderAction::UNSUPPORTED&&(error.find("view")!=std::string::npos||error.find("budget")!=std::string::npos)&&
        rejected.rgba==budgetStable.rgba&&rejected.depth==budgetStable.depth&&rejected.serial==budgetStable.serial,"negotiated shadow/layer excess rejects without publication");
      setCount(fit);Frame restored;status=capture(restored);
      ok &= check(status==CoinRenderAction::SUCCESS&&restored.rgba==budgetStable.rgba&&restored.depth==budgetStable.depth&&restored.serial==budgetStable.serial+1,
        "last admitted eight-map manager frame recovers exactly");}
  }
  setCount(0);
  manager.setSceneGraph(nullptr);return ok;
}
bool inheritedState(const CoinRenderCapabilities &caps){
  bool ok=true;
  for(unsigned samples:{1u,2u,4u}){
    Scene scene(1,false);
    auto *mapCamera=new SoOrthographicCamera;
    mapCamera->position.setValue(0,0,5);mapCamera->height=4;
    mapCamera->nearDistance=1;mapCamera->farDistance=10;
    scene.mapScenes[0]->insertChild(mapCamera,0);
    auto *environment=new SoEnvironment;
    environment->fogType=SoEnvironment::NONE;
    environment->fogColor.setValue(.12f,.61f,.84f);
    environment->fogVisibility=14.f;
    scene.root->insertChild(environment,2);
    auto *clip=new SoClipPlane;
    clip->plane.setValue(SbPlane(SbVec3f(1,0,0),0));
    clip->on=FALSE;scene.root->insertChild(clip,3);
    auto *alpha=new SoAlphaTest;alpha->function=SoAlphaTest::ALWAYS;
    scene.receiver->insertChild(alpha,1);
    Harness harness(caps,samples);Frame base,clipped,alphaPass,alphaFail,restored;
    std::array<Frame,3> fogged;
    const bool a=harness.render(scene.root,base,"authored shadow camera");
    bool fogOk=true;
    const SoEnvironment::FogType fogModes[]={SoEnvironment::HAZE,
      SoEnvironment::FOG,SoEnvironment::SMOKE};
    for(size_t i=0;i<3;++i){
      environment->fogType=fogModes[i];
      fogOk &= harness.render(scene.root,fogged[i],"shadow plus geometric fog");
    }
    environment->fogType=SoEnvironment::NONE;clip->on=TRUE;
    const bool c=harness.render(scene.root,clipped,"shadow plus world clip");
    alpha->function=SoAlphaTest::GREATER;alpha->value=.5f;
    const bool d=harness.render(scene.root,alphaPass,"opaque receiver alpha pass");
    alpha->function=SoAlphaTest::NEVER;
    const bool e=harness.render(scene.root,alphaFail,"receiver alpha rejected");
    alpha->function=SoAlphaTest::ALWAYS;clip->on=FALSE;
    const bool f=harness.render(scene.root,restored,"state A/B/A restored");
    if(!(a&&fogOk&&c&&d&&e&&f)){ok=false;continue;}
    const size_t centre=at(64,64),left=at(32,64),right=at(96,64);
    // Geometry uses eye distance: observer z=6, receiver z=-1, start=0.
    const float ratio=7.f/14.f;
    const float transmittance[]={1.f-ratio,std::exp(-5.545f*ratio),
      std::exp(-std::pow(2.35f*ratio,2.f))};
    const int fogColor[3]={31,156,214};
    for(size_t mode=0;mode<3;++mode)for(int channel=0;channel<3;++channel){
      const int expected=int(std::lround(base.rgba[4*centre+channel]*
        transmittance[mode]+fogColor[channel]*(1.f-transmittance[mode])));
      std::cout<<"msaa_shadow fog samples="<<samples<<" mode="<<mode
        <<" channel="<<channel<<" actual="
        <<int(fogged[mode].rgba[4*centre+channel])
        <<" expected="<<expected<<'\n';
      ok &= check(std::abs(int(fogged[mode].rgba[4*centre+channel])-expected)<=3,
        "HAZE/FOG/SMOKE geometric fog uses analytic eye depth and keeps shadow");
    }
    std::cout<<"msaa_shadow state samples="<<samples
      <<" base_left="<<int(base.rgba[4*left+3])
      <<" clip_left="<<int(clipped.rgba[4*left+3])
      <<" clip_right="<<int(clipped.rgba[4*right+3])
      <<" rejected_centre="<<int(alphaFail.rgba[4*centre+3])<<'\n';
    ok &= check(base.rgba[4*left+3]>0 && clipped.rgba[4*left+3]==0 &&
      clipped.rgba[4*right+3]>0 && clipped.rgba==alphaPass.rgba &&
      clipped.depth==alphaPass.depth && alphaFail.rgba[4*centre+3]==0 &&
      alphaFail.depth[centre]==1.f,
      "world clip and receiver AlphaTest distinguish valid and rejected fragments");
    ok &= check(base.rgba==restored.rgba && base.depth==restored.depth &&
      base.serial+1==fogged[0].serial &&
      fogged[0].serial+1==fogged[1].serial &&
      fogged[1].serial+1==fogged[2].serial &&
      fogged[2].serial+1==clipped.serial &&
      clipped.serial+1==alphaPass.serial && alphaPass.serial+1==alphaFail.serial &&
      alphaFail.serial+1==restored.serial,
      "authored camera, fog, clip and AlphaTest restore with one publication");
  }
  return ok;
}
}
int main(int argc,char **argv){
  if(argc!=2)return 2;
  SoDB::init();CoinRenderAction::initClass();
  const auto caps=capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN);
  if(!requireGpu(caps))return 1;
  const std::string mode(argv[1]);
  if(mode=="--offscreen")return offscreen(caps)?0:1;
  if(mode=="--slots")return slots(caps)?0:1;
  if(mode=="--blend")return blend(caps)?0:1;
  if(mode=="--layers")return layers(caps)?0:1;
  if(mode=="--rejections")return rejections(caps)?0:1;
  if(mode=="--state")return inheritedState(caps)?0:1;
  return 2;
}
