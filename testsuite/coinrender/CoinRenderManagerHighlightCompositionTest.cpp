#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoInteraction.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoBoxHighlightRenderAction.h>
#include <Inventor/actions/SoLineHighlightRenderAction.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSelection.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
namespace {
constexpr int side=96;
bool separateTransparencyOverride() {
  const char *value=std::getenv("COIN_SEPARATE_DIFFUSE_TRANSPARENCY_OVERRIDE");
  return value && std::string(value)=="1";
}
bool check(bool ok,const char * label) {
  if(!ok)std::cerr << "highlight_composition FAIL " << label << '\n';
  return ok;
}
template<class Parent> struct CountAction : Parent {
  int calls=0;
  void apply(SoNode *root) override {++calls;Parent::apply(root);}
};
struct Scene {
  SoSeparator *root=new SoSeparator;
  SoSelection *selection=new SoSelection;
  SoSeparator *selected=new SoSeparator;
  SoMaterial *selectedMaterial=new SoMaterial;
  SoClipPlane *selectedClip=new SoClipPlane;
  SoSeparator *occluder=new SoSeparator;
  Scene() {
    root->ref();
    auto *camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,3);camera->height=2;
    camera->nearDistance=1;camera->farDistance=5;root->addChild(camera);
    auto *lighting=new SoLightModel;lighting->model=SoLightModel::BASE_COLOR;
    root->addChild(lighting);
    selectedMaterial->diffuseColor.setValue(1,1,1);
    selectedMaterial->transparency=.5f;selected->addChild(selectedMaterial);
    auto *tex=new SoTexture2;const uint8_t green[]={0,255,0,255};
    tex->image.setValue(SbVec2s(1,1),4,green);tex->model=SoTexture2::MODULATE;
    selected->addChild(tex);
    selectedClip->plane.setValue(SbPlane(SbVec3f(0,1,0),0.f));
    selectedClip->on=FALSE;selected->addChild(selectedClip);
    auto *coordinates=new SoCoordinate3;
    const SbVec3f tri[]={SbVec3f(-.72f,-.62f,0),
      SbVec3f(.72f,-.62f,0),SbVec3f(-.72f,.7f,0)};
    coordinates->point.setValues(0,3,tri);selected->addChild(coordinates);
    auto *shape=new SoIndexedFaceSet;const int32_t triangle[]={0,1,2,-1};
    shape->coordIndex.setValues(0,4,triangle);selected->addChild(shape);
    selection->addChild(selected);root->addChild(selection);
    auto *blue=new SoMaterial;blue->diffuseColor.setValue(0,0,1);
    occluder->addChild(blue);
    auto *quadCoordinates=new SoCoordinate3;
    const SbVec3f quad[]={SbVec3f(.02f,-.8f,.45f),
      SbVec3f(.83f,-.8f,.45f),SbVec3f(.83f,.84f,.45f),
      SbVec3f(.02f,.84f,.45f)};
    quadCoordinates->point.setValues(0,4,quad);occluder->addChild(quadCoordinates);
    auto *quadShape=new SoIndexedFaceSet;
    const int32_t ix[]={0,1,2,3,-1};quadShape->coordIndex.setValues(0,5,ix);
    occluder->addChild(quadShape);root->addChild(occluder);
  }
  ~Scene(){root->unref();}
};
struct Frame {std::vector<uint8_t>rgba;std::vector<float>depth;uint64_t serial=0;};
bool render(CoinRenderManagerAdapter &adapter,Frame &frame,const char * label) {
  const auto status=adapter.render();
  auto *target=adapter.getSceneManager()->getRenderTarget();
  target->readbackRGBA(frame.rgba);target->readbackDepth(frame.depth);
  frame.serial=target->getLastSubmissionSerial();
  if(status!=CoinRenderAction::SUCCESS)
    std::cerr << "highlight_composition " << label << " status=" << int(status)
              << " error=" << adapter.getLastError().getString() << '\n';
  return check(status==CoinRenderAction::SUCCESS &&
    frame.rgba.size()==side*side*4u && frame.depth.size()==side*side &&
    frame.serial>0,label);
}
size_t red(const Frame &frame,int x0,int x1,int y0,int y1) {
  size_t count=0;
  for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x) {
    const size_t p=4u*(size_t(y)*side+x);
    count+=(frame.rgba[p]>frame.rgba[p+1]+50 && frame.rgba[p]>100);
  }
  return count;
}
template<class Action>
bool exercise(bool gpu,const CoinRenderOptions &options,const char *kind) {
  Scene scene;Action highlight;
  highlight.setColor(SbColor(1,0,0));highlight.setLineWidth(3);
  highlight.setLinePattern(0xffff);
  highlight.setTransparencyType(SoGLRenderAction::BLEND);
  SoRenderManager manager;
  manager.setViewportRegion(SbViewportRegion(side,side));
  manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.setBackgroundColor(SbColor4f(0,0,0,1));
  manager.setSceneGraph(scene.root);manager.setGLRenderAction(&highlight);
  CoinRenderManagerAdapter adapter(manager,SbVec2i32(side,side),options);
  auto *target=adapter.getSceneManager()->getRenderTarget();
  if(!check(bool(target) && (!gpu || target->getOptions().renderer==options.renderer),
            "composition renderer"))return false;
  target->setDepthReadbackEnabled(TRUE);
  if(!gpu)target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
  Frame bare,translucent,bareOpaque,opaque,restored;
  bool ok=render(adapter,bare,"translucent base");
  scene.selection->select(scene.selected);
  ok &= render(adapter,translucent,"translucent selected");
  const size_t left=red(translucent,8,47,15,80);
  const size_t right=red(translucent,49,87,15,80);
  std::cout << "highlight_composition kind=" << kind << " gpu=" << gpu
            << " red_left=" << left << " red_occluded=" << right << '\n';
  ok &= check(left>10,"highlight visible beside occluder");
  ok &= check(right==0,"closer opaque geometry occludes highlight");
  // The sibling occluder is authored blue; the highlight must not repaint it.
  const size_t center=4u*(size_t(side/2)*side+70);
  ok &= check(translucent.rgba[center+2]>180 && translucent.rgba[center]<40,
              "occluder retains blue material and depth ordering");
  scene.selectedMaterial->transparency=0.f;
  scene.selection->deselectAll();
  ok &= render(adapter,bareOpaque,"opaque base control");
  scene.selection->select(scene.selected);
  ok &= render(adapter,opaque,"opaque selected control");
  ok &= check(opaque.rgba!=translucent.rgba &&
              opaque.serial>translucent.serial,
              "selected material alpha updates composition");
  size_t strokeOnly=0,alphaDifference=0,boxUnchanged=0;
  for(size_t pixel=0;pixel<size_t(side)*side;++pixel) {
    const size_t p=4*pixel;
    const bool bothBaseBlack=bare.rgba[p]<5 && bare.rgba[p+1]<5 &&
      bare.rgba[p+2]<5 && bareOpaque.rgba[p]<5 &&
      bareOpaque.rgba[p+1]<5 && bareOpaque.rgba[p+2]<5;
    const int halfRed=translucent.rgba[p],fullRed=opaque.rgba[p];
    if(bothBaseBlack && halfRed>40 && fullRed>40) {
      ++strokeOnly;
      alphaDifference+=(fullRed>halfRed+30);
      boxUnchanged+=(std::abs(fullRed-halfRed)<=2);
    }
  }
  std::cout << "highlight_composition kind=" << kind
            << " stroke_only=" << strokeOnly
            << " alpha_difference=" << alphaDifference
            << " box_unchanged=" << boxUnchanged << '\n';
  const bool lineInheritsAlpha=std::string(kind)=="line" &&
    separateTransparencyOverride();
  ok &= check(strokeOnly>8 && (lineInheritsAlpha
      ?alphaDifference>8:boxUnchanged>8),
      "stroke-only pixels obey native diffuse/transparency override policy");
  scene.selectedMaterial->transparency=.5f;
  ok &= render(adapter,restored,"translucent restoration") &&
    check(restored.rgba==translucent.rgba && restored.depth==translucent.depth &&
      restored.serial>opaque.serial,"material alpha A/B/A exact restoration");
  if(std::string(kind)=="line") {
    // This plane removes the lower half of the selected face, including its
    // replayed outline; the closer blue sibling remains in front on the right.
    const size_t beforeLower=red(restored,8,47,54,87);
    scene.selectedClip->on=TRUE;
    Frame clipped;
    const bool clippedCaptured=render(adapter,clipped,"selected line with clip plane");
    ok &= clippedCaptured;
    if(clippedCaptured) {
      const size_t afterLower=red(clipped,8,47,54,87);
      const size_t afterUpper=red(clipped,8,47,8,46);
      std::cout << "highlight_composition clip lower=" << beforeLower
                << '/' << afterLower << " upper=" << afterUpper << '\n';
      ok &= check(beforeLower>8 && afterLower==0 && afterUpper>8,
        "line highlight follows selected face clipping without losing upper outline");
      ok &= check(red(clipped,49,87,15,80)==0 &&
        clipped.rgba[center+2]>180 && clipped.rgba[center]<40,
        "closer blue foreground sibling still occludes clipped highlight");
    }
    scene.selectedClip->on=FALSE;
    Frame unclipped;
    ok &= render(adapter,unclipped,"clip restoration") &&
      check(unclipped.rgba==restored.rgba && unclipped.depth==restored.depth &&
        unclipped.serial>clipped.serial,"clip A/B/A exact restoration");
  }
  // A true manager foreground layer is submitted after the highlighted base.
  // Its quad is behind the selected triangle, so ZBUFFEROFF is also a depth
  // ordering witness rather than just a test for an opaque overlay.
  auto *foreground=new SoSeparator;foreground->ref();
  auto *foregroundCamera=new SoOrthographicCamera;
  foregroundCamera->position.setValue(0,0,3);foregroundCamera->height=2;
  foregroundCamera->nearDistance=1;foregroundCamera->farDistance=5;
  foreground->addChild(foregroundCamera);
  auto *foregroundLight=new SoLightModel;
  foregroundLight->model=SoLightModel::BASE_COLOR;
  foreground->addChild(foregroundLight);
  auto *foregroundMaterial=new SoMaterial;
  foregroundMaterial->diffuseColor.setValue(0,1,0);
  foreground->addChild(foregroundMaterial);
  auto *foregroundCoordinates=new SoCoordinate3;
  const SbVec3f foregroundCorners[]={
    SbVec3f(-.86f,-.22f,-.8f),SbVec3f(-.54f,-.22f,-.8f),
    SbVec3f(-.54f,.22f,-.8f),SbVec3f(-.86f,.22f,-.8f)};
  foregroundCoordinates->point.setValues(0,4,foregroundCorners);
  foreground->addChild(foregroundCoordinates);
  auto *foregroundQuad=new SoFaceSet;
  foregroundQuad->numVertices.set1Value(0,4);
  foreground->addChild(foregroundQuad);
  auto *layer=manager.addSuperimposition(foreground,
    SoRenderManager::Superimposition::AUTOREDRAW);
  Frame layered,unlayered;
  const bool layeredCaptured=render(adapter,layered,"foreground over highlight");
  ok &= layeredCaptured;
  if(layeredCaptured) {
    const Frame &before=std::string(kind)=="line"?restored:translucent;
    size_t replacedStroke=0;
    for(int y=39;y<57;++y)for(int x=7;x<24;++x) {
      const size_t p=4u*(size_t(y)*side+x);
      const bool wasRed=before.rgba[p]>before.rgba[p+1]+50 &&
        before.rgba[p]>100;
      const bool nowGreen=layered.rgba[p+1]>layered.rgba[p]+80 &&
        layered.rgba[p+1]>layered.rgba[p+2]+80;
      replacedStroke+=(wasRed && nowGreen);
    }
    std::cout << "highlight_composition kind=" << kind
              << " foreground_replaced_stroke=" << replacedStroke << '\n';
    ok &= check(replacedStroke>5,
      "ZBUFFEROFF manager foreground covers selected highlight stroke");
  }
  layer->setEnabled(FALSE);
  const bool unlayeredCaptured=render(adapter,unlayered,"foreground disabled");
  ok &= unlayeredCaptured;
  if(unlayeredCaptured) {
    const Frame &before=std::string(kind)=="line"?restored:translucent;
    ok &= check(unlayered.rgba==before.rgba && unlayered.depth==before.depth &&
      unlayered.serial>layered.serial,
      "foreground enable/disable restores highlighted frame exactly");
  }
  manager.removeSuperimposition(layer);foreground->unref();
  ok &= check(highlight.calls==0,"composition never applies host GL action");
  manager.setGLRenderAction(nullptr);manager.setSceneGraph(nullptr);
  return ok;
}
bool run(bool gpu) {
  CoinRenderOptions options;
  if(gpu) {
    CoinRenderCapabilities caps{};
    const bool found=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
      &caps,sizeof(caps))==0 && caps.gpu_available &&
      (caps.backend==COIN_RENDER_EXPERIMENTAL_RUST ||
       caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION) &&
      caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN;
    std::cout << "highlight_composition backend=" << caps.backend
              << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
              << " adapter=" << caps.adapter_name << '\n';
    if(!check(found,"real GPU required"))return false;
    options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  }
  const bool box=exercise<CountAction<SoBoxHighlightRenderAction>>(gpu,options,"box");
  const bool line=exercise<CountAction<SoLineHighlightRenderAction>>(gpu,options,"line");
  return box&&line;
}
template<class Action>
bool nativeAlpha(const char *kind) {
  Scene scene;
  Action action(SbViewportRegion(side,side));
  action.setColor(SbColor(1,0,0));
  action.setLineWidth(3);
  action.setLinePattern(0xffff);
  action.setTransparencyType(SoGLRenderAction::BLEND);
  SoOffscreenRenderer renderer(SbViewportRegion(side,side));
  renderer.setGLRenderAction(&action);
  renderer.setComponents(SoOffscreenRenderer::RGB);
  renderer.setBackgroundColor(SbColor(0,0,0));
  auto capture=[&](std::vector<uint8_t> &out,const char *label) {
    if(!renderer.render(scene.root) || !renderer.getBuffer()) {
      std::cerr << "highlight_composition native " << label << " render failed\n";
      return false;
    }
    const auto *raw=renderer.getBuffer();
    out.resize(size_t(side)*side*3);
    // SoOffscreenRenderer RGB is bottom-up; all comparisons use top-down rows.
    for(int y=0;y<side;++y)for(int x=0;x<side;++x)
      for(int c=0;c<3;++c)
        out[3u*(size_t(y)*side+x)+c]=
          raw[3u*(size_t(side-1-y)*side+x)+c];
    return true;
  };
  std::vector<uint8_t> bare,half,bareOpaque,full;
  bool ok=capture(bare,"bare translucent");
  scene.selection->select(scene.selected);
  ok &= capture(half,"selected translucent");
  scene.selectedMaterial->transparency=0.f;
  scene.selection->deselectAll();
  ok &= capture(bareOpaque,"bare opaque");
  scene.selection->select(scene.selected);
  ok &= capture(full,"selected opaque");
  if(!ok)return false;
  size_t strokeOnly=0,alphaDifference=0,unchanged=0;
  int firstX=-1,firstY=-1,firstHalf=-1,firstFull=-1;
  for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
    const size_t p=3u*(size_t(y)*side+x);
    const bool bothBaseBlack=bare[p]<5 && bare[p+1]<5 && bare[p+2]<5 &&
      bareOpaque[p]<5 && bareOpaque[p+1]<5 && bareOpaque[p+2]<5;
    const int halfRed=half[p],fullRed=full[p];
    if(bothBaseBlack && halfRed>40 && fullRed>40) {
      ++strokeOnly;
      alphaDifference+=(fullRed>halfRed+30);
      unchanged+=(std::abs(fullRed-halfRed)<=2);
      if(firstX<0) {
        firstX=x;firstY=y;firstHalf=halfRed;firstFull=fullRed;
      }
    }
  }
  const char *split=std::getenv("COIN_SEPARATE_DIFFUSE_TRANSPARENCY_OVERRIDE");
  std::cout << "highlight_composition native kind=" << kind
            << " separate_override=" << (split?split:"unset")
            << " stroke_only=" << strokeOnly
            << " alpha_difference=" << alphaDifference
            << " unchanged=" << unchanged
            << " first=" << firstX << ',' << firstY
            << " first_red=" << firstHalf << '/' << firstFull << '\n';
  const bool lineInheritsAlpha=std::string(kind)=="line" &&
    separateTransparencyOverride();
  bool profile=check(strokeOnly>8 && (lineInheritsAlpha
      ?alphaDifference>8:unchanged>8),
      "native highlight obeys diffuse/transparency override policy");
  if(std::string(kind)=="line") {
    const auto redRegion=[&](const std::vector<uint8_t> &image,
                             int y0,int y1) {
      size_t count=0;
      for(int y=y0;y<y1;++y)for(int x=8;x<47;++x) {
        const size_t p=3u*(size_t(y)*side+x);
        count+=(image[p]>image[p+1]+50 && image[p]>100);
      }
      return count;
    };
    scene.selectedClip->on=TRUE;
    std::vector<uint8_t> clipped;
    profile &= capture(clipped,"native line clipped");
    if(!clipped.empty()) {
      const size_t beforeLower=redRegion(full,54,87);
      const size_t afterLower=redRegion(clipped,54,87);
      const size_t afterUpper=redRegion(clipped,8,46);
      std::cout << "highlight_composition native clip lower="
                << beforeLower << '/' << afterLower
                << " upper=" << afterUpper << '\n';
      profile &= check(beforeLower>8 && afterLower==0 && afterUpper>8,
        "native line highlight follows selected face clip plane");
    }
  }
  renderer.setGLRenderAction(nullptr);
  return profile;
}
}
int main(int argc,char **argv) {
  if(argc!=2)return 2;SoDB::init();SoInteraction::init();CoinRenderAction::initClass();
  if(SoBoxHighlightRenderAction::getClassTypeId().isBad())
    SoBoxHighlightRenderAction::initClass();
  if(SoLineHighlightRenderAction::getClassTypeId().isBad())
    SoLineHighlightRenderAction::initClass();
  const std::string mode=argv[1];
  if(mode=="--cpu")return run(false)?0:1;
  if(mode=="--gpu")return run(true)?0:1;
  if(mode=="--native") {
    const bool box=nativeAlpha<SoBoxHighlightRenderAction>("box");
    const bool line=nativeAlpha<SoLineHighlightRenderAction>("line");
    return box&&line?0:1;
  }
  return 2;
}
