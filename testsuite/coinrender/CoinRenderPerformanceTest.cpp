#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include "rendering/coinwgpu/CoinWgpuFfi.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

static_assert(sizeof(CoinWgpuPerformanceStats) == 9 * sizeof(uint64_t),
              "C/Rust performance stats layout mismatch");

namespace {
bool check(bool condition, const char * message) {
  if (!condition) std::cerr << "CoinRenderPerformanceTest: " << message << "\n";
  return condition;
}

void setColor(SoTexture2 * texture, uint8_t red) {
  std::array<uint8_t, 4 * 4 * 3> pixels;
  for (size_t i = 0; i < 16; ++i) {
    pixels[i * 3] = red;
    pixels[i * 3 + 1] = 70;
    pixels[i * 3 + 2] = 140;
  }
  texture->image.setValue(SbVec2s(4, 4), 3, pixels.data());
}

SoSeparator * makeScene(SoTexture2 *& texture, SoMaterial *& material) {
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position.setValue(0.0f, 0.0f, 3.0f);
  camera->height = 2.0f;
  camera->nearDistance = 0.1f;
  camera->farDistance = 10.0f;
  root->addChild(camera);
  SoLightModel * light = new SoLightModel;
  light->model = SoLightModel::BASE_COLOR;
  root->addChild(light);
  material = new SoMaterial;
  material->diffuseColor.setValue(1.0f, 1.0f, 1.0f);
  root->addChild(material);
  texture = new SoTexture2;
  texture->model = SoTexture2::MODULATE;
  setColor(texture, 20);
  root->addChild(texture);
  SoTextureCoordinate2 * uv = new SoTextureCoordinate2;
  uv->point.set1Value(0, SbVec2f(0.0f, 0.0f));
  uv->point.set1Value(1, SbVec2f(1.0f, 0.0f));
  uv->point.set1Value(2, SbVec2f(1.0f, 1.0f));
  uv->point.set1Value(3, SbVec2f(0.0f, 1.0f));
  root->addChild(uv);
  SoCoordinate3 * coords = new SoCoordinate3;
  coords->point.set1Value(0, SbVec3f(-0.8f, -0.8f, 0.0f));
  coords->point.set1Value(1, SbVec3f( 0.8f, -0.8f, 0.0f));
  coords->point.set1Value(2, SbVec3f( 0.8f,  0.8f, 0.0f));
  coords->point.set1Value(3, SbVec3f(-0.8f,  0.8f, 0.0f));
  root->addChild(coords);
  SoIndexedFaceSet * face = new SoIndexedFaceSet;
  const int32_t indices[] = {0, 1, 2, 3, -1};
  face->coordIndex.setValues(0, 5, indices);
  face->textureCoordIndex.setValues(0, 5, indices);
  root->addChild(face);
  return root;
}

bool render(CoinRenderAction & action, SoSeparator * root) {
  action.apply(root);
  if (action.getLastStatus() != CoinRenderAction::SUCCESS) {
    std::cerr << "CoinRenderPerformanceTest: " << action.getLastError().getString() << "\n";
    return false;
  }
  return true;
}
}

int main() {
  SoDB::init();
  CoinRenderAction::initClass();
  if (!CoinRenderAction::isGpuBackendAvailable()) {
    std::cout << "[SKIP] GPU backend unavailable\n";
    return 0;
  }
  char adapter[256] = {};
  coin_wgpu_get_adapter_info(adapter, sizeof(adapter));
  SoTexture2 * texture = NULL;
  SoMaterial * material = NULL;
  SoSeparator * root = makeScene(texture, material);
  CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(64, 64));
  if (!check(target != NULL, "createOffscreen failed")) return 1;
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setRenderTarget(target);

  typedef std::chrono::steady_clock Clock;
  const Clock::time_point start = Clock::now();
  if (!render(action, root)) return 1;
  CoinWgpuPerformanceStats warm = {};
  coin_wgpu_get_performance_stats(&warm);
  if (!check(warm.texture_uploads >= 1 && warm.texture_uploaded_bytes >= 64,
             "first frame did not upload RGBA8 texture") ||
      !check(warm.pipeline_compilations >= 1, "first frame did not create pipeline")) return 1;

  if (!render(action, root)) return 1;
  CoinWgpuPerformanceStats staticFrame = {};
  coin_wgpu_get_performance_stats(&staticFrame);
  if (!check(staticFrame.texture_uploads == warm.texture_uploads &&
             staticFrame.texture_hits > warm.texture_hits,
             "static frame missed texture cache") ||
      !check(staticFrame.pipeline_compilations == warm.pipeline_compilations &&
             staticFrame.pipeline_hits > warm.pipeline_hits,
             "static frame rebuilt pipeline")) return 1;

  for (int i = 0; i < 20; ++i) {
    setColor(texture, static_cast<uint8_t>(30 + i));
    if (!render(action, root)) return 1;
  }
  target->pollDevice();
  CoinWgpuPerformanceStats mutated = {};
  coin_wgpu_get_performance_stats(&mutated);
  if (!check(mutated.texture_uploads >= staticFrame.texture_uploads + 20 &&
             mutated.texture_uploaded_bytes >= staticFrame.texture_uploaded_bytes + 20 * 64,
             "mutated textures did not upload independently") ||
      !check(mutated.texture_evictions > staticFrame.texture_evictions &&
             mutated.texture_active_entries <= 9,
             "removed textures retained beyond stale-serial window") ||
      !check(mutated.texture_retired_entries <= 1,
             "completed textures remain in deferred retirement") ||
      !check(mutated.pipeline_compilations == warm.pipeline_compilations,
             "texture mutation recompiled pipeline")) return 1;

  root->removeChild(texture);
  for (int i = 0; i < 9; ++i) {
    if (!render(action, root)) return 1;
  }
  target->pollDevice();
  CoinWgpuPerformanceStats removed = {};
  coin_wgpu_get_performance_stats(&removed);
  if (!check(removed.texture_active_entries == 0 &&
             removed.texture_retired_entries <= 1,
             "textures remained cached after removal from the scene") ||
      !check(removed.pipeline_compilations == warm.pipeline_compilations + 1,
             "untextured specialization must compile once and then reuse its pipeline")) return 1;

  const double elapsedMs = std::chrono::duration<double, std::milli>(
      Clock::now() - start).count();

  // Measure the first blended pipeline and verify that the next frame reuses it.
  SoTransparencyType * blendMode = new SoTransparencyType;
  blendMode->value = SoTransparencyType::SORTED_OBJECT_BLEND;
  root->insertChild(blendMode, 2);
  material->transparency.setValue(0.5f);
  if (!render(action, root)) return 1;
  CoinWgpuPerformanceStats firstBlend = {};
  coin_wgpu_get_performance_stats(&firstBlend);
  if (!render(action, root)) return 1;
  CoinWgpuPerformanceStats secondBlend = {};
  coin_wgpu_get_performance_stats(&secondBlend);
  if (!check(firstBlend.pipeline_compilations == removed.pipeline_compilations + 1 &&
             secondBlend.pipeline_compilations == firstBlend.pipeline_compilations &&
             secondBlend.pipeline_hits > firstBlend.pipeline_hits,
             "blended pipeline was not compiled once and reused")) return 1;

  // The supported RTT path submits a 32x32 child, then a 64x64 parent.
  material->transparency.setValue(0.0f);
  SoSeparator * child = new SoSeparator;
  child->addChild(new SoOrthographicCamera);
  SoLightModel * childLight = new SoLightModel;
  childLight->model = SoLightModel::BASE_COLOR;
  child->addChild(childLight);
  child->addChild(new SoCube);
  SoSceneTexture2 * sceneTexture = new SoSceneTexture2;
  sceneTexture->size.setValue(32, 32);
  sceneTexture->scene.setValue(child);
  sceneTexture->type.setValue(SoSceneTexture2::RGBA8);
  root->insertChild(sceneTexture, root->getNumChildren() - 2);
  CoinWgpuCacheStats beforeRtt = {};
  coin_wgpu_get_cache_stats(&beforeRtt);
  CoinWgpuPerformanceStats beforeRttPerf = {};
  coin_wgpu_get_performance_stats(&beforeRttPerf);
  const Clock::time_point rttStart = Clock::now();
  if (!render(action, root)) return 1;
  const double rttMs = std::chrono::duration<double, std::milli>(
      Clock::now() - rttStart).count();
  CoinWgpuCacheStats afterRtt = {};
  coin_wgpu_get_cache_stats(&afterRtt);
  CoinWgpuPerformanceStats afterRttPerf = {};
  coin_wgpu_get_performance_stats(&afterRttPerf);
  const uint64_t rttPasses = afterRtt.submission_serial - beforeRtt.submission_serial;
  if (!check(rttPasses == 2 &&
             afterRttPerf.texture_uploaded_bytes >=
               beforeRttPerf.texture_uploaded_bytes + 32u * 32u * 4u,
             "staged RTT did not submit child+parent and upload the child image")) return 1;

  // Poll latency begins after submission: it excludes traversal and upload.
  root->removeChild(sceneTexture);
  CoinRenderReadbackTicket ticket{};
  action.applyAsync(root, ticket);
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
             ticket.token != 0, "asynchronous performance readback submission")) return 1;
  const Clock::time_point readbackStart = Clock::now();
  std::vector<uint8_t> color;
  std::vector<float> depth;
  CoinRenderTarget::ReadbackStatus readbackStatus =
    CoinRenderTarget::READBACK_NOT_READY;
  for (int attempt = 0; attempt < 10000; ++attempt) {
    readbackStatus = CoinRenderTarget::pollReadback(ticket, color, depth);
    if (readbackStatus != CoinRenderTarget::READBACK_NOT_READY) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const double readbackMs = std::chrono::duration<double, std::milli>(
      Clock::now() - readbackStart).count();
  const uint64_t stagingBytes =
    (uint64_t(ticket.colorRowPitch) + uint64_t(ticket.depthRowPitch)) * ticket.height;
  if (!check(readbackStatus == CoinRenderTarget::READBACK_READY &&
             color.size() == ticket.colorBytes &&
             depth.size() * sizeof(float) == ticket.depthBytes &&
             stagingBytes >= ticket.colorBytes + ticket.depthBytes,
             "asynchronous readback metrics are incomplete")) return 1;

  std::cout << "adapter=" << adapter << " resolution=64x64 frames=31 elapsed_ms="
            << elapsedMs << " texture_uploads=" << mutated.texture_uploads
            << " texture_hits=" << mutated.texture_hits
            << " texture_uploaded_bytes=" << mutated.texture_uploaded_bytes
            << " texture_evictions=" << mutated.texture_evictions
            << " texture_active_after_mutation=" << mutated.texture_active_entries
            << " texture_active_after_removal=" << removed.texture_active_entries
            << " pipeline_compilations=" << removed.pipeline_compilations
            << " pipeline_hits=" << removed.pipeline_hits
            << " blend_pipeline_compilations="
            << firstBlend.pipeline_compilations - removed.pipeline_compilations
            << " blend_pipeline_reuse_hits="
            << secondBlend.pipeline_hits - firstBlend.pipeline_hits
            << " staged_rtt_passes=" << rttPasses
            << " staged_rtt_elapsed_ms=" << rttMs
            << " staged_rtt_upload_bytes="
            << afterRttPerf.texture_uploaded_bytes - beforeRttPerf.texture_uploaded_bytes
            << " async_staging_bytes=" << stagingBytes
            << " async_readback_poll_elapsed_ms=" << readbackMs << "\n";

  delete target;
  root->unref();
  return 0;
}
