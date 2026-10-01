#ifndef COIN_RENDER_SHADOW_CORE_H
#define COIN_RENDER_SHADOW_CORE_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderClipCore.h"
#include <Inventor/SbBox3f.h>
#include <Inventor/SbRotation.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <algorithm>
#include <cmath>
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
  // -1 means the shadow light is discovered after this shape and must be
  // added by Infra; otherwise it replaces that ordinary light contribution.
  std::vector<int32_t> lightingIndexByState;
  std::vector<CoinRenderLightSourceSnapshot> resolvedLightByState;
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

inline bool
coin_render_shadow_camera(const CoinRenderShadowGroupSnapshot & group,
                          const CoinRenderShadowLightSnapshot & light,
                          const SbBox3f & groupBounds,
                          const SbViewVolume * mainView,
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
      const float near = visibleVolume.getNearDist();
      const float depth = visibleVolume.getDepth();
      if (light.maxShadowDistance <= near) {
        pass.visible = false;
        return true;
      }
      const float narrowedDepth = std::min(light.maxShadowDistance - near, depth);
      visibleVolume = visibleVolume.zNarrow(
        1.0f, 1.0f - narrowedDepth / depth);
    }
    bounds = visibleVolume.intersectionBox(fullBounds);
    if (bounds.isEmpty()) {
      pass.visible = false;
      return true;
    }
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

// Pure Core planning over immutable captures. Resources and GPU passes belong
// to CoinBgfx/CoinWgpu; only explicitly qualified profiles execute.
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
      if (!std::isfinite(light.maxShadowDistance) ||
          (light.hasCustomScene && !light.customSceneDirectShape &&
           !light.customSceneDirectSubtree) ||
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
      pass.perFragmentLighting = group.quality > 0.3f;
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
      uint32_t customSceneDraws = 0;
      std::vector<uint32_t> customShapeDraws(light.customSceneShapeNodeIds.size(), 0);
      for (size_t d = 0; d < frame.draws.size(); ++d) {
        const auto & draw = frame.draws[d];
        if (draw.renderStateSlot >= frame.renderStates.size()) {
          diagnostic = "Shadow draw references an invalid render state";
          return false;
        }
        const auto & state = frame.renderStates[draw.renderStateSlot];
        if (state.shadowGroupSlot != pass.groupSlot) continue;
        const bool selectedShape = light.customSceneDirectShape &&
          draw.sourceNodeId == light.customSceneNodeId;
        const auto shapeIt = std::find(light.customSceneShapeNodeIds.begin(),
                                       light.customSceneShapeNodeIds.end(),
                                       draw.sourceNodeId);
        const bool selectedSubtree = light.customSceneDirectSubtree &&
          shapeIt != light.customSceneShapeNodeIds.end();
        if (selectedShape || selectedSubtree) {
          if (selectedSubtree)
            ++customShapeDraws[static_cast<size_t>(
              shapeIt - light.customSceneShapeNodeIds.begin())];
          ++customSceneDraws;
          if ((state.shadowStyle & 1u) == 0 ||
              (selectedShape &&
               (state.model != SbMatrix::identity() ||
                !state.clipPlanesWorld.empty() ||
                state.cullMode != CoinRenderCullMode::NONE ||
                state.frontFace != CoinRenderFrontFace::CCW))) {
            diagnostic = "shadowMapScene requires isolated casting geometry";
            return false;
          }
        }
        if ((state.shadowStyle & 1u) != 0 &&
            (!light.hasCustomScene || selectedShape || selectedSubtree))
          pass.casterDraws.push_back(static_cast<uint32_t>(d));
        if ((state.shadowStyle & 2u) != 0)
          pass.receiverDraws.push_back(static_cast<uint32_t>(d));
      }
      if (light.hasCustomScene &&
          (customSceneDraws != (light.customSceneDirectShape ? 1u :
            static_cast<uint32_t>(light.customSceneShapeNodeIds.size())) ||
           (light.customSceneDirectSubtree &&
            std::any_of(customShapeDraws.begin(), customShapeDraws.end(),
                        [](uint32_t count) { return count != 1; })))) {
        diagnostic = "shadowMapScene shapes must each occur exactly once in their group";
        return false;
      }
      if (!coin_render_shadow_camera(group, light, groupBounds,
                                     mainViewPtr, pass, diagnostic)) return false;
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

// Narrow opaque profile over captured Coin state. It checks the scene once,
// then matches each ordinary light contribution to exactly one shadow pass.
// Backends consume the resolved pass order without rediscovering Coin nodes.
inline bool
coin_render_shadow_opaque_profile(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadows,
  size_t lightCount, std::string & diagnostic)
{
  if (frame.shadowGroups.empty() || shadows.passes.size() != lightCount ||
      (lightCount < 1 || lightCount > 8)) {
    diagnostic = "Opaque shadow profile requires groups with one to eight passes";
    return false;
  }
  for (const auto & group : frame.shadowGroups)
    if (group.smoothBorder != 0.0f) {
      diagnostic = "Opaque shadow profile requires no smooth borders";
      return false;
    }
  for (size_t p = 0; p < lightCount; ++p) {
    const auto & pass = shadows.passes[p];
    if (pass.groupSlot == 0 || pass.groupSlot > frame.shadowGroups.size() ||
        pass.lightSlot >= frame.shadowLights.size() ||
        frame.shadowLights[pass.lightSlot].groupSlot != pass.groupSlot ||
        !frame.shadowLights[pass.lightSlot].shadowEligible ||
        !pass.visible || pass.casterDraws.empty() || pass.receiverDraws.empty() ||
        pass.epsilon < 0.0f ||
        pass.threshold < 0.0f || pass.threshold >= 1.0f ||
        std::any_of(shadows.passes.begin(), shadows.passes.begin() + p,
                    [&](const CoinRenderShadowPass & previous) {
                      return previous.lightSlot == pass.lightSlot;
                    })) {
      diagnostic = "Opaque shadow profile requires visible independent passes with casters and receivers";
      return false;
    }
    if (!pass.perFragmentLighting &&
        frame.shadowLights[pass.lightSlot].type != CoinRenderLightType::DIRECTIONAL) {
      diagnostic = "Opaque low-quality shadow profile requires directional lights";
      return false;
    }
  }
  for (const auto & draw : frame.draws) {
    if (draw.renderStateSlot >= frame.renderStates.size()) {
      diagnostic = "Shadow profile draw references an invalid state";
      return false;
    }
    const auto & state = frame.renderStates[draw.renderStateSlot];
    if (state.shadowGroupSlot > frame.shadowGroups.size() ||
        draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST ||
        draw.renderLayer != 0 || draw.clearDepthBefore ||
        state.lightModel != CoinRenderLightModel::PHONG ||
        state.screenDoorTransparency > 0.0f ||
        state.fogMode != CoinRenderFogMode::NONE ||
        state.materialSlot >= frame.materials.size() ||
        state.lightingSlot >= frame.lightingStates.size() ||
        std::any_of(std::begin(state.extraTextures), std::end(state.extraTextures),
                    [](const CoinRenderTextureUnitSnapshot & unit) {
                      return unit.enabled;
                    })) {
      diagnostic = "Opaque shadow profile supports PHONG triangles without extra texture units";
      return false;
    }
    if (state.hasTexture) {
      if (shadows.passes.size() > 4 &&
          (state.textureModel != CoinRenderTextureModel::MODULATE ||
           state.textureCombines[0].instructions[0][0] != 0.0f)) {
        diagnostic = "Extra shadow batch requires linear MODULATE texture";
        return false;
      }
      if (state.textureImageSlot >= frame.textures.size() ||
          state.samplerSlot >= frame.samplers.size()) {
        diagnostic = "Opaque shadow receiver references an invalid texture";
        return false;
      }
      const auto & image = frame.textures[state.textureImageSlot];
      const uint64_t pixelCount = uint64_t(image.width) * image.height;
      const bool sceneTexture = image.producerId || image.gpuToken;
      if (!pixelCount || pixelCount > SIZE_MAX / 4 ||
          (sceneTexture
            ? (!image.gpuOpaque ||
               image.sceneTransparencyFunction != SoSceneTexture2::NONE ||
               (image.gpuToken && !image.pixelsRgba.empty()))
            : image.pixelsRgba.size() != static_cast<size_t>(pixelCount * 4))) {
        diagnostic = "Opaque shadow profile requires alpha-one static pixels or an opaque SceneTexture2";
        return false;
      }
      if (!image.producerId && !image.gpuToken)
        for (size_t alpha = 3; alpha < image.pixelsRgba.size(); alpha += 4)
          if (image.pixelsRgba[alpha] != 255) {
            diagnostic = "Opaque shadow profile requires alpha-one texels";
            return false;
          }
    }
    float clipEquations[COIN_RENDER_MAX_CLIP_PLANES][4] = {};
    if (!coin_render_clip_equations(state, clipEquations, diagnostic)) return false;
    const auto & material = frame.materials[state.materialSlot];
    if (material.transparency != 0.0f || material.diffuse[3] != 1.0f) {
      diagnostic = "Opaque shadow profile does not support transparency";
      return false;
    }
    // Coin moves shadow-map directional lighting to vertices at quality <= 0.3.
    // Per-fragment execution is equivalent for flat diffuse triangles.
    const bool lowQuality = std::any_of(shadows.passes.begin(), shadows.passes.end(),
      [&](const CoinRenderShadowPass & pass) {
        return pass.groupSlot == state.shadowGroupSlot && !pass.perFragmentLighting;
      });
    const auto & lights = frame.lightingStates[state.lightingSlot].lights;
    const bool externalOrdinary = state.shadowGroupSlot != 0 &&
      std::any_of(lights.begin(), lights.end(),
      [&](const CoinRenderLightSourceSnapshot & source) {
        if (source.type != CoinRenderLightType::DIRECTIONAL &&
            source.type != CoinRenderLightType::POINT) return false;
        return std::none_of(frame.shadowLights.begin(), frame.shadowLights.end(),
          [&](const CoinRenderShadowLightSnapshot & light) {
            return light.groupSlot == state.shadowGroupSlot &&
              light.sourceRevision == source.sourceRevision &&
              light.modelViewAtLight == source.sourceModel;
          });
      });
    if (lowQuality || externalOrdinary) {
      if (material.specular[0] != 0.0f || material.specular[1] != 0.0f ||
          material.specular[2] != 0.0f || draw.geometry.indexCount % 3 != 0) {
        diagnostic = "Vertex-lit shadow profile requires flat diffuse triangles";
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
        for (int axis = 0; axis < 3; ++axis)
          if (std::abs(frame.vertices[a].normal[axis] - frame.vertices[b].normal[axis]) > 1e-5f ||
              std::abs(frame.vertices[a].normal[axis] - frame.vertices[c].normal[axis]) > 1e-5f) {
            diagnostic = "Vertex-lit shadow profile requires flat normals";
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
    if (lights.size() > COIN_RENDER_MAX_LIGHTS ||
        (externalOrdinary && shadows.passes.size() > 2)) {
      diagnostic = "Opaque shadow profile exceeds the qualified light combination";
      return false;
    }
    if (std::any_of(frame.shadowLights.begin(), frame.shadowLights.end(),
      [&](const CoinRenderShadowLightSnapshot & light) {
        return light.groupSlot == state.shadowGroupSlot && light.enabled &&
          !light.shadowEligible;
      })) {
      diagnostic = "Ordinary lights inside an active shadow group are not qualified";
      return false;
    }
    std::vector<bool> claimed(lights.size(), false);
    size_t lateLightCount = 0;
    for (const auto & pass : shadows.passes) {
      if (pass.groupSlot != state.shadowGroupSlot) continue;
      if (draw.renderStateSlot >= pass.lightingIndexByState.size()) {
        diagnostic = "Shadow pass lacks a draw lighting index";
        return false;
      }
      const int32_t index = pass.lightingIndexByState[draw.renderStateSlot];
      const auto & shadowLight = frame.shadowLights[pass.lightSlot];
      if (index == -1) {
        if (lights.size() + ++lateLightCount > COIN_RENDER_MAX_LIGHTS) {
          diagnostic = "Opaque shadow profile exceeds the eight-light receiver limit";
          return false;
        }
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
         source.type == CoinRenderLightType::POINT) &&
        std::none_of(frame.shadowLights.begin(), frame.shadowLights.end(),
          [&](const CoinRenderShadowLightSnapshot & light) {
            return light.groupSlot == state.shadowGroupSlot &&
              light.sourceRevision == source.sourceRevision &&
              light.modelViewAtLight == source.sourceModel;
          });
      if (!ordinary) {
        diagnostic = "Opaque shadow profile has an unmatched Coin light";
        return false;
      }
    }
  }
  diagnostic.clear();
  return true;
}

inline bool
coin_render_shadow_single_opaque_profile(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadows,
  CoinRenderLightType lightType, std::string & diagnostic)
{
  if (shadows.passes.size() != 1 ||
      shadows.passes[0].lightSlot >= frame.shadowLights.size() ||
      frame.shadowLights[shadows.passes[0].lightSlot].type != lightType) {
    diagnostic = "Single-light shadow profile requires the requested light type";
    return false;
  }
  return coin_render_shadow_opaque_profile(frame, shadows, 1, diagnostic);
}

inline bool
coin_render_shadow_single_spot_opaque_profile(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadows,
  std::string & diagnostic)
{
  return coin_render_shadow_single_opaque_profile(
    frame, shadows, CoinRenderLightType::SPOT, diagnostic);
}

inline bool
coin_render_shadow_single_directional_opaque_profile(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadows,
  std::string & diagnostic)
{
  return coin_render_shadow_single_opaque_profile(
    frame, shadows, CoinRenderLightType::DIRECTIONAL, diagnostic);
}

// The bounded two-pass opaque profile accepts any pair of eligible spot and/or
// directional lights. Core already owns the Coin light identity and index match.
inline bool
coin_render_shadow_two_opaque_profile(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadows,
  std::string & diagnostic)
{
  return coin_render_shadow_opaque_profile(frame, shadows, 2, diagnostic);
}

// Stronger fixture predicate for scenes with one spot and one directional.
inline bool
coin_render_shadow_spot_directional_opaque_profile(
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
  return coin_render_shadow_opaque_profile(frame, shadows, 2, diagnostic);
}

#endif // COIN_RENDER_SHADOW_CORE_H
