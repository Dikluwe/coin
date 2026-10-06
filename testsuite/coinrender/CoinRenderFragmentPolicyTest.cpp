#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#if COIN_HAVE_LEGACY_GL_RENDERER
#include <Inventor/SoOffscreenRenderer.h>
#endif
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoFont.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCombine.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr int width = 96, height = 80;
using Color = std::array<int, 3>;
bool check(bool condition, const std::string & message) {
  if (!condition) std::cerr << "CoinRenderFragmentPolicyTest: " << message << '\n';
  return condition;
}
struct AlphaRecord { uint32_t function; float reference; };
class CaptureBackend : public CoinRenderCpuReferenceBackend {
public:
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target) override {
    ++submissions; revision = frame.revision; draws = frame.draws.size(); alpha.clear();
    for (const auto & draw : frame.draws) {
      const auto & state = frame.renderStates[draw.renderStateSlot];
      alpha.push_back({static_cast<uint32_t>(state.alphaTestFunction), state.alphaTestReference});
    }
    return CoinRenderCpuReferenceBackend::submit(frame, target);
  }
  size_t submissions = 0, draws = 0;
  uint64_t revision = 0;
  std::vector<AlphaRecord> alpha;
};

struct Scene {
  Scene() {
    root = new SoSeparator; root->ref();
    camera = new SoOrthographicCamera;
    camera->height = 2; camera->position.setValue(0, 0, 5);
    camera->nearDistance = 1; camera->farDistance = 20; root->addChild(camera);
    auto * lighting = new SoLightModel; lighting->model = SoLightModel::BASE_COLOR;
    root->addChild(lighting);
    auto * quality = new SoComplexity; quality->textureQuality = .5f; root->addChild(quality);
  }
  ~Scene() { root->unref(); }
  Scene(const Scene &) = delete;
  Scene & operator=(const Scene &) = delete;
  SoSeparator * root = nullptr;
  SoOrthographicCamera * camera = nullptr;
};
struct Quad {
  explicit Quad(float z = 0, const SbColor & color = SbColor(1, 0, 0),
                float left = -.8f, float right = .8f) {
    group = new SoSeparator;
    depth = new SoDepthBuffer; depth->test = TRUE; depth->write = TRUE;
    depth->function = SoDepthBuffer::LEQUAL; group->addChild(depth);
    material = new SoMaterial; material->diffuseColor.setValue(color); group->addChild(material);
    alpha = new SoAlphaTest; group->addChild(alpha);
    textures = new SoSwitch; textures->whichChild = SO_SWITCH_ALL; group->addChild(textures);
    coordinates = new SoCoordinate3;
    const SbVec3f points[] = {{left, -.7f, z}, {right, -.7f, z},
                              {right, .7f, z}, {left, .7f, z}};
    coordinates->point.setValues(0, 4, points); group->addChild(coordinates);
    auto * faces = new SoIndexedFaceSet; const int32_t indices[] = {0, 1, 2, 3, -1};
    faces->coordIndex.setValues(0, 5, indices); group->addChild(faces);
  }
  SoTexture2 * texture(int unit, int components, const std::vector<uint8_t> & pixels,
                      int imageWidth = 1, int imageHeight = 1) {
    auto * selected = new SoTextureUnit; selected->unit = unit; textures->addChild(selected);
    auto * uv = new SoTextureCoordinate2;
    const SbVec2f points[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    uv->point.setValues(0, 4, points); textures->addChild(uv);
    auto * image = new SoTexture2; image->model = SoTexture2::REPLACE;
    image->wrapS = SoTexture2::CLAMP; image->wrapT = SoTexture2::CLAMP;
    image->image.setValue(SbVec2s(imageWidth, imageHeight), components, pixels.data());
    textures->addChild(image); return image;
  }
  SoSeparator * group = nullptr;
  SoDepthBuffer * depth = nullptr;
  SoMaterial * material = nullptr;
  SoAlphaTest * alpha = nullptr;
  SoSwitch * textures = nullptr;
  SoCoordinate3 * coordinates = nullptr;
};

size_t colored(const std::vector<uint8_t> & pixels) {
  size_t count = 0;
  for (size_t i = 0; i < pixels.size(); i += 4)
    count += pixels[i] > 3 || pixels[i + 1] > 3 || pixels[i + 2] > 3;
  return count;
}
bool pixel(const std::vector<uint8_t> & pixels, int x, int y, const Color & expected,
           const std::string & label) {
  if (!check(pixels.size() == size_t(width * height * 4), label + ": incomplete pixels")) return false;
  const size_t offset = size_t(y * width + x) * 4;
  bool good = true;
  for (int c = 0; c < 3; ++c) good &= std::abs(int(pixels[offset + c]) - expected[c]) <= 3;
  if (!good) std::cerr << label << " at=" << x << ',' << y << " actual="
    << int(pixels[offset]) << ',' << int(pixels[offset + 1]) << ',' << int(pixels[offset + 2])
    << " expected=" << expected[0] << ',' << expected[1] << ',' << expected[2] << '\n';
  return check(good, label + ": discriminating sample failed");
}

// CoinGL is the pixel oracle. Capture mode has independent numeric checks;
// it neither reconstructs the new lowering nor requires a GL context.
struct Harness {
  explicit Harness(bool selectedGpu, const CoinRenderOptions & options = CoinRenderOptions())
    : gpu(selectedGpu), cpuAction(SbViewportRegion(width, height)),
      gpuAction(SbViewportRegion(width, height))
#if COIN_HAVE_LEGACY_GL_RENDERER
      , gl(SbViewportRegion(width, height))
#endif
  {
    cpu.reset(CoinRenderTarget::createOffscreen(SbVec2i32(width, height), options));
    if (cpu) {
      observer = new CaptureBackend; cpu->getPimpl()->backend.reset(observer);
      cpu->getPimpl()->depthBuffer.assign(width * height, 1.0f); cpuAction.setRenderTarget(cpu.get());
    }
    if (gpu) {
      native.reset(CoinRenderTarget::createOffscreen(SbVec2i32(width, height), options));
      if (native) gpuAction.setRenderTarget(native.get());
    }
    cpuAction.setTransparencyType(CoinRenderAction::BLEND);
    gpuAction.setTransparencyType(CoinRenderAction::BLEND);
    cpuAction.setBackgroundColor(SbColor4f(0, 0, 0, 1));
    gpuAction.setBackgroundColor(SbColor4f(0, 0, 0, 1));
#if COIN_HAVE_LEGACY_GL_RENDERER
    gl.setComponents(SoOffscreenRenderer::RGB); gl.setBackgroundColor(SbColor(0, 0, 0));
    gl.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
#endif
  }
  ~Harness() { cpuAction.setRenderTarget(nullptr); gpuAction.setRenderTarget(nullptr); }
  void transparency(CoinRenderAction::TransparencyType mode) {
    cpuAction.setTransparencyType(mode); gpuAction.setTransparencyType(mode); coinMode = mode;
    cpuAction.setSortedLayersNumPasses(4); gpuAction.setSortedLayersNumPasses(4);
#if COIN_HAVE_LEGACY_GL_RENDERER
    gl.getGLRenderAction()->setTransparencyType(static_cast<SoGLRenderAction::TransparencyType>(mode));
    gl.getGLRenderAction()->setSortedLayersNumPasses(4);
    glChannels = mode == CoinRenderAction::SORTED_LAYERS_BLEND ? 4 : 3;
    gl.setComponents(glChannels == 4 ? SoOffscreenRenderer::RGB_TRANSPARENCY : SoOffscreenRenderer::RGB);
#endif
  }
  bool referencePixels(SoNode * root, const std::string & label) {
#if COIN_HAVE_LEGACY_GL_RENDERER
    if (!check(gl.render(root) && gl.getBuffer() &&
               int(gl.getGLRenderAction()->getTransparencyType()) == int(coinMode),
               label + ": mandatory CoinGL oracle unavailable or changed transparency mode")) return false;
    reference.resize(width * height * 4); const uint8_t * source = gl.getBuffer();
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
      const size_t a = size_t(y * width + x) * 4;
      const size_t b = size_t((height - 1 - y) * width + x) * glChannels;
      for (int c = 0; c < 3; ++c) reference[a + c] = source[b + c];
      reference[a + 3] = 255;
    }
    return true;
#else
    (void)root;
    return check(false, label + ": mandatory CoinGL oracle was not compiled");
#endif
  }
  bool compare(const std::vector<uint8_t> & actual, const std::string & label) {
    if (!check(actual.size() == reference.size() && !actual.empty(), label + ": full readback required")) return false;
    size_t active = 0, bad = 0; uint64_t error = 0; int maximum = 0;
    for (size_t i = 0; i < actual.size(); i += 4) {
      if (!(actual[i] > 3 || actual[i + 1] > 3 || actual[i + 2] > 3 ||
            reference[i] > 3 || reference[i + 1] > 3 || reference[i + 2] > 3)) continue;
      ++active; bool different = false;
      for (int c = 0; c < 3; ++c) {
        const int delta = std::abs(int(actual[i + c]) - int(reference[i + c]));
        error += delta; maximum = std::max(maximum, delta); different |= delta > 3;
      }
      bad += different;
    }
    const double mae = active ? double(error) / (active * 3) : 0;
    std::cout << label << " gl_pixels=" << colored(reference) << " actual_pixels=" << colored(actual)
              << " roi_mae=" << mae << " max=" << maximum << " pixels_over3=" << bad << '\n';
    return check(mae <= 1.5 && bad <= std::max<size_t>(4, active / 50), label + ": differs from CoinGL");
  }
  // Every discard group is preceded by a visible control. Empty output alone
  // cannot qualify an implementation that omitted the shape altogether.
  bool render(SoNode * root, const std::string & label, bool visible = true,
              SoNode * referenceRoot = nullptr, bool compareFullRaster = true,
              bool allowEmptyCapture = false) {
    if (!check(cpu && observer && (!gpu || native), label + ": target unavailable")) return false;
    const size_t submissions = observer->submissions;
    cpuAction.apply(root); cpu->readbackRGBA(lastCpu);
    if (!check(cpuAction.getLastStatus() == CoinRenderAction::SUCCESS &&
               observer->submissions == submissions + 1 && (observer->draws || allowEmptyCapture) &&
               lastCpu.size() == size_t(width * height * 4),
               label + ": CPU capture/submission failed: " + cpuAction.getLastError().getString())) return false;
    if (!check(visible ? colored(lastCpu) >= 8 : colored(lastCpu) == 0,
               label + ": CPU visibility differs from the scene contract")) return false;
    if (!gpu) return true;
    if (!referencePixels(referenceRoot ? referenceRoot : root, label)) return false;
    if (!check(visible ? colored(reference) >= 8 : colored(reference) == 0,
               label + ": CoinGL control is vacuous or disagrees with the specified visibility")) return false;
    gpuAction.apply(root); native->readbackRGBA(lastNative);
    if (!check(gpuAction.getLastStatus() == CoinRenderAction::SUCCESS &&
               lastNative.size() == reference.size() &&
               (visible ? colored(lastNative) >= 8 : colored(lastNative) == 0),
               label + ": GPU submission failed: " + gpuAction.getLastError().getString())) return false;
    ++comparisons;
    return !compareFullRaster ||
           (compare(lastCpu, label + "/CPU") && compare(lastNative, label + "/GPU"));
  }
  bool renderGpuOnly(SoNode * root, const std::string & label,
                     SoNode * referenceRoot = nullptr, bool compareRaster = true) {
    if (!check(gpu && native && referencePixels(referenceRoot ? referenceRoot : root, label),
               label + ": native/reference targets missing")) return false;
    gpuAction.apply(root); native->readbackRGBA(lastNative);
    if (!check(gpuAction.getLastStatus() == CoinRenderAction::SUCCESS &&
               lastNative.size() == reference.size() && colored(lastNative) >= 8 && colored(reference) >= 8,
               label + ": required GPU/control frame failed: " + gpuAction.getLastError().getString())) return false;
    return !compareRaster || compare(lastNative, label + "/GPU");
  }
  bool reject(SoNode * root, const std::string & label, CoinRenderAction::Status expected,
              bool gpuOnly = false) {
    std::vector<uint8_t> pixels;
    if (!gpuOnly) {
      const auto before = lastCpu; const uint64_t serial = cpu->getLastSubmissionSerial();
      const size_t submissions = observer->submissions;
      cpuAction.apply(root); cpu->readbackRGBA(pixels);
      if (!check(cpuAction.getLastStatus() == expected && cpuAction.getLastError().getLength() &&
                 observer->submissions == submissions && pixels == before &&
                 cpu->getLastSubmissionSerial() == serial, label + ": CPU failure did not preserve publication")) return false;
    }
    if (gpu) {
      const auto before = lastNative; const uint64_t serial = native->getLastSubmissionSerial();
      gpuAction.apply(root); native->readbackRGBA(pixels);
      if (!check(gpuAction.getLastStatus() == expected && gpuAction.getLastError().getLength() &&
                 pixels == before && native->getLastSubmissionSerial() == serial,
                 label + ": GPU failure did not preserve publication")) return false;
    }
    return true;
  }
  bool sample(int x, int y, const Color & expected, const std::string & label) {
    return pixel(lastCpu, x, y, expected, label + "/CPU") &&
      (!gpu || (pixel(reference, x, y, expected, label + "/CoinGL") &&
                pixel(lastNative, x, y, expected, label + "/GPU")));
  }
  bool captured(int function, float ref, const std::string & label) {
    return check(!observer->alpha.empty() && std::all_of(observer->alpha.begin(), observer->alpha.end(),
      [&](const AlphaRecord & state) { return state.function == uint32_t(function) && state.reference == ref; }),
      label + ": capture did not preserve the effective alpha policy");
  }
  bool gpu;
  CoinRenderAction::TransparencyType coinMode = CoinRenderAction::BLEND;
  int glChannels = 3;
  std::unique_ptr<CoinRenderTarget> cpu, native;
  CaptureBackend * observer = nullptr;
  CoinRenderAction cpuAction, gpuAction;
#if COIN_HAVE_LEGACY_GL_RENDERER
  SoOffscreenRenderer gl;
#endif
  std::vector<uint8_t> lastCpu, lastNative, reference;
  size_t comparisons = 0;
};

bool depthDisabled(bool gpu) {
  Scene scene; Harness test(gpu);
  Quad far(-1, SbColor(0, 0, 1)), near(1, SbColor(1, 0, 0)), middle(0, SbColor(0, 1, 0));
  near.depth->test = FALSE; near.depth->write = TRUE;
  scene.root->addChild(far.group); scene.root->addChild(near.group); scene.root->addChild(middle.group);
  if (!test.render(scene.root, "depth/off-write-on-three-quads") ||
      !test.sample(53, 37, {{0, 255, 0}}, "depth/off-write-on")) return false;
  std::vector<float> cpuDepth; test.cpu->readbackDepth(cpuDepth);
  if (!check(cpuDepth.size() == width * height, "depth: CPU depth readback missing")) return false;
  const auto preserved = cpuDepth;
  near.depth->write = FALSE;
  if (!test.render(scene.root, "depth/off-write-off-control") ||
      !test.sample(53, 37, {{0, 255, 0}}, "depth/off-write-off")) return false;
  test.cpu->readbackDepth(cpuDepth);
  if (!check(cpuDepth == preserved, "disabled depth test must make both write masks equivalent")) return false;
  near.depth->test = TRUE; near.depth->write = TRUE;
  return test.render(scene.root, "depth/on-write-on-control") &&
         test.sample(53, 37, {{255, 0, 0}}, "depth/on-write-on");
}

bool alphaFunctions(bool gpu) {
  Scene scene; Harness test(gpu); Quad content; scene.root->addChild(content.group);
  // CoinGL sends material alpha through SbColor::getPackedValue/glColor4ub:
  // material .5 becomes 128/255. This is the exact native equality boundary.
  const float reference = 128.f / 255.f;
  const int functions[] = {SoAlphaTest::NONE, SoAlphaTest::NEVER, SoAlphaTest::ALWAYS,
    SoAlphaTest::LESS, SoAlphaTest::LEQUAL, SoAlphaTest::EQUAL,
    SoAlphaTest::GEQUAL, SoAlphaTest::GREATER, SoAlphaTest::NOTEQUAL};
  const bool passes[][3] = {{true,true,true},{false,false,false},{true,true,true},
    {true,false,false},{true,true,false},{false,true,false},{false,true,true},
    {false,false,true},{true,false,true}};
  content.alpha->value = reference;
  for (int row = 0; row < 9; ++row) for (int column = 0; column < 3; ++column) {
    const float alpha = .25f * (column + 1); content.material->transparency = 1 - alpha;
    content.alpha->function = functions[row];
    const std::string label = "alpha/function-" + std::to_string(functions[row]) +
                              "/alpha-" + std::to_string(column + 1);
    if (!test.render(scene.root, label, passes[row][column]) ||
        !test.captured(functions[row], reference, label) ||
        !test.sample(53, 37, {{passes[row][column] ? int(std::lround(alpha * 255)) : 0, 0, 0}}, label)) return false;
  }
  content.material->transparency = .5f; content.alpha->value = .5f;
  for (int function : {SoAlphaTest::LEQUAL, SoAlphaTest::GREATER, SoAlphaTest::EQUAL}) {
    content.alpha->function = function;
    const bool pass = function == SoAlphaTest::GREATER;
    const std::string label = "alpha/packed-half-vs-float-half/function-" + std::to_string(function);
    if (!test.render(scene.root, label, pass) || !test.captured(function,.5f,label) ||
        !test.sample(53,37,{{pass ? 128 : 0,0,0}},label)) return false;
  }
  content.material->transparency = 0; content.alpha->function = SoAlphaTest::EQUAL; content.alpha->value = 1;
  if (!test.render(scene.root, "alpha/equal-one") || !test.captured(SoAlphaTest::EQUAL, 1, "equal-one")) return false;
  content.alpha->function = SoAlphaTest::GREATER; content.alpha->value = -1;
  if (!test.render(scene.root, "alpha/reference-clamped-zero") ||
      !test.captured(SoAlphaTest::GREATER, 0, "clamped-zero")) return false;
  content.alpha->function = SoAlphaTest::LESS; content.alpha->value = 2;
  return test.render(scene.root, "alpha/reference-clamped-one", false) &&
         test.captured(SoAlphaTest::LESS, 1, "clamped-one");
}

bool alphaTextureDepth(bool gpu) {
  Scene scene; Harness test(gpu);
  Quad far(-1, SbColor(0, 0, 1)), front(1), following(0, SbColor(0, 1, 0));
  front.alpha->function = SoAlphaTest::GREATER; front.alpha->value = .5f;
  front.texture(0, 4, {255, 0, 0, 0, 255, 0, 0, 255}, 2, 1);
  scene.root->addChild(far.group); scene.root->addChild(front.group); scene.root->addChild(following.group);
  if (!test.render(scene.root, "alpha/final-texture-discard-depth") ||
      !test.sample(31, 37, {{0, 255, 0}}, "alpha/discard-left") ||
      !test.sample(65, 37, {{255, 0, 0}}, "alpha/pass-right")) return false;
  front.material->transparency = .75f;
  if (!test.render(scene.root, "alpha/texture-replaces-low-material-alpha") ||
      !test.sample(65, 37, {{255, 0, 0}}, "alpha/texture-final")) return false;
  front.alpha->function = SoAlphaTest::NEVER;
  if (!test.render(scene.root, "alpha/discard-all-before-depth") ||
      !test.sample(31, 37, {{0, 255, 0}}, "alpha/never-left") ||
      !test.sample(65, 37, {{0, 255, 0}}, "alpha/never-right")) return false;
  front.alpha->function = SoAlphaTest::ALWAYS;
  return test.render(scene.root, "alpha/restore-pass") &&
         test.sample(65, 37, {{255, 0, 0}}, "alpha/restore-right");
}

bool screenDoorAlpha(bool gpu) {
  Scene scene; Harness test(gpu); Quad content; scene.root->addChild(content.group);
  content.material->transparency = .5f; test.transparency(CoinRenderAction::SCREEN_DOOR);
  if (!test.render(scene.root,"alpha/screen-door-none-visible-control")) return false;
  const auto cpuControl = test.lastCpu, gpuControl = test.lastNative;
  const size_t coverage = colored(cpuControl);
  if (!check(coverage > 100 && coverage < 3000,"screen-door control must contain real partial stipple coverage")) return false;
  const struct { int function; float reference; bool passes; } cases[] = {
    {SoAlphaTest::EQUAL,1,true}, {SoAlphaTest::GREATER,.75f,true},
    {SoAlphaTest::LEQUAL,.5f,false}, {SoAlphaTest::EQUAL,128.f/255.f,false}
  };
  for (const auto & policy : cases) {
    content.alpha->function = policy.function; content.alpha->value = policy.reference;
    const std::string label = "alpha/screen-door-function-" + std::to_string(policy.function) +
                              "/reference-" + std::to_string(policy.reference);
    if (!test.render(scene.root,label,policy.passes) ||
        !test.captured(policy.function,policy.reference,label)) return false;
    if (policy.passes && !check(test.lastCpu == cpuControl && (!gpu || test.lastNative == gpuControl),
                               label + ": passing alpha test changed native screen-door coverage")) return false;
  }
  content.texture(0,4,{255,0,0,64});
  content.alpha->function = SoAlphaTest::GREATER; content.alpha->value = .75f;
  if (!test.render(scene.root,"alpha/screen-door-final-texture-discard",false)) return false;
  content.alpha->function = SoAlphaTest::LESS; content.alpha->value = .5f;
  return test.render(scene.root,"alpha/screen-door-final-texture-pass");
}

bool tinyTransparencyDepth(bool gpu) {
  for (bool authoredCombine : {false,true}) {
    Scene scene; Harness test(gpu); Quad content;
    // No SoDepthBuffer field may explicitly authorize writes in the deferred pass.
    content.group->removeChild(content.depth); content.depth = nullptr;
    content.material->transparency = .0001f;
    content.alpha->function = SoAlphaTest::GREATER; content.alpha->value = .5f;
    auto * activeTexture = content.texture(0,4,{255,0,0,255});
    if (authoredCombine) {
      auto * combine = new SoTextureCombine;
      combine->rgbOperation = SoTextureCombine::REPLACE; combine->rgbSource.set1Value(0,SoTextureCombine::TEXTURE);
      combine->alphaOperation = SoTextureCombine::REPLACE; combine->alphaSource.set1Value(0,SoTextureCombine::TEXTURE);
      content.textures->insertChild(combine,content.textures->findChild(activeTexture));
    }
    scene.root->addChild(content.group); test.transparency(CoinRenderAction::SORTED_OBJECT_BLEND);
    const std::string label = authoredCombine ? "alpha/tiny-transparency-authored-combine"
                                             : "alpha/tiny-transparency-opaque-replace";
    if (!test.render(scene.root,label) || !test.sample(53,37,{{255,0,0}},label)) return false;
    std::vector<float> depth; test.cpu->readbackDepth(depth);
    if (!check(depth.size() == width * height &&
               std::all_of(depth.begin(),depth.end(),[](float value) { return value == 1.0f; }),
               label + ": CPU lost native deferred classification after alpha rounded to one")) return false;
    if (gpu) {
      test.native->readbackDepth(depth);
      if (!check(depth.size() == width * height &&
                 std::all_of(depth.begin(),depth.end(),[](float value) { return value == 1.0f; }),
                 label + ": GPU wrote depth despite native tiny material transparency")) return false;
    }
  }
  return true;
}

bool inheritanceAndMutation(bool gpu) {
  Scene scene; Harness test(gpu);
  auto * inherited = new SoAlphaTest; inherited->function = SoAlphaTest::NEVER; scene.root->addChild(inherited);
  Quad left(0, SbColor(1, 0, 0), -.9f, -.1f), right(0, SbColor(0, 1, 0), .1f, .9f);
  left.alpha->function = SoAlphaTest::ALWAYS;
  right.group->removeChild(right.alpha); right.alpha = nullptr;
  scene.root->addChild(left.group); scene.root->addChild(right.group);
  if (!test.render(scene.root, "alpha/separator-restores-inherited-never") ||
      !test.sample(26, 37, {{255, 0, 0}}, "inheritance/local-always") ||
      !test.sample(69, 37, {{0, 0, 0}}, "inheritance/sibling-never")) return false;
  const auto first = test.lastCpu; const uint64_t revision = test.observer->revision;
  if (!test.render(scene.root, "alpha/repeated-identical-frame") ||
      !check(test.lastCpu == first && test.observer->revision == revision,
             "unchanged alpha scene must reuse an identical payload")) return false;
  inherited->function = SoAlphaTest::ALWAYS;
  if (!test.render(scene.root, "alpha/mutate-inherited-policy") ||
      !test.sample(69, 37, {{0, 255, 0}}, "inheritance/mutation") ||
      !check(test.lastCpu != first, "alpha policy mutation retained stale output")) return false;
  inherited->function = SoAlphaTest::GREATER; inherited->value = .5f; right.material->transparency = .75f;
  if (!test.render(scene.root, "alpha/mutate-material-alpha") ||
      !test.sample(69, 37, {{0, 0, 0}}, "inheritance/low-alpha")) return false;
  inherited->value = .125f;
  if (!test.render(scene.root, "alpha/mutate-reference") ||
      !test.sample(69, 37, {{0, 64, 0}}, "inheritance/new-reference")) return false;
  inherited->function = SoAlphaTest::NEVER; right.material->transparency = 0;
  return test.render(scene.root, "alpha/restore-original-policy") &&
         check(test.lastCpu == first, "A/B/A alpha mutation did not restore exact pixels");
}

bool replaceComponents(bool gpu) {
  const std::vector<std::vector<uint8_t>> texels = {{160}, {160,192}, {230,40,100}, {230,40,100,192}};
  const Color expected[] = {{{80,80,80}}, {{120,120,120}}, {{115,20,50}}, {{173,30,75}}};
  for (int components = 1; components <= 4; ++components) {
    Scene scene; Harness test(gpu); Quad content; content.material->transparency = .5f;
    content.texture(0, components, texels[components - 1]); scene.root->addChild(content.group);
    const std::string label = "replace/components-" + std::to_string(components);
    if (!test.render(scene.root, label) || !test.sample(53, 37, expected[components - 1], label)) return false;
    content.alpha->function = SoAlphaTest::GREATER; content.alpha->value = .625f;
    const bool alphaImage = components == 2 || components == 4;
    if (!test.render(scene.root, label + "/alpha-greater", alphaImage)) return false;
    content.alpha->function = SoAlphaTest::LESS;
    if (!test.render(scene.root, label + "/alpha-less", !alphaImage)) return false;
  }
  return true;
}

bool replaceUnusedMaterialTransparency(bool gpu) {
  Scene scene; Harness test(gpu); Quad textured,cover(0,SbColor(0,1,0));
  test.transparency(CoinRenderAction::SORTED_OBJECT_BLEND);
  auto * binding = new SoMaterialBinding; binding->value = SoMaterialBinding::OVERALL;
  textured.group->insertChild(binding,textured.group->findChild(textured.alpha));
  const float opaque[] = {0,0}; textured.material->transparency.setValues(0,2,opaque);
  textured.alpha->function = SoAlphaTest::NONE;
  textured.texture(0,3,{255,0,0});
  cover.depth->test = FALSE; cover.depth->write = FALSE;
  scene.root->addChild(textured.group); scene.root->addChild(cover.group);
  if (!test.render(scene.root,"replace/overall-all-opaque-slots-immediate") ||
      !test.sample(53,37,{{0,255,0}},"replace/opaque-slots-covered")) return false;
  const auto originalCpu = test.lastCpu,originalGpu = test.lastNative;
  // Native material classification considers every transparency slot even
  // when OVERALL uses opaque slot zero. RGB REPLACE keeps that slot's alpha,
  // but the shape is still deferred and is drawn after the green cover.
  textured.material->transparency.set1Value(1,.5f);
  if (!test.render(scene.root,"replace/overall-unused-transparent-slot-deferred") ||
      !test.sample(53,37,{{255,0,0}},"replace/unused-slot-native-order")) return false;
  const auto deferredCpu = test.lastCpu,deferredGpu = test.lastNative;
  const uint64_t revision = test.observer->revision;
  if (!test.render(scene.root,"replace/overall-unused-transparent-slot-repeat") ||
      !check(test.lastCpu == deferredCpu && test.observer->revision == revision &&
             (!gpu || test.lastNative == deferredGpu),"RGB REPLACE repeated frame retained a different order")) return false;
  textured.alpha->function = SoAlphaTest::ALWAYS;
  if (!test.render(scene.root,"replace/overall-unused-transparent-slot-alpha-always") ||
      !test.sample(53,37,{{255,0,0}},"replace/always-preserves-native-order")) return false;
  textured.alpha->function = SoAlphaTest::NONE;
  textured.material->transparency.set1Value(0,.5f);
  if (!test.render(scene.root,"replace/overall-used-transparent-slot-alpha-preserved") ||
      !test.sample(53,37,{{128,127,0}},"replace/used-slot-blends-over-cover")) return false;
  textured.material->transparency.setValues(0,2,opaque);
  return test.render(scene.root,"replace/overall-unused-transparent-slot-restore") &&
         test.sample(53,37,{{0,255,0}},"replace/opaque-slots-restored") &&
         check(test.lastCpu == originalCpu && (!gpu || test.lastNative == originalGpu),
               "RGB REPLACE transparency-array A/B/A did not restore the immediate order");
}

bool multipleUnitsAndCombine(bool gpu) {
  Scene scene; Harness test(gpu); Quad content; content.material->transparency = .5f;
  auto * first = content.texture(0, 2, {255,192});
  auto * second = content.texture(1, 3, {230,40,100}); scene.root->addChild(content.group);
  if (!test.render(scene.root, "replace/unit-one-rgb-preserves-unit-zero-alpha") ||
      !test.sample(53, 37, {{173,30,75}}, "replace/previous-alpha")) return false;
  const auto previous = test.lastCpu;
  const uint8_t l[] = {255}; first->image.setValue(SbVec2s(1,1), 1, l);
  if (!test.render(scene.root, "replace/two-no-alpha-units-preserve-material") ||
      !test.sample(53, 37, {{115,20,50}}, "replace/material-alpha")) return false;
  const uint8_t la[] = {255,192}; first->image.setValue(SbVec2s(1,1), 2, la);
  if (!test.render(scene.root, "replace/components-restored") ||
      !check(test.lastCpu == previous, "component mutation A/B/A retained stale alpha semantics")) return false;
  // Explicit TEXTURE alpha is 1 for RGB, whereas ordinary RGB REPLACE
  // preserves the previous unit's alpha. Their RGB outputs differ.
  auto * program = new SoTextureCombine;
  program->rgbOperation = SoTextureCombine::REPLACE; program->rgbSource.set1Value(0, SoTextureCombine::TEXTURE);
  program->alphaOperation = SoTextureCombine::REPLACE; program->alphaSource.set1Value(0, SoTextureCombine::TEXTURE);
  content.textures->insertChild(program,content.textures->findChild(second));
  if (!test.render(scene.root, "replace/explicit-combine-overrides-rgb-default") ||
      !test.sample(53, 37, {{230,40,100}}, "combine/texture-alpha-one")) return false;
  const uint8_t rgba[] = {230,40,100,64}; second->image.setValue(SbVec2s(1,1), 4, rgba);
  program->alphaSource.set1Value(0, SoTextureCombine::PREVIOUS);
  if (!test.render(scene.root, "replace/explicit-previous-overrides-rgba-alpha") ||
      !test.sample(53, 37, {{173,30,75}}, "combine/previous-alpha")) return false;
  program->constantColor = SbVec4f(1,1,1,.25f);
  program->alphaSource.set1Value(0, SoTextureCombine::CONSTANT);
  return test.render(scene.root, "replace/explicit-constant-alpha") &&
         test.sample(53, 37, {{58,10,25}}, "combine/constant-alpha");
}

struct AlphaHook {
  bool prune = true;
  int preCalls = 0, postCalls = 0;
  bool postSawMutation = true;
};
SoCallbackAction::Response alphaPre(void * data, SoCallbackAction *, const SoNode * node) {
  auto & hook = *static_cast<AlphaHook *>(data); ++hook.preCalls;
  if (hook.prune) return SoCallbackAction::PRUNE;
  auto * alpha = const_cast<SoAlphaTest *>(static_cast<const SoAlphaTest *>(node));
  alpha->function = SoAlphaTest::ALWAYS; alpha->value = .25f;
  return SoCallbackAction::CONTINUE;
}
SoCallbackAction::Response alphaPost(void * data, SoCallbackAction * action, const SoNode *) {
  auto & hook = *static_cast<AlphaHook *>(data); ++hook.postCalls;
  float reference = 0;
  const int function = SoLazyElement::getAlphaTest(action->getState(), reference);
  if (!hook.prune) hook.postSawMutation &= function != 0 && reference == .25f;
  return SoCallbackAction::CONTINUE;
}
bool alphaCallbacks(bool gpu) {
  Scene scene; AlphaHook hook; Harness test(gpu); Quad content;
  content.alpha->function = SoAlphaTest::NEVER; scene.root->addChild(content.group);
  test.cpuAction.addPreCallback(SoAlphaTest::getClassTypeId(), alphaPre, &hook);
  test.gpuAction.addPreCallback(SoAlphaTest::getClassTypeId(), alphaPre, &hook);
  test.cpuAction.addPostCallback(SoAlphaTest::getClassTypeId(), alphaPost, &hook);
  test.gpuAction.addPostCallback(SoAlphaTest::getClassTypeId(), alphaPost, &hook);
  // The GL reference removes precisely the pruned node; it does not invoke
  // CoinRender callbacks and must still render the unpruned geometry.
  auto * reference = static_cast<SoSeparator *>(scene.root->copy(TRUE)); reference->ref();
  auto * group = static_cast<SoSeparator *>(reference->getChild(scene.root->findChild(content.group)));
  group->removeChild(2);
  const bool pruned = test.render(scene.root, "alpha/pre-callback-prune", true, reference) &&
                     test.captured(SoAlphaTest::NONE, .5f, "callback/prune-inert");
  reference->unref();
  if (!pruned) return false;
  hook.prune = false;
  if (!test.render(scene.root, "alpha/pre-callback-mutates-current-capture") ||
      !test.captured(SoAlphaTest::ALWAYS, .25f, "callback/mutation") ||
      !check(hook.postCalls > 0 && hook.postSawMutation,
             "post callback must observe the alpha policy updated by the pre callback")) return false;
  hook.prune = true; content.alpha->function = SoAlphaTest::NEVER; content.alpha->value = .5f;
  reference = static_cast<SoSeparator *>(scene.root->copy(TRUE)); reference->ref();
  group = static_cast<SoSeparator *>(reference->getChild(scene.root->findChild(content.group)));
  group->removeChild(2);
  const bool restored = test.render(scene.root, "alpha/pre-callback-prune-restored", true, reference) &&
                        test.captured(SoAlphaTest::NONE, .5f, "callback/prune-restored");
  reference->unref();
  return restored && check(hook.preCalls >= (gpu ? 6 : 3), "alpha pre callbacks were not executed");
}

bool lineAndPointPolicies(bool gpu) {
  for (bool point : {false, true}) {
    Scene scene; Harness test(gpu);
    auto * material = new SoMaterial; material->diffuseColor.setValue(1,0,0); scene.root->addChild(material);
    auto * alpha = new SoAlphaTest; scene.root->addChild(alpha);
    auto * style = new SoDrawStyle; style->lineWidth = 3; style->pointSize = 9; scene.root->addChild(style);
    auto * coordinates = new SoCoordinate3;
    if (point) coordinates->point.setValue(0,0,0);
    else { const SbVec3f points[] = {{-.7f,0,0},{.7f,0,0}}; coordinates->point.setValues(0,2,points); }
    scene.root->addChild(coordinates);
    if (point) { auto * points = new SoPointSet; points->numPoints = 1; scene.root->addChild(points); }
    else { auto * line = new SoLineSet; line->numVertices = 2; scene.root->addChild(line); }
    const std::string label = point ? "alpha/points" : "alpha/lines";
    // Interior samples qualify fragment policy. Raster edge coverage belongs
    // to DrawStyle's dedicated oracle and differs between CPU line algorithms.
    if (!test.render(scene.root, label + "/visible-none", true, nullptr, false) ||
        !test.sample(48,39,{{255,0,0}},label + "/control")) return false;
    alpha->function = SoAlphaTest::NEVER;
    if (!test.render(scene.root, label + "/never", false) ||
        !test.captured(SoAlphaTest::NEVER,.5f,label + "/never")) return false;
    alpha->function = SoAlphaTest::NONE;
    if (!test.render(scene.root,label + "/restored-none",true,nullptr,false) ||
        !test.sample(48,39,{{255,0,0}},label + "/restore")) return false;
  }
  return true;
}

bool rasterPolicies(bool gpu, const char * fontName) {
  for (bool textNode : {false, true}) {
    Scene scene; Harness test(gpu); scene.camera->height = float(height);
    auto * material = new SoMaterial; material->diffuseColor.setValue(1,0,0); scene.root->addChild(material);
    auto * alpha = new SoAlphaTest; scene.root->addChild(alpha);
    SoFont * font = nullptr;
    if (textNode) {
      font = new SoFont; font->name = "defaultFont"; font->size = 18; scene.root->addChild(font);
      auto * text = new SoText2; text->string = "AV jg"; text->justification = SoText2::CENTER;
      scene.root->addChild(text);
    } else {
      auto * image = new SoImage; image->horAlignment = SoImage::CENTER; image->vertAlignment = SoImage::HALF;
      std::vector<uint8_t> pixels(24 * 12 * 4);
      for (int y = 0; y < 12; ++y) for (int x = 0; x < 24; ++x) {
        const size_t i = size_t(y * 24 + x) * 4;
        pixels[i] = 255; pixels[i+1] = pixels[i+2] = 0; pixels[i+3] = x < 12 ? 64 : 192;
      }
      image->image.setValue(SbVec2s(24,12), 4, pixels.data()); scene.root->addChild(image);
    }
    const std::string label = textNode ? "raster/mono" : "raster/image";
    alpha->function = SoAlphaTest::ALWAYS;
    if (!test.render(scene.root, label + "/visible-control") ||
        !test.captured(SoAlphaTest::ALWAYS, .5f, label)) return false;
    alpha->function = SoAlphaTest::NEVER;
    if (!test.render(scene.root, label + "/inherited-never", false) ||
        !test.captured(SoAlphaTest::NEVER, .5f, label)) return false;
    alpha->function = SoAlphaTest::GREATER; alpha->value = .5f;
    if (!test.render(scene.root, label + "/inherited-greater") ||
        !test.captured(SoAlphaTest::GREATER, .5f, label)) return false;
    if (!textNode) {
      if (!test.sample(41, 38, {{0,0,0}}, "image/discard-alpha-quarter") ||
          !test.sample(55, 38, {{192,0,0}}, "image/pass-alpha-three-quarters")) return false;
      continue;
    }
    font->name = fontName; material->transparency = .25f; alpha->function = SoAlphaTest::NEVER;
    if (!test.render(scene.root, "raster/gray-own-alpha-policy") ||
        !test.captured(SoAlphaTest::GREATER, .3f, "gray/own-greater")) return false;
    const auto highOpacity = test.lastCpu;
    alpha->function = SoAlphaTest::GREATER; alpha->value = 1;
    if (!test.render(scene.root, "raster/gray-overrides-inherited-greater-one") ||
        !check(test.lastCpu == highOpacity, "gray text must retain its own alpha test")) return false;
    material->transparency = .75f;
    if (!test.render(scene.root, "raster/gray-own-threshold-discards-low-opacity",
                     false, nullptr, true, true)) return false;
    material->transparency = .25f;
    if (!test.render(scene.root, "raster/gray-restore") ||
        !check(test.lastCpu == highOpacity, "gray alpha mutation did not restore exact output")) return false;
  }
  return true;
}

bool invalidAlphaRecovery(bool gpu) {
  Scene scene; Harness test(gpu); Quad content; scene.root->addChild(content.group);
  content.alpha->function = SoAlphaTest::ALWAYS;
  if (!test.render(scene.root,"alpha/invalid-reference-visible-anchor")) return false;
  const auto cpuBefore = test.lastCpu, gpuBefore = test.lastNative;
  content.alpha->function = SoAlphaTest::GREATER;
  content.alpha->value = std::numeric_limits<float>::quiet_NaN();
  if (!test.reject(scene.root,"alpha/nonfinite-reference",CoinRenderAction::INVALID_SCENE)) return false;
  content.alpha->function = SoAlphaTest::ALWAYS; content.alpha->value = .5f;
  return test.render(scene.root,"alpha/nonfinite-reference-repair") &&
         check(test.lastCpu == cpuBefore && (!gpu || test.lastNative == gpuBefore),
               "repair after invalid alpha reference must restore exact published pixels");
}

bool layeredAlpha(bool gpu, bool weighted) {
  if (weighted && !gpu) {
    std::cout << "alpha/weighted profile requires --gpu; CPU capture does not claim OIT execution\n";
    return true;
  }
  CoinRenderOptions options;
  options.transparency = weighted ? COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT : COIN_RENDER_TRANSPARENCY_PEELING;
  Harness test(gpu,options); Scene scene;
  test.transparency(weighted ? CoinRenderAction::SORTED_OBJECT_BLEND : CoinRenderAction::SORTED_LAYERS_BLEND);
  Quad background(-1,SbColor(0,0,1)), front(1), middle(0,SbColor(0,1,0));
  // The layer renderer manages its scratch depth and final transparent mask.
  // An explicit write=FALSE also disables CoinGL's temporary peel-layer depth.
  front.group->removeChild(front.depth); front.depth = nullptr;
  middle.group->removeChild(middle.depth); middle.depth = nullptr;
  front.material->transparency = .75f;
  front.alpha->function = SoAlphaTest::GREATER; front.alpha->value = .125f;
  middle.material->transparency = .5f;
  auto * frontSwitch = new SoSwitch; frontSwitch->whichChild = SO_SWITCH_ALL; frontSwitch->addChild(front.group);
  scene.root->addChild(background.group); scene.root->addChild(frontSwitch);
  const std::string label = weighted ? "alpha/weighted" : "alpha/peeling";
  const auto render = [&](const std::string & phase) {
    return weighted ? test.renderGpuOnly(scene.root,label + "/" + phase)
                    : test.render(scene.root,label + "/" + phase);
  };
  // A single surviving transparent contributor has the same source-over RGB
  // under weighted OIT and CoinGL. No multi-layer OIT/ordered equivalence is assumed.
  if (!render("visible-front-control")) return false;
  if (weighted) {
    if (!pixel(test.lastNative,53,37,{{64,0,191}},label + "/front")) return false;
  } else if (!test.sample(53,37,{{64,0,191}},label + "/front")) return false;
  scene.root->addChild(middle.group); frontSwitch->whichChild = SO_SWITCH_NONE;
  if (!render("counterfactual-no-front")) return false;
  const auto cpuControl = test.lastCpu, gpuControl = test.lastNative;
  if (weighted) {
    if (!pixel(test.lastNative,53,37,{{0,128,127}},label + "/counterfactual")) return false;
  } else if (!test.sample(53,37,{{0,128,127}},label + "/counterfactual")) return false;
  frontSwitch->whichChild = SO_SWITCH_ALL;
  for (int function : {SoAlphaTest::NEVER, SoAlphaTest::GREATER}) {
    front.alpha->function = function; front.alpha->value = .5f;
    if (!render("discard-front-function-" + std::to_string(function)) ||
        !check((weighted || test.lastCpu == cpuControl) && (!gpu || test.lastNative == gpuControl),
               label + ": discarded front must equal the counterfactual without front")) return false;
  }
  scene.root->removeChild(middle.group);
  front.alpha->value = .125f;
  return render("restore-visible-front");
}

bool rttProducerAlpha(bool gpu) {
  if (!gpu) {
    std::cout << "alpha/RTT execution requires --gpu; temporary producer targets are not CPU mocks\n";
    return true;
  }
  for (auto mode : {COIN_RENDER_SCENE_TEXTURE_STAGED, COIN_RENDER_SCENE_TEXTURE_DIRECT}) {
    Scene producer, consumer; Quad source(0), surface(0,SbColor(1,1,1));
    // Fill the producer viewport with constant texels. RTT edge resampling
    // has a separate oracle; these cases isolate the producer's alpha policy.
    const SbVec3f fill[] = {{-2,-2,0},{2,-2,0},{2,2,0},{-2,2,0}};
    source.coordinates->point.setValues(0,4,fill);
    source.material->transparency = .75f;
    source.alpha->function = SoAlphaTest::GREATER; source.alpha->value = .5f;
    auto * sourceTexture = source.texture(0,4,{255,0,0,192});
    // Both producer render actions execute the same explicit blending policy.
    auto * blend = new SoTransparencyType; blend->value = SoTransparencyType::BLEND;
    producer.root->addChild(blend); producer.root->addChild(source.group);
    auto * sceneTexture = new SoSceneTexture2; sceneTexture->scene = producer.root;
    sceneTexture->size.setValue(32,32); sceneTexture->type = SoSceneTexture2::RGBA8;
    sceneTexture->model = SoSceneTexture2::MODULATE;
    sceneTexture->wrapS = SoSceneTexture2::CLAMP; sceneTexture->wrapT = SoSceneTexture2::CLAMP;
    sceneTexture->transparencyFunction = SoSceneTexture2::NONE;
    sceneTexture->backgroundColor.setValue(0,0,1,1); surface.textures->addChild(sceneTexture);
    auto * uv = new SoTextureCoordinate2;
    // Sample the constant interior. Sampler border parity belongs to the
    // texture/RTT gate and is not certified by this producer AlphaTest oracle.
    const SbVec2f coords[] = {{.25f,.25f},{.75f,.25f},{.75f,.75f},{.25f,.75f}};
    uv->point.setValues(0,4,coords);
    surface.textures->addChild(uv); consumer.root->addChild(surface.group);
    CoinRenderOptions options; options.sceneTexture = mode; Harness test(true,options);
    const std::string label = mode == COIN_RENDER_SCENE_TEXTURE_DIRECT ? "alpha/RTT-direct" : "alpha/RTT-staged";
    if (!test.renderGpuOnly(consumer.root,label + "/rgba-replaces-material-alpha") ||
        !pixel(test.lastNative,53,37,{{192,0,63}},label + "/texture-alpha")) return false;
    const auto visible = test.lastNative;
    const uint8_t rgb[] = {255,0,0}; sourceTexture->image.setValue(SbVec2s(1,1),3,rgb);
    if (!test.renderGpuOnly(consumer.root,label + "/rgb-preserves-low-material-alpha") ||
        !pixel(test.lastNative,53,37,{{0,0,255}},label + "/discarded-producer")) return false;
    const auto discarded = test.lastNative;
    const uint8_t rgba[] = {255,0,0,192}; sourceTexture->image.setValue(SbVec2s(1,1),4,rgba);
    source.alpha->function = SoAlphaTest::NEVER;
    if (!test.renderGpuOnly(consumer.root,label + "/never") ||
        !check(test.lastNative == discarded,label + ": NEVER must leave the producer background")) return false;
    source.alpha->function = SoAlphaTest::GREATER;
    if (!test.renderGpuOnly(consumer.root,label + "/repair") ||
        !check(test.lastNative == visible,label + ": producer alpha mutation did not restore exact output")) return false;
  }
  return true;
}

bool shadowAlphaRejection(bool gpu) {
  Scene anchor; Harness test(gpu); Quad visible; anchor.root->addChild(visible.group);
  if (!test.render(anchor.root,"alpha/shadow-rejection-published-anchor")) return false;
  Scene scene;
  auto * model = new SoLightModel; model->model = SoLightModel::PHONG; scene.root->replaceChild(1,model);
  auto * shadow = new SoShadowGroup; shadow->isActive = TRUE; shadow->precision = .0625f;
  auto * light = new SoShadowDirectionalLight; light->direction.setValue(0,0,-1); shadow->addChild(light);
  auto * material = new SoMaterial; material->diffuseColor.setValue(1,0,0); shadow->addChild(material);
  auto * alpha = new SoAlphaTest; shadow->addChild(alpha);
  auto * cube = new SoCube; cube->width = 1; cube->height = 1; cube->depth = 1; shadow->addChild(cube);
  scene.root->addChild(shadow);
  // A real one-map PHONG control qualifies the shadow scene before active
  // alpha rejection. Its lighting/map quality is not claimed pixel-identical.
  std::vector<uint8_t> shadowControl;
  if (gpu) {
    if (!test.renderGpuOnly(scene.root,"alpha/shadow-none-control",nullptr,false)) return false;
    shadowControl = test.lastNative;
  }
  for (int function : {SoAlphaTest::NEVER,SoAlphaTest::GREATER}) {
    alpha->function = function; alpha->value = .5f;
    if (!test.reject(scene.root,"alpha/active-shadow-function-" + std::to_string(function),
                     CoinRenderAction::UNSUPPORTED)) return false;
  }
  if (gpu) {
    alpha->function = SoAlphaTest::ALWAYS;
    if (!test.renderGpuOnly(scene.root,"alpha/shadow-always-admitted",nullptr,false) ||
        !check(test.lastNative == shadowControl,"inert ALWAYS changed the qualified shadow result")) return false;
    alpha->function = SoAlphaTest::NONE;
    if (!test.renderGpuOnly(scene.root,"alpha/shadow-none-repair",nullptr,false) ||
        !check(test.lastNative == shadowControl,"shadow repair did not restore exact output")) return false;
  }
  return test.render(anchor.root,"alpha/shadow-rejection-return-to-anchor");
}
} // namespace

int main(int argc, char ** argv) {
  bool gpu = false; const char * font = "DejaVu Sans";
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--gpu") gpu = true;
    else if (argument == "--capture") gpu = false;
    else if (argument == "--font" && i + 1 < argc) font = argv[++i];
    else { std::cerr << "Usage: CoinRenderFragmentPolicyTest --capture|--gpu [--font NAME]\n"; return 2; }
  }
  SoDB::init(); CoinRenderAction::initClass();
  if (gpu && !CoinRenderAction::isGpuBackendAvailable()) {
    std::cerr << "CoinRenderFragmentPolicyTest requires the selected GPU backend\n"; return 77;
  }
#if !COIN_HAVE_LEGACY_GL_RENDERER
  if (gpu) {
    std::cerr << "CoinRenderFragmentPolicyTest mandatory CoinGL oracle was not compiled\n"; return 1;
  }
#endif
  if (!depthDisabled(gpu) || !alphaFunctions(gpu) || !alphaTextureDepth(gpu) ||
      !screenDoorAlpha(gpu) || !tinyTransparencyDepth(gpu) ||
      !inheritanceAndMutation(gpu) || !replaceComponents(gpu) || !replaceUnusedMaterialTransparency(gpu) ||
      !multipleUnitsAndCombine(gpu) || !alphaCallbacks(gpu) ||
      !lineAndPointPolicies(gpu) || !rasterPolicies(gpu, font) ||
      !invalidAlphaRecovery(gpu) || !layeredAlpha(gpu,false) || !layeredAlpha(gpu,true) ||
      !rttProducerAlpha(gpu) || !shadowAlphaRejection(gpu)) return 1;
  std::cout << "CoinRenderFragmentPolicyTest " << (gpu ? "GPU/CoinGL" : "capture/CPU") << " passed\n";
  return 0;
}
