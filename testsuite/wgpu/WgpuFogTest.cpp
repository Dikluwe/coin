#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#ifdef HAVE_WGPU_BGFX
#include <Inventor/actions/SoBGFXRenderAction.h>
#define FogRenderAction SoBGFXRenderAction
#else
#define FogRenderAction SoWgpuRenderAction
#endif
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTranslation.h>
#include "rendering/wgpu/SoWgpuCpuReferenceBackend.h"
#include "rendering/wgpu/SoWgpuRenderTargetP.h"
#include "rendering/wgpu/coin_wgpu_ffi.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
struct Sample {
  std::array<int, 4> rgba = {{0, 0, 0, 0}};
  float depth = 1.0f;
  bool hasDepth = false;
};

bool check(bool ok, const char * message) {
  if (!ok) std::cerr << "WgpuFogTest: " << message << "\n";
  return ok;
}

SoSeparator * makeQuad(float xCenter, float z) {
  SoSeparator * group = new SoSeparator;
  SoTranslation * translation = new SoTranslation;
  translation->translation.setValue(xCenter, 0.0f, 0.0f);
  group->addChild(translation);
  SoCoordinate3 * coords = new SoCoordinate3;
  coords->point.set1Value(0, SbVec3f(-0.35f, -0.35f, z));
  coords->point.set1Value(1, SbVec3f( 0.35f, -0.35f, z));
  coords->point.set1Value(2, SbVec3f( 0.35f,  0.35f, z));
  coords->point.set1Value(3, SbVec3f(-0.35f,  0.35f, z));
  group->addChild(coords);
  SoIndexedFaceSet * face = new SoIndexedFaceSet;
  const int32_t indices[] = {0, 1, 2, 3, -1};
  face->coordIndex.setValues(0, 5, indices);
  group->addChild(face);
  return group;
}

SoEnvironment * makeEnvironment(SoEnvironment::FogType mode, float visibility) {
  SoEnvironment * environment = new SoEnvironment;
  environment->ambientIntensity = 0.0f;
  environment->fogType = mode;
  environment->fogColor.setValue(0.0f, 0.0f, 1.0f);
  environment->fogVisibility = visibility;
  return environment;
}

SoSeparator * makeScene(SoEnvironment::FogType mode, bool perspective,
                        float geometryZ = 0.0f, float visibility = 10.0f,
                        float farDistance = 10.0f) {
  SoSeparator * root = new SoSeparator;
  root->ref();
  if (perspective) {
    SoPerspectiveCamera * camera = new SoPerspectiveCamera;
    camera->position.setValue(0.0f, 0.0f, 5.0f);
    camera->nearDistance = 0.1f;
    camera->farDistance = farDistance;
    root->addChild(camera);
  } else {
    SoOrthographicCamera * camera = new SoOrthographicCamera;
    camera->position.setValue(0.0f, 0.0f, 5.0f);
    camera->height = 2.0f;
    camera->nearDistance = 0.1f;
    camera->farDistance = farDistance;
    root->addChild(camera);
  }
  root->addChild(makeEnvironment(mode, visibility));
  SoLightModel * lightModel = new SoLightModel;
  lightModel->model = SoLightModel::BASE_COLOR;
  root->addChild(lightModel);
  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(1.0f, 0.0f, 0.0f);
  root->addChild(material);
  root->addChild(makeQuad(0.0f, geometryZ));
  return root;
}

SoSeparator * makeTexturedScene() {
  SoSeparator * root = makeScene(SoEnvironment::HAZE, false);
  static_cast<SoMaterial *>(root->getChild(3))->diffuseColor.setValue(1.0f, 1.0f, 1.0f);
  SoSeparator * quad = static_cast<SoSeparator *>(root->getChild(4));
  SoTexture2 * texture = new SoTexture2;
  texture->model = SoTexture2::MODULATE;
  unsigned char green[4 * 4 * 3];
  for (int i = 0; i < 16; ++i) {
    green[3 * i] = 0;
    green[3 * i + 1] = 255;
    green[3 * i + 2] = 0;
  }
  texture->image.setValue(SbVec2s(4, 4), 3, green);
  quad->insertChild(texture, 1);
  SoTextureCoordinate2 * uv = new SoTextureCoordinate2;
  uv->point.set1Value(0, SbVec2f(0.0f, 0.0f));
  uv->point.set1Value(1, SbVec2f(1.0f, 0.0f));
  uv->point.set1Value(2, SbVec2f(1.0f, 1.0f));
  uv->point.set1Value(3, SbVec2f(0.0f, 1.0f));
  quad->insertChild(uv, 2);
  SoIndexedFaceSet * face = static_cast<SoIndexedFaceSet *>(quad->getChild(4));
  const int32_t indices[] = {0, 1, 2, 3, -1};
  face->textureCoordIndex.setValues(0, 5, indices);
  return root;
}

bool renderAt(SoSeparator * scene, bool cpu, int x, int y, Sample & out) {
  SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
  if (!check(target != NULL, "createOffscreen failed")) return false;
  if (cpu) target->getPimpl()->backend.reset(new SoWgpuCpuReferenceBackend);
  if (!cpu) target->setDepthReadbackEnabled(FALSE);
  FogRenderAction action(SbViewportRegion(64, 64));
  action.setRenderTarget(target);
  action.apply(scene);
  const bool success = action.getLastStatus() == SoWgpuRenderAction::SUCCESS;
  if (!success) std::cerr << "Render error: " << action.getLastError().getString() << "\n";
  std::vector<uint8_t> pixels;
  std::vector<float> depth;
  if (success) {
    target->readbackRGBA(pixels);
    target->readbackDepth(depth);
  }
  delete target;
  if (!check(success && pixels.size() == 64u * 64u * 4u &&
             (!cpu || depth.size() == 64u * 64u), "color/depth readback failed")) return false;
  size_t sample = (x >= 0 && y >= 0) ? static_cast<size_t>(y * 64 + x) : 64u * 64u;
  if (x < 0 || y < 0) {
    for (int py = 29; py <= 35 && sample == 64u * 64u; ++py) {
      for (int px = 29; px <= 35; ++px) {
        const size_t candidate = static_cast<size_t>(py * 64 + px);
        if (depth.empty() ? (pixels[candidate*4] || pixels[candidate*4+1] || pixels[candidate*4+2]) : depth[candidate] < 0.999f) {
          sample = candidate;
          break;
        }
      }
    }
  }

  if (!check(sample < 64u * 64u, "no primitive in center region")) return false;
  for (int c = 0; c < 4; ++c) out.rgba[c] = pixels[sample * 4u + c];
  out.hasDepth = !depth.empty();
  if (out.hasDepth) out.depth = depth[sample];
  return !out.hasDepth || check(std::isfinite(out.depth) && out.depth >= 0.0f && out.depth < 1.0f,
               "invalid or missing center depth");
}

bool renderGlAt(SoSeparator * scene, int x, int y, Sample & out) {
  SoOffscreenRenderer renderer(SbViewportRegion(64, 64));
  renderer.setComponents(SoOffscreenRenderer::RGB);
  renderer.setBackgroundColor(SbColor(0.0f, 0.0f, 0.0f));
  if (!renderer.render(scene)) return false;
  const unsigned char * pixels = renderer.getBuffer();
  if (!pixels) return false;
  const size_t sample = static_cast<size_t>(y * 64 + x) * 3u;
  for (int c = 0; c < 3; ++c) out.rgba[c] = pixels[sample + c];
  return true;
}

bool compare(SoSeparator * scene, int x, int y, Sample & cpu,
             bool glAvailable, int tolerance = 18) {
  if (!renderAt(scene, true, x, y, cpu)) return false;
  Sample gpu;
  const bool hasGpu = FogRenderAction::isGpuBackendAvailable();
  if (hasGpu) {
    if (!renderAt(scene, false, x, y, gpu)) return false;
    for (int c = 0; c < 4; ++c) {
      if (!check(std::abs(cpu.rgba[c] - gpu.rgba[c]) <= tolerance,
                 "CPU/GPU fog color mismatch")) return false;
    }
    if (gpu.hasDepth && !check(std::abs(cpu.depth - gpu.depth) <= 0.025f,
               "CPU/GPU fog depth mismatch")) return false;
  }
  if (glAvailable) {
    Sample gl;
    if (!check(renderGlAt(scene, x, y, gl), "Coin/GL fog render failed")) return false;
    for (int c = 0; c < 3; ++c) {
      if (hasGpu && std::abs(gpu.rgba[c] - gl.rgba[c]) > tolerance) {
        std::cerr << "Fog GL mismatch channel " << c << ": GPU="
                  << gpu.rgba[c] << " GL=" << gl.rgba[c] << "\n";
        return false;
      }
      if (std::abs(cpu.rgba[c] - gl.rgba[c]) > tolerance) {
        std::cerr << "Fog GL mismatch channel " << c << ": CPU="
                  << cpu.rgba[c] << " GL=" << gl.rgba[c] << "\n";
        return false;
      }
    }
  }
  return check(cpu.rgba[3] == 255, "fog modified alpha");
}

bool checkInvalidBridgeFog() {
#if defined(HAVE_WGPU_RUST_BRIDGE)
  if (!FogRenderAction::isGpuBackendAvailable()) return true;
  CoinWgpuRenderState state{};
  state.fog_mode = 4;
  state.fog_end = 10.0f;
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
  return check(status == COIN_WGPU_INVALID_ARGUMENT, "FFI accepted invalid fog mode") &&
         check(target.submission_serial == 0, "invalid fog frame was submitted");
#else
  return true; // The CPU/Recording path has no Rust FFI submission.
#endif
}
}

int main() {
  const bool requireGl = std::getenv("COIN_WGPU_REQUIRE_GL_REFERENCE") != NULL;
  SoDB::init();
  FogRenderAction::initClass();
  if (requireGl &&
      !check(FogRenderAction::isGpuBackendAvailable(), "required GPU unavailable")) return 1;
  if (!checkInvalidBridgeFog()) return 1;

  SoSeparator * none = makeScene(SoEnvironment::NONE, true);
  Sample glProbe;
  const bool glAvailable = renderGlAt(none, 32, 32, glProbe);
  if (requireGl && !check(glAvailable, "required Coin/GL offscreen unavailable")) return 1;
  if (!glAvailable) std::cout << "[SKIP] Coin/GL fog reference unavailable\n";

  const SoEnvironment::FogType modes[] = {
    SoEnvironment::NONE, SoEnvironment::HAZE,
    SoEnvironment::FOG, SoEnvironment::SMOKE
  };
  Sample perspective[4], orthographic[4];
  for (int i = 0; i < 4; ++i) {
    SoSeparator * scene = makeScene(modes[i], true);
    if (!compare(scene, 32, 32, perspective[i], glAvailable)) return 1;
    scene->unref();
    SoSeparator * ortho = makeScene(modes[i], false);
    if (!compare(ortho, 32, 32, orthographic[i], glAvailable)) return 1;
    ortho->unref();
    for (int c = 0; c < 3; ++c) {
      if (!check(std::abs(perspective[i].rgba[c] - orthographic[i].rgba[c]) <= 4,
                 "fog depends on projection depth instead of view distance")) return 1;
    }
  }
  if (!check(perspective[0].rgba[0] > 230 && perspective[0].rgba[2] < 10,
             "NONE changed base color") ||
      !check(perspective[1].rgba[0] > perspective[3].rgba[0] &&
             perspective[3].rgba[0] > perspective[2].rgba[0],
             "HAZE/FOG/SMOKE ordering differs") ||
      !check(perspective[2].rgba[2] > perspective[3].rgba[2] &&
             perspective[3].rgba[2] > perspective[1].rgba[2],
             "fog color ordering differs")) return 1;
  none->unref();

  SoSeparator * textured = makeTexturedScene();
  Sample texturedSample;
  if (!compare(textured, 32, 32, texturedSample, glAvailable) ||
      !check(texturedSample.rgba[0] < 10 &&
             texturedSample.rgba[1] > 80 && texturedSample.rgba[2] > 80,
             "fog must blend RGB after texture modulation")) return 1;
  textured->unref();

  SoSeparator * nearScene = makeScene(SoEnvironment::HAZE, true, 2.0f);
  SoSeparator * farScene = makeScene(SoEnvironment::HAZE, true, -2.0f);
  Sample nearSample, farSample;
  if (!compare(nearScene, 32, 32, nearSample, glAvailable) ||
      !compare(farScene, 32, 32, farSample, glAvailable) ||
      !check(nearSample.rgba[0] > farSample.rgba[0] + 40,
             "fog ignored view-space distance")) return 1;
  nearScene->unref();
  farScene->unref();

  SoSeparator * fallback = makeScene(SoEnvironment::HAZE, true, 0.0f, 0.0f);
  SoSeparator * longFar = makeScene(SoEnvironment::HAZE, true, 0.0f, 10.0f, 100.0f);
  Sample fallbackSample, longFarSample;
  if (!compare(fallback, 32, 32, fallbackSample, glAvailable) ||
      !compare(longFar, 32, 32, longFarSample, glAvailable) ||
      !check(std::abs(fallbackSample.rgba[0] - perspective[1].rgba[0]) <= 4,
             "fogVisibility=0 did not inherit camera far distance") ||
      !check(std::abs(longFarSample.rgba[0] - perspective[1].rgba[0]) <= 4,
             "fog used normalized depth instead of view distance")) return 1;
  fallback->unref();
  longFar->unref();

  SoSeparator * changed = makeScene(SoEnvironment::HAZE, false);
  changed->removeChild(4);
  changed->addChild(makeQuad(-0.5f, 0.0f));
  changed->addChild(makeEnvironment(SoEnvironment::NONE, 10.0f));
  changed->addChild(makeQuad(0.5f, 0.0f));
  SoWgpuRenderAction record(SbViewportRegion(64, 64));
  record.apply(changed);
  const std::string log = record.getRecordingLog().getString();
  if (!check(record.getLastStatus() == SoWgpuRenderAction::SUCCESS,
             "fog draw-state capture failed") ||
      !check(log.find("fogMode=1") != std::string::npos &&
             log.find("fogMode=0") != std::string::npos,
             "fog state leaked or was deduplicated across draws")) return 1;
  Sample left, right;
  if (!compare(changed, 16, 32, left, glAvailable) ||
      !compare(changed, 48, 32, right, glAvailable) ||
      !check(left.rgba[2] > 80 && right.rgba[0] > 230 &&
             right.rgba[2] < 10, "fog state leaked into following draw")) return 1;
  changed->unref();

  for (int topology = 0; topology < 2; ++topology) {
    SoSeparator * clearScene = makeScene(SoEnvironment::NONE, false);
    SoSeparator * fogScene = makeScene(SoEnvironment::HAZE, false);
    SoSeparator * scenes[] = {clearScene, fogScene};
    for (int sceneIndex = 0; sceneIndex < 2; ++sceneIndex) {
      SoSeparator * scene = scenes[sceneIndex];
      scene->removeChild(4);
      SoCoordinate3 * coords = new SoCoordinate3;
      if (topology == 0) {
        coords->point.set1Value(0, SbVec3f(-0.8f, 0.0f, 0.0f));
        coords->point.set1Value(1, SbVec3f( 0.8f, 0.0f, 0.0f));
        scene->addChild(coords);
        SoLineSet * lines = new SoLineSet;
        const int32_t count[] = {2};
        lines->numVertices.setValues(0, 1, count);
        scene->addChild(lines);
      } else {
        coords->point.set1Value(0, SbVec3f(0.0f, 0.0f, 0.0f));
        scene->addChild(coords);
        SoPointSet * points = new SoPointSet;
        points->numPoints = 1;
        scene->addChild(points);
      }
    }
    Sample clearSample, fogSample;
    if (!compare(clearScene, -1, -1, clearSample, false) ||
        !compare(fogScene, -1, -1, fogSample, false) ||
        !check(clearSample.rgba[0] > fogSample.rgba[0] + 40 &&
               fogSample.rgba[2] > clearSample.rgba[2] + 40,
               "fog not applied to line or point topology")) return 1;
    clearScene->unref();
    fogScene->unref();
  }

  std::cout << "WgpuFogTest passed\n";
  return 0;
}
