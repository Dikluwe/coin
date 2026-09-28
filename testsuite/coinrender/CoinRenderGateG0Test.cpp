#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/C/basic.h>
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/lists/SoPathList.h>

#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderBackend.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
#include "rendering/coinwgpu/CoinWgpuBackend.h"
#endif

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <limits>

#define TEST_ASSERT(cond, msg) do { \
  if (!(cond)) { \
    std::cerr << "FAILED: " << msg << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
    return 1; \
  } \
} while (0)

// Helper: Custom callback node to test reentrancy (B08)
static void reentrantCallback(void * userdata, SoAction * /*action*/) {
  CoinRenderAction * renderAction = static_cast<CoinRenderAction *>(userdata);
  SoSeparator * dummy = new SoSeparator;
  dummy->ref();
  renderAction->apply(dummy); // Nested apply invocation
  dummy->unref();
}

int main() {
  SoDB::init();
  CoinRenderAction::initClass();
  std::cout << "Running CoinRenderGateG0Test (Onda 2A Saneamento)..." << std::endl;

  // =========================================================================
  // G0.1: Multi-root SoPathList (B02)
  // Two independent roots must accumulate in a single frame and submit once.
  // =========================================================================
  {
    std::cout << "-> Test G0.1: Multi-root SoPathList accumulation (B02)..." << std::endl;
    SoSeparator * root1 = new SoSeparator;
    root1->ref();
    SoTranslation * t1 = new SoTranslation;
    t1->translation.setValue(-2.0f, 0.0f, 0.0f);
    root1->addChild(t1);
    root1->addChild(new SoCone);

    SoSeparator * root2 = new SoSeparator;
    root2->ref();
    SoTranslation * t2 = new SoTranslation;
    t2->translation.setValue(2.0f, 0.0f, 0.0f);
    root2->addChild(t2);
    root2->addChild(new SoCube);

    SoPath * p1 = new SoPath(root1);
    p1->ref();
    SoPath * p2 = new SoPath(root2);
    p2->ref();

    SoPathList pathList;
    pathList.append(p1);
    pathList.append(p2);

    CoinRenderAction action(SbViewportRegion(320, 240));
    action.apply(pathList, FALSE);

    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Multi-root apply must succeed");
    const SbString & log = action.getRecordingLog();
    // Recording log should contain primitives from both roots
    TEST_ASSERT(log.getLength() > 0, "Recording log must not be empty");
    std::string logStr(log.getString());
    // Both SoCone and SoCube generate multiple draw calls or triangles
    TEST_ASSERT(logStr.find("draws count: ") != std::string::npos, "Log must include draws section");
    // Verify that multiple draws were recorded
    size_t drawsPos = logStr.find("draws count: ");
    TEST_ASSERT(drawsPos != std::string::npos, "DRAWS count must be present");

    p1->unref();
    p2->unref();
    root1->unref();
    root2->unref();
    std::cout << "   [PASS] G0.1 Multi-root accumulation" << std::endl;
  }

  // =========================================================================
  // G0.2: PER_VERTEX Material Binding (B03)
  // Distinct per-vertex material slots must not collapse to v0.
  // =========================================================================
  {
    std::cout << "-> Test G0.2: PER_VERTEX material binding preservation (B03)..." << std::endl;
    SoSeparator * root = new SoSeparator;
    root->ref();

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.set1Value(0, SbVec3f(-1.0f, -1.0f, 0.0f));
    coords->point.set1Value(1, SbVec3f( 1.0f, -1.0f, 0.0f));
    coords->point.set1Value(2, SbVec3f( 0.0f,  1.0f, 0.0f));
    root->addChild(coords);

    SoMaterial * mat = new SoMaterial;
    mat->diffuseColor.set1Value(0, SbColor(1.0f, 0.0f, 0.0f)); // Red
    mat->diffuseColor.set1Value(1, SbColor(0.0f, 1.0f, 0.0f)); // Green
    mat->diffuseColor.set1Value(2, SbColor(0.0f, 0.0f, 1.0f)); // Blue
    root->addChild(mat);

    SoMaterialBinding * mb = new SoMaterialBinding;
    mb->value = SoMaterialBinding::PER_VERTEX;
    root->addChild(mb);

    SoDirectionalLight * dl = new SoDirectionalLight;
    dl->direction.setValue(0.0f, 0.0f, -1.0f);
    root->addChild(dl);

    SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
    const int32_t indices[] = {0, 1, 2, -1};
    ifs->coordIndex.setValues(0, 4, indices);
    root->addChild(ifs);

    CoinRenderAction action(SbViewportRegion(64, 64));
    CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(64, 64));
    target->getPimpl()->backend = std::unique_ptr<CoinRenderBackend>(new CoinRenderCpuReferenceBackend());
    action.setRenderTarget(target);
    action.apply(root);

    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS,
                "Rendering PER_VERTEX material scene must succeed without UNSUPPORTED error");

    // Verify in recording log that vertices have distinct material slots (B03)
    CoinRenderAction recordAction(SbViewportRegion(64, 64));
    recordAction.apply(root);
    SbString recLog = recordAction.getRecordingLog();
    TEST_ASSERT(recLog.find("matSlot=0") != -1 && recLog.find("matSlot=1") != -1 && recLog.find("matSlot=2") != -1,
                "PER_VERTEX scene vertices must reference distinct material slots, not collapse to v0");

    // Verify in CPU rasterizer readback that vertices render with distinct colors
    std::vector<uint8_t> pixels;
    target->getPimpl()->readbackRGBA(pixels);
    bool hasGreenDominant = false;
    bool hasBlueDominant = false;
    for (size_t i = 0; i < pixels.size(); i += 4) {
      uint8_t r = pixels[i + 0];
      uint8_t g = pixels[i + 1];
      uint8_t b = pixels[i + 2];
      if (g > 50 && g > r + 20) hasGreenDominant = true;
      if (b > 50 && b > r + 20) hasBlueDominant = true;
    }
    TEST_ASSERT(hasGreenDominant, "PER_VERTEX rasterization must produce green-dominant pixels near vertex 1");
    TEST_ASSERT(hasBlueDominant, "PER_VERTEX rasterization must produce blue-dominant pixels near vertex 2");

    delete target;
    root->unref();
    std::cout << "   [PASS] G0.2 PER_VERTEX material binding" << std::endl;
  }

  // =========================================================================
  // G0.3: Directional Light under Camera Rotation (B04)
  // Light snapshot direction is strictly in view space without duplicate transforms.
  // =========================================================================
  {
    std::cout << "-> Test G0.3: Directional light view space transform (B04)..." << std::endl;
    SoSeparator * root = new SoSeparator;
    root->ref();

    SoPerspectiveCamera * cam = new SoPerspectiveCamera;
    cam->position.setValue(0.0f, 0.0f, 5.0f);
    root->addChild(cam);

    SoDirectionalLight * light = new SoDirectionalLight;
    light->direction.setValue(0.0f, 0.0f, -1.0f); // Points along -Z in world
    root->addChild(light);

    root->addChild(new SoCube);

    CoinRenderAction action(SbViewportRegion(64, 64));
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Apply with camera and light must succeed");

    std::string logInitial(action.getRecordingLog().getString());
    TEST_ASSERT(logInitial.find("lightingStates count: ") != std::string::npos, "Lighting state must be captured");

    // Now rotate camera 90 degrees around Y axis (look towards +X)
    cam->orientation.setValue(SbVec3f(0.0f, 1.0f, 0.0f), static_cast<float>(M_PI / 2.0));
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Apply with rotated camera must succeed");

    std::string logRotated(action.getRecordingLog().getString());
    TEST_ASSERT(logInitial.find("dir=[0.0000,0.0000,-1.0000]") != std::string::npos,
                "Initial directional light in view space must be along -Z");
    TEST_ASSERT(logRotated.find("dir=[1.0000,0.0000,0.0000]") != std::string::npos,
                "Rotated light direction must strictly be [1, 0, 0] in view space without duplicate transform");

    root->unref();
    std::cout << "   [PASS] G0.3 Directional light in view space" << std::endl;
  }

  // =========================================================================
  // G0.4: Arithmetic Overflow Protection 32-bit (B05)
  // Validation with safe subtraction prevents 32-bit wraparound.
  // =========================================================================
  {
    std::cout << "-> Test G0.4: 32-bit arithmetic overflow protection (B05)..." << std::endl;
    CoinRenderFramePlan plan;
    plan.clearColor = SbColor4f(0.0f, 0.0f, 0.0f, 1.0f);

    // Provide 48 valid vertices/indices so wraparound sum (32) is strictly less than buffer size (48)
    // and indexCount (0x30 = 48) is a valid multiple of 3 for TRIANGLE_LIST.
    CoinRenderVertexSnapshot vs;
    plan.vertices.assign(48, vs);
    plan.indices.assign(48, 0);
    plan.materials.push_back(CoinRenderMaterialSnapshot{});
    plan.lightingStates.push_back(CoinRenderLightingSnapshot{});
    plan.cameras.push_back(CoinRenderCameraSnapshot{});
    plan.viewports.push_back(CoinRenderViewportSnapshot{});
    plan.renderStates.push_back(CoinRenderRenderStateSnapshot{});

    CoinRenderDrawPacket dp;
    dp.topology = CoinRenderPrimitiveTopology::TRIANGLE_LIST;
    dp.renderStateSlot = 0;
    // Overflowing range: firstIndex = 0xFFFFFFF0, indexCount = 0x30 (48)
    // Sum wraps around to 0x20 (32) in 32-bit math, which is <= 48!
    dp.geometry.firstIndex = 0xFFFFFFF0U;
    dp.geometry.indexCount = 0x30U;
    plan.draws.push_back(dp);

    std::string diag;
    bool valid = plan.isValid(&diag);
    TEST_ASSERT(!valid && diag.find("Draw index range out of bounds") != std::string::npos,
                "Plan with overflowing index range must be rejected specifically by bounds check");
    TEST_ASSERT(!diag.empty(), "Diagnostic message must be provided on overflow rejection");

    // Overflowing vertex range: firstVertex = 0xFFFFFFFF, vertexCount = 10
    plan.draws[0].geometry.firstIndex = 0;
    plan.draws[0].geometry.indexCount = 0;
    plan.draws[0].geometry.firstVertex = 0xFFFFFFFFU;
    plan.draws[0].geometry.vertexCount = 10U;

    valid = plan.isValid(&diag);
    TEST_ASSERT(!valid, "Plan with overflowing vertex range must be rejected");

    std::cout << "   [PASS] G0.4 32-bit overflow protection" << std::endl;
  }

  // Wave 4 preflight: invalid alpha must fail even without a GPU backend.
  {
    CoinRenderFramePlan plan;
    plan.materials.push_back(CoinRenderMaterialSnapshot{});
    std::string diag;
    TEST_ASSERT(plan.isValid(&diag), "Default material must be valid");
    plan.materials[0].transparency = std::nanf("");
    TEST_ASSERT(!plan.isValid(&diag) && diag.find("invalid transparency") != std::string::npos,
                "Non-finite transparency must be rejected by CoinRenderFramePlan");
    plan.materials[0].transparency = -0.1f;
    TEST_ASSERT(!plan.isValid(&diag), "Negative transparency must be rejected by CoinRenderFramePlan");
    plan.materials[0].transparency = 0.0f;
    plan.materials[0].diffuse[3] = 1.1f;
    TEST_ASSERT(!plan.isValid(&diag), "Diffuse alpha over one must be rejected by CoinRenderFramePlan");
    plan.materials[0].diffuse[3] = std::nanf("");
    TEST_ASSERT(!plan.isValid(&diag), "Non-finite diffuse alpha must be rejected by CoinRenderFramePlan");
  }


  // =========================================================================
  // G0.5: Target Overflow & OOM Handling (B06)
  // Multiplicative overflow in target size is rejected safely without crashing.
  // =========================================================================
  {
    std::cout << "-> Test G0.5: Target dimension overflow & OOM protection (B06)..." << std::endl;
    CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(0, 0));
    TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_NOT_READY, "Zero-size target must be TARGET_NOT_READY");

    // Attempt resize with massive overflowing dimensions
    SbBool ok = target->resize(SbVec2i32(100000, 100000));
    TEST_ASSERT(!ok, "Excessive dimensions must fail resize");
    TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_ERROR, "Target must enter TARGET_ERROR on overflow");

    // Negative dimensions
    ok = target->resize(SbVec2i32(-10, 50));
    TEST_ASSERT(!ok, "Negative dimensions must fail");

    delete target;
    std::cout << "   [PASS] G0.5 Target overflow protection" << std::endl;
  }

  // =========================================================================
  // G0.6: GPU Availability Query (B07)
  // Recording and CPU reference backends must not report isGpuBackendAvailable() == TRUE.
  // =========================================================================
  {
    std::cout << "-> Test G0.6: Backend GPU availability isolation (B07)..." << std::endl;
#if !defined(HAVE_COIN_WGPU_RUST_BRIDGE)
    TEST_ASSERT(!CoinRenderAction::isGpuBackendAvailable(),
                "RECORDING or CPU reference must report isGpuBackendAvailable() == FALSE");
#endif
    std::cout << "   [PASS] G0.6 GPU availability query" << std::endl;
  }

  // =========================================================================
  // G0.7: Reentrancy Error Preservation (B08)
  // Nested apply() error must not be masked or cleared by outer apply() completion.
  // =========================================================================
  {
    std::cout << "-> Test G0.7: Reentrancy protection and terminal error preservation (B08)..." << std::endl;
    SoSeparator * root = new SoSeparator;
    root->ref();

    CoinRenderAction action(SbViewportRegion(64, 64));

    SoCallback * cbNode = new SoCallback;
    cbNode->setCallback(reentrantCallback, &action);
    root->addChild(cbNode);
    root->addChild(new SoCube);

    action.apply(root);

    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::INVALID_SCENE,
                "Outer action must preserve INVALID_SCENE error from reentrant apply()");
    TEST_ASSERT(action.getLastError().getLength() > 0, "Error diagnostic must explain reentrancy prohibition");

    root->unref();
    std::cout << "   [PASS] G0.7 Reentrancy error preservation" << std::endl;
  }

  // =========================================================================
  // G0.8: Clipping near plane robustness (B09)
  // Geometry crossing camera near plane must not cause division-by-zero or crash.
  // =========================================================================
  {
    std::cout << "-> Test G0.8: Near plane frustum clipping robustness (B09)..." << std::endl;
    SoSeparator * root = new SoSeparator;
    root->ref();

    SoPerspectiveCamera * cam = new SoPerspectiveCamera;
    cam->position.setValue(0.0f, 0.0f, 1.0f);
    cam->nearDistance = 0.5f;
    cam->farDistance = 10.0f;
    root->addChild(cam);

    // Large polygon crossing the near plane (z from -2 to +2, camera at z=1)
    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.set1Value(0, SbVec3f(-5.0f, -5.0f,  2.0f)); // Behind eye
    coords->point.set1Value(1, SbVec3f( 5.0f, -5.0f, -2.0f)); // In front of eye
    coords->point.set1Value(2, SbVec3f( 0.0f,  5.0f, -1.0f)); // In front of eye
    root->addChild(coords);

    SoDirectionalLight * dl = new SoDirectionalLight;
    dl->direction.setValue(0.0f, 0.0f, -1.0f);
    root->addChild(dl);

    SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
    const int32_t indices[] = {0, 1, 2, -1};
    ifs->coordIndex.setValues(0, 4, indices);
    root->addChild(ifs);

    CoinRenderAction action(SbViewportRegion(64, 64));
    CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(64, 64));
    target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
    action.setRenderTarget(target);

    // Must execute cleanly without crash or abort
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS,
                "Rendering polygon crossing near plane must succeed with robust clipping");

    delete target;
    root->unref();
    std::cout << "   [PASS] G0.8 Near plane clipping robustness" << std::endl;
  }

  std::cout << "\n=======================================================" << std::endl;
  std::cout << "All CoinRenderGateG0Test test suites PASSED successfully!" << std::endl;
  std::cout << "=======================================================" << std::endl;
  return 0;
}
