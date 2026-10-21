#ifndef COIN_RENDER_SHADOW_CORE_H
#define COIN_RENDER_SHADOW_CORE_H

#include <Inventor/CoinRenderExport.h>

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderClipCore.h"
#include "rendering/coinrender/CoinRenderTextureAlphaCore.h"
#include <Inventor/SbBox3f.h>
#include <Inventor/SbRotation.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <algorithm>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

struct CoinRenderShadowPass {
  uint32_t groupSlot = 0;
  uint32_t lightSlot = 0;
  uint32_t mapSize = 0;
  bool visible = true;
  SbMatrix view = SbMatrix::identity();
  SbMatrix projectionCoin = SbMatrix::identity();
  float nearDistance = 0.1f;
  float farDistance = 100.0f;
  // Coin/GL normalizes VSM moments with a farther range than the camera clips.
  float vsmFarDistance = 100.0f;
  float epsilon = 0.00001f;
  float threshold = 0.1f;
  float maxShadowDistance = -1.0f;
  float distanceFalloffCoefficient = 2.35f; // Coin directional exp(z*abs(z)/d^2).
  bool perFragmentLighting = false;
  // -1 means the light is absent from this draw's captured Coin state.
  // It cannot illuminate this draw before traversal reaches its node.
  std::vector<int32_t> lightingIndexByState;
  std::vector<CoinRenderLightSourceSnapshot> resolvedLightByState;
  std::vector<uint32_t> casterDraws;
  std::vector<uint32_t> receiverDraws;
};

struct CoinRenderShadowPlan {
  std::vector<CoinRenderShadowPass> passes;
  uint64_t attachmentBytes = 0;
};

// Resolve the actual square target before traversal, so pixel viewports stay
// pixels and normalized viewports are evaluated at the real map resolution.
COIN_RENDER_DLL_API bool coin_render_shadow_map_size(float precision, uint32_t &size,
                                                     std::string &diagnostic);
// Projection maps to the full map; scissor is bottom-left map pixels. Original
// camera XY clipping is retained by this viewport/map intersection.
COIN_RENDER_DLL_API bool coin_render_shadow_caster_viewport(
    const CoinRenderFramePlan &frame, const CoinRenderRenderStateSnapshot &state, uint32_t mapSize,
    SbMatrix &transform, int32_t scissor[4], std::string &diagnostic);

// Infra's packed moment receiver retains the original render targets and one
// array copy. Charge its real padding/layers, including D32F source attachments,
// against the existing per-apply shadow attachment budget before GPU work.
inline bool coin_render_shadow_array_budget(const CoinRenderShadowPlan & plan,
    uint32_t minimumLayers, std::string & diagnostic) {
  if(plan.passes.empty())return true;
  uint64_t extent=0,sourceBytes=0;
  for(const auto &pass:plan.passes) {
    extent=std::max(extent,uint64_t(pass.mapSize));
    sourceBytes+=uint64_t(pass.mapSize)*pass.mapSize*20u;
  }
  const uint64_t arrayBytes=extent*extent*16u*std::max<uint64_t>(minimumLayers,plan.passes.size());
  const uint64_t budget=128ull*1024*1024;
  if(sourceBytes>budget || arrayBytes>budget-sourceBytes) {
    diagnostic="Shadow source attachments and moment array exceed 128 MiB per apply";return false;
  }
  return true;
}

COIN_RENDER_DLL_API bool
coin_render_shadow_finite(const SbVec3f & value);

// Only Core interprets Coin quality; Infra executes these resolved flags.
enum CoinRenderShadowShadingFlags {
  COIN_RENDER_SHADOW_RECEIVES = 1u,
  COIN_RENDER_SHADOW_VERTEX_LIGHTING = 2u,
  COIN_RENDER_SHADOW_ORDINARY_FRAGMENT = 4u,
  COIN_RENDER_SHADOW_GROUP_LIGHTING = 8u
};
COIN_RENDER_DLL_API uint32_t coin_render_shadow_shading_flags(
  const CoinRenderFramePlan & frame, const CoinRenderRenderStateSnapshot & state);

// Groups form a captured tree. No backend walks Coin nodes or interprets nesting.
COIN_RENDER_DLL_API bool coin_render_shadow_descends_from(const CoinRenderFramePlan & frame,
                                             uint32_t group, uint32_t ancestor);

// Build the world-space bounds of the group's captured shapes once. This is
// Coin geometry/state, not a backend-specific reinterpretation of the scene.
COIN_RENDER_DLL_API bool
coin_render_shadow_group_bounds(const CoinRenderFramePlan & frame,
                                uint32_t groupSlot, SbBox3f & bounds,
                                std::string & diagnostic);

COIN_RENDER_DLL_API SbBox3f
coin_render_shadow_camera_bounds(const SbBox3f & worldBounds,
                                 const SbVec3f & cameraPosition,
                                 const SbRotation & orientation);

// Rebuild the Coin view volume from the captured camera, for directional
// shadow-frustum intersection. No scene traversal is needed by a backend.
COIN_RENDER_DLL_API bool
coin_render_shadow_view_volume(const CoinRenderCameraSnapshot & camera,
                               SbViewVolume & volume, std::string & diagnostic);

COIN_RENDER_DLL_API bool
coin_render_shadow_camera(const CoinRenderShadowGroupSnapshot & group,
                          const CoinRenderShadowLightSnapshot & light,
                          const SbBox3f & groupBounds,
                          const SbViewVolume * mainView,
                          CoinRenderShadowPass & pass,
                          std::string & diagnostic);

// Coin SoShape::shouldGLRender excludes transparent shapes from SHADOWMAP,
// independently of transparency mode and SoShadowStyle. Wiring captures the
// material-array flag; Core combines it with the immutable texture evidence.
COIN_RENDER_DLL_API bool
coin_render_shadow_transparent_shape(const CoinRenderFramePlan & frame,
                                    const CoinRenderRenderStateSnapshot & state);

// Append separately traversed geometry without exposing it to object composition.
COIN_RENDER_DLL_API void
coin_render_append_shadow_scene(CoinRenderFramePlan & frame,
                                const CoinRenderFramePlan & scene, uint32_t lightSlot,
                                uint32_t inheritedClipPlaneCount);

// Pure Core planning over immutable captures. Resources and GPU passes belong
// to CoinBgfx/CoinWgpu; only explicitly qualified profiles execute.
COIN_RENDER_DLL_API bool
coin_render_plan_shadows(const CoinRenderFramePlan & frame,
                         CoinRenderShadowPlan & output, std::string & diagnostic);

// Materialize bitmap coverage independently for each light camera. The
// observer's screen-space quads are never used as shadow casters.
COIN_RENDER_DLL_API bool
coin_render_materialize_raster_casters(CoinRenderFramePlan & frame,
                                      const CoinRenderShadowPlan & shadows,
                                      std::string & diagnostic);

// Qualified object-composition profile over captured Coin state. It checks the scene once,
// then matches each ordinary light contribution to exactly one shadow pass.
// Backends consume the resolved pass order without rediscovering Coin nodes.
COIN_RENDER_DLL_API bool
coin_render_shadow_object_profile(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadows,
  size_t lightCount, std::string & diagnostic);

// Coin's shadow shader is assembled from lights present at group entry.
// An ordinary light encountered inside that active group is captured by the
// normal traversal, but cannot contribute to the already assembled shader.
COIN_RENDER_DLL_API bool
coin_render_shadow_suppresses_ordinary_light(
  const CoinRenderFramePlan & frame, const CoinRenderRenderStateSnapshot & state,
  const CoinRenderLightSourceSnapshot & source);

COIN_RENDER_DLL_API bool
coin_render_shadow_single_object_profile(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadows,
  CoinRenderLightType lightType, std::string & diagnostic);

COIN_RENDER_DLL_API bool
coin_render_shadow_single_spot_object_profile(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadows,
  std::string & diagnostic);

COIN_RENDER_DLL_API bool
coin_render_shadow_single_directional_object_profile(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadows,
  std::string & diagnostic);

// The bounded two-pass opaque profile accepts any pair of eligible spot and/or
// directional lights. Core already owns the Coin light identity and index match.
COIN_RENDER_DLL_API bool
coin_render_shadow_two_object_profile(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadows,
  std::string & diagnostic);

// Stronger fixture predicate for scenes with one spot and one directional.
COIN_RENDER_DLL_API bool
coin_render_shadow_spot_directional_object_profile(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadows,
  std::string & diagnostic);

#endif // COIN_RENDER_SHADOW_CORE_H
