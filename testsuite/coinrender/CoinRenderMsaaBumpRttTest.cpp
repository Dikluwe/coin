#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoRotation.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

struct Snapshot { std::vector<uint8_t> color; std::vector<float> depth; };
struct Fixture { SoSeparator * parent; SoSeparator * child; SoDirectionalLight * light; };

static SoOrthographicCamera * camera(float height) {
  auto * result = new SoOrthographicCamera;
  result->position.setValue(0, 0, 4);
  result->height = height;
  result->nearDistance = .1f;
  result->farDistance = 10;
  return result;
}

static Fixture makeFixture() {
  auto * child = new SoSeparator; child->ref();
  child->addChild(camera(4));
  auto * light = new SoDirectionalLight;
  light->direction.setValue(0, 0, -1);
  child->addChild(light);
  auto * hints = new SoShapeHints;
  hints->shapeType = SoShapeHints::UNKNOWN_SHAPE_TYPE;
  child->addChild(hints);
  auto * material = new SoMaterial;
  material->diffuseColor.setValue(.8f, .2f, .1f);
  child->addChild(material);
  auto * bump = new SoBumpMap;
  const unsigned char normal[] = {128, 128, 255};
  bump->image.setValue(SbVec2s(1, 1), 3, normal);
  child->addChild(bump);
  auto * rotation = new SoRotation;
  rotation->rotation.setValue(SbVec3f(0, 0, 1), .23f);
  child->addChild(rotation);
  child->addChild(new SoCube);

  auto * texture = new SoSceneTexture2;
  texture->size.setValue(32, 32);
  texture->type.setValue(SoSceneTexture2::RGBA8);
  texture->transparencyFunction.setValue(SoSceneTexture2::NONE);
  texture->backgroundColor.setValue(0, 0, 1, 1);
  texture->scene.setValue(child);
  auto * parent = new SoSeparator; parent->ref();
  parent->addChild(camera(2));
  auto * base = new SoLightModel; base->model = SoLightModel::BASE_COLOR;
  parent->addChild(base);
  auto * white = new SoMaterial; white->diffuseColor.setValue(1, 1, 1);
  parent->addChild(white);
  parent->addChild(texture);
  auto * uv = new SoTextureCoordinate2;
  uv->point.set1Value(0, 0, 0); uv->point.set1Value(1, 1, 0);
  uv->point.set1Value(2, 1, 1); uv->point.set1Value(3, 0, 1);
  parent->addChild(uv);
  auto * points = new SoCoordinate3;
  points->point.set1Value(0, -.8f, -.8f, 0);
  points->point.set1Value(1, .8f, -.8f, 0);
  points->point.set1Value(2, .8f, .8f, 0);
  points->point.set1Value(3, -.8f, .8f, 0);
  parent->addChild(points);
  auto * quad = new SoIndexedFaceSet;
  const int32_t indices[] = {0, 1, 2, 3, -1};
  quad->coordIndex.setValues(0, 5, indices);
  quad->textureCoordIndex.setValues(0, 5, indices);
  parent->addChild(quad);
  return {parent, child, light};
}

static void release(Fixture & f) { f.parent->unref(); f.child->unref(); }

static bool read(CoinRenderTarget * target, Snapshot & result) {
  target->readbackRGBA(result.color);
  target->readbackDepth(result.depth);
  return result.color.size() == 64u * 64u * 4u && result.depth.size() == 64u * 64u;
}

static bool depthCoverage(const Snapshot & shot, uint32_t samples,
                          CoinRenderSceneTextureMode mode, float & plane) {
  if (shot.color.size() != 64u * 64u * 4u || shot.depth.size() != 64u * 64u)
    return false;
  plane = shot.depth[32u * 64u + 32u];
  if (!(plane > 0.0f && plane < 1.0f)) return false;
  size_t covered = 0, empty = 0, partial = 0;
  for (size_t i = 0; i < shot.depth.size(); ++i) {
    const uint8_t alpha = shot.color[4u * i + 3u];
    if (alpha == 0) {
      ++empty;
      if (shot.depth[i] != 1.0f) {
        std::fprintf(stderr, "bump RTT depth background mismatch samples=%u xy=(%zu,%zu) value=%.9g\n",
                     samples, i%64u, i/64u, shot.depth[i]);
        return false;
      }
    } else {
      ++covered;
      if (alpha < 255) ++partial;
      if (shot.depth[i] != plane) {
        std::fprintf(stderr, "bump RTT depth plane mismatch samples=%u xy=(%zu,%zu) alpha=%u value=%.9g plane=%.9g\n",
                     samples, i%64u, i/64u, unsigned(alpha), shot.depth[i], plane);
        return false;
      }
    }
  }
  std::printf("msaa_bump_rtt coverage samples=%u mode=%s covered=%zu empty=%zu partial=%zu depth=%.9g\n",
              samples, mode == COIN_RENDER_SCENE_TEXTURE_DIRECT ? "direct" : "staged",
              covered, empty, partial, plane);
  return covered > 0 && empty > 0 && (samples == 1 || partial > 0);
}

static bool capture(Fixture & fixture, uint32_t samples,
                    CoinRenderSceneTextureMode mode, Snapshot & result) {
  CoinRenderOptions options;
  options.sampleCount = samples;
  options.sceneTexture = mode;
  std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createOffscreen(SbVec2i32(64, 64), options));
  if (!target || !target->setDepthReadbackEnabled(TRUE)) return false;
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setTransparencyType(CoinRenderAction::NONE);
  action.setBackgroundColor(SbColor4f(0, 0, 0, 0));
  action.setRenderTarget(target.get());
  CoinWgpuPerformanceStats before{};
  coin_wgpu_get_performance_stats(&before);
  action.apply(fixture.parent);
  const bool pixels = read(target.get(), result);
  CoinWgpuPerformanceStats after{};
  coin_wgpu_get_performance_stats(&after);
  float plane = 1.0f;
  const bool passed = action.getLastStatus() == CoinRenderAction::SUCCESS &&
      pixels && target->getLastSubmissionSerial() > 0 &&
      depthCoverage(result, samples, mode, plane) &&
      (mode != COIN_RENDER_SCENE_TEXTURE_DIRECT ||
       after.texture_uploaded_bytes == before.texture_uploaded_bytes);
  std::printf("msaa_bump_rtt capture samples=%u mode=%s passed=%d status=%d uploaded_delta=%llu serial=%llu error=%s\n",
              samples, mode == COIN_RENDER_SCENE_TEXTURE_DIRECT ? "direct" : "staged",
              int(passed), int(action.getLastStatus()),
              static_cast<unsigned long long>(after.texture_uploaded_bytes - before.texture_uploaded_bytes),
              static_cast<unsigned long long>(target->getLastSubmissionSerial()),
              action.getLastError().getString());
  action.setRenderTarget(nullptr);
  return passed;
}

static bool equivalent(const Snapshot & direct, const Snapshot & staged,
                       uint32_t samples) {
  if (direct.color.size() != staged.color.size() || direct.depth != staged.depth)
    return false;
  unsigned maximum = 0;
  for (size_t i = 0; i < direct.color.size(); ++i)
    maximum = std::max(maximum, unsigned(direct.color[i] > staged.color[i]
        ? direct.color[i] - staged.color[i] : staged.color[i] - direct.color[i]));
  std::printf("msaa_bump_rtt equivalent samples=%u max_color_delta=%u depth_equal=%d\n",
              samples, maximum, int(direct.depth == staged.depth));
  return maximum <= 4;
}

static bool transaction(Fixture & fixture, uint32_t samples,
                        CoinRenderSceneTextureMode mode) {
  CoinRenderOptions options;
  options.sampleCount = samples;
  options.sceneTexture = mode;
  std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createOffscreen(SbVec2i32(64, 64), options));
  if (!target || !target->setDepthReadbackEnabled(TRUE)) return false;
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setTransparencyType(CoinRenderAction::NONE);
  action.setBackgroundColor(SbColor4f(0, 0, 0, 0));
  action.setRenderTarget(target.get());
  action.apply(fixture.parent);
  Snapshot published, after;
  if (action.getLastStatus() != CoinRenderAction::SUCCESS ||
      !read(target.get(), published)) return false;
  const uint64_t serial = target->getLastSubmissionSerial();
  coin_wgpu_inject_fault(COIN_WGPU_FAULT_MSAA_AFTER_ENCODE);
  action.apply(fixture.parent);
  coin_wgpu_inject_fault(0);
  const bool faultStatus = action.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY;
  const bool oldPixels = read(target.get(), after) &&
      after.color == published.color && after.depth == published.depth;
  const bool oldSerial = target->getLastSubmissionSerial() == serial;
  action.apply(fixture.parent);
  const bool recovered = action.getLastStatus() == CoinRenderAction::SUCCESS &&
      read(target.get(), after) && after.color == published.color &&
      after.depth == published.depth && target->getLastSubmissionSerial() > serial;
  std::printf("msaa_bump_rtt transaction samples=%u mode=%s fault_status=%d old_pixels=%d old_serial=%d recovered=%d error=%s\n",
              samples, mode == COIN_RENDER_SCENE_TEXTURE_DIRECT ? "direct" : "staged",
              int(faultStatus), int(oldPixels), int(oldSerial), int(recovered),
              action.getLastError().getString());
  action.setRenderTarget(nullptr);
  return faultStatus && oldPixels && oldSerial && recovered;
}

static bool invalidNoLight(Fixture & fixture, uint32_t samples,
                           CoinRenderSceneTextureMode mode) {
  CoinRenderOptions options;
  options.sampleCount = samples;
  options.sceneTexture = mode;
  std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createOffscreen(SbVec2i32(64, 64), options));
  if (!target || !target->setDepthReadbackEnabled(TRUE)) return false;
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setTransparencyType(CoinRenderAction::NONE);
  action.setBackgroundColor(SbColor4f(0, 0, 0, 0));
  action.setRenderTarget(target.get());
  action.apply(fixture.parent);
  Snapshot published, after;
  if (action.getLastStatus() != CoinRenderAction::SUCCESS ||
      !read(target.get(), published)) return false;
  const uint64_t serial = target->getLastSubmissionSerial();
  fixture.light->ref();
  fixture.child->removeChild(fixture.light);
  action.apply(fixture.parent);
  const bool oldPixels = read(target.get(), after) &&
      after.color == published.color && after.depth == published.depth;
  const bool rejected = action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
      target->getLastSubmissionSerial() == serial && oldPixels;
  fixture.child->insertChild(fixture.light, 1);
  fixture.light->unref();
  action.apply(fixture.parent);
  const bool recovered = action.getLastStatus() == CoinRenderAction::SUCCESS &&
      read(target.get(), after) && after.color == published.color &&
      after.depth == published.depth && target->getLastSubmissionSerial() > serial;
  std::printf("msaa_bump_rtt no_light samples=%u mode=%s rejected=%d recovered=%d error=%s\n",
              samples, mode == COIN_RENDER_SCENE_TEXTURE_DIRECT ? "direct" : "staged",
              int(rejected), int(recovered), action.getLastError().getString());
  action.setRenderTarget(nullptr);
  return rejected && recovered;
}

int main() {
  SoDB::init(); CoinRenderAction::initClass();
  if (!CoinRenderAction::isGpuBackendAvailable()) return 77;
  Fixture fixture = makeFixture();
  Snapshot direct[3], staged[3];
  bool passed = true;
  const uint32_t counts[3] = {1, 2, 4};
  for (size_t i = 0; i < 3 && passed; ++i) {
    passed = capture(fixture, counts[i], COIN_RENDER_SCENE_TEXTURE_DIRECT, direct[i]) &&
             capture(fixture, counts[i], COIN_RENDER_SCENE_TEXTURE_STAGED, staged[i]) &&
             equivalent(direct[i], staged[i], counts[i]);
  }
  if (passed) {
    size_t changedInterior2 = 0, changedInterior4 = 0;
    for (size_t pixel = 0; pixel < 64u * 64u; ++pixel) {
      const size_t x = pixel % 64u, y = pixel / 64u;
      if (x < 12 || x >= 52 || y < 12 || y >= 52) continue;
      for (size_t c = 0; c < 4; ++c) {
        changedInterior2 += direct[0].color[4u*pixel+c] != direct[1].color[4u*pixel+c];
        changedInterior4 += direct[0].color[4u*pixel+c] != direct[2].color[4u*pixel+c];
      }
    }
    const size_t center = 32u * 64u + 32u;
    passed = changedInterior2 > 0 && changedInterior4 > 0 &&
             direct[0].depth[center] == direct[1].depth[center] &&
             direct[0].depth[center] == direct[2].depth[center];
    std::printf("msaa_bump_rtt changed_interior_channels_1x_2x=%zu changed_interior_channels_1x_4x=%zu center_depth_stable=%d\n",
                changedInterior2, changedInterior4,
                int(direct[0].depth[center] == direct[2].depth[center]));
  }
  for (uint32_t samples : {2u, 4u})
    for (CoinRenderSceneTextureMode mode : {COIN_RENDER_SCENE_TEXTURE_DIRECT,
                                            COIN_RENDER_SCENE_TEXTURE_STAGED})
      if (passed) passed = transaction(fixture, samples, mode);
  for (uint32_t samples : {1u, 2u, 4u})
    for (CoinRenderSceneTextureMode mode : {COIN_RENDER_SCENE_TEXTURE_DIRECT,
                                            COIN_RENDER_SCENE_TEXTURE_STAGED})
      if (passed) passed = invalidNoLight(fixture, samples, mode);
  release(fixture);
  return passed ? 0 : 1;
}
