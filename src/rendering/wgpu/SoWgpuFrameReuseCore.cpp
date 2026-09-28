#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuFrameReuseCore.h"

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
sameTextures(const std::vector<TextureImageSnapshot> & a,
             const std::vector<TextureImageSnapshot> & b)
{
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const TextureImageSnapshot & x = a[i];
    const TextureImageSnapshot & y = b[i];
    if (x.width != y.width || x.height != y.height ||
        x.components != y.components || x.contentDigest != y.contentDigest ||
        x.gpuToken != y.gpuToken || x.gpuOpaque != y.gpuOpaque ||
        x.pixelsRgba != y.pixelsRgba) return false;
  }
  return true;
}

bool
sameLighting(const std::vector<LightingSnapshot> & a,
             const std::vector<LightingSnapshot> & b)
{
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const LightingSnapshot & x = a[i];
    const LightingSnapshot & y = b[i];
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
sameRenderStateExceptCamera(const RenderStateSnapshot & a,
                            const RenderStateSnapshot & b)
{
  return sameMatrix(a.model, b.model) &&
    a.materialSlot == b.materialSlot && a.lightingSlot == b.lightingSlot &&
    a.cameraSlot == b.cameraSlot && a.viewportSlot == b.viewportSlot &&
    a.cullMode == b.cullMode && a.frontFace == b.frontFace &&
    a.depthTest == b.depthTest && a.depthWrite == b.depthWrite &&
    a.explicitDepthMask == b.explicitDepthMask &&
    a.screenDoorTransparency == b.screenDoorTransparency &&
    a.depthFunction == b.depthFunction &&
    std::memcmp(a.depthRange, b.depthRange, sizeof(a.depthRange)) == 0 &&
    a.polygonOffsetEnabled == b.polygonOffsetEnabled &&
    a.polygonOffsetFactor == b.polygonOffsetFactor &&
    a.polygonOffsetUnits == b.polygonOffsetUnits &&
    a.polygonOffsetStyles == b.polygonOffsetStyles &&
    a.polygonOffsetPrimitiveStyle == b.polygonOffsetPrimitiveStyle &&
    a.lightModel == b.lightModel && a.lineWidth == b.lineWidth &&
    a.pointSize == b.pointSize && a.linePattern == b.linePattern &&
    a.linePatternScaleFactor == b.linePatternScaleFactor &&
    sameMatrix(a.textureMatrix, b.textureMatrix) &&
    std::memcmp(a.extraTextures, b.extraTextures, sizeof(a.extraTextures)) == 0 &&
    a.hasTexture == b.hasTexture && a.textureImageSlot == b.textureImageSlot &&
    a.samplerSlot == b.samplerSlot && a.textureModel == b.textureModel &&
    std::memcmp(a.textureBlendColor, b.textureBlendColor,
                sizeof(a.textureBlendColor)) == 0 &&
    a.transparencyType == b.transparencyType && a.fogMode == b.fogMode &&
    std::memcmp(a.fogColor, b.fogColor, sizeof(a.fogColor)) == 0 &&
    a.fogStart == b.fogStart && a.fogEnd == b.fogEnd;
}

bool
hasOpaqueConnectorResource(const FramePlan & plan)
{
  for (const TextureImageSnapshot & texture : plan.textures) {
    if (texture.gpuToken != 0) return true;
  }
  return false;
}

bool
sameCameraIndependentPayload(const FramePlan & previous,
                             const FramePlan & current)
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
    const RenderStateSnapshot & a = previous.renderStates[i];
    const RenderStateSnapshot & b = current.renderStates[i];
    if (!sameRenderStateExceptCamera(a, b) ||
        a.cameraSlot >= previous.cameras.size() ||
        b.cameraSlot >= current.cameras.size()) return false;

    const CameraSnapshot & oldCamera = previous.cameras[a.cameraSlot];
    const CameraSnapshot & newCamera = current.cameras[b.cameraSlot];
    if (!sameMatrix(a.view, oldCamera.viewMatrix) ||
        !sameMatrix(a.projectionCoin, oldCamera.projectionMatrixCoin) ||
        !sameMatrix(b.view, newCamera.viewMatrix) ||
        !sameMatrix(b.projectionCoin, newCamera.projectionMatrixCoin)) return false;
  }
  return true;
}

bool
sameDrawStructure(const DrawPacket & a, const DrawPacket & b)
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
sameExecutionStructure(const FramePlan & previous, const FramePlan & current)
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

SoWgpuFrameReuseDecision
SoWgpuFrameReuseCore::classify(const FramePlan & previous,
                               const FramePlan & current)
{
  if (previous.revision == 0 || current.revision == 0 ||
      hasOpaqueConnectorResource(previous) ||
      hasOpaqueConnectorResource(current)) {
    return SoWgpuFrameReuseDecision(SoWgpuFrameReuseKind::UNKNOWN, 0);
  }
  if (previous.hasSamePayload(current)) {
    return SoWgpuFrameReuseDecision(SoWgpuFrameReuseKind::REUSE,
                                    previous.revision);
  }
  if (sameCameraIndependentPayload(previous, current)) {
    return SoWgpuFrameReuseDecision(SoWgpuFrameReuseKind::CAMERA_PATCH,
                                    previous.revision);
  }
  if (sameExecutionStructure(previous, current)) {
    return SoWgpuFrameReuseDecision(SoWgpuFrameReuseKind::RESOURCE_REBUILD,
                                    previous.revision);
  }
  return SoWgpuFrameReuseDecision(SoWgpuFrameReuseKind::FULL_REBUILD,
                                  previous.revision);
}

bool
SoWgpuFrameReuseCore::cameraOverlay(const FramePlan & previous,
                                    const CameraSnapshot & camera,
                                    uint64_t revision,
                                    FramePlan & result)
{
  FramePlan candidate = previous;
  SoWgpuCameraOverlayUndo undo;
  if (!SoWgpuFrameReuseCore::beginCameraOverlay(
        candidate, camera, revision, undo)) return false;
  if (!candidate.isValid()) return false;
  result = std::move(candidate);
  return true;
}

bool
SoWgpuFrameReuseCore::beginCameraOverlay(
  FramePlan & plan, const CameraSnapshot & camera, uint64_t revision,
  SoWgpuCameraOverlayUndo & undo)
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
  const CameraSnapshot & oldCamera = plan.cameras[0];
  for (const RenderStateSnapshot & rs : plan.renderStates) {
    if (rs.cameraSlot != 0 || rs.lightModel != LightModel::BASE_COLOR ||
        rs.fogMode != FogMode::NONE || rs.polygonOffsetPrimitiveStyle != 1 ||
        !sameMatrix(rs.view, oldCamera.viewMatrix) ||
        !sameMatrix(rs.projectionCoin, oldCamera.projectionMatrixCoin)) return false;
  }

  SoWgpuCameraOverlayUndo prepared;
  prepared.revision = plan.revision;
  prepared.camera = oldCamera;
  prepared.states.reserve(plan.renderStates.size());
  for (const RenderStateSnapshot & rs : plan.renderStates) {
    SoWgpuCameraStateUndo state;
    state.view = rs.view;
    state.projectionCoin = rs.projectionCoin;
    state.fogEnd = rs.fogEnd;
    prepared.states.push_back(state);
  }
  prepared.active = true;
  undo = std::move(prepared);
  for (RenderStateSnapshot & rs : plan.renderStates) {
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
SoWgpuFrameReuseCore::rollbackCameraOverlay(
  FramePlan & plan, SoWgpuCameraOverlayUndo & undo)
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
