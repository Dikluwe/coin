#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

class ManagerGLAction : public SoGLRenderAction {
public:
  ManagerGLAction(SoRenderManager & source, const SbViewportRegion & viewport)
    : SoGLRenderAction(viewport), manager(source) {}
  void apply(SoNode * root) override {
    if (insideManager) { SoGLRenderAction::apply(root); return; }
    insideManager = true;
    manager.render(this, TRUE, TRUE, TRUE);
    insideManager = false;
  }
private:
  SoRenderManager & manager;
  bool insideManager = false;
};

struct Scene { SoSeparator * root; SoCoordinate3 * points; };
struct Image { std::vector<uint8_t> rgb; std::vector<float> depth; };

static Scene quad(float extent, float z, const SbColor & color) {
  auto * root = new SoSeparator; root->ref();
  auto * camera = new SoOrthographicCamera;
  camera->position.setValue(0,0,4); camera->height=4;
  root->addChild(camera);
  auto * model = new SoLightModel; model->model=SoLightModel::BASE_COLOR;
  root->addChild(model);
  auto * material = new SoMaterial; material->diffuseColor.setValue(color);
  root->addChild(material);
  auto * points = new SoCoordinate3;
  const SbVec3f corners[] = {
    SbVec3f(-extent,-extent,z), SbVec3f(extent,-extent,z),
    SbVec3f(extent,extent,z), SbVec3f(-extent,extent,z)
  };
  points->point.setValues(0,4,corners); root->addChild(points);
  auto * face = new SoFaceSet; face->numVertices.set1Value(0,4);
  root->addChild(face);
  return {root,points};
}

static void setZ(Scene & scene, float extent, float z) {
  const SbVec3f corners[] = {
    SbVec3f(-extent,-extent,z), SbVec3f(extent,-extent,z),
    SbVec3f(extent,extent,z), SbVec3f(-extent,extent,z)
  };
  scene.points->point.setValues(0,4,corners);
}

static bool coinImage(CoinRenderManagerAdapter & adapter, Image & result) {
  if (adapter.render()!=CoinRenderAction::SUCCESS) {
    std::fprintf(stderr,"draw_modes_gl CoinRender status: %s\n",
                 adapter.getLastError().getString());
    return false;
  }
  std::vector<uint8_t> rgba;
  CoinRenderTarget * target=adapter.getSceneManager()->getRenderTarget();
  target->readbackRGBA(rgba);
  target->readbackDepth(result.depth);
  if (rgba.size()!=64u*64u*4u || result.depth.size()!=64u*64u) return false;
  result.rgb.resize(64u*64u*3u);
  for (size_t p=0;p<64u*64u;++p)
    for (size_t c=0;c<3u;++c) result.rgb[3u*p+c]=rgba[4u*p+c];
  return true;
}

static bool glImage(SoRenderManager & manager, ManagerGLAction & action,
                    SoOffscreenRenderer & renderer, SoSeparator * root,
                    Image & result) {
  const bool ready=manager.prepareFrame();
  const bool drawn=ready && renderer.render(root);
  const uint8_t * buffer=drawn?renderer.getBuffer():nullptr;
  manager.finishFrame(drawn && buffer);
  if (!buffer) return false;
  result.rgb.assign(buffer,buffer+64u*64u*3u);
  return true;
}

static bool closeColor(const Image & from, size_t at,
                       const Image & to, int x, int y) {
  const size_t source=3u*at;
  for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
    const int xx=x+dx, yy=y+dy;
    if (xx<0 || xx>=64 || yy<0 || yy>=64) continue;
    const size_t target=3u*(size_t(yy)*64u+size_t(xx));
    if (std::abs(int(from.rgb[source])-int(to.rgb[target]))<=4 &&
        std::abs(int(from.rgb[source+1])-int(to.rgb[target+1]))<=4 &&
        std::abs(int(from.rgb[source+2])-int(to.rgb[target+2]))<=4)
      return true;
  }
  return false;
}

static bool boundaryAt(const Image & image, int x, int y) {
  const size_t at=3u*(size_t(y)*64u+size_t(x));
  for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
    if (dx==0 && dy==0) continue;
    const int xx=x+dx, yy=y+dy;
    if (xx<0 || xx>=64 || yy<0 || yy>=64) continue;
    const size_t near=3u*(size_t(yy)*64u+size_t(xx));
    for (size_t c=0;c<3u;++c)
      if (std::abs(int(image.rgb[at+c])-int(image.rgb[near+c]))>4)
        return true;
  }
  return false;
}

static bool compare(const Image & coin, const Image & gl,
                    SoRenderManager::RenderMode mode, const char * scenario) {
  if (coin.rgb.size()!=64u*64u*3u || gl.rgb.size()!=coin.rgb.size()) return false;
  size_t exact=0, solidMismatch=0, borderCoinUnmatched=0, borderGlUnmatched=0;
  size_t borderPixels=0, foregroundGreenCoin=0, foregroundGreenGl=0;
  unsigned maxDirect=0, maxForeground=0, maxBase=0, maxBorder=0;
  for (int y=0;y<64;++y) for (int x=0;x<64;++x) {
    const size_t p=size_t(y)*64u+size_t(x);
    unsigned pixelDelta=0;
    for (size_t c=0;c<3u;++c)
      pixelDelta=std::max(pixelDelta,unsigned(std::abs(int(coin.rgb[3u*p+c])-
                                                      int(gl.rgb[3u*p+c]))));
    exact+=pixelDelta==0;
    maxDirect=std::max(maxDirect,pixelDelta);
    const bool border=boundaryAt(coin,x,y) || boundaryAt(gl,x,y);
    if (border) {
      ++borderPixels;
      maxBorder=std::max(maxBorder,pixelDelta);
      borderCoinUnmatched+=!closeColor(coin,p,gl,x,y);
      borderGlUnmatched+=!closeColor(gl,p,coin,x,y);
    } else {
      solidMismatch+=pixelDelta>4;
    }
    if (x>=28 && x<36 && y>=28 && y<36) {
      maxForeground=std::max(maxForeground,pixelDelta);
      foregroundGreenCoin+=coin.rgb[3u*p+1]>coin.rgb[3u*p]*2u &&
                            coin.rgb[3u*p+1]>coin.rgb[3u*p+2]*2u;
      foregroundGreenGl+=gl.rgb[3u*p+1]>gl.rgb[3u*p]*2u &&
                          gl.rgb[3u*p+1]>gl.rgb[3u*p+2]*2u;
    }
    if (x>=19 && x<25 && y>=28 && y<36)
      maxBase=std::max(maxBase,pixelDelta);
  }
  const size_t center=3u*(32u*64u+32u);
  unsigned centerDelta=0;
  for (size_t c=0;c<3u;++c)
    centerDelta=std::max(centerDelta,unsigned(std::abs(int(coin.rgb[center+c])-
                                                      int(gl.rgb[center+c]))));
  std::printf("draw_modes_gl mode=%d scenario=%s exact=%zu/4096 max_direct=%u solid_mismatch=%zu border=%zu border_unmatched_coin=%zu border_unmatched_gl=%zu max_foreground=%u max_base=%u max_border=%u green_foreground=%zu/%zu center_delta=%u coin_center=%u,%u,%u gl_center=%u,%u,%u\n",
              int(mode),scenario,exact,maxDirect,solidMismatch,borderPixels,
              borderCoinUnmatched,borderGlUnmatched,maxForeground,maxBase,maxBorder,
              foregroundGreenCoin,foregroundGreenGl,centerDelta,
              unsigned(coin.rgb[center]),unsigned(coin.rgb[center+1]),unsigned(coin.rgb[center+2]),
              unsigned(gl.rgb[center]),unsigned(gl.rgb[center+1]),unsigned(gl.rgb[center+2]));
  // Solid interiors must match in place; only boundary pixels may move by
  // one pixel due to raster/sample placement, with at most four channel levels.
  return solidMismatch==0 && borderCoinUnmatched==0 &&
         borderGlUnmatched==0 && centerDelta<=4;
}

static bool foregroundCenter(const Image & image) {
  if (image.rgb.size()!=64u*64u*3u) return false;
  const size_t at=3u*(32u*64u+32u);
  return image.rgb[at+1]>image.rgb[at]*2u &&
         image.rgb[at+1]>image.rgb[at+2]*2u && image.rgb[at+1]>100;
}

static int exercise(SoRenderManager::RenderMode mode) {
  Scene base=quad(.8f,0,SbColor(1,0,0));
  Scene overlay=quad(.3f,.5f,SbColor(0,1,0));
  SoRenderManager coinManager, glManager;
  coinManager.setSceneGraph(base.root); glManager.setSceneGraph(base.root);
  coinManager.setViewportRegion(SbViewportRegion(64,64));
  glManager.setViewportRegion(SbViewportRegion(64,64));
  coinManager.setBackgroundColor(SbColor4f(0,0,0,1));
  glManager.setBackgroundColor(SbColor4f(0,0,0,1));
  coinManager.setRenderMode(mode); glManager.setRenderMode(mode);
  CoinRenderManagerAdapter adapter(coinManager,SbVec2i32(64,64));
  CoinRenderTarget * coinTarget=adapter.getSceneManager()->getRenderTarget();
  if (!coinTarget || !coinTarget->setDepthReadbackEnabled(TRUE)) {
    overlay.root->unref(); base.root->unref(); return 1;
  }
  ManagerGLAction glAction(glManager,SbViewportRegion(64,64));
  glManager.setGLRenderAction(&glAction);
  SoOffscreenRenderer glRenderer(SbViewportRegion(64,64));
  glRenderer.setGLRenderAction(&glAction);
  glRenderer.setComponents(SoOffscreenRenderer::RGB);
  glRenderer.setBackgroundColor(SbColor(0,0,0));
  const int depthFlags=SoRenderManager::Superimposition::AUTOREDRAW |
                       SoRenderManager::Superimposition::ZBUFFERON;
  Image coin,gl;
  if (!glImage(glManager,glAction,glRenderer,base.root,gl)) {
    overlay.root->unref(); base.root->unref(); return 77;
  }
  bool ok=coinImage(adapter,coin) && compare(coin,gl,mode,"baseline");
  const size_t centerPixel=32u*64u+32u;
  const float baselineDepth=coin.depth.size()==64u*64u ? coin.depth[centerPixel] : 1.0f;
  auto * coinLayer=coinManager.addSuperimposition(overlay.root,depthFlags);
  auto * glLayer=glManager.addSuperimposition(overlay.root,depthFlags);
  const bool frontGL=glImage(glManager,glAction,glRenderer,base.root,gl);
  const bool frontCoin=coinImage(adapter,coin);
  ok=frontGL && frontCoin && compare(coin,gl,mode,"z_front") &&
     foregroundCenter(coin) && foregroundCenter(gl) && ok;
  const float frontDepth=coin.depth.size()==64u*64u ? coin.depth[centerPixel] : 1.0f;
  setZ(overlay,.3f,-.5f);
  ok=glImage(glManager,glAction,glRenderer,base.root,gl) &&
     coinImage(adapter,coin) && compare(coin,gl,mode,"z_rear") && ok;
  const float rearDepth=coin.depth.size()==64u*64u ? coin.depth[centerPixel] : 1.0f;
  coinManager.removeSuperimposition(coinLayer);
  glManager.removeSuperimposition(glLayer);
  coinLayer=coinManager.addSuperimposition(overlay.root,depthFlags |
      SoRenderManager::Superimposition::CLEARZBUFFER);
  glLayer=glManager.addSuperimposition(overlay.root,depthFlags |
      SoRenderManager::Superimposition::CLEARZBUFFER);
  const bool clearGL=glImage(glManager,glAction,glRenderer,base.root,gl);
  const bool clearCoin=coinImage(adapter,coin);
  ok=clearGL && clearCoin && compare(coin,gl,mode,"clear_rear") &&
     foregroundCenter(coin) && foregroundCenter(gl) && ok;
  const float clearDepth=coin.depth.size()==64u*64u ? coin.depth[centerPixel] : 1.0f;
  const bool depthOrdered=frontDepth<baselineDepth &&
      (baselineDepth<1.0f ? rearDepth==baselineDepth : rearDepth<1.0f) &&
      clearDepth>frontDepth && clearDepth<1.0f;
  std::printf("draw_modes_gl mode=%d depth_ordered=%d baseline=%.9g front=%.9g rear=%.9g clear=%.9g\n",
              int(mode),int(depthOrdered),baselineDepth,frontDepth,rearDepth,clearDepth);
  ok &= depthOrdered;
  coinManager.removeSuperimposition(coinLayer);
  glManager.removeSuperimposition(glLayer);
  overlay.root->unref(); base.root->unref();
  return ok?0:1;
}

int main() {
  SoDB::init(); CoinRenderAction::initClass();
  if (!CoinRenderAction::isGpuBackendAvailable()) return 77;
  bool ok=true, reportedFirst=false;
  for (auto mode : {SoRenderManager::WIREFRAME, SoRenderManager::POINTS,
                    SoRenderManager::BOUNDING_BOX, SoRenderManager::WIREFRAME_OVERLAY,
                    SoRenderManager::HIDDEN_LINE, SoRenderManager::SHADED_HIDDEN_LINES}) {
    const int result=exercise(mode);
    if (result==77) return 77;
    if (result!=0 && !reportedFirst) {
      std::fprintf(stderr,"draw_modes_gl first_failure mode=%d (see preceding per-region and depth metrics)\n",int(mode));
      reportedFirst=true;
    }
    ok &= result==0;
  }
  return ok?0:1;
}
