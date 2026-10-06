#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoFont.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTexture2Transform.h>
#include <Inventor/nodes/SoTransform.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
const SbVec2i32 initialSize(160, 120);
const SbColor background(7.0f / 255, 11.0f / 255, 19.0f / 255);
bool check(bool condition, const std::string & message) {
  if (!condition) std::cerr << "CoinRenderScreenContentTest: " << message << '\n';
  return condition;
}
class CaptureCpuBackend : public CoinRenderCpuReferenceBackend {
public:
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target) override {
    ++submissions; vertices = frame.vertices.size(); draws = frame.draws.size();
    return CoinRenderCpuReferenceBackend::submit(frame, target);
  }
  size_t submissions = 0, vertices = 0, draws = 0;
};

struct Scene {
  explicit Scene(bool withText) {
    root = new SoSeparator; root->ref();
    camera = new SoOrthographicCamera;
    camera->position.setValue(0, 0, 10); camera->nearDistance = 1; camera->farDistance = 40;
    static_cast<SoOrthographicCamera *>(camera)->height = 120;
    root->addChild(camera);
    auto * light = new SoLightModel; light->model = SoLightModel::BASE_COLOR; root->addChild(light);
    screen = new SoSwitch; screen->whichChild = SO_SWITCH_ALL; root->addChild(screen);
    auto * content = new SoSeparator; screen->addChild(content);
    transform = new SoTransform; content->addChild(transform);
    depth = new SoDepthBuffer; depth->test = TRUE; depth->write = TRUE;
    depth->function = SoDepthBuffer::LEQUAL; content->addChild(depth);
    quality = new SoComplexity; quality->textureQuality = .5f; content->addChild(quality);
    inheritedTexture = new SoTexture2;
    const unsigned char cyan[] = {20, 230, 210, 255};
    if (withText) inheritedTexture->image.setValue(SbVec2s(1, 1), 4, cyan);
    else inheritedTexture->image.setValue(SbVec2s(0, 0), 0, nullptr);
    inheritedTexture->model = SoTexture2::REPLACE; content->addChild(inheritedTexture);
    textureTransform = new SoTexture2Transform; content->addChild(textureTransform);
    material = new SoMaterial; material->diffuseColor.setValue(.85f, .18f, .42f);
    content->addChild(material);
    if (withText) {
      font = new SoFont; font->name = "defaultFont"; font->size = 20; content->addChild(font);
      text = new SoText2;
      const char * lines[] = {"Coin", "Render"};
      text->string.setValues(0, 2, lines); content->addChild(text);
    } else {
      image = new SoImage; image->horAlignment = SoImage::CENTER; image->vertAlignment = SoImage::HALF;
      content->addChild(image);
    }
  }
  ~Scene() { root->unref(); }
  Scene(const Scene &) = delete;
  Scene & operator=(const Scene &) = delete;
  void perspective() {
    auto * replacement = new SoPerspectiveCamera;
    replacement->position.setValue(0, 0, 10); replacement->nearDistance = 1;
    replacement->farDistance = 40; replacement->heightAngle = .75f;
    root->replaceChild(camera, replacement); camera = replacement;
  }
  SoSeparator * root = nullptr;
  SoCamera * camera = nullptr;
  SoSwitch * screen = nullptr;
  SoTransform * transform = nullptr;
  SoDepthBuffer * depth = nullptr;
  SoComplexity * quality = nullptr;
  SoTexture2 * inheritedTexture = nullptr;
  SoTexture2Transform * textureTransform = nullptr;
  SoMaterial * material = nullptr;
  SoFont * font = nullptr;
  SoText2 * text = nullptr;
  SoImage * image = nullptr;
};

// Unequal rows, columns and channels expose flipped rows and RGBA/L/LA mistakes.
void setImage(SoImage * image, int components, int revision = 0) {
  const int width = 7, height = 5;
  std::vector<unsigned char> pixels(size_t(width * height * components));
  for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
    const size_t offset = size_t(y * width + x) * components;
    const unsigned char luminance = static_cast<unsigned char>(35 + 18 * x + 13 * y + 3 * revision);
    if (components <= 2) pixels[offset] = luminance;
    else {
      pixels[offset] = luminance;
      pixels[offset + 1] = static_cast<unsigned char>(210 - 19 * x + 5 * revision);
      pixels[offset + 2] = static_cast<unsigned char>(30 + 31 * y + 7 * revision);
    }
    if (components == 2 || components == 4)
      pixels[offset + components - 1] = static_cast<unsigned char>(40 + 17 * x + 12 * y);
  }
  image->image.setValue(SbVec2s(width, height), components, pixels.data());
}
SoSeparator * quad(float x0, float y0, float x1, float y1, float z, const SbColor & color) {
  auto * group = new SoSeparator;
  auto * material = new SoMaterial; material->diffuseColor.setValue(color); group->addChild(material);
  auto * coordinates = new SoCoordinate3;
  const SbVec3f points[] = {{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}};
  coordinates->point.setValues(0, 4, points); group->addChild(coordinates);
  auto * faces = new SoIndexedFaceSet;
  const int32_t indices[] = {0, 1, 2, 3, -1};
  faces->coordIndex.setValues(0, 5, indices); group->addChild(faces);
  return group;
}

// Expected pixels come from Coin/OpenGL, not a reconstruction of its glyph
// layout, image resizer, or the new screen-content capture implementation.
bool glPixels(SoOffscreenRenderer & gl, SoNode * root, const SbVec2i32 & size,
              std::vector<uint8_t> & rgba) {
  if (!check(gl.render(root) && gl.getBuffer(), "required Coin/OpenGL reference unavailable")) return false;
  const uint8_t * source = gl.getBuffer();
  rgba.resize(size_t(size[0]) * size[1] * 4);
  for (int y = 0; y < size[1]; ++y) for (int x = 0; x < size[0]; ++x) {
    const size_t a = (size_t(y) * size[0] + x) * 4;
    const size_t b = (size_t(size[1] - y - 1) * size[0] + x) * 3;
    for (int c = 0; c < 3; ++c) rgba[a + c] = source[b + c];
    rgba[a + 3] = 255;
  }
  return true;
}

// Check the union of reference/observed content effects. A mostly empty
// framebuffer must not let omitted text or misplaced images pass the gate.
bool compare(const std::vector<uint8_t> & actual, const std::vector<uint8_t> & reference,
             const std::vector<uint8_t> & bare, const std::string & label, bool expectVisible) {
  if (!check(actual.size() == reference.size() && actual.size() == bare.size() && !actual.empty(),
             label + ": complete RGBA readback required")) return false;
  size_t referencePixels = 0, active = 0, over3 = 0;
  uint64_t error = 0; int maximum = 0;
  size_t printed = 0;
  for (size_t pixel = 0; pixel < actual.size(); pixel += 4) {
    bool changedReference = false, changedActual = false;
    for (size_t c = 0; c < 3; ++c) {
      changedReference |= std::abs(int(reference[pixel + c]) - int(bare[pixel + c])) > 2;
      changedActual |= std::abs(int(actual[pixel + c]) - int(bare[pixel + c])) > 2;
    }
    referencePixels += changedReference;
    if (!changedReference && !changedActual) continue;
    ++active; bool bad = false;
    for (size_t c = 0; c < 3; ++c) {
      const int delta = std::abs(int(actual[pixel + c]) - int(reference[pixel + c]));
      error += delta; maximum = std::max(maximum, delta); bad |= delta > 3;
    }
    over3 += bad;
    if (bad && printed++ < 3)
      std::cout << label << " sample_pixel=" << pixel / 4 << " actual_rgb="
                << int(actual[pixel]) << ',' << int(actual[pixel + 1]) << ',' << int(actual[pixel + 2])
                << " reference_rgb=" << int(reference[pixel]) << ',' << int(reference[pixel + 1])
                << ',' << int(reference[pixel + 2]) << '\n';
  }
  const double mae = active ? double(error) / (active * 3) : 0;
  std::cout << label << " effect_pixels=" << referencePixels << " checked_pixels=" << active
            << " rgb_roi_mae=" << mae << " max=" << maximum << " pixels_over3=" << over3 << '\n';
  return check((!expectVisible || referencePixels >= 8) && mae <= 1.5 &&
               over3 <= std::max<size_t>(4, active / 50),
               label + ": differs from Coin/OpenGL or the reference is vacuous");
}

struct Harness {
  explicit Harness(bool useGpu) : gpu(useGpu), size(initialSize), cpuAction(SbViewportRegion(160, 120)),
      gpuAction(SbViewportRegion(160, 120)), gl(SbViewportRegion(160, 120)) {
    cpu.reset(CoinRenderTarget::createOffscreen(size));
    if (cpu) {
      observer = new CaptureCpuBackend; cpu->getPimpl()->backend.reset(observer);
      cpu->getPimpl()->depthBuffer.assign(size_t(size[0]) * size[1], 1.0f);
      cpuAction.setRenderTarget(cpu.get());
    }
    if (gpu) {
      native.reset(CoinRenderTarget::createOffscreen(size));
      if (native) gpuAction.setRenderTarget(native.get());
    }
    cpuAction.setBackgroundColor(SbColor4f(background[0], background[1], background[2], 1));
    gpuAction.setBackgroundColor(SbColor4f(background[0], background[1], background[2], 1));
    cpuAction.setTransparencyType(CoinRenderAction::BLEND);
    gpuAction.setTransparencyType(CoinRenderAction::BLEND);
    gl.setComponents(SoOffscreenRenderer::RGB); gl.setBackgroundColor(background);
    gl.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
  }
  ~Harness() { cpuAction.setRenderTarget(nullptr); gpuAction.setRenderTarget(nullptr); }
  bool resize(const SbVec2i32 & changed) {
    if (!check(cpu && cpu->resize(changed) && (!gpu || (native && native->resize(changed))),
               "target resize failed")) return false;
    size = changed;
    const SbViewportRegion viewport(static_cast<short>(size[0]), static_cast<short>(size[1]));
    cpuAction.setViewportRegion(viewport); gpuAction.setViewportRegion(viewport); gl.setViewportRegion(viewport);
    return true;
  }
  void transparency(int mode) {
    cpuAction.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(mode));
    gpuAction.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(mode));
    gl.getGLRenderAction()->setTransparencyType(static_cast<SoGLRenderAction::TransparencyType>(mode));
  }
  bool reject(Scene & scene, const std::string & label, CoinRenderAction::Status expected) {
    const auto beforeCpu = lastCpu, beforeNative = lastNative;
    const size_t before = observer->submissions;
    cpuAction.apply(scene.root);
    std::vector<uint8_t> pixels; cpu->readbackRGBA(pixels);
    bool ok = check(cpuAction.getLastStatus() == expected &&
                    cpuAction.getLastError().getLength() != 0 &&
                    observer->submissions == before && pixels == beforeCpu,
                    label + ": CPU rejection must be explicit and preserve the previous frame");
    if (gpu) {
      gpuAction.apply(scene.root); native->readbackRGBA(pixels);
      ok &= check(gpuAction.getLastStatus() == expected &&
                  gpuAction.getLastError().getLength() != 0 && pixels == beforeNative,
                  label + ": GPU rejection must be explicit and preserve the previous frame");
    }
    return ok;
  }
  bool render(Scene & scene, const std::string & label, bool expectVisible = true,
              bool prunedReference = false) {
    if (!check(cpu && observer && (!gpu || native), "screen-content target creation failed")) return false;
    const size_t before = observer->submissions;
    cpuAction.apply(scene.root);
    if (!check(cpuAction.getLastStatus() == CoinRenderAction::SUCCESS,
               label + ": CPU capture failed: " + cpuAction.getLastError().getString())) return false;
    std::vector<uint8_t> cpuPixels; cpu->readbackRGBA(cpuPixels);
    if (!check(observer->submissions == before + 1 && cpuPixels.size() == size_t(size[0]) * size[1] * 4,
               label + ": capture/CPU submission was not executed")) return false;
    bool visible = false;
    for (size_t i = 0; i < cpuPixels.size(); i += 4)
      visible |= std::abs(int(cpuPixels[i]) - 7) > 2 || std::abs(int(cpuPixels[i + 1]) - 11) > 2 ||
                 std::abs(int(cpuPixels[i + 2]) - 19) > 2;
    if (!check(!expectVisible || (observer->draws && observer->vertices && visible),
               label + ": active node produced no captured/rendered content")) return false;
    if (gpu) {
      gpuAction.apply(scene.root);
      if (!check(gpuAction.getLastStatus() == CoinRenderAction::SUCCESS,
                 label + ": GPU capture failed: " + gpuAction.getLastError().getString())) return false;
      std::vector<uint8_t> pixels; native->readbackRGBA(pixels);
      // Callback mutations are visible in the reference too. A fresh copy
      // removes only screen content; the original graph is never toggled.
      std::vector<uint8_t> bare, reference;
      auto * without = static_cast<SoSeparator *>(scene.root->copy(TRUE)); without->ref();
      auto * hidden = static_cast<SoSwitch *>(without->getChild(scene.root->findChild(scene.screen)));
      hidden->whichChild = SO_SWITCH_NONE;
      const bool rendered = glPixels(gl, without, size, bare); without->unref();
      if (!rendered) return false;
      if (prunedReference) reference = bare;
      else if (!glPixels(gl, scene.root, size, reference)) return false;
      if (!compare(cpuPixels, reference, bare, label + "/CPU-CoinGL", expectVisible)) return false;
      if (!compare(pixels, reference, bare, label + "/GPU-CoinGL", expectVisible)) return false;
      lastReference = reference; lastNative = pixels;
    }
    lastCpu = cpuPixels; return true;
  }
  bool gpu; SbVec2i32 size;
  std::unique_ptr<CoinRenderTarget> cpu, native;
  CaptureCpuBackend * observer = nullptr;
  CoinRenderAction cpuAction, gpuAction;
  SoOffscreenRenderer gl;
  std::vector<uint8_t> lastReference, lastNative, lastCpu;
};

bool images(bool gpu) {
  Scene scene(false); Harness harness(gpu);
  for (int components = 1; components <= 4; ++components) {
    setImage(scene.image, components);
    if (!harness.render(scene, "image/components-" + std::to_string(components))) return false;
  }
  const auto unchangedCpu = harness.lastCpu, unchangedNative = harness.lastNative;
  if (!harness.render(scene, "image/repeated") ||
      !check(harness.lastCpu == unchangedCpu && (!gpu || harness.lastNative == unchangedNative),
             "unchanged image drifted across repeated apply")) return false;
  scene.image->width = 21; scene.image->height = 15;
  for (int horizontal : {SoImage::LEFT, SoImage::CENTER, SoImage::RIGHT})
    for (int vertical : {SoImage::BOTTOM, SoImage::HALF, SoImage::TOP}) {
      scene.image->horAlignment = horizontal; scene.image->vertAlignment = vertical;
      if (!harness.render(scene, "image/alignment-" + std::to_string(horizontal) + "-" + std::to_string(vertical))) return false;
    }
  scene.image->horAlignment = SoImage::CENTER; scene.image->vertAlignment = SoImage::HALF;
  scene.image->width = -1; scene.image->height = 13;
  if (!harness.render(scene, "image/height-only")) return false;
  scene.image->width = 19; scene.image->height = -1;
  if (!harness.render(scene, "image/width-only")) return false;
  scene.image->width = 21; scene.image->height = 15;
  if (!harness.render(scene, "image/before-byte-mutation")) return false;
  const auto before = harness.lastCpu; setImage(scene.image, 4, 2);
  if (!harness.render(scene, "image/byte-mutation") ||
      !check(harness.lastCpu != before, "image byte mutation did not change current output")) return false;
  scene.material->transparency = .6f; scene.quality->textureQuality = 0;
  scene.textureTransform->translation.setValue(.7f, -.4f); scene.textureTransform->scaleFactor.setValue(3, .2f);
  if (!harness.render(scene, "image/inherited-state")) return false;
  scene.transform->scaleFactor.setValue(2, .6f, 1); scene.transform->rotation.setValue(SbVec3f(0, 0, 1), .45f);
  scene.transform->translation.setValue(7, -5, 0); scene.camera->position.setValue(4, 2, 12);
  if (!harness.render(scene, "image/model-camera")) return false;
  if (!harness.resize(SbVec2i32(192, 144)) || !harness.render(scene, "image/target-resize")) return false;
  scene.transform->translation.setValue(80, 52, 0);
  scene.image->horAlignment = SoImage::LEFT; scene.image->vertAlignment = SoImage::BOTTOM;
  if (!harness.render(scene, "image/viewport-clipping", false)) return false;
  scene.transform->translation.setValue(0, 0, 0); scene.perspective();
  if (!harness.render(scene, "image/perspective")) return false;
  scene.transform->translation.setValue(0, 0, 12);
  if (!harness.render(scene, "image/behind-camera", false)) return false;
  scene.transform->translation.setValue(0, 0, 0);
  scene.image->image.setValue(SbVec2s(0, 0), 0, nullptr);
  if (!harness.render(scene, "image/empty", false)) return false;
  setImage(scene.image, 3); return harness.render(scene, "image/restore");
}
bool text(bool gpu, const char * trueTypeFont) {
  Scene scene(true); Harness harness(gpu);
  std::vector<uint8_t> defaultReference;
  for (int justification : {SoText2::LEFT, SoText2::CENTER, SoText2::RIGHT}) {
    scene.text->justification = justification;
    if (!harness.render(scene, "text/default-justification-" + std::to_string(justification))) return false;
    if (justification == SoText2::LEFT) defaultReference = harness.lastReference;
  }
  scene.text->justification = SoText2::LEFT;
  for (float fraction : {.25f, .75f}) {
    scene.transform->translation.setValue(fraction, fraction, 0);
    if (!harness.render(scene, "text/default-fraction-" + std::to_string(fraction))) return false;
  }
  scene.transform->translation.setValue(0, 0, 0); scene.font->name = trueTypeFont;
  if (!harness.render(scene, "text/truetype-font")) return false;
  if (gpu && !check(harness.lastReference != defaultReference,
                    "TrueType CoinGL reference fell back to DEFAULT; configure --font")) return false;
  const char * utf8[] = {"Caf\xc3\xa9 \xce\xa9", "Ol\xc3\xa1 Render"};
  scene.text->string.setValues(0, 2, utf8);
  if (!harness.render(scene, "text/truetype-utf8")) return false;
  const auto unchangedCpu = harness.lastCpu, unchangedNative = harness.lastNative;
  if (!harness.render(scene, "text/repeated") ||
      !check(harness.lastCpu == unchangedCpu && (!gpu || harness.lastNative == unchangedNative),
             "unchanged text drifted across repeated apply")) return false;
  for (int justification : {SoText2::CENTER, SoText2::RIGHT}) {
    scene.text->justification = justification; scene.text->spacing = 1.7f;
    if (!harness.render(scene, "text/utf8-spacing-justification-" + std::to_string(justification))) return false;
  }
  const auto before = harness.lastCpu;
  scene.font->size = 27; scene.text->string.setValue("Changed \xc3\xa9");
  if (!harness.render(scene, "text/font-string-mutation") ||
      !check(harness.lastCpu != before, "text/font mutation did not change current output")) return false;
  scene.material->transparency = .55f; scene.material->diffuseColor.setValue(.15f, .8f, .35f);
  scene.quality->textureQuality = 0; scene.textureTransform->translation.setValue(.6f, .8f);
  if (!harness.render(scene, "text/material-alpha-texture-state")) return false;
  scene.transform->translation.setValue(12, 8, 0); scene.transform->scaleFactor.setValue(2, .5f, 1);
  scene.transform->rotation.setValue(SbVec3f(0, 0, 1), .35f); scene.camera->position.setValue(3, 2, 12);
  if (!harness.render(scene, "text/model-camera")) return false;
  if (!harness.resize(SbVec2i32(192, 144)) || !harness.render(scene, "text/target-resize")) return false;
  scene.text->justification = SoText2::LEFT; scene.transform->translation.setValue(85, 4, 0);
  if (!harness.render(scene, "text/viewport-clipping", false)) return false;
  scene.transform->translation.setValue(0, 0, 0); scene.perspective();
  if (!harness.render(scene, "text/perspective")) return false;
  scene.transform->translation.setValue(0, 0, 12);
  if (!harness.render(scene, "text/behind-camera", false)) return false;
  scene.transform->translation.setValue(0, 0, 0); scene.text->string.setNum(0);
  if (!harness.render(scene, "text/empty", false)) return false;
  scene.font->name = "defaultFont"; scene.font->size = 20; scene.text->string.setValue("Restored");
  return harness.render(scene, "text/restore");
}
bool depth(bool gpu, bool withText, const char * fontName) {
  Scene scene(withText); Harness harness(gpu);
  if (withText) {
    scene.font->name = fontName; scene.text->string.setValue("DEPTH"); scene.text->justification = SoText2::CENTER;
  } else { setImage(scene.image, 4); scene.image->width = 21; scene.image->height = 15; }
  scene.root->insertChild(quad(-80, -60, 80, 60, -.4f, SbColor(.1f, .15f, .65f)), 2);
  scene.root->insertChild(quad(-80, -60, -3, 60, .5f, SbColor(.7f, .1f, .1f)), 3);
  // The later third draw reveals depth writes, beyond simple foreground occlusion.
  scene.root->addChild(quad(4, -10, 44, 10, -.2f, SbColor(.1f, .7f, .15f)));
  for (bool write : {true, false}) {
    scene.depth->write = write ? TRUE : FALSE;
    if (!harness.render(scene, std::string(withText ? "text" : "image") +
                        "/depth-write-" + (write ? "on/" : "off/") + fontName)) return false;
  }
  scene.depth->test = FALSE; scene.depth->write = TRUE;
  if (!harness.render(scene, std::string(withText ? "text" : "image") +
                      "/depth-test-off-write-on/" + fontName)) return false;
  return true;
}

bool policies(bool gpu, bool withText, const char * fontName) {
  Scene scene(withText); Harness harness(gpu);
  scene.material->transparency = .55f;
  if (withText) {
    scene.font->name = fontName; scene.text->string.setValue("AV jg");
  } else {
    scene.image->width = 19; scene.image->height = 13;
  }
  for (int components : (withText ? std::vector<int>{0} : std::vector<int>{1, 4})) {
    if (!withText) setImage(scene.image, components);
    for (int mode : {SoGLRenderAction::SCREEN_DOOR, SoGLRenderAction::NONE,
                     SoGLRenderAction::SORTED_OBJECT_BLEND}) {
      harness.transparency(mode);
      if (!harness.render(scene, std::string(withText ? "text" : "image") +
                          "/policy-" + std::to_string(mode) + "-" +
                          std::to_string(components) + "/" + fontName)) return false;
    }
  }
  return true;
}

bool effects(bool gpu, bool withText, const char * fontName) {
  Scene scene(withText); Harness harness(gpu);
  if (withText) {
    scene.font->name = fontName; scene.text->string.setValue("CLIP");
    scene.text->justification = SoText2::CENTER;
  } else {
    setImage(scene.image, 3); scene.image->width = 21; scene.image->height = 15;
  }
  scene.transform->translation.setValue(2, 0, 0);
  auto * content = static_cast<SoSeparator *>(scene.screen->getChild(0));
  auto * clip = new SoClipPlane;
  // The bitmap rectangle crosses this plane but its raster origin is inside.
  // CoinGL remains the authority for raster-origin versus per-pixel clipping.
  clip->plane.setValue(SbPlane(SbVec3f(1, 0, 0), 0)); content->insertChild(clip, 0);
  if (!harness.render(scene, std::string(withText ? "text" : "image") +
                      "/clip-crossing/" + fontName)) return false;
  clip->plane.setValue(SbPlane(SbVec3f(-1, 0, 0), 100));
  if (!harness.render(scene, std::string(withText ? "text" : "image") +
                      "/clip-rejected-origin/" + fontName, false)) return false;
  clip->on = FALSE;
  if (!harness.render(scene, std::string(withText ? "text" : "image") +
                      "/before-fog/" + fontName)) return false;
  const auto referenceBefore = harness.lastReference;
  auto * environment = new SoEnvironment;
  environment->fogType = SoEnvironment::FOG;
  environment->fogColor.setValue(.3f, .5f, .7f); environment->fogVisibility = 18;
  scene.root->insertChild(environment, 2);
  if (!harness.render(scene, std::string(withText ? "text" : "image") +
                      "/fog/" + fontName)) return false;
  if (gpu) std::cout << "CoinRenderScreenContentTest fog_reference_changed="
                    << (harness.lastReference != referenceBefore) << " font=" << fontName << '\n';
  return true;
}

bool inheritedImage(bool gpu) {
  Scene scene(false); Harness harness(gpu);
  setImage(scene.image, 4);
  if (!harness.render(scene, "image/inherited-negative-anchor")) return false;
  const unsigned char cyan[] = {20, 230, 210, 255};
  scene.inheritedTexture->image.setValue(SbVec2s(1, 1), 4, cyan);
  if (!harness.reject(scene, "image/active-inherited-texture", CoinRenderAction::UNSUPPORTED)) return false;
  // An empty screen node is inert even if the preceding texture is active.
  scene.image->image.setValue(SbVec2s(0, 0), 0, nullptr);
  if (!harness.render(scene, "image/empty-with-active-texture", false)) return false;
  scene.inheritedTexture->image.setValue(SbVec2s(0, 0), 0, nullptr);
  setImage(scene.image, 4);
  if (!harness.render(scene, "image/inherited-negative-repair")) return false;
  scene.material->transparency = .55f;
  harness.transparency(SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
  return harness.reject(scene, "image/sorted-triangle-profile", CoinRenderAction::UNSUPPORTED);
}

struct ScreenHook { int mode = 0; };
SoCallbackAction::Response screenHook(void * data, SoCallbackAction *, const SoNode * node) {
  const int mode = static_cast<ScreenHook *>(data)->mode;
  if (mode == 1) return SoCallbackAction::PRUNE;
  if (mode == 2) {
    if (node->isOfType(SoImage::getClassTypeId()))
      setImage(static_cast<SoImage *>(const_cast<SoNode *>(node)), 3, 3);
    else static_cast<SoText2 *>(const_cast<SoNode *>(node))->string.setValue("Callback \xc3\xa9");
  }
  return SoCallbackAction::CONTINUE;
}
bool callbacks(bool gpu, bool withText) {
  Scene scene(withText); ScreenHook hook; Harness harness(gpu);
  if (!withText) setImage(scene.image, 1);
  const SoType type = withText ? SoText2::getClassTypeId() : SoImage::getClassTypeId();
  harness.cpuAction.addPreCallback(type, screenHook, &hook);
  harness.gpuAction.addPreCallback(type, screenHook, &hook);
  // SoCallbackAction exposes registration, not removal. Hook outlives both
  // actions; each callback fixture owns fresh actions and releases them here.
  const std::string label = withText ? "text/callback-" : "image/callback-";
  if (!harness.render(scene, label + "anchor")) return false;
  const auto before = harness.lastCpu;
  hook.mode = 1;
  if (!harness.render(scene, label + "prune", false, true) ||
      !check(harness.observer->draws == 0 && harness.observer->vertices == 0,
             label + "PRUNE must prevent all screen geometry capture")) return false;
  hook.mode = 2;
  if (!harness.render(scene, label + "continue-mutation") ||
      !check(harness.lastCpu != before, label + "CONTINUE mutation was captured too early")) return false;
  hook.mode = 1;
  if (!harness.render(scene, label + "prune-restored", false, true) ||
      !check(harness.observer->draws == 0, label + "restored PRUNE did not remove the mutated content")) return false;
  hook.mode = 0;
  return harness.render(scene, label + "continue-restored");
}

bool limits(bool gpu, const char * fontName) {
  Scene scene(true); Harness harness(gpu);
  scene.font->name = fontName; scene.text->string.setValue("Limits");
  if (!harness.render(scene, "text/limit-anchor")) return false;
  const auto beforeCpu = harness.lastCpu, beforeNative = harness.lastNative;
  scene.font->size = 1.0e9f;
  if (!harness.reject(scene, "text/oversize-font", CoinRenderAction::UNSUPPORTED)) return false;
  scene.font->size = 20;
  scene.text->string.setValue(std::string(1024 * 1024 + 1, 'W').c_str());
  if (!harness.reject(scene, "text/oversize-input", CoinRenderAction::UNSUPPORTED)) return false;
  scene.text->string.setValue("Limits");
  scene.font->size = std::numeric_limits<float>::quiet_NaN();
  if (!harness.reject(scene, "text/nonfinite-font", CoinRenderAction::INVALID_SCENE)) return false;
  scene.font->size = 20;
  if (!harness.render(scene, "text/limit-repair") ||
      !check(harness.lastCpu == beforeCpu && (!gpu || harness.lastNative == beforeNative),
             "repair after rejected text must restore the exact previous image")) return false;
  scene.material->transparency = .55f;
  harness.transparency(SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
  if (!harness.reject(scene, "text/sorted-triangle-profile", CoinRenderAction::UNSUPPORTED)) return false;
  harness.transparency(SoGLRenderAction::BLEND); scene.material->transparency = 0;
  scene.screen->ref(); scene.root->removeChild(scene.screen);
  auto * shadow = new SoShadowGroup;
  shadow->isActive = TRUE; shadow->addChild(scene.screen); scene.screen->unref();
  scene.root->addChild(shadow);
  return harness.reject(scene, "text/active-shadow-profile", CoinRenderAction::UNSUPPORTED);
}
}
int main(int argc, char ** argv) {
  bool gpu = false; const char * font = "DejaVu Sans";
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--gpu") gpu = true;
    else if (argument == "--capture") gpu = false;
    else if (argument == "--font" && i + 1 < argc) font = argv[++i];
    else { std::cerr << "Usage: CoinRenderScreenContentTest --capture|--gpu [--font NAME]\n"; return 2; }
  }
  SoDB::init(); CoinRenderAction::initClass();
  if (gpu && !CoinRenderAction::isGpuBackendAvailable()) {
    std::cerr << "CoinRenderScreenContentTest requires the selected GPU backend\n"; return 77;
  }
  if (!images(gpu) || !text(gpu, font) || !depth(gpu, false, "image") ||
      !depth(gpu, true, "defaultFont") || !depth(gpu, true, font) ||
      !policies(gpu, false, "image") || !policies(gpu, true, "defaultFont") ||
      !policies(gpu, true, font) || !effects(gpu, false, "image") ||
      !effects(gpu, true, "defaultFont") || !effects(gpu, true, font) ||
      !inheritedImage(gpu) || !callbacks(gpu, false) || !callbacks(gpu, true) ||
      !limits(gpu, font)) return 1;
  std::cout << "CoinRenderScreenContentTest " << (gpu ? "GPU/CoinGL" : "capture/CPU") << " passed\n";
  return 0;
}
