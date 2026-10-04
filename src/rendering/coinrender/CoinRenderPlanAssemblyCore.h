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
  using StateIndex = std::unordered_map<uint64_t, std::vector<uint32_t>>;
  static void normalizeCamera(CoinRenderCameraSnapshot &);
  static void transformLight(CoinRenderLightSourceSnapshot &);
  static void normalizeState(CoinRenderRenderStateSnapshot &, const CoinRenderCameraSnapshot &);
  static bool projectTexcoord(const SbVec4f &, float (&)[2]);
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

// One bounded frame-local mesh learned from native callbacks. Wiring proves
// shape eligibility and captures dimensions/binding before invoking this Core.
class COIN_RENDER_DLL_API CoinRenderCubeGeometryCore {
public:
  void reset() { ready = false; meshes.clear(); }
  void learn(const CoinRenderFramePlan &, size_t firstVertex, size_t firstIndex,
             size_t firstDraw, const float (&dimensions)[3], int normalBinding);
  bool matches(const float (&dimensions)[3], int normalBinding) const;
  void replay(CoinRenderFramePlan &, CoinRenderDrawPacket &, uint32_t materialSlot);
private:
  bool ready = false;
  CoinRenderVertexSnapshot vertices[24];
  uint32_t indices[36];
  float dimensions[3] = {};
  int normalBinding = 0;
  struct Geometry { uint32_t materialSlot, firstVertex, firstIndex; };
  std::vector<Geometry> meshes;
};
#endif
