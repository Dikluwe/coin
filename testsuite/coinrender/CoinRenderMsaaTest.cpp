#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static bool render(SoSeparator * scene, uint32_t samples,
                   std::vector<uint8_t> & pixels, size_t & partial) {
  CoinRenderOptions options;
  options.sampleCount = samples;
  CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(32, 32), options);
  if (!target) return false;
  if (!target->setDepthReadbackEnabled(FALSE)) { delete target; return false; }
  CoinRenderAction action(SbViewportRegion(32, 32));
  action.setBackgroundColor(SbColor4f(0.0f, 0.0f, 0.0f, 0.0f));
  action.setRenderTarget(target);
  action.apply(scene);
  const bool ok = action.getLastStatus() == CoinRenderAction::SUCCESS;
  if (ok) target->readbackRGBA(pixels);
  partial = 0;
  for (size_t i = 3; i < pixels.size(); i += 4)
    if (pixels[i] > 0 && pixels[i] < 255) ++partial;
  std::printf("msaa_public samples=%u status=%d partial_alpha=%zu error=%s\n",
              samples, int(action.getLastStatus()), partial, target->getLastError());
  action.setRenderTarget(NULL);
  delete target;
  return ok && pixels.size() == 32u * 32u * 4u;
}

static bool renderDepth(SoSeparator * scene, uint32_t samples,
                        std::vector<float> & depth) {
  CoinRenderOptions options;
  options.sampleCount = samples;
  CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(32, 32), options);
  if (!target || !target->setDepthReadbackEnabled(TRUE)) return false;
  CoinRenderAction action(SbViewportRegion(32, 32));
  action.setRenderTarget(target);
  action.apply(scene);
  const bool rendered = action.getLastStatus() == CoinRenderAction::SUCCESS;
  if (rendered) target->readbackDepth(depth);
  std::printf("msaa_depth samples=%u status=%d pixels=%zu error=%s\n",
              samples, int(action.getLastStatus()), depth.size(), target->getLastError());
  action.setRenderTarget(NULL);
  delete target;
  return rendered && depth.size() == 1024;
}

static bool depthFollowsCoverage(const std::vector<uint8_t> & rgba,
                                 const std::vector<float> & depth,
                                 uint32_t samples) {
  if (rgba.size() != 32u * 32u * 4u || depth.size() != 32u * 32u)
    return false;
  size_t covered = 0, empty = 0, partial = 0;
  for (size_t i = 0; i < depth.size(); ++i) {
    const uint8_t alpha = rgba[4u * i + 3u];
    if (alpha == 0) {
      ++empty;
      if (depth[i] != 1.0f) {
        uint32_t bits = 0;
        std::memcpy(&bits, &depth[i], sizeof(bits));
        std::printf("msaa_depth_mismatch samples=%u x=%zu y=%zu alpha=%u expected=1 actual=%.9g bits=0x%08x\n",
                    samples, i % 32u, i / 32u, unsigned(alpha), depth[i], bits);
        return false;
      }
    } else {
      ++covered;
      if (!(depth[i] >= 0.0f && depth[i] < 1.0f)) {
        uint32_t bits = 0;
        std::memcpy(&bits, &depth[i], sizeof(bits));
        std::printf("msaa_depth_mismatch samples=%u x=%zu y=%zu alpha=%u expected=[0,1) actual=%.9g bits=0x%08x\n",
                    samples, i % 32u, i / 32u, unsigned(alpha), depth[i], bits);
        return false;
      }
      if (alpha < 255) ++partial;
    }
  }
  std::printf("msaa_depth_coverage samples=%u covered=%zu empty=%zu partial=%zu\n",
              samples, covered, empty, partial);
  return covered > 0 && empty > 0 && (samples == 1 || partial > 0);
}

static bool depthAgreesOnInterior(const std::vector<uint8_t> & one,
                                  const std::vector<uint8_t> & two,
                                  const std::vector<uint8_t> & four,
                                  const std::vector<float> & depthOne,
                                  const std::vector<float> & depthTwo,
                                  const std::vector<float> & depthFour) {
  if (one.size() != 4096 || two.size() != 4096 || four.size() != 4096 ||
      depthOne.size() != 1024 || depthTwo.size() != 1024 || depthFour.size() != 1024)
    return false;
  size_t shared = 0;
  for (size_t i = 0; i < 1024; ++i) {
    if (one[4u*i+3u] != 255 || two[4u*i+3u] != 255 || four[4u*i+3u] != 255)
      continue;
    ++shared;
    if (!(depthOne[i] < 1.0f && depthOne[i] == depthTwo[i] &&
          depthOne[i] == depthFour[i]))
      return false;
  }
  return shared > 0;
}

static bool depthMatchesMultisampleCoverage(const std::vector<uint8_t> & one,
                                            const std::vector<uint8_t> & multi,
                                            const std::vector<float> & depthOne,
                                            const std::vector<float> & depthMulti,
                                            uint32_t samples) {
  if (one.size() != 4096 || multi.size() != 4096 ||
      depthOne.size() != 1024 || depthMulti.size() != 1024) return false;
  size_t newlyCovered = 0, partial = 0, shared = 0, empty = 0;
  size_t colorNewlyCovered = 0;
  for (size_t i = 0; i < 1024; ++i)
    if (one[4u * i + 3u] == 0 && multi[4u * i + 3u] > 0)
      ++colorNewlyCovered;
  float planeDepth = 1.0f;
  for (size_t i = 0; i < 1024; ++i) {
    if (one[4u * i + 3u] == 255 && multi[4u * i + 3u] == 255) {
      planeDepth = depthOne[i];
      break;
    }
  }
  if (!(planeDepth > 0.0f && planeDepth < 1.0f)) return false;
  for (size_t i = 0; i < 1024; ++i) {
    const uint8_t a1 = one[4u * i + 3u];
    const uint8_t am = multi[4u * i + 3u];
    if (am == 0) {
      ++empty;
      if (depthMulti[i] != 1.0f) return false;
    } else {
      if (depthMulti[i] != planeDepth) return false;
      if (am < 255) ++partial;
      if (a1 == 0) ++newlyCovered;
      if (a1 == 255 && am == 255) {
        ++shared;
        if (depthMulti[i] != depthOne[i]) return false;
      }
    }
  }
  std::printf("msaa_depth_edges samples=%u new=%zu partial=%zu shared=%zu empty=%zu depth=%.9g\n",
              samples, newlyCovered, partial, shared, empty, planeDepth);
  return newlyCovered == colorNewlyCovered && partial > 0 && shared > 0 && empty > 0;
}

static bool depthPublicationIsTransactional(SoSeparator * scene, uint32_t samples) {
  CoinRenderOptions options;
  options.sampleCount = samples;
  CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(32, 32), options);
  if (!target || !target->setDepthReadbackEnabled(TRUE)) {
    delete target;
    return false;
  }
  CoinRenderAction action(SbViewportRegion(32, 32));
  action.setBackgroundColor(SbColor4f(0.0f, 0.0f, 0.0f, 0.0f));
  action.setRenderTarget(target);
  action.apply(scene);
  std::vector<uint8_t> before, after;
  std::vector<float> beforeDepth, afterDepth;
  target->readbackRGBA(before);
  target->readbackDepth(beforeDepth);
  const uint64_t serial = target->getLastSubmissionSerial();
  const bool baseline = action.getLastStatus() == CoinRenderAction::SUCCESS &&
                        before.size() == 4096 && beforeDepth.size() == 1024 && serial > 0;
  coin_wgpu_inject_fault(COIN_WGPU_FAULT_MSAA_AFTER_ENCODE);
  action.apply(scene);
  coin_wgpu_inject_fault(0);
  target->readbackRGBA(after);
  target->readbackDepth(afterDepth);
  const bool rollback = action.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY &&
                        target->getLastSubmissionSerial() == serial &&
                        after == before && afterDepth == beforeDepth;
  action.apply(scene);
  target->readbackRGBA(after);
  target->readbackDepth(afterDepth);
  const uint64_t recoveredSerial = target->getLastSubmissionSerial();
  const bool recovery =
                        action.getLastStatus() == CoinRenderAction::SUCCESS &&
                        after == before && afterDepth == beforeDepth &&
                        recoveredSerial > serial;
  size_t firstColor = 0, firstDepth = 0;
  while (firstColor < before.size() && firstColor < after.size() &&
         before[firstColor] == after[firstColor]) ++firstColor;
  while (firstDepth < beforeDepth.size() && firstDepth < afterDepth.size() &&
         beforeDepth[firstDepth] == afterDepth[firstDepth]) ++firstDepth;
  std::printf("msaa_depth_transaction samples=%u baseline=%d rollback=%d recovery=%d status=%d serial=%llu->%llu color=%zu/%zu first_color=%zu depth=%zu/%zu first_depth=%zu error=%s\n",
              samples, int(baseline), int(rollback), int(recovery), int(action.getLastStatus()),
              static_cast<unsigned long long>(serial),
              static_cast<unsigned long long>(recoveredSerial),
              before.size(), after.size(), firstColor,
              beforeDepth.size(), afterDepth.size(), firstDepth, target->getLastError());
  action.setRenderTarget(NULL);
  delete target;
  return baseline && rollback && recovery;
}

static void printCoverageDiagnostics(const std::vector<uint8_t> & one,
                                     const std::vector<uint8_t> & two,
                                     const std::vector<uint8_t> & four) {
  if (one.size() != 4096 || two.size() != 4096 || four.size() != 4096) return;
  size_t changedPixels = 0, changedBytes = 0, edgePixels = 0, shown = 0;
  for (size_t i = 0; i < 1024; ++i) {
    const size_t offset = 4u * i;
    bool changed = false;
    for (size_t channel = 0; channel < 4; ++channel) {
      if (one[offset + channel] != four[offset + channel]) {
        ++changedBytes;
        changed = true;
      }
    }
    if (changed) ++changedPixels;
    const size_t x = i % 32u, y = i / 32u;
    const uint8_t alpha = one[offset + 3u];
    bool edge = false;
    if (x > 0 && one[offset - 1u] != alpha) edge = true;
    if (x + 1u < 32u && one[offset + 7u] != alpha) edge = true;
    if (y > 0 && one[offset - 32u * 4u + 3u] != alpha) edge = true;
    if (y + 1u < 32u && one[offset + 32u * 4u + 3u] != alpha) edge = true;
    if (edge) ++edgePixels;
    if ((changed || edge) && shown < 12) {
      std::printf("msaa_edge x=%zu y=%zu 1x=(%u,%u,%u,%u) 2x=(%u,%u,%u,%u) 4x=(%u,%u,%u,%u)\n",
                  x, y,
                  unsigned(one[offset]), unsigned(one[offset+1]), unsigned(one[offset+2]), unsigned(one[offset+3]),
                  unsigned(two[offset]), unsigned(two[offset+1]), unsigned(two[offset+2]), unsigned(two[offset+3]),
                  unsigned(four[offset]), unsigned(four[offset+1]), unsigned(four[offset+2]), unsigned(four[offset+3]));
      ++shown;
    }
  }
  std::printf("msaa_coverage_diagnostic changed_pixels_1x_4x=%zu changed_bytes_1x_4x=%zu baseline_edge_pixels=%zu\n",
              changedPixels, changedBytes, edgePixels);
}

int main() {
  SoDB::init();
  CoinRenderAction::initClass();
  if (!CoinRenderAction::isGpuBackendAvailable()) return 77;
  SoSeparator * scene = new SoSeparator;
  scene->ref();
  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position.setValue(0.0f, 0.0f, 2.0f);
  camera->height = 2.0f;
  camera->nearDistance = 0.1f;
  camera->farDistance = 10.0f;
  scene->addChild(camera);
  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(1.0f, 0.0f, 0.0f);
  material->emissiveColor.setValue(1.0f, 0.0f, 0.0f);
  scene->addChild(material);
  SoCoordinate3 * coordinates = new SoCoordinate3;
  coordinates->point.set1Value(0, -1.0f, -1.0f, 0.0f);
  coordinates->point.set1Value(1, 1.0f, -1.0f, 0.0f);
  coordinates->point.set1Value(2, -1.0f, 0.83f, 0.0f);
  scene->addChild(coordinates);
  SoIndexedFaceSet * face = new SoIndexedFaceSet;
  face->coordIndex.set1Value(0, 0);
  face->coordIndex.set1Value(1, 1);
  face->coordIndex.set1Value(2, 2);
  face->coordIndex.set1Value(3, -1);
  scene->addChild(face);

  std::vector<uint8_t> one, two, four;
  size_t p1 = 0, p2 = 0, p4 = 0;
  const bool ok = render(scene, 1, one, p1) &&
                  render(scene, 2, two, p2) &&
                  render(scene, 4, four, p4) &&
                  p1 == 0 && p2 > 0 && p4 > 0 && one != four;
  printCoverageDiagnostics(one, two, four);
  std::vector<float> depthOne, depthTwo, depthFour;
  const bool depthOk = (renderDepth(scene, 1, depthOne) &&
         renderDepth(scene, 2, depthTwo) &&
         renderDepth(scene, 4, depthFour) &&
         depthFollowsCoverage(one, depthOne, 1) &&
         depthFollowsCoverage(two, depthTwo, 2) &&
         depthFollowsCoverage(four, depthFour, 4) &&
         depthAgreesOnInterior(one, two, four,
                               depthOne, depthTwo, depthFour) &&
         depthMatchesMultisampleCoverage(one, two, depthOne, depthTwo, 2) &&
         depthMatchesMultisampleCoverage(one, four, depthOne, depthFour, 4));
  const bool depthTransactional = depthPublicationIsTransactional(scene, 2) &&
                                  depthPublicationIsTransactional(scene, 4);
  CoinRenderOptions invalidOptions;
  invalidOptions.sampleCount = 8;
  CoinRenderTarget * invalid = CoinRenderTarget::createOffscreen(SbVec2i32(32, 32), invalidOptions);
  CoinRenderAction invalidAction(SbViewportRegion(32, 32));
  invalidAction.setRenderTarget(invalid);
  invalidAction.apply(scene);
  const bool rejected = invalidAction.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
                        invalid->getLastSubmissionSerial() == 0;
  invalidAction.setRenderTarget(NULL);
  delete invalid;

  CoinRenderOptions msaaOptions;
  msaaOptions.sampleCount = 4;
  CoinRenderTarget * resized = CoinRenderTarget::createOffscreen(SbVec2i32(32, 32), msaaOptions);
  resized->setDepthReadbackEnabled(FALSE);
  CoinRenderAction resizeAction(SbViewportRegion(32, 32));
  resizeAction.setBackgroundColor(SbColor4f(0.0f, 0.0f, 0.0f, 0.0f));
  resizeAction.setRenderTarget(resized);
  resizeAction.apply(scene);
  std::vector<uint8_t> before, middle, after;
  resized->readbackRGBA(before);
  const bool larger = resized->resize(SbVec2i32(64, 64));
  resizeAction.setViewportRegion(SbViewportRegion(64, 64));
  resizeAction.apply(scene);
  resized->readbackRGBA(middle);
  const bool suspended = resized->resize(SbVec2i32(0, 0));
  resizeAction.apply(scene);
  const bool notReady = resizeAction.getLastStatus() == CoinRenderAction::NOT_READY;
  const bool restored = resized->resize(SbVec2i32(32, 32));
  resizeAction.setViewportRegion(SbViewportRegion(32, 32));
  resizeAction.apply(scene);
  resized->readbackRGBA(after);
  const bool resizeOk = larger && suspended && notReady && restored &&
                        resizeAction.getLastStatus() == CoinRenderAction::SUCCESS &&
                        before.size() == 32u * 32u * 4u &&
                        middle.size() == 64u * 64u * 4u && before == after;
  std::printf("msaa_public resize_32_64_suspend_32=%d\n", int(resizeOk));
  const uint64_t stableSerial = resized->getLastSubmissionSerial();
  const std::vector<uint8_t> stablePixels = after;
  coin_wgpu_inject_fault(COIN_WGPU_NOT_READY);
  resizeAction.apply(scene);
  resized->readbackRGBA(after);
  const bool notReadyRollback = resizeAction.getLastStatus() == CoinRenderAction::NOT_READY &&
                                resized->getLastSubmissionSerial() == stableSerial &&
                                after == stablePixels;
  coin_wgpu_inject_fault(0);
  resizeAction.apply(scene);
  const bool recovered = resizeAction.getLastStatus() == CoinRenderAction::SUCCESS;
  const uint64_t recoveredSerial = resized->getLastSubmissionSerial();
  coin_wgpu_inject_fault(COIN_WGPU_OUT_OF_MEMORY);
  resizeAction.apply(scene);
  resized->readbackRGBA(after);
  const bool oomRollback = resizeAction.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY &&
                           resized->getLastSubmissionSerial() == recoveredSerial &&
                           after == stablePixels;
  coin_wgpu_inject_fault(0);
  std::printf("msaa_public rollback_not_ready=%d rollback_oom=%d\n",
              int(notReadyRollback), int(oomRollback));
  resizeAction.setRenderTarget(NULL);
  delete resized;

  CoinRenderTarget * lost = CoinRenderTarget::createOffscreen(SbVec2i32(32, 32), msaaOptions);
  lost->setDepthReadbackEnabled(FALSE);
  CoinRenderAction lostAction(SbViewportRegion(32, 32));
  lostAction.setBackgroundColor(SbColor4f(0.0f, 0.0f, 0.0f, 0.0f));
  lostAction.setRenderTarget(lost);
  lostAction.apply(scene);
  const uint64_t lostSerial = lost->getLastSubmissionSerial();
  coin_wgpu_inject_fault(COIN_WGPU_DEVICE_LOST);
  lostAction.apply(scene);
  lost->readbackRGBA(after);
  const bool deviceLossRollback = lostAction.getLastStatus() == CoinRenderAction::DEVICE_LOST &&
                                  lost->getLastSubmissionSerial() == lostSerial &&
                                  after == stablePixels;
  coin_wgpu_inject_fault(0);
  lostAction.apply(scene);
  const bool lossRecovered = lostAction.getLastStatus() == CoinRenderAction::SUCCESS;
  std::printf("msaa_public rollback_device_loss=%d recovery=%d\n",
              int(deviceLossRollback), int(lossRecovered));
  lostAction.setRenderTarget(NULL);
  delete lost;

  CoinRenderTarget * late = CoinRenderTarget::createOffscreen(SbVec2i32(32, 32), msaaOptions);
  late->setDepthReadbackEnabled(FALSE);
  CoinRenderAction lateAction(SbViewportRegion(32, 32));
  lateAction.setBackgroundColor(SbColor4f(0.0f, 0.0f, 0.0f, 0.0f));
  lateAction.setRenderTarget(late);
  lateAction.apply(scene);
  const uint64_t lateSerial = late->getLastSubmissionSerial();
  coin_wgpu_inject_fault(COIN_WGPU_FAULT_MSAA_AFTER_ENCODE);
  lateAction.apply(scene);
  late->readbackRGBA(after);
  const bool lateRollback = lateAction.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY &&
                            late->getLastSubmissionSerial() == lateSerial &&
                            after == stablePixels;
  coin_wgpu_inject_fault(0);
  std::printf("msaa_public rollback_after_encode=%d\n", int(lateRollback));
  lateAction.setRenderTarget(NULL);
  delete late;
  scene->unref();
  return ok && depthOk && depthTransactional && rejected && resizeOk && notReadyRollback && recovered &&
         oomRollback && deviceLossRollback && lossRecovered && lateRollback ? 0 : 1;
}
