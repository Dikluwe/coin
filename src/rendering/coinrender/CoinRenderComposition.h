#ifndef COIN_RENDER_COMPOSITION_H
#define COIN_RENDER_COMPOSITION_H

#include <Inventor/CoinRenderExport.h>

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderAlphaTestCore.h"
#include "rendering/coinrender/CoinRenderTextureCombineCore.h"
#include "rendering/coinrender/CoinRenderTextureAlphaCore.h"
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include "rendering/coinrender/CoinRenderSelectionCore.h"
#include "rendering/coinrender/CoinRenderPhaseTimer.h"
#include "rendering/coinrender/CoinRenderFloatCore.h"
#include <memory>
#include <unordered_map>

struct CoinRenderCompositionItem {
  size_t drawIndex = 0;
  uint32_t firstIndex = 0, indexCount = 0;
  bool blend = false;
  bool deferred = false;
  bool additive = false;
  bool sortTriangles = false;
  bool sortObject = false;
  float eyeDepth = 0.0f;
  bool screenDoor = false;
  uint32_t screenDoorLevel = 0;
  bool depthTest = true;
  bool depthWrite = true;
  CoinRenderDepthFunction depthFunction = CoinRenderDepthFunction::LESS;
  float depthRange[2] = {0.0f, 1.0f};
  enum TransparencyStrategy { OBJECT, WEIGHTED_OIT, SORTED_LAYERS } transparencyStrategy = OBJECT;
};

// This option revokes both submission-local loans; the literal owned paths
// remain available for ablation and for every composition outside the profile.
COIN_RENDER_DLL_API bool coin_render_composition_borrow_enabled();
COIN_RENDER_DLL_API bool coin_render_scene_layers_valid(const CoinRenderFramePlan &, std::string &);

// Logical payload transfers, not vector capacity or allocator traffic. Clock
// reads and output are disabled unless phase tracing is requested.
struct CoinRenderCompositionTransferTrace {
  explicit CoinRenderCompositionTransferTrace(const char * scope, const char * consumer = "common")
    : scope(scope), consumer(consumer), enabled(std::getenv("COIN_RENDER_TRACE_PHASES") ||
                                               std::getenv("COIN_WGPU_TRACE_PHASES")) {}
  using Clock = std::chrono::steady_clock;
  Clock::time_point begin() const { return enabled ? Clock::now() : Clock::time_point{}; }
  COIN_RENDER_DLL_API double elapsed(Clock::time_point start) const;
  COIN_RENDER_DLL_API ~CoinRenderCompositionTransferTrace();
  const char * scope;
  const char * consumer;
  bool enabled;
  size_t copiedItems = 0, borrowedItems = 0, computedItems = 0;
  double qualifyMs = 0, copyMs = 0;
};

// Only a final ordinary capture can carry its common validation into Target.
// Shadow scene assembly and RTT resource resolution can change the payload
// after Builder publication and must take the complete submission path.
COIN_RENDER_DLL_API bool coin_render_capture_preflight_eligible(const CoinRenderFramePlan & frame);

// A borrowed proof for one C++ capture/submission scope. Builder and Target
// alone can bind it after validation. Wiring must invalidate it before changing
// the captured payload; address/revision/policy checks also reject other plans.
// It carries no target, Coin traversal state, or GPU resource.
class CoinRenderFramePreflight {
  friend class CoinRenderTargetP;
  friend class CoinRenderFramePlanBuilder;
public:
  CoinRenderFramePreflight() = default;
  COIN_RENDER_DLL_API void invalidate();
  COIN_RENDER_DLL_API const std::vector<CoinRenderCompositionItem> * compositionFor(
      const CoinRenderFramePlan & candidate) const;
  COIN_RENDER_DLL_API const std::vector<CoinRenderCompositionItem> * opaqueCompositionFor(
      const CoinRenderFramePlan & candidate) const;
private:
  CoinRenderFramePreflight(const CoinRenderFramePreflight &) = delete;
  CoinRenderFramePreflight & operator=(const CoinRenderFramePreflight &) = delete;
  const CoinRenderFramePlan * frame = nullptr;
  uint64_t revision = 0;
  CoinRenderTransparencyOptions transparency;
  bool opaqueIdentity = false;
  std::vector<CoinRenderCompositionItem> order;
};

namespace coin_render_composition_detail {
// Borrowed captured ranges are immutable during this single composition call.
// Exact range keys retain index/material validation; no revision or source
// identity carries this summary into another invocation.
class RangeMemo {
public:
  COIN_RENDER_DLL_API RangeMemo();
  COIN_RENDER_DLL_API ~RangeMemo();
  COIN_RENDER_DLL_API bool centeredDepth(const CoinRenderFramePlan & frame, const SbMatrix & modelView,
                     uint32_t first, uint32_t count, bool & materialAlpha);
private:
  enum { LIMIT = 1024 };
  struct Summary { bool alpha = false; double maxAbs[3] = {}; };
  using Map = std::unordered_map<uint64_t, Summary>;
  std::unique_ptr<Map> ranges;
  bool enabled;
  size_t admissions = 0, hits = 0, depthSkipped = 0, boundFallbacks = 0;
};
}

// Names describe the Coin operation; GPU algorithms are selected by Infra.
COIN_RENDER_DLL_API bool coin_render_transparency_strategy(
    int32_t type, CoinRenderCompositionItem::TransparencyStrategy& strategy, const char*& name);

// Common Coin policy: classify alpha, resolve immediate/delayed/sorted draws,
// preserve annotation traversal, and apply transparent-pass depth defaults.
// This function consumes captured data only; it never traverses or submits.
COIN_RENDER_DLL_API bool coin_render_composition_order(const CoinRenderFramePlan& frame,
                                          std::vector<CoinRenderCompositionItem>& order,
                                          std::string& diagnostic,
                                          bool deferUnresolvedAlpha = false,
                                          bool * opaqueIdentity = nullptr);

COIN_RENDER_DLL_API bool coin_render_materialize_backfaces(CoinRenderFramePlan &, std::string &);

// Coin's 32x32 Bayer matrix, built from {0,2,3,1}; framebuffer origin is bottom-left.
COIN_RENDER_DLL_API uint32_t coin_render_screen_door_rank(uint32_t x, uint32_t y);

// Expand sorted requests into triangle ranges, including material runs of one
// shape. Executors consume this sequence; no backend sorts Coin triangles.
COIN_RENDER_DLL_API bool coin_render_composition_schedule(const CoinRenderFramePlan& frame,
                                             std::vector<CoinRenderCompositionItem>& schedule,
                                             std::string& diagnostic,
                                             const CoinRenderFramePreflight * preflight = nullptr);

// Immutable schedule for one lowering call. A positive current preflight can
// loan its exact, unexpanded order; every other caller executes the original
// vector algorithm, including its partial output and diagnostics on failure.
// This object never crosses backend/Rust submission or asynchronous tickets.
class CoinRenderCompositionScheduleView {
public:
  explicit CoinRenderCompositionScheduleView(const char * consumer)
    : trace("composition_schedule_copy", consumer), items(&owned) {}
  CoinRenderCompositionScheduleView(const CoinRenderCompositionScheduleView &) = delete;
  CoinRenderCompositionScheduleView & operator=(const CoinRenderCompositionScheduleView &) = delete;
  CoinRenderCompositionScheduleView(CoinRenderCompositionScheduleView &&) = delete;
  CoinRenderCompositionScheduleView & operator=(CoinRenderCompositionScheduleView &&) = delete;
  COIN_RENDER_DLL_API bool prepare(const CoinRenderFramePlan & frame, std::string & diagnostic,
                                  const CoinRenderFramePreflight * preflight = nullptr);
  size_t size() const { return items->size(); }
  const CoinRenderCompositionItem & front() const { return items->front(); }
  const CoinRenderCompositionItem & operator[](size_t index) const { return (*items)[index]; }
  std::vector<CoinRenderCompositionItem>::const_iterator begin() const { return items->begin(); }
  std::vector<CoinRenderCompositionItem>::const_iterator end() const { return items->end(); }
  bool borrowed() const { return items != &owned; }
private:
  CoinRenderCompositionTransferTrace trace;
  std::vector<CoinRenderCompositionItem> owned;
  const std::vector<CoinRenderCompositionItem> * items;
};

#endif // COIN_RENDER_COMPOSITION_H
