#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoScale.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSpotLight.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinwgpu/CoinWgpuFfi.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static_assert(COIN_WGPU_BRIDGE_PROTOCOL_REVISION == 50,
              "Shadow, opaque-instance, alpha-test and projective-UV bridge protocol");
static_assert(sizeof(CoinWgpuLight) == 64, "3C light layout");
static_assert(sizeof(CoinWgpuRenderState) == 2292, "Texture projection policy bridge layout");
static_assert(offsetof(CoinWgpuRenderState, texture_projection) == 2288, "Texture projection policy bridge tail");

namespace {
enum SceneLight { DIRECTIONAL, POINT, SPOT_INSIDE, SPOT_OUTSIDE };

bool gpuAvailable() {
#if defined(HAVE_COIN_BGFX)
  // The legacy convenience probe deliberately excludes the experimental BGFX
  // profile. This test must nevertheless submit to it, never silently skip it.
  return true;
#else
  return CoinRenderAction::isGpuBackendAvailable();
#endif
}

bool check(bool condition, const char * message) {
  if (!condition) std::cerr << "CoinRenderLightingTest: " << message << "\n";
  return condition;
}

SoSeparator * makeScene(SceneLight kind, int lightCount,
                        const SbVec3f & attenuation = SbVec3f(0, 0, 1),
                        float scaleZ = 1.0f, float cameraX = 0.0f,
                        float halfExtent = 1.0f) {
  SoSeparator * root = new SoSeparator;
  root->ref();

  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  camera->position.setValue(cameraX, 0.0f, 3.0f);
  if (cameraX != 0.0f) camera->pointAt(SbVec3f(0.0f, 0.0f, 0.0f));
  root->addChild(camera);

  SoEnvironment * environment = new SoEnvironment;
  environment->ambientIntensity = 0.0f;
  environment->attenuation = attenuation;
  root->addChild(environment);

  for (int i = 0; i < lightCount; ++i) {
    if (kind == DIRECTIONAL) {
      SoDirectionalLight * light = new SoDirectionalLight;
      light->direction.setValue(0.0f, 0.0f, -1.0f);
      light->intensity = lightCount <= 2 ? 0.35f : 0.08f;
      root->addChild(light);
    } else if (kind == POINT) {
      SoPointLight * light = new SoPointLight;
      light->location.setValue(0.0f, 0.0f, 2.0f);
      light->intensity = 0.8f;
      root->addChild(light);
    } else {
      SoSpotLight * light = new SoSpotLight;
      light->location.setValue(kind == SPOT_OUTSIDE ? 2.0f : 0.0f, 0.0f, 1.0f);
      light->direction.setValue(0.0f, 0.0f, -1.0f);
      light->cutOffAngle = 0.25f;
      light->dropOffRate = 0.0f;
      light->intensity = 0.8f;
      root->addChild(light);
    }
  }

  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(0.8f, 0.8f, 0.8f);
  material->ambientColor.setValue(0.0f, 0.0f, 0.0f);
  material->specularColor.setValue(0.0f, 0.0f, 0.0f);
  root->addChild(material);

  if (scaleZ != 1.0f) {
    SoScale * scale = new SoScale;
    scale->scaleFactor.setValue(1.5f, 0.7f, scaleZ);
    root->addChild(scale);
    SoNormal * normals = new SoNormal;
    normals->vector.set1Value(0, SbVec3f(0.6f, 0.0f, 0.8f));
    root->addChild(normals);
    SoNormalBinding * normalBinding = new SoNormalBinding;
    normalBinding->value = SoNormalBinding::OVERALL;
    root->addChild(normalBinding);
  }

  SoCoordinate3 * coordinates = new SoCoordinate3;
  coordinates->point.setNum(4);
  coordinates->point.set1Value(0, SbVec3f(-halfExtent, -halfExtent, 0.0f));
  coordinates->point.set1Value(1, SbVec3f( halfExtent, -halfExtent, 0.0f));
  coordinates->point.set1Value(2, SbVec3f( halfExtent,  halfExtent, 0.0f));
  coordinates->point.set1Value(3, SbVec3f(-halfExtent,  halfExtent, 0.0f));
  root->addChild(coordinates);

  SoIndexedFaceSet * faces = new SoIndexedFaceSet;
  const int32_t indices[] = {0, 1, 2, 3, -1};
  faces->coordIndex.setValues(0, 5, indices);
  root->addChild(faces);
  return root;
}

bool renderCenter(SoSeparator * root, bool cpu, std::array<int, 3> & rgb,
                  float * centerDepth = nullptr, int sampleX = 32, int sampleY = 32) {
  CoinRenderTarget * target =
    CoinRenderTarget::createOffscreen(SbVec2i32(64, 64));
  if (!check(target != NULL, "createOffscreen failed")) return false;
#if defined(HAVE_COIN_BGFX)
  if (!cpu) target->setDepthReadbackEnabled(FALSE);
#endif
  if (cpu) {
    target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
#if defined(HAVE_COIN_BGFX)
    target->getPimpl()->depthBuffer.assign(64u * 64u, 1.0f);
#endif
  }
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setRenderTarget(target);
  action.apply(root);
  const bool success = action.getLastStatus() == CoinRenderAction::SUCCESS;
  if (!success) std::cerr << "Render error: " << action.getLastError().getString() << "\n";
  std::vector<uint8_t> pixels;
  std::vector<float> depth;
  if (success) {
    target->readbackRGBA(pixels);
    target->readbackDepth(depth);
  }
  delete target;
  if (!(success && pixels.size() == 64u * 64u * 4u &&
        (depth.size() == 64u * 64u || (!cpu && depth.empty()))))
    std::cerr << "renderCenter cpu=" << cpu << " pixels=" << pixels.size()
              << " depth=" << depth.size() << "\n";
  if (!check(success && pixels.size() == 64u * 64u * 4u &&
             (depth.size() == 64u * 64u || (!cpu && depth.empty())),
             "color/depth readback failed")) return false;
  const size_t center = static_cast<size_t>(sampleY) * 64u + sampleX;
  if (!depth.empty()) {
    if (!check(std::isfinite(depth[center]) && depth[center] >= 0.0f &&
               depth[center] <= 1.0f, "invalid center depth")) return false;
    if (centerDepth) *centerDepth = depth[center];
  } else if (centerDepth) {
    *centerDepth = 1.0f;
  }
  for (int c = 0; c < 3; ++c) rgb[c] = pixels[center * 4u + c];
  return true;
}

bool checkParity(SoSeparator * root, std::array<int, 3> & cpuRgb,
                 std::array<int, 3> * gpuResult = nullptr) {
  float cpuDepth = 1.0f;
  if (!renderCenter(root, true, cpuRgb, &cpuDepth)) return false;
  if (!gpuAvailable()) return true;
  std::array<int, 3> gpuRgb = {{0, 0, 0}};
  float gpuDepth = 1.0f;
  if (!renderCenter(root, false, gpuRgb, &gpuDepth)) return false;
  if (gpuResult) *gpuResult = gpuRgb;
#if !defined(HAVE_COIN_BGFX)
  if (!check(std::abs(cpuDepth - gpuDepth) < 0.025f,
             "CPU/GPU center depth mismatch")) return false;
#endif
  for (int c = 0; c < 3; ++c) {
    if (std::abs(cpuRgb[c] - gpuRgb[c]) > 36) {
      std::cerr << "CPU/GPU mismatch: " << cpuRgb[c] << " vs " << gpuRgb[c] << "\n";
      return false;
    }
  }
  return true;
}
bool checkBridgeLimit() {
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  if (!gpuAvailable()) return true;
  CoinWgpuRenderState state{};
  state.light_count = 9;
  CoinWgpuFrameView frame{};
  frame.abi_version = COIN_WGPU_BRIDGE_PROTOCOL_REVISION;
  frame.struct_size = sizeof(frame);
  frame.states = &state;
  frame.state_count = 1;
  frame.width = 16;
  frame.height = 16;
  frame.clear_color[3] = 1.0f;
  uint8_t color[16 * 16 * 4] = {};
  float depth[16 * 16] = {};
  CoinWgpuTarget target{};
  target.width = 16;
  target.height = 16;
  target.color_buffer = color;
  target.color_buffer_len = sizeof(color);
  target.depth_buffer = depth;
  target.depth_buffer_len = 16 * 16;
  char error[256] = {};
  const CoinWgpuStatus status = coin_wgpu_submit(&target, &frame, error, sizeof(error));
  const bool lightLimit = check(status == COIN_WGPU_UNSUPPORTED,
                                "FFI accepted light_count=9") &&
                          check(target.submission_serial == 0,
                                "invalid FFI frame was submitted");
  state.light_count = 0;
  CoinWgpuShadowDraw caster{};
  frame.shadow_casters = &caster;
  frame.shadow_caster_count = 1;
  frame.shadow_map_size = 1024;
  frame.shadow_near_distance = 1.0f;
  frame.shadow_far_distance = 10.0f;
  const CoinWgpuStatus shadowStatus =
    coin_wgpu_submit(&target, &frame, error, sizeof(error));
  return lightLimit &&
         check(shadowStatus == COIN_WGPU_INVALID_ARGUMENT,
               "FFI accepted incomplete shadow payload") &&
         check(target.submission_serial == 0,
               "incomplete shadow frame was submitted");
#else
  return true; // Bridge-specific contract is covered only by RUST_BRIDGE.
#endif
}

bool renderGlCenter(SoSeparator * root, std::array<int, 3> & rgb,
                    int sampleX = 32, int sampleY = 32) {
  SoOffscreenRenderer renderer(SbViewportRegion(64, 64));
  renderer.setComponents(SoOffscreenRenderer::RGB);
  renderer.setBackgroundColor(SbColor(0.0f, 0.0f, 0.0f));
  if (!renderer.render(root)) return false;
  const unsigned char * pixels = renderer.getBuffer();
  if (!pixels) return false;
  const size_t index = (static_cast<size_t>(63 - sampleY) * 64u + sampleX) * 3u;
  for (int c = 0; c < 3; ++c) rgb[c] = pixels[index + c];
  return true;
}

bool checkGlReference(const std::array<int, 3> & cpu,
                      const std::array<int, 3> & gpu,
                      const std::array<int, 3> & gl,
                      const char * scene,
                      int tolerance = 12) {
  for (int c = 0; c < 3; ++c) {
    if (std::abs(gl[c] - cpu[c]) > tolerance ||
        (gpuAvailable() &&
         std::abs(gl[c] - gpu[c]) > tolerance)) {
      std::cerr << "Coin/GL mismatch in " << scene << " channel " << c
                << ": GL=" << gl[c] << " CPU=" << cpu[c]
                << " GPU=" << gpu[c] << "\n";
      return false;
    }
  }
  return true;
}
}

// These oracles are independent of both implementations. PHONG is Coin's
// reflectance model; fixed-function GL_SMOOTH interpolates *lit colors*, not
// normals, positions or material properties. In particular, a spotlight whose
// cone misses all vertices does not illuminate the middle of a coarse face.
bool checkGouraudOracles(bool glAvailable) {
  SoSeparator * curved = makeScene(DIRECTIONAL, 1);
  auto * material = static_cast<SoMaterial *>(curved->getChild(3));
  material->diffuseColor.setValue(0.0f, 0.0f, 0.0f);
  material->specularColor.setValue(1.0f, 1.0f, 1.0f);
  material->shininess = 16.0f / 128.0f;
  auto * normals = new SoNormal;
  const SbVec3f curvedNormals[] = {
    SbVec3f(0.8f, 0, 0.6f), SbVec3f(0, 0.8f, 0.6f),
    SbVec3f(-0.8f, 0, 0.6f), SbVec3f(0, -0.8f, 0.6f)
  };
  normals->vector.setValues(0, 4, curvedNormals);
  curved->insertChild(normals, curved->getNumChildren() - 1);
  auto * binding = new SoNormalBinding;
  binding->value = SoNormalBinding::PER_VERTEX_INDEXED;
  curved->insertChild(binding, curved->getNumChildren() - 1);

  SoSeparator * point = makeScene(POINT, 1);
  SoSeparator * spot = makeScene(SPOT_INSIDE, 1);
  SoSeparator * drop = makeScene(SPOT_INSIDE, 1);
  auto * spotLight = static_cast<SoSpotLight *>(drop->getChild(2));
  spotLight->cutOffAngle = 1.2f;
  spotLight->dropOffRate = 1.0f / 128.0f;

  SoSeparator * grazing = makeScene(DIRECTIONAL, 1);
  static_cast<SoDirectionalLight *>(grazing->getChild(2))->direction.setValue(1, 0, 0);
  auto * grazingMaterial = static_cast<SoMaterial *>(grazing->getChild(3));
  grazingMaterial->diffuseColor.setValue(0, 0, 0);
  grazingMaterial->specularColor.setValue(1, 1, 1);
  grazingMaterial->shininess = 0.1f / 128.0f;
  auto * grazingNormal = new SoNormal;
  grazingNormal->vector.setValue(-0.6f, 0, -0.8f);
  grazing->insertChild(grazingNormal, grazing->getNumChildren() - 1);
  auto * grazingBinding = new SoNormalBinding;
  grazingBinding->value = SoNormalBinding::OVERALL;
  grazing->insertChild(grazingBinding, grazing->getNumChildren() - 1);

  // All four vertices have the same distance / angle in these scenes.
  const float pointExpected = 255.0f * 0.8f * 0.8f * 2.0f / std::sqrt(6.0f);
  const float dropExpected = 255.0f * 0.8f * 0.8f / 3.0f;
  const float specularExpected = 255.0f * 0.35f * std::pow(0.6f, 16.0f);
  struct Oracle { SoSeparator * root; float value; const char * name; };
  const Oracle cases[] = {
    {curved, specularExpected, "curved normals: specular before interpolation"},
    {point, pointExpected, "broad point-light face: vertex distance"},
    {spot, 0.0f, "spot cone misses vertices: no fragment relighting"},
    {drop, dropExpected, "spot drop-off: vertex cone factor"},
    {grazing, 0.0f, "negative N.H: no artificial specular floor"}
  };
  for (const auto & oracle : cases) {
    for (bool cpu : {true, false}) {
      if (!cpu && !gpuAvailable()) continue;
      std::array<int, 3> rgb{};
      if (!renderCenter(oracle.root, cpu, rgb)) return false;
      for (int channel = 0; channel < 3; ++channel) {
        if (std::abs(rgb[channel] - oracle.value) > 3.0f) {
          std::cerr << "Gouraud oracle failed: " << oracle.name
                    << " cpu=" << cpu << " actual=" << rgb[channel]
                    << " expected=" << oracle.value << "\n";
          return false;
        }
      }
    }
    if (glAvailable) {
      std::array<int, 3> gl{};
      if (!renderGlCenter(oracle.root, gl)) return false;
      for (int channel = 0; channel < 3; ++channel)
        if (!check(std::abs(gl[channel] - oracle.value) <= 3.0f,
                   oracle.name)) return false;
    }
    std::cout << "Gouraud oracle PASS: " << oracle.name << "\n";
  }
  for (const auto & oracle : cases) oracle.root->unref();
  return true;
}

bool checkCurvedSurface(bool glAvailable) {
  // Real SoSphere primitive callbacks, not just a flat face with supplied normals.
  for (SceneLight kind : {DIRECTIONAL, POINT, SPOT_INSIDE}) {
    SoSeparator * root = makeScene(kind, 1);
    root->removeChild(root->getNumChildren() - 1);
    root->removeChild(root->getNumChildren() - 1);
    auto * material = static_cast<SoMaterial *>(root->getChild(3));
    material->diffuseColor.setValue(0.3f, 0.4f, 0.5f);
    material->specularColor.setValue(0.7f, 0.6f, 0.5f);
    material->shininess = 0.2f;
    auto * complexity = new SoComplexity;
    complexity->value = 0.25f;
    root->addChild(complexity);
    auto * sphere = new SoSphere;
    sphere->radius = 0.65f;
    root->addChild(sphere);
    if (kind == SPOT_INSIDE) {
      auto * light = static_cast<SoSpotLight *>(root->getChild(2));
      light->location.setValue(0.2f, 0.0f, 2.0f);
      light->cutOffAngle = 0.6f;
      light->dropOffRate = 0.1f;
    }
    for (const auto & pixel : {std::array<int, 2>{{24, 32}}, {{40, 32}},
                               {{32, 24}}, {{32, 40}}, {{28, 28}}}) {
      std::array<int, 3> cpu{}, gpu{}, gl{};
      if (!renderCenter(root, true, cpu, nullptr, pixel[0], pixel[1])) return false;
      if (gpuAvailable() && !renderCenter(root, false, gpu, nullptr, pixel[0], pixel[1])) return false;
      for (int channel = 0; channel < 3; ++channel)
        if (gpuAvailable() && !check(std::abs(cpu[channel] - gpu[channel]) <= 3,
                                    "curved sphere CPU/GPU Gouraud mismatch")) return false;
      if (glAvailable && (!renderGlCenter(root, gl, pixel[0], pixel[1]) ||
                         !checkGlReference(cpu, gpu, gl, "curved sphere", 3))) return false;
    }
    root->unref();
    std::cout << "Curved sphere Gouraud PASS: light=" << kind << "\n";
  }
  return true;
}

int main() {
  const bool requireGlReference = std::getenv("COIN_WGPU_REQUIRE_GL_REFERENCE") != nullptr;
  SoDB::init();
  CoinRenderAction::initClass();
  if (requireGlReference &&
      !check(gpuAvailable(),
             "required WebGPU backend unavailable")) return 1;
  if (!checkBridgeLimit()) return 1;

  SoSeparator * zero = makeScene(DIRECTIONAL, 0);
  std::array<int, 3> zeroRgb = {{0, 0, 0}};
  if (!checkParity(zero, zeroRgb) || !check(zeroRgb[0] < 10, "zero lights should be dark")) return 1;
  zero->unref();

  SoSeparator * disabled = makeScene(DIRECTIONAL, 1);
  static_cast<SoDirectionalLight *>(disabled->getChild(2))->on = FALSE;
  std::array<int, 3> disabledRgb = {{0, 0, 0}};
  if (!checkParity(disabled, disabledRgb) || !check(disabledRgb[0] < 10, "disabled light still illuminates")) return 1;
  disabled->unref();

  SoSeparator * one = makeScene(DIRECTIONAL, 1);
  SoSeparator * two = makeScene(DIRECTIONAL, 2);
  std::array<int, 3> oneRgb = {{0, 0, 0}}, twoRgb = {{0, 0, 0}};
  std::array<int, 3> oneGpu = {{0, 0, 0}}, twoGpu = {{0, 0, 0}};
  if (!checkParity(one, oneRgb, &oneGpu) || !checkParity(two, twoRgb, &twoGpu) ||
      !check(twoRgb[0] > oneRgb[0] + 20, "two lights should be brighter than one")) return 1;
  std::array<int, 3> glOne = {{0, 0, 0}};
  const bool glAvailable = renderGlCenter(one, glOne);
  if (requireGlReference && !check(glAvailable, "required Coin/GL reference unavailable")) return 1;
  if (glAvailable) {
    std::array<int, 3> glTwo = {{0, 0, 0}};
    if (!check(renderGlCenter(two, glTwo), "Coin/GL two-light reference unavailable")) return 1;
    std::cout << "Coin/GL reference active; directional CPU=" << oneRgb[0]
              << " GL=" << glOne[0] << "; two lights CPU=" << twoRgb[0]
              << " GL=" << glTwo[0] << "\n";
    if (!checkGlReference(oneRgb, oneGpu, glOne, "directional") ||
        !checkGlReference(twoRgb, twoGpu, glTwo, "two directional lights")) return 1;
  } else {
    std::cout << "[SKIP] Coin/GL offscreen reference unavailable\n";
  }
  if (!checkGouraudOracles(glAvailable) || !checkCurvedSurface(glAvailable)) return 1;
  one->unref();
  two->unref();

  // GL_LIGHT_MODEL_LOCAL_VIEWER defaults to false: even an off-axis
  // fragment sees the eye at +Z infinity, not at the camera origin.
  SoSeparator * specularRef = makeScene(DIRECTIONAL, 1);
  auto * specularMaterial = static_cast<SoMaterial *>(specularRef->getChild(3));
  specularMaterial->diffuseColor.setValue(0.0f, 0.0f, 0.0f);
  specularMaterial->specularColor.setValue(0.9f, 0.5f, 0.2f);
  specularMaterial->shininess = 0.8f;
  std::array<int, 3> specularCenter{}, specularGpu{}, specularGl{};
  if (!checkParity(specularRef, specularCenter, &specularGpu)) return 1;
  if (!check(specularCenter[0] > 70, "specular reference is dark")) return 1;
  if (glAvailable && (!renderGlCenter(specularRef, specularGl) ||
      !checkGlReference(specularCenter, specularGpu, specularGl, "infinite viewer specular", 2))) return 1;
  for (int x : {16, 48}) {
    std::array<int, 3> offAxisCpu{}, offAxisGpu{};
    if (!renderCenter(specularRef, true, offAxisCpu, nullptr, x, 32)) return 1;
    if (gpuAvailable() &&
        !renderCenter(specularRef, false, offAxisGpu, nullptr, x, 32)) return 1;
    for (int channel = 0; channel < 3; ++channel) {
      if (!check(std::abs(offAxisCpu[channel] - specularCenter[channel]) <= 2,
                 "off-axis CPU specular used a local viewer") ||
          (gpuAvailable() &&
           !check(std::abs(offAxisGpu[channel] - specularGpu[channel]) <= 2,
                  "off-axis GPU specular used a local viewer"))) return 1;
    }
  }
  specularRef->unref();

  SoSeparator * ambientRef = makeScene(DIRECTIONAL, 0, SbVec3f(0, 0, 1),
                                       1.0f, 0.0f, 0.1f);
  SoEnvironment * ambientEnvironment = static_cast<SoEnvironment *>(ambientRef->getChild(1));
  ambientEnvironment->ambientIntensity = 0.5f;
  ambientEnvironment->ambientColor.setValue(0.5f, 1.0f, 0.25f);
  SoMaterial * ambientMaterial = static_cast<SoMaterial *>(ambientRef->getChild(2));
  ambientMaterial->ambientColor.setValue(0.4f, 0.4f, 0.4f);
  std::array<int, 3> ambientRgb = {{0, 0, 0}}, ambientGpu = {{0, 0, 0}};
  if (!checkParity(ambientRef, ambientRgb, &ambientGpu) ||
      !check(ambientRgb[1] > ambientRgb[0] && ambientRgb[0] > ambientRgb[2],
             "ambient color/intensity ordering differs")) return 1;
  if (glAvailable) {
    std::array<int, 3> glAmbient = {{0, 0, 0}};
    if (!check(renderGlCenter(ambientRef, glAmbient),
               "Coin/GL ambient reference unavailable")) return 1;
    std::cout << "Ambient CPU=[" << ambientRgb[0] << "," << ambientRgb[1] << ","
              << ambientRgb[2] << "] GL=[" << glAmbient[0] << "," << glAmbient[1]
              << "," << glAmbient[2] << "]\n";
    if (!checkGlReference(ambientRgb, ambientGpu, glAmbient, "ambient")) return 1;
  }
  ambientRef->unref();

  SoSeparator * eight = makeScene(DIRECTIONAL, 8);
  CoinRenderAction record(SbViewportRegion(64, 64));
  record.apply(eight);
  const std::string log = record.getRecordingLog().getString();
  if (!check(record.getLastStatus() == CoinRenderAction::SUCCESS, "eight lights rejected") ||
      !check(log.find("lights=8") != std::string::npos, "eight lights absent from Recording")) return 1;
  std::array<int, 3> eightRgb = {{0, 0, 0}};
  if (!checkParity(eight, eightRgb) || !check(eightRgb[0] > oneRgb[0], "eight lights not accumulated")) return 1;
  eight->unref();


  SoSeparator * nine = makeScene(DIRECTIONAL, 9);
  record.apply(nine);
  if (!check(record.getLastStatus() == CoinRenderAction::UNSUPPORTED, "nine lights not rejected")) return 1;
  nine->unref();

  SoSeparator * pointConstant = makeScene(POINT, 1);
  SoSeparator * pointLinear = makeScene(POINT, 1, SbVec3f(0, 1, 0));
  std::array<int, 3> pointRgb = {{0, 0, 0}}, linearRgb = {{0, 0, 0}};
  if (!checkParity(pointConstant, pointRgb) || !checkParity(pointLinear, linearRgb) ||
      !check(pointRgb[0] > linearRgb[0] + 20, "point attenuation has no effect")) return 1;
  if (glAvailable) {
    // Broad faces deliberately exercise Gouraud positional-light interpolation.
    SoSeparator * pointRefConstant = makeScene(POINT, 1);
    SoSeparator * pointRefLinear = makeScene(POINT, 1, SbVec3f(0, 1, 0));
    std::array<int, 3> refConstant = {{0, 0, 0}}, refLinear = {{0, 0, 0}};
    std::array<int, 3> gpuConstant = {{0, 0, 0}}, gpuLinear = {{0, 0, 0}};
    std::array<int, 3> glConstant = {{0, 0, 0}}, glLinear = {{0, 0, 0}};
    const bool glPointAvailable =
      checkParity(pointRefConstant, refConstant, &gpuConstant) &&
      checkParity(pointRefLinear, refLinear, &gpuLinear) &&
      renderGlCenter(pointRefConstant, glConstant) && renderGlCenter(pointRefLinear, glLinear);
    pointRefConstant->unref();
    pointRefLinear->unref();
    if (requireGlReference && !check(glPointAvailable, "required Coin/GL point reference unavailable")) return 1;
    if (glPointAvailable) {
      std::cout << "Point constant CPU=" << refConstant[0] << " GL=" << glConstant[0]
                << "; linear CPU=" << refLinear[0] << " GL=" << glLinear[0] << "\n";
      if (!check(glConstant[0] > glLinear[0] + 10,
                 "Coin/GL point attenuation ordering differs") ||
          !checkGlReference(refConstant, gpuConstant, glConstant, "point constant", 16) ||
          !checkGlReference(refLinear, gpuLinear, glLinear, "point linear", 16)) return 1;
    } else {
      std::cout << "[SKIP] Coin/GL point reference unavailable\n";
    }
  }
  // A later environment changes ambient state, but not attenuation of an existing light.
  SoSeparator * environmentAfterLight = makeScene(POINT, 1);
  SoEnvironment * laterEnvironment = new SoEnvironment;
  laterEnvironment->ambientIntensity = 0.0f;
  laterEnvironment->attenuation = SbVec3f(0.0f, 1.0f, 0.0f);
  environmentAfterLight->insertChild(laterEnvironment, 3);
  std::array<int, 3> afterRgb = {{0, 0, 0}};
  if (!checkParity(environmentAfterLight, afterRgb) ||
      !check(std::abs(afterRgb[0] - pointRgb[0]) <= 10,
             "later environment changed existing point light attenuation")) return 1;
  environmentAfterLight->unref();

  // Two lights retain the attenuation active at their respective traversal points.
  SoSeparator * mixedAttenuation = makeScene(POINT, 1);
  SoEnvironment * changedEnvironment = new SoEnvironment;
  changedEnvironment->ambientIntensity = 0.0f;
  changedEnvironment->attenuation = SbVec3f(0.0f, 1.0f, 0.0f);
  mixedAttenuation->insertChild(changedEnvironment, 3);
  SoPointLight * secondPoint = new SoPointLight;
  secondPoint->location.setValue(0.0f, 0.0f, 2.0f);
  secondPoint->intensity = 0.8f;
  mixedAttenuation->insertChild(secondPoint, 4);
  record.apply(mixedAttenuation);
  const std::string mixedLog = record.getRecordingLog().getString();
  if (!check(record.getLastStatus() == CoinRenderAction::SUCCESS,
             "two lights with different attenuation rejected") ||
      !check(mixedLog.find("attenuation=[0.0000,0.0000,1.0000]") != std::string::npos &&
             mixedLog.find("attenuation=[0.0000,1.0000,0.0000]") != std::string::npos,
             "per-light attenuation not preserved in frame plan")) return 1;
  std::array<int, 3> mixedRgb = {{0, 0, 0}};
  if (!checkParity(mixedAttenuation, mixedRgb) ||
      !check(mixedRgb[0] > pointRgb[0] + 10,
             "second point light did not contribute")) return 1;
  mixedAttenuation->unref();
  pointConstant->unref();
  pointLinear->unref();

  SoSeparator * degenerateAttenuation = makeScene(POINT, 1, SbVec3f(0, 0, 0));
  record.apply(degenerateAttenuation);
  if (!check(record.getLastStatus() == CoinRenderAction::INVALID_SCENE,
             "degenerate point attenuation was not rejected")) return 1;
  degenerateAttenuation->unref();

  SoSeparator * inside = makeScene(SPOT_INSIDE, 1, SbVec3f(0, 0, 1), 1.0f, 0.0f, 0.1f);
  SoSeparator * outside = makeScene(SPOT_OUTSIDE, 1, SbVec3f(0, 0, 1), 1.0f, 0.0f, 0.1f);
  std::array<int, 3> insideRgb = {{0, 0, 0}}, outsideRgb = {{0, 0, 0}};
  if (!checkParity(inside, insideRgb) || !checkParity(outside, outsideRgb) ||
      !check(insideRgb[0] > outsideRgb[0] + 20, "spot cone has no effect")) return 1;
  if (glAvailable) {
    SoSeparator * spotRefInside = makeScene(SPOT_INSIDE, 1, SbVec3f(0, 0, 1), 1.0f, 0.0f, 0.1f);
    SoSeparator * spotRefOutside = makeScene(SPOT_OUTSIDE, 1, SbVec3f(0, 0, 1), 1.0f, 0.0f, 0.1f);
    std::array<int, 3> refInside = {{0, 0, 0}}, refOutside = {{0, 0, 0}};
    std::array<int, 3> gpuInside = {{0, 0, 0}}, gpuOutside = {{0, 0, 0}};
    std::array<int, 3> glInside = {{0, 0, 0}}, glOutside = {{0, 0, 0}};
    const bool glSpotAvailable =
      checkParity(spotRefInside, refInside, &gpuInside) &&
      checkParity(spotRefOutside, refOutside, &gpuOutside) &&
      renderGlCenter(spotRefInside, glInside) && renderGlCenter(spotRefOutside, glOutside);
    spotRefInside->unref();
    spotRefOutside->unref();
    if (requireGlReference && !check(glSpotAvailable, "required Coin/GL spot reference unavailable")) return 1;
    if (glSpotAvailable) {
      std::cout << "Spot inside CPU=" << refInside[0] << " GL=" << glInside[0]
                << "; outside CPU=" << refOutside[0] << " GL=" << glOutside[0] << "\n";
      if (!check(glInside[0] > glOutside[0] + 10,
                 "Coin/GL spot cone ordering differs") ||
          !checkGlReference(refInside, gpuInside, glInside, "spot inside", 16) ||
          !checkGlReference(refOutside, gpuOutside, glOutside, "spot outside", 16)) return 1;
    }
  }
  inside->unref();
  outside->unref();

  SoSeparator * spotHard = makeScene(SPOT_INSIDE, 1, SbVec3f(0, 0, 1), 1.0f, 0.0f, 0.1f);
  SoSeparator * spotSoft = makeScene(SPOT_INSIDE, 1, SbVec3f(0, 0, 1), 1.0f, 0.0f, 0.1f);
  SoSpotLight * hardLight = static_cast<SoSpotLight *>(spotHard->getChild(2));
  SoSpotLight * softLight = static_cast<SoSpotLight *>(spotSoft->getChild(2));
  hardLight->location.setValue(0.3f, 0.0f, 1.0f);
  softLight->location.setValue(0.3f, 0.0f, 1.0f);
  hardLight->cutOffAngle = 0.6f;
  softLight->cutOffAngle = 0.6f;
  softLight->dropOffRate = 1.0f;
  std::array<int, 3> spotHardRgb = {{0, 0, 0}}, spotSoftRgb = {{0, 0, 0}};
  if (!checkParity(spotHard, spotHardRgb) ||
      !checkParity(spotSoft, spotSoftRgb) ||
      !check(spotHardRgb[0] > spotSoftRgb[0] + 20,
             "spot dropOffRate has no effect")) return 1;
  spotHard->unref();
  spotSoft->unref();

  if (glAvailable) {
    SoSeparator * hardRef = makeScene(SPOT_INSIDE, 1, SbVec3f(0, 0, 1),
                                      1.0f, 0.0f, 0.1f);
    SoSeparator * softRef = makeScene(SPOT_INSIDE, 1, SbVec3f(0, 0, 1),
                                      1.0f, 0.0f, 0.1f);
    SoSpotLight * hardRefLight = static_cast<SoSpotLight *>(hardRef->getChild(2));
    SoSpotLight * softRefLight = static_cast<SoSpotLight *>(softRef->getChild(2));
    hardRefLight->location.setValue(0.2f, 0.0f, 2.0f);
    softRefLight->location.setValue(0.2f, 0.0f, 2.0f);
    hardRefLight->cutOffAngle = 0.6f;
    softRefLight->cutOffAngle = 0.6f;
    softRefLight->dropOffRate = 1.0f;
    std::array<int, 3> refHard = {{0, 0, 0}}, refSoft = {{0, 0, 0}};
    std::array<int, 3> gpuHard = {{0, 0, 0}}, gpuSoft = {{0, 0, 0}};
    std::array<int, 3> glHard = {{0, 0, 0}}, glSoft = {{0, 0, 0}};
    const bool glDropoffAvailable =
      checkParity(hardRef, refHard, &gpuHard) &&
      checkParity(softRef, refSoft, &gpuSoft) &&
      renderGlCenter(hardRef, glHard) && renderGlCenter(softRef, glSoft);
    hardRef->unref();
    softRef->unref();
    if (requireGlReference &&
        !check(glDropoffAvailable, "required Coin/GL spot dropoff reference unavailable")) return 1;
    if (glDropoffAvailable) {
      std::cout << "Spot dropoff hard CPU=" << refHard[0] << " GL=" << glHard[0]
                << "; soft CPU=" << refSoft[0] << " GL=" << glSoft[0] << "\n";
      if (!check(glHard[0] > glSoft[0] + 20,
                 "Coin/GL spot dropoff ordering differs") ||
          !checkGlReference(refHard, gpuHard, glHard, "spot no dropoff", 16) ||
          !checkGlReference(refSoft, gpuSoft, glSoft, "spot full dropoff", 16)) return 1;
    }
  }

  SoSeparator * scaled = makeScene(DIRECTIONAL, 1, SbVec3f(0, 0, 1), 0.5f);
  std::array<int, 3> scaledRgb = {{0, 0, 0}}, scaledGpu = {{0, 0, 0}};
  if (!checkParity(scaled, scaledRgb, &scaledGpu)) return 1;
  if (glAvailable) {
    std::array<int, 3> glScaled = {{0, 0, 0}};
    if (!check(renderGlCenter(scaled, glScaled),
               "Coin/GL nonuniform-scale reference unavailable")) return 1;
    std::cout << "Nonuniform normal CPU=" << scaledRgb[0]
              << " GL=" << glScaled[0] << "\n";
    if (!checkGlReference(scaledRgb, scaledGpu, glScaled,
                          "nonuniform normal")) return 1;
  }
  scaled->unref();
  SoSeparator * rotatedCamera = makeScene(POINT, 1, SbVec3f(0, 0, 1), 1.0f, 0.75f);
  std::array<int, 3> rotatedRgb = {{0, 0, 0}};
  if (!checkParity(rotatedCamera, rotatedRgb) || !check(rotatedRgb[0] > 20, "rotated camera lost point light")) return 1;
  rotatedCamera->unref();


  // A negative determinant reverses face winding; SoShapeHints keeps CCW as front.
  SoSeparator * frontFacing = makeScene(DIRECTIONAL, 1);
  SoShapeHints * frontHints = new SoShapeHints;
  frontHints->vertexOrdering = SoShapeHints::COUNTERCLOCKWISE;
  frontHints->shapeType = SoShapeHints::SOLID;
  frontFacing->insertChild(frontHints, 4);
  std::array<int, 3> frontRgb = {{0, 0, 0}};
  if (!checkParity(frontFacing, frontRgb) ||
      !check(frontRgb[0] > 20, "front-facing solid disappeared")) return 1;
  frontFacing->unref();

  SoSeparator * reflected = makeScene(DIRECTIONAL, 1);
  SoScale * reflection = new SoScale;
  reflection->scaleFactor.setValue(-1.0f, 1.0f, 1.0f);
  reflected->insertChild(reflection, 4);
  SoShapeHints * reflectedHints = new SoShapeHints;
  reflectedHints->vertexOrdering = SoShapeHints::COUNTERCLOCKWISE;
  reflectedHints->shapeType = SoShapeHints::SOLID;
  reflected->insertChild(reflectedHints, 5);
  std::array<int, 3> reflectedRgb = {{0, 0, 0}};
  if (!checkParity(reflected, reflectedRgb) ||
      !check(reflectedRgb[0] < 10,
             "reflection failed to reverse winding under back-face culling")) return 1;
  reflected->unref();

  // Directional, point and spot contributions coexist in one draw.
  SoSeparator * combined = makeScene(DIRECTIONAL, 1);
  SoPointLight * combinedPoint = new SoPointLight;
  combinedPoint->location.setValue(0.0f, 0.0f, 2.0f);
  combinedPoint->intensity = 0.35f;
  combined->insertChild(combinedPoint, 3);
  SoSpotLight * combinedSpot = new SoSpotLight;
  combinedSpot->location.setValue(0.0f, 0.0f, 1.0f);
  combinedSpot->direction.setValue(0.0f, 0.0f, -1.0f);
  combinedSpot->cutOffAngle = 0.25f;
  combinedSpot->intensity = 0.35f;
  combined->insertChild(combinedSpot, 4);
  std::array<int, 3> combinedRgb = {{0, 0, 0}};
  if (!checkParity(combined, combinedRgb) ||
      !check(combinedRgb[0] > oneRgb[0] + 25,
             "mixed light types did not accumulate")) return 1;
  combined->unref();

  SoSeparator * singular = makeScene(DIRECTIONAL, 1, SbVec3f(0, 0, 1), 0.0f);
  record.apply(singular);
  if (!check(record.getLastStatus() == CoinRenderAction::SUCCESS,
             "singular normal matrix did not use the finite identity fallback")) return 1;
  singular->unref();

  std::cout << "CoinRenderLightingTest passed\n";
  return 0;
}
