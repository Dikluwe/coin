#include "CoinRenderPortableShaderModuleScene.h"
#include "rendering/coinrender/CoinRenderBackend.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderRecordingBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/rendering/CoinRenderShaderModule.h>
#include <cctype>

namespace portable_module_test {
// Real CPU executors, observed without installing a GPU backend/callback or
// querying capabilities. Preparation entries are not GPU command submissions.
class CpuExecutionAudit : public CoinRenderBackend {
public:
  std::unique_ptr<CoinRenderBackend> real;
  unsigned submissions = 0, producers = 0, preparations = 0;
  mutable unsigned preflights = 0;
  CoinRenderFramePlan last;
  explicit CpuExecutionAudit(std::unique_ptr<CoinRenderBackend> backend)
      : real(std::move(backend)) {}
  bool isGpuBackend() const override { return real->isGpuBackend(); }
  bool initializesCpuDepthBuffer() const override {
    return real->initializesCpuDepthBuffer();
  }
  CoinRenderBackendStatus getStatus() const override {
    return real->getStatus();
  }
  CoinRenderBackendStatus prepare(CoinRenderTargetP &target) override {
    ++preparations;
    return real->prepare(target);
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &frame,
                                CoinRenderTargetP &target) override {
    ++submissions;
    last = frame;
    return real->submit(frame, target);
  }
  CoinRenderSubmitResult
  submit(const CoinRenderFramePlan &frame, CoinRenderTargetP &target,
         const CoinRenderFrameReuseDecision &reuse) override {
    ++submissions;
    last = frame;
    return real->submit(frame, target, reuse);
  }
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan &graph,
                                      const CoinRenderFramePlan &frame,
                                      const SbVec2i32 &size) const override {
    ++preflights;
    return real->preflightRtt(graph, frame, size);
  }
  CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan &frame,
                                   const SbVec2i32 &size, uint64_t id,
                                   CoinRenderTargetP &target,
                                   uint64_t &token) override {
    ++producers;
    return real->submitRtt(frame, size, id, target, token);
  }
  void poll() override { real->poll(); }
  const std::string &getLastError() const override {
    return real->getLastError();
  }
};

bool cpuExecution(bool recording) {
  Scene scene, source;
  CoinRenderOptions options{};
  options.sampleCount = 1;
  options.sceneTexture = COIN_RENDER_SCENE_TEXTURE_STAGED;
  std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createOffscreen(SbVec2i32(64, 64), options));
  if (!check(bool(target), "CPU public target allocation succeeds"))
    return false;
  std::unique_ptr<CoinRenderBackend> backend;
  CoinRenderRecordingBackend *recorder = nullptr;
  if (recording) {
    recorder = new CoinRenderRecordingBackend;
    backend.reset(recorder);
  } else
    backend.reset(new CoinRenderCpuReferenceBackend);
  auto *audit = new CpuExecutionAudit(std::move(backend));
  // Injection precedes prepareBackend: no default GPU executor is initialized.
  target->getPimpl()->backend.reset(audit);
  bool ok = check(target->getPimpl()->prepareBackend() ==
                          CoinRenderBackendStatus::SUCCESS &&
                      !audit->isGpuBackend(),
                  "explicit real CPU/RECORDING executor prepares without GPU");
  target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setRenderTarget(target.get());
  action.setTransparencyType(CoinRenderAction::NONE);
  action.setBackgroundColor(SbColor4f(0, 0, 0, 0));
  auto read = [&]() {
    Frame frame;
    target->readbackRGBA(frame.rgba);
    target->readbackDepth(frame.depth);
    frame.serial = target->getLastSubmissionSerial();
    return frame;
  };
  auto ordinary = [&](const char *label) {
    const auto previous = target->getLastSubmissionSerial();
    const auto calls = audit->submissions;
    const auto id = scene.root->getNodeId();
    action.apply(scene.root);
    const auto frame = read();
    bool current = check(
        action.getLastStatus() == CoinRenderAction::SUCCESS &&
            frame.serial > previous && audit->submissions == calls + 1 &&
            target->getPimpl()->backend.get() == audit &&
            !audit->real->isGpuBackend() && scene.root->getNodeId() == id &&
            frame.rgba.size() == 64 * 64 * 4 && frame.depth.size() == 64 * 64,
        label);
    current &=
        check(audit->last.portableShaders.empty() && !audit->last.draws.empty(),
              "ordinary recovery executes actual authored CPU geometry");
    for (const auto &state : audit->last.renderStates)
      current &= check(!state.portableShaderSlot,
                       "ordinary CPU draw has no consumed portable instance");
    if (!recording && current)
      current &= image(frame, {{.6f, .3f, .8f, 1}});
    if (recording)
      current &= check(!recorder->getLastRecordingLog().empty(),
                       "real RECORDING executor publishes its authored plan");
    ok &= current;
    return frame;
  };
  Frame stable =
      ordinary("ordinary predecessor publishes through actual CPU executor");
  CoinRenderShaderModule module;
  CoinRenderShaderExpression color;
  color.op = CoinRenderShaderOp::CONSTANT;
  color.type = CoinRenderShaderValueType::FLOAT4;
  color.value = {{.2f, .5f, .7f, 1}};
  module.expressions.push_back(color);
  module.alphaMayVary = FALSE;
  ok &= check(action.setPortableShaderModule(scene.program, module),
              "pure association remains legal for an explicit CPU action");
  auto *texture = new SoSceneTexture2;
  texture->ref();
  texture->scene = source.root;
  texture->size.setValue(64, 64);
  texture->type = SoSceneTexture2::RGBA8;
  texture->transparencyFunction = SoSceneTexture2::NONE;
  auto activeRtt = [&]() {
    scene.fragment->isActive = TRUE;
    scene.attach(true);
    scene.root->insertChild(texture, scene.root->findChild(scene.positions));
  };
  auto refuse = [&]() {
    const auto before = read();
    const auto calls = audit->submissions;
    const auto producers = audit->producers;
    const auto sourceId = source.root->getNodeId();
    const auto id = scene.root->getNodeId();
    const auto log = recorder ? recorder->getLastRecordingLog() : std::string();
    action.apply(scene.root);
    const auto after = read();
    std::string error = action.getLastError().getString();
    for (auto &c : error)
      c = char(std::tolower(static_cast<unsigned char>(c)));
    std::cout << "portable_module_cpu_execution backend="
              << (recording ? "RECORDING" : "ReferenceCPU")
              << " status=" << action.getLastStatus()
              << " serial=" << before.serial << "->" << after.serial
              << " cpu_entries=" << calls << "->" << audit->submissions
              << " producers=" << producers << "->" << audit->producers
              << " error=" << error << std::endl;
    bool refused = check(
        action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
            (error.find("shader") != std::string::npos ||
             error.find("module") != std::string::npos) &&
            (error.find("gpu") != std::string::npos ||
             error.find("cpu") != std::string::npos ||
             error.find("record") != std::string::npos) &&
            after.serial == before.serial && after.rgba == before.rgba &&
            after.depth == before.depth && audit->producers == producers &&
            target->getPimpl()->backend.get() == audit &&
            !audit->real->isGpuBackend() && scene.root->getNodeId() == id &&
            source.root->getNodeId() == sourceId,
        "active CPU module plus STAGED RTT refuses explicitly without producer "
        "execution, fallback or publication");
    if (recording)
      refused &= check(recorder->getLastRecordingLog() == log,
                       "rejected module cannot replace last real recording");
    return refused;
  };
  activeRtt();
  ok &= refuse();
  scene.root->removeChild(texture);
  scene.fragment->isActive = FALSE;
  stable = ordinary("inactive adapted object recovers same CPU target");
  activeRtt();
  ok &= refuse();
  scene.root->removeChild(texture);
  scene.attach(false);
  action.clearPortableShaderModule(scene.program);
  stable = ordinary("clear and detach recovers ordinary CPU execution");
  (void)stable;
  action.setRenderTarget(nullptr);
  texture->unref();
  return ok;
}
} // namespace portable_module_test
int main(int argc, char **) {
  if (argc != 1)
    return 2;
  SoDB::init();
  CoinRenderAction::initClass();
  bool ok = portable_module_test::cpuExecution(false);
  ok &= portable_module_test::cpuExecution(true);
  std::cout << "portable_module_explicit_cpu complete=" << ok
            << " capability_queries_in_fixture=0" << std::endl;
  return ok ? 0 : 1;
}
