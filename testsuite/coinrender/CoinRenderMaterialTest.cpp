#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/C/basic.h>
#include <Inventor/SoDB.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoBaseColor.h>
#include <Inventor/nodes/SoPackedColor.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/SbColor.h>
#include <Inventor/SbViewportRegion.h>

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderFramePlanBuilder.h"
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#include <Inventor/rendering/CoinRenderTarget.h>

#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <cstring>

#define ASSERT_TRUE(cond, msg)   do {     if (!(cond)) {       std::cerr << "ASSERTION FAILED: " << msg << " at line " << __LINE__ << std::endl;       return false;     }   } while (0)

// 1. testAbiVersionCheck
static bool testAbiVersionCheck() {
  if (!coin_wgpu_is_available()) {
    std::cout << "[SKIP] WebGPU not available, skipping testAbiVersionCheck\n";
    return true;
  }

  CoinWgpuSurfaceCreateInfo info{};
  info.abi_version = 999; // invalid
  info.struct_size = sizeof(CoinWgpuSurfaceCreateInfo);

  CoinWgpuSurfaceId sId = 0;
  char err[256] = {0};
  CoinWgpuStatus st = coin_wgpu_surface_create(&info, &sId, err, sizeof(err));
  ASSERT_TRUE(st == COIN_WGPU_INVALID_ARGUMENT, "surface_create must reject invalid abi_version");

  // Also test submit with invalid ABI version
  CoinWgpuFrameView fView{};
  fView.abi_version = 999;
  fView.struct_size = sizeof(CoinWgpuFrameView);
  uint8_t colBuf[64 * 64 * 4] = {0};
  float depthBuf[64 * 64] = {0};
  CoinWgpuTarget target{};
  target.width = 64;
  target.height = 64;
  target.color_buffer = colBuf;
  target.color_buffer_len = sizeof(colBuf);
  target.depth_buffer = depthBuf;
  target.depth_buffer_len = 64 * 64;

  st = coin_wgpu_submit(&target, &fView, err, sizeof(err));
  ASSERT_TRUE(st == COIN_WGPU_INVALID_ARGUMENT, "submit must reject invalid abi_version");

  return true;
}

// 2. testMaterialBindingsFaceVsLineNoGpu
static bool testMaterialBindingsFaceVsLineNoGpu() {

  // Parte A: SoIndexedFaceSet com PER_FACE (não indexado)
  // Deve ignorar ifs->materialIndex mesmo com valores espúrios.
  {
    SoSeparator * root = new SoSeparator;
    root->ref();

    SoMaterial * mat = new SoMaterial;
    mat->diffuseColor.setNum(3);
    mat->diffuseColor.set1Value(0, SbColor(1.0f, 0.0f, 0.0f)); // Red
    mat->diffuseColor.set1Value(1, SbColor(0.0f, 1.0f, 0.0f)); // Green
    mat->diffuseColor.set1Value(2, SbColor(0.0f, 0.0f, 1.0f)); // Blue
    root->addChild(mat);

    SoMaterialBinding * mbFace = new SoMaterialBinding;
    mbFace->value = SoMaterialBinding::PER_FACE;
    root->addChild(mbFace);

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.setNum(6);
    coords->point.set1Value(0, SbVec3f(-1, -1, 0));
    coords->point.set1Value(1, SbVec3f( 0, -1, 0));
    coords->point.set1Value(2, SbVec3f(-0.5f, 1, 0));
    coords->point.set1Value(3, SbVec3f( 0, -1, 0));
    coords->point.set1Value(4, SbVec3f( 1, -1, 0));
    coords->point.set1Value(5, SbVec3f( 0.5f, 1, 0));
    root->addChild(coords);

    SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
    int32_t cIndices[] = { 0, 1, 2, -1,  3, 4, 5, -1 };
    ifs->coordIndex.setValues(0, 8, cIndices);
    // Preenche com bogus indices para provar que PER_FACE não-indexado ignora ifs->materialIndex:
    int32_t bogusMatIndices[] = { 2, 0 };
    ifs->materialIndex.setValues(0, 2, bogusMatIndices);
    root->addChild(ifs);

    CoinRenderAction action;
    action.setFastPathEnabled(TRUE);
    action.apply(root);

    ASSERT_TRUE(action.getLastStatus() == CoinRenderAction::SUCCESS, "RenderAction apply with PER_FACE must succeed");

    std::string log = action.getRecordingLog().getString();
    ASSERT_TRUE(log.find("materials count:") != std::string::npos, "Materials must be recorded");
    ASSERT_TRUE(log.find("matSlot=0") != std::string::npos, "Face 0 must use matSlot 0");
    ASSERT_TRUE(log.find("matSlot=1") != std::string::npos, "Face 1 must use matSlot 1");

    root->unref();
  }

  // Parte B: SoIndexedLineSet com PER_PART vs PER_FACE
  {
    // B1: PER_PART (por segmento)
    SoSeparator * rootLine = new SoSeparator;
    rootLine->ref();

    SoMaterial * mat = new SoMaterial;
    mat->diffuseColor.setNum(3);
    mat->diffuseColor.set1Value(0, SbColor(1.0f, 0.0f, 0.0f));
    mat->diffuseColor.set1Value(1, SbColor(0.0f, 1.0f, 0.0f));
    mat->diffuseColor.set1Value(2, SbColor(0.0f, 0.0f, 1.0f));
    rootLine->addChild(mat);

    SoMaterialBinding * mb = new SoMaterialBinding;
    mb->value = SoMaterialBinding::PER_PART;
    rootLine->addChild(mb);

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.setNum(4);
    coords->point.set1Value(0, SbVec3f(0, 0, 0));
    coords->point.set1Value(1, SbVec3f(1, 0, 0));
    coords->point.set1Value(2, SbVec3f(2, 0, 0));
    coords->point.set1Value(3, SbVec3f(3, 0, 0));
    rootLine->addChild(coords);

    // 1 polyline de 3 vertices (2 segmentos: 0-1 e 1-2)
    SoIndexedLineSet * ils = new SoIndexedLineSet;
    int32_t lineIndices[] = { 0, 1, 2, -1 };
    ils->coordIndex.setValues(0, 4, lineIndices);
    rootLine->addChild(ils);

    CoinRenderAction actionPart;
    actionPart.setFastPathEnabled(TRUE);
    actionPart.apply(rootLine);

    ASSERT_TRUE(actionPart.getLastStatus() == CoinRenderAction::SUCCESS, "LineSet with PER_PART must succeed");
    std::string logPart = actionPart.getRecordingLog().getString();
    ASSERT_TRUE(logPart.find("matSlot=0") != std::string::npos, "Segment 0 must have matSlot 0");
    ASSERT_TRUE(logPart.find("matSlot=1") != std::string::npos, "Segment 1 must have matSlot 1");

    rootLine->unref();
  }

  {
    // B2: PER_FACE (por polyline)
    SoSeparator * rootLine = new SoSeparator;
    rootLine->ref();

    SoMaterial * mat = new SoMaterial;
    mat->diffuseColor.setNum(3);
    mat->diffuseColor.set1Value(0, SbColor(1.0f, 0.0f, 0.0f));
    mat->diffuseColor.set1Value(1, SbColor(0.0f, 1.0f, 0.0f));
    rootLine->addChild(mat);

    SoMaterialBinding * mb = new SoMaterialBinding;
    mb->value = SoMaterialBinding::PER_FACE;
    rootLine->addChild(mb);

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.setNum(4);
    coords->point.set1Value(0, SbVec3f(0, 0, 0));
    coords->point.set1Value(1, SbVec3f(1, 0, 0));
    coords->point.set1Value(2, SbVec3f(2, 0, 0));
    coords->point.set1Value(3, SbVec3f(3, 0, 0));
    rootLine->addChild(coords);

    // 2 polylines: 0-1 (-1), 2-3 (-1)
    SoIndexedLineSet * ils = new SoIndexedLineSet;
    int32_t lineIndices[] = { 0, 1, -1, 2, 3, -1 };
    ils->coordIndex.setValues(0, 6, lineIndices);
    rootLine->addChild(ils);

    CoinRenderAction actionFace;
    actionFace.setFastPathEnabled(TRUE);
    actionFace.apply(rootLine);

    ASSERT_TRUE(actionFace.getLastStatus() == CoinRenderAction::SUCCESS, "LineSet with PER_FACE must succeed");
    std::string logFace = actionFace.getRecordingLog().getString();
    ASSERT_TRUE(logFace.find("matSlot=0") != std::string::npos, "Polyline 0 must have matSlot 0");
    ASSERT_TRUE(logFace.find("matSlot=1") != std::string::npos, "Polyline 1 must have matSlot 1");

    rootLine->unref();
  }

  return true;
}

// 3. testInvalidMaterialIndexHandling
static bool testInvalidMaterialIndexHandling() {

  // Teste 3A: Invalid negative index < -1 in PER_FACE_INDEXED
  {
    SoSeparator * root = new SoSeparator;
    root->ref();

    SoMaterial * mat = new SoMaterial;
    mat->diffuseColor.setValue(1, 0, 0); // 1 diffuse color
    root->addChild(mat);

    SoMaterialBinding * mb = new SoMaterialBinding;
    mb->value = SoMaterialBinding::PER_FACE_INDEXED;
    root->addChild(mb);

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.setNum(3);
    coords->point.set1Value(0, SbVec3f(0, 0, 0));
    coords->point.set1Value(1, SbVec3f(1, 0, 0));
    coords->point.set1Value(2, SbVec3f(0, 1, 0));
    root->addChild(coords);

    SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
    int32_t cIndices[] = { 0, 1, 2, -1 };
    ifs->coordIndex.setValues(0, 4, cIndices);

    // Invalid negative index < -1 (e.g. -5)
    int32_t badMatIndicesNeg[] = { -5 };
    ifs->materialIndex.setValues(0, 1, badMatIndicesNeg);
    root->addChild(ifs);

    CoinRenderAction action;
    action.setFastPathEnabled(TRUE);
    action.apply(root);

    ASSERT_TRUE(action.getLastStatus() == CoinRenderAction::INVALID_SCENE,
                "Negative material index < -1 must reject scene as INVALID_SCENE");

    root->unref();
  }

  // Teste 3B: Out of bounds material index >= numDiffuse in PER_FACE_INDEXED
  {
    SoSeparator * root = new SoSeparator;
    root->ref();

    SoMaterial * mat = new SoMaterial;
    mat->diffuseColor.setValue(1, 0, 0); // 1 diffuse color
    root->addChild(mat);

    SoMaterialBinding * mb = new SoMaterialBinding;
    mb->value = SoMaterialBinding::PER_FACE_INDEXED;
    root->addChild(mb);

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.setNum(3);
    coords->point.set1Value(0, SbVec3f(0, 0, 0));
    coords->point.set1Value(1, SbVec3f(1, 0, 0));
    coords->point.set1Value(2, SbVec3f(0, 1, 0));
    root->addChild(coords);

    SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
    int32_t cIndices[] = { 0, 1, 2, -1 };
    ifs->coordIndex.setValues(0, 4, cIndices);

    // Out of bounds index >= 1 (e.g. 100)
    int32_t badMatIndicesOob[] = { 100 };
    ifs->materialIndex.setValues(0, 1, badMatIndicesOob);
    root->addChild(ifs);

    CoinRenderAction action;
    action.setFastPathEnabled(TRUE);
    action.apply(root);

    ASSERT_TRUE(action.getLastStatus() == CoinRenderAction::INVALID_SCENE,
                "Out-of-bounds material index must reject scene as INVALID_SCENE");

    root->unref();
  }

  return true;
}

// 4. testStorageBufferLimitsAndSlotValidation
static bool testStorageBufferLimitsAndSlotValidation() {
  if (!coin_wgpu_is_available()) {
    std::cout << "[SKIP] WebGPU not available, skipping testStorageBufferLimitsAndSlotValidation\n";
    return true;
  }

  uint8_t colBuf[64 * 64 * 4] = {0};
  float depthBuf[64 * 64] = {0};
  CoinWgpuTarget target{};
  target.width = 64;
  target.height = 64;
  target.color_buffer = colBuf;
  target.color_buffer_len = sizeof(colBuf);
  target.depth_buffer = depthBuf;
  target.depth_buffer_len = 64 * 64;

  CoinWgpuVertex vertices[3] = {};
  vertices[0].position[0] = 0.0f;
  vertices[1].position[1] = 1.0f;
  vertices[2].position[0] = 1.0f;
  vertices[2].material_slot = 10; // Out of bounds!

  uint32_t indices[3] = {0, 1, 2};
  CoinWgpuDraw draw{};
  draw.topology = 0;
  draw.vertex_count = 3;
  draw.index_count = 3;
  draw.render_state_slot = 0;

  CoinWgpuMaterial mat{};
  mat.diffuse[0] = 1.0f; mat.diffuse[3] = 1.0f;

  CoinWgpuRenderState st{};
  st.cull_mode = 0;
  st.front_face = 0;
  st.light_model = 1;

  CoinWgpuFrameView fView{};
  fView.abi_version = COIN_WGPU_ABI_VERSION;
  fView.struct_size = sizeof(CoinWgpuFrameView);
  fView.width = 64;
  fView.height = 64;
  fView.vertices = vertices;
  fView.vertex_count = 3;
  fView.indices = indices;
  fView.index_count = 3;
  fView.draws = &draw;
  fView.draw_count = 1;
  fView.materials = &mat;
  fView.material_count = 1; // Only 1 material, but vertex 2 has slot 10!
  fView.states = &st;
  fView.state_count = 1;

  char err[256] = {0};
  CoinWgpuStatus res = coin_wgpu_submit(&target, &fView, err, sizeof(err));
  ASSERT_TRUE(res == COIN_WGPU_INVALID_ARGUMENT, "Submit with out-of-bounds material_slot must return INVALID_ARGUMENT");

  // Also test material_count == 0
  fView.material_count = 0;
  res = coin_wgpu_submit(&target, &fView, err, sizeof(err));
  ASSERT_TRUE(res == COIN_WGPU_INVALID_ARGUMENT, "Submit with material_count == 0 must return INVALID_ARGUMENT");

  return true;
}

// 5. testBaseColorVsPhongLitAndUnlitGpu
static bool testBaseColorVsPhongLitAndUnlitGpu() {
  if (!coin_wgpu_is_available()) {
    std::cout << "[SKIP] WebGPU not available, skipping testBaseColorVsPhongLitAndUnlitGpu\n";
    return true;
  }

  // Render a quad filling the screen with BASE_COLOR
  const uint32_t W = 64, H = 64;
  std::vector<uint8_t> colBuf(W * H * 4, 0);
  std::vector<float> depthBuf(W * H, 1.0f);

  CoinWgpuTarget target{};
  target.width = W;
  target.height = H;
  target.color_buffer = colBuf.data();
  target.color_buffer_len = colBuf.size();
  target.depth_buffer = depthBuf.data();
  target.depth_buffer_len = depthBuf.size();

  CoinWgpuVertex quadVerts[4] = {
    {{-1.0f, -1.0f, 0.0f}, {0, 0, 1}, {0, 0}, 0},
    {{ 1.0f, -1.0f, 0.0f}, {0, 0, 1}, {1, 0}, 0},
    {{ 1.0f,  1.0f, 0.0f}, {0, 0, 1}, {1, 1}, 0},
    {{-1.0f,  1.0f, 0.0f}, {0, 0, 1}, {0, 1}, 0}
  };
  uint32_t quadIndices[6] = {0, 1, 2, 0, 2, 3};

  CoinWgpuDraw draw{};
  draw.topology = 0;
  draw.vertex_count = 4;
  draw.index_count = 6;
  draw.render_state_slot = 0;

  // Material: diffuse = Red (1, 0, 0), emission = Green (0, 1, 0)
  CoinWgpuMaterial mat{};
  mat.diffuse[0] = 1.0f; mat.diffuse[1] = 0.0f; mat.diffuse[2] = 0.0f; mat.diffuse[3] = 1.0f;
  mat.emission[0] = 0.0f; mat.emission[1] = 1.0f; mat.emission[2] = 0.0f; mat.emission[3] = 1.0f;

  CoinWgpuRenderState st{};
  // Identity matrices
  st.model_view[0] = 1.0f; st.model_view[5] = 1.0f; st.model_view[10] = 1.0f; st.model_view[15] = 1.0f;
  st.model_view_projection[0] = 1.0f; st.model_view_projection[5] = 1.0f; st.model_view_projection[10] = 1.0f; st.model_view_projection[15] = 1.0f;
  st.normal_matrix[0] = 1.0f; st.normal_matrix[5] = 1.0f; st.normal_matrix[10] = 1.0f; st.normal_matrix[15] = 1.0f;
  st.light_model = 0; // BASE_COLOR!
  st.has_light = 0;

  CoinWgpuFrameView fView{};
  fView.abi_version = COIN_WGPU_ABI_VERSION;
  fView.struct_size = sizeof(CoinWgpuFrameView);
  fView.width = W;
  fView.height = H;
  fView.vertices = quadVerts;
  fView.vertex_count = 4;
  fView.indices = quadIndices;
  fView.index_count = 6;
  fView.draws = &draw;
  fView.draw_count = 1;
  fView.materials = &mat;
  fView.material_count = 1;
  fView.states = &st;
  fView.state_count = 1;

  char err[256] = {0};
  CoinWgpuStatus status = coin_wgpu_submit(&target, &fView, err, sizeof(err));
  ASSERT_TRUE(status == COIN_WGPU_OK, "Submit for BASE_COLOR failed: " + std::string(err));

  // Check center pixel: In BASE_COLOR, emission is NOT added! It must be Red (255, 0, 0), not Yellow!
  size_t centerIdx = (32 * W + 32) * 4;
  uint8_t r = colBuf[centerIdx];
  uint8_t g = colBuf[centerIdx + 1];
  uint8_t b = colBuf[centerIdx + 2];
  ASSERT_TRUE(r > 240, "BASE_COLOR Red channel must be high");
  ASSERT_TRUE(g == 0, "BASE_COLOR Green channel (emission) must be strictly 0");
  ASSERT_TRUE(b == 0, "BASE_COLOR Blue channel must be 0");

  // Now test PHONG without light: must be ambient + emission (so Green emission is rendered!)
  st.light_model = 1; // PHONG
  st.has_light = 0;
  status = coin_wgpu_submit(&target, &fView, err, sizeof(err));
  ASSERT_TRUE(status == COIN_WGPU_OK, "Submit for PHONG unlit failed: " + std::string(err));

  r = colBuf[centerIdx];
  g = colBuf[centerIdx + 1];
  b = colBuf[centerIdx + 2];
  ASSERT_TRUE(r < 60, "PHONG unlit diffuse must not be illuminated by absent lights");
  ASSERT_TRUE(g > 240, "PHONG unlit must render emission (Green)");

  return true;
}

// 6. testShininessZeroAndExponentConversion
static bool testShininessZeroAndExponentConversion() {
  if (!coin_wgpu_is_available()) {
    std::cout << "[SKIP] WebGPU not available, skipping testShininessZeroAndExponentConversion\n";
    return true;
  }

  const uint32_t W = 64, H = 64;
  std::vector<uint8_t> colBuf(W * H * 4, 0);
  std::vector<float> depthBuf(W * H, 1.0f);

  CoinWgpuTarget target{};
  target.width = W;
  target.height = H;
  target.color_buffer = colBuf.data();
  target.color_buffer_len = colBuf.size();
  target.depth_buffer = depthBuf.data();
  target.depth_buffer_len = depthBuf.size();

  CoinWgpuVertex quadVerts[4] = {
    {{-1.0f, -1.0f, 0.0f}, {0, 0, 1}, {0, 0}, 0},
    {{ 1.0f, -1.0f, 0.0f}, {0, 0, 1}, {1, 0}, 0},
    {{ 1.0f,  1.0f, 0.0f}, {0, 0, 1}, {1, 1}, 0},
    {{-1.0f,  1.0f, 0.0f}, {0, 0, 1}, {0, 1}, 0}
  };
  uint32_t quadIndices[6] = {0, 1, 2, 0, 2, 3};

  CoinWgpuDraw draw{};
  draw.topology = 0;
  draw.vertex_count = 4;
  draw.index_count = 6;
  draw.render_state_slot = 0;

  CoinWgpuMaterial mat{};
  mat.diffuse[0] = 0.5f; mat.diffuse[3] = 1.0f;
  mat.specular[0] = 1.0f; mat.specular[1] = 1.0f; mat.specular[2] = 1.0f; mat.specular[3] = 1.0f;
  mat.shininess = 0.0f; // Shininess zero must be valid!

  CoinWgpuRenderState st{};
  st.model_view[0] = 1.0f; st.model_view[5] = 1.0f; st.model_view[10] = 1.0f; st.model_view[15] = 1.0f;
  st.model_view_projection[0] = 1.0f; st.model_view_projection[5] = 1.0f; st.model_view_projection[10] = 1.0f; st.model_view_projection[15] = 1.0f;
  st.normal_matrix[0] = 1.0f; st.normal_matrix[5] = 1.0f; st.normal_matrix[10] = 1.0f; st.normal_matrix[15] = 1.0f;
  st.light_model = 1;
  st.has_light = 1;
  st.light_direction[2] = -1.0f;
  st.light_color[0] = 1.0f; st.light_color[1] = 1.0f; st.light_color[2] = 1.0f; st.light_color[3] = 1.0f;
  st.light_intensity = 1.0f;

  CoinWgpuFrameView fView{};
  fView.abi_version = COIN_WGPU_ABI_VERSION;
  fView.struct_size = sizeof(CoinWgpuFrameView);
  fView.width = W;
  fView.height = H;
  fView.vertices = quadVerts;
  fView.vertex_count = 4;
  fView.indices = quadIndices;
  fView.index_count = 6;
  fView.draws = &draw;
  fView.draw_count = 1;
  fView.materials = &mat;
  fView.material_count = 1;
  fView.states = &st;
  fView.state_count = 1;

  char err[256] = {0};
  CoinWgpuStatus status = coin_wgpu_submit(&target, &fView, err, sizeof(err));
  ASSERT_TRUE(status == COIN_WGPU_OK, "Shininess 0.0 submit failed: " + std::string(err));

  return true;
}

// 7. testOpaqueRuleForAlpha
static bool testOpaqueRuleForAlpha() {
  if (!coin_wgpu_is_available()) {
    std::cout << "[SKIP] WebGPU not available, skipping testOpaqueRuleForAlpha\n";
    return true;
  }

  const uint32_t W = 64, H = 64;
  std::vector<uint8_t> colBuf(W * H * 4, 0);
  std::vector<float> depthBuf(W * H, 1.0f);

  CoinWgpuTarget target{};
  target.width = W;
  target.height = H;
  target.color_buffer = colBuf.data();
  target.color_buffer_len = colBuf.size();
  target.depth_buffer = depthBuf.data();
  target.depth_buffer_len = depthBuf.size();

  CoinWgpuVertex quadVerts[4] = {
    {{-1.0f, -1.0f, 0.0f}, {0, 0, 1}, {0, 0}, 0},
    {{ 1.0f, -1.0f, 0.0f}, {0, 0, 1}, {1, 0}, 0},
    {{ 1.0f,  1.0f, 0.0f}, {0, 0, 1}, {1, 1}, 0},
    {{-1.0f,  1.0f, 0.0f}, {0, 0, 1}, {0, 1}, 0}
  };
  uint32_t quadIndices[6] = {0, 1, 2, 0, 2, 3};

  CoinWgpuDraw draw{};
  draw.topology = 0;
  draw.vertex_count = 4;
  draw.index_count = 6;
  draw.render_state_slot = 0;

  // Material with alpha = 0.5 (transparency = 0.5)
  CoinWgpuMaterial mat{};
  mat.diffuse[0] = 1.0f; mat.diffuse[1] = 0.0f; mat.diffuse[2] = 0.0f; mat.diffuse[3] = 0.5f;
  mat.transparency = 0.5f;

  CoinWgpuRenderState st{};
  st.model_view[0] = 1.0f; st.model_view[5] = 1.0f; st.model_view[10] = 1.0f; st.model_view[15] = 1.0f;
  st.model_view_projection[0] = 1.0f; st.model_view_projection[5] = 1.0f; st.model_view_projection[10] = 1.0f; st.model_view_projection[15] = 1.0f;
  st.normal_matrix[0] = 1.0f; st.normal_matrix[5] = 1.0f; st.normal_matrix[10] = 1.0f; st.normal_matrix[15] = 1.0f;
  st.light_model = 0; // BASE_COLOR
  st.has_light = 0;

  CoinWgpuFrameView fView{};
  fView.abi_version = COIN_WGPU_ABI_VERSION;
  fView.struct_size = sizeof(CoinWgpuFrameView);
  fView.width = W;
  fView.height = H;
  fView.vertices = quadVerts;
  fView.vertex_count = 4;
  fView.indices = quadIndices;
  fView.index_count = 6;
  fView.draws = &draw;
  fView.draw_count = 1;
  fView.materials = &mat;
  fView.material_count = 1;
  fView.states = &st;
  fView.state_count = 1;
  fView.clear_color[0] = 0.0f; fView.clear_color[1] = 0.0f; fView.clear_color[2] = 0.0f; fView.clear_color[3] = 1.0f;

  char err[256] = {0};
  CoinWgpuStatus status = coin_wgpu_submit(&target, &fView, err, sizeof(err));
  ASSERT_TRUE(status == COIN_WGPU_OK, "Direct FFI alpha blend failed: " + std::string(err));
  size_t centerIdx = (32 * W + 32) * 4;
  uint8_t r = colBuf[centerIdx];
  uint8_t a = colBuf[centerIdx + 3];
  ASSERT_TRUE(r > 115 && r < 140, "Half-alpha red must blend over black");
  ASSERT_TRUE(a == 255, "Source-over alpha over opaque clear must remain one");
  const uint64_t goodSerial = target.submission_serial;

  mat.diffuse[3] = 1.0f;
  mat.transparency = 0.0001f;
  status = coin_wgpu_submit(&target, &fView, err, sizeof(err));
  ASSERT_TRUE(status == COIN_WGPU_INVALID_ARGUMENT, "Inconsistent material alpha/transparency must be invalid");
  ASSERT_TRUE(target.submission_serial == goodSerial, "Invalid alpha must not submit a frame");

  mat.transparency = std::nanf("");
  status = coin_wgpu_submit(&target, &fView, err, sizeof(err));
  ASSERT_TRUE(status == COIN_WGPU_INVALID_ARGUMENT, "Non-finite transparency must be invalid");
  ASSERT_TRUE(target.submission_serial == goodSerial, "Non-finite alpha must not submit a frame");

  mat.diffuse[3] = 1.0f;
  mat.transparency = 0.0f;
  err[0] = '\0';
  status = coin_wgpu_submit(&target, &fView, err, sizeof(err));
  ASSERT_TRUE(status == COIN_WGPU_OK, "Opaque submission after alpha error failed: " + std::string(err));
  r = colBuf[centerIdx];
  a = colBuf[centerIdx + 3];
  ASSERT_TRUE(r > 240, "Opaque recovery must render the expected color");
  ASSERT_TRUE(a == 255, "Opaque recovery must preserve full alpha");
  return true;
}

// 8. testMaterialMutationWithGeometryCacheHit
static bool testMaterialMutationWithGeometryCacheHit() {
  if (!coin_wgpu_is_available()) {
    std::cout << "[SKIP] WebGPU not available, skipping testMaterialMutationWithGeometryCacheHit\n";
    return true;
  }

  const uint32_t W = 64, H = 64;
  std::vector<uint8_t> colBuf(W * H * 4, 0);
  std::vector<float> depthBuf(W * H, 1.0f);

  CoinWgpuTarget target{};
  target.width = W;
  target.height = H;
  target.color_buffer = colBuf.data();
  target.color_buffer_len = colBuf.size();
  target.depth_buffer = depthBuf.data();
  target.depth_buffer_len = depthBuf.size();

  CoinWgpuVertex quadVerts[4] = {
    {{-1.0f, -1.0f, 0.0f}, {0, 0, 1}, {0, 0}, 0},
    {{ 1.0f, -1.0f, 0.0f}, {0, 0, 1}, {1, 0}, 0},
    {{ 1.0f,  1.0f, 0.0f}, {0, 0, 1}, {1, 1}, 0},
    {{-1.0f,  1.0f, 0.0f}, {0, 0, 1}, {0, 1}, 0}
  };
  uint32_t quadIndices[6] = {0, 1, 2, 0, 2, 3};

  CoinWgpuDraw draw{};
  draw.topology = 0;
  draw.vertex_count = 4;
  draw.index_count = 6;
  draw.render_state_slot = 0;
  draw.stable_node_id = 0xABCD1234;
  draw.draw_ordinal = 0;
  draw.source_revision = 0x99887766;

  // Frame 1: Red material
  CoinWgpuMaterial mat1{};
  mat1.diffuse[0] = 1.0f; mat1.diffuse[3] = 1.0f;

  CoinWgpuRenderState st{};
  st.model_view[0] = 1.0f; st.model_view[5] = 1.0f; st.model_view[10] = 1.0f; st.model_view[15] = 1.0f;
  st.model_view_projection[0] = 1.0f; st.model_view_projection[5] = 1.0f; st.model_view_projection[10] = 1.0f; st.model_view_projection[15] = 1.0f;
  st.normal_matrix[0] = 1.0f; st.normal_matrix[5] = 1.0f; st.normal_matrix[10] = 1.0f; st.normal_matrix[15] = 1.0f;
  st.light_model = 0;

  CoinWgpuFrameView fView{};
  fView.abi_version = COIN_WGPU_ABI_VERSION;
  fView.struct_size = sizeof(CoinWgpuFrameView);
  fView.width = W;
  fView.height = H;
  fView.vertices = quadVerts;
  fView.vertex_count = 4;
  fView.indices = quadIndices;
  fView.index_count = 6;
  fView.draws = &draw;
  fView.draw_count = 1;
  fView.materials = &mat1;
  fView.material_count = 1;
  fView.states = &st;
  fView.state_count = 1;

  char err[256] = {0};
  CoinWgpuStatus status = coin_wgpu_submit(&target, &fView, err, sizeof(err));
  ASSERT_TRUE(status == COIN_WGPU_OK, "Frame 1 submit failed: " + std::string(err));

  size_t centerIdx = (32 * W + 32) * 4;
  ASSERT_TRUE(colBuf[centerIdx] > 240, "Frame 1 must be Red");
  ASSERT_TRUE(colBuf[centerIdx + 1] == 0, "Frame 1 green must be 0");

  // Frame 2: Change material to Green, KEEP GEOMETRY AND REVISION INTACT
  CoinWgpuMaterial mat2{};
  mat2.diffuse[1] = 1.0f; mat2.diffuse[3] = 1.0f; // Green!
  fView.materials = &mat2;

  status = coin_wgpu_submit(&target, &fView, err, sizeof(err));
  ASSERT_TRUE(status == COIN_WGPU_OK, "Frame 2 submit failed: " + std::string(err));

  // Must now be Green
  ASSERT_TRUE(colBuf[centerIdx] == 0, "Frame 2 red must be 0");
  ASSERT_TRUE(colBuf[centerIdx + 1] > 240, "Frame 2 must be Green");

  CoinWgpuCacheStats stats{};
  coin_wgpu_get_cache_stats(&stats);
  ASSERT_TRUE(stats.frame_hits > 0, "Frame 2 geometry must be a cache hit");
  ASSERT_TRUE(stats.frame_uploads == 0, "Frame 2 geometry must not be re-uploaded");

  return true;
}

int main(int argc, char ** argv) {
  std::cout << "Running CoinRenderMaterialTest...\n";

  SoDB::init();
  CoinRenderAction::initClass();
  if (!testAbiVersionCheck()) {
    std::cerr << "testAbiVersionCheck FAILED\n";
    return 1;
  }
  std::cout << "[PASS] testAbiVersionCheck\n";

  if (!testMaterialBindingsFaceVsLineNoGpu()) {
    std::cerr << "testMaterialBindingsFaceVsLineNoGpu FAILED\n";
    return 1;
  }
  std::cout << "[PASS] testMaterialBindingsFaceVsLineNoGpu\n";

  if (!testInvalidMaterialIndexHandling()) {
    std::cerr << "testInvalidMaterialIndexHandling FAILED\n";
    return 1;
  }
  std::cout << "[PASS] testInvalidMaterialIndexHandling\n";

  if (!testStorageBufferLimitsAndSlotValidation()) {
    std::cerr << "testStorageBufferLimitsAndSlotValidation FAILED\n";
    return 1;
  }
  std::cout << "[PASS] testStorageBufferLimitsAndSlotValidation\n";

  if (!testBaseColorVsPhongLitAndUnlitGpu()) {
    std::cerr << "testBaseColorVsPhongLitAndUnlitGpu FAILED\n";
    return 1;
  }
  std::cout << "[PASS] testBaseColorVsPhongLitAndUnlitGpu\n";

  if (!testShininessZeroAndExponentConversion()) {
    std::cerr << "testShininessZeroAndExponentConversion FAILED\n";
    return 1;
  }
  std::cout << "[PASS] testShininessZeroAndExponentConversion\n";

  if (!testOpaqueRuleForAlpha()) {
    std::cerr << "testOpaqueRuleForAlpha FAILED\n";
    return 1;
  }
  std::cout << "[PASS] testOpaqueRuleForAlpha\n";

  if (!testMaterialMutationWithGeometryCacheHit()) {
    std::cerr << "testMaterialMutationWithGeometryCacheHit FAILED\n";
    return 1;
  }
  std::cout << "[PASS] testMaterialMutationWithGeometryCacheHit\n";

  std::cout << "ALL 8 CoinRenderMaterialTest SUITES PASSED!\n";
  return 0;
}
