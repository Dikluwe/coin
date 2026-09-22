#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/C/basic.h>
#include <Inventor/SoDB.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoCube.h>

#include "rendering/wgpu/SoWgpuRenderTargetP.h"
#include "rendering/wgpu/SoWgpuBackend.h"

#if defined(HAVE_WGPU_RUST_BRIDGE)
#include "rendering/wgpu/SoWgpuRustBackend.h"
#include "rendering/wgpu/coin_wgpu_ffi.h"
#endif

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
  SoWgpuRenderAction::initClass();

  std::cout << "Running WgpuGateG1Test (Onda 2B Backend WebGPU Real)..." << std::endl;

  // =========================================================================
  // G1.1: GPU Backend Availability & Initialization
  // =========================================================================
  {
    std::cout << "-> Test G1.1: WebGPU backend availability query..." << std::endl;
#if defined(HAVE_WGPU_RUST_BRIDGE)
    TEST_ASSERT(SoWgpuRenderAction::isGpuBackendAvailable(),
                "isGpuBackendAvailable() must return TRUE when compiled with real Rust/WGPU bridge");
#endif
    std::cout << "   [PASS] G1.1 WebGPU backend availability" << std::endl;
  }

  // =========================================================================
  // G1.2: Offscreen Triangle Rendering with Color and Depth Readback
  // =========================================================================
  {
    std::cout << "-> Test G1.2: Triangle offscreen color and depth readback..." << std::endl;
    SoSeparator * root = new SoSeparator;
    root->ref();

    SoPerspectiveCamera * cam = new SoPerspectiveCamera;
    cam->position.setValue(0.0f, 0.0f, 5.0f);
    cam->pointAt(SbVec3f(0.0f, 0.0f, 0.0f));
    cam->nearDistance = 1.0f;
    cam->farDistance = 10.0f;
    root->addChild(cam);

    SoDirectionalLight * light = new SoDirectionalLight;
    light->direction.setValue(0.0f, 0.0f, -1.0f);
    root->addChild(light);

    SoMaterial * mat = new SoMaterial;
    mat->diffuseColor.setValue(1.0f, 0.0f, 0.0f); // Red
    mat->ambientColor.setValue(0.2f, 0.0f, 0.0f);
    root->addChild(mat);

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.set1Value(0, SbVec3f(-1.0f, -1.0f, 0.0f));
    coords->point.set1Value(1, SbVec3f( 1.0f, -1.0f, 0.0f));
    coords->point.set1Value(2, SbVec3f( 0.0f,  1.0f, 0.0f));
    root->addChild(coords);

    SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
    const int32_t indices[] = {0, 1, 2, -1};
    ifs->coordIndex.setValues(0, 4, indices);
    root->addChild(ifs);

    SoWgpuRenderAction action(SbViewportRegion(64, 64));
    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
    action.setRenderTarget(target);
    action.apply(root);

    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
                "Offscreen rendering of triangle scene must succeed");

    // 1. Color readback
    std::vector<uint8_t> pixels;
    target->readbackRGBA(pixels);
    TEST_ASSERT(pixels.size() == 64 * 64 * 4, "Color buffer size must be width * height * 4");

    // Corner pixel (outside triangle) must be clear color (black)
    size_t cornerIdx = (5 * 64 + 5) * 4;
    TEST_ASSERT(pixels[cornerIdx + 0] == 0 && pixels[cornerIdx + 1] == 0 && pixels[cornerIdx + 2] == 0,
                "Background corner pixel must remain clear color");

    // Center pixel (inside triangle) must have dominant red color
    size_t centerIdx = (32 * 64 + 32) * 4;
    TEST_ASSERT(pixels[centerIdx + 0] > 150 && pixels[centerIdx + 1] < 50 && pixels[centerIdx + 2] < 50,
                "Center pixel inside triangle must have dominant red component");

    // 2. Depth readback
    std::vector<float> depths;
    target->readbackDepth(depths);
    TEST_ASSERT(depths.size() == 64 * 64, "Depth buffer size must be width * height");

    // Clear depth in background must be exactly 1.0
    float bgDepth = depths[5 * 64 + 5];
    TEST_ASSERT(std::abs(bgDepth - 1.0f) < 1e-4f, "Background depth must be 1.0f (clear value)");

    // Triangle depth at center must be in (0.0, 1.0)
    float triDepth = depths[32 * 64 + 32];
    TEST_ASSERT(triDepth > 0.0f && triDepth < 0.99f,
                "Triangle depth at center must be within depth range (0.0, 1.0)");

    delete target;
    root->unref();
    std::cout << "   [PASS] G1.2 Triangle offscreen color and depth readback" << std::endl;
  }

  // =========================================================================
  // G1.3: Depth Buffer Occlusion Order Verification
  // =========================================================================
  {
    std::cout << "-> Test G1.3: Depth buffer occlusion ordering..." << std::endl;
    // Front triangle: Red at z = 1.0
    // Back triangle: Green at z = -1.0
    // Camera looking down -Z from (0, 0, 5)
    // The front red triangle must occlude the back green triangle regardless of draw order.

    SoPerspectiveCamera * cam = new SoPerspectiveCamera;
    cam->position.setValue(0.0f, 0.0f, 5.0f);
    cam->pointAt(SbVec3f(0.0f, 0.0f, 0.0f));
    cam->nearDistance = 1.0f;
    cam->farDistance = 10.0f;

    SoDirectionalLight * light = new SoDirectionalLight;
    light->direction.setValue(0.0f, 0.0f, -1.0f);

    // Front triangle (Red, z = 1.0)
    SoSeparator * frontSep = new SoSeparator;
    frontSep->ref();
    SoMaterial * redMat = new SoMaterial;
    redMat->diffuseColor.setValue(1.0f, 0.0f, 0.0f);
    frontSep->addChild(redMat);
    SoCoordinate3 * frontCoords = new SoCoordinate3;
    frontCoords->point.set1Value(0, SbVec3f(-1.0f, -1.0f, 1.0f));
    frontCoords->point.set1Value(1, SbVec3f( 1.0f, -1.0f, 1.0f));
    frontCoords->point.set1Value(2, SbVec3f( 0.0f,  1.0f, 1.0f));
    frontSep->addChild(frontCoords);
    SoIndexedFaceSet * frontIfs = new SoIndexedFaceSet;
    const int32_t fIdx[] = {0, 1, 2, -1};
    frontIfs->coordIndex.setValues(0, 4, fIdx);
    frontSep->addChild(frontIfs);

    // Back triangle (Green, z = -1.0)
    SoSeparator * backSep = new SoSeparator;
    backSep->ref();
    SoMaterial * greenMat = new SoMaterial;
    greenMat->diffuseColor.setValue(0.0f, 1.0f, 0.0f);
    backSep->addChild(greenMat);
    SoCoordinate3 * backCoords = new SoCoordinate3;
    backCoords->point.set1Value(0, SbVec3f(-1.0f, -1.0f, -1.0f));
    backCoords->point.set1Value(1, SbVec3f( 1.0f, -1.0f, -1.0f));
    backCoords->point.set1Value(2, SbVec3f( 0.0f,  1.0f, -1.0f));
    backSep->addChild(backCoords);
    SoIndexedFaceSet * backIfs = new SoIndexedFaceSet;
    const int32_t bIdx[] = {0, 1, 2, -1};
    backIfs->coordIndex.setValues(0, 4, bIdx);
    backSep->addChild(backIfs);

    // Case 1: Draw Back (green) first, then Front (red) second
    SoSeparator * root1 = new SoSeparator;
    root1->ref();
    root1->addChild(cam);
    root1->addChild(light);
    root1->addChild(backSep);
    root1->addChild(frontSep);

    SoWgpuRenderAction action(SbViewportRegion(64, 64));
    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
    action.setRenderTarget(target);
    action.apply(root1);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Occlusion scene 1 must render");

    std::vector<uint8_t> pixels1;
    target->readbackRGBA(pixels1);
    size_t cIdx = (32 * 64 + 32) * 4;
    TEST_ASSERT(pixels1[cIdx + 0] > 150 && pixels1[cIdx + 1] < 50,
                "Front red triangle must be visible over back green triangle (drawn back first)");

    std::vector<float> depths1;
    target->readbackDepth(depths1);
    float depthVal1 = depths1[32 * 64 + 32];

    // Case 2: Invert scene order: Draw Front (red) first, then Back (green) second
    SoSeparator * root2 = new SoSeparator;
    root2->ref();
    root2->addChild(cam);
    root2->addChild(light);
    root2->addChild(frontSep);
    root2->addChild(backSep);

    action.apply(root2);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Occlusion scene 2 must render");

    std::vector<uint8_t> pixels2;
    target->readbackRGBA(pixels2);
    TEST_ASSERT(pixels2[cIdx + 0] > 150 && pixels2[cIdx + 1] < 50,
                "Depth test must prevent back green triangle from overwriting front red triangle");

    std::vector<float> depths2;
    target->readbackDepth(depths2);
    float depthVal2 = depths2[32 * 64 + 32];

    // Both cases must yield the identical front depth
    TEST_ASSERT(std::abs(depthVal1 - depthVal2) < 1e-4f,
                "Depth values at center pixel must match between both draw orders");

    delete target;
    frontSep->unref();
    backSep->unref();
    root1->unref();
    root2->unref();
    std::cout << "   [PASS] G1.3 Depth buffer occlusion ordering" << std::endl;
  }

  // =========================================================================
  // G1.4: Monotonic Submission Serial Verification
  // =========================================================================
  {
    std::cout << "-> Test G1.4: Monotonic submission serials..." << std::endl;
    SoSeparator * root = new SoSeparator;
    root->ref();

    SoCube * cube = new SoCube;
    root->addChild(cube);

    SoWgpuRenderAction action(SbViewportRegion(64, 64));
    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
    action.setRenderTarget(target);

    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Frame 1 must succeed");
    uint64_t serial1 = target->getLastSubmissionSerial();
    TEST_ASSERT(serial1 > 0, "Submission serial must be non-zero after successful submit");

    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Frame 2 must succeed");
    uint64_t serial2 = target->getLastSubmissionSerial();
    TEST_ASSERT(serial2 > serial1, "Submission serial must strictly increment on each new frame");

    delete target;
    root->unref();
    std::cout << "   [PASS] G1.4 Monotonic submission serials" << std::endl;
  }

  // =========================================================================
  // G1.5: Typed Error Distinction and Recovery (G1 Contract)
  // =========================================================================
  {
    std::cout << "-> Test G1.5: Status distinction and DEVICE_LOST recovery..." << std::endl;
    SoSeparator * root = new SoSeparator;
    root->ref();
    root->addChild(new SoCube);

    SoWgpuRenderAction action(SbViewportRegion(64, 64));
    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
    action.setRenderTarget(target);

    // Initial frame succeeds
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Initial frame must succeed");

#if defined(HAVE_WGPU_RUST_BRIDGE)
    // Inject synchronous DEVICE_LOST fault
    coin_wgpu_inject_fault(COIN_WGPU_DEVICE_LOST);
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::DEVICE_LOST,
                "Injected DEVICE_LOST must be reported typed as SoWgpuRenderAction::DEVICE_LOST");
    TEST_ASSERT(target->getStatus() == SoWgpuRenderTarget::TARGET_LOST,
                "Target must enter TARGET_LOST state on DEVICE_LOST");

    // Next frame must transparently recover and succeed
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
                "Target must transparently recreate backend and succeed on apply after DEVICE_LOST");
    TEST_ASSERT(target->getStatus() == SoWgpuRenderTarget::TARGET_READY,
                "Target must return to TARGET_READY after successful recovery");
#endif

    delete target;
    root->unref();
    std::cout << "   [PASS] G1.5 Status distinction and recovery" << std::endl;
  }

  std::cout << "\n=======================================================" << std::endl;
  std::cout << "All WgpuGateG1Test test suites PASSED successfully!" << std::endl;
  std::cout << "=======================================================" << std::endl;
  return 0;
}
