#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuFrameReuseCore.h"

#include <cstring>

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
    a.lightModel == b.lightModel && a.lineWidth == b.lineWidth &&
    a.pointSize == b.pointSize && sameMatrix(a.textureMatrix, b.textureMatrix) &&
    a.hasTexture == b.hasTexture && a.textureImageSlot == b.textureImageSlot &&
    a.samplerSlot == b.samplerSlot && a.textureModel == b.textureModel &&
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
    a.drawOrdinal == b.drawOrdinal;
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
