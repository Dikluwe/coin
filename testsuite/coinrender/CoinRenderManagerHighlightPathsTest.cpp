#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoInteraction.h>
#include <Inventor/SoPath.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoBoxHighlightRenderAction.h>
#include <Inventor/actions/SoLineHighlightRenderAction.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSelection.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
namespace {
constexpr int side=96;
bool check(bool value,const char * label) {
  if(!value)std::cerr << "highlight_paths FAIL " << label << '\n';
  return value;
}
struct Box : SoBoxHighlightRenderAction {
  int calls=0;
  void apply(SoNode *node) override {++calls;SoBoxHighlightRenderAction::apply(node);}
};
struct Line : SoLineHighlightRenderAction {
  int calls=0;
  void apply(SoNode *node) override {++calls;SoLineHighlightRenderAction::apply(node);}
};
struct Scene {
  SoSeparator *root=new SoSeparator;
  SoSelection *first=new SoSelection,*second=new SoSelection;
  SoSeparator *shared=new SoSeparator;
  SoSeparator *left=new SoSeparator,*right=new SoSeparator;
  SoPath *leftPath=nullptr,*rightPath=nullptr;
  SoPath *otherPath=nullptr;
  Scene() {
    root->ref();
    auto *cam=new SoOrthographicCamera;
    cam->position.setValue(0,0,3);cam->height=2;
    cam->nearDistance=1;cam->farDistance=5;root->addChild(cam);
    auto *light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;
    root->addChild(light);
    auto *material=new SoMaterial;material->diffuseColor.setValue(1,1,1);
    shared->addChild(material);
    auto *texture=new SoTexture2;
    const uint8_t green[]={0,255,0,255};
    texture->image.setValue(SbVec2s(1,1),4,green);
    texture->model=SoTexture2::MODULATE;shared->addChild(texture);
    auto *coords=new SoCoordinate3;
    const SbVec3f points[]={SbVec3f(-.25f,-.22f,0),
      SbVec3f(.25f,-.22f,0),SbVec3f(-.25f,.23f,0)};
    coords->point.setValues(0,3,points);shared->addChild(coords);
    auto *face=new SoIndexedFaceSet;
    const int32_t ix[]={0,1,2,-1};face->coordIndex.setValues(0,4,ix);
    shared->addChild(face);
    auto *leftMove=new SoTranslation;leftMove->translation.setValue(-.5f,0,0);
    auto *rightMove=new SoTranslation;rightMove->translation.setValue(.5f,0,0);
    left->addChild(leftMove);left->addChild(shared);
    right->addChild(rightMove);right->addChild(shared);
    first->addChild(left);first->addChild(right);
    root->addChild(first);
    auto *other=new SoSeparator;
    auto *otherMove=new SoTranslation;otherMove->translation.setValue(0,.65f,0);
    other->addChild(otherMove);other->addChild(shared);
    second->addChild(other);root->addChild(second);
    leftPath=new SoPath(first);leftPath->ref();leftPath->append(0);leftPath->append(1);
    rightPath=new SoPath(first);rightPath->ref();rightPath->append(1);rightPath->append(1);
    otherPath=new SoPath(second);otherPath->ref();otherPath->append(0);otherPath->append(1);
  }
  ~Scene(){leftPath->unref();rightPath->unref();otherPath->unref();root->unref();}
};
struct Frame {std::vector<uint8_t>rgba;std::vector<float>depth;uint64_t serial=0;};
bool render(CoinRenderManagerAdapter &adapter,Frame &frame,const char * label) {
  const auto status=adapter.render();auto *target=adapter.getSceneManager()->getRenderTarget();
  target->readbackRGBA(frame.rgba);target->readbackDepth(frame.depth);
  frame.serial=target->getLastSubmissionSerial();
  if(status!=CoinRenderAction::SUCCESS)
    std::cerr << "highlight_paths " << label << " error="
              << adapter.getLastError().getString() << '\n';
  return check(status==CoinRenderAction::SUCCESS &&
    frame.rgba.size()==side*side*4u && frame.depth.size()==side*side &&
    frame.serial>0,label);
}
size_t red(const Frame &frame,int x0,int x1,int y0,int y1) {
  size_t count=0;
  for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){
    const size_t p=4u*(size_t(y)*side+x);
    count+=(frame.rgba[p]>120 && frame.rgba[p]>frame.rgba[p+1]+60);
  }
  return count;
}
template<class Highlight>
bool exercise(bool gpu,const CoinRenderOptions &options,const char *kind) {
  Scene scene;Highlight highlight;SoRenderManager manager;
  manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.setViewportRegion(SbViewportRegion(side,side));
  manager.setBackgroundColor(SbColor4f(0,0,0,1));
  manager.setSceneGraph(scene.root);manager.setGLRenderAction(&highlight);
  CoinRenderManagerAdapter adapter(manager,SbVec2i32(side,side),options);
  auto *target=adapter.getSceneManager()->getRenderTarget();
  if(!check(bool(target) && (!gpu || target->getOptions().renderer==options.renderer),
            "explicit renderer target"))return false;
  target->setDepthReadbackEnabled(TRUE);
  if(!gpu)target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
  Frame none,left,both,right,firstRestored;
  bool ok=render(adapter,none,"two instances without selection");
  scene.second->select(scene.otherPath);
  scene.first->select(scene.leftPath);
  ok &= check(scene.first->getNumSelected()==1,"first occurrence selected");
  ok &= render(adapter,left,"left occurrence selected");
  scene.first->select(scene.rightPath);
  ok &= check(scene.first->getNumSelected()==2,"both occurrences selected");
  ok &= render(adapter,both,"two occurrences selected");
  scene.first->deselect(scene.leftPath);
  ok &= render(adapter,right,"right occurrence selected");
  scene.first->deselect(scene.rightPath);
  scene.first->select(scene.leftPath);
  ok &= render(adapter,firstRestored,"left occurrence restored");
  const auto left0=red(none,8,42,31,68),right0=red(none,54,88,31,68);
  const auto left1=red(left,8,42,31,68),right1=red(left,54,88,31,68);
  const auto left2=red(both,8,42,31,68),right2=red(both,54,88,31,68);
  const auto left3=red(right,8,42,31,68),right3=red(right,54,88,31,68);
  const auto top0=red(none,35,61,75,94),top2=red(both,35,61,75,94);
  std::cout << "highlight_paths kind=" << kind << " gpu=" << gpu
            << " left=" << left0 << ',' << left1 << ',' << left2 << ',' << left3
            << " right=" << right0 << ',' << right1 << ',' << right2 << ',' << right3
            << " second_selection=" << top0 << ',' << top2 << '\n';
  ok &= check(left1>left0+4 && right1==right0 &&
    left2>left0+4 && right2>right0+4 &&
    left3==left0 && right3>right0+4,
    "shared node highlights selected path occurrences only");
  ok &= check(top2==top0,"only first SoSelection receives highlight");
  ok &= check(firstRestored.rgba==left.rgba && firstRestored.depth==left.depth &&
    firstRestored.serial>right.serial,"occurrence A/B/A exact restoration");
  ok &= check(highlight.calls==0,"no host GL action apply");
  manager.setGLRenderAction(nullptr);manager.setSceneGraph(nullptr);
  return ok;
}
bool run(bool gpu) {
  CoinRenderOptions options;
  if(gpu) {
    CoinRenderCapabilities caps{};
    const bool found=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
      &caps,sizeof(caps))==0 && caps.gpu_available &&
      (caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION ||
       caps.backend==COIN_RENDER_EXPERIMENTAL_RUST) &&
      caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN;
    std::cout << "highlight_paths backend=" << caps.backend
              << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
              << " adapter=" << caps.adapter_name << '\n';
    if(!check(found,"real GPU backend/renderer"))return false;
    options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  }
  const bool box=exercise<Box>(gpu,options,"box");
  const bool line=exercise<Line>(gpu,options,"line");
  return box&&line;
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
  return 2;
}
