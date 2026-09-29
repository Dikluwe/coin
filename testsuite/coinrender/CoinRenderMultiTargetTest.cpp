#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderResourceCore.h"
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#endif
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

static bool check(bool value, const char* message) {
  if (!value)
    std::cerr << "CoinRenderMultiTargetTest: " << message << '\n';
  return value;
}
static bool render(CoinRenderAction& action, SoNode* root) {
  action.apply(root);
  if (action.getLastStatus() != CoinRenderAction::SUCCESS)
    std::cerr << action.getLastError().getString() << '\n';
  return action.getLastStatus() == CoinRenderAction::SUCCESS;
}
static bool red(const std::vector<uint8_t>& pixels, unsigned w, unsigned h) {
  const size_t offset = (size_t(h / 2) * w + w / 2) * 4;
  return pixels.size() == size_t(w) * h * 4 && pixels[offset] > 240 && pixels[offset + 1] < 10 &&
         pixels[offset + 2] < 10;
}
static CoinRenderTarget::ReadbackStatus poll(const CoinRenderReadbackTicket& ticket,
                                             std::vector<uint8_t>& color,
                                             std::vector<float>& depth) {
  for (int i = 0; i < 5000; ++i) {
    auto status = CoinRenderTarget::pollReadback(ticket, color, depth);
    if (status != CoinRenderTarget::READBACK_NOT_READY)
      return status;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return CoinRenderTarget::READBACK_NOT_READY;
}
int main() {
  SoDB::init();
  CoinRenderAction::initClass();
  const uint64_t limit = UINT64_C(128) * 1024 * 1024;
  if (!check(coin_render_readback_admitted(15, limit - 8, 8) &&
                 !coin_render_readback_admitted(16, 0, 4) &&
                 !coin_render_readback_admitted(0, limit - 3, 4) &&
                 !coin_render_readback_admitted(0, 0, UINT64_MAX),
             "queue count, bytes, boundary and overflow admission"))
    return 1;
#if !defined(HAVE_COIN_BGFX) && !defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  return 0;
#else
  SoSeparator* root = new SoSeparator;
  root->ref();
  auto* camera = new SoOrthographicCamera;
  camera->position.setValue(0, 0, 2);
  camera->height = 2;
  root->addChild(camera);
  auto* lighting = new SoLightModel;
  lighting->model = SoLightModel::BASE_COLOR;
  root->addChild(lighting);
  auto* producer = new SoSeparator;
  auto* texture = new SoSceneTexture2;
  texture->scene = producer;
  texture->size.setValue(8, 8);
  texture->backgroundColor.setValue(1, 0, 0, 1);
  texture->transparencyFunction = SoSceneTexture2::NONE;
  root->addChild(texture);
  auto* material = new SoMaterial;
  material->diffuseColor.setValue(1, 1, 1);
  root->addChild(material);
  auto* uv = new SoTextureCoordinate2;
  const SbVec2f coords[] = {SbVec2f(0, 0), SbVec2f(1, 0), SbVec2f(1, 1), SbVec2f(0, 1)};
  uv->point.setValues(0, 4, coords);
  root->addChild(uv);
  auto* points = new SoCoordinate3;
  const SbVec3f positions[] = {SbVec3f(-1, -1, 0), SbVec3f(1, -1, 0), SbVec3f(1, 1, 0),
                               SbVec3f(-1, 1, 0)};
  points->point.setValues(0, 4, positions);
  root->addChild(points);
  auto* faces = new SoIndexedFaceSet;
  const int32_t ids[] = {0, 1, 2, 3, -1};
  faces->coordIndex.setValues(0, 5, ids);
  faces->textureCoordIndex.setValues(0, 5, ids);
  root->addChild(faces);
  std::unique_ptr<CoinRenderTarget> a(CoinRenderTarget::createOffscreen(SbVec2i32(24, 16)));
  std::unique_ptr<CoinRenderTarget> b(CoinRenderTarget::createOffscreen(SbVec2i32(16, 24)));
  a->setDepthReadbackEnabled(FALSE);
  b->setDepthReadbackEnabled(FALSE);
  CoinRenderAction aa(SbViewportRegion(24, 16)), ab(SbViewportRegion(16, 24));
  aa.setRenderTarget(a.get());
  ab.setRenderTarget(b.get());
  if (!render(aa, root) || !render(ab, root))
    return 1;
  std::vector<uint8_t> ca, cb;
  a->readbackRGBA(ca);
  b->readbackRGBA(cb);
  if (!check(red(ca, 24, 16) && red(cb, 16, 24), "RTT output belongs to each target"))
    return 1;
  texture->backgroundColor.setValue(0, 1, 0, 1);
  if (!render(ab, root))
    return 1;
  std::vector<uint8_t> green;
  b->readbackRGBA(green);
  a->readbackRGBA(ca);
  const size_t center = (size_t(12) * 16 + 8) * 4;
  if (!check(green[center] < 10 && green[center + 1] > 240 && red(ca, 24, 16),
             "producer mutation publishes only to the submitted target"))
    return 1;
  texture->backgroundColor.setValue(1, 0, 0, 1);
  if (!render(ab, root))
    return 1;
  b->readbackRGBA(cb);
  const auto peerSerial = b->getLastSubmissionSerial();
  if (!check(a->resize(SbVec2i32(32, 20)), "resize one target"))
    return 1;
  aa.setViewportRegion(SbViewportRegion(32, 20));
  if (!render(aa, root))
    return 1;
  b->readbackRGBA(ca);
  if (!check(ca == cb && b->getLastSubmissionSerial() == peerSerial,
             "resize preserves peer publication"))
    return 1;

  // A ticket retains its dimensions and output independently of its originating target.
  CoinRenderReadbackTicket detached{};
  aa.applyAsync(root, detached);
  if (!check(aa.getLastStatus() == CoinRenderAction::SUCCESS,
             "async RTT before target destruction"))
    return 1;
  aa.setRenderTarget(nullptr);
  a.reset();
  if (!render(ab, root))
    return 1;
  std::vector<float> depth;
  if (!check(poll(detached, ca, depth) == CoinRenderTarget::READBACK_READY && red(ca, 32, 20) &&
                 depth.empty(),
             "pending RTT ticket survives owner destruction and peer rendering"))
    return 1;

  a.reset(CoinRenderTarget::createOffscreen(SbVec2i32(24, 16)));
  a->setDepthReadbackEnabled(FALSE);
  aa.setRenderTarget(a.get());
  aa.setViewportRegion(SbViewportRegion(24, 16));
  std::vector<CoinRenderReadbackTicket> tickets(16);
  for (size_t i = 0; i < tickets.size(); ++i) {
    auto& action = (i & 1) ? aa : ab;
    action.applyAsync(root, tickets[i]);
    if (action.getLastStatus() != CoinRenderAction::SUCCESS)
      std::cerr << "ticket " << i << ": " << action.getLastError().getString() << "\n";
    if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS,
               "sixteen tickets shared across targets"))
      return 1;
  }
  const auto priorSerial = a->getLastSubmissionSerial();
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  CoinWgpuCacheStats before{}, after{};
  coin_wgpu_get_cache_stats(&before);
#endif
#if defined(HAVE_COIN_BGFX)
  setenv("COIN_BGFX_TEST_DEVICE_LOST_ON_SUBMIT_ONCE", "1", 1);
#endif
  CoinRenderReadbackTicket rejected{};
  aa.applyAsync(root, rejected);
#if defined(HAVE_COIN_BGFX)
  const bool producerNotSubmitted =
      std::getenv("COIN_BGFX_TEST_DEVICE_LOST_ON_SUBMIT_ONCE") != nullptr;
  unsetenv("COIN_BGFX_TEST_DEVICE_LOST_ON_SUBMIT_ONCE");
  if (!check(producerNotSubmitted, "queue rejection precedes BGFX RTT submission"))
    return 1;
#endif
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  coin_wgpu_get_cache_stats(&after);
  if (!check(before.submission_serial == after.submission_serial,
             "queue rejection precedes RTT GPU submission"))
    return 1;
#endif
  if (!check(aa.getLastStatus() != CoinRenderAction::SUCCESS && !rejected.token &&
                 priorSerial == a->getLastSubmissionSerial(),
             "seventeenth ticket rejected without publication"))
    return 1;
  for (const auto& ticket : tickets)
    if (!check(CoinRenderTarget::cancelReadback(ticket), "retire tickets to restore capacity"))
      return 1;
  if (!render(aa, root) || !render(ab, root))
    return 1;
  a->readbackRGBA(ca);
  b->readbackRGBA(cb);
  const auto sa = a->getLastSubmissionSerial(), sb = b->getLastSubmissionSerial();
  const auto oldEpoch = b->getPimpl().get().backend->resourceDomain().generation;
  CoinRenderReadbackTicket lost{};
  ab.applyAsync(root, lost);
  if (!check(ab.getLastStatus() == CoinRenderAction::SUCCESS && lost.token,
             "retired queue restores admission for a pending loss ticket"))
    return 1;
  // Publish a synchronous peer image while its independent ticket remains pending.
  if (!render(ab, root))
    return 1;
  b->readbackRGBA(cb);
  const auto publishedPeer = b->getLastSubmissionSerial();
#if defined(HAVE_COIN_BGFX)
  setenv("COIN_BGFX_TEST_DEVICE_LOST_ON_SUBMIT_ONCE", "1", 1);
#else
  coin_wgpu_inject_fault(COIN_WGPU_DEVICE_LOST);
#endif
  aa.apply(root);
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  coin_wgpu_inject_fault(COIN_WGPU_OK);
#endif
  if (!check(aa.getLastStatus() != CoinRenderAction::SUCCESS, "injected device loss reported"))
    return 1;
  std::vector<uint8_t> unchanged;
  a->readbackRGBA(unchanged);
  if (!check(unchanged == ca && a->getLastSubmissionSerial() == sa &&
                 b->getLastSubmissionSerial() == publishedPeer,
             "loss preserves both publications"))
    return 1;
  std::vector<uint8_t> sentinel(1, 73);
  std::vector<float> sentinelDepth(1, -1);
  auto lostStatus = poll(lost, sentinel, sentinelDepth);
  if (!check((lostStatus == CoinRenderTarget::READBACK_DEVICE_LOST ||
              lostStatus == CoinRenderTarget::READBACK_INVALID_TICKET) &&
                 sentinel == std::vector<uint8_t>(1, 73) &&
                 sentinelDepth == std::vector<float>(1, -1),
             "old generation cannot publish readback"))
    return 1;
#if defined(HAVE_COIN_BGFX)
  if (!check(CoinRenderTarget::cancelReadback(lost), "retired loss descriptor can be cancelled"))
    return 1;
#endif
  if (!render(ab, root) || !render(aa, root))
    return 1;
  b->readbackRGBA(unchanged);
  if (!check(unchanged == cb &&
                 b->getPimpl().get().backend->resourceDomain().generation != oldEpoch,
             "same peer reconstructs RTT on a fresh device epoch"))
    return 1;
#if defined(HAVE_COIN_BGFX)
  // The view allocator is a device budget: rejection must leave existing targets usable.
  std::vector<std::unique_ptr<CoinRenderTarget>> extra;
  bool exhausted = false;
  for (unsigned i = 0; i < 256; ++i) {
    std::unique_ptr<CoinRenderTarget> candidate(CoinRenderTarget::createOffscreen(SbVec2i32(2, 2)));
    auto& state = candidate->getPimpl().get();
    const auto result = state.prepareBackend();
    if (result != CoinRenderBackendStatus::SUCCESS) {
      exhausted = result == CoinRenderBackendStatus::UNSUPPORTED &&
                  state.backend->getLastError() == "BGFX target view budget exhausted";
      break;
    }
    extra.push_back(std::move(candidate));
  }
  if (!check(exhausted && !extra.empty(), "finite device view budget rejects excess targets"))
    return 1;
  extra.pop_back();
  std::unique_ptr<CoinRenderTarget> recycled(CoinRenderTarget::createOffscreen(SbVec2i32(2, 2)));
  if (!check(recycled->getPimpl().get().prepareBackend() == CoinRenderBackendStatus::SUCCESS,
             "destroyed target returns its view block"))
    return 1;
  recycled.reset();
  extra.clear();
  if (!render(ab, root))
    return 1;
#endif
  (void)sb;
  aa.setRenderTarget(nullptr);
  ab.setRenderTarget(nullptr);
  root->unref();
  std::cout << "multi-target RTT, queues, resize, destruction and shared recovery passed\n";
  return 0;
#endif
}
