#include "rendering/coinrender/CoinRenderSamplingCore.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinrender/CoinRenderRttExecution.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderImageCore.h"
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#endif

CoinRenderRttExecution::CoinRenderRttExecution(CoinRenderTargetP* selected,
                                               const CoinRenderOptions& opts)
    : target(selected), options(opts),
      owner(selected ? selected->resourceOwnerId : CoinRenderTargetP::allocateResourceOwnerId()) {}

CoinRenderRttExecution::~CoinRenderRttExecution() {
  resources.invalidate();
  if (target && target->backend && (!directBackend || target->backend.get() == directBackend))
    target->backend->finishRtt(tokens);
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  else {
    // A lost device may have destroyed the C++ connector during root submit.
    // Bridge release is idempotent, including tokens of a discarded generation.
    for (uint64_t token : tokens)
      coin_wgpu_release_texture(token);
  }
#endif
}

CoinRenderResourceStamp CoinRenderRttExecution::stamp() const {
  CoinRenderResourceStamp result;
  result.owner = owner;
  result.targetGeneration = target ? target->resourceGeneration : 0;
  if (directBackend && target->backend.get() == directBackend) {
    const auto domain = directBackend->resourceDomain();
    result.device = domain.device;
    result.deviceGeneration = domain.generation;
  }
  return result;
}

CoinRenderSubmitResult CoinRenderRttExecution::prepare(const CoinRenderRttPlan& graph,
                                                       const CoinRenderFramePlan& root,
                                                       CoinRenderFramePlan& resolvedRoot) {
  std::string diagnostic;
  tokens.reserve(graph.producers.size());
  resources.entries.reserve(graph.producers.size());
  if (target && (target->suspended || target->size[0] <= 0 || target->size[1] <= 0))
    return {CoinRenderBackendStatus::NOT_READY,
            "Scene texture consumer target is suspended or has zero size"};
  if (!graph.validate(root, diagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  if (root.textureSamplingPolicy != options.textureSamplingPolicy || !coin_render_valid_sampling(root, diagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic.empty() ? "RTT root sampling policy differs from target policy" : diagnostic};
  const bool staged = graph.mode == COIN_RENDER_SCENE_TEXTURE_STAGED;
  for (const auto& producer : graph.producers) {
    if (producer.plan.textureSamplingPolicy != root.textureSamplingPolicy ||
        !coin_render_valid_sampling(producer.plan, diagnostic))
      return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic.empty() ? "RTT producer sampling policy differs from consumer policy" : diagnostic};
    auto check = CoinRenderTargetP::validateProfile(producer.plan, producer.size, staged);
    if (check.status != CoinRenderBackendStatus::SUCCESS)
      return check;
  }
  auto check =
      CoinRenderTargetP::validateProfile(root, target ? target->size : SbVec2i32(640, 480), staged);
  if (check.status != CoinRenderBackendStatus::SUCCESS)
    return check;
  if (graph.producers.empty()) {
    resolvedRoot = root;
    return {};
  }

  std::unique_ptr<CoinRenderBackend> preflightBackend;
  CoinRenderBackend* inspector = target ? target->backend.get() : nullptr;
  if (!inspector) {
    preflightBackend = CoinRenderTargetP::createBackend();
    inspector = preflightBackend.get();
  }

  // Pure/known connector limits must reject before device preparation. A
  // second inspection after prepare checks selected-adapter format facts.
  check = inspector->preflightRtt(graph, root, target ? target->size : SbVec2i32(640, 480));
  if (check.status != CoinRenderBackendStatus::SUCCESS) return check;


  if (graph.mode == COIN_RENDER_SCENE_TEXTURE_DIRECT) {
    if (!target)
      return {CoinRenderBackendStatus::UNSUPPORTED,
              "Direct scene texture requires a live GPU consumer target"};
    auto prepared = target->prepareBackend();
    if (prepared != CoinRenderBackendStatus::SUCCESS) {
      const auto error = target->backend->getLastError();
      if (prepared == CoinRenderBackendStatus::DEVICE_LOST)
        target->deviceLost();
      return {prepared, error};
    }
    directBackend = target->backend.get();
    if (!stamp().device || !stamp().deviceGeneration)
      return {CoinRenderBackendStatus::UNSUPPORTED,
              "Direct scene texture has no live device domain"};
  } else if (target && inspector->stagedRttRequiresPreparedParent()) {
    // Keep the shared runtime alive while temporary staged producers retire.
    const auto prepared = target->prepareBackend();
    if (prepared != CoinRenderBackendStatus::SUCCESS) {
      const auto error = target->backend->getLastError();
      if (prepared == CoinRenderBackendStatus::DEVICE_LOST)
        target->deviceLost();
      return {prepared, error};
    }
  }
  if (target && target->backend) inspector = target->backend.get();
  check = inspector->preflightRtt(graph, root, target ? target->size : SbVec2i32(640, 480));
  if (check.status != CoinRenderBackendStatus::SUCCESS)
    return check;

  const CoinRenderResourceStamp capturedStamp = stamp();
  for (size_t i = 0; i < graph.producers.size(); ++i) {
    const auto& producer = graph.producers[i];
    CoinRenderFramePlan frame;
    if (!resources.resolve(producer.plan, stamp(), frame, diagnostic))
      return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
    CoinRenderTextureImageSnapshot resultTexture;
    resultTexture.width = uint32_t(producer.size[0]);
    resultTexture.height = uint32_t(producer.size[1]);
    resultTexture.format=producer.format;
    if (directBackend) {
      frame.outputColorFormat=producer.format;
      frame.outputMipmaps=graph.requestsMips(i+1,root);
      resultTexture.mipmapped=frame.outputMipmaps;
      uint64_t token = 0;
      auto result =
          directBackend->submitRtt(frame, producer.size, producer.sourceRevision, *target, token);
      // Own any returned allocation even on failure; never leak partial outputs.
      if (token)
        tokens.push_back(token);
      if (result.status != CoinRenderBackendStatus::SUCCESS) {
        if (result.status == CoinRenderBackendStatus::DEVICE_LOST)
          target->deviceLost();
        return result;
      }
      if (!token)
        return {CoinRenderBackendStatus::BACKEND_ERROR,
                "Planned SoSceneTexture2 pass returned no GPU texture"};
      if (!(stamp() == capturedStamp))
        return {CoinRenderBackendStatus::DEVICE_LOST,
                "Scene texture device generation changed during execution"};
      resultTexture.gpuToken = token;
      resultTexture.contentDigest = token;
    } else {
      std::unique_ptr<CoinRenderTarget> child(
          CoinRenderTarget::createOffscreen(producer.size, options));
      if (!child || child->getStatus() != CoinRenderTarget::TARGET_READY ||
          !child->setDepthReadbackEnabled(FALSE))
        return {CoinRenderBackendStatus::BACKEND_ERROR,
                "Cannot create SoSceneTexture2 color-only offscreen target"};
      auto result = child->getPimpl()->executeFrame(frame);
      if (result.status != CoinRenderBackendStatus::SUCCESS)
        return result;
      child->readbackRGBA(resultTexture.pixelsRgba);
      if (resultTexture.pixelsRgba.size() !=
          uint64_t(resultTexture.width) * resultTexture.height * 4)
        return {CoinRenderBackendStatus::BACKEND_ERROR,
                "SoSceneTexture2 subscene returned incomplete RGBA8 readback"};
      // Coin's staged image convention is bottom-left; readback is top-left.
      if (!CoinRenderImageCore::flipRgba8Rows(resultTexture.pixelsRgba, producer.size))
        return {CoinRenderBackendStatus::BACKEND_ERROR,
                "SoSceneTexture2 subscene returned invalid RGBA8 dimensions"};
      resultTexture.contentDigest = CoinRenderImageCore::rgba8Digest(resultTexture.pixelsRgba);
    }
    if (!resources.bind(i + 1, producer.sourceRevision, capturedStamp, std::move(resultTexture),
                        diagnostic))
      return {CoinRenderBackendStatus::BACKEND_ERROR, diagnostic};
  }
  if (!resources.resolve(root, stamp(), resolvedRoot, diagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  return {};
}
