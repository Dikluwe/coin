#ifndef COIN_RENDER_SHADOW_CORE_H
#define COIN_RENDER_SHADOW_CORE_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderClipCore.h"
#include "rendering/coinrender/CoinRenderTextureAlphaCore.h"
#include <Inventor/SbBox3f.h>
#include <Inventor/SbVec4f.h>
#include <Inventor/rendering/CoinRenderOptions.h>
#include <Inventor/SbRotation.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/SbDPViewVolume.h>
#include <Inventor/SbDPMatrix.h>
#include <Inventor/SbDPRotation.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

struct CoinRenderShadowPass {
  uint32_t groupSlot = 0;
  uint32_t lightSlot = 0;
  uint32_t mapSize = 0;
  bool visible = true;
  bool stabilized = false;
  uint32_t stabilizationLevel = 0;
  double texelWorld = 0.0;
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
  uint64_t momentsBytes = 0;
  uint64_t depthBytes = 0;
  uint64_t attachmentBytes = 0;
};

// Exact, bounded process-local content keys: no hash collision can publish a
// stale map. Opaque moment maps depend on positions, transforms, clipping and
// caster culling, not the receiver's BRDF, light color or VSM lookup epsilon.
class CoinRenderShadowKeyWriter {
public:
  explicit CoinRenderShadowKeyWriter(size_t limit):limit(limit) {}
  template<class T> bool add(const T & value) {return bytes(&value,sizeof(value));}
  bool bytes(const void * source,size_t count) {
    if(count>limit||key.size()>limit-count)return false;
    const size_t needed=key.size()+count;
    if(needed>key.capacity())key.reserve(std::min(limit,std::max(needed,std::max(size_t(256),key.capacity()*2))));
    const auto * p=static_cast<const uint8_t *>(source);key.insert(key.end(),p,p+count);return true;
  }
  std::vector<uint8_t> key;
private: size_t limit;
};
inline std::vector<uint8_t>
coin_render_shadow_content_key(const CoinRenderFramePlan & frame,
  const CoinRenderShadowPass & pass,bool momentCorrection,uint64_t device,
  uint64_t generation,size_t limit)
{
  CoinRenderShadowKeyWriter w(limit);
  if(!w.add(device)||!w.add(generation)||!w.add(pass.mapSize)||!w.add(momentCorrection)||
     !w.bytes(pass.view.getValue(),sizeof(SbMat))||!w.bytes(pass.projectionCoin.getValue(),sizeof(SbMat))||
     !w.add(pass.nearDistance)||!w.add(pass.vsmFarDistance)||
     pass.lightSlot>=frame.shadowLights.size()||!w.add(frame.shadowLights[pass.lightSlot].type))return {};
  for(uint32_t slot:pass.casterDraws) {
    if(slot>=frame.draws.size())return {};const auto & draw=frame.draws[slot];
    if(draw.renderStateSlot>=frame.renderStates.size())return {};const auto & state=frame.renderStates[draw.renderStateSlot];
    if(!w.add(draw.geometry.indexCount)||!w.bytes(state.model.getValue(),sizeof(SbMat))||
       !w.add(state.cullMode)||!w.add(state.frontFace)||!w.add(state.shadowCulling)||
       !w.add(state.clipPlanesWorld.size()))return {};
    if(!state.clipPlanesWorld.empty()) {
      if(!w.bytes(state.view.getValue(),sizeof(SbMat)))return {};
      for(const auto & plane:state.clipPlanesWorld) {
        const auto & n=plane.getNormal();const float d=plane.getDistanceFromOrigin();
        if(!w.bytes(n.getValue(),3*sizeof(float))||!w.add(d))return {};
      }
    }
    const uint64_t end=uint64_t(draw.geometry.firstIndex)+draw.geometry.indexCount;
    if(end>frame.indices.size())return {};
    for(uint64_t i=draw.geometry.firstIndex;i<end;++i) {
      const auto index=frame.indices[size_t(i)];if(index>=frame.vertices.size())return {};
      if(!w.bytes(frame.vertices[index].position,3*sizeof(float)))return {};
    }
  }
  return std::move(w.key);
}

// The former 128 MiB check counted RGBA32F alone. Its actually admitted
// RGBA32F + D32F attachments occupied up to 160 MiB. Preserve that physical
// ceiling and the qualified eight-map profile while accounting for both.
constexpr uint64_t COIN_RENDER_SHADOW_ATTACHMENT_LIMIT = uint64_t(160) * 1024 * 1024;

inline bool
coin_render_shadow_view_budget(size_t maps, size_t sceneViews, size_t overlayViews,
                              size_t availableViews, std::string & diagnostic)
{
  if (maps > availableViews || sceneViews > availableViews - maps ||
      overlayViews > availableViews - maps - sceneViews) {
    diagnostic = "Shadow, scene and overlay passes exceed the target view budget";
    return false;
  }
  return true;
}

inline bool
coin_render_shadow_finite(const SbVec3f & value)
{
  return std::isfinite(value[0]) && std::isfinite(value[1]) &&
         std::isfinite(value[2]);
}

// Only Core interprets Coin quality; Infra executes these resolved flags.
enum CoinRenderShadowShadingFlags {
  COIN_RENDER_SHADOW_RECEIVES = 1u,
  COIN_RENDER_SHADOW_VERTEX_LIGHTING = 2u,
  COIN_RENDER_SHADOW_ORDINARY_FRAGMENT = 4u,
  COIN_RENDER_SHADOW_GROUP_LIGHTING = 8u
};
inline uint32_t coin_render_shadow_shading_flags(
  const CoinRenderFramePlan & frame, const CoinRenderRenderStateSnapshot & state)
{
  if (!state.shadowGroupSlot || state.shadowGroupSlot > frame.shadowGroups.size() ||
      !(state.shadowStyle & 2u)) return 0;
  const float quality = frame.shadowGroups[state.shadowGroupSlot - 1].quality;
  // Match Coin's double literals: float(0.3) is already above double(0.3).
  return COIN_RENDER_SHADOW_GROUP_LIGHTING |
    (quality <= 0.3 ? COIN_RENDER_SHADOW_VERTEX_LIGHTING : 0u) |
    (quality > 0.7 ? COIN_RENDER_SHADOW_ORDINARY_FRAGMENT : 0u);
}

// Groups form a captured tree. No backend walks Coin nodes or interprets nesting.
inline bool coin_render_shadow_descends_from(const CoinRenderFramePlan & frame,
                                             uint32_t group, uint32_t ancestor)
{
  while (group && group <= frame.shadowGroups.size()) {
    if (group == ancestor) return true;
    const uint32_t parent = frame.shadowGroups[group - 1].parentGroupSlot;
    if (parent >= group) return false;
    group = parent;
  }
  return false;
}

// Build the world-space bounds of the group's captured shapes once. This is
// Coin geometry/state, not a backend-specific reinterpretation of the scene.
inline bool
coin_render_shadow_group_bounds(const CoinRenderFramePlan & frame,
                                uint32_t groupSlot, SbBox3f & bounds,
                                std::string & diagnostic)
{
  bounds.makeEmpty();
  for (const auto & draw : frame.draws) {
    if (draw.renderStateSlot >= frame.renderStates.size()) {
      diagnostic = "Shadow draw references an invalid render state";
      return false;
    }
    const auto & state = frame.renderStates[draw.renderStateSlot];
    if (draw.renderLayer != 0 || !coin_render_shadow_descends_from(frame, state.shadowGroupSlot, groupSlot)) continue;
    // A partial main traversal still uses the whole group's bounds for maps.
    // An implicit group capture supplies the omitted shapes; external custom
    // scenes keep Coin's camera bounds tied to the original group.
    if (draw.shadowLightSlot &&
        (draw.shadowLightSlot > frame.shadowLights.size() ||
         frame.shadowLights[draw.shadowLightSlot-1].hasCustomScene)) continue;
    const uint64_t end = uint64_t(draw.geometry.firstVertex) + draw.geometry.vertexCount;
    if (end > frame.vertices.size()) {
      diagnostic = "Shadow draw references an invalid vertex range";
      return false;
    }
    for (uint64_t index = draw.geometry.firstVertex; index < end; ++index) {
      const auto & vertex = frame.vertices[static_cast<size_t>(index)];
      const SbVec3f local(vertex.position[0], vertex.position[1], vertex.position[2]);
      SbVec3f world;
      state.model.multVecMatrix(local, world);
      if (!coin_render_shadow_finite(world)) {
        diagnostic = "Shadow geometry has a non-finite world position";
        return false;
      }
      bounds.extendBy(world);
    }
  }
  return true;
}

inline SbBox3f
coin_render_shadow_camera_bounds(const SbBox3f & worldBounds,
                                 const SbVec3f & cameraPosition,
                                 const SbRotation & orientation)
{
  SbBox3f result;
  if (worldBounds.isEmpty()) return result;
  const SbRotation inverse = orientation.inverse();
  const SbVec3f & lo = worldBounds.getMin();
  const SbVec3f & hi = worldBounds.getMax();
  for (int x = 0; x < 2; ++x)
    for (int y = 0; y < 2; ++y)
      for (int z = 0; z < 2; ++z) {
        SbVec3f local;
        inverse.multVec(SbVec3f(x ? hi[0] : lo[0],
                                y ? hi[1] : lo[1],
                                z ? hi[2] : lo[2]) - cameraPosition, local);
        result.extendBy(local);
      }
  return result;
}

// Rebuild the Coin view volume from the captured camera, for directional
// shadow-frustum intersection. No scene traversal is needed by a backend.
inline bool
coin_render_shadow_view_volume(const CoinRenderCameraSnapshot & camera,
                               SbViewVolume & volume, std::string & diagnostic)
{
  if (camera.nearDistance <= 0.0f ||
      camera.farDistance <= camera.nearDistance ||
      std::abs(camera.projectionMatrixCoin.det4()) < 1.0e-12f ||
      std::abs(camera.viewMatrix.det4()) < 1.0e-12f) {
    diagnostic = "Directional shadow camera has invalid clip planes or matrices";
    return false;
  }
  const SbMatrix inverseProjection = camera.projectionMatrixCoin.inverse();
  SbVec3f lowerLeft, lowerRight, upperLeft;
  inverseProjection.multVecMatrix(SbVec3f(-1, -1, -1), lowerLeft);
  inverseProjection.multVecMatrix(SbVec3f(1, -1, -1), lowerRight);
  inverseProjection.multVecMatrix(SbVec3f(-1, 1, -1), upperLeft);
  if (!coin_render_shadow_finite(lowerLeft) ||
      !coin_render_shadow_finite(lowerRight) ||
      !coin_render_shadow_finite(upperLeft) ||
      lowerRight[0] <= lowerLeft[0] || upperLeft[1] <= lowerLeft[1]) {
    diagnostic = "Directional shadow camera has an invalid near plane";
    return false;
  }
  if (camera.isPerspective)
    volume.frustum(lowerLeft[0], lowerRight[0], lowerLeft[1], upperLeft[1],
                   camera.nearDistance, camera.farDistance);
  else
    volume.ortho(lowerLeft[0], lowerRight[0], lowerLeft[1], upperLeft[1],
                 camera.nearDistance, camera.farDistance);
  volume.transform(camera.viewMatrix.inverse());
  return true;
}

// Stable square footprint: discrete scale relative to the captured drawing,
// snapped in light-space texels. Full caster depth remains independent of the
// camera. Double Sb math reduces cancellation; source captures are still float.
inline bool
coin_render_shadow_stable_directional(const SbBox3f & fullBounds,
  const SbBox3f & visibleBounds, const SbVec3f & direction,
  CoinRenderShadowPass & pass, std::string & diagnostic)
{
  if (pass.mapSize < 4) {
    diagnostic="Stable PBR directional maps require at least four texels";
    return false;
  }
  const SbVec3f fullSize=fullBounds.getSize(), fitSize=visibleBounds.getSize();
  const double radius=0.5*SbVec3d(fullSize[0],fullSize[1],fullSize[2]).length();
  const double fitRadius=0.5*SbVec3d(fitSize[0],fitSize[1],fitSize[2]).length();
  if (!std::isfinite(radius)||radius<=0||!std::isfinite(fitRadius)) {
    diagnostic="Stable directional footprint requires finite nondegenerate drawing bounds";
    return false;
  }
  double extent=radius;
  uint32_t level=0;
  while (level<20 && extent*0.5>=fitRadius && fitRadius>0) {extent*=0.5;++level;}
  // One texel margin in each direction covers center rounding at every level.
  extent/=1.0-2.0/double(pass.mapSize);
  const double pitch=2.0*extent/pass.mapSize;
  const SbDPRotation orientation(SbVec3d(0,0,-1),SbVec3d(direction[0],direction[1],direction[2]));
  const auto inverse=orientation.inverse();
  const SbVec3f fc=visibleBounds.getCenter();SbVec3d center;
  inverse.multVec(SbVec3d(fc[0],fc[1],fc[2]),center);
  center[0]=std::floor(center[0]/pitch+0.5)*pitch;
  center[1]=std::floor(center[1]/pitch+0.5)*pitch;
  double low=INFINITY,high=-INFINITY;
  const auto lo=fullBounds.getMin(),hi=fullBounds.getMax();
  for(int x=0;x<2;++x)for(int y=0;y<2;++y)for(int z=0;z<2;++z) {
    SbVec3d corner;inverse.multVec(SbVec3d(x?hi[0]:lo[0],y?hi[1]:lo[1],z?hi[2]:lo[2]),corner);
    low=std::min(low,corner[2]);high=std::max(high,corner[2]);
  }
  const double depth=std::max(high-low,pitch);
  center[2]=high+depth+pitch;
  SbVec3d position;orientation.multVec(center,position);
  SbDPViewVolume volume;volume.ortho(-extent,extent,-extent,extent,depth,2.0*depth+2.0*pitch);
  volume.rotateCamera(orientation);volume.translateCamera(position);
  SbDPMatrix view,projection;volume.getMatrices(view,projection);
  CoinRenderShadowPass candidate=pass;
  for(int row=0;row<4;++row)for(int column=0;column<4;++column) {
    candidate.view[row][column]=float(view[row][column]);
    candidate.projectionCoin[row][column]=float(projection[row][column]);
    if(!std::isfinite(candidate.view[row][column])||!std::isfinite(candidate.projectionCoin[row][column])) {
      diagnostic="Stable directional matrices exceed the float capture/output range";return false;
    }
  }
  candidate.nearDistance=float(depth);candidate.farDistance=float(2.0*depth+2.0*pitch);
  candidate.vsmFarDistance=candidate.farDistance*1.1f;
  if(candidate.nearDistance<=0||!std::isfinite(candidate.vsmFarDistance)||
     candidate.farDistance<=candidate.nearDistance) {
    diagnostic="Stable directional depth exceeds the float capture/output range";return false;
  }
  candidate.stabilized=true;candidate.stabilizationLevel=level;candidate.texelWorld=pitch;
  pass=std::move(candidate);
  return true;
}

inline bool
coin_render_shadow_camera(const CoinRenderShadowGroupSnapshot & group,
                          const CoinRenderShadowLightSnapshot & light,
                          const SbBox3f & groupBounds,
                          const SbViewVolume * mainView,
                          CoinRenderShadowPass & pass,
                          std::string & diagnostic, bool stablePbr = false)
{
  if (groupBounds.isEmpty()) {
    pass.visible = false;
    return true;
  }
  SbVec3f direction;
  light.model.multDirMatrix(light.direction, direction);
  if (!coin_render_shadow_finite(direction) || direction.normalize() == 0.0f) {
    diagnostic = "Shadow light has an invalid direction";
    return false;
  }
  const SbRotation orientation(SbVec3f(0, 0, -1), direction);
  SbViewVolume volume;
  if (light.type == CoinRenderLightType::SPOT) {
    SbVec3f position;
    light.model.multVecMatrix(light.position, position);
    if (!coin_render_shadow_finite(position) ||
        !std::isfinite(light.cutOffAngle) || light.cutOffAngle <= 0.0f) {
      diagnostic = "Shadow spot light has an invalid position or cone";
      return false;
    }
    const SbBox3f cameraBounds = coin_render_shadow_camera_bounds(
      groupBounds, position, orientation);
    const float rawNear = -cameraBounds.getMax()[2];
    const float rawFar = -cameraBounds.getMin()[2];
    if (rawFar <= 0.0f) {
      pass.visible = false;
      return true;
    }
    // Coin/GL limits the perspective half-angle to 0.78 radians.
    const float halfAngle = std::min(light.cutOffAngle, 0.78f);
    float nearDistance = std::max(rawFar / 65536.0f, rawNear) * 0.999f;
    float farDistance = rawFar * 1.001f;
    if (group.visibilityFlag == 1 || group.visibilityFlag == 2) {
      float scale = farDistance;
      if (group.visibilityFlag == 1) {
        const SbVec3f size = groupBounds.getSize();
        scale = std::max(size[0], std::max(size[1], size[2]));
      }
      if (group.visibilityNearRadius > 0.0f)
        nearDistance = scale * group.visibilityNearRadius;
      if (group.visibilityRadius > 0.0f)
        farDistance = scale * group.visibilityRadius;
    } else {
      if (group.visibilityNearRadius > 0.0f)
        nearDistance = group.visibilityNearRadius;
      if (group.visibilityRadius > 0.0f)
        farDistance = group.visibilityRadius;
    }
    // SoShadowSpotLight overrides the group's visibility calculation.
    if (light.nearDistance > 0.0f && light.farDistance > light.nearDistance) {
      nearDistance = light.nearDistance;
      farDistance = light.farDistance;
    }
    if (!std::isfinite(nearDistance) || !std::isfinite(farDistance) ||
        nearDistance <= 0.0f || farDistance <= nearDistance) {
      diagnostic = "Shadow spot light has an invalid near/far range";
      return false;
    }
    pass.nearDistance = nearDistance;
    pass.farDistance = farDistance;
    pass.vsmFarDistance = farDistance / std::cos(halfAngle * 2.0f);
    volume.perspective(halfAngle * 2.0f, 1.0f, nearDistance, farDistance);
    volume.rotateCamera(orientation);
    volume.translateCamera(position);
  } else {
    SbBox3f bounds = groupBounds;
    if (light.bboxSize[0] >= 0.0f && light.bboxSize[1] >= 0.0f &&
        light.bboxSize[2] >= 0.0f) {
      const SbVec3f half = light.bboxSize * 0.5f;
      bounds.setBounds(light.bboxCenter - half, light.bboxCenter + half);
    }
    const SbBox3f fullBounds = bounds;
    if (!mainView) {
      diagnostic = "Directional shadow requires the group-entry view volume";
      return false;
    }
    SbViewVolume visibleVolume = *mainView;
    if (light.maxShadowDistance > 0.0f) {
      const float nearDistance = visibleVolume.getNearDist();
      const float depth = visibleVolume.getDepth();
      if (light.maxShadowDistance <= nearDistance) {
        pass.visible = false;
        return true;
      }
      const float narrowedDepth = std::min(light.maxShadowDistance - nearDistance, depth);
      visibleVolume = visibleVolume.zNarrow(
        1.0f, 1.0f - narrowedDepth / depth);
    }
    bounds = visibleVolume.intersectionBox(fullBounds);
    if (bounds.isEmpty()) {
      pass.visible = false;
      return true;
    }
    if (stablePbr)
      return coin_render_shadow_stable_directional(fullBounds,bounds,direction,pass,diagnostic);
    const SbVec3f size = bounds.getSize();
    const float extent = std::max(1.0f, std::max(size[0], std::max(size[1], size[2])));
    const SbVec3f position = bounds.getCenter() - direction * (extent * 2.0f);
    const SbBox3f fitCameraBounds = coin_render_shadow_camera_bounds(
      bounds, position, orientation);
    const SbBox3f depthCameraBounds = coin_render_shadow_camera_bounds(
      fullBounds, position, orientation);
    const SbVec3f & lo = fitCameraBounds.getMin();
    const SbVec3f & hi = fitCameraBounds.getMax();
    const float halfWidth = std::max(0.01f, (hi[0] - lo[0]) * 0.505f);
    const float halfHeight = std::max(0.01f, (hi[1] - lo[1]) * 0.505f);
    const float centerX = (lo[0] + hi[0]) * 0.5f;
    const float centerY = (lo[1] + hi[1]) * 0.5f;
    pass.nearDistance = std::max(0.001f, -depthCameraBounds.getMax()[2] * 0.99f);
    pass.farDistance = -depthCameraBounds.getMin()[2] * 1.01f;
    if (!std::isfinite(pass.farDistance) ||
        pass.farDistance <= pass.nearDistance) {
      diagnostic = "Shadow directional light has an invalid volume";
      return false;
    }
    pass.vsmFarDistance = pass.farDistance * 1.1f;
    volume.ortho(centerX - halfWidth, centerX + halfWidth,
                 centerY - halfHeight, centerY + halfHeight,
                 pass.nearDistance, pass.farDistance);
    volume.rotateCamera(orientation);
    volume.translateCamera(position);
  }
  if (!std::isfinite(pass.vsmFarDistance) ||
      pass.vsmFarDistance <= pass.nearDistance) {
    diagnostic = "Shadow VSM far distance is invalid";
    return false;
  }
  volume.getMatrices(pass.view, pass.projectionCoin);
  return true;
}

// Coin SoShape::shouldGLRender excludes transparent shapes from SHADOWMAP,
// independently of transparency mode and SoShadowStyle. Wiring captures the
// material-array flag; Core combines it with the immutable texture evidence.
inline bool
coin_render_shadow_transparent_shape(const CoinRenderFramePlan & frame,
                                    const CoinRenderRenderStateSnapshot & state)
{
  if (state.transparentMaterial || state.transparentTexture || state.screenDoorTransparency > 0.0f ||
      (state.materialSlot < frame.materials.size() &&
       frame.materials[state.materialSlot].transparency > 0.0f)) return true;
  if (!state.hasTexture || state.textureImageSlot >= frame.textures.size()) return false;
  return coin_render_texture_has_transparency(frame.textures[state.textureImageSlot]);
}

// Append separately traversed geometry without exposing it to object composition.
inline void
coin_render_append_shadow_scene(CoinRenderFramePlan & frame,
                                const CoinRenderFramePlan & scene, uint32_t lightSlot,
                                uint32_t inheritedClipPlaneCount)
{
  const uint32_t vertices = frame.vertices.size(), indices = frame.indices.size();
  const uint32_t materials = frame.materials.size(), states = frame.renderStates.size();
  const uint32_t lighting = frame.lightingStates.size(), cameras = frame.cameras.size();
  const uint32_t viewports = frame.viewports.size(), textures = frame.textures.size();
  const uint32_t samplers = frame.samplers.size();
  frame.materials.insert(frame.materials.end(), scene.materials.begin(), scene.materials.end());
  frame.lightingStates.insert(frame.lightingStates.end(), scene.lightingStates.begin(), scene.lightingStates.end());
  frame.cameras.insert(frame.cameras.end(), scene.cameras.begin(), scene.cameras.end());
  frame.viewports.insert(frame.viewports.end(), scene.viewports.begin(), scene.viewports.end());
  frame.textures.insert(frame.textures.end(), scene.textures.begin(), scene.textures.end());
  frame.samplers.insert(frame.samplers.end(), scene.samplers.begin(), scene.samplers.end());
  for (auto vertex : scene.vertices) { vertex.materialSlot += materials; frame.vertices.push_back(vertex); }
  for (auto index : scene.indices) frame.indices.push_back(index + vertices);
  for (auto state : scene.renderStates) {
    state.materialSlot += materials; state.lightingSlot += lighting;
    state.cameraSlot += cameras; state.viewportSlot += viewports;
    if (state.hasTexture) { state.textureImageSlot += textures; state.samplerSlot += samplers; }
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS - 1; ++unit)
      if (static_cast<const CoinRenderRenderStateSnapshot &>(state).extraTextures[unit].enabled) {
        state.extraTextures[unit].imageSlot += textures;
        state.extraTextures[unit].samplerSlot += samplers;
      }
    state.shadowGroupSlot = frame.shadowLights[lightSlot - 1].groupSlot;
    // Map geometry is captured in group-local coordinates after GL resets
    // the model matrix. The common pass uses world coordinates.
    const auto & entryModel = frame.shadowGroups[state.shadowGroupSlot - 1].entryModel;
    state.model = state.model * entryModel;
    // Entry planes are already world-space; only planes added by the
    // independent traversal need the group-local to world conversion.
    for (size_t i = inheritedClipPlaneCount; i < state.clipPlanesWorld.size(); ++i)
      state.clipPlanesWorld[i].transform(entryModel);
    frame.renderStates.push_back(std::move(state));
  }
  for (auto draw : scene.draws) {
    draw.geometry.firstVertex += vertices; draw.geometry.firstIndex += indices;
    draw.renderStateSlot += states; draw.shadowLightSlot = lightSlot;
    frame.draws.push_back(std::move(draw));
  }
}

// Pure Core planning over immutable captures. Resources and GPU passes belong
// to CoinBgfx/CoinWgpu; only explicitly qualified profiles execute.
inline bool
coin_render_plan_shadows(const CoinRenderFramePlan & frame,
                         CoinRenderShadowPlan & output, std::string & diagnostic,
                         uint64_t budget = COIN_RENDER_SHADOW_ATTACHMENT_LIMIT)
{
  CoinRenderShadowPlan candidate;
  const bool stablePbr=std::any_of(frame.materials.begin(),frame.materials.end(),
    [](const CoinRenderMaterialSnapshot & m){return m.model==CoinRenderMaterialModel::PBR;});
  for (size_t g = 0; g < frame.shadowGroups.size(); ++g) {
    const auto & group = frame.shadowGroups[g];
    if (!group.sourceRevision || group.parentGroupSlot > g ||
        !std::isfinite(group.precision) || group.precision <= 0.0f ||
        group.precision > 1.0f || !std::isfinite(group.quality) ||
        !std::isfinite(group.intensity) || !std::isfinite(group.epsilon) ||
        !std::isfinite(group.threshold) || !std::isfinite(group.smoothBorder) ||
        group.smoothBorder < 0.0f || group.smoothBorder > 1.0f ||
        !std::isfinite(group.visibilityNearRadius) ||
        !std::isfinite(group.visibilityRadius) ||
        group.visibilityFlag < 0 || group.visibilityFlag > 2) {
      diagnostic = "Unsupported or invalid SoShadowGroup parameters/ancestry";
      return false;
    }
    SbBox3f groupBounds;
    if (!coin_render_shadow_group_bounds(frame, static_cast<uint32_t>(g + 1),
                                         groupBounds, diagnostic)) return false;
    uint32_t mapSize = 1;
    const uint32_t requested = static_cast<uint32_t>(std::ceil(group.precision * 2048.0f));
    while (mapSize < requested) mapSize <<= 1;
    for (size_t l = 0; l < frame.shadowLights.size(); ++l) {
      const auto & light = frame.shadowLights[l];
      if (light.groupSlot != g + 1 || !light.enabled || !light.shadowEligible) continue;
      if (!std::isfinite(light.maxShadowDistance) ||
          (light.hasCustomScene && !light.mapSceneCaptured) ||
          (light.type != CoinRenderLightType::DIRECTIONAL &&
           light.type != CoinRenderLightType::SPOT)) {
        diagnostic = "Shadow light requires a supported spot/directional scene";
        return false;
      }
      SbViewVolume mainView;
      const SbViewVolume * mainViewPtr = nullptr;
      if (light.type == CoinRenderLightType::DIRECTIONAL) {
        if (!group.hasEntryCamera) {
          diagnostic = "Directional shadow requires the group-entry camera";
          return false;
        }
        if (!coin_render_shadow_view_volume(group.entryCamera,
                                            mainView, diagnostic)) return false;
        mainViewPtr = &mainView;
      }
      CoinRenderShadowPass pass;
      pass.groupSlot = static_cast<uint32_t>(g + 1);
      pass.lightSlot = static_cast<uint32_t>(l);
      pass.mapSize = mapSize;
      pass.epsilon = group.epsilon;
      pass.threshold = group.threshold;
      pass.maxShadowDistance = light.maxShadowDistance;
      // Coin uses perpixelspot for every shadow-map light, including directional.
      pass.perFragmentLighting = group.quality > 0.3;
      pass.lightingIndexByState.assign(frame.renderStates.size(), -1);
      pass.resolvedLightByState.resize(frame.renderStates.size());
      for (size_t s = 0; s < frame.renderStates.size(); ++s) {
        const auto & state = frame.renderStates[s];
        if (state.shadowGroupSlot != pass.groupSlot) continue;
        CoinRenderLightSourceSnapshot source;
        source.sourceRevision = light.sourceRevision;
        source.sourceModel = light.modelViewAtLight;
        source.type = light.type;
        source.intensity = light.intensity;
        source.cutOffAngle = light.cutOffAngle;
        source.dropOffRate = light.dropOffRate;
        for (int c = 0; c < 3; ++c) {
          source.color[c] = light.color[c];
          source.attenuation[c] = light.attenuation[c];
        }
        const SbMatrix modelView = light.model * state.view;
        SbVec3f direction;
        modelView.multDirMatrix(light.direction, direction);
        if (!coin_render_shadow_finite(direction) || direction.normalize() == 0.0f) {
          diagnostic = "Shadow light has an invalid view-space direction";
          return false;
        }
        for (int c = 0; c < 3; ++c) source.direction[c] = direction[c];
        if (light.type == CoinRenderLightType::SPOT) {
          SbVec3f position;
          modelView.multVecMatrix(light.position, position);
          if (!coin_render_shadow_finite(position)) {
            diagnostic = "Shadow light has an invalid view-space position";
            return false;
          }
          for (int c = 0; c < 3; ++c) source.position[c] = position[c];
        }
        pass.resolvedLightByState[s] = source;
        if (state.lightingSlot >= frame.lightingStates.size()) continue;
        const auto & sources = frame.lightingStates[state.lightingSlot].lights;
        for (size_t i = 0; i < sources.size(); ++i)
          if (sources[i].sourceRevision == light.sourceRevision &&
              sources[i].sourceModel == light.modelViewAtLight) {
            pass.lightingIndexByState[s] = static_cast<int32_t>(i);
            break;
          }
      }
      for (size_t d = 0; d < frame.draws.size(); ++d) {
        const auto & draw = frame.draws[d];
        if (draw.renderStateSlot >= frame.renderStates.size()) {
          diagnostic = "Shadow draw references an invalid render state";
          return false;
        }
        const auto & state = frame.renderStates[draw.renderStateSlot];
        if (draw.renderLayer != 0 || state.shadowGroupSlot != pass.groupSlot) continue;
        const bool casterScene = light.mapSceneCaptured ? draw.shadowLightSlot == l + 1 : draw.shadowLightSlot == 0;
        if (casterScene && (state.shadowStyle & 1u) != 0 &&
            !coin_render_shadow_transparent_shape(frame, state))
          pass.casterDraws.push_back(static_cast<uint32_t>(d));
        if (!draw.shadowLightSlot && (state.shadowStyle & 2u) != 0)
          pass.receiverDraws.push_back(static_cast<uint32_t>(d));
      }
      if (!coin_render_shadow_camera(group, light, groupBounds,
                                     mainViewPtr, pass, diagnostic, stablePbr)) return false;
      const uint64_t pixels = uint64_t(mapSize) * mapSize;
      const uint64_t moments = pixels * 16u; // RGBA32F.
      const uint64_t depth = pixels * 4u; // D32F used by both portable executors.
      const uint64_t bytes = moments + depth;
      if (bytes > budget || candidate.attachmentBytes > budget - bytes) {
        diagnostic = "Shadow moments and depth attachments exceed the configured byte budget";
        return false;
      }
      candidate.momentsBytes += moments;
      candidate.depthBytes += depth;
      candidate.attachmentBytes += bytes;
      candidate.passes.push_back(std::move(pass));
    }
  }
  for (const auto & light : frame.shadowLights)
    if (light.groupSlot == 0 || light.groupSlot > frame.shadowGroups.size()) {
      diagnostic = "Shadow light references an invalid group";
      return false;
    }
  output = std::move(candidate);
  diagnostic.clear();
  return true;
}

// Qualified object-composition profile over captured Coin state. It checks the scene once,
// then matches each ordinary light contribution to exactly one shadow pass.
// Backends consume the resolved pass order without rediscovering Coin nodes.
// A conservative coverage/tap budget, in physical viewport pixels, includes
// receiver triangle overdraw. It is admission, not a frame-time/FPS promise.
inline bool coin_render_shadow_filter_admission(const CoinRenderFramePlan & frame,
  const CoinRenderShadowPlan & plan,const CoinRenderOptions & options,
  const SbVec2i32 & targetSize,uint64_t & samples,std::string & diagnostic)
{
  const uint64_t taps=options.pbrShadowFilter==COIN_RENDER_PBR_SHADOW_PCSS?32:
    options.pbrShadowFilter==COIN_RENDER_PBR_SHADOW_PCF?9:1;
  uint64_t total=0;
  for(const auto & pass:plan.passes) {
    const auto & light=frame.shadowLights[pass.lightSlot];
    if(options.pbrShadowFilter==COIN_RENDER_PBR_SHADOW_PCSS) {
      const double extentFactor=std::abs(double(pass.projectionCoin[0][0]))*0.5;
      const double radius=light.type==CoinRenderLightType::SPOT
        ? light.sourceRadius*extentFactor*(pass.farDistance-pass.nearDistance)/(pass.nearDistance*pass.farDistance)
        : std::tan(light.angularRadius)*extentFactor*(pass.farDistance-pass.nearDistance);
      if(!std::isfinite(radius)||radius*pass.mapSize>options.pbrShadowMaxPcssRadiusTexels) {
        diagnostic="Physical PCSS emitter exceeds the configured texel radius cap; emitter and map are not clamped";return false;
      }
    }
    for(uint32_t slot:pass.receiverDraws) {
      const auto & draw=frame.draws[slot];const auto & state=frame.renderStates[draw.renderStateSlot];
      if(draw.renderLayer||draw.shadowLightSlot||frame.materials[state.materialSlot].model!=CoinRenderMaterialModel::PBR||
         pass.lightingIndexByState[draw.renderStateSlot]<0)continue;
      const auto & viewport=frame.viewports[state.viewportSlot];
      const double vleft=std::max(0,viewport.x),vbottom=std::max(0,viewport.y);
      const double vright=std::min(int64_t(targetSize[0]),int64_t(viewport.x)+viewport.width);
      const double vtop=std::min(int64_t(targetSize[1]),int64_t(viewport.y)+viewport.height);
      if(vright<=vleft||vtop<=vbottom)continue;
      const auto mvp=state.model*state.view*state.projectionCoin;
      const uint64_t end=uint64_t(draw.geometry.firstIndex)+draw.geometry.indexCount;
      for(uint64_t i=draw.geometry.firstIndex;i+2<end;i+=3) {
        double left=INFINITY,right=-INFINITY,bottom=INFINITY,top=-INFINITY;bool uncertain=false;
        for(unsigned j=0;j<3;++j) {
          const auto & vertex=frame.vertices[frame.indices[size_t(i+j)]];
          SbVec4f clip;mvp.multVecMatrix(SbVec4f(vertex.position[0],vertex.position[1],vertex.position[2],1),clip);
          if(clip[3]<=0||!std::isfinite(clip[3])){uncertain=true;break;}
          const double x=double(clip[0])/clip[3],y=double(clip[1])/clip[3];
          if(!std::isfinite(x)||!std::isfinite(y)){uncertain=true;break;}
          left=std::min(left,x);right=std::max(right,x);bottom=std::min(bottom,y);top=std::max(top,y);
        }
        double x0=vleft,x1=vright,y0=vbottom,y1=vtop;
        if(!uncertain) {
          x0=std::max(vleft,std::floor(viewport.x+(left+1)*.5*viewport.width)-1);
          x1=std::min(vright,std::ceil(viewport.x+(right+1)*.5*viewport.width)+1);
          y0=std::max(vbottom,std::floor(viewport.y+(bottom+1)*.5*viewport.height)-1);
          y1=std::min(vtop,std::ceil(viewport.y+(top+1)*.5*viewport.height)+1);
        }
        const uint64_t pixels=uint64_t(std::max(0.0,x1-x0))*uint64_t(std::max(0.0,y1-y0));
        if(pixels>options.pbrShadowSampleBudget/taps||total>options.pbrShadowSampleBudget-pixels*taps) {
          diagnostic="PBR shadow triangle coverage/tap bound exceeds the sample budget";return false;
        }
        total+=pixels*taps;
      }
    }
  }
  samples=total;return true;
}

inline bool
coin_render_shadow_object_profile(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadows,
  size_t lightCount, std::string & diagnostic, bool pbr = false)
{
  if (frame.shadowGroups.empty() || shadows.passes.size() != lightCount ||
      (lightCount < 1 || lightCount > 8)) {
    diagnostic = "Shadow object profile requires groups with one to eight passes";
    return false;
  }
  for (size_t p = 0; p < lightCount; ++p) {
    const auto & pass = shadows.passes[p];
    if (pass.groupSlot == 0 || pass.groupSlot > frame.shadowGroups.size() ||
        pass.lightSlot >= frame.shadowLights.size() ||
        frame.shadowLights[pass.lightSlot].groupSlot != pass.groupSlot ||
        !frame.shadowLights[pass.lightSlot].shadowEligible ||
        !pass.visible ||
        pass.epsilon < 0.0f ||
        pass.threshold < 0.0f || pass.threshold >= 1.0f ||
        std::any_of(shadows.passes.begin(), shadows.passes.begin() + p,
                    [&](const CoinRenderShadowPass & previous) {
                      return previous.lightSlot == pass.lightSlot;
                    })) {
      diagnostic = "Shadow object profile requires visible independent passes with receivers";
      return false;
    }
  }
  for (const auto & draw : frame.draws) {
    if (draw.renderStateSlot >= frame.renderStates.size()) {
      diagnostic = "Shadow profile draw references an invalid state";
      return false;
    }
    const auto & state = frame.renderStates[draw.renderStateSlot];
    // CAD technical layers are composed after the HDR output transform. The
    // PBR admission validates their BASE_COLOR contract separately.
    if (pbr && draw.renderLayer != 0) continue;
    if (state.shadowGroupSlot > frame.shadowGroups.size() ||
        draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST ||
        (!draw.shadowLightSlot && state.lightModel != CoinRenderLightModel::PHONG) ||
        state.fogMode != CoinRenderFogMode::NONE ||
        state.materialSlot >= frame.materials.size() ||
        state.lightingSlot >= frame.lightingStates.size() ||
        std::any_of(std::begin(state.extraTextures), std::end(state.extraTextures),
                    [](const CoinRenderTextureUnitSnapshot & unit) {
                      return unit.enabled;
                    })) {
      diagnostic = "Shadow object profile supports PHONG triangles without extra texture units";
      return false;
    }
    if (state.hasTexture) {
      if (shadows.passes.size() > 4 &&
          (state.textureModel != CoinRenderTextureModel::MODULATE ||
           state.textureCombines[0].instructions[0][0] != 0.0f)) {
        diagnostic = "Five to eight shadow maps require linear MODULATE texture";
        return false;
      }
      if (state.textureImageSlot >= frame.textures.size() ||
          state.samplerSlot >= frame.samplers.size()) {
        diagnostic = "Shadow receiver references an invalid texture";
        return false;
      }
      const auto & image = frame.textures[state.textureImageSlot];
      const uint64_t pixelCount = uint64_t(image.width) * image.height;
      const bool sceneTexture = image.producerId || image.gpuToken;
      if (!pixelCount || pixelCount > SIZE_MAX / 4 ||
          (sceneTexture
            ? (!coin_render_scene_texture_policy_supported(image.sceneTransparencyFunction) ||
               (image.gpuToken && !image.pixelsRgba.empty()))
            : image.pixelsRgba.size() != static_cast<size_t>(pixelCount * 4))) {
        diagnostic = "Shadow object profile requires complete static pixels or a supported SceneTexture2 alpha policy";
        return false;
      }
      {
        const bool hasAlpha = coin_render_texture_has_transparency(image);
        if (hasAlpha && (state.textureModel != CoinRenderTextureModel::MODULATE ||
            state.textureCombines[0].instructions[0][0] != 0.0f)) {
          diagnostic = "Alpha shadow texture requires linear MODULATE composition";
          return false;
        }
      }
    }
    float clipEquations[COIN_RENDER_MAX_CLIP_PLANES][4] = {};
    if (!coin_render_clip_equations(state, clipEquations, diagnostic)) return false;
    const auto & material = frame.materials[state.materialSlot];
    if (!std::isfinite(material.transparency) || material.transparency < 0.0f ||
        material.transparency > 1.0f ||
        std::abs(material.diffuse[3] - (1.0f - material.transparency)) > 1e-6f) {
      diagnostic = "Shadow material requires consistent captured alpha/transparency";
      return false;
    }
    if (!draw.shadowLightSlot && coin_render_shadow_transparent_shape(frame, state) &&
        (state.transparencyType < 0 || state.transparencyType > 10 ||
         (frame.transparency.mode < COIN_RENDER_TRANSPARENCY_COIN ||
          frame.transparency.mode > COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT))) {
      diagnostic = "Transparent shadow receivers require a known Coin mode and at most eight maps";
      return false;
    }
    if (draw.shadowLightSlot) continue;
    // Coin moves shadow-map lighting to vertices at quality <= 0.3.
    // Both stages now execute the captured contribution, including specular
    // and smooth normals. Keep the geometry validation independent of quality.
    const bool lowQuality = std::any_of(shadows.passes.begin(), shadows.passes.end(),
      [&](const CoinRenderShadowPass & pass) {
        return pass.groupSlot == state.shadowGroupSlot && !pass.perFragmentLighting;
      });
    const auto & lights = frame.lightingStates[state.lightingSlot].lights;
    const bool externalOrdinary = state.shadowGroupSlot != 0 &&
      std::any_of(lights.begin(), lights.end(),
      [&](const CoinRenderLightSourceSnapshot & source) {
        if (source.type != CoinRenderLightType::DIRECTIONAL &&
            source.type != CoinRenderLightType::POINT &&
            source.type != CoinRenderLightType::SPOT) return false;
        return std::none_of(frame.shadowLights.begin(), frame.shadowLights.end(),
          [&](const CoinRenderShadowLightSnapshot & light) {
            return light.groupSlot == state.shadowGroupSlot &&
              light.sourceRevision == source.sourceRevision &&
              light.modelViewAtLight == source.sourceModel;
          });
      });
    if (!pbr && (lowQuality || externalOrdinary)) {
      if (draw.geometry.indexCount % 3 != 0) {
        diagnostic = "Vertex-lit shadow profile requires triangles";
        return false;
      }
      const uint64_t end = uint64_t(draw.geometry.firstIndex) + draw.geometry.indexCount;
      if (end > frame.indices.size()) {
        diagnostic = "Vertex-lit shadow draw has an invalid index range";
        return false;
      }
      for (uint64_t i = draw.geometry.firstIndex; i < end; i += 3) {
        const uint32_t a = frame.indices[static_cast<size_t>(i)];
        const uint32_t b = frame.indices[static_cast<size_t>(i + 1)];
        const uint32_t c = frame.indices[static_cast<size_t>(i + 2)];
        if (a >= frame.vertices.size() || b >= frame.vertices.size() ||
            c >= frame.vertices.size()) {
          diagnostic = "Vertex-lit shadow draw has an invalid vertex index";
          return false;
        }
      }
    }
    // An inactive sibling is ordinary Coin geometry: it receives no shadow
    // pass, while its captured lights remain in the regular lighting state.
    if (state.shadowGroupSlot == 0) {
      if (lights.size() > COIN_RENDER_MAX_LIGHTS) {
        diagnostic = "Unshadowed sibling exceeds the eight-light receiver limit";
        return false;
      }
      continue;
    }
    if (lights.size() > COIN_RENDER_MAX_LIGHTS) {
      diagnostic = "Shadow object profile exceeds the qualified light combination";
      return false;
    }
    std::vector<bool> claimed(lights.size(), false);
    for (const auto & pass : shadows.passes) {
      if (pass.groupSlot != state.shadowGroupSlot) continue;
      if (draw.renderStateSlot >= pass.lightingIndexByState.size()) {
        diagnostic = "Shadow pass lacks a draw lighting index";
        return false;
      }
      const int32_t index = pass.lightingIndexByState[draw.renderStateSlot];
      const auto & shadowLight = frame.shadowLights[pass.lightSlot];
      if (index == -1) {
        // A later light cannot already be present in this draw's Coin state.
        for (const auto & source : lights)
          if (source.sourceRevision == shadowLight.sourceRevision &&
              source.sourceModel == shadowLight.modelViewAtLight) {
            diagnostic = "Shadow pass omits a light present at the draw";
            return false;
          }
        continue;
      }
      if (index < 0 || static_cast<size_t>(index) >= lights.size() ||
          claimed[static_cast<size_t>(index)]) {
        diagnostic = "Shadow pass lighting index is outside the captured state";
        return false;
      }
      const auto & source = lights[static_cast<size_t>(index)];
      if (source.sourceRevision != shadowLight.sourceRevision ||
          source.sourceModel != shadowLight.modelViewAtLight) {
        diagnostic = "Shadow pass does not match its captured Coin light";
        return false;
      }
      claimed[static_cast<size_t>(index)] = true;
    }
    for (size_t i = 0; i < claimed.size(); ++i) {
      if (claimed[i]) continue;
      const auto & source = lights[i];
      const bool ordinary =
        (source.type == CoinRenderLightType::DIRECTIONAL ||
         source.type == CoinRenderLightType::POINT ||
         source.type == CoinRenderLightType::SPOT) &&
        std::none_of(frame.shadowLights.begin(), frame.shadowLights.end(),
          [&](const CoinRenderShadowLightSnapshot & light) {
            return light.groupSlot == state.shadowGroupSlot &&
              light.shadowEligible &&
              light.sourceRevision == source.sourceRevision &&
              light.modelViewAtLight == source.sourceModel;
          });
      if (!ordinary) {
        diagnostic = "Shadow object profile has an unmatched Coin light";
        return false;
      }
    }
  }
  diagnostic.clear();
  return true;
}

// Coin's shadow shader is assembled from lights present at group entry.
// An ordinary light encountered inside that active group is captured by the
// normal traversal, but cannot contribute to the already assembled shader.
inline bool
coin_render_shadow_suppresses_ordinary_light(
  const CoinRenderFramePlan & frame, const CoinRenderRenderStateSnapshot & state,
  const CoinRenderLightSourceSnapshot & source)
{
  if (state.shadowGroupSlot == 0) return false;
  return std::any_of(frame.shadowLights.begin(), frame.shadowLights.end(),
    [&](const CoinRenderShadowLightSnapshot & light) {
      return light.groupSlot == state.shadowGroupSlot && light.enabled &&
        !light.shadowEligible && light.sourceRevision == source.sourceRevision &&
        light.modelViewAtLight == source.sourceModel;
    });
}

inline bool
coin_render_shadow_single_object_profile(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadows,
  CoinRenderLightType lightType, std::string & diagnostic)
{
  if (shadows.passes.size() != 1 ||
      shadows.passes[0].lightSlot >= frame.shadowLights.size() ||
      frame.shadowLights[shadows.passes[0].lightSlot].type != lightType) {
    diagnostic = "Single-light shadow profile requires the requested light type";
    return false;
  }
  return coin_render_shadow_object_profile(frame, shadows, 1, diagnostic);
}

inline bool
coin_render_shadow_single_spot_object_profile(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadows,
  std::string & diagnostic)
{
  return coin_render_shadow_single_object_profile(
    frame, shadows, CoinRenderLightType::SPOT, diagnostic);
}

inline bool
coin_render_shadow_single_directional_object_profile(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadows,
  std::string & diagnostic)
{
  return coin_render_shadow_single_object_profile(
    frame, shadows, CoinRenderLightType::DIRECTIONAL, diagnostic);
}

// The bounded two-pass opaque profile accepts any pair of eligible spot and/or
// directional lights. Core already owns the Coin light identity and index match.
inline bool
coin_render_shadow_two_object_profile(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadows,
  std::string & diagnostic)
{
  return coin_render_shadow_object_profile(frame, shadows, 2, diagnostic);
}

// Stronger fixture predicate for scenes with one spot and one directional.
inline bool
coin_render_shadow_spot_directional_object_profile(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadows,
  std::string & diagnostic)
{
  if (shadows.passes.size() != 2 ||
      shadows.passes[0].lightSlot >= frame.shadowLights.size() ||
      shadows.passes[1].lightSlot >= frame.shadowLights.size()) {
    diagnostic = "Spot/directional shadow profile requires two valid passes";
    return false;
  }
  const auto & first = frame.shadowLights[shadows.passes[0].lightSlot];
  const auto & second = frame.shadowLights[shadows.passes[1].lightSlot];
  if (!((first.type == CoinRenderLightType::SPOT &&
         second.type == CoinRenderLightType::DIRECTIONAL) ||
        (first.type == CoinRenderLightType::DIRECTIONAL &&
         second.type == CoinRenderLightType::SPOT))) {
    diagnostic = "Two-light shadow profile requires one spot and one directional light";
    return false;
  }
  return coin_render_shadow_object_profile(frame, shadows, 2, diagnostic);
}

#endif // COIN_RENDER_SHADOW_CORE_H
