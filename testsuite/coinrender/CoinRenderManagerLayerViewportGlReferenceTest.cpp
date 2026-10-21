#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace {
constexpr int side=64;
struct Image { std::vector<uint8_t> rgb; std::vector<float> depth; };

class LayerViewport : public SoNode {
  SO_NODE_HEADER(LayerViewport);
public:
  static void initClass() { SO_NODE_INIT_CLASS(LayerViewport,SoNode,"Node"); }
  LayerViewport() { SO_NODE_CONSTRUCTOR(LayerViewport); }
  bool enabled=true;
  SbViewportRegion region;
  void callback(SoCallbackAction * action) override {
    if (enabled) SoViewportRegionElement::set(action->getState(),region);
  }
  void GLRender(SoGLRenderAction * action) override {
    if (enabled) SoViewportRegionElement::set(action->getState(),region);
  }
protected:
  ~LayerViewport() override=default;
};
SO_NODE_SOURCE(LayerViewport);

class ManagerGLAction : public SoGLRenderAction {
public:
  ManagerGLAction(SoRenderManager & source)
    : SoGLRenderAction(SbViewportRegion(side,side)), manager(source) {}
  void apply(SoNode * root) override {
    if (insideManager) { SoGLRenderAction::apply(root); return; }
    insideManager=true;
    manager.render(this,TRUE,TRUE,TRUE);
    insideManager=false;
  }
private:
  SoRenderManager & manager;
  bool insideManager=false;
};

SoSeparator * quad(float z, const SbColor & color, LayerViewport ** viewportNode=nullptr) {
  auto * root=new SoSeparator; root->ref();
  if (viewportNode) {
    *viewportNode=new LayerViewport;
    (*viewportNode)->region=SbViewportRegion(side,side);
    (*viewportNode)->region.setViewportPixels(8,8,48,48);
    root->addChild(*viewportNode);
  }
  auto * camera=new SoOrthographicCamera;
  camera->position.setValue(0,0,4); camera->height=4;
  root->addChild(camera);
  auto * lighting=new SoLightModel; lighting->model=SoLightModel::BASE_COLOR;
  root->addChild(lighting);
  auto * material=new SoMaterial; material->diffuseColor.setValue(color);
  root->addChild(material);
  auto * coords=new SoCoordinate3;
  const SbVec3f corners[]={SbVec3f(-2,-2,z),SbVec3f(2,-2,z),
                           SbVec3f(2,2,z),SbVec3f(-2,2,z)};
  coords->point.setValues(0,4,corners); root->addChild(coords);
  auto * face=new SoFaceSet; face->numVertices.set1Value(0,4);
  root->addChild(face);
  return root;
}

bool coinImage(CoinRenderManagerAdapter & adapter, Image & image) {
  if (adapter.render()!=CoinRenderAction::SUCCESS) {
    std::fprintf(stderr,"layer_viewport CoinRender: %s\n",adapter.getLastError().getString());
    return false;
  }
  auto * target=adapter.getSceneManager()->getRenderTarget();
  std::vector<uint8_t> rgba;
  target->readbackRGBA(rgba); target->readbackDepth(image.depth);
  if (rgba.size()!=side*side*4u || image.depth.size()!=side*side) return false;
  image.rgb.resize(side*side*3u);
  for (size_t p=0;p<side*side;++p)
    for (size_t c=0;c<3;++c) image.rgb[3*p+c]=rgba[4*p+c];
  return true;
}

bool glImage(SoRenderManager & manager, SoOffscreenRenderer & renderer,
             SoNode * scene, Image & image) {
  const bool ready=manager.prepareFrame();
  const bool drawn=ready && renderer.render(scene);
  const uint8_t * buffer=drawn?renderer.getBuffer():nullptr;
  manager.finishFrame(drawn && buffer);
  if (!buffer) return false;
  image.rgb.assign(buffer,buffer+side*side*3u);
  return true;
}

bool colorAt(const Image & image, int x, int y, int channel) {
  const size_t p=3u*(size_t(y)*side+size_t(x));
  return image.rgb[p+channel]>160 &&
         image.rgb[p+(channel+1)%3]<80 && image.rgb[p+(channel+2)%3]<80;
}

bool compare(const Image & coin, const Image & gl, const char * scenario,
             bool greenInside, bool greenOutside) {
  if (coin.rgb.size()!=side*side*3u || gl.rgb.size()!=coin.rgb.size()) return false;
  size_t exact=0, mismatch=0, outsideGreen=0;
  unsigned maxDelta=0;
  for (int y=0;y<side;++y) for (int x=0;x<side;++x) {
    const size_t p=3u*(size_t(y)*side+size_t(x));
    unsigned delta=0;
    for (size_t c=0;c<3;++c)
      delta=std::max(delta,unsigned(std::abs(int(coin.rgb[p+c])-int(gl.rgb[p+c]))));
    exact+=delta==0; mismatch+=delta>4; maxDelta=std::max(maxDelta,delta);
    if ((x<8 || x>=56 || y<8 || y>=56) && colorAt(coin,x,y,1)) ++outsideGreen;
  }
  const bool centers=colorAt(coin,32,32,greenInside?1:0) &&
                     colorAt(gl,32,32,greenInside?1:0) &&
                     colorAt(coin,4,32,greenOutside?1:0) &&
                     colorAt(gl,4,32,greenOutside?1:0);
  std::printf("layer_viewport scenario=%s exact=%zu/4096 mismatch=%zu max_delta=%u outside_green=%zu centers=%d depth_center=%.9g depth_outside=%.9g\n",
              scenario,exact,mismatch,maxDelta,outsideGreen,int(centers),
              coin.depth[32u*side+32u],coin.depth[32u*side+4u]);
  // This opaque fixture has exact CoinGL agreement, including its edges.
  // Any changed pixel must fail the oracle.
  return centers && exact==side*side &&
         (greenOutside ? outsideGreen>1000 : outsideGreen==0);
}

int exercise(bool clearDepth, bool rear=false) {
  auto * base=quad(0,SbColor(1,0,0));
  LayerViewport * viewport=nullptr;
  auto * foreground=quad((clearDepth || rear)?-.5f:.5f,SbColor(0,1,0),&viewport);
  SoRenderManager coinManager,glManager;
  for (SoRenderManager * manager : {&coinManager,&glManager}) {
    manager->setSceneGraph(base);
    manager->setViewportRegion(SbViewportRegion(side,side));
    manager->setBackgroundColor(SbColor4f(0,0,0,1));
  }
  CoinRenderManagerAdapter adapter(coinManager,SbVec2i32(side,side));
  auto * target=adapter.getSceneManager()->getRenderTarget();
  if (!target || !target->setDepthReadbackEnabled(TRUE)) {
    foreground->unref(); base->unref(); return 1;
  }
  ManagerGLAction action(glManager);
  glManager.setGLRenderAction(&action);
  SoOffscreenRenderer renderer(SbViewportRegion(side,side));
  renderer.setGLRenderAction(&action);
  renderer.setComponents(SoOffscreenRenderer::RGB);
  renderer.setBackgroundColor(SbColor(0,0,0));
  const int flags=SoRenderManager::Superimposition::AUTOREDRAW |
                  SoRenderManager::Superimposition::ZBUFFERON |
                  (clearDepth?SoRenderManager::Superimposition::CLEARZBUFFER:0);
  auto * coinLayer=coinManager.addSuperimposition(foreground,flags);
  auto * glLayer=glManager.addSuperimposition(foreground,flags);
  Image coin,gl;
  if (!glImage(glManager,renderer,base,gl)) {
    coinManager.removeSuperimposition(coinLayer);
    glManager.removeSuperimposition(glLayer);
    foreground->unref(); base->unref(); return 77;
  }
  bool ok=coinImage(adapter,coin) &&
          compare(coin,gl,clearDepth?"clear_sub":rear?"z_rear_sub":"z_front_sub",
                  !rear || clearDepth,false);
  const float subDepth=coin.depth[32u*side+32u];
  const float outsideDepth=coin.depth[32u*side+4u];
  viewport->enabled=false;
  ok=glImage(glManager,renderer,base,gl) && coinImage(adapter,coin) &&
     compare(coin,gl,clearDepth?"clear_restored":rear?"z_rear_restored":"z_front_restored",
             !rear || clearDepth,!rear || clearDepth) && ok;
  const float restoredDepth=coin.depth[32u*side+4u];
  if (clearDepth) ok &= restoredDepth>outsideDepth && restoredDepth<1.0f;
  else if (rear) ok &= std::abs(restoredDepth-outsideDepth)<1e-5f;
  else ok &= restoredDepth<outsideDepth && restoredDepth<1.0f;
  std::printf("layer_viewport clear=%d rear=%d depth_sub=%.9g outside=%.9g restored=%.9g ordered=%d\n",
              int(clearDepth),int(rear),subDepth,outsideDepth,restoredDepth,int(ok));
  coinManager.removeSuperimposition(coinLayer);
  glManager.removeSuperimposition(glLayer);
  foreground->unref(); base->unref();
  return ok?0:1;
}
}

int main() {
  SoDB::init(); LayerViewport::initClass(); CoinRenderAction::initClass();
  if (!CoinRenderAction::isGpuBackendAvailable()) return 77;
  const int z=exercise(false), rear=exercise(false,true), clear=exercise(true);
  if (z==77 || rear==77 || clear==77) return 77;
  return (z==0 && rear==0 && clear==0)?0:1;
}
