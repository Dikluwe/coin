#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <iostream>
#include <memory>

#define CHECK(c) do { if (!(c)) { std::cerr << "Failed: " << #c << " at " << __LINE__ << '\n'; return 1; } } while (0)

// An executor unknown to Action and Target. No device, SDK or renderer identity.
class IndependentExecutor : public CoinRenderBackend {
public:
  bool shadows = true;
  int prepared = 0, submitted = 0, legacySubmitted = 0, asyncSubmitted = 0;
  bool capturedShadows = false;
  CoinRenderFrameReuseDecision lastReuse;
  CoinRenderFramePlan lastFrame;
  std::string error;
  bool isGpuBackend() const override { return false; }
  bool supportsOffscreenShadows() const override { return shadows; }
  CoinRenderBackendStatus getStatus() const override { return CoinRenderBackendStatus::SUCCESS; }
  CoinRenderBackendStatus prepare(CoinRenderTargetP &) override {
    ++prepared; return CoinRenderBackendStatus::SUCCESS;
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &, CoinRenderTargetP &) override {
    ++legacySubmitted;
    return {CoinRenderBackendStatus::BACKEND_ERROR, "Reuse-aware dispatch was bypassed"};
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
                                const CoinRenderFrameReuseDecision & reuse) override {
    ++submitted; lastReuse = reuse; lastFrame = frame;
    capturedShadows = !frame.shadowGroups.empty() && !frame.shadowLights.empty();
    target.clear(0.25f, 0.5f, 0.75f, 1.0f);
    return {CoinRenderBackendStatus::SUCCESS, "", static_cast<uint64_t>(submitted)};
  }
  bool readbackLoad(uint64_t & jobs, uint64_t & bytes) const override {
    jobs = bytes = 0; return true;
  }
  CoinRenderSubmitResult submitAsync(const CoinRenderFramePlan &, CoinRenderTargetP & target,
                                     CoinRenderReadbackTicket & ticket,
                                     const CoinRenderFrameReuseDecision & reuse) override {
    ++asyncSubmitted; lastReuse = reuse;
    target.clear(1,0,0,1); ticket.token = 123; // A rejected candidate must not leak.
    return {CoinRenderBackendStatus::UNSUPPORTED, "Independent executor rejected candidate"};
  }
  void poll() override {}
  const std::string & getLastError() const override { return error; }
};

void observeAction(void * data, SoAction * action) {
  *static_cast<SoType *>(data) = action->getTypeId();
}

int main() {
  SoDB::init();
  CoinRenderAction::initClass();
  CoinRenderTargetP unprepared(SbVec2i32(16,16));
  unprepared.supportsOffscreenShadows(false);
  CHECK(!unprepared.backend); // A fact query must not prepare or retain a device.

  CoinRenderSceneManager manager(SbVec2i32(16,16));
  auto & target = manager.getRenderTarget()->getPimpl();
  auto * executor = new IndependentExecutor;
  target->backend.reset(executor);
  CHECK(target->supportsOffscreenShadows(false));
  CHECK(executor->prepared == 0);
  CHECK(!target->supportsOffscreenShadows(true));
  target->directTextureOutput = true;
  CHECK(!target->supportsOffscreenShadows(false));
  target->directTextureOutput = false;
  target->kind = CoinRenderTargetP::KIND_WINDOW;
  CHECK(!target->supportsOffscreenShadows(false));
  target->kind = CoinRenderTargetP::KIND_OFFSCREEN;

  auto * scene = new SoSeparator;
  auto * camera = new SoOrthographicCamera;
  camera->position.setValue(0,0,8);
  camera->nearDistance = 1; camera->farDistance = 20;
  scene->addChild(camera);
  SoType observed;
  auto * callback = new SoCallback;
  callback->setCallback(observeAction, &observed);
  scene->addChild(callback);
  auto * group = new SoShadowGroup;
  auto * light = new SoShadowSpotLight;
  light->location.setValue(2,2,4); light->direction.setValue(-2,-2,-5);
  group->addChild(light); group->addChild(new SoCube);
  scene->addChild(group);
  manager.setSceneGraph(scene);
  CHECK(manager.render() == CoinRenderAction::SUCCESS);
  CHECK(observed == CoinRenderAction::getClassTypeId());
  CHECK(executor->submitted == 1 && executor->legacySubmitted == 0);
  CHECK(executor->capturedShadows);
  const CoinRenderFramePlan captured = executor->lastFrame;
  const CoinRenderFrameReuseDecision reuse(CoinRenderFrameReuseKind::REUSE, captured.revision);
  CHECK(target->executeFrame(captured, reuse).status == CoinRenderBackendStatus::SUCCESS);
  CHECK(executor->submitted == 2);
  CHECK(executor->lastReuse.kind == CoinRenderFrameReuseKind::REUSE);
  std::vector<uint8_t> before, after;
  manager.getRenderTarget()->readbackRGBA(before);
  CHECK(before.size() == 16 * 16 * 4 && before[3] == 255);

  CoinRenderReadbackTicket ticket{};
  const auto asynchronous = target->executeFrameAsync(captured, ticket, reuse);
  CHECK(asynchronous.status == CoinRenderBackendStatus::UNSUPPORTED);
  CHECK(asynchronous.diagnostic == "Independent executor rejected candidate");
  CHECK(executor->asyncSubmitted == 1 && ticket.token == 0);
  CHECK(executor->lastReuse.kind == reuse.kind && executor->lastReuse.baseRevision == reuse.baseRevision);
  manager.getRenderTarget()->readbackRGBA(after);
  CHECK(before == after && target->lastSubmissionSerial == 2);

  executor->shadows = false;
  CoinRenderAction rejected(SbViewportRegion(16,16));
  rejected.setRenderTarget(manager.getRenderTarget());
  rejected.apply(scene);
  CHECK(rejected.getLastStatus() == CoinRenderAction::UNSUPPORTED);
  CHECK(executor->submitted == 2);
  manager.getRenderTarget()->readbackRGBA(after);
  CHECK(before == after && target->lastSubmissionSerial == 2);
  std::cout << "Common action, manager, shadow admission and reuse dispatch passed\n";
  return 0;
}
