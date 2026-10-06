#ifndef COIN_RENDER_COMPOSITION_H
#define COIN_RENDER_COMPOSITION_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderAlphaTestCore.h"
#include "rendering/coinrender/CoinRenderTextureCombineCore.h"
#include "rendering/coinrender/CoinRenderTextureAlphaCore.h"
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include "rendering/coinrender/CoinRenderSelectionCore.h"
#include "rendering/coinrender/CoinRenderPhaseTimer.h"
#include "rendering/coinrender/CoinRenderFloatCore.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
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
inline bool coin_render_composition_borrow_enabled() {
  const char * option = std::getenv("COIN_RENDER_DISABLE_COMPOSITION_BORROW");
  return !(option && std::strcmp(option, "1") == 0);
}

// Logical payload transfers, not vector capacity or allocator traffic. Clock
// reads and output are disabled unless phase tracing is requested.
struct CoinRenderCompositionTransferTrace {
  explicit CoinRenderCompositionTransferTrace(const char * scope, const char * consumer = "common")
    : scope(scope), consumer(consumer), enabled(std::getenv("COIN_RENDER_TRACE_PHASES") ||
                                               std::getenv("COIN_WGPU_TRACE_PHASES")) {}
  using Clock = std::chrono::steady_clock;
  Clock::time_point begin() const { return enabled ? Clock::now() : Clock::time_point{}; }
  double elapsed(Clock::time_point start) const {
    return enabled ? std::chrono::duration<double, std::milli>(Clock::now() - start).count() : 0;
  }
  ~CoinRenderCompositionTransferTrace() {
    if (enabled) std::fprintf(stderr,
      "COIN_RENDER_PHASE %s consumer=%s copied_items=%zu copied_bytes=%zu borrowed_items=%zu borrowed_bytes=%zu computed_items=%zu qualify_ms=%.6f copy_ms=%.6f\n",
      scope, consumer, copiedItems, copiedItems * sizeof(CoinRenderCompositionItem),
      borrowedItems, borrowedItems * sizeof(CoinRenderCompositionItem), computedItems, qualifyMs, copyMs);
  }
  const char * scope;
  const char * consumer;
  bool enabled;
  size_t copiedItems = 0, borrowedItems = 0, computedItems = 0;
  double qualifyMs = 0, copyMs = 0;
};

// Only a final ordinary capture can carry its common validation into Target.
// Shadow scene assembly and RTT resource resolution can change the payload
// after Builder publication and must take the complete submission path.
inline bool coin_render_capture_preflight_eligible(const CoinRenderFramePlan & frame) {
  if (!frame.shadowGroups.empty() || !frame.shadowLights.empty()) return false;
  for (const auto & texture : frame.textures)
    if (texture.producerId || texture.gpuToken) return false;
  return true;
}

// A borrowed proof for one C++ capture/submission scope. Builder and Target
// alone can bind it after validation. Wiring must invalidate it before changing
// the captured payload; address/revision/policy checks also reject other plans.
// It carries no target, Coin traversal state, or GPU resource.
class CoinRenderFramePreflight {
  friend class CoinRenderTargetP;
  friend class CoinRenderFramePlanBuilder;
public:
  CoinRenderFramePreflight() = default;
  void invalidate() {
    frame = nullptr;
    revision = 0;
    opaqueIdentity = false;
    order.clear();
  }
  const std::vector<CoinRenderCompositionItem> * compositionFor(
      const CoinRenderFramePlan & candidate) const {
    return frame == &candidate && revision == candidate.revision &&
      coin_render_same_transparency_options(transparency, candidate.transparency) ? &order : nullptr;
  }
  const std::vector<CoinRenderCompositionItem> * opaqueCompositionFor(
      const CoinRenderFramePlan & candidate) const {
    return opaqueIdentity ? compositionFor(candidate) : nullptr;
  }
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
  RangeMemo() {
    const char * option = std::getenv("COIN_RENDER_DISABLE_COMPOSITION_RANGE_MEMOIZATION");
    enabled = !(option && std::strcmp(option, "1") == 0);
  }
  ~RangeMemo() {
    if (std::getenv("COIN_RENDER_TRACE_PHASES") || std::getenv("COIN_WGPU_TRACE_PHASES"))
      std::fprintf(stderr, "COIN_RENDER_PHASE composition_range_memo enabled=%d entries=%zu admissions=%zu hits=%zu depth_skipped=%zu bound_fallbacks=%zu\n",
        enabled ? 1 : 0, ranges ? ranges->size() : 0, admissions, hits, depthSkipped, boundFallbacks);
  }
  bool centeredDepth(const CoinRenderFramePlan & frame, const SbMatrix & modelView,
                     uint32_t first, uint32_t count, bool & materialAlpha) {
    if (!enabled) return false;
    const auto & matrix = modelView.getValue();
    // The old homogeneous divisor is exactly one in this profile. General
    // projective/stroke depths, including their rejection behavior, stay full.
    for (int row = 0; row < 4; ++row)
      if (matrix[row][3] != (row == 3 ? 1.0f : 0.0f) || !coin_render_is_finite(matrix[row][2])) return false;
    const uint64_t key = (uint64_t(first) << 32) | count;
    const Summary * cached = nullptr;
    if (ranges) {
      const auto found = ranges->find(key);
      if (found != ranges->end()) cached = &found->second;
    }
    Summary summary;
    if (cached) { summary = *cached; ++hits; }
    else {
      // A full cache still serves earlier admitted aliases, but never scans or
      // allocates optional summaries for further misses.
      if (ranges && ranges->size() == LIMIT) return false;
      for (size_t i = first; i < size_t(first) + count; ++i) {
        const uint32_t index = frame.indices[i];
        if (index >= frame.vertices.size()) return false;
        const auto & vertex = frame.vertices[index];
        if (vertex.materialSlot >= frame.materials.size()) return false;
        summary.alpha = summary.alpha || frame.materials[vertex.materialSlot].diffuse[3] < 1.0f;
        for (int axis = 0; axis < 3; ++axis) {
          // This is qualification, not a new rejection: identity-view legacy
          // depth may legitimately ignore a non-finite X/Y. Use its full loop.
          if (!coin_render_is_finite(vertex.position[axis])) return false;
          summary.maxAbs[axis] = std::max(summary.maxAbs[axis], std::abs(double(vertex.position[axis])));
        }
      }
      try {
        if (!ranges) ranges.reset(new Map);
        ranges->emplace(key, summary); ++admissions;
      } catch (const std::bad_alloc &) { ranges.reset(); enabled = false; return false; }
    }
    double bound = std::abs(double(matrix[3][2]));
    for (int axis = 0; axis < 3; ++axis) bound += summary.maxAbs[axis] * std::abs(double(matrix[axis][2]));
    // Every float product and partial sum, and min/max midpoint, remains
    // finite with margin. The sorting center replaces that midpoint below.
    if (!(bound < double(std::numeric_limits<float>::max()) / 8)) { ++boundFallbacks; return false; }
    materialAlpha = materialAlpha || summary.alpha;
    depthSkipped += count;
    return true;
  }
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
inline bool coin_render_transparency_strategy(
    int32_t type, CoinRenderCompositionItem::TransparencyStrategy& strategy, const char*& name) {
  strategy = CoinRenderCompositionItem::OBJECT;
  switch (type) {
#define COIN_RENDER_MODE(mode)                                                                     \
  case SoGLRenderAction::mode:                                                                     \
    name = #mode " -> object";                                                                     \
    return true
    COIN_RENDER_MODE(NONE);
    COIN_RENDER_MODE(SCREEN_DOOR);
    COIN_RENDER_MODE(ADD);
    COIN_RENDER_MODE(BLEND);
    COIN_RENDER_MODE(DELAYED_ADD);
    COIN_RENDER_MODE(DELAYED_BLEND);
    COIN_RENDER_MODE(SORTED_OBJECT_ADD);
    COIN_RENDER_MODE(SORTED_OBJECT_BLEND);
    COIN_RENDER_MODE(SORTED_OBJECT_SORTED_TRIANGLE_ADD);
    COIN_RENDER_MODE(SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
#undef COIN_RENDER_MODE
  case SoGLRenderAction::SORTED_LAYERS_BLEND:
    strategy = CoinRenderCompositionItem::SORTED_LAYERS;
    name = "SORTED_LAYERS_BLEND -> sorted_layers";
    return true;
  default:
    name = "unsupported";
    return false;
  }
}

// Common Coin policy: classify alpha, resolve immediate/delayed/sorted draws,
// preserve annotation traversal, and apply transparent-pass depth defaults.
// This function consumes captured data only; it never traverses or submits.
inline bool coin_render_composition_order(const CoinRenderFramePlan& frame,
                                          std::vector<CoinRenderCompositionItem>& order,
                                          std::string& diagnostic,
                                          bool deferUnresolvedAlpha = false,
                                          bool * opaqueIdentity = nullptr) {
  CoinRenderPhaseTimer timer("composition_detail");
  if (opaqueIdentity) *opaqueIdentity = false;
  // Classification already visits each packet/item. Accumulate the loan proof
  // there, rather than validating the completed schedule in a second pass.
  bool identity = opaqueIdentity && coin_render_composition_borrow_enabled() &&
    !deferUnresolvedAlpha && frame.draws.size() >= 256 && frame.textures.empty() &&
    frame.samplers.empty() && frame.shadowGroups.empty() && frame.shadowLights.empty();
  CoinRenderCompositionTransferTrace qualification("composition_identity");
  const auto qualificationBegin = qualification.begin();
  coin_render_composition_detail::RangeMemo rangeMemo;
  order.clear();
  uint64_t unusedBudget = 0;
  if (!coin_render_transparency_budget(1, 1, frame.transparency, false, unusedBudget, diagnostic))
    return false;
  order.reserve(frame.draws.size());
  for (const auto& material : frame.materials) {
    const float alpha = material.diffuse[3], transparency = material.transparency;
    if (!coin_render_is_finite(alpha) || !coin_render_is_finite(transparency) || alpha < 0 || alpha > 1 ||
        transparency < 0 || transparency > 1 || std::abs(alpha + transparency - 1.0f) > 1.0e-5f) {
      diagnostic = "Invalid or inconsistent material alpha/transparency";
      return false;
    }
  }
  std::vector<int8_t> textureHasAlpha(frame.textures.size(), -1);
  for (size_t i = 0; i < frame.draws.size(); ++i) {
    const CoinRenderDrawPacket& draw = frame.draws[i];
    if (draw.shadowLightSlot) continue;
    if (draw.renderStateSlot >= frame.renderStates.size()) {
      diagnostic = "Invalid render state in composition order";
      return false;
    }
    const CoinRenderRenderStateSnapshot& rs = frame.renderStates[draw.renderStateSlot];
    if (rs.materialSlot >= frame.materials.size()) {
      diagnostic = "Invalid material in composition order";
      return false;
    }
    const float alpha = frame.materials[rs.materialSlot].diffuse[3];
    // Coin classifies the whole material before shading, including unused
    // transparency slots and packed primary alpha that rounds to opaque.
    // Preserve that traversal flag independently of the alpha-test function.
    const bool nativeMaterialAlpha = rs.transparentMaterial;
    bool materialAlpha = alpha < 1.0f || nativeMaterialAlpha;
    const size_t first = draw.geometry.firstIndex;
    const size_t count = draw.geometry.indexCount;
    if (first > frame.indices.size() || count > frame.indices.size() - first) {
      diagnostic = "Invalid index range in composition order";
      return false;
    }
    SbMatrix modelView = rs.model * rs.view;
    const bool identityView = modelView == SbMatrix::identity();
    const auto & matrix = modelView.getValue();
    float minDepth = 0, maxDepth = 0;
    const bool centeredDepth = draw.hasSortingCenter && rs.polygonOffsetPrimitiveStyle == 1 &&
      rangeMemo.centeredDepth(frame, modelView, draw.geometry.firstIndex, draw.geometry.indexCount, materialAlpha);
    if (!centeredDepth) for (size_t j = first; j < first + count; ++j) {
      const uint32_t vertexIndex = frame.indices[j];
      if (vertexIndex >= frame.vertices.size()) {
        diagnostic = "Invalid vertex in composition order";
        return false;
      }
      const CoinRenderVertexSnapshot& vertex = frame.vertices[vertexIndex];
      if (vertex.materialSlot >= frame.materials.size()) {
        diagnostic = "Invalid vertex material in composition order";
        return false;
      }
      materialAlpha = materialAlpha || frame.materials[vertex.materialSlot].diffuse[3] < 1.0f;
      // Composition needs only Z. Keep the same homogeneous divide and
      // finite-depth checks without computing X/Y or calling into Core DLL.
      const float * position = vertex.position;
      const float viewZ = identityView ? position[2] :
        (position[0] * matrix[0][2] + position[1] * matrix[1][2] + position[2] * matrix[2][2] + matrix[3][2]) /
        (position[0] * matrix[0][3] + position[1] * matrix[1][3] + position[2] * matrix[2][3] + matrix[3][3]);
      if (!coin_render_is_finite(viewZ)) {
        diagnostic = "Invalid non-finite eye depth in composition order";
        return false;
      }
      float eyeDepth = -viewZ;
      if (rs.polygonOffsetPrimitiveStyle != 1) {
        // Expanded strokes are in NDC. Recover their original eye-space depth
        // from the source camera rather than sorting on normalized depth.
        if (rs.cameraSlot >= frame.cameras.size()) {
          diagnostic = "Invalid stroke camera in composition order";
          return false;
        }
        const auto& projection = frame.cameras[rs.cameraSlot].projectionMatrixCoin;
        SbVec3f eye;
        projection.inverse().multVecMatrix(SbVec3f(vertex.position), eye);
        eyeDepth = -eye[2];
      }
      if (j == first)
        minDepth = maxDepth = eyeDepth;
      else {
        minDepth = std::min(minDepth, eyeDepth);
        maxDepth = std::max(maxDepth, eyeDepth);
      }
    }
    const bool primaryAlpha = materialAlpha;
    bool unresolvedAlpha = false;
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      if (!coin_render_texture_unit_enabled(rs, unit))
        continue;
      const CoinRenderTextureUnitSnapshot tex = coin_render_texture_unit(rs, unit);
      if (tex.imageSlot >= frame.textures.size()) {
        diagnostic = "Invalid texture in composition order";
        return false;
      }
      unresolvedAlpha =
          unresolvedAlpha || (frame.textures[tex.imageSlot].producerId != 0 &&
                              !frame.textures[tex.imageSlot].gpuOpaque &&
                              frame.textures[tex.imageSlot].sceneTransparencyFunction == -1);
      int8_t& cached = textureHasAlpha[tex.imageSlot];
      if (cached < 0)
        cached = coin_render_texture_has_transparency(frame.textures[tex.imageSlot]) ? 1 : 0;
      if (rs.textureCombines[unit].instructions[0][0] > .5f)
        materialAlpha = coin_render_combine_may_have_alpha(rs.textureCombines[unit], primaryAlpha,
                                                           cached != 0, materialAlpha);
      else if (tex.model == CoinRenderTextureModel::REPLACE)
        materialAlpha = cached != 0;
      else if (tex.model != CoinRenderTextureModel::DECAL)
        materialAlpha = materialAlpha || cached != 0;
    }

    // Coin's traversal flag precedes shading, including an opaque REPLACE or
    // combine result that replaces packed primary alpha with one.
    materialAlpha = materialAlpha || nativeMaterialAlpha;

    CoinRenderCompositionItem item;
    item.drawIndex = i;
    item.firstIndex = draw.geometry.firstIndex;
    item.indexCount = draw.geometry.indexCount;
    // Native Bitmap/DrawPixels traversal classifies before raster shading.
    // The same flag governs blending and deferral; gray Text2's explicit
    // rasterForceBlend remains a separate non-additive override below.
    item.blend = rs.rasterPixels ? rs.rasterTransparent : materialAlpha;
    const bool traversalTransparent = rs.rasterPixels ? rs.rasterTransparent : item.blend;
    item.eyeDepth = count ? (minDepth + maxDepth) * 0.5f : 0.0f;
    if (draw.hasSortingCenter) {
      if (rs.cameraSlot >= frame.cameras.size()) {
        diagnostic = "Invalid sorting camera in composition order";
        return false;
      }
      SbVec3f center;
      frame.cameras[rs.cameraSlot].viewMatrix.multVecMatrix(SbVec3f(draw.sortingCenterWorld),
                                                            center);
      item.eyeDepth = -center[2];
    }
    if (!coin_render_is_finite(item.eyeDepth)) {
      diagnostic = "Invalid non-finite average eye depth in composition order";
      return false;
    }
    {
      switch (rs.transparencyType) {
      case SoGLRenderAction::NONE:
      case SoGLRenderAction::SCREEN_DOOR:
        item.blend = false;
        item.screenDoor = rs.transparencyType == SoGLRenderAction::SCREEN_DOOR;
        if (item.screenDoor && rs.polygonOffsetPrimitiveStyle == 1) {
          const float transparency = rs.screenDoorTransparency >= 0
                                         ? rs.screenDoorTransparency
                                         : frame.materials[rs.materialSlot].transparency;
          if (!std::isfinite(transparency)) {
            diagnostic = "Invalid non-finite screen-door transparency";
            return false;
          }
          item.screenDoorLevel =
              static_cast<uint32_t>(std::min(64, std::max(0, int(transparency * 64.0f))));
        }
        break;
      case SoGLRenderAction::ADD:
        item.additive = true;
        break;
      case SoGLRenderAction::BLEND:
        break;
      case SoGLRenderAction::DELAYED_ADD:
        item.additive = true;
        item.deferred = traversalTransparent;
        break;
      case SoGLRenderAction::DELAYED_BLEND:
        item.deferred = traversalTransparent;
        break;
      case SoGLRenderAction::SORTED_OBJECT_ADD:
        item.additive = true;
        item.sortObject = true;
        item.deferred = traversalTransparent;
        break;
      case SoGLRenderAction::SORTED_OBJECT_BLEND:
        item.sortObject = true;
        item.deferred = traversalTransparent;
        break;
      case SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD:
        item.additive = true;
        item.sortTriangles = true;
        item.sortObject = true;
        item.deferred = traversalTransparent;
        break;
      case SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND:
        item.sortTriangles = true;
        item.sortObject = true;
        item.deferred = traversalTransparent;
        break;
      case SoGLRenderAction::SORTED_LAYERS_BLEND:
        item.deferred = traversalTransparent;
        item.transparencyStrategy = CoinRenderCompositionItem::SORTED_LAYERS;
        break;
      default:
        diagnostic = "Unknown Coin transparency mode";
        return false;
      }
    }
    if (rs.rasterForceBlend) { item.blend = true; item.additive = false; }
    if (item.blend && item.deferred && !item.additive && draw.renderLayer == 0) {
      const uint64_t required =
          item.transparencyStrategy == CoinRenderCompositionItem::SORTED_LAYERS
              ? COIN_RENDER_MECHANISM_PEELING
              : COIN_RENDER_MECHANISM_OBJECT;
      const auto selection = coin_render_requested_mechanism(deferUnresolvedAlpha && unresolvedAlpha
                                                                 ? COIN_RENDER_TRANSPARENCY_COIN
                                                                 : frame.transparency.mode,
                                                             required, true);
      if (selection.reason != COIN_RENDER_SELECTION_SUPPORTED) {
        diagnostic = coin_render_selection_diagnostic(selection.reason);
        return false;
      }
      item.transparencyStrategy = selection.mechanism == COIN_RENDER_MECHANISM_PEELING
                                      ? CoinRenderCompositionItem::SORTED_LAYERS
                                  : selection.mechanism == COIN_RENDER_MECHANISM_WEIGHTED_OIT
                                      ? CoinRenderCompositionItem::WEIGHTED_OIT
                                      : CoinRenderCompositionItem::OBJECT;
    }
    // Annotation paths execute immediately, including translucent depth writers.
    item.deferred = item.deferred && draw.renderLayer == 0;
    item.sortTriangles = item.sortTriangles && rs.polygonOffsetPrimitiveStyle == 1;
    item.depthTest = rs.depthTest;
    item.depthWrite = rs.depthWrite;
    item.depthFunction = rs.depthFunction;
    item.depthRange[0] = rs.depthRange[0];
    item.depthRange[1] = rs.depthRange[1];
    // SoGLRenderAction::renderSingle changes these defaults for the transparent
    // traversal. Explicit SoDepthBuffer fields are reapplied by that traversal.
    if (item.deferred) {
      if (!(rs.explicitDepthMask & 1))
        item.depthTest = true;
      if (!(rs.explicitDepthMask & 2))
        item.depthWrite = false;
      if (!(rs.explicitDepthMask & 4))
        item.depthFunction = CoinRenderDepthFunction::LEQUAL;
      if (!(rs.explicitDepthMask & 8)) {
        item.depthRange[0] = 0;
        item.depthRange[1] = 1;
      }
    }
    if (identity && (draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST ||
        draw.stableNodeId || draw.renderLayer || draw.clearDepthBefore || draw.lineStripId ||
        !draw.geometry.vertexCount || !draw.geometry.indexCount || draw.geometry.indexCount % 3 ||
        rs.polygonOffsetPrimitiveStyle != 1 || item.blend || item.deferred || item.additive ||
        item.sortTriangles || item.screenDoor || item.screenDoorLevel)) identity = false;
    // sortObject is inert for immediate opaque items, and remains in the loan.
    order.push_back(item);
  }
  timer.mark("classify");
  const auto precedes = [&frame](const CoinRenderCompositionItem& a, const CoinRenderCompositionItem& b) {
        const uint32_t layerA = frame.draws[a.drawIndex].renderLayer;
        const uint32_t layerB = frame.draws[b.drawIndex].renderLayer;
        if (layerA != layerB)
          return layerA < layerB;
        // Overlay layers follow immediate traversal order: their opaque depth
        // writers and translucent labels can depend on the exact submission order.
        if (layerA != 0)
          return false;
        if (a.deferred != b.deferred)
          return !a.deferred;
        // Coin renders its sorted path list before its unsorted delayed list.
        if (!a.deferred)
          return false;
        if (a.sortObject != b.sortObject)
          return a.sortObject;
        return a.sortObject && a.eyeDepth > b.eyeDepth;
      };
  // Preserve traversal order without sorting/allocating for already ordered
  // captures, including the common all-opaque base layer.
  if (!std::is_sorted(order.begin(), order.end(), precedes))
    std::stable_sort(order.begin(), order.end(), precedes);
  timer.mark("sort");
  if (opaqueIdentity) *opaqueIdentity = identity && order.size() == frame.draws.size();
  // Includes the existing classification/sort pass; this is not an estimate
  // of incremental predicate overhead. No additional qualification scan runs.
  if (opaqueIdentity && coin_render_composition_borrow_enabled()) {
    qualification.qualifyMs = qualification.elapsed(qualificationBegin);
    qualification.computedItems = order.size();
  }
  diagnostic.clear();
  return true;
}

// Coin's 32x32 Bayer matrix, built from {0,2,3,1}; framebuffer origin is bottom-left.
inline uint32_t coin_render_screen_door_rank(uint32_t x, uint32_t y) {
  uint32_t rank = 0, weight = 256;
  for (int bit = 0; bit < 5; ++bit) {
    const uint32_t bx = x & 1, by = y & 1;
    rank += (by ? 3 - 2 * bx : 2 * bx) * weight;
    x >>= 1;
    y >>= 1;
    weight >>= 2;
  }
  return rank;
}

// Expand sorted requests into triangle ranges, including material runs of one
// shape. Executors consume this sequence; no backend sorts Coin triangles.
inline bool coin_render_composition_schedule(const CoinRenderFramePlan& frame,
                                             std::vector<CoinRenderCompositionItem>& schedule,
                                             std::string& diagnostic,
                                             const CoinRenderFramePreflight * preflight = nullptr) {
  std::vector<CoinRenderCompositionItem> computedOrder;
  const auto * cachedOrder = preflight ? preflight->compositionFor(frame) : nullptr;
  if (!cachedOrder && !coin_render_composition_order(frame, computedOrder, diagnostic))
    return false;
  const auto & order = cachedOrder ? *cachedOrder : computedOrder;
  schedule.clear();
  for (size_t begin = 0; begin < order.size();) {
    const auto& first = order[begin];
    const auto& source = frame.draws[first.drawIndex];
    if (!first.blend || !first.sortTriangles) {
      schedule.push_back(first);
      ++begin;
      continue;
    }
    size_t end = begin + 1;
    if (source.sourceNodeId && source.renderLayer == 0)
      while (end < order.size()) {
        const auto& next = order[end];
        const auto& draw = frame.draws[next.drawIndex];
        const auto& a = frame.renderStates[source.renderStateSlot];
        const auto& b = frame.renderStates[draw.renderStateSlot];
        if (!next.blend || !next.sortTriangles || draw.sourceNodeId != source.sourceNodeId ||
            draw.renderLayer != source.renderLayer || a.model != b.model || a.view != b.view)
          break;
        ++end;
      }
    std::vector<CoinRenderCompositionItem> triangles;
    for (size_t i = begin; i < end; ++i) {
      const auto& item = order[i];
      const auto& draw = frame.draws[item.drawIndex];
      const auto& state = frame.renderStates[draw.renderStateSlot];
      if (draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST || item.indexCount % 3) {
        diagnostic = "Triangle sorting requires complete triangle lists";
        return false;
      }
      const SbMatrix mv = state.model * state.view;
      for (uint32_t index = item.firstIndex; index < item.firstIndex + item.indexCount;
           index += 3) {
        auto triangle = item;
        triangle.firstIndex = index;
        triangle.indexCount = 3;
        triangle.eyeDepth = 0;
        for (uint32_t v = 0; v < 3; ++v) {
          SbVec3f eye;
          mv.multVecMatrix(SbVec3f(frame.vertices[frame.indices[index + v]].position), eye);
          triangle.eyeDepth -= eye[2] / 3;
        }
        if (!coin_render_is_finite(triangle.eyeDepth)) {
          diagnostic = "Non-finite triangle sorting depth";
          return false;
        }
        triangles.push_back(triangle);
      }
    }
    std::stable_sort(triangles.begin(), triangles.end(),
                     [](const CoinRenderCompositionItem& a, const CoinRenderCompositionItem& b) {
                       return a.eyeDepth > b.eyeDepth;
                     });
    schedule.insert(schedule.end(), triangles.begin(), triangles.end());
    begin = end;
  }
  diagnostic.clear();
  return true;
}

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
  bool prepare(const CoinRenderFramePlan & frame, std::string & diagnostic,
               const CoinRenderFramePreflight * preflight = nullptr) {
    items = &owned;
    trace.copiedItems = trace.borrowedItems = trace.computedItems = 0;
    const auto qualifyBegin = trace.begin();
    const auto * loan = coin_render_composition_borrow_enabled() && preflight
      ? preflight->opaqueCompositionFor(frame) : nullptr;
    trace.qualifyMs += trace.elapsed(qualifyBegin);
    if (loan) {
      items = loan;
      trace.borrowedItems = loan->size();
      diagnostic.clear();
      return true;
    }
    const auto copyBegin = trace.begin();
    const bool result = coin_render_composition_schedule(frame, owned, diagnostic, preflight);
    trace.copyMs += trace.elapsed(copyBegin);
    trace.copiedItems = owned.size();
    trace.computedItems = preflight && preflight->compositionFor(frame) ? 0 : owned.size();
    return result;
  }
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
