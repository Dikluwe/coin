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
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoSceneTexture2.h>

#if defined(HAVE_WGPU_RUST_BRIDGE)
#include "rendering/wgpu/coin_wgpu_ffi.h"
#endif
#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace {

bool check(bool condition, const char * message) {
  if (!condition) std::cerr << "WgpuAsyncActionTest: " << message << "\n";
  return condition;
}

SoWgpuRenderTarget::ReadbackStatus pollUntilReady(
  const SoWgpuReadbackTicket & ticket,
  std::vector<uint8_t> & color,
  std::vector<float> & depth,
  SbString & diagnostic) {
  for (int attempt = 0; attempt < 5000; ++attempt) {
    const SoWgpuRenderTarget::ReadbackStatus status =
      SoWgpuRenderTarget::pollReadback(ticket, color, depth, &diagnostic);
    if (status != SoWgpuRenderTarget::READBACK_NOT_READY) return status;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return SoWgpuRenderTarget::READBACK_NOT_READY;
}

} // namespace

int main() {
  SoDB::init();
  SoWgpuRenderAction::initClass();
  if (!SoWgpuRenderAction::isGpuBackendAvailable()) {
    std::cout << "No WebGPU adapter; async action test skipped\n";
    return 0;
  }

  SoSeparator * root = new SoSeparator;
  root->ref();
  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  camera->position.setValue(0.0f, 0.0f, 4.0f);
  camera->nearDistance = 0.1f;
  camera->farDistance = 10.0f;
  root->addChild(camera);
  SoDirectionalLight * light = new SoDirectionalLight;
  light->direction.setValue(0.0f, 0.0f, -1.0f);
  root->addChild(light);
  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(0.8f, 0.2f, 0.1f);
  root->addChild(material);
  root->addChild(new SoCone);

  SoWgpuRenderAction noTarget(SbViewportRegion(64, 64));
  SoWgpuReadbackTicket missing{};
  noTarget.applyAsync(root, missing);
  if (!check(noTarget.getLastStatus() == SoWgpuRenderAction::NO_TARGET &&
             missing.token == 0, "applyAsync without target must fail explicitly")) return 1;

  SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
  SoWgpuRenderAction * action = new SoWgpuRenderAction(SbViewportRegion(64, 64));
  action->setRenderTarget(target);
  action->setBackgroundColor(SbColor4f(0.05f, 0.05f, 0.1f, 1.0f));
  SoWgpuReadbackTicket ticket{};
  action->applyAsync(root, ticket);
  if (!check(action->getLastStatus() == SoWgpuRenderAction::SUCCESS &&
             ticket.token != 0 && ticket.width == 64 && ticket.height == 64 &&
             ticket.colorBytes == 64u * 64u * 4u &&
             ticket.depthBytes == 64u * 64u * 4u &&
             ticket.submissionSerial == target->getLastSubmissionSerial(),
             "async action must return a complete ticket")) return 1;

  std::vector<uint8_t> oldColor(1, 17);
  std::vector<float> oldDepth(1, -1.0f);
  target->readbackRGBA(oldColor);
  target->readbackDepth(oldDepth);
  if (!check(oldColor.empty() && oldDepth.empty(),
             "synchronous accessors must not expose stale frame after async submit")) return 1;

  if (!check(target->resize(SbVec2i32(32, 32)), "resize while readback is pending")) return 1;
  delete action;
  delete target;

  std::vector<uint8_t> color(1, 17);
  std::vector<float> depth(1, -1.0f);
  SbString diagnostic;
  SoWgpuReadbackTicket malformed = ticket;
  malformed.colorRowPitch = 0;
  if (!check(SoWgpuRenderTarget::pollReadback(malformed, color, depth, &diagnostic) ==
               SoWgpuRenderTarget::READBACK_INVALID_TICKET &&
             color.size() == 1 && color[0] == 17 &&
             depth.size() == 1 && depth[0] == -1.0f,
             "invalid ticket metadata must not consume or publish output")) return 1;
  if (!check(pollUntilReady(ticket, color, depth, diagnostic) ==
             SoWgpuRenderTarget::READBACK_READY &&
             color.size() == 64u * 64u * 4u && depth.size() == 64u * 64u,
             "ticket must outlive action and target")) {
    std::cerr << diagnostic.getString() << "\n";
    return 1;
  }

  SoWgpuRenderTarget * synchronous = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
  SoWgpuRenderAction syncAction(SbViewportRegion(64, 64));
  syncAction.setRenderTarget(synchronous);
  syncAction.setBackgroundColor(SbColor4f(0.05f, 0.05f, 0.1f, 1.0f));
  syncAction.apply(root);
  std::vector<uint8_t> expectedColor;
  std::vector<float> expectedDepth;
  synchronous->readbackRGBA(expectedColor);
  synchronous->readbackDepth(expectedDepth);
  if (!check(syncAction.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
             color == expectedColor && depth == expectedDepth,
             "async action pixels and depth must match synchronous action")) return 1;
  if (!check(SoWgpuRenderTarget::pollReadback(ticket, oldColor, oldDepth) ==
             SoWgpuRenderTarget::READBACK_INVALID_TICKET,
             "completed ticket must be consumed")) return 1;

  if (!check(synchronous->setDepthReadbackEnabled(FALSE),
             "disable depth for async color-only readback")) return 1;
  SoWgpuReadbackTicket colorOnlyTicket{};
  syncAction.applyAsync(root, colorOnlyTicket);
  if (!check(syncAction.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
             colorOnlyTicket.token != 0 && colorOnlyTicket.depthFormat == 0 &&
             colorOnlyTicket.depthBytes == 0 && colorOnlyTicket.depthRowPitch == 0,
             "color-only ticket must omit depth metadata")) return 1;
  if (!check(synchronous->setDepthReadbackEnabled(TRUE),
             "restore depth while color-only ticket is pending")) return 1;
  std::vector<uint8_t> colorOnlyPixels;
  std::vector<float> colorOnlyDepth(1, -1.0f);
  if (!check(pollUntilReady(colorOnlyTicket, colorOnlyPixels, colorOnlyDepth, diagnostic) ==
             SoWgpuRenderTarget::READBACK_READY &&
             colorOnlyPixels == expectedColor && colorOnlyDepth.empty(),
             "color-only ticket must preserve color and omit depth after mode change")) return 1;

  SoWgpuReadbackTicket cancelled{};
  syncAction.applyAsync(root, cancelled);
  if (!check(syncAction.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
             SoWgpuRenderTarget::cancelReadback(cancelled) &&
             SoWgpuRenderTarget::pollReadback(cancelled, oldColor, oldDepth) ==
               SoWgpuRenderTarget::READBACK_INVALID_TICKET,
             "cancelled ticket must not publish output")) return 1;

  SoWgpuReadbackTicket parallelA{}, parallelB{};
  syncAction.applyAsync(root, parallelA);
  if (!check(syncAction.getLastStatus() == SoWgpuRenderAction::SUCCESS,
             "first concurrent readback submission")) return 1;
  syncAction.applyAsync(root, parallelB);
  if (!check(syncAction.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
             parallelA.token != parallelB.token,
             "second concurrent readback submission")) return 1;
  std::vector<uint8_t> colorA, colorB;
  std::vector<float> depthA, depthB;
  SbString errorA, errorB;
  SoWgpuRenderTarget::ReadbackStatus statusA = SoWgpuRenderTarget::READBACK_ERROR;
  SoWgpuRenderTarget::ReadbackStatus statusB = SoWgpuRenderTarget::READBACK_ERROR;
  std::thread pollA([&]() {
    statusA = pollUntilReady(parallelA, colorA, depthA, errorA);
  });
  std::thread pollB([&]() {
    statusB = pollUntilReady(parallelB, colorB, depthB, errorB);
  });
  pollA.join();
  pollB.join();
  if (!check(statusA == SoWgpuRenderTarget::READBACK_READY &&
             statusB == SoWgpuRenderTarget::READBACK_READY &&
             colorA == expectedColor && colorB == expectedColor &&
             depthA == expectedDepth && depthB == expectedDepth,
             "concurrent polls must publish independent complete frames")) {
    std::cerr << errorA.getString() << " " << errorB.getString() << "\n";
    return 1;
  }

  SoSeparator * texturedParent = new SoSeparator;
  texturedParent->ref();
  SoPerspectiveCamera * texturedCamera = new SoPerspectiveCamera;
  texturedCamera->position.setValue(0.0f, 0.0f, 4.0f);
  texturedCamera->nearDistance = 0.1f;
  texturedCamera->farDistance = 10.0f;
  texturedParent->addChild(texturedCamera);
  SoLightModel * unlit = new SoLightModel;
  unlit->model = SoLightModel::BASE_COLOR;
  texturedParent->addChild(unlit);
  SoMaterial * white = new SoMaterial;
  white->diffuseColor.setValue(1.0f, 1.0f, 1.0f);
  texturedParent->addChild(white);
  SoSceneTexture2 * sceneTexture = new SoSceneTexture2;
  sceneTexture->scene.setValue(root);
  sceneTexture->size.setValue(32, 32);
  sceneTexture->type.setValue(SoSceneTexture2::RGBA8);
  sceneTexture->backgroundColor.setValue(0.0f, 0.0f, 1.0f, 1.0f);
  texturedParent->addChild(sceneTexture);
  SoTextureCoordinate2 * texcoord = new SoTextureCoordinate2;
  texcoord->point.set1Value(0, SbVec2f(0, 0));
  texcoord->point.set1Value(1, SbVec2f(1, 0));
  texcoord->point.set1Value(2, SbVec2f(1, 1));
  texcoord->point.set1Value(3, SbVec2f(0, 1));
  texturedParent->addChild(texcoord);
  SoCoordinate3 * quad = new SoCoordinate3;
  quad->point.set1Value(0, SbVec3f(-1, -1, 0));
  quad->point.set1Value(1, SbVec3f(1, -1, 0));
  quad->point.set1Value(2, SbVec3f(1, 1, 0));
  quad->point.set1Value(3, SbVec3f(-1, 1, 0));
  texturedParent->addChild(quad);
  SoIndexedFaceSet * face = new SoIndexedFaceSet;
  const int32_t faceIndices[] = {0, 1, 2, 3, -1};
  face->coordIndex.setValues(0, 5, faceIndices);
  face->textureCoordIndex.setValues(0, 5, faceIndices);
  texturedParent->addChild(face);

  syncAction.apply(texturedParent);
  std::vector<uint8_t> sceneExpectedColor;
  std::vector<float> sceneExpectedDepth;
  synchronous->readbackRGBA(sceneExpectedColor);
  synchronous->readbackDepth(sceneExpectedDepth);
  if (!check(syncAction.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
             sceneExpectedColor.size() == 64u * 64u * 4u &&
             sceneExpectedDepth.size() == 64u * 64u,
             "synchronous scene-texture frame")) {
    std::cerr << syncAction.getLastError().getString() << "\n";
    return 1;
  }
  std::size_t borrowedSceneBytes = 0;
  if (!check(synchronous->borrowRGBA(borrowedSceneBytes) != NULL &&
             borrowedSceneBytes == sceneExpectedColor.size(),
             "synchronous scene-texture frame must expose borrowed RGBA")) return 1;

  SoWgpuReadbackTicket sceneTicket{};
  syncAction.applyAsync(texturedParent, sceneTicket);
  if (!check(syncAction.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
             sceneTicket.token != 0,
             "async scene-texture submission")) return 1;
  borrowedSceneBytes = 1;
  if (!check(synchronous->borrowRGBA(borrowedSceneBytes) == NULL &&
             borrowedSceneBytes == 0,
             "async submit must invalidate borrowed synchronous RGBA")) return 1;
  std::vector<uint8_t> staleSceneColor;
  synchronous->readbackRGBA(staleSceneColor);
  if (!check(staleSceneColor.empty(),
             "async scene-texture submit must hide prior synchronous frame")) return 1;
#if defined(HAVE_WGPU_RUST_BRIDGE)
  const char * directRtt = std::getenv("COIN_WGPU_RTT_GPU_DIRECT");
  if (directRtt && directRtt[0] == '1' && directRtt[1] == '\0') {
    coin_wgpu_trim_cache(); // Evict stale geometry while the RTT ticket is in flight.
  }
#endif
  material->diffuseColor.setValue(0.1f, 0.8f, 0.2f);

  std::vector<uint8_t> sceneColor;
  std::vector<float> sceneDepth;
  if (!check(pollUntilReady(sceneTicket, sceneColor, sceneDepth, diagnostic) ==
               SoWgpuRenderTarget::READBACK_READY &&
             sceneColor == sceneExpectedColor &&
             sceneDepth == sceneExpectedDepth,
             "async scene-texture pixels and depth must match synchronous output")) {
    std::cerr << diagnostic.getString() << "\n";
    return 1;
  }
  material->diffuseColor.setValue(0.8f, 0.2f, 0.1f);

  // Two RTT frames overlap in flight while the producer scene mutates.
  SoWgpuReadbackTicket redTicket{}, greenTicket{};
  syncAction.applyAsync(texturedParent, redTicket);
  if (!check(syncAction.getLastStatus() == SoWgpuRenderAction::SUCCESS && redTicket.token != 0,
             "first overlapping RTT ticket")) return 1;
  material->diffuseColor.setValue(0.1f, 0.8f, 0.2f);
  syncAction.applyAsync(texturedParent, greenTicket);
  if (!check(syncAction.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
             greenTicket.token != 0 && greenTicket.token != redTicket.token,
             "second overlapping RTT ticket")) return 1;
  syncAction.apply(texturedParent);
  std::vector<uint8_t> expectedGreenColor;
  std::vector<float> expectedGreenDepth;
  synchronous->readbackRGBA(expectedGreenColor);
  synchronous->readbackDepth(expectedGreenDepth);
  std::vector<uint8_t> greenColor, redColor;
  std::vector<float> greenDepth, redDepth;
  if (!check(syncAction.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
             pollUntilReady(greenTicket, greenColor, greenDepth, diagnostic) ==
               SoWgpuRenderTarget::READBACK_READY &&
             pollUntilReady(redTicket, redColor, redDepth, diagnostic) ==
               SoWgpuRenderTarget::READBACK_READY &&
             greenColor == expectedGreenColor && greenDepth == expectedGreenDepth &&
             redColor == sceneExpectedColor && redDepth == sceneExpectedDepth,
             "overlapping RTT tickets did not retain independent snapshots")) return 1;
  material->diffuseColor.setValue(0.8f, 0.2f, 0.1f);
#if defined(HAVE_WGPU_RUST_BRIDGE)
  if (directRtt && directRtt[0] == '1' && directRtt[1] == '\0') {
    uint64_t previousGeneration = 0;
    for (int loss = 0; loss < 2; ++loss) {
      SoWgpuReadbackTicket pendingLoss{};
      syncAction.applyAsync(texturedParent, pendingLoss);
      if (!check(syncAction.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
                 pendingLoss.token != 0 &&
                 (loss == 0 || pendingLoss.generation > previousGeneration),
                 "direct RTT ticket was not submitted in a new generation")) return 1;
      previousGeneration = pendingLoss.generation;
      coin_wgpu_inject_async_fault(COIN_WGPU_DEVICE_LOST);
      std::vector<uint8_t> untouchedColor(1, 0x5a);
      std::vector<float> untouchedDepth(1, -1.0f);
      if (!check(SoWgpuRenderTarget::pollReadback(
                   pendingLoss, untouchedColor, untouchedDepth, &diagnostic) ==
                   SoWgpuRenderTarget::READBACK_DEVICE_LOST &&
                 untouchedColor.size() == 1 && untouchedColor[0] == 0x5a &&
                 untouchedDepth.size() == 1 && untouchedDepth[0] == -1.0f,
                 "device loss published a pending direct RTT ticket")) return 1;
      coin_wgpu_poll_device();
      uint64_t activeRtt = UINT64_MAX, retiredRtt = UINT64_MAX;
      coin_wgpu_rtt_resource_counts(&activeRtt, &retiredRtt);
      if (!check(activeRtt == 0 && retiredRtt == 0,
                 "device loss retained an RTT resource from the old generation")) return 1;
      syncAction.apply(texturedParent);
      std::vector<uint8_t> recoveredColor;
      std::vector<float> recoveredDepth;
      synchronous->readbackRGBA(recoveredColor);
      synchronous->readbackDepth(recoveredDepth);
      if (!check(syncAction.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
                 recoveredColor == sceneExpectedColor &&
                 recoveredDepth == sceneExpectedDepth,
                 "direct RTT did not recover after pending ticket loss")) return 1;
      if (!check(SoWgpuRenderTarget::pollReadback(
                   pendingLoss, untouchedColor, untouchedDepth, &diagnostic) ==
                   SoWgpuRenderTarget::READBACK_INVALID_TICKET &&
                 untouchedColor.size() == 1 && untouchedColor[0] == 0x5a &&
                 untouchedDepth.size() == 1 && untouchedDepth[0] == -1.0f,
                 "stale direct RTT ticket wrote into caller buffers")) return 1;
    }
  }
#endif
  const uint64_t publishedSerial = synchronous->getLastSubmissionSerial();
  sceneTexture->type.setValue(SoSceneTexture2::DEPTH);
  SoWgpuReadbackTicket rejectedScene{};
  syncAction.applyAsync(texturedParent, rejectedScene);
  if (!check(syncAction.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED &&
             rejectedScene.token == 0 &&
             synchronous->getLastSubmissionSerial() == publishedSerial,
             "unsupported scene-texture pass must not submit parent frame")) return 1;
  const char * stressMode = std::getenv("COIN_WGPU_RTT_STRESS");
  if (stressMode && stressMode[0] == '1' && stressMode[1] == '\0') {
    sceneTexture->type.setValue(SoSceneTexture2::RGBA8);
    for (int batch = 0; batch < 128; ++batch) {
      const short edge = (batch & 1) ? 16 : 32;
      sceneTexture->size.setValue(edge, edge);
      SoWgpuReadbackTicket inFlight[8]{};
      for (int j = 0; j < 8; ++j) {
        material->diffuseColor.setValue((j & 1) ? 0.1f : 0.8f,
                                        (j & 1) ? 0.8f : 0.2f, 0.2f);
        syncAction.applyAsync(texturedParent, inFlight[j]);
        if (!check(syncAction.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
                   inFlight[j].token != 0, "RTT stress submit")) return 1;
      }
      SoWgpuRenderTarget * replacement =
        SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
      if (!check(replacement && replacement->getStatus() == SoWgpuRenderTarget::TARGET_READY,
                 "RTT stress target replacement")) return 1;
      syncAction.setRenderTarget(replacement);
      delete synchronous;
      synchronous = replacement;
      for (int j = 7; j >= 0; --j) {
        if (j % 3 == 0) {
          if (!check(SoWgpuRenderTarget::cancelReadback(inFlight[j]),
                     "RTT stress cancel")) return 1;
        } else {
          std::vector<uint8_t> outColor;
          std::vector<float> outDepth;
          if (!check(pollUntilReady(inFlight[j], outColor, outDepth, diagnostic) ==
                       SoWgpuRenderTarget::READBACK_READY &&
                     outColor.size() == 64u * 64u * 4u &&
                     outDepth.size() == 64u * 64u,
                     "RTT stress poll after target destruction")) return 1;
        }
      }
#if defined(HAVE_WGPU_RUST_BRIDGE)
      coin_wgpu_poll_device();
      uint64_t activeRtt = UINT64_MAX, retiredRtt = UINT64_MAX;
      coin_wgpu_rtt_resource_counts(&activeRtt, &retiredRtt);
      if (!check(activeRtt == 0 && retiredRtt == 0,
                 "RTT stress retained producer after completed batch")) return 1;
#endif
    }
    sceneTexture->size.setValue(32, 32);
    material->diffuseColor.setValue(0.8f, 0.2f, 0.1f);
  }
  texturedParent->unref();
  delete synchronous;
  root->unref();
  std::cout << "WgpuAsyncActionTest passed\n";
  return 0;
}
