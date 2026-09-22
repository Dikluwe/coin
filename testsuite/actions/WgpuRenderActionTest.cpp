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

  root->unref();
  return 0;
}

int main() {
  SoDB::init();
  std::cout << "Running WgpuRenderActionTest..." << std::endl;

  int failed = 0;
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
