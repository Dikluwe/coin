#ifndef COIN_RENDER_SHADOW_CORE_H
#define COIN_RENDER_SHADOW_CORE_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <Inventor/SbBox3f.h>
#include <Inventor/SbRotation.h>
#include <Inventor/SbViewVolume.h>
#include <algorithm>
#include <cmath>
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
  std::vector<uint32_t> casterDraws;
  std::vector<uint32_t> receiverDraws;
};

struct CoinRenderShadowPlan {
  std::vector<CoinRenderShadowPass> passes;
  uint64_t attachmentBytes = 0;
};

inline bool
coin_render_shadow_finite(const SbVec3f & value)
{
  return std::isfinite(value[0]) && std::isfinite(value[1]) &&
         std::isfinite(value[2]);
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
    if (state.shadowGroupSlot != groupSlot) continue;
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

inline bool
coin_render_shadow_camera(const CoinRenderShadowGroupSnapshot & group,
                          const CoinRenderShadowLightSnapshot & light,
                          const SbBox3f & groupBounds,
                          CoinRenderShadowPass & pass,
                          std::string & diagnostic)
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
    const SbVec3f size = bounds.getSize();
    const float extent = std::max(1.0f, std::max(size[0], std::max(size[1], size[2])));
    const SbVec3f position = bounds.getCenter() - direction * (extent * 2.0f);
    const SbBox3f cameraBounds = coin_render_shadow_camera_bounds(
      bounds, position, orientation);
    const SbVec3f & lo = cameraBounds.getMin();
    const SbVec3f & hi = cameraBounds.getMax();
    const float halfWidth = std::max(0.01f, (hi[0] - lo[0]) * 0.505f);
    const float halfHeight = std::max(0.01f, (hi[1] - lo[1]) * 0.505f);
    const float centerX = (lo[0] + hi[0]) * 0.5f;
    const float centerY = (lo[1] + hi[1]) * 0.5f;
    pass.nearDistance = std::max(0.001f, -hi[2] * 0.99f);
    pass.farDistance = -lo[2] * 1.01f;
    if (!std::isfinite(pass.farDistance) ||
        pass.farDistance <= pass.nearDistance) {
      diagnostic = "Shadow directional light has an invalid volume";
      return false;
    }
    volume.ortho(centerX - halfWidth, centerX + halfWidth,
                 centerY - halfHeight, centerY + halfHeight,
                 pass.nearDistance, pass.farDistance);
    volume.rotateCamera(orientation);
    volume.translateCamera(position);
  }
  volume.getMatrices(pass.view, pass.projectionCoin);
  return true;
}

// Pure Core planning over immutable captures. Resources and GPU passes belong
// to CoinBgfx/CoinWgpu. The current executors deliberately reject this plan.
inline bool
coin_render_plan_shadows(const CoinRenderFramePlan & frame,
                         CoinRenderShadowPlan & output, std::string & diagnostic)
{
  CoinRenderShadowPlan candidate;
  for (size_t g = 0; g < frame.shadowGroups.size(); ++g) {
    const auto & group = frame.shadowGroups[g];
    if (!group.sourceRevision || group.nested ||
        !std::isfinite(group.precision) || group.precision <= 0.0f ||
        group.precision > 1.0f || !std::isfinite(group.quality) ||
        !std::isfinite(group.intensity) || !std::isfinite(group.epsilon) ||
        !std::isfinite(group.threshold) || !std::isfinite(group.smoothBorder) ||
        !std::isfinite(group.visibilityNearRadius) ||
        !std::isfinite(group.visibilityRadius) ||
        group.visibilityFlag < 0 || group.visibilityFlag > 2) {
      diagnostic = "Unsupported or invalid SoShadowGroup parameters/nesting";
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
      if (light.hasCustomScene ||
          (light.type != CoinRenderLightType::DIRECTIONAL &&
           light.type != CoinRenderLightType::SPOT)) {
        diagnostic = "Shadow light requires a supported spot/directional scene";
        return false;
      }
      if (light.type == CoinRenderLightType::DIRECTIONAL &&
          light.maxShadowDistance > 0.0f) {
        diagnostic = "Directional maxShadowDistance requires view-frustum intersection";
        return false;
      }
      CoinRenderShadowPass pass;
      pass.groupSlot = static_cast<uint32_t>(g + 1);
      pass.lightSlot = static_cast<uint32_t>(l);
      pass.mapSize = mapSize;
      for (size_t d = 0; d < frame.draws.size(); ++d) {
        const auto & draw = frame.draws[d];
        if (draw.renderStateSlot >= frame.renderStates.size()) {
          diagnostic = "Shadow draw references an invalid render state";
          return false;
        }
        const auto & state = frame.renderStates[draw.renderStateSlot];
        if (state.shadowGroupSlot != pass.groupSlot) continue;
        if ((state.shadowStyle & 1u) != 0)
          pass.casterDraws.push_back(static_cast<uint32_t>(d));
        if ((state.shadowStyle & 2u) != 0)
          pass.receiverDraws.push_back(static_cast<uint32_t>(d));
      }
      if (!coin_render_shadow_camera(group, light, groupBounds,
                                      pass, diagnostic)) return false;
      const uint64_t bytes = uint64_t(mapSize) * mapSize * 16u; // RGBA32F moments.
      const uint64_t budget = uint64_t(128) * 1024 * 1024;
      if (bytes > budget || candidate.attachmentBytes > budget - bytes) {
        diagnostic = "Shadow maps exceed 128 MiB per apply";
        return false;
      }
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

#endif // COIN_RENDER_SHADOW_CORE_H
