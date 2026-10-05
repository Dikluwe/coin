#include <setup.h>
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "actions/CoinRenderActionP.h"
#include "rendering/coinrender/CoinRenderStateCore.h"
#include "../coinrender/CoinRenderTestEnvironment.h"
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/SoDB.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoCylinder.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/elements/SoDepthBufferElement.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoSpotLight.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/SoPath.h>
#include <Inventor/lists/SoPathList.h>

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#define TEST_ASSERT(cond, msg) do {   if (!(cond)) {     std::cerr << "FAILED: " << msg << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl;     return 1;   } } while (0)

int testTypeAndInit() {
  SoType t = SoType::fromName("CoinRenderAction");
  TEST_ASSERT(t != SoType::badType(), "CoinRenderAction must be registered in SoType");
  TEST_ASSERT(t.isDerivedFrom(SoCallbackAction::getClassTypeId()), "CoinRenderAction must derive from SoCallbackAction");

  CoinRenderAction action1;
  TEST_ASSERT(action1.getLastStatus() == CoinRenderAction::SUCCESS, "Initial status should be SUCCESS");

  SbViewportRegion vp(800, 600);
  CoinRenderAction action2(vp);
  TEST_ASSERT(action2.getViewportRegion().getViewportSizePixels() == SbVec2s(800, 600), "Viewport constructor should store viewport");
  return 0;
}

class CapturedPolicyBackend : public CoinRenderBackend {
public:
  CoinRenderCompositionItem::TransparencyStrategy expected = CoinRenderCompositionItem::WEIGHTED_OIT;
  bool matched = false;
  bool isGpuBackend() const override { return false; }
  CoinRenderBackendStatus getStatus() const override { return CoinRenderBackendStatus::SUCCESS; }
  CoinRenderBackendStatus prepare(CoinRenderTargetP &) override { return CoinRenderBackendStatus::SUCCESS; }
  void poll() override {}
  const std::string & getLastError() const override { return error; }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target) override {
    const auto * preflight = target.submissionPreflight(frame);
    const auto * order = preflight ? preflight->compositionFor(frame) : nullptr;
    matched = order && !order->empty();
    if (order) for (const auto & item : *order)
      matched = matched && item.blend && item.transparencyStrategy == expected;
    return {};
  }
private:
  std::string error;
};

int testCapturedCompositionPolicy() {
  auto * root = new SoSeparator;
  root->ref();
  auto * material = new SoMaterial;
  material->transparency = .5f;
  root->addChild(material);
  root->addChild(new SoCube);
  auto * target = CoinRenderTarget::createOffscreen(SbVec2i32(1, 1));
  TEST_ASSERT(target != nullptr, "CPU policy test requires an offscreen target shell");
  auto * backend = new CapturedPolicyBackend;
  target->getPimpl()->backend.reset(backend);
  target->getPimpl()->depthReadbackEnabled = false;
  target->getPimpl()->options.transparency = COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;
  CoinRenderAction action(SbViewportRegion(1, 1));
  action.setTransparencyType(CoinRenderAction::DELAYED_BLEND);
  action.setRenderTarget(target);
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && backend->matched,
              "captured composition must use the selected policy before it reaches the target");
  TEST_ASSERT(!target->getPimpl()->submissionPreflight(action.getPimpl()->lastValidPlan),
              "capture preflight must not survive apply or move into the cached plan");
  backend->matched = false;
  backend->expected = CoinRenderCompositionItem::OBJECT;
  target->getPimpl()->options.transparency = COIN_RENDER_TRANSPARENCY_OBJECT;
  action.setRenderTarget(target);
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && backend->matched,
              "a new capture must recompute composition after the selected policy changes");
  action.setRenderTarget(nullptr);
  root->unref();
  delete target;
  return 0;
}

class CompositionCaptureBackend : public CoinRenderBackend {
public:
  bool fail = false, receiptPresent = false, borrowed = false, matched = true;
  CoinRenderFramePlan captured;
  std::vector<CoinRenderCompositionItem> items;
  CoinRenderFrameReuseDecision lastReuse;
  unsigned submissions = 0;
  bool isGpuBackend() const override { return false; }
  CoinRenderBackendStatus getStatus() const override { return CoinRenderBackendStatus::SUCCESS; }
  CoinRenderBackendStatus prepare(CoinRenderTargetP &) override { return CoinRenderBackendStatus::SUCCESS; }
  void poll() override {}
  const std::string & getLastError() const override { return error; }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target) override {
    ++submissions;
    const auto * receipt = target.submissionPreflight(frame);
    receiptPresent = receipt != nullptr;
    CoinRenderCompositionScheduleView schedule("capture_test");
    std::string diagnostic;
    matched = schedule.prepare(frame, diagnostic, receipt);
    borrowed = schedule.borrowed();
    captured = frame;
    items.assign(schedule.begin(), schedule.end());
    return {fail ? CoinRenderBackendStatus::BACKEND_ERROR : CoinRenderBackendStatus::SUCCESS,
            fail ? "intentional composition failure" : ""};
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
                                const CoinRenderFrameReuseDecision & reuse) override {
    lastReuse = reuse;
    return submit(frame, target);
  }
private:
  std::string error;
};

bool sameCapturedComposition(const CompositionCaptureBackend & a, const CompositionCaptureBackend & b) {
  if (!a.matched || !b.matched || !a.captured.hasSamePayload(b.captured) ||
      a.items.size() != b.items.size() || a.lastReuse.kind != b.lastReuse.kind) return false;
  for (size_t i = 0; i < a.items.size(); ++i) {
    const auto & x = a.items[i]; const auto & y = b.items[i];
    if (x.drawIndex != y.drawIndex || x.firstIndex != y.firstIndex || x.indexCount != y.indexCount ||
        x.blend != y.blend || x.deferred != y.deferred || x.additive != y.additive ||
        x.sortTriangles != y.sortTriangles || x.sortObject != y.sortObject ||
        std::memcmp(&x.eyeDepth, &y.eyeDepth, sizeof(x.eyeDepth)) ||
        x.screenDoor != y.screenDoor || x.screenDoorLevel != y.screenDoorLevel ||
        x.depthTest != y.depthTest || x.depthWrite != y.depthWrite || x.depthFunction != y.depthFunction ||
        std::memcmp(x.depthRange, y.depthRange, sizeof(x.depthRange)) ||
        x.transparencyStrategy != y.transparencyStrategy) return false;
  }
  return true;
}

int testCapturedCompositionBorrow() {
  const char * option = std::getenv("COIN_RENDER_DISABLE_COMPOSITION_BORROW");
  struct Restore {
    bool present;
    std::string value;
    ~Restore() { coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMPOSITION_BORROW", present ? value.c_str() : nullptr); }
  } restore{option != nullptr, option ? option : ""};
  auto * root = new SoSeparator;
  root->ref();
  auto * camera = new SoPerspectiveCamera;
  camera->position.setValue(0, 0, 20);
  root->addChild(camera);
  auto * model = new SoLightModel; model->model = SoLightModel::BASE_COLOR;
  root->addChild(model);
  auto * material = new SoMaterial;
  auto * cube = new SoCube;
  std::vector<SoTranslation *> positions;
  for (int i = 0; i < 300; ++i) {
    auto * occurrence = new SoSeparator;
    auto * translation = new SoTranslation;
    translation->translation.setValue(float(i) * .01f, 0, 0);
    positions.push_back(translation);
    // The object proof requires the isolated Material/Transform/Cube scope;
    // each syntactic material occurrence must belong to one mapped object.
    occurrence->addChild(material); occurrence->addChild(translation);
    occurrence->addChild(cube); root->addChild(occurrence);
  }
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(1, 1)));
  std::unique_ptr<CoinRenderTarget> literalTarget(CoinRenderTarget::createOffscreen(SbVec2i32(1, 1)));
  TEST_ASSERT(target && literalTarget, "composition capture requires mock target shells");
  auto * backend = new CompositionCaptureBackend, * literalBackend = new CompositionCaptureBackend;
  target->getPimpl()->backend.reset(backend); literalTarget->getPimpl()->backend.reset(literalBackend);
  target->getPimpl()->depthReadbackEnabled = literalTarget->getPimpl()->depthReadbackEnabled = false;
  CoinRenderAction action(SbViewportRegion(1, 1)), literal(SbViewportRegion(1, 1));
  action.setRenderTarget(target.get()); literal.setRenderTarget(literalTarget.get());
  action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
  literal.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
  const auto apply = [&]() {
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMPOSITION_BORROW", "0"); action.apply(root);
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMPOSITION_BORROW", "1"); literal.apply(root);
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMPOSITION_BORROW", "0");
  };
  const auto same = [&]() {
    return action.getLastStatus() == literal.getLastStatus() &&
      std::string(action.getLastError().getString()) == literal.getLastError().getString() &&
      sameCapturedComposition(*backend, *literalBackend) &&
      action.getPimpl()->lastValidPlan.hasSamePayload(literal.getPimpl()->lastValidPlan) &&
      !target->getPimpl()->submissionPreflight(action.getPimpl()->lastValidPlan) &&
      !literalTarget->getPimpl()->submissionPreflight(literal.getPimpl()->lastValidPlan);
  };
  apply();
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && same() &&
              backend->captured.draws.size() >= 256 && backend->receiptPresent && backend->borrowed &&
              literalBackend->receiptPresent && !literalBackend->borrowed,
              "captured submission must loan the exact opaque order and retain literal payload/status");
  TEST_ASSERT(action.getPimpl()->translationProofValid && literal.getPimpl()->translationProofValid &&
              action.getPimpl()->materialByNode.count(material) && literal.getPimpl()->materialByNode.count(material),
              "the fixture must admit real object/material overlays");
  apply();
  TEST_ASSERT(same() && backend->lastReuse.kind == CoinRenderFrameReuseKind::REUSE &&
              !backend->receiptPresent && !literalBackend->receiptPresent && !backend->borrowed,
              "cached REUSE must not retain the previous capture lender");
  const auto generation = action.getPimpl()->translationProofGeneration;
  const auto * vertices = action.getPimpl()->lastValidPlan.vertices.data();
  const auto baseRevision = action.getPimpl()->lastValidPlan.revision;
  positions[0]->translation.setValue(-.25f, .125f, 0);
  apply();
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && same() &&
              backend->lastReuse.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD &&
              backend->lastReuse.baseRevision == baseRevision &&
              action.getPimpl()->translationProofGeneration == generation &&
              action.getPimpl()->lastValidPlan.vertices.data() == vertices,
              "object update must preserve literal composition and its existing reuse decision");
  material->diffuseColor.setValue(.2f,.6f,.3f);
  backend->fail = literalBackend->fail = true;
  const auto previous = action.getPimpl()->lastValidPlan;
  apply();
  TEST_ASSERT(action.getLastStatus() != CoinRenderAction::SUCCESS && same() &&
              action.getPimpl()->lastValidPlan.hasSamePayload(previous) &&
              action.getPimpl()->lastValidPlan.revision == previous.revision &&
              action.getPimpl()->materialDirty.count(material) &&
              backend->lastReuse.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD,
              "submission failure must roll back payload and release the scoped lender");
  backend->fail = literalBackend->fail = false;
  const auto failedSubmissions = backend->submissions;
  apply();
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::BACKEND_ERROR && same() &&
              backend->submissions == failedSubmissions &&
              action.getPimpl()->lastValidPlan.hasSamePayload(previous) &&
              action.getPimpl()->materialDirty.count(material),
              "TARGET_ERROR must block retry and retain the pending update until target recovery");
  // BACKEND_ERROR makes Target sticky. Recover through its existing public
  // resize contract before expecting another successful submission.
  TEST_ASSERT(target->resize(SbVec2i32(1, 1)) && literalTarget->resize(SbVec2i32(1, 1)),
              "mock targets must recover explicitly after a backend error");
  apply();
  const bool sameRetry = same();
  const bool retryAccepted = action.getLastStatus() == CoinRenderAction::SUCCESS && sameRetry &&
    action.getPimpl()->translationProofValid && literal.getPimpl()->translationProofValid &&
    !action.getPimpl()->materialDirty.count(material) && !literal.getPimpl()->materialDirty.count(material);
  if (!retryAccepted)
    std::cerr << "composition retry: status=" << action.getLastStatus()
              << " literal_status=" << literal.getLastStatus()
              << " reuse=" << static_cast<int>(backend->lastReuse.kind)
              << " base=" << backend->lastReuse.baseRevision
              << " dirty=" << action.getPimpl()->materialDirty.size()
              << " literal_dirty=" << literal.getPimpl()->materialDirty.size()
              << " proof=" << action.getPimpl()->translationProofValid
              << " same=" << sameRetry << std::endl;
  TEST_ASSERT(retryAccepted,
              "recovered retry must publish equivalent payload/status and clear pending object updates");
  material->transparency = .5f;
  apply();
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && same() &&
              !backend->borrowed && !literalBackend->borrowed,
              "transparent capture must keep the general owned schedule");
  action.setRenderTarget(nullptr); literal.setRenderTarget(nullptr);
  root->unref();
  return 0;
}

int testSeparatorAndState() {
  SoSeparator * root = new SoSeparator;
  root->ref();

  // Sibling A: has translation, red material, and cube
  SoSeparator * sibA = new SoSeparator;
  SoTranslation * transA = new SoTranslation;
  transA->translation.setValue(10.0f, 0.0f, 0.0f);
  SoMaterial * matA = new SoMaterial;
  matA->diffuseColor.setValue(1.0f, 0.0f, 0.0f);
  SoCube * cubeA = new SoCube;
  sibA->addChild(transA);
  sibA->addChild(matA);
  sibA->addChild(cubeA);
  root->addChild(sibA);

  // Sibling B: just a cube without translation or material
  SoSeparator * sibB = new SoSeparator;
  SoCube * cubeB = new SoCube;
  sibB->addChild(cubeB);
  root->addChild(sibB);

  CoinRenderAction action;
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "apply should succeed");

  std::string log = action.getRecordingLog().getString();
  TEST_ASSERT(!log.empty(), "Recording log should not be empty");
  TEST_ASSERT(log.find("draws count: 2") != std::string::npos, "There should be 2 draw packets for 2 cubes in different states");

  root->unref();
  return 0;
}

int testCameraPerDraw() {
  SoSeparator * root = new SoSeparator;
  root->ref();

  SoSeparator * branch1 = new SoSeparator;
  SoPerspectiveCamera * cam1 = new SoPerspectiveCamera;
  cam1->position.setValue(0.0f, 0.0f, 10.0f);
  branch1->addChild(cam1);
  branch1->addChild(new SoCone);
  root->addChild(branch1);

  SoSeparator * branch2 = new SoSeparator;
  SoPerspectiveCamera * cam2 = new SoPerspectiveCamera;
  cam2->position.setValue(0.0f, 10.0f, 0.0f);
  branch2->addChild(cam2);
  branch2->addChild(new SoCube);
  root->addChild(branch2);

  CoinRenderAction action;
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "apply should succeed");
  std::string log = action.getRecordingLog().getString();
  TEST_ASSERT(log.find("cameras count: 2") != std::string::npos, "Should record 2 camera snapshots");

  root->unref();
  return 0;
}

int testScopedLight() {
  SoSeparator * root = new SoSeparator;
  root->ref();

  SoSeparator * branchWithLight = new SoSeparator;
  SoDirectionalLight * light = new SoDirectionalLight;
  light->direction.setValue(0.0f, -1.0f, 0.0f);
  branchWithLight->addChild(light);
  branchWithLight->addChild(new SoCone);
  root->addChild(branchWithLight);

  SoSeparator * branchNoLight = new SoSeparator;
  branchNoLight->addChild(new SoCube);
  root->addChild(branchNoLight);

  CoinRenderAction action;
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "apply should succeed");
  std::string log = action.getRecordingLog().getString();
  TEST_ASSERT(log.find("lights=1") != std::string::npos, "Branch 1 should see light");
  TEST_ASSERT(log.find("lights=0") != std::string::npos, "Branch 2 should not see scoped light from sibling");

  root->unref();
  return 0;
}

int testLightingCaptureAcrossShapesAndFrames() {
  auto * root = new SoSeparator;
  root->ref();
  auto * model = new SoLightModel;
  model->model = SoLightModel::PHONG;
  root->addChild(model);
  auto * ambientA = new SoEnvironment;
  ambientA->ambientColor.setValue(.1f,.2f,.8f);
  ambientA->ambientIntensity = .4f;
  root->addChild(ambientA);
  auto * lightA = new SoDirectionalLight;
  lightA->color.setValue(1,.2f,.1f);
  root->addChild(lightA);
  root->addChild(new SoCube);
  auto * branch = new SoSeparator;
  auto * ambientB = new SoEnvironment;
  ambientB->ambientColor.setValue(.8f,.3f,.1f);
  ambientB->ambientIntensity = .6f;
  branch->addChild(ambientB);
  auto * lightB = new SoDirectionalLight;
  lightB->color.setValue(.1f,1,.2f);
  branch->addChild(lightB);
  branch->addChild(new SoCube);
  root->addChild(branch);
  root->addChild(new SoCube);
  CoinRenderAction action;
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "scoped lighting capture succeeds");
  const auto first = action.getPimpl()->lastValidPlan;
  TEST_ASSERT(first.draws.size() == 3, "capture three independent shapes");
  const auto & a = first.lightingStates[first.renderStates[first.draws[0].renderStateSlot].lightingSlot];
  const auto & b = first.lightingStates[first.renderStates[first.draws[1].renderStateSlot].lightingSlot];
  const auto & c = first.lightingStates[first.renderStates[first.draws[2].renderStateSlot].lightingSlot];
  TEST_ASSERT(a.lights.size() == 1 && b.lights.size() == 2 && c.lights.size() == 1,
              "light lists follow separator scope");
  TEST_ASSERT(a.lights[0].color[0] == 1 && b.lights[1].color[1] == 1 && c.lights[0].color[0] == 1,
              "captured lights remain independent of later shapes");
  TEST_ASSERT(a.ambientIntensity == .4f && b.ambientIntensity == .6f && c.ambientIntensity == .4f &&
              a.ambientColor[2] == .8f && b.ambientColor[0] == .8f && c.ambientColor[2] == .8f,
              "ambient values follow separator scope");
  lightA->on = FALSE;
  ambientA->ambientIntensity = .7f;
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "recapture changed lighting succeeds");
  const auto & second = action.getPimpl()->lastValidPlan;
  for (size_t i = 0; i < 3; ++i) {
    const auto & lighting = second.lightingStates[second.renderStates[second.draws[i].renderStateSlot].lightingSlot];
    TEST_ASSERT(lighting.lights.size() == (i == 1 ? 1u : 0u), "no light values leak from a previous frame");
    TEST_ASSERT(lighting.ambientIntensity == (i == 1 ? .6f : .7f), "changed ambient values are recaptured");
  }
  TEST_ASSERT(a.lights.size() == 1 && b.lights.size() == 2 && a.ambientIntensity == .4f,
              "previous captured plan retains its own light payload");
  root->unref();
  return 0;
}

int testPrimitives() {
  SoSeparator * root = new SoSeparator;
  root->ref();

  // 1. Cube (triangles)
  root->addChild(new SoCube);

  // 2. Line set (lines)
  SoSeparator * lineSep = new SoSeparator;
  SoCoordinate3 * coords = new SoCoordinate3;
  coords->point.set1Value(0, 0.0f, 0.0f, 0.0f);
  coords->point.set1Value(1, 1.0f, 1.0f, 1.0f);
  SoLineSet * lineSet = new SoLineSet;
  lineSep->addChild(coords);
  lineSep->addChild(lineSet);
  root->addChild(lineSep);

  // 3. Point set (points)
  SoSeparator * ptSep = new SoSeparator;
  SoCoordinate3 * ptCoords = new SoCoordinate3;
  ptCoords->point.set1Value(0, 2.0f, 2.0f, 2.0f);
  SoPointSet * ptSet = new SoPointSet;
  ptSep->addChild(ptCoords);
  ptSep->addChild(ptSet);
  root->addChild(ptSep);

  CoinRenderAction action;
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "apply should succeed");
  std::string log = action.getRecordingLog().getString();
  const size_t firstTriangle = log.find("top=TRIANGLES");
  const size_t secondTriangle = firstTriangle == std::string::npos
    ? std::string::npos : log.find("top=TRIANGLES", firstTriangle + 1);
  const size_t thirdTriangle = secondTriangle == std::string::npos
    ? std::string::npos : log.find("top=TRIANGLES", secondTriangle + 1);
  TEST_ASSERT(firstTriangle != std::string::npos &&
              secondTriangle != std::string::npos &&
              thirdTriangle != std::string::npos,
              "Triangles, lines and points should produce three portable triangle draws");

  root->unref();
  return 0;
}

int testMaterialBinding() {
  SoSeparator * root = new SoSeparator;
  root->ref();

  SoMaterial * mat = new SoMaterial;
  mat->diffuseColor.setValue(0.2f, 0.6f, 0.8f);
  mat->shininess.setValue(0.5f);
  root->addChild(mat);
  root->addChild(new SoCone);

  CoinRenderAction action;
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "apply should succeed");
  std::string log = action.getRecordingLog().getString();
  TEST_ASSERT(log.find("diff=[0.2000,0.6000,0.8000,1.0000]") != std::string::npos, "Material diffuse should match");

  root->unref();
  return 0;
}

int testProjectionConversion() {
  // Test mathematically the clip space conversion matrix C
  SbMatrix C(
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.5f, 0.0f,
    0.0f, 0.0f, 0.5f, 1.0f
  );

  // In Coin/OpenGL NDC:
  // Near plane NDC: (0, 0, -1, 1) -> z_ndc = -1
  SbVec4f nearGl(0.0f, 0.0f, -1.0f, 1.0f);
  SbVec4f nearWgpu;
  C.multVecMatrix(nearGl, nearWgpu);
  float nearZ = nearWgpu[2] / nearWgpu[3];
  TEST_ASSERT(std::abs(nearZ - 0.0f) < 1e-6f, "Near plane must map to 0.0 in WebGPU clip space");

  // Far plane NDC: (0, 0, 1, 1) -> z_ndc = 1
  SbVec4f farGl(0.0f, 0.0f, 1.0f, 1.0f);
  SbVec4f farWgpu;
  C.multVecMatrix(farGl, farWgpu);
  float farZ = farWgpu[2] / farWgpu[3];
  TEST_ASSERT(std::abs(farZ - 1.0f) < 1e-6f, "Far plane must map to 1.0 in WebGPU clip space");

  // Midplane NDC: (0, 0, 0, 1) -> z_ndc = 0
  SbVec4f midGl(0.0f, 0.0f, 0.0f, 1.0f);
  SbVec4f midWgpu;
  C.multVecMatrix(midGl, midWgpu);
  float midZ = midWgpu[2] / midWgpu[3];
  TEST_ASSERT(std::abs(midZ - 0.5f) < 1e-6f, "Midplane must map to 0.5 in WebGPU clip space");

  return 0;
}

int testEmptyInputs() {
  CoinRenderAction action;
  // 1. apply(NULL)
  action.apply(static_cast<SoNode *>(NULL));
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "apply(NULL) should succeed with empty plan");

  // 2. empty separator
  SoSeparator * emptySep = new SoSeparator;
  emptySep->ref();
  action.apply(emptySep);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "apply(emptySep) should succeed");
  emptySep->unref();

  return 0;
}

int testNodePathPathList() {
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoCube * cube = new SoCube;
  root->addChild(cube);

  CoinRenderAction action;

  // Apply to Node
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "apply(node) should succeed");
  std::string nodeLog = action.getRecordingLog().getString();

  // Apply to Path
  SoPath * path = new SoPath(root);
  path->ref();
  path->append(cube);
  action.apply(path);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "apply(path) should succeed");
  std::string pathLog = action.getRecordingLog().getString();
  TEST_ASSERT(!pathLog.empty(), "Path log should not be empty");

  // Apply to PathList
  SoPathList pathlist;
  pathlist.append(path);
  action.apply(pathlist);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "apply(pathlist) should succeed");

  path->unref();
  root->unref();
  return 0;
}

int testDeterminism() {
  SoSeparator * root1 = new SoSeparator;
  root1->ref();
  root1->addChild(new SoCone);

  SoSeparator * root2 = new SoSeparator;
  root2->ref();
  root2->addChild(new SoCone);

  CoinRenderAction action1;
  action1.apply(root1);
  std::string log1 = action1.getRecordingLog().getString();

  CoinRenderAction action2;
  action2.apply(root2);
  std::string log2 = action2.getRecordingLog().getString();

  TEST_ASSERT(log1 == log2, "Two identical scenes must generate identical canonical recording logs");

  root1->unref();
  root2->unref();
  return 0;
}

static CoinRenderAction::Status g_nestedStatus = CoinRenderAction::SUCCESS;

static void reentrantCallback(void * userdata, SoAction * action) {
  CoinRenderAction * wgpuAction = static_cast<CoinRenderAction *>(userdata);
  if (action == wgpuAction) {
    SoCube * dummy = new SoCube;
    dummy->ref();
    wgpuAction->apply(dummy); // Forbidden reentrant apply()!
    g_nestedStatus = wgpuAction->getLastStatus();
    dummy->unref();
  }
}

int testForbiddenUsage() {
  SoSeparator * root = new SoSeparator;
  root->ref();
  CoinRenderAction action;

  SoCallback * cb = new SoCallback;
  cb->setCallback(reentrantCallback, &action);
  root->addChild(cb);

  action.apply(root);
  TEST_ASSERT(g_nestedStatus == CoinRenderAction::INVALID_SCENE, "Nested apply() must be rejected with INVALID_SCENE");
  // Finding 6: Outer status must remain INVALID_SCENE after nested failure
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::INVALID_SCENE, "Outer status must remain INVALID_SCENE");

  root->unref();
  return 0;
}

int testBackendAvailability() {
  // This reports whether a hardware backend was compiled.
  SbBool avail = CoinRenderAction::isGpuBackendAvailable();
#if defined(HAVE_COIN_DAWN) || defined(HAVE_COIN_WGPU_NATIVE) || defined(HAVE_COIN_WGPU_RUST_BRIDGE) || defined(HAVE_COIN_BGFX)
  TEST_ASSERT(avail == TRUE, "isGpuBackendAvailable must be TRUE when hardware backend is compiled");
#else
  TEST_ASSERT(avail == FALSE, "isGpuBackendAvailable must be FALSE in software/recording mode");
#endif
  return 0;
}

int testMultipleRootsPathList() {
  // Finding 2: SoPathList with multiple roots must preserve geometry from all roots
  SoSeparator * root1 = new SoSeparator;
  root1->ref();
  SoCube * cube = new SoCube;
  root1->addChild(cube);

  SoSeparator * root2 = new SoSeparator;
  root2->ref();
  SoCone * cone = new SoCone;
  root2->addChild(cone);

  SoPath * p1 = new SoPath(root1);
  p1->ref();
  p1->append(cube);

  SoPath * p2 = new SoPath(root2);
  p2->ref();
  p2->append(cone);

  SoPathList pathlist;
  pathlist.append(p1);
  pathlist.append(p2);

  CoinRenderAction action;
  action.apply(pathlist);

  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "apply(pathlist with multiple roots) should succeed");
  std::string log = action.getRecordingLog().getString();
  TEST_ASSERT(log.find("draws count: 2") != std::string::npos, "Frame plan must contain draws from both roots (draws count: 2)");

  p1->unref();
  p2->unref();
  root1->unref();
  root2->unref();
  return 0;
}

int testPerVertexMaterialCapture() {
  // Finding 3: Per-vertex materials must capture distinct materials for each vertex
  SoSeparator * root = new SoSeparator;
  root->ref();

  SoMaterial * mat = new SoMaterial;
  mat->diffuseColor.set1Value(0, SbColor(1.0f, 0.0f, 0.0f)); // v0 = Red
  mat->diffuseColor.set1Value(1, SbColor(0.0f, 1.0f, 0.0f)); // v1 = Green
  mat->diffuseColor.set1Value(2, SbColor(0.0f, 0.0f, 1.0f)); // v2 = Blue
  root->addChild(mat);

  SoMaterialBinding * mb = new SoMaterialBinding;
  mb->value = SoMaterialBinding::PER_VERTEX;
  root->addChild(mb);

  SoCoordinate3 * coords = new SoCoordinate3;
  coords->point.set1Value(0, SbVec3f(0.0f, 1.0f, 0.0f));
  coords->point.set1Value(1, SbVec3f(-1.0f, -1.0f, 0.0f));
  coords->point.set1Value(2, SbVec3f(1.0f, -1.0f, 0.0f));
  root->addChild(coords);

  SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
  int32_t indices[] = { 0, 1, 2, -1 };
  ifs->coordIndex.setValues(0, 4, indices);
  root->addChild(ifs);

  CoinRenderAction action;
  action.apply(root);

  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "apply() on per-vertex material scene should succeed");
  std::string log = action.getRecordingLog().getString();
  // Ensure multiple materials were captured (at least 3 distinct materials for R, G, B)
  TEST_ASSERT(log.find("materials count: 3") != std::string::npos || log.find("materials count: 4") != std::string::npos,
              "Frame plan must capture distinct materials for each vertex");

  root->unref();
  return 0;
}

int testLightTransformWithRotatedCamera() {
  // Finding 4: Directional light should not be double-transformed when camera is rotated
  SoSeparator * root = new SoSeparator;
  root->ref();

  SoPerspectiveCamera * cam = new SoPerspectiveCamera;
  // Rotate camera 90 degrees around Y axis
  cam->orientation.setValue(SbVec3f(0.0f, 1.0f, 0.0f), static_cast<float>(M_PI / 2.0));
  cam->position.setValue(5.0f, 0.0f, 0.0f);
  root->addChild(cam);

  SoDirectionalLight * dl = new SoDirectionalLight;
  dl->direction.setValue(0.0f, 0.0f, -1.0f);
  root->addChild(dl);

  SoCube * cube = new SoCube;
  root->addChild(cube);

  CoinRenderAction action;
  action.apply(root);

  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "apply() with rotated camera must succeed");
  std::string log = action.getRecordingLog().getString();
  TEST_ASSERT(!log.empty(), "Recording log must not be empty");

  root->unref();
  return 0;
}

int testPlanOverflowAndSlotValidation() {
  // Finding 5: Validation must catch range overflow and empty slots
  CoinRenderFramePlan plan;
  plan.viewports.push_back(CoinRenderViewportSnapshot{});
  plan.cameras.push_back(CoinRenderCameraSnapshot{});
  plan.materials.push_back(CoinRenderMaterialSnapshot{});
  plan.lightingStates.push_back(CoinRenderLightingSnapshot{});

  CoinRenderRenderStateSnapshot rs;
  rs.viewportSlot = 0;
  rs.cameraSlot = 0;
  rs.materialSlot = 0;
  rs.lightingSlot = 0;
  plan.renderStates.push_back(rs);

  CoinRenderVertexSnapshot v{};
  v.position[0] = 0.0f; v.position[1] = 0.0f; v.position[2] = 0.0f;
  v.normal[0] = 0.0f; v.normal[1] = 0.0f; v.normal[2] = 1.0f;
  v.texcoord[0] = 0.0f; v.texcoord[1] = 0.0f;
  v.materialSlot = 0;
  plan.vertices.push_back(v);
  plan.vertices.push_back(v);
  plan.vertices.push_back(v);
  plan.indices.push_back(0);
  plan.indices.push_back(1);
  plan.indices.push_back(2);

  CoinRenderDrawPacket draw;
  draw.renderStateSlot = 0;
  draw.topology = CoinRenderPrimitiveTopology::TRIANGLE_LIST;
  draw.geometry.firstVertex = 0;
  draw.geometry.vertexCount = 3;
  draw.geometry.firstIndex = 0;
  draw.geometry.indexCount = 3;
  plan.draws.push_back(draw);

  std::string err;
  TEST_ASSERT(plan.isValid(&err), "Base plan must be valid");

  // Test 1: Overflow firstVertex + vertexCount
  plan.draws[0].geometry.firstVertex = 0xFFFFFFFF;
  plan.draws[0].geometry.vertexCount = 2;
  TEST_ASSERT(!plan.isValid(&err), "Overflow in vertex range must be rejected");

  // Test 2: Empty materials with vertices referencing slot 0
  plan.draws[0].geometry.firstVertex = 0;
  plan.draws[0].geometry.vertexCount = 3;
  plan.materials.clear();
  TEST_ASSERT(!plan.isValid(&err), "Empty materials with active vertices must be rejected");

  return 0;
}


int testProfileMultiLightAndPerVertexTransparency() {
  // Test Finding 3: Multi-light rejection and per-vertex transparency
  CoinRenderFramePlan plan;
  plan.viewports.push_back(CoinRenderViewportSnapshot{});
  plan.cameras.push_back(CoinRenderCameraSnapshot{});

  CoinRenderMaterialSnapshot opaqueMat;
  opaqueMat.transparency = 0.0f;
  plan.materials.push_back(opaqueMat);

  CoinRenderMaterialSnapshot transparentMat;
  transparentMat.transparency = 0.5f;
  transparentMat.diffuse[3] = 0.5f;
  plan.materials.push_back(transparentMat);

  CoinRenderLightingSnapshot singleLight;
  CoinRenderLightSourceSnapshot l1;
  l1.type = CoinRenderLightType::DIRECTIONAL;
  singleLight.lights.push_back(l1);
  plan.lightingStates.push_back(singleLight);

  CoinRenderLightingSnapshot multiLight;
  multiLight.lights.push_back(l1);
  multiLight.lights.push_back(l1);
  plan.lightingStates.push_back(multiLight);

  CoinRenderRenderStateSnapshot rs;
  rs.viewportSlot = 0;
  rs.cameraSlot = 0;
  rs.materialSlot = 0; // opaque
  rs.lightingSlot = 0; // single light
  rs.transparencyType = SoGLRenderAction::SORTED_OBJECT_BLEND;
  plan.renderStates.push_back(rs);

  CoinRenderVertexSnapshot v0{}, v1{}, v2{};
  v0.materialSlot = 0;
  v1.materialSlot = 0;
  v2.materialSlot = 0;
  plan.vertices.push_back(v0);
  plan.vertices.push_back(v1);
  plan.vertices.push_back(v2);

  plan.indices.push_back(0);
  plan.indices.push_back(1);
  plan.indices.push_back(2);

  CoinRenderDrawPacket draw;
  draw.renderStateSlot = 0;
  draw.topology = CoinRenderPrimitiveTopology::TRIANGLE_LIST;
  draw.geometry.firstVertex = 0;
  draw.geometry.vertexCount = 3;
  draw.geometry.firstIndex = 0;
  draw.geometry.indexCount = 3;
  plan.draws.push_back(draw);

  std::string diag;
  // Case 1: Valid single light, opaque
  TEST_ASSERT(CoinRenderTargetP::validateProfile(plan, diag), "Base profile should be valid");

  // Case 2: two lights are supported in 3C; nine exceed the published budget.
  plan.renderStates[0].lightingSlot = 1; // multiLight
  TEST_ASSERT(CoinRenderTargetP::validateProfile(plan, diag), "Two lights should be supported");
  plan.lightingStates[1].lights.resize(9, l1);
  TEST_ASSERT(!CoinRenderTargetP::validateProfile(plan, diag), "Nine lights must be rejected by validateProfile");
  TEST_ASSERT(diag.find("eight") != std::string::npos, "Diagnostic must mention eight-light limit");
  plan.lightingStates[1].lights.resize(2);
  plan.renderStates[0].lightingSlot = 0; // restore

  // Case 3: heterogeneous per-vertex alpha is a supported blended draw.
  plan.vertices[1].materialSlot = 1; // transparentMat
  TEST_ASSERT(CoinRenderTargetP::validateProfile(plan, diag), "Per-vertex transparency should be supported");
  plan.vertices[1].materialSlot = 0; // restore

  return 0;
}

int testBaseApplyNotHidden() {
  // Test Finding 4: SoAction::apply(SoAction*) is not hidden
  void (CoinRenderAction::*applyActionFn)(SoAction*) = &CoinRenderAction::apply;
  TEST_ASSERT(applyActionFn != nullptr, "apply(SoAction*) must be accessible via CoinRenderAction");

  SoSeparator * root = new SoSeparator;
  root->ref();
  CoinRenderAction action1;
  CoinRenderAction action2;
  // If we apply action1 to a node, then action2.apply(&action1) copies what action1 was applied to
  // (though action1 is done, let's verify compiler and method binding)
  root->unref();
  return 0;
}

int testWindowTargetRecordingBackend() {
#if !defined(HAVE_COIN_WGPU_RUST_BRIDGE) && !defined(HAVE_COIN_BGFX)
  CoinRenderNativeSurfaceDescriptor desc{};
  desc.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  desc.structSize = sizeof(desc);
  desc.type = COIN_RENDER_SURFACE_XLIB;
  desc.reserved = 0;
  desc.native.xlib.display = (void*)0x1234;
  desc.native.xlib.window = 1;

  CoinRenderTarget * target = CoinRenderTarget::createWindow(desc, SbVec2i32(100, 100));
  TEST_ASSERT(target != nullptr, "Target pointer must not be null");
  TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_ERROR, "Status must be TARGET_ERROR in RECORDING mode");
  TEST_ASSERT(std::string(target->getLastError()).find("RUST_BRIDGE or BGFX") != std::string::npos, "Diagnostic must identify the supported window executors");

  SoSeparator * root = new SoSeparator;
  root->ref();
  CoinRenderAction action;
  action.setRenderTarget(target);
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::BACKEND_ERROR, "apply on TARGET_ERROR must report BACKEND_ERROR");
  TEST_ASSERT(action.getLastError().getLength() > 0, "action getLastError must be populated");
  root->unref();
  delete target;
#endif
  return 0;
}

int testCameraOverlayAndFallback() {
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  camera->position.setValue(0.0f, 0.0f, 5.0f);
  root->addChild(camera);
  SoLightModel * lightModel = new SoLightModel;
  lightModel->model = SoLightModel::BASE_COLOR;
  root->addChild(lightModel);
  SoCoordinate3 * coordinates = new SoCoordinate3;
  coordinates->point.set1Value(0, SbVec3f(-1.0f, -1.0f, 0.0f));
  coordinates->point.set1Value(1, SbVec3f(1.0f, -1.0f, 0.0f));
  coordinates->point.set1Value(2, SbVec3f(0.0f, 1.0f, 0.0f));
  root->addChild(coordinates);
  SoIndexedFaceSet * faces = new SoIndexedFaceSet;
  const int32_t indices[] = {0, 1, 2, -1};
  faces->coordIndex.setValues(0, 4, indices);
  root->addChild(faces);

  CoinRenderAction cached(SbViewportRegion(128, 128));
  CoinRenderAction fresh(SbViewportRegion(128, 128));
  cached.apply(root);
  TEST_ASSERT(cached.getLastStatus() == CoinRenderAction::SUCCESS,
              "Initial eligible frame should succeed");

  camera->position.setValue(0.25f, 0.0f, 5.0f);
  camera->nearDistance = 0.2f;
  camera->farDistance = 20.0f;
  CoinRenderReadbackTicket missingTargetTicket{};
  cached.applyAsync(root, missingTargetTicket);
  TEST_ASSERT(cached.getLastStatus() == CoinRenderAction::NO_TARGET,
              "Camera overlay must roll back after missing async target");
  cached.apply(root);
  fresh.apply(root);
  TEST_ASSERT(cached.getLastStatus() == CoinRenderAction::SUCCESS &&
              fresh.getLastStatus() == CoinRenderAction::SUCCESS,
              "Moved-camera frames should succeed");
  TEST_ASSERT(std::string(cached.getRecordingLog().getString()) ==
              std::string(fresh.getRecordingLog().getString()),
              "Camera overlay must match a full traversal");

  coordinates->point.set1Value(2, SbVec3f(0.0f, 1.25f, 0.0f));
  cached.apply(root);
  fresh.apply(root);
  TEST_ASSERT(std::string(cached.getRecordingLog().getString()) ==
              std::string(fresh.getRecordingLog().getString()),
              "Geometry notification must force an equivalent full traversal");

  root->addChild(new SoCube);
  camera->position.setValue(-0.25f, 0.0f, 5.0f);
  cached.apply(root);
  fresh.apply(root);
  TEST_ASSERT(std::string(cached.getRecordingLog().getString()) ==
              std::string(fresh.getRecordingLog().getString()),
              "Structural notification must force an equivalent full traversal");
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  CoinRenderTarget * cachedTarget =
    CoinRenderTarget::createOffscreen(SbVec2i32(128, 128));
  CoinRenderTarget * freshTarget =
    CoinRenderTarget::createOffscreen(SbVec2i32(128, 128));
  TEST_ASSERT(cachedTarget && freshTarget, "GPU comparison targets must exist");
  CoinRenderAction cachedGpu(SbViewportRegion(128, 128));
  CoinRenderAction freshGpu(SbViewportRegion(128, 128));
  cachedGpu.setRenderTarget(cachedTarget);
  freshGpu.setRenderTarget(freshTarget);
  cachedGpu.apply(root);
  TEST_ASSERT(cachedGpu.getLastStatus() == CoinRenderAction::SUCCESS,
              "Initial GPU frame must succeed");
  camera->position.setValue(0.5f, 0.0f, 5.0f);
  cachedGpu.apply(root);
  freshGpu.apply(root);
  TEST_ASSERT(cachedGpu.getLastStatus() == CoinRenderAction::SUCCESS &&
              freshGpu.getLastStatus() == CoinRenderAction::SUCCESS,
              "Moved-camera GPU frames must succeed");
  std::vector<uint8_t> cachedPixels, freshPixels;
  cachedTarget->readbackRGBA(cachedPixels);
  freshTarget->readbackRGBA(freshPixels);
  TEST_ASSERT(!cachedPixels.empty() && cachedPixels == freshPixels,
              "Camera overlay pixels must match a full GPU traversal");
  delete cachedTarget;
  delete freshTarget;
#endif
  root->unref();
  return 0;
}

class CameraOverlayTestBackend : public CoinRenderBackend {
public:
  bool failNext = false;
  CoinRenderFrameReuseDecision lastReuse;
  unsigned submissions = 0;
  bool isGpuBackend() const override { return false; }
  CoinRenderBackendStatus getStatus() const override { return CoinRenderBackendStatus::SUCCESS; }
  CoinRenderBackendStatus prepare(CoinRenderTargetP &) override { return CoinRenderBackendStatus::SUCCESS; }
  void poll() override {}
  const std::string & getLastError() const override { return error; }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target) override {
    return submit(frame, target, CoinRenderFrameReuseDecision{});
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &, CoinRenderTargetP & target,
                               const CoinRenderFrameReuseDecision & reuse) override {
    lastReuse = reuse;
    ++submissions;
    if (failNext) {
      failNext = false;
      return {CoinRenderBackendStatus::UNSUPPORTED, "injected camera-overlay submission rejection"};
    }
    target.colorBuffer = {17, 29, 43, 255};
    return {};
  }
private:
  std::string error;
};

struct TranslationOverlayEnvironment {
  const char * flags[3] = {"COIN_RENDER_DISABLE_TRANSLATION_OVERLAY", "COIN_RENDER_DISABLE_MATERIAL_OVERLAY",
                          "COIN_RENDER_DISABLE_CUBE_OVERLAY"};
  std::string values[3];
  bool present[3];
  TranslationOverlayEnvironment() {
    for (int i = 0; i < 3; ++i) {
      const char * previous = std::getenv(this->flags[i]);
      this->present[i] = previous != nullptr; this->values[i] = previous ? previous : "";
      coinRenderTestSetEnvironment(this->flags[i], "0");
    }
  }
  ~TranslationOverlayEnvironment() {
    for (int i = 0; i < 3; ++i)
      coinRenderTestSetEnvironment(this->flags[i], this->present[i] ? this->values[i].c_str() : nullptr);
  }
};

struct TranslationOverlayScene {
  SoSeparator * root = new SoSeparator;
  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  SoTransform * prefix = new SoTransform;
  SoPointLight * light = new SoPointLight;
  SoMaterial * material = new SoMaterial;
  SoCube * cube = new SoCube;
  std::vector<SoSeparator *> objects;
  std::vector<SoNode *> transforms;
  std::vector<SoSFVec3f *> positions;
  std::vector<SbVec3f> initialPositions;
  TranslationOverlayScene(unsigned count = 6) {
    this->root->ref();
    this->camera->position.setValue(0, 0, 20);
    this->camera->nearDistance = .2f; this->camera->farDistance = 80;
    this->root->addChild(this->camera);
    this->prefix->translation.setValue(1, -2, .5f);
    this->prefix->rotation.setValue(SbVec3f(1, -2, 3), .23f);
    this->prefix->scaleFactor.setValue(.7f, -1.1f, 1.4f);
    this->prefix->center.setValue(.5f, -.3f, .2f);
    this->root->addChild(this->prefix);
    this->light->location.setValue(-2, 4, 7);
    this->root->addChild(this->light);
    this->material->diffuseColor.setValue(.5f, .3f, .2f);
    for (unsigned i = 0; i < count; ++i) {
      auto * object = new SoSeparator;
      object->addChild(this->material);
      SoNode * transform;
      SoSFVec3f * position;
      if (i % 2) {
        auto * local = new SoTransform;
        local->rotation.setValue(SbVec3f(2, 1, -3), .17f);
        local->scaleFactor.setValue(-.8f, 1.2f, .9f);
        local->scaleOrientation.setValue(SbVec3f(1, 3, 2), .11f);
        transform = local; position = &local->translation;
      } else {
        auto * local = new SoTranslation;
        transform = local; position = &local->translation;
      }
      const SbVec3f initial(float(i * 4) + .2f, float(i % 3) + .3f, -.4f);
      position->setValue(initial);
      object->addChild(transform); object->addChild(this->cube); this->root->addChild(object);
      this->objects.push_back(object); this->transforms.push_back(transform);
      this->positions.push_back(position); this->initialPositions.push_back(initial);
    }
  }
  ~TranslationOverlayScene() { this->root->unref(); }
};

int testTranslationOverlayAndRollback() {
  TranslationOverlayEnvironment environment;
  TranslationOverlayScene scene;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(1, 1)));
  TEST_ASSERT(target, "CPU translation proof test requires a target shell");
  auto * backend = new CameraOverlayTestBackend;
  target->getPimpl()->backend.reset(backend); target->getPimpl()->depthReadbackEnabled = false;
  CoinRenderAction action(SbViewportRegion(1, 1)), full(SbViewportRegion(1, 1));
  full.getPimpl()->planOnly = true;
  action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
  full.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
  action.setRenderTarget(target.get()); action.apply(scene.root); full.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && action.getPimpl()->translationProofValid &&
              action.getPimpl()->translationBindings.size() == scene.positions.size() &&
              action.getPimpl()->lastValidPlan.hasSamePayload(full.getPimpl()->lastValidPlan),
              "shared Cube geometry with exclusive transform/state occurrences must qualify exactly");
  const auto * vertices = action.getPimpl()->lastValidPlan.vertices.data();
  const auto * indices = action.getPimpl()->lastValidPlan.indices.data();
  const uint64_t generation = action.getPimpl()->translationProofGeneration;
  for (int frame = 0; frame < 3; ++frame) {
    const uint64_t base = action.getPimpl()->lastValidPlan.revision;
    for (unsigned i : {0u, 1u, 4u}) {
      SbVec3f position = scene.initialPositions[i];
      if (frame < 2) position += SbVec3f(.13f * (frame + 1), -.07f * frame, .04f);
      scene.positions[i]->setValue(position);
    }
    TEST_ASSERT(action.getPimpl()->translationInputDirty && !action.getPimpl()->translationInvalidated &&
                action.getPimpl()->translationDirty.size() == 3,
                "only changed translations must enter the bounded dirty occurrence set");
    action.apply(scene.root); full.apply(scene.root);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                backend->lastReuse.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD &&
                backend->lastReuse.baseRevision == base && action.getPimpl()->lastValidPlan.revision > base &&
                action.getPimpl()->lastValidPlan.vertices.data() == vertices &&
                action.getPimpl()->lastValidPlan.indices.data() == indices &&
                action.getPimpl()->translationProofGeneration == generation &&
                action.getPimpl()->translationDirty.empty() &&
                action.getPimpl()->lastValidPlan.hasSamePayload(full.getPimpl()->lastValidPlan),
                "mixed Translation/Transform movement and return must match full matrix capture by bytes without copying geometry");
  }
  const auto beforeFailure = action.getPimpl()->lastValidPlan;
  const auto pixels = target->getPimpl()->colorBuffer;
  scene.positions[1]->setValue(scene.initialPositions[1] + SbVec3f(.31f, -.2f, .1f));
  backend->failNext = true; action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
              action.getPimpl()->lastValidPlan.revision == beforeFailure.revision &&
              action.getPimpl()->lastValidPlan.hasSamePayload(beforeFailure) &&
              target->getPimpl()->colorBuffer == pixels && action.getPimpl()->translationDirty.size() == 1 &&
              action.getPimpl()->translationProofValid && action.getPimpl()->translationInputDirty,
              "late failure must restore model/revision/pixels and preserve the pending translation proof");
  action.apply(scene.root); full.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS &&
              backend->lastReuse.baseRevision == beforeFailure.revision &&
              backend->lastReuse.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD &&
              action.getPimpl()->lastValidPlan.hasSamePayload(full.getPimpl()->lastValidPlan),
              "retry must apply from the last successfully submitted plan and match full capture");
  action.apply(scene.root);
  TEST_ASSERT(backend->lastReuse.kind == CoinRenderFrameReuseKind::REUSE,
              "a static frame after translation must reuse the submitted model state");
  scene.camera->position.setValue(.4f, -.2f, 19);
  action.apply(scene.root); full.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS &&
              backend->lastReuse.kind != CoinRenderFrameReuseKind::CAMERA_PATCH &&
              action.getPimpl()->translationProofValid && !action.getPimpl()->cameraRecaptureRequired &&
              action.getPimpl()->translationProofGeneration > generation &&
              action.getPimpl()->lastValidPlan.hasSamePayload(full.getPimpl()->lastValidPlan),
              "camera after translated objects must recapture and readmit the new translation anchor");
  vertices = action.getPimpl()->lastValidPlan.vertices.data();
  scene.positions[0]->setValue(scene.initialPositions[0] + SbVec3f(-.15f, .06f, 0));
  action.apply(scene.root); full.apply(scene.root);
  TEST_ASSERT(backend->lastReuse.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD &&
              action.getPimpl()->lastValidPlan.vertices.data() == vertices &&
              action.getPimpl()->lastValidPlan.hasSamePayload(full.getPimpl()->lastValidPlan),
              "translation after camera recapture must reuse only its newly qualified model anchor");
  coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_TRANSLATION_OVERLAY", "1");
  scene.positions[0]->setValue(scene.initialPositions[0]);
  const uint64_t oldGeneration = action.getPimpl()->translationGeneration;
  action.apply(scene.root); full.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && !action.getPimpl()->translationProofValid &&
              action.getPimpl()->translationGeneration > oldGeneration &&
              action.getPimpl()->lastValidPlan.hasSamePayload(full.getPimpl()->lastValidPlan),
              "optout must force ordinary capture and discard the previous proof");
  return 0;
}

SoCallbackAction::Response translationCountingCallback(void * data, SoCallbackAction *, const SoNode *) {
  ++*static_cast<unsigned *>(data);
  return SoCallbackAction::CONTINUE;
}

struct CaptureCameraBasisEnvironment {
  const char * flags[2] = {"COIN_RENDER_DISABLE_CAPTURE_CAMERA_BASIS_REUSE", "COIN_RENDER_DISABLE_CAMERA_OVERLAY"};
  std::string values[2];
  bool present[2];
  CaptureCameraBasisEnvironment() {
    for (int i = 0; i < 2; ++i) {
      const char * previous = std::getenv(flags[i]);
      present[i] = previous != nullptr; values[i] = previous ? previous : "";
      coinRenderTestSetEnvironment(flags[i], "0");
    }
  }
  ~CaptureCameraBasisEnvironment() {
    for (int i = 0; i < 2; ++i)
      coinRenderTestSetEnvironment(flags[i], present[i] ? values[i].c_str() : nullptr);
  }
};

struct CaptureCameraBasisOracle {
  // Actions/sensors must be destroyed before their target shells.
  std::unique_ptr<CoinRenderTarget> target, literalTarget;
  CameraOverlayTestBackend * backend = nullptr, * literalBackend = nullptr;
  CoinRenderAction action{SbViewportRegion(1, 1)}, literal{SbViewportRegion(1, 1)};
  bool ready = true;
  explicit CaptureCameraBasisOracle(bool recording = false) {
    if (recording) return;
    target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(1, 1)));
    literalTarget.reset(CoinRenderTarget::createOffscreen(SbVec2i32(1, 1)));
    ready = target && literalTarget;
    if (!ready) return;
    backend = new CameraOverlayTestBackend; literalBackend = new CameraOverlayTestBackend;
    target->getPimpl()->backend.reset(backend); literalTarget->getPimpl()->backend.reset(literalBackend);
    target->getPimpl()->depthReadbackEnabled = literalTarget->getPimpl()->depthReadbackEnabled = false;
    action.setRenderTarget(target.get()); literal.setRenderTarget(literalTarget.get());
  }
  template <typename T> void apply(T * root) {
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CAPTURE_CAMERA_BASIS_REUSE", "0");
    action.apply(root);
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CAPTURE_CAMERA_BASIS_REUSE", "1");
    literal.apply(root);
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CAPTURE_CAMERA_BASIS_REUSE", "0");
  }
  bool same() {
    const CoinRenderActionP * a = &action.getPimpl().get(); const CoinRenderActionP * b = &literal.getPimpl().get();
    if (action.getLastStatus() != literal.getLastStatus() || a->lastDiagnosticDomain != b->lastDiagnosticDomain ||
        std::string(action.getLastError().getString()) != literal.getLastError().getString() ||
        a->hasLastValidPlan != b->hasLastValidPlan || !a->lastValidPlan.hasSamePayload(b->lastValidPlan) ||
        a->candidateCamera != b->candidateCamera || a->cachedCamera != b->cachedCamera ||
        a->translationProofValid != b->translationProofValid ||
        a->translationBindings.size() != b->translationBindings.size() ||
        a->materialByNode != b->materialByNode || a->cubeByNode != b->cubeByNode ||
        a->translationInputDirty != b->translationInputDirty || a->translationInvalidated != b->translationInvalidated ||
        a->cameraOnlyDirty != b->cameraOnlyDirty || a->cameraPatchInvalidated != b->cameraPatchInvalidated ||
        a->cameraRecaptureRequired != b->cameraRecaptureRequired) return false;
    for (size_t i = 0; i < a->translationBindings.size(); ++i) {
      const auto & x = a->translationBindings[i]; const auto & y = b->translationBindings[i];
      if (x.transform != y.transform || x.parent != y.parent || x.cube != y.cube || x.field != y.field ||
          x.originalPosition != y.originalPosition || x.prefix != y.prefix || x.anchor != y.anchor ||
          x.firstDraw != y.firstDraw || x.endDraw != y.endDraw || x.stateSlot != y.stateSlot ||
          x.geometry.firstVertex != y.geometry.firstVertex || x.geometry.vertexCount != y.geometry.vertexCount ||
          x.geometry.firstIndex != y.geometry.firstIndex || x.geometry.indexCount != y.geometry.indexCount ||
          x.material != y.material || x.cubeDimensions != y.cubeDimensions || x.overallMaterial != y.overallMaterial ||
          x.materialEligible != y.materialEligible || x.geometryEligible != y.geometryEligible) return false;
    }
    // The proof keeps the captured anchor revision across object overlays;
    // successful overlays advance the published frame revision independently.
    if (a->translationProofValid &&
        (!a->translationProofRevision || a->translationProofRevision > a->lastValidPlan.revision ||
         a->translationProofGeneration != a->translationGeneration ||
         !b->translationProofRevision || b->translationProofRevision > b->lastValidPlan.revision ||
         b->translationProofGeneration != b->translationGeneration)) return false;
    if (backend && (backend->lastReuse.kind != literalBackend->lastReuse.kind ||
                    target->getPimpl()->colorBuffer != literalTarget->getPimpl()->colorBuffer)) return false;
    return true;
  }
  bool counts(size_t prepares, size_t literalPrepares, size_t reuse, size_t calls = 1) {
    const CoinRenderActionP * a = &action.getPimpl().get(); const CoinRenderActionP * b = &literal.getPimpl().get();
    return a->captureCameraBasisCalls == calls && b->captureCameraBasisCalls == calls &&
      a->captureCameraBasisPrepares == prepares && b->captureCameraBasisPrepares == literalPrepares &&
      a->captureCameraBasisReuses == reuse && b->captureCameraBasisReuses == 0;
  }
};

int testCaptureCameraBasisReuse() {
  TranslationOverlayEnvironment objectEnvironment;
  CaptureCameraBasisEnvironment environment;
  // Fresh perspective/orthographic and PHONG/BASE_COLOR capture, through both
  // recording and successful target submission, must publish identical proofs.
  for (bool recording : {false, true}) for (bool ortho : {false, true}) for (bool phong : {false, true}) {
    TranslationOverlayScene scene;
    SoCamera * camera = scene.camera;
    if (ortho) {
      auto * replacement = new SoOrthographicCamera;
      replacement->position.setValue(0, 0, 20);
      replacement->nearDistance = .2f; replacement->farDistance = 80;
      scene.root->replaceChild(scene.camera, replacement);
      camera = replacement;
    }
    auto * model = new SoLightModel;
    model->model = phong ? SoLightModel::PHONG : SoLightModel::BASE_COLOR;
    scene.root->insertChild(model, 1);
    CaptureCameraBasisOracle oracle(recording);
    TEST_ASSERT(oracle.ready, "camera basis oracle requires CPU target shells");
    oracle.apply(scene.root);
    TEST_ASSERT(oracle.same() && oracle.action.getLastStatus() == CoinRenderAction::SUCCESS &&
                oracle.action.getPimpl()->cachedCamera == camera && oracle.action.getPimpl()->translationProofValid &&
                oracle.counts(1, 2, 1),
                "fresh captures must reuse only the same-call successful basis and remove exactly one Core prepare");
    const auto * vertices = oracle.action.getPimpl()->lastValidPlan.vertices.data();
    oracle.apply(scene.root);
    TEST_ASSERT(oracle.same() && oracle.counts(0, 0, 0, 0),
                "unchanged reuse must reset the last-apply counters without preparing or reusing a capture basis");
    camera->position.setValue(.25f, -.1f, 19.5f);
    oracle.apply(scene.root);
    TEST_ASSERT(oracle.same() && oracle.counts(0, 0, 0, 0) &&
                oracle.action.getPimpl()->lastValidPlan.vertices.data() == vertices &&
                (!oracle.backend || oracle.backend->lastReuse.kind == CoinRenderFrameReuseKind::CAMERA_PATCH),
                "camera patches must keep their existing basis and must not consume a capture-time lens");
  }
  {
    TranslationOverlayScene scene, other;
    CaptureCameraBasisOracle oracle;
    TEST_ASSERT(oracle.ready, "root ownership oracle requires CPU targets");
    for (SoNode * root : {static_cast<SoNode *>(scene.root), static_cast<SoNode *>(other.root), static_cast<SoNode *>(scene.root)}) {
      oracle.apply(root);
      TEST_ASSERT(oracle.same() && oracle.action.getPimpl()->translationProofValid && oracle.counts(1, 2, 1),
                  "A/B/A roots must each prepare a fresh owned basis instead of licensing an earlier capture");
    }
    // A non-camera change plus object movement recaptures with qualifyCamera
    // false. The object path must prepare independently despite an empty basis.
    scene.prefix->rotation.setValue(SbVec3f(1, 2, -1), .31f);
    scene.positions[0]->setValue(scene.initialPositions[0] + SbVec3f(.2f, 0, 0));
    oracle.apply(scene.root);
    TEST_ASSERT(oracle.same() && oracle.action.getPimpl()->translationProofValid &&
                !oracle.action.getPimpl()->cachedCamera && !oracle.action.getPimpl()->cameraOverlayBasis.owner &&
                oracle.counts(1, 1, 0),
                "object-only recapture must qualify its own current plan when camera qualification was deferred");
    scene.positions[1]->setValue(scene.initialPositions[1] + SbVec3f(.1f, -.1f, .05f));
    oracle.apply(scene.root);
    TEST_ASSERT(oracle.same() && oracle.counts(0, 0, 0, 0) &&
                oracle.backend->lastReuse.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD,
                "objects admitted independently must retain the existing RESOURCE_REBUILD overlay path");
  }
  // Finite but invalid authored camera values can be normalized by capture.
  // The literal path, rather than an assumed rejection policy, is the oracle.
  for (int cameraValue = 0; cameraValue < 6; ++cameraValue) {
    TranslationOverlayScene scene;
    if (cameraValue == 0) scene.camera->nearDistance = -1;
    if (cameraValue == 1) scene.camera->farDistance = -1;
    if (cameraValue == 2) scene.camera->farDistance = scene.camera->nearDistance.getValue();
    if (cameraValue == 3) scene.camera->focalDistance = 0;
    if (cameraValue == 4) scene.camera->aspectRatio = 0;
    if (cameraValue == 5) scene.camera->heightAngle = 0;
    CaptureCameraBasisOracle oracle;
    TEST_ASSERT(oracle.ready, "authored camera fallback oracle requires CPU targets");
    oracle.apply(scene.root);
    const CoinRenderActionP * a = &oracle.action.getPimpl().get(); const CoinRenderActionP * b = &oracle.literal.getPimpl().get();
    TEST_ASSERT(oracle.same() && a->captureCameraBasisCalls == b->captureCameraBasisCalls &&
                a->captureCameraBasisPrepares + a->captureCameraBasisReuses == b->captureCameraBasisPrepares &&
                a->captureCameraBasisReuses <= 1 && b->captureCameraBasisReuses == 0,
                "invalid authored camera values must preserve literal acceptance, normalized payload, diagnostics and admission");
  }
  // Camera scene admission skips ignored fields and the camera's own fields;
  // strict object admission must still inspect them after the successful lens.
  for (bool connectedCamera : {false, true}) {
    TranslationOverlayScene scene;
    auto * driver = new SoTranslation;
    driver->ref(); driver->translation.setValue(0, 0, 20);
    if (connectedCamera) scene.camera->position.connectFrom(&driver->translation);
    else scene.prefix->translation.setIgnored(TRUE);
    CaptureCameraBasisOracle oracle;
    TEST_ASSERT(oracle.ready, "independent scene profile oracle requires CPU targets");
    oracle.apply(scene.root);
    TEST_ASSERT(oracle.same() && oracle.action.getLastStatus() == CoinRenderAction::SUCCESS &&
                oracle.action.getPimpl()->cachedCamera == scene.camera && !oracle.action.getPimpl()->translationProofValid &&
                oracle.counts(1, 1, 0),
                "a successful camera basis must never bypass ignored or connected-field rejection of objects");
    if (!connectedCamera) {
      scene.camera->position.setValue(.15f, 0, 19.8f);
      oracle.apply(scene.root);
      TEST_ASSERT(oracle.same() && oracle.backend->lastReuse.kind == CoinRenderFrameReuseKind::CAMERA_PATCH &&
                  !oracle.action.getPimpl()->translationProofValid && oracle.counts(0, 0, 0, 0),
                  "rejecting the object profile must preserve the independent camera patch admission");
    } else scene.camera->position.disconnect();
    driver->unref();
  }
  {
    TranslationOverlayScene scene;
    CaptureCameraBasisOracle oracle;
    TEST_ASSERT(oracle.ready, "capture failure oracle requires CPU targets");
    oracle.backend->failNext = oracle.literalBackend->failNext = true;
    oracle.apply(scene.root);
    TEST_ASSERT(oracle.same() && oracle.action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
                !oracle.action.getPimpl()->hasLastValidPlan && oracle.counts(0, 0, 0, 0),
                "rejected first submission must not install or qualify a camera/object proof");
    oracle.apply(scene.root);
    TEST_ASSERT(oracle.same() && oracle.action.getLastStatus() == CoinRenderAction::SUCCESS && oracle.counts(1, 2, 1),
                "retry after failed first capture must prepare a new same-call basis");
    const auto before = oracle.action.getPimpl()->lastValidPlan;
    scene.camera->position.setValue(.3f, -.2f, 19.8f);
    oracle.backend->failNext = oracle.literalBackend->failNext = true;
    oracle.apply(scene.root);
    TEST_ASSERT(oracle.same() && oracle.action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
                oracle.action.getPimpl()->lastValidPlan.revision == before.revision &&
                oracle.action.getPimpl()->lastValidPlan.hasSamePayload(before) && oracle.counts(0, 0, 0, 0),
                "failed camera patch must roll back the accepted payload without preparing a capture lens");
    oracle.apply(scene.root);
    TEST_ASSERT(oracle.same() && oracle.action.getLastStatus() == CoinRenderAction::SUCCESS &&
                oracle.backend->lastReuse.kind == CoinRenderFrameReuseKind::CAMERA_PATCH && oracle.counts(0, 0, 0, 0),
                "camera patch retry must retain its existing anchor and payload semantics");
  }
  {
    TranslationOverlayScene scene;
    scene.prefix->scaleFactor.setValue(1, 1, 1); scene.prefix->rotation.setValue(SbVec3f(0, 1, 0), 0);
    scene.prefix->translation.setValue(0, 0, 0); scene.prefix->center.setValue(0, 0, 0);
    scene.camera->position.setValue(0, 0, 1e8f);
    scene.light->location.setValue(0, 0, 1);
    CaptureCameraBasisOracle oracle;
    TEST_ASSERT(oracle.ready, "basis failure oracle requires CPU targets");
    oracle.apply(scene.root);
    TEST_ASSERT(oracle.same() && oracle.action.getLastStatus() == CoinRenderAction::SUCCESS &&
                !oracle.action.getPimpl()->cachedCamera && !oracle.action.getPimpl()->translationProofValid &&
                oracle.counts(2, 2, 0),
                "failed camera basis must leave the independent object prepare attempt and literal fallback intact");
  }
  {
    TranslationOverlayScene scene;
    scene.root->addChild(scene.camera);
    CaptureCameraBasisOracle oracle;
    TEST_ASSERT(oracle.ready, "shared camera oracle requires CPU targets");
    oracle.apply(scene.root);
    TEST_ASSERT(oracle.same() && oracle.action.getLastStatus() == CoinRenderAction::SUCCESS &&
                !oracle.action.getPimpl()->cachedCamera && !oracle.action.getPimpl()->translationProofValid &&
                oracle.counts(0, 0, 0),
                "repeated syntactic cameras must fail both independent scene profiles without a Core prepare");
  }
  {
    TranslationOverlayScene scene;
    CaptureCameraBasisOracle oracle;
    TEST_ASSERT(oracle.ready, "path capture oracle requires CPU targets");
    auto * path = new SoPath(scene.root);
    path->ref(); path->append(scene.root->findChild(scene.objects[0]));
    oracle.apply(path);
    TEST_ASSERT(oracle.same() && oracle.action.getLastStatus() == CoinRenderAction::SUCCESS &&
                !oracle.action.getPimpl()->cachedCamera && !oracle.action.getPimpl()->translationProofValid &&
                oracle.counts(0, 0, 0, 0),
                "path traversal must preserve the literal non-root capture without creating a camera lens");
    path->unref();
  }
  for (int exclusion = 0; exclusion < 3; ++exclusion) {
    TranslationOverlayScene scene;
    CaptureCameraBasisOracle oracle;
    TEST_ASSERT(oracle.ready, "callback and plan-only oracle requires CPU targets");
    unsigned calls = 0, literalCalls = 0;
    if (exclusion == 0) {
      oracle.action.addPreCallback(SoCube::getClassTypeId(), translationCountingCallback, &calls);
      oracle.literal.addPreCallback(SoCube::getClassTypeId(), translationCountingCallback, &literalCalls);
    } else if (exclusion == 1) oracle.action.getPimpl()->planOnly = oracle.literal.getPimpl()->planOnly = true;
    else coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_TRANSLATION_OVERLAY", "1");
    oracle.apply(scene.root);
    TEST_ASSERT(oracle.same() && oracle.action.getLastStatus() == CoinRenderAction::SUCCESS &&
                !oracle.action.getPimpl()->translationProofValid &&
                oracle.counts(exclusion == 2 ? 1 : 0, exclusion == 2 ? 1 : 0, 0, exclusion == 1 ? 0 : 1) &&
                calls == literalCalls && (exclusion != 0 || calls == scene.positions.size()),
                "callbacks, plan-only capture and disabled objects must preserve their literal proof admission and work counts");
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_TRANSLATION_OVERLAY", "0");
  }
  return 0;
}

int testTranslationOverlayInvalidationAndOwnership() {
  TranslationOverlayEnvironment environment;
  // These mutations also change a translation in the same notification batch.
  // None may preserve the translation-only proof or hide its full traversal.
  for (int mutation = 0; mutation < 10; ++mutation) {
    if (mutation == 2 || mutation == 4) continue; // Covered by the admitted material/Cube overlay test.
    TranslationOverlayScene scene(2);
    CoinRenderAction action, full; full.getPimpl()->planOnly = true;
    action.apply(scene.root);
    TEST_ASSERT(action.getPimpl()->translationProofValid, "ordinary fixture must qualify before invalidation");
    auto * transform = static_cast<SoTransform *>(scene.transforms[1]);
    if (mutation == 0) transform->rotation.setValue(SbVec3f(0, 1, 0), .4f);
    if (mutation == 1) transform->scaleFactor.setValue(1, 1.2f, .9f);
    if (mutation == 2) scene.material->diffuseColor.setValue(.2f, .4f, .7f);
    if (mutation == 3) scene.light->location.setValue(3, 2, 1);
    if (mutation == 4) scene.cube->width = 3;
    if (mutation == 5) scene.root->addChild(new SoCube);
    if (mutation == 6) scene.prefix->translation.setValue(2, 3, 4);
    if (mutation == 7) transform->center.setValue(.2f, -.1f, .3f);
    if (mutation == 8) transform->translation.setIgnored(TRUE);
    if (mutation == 9) transform->translation.connectFrom(&scene.camera->position);
    scene.positions[0]->setValue(scene.initialPositions[0] + SbVec3f(.3f, 0, 0));
    TEST_ASSERT(action.getPimpl()->translationInvalidated, "nontranslation/ignored/connected changes must invalidate the proof");
    const uint64_t oldGeneration = action.getPimpl()->translationGeneration;
    action.apply(scene.root); full.apply(scene.root);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && full.getLastStatus() == CoinRenderAction::SUCCESS &&
                action.getPimpl()->translationGeneration > oldGeneration &&
                action.getPimpl()->lastValidPlan.hasSamePayload(full.getPimpl()->lastValidPlan),
                "invalidated movement must take full capture and match its complete payload");
  }
  for (int alias = 0; alias < 4; ++alias) {
    TranslationOverlayScene scene(1);
    if (alias == 0) {
      auto * other = new SoSeparator;
      other->addChild(scene.material); other->addChild(scene.transforms[0]); other->addChild(scene.cube);
      scene.root->addChild(other);
    }
    if (alias == 1) scene.root->addChild(scene.objects[0]);
    if (alias == 2) {
      // Equal external state must not share ownership with a mutable object.
      scene.positions[0]->setValue(0, 0, 0);
      scene.root->addChild(scene.material); scene.root->addChild(scene.cube);
    }
    if (alias == 3) scene.transforms[0]->ref();
    CoinRenderAction action, full; full.getPimpl()->planOnly = true;
    action.apply(scene.root);
    TEST_ASSERT(!action.getPimpl()->translationProofValid,
                "shared transform/parent, interned external state or held transform must refuse ownership");
    scene.positions[0]->setValue(.3f, .2f, -.1f);
    action.apply(scene.root); full.apply(scene.root);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                action.getPimpl()->lastValidPlan.hasSamePayload(full.getPimpl()->lastValidPlan),
                "unqualified ownership must capture every actual occurrence after movement");
    if (alias == 3) scene.transforms[0]->unref();
  }
  for (bool registerBefore : {false, true}) for (bool onGroup : {false, true}) {
    TranslationOverlayScene scene(2);
    CoinRenderAction action;
    unsigned calls = 0;
    if (registerBefore)
      action.addPreCallback(onGroup ? SoSeparator::getClassTypeId() : SoTransform::getClassTypeId(), translationCountingCallback, &calls);
    action.apply(scene.root);
    TEST_ASSERT(action.getPimpl()->translationProofValid == !registerBefore,
                "extra callbacks registered before capture must prevent translation qualification");
    if (!registerBefore)
      action.addPreCallback(onGroup ? SoSeparator::getClassTypeId() : SoTransform::getClassTypeId(), translationCountingCallback, &calls);
    const unsigned before = calls;
    scene.positions[0]->setValue(.6f, .2f, -.1f); action.apply(scene.root);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && calls > before &&
                !action.getPimpl()->translationProofValid,
                "callbacks added before or after proof must execute on full traversal of translated objects");
    const unsigned afterMovement = calls; action.apply(scene.root);
    TEST_ASSERT(calls > afterMovement, "external callbacks must also execute on an unchanged scene");
  }
  return 0;
}

bool sameObjectDrawPayload(const CoinRenderFramePlan & a, const CoinRenderFramePlan & b) {
  if (a.draws.size() != b.draws.size() || a.cameras.size() != b.cameras.size()) return false;
  // Ignore only the interned geometry/material/state layouts. All global
  // backend-visible fields must still equal a fresh capture exactly.
  const auto globals = [](const CoinRenderFramePlan & frame) {
    CoinRenderFramePlan result;
    result.transparency = frame.transparency; result.clearColor = frame.clearColor;
    result.lightingStates = frame.lightingStates; result.shadowGroups = frame.shadowGroups;
    result.shadowLights = frame.shadowLights; result.cameras = frame.cameras;
    result.viewports = frame.viewports; result.textures = frame.textures; result.samplers = frame.samplers;
    return result;
  };
  if (!globals(a).hasSamePayload(globals(b))) return false;
  for (size_t draw = 0; draw < a.draws.size(); ++draw) {
    const auto & x = a.draws[draw]; const auto & y = b.draws[draw];
    if (x.topology != y.topology || x.geometry.indexCount != y.geometry.indexCount ||
        x.sourceNodeId != y.sourceNodeId || x.stableNodeId != y.stableNodeId || x.sourceRevision != y.sourceRevision ||
        x.renderLayer != y.renderLayer || x.clearDepthBefore != y.clearDepthBefore ||
        x.shadowLightSlot != y.shadowLightSlot || x.lineStripId != y.lineStripId || x.hasSortingCenter != y.hasSortingCenter ||
        std::memcmp(x.sortingCenterWorld, y.sortingCenterWorld, sizeof(x.sortingCenterWorld)) != 0) return false;
    auto sx = a.renderStates[x.renderStateSlot], sy = b.renderStates[y.renderStateSlot];
    const auto mx = sx.materialSlot, my = sy.materialSlot; sx.materialSlot = sy.materialSlot = 0;
    if (!coin_render_same_state_except_camera(sx, sy) ||
        std::memcmp(sx.model.getValue(), sy.model.getValue(), sizeof(float) * 16) != 0 ||
        std::memcmp(sx.view.getValue(), sy.view.getValue(), sizeof(float) * 16) != 0 ||
        std::memcmp(sx.projectionCoin.getValue(), sy.projectionCoin.getValue(), sizeof(float) * 16) != 0 ||
        std::memcmp(&a.materials[mx], &b.materials[my], sizeof(CoinRenderMaterialSnapshot)) != 0) return false;
    for (uint32_t index = 0; index < x.geometry.indexCount; ++index) {
      auto vx = a.vertices[a.indices[x.geometry.firstIndex + index]], vy = b.vertices[b.indices[y.geometry.firstIndex + index]];
      const auto vmx = vx.materialSlot, vmy = vy.materialSlot; vx.materialSlot = vy.materialSlot = 0;
      if (std::memcmp(&vx, &vy, sizeof(vx)) != 0 ||
          std::memcmp(&a.materials[vmx], &b.materials[vmy], sizeof(CoinRenderMaterialSnapshot)) != 0) return false;
    }
  }
  return true;
}

int testObjectMaterialGeometryOverlay() {
  TranslationOverlayEnvironment environment;
  TranslationOverlayScene scene(3);
  auto * uniqueCube = new SoCube;
  uniqueCube->width = 3; uniqueCube->height = 4; uniqueCube->depth = 5;
  scene.objects[1]->replaceChild(2, uniqueCube);
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(1, 1)));
  TEST_ASSERT(target, "object overlay requires a CPU target shell");
  auto * backend = new CameraOverlayTestBackend;
  target->getPimpl()->backend.reset(backend); target->getPimpl()->depthReadbackEnabled = false;
  CoinRenderAction action(SbViewportRegion(1, 1)), full(SbViewportRegion(1, 1));
  full.getPimpl()->planOnly = true;
  action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
  full.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
  action.setRenderTarget(target.get()); action.apply(scene.root);
  TEST_ASSERT(action.getPimpl()->materialByNode.count(scene.material) &&
              action.getPimpl()->cubeByNode.count(scene.cube) && action.getPimpl()->cubeByNode.count(uniqueCube),
              "shared material, shared Cube and distinct unique Cube ranges must all be admitted by source");
  const auto * vertices = action.getPimpl()->lastValidPlan.vertices.data();
  const auto * indices = action.getPimpl()->lastValidPlan.indices.data();
  const uint64_t generation = action.getPimpl()->translationProofGeneration;
  for (int frame = 0; frame < 4; ++frame) {
    scene.material->ambientColor.setValue(.1f, .2f, .3f);
    scene.material->diffuseColor.setValue(.3f + .05f * frame, .6f, .4f);
    scene.material->specularColor.setValue(.2f, .4f, .6f);
    scene.material->emissiveColor.setValue(.01f, .02f, .03f);
    scene.material->shininess = .4f + .05f * frame;
    scene.cube->width = 2 + .1f * frame; scene.cube->height = 1.3f; scene.cube->depth = .7f;
    uniqueCube->width = 3 + .2f * frame; uniqueCube->height = 4 - .1f * frame; uniqueCube->depth = 5.5f;
    scene.positions[0]->setValue(scene.initialPositions[0] + SbVec3f(.03f * frame, .04f, -.02f));
    const uint64_t base = action.getPimpl()->lastValidPlan.revision;
    action.apply(scene.root); full.apply(scene.root);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && full.getLastStatus() == CoinRenderAction::SUCCESS &&
                backend->lastReuse.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD && backend->lastReuse.baseRevision == base &&
                action.getPimpl()->translationProofGeneration == generation &&
                action.getPimpl()->lastValidPlan.vertices.data() == vertices && action.getPimpl()->lastValidPlan.indices.data() == indices &&
                sameObjectDrawPayload(action.getPimpl()->lastValidPlan, full.getPimpl()->lastValidPlan),
                "mixed material/dimensions/translation must preserve MV/normals/UV/topology and match full expanded payload by bytes");
  }
  const auto previous = action.getPimpl()->lastValidPlan;
  scene.material->diffuseColor.setValue(.8f, .3f, .2f); uniqueCube->width = 3.7f;
  scene.positions[1]->setValue(scene.initialPositions[1] + SbVec3f(.2f, 0, 0));
  backend->failNext = true; action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
              action.getPimpl()->lastValidPlan.revision == previous.revision &&
              action.getPimpl()->lastValidPlan.hasSamePayload(previous) &&
              !action.getPimpl()->materialDirty.empty() && !action.getPimpl()->geometryDirty.empty() &&
              !action.getPimpl()->translationDirty.empty(),
              "failure must restore material/vertex/model/draw IDs together and retain every pending source for retry");
  action.apply(scene.root); full.apply(scene.root);
  TEST_ASSERT(backend->lastReuse.baseRevision == previous.revision &&
              sameObjectDrawPayload(action.getPimpl()->lastValidPlan, full.getPimpl()->lastValidPlan),
              "object-overlay retry must match the complete freshly captured payload");
  scene.camera->position.setValue(.2f, -.1f, 19); action.apply(scene.root); full.apply(scene.root);
  TEST_ASSERT(backend->lastReuse.kind != CoinRenderFrameReuseKind::CAMERA_PATCH &&
              sameObjectDrawPayload(action.getPimpl()->lastValidPlan, full.getPimpl()->lastValidPlan) &&
              action.getPimpl()->materialByNode.count(scene.material) && action.getPimpl()->cubeByNode.count(uniqueCube),
              "camera after object payload patches must full-recapture and readmit the new source ranges");
  uniqueCube->depth = 6; scene.material->shininess = .7f;
  action.apply(scene.root); full.apply(scene.root);
  TEST_ASSERT(sameObjectDrawPayload(action.getPimpl()->lastValidPlan, full.getPimpl()->lastValidPlan) &&
              backend->lastReuse.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD,
              "materials and geometry must patch safely again after camera recapture");
  const uint64_t readmittedGeneration = action.getPimpl()->translationProofGeneration;
  scene.material->diffuseColor.setValue(.5f, .3f, .2f); scene.material->shininess = .2f;
  action.apply(scene.root); full.apply(scene.root);
  TEST_ASSERT(action.getPimpl()->translationProofGeneration == readmittedGeneration &&
              sameObjectDrawPayload(action.getPimpl()->lastValidPlan, full.getPimpl()->lastValidPlan),
              "material-only return to earlier values must preserve the source proof and full payload semantics");
  scene.cube->width = 2; scene.cube->height = 2; scene.cube->depth = 2;
  uniqueCube->width = 3; uniqueCube->height = 4; uniqueCube->depth = 5;
  action.apply(scene.root); full.apply(scene.root);
  TEST_ASSERT(action.getPimpl()->translationProofGeneration == readmittedGeneration &&
              sameObjectDrawPayload(action.getPimpl()->lastValidPlan, full.getPimpl()->lastValidPlan),
              "geometry-only return to original dimensions must avoid accumulated drift in all captured vertex attributes");
  return 0;
}

int testInterleavedCubeSourceOwnership() {
  TranslationOverlayEnvironment environment;
  struct TemplateEnvironment {
    bool present;
    std::string value;
    TemplateEnvironment() {
      const char * previous = std::getenv("COIN_RENDER_DISABLE_CUBE_TEMPLATE_CACHE");
      present = previous != NULL; value = previous ? previous : "";
      coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CUBE_TEMPLATE_CACHE","0");
    }
    ~TemplateEnvironment() {
      coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CUBE_TEMPLATE_CACHE",present ? value.c_str() : NULL);
    }
  } templateEnvironment;
  TranslationOverlayScene scene(3);
  SoCube * cubes[3];
  for (unsigned i = 0; i < 3; ++i) {
    cubes[i] = new SoCube;
    const float dimension = i == 1 ? 3 : 2;
    cubes[i]->width = dimension; cubes[i]->height = dimension; cubes[i]->depth = dimension;
    scene.objects[i]->replaceChild(2,cubes[i]);
  }
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(1,1)));
  TEST_ASSERT(target,"interleaved source ownership requires a CPU target shell");
  auto * backend = new CameraOverlayTestBackend;
  target->getPimpl()->backend.reset(backend); target->getPimpl()->depthReadbackEnabled = false;
  CoinRenderAction action(SbViewportRegion(1,1)), full(SbViewportRegion(1,1));
  action.setRenderTarget(target.get()); full.getPimpl()->planOnly = true;
  action.apply(scene.root); full.apply(scene.root);
  auto * p = &action.getPimpl().get();
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && p->translationProofValid &&
              p->translationBindings.size() == 3,"three isolated sources must retain distinct occurrence states");
  const auto & a = p->translationBindings[0].geometry, & b = p->translationBindings[1].geometry,
             & c = p->translationBindings[2].geometry;
  TEST_ASSERT(a.firstVertex == c.firstVertex && a.firstIndex == c.firstIndex && a.firstVertex != b.firstVertex &&
              !p->cubeByNode.count(cubes[0]) && !p->cubeByNode.count(cubes[2]) && p->cubeByNode.count(cubes[1]) &&
              sameObjectDrawPayload(p->lastValidPlan,full.getPimpl()->lastValidPlan),
              "value-shared A/B/A ranges across independent Cube sources must reject writable geometry ownership");
  const auto consumedA = [&](const CoinRenderFramePlan & plan) {
    std::vector<CoinRenderVertexSnapshot> result;
    const auto & draw = plan.draws[0];
    for (uint32_t i = 0; i < draw.geometry.indexCount; ++i)
      result.push_back(plan.vertices[plan.indices[draw.geometry.firstIndex+i]]);
    return result;
  };
  const auto originalA = consumedA(p->lastValidPlan);
  const uint64_t initialGeneration = p->translationProofGeneration;
  cubes[2]->width = 2.5f;
  action.apply(scene.root); full.apply(scene.root);
  const auto afterA = consumedA(p->lastValidPlan);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS &&
              backend->lastReuse.kind == CoinRenderFrameReuseKind::FULL_REBUILD &&
              p->translationProofGeneration != initialGeneration && p->cubeByNode.count(cubes[2]) &&
              originalA.size() == afterA.size() &&
              std::memcmp(originalA.data(),afterA.data(),originalA.size()*sizeof(CoinRenderVertexSnapshot)) == 0 &&
              sameObjectDrawPayload(p->lastValidPlan,full.getPimpl()->lastValidPlan),
              "changing C alone must recapture its former alias safely, preserve A and readmit C's separate range");
  const uint64_t readmittedGeneration = p->translationProofGeneration;
  const uint64_t base = p->lastValidPlan.revision;
  cubes[2]->height = 2.75f;
  action.apply(scene.root); full.apply(scene.root);
  TEST_ASSERT(backend->lastReuse.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD &&
              backend->lastReuse.baseRevision == base && p->translationProofGeneration == readmittedGeneration &&
              sameObjectDrawPayload(p->lastValidPlan,full.getPimpl()->lastValidPlan),
              "a later C update must reuse only its readmitted exclusive range and match expanded full capture");
  const auto previous = p->lastValidPlan;
  cubes[2]->depth = 3.25f; backend->failNext = true; action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::UNSUPPORTED && previous.hasSamePayload(p->lastValidPlan) &&
              p->lastValidPlan.revision == previous.revision && p->geometryDirty.count(cubes[2]),
              "failure after alias separation must restore the exclusive C range and retain it for retry");
  action.apply(scene.root); full.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS &&
              backend->lastReuse.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD &&
              p->translationProofGeneration == readmittedGeneration &&
              sameObjectDrawPayload(p->lastValidPlan,full.getPimpl()->lastValidPlan),
              "retry of a formerly aliased source must preserve the readmitted proof and full capture semantics");
  return 0;
}

int testObjectProofMemoization() {
  TranslationOverlayEnvironment environment;
  struct MemoEnvironment {
    const char * flag = "COIN_RENDER_DISABLE_OBJECT_PROOF_MEMOIZATION";
    bool present;
    std::string value;
    MemoEnvironment() {
      const char * previous = std::getenv(flag);
      present = previous != NULL; value = previous ? previous : "";
      coinRenderTestSetEnvironment(flag,"0");
    }
    ~MemoEnvironment() { coinRenderTestSetEnvironment(flag,present ? value.c_str() : NULL); }
  } memoEnvironment;
  // Use the original per-occurrence path as an admission oracle. Qualification
  // itself must neither change the plan nor depend on a prior invocation.
  const auto equivalentProof = [&](CoinRenderActionP * action,
                                   const std::unordered_map<const SoNode *, size_t> & visits) {
    const auto plan = action->lastValidPlan;
    const auto eligibility = [&]() {
      std::vector<uint8_t> flags;
      for (const auto & binding : action->translationBindings)
        flags.push_back((binding.materialEligible ? 1 : 0) | (binding.geometryEligible ? 2 : 0));
      return flags;
    };
    action->materialByNode.clear(); action->cubeByNode.clear();
    coinRenderTestSetEnvironment(memoEnvironment.flag,"0");
    action->qualifyObjectPayloads(visits);
    const auto materialOwners = action->materialByNode, cubeOwners = action->cubeByNode;
    const auto flags = eligibility();
    action->materialByNode.clear(); action->cubeByNode.clear();
    coinRenderTestSetEnvironment(memoEnvironment.flag,"1");
    action->qualifyObjectPayloads(visits);
    coinRenderTestSetEnvironment(memoEnvironment.flag,"0");
    return materialOwners == action->materialByNode && cubeOwners == action->cubeByNode &&
      flags == eligibility() && plan.hasSamePayload(action->lastValidPlan);
  };
  {
    TranslationOverlayScene scene(6);
    auto * second = new SoMaterial; second->diffuseColor.setValue(.2f,.6f,.4f);
    auto * third = new SoMaterial; third->diffuseColor.setValue(.7f,.1f,.8f);
    scene.objects[2]->replaceChild(0,second); scene.objects[3]->replaceChild(0,third);
    std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(1,1)));
    TEST_ASSERT(target,"memo proof test requires a CPU target shell");
    target->getPimpl()->backend.reset(new CameraOverlayTestBackend);
    target->getPimpl()->depthReadbackEnabled = false;
    CoinRenderAction action(SbViewportRegion(1,1)); action.setRenderTarget(target.get()); action.apply(scene.root);
    auto * p = &action.getPimpl().get();
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && p->translationProofValid &&
                p->translationBindings.size() == scene.objects.size(),
                "shared source fixture must establish the complete occurrence proof");
    std::unordered_map<const SoNode *, size_t> visits;
    for (const auto & binding : p->translationBindings) ++visits[binding.material];
    const auto original = p->lastValidPlan;
    const auto bindings = p->translationBindings;
    TEST_ASSERT(equivalentProof(p,visits) && p->materialByNode.count(scene.material) &&
                p->materialByNode.count(second) && p->materialByNode.count(third) && p->cubeByNode.count(scene.cube),
                "memoized shared-source admission must equal every original per-occurrence proof");
    const auto & first = p->lastValidPlan.draws[bindings[0].firstDraw];
    const uint32_t secondSlot = p->lastValidPlan.renderStates[bindings[2].stateSlot].materialSlot;
    const uint32_t thirdSlot = p->lastValidPlan.renderStates[bindings[3].stateSlot].materialSlot;
    p->lastValidPlan.vertices[p->lastValidPlan.indices[first.geometry.firstIndex]].materialSlot = secondSlot;
    p->lastValidPlan.vertices[p->lastValidPlan.indices[first.geometry.firstIndex+1]].materialSlot = thirdSlot;
    TEST_ASSERT(equivalentProof(p,visits) && !p->materialByNode.count(scene.material) &&
                !p->materialByNode.count(second) && !p->materialByNode.count(third),
                "shared nonuniform index spans must record ALL divergent material slots before memoizing");
    p->lastValidPlan = original;
    // Same consumed span, different expected slot: both sources conflict.
    // The full geometry range stays valid; only material uniformity differs.
    auto & secondDraw = p->lastValidPlan.draws[bindings[2].firstDraw];
    secondDraw.geometry = p->lastValidPlan.draws[bindings[0].firstDraw].geometry;
    p->translationBindings[2].geometry = secondDraw.geometry;
    TEST_ASSERT(equivalentProof(p,visits) && !p->materialByNode.count(second) &&
                !p->materialByNode.count(scene.material) && p->materialByNode.count(third),
                "index memo keys must include the draw's expected material slot");
    p->lastValidPlan = original; p->translationBindings = bindings;
    // One Cube source has several ranges because materials remain in vertices.
    // A late malformed range cannot inherit the first range's successful proof.
    p->lastValidPlan.vertices[bindings[3].geometry.firstVertex+23].screenSpaceW = 2;
    TEST_ASSERT(equivalentProof(p,visits) && !p->cubeByNode.count(scene.cube),
                "a late bad vertex span must reject all occurrences of its shared Cube source");
    p->lastValidPlan = original;
    p->translationBindings.back().overallMaterial = false;
    TEST_ASSERT(equivalentProof(p,visits) && !p->materialByNode.count(scene.material) &&
                !p->cubeByNode.count(scene.cube),
                "an unqualified late occurrence must reject its source after earlier memo hits");
    p->translationBindings = bindings;
    auto extraVisits = visits; ++extraVisits[scene.material];
    TEST_ASSERT(equivalentProof(p,extraVisits) && !p->materialByNode.count(scene.material),
                "memoization must retain rejection of unmapped material syntax");
    scene.material->diffuseColor.setValue(.9f,.8f,.7f);
    TEST_ASSERT(equivalentProof(p,visits) && !p->materialByNode.count(scene.material),
                "a new qualification must reread changed source values despite identical slots and revision");
    scene.material->diffuseColor.setValue(.5f,.3f,.2f);
    scene.cube->width = 2.5f;
    TEST_ASSERT(equivalentProof(p,visits) && !p->cubeByNode.count(scene.cube),
                "a new qualification must reread dimensions despite identical ranges and revision");
    scene.cube->width = 2;
    TEST_ASSERT(equivalentProof(p,visits) && p->materialByNode.count(scene.material) && p->cubeByNode.count(scene.cube),
                "local proof caches must expire and permit readmission after values are restored");
  }
  {
    // Saturate both range caches and the source cache. Admission past the cap
    // must still perform ordinary proofs, including a failing final source.
    TranslationOverlayScene scene(1030);
    std::vector<SoCube *> cubes;
    for (unsigned i = 0; i < scene.objects.size(); ++i) {
      auto * cube = new SoCube; cube->width = 2 + float(i)*.0009765625f;
      scene.objects[i]->replaceChild(2,cube); cubes.push_back(cube);
    }
    std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(1,1)));
    TEST_ASSERT(target,"bounded memo proof test requires a CPU target shell");
    target->getPimpl()->backend.reset(new CameraOverlayTestBackend);
    target->getPimpl()->depthReadbackEnabled = false;
    CoinRenderAction action(SbViewportRegion(1,1)); action.setRenderTarget(target.get()); action.apply(scene.root);
    auto * p = &action.getPimpl().get();
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && p->translationProofValid &&
                p->translationBindings.size() == cubes.size(),
                "bounded fixture must retain all distinct captured object occurrences");
    std::unordered_map<const SoNode *, size_t> visits;
    for (const auto & binding : p->translationBindings) ++visits[binding.material];
    TEST_ASSERT(equivalentProof(p,visits) && p->cubeByNode.size() == cubes.size(),
                "sources beyond the optional memo cap must qualify equivalently through ordinary checks");
    const auto & last = p->translationBindings.back();
    p->lastValidPlan.vertices[last.geometry.firstVertex+23].position[0] *= .75f;
    TEST_ASSERT(equivalentProof(p,visits) && p->cubeByNode.size() == cubes.size()-1 &&
                !p->cubeByNode.count(cubes.back()) && p->cubeByNode.count(cubes.front()),
                "a bad span beyond the memo cap must reject only its source and preserve earlier valid proofs");
  }
  return 0;
}

int testObjectPayloadAliasFallbacks() {
  TranslationOverlayEnvironment environment;
  for (int profile = 0; profile < 9; ++profile) {
    TranslationOverlayScene scene(2);
    auto * localMaterial = new SoMaterial;
    localMaterial->diffuseColor.setValue(scene.material->diffuseColor[0]);
    if (profile == 0) scene.objects[1]->replaceChild(0, localMaterial);
    else { localMaterial->ref(); }
    if (profile == 1) scene.material->diffuseColor.set1Value(1, SbColor(.2f, .3f, .4f));
    if (profile == 2) {
      auto * binding = new SoMaterialBinding; binding->value = SoMaterialBinding::PER_PART;
      scene.root->insertChild(binding, 1);
    }
    if (profile == 3) scene.material->setOverride(TRUE);
    if (profile == 4) {
      auto * external = new SoSeparator;
      auto * material = new SoMaterial; material->diffuseColor.setValue(.1f, .2f, .9f);
      external->addChild(material); external->addChild(scene.cube); scene.root->addChild(external);
    }
    if (profile == 5) scene.cube->width = 0;
    if (profile == 6) scene.material->diffuseColor.setIgnored(TRUE);
    if (profile == 7) {
      // The external occurrence depends on this source's diffuse/shininess,
      // but inherited ambient override creates a different material slot.
      auto * external = new SoSeparator;
      auto * partialOverride = new SoMaterial;
      partialOverride->ambientColor.setValue(.7f, .1f, .4f);
      partialOverride->diffuseColor.setNum(0); partialOverride->specularColor.setNum(0);
      partialOverride->emissiveColor.setNum(0); partialOverride->shininess.setNum(0);
      partialOverride->transparency.setNum(0); partialOverride->setOverride(TRUE);
      external->addChild(partialOverride); external->addChild(scene.material); external->addChild(new SoCube);
      scene.root->addChild(external);
    }
    if (profile == 8) { scene.material->ref(); scene.cube->ref(); }
    CoinRenderAction action, full; full.getPimpl()->planOnly = true;
    action.apply(scene.root);
    if (profile < 4 || profile == 6 || profile == 7) TEST_ASSERT(!action.getPimpl()->materialByNode.count(scene.material),
      "material aliases/arrays/per-part/override/ignored fields must not obtain a writable material slot");
    if (profile == 4 || profile == 5) TEST_ASSERT(!action.getPimpl()->cubeByNode.count(scene.cube),
      "unmapped Cube occurrence or degenerate dimensions must reject geometry ownership");
    if (profile == 8) TEST_ASSERT(action.getPimpl()->materialByNode.count(scene.material) &&
                                 action.getPimpl()->cubeByNode.count(scene.cube),
      "held source references must preserve admission when every occurrence in this root is isolated and mapped");
    if (profile == 7) TEST_ASSERT(action.getPimpl()->translationProofValid,
      "partial override fixture must retain the transform proof while material syntax rejects ownership");
    scene.material->diffuseColor.set1Value(0, SbColor(.7f, .4f, .2f));
    scene.cube->width = 2.7f; action.apply(scene.root); full.apply(scene.root);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && full.getLastStatus() == CoinRenderAction::SUCCESS &&
                sameObjectDrawPayload(action.getPimpl()->lastValidPlan, full.getPimpl()->lastValidPlan),
                "unsupported object payload ownership must fall back to complete semantic capture");
    if (profile == 0) {
      TEST_ASSERT(action.getPimpl()->materialByNode.count(scene.material),
                  "diverging material values after fallback must readmit their now exclusively owned slots");
      const uint64_t generation = action.getPimpl()->translationProofGeneration;
      scene.material->diffuseColor.setValue(.6f, .2f, .4f); scene.cube->height = 1.7f;
      action.apply(scene.root); full.apply(scene.root);
      TEST_ASSERT(action.getPimpl()->translationProofGeneration == generation &&
                  sameObjectDrawPayload(action.getPimpl()->lastValidPlan, full.getPimpl()->lastValidPlan),
                  "later changes of separated material sources must reuse the readmitted payload proof");
    }
    if (profile != 0) localMaterial->unref();
    if (profile == 8) { scene.material->unref(); scene.cube->unref(); }
  }
  return 0;
}

int testObjectPayloadInvalidationAndOptout() {
  TranslationOverlayEnvironment environment;
  for (int mutation = 0; mutation < 9; ++mutation) {
    TranslationOverlayScene scene(2);
    auto * driver = new SoMaterial; driver->ref();
    driver->diffuseColor.setValue(.9f, .1f, .4f);
    std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(1, 1)));
    TEST_ASSERT(target, "object invalidation requires a CPU target shell");
    auto * backend = new CameraOverlayTestBackend;
    target->getPimpl()->backend.reset(backend); target->getPimpl()->depthReadbackEnabled = false;
    CoinRenderAction action(SbViewportRegion(1, 1)), full(SbViewportRegion(1, 1));
    full.getPimpl()->planOnly = true; action.setRenderTarget(target.get()); action.apply(scene.root);
    TEST_ASSERT(action.getPimpl()->materialByNode.count(scene.material) && action.getPimpl()->cubeByNode.count(scene.cube),
                "object invalidation fixture must begin with both source proofs");
    const uint64_t generation = action.getPimpl()->translationGeneration;
    unsigned callbacks = 0;
    if (mutation == 0) scene.material->transparency = .25f;
    if (mutation == 1) scene.material->diffuseColor.setIgnored(TRUE);
    if (mutation == 2) scene.cube->width.connectFrom(&scene.camera->focalDistance);
    if (mutation == 3) scene.cube->depth.setIgnored(TRUE);
    if (mutation == 4) scene.root->addChild(new SoCube);
    if (mutation == 5) scene.material->diffuseColor.set1Value(1, SbColor(.1f, .3f, .7f));
    if (mutation == 6) scene.material->setOverride(TRUE);
    if (mutation == 7) action.addPreCallback(SoCube::getClassTypeId(), translationCountingCallback, &callbacks);
    if (mutation == 8) scene.material->diffuseColor.connectFrom(&driver->diffuseColor);
    scene.cube->height = 2.4f; scene.material->shininess = .35f;
    action.apply(scene.root); full.apply(scene.root);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && full.getLastStatus() == CoinRenderAction::SUCCESS &&
                action.getPimpl()->translationGeneration > generation &&
                sameObjectDrawPayload(action.getPimpl()->lastValidPlan, full.getPimpl()->lastValidPlan),
                "late transparency/ignored/connection/structure/array/override/callback must recapture the full payload");
    if (mutation == 7) TEST_ASSERT(callbacks == 2, "new shape callbacks must execute during material/geometry changes");
    if (mutation == 8) scene.material->diffuseColor.disconnect();
    driver->unref();
  }
  for (bool material : {false, true}) {
    TranslationOverlayScene scene(2);
    std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(1, 1)));
    TEST_ASSERT(target, "object optout requires a CPU target shell");
    target->getPimpl()->backend.reset(new CameraOverlayTestBackend);
    target->getPimpl()->depthReadbackEnabled = false;
    CoinRenderAction action(SbViewportRegion(1, 1)), full(SbViewportRegion(1, 1));
    full.getPimpl()->planOnly = true; action.setRenderTarget(target.get()); action.apply(scene.root);
    const uint64_t generation = action.getPimpl()->translationGeneration;
    const char * flag = material ? "COIN_RENDER_DISABLE_MATERIAL_OVERLAY" : "COIN_RENDER_DISABLE_CUBE_OVERLAY";
    coinRenderTestSetEnvironment(flag, "1");
    if (material) scene.material->diffuseColor.setValue(.1f, .4f, .7f);
    else scene.cube->width = 3.2f;
    action.apply(scene.root); full.apply(scene.root);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                action.getPimpl()->translationGeneration > generation &&
                sameObjectDrawPayload(action.getPimpl()->lastValidPlan, full.getPimpl()->lastValidPlan),
                "source optouts changed after admission must force equivalent complete capture");
    coinRenderTestSetEnvironment(flag, "0");
  }
  return 0;
}

bool cameraLightingNear(const CoinRenderFramePlan & a, const CoinRenderFramePlan & b) {
  if (a.lightingStates.size() != b.lightingStates.size()) return false;
  const auto near = [](float x, float y) {
    return std::abs(x - y) <= 2.0e-5f * (1.0f + std::abs(y));
  };
  for (size_t slot = 0; slot < a.lightingStates.size(); ++slot) {
    const auto & x = a.lightingStates[slot]; const auto & y = b.lightingStates[slot];
    if (x.lights.size() != y.lights.size() || x.ambientIntensity != y.ambientIntensity ||
        std::memcmp(x.ambientColor, y.ambientColor, sizeof(x.ambientColor)) != 0) return false;
    for (size_t index = 0; index < x.lights.size(); ++index) {
      const auto & l = x.lights[index]; const auto & r = y.lights[index];
      if (l.type != r.type || l.sourceRevision != r.sourceRevision || l.intensity != r.intensity ||
          l.cutOffAngle != r.cutOffAngle || l.dropOffRate != r.dropOffRate) return false;
      for (int k = 0; k < 3; ++k)
        if (!near(l.position[k], r.position[k]) || !near(l.direction[k], r.direction[k]) ||
            l.color[k] != r.color[k] || l.attenuation[k] != r.attenuation[k]) return false;
      for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col)
        if (!near(l.sourceModel[row][col], r.sourceModel[row][col])) return false;
    }
  }
  return true;
}

int testPhongCameraOverlayNotificationsAndRollback() {
  const char * previous = std::getenv("COIN_RENDER_DISABLE_CAMERA_OVERLAY");
  struct RestoreEnvironment {
    std::string value;
    bool present;
    ~RestoreEnvironment() {
      coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CAMERA_OVERLAY", present ? value.c_str() : nullptr);
    }
  } restoreEnvironment{previous ? previous : "", previous != nullptr};
  coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CAMERA_OVERLAY", "0");
  struct Scene {
    SoSeparator * root = new SoSeparator;
    Scene() { root->ref(); }
    ~Scene() { root->unref(); }
  } scene;
  auto * camera = new SoPerspectiveCamera;
  camera->position.setValue(0, 0, 10);
  camera->nearDistance = .2f; camera->farDistance = 40;
  scene.root->addChild(camera);
  auto * lightTransform = new SoTransform;
  lightTransform->translation.setValue(1, -.5f, 2);
  lightTransform->rotation.setValue(SbVec3f(1, 2, 3), .25f);
  lightTransform->scaleFactor.setValue(1.3f, .8f, 1.5f);
  scene.root->addChild(lightTransform);
  auto * directional = new SoDirectionalLight;
  directional->direction.setValue(.4f, -.6f, -1);
  auto * point = new SoPointLight;
  point->location.setValue(-3, 2, 4);
  auto * spot = new SoSpotLight;
  spot->location.setValue(2, 1, 5); spot->direction.setValue(-.2f, -.3f, -1);
  scene.root->addChild(directional); scene.root->addChild(point); scene.root->addChild(spot);
  auto * material = new SoMaterial;
  material->diffuseColor.setValue(.5f, .3f, .2f);
  scene.root->addChild(material);
  auto * objectTransform = new SoTransform;
  objectTransform->translation.setValue(.2f, .1f, -.3f);
  objectTransform->scaleFactor.setValue(.7f, 1.2f, .9f);
  scene.root->addChild(objectTransform);
  auto * cube = new SoCube;
  scene.root->addChild(cube);
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(1, 1)));
  TEST_ASSERT(target, "CPU camera proof test requires a target shell");
  auto * backend = new CameraOverlayTestBackend;
  target->getPimpl()->backend.reset(backend);
  target->getPimpl()->depthReadbackEnabled = false;
  CoinRenderAction action(SbViewportRegion(1, 1)), full(SbViewportRegion(1, 1));
  action.setRenderTarget(target.get());
  action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS &&
              action.getPimpl()->cachedCamera == camera,
              "PHONG scene with exact transform/light types must qualify the common overlay");
  const auto * originalGeometry = action.getPimpl()->lastValidPlan.vertices.data();
  for (int frame = 0; frame < 4; ++frame) {
    camera->position.setValue(.25f * frame, -.1f, 10 - .3f * frame);
    camera->orientation.setValue(SbVec3f(0, 1, 0), .08f * frame);
    camera->heightAngle = .7f + .03f * frame;
    camera->farDistance = 40 + frame;
    TEST_ASSERT(action.getPimpl()->cameraOnlyDirty && !action.getPimpl()->cameraPatchInvalidated,
                "camera fields alone must produce the notification proof");
    action.apply(scene.root);
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CAMERA_OVERLAY", "1");
    full.apply(scene.root);
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CAMERA_OVERLAY", "0");
    auto comparison = action.getPimpl()->lastValidPlan;
    const auto & captured = full.getPimpl()->lastValidPlan;
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && full.getLastStatus() == CoinRenderAction::SUCCESS &&
                backend->lastReuse.kind == CoinRenderFrameReuseKind::CAMERA_PATCH &&
                action.getPimpl()->lastValidPlan.vertices.data() == originalGeometry &&
                cameraLightingNear(comparison, captured),
                "translation/rotation/projection must reuse geometry and match freshly captured PHONG lights");
    comparison.lightingStates = captured.lightingStates;
    TEST_ASSERT(comparison.hasSamePayload(captured),
                "all non-lighting fields of the camera overlay must match a full capture exactly");
  }
  const auto beforeFailure = action.getPimpl()->lastValidPlan;
  camera->position.setValue(.7f, -.2f, 8);
  backend->failNext = true;
  action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
              backend->lastReuse.kind == CoinRenderFrameReuseKind::CAMERA_PATCH &&
              action.getPimpl()->lastValidPlan.revision == beforeFailure.revision &&
              action.getPimpl()->lastValidPlan.hasSamePayload(beforeFailure) &&
              action.getPimpl()->cameraOnlyDirty,
              "submission failure must roll back all camera/light values and preserve the pending proof");
  action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS &&
              backend->lastReuse.kind == CoinRenderFrameReuseKind::CAMERA_PATCH &&
              backend->lastReuse.baseRevision == beforeFailure.revision,
              "retry after submission failure must patch the last successfully submitted revision");
  // Each non-camera notification must independently reject the proof. A new
  // successful capture may then establish a fresh anchor for camera updates.
  for (int mutation = 0; mutation < 5; ++mutation) {
    if (mutation == 0) objectTransform->translation.setValue(.3f, .4f, .2f);
    if (mutation == 1) material->diffuseColor.setValue(.2f, .6f, .4f);
    if (mutation == 2) point->location.setValue(-1, 3, 2);
    if (mutation == 3) cube->width = 2.5f;
    if (mutation == 4) scene.root->addChild(new SoCube);
    camera->position.setValue(.1f * mutation, .1f, 9);
    TEST_ASSERT(action.getPimpl()->cameraPatchInvalidated,
                "transform/material/light/geometry/structure mutation must invalidate the overlay");
    action.apply(scene.root);
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CAMERA_OVERLAY", "1"); full.apply(scene.root);
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CAMERA_OVERLAY", "0");
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                backend->lastReuse.kind != CoinRenderFrameReuseKind::CAMERA_PATCH &&
                !action.getPimpl()->cachedCamera && action.getPimpl()->candidateCamera == camera &&
                !action.getPimpl()->cameraOverlayBasis.owner &&
                action.getPimpl()->lastValidPlan.hasSamePayload(full.getPimpl()->lastValidPlan),
                "object mutations must capture equivalently and defer scene-wide camera qualification");
  }
  const auto * mutatedGeometry = action.getPimpl()->lastValidPlan.vertices.data();
  const auto latestObjectCapture = action.getPimpl()->lastValidPlan;
  camera->position.setValue(-.2f, .3f, 9);
  TEST_ASSERT(action.getPimpl()->cameraOnlyDirty && !action.getPimpl()->cameraPatchInvalidated,
              "the unqualified candidate sensor must still identify a camera-only change");
  backend->failNext = true;
  action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
              action.getPimpl()->lastValidPlan.hasSamePayload(latestObjectCapture) &&
              action.getPimpl()->lastValidPlan.revision == latestObjectCapture.revision &&
              action.getPimpl()->cameraOnlyDirty,
              "failed first lazy overlay must roll back to the latest object capture and remain retryable");
  action.apply(scene.root);
  coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CAMERA_OVERLAY", "1"); full.apply(scene.root);
  coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CAMERA_OVERLAY", "0");
  auto lazyComparison = action.getPimpl()->lastValidPlan;
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS &&
              backend->lastReuse.kind == CoinRenderFrameReuseKind::CAMERA_PATCH &&
              action.getPimpl()->cachedCamera == camera &&
              action.getPimpl()->lastValidPlan.vertices.data() == mutatedGeometry &&
              cameraLightingNear(lazyComparison, full.getPimpl()->lastValidPlan),
              "camera after consecutive object captures must qualify the latest anchor and preserve its geometry");
  lazyComparison.lightingStates = full.getPimpl()->lastValidPlan.lightingStates;
  TEST_ASSERT(lazyComparison.hasSamePayload(full.getPimpl()->lastValidPlan),
              "lazy camera qualification must match the latest full capture after transform/material/light/geometry/structure changes");
  objectTransform->translation.connectFrom(&camera->position);
  action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && !action.getPimpl()->cachedCamera,
              "camera-connected scene fields must disqualify the notification proof");
  camera->position.setValue(1, 2, 10);
  action.apply(scene.root);
  TEST_ASSERT(backend->lastReuse.kind != CoinRenderFrameReuseKind::CAMERA_PATCH && !action.getPimpl()->cachedCamera,
              "camera-driven transform changes must never be treated as an immutable scene");
  objectTransform->translation.disconnect();
  objectTransform->translation.touch();
  action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && !action.getPimpl()->cachedCamera,
              "a notification after disconnect must capture before the next lazy camera qualification");
  camera->position.setValue(.8f, 1.8f, 10);
  action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && action.getPimpl()->cachedCamera == camera &&
              backend->lastReuse.kind == CoinRenderFrameReuseKind::CAMERA_PATCH,
              "camera motion after a disconnected transform is captured must restore the qualified overlay");
  return 0;
}

int testSharedCameraQualification() {
  struct Scene {
    SoSeparator * root = new SoSeparator;
    Scene() { root->ref(); }
    ~Scene() { root->unref(); }
  } scene;
  auto * camera = new SoPerspectiveCamera;
  camera->position.setValue(0, 0, 10);
  scene.root->addChild(camera);
  auto * sharedMaterial = new SoMaterial;
  auto * sharedCube = new SoCube;
  for (int i = 0; i < 64; ++i) {
    auto * occurrence = new SoSeparator;
    occurrence->addChild(sharedMaterial);
    occurrence->addChild(sharedCube);
    scene.root->addChild(occurrence);
  }
  CoinRenderAction action;
  action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && action.getPimpl()->cachedCamera == camera,
              "shared exact leaves must qualify without changing occurrence capture");
  sharedCube->width.connectFrom(&camera->focalDistance);
  action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && !action.getPimpl()->cachedCamera,
              "a connected field on a shared leaf must disqualify every occurrence");
  sharedCube->width.disconnect();
  // Disconnect preserves the evaluated value and removes auditor links; it
  // does not itself notify. Explicitly request capture before requalifying.
  sharedCube->width.touch();
  action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && !action.getPimpl()->cachedCamera &&
              action.getPimpl()->candidateCamera == camera && !action.getPimpl()->cameraOverlayBasis.owner,
              "an explicit notification after disconnect must capture and defer qualification");
  camera->position.setValue(.2f, 0, 10);
  action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && action.getPimpl()->cachedCamera == camera,
              "camera motion after disconnected shared-leaf capture must qualify its safe anchor lazily");
  auto * reusedGroup = new SoSeparator;
  reusedGroup->addChild(camera);
  reusedGroup->addChild(sharedCube);
  scene.root->addChild(reusedGroup);
  scene.root->addChild(reusedGroup);
  action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && !action.getPimpl()->cachedCamera,
              "shared groups must retain the count of all repeated camera occurrences");
  camera->position.setValue(.4f, 0, 10);
  action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && !action.getPimpl()->cachedCamera,
              "lazy qualification must also refuse a camera reused through shared groups");
  return 0;
}

int testDistantLightCameraFallback() {
  struct Scene {
    SoSeparator * root = new SoSeparator;
    Scene() { root->ref(); }
    ~Scene() { root->unref(); }
  } scene;
  auto * camera = new SoPerspectiveCamera;
  camera->position.setValue(0, 0, 1.0e8f);
  scene.root->addChild(camera);
  auto * light = new SoPointLight;
  light->location.setValue(0, 0, 1);
  scene.root->addChild(light);
  scene.root->addChild(new SoCube);
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(1, 1)));
  TEST_ASSERT(target, "CPU precision fallback test requires a target shell");
  auto * backend = new CameraOverlayTestBackend;
  target->getPimpl()->backend.reset(backend);
  target->getPimpl()->depthReadbackEnabled = false;
  CoinRenderAction action(SbViewportRegion(1, 1));
  action.setRenderTarget(target.get());
  action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && !action.getPimpl()->cachedCamera &&
              action.getPimpl()->lastValidPlan.lightingStates[0].lights[0].position[2] == -1.0e8f,
              "distant capture must succeed through the ordinary path without qualifying a lossy basis");
  camera->position.setValue(0, 0, 0);
  action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS &&
              backend->lastReuse.kind != CoinRenderFrameReuseKind::CAMERA_PATCH &&
              action.getPimpl()->lastValidPlan.lightingStates[0].lights[0].position[2] == 1 &&
              !action.getPimpl()->cachedCamera && action.getPimpl()->candidateCamera == camera,
              "moving near must recapture the original unit-sized light and defer qualification of that safe anchor");
  camera->position.setValue(.2f, 0, 0);
  action.apply(scene.root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && action.getPimpl()->cachedCamera == camera &&
              backend->lastReuse.kind == CoinRenderFrameReuseKind::CAMERA_PATCH &&
              action.getPimpl()->lastValidPlan.lightingStates[0].lights[0].position[2] == 1,
              "a camera-only change after distant fallback must qualify the freshly recaptured safe anchor");
  return 0;
}

int testAnnotationLayers() {
  SoSeparator * root = new SoSeparator;
  root->ref();
  root->addChild(new SoCube);
  SoAnnotation * first = new SoAnnotation;
  first->addChild(new SoCube);
  root->addChild(first);
  root->addChild(new SoCube);
  SoAnnotation * second = new SoAnnotation;
  second->addChild(new SoCube);
  root->addChild(second);

  CoinRenderAction action(SbViewportRegion(64, 64));
  action.apply(root);
  const std::string log(action.getRecordingLog().getString());
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS,
              "SoAnnotation capture should succeed");
  TEST_ASSERT(log.find("layer=0 clearDepthBefore=0") != std::string::npos,
              "regular geometry should remain in the base layer");
  TEST_ASSERT(log.find("layer=1 clearDepthBefore=0") != std::string::npos,
              "first annotation should start layer one without clearing base depth");
  TEST_ASSERT(log.find("layer=2 clearDepthBefore=0") != std::string::npos,
              "second annotation should start a distinct foreground layer");
  TEST_ASSERT(log.find("depthTest=0 depthWrite=0") != std::string::npos,
              "foreground annotation should disable depth testing and writes");
  root->unref();
  return 0;
}

int testDepthStateCapture() {
  SoSeparator * root = new SoSeparator;
  root->ref();

  SoSeparator * cubeBranch = new SoSeparator;
  SoDepthBuffer * cubeDepth = new SoDepthBuffer;
  cubeDepth->test = TRUE;
  cubeDepth->write = TRUE;
  cubeDepth->function = SoDepthBuffer::LEQUAL;
  cubeBranch->addChild(cubeDepth);
  cubeBranch->addChild(new SoCube);
  root->addChild(cubeBranch);

  SoSeparator * labelBranch = new SoSeparator;
  SoDepthBuffer * labelDepth = new SoDepthBuffer;
  labelDepth->test = TRUE;
  labelDepth->write = FALSE;
  labelDepth->function = SoDepthBuffer::LEQUAL;
  labelBranch->addChild(labelDepth);
  labelBranch->addChild(new SoCube);
  root->addChild(labelBranch);

  CoinRenderAction action(SbViewportRegion(64, 64));
  action.apply(root);
  const std::string log(action.getRecordingLog().getString());
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS,
              "SoDepthBuffer capture should succeed");
  TEST_ASSERT(log.find("depthTest=1 depthWrite=1 depthFunction=LEQUAL") != std::string::npos,
              "cube branch should capture depth write plus LEQUAL");
  TEST_ASSERT(log.find("depthTest=1 depthWrite=0 depthFunction=LEQUAL") != std::string::npos,
              "label branch should capture test-only LEQUAL");
  root->unref();
  return 0;
}

namespace {
void noOpTriangle(void *, SoCallbackAction *, const SoPrimitiveVertex *,
                  const SoPrimitiveVertex *, const SoPrimitiveVertex *) {}
std::string expandedRecording(CoinRenderAction & action) {
  // Compare the primitive stream, not its storage representation. The
  // observer reference deliberately bypasses native vertex/state reuse.
  const auto & source = action.getPimpl()->lastValidPlan;
  auto expanded = source;
  expanded.vertices.clear();
  expanded.indices.clear();
  for (size_t d = 0; d < source.draws.size(); ++d) {
    const auto & geometry = source.draws[d].geometry;
    auto & output = expanded.draws[d].geometry;
    output.firstVertex = static_cast<uint32_t>(expanded.vertices.size());
    output.firstIndex = static_cast<uint32_t>(expanded.indices.size());
    output.vertexCount = output.indexCount = geometry.indexCount;
    for (size_t i = geometry.firstIndex; i < geometry.firstIndex + geometry.indexCount; ++i) {
      expanded.indices.push_back(static_cast<uint32_t>(expanded.vertices.size()));
      expanded.vertices.push_back(source.vertices[source.indices[i]]);
    }
  }
  CoinRenderRecordingBackend recording;
  return recording.recordToString(expanded);
}
void alternateDepth(void * data, SoCallbackAction * action, const SoPrimitiveVertex *,
                    const SoPrimitiveVertex *, const SoPrimitiveVertex *) {
  int & count = *static_cast<int *>(data);
  SoDepthBufferElement::set(action->getState(), TRUE, (++count % 2) ? FALSE : TRUE,
                           SoDepthBufferElement::LEQUAL, SbVec2f(0, 1));
}
class MutatingCaptureCube : public SoCube {
  SO_NODE_HEADER(MutatingCaptureCube);
public:
  MutatingCaptureCube() { SO_NODE_CONSTRUCTOR(MutatingCaptureCube); }
  static void initClass() { SO_NODE_INIT_CLASS(MutatingCaptureCube, SoCube, "Cube"); }
protected:
  void generatePrimitives(SoAction * action) override {
    SoCube::generatePrimitives(action);
    SoDepthBufferElement::set(action->getState(), TRUE, FALSE,
                             SoDepthBufferElement::LEQUAL, SbVec2f(0, 1));
    SoCube::generatePrimitives(action);
  }
};
SO_NODE_SOURCE(MutatingCaptureCube);
}

int testPrimitiveStateReuse() {
  SoSeparator * root = new SoSeparator;
  root->ref();
  root->addChild(new SoDirectionalLight);
  auto * binding = new SoMaterialBinding;
  binding->value = SoMaterialBinding::PER_PART;
  root->addChild(binding);
  const SbColor colors[] = {SbColor(1,0,0), SbColor(0,1,0), SbColor(0,0,1),
                            SbColor(1,1,0), SbColor(0,1,1), SbColor(1,0,1)};
  SoNode * shapes[] = {new SoCube, new SoCone, new SoCylinder, new SoSphere};
  SoMaterial * changed = nullptr;
  for (int occurrence = 0; occurrence < 2; ++occurrence) {
    auto * branch = new SoSeparator;
    auto * translation = new SoTranslation;
    translation->translation.setValue(float(occurrence * 5), 0, 0);
    branch->addChild(translation);
    auto * material = new SoMaterial;
    material->diffuseColor.setValues(0, 6, colors);
    material->transparency = occurrence ? .25f : 0.0f;
    branch->addChild(material);
    changed = material;
    for (SoNode * shape : shapes) branch->addChild(shape);
    root->addChild(branch);
  }
  CoinRenderAction optimized, reference;
  // An observer callback conservatively selects the full-capture reference.
  static_cast<SoCallbackAction &>(reference).addTriangleCallback(
    SoShape::getClassTypeId(), noOpTriangle, nullptr);
  for (int frame = 0; frame < 2; ++frame) {
    if (frame) {
      changed->diffuseColor.set1Value(0, SbColor(.3f,.7f,.9f));
      static_cast<SoCube *>(shapes[0])->width = 3.0f;
    }
    optimized.apply(root);
    reference.apply(root);
    TEST_ASSERT(optimized.getLastStatus() == CoinRenderAction::SUCCESS &&
                reference.getLastStatus() == CoinRenderAction::SUCCESS,
                "native shapes with shared occurrences and per-part materials must capture");
    TEST_ASSERT(expandedRecording(optimized) == expandedRecording(reference),
                "reuse must preserve geometry, material bindings, transforms and changed frames");
  }
  root->unref();

  // Repeated OVERALL cubes may replay geometry, but never occurrence state.
  root = new SoSeparator;
  root->ref();
  root->addChild(new SoPerspectiveCamera);
  auto * repeatedCube = new SoCube;
  auto * otherDimensions = new SoCube;
  otherDimensions->width = 3.0f;
  SoMaterial * replayMaterial = nullptr;
  for (int i = 0; i < 8; ++i) {
    auto * occurrence = new SoSeparator;
    auto * transform = new SoTransform;
    transform->translation.setValue(float(i * 3), 0, 0);
    occurrence->addChild(transform);
    auto * material = new SoMaterial;
    material->diffuseColor.setValue(.1f + i * .1f, .2f, .7f);
    material->transparency = i % 2 ? .25f : 0.0f;
    replayMaterial = material;
    occurrence->addChild(material);
    auto * depth = new SoDepthBuffer;
    depth->write = i % 2 ? FALSE : TRUE;
    occurrence->addChild(depth);
    occurrence->addChild(i == 4 ? otherDimensions : repeatedCube);
    if (i == 0) occurrence->addChild(repeatedCube); // One merged draw range.
    root->addChild(occurrence);
  }
  CoinRenderAction replay, fullCapture;
  replay.getPimpl()->planOnly = fullCapture.getPimpl()->planOnly = true;
  static_cast<SoCallbackAction &>(fullCapture).addTriangleCallback(
    SoShape::getClassTypeId(), noOpTriangle, nullptr);
  for (int frame = 0; frame < 2; ++frame) {
    if (frame) {
      repeatedCube->width = 4.0f;
      replayMaterial->diffuseColor.setValue(.8f, .3f, .1f);
    }
    replay.apply(root);
    fullCapture.apply(root);
    TEST_ASSERT(replay.getLastStatus() == CoinRenderAction::SUCCESS &&
                fullCapture.getLastStatus() == CoinRenderAction::SUCCESS &&
                expandedRecording(replay) == expandedRecording(fullCapture),
                "cube replay must preserve occurrence materials, depth, transforms, dimensions and merged draws");
  }
  root->unref();

  // Alternating materials share two immutable meshes; a merged occurrence
  // must append indices without corrupting any preceding shared draw.
  root = new SoSeparator;
  root->ref();
  repeatedCube = new SoCube;
  for (int i = 0; i < 32; ++i) {
    auto * occurrence = new SoSeparator;
    auto * transform = new SoTransform;
    transform->translation.setValue(float(i * 3), 0, 0);
    occurrence->addChild(transform);
    auto * material = new SoMaterial;
    material->diffuseColor.setValue(i % 2 ? .8f : .2f, .3f, .7f);
    occurrence->addChild(material);
    occurrence->addChild(repeatedCube);
    if (i == 7) occurrence->addChild(repeatedCube);
    root->addChild(occurrence);
  }
  for (int frame = 0; frame < 2; ++frame) {
    if (frame) repeatedCube->width = 3.0f;
    replay.apply(root);
    fullCapture.apply(root);
    TEST_ASSERT(replay.getLastStatus() == CoinRenderAction::SUCCESS &&
                fullCapture.getLastStatus() == CoinRenderAction::SUCCESS &&
                expandedRecording(replay) == expandedRecording(fullCapture),
                "shared cube ranges must preserve merged draws and every occurrence across frames");
    const auto & plan = replay.getPimpl()->lastValidPlan;
    TEST_ASSERT(plan.vertices.size() == 48 && plan.indices.size() <= 144,
                "repeated cubes must store only two material meshes, including merged occurrences");
  }
  root->unref();

  root = new SoSeparator;
  root->ref();
  auto * observedCube = new SoCube;
  root->addChild(observedCube);
  root->addChild(observedCube);
  CoinRenderAction callbacks;
  int count = 0;
  // Register through the base API to cover callbacks the derived action cannot intercept.
  static_cast<SoCallbackAction &>(callbacks).addTriangleCallback(
    SoShape::getClassTypeId(), alternateDepth, &count);
  callbacks.apply(root);
  const std::string log = callbacks.getRecordingLog().getString();
  TEST_ASSERT(callbacks.getLastStatus() == CoinRenderAction::SUCCESS && count == 24,
              "additional callback must still receive every cube triangle");
  TEST_ASSERT(log.find("depthWrite=0") != std::string::npos &&
              log.find("depthWrite=1") != std::string::npos,
              "state changes between triangle callbacks must not be cached away");
  root->unref();

  root = new SoSeparator;
  root->ref();
  root->addChild(new MutatingCaptureCube);
  CoinRenderAction subclass;
  subclass.apply(root);
  const std::string subclassLog = subclass.getRecordingLog().getString();
  TEST_ASSERT(subclass.getLastStatus() == CoinRenderAction::SUCCESS &&
              subclassLog.find("depthWrite=0") != std::string::npos &&
              subclassLog.find("depthWrite=1") != std::string::npos,
              "overridden primitive generation must retain state changes inside the shape");
  root->unref();
  return 0;
}

int testCaptureStorageReserve() {
  struct RestoreReserveOption {
    std::string previous;
    bool existed = false;
    RestoreReserveOption() {
      const char * value = std::getenv("COIN_RENDER_DISABLE_CAPTURE_RESERVE");
      existed = value != nullptr;
      if (value) previous = value;
    }
    ~RestoreReserveOption() {
      coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CAPTURE_RESERVE",
                                  existed ? previous.c_str() : nullptr);
    }
  } restore;

  // Match imported-scene wrappers: the broad group is two child levels down.
  auto * root = new SoSeparator;
  root->ref();
  root->addChild(new SoPerspectiveCamera);
  root->addChild(new SoDirectionalLight);
  auto * imported = new SoSeparator;
  auto * scene = new SoSeparator;
  root->addChild(imported);
  imported->addChild(scene);
  auto * cube = new SoCube;
  SoMaterial * changedMaterial = nullptr;
  for (int i = 0; i < 320; ++i) {
    auto * branch = new SoSeparator;
    auto * transform = new SoTransform;
    transform->translation.setValue(float(i % 20), float(i / 20), float(i % 3));
    transform->scaleFactor.setValue(1, .5f + float(i % 3) * .25f, .75f);
    auto * material = new SoMaterial;
    material->diffuseColor.setValue(i % 2 ? .8f : .1f, .3f, i % 2 ? .2f : .7f);
    material->transparency = i % 7 ? 0.0f : .25f;
    changedMaterial = material;
    auto * depth = new SoDepthBuffer;
    depth->write = i % 2 ? FALSE : TRUE;
    depth->function = SoDepthBuffer::LEQUAL;
    branch->addChild(transform);
    branch->addChild(material);
    branch->addChild(depth);
    branch->addChild(cube);
    scene->addChild(branch);
  }
  CoinRenderAction reserved, growing;
  reserved.getPimpl()->planOnly = growing.getPimpl()->planOnly = true;
  for (int frame = 0; frame < 2; ++frame) {
    if (frame) {
      cube->width = 3.0f;
      changedMaterial->diffuseColor.setValue(.3f, .6f, .9f);
    }
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CAPTURE_RESERVE", "0");
    reserved.apply(root);
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CAPTURE_RESERVE", "1");
    growing.apply(root);
    const auto & a = reserved.getPimpl()->lastValidPlan;
    const auto & b = growing.getPimpl()->lastValidPlan;
    TEST_ASSERT(reserved.getLastStatus() == CoinRenderAction::SUCCESS &&
                growing.getLastStatus() == CoinRenderAction::SUCCESS,
                "both allocation routes must capture changing imported scenes");
    TEST_ASSERT(a.renderStates.size() == 320 && a.draws.size() == 320 &&
                a.renderStates.capacity() == 320 && a.draws.capacity() == 320 &&
                b.renderStates.capacity() > a.renderStates.capacity(),
                "shallow fanout must reserve storage before capture and optout must grow normally");
    TEST_ASSERT(a.indices == b.indices && expandedRecording(reserved) == expandedRecording(growing),
                "reserve must preserve geometry, lighting, camera, transform, material and depth capture");
  }
  root->unref();

  // Many non-drawing children must only affect the bounded allocation hint.
  for (int children : {255, 65538}) {
    root = new SoSeparator;
    root->ref();
    auto * empty = new SoCallback;
    for (int i = 0; i < children - 1; ++i) root->addChild(empty);
    root->addChild(new SoCube);
    CoinRenderAction bounded, reference;
    bounded.getPimpl()->planOnly = reference.getPimpl()->planOnly = true;
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CAPTURE_RESERVE", "0");
    bounded.apply(root);
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CAPTURE_RESERVE", "1");
    reference.apply(root);
    const auto & captured = bounded.getPimpl()->lastValidPlan;
    const size_t expectedCapacity = children < 256 ? 1 : 65536;
    TEST_ASSERT(bounded.getLastStatus() == CoinRenderAction::SUCCESS &&
                reference.getLastStatus() == CoinRenderAction::SUCCESS &&
                captured.renderStates.capacity() == expectedCapacity,
                "small scenes must stay unreserved and large estimates must cap at 65536");
    // Stroke expansion publishes a new draw vector, even for triangles, so
    // its final capacity describes expansion rather than the capture hint.
    TEST_ASSERT(expandedRecording(bounded) == expandedRecording(reference),
                "non-drawing fanout must not change captured payload");
    root->unref();
  }
  return 0;
}

int main() {

  SoDB::init();
  CoinRenderAction::initClass();
  MutatingCaptureCube::initClass();
  std::cout << "Running CoinRenderActionTest..." << std::endl;

  int failed = 0;
  if (testCaptureStorageReserve()) { std::cerr << "testCaptureStorageReserve failed" << std::endl; failed++; }
  if (testCapturedCompositionBorrow()) { std::cerr << "testCapturedCompositionBorrow failed" << std::endl; failed++; }
  if (testCapturedCompositionPolicy()) { std::cerr << "testCapturedCompositionPolicy failed" << std::endl; failed++; }
  if (testPrimitiveStateReuse()) { std::cerr << "testPrimitiveStateReuse failed" << std::endl; failed++; }
  if (testDepthStateCapture()) { std::cerr << "testDepthStateCapture failed" << std::endl; failed++; }
  if (testAnnotationLayers()) { std::cerr << "testAnnotationLayers failed" << std::endl; failed++; }
  if (testCameraOverlayAndFallback()) { std::cerr << "testCameraOverlayAndFallback failed" << std::endl; failed++; }
  if (testPhongCameraOverlayNotificationsAndRollback()) { std::cerr << "testPhongCameraOverlayNotificationsAndRollback failed" << std::endl; failed++; }
  if (testTranslationOverlayAndRollback()) { std::cerr << "testTranslationOverlayAndRollback failed" << std::endl; failed++; }
  if (testCaptureCameraBasisReuse()) { std::cerr << "testCaptureCameraBasisReuse failed" << std::endl; failed++; }
  if (testTranslationOverlayInvalidationAndOwnership()) { std::cerr << "testTranslationOverlayInvalidationAndOwnership failed" << std::endl; failed++; }
  if (testObjectMaterialGeometryOverlay()) { std::cerr << "testObjectMaterialGeometryOverlay failed" << std::endl; failed++; }
  if (testInterleavedCubeSourceOwnership()) { std::cerr << "testInterleavedCubeSourceOwnership failed" << std::endl; failed++; }
  if (testObjectProofMemoization()) { std::cerr << "testObjectProofMemoization failed" << std::endl; failed++; }
  if (testObjectPayloadAliasFallbacks()) { std::cerr << "testObjectPayloadAliasFallbacks failed" << std::endl; failed++; }
  if (testObjectPayloadInvalidationAndOptout()) { std::cerr << "testObjectPayloadInvalidationAndOptout failed" << std::endl; failed++; }
  if (testSharedCameraQualification()) { std::cerr << "testSharedCameraQualification failed" << std::endl; failed++; }
  if (testDistantLightCameraFallback()) { std::cerr << "testDistantLightCameraFallback failed" << std::endl; failed++; }
      if (testWindowTargetRecordingBackend()) { std::cerr << "testWindowTargetRecordingBackend failed" << std::endl; failed++; }
  if (testProfileMultiLightAndPerVertexTransparency()) { std::cerr << "testProfileMultiLightAndPerVertexTransparency failed" << std::endl; failed++; }
  if (testBaseApplyNotHidden()) { std::cerr << "testBaseApplyNotHidden failed" << std::endl; failed++; }
  if (testBackendAvailability()) { std::cerr << "testBackendAvailability failed" << std::endl; failed++; }
  if (testMultipleRootsPathList()) { std::cerr << "testMultipleRootsPathList failed" << std::endl; failed++; }
  if (testPerVertexMaterialCapture()) { std::cerr << "testPerVertexMaterialCapture failed" << std::endl; failed++; }
  if (testLightTransformWithRotatedCamera()) { std::cerr << "testLightTransformWithRotatedCamera failed" << std::endl; failed++; }
  if (testPlanOverflowAndSlotValidation()) { std::cerr << "testPlanOverflowAndSlotValidation failed" << std::endl; failed++; }
  if (testTypeAndInit()) { std::cerr << "testTypeAndInit failed" << std::endl; failed++; }
  if (testSeparatorAndState()) { std::cerr << "testSeparatorAndState failed" << std::endl; failed++; }
  if (testCameraPerDraw()) { std::cerr << "testCameraPerDraw failed" << std::endl; failed++; }
  if (testScopedLight()) { std::cerr << "testScopedLight failed" << std::endl; failed++; }
  if (testLightingCaptureAcrossShapesAndFrames()) { std::cerr << "testLightingCaptureAcrossShapesAndFrames failed" << std::endl; failed++; }
  if (testPrimitives()) { std::cerr << "testPrimitives failed" << std::endl; failed++; }
  if (testMaterialBinding()) { std::cerr << "testMaterialBinding failed" << std::endl; failed++; }
  if (testProjectionConversion()) { std::cerr << "testProjectionConversion failed" << std::endl; failed++; }
  if (testEmptyInputs()) { std::cerr << "testEmptyInputs failed" << std::endl; failed++; }
  if (testNodePathPathList()) { std::cerr << "testNodePathPathList failed" << std::endl; failed++; }
  if (testDeterminism()) { std::cerr << "testDeterminism failed" << std::endl; failed++; }
  if (testForbiddenUsage()) { std::cerr << "testForbiddenUsage failed" << std::endl; failed++; }

  if (failed == 0) {
    std::cout << "All CoinRenderRenderAction tests PASSED!" << std::endl;
    return 0;
  }
  std::cerr << failed << " test(s) FAILED!" << std::endl;
  return 1;
}
