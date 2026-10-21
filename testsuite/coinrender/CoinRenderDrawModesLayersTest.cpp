#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <cstdio>
#include <vector>

struct Scene { SoSeparator * root; SoCoordinate3 * points; SoMaterial * material; SoAlphaTest * alpha; };
struct Frame { std::vector<uint8_t> color; std::vector<float> depth; uint64_t serial=0; };

static Scene quad(float extent, float z, const SbColor & color) {
  auto * root = new SoSeparator; root->ref();
  auto * camera = new SoOrthographicCamera;
  camera->position.setValue(0,0,4); camera->height=4;
  root->addChild(camera);
  auto * model = new SoLightModel; model->model=SoLightModel::BASE_COLOR;
  root->addChild(model);
  auto * material = new SoMaterial; material->diffuseColor.setValue(color);
  root->addChild(material);
  auto * alpha = new SoAlphaTest; alpha->function = SoAlphaTest::NONE;
  root->addChild(alpha);
  auto * points = new SoCoordinate3;
  const SbVec3f corners[] = {
    SbVec3f(-extent,-extent,z), SbVec3f(extent,-extent,z),
    SbVec3f(extent,extent,z), SbVec3f(-extent,extent,z)
  };
  points->point.setValues(0,4,corners); root->addChild(points);
  auto * face = new SoFaceSet; face->numVertices.set1Value(0,4);
  root->addChild(face);
  return {root,points,material,alpha};
}

static void setZ(Scene & scene, float extent, float z) {
  const SbVec3f corners[] = {
    SbVec3f(-extent,-extent,z), SbVec3f(extent,-extent,z),
    SbVec3f(extent,extent,z), SbVec3f(-extent,extent,z)
  };
  scene.points->point.setValues(0,4,corners);
}

static bool read(CoinRenderManagerAdapter & adapter, Frame & frame) {
  const auto status=adapter.render();
  CoinRenderTarget * target=adapter.getSceneManager()->getRenderTarget();
  target->readbackRGBA(frame.color);
  target->readbackDepth(frame.depth);
  frame.serial=target->getLastSubmissionSerial();
  const bool good=status==CoinRenderAction::SUCCESS &&
      frame.color.size()==64u*64u*4u && frame.depth.size()==64u*64u && frame.serial>0;
  if (!good)
    std::fprintf(stderr,"draw_mode_layers status=%d color=%zu depth=%zu error=%s\n",
                 int(status),frame.color.size(),frame.depth.size(),
                 adapter.getLastError().getString());
  return good;
}

static bool greenCenter(const Frame & frame) {
  if (frame.color.size()!=64u*64u*4u) return false;
  const size_t at=(32u*64u+32u)*4u;
  return frame.color[at+1u]>frame.color[at]*2u &&
         frame.color[at+1u]>frame.color[at+2u]*2u && frame.color[at+1u]>100;
}

static bool outsideUnchanged(const Frame & a, const Frame & b) {
  if (a.color.size()!=b.color.size() || a.depth.size()!=b.depth.size()) return false;
  for (size_t y=0;y<64u;++y) for (size_t x=0;x<64u;++x) {
    if (x>=24 && x<40 && y>=24 && y<40) continue;
    const size_t pixel=y*64u+x;
    for (size_t c=0;c<4u;++c)
      if (a.color[4u*pixel+c]!=b.color[4u*pixel+c]) return false;
    if (a.depth[pixel]!=b.depth[pixel]) return false;
  }
  return true;
}

static bool exercise(SoRenderManager::RenderMode mode) {
  Scene base=quad(.8f,0,SbColor(1,0,0));
  Scene overlay=quad(.3f,.5f,SbColor(0,1,0));
  SoRenderManager manager;
  manager.setSceneGraph(base.root);
  manager.setViewportRegion(SbViewportRegion(64,64));
  manager.setBackgroundColor(SbColor4f(0,0,0,1));
  manager.setRenderMode(mode);
  CoinRenderManagerAdapter adapter(manager,SbVec2i32(64,64));
  CoinRenderTarget * target=adapter.getSceneManager()->getRenderTarget();
  if (!target || !target->setDepthReadbackEnabled(TRUE)) return false;
  Frame baseline,front,rear,cleared,retry;
  bool ok=read(adapter,baseline);
  const int depthFlags=SoRenderManager::Superimposition::AUTOREDRAW |
                       SoRenderManager::Superimposition::ZBUFFERON;
  auto * layer=manager.addSuperimposition(overlay.root,depthFlags);
  ok=read(adapter,front) && ok;
  const size_t center=32u*64u+32u;
  const bool frontFilled=greenCenter(front) &&
      front.depth[center]<baseline.depth[center] && outsideUnchanged(baseline,front);
  setZ(overlay,.3f,-.5f);
  ok=read(adapter,rear) && ok;
  const bool rearOrdered=baseline.depth[center]<1.0f ?
      (!greenCenter(rear) && rear.depth[center]==baseline.depth[center]) :
      (greenCenter(rear) && rear.depth[center]<1.0f);
  manager.removeSuperimposition(layer);
  layer=manager.addSuperimposition(overlay.root,depthFlags |
      SoRenderManager::Superimposition::CLEARZBUFFER);
  ok=read(adapter,cleared) && ok;
  const bool clearFilled=greenCenter(cleared) &&
      cleared.depth[center]>front.depth[center];
  std::printf("draw_mode_layers mode=%d front_filled=%d rear_ordered=%d clear_filled=%d center_depth=%.9g/%.9g/%.9g/%.9g\n",
              int(mode),int(frontFilled),int(rearOrdered),int(clearFilled),
              baseline.depth[center],front.depth[center],rear.depth[center],cleared.depth[center]);
  ok &= frontFilled && rearOrdered && clearFilled;

  const uint64_t serial=cleared.serial;
  overlay.alpha->function=SoAlphaTest::GREATER; overlay.alpha->value=.5f;
  const auto status=adapter.render();
  Frame rejected;
  target->readbackRGBA(rejected.color);
  target->readbackDepth(rejected.depth);
  rejected.serial=target->getLastSubmissionSerial();
  const bool atomic=status==CoinRenderAction::UNSUPPORTED &&
      rejected.color==cleared.color && rejected.depth==cleared.depth && rejected.serial==serial;
  overlay.alpha->function=SoAlphaTest::NONE;
  ok=read(adapter,retry) && ok;
  const bool recovered=retry.color==cleared.color && retry.depth==cleared.depth &&
                       retry.serial>serial;
  std::printf("draw_mode_layers mode=%d alpha_test_atomic=%d recovered=%d\n",
              int(mode),int(atomic),int(recovered));
  ok &= atomic && recovered;
  manager.removeSuperimposition(layer);
  overlay.root->unref(); base.root->unref();
  return ok;
}

int main() {
  SoDB::init(); CoinRenderAction::initClass();
  if (!CoinRenderAction::isGpuBackendAvailable()) return 77;
  bool ok=true;
  for (auto mode : {SoRenderManager::WIREFRAME, SoRenderManager::POINTS,
                    SoRenderManager::BOUNDING_BOX, SoRenderManager::WIREFRAME_OVERLAY})
    ok=exercise(mode) && ok;
  return ok?0:1;
}
