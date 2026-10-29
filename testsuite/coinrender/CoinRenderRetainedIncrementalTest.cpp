#include "config.h"
#include "CoinRenderRetainedTestRenderer.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderOptions.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include "rendering/coinrender/CoinRenderComposition.h"
#include <memory>
#include <vector>
#include <iostream>
#include <limits>
#include <Inventor/lists/SoAuditorList.h>
#include <Inventor/misc/SoNotification.h>

namespace {
int failures = 0;
void check(bool value, const char * message) {
  if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}
int main() {
  SoDB::init();
  CoinRenderOptions options;
#if defined(HAVE_COIN_BGFX) || defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  options.renderer = COIN_RENDER_RENDERER_VULKAN;
#endif
  if (!coinRetainedTestRenderer(options)) return 1;
  const SbViewportRegion viewport(64, 64);
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64,64), options));
  std::unique_ptr<CoinRenderTarget> control(CoinRenderTarget::createOffscreen(SbVec2i32(64,64), options));
  CoinRenderRetainedRenderer retained(viewport), rebuilt(viewport);
  auto * root = new SoSeparator; root->ref();
  auto * camera = new SoOrthographicCamera;
  camera->position.setValue(0,0,5); camera->height = 4; camera->nearDistance = 1; camera->farDistance = 10;
  root->addChild(camera);
  auto * light = new SoLightModel; light->model = SoLightModel::BASE_COLOR; root->addChild(light);
  root->addChild(new SoDepthBuffer);
  SoTranslation * moving = nullptr;
  SoMaterial * material = nullptr;
  SoCoordinate3 * coordinates = nullptr;
  SoFaceSet * topology = nullptr;
  for (int i = 0; i < 2; ++i) {
    auto * part = new SoSeparator;
    auto * translation = new SoTranslation; translation->translation.setValue(i ? 0.7f : -0.7f,0,0);
    auto * color = new SoMaterial; color->diffuseColor.setValue(1, 0, 0);
    auto * points = new SoCoordinate3;
    const SbVec3f triangle[] = {SbVec3f(-0.4f,-0.4f,0),SbVec3f(0.4f,-0.4f,0),SbVec3f(0,0.4f,0)};
    points->point.setValues(0,3,triangle);
    auto * faces = new SoFaceSet; faces->numVertices.setValue(3);
    part->addChild(translation); part->addChild(color); part->addChild(points); part->addChild(faces); root->addChild(part);
    if (!i) { moving = translation; material = color; coordinates = points; topology = faces; }
  }
  const auto compare = [&]() {
    const auto status = retained.render(root, *target);
    if (status != CoinRenderAction::SUCCESS) std::cerr << retained.getLastError().getString() << '\n';
    check(status == CoinRenderAction::SUCCESS, "retained frame executes");
    rebuilt.invalidate();
    check(rebuilt.render(root, *control) == CoinRenderAction::SUCCESS, "forced rebuild executes");
    std::vector<uint8_t> a,b; target->readbackRGBA(a); control->readbackRGBA(b);
    std::vector<float> da,db; target->readbackDepth(da); control->readbackDepth(db);
    check(a == b && da == db && !a.empty(), "retained pixels/depth equal forced rebuild exactly");
  };
  const auto before = coin_render_composition_classification_count_for_tests();
  compare();
  auto first = retained.getStatistics();
  check(first.captures == 1 && first.planBuilds == 1 && first.transportBuilds == 1, "first frame builds once");
  check(first.materialSlots == 1, "equal materials share one transport slot");
  check(retained.getDrawList().getNumGeometryResources() == 2, "native FaceSet resources are retained");
  compare();
  auto unchanged = retained.getStatistics();
  check(unchanged.captures == 1 && unchanged.planBuilds == 1 && unchanged.transportReuses == 1 &&
        unchanged.copiedVertices == first.copiedVertices, "unchanged frame copies no geometry and captures/plans nothing");
  moving->translation.setValue(-0.5f,0.1f,0); compare();
  auto moved = retained.getStatistics();
  check(moved.captures == 1 && moved.planBuilds == 1 && moved.incrementalUpdates == 1 &&
        moved.transportPatchedCommands == 1 && moved.copiedVertices == first.copiedVertices,
        "native translation patch updates one transport command without geometry copy or planning");
  material->diffuseColor.setValue(0.2f,0.3f,1); compare();
  auto colored = retained.getStatistics();
  check(colored.captures == 1 && colored.planBuilds == 1 && colored.incrementalUpdates == 2 &&
        colored.transportPatchedCommands == 2, "native diffuse patch is incremental");
  check(colored.materialSlots == 2, "editing one shared material uses copy-on-write without changing its neighbor");
  coordinates->point.set1Value(0,SbVec3f(-0.5f,-0.3f,0)); compare();
  auto geometry = retained.getStatistics();
  check(geometry.captures == 1 && geometry.planBuilds == 2 && geometry.incrementalUpdates == 3 &&
        geometry.transportBuilds == 1 && geometry.transportGeometryPatches == 1 &&
        geometry.geometryPatchedVertices == 1 && geometry.copiedIndices == first.copiedIndices,
        "geometry patches existing transport positions with no rebuild or unchanged index copy");
  for (int edit = 0; edit < 12; ++edit) {
    material->diffuseColor.setValue(edit % 2 ? 0.2f : 0.8f,0.3f,0.6f); compare();
    coordinates->point.set1Value(0,SbVec3f(edit % 2 ? -0.45f : -0.55f,-0.3f,0)); compare();
  }
  auto repeated = retained.getStatistics();
  check(repeated.transportBuilds == 1 && repeated.materialSlots == 2 &&
        repeated.transportGeometryPatches == 13 && repeated.copiedIndices == first.copiedIndices,
        "repeated geometry/material edits keep transport and bound slot growth");
  const auto plans = retained.getPlanBuildCount();
  retained.setClearColor(SbColor4f(0.1f,0.2f,0.3f,1)); rebuilt.setClearColor(SbColor4f(0.1f,0.2f,0.3f,1)); compare();
  check(retained.getPlanBuildCount() == plans && retained.getCaptureCount() == 1, "clear color does not recapture or plan");
  camera->height = 4.2f; compare();
  check(retained.getCaptureCount() == 1 && retained.getStatistics().transportBuilds == geometry.transportBuilds, "projection mutation reuses capture and transport");
  camera->position.setValue(.1f,0,5); compare();
  check(retained.getCaptureCount() == 2, "camera pose mutation conservatively recaptures");
  auto * empty = new SoSeparator; root->addChild(empty); compare();
  check(retained.getCaptureCount() == 3, "structure mutation conservatively recaptures");
  std::vector<uint8_t> previous; target->readbackRGBA(previous);
  const auto serial = target->getLastSubmissionSerial();
  auto * unsupportedState = new SoTextureUnit; root->insertChild(unsupportedState,3);
  check(retained.render(root,*target) == CoinRenderAction::UNSUPPORTED, "unsupported inserted state is not lost through retention");
  std::vector<uint8_t> after; target->readbackRGBA(after);
  check(previous == after && target->getLastSubmissionSerial() == serial, "failed rebuild preserves publication");
  root->removeChild(unsupportedState); compare();
  check(coin_render_composition_classification_count_for_tests() == before, "retention never invokes another composition classifier");
  // Normal apply(root) retains the root in command paths. Inherited state at
  // root scope must patch every affected occurrence without dropping that prefix.
  auto * inherited = new SoTranslation; root->insertChild(inherited,3); compare();
  auto baseline = retained.getStatistics();
  inherited->translation.setValue(0.1f,0.15f,0); compare();
  auto inheritedUpdate = retained.getStatistics();
  check(inheritedUpdate.captures == baseline.captures &&
        inheritedUpdate.planBuilds == baseline.planBuilds &&
        inheritedUpdate.transportPatchedCommands == baseline.transportPatchedCommands + 2,
        "root-scope inherited transform patches both commands");
  moving->translation.setValue(-0.6f,0,0); material->diffuseColor.setValue(0.4f,0.5f,0.6f); compare();
  check(retained.getCaptureCount() == inheritedUpdate.captures + 1,
        "mixed notification kinds conservatively rebuild");
  empty->addChild(moving); compare();
  baseline = retained.getStatistics();
  // getAuditors must return a fresh, complete snapshot on every request.
  for (int repeat = 0; repeat < 3; ++repeat) {
    const auto & auditors = moving->getAuditors();
    int parents = 0;
    for (int i = 0; i < auditors.getLength(); ++i)
      if (auditors.getType(i) == SoNotRec::PARENT) ++parents;
    check(parents == 2, "auditor snapshot preserves both parents across repeated queries");
  }
  moving->translation.setValue(-0.45f,0,0); compare();
  check(retained.getCaptureCount() == baseline.captures + 1,
        "ambiguous shared state falls back to one capture");
  target->readbackRGBA(previous);
  const auto validSerial = target->getLastSubmissionSerial();
  coordinates->point.set1Value(0,SbVec3f(std::numeric_limits<float>::quiet_NaN(),0,0));
  check(retained.render(root,*target) == CoinRenderAction::UNSUPPORTED, "non-finite geometry patch is rejected");
  target->readbackRGBA(after);
  check(after == previous && target->getLastSubmissionSerial() == validSerial,
        "invalid geometry patch preserves publication");
  coordinates->point.set1Value(0,SbVec3f(-0.5f,-0.3f,0)); compare();
  target->readbackRGBA(previous);
  const auto materialSerial = target->getLastSubmissionSerial();
  material->diffuseColor.setValue(std::numeric_limits<float>::quiet_NaN(),0,0);
  check(retained.render(root,*target) == CoinRenderAction::UNSUPPORTED,
        "invalid incremental material is rejected");
  target->readbackRGBA(after);
  check(after == previous && target->getLastSubmissionSerial() == materialSerial,
        "invalid incremental patch preserves the last published image");
  material->diffuseColor.setValue(0.2f,0.4f,0.6f); compare();
  check(coin_render_composition_classification_count_for_tests() == before,
        "fallback and recovery still use the authoritative native plan");
  baseline = retained.getStatistics();
  coordinates->point.set1Value(3,SbVec3f(-0.4f,0.4f,0)); topology->numVertices.setValue(4); compare();
  auto resized = retained.getStatistics();
  check(resized.captures == baseline.captures + 1 && resized.transportBuilds == baseline.transportBuilds + 1,
        "topology changes use one fresh capture/transport instead of patching stale spans");
  root->unref();
  return failures ? 1 : 0;
}
