#include "rendering/coinrender/CoinRenderRetainedTransport.h"
#include "rendering/coinrender/CoinRenderPublishedRevision.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderFramePlanBuilder.h"
#include "rendering/coinrender/CoinRenderPlanAssemblyCore.h"
#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"
#include "rendering/coinrender/CoinRenderStrokeCore.h"
#include "rendering/coinrender/CoinRenderImageCore.h"
#include "rendering/coinrender/CoinRenderPbrMaterialCapture.h"
#include "rendering/coinrender/CoinRenderPbrSurfaceCore.h"
#include <map>
#include <tuple>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <unordered_map>
#include <unordered_set>

namespace {
bool opaqueIdentityPolicy(CoinRenderAction::TransparencyType type) {
  return type == CoinRenderAction::NONE || type == CoinRenderAction::BLEND ||
    type == CoinRenderAction::DELAYED_BLEND || type == CoinRenderAction::SORTED_OBJECT_BLEND ||
    type == CoinRenderAction::SORTED_LAYERS_BLEND;
}
bool sameMaterial(const CoinRenderMaterialSnapshot & a, const CoinRenderMaterialSnapshot & b) {
  return std::equal(a.ambient, a.ambient + 4, b.ambient) && std::equal(a.diffuse, a.diffuse + 4, b.diffuse) &&
    std::equal(a.specular, a.specular + 4, b.specular) && std::equal(a.emission, a.emission + 4, b.emission) &&
    a.shininess == b.shininess && a.transparency == b.transparency &&
    a.model == b.model && (a.model != CoinRenderMaterialModel::PBR || coin_render_same_pbr_material(a.pbr,b.pbr));
}
bool copyTextureCoordinate(const SoGeometryDesc & geometry, uint32_t index, CoinRenderVertexSnapshot & vertex) {
  if (!geometry.texcoords) return true;
  const size_t stride = geometry.texcoordStride ? geometry.texcoordStride : 4 * sizeof(float);
  if (stride < 4 * sizeof(float) || stride % alignof(float) ||
      geometry.vertexCount > std::numeric_limits<size_t>::max() / stride) return false;
  float coordinate[4];
  std::memcpy(coordinate, reinterpret_cast<const unsigned char *>(geometry.texcoords) + index * stride, sizeof(coordinate));
  for (float value : coordinate) if (!std::isfinite(value)) return false;
  vertex.texcoord[0] = coordinate[0]; vertex.texcoord[1] = coordinate[1];
  vertex.textureR[0] = coordinate[2]; vertex.textureQ[0] = coordinate[3];
  if(geometry.pbrCoordinates)for(int unit=1;unit<8;++unit) {
    const auto & values=geometry.pbrCoordinates->units[unit];if(values.empty())continue;
    if(index>=values.size())return false;
    for(int c=0;c<4;++c)if(!std::isfinite(values[index][c]))return false;
    vertex.extraTexcoords[unit-1][0]=values[index][0];vertex.extraTexcoords[unit-1][1]=values[index][1];
    vertex.textureR[unit]=values[index][2];vertex.textureQ[unit]=values[index][3];
  }

  return true;
}

bool textureSampler(const SoTextureData & texture, uint32_t maxAnisotropy, CoinRenderSamplerSnapshot & sampler) {
  if ((texture.wrapS != SO_TEXTURE_WRAP_REPEAT && texture.wrapS != SO_TEXTURE_WRAP_CLAMP_TO_EDGE) ||
      (texture.wrapT != SO_TEXTURE_WRAP_REPEAT && texture.wrapT != SO_TEXTURE_WRAP_CLAMP_TO_EDGE)) return false;
  sampler.wrapS = texture.wrapS == SO_TEXTURE_WRAP_REPEAT ? CoinRenderTextureWrap::REPEAT : CoinRenderTextureWrap::CLAMP;
  sampler.wrapT = texture.wrapT == SO_TEXTURE_WRAP_REPEAT ? CoinRenderTextureWrap::REPEAT : CoinRenderTextureWrap::CLAMP;
  switch (texture.minFilter) {
  case SO_TEXTURE_FILTER_NEAREST:
    if (texture.magFilter != SO_TEXTURE_FILTER_NEAREST) return false;
    sampler.filter = CoinRenderTextureFilter::NEAREST; break;
  case SO_TEXTURE_FILTER_LINEAR:
    if (texture.magFilter != SO_TEXTURE_FILTER_LINEAR) return false;
    sampler.filter = CoinRenderTextureFilter::LINEAR; break;
  case SO_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
    if (texture.magFilter != SO_TEXTURE_FILTER_LINEAR) return false;
    sampler.filter = CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR; break;
  case SO_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
    if (texture.magFilter != SO_TEXTURE_FILTER_LINEAR) return false;
    sampler.filter = CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR; break;
  default: return false;
  }
  sampler.maxAnisotropy = texture.anisotropic ? maxAnisotropy : 1;
  return !texture.anisotropic || sampler.filter == CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR;
}

uint32_t findMaterial(const std::vector<CoinRenderMaterialSnapshot> & materials,
                      const std::vector<uint32_t> & users, const CoinRenderMaterialSnapshot & value) {
  for (uint32_t i = 0; i < materials.size(); ++i)
    if (users[i] && sameMaterial(materials[i], value)) return i;
  return UINT32_MAX;
}
}

struct CoinRenderRetainedTransport::TransportCache {
  CoinRenderFramePlan frame;
  CoinRenderPbrSurfaceCore surfaces;
  // Empty means identity: draws are already stored in native plan order.
  std::vector<uint32_t> plannedDrawIndices;
  std::vector<uint32_t> drawIndexByCommand;
  std::vector<uint32_t> materialUseCounts;
  std::unordered_map<uint32_t, uint32_t> geometryUseCounts;
  std::unordered_map<uint64_t, CoinRenderGeometryRange> sharedGeometry;
  const SoDrawList * source = nullptr;
  uint32_t generation = 0;
  uint64_t contentRevision = 0, planRevision = 0, resourceRevision = 0;
  bool reuseValid = false;
  bool opaqueIdentity = true;
  bool expandedStrokes = false;
  bool vertexMaterials = false;
  SoRenderParams params;
  CoinRenderFrameBorrow::Identity borrow;
  CoinRenderRetainedRenderer::Statistics statistics;
};
CoinRenderRetainedTransport::CoinRenderRetainedTransport(CoinRenderTarget & value)
  : transportCache(new TransportCache), transportOptions(value.getOptions()), target(&value) {}
CoinRenderRetainedTransport::~CoinRenderRetainedTransport() { shutdown(); }
CoinRenderRetainedRenderer::Statistics CoinRenderRetainedTransport::getTransportStatistics() const
{
  auto value = transportCache->statistics;
  value.materialSlots = transportCache->frame.materials.size();
  value.transportVertices = transportCache->frame.vertices.size();
  value.transportCameras = transportCache->frame.cameras.size();
  value.transportScheduleBytes = transportCache->plannedDrawIndices.capacity()*sizeof(uint32_t);
  return value;
}
void CoinRenderRetainedTransport::rebindTarget(CoinRenderTarget & destination) {
  const auto & options = destination.getOptions();
  if (transportOptions.maxTextureAnisotropy != options.maxTextureAnisotropy ||
      transportOptions.storedTextureColorSpace != options.storedTextureColorSpace ||
      transportOptions.textureSamplingPolicy != options.textureSamplingPolicy ||
      transportOptions.rasterizationPolicy != options.rasterizationPolicy ||
      transportOptions.transparency != options.transparency)
    transportCache->reuseValid = false;
  transportOptions = options;
  target = &destination;
}
void CoinRenderRetainedTransport::setTransparencyType(CoinRenderAction::TransparencyType type) {
  if (transparencyType != type) {
    transparencyType = type;
    transportCache->reuseValid = false;
  }
}
SbBool CoinRenderRetainedTransport::initialize(const SoRenderBackendInitParams & params)
{ setInitParams(params); setInitialized(TRUE); return TRUE; }
void CoinRenderRetainedTransport::shutdown() { transportCache->reuseValid = false; transportCache->borrow.reset(); setInitialized(FALSE); }

SbBool CoinRenderRetainedTransport::render(const SoDrawList & list, const SoRenderPlan & plan,
  const SoRenderParams & params, const SoSelectionState * selection)
{ return submitCapture(list, plan, params, selection, CaptureOwnership::External); }
SbBool CoinRenderRetainedTransport::submitOwnedCapture(const SoDrawList & list,
  const SoRenderPlan & plan, const SoRenderParams & params, const CoinRenderPresentationReceipt & proof)
{ producerReceipt = proof;
  struct Reset { CoinRenderPresentationReceipt & proof; ~Reset() { proof = {}; } } reset{producerReceipt};
  return submitCapture(list, plan, params, nullptr, CaptureOwnership::RendererOwned); }

SbBool
CoinRenderRetainedTransport::submitCapture(const SoDrawList & list, const SoRenderPlan & plan,
  const SoRenderParams & params, const SoSelectionState * selection, CaptureOwnership ownership)
{
  const bool rendererOwnsCapture = ownership == CaptureOwnership::RendererOwned;
  const auto refuse = [this](const char * reason) {
    transportCache->reuseValid = false; transportCache->borrow.reset();
    result = {CoinRenderBackendStatus::UNSUPPORTED, reason}; return FALSE;
  };
  if (!isInitialized()) return refuse("Retained transport is not initialized");
  auto & destination = target->getPimpl().get();
  if ((destination.kind != CoinRenderTargetP::KIND_OFFSCREEN && destination.kind != CoinRenderTargetP::KIND_WINDOW) ||
      params.flags != (SO_PARAM_CLEAR_WINDOW | SO_PARAM_CLEAR_DEPTH) || params.clearDepth != 1.0f)
    return refuse("Retained transport requires window/offscreen and full color/depth clear");
  const auto origin = params.viewport.getViewportOriginPixels();
  const auto extent = params.viewport.getViewportSizePixels();
  if (origin != SbVec2s(0,0) || extent[0] != destination.size[0] || extent[1] != destination.size[1])
    return refuse("Retained transport requires a full-target viewport");
  if (selection && (!selection->selected.empty() || !selection->highlighted.empty()))
    return refuse("Retained selection is not connected yet");
  if (!list.getSelectionState().selected.empty() || !list.getSelectionState().highlighted.empty() ||
      !list.getDepthClearEvents().empty())
    return refuse("Retained layers/selection are outside the retained transport profile");

  try {
    auto & cache = *transportCache;
    auto & frame = cache.frame;
    const uint64_t previousRevision = frame.revision;
    const auto execute = [&](CoinRenderFrameReuseKind kind) {
      CoinRenderFramePreflight resolved;
      resolved.frame = &frame; resolved.revision = frame.revision;
      resolved.transparency = frame.transparency;
      resolved.compactOrder=&cache.plannedDrawIndices;
      resolved.resolvedSchedule = true;
      resolved.opaqueIdentity = cache.opaqueIdentity;
      CoinRenderResolvedCompositionScope orderScope(frame, cache.plannedDrawIndices);
      CoinRenderFrameBorrow borrow(frame, cache.borrow, kind != CoinRenderFrameReuseKind::REUSE, rendererOwnsCapture);
      CoinRenderRevisionScope revisionScope(&frame, producerReceipt, list.getResourceRevision());
      result = destination.executeFrame(frame, {kind, previousRevision}, &resolved);
      cache.reuseValid = rendererOwnsCapture && result.status == CoinRenderBackendStatus::SUCCESS;
      if (cache.reuseValid) {
        cache.source = &list; cache.generation = list.getGeneration();
        cache.contentRevision = list.getContentRevision();
        cache.planRevision = list.getRenderPlanRevision(); cache.resourceRevision = list.getResourceRevision();
        cache.params = params;
      } else cache.borrow.reset();
      return result.status == CoinRenderBackendStatus::SUCCESS;
    };
    bool reusable = rendererOwnsCapture && cache.reuseValid && cache.source == &list &&
      cache.generation == list.getGeneration() &&
      cache.drawIndexByCommand.size() == static_cast<size_t>(list.getNumCommands()) &&
      cache.params.viewport == params.viewport && cache.params.viewMatrix == params.viewMatrix &&
      cache.params.projMatrix == params.projMatrix;
    const bool contentChanged = cache.contentRevision != list.getContentRevision();
    const auto & projectionUpdate = list.getRetainedProjectionUpdate();
    const bool projectionChanged = contentChanged && projectionUpdate.valid &&
      projectionUpdate.fromRevision == cache.contentRevision && projectionUpdate.toRevision == list.getContentRevision();
    if (reusable && projectionChanged && !cache.expandedStrokes && !cache.vertexMaterials &&
        frame.cameras.size() == 1 && cache.resourceRevision == list.getResourceRevision() &&
        cache.planRevision == list.getRenderPlanRevision()) {
      for (int c = 0; c < 4; ++c)
        if (!std::isfinite(params.clearColor[c]) || params.clearColor[c] < 0 || params.clearColor[c] > 1)
          return refuse("Invalid retained clear color");
      auto camera = frame.cameras[0];
      camera.projectionMatrixCoin = projectionUpdate.projection;
      camera.isPerspective = projectionUpdate.camera.perspective;
      camera.nearDistance = projectionUpdate.camera.nearDistance; camera.farDistance = projectionUpdate.camera.farDistance;
      camera.focalDistance = projectionUpdate.camera.focalDistance; camera.aspectRatio = projectionUpdate.camera.aspectRatio;
      CoinRenderPlanAssemblyCore::normalizeCamera(camera);
      cache.borrow.reset();
      for (const auto & draw : frame.draws) {
        auto & state = frame.renderStates[draw.renderStateSlot];
        state.projectionCoin = camera.projectionMatrixCoin;
        const auto source = list.getCommandSnapshot(static_cast<int>(draw.frameNodeOrdinal));
        if (source.state.fog.visibility <= 0) state.fogEnd = camera.farDistance;
      }
      frame.cameras[0] = camera; frame.clearColor = params.clearColor;
      frame.revision = CoinRenderFramePlanBuilder::nextRevision();
      ++cache.statistics.transportReuses;
      cache.statistics.transportPatchedCommands += frame.draws.size();
      return execute(CoinRenderFrameReuseKind::CAMERA_PATCH);
    }
    if (reusable && contentChanged && (cache.expandedStrokes || cache.vertexMaterials)) reusable = false;
    if (reusable && projectionChanged) reusable = false;
    if (reusable && contentChanged && list.getRetainedCommandUpdatesFromRevision() != cache.contentRevision)
      reusable = false;
    const bool geometryChanged = cache.resourceRevision != list.getResourceRevision();
    const bool planChanged = cache.planRevision != list.getRenderPlanRevision();
    if(geometryChanged&&!frame.pbrSurfaceVertices.empty())reusable=false;
    if (reusable && contentChanged && planChanged) {
      for (uint32_t commandIndex : list.getRetainedCommandUpdates()) {
        if (commandIndex >= cache.drawIndexByCommand.size()) return refuse("Invalid retained visibility update");
        const bool hadDraw = cache.drawIndexByCommand[commandIndex] != UINT32_MAX;
        if (hadDraw != bool(list.isCommandVisible(static_cast<int>(commandIndex)))) { reusable = false; break; }
      }
    }
    // A native generation fixes command membership. A committed same-layout
    // geometry delta may invalidate its plan, but it does not replace transport.
    if (reusable && (geometryChanged || planChanged) && !contentChanged) reusable = false;
    if (reusable) {
      const bool clearChanged = frame.clearColor != params.clearColor;
      if (contentChanged || clearChanged) cache.borrow.reset();
      std::unordered_set<uint32_t> patchedRanges;
      if (contentChanged) {
        for (uint32_t commandIndex : list.getRetainedCommandUpdates()) {
          if (commandIndex >= cache.drawIndexByCommand.size()) return refuse("Invalid retained command patch");
          const uint32_t drawIndex = cache.drawIndexByCommand[commandIndex];
          if (drawIndex == UINT32_MAX && !list.isCommandVisible(static_cast<int>(commandIndex))) continue;
          if (drawIndex >= frame.draws.size()) return refuse("Retained patch changes command membership");
          const auto & command = list.getCommandSnapshot(static_cast<int>(commandIndex));
          // Native deltas name the changed commands. Geometry resources have a
          // stable layout after the updater's transaction; verify our own spans
          // before modifying owned storage. No native pointer is retained.
          if (geometryChanged && patchedRanges.insert(frame.draws[drawIndex].geometry.firstVertex).second) {
            const auto & geometry = list.getCommandGeometry(command);
            const auto & range = frame.draws[drawIndex].geometry;
            const size_t stride = geometry.vertexStride ? geometry.vertexStride : 3 * sizeof(float);
            if (static_cast<uint32_t>(geometry.topology) != static_cast<uint32_t>(frame.draws[drawIndex].topology) ||
                !geometry.positions || geometry.colors ||
                (geometry.indexCount && !geometry.indices) || stride < 3 * sizeof(float) ||
                stride % alignof(float) || geometry.vertexCount != range.vertexCount ||
                (geometry.indexCount ? geometry.indexCount : geometry.vertexCount) != range.indexCount ||
                geometry.vertexCount > std::numeric_limits<size_t>::max() / stride)
              return refuse("Retained geometry patch changes the admitted layout");
            for (uint32_t vertex = 0; vertex < range.vertexCount; ++vertex) {
              float position[3];
              std::memcpy(position, reinterpret_cast<const unsigned char *>(geometry.positions) + vertex * stride,
                          sizeof(position));
              for (float value : position)
                if (!std::isfinite(value)) return refuse("Non-finite retained geometry patch");
              auto & owned = frame.vertices[range.firstVertex + vertex];
              if (command.material.shadingModel != SO_SHADING_UNLIT) {
                if (!geometry.normals || geometry.normalCount != geometry.vertexCount)
                  return refuse("Lit retained patch requires vertex normals");
                float normal[3];
                std::memcpy(normal, reinterpret_cast<const unsigned char *>(geometry.normals) + vertex * stride, sizeof(normal));
                for (float value : normal) if (!std::isfinite(value)) return refuse("Non-finite retained normal");
                std::memcpy(owned.normal, normal, sizeof(normal));
              }
              if (!copyTextureCoordinate(geometry, vertex, owned)) return refuse("Invalid retained texture coordinate patch");
              if (std::memcmp(owned.position, position, sizeof(position))) {
                std::memcpy(owned.position, position, sizeof(position));
                ++cache.statistics.copiedVertices;
                ++cache.statistics.geometryPatchedVertices;
              }
            }
            for (uint32_t index = 0; index < range.indexCount; ++index) {
              const uint32_t local = geometry.indexCount ? geometry.indices[index] : index;
              if (local >= range.vertexCount) return refuse("Retained geometry patch index is out of range");
              auto & owned = frame.indices[range.firstIndex + index];
              if (owned != range.firstVertex + local) {
                owned = range.firstVertex + local;
                ++cache.statistics.copiedIndices;
              }
            }
          }
          for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
            if (!std::isfinite(command.modelMatrix[row][column])) return refuse("Non-finite retained matrix patch");
          for (int channel = 0; channel < 4; ++channel)
            if (!std::isfinite(command.material.diffuse[channel]) || command.material.diffuse[channel] < 0 ||
                command.material.diffuse[channel] > 1) return refuse("Invalid retained diffuse patch");
          auto & state = frame.renderStates[frame.draws[drawIndex].renderStateSlot];
          state.model = command.modelMatrix;
          auto updatedMaterial = frame.materials[state.materialSlot];
          if(command.material.pbr) {
            if(!so_valid_pbr_material(*command.material.pbr))return refuse("Invalid retained PBR patch");
            updatedMaterial.pbr=coin_render_pbr_snapshot(*command.material.pbr);
          }
          for (int channel = 0; channel < 4; ++channel)
            updatedMaterial.diffuse[channel] = command.material.diffuse[channel];
          const uint32_t previousSlot = state.materialSlot;
          if (!sameMaterial(frame.materials[previousSlot], updatedMaterial)) {
            uint32_t slot = previousSlot;
            if (cache.materialUseCounts[previousSlot] > 1) {
              slot = findMaterial(frame.materials, cache.materialUseCounts, updatedMaterial);
              if (slot == UINT32_MAX) {
                auto free = std::find(cache.materialUseCounts.begin(), cache.materialUseCounts.end(), 0u);
                if (free != cache.materialUseCounts.end()) {
                  slot = static_cast<uint32_t>(free - cache.materialUseCounts.begin());
                  frame.materials[slot] = updatedMaterial;
                } else {
                  slot = static_cast<uint32_t>(frame.materials.size());
                  frame.materials.push_back(updatedMaterial); cache.materialUseCounts.push_back(0);
                }
              }
              --cache.materialUseCounts[previousSlot]; ++cache.materialUseCounts[slot];
              state.materialSlot = slot;
              auto & range = frame.draws[drawIndex].geometry;
              const auto original = range;
              const uint64_t key = (uint64_t(command.geometryHandle) << 32) | slot;
              const auto peer = cache.sharedGeometry.find(key);
              if (command.geometryHandle && peer != cache.sharedGeometry.end() &&
                  cache.geometryUseCounts[peer->second.firstVertex] &&
                  frame.vertices[peer->second.firstVertex].materialSlot == slot) {
                --cache.geometryUseCounts[range.firstVertex]; range = peer->second;
                ++cache.geometryUseCounts[range.firstVertex];
              } else if (cache.geometryUseCounts[range.firstVertex] > 1) {
                if (frame.vertices.size() + range.vertexCount > (32u * 1024u * 1024u) / sizeof(CoinRenderVertexSnapshot) ||
                    frame.indices.size() + range.indexCount > (32u * 1024u * 1024u) / sizeof(uint32_t))
                  return refuse("Retained material span split exceeds geometry budget");
                --cache.geometryUseCounts[original.firstVertex];
                range.firstVertex = static_cast<uint32_t>(frame.vertices.size());
                range.firstIndex = static_cast<uint32_t>(frame.indices.size());
                for (uint32_t v = 0; v < original.vertexCount; ++v) {
                  const auto copy = frame.vertices[original.firstVertex + v]; frame.vertices.push_back(copy);
                }
                for (uint32_t i = 0; i < original.indexCount; ++i)
                  frame.indices.push_back(range.firstVertex + frame.indices[original.firstIndex + i] - original.firstVertex);
                cache.geometryUseCounts[range.firstVertex] = 1;
              }
              for (uint32_t vertex = 0; vertex < range.vertexCount; ++vertex)
                frame.vertices[range.firstVertex + vertex].materialSlot = slot;
              if (command.geometryHandle) cache.sharedGeometry[key] = range;

            } else frame.materials[slot] = updatedMaterial;
          }
          ++cache.statistics.transportPatchedCommands;
        }
      }
      if (planChanged) {
        // Consume the new authoritative plan without generating a new one.
        size_t drawOrdinal = 0;
        cache.opaqueIdentity = true;
        bool identityOrder = true;
        std::vector<bool> seen(frame.draws.size(), false);
        for (int operation = 0; operation < plan.getNumOperations(); ++operation) {
          const auto & op = plan.getOperation(operation);
          if (op.type == SoRenderOperationType::END_DEPTH_SEGMENT) continue;
          if (op.type != SoRenderOperationType::DRAW || op.commandIndex >= cache.drawIndexByCommand.size())
            return refuse("Invalid incremental retained plan");
          const uint32_t drawIndex = cache.drawIndexByCommand[op.commandIndex];
          if (drawIndex == UINT32_MAX && !list.isCommandVisible(static_cast<int>(op.commandIndex))) continue;
          if (drawIndex >= frame.draws.size() || seen[drawIndex] || drawOrdinal >= frame.draws.size())
            return refuse("Incremental retained plan changes membership");
          seen[drawIndex] = true;
          identityOrder = identityOrder && drawIndex == drawOrdinal;
          cache.opaqueIdentity = cache.opaqueIdentity && drawIndex == drawOrdinal &&
            !frame.renderStates[frame.draws[drawIndex].renderStateSlot].transparentMaterial && opaqueIdentityPolicy(transparencyType);
          if(drawIndex!=drawOrdinal && cache.plannedDrawIndices.empty()){
            cache.plannedDrawIndices.resize(frame.draws.size());
            for(size_t i=0;i<cache.plannedDrawIndices.size();++i)cache.plannedDrawIndices[i]=static_cast<uint32_t>(i);
          }
          if(!cache.plannedDrawIndices.empty())cache.plannedDrawIndices[drawOrdinal]=drawIndex;
          ++drawOrdinal;
        }
        if (drawOrdinal != frame.draws.size()) return refuse("Incremental retained plan omits draws");
        if(identityOrder)std::vector<uint32_t>().swap(cache.plannedDrawIndices);
      }
      if (geometryChanged) ++cache.statistics.transportGeometryPatches;
      for (int channel = 0; channel < 4; ++channel)
        if (!std::isfinite(params.clearColor[channel]) || params.clearColor[channel] < 0 || params.clearColor[channel] > 1)
          return refuse("Invalid retained clear color");
      frame.clearColor = params.clearColor;
      if (contentChanged || clearChanged) frame.revision = CoinRenderFramePlanBuilder::nextRevision();
      ++cache.statistics.transportReuses;
      return execute(contentChanged || clearChanged ? CoinRenderFrameReuseKind::RESOURCE_REBUILD : CoinRenderFrameReuseKind::REUSE);
    }
    cache.reuseValid = false; cache.borrow.reset();
    frame = CoinRenderFramePlan();
    frame.textureSamplingPolicy = destination.options.textureSamplingPolicy;
    frame.rasterizationPolicy = destination.options.rasterizationPolicy;
    frame.transparency.mode = destination.options.transparency;
    frame.revision = rendererOwnsCapture ? CoinRenderFramePlanBuilder::nextRevision() : 0;
    frame.clearColor = params.clearColor;
    frame.lightingStates.emplace_back();
    auto & schedule = cache.plannedDrawIndices;
    schedule.clear();
    const size_t count = static_cast<size_t>(plan.getNumOperations());
    std::vector<uint32_t>().swap(schedule); frame.draws.reserve(count); frame.renderStates.reserve(count);
    cache.opaqueIdentity = true; // Conversion emits draws in supplied plan order.
    cache.expandedStrokes = false;
    cache.vertexMaterials = false;
    cache.materialUseCounts.clear(); cache.geometryUseCounts.clear();
    cache.sharedGeometry.clear();
    cache.surfaces.beginCapture();
    auto & sharedGeometry = cache.sharedGeometry;
    std::unordered_map<SoLightingHandle, uint32_t> lightingSlots;
    using TextureKey = std::tuple<uintptr_t, int, int, int, bool>;
    std::map<TextureKey, uint32_t> textureSlots;
    size_t textureBytes = 0;
    cache.drawIndexByCommand.assign(static_cast<size_t>(list.getNumCommands()), UINT32_MAX);
    std::vector<bool> visited(static_cast<size_t>(list.getNumCommands()), false);

    for (int operation = 0; operation < plan.getNumOperations(); ++operation) {
      const auto & op = plan.getOperation(operation);
      if (op.type == SoRenderOperationType::END_DEPTH_SEGMENT) continue;
      if (op.type != SoRenderOperationType::DRAW || op.commandIndex >= visited.size() || visited[op.commandIndex])
        return refuse("Invalid or unsupported retained plan operation");
      visited[op.commandIndex] = true;
      const auto & command = list.getCommandSnapshot(static_cast<int>(op.commandIndex));
      const auto & geometry = list.getCommandGeometry(command);
      const auto & material = command.material;
      const auto & state = command.state;
      const auto & raster = state.raster;
      const bool pbr = material.shadingModel == SO_SHADING_PBR_METALLIC_ROUGHNESS;
      if (pbr != bool(material.pbr)) return refuse("Invalid retained PBR descriptor/model");
#ifndef HAVE_COIN_BGFX
      if (pbr) return refuse("PBR executor transport requires BGFX in this profile");
#endif
      if (pbr && (!so_valid_pbr_material(*material.pbr) || geometry.topology != SO_TOPOLOGY_TRIANGLES ||
          geometry.colors || command.opacityClass != SO_OPACITY_OPAQUE))
        return refuse("Retained PBR requires opaque uniform triangles");
      if (command.stage != SoRenderStage::Main ||
          (command.opacityClass != SO_OPACITY_OPAQUE && command.opacityClass != SO_OPACITY_TRANSPARENT) ||
          (!pbr && material.shadingModel != SO_SHADING_UNLIT && material.shadingModel != SO_SHADING_LEGACY_GOURAUD) || !std::isfinite(material.opacity) || material.opacity < 0 || material.opacity > 1 ||
          material.diffuse[3] != material.opacity ||
          command.pixelRaster.enabled || material.textureAlphaIncludesOpacity || material.twoSidedLighting ||
          (state.alphaTest.policy != SO_ALPHA_TEST_POLICY_NONE && state.alphaTest.policy != SO_ALPHA_TEST_POLICY_EXPLICIT) ||
          raster.fillMode != SO_RASTER_FILL || raster.scissorEnabled)
        return refuse("Retained command exceeds the supported geometry profile");
      if (state.blend.enabled &&
          (state.blend.srcRGBFactor != SO_BLEND_FACTOR_SRC_ALPHA ||
           state.blend.dstRGBFactor != SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA ||
           state.blend.srcAlphaFactor != SO_BLEND_FACTOR_SRC_ALPHA ||
           state.blend.dstAlphaFactor != SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA ||
           state.blend.rgbEquation != SO_BLEND_EQUATION_ADD || state.blend.alphaEquation != SO_BLEND_EQUATION_ADD))
        return refuse("Nonstandard retained blending is not connected yet");
      const uint32_t primitiveWidth = geometry.topology == SO_TOPOLOGY_TRIANGLES ? 3 :
        geometry.topology == SO_TOPOLOGY_LINES ? 2 : geometry.topology == SO_TOPOLOGY_POINTS ? 1 : 0;
      if (!primitiveWidth || !geometry.positions || geometry.vertexCount == 0 ||
          (geometry.indexCount && !geometry.indices) ||
          (geometry.indexCount ? geometry.indexCount : geometry.vertexCount) % primitiveWidth != 0)
        return refuse("Retained transport requires complete triangle, line or point geometry");
      if (geometry.topology == SO_TOPOLOGY_LINES && raster.linePattern != 0xffff)
        return refuse("Retained stippled strips require original strip provenance");
      cache.expandedStrokes = cache.expandedStrokes || geometry.topology != SO_TOPOLOGY_TRIANGLES;
      const size_t stride = geometry.vertexStride ? geometry.vertexStride : 3 * sizeof(float);
      if (stride < 3 * sizeof(float) || stride % alignof(float) ||
          geometry.vertexCount > (32u * 1024u * 1024u) / sizeof(CoinRenderVertexSnapshot) ||
          geometry.indexCount > (32u * 1024u * 1024u) / sizeof(uint32_t) ||
          geometry.vertexCount > std::numeric_limits<size_t>::max() / stride ||
          frame.indices.size() + (geometry.indexCount ? geometry.indexCount : geometry.vertexCount) >
            (32u * 1024u * 1024u) / sizeof(uint32_t) ||
          frame.vertices.size() + geometry.vertexCount > (32u * 1024u * 1024u) / sizeof(CoinRenderVertexSnapshot))
        return refuse("Retained geometry stride or connector memory budget exceeded");
      if (raster.viewportOverride || raster.viewportX != 0 || raster.viewportY != 0 ||
          raster.viewportWidth != extent[0] || raster.viewportHeight != extent[1])
        return refuse("Retained command viewport is outside the initial profile");
      if (!raster.visible) continue;

      CoinRenderDrawPacket draw;
      draw.topology = geometry.topology == SO_TOPOLOGY_LINES ? CoinRenderPrimitiveTopology::LINE_LIST :
        geometry.topology == SO_TOPOLOGY_POINTS ? CoinRenderPrimitiveTopology::POINT_LIST : CoinRenderPrimitiveTopology::TRIANGLE_LIST;
      draw.renderStateSlot = static_cast<uint32_t>(frame.renderStates.size());
      draw.geometry.firstVertex = static_cast<uint32_t>(frame.vertices.size());
      draw.geometry.vertexCount = geometry.vertexCount;
      draw.geometry.firstIndex = static_cast<uint32_t>(frame.indices.size());
      draw.geometry.indexCount = geometry.indexCount ? geometry.indexCount : geometry.vertexCount;
      draw.frameNodeOrdinal = op.commandIndex;
      // A native occurrence ID is not CoinRender's baked-geometry cache key.
      // Keep diagnostic identity, but grant no cross-frame GPU cache proof.
      draw.sourceOccurrence = uint64_t(op.commandIndex) + 1;
      draw.stableNodeId = 0;
      CoinRenderMaterialSnapshot copiedMaterial;
      if (pbr) {
        copiedMaterial.model = CoinRenderMaterialModel::PBR;
        copiedMaterial.pbr = coin_render_pbr_snapshot(*material.pbr);
      }
      for (int channel = 0; channel < 4; ++channel) {
        copiedMaterial.diffuse[channel] = material.diffuse[channel];
        copiedMaterial.ambient[channel] = material.ambient[channel];
        copiedMaterial.specular[channel] = material.specular[channel];
        copiedMaterial.emission[channel] = material.emissive[channel];
      }
      copiedMaterial.shininess = material.shininess;
      copiedMaterial.transparency = 1.0f - material.opacity;
      uint32_t materialSlot = findMaterial(frame.materials, cache.materialUseCounts, copiedMaterial);
      if (materialSlot == UINT32_MAX) {
        materialSlot = static_cast<uint32_t>(frame.materials.size());
        frame.materials.push_back(copiedMaterial); cache.materialUseCounts.push_back(0);
      }
      ++cache.materialUseCounts[materialSlot];
      const uint64_t sharedKey = (uint64_t(command.geometryHandle) << 32) | materialSlot;
      const auto shared = sharedGeometry.find(sharedKey);
      if (command.geometryHandle && shared != sharedGeometry.end()) draw.geometry = shared->second;
      else {
      for (uint32_t vertex = 0; vertex < geometry.vertexCount; ++vertex) {
        CoinRenderVertexSnapshot copy;
        const auto * bytes = reinterpret_cast<const unsigned char *>(geometry.positions) + vertex * stride;
        std::memcpy(copy.position, bytes, sizeof(copy.position));
        if (geometry.normals && geometry.normalCount == geometry.vertexCount)
          std::memcpy(copy.normal, reinterpret_cast<const unsigned char *>(geometry.normals) + vertex * stride, sizeof(copy.normal));
        else if (material.shadingModel != SO_SHADING_UNLIT) return refuse("Lit retained geometry requires vertex normals");
        if (!copyTextureCoordinate(geometry, vertex, copy)) return refuse("Invalid retained texture coordinate stride/value");
        copy.materialSlot = materialSlot;
        if (geometry.colors) {
          cache.vertexMaterials = true;
          CoinRenderMaterialSnapshot vertexMaterial = copiedMaterial;
          for (int c = 0; c < 4; ++c) {
            const float value = geometry.colors[static_cast<size_t>(vertex) * 4 + c];
            if (!std::isfinite(value) || value < 0 || value > 1) return refuse("Invalid retained vertex color");
            vertexMaterial.diffuse[c] = value;
          }
          if (!material.vertexColorAlphaIncludesOpacity)
            vertexMaterial.diffuse[3] *= material.opacity;
          vertexMaterial.transparency = 1.0f - vertexMaterial.diffuse[3];
          copy.materialSlot = CoinRenderPlanAssemblyCore::material(frame, vertexMaterial);
          if (cache.materialUseCounts.size() < frame.materials.size()) cache.materialUseCounts.resize(frame.materials.size());
          ++cache.materialUseCounts[copy.materialSlot];
        }
        frame.vertices.push_back(copy);
      }
      for (uint32_t index = 0; index < draw.geometry.indexCount; ++index) {
        const uint32_t local = geometry.indexCount ? geometry.indices[index] : index;
        if (local >= geometry.vertexCount) return refuse("Retained index is outside its geometry");
        frame.indices.push_back(draw.geometry.firstVertex + local);
      }
      if (command.geometryHandle) sharedGeometry.emplace(sharedKey, draw.geometry);
      }
      ++cache.geometryUseCounts[draw.geometry.firstVertex];
      CoinRenderRenderStateSnapshot copiedState;
      copiedState.model = command.modelMatrix;
      copiedState.view = state.useCommandMatrices ? command.viewMatrix : params.viewMatrix;
      copiedState.projectionCoin = state.useCommandMatrices ? command.projMatrix : params.projMatrix;
      copiedState.materialSlot = materialSlot;
      copiedState.cameraSlot = static_cast<uint32_t>(frame.cameras.size());
      copiedState.lightModel = material.shadingModel == SO_SHADING_UNLIT ? CoinRenderLightModel::BASE_COLOR : CoinRenderLightModel::PHONG;
      if (copiedState.lightModel == CoinRenderLightModel::PHONG) {
        auto found = lightingSlots.find(command.lightingHandle);
        if (found == lightingSlots.end()) {
          const auto * native = list.getLighting(command.lightingHandle);
          if (!native || native->lights.size() > COIN_RENDER_MAX_LIGHTS) return refuse("Missing/excess retained lighting");
          CoinRenderLightingSnapshot lighting;
          lighting.ambientIntensity = std::max(1.0f, std::max(native->ambient[0], std::max(native->ambient[1], native->ambient[2])));
          for (int c = 0; c < 3; ++c) lighting.ambientColor[c] = native->ambient[c] / lighting.ambientIntensity;
          for (const auto & source : native->lights) {
            if (source.type != SO_LIGHT_DIRECTIONAL && source.type != SO_LIGHT_POINT && source.type != SO_LIGHT_SPOT)
              return refuse("Unknown retained light type");
            CoinRenderLightSourceSnapshot light;
            light.type = static_cast<CoinRenderLightType>(source.type);
            // IR carries intensity-weighted RGB. Preserve values above one
            // through the executor's separate color/intensity representation.
            light.intensity = std::max(1.0f, std::max(source.color[0], std::max(source.color[1], source.color[2])));
            for (int c = 0; c < 3; ++c) {
              light.color[c] = source.color[c] / light.intensity;
              // Directional IR points toward the source; spots point away.
              light.direction[c] = source.type == SO_LIGHT_DIRECTIONAL ? -source.direction[c] : source.direction[c];
              light.position[c] = source.position[c];
              light.attenuation[c] = source.attenuation[c];
            }
            if (source.type == SO_LIGHT_SPOT) {
              if (!std::isfinite(source.spotCutoffCos) || source.spotCutoffCos < -std::numeric_limits<float>::epsilon() || source.spotCutoffCos > 1 ||
                  !std::isfinite(source.spotExponent) || source.spotExponent < 0 || source.spotExponent > 128)
                return refuse("Invalid retained spotlight cone");
              light.cutOffAngle = std::acos(std::max(0.0f, source.spotCutoffCos));
              light.dropOffRate = source.spotExponent / 128.0f;
            }
            lighting.lights.push_back(light);
          }
          const uint32_t index = frame.lightingStates.size(); frame.lightingStates.push_back(lighting);
          found = lightingSlots.emplace(command.lightingHandle, index).first;
        }
        copiedState.lightingSlot = found->second;
      }
      if (material.texture.pixels) {
        const auto & source = material.texture;
        CoinRenderSamplerSnapshot sampler;
        if (!textureSampler(source, destination.options.maxTextureAnisotropy, sampler) ||
            source.width <= 0 || source.height <= 0 || source.numComponents < 1 || source.numComponents > 4 ||
            static_cast<size_t>(source.width) > (32u * 1024u * 1024u) / static_cast<size_t>(source.height) ||
            !geometry.texcoords)
          return refuse("Invalid or unsupported retained 2D texture/sampler");
        const bool mipmapped = CoinRenderTextureSamplingCore::mipFilter(sampler.filter);
        const TextureKey key(reinterpret_cast<uintptr_t>(source.pixels), source.width, source.height, source.numComponents, mipmapped);
        auto found = textureSlots.find(key);
        if (found == textureSlots.end()) {
          const size_t pixels = static_cast<size_t>(source.width) * source.height;
          const size_t bytes = pixels * 4;
          if (textureBytes + bytes > 128u * 1024u * 1024u) return refuse("Retained texture budget exceeded");
          CoinRenderTextureImageSnapshot image;
          image.format = destination.options.storedTextureColorSpace == COIN_RENDER_TEXTURE_SRGB
            ? CoinRenderTextureFormat::RGBA8_SRGB : CoinRenderTextureFormat::RGBA8_LINEAR;
          image.width = source.width; image.height = source.height;
          if (!CoinRenderImageCore::convertToRgba8(source.pixels, pixels, source.numComponents, image.pixelsRgba))
            return refuse("Invalid retained image components");
          image.contentDigest = CoinRenderImageCore::rgba8Digest(image.pixelsRgba);
          if (mipmapped && !CoinRenderTextureSamplingCore::generate(image)) return refuse("Retained mipmap generation failed");
          textureBytes += image.pixelsRgba.size() + image.mipmapsRgba.size();
          if (textureBytes > 128u * 1024u * 1024u) return refuse("Retained texture/mipmap budget exceeded");
          const uint32_t slot = CoinRenderPlanAssemblyCore::texture(frame, std::move(image));
          found = textureSlots.emplace(key, slot).first;
        }
        copiedState.hasTexture = true;
        copiedState.textureImageSlot = found->second;
        copiedState.samplerSlot = CoinRenderPlanAssemblyCore::sampler(frame, sampler);
        copiedState.textureMatrix = source.matrix;
        switch (source.model) {
        case SO_TEXTURE_MODEL_MODULATE: copiedState.textureModel = CoinRenderTextureModel::MODULATE; break;
        case SO_TEXTURE_MODEL_REPLACE: copiedState.textureModel = CoinRenderTextureModel::REPLACE; break;
        case SO_TEXTURE_MODEL_DECAL: copiedState.textureModel = CoinRenderTextureModel::DECAL; break;
        case SO_TEXTURE_MODEL_BLEND: copiedState.textureModel = CoinRenderTextureModel::BLEND; break;
        default: return refuse("Unknown retained texture model");
        }
        for (int c = 0; c < 4; ++c) copiedState.textureBlendColor[c] = source.blendColor[c];
      } else if (material.texture.cacheKey || material.texture.width || material.texture.height)
        return refuse("Retained texture metadata has no pixel payload");
      if(pbr && material.pbrSurface) {
        std::string error;
        if(!cache.surfaces.images(frame,copiedState,*material.pbrSurface,destination.options.maxTextureAnisotropy,error))return refuse(error.c_str());
      }
      copiedState.cullMode = raster.cullBackFaces ? CoinRenderCullMode::BACK : CoinRenderCullMode::NONE;
      copiedState.frontFace = raster.frontFaceCCW ? CoinRenderFrontFace::CCW : CoinRenderFrontFace::CW;
      copiedState.alphaTestFunction = static_cast<CoinRenderAlphaTestFunction>(state.alphaTest.function);
      copiedState.alphaTestReference = state.alphaTest.reference;
      copiedState.explicitDepthMask = state.depth.explicitMask;
      copiedState.transparentMaterial = command.opacityClass == SO_OPACITY_TRANSPARENT;
      cache.opaqueIdentity = cache.opaqueIdentity && !copiedState.transparentMaterial && opaqueIdentityPolicy(transparencyType);
      copiedState.depthTest = state.depth.enabled;
      copiedState.depthWrite = state.depth.writeEnabled;
      copiedState.depthFunction = static_cast<CoinRenderDepthFunction>(state.depth.func);
      copiedState.depthRange[0] = state.depth.range[0];
      copiedState.depthRange[1] = state.depth.range[1];
      if (state.clipPlaneCount > COIN_RENDER_MAX_CLIP_PLANES) return refuse("More than eight retained clipping planes");
      copiedState.clipPlanesWorld.assign(state.clipPlanesWorld.begin(), state.clipPlanesWorld.begin() + state.clipPlaneCount);
      copiedState.fogMode = static_cast<CoinRenderFogMode>(state.fog.mode);
      for (int c = 0; c < 3; ++c) copiedState.fogColor[c] = state.fog.color[c];
      copiedState.fogStart = state.fog.start;
      copiedState.fogEnd = state.fog.visibility;
      copiedState.lineWidth = raster.lineWidth == 0 ? 1.0f : raster.lineWidth;
      copiedState.pointSize = raster.pointSize == 0 ? 1.0f : raster.pointSize;
      copiedState.linePattern = raster.linePattern;
      copiedState.linePatternScaleFactor = raster.linePatternScale;
      copiedState.polygonOffsetFactor = raster.polygonOffsetFactor;
      copiedState.polygonOffsetUnits = raster.polygonOffsetUnits;
      copiedState.polygonOffsetStyles = (raster.polygonOffsetFilled ? 1u : 0u) |
        (raster.polygonOffsetLines ? 2u : 0u) | (raster.polygonOffsetPoints ? 4u : 0u);
      copiedState.polygonOffsetPrimitiveStyle = primitiveWidth == 3 ? 1u : primitiveWidth == 2 ? 2u : 4u;
      copiedState.polygonOffsetEnabled = (copiedState.polygonOffsetStyles & copiedState.polygonOffsetPrimitiveStyle) != 0;
      copiedState.screenDoorTransparency = material.screenDoorTransparency;
      copiedState.transparencyType = transparencyType;
      CoinRenderCameraSnapshot camera;
      camera.isPerspective = state.camera.perspective;
      camera.nearDistance = state.camera.nearDistance;
      camera.farDistance = state.camera.farDistance;
      camera.focalDistance = state.camera.focalDistance;
      camera.aspectRatio = state.camera.aspectRatio;
      CoinRenderPlanAssemblyCore::normalizeCamera(camera);
      CoinRenderPlanAssemblyCore::normalizeState(copiedState, camera);
      camera.viewMatrix = copiedState.view;
      camera.projectionMatrixCoin = copiedState.projectionCoin;
      if (!frame.cameras.empty() && frame.cameras.back().viewMatrix == camera.viewMatrix &&
          frame.cameras.back().projectionMatrixCoin == camera.projectionMatrixCoin &&
          frame.cameras.back().isPerspective == camera.isPerspective &&
          frame.cameras.back().nearDistance == camera.nearDistance && frame.cameras.back().farDistance == camera.farDistance &&
          frame.cameras.back().focalDistance == camera.focalDistance && frame.cameras.back().aspectRatio == camera.aspectRatio)
        copiedState.cameraSlot = frame.cameras.size() - 1;
      else frame.cameras.push_back(camera);
      frame.renderStates.push_back(copiedState);
      frame.draws.push_back(draw);
      cache.drawIndexByCommand[op.commandIndex] = static_cast<uint32_t>(frame.draws.size() - 1);

    }
    if (std::find(visited.begin(), visited.end(), false) != visited.end())
      return refuse("Retained plan omits commands");
    CoinRenderViewportSnapshot viewport;
    viewport.width = extent[0]; viewport.height = extent[1];
    frame.viewports.push_back(viewport);
    std::string diagnostic;
    // Shared mechanical lowering consumes the native order, without another
    // capture, classifier or planner. Static lowered payloads remain reusable.
    if (cache.expandedStrokes) {
      if (!coin_render_expand_strokes(frame, diagnostic)) {
        result = {CoinRenderBackendStatus::UNSUPPORTED, diagnostic}; return FALSE;
      }
      cache.opaqueIdentity = false;
    }
    if(!cache.surfaces.prepare(frame,128u*1024u*1024u,diagnostic))return refuse(diagnostic.c_str());
    if(!frame.pbrSurfaceVertices.empty()) {
      cache.sharedGeometry.clear();cache.geometryUseCounts.clear();
      for(const auto & draw:frame.draws)++cache.geometryUseCounts[draw.geometry.firstVertex];
    }
    if (!frame.isValid(&diagnostic)) {
      result = {CoinRenderBackendStatus::UNSUPPORTED, diagnostic}; return FALSE;
    }
    ++cache.statistics.transportBuilds;
    cache.statistics.copiedVertices += frame.vertices.size();
    cache.statistics.copiedIndices += frame.indices.size();
    return execute(CoinRenderFrameReuseKind::FULL_REBUILD);
  }
  catch (const std::bad_alloc &) {
    transportCache->reuseValid = false; transportCache->borrow.reset();
    result = {CoinRenderBackendStatus::OUT_OF_MEMORY, "Cannot allocate retained transport snapshot"};
    return FALSE;
  }
}
