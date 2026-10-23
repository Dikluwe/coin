#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderMultipassCore.h"
#include "rendering/coinrender/CoinRenderRttCore.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderBackend.h"

CoinRenderSubmitResult
CoinRenderBackend::submit(const CoinRenderFramePlan &frame, CoinRenderTargetP &target,
                          const CoinRenderFrameReuseDecision &)
{
  return this->submit(frame, target);
}

CoinRenderSubmitResult
CoinRenderBackend::submitAsync(const CoinRenderFramePlan &, CoinRenderTargetP &,
                               CoinRenderReadbackTicket &, const CoinRenderFrameReuseDecision &)
{
  return {CoinRenderBackendStatus::UNSUPPORTED,
          "The selected backend does not support asynchronous readback"};
}

CoinRenderSubmitResult
CoinRenderBackend::preflightRtt(const CoinRenderRttPlan &, const CoinRenderFramePlan &,
                                const SbVec2i32 &) const
{
  return {};
}

CoinRenderSubmitResult
CoinRenderBackend::submitRtt(const CoinRenderFramePlan &, const SbVec2i32 &, uint64_t,
                             CoinRenderTargetP &, uint64_t &)
{
  return {CoinRenderBackendStatus::UNSUPPORTED,
          "Direct scene texture is not implemented; no fallback was applied"};
}

CoinRenderSubmitResult CoinRenderBackend::assembleCubeRtt(const std::array<uint64_t,6>&,
    uint32_t,bool,CoinRenderTargetP&,uint64_t & token) {
  token=0;return {CoinRenderBackendStatus::UNSUPPORTED,"Direct cube RTT assembly is not implemented"};
}

CoinRenderSubmitResult CoinRenderBackend::preflightMultipass(const CoinRenderFramePlan & frame,
                                                            const SbVec2i32 &) const {
  if (frame.numPasses > 1)
    return {CoinRenderBackendStatus::UNSUPPORTED,"Selected backend does not execute multipass accumulation"};
  return {};
}

CoinRenderSubmitResult CoinRenderBackend::preflightMultipassGraph(const CoinRenderRttPlan & graph,
    const CoinRenderFramePlan & root, const SbVec2i32 & extent) const {
  if (root.numPasses == 1) return {};
  uint64_t retained = 0;
  if (!CoinRenderMultipassCore::graphBytes(graph, root, extent, 1,
        CoinRenderMultipassCore::Executor::CPU, retained) || retained > root.multipassBufferBudget)
    return {CoinRenderBackendStatus::OUT_OF_MEMORY, "Multipass simultaneous RTT resources exceed the buffer budget"};
  CoinRenderFramePlan admitted = root;
  admitted.multipassBufferBudget -= retained;
  return this->preflightMultipass(admitted, extent);
}

bool CoinRenderBackend::multipassOutputBytes(const CoinRenderTargetP & target, bool, uint64_t & bytes) const {
  bytes = 0;
  if (target.size[0] < 0 || target.size[1] < 0) return false;
  const uint64_t channels = target.depthReadbackEnabled ? 8 : 4;
  uint64_t candidate = 0;
  if (target.kind == CoinRenderTargetP::KIND_OFFSCREEN && !target.directTextureOutput &&
      !CoinRenderMultipassCore::addAttachmentBytes(target.size[0],target.size[1],channels,1,candidate)) return false;
  // Atomic publication retains the previous output while its candidate is
  // prepared. Existing spare storage is reused, never charged a second time.
  uint64_t spare = 0;
  return CoinRenderMultipassCore::addAttachmentBytes(target.spareColorBuffer.size(),1,1,1,spare) &&
    CoinRenderMultipassCore::addAttachmentBytes(target.spareDepthBuffer.size(),1,4,1,spare) &&
    CoinRenderMultipassCore::addAttachmentBytes(target.colorBuffer.size(),1,1,1,bytes) &&
    CoinRenderMultipassCore::addAttachmentBytes(target.depthBuffer.size(),1,4,1,bytes) &&
    CoinRenderMultipassCore::addAttachmentBytes(std::max(candidate,spare),1,1,1,bytes);
}
CoinRenderSubmitResult CoinRenderBackend::preflightMultipassOutput(const CoinRenderFramePlan & frame,
    const CoinRenderTargetP & target, bool asynchronous, const CoinRenderRttPlan * graph) const {
  if (frame.numPasses == 1) return {};
  uint64_t bytes = 0;
  if (!this->multipassOutputBytes(target, asynchronous, bytes) || bytes > frame.multipassBufferBudget)
    return {CoinRenderBackendStatus::OUT_OF_MEMORY, "Multipass output/staging resources exceed the buffer budget"};
  CoinRenderFramePlan admitted = frame;
  admitted.multipassBufferBudget -= bytes;
  return graph ? this->preflightMultipassGraph(*graph, admitted, target.size) :
    this->preflightMultipass(admitted, target.size);
}

CoinRenderSubmitResult CoinRenderBackend::preflightStereo(const CoinRenderFramePlan &,
    const CoinRenderTargetP &, size_t) const {
  return {CoinRenderBackendStatus::UNSUPPORTED, "Completed stereo views require a GPU compositor"};
}
