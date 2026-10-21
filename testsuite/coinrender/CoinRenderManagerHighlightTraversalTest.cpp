#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoInteraction.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/SoPath.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoLineHighlightRenderAction.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSelection.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
namespace {
constexpr int side=96;
bool check(bool good,const char *label) {
  if(!good)std::cerr << "highlight_traversal FAIL " << label << '\n';
  return good;
}
struct Counters {
  int off=0,left=0,right=0;
  std::vector<int> sequence;
};
struct Probe {
  Counters *counters;
  int id;
  Probe(Counters *value,int identity):counters(value),id(identity) {}
};
void countRenderTraversal(void *data,SoAction *action) {
  if(action->isOfType(CoinRenderAction::getClassTypeId()) ||
     action->isOfType(SoGLRenderAction::getClassTypeId())) {
    auto *probe=static_cast<Probe *>(data);
    if(probe->id==0)++probe->counters->off;
    else if(probe->id==1)++probe->counters->left;
    else ++probe->counters->right;
    probe->counters->sequence.push_back(probe->id);
  }
}
SoSeparator *triangle(float x,Probe &probe) {
  auto *branch=new SoSeparator;
  branch->renderCaching=SoSeparator::OFF;
  auto *witness=new SoCallback;witness->setCallback(countRenderTraversal,&probe);
  branch->addChild(witness);
  auto *move=new SoTranslation;move->translation.setValue(x,0,0);
  branch->addChild(move);
  auto *points=new SoCoordinate3;
  const SbVec3f p[]={SbVec3f(-.23f,-.28f,0),
    SbVec3f(.23f,-.28f,0),SbVec3f(-.23f,.28f,0)};
  points->point.setValues(0,3,p);branch->addChild(points);
  auto *face=new SoIndexedFaceSet;
  const int32_t indices[]={0,1,2,-1};
  face->coordIndex.setValues(0,4,indices);branch->addChild(face);
  return branch;
}
struct Scene {
  Counters count;
  Probe offProbe{&count,0},leftProbe{&count,1},rightProbe{&count,2};
  SoSeparator *root=new SoSeparator;
  SoSelection *selection=new SoSelection;
  SoSwitch *previousSwitch=new SoSwitch;
  SoSwitch *switchNode=new SoSwitch;
  SoPath *selectedPath=nullptr,*rightPath=nullptr;
  Scene() {
    root->ref();
    // Coin's native GL cache can replay a display list without calling
    // SoCallback::GLRender again. Count graph traversals, not cache hits.
    root->renderCaching=SoSeparator::OFF;
    selection->renderCaching=SoSeparator::OFF;
    auto *camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,3);camera->height=2;
    camera->nearDistance=1;camera->farDistance=5;root->addChild(camera);
    auto *light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;
    root->addChild(light);
    auto *unrelated=new SoSeparator;
    unrelated->renderCaching=SoSeparator::OFF;
    auto *offCallback=new SoCallback;
    offCallback->setCallback(countRenderTraversal,&offProbe);
    unrelated->addChild(offCallback);root->addChild(unrelated);
    auto *ancestorMaterial=new SoMaterial;
    ancestorMaterial->diffuseColor.setValue(0,.7f,0);
    selection->addChild(ancestorMaterial);
    // This preceding switch genuinely affects state and leaves its switch
    // index available to a later INHERIT. The translations remain outside a
    // Separator so the inherited model state is also exercised.
    auto *previousLeft=new SoTranslation;
    previousLeft->translation.setValue(-.08f,0,0);
    auto *previousRight=new SoTranslation;
    previousRight->translation.setValue(.08f,0,0);
    previousSwitch->addChild(previousLeft);
    previousSwitch->addChild(previousRight);
    previousSwitch->whichChild=SO_SWITCH_NONE;
    selection->addChild(previousSwitch);
    switchNode->addChild(triangle(-.45f,leftProbe));
    switchNode->addChild(triangle(.45f,rightProbe));
    switchNode->whichChild=0;selection->addChild(switchNode);
    root->addChild(selection);
    selectedPath=new SoPath(selection);selectedPath->ref();
    selectedPath->append(2);selectedPath->append(0);
    rightPath=new SoPath(selection);rightPath->ref();
    rightPath->append(2);rightPath->append(1);
    selection->select(selectedPath);
  }
  ~Scene(){selectedPath->unref();rightPath->unref();root->unref();}
  void clear(){count=Counters{};}
};
size_t red(const std::vector<uint8_t> &rgba,int x0,int x1) {
  size_t found=0;
  const size_t stride=rgba.size()==size_t(side)*side*4?4:3;
  for(int y=27;y<69;++y)for(int x=x0;x<x1;++x) {
    const size_t p=stride*(size_t(y)*side+x);
    found+=(rgba[p]>120 && rgba[p]>rgba[p+1]+60);
  }
  return found;
}
struct Frame {std::vector<uint8_t>rgba;std::vector<float>depth;uint64_t serial=0;};
bool expected(int mode,const Counters &counts,size_t left,size_t right) {
  const bool leftActive=mode==0 || mode==SO_SWITCH_ALL;
  const bool rightActive=mode==1 || mode==SO_SWITCH_ALL;
  std::cout << "highlight_traversal switch=" << mode << " callbacks="
            << counts.off << ',' << counts.left << ',' << counts.right
            << " red=" << left << ',' << right << '\n';
  return check(counts.off==1 && counts.left==(leftActive?2:0) &&
      counts.right==(rightActive?1:0),
      "only path ancestors and active selected subtree replay") &&
    check((leftActive?left>8:left==0) && right==0,
      "switch suppresses inactive selected path without highlighting sibling");
}
bool selectedOrder(const Counters &counts,bool rightFirst,
                   size_t left,size_t right) {
  const std::vector<int> wanted=rightFirst?
    std::vector<int>{0,1,2,2,1}:std::vector<int>{0,1,2,1,2};
  std::cout << "highlight_traversal selection_order="
            << (rightFirst?"right-left":"left-right") << " callbacks=";
  for(int id:counts.sequence)std::cout << id;
  std::cout << " red=" << left << ',' << right << '\n';
  return check(counts.sequence==wanted,
      "base order followed by selected path order without off-path replay") &&
    check(left>8 && right>8,"both selected occurrences are visible");
}
bool captured(CoinRenderManagerAdapter &adapter,Frame &frame) {
  const auto status=adapter.render();
  auto *target=adapter.getSceneManager()->getRenderTarget();
  target->readbackRGBA(frame.rgba);target->readbackDepth(frame.depth);
  frame.serial=target->getLastSubmissionSerial();
  if(status!=CoinRenderAction::SUCCESS)
    std::cerr << "highlight_traversal status=" << int(status)
              << " error=" << adapter.getLastError().getString() << '\n';
  return check(status==CoinRenderAction::SUCCESS &&
    frame.rgba.size()==size_t(side)*side*4 &&
    frame.depth.size()==size_t(side)*side && frame.serial>0,
    "single published frame");
}
bool runCapture(bool gpu) {
  Scene scene;SoLineHighlightRenderAction highlight;
  highlight.setColor(SbColor(1,0,0));highlight.setLineWidth(3);
  SoRenderManager manager;
  manager.setViewportRegion(SbViewportRegion(side,side));
  manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.setBackgroundColor(SbColor4f(0,0,0,1));
  manager.setSceneGraph(scene.root);manager.setGLRenderAction(&highlight);
  CoinRenderOptions options;
  if(gpu) {
    CoinRenderCapabilities caps{};
    const bool available=coin_render_query_capabilities(
      COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))==0 &&
      caps.gpu_available && caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN &&
      (caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION ||
       caps.backend==COIN_RENDER_EXPERIMENTAL_RUST);
    std::cout << "highlight_traversal backend=" << caps.backend
              << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
              << " adapter=" << caps.adapter_name << '\n';
    if(!check(available,"real GPU adapter required"))return false;
    options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  }
  CoinRenderManagerAdapter adapter(manager,SbVec2i32(side,side),options);
  auto *target=adapter.getSceneManager()->getRenderTarget();
  if(!check(target && (!gpu || target->getOptions().renderer==options.renderer),
     "requested renderer identity"))return false;
  target->setDepthReadbackEnabled(TRUE);
  if(!gpu)target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
  Frame first,all,none,other,inherit,restored;
  Frame inherited0,inherited1;
  const int modes[]={0,SO_SWITCH_ALL,SO_SWITCH_NONE,1,
                     SO_SWITCH_INHERIT,SO_SWITCH_INHERIT,
                     SO_SWITCH_INHERIT,0};
  const int previous[]={SO_SWITCH_NONE,SO_SWITCH_NONE,SO_SWITCH_NONE,
    SO_SWITCH_NONE,SO_SWITCH_NONE,0,1,SO_SWITCH_NONE};
  Frame *frames[]={&first,&all,&none,&other,&inherit,&inherited0,
                   &inherited1,&restored};
  bool ok=true;uint64_t lastSerial=0;
  for(int i=0;i<8;++i) {
    scene.previousSwitch->whichChild=previous[i];
    scene.switchNode->whichChild=modes[i];scene.clear();
    const bool valid=captured(adapter,*frames[i]);ok &= valid;
    if(valid) {
      const int effective=modes[i]==SO_SWITCH_INHERIT?previous[i]:modes[i];
      std::cout << "highlight_traversal authored_switch=" << modes[i]
                << " preceding_switch=" << previous[i] << '\n';
      ok &= expected(effective,scene.count,
        red(frames[i]->rgba,12,44),red(frames[i]->rgba,52,84));
      ok &= check(lastSerial==0 ? frames[i]->serial>0 :
        frames[i]->serial==lastSerial+1,
        "exactly one publication per switch state");
      lastSerial=frames[i]->serial;
    }
  }
  scene.previousSwitch->whichChild=SO_SWITCH_NONE;
  scene.switchNode->whichChild=SO_SWITCH_ALL;
  Frame orderedRL,orderedLR,orderedRestored;
  const bool rightFirst[]={true,false,true};
  Frame *orderedFrames[]={&orderedRL,&orderedLR,&orderedRestored};
  for(int i=0;i<3;++i) {
    scene.selection->deselectAll();
    scene.selection->select(rightFirst[i]?scene.rightPath:scene.selectedPath);
    scene.selection->select(rightFirst[i]?scene.selectedPath:scene.rightPath);
    scene.clear();
    const bool valid=captured(adapter,*orderedFrames[i]);ok &= valid;
    if(valid) {
      ok &= selectedOrder(scene.count,rightFirst[i],
        red(orderedFrames[i]->rgba,12,44),
        red(orderedFrames[i]->rgba,52,84));
      ok &= check(orderedFrames[i]->serial==lastSerial+1,
        "one publication for two selected paths");
      lastSerial=orderedFrames[i]->serial;
    }
  }
  if(!orderedRL.rgba.empty() && !orderedRestored.rgba.empty())
    ok &= check(orderedRL.rgba==orderedRestored.rgba &&
      orderedRL.depth==orderedRestored.depth,
      "selected path order A/B/A restores RGBA and depth");
  scene.selection->deselectAll();scene.selection->select(scene.selectedPath);
  if(!first.rgba.empty() && !restored.rgba.empty())
    ok &= check(first.rgba==restored.rgba && first.depth==restored.depth,
      "selected path switch A/B/A restores RGBA and depth");
  if(!first.rgba.empty() && !inherited0.rgba.empty()) {
    const size_t unshifted=red(first.rgba,10,13);
    const size_t shifted=red(inherited0.rgba,10,13);
    std::cout << "highlight_traversal inherited_state_roi="
              << unshifted << '/' << shifted << '\n';
    ok &= check(unshifted==0 && shifted>8,
      "INHERIT0 retains preceding switch translation on selected stroke");
  }
  ok &= check(scene.selection->getNumSelected()==1 &&
    scene.selectedPath->getTail()==scene.switchNode->getChild(0),
    "host selection/path remains unchanged");
  manager.setGLRenderAction(nullptr);manager.setSceneGraph(nullptr);
  return ok;
}
bool runNative() {
  Scene scene;SoLineHighlightRenderAction action(SbViewportRegion(side,side));
  action.setColor(SbColor(1,0,0));action.setLineWidth(3);
  SoOffscreenRenderer renderer(SbViewportRegion(side,side));
  renderer.setGLRenderAction(&action);
  renderer.setComponents(SoOffscreenRenderer::RGB);
  renderer.setBackgroundColor(SbColor(0,0,0));
  bool ok=true;
  std::vector<uint8_t> unshifted,shifted;
  const int modes[]={0,SO_SWITCH_ALL,SO_SWITCH_NONE,1,
    SO_SWITCH_INHERIT,SO_SWITCH_INHERIT,SO_SWITCH_INHERIT};
  const int previous[]={SO_SWITCH_NONE,SO_SWITCH_NONE,SO_SWITCH_NONE,
    SO_SWITCH_NONE,SO_SWITCH_NONE,0,1};
  for(int i=0;i<7;++i) {
    const int mode=modes[i];scene.previousSwitch->whichChild=previous[i];
    scene.switchNode->whichChild=mode;scene.clear();
    const bool valid=renderer.render(scene.root) && renderer.getBuffer();
    ok &= check(valid,"native CoinGL frame");
    if(!valid)continue;
    const auto *raw=renderer.getBuffer();
    std::vector<uint8_t>rgb(size_t(side)*side*3);
    for(int y=0;y<side;++y)for(int x=0;x<side;++x)
      for(int c=0;c<3;++c)
        rgb[3u*(size_t(y)*side+x)+c]=
          raw[3u*(size_t(side-1-y)*side+x)+c];
    const int effective=mode==SO_SWITCH_INHERIT?previous[i]:mode;
    std::cout << "highlight_traversal native authored_switch=" << mode
              << " preceding_switch=" << previous[i] << '\n';
    ok &= expected(effective,scene.count,red(rgb,12,44),red(rgb,52,84));
    if(i==0)unshifted=rgb;
    if(i==5)shifted=rgb;
  }
  if(!unshifted.empty() && !shifted.empty()) {
    const size_t plain=red(unshifted,10,13);
    const size_t inherited=red(shifted,10,13);
    std::cout << "highlight_traversal native inherited_state_roi="
              << plain << '/' << inherited << '\n';
    ok &= check(plain==0 && inherited>8,
      "native INHERIT0 retains preceding switch translation");
  }
  scene.previousSwitch->whichChild=SO_SWITCH_NONE;
  scene.switchNode->whichChild=SO_SWITCH_ALL;
  for(bool rightFirst:{true,false,true}) {
    scene.selection->deselectAll();
    scene.selection->select(rightFirst?scene.rightPath:scene.selectedPath);
    scene.selection->select(rightFirst?scene.selectedPath:scene.rightPath);
    scene.clear();
    const bool valid=renderer.render(scene.root) && renderer.getBuffer();
    ok &= check(valid,"native ordered paths frame");
    if(!valid)continue;
    const auto *raw=renderer.getBuffer();
    std::vector<uint8_t>rgb(size_t(side)*side*3);
    for(int y=0;y<side;++y)for(int x=0;x<side;++x)
      for(int c=0;c<3;++c)
        rgb[3u*(size_t(y)*side+x)+c]=
          raw[3u*(size_t(side-1-y)*side+x)+c];
    ok &= selectedOrder(scene.count,rightFirst,red(rgb,12,44),red(rgb,52,84));
  }
  renderer.setGLRenderAction(nullptr);
  return ok;
}
}
int main(int argc,char **argv) {
  if(argc!=2)return 2;
  SoDB::init();SoInteraction::init();CoinRenderAction::initClass();
  if(SoLineHighlightRenderAction::getClassTypeId().isBad())
    SoLineHighlightRenderAction::initClass();
  const std::string mode=argv[1];
  if(mode=="--cpu")return runCapture(false)?0:1;
  if(mode=="--gpu")return runCapture(true)?0:1;
  if(mode=="--native")return runNative()?0:1;
  return 2;
}
