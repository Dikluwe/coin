#include "rendering/coinrender/CoinRenderPlanAssemblyCore.h"
#include <utility>
#include "rendering/coinrender/CoinRenderStateCore.h"
#include "rendering/coinrender/CoinRenderPolygonStyleCore.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace {
const size_t INTERN_INDEX_THRESHOLD = 32;

struct InternCandidates {
  InternCandidates() : first(UINT32_MAX) {}
  explicit InternCandidates(uint32_t slot) : first(slot) {}
  uint32_t first;
  std::vector<uint32_t> additional;
};

class InternIndex : public SbHash<unsigned long, InternCandidates> {
public:
  InternIndex() : SbHash<unsigned long, InternCandidates>(256), indexed(0),
                  lastSlot(UINT32_MAX), disabled(false) {}
  void reset() {
    this->clear();
    this->indexed = 0;
    this->lastSlot = UINT32_MAX;
    this->disabled = false;
  }
  void abandon() {
    this->clear();
    this->indexed = 0;
    this->lastSlot = UINT32_MAX;
    this->disabled = true;
  }
  size_t indexed;
  uint32_t lastSlot;
  bool disabled;
};

uint64_t hashBytes(uint64_t hash, const void * data, size_t size)
{
  const unsigned char * bytes = static_cast<const unsigned char *>(data);
  for (size_t i = 0; i < size; ++i)
    hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
  return hash;
}

template <typename T>
uint64_t hashValue(uint64_t hash, const T & value)
{
  return hashBytes(hash, &value, sizeof(value));
}

uint64_t hashFloat(uint64_t hash, float value)
{
  // SbMatrix and scalar equality treat the two zero representations alike.
  if (value == 0.0f) value = 0.0f;
  return hashValue(hash, value);
}

uint64_t hashMatrix(uint64_t hash, const SbMatrix & matrix)
{
  for (int row = 0; row < 4; ++row)
    for (int column = 0; column < 4; ++column)
      hash = hashFloat(hash, matrix[row][column]);
  return hash;
}

unsigned long narrowHash(uint64_t hash)
{
#if ULONG_MAX == 0xffffffffUL
  hash ^= hash >> 32;
#endif
  return static_cast<unsigned long>(hash);
}

uint64_t hashMaterial64(const CoinRenderMaterialSnapshot & material)
{
  return hashBytes(UINT64_C(14695981039346656037), &material, sizeof(material));
}

bool sameMaterial(const CoinRenderMaterialSnapshot & a, const CoinRenderMaterialSnapshot & b)
{
  return std::memcmp(&a, &b, sizeof(a)) == 0;
}

uint64_t hashLight64(uint64_t hash, const CoinRenderLightSourceSnapshot & light)
{
  hash = hashValue(hash, light.sourceRevision);
  hash = hashMatrix(hash, light.sourceModel);
  hash = hashMatrix(hash, light.sourceWorldModel);
  hash = hashValue(hash, light.hasAuthoredSource);
  hash = hashBytes(hash, light.authoredDirection, sizeof(light.authoredDirection));
  hash = hashBytes(hash, light.authoredPosition, sizeof(light.authoredPosition));
  hash = hashValue(hash, light.type);
  hash = hashFloat(hash, light.intensity);
  hash = hashFloat(hash, light.ambientIntensity);
  hash = hashFloat(hash, light.cutOffAngle);
  hash = hashFloat(hash, light.dropOffRate);
  hash = hashBytes(hash, light.color, sizeof(light.color));
  hash = hashBytes(hash, light.direction, sizeof(light.direction));
  hash = hashBytes(hash, light.position, sizeof(light.position));
  return hashBytes(hash, light.attenuation, sizeof(light.attenuation));
}

bool sameLight(const CoinRenderLightSourceSnapshot & a, const CoinRenderLightSourceSnapshot & b)
{
  return a.sourceRevision == b.sourceRevision && a.sourceModel == b.sourceModel &&
    a.sourceWorldModel == b.sourceWorldModel && a.hasAuthoredSource == b.hasAuthoredSource &&
    std::memcmp(a.authoredDirection, b.authoredDirection, sizeof(a.authoredDirection)) == 0 &&
    std::memcmp(a.authoredPosition, b.authoredPosition, sizeof(a.authoredPosition)) == 0 &&
    a.type == b.type && a.intensity == b.intensity &&
    a.ambientIntensity == b.ambientIntensity && a.cutOffAngle == b.cutOffAngle &&
    a.dropOffRate == b.dropOffRate &&
    std::memcmp(a.color, b.color, sizeof(a.color)) == 0 &&
    std::memcmp(a.direction, b.direction, sizeof(a.direction)) == 0 &&
    std::memcmp(a.position, b.position, sizeof(a.position)) == 0 &&
    std::memcmp(a.attenuation, b.attenuation, sizeof(a.attenuation)) == 0;
}

uint64_t hashLighting64(const CoinRenderLightingSnapshot & lighting)
{
  uint64_t hash = UINT64_C(14695981039346656037);
  hash = hashFloat(hash, lighting.ambientIntensity);
  hash = hashBytes(hash, lighting.ambientColor, sizeof(lighting.ambientColor));
  hash = hashValue(hash, lighting.lights.size());
  for (const auto & light : lighting.lights) hash = hashLight64(hash, light);
  return hash;
}

bool sameLighting(const CoinRenderLightingSnapshot & a, const CoinRenderLightingSnapshot & b)
{
  if (a.ambientIntensity != b.ambientIntensity ||
      std::memcmp(a.ambientColor, b.ambientColor, sizeof(a.ambientColor)) != 0 ||
      a.lights.size() != b.lights.size()) return false;
  for (size_t i = 0; i < a.lights.size(); ++i)
    if (!sameLight(a.lights[i], b.lights[i])) return false;
  return true;
}

uint64_t hashCamera64(const CoinRenderCameraSnapshot & camera)
{
  uint64_t hash = hashMatrix(UINT64_C(14695981039346656037), camera.viewMatrix);
  hash = hashMatrix(hash, camera.projectionMatrixCoin);
  hash = hashValue(hash, camera.isPerspective);
  hash = hashFloat(hash, camera.nearDistance);
  hash = hashFloat(hash, camera.farDistance);
  hash = hashFloat(hash, camera.focalDistance);
  return hashFloat(hash, camera.aspectRatio);
}

bool sameCamera(const CoinRenderCameraSnapshot & a, const CoinRenderCameraSnapshot & b)
{
  return a.viewMatrix == b.viewMatrix && a.projectionMatrixCoin == b.projectionMatrixCoin &&
    a.isPerspective == b.isPerspective && a.nearDistance == b.nearDistance &&
    a.farDistance == b.farDistance && a.focalDistance == b.focalDistance &&
    a.aspectRatio == b.aspectRatio;
}

uint64_t hashViewport64(const CoinRenderViewportSnapshot & viewport)
{
  uint64_t hash = hashValue(UINT64_C(14695981039346656037), viewport.x);
  hash = hashValue(hash, viewport.y);
  hash = hashValue(hash, viewport.width);
  return hashValue(hash, viewport.height);
}

bool sameViewport(const CoinRenderViewportSnapshot & a, const CoinRenderViewportSnapshot & b)
{
  return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

uint64_t hashTexture64(const CoinRenderTextureImageSnapshot & texture)
{
  uint64_t hash = UINT64_C(14695981039346656037);
  hash = hashValue(hash, texture.format);
  hash = hashValue(hash, texture.cubeFaceSize);
  hash = hashValue(hash, texture.cubeSourceComponents);
  hash = hashValue(hash, texture.cubeMipmapped);
  hash = hashValue(hash, texture.volumeWidth);
  hash = hashValue(hash, texture.volumeHeight);
  hash = hashValue(hash, texture.volumeDepth);
  hash = hashValue(hash, texture.volumeColumns);
  hash = hashValue(hash, texture.volumeSourceComponents);
  hash = hashValue(hash, texture.volumeMipmapped);
  hash = hashValue(hash, texture.width);
  hash = hashValue(hash, texture.height);
  hash = hashValue(hash, texture.components);
  hash = hashValue(hash, texture.contentDigest);
  hash = hashValue(hash, texture.producerId);
  hash = hashValue(hash, texture.gpuToken);
  hash = hashValue(hash, texture.sceneTransparencyFunction);
  hash = hashValue(hash, texture.gpuOpaque);
  hash = hashValue(hash, texture.mipmapped);
  hash = hashValue(hash, texture.mipmapsRgba.size());
  if (!texture.mipmapsRgba.empty())
    hash = hashBytes(hash, texture.mipmapsRgba.data(), texture.mipmapsRgba.size());
  hash = hashValue(hash, texture.pixelsRgba.size());
  if (!texture.pixelsRgba.empty())
    hash = hashBytes(hash, texture.pixelsRgba.data(), texture.pixelsRgba.size());
  return hash;
}

bool sameTexture(const CoinRenderTextureImageSnapshot & a, const CoinRenderTextureImageSnapshot & b)
{
  return a.format == b.format && a.cubeFaceSize == b.cubeFaceSize &&
    a.cubeSourceComponents == b.cubeSourceComponents && a.cubeMipmapped == b.cubeMipmapped &&
    a.volumeWidth == b.volumeWidth && a.volumeHeight == b.volumeHeight &&
    a.volumeDepth == b.volumeDepth && a.volumeColumns == b.volumeColumns &&
    a.volumeSourceComponents == b.volumeSourceComponents && a.volumeMipmapped == b.volumeMipmapped &&
    a.width == b.width && a.height == b.height && a.components == b.components &&
    a.contentDigest == b.contentDigest && a.producerId == b.producerId &&
    a.gpuToken == b.gpuToken && a.sceneTransparencyFunction == b.sceneTransparencyFunction &&
    a.gpuOpaque == b.gpuOpaque && a.mipmapped == b.mipmapped &&
    a.mipmapsRgba == b.mipmapsRgba && a.pixelsRgba == b.pixelsRgba;
}

uint64_t hashSampler64(const CoinRenderSamplerSnapshot & sampler)
{
  uint64_t hash = hashValue(UINT64_C(14695981039346656037), sampler.wrapS);
  hash = hashValue(hash, sampler.wrapT);
  hash = hashValue(hash, sampler.wrapR);
  hash = hashValue(hash, sampler.filter);
  return hashValue(hash, sampler.maxAnisotropy);
}

bool sameSampler(const CoinRenderSamplerSnapshot & a, const CoinRenderSamplerSnapshot & b)
{
  return a.wrapS == b.wrapS && a.wrapT == b.wrapT && a.wrapR == b.wrapR &&
    a.filter == b.filter && a.maxAnisotropy == b.maxAnisotropy;
}

template <typename Value, typename Hash, typename Equal>
bool synchronize(InternIndex & index, const std::vector<Value> & values, Hash hash, Equal equal)
{
  (void) equal;
  if (index.disabled) return false;
  try {
    if (index.indexed > values.size()) index.reset();
    while (index.indexed < values.size()) {
      const uint32_t slot = static_cast<uint32_t>(index.indexed);
      InternCandidates & candidates = index[narrowHash(hash(values[slot]))];
      if (candidates.first == UINT32_MAX) candidates.first = slot;
      else candidates.additional.push_back(slot);
      ++index.indexed;
    }
  } catch (const std::bad_alloc &) {
    index.abandon();
    return false;
  }
  return true;
}

template <typename Value, typename Hash, typename Equal>
uint32_t findValue(const std::vector<Value> & values, InternIndex * index,
                   const Value & value, Hash hash, Equal equal)
{
  if (index && synchronize(*index, values, hash, equal)) {
    if (index->lastSlot < values.size() && equal(values[index->lastSlot], value))
      return index->lastSlot;
    const auto found = index->find(narrowHash(hash(value)));
    if (found != index->const_end()) {
      const InternCandidates & candidates = found->obj;
      if (candidates.first != UINT32_MAX && equal(values[candidates.first], value)) {
        index->lastSlot = candidates.first;
        return candidates.first;
      }
      for (uint32_t slot : candidates.additional) {
        if (equal(values[slot], value)) {
          index->lastSlot = slot;
          return slot;
        }
      }
    }
    return UINT32_MAX;
  }
  for (size_t i = 0; i < values.size(); ++i)
    if (equal(values[i], value)) return static_cast<uint32_t>(i);
  return UINT32_MAX;
}
}

struct CoinRenderFrameInternIndexes {
  explicit CoinRenderFrameInternIndexes(const CoinRenderFramePlan * frame) : owner(frame) {}
  const CoinRenderFramePlan * owner;
  InternIndex materials, lighting, cameras, viewports, textures, samplers;
};

namespace {
CoinRenderFrameInternIndexes * frameIndexes(CoinRenderFramePlan & plan)
{
  try {
    if (!plan.internIndexes || plan.internIndexes->owner != &plan)
      plan.internIndexes = std::make_shared<CoinRenderFrameInternIndexes>(&plan);
    return plan.internIndexes.get();
  } catch (const std::bad_alloc &) {
    plan.internIndexes.reset();
    return nullptr;
  }
}

template <typename Value, typename Hash, typename Equal>
uint32_t appendValue(std::vector<Value> & values, InternIndex * index,
                     const Value & value, Hash hash, Equal equal)
{
  const uint32_t existing = findValue(values, index, value, hash, equal);
  if (existing != UINT32_MAX) return existing;
  const uint32_t slot = static_cast<uint32_t>(values.size());
  values.push_back(value);
  if (index && synchronize(*index, values, hash, equal)) index->lastSlot = slot;
  return slot;
}

template <typename Value, typename Equal>
bool appendSmallValue(std::vector<Value> & values, const Value & value,
                      Equal equal, uint32_t & slot)
{
  if (values.size() >= INTERN_INDEX_THRESHOLD) return false;
  for (size_t i = 0; i < values.size(); ++i) {
    if (equal(values[i], value)) {
      slot = static_cast<uint32_t>(i);
      return true;
    }
  }
  slot = static_cast<uint32_t>(values.size());
  values.push_back(value);
  return true;
}
}

void CoinRenderPlanAssemblyCore::normalizeCamera(CoinRenderCameraSnapshot & camera) {
  // Preserve non-finite source values for validation instead of repairing them.
  if (camera.isPerspective && std::isfinite(camera.nearDistance) && camera.nearDistance <= 0)
    camera.nearDistance = 0.1f;
  if (std::isfinite(camera.nearDistance) && std::isfinite(camera.farDistance) &&
      camera.farDistance <= camera.nearDistance)
    camera.farDistance = camera.nearDistance + 100.0f;
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
  if (std::isfinite(state.screenDoorTransparency))
    state.screenDoorTransparency = std::max(0.0f, std::min(1.0f, state.screenDoorTransparency));
  if (std::isfinite(state.fogEnd) && state.fogEnd <= 0)
    state.fogEnd = camera.farDistance;
  if (std::isfinite(state.lineWidth) && state.lineWidth <= 0) state.lineWidth = 1;
  if (std::isfinite(state.pointSize) && state.pointSize <= 0) state.pointSize = 1;
  state.linePattern &= 0xffffu;
  state.linePatternScaleFactor = std::max(1, state.linePatternScaleFactor);
}
void CoinRenderPlanAssemblyCore::sortingCenter(CoinRenderDrawPacket & draw,
                                              const SbMatrix & model, const SbVec3f & local) {
  SbVec3f center;
  model.multVecMatrix(local, center);
  center.getValue(draw.sortingCenterWorld[0], draw.sortingCenterWorld[1], draw.sortingCenterWorld[2]);
  draw.hasSortingCenter = true;
}

uint32_t CoinRenderPlanAssemblyCore::material(CoinRenderFramePlan & plan, const CoinRenderMaterialSnapshot & matSnap) {
  uint32_t slot;
  if (appendSmallValue(plan.materials, matSnap, sameMaterial, slot)) return slot;
  CoinRenderFrameInternIndexes * indexes = frameIndexes(plan);
  return appendValue(plan.materials, indexes ? &indexes->materials : nullptr, matSnap,
                     hashMaterial64, sameMaterial);
}

uint32_t CoinRenderPlanAssemblyCore::lighting(CoinRenderFramePlan & plan, const CoinRenderLightingSnapshot & lightSnap) {
  uint32_t slot;
  if (appendSmallValue(plan.lightingStates, lightSnap, sameLighting, slot)) return slot;
  CoinRenderFrameInternIndexes * indexes = frameIndexes(plan);
  return appendValue(plan.lightingStates, indexes ? &indexes->lighting : nullptr, lightSnap,
                     hashLighting64, sameLighting);
}

uint32_t CoinRenderPlanAssemblyCore::camera(CoinRenderFramePlan & plan, const CoinRenderCameraSnapshot & camSnap) {
  uint32_t slot;
  if (appendSmallValue(plan.cameras, camSnap, sameCamera, slot)) return slot;
  CoinRenderFrameInternIndexes * indexes = frameIndexes(plan);
  return appendValue(plan.cameras, indexes ? &indexes->cameras : nullptr, camSnap,
                     hashCamera64, sameCamera);
}

uint32_t CoinRenderPlanAssemblyCore::viewport(CoinRenderFramePlan & plan, const CoinRenderViewportSnapshot & vpSnap) {
  uint32_t slot;
  if (appendSmallValue(plan.viewports, vpSnap, sameViewport, slot)) return slot;
  CoinRenderFrameInternIndexes * indexes = frameIndexes(plan);
  return appendValue(plan.viewports, indexes ? &indexes->viewports : nullptr, vpSnap,
                     hashViewport64, sameViewport);
}

uint32_t CoinRenderPlanAssemblyCore::state(CoinRenderFramePlan & plan, StateIndex & byModel, const CoinRenderRenderStateSnapshot & rs) {
  if (byModel.lastSlot < plan.renderStates.size() &&
      coin_render_same_state(plan.renderStates[byModel.lastSlot], rs))
    return byModel.lastSlot;
  uint32_t rsSlot = 0;
  bool rsFound = false;
  const unsigned long stateKey = coin_render_state_hash(rs);
  StateCandidates & candidates = byModel[stateKey];
  const size_t count = candidates.first == UINT32_MAX ? 0 : 1 + candidates.additional.size();
  for (size_t candidate = 0; candidate < count; ++candidate) {
    const uint32_t i = candidate == 0 ? candidates.first : candidates.additional[candidate - 1];
    if (coin_render_same_state(plan.renderStates[i], rs)) {
      rsSlot = static_cast<uint32_t>(i);
      rsFound = true;
      break;
    }
  }
  if (!rsFound) {
    rsSlot = static_cast<uint32_t>(plan.renderStates.size());
    plan.renderStates.push_back(rs);
    if (candidates.first == UINT32_MAX) candidates.first = rsSlot;
    else candidates.additional.push_back(rsSlot);
  }
  byModel.lastSlot = rsSlot;
  return rsSlot;
}

uint32_t CoinRenderPlanAssemblyCore::texture(CoinRenderFramePlan & plan, CoinRenderTextureImageSnapshot && image) {
  if (plan.textures.size() < INTERN_INDEX_THRESHOLD) {
    for (size_t i = 0; i < plan.textures.size(); ++i)
      if (sameTexture(plan.textures[i], image)) return static_cast<uint32_t>(i);
    const uint32_t slot = static_cast<uint32_t>(plan.textures.size());
    plan.textures.push_back(std::move(image));
    return slot;
  }
  CoinRenderFrameInternIndexes * indexes = frameIndexes(plan);
  InternIndex * index = indexes ? &indexes->textures : nullptr;
  const uint32_t existing = findValue(plan.textures, index, image, hashTexture64, sameTexture);
  if (existing != UINT32_MAX) return existing;
  const uint32_t slot = static_cast<uint32_t>(plan.textures.size());
  plan.textures.push_back(std::move(image));
  if (index && synchronize(*index, plan.textures, hashTexture64, sameTexture))
    index->lastSlot = slot;
  return slot;
}

uint32_t CoinRenderPlanAssemblyCore::sampler(CoinRenderFramePlan & plan, const CoinRenderSamplerSnapshot & sampSnap) {
  uint32_t slot;
  if (appendSmallValue(plan.samplers, sampSnap, sameSampler, slot)) return slot;
  CoinRenderFrameInternIndexes * indexes = frameIndexes(plan);
  return appendValue(plan.samplers, indexes ? &indexes->samplers : nullptr, sampSnap,
                     hashSampler64, sameSampler);
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
