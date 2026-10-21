#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace {
constexpr int side=64;
using Pixels=std::vector<uint8_t>;

class ReferenceAction : public SoGLRenderAction {
public:
  ReferenceAction(SoRenderManager & source) : SoGLRenderAction(SbViewportRegion(side,side)), source(source) {}
  void apply(SoNode * root) override {
    if (inside) { SoGLRenderAction::apply(root); return; }
    inside=true;
    source.render(this,TRUE,TRUE,TRUE);
    inside=false;
  }
private:
  SoRenderManager & source;
  bool inside=false;
};

struct Quad { SoSeparator * root; SoCoordinate3 * coordinates; };
Quad makeQuad(float size,float depth,const SbColor & color,float transparency) {
  auto * root=new SoSeparator; root->ref();
  auto * camera=new SoOrthographicCamera;
  camera->position.setValue(0,0,4); camera->height=4;
  root->addChild(camera);
  auto * light=new SoLightModel; light->model=SoLightModel::BASE_COLOR;
  root->addChild(light);
  auto * material=new SoMaterial; material->diffuseColor.setValue(color);
  material->transparency=transparency;
  root->addChild(material);
  auto * coordinates=new SoCoordinate3;
  const SbVec3f corners[]={{-size,-size,depth},{size,-size,depth},
                            {size,size,depth},{-size,size,depth}};
  coordinates->point.setValues(0,4,corners);
  root->addChild(coordinates);
  auto * face=new SoFaceSet; face->numVertices.set1Value(0,4); root->addChild(face);
  return {root,coordinates};
}
void moveQuad(Quad & quad,float size,float depth) {
  const SbVec3f corners[]={{-size,-size,depth},{size,-size,depth},
                            {size,size,depth},{-size,size,depth}};
  quad.coordinates->point.setValues(0,4,corners);
}
bool captureCoin(CoinRenderManagerAdapter & adapter,Pixels & image) {
  if (adapter.render()!=CoinRenderAction::SUCCESS) {
    std::fprintf(stderr,"technical_blend CoinRender error: %s\n",adapter.getLastError().getString());
    return false;
  }
  Pixels rgba;
  adapter.getSceneManager()->getRenderTarget()->readbackRGBA(rgba);
  if (rgba.size()!=side*side*4u) return false;
  image.resize(side*side*3u);
  for (size_t p=0;p<side*side;++p)
    for (size_t c=0;c<3;++c) image[p*3+c]=rgba[p*4+c];
  return true;
}
bool captureGL(SoRenderManager & manager,SoOffscreenRenderer & renderer,
               SoSeparator * root,Pixels & image) {
  const bool ready=manager.prepareFrame();
  const bool drawn=ready && renderer.render(root);
  const uint8_t * data=drawn?renderer.getBuffer():nullptr;
  manager.finishFrame(drawn && data);
  if (!data) return false;
  image.assign(data,data+side*side*3u);
  return true;
}
bool boundary(const Pixels & image,int x,int y) {
  const size_t p=3u*(y*side+x);
  for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
    const int xx=x+dx, yy=y+dy;
    if ((dx==0 && dy==0) || xx<0 || xx>=side || yy<0 || yy>=side) continue;
    const size_t q=3u*(yy*side+xx);
    for (int c=0;c<3;++c) if (std::abs(int(image[p+c])-int(image[q+c]))>4) return true;
  }
  return false;
}
bool matchNearby(const Pixels & a,const Pixels & b,int x,int y) {
  const size_t p=3u*(y*side+x);
  for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
    const int xx=x+dx, yy=y+dy;
    if (xx<0 || xx>=side || yy<0 || yy>=side) continue;
    const size_t q=3u*(yy*side+xx);
    bool close=true;
    for (int c=0;c<3;++c) close &= std::abs(int(a[p+c])-int(b[q+c]))<=4;
    if (close) return true;
  }
  return false;
}
bool compare(const Pixels & coin,const Pixels & gl,int mode,const char * label) {
  if (coin.size()!=side*side*3u || gl.size()!=coin.size()) return false;
  size_t exact=0,solid=0,edges=0;
  unsigned maxDelta=0;
  for (int y=0;y<side;++y) for (int x=0;x<side;++x) {
    const size_t p=3u*(y*side+x);
    unsigned delta=0;
    for (int c=0;c<3;++c) delta=std::max(delta,unsigned(std::abs(int(coin[p+c])-int(gl[p+c]))));
    exact+=delta==0; maxDelta=std::max(maxDelta,delta);
    if (boundary(coin,x,y) || boundary(gl,x,y))
      edges+=!matchNearby(coin,gl,x,y) || !matchNearby(gl,coin,x,y);
    else solid+=delta>4;
  }
  const size_t center=3u*(32*side+32);
  unsigned centerDelta=0;
  for (int c=0;c<3;++c)
    centerDelta=std::max(centerDelta,unsigned(std::abs(int(coin[center+c])-int(gl[center+c]))));
  std::printf("technical_blend mode=%d scenario=%s exact=%zu/4096 max=%u solid=%zu edges=%zu center_delta=%u coin_center=%u,%u,%u gl_center=%u,%u,%u\n",
              mode,label,exact,maxDelta,solid,edges,centerDelta,
              unsigned(coin[center]),unsigned(coin[center+1]),unsigned(coin[center+2]),
              unsigned(gl[center]),unsigned(gl[center+1]),unsigned(gl[center+2]));
  return solid==0 && edges==0 && centerDelta<=4;
}
bool blueAtCenter(const Pixels & image) {
  const size_t p=3u*(32*side+32);
  return image[p+2]>32 && image[p+2]>image[p+1]*2u;
}
int exercise(SoRenderManager::RenderMode mode) {
  Quad base=makeQuad(.8f,0,SbColor(1,0,0),0);
  Quad overlay=makeQuad(.8f,.5f,SbColor(0,0,1),.5f);
  SoRenderManager coinManager,glManager;
  coinManager.setSceneGraph(base.root); glManager.setSceneGraph(base.root);
  coinManager.setViewportRegion(SbViewportRegion(side,side));
  glManager.setViewportRegion(SbViewportRegion(side,side));
  coinManager.setBackgroundColor(SbColor4f(0,0,0,1));
  glManager.setBackgroundColor(SbColor4f(0,0,0,1));
  coinManager.setRenderMode(mode); glManager.setRenderMode(mode);
  coinManager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
  CoinRenderManagerAdapter adapter(coinManager,SbVec2i32(side,side));
  ReferenceAction glAction(glManager);
  glAction.setTransparencyType(SoGLRenderAction::BLEND);
  glManager.setGLRenderAction(&glAction);
  SoOffscreenRenderer renderer(SbViewportRegion(side,side));
  renderer.setGLRenderAction(&glAction);
  renderer.setComponents(SoOffscreenRenderer::RGB);
  renderer.setBackgroundColor(SbColor(0,0,0));
  Pixels coin,gl,front,rear,cleared;
  if (!captureGL(glManager,renderer,base.root,gl)) {
    overlay.root->unref(); base.root->unref(); return 77;
  }
  bool ok=captureCoin(adapter,coin) && compare(coin,gl,int(mode),"baseline");
  const auto flags=SoRenderManager::Superimposition::AUTOREDRAW |
                   SoRenderManager::Superimposition::ZBUFFERON;
  auto * layer=coinManager.addSuperimposition(overlay.root,flags);
  layer->setTransparencyType(SoGLRenderAction::BLEND);
  auto * glLayer=glManager.addSuperimposition(overlay.root,flags);
  glLayer->setTransparencyType(SoGLRenderAction::BLEND);
  const bool frontCaptured=captureGL(glManager,renderer,base.root,gl) && captureCoin(adapter,coin);
  ok=frontCaptured && compare(coin,gl,int(mode),"front") && blueAtCenter(coin) && blueAtCenter(gl) && ok;
  front=coin;
  moveQuad(overlay,.8f,-.5f);
  const bool rearCaptured=captureGL(glManager,renderer,base.root,gl) && captureCoin(adapter,coin);
  ok=rearCaptured && compare(coin,gl,int(mode),"rear") && ok;
  rear=coin;
  coinManager.removeSuperimposition(layer); glManager.removeSuperimposition(glLayer);
  layer=coinManager.addSuperimposition(overlay.root,flags|SoRenderManager::Superimposition::CLEARZBUFFER);
  layer->setTransparencyType(SoGLRenderAction::BLEND);
  glLayer=glManager.addSuperimposition(overlay.root,flags|SoRenderManager::Superimposition::CLEARZBUFFER);
  glLayer->setTransparencyType(SoGLRenderAction::BLEND);
  const bool clearCaptured=captureGL(glManager,renderer,base.root,gl) && captureCoin(adapter,coin);
  ok=clearCaptured && compare(coin,gl,int(mode),"clear_rear") && blueAtCenter(coin) && blueAtCenter(gl) && ok;
  cleared=coin;
  // A failed frame must preserve the last published image and allow recovery.
  Pixels beforeFailure;
  auto * target=adapter.getSceneManager()->getRenderTarget();
  target->readbackRGBA(beforeFailure);
  const uint64_t serialBefore=target->getLastSubmissionSerial();
  coinManager.setSceneGraph(nullptr);
  const auto failure=adapter.render();
  Pixels afterFailure;
  target->readbackRGBA(afterFailure);
  const uint64_t serialAfter=target->getLastSubmissionSerial();
  coinManager.setSceneGraph(base.root);
  Pixels recovered;
  const bool recoveredOK=captureCoin(adapter,recovered);
  const bool atomic=failure==CoinRenderAction::UNSUPPORTED &&
      beforeFailure.size()==side*side*4u && afterFailure==beforeFailure &&
      serialAfter==serialBefore && recoveredOK && recovered==cleared;
  std::printf("technical_blend mode=%d distinct_front_rear=%d distinct_clear=%d atomic=%d reject_status=%d expected=%d rgba_preserved=%d serial_preserved=%d recovery=%d\n",
              int(mode),int(front!=rear),int(rear!=cleared),int(atomic),int(failure),
              int(CoinRenderAction::UNSUPPORTED),int(afterFailure==beforeFailure),
              int(serialAfter==serialBefore),int(recoveredOK && recovered==cleared));
  ok=ok && front!=rear && rear!=cleared && atomic;
  coinManager.removeSuperimposition(layer); glManager.removeSuperimposition(glLayer);
  overlay.root->unref(); base.root->unref();
  return ok?0:1;
}
}
int main() {
  SoDB::init(); CoinRenderAction::initClass();
  if (!CoinRenderAction::isGpuBackendAvailable()) return 77;
  bool ok=true;
  for (const auto mode : {SoRenderManager::WIREFRAME,SoRenderManager::POINTS,
                          SoRenderManager::BOUNDING_BOX,SoRenderManager::WIREFRAME_OVERLAY,
                          SoRenderManager::HIDDEN_LINE,SoRenderManager::SHADED_HIDDEN_LINES}) {
    const int result=exercise(mode);
    if (result==77) return 77;
    ok &= result==0;
  }
  return ok?0:1;
}
