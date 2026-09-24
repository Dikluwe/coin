#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#if defined(HAVE_WGPU_RUST_BRIDGE)
#include "rendering/wgpu/coin_wgpu_ffi.h"
#endif

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const char * message, const SoWgpuRenderAction & action) {
  if (!condition) {
    std::cerr << "WgpuSceneTextureTest: " << message << ": "
              << action.getLastError().getString() << "\n";
  }
  return condition;
}

SoSeparator * makeTexturedQuad(SoSceneTexture2 * texture) {
  SoSeparator * quad = new SoSeparator;
  quad->addChild(texture);
  SoTextureCoordinate2 * uv = new SoTextureCoordinate2;
  uv->point.set1Value(0, SbVec2f(0, 0));
  uv->point.set1Value(1, SbVec2f(1, 0));
  uv->point.set1Value(2, SbVec2f(1, 1));
  uv->point.set1Value(3, SbVec2f(0, 1));
  quad->addChild(uv);
  SoCoordinate3 * coordinates = new SoCoordinate3;
  coordinates->point.set1Value(0, SbVec3f(-1, -1, 0));
  coordinates->point.set1Value(1, SbVec3f(1, -1, 0));
  coordinates->point.set1Value(2, SbVec3f(1, 1, 0));
  coordinates->point.set1Value(3, SbVec3f(-1, 1, 0));
  quad->addChild(coordinates);
  SoIndexedFaceSet * faces = new SoIndexedFaceSet;
  const int32_t indices[] = {0, 1, 2, 3, -1};
  faces->coordIndex.setValues(0, 5, indices);
  faces->textureCoordIndex.setValues(0, 5, indices);
  quad->addChild(faces);
  return quad;
}

SoPerspectiveCamera * makeCamera() {
  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  camera->position.setValue(0, 0, 2.5f);
  camera->nearDistance = 0.1f;
  camera->farDistance = 10.0f;
  return camera;
}

} // namespace

int main() {
  SoDB::init();
  SoWgpuRenderAction::initClass();

  SoSeparator * child = new SoSeparator;
  child->ref();
  child->addChild(makeCamera());
  SoLightModel * childLighting = new SoLightModel;
  childLighting->model = SoLightModel::BASE_COLOR;
  child->addChild(childLighting);
  SoMaterial * red = new SoMaterial;
  red->diffuseColor.setValue(1, 0, 0);
  child->addChild(red);
  SoCoordinate3 * upperCoordinates = new SoCoordinate3;
  upperCoordinates->point.set1Value(0, SbVec3f(-1, 0.1f, 0));
  upperCoordinates->point.set1Value(1, SbVec3f(1, 0.1f, 0));
  upperCoordinates->point.set1Value(2, SbVec3f(1, 1, 0));
  upperCoordinates->point.set1Value(3, SbVec3f(-1, 1, 0));
  child->addChild(upperCoordinates);
  SoIndexedFaceSet * upperFace = new SoIndexedFaceSet;
  const int32_t upperIndices[] = {0, 1, 2, 3, -1};
  upperFace->coordIndex.setValues(0, 5, upperIndices);
  child->addChild(upperFace);

  SoSceneTexture2 * sceneTexture = new SoSceneTexture2;
  sceneTexture->size.setValue(32, 32);
  sceneTexture->scene.setValue(child);
  sceneTexture->backgroundColor.setValue(0, 0, 1, 1);
  sceneTexture->type.setValue(SoSceneTexture2::RGBA8);

  SoSeparator * parent = new SoSeparator;
  parent->ref();
  parent->addChild(makeCamera());
  SoTransparencyType * blendMode = new SoTransparencyType;
  blendMode->value = SoTransparencyType::SORTED_OBJECT_BLEND;
  parent->addChild(blendMode);
  SoLightModel * parentLighting = new SoLightModel;
  parentLighting->model = SoLightModel::BASE_COLOR;
  parent->addChild(parentLighting);
  SoMaterial * white = new SoMaterial;
  white->diffuseColor.setValue(1, 1, 1);
  parent->addChild(white);
  parent->addChild(makeTexturedQuad(sceneTexture));

  SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
  SoWgpuRenderAction action(SbViewportRegion(64, 64));
  action.setRenderTarget(target);
  action.apply(parent);
  if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
             "simple scene-to-texture pass failed", action)) return 1;
  std::vector<uint8_t> baseline;
  target->readbackRGBA(baseline);
  if (!check(baseline.size() == 64u * 64u * 4u,
             "parent readback missing", action)) return 1;
  const size_t upper = (22u * 64u + 32u) * 4u;
  const size_t lower = (42u * 64u + 32u) * 4u;
  if (!check(baseline[upper] > baseline[upper + 2] &&
             baseline[upper] > 80 &&
             baseline[lower + 2] > baseline[lower],
             "scene texture has incorrect colors or vertical orientation", action)) return 1;

#if defined(HAVE_WGPU_RUST_BRIDGE)
  const char * sharingMode = std::getenv("COIN_WGPU_RTT_GPU_DIRECT");
  if (sharingMode && sharingMode[0] == '1' && sharingMode[1] == '\0') {
    SoSeparator * secondConsumer = makeTexturedQuad(sceneTexture);
    parent->addChild(secondConsumer);
    CoinWgpuCacheStats beforeSharedProducer{};
    coin_wgpu_get_cache_stats(&beforeSharedProducer);
    action.apply(parent);
    CoinWgpuCacheStats afterSharedProducer{};
    coin_wgpu_get_cache_stats(&afterSharedProducer);
    std::vector<uint8_t> sharedPixels;
    target->readbackRGBA(sharedPixels);
    if (afterSharedProducer.submission_serial != beforeSharedProducer.submission_serial + 2 ||
        sharedPixels != baseline) {
      std::cerr << "shared RTT submits="
                << (afterSharedProducer.submission_serial - beforeSharedProducer.submission_serial)
                << " pixelsEqual=" << (sharedPixels == baseline) << "\n";
    }
    if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
               afterSharedProducer.submission_serial == beforeSharedProducer.submission_serial + 2 &&
               sharedPixels == baseline,
               "equivalent consumers did not share one GPU producer", action)) return 1;
    parent->removeChild(secondConsumer);
    SoSceneTexture2 * differentProducer = new SoSceneTexture2;
    differentProducer->size.setValue(32, 32);
    differentProducer->scene.setValue(child);
    differentProducer->backgroundColor.setValue(0, 1, 0, 1);
    differentProducer->type.setValue(SoSceneTexture2::RGBA8);
    SoSeparator * differentConsumer = makeTexturedQuad(differentProducer);
    parent->addChild(differentConsumer);
    CoinWgpuCacheStats beforeDifferentProducer{};
    coin_wgpu_get_cache_stats(&beforeDifferentProducer);
    action.apply(parent);
    CoinWgpuCacheStats afterDifferentProducer{};
    coin_wgpu_get_cache_stats(&afterDifferentProducer);
    if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
               afterDifferentProducer.submission_serial == beforeDifferentProducer.submission_serial + 3,
               "producers with different clear colors were incorrectly shared", action)) return 1;
    parent->removeChild(differentConsumer);
  }
#endif
  red->diffuseColor.setValue(0, 1, 0);
  action.apply(parent);
  std::vector<uint8_t> changedScene;
  target->readbackRGBA(changedScene);
  if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
             changedScene.size() == baseline.size() &&
             changedScene[upper + 1] > changedScene[upper] &&
             changedScene[upper + 1] > changedScene[upper + 2],
             "subscene mutation did not refresh the staged texture", action)) return 1;
  red->diffuseColor.setValue(1, 0, 0);
  action.apply(parent);
  std::vector<uint8_t> restoredScene;
  target->readbackRGBA(restoredScene);
  if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
             restoredScene == baseline,
             "restored subscene did not recover original pixels", action)) return 1;

  SoSeparator * clearOnlyScene = new SoSeparator;
  clearOnlyScene->ref();
  sceneTexture->scene.setValue(clearOnlyScene);
  sceneTexture->backgroundColor.setValue(1, 0, 0, 0.5f);
  action.setBackgroundColor(SbColor4f(0, 0, 1, 1));
  action.apply(parent);
  std::vector<uint8_t> alphaColor;
  target->readbackRGBA(alphaColor);
  const size_t center = (32u * 64u + 32u) * 4u;
  if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
             alphaColor.size() == baseline.size() &&
             alphaColor[center] > 80 && alphaColor[center] < 180 &&
             alphaColor[center + 1] < 40 &&
             alphaColor[center + 2] > 80 && alphaColor[center + 2] < 180,
             "scene texture alpha did not blend over the parent background", action)) return 1;
  sceneTexture->scene.setValue(child);
  sceneTexture->backgroundColor.setValue(0, 0, 1, 1);
  action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
  clearOnlyScene->unref();

  SoWgpuRenderTarget * swapped = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
  action.setRenderTarget(swapped);
  action.apply(parent);
  std::vector<uint8_t> swappedColor;
  swapped->readbackRGBA(swappedColor);
  if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
             swappedColor == baseline,
             "target swap changed the staged scene-texture result", action)) return 1;
  action.setRenderTarget(target);
  delete swapped;

  SoSeparator * middle = new SoSeparator;
  middle->ref();
  middle->addChild(makeCamera());
  SoLightModel * middleLighting = new SoLightModel;
  middleLighting->model = SoLightModel::BASE_COLOR;
  middle->addChild(middleLighting);
  SoMaterial * middleWhite = new SoMaterial;
  middleWhite->diffuseColor.setValue(1, 1, 1);
  middle->addChild(middleWhite);
  SoSceneTexture2 * nested = new SoSceneTexture2;
  nested->size.setValue(32, 32);
  nested->scene.setValue(child);
  nested->backgroundColor.setValue(0, 0, 1, 1);
  middle->addChild(makeTexturedQuad(nested));
  sceneTexture->scene.setValue(middle);
  action.apply(parent);
  std::vector<uint8_t> nestedColor;
  target->readbackRGBA(nestedColor);
  if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
             nestedColor.size() == baseline.size() &&
             nestedColor[upper] > nestedColor[upper + 2] &&
             nestedColor[lower + 2] > nestedColor[lower],
             "two dependent scene-texture passes failed", action)) return 1;
  sceneTexture->scene.setValue(child);
  middle->unref();

  SoSceneTexture2 * invalidSecond = new SoSceneTexture2;
  invalidSecond->scene.setValue(child);
  invalidSecond->type.setValue(SoSceneTexture2::DEPTH);
  parent->addChild(invalidSecond);
#if defined(HAVE_WGPU_RUST_BRIDGE)
  CoinWgpuCacheStats beforeInvalidPass{};
  coin_wgpu_get_cache_stats(&beforeInvalidPass);
#endif
  action.apply(parent);
  std::vector<uint8_t> afterFailure;
  target->readbackRGBA(afterFailure);
  if (!check(action.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED &&
             afterFailure == nestedColor,
             "second-pass rejection changed published parent frame", action)) return 1;
#if defined(HAVE_WGPU_RUST_BRIDGE)
  const char * preflightMode = std::getenv("COIN_WGPU_RTT_GPU_DIRECT");
  if (preflightMode && preflightMode[0] == '1' && preflightMode[1] == '\0') {
    CoinWgpuCacheStats afterInvalidPass{};
    coin_wgpu_get_cache_stats(&afterInvalidPass);
    if (!check(afterInvalidPass.submission_serial == beforeInvalidPass.submission_serial,
               "preflight submitted a child before rejecting the second pass", action)) return 1;
  }
#endif
  parent->removeChild(invalidSecond);

  sceneTexture->scene.setValue(parent);
  action.apply(parent);
  if (!check(action.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED &&
             std::string(action.getLastError().getString()).find("cycle") != std::string::npos,
             "recursive scene-texture dependency was not rejected", action)) return 1;
  sceneTexture->scene.setValue(child);

  sceneTexture->size.setValue(64, 64);
  if (!check(target->resize(SbVec2i32(96, 96)), "target resize failed", action)) return 1;
  action.setViewportRegion(SbViewportRegion(96, 96));
  action.apply(parent);
  if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
             "resize/recovery after rejected passes failed", action)) return 1;
  std::vector<uint8_t> resized;
  target->readbackRGBA(resized);
  if (!check(resized.size() == 96u * 96u * 4u,
             "resized parent frame missing", action)) return 1;

#if defined(HAVE_WGPU_RUST_BRIDGE)
  const char * directMode = std::getenv("COIN_WGPU_RTT_GPU_DIRECT");
  if (directMode && directMode[0] == '1' && directMode[1] == '\0') {
    CoinWgpuPerformanceStats stats{};
    coin_wgpu_get_performance_stats(&stats);
    if (!check(stats.texture_uploaded_bytes == 0,
               "direct RTT unexpectedly uploaded intermediate pixels", action)) return 1;

    coin_wgpu_poll_device();
    uint64_t activeRtt = UINT64_MAX;
    uint64_t retiredRtt = UINT64_MAX;
    coin_wgpu_rtt_resource_counts(&activeRtt, &retiredRtt);
    if (!check(activeRtt == 0 && retiredRtt == 0,
               "direct RTT resources were not retired after the completed parent frame", action)) return 1;
    // Two parent consumers share a producer in a nested DAG. Losing the
    // device after either child submit must not publish a partial parent.
    SoSeparator * nestedScene = new SoSeparator;
    nestedScene->ref();
    nestedScene->addChild(makeCamera());
    SoLightModel * nestedLighting = new SoLightModel;
    nestedLighting->model = SoLightModel::BASE_COLOR;
    nestedScene->addChild(nestedLighting);
    SoMaterial * nestedWhite = new SoMaterial;
    nestedWhite->diffuseColor.setValue(1, 1, 1);
    nestedScene->addChild(nestedWhite);
    SoSceneTexture2 * nestedProducer = new SoSceneTexture2;
    nestedProducer->size.setValue(32, 32);
    nestedProducer->scene.setValue(child);
    nestedProducer->backgroundColor.setValue(0, 0, 1, 1);
    nestedScene->addChild(makeTexturedQuad(nestedProducer));
    sceneTexture->scene.setValue(nestedScene);
    SoSeparator * sharedConsumer = makeTexturedQuad(sceneTexture);
    parent->addChild(sharedConsumer);
    CoinWgpuCacheStats beforeNested{};
    coin_wgpu_get_cache_stats(&beforeNested);
    action.apply(parent);
    CoinWgpuCacheStats afterNested{};
    coin_wgpu_get_cache_stats(&afterNested);
    std::vector<uint8_t> nestedSharedPixels;
    target->readbackRGBA(nestedSharedPixels);
    if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
               afterNested.submission_serial == beforeNested.submission_serial + 3 &&
               nestedSharedPixels.size() == resized.size(),
               "nested shared DAG did not submit two producers and one parent", action)) return 1;
    for (unsigned int completedChildren = 1; completedChildren <= 2; ++completedChildren) {
      CoinWgpuCacheStats beforeNestedLoss{};
      coin_wgpu_get_cache_stats(&beforeNestedLoss);
      coin_wgpu_inject_fault_after_submits(COIN_WGPU_DEVICE_LOST, completedChildren);
      action.apply(parent);
      CoinWgpuCacheStats afterNestedLoss{};
      coin_wgpu_get_cache_stats(&afterNestedLoss);
      std::vector<uint8_t> preservedNestedPixels;
      target->readbackRGBA(preservedNestedPixels);
      coin_wgpu_poll_device();
      uint64_t nestedActive = UINT64_MAX, nestedRetired = UINT64_MAX;
      coin_wgpu_rtt_resource_counts(&nestedActive, &nestedRetired);
      if (!check(action.getLastStatus() == SoWgpuRenderAction::DEVICE_LOST &&
                 afterNestedLoss.submission_serial ==
                   beforeNestedLoss.submission_serial + completedChildren &&
                 preservedNestedPixels == nestedSharedPixels &&
                 nestedActive == 0 && nestedRetired == 0,
                 "nested device loss leaked RTT or published a partial frame", action)) return 1;
      action.apply(parent);
      std::vector<uint8_t> recoveredNestedPixels;
      target->readbackRGBA(recoveredNestedPixels);
      if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
                 recoveredNestedPixels == nestedSharedPixels,
                 "nested shared DAG did not recover after device loss", action)) return 1;
    }
    parent->removeChild(sharedConsumer);
    sceneTexture->scene.setValue(child);
    nestedScene->unref();
    action.apply(parent);
    std::vector<uint8_t> restoredAfterNested;
    target->readbackRGBA(restoredAfterNested);
    if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
               restoredAfterNested == resized,
               "restoring the parent after nested losses changed its frame", action)) return 1;
    // The child is valid, but the parent viewport is not. Preflight must
    // reject the complete graph before submitting even its first producer.
    CoinWgpuCacheStats beforeBadParent{};
    coin_wgpu_get_cache_stats(&beforeBadParent);
    action.setViewportRegion(SbViewportRegion(64, 64));
    action.apply(parent);
    CoinWgpuCacheStats afterBadParent{};
    coin_wgpu_get_cache_stats(&afterBadParent);
    std::vector<uint8_t> afterBadParentPixels;
    target->readbackRGBA(afterBadParentPixels);
    if (!check(action.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED &&
               afterBadParent.submission_serial == beforeBadParent.submission_serial &&
               afterBadParentPixels == resized,
               "invalid parent was not rejected before child submit", action)) return 1;
    action.setViewportRegion(SbViewportRegion(96, 96));
    coin_wgpu_inject_fault(COIN_WGPU_DEVICE_LOST);
    action.apply(parent);
    coin_wgpu_inject_fault(COIN_WGPU_OK);
    std::vector<uint8_t> afterLoss;
    target->readbackRGBA(afterLoss);
    if (!check(action.getLastStatus() == SoWgpuRenderAction::DEVICE_LOST &&
               afterLoss == resized,
               "device loss published a partial parent frame", action)) return 1;
    action.apply(parent);
    std::vector<uint8_t> recovered;
    target->readbackRGBA(recovered);
    if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
               recovered == resized,
               "direct RTT did not recover on the next apply", action)) return 1;
    // Lose the device after a child GPU submit, before the parent submit.
    for (int loss = 0; loss < 2; ++loss) {
      CoinWgpuCacheStats beforeMidGraphLoss{};
      coin_wgpu_get_cache_stats(&beforeMidGraphLoss);
      coin_wgpu_inject_fault_after_submits(COIN_WGPU_DEVICE_LOST, 1);
      action.apply(parent);
      CoinWgpuCacheStats afterMidGraphLoss{};
      coin_wgpu_get_cache_stats(&afterMidGraphLoss);
      std::vector<uint8_t> midGraphPixels;
      target->readbackRGBA(midGraphPixels);
      uint64_t liveAfterLoss = UINT64_MAX;
      uint64_t retiredAfterLoss = UINT64_MAX;
      coin_wgpu_rtt_resource_counts(&liveAfterLoss, &retiredAfterLoss);
      if (!check(action.getLastStatus() == SoWgpuRenderAction::DEVICE_LOST &&
                 afterMidGraphLoss.submission_serial == beforeMidGraphLoss.submission_serial + 1 &&
                 midGraphPixels == resized && liveAfterLoss == 0 && retiredAfterLoss == 0,
                 "mid-graph device loss leaked a producer or published a partial frame", action)) return 1;
      action.apply(parent);
      std::vector<uint8_t> restoredAfterMidGraphLoss;
      target->readbackRGBA(restoredAfterMidGraphLoss);
      if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
                 restoredAfterMidGraphLoss == resized,
                 "mid-graph device loss did not recover on a new apply", action)) return 1;
    }
    // The child is already submitted; force loss while the parent waits
    // for its own GPU work, before either color or depth is published.
    coin_wgpu_inject_async_fault(COIN_WGPU_DEVICE_LOST);
    action.apply(parent);
    std::vector<uint8_t> afterParentSubmitLoss;
    target->readbackRGBA(afterParentSubmitLoss);
    uint64_t liveAfterParentLoss = UINT64_MAX;
    uint64_t retiredAfterParentLoss = UINT64_MAX;
    coin_wgpu_rtt_resource_counts(&liveAfterParentLoss, &retiredAfterParentLoss);
    if (!check(action.getLastStatus() == SoWgpuRenderAction::DEVICE_LOST &&
               afterParentSubmitLoss == resized &&
               liveAfterParentLoss == 0 && retiredAfterParentLoss == 0,
               "loss during parent submit leaked RTT or published a partial frame", action)) return 1;
    action.apply(parent);
    std::vector<uint8_t> recoveredAfterParentLoss;
    target->readbackRGBA(recoveredAfterParentLoss);
    if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
               recoveredAfterParentLoss == resized,
               "loss during parent submit did not recover", action)) return 1;
    coin_wgpu_inject_fault_after_submits(COIN_WGPU_OUT_OF_MEMORY, 0);
    action.apply(parent);
    std::vector<uint8_t> afterChildOom;
    target->readbackRGBA(afterChildOom);
    if (!check(action.getLastStatus() == SoWgpuRenderAction::OUT_OF_MEMORY &&
               afterChildOom == resized,
               "OOM before child texture creation published a partial frame", action)) return 1;
    action.apply(parent);
    if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
               "OOM before child texture creation did not recover", action)) return 1;

    coin_wgpu_inject_fault_after_submits(COIN_WGPU_OUT_OF_MEMORY, 1);
    action.apply(parent);
    std::vector<uint8_t> afterParentOom;
    target->readbackRGBA(afterParentOom);
    coin_wgpu_poll_device();
    uint64_t liveAfterOom = UINT64_MAX, retiredAfterOom = UINT64_MAX;
    coin_wgpu_rtt_resource_counts(&liveAfterOom, &retiredAfterOom);
    if (!check(action.getLastStatus() == SoWgpuRenderAction::OUT_OF_MEMORY &&
               afterParentOom == resized && liveAfterOom == 0 && retiredAfterOom == 0,
               "OOM after child submit leaked RTT or published a partial frame", action)) return 1;
    SoWgpuRenderTarget * replacement = SoWgpuRenderTarget::createOffscreen(SbVec2i32(96, 96));
    if (!check(replacement && replacement->getStatus() == SoWgpuRenderTarget::TARGET_READY,
               "cannot replace fatal OOM target", action)) return 1;
    action.setRenderTarget(replacement);
    delete target;
    target = replacement;
    action.apply(parent);
    std::vector<uint8_t> recoveredAfterOom;
    target->readbackRGBA(recoveredAfterOom);
    if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
               recoveredAfterOom == resized,
               "OOM did not recover with a new target", action)) return 1;
    const int32_t resourceFaults[] = {
      COIN_WGPU_FAULT_RTT_COLOR_ALLOC,
      COIN_WGPU_FAULT_RTT_COLOR_VIEW,
      COIN_WGPU_FAULT_RTT_DEPTH_ALLOC,
      COIN_WGPU_FAULT_RTT_BIND_GROUP
    };
    for (int32_t fault : resourceFaults) {
      coin_wgpu_inject_fault(fault);
      action.apply(parent);
      coin_wgpu_inject_fault(COIN_WGPU_OK);
      std::vector<uint8_t> afterResourceFault;
      target->readbackRGBA(afterResourceFault);
      coin_wgpu_poll_device();
      uint64_t activeFaultRtt = UINT64_MAX, retiredFaultRtt = UINT64_MAX;
      coin_wgpu_rtt_resource_counts(&activeFaultRtt, &retiredFaultRtt);
      if (!check(action.getLastStatus() == SoWgpuRenderAction::OUT_OF_MEMORY &&
                 afterResourceFault == resized &&
                 activeFaultRtt == 0 && retiredFaultRtt == 0,
                 "RTT resource failpoint leaked a texture or published a frame", action)) return 1;
      SoWgpuRenderTarget * nextTarget = SoWgpuRenderTarget::createOffscreen(SbVec2i32(96, 96));
      if (!check(nextTarget && nextTarget->getStatus() == SoWgpuRenderTarget::TARGET_READY,
                 "cannot replace target after RTT resource fault", action)) return 1;
      action.setRenderTarget(nextTarget);
      delete target;
      target = nextTarget;
      action.apply(parent);
      std::vector<uint8_t> afterResourceRecovery;
      target->readbackRGBA(afterResourceRecovery);
      if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
                 afterResourceRecovery == resized,
                 "RTT resource failpoint did not recover", action)) return 1;
    }
  }
#endif

  delete target;
  parent->unref();
  child->unref();
  std::cout << "WgpuSceneTextureTest passed\n";
  return 0;
}
