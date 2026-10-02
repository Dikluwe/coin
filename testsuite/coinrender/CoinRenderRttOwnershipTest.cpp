#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinrender/CoinRenderRttCore.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderRttExecution.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/SoGLRenderAction.h>
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#endif
#include <iostream>
#include <memory>

namespace {
bool check(bool ok, const char* message) {
  if (!ok)
    std::cerr << "CoinRenderRttOwnershipTest: " << message << '\n';
  return ok;
}
CoinRenderFramePlan consumer(uint64_t producer, uint32_t size = 1) {
  CoinRenderFramePlan frame;
  CoinRenderTextureImageSnapshot texture;
  texture.width = texture.height = size;
  texture.producerId = producer;
  frame.textures.push_back(texture);
  return frame;
}
CoinRenderRttProducer producer(uint64_t source, int size = 1) {
  CoinRenderRttProducer result;
  result.sourceRevision = source;
  result.size = SbVec2i32(size, size);
  return result;
}
CoinRenderFramePlan triangle(bool transparent = false, int viewportWidth = 4) {
  CoinRenderFramePlan result;
  result.materials.push_back(CoinRenderMaterialSnapshot());
  if (transparent) {
    result.materials[0].diffuse[3] = 0.5f;
    result.materials[0].transparency = 0.5f;
  }
  result.cameras.push_back(CoinRenderCameraSnapshot());
  result.lightingStates.push_back(CoinRenderLightingSnapshot());
  CoinRenderViewportSnapshot viewport;
  viewport.width = viewportWidth;
  viewport.height = 4;
  result.viewports.push_back(viewport);
  CoinRenderRenderStateSnapshot state;
  state.lightModel = CoinRenderLightModel::BASE_COLOR;
  state.transparencyType = SoGLRenderAction::SORTED_LAYERS_BLEND;
  result.renderStates.push_back(state);
  result.vertices.resize(3);
  result.vertices[0].position[0] = -1;
  result.vertices[1].position[0] = 1;
  result.vertices[2].position[1] = 1;
  result.indices = {0, 1, 2};
  CoinRenderDrawPacket draw;
  draw.geometry.vertexCount = draw.geometry.indexCount = 3;
  result.draws.push_back(draw);
  return result;
}
struct Witness {
  int submits = 0, finishes = 0;
  bool sawDependency = false, failSecond = false, changeEpoch = false, rejectPreflight = false;
  uint64_t epoch = 1;
  std::vector<uint64_t> retired;
};
class FixtureBackend : public CoinRenderBackend {
public:
  explicit FixtureBackend(std::shared_ptr<Witness> record) : witness(record) {}
  std::shared_ptr<Witness> witness;
  std::string diagnostic;
  bool isGpuBackend() const override { return true; }
  CoinRenderBackendStatus getStatus() const override { return CoinRenderBackendStatus::SUCCESS; }
  CoinRenderBackendStatus prepare(CoinRenderTargetP&) override {
    return CoinRenderBackendStatus::SUCCESS;
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&, CoinRenderTargetP&) override {
    return {};
  }
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan&, const CoinRenderFramePlan&,
                                      const SbVec2i32&) const override {
    return witness->rejectPreflight
               ? CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, "fixture GPU limit")
               : CoinRenderSubmitResult();
  }
  CoinRenderDeviceDomain resourceDomain() const override {
    CoinRenderDeviceDomain result;
    result.device = 7;
    result.generation = witness->epoch;
    return result;
  }
  CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan& frame, const SbVec2i32&, uint64_t,
                                   CoinRenderTargetP&, uint64_t& token) override {
    token = uint64_t(++witness->submits) + 100;
    if (witness->submits == 2)
      witness->sawDependency = frame.textures.size() == 1 && frame.textures[0].producerId == 0 &&
                               frame.textures[0].gpuToken == 101;
    if (witness->changeEpoch)
      ++witness->epoch;
    if (witness->failSecond && witness->submits == 2)
      return {CoinRenderBackendStatus::OUT_OF_MEMORY, "fixture allocation failed after output"};
    return {};
  }
  void finishRtt(const std::vector<uint64_t>& tokens) override {
    ++witness->finishes;
    witness->retired = tokens;
  }
  void poll() override {}
  const std::string& getLastError() const override { return diagnostic; }
};
} // namespace

int main() {
  SoDB::init();
  bool ok = true;
  std::string diagnostic;
  uint64_t id = 0;
  CoinRenderRttPlan graph(COIN_RENDER_SCENE_TEXTURE_DIRECT);
  auto first = producer(41);
  ok &= check(graph.append(first, id, diagnostic) && id == 1, "first logical ID");
  ok &= check(graph.append(first, id, diagnostic) && id == 1 && graph.chargedBytes == 8,
              "same producer payload must share charge");
  auto second = producer(42);
  second.plan = consumer(1);
  ok &= check(graph.append(second, id, diagnostic) && id == 2 && graph.chargedBytes == 16,
              "nested postorder producer");
  const auto root = consumer(2);
  ok &= check(graph.validate(root, diagnostic), "valid logical graph");
  auto wrongSize = consumer(2, 2);
  ok &= check(!graph.validate(wrongSize, diagnostic), "dimension mismatch rejected");
  ok &= check(!graph.validate(consumer(3), diagnostic), "missing producer rejected");
  auto corrupt = graph;
  corrupt.producers[0].plan = consumer(1);
  ok &= check(!corrupt.validate(root, diagnostic), "self dependency rejected");
  corrupt = graph;
  corrupt.producers[0].plan = consumer(2);
  ok &= check(!corrupt.validate(root, diagnostic), "future dependency rejected");
  auto raw = root;
  raw.textures[0].producerId = 0;
  raw.textures[0].gpuToken = 9;
  ok &= check(!graph.validate(raw, diagnostic), "capture cannot import concrete tokens");
  auto ambiguous = root;
  ambiguous.textures[0].gpuToken = 9;
  ok &= check(!ambiguous.isValid(&diagnostic),
              "reference cannot be captured and resolved simultaneously");
  auto changed = first;
  changed.plan.clearColor = SbColor4f(1, 0, 0, 1);
  ok &= check(graph.append(changed, id, diagnostic) && id == 3,
              "changed payload creates new identity");
  auto unrelated = producer(99);
  ok &= check(graph.append(unrelated, id, diagnostic) && id == 4,
              "distinct source must not alias identical payload");

  CoinRenderRttPlan stack;
  ok &= check(stack.enter(1, SbVec2i32(1, 1), diagnostic), "source stack entry");
  ok &= check(!stack.enter(1, SbVec2i32(1, 1), diagnostic), "cycle before child capture");
  stack.leave();
  for (uint64_t source = 1; source <= 8; ++source)
    ok &= check(stack.enter(source, SbVec2i32(1, 1), diagnostic), "eight nesting levels");
  ok &= check(!stack.enter(9, SbVec2i32(1, 1), diagnostic), "ninth nesting level rejected");
  CoinRenderRttPlan budget(COIN_RENDER_SCENE_TEXTURE_DIRECT);
  ok &= check(budget.append(producer(1, 2048), id, diagnostic) &&
                  budget.append(producer(2, 2048), id, diagnostic),
              "exact 64 MiB direct budget");
  ok &= check(!budget.append(producer(3), id, diagnostic) && budget.producers.size() == 2 &&
                  budget.chargedBytes == CoinRenderRttPlan::budget(),
              "overbudget append is atomic");

  {
    auto policy = triangle();
    policy.renderStates[0].hasTexture = true;
    policy.textures = consumer(1, 4).textures;
    policy.samplers.push_back(CoinRenderSamplerSnapshot());
    policy.textures[0].sceneTransparencyFunction = SoSceneTexture2::NONE;
    std::vector<CoinRenderCompositionItem> order;
    ok &= check(coin_render_composition_order(policy, order, diagnostic) && !order[0].blend,
                "Coin NONE suppresses unresolved texture transparency");
    policy.textures[0].gpuOpaque = true;
    policy.textures[0].sceneTransparencyFunction = SoSceneTexture2::ALPHA_BLEND;
    ok &= check(coin_render_composition_order(policy, order, diagnostic) && order[0].blend,
                "Coin ALPHA_BLEND forces transparency even with an opaque producer");
    policy.textures[0].sceneTransparencyFunction = SoSceneTexture2::NONE;
    policy.materials[0].diffuse[3] = policy.materials[0].transparency = .5f;
    ok &= check(coin_render_composition_order(policy, order, diagnostic) && order[0].blend,
                "Coin NONE preserves material transparency");
    policy.textures[0].sceneTransparencyFunction = SoSceneTexture2::ALPHA_TEST;
    ok &= check(policy.isValid(&diagnostic) &&
                coin_render_composition_order(policy, order, diagnostic) && order[0].blend,
                "Coin ALPHA_TEST follows the GL transparent scheduling contract");
    policy.materials[0].diffuse[3] = 1.0f;
    policy.materials[0].transparency = 0.0f;
    ok &= check(coin_render_composition_order(policy, order, diagnostic) && order[0].blend,
                "Coin ALPHA_TEST forces transparency for an opaque producer and material");
    policy.textures[0].sceneTransparencyFunction = 999;
    ok &= check(!policy.isValid(&diagnostic), "unknown alpha policy remains unsupported");
  }

  CoinRenderRttResources resources;
  CoinRenderResourceStamp stamp;
  stamp.owner = 10;
  stamp.targetGeneration = 3;
  stamp.device = 7;
  stamp.deviceGeneration = 2;
  CoinRenderTextureImageSnapshot texture;
  texture.width = texture.height = 1;
  texture.gpuToken = 72;
  ok &= check(resources.bind(1, 41, stamp, texture, diagnostic), "bind retained opaque resource");
  CoinRenderFramePlan output;
  auto captured = consumer(1);
  captured.textures[0].sceneTransparencyFunction = SoSceneTexture2::ALPHA_BLEND;
  ok &= check(resources.resolve(captured, stamp, output, diagnostic) &&
                  output.textures[0].gpuToken == 72 && !output.textures[0].producerId &&
                  captured.textures[0].producerId == 1 && !captured.textures[0].gpuToken &&
                  output.textures[0].sceneTransparencyFunction == SoSceneTexture2::ALPHA_BLEND,
              "resolve preserves immutable capture");
  for (int field = 0; field < 4; ++field) {
    auto stale = stamp;
    if (field == 0)
      ++stale.owner;
    if (field == 1)
      ++stale.targetGeneration;
    if (field == 2)
      ++stale.device;
    if (field == 3)
      ++stale.deviceGeneration;
    CoinRenderFramePlan sentinel;
    sentinel.revision = 765;
    ok &=
        check(!resources.resolve(captured, stale, sentinel, diagnostic) && sentinel.revision == 765,
              "cross-owner/device/resize/loss rejection must preserve output");
  }
  resources.invalidate();
  ok &= check(!resources.resolve(captured, stamp, output, diagnostic),
              "invalidation drops logical bindings");

  CoinRenderOptions options;
  options.sceneTexture = COIN_RENDER_SCENE_TEXTURE_DIRECT;
  CoinRenderTargetP target(SbVec2i32(4, 4));
  auto witness = std::make_shared<Witness>();
  target.options = options;
  target.optionsDiagnostic.clear();
  target.backend.reset(new FixtureBackend(witness));
  CoinRenderRttPlan executionGraph(COIN_RENDER_SCENE_TEXTURE_DIRECT);
  executionGraph.append(first, id, diagnostic);
  executionGraph.append(second, id, diagnostic);
  {
    CoinRenderRttExecution execution(&target, options);
    CoinRenderFramePlan resolved;
    auto result = execution.prepare(executionGraph, root, resolved);
    ok &= check(result.status == CoinRenderBackendStatus::SUCCESS && witness->submits == 2 &&
                    witness->sawDependency && witness->finishes == 0 &&
                    resolved.textures[0].gpuToken == 102,
                "resources retained across nested producer and root preparation");
  }
  ok &= check(witness->finishes == 1 && witness->retired == std::vector<uint64_t>({101, 102}),
              "scope retires all resources after consumer submit scope");
  *witness = Witness();
  witness->failSecond = true;
  {
    CoinRenderRttExecution execution(&target, options);
    CoinRenderFramePlan untouched;
    untouched.revision = 55;
    auto result = execution.prepare(executionGraph, root, untouched);
    ok &= check(result.status == CoinRenderBackendStatus::OUT_OF_MEMORY && untouched.revision == 55,
                "partial producer failure never publishes root");
  }
  ok &= check(witness->retired == std::vector<uint64_t>({101, 102}),
              "failure releases even returned partial allocation");
  *witness = Witness();
  witness->changeEpoch = true;
  {
    CoinRenderRttExecution execution(&target, options);
    CoinRenderFramePlan resolved;
    auto result = execution.prepare(executionGraph, root, resolved);
    ok &= check(result.status == CoinRenderBackendStatus::DEVICE_LOST && witness->submits == 1,
                "generation change stops before next producer");
  }
  *witness = Witness();
  {
    CoinRenderRttExecution execution(&target, options);
    auto invalidGraph = executionGraph;
    invalidGraph.producers[1].plan = consumer(3);
    CoinRenderFramePlan resolved;
    auto result = execution.prepare(invalidGraph, root, resolved);
    ok &= check(result.status != CoinRenderBackendStatus::SUCCESS && witness->submits == 0,
                "whole graph validated before any GPU producer");
  }
  *witness = Witness();
  witness->rejectPreflight = true;
  {
    CoinRenderRttExecution execution(&target, options);
    CoinRenderFramePlan untouched;
    untouched.revision = 555;
    auto result = execution.prepare(executionGraph, root, untouched);
    ok &= check(result.status == CoinRenderBackendStatus::UNSUPPORTED && witness->submits == 0 &&
                    untouched.revision == 555,
                "backend limit rejected before any producer");
  }
  *witness = Witness();
  target.kind = CoinRenderTargetP::KIND_WINDOW;
  {
    CoinRenderRttExecution execution(&target, options);
    CoinRenderFramePlan resolved;
    auto result = execution.prepare(executionGraph, root, resolved);
    ok &= check(result.status == CoinRenderBackendStatus::UNSUPPORTED && witness->submits == 0,
                "direct window RTT must not silently stage");
  }
  target.kind = CoinRenderTargetP::KIND_OFFSCREEN;
  {
    CoinRenderRttExecution execution(nullptr, options);
    CoinRenderFramePlan resolved;
    auto result = execution.prepare(executionGraph, root, resolved);
    ok &= check(result.status == CoinRenderBackendStatus::UNSUPPORTED,
                "direct recording RTT must not silently stage");
  }
  const uint64_t owner = target.resourceOwnerId, epoch = target.resourceGeneration;
  target.resize(SbVec2i32(8, 8));
  CoinRenderTargetP other(SbVec2i32(8, 8));
  ok &= check(target.resourceOwnerId == owner && target.resourceGeneration > epoch &&
                  other.resourceOwnerId != owner,
              "target resize and identity are independent");
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  {
    auto inspector = CoinRenderTargetP::createBackend();
    CoinRenderRttPlan capacity(COIN_RENDER_SCENE_TEXTURE_DIRECT);
    for (unsigned source = 1; source <= 64; ++source)
      ok &= check(capacity.append(producer(source), id, diagnostic), "capture capacity fixture");
    ok &= check(inspector->preflightRtt(capacity, consumer(64), SbVec2i32(4, 4)).status ==
                    CoinRenderBackendStatus::SUCCESS,
                "64 direct producers fit the bridge");
    capacity.append(producer(65), id, diagnostic);
    CoinRenderTargetP noWork(SbVec2i32(4, 4));
    CoinWgpuCacheStats before{}, after{};
    coin_wgpu_get_cache_stats(&before);
    CoinRenderRttExecution execution(&noWork, options);
    CoinRenderFramePlan resolved;
    const auto result = execution.prepare(capacity, consumer(65), resolved);
    coin_wgpu_get_cache_stats(&after);
    ok &= check(result.status == CoinRenderBackendStatus::UNSUPPORTED && !noWork.backend &&
                    before.submission_serial == after.submission_serial,
                "65th direct producer rejected without GPU work or device preparation");
  }
  for (auto mode : {COIN_RENDER_SCENE_TEXTURE_STAGED, COIN_RENDER_SCENE_TEXTURE_DIRECT}) {
    CoinRenderRttPlan external(mode);
    external.append(producer(10), id, diagnostic);
    auto outside = producer(11, 4);
    outside.plan = triangle(false, 5);
    external.append(outside, id, diagnostic);
    CoinRenderTargetP noWork(SbVec2i32(4, 4));
    CoinWgpuCacheStats before{}, after{};
    coin_wgpu_get_cache_stats(&before);
    CoinRenderRttExecution execution(&noWork, options);
    CoinRenderFramePlan resolved;
    const auto result = execution.prepare(external, consumer(2, 4), resolved);
    coin_wgpu_get_cache_stats(&after);
    ok &= check(result.status == CoinRenderBackendStatus::UNSUPPORTED && !noWork.backend &&
                    before.submission_serial == after.submission_serial,
                "external second viewport rejected before either RTT mode submits first producer");
  }
#elif defined(HAVE_COIN_BGFX)
  {
    CoinRenderRttPlan limited(COIN_RENDER_SCENE_TEXTURE_DIRECT);
    limited.append(producer(10), id, diagnostic);
    auto layers = producer(11, 4);
    layers.plan = triangle(true);
    limited.append(layers, id, diagnostic);
    CoinRenderTargetP noWork(SbVec2i32(4, 4));
    CoinRenderRttExecution execution(&noWork, options);
    CoinRenderFramePlan resolved;
    const auto result = execution.prepare(limited, consumer(2, 4), resolved);
    if (result.status != CoinRenderBackendStatus::UNSUPPORTED ||
        result.diagnostic.find("transparency") == std::string::npos)
      std::cerr << "BGFX direct preflight: " << result.diagnostic << '\n';
    ok &= check(result.status == CoinRenderBackendStatus::UNSUPPORTED && !noWork.backend &&
                    result.diagnostic.find("transparency") != std::string::npos,
                "unavailable second producer peeling rejected before BGFX initializes");
    auto inspector = CoinRenderTargetP::createBackend();
    limited.producers[1].plan = triangle(false);
    const auto opaquePreflight = inspector->preflightRtt(limited, consumer(2, 4), SbVec2i32(4, 4));
    if (opaquePreflight.status != CoinRenderBackendStatus::SUCCESS)
      std::cerr << "BGFX opaque preflight: " << opaquePreflight.diagnostic << '\n';
    ok &= check(opaquePreflight.status ==
                    CoinRenderBackendStatus::SUCCESS,
                "opaque producer using Coin sorted layers does not require peeling");
  }
#endif
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE) || defined(HAVE_COIN_BGFX)
  // The same Coin origin may produce distinct captured payloads within one
  // apply. Both outputs must remain independently retained for its consumers.
  CoinRenderTargetP gpuTarget(SbVec2i32(4, 4));
  CoinRenderOptions gpuOptions = gpuTarget.options; // Keep the matrix renderer captured by Shell.
  gpuOptions.sceneTexture = COIN_RENDER_SCENE_TEXTURE_DIRECT;
  gpuOptions.transparency = COIN_RENDER_TRANSPARENCY_COIN;
  gpuTarget.options = gpuOptions;
  gpuTarget.optionsDiagnostic.clear();
  CoinRenderRttPlan distinctOutputs(COIN_RENDER_SCENE_TEXTURE_DIRECT);
  auto redOutput = producer(123);
  redOutput.plan.clearColor = SbColor4f(1, 0, 0, 1);
  auto blueOutput = producer(123);
  blueOutput.plan.clearColor = SbColor4f(0, 0, 1, 1);
  distinctOutputs.append(redOutput, id, diagnostic);
  distinctOutputs.append(blueOutput, id, diagnostic);
  auto bothOutputs = consumer(1);
  bothOutputs.textures.push_back(consumer(2).textures[0]);
  {
    CoinRenderRttExecution execution(&gpuTarget, gpuOptions);
    CoinRenderFramePlan resolved;
    const auto result = execution.prepare(distinctOutputs, bothOutputs, resolved);
    ok &= check(result.status == CoinRenderBackendStatus::SUCCESS && resolved.textures.size() == 2 &&
                resolved.textures[0].gpuToken && resolved.textures[1].gpuToken &&
                resolved.textures[0].gpuToken != resolved.textures[1].gpuToken,
                "live GPU outputs from one origin must not overwrite or alias each other");
    if (result.status != CoinRenderBackendStatus::SUCCESS)
      std::cerr << result.diagnostic << '\n';
  }
#endif
  if (!ok)
    return 1;
  std::cout << "CoinRenderRttOwnershipTest passed\n";
  return 0;
}
