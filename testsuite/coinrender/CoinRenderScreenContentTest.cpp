#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoFont.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureCoordinateEnvironment.h>
#include <Inventor/nodes/SoTextureCoordinateNormalMap.h>
#include <Inventor/nodes/SoTextureCoordinateObject.h>
#include <Inventor/nodes/SoTextureCoordinatePlane.h>
#include <Inventor/nodes/SoTextureCoordinateReflectionMap.h>
#include <Inventor/nodes/SoTexture3.h>
#include <Inventor/nodes/SoTextureCombine.h>
#include <Inventor/nodes/SoTexture2Transform.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/nodes/SoTextureUnit.h>
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

// SoNode::copy(TRUE) preserves the child path, even after a test moves the
// screen switch beneath SoShadowGroup. Locate it by that path in the copied
// graph; never use getChild(-1) or hide a different switch silently.
int hideCopiedScreen(SoGroup * original, SoGroup * copied, SoSwitch * screen) {
  if(original->getNumChildren()!=copied->getNumChildren()) return -1;
  int found=0;
  for(int index=0;index<original->getNumChildren();++index) {
    SoNode * source=original->getChild(index);
    SoNode * target=copied->getChild(index);
    if(source==screen) {
      if(!target->isOfType(SoSwitch::getClassTypeId())) return -1;
      static_cast<SoSwitch *>(target)->whichChild=SO_SWITCH_NONE;
      ++found;
    } else if(source->isOfType(SoGroup::getClassTypeId())) {
      if(!target->isOfType(SoGroup::getClassTypeId())) return -1;
      const int nested=hideCopiedScreen(
        static_cast<SoGroup *>(source),static_cast<SoGroup *>(target),screen);
      if(nested<0) return -1;
      found+=nested;
    }
  }
  return found;
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
    std::vector<float> beforeCpuDepth, beforeNativeDepth;
    cpu->readbackDepth(beforeCpuDepth);
    const uint64_t beforeCpuSerial=cpu->getLastSubmissionSerial();
    uint64_t beforeNativeSerial=0;
    if (gpu) { native->readbackDepth(beforeNativeDepth); beforeNativeSerial=native->getLastSubmissionSerial(); }
    cpuAction.apply(scene.root);
    std::vector<uint8_t> pixels; std::vector<float> depth;
    cpu->readbackRGBA(pixels); cpu->readbackDepth(depth);
    bool ok = check(cpuAction.getLastStatus() == expected &&
                    cpuAction.getLastError().getLength() != 0 &&
                    observer->submissions == before && pixels == beforeCpu &&
                    depth == beforeCpuDepth && cpu->getLastSubmissionSerial() == beforeCpuSerial,
                    label + ": CPU rejection must preserve RGBA, depth and submission serial");
    if (gpu) {
      gpuAction.apply(scene.root); native->readbackRGBA(pixels); native->readbackDepth(depth);
      ok &= check(gpuAction.getLastStatus() == expected &&
                  gpuAction.getLastError().getLength() != 0 && pixels == beforeNative &&
                  depth == beforeNativeDepth && native->getLastSubmissionSerial() == beforeNativeSerial,
                  label + ": GPU rejection must preserve RGBA, depth and submission serial");
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
      if (!reportedGpu) {
        CoinRenderCapabilities caps{};
        const bool queried=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
                                                            &caps,sizeof(caps))==0;
        if (!check(queried && caps.gpu_available,
                   label + ": GPU submission requires a real GPU adapter")) return false;
        std::cout << "CoinRenderScreenContentTest runtime backend=" << caps.backend
                  << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
                  << " adapter=" << caps.adapter_name << " gpu_available=" << caps.gpu_available
                  << '\n';
        reportedGpu=true;
      }
      std::vector<uint8_t> pixels; native->readbackRGBA(pixels);
      if (!glReference) { lastNative = pixels; lastCpu = cpuPixels; return true; }
      // Callback mutations are visible in the reference too. A fresh copy
      // removes only screen content; the original graph is never toggled.
      std::vector<uint8_t> bare, reference;
      auto * without = static_cast<SoSeparator *>(scene.root->copy(TRUE)); without->ref();
      const int hidden=hideCopiedScreen(scene.root,without,scene.screen);
      if(!check(hidden==1,label+": copied scene must contain exactly one screen switch")) {
        without->unref();return false;
      }
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
  bool glReference = true;
  bool reportedGpu = false;
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

bool inheritedImage(bool gpu, bool portableOnly = false) {
  Scene scene(false); Harness harness(gpu);
  harness.glReference = !portableOnly;
  setImage(scene.image, 4);
  if (!harness.render(scene, "image/inherited-negative-anchor")) return false;
  const auto ordinaryCpu=harness.lastCpu;
  const auto ordinaryNative=harness.lastNative;
  const unsigned char cyan[] = {20, 230, 210, 255};
  scene.inheritedTexture->image.setValue(SbVec2s(1, 1), 4, cyan);
  if (!harness.render(scene, "image/active-inherited-texture") ||
      !check(harness.lastCpu!=ordinaryCpu && (!gpu || harness.lastNative!=ordinaryNative),
             "inherited 1x1 texture must visibly change SoImage pixels")) return false;
  // Exercise actual texel selection and payload conversion. The 1x1 case
  // alone cannot distinguish inherited sampling from a constant-color path.
  const unsigned char texels2d[] = {
    23, 211, 37, 255, 227, 31, 83, 128,
    57, 73, 239, 0, 181, 149, 19, 255
  };
  // Coin disables SoTexture2 at quality zero; .1 is the active nearest band.
  scene.quality->textureQuality = .1f;
  scene.inheritedTexture->wrapS = SoTexture2::CLAMP;
  scene.inheritedTexture->wrapT = SoTexture2::CLAMP;
  scene.inheritedTexture->image.setValue(SbVec2s(2, 2), 4, texels2d);
  auto * content = static_cast<SoSeparator *>(scene.screen->getChild(0));
  for (int model : {SoTexture2::REPLACE, SoTexture2::MODULATE}) {
    scene.inheritedTexture->model = model;
    for (int components = 1; components <= 4; ++components) {
      setImage(scene.image, components);
      if (!harness.render(scene, "image/inherited-2d/model-" + std::to_string(model) +
                          "/components-" + std::to_string(components))) return false;
    }
  }
  if (portableOnly) {
    // The portable inherited coordinate starts at (0,0,0,1). Sampling a
    // CLAMP/nearest 2x2 texture therefore selects texel zero, independent of
    // OpenGL's persistent current attribute after earlier drawing.
    std::vector<unsigned char> solidImage(16 * 16 * 3);
    for (size_t pixel = 0; pixel < 16 * 16; ++pixel) {
      solidImage[3 * pixel] = 180;
      solidImage[3 * pixel + 1] = 80;
      solidImage[3 * pixel + 2] = 40;
    }
    scene.image->image.setValue(SbVec2s(16, 16), 3, solidImage.data());
    const unsigned char numericTexels2d[] = {
      23,211,37,255, 227,31,83,255,
      57,73,239,255, 181,149,19,255
    };
    scene.inheritedTexture->image.setValue(SbVec2s(2, 2), 4, numericTexels2d);
    scene.inheritedTexture->model = SoTexture2::REPLACE;
    if (!harness.render(scene, "image/inherited-2d/default-coordinate-numeric")) return false;
    const size_t center = (size_t(60) * size_t(initialSize[0]) + 80) * 4;
    const auto matchesTexelZero = [center](const std::vector<uint8_t> & rgba) {
      return rgba.size() >= center + 4 &&
             std::abs(int(rgba[center]) - 23) <= 2 &&
             std::abs(int(rgba[center + 1]) - 211) <= 2 &&
             std::abs(int(rgba[center + 2]) - 37) <= 2;
    };
    if (!check(matchesTexelZero(harness.lastCpu) &&
               (!gpu || matchesTexelZero(harness.lastNative)),
               "default inherited coordinate must select the authored first texel")) return false;
    scene.quality->textureQuality = 0;
    if (!harness.render(scene, "image/inherited-2d/quality-zero")) return false;
    const auto matchesPayload = [center](const std::vector<uint8_t> & rgba) {
      return rgba.size() >= center + 4 &&
             std::abs(int(rgba[center]) - 180) <= 2 &&
             std::abs(int(rgba[center + 1]) - 80) <= 2 &&
             std::abs(int(rgba[center + 2]) - 40) <= 2;
    };
    if (!check(matchesPayload(harness.lastCpu) &&
               (!gpu || matchesPayload(harness.lastNative)),
               "texture quality zero must leave SoImage payload untextured")) return false;
    scene.quality->textureQuality = .1f;
    auto * projective = new SoTextureMatrixTransform;
    SbMatrix matrix = SbMatrix::identity();
    matrix[3][0] = .75f;
    projective->matrix = matrix;
    content->insertChild(projective, content->findChild(scene.material));
    if (!harness.render(scene, "image/inherited-2d/translated-s")) return false;
    const auto matchesTexelOne = [center](const std::vector<uint8_t> & rgba) {
      return rgba.size() >= center + 4 &&
             std::abs(int(rgba[center]) - 227) <= 2 &&
             std::abs(int(rgba[center + 1]) - 31) <= 2 &&
             std::abs(int(rgba[center + 2]) - 83) <= 2;
    };
    if (!check(matchesTexelOne(harness.lastCpu) &&
               (!gpu || matchesTexelOne(harness.lastNative)),
               "texture-matrix S translation must select authored texel one")) return false;
    matrix[3][3] = 2.0f;
    projective->matrix = matrix;
    if (!harness.render(scene, "image/inherited-2d/projective-q")) return false;
    if (!check(matchesTexelZero(harness.lastCpu) &&
               (!gpu || matchesTexelZero(harness.lastNative)),
               "homogeneous Q must divide inherited texture coordinates")) return false;
    const auto stableCpu = harness.lastCpu, stableNative = harness.lastNative;
    matrix[3][3] = 0.0f;
    projective->matrix = matrix;
    const bool zeroRejected = harness.reject(scene, "image/inherited-2d/zero-q",
                                             CoinRenderAction::INVALID_SCENE);
    matrix[3][3] = 2.0f;
    matrix[3][0] = std::numeric_limits<float>::quiet_NaN();
    projective->matrix = matrix;
    const bool nanRejected = harness.reject(scene, "image/inherited-2d/nan-matrix",
                                            CoinRenderAction::INVALID_SCENE);
    matrix[3][0] = .75f;
    projective->matrix = matrix;
    if (!zeroRejected || !nanRejected ||
        !harness.render(scene, "image/inherited-2d/matrix-recovered") ||
        !check(harness.lastCpu == stableCpu && (!gpu || harness.lastNative == stableNative),
               "invalid Q and NaN matrix must recover the exact previous frame")) return false;
    content->removeChild(projective);
  }
  // A volume's two slices differ at every texel, so the inherited volume
  // path cannot be represented by the 2D texture descriptor above.
  const unsigned char texels3d[] = {
    23,211,37,255, 227,31,83,128, 57,73,239,0, 181,149,19,255,
    247,29,211,255, 31,197,101,128, 139,41,59,0, 83,233,173,255
  };
  auto * volume = new SoTexture3;
  volume->images.setValue(SbVec3s(2, 2, 2), 4, texels3d);
  volume->wrapS = SoTexture3::CLAMP;
  volume->wrapT = SoTexture3::CLAMP;
  volume->wrapR = SoTexture3::CLAMP;
  volume->model = SoTexture3::MODULATE;
  scene.inheritedTexture->ref();
  content->replaceChild(scene.inheritedTexture, volume);
  setImage(scene.image, 4);
  const bool volumeRendered = harness.render(scene, "image/inherited-3d/two-slices");
  bool volumeNumeric = true;
  if (volumeRendered && portableOnly) {
    std::vector<unsigned char> whiteImage(16 * 16 * 3, 255);
    scene.image->image.setValue(SbVec2s(16, 16), 3, whiteImage.data());
    auto * depthTranslation = new SoTextureMatrixTransform;
    SbMatrix textureMatrix = SbMatrix::identity();
    textureMatrix[3][2] = .75f;
    depthTranslation->matrix = textureMatrix;
    content->insertChild(depthTranslation, content->findChild(scene.material));
    volumeNumeric = harness.render(scene, "image/inherited-3d/slice-one-numeric");
    if (volumeNumeric) {
      const size_t center = (size_t(60) * size_t(initialSize[0]) + 80) * 4;
      const auto matchesSliceOne = [center](const std::vector<uint8_t> & rgba) {
        return rgba.size() >= center + 4 &&
               std::abs(int(rgba[center]) - 247) <= 2 &&
               std::abs(int(rgba[center + 1]) - 29) <= 2 &&
               std::abs(int(rgba[center + 2]) - 211) <= 2;
      };
      volumeNumeric = check(matchesSliceOne(harness.lastCpu) &&
                            (!gpu || matchesSliceOne(harness.lastNative)),
                            "texture-matrix R must select the authored second volume slice");
    }
    content->removeChild(depthTranslation);
  }
  content->replaceChild(volume, scene.inheritedTexture);
  scene.inheritedTexture->unref();
  if (!volumeRendered || !volumeNumeric) return false;
  if (portableOnly) {
    // Eight distinct texture units must compose in traversal order. A last
    // unit mutation is observable even when all earlier units are unchanged.
    const unsigned char baseTexel[] = {249, 246, 243, 255};
    const unsigned char changedTexel[] = {61, 223, 117, 255};
    scene.inheritedTexture->image.setValue(SbVec2s(1, 1), 4, baseTexel);
    scene.inheritedTexture->model = SoTexture2::MODULATE;
    std::vector<unsigned char> whiteImage(16 * 16 * 3, 255);
    scene.image->image.setValue(SbVec2s(16, 16), 3, whiteImage.data());
    std::vector<SoNode *> unitNodes;
    SoTexture2 * lastTexture = nullptr;
    for (int index = 1; index != 8; ++index) {
      auto * unit = new SoTextureUnit; unit->unit = index;
      auto * texture = new SoTexture2;
      texture->image.setValue(SbVec2s(1, 1), 4, baseTexel);
      texture->model = SoTexture2::MODULATE;
      content->insertChild(unit, content->findChild(scene.image));
      content->insertChild(texture, content->findChild(scene.image));
      unitNodes.push_back(unit); unitNodes.push_back(texture);
      lastTexture = texture;
    }
    const size_t center = (size_t(60) * size_t(initialSize[0]) + 80) * 4;
    const auto matchesProduct = [center, gpu, &harness](const unsigned char * finalTexel) {
      float expected[3] = {1, 1, 1};
      for (int index = 0; index < 7; ++index)
        for (int channel = 0; channel < 3; ++channel)
          expected[channel] *= float(249 - 3 * channel) / 255.0f;
      for (int channel = 0; channel < 3; ++channel)
        expected[channel] *= float(finalTexel[channel]) / 255.0f;
      const auto matches = [center, &expected](const std::vector<uint8_t> & rgba) {
        if (rgba.size() < center + 4) return false;
        for (int channel = 0; channel < 3; ++channel)
          if (std::abs(int(rgba[center + channel]) - int(std::lround(255 * expected[channel]))) > 3)
            return false;
        return true;
      };
      return matches(harness.lastCpu) && (!gpu || matches(harness.lastNative));
    };
    if (!harness.render(scene, "image/inherited-eight-units/initial") ||
        !check(matchesProduct(baseTexel),
               "all eight inherited MODULATE units must contribute numerically")) return false;
    lastTexture->image.setValue(SbVec2s(1, 1), 4, changedTexel);
    if (!harness.render(scene, "image/inherited-eight-units/last-unit-mutated") ||
        !check(matchesProduct(changedTexel),
               "mutating unit seven must alter the eight-unit composition")) return false;
    const auto stableCpu = harness.lastCpu, stableNative = harness.lastNative;
    auto * overflow = new SoTextureUnit; overflow->unit = 8;
    content->insertChild(overflow, content->findChild(scene.image));
    auto * overflowTexture = new SoTexture2;
    overflowTexture->image.setValue(SbVec2s(1, 1), 4, baseTexel);
    overflowTexture->model = SoTexture2::MODULATE;
    content->insertChild(overflowTexture, content->findChild(scene.image));
    const bool rejected = harness.reject(scene, "image/inherited-eight-units/overflow",
                                         CoinRenderAction::UNSUPPORTED);
    content->removeChild(overflowTexture);
    content->removeChild(overflow);
    if (!rejected || !harness.render(scene, "image/inherited-eight-units/recovered") ||
        !check(harness.lastCpu == stableCpu && (!gpu || harness.lastNative == stableNative),
               "unit overflow recovery must reproduce the last accepted frame")) return false;
    for (auto it = unitNodes.rbegin(); it != unitNodes.rend(); ++it) content->removeChild(*it);
    // PRIMARY_COLOR is the SoImage pixel before textures; PREVIOUS is the
    // result of unit zero. Both are defined here and deliberately unequal.
    const unsigned char primaryImage[] = {180, 80, 40};
    std::vector<unsigned char> primaryPixels(16 * 16 * 3);
    for (size_t pixel = 0; pixel < 16 * 16; ++pixel)
      for (int channel = 0; channel < 3; ++channel)
        primaryPixels[3 * pixel + channel] = primaryImage[channel];
    scene.image->image.setValue(SbVec2s(16, 16), 3, primaryPixels.data());
    const unsigned char previousTexel[] = {37, 149, 231, 255};
    const unsigned char whiteTexel[] = {255, 255, 255, 255};
    scene.inheritedTexture->image.setValue(SbVec2s(1, 1), 4, previousTexel);
    scene.inheritedTexture->model = SoTexture2::REPLACE;
    auto * nextUnit = new SoTextureUnit; nextUnit->unit = 1;
    auto * nextTexture = new SoTexture2;
    nextTexture->image.setValue(SbVec2s(1, 1), 4, whiteTexel);
    auto * combine = new SoTextureCombine;
    combine->rgbOperation = SoTextureCombine::REPLACE;
    combine->rgbOperand.set1Value(0, SoTextureCombine::SRC_COLOR);
    combine->alphaOperation = SoTextureCombine::REPLACE;
    combine->alphaSource.set1Value(0, SoTextureCombine::TEXTURE);
    combine->alphaOperand.set1Value(0, SoTextureCombine::SRC_ALPHA);
    content->insertChild(nextUnit, content->findChild(scene.image));
    content->insertChild(nextTexture, content->findChild(scene.image));
    content->insertChild(combine, content->findChild(scene.image));
    const auto matchesColor = [center, gpu, &harness](const unsigned char * expected) {
      const auto matches = [center, expected](const std::vector<uint8_t> & rgba) {
        if (rgba.size() < center + 4) return false;
        for (int channel = 0; channel < 3; ++channel)
          if (std::abs(int(rgba[center + channel]) - int(expected[channel])) > 2) return false;
        return true;
      };
      return matches(harness.lastCpu) && (!gpu || matches(harness.lastNative));
    };
    combine->rgbSource.set1Value(0, SoTextureCombine::PREVIOUS);
    if (!harness.render(scene, "image/inherited-combine/previous") ||
        !check(matchesColor(previousTexel),
               "PREVIOUS must read the result of inherited unit zero")) return false;
    combine->rgbSource.set1Value(0, SoTextureCombine::PRIMARY_COLOR);
    if (!harness.render(scene, "image/inherited-combine/primary") ||
        !check(matchesColor(primaryImage),
               "PRIMARY_COLOR must read the original SoImage fragment")) return false;
    content->removeChild(combine);
    content->removeChild(nextTexture);
    content->removeChild(nextUnit);
  }
  // An empty screen node is inert even if the preceding texture is active.
  scene.image->image.setValue(SbVec2s(0, 0), 0, nullptr);
  if (!harness.render(scene, "image/empty-with-active-texture", false)) return false;
  const auto emptyCpu=harness.lastCpu, emptyNative=harness.lastNative;
  // (128/255)^2 is above .2512. Quantizing that product back to RGBA8
  // before SoAlphaTest would yield 64/255, below .2512, and lose the image.
  const unsigned char halfAlphaTexel[] = {255, 255, 255, 128};
  scene.inheritedTexture->image.setValue(SbVec2s(1, 1), 4, halfAlphaTexel);
  scene.inheritedTexture->model = SoTexture2::MODULATE;
  std::vector<unsigned char> halfAlphaImage(16 * 16 * 4, 255);
  for (size_t pixel = 0; pixel < 16 * 16; ++pixel) halfAlphaImage[4 * pixel + 3] = 128;
  scene.image->image.setValue(SbVec2s(16, 16), 4, halfAlphaImage.data());
  auto * alphaTest = new SoAlphaTest;
  alphaTest->function = SoAlphaTest::GREATER;
  alphaTest->value = .2512f;
  content->insertChild(alphaTest, content->findChild(scene.image));
  if (!harness.render(scene, "image/inherited-alpha-precision/pass") ||
      !check(harness.lastCpu != emptyCpu && (!gpu || harness.lastNative != emptyNative),
             "the unrounded product of image and inherited texture alpha must pass .2512")) return false;
  alphaTest->value = .2525f;
  if (!harness.render(scene, "image/inherited-alpha-precision/reject", false) ||
      !check(harness.lastCpu == emptyCpu && (!gpu || harness.lastNative == emptyNative),
             "the same alpha product must fail .2525 without publishing image pixels")) return false;
  alphaTest->function = SoAlphaTest::EQUAL;
  alphaTest->value = (128.0f / 255.0f) * (128.0f / 255.0f);
  if (!harness.render(scene, "image/inherited-alpha-precision/equal-f32") ||
      !check(harness.lastCpu != emptyCpu && (!gpu || harness.lastNative != emptyNative),
             "the exact f32 alpha product must survive intermediate precision")) return false;
  content->removeChild(alphaTest);
  scene.inheritedTexture->image.setValue(SbVec2s(0, 0), 0, nullptr);
  setImage(scene.image, 4);
  if (!harness.render(scene, "image/inherited-negative-repair") ||
      !check(harness.lastCpu==ordinaryCpu && (!gpu || harness.lastNative==ordinaryNative),
             "removing inherited texture must restore original SoImage frame")) return false;
  scene.material->transparency = .55f;
  harness.transparency(SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
  return harness.render(scene, "image/sorted-triangle-profile");
}

bool inheritedHistory(bool gpu) {
  Scene scene(false); Harness harness(gpu);
  harness.glReference = false;
  scene.quality->textureQuality = .1f;
  scene.inheritedTexture->wrapS = SoTexture2::CLAMP;
  scene.inheritedTexture->wrapT = SoTexture2::CLAMP;
  scene.inheritedTexture->model = SoTexture2::REPLACE;
  const unsigned char texels[] = {
    23,211,37,255, 227,31,83,255,
    57,73,239,255, 181,149,19,255
  };
  scene.inheritedTexture->image.setValue(SbVec2s(2,2),4,texels);
  const unsigned char payload[] = {180,80,40};
  std::vector<unsigned char> image(16*16*3);
  for(size_t pixel=0;pixel<16*16;++pixel)
    for(int channel=0;channel<3;++channel) image[3*pixel+channel]=payload[channel];
  scene.image->image.setValue(SbVec2s(16,16),3,image.data());
  auto * content=static_cast<SoSeparator *>(scene.screen->getChild(0));
  const size_t center=(size_t(60)*size_t(initialSize[0])+80)*4;
  const auto matches=[&harness,gpu,center](const unsigned char * expected) {
    const auto one=[center,expected](const std::vector<uint8_t> & rgba) {
      if(rgba.size()<center+4) return false;
      for(int channel=0;channel<3;++channel)
        if(std::abs(int(rgba[center+channel])-int(expected[channel]))>2) return false;
      return true;
    };
    return one(harness.lastCpu) && (!gpu || one(harness.lastNative));
  };
  auto * points=new SoCoordinate3;
  const SbVec3f vertices[]={{-70,-50,-1},{-65,-50,-1},{-70,-45,-1}};
  points->point.setValues(0,3,vertices);
  auto * face=new SoFaceSet;face->numVertices.set1Value(0,3);
  const int originalTextureIndex=content->findChild(scene.inheritedTexture);
  scene.inheritedTexture->ref();
  content->removeChild(scene.inheritedTexture);
  content->insertChild(points,content->findChild(scene.image));
  content->insertChild(face,content->findChild(scene.image));
  content->insertChild(scene.inheritedTexture,content->findChild(scene.image));
  if(!harness.render(scene,"image/inherited-history/untextured-precursor") ||
     !check(matches(texels),"untextured precursor preserves the default inherited UV")) return false;
  // An array draw with varying UVs makes OpenGL's current coordinate
  // indeterminate (§2.8). Reject that texture history before publication.
  content->removeChild(scene.inheritedTexture);
  content->insertChild(scene.inheritedTexture,originalTextureIndex);
  auto * uv=new SoTextureCoordinate2;
  const SbVec2f distinct[]={{.25f,0},{.75f,0},{.25f,.75f}};
  uv->point.setValues(0,3,distinct);
  content->insertChild(uv,content->findChild(face));
  const bool rejected=harness.reject(scene,"image/inherited-history/indeterminate-array-current",
                                     CoinRenderAction::UNSUPPORTED);
  // Texture quality zero disables the effective unit. Even the indeterminate
  // current coordinate left by an active array cannot affect glDrawPixels
  // while the unit is disabled. Reactivating the array remains unsupported.
  scene.quality->textureQuality=0;
  const bool disabled=rejected &&
    harness.render(scene,"image/inherited-history/quality-zero-after-array") &&
    check(matches(payload),"disabled texture ignores indeterminate array UV");
  scene.quality->textureQuality=.1f;
  const bool reactivated=disabled &&
    harness.reject(scene,"image/inherited-history/quality-restored-array",
                   CoinRenderAction::UNSUPPORTED);
  content->removeChild(uv);
  content->removeChild(scene.inheritedTexture);
  const bool recovered=reactivated &&
    harness.render(scene,"image/inherited-history/recovered-untextured") &&
    check(matches(payload),"untextured recovery does not invent a current UV");
  content->insertChild(scene.inheritedTexture,originalTextureIndex);
  scene.inheritedTexture->unref();
  content->removeChild(face);content->removeChild(points);
  return recovered;
}

bool inheritedDisabledGenerators(bool gpu) {
  const unsigned char payload[]={180,80,40};
  const unsigned char texel[]={23,211,37,255};
  for(int kind=0;kind<3;++kind) {
    Scene scene(false);Harness harness(gpu);harness.glReference=false;
    struct EnvironmentCapture : CaptureCpuBackend {
      CoinRenderFramePlan frame;
      CoinRenderSubmitResult submit(const CoinRenderFramePlan & candidate,CoinRenderTargetP & target) override {
        frame=candidate;return CaptureCpuBackend::submit(candidate,target);
      }
    };
    EnvironmentCapture * environmentCapture=nullptr;
    if(kind==1) {environmentCapture=new EnvironmentCapture;harness.cpu->getPimpl()->backend.reset(environmentCapture);harness.observer=environmentCapture;}
    auto * content=static_cast<SoSeparator *>(scene.screen->getChild(0));
    scene.quality->textureQuality=0;
    scene.inheritedTexture->image.setValue(SbVec2s(1,1),4,texel);
    scene.inheritedTexture->model=SoTexture2::REPLACE;
    std::vector<unsigned char> pixels(16*16*3);
    for(size_t p=0;p<16*16;++p) for(int c=0;c<3;++c)
      pixels[3*p+c]=payload[c];
    scene.image->image.setValue(SbVec2s(16,16),3,pixels.data());
    SoNode * generator=kind==0 ? static_cast<SoNode *>(new SoTextureCoordinateNormalMap) :
      kind==1 ? static_cast<SoNode *>(new SoTextureCoordinateEnvironment) :
                static_cast<SoNode *>(new SoTextureCoordinateReflectionMap);
    content->insertChild(generator,content->findChild(scene.image));
    const std::string label=std::string("image/quality-zero/")+
      (kind==0 ? "normal-map" : kind==1 ? "environment" : "reflection-map");
    const size_t center=(size_t(60)*size_t(initialSize[0])+80)*4;
    const auto payloadMatches=[&harness,gpu,center,&payload]() {
      const auto one=[center,&payload](const std::vector<uint8_t> & rgba) {
        if(rgba.size()<center+4) return false;
        for(int c=0;c<3;++c)
          if(std::abs(int(rgba[center+c])-int(payload[c]))>2) return false;
        return true;
      };
      return one(harness.lastCpu)&&(!gpu||one(harness.lastNative));
    };
    if(!harness.render(scene,label+"/disabled") ||
       !check(payloadMatches(),label+" disabled unit preserves image PRIMARY color"))
      return false;
    const auto stableCpu=harness.lastCpu,stableGpu=harness.lastNative;
    scene.quality->textureQuality=.1f;
    if(kind==1) {
      // Environment defines R/Q=1 and initial GL normalZ is defined. The
      // other two fresh generators retain default Q=0 for projected2D.
      if(!harness.render(scene,label+"/enabled-defined-environment"))return false;
      const auto outputMatches=[center,&texel](const std::vector<uint8_t>&rgba) {
        if(rgba.size()<center+4)return false;for(int c=0;c<3;++c)if(std::abs(int(rgba[center+c])-int(texel[c]))>2)return false;return true;
      };
      if(!check(outputMatches(harness.lastCpu)&&(!gpu||outputMatches(harness.lastNative)),label+" enabled Environment samples inherited REPLACE texel"))return false;
      const auto&frame=environmentCapture->frame;unsigned images=0;bool delivered=true;
      const double x=72,y=52,z=1.-2.*9./39.,length=std::sqrt(x*x+y*y+z*z),rx=x/length,ry=y/length,rz=-z/length;
      const double m=2.*std::sqrt(rx*rx+ry*ry+(rz+1)*(rz+1));const double expected[]={rx/m+.5,ry/m+.5,1,1};
      for(const auto&draw:frame.draws)if(draw.sourceNodeId==scene.image->getNodeId()&&draw.renderStateSlot<frame.renderStates.size()) {
        const auto&state=frame.renderStates[draw.renderStateSlot];if(!state.rasterPixels)continue;++images;
        delivered &= state.rasterTextureSourceSlot<frame.renderStates.size();
        for(int c=0;c<4;++c)delivered &= std::abs(double(state.rasterTextureCoordinates[0][c])-expected[c])<=8.*std::numeric_limits<float>::epsilon();
        if(state.rasterTextureSourceSlot<frame.renderStates.size()) {const auto&source=frame.renderStates[state.rasterTextureSourceSlot];delivered &= !source.rasterPixels&&source.hasTexture&&source.textureImageSlot<frame.textures.size();
          if(source.textureImageSlot<frame.textures.size())delivered &= frame.textures[source.textureImageSlot].pixelsRgba==std::vector<uint8_t>(texel,texel+4);}
      }
      if(!check(images==1&&delivered,label+" Environment Core keeps aligned RasterPos formula, native R/Q1 and actual inherited texture"))return false;
    } else {
      // The now implemented generator can be recognized while projected Q0
      // is invalid. Both explicit unsupported-profile and invalid-input
      // rejection are legitimate; publication invariants remain identical.
      const auto refusesQzero=[](CoinRenderAction::Status status) {return status==CoinRenderAction::UNSUPPORTED||status==CoinRenderAction::INVALID_SCENE;};
      const size_t calls=harness.observer->submissions;const uint64_t cpuSerial=harness.cpu->getLastSubmissionSerial();
      std::vector<float>beforeCpuDepth,afterDepth;std::vector<uint8_t>after;harness.cpu->readbackDepth(beforeCpuDepth);
      harness.cpuAction.apply(scene.root);harness.cpu->readbackRGBA(after);harness.cpu->readbackDepth(afterDepth);
      const bool cpuCalls=harness.observer->submissions==calls,cpuRgba=after==stableCpu,cpuDepth=afterDepth==beforeCpuDepth,cpuPublication=harness.cpu->getLastSubmissionSerial()==cpuSerial;
      std::cout<<label<<"/enabled-Qzero CPU status="<<int(harness.cpuAction.getLastStatus())<<" error="<<harness.cpuAction.getLastError().getString()
        <<" zero_submits="<<cpuCalls<<" rgba_preserved="<<cpuRgba<<" depth_preserved="<<cpuDepth<<" serial_preserved="<<cpuPublication<<'\n';
      bool refused=check(refusesQzero(harness.cpuAction.getLastStatus())&&harness.cpuAction.getLastError().getLength()!=0&&cpuCalls&&cpuRgba&&cpuDepth&&cpuPublication,
        label+" projected Qzero must refuse explicitly before changing CPU RGBA, depth, serial or submit count");
      if(gpu) {const uint64_t gpuSerial=harness.native->getLastSubmissionSerial();std::vector<float>beforeGpuDepth;harness.native->readbackDepth(beforeGpuDepth);
        harness.gpuAction.apply(scene.root);harness.native->readbackRGBA(after);harness.native->readbackDepth(afterDepth);
        const bool gpuRgba=after==stableGpu,gpuDepth=afterDepth==beforeGpuDepth,gpuPublication=harness.native->getLastSubmissionSerial()==gpuSerial;
        std::cout<<label<<"/enabled-Qzero GPU status="<<int(harness.gpuAction.getLastStatus())<<" error="<<harness.gpuAction.getLastError().getString()
          <<" rgba_preserved="<<gpuRgba<<" depth_preserved="<<gpuDepth<<" serial_preserved="<<gpuPublication<<'\n';
        refused &= check(refusesQzero(harness.gpuAction.getLastStatus())&&harness.gpuAction.getLastError().getLength()!=0&&gpuRgba&&gpuDepth&&gpuPublication,
          label+" projected Qzero must refuse explicitly before changing GPU RGBA, depth or submission serial");
      }
      if(!refused)return false;
    }
    scene.quality->textureQuality=0;
    if(!harness.render(scene,label+"/disabled-recovery") ||
       !check(harness.lastCpu==stableCpu && (!gpu||harness.lastNative==stableGpu),
         label+" recovery reproduces image after enabled generator control"))
      return false;
  }
  return true;
}

bool inheritedQualityZeroPrecursor(bool gpu) {
  Scene scene(false);Harness harness(gpu);harness.glReference=false;
  auto * content=static_cast<SoSeparator *>(scene.screen->getChild(0));
  const unsigned char payload[]={180,80,40};
  const unsigned char texel[]={23,211,37,255};
  std::vector<unsigned char> pixels(16*16*3);
  for(size_t p=0;p<16*16;++p) for(int c=0;c<3;++c)
    pixels[3*p+c]=payload[c];
  scene.image->image.setValue(SbVec2s(16,16),3,pixels.data());
  scene.inheritedTexture->image.setValue(SbVec2s(1,1),4,texel);
  scene.inheritedTexture->model=SoTexture2::REPLACE;
  scene.quality->textureQuality=0;
  auto * points=new SoCoordinate3;
  const SbVec3f vertices[]={{-70,-50,-1},{-65,-50,-1},{-70,-45,-1}};
  points->point.setValues(0,3,vertices);
  auto * uv=new SoTextureCoordinate2;
  const SbVec2f varying[]={{.15f,.2f},{.8f,.2f},{.2f,.85f}};
  uv->point.setValues(0,3,varying);
  auto * face=new SoFaceSet;face->numVertices.set1Value(0,3);
  content->insertChild(uv,content->findChild(scene.image));
  content->insertChild(points,content->findChild(scene.image));
  content->insertChild(face,content->findChild(scene.image));
  const size_t center=(size_t(60)*size_t(initialSize[0])+80)*4;
  const auto matches=[&harness,gpu,center](const unsigned char * expected) {
    const auto one=[center,expected](const std::vector<uint8_t> & rgba) {
      if(rgba.size()<center+4) return false;
      for(int c=0;c<3;++c)
        if(std::abs(int(rgba[center+c])-int(expected[c]))>2) return false;
      return true;
    };
    return one(harness.lastCpu)&&(!gpu||one(harness.lastNative));
  };
  if(!harness.render(scene,"image/quality-zero/precursor-and-image") ||
     !check(matches(payload),"quality-zero geometry and image ignore varying UV"))
    return false;
  // The same geometry is drawn with quality zero, then the inherited image
  // alone enables texturing later in this traversal. No texture coordinate
  // array was submitted for the precursor, so current UV remains defined.
  auto * enable=new SoComplexity;enable->textureQuality=.1f;
  content->insertChild(enable,content->findChild(scene.image));
  return harness.render(scene,"image/quality-zero/precursor-then-enable") &&
    check(matches(texel),"disabled precursor must preserve default UV when image enables unit");
}

bool inheritedTexgen(bool gpu) {
  Scene scene(false); Harness harness(gpu); harness.glReference=false;
  auto * content=static_cast<SoSeparator *>(scene.screen->getChild(0));
  scene.quality->textureQuality=.1f;
  scene.inheritedTexture->wrapS=SoTexture2::CLAMP;
  scene.inheritedTexture->wrapT=SoTexture2::CLAMP;
  scene.inheritedTexture->model=SoTexture2::REPLACE;
  const unsigned char texels[] = {
    23,211,37,255, 227,31,83,255,
    57,73,239,255, 181,149,19,255
  };
  scene.inheritedTexture->image.setValue(SbVec2s(2,2),4,texels);
  std::vector<unsigned char> white(16*16*3,255);
  scene.image->image.setValue(SbVec2s(16,16),3,white.data());
  const size_t center=(size_t(60)*size_t(initialSize[0])+80)*4;
  const auto matches=[&harness,gpu,center](const unsigned char * expected) {
    const auto one=[center,expected](const std::vector<uint8_t> & rgba) {
      if(rgba.size()<center+4) return false;
      for(int channel=0;channel<3;++channel)
        if(std::abs(int(rgba[center+channel])-int(expected[channel]))>2) return false;
      return true;
    };
    return one(harness.lastCpu)&&(!gpu||one(harness.lastNative));
  };
  auto * object=new SoTextureCoordinateObject;
  object->factorS=SbVec4f(0,0,0,.75f);
  object->factorT=SbVec4f(0,0,0,0);
  object->factorR=SbVec4f(0,0,0,0);
  object->factorQ=SbVec4f(0,0,0,1);
  content->insertChild(object,content->findChild(scene.image));
  if(!harness.render(scene,"image/inherited-object-gen/anchor-s") ||
     !check(matches(texels+4),"OBJECT_LINEAR must evaluate S at the raster anchor")) return false;
  object->factorQ=SbVec4f(0,0,0,2);
  if(!harness.render(scene,"image/inherited-object-gen/q-two") ||
     !check(matches(texels),"OBJECT_LINEAR Q must divide generated S")) return false;
  // CoinGL evaluates OBJECT_LINEAR at glRasterPos in its temporary pixel
  // projection: CENTER/HALF puts a 16x16 image at (72,52), LEFT at (80,52).
  object->factorS=SbVec4f(.01f,0,0,.03f);
  object->factorQ=SbVec4f(0,0,0,1);
  if(!harness.render(scene,"image/inherited-object-gen/pixel-anchor-72") ||
     !check(matches(texels+4),"object plane .01*x+.03 must sample S=.75 at x=72")) return false;
  object->factorS=SbVec4f(.0625f,0,0,-4.25f);
  if(!harness.render(scene,"image/inherited-object-gen/center-origin") ||
     !check(matches(texels),"CENTER alignment must generate S=.25 at x=72")) return false;
  scene.image->horAlignment=SoImage::LEFT;
  const size_t leftInterior=(size_t(60)*size_t(initialSize[0])+84)*4;
  const auto matchesLeft=[&harness,gpu,leftInterior](const unsigned char * expected) {
    const auto one=[leftInterior,expected](const std::vector<uint8_t> & rgba) {
      if(rgba.size()<leftInterior+4) return false;
      for(int channel=0;channel<3;++channel)
        if(std::abs(int(rgba[leftInterior+channel])-int(expected[channel]))>2) return false;
      return true;
    };
    return one(harness.lastCpu)&&(!gpu||one(harness.lastNative));
  };
  if(!harness.render(scene,"image/inherited-object-gen/left-origin") ||
     !check(matchesLeft(texels+4),"LEFT alignment must generate S=.75 at x=80")) return false;
  scene.image->horAlignment=SoImage::CENTER;
  object->factorS=SbVec4f(0,0,0,0);
  object->factorQ=SbVec4f(0,0,0,1);
  auto * matrixNode=new SoTextureMatrixTransform;
  SbMatrix matrix=SbMatrix::identity();matrix[3][0]=.75f;
  matrixNode->matrix=matrix;
  content->insertChild(matrixNode,content->findChild(scene.image));
  if(!harness.render(scene,"image/inherited-object-gen/matrix-after") ||
     !check(matches(texels+4),"texture matrix must act after OBJECT_LINEAR generation")) return false;
  content->removeChild(matrixNode);
  content->removeChild(object);
  auto * plane=new SoTextureCoordinatePlane;
  plane->directionS.setValue(.01f,0,0);
  plane->directionT.setValue(0,0,0);
  content->insertChild(plane,content->findChild(scene.image));
  if(!harness.render(scene,"image/inherited-plane-gen/anchor-72") ||
     !check(matches(texels+4),"Plane OBJECT_LINEAR must generate S=.72 at raster x=72")) return false;
  auto * planeMatrix=new SoTextureMatrixTransform;
  SbMatrix planeProjective=SbMatrix::identity();planeProjective[3][3]=2;
  planeMatrix->matrix=planeProjective;
  content->insertChild(planeMatrix,content->findChild(scene.image));
  if(!harness.render(scene,"image/inherited-plane-gen/matrix-after") ||
     !check(matches(texels),"texture-matrix Q must divide Plane-generated S=.72")) return false;
  content->removeChild(planeMatrix);
  content->removeChild(plane);
  const unsigned char volumeTexels[] = {
    23,211,37,255, 227,31,83,255, 57,73,239,255, 181,149,19,255,
    247,29,211,255, 31,197,101,255, 139,41,59,255, 83,233,173,255
  };
  auto * volume=new SoTexture3;
  volume->images.setValue(SbVec3s(2,2,2),4,volumeTexels);
  volume->wrapS=SoTexture3::CLAMP;
  volume->wrapT=SoTexture3::CLAMP;
  volume->wrapR=SoTexture3::CLAMP;
  volume->model=SoTexture3::MODULATE;
  scene.inheritedTexture->ref();
  content->replaceChild(scene.inheritedTexture,volume);
  auto * volumeGen=new SoTextureCoordinateObject;
  volumeGen->factorS=SbVec4f(0,0,0,0);
  volumeGen->factorT=SbVec4f(0,0,0,0);
  volumeGen->factorR=SbVec4f(0,0,0,.75f);
  volumeGen->factorQ=SbVec4f(0,0,0,1);
  content->insertChild(volumeGen,content->findChild(scene.image));
  const bool volumeGood=harness.render(scene,"image/inherited-object-gen/volume-r") &&
    check(matches(volumeTexels+16),"OBJECT_LINEAR R must select volume slice one");
  content->removeChild(volumeGen);
  content->replaceChild(volume,scene.inheritedTexture);
  scene.inheritedTexture->unref();
  return volumeGood;
}

bool inheritedModels(bool gpu) {
  Scene scene(false); Harness harness(gpu); harness.glReference=false;
  scene.quality->textureQuality=.1f;
  const unsigned char source[]={180,80,40};
  std::vector<unsigned char> image(16*16*3);
  for(size_t pixel=0;pixel<16*16;++pixel)
    for(int channel=0;channel<3;++channel) image[3*pixel+channel]=source[channel];
  scene.image->image.setValue(SbVec2s(16,16),3,image.data());
  const size_t center=(size_t(60)*size_t(initialSize[0])+80)*4;
  const auto matches=[&harness,gpu,center](const float expected[3]) {
    const auto one=[center,expected](const std::vector<uint8_t> & rgba) {
      if(rgba.size()<center+4) return false;
      for(int channel=0;channel<3;++channel)
        if(std::abs(int(rgba[center+channel])-int(std::lround(expected[channel])))>2) return false;
      return true;
    };
    return one(harness.lastCpu)&&(!gpu||one(harness.lastNative));
  };
  const unsigned char texel[]={100,200,50,128};
  scene.inheritedTexture->image.setValue(SbVec2s(1,1),4,texel);
  scene.inheritedTexture->model=SoTexture2::DECAL;
  const float alpha=128.0f/255.0f;
  const float decal[]={source[0]*(1-alpha)+texel[0]*alpha,
                       source[1]*(1-alpha)+texel[1]*alpha,
                       source[2]*(1-alpha)+texel[2]*alpha};
  if(!harness.render(scene,"image/inherited-model/decal") ||
     !check(matches(decal),"DECAL must mix image and texture RGB by texture alpha")) return false;
  const unsigned char opaqueTexel[]={100,200,50,255};
  scene.inheritedTexture->image.setValue(SbVec2s(1,1),4,opaqueTexel);
  scene.inheritedTexture->model=SoTexture2::BLEND;
  const unsigned char constant[]={20,220,130};
  scene.inheritedTexture->blendColor.setValue(float(constant[0])/255,
                                               float(constant[1])/255,
                                               float(constant[2])/255);
  float blended[3];
  for(int channel=0;channel<3;++channel) {
    const float weight=float(opaqueTexel[channel])/255.0f;
    blended[channel]=source[channel]*(1-weight)+constant[channel]*weight;
  }
  return harness.render(scene,"image/inherited-model/blend") &&
    check(matches(blended),"BLEND must use image RGB, texture RGB and constant color");
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
  if (!harness.render(scene, "text/sorted-triangle-profile")) return false;
  harness.transparency(SoGLRenderAction::BLEND); scene.material->transparency = 0;
  scene.screen->ref(); scene.root->removeChild(scene.screen);
  auto * shadow = new SoShadowGroup;
  shadow->isActive = TRUE; shadow->addChild(scene.screen); scene.screen->unref();
  scene.root->addChild(shadow);
  return harness.render(scene, "text/active-shadow-without-maps");
}
}
int main(int argc, char ** argv) {
  bool gpu = false, inheritedOnly = false, inheritedPortableOnly = false; const char * font = "DejaVu Sans";
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--gpu") gpu = true;
    else if (argument == "--capture") gpu = false;
    else if (argument == "--inherited-only") inheritedOnly = true;
    else if (argument == "--inherited-portable-only") inheritedPortableOnly = true;
    else if (argument == "--font" && i + 1 < argc) font = argv[++i];
    else { std::cerr << "Usage: CoinRenderScreenContentTest --capture|--gpu [--inherited-only|--inherited-portable-only] [--font NAME]\n"; return 2; }
  }
  SoDB::init(); CoinRenderAction::initClass();
  if (gpu && !CoinRenderAction::isGpuBackendAvailable()) {
    std::cerr << "CoinRenderScreenContentTest requires the selected GPU backend\n"; return 77;
  }
  if (inheritedOnly || inheritedPortableOnly) {
    if (!inheritedImage(gpu, inheritedPortableOnly) ||
        (inheritedPortableOnly && (!inheritedHistory(gpu) ||
          !inheritedDisabledGenerators(gpu) || !inheritedQualityZeroPrecursor(gpu) ||
          !inheritedTexgen(gpu) || !inheritedModels(gpu)))) return 1;
    std::cout << "CoinRenderScreenContentTest inherited-image "
              << (inheritedPortableOnly ? "portable-numeric " : "CoinGL-strict ")
              << (gpu ? (inheritedPortableOnly ? "GPU/CPU" : "GPU/CoinGL") : "capture/CPU")
              << " passed\n";
    return 0;
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
