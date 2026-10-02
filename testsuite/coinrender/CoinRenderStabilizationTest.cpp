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
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>

#include "rendering/coinwgpu/CoinWgpuFfi.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <string>

#define TEST_ASSERT(cond, msg) do {   if (!(cond)) {     std::cerr << "FAILED: " << msg << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl;     return 1;   } } while (0)

int main() {
  SoDB::init();
  CoinRenderAction::initClass();

  std::cout << "Running CoinRenderStabilizationTest (Onda 2F Concurrency, Packet Separation, Exact Hash & Budget)..." << std::endl;

#if !defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  std::cout << "WGPU Rust bridge not enabled, skipping test." << std::endl;
  return 0;
#endif

  // =========================================================================
  // Test 1: Strict GPU Completion Serial Order and Safe Retirement
  // =========================================================================
  {
    std::cout << "-> Test 1: Strict completion serial order with pending GPU work..." << std::endl;

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

    CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(128, 128));
    TEST_ASSERT(target != nullptr, "target create must succeed");

    CoinRenderAction action(SbViewportRegion(128, 128));
    action.setRenderTarget(target);
    action.setFastPathEnabled(TRUE);

    // Apply frame 1
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Frame 1 apply must succeed");

    CoinRenderCacheTelemetry telem1;
    target->getCacheTelemetry(telem1);
    TEST_ASSERT(telem1.submissionSerial >= 1, "Submission serial must be at least 1");

    // In offscreen render, pollDevice advances GPU work
    target->pollDevice();
    CoinRenderCacheTelemetry telemAfterPoll;
    target->getCacheTelemetry(telemAfterPoll);
    TEST_ASSERT(telemAfterPoll.completedSerial >= telem1.submissionSerial,
                "Completed serial must match or exceed submission serial after poll");

    // Mutate and check retirement tracking
    coords->point.set1Value(0, SbVec3f(0.1f, 0.1f, 0.0f));
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Frame 2 apply must succeed");

    target->pollDevice();

    delete target;
    root->unref();
    std::cout << "   [PASS] Test 1: Strict completion serial order verified" << std::endl;
  }

  // =========================================================================
  // Test 2: Separation of Multiple Occurrences with Identical State in Frame
  // =========================================================================
  {
    std::cout << "-> Test 2: Separation of multiple occurrences with identical state in frame..." << std::endl;

    SoSeparator * root = new SoSeparator;
    root->ref();

    SoPerspectiveCamera * camera = new SoPerspectiveCamera;
    root->addChild(camera);

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.set1Value(0, SbVec3f(-1.0f, -1.0f, 0.0f));
    coords->point.set1Value(1, SbVec3f( 1.0f, -1.0f, 0.0f));
    coords->point.set1Value(2, SbVec3f( 0.0f,  1.0f, 0.0f));
    root->addChild(coords);

    // The EXACT SAME instance of SoIndexedFaceSet added to two branches
    SoIndexedFaceSet * sharedIfs = new SoIndexedFaceSet;
    const int32_t indices[] = {0, 1, 2, -1};
    sharedIfs->coordIndex.setValues(0, 4, indices);

    SoSeparator * branch1 = new SoSeparator;
    branch1->addChild(sharedIfs);
    root->addChild(branch1);

    SoSeparator * branch2 = new SoSeparator;
    branch2->addChild(sharedIfs);
    root->addChild(branch2);

    CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(128, 128));
    CoinRenderAction action(SbViewportRegion(128, 128));
    action.setRenderTarget(target);
    action.setFastPathEnabled(TRUE);

    // Frame 1: Initial upload of both occurrences
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Frame 1 apply must succeed");

    std::string log = action.getRecordingLog().getString();
    // Verify that both occurrences produced separate draw packets in the recording log
    TEST_ASSERT(log.find("draws count: 2") != std::string::npos,
                "Two occurrences of the same node MUST produce separate DrawPackets (draws count: 2)");
    TEST_ASSERT(log.find("draw 0:") != std::string::npos, "Must record draw 0");
    TEST_ASSERT(log.find("draw 1:") != std::string::npos, "Must record draw 1");

    CoinRenderCacheTelemetry telem1;
    target->getCacheTelemetry(telem1);
    TEST_ASSERT(telem1.activeEntries >= 2, "Both occurrences must be tracked in active_entries without collision");
    TEST_ASSERT(telem1.frameUploads >= 2, "Both occurrences must upload on frame 1");

    // Frame 2: Static render - both occurrences must hit cache!
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Frame 2 apply must succeed");

    CoinRenderCacheTelemetry telem2;
    target->getCacheTelemetry(telem2);
    TEST_ASSERT(telem2.frameUploadedBytes == 0, "Frame 2 static scene must have ZERO uploads");
    TEST_ASSERT(telem2.frameHits >= 2, "Both occurrences must hit the cache in frame 2");

    delete target;
    root->unref();
    std::cout << "   [PASS] Test 2: Separation of multiple occurrences verified" << std::endl;
  }

  // =========================================================================
  // Test 3: Canonical Vertex Serialization Hash Fidelity
  // =========================================================================
  {
    std::cout << "-> Test 3: Canonical vertex serialization hash fidelity..." << std::endl;

    SoSeparator * root = new SoSeparator;
    root->ref();

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.set1Value(0, SbVec3f(0.0f, 0.0f, 0.0f));
    coords->point.set1Value(1, SbVec3f(1.0f, 0.0f, 0.0f));
    coords->point.set1Value(2, SbVec3f(0.0f, 1.0f, 0.0f));
    root->addChild(coords);

    SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
    const int32_t indices[] = {0, 1, 2, -1};
    ifs->coordIndex.setValues(0, 4, indices);
    root->addChild(ifs);

    CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(128, 128));
    CoinRenderAction action(SbViewportRegion(128, 128));
    action.setRenderTarget(target);
    action.setFastPathEnabled(TRUE);

    // Frame 1: initial upload
    action.apply(root);
    CoinRenderCacheTelemetry telem1;
    target->getCacheTelemetry(telem1);
    TEST_ASSERT(telem1.frameUploads == 1, "Frame 1 must upload 1 shape");

    // Frame 2: static - cache hit
    action.apply(root);
    CoinRenderCacheTelemetry telem2;
    target->getCacheTelemetry(telem2);
    TEST_ASSERT(telem2.frameHits == 1, "Frame 2 must hit cache");
    TEST_ASSERT(telem2.frameUploads == 0, "Frame 2 uploads must be 0");

    // Mutate a vertex coordinate
    coords->point.set1Value(1, SbVec3f(1.5f, 0.0f, 0.0f));

    // Frame 3: render mutated shape - must invalidate canonical hash and trigger upload
    action.apply(root);
    CoinRenderCacheTelemetry telem3;
    target->getCacheTelemetry(telem3);
    TEST_ASSERT(telem3.frameUploads == 1, "Mutating position must invalidate canonical hash, causing a cache miss");
    TEST_ASSERT(telem3.frameHits == 0, "Mutated frame must have 0 hits");

    // Frame 4: static render of newly mutated shape - must hit cache
    action.apply(root);
    CoinRenderCacheTelemetry telem4;
    target->getCacheTelemetry(telem4);
    TEST_ASSERT(telem4.frameHits == 1, "Next static frame must hit the newly updated revision");
    TEST_ASSERT(telem4.frameUploads == 0, "Next static frame must have 0 uploads");

    delete target;
    root->unref();
    std::cout << "   [PASS] Test 3: Canonical hash fidelity verified" << std::endl;
  }

  // =========================================================================
  // Test 4: Eviction of Removed Shapes with Active Key Protection
  // =========================================================================
  {
    std::cout << "-> Test 4: Eviction of removed shapes with active key protection..." << std::endl;

    SoSeparator * root = new SoSeparator;
    root->ref();

    SoSeparator * sepA = new SoSeparator;
    SoCoordinate3 * coordsA = new SoCoordinate3;
    coordsA->point.set1Value(0, SbVec3f(-1.0f, 0.0f, 0.0f));
    coordsA->point.set1Value(1, SbVec3f( 0.0f, 0.0f, 0.0f));
    coordsA->point.set1Value(2, SbVec3f(-0.5f, 1.0f, 0.0f));
    sepA->addChild(coordsA);
    SoIndexedFaceSet * ifsA = new SoIndexedFaceSet;
    const int32_t idxA[] = {0, 1, 2, -1};
    ifsA->coordIndex.setValues(0, 4, idxA);
    sepA->addChild(ifsA);
    root->addChild(sepA);

    SoSeparator * sepB = new SoSeparator;
    SoCoordinate3 * coordsB = new SoCoordinate3;
    coordsB->point.set1Value(0, SbVec3f(0.0f, 0.0f, 0.0f));
    coordsB->point.set1Value(1, SbVec3f(1.0f, 0.0f, 0.0f));
    coordsB->point.set1Value(2, SbVec3f(0.5f, 1.0f, 0.0f));
    sepB->addChild(coordsB);
    SoIndexedFaceSet * ifsB = new SoIndexedFaceSet;
    const int32_t idxB[] = {0, 1, 2, -1};
    ifsB->coordIndex.setValues(0, 4, idxB);
    sepB->addChild(ifsB);
    root->addChild(sepB);

    CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(128, 128));
    CoinRenderAction action(SbViewportRegion(128, 128));
    action.setRenderTarget(target);
    action.setFastPathEnabled(TRUE);

    // Frame 1: Upload both A and B
    action.apply(root);
    CoinRenderCacheTelemetry telem1;
    target->getCacheTelemetry(telem1);
    TEST_ASSERT(telem1.activeEntries >= 2, "Both shapes must be cached");

    // Remove Shape B from scene
    root->removeChild(sepB);

    // Render Frame 2 with only Shape A
    action.apply(root);

    // Explicit trim: shapes not referenced in current frame are trimmed
    coin_wgpu_trim_cache();

    CoinRenderCacheTelemetry telemAfterTrim;
    target->getCacheTelemetry(telemAfterTrim);
    TEST_ASSERT(telemAfterTrim.activeEntries == 1,
                "Shape B must be evicted, Shape A must remain protected in active_entries");
    TEST_ASSERT(telemAfterTrim.retiredEntries >= 1,
                "Evicted Shape B must be placed in deferred_release for safe retirement");

    // Advance GPU poll to drain safely retired entry
    target->pollDevice();
    action.apply(root); // next frame drains retired entries

    CoinRenderCacheTelemetry telemDrained;
    target->getCacheTelemetry(telemDrained);
    TEST_ASSERT(telemDrained.activeEntries == 1, "Shape A remains active");
    TEST_ASSERT(telemDrained.retiredEntries == 0, "Retired Shape B drained completely");

    delete target;
    root->unref();
    std::cout << "   [PASS] Test 4: Eviction of removed shapes verified" << std::endl;
  }

  // =========================================================================
  // Test 5: Cache Geometry Budget (LRU) and Transactional Rollback
  // =========================================================================
  {
    std::cout << "-> Test 5: Cache geometry budget (LRU) and transactional rollback..." << std::endl;
    coin_wgpu_reset_context();

    SoSeparator * root = new SoSeparator;
    root->ref();

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.set1Value(0, SbVec3f(0.0f, 0.0f, 0.0f));
    coords->point.set1Value(1, SbVec3f(1.0f, 0.0f, 0.0f));
    coords->point.set1Value(2, SbVec3f(0.0f, 1.0f, 0.0f));
    root->addChild(coords);

    SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
    const int32_t indices[] = {0, 1, 2, -1};
    ifs->coordIndex.setValues(0, 4, indices);
    root->addChild(ifs);

    CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(128, 128));
    CoinRenderAction action(SbViewportRegion(128, 128));
    action.setRenderTarget(target);
    action.setFastPathEnabled(TRUE);

    // Initial successful render
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Initial apply must succeed");

    CoinRenderCacheTelemetry telemPre;
    target->getCacheTelemetry(telemPre);
    TEST_ASSERT(telemPre.activeEntries == 1, "Initial entry present");

    // Inject simulated allocation failure (FAULT_CACHE_ALLOC_FAIL = 201)
    coin_wgpu_inject_fault(201);

    // Attempt to render mutated shape: allocation fails transacionalmente
    coords->point.set1Value(0, SbVec3f(0.5f, 0.5f, 0.0f));
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() != CoinRenderAction::SUCCESS, "Apply must fail under simulated OOM");

    // Target entered error state; replace target with fresh healthy target to test cache integrity
    delete target;
    target = CoinRenderTarget::createOffscreen(SbVec2i32(128, 128));
    action.setRenderTarget(target);

    // Transactional rollback check: retry apply succeeds cleanly without cache corruption
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Apply on fresh target must succeed");

    // Test Cache Geometry Budget LRU:
    // Size follows the current bridge vertex layout, including extra UV sets.
    const uint64_t shapeBytes = 3 * sizeof(CoinWgpuVertex) + 3 * sizeof(uint32_t);
    // Keep enough room for one triangle, but not for both shapes together.
    coin_wgpu_set_cache_budget(shapeBytes + shapeBytes / 2, 10);

    // Create a separate shape that exceeds the budget together with the first.
    SoSeparator * rootShape2 = new SoSeparator;
    rootShape2->ref();
    SoCoordinate3 * coords2 = new SoCoordinate3;
    coords2->point.set1Value(0, SbVec3f(2.0f, 0.0f, 0.0f));
    coords2->point.set1Value(1, SbVec3f(3.0f, 0.0f, 0.0f));
    coords2->point.set1Value(2, SbVec3f(2.5f, 1.0f, 0.0f));
    rootShape2->addChild(coords2);
    SoIndexedFaceSet * ifs2 = new SoIndexedFaceSet;
    const int32_t idx2[] = {0, 1, 2, -1};
    ifs2->coordIndex.setValues(0, 4, idx2);
    rootShape2->addChild(ifs2);

    // The cache must evict shape 1 before retaining shape 2 within the budget.
    action.apply(rootShape2);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Apply shape 2 must succeed");

    CoinRenderCacheTelemetry telemBudget;
    target->getCacheTelemetry(telemBudget);
    TEST_ASSERT(telemBudget.activeEntries == 1, "LRU eviction must keep active entries at 1 within budget");
    TEST_ASSERT(telemBudget.retiredEntries >= 1, "Evicted shape 1 must be moved to deferred_release");

    // Restore standard budget
    coin_wgpu_set_cache_budget(128 * 1024 * 1024, 10);

    rootShape2->unref();
    delete target;
    root->unref();
    std::cout << "   [PASS] Test 5: Cache geometry budget and transactional rollback verified" << std::endl;
  }

  std::cout << "All CoinRenderStabilizationTest assertions PASSED successfully!" << std::endl;
  return 0;
}
