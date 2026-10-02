#include "../coinrender/CoinRenderTestEnvironment.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#endif

#include <chrono>
#include <thread>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

CoinRenderTarget * makeColorTarget(const SbVec2i32 & size) {
  CoinRenderTarget * target = CoinRenderTarget::createOffscreen(size);
  if (target) target->setDepthReadbackEnabled(FALSE);
  return target;
}

bool check(bool condition, const char * message, const CoinRenderAction & action) {
  if (!condition) {
    std::cerr << "CoinRenderSceneTextureTest: " << message << ": "
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
  CoinRenderAction::initClass();

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
  sceneTexture->transparencyFunction.setValue(SoSceneTexture2::NONE);
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

  CoinRenderTarget * target = makeColorTarget(SbVec2i32(64, 64));
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setRenderTarget(target);
  action.apply(parent);
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS,
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

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  const char * sharingMode = std::getenv("COIN_RENDER_RTT_GPU_DIRECT");
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
    if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
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
    if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
               afterDifferentProducer.submission_serial == beforeDifferentProducer.submission_serial + 3,
               "producers with different clear colors were incorrectly shared", action)) return 1;
    parent->removeChild(differentConsumer);
  }
#endif
  red->diffuseColor.setValue(0, 1, 0);
  action.apply(parent);
  std::vector<uint8_t> changedScene;
  target->readbackRGBA(changedScene);
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
             changedScene.size() == baseline.size() &&
             changedScene[upper + 1] > changedScene[upper] &&
             changedScene[upper + 1] > changedScene[upper + 2],
             "subscene mutation did not refresh the staged texture", action)) return 1;
  red->diffuseColor.setValue(1, 0, 0);
  action.apply(parent);
  std::vector<uint8_t> restoredScene;
  target->readbackRGBA(restoredScene);
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
             restoredScene == baseline,
             "restored subscene did not recover original pixels", action)) return 1;

  SoSeparator * clearOnlyScene = new SoSeparator;
  clearOnlyScene->ref();
  sceneTexture->scene.setValue(clearOnlyScene);
  sceneTexture->backgroundColor.setValue(1, 0, 0, 0.5f);
  action.setBackgroundColor(SbColor4f(0, 0, 1, 1));
  action.apply(parent);
  std::vector<uint8_t> noneColor;
  target->readbackRGBA(noneColor);
  const size_t noneCenter = (32u * 64u + 32u) * 4u;
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                 noneColor.size() == baseline.size() && noneColor[noneCenter] > 240 &&
                 noneColor[noneCenter + 2] < 20,
             "Coin NONE must ignore texture alpha when scheduling blending", action))
    return 1;
  sceneTexture->transparencyFunction.setValue(SoSceneTexture2::ALPHA_BLEND);
  action.apply(parent);
  std::vector<uint8_t> alphaColor;
  target->readbackRGBA(alphaColor);
  const size_t center = (32u * 64u + 32u) * 4u;
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
             alphaColor.size() == baseline.size() &&
             alphaColor[center] > 80 && alphaColor[center] < 180 &&
             alphaColor[center + 1] < 40 &&
             alphaColor[center + 2] > 80 && alphaColor[center + 2] < 180,
             "scene texture alpha did not blend over the parent background", action)) return 1;
  sceneTexture->scene.setValue(child);
  sceneTexture->backgroundColor.setValue(0, 0, 1, 1);
  action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
  clearOnlyScene->unref();

  CoinRenderTarget * swapped = makeColorTarget(SbVec2i32(64, 64));
  action.setRenderTarget(swapped);
  action.apply(parent);
  std::vector<uint8_t> swappedColor;
  swapped->readbackRGBA(swappedColor);
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
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
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
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
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  CoinWgpuCacheStats beforeInvalidPass{};
  coin_wgpu_get_cache_stats(&beforeInvalidPass);
#endif
  action.apply(parent);
  std::vector<uint8_t> afterFailure;
  target->readbackRGBA(afterFailure);
  if (!check(action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
             afterFailure == nestedColor,
             "second-pass rejection changed published parent frame", action)) return 1;
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  CoinWgpuCacheStats afterInvalidPass{};
  coin_wgpu_get_cache_stats(&afterInvalidPass);
  if (!check(afterInvalidPass.submission_serial == beforeInvalidPass.submission_serial,
             "preflight submitted a child before rejecting the second pass", action))
    return 1;
#endif
  parent->removeChild(invalidSecond);

  sceneTexture->scene.setValue(parent);
  action.apply(parent);
  if (!check(action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
             std::string(action.getLastError().getString()).find("cycle") != std::string::npos,
             "recursive scene-texture dependency was not rejected", action)) return 1;
  sceneTexture->scene.setValue(child);

  sceneTexture->size.setValue(64, 64);
  if (!check(target->resize(SbVec2i32(96, 96)), "target resize failed", action)) return 1;
  action.setViewportRegion(SbViewportRegion(96, 96));
  action.apply(parent);
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS,
             "resize/recovery after rejected passes failed", action)) return 1;
  std::vector<uint8_t> resized;
  target->readbackRGBA(resized);
  if (!check(resized.size() == 96u * 96u * 4u,
             "resized parent frame missing", action)) return 1;

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  const char * directMode = std::getenv("COIN_RENDER_RTT_GPU_DIRECT");
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
    if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
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
      if (!check(action.getLastStatus() == CoinRenderAction::DEVICE_LOST &&
                 afterNestedLoss.submission_serial ==
                   beforeNestedLoss.submission_serial + completedChildren &&
                 preservedNestedPixels == nestedSharedPixels &&
                 nestedActive == 0 && nestedRetired == 0,
                 "nested device loss leaked RTT or published a partial frame", action)) return 1;
      action.apply(parent);
      std::vector<uint8_t> recoveredNestedPixels;
      target->readbackRGBA(recoveredNestedPixels);
      if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                 recoveredNestedPixels == nestedSharedPixels,
                 "nested shared DAG did not recover after device loss", action)) return 1;
    }
    parent->removeChild(sharedConsumer);
    sceneTexture->scene.setValue(child);
    nestedScene->unref();
    action.apply(parent);
    std::vector<uint8_t> restoredAfterNested;
    target->readbackRGBA(restoredAfterNested);
    if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
               restoredAfterNested == resized,
               "restoring the parent after nested losses changed its frame", action)) return 1;
    // The child is valid, but the parent Coin modality is not. Preflight must
    // reject the complete graph before submitting even its first producer.
    CoinWgpuCacheStats beforeBadParent{};
    coin_wgpu_get_cache_stats(&beforeBadParent);
    SoSeparator* parentQuad =
        static_cast<SoSeparator*>(parent->getChild(parent->getNumChildren() - 1));
    SoTransparencyType* invalidParentMode = new SoTransparencyType;
    invalidParentMode->value = -1;
    parentQuad->insertChild(invalidParentMode, 1); // After the valid child producer.
    action.apply(parent);
    CoinWgpuCacheStats afterBadParent{};
    coin_wgpu_get_cache_stats(&afterBadParent);
    std::vector<uint8_t> afterBadParentPixels;
    target->readbackRGBA(afterBadParentPixels);
    if (!check(action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
                   afterBadParent.submission_serial == beforeBadParent.submission_serial &&
                   afterBadParentPixels == resized,
               "invalid parent was not rejected before child submit", action))
      return 1;
    parentQuad->removeChild(invalidParentMode);
    coin_wgpu_inject_fault(COIN_WGPU_DEVICE_LOST);
    action.apply(parent);
    coin_wgpu_inject_fault(COIN_WGPU_OK);
    std::vector<uint8_t> afterLoss;
    target->readbackRGBA(afterLoss);
    if (!check(action.getLastStatus() == CoinRenderAction::DEVICE_LOST && afterLoss == resized,
               "device loss published a partial parent frame", action))
      return 1;
    action.apply(parent);
    std::vector<uint8_t> recovered;
    target->readbackRGBA(recovered);
    if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
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
      if (!check(action.getLastStatus() == CoinRenderAction::DEVICE_LOST &&
                 afterMidGraphLoss.submission_serial == beforeMidGraphLoss.submission_serial + 1 &&
                 midGraphPixels == resized && liveAfterLoss == 0 && retiredAfterLoss == 0,
                 "mid-graph device loss leaked a producer or published a partial frame", action)) return 1;
      action.apply(parent);
      std::vector<uint8_t> restoredAfterMidGraphLoss;
      target->readbackRGBA(restoredAfterMidGraphLoss);
      if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
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
    if (!check(action.getLastStatus() == CoinRenderAction::DEVICE_LOST &&
               afterParentSubmitLoss == resized &&
               liveAfterParentLoss == 0 && retiredAfterParentLoss == 0,
               "loss during parent submit leaked RTT or published a partial frame", action)) return 1;
    action.apply(parent);
    std::vector<uint8_t> recoveredAfterParentLoss;
    target->readbackRGBA(recoveredAfterParentLoss);
    if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
               recoveredAfterParentLoss == resized,
               "loss during parent submit did not recover", action)) return 1;
    coin_wgpu_inject_fault_after_submits(COIN_WGPU_OUT_OF_MEMORY, 0);
    action.apply(parent);
    std::vector<uint8_t> afterChildOom;
    target->readbackRGBA(afterChildOom);
    if (!check(action.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY &&
               afterChildOom == resized,
               "OOM before child texture creation published a partial frame", action)) return 1;
    action.apply(parent);
    if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS,
               "OOM before child texture creation did not recover", action)) return 1;

    coin_wgpu_inject_fault_after_submits(COIN_WGPU_OUT_OF_MEMORY, 1);
    action.apply(parent);
    std::vector<uint8_t> afterParentOom;
    target->readbackRGBA(afterParentOom);
    uint64_t liveAfterOom = UINT64_MAX, retiredAfterOom = UINT64_MAX;
    coin_wgpu_poll_device();
    coin_wgpu_rtt_resource_counts(&liveAfterOom, &retiredAfterOom);
    if (!check(action.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY &&
               afterParentOom == resized && liveAfterOom == 0 && retiredAfterOom <= 1,
               "OOM after child submit leaked RTT or published a partial frame", action)) return 1;
    CoinRenderTarget * replacement = makeColorTarget(SbVec2i32(96, 96));
    if (!check(replacement && replacement->getStatus() == CoinRenderTarget::TARGET_READY,
               "cannot replace fatal OOM target", action)) return 1;
    action.setRenderTarget(replacement);
    delete target;
    target = replacement;
    action.apply(parent);
    std::vector<uint8_t> recoveredAfterOom;
    target->readbackRGBA(recoveredAfterOom);
    coin_wgpu_poll_device();
    uint64_t liveAfterRecovery = UINT64_MAX, retiredAfterRecovery = UINT64_MAX;
    coin_wgpu_rtt_resource_counts(&liveAfterRecovery, &retiredAfterRecovery);
    if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
               recoveredAfterOom == resized && liveAfterRecovery == 0 &&
               retiredAfterRecovery == 0,
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
      if (!check(action.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY &&
                 afterResourceFault == resized &&
                 activeFaultRtt == 0 && retiredFaultRtt <= 1,
                 "RTT resource failpoint leaked a texture or published a frame", action)) return 1;
      CoinRenderTarget * nextTarget = makeColorTarget(SbVec2i32(96, 96));
      if (!check(nextTarget && nextTarget->getStatus() == CoinRenderTarget::TARGET_READY,
                 "cannot replace target after RTT resource fault", action)) return 1;
      action.setRenderTarget(nextTarget);
      delete target;
      target = nextTarget;
      action.apply(parent);
      std::vector<uint8_t> afterResourceRecovery;
      target->readbackRGBA(afterResourceRecovery);
      coin_wgpu_poll_device();
      uint64_t activeAfterResourceRecovery = UINT64_MAX;
      uint64_t retiredAfterResourceRecovery = UINT64_MAX;
      coin_wgpu_rtt_resource_counts(&activeAfterResourceRecovery,
                                    &retiredAfterResourceRecovery);
      if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                 afterResourceRecovery == resized &&
                 activeAfterResourceRecovery == 0 &&
                 retiredAfterResourceRecovery == 0,
                 "RTT resource failpoint did not recover", action)) return 1;
    }
  }
#endif

#if defined(HAVE_COIN_BGFX)
  // RTT tickets use the same API thread as the BGFX runtime. Compare the
  // resolved graph in sync/async and fail after allocating a private ticket.
  action.apply(parent);
  std::vector<uint8_t> asyncExpected;
  target->readbackRGBA(asyncExpected);
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS, "BGFX RTT async baseline",
             action))
    return 1;
  CoinRenderReadbackTicket rttTicket{};
  action.applyAsync(parent, rttTicket);
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS && rttTicket.token &&
                 rttTicket.depthBytes == 0 &&
                 rttTicket.submissionSerial == target->getLastSubmissionSerial(),
             "BGFX RTT async metadata", action))
    return 1;
  std::vector<uint8_t> asyncPixels;
  std::vector<float> asyncDepth;
  CoinRenderTarget::ReadbackStatus pollStatus = CoinRenderTarget::READBACK_NOT_READY;
  for (unsigned attempt = 0; attempt < 5000 && pollStatus == CoinRenderTarget::READBACK_NOT_READY;
       ++attempt) {
    pollStatus = CoinRenderTarget::pollReadback(rttTicket, asyncPixels, asyncDepth);
    if (pollStatus == CoinRenderTarget::READBACK_NOT_READY)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (!check(pollStatus == CoinRenderTarget::READBACK_READY && asyncPixels == asyncExpected &&
                 asyncDepth.empty(),
             "BGFX RTT async pixels/orientation", action))
    return 1;
  action.apply(parent);
  std::vector<uint8_t> beforeAsyncFault;
  target->readbackRGBA(beforeAsyncFault);
  const uint64_t beforeAsyncSerial = target->getLastSubmissionSerial();
  std::size_t beforeAsyncBytes = 0;
  const uint8_t* beforeAsyncPointer = target->borrowRGBA(beforeAsyncBytes);
  coinRenderTestSetEnvironment("COIN_BGFX_TEST_DEVICE_LOST_AFTER_ASYNC_ONCE", "1");
  CoinRenderReadbackTicket failedAsync{};
  failedAsync.token = 999;
  action.applyAsync(parent, failedAsync);
  std::vector<uint8_t> afterAsyncFault;
  target->readbackRGBA(afterAsyncFault);
  std::size_t afterAsyncBytes = 0;
  if (!check(action.getLastStatus() == CoinRenderAction::DEVICE_LOST && !failedAsync.token &&
                 afterAsyncFault == beforeAsyncFault &&
                 target->getLastSubmissionSerial() == beforeAsyncSerial &&
                 target->borrowRGBA(afterAsyncBytes) == beforeAsyncPointer &&
                 afterAsyncBytes == beforeAsyncBytes,
             "late BGFX async failure must retire candidate ticket and preserve publication",
             action))
    return 1;
  // Recovery requires retirement of the failed ticket's runtime reference.
  CoinRenderTarget* recoveredTarget = makeColorTarget(target->getSize());
  action.setRenderTarget(recoveredTarget);
  delete target;
  target = recoveredTarget;
  action.apply(parent);
  std::vector<uint8_t> asyncRecovery;
  target->readbackRGBA(asyncRecovery);
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS && asyncRecovery == asyncExpected,
             "BGFX RTT failed async ticket leaked a runtime reference", action))
    return 1;
#endif

  CoinRenderOptions stagedOptions = target->getOptions();
  stagedOptions.sceneTexture = COIN_RENDER_SCENE_TEXTURE_STAGED;
  stagedOptions.transparency = COIN_RENDER_TRANSPARENCY_OBJECT;
  action.setRenderTarget(nullptr);
  delete target;

  // SceneTexture NONE follows Coin's forced scheduling policy even when the
  // produced pixels have alpha. An explicit ALPHA_BLEND must expose the mode
  // conflict in preflight. The transparent clear is
  // fully covered by opaque geometry, so the resulting texture is opaque.
  SoSeparator* opaqueScene = new SoSeparator;
  opaqueScene->addChild(makeCamera());
  SoLightModel* opaqueLighting = new SoLightModel;
  opaqueLighting->model = SoLightModel::BASE_COLOR;
  opaqueScene->addChild(opaqueLighting);
  SoTransparencyType* childObjectMode = new SoTransparencyType;
  childObjectMode->value = SoTransparencyType::SORTED_OBJECT_BLEND;
  opaqueScene->addChild(childObjectMode);
  SoMaterial* opaqueMaterial = new SoMaterial;
  opaqueMaterial->diffuseColor.setValue(1, 0, 0);
  opaqueScene->addChild(opaqueMaterial);
  SoCoordinate3* fullCoverage = new SoCoordinate3;
  const SbVec3f coverage[] = {SbVec3f(-10, -10, 0), SbVec3f(10, -10, 0), SbVec3f(10, 10, 0),
                              SbVec3f(-10, 10, 0)};
  fullCoverage->point.setValues(0, 4, coverage);
  opaqueScene->addChild(fullCoverage);
  SoIndexedFaceSet* opaqueFace = new SoIndexedFaceSet;
  opaqueFace->coordIndex.setValues(0, 5, upperIndices);
  opaqueScene->addChild(opaqueFace);
  SoSceneTexture2* unknownAlpha = new SoSceneTexture2;
  unknownAlpha->scene = opaqueScene;
  unknownAlpha->size.setValue(16, 16);
  unknownAlpha->backgroundColor.setValue(0, 0, 0, 0);
  unknownAlpha->type = SoSceneTexture2::RGBA8;
  SoSeparator* alphaParent = new SoSeparator;
  alphaParent->ref();
  alphaParent->addChild(makeCamera());
  SoLightModel* alphaLighting = new SoLightModel;
  alphaLighting->model = SoLightModel::BASE_COLOR;
  alphaParent->addChild(alphaLighting);
  SoTransparencyType* rootLayersMode = new SoTransparencyType;
  rootLayersMode->value.setValue(static_cast<int>(CoinRenderAction::SORTED_LAYERS_BLEND));
  alphaParent->addChild(rootLayersMode);
  alphaParent->addChild(makeTexturedQuad(unknownAlpha));
  {
    std::unique_ptr<CoinRenderTarget> alphaTarget(
        CoinRenderTarget::createOffscreen(SbVec2i32(32, 32), stagedOptions));
    alphaTarget->setDepthReadbackEnabled(FALSE);
    CoinRenderAction alphaAction(SbViewportRegion(32, 32));
    alphaAction.setRenderTarget(alphaTarget.get());
    alphaAction.apply(alphaParent);
    std::vector<uint8_t> opaqueOutput;
    alphaTarget->readbackRGBA(opaqueOutput);
    const size_t center = (16u * 32u + 16u) * 4u;
    if (!check(alphaAction.getLastStatus() == CoinRenderAction::SUCCESS &&
                   opaqueOutput.size() == 32u * 32u * 4u && opaqueOutput[center] > 100 &&
                   opaqueOutput[center + 3] == 255,
               "unknown staged alpha rejected a fully opaque texture", alphaAction))
      return 1;
    if (!check(std::string(alphaAction.getRecordingLog().getString()).find("pass=OPAQUE") !=
                   std::string::npos,
               "Recording log did not retain resolved staged alpha", alphaAction))
      return 1;
    opaqueMaterial->transparency = .5f;
    alphaAction.apply(alphaParent);
    std::vector<uint8_t> noneAlphaOutput;
    alphaTarget->readbackRGBA(noneAlphaOutput);
    if (!check(
            alphaAction.getLastStatus() == CoinRenderAction::SUCCESS &&
                noneAlphaOutput.size() == opaqueOutput.size() && noneAlphaOutput[center + 3] < 255,
            "Coin NONE must preserve sampled alpha without forcing parent blending", alphaAction))
      return 1;
    const uint64_t opaqueSerial = alphaTarget->getLastSubmissionSerial();
    unknownAlpha->transparencyFunction.setValue(SoSceneTexture2::ALPHA_BLEND);
    alphaAction.apply(alphaParent);
    std::vector<uint8_t> rejectedOutput;
    alphaTarget->readbackRGBA(rejectedOutput);
    if (!check(alphaAction.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
                   rejectedOutput == noneAlphaOutput &&
                   alphaTarget->getLastSubmissionSerial() == opaqueSerial,
               "resolved translucent texture bypassed the Coin mode conflict", alphaAction))
      return 1;
    unknownAlpha->transparencyFunction.setValue(SoSceneTexture2::NONE);
    opaqueMaterial->transparency = 0;
    alphaAction.apply(alphaParent);
    std::vector<uint8_t> recoveredOutput;
    alphaTarget->readbackRGBA(recoveredOutput);
    if (!check(alphaAction.getLastStatus() == CoinRenderAction::SUCCESS &&
                   recoveredOutput == opaqueOutput,
               "staged alpha decision did not recover", alphaAction))
      return 1;
  }
  alphaParent->unref();
  parent->unref();
  child->unref();
  std::cout << "CoinRenderSceneTextureTest passed\n";
  return 0;
}
