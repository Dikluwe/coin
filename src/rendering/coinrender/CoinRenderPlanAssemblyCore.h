#ifndef COIN_RENDER_PLAN_ASSEMBLY_CORE_H
#define COIN_RENDER_PLAN_ASSEMBLY_CORE_H
#include <Inventor/CoinRenderExport.h>
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderIndexedGeometryCore.h"
#include <unordered_map>
struct CoinRenderPolygonStyleResult;

// Mechanical assembly over captured values; never reads an action, node or state.
class COIN_RENDER_DLL_API CoinRenderPlanAssemblyCore {
public:
  // Unique model transforms need one candidate without a second allocation.
  // Shared transforms retain the contiguous scan of additional candidates.
  struct StateCandidates {
    explicit StateCandidates(uint32_t slot) : first(slot) {}
    uint32_t first;
    std::vector<uint32_t> additional;
  };
  using StateIndex = std::unordered_map<uint64_t, StateCandidates>;
  static void normalizeCamera(CoinRenderCameraSnapshot &);
  static void transformLight(CoinRenderLightSourceSnapshot &);
  static void normalizeState(CoinRenderRenderStateSnapshot &, const CoinRenderCameraSnapshot &);
  static void sortingCenter(CoinRenderDrawPacket &, const SbMatrix &, const SbVec3f &);
  static uint32_t material(CoinRenderFramePlan &, const CoinRenderMaterialSnapshot &);
  static uint32_t lighting(CoinRenderFramePlan &, const CoinRenderLightingSnapshot &);
  static uint32_t camera(CoinRenderFramePlan &, const CoinRenderCameraSnapshot &);
  static uint32_t viewport(CoinRenderFramePlan &, const CoinRenderViewportSnapshot &);
  static uint32_t state(CoinRenderFramePlan &, StateIndex &, const CoinRenderRenderStateSnapshot &);
  static uint32_t texture(CoinRenderFramePlan &, CoinRenderTextureImageSnapshot &&);
  static uint32_t sampler(CoinRenderFramePlan &, const CoinRenderSamplerSnapshot &);
  static void makeIndicesAppendable(CoinRenderFramePlan &, CoinRenderGeometryRange &);
  static void appendIndexed(CoinRenderFramePlan &, CoinRenderDrawPacket &, const CoinRenderIndexedGeometryResult &);
  static void appendPolygon(CoinRenderFramePlan &, CoinRenderDrawPacket &, CoinRenderPolygonStyleResult &);
};

// Bounded frame-local meshes learned from native callbacks. Wiring proves
// shape eligibility and captures dimensions/binding before invoking this Core.
class COIN_RENDER_DLL_API CoinRenderCubeGeometryCore {
public:
  void reset(bool templateCache = true);
  void learn(const CoinRenderFramePlan &, size_t firstVertex, size_t firstIndex,
             size_t firstDraw, const float (&dimensions)[3], int normalBinding);
  bool matches(const float (&dimensions)[3], int normalBinding) const;
  bool replay(CoinRenderFramePlan &, CoinRenderDrawPacket &, uint32_t materialSlot,
              const float (&dimensions)[3], int normalBinding);
  size_t templateCount() const { return templates.size(); }
  size_t rangeCount() const;
  uint64_t templateEvictionCount() const { return templateEvictions; }
  uint64_t rangeEvictionCount() const { return rangeEvictions; }
  uint64_t rangeReuseCount() const { return rangeReuseHits; }
private:
  enum { TEMPLATE_LIMIT = 32, RANGE_LIMIT = 64, LEGACY_RANGE_LIMIT = 32 };
  struct Geometry {
    Geometry(uint32_t material, uint32_t vertex, uint32_t index, uint64_t used)
      : materialSlot(material), firstVertex(vertex), firstIndex(index), lastUsed(used) {}
    uint32_t materialSlot, firstVertex, firstIndex;
    uint64_t lastUsed;
  };
  struct Template {
    CoinRenderVertexSnapshot vertices[24];
    uint32_t indices[36];
    float dimensions[3] = {};
    int normalBinding = 0;
    uint64_t lastUsed = 0;
    std::vector<Geometry> meshes;
  };
  Template * find(const float (&dimensions)[3], int normalBinding);
  const Template * find(const float (&dimensions)[3], int normalBinding) const;
  void rememberRange(Template &, uint32_t material, uint32_t vertex, uint32_t index);
  std::vector<Template> templates;
  bool templateCache = true, disabled = false;
  uint64_t clock = 0, templateEvictions = 0, rangeEvictions = 0, rangeReuseHits = 0;
};
#endif
