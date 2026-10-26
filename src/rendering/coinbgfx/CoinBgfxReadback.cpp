// CoinRender BGFX Readback responsibilities.
#include "rendering/coinbgfx/CoinBgfxBackendInternal.h"

using namespace coin_bgfx_internal;

CoinRenderDeviceDomain CoinBgfxBackend::resourceDomain() const {
  if (!this->initialized || !this->onApiThread() ||
      static_cast<CoinBgfxCallback*>(this->callback.get())->failed())
    return {};
  CoinRenderDeviceDomain result;
  result.device = 1; // One BGFX runtime per process; generation distinguishes recreation.
  result.generation = sharedRuntime().generation;
  return result;
}

CoinRenderTarget::ReadbackStatus
CoinBgfxBackend::pollReadback(const CoinRenderReadbackTicket & ticket,
  std::vector<uint8_t> & color, std::vector<float> & depth, SbString * diagnostic)
{
  auto & runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
  auto lost = lostReadbacks.find(ticket.token);
  if (lost != lostReadbacks.end() && sameTicket(lost->second.ticket, ticket)) {
    if (lost->second.thread != std::this_thread::get_id())
      return CoinRenderTarget::READBACK_ERROR;
    if (diagnostic)
      *diagnostic = "Readback belongs to a retired device generation";
    return CoinRenderTarget::READBACK_DEVICE_LOST;
  }
  auto found = asyncEntries().find(ticket.token);
  if (found == asyncEntries().end() || !sameTicket(found->second->ticket, ticket)) {
    if (diagnostic) *diagnostic = "Invalid or consumed BGFX readback ticket";
    return CoinRenderTarget::READBACK_INVALID_TICKET;
  }
  if (runtime.apiThread != std::this_thread::get_id()) {
    if (diagnostic) *diagnostic = "BGFX readback must be polled on its API thread";
    return CoinRenderTarget::READBACK_ERROR;
  }
  auto * cb = static_cast<CoinBgfxCallback *>(runtime.callback.get());
  if (cb->failed()) {
    if (diagnostic) *diagnostic = cb->diagnostic().c_str();
    return cb->deviceLost() ? CoinRenderTarget::READBACK_DEVICE_LOST : CoinRenderTarget::READBACK_ERROR;
  }
  const uint32_t completed = bgfx::frame();
  // The render worker may report a fatal while frame() waits for it. Do not
  // publish (or normalize) staging after that failure, even if its frame ID
  // would otherwise look complete.
  if (cb->failed()) {
    if (diagnostic) *diagnostic = cb->diagnostic().c_str();
    return cb->deviceLost() ? CoinRenderTarget::READBACK_DEVICE_LOST : CoinRenderTarget::READBACK_ERROR;
  }
  auto & entry = *found->second;
  if (static_cast<int32_t>(completed - entry.slot.readyFrame) < 0)
    return CoinRenderTarget::READBACK_NOT_READY;
  if (entry.bottomLeft) {
    CoinRenderImageCore::flipRgba8Rows(entry.slot.pixels, SbVec2i32(ticket.width, ticket.height));
    for (uint32_t y = 0; !entry.slot.depth.empty() && y < ticket.height / 2; ++y)
      std::swap_ranges(entry.slot.depth.begin() + y * ticket.width,
        entry.slot.depth.begin() + (y + 1) * ticket.width,
        entry.slot.depth.begin() + (ticket.height - y - 1) * ticket.width);
  }
  color.swap(entry.slot.pixels); depth.swap(entry.slot.depth);
  releaseAsync(ticket.token);
  return CoinRenderTarget::READBACK_READY;
}

bool
CoinBgfxBackend::cancelReadback(const CoinRenderReadbackTicket & ticket)
{
  auto & runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
  auto lost = lostReadbacks.find(ticket.token);
  if (lost != lostReadbacks.end() && sameTicket(lost->second.ticket, ticket)) {
    if (lost->second.thread != std::this_thread::get_id())
      return false;
    lostReadbacks.erase(lost);
    return true;
  }
  auto found = asyncEntries().find(ticket.token);
  if (found == asyncEntries().end() || !sameTicket(found->second->ticket, ticket) ||
      runtime.apiThread != std::this_thread::get_id()) return false;
  auto * cb = static_cast<CoinBgfxCallback *>(runtime.callback.get());
  if (cb->failed()) {
    // Shutdown is the only safe retirement fence after device loss.
    releaseAsync(ticket.token, true);
    return true;
  }
  uint32_t completed = bgfx::frame();
  for (int attempts = 0; !cb->failed() &&
       static_cast<int32_t>(completed - found->second->slot.readyFrame) < 0 && attempts < 16; ++attempts)
    completed = bgfx::frame();
  if (cb->failed()) {
    // Keep CPU destinations alive until the existing shutdown retirement
    // fence; another frame cannot establish completion after a fatal.
    releaseAsync(ticket.token, true);
    return true;
  }
  if (static_cast<int32_t>(completed - found->second->slot.readyFrame) < 0) return false;
  releaseAsync(ticket.token);
  return true;
}

bool CoinBgfxBackend::readbackLoad(uint64_t& jobs, uint64_t& bytes) const {
  auto& runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
  jobs = asyncEntries().size();
  bytes = 0;
  for (const auto& item : asyncEntries())
    bytes += item.second->ticket.colorBytes + item.second->ticket.depthBytes;
  return true;
}

void CoinBgfxBackend::retireLostReadbacks() {
  auto& runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
  while (!asyncEntries().empty()) {
    const auto entry = asyncEntries().begin()->second;
    lostReadbacks.emplace(entry->ticket.token, LostReadback{entry->ticket, runtime.apiThread});
    releaseAsync(entry->ticket.token, true);
  }
}

void
CoinBgfxBackend::poll()
{
  // Synchronous readback in submit() already advances BGFX frames.
}
