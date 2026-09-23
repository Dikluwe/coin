#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/C/basic.h>
#include <Inventor/SoDB.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTexture2Transform.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>

#include <iostream>
#include <vector>
#include <cstring>

#define ASSERT_TRUE(cond, msg) \
  do { \
    if (!(cond)) { \
      std::cerr << "ASSERTION FAILED: " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
      return false; \
    } \
  } while (0)

// Helper to construct a textured quad (Z=0, square -1 to 1)
static SoSeparator * createTexturedQuad(SoTexture2 * texNode, bool addTexCoords = true) {
  SoSeparator * sep = new SoSeparator;

  if (texNode) {
    sep->addChild(texNode);
  }

  if (addTexCoords) {
    SoTextureCoordinate2 * tc = new SoTextureCoordinate2;
    tc->point.setNum(4);
    tc->point.set1Value(0, SbVec2f(0.0f, 0.0f));
    tc->point.set1Value(1, SbVec2f(1.0f, 0.0f));
    tc->point.set1Value(2, SbVec2f(1.0f, 1.0f));
    tc->point.set1Value(3, SbVec2f(0.0f, 1.0f));
    sep->addChild(tc);
  }

  SoCoordinate3 * coords = new SoCoordinate3;
  coords->point.setNum(4);
  coords->point.set1Value(0, SbVec3f(-1.0f, -1.0f, 0.0f));
  coords->point.set1Value(1, SbVec3f( 1.0f, -1.0f, 0.0f));
  coords->point.set1Value(2, SbVec3f( 1.0f,  1.0f, 0.0f));
  coords->point.set1Value(3, SbVec3f(-1.0f,  1.0f, 0.0f));
  sep->addChild(coords);

  SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
  const int32_t indices[] = {0, 1, 2, 3, -1};
  ifs->coordIndex.setValues(0, 5, indices);
  if (addTexCoords) {
    ifs->textureCoordIndex.setValues(0, 5, indices);
  }
  sep->addChild(ifs);

  return sep;
}

// 1. testTextureFormats1to4Components
static bool testTextureFormats1to4Components() {
  std::cout << "-> Test 1: Texture formats (1, 2, 3, 4 components) with MODULATE..." << std::endl;

  int componentsToTest[] = {1, 2, 3, 4};
  for (int nc : componentsToTest) {
    SoSeparator * root = new SoSeparator;
    root->ref();

    SoPerspectiveCamera * cam = new SoPerspectiveCamera;
    cam->position.setValue(0.0f, 0.0f, 2.5f);
    cam->pointAt(SbVec3f(0.0f, 0.0f, 0.0f));
    root->addChild(cam);

    SoTexture2 * tex = new SoTexture2;
    tex->model.setValue(SoTexture2::MODULATE);
    const int W = 4, H = 4;
    std::vector<unsigned char> bytes(W * H * nc);
    for (size_t i = 0; i < bytes.size(); ++i) {
      if (nc == 1) {
        bytes[i] = 180; // Luminance
      } else if (nc == 2) {
        bytes[i] = (i % 2 == 0) ? 200 : 255; // L, A (A must be 255 for opaque 3B profile)
      } else if (nc == 3) {
        bytes[i] = (i % 3 == 0) ? 255 : ((i % 3 == 1) ? 128 : 64); // RGB
      } else if (nc == 4) {
        bytes[i] = (i % 4 == 3) ? 255 : 220; // RGBA with A=255
      }
    }
    tex->image.setValue(SbVec2s(W, H), nc, bytes.data());

    root->addChild(createTexturedQuad(tex, true));

    // Recording verification (Mode 0)
    SoWgpuRenderAction recordAction(SbViewportRegion(64, 64));
    recordAction.apply(root);
    SbString recLog = recordAction.getRecordingLog();
    ASSERT_TRUE(recLog.find("textures count: 1") != -1, "Recording log must contain textures count: 1");
    ASSERT_TRUE(recLog.find("hasTex=1") != -1, "Recording log must indicate hasTex=1");
    ASSERT_TRUE(recLog.find("texModel=MODULATE") != -1, "Recording log must indicate texModel=MODULATE");

    // Offscreen render verification
    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
    SoWgpuRenderAction action(SbViewportRegion(64, 64));
    action.setRenderTarget(target);
    action.apply(root);

    ASSERT_TRUE(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
                "Render of format must succeed");

    std::vector<uint8_t> col;
    target->readbackRGBA(col);
    ASSERT_TRUE(!col.empty(), "Color buffer must be present");
    size_t centerIdx = (32 * 64 + 32) * 4;
    ASSERT_TRUE(col[centerIdx + 3] == 255, "Alpha must be 255");
    ASSERT_TRUE(col[centerIdx + 0] > 0 || col[centerIdx + 1] > 0 || col[centerIdx + 2] > 0,
                "Center pixel must have non-zero color");

    delete target;
    root->unref();
  }

  return true;
}

// 2. testModulateModelStrict
static bool testModulateModelStrict() {
  std::cout << "-> Test 2: MODULATE model color verification..." << std::endl;

  SoSeparator * root = new SoSeparator;
  root->ref();

  SoPerspectiveCamera * cam = new SoPerspectiveCamera;
  cam->position.setValue(0.0f, 0.0f, 2.5f);
  cam->pointAt(SbVec3f(0.0f, 0.0f, 0.0f));
  root->addChild(cam);

  // Directional light white
  SoDirectionalLight * lt = new SoDirectionalLight;
  lt->direction.setValue(0.0f, 0.0f, -1.0f);
  lt->color.setValue(1.0f, 1.0f, 1.0f);
  lt->intensity.setValue(1.0f);
  root->addChild(lt);

  // Material yellow: R=1, G=1, B=0
  SoMaterial * mat = new SoMaterial;
  mat->diffuseColor.setValue(1.0f, 1.0f, 0.0f);
  mat->ambientColor.setValue(0.0f, 0.0f, 0.0f);
  mat->specularColor.setValue(0.0f, 0.0f, 0.0f);
  mat->emissiveColor.setValue(0.0f, 0.0f, 0.0f);
  root->addChild(mat);

  // Texture cyan: R=0, G=255, B=255
  SoTexture2 * tex = new SoTexture2;
  tex->model.setValue(SoTexture2::MODULATE);
  unsigned char cyanPixels[4 * 4 * 3];
  for (int i = 0; i < 16; ++i) {
    cyanPixels[i * 3 + 0] = 0;   // R = 0
    cyanPixels[i * 3 + 1] = 255; // G = 1
    cyanPixels[i * 3 + 2] = 255; // B = 1
  }
  tex->image.setValue(SbVec2s(4, 4), 3, cyanPixels);

  root->addChild(createTexturedQuad(tex, true));

  SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
  SoWgpuRenderAction action(SbViewportRegion(64, 64));
  action.setRenderTarget(target);
  action.apply(root);

  ASSERT_TRUE(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Render must succeed");

  // Yellow (1, 1, 0) MODULATE Cyan (0, 1, 1) = Green (0, 1, 0)
  std::vector<uint8_t> col;
  target->readbackRGBA(col);
  size_t centerIdx = (32 * 64 + 32) * 4;
  uint8_t r = col[centerIdx + 0];
  uint8_t g = col[centerIdx + 1];
  uint8_t b = col[centerIdx + 2];

  ASSERT_TRUE(g > 150, "Green channel must be high (> 150)");
  ASSERT_TRUE(r < 30, "Red channel must be near zero (< 30) due to texture modulation");
  ASSERT_TRUE(b < 30, "Blue channel must be near zero (< 30) due to material modulation");

  delete target;
  root->unref();
  return true;
}

// 3. testUnsupportedRejections
static bool testUnsupportedRejections() {
  std::cout << "-> Test 3: Strict UNSUPPORTED rejections for Subwave 3B profile..." << std::endl;

  // Case 3A: Texture with alpha < 255
  {
    SoSeparator * root = new SoSeparator;
    root->ref();
    SoTexture2 * tex = new SoTexture2;
    unsigned char rgbaSemi[4 * 4 * 4];
    for (int i = 0; i < 16; ++i) {
      rgbaSemi[i * 4 + 0] = 200;
      rgbaSemi[i * 4 + 1] = 200;
      rgbaSemi[i * 4 + 2] = 200;
      rgbaSemi[i * 4 + 3] = (i == 0 ? 128 : 255); // Alpha < 255!
    }
    tex->image.setValue(SbVec2s(4, 4), 4, rgbaSemi);
    root->addChild(createTexturedQuad(tex, true));

    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(32, 32));
    SoWgpuRenderAction action(SbViewportRegion(32, 32));
    action.setRenderTarget(target);
    action.apply(root);

    ASSERT_TRUE(action.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED,
                "Texture with alpha < 255 must be rejected with UNSUPPORTED");
    delete target;
    root->unref();
  }

  // Case 3B: Material with transparency > 0 and texture
  {
    SoSeparator * root = new SoSeparator;
    root->ref();
    SoMaterial * mat = new SoMaterial;
    mat->transparency.setValue(0.5f);
    root->addChild(mat);

    SoTexture2 * tex = new SoTexture2;
    unsigned char rgb[4 * 4 * 3];
    std::memset(rgb, 200, sizeof(rgb));
    tex->image.setValue(SbVec2s(4, 4), 3, rgb);
    root->addChild(createTexturedQuad(tex, true));

    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(32, 32));
    SoWgpuRenderAction action(SbViewportRegion(32, 32));
    action.setRenderTarget(target);
    action.apply(root);

    ASSERT_TRUE(action.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED,
                "Material with transparency > 0 with texture must be rejected with UNSUPPORTED");
    delete target;
    root->unref();
  }

  // Case 3C: Texture on lines (SoIndexedLineSet)
  {
    SoSeparator * root = new SoSeparator;
    root->ref();
    SoTexture2 * tex = new SoTexture2;
    unsigned char rgb[4 * 4 * 3];
    std::memset(rgb, 200, sizeof(rgb));
    tex->image.setValue(SbVec2s(4, 4), 3, rgb);
    root->addChild(tex);

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.set1Value(0, SbVec3f(0.0f, 0.0f, 0.0f));
    coords->point.set1Value(1, SbVec3f(1.0f, 1.0f, 0.0f));
    root->addChild(coords);

    SoIndexedLineSet * ils = new SoIndexedLineSet;
    const int32_t indices[] = {0, 1, -1};
    ils->coordIndex.setValues(0, 3, indices);
    root->addChild(ils);

    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(32, 32));
    SoWgpuRenderAction action(SbViewportRegion(32, 32));
    action.setRenderTarget(target);
    action.apply(root);

    ASSERT_TRUE(action.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED,
                "Textured lines must be rejected with UNSUPPORTED");
    delete target;
    root->unref();
  }

  // Case 3D: Unsupported texture model (e.g. REPLACE, DECAL, BLEND)
  {
    SoSeparator * root = new SoSeparator;
    root->ref();
    SoTexture2 * tex = new SoTexture2;
    tex->model.setValue(SoTexture2::REPLACE); // REPLACE unsupported in 3B
    unsigned char rgb[4 * 4 * 3];
    std::memset(rgb, 200, sizeof(rgb));
    tex->image.setValue(SbVec2s(4, 4), 3, rgb);
    root->addChild(createTexturedQuad(tex, true));

    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(32, 32));
    SoWgpuRenderAction action(SbViewportRegion(32, 32));
    action.setRenderTarget(target);
    action.apply(root);

    ASSERT_TRUE(action.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED,
                "Model REPLACE must be rejected with UNSUPPORTED");
    delete target;
    root->unref();
  }

  // Case 3E: Missing/pending texture file dummy rejection
  {
    SoSeparator * root = new SoSeparator;
    root->ref();
    SoTexture2 * tex = new SoTexture2;
    tex->filename.setValue("non_existent_file_dummy_test_123.png");
    root->addChild(createTexturedQuad(tex, true));

    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(32, 32));
    SoWgpuRenderAction action(SbViewportRegion(32, 32));
    action.setRenderTarget(target);
    action.apply(root);

    ASSERT_TRUE(action.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED,
                "Dummy missing texture must be rejected with UNSUPPORTED");
    delete target;
    root->unref();
  }

  // Case 3F: Unsupported texture quality
  {
    SoSeparator * root = new SoSeparator;
    root->ref();
    SoComplexity * comp = new SoComplexity;
    comp->textureQuality.setValue(0.8f); // 0.8 is unsupported (only 0.0 and 0.5)
    root->addChild(comp);

    SoTexture2 * tex = new SoTexture2;
    unsigned char rgb[4 * 4 * 3];
    std::memset(rgb, 200, sizeof(rgb));
    tex->image.setValue(SbVec2s(4, 4), 3, rgb);
    root->addChild(createTexturedQuad(tex, true));

    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(32, 32));
    SoWgpuRenderAction action(SbViewportRegion(32, 32));
    action.setRenderTarget(target);
    action.apply(root);

    ASSERT_TRUE(action.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED,
                "Texture quality 0.8 must be rejected with UNSUPPORTED");
    delete target;
    root->unref();
  }

  return true;
}

// 4. testTextureCoordinatesExplicitVsProcedural
static bool testTextureCoordinatesExplicitVsProcedural() {
  std::cout << "-> Test 4: UV coordinates (explicit vs procedural vs out-of-bounds)..." << std::endl;

  // Case 4A: Procedural / default texture coordinates (unsupported)
  {
    SoSeparator * root = new SoSeparator;
    root->ref();
    SoTexture2 * tex = new SoTexture2;
    unsigned char rgb[4 * 4 * 3];
    std::memset(rgb, 200, sizeof(rgb));
    tex->image.setValue(SbVec2s(4, 4), 3, rgb);

    // Quad without SoTextureCoordinate2 node
    root->addChild(createTexturedQuad(tex, /*addTexCoords=*/false));

    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(32, 32));
    SoWgpuRenderAction action(SbViewportRegion(32, 32));
    action.setRenderTarget(target);
    action.apply(root);

    ASSERT_TRUE(action.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED,
                "Procedural texture coordinates must be rejected with UNSUPPORTED");
    delete target;
    root->unref();
  }

  // Case 4B: Out-of-bounds textureCoordIndex (invalid scene)
  {
    SoSeparator * root = new SoSeparator;
    root->ref();
    SoTexture2 * tex = new SoTexture2;
    unsigned char rgb[4 * 4 * 3];
    std::memset(rgb, 200, sizeof(rgb));
    tex->image.setValue(SbVec2s(4, 4), 3, rgb);
    root->addChild(tex);

    SoTextureCoordinate2 * tc = new SoTextureCoordinate2;
    tc->point.setNum(4);
    tc->point.set1Value(0, SbVec2f(0.0f, 0.0f));
    tc->point.set1Value(1, SbVec2f(1.0f, 0.0f));
    tc->point.set1Value(2, SbVec2f(1.0f, 1.0f));
    tc->point.set1Value(3, SbVec2f(0.0f, 1.0f));
    root->addChild(tc);

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.setNum(4);
    coords->point.set1Value(0, SbVec3f(-1.0f, -1.0f, 0.0f));
    coords->point.set1Value(1, SbVec3f( 1.0f, -1.0f, 0.0f));
    coords->point.set1Value(2, SbVec3f( 1.0f,  1.0f, 0.0f));
    coords->point.set1Value(3, SbVec3f(-1.0f,  1.0f, 0.0f));
    root->addChild(coords);

    SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
    const int32_t cIndices[] = {0, 1, 2, 3, -1};
    ifs->coordIndex.setValues(0, 5, cIndices);

    // Out of bounds texture index: 9999
    const int32_t badTexIndices[] = {0, 1, 9999, 3, -1};
    ifs->textureCoordIndex.setValues(0, 5, badTexIndices);
    root->addChild(ifs);

    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(32, 32));
    SoWgpuRenderAction action(SbViewportRegion(32, 32));
    action.setRenderTarget(target);
    action.apply(root);

    ASSERT_TRUE(action.getLastStatus() == SoWgpuRenderAction::INVALID_SCENE,
                "Out-of-bounds textureCoordIndex must be rejected with INVALID_SCENE");
    delete target;
    root->unref();
  }

  return true;
}

// 5. testTextureTransformAndWrap
static bool testTextureTransformAndWrap() {
  std::cout << "-> Test 5: Texture transformation and wrap modes..." << std::endl;

  SoSeparator * root = new SoSeparator;
  root->ref();

  SoTexture2 * tex = new SoTexture2;
  tex->wrapS.setValue(SoTexture2::REPEAT);
  tex->wrapT.setValue(SoTexture2::CLAMP);
  unsigned char rgb[4 * 4 * 3];
  std::memset(rgb, 200, sizeof(rgb));
  tex->image.setValue(SbVec2s(4, 4), 3, rgb);
  root->addChild(tex);

  SoTexture2Transform * tt = new SoTexture2Transform;
  tt->scaleFactor.setValue(2.0f, 2.0f);
  tt->translation.setValue(0.5f, 0.25f);
  root->addChild(tt);

  root->addChild(createTexturedQuad(nullptr, true));

  // Recording verification
  SoWgpuRenderAction recordAction(SbViewportRegion(32, 32));
  recordAction.apply(root);
  SbString log = recordAction.getRecordingLog();
  ASSERT_TRUE(log.find("wrapS=REPEAT") != -1, "Sampler wrapS must be REPEAT");
  ASSERT_TRUE(log.find("wrapT=CLAMP") != -1, "Sampler wrapT must be CLAMP");
  ASSERT_TRUE(log.find("texMat:") != -1, "Recording log must contain texMat");

  // Offscreen render verification
  SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(32, 32));
  SoWgpuRenderAction action(SbViewportRegion(32, 32));
  action.setRenderTarget(target);
  action.apply(root);

  ASSERT_TRUE(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Render must succeed");

  delete target;
  root->unref();
  return true;
}

// 6. testMultipleDrawsDifferentTextures
static bool testMultipleDrawsDifferentTextures() {
  std::cout << "-> Test 6: Multiple draws with different textures in same frame..." << std::endl;

  SoSeparator * root = new SoSeparator;
  root->ref();

  // Subtree 1: Red texture
  SoSeparator * sep1 = new SoSeparator;
  SoTexture2 * tex1 = new SoTexture2;
  unsigned char redPixels[4 * 4 * 3];
  for (int i = 0; i < 16; ++i) {
    redPixels[i * 3 + 0] = 255;
    redPixels[i * 3 + 1] = 0;
    redPixels[i * 3 + 2] = 0;
  }
  tex1->image.setValue(SbVec2s(4, 4), 3, redPixels);
  sep1->addChild(createTexturedQuad(tex1, true));
  root->addChild(sep1);

  // Subtree 2: Blue texture
  SoSeparator * sep2 = new SoSeparator;
  SoTexture2 * tex2 = new SoTexture2;
  unsigned char bluePixels[4 * 4 * 3];
  for (int i = 0; i < 16; ++i) {
    bluePixels[i * 3 + 0] = 0;
    bluePixels[i * 3 + 1] = 0;
    bluePixels[i * 3 + 2] = 255;
  }
  tex2->image.setValue(SbVec2s(4, 4), 3, bluePixels);
  sep2->addChild(createTexturedQuad(tex2, true));
  root->addChild(sep2);

  // Recording verification
  SoWgpuRenderAction recordAction(SbViewportRegion(64, 64));
  recordAction.apply(root);
  SbString log = recordAction.getRecordingLog();
  ASSERT_TRUE(log.find("textures count: 2") != -1, "Plan must contain 2 textures");
  ASSERT_TRUE(log.find("texSlot=0") != -1 && log.find("texSlot=1") != -1,
              "Draws must bind different texture slots");

  // Offscreen render verification
  SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
  SoWgpuRenderAction action(SbViewportRegion(64, 64));
  action.setRenderTarget(target);
  action.apply(root);

  ASSERT_TRUE(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Render must succeed");

  delete target;
  root->unref();
  return true;
}

// 7. testTextureDeduplicationAndCache
static bool testTextureDeduplicationAndCache() {
  std::cout << "-> Test 7: Texture image deduplication with different samplers..." << std::endl;

  SoSeparator * root = new SoSeparator;
  root->ref();

  unsigned char identicalPixels[4 * 4 * 3];
  std::memset(identicalPixels, 120, sizeof(identicalPixels));

  // Subtree 1: Wrap REPEAT
  SoSeparator * sep1 = new SoSeparator;
  SoTexture2 * tex1 = new SoTexture2;
  tex1->wrapS.setValue(SoTexture2::REPEAT);
  tex1->wrapT.setValue(SoTexture2::REPEAT);
  tex1->image.setValue(SbVec2s(4, 4), 3, identicalPixels);
  sep1->addChild(createTexturedQuad(tex1, true));
  root->addChild(sep1);

  // Subtree 2: Wrap CLAMP with identical pixel bytes
  SoSeparator * sep2 = new SoSeparator;
  SoTexture2 * tex2 = new SoTexture2;
  tex2->wrapS.setValue(SoTexture2::CLAMP);
  tex2->wrapT.setValue(SoTexture2::CLAMP);
  tex2->image.setValue(SbVec2s(4, 4), 3, identicalPixels);
  sep2->addChild(createTexturedQuad(tex2, true));
  root->addChild(sep2);

  // Recording verification
  SoWgpuRenderAction recordAction(SbViewportRegion(64, 64));
  recordAction.apply(root);
  SbString log = recordAction.getRecordingLog();
  ASSERT_TRUE(log.find("textures count: 1") != -1,
              "Identical texture image must be deduplicated into 1 texture entry");
  ASSERT_TRUE(log.find("samplers count: 2") != -1,
              "Different wrap modes must produce 2 sampler entries");

  // Offscreen render verification
  SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
  SoWgpuRenderAction action(SbViewportRegion(64, 64));
  action.setRenderTarget(target);
  action.apply(root);

  ASSERT_TRUE(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Render must succeed");

  delete target;
  root->unref();
  return true;
}

// 8. testQualityZeroDisablesTexture
static bool testQualityZeroDisablesTexture() {
  std::cout << "-> Test 8: Texture quality 0.0 disables texture smoothly..." << std::endl;

  SoSeparator * root = new SoSeparator;
  root->ref();

  SoComplexity * comp = new SoComplexity;
  comp->textureQuality.setValue(0.0f);
  root->addChild(comp);

  SoTexture2 * tex = new SoTexture2;
  unsigned char redPixels[4 * 4 * 3];
  for (int i = 0; i < 16; ++i) {
    redPixels[i * 3 + 0] = 255;
    redPixels[i * 3 + 1] = 0;
    redPixels[i * 3 + 2] = 0;
  }
  tex->image.setValue(SbVec2s(4, 4), 3, redPixels);
  root->addChild(createTexturedQuad(tex, true));

  // Recording verification
  SoWgpuRenderAction recordAction(SbViewportRegion(32, 32));
  recordAction.apply(root);
  SbString log = recordAction.getRecordingLog();
  ASSERT_TRUE(log.find("hasTex=0") != -1,
              "Recording log must indicate hasTex=0 when quality is 0.0");

  // Offscreen render verification
  SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(32, 32));
  SoWgpuRenderAction action(SbViewportRegion(32, 32));
  action.setRenderTarget(target);
  action.apply(root);

  ASSERT_TRUE(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Render must succeed");

  delete target;
  root->unref();
  return true;
}

static bool testTexturedLineAndPointRejected() {
  for (int topology = 0; topology < 2; ++topology) {
    SoSeparator * root = new SoSeparator;
    root->ref();
    root->addChild(new SoPerspectiveCamera);

    SoTexture2 * texture = new SoTexture2;
    const unsigned char green[] = {0, 255, 0, 0, 255, 0,
                                   0, 255, 0, 0, 255, 0};
    texture->image.setValue(SbVec2s(2, 2), 3, green);
    root->addChild(texture);

    SoTextureCoordinate2 * uv = new SoTextureCoordinate2;
    uv->point.set1Value(0, SbVec2f(0.0f, 0.0f));
    uv->point.set1Value(1, SbVec2f(1.0f, 1.0f));
    root->addChild(uv);

    SoCoordinate3 * coords = new SoCoordinate3;
    coords->point.set1Value(0, SbVec3f(-0.5f, 0.0f, 0.0f));
    coords->point.set1Value(1, SbVec3f( 0.5f, 0.0f, 0.0f));
    root->addChild(coords);
    if (topology == 0) {
      SoLineSet * lines = new SoLineSet;
      lines->numVertices.set1Value(0, 2);
      root->addChild(lines);
    } else {
      SoPointSet * points = new SoPointSet;
      points->numPoints = 1;
      root->addChild(points);
    }

    SoWgpuRenderAction action(SbViewportRegion(32, 32));
    action.apply(root);
    ASSERT_TRUE(action.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED,
                "Textured line/point must fail explicitly");
    const char * expected = topology == 0 ? "Textured lines" : "Textured points";
    ASSERT_TRUE(std::strstr(action.getLastError().getString(), expected) != nullptr,
                "Textured line/point must report an actionable diagnostic");
    root->unref();
  }
  return true;
}

int main(int argc, char ** argv) {
  SoDB::init();
  SoWgpuRenderAction::initClass();

  std::cout << "========================================================\n";
  std::cout << "Running WgpuTextureTest (Subwave 3B: 2D Texture Pipeline)\n";
  std::cout << "========================================================\n";

  if (!testTextureFormats1to4Components()) return 1;
  if (!testModulateModelStrict()) return 1;
  if (!testUnsupportedRejections()) return 1;
  if (!testTexturedLineAndPointRejected()) return 1;
  if (!testTextureCoordinatesExplicitVsProcedural()) return 1;
  if (!testTextureTransformAndWrap()) return 1;
  if (!testMultipleDrawsDifferentTextures()) return 1;
  if (!testTextureDeduplicationAndCache()) return 1;
  if (!testQualityZeroDisablesTexture()) return 1;

  std::cout << "\nALL WgpuTextureTest checks PASSED successfully!\n";
  return 0;
}
