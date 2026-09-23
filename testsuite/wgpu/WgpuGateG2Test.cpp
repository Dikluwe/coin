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
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoDrawStyle.h>

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

  std::cout << "Running WgpuGateG2Test (Onda 2C Native Topologies: Lines and Points)..." << std::endl;

  // =========================================================================
  // G2.1: Native Unit Line Rendering and Readback (SoLineSet)
  // =========================================================================
  {
    std::cout << "-> Test G2.1: Native unit line rendering and readback (SoLineSet)..." << std::endl;
    SoSeparator * root = new SoSeparator;
    root->ref();

    SoOrthographicCamera * cam = new SoOrthographicCamera;
    cam->position.setValue(0.0f, 0.0f, 5.0f);
    cam->pointAt(SbVec3f(0.0f, 0.0f, 0.0f));
    cam->height = 2.0f;
    cam->nearDistance = 1.0f;
    cam->farDistance = 10.0f;
    root->addChild(cam);

    // Green horizontal line at y = 0 from x = -0.8 to x = 0.8 at z = 0
    SoMaterial * lineMat = new SoMaterial;
    lineMat->diffuseColor.setValue(0.0f, 1.0f, 0.0f);
    lineMat->ambientColor.setValue(0.0f, 0.0f, 0.0f);
    root->addChild(lineMat);

    SoCoordinate3 * lineCoords = new SoCoordinate3;
    lineCoords->point.set1Value(0, SbVec3f(-0.8f, 0.0f, 0.0f));
    lineCoords->point.set1Value(1, SbVec3f( 0.8f, 0.0f, 0.0f));
    root->addChild(lineCoords);

    SoLineSet * lineSet = new SoLineSet;
    const int32_t numVerts[] = {2};
    lineSet->numVertices.setValues(0, 1, numVerts);
    root->addChild(lineSet);

    SoWgpuRenderAction action(SbViewportRegion(64, 64));
    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
    action.setRenderTarget(target);

    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
                "Unit line rendering with SoLineSet must succeed");

    std::vector<uint8_t> pixels;
    target->readbackRGBA(pixels);
    TEST_ASSERT(pixels.size() == 64 * 64 * 4, "Color buffer size must be 64*64*4");

    // Center region pixel on line
    bool foundLine = false;
    size_t lineIdx = 0;
    for (int y = 30; y <= 33; ++y) {
      size_t idx = (y * 64 + 32) * 4;
      if (pixels[idx + 1] > 150 && pixels[idx + 0] < 50 && pixels[idx + 2] < 50) {
        foundLine = true;
        lineIdx = y * 64 + 32;
        break;
      }
    }
    TEST_ASSERT(foundLine, "Center pixel on line must have dominant green component");

    // Top-left corner pixel (5, 5) must be background (black)
    size_t cornerIdx = (5 * 64 + 5) * 4;
    TEST_ASSERT(pixels[cornerIdx + 0] == 0 && pixels[cornerIdx + 1] == 0 && pixels[cornerIdx + 2] == 0,
                "Background pixel away from line must be clear color");

    // Depth readback
    std::vector<float> depths;
    target->readbackDepth(depths);
    TEST_ASSERT(depths.size() == 64 * 64, "Depth buffer size must be 64*64");

    float lineDepth = depths[lineIdx];
    TEST_ASSERT(lineDepth > 0.0f && lineDepth < 0.99f,
                "Depth on line must be within active depth range (0.0, 1.0)");
    float bgDepth = depths[5 * 64 + 5];
    TEST_ASSERT(std::abs(bgDepth - 1.0f) < 1e-4f,
                "Background depth must remain at clear value 1.0f");

    delete target;
    root->unref();
    std::cout << "   [PASS] G2.1 Native unit line rendering and readback" << std::endl;
  }

  // =========================================================================
  // G2.2: Native Unit Point Rendering and Readback (SoPointSet)
  // =========================================================================
  {
    std::cout << "-> Test G2.2: Native unit point rendering and readback (SoPointSet)..." << std::endl;
    SoSeparator * root = new SoSeparator;
    root->ref();

    SoOrthographicCamera * cam = new SoOrthographicCamera;
    cam->position.setValue(0.0f, 0.0f, 5.0f);
    cam->pointAt(SbVec3f(0.0f, 0.0f, 0.0f));
    cam->height = 2.0f;
    cam->nearDistance = 1.0f;
    cam->farDistance = 10.0f;
    root->addChild(cam);

    // Blue point at origin (0, 0, 0)
    SoMaterial * pointMat = new SoMaterial;
    pointMat->diffuseColor.setValue(0.0f, 0.0f, 1.0f);
    pointMat->ambientColor.setValue(0.0f, 0.0f, 0.0f);
    root->addChild(pointMat);

    SoCoordinate3 * pointCoords = new SoCoordinate3;
    pointCoords->point.set1Value(0, SbVec3f(0.0f, 0.0f, 0.0f));
    root->addChild(pointCoords);

    SoPointSet * pointSet = new SoPointSet;
    pointSet->numPoints.setValue(1);
    root->addChild(pointSet);

    SoWgpuRenderAction action(SbViewportRegion(64, 64));
    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
    action.setRenderTarget(target);

    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
                "Unit point rendering with SoPointSet must succeed");

    std::vector<uint8_t> pixels;
    target->readbackRGBA(pixels);

    // Center region pixel for point at origin (0, 0)
    bool foundPt = false;
    size_t ptIdx = 0;
    for (int y = 30; y <= 33; ++y) {
      for (int x = 30; x <= 33; ++x) {
        size_t idx = (y * 64 + x) * 4;
        if (pixels[idx + 2] > 150 && pixels[idx + 0] < 50 && pixels[idx + 1] < 50) {
          foundPt = true;
          ptIdx = y * 64 + x;
          break;
        }
      }
      if (foundPt) break;
    }
    TEST_ASSERT(foundPt, "Center pixel must have dominant blue component for point at origin");

    std::vector<float> depths;
    target->readbackDepth(depths);
    float ptDepth = depths[ptIdx];
    TEST_ASSERT(ptDepth > 0.0f && ptDepth < 0.99f,
                "Point depth must be within active depth range (0.0, 1.0)");

    delete target;
    root->unref();
    std::cout << "   [PASS] G2.2 Native unit point rendering and readback" << std::endl;
  }

  // =========================================================================
  // G2.3: Cross-Topology Depth Occlusion (Line vs Triangle)
  // =========================================================================
  {
    std::cout << "-> Test G2.3: Cross-topology depth occlusion (Line vs Triangle)..." << std::endl;

    SoPerspectiveCamera * cam = new SoPerspectiveCamera;
    cam->position.setValue(0.0f, 0.0f, 5.0f);
    cam->pointAt(SbVec3f(0.0f, 0.0f, 0.0f));
    cam->nearDistance = 1.0f;
    cam->farDistance = 10.0f;

    SoDirectionalLight * light = new SoDirectionalLight;
    light->direction.setValue(0.0f, 0.0f, -1.0f);

    // Front: Horizontal Red Line at z = +1.0
    SoSeparator * lineSep = new SoSeparator;
    lineSep->ref();
    SoMaterial * lineMat = new SoMaterial;
    lineMat->diffuseColor.setValue(1.0f, 0.0f, 0.0f);
    lineMat->ambientColor.setValue(0.0f, 0.0f, 0.0f);
    lineSep->addChild(lineMat);
    SoCoordinate3 * lineCoords = new SoCoordinate3;
    lineCoords->point.set1Value(0, SbVec3f(-1.0f, 0.0f, 1.0f));
    lineCoords->point.set1Value(1, SbVec3f( 1.0f, 0.0f, 1.0f));
    lineSep->addChild(lineCoords);
    SoLineSet * lineSet = new SoLineSet;
    const int32_t nv[] = {2};
    lineSet->numVertices.setValues(0, 1, nv);
    lineSep->addChild(lineSet);

    // Back: Green Triangle at z = -1.0
    SoSeparator * triSep = new SoSeparator;
    triSep->ref();
    SoMaterial * triMat = new SoMaterial;
    triMat->diffuseColor.setValue(0.0f, 1.0f, 0.0f);
    triMat->ambientColor.setValue(0.0f, 0.0f, 0.0f);
    triSep->addChild(triMat);
    SoCoordinate3 * triCoords = new SoCoordinate3;
    triCoords->point.set1Value(0, SbVec3f(-1.0f, -1.0f, -1.0f));
    triCoords->point.set1Value(1, SbVec3f( 1.0f, -1.0f, -1.0f));
    triCoords->point.set1Value(2, SbVec3f( 0.0f,  1.0f, -1.0f));
    triSep->addChild(triCoords);
    SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
    const int32_t indices[] = {0, 1, 2, -1};
    ifs->coordIndex.setValues(0, 4, indices);
    triSep->addChild(ifs);

    // Case 1: Draw triangle (back) first, then line (front) second
    SoSeparator * root1 = new SoSeparator;
    root1->ref();
    root1->addChild(cam);
    root1->addChild(light);
    root1->addChild(triSep);
    root1->addChild(lineSep);

    SoWgpuRenderAction action(SbViewportRegion(64, 64));
    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
    action.setRenderTarget(target);
    action.apply(root1);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Apply root1 must succeed");

    std::vector<uint8_t> pixels1;
    target->readbackRGBA(pixels1);

    bool foundRedLine1 = false;
    size_t lineIdx1 = 0;
    for (int y = 30; y <= 33; ++y) {
      size_t idx = (y * 64 + 32) * 4;
      if (pixels1[idx + 0] > 150 && pixels1[idx + 1] < 100) {
        foundRedLine1 = true;
        lineIdx1 = y * 64 + 32;
        break;
      }
    }
    TEST_ASSERT(foundRedLine1,
                "Front red line must be visible over back green triangle (drawn back first)");

    std::vector<float> depths1;
    target->readbackDepth(depths1);
    float depth1 = depths1[lineIdx1];

    // Case 2: Invert order: Draw line (front) first, then triangle (back) second
    SoSeparator * root2 = new SoSeparator;
    root2->ref();
    root2->addChild(cam);
    root2->addChild(light);
    root2->addChild(lineSep);
    root2->addChild(triSep);

    action.apply(root2);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Apply root2 must succeed");

    std::vector<uint8_t> pixels2;
    target->readbackRGBA(pixels2);

    bool foundRedLine2 = false;
    size_t lineIdx2 = 0;
    for (int y = 30; y <= 33; ++y) {
      size_t idx = (y * 64 + 32) * 4;
      if (pixels2[idx + 0] > 150 && pixels2[idx + 1] < 100) {
        foundRedLine2 = true;
        lineIdx2 = y * 64 + 32;
        break;
      }
    }
    TEST_ASSERT(foundRedLine2,
                "Depth test must prevent back green triangle from overwriting front red line");

    std::vector<float> depths2;
    target->readbackDepth(depths2);
    float depth2 = depths2[lineIdx2];

    TEST_ASSERT(std::abs(depth1 - depth2) < 1e-4f,
                "Depth values at intersection must match regardless of draw order");

    delete target;
    lineSep->unref();
    triSep->unref();
    root1->unref();
    root2->unref();
    std::cout << "   [PASS] G2.3 Cross-topology depth occlusion" << std::endl;
  }

  // =========================================================================
  // G2.4: Capacity Preflight Policy (lineWidth > 1 and pointSize > 1)
  // =========================================================================
  {
    std::cout << "-> Test G2.4: Rejection of lineWidth > 1 and pointSize > 1..." << std::endl;
    SoWgpuRenderAction action(SbViewportRegion(64, 64));
    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
    action.setRenderTarget(target);

    // 1. Line width > 1.0 must return UNSUPPORTED
    SoSeparator * rootLine = new SoSeparator;
    rootLine->ref();
    SoDrawStyle * dsLine = new SoDrawStyle;
    dsLine->lineWidth = 2.0f;
    rootLine->addChild(dsLine);

    SoCoordinate3 * cLine = new SoCoordinate3;
    cLine->point.set1Value(0, SbVec3f(-1.0f, 0.0f, 0.0f));
    cLine->point.set1Value(1, SbVec3f( 1.0f, 0.0f, 0.0f));
    rootLine->addChild(cLine);

    SoLineSet * ls = new SoLineSet;
    const int32_t nv[] = {2};
    ls->numVertices.setValues(0, 1, nv);
    rootLine->addChild(ls);

    action.apply(rootLine);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED,
                "lineWidth > 1.0 must be rejected with UNSUPPORTED status");

    // 2. Point size > 1.0 must return UNSUPPORTED
    SoSeparator * rootPoint = new SoSeparator;
    rootPoint->ref();
    SoDrawStyle * dsPoint = new SoDrawStyle;
    dsPoint->pointSize = 3.0f;
    rootPoint->addChild(dsPoint);

    SoCoordinate3 * cPoint = new SoCoordinate3;
    cPoint->point.set1Value(0, SbVec3f(0.0f, 0.0f, 0.0f));
    rootPoint->addChild(cPoint);

    SoPointSet * ps = new SoPointSet;
    ps->numPoints.setValue(1);
    rootPoint->addChild(ps);

    action.apply(rootPoint);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED,
                "pointSize > 1.0 must be rejected with UNSUPPORTED status");

    delete target;
    rootLine->unref();
    rootPoint->unref();
    std::cout << "   [PASS] G2.4 Capacity preflight policy" << std::endl;
  }

  std::cout << "\n=======================================================" << std::endl;
  std::cout << "All WgpuGateG2Test test suites PASSED successfully!" << std::endl;
  std::cout << "=======================================================" << std::endl;
  return 0;
}
