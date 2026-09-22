#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/SoDB.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include "rendering/wgpu/SoWgpuRenderTargetP.h"

#include <cassert>
#include <iostream>
#include <vector>

#define TEST_ASSERT(cond, msg) do {   if (!(cond)) {     std::cerr << "FAILED: " << msg << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl;     return 1;   } } while (0)

int main() {
  SoDB::init();
  std::cout << "Running WgpuOffscreenTest..." << std::endl;

  // 1. Build canonical controlled cone scene (Section 4.8)
  SoSeparator * root = new SoSeparator;
  root->ref();

  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  camera->position.setValue(0.0f, 0.0f, 4.0f);
  camera->nearDistance = 0.1f;
  camera->farDistance = 10.0f;
  root->addChild(camera);

  SoDirectionalLight * light = new SoDirectionalLight;
  light->direction.setValue(0.0f, 0.0f, -1.0f);
  light->color.setValue(1.0f, 1.0f, 1.0f);
  light->intensity.setValue(1.0f);
  root->addChild(light);

  SoMaterial * mat = new SoMaterial;
  mat->diffuseColor.setValue(0.8f, 0.2f, 0.1f);
  mat->ambientColor.setValue(0.2f, 0.05f, 0.02f);
  root->addChild(mat);

  SoCone * cone = new SoCone;
  root->addChild(cone);

  // 2. Create offscreen render target
  SbVec2i32 initialSize(256, 256);
  SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(initialSize);
  TEST_ASSERT(target != NULL, "createOffscreen must return non-NULL target");
  TEST_ASSERT(target->getStatus() == SoWgpuRenderTarget::TARGET_READY, "New offscreen target must be READY");
  TEST_ASSERT(target->getSize() == initialSize, "Target size must match initial size");

  // 3. Render action application
  SoWgpuRenderAction action(SbViewportRegion(256, 256));
  action.setBackgroundColor(SbColor4f(0.05f, 0.05f, 0.1f, 1.0f)); // Dark blue background
  action.setRenderTarget(target);
  action.apply(root);

  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "action.apply() on SoCone scene should succeed");

  // 4. Validate pixel readback
  std::vector<uint8_t> pixels;
  target->getPimpl()->readbackRGBA(pixels);
  TEST_ASSERT(pixels.size() == 256 * 256 * 4, "Readback buffer size must be width * height * 4");

  // Check background pixel at corner (x=5, y=5)
  size_t cornerIdx = (5 * 256 + 5) * 4;
  uint8_t bgR = pixels[cornerIdx + 0];
  uint8_t bgG = pixels[cornerIdx + 1];
  uint8_t bgB = pixels[cornerIdx + 2];
  TEST_ASSERT(bgR < 30 && bgG < 30 && bgB > 10, "Corner pixel should be dark blue background");

  // Check cone center pixel at (x=128, y=128)
  size_t centerIdx = (128 * 256 + 128) * 4;
  uint8_t fgR = pixels[centerIdx + 0];
  uint8_t fgG = pixels[centerIdx + 1];
  (void)pixels[centerIdx + 2];
  TEST_ASSERT(fgR > 100 && fgG < 100, "Center pixel must show illuminated reddish cone");

  // 5. Test resize and re-apply
  SbVec2i32 newSize(128, 128);
  TEST_ASSERT(target->resize(newSize), "resize to 128x128 should succeed");
  TEST_ASSERT(target->getSize() == newSize, "Size should update after resize");

  action.setViewportRegion(SbViewportRegion(128, 128));
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "apply after resize should succeed");

  target->getPimpl()->readbackRGBA(pixels);
  TEST_ASSERT(pixels.size() == 128 * 128 * 4, "Resized buffer must have 128*128*4 bytes");

  // 6. Test unsupported feature preflight rejection (Section 4.6)
  // Adding a LineSet to the scene should be detected by preflight and return UNSUPPORTED
  SoSeparator * unsupportedSep = new SoSeparator;
  unsupportedSep->ref();
  unsupportedSep->addChild(cone);

  SoCoordinate3 * coords = new SoCoordinate3;
  coords->point.set1Value(0, 0, 0, 0);
  coords->point.set1Value(1, 1, 1, 1);
  unsupportedSep->addChild(coords);
  unsupportedSep->addChild(new SoLineSet);

  action.apply(unsupportedSep);
  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED, "Non-triangle topology must be rejected with UNSUPPORTED");
  std::string err = action.getLastError().getString();
  TEST_ASSERT(err.find("UNSUPPORTED") != std::string::npos, "Diagnostic message must explain unsupported capability");

  unsupportedSep->unref();

  // 7. Test invalid resize allocation guards (Finding 5)
  TEST_ASSERT(!target->resize(SbVec2i32(-10, 50)), "Negative size resize must fail");
  TEST_ASSERT(target->getStatus() == SoWgpuRenderTarget::TARGET_ERROR, "Status must be TARGET_ERROR after negative resize");
  TEST_ASSERT(!target->resize(SbVec2i32(100000, 100000)), "Excessive dimension resize must fail");
  TEST_ASSERT(target->getStatus() == SoWgpuRenderTarget::TARGET_ERROR, "Status must be TARGET_ERROR after excessive resize");

  // Restore target to valid state
  TEST_ASSERT(target->resize(SbVec2i32(128, 128)), "Restore resize must succeed");
  TEST_ASSERT(target->getStatus() == SoWgpuRenderTarget::TARGET_READY, "Status must be TARGET_READY");

  // 8. Test per-vertex materials offscreen rendering (Finding 3)
  SoSeparator * triRoot = new SoSeparator;
  triRoot->ref();

  SoOrthographicCamera * orthoCam = new SoOrthographicCamera;
  orthoCam->position.setValue(0.0f, 0.0f, 2.0f);
  orthoCam->height = 2.5f;
  orthoCam->nearDistance = 0.1f;
  orthoCam->farDistance = 10.0f;
  triRoot->addChild(orthoCam);

  SoDirectionalLight * triLight = new SoDirectionalLight;
  triLight->direction.setValue(0.0f, 0.0f, -1.0f);
  triLight->color.setValue(1.0f, 1.0f, 1.0f);
  triLight->intensity.setValue(1.0f);
  triRoot->addChild(triLight);

  SoMaterial * triMat = new SoMaterial;
  triMat->diffuseColor.set1Value(0, SbColor(1.0f, 0.0f, 0.0f)); // v0 = Red
  triMat->diffuseColor.set1Value(1, SbColor(0.0f, 1.0f, 0.0f)); // v1 = Green
  triMat->diffuseColor.set1Value(2, SbColor(0.0f, 0.0f, 1.0f)); // v2 = Blue
  triMat->ambientColor.setValue(0.0f, 0.0f, 0.0f);
  triRoot->addChild(triMat);

  SoMaterialBinding * triMb = new SoMaterialBinding;
  triMb->value = SoMaterialBinding::PER_VERTEX;
  triRoot->addChild(triMb);

  SoCoordinate3 * triCoords = new SoCoordinate3;
  triCoords->point.set1Value(0, SbVec3f(0.0f, 0.8f, 0.0f));
  triCoords->point.set1Value(1, SbVec3f(-0.8f, -0.8f, 0.0f));
  triCoords->point.set1Value(2, SbVec3f(0.8f, -0.8f, 0.0f));
  triRoot->addChild(triCoords);

  SoIndexedFaceSet * triIfs = new SoIndexedFaceSet;
  int32_t triIndices[] = { 0, 1, 2, -1 };
  triIfs->coordIndex.setValues(0, 4, triIndices);
  triRoot->addChild(triIfs);

  action.setViewportRegion(SbViewportRegion(128, 128));
  action.apply(triRoot);
  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "apply() on per-vertex colored triangle must succeed");

  target->getPimpl()->readbackRGBA(pixels);
  // Read top pixel near vertex 0 (x=64, y=25) -> should be red
  size_t topIdx = (25 * 128 + 64) * 4;
  uint8_t topR = pixels[topIdx + 0];
  uint8_t topG = pixels[topIdx + 1];
  uint8_t topB = pixels[topIdx + 2];
  TEST_ASSERT(topR > 100 && topG < 60 && topB < 60, "Top pixel must be red");

  // Read bottom-left pixel near vertex 1 (x=25, y=100) -> should be green
  size_t blIdx = (100 * 128 + 25) * 4;
  uint8_t blR = pixels[blIdx + 0];
  uint8_t blG = pixels[blIdx + 1];
  uint8_t blB = pixels[blIdx + 2];
  TEST_ASSERT(blG > 150 && blR < 60 && blB < 60, "Bottom-left pixel must be green");

  // Read bottom-right pixel near vertex 2 (x=100, y=100) -> should be blue
  size_t brIdx = (100 * 128 + 100) * 4;
  uint8_t brR = pixels[brIdx + 0];
  uint8_t brG = pixels[brIdx + 1];
  uint8_t brB = pixels[brIdx + 2];
  TEST_ASSERT(brB > 150 && brR < 60 && brG < 60, "Bottom-right pixel must be blue");

  triRoot->unref();

  // 9. Test homogeneous near-plane clipping (Finding 7)
  SoSeparator * clipRoot = new SoSeparator;
  clipRoot->ref();

  SoPerspectiveCamera * clipCam = new SoPerspectiveCamera;
  clipCam->position.setValue(0.0f, 0.0f, 2.0f);
  clipCam->nearDistance = 1.0f; // Near plane at z = 1.0 (view space z = -1.0)
  clipCam->farDistance = 10.0f;
  clipRoot->addChild(clipCam);

  SoCoordinate3 * clipCoords = new SoCoordinate3;
  // Vertex 0 is behind near plane in view space (z = 1.5 in world, so z = -0.5 in view, behind near plane 1.0)
  clipCoords->point.set1Value(0, SbVec3f(0.0f, 1.0f, 1.5f));
  // Vertices 1 and 2 are in front of near plane
  clipCoords->point.set1Value(1, SbVec3f(-1.0f, -1.0f, -1.0f));
  clipCoords->point.set1Value(2, SbVec3f(1.0f, -1.0f, -1.0f));
  clipRoot->addChild(clipCoords);

  SoIndexedFaceSet * clipIfs = new SoIndexedFaceSet;
  clipIfs->coordIndex.setValues(0, 4, triIndices);
  clipRoot->addChild(clipIfs);

  action.apply(clipRoot);
  TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Rendering near-clipped triangle must succeed without crash");

  clipRoot->unref();

  delete target;
  root->unref();

  std::cout << "All WgpuOffscreen tests PASSED!" << std::endl;
  return 0;
}
