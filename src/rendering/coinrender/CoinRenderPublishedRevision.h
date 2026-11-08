#ifndef COIN_RENDER_PUBLISHED_REVISION_H
#define COIN_RENDER_PUBLISHED_REVISION_H

#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/sensors/SoNodeSensor.h>
#include <Inventor/misc/SoNotification.h>
#include <atomic>
#include <utility>
#include "nodes/SoCameraP.h"

// Notification delivery and explicit configuration publication on the producer
// thread are the only writers of this authority.
// Consumers retain only atomics, never a node, field, path or producer pointer.
class CoinRenderPublishedRevision : public SoCameraStereoObserver {
public:
  std::atomic<uint64_t> scene{1}, camera{1};
  std::atomic<bool> alive{true};
  std::atomic<uint64_t> stereo{0};
  bool isActive() const override { return alive.load(std::memory_order_acquire); }
  void stereoChanged() override {
    stereo.fetch_add(1, std::memory_order_release);
    camera.fetch_add(1, std::memory_order_release);
    scene.fetch_add(1, std::memory_order_release);
  }
};

class CoinRenderRevisionTracker : private SoNodeSensor {
public:
  CoinRenderRevisionTracker() { setDeleteCallback(deleted, this); }
  ~CoinRenderRevisionTracker() override { retire(); detach(); }
  void bind(SoNode * root) {
    if (getAttachedNode() == root) return;
    retire(); detach();
    if (root) {
      state = std::make_shared<CoinRenderPublishedRevision>();
      identity = nextSceneIdentity();
      cameraIdentity = nextSceneIdentity();
      attach(root);
    }
  }
  void observeCamera(SoCamera * camera) {
    if (state) SoCameraP::observeStereo(camera, state);
  }
  uint64_t stereoRevision() const {
    return state ? state->stereo.load(std::memory_order_acquire) : 0;
  }
  void changed(bool camera = false) {
    if (!state) return;
    if (camera) state->camera.fetch_add(1, std::memory_order_release);
    state->scene.fetch_add(1, std::memory_order_release);
  }
  CoinRenderPresentationReceipt capture() const {
    CoinRenderPresentationReceipt receipt;
    if (state && getAttachedNode()) {
      receipt.authority = state; receipt.sceneIdentity = identity;
      receipt.cameraIdentity = cameraIdentity;
      receipt.sceneRevision = state->scene.load(std::memory_order_acquire);
      receipt.cameraRevision = state->camera.load(std::memory_order_acquire);
    }
    return receipt;
  }
  static bool current(const CoinRenderPresentationReceipt & receipt) {
    const auto & state = receipt.authority;
    return state && state->alive.load(std::memory_order_acquire) &&
      receipt.sceneRevision == state->scene.load(std::memory_order_acquire) &&
      receipt.cameraRevision == state->camera.load(std::memory_order_acquire);
  }
private:
  static uint64_t nextSceneIdentity() {
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
  }
  void retire() { if (state) state->alive.store(false, std::memory_order_release); state.reset(); }
  static void deleted(void * data, SoSensor *) {
    static_cast<CoinRenderRevisionTracker *>(data)->retire();
  }
  void notify(SoNotList * list) override {
    if (!state) return;
    const auto * first = list->getFirstRecAtNode();
    const auto * node = first ? first->getBase() : nullptr;
    // Direct camera edits and structural changes advance the camera-set
    // revision. Inherited camera dependencies (e.g. transforms) are also
    // protected by the encompassing scene revision, without inspecting a graph.
    if (!list->getLastField() || (node && node->isOfType(SoCamera::getClassTypeId())))
      state->camera.fetch_add(1, std::memory_order_release);
    state->scene.fetch_add(1, std::memory_order_release);
    // Caller-driven rendering: no delayed sensor scheduling or traversal.
  }
  std::shared_ptr<CoinRenderPublishedRevision> state;
  uint64_t identity = 0, cameraIdentity = 0;
};

// Like the immutable borrow, this capability is lexical and cannot be forged
// by a caller changing DTO revision fields or copying a previously valid DTO.
class CoinRenderRevisionScope {
public:
  ~CoinRenderRevisionScope() { active = previous; }
  static CoinRenderPresentationReceipt forFrame(const void * frame) {
    return active && active->frame == frame ? active->receipt : CoinRenderPresentationReceipt{};
  }
private:
  friend class CoinRenderActionP;
  friend class CoinRenderRetainedTransport;
  CoinRenderRevisionScope(const void * value, CoinRenderPresentationReceipt proof, uint64_t resources)
    : previous(active), frame(value), receipt(std::move(proof)) {
    receipt.resourceRevision = resources; active = this;
  }
  CoinRenderRevisionScope(const CoinRenderRevisionScope &) = delete;
  inline static thread_local const CoinRenderRevisionScope * active = nullptr;
  const CoinRenderRevisionScope * previous;
  const void * frame;
  CoinRenderPresentationReceipt receipt;
};
#endif
