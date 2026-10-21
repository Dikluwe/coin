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
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#include <cstdio>
#include <memory>
#include <vector>

struct Scene {
  SoSeparator * root;
  SoCoordinate3 * coordinates;
  SoMaterial * material;
  SoAlphaTest * alpha;
};
struct Frame {
  std::vector<uint8_t> color;
  std::vector<float> depth;
  uint64_t serial = 0;
};

static Scene quad(float extent, float z, const SbColor & color) {
  auto * root = new SoSeparator; root->ref();
  auto * camera = new SoOrthographicCamera;
  camera->position.setValue(0, 0, 4); camera->height = 4;
  root->addChild(camera);
  auto * model = new SoLightModel; model->model = SoLightModel::BASE_COLOR;
  root->addChild(model);
  auto * material = new SoMaterial;
  material->diffuseColor.setValue(color);
  root->addChild(material);
  auto * alpha = new SoAlphaTest; alpha->function = SoAlphaTest::NONE;
  root->addChild(alpha);
  auto * coordinates = new SoCoordinate3;
  const SbVec3f corners[] = {
    SbVec3f(-extent, -extent, z), SbVec3f(extent, -extent, z),
    SbVec3f(extent, extent, z), SbVec3f(-extent, extent, z)
  };
  coordinates->point.setValues(0, 4, corners);
  root->addChild(coordinates);
  auto * face = new SoFaceSet; face->numVertices.set1Value(0, 4);
  root->addChild(face);
  return {root, coordinates, material, alpha};
}

static void setZ(Scene & scene, float extent, float z) {
  const SbVec3f corners[] = {
    SbVec3f(-extent, -extent, z), SbVec3f(extent, -extent, z),
    SbVec3f(extent, extent, z), SbVec3f(-extent, extent, z)
  };
  scene.coordinates->point.setValues(0, 4, corners);
}

static bool capture(CoinRenderManagerAdapter & adapter, Frame & frame) {
  const auto status = adapter.render();
  CoinRenderTarget * target = adapter.getSceneManager()->getRenderTarget();
  target->readbackRGBA(frame.color);
  target->readbackDepth(frame.depth);
  frame.serial = target->getLastSubmissionSerial();
  const bool good = status == CoinRenderAction::SUCCESS &&
                    frame.color.size() == 64u * 64u * 4u &&
                    frame.depth.size() == 64u * 64u && frame.serial > 0;
  if (!good)
    std::fprintf(stderr, "technical layers capture status=%d color=%zu depth=%zu serial=%llu error=%s\n",
                 int(status), frame.color.size(), frame.depth.size(),
                 static_cast<unsigned long long>(frame.serial),
                 adapter.getLastError().getString());
  return good;
}

static size_t differingPixels(const Frame & a, const Frame & b) {
  if (a.color.size() != b.color.size()) return 0;
  size_t changed = 0;
  for (size_t i = 0; i < a.color.size(); i += 4)
    changed += a.color[i] != b.color[i] || a.color[i+1] != b.color[i+1] ||
               a.color[i+2] != b.color[i+2];
  return changed;
}

static bool exercise(SoRenderManager::RenderMode mode) {
  Scene base = quad(.8f, 0, SbColor(1, 0, 0));
  Scene overlay = quad(.4f, -.5f, SbColor(0, 1, 0));
  SoRenderManager manager;
  manager.setSceneGraph(base.root);
  manager.setViewportRegion(SbViewportRegion(64, 64));
  manager.setBackgroundColor(SbColor4f(0, 0, 0, 1));
  manager.setRenderMode(mode);
  CoinRenderManagerAdapter adapter(manager, SbVec2i32(64, 64));
  CoinRenderTarget * target = adapter.getSceneManager()->getRenderTarget();
  if (!target || !target->setDepthReadbackEnabled(TRUE)) return false;
  Frame baseline, behind, ahead, cleared, restored;
  bool ok = capture(adapter, baseline);
  const int depthFlags = SoRenderManager::Superimposition::AUTOREDRAW |
                         SoRenderManager::Superimposition::ZBUFFERON;
  auto * layer = manager.addSuperimposition(overlay.root, depthFlags);
  ok = capture(adapter, behind) && ok;
  const bool rearOccluded = behind.color == baseline.color &&
                            behind.depth == baseline.depth;
  setZ(overlay, .4f, .5f);
  ok = capture(adapter, ahead) && ok;
  if (!ok) {
    manager.removeSuperimposition(layer);
    overlay.root->unref(); base.root->unref();
    return false;
  }
  const size_t visible = differingPixels(baseline, ahead);
  const size_t center = 32u * 64u + 32u;
  const bool frontVisible = visible > 0 &&
      ahead.depth.size() == 64u * 64u && baseline.depth.size() == 64u * 64u &&
      ahead.depth[center] < baseline.depth[center];
  std::printf("technical_layers mode=%d rear_occluded=%d front_visible=%d changed=%zu depth_center=%.9g->%.9g\n",
              int(mode), int(rearOccluded), int(frontVisible), visible,
              baseline.depth[center], ahead.depth[center]);
  ok &= rearOccluded && frontVisible;

  const uint64_t stableSerial = ahead.serial;
  overlay.alpha->function = SoAlphaTest::GREATER; overlay.alpha->value = .5f;
  const auto rejectedStatus = adapter.render();
  Frame rejected;
  target->readbackRGBA(rejected.color);
  target->readbackDepth(rejected.depth);
  rejected.serial = target->getLastSubmissionSerial();
  const bool rejectedAtomically = rejectedStatus == CoinRenderAction::UNSUPPORTED &&
      rejected.color == ahead.color && rejected.depth == ahead.depth &&
      rejected.serial == stableSerial;
  overlay.alpha->function = SoAlphaTest::NONE;
  ok = capture(adapter, restored) && ok;
  const bool retry = restored.color == ahead.color &&
                     restored.depth == ahead.depth && restored.serial > stableSerial;
  std::printf("technical_layers mode=%d alpha_test_rejected=%d retry=%d status=%d\n",
              int(mode), int(rejectedAtomically), int(retry), int(rejectedStatus));
  ok &= rejectedAtomically && retry;

  setZ(overlay, .4f, -.5f);
  manager.removeSuperimposition(layer);
  layer = manager.addSuperimposition(overlay.root, depthFlags |
      SoRenderManager::Superimposition::CLEARZBUFFER);
  ok = capture(adapter, cleared) && ok;
  if (!ok) {
    manager.removeSuperimposition(layer);
    overlay.root->unref(); base.root->unref();
    return false;
  }
  const size_t clearChanged = differingPixels(baseline, cleared);
  const bool clearReleasesDepth = clearChanged > 0 &&
      cleared.depth.size() == 64u * 64u &&
      cleared.depth[center] > baseline.depth[center] &&
      cleared.color == ahead.color;
  std::printf("technical_layers mode=%d clear_releases_depth=%d changed=%zu cleared_depth=%.9g\n",
              int(mode), int(clearReleasesDepth), clearChanged, cleared.depth[center]);
  ok &= clearReleasesDepth;
  layer->setEnabled(FALSE);
  Frame disabled;
  ok = capture(adapter, disabled) && ok;
  const bool disabledRestores = disabled.color == baseline.color &&
                                disabled.depth == baseline.depth;
  ok &= disabledRestores;
  manager.removeSuperimposition(layer);
  overlay.root->unref(); base.root->unref();
  return ok;
}

int main() {
  SoDB::init(); CoinRenderAction::initClass();
  if (!CoinRenderAction::isGpuBackendAvailable()) return 77;
  const bool hidden = exercise(SoRenderManager::HIDDEN_LINE);
  const bool shaded = exercise(SoRenderManager::SHADED_HIDDEN_LINES);
  return hidden && shaded ? 0 : 1;
}
