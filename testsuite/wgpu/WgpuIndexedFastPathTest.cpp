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
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <string>

#define TEST_ASSERT(cond, msg) do { \
  if (!(cond)) { \
    std::cerr << "FAILED: " << msg << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
    return 1; \
  } \
} while (0)

int main() {
  SoDB::init();
  SoWgpuRenderAction::initClass();

  std::cout << "Running WgpuIndexedFastPathTest (Onda 2D Indexed Fast Path)..." << std::endl;

  // =========================================================================
  // Test 1: Parser de sentinelas, triângulos e quads convexos no Fast Path
  // =========================================================================
  {
    std::cout << "-> Test 1: Sentinel parser, triangles and convex quads..." << std::endl;
    SoSeparator * root = new SoSeparator;
    root->ref();

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.set1Value(0, SbVec3f(0.0f, 0.0f, 0.0f));
    coords->point.set1Value(1, SbVec3f(1.0f, 0.0f, 0.0f));
    coords->point.set1Value(2, SbVec3f(1.0f, 1.0f, 0.0f));
    coords->point.set1Value(3, SbVec3f(0.0f, 1.0f, 0.0f));
    coords->point.set1Value(4, SbVec3f(2.0f, 0.0f, 0.0f));
    coords->point.set1Value(5, SbVec3f(2.0f, 1.0f, 0.0f));
    root->addChild(coords);

    SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
    // Face 1: quad (0, 1, 2, 3) -> 2 triangles (6 indices)
    // Face 2: triangle (1, 4, 5) -> 1 triangle (3 indices)
    const int32_t indices[] = {0, 1, 2, 3, -1, 1, 4, 5, -1};
    ifs->coordIndex.setValues(0, 9, indices);
    root->addChild(ifs);

    SoWgpuRenderAction action;
    action.setFastPathEnabled(TRUE);
    action.apply(root);

    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
                "RenderAction apply with valid indexed face set must succeed");

    std::string log = action.getRecordingLog().getString();
    TEST_ASSERT(log.find("top=TRIANGLES") != std::string::npos,
                "DrawPacket topology must be TRIANGLES");
    TEST_ASSERT(log.find("ic=9") != std::string::npos,
                "Quad (2 tris = 6 indices) + Tri (3 indices) must produce ic=9");

    root->unref();
    std::cout << "   [PASS] Test 1: Sentinel parser and convex quads" << std::endl;
  }

  // =========================================================================
  // Test 2: Degenerescências e sentinelas consecutivas
  // =========================================================================
  {
    std::cout << "-> Test 2: Degenerate faces and consecutive sentinels..." << std::endl;
    SoSeparator * root = new SoSeparator;
    root->ref();

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.set1Value(0, SbVec3f(0.0f, 0.0f, 0.0f));
    coords->point.set1Value(1, SbVec3f(1.0f, 0.0f, 0.0f));
    coords->point.set1Value(2, SbVec3f(0.0f, 1.0f, 0.0f));
    root->addChild(coords);

    SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
    // -1, -1 (consecutive empty), then 0, 1, -1 (degenerate < 3), then 0, 1, 2, -1 (valid triangle)
    const int32_t indices[] = {-1, -1, 0, 1, -1, 0, 1, 2, -1, -1};
    ifs->coordIndex.setValues(0, 10, indices);
    root->addChild(ifs);

    SoWgpuRenderAction action;
    action.setFastPathEnabled(TRUE);
    action.apply(root);

    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
                "Degenerate faces and multiple sentinels must be safely handled without failure");

    std::string log = action.getRecordingLog().getString();
    TEST_ASSERT(log.find("ic=3") != std::string::npos,
                "Only the single valid triangle (3 indices) should be emitted");

    root->unref();
    std::cout << "   [PASS] Test 2: Degenerate faces handled cleanly" << std::endl;
  }

  // =========================================================================
  // Test 3: Validação atômica e rollback (índices inválidos)
  // =========================================================================
  {
    std::cout << "-> Test 3: Atomic validation and rollback on invalid indices..." << std::endl;

    // 3A: Invalid negative index < -1
    {
      SoSeparator * root = new SoSeparator;
      root->ref();

      SoCoordinate3 * coords = new SoCoordinate3;
      coords->point.set1Value(0, SbVec3f(0.0f, 0.0f, 0.0f));
      coords->point.set1Value(1, SbVec3f(1.0f, 0.0f, 0.0f));
      coords->point.set1Value(2, SbVec3f(0.0f, 1.0f, 0.0f));
      root->addChild(coords);

      SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
      // Index -2 is invalid (only -1 is a sentinel)
      const int32_t indices[] = {0, 1, 2, -1, 0, -2, 2, -1};
      ifs->coordIndex.setValues(0, 8, indices);
      root->addChild(ifs);

      SoWgpuRenderAction action;
      action.setFastPathEnabled(TRUE);
      action.apply(root);

      TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::INVALID_SCENE,
                  "Negative index < -1 must reject scene as INVALID_SCENE");

      root->unref();
    }

    // 3B: Out of bounds index
    {
      SoSeparator * root = new SoSeparator;
      root->ref();

      SoCoordinate3 * coords = new SoCoordinate3;
      coords->point.set1Value(0, SbVec3f(0.0f, 0.0f, 0.0f));
      coords->point.set1Value(1, SbVec3f(1.0f, 0.0f, 0.0f));
      coords->point.set1Value(2, SbVec3f(0.0f, 1.0f, 0.0f));
      root->addChild(coords);

      SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
      // Index 42 is out of range of 3 coords
      const int32_t indices[] = {0, 1, 42, -1};
      ifs->coordIndex.setValues(0, 4, indices);
      root->addChild(ifs);

      SoWgpuRenderAction action;
      action.setFastPathEnabled(TRUE);
      action.apply(root);

      TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::INVALID_SCENE,
                  "Out-of-bounds coordinate index must reject scene as INVALID_SCENE");

      root->unref();
    }

    std::cout << "   [PASS] Test 3: Atomic validation and rollback" << std::endl;
  }

  // =========================================================================
  // Test 4: Quads côncavos e fallback transparente
  // =========================================================================
  {
    std::cout << "-> Test 4: Concave quads diversion to fallback..." << std::endl;
    SoSeparator * root = new SoSeparator;
    root->ref();

    SoCoordinate3 * coords = new SoCoordinate3;
    // Concave quad (dart / boomerang shape):
    // p0=(0,0), p1=(1,0), p2=(0.2, 0.2) [reflex vertex], p3=(0,1)
    coords->point.set1Value(0, SbVec3f(0.0f, 0.0f, 0.0f));
    coords->point.set1Value(1, SbVec3f(1.0f, 0.0f, 0.0f));
    coords->point.set1Value(2, SbVec3f(0.2f, 0.2f, 0.0f));
    coords->point.set1Value(3, SbVec3f(0.0f, 1.0f, 0.0f));
    root->addChild(coords);

    SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
    const int32_t indices[] = {0, 1, 2, 3, -1};
    ifs->coordIndex.setValues(0, 5, indices);
    root->addChild(ifs);

    SoWgpuRenderAction action;
    action.setFastPathEnabled(TRUE);
    action.apply(root);

    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
                "Concave quad must fallback to Coin's standard tessellator without failing");

    std::string log = action.getRecordingLog().getString();
    TEST_ASSERT(log.find("draws count: 1") != std::string::npos, "DrawPacket must be generated via fallback");

    root->unref();
    std::cout << "   [PASS] Test 4: Concave quad fallback verified" << std::endl;
  }

  // =========================================================================
  // Test 5: Deduplicação de vértice canônico completo (Smooth vs Faceted)
  // =========================================================================
  {
    std::cout << "-> Test 5: Canonical vertex deduplication..." << std::endl;

    // 5A: Smooth mesh sharing vertices
    {
      SoSeparator * root = new SoSeparator;
      root->ref();

      SoCoordinate3 * coords = new SoCoordinate3;
      coords->point.set1Value(0, SbVec3f(0.0f, 0.0f, 0.0f));
      coords->point.set1Value(1, SbVec3f(1.0f, 0.0f, 0.0f));
      coords->point.set1Value(2, SbVec3f(1.0f, 1.0f, 0.0f));
      coords->point.set1Value(3, SbVec3f(0.0f, 1.0f, 0.0f));
      root->addChild(coords);

      SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
      // 2 triangles sharing an edge (0,2):
      // Triangle 1: (0, 1, 2)
      // Triangle 2: (0, 2, 3)
      const int32_t indices[] = {0, 1, 2, -1, 0, 2, 3, -1};
      ifs->coordIndex.setValues(0, 8, indices);
      root->addChild(ifs);

      // Fast path: should deduplicate to 4 vertices
      SoWgpuRenderAction actionFast;
      actionFast.setFastPathEnabled(TRUE);
      actionFast.apply(root);
      TEST_ASSERT(actionFast.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Fast path must succeed");
      std::string logFast = actionFast.getRecordingLog().getString();
      TEST_ASSERT(logFast.find("vertices count: 4") != std::string::npos,
                  "Fast path with shared vertices must deduplicate exactly to 4 unique vertices");
      TEST_ASSERT(logFast.find("indices count: 6") != std::string::npos,
                  "Indices must contain 6 entries (2 triangles)");

      // Fallback path: duplicates vertices to 6
      SoWgpuRenderAction actionFallback;
      actionFallback.setFastPathEnabled(FALSE);
      actionFallback.apply(root);
      TEST_ASSERT(actionFallback.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Fallback must succeed");
      std::string logFallback = actionFallback.getRecordingLog().getString();
      TEST_ASSERT(logFallback.find("vertices count: 6") != std::string::npos,
                  "Fallback duplicates vertices for each triangle (6 vertices)");

      root->unref();
    }

    // 5B: Faceted mesh with PER_FACE normals
    {
      SoSeparator * root = new SoSeparator;
      root->ref();

      SoCoordinate3 * coords = new SoCoordinate3;
      coords->point.set1Value(0, SbVec3f(0.0f, 0.0f, 0.0f));
      coords->point.set1Value(1, SbVec3f(1.0f, 0.0f, 0.0f));
      coords->point.set1Value(2, SbVec3f(1.0f, 1.0f, 0.0f));
      coords->point.set1Value(3, SbVec3f(0.0f, 1.0f, 0.0f));
      root->addChild(coords);

      SoNormal * normals = new SoNormal;
      normals->vector.set1Value(0, SbVec3f(0.0f, 0.0f, 1.0f));  // Face 1 normal
      normals->vector.set1Value(1, SbVec3f(0.0f, 1.0f, 0.0f));  // Face 2 normal (different!)
      root->addChild(normals);

      SoNormalBinding * nbind = new SoNormalBinding;
      nbind->value = SoNormalBinding::PER_FACE;
      root->addChild(nbind);

      SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
      const int32_t indices[] = {0, 1, 2, -1, 0, 2, 3, -1};
      ifs->coordIndex.setValues(0, 8, indices);
      root->addChild(ifs);

      SoWgpuRenderAction actionFaceted;
      actionFaceted.setFastPathEnabled(TRUE);
      actionFaceted.apply(root);

      TEST_ASSERT(actionFaceted.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Faceted fast path must succeed");
      std::string logFaceted = actionFaceted.getRecordingLog().getString();

      // Because normals differ per face, shared edge vertices (0 and 2) must NOT be merged
      // 3 vertices for Face 1 + 3 vertices for Face 2 = 6 vertices
      TEST_ASSERT(logFaceted.find("vertices count: 6") != std::string::npos,
                  "Faceted mesh with different per-face normals must keep vertices separated (6 vertices)");

      root->unref();
    }

    std::cout << "   [PASS] Test 5: Canonical vertex deduplication verified" << std::endl;
  }

  // =========================================================================
  // Test 6: Linhas indexadas (SoIndexedLineSet)
  // =========================================================================
  {
    std::cout << "-> Test 6: Indexed line sets (SoIndexedLineSet)..." << std::endl;
    SoSeparator * root = new SoSeparator;
    root->ref();

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.set1Value(0, SbVec3f(0.0f, 0.0f, 0.0f));
    coords->point.set1Value(1, SbVec3f(1.0f, 0.0f, 0.0f));
    coords->point.set1Value(2, SbVec3f(1.0f, 1.0f, 0.0f));
    coords->point.set1Value(3, SbVec3f(0.0f, 1.0f, 0.0f));
    root->addChild(coords);

    SoIndexedLineSet * ils = new SoIndexedLineSet;
    // Polyline 1: (0, 1, 2, 3, -1) -> 3 line segments (6 indices)
    // Polyline 2: (0, -1) -> degenerate (< 2 vertices), ignored
    const int32_t indices[] = {0, 1, 2, 3, -1, 0, -1};
    ils->coordIndex.setValues(0, 7, indices);
    root->addChild(ils);

    SoWgpuRenderAction action;
    action.setFastPathEnabled(TRUE);
    action.apply(root);

    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "IndexedLineSet fast path must succeed");

    std::string log = action.getRecordingLog().getString();
    TEST_ASSERT(log.find("top=LINES") != std::string::npos,
                "DrawPacket topology must be LINES");
    TEST_ASSERT(log.find("ic=6") != std::string::npos,
                "Polyline of 4 vertices must produce 3 segments = 6 indices");

    root->unref();
    std::cout << "   [PASS] Test 6: IndexedLineSet fast path verified" << std::endl;
  }

  // =========================================================================
  // Test 7: Equivalência estrita de pixels e profundidade (Fast Path vs Fallback)
  // =========================================================================
  {
    std::cout << "-> Test 7: Strict pixel and depth equivalence (Fast Path vs Fallback)..." << std::endl;

    SoSeparator * root = new SoSeparator;
    root->ref();

    SoPerspectiveCamera * cam = new SoPerspectiveCamera;
    cam->position.setValue(0.0f, 0.0f, 4.0f);
    cam->pointAt(SbVec3f(0.0f, 0.0f, 0.0f));
    cam->nearDistance = 1.0f;
    cam->farDistance = 10.0f;
    root->addChild(cam);

    SoDirectionalLight * light = new SoDirectionalLight;
    light->direction.setValue(0.0f, 0.0f, -1.0f);
    root->addChild(light);

    SoMaterial * mat = new SoMaterial;
    mat->diffuseColor.setValue(0.0f, 0.8f, 0.2f); // Green
    mat->ambientColor.setValue(0.1f, 0.2f, 0.1f);
    root->addChild(mat);

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.set1Value(0, SbVec3f(-0.8f, -0.8f, 0.0f));
    coords->point.set1Value(1, SbVec3f( 0.8f, -0.8f, 0.0f));
    coords->point.set1Value(2, SbVec3f( 0.8f,  0.8f, 0.0f));
    coords->point.set1Value(3, SbVec3f(-0.8f,  0.8f, 0.0f));
    root->addChild(coords);

    // Convex quad face
    SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
    const int32_t indices[] = {0, 1, 2, 3, -1};
    ifs->coordIndex.setValues(0, 5, indices);
    root->addChild(ifs);

    const int W = 64;
    const int H = 64;

    // Render 1: Fast Path Enabled
    std::vector<uint8_t> pixelsFast;
    std::vector<float> depthFast;
    {
      SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(W, H));
      SoWgpuRenderAction action(SbViewportRegion(W, H));
      action.setRenderTarget(target);
      action.setFastPathEnabled(TRUE);
      action.apply(root);

      TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Fast path render must succeed");
      target->readbackRGBA(pixelsFast);
      target->readbackDepth(depthFast);
      delete target;
    }

    // Render 2: Fallback (Fast Path Disabled)
    std::vector<uint8_t> pixelsFallback;
    std::vector<float> depthFallback;
    {
      SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(W, H));
      SoWgpuRenderAction action(SbViewportRegion(W, H));
      action.setRenderTarget(target);
      action.setFastPathEnabled(FALSE);
      action.apply(root);

      TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Fallback render must succeed");
      target->readbackRGBA(pixelsFallback);
      target->readbackDepth(depthFallback);
      delete target;
    }

    TEST_ASSERT(pixelsFast.size() == pixelsFallback.size(), "Color buffer sizes must match");
    TEST_ASSERT(depthFast.size() == depthFallback.size(), "Depth buffer sizes must match");

    // Compare color buffers pixel-by-pixel
    int pixelDiffCount = 0;
    for (size_t i = 0; i < pixelsFast.size(); i += 4) {
      int dr = std::abs((int)pixelsFast[i + 0] - (int)pixelsFallback[i + 0]);
      int dg = std::abs((int)pixelsFast[i + 1] - (int)pixelsFallback[i + 1]);
      int db = std::abs((int)pixelsFast[i + 2] - (int)pixelsFallback[i + 2]);
      int da = std::abs((int)pixelsFast[i + 3] - (int)pixelsFallback[i + 3]);
      if (dr > 1 || dg > 1 || db > 1 || da > 1) {
        pixelDiffCount++;
      }
    }

    // Compare depth buffers pixel-by-pixel
    int depthDiffCount = 0;
    for (size_t i = 0; i < depthFast.size(); ++i) {
      float dDiff = std::abs(depthFast[i] - depthFallback[i]);
      if (dDiff > 1e-4f) {
        depthDiffCount++;
      }
    }

    // The two triangles produced by the convex quad fan match identically
    TEST_ASSERT(pixelDiffCount == 0, "Pixels between Fast Path and Fallback must match 100%");
    TEST_ASSERT(depthDiffCount == 0, "Depth values between Fast Path and Fallback must match 100%");

    root->unref();
    std::cout << "   [PASS] Test 7: Strict pixel and depth equivalence verified" << std::endl;
  }

  // Test 8: missing normals must honor creaseAngle in the indexed fast path.
  // The shared edge is 90 degrees: hard at zero, smooth above PI/2.
  {
    std::cout << "-> Test 8: Generated normals at hard and smooth edges..." << std::endl;
    for (int variant = 0; variant < 2; ++variant) {
      SoSeparator * root = new SoSeparator;
      root->ref();

      SoPerspectiveCamera * camera = new SoPerspectiveCamera;
      camera->position.setValue(2.0f, 2.0f, 3.0f);
      camera->pointAt(SbVec3f(0.3f, 0.3f, 0.3f));
      camera->nearDistance = 0.1f;
      camera->farDistance = 10.0f;
      root->addChild(camera);
      SoDirectionalLight * light = new SoDirectionalLight;
      light->direction.setValue(-0.3f, -0.5f, -1.0f);
      root->addChild(light);
      SoMaterial * material = new SoMaterial;
      material->diffuseColor.setValue(0.8f, 0.5f, 0.2f);
      root->addChild(material);
      SoShapeHints * hints = new SoShapeHints;
      hints->shapeType = SoShapeHints::SOLID;
      hints->creaseAngle = variant == 0 ? 0.0f : 1.6f;
      root->addChild(hints);

      SoCoordinate3 * coords = new SoCoordinate3;
      coords->point.set1Value(0, SbVec3f(0.0f, 0.0f, 0.0f));
      coords->point.set1Value(1, SbVec3f(1.0f, 0.0f, 0.0f));
      coords->point.set1Value(2, SbVec3f(0.0f, 1.0f, 0.0f));
      coords->point.set1Value(3, SbVec3f(0.0f, 0.0f, 1.0f));
      root->addChild(coords);
      SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
      const int32_t indices[] = {0, 1, 2, -1, 0, 3, 1, -1};
      ifs->coordIndex.setValues(0, 8, indices);
      root->addChild(ifs);

      SoWgpuRenderAction recording;
      recording.setFastPathEnabled(TRUE);
      recording.apply(root);
      TEST_ASSERT(recording.getLastStatus() == SoWgpuRenderAction::SUCCESS,
                  "Generated-normal fast path must succeed");
      const std::string log = recording.getRecordingLog().getString();
      if (variant == 0) {
        TEST_ASSERT(log.find("vertices count: 6") != std::string::npos,
                    "Hard edge must split shared positions by face normal");
      } else {
        TEST_ASSERT(log.find("norm=[0.0000,0.7071,0.7071]") != std::string::npos,
                    "Smooth edge must receive the crease-angle averaged normal");
      }

      std::vector<uint8_t> fastPixels, fallbackPixels;
      {
        SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(96, 96));
        SoWgpuRenderAction action(SbViewportRegion(96, 96));
        action.setRenderTarget(target);
        action.setFastPathEnabled(TRUE);
        action.apply(root);
        TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
                    "Generated-normal fast render must succeed");
        target->readbackRGBA(fastPixels);
        delete target;
      }
      {
        SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(96, 96));
        SoWgpuRenderAction action(SbViewportRegion(96, 96));
        action.setRenderTarget(target);
        action.setFastPathEnabled(FALSE);
        action.apply(root);
        TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
                    "Generated-normal fallback render must succeed");
        target->readbackRGBA(fallbackPixels);
        delete target;
      }
      TEST_ASSERT(fastPixels.size() == fallbackPixels.size() && !fastPixels.empty(),
                  "Both paths must return the same nonempty RGBA buffer");
      size_t foreground = 0, mismatchedPixels = 0;
      for (size_t i = 0; i < fastPixels.size(); i += 4) {
        if (fastPixels[i] > 30 || fastPixels[i + 1] > 30 ||
            fastPixels[i + 2] > 30) ++foreground;
        if (std::abs(int(fastPixels[i]) - int(fallbackPixels[i])) > 1 ||
            std::abs(int(fastPixels[i + 1]) - int(fallbackPixels[i + 1])) > 1 ||
            std::abs(int(fastPixels[i + 2]) - int(fallbackPixels[i + 2])) > 1) {
          ++mismatchedPixels;
        }
      }
      TEST_ASSERT(foreground > 100, "The wedge must be visible in the image");
      TEST_ASSERT(mismatchedPixels == 0,
                  "Fast path generated normals must match Coin fallback pixels");
      root->unref();
    }
    std::cout << "   [PASS] Test 8: Hard and smooth generated normals verified" << std::endl;
  }

  std::cout << "\nALL 8 TESTS IN WgpuIndexedFastPathTest PASSED!" << std::endl;
  return 0;
}
