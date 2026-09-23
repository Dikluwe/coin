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
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>

#if defined(HAVE_WGPU_RUST_BRIDGE)
#include "rendering/wgpu/coin_wgpu_ffi.h"
#endif

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <string>

#define TEST_ASSERT(cond, msg) do {   if (!(cond)) {     std::cerr << "FAILED: " << msg << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl;     return 1;   } } while (0)

int main() {
  SoDB::init();

  std::cout << "Running WgpuCacheTest (Onda 2E GPU Resource Cache & Safe Retirement)..." << std::endl;

#if !defined(HAVE_WGPU_RUST_BRIDGE)
  std::cout << "WGPU Rust bridge not enabled, skipping test." << std::endl;
  return 0;
#endif

  // =========================================================================
  // Test 1: Warm-up and Zero New Uploads on Static Scene (Critical Requirement)
  // =========================================================================
  {
    std::cout << "-> Test 1: Warm-up and zero new uploads on static scene..." << std::endl;

    SoSeparator * root = new SoSeparator;
    root->ref();

    SoPerspectiveCamera * camera = new SoPerspectiveCamera;
    camera->position.setValue(0.0f, 0.0f, 5.0f);
    root->addChild(camera);

    SoDirectionalLight * light = new SoDirectionalLight;
    light->direction.setValue(0.0f, 0.0f, -1.0f);
    root->addChild(light);

    // Separator for IndexedFaceSet
    SoSeparator * faceSep = new SoSeparator;
    SoCoordinate3 * coordsFace = new SoCoordinate3;
    coordsFace->point.set1Value(0, SbVec3f(-1.0f, -1.0f, 0.0f));
    coordsFace->point.set1Value(1, SbVec3f( 1.0f, -1.0f, 0.0f));
    coordsFace->point.set1Value(2, SbVec3f( 1.0f,  1.0f, 0.0f));
    coordsFace->point.set1Value(3, SbVec3f(-1.0f,  1.0f, 0.0f));
    faceSep->addChild(coordsFace);

    SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
    const int32_t faceIndices[] = {0, 1, 2, 3, -1};
    ifs->coordIndex.setValues(0, 5, faceIndices);
    faceSep->addChild(ifs);
    root->addChild(faceSep);

    // Separator for IndexedLineSet
    SoSeparator * lineSep = new SoSeparator;
    SoCoordinate3 * coordsLine = new SoCoordinate3;
    coordsLine->point.set1Value(0, SbVec3f(-1.5f, -1.5f, 0.5f));
    coordsLine->point.set1Value(1, SbVec3f( 1.5f,  1.5f, 0.5f));
    lineSep->addChild(coordsLine);

    SoIndexedLineSet * ils = new SoIndexedLineSet;
    const int32_t lineIndices[] = {0, 1, -1};
    ils->coordIndex.setValues(0, 3, lineIndices);
    lineSep->addChild(ils);
    root->addChild(lineSep);

    // Create target
    SbVec2i32 size(256, 256);
    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(size);
    TEST_ASSERT(target != NULL, "createOffscreen must return non-NULL");

    SoWgpuRenderAction action(SbViewportRegion(256, 256));
    action.setRenderTarget(target);
    action.setFastPathEnabled(TRUE);

    // Frame 1: Warm-up (initial uploads)
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Frame 1 apply must succeed");

    SoWgpuCacheTelemetry telem1;
    TEST_ASSERT(target->getCacheTelemetry(telem1), "getCacheTelemetry must succeed");
    TEST_ASSERT(telem1.frameUploads >= 2, "Frame 1 must upload at least 2 shapes (IFS + ILS)");
    TEST_ASSERT(telem1.frameUploadedBytes > 0, "Frame 1 must upload > 0 bytes");
    TEST_ASSERT(telem1.activeEntries >= 2, "Frame 1 must have at least 2 active cache entries");

    // Frame 2: Static scene render (must have ZERO uploads!)
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Frame 2 apply must succeed");

    SoWgpuCacheTelemetry telem2;
    TEST_ASSERT(target->getCacheTelemetry(telem2), "getCacheTelemetry must succeed");
    TEST_ASSERT(telem2.frameUploadedBytes == 0, "Frame 2 on static scene MUST upload 0 bytes");
    TEST_ASSERT(telem2.frameUploads == 0, "Frame 2 on static scene MUST have 0 uploads");
    TEST_ASSERT(telem2.frameHits >= 2, "Frame 2 must record cache hits for both shapes");
    TEST_ASSERT(telem2.cumulativeHits >= 2, "Cumulative hits must increase");

    // Frame 3: Another static scene render (confirming continued stability)
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Frame 3 apply must succeed");

    SoWgpuCacheTelemetry telem3;
    TEST_ASSERT(target->getCacheTelemetry(telem3), "getCacheTelemetry must succeed");
    TEST_ASSERT(telem3.frameUploadedBytes == 0, "Frame 3 on static scene MUST upload 0 bytes");
    TEST_ASSERT(telem3.frameUploads == 0, "Frame 3 on static scene MUST have 0 uploads");

    // =======================================================================
    // Test 2: Selective Invalidation by In-Place Mutation (set1Value)
    // =======================================================================
    std::cout << "-> Test 2: Selective invalidation by in-place mutation..." << std::endl;

    // Mutate only coordsFace (IFS), leaving coordsLine (ILS) completely untouched
    coordsFace->point.set1Value(0, SbVec3f(-1.2f, -1.2f, 0.0f));

    // Frame 4: Render after mutation
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Frame 4 apply must succeed");

    SoWgpuCacheTelemetry telem4;
    TEST_ASSERT(target->getCacheTelemetry(telem4), "getCacheTelemetry must succeed");
    TEST_ASSERT(telem4.frameUploads == 1, "Frame 4 must upload exactly 1 shape (the mutated IFS)");
    TEST_ASSERT(telem4.frameHits == 1, "Frame 4 must hit cache for the unmutated ILS");
    TEST_ASSERT(telem4.frameUploadedBytes > 0, "Frame 4 must upload bytes for mutated IFS");

    // =======================================================================
    // Test 3: Safe Asynchronous Resource Retirement and Serial Drainage
    // =======================================================================
    std::cout << "-> Test 3: Safe asynchronous resource retirement and serial drainage..." << std::endl;

    // During frame 4, the old IFS revision was retired into deferred_release.
    // In offscreen readback, poll(Wait) finishes the work, so on next frame or poll,
    // retired entries whose retired_at_serial <= completed_serial are drained.
    target->pollDevice();

    // Render Frame 5 (static)
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Frame 5 apply must succeed");

    SoWgpuCacheTelemetry telem5;
    TEST_ASSERT(target->getCacheTelemetry(telem5), "getCacheTelemetry must succeed");
    TEST_ASSERT(telem5.frameUploadedBytes == 0, "Frame 5 must have zero uploads");
    TEST_ASSERT(telem5.retiredEntries == 0, "Deferred retirement entries must be drained after completion");

    delete target;
    root->unref();
    std::cout << "   [PASS] Tests 1, 2, 3: Warm-up, selective mutation, safe retirement" << std::endl;
  }

  // =========================================================================
  // Test 4: O(1) Logical Invalidation under Device Lost & Clean Recovery
  // =========================================================================
  {
    std::cout << "-> Test 4: O(1) logical invalidation under device lost & clean recovery..." << std::endl;

    SoSeparator * root = new SoSeparator;
    root->ref();

    SoPerspectiveCamera * camera = new SoPerspectiveCamera;
    root->addChild(camera);

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.set1Value(0, SbVec3f(0.0f, 0.0f, 0.0f));
    coords->point.set1Value(1, SbVec3f(1.0f, 0.0f, 0.0f));
    coords->point.set1Value(2, SbVec3f(0.0f, 1.0f, 0.0f));
    root->addChild(coords);

    SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
    const int32_t indices[] = {0, 1, 2, -1};
    ifs->coordIndex.setValues(0, 4, indices);
    root->addChild(ifs);

    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(128, 128));
    SoWgpuRenderAction action(SbViewportRegion(128, 128));
    action.setRenderTarget(target);
    action.setFastPathEnabled(TRUE);

    // Initial render
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Initial apply must succeed");

    SoWgpuCacheTelemetry telemPre;
    TEST_ASSERT(target->getCacheTelemetry(telemPre), "Telemetry pre-fault");
    TEST_ASSERT(telemPre.activeEntries >= 1, "Active entries before fault");

    // Inject Device Lost fault
    coin_wgpu_inject_async_fault(5); // 5 = CoinWgpuStatus::DeviceLost

    // Next apply encounters DeviceLost
    action.apply(root);
    // RenderAction handles or reports failure
    TEST_ASSERT(action.getLastStatus() != SoWgpuRenderAction::SUCCESS, "Apply must fail under DeviceLost");

    // Clean recovery: next render action recreates the device and clears cache in O(1)
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Recovery apply must succeed");

    SoWgpuCacheTelemetry telemPost;
    TEST_ASSERT(target->getCacheTelemetry(telemPost), "Telemetry post-recovery");
    TEST_ASSERT(telemPost.frameUploads >= 1, "Post-recovery must re-upload geometry to new device");
    TEST_ASSERT(telemPost.activeEntries >= 1, "New cache has fresh entries");

    delete target;
    root->unref();
    std::cout << "   [PASS] Test 4: O(1) Device Lost invalidation and clean recovery" << std::endl;
  }

  // =========================================================================
  // Test 5: Exact Visual Equivalence between Initial Upload and Cache Hits
  // =========================================================================
  {
    std::cout << "-> Test 5: Exact visual equivalence between upload and cache hits..." << std::endl;

    SoSeparator * root = new SoSeparator;
    root->ref();

    SoPerspectiveCamera * camera = new SoPerspectiveCamera;
    camera->position.setValue(0.0f, 0.0f, 4.0f);
    root->addChild(camera);

    SoDirectionalLight * light = new SoDirectionalLight;
    light->direction.setValue(0.0f, 0.0f, -1.0f);
    light->intensity.setValue(1.0f);
    root->addChild(light);

    SoMaterial * mat = new SoMaterial;
    mat->diffuseColor.setValue(0.2f, 0.7f, 0.3f);
    root->addChild(mat);

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.set1Value(0, SbVec3f(-1.0f, -1.0f, 0.0f));
    coords->point.set1Value(1, SbVec3f( 1.0f, -1.0f, 0.0f));
    coords->point.set1Value(2, SbVec3f( 1.0f,  1.0f, 0.0f));
    coords->point.set1Value(3, SbVec3f(-1.0f,  1.0f, 0.0f));
    root->addChild(coords);

    SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
    const int32_t indices[] = {0, 1, 2, 3, -1};
    ifs->coordIndex.setValues(0, 5, indices);
    root->addChild(ifs);

    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(128, 128));
    SoWgpuRenderAction action(SbViewportRegion(128, 128));
    action.setBackgroundColor(SbColor4f(0.1f, 0.1f, 0.2f, 1.0f));
    action.setRenderTarget(target);
    action.setFastPathEnabled(TRUE);

    // Frame 1: Initial upload
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Frame 1 apply must succeed");

    std::vector<uint8_t> pixels1;
    target->readbackRGBA(pixels1);
    TEST_ASSERT(pixels1.size() == 128 * 128 * 4, "Pixels 1 size check");

    // Frame 2: Cache hit (zero uploads)
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Frame 2 apply must succeed");

    SoWgpuCacheTelemetry telem;
    TEST_ASSERT(target->getCacheTelemetry(telem), "Telemetry check");
    TEST_ASSERT(telem.frameUploadedBytes == 0, "Frame 2 must be 100% cache hit");

    std::vector<uint8_t> pixels2;
    target->readbackRGBA(pixels2);
    TEST_ASSERT(pixels2.size() == 128 * 128 * 4, "Pixels 2 size check");

    // Exact bitwise comparison
    TEST_ASSERT(pixels1 == pixels2, "Pixels from cache hit MUST be bitwise identical to initial upload");

    delete target;
    root->unref();
    std::cout << "   [PASS] Test 5: Exact visual equivalence verified" << std::endl;
  }

  std::cout << "All WgpuCacheTest assertions PASSED successfully!" << std::endl;
  return 0;
}
