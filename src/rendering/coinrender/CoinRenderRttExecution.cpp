#include "rendering/coinrender/CoinRenderSamplingCore.h"
#include <utility>
#include <algorithm>
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinrender/CoinRenderRttExecution.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderImageCore.h"
#include "rendering/coinrender/CoinRenderCubeTextureCore.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include <cstdio>
#include <new>
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#endif

namespace {
bool sameStagedOptions(const CoinRenderOptions & a, const CoinRenderOptions & b)
{
  return a.renderer == b.renderer && a.transparency == b.transparency &&
    a.sceneTexture == b.sceneTexture && a.storedTextureColorSpace == b.storedTextureColorSpace &&
    a.maxTextureAnisotropy == b.maxTextureAnisotropy &&
    a.textureSamplingPolicy == b.textureSamplingPolicy &&
    a.rasterizationPolicy == b.rasterizationPolicy && a.sampleCount == b.sampleCount;
}

CoinRenderDeviceDomain stagedDomain(const CoinRenderTargetP & target)
{
  return target.backend ? target.backend->resourceDomain() : target.preparedDomain;
}

bool sameStagedDomain(const CoinRenderDeviceDomain & a, const CoinRenderDeviceDomain & b)
{
  return a.device == b.device && a.generation == b.generation;
}

bool sameStagedKey(const CoinRenderStagedTargetKey & a, const CoinRenderStagedTargetKey & b)
{
  return a.size == b.size && sameStagedOptions(a.options, b.options) &&
    a.format == b.format && a.mipmaps == b.mipmaps &&
    a.owner == b.owner && a.generation == b.generation && sameStagedDomain(a.domain, b.domain);
}

// Execution-only lease: a second active occurrence cannot receive the same
// target. A successful readback is copied into the occurrence's owned image
// before this lease can return its scratch attachments/backend to the owner.
class StagedTargetLease {
public:
  StagedTargetLease(CoinRenderTargetP * parent, const CoinRenderOptions & options,
                    const SbVec2i32 & size, const CoinRenderFramePlan & frame)
    : parent(parent)
  {
    key.size = size;
    key.options = options;
    key.format = frame.outputColorFormat;
    key.mipmaps = frame.outputMipmaps;
    if (parent) {
      key.owner = parent->resourceOwnerId;
      key.generation = parent->resourceGeneration;
      key.domain = stagedDomain(*parent);
      for (auto it = parent->stagedTargets.begin(); it != parent->stagedTargets.end();) {
        const auto & candidate = it->target->getPimpl().get();
        const bool validOwner = it->key.owner == key.owner && it->key.generation == key.generation &&
          sameStagedDomain(it->key.domain, key.domain);
        const bool eligible = validOwner && it->target->getStatus() == CoinRenderTarget::TARGET_READY &&
          candidate.backend && sameStagedDomain(candidate.backend->resourceDomain(), key.domain);
        if (!eligible) {
          // Loss/resize invalidation retires idle registrations here, never
          // from inside the registry's shared-retirement iteration.
          it = parent->stagedTargets.erase(it);
        } else if (!child && sameStagedKey(it->key, key)) {
          child = std::move(it->target);
          it = parent->stagedTargets.erase(it);
          reused = true;
        } else {
          ++it;
        }
      }
    }
    if (!child) child.reset(CoinRenderTarget::createOffscreen(size, options));
    if (child) child->getPimpl()->requiresCurrentReadback = true;
    if (CoinRenderDiagnosticShell::phaseTracingEnabled())
      std::fprintf(stderr, "COIN_RENDER_PHASE rtt_staged_target_cache reused=%u owner=%llu generation=%llu width=%d height=%d samples=%u retained_idle_limit=%zu\n",
        reused ? 1u : 0u, static_cast<unsigned long long>(key.owner),
        static_cast<unsigned long long>(key.generation), size[0], size[1], options.sampleCount,
        CoinRenderTargetP::stagedIdleTargetLimit);
  }
  ~StagedTargetLease()
  {
    // Loss may invalidate every registered peer while a child executes. Never
    // destroy a registered idle child from inside that registry iteration;
    // the generation/domain check here discards it outside the lock instead.
    if (!completed || !parent || !child ||
        child->getStatus() != CoinRenderTarget::TARGET_READY ||
        key.owner != parent->resourceOwnerId || key.generation != parent->resourceGeneration ||
        !sameStagedDomain(key.domain, stagedDomain(*parent))) return;
    const auto & candidate = child->getPimpl().get();
    if (!candidate.backend || !sameStagedDomain(candidate.backend->resourceDomain(), key.domain)) return;
    // Returning a lease is optional cache maintenance. Allocation failure
    // retires this scratch target without changing the completed execution.
    try {
      if (parent->stagedTargets.capacity() < CoinRenderTargetP::stagedIdleTargetLimit)
        parent->stagedTargets.reserve(CoinRenderTargetP::stagedIdleTargetLimit);
    } catch (const std::bad_alloc &) { return; }
    const uint64_t used = ++parent->stagedTargetUse;
    for (auto & entry : parent->stagedTargets) if (sameStagedKey(entry.key, key)) {
      entry.target = std::move(child);
      entry.used = used;
      return;
    }
    if (parent->stagedTargets.size() >= CoinRenderTargetP::stagedIdleTargetLimit) {
      const auto oldest = std::min_element(parent->stagedTargets.begin(), parent->stagedTargets.end(),
        [](const CoinRenderTargetP::StagedTargetEntry & a, const CoinRenderTargetP::StagedTargetEntry & b) {
          return a.used < b.used;
        });
      parent->stagedTargets.erase(oldest);
    }
    CoinRenderTargetP::StagedTargetEntry entry;
    entry.key = key;
    entry.used = used;
    entry.target = std::move(child);
    parent->stagedTargets.push_back(std::move(entry));
  }
  StagedTargetLease(const StagedTargetLease &) = delete;
  StagedTargetLease & operator=(const StagedTargetLease &) = delete;
  CoinRenderTarget * operator->() const { return child.get(); }
  explicit operator bool() const { return bool(child); }
  void retain() { completed = true; }
private:
  CoinRenderTargetP * parent;
  CoinRenderStagedTargetKey key;
  std::unique_ptr<CoinRenderTarget> child;
  bool reused = false, completed = false;
};
} // namespace

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

CoinRenderSubmitResult CoinRenderRttExecution::preflight(const CoinRenderRttPlan& graph,
    const CoinRenderFramePlan& root) {
  std::string diagnostic;
  if (target && (target->suspended || target->size[0] <= 0 || target->size[1] <= 0))
    return {CoinRenderBackendStatus::NOT_READY,
            "Scene texture consumer target is suspended or has zero size"};
  if (!graph.validate(root, diagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  if (root.rasterizationPolicy != options.rasterizationPolicy)
    return {CoinRenderBackendStatus::UNSUPPORTED, "RTT root rasterization policy differs from target"};
  if (root.textureSamplingPolicy != options.textureSamplingPolicy || !coin_render_valid_sampling(root, diagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic.empty() ? "RTT root sampling policy differs from target policy" : diagnostic};
  const bool staged = graph.mode == COIN_RENDER_SCENE_TEXTURE_STAGED;
  for (const auto& producer : graph.producers) for(size_t face=0;face<producer.faceCount();++face) {
    const auto & producerFrame=producer.face(face);
    if (producerFrame.rasterizationPolicy != root.rasterizationPolicy)
      return {CoinRenderBackendStatus::UNSUPPORTED, "RTT producer rasterization policy differs from consumer"};
    if (producerFrame.textureSamplingPolicy != root.textureSamplingPolicy ||
        !coin_render_valid_sampling(producerFrame, diagnostic))
      return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic.empty() ? "RTT producer sampling policy differs from consumer policy" : diagnostic};
    auto check = CoinRenderTargetP::validateProfile(producerFrame, producer.size, staged);
    if (check.status != CoinRenderBackendStatus::SUCCESS)
      return check;
  }
  auto check =
      CoinRenderTargetP::validateProfile(root, target ? target->size : SbVec2i32(640, 480), staged);
  if (check.status != CoinRenderBackendStatus::SUCCESS)
    return check;

  std::unique_ptr<CoinRenderBackend> preflightBackend;
  CoinRenderBackend* inspector = target ? target->backend.get() : nullptr;
  if (!inspector) {
    preflightBackend = CoinRenderTargetP::createBackend();
    inspector = preflightBackend.get();
  }

  if (target) inspector->configureMultipassTarget(*target);
  bool portableExecution = root.hasPortableShaderDraws();
  for (const auto &producer : graph.producers)
    for (size_t face = 0; face < producer.faceCount(); ++face)
      portableExecution = portableExecution || producer.face(face).hasPortableShaderDraws();
  if (portableExecution && (!inspector || !inspector->isGpuBackend()))
    return {CoinRenderBackendStatus::UNSUPPORTED,
            "Portable shader module executor is unavailable before scene texture production"};

  check = inspector->preflightMultipassGraph(graph, root, target ? target->size : SbVec2i32(640, 480));
  if (check.status != CoinRenderBackendStatus::SUCCESS) return check;
  check = inspector->preflightMultipass(root, target ? target->size : SbVec2i32(640, 480));
  if (check.status != CoinRenderBackendStatus::SUCCESS) return check;
  for (const auto & producer : graph.producers) for (size_t face=0; face<producer.faceCount(); ++face) {
    check = inspector->preflightMultipass(producer.face(face), producer.size);
    if (check.status != CoinRenderBackendStatus::SUCCESS) return check;
  }
  // Pure/known connector limits must reject before device preparation. A
  // second inspection after prepare checks selected-adapter format facts.
  check = inspector->preflightRtt(graph, root, target ? target->size : SbVec2i32(640, 480));
  if (check.status != CoinRenderBackendStatus::SUCCESS) return check;

  return {};
}

CoinRenderSubmitResult CoinRenderRttExecution::prepare(const CoinRenderRttPlan& graph,
                                                       const CoinRenderFramePlan& root,
                                                       CoinRenderFramePlan& resolvedRoot) {
  std::string diagnostic;
  if (target && (target->suspended || target->size[0] <= 0 || target->size[1] <= 0))
    return {CoinRenderBackendStatus::NOT_READY,
            "Scene texture consumer target is suspended or has zero size"};
  if (!graph.validate(root, diagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  tokens.reserve(graph.tokenCount());
  resources.entries.reserve(graph.producers.size());
  if (root.rasterizationPolicy != options.rasterizationPolicy)
    return {CoinRenderBackendStatus::UNSUPPORTED, "RTT root rasterization policy differs from target"};
  if (root.textureSamplingPolicy != options.textureSamplingPolicy || !coin_render_valid_sampling(root, diagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic.empty() ? "RTT root sampling policy differs from target policy" : diagnostic};
  const bool staged = graph.mode == COIN_RENDER_SCENE_TEXTURE_STAGED;
  for (const auto& producer : graph.producers) for(size_t face=0;face<producer.faceCount();++face) {
    const auto & producerFrame=producer.face(face);
    if (producerFrame.rasterizationPolicy != root.rasterizationPolicy)
      return {CoinRenderBackendStatus::UNSUPPORTED, "RTT producer rasterization policy differs from consumer"};
    if (producerFrame.textureSamplingPolicy != root.textureSamplingPolicy ||
        !coin_render_valid_sampling(producerFrame, diagnostic))
      return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic.empty() ? "RTT producer sampling policy differs from consumer policy" : diagnostic};
    auto check = CoinRenderTargetP::validateProfile(producerFrame, producer.size, staged);
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

  if (target) inspector->configureMultipassTarget(*target);
  bool portableExecution = root.hasPortableShaderDraws();
  for (const auto &producer : graph.producers)
    for (size_t face = 0; face < producer.faceCount(); ++face)
      portableExecution = portableExecution || producer.face(face).hasPortableShaderDraws();
  if (portableExecution && (!inspector || !inspector->isGpuBackend()))
    return {CoinRenderBackendStatus::UNSUPPORTED,
            "Portable shader module executor is unavailable before scene texture production"};

  check = inspector->preflightMultipassGraph(graph, root, target ? target->size : SbVec2i32(640, 480));
  if (check.status != CoinRenderBackendStatus::SUCCESS) return check;
  check = inspector->preflightMultipass(root, target ? target->size : SbVec2i32(640, 480));
  if (check.status != CoinRenderBackendStatus::SUCCESS) return check;
  for (const auto & producer : graph.producers) for (size_t face=0; face<producer.faceCount(); ++face) {
    check = inspector->preflightMultipass(producer.face(face), producer.size);
    if (check.status != CoinRenderBackendStatus::SUCCESS) return check;
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
  check = inspector->preflightMultipassGraph(graph, root, target ? target->size : SbVec2i32(640,480));
  if (check.status != CoinRenderBackendStatus::SUCCESS) return check;
  check = inspector->preflightMultipass(root, target ? target->size : SbVec2i32(640,480));
  if (check.status != CoinRenderBackendStatus::SUCCESS) return check;
  for (const auto & producer : graph.producers) for (size_t face=0; face<producer.faceCount(); ++face) {
    check = inspector->preflightMultipass(producer.face(face), producer.size);
    if (check.status != CoinRenderBackendStatus::SUCCESS) return check;
  }
  check = inspector->preflightRtt(graph, root, target ? target->size : SbVec2i32(640, 480));
  if (check.status != CoinRenderBackendStatus::SUCCESS)
    return check;

  const CoinRenderResourceStamp capturedStamp = stamp();
  for (size_t i = 0; i < graph.producers.size(); ++i) {
    const auto& producer = graph.producers[i];
    if(producer.cube) {
      CoinRenderTextureImageSnapshot resultTexture;
      const bool mips=graph.requestsMips(i+1,root);
      if(!CoinRenderCubeTextureCore::describe(uint32_t(producer.size[0]),mips,resultTexture))
        return {CoinRenderBackendStatus::UNSUPPORTED,"Invalid cube RTT layout"};
      std::array<uint64_t,6> faceTokens={{0,0,0,0,0,0}};
      std::array<CoinRenderCubeTextureCore::Face,6> faceImages;
      for(size_t face=0;face<6;++face) {
        CoinRenderFramePlan frame;
        if(!resources.resolve(producer.face(face),stamp(),frame,diagnostic))
          return {CoinRenderBackendStatus::UNSUPPORTED,diagnostic};
        frame.outputColorFormat=CoinRenderTextureFormat::RGBA8_LINEAR;
        frame.outputMipmaps=directBackend && mips;
        if(directBackend) {
          uint64_t token=0;
          const auto result=directBackend->submitRtt(frame,producer.size,producer.sourceRevision,*target,token);
          const bool uniqueToken=token && std::find(tokens.begin(),tokens.end(),token)==tokens.end();
          if(uniqueToken)tokens.push_back(token);
          if(result.status!=CoinRenderBackendStatus::SUCCESS) {
            if(result.status==CoinRenderBackendStatus::DEVICE_LOST)target->deviceLost();return result;
          }
          if(!uniqueToken)return {CoinRenderBackendStatus::BACKEND_ERROR,"Cube RTT face returned no token or an aliased token"};
          faceTokens[face]=token;
        } else {
          StagedTargetLease child(target, options, producer.size, frame);
          if(!child||child->getStatus()!=CoinRenderTarget::TARGET_READY||!child->setDepthReadbackEnabled(FALSE))
            return {CoinRenderBackendStatus::BACKEND_ERROR,"Cannot allocate staged cube RTT face"};
          const auto result=child->getPimpl()->executeFrame(frame);
          if(result.status!=CoinRenderBackendStatus::SUCCESS)return result;
          auto & image=faceImages[face];image.width=producer.size[0];image.height=producer.size[1];image.components=4;
          child->readbackRGBA(image.pixels);
          if(!CoinRenderImageCore::flipRgba8Rows(image.pixels,producer.size))
            return {CoinRenderBackendStatus::BACKEND_ERROR,"Incomplete staged cube face readback"};
          child.retain();
        }
        if(!(stamp()==capturedStamp))return {CoinRenderBackendStatus::DEVICE_LOST,"Cube RTT generation changed between faces"};
      }
      if(directBackend) {
        uint64_t token=0;
        const auto result=directBackend->assembleCubeRtt(faceTokens,uint32_t(producer.size[0]),mips,*target,token);
        const bool uniqueToken=token && std::find(tokens.begin(),tokens.end(),token)==tokens.end();
        if(uniqueToken)tokens.push_back(token);
        if(result.status!=CoinRenderBackendStatus::SUCCESS) {
          if(result.status==CoinRenderBackendStatus::DEVICE_LOST)target->deviceLost();return result;
        }
        if(!uniqueToken)return {CoinRenderBackendStatus::BACKEND_ERROR,"Cube RTT assembly returned no token or an aliased token"};
        if(!(stamp()==capturedStamp))return {CoinRenderBackendStatus::DEVICE_LOST,"Cube RTT assembly changed generation"};
        // The atlas owns independent storage. Retire only our six temporary
        // faces after their last assembly use has been submitted. Backends keep
        // the actual allocations alive through their existing fences/queues.
        const std::vector<uint64_t> completedFaces(faceTokens.begin(),faceTokens.end());
        directBackend->retireRtt(completedFaces);
        tokens.erase(std::remove_if(tokens.begin(),tokens.end(),[&completedFaces](uint64_t candidate) {
          return std::find(completedFaces.begin(),completedFaces.end(),candidate)!=completedFaces.end();
        }),tokens.end());
        resultTexture.gpuToken=token;resultTexture.contentDigest=token;
      } else {
        CoinRenderCubeTextureCore::Atlas atlas;
        if(!CoinRenderCubeTextureCore::prepare(faceImages,mips,atlas))
          return {CoinRenderBackendStatus::BACKEND_ERROR,"Cannot assemble staged cube RTT"};
        resultTexture.pixelsRgba=std::move(atlas.pixelsRgba);
        resultTexture.contentDigest=CoinRenderImageCore::rgba8Digest(resultTexture.pixelsRgba);
      }
      if(!(stamp()==capturedStamp))return {CoinRenderBackendStatus::DEVICE_LOST,"Cube RTT assembly changed generation"};
      if(!resources.bind(i+1,producer.sourceRevision,capturedStamp,std::move(resultTexture),diagnostic))
        return {CoinRenderBackendStatus::BACKEND_ERROR,diagnostic};
      if (CoinRenderDiagnosticShell::phaseTracingEnabled()) {
        std::fprintf(stderr,"COIN_RENDER_PHASE rtt_producer_complete producer_id=%llu source_revision=%llu route=%s faces=6\n",
          static_cast<unsigned long long>(i+1), static_cast<unsigned long long>(producer.sourceRevision),
          directBackend ? "direct" : "staged");
        std::fflush(stderr);
      }
      continue;
    }
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
      const bool uniqueToken = token && std::find(tokens.begin(),tokens.end(),token)==tokens.end();
      if (uniqueToken)
        tokens.push_back(token);
      if (result.status != CoinRenderBackendStatus::SUCCESS) {
        if (result.status == CoinRenderBackendStatus::DEVICE_LOST)
          target->deviceLost();
        return result;
      }
      if (!uniqueToken)
        return {CoinRenderBackendStatus::BACKEND_ERROR,
                "Planned SoSceneTexture2 pass returned no GPU texture or an aliased token"};
      if (!(stamp() == capturedStamp))
        return {CoinRenderBackendStatus::DEVICE_LOST,
                "Scene texture device generation changed during execution"};
      resultTexture.gpuToken = token;
      resultTexture.contentDigest = token;
    } else {
      StagedTargetLease child(target, options, producer.size, frame);
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
      child.retain();
    }
    if (!resources.bind(i + 1, producer.sourceRevision, capturedStamp, std::move(resultTexture),
                        diagnostic))
      return {CoinRenderBackendStatus::BACKEND_ERROR, diagnostic};
    if (CoinRenderDiagnosticShell::phaseTracingEnabled()) {
      std::fprintf(stderr,"COIN_RENDER_PHASE rtt_producer_complete producer_id=%llu source_revision=%llu route=%s faces=1\n",
        static_cast<unsigned long long>(i+1), static_cast<unsigned long long>(producer.sourceRevision),
        directBackend ? "direct" : "staged");
      std::fflush(stderr);
    }
  }
  if (!resources.resolve(root, stamp(), resolvedRoot, diagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  return {};
}
