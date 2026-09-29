#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderReadbackCore.h"
#include <Inventor/SoDB.h>
#include <iostream>
#include <memory>

namespace {
bool check(bool value, const char* message) {
  if (!value)
    std::cerr << "CoinRenderPublicationTest: " << message << '\n';
  return value;
}
struct Witness {
  CoinRenderBackendStatus result = CoinRenderBackendStatus::SUCCESS;
  bool incomplete = false;
  unsigned submits = 0;
};
class MutatingBackend : public CoinRenderBackend {
public:
  explicit MutatingBackend(std::shared_ptr<Witness> value) : witness(value) {}
  bool isGpuBackend() const override { return false; }
  CoinRenderBackendStatus getStatus() const override { return CoinRenderBackendStatus::SUCCESS; }
  CoinRenderBackendStatus prepare(CoinRenderTargetP&) override {
    return CoinRenderBackendStatus::SUCCESS;
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&, CoinRenderTargetP& target) override {
    ++witness->submits;
    // A backend may have already copied color when a later depth/map/runtime check fails.
    target.colorBuffer.assign(witness->incomplete ? 1 : 64, uint8_t(witness->submits));
    target.depthBuffer.assign(16, float(witness->submits) / 10.0f);
    return {witness->result, "fixture", uint64_t(witness->submits)};
  }
  void poll() override {}
  const std::string& getLastError() const override { return diagnostic; }

private:
  std::shared_ptr<Witness> witness;
  std::string diagnostic;
};
} // namespace

int main() {
  SoDB::init();
  bool ok = true;
  CoinRenderReadbackTicket ticket{};
  ticket.token = 7;
  ticket.submissionSerial = 9;
  ticket.width = 3;
  ticket.height = 5;
  ticket.colorBytes = 60;
  for (uint32_t pitch : {12u, 256u}) {
    ticket.colorRowPitch = pitch;
    ok &= check(coin_render_complete_readback_ticket(ticket, SbVec2i32(3, 5), false, 9),
                "packed and padded staging rows have the same packed output size");
    ticket.depthFormat = 1;
    ticket.depthBytes = 60;
    ticket.depthRowPitch = pitch;
    ok &= check(coin_render_complete_readback_ticket(ticket, SbVec2i32(3, 5), true, 9),
                "depth staging also permits backend row alignment");
    ticket.depthFormat = ticket.depthRowPitch = 0;
    ticket.depthBytes = 0;
  }
  CoinRenderReadbackTicket emptyTicket{};
  emptyTicket.token = 7;
  emptyTicket.submissionSerial = 9;
  ok &= check(!coin_render_complete_readback_ticket(emptyTicket, SbVec2i32(0, 0), false, 9),
              "empty descriptors are not completed readbacks");
  auto invalidTicket = ticket;
  invalidTicket.colorRowPitch = 8;
  ok &= check(!coin_render_complete_readback_ticket(invalidTicket, SbVec2i32(3, 5), false, 9),
              "staging row must fit all pixels");
  invalidTicket = ticket;
  invalidTicket.colorBytes = uint64_t(ticket.colorRowPitch) * ticket.height;
  ok &= check(!coin_render_complete_readback_ticket(invalidTicket, SbVec2i32(3, 5), false, 9),
              "staging padding must not inflate published vector sizes");
  ok &= check(!coin_render_complete_readback_ticket(ticket, SbVec2i32(3, 5), false, 10) &&
                  !coin_render_complete_readback_ticket(ticket, SbVec2i32(4, 5), false, 9) &&
                  !coin_render_complete_readback_ticket(ticket, SbVec2i32(3, 5), true, 9),
              "serial, dimensions and requested depth must agree");

  auto witness = std::make_shared<Witness>();
  CoinRenderTargetP target(SbVec2i32(4, 4));
  target.options = CoinRenderOptions();
  target.optionsDiagnostic.clear();
  target.backend.reset(new MutatingBackend(witness));
  CoinRenderFramePlan frame;
  frame.revision = 100;
  ok &= check(target.executeFrame(frame).status == CoinRenderBackendStatus::SUCCESS,
              "publish baseline");
  const auto color = target.colorBuffer;
  const auto depth = target.depthBuffer;
  const uint8_t* pointer = target.colorBuffer.data();
  const float* depthPointer = target.depthBuffer.data();
  const auto serial = target.lastSubmissionSerial;
  const auto revision = target.lastValidatedPlanRevision;
  const CoinRenderBackendStatus failures[] = {
      CoinRenderBackendStatus::UNSUPPORTED,   CoinRenderBackendStatus::OUT_OF_MEMORY,
      CoinRenderBackendStatus::BACKEND_ERROR, CoinRenderBackendStatus::NOT_READY,
      CoinRenderBackendStatus::SURFACE_LOST,  CoinRenderBackendStatus::DEVICE_LOST};
  for (auto failure : failures) {
    if (!target.backend)
      target.backend.reset(new MutatingBackend(witness));
    witness->result = failure;
    ++frame.revision;
    auto result = target.executeFrame(frame);
    ok &= check(
        result.status == failure && target.colorBuffer == color && target.depthBuffer == depth &&
            target.colorBuffer.data() == pointer && target.depthBuffer.data() == depthPointer &&
            target.lastSubmissionSerial == serial && target.lastValidatedPlanRevision == revision &&
            target.synchronousReadbackValid && target.borrowedReadbackValid,
        "late failure must preserve the complete publication and allocations");
  }
  target.backend.reset(new MutatingBackend(witness));
  witness->result = CoinRenderBackendStatus::SUCCESS;
  witness->incomplete = true;
  ok &= check(target.executeFrame(frame).status == CoinRenderBackendStatus::BACKEND_ERROR &&
                  target.colorBuffer == color && target.colorBuffer.data() == pointer &&
                  target.lastSubmissionSerial == serial,
              "a successful but incomplete backend result must not publish");
  witness->incomplete = false;
  ++frame.revision;
  ok &= check(target.executeFrame(frame).status == CoinRenderBackendStatus::SUCCESS &&
                  target.colorBuffer != color && target.depthBuffer != depth &&
                  target.lastValidatedPlanRevision == frame.revision &&
                  target.lastSubmissionSerial == witness->submits,
              "successful recovery commits all result fields together");
  const auto recovered = target.colorBuffer;
  const auto recoveredPointer = target.colorBuffer.data();
  ++frame.revision;
  frame.textures.push_back(CoinRenderTextureImageSnapshot());
  const auto submits = witness->submits;
  ok &=
      check(target.executeFrame(frame).status != CoinRenderBackendStatus::SUCCESS &&
                target.colorBuffer == recovered && target.colorBuffer.data() == recoveredPointer &&
                target.borrowedReadbackValid && witness->submits == submits,
            "preflight failure preserves publication without entering backend");
  if (!ok)
    return 1;
  std::cout << "CoinRenderPublicationTest passed\n";
  return 0;
}
