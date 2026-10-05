#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinrender/CoinRenderStateCore.h"
#include "rendering/coinrender/CoinRenderTransformCore.h"
#include "rendering/coinrender/CoinRenderPlanAssemblyCore.h"

#include <Inventor/actions/SoGLRenderAction.h>

#include <cstring>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <utility>
#include <unordered_set>

namespace {
template <typename T>
bool
samePlainSnapshots(const std::vector<T> & a, const std::vector<T> & b)
{
  return a.size() == b.size() &&
    (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}

bool
sameTextures(const std::vector<CoinRenderTextureImageSnapshot> & a,
             const std::vector<CoinRenderTextureImageSnapshot> & b)
{
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const CoinRenderTextureImageSnapshot & x = a[i];
    const CoinRenderTextureImageSnapshot & y = b[i];
    if (x.width != y.width || x.height != y.height || x.components != y.components ||
        x.contentDigest != y.contentDigest || x.producerId != y.producerId ||
        x.gpuToken != y.gpuToken || x.gpuOpaque != y.gpuOpaque ||
        x.sceneTransparencyFunction != y.sceneTransparencyFunction || x.pixelsRgba != y.pixelsRgba)
      return false;
  }
  return true;
}

bool
sameLighting(const std::vector<CoinRenderLightingSnapshot> & a,
             const std::vector<CoinRenderLightingSnapshot> & b)
{
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const CoinRenderLightingSnapshot & x = a[i];
    const CoinRenderLightingSnapshot & y = b[i];
    if (x.ambientIntensity != y.ambientIntensity ||
        std::memcmp(x.ambientColor, y.ambientColor, sizeof(x.ambientColor)) != 0 ||
        !samePlainSnapshots(x.lights, y.lights)) return false;
  }
  return true;
}

bool
sameColor(const SbColor4f & a, const SbColor4f & b)
{
  for (int i = 0; i < 4; ++i) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

bool
sameMatrix(const SbMatrix & a, const SbMatrix & b)
{
  return std::memcmp(a.getValue(), b.getValue(), sizeof(float) * 16) == 0;
}


bool finiteLight(const CoinRenderLightSourceSnapshot & light)
{
  if (!CoinRenderTransformCore::finiteMatrix(light.sourceModel)) return false;
  for (int i = 0; i < 3; ++i)
    if (!std::isfinite(light.position[i]) || !std::isfinite(light.direction[i])) return false;
  return light.type == CoinRenderLightType::DIRECTIONAL ||
    light.type == CoinRenderLightType::POINT || light.type == CoinRenderLightType::SPOT;
}

bool reusableLightCoordinates(const CoinRenderLightSourceSnapshot & light)
{
  if (!finiteLight(light)) return false;
  // Keep float eye/WORLD round trips in the same precision domain as the
  // reusable camera. Light source transforms may contain scale or shear.
  for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
    if (std::abs(light.sourceModel[row][column]) > 32768.0f) return false;
  for (int axis = 0; axis < 3; ++axis)
    if (std::abs(light.position[axis]) > 32768.0f) return false;
  return true;
}

bool transformLightFrame(CoinRenderLightSourceSnapshot & light, const SbMatrix & transform,
                         bool unchangedDirection = false)
{
  light.sourceModel = light.sourceModel * transform;
  if (light.type != CoinRenderLightType::DIRECTIONAL) {
    SbVec3f position;
    transform.multVecMatrix(SbVec3f(light.position), position);
    position.getValue(light.position[0], light.position[1], light.position[2]);
  }
  if (light.type != CoinRenderLightType::POINT && !unchangedDirection) {
    SbVec3f direction;
    transform.multDirMatrix(SbVec3f(light.direction), direction);
    direction.normalize();
    direction.getValue(light.direction[0], light.direction[1], light.direction[2]);
  }
  return finiteLight(light);
}



bool
hasOpaqueConnectorResource(const CoinRenderFramePlan & plan)
{
  for (const CoinRenderTextureImageSnapshot & texture : plan.textures) {
    if (texture.producerId != 0 || texture.gpuToken != 0)
      return true;
  }
  return false;
}

bool
sameCameraIndependentPayload(const CoinRenderFramePlan & previous,
                             const CoinRenderFramePlan & current)
{
  if (!sameColor(previous.clearColor, current.clearColor) ||
      !samePlainSnapshots(previous.vertices, current.vertices) ||
      previous.indices != current.indices ||
      !samePlainSnapshots(previous.materials, current.materials) ||
      !samePlainSnapshots(previous.shadowGroups, current.shadowGroups) ||
      !samePlainSnapshots(previous.shadowLights, current.shadowLights) ||
      !sameLighting(previous.lightingStates, current.lightingStates) ||
      !samePlainSnapshots(previous.viewports, current.viewports) ||
      !sameTextures(previous.textures, current.textures) ||
      !samePlainSnapshots(previous.samplers, current.samplers) ||
      !samePlainSnapshots(previous.draws, current.draws) ||
      previous.cameras.size() != current.cameras.size() ||
      previous.cameras.empty() ||
      previous.renderStates.size() != current.renderStates.size()) return false;

  for (size_t i = 0; i < previous.renderStates.size(); ++i) {
    const CoinRenderRenderStateSnapshot & a = previous.renderStates[i];
    const CoinRenderRenderStateSnapshot & b = current.renderStates[i];
    if (!coin_render_same_state_except_camera(a, b) ||
        a.cameraSlot >= previous.cameras.size() ||
        b.cameraSlot >= current.cameras.size()) return false;

    const CoinRenderCameraSnapshot & oldCamera = previous.cameras[a.cameraSlot];
    const CoinRenderCameraSnapshot & newCamera = current.cameras[b.cameraSlot];
    if (!sameMatrix(a.view, oldCamera.viewMatrix) ||
        !sameMatrix(a.projectionCoin, oldCamera.projectionMatrixCoin) ||
        !sameMatrix(b.view, newCamera.viewMatrix) ||
        !sameMatrix(b.projectionCoin, newCamera.projectionMatrixCoin)) return false;
  }
  return true;
}

bool
sameDrawStructure(const CoinRenderDrawPacket & a, const CoinRenderDrawPacket & b)
{
  return a.topology == b.topology &&
    a.geometry.firstVertex == b.geometry.firstVertex &&
    a.geometry.vertexCount == b.geometry.vertexCount &&
    a.geometry.firstIndex == b.geometry.firstIndex &&
    a.geometry.indexCount == b.geometry.indexCount &&
    a.renderStateSlot == b.renderStateSlot &&
    a.frameNodeOrdinal == b.frameNodeOrdinal &&
    a.sourceNodeId == b.sourceNodeId &&
    a.stableNodeId == b.stableNodeId &&
    a.drawOrdinal == b.drawOrdinal &&
    a.hasSortingCenter == b.hasSortingCenter &&
    (!a.hasSortingCenter || std::memcmp(a.sortingCenterWorld, b.sortingCenterWorld, sizeof(a.sortingCenterWorld)) == 0) &&
    a.renderLayer == b.renderLayer &&
    a.clearDepthBefore == b.clearDepthBefore;
}

bool
sameExecutionStructure(const CoinRenderFramePlan & previous, const CoinRenderFramePlan & current)
{
  if (previous.vertices.size() != current.vertices.size() ||
      previous.indices.size() != current.indices.size() ||
      previous.materials.size() != current.materials.size() ||
      !samePlainSnapshots(previous.shadowGroups, current.shadowGroups) ||
      !samePlainSnapshots(previous.shadowLights, current.shadowLights) ||
      previous.lightingStates.size() != current.lightingStates.size() ||
      previous.cameras.size() != current.cameras.size() ||
      previous.viewports.size() != current.viewports.size() ||
      previous.renderStates.size() != current.renderStates.size() ||
      previous.textures.size() != current.textures.size() ||
      previous.samplers.size() != current.samplers.size() ||
      previous.draws.size() != current.draws.size()) return false;
  for (size_t i = 0; i < previous.draws.size(); ++i) {
    if (!sameDrawStructure(previous.draws[i], current.draws[i])) return false;
  }
  return true;
}
}

CoinRenderFrameReuseDecision
CoinRenderFrameReuseCore::classify(const CoinRenderFramePlan & previous,
                               const CoinRenderFramePlan & current)
{
  if (!coin_render_same_transparency_options(previous.transparency, current.transparency))
    return CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD, previous.revision);
  if (previous.revision == 0 || current.revision == 0 ||
      hasOpaqueConnectorResource(previous) ||
      hasOpaqueConnectorResource(current)) {
    return CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::UNKNOWN, 0);
  }
  if (previous.hasSamePayload(current)) {
    return CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::REUSE,
                                    previous.revision);
  }
  if (sameCameraIndependentPayload(previous, current)) {
    return CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,
                                    previous.revision);
  }
  if (sameExecutionStructure(previous, current)) {
    return CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::RESOURCE_REBUILD,
                                    previous.revision);
  }
  return CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD,
                                  previous.revision);
}

bool
CoinRenderFrameReuseCore::cameraOverlay(const CoinRenderFramePlan & previous,
                                    const CoinRenderCameraSnapshot & camera,
                                    uint64_t revision,
                                    CoinRenderFramePlan & result)
{
  CoinRenderFramePlan candidate = previous;
  CoinRenderCameraOverlayUndo undo;
  if (!CoinRenderFrameReuseCore::beginCameraOverlay(
        candidate, camera, revision, undo)) return false;
  if (!candidate.isValid()) return false;
  result = std::move(candidate);
  return true;
}

bool
CoinRenderFrameReuseCore::prepareCameraOverlayBasis(
  const CoinRenderFramePlan & plan, CoinRenderCameraOverlayBasis & basis)
{
  basis = CoinRenderCameraOverlayBasis();
  if (plan.revision == 0 || plan.cameras.size() != 1 || plan.renderStates.empty() ||
      hasOpaqueConnectorResource(plan) || !plan.shadowGroups.empty() || !plan.shadowLights.empty() ||
      !CoinRenderTransformCore::cameraReuseView(plan.cameras[0].viewMatrix)) return false;
  const auto & camera = plan.cameras[0];
  // PHONG usually appears in the first state. Combining the qualification
  // checks avoids a second scan of the large render-state snapshots.
  const bool phong = std::any_of(plan.renderStates.begin(), plan.renderStates.end(),
    [](const CoinRenderRenderStateSnapshot & state) {
      return state.lightModel == CoinRenderLightModel::PHONG;
    });
  for (const auto & state : plan.renderStates) {
    if (state.cameraSlot != 0 || state.fogMode != CoinRenderFogMode::NONE ||
        state.polygonOffsetPrimitiveStyle != 1 ||
        (state.lightModel != CoinRenderLightModel::BASE_COLOR && state.lightModel != CoinRenderLightModel::PHONG) ||
        !sameMatrix(state.view, camera.viewMatrix) ||
        !sameMatrix(state.projectionCoin, camera.projectionMatrixCoin)) return false;
    // The PHONG extension qualifies an opaque surface only. Keep the older
    // BASE_COLOR overlay's admission unchanged, including mixed-state plans.
    if (phong) {
      if (state.materialSlot >= plan.materials.size() || state.lightingSlot >= plan.lightingStates.size() ||
          state.transparentMaterial || state.transparentTexture || state.hasTexture ||
          state.screenDoorTransparency > 0.0f || !state.clipPlanesWorld.empty() ||
          state.shadowGroupSlot || state.polygonOffsetEnabled || state.polygonLinePattern) return false;
      if (state.transparencyType != SoGLRenderAction::SCREEN_DOOR &&
          state.transparencyType != SoGLRenderAction::BLEND &&
          state.transparencyType != SoGLRenderAction::DELAYED_BLEND &&
          state.transparencyType != SoGLRenderAction::SORTED_OBJECT_BLEND &&
          state.transparencyType != SoGLRenderAction::NONE &&
          state.transparencyType != SoGLRenderAction::SORTED_LAYERS_BLEND) return false;
      const auto & material = plan.materials[state.materialSlot];
      if (material.transparency != 0.0f || material.diffuse[3] < 1.0f) return false;
      for (const auto & texture : state.extraTextures) if (texture.enabled) return false;
    }
  }
  if (phong) {
    for (const auto & draw : plan.draws)
      if (draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST ||
          draw.renderLayer || draw.clearDepthBefore || draw.shadowLightSlot || draw.lineStripId) return false;
  }
  CoinRenderCameraOverlayBasis prepared;
  prepared.owner = &plan;
  prepared.revision = plan.revision;
  prepared.stateCount = plan.renderStates.size();
  prepared.camera = camera;
  prepared.referenceLighting = plan.lightingStates;
  prepared.worldLighting = plan.lightingStates;
  const SbMatrix viewInverse = camera.viewMatrix.inverse();
  for (auto & lighting : prepared.worldLighting)
    for (auto & light : lighting.lights)
      if (!reusableLightCoordinates(light) || !transformLightFrame(light, viewInverse) ||
          !reusableLightCoordinates(light)) return false;
  basis = std::move(prepared);
  return true;
}

bool
CoinRenderFrameReuseCore::beginCameraOverlay(
  CoinRenderFramePlan & plan, const CoinRenderCameraSnapshot & camera, uint64_t revision,
  CoinRenderCameraOverlayUndo & undo)
{
  CoinRenderCameraOverlayBasis basis;
  return prepareCameraOverlayBasis(plan, basis) &&
    beginCameraOverlay(plan, camera, revision, undo, basis);
}

bool
CoinRenderFrameReuseCore::beginCameraOverlay(
  CoinRenderFramePlan & plan, const CoinRenderCameraSnapshot & camera, uint64_t revision,
  CoinRenderCameraOverlayUndo & undo, const CoinRenderCameraOverlayBasis & basis)
{
  if (undo.active || basis.owner != &plan || basis.revision == 0 ||
      plan.revision == 0 || revision == 0 || revision == plan.revision ||
      plan.cameras.size() != 1 || plan.renderStates.size() != basis.stateCount ||
      plan.lightingStates.size() != basis.worldLighting.size() ||
      !std::isfinite(camera.nearDistance) || !std::isfinite(camera.farDistance) ||
      camera.farDistance <= camera.nearDistance || !std::isfinite(camera.focalDistance) ||
      !std::isfinite(camera.aspectRatio) || camera.aspectRatio <= 0.0f ||
      !CoinRenderTransformCore::cameraReuseView(basis.camera.viewMatrix) ||
      !CoinRenderTransformCore::cameraReuseView(camera.viewMatrix) ||
      !CoinRenderTransformCore::finiteMatrix(camera.projectionMatrixCoin)) return false;

  const auto oldCamera = plan.cameras[0];
  std::vector<CoinRenderLightingSnapshot> lighting = basis.referenceLighting;
  if (!sameMatrix(camera.viewMatrix, basis.camera.viewMatrix)) {
    const bool unchangedOrientation =
      std::memcmp(camera.viewMatrix.getValue(), basis.camera.viewMatrix.getValue(),
                  sizeof(float) * 12) == 0;
    SbMatrix delta, normalDelta;
    if (!CoinRenderTransformCore::cameraDelta(
          basis.camera.viewMatrix, camera.viewMatrix, delta, normalDelta)) return false;
    // A pan/dolly cannot change a directional/spot direction. Preserve the
    // captured bits and use the exact anchored translation for positions.
    // Rotation starts from WORLD values, never the previous overlay.
    if (!unchangedOrientation) lighting = basis.worldLighting;
    for (auto & snapshot : lighting)
      for (auto & light : snapshot.lights)
        if (!transformLightFrame(light, unchangedOrientation ? delta : camera.viewMatrix,
                                 unchangedOrientation) || !reusableLightCoordinates(light)) return false;
  }
  CoinRenderCameraOverlayUndo prepared;
  prepared.revision = plan.revision;
  prepared.camera = oldCamera;
  prepared.lighting = plan.lightingStates;
  prepared.states.reserve(plan.renderStates.size());
  for (const auto & rs : plan.renderStates) {
    CoinRenderCameraStateUndo state;
    state.view = rs.view;
    state.projectionCoin = rs.projectionCoin;
    state.fogEnd = rs.fogEnd;
    prepared.states.push_back(state);
  }
  prepared.active = true;
  undo = std::move(prepared);
  for (auto & rs : plan.renderStates) {
    rs.view = camera.viewMatrix;
    rs.projectionCoin = camera.projectionMatrixCoin;
    if (rs.fogEnd == oldCamera.farDistance) rs.fogEnd = camera.farDistance;
  }
  plan.lightingStates.swap(lighting);
  plan.cameras[0] = camera;
  plan.revision = revision;
  return true;
}

void
CoinRenderFrameReuseCore::rollbackCameraOverlay(
  CoinRenderFramePlan & plan, CoinRenderCameraOverlayUndo & undo)
{
  if (!undo.active) return;
  assert(plan.cameras.size() == 1 && plan.renderStates.size() == undo.states.size());
  plan.revision = undo.revision;
  plan.cameras[0] = undo.camera;
  plan.lightingStates.swap(undo.lighting);
  for (size_t i = 0; i < undo.states.size(); ++i) {
    plan.renderStates[i].view = undo.states[i].view;
    plan.renderStates[i].projectionCoin = undo.states[i].projectionCoin;
    plan.renderStates[i].fogEnd = undo.states[i].fogEnd;
  }
  undo.active = false;
}

namespace {
bool boundedAffineModel(const SbMatrix & matrix)
{
  if (!CoinRenderTransformCore::finiteMatrix(matrix) || matrix[0][3] != 0 ||
      matrix[1][3] != 0 || matrix[2][3] != 0 || matrix[3][3] != 1) return false;
  for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
    if (std::abs(matrix[row][column]) > 32768.0f) return false;
  return true;
}
}

bool CoinRenderFrameReuseCore::translatedModel(
  const SbMatrix & anchor, const SbMatrix & prefix, const SbVec3f & translation,
  SbMatrix & result)
{
  if (!boundedAffineModel(anchor) || !boundedAffineModel(prefix)) return false;
  for (int axis = 0; axis < 3; ++axis)
    if (!std::isfinite(translation[axis]) || std::abs(translation[axis]) > 32768.0f) return false;
  // The local transform has zero center. Its last row is the position field,
  // regardless of scale, rotation or reflection. Recompute that row through
  // the real upstream matrix, avoiding cancellation in anchor + delta.
  SbMatrix positioned;
  positioned.setTranslate(translation);
  positioned.multRight(prefix);
  SbMatrix candidate = anchor;
  for (int column = 0; column < 4; ++column) candidate[3][column] = positioned[3][column];
  if (!boundedAffineModel(candidate)) return false;
  result = candidate;
  return true;
}

bool CoinRenderFrameReuseCore::beginTranslationOverlay(
  CoinRenderFramePlan & plan, const std::vector<CoinRenderModelUpdate> & updates,
  uint64_t revision, CoinRenderTranslationOverlayUndo & undo)
{
  if (undo.active || !plan.revision || !revision || revision == plan.revision ||
      updates.empty() || updates.size() > 65536) return false;
  std::unordered_set<uint32_t> slots;
  CoinRenderTranslationOverlayUndo prepared;
  prepared.revision = plan.revision;
  prepared.states.reserve(updates.size());
  for (const auto & update : updates) {
    if (update.stateSlot >= plan.renderStates.size() ||
        !boundedAffineModel(update.model) || !slots.insert(update.stateSlot).second) return false;
    if (update.sortingDrawSlot != UINT32_MAX &&
        (update.sortingDrawSlot >= plan.draws.size() || !plan.draws[update.sortingDrawSlot].hasSortingCenter ||
         plan.draws[update.sortingDrawSlot].renderStateSlot != update.stateSlot)) return false;
    CoinRenderTranslationStateUndo previous;
    previous.state = update;
    previous.state.model = plan.renderStates[update.stateSlot].model;
    if (update.sortingDrawSlot != UINT32_MAX)
      std::memcpy(previous.sortingCenterWorld, plan.draws[update.sortingDrawSlot].sortingCenterWorld,
                  sizeof(previous.sortingCenterWorld));
    prepared.states.push_back(previous);
  }
  prepared.active = true;
  undo = std::move(prepared);
  for (const auto & update : updates) {
    plan.renderStates[update.stateSlot].model = update.model;
    if (update.sortingDrawSlot != UINT32_MAX)
      CoinRenderPlanAssemblyCore::sortingCenter(plan.draws[update.sortingDrawSlot], update.model, SbVec3f(0, 0, 0));
  }
  plan.revision = revision;
  return true;
}

void CoinRenderFrameReuseCore::rollbackTranslationOverlay(
  CoinRenderFramePlan & plan, CoinRenderTranslationOverlayUndo & undo)
{
  if (!undo.active) return;
  plan.revision = undo.revision;
  for (const auto & previous : undo.states) {
    plan.renderStates[previous.state.stateSlot].model = previous.state.model;
    if (previous.state.sortingDrawSlot != UINT32_MAX)
      std::memcpy(plan.draws[previous.state.sortingDrawSlot].sortingCenterWorld, previous.sortingCenterWorld,
                  sizeof(previous.sortingCenterWorld));
  }
  undo.active = false;
}
