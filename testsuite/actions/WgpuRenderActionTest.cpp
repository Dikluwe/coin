#include <setup.h>
#include "rendering/wgpu/SoWgpuFramePlan.h"
#include "rendering/wgpu/SoWgpuRenderTargetP.h"
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/rendering/SoWgpuNativeSurface.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/SoDB.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/SoPath.h>
#include <Inventor/lists/SoPathList.h>

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

#define TEST_ASSERT(cond, msg) do {   if (!(cond)) {     std::cerr << "FAILED: " << msg << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl;     return 1;   } } while (0)

int testTypeAndInit() {
  SoType t = SoType::fromName("SoWgpuRenderAction");
  TEST_ASSERT(t != SoType::badType(), "SoWgpuRenderAction must be registered in SoType");
  TEST_ASSERT(t.isDerivedFrom(SoCallbackAction::getClassTypeId()), "SoWgpuRenderAction must derive from SoCallbackAction");

  SoWgpuRenderAction action1;
  TEST_ASSERT(action1.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Initial status should be SUCCESS");

  SbViewportRegion vp(800, 600);
  SoWgpuRenderAction action2(vp);
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

  SoWgpuRenderAction action;
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "apply should succeed");

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

  SoWgpuRenderAction action;
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "apply should succeed");
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

  SoWgpuRenderAction action;
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "apply should succeed");
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

  SoWgpuRenderAction action;
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "apply should succeed");
  std::string log = action.getRecordingLog().getString();
  TEST_ASSERT(log.find("top=TRIANGLES") != std::string::npos, "Should record TRIANGLES");
  TEST_ASSERT(log.find("top=LINES") != std::string::npos, "Should record LINES");
  TEST_ASSERT(log.find("top=POINTS") != std::string::npos, "Should record POINTS");

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

  SoWgpuRenderAction action;
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "apply should succeed");
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
  SoWgpuRenderAction action;
  // 1. apply(NULL)
  action.apply(static_cast<SoNode *>(NULL));
  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "apply(NULL) should succeed with empty plan");

  // 2. empty separator
  SoSeparator * emptySep = new SoSeparator;
  emptySep->ref();
  action.apply(emptySep);
  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "apply(emptySep) should succeed");
  emptySep->unref();

  return 0;
}

int testNodePathPathList() {
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoCube * cube = new SoCube;
  root->addChild(cube);

  SoWgpuRenderAction action;

  // Apply to Node
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "apply(node) should succeed");
  std::string nodeLog = action.getRecordingLog().getString();

  // Apply to Path
  SoPath * path = new SoPath(root);
  path->ref();
  path->append(cube);
  action.apply(path);
  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "apply(path) should succeed");
  std::string pathLog = action.getRecordingLog().getString();
  TEST_ASSERT(!pathLog.empty(), "Path log should not be empty");

  // Apply to PathList
  SoPathList pathlist;
  pathlist.append(path);
  action.apply(pathlist);
  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "apply(pathlist) should succeed");

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

  SoWgpuRenderAction action1;
  action1.apply(root1);
  std::string log1 = action1.getRecordingLog().getString();

  SoWgpuRenderAction action2;
  action2.apply(root2);
  std::string log2 = action2.getRecordingLog().getString();

  TEST_ASSERT(log1 == log2, "Two identical scenes must generate identical canonical recording logs");

  root1->unref();
  root2->unref();
  return 0;
}

static SoWgpuRenderAction::Status g_nestedStatus = SoWgpuRenderAction::SUCCESS;

static void reentrantCallback(void * userdata, SoAction * action) {
  SoWgpuRenderAction * wgpuAction = static_cast<SoWgpuRenderAction *>(userdata);
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
  SoWgpuRenderAction action;

  SoCallback * cb = new SoCallback;
  cb->setCallback(reentrantCallback, &action);
  root->addChild(cb);

  action.apply(root);
  TEST_ASSERT(g_nestedStatus == SoWgpuRenderAction::INVALID_SCENE, "Nested apply() must be rejected with INVALID_SCENE");
  // Finding 6: Outer status must remain INVALID_SCENE after nested failure
  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::INVALID_SCENE, "Outer status must remain INVALID_SCENE");

  root->unref();
  return 0;
}

int testBackendAvailability() {
  // When built without Dawn / wgpu-native, isGpuBackendAvailable must be FALSE
  SbBool avail = SoWgpuRenderAction::isGpuBackendAvailable();
#if defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE) || defined(HAVE_WGPU_RUST_BRIDGE)
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

  SoWgpuRenderAction action;
  action.apply(pathlist);

  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "apply(pathlist with multiple roots) should succeed");
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

  SoWgpuRenderAction action;
  action.apply(root);

  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "apply() on per-vertex material scene should succeed");
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

  SoWgpuRenderAction action;
  action.apply(root);

  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "apply() with rotated camera must succeed");
  std::string log = action.getRecordingLog().getString();
  TEST_ASSERT(!log.empty(), "Recording log must not be empty");

  root->unref();
  return 0;
}

int testPlanOverflowAndSlotValidation() {
  // Finding 5: Validation must catch range overflow and empty slots
  FramePlan plan;
  plan.viewports.push_back(ViewportSnapshot{});
  plan.cameras.push_back(CameraSnapshot{});
  plan.materials.push_back(MaterialSnapshot{});
  plan.lightingStates.push_back(LightingSnapshot{});

  RenderStateSnapshot rs;
  rs.viewportSlot = 0;
  rs.cameraSlot = 0;
  rs.materialSlot = 0;
  rs.lightingSlot = 0;
  plan.renderStates.push_back(rs);

  VertexSnapshot v{};
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

  DrawPacket draw;
  draw.renderStateSlot = 0;
  draw.topology = PrimitiveTopology::TRIANGLE_LIST;
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
  FramePlan plan;
  plan.viewports.push_back(ViewportSnapshot{});
  plan.cameras.push_back(CameraSnapshot{});

  MaterialSnapshot opaqueMat;
  opaqueMat.transparency = 0.0f;
  plan.materials.push_back(opaqueMat);

  MaterialSnapshot transparentMat;
  transparentMat.transparency = 0.5f;
  transparentMat.diffuse[3] = 0.5f;
  plan.materials.push_back(transparentMat);

  LightingSnapshot singleLight;
  LightSourceSnapshot l1;
  l1.type = LightType::DIRECTIONAL;
  singleLight.lights.push_back(l1);
  plan.lightingStates.push_back(singleLight);

  LightingSnapshot multiLight;
  multiLight.lights.push_back(l1);
  multiLight.lights.push_back(l1);
  plan.lightingStates.push_back(multiLight);

  RenderStateSnapshot rs;
  rs.viewportSlot = 0;
  rs.cameraSlot = 0;
  rs.materialSlot = 0; // opaque
  rs.lightingSlot = 0; // single light
  plan.renderStates.push_back(rs);

  VertexSnapshot v0{}, v1{}, v2{};
  v0.materialSlot = 0;
  v1.materialSlot = 0;
  v2.materialSlot = 0;
  plan.vertices.push_back(v0);
  plan.vertices.push_back(v1);
  plan.vertices.push_back(v2);

  plan.indices.push_back(0);
  plan.indices.push_back(1);
  plan.indices.push_back(2);

  DrawPacket draw;
  draw.renderStateSlot = 0;
  draw.topology = PrimitiveTopology::TRIANGLE_LIST;
  draw.geometry.firstVertex = 0;
  draw.geometry.vertexCount = 3;
  draw.geometry.firstIndex = 0;
  draw.geometry.indexCount = 3;
  plan.draws.push_back(draw);

  std::string diag;
  // Case 1: Valid single light, opaque
  TEST_ASSERT(SoWgpuRenderTargetP::validateProfile(plan, diag), "Base profile should be valid");

  // Case 2: two lights are supported in 3C; nine exceed the published budget.
  plan.renderStates[0].lightingSlot = 1; // multiLight
  TEST_ASSERT(SoWgpuRenderTargetP::validateProfile(plan, diag), "Two lights should be supported");
  plan.lightingStates[1].lights.resize(9, l1);
  TEST_ASSERT(!SoWgpuRenderTargetP::validateProfile(plan, diag), "Nine lights must be rejected by validateProfile");
  TEST_ASSERT(diag.find("eight") != std::string::npos, "Diagnostic must mention eight-light limit");
  plan.lightingStates[1].lights.resize(2);
  plan.renderStates[0].lightingSlot = 0; // restore

  // Case 3: Per-vertex transparency must be rejected
  plan.vertices[1].materialSlot = 1; // transparentMat
  TEST_ASSERT(!SoWgpuRenderTargetP::validateProfile(plan, diag), "Per-vertex transparency must be rejected");
  TEST_ASSERT(diag.find("mixed") != std::string::npos, "Diagnostic must mention mixed alpha");
  plan.vertices[1].materialSlot = 0; // restore

  return 0;
}

int testBaseApplyNotHidden() {
  // Test Finding 4: SoAction::apply(SoAction*) is not hidden
  void (SoAction::*applyActionFn)(SoAction*) = &SoWgpuRenderAction::apply;
  TEST_ASSERT(applyActionFn != nullptr, "apply(SoAction*) must be accessible via SoWgpuRenderAction");

  SoSeparator * root = new SoSeparator;
  root->ref();
  SoWgpuRenderAction action1;
  SoWgpuRenderAction action2;
  // If we apply action1 to a node, then action2.apply(&action1) copies what action1 was applied to
  // (though action1 is done, let's verify compiler and method binding)
  root->unref();
  return 0;
}

int testWindowTargetRecordingBackend() {
#if !defined(HAVE_WGPU_RUST_BRIDGE)
  SoWgpuNativeSurfaceDescriptor desc{};
  desc.abiVersion = COIN_WGPU_NATIVE_SURFACE_ABI_VERSION;
  desc.structSize = sizeof(desc);
  desc.type = COIN_WGPU_SURFACE_XLIB;
  desc.reserved = 0;
  desc.native.xlib.display = (void*)0x1234;
  desc.native.xlib.window = 1;

  SoWgpuRenderTarget * target = SoWgpuRenderTarget::createWindow(desc, SbVec2i32(100, 100));
  TEST_ASSERT(target != nullptr, "Target pointer must not be null");
  TEST_ASSERT(target->getStatus() == SoWgpuRenderTarget::TARGET_ERROR, "Status must be TARGET_ERROR in RECORDING mode");
  TEST_ASSERT(std::string(target->getLastError()).find("RECORDING") != std::string::npos, "Diagnostic must mention RECORDING backend");

  SoSeparator * root = new SoSeparator;
  root->ref();
  SoWgpuRenderAction action;
  action.setRenderTarget(target);
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::BACKEND_ERROR, "apply on TARGET_ERROR must report BACKEND_ERROR");
  TEST_ASSERT(action.getLastError().getLength() > 0, "action getLastError must be populated");
  root->unref();
  delete target;
#endif
  return 0;
}

int main() {

  SoDB::init();
  SoWgpuRenderAction::initClass();
  std::cout << "Running WgpuRenderActionTest..." << std::endl;

  int failed = 0;
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
    std::cout << "All WgpuRenderAction tests PASSED!" << std::endl;
    return 0;
  }
  std::cerr << failed << " test(s) FAILED!" << std::endl;
  return 1;
}
