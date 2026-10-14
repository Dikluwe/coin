#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#if COIN_HAVE_LEGACY_GL_RENDERER
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/system/gl.h>
#endif
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/elements/SoMultiTextureCoordinateElement.h>
#include <Inventor/elements/SoTextureUnitElement.h>
#include <Inventor/fields/SoMFVec4f.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureCoordinate3.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoShaderProgram.h>
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
#include <utility>
#include <vector>

// Coin has no public TextureCoordinate4 node. This fixture exercises its
// public coordinate element, whose native GL path sends all four components.
class ProjectiveCoordinates : public SoNode {
  SO_NODE_HEADER(ProjectiveCoordinates);
public:
  ProjectiveCoordinates() {
    SO_NODE_CONSTRUCTOR(ProjectiveCoordinates);
    SO_NODE_ADD_FIELD(point, (SbVec4f(0, 0, 0, 1)));
  }
  static void initClass() { SO_NODE_INIT_CLASS(ProjectiveCoordinates, SoNode, "Node"); }
  SoMFVec4f point;
  static int fixedFunctionTextureUnits;
  void doAction(SoAction * action) override {
    SoMultiTextureCoordinateElement::set4(action->getState(), this,
      SoTextureUnitElement::get(action->getState()), point.getNum(), point.getValues(0));
  }
  void callback(SoCallbackAction * action) override { doAction(action); }
  void GLRender(SoGLRenderAction * action) override {
#if COIN_HAVE_LEGACY_GL_RENDERER
    GLint units = 0; glGetIntegerv(GL_MAX_TEXTURE_UNITS, &units);
    fixedFunctionTextureUnits = units;
#endif
    doAction(action);
  }
protected:
  ~ProjectiveCoordinates() override = default;
};
SO_NODE_SOURCE(ProjectiveCoordinates);
int ProjectiveCoordinates::fixedFunctionTextureUnits = 0;

namespace {
constexpr int width = 96, height = 80, textureSize = 16;
size_t nativeGlComparisons = 0, projectedGlComparisons = 0;
size_t equivalentOneLayerGlComparisons = 0;
size_t analyticCpuComparisons = 0, analyticGpuComparisons = 0;
using Color = std::array<int, 3>;
bool check(bool condition, const std::string & message) {
  if (!condition) std::cerr << "CoinRenderProjectiveUvTest: " << message << '\n';
  return condition;
}
class CaptureBackend : public CoinRenderCpuReferenceBackend {
public:
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target) override {
    ++submissions; revision = frame.revision; draws = frame.draws.size();
    return CoinRenderCpuReferenceBackend::submit(frame, target);
  }
  size_t submissions = 0, draws = 0;
  uint64_t revision = 0;
};

SbMatrix projective(float variation = 0) {
  // SbMatrix uses row vectors. R contributes to all three sampled components.
  SbMatrix result = SbMatrix::identity();
  result[0][0] = .7f; result[0][1] = 0; result[0][3] = .9f + variation;
  result[1][0] = 0; result[1][1] = .65f; result[1][3] = .25f;
  result[2][0] = .15f; result[2][1] = .12f; result[2][3] = .4f;
  result[3][0] = .12f; result[3][1] = .14f; result[3][3] = .8f;
  return result;
}
std::vector<uint8_t> gradient(bool bright = false, int alpha = -1) {
  const int components = alpha < 0 ? 3 : 4;
  std::vector<uint8_t> image(textureSize * textureSize * components);
  for (int y = 0; y < textureSize; ++y) for (int x = 0; x < textureSize; ++x) {
    const size_t offset = size_t(y * textureSize + x) * components;
    image[offset] = bright ? 176 + 4 * x : 32 + 12 * x;
    image[offset + 1] = bright ? 180 + 4 * y : 40 + 10 * y;
    image[offset + 2] = bright ? 192 + x + y : 96 + 4 * x + 3 * y;
    if (components == 4) image[offset + 3] = uint8_t(alpha);
  }
  return image;
}
Color gradientColor(float s, float t, float opacity = 1) {
  const float x = s * textureSize - .5f, y = t * textureSize - .5f;
  return {{int(std::lround((32 + 12 * x) * opacity)),
           int(std::lround((40 + 10 * y) * opacity)),
           int(std::lround((96 + 4 * x + 3 * y) * opacity))}};
}
struct TextureStage {
  SoTextureCoordinate2 * uv2 = nullptr;
  SoTextureCoordinate3 * uv3 = nullptr;
  ProjectiveCoordinates * uv4 = nullptr;
  SoTextureMatrixTransform * matrix = nullptr;
  SoTexture2 * image = nullptr;
};
struct Surface {
  Surface() {
    group = new SoSeparator;
    depth = new SoDepthBuffer; depth->test = TRUE; depth->write = TRUE;
    depth->function = SoDepthBuffer::LEQUAL; group->addChild(depth);
    material = new SoMaterial; material->diffuseColor.setValue(1, 1, 1); group->addChild(material);
    alpha = new SoAlphaTest; group->addChild(alpha);
    style = new SoDrawStyle; style->lineWidth = 1; style->pointSize = 5; group->addChild(style);
    textures = new SoGroup; group->addChild(textures);
    coordinates = new SoCoordinate3;
    const SbVec3f positions[] = {{-.75f, -.75f, 0}, {.75f, -.75f, 0},
                                 {.75f, .75f, 0}, {-.75f, .75f, 0}};
    coordinates->point.setValues(0, 4, positions); group->addChild(coordinates);
    geometry = new SoGroup; group->addChild(geometry); shape(0);
  }
  TextureStage stage(int unit, int dimension = 2, bool bright = false) {
    auto * selected = new SoTextureUnit; selected->unit = unit; textures->addChild(selected);
    TextureStage result;
    if (dimension == 2) {
      result.uv2 = new SoTextureCoordinate2;
      const SbVec2f uv[] = {{.1f, .1f}, {.9f, .1f}, {.9f, .9f}, {.1f, .9f}};
      result.uv2->point.setValues(0, 4, uv); textures->addChild(result.uv2);
    } else if (dimension == 3) {
      result.uv3 = new SoTextureCoordinate3;
      const SbVec3f uv[] = {{.1f, .1f, .05f}, {.9f, .1f, .75f},
                             {.9f, .9f, .75f}, {.1f, .9f, .05f}};
      result.uv3->point.setValues(0, 4, uv); textures->addChild(result.uv3);
    } else {
      result.uv4 = new ProjectiveCoordinates;
      // Homogeneous coordinates vary in Q independently of the geometric W.
      const SbVec4f uv[] = {{.1f, .1f, .05f, .8f}, {.9f, .1f, .75f, 1.7f},
                             {.9f, .9f, .75f, 1.7f}, {.1f, .9f, .05f, .8f}};
      result.uv4->point.setValues(0, 4, uv); textures->addChild(result.uv4);
    }
    result.matrix = new SoTextureMatrixTransform; result.matrix->matrix = projective();
    textures->addChild(result.matrix);
    result.image = new SoTexture2;
    result.image->model = bright ? SoTexture2::MODULATE : SoTexture2::REPLACE;
    result.image->wrapS = SoTexture2::CLAMP; result.image->wrapT = SoTexture2::CLAMP;
    const auto image = gradient(bright); result.image->image.setValue(SbVec2s(textureSize, textureSize), 3, image.data());
    textures->addChild(result.image);
    return result;
  }
  void shape(int kind) {
    geometry->removeAllChildren();
    if (kind == 0) {
      auto * shape = new SoIndexedFaceSet;
      const int32_t indices[] = {0, 1, 2, 3, -1}; shape->coordIndex.setValues(0, 5, indices);
      geometry->addChild(shape);
    } else if (kind == 1) {
      auto * shape = new SoLineSet; shape->numVertices = 2; geometry->addChild(shape);
    } else {
      auto * shape = new SoPointSet; shape->numPoints = 1; geometry->addChild(shape);
    }
  }
  SoSeparator * group = nullptr;
  SoDepthBuffer * depth = nullptr;
  SoMaterial * material = nullptr;
  SoAlphaTest * alpha = nullptr;
  SoDrawStyle * style = nullptr;
  SoGroup * textures = nullptr, * geometry = nullptr;
  SoCoordinate3 * coordinates = nullptr;
};
struct Scene {
  Scene() {
    root = new SoSeparator; root->ref();
    auto * camera = new SoOrthographicCamera;
    camera->height = 2; camera->position.setValue(0, 0, 5);
    camera->nearDistance = 1; camera->farDistance = 20; root->addChild(camera);
    auto * lighting = new SoLightModel; lighting->model = SoLightModel::BASE_COLOR; root->addChild(lighting);
    auto * quality = new SoComplexity; quality->textureQuality = .5f; root->addChild(quality);
  }
  ~Scene() { root->unref(); }
  Scene(const Scene &) = delete;
  Scene & operator=(const Scene &) = delete;
  SoSeparator * root = nullptr;
};
size_t colored(const std::vector<uint8_t> & pixels) {
  size_t count = 0;
  for (size_t i = 0; i < pixels.size(); i += 4)
    count += pixels[i] > 3 || pixels[i + 1] > 3 || pixels[i + 2] > 3;
  return count;
}
bool pixel(const std::vector<uint8_t> & image, int x, int y, Color expected, const std::string & label) {
  if (!check(image.size() == size_t(width * height * 4), label + ": incomplete readback")) return false;
  const size_t offset = size_t(y * width + x) * 4;
  for (int c = 0; c < 3; ++c) {
    if (std::abs(int(image[offset + c]) - expected[c]) > 3) {
      std::cerr << label << " sample=" << x << ',' << y << " channel=" << c << " actual="
                << int(image[offset + c]) << " expected=" << expected[c] << '\n';
      return check(false, label + ": independent projective sample differs");
    }
  }
  return true;
}
// The comparator uses the existing fragment policy gate's three-code-value
// pixel threshold and two-percent outlier bound, with a stricter MAE of one.
// No format, sampler or silhouette tolerance is widened for this feature.
struct Harness {
  explicit Harness(bool selectedGpu, bool fast = true, const CoinRenderOptions & options = CoinRenderOptions())
    : gpu(selectedGpu), cpuAction(SbViewportRegion(width, height)), gpuAction(SbViewportRegion(width, height))
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
    cpuAction.setFastPathEnabled(fast); gpuAction.setFastPathEnabled(fast);
    cpuAction.setBackgroundColor(SbColor4f(0, 0, 0, 1)); gpuAction.setBackgroundColor(SbColor4f(0, 0, 0, 1));
    transparency(CoinRenderAction::BLEND);
#if COIN_HAVE_LEGACY_GL_RENDERER
    gl.setComponents(SoOffscreenRenderer::RGB); gl.setBackgroundColor(SbColor(0, 0, 0));
#endif
  }
  ~Harness() { cpuAction.setRenderTarget(nullptr); gpuAction.setRenderTarget(nullptr); }
  void fast(bool enable) { cpuAction.setFastPathEnabled(enable); gpuAction.setFastPathEnabled(enable); }
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
        label + ": mandatory CoinGL oracle unavailable or changed mode")) return false;
    reference.resize(width * height * 4); const uint8_t * source = gl.getBuffer();
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
      const size_t a = size_t(y * width + x) * 4, b = size_t((height - 1 - y) * width + x) * glChannels;
      for (int c = 0; c < 3; ++c) reference[a + c] = source[b + c];
      reference[a + 3] = 255;
    }
    return true;
#else
    (void)root;
    return check(false, label + ": mandatory CoinGL oracle was not compiled");
#endif
  }
  bool compare(const std::vector<uint8_t> & actual, const std::string & label,
               const std::string & oracle = "CoinGL") {
    if (!check(actual.size() == reference.size() && !actual.empty(), label + ": incomplete comparison")) return false;
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
    std::cout << label << " oracle=" << oracle << " reference_pixels=" << colored(reference) << " actual_pixels=" << colored(actual)
              << " roi_mae=" << mae << " max=" << maximum << " pixels_over3=" << bad << '\n';
    return check(mae <= 1 && bad <= std::max<size_t>(4, active / 50), label + ": differs from " + oracle);
  }
  bool render(SoNode * root, const std::string & label, bool visible = true, SoNode * projectedReference = nullptr) {
    if (!check(cpu && observer && (!gpu || native), label + ": target unavailable")) return false;
    const size_t submissions = observer->submissions;
    cpuAction.apply(root); cpu->readbackRGBA(lastCpu);
    if (!check(cpuAction.getLastStatus() == CoinRenderAction::SUCCESS && observer->submissions == submissions + 1 &&
        observer->draws && lastCpu.size() == size_t(width * height * 4),
        label + ": CPU capture/submission failed: " + cpuAction.getLastError().getString())) return false;
    if (!check(visible ? colored(lastCpu) >= 8 : colored(lastCpu) == 0, label + ": invalid CPU visibility")) return false;
    if (!gpu) return true;
    if (!referencePixels(projectedReference ? projectedReference : root, label) || !check(visible ? colored(reference) >= 8 : colored(reference) == 0,
        label + ": native control is vacuous or has incorrect visibility")) return false;
    gpuAction.apply(root); native->readbackRGBA(lastNative);
    if (!check(gpuAction.getLastStatus() == CoinRenderAction::SUCCESS && lastNative.size() == reference.size() &&
        (visible ? colored(lastNative) >= 8 : colored(lastNative) == 0),
        label + ": GPU execution failed: " + gpuAction.getLastError().getString())) return false;
    ++comparisons;
    if (projectedReference) ++projectedGlComparisons; else ++nativeGlComparisons;
    const std::string oracle = projectedReference ? "CoinGL-equivalent-unit-zero-projection" : "CoinGL";
    return compare(lastCpu, label + "/CPU", oracle) && compare(lastNative, label + "/GPU", oracle);
  }
  bool renderAnalytic(SoNode * root, const std::string & label, const std::vector<uint8_t> & expected) {
    if (!check(cpu && observer && (!gpu || native) && expected.size() == size_t(width * height * 4),
               label + ": analytic/reference targets unavailable")) return false;
    const size_t submissions = observer->submissions;
    cpuAction.apply(root); cpu->readbackRGBA(lastCpu); reference = expected;
    if (!check(cpuAction.getLastStatus() == CoinRenderAction::SUCCESS && observer->submissions == submissions + 1 &&
        observer->draws && lastCpu.size() == reference.size() && colored(lastCpu) >= 8,
        label + ": CPU capture failed: " + cpuAction.getLastError().getString())) return false;
    ++analyticCpuComparisons;
    if (!compare(lastCpu, label + "/CPU", "independent-eight-stage-analytic")) return false;
    if (!gpu) return true;
    gpuAction.apply(root); native->readbackRGBA(lastNative);
    if (!check(gpuAction.getLastStatus() == CoinRenderAction::SUCCESS && lastNative.size() == reference.size() && colored(lastNative) >= 8,
        label + ": GPU execution failed: " + gpuAction.getLastError().getString())) return false;
    ++analyticGpuComparisons;
    return compare(lastNative, label + "/GPU", "independent-eight-stage-analytic");
  }
  bool renderGpuOnly(SoNode * root, const std::string & label, bool visible = true) {
    if (!check(gpu && native && referencePixels(root, label) &&
               (visible ? colored(reference) >= 8 : colored(reference) == 0),
               label + ": mandatory GPU/CoinGL targets or control unavailable")) return false;
    gpuAction.apply(root); native->readbackRGBA(lastNative);
    if (!check(gpuAction.getLastStatus() == CoinRenderAction::SUCCESS && lastNative.size() == reference.size() &&
               (visible ? colored(lastNative) >= 8 : colored(lastNative) == 0),
               label + ": GPU execution failed: " + gpuAction.getLastError().getString())) return false;
    ++nativeGlComparisons;
    return compare(lastNative, label + "/GPU", "CoinGL");
  }
  bool renderGpuEquivalentOneLayer(SoNode * root, const std::string & label) {
#if COIN_HAVE_LEGACY_GL_RENDERER
    // The native legacy sorted-layers ARB program adds primary color to the
    // texture and does not project its Q coordinate. For this explicitly
    // single-contributor scene, ordinary BLEND provides the equivalent
    // source-over RGB contract without certifying that native ARB program.
    if (!check(gpu && native && gpuAction.getTransparencyType() == CoinRenderAction::SORTED_LAYERS_BLEND,
               label + ": GPU peeling target or modality unavailable")) return false;
    SoOffscreenRenderer equivalent(SbViewportRegion(width, height));
    equivalent.setComponents(SoOffscreenRenderer::RGB); equivalent.setBackgroundColor(SbColor(0, 0, 0));
    equivalent.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
    if (!check(equivalent.render(root) && equivalent.getBuffer() &&
               equivalent.getGLRenderAction()->getTransparencyType() == SoGLRenderAction::BLEND,
               label + ": mandatory equivalent one-layer CoinGL BLEND oracle unavailable")) return false;
    reference.resize(width * height * 4); const uint8_t * source = equivalent.getBuffer();
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
      const size_t a = size_t(y * width + x) * 4, b = size_t((height - 1 - y) * width + x) * 3;
      for (int c = 0; c < 3; ++c) reference[a + c] = source[b + c];
      reference[a + 3] = 255;
    }
    gpuAction.apply(root); native->readbackRGBA(lastNative);
    if (!check(gpuAction.getLastStatus() == CoinRenderAction::SUCCESS && lastNative.size() == reference.size() &&
               colored(lastNative) >= 8 && colored(reference) >= 8,
               label + ": GPU peeling/equivalent control failed: " + gpuAction.getLastError().getString())) return false;
    ++equivalentOneLayerGlComparisons;
    return compare(lastNative, label + "/GPU", "CoinGL-equivalent-one-layer-source-over");
#else
    (void)root;
    return check(false, label + ": mandatory equivalent CoinGL oracle was not compiled");
#endif
  }
  bool sample(int x, int y, Color expected, const std::string & label) {
    return pixel(lastCpu, x, y, expected, label + "/CPU") && (!gpu ||
      (pixel(reference, x, y, expected, label + "/CoinGL") && pixel(lastNative, x, y, expected, label + "/GPU")));
  }
  bool reject(SoNode * root, const std::string & label,
              CoinRenderAction::Status expected = CoinRenderAction::INVALID_SCENE,
              bool gpuOnly = false, const std::string & diagnostic = "") {
    std::vector<uint8_t> pixels;
    if (!gpuOnly) {
      const auto previous = lastCpu; const uint64_t serial = cpu->getLastSubmissionSerial();
      const size_t submissions = observer->submissions;
      cpuAction.apply(root); cpu->readbackRGBA(pixels);
      if (!check(cpuAction.getLastStatus() == expected && cpuAction.getLastError().getLength() &&
          (diagnostic.empty() || std::string(cpuAction.getLastError().getString()).find(diagnostic) != std::string::npos) &&
          observer->submissions == submissions && cpu->getLastSubmissionSerial() == serial && pixels == previous,
          label + ": CPU rejection changed published pixels or serial")) return false;
    }
    if (gpu) {
      const auto previousNative = lastNative; const uint64_t nativeSerial = native->getLastSubmissionSerial();
      gpuAction.apply(root); native->readbackRGBA(pixels);
      if (!check(gpuAction.getLastStatus() == expected && gpuAction.getLastError().getLength() &&
          (diagnostic.empty() || std::string(gpuAction.getLastError().getString()).find(diagnostic) != std::string::npos) &&
          native->getLastSubmissionSerial() == nativeSerial && pixels == previousNative,
          label + ": GPU rejection changed published pixels or serial")) return false;
    }
    return true;
  }
  bool gpu;
  int glChannels = 3;
  CoinRenderAction::TransparencyType coinMode = CoinRenderAction::BLEND;
  std::unique_ptr<CoinRenderTarget> cpu, native;
  CaptureBackend * observer = nullptr;
  CoinRenderAction cpuAction, gpuAction;
#if COIN_HAVE_LEGACY_GL_RENDERER
  SoOffscreenRenderer gl;
#endif
  std::vector<uint8_t> lastCpu, lastNative, reference;
  size_t comparisons = 0;
};

bool changed(const std::vector<uint8_t> & a, const std::vector<uint8_t> & b, const std::string & label) {
  size_t changes = 0;
  if (a.size() == b.size()) for (size_t i = 0; i < a.size(); i += 4)
    changes += std::abs(int(a[i]) - int(b[i])) > 3 || std::abs(int(a[i + 1]) - int(b[i + 1])) > 3;
  return check(changes >= 16, label + ": counterfactual did not change enough interior pixels");
}
bool restored(Harness & test, const std::vector<uint8_t> & cpu, const std::vector<uint8_t> & native,
              const std::string & label) {
  return check(test.lastCpu == cpu && (!test.gpu || test.lastNative == native),
               label + ": A/B/A must restore exact published pixels");
}

bool twoDimensional(bool gpu) {
  Scene scene; Surface surface; scene.root->addChild(surface.group);
  TextureStage stage = surface.stage(0); Harness test(gpu);
  stage.matrix->matrix = SbMatrix::identity();
  if (!test.render(scene.root, "uv2/identity-control")) return false;
  SbMatrix affine = SbMatrix::identity(); affine[0][0] = .7f; affine[1][1] = .65f;
  affine[3][0] = .12f; affine[3][1] = .14f; stage.matrix->matrix = affine;
  if (!test.render(scene.root, "uv2/affine-control")) return false;
  stage.matrix->matrix = projective();
  if (!test.render(scene.root, "uv2/projective-fragment-divide")) return false;
  const auto originalCpu = test.lastCpu, originalNative = test.lastNative, originalGl = test.reference;
  const uint64_t originalRevision = test.observer->revision;
  // Independent screen -> plane -> raw UV arithmetic. Texture texels form
  // affine color ramps; bilinear filtering evaluates their analytic value.
  for (const auto & position : std::array<std::array<int, 2>, 3>{{{{35, 47}}, {{48, 40}}, {{62, 29}}}}) {
    const int x = position[0], y = position[1];
    const float worldX = (x + .5f - width * .5f) / 40.f;
    const float worldY = (height * .5f - y - .5f) / 40.f;
    const float s = .1f + .8f * (worldX + .75f) / 1.5f;
    const float t = .1f + .8f * (worldY + .75f) / 1.5f;
    const float q = .9f * s + .25f * t + .8f;
    if (!test.sample(x, y, gradientColor((.7f * s + .12f) / q, (.65f * t + .14f) / q),
                     "uv2/analytic-fragment-ratio")) return false;
  }
  if (!test.render(scene.root, "uv2/unchanged-reuse") || !restored(test, originalCpu, originalNative, "uv2/reuse") ||
      !check(test.observer->revision == originalRevision, "unchanged projective frame revision changed")) return false;
  test.fast(false);
  if (!test.render(scene.root, "uv2/callback-fallback") || !restored(test, originalCpu, originalNative, "uv2/fast-vs-fallback")) return false;
  test.fast(true);
  stage.matrix->matrix = projective(.65f);
  if (!test.render(scene.root, "uv2/matrix-B") || !changed(originalCpu, test.lastCpu, "uv2/matrix") ||
      !check(test.observer->revision != originalRevision, "matrix mutation retained stale frame revision")) return false;
  stage.matrix->matrix = projective();
  if (!test.render(scene.root, "uv2/matrix-A-restored") || !restored(test, originalCpu, originalNative, "uv2/matrix")) return false;
  const SbVec2f raw[] = {{.1f, .1f}, {.9f, .1f}, {.9f, .9f}, {.1f, .9f}};
  // This counterfactual is precisely the incorrect per-vertex division.
  SbVec2f divided[4];
  for (int i = 0; i < 4; ++i) {
    const float s = raw[i][0], t = raw[i][1], q = .9f * s + .25f * t + .8f;
    divided[i].setValue((.7f * s + .12f) / q, (.65f * t + .14f) / q);
  }
  stage.uv2->point.setValues(0, 4, divided); stage.matrix->matrix = SbMatrix::identity();
  if (!test.render(scene.root, "uv2/predivided-counterfactual") ||
      !changed(originalCpu, test.lastCpu, "uv2/predivided-CPU") ||
      (gpu && !changed(originalGl, test.reference, "uv2/predivided-CoinGL"))) return false;
  stage.uv2->point.setValues(0, 4, raw); stage.matrix->matrix = projective();
  if (!test.render(scene.root, "uv2/predivided-return-to-A") || !restored(test, originalCpu, originalNative, "uv2/predivide")) return false;
  stage.uv2->point.set1Value(1, .65f, .15f); stage.uv2->point.set1Value(2, .65f, .85f);
  if (!test.render(scene.root, "uv2/coordinate-B") || !changed(originalCpu, test.lastCpu, "uv2/coordinate")) return false;
  stage.uv2->point.setValues(0, 4, raw);
  return test.render(scene.root, "uv2/coordinate-A-restored") && restored(test, originalCpu, originalNative, "uv2/coordinate");
}

bool higherDimensions(bool gpu) {
  for (int dimension : {3, 4}) {
    Scene scene; Surface surface; scene.root->addChild(surface.group);
    TextureStage stage = surface.stage(0, dimension); Harness test(gpu);
    const std::string label = "uv" + std::to_string(dimension);
    if (!test.render(scene.root, label + "/r-contributes-to-STQ")) return false;
    const auto originalCpu = test.lastCpu, originalNative = test.lastNative, originalGl = test.reference;
    test.fast(false);
    if (!test.render(scene.root, label + "/callback-fallback") || !restored(test, originalCpu, originalNative, label + "/fallback")) return false;
    test.fast(true);
    if (dimension == 3) {
      for (int i = 0; i < 4; ++i) {
        SbVec3f value = stage.uv3->point[i]; value[2] = 0; stage.uv3->point.set1Value(i, value);
      }
    } else {
      for (int i = 0; i < 4; ++i) {
        SbVec4f value = stage.uv4->point[i]; value[3] = 1; stage.uv4->point.set1Value(i, value);
      }
    }
    if (!test.render(scene.root, label + "/component-B") || !changed(originalCpu, test.lastCpu, label + "/component-CPU") ||
        (gpu && !changed(originalGl, test.reference, label + "/component-CoinGL"))) return false;
    if (dimension == 3) {
      const SbVec3f uv[] = {{.1f, .1f, .05f}, {.9f, .1f, .75f}, {.9f, .9f, .75f}, {.1f, .9f, .05f}};
      stage.uv3->point.setValues(0, 4, uv);
    } else {
      const SbVec4f uv[] = {{.1f, .1f, .05f, .8f}, {.9f, .1f, .75f, 1.7f},
                           {.9f, .9f, .75f, 1.7f}, {.1f, .9f, .05f, .8f}};
      stage.uv4->point.setValues(0, 4, uv);
    }
    if (!test.render(scene.root, label + "/component-A-restored") || !restored(test, originalCpu, originalNative, label)) return false;
    // A homogeneous vector and its global negative describe the same UV.
    if (dimension == 4) {
      for (int i = 0; i < 4; ++i) {
        SbVec4f value = stage.uv4->point[i]; value *= -1; stage.uv4->point.set1Value(i, value);
      }
      if (!test.render(scene.root, "uv4/consistent-negative-Q") || !restored(test, originalCpu, originalNative, "uv4/global-negation")) return false;
    }
  }
  return true;
}

std::array<double, 2> analyticStage(int unit, double x, double y, double variation) {
  // Independent closed-form fixture description. This does not read nodes,
  // captured frames, packed coordinates, shader code, or implementation math.
  const int dimension = 2 + unit % 3;
  const double s = .1 + .8 * x, t = .1 + .8 * y;
  const double r = dimension == 2 ? 0 : .05 + .7 * x;
  const double q = dimension == 4 ? .8 + .9 * x : 1;
  const double transformedQ = (.9 + variation) * s + .25 * t + .4 * r + .8 * q;
  return {{(.7 * s + .15 * r + .12 * q) / transformedQ,
           (.65 * t + .12 * r + .14 * q) / transformedQ}};
}
std::vector<uint8_t> eightStageReference(const std::array<float, 8> & variations, int predividedUnit = -1,
                                       int isolatedPatternUnit = -1) {
  std::vector<uint8_t> image(width * height * 4, 0);
  for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
    const size_t offset = size_t(y * width + x) * 4; image[offset + 3] = 255;
    // The orthographic 1.5-by-1.5 plane occupies these exact 60-by-60 pixels.
    if (x < 18 || x >= 78 || y < 10 || y >= 70) continue;
    const double planeX = (double(x) + .5 - 18) / 60;
    const double planeY = (70 - double(y) - .5) / 60;
    std::array<double, 3> product{{1, 1, 1}};
    for (int unit = 0; unit < 8; ++unit) {
      auto projected = analyticStage(unit, planeX, planeY, variations[unit]);
      if (unit == predividedUnit) {
        const auto a = analyticStage(unit, 0, 0, variations[unit]);
        const auto b = analyticStage(unit, 1, 0, variations[unit]);
        const auto c = analyticStage(unit, 1, 1, variations[unit]);
        const auto d = analyticStage(unit, 0, 1, variations[unit]);
        // Indexed quad triangulation is independently stated here. The two
        // triangles have the same diagonal as the explicit face indices.
        for (int component = 0; component < 2; ++component) projected[component] = planeX >= planeY
          ? a[component] * (1 - planeX) + b[component] * (planeX - planeY) + c[component] * planeY
          : a[component] * (1 - planeY) + c[component] * planeX + d[component] * (planeY - planeX);
      }
      const double texelX = std::max(0.0, std::min(15.0, projected[0] * 16 - .5));
      const double texelY = std::max(0.0, std::min(15.0, projected[1] * 16 - .5));
      if (isolatedPatternUnit < 0) {
        product[0] *= (176 + 4 * texelX) / 255;
        product[1] *= (180 + 4 * texelY) / 255;
        product[2] *= (192 + texelX + texelY) / 255;
      } else if (unit == isolatedPatternUnit) {
        product[0] *= (32 + 12 * texelX) / 255;
        product[1] *= (40 + 10 * texelY) / 255;
        product[2] *= (96 + 4 * texelX + 3 * texelY) / 255;
      }
    }
    for (int component = 0; component < 3; ++component)
      image[offset + component] = uint8_t(std::lround(product[component] * 255));
  }
  return image;
}
void stageInput(TextureStage & stage, int unit, bool predivided, float variation) {
  const double cornerX[] = {0, 1, 1, 0}, cornerY[] = {0, 0, 1, 1};
  const int dimension = 2 + unit % 3;
  for (int vertex = 0; vertex < 4; ++vertex) {
    const double x = cornerX[vertex], y = cornerY[vertex];
    const auto projected = analyticStage(unit, x, y, variation);
    const float s = float(predivided ? projected[0] : .1 + .8 * x);
    const float t = float(predivided ? projected[1] : .1 + .8 * y);
    const float r = predivided ? 0 : float(.05 + .7 * x);
    const float q = predivided ? 1 : float(.8 + .9 * x);
    if (dimension == 2) stage.uv2->point.set1Value(vertex, s, t);
    else if (dimension == 3) stage.uv3->point.set1Value(vertex, s, t, r);
    else stage.uv4->point.set1Value(vertex, SbVec4f(s, t, r, q));
  }
  stage.matrix->matrix = predivided ? SbMatrix::identity() : projective(variation);
}

bool units(bool gpu) {
  if (gpu) {
    std::cout << "units/native-CoinGL GL_MAX_TEXTURE_UNITS=" << ProjectiveCoordinates::fixedFunctionTextureUnits
              << " high-unit REPLACE oracle=equivalent-unit-zero-projection eight-stage oracle=independent-analytic\n";
    if (!check(ProjectiveCoordinates::fixedFunctionTextureUnits >= 4,
               "native four-stage projective qualification requires four fixed-function texture units")) return false;
  }
  {
    Scene scene; Surface surface; scene.root->addChild(surface.group); auto stage = surface.stage(7, 4); Harness test(gpu);
    Scene projection; Surface projectedSurface; projection.root->addChild(projectedSurface.group);
    auto projectedStage = projectedSurface.stage(0, 4);
    // A sole RGB REPLACE stage is independent of earlier stages. Relabeling
    // unit seven as unit zero preserves the image, ST R Q, matrix and alpha.
    // This qualifies the high-unit addressing against an equivalent GL
    // projection, not against native fixed-function unit seven execution.
    if (!test.render(scene.root, "units/sparse-seven-only-GL-projection", true, projection.root)) return false;
    const auto originalCpu = test.lastCpu, originalNative = test.lastNative;
    test.fast(false);
    if (!test.render(scene.root, "units/sparse-seven-fallback-GL-projection", true, projection.root) ||
        !restored(test, originalCpu, originalNative, "units/sparse-seven")) return false;
    stage.matrix->matrix = projective(.6f); projectedStage.matrix->matrix = projective(.6f);
    if (!test.render(scene.root, "units/sparse-seven-B-GL-projection", true, projection.root) ||
        !changed(originalCpu, test.lastCpu, "units/seven")) return false;
    stage.matrix->matrix = projective(); projectedStage.matrix->matrix = projective();
    if (!test.render(scene.root, "units/sparse-seven-A-GL-projection", true, projection.root) ||
        !restored(test, originalCpu, originalNative, "units/seven-A")) return false;
  }
  {
    Scene scene; Surface surface; scene.root->addChild(surface.group); Harness test(gpu);
    std::array<TextureStage, 4> stages;
    for (int unit = 0; unit < 4; ++unit) {
      stages[unit] = surface.stage(unit, 2 + unit % 3, true);
      stages[unit].matrix->matrix = projective(unit * .04f);
    }
    if (!test.render(scene.root, "units/four-native-GL-projective-stages")) return false;
    const auto originalCpu = test.lastCpu, originalNative = test.lastNative;
    test.fast(false);
    if (!test.render(scene.root, "units/four-native-GL-fallback") ||
        !restored(test, originalCpu, originalNative, "units/four-native-fallback")) return false;
    for (int unit = 0; unit < 4; ++unit) {
      stages[unit].matrix->matrix = projective(unit * .04f + 2);
      const auto label = "units/four-native/unit-" + std::to_string(unit);
      if (!test.render(scene.root, label + "/B") || !changed(originalCpu, test.lastCpu, label)) return false;
      stages[unit].matrix->matrix = projective(unit * .04f);
      if (!test.render(scene.root, label + "/A") || !restored(test, originalCpu, originalNative, label)) return false;
    }
  }
  Scene scene; Surface surface; scene.root->addChild(surface.group); Harness test(gpu);
  // State the diagonal explicitly so the independent counterfactual oracle
  // is independent of any quad tessellator's choice of triangulation.
  const int32_t triangles[] = {0, 1, 2, -1, 0, 2, 3, -1};
  static_cast<SoIndexedFaceSet *>(surface.geometry->getChild(0))->coordIndex.setValues(0, 8, triangles);
  std::array<TextureStage, 8> stages;
  std::array<float, 8> variations;
  for (int i = 0; i < 8; ++i) {
    stages[i] = surface.stage(i, 2 + i % 3, true);
    variations[i] = i * .04f; stages[i].matrix->matrix = projective(variations[i]);
  }
  if (!test.renderAnalytic(scene.root, "units/all-eight-independent-Q", eightStageReference(variations))) return false;
  const auto originalCpu = test.lastCpu, originalNative = test.lastNative;
  test.fast(false);
  if (!test.renderAnalytic(scene.root, "units/all-eight-fallback", eightStageReference(variations)) ||
      !restored(test, originalCpu, originalNative, "units/eight-fast-fallback")) return false;
  test.fast(true);
  for (int i = 0; i < 8; ++i) {
    variations[i] += 4.5f; stages[i].matrix->matrix = projective(variations[i]);
    const std::string label = "units/unit-" + std::to_string(i);
    if (!test.renderAnalytic(scene.root, label + "/B", eightStageReference(variations)) ||
        !changed(originalCpu, test.lastCpu, label)) return false;
    variations[i] = i * .04f; stages[i].matrix->matrix = projective(variations[i]);
    if (!test.renderAnalytic(scene.root, label + "/A-restored", eightStageReference(variations)) ||
        !restored(test, originalCpu, originalNative, label)) return false;
    // Keep all eight units enabled, and isolate one strong RGB ramp so the
    // other seven MODULATE stages cannot attenuate the predivision error.
    const uint8_t white[] = {255, 255, 255}; const auto discriminating = gradient(false);
    for (int unit = 0; unit < 8; ++unit)
      stages[unit].image->image.setValue(SbVec2s(1, 1), 3, white);
    stages[i].image->image.setValue(SbVec2s(textureSize, textureSize), 3, discriminating.data());
    if (!test.renderAnalytic(scene.root, label + "/eight-active-isolated-projective-control",
                            eightStageReference(variations, -1, i))) return false;
    const auto isolatedCpu = test.lastCpu, isolatedNative = test.lastNative;
    stageInput(stages[i], i, true, variations[i]);
    if (!test.renderAnalytic(scene.root, label + "/eight-active-predivided-counterfactual",
                            eightStageReference(variations, i, i)) ||
        !changed(isolatedCpu, test.lastCpu, label + "/predivide")) return false;
    stageInput(stages[i], i, false, variations[i]);
    if (!test.renderAnalytic(scene.root, label + "/eight-active-isolated-A-restored",
                            eightStageReference(variations, -1, i)) ||
        !restored(test, isolatedCpu, isolatedNative, label + "/predivide")) return false;
    const auto bright = gradient(true);
    for (int unit = 0; unit < 8; ++unit)
      stages[unit].image->image.setValue(SbVec2s(textureSize, textureSize), 3, bright.data());
    if (!test.renderAnalytic(scene.root, label + "/full-eight-patterns-A-restored", eightStageReference(variations)) ||
        !restored(test, originalCpu, originalNative, label + "/full-eight")) return false;
  }
  return true;
}

bool perspective(bool gpu) {
  Scene scene; Surface surface; scene.root->addChild(surface.group); surface.stage(0, 4); Harness test(gpu);
  auto * camera = new SoPerspectiveCamera; camera->position.setValue(0, 0, 3);
  camera->heightAngle = .65f; camera->nearDistance = .1f; camera->farDistance = 20;
  scene.root->replaceChild(0, camera);
  // Depth varies across the plane. Geometric perspective and projective
  // texture interpolation must compose, rather than cancel one another.
  const SbVec3f points[] = {{-.75f, -.75f, -.5f}, {.75f, -.75f, .5f},
                           {.75f, .75f, .5f}, {-.75f, .75f, -.5f}};
  surface.coordinates->point.setValues(0, 4, points);
  if (!test.render(scene.root, "perspective/geometric-W-and-texture-Q")) return false;
  const auto originalCpu = test.lastCpu, originalNative = test.lastNative;
  test.fast(false);
  if (!test.render(scene.root, "perspective/callback-fallback") || !restored(test, originalCpu, originalNative, "perspective/fallback")) return false;
  camera->position.setValue(.18f, .1f, 3.4f);
  if (!test.render(scene.root, "perspective/camera-B") || !changed(originalCpu, test.lastCpu, "perspective/camera")) return false;
  camera->position.setValue(0, 0, 3);
  return test.render(scene.root, "perspective/camera-A-restored") && restored(test, originalCpu, originalNative, "perspective/camera");
}

bool alphaAndTransparency(bool gpu) {
  Scene scene; Surface surface; scene.root->addChild(surface.group); surface.stage(0, 2); Harness test(gpu);
  surface.material->transparency = .5f;
  if (!test.render(scene.root, "alpha/RGB-REPLACE-retains-material-alpha")) return false;
  const auto originalCpu = test.lastCpu, originalNative = test.lastNative;
  const float s = .1f + .8f * ((.0125f + .75f) / 1.5f);
  const float t = .1f + .8f * ((-.0125f + .75f) / 1.5f);
  const float q = .9f * s + .25f * t + .8f;
  if (!test.sample(48, 40, gradientColor((.7f * s + .12f) / q, (.65f * t + .14f) / q, 128.f / 255), "alpha/material-alpha")) return false;
  surface.alpha->function = SoAlphaTest::GREATER; surface.alpha->value = .6f;
  if (!test.render(scene.root, "alpha/projective-RGB-discard", false)) return false;
  surface.alpha->value = .4f;
  if (!test.render(scene.root, "alpha/projective-RGB-pass")) return false;
  // Effective alpha testing packs primary alpha to the native byte value.
  // Restore NONE before demanding exact A/B/A equality with the NONE anchor.
  surface.alpha->function = SoAlphaTest::NONE;
  if (!test.render(scene.root, "alpha/NONE-A-restored") || !restored(test, originalCpu, originalNative, "alpha/NONE")) return false;
  surface.alpha->function = SoAlphaTest::GREATER;
  test.transparency(CoinRenderAction::SORTED_OBJECT_BLEND);
  if (!test.render(scene.root, "alpha/projective-sorted-object")) return false;
  auto * cover = new Surface; cover->material->diffuseColor.setValue(0, 1, 0);
  cover->depth->test = FALSE; cover->depth->write = FALSE; scene.root->addChild(cover->group);
  // The green shape is traversed later; the transparent textured shape is
  // deferred by native CoinGL and appears blended over the green control.
  if (!test.render(scene.root, "alpha/projective-deferred-over-green")) { delete cover; return false; }
  Color overGreen = gradientColor((.7f * s + .12f) / q, (.65f * t + .14f) / q, 128.f / 255);
  overGreen[1] += 127;
  const bool valid = test.sample(48, 40, overGreen, "alpha/deferred-order");
  delete cover;
  return valid;
}

bool separatorScope(bool gpu) {
  Scene scene; Surface first, second; scene.root->addChild(first.group); scene.root->addChild(second.group);
  auto a = first.stage(0, 3), b = second.stage(0, 3); Harness test(gpu);
  SbVec3f left[] = {{-.95f, -.65f, 0}, {-.05f, -.65f, 0}, {-.05f, .65f, 0}, {-.95f, .65f, 0}};
  SbVec3f right[] = {{.05f, -.65f, 0}, {.95f, -.65f, 0}, {.95f, .65f, 0}, {.05f, .65f, 0}};
  first.coordinates->point.setValues(0, 4, left); second.coordinates->point.setValues(0, 4, right);
  b.matrix->matrix = SbMatrix::identity();
  if (!test.render(scene.root, "scope/separator-projective-then-affine")) return false;
  const auto originalCpu = test.lastCpu, originalNative = test.lastNative;
  a.matrix->matrix = projective(.6f);
  if (!test.render(scene.root, "scope/first-matrix-B")) return false;
  for (int y = 16; y < 64; ++y) for (int x = 52; x < 82; ++x) {
    const size_t offset = size_t(y * width + x) * 4;
    if (!check(std::equal(originalCpu.begin() + offset, originalCpu.begin() + offset + 4, test.lastCpu.begin() + offset) &&
      (!gpu || std::equal(originalNative.begin() + offset, originalNative.begin() + offset + 4, test.lastNative.begin() + offset)),
      "scope: sibling separator leaked projective matrix mutation")) return false;
  }
  a.matrix->matrix = projective();
  return test.render(scene.root, "scope/first-matrix-A-restored") && restored(test, originalCpu, originalNative, "scope/A");
}

bool lineAndPoint(bool gpu) {
  for (int kind : {1, 2}) {
    Scene scene; Surface surface; scene.root->addChild(surface.group); auto stage = surface.stage(0, 4); Harness test(gpu);
    surface.shape(kind);
    const SbVec3f points[] = {{-.4875f, -.0125f, 0}, {.5125f, -.0125f, 0}, {.5125f, .5f, 0}, {-.4875f, .5f, 0}};
    surface.coordinates->point.setValues(0, 4, points);
    if (kind == 2) surface.coordinates->point.set1Value(0, .0125f, -.0125f, 0);
    const std::string label = kind == 1 ? "line" : "point";
    if (!test.render(scene.root, label + "/projective-STQ")) return false;
    const auto originalCpu = test.lastCpu, originalNative = test.lastNative;
    stage.matrix->matrix = projective(1.4f);
    if (!test.render(scene.root, label + "/matrix-B") || !changed(originalCpu, test.lastCpu, label)) return false;
    stage.matrix->matrix = projective();
    if (!test.render(scene.root, label + "/matrix-A-restored") || !restored(test, originalCpu, originalNative, label)) return false;
  }
  return true;
}

bool primitiveLocalSigns(bool gpu) {
  Scene scene; Surface surface; scene.root->addChild(surface.group); auto stage = surface.stage(0, 4); Harness test(gpu);
  const SbVec3f points[] = {{.0125f, -.0125f, 0}, {.2625f, -.0125f, 0},
                           {.2625f, .2375f, 0}, {.0125f, .2375f, 0}};
  surface.coordinates->point.setValues(0, 4, points);
  surface.shape(2);
  static_cast<SoPointSet *>(surface.geometry->getChild(0))->numPoints = 4;
  if (!test.render(scene.root, "point/four-positive-Q-controls")) return false;
  const auto originalCpu = test.lastCpu, originalNative = test.lastNative;
  for (int i : {1, 2}) {
    SbVec4f value = stage.uv4->point[i]; value *= -1; stage.uv4->point.set1Value(i, value);
  }
  if (!test.render(scene.root, "point/opposite-Q-signs-in-separate-primitives") ||
      !restored(test, originalCpu, originalNative, "point/primitive-local-signs")) return false;
  surface.shape(0);
  if (!test.reject(scene.root, "reject/same-signs-cross-inside-triangle")) return false;
  surface.shape(2); static_cast<SoPointSet *>(surface.geometry->getChild(0))->numPoints = 4;
  return test.render(scene.root, "point/primitive-local-sign-repair") &&
    restored(test, originalCpu, originalNative, "point/primitive-local-repair");
}

struct ShadowFixture {
  ShadowFixture() {
    auto * lighting = new SoLightModel; lighting->model = SoLightModel::PHONG;
    scene.root->replaceChild(1, lighting);
    auto * environment = new SoEnvironment; environment->ambientIntensity = 0; scene.root->addChild(environment);
    group = new SoShadowGroup; group->isActive = TRUE; group->precision = .0625f;
    // One real map is required by the existing qualified shadow executor.
    // Zero intensity isolates texture sampling from VSM/light differences.
    auto * light = new SoShadowSpotLight; light->location.setValue(0, 0, 3);
    light->direction.setValue(0, 0, -1); light->cutOffAngle = .7f;
    light->nearDistance = .1f; light->farDistance = 10; light->intensity = 0;
    group->addChild(light);
    style = new SoShadowStyle; style->style = SoShadowStyle::SHADOWED; group->addChild(style);
    surface.material->ambientColor.setValue(0, 0, 0); surface.material->specularColor.setValue(0, 0, 0);
    surface.material->emissiveColor.setValue(1, 1, 1);
    group->addChild(surface.group); scene.root->addChild(group);
    stage = surface.stage(0, 2); stage.image->model = SoTexture2::MODULATE;
  }
  Scene scene;
  Surface surface;
  TextureStage stage;
  SoShadowGroup * group = nullptr;
  SoShadowStyle * style = nullptr;
};
SbMatrix zeroTextureQ(const SbMatrix & original) {
  SbMatrix changed = original;
  for (int row = 0; row < 4; ++row) changed[row][3] = 0;
  return changed;
}
bool shadowProgramSampling(bool gpu) {
  if (!gpu) {
    std::cout << "shadow/projective requires --gpu; CPU reference does not execute active ShadowGroup\n";
    return true;
  }
  std::cout << "shadow/projective qualification=one-spot-map-zero-intensity emissive-white native-generated-program\n";
  for (int policy = 0; policy < 5; ++policy) {
    ShadowFixture fixture; Harness test(gpu);
    const bool direct = policy < 2;
    if (policy == 1) fixture.style->style = SoShadowStyle::CASTS_SHADOW_AND_SHADOWED;
    if (policy == 2) fixture.group->isActive = FALSE;
    if (policy == 3) fixture.style->style = SoShadowStyle::CASTS_SHADOW;
    if (policy == 4) fixture.style->style = SoShadowStyle::NO_SHADOWING;
    const std::string label = "shadow/one-map-policy-" + std::to_string(policy);
    const SbMatrix original = fixture.stage.matrix->matrix.getValue();
    if (!test.renderGpuOnly(fixture.scene.root, label + "/original-STQ")) return false;
    const auto originalNative = test.lastNative, originalGl = test.reference;
    SbMatrix differentQ = original; differentQ[0][3] += 2;
    fixture.stage.matrix->matrix = differentQ;
    if (!test.renderGpuOnly(fixture.scene.root, label + "/only-Q-B")) return false;
    if (direct) {
      if (!check(test.lastNative == originalNative && test.reference == originalGl,
                 label + ": direct ST changed when only Q changed")) return false;
    } else if (!changed(originalNative, test.lastNative, label + "/fixed-function-Q") ||
        !changed(originalGl, test.reference, label + "/fixed-function-CoinGL-Q")) return false;
    fixture.stage.matrix->matrix = original;
    if (!test.renderGpuOnly(fixture.scene.root, label + "/Q-A-restored") ||
        !check(test.lastNative == originalNative && test.reference == originalGl, label + ": Q A/B/A differs")) return false;
    fixture.stage.matrix->matrix = zeroTextureQ(original);
    if (direct) {
      if (!test.renderGpuOnly(fixture.scene.root, label + "/direct-ST-zero-Q-admitted") ||
          !check(test.lastNative == originalNative && test.reference == originalGl, label + ": direct zero Q changed ST")) return false;
      SbMatrix crossing = original; crossing[0][3] = 1; crossing[1][3] = 0;
      crossing[2][3] = 0; crossing[3][3] = -.5f; fixture.stage.matrix->matrix = crossing;
      if (!test.renderGpuOnly(fixture.scene.root, label + "/direct-ST-crossing-Q-admitted") ||
          !check(test.lastNative == originalNative && test.reference == originalGl, label + ": direct crossing Q changed ST")) return false;
    } else if (!test.reject(fixture.scene.root, label + "/fixed-function-zero-Q-refused",
                            CoinRenderAction::INVALID_SCENE, true)) return false;
    fixture.stage.matrix->matrix = original;
    if (!test.renderGpuOnly(fixture.scene.root, label + "/final-Q-A-restored") ||
        !check(test.lastNative == originalNative && test.reference == originalGl, label + ": final Q A/B/A differs")) return false;
    if (policy == 0) {
      auto * emptyProgram = new SoShaderProgram; fixture.group->insertChild(emptyProgram, 1);
      if (!test.reject(fixture.scene.root, label + "/empty-program-refused", CoinRenderAction::UNSUPPORTED,
                       true, "Empty SoShaderProgram")) return false;
      fixture.group->removeChild(emptyProgram);
      if (!test.renderGpuOnly(fixture.scene.root, label + "/empty-program-recovery") ||
          !check(test.lastNative == originalNative && test.reference == originalGl, label + ": empty program repair differs")) return false;
    }
  }
  // Outside ShadowGroup the regular native texture pipeline divides by Q.
  Scene scene; Surface surface; scene.root->addChild(surface.group); auto stage = surface.stage(0); Harness test(gpu);
  if (!test.renderGpuOnly(scene.root, "shadow/outside-group-fixed-function-control")) return false;
  const auto originalNative = test.lastNative, originalGl = test.reference;
  stage.matrix->matrix = zeroTextureQ(projective());
  if (!test.reject(scene.root, "shadow/outside-group-zero-Q-refused", CoinRenderAction::INVALID_SCENE, true)) return false;
  stage.matrix->matrix = projective();
  return test.renderGpuOnly(scene.root, "shadow/outside-group-repair") &&
    check(test.lastNative == originalNative && test.reference == originalGl, "shadow/outside-group repair differs");
}

bool layeredProjectiveSampling(bool gpu) {
  if (!gpu) {
    std::cout << "projective/layered-shadow requires --gpu; CPU reference does not execute active ShadowGroup\n";
    return true;
  }
  for (bool weighted : {false, true}) {
    CoinRenderOptions options;
    options.transparency = weighted ? COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT : COIN_RENDER_TRANSPARENCY_PEELING;
    ShadowFixture fixture; Harness test(gpu, true, options);
    fixture.surface.group->removeChild(fixture.surface.depth); fixture.surface.depth = nullptr;
    fixture.surface.material->transparency = .5f;
    test.transparency(weighted ? CoinRenderAction::SORTED_OBJECT_BLEND : CoinRenderAction::SORTED_LAYERS_BLEND);
    const std::string label = weighted ? "projective/weighted-one-contributor-direct-ST"
                                       : "projective/peeling-one-layer-direct-ST";
    const auto render = [&](const std::string & phase) {
      return test.renderGpuOnly(fixture.scene.root, label + "/" + phase);
    };
    if (!render("visible-control")) return false;
    const auto originalNative = test.lastNative, originalGl = test.reference;
    const SbMatrix original = fixture.stage.matrix->matrix.getValue();
    fixture.stage.matrix->matrix = zeroTextureQ(original);
    if (!render("zero-Q-same-direct-ST") ||
        !check(test.lastNative == originalNative && test.reference == originalGl,
               label + ": native/GPU layer path changed direct ST")) return false;
    SbMatrix changedST = original; changedST[3][0] += .18f; fixture.stage.matrix->matrix = changedST;
    if (!render("ST-B") || !changed(originalNative, test.lastNative, label + "/ST") ||
        !changed(originalGl, test.reference, label + "/native-ST")) return false;
    fixture.stage.matrix->matrix = original;
    if (!render("ST-A-restored") ||
        !check(test.lastNative == originalNative && test.reference == originalGl, label + ": native/GPU A/B/A differs")) return false;
  }
  return true;
}

bool classicLayeredProjectiveSampling(bool gpu) {
  if (!gpu) {
    std::cout << "projective/classic-layered requires --gpu; execution is qualified against CoinGL\n";
    return true;
  }
  for (bool weighted : {false, true}) {
    CoinRenderOptions options;
    options.transparency = weighted ? COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT : COIN_RENDER_TRANSPARENCY_PEELING;
    Scene scene; Surface surface; scene.root->addChild(surface.group); auto stage = surface.stage(0, 2);
    surface.group->removeChild(surface.depth); surface.depth = nullptr;
    surface.material->transparency = .5f;
    Harness test(true, true, options);
    test.transparency(weighted ? CoinRenderAction::SORTED_OBJECT_BLEND : CoinRenderAction::SORTED_LAYERS_BLEND);
    const std::string label = weighted ? "projective/weighted-one-contributor-classic-STQ"
                                       : "projective/peeling-one-layer-classic-STQ";
    const auto render = [&](const std::string & phase) {
      return weighted ? test.renderGpuOnly(scene.root, label + "/" + phase)
                      : test.renderGpuEquivalentOneLayer(scene.root, label + "/" + phase);
    };
    if (!render("variable-Q-control")) return false;
    const auto originalNative = test.lastNative, originalGl = test.reference;
    stage.matrix->matrix = projective(.8f);
    if (!render("only-Q-B") ||
        !changed(originalNative, test.lastNative, label + "/GPU-Q") ||
        !changed(originalGl, test.reference, label + "/CoinGL-Q")) return false;
    stage.matrix->matrix = projective();
    if (!render("Q-A-restored") ||
        !check(test.lastNative == originalNative && test.reference == originalGl, label + ": Q A/B/A differs")) return false;
    stageInput(stage, 0, true, 0);
    if (!render("predivided-counterfactual") ||
        !changed(originalNative, test.lastNative, label + "/GPU-predivide") ||
        !changed(originalGl, test.reference, label + "/CoinGL-predivide")) return false;
    stageInput(stage, 0, false, 0);
    if (!render("predivided-A-restored") ||
        !check(test.lastNative == originalNative && test.reference == originalGl, label + ": predivide A/B/A differs")) return false;
  }
  return true;
}

bool rttProjectiveSampling(bool gpu) {
  if (!gpu) {
    std::cout << "projective/RTT requires --gpu; producer targets are not CPU mocks\n";
    return true;
  }
  for (auto mode : {COIN_RENDER_SCENE_TEXTURE_STAGED, COIN_RENDER_SCENE_TEXTURE_DIRECT}) {
    Scene producer, consumer; Surface source, destination;
    producer.root->addChild(source.group); consumer.root->addChild(destination.group);
    auto sourceStage = source.stage(0, 2); auto destinationStage = destination.stage(0, 2);
    // Fill the producer viewport; only the smooth interior is sampled so this
    // gate certifies both projective stages and orientation, not border modes.
    const SbVec3f fill[] = {{-2, -2, 0}, {2, -2, 0}, {2, 2, 0}, {-2, 2, 0}};
    source.coordinates->point.setValues(0, 4, fill);
    const SbVec2f producerUv[] = {{-.3f, -.3f}, {1.3f, -.3f}, {1.3f, 1.3f}, {-.3f, 1.3f}};
    sourceStage.uv2->point.setValues(0, 4, producerUv);
    auto * producerMode = new SoTransparencyType; producerMode->value = SoTransparencyType::BLEND;
    producer.root->insertChild(producerMode, 3);
    auto * texture = new SoSceneTexture2; texture->scene = producer.root;
    texture->size.setValue(64, 64); texture->type = SoSceneTexture2::RGBA8;
    texture->model = SoSceneTexture2::MODULATE; texture->wrapS = SoSceneTexture2::CLAMP; texture->wrapT = SoSceneTexture2::CLAMP;
    texture->transparencyFunction = SoSceneTexture2::NONE; texture->backgroundColor.setValue(0, 0, 0, 1);
    // Native FBO traversal pushes the consumer state without resetting its
    // texture matrix. Visit the producer before the consumer's matrix so this
    // fixture qualifies two independently authored projective stages, without
    // depending on inherited producer texture state (which differs for FBOs
    // and separate native pbuffer actions).
    destination.textures->insertChild(texture, destination.textures->findChild(destinationStage.matrix));
    destination.textures->removeChild(destinationStage.image); destinationStage.image = nullptr;
    CoinRenderOptions options; options.sceneTexture = mode; Harness test(true, true, options);
    const std::string label = mode == COIN_RENDER_SCENE_TEXTURE_DIRECT ? "projective/RTT-direct" : "projective/RTT-staged";
    if (!test.renderGpuOnly(consumer.root, label + "/producer-and-consumer-projective-control")) return false;
    const auto original = test.lastNative;
    const size_t upper = size_t(25 * width + 40) * 4, lower = size_t(55 * width + 40) * 4;
    if (!check(int(test.lastNative[upper + 1]) > int(test.lastNative[lower + 1]) + 5 &&
               int(test.reference[upper + 1]) > int(test.reference[lower + 1]) + 5,
               label + ": producer/consumer vertical orientation control is vacuous")) return false;
    sourceStage.matrix->matrix = projective(.8f);
    if (!test.renderGpuOnly(consumer.root, label + "/producer-Q-B") || !changed(original, test.lastNative, label + "/producer-Q")) return false;
    sourceStage.matrix->matrix = projective();
    if (!test.renderGpuOnly(consumer.root, label + "/producer-Q-A-restored") ||
        !check(test.lastNative == original, label + ": producer Q A/B/A differs")) return false;
    destinationStage.matrix->matrix = projective(.8f);
    if (!test.renderGpuOnly(consumer.root, label + "/consumer-Q-B") || !changed(original, test.lastNative, label + "/consumer-Q")) return false;
    destinationStage.matrix->matrix = projective();
    if (!test.renderGpuOnly(consumer.root, label + "/consumer-Q-A-restored") ||
        !check(test.lastNative == original, label + ": consumer Q A/B/A differs")) return false;
    const SbVec2f flipped[] = {{.1f, .9f}, {.9f, .9f}, {.9f, .1f}, {.1f, .1f}};
    destinationStage.uv2->point.setValues(0, 4, flipped);
    if (!test.renderGpuOnly(consumer.root, label + "/consumer-vertical-flip-counterfactual") ||
        !changed(original, test.lastNative, label + "/orientation") ||
        !check(int(test.lastNative[upper + 1]) + 5 < int(test.lastNative[lower + 1]) &&
               int(test.reference[upper + 1]) + 5 < int(test.reference[lower + 1]),
               label + ": explicit vertical flip did not reverse the gradient")) return false;
    const SbVec2f originalUv[] = {{.1f, .1f}, {.9f, .1f}, {.9f, .9f}, {.1f, .9f}};
    destinationStage.uv2->point.setValues(0, 4, originalUv);
    if (!test.renderGpuOnly(consumer.root, label + "/consumer-vertical-A-restored") ||
        !check(test.lastNative == original, label + ": orientation A/B/A differs")) return false;
  }
  return true;
}

bool rejectionAndRecovery(bool gpu) {
  for (bool fast : {true, false}) {
    Scene scene; Surface surface; scene.root->addChild(surface.group); auto stage = surface.stage(0, 4); Harness test(gpu, fast);
    if (!test.render(scene.root, "reject/published-anchor")) return false;
    const auto originalCpu = test.lastCpu, originalNative = test.lastNative;
    const SbMatrix original = stage.matrix->matrix.getValue();
    for (const auto & variant : std::array<std::pair<float, float>, 2>{{{0, 0}, {1, -.5f}}}) {
      SbMatrix invalid = SbMatrix::identity();
      invalid[0][3] = variant.first; invalid[3][3] = variant.second;
      stage.matrix->matrix = invalid;
      if (!test.reject(scene.root, variant.first == 0 ? "reject/zero-Q" : "reject/Q-sign-change")) return false;
      stage.matrix->matrix = original;
      if (!test.render(scene.root, "reject/Q-repair") || !restored(test, originalCpu, originalNative, "reject/Q-repair")) return false;
    }
    SbMatrix crossing = SbMatrix::identity(); crossing[0][3] = 1; crossing[3][3] = -.5f;
    surface.shape(1); stage.matrix->matrix = crossing;
    if (!test.reject(scene.root, "reject/native-line-Q-sign-change")) return false;
    // An offscreen endpoint must be checked before geometric clipping can
    // hide its opposite Q sign or wide-line expansion duplicates the endpoint.
    const SbVec3f endpoint = surface.coordinates->point[0];
    surface.coordinates->point.set1Value(0, -4, -.75f, 0);
    if (!test.reject(scene.root, "reject/clipped-line-Q-sign-change")) return false;
    surface.coordinates->point.set1Value(0, endpoint); surface.shape(0);
    surface.style->style = SoDrawStyle::LINES;
    if (!test.reject(scene.root, "reject/polygon-lines-Q-sign-change")) return false;
    surface.style->style = SoDrawStyle::FILLED; stage.matrix->matrix = original;
    if (!test.render(scene.root, "reject/line-repair") || !restored(test, originalCpu, originalNative, "reject/line")) return false;
    for (bool overflow : {false, true}) {
      SbMatrix invalid = SbMatrix::identity();
      invalid[3][3] = overflow ? std::numeric_limits<float>::min() * 8 : std::numeric_limits<float>::denorm_min();
      if (overflow) invalid[0][0] = std::numeric_limits<float>::max() * .25f;
      stage.matrix->matrix = invalid;
      if (!test.reject(scene.root, overflow ? "reject/projected-ST-overflow" : "reject/subnormal-Q")) return false;
      stage.matrix->matrix = original;
      if (!test.render(scene.root, "reject/sampling-precision-repair") ||
          !restored(test, originalCpu, originalNative, "reject/sampling-precision")) return false;
    }
    for (float invalidValue : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
      SbMatrix invalid = original; invalid[2][0] = invalidValue; stage.matrix->matrix = invalid;
      if (!test.reject(scene.root, "reject/nonfinite-texture-matrix")) return false;
      stage.matrix->matrix = original;
      if (!test.render(scene.root, "reject/matrix-repair") || !restored(test, originalCpu, originalNative, "reject/matrix")) return false;
      for (int component = 0; component < 4; ++component) {
        const SbVec4f before = stage.uv4->point[1]; SbVec4f bad = before; bad[component] = invalidValue;
        stage.uv4->point.set1Value(1, bad);
        if (!test.reject(scene.root, "reject/nonfinite-input-component-" + std::to_string(component))) return false;
        stage.uv4->point.set1Value(1, before);
        if (!test.render(scene.root, "reject/input-repair") || !restored(test, originalCpu, originalNative, "reject/input")) return false;
      }
    }
  }
  return true;
}
} // namespace

int main(int argc, char ** argv) {
  bool gpu = false;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--gpu") gpu = true;
    else if (argument == "--capture") gpu = false;
    else { std::cerr << "Usage: CoinRenderProjectiveUvTest --capture|--gpu\n"; return 2; }
  }
  SoDB::init(); CoinRenderAction::initClass(); ProjectiveCoordinates::initClass();
  if (gpu && !CoinRenderAction::isGpuBackendAvailable()) {
    std::cerr << "CoinRenderProjectiveUvTest requires the selected GPU backend\n"; return 77;
  }
#if !COIN_HAVE_LEGACY_GL_RENDERER
  if (gpu) {
    std::cerr << "CoinRenderProjectiveUvTest mandatory CoinGL oracle was not compiled\n"; return 1;
  }
#endif
  if (!twoDimensional(gpu) || !higherDimensions(gpu) || !units(gpu) || !perspective(gpu) ||
      !alphaAndTransparency(gpu) || !separatorScope(gpu) || !lineAndPoint(gpu) || !primitiveLocalSigns(gpu) ||
      !shadowProgramSampling(gpu) || !layeredProjectiveSampling(gpu) || !classicLayeredProjectiveSampling(gpu) ||
      !rttProjectiveSampling(gpu) ||
      !rejectionAndRecovery(gpu)) return 1;
  std::cout << "CoinRenderProjectiveUvTest " << (gpu ? "GPU/CoinGL" : "capture/CPU")
            << " passed native_gl_comparisons=" << nativeGlComparisons << " equivalent_gl_projection_comparisons="
            << projectedGlComparisons << " equivalent_one_layer_gl_comparisons=" << equivalentOneLayerGlComparisons
            << " independent_analytic_cpu_comparisons=" << analyticCpuComparisons
            << " independent_analytic_gpu_comparisons=" << analyticGpuComparisons << '\n';
  return 0;
}
