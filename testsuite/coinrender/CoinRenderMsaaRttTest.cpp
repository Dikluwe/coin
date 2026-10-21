#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

static SoOrthographicCamera * camera() {
  auto * result = new SoOrthographicCamera;
  result->position.setValue(0, 0, 2);
  result->height = 2;
  result->nearDistance = 0.1f;
  result->farDistance = 10;
  return result;
}
static SoIndexedFaceSet * triangle() {
  auto * face = new SoIndexedFaceSet;
  const int32_t indices[] = {0, 1, 2, -1};
  face->coordIndex.setValues(0, 4, indices);
  return face;
}
static SoSeparator * makeScene() {
  auto * child = new SoSeparator;
  child->addChild(camera());
  auto * base = new SoLightModel; base->model = SoLightModel::BASE_COLOR;
  child->addChild(base);
  auto * red = new SoMaterial; red->diffuseColor.setValue(1, 0, 0);
  child->addChild(red);
  auto * childPoints = new SoCoordinate3;
  childPoints->point.set1Value(0, -1, -1, 0);
  childPoints->point.set1Value(1, 1, -1, 0);
  childPoints->point.set1Value(2, -1, 0.83f, 0);
  child->addChild(childPoints);
  child->addChild(triangle());

  auto * texture = new SoSceneTexture2;
  texture->size.setValue(32, 32);
  texture->type.setValue(SoSceneTexture2::RGBA8);
  texture->transparencyFunction.setValue(SoSceneTexture2::NONE);
  texture->backgroundColor.setValue(0, 0, 1, 1);
  texture->scene.setValue(child);
  auto * parent = new SoSeparator;
  parent->addChild(camera());
  auto * parentBase = new SoLightModel; parentBase->model = SoLightModel::BASE_COLOR;
  parent->addChild(parentBase);
  auto * white = new SoMaterial; white->diffuseColor.setValue(1, 1, 1);
  parent->addChild(white);
  parent->addChild(texture);
  auto * uv = new SoTextureCoordinate2;
  uv->point.set1Value(0, 0, 0); uv->point.set1Value(1, 1, 0);
  uv->point.set1Value(2, 1, 1); uv->point.set1Value(3, 0, 1);
  parent->addChild(uv);
  auto * points = new SoCoordinate3;
  points->point.set1Value(0, -0.8f, -0.8f, 0);
  points->point.set1Value(1, 0.8f, -0.8f, 0);
  points->point.set1Value(2, 0.8f, 0.8f, 0);
  points->point.set1Value(3, -0.8f, 0.8f, 0);
  parent->addChild(points);
  auto * quad = new SoIndexedFaceSet;
  const int32_t indices[] = {0, 1, 2, 3, -1};
  quad->coordIndex.setValues(0, 5, indices);
  quad->textureCoordIndex.setValues(0, 5, indices);
  parent->addChild(quad);
  return parent;
}

struct Snapshot {
  std::vector<uint8_t> color;
  std::vector<float> depth;
};

static bool capture(SoSeparator * scene, uint32_t samples,
                    CoinRenderSceneTextureMode mode, Snapshot & result) {
  CoinRenderOptions options;
  options.sampleCount = samples;
  options.sceneTexture = mode;
  std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createOffscreen(SbVec2i32(64, 64), options));
  if (!target || !target->setDepthReadbackEnabled(TRUE)) return false;
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setBackgroundColor(SbColor4f(0, 0, 0, 0));
  action.setRenderTarget(target.get());
  CoinWgpuPerformanceStats beforeStats{};
  coin_wgpu_get_performance_stats(&beforeStats);
  action.apply(scene);
  target->readbackRGBA(result.color);
  target->readbackDepth(result.depth);
  CoinWgpuPerformanceStats stats{};
  coin_wgpu_get_performance_stats(&stats);
  const bool passed = action.getLastStatus() == CoinRenderAction::SUCCESS &&
                      result.color.size() == 64u * 64u * 4u &&
                      result.depth.size() == 64u * 64u &&
                      (mode != COIN_RENDER_SCENE_TEXTURE_DIRECT ||
                       stats.texture_uploaded_bytes == beforeStats.texture_uploaded_bytes);
  std::printf("msaa_rtt_depth samples=%u mode=%s status=%d serial=%llu color=%zu depth=%zu uploaded_bytes_delta=%llu error=%s\n",
              samples, mode == COIN_RENDER_SCENE_TEXTURE_DIRECT ? "direct" : "staged",
              int(action.getLastStatus()),
              static_cast<unsigned long long>(target->getLastSubmissionSerial()),
              result.color.size(), result.depth.size(),
              static_cast<unsigned long long>(stats.texture_uploaded_bytes - beforeStats.texture_uploaded_bytes),
              action.getLastError().getString());
  action.setRenderTarget(nullptr);
  return passed;
}

static bool depthMatchesCoverage(const Snapshot & shot, uint32_t samples,
                                 float & planeDepth) {
  if (shot.color.size() != 64u * 64u * 4u || shot.depth.size() != 64u * 64u)
    return false;
  size_t covered = 0, empty = 0, partial = 0, interior = 0;
  planeDepth = 1.0f;
  for (size_t i = 0; i < shot.depth.size(); ++i) {
    if (shot.color[4u * i + 3u] == 255) {
      planeDepth = shot.depth[i];
      break;
    }
  }
  if (!(planeDepth > 0.0f && planeDepth < 1.0f)) return false;
  for (size_t i = 0; i < shot.depth.size(); ++i) {
    const uint8_t alpha = shot.color[4u * i + 3u];
    if (alpha == 0) {
      ++empty;
      if (shot.depth[i] != 1.0f) {
        std::printf("msaa_rtt_depth_mismatch samples=%u x=%zu y=%zu alpha=0 depth=%.9g expected=1\n",
                    samples, i % 64u, i / 64u, shot.depth[i]);
        return false;
      }
    } else {
      ++covered;
      if (alpha < 255) ++partial;
      const size_t x = i % 64u, y = i / 64u;
      if (x >= 12 && x < 52 && y >= 12 && y < 52) ++interior;
      if (shot.depth[i] != planeDepth) {
        std::printf("msaa_rtt_depth_mismatch samples=%u x=%zu y=%zu alpha=%u depth=%.9g expected=%.9g\n",
                    samples, x, y, unsigned(alpha), shot.depth[i], planeDepth);
        return false;
      }
    }
  }
  std::printf("msaa_rtt_depth_coverage samples=%u covered=%zu empty=%zu partial=%zu interior=%zu depth=%.9g\n",
              samples, covered, empty, partial, interior, planeDepth);
  return covered > 0 && empty > 0 && interior > 0 && (samples == 1 || partial > 0);
}

static bool equivalent(const Snapshot & direct, const Snapshot & staged,
                       uint32_t samples) {
  if (direct.color.size() != staged.color.size() || direct.depth != staged.depth)
    return false;
  unsigned maximum = 0;
  for (size_t i = 0; i < direct.color.size(); ++i)
    maximum = std::max(maximum, unsigned(direct.color[i] > staged.color[i]
        ? direct.color[i] - staged.color[i] : staged.color[i] - direct.color[i]));
  std::printf("msaa_rtt_equivalence samples=%u max_color_delta=%u depth_equal=%d\n",
              samples, maximum, int(direct.depth == staged.depth));
  return maximum <= 4;
}

static bool rollback(SoSeparator * scene, uint32_t samples,
                     CoinRenderSceneTextureMode mode) {
  CoinRenderOptions options;
  options.sampleCount = samples;
  options.sceneTexture = mode;
  std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createOffscreen(SbVec2i32(64, 64), options));
  if (!target || !target->setDepthReadbackEnabled(TRUE)) return false;
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setBackgroundColor(SbColor4f(0, 0, 0, 0));
  action.setRenderTarget(target.get());
  action.apply(scene);
  Snapshot published, after;
  target->readbackRGBA(published.color);
  target->readbackDepth(published.depth);
  const uint64_t serial = target->getLastSubmissionSerial();
  const bool baseline = action.getLastStatus() == CoinRenderAction::SUCCESS &&
                        published.color.size() == 64u * 64u * 4u &&
                        published.depth.size() == 64u * 64u && serial > 0;
  CoinWgpuCacheStats before{};
  coin_wgpu_get_cache_stats(&before);
  coin_wgpu_inject_fault(COIN_WGPU_FAULT_MSAA_AFTER_ENCODE);
  action.apply(scene);
  coin_wgpu_inject_fault(0);
  target->readbackRGBA(after.color);
  target->readbackDepth(after.depth);
  CoinWgpuCacheStats afterStats{};
  coin_wgpu_get_cache_stats(&afterStats);
  const bool rolledBack = action.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY &&
                          after.color == published.color && after.depth == published.depth &&
                          target->getLastSubmissionSerial() == serial &&
                          afterStats.submission_serial == before.submission_serial;
  action.apply(scene);
  target->readbackRGBA(after.color);
  target->readbackDepth(after.depth);
  const bool recovered = action.getLastStatus() == CoinRenderAction::SUCCESS &&
                         after.color == published.color && after.depth == published.depth &&
                         target->getLastSubmissionSerial() > serial;
  std::printf("msaa_rtt_depth_transaction samples=%u mode=%s baseline=%d rollback=%d recovery=%d status=%d serial=%llu->%llu error=%s\n",
              samples, mode == COIN_RENDER_SCENE_TEXTURE_DIRECT ? "direct" : "staged",
              int(baseline), int(rolledBack), int(recovered), int(action.getLastStatus()),
              static_cast<unsigned long long>(serial),
              static_cast<unsigned long long>(target->getLastSubmissionSerial()),
              action.getLastError().getString());
  action.setRenderTarget(nullptr);
  return baseline && rolledBack && recovered;
}

int main() {
  SoDB::init(); CoinRenderAction::initClass();
  if (!CoinRenderAction::isGpuBackendAvailable()) return 77;
  SoSeparator * scene = makeScene(); scene->ref();
  char adapter[256] = {0};
  coin_wgpu_get_adapter_info(adapter, sizeof(adapter));
  std::printf("msaa_rtt adapter=%s\n", adapter);
  Snapshot direct1, direct2, direct4, staged2, staged4;
  bool passed = capture(scene, 1, COIN_RENDER_SCENE_TEXTURE_DIRECT, direct1) &&
                capture(scene, 2, COIN_RENDER_SCENE_TEXTURE_DIRECT, direct2) &&
                capture(scene, 2, COIN_RENDER_SCENE_TEXTURE_STAGED, staged2) &&
                capture(scene, 4, COIN_RENDER_SCENE_TEXTURE_DIRECT, direct4) &&
                capture(scene, 4, COIN_RENDER_SCENE_TEXTURE_STAGED, staged4);
  if (passed) {
    float depth1 = 1.0f, depth2 = 1.0f, depth4 = 1.0f;
    passed = depthMatchesCoverage(direct1, 1, depth1) &&
             depthMatchesCoverage(direct2, 2, depth2) &&
             depthMatchesCoverage(staged2, 2, depth2) &&
             depthMatchesCoverage(direct4, 4, depth4) &&
             depthMatchesCoverage(staged4, 4, depth4) &&
             depth1 == depth2 && depth1 == depth4 &&
             equivalent(direct2, staged2, 2) && equivalent(direct4, staged4, 4);
  }
  if (passed) {
    size_t changed = 0, interiorChanged = 0;
    for (size_t i = 0; i < direct4.color.size(); ++i) {
      if (direct4.color[i] != direct1.color[i]) ++changed;
      const size_t pixel = i / 4;
      const size_t x = pixel % 64, y = pixel / 64;
      if (x >= 12 && x < 52 && y >= 12 && y < 52 &&
          direct4.color[i] != direct1.color[i]) ++interiorChanged;
    }
    std::printf("msaa_rtt direct_1_vs_4_changed_channels=%zu interior_changed=%zu\n",
                changed, interiorChanged);
    passed = changed > 0 && interiorChanged > 0;
  }
  if (passed) {
    for (uint32_t samples : {2u, 4u})
      for (CoinRenderSceneTextureMode mode : {COIN_RENDER_SCENE_TEXTURE_DIRECT,
                                              COIN_RENDER_SCENE_TEXTURE_STAGED})
        passed = rollback(scene, samples, mode) && passed;
  }
  scene->unref();
  return passed ? 0 : 1;
}
