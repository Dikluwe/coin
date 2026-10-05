#include "rendering/coinrender/CoinRenderPlanAssemblyCore.h"
#include "rendering/coinrender/CoinRenderStateCore.h"
#include "rendering/coinrender/CoinRenderPolygonStyleCore.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace {
uint64_t modelMatrixKey(const SbMatrix & matrix)
{
  uint64_t key = UINT64_C(14695981039346656037);
  const auto & values = matrix.getValue();
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      const float value = values[row][column];
      uint32_t bits = 0;
      // SbMatrix equality treats positive and negative zero as equal.
      if (value != 0.0f) std::memcpy(&bits, &value, sizeof(bits));
      key = (key ^ bits) * UINT64_C(1099511628211);
    }
  }
  return key;
}
}


void CoinRenderPlanAssemblyCore::normalizeCamera(CoinRenderCameraSnapshot & camera) {
  if (camera.isPerspective && camera.nearDistance <= 0) camera.nearDistance = 0.1f;
  if (camera.farDistance <= camera.nearDistance) camera.farDistance = camera.nearDistance + 100.0f;
}
void CoinRenderPlanAssemblyCore::transformLight(CoinRenderLightSourceSnapshot & light) {
  if (light.type != CoinRenderLightType::DIRECTIONAL) {
    SbVec3f position;
    light.sourceModel.multVecMatrix(SbVec3f(light.position), position);
    position.getValue(light.position[0], light.position[1], light.position[2]);
  }
  if (light.type != CoinRenderLightType::POINT) {
    SbVec3f direction;
    light.sourceModel.multDirMatrix(SbVec3f(light.direction), direction);
    direction.normalize();
    direction.getValue(light.direction[0], light.direction[1], light.direction[2]);
  }
  if (light.type == CoinRenderLightType::SPOT) {
    const float cutoff = light.cutOffAngle, dropoff = light.dropOffRate;
    light.cutOffAngle = std::isfinite(cutoff) ? std::max(0.0f, std::min(1.570796327f, cutoff)) : cutoff;
    light.dropOffRate = std::isfinite(dropoff) ? std::max(0.0f, std::min(1.0f, dropoff)) : dropoff;
  }
}
void CoinRenderPlanAssemblyCore::normalizeState(CoinRenderRenderStateSnapshot & state,
                                                const CoinRenderCameraSnapshot & camera) {
  state.screenDoorTransparency = std::max(0.0f, std::min(1.0f, state.screenDoorTransparency));
  state.fogEnd = state.fogEnd > 0 ? state.fogEnd : camera.farDistance;
  if (state.lineWidth <= 0) state.lineWidth = 1;
  if (state.pointSize <= 0) state.pointSize = 1;
  state.linePattern &= 0xffffu;
  state.linePatternScaleFactor = std::max(1, state.linePatternScaleFactor);
}
bool CoinRenderPlanAssemblyCore::projectTexcoord(const SbVec4f & uv, float (&output)[2]) {
  if (std::abs(uv[3]) <= 1.0e-8f) return false;
  output[0] = uv[0] / uv[3]; output[1] = uv[1] / uv[3]; return true;
}
void CoinRenderPlanAssemblyCore::sortingCenter(CoinRenderDrawPacket & draw,
                                              const SbMatrix & model, const SbVec3f & local) {
  SbVec3f center;
  model.multVecMatrix(local, center);
  center.getValue(draw.sortingCenterWorld[0], draw.sortingCenterWorld[1], draw.sortingCenterWorld[2]);
  draw.hasSortingCenter = true;
}

uint32_t CoinRenderPlanAssemblyCore::material(CoinRenderFramePlan & plan, const CoinRenderMaterialSnapshot & matSnap) {
  for (size_t i = 0; i < plan.materials.size(); ++i) {
    const auto & m = plan.materials[i];
    if (std::memcmp(&m, &matSnap, sizeof(CoinRenderMaterialSnapshot)) == 0) {
      return static_cast<uint32_t>(i);
    }
  }
  uint32_t materialSlot = static_cast<uint32_t>(plan.materials.size());
  plan.materials.push_back(matSnap);
  return materialSlot;
}

uint32_t CoinRenderPlanAssemblyCore::lighting(CoinRenderFramePlan & plan, const CoinRenderLightingSnapshot & lightSnap) {
  uint32_t lightingSlot = 0;
  bool lightFound = false;
  for (size_t i = 0; i < plan.lightingStates.size(); ++i) {
    const auto & ls = plan.lightingStates[i];
    if (ls.lights.size() == lightSnap.lights.size()) {
      if (ls.ambientIntensity != lightSnap.ambientIntensity ||
          std::memcmp(ls.ambientColor, lightSnap.ambientColor, sizeof(ls.ambientColor)) != 0) continue;
      bool allMatch = true;
      for (size_t k = 0; k < ls.lights.size(); ++k) {
        const CoinRenderLightSourceSnapshot & a = ls.lights[k];
        const CoinRenderLightSourceSnapshot & b = lightSnap.lights[k];
        if (a.sourceRevision != b.sourceRevision ||
            a.sourceModel != b.sourceModel ||
            a.type != b.type || a.intensity != b.intensity ||
            a.cutOffAngle != b.cutOffAngle || a.dropOffRate != b.dropOffRate ||
            std::memcmp(a.color, b.color, sizeof(a.color)) != 0 ||
            std::memcmp(a.direction, b.direction, sizeof(a.direction)) != 0 ||
            std::memcmp(a.position, b.position, sizeof(a.position)) != 0 ||
            std::memcmp(a.attenuation, b.attenuation, sizeof(a.attenuation)) != 0) {
          allMatch = false;
          break;
        }
      }
      if (allMatch) {
        lightingSlot = static_cast<uint32_t>(i);
        lightFound = true;
        break;
      }
    }
  }
  if (!lightFound) {
    lightingSlot = static_cast<uint32_t>(plan.lightingStates.size());
    plan.lightingStates.push_back(lightSnap);
  }

  return lightingSlot;
}

uint32_t CoinRenderPlanAssemblyCore::camera(CoinRenderFramePlan & plan, const CoinRenderCameraSnapshot & camSnap) {
  uint32_t cameraSlot = 0;
  bool camFound = false;
  for (size_t i = 0; i < plan.cameras.size(); ++i) {
    const auto & c = plan.cameras[i];
    if (c.viewMatrix == camSnap.viewMatrix &&
        c.projectionMatrixCoin == camSnap.projectionMatrixCoin &&
        c.isPerspective == camSnap.isPerspective &&
        std::abs(c.nearDistance - camSnap.nearDistance) < 1e-5f &&
        std::abs(c.farDistance - camSnap.farDistance) < 1e-5f) {
      cameraSlot = static_cast<uint32_t>(i);
      camFound = true;
      break;
    }
  }
  if (!camFound) {
    cameraSlot = static_cast<uint32_t>(plan.cameras.size());
    plan.cameras.push_back(camSnap);
  }

  return cameraSlot;
}

uint32_t CoinRenderPlanAssemblyCore::viewport(CoinRenderFramePlan & plan, const CoinRenderViewportSnapshot & vpSnap) {
  uint32_t viewportSlot = 0;
  bool vpFound = false;
  for (size_t i = 0; i < plan.viewports.size(); ++i) {
    const auto & v = plan.viewports[i];
    if (v.x == vpSnap.x && v.y == vpSnap.y && v.width == vpSnap.width && v.height == vpSnap.height) {
      viewportSlot = static_cast<uint32_t>(i);
      vpFound = true;
      break;
    }
  }
  if (!vpFound) {
    viewportSlot = static_cast<uint32_t>(plan.viewports.size());
    plan.viewports.push_back(vpSnap);
  }

  return viewportSlot;
}

uint32_t CoinRenderPlanAssemblyCore::state(CoinRenderFramePlan & plan, StateIndex & byModel, const CoinRenderRenderStateSnapshot & rs) {
  uint32_t rsSlot = 0;
  bool rsFound = false;
  const uint64_t modelKey = modelMatrixKey(rs.model);
  const auto candidates = byModel.find(modelKey);
  const size_t count = candidates == byModel.end() ? 0 : 1 + candidates->second.additional.size();
  for (size_t candidate = 0; candidate < count; ++candidate) {
    const uint32_t i = candidate == 0 ? candidates->second.first : candidates->second.additional[candidate - 1];
    const auto & existing = plan.renderStates[i];
    if (existing.clipPlanesWorld == rs.clipPlanesWorld &&
        existing.materialSlot == rs.materialSlot &&
        existing.shadowGroupSlot == rs.shadowGroupSlot &&
        existing.shadowStyle == rs.shadowStyle &&
        existing.transparentMaterial == rs.transparentMaterial &&
        existing.transparentTexture == rs.transparentTexture &&
        existing.rasterPixels == rs.rasterPixels &&
        existing.rasterTransparent == rs.rasterTransparent &&
        existing.rasterForceBlend == rs.rasterForceBlend &&
        existing.lightingSlot == rs.lightingSlot &&
        existing.lightModel == rs.lightModel &&
        existing.transparencyType == rs.transparencyType &&
        existing.cameraSlot == rs.cameraSlot &&
        existing.viewportSlot == rs.viewportSlot &&
        existing.cullMode == rs.cullMode &&
        existing.frontFace == rs.frontFace &&
        existing.depthTest == rs.depthTest &&
        existing.depthWrite == rs.depthWrite &&
        existing.depthFunction == rs.depthFunction &&
        existing.explicitDepthMask == rs.explicitDepthMask &&
        existing.screenDoorTransparency == rs.screenDoorTransparency &&
        existing.depthRange[0] == rs.depthRange[0] &&
        existing.depthRange[1] == rs.depthRange[1] &&
        existing.polygonOffsetEnabled == rs.polygonOffsetEnabled &&
        existing.polygonOffsetFactor == rs.polygonOffsetFactor &&
        existing.polygonOffsetUnits == rs.polygonOffsetUnits &&
        existing.polygonOffsetSlopeBias == rs.polygonOffsetSlopeBias &&
        existing.polygonOffsetMaxDepth == rs.polygonOffsetMaxDepth &&
        existing.polygonOffsetPrimitiveStyle == rs.polygonOffsetPrimitiveStyle &&
        existing.polygonOffsetStyles == rs.polygonOffsetStyles &&
        existing.fogMode == rs.fogMode &&
        existing.fogStart == rs.fogStart &&
        existing.fogEnd == rs.fogEnd &&
        std::memcmp(existing.fogColor, rs.fogColor, sizeof(rs.fogColor)) == 0 &&
        existing.lineWidth == rs.lineWidth &&
        existing.pointSize == rs.pointSize &&
        coin_render_same_texture_units(existing.extraTextures, rs.extraTextures) &&
        std::memcmp(existing.textureCombines, rs.textureCombines, sizeof(rs.textureCombines)) == 0 &&
        existing.hasTexture == rs.hasTexture &&
        existing.linePattern == rs.linePattern &&
        existing.linePatternScaleFactor == rs.linePatternScaleFactor &&
        existing.polygonLinePattern == rs.polygonLinePattern &&
        (!rs.hasTexture || (
          existing.textureImageSlot == rs.textureImageSlot &&
          existing.samplerSlot == rs.samplerSlot &&
          existing.textureModel == rs.textureModel &&
          std::memcmp(existing.textureBlendColor, rs.textureBlendColor,
                      sizeof(rs.textureBlendColor)) == 0 &&
          existing.textureMatrix == rs.textureMatrix)) &&
        existing.model == rs.model &&
        existing.view == rs.view &&
        existing.projectionCoin == rs.projectionCoin) {
      rsSlot = static_cast<uint32_t>(i);
      rsFound = true;
      break;
    }
  }
  if (!rsFound) {
    rsSlot = static_cast<uint32_t>(plan.renderStates.size());
    plan.renderStates.push_back(rs);
    if (candidates == byModel.end()) byModel.emplace(modelKey, StateCandidates(rsSlot));
    else candidates->second.additional.push_back(rsSlot);
  }
  return rsSlot;
}

uint32_t CoinRenderPlanAssemblyCore::texture(CoinRenderFramePlan & plan, CoinRenderTextureImageSnapshot && image) {
  for (size_t i = 0; i < plan.textures.size(); ++i) {
    const auto & existing = plan.textures[i];
    if (existing.width == image.width && existing.height == image.height &&
        existing.contentDigest == image.contentDigest && existing.producerId == image.producerId &&
        existing.sceneTransparencyFunction == image.sceneTransparencyFunction && existing.pixelsRgba == image.pixelsRgba)
      return static_cast<uint32_t>(i);
  }
  const auto slot = static_cast<uint32_t>(plan.textures.size());
  plan.textures.push_back(std::move(image)); return slot;
}

uint32_t CoinRenderPlanAssemblyCore::sampler(CoinRenderFramePlan & plan, const CoinRenderSamplerSnapshot & sampSnap) {
  uint32_t sampSlot = UINT32_MAX;
  for (size_t i = 0; i < plan.samplers.size(); ++i) {
    const auto & s = plan.samplers[i];
    if (s.wrapS == sampSnap.wrapS &&
        s.wrapT == sampSnap.wrapT &&
        s.filter == sampSnap.filter) {
      sampSlot = static_cast<uint32_t>(i);
      break;
    }
  }
  if (sampSlot == UINT32_MAX) {
    sampSlot = static_cast<uint32_t>(plan.samplers.size());
    plan.samplers.push_back(sampSnap);
  }

  return sampSlot;
}

void CoinRenderPlanAssemblyCore::makeIndicesAppendable(CoinRenderFramePlan & plan, CoinRenderGeometryRange & range) {
      if (size_t(range.firstIndex) + range.indexCount != plan.indices.size()) {
        const size_t first = range.firstIndex, count = range.indexCount;
        const size_t tail = plan.indices.size();
        plan.indices.resize(tail + count);
        std::copy_n(plan.indices.begin() + first, count,
                    plan.indices.begin() + tail);
        range.firstIndex = static_cast<uint32_t>(tail);
      }
}

void CoinRenderPlanAssemblyCore::appendPolygon(CoinRenderFramePlan & plan, CoinRenderDrawPacket & draw, CoinRenderPolygonStyleResult & resolved) {
  const uint32_t first = static_cast<uint32_t>(plan.vertices.size());
  for (auto& item : resolved.vertices) {
    const uint32_t material = CoinRenderPlanAssemblyCore::material(plan, item.material);
    item.vertex.materialSlot = material;
    plan.vertices.push_back(item.vertex);
  }
  for (uint32_t index : resolved.indices)
    plan.indices.push_back(first + index);
  draw.geometry.vertexCount = static_cast<uint32_t>(resolved.vertices.size());
  draw.geometry.indexCount = static_cast<uint32_t>(resolved.indices.size());
}

void CoinRenderPlanAssemblyCore::appendIndexed(CoinRenderFramePlan & plan, CoinRenderDrawPacket & packet, const CoinRenderIndexedGeometryResult & transformed) {
  const uint32_t vertexOffset =
    static_cast<uint32_t>(plan.vertices.size());
  for (size_t i = 0; i < transformed.vertices.size(); ++i) {
    plan.vertices.push_back(transformed.vertices[i].vertex);
  }
  for (size_t i = 0; i < transformed.indices.size(); ++i) {
    plan.indices.push_back(
      vertexOffset + transformed.indices[i]);
  }

  packet.geometry.vertexCount =
    static_cast<uint32_t>(transformed.vertices.size());
  packet.geometry.indexCount =
    static_cast<uint32_t>(transformed.indices.size());
}

void CoinRenderCubeGeometryCore::reset(bool useTemplateCache) {
  templates.clear(); templateCache = useTemplateCache; disabled = false;
  clock = templateEvictions = rangeEvictions = rangeReuseHits = 0;
}

const CoinRenderCubeGeometryCore::Template * CoinRenderCubeGeometryCore::find(
  const float (&dimensions)[3], int normalBinding) const {
  if (disabled) return nullptr;
  for (const auto & entry : templates)
    if (entry.normalBinding == normalBinding &&
        std::memcmp(entry.dimensions, dimensions, sizeof(entry.dimensions)) == 0) return &entry;
  return nullptr;
}
CoinRenderCubeGeometryCore::Template * CoinRenderCubeGeometryCore::find(
  const float (&dimensions)[3], int normalBinding) {
  return const_cast<Template *>(static_cast<const CoinRenderCubeGeometryCore *>(this)->find(dimensions, normalBinding));
}
size_t CoinRenderCubeGeometryCore::rangeCount() const {
  size_t count = 0;
  for (const auto & entry : templates) count += entry.meshes.size();
  return count;
}
void CoinRenderCubeGeometryCore::rememberRange(Template & entry, uint32_t material, uint32_t vertex, uint32_t index) {
  for (auto & range : entry.meshes) if (range.materialSlot == material) {
    range.lastUsed = clock; return;
  }
  const size_t limit = templateCache ? RANGE_LIMIT : LEGACY_RANGE_LIMIT;
  if (entry.meshes.size() < limit) entry.meshes.emplace_back(material, vertex, index, clock);
  else if (templateCache) {
    auto oldest = std::min_element(entry.meshes.begin(), entry.meshes.end(),
      [](const Geometry & a, const Geometry & b) { return a.lastUsed < b.lastUsed; });
    *oldest = Geometry(material, vertex, index, clock); ++rangeEvictions;
  }
}

void CoinRenderCubeGeometryCore::learn(const CoinRenderFramePlan & plan, size_t firstVertex, size_t firstIndex,
  size_t firstDraw, const float (&dimensions)[3], int normalBinding) {
  static_assert(sizeof(Template) * TEMPLATE_LIMIT + sizeof(Geometry) * TEMPLATE_LIMIT * RANGE_LIMIT < 256 * 1024,
                "Cube template metadata and captured payload must stay below 256 KiB");
  if (disabled || clock == UINT64_MAX || firstVertex > plan.vertices.size() ||
      plan.vertices.size() - firstVertex != 24 || firstIndex > plan.indices.size() ||
      plan.indices.size() - firstIndex != 36 || firstDraw > plan.draws.size() ||
      plan.draws.size() - firstDraw != 1) return;
  const uint32_t material = plan.vertices[firstVertex].materialSlot;
  for (size_t i = 0; i < 24; ++i) if (plan.vertices[firstVertex + i].materialSlot != material) return;
  for (size_t i = 0; i < 36; ++i)
    if (plan.indices[firstIndex + i] < firstVertex || plan.indices[firstIndex + i] >= firstVertex + 24) return;
  try {
    Template * entry = find(dimensions, normalBinding);
    // A complete native snapshot must agree with an existing key, apart from
    // its uniform material slot. An unexpected attribute change declines reuse.
    if (entry && templateCache) {
      for (size_t i = 0; i < 24; ++i) {
        auto vertex = plan.vertices[firstVertex + i]; vertex.materialSlot = entry->vertices[i].materialSlot;
        if (std::memcmp(&vertex, &entry->vertices[i], sizeof(vertex)) != 0) {
          templates.clear(); disabled = true; return;
        }
      }
      for (size_t i = 0; i < 36; ++i) if (plan.indices[firstIndex + i] - firstVertex != entry->indices[i]) {
        templates.clear(); disabled = true; return;
      }
    }
    if (!entry || !templateCache) {
      const size_t limit = templateCache ? TEMPLATE_LIMIT : 1;
      if (templates.capacity() < limit) templates.reserve(limit);
      if (templates.size() < limit) { templates.emplace_back(); entry = &templates.back(); }
      else {
        entry = &*std::min_element(templates.begin(), templates.end(),
          [](const Template & a, const Template & b) { return a.lastUsed < b.lastUsed; });
        ++templateEvictions;
      }
      entry->meshes.clear();
      entry->meshes.reserve(templateCache ? RANGE_LIMIT : LEGACY_RANGE_LIMIT);
      std::copy_n(plan.vertices.begin() + firstVertex, 24, entry->vertices);
      for (size_t i = 0; i < 36; ++i) entry->indices[i] = plan.indices[firstIndex + i] - static_cast<uint32_t>(firstVertex);
      std::copy_n(dimensions, 3, entry->dimensions); entry->normalBinding = normalBinding;
    }
    entry->lastUsed = ++clock;
    rememberRange(*entry, material, static_cast<uint32_t>(firstVertex), static_cast<uint32_t>(firstIndex));
  } catch (const std::bad_alloc &) { templates.clear(); disabled = true; }
}

bool CoinRenderCubeGeometryCore::matches(const float (&dimensions)[3], int normalBinding) const {
  return clock != UINT64_MAX && find(dimensions, normalBinding);
}
bool CoinRenderCubeGeometryCore::replay(CoinRenderFramePlan & plan, CoinRenderDrawPacket & draw, uint32_t materialSlot,
  const float (&dimensions)[3], int normalBinding) {
  Template * entry = find(dimensions, normalBinding);
  if (!entry || clock == UINT64_MAX) return false;
  entry->lastUsed = ++clock;
  if (draw.geometry.indexCount == 0) {
    for (auto & geometry : entry->meshes) {
      if (geometry.materialSlot != materialSlot) continue;
      draw.geometry.firstVertex = geometry.firstVertex;
      draw.geometry.vertexCount = 24;
      draw.geometry.firstIndex = geometry.firstIndex;
      draw.geometry.indexCount = 36;
      geometry.lastUsed = clock; ++rangeReuseHits;
      return true;
    }
  }
  // Consecutive identical cubes still form one draw. Repeat its indices,
  // rather than widening the vertex range through unrelated shared meshes.
  if (draw.geometry.vertexCount == 24) {
    for (auto & geometry : entry->meshes) {
      if (geometry.materialSlot != materialSlot || geometry.firstVertex != draw.geometry.firstVertex) continue;
      CoinRenderPlanAssemblyCore::makeIndicesAppendable(plan, draw.geometry);
      for (const uint32_t index : entry->indices)
        plan.indices.push_back(geometry.firstVertex + index);
      draw.geometry.indexCount += 36;
      geometry.lastUsed = clock; ++rangeReuseHits;
      return true;
    }
    // LRU eviction removes metadata, never an earlier packet's payload. A
    // packet can still own this exact 24-vertex range after its cache row left.
    const auto & range = draw.geometry;
    bool exact = templateCache && size_t(range.firstVertex) + 24 <= plan.vertices.size() && range.indexCount >= 36 &&
      size_t(range.firstIndex) + 36 <= plan.indices.size();
    for (size_t i = 0; exact && i < 24; ++i) {
      auto expected = entry->vertices[i]; expected.materialSlot = materialSlot;
      exact = std::memcmp(&plan.vertices[range.firstVertex + i], &expected, sizeof(expected)) == 0;
    }
    for (size_t i = 0; exact && i < 36; ++i)
      exact = plan.indices[range.firstIndex + i] == range.firstVertex + entry->indices[i];
    if (exact) {
      CoinRenderPlanAssemblyCore::makeIndicesAppendable(plan, draw.geometry);
      for (const uint32_t index : entry->indices) plan.indices.push_back(draw.geometry.firstVertex + index);
      draw.geometry.indexCount += 36; ++rangeReuseHits;
      return true;
    }
  }
  const bool newDraw = draw.geometry.indexCount == 0;
  const uint32_t base = static_cast<uint32_t>(plan.vertices.size());
  const uint32_t firstIndex = static_cast<uint32_t>(plan.indices.size());
  plan.vertices.insert(plan.vertices.end(),
      entry->vertices, entry->vertices + 24);
  for (size_t i = base; i < plan.vertices.size(); ++i)
    plan.vertices[i].materialSlot = materialSlot;
  for (const uint32_t index : entry->indices)
    plan.indices.push_back(base + index);
  draw.geometry.vertexCount = static_cast<uint32_t>(plan.vertices.size()) - draw.geometry.firstVertex;
  draw.geometry.indexCount += 36;
  if (newDraw) {
    try { rememberRange(*entry, materialSlot, base, firstIndex); }
    catch (const std::bad_alloc &) { templates.clear(); disabled = true; }
  }
  return true;
}
