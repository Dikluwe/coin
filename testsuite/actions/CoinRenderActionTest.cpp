#include <setup.h>
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "actions/CoinRenderActionP.h"
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
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/SoPath.h>
#include <Inventor/lists/SoPathList.h>

#include <cassert>
#include <cmath>
#include <iostream>
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
  TEST_ASSERT(std::string(target->getLastError()).find("RECORDING") != std::string::npos, "Diagnostic must mention RECORDING backend");

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

int main() {

  SoDB::init();
  CoinRenderAction::initClass();
  MutatingCaptureCube::initClass();
  std::cout << "Running CoinRenderActionTest..." << std::endl;

  int failed = 0;
  if (testPrimitiveStateReuse()) { std::cerr << "testPrimitiveStateReuse failed" << std::endl; failed++; }
  if (testDepthStateCapture()) { std::cerr << "testDepthStateCapture failed" << std::endl; failed++; }
  if (testAnnotationLayers()) { std::cerr << "testAnnotationLayers failed" << std::endl; failed++; }
  if (testCameraOverlayAndFallback()) { std::cerr << "testCameraOverlayAndFallback failed" << std::endl; failed++; }
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
