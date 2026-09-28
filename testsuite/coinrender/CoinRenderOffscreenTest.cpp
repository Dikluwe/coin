#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/C/basic.h>

#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
#include "rendering/coinwgpu/CoinWgpuBackend.h"
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#endif

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#define TEST_ASSERT(cond, msg) do { \
  if (!(cond)) { \
    std::cerr << "FAILED: " << msg << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
    return 1; \
  } \
} while (0)

int main() {
  SoDB::init();
  CoinRenderAction::initClass();
  std::cout << "Running CoinRenderOffscreenTest..." << std::endl;

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  std::cout << "Active WebGPU Adapter: " << CoinWgpuBackend::getAdapterInfo() << std::endl;
  TEST_ASSERT(CoinRenderAction::isGpuBackendAvailable(), "WebGPU GPU backend must be reported as available");
#endif

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
  CoinRenderTarget * target = CoinRenderTarget::createOffscreen(initialSize);
  TEST_ASSERT(target != NULL, "createOffscreen must return non-NULL target");
  TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_READY, "New offscreen target must be READY");
  TEST_ASSERT(target->getSize() == initialSize, "Target size must match initial size");
  // Backend-specific GPU readback is covered by CoinBgfxOffscreenTest.
  target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
  std::size_t preRenderBytes = 1;
  TEST_ASSERT(target->borrowRGBA(preRenderBytes) == NULL && preRenderBytes == 0,
              "Borrowed RGBA must be unavailable before the first render");

  // 3. Render action application
  CoinRenderAction action(SbViewportRegion(256, 256));
  action.setBackgroundColor(SbColor4f(0.05f, 0.05f, 0.1f, 1.0f)); // Dark blue background
  action.setRenderTarget(target);
  action.apply(root);

  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "action.apply() on SoCone scene should succeed");

  // 4. Validate pixel readback with chromatic tolerance
  std::vector<uint8_t> pixels;
  target->getPimpl()->readbackRGBA(pixels);
  TEST_ASSERT(pixels.size() == 256 * 256 * 4, "Readback buffer size must be width * height * 4");
  std::size_t borrowedBytes = 0;
  const uint8_t * borrowed = target->borrowRGBA(borrowedBytes);
  TEST_ASSERT(borrowed != NULL && borrowedBytes == pixels.size() &&
              std::equal(pixels.begin(), pixels.end(), borrowed),
              "Borrowed RGBA view must match the copying accessor");
  std::size_t repeatedBytes = 0;
  TEST_ASSERT(target->borrowRGBA(repeatedBytes) == borrowed &&
              repeatedBytes == borrowedBytes,
              "Repeated borrowed reads must not move the target buffer");

  // Check background pixel at corner (x=5, y=5)
  size_t cornerIdx = (5 * 256 + 5) * 4;
  uint8_t bgR = pixels[cornerIdx + 0];
  uint8_t bgG = pixels[cornerIdx + 1];
  uint8_t bgB = pixels[cornerIdx + 2];
  TEST_ASSERT(bgR < 30 && bgG < 30 && bgB > 10, "Corner pixel should be dark blue background");

  // Check cone center pixel at (x=128, y=128) with tolerance for hardware interpolation
  size_t centerIdx = (128 * 256 + 128) * 4;
  uint8_t fgR = pixels[centerIdx + 0];
  uint8_t fgG = pixels[centerIdx + 1];
  uint8_t fgB = pixels[centerIdx + 2];
  // Coin's default global ambient intensity is 0.2, not 1.0.
  TEST_ASSERT(std::abs(static_cast<int>(fgR) - 192) <= 25 &&
              std::abs(static_cast<int>(fgG) - 48) <= 15 &&
              std::abs(static_cast<int>(fgB) - 24) <= 12,
              "Center pixel must show illuminated reddish cone with valid chromatic tolerance");

  // Depth remains an attachment for rendering, but its CPU readback is selectable.
  // The default stays compatible with existing consumers.
  std::vector<float> defaultDepth;
  target->readbackDepth(defaultDepth);
  TEST_ASSERT(target->isDepthReadbackEnabled() &&
              defaultDepth.size() == 256u * 256u,
              "New offscreen targets must provide depth readback by default");
  const std::vector<uint8_t> defaultColor = pixels;
  TEST_ASSERT(target->setDepthReadbackEnabled(FALSE) &&
              !target->isDepthReadbackEnabled(), "Disable depth readback");
  target->readbackRGBA(pixels);
  TEST_ASSERT(pixels.empty(), "Changing readback mode must invalidate the old frame");
  borrowedBytes = 1;
  TEST_ASSERT(target->borrowRGBA(borrowedBytes) == NULL && borrowedBytes == 0,
              "Borrowed RGBA must be unavailable after an output-policy change");
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS,
              "Color-only render should succeed");
  target->readbackRGBA(pixels);
  target->readbackDepth(defaultDepth);
  TEST_ASSERT(pixels == defaultColor && defaultDepth.empty(),
              "Color-only render must preserve color and suppress depth output");

  // 5. Test resize and re-apply
  SbVec2i32 newSize(128, 128);
  TEST_ASSERT(target->resize(newSize), "resize to 128x128 should succeed");
  TEST_ASSERT(target->getSize() == newSize, "Size should update after resize");
  TEST_ASSERT(!target->isDepthReadbackEnabled(), "Resize must preserve readback policy");
  borrowedBytes = 1;
  TEST_ASSERT(target->borrowRGBA(borrowedBytes) == NULL && borrowedBytes == 0,
              "Resize must invalidate the borrowed RGBA view");

  action.setViewportRegion(SbViewportRegion(128, 128));
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "apply after resize should succeed");

  target->getPimpl()->readbackRGBA(pixels);
  TEST_ASSERT(pixels.size() == 128 * 128 * 4, "Resized buffer must have 128*128*4 bytes");
  std::vector<float> resizedDepth;
  target->readbackDepth(resizedDepth);
  TEST_ASSERT(resizedDepth.empty(), "Color-only resize must not expose depth");
  const std::vector<uint8_t> resizedColor = pixels;
  TEST_ASSERT(target->setDepthReadbackEnabled(TRUE), "Restore depth readback");
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS,
              "Render after restoring depth should succeed");
  target->readbackRGBA(pixels);
  target->readbackDepth(resizedDepth);
  TEST_ASSERT(pixels == resizedColor && resizedDepth.size() == 128u * 128u,
              "Restoring depth must preserve color and publish depth again");

  // 5b. Test zero-size target behavior: must return NOT_READY, not UNSUPPORTED
  std::cout << "Testing zero-size target behavior..." << std::endl;
  target->resize(SbVec2i32(0, 0));
  TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_NOT_READY,
              "Target must enter TARGET_NOT_READY on size 0x0");
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::NOT_READY,
              "apply() on zero-size target must return NOT_READY, not UNSUPPORTED");
  TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_NOT_READY,
              "Target remains TARGET_NOT_READY after zero-size apply");

  // Restore valid size
  TEST_ASSERT(target->resize(SbVec2i32(128, 128)), "Restoring valid size must succeed");
  action.setViewportRegion(SbViewportRegion(128, 128));
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS,
              "apply() must succeed after restoring valid size");
  TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_READY,
              "Target returns to TARGET_READY after valid apply");

  // 6. Test portable wide-line expansion.
  SoSeparator * unsupportedSep = new SoSeparator;
  unsupportedSep->ref();
  unsupportedSep->addChild(cone);

  SoDrawStyle * ds = new SoDrawStyle;
  ds->lineWidth = 2.0f;
  unsupportedSep->addChild(ds);
  SoCoordinate3 * coords = new SoCoordinate3;
  coords->point.set1Value(0, 0, 0, 0);
  coords->point.set1Value(1, 1, 1, 1);
  unsupportedSep->addChild(coords);
  unsupportedSep->addChild(new SoLineSet);

  action.apply(unsupportedSep);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "lineWidth > 1.0 must render through portable expansion");

  unsupportedSep->unref();

  // 7. Test invalid resize allocation guards
  TEST_ASSERT(!target->resize(SbVec2i32(-10, 50)), "Negative size resize must fail");
  TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_ERROR, "Status must be TARGET_ERROR after negative resize");
  TEST_ASSERT(!target->resize(SbVec2i32(100000, 100000)), "Excessive dimension resize must fail");
  TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_ERROR, "Status must be TARGET_ERROR after excessive resize");

  // Restore target to valid state
  TEST_ASSERT(target->resize(SbVec2i32(128, 128)), "Restore resize must succeed");
  TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_READY, "Status must be TARGET_READY");

  // 8. Test Wave 1A profile rejection of per-vertex material overrides (Section 4.6)
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
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS,
              "apply() on per-vertex material scene must succeed in Wave 2A profile");

  triRoot->unref();

  // 9. Test multi-draw scene rendering
  SoSeparator * multiRoot = new SoSeparator;
  multiRoot->ref();

  SoOrthographicCamera * multiCam = new SoOrthographicCamera;
  multiCam->position.setValue(0.0f, 0.0f, 5.0f);
  multiCam->height = 4.0f;
  multiCam->nearDistance = 0.1f;
  multiCam->farDistance = 10.0f;
  multiRoot->addChild(multiCam);

  SoDirectionalLight * multiLight = new SoDirectionalLight;
  multiLight->direction.setValue(0.0f, 0.0f, -1.0f);
  multiRoot->addChild(multiLight);

  // Left object: Red cube
  SoSeparator * leftSep = new SoSeparator;
  SoTranslation * leftTrans = new SoTranslation;
  leftTrans->translation.setValue(-1.0f, 0.0f, 0.0f);
  leftSep->addChild(leftTrans);
  SoMaterial * leftMat = new SoMaterial;
  leftMat->diffuseColor.setValue(1.0f, 0.0f, 0.0f);
  leftMat->ambientColor.setValue(0.2f, 0.0f, 0.0f);
  leftSep->addChild(leftMat);
  SoCube * leftCube = new SoCube;
  leftCube->width = 0.8f;
  leftCube->height = 0.8f;
  leftCube->depth = 0.8f;
  leftSep->addChild(leftCube);
  multiRoot->addChild(leftSep);

  // Right object: Green cube
  SoSeparator * rightSep = new SoSeparator;
  SoTranslation * rightTrans = new SoTranslation;
  rightTrans->translation.setValue(1.0f, 0.0f, 0.0f);
  rightSep->addChild(rightTrans);
  SoMaterial * rightMat = new SoMaterial;
  rightMat->diffuseColor.setValue(0.0f, 1.0f, 0.0f);
  rightMat->ambientColor.setValue(0.0f, 0.2f, 0.0f);
  rightSep->addChild(rightMat);
  SoCube * rightCube = new SoCube;
  rightCube->width = 0.8f;
  rightCube->height = 0.8f;
  rightCube->depth = 0.8f;
  rightSep->addChild(rightCube);
  multiRoot->addChild(rightSep);

  action.setViewportRegion(SbViewportRegion(128, 128));
  action.apply(multiRoot);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Multi-draw scene must render successfully");

  target->getPimpl()->readbackRGBA(pixels);
  size_t leftIdx = (64 * 128 + 32) * 4;
  TEST_ASSERT(pixels[leftIdx + 0] > 100 && pixels[leftIdx + 1] < 50, "Left object must be Red");

  size_t rightIdx = (64 * 128 + 96) * 4;
  TEST_ASSERT(pixels[rightIdx + 1] > 100 && pixels[rightIdx + 0] < 50, "Right object must be Green");

  multiRoot->unref();

  // 10. Test depth occlusion (Depth Test validation)
  SoSeparator * depthRoot = new SoSeparator;
  depthRoot->ref();

  SoOrthographicCamera * depthCam = new SoOrthographicCamera;
  depthCam->ref();
  depthCam->position.setValue(0.0f, 0.0f, 5.0f);
  depthCam->height = 2.0f;
  depthCam->nearDistance = 0.1f;
  depthCam->farDistance = 10.0f;
  depthRoot->addChild(depthCam);

  // Back triangle (z = -2.0, Green)
  SoSeparator * backSep = new SoSeparator;
  backSep->ref();
  SoTranslation * backTrans = new SoTranslation;
  backTrans->translation.setValue(0.0f, 0.0f, -2.0f);
  backSep->addChild(backTrans);
  SoMaterial * backMat = new SoMaterial;
  backMat->diffuseColor.setValue(0.0f, 1.0f, 0.0f);
  backMat->ambientColor.setValue(0.0f, 0.5f, 0.0f);
  backSep->addChild(backMat);
  SoCoordinate3 * backCoords = new SoCoordinate3;
  backCoords->point.set1Value(0, SbVec3f(-0.5f, -0.5f, 0.0f));
  backCoords->point.set1Value(1, SbVec3f(0.5f, -0.5f, 0.0f));
  backCoords->point.set1Value(2, SbVec3f(0.0f, 0.5f, 0.0f));
  backSep->addChild(backCoords);
  SoIndexedFaceSet * backIfs = new SoIndexedFaceSet;
  backIfs->coordIndex.setValues(0, 4, triIndices);
  backSep->addChild(backIfs);
  depthRoot->addChild(backSep);

  // Front triangle (z = 0.0, Red)
  SoSeparator * frontSep = new SoSeparator;
  frontSep->ref();
  SoTranslation * frontTrans = new SoTranslation;
  frontTrans->translation.setValue(0.0f, 0.0f, 0.0f);
  frontSep->addChild(frontTrans);
  SoMaterial * frontMat = new SoMaterial;
  frontMat->diffuseColor.setValue(1.0f, 0.0f, 0.0f);
  frontMat->ambientColor.setValue(0.5f, 0.0f, 0.0f);
  frontSep->addChild(frontMat);
  SoCoordinate3 * frontCoords = new SoCoordinate3;
  frontCoords->point.set1Value(0, SbVec3f(-0.5f, -0.5f, 0.0f));
  frontCoords->point.set1Value(1, SbVec3f(0.5f, -0.5f, 0.0f));
  frontCoords->point.set1Value(2, SbVec3f(0.0f, 0.5f, 0.0f));
  frontSep->addChild(frontCoords);
  SoIndexedFaceSet * frontIfs = new SoIndexedFaceSet;
  frontIfs->coordIndex.setValues(0, 4, triIndices);
  frontSep->addChild(frontIfs);
  depthRoot->addChild(frontSep);

  action.apply(depthRoot);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Depth test scene 1 must render successfully");

  target->getPimpl()->readbackRGBA(pixels);
  size_t centerPixel = (64 * 128 + 64) * 4;
  TEST_ASSERT(pixels[centerPixel + 0] > 18 && pixels[centerPixel + 1] < 50,
              "Front red triangle must be visible over back green triangle");

  // Invert order on separate root2: draw Front first, Back second
  SoSeparator * depthRoot2 = new SoSeparator;
  depthRoot2->ref();
  depthRoot2->addChild(depthCam);
  depthRoot2->addChild(frontSep);
  depthRoot2->addChild(backSep);

  action.apply(depthRoot2);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Depth test scene 2 must render successfully");

  target->getPimpl()->readbackRGBA(pixels);
  TEST_ASSERT(pixels[centerPixel + 0] > 18 && pixels[centerPixel + 1] < 50,
              "Depth buffering must occlude back green triangle even when drawn second");

  depthCam->unref();
  frontSep->unref();
  backSep->unref();
  depthRoot->unref();
  depthRoot2->unref();

  // 11. Test culling and winding via SoShapeHints
  std::cout << "Testing culling and winding via SoShapeHints..." << std::endl;
  SoSeparator * hintsRoot = new SoSeparator;
  hintsRoot->ref();

  SoOrthographicCamera * hintsCam = new SoOrthographicCamera;
  hintsCam->position.setValue(0.0f, 0.0f, 5.0f);
  hintsCam->height = 2.0f;
  hintsRoot->addChild(hintsCam);

  SoShapeHints * hints = new SoShapeHints;
  hints->vertexOrdering = SoShapeHints::COUNTERCLOCKWISE;
  hints->shapeType = SoShapeHints::SOLID; // Enables backface culling
  hintsRoot->addChild(hints);

  SoMaterial * hintsMat = new SoMaterial;
  hintsMat->diffuseColor.setValue(1.0f, 1.0f, 0.0f); // Yellow
  hintsMat->ambientColor.setValue(0.5f, 0.5f, 0.0f);
  hintsRoot->addChild(hintsMat);

  // Clockwise triangle: should be culled as backface when SOLID + CCW
  SoCoordinate3 * cwCoords = new SoCoordinate3;
  cwCoords->point.set1Value(0, SbVec3f(-0.5f, -0.5f, 0.0f));
  cwCoords->point.set1Value(1, SbVec3f(0.0f, 0.5f, 0.0f));
  cwCoords->point.set1Value(2, SbVec3f(0.5f, -0.5f, 0.0f));
  hintsRoot->addChild(cwCoords);

  SoIndexedFaceSet * cwIfs = new SoIndexedFaceSet;
  cwIfs->coordIndex.setValues(0, 4, triIndices);
  hintsRoot->addChild(cwIfs);

  action.apply(hintsRoot);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Culling scene 1 should apply successfully");

  target->getPimpl()->readbackRGBA(pixels);
  // CW triangle must be culled when CCW+SOLID is configured -> background remains
  TEST_ASSERT(pixels[centerPixel + 0] < 30 && pixels[centerPixel + 1] < 30,
              "CW triangle must be backface culled when vertexOrdering is CCW and shape is SOLID");

  // Disable culling by setting shapeType to UNKNOWN_SHAPE_TYPE
  hints->shapeType = SoShapeHints::UNKNOWN_SHAPE_TYPE;
  action.apply(hintsRoot);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Non-solid culling scene should apply successfully");

  target->getPimpl()->readbackRGBA(pixels);
  TEST_ASSERT(pixels[centerPixel + 0] > 18 && pixels[centerPixel + 1] > 18,
              "Triangle must NOT be culled when shapeType is UNKNOWN_SHAPE_TYPE (two-sided)");

  hintsRoot->unref();

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  char adapterName[256] = {0};
  coin_wgpu_get_adapter_info(adapterName, sizeof(adapterName));
  std::cout << "Detected WebGPU Adapter: " << adapterName << std::endl;

  // 12. Fault injection and state machine lifecycle tests (Section 4.5)
  std::cout << "Testing fault injection and state machine lifecycle..." << std::endl;

  // Test 12.1: Isolated NOT_READY
  coin_wgpu_inject_fault(COIN_WGPU_NOT_READY);
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::NOT_READY,
              "Injected NOT_READY fault must propagate to action status");
  TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_NOT_READY,
              "Target must transition to retryable TARGET_NOT_READY");

  // Recovery from NOT_READY
  coin_wgpu_inject_fault(0);
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS,
              "Target must recover from TARGET_NOT_READY on subsequent apply");
  TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_READY,
              "Target must return to TARGET_READY after successful recovery");

  // Test 12.2: Isolated OUT_OF_MEMORY -> TARGET_ERROR (terminal for target)
  coin_wgpu_inject_fault(COIN_WGPU_OUT_OF_MEMORY);
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY,
              "Injected OUT_OF_MEMORY fault must propagate to action status");
  TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_ERROR,
              "Target must transition to TARGET_ERROR on OOM");
  TEST_ASSERT(target->getLastError() != nullptr && target->getLastError()[0] != '\0',
              "Target getLastError() must have diagnostic on OOM");

  coin_wgpu_inject_fault(0);
  // Subsequent apply on target in TARGET_ERROR must report BACKEND_ERROR
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::BACKEND_ERROR,
              "Subsequent apply on TARGET_ERROR must report BACKEND_ERROR");

  // Re-create target for subsequent tests
  delete target;
  target = CoinRenderTarget::createOffscreen(SbVec2i32(128, 128));
  action.setRenderTarget(target);

  // Test 12.3: Isolated DEVICE_LOST
  coin_wgpu_inject_fault(COIN_WGPU_DEVICE_LOST);
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::DEVICE_LOST,
              "Injected DEVICE_LOST fault must propagate to action status");
  TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_LOST,
              "Target must enter TARGET_LOST state on device lost");

  // Recovery: next apply recycles backend (READY -> LOST -> RECREATING -> READY)
  coin_wgpu_inject_fault(0);
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS,
              "Target must transparently recreate backend and succeed on apply after DEVICE_LOST");
  TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_READY,
              "Target must return to TARGET_READY after automatic recovery");

  // Test 12.4: Sequential test NOT_READY -> OUT_OF_MEMORY (terminal) -> fresh target DEVICE_LOST -> SUCCESS
  std::cout << "Testing sequential fault recovery: NOT_READY -> OOM -> DEVICE_LOST -> SUCCESS..." << std::endl;
  coin_wgpu_inject_fault(COIN_WGPU_NOT_READY);
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::NOT_READY, "Step 1: NOT_READY failed");

  coin_wgpu_inject_fault(COIN_WGPU_OUT_OF_MEMORY);
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY, "Step 2: OUT_OF_MEMORY failed");
  TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_ERROR, "Step 2: Target must enter TARGET_ERROR on OOM");

  // Terminal state verification: subsequent apply without recreating target must fail with BACKEND_ERROR
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::BACKEND_ERROR, "Step 2b: apply on TARGET_ERROR must report BACKEND_ERROR");

  // Re-create target for DEVICE_LOST step
  delete target;
  target = CoinRenderTarget::createOffscreen(SbVec2i32(128, 128));
  action.setRenderTarget(target);

  coin_wgpu_inject_fault(COIN_WGPU_DEVICE_LOST);
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::DEVICE_LOST, "Step 3: DEVICE_LOST failed");

  coin_wgpu_inject_fault(0);
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Step 4: Full sequential recovery failed");
  TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_READY, "Step 4: Target state not READY after recovery");

  // Test 12.5: Async DEVICE_LOST during GPU execution (exercises deadlock-free poll / map error handling)
  std::cout << "Testing async DEVICE_LOST during GPU execution..." << std::endl;
  coin_wgpu_inject_async_fault(COIN_WGPU_DEVICE_LOST);
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::DEVICE_LOST,
              "Async DEVICE_LOST must propagate to action status");
  TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_LOST,
              "Target must enter TARGET_LOST state on async device lost");

  // Recovery: next apply recycles backend (READY -> LOST -> RECREATING -> READY)
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS,
              "Target must transparently recreate backend and succeed on apply after async DEVICE_LOST");
  TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_READY,
              "Target must return to TARGET_READY after automatic recovery from async fault");

  // Test 12.6: FFI boundary rejection (ABI version mismatch & invalid arguments)
  std::cout << "Testing FFI boundary rejection..." << std::endl;
  CoinWgpuTarget tPod{};
  tPod.width = 128;
  tPod.height = 128;
  std::vector<uint8_t> dummyBuf(128 * 128 * 4, 0);
  tPod.color_buffer = dummyBuf.data();
  tPod.color_buffer_len = dummyBuf.size();

  CoinWgpuFrameView fView{};
  fView.abi_version = 999; // Invalid ABI version
  fView.struct_size = sizeof(CoinWgpuFrameView);
  fView.width = 128;
  fView.height = 128;
  char errBuf[256] = {0};

  CoinWgpuStatus ffiStatus = coin_wgpu_submit(&tPod, &fView, errBuf, sizeof(errBuf));
  TEST_ASSERT(ffiStatus == COIN_WGPU_INVALID_ARGUMENT, "Invalid ABI version must return INVALID_ARGUMENT");
#endif

  delete target;
  root->unref();

  std::cout << "All CoinRenderOffscreen tests PASSED!" << std::endl;
  return 0;
}
