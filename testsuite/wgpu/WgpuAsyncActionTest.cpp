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

  delete synchronous;
  root->unref();
  std::cout << "WgpuAsyncActionTest passed\n";
  return 0;
}
