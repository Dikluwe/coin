#ifndef COIN_RENDER_RETAINED_TRANSPORT_H
#define COIN_RENDER_RETAINED_TRANSPORT_H

#include "rendering/SoRenderBackend.h"
#include <Inventor/rendering/CoinRenderOptions.h>
#include "rendering/coinrender/CoinRenderBackend.h"
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>

class CoinRenderTarget;

// No traversal and no planning. Translate an already resolved native IR frame
// to executor transport; the target retains its normal publication semantics.
class COIN_RENDER_DLL_API CoinRenderRetainedTransport : public SoRenderBackend {
public:
  explicit CoinRenderRetainedTransport(CoinRenderTarget & target);
  ~CoinRenderRetainedTransport() override;
  const char * getName() const override { return "CoinRenderRetainedTransport"; }
  SbBool initialize(const SoRenderBackendInitParams &) override;
  void shutdown() override;
  SbBool render(const SoDrawList &, const SoRenderPlan &, const SoRenderParams &,
                const SoSelectionState * selection = nullptr) override;
  const CoinRenderSubmitResult & getResult() const { return result; }
  CoinRenderRetainedRenderer::Statistics getTransportStatistics() const;
private:
  friend class CoinRenderRetainedRenderer;
  void setTransparencyType(CoinRenderAction::TransparencyType);
  CoinRenderAction::TransparencyType transparencyType = CoinRenderAction::SORTED_OBJECT_BLEND;
  void rebindTarget(CoinRenderTarget & destination);
  SbBool submitOwnedCapture(const SoDrawList &, const SoRenderPlan &, const SoRenderParams &);
  // Only the renderer owns the capture and can authorize reuse. External
  // SoRenderBackend callers may mutate their draw list without a revision.
  enum class CaptureOwnership { External, RendererOwned };
  struct TransportCache;
  std::unique_ptr<TransportCache> transportCache;
  SbBool submitCapture(const SoDrawList &, const SoRenderPlan &, const SoRenderParams &,
                       const SoSelectionState *, CaptureOwnership ownership);
  CoinRenderOptions transportOptions;
  CoinRenderTarget * target;
  CoinRenderSubmitResult result;
};

#endif
