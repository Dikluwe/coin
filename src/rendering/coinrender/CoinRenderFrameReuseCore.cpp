#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinrender/CoinRenderStateCore.h"

#include <cstring>
#include <cmath>
#include <cassert>
#include <utility>

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
    if (x.width != y.width || x.height != y.height ||
        x.components != y.components || x.contentDigest != y.contentDigest ||
        x.gpuToken != y.gpuToken || x.gpuOpaque != y.gpuOpaque ||
        x.pixelsRgba != y.pixelsRgba) return false;
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



bool
hasOpaqueConnectorResource(const CoinRenderFramePlan & plan)
{
  for (const CoinRenderTextureImageSnapshot & texture : plan.textures) {
    if (texture.gpuToken != 0) return true;
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
CoinRenderFrameReuseCore::beginCameraOverlay(
  CoinRenderFramePlan & plan, const CoinRenderCameraSnapshot & camera, uint64_t revision,
  CoinRenderCameraOverlayUndo & undo)
{
  if (undo.active || plan.revision == 0 || revision == 0 ||
      revision == plan.revision || plan.cameras.size() != 1 ||
      plan.renderStates.empty() || hasOpaqueConnectorResource(plan) ||
      !std::isfinite(camera.nearDistance) ||
      !std::isfinite(camera.farDistance) ||
      camera.farDistance <= camera.nearDistance ||
      !std::isfinite(camera.focalDistance) ||
      !std::isfinite(camera.aspectRatio) || camera.aspectRatio <= 0.0f) {
    return false;
  }
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      if (!std::isfinite(camera.viewMatrix[row][col]) ||
          !std::isfinite(camera.projectionMatrixCoin[row][col])) return false;
    }
  }
  const CoinRenderCameraSnapshot & oldCamera = plan.cameras[0];
  for (const CoinRenderRenderStateSnapshot & rs : plan.renderStates) {
    if (rs.cameraSlot != 0 || rs.lightModel != CoinRenderLightModel::BASE_COLOR ||
        rs.fogMode != CoinRenderFogMode::NONE || rs.polygonOffsetPrimitiveStyle != 1 ||
        !sameMatrix(rs.view, oldCamera.viewMatrix) ||
        !sameMatrix(rs.projectionCoin, oldCamera.projectionMatrixCoin)) return false;
  }

  CoinRenderCameraOverlayUndo prepared;
  prepared.revision = plan.revision;
  prepared.camera = oldCamera;
  prepared.states.reserve(plan.renderStates.size());
  for (const CoinRenderRenderStateSnapshot & rs : plan.renderStates) {
    CoinRenderCameraStateUndo state;
    state.view = rs.view;
    state.projectionCoin = rs.projectionCoin;
    state.fogEnd = rs.fogEnd;
    prepared.states.push_back(state);
  }
  prepared.active = true;
  undo = std::move(prepared);
  for (CoinRenderRenderStateSnapshot & rs : plan.renderStates) {
    rs.view = camera.viewMatrix;
    rs.projectionCoin = camera.projectionMatrixCoin;
    // Default visibility derives fogEnd from the camera even with no fog.
    if (rs.fogEnd == oldCamera.farDistance) rs.fogEnd = camera.farDistance;
  }
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
  for (size_t i = 0; i < undo.states.size(); ++i) {
    plan.renderStates[i].view = undo.states[i].view;
    plan.renderStates[i].projectionCoin = undo.states[i].projectionCoin;
    plan.renderStates[i].fogEnd = undo.states[i].fogEnd;
  }
  undo.active = false;
}
