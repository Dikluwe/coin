#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include "rendering/coinrender/CoinRenderTransformCore.h"
#include "rendering/coinrender/CoinRenderClipCore.h"

#include <Inventor/SbMatrix.h>
#include "rendering/coinrender/CoinRenderComposition.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cstdio>

static_assert(sizeof(CoinWgpuFrameView) == 416, "Frame view ABI size changed");
static_assert(sizeof(CoinWgpuShadowPassView) == 56, "Extra shadow pass ABI size changed");
static_assert(offsetof(CoinWgpuFrameView, extra_shadow_passes) == 400,
              "Extra shadow pass ABI offset changed");
static_assert(offsetof(CoinWgpuFrameView, shadow_epsilon_second) == 376,
              "Frame view per-pass VSM ABI offset changed");
static_assert(sizeof(CoinWgpuShadowDraw) == 144, "Shadow draw ABI size changed");
static_assert(sizeof(CoinWgpuShadowReceiver) == 144, "Shadow receiver ABI size changed");
static_assert(offsetof(CoinWgpuShadowReceiver, max_shadow_distance) == 8,
              "Shadow distance ABI offset changed");
static_assert(offsetof(CoinWgpuFrameView, shadow_receivers) == 216,
              "Shadow receivers ABI offset changed");
static_assert(offsetof(CoinWgpuFrameView, shadow_casters_second) == 232,
              "Second shadow casters ABI offset changed");
static_assert(offsetof(CoinWgpuFrameView, shadow_receivers_second) == 264,
              "Second shadow receivers ABI offset changed");
static_assert(offsetof(CoinWgpuFrameView, shadow_casters) == 176,
              "Shadow casters ABI offset changed");
static_assert(offsetof(CoinWgpuFrameView, shadow_kind) == 212,
              "Shadow kind ABI offset changed");
static_assert(offsetof(CoinWgpuFrameView, shadow_map_size) == 192,
              "Shadow map ABI offset changed");
static_assert(offsetof(CoinWgpuFrameView, sorted_layers_passes) == 160,
              "Layer count ABI offset changed");
static_assert(offsetof(CoinWgpuFrameView, transparency_reserved) == 164,
              "Transparency reserved ABI offset changed");
static_assert(offsetof(CoinWgpuFrameView, transparency_budget_bytes) == 168,
              "Transparency budget ABI offset changed");
static_assert(sizeof(CoinWgpuTextureUnit) == 96, "Texture unit ABI size changed");
static_assert(offsetof(CoinWgpuVertex, extra_texcoords) == 44, "Extra UV ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, extra_textures) == 1096, "Extra textures ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, texture_combines) == 1768, "Combine ABI offset changed");
static_assert(sizeof(CoinWgpuRenderState) == 2280, "CoinWgpuRenderState ABI size changed");
static_assert(offsetof(CoinWgpuRenderState, polygon_offset_max_depth_bits) == 1092, "Maximum depth ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, polygon_offset_slope_bias) == 1088, "Slope bias ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, polygon_offset_enabled) == 936, "Polygon offset ABI tail changed");
static_assert(offsetof(CoinWgpuRenderState, depth_test) == 916, "depth_test ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, depth_write) == 920, "depth_write ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, depth_function) == 924, "depth_function ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, depth_range) == 928, "depth_range ABI offset changed");

namespace {
// Only the view-space coordinates of captured lights may change with a camera.
// Type, color, intensity, cutoff and attenuation remain part of the immutable key.
CoinWgpuRenderState opaqueCameraKey(CoinWgpuRenderState key)
{
  std::memset(key.model_view, 0, sizeof(key.model_view));
  std::memset(key.model_view_projection, 0, sizeof(key.model_view_projection));
  std::memset(key.normal_matrix, 0, sizeof(key.normal_matrix));
  key.material_slot = 0;
  key.fog_end = 0;
  for (int c = 0; c < 3; ++c) key.light_direction[c] = 0;
  for (auto & light : key.lights) {
    for (int c = 0; c < 3; ++c) {
      light.position_type[c] = 0;
      light.direction_cutoff[c] = 0;
    }
  }
  return key;
}

bool patchableBakedVertex(const SbVec3f & position, const SbVec3f & normal)
{
  for (int c = 0; c < 3; ++c) {
    if (!std::isfinite(position[c]) || !std::isfinite(normal[c]) ||
        std::abs(normal[c]) > 1.0e18f) return false;
  }
  return true;
}

void packVertex(const CoinRenderVertexSnapshot & src, CoinWgpuVertex & dst)
{
  std::memcpy(dst.position, src.position, sizeof(src.position));
  std::memcpy(dst.normal, src.normal, sizeof(src.normal));
  std::memcpy(dst.texcoord, src.texcoord, sizeof(src.texcoord));
  dst.material_slot = src.materialSlot;
  std::memcpy(dst.extra_texcoords, src.extraTexcoords, sizeof(dst.extra_texcoords));
  dst.screen_space_w = src.screenSpaceW;
  dst.fog_eye_depth_plus_one = src.fogEyeDepth >= 0 ? src.fogEyeDepth + 1.0f : 0.0f;
}

template <typename T>
bool sameOpaqueInput(const std::vector<T> & previous, const std::vector<T> & current)
{
  return previous.size() == current.size() && (current.empty() ||
    std::memcmp(previous.data(), current.data(), current.size() * sizeof(T)) == 0);
}
}

CoinWgpuFfiFrame::CoinWgpuFfiFrame()
  : packedRevision(0), reused(false), opaqueBatched(false),
    opaqueCameraPatchable(false), opaqueGeometryPatchable(false), opaqueCameraState{}, opaqueSourceVertices(0),
    opaqueSourceIndices(0), opaqueSourceDraws(0), opaqueSourceStates(0),
    opaqueTargetWidth(0), opaqueTargetHeight(0), opaqueClearColor{},
    prepareKind(CoinRenderFrameReuseKind::UNKNOWN), view{}
{
}

bool
CoinWgpuFfiFrame::prepare(const CoinRenderFramePlan & frame, uint32_t width, uint32_t height,
                        std::string & outDiagnostic)
{
  return this->prepare(frame, width, height,
    CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD, 0),
    outDiagnostic);
}

bool
CoinWgpuFfiFrame::prepare(const CoinRenderFramePlan & frame, uint32_t width, uint32_t height,
                        const CoinRenderFrameReuseDecision & reuse,
                        std::string & outDiagnostic,
                        const CoinRenderFramePreflight * preflight)
{
  outDiagnostic.clear();
  this->opaqueIncrementalUsed = false;
  this->opaqueRebakedRanges = this->opaqueRebakedVertices = 0;
  this->opaqueIncrementalCandidate = false;
  // Failed qualification/packing must never leave an old revision or geometry
  // proof available to a retry. Successful exact reuse retains the proof.
  struct PrepareGuard {
    bool & valid;
    uint64_t & revision;
    CoinRenderFrameReuseKind & kind;
    bool committed;
    PrepareGuard(bool & v, uint64_t & r, CoinRenderFrameReuseKind & k)
      : valid(v), revision(r), kind(k), committed(false) {}
    ~PrepareGuard() {
      if (!committed) {
        valid = false;
        revision = 0;
        kind = CoinRenderFrameReuseKind::UNKNOWN;
      }
    }
  } guard(this->opaqueIncrementalValid, this->packedRevision, this->prepareKind);
  for (const auto& texture : frame.textures) {
    if (texture.producerId) {
      outDiagnostic = "Unresolved scene texture producer at wgpu execution boundary";
      return false;
    }
  }
  if (frame.revision != 0 && frame.revision == this->packedRevision) {
    uint64_t requiredBytes = 0;
    const bool needsPeeling =
        std::any_of(this->draws.begin(), this->draws.end(), [](const CoinWgpuDraw& draw) {
          return (draw.composition_flags & (1u << 3)) != 0;
        });
    if (!coin_render_transparency_budget(width, height, frame.transparency, needsPeeling,
                                         requiredBytes, outDiagnostic))
      return false;
    this->reused = true;
    this->prepareKind = CoinRenderFrameReuseKind::REUSE;
    this->view.camera_base_revision = 0;
    this->view.width = width;
    this->view.height = height;
    this->view.sorted_layers_passes = frame.transparency.layers;
    this->view.transparency_budget_bytes = frame.transparency.bufferBudget;
    guard.committed = true;
    return true;
  }
  this->reused = false;
  CoinWgpuShadowFrame candidateShadow;
  if (!candidateShadow.prepare(frame, outDiagnostic)) return false;

  if (this->patchOpaqueCamera(frame, width, height, reuse)) {
    // The output still belongs to the camera anchor, while the captured states
    // now contain a different view. Do not mix it with a later object rebake.
    this->opaqueIncrementalValid = false;
    this->shadowFrame = std::move(candidateShadow);
    this->bindView(frame, width, height);
    this->view.camera_base_revision = reuse.baseRevision;
    this->packedRevision = frame.revision;
    this->prepareKind = CoinRenderFrameReuseKind::CAMERA_PATCH;
    guard.committed = true;
    return true;
  }

  // The Rust camera patch owns only the standard immutable payload; shadow
  // casters/receivers must be transported through the full validated path.
  if (!this->opaqueBatched && frame.shadowGroups.empty() &&
      reuse.kind == CoinRenderFrameReuseKind::CAMERA_PATCH &&
      frame.revision != 0 && frame.revision != reuse.baseRevision &&
      reuse.baseRevision != 0 && reuse.baseRevision == this->packedRevision) {
    const auto previousDraws = this->draws;
    this->packedRevision = 0;
    if (!this->packStates(frame, candidateShadow, width, height, outDiagnostic, preflight)) return false;
    const bool sameOrder = previousDraws.size() == this->draws.size() &&
      (previousDraws.empty() || std::memcmp(previousDraws.data(), this->draws.data(),
        previousDraws.size() * sizeof(CoinWgpuDraw)) == 0);
    this->shadowFrame = std::move(candidateShadow);
    this->bindView(frame, width, height);
    this->view.camera_base_revision = sameOrder ? reuse.baseRevision : 0;
    this->packedRevision = frame.revision;
    this->prepareKind = CoinRenderFrameReuseKind::CAMERA_PATCH;
    this->opaqueIncrementalValid = false;
    guard.committed = true;
    return true;
  }

  this->packedRevision = 0;
  this->opaqueBatched = false;
  this->opaqueCameraPatchable = false;
  this->opaqueGeometryPatchable = false;
  this->materials.resize(frame.materials.size());
  for (size_t i = 0; i < frame.materials.size(); ++i) {
    const CoinRenderMaterialSnapshot & src = frame.materials[i];
    CoinWgpuMaterial & dst = this->materials[i];
    std::memcpy(dst.ambient, src.ambient, sizeof(src.ambient));
    std::memcpy(dst.diffuse, src.diffuse, sizeof(src.diffuse));
    std::memcpy(dst.specular, src.specular, sizeof(src.specular));
    std::memcpy(dst.emission, src.emission, sizeof(src.emission));
    dst.shininess = src.shininess;
    dst.transparency = src.transparency;
  }

  if (this->tryEarlyOpaqueBatch(frame, width, height, preflight)) {
    this->rememberOpaqueCamera(frame, width, height);
    this->textures.clear();
    this->texturePixels.clear();
    this->samplers.clear();
    this->shadowFrame = std::move(candidateShadow);
    this->bindView(frame, width, height);
    this->packedRevision = frame.revision;
    this->prepareKind = reuse.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD
      ? CoinRenderFrameReuseKind::RESOURCE_REBUILD
      : CoinRenderFrameReuseKind::FULL_REBUILD;
    this->opaqueIncrementalValid = this->opaqueIncrementalCandidate;
    guard.committed = true;
    return true;
  }
  this->opaqueIncrementalValid = false;
  this->vertices.resize(frame.vertices.size());
  for (size_t i = 0; i < frame.vertices.size(); ++i)
    packVertex(frame.vertices[i], this->vertices[i]);
  this->indices = frame.indices;
  if (!this->packStates(frame, candidateShadow, width, height, outDiagnostic, preflight)) return false;

  this->texturePixels.resize(frame.textures.size());
  this->textures.assign(frame.textures.size(), CoinWgpuTexture{});
  for (size_t i = 0; i < frame.textures.size(); ++i) {
    const CoinRenderTextureImageSnapshot & src = frame.textures[i];
    CoinWgpuTexture & dst = this->textures[i];
    this->texturePixels[i] = src.pixelsRgba;
    dst.width = src.width;
    dst.height = src.height;
    dst.format = src.gpuToken ? 1 : 0;
    dst.reserved = src.gpuToken && src.gpuOpaque ? 1 : 0;
    dst.content_digest = src.gpuToken ? src.gpuToken : src.contentDigest;
    dst.pixels = src.gpuToken ? NULL : this->texturePixels[i].data();
    dst.pixel_bytes_len = static_cast<uint64_t>(this->texturePixels[i].size());
  }

  this->samplers.assign(frame.samplers.size(), CoinWgpuSampler{});
  for (size_t i = 0; i < frame.samplers.size(); ++i) {
    this->samplers[i].wrap_s = static_cast<uint32_t>(frame.samplers[i].wrapS);
    this->samplers[i].wrap_t = static_cast<uint32_t>(frame.samplers[i].wrapT);
    this->samplers[i].filter = static_cast<uint32_t>(frame.samplers[i].filter);
  }

  this->batchOpaqueTriangles(frame);
  this->rememberOpaqueCamera(frame, width, height);

  this->shadowFrame = std::move(candidateShadow);
  this->bindView(frame, width, height);
  this->packedRevision = frame.revision;
  this->prepareKind = reuse.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD
    ? CoinRenderFrameReuseKind::RESOURCE_REBUILD
    : CoinRenderFrameReuseKind::FULL_REBUILD;
  guard.committed = true;
  return true;
}

void
CoinWgpuFfiFrame::rememberOpaqueCamera(const CoinRenderFramePlan & frame,
                                      uint32_t width, uint32_t height)
{
  this->opaqueCameraPatchable = false;
  if (!this->opaqueBatched || !this->opaqueGeometryPatchable || this->states.size() != 1 || frame.renderStates.empty() ||
      frame.revision == 0 || !frame.shadowGroups.empty()) return;
  const auto & first = frame.renderStates.front();
  if (!CoinRenderTransformCore::cameraReuseView(first.view)) return;
  for (const auto & state : frame.renderStates) {
    // Near-singular normals use Core's identity fallback and cannot be rotated
    // from a baked reference. Keep those valid frames on the existing rebake path.
    const SbMatrix modelView = state.model * state.view;
    const float det = modelView.det4();
    if (!CoinRenderTransformCore::finiteMatrix(state.model) || !std::isfinite(det) ||
        state.model[0][3] != 0 || state.model[1][3] != 0 ||
        state.model[2][3] != 0 || state.model[3][3] != 1 ||
        std::abs(det) <= 1.0e-9f ||
        std::memcmp(state.view.getValue(), first.view.getValue(), sizeof(float) * 16) != 0 ||
        std::memcmp(state.projectionCoin.getValue(), first.projectionCoin.getValue(), sizeof(float) * 16) != 0 ||
        state.lightModel != first.lightModel || state.lightingSlot != first.lightingSlot ||
        state.fogMode != CoinRenderFogMode::NONE || state.hasTexture ||
        !state.clipPlanesWorld.empty() || state.polygonOffsetEnabled) return;
    const SbMatrix inverse = modelView.inverse();
    const SbMatrix residual = modelView * inverse;
    if (!CoinRenderTransformCore::finiteMatrix(inverse) ||
        !CoinRenderTransformCore::finiteMatrix(residual)) return;
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col) {
        if (std::abs(residual[row][col] - (row == col ? 1.0f : 0.0f)) > 1.0e-4f) return;
      }
    }
  }
  if (first.lightModel != CoinRenderLightModel::PHONG &&
      first.lightModel != CoinRenderLightModel::BASE_COLOR) return;
  this->opaqueCameraAnchor = first.view;
  this->opaqueCameraState = opaqueCameraKey(this->states.front());
  this->opaqueSourceVertices = frame.vertices.size();
  this->opaqueSourceIndices = frame.indices.size();
  this->opaqueSourceDraws = frame.draws.size();
  this->opaqueSourceStates = frame.renderStates.size();
  this->opaqueTargetWidth = width; this->opaqueTargetHeight = height;
  std::memcpy(this->opaqueClearColor, frame.clearColor.getValue(), sizeof(this->opaqueClearColor));
  this->opaqueCameraPatchable = true;
}

bool
CoinWgpuFfiFrame::patchOpaqueCamera(const CoinRenderFramePlan & frame, uint32_t width,
                                   uint32_t height, const CoinRenderFrameReuseDecision & reuse)
{
  const char * disabled = std::getenv("COIN_WGPU_DISABLE_OPAQUE_CAMERA_PATCH");
  if ((disabled && std::strcmp(disabled, "1") == 0) || !this->opaqueCameraPatchable ||
      !this->opaqueBatched || reuse.kind != CoinRenderFrameReuseKind::CAMERA_PATCH ||
      !reuse.baseRevision || reuse.baseRevision != this->packedRevision ||
      !frame.revision || frame.revision == reuse.baseRevision || !frame.shadowGroups.empty() ||
      frame.vertices.size() != this->opaqueSourceVertices || frame.indices.size() != this->opaqueSourceIndices ||
      frame.draws.size() != this->opaqueSourceDraws || frame.renderStates.size() != this->opaqueSourceStates ||
      frame.materials.size() != this->materials.size() || !frame.textures.empty() || !frame.samplers.empty() ||
      width != this->opaqueTargetWidth || height != this->opaqueTargetHeight ||
      std::memcmp(this->opaqueClearColor, frame.clearColor.getValue(), sizeof(this->opaqueClearColor)) != 0)
    return false;
  const auto & first = frame.renderStates.front();
  if (!CoinRenderTransformCore::cameraReuseView(first.view) || !CoinRenderTransformCore::finiteMatrix(first.projectionCoin)) return false;
  for (const auto & state : frame.renderStates) {
    if (std::memcmp(state.view.getValue(), first.view.getValue(), sizeof(float) * 16) != 0 ||
        std::memcmp(state.projectionCoin.getValue(), first.projectionCoin.getValue(), sizeof(float) * 16) != 0)
      return false;
  }
  CoinWgpuRenderState updated{};
  std::string diagnostic;
  if (!this->packState(frame, first, width, height, updated, diagnostic)) return false;
  const auto key = opaqueCameraKey(updated);
  if (std::memcmp(&key, &this->opaqueCameraState, sizeof(key)) != 0) return false;
  SbMatrix delta, normal;
  if (!CoinRenderTransformCore::cameraDelta(this->opaqueCameraAnchor, first.view, delta, normal)) return false;
  const SbMatrix mvp = delta * CoinRenderTransformCore::projection(first.projectionCoin, false);
  if (!CoinRenderTransformCore::finiteMatrix(delta) || !CoinRenderTransformCore::finiteMatrix(normal) ||
      !CoinRenderTransformCore::finiteMatrix(mvp)) return false;
  std::memcpy(updated.model_view, delta.getValue(), sizeof(updated.model_view));
  std::memcpy(updated.normal_matrix, normal.getValue(), sizeof(updated.normal_matrix));
  std::memcpy(updated.model_view_projection, mvp.getValue(), sizeof(updated.model_view_projection));
  updated.material_slot = 0;
  this->states.front() = updated;
  return true;
}

// Qualify without allocating one 2280-byte GPU state per occurrence. Every
// captured state still passes the ordinary packer; only its two bake matrices
// survive the scan. Publication happens after all draws and states qualify.
// Unsupported profiles keep the full pack-then-batch mechanism below.
bool
CoinWgpuFfiFrame::tryEarlyOpaqueBatch(const CoinRenderFramePlan & frame,
                                    uint32_t width, uint32_t height,
                                    const CoinRenderFramePreflight * preflight)
{
  for (const char * option : {"COIN_WGPU_DISABLE_OPAQUE_BATCHING", "COIN_WGPU_DISABLE_EARLY_OPAQUE_BATCHING"}) {
    const char * value = std::getenv(option);
    if (value && std::strcmp(value, "1") == 0) return false;
  }
  if (frame.draws.size() < 256 || !frame.shadowGroups.empty() ||
      !frame.textures.empty() || !frame.samplers.empty() || frame.renderStates.empty()) return false;
  // Reject common heterogeneous profiles before scheduling or packing twice.
  // These captured-value checks are deliberately conservative; the packed
  // byte comparison below remains the authority for a successful batch.
  const auto & firstState = frame.renderStates.front();
  for (const auto & state : frame.renderStates) {
    if (state.hasTexture || state.fogMode != CoinRenderFogMode::NONE ||
        !state.clipPlanesWorld.empty() || state.polygonOffsetEnabled ||
        state.cullMode != firstState.cullMode || state.frontFace != firstState.frontFace ||
        state.lightModel != firstState.lightModel || state.lightingSlot != firstState.lightingSlot ||
        state.viewportSlot != firstState.viewportSlot || state.depthTest != firstState.depthTest ||
        state.depthWrite != firstState.depthWrite || state.depthFunction != firstState.depthFunction ||
        state.depthRange[0] != firstState.depthRange[0] || state.depthRange[1] != firstState.depthRange[1]) return false;
    for (const SbMatrix * matrix : {&state.model, &state.view}) {
      const auto & values = matrix->getValue();
      if (values[0][3] != 0 || values[1][3] != 0 || values[2][3] != 0 || values[3][3] != 1) return false;
    }
  }
  std::vector<CoinRenderCompositionItem> order;
  std::string diagnostic;
  if (!coin_render_composition_schedule(frame, order, diagnostic, preflight) ||
      order.size() != frame.draws.size() || order.size() < 256) return false;
  uint64_t nextVertex = 0, nextIndex = 0;
  for (const auto & item : order) {
    const auto & draw = frame.draws[item.drawIndex];
    const auto & range = draw.geometry;
    if (draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST ||
        draw.stableNodeId || item.blend || item.screenDoor || item.screenDoorLevel ||
        draw.renderLayer || draw.clearDepthBefore || !range.vertexCount ||
        !range.indexCount || range.indexCount % 3 ||
        draw.renderStateSlot >= frame.renderStates.size() ||
        item.firstIndex != range.firstIndex || item.indexCount != range.indexCount) return false;
    const auto & state = frame.renderStates[draw.renderStateSlot];
    if (item.depthTest != state.depthTest || item.depthWrite != state.depthWrite ||
        item.depthFunction != state.depthFunction ||
        item.depthRange[0] != state.depthRange[0] || item.depthRange[1] != state.depthRange[1]) return false;
    nextVertex += range.vertexCount; nextIndex += range.indexCount;
    const uint64_t vertexEnd = uint64_t(range.firstVertex) + range.vertexCount;
    const uint64_t indexEnd = uint64_t(range.firstIndex) + range.indexCount;
    if (vertexEnd > frame.vertices.size() || indexEnd > frame.indices.size() ||
        nextVertex > UINT32_MAX || nextIndex > UINT32_MAX) return false;
    for (uint64_t v = range.firstVertex; v < vertexEnd; ++v)
      if (frame.vertices[v].screenSpaceW != 1.0f ||
          frame.vertices[v].fogEyeDepth >= 0.0f) return false;
    for (uint64_t j = range.firstIndex; j < indexEnd; ++j)
      if (frame.indices[j] < range.firstVertex || frame.indices[j] >= vertexEnd) return false;
  }
  this->bakeMatrices.resize(frame.renderStates.size());
  auto & matrices = this->bakeMatrices;
  CoinWgpuRenderState common{};
  const SbMatrix identity = SbMatrix::identity();
  // Including unreferenced states is conservative: their validation and errors
  // remain observable; different unused states simply select the fallback.
  for (size_t i = 0; i < frame.renderStates.size(); ++i) {
    CoinWgpuRenderState key{};
    if (!this->packState(frame, frame.renderStates[i], width, height, key, diagnostic)) return false;
    if (key.has_texture || key.fog_mode || key.clip_plane_count || key.polygon_offset_enabled ||
        key.model_view[3] != 0 || key.model_view[7] != 0 ||
        key.model_view[11] != 0 || key.model_view[15] != 1) return false;
    std::memcpy(matrices[i].modelView, key.model_view, sizeof(key.model_view));
    std::memcpy(matrices[i].normal, key.normal_matrix, sizeof(key.normal_matrix));
    key.material_slot = 0;
    std::memcpy(key.model_view, identity.getValue(), sizeof(key.model_view));
    std::memcpy(key.normal_matrix, identity.getValue(), sizeof(key.normal_matrix));
    const SbMatrix projection = CoinRenderTransformCore::projection(frame.renderStates[i].projectionCoin, false);
    std::memcpy(key.model_view_projection, projection.getValue(), sizeof(key.model_view_projection));
    if (i == 0) common = key;
    else if (std::memcmp(&common, &key, sizeof(key)) != 0) return false;
  }
  uint64_t requiredBytes = 0;
  if (!coin_render_transparency_budget(width, height, frame.transparency, false,
                                       requiredBytes, diagnostic)) return false;
  // Admission is based on the exact source contents and layout, not revision
  // IDs or RESOURCE_REBUILD (which guarantees only an execution structure).
  // Bound the source proof plus occurrence/matrix tables to 16 MiB. A large
  // unique mesh keeps the persistent full-bake arena without retaining a copy.
  size_t remaining = 16u * 1024u * 1024u;
  const auto fits = [&remaining](size_t count, size_t stride) {
    if (count > remaining / stride) return false;
    remaining -= count * stride;
    return true;
  };
  const char * disabled = std::getenv("COIN_WGPU_DISABLE_INCREMENTAL_OPAQUE_BATCH");
  bool eligible = !(disabled && std::strcmp(disabled, "1") == 0) && frame.revision != 0 &&
    fits(frame.vertices.size(), sizeof(CoinRenderVertexSnapshot)) &&
    fits(frame.indices.size(), sizeof(uint32_t)) &&
    fits(frame.materials.size(), sizeof(CoinRenderMaterialSnapshot)) &&
    fits(frame.renderStates.size(), sizeof(BakeMatrices) + sizeof(uint32_t)) &&
    fits(order.size(), sizeof(OpaqueRange));
  // A unique common view/projection makes the baked coordinate space explicit.
  // Mixed-view batches remain valid, but take the ordinary full bake.
  for (const auto & state : frame.renderStates) {
    if (std::memcmp(state.view.getValue(), firstState.view.getValue(), sizeof(float) * 16) ||
        std::memcmp(state.projectionCoin.getValue(), firstState.projectionCoin.getValue(), sizeof(float) * 16)) {
      eligible = false;
      break;
    }
  }
  bool incremental = eligible && this->opaqueIncrementalValid &&
    this->opaquePreviousMatrices.size() == matrices.size() &&
    this->opaqueRanges.size() == order.size() &&
    this->opaqueStateMaterialSlots.size() == frame.renderStates.size() &&
    this->vertices.size() == nextVertex && this->indices.size() == nextIndex &&
    this->opaqueIncrementalWidth == width && this->opaqueIncrementalHeight == height &&
    std::memcmp(&this->opaqueIncrementalState, &common, sizeof(common)) == 0 &&
    std::memcmp(this->opaqueIncrementalView.getValue(), firstState.view.getValue(), sizeof(float) * 16) == 0 &&
    std::memcmp(this->opaqueIncrementalProjection.getValue(), firstState.projectionCoin.getValue(), sizeof(float) * 16) == 0 &&
    sameOpaqueInput(this->opaqueInputVertices, frame.vertices) &&
    sameOpaqueInput(this->opaqueInputIndices, frame.indices) &&
    sameOpaqueInput(this->opaqueInputMaterials, frame.materials);
  for (size_t i = 0; incremental && i < order.size(); ++i) {
    const auto & item = order[i];
    const auto & draw = frame.draws[item.drawIndex];
    const auto & previous = this->opaqueRanges[i];
    incremental = previous.drawIndex == item.drawIndex &&
      previous.stateSlot == draw.renderStateSlot && previous.drawOrdinal == draw.drawOrdinal &&
      std::memcmp(&previous.geometry, &draw.geometry, sizeof(draw.geometry)) == 0;
  }
  for (size_t i = 0; incremental && i < frame.renderStates.size(); ++i)
    incremental = this->opaqueStateMaterialSlots[i] == frame.renderStates[i].materialSlot;

  // All qualifications completed before touching output or cache contents.
  this->opaqueIncrementalValid = false;
  this->vertices.resize(static_cast<size_t>(nextVertex));
  this->indices.resize(static_cast<size_t>(nextIndex));
  bool geometryPatchable = true;
  size_t outputVertex = 0, outputIndex = 0;
  for (const auto & item : order) {
    const auto & draw = frame.draws[item.drawIndex];
    const auto & range = draw.geometry;
    const bool rebake = !incremental || std::memcmp(
      &matrices[draw.renderStateSlot], &this->opaquePreviousMatrices[draw.renderStateSlot],
      sizeof(BakeMatrices)) != 0;
    SbMatrix modelView, normal;
    if (rebake) {
      modelView.setValue(matrices[draw.renderStateSlot].modelView);
      normal.setValue(matrices[draw.renderStateSlot].normal);
      ++this->opaqueRebakedRanges;
      this->opaqueRebakedVertices += range.vertexCount;
      for (uint32_t v = 0; v < range.vertexCount; ++v) {
        const auto & sourceVertex = frame.vertices[range.firstVertex + v];
        auto & vertex = this->vertices[outputVertex + v];
        packVertex(sourceVertex, vertex);
        SbVec3f position, direction;
        modelView.multVecMatrix(SbVec3f(sourceVertex.position), position);
        normal.multDirMatrix(SbVec3f(sourceVertex.normal), direction);
        geometryPatchable = geometryPatchable && patchableBakedVertex(position, direction);
        std::memcpy(vertex.position, position.getValue(), sizeof(vertex.position));
        std::memcpy(vertex.normal, direction.getValue(), sizeof(vertex.normal));
      }
    }
    if (!incremental) {
      for (uint32_t j = 0; j < range.indexCount; ++j)
        this->indices[outputIndex + j] = static_cast<uint32_t>(outputVertex) +
          frame.indices[range.firstIndex + j] - range.firstVertex;
    }
    outputVertex += range.vertexCount;
    outputIndex += range.indexCount;
  }
  const auto & source = frame.draws[order.front().drawIndex];
  CoinWgpuDraw merged{};
  merged.vertex_count = static_cast<uint32_t>(nextVertex);
  merged.index_count = static_cast<uint32_t>(nextIndex);
  merged.draw_ordinal = source.drawOrdinal;
  merged.source_revision = source.sourceRevision;
  this->draws.assign(1, merged);
  this->states.assign(1, common);
  this->opaqueBatched = true;
  this->opaqueGeometryPatchable = geometryPatchable;
  this->opaqueIncrementalUsed = incremental;
  this->opaqueIncrementalCandidate = eligible && geometryPatchable;
  const auto releaseCache = [this]() {
    std::vector<BakeMatrices>().swap(this->opaquePreviousMatrices);
    std::vector<OpaqueRange>().swap(this->opaqueRanges);
    std::vector<uint32_t>().swap(this->opaqueStateMaterialSlots);
    std::vector<CoinRenderVertexSnapshot>().swap(this->opaqueInputVertices);
    std::vector<uint32_t>().swap(this->opaqueInputIndices);
    std::vector<CoinRenderMaterialSnapshot>().swap(this->opaqueInputMaterials);
  };
  if (this->opaqueIncrementalCandidate) {
    if (!incremental) {
      // Different scene shapes must not accumulate the largest capacity of
      // every source table. Reserve exact growth before resize/assignment.
      size_t capacityBudget = 16u * 1024u * 1024u;
      const auto capacityFits = [&capacityBudget](size_t capacity, size_t size, size_t stride) {
        const size_t count = std::max(capacity, size);
        if (count > capacityBudget / stride) return false;
        capacityBudget -= count * stride;
        return true;
      };
      if (!capacityFits(this->opaquePreviousMatrices.capacity(), matrices.size(), sizeof(BakeMatrices)) ||
          !capacityFits(this->opaqueRanges.capacity(), order.size(), sizeof(OpaqueRange)) ||
          !capacityFits(this->opaqueStateMaterialSlots.capacity(), frame.renderStates.size(), sizeof(uint32_t)) ||
          !capacityFits(this->opaqueInputVertices.capacity(), frame.vertices.size(), sizeof(CoinRenderVertexSnapshot)) ||
          !capacityFits(this->opaqueInputIndices.capacity(), frame.indices.size(), sizeof(uint32_t)) ||
          !capacityFits(this->opaqueInputMaterials.capacity(), frame.materials.size(), sizeof(CoinRenderMaterialSnapshot)))
        releaseCache();
      this->opaqueInputVertices = frame.vertices;
      this->opaqueInputIndices = frame.indices;
      this->opaqueInputMaterials = frame.materials;
      this->opaqueRanges.reserve(order.size());
      this->opaqueStateMaterialSlots.reserve(frame.renderStates.size());
      this->opaqueRanges.resize(order.size());
      this->opaqueStateMaterialSlots.resize(frame.renderStates.size());
      for (size_t i = 0; i < order.size(); ++i) {
        const auto & draw = frame.draws[order[i].drawIndex];
        this->opaqueRanges[i] = {order[i].drawIndex, draw.geometry, draw.renderStateSlot, draw.drawOrdinal};
      }
      for (size_t i = 0; i < frame.renderStates.size(); ++i)
        this->opaqueStateMaterialSlots[i] = frame.renderStates[i].materialSlot;
      this->opaqueIncrementalWidth = width; this->opaqueIncrementalHeight = height;
      this->opaqueIncrementalView = firstState.view;
      this->opaqueIncrementalProjection = firstState.projectionCoin;
      this->opaqueIncrementalState = common;
    }
    this->opaquePreviousMatrices = matrices;
  }
  else releaseCache();
  if (std::getenv("COIN_RENDER_TRACE_PHASES")) {
    std::fprintf(stderr, "COIN_RENDER_PHASE wgpu_opaque_batch incremental=%u ranges=%zu rebaked_ranges=%zu rebaked_vertices=%zu rebaked_vertex_bytes=%llu index_bytes_written=%llu\n",
      incremental ? 1u : 0u, order.size(), this->opaqueRebakedRanges, this->opaqueRebakedVertices,
      static_cast<unsigned long long>(this->opaqueRebakedVertices) * sizeof(CoinWgpuVertex),
      incremental ? 0ull : static_cast<unsigned long long>(nextIndex) * sizeof(uint32_t));
  }
  return true;
}

void
CoinWgpuFfiFrame::batchOpaqueTriangles(const CoinRenderFramePlan & frame)
{
  // Preserve composition order. Native triangle ranges with one effective
  // state can become a single draw, including shared capture geometry.
  // Materials stay indexed per vertex; strokes, shadows, alpha and offsets
  // retain the regular encoder. The switch is for same-binary qualification.
  const char * disabled = std::getenv("COIN_WGPU_DISABLE_OPAQUE_BATCHING");
  if ((disabled && std::strcmp(disabled, "1") == 0) || this->draws.size() < 256 ||
      !frame.shadowGroups.empty() || !this->textures.empty() || !this->samplers.empty()) return;
  CoinWgpuRenderState common{};
  uint64_t nextVertex = 0, nextIndex = 0;
  bool contiguous = true;
  for (size_t i = 0; i < this->draws.size(); ++i) {
    const auto & draw = this->draws[i];
    if (draw.topology != 0 || draw.stable_node_id || draw.composition_flags ||
        draw.render_layer || draw.clear_depth_before || !draw.vertex_count ||
        !draw.index_count || draw.index_count % 3 ||
        draw.render_state_slot >= this->states.size() ||
        draw.render_state_slot >= frame.renderStates.size()) return;
    contiguous = contiguous && draw.first_vertex == nextVertex && draw.first_index == nextIndex;
    nextVertex += draw.vertex_count;
    nextIndex += draw.index_count;
    const uint64_t vertexEnd = uint64_t(draw.first_vertex) + draw.vertex_count;
    const uint64_t indexEnd = uint64_t(draw.first_index) + draw.index_count;
    if (vertexEnd > this->vertices.size() || indexEnd > this->indices.size() ||
        nextVertex > UINT32_MAX || nextIndex > UINT32_MAX) return;
    const auto & state = this->states[draw.render_state_slot];
    if (state.has_texture || state.fog_mode || state.clip_plane_count ||
        state.polygon_offset_enabled ||
        state.model_view[3] != 0 || state.model_view[7] != 0 ||
        state.model_view[11] != 0 || state.model_view[15] != 1) return;
    for (uint64_t v = draw.first_vertex; v < vertexEnd; ++v)
      if (this->vertices[v].screen_space_w != 1.0f ||
          this->vertices[v].fog_eye_depth_plus_one != 0.0f) return;
    for (uint64_t j = draw.first_index; j < indexEnd; ++j)
      if (this->indices[j] < draw.first_vertex || this->indices[j] >= vertexEnd) return;
    CoinWgpuRenderState key = state;
    // All three shaders fetch every material value from the vertex slot.
    key.material_slot = 0;
    const SbMatrix identity = SbMatrix::identity();
    std::memcpy(key.model_view, identity.getValue(), sizeof(key.model_view));
    std::memcpy(key.normal_matrix, identity.getValue(), sizeof(key.normal_matrix));
    const SbMatrix projection = CoinRenderTransformCore::projection(
      frame.renderStates[draw.render_state_slot].projectionCoin, false);
    std::memcpy(key.model_view_projection, projection.getValue(), sizeof(key.model_view_projection));
    if (i == 0) common = key;
    else if (std::memcmp(&common, &key, sizeof(key)) != 0) return;
  }
  contiguous = contiguous && nextVertex == this->vertices.size() && nextIndex == this->indices.size();
  // Qualify the whole sequence before baking. Shared vertices need a separate
  // destination because each occurrence has its own model/view transform.
  std::vector<CoinWgpuVertex> expandedVertices;
  std::vector<uint32_t> expandedIndices;
  if (!contiguous) {
    expandedVertices.reserve(static_cast<size_t>(nextVertex));
    expandedIndices.reserve(static_cast<size_t>(nextIndex));
  }
  bool geometryPatchable = true;
  for (const auto & draw : this->draws) {
    const auto & state = this->states[draw.render_state_slot];
    SbMatrix modelView, normalMatrix;
    modelView.setValue(state.model_view);
    normalMatrix.setValue(state.normal_matrix);
    for (uint64_t i = draw.first_vertex; i < uint64_t(draw.first_vertex) + draw.vertex_count; ++i) {
      if (!contiguous) expandedVertices.push_back(this->vertices[i]);
      auto & vertex = contiguous ? this->vertices[i] : expandedVertices.back();
      SbVec3f position, normal;
      modelView.multVecMatrix(SbVec3f(vertex.position), position);
      normalMatrix.multDirMatrix(SbVec3f(vertex.normal), normal);
      geometryPatchable = geometryPatchable && patchableBakedVertex(position, normal);
      std::memcpy(vertex.position, position.getValue(), sizeof(vertex.position));
      std::memcpy(vertex.normal, normal.getValue(), sizeof(vertex.normal));
    }
    if (!contiguous) {
      const uint32_t base = static_cast<uint32_t>(expandedVertices.size()) - draw.vertex_count;
      for (uint64_t j = draw.first_index; j < uint64_t(draw.first_index) + draw.index_count; ++j)
        expandedIndices.push_back(base + this->indices[j] - draw.first_vertex);
    }
  }
  if (!contiguous) {
    this->vertices.swap(expandedVertices);
    this->indices.swap(expandedIndices);
  }
  CoinWgpuDraw merged = this->draws.front();
  merged.first_vertex = merged.first_index = 0;
  merged.vertex_count = static_cast<uint32_t>(nextVertex);
  merged.index_count = static_cast<uint32_t>(nextIndex);
  merged.render_state_slot = 0;
  this->draws.assign(1, merged);
  this->states.assign(1, common);
  this->opaqueBatched = true;
  this->opaqueGeometryPatchable = geometryPatchable;
}

bool
CoinWgpuFfiFrame::packState(const CoinRenderFramePlan & frame,
                           const CoinRenderRenderStateSnapshot & src,
                           uint32_t targetWidth, uint32_t targetHeight,
                           CoinWgpuRenderState & dst, std::string & outDiagnostic)
{
  dst.clip_plane_count = static_cast<uint32_t>(src.clipPlanesWorld.size());
  if (!coin_render_clip_equations(src, dst.clip_planes, outDiagnostic)) return false;
  const SbMatrix modelView = src.model * src.view;
  const SbMatrix normalMatrix = CoinRenderTransformCore::normalMatrix(modelView);
  const SbMatrix projectionWgpu = CoinRenderTransformCore::projection(src.projectionCoin, false);
  const SbMatrix mvpWgpu = modelView * projectionWgpu;

  std::memcpy(dst.model_view, modelView.getValue(), sizeof(float) * 16);
  std::memcpy(dst.model_view_projection, mvpWgpu.getValue(), sizeof(float) * 16);
  std::memcpy(dst.normal_matrix, normalMatrix.getValue(), sizeof(float) * 16);

  bool hasLight = false;
  if (src.lightModel == CoinRenderLightModel::PHONG &&
      src.lightingSlot < frame.lightingStates.size() &&
      !frame.lightingStates[src.lightingSlot].lights.empty()) {
    const CoinRenderLightSourceSnapshot & light = frame.lightingStates[src.lightingSlot].lights[0];
    hasLight = true;
    for (int c = 0; c < 3; ++c) {
      dst.light_direction[c] = light.direction[c];
      dst.light_color[c] = light.color[c];
    }
    dst.light_direction[3] = 0.0f;
    dst.light_color[3] = 1.0f;
    dst.light_intensity = light.intensity;
  } else {
    dst.light_direction[2] = 1.0f;
    dst.light_color[0] = 1.0f;
    dst.light_color[1] = 1.0f;
    dst.light_color[2] = 1.0f;
    dst.light_color[3] = 1.0f;
    dst.light_intensity = 1.0f;
  }
  dst.has_light = hasLight ? 1 : 0;
  dst.material_slot = src.materialSlot;
  dst.cull_mode = static_cast<uint32_t>(src.cullMode);
  dst.front_face = static_cast<uint32_t>(src.frontFace);
  dst.light_model = static_cast<uint32_t>(src.lightModel);
  std::memcpy(dst.texture_matrix, src.textureMatrix.getValue(), sizeof(float) * 16);
  std::memcpy(dst.texture_combines, src.textureCombines, sizeof(dst.texture_combines));
  for (size_t unit = 1; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
    const auto layer = coin_render_texture_unit(src, unit);
    auto& target = dst.extra_textures[unit - 1];
    std::memcpy(target.matrix, layer.matrix.getValue(), sizeof(target.matrix));
    target.enabled = layer.enabled ? 1 : 0;
    target.texture_slot = layer.imageSlot; target.sampler_slot = layer.samplerSlot;
    target.model = static_cast<uint32_t>(layer.model);
    std::memcpy(target.blend_color, layer.blendColor, sizeof(target.blend_color));
  }
  dst.has_texture = src.hasTexture ? 1 : 0;
  dst.texture_slot = src.textureImageSlot;
  dst.sampler_slot = src.samplerSlot;
  dst.texture_model = static_cast<uint32_t>(src.textureModel);
  std::memcpy(dst.texture_blend_color, src.textureBlendColor,
              sizeof(src.textureBlendColor));
  if (src.viewportSlot < frame.viewports.size()) {
    const CoinRenderViewportSnapshot & viewport = frame.viewports[src.viewportSlot];
    dst.viewport[0] = viewport.x;
    dst.viewport[1] = static_cast<int32_t>(targetHeight) - viewport.y - viewport.height;
    dst.viewport[2] = viewport.width;
    dst.viewport[3] = viewport.height;
  } else {
    dst.viewport[0] = dst.viewport[1] = 0;
    dst.viewport[2] = static_cast<int32_t>(targetWidth);
    dst.viewport[3] = static_cast<int32_t>(targetHeight);
  }
  dst.fog_mode = static_cast<uint32_t>(src.fogMode);
  std::memcpy(dst.fog_color, src.fogColor, sizeof(src.fogColor));
  dst.fog_start = src.fogStart;
  dst.fog_end = src.fogEnd;
  dst.depth_test = src.depthTest ? 1u : 0u;
  dst.depth_write = src.depthWrite ? 1u : 0u;
  dst.depth_function = static_cast<uint32_t>(src.depthFunction);
  dst.depth_range[0] = src.depthRange[0];
  dst.depth_range[1] = src.depthRange[1];
  dst.polygon_offset_enabled = src.polygonOffsetEnabled ? 1u : 0u;
  dst.polygon_offset_factor = src.polygonOffsetFactor;
  dst.polygon_offset_units = src.polygonOffsetUnits;
  dst.polygon_offset_slope_bias = src.polygonOffsetSlopeBias;
  dst.polygon_offset_max_depth_bits = 0;
  if (src.polygonOffsetMaxDepth >= 0) {
    std::memcpy(&dst.polygon_offset_max_depth_bits, &src.polygonOffsetMaxDepth, sizeof(float));
    ++dst.polygon_offset_max_depth_bits;
  }
  dst.polygon_offset_styles = src.polygonOffsetStyles;
  dst.polygon_offset_primitive_style = src.polygonOffsetPrimitiveStyle;
  dst.ambient_light[3] = 1.0f;

  if (src.lightingSlot < frame.lightingStates.size()) {
    const CoinRenderLightingSnapshot & lighting = frame.lightingStates[src.lightingSlot];
    for (int c = 0; c < 3; ++c) {
      dst.ambient_light[c] = lighting.ambientColor[c] * lighting.ambientIntensity;
    }
    if (src.lightModel == CoinRenderLightModel::PHONG) {
      if (lighting.lights.size() > COIN_WGPU_FFI_MAX_LIGHTS) {
        outDiagnostic = "More than eight active lights in CoinRenderFramePlan";
        this->packedRevision = 0;
        this->prepareKind = CoinRenderFrameReuseKind::UNKNOWN;
        return false;
      }
      dst.light_count = static_cast<uint32_t>(lighting.lights.size());
      for (size_t j = 0; j < lighting.lights.size(); ++j) {
        const CoinRenderLightSourceSnapshot & light = lighting.lights[j];
        dst.lights[j] = coin_wgpu_pack_light(light);
        if (coin_render_shadow_suppresses_ordinary_light(frame, src, light))
          dst.lights[j].color_intensity[3] = 0.0f;
      }
    }
  }
  return true;
}

bool
CoinWgpuFfiFrame::packStates(const CoinRenderFramePlan & frame,
                             CoinWgpuShadowFrame & shadow,
                             uint32_t targetWidth, uint32_t targetHeight,
                             std::string & outDiagnostic,
                             const CoinRenderFramePreflight * preflight)
{
  this->states.assign(frame.renderStates.size(), CoinWgpuRenderState{});
  for (size_t i = 0; i < frame.renderStates.size(); ++i) {
    const CoinRenderRenderStateSnapshot & src = frame.renderStates[i];
    CoinWgpuRenderState & dst = this->states[i];
    if (!this->packState(frame, src, targetWidth, targetHeight, dst, outDiagnostic)) return false;
  }
  std::vector<CoinRenderCompositionItem> order;
  if (!coin_render_composition_schedule(frame, order, outDiagnostic, preflight))
    return false;
  uint64_t requiredBytes = 0;
  const bool needsPeeling =
      std::any_of(order.begin(), order.end(), [](const CoinRenderCompositionItem& item) {
        return item.blend && item.deferred &&
               item.transparencyStrategy == CoinRenderCompositionItem::SORTED_LAYERS;
      });
  const bool needsWeighted = std::any_of(order.begin(),order.end(),[](const CoinRenderCompositionItem & item) {
    return item.blend && item.deferred && item.transparencyStrategy == CoinRenderCompositionItem::WEIGHTED_OIT;
  });
  auto allocationOptions = frame.transparency;
  if (needsWeighted && !needsPeeling) allocationOptions.layers = 1;
  if (!coin_render_transparency_budget(targetWidth, targetHeight, allocationOptions, needsPeeling || needsWeighted,
                                       requiredBytes, outDiagnostic))
    return false;
  std::vector<CoinWgpuDraw> resolvedDraws;
  resolvedDraws.reserve(order.size());
  for (const auto & item : order) {
    const auto & src = frame.draws[item.drawIndex];
    const auto & state = frame.renderStates[src.renderStateSlot];
    CoinWgpuDraw dst{};
    dst.topology = static_cast<uint32_t>(src.topology);
    dst.first_vertex = src.geometry.firstVertex;
    dst.vertex_count = src.geometry.vertexCount;
    dst.first_index = item.firstIndex;
    dst.index_count = item.indexCount;
    dst.render_state_slot = src.renderStateSlot;
    dst.stable_node_id = item.blend && item.sortTriangles ? 0 : src.stableNodeId;
    dst.draw_ordinal = src.drawOrdinal;
    dst.composition_flags =
        (item.blend ? 1u : 0u) | (item.additive && item.blend ? 2u : 0u) |
        (item.screenDoor ? 4u : 0u) |
        (item.blend && item.deferred &&
                 item.transparencyStrategy == CoinRenderCompositionItem::SORTED_LAYERS
             ? 8u
             : 0u) |
        (item.blend && item.deferred && item.transparencyStrategy == CoinRenderCompositionItem::WEIGHTED_OIT ? 16u : 0u) |
        (item.screenDoorLevel << 8);
    dst.source_revision = src.sourceRevision;
    dst.render_layer = src.renderLayer;
    dst.clear_depth_before = src.clearDepthBefore ? 1u : 0u;
    if (item.depthTest != state.depthTest || item.depthWrite != state.depthWrite ||
        item.depthFunction != state.depthFunction ||
        item.depthRange[0] != state.depthRange[0] || item.depthRange[1] != state.depthRange[1]) {
      CoinWgpuRenderState resolved = this->states[src.renderStateSlot];
      resolved.depth_test = item.depthTest ? 1u : 0u;
      resolved.depth_write = item.depthWrite ? 1u : 0u;
      resolved.depth_function = static_cast<uint32_t>(item.depthFunction);
      resolved.depth_range[0] = item.depthRange[0];
      resolved.depth_range[1] = item.depthRange[1];
      dst.render_state_slot = static_cast<uint32_t>(this->states.size());
      this->states.push_back(resolved);
      shadow.appendReceiverState(src.renderStateSlot);
    }
    resolvedDraws.push_back(dst);
  }
  this->draws.swap(resolvedDraws);
  return true;
}

void
CoinWgpuFfiFrame::bindView(const CoinRenderFramePlan & frame, uint32_t width, uint32_t height)
{
  this->view = CoinWgpuFrameView{};
  this->view.abi_version = COIN_WGPU_ABI_VERSION;
  this->view.struct_size = sizeof(CoinWgpuFrameView);
  this->view.frame_revision = frame.revision;
  this->view.vertices = this->vertices.empty() ? NULL : this->vertices.data();
  this->view.vertex_count = static_cast<uint64_t>(this->vertices.size());
  this->view.indices = this->indices.empty() ? NULL : this->indices.data();
  this->view.index_count = static_cast<uint64_t>(this->indices.size());
  this->view.draws = this->draws.empty() ? NULL : this->draws.data();
  this->view.draw_count = static_cast<uint64_t>(this->draws.size());
  this->view.materials = this->materials.empty() ? NULL : this->materials.data();
  this->view.material_count = static_cast<uint64_t>(this->materials.size());
  this->view.states = this->states.empty() ? NULL : this->states.data();
  this->view.state_count = static_cast<uint64_t>(this->states.size());
  this->view.textures = this->textures.empty() ? NULL : this->textures.data();
  this->view.texture_count = static_cast<uint64_t>(this->textures.size());
  this->view.samplers = this->samplers.empty() ? NULL : this->samplers.data();
  this->view.sampler_count = static_cast<uint64_t>(this->samplers.size());
  for (int c = 0; c < 4; ++c) this->view.clear_color[c] = frame.clearColor[c];
  this->view.width = width;
  this->view.height = height;
  this->view.sorted_layers_passes = frame.transparency.layers;
  this->view.transparency_budget_bytes = frame.transparency.bufferBudget;
  this->view.shadow_casters = this->shadowFrame.casters.empty()
    ? NULL : this->shadowFrame.casters.data();
  this->view.shadow_caster_count =
    static_cast<uint64_t>(this->shadowFrame.casters.size());
  this->view.shadow_map_size = this->shadowFrame.mapSize;
  this->view.shadow_kind = this->shadowFrame.kind;
  this->view.shadow_near_distance = this->shadowFrame.nearDistance;
  this->view.shadow_far_distance = this->shadowFrame.farDistance;
  this->view.shadow_epsilon = this->shadowFrame.epsilon;
  this->view.shadow_threshold = this->shadowFrame.threshold;
  this->view.shadow_receivers = this->shadowFrame.receivers.empty()
    ? NULL : this->shadowFrame.receivers.data();
  this->view.shadow_receiver_count =
    static_cast<uint64_t>(this->shadowFrame.receivers.size());
  if (this->shadowFrame.hasSecond) {
    const auto & second = this->shadowFrame.second;
    this->view.shadow_casters_second = second.casters.data();
    this->view.shadow_caster_count_second = static_cast<uint64_t>(second.casters.size());
    this->view.shadow_map_size_second = second.mapSize;
    this->view.shadow_near_distance_second = second.nearDistance;
    this->view.shadow_far_distance_second = second.farDistance;
    this->view.shadow_kind_second = second.kind;
    this->view.shadow_epsilon_second = second.epsilon;
    this->view.shadow_threshold_second = second.threshold;
    this->view.shadow_receivers_second = second.receivers.data();
    this->view.shadow_receiver_count_second = static_cast<uint64_t>(second.receivers.size());
  }
  if (this->shadowFrame.hasThird) {
    const auto & pass = this->shadowFrame.third;
    this->view.shadow_casters_third = pass.casters.data();
    this->view.shadow_caster_count_third = static_cast<uint64_t>(pass.casters.size());
    this->view.shadow_map_size_third = pass.mapSize;
    this->view.shadow_near_distance_third = pass.nearDistance;
    this->view.shadow_far_distance_third = pass.farDistance;
    this->view.shadow_kind_third = pass.kind;
    this->view.shadow_epsilon_third = pass.epsilon;
    this->view.shadow_threshold_third = pass.threshold;
    this->view.shadow_receivers_third = pass.receivers.data();
    this->view.shadow_receiver_count_third = static_cast<uint64_t>(pass.receivers.size());
  }
  if (this->shadowFrame.hasFourth) {
    const auto & pass = this->shadowFrame.fourth;
    this->view.shadow_casters_fourth = pass.casters.data();
    this->view.shadow_caster_count_fourth = static_cast<uint64_t>(pass.casters.size());
    this->view.shadow_map_size_fourth = pass.mapSize;
    this->view.shadow_near_distance_fourth = pass.nearDistance;
    this->view.shadow_far_distance_fourth = pass.farDistance;
    this->view.shadow_kind_fourth = pass.kind;
    this->view.shadow_epsilon_fourth = pass.epsilon;
    this->view.shadow_threshold_fourth = pass.threshold;
    this->view.shadow_receivers_fourth = pass.receivers.data();
    this->view.shadow_receiver_count_fourth = static_cast<uint64_t>(pass.receivers.size());
  }
  this->extraShadowPassViews.clear();
  this->extraShadowPassViews.reserve(this->shadowFrame.extra.size());
  for (const auto & pass : this->shadowFrame.extra) {
    CoinWgpuShadowPassView packed{};
    packed.casters = pass.casters.data();
    packed.caster_count = static_cast<uint64_t>(pass.casters.size());
    packed.receivers = pass.receivers.data();
    packed.receiver_count = static_cast<uint64_t>(pass.receivers.size());
    packed.map_size = pass.mapSize;
    packed.kind = pass.kind;
    packed.near_distance = pass.nearDistance;
    packed.far_distance = pass.farDistance;
    packed.epsilon = pass.epsilon;
    packed.threshold = pass.threshold;
    this->extraShadowPassViews.push_back(packed);
  }
  this->view.extra_shadow_passes = this->extraShadowPassViews.empty() ? NULL :
    this->extraShadowPassViews.data();
  this->view.extra_shadow_pass_count = static_cast<uint64_t>(this->extraShadowPassViews.size());
}

const CoinWgpuFrameView &
CoinWgpuFfiFrame::getView() const
{
  return this->view;
}

const CoinWgpuShadowFrame &
CoinWgpuFfiFrame::getShadowFrame() const
{
  return this->shadowFrame;
}

bool
CoinWgpuFfiFrame::reusedLastPrepare() const
{
  return this->reused;
}

CoinRenderFrameReuseKind
CoinWgpuFfiFrame::lastPrepareKind() const
{
  return this->prepareKind;
}

bool
CoinWgpuFfiFrame::incrementalOpaqueLastPrepare() const
{
  return this->opaqueIncrementalUsed;
}

size_t
CoinWgpuFfiFrame::opaqueRangesRebakedLastPrepare() const
{
  return this->opaqueRebakedRanges;
}

size_t
CoinWgpuFfiFrame::opaqueVerticesRebakedLastPrepare() const
{
  return this->opaqueRebakedVertices;
}
