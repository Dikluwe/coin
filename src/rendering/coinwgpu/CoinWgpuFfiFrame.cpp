#include "rendering/coinrender/CoinRenderMultipassCore.h"
#include "rendering/coinrender/CoinRenderCubeTextureCore.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include "rendering/coinrender/CoinRenderTransformCore.h"
#include "rendering/coinrender/CoinRenderClipCore.h"
#include "rendering/coinrender/CoinRenderBumpMapCore.h"
#include "rendering/coinrender/CoinRenderAlphaTestCore.h"
#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"

#include <Inventor/SbMatrix.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include "rendering/coinrender/CoinRenderComposition.h"

#include <algorithm>
#include <cmath>
#include <cfenv>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <new>
#include <unordered_map>

#if (defined(__GNUC__) || defined(__clang__)) && defined(__SSE_MATH__) && defined(__SSE2_MATH__) && \
    (defined(__x86_64__) || defined(__i386__))
#include <xmmintrin.h>
#define COIN_WGPU_MATRIX_CACHE_FP_X86 1
#endif

static_assert(sizeof(CoinWgpuPortableNode) == 40, "Portable IR statement ABI size changed");
static_assert(sizeof(CoinWgpuPortableModule) == 56, "Portable module ABI size changed");
static_assert(sizeof(CoinWgpuPortableRange) == 64, "Portable range ABI size changed");
static_assert(sizeof(CoinWgpuFrameView) == 544, "Frame view ABI size changed");
static_assert(sizeof(CoinWgpuBumpVertex) == 44, "Bump vertex ABI size changed");
static_assert(sizeof(CoinWgpuBumpImage) == 32, "Bump image ABI size changed");
static_assert(offsetof(CoinWgpuFrameView, bump_vertices) == 464, "Bump ABI tail offset changed");
static_assert(sizeof(CoinWgpuInstance) == 144, "Instance ABI size changed");
static_assert(sizeof(CoinWgpuInstanceRange) == 16, "Instance range ABI size changed");
static_assert(offsetof(CoinWgpuFrameView, instances) == 416, "Instance ABI tail offset changed");
static_assert(offsetof(CoinWgpuFrameView, instance_ranges) == 432, "Instance range ABI tail offset changed");
static_assert(sizeof(CoinWgpuShadowPassView) == 56, "Extra shadow pass ABI size changed");
static_assert(offsetof(CoinWgpuFrameView, extra_shadow_passes) == 400,
              "Extra shadow pass ABI offset changed");
static_assert(offsetof(CoinWgpuFrameView, shadow_epsilon_second) == 376,
              "Frame view per-pass VSM ABI offset changed");
static_assert(sizeof(CoinWgpuShadowDraw) == 160, "Shadow draw ABI size changed");
static_assert(offsetof(CoinWgpuShadowDraw, viewport) == 144, "Shadow viewport ABI offset changed");
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
static_assert(offsetof(CoinWgpuVertex, extra_texcoords) == 52, "Extra UV ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, extra_textures) == 1096, "Extra textures ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, texture_combines) == 1768, "Combine ABI offset changed");
static_assert(sizeof(CoinWgpuRenderState) == 2436, "CoinWgpuRenderState ABI size changed");
static_assert(offsetof(CoinWgpuRenderState, raster_texture_source_slot) == 2300, "Raster source ABI tail mismatch");
static_assert(offsetof(CoinWgpuRenderState, raster_texture_coordinates) == 2304, "Raster coordinate ABI tail mismatch");
static_assert(offsetof(CoinWgpuRenderState, alpha_test_function) == 2280, "Alpha function ABI tail changed");
static_assert(offsetof(CoinWgpuRenderState, alpha_test_reference) == 2284, "Alpha reference ABI tail changed");
static_assert(offsetof(CoinWgpuRenderState, texture_projection) == 2288, "Texture projection ABI tail changed");
static_assert(offsetof(CoinWgpuRenderState, polygon_offset_max_depth_bits) == 1092, "Maximum depth ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, polygon_offset_slope_bias) == 1088, "Slope bias ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, polygon_offset_enabled) == 936, "Polygon offset ABI tail changed");
static_assert(offsetof(CoinWgpuRenderState, depth_test) == 916, "depth_test ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, depth_write) == 920, "depth_write ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, depth_function) == 924, "depth_function ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, depth_range) == 928, "depth_range ABI offset changed");

namespace {
// Validate new reference-bearing state before revision reuse or vector/cache
// mutation. This does not inspect vertices and leaves old packed bytes intact.
inline bool validRasterTextureStateReferences(const CoinRenderFramePlan & frame, std::string & diagnostic) {
  if (!CoinRenderCubeTextureCore::validate(frame, diagnostic)) return false;
  for (const auto & state : frame.renderStates) {
    if (state.rasterTextureSourceSlot == UINT32_MAX) continue;
    if (!state.rasterPixels || !state.hasTexture ||
        state.textureImageSlot >= frame.textures.size() || state.samplerSlot >= frame.samplers.size() ||
        state.rasterTextureSourceSlot >= frame.renderStates.size()) {
      diagnostic = "Invalid raster inherited texture source or payload"; return false;
    }
    const auto & source = frame.renderStates[state.rasterTextureSourceSlot];
    if (source.rasterPixels || source.rasterTextureSourceSlot != UINT32_MAX ||
        static_cast<unsigned>(source.textureProjection) > 1) {
      diagnostic = "Recursive or invalid raster inherited texture source"; return false;
    }
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      if (state.rasterTextureProvenance[unit] > 1) {
        diagnostic = "Invalid raster texture provenance"; return false;
      }
      const auto & raw = state.rasterTextureCoordinates[unit];
      for (int c = 0; c < 4; ++c) if (!std::isfinite(raw[c])) {
        diagnostic = "Non-finite raster texture coordinate"; return false;
      }
      const auto layer = coin_render_texture_unit(source, unit);
      if (!layer.enabled) continue;
      if (layer.imageSlot >= frame.textures.size() || layer.samplerSlot >= frame.samplers.size()) {
        diagnostic = "Invalid raster inherited texture resource slot"; return false;
      }
      SbVec4f transformed;
      layer.matrix.multVecMatrix(raw, transformed);
      for (int c = 0; c < 4; ++c) if (!std::isfinite(transformed[c])) {
        diagnostic = "Non-finite raster texture matrix result"; return false;
      }
      if (!frame.textures[layer.imageSlot].cubeFaceSize && source.textureProjection == CoinRenderTextureProjection::PROJECTIVE &&
          (transformed[3] == 0 || !std::isfinite(transformed[0]/transformed[3]) ||
           !std::isfinite(transformed[1]/transformed[3]) || !std::isfinite(transformed[2]/transformed[3]))) {
        diagnostic = "Invalid raster texture projective coordinate"; return false;
      }
    }
  }
  return true;
}
// Numerical memoization is optional. Recognize a fully masked, gradual,
// nearest-even x86 environment, including x87 precision/infinity controls.
// Unknown platforms/modes retain the literal matrix operations. Sticky flags
// are read separately: they are never cleared, raised or restored here.
bool instanceMatrixFpControl(uint32_t & key)
{
#ifdef COIN_WGPU_MATRIX_CACHE_FP_X86
  if (sizeof(float) != 4 || !std::numeric_limits<float>::is_iec559 ||
      std::fegetround() != FE_TONEAREST) return false;
  const uint32_t sse = _mm_getcsr() & 0xffc0u;
  uint16_t x87;
  __asm__ __volatile__("fnstcw %0" : "=m" (x87));
  const uint32_t x87Control = x87 & 0x1f3fu;
  if (sse != 0x1f80u || x87Control != 0x033fu) return false;
  key = (x87Control << 16) | sse;
  return true;
#else
  (void)key;
  return false;
#endif
}

uint32_t instanceMatrixFpStatus()
{
#ifdef COIN_WGPU_MATRIX_CACHE_FP_X86
  uint16_t x87;
  __asm__ __volatile__("fnstsw %0" : "=am" (x87));
  return (_mm_getcsr() & 0x3fu) | (uint32_t(x87 & 0x3fu) << 6);
#else
  return 0;
#endif
}
const uint32_t unchangedMatrixFpStatus = 1u << 12;
const size_t matrixCacheBudget = 8u * 1024u * 1024u;
const size_t matrixCacheAllocatorAllowance = 256;

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

double instanceMatrixBound(const float * matrix, double value, int column, bool translation)
{
  return value * (std::abs(double(matrix[column])) + std::abs(double(matrix[4 + column])) +
                  std::abs(double(matrix[8 + column]))) +
    (translation ? std::abs(double(matrix[12 + column])) : 0.0);
}

bool instanceCommonMatricesValid(const CoinWgpuRenderState & state, double position)
{
  for (int column = 0; column < 4; ++column)
    if (instanceMatrixBound(state.model_view, position, column, true) > std::numeric_limits<float>::max() ||
        instanceMatrixBound(state.model_view_projection, position, column, true) > std::numeric_limits<float>::max() ||
        instanceMatrixBound(state.normal_matrix, 1.0e18, column, false) > std::numeric_limits<float>::max()) return false;
  return true;
}

bool diagonalPositionMatrix(const float * authored, const float * scale, float * transport)
{
  std::memcpy(transport, authored, sizeof(float) * 16);
  for (int row = 0; row < 3; ++row) for (int column = 0; column < 4; ++column) {
    const size_t at = row * 4 + column;
    transport[at] = authored[at] * scale[row];
    if (!std::isfinite(transport[at]) ||
        (authored[at] != 0 && transport[at] == 0)) return false;
  }
  return true;
}

uint32_t packedMaximumDepth(float depth)
{
  uint32_t bits = 0;
  if (depth >= 0) { std::memcpy(&bits, &depth, sizeof(bits)); ++bits; }
  return bits;
}

// After the opaque qualifier has proved common view/projection, lighting,
// viewport, cull/depth flags and empty clip planes, these are the remaining
// source fields transported by packState. Compare fields rather than struct
// padding, and retain disabled texture/fog/offset payloads in the exact key.
// The absent maximum-depth encoding intentionally maps all negative values
// to zero, exactly as the ordinary packer does.
bool sameOpaquePackedFields(const CoinRenderRenderStateSnapshot & a,
                            const CoinRenderRenderStateSnapshot & b)
{
  if (std::memcmp(a.textureMatrix.getValue(), b.textureMatrix.getValue(), sizeof(float) * 16) ||
      a.alphaTestFunction != b.alphaTestFunction ||
      std::memcmp(&a.alphaTestReference, &b.alphaTestReference, sizeof(float)) ||
      std::memcmp(a.textureCombines.data(), b.textureCombines.data(),
                  sizeof(CoinRenderTextureCombineSnapshot) * COIN_RENDER_MAX_TEXTURE_UNITS) ||
      a.rasterTextureSourceSlot != b.rasterTextureSourceSlot ||
      a.rasterTextureCoordinates != b.rasterTextureCoordinates ||
      a.rasterTextureProvenance != b.rasterTextureProvenance ||
      a.textureImageSlot != b.textureImageSlot || a.samplerSlot != b.samplerSlot ||
      a.textureModel != b.textureModel || a.textureProjection != b.textureProjection ||
      std::memcmp(a.textureBlendColor, b.textureBlendColor, sizeof(a.textureBlendColor)) ||
      std::memcmp(a.fogColor, b.fogColor, sizeof(a.fogColor)) ||
      std::memcmp(&a.fogStart, &b.fogStart, sizeof(float)) ||
      std::memcmp(&a.fogEnd, &b.fogEnd, sizeof(float)) ||
      std::memcmp(a.depthRange, b.depthRange, sizeof(a.depthRange)) ||
      std::memcmp(&a.polygonOffsetFactor, &b.polygonOffsetFactor, sizeof(float)) ||
      std::memcmp(&a.polygonOffsetUnits, &b.polygonOffsetUnits, sizeof(float)) ||
      std::memcmp(&a.polygonOffsetSlopeBias, &b.polygonOffsetSlopeBias, sizeof(float)) ||
      packedMaximumDepth(a.polygonOffsetMaxDepth) != packedMaximumDepth(b.polygonOffsetMaxDepth) ||
      a.polygonOffsetStyles != b.polygonOffsetStyles ||
      a.polygonOffsetPrimitiveStyle != b.polygonOffsetPrimitiveStyle) return false;
  for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS - 1; ++unit) {
    const auto & x = a.extraTextures[unit];
    const auto & y = b.extraTextures[unit];
    if (x.enabled != y.enabled || x.imageSlot != y.imageSlot || x.samplerSlot != y.samplerSlot ||
        x.model != y.model || std::memcmp(x.matrix.getValue(), y.matrix.getValue(), sizeof(float) * 16) ||
        std::memcmp(x.blendColor, y.blendColor, sizeof(x.blendColor))) return false;
  }
  return true;
}

bool validOpaqueAuthoredMatrices(const SbMatrix & modelView, const SbMatrix & normal)
{
  const float determinant = modelView.det4();
  // Singular/unstable normals retain the ordinary identity-normal fallback.
  // This proof applies before any position-only transport factorization.
  if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-9f ||
      !CoinRenderTransformCore::finiteMatrix(modelView) || !CoinRenderTransformCore::finiteMatrix(normal))
    return false;
  const SbMatrix inverse = normal.transpose();
  const SbMatrix residual = modelView * inverse;
  if (!CoinRenderTransformCore::finiteMatrix(residual)) return false;
  for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col)
    if (std::abs(residual[row][col] - (row == col ? 1.0f : 0.0f)) > 1.0e-4f) return false;
  return true;
}

void packVertex(const CoinRenderVertexSnapshot & src, CoinWgpuVertex & dst)
{
  std::memcpy(dst.position, src.position, sizeof(src.position));
  std::memcpy(dst.normal, src.normal, sizeof(src.normal));
  std::memcpy(dst.texcoord, src.texcoord, sizeof(src.texcoord));
  dst.material_slot = src.materialSlot;
  dst.texcoord[2] = src.textureR[0];
  dst.texcoord[3] = src.textureQ[0];
  for (size_t unit = 0; unit < 7; ++unit) {
    std::memcpy(dst.extra_texcoords[unit], src.extraTexcoords[unit], sizeof(src.extraTexcoords[unit]));
    dst.extra_texcoords[unit][2] = src.textureR[unit + 1];
    dst.extra_texcoords[unit][3] = src.textureQ[unit + 1];
  }
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

// All packing paths, including camera patches and baked/instanced geometry,
// must use the same viewport compensation. FFI viewport remains the scissor.
static SbMatrix coinWgpuProjection(const CoinRenderFramePlan & frame,
                                  const CoinRenderRenderStateSnapshot & state,
                                  uint32_t width, uint32_t height) {
  SbMatrix projection = CoinRenderTransformCore::projection(state.projectionCoin, false);
  if (state.viewportSlot < frame.viewports.size()) {
    int32_t clipped[4];
    projection *= CoinRenderTransformCore::clippedViewportTransform(
      frame.viewports[state.viewportSlot], width, height, clipped);
  }
  return projection;
}


bool
CoinWgpuFfiFrame::prepare(const CoinRenderFramePlan & frame, uint32_t width, uint32_t height,
                        std::string & outDiagnostic, bool allowInstancing)
{
  return this->prepare(frame, width, height,
    CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD, 0),
    outDiagnostic, nullptr, allowInstancing);
}

bool
CoinWgpuFfiFrame::prepare(const CoinRenderFramePlan & frame, uint32_t width, uint32_t height,
                        const CoinRenderFrameReuseDecision & reuse,
                        std::string & outDiagnostic,
                        const CoinRenderFramePreflight * preflight,
                        bool allowInstancing)
{
  return this->prepareImpl(frame, width, height, reuse, outDiagnostic,
                           preflight, allowInstancing, false);
}

bool
CoinWgpuFfiFrame::prepareBumpFrame(const CoinRenderFramePlan & frame,
                                   uint32_t width, uint32_t height,
                                   std::string & diagnostic)
{
  if (!coin_render_validate_extended_bump(frame, diagnostic))
    return false;
  return this->prepareImpl(frame, width, height,
    CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD, 0),
    diagnostic, nullptr, false, true);
}

bool
CoinWgpuFfiFrame::preparePortablePreflight(const CoinRenderFramePlan & frame,
                                          uint32_t width, uint32_t height,
                                          std::string & diagnostic)
{
  // Graph admission has validated the producer identities and descriptors.
  // This path neither resolves them nor manufactures pixel/token payloads.
  // The ordinary execution boundary below remains strict.
  if (!coin_render_shader_execution_profile(frame, diagnostic) ||
      !coin_render_validate_extended_bump(frame, diagnostic)) return false;
  const bool bump = std::any_of(frame.bumpCoords.begin(), frame.bumpCoords.end(),
      [](const CoinRenderBumpCoordOverride &coordinate) { return coordinate.imageSlot != 0; });
  return this->prepareImpl(frame, width, height,
      CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD, 0),
      diagnostic, nullptr, false, bump, true);
}

bool
CoinWgpuFfiFrame::prepareImpl(const CoinRenderFramePlan & frame, uint32_t width,
                              uint32_t height,
                              const CoinRenderFrameReuseDecision & reuse,
                              std::string & outDiagnostic,
                              const CoinRenderFramePreflight * preflight,
                              bool allowInstancing, bool allowBump, bool allowUnresolved)
{
  outDiagnostic.clear();
  if (frame.numPasses > 1) {
    // Per-sample projection payloads require the ordinary state-preserving
    // packer; specialized immutable/instancing snapshots describe one MVP.
    allowInstancing = false;
  }
  if (frame.numPasses < 1 || frame.numPasses > 16) {
    outDiagnostic = "Invalid multipass count at wgpu transport";
    return false;
  }
  if (!CoinRenderMultipassCore::projectionsFinite(frame)) {
    outDiagnostic = "Multipass translated projection is non-finite or invalid";
    return false;
  }

  if (!frame.hasFiniteNormalizedInputs(&outDiagnostic)) return false;
  if (!frame.portableShaders.empty()) {
    // The new typed ingress shares the Core resource and finite-operation proof.
    // Keep failures ahead of vector mutation, fast paths and GPU commands.
    if (!frame.isValid(&outDiagnostic)) return false;
    allowInstancing = false;
  }
  if (!validRasterTextureStateReferences(frame, outDiagnostic)) return false;
  if (!coin_render_scene_layers_valid(frame, outDiagnostic)) return false;
  // Validate cheap per-state ingress fields before any fast path or payload
  // vector is modified. A failed prepare must leave the previously packed
  // FFI state/caster pointers and bytes available to its owner.
  for (const auto & state : frame.renderStates) {
    if (state.colorWriteMask & ~uint8_t(0x0f)) {
      outDiagnostic = "RenderState has invalid color write mask";
      return false;
    }
    if (!coin_render_alpha_test_valid(state.alphaTestFunction, state.alphaTestReference)) {
      outDiagnostic = "Invalid alpha comparison function or reference";
      return false;
    }
    if (state.shadowStyle > 3u) {
      outDiagnostic = "Invalid shadow style";
      return false;
    }
  }
  for (const auto & draw : frame.draws) {
    if ((draw.multipassMask >> frame.numPasses) != 0 ||
        (draw.multipassRasterFixed && (draw.renderStateSlot >= frame.renderStates.size() ||
          !frame.renderStates[draw.renderStateSlot].rasterPixels))) {
      outDiagnostic = "Invalid multipass draw mask or raster identity";
      return false;
    }
  }
  const bool portableFrame = !frame.portableShaders.empty() || !this->portableViews.empty();
  const char * loweringDisabled = std::getenv("COIN_WGPU_DISABLE_DIAGONAL_MESH_LOWERING");
  const bool loweringAllowed = !portableFrame && !allowBump &&
    !(loweringDisabled && std::strcmp(loweringDisabled, "1") == 0);
  this->opaqueIncrementalUsed = false;
  this->opaqueHashedRanges = 0;
  this->opaqueCommonStatesPacked = this->opaqueCameraProofReused = 0;
  this->opaqueCameraMatricesQualified = false;
  this->opaqueMatrixCacheHits = this->opaqueMatricesCalculated = this->opaqueMatrixCacheAllocations = 0;
  this->opaqueMatrixCache.candidate = false;
  const char * matrixDisabled = std::getenv("COIN_WGPU_DISABLE_INSTANCE_MATRIX_CACHE");
  const char * commonDisabled = std::getenv("COIN_WGPU_DISABLE_INSTANCE_COMMON_STATE");
  this->opaqueMatrixCacheBypass = !allowInstancing ? 6 :
    (matrixDisabled && std::strcmp(matrixDisabled, "1") == 0) ? 1 :
    (commonDisabled && std::strcmp(commonDisabled, "1") == 0) ? 5 :
    !instanceMatrixFpControl(this->opaqueMatrixFpControl) ? 2 : 0;
  this->opaqueMatrixCacheAllowed = this->opaqueMatrixCacheBypass == 0;
  // These switches/environment checks also revoke the optional proof on the
  // pre-existing immutable-revision fast path; that payload contract is intact.
  if (!this->opaqueMatrixCacheAllowed) this->opaqueMatrixCache.valid = false;
  this->opaqueRebakedRanges = this->opaqueRebakedVertices = 0;
  this->opaqueIncrementalCandidate = false;
  // Failed qualification/packing must never leave an old revision or geometry
  // proof available to a retry. Successful exact reuse retains the proof.
  struct PrepareGuard {
    bool & valid;
    bool & cameraPatchable;
    bool & instanced;
    bool & matricesQualified;
    bool & matrixCacheValid;
    bool & matrixCacheCandidate;
    uint64_t & revision;
    CoinRenderFrameReuseKind & kind;
    bool committed;
    PrepareGuard(bool & v, bool & c, bool & i, bool & m, bool & mv, bool & mc,
                 uint64_t & r, CoinRenderFrameReuseKind & k)
      : valid(v), cameraPatchable(c), instanced(i), matricesQualified(m), matrixCacheValid(mv),
        matrixCacheCandidate(mc), revision(r), kind(k), committed(false) {}
    ~PrepareGuard() {
      if (!committed) {
        valid = false;
        cameraPatchable = false;
        instanced = false;
        matricesQualified = false;
        matrixCacheValid = matrixCacheCandidate = false;
        revision = 0;
        kind = CoinRenderFrameReuseKind::UNKNOWN;
      }
    }
  } guard(this->opaqueIncrementalValid, this->opaqueCameraPatchable, this->opaqueInstanced,
          this->opaqueCameraMatricesQualified, this->opaqueMatrixCache.valid,
          this->opaqueMatrixCache.candidate, this->packedRevision, this->prepareKind);
  if (frame.numPasses > 1) this->packedRevision = 0;
  if (!allowBump && std::any_of(frame.bumpCoords.begin(), frame.bumpCoords.end(),
      [](const CoinRenderBumpCoordOverride & coord) { return coord.imageSlot != 0; })) {
    outDiagnostic = "wgpu bump shader execution is not yet supported";
    return false;
  }
  // Validate before subtracting/packing: bit 4 is wrap-R, not a seventeenth
  // anisotropy value. Truncating an invalid public factor changes its meaning.
  for (const auto & sampler : frame.samplers) {
    if (!CoinRenderTextureSamplingCore::powerOfTwo(sampler.maxAnisotropy) ||
        sampler.maxAnisotropy > 16 ||
        (sampler.maxAnisotropy > 1 && sampler.filter != CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR) ||
        uint32_t(sampler.wrapS) > 1 || uint32_t(sampler.wrapT) > 1 || uint32_t(sampler.wrapR) > 1 ||
        uint32_t(sampler.filter) > 3) {
      outDiagnostic = "Invalid sampler anisotropy, wrap or filter at wgpu transport boundary";
      return false;
    }
  }
  std::string cubeDiagnostic;
  if (!CoinRenderCubeTextureCore::validate(frame,cubeDiagnostic)) {
    outDiagnostic=cubeDiagnostic;return false;
  }
  for (const auto& texture : frame.textures) {
    if (texture.producerId && !allowUnresolved) {
      outDiagnostic = "Unresolved scene texture producer at wgpu execution boundary";
      return false;
    }
    if ((texture.volumeDepth || texture.volumeWidth || texture.volumeHeight ||
         texture.volumeColumns || texture.volumeSourceComponents || texture.volumeMipmapped) &&
        !CoinRenderTextureSamplingCore::validMipImage(texture)) {
      outDiagnostic = "Invalid SoTexture3 atlas metadata at wgpu execution boundary";
      return false;
    }
  }
  bool sameAtlasTextures = true;
  for (size_t i = frame.textures.size(); i < this->textures.size(); ++i)
    if (this->textures[i].format == 11 || this->textures[i].format == 12) sameAtlasTextures = false;
  for (size_t i = 0; i < frame.textures.size(); ++i) {
    const auto & image = frame.textures[i];
    const uint32_t declaration = image.sceneTransparencyFunction == SoSceneTexture2::NONE ?
      COIN_WGPU_TEXTURE_DECLARED_OPAQUE : 0u;
    if (i >= this->textures.size() ||
        (this->textures[i].reserved & COIN_WGPU_TEXTURE_DECLARED_OPAQUE) != declaration) {
      sameAtlasTextures = false;
      break;
    }
    if (!image.cubeFaceSize && !image.volumeDepth &&
        !(i < this->textures.size() && (this->textures[i].format == 11 || this->textures[i].format == 12))) continue;
    uint32_t exponent = 0; for (uint32_t n = image.cubeFaceSize; n > 1; n >>= 1) ++exponent;
    const uint32_t metadata = (image.sceneTransparencyFunction == SoSceneTexture2::NONE ?
      COIN_WGPU_TEXTURE_DECLARED_OPAQUE : 0u) | (image.volumeDepth ?
      ((image.volumeWidth - 1) | ((image.volumeHeight - 1) << 8) |
       ((image.volumeDepth - 1) << 16) | (image.volumeMipmapped ? 1u << 24 : 0u) |
       (image.format == CoinRenderTextureFormat::RGBA8_SRGB ? COIN_WGPU_TEXTURE_VOLUME_SRGB : 0u)) :
      (exponent | (image.cubeSourceComponents << 4) | (image.cubeMipmapped ? 128u : 0u) | (image.gpuToken ? 256u : 0u) |
       (image.format == CoinRenderTextureFormat::RGBA8_SRGB ? COIN_WGPU_TEXTURE_CUBE_SRGB : 0u)));
    if (i >= this->textures.size() || i >= this->texturePixels.size() ||
        this->textures[i].format != (image.volumeDepth ? 11u : 12u) || this->textures[i].width != image.width ||
        this->textures[i].height != image.height || this->textures[i].reserved != metadata ||
        this->textures[i].content_digest != (image.gpuToken ? image.gpuToken : image.contentDigest) || this->texturePixels[i] != image.pixelsRgba) {
      sameAtlasTextures = false; break;
    }
  }
  const auto layerIdentity = [](const CoinRenderDrawPacket & draw) {
    return (uint64_t(draw.renderLayer) << 2) | (draw.authoredScene ? 2u : 0u) |
      (draw.clearDepthBefore ? 1u : 0u);
  };
  bool sameLayerIdentity = frame.draws.size() == this->packedLayerIdentity.size();
  if (sameLayerIdentity) for (size_t i = 0; i < frame.draws.size(); ++i)
    if (layerIdentity(frame.draws[i]) != this->packedLayerIdentity[i]) { sameLayerIdentity = false; break; }
  const auto rememberLayerIdentity = [&]() {
    this->packedLayerIdentity.resize(frame.draws.size());
    for (size_t i = 0; i < frame.draws.size(); ++i)
      this->packedLayerIdentity[i] = layerIdentity(frame.draws[i]);
  };
  if (!portableFrame && !allowBump && sameLayerIdentity && sameAtlasTextures && frame.revision != 0 && frame.revision == this->packedRevision &&
      (allowInstancing || !this->opaqueInstanced) && (loweringAllowed || !this->opaqueDiagonalLowered)) {
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
    rememberLayerIdentity();
    guard.committed = true;
    return true;
  }
  this->reused = false;
  CoinWgpuShadowFrame candidateShadow;
  if (!candidateShadow.prepare(frame, outDiagnostic)) return false;

  if (!portableFrame && !allowBump && (allowInstancing || !this->opaqueInstanced) &&
      (loweringAllowed || !this->opaqueDiagonalLowered) &&
      this->patchOpaqueCamera(frame, width, height, reuse)) {
    // The output still belongs to the camera anchor, while the captured states
    // now contain a different view. Do not mix it with a later object rebake.
    this->opaqueIncrementalValid = false;
    this->opaqueMatrixCache.valid = false;
    this->shadowFrame = std::move(candidateShadow);
    this->bindView(frame, width, height);
    this->view.camera_base_revision = reuse.baseRevision;
    this->packedRevision = frame.revision;
    this->prepareKind = CoinRenderFrameReuseKind::CAMERA_PATCH;
    rememberLayerIdentity();
    guard.committed = true;
    return true;
  }

  // The Rust camera patch owns only the standard immutable payload; shadow
  // casters/receivers must be transported through the full validated path.
  if (!allowBump && !this->opaqueBatched && frame.shadowGroups.empty() &&
      reuse.kind == CoinRenderFrameReuseKind::CAMERA_PATCH &&
      frame.revision != 0 && frame.revision != reuse.baseRevision &&
      reuse.baseRevision != 0 && reuse.baseRevision == this->packedRevision) {
    this->opaqueMatrixCache.valid = false;
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
    rememberLayerIdentity();
    guard.committed = true;
    return true;
  }

  this->packedRevision = 0;
  this->opaqueBatched = false;
  this->opaqueInstanced = false;
  this->opaqueDiagonalLowered = false;
  this->instances.clear();
  this->instanceRanges.clear();
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

  if (frame.numPasses == 1 && !portableFrame && !allowBump && ((allowInstancing && this->tryOpaqueInstancing(frame, width, height, preflight)) ||
      this->tryEarlyOpaqueBatch(frame, width, height, preflight))) {
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
    this->opaqueMatrixCache.valid = this->opaqueInstanced && this->opaqueMatrixCache.candidate;
    this->opaqueMatrixCache.candidate = false;
    rememberLayerIdentity();
    guard.committed = true;
    return true;
  }
  this->opaqueIncrementalValid = false;
  this->opaqueMatrixCache.valid = this->opaqueMatrixCache.candidate = false;
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
    this->texturePixels[i].insert(this->texturePixels[i].end(),src.mipmapsRgba.begin(),src.mipmapsRgba.end());
    dst.width = src.width;
    dst.height = src.height;
    dst.format = src.cubeFaceSize ? 12 : src.volumeDepth ? 11 : src.gpuToken?1:src.format==CoinRenderTextureFormat::RGBA8_LINEAR?(src.mipmapped?2:0):
      3+(uint32_t(src.format)-1)*2+(src.mipmapped?1:0);
    auto exponent = [](uint32_t v) { uint32_t bits=0; while (v>1) {v>>=1;++bits;} return bits; };
    dst.reserved = src.cubeFaceSize ?
      (exponent(src.cubeFaceSize) | (src.cubeSourceComponents << 4) | (src.cubeMipmapped ? 128u : 0u) | (src.gpuToken ? 256u : 0u) |
       (src.format == CoinRenderTextureFormat::RGBA8_SRGB ? COIN_WGPU_TEXTURE_CUBE_SRGB : 0u)) : src.volumeDepth ?
      ((src.volumeWidth - 1) | ((src.volumeHeight - 1)<<8) |
       ((src.volumeDepth - 1)<<16) | (src.volumeMipmapped ? 1u<<24 : 0u) |
       (src.format == CoinRenderTextureFormat::RGBA8_SRGB ? COIN_WGPU_TEXTURE_VOLUME_SRGB : 0u)) :
      src.gpuToken?((src.gpuOpaque?1:0)|(src.mipmapped?2:0)|(uint32_t(src.format)<<2)):0;
    if (src.sceneTransparencyFunction == SoSceneTexture2::NONE)
      dst.reserved |= COIN_WGPU_TEXTURE_DECLARED_OPAQUE;
    dst.content_digest = src.gpuToken ? src.gpuToken : src.contentDigest;
    dst.pixels = src.gpuToken ? NULL : this->texturePixels[i].data();
    dst.pixel_bytes_len = static_cast<uint64_t>(this->texturePixels[i].size());
  }

  this->samplers.assign(frame.samplers.size(), CoinWgpuSampler{});
  for (size_t i = 0; i < frame.samplers.size(); ++i) {
    this->samplers[i].wrap_s = static_cast<uint32_t>(frame.samplers[i].wrapS);
    this->samplers[i].wrap_t = static_cast<uint32_t>(frame.samplers[i].wrapT);
    this->samplers[i].filter = static_cast<uint32_t>(frame.samplers[i].filter);
    this->samplers[i].reserved=(frame.samplers[i].maxAnisotropy-1) |
        (uint32_t(frame.samplers[i].wrapR)<<4);
  }

  if (!allowBump) this->batchOpaqueTriangles(frame, width, height);
  this->rememberOpaqueCamera(frame, width, height);

  this->shadowFrame = std::move(candidateShadow);
  this->bindView(frame, width, height);
  if (allowBump) {
    if (frame.vertices.size() > UINT32_MAX) {
      outDiagnostic = "Bump frame exceeds source vertex index range";
      return false;
    }
    std::vector<uint32_t> sourceVertexForEmitted(frame.vertices.size());
    for (size_t i = 0; i < sourceVertexForEmitted.size(); ++i)
      sourceVertexForEmitted[i] = static_cast<uint32_t>(i);
    if (!this->prepareBumpPayload(frame, sourceVertexForEmitted, outDiagnostic)) return false;
  }
  this->packedRevision = frame.revision;
  this->prepareKind = reuse.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD
    ? CoinRenderFrameReuseKind::RESOURCE_REBUILD
    : CoinRenderFrameReuseKind::FULL_REBUILD;
  rememberLayerIdentity();
  guard.committed = true;
  return true;
}

void
CoinWgpuFfiFrame::rememberOpaqueCamera(const CoinRenderFramePlan & frame,
                                      uint32_t width, uint32_t height)
{
  this->opaqueCameraPatchable = false;
  const bool matricesQualified = this->opaqueInstanced && this->opaqueCameraMatricesQualified;
  this->opaqueCameraMatricesQualified = false;
  if (!this->opaqueBatched || !this->opaqueGeometryPatchable || this->states.size() != 1 || frame.renderStates.empty() ||
      frame.revision == 0 || !frame.shadowGroups.empty()) return;
  const auto & first = frame.renderStates.front();
  if (!CoinRenderTransformCore::cameraReuseView(first.view)) return;
  // Instancing already proved every authored matrix with these same guards.
  // This proof cannot survive prepare's reset/failure or come from a revision
  // hint. Baked/fallback frames continue through the original camera proof.
  if (matricesQualified) {
    this->opaqueCameraProofReused = frame.renderStates.size();
  }
  else for (const auto & state : frame.renderStates) {
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
  if (this->opaqueCameraProofReused && std::getenv("COIN_RENDER_TRACE_PHASES"))
    std::fprintf(stderr, "COIN_RENDER_PHASE wgpu_opaque_camera authored_proofs_reused=%zu\n",
                 this->opaqueCameraProofReused);
}

bool
CoinWgpuFfiFrame::patchOpaqueCamera(const CoinRenderFramePlan & frame, uint32_t width,
                                   uint32_t height, const CoinRenderFrameReuseDecision & reuse)
{
  const char * disabled = std::getenv("COIN_WGPU_DISABLE_OPAQUE_CAMERA_PATCH");
  const char * instancingDisabled = std::getenv("COIN_WGPU_DISABLE_OPAQUE_INSTANCING");
  if ((disabled && std::strcmp(disabled, "1") == 0) || !this->opaqueCameraPatchable ||
      (this->opaqueInstanced && instancingDisabled && std::strcmp(instancingDisabled, "1") == 0) ||
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
  SbMatrix cameraTransform, normal;
  if (this->opaqueInstanced) {
    // Instances carry authored world matrices. Applying the current view
    // directly avoids rounding a camera delta against an old eye-space anchor.
    cameraTransform = first.view;
    normal = CoinRenderTransformCore::normalMatrix(first.view);
  } else if (!CoinRenderTransformCore::cameraDelta(
               this->opaqueCameraAnchor, first.view, cameraTransform, normal)) return false;
  const SbMatrix mvp = cameraTransform * coinWgpuProjection(frame, first, width, height);
  if (!CoinRenderTransformCore::finiteMatrix(cameraTransform) || !CoinRenderTransformCore::finiteMatrix(normal) ||
      !CoinRenderTransformCore::finiteMatrix(mvp)) return false;
  std::memcpy(updated.model_view, cameraTransform.getValue(), sizeof(updated.model_view));
  std::memcpy(updated.normal_matrix, normal.getValue(), sizeof(updated.normal_matrix));
  std::memcpy(updated.model_view_projection, mvp.getValue(), sizeof(updated.model_view_projection));
  updated.material_slot = 0;
  if (this->opaqueInstanced && !instanceCommonMatricesValid(updated, this->opaqueInstancePositionBound)) return false;
  this->states.front() = updated;
  return true;
}

// Homogeneous opaque surfaces can transport their local geometry once and
// apply their exact captured model/view matrices per occurrence. Consecutive
// canonical meshes form groups; no draw is reordered across another mesh.
bool
CoinWgpuFfiFrame::tryOpaqueInstancing(const CoinRenderFramePlan & frame,
                                    uint32_t width, uint32_t height,
                                    const CoinRenderFramePreflight * preflight)
{
  const bool previousMatrixCacheValid = this->opaqueMatrixCache.valid;
  // Forward-only in-place writes need no second transactional array: no entry
  // is read twice, and only prepare's final commit can license the new data.
  this->opaqueMatrixCache.valid = this->opaqueMatrixCache.candidate = false;
  for (const char * option : {"COIN_WGPU_DISABLE_OPAQUE_INSTANCING", "COIN_WGPU_DISABLE_OPAQUE_BATCHING"}) {
    const char * value = std::getenv(option);
    if (value && std::strcmp(value, "1") == 0) return false;
  }
  if (frame.draws.size() < 256 || frame.draws.size() > 1048576u ||
      frame.renderStates.empty() ||
      frame.materials.empty() || frame.materials.size() > (32u * 1024u * 1024u) / sizeof(CoinWgpuMaterial) ||
      !frame.shadowGroups.empty() || !frame.shadowLights.empty() ||
      !frame.textures.empty() || !frame.samplers.empty()) return false;
  // Rust's bounded profile validates the entire material table, including
  // unused entries. Reject this candidate rather than changing acceptance of
  // an otherwise valid opaque frame with an unused transparent material.
  for (const auto & material : frame.materials) {
    if (material.diffuse[3] != 1 || material.transparency != 0 ||
        !std::isfinite(material.shininess) || !std::isfinite(material.transparency)) return false;
    for (int c = 0; c < 4; ++c)
      if (!std::isfinite(material.ambient[c]) || !std::isfinite(material.diffuse[c]) ||
          !std::isfinite(material.specular[c]) || !std::isfinite(material.emission[c])) return false;
  }
  const auto & first = frame.renderStates.front();
  if (!CoinRenderTransformCore::cameraReuseView(first.view) ||
      !CoinRenderTransformCore::finiteMatrix(first.projectionCoin) ||
      (first.lightModel != CoinRenderLightModel::PHONG && first.lightModel != CoinRenderLightModel::BASE_COLOR))
    return false;
  CoinRenderCompositionScheduleView order("wgpu_instancing");
  std::string diagnostic;
  if (!order.prepare(frame, diagnostic, preflight) ||
      order.size() != frame.draws.size()) return false;

  struct SourceKey {
    uint32_t firstVertex, vertexCount, firstIndex, indexCount;
    bool operator==(const SourceKey & other) const {
      return firstVertex == other.firstVertex && vertexCount == other.vertexCount &&
        firstIndex == other.firstIndex && indexCount == other.indexCount;
    }
  };
  struct SourceHash {
    size_t operator()(const SourceKey & key) const {
      size_t hash = key.firstVertex;
      hash = hash * 16777619u ^ key.vertexCount;
      hash = hash * 16777619u ^ key.firstIndex;
      return hash * 16777619u ^ key.indexCount;
    }
  };
  struct SourceInfo { size_t canonical; uint32_t material; float scale[3]; };
  struct CanonicalMesh {
    CoinRenderGeometryRange source;
    uint32_t firstVertex, firstIndex;
    double maxPosition[3], maxNormal[3];
    float scale[3];
    bool diagonal;
  };
  struct Group { size_t canonical, firstInstance, count, sourceDraw; };
  struct Occurrence { size_t canonical; float scale[3]; };
  std::unordered_map<SourceKey, SourceInfo, SourceHash> sourceMemo;
  std::unordered_multimap<uint64_t, size_t> canonicalHashes;
  std::vector<CanonicalMesh> canonicalMeshes;
  std::vector<Group> groups;
  std::vector<Occurrence> occurrences;
  // Source spans and hash nodes are metadata, not part of the ABI geometry
  // budget. Bound their admission separately, including allocator/bucket
  // overhead conservatively. Existing per-state bake storage is unchanged.
  const size_t maxSourceSpans = 65536;
  const uint64_t maxMetadataBytes = 64u * 1024u * 1024u;
  const auto metadataFits = [&](size_t spans, size_t meshes, size_t groupCount) {
    return uint64_t(order.size()) * sizeof(Occurrence) + uint64_t(spans) * 128u +
      uint64_t(meshes) * (sizeof(CanonicalMesh) + 128u) + uint64_t(groupCount) * sizeof(Group)
      <= maxMetadataBytes;
  };
  if (!metadataFits(0, 0, 0)) return false;
  occurrences.reserve(order.size());
  uint64_t compactVertices = 0, compactIndices = 0;
  size_t diagonalSpans = 0;
  const char * loweringDisabled = std::getenv("COIN_WGPU_DISABLE_DIAGONAL_MESH_LOWERING");
  const bool diagonalEnabled = !(loweringDisabled && std::strcmp(loweringDisabled, "1") == 0);
  const size_t materialOffset = offsetof(CoinRenderVertexSnapshot, materialSlot);
  static_assert(sizeof(CoinRenderVertexSnapshot) == 164, "Source vertex proof layout changed");
  const auto hashBytes = [](uint64_t hash, const unsigned char * bytes, size_t length) {
    for (size_t i = 0; i < length; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
  };
  const auto canonicalVertex = [](CoinRenderVertexSnapshot vertex, bool diagonal) -> CoinRenderVertexSnapshot {
    if (diagonal) for (int c = 0; c < 3; ++c)
      if (vertex.position[c] != 0) vertex.position[c] = std::copysign(1.0f, vertex.position[c]);
    return vertex;
  };
  const auto sameMesh = [&](const CanonicalMesh & a, const CanonicalMesh & b) -> bool {
    if (a.diagonal != b.diagonal || a.source.vertexCount != b.source.vertexCount ||
        a.source.indexCount != b.source.indexCount) return false;
    for (uint32_t v = 0; v < a.source.vertexCount; ++v) {
      const auto & x = frame.vertices[a.source.firstVertex + v];
      const auto & y = frame.vertices[b.source.firstVertex + v];
      for (int c = 0; c < 3; ++c) {
        const float p = a.diagonal && x.position[c] != 0 ? std::copysign(1.0f, x.position[c]) : x.position[c];
        const float q = b.diagonal && y.position[c] != 0 ? std::copysign(1.0f, y.position[c]) : y.position[c];
        if (std::memcmp(&p, &q, sizeof(float))) return false;
      }
      // Positions are handled above; every remaining attribute except the
      // uniform per-instance material is compared directly, without hashing
      // or making a temporary vertex for each source occurrence.
      const auto * p = reinterpret_cast<const unsigned char *>(&x);
      const auto * q = reinterpret_cast<const unsigned char *>(&y);
      if (std::memcmp(p + sizeof(x.position), q + sizeof(y.position), materialOffset - sizeof(x.position)) ||
          std::memcmp(p + materialOffset + sizeof(uint32_t), q + materialOffset + sizeof(uint32_t),
                      sizeof(x) - materialOffset - sizeof(uint32_t))) return false;
    }
    for (uint32_t j = 0; j < a.source.indexCount; ++j)
      if (frame.indices[a.source.firstIndex + j] - a.source.firstVertex !=
          frame.indices[b.source.firstIndex + j] - b.source.firstVertex) return false;
    return true;
  };
  // A zero screen-door level discards no pixels. Fully opaque materials and
  // all state checks below still qualify the candidate; positive levels fall back.
  // Each source range is checked/hashed once even when referenced 40,001 times.
  // Different material bindings may canonicalize to the same local geometry.
  for (const auto & item : order) {
    const auto & draw = frame.draws[item.drawIndex];
    const auto & range = draw.geometry;
    if (draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST || draw.stableNodeId ||
        item.blend || item.screenDoorLevel || draw.renderLayer || draw.clearDepthBefore ||
        !range.vertexCount || !range.indexCount || range.indexCount % 3 ||
        draw.renderStateSlot >= frame.renderStates.size() ||
        item.firstIndex != range.firstIndex || item.indexCount != range.indexCount) return false;
    const auto & state = frame.renderStates[draw.renderStateSlot];
    if (!state.colorWrite || state.materialSlot >= frame.materials.size() ||
        frame.materials[state.materialSlot].diffuse[3] != 1 || frame.materials[state.materialSlot].transparency != 0 ||
        item.depthTest != state.depthTest || item.depthWrite != state.depthWrite ||
        item.depthFunction != state.depthFunction || item.depthRange[0] != state.depthRange[0] ||
        item.depthRange[1] != state.depthRange[1]) return false;
    const SourceKey key{range.firstVertex, range.vertexCount, range.firstIndex, range.indexCount};
    auto memo = sourceMemo.find(key);
    size_t canonical;
    float sourceScale[3] = {1, 1, 1};
    if (memo != sourceMemo.end()) {
      if (memo->second.material != state.materialSlot) return false;
      canonical = memo->second.canonical;
      std::memcpy(sourceScale, memo->second.scale, sizeof(sourceScale));
    }
    else {
      if (sourceMemo.size() == maxSourceSpans ||
          !metadataFits(sourceMemo.size() + 1, canonicalMeshes.size() + 1, groups.size() + 1)) {
        if (std::getenv("COIN_RENDER_TRACE_PHASES"))
          std::fprintf(stderr, "COIN_RENDER_PHASE wgpu_opaque_instancing rejected=metadata_budget source_ranges=%zu visited_draws=%zu\n",
                       sourceMemo.size(), occurrences.size() + 1);
        return false;
      }
      const uint64_t vertexEnd = uint64_t(range.firstVertex) + range.vertexCount;
      const uint64_t indexEnd = uint64_t(range.firstIndex) + range.indexCount;
      if (vertexEnd > frame.vertices.size() || indexEnd > frame.indices.size()) return false;
      uint64_t hash = 14695981039346656037ull;
      CanonicalMesh candidate{};
      candidate.source = range;
      bool positive[3] = {}, negative[3] = {};
      for (uint64_t v = range.firstVertex; v < vertexEnd; ++v) {
        const auto & vertex = frame.vertices[v];
        if (vertex.materialSlot != state.materialSlot || vertex.screenSpaceW != 1 || vertex.fogEyeDepth >= 0)
          return false;
        for (int c = 0; c < 3; ++c) {
          if (!std::isfinite(vertex.position[c])) return false;
          candidate.maxPosition[c] = std::max(candidate.maxPosition[c], std::abs(double(vertex.position[c])));
          positive[c] |= vertex.position[c] > 0;
          negative[c] |= vertex.position[c] < 0;
        }
      }
      // A value-only profile for diagonal position factorization. No shape
      // type, node ID or benchmark identity is involved. Opposing nonzero
      // extrema in every axis exclude flat/zero-dimensional and one-sided
      // meshes. All normals/UVs and all topology remain exact attributes.
      candidate.diagonal = diagonalEnabled;
      for (int c = 0; c < 3; ++c) {
        candidate.scale[c] = static_cast<float>(candidate.maxPosition[c]);
        if (!positive[c] || !negative[c] || candidate.scale[c] < std::numeric_limits<float>::min())
          candidate.diagonal = false;
      }
      for (uint64_t v = range.firstVertex; candidate.diagonal && v < vertexEnd; ++v) {
        const auto & vertex = frame.vertices[v];
        for (int c = 0; c < 3; ++c) {
          const float local = vertex.position[c] == 0 ? vertex.position[c] : std::copysign(1.0f, vertex.position[c]);
          const float reconstructed = local * candidate.scale[c];
          if (std::memcmp(&reconstructed, &vertex.position[c], sizeof(float))) candidate.diagonal = false;
        }
      }
      if (candidate.diagonal) {
        ++diagonalSpans;
        std::memcpy(sourceScale, candidate.scale, sizeof(sourceScale));
        for (int c = 0; c < 3; ++c) candidate.maxPosition[c] = 1;
      }
      else for (int c = 0; c < 3; ++c) candidate.scale[c] = 1;
      for (uint64_t j = range.firstIndex; j < indexEnd; ++j) {
        if (frame.indices[j] < range.firstVertex || frame.indices[j] >= vertexEnd) return false;
      }
      canonical = canonicalMeshes.size();
      // Equal unique source spans are common after local geometry changes.
      // Exact comparison against the first/latest canonical mesh avoids a
      // serial byte-at-a-time FNV scan of an otherwise identical 86 MiB plan.
      if (!canonicalMeshes.empty() && sameMesh(canonicalMeshes.front(), candidate)) canonical = 0;
      else if (canonicalMeshes.size() > 1 && sameMesh(canonicalMeshes.back(), candidate))
        canonical = canonicalMeshes.size() - 1;
      if (canonical == canonicalMeshes.size()) {
        // Hashing is only for divergent meshes; equality still proves every
        // attribute and index after a hash match. Never trust sourceRevision.
        // A direct exact match above inherits finite normal/UV attributes
        // from its already-qualified canonical mesh; divergent candidates
        // qualify these values explicitly before hashing or publication.
        for (uint64_t v = range.firstVertex; v < vertexEnd; ++v) {
          const auto & vertex = frame.vertices[v];
          for (int c = 0; c < 3; ++c) {
            if (!std::isfinite(vertex.normal[c])) return false;
            candidate.maxNormal[c] = std::max(candidate.maxNormal[c],std::abs(double(vertex.normal[c])));
          }
          for (float coordinate : vertex.texcoord) if (!std::isfinite(coordinate)) return false;
          for (float coordinate : vertex.textureR) if (!std::isfinite(coordinate)) return false;
          for (float coordinate : vertex.textureQ) if (!std::isfinite(coordinate)) return false;
          for (const auto & unit : vertex.extraTexcoords)
            for (float coordinate : unit) if (!std::isfinite(coordinate)) return false;
        }
        ++this->opaqueHashedRanges;
        const unsigned char diagonalTag = candidate.diagonal ? 1 : 0;
        hash = hashBytes(hash, &diagonalTag, sizeof(diagonalTag));
        for (uint64_t v = range.firstVertex; v < vertexEnd; ++v) {
          const auto vertex = canonicalVertex(frame.vertices[v], candidate.diagonal);
          const auto * bytes = reinterpret_cast<const unsigned char *>(&vertex);
          hash = hashBytes(hash, bytes, materialOffset);
          hash = hashBytes(hash, bytes + materialOffset + sizeof(uint32_t),
                           sizeof(vertex) - materialOffset - sizeof(uint32_t));
        }
        for (uint64_t j = range.firstIndex; j < indexEnd; ++j) {
          const uint32_t local = frame.indices[j] - range.firstVertex;
          hash = hashBytes(hash, reinterpret_cast<const unsigned char *>(&local), sizeof(local));
        }
        const auto bucket = canonicalHashes.equal_range(hash);
        for (auto match = bucket.first; match != bucket.second; ++match)
          if (sameMesh(canonicalMeshes[match->second], candidate)) { canonical = match->second; break; }
      }
      if (canonical == canonicalMeshes.size()) {
        if (compactVertices + range.vertexCount > UINT32_MAX || compactIndices + range.indexCount > UINT32_MAX ||
            (compactVertices + range.vertexCount) * sizeof(CoinWgpuVertex) +
            (compactIndices + range.indexCount) * sizeof(uint32_t) > 8u * 1024u * 1024u)
          return false;
        candidate.firstVertex = static_cast<uint32_t>(compactVertices);
        candidate.firstIndex = static_cast<uint32_t>(compactIndices);
        compactVertices += range.vertexCount; compactIndices += range.indexCount;
        canonicalMeshes.push_back(candidate);
        canonicalHashes.emplace(hash, canonical);
      }
      sourceMemo.emplace(key, SourceInfo{canonical, state.materialSlot,
                                       {sourceScale[0], sourceScale[1], sourceScale[2]}});
    }
    if (groups.empty() || groups.back().canonical != canonical) {
      // Changed geometry interspersed among shared shapes can form thousands
      // of groups; preserve the full-bake path instead of multiplying submits.
      if (groups.size() == 128) {
        if (std::getenv("COIN_RENDER_TRACE_PHASES"))
          std::fprintf(stderr, "COIN_RENDER_PHASE wgpu_opaque_instancing rejected=group_limit source_ranges=%zu canonical_meshes=%zu visited_draws=%zu\n",
                       sourceMemo.size(), canonicalMeshes.size(), occurrences.size() + 1);
        return false;
      }
      groups.push_back(Group{canonical, occurrences.size(), 0, item.drawIndex});
    }
    ++groups.back().count;
    occurrences.push_back(Occurrence{canonical, {sourceScale[0], sourceScale[1], sourceScale[2]}});
  }
  // A proven geometry/group fallback returns before scanning the entire state
  // table a second time. Successful candidates still qualify every captured
  // state, including unreferenced states and late lighting errors.
  const char * commonDisabled = std::getenv("COIN_WGPU_DISABLE_INSTANCE_COMMON_STATE");
  const bool fastCommon = !(commonDisabled && std::strcmp(commonDisabled, "1") == 0);
  for (const auto & state : frame.renderStates) {
    if (!CoinRenderTransformCore::finiteMatrix(state.model) ||
        coin_render_alpha_test_active(state.alphaTestFunction) ||
        state.model[0][3] != 0 || state.model[1][3] != 0 || state.model[2][3] != 0 || state.model[3][3] != 1 ||
        std::memcmp(state.view.getValue(), first.view.getValue(), sizeof(float) * 16) ||
        std::memcmp(state.projectionCoin.getValue(), first.projectionCoin.getValue(), sizeof(float) * 16) ||
        !state.colorWrite || state.hasTexture || state.transparentMaterial || state.transparentTexture ||
        state.screenDoorTransparency > 0 || state.fogMode != CoinRenderFogMode::NONE ||
        !state.clipPlanesWorld.empty() || state.polygonOffsetEnabled ||
        state.lightModel != first.lightModel || state.lightingSlot != first.lightingSlot ||
        state.cullMode != first.cullMode || state.frontFace != first.frontFace ||
        state.viewportSlot != first.viewportSlot || state.depthTest != first.depthTest ||
        state.depthWrite != first.depthWrite || state.depthFunction != first.depthFunction ||
        state.depthRange[0] != first.depthRange[0] || state.depthRange[1] != first.depthRange[1]) return false;
    for (const auto & texture : state.extraTextures) if (texture.enabled) return false;
    if (fastCommon && !sameOpaquePackedFields(first, state)) return false;
  }
  this->bakeMatrices.resize(frame.renderStates.size());
  CoinWgpuRenderState common{};
  if (fastCommon) {
    ++this->opaqueMatricesCalculated;
    if (!this->packState(frame, first, width, height, common, diagnostic)) return false;
  }
  auto & cache = this->opaqueMatrixCache;
  static_assert(sizeof(MatrixCacheEntry) == 196, "Matrix cache accounting changed");
  const size_t cacheOverhead = sizeof(MatrixCache) + matrixCacheAllocatorAllowance;
  const size_t cacheLimit = (matrixCacheBudget - cacheOverhead) / sizeof(MatrixCacheEntry);
  bool useMatrixCache = fastCommon && this->opaqueMatrixCacheAllowed;
  bool reuseMatrices = useMatrixCache && previousMatrixCacheValid &&
    cache.count == frame.renderStates.size() && cache.fpControl == this->opaqueMatrixFpControl &&
    std::memcmp(cache.view.getValue(), first.view.getValue(), sizeof(float) * 16) == 0;
  if (useMatrixCache && frame.renderStates.size() > cacheLimit) {
    cache.entries.reset(); cache.capacity = cache.count = 0;
    this->opaqueMatrixCacheBypass = 3;
    useMatrixCache = reuseMatrices = false;
  }
  if (useMatrixCache && frame.renderStates.size() > cache.capacity) {
    // Release before growing: optional cache allocation never temporarily
    // doubles its independent budget and never turns a valid frame into OOM.
    cache.entries.reset(); cache.capacity = cache.count = 0;
    reuseMatrices = false;
    cache.entries.reset(new (std::nothrow) MatrixCacheEntry[frame.renderStates.size()]);
    if (cache.entries) {
      cache.capacity = frame.renderStates.size();
      ++this->opaqueMatrixCacheAllocations;
    }
    else {
      this->opaqueMatrixCacheBypass = 4;
      useMatrixCache = false;
    }
  }
  for (size_t i = 0; i < frame.renderStates.size(); ++i) {
    SbMatrix modelView, normal;
    uint32_t beforeFpStatus = 0;
    if (fastCommon) {
      // Qualify each complete packed common key before considering its matrix
      // hit, including unreferenced states and disabled transported metadata.
      if (useMatrixCache) beforeFpStatus = instanceMatrixFpStatus();
      if (i && reuseMatrices &&
          std::memcmp(cache.entries[i].model, frame.renderStates[i].model.getValue(), sizeof(float) * 16) == 0 &&
          cache.entries[i].unchangedFpStatus == (beforeFpStatus | unchangedMatrixFpStatus)) {
        this->bakeMatrices[i] = cache.entries[i].matrices;
        ++this->opaqueMatrixCacheHits;
        continue;
      }
      // Slot zero still passes the ordinary packer on a warm cache; account
      // for that validation even though instances retain world matrices.
      if (i) ++this->opaqueMatricesCalculated;
      // The shared state supplies the current view. Keep immutable instance
      // matrices in world space so camera patches need no relative transform.
      modelView = frame.renderStates[i].model;
      normal = CoinRenderTransformCore::normalMatrix(modelView);
    }
    else {
      // Literal pre-optimization path for controlled ablation.
      CoinWgpuRenderState key{};
      ++this->opaqueMatricesCalculated;
      if (!this->packState(frame, frame.renderStates[i], width, height, key, diagnostic)) return false;
      if (key.has_texture || key.fog_mode || key.clip_plane_count || key.polygon_offset_enabled) return false;
      modelView = frame.renderStates[i].model;
      normal = CoinRenderTransformCore::normalMatrix(modelView);
      if (!validOpaqueAuthoredMatrices(modelView, normal)) return false;
      auto & matrices = this->bakeMatrices[i];
      std::memcpy(matrices.modelView, modelView.getValue(), sizeof(matrices.modelView));
      std::memcpy(matrices.normal, normal.getValue(), sizeof(matrices.normal));
      key.material_slot = 0;
      std::memcpy(key.model_view, first.view.getValue(), sizeof(key.model_view));
      const SbMatrix viewNormal = CoinRenderTransformCore::normalMatrix(first.view);
      std::memcpy(key.normal_matrix, viewNormal.getValue(), sizeof(key.normal_matrix));
      const SbMatrix projection = coinWgpuProjection(frame, frame.renderStates[i], width, height);
      const SbMatrix mvp = first.view * projection;
      std::memcpy(key.model_view_projection, mvp.getValue(), sizeof(key.model_view_projection));
      if (i == 0) common = key;
      else if (std::memcmp(&common, &key, sizeof(key))) return false;
      continue;
    }
    if (!validOpaqueAuthoredMatrices(modelView, normal)) return false;
    auto & matrices = this->bakeMatrices[i];
    std::memcpy(matrices.modelView, modelView.getValue(), sizeof(matrices.modelView));
    std::memcpy(matrices.normal, normal.getValue(), sizeof(matrices.normal));
    if (useMatrixCache) {
      const uint32_t afterFpStatus = instanceMatrixFpStatus();
      auto & entry = cache.entries[i];
      std::memcpy(entry.model, frame.renderStates[i].model.getValue(), sizeof(entry.model));
      entry.matrices = matrices;
      // A hit may omit operations only when they provably left sticky flags
      // unchanged, under the exact same incoming SSE/x87 exception status.
      entry.unchangedFpStatus = beforeFpStatus == afterFpStatus
        ? beforeFpStatus | unchangedMatrixFpStatus : 0;
    }
  }
  if (useMatrixCache) {
    cache.count = frame.renderStates.size();
    cache.view = first.view;
    cache.fpControl = this->opaqueMatrixFpControl;
  }
  if (fastCommon) {
    common.material_slot = 0;
    std::memcpy(common.model_view, first.view.getValue(), sizeof(common.model_view));
    const SbMatrix viewNormal = CoinRenderTransformCore::normalMatrix(first.view);
    std::memcpy(common.normal_matrix, viewNormal.getValue(), sizeof(common.normal_matrix));
    const SbMatrix projection = coinWgpuProjection(frame, first, width, height);
    const SbMatrix mvp = first.view * projection;
    std::memcpy(common.model_view_projection, mvp.getValue(), sizeof(common.model_view_projection));
  }
  // Match Rust's conservative global bounds exactly: all canonical meshes
  // contribute to one coordinate/normal maximum, including mesh reuse across
  // different draws. No per-occurrence vertex expansion is needed.
  double sourcePosition = 0, sourceNormal = 0, worldBound = 0;
  for (const auto & mesh : canonicalMeshes) for (int c = 0; c < 3; ++c) {
    sourcePosition = std::max(sourcePosition, mesh.maxPosition[c]);
    sourceNormal = std::max(sourceNormal, mesh.maxNormal[c]);
  }
  for (size_t i = 0; i < order.size(); ++i) {
    const auto & matrix = this->bakeMatrices[frame.draws[order[i].drawIndex].renderStateSlot];
    float transport[16];
    if (!diagonalPositionMatrix(matrix.modelView, occurrences[i].scale, transport)) return false;
    for (int c = 0; c < 3; ++c) {
      const double position = instanceMatrixBound(transport, sourcePosition, c, true);
      const double normal = instanceMatrixBound(matrix.normal, sourceNormal, c, false);
      if (position > 1.0e30 || normal > double(1.0e18f)) return false;
      worldBound = std::max(worldBound, position);
    }
  }
  if (!instanceCommonMatricesValid(common, worldBound)) return false;
  uint64_t requiredBytes = 0;
  if (!coin_render_transparency_budget(width, height, frame.transparency, false, requiredBytes, diagnostic))
    return false;
  // The entire candidate is qualified before the first output write.
  this->opaqueIncrementalValid = false;
  this->vertices.resize(static_cast<size_t>(compactVertices));
  this->indices.resize(static_cast<size_t>(compactIndices));
  for (const auto & mesh : canonicalMeshes) {
    for (uint32_t v = 0; v < mesh.source.vertexCount; ++v) {
      auto & target = this->vertices[mesh.firstVertex + v];
      packVertex(frame.vertices[mesh.source.firstVertex + v], target);
      if (mesh.diagonal) for (int c = 0; c < 3; ++c)
        if (target.position[c] != 0) target.position[c] = std::copysign(1.0f, target.position[c]);
      target.material_slot = 0;
    }
    for (uint32_t j = 0; j < mesh.source.indexCount; ++j)
      this->indices[mesh.firstIndex + j] = mesh.firstVertex +
        frame.indices[mesh.source.firstIndex + j] - mesh.source.firstVertex;
  }
  this->instances.resize(order.size());
  for (size_t i = 0; i < order.size(); ++i) {
    const auto & draw = frame.draws[order[i].drawIndex];
    auto & instance = this->instances[i];
    const auto & matrix = this->bakeMatrices[draw.renderStateSlot];
    // Position factorization is not an authored model change: the captured
    // normal attribute and authored normal matrix remain exactly the same.
    // The inverse/residual proof above applies before this transport scale.
    diagonalPositionMatrix(matrix.modelView, occurrences[i].scale, instance.model_view);
    std::memcpy(instance.normal_matrix, matrix.normal, sizeof(instance.normal_matrix));
    instance.material_slot = frame.renderStates[draw.renderStateSlot].materialSlot;
    instance.reserved[0] = instance.reserved[1] = instance.reserved[2] = 0;
  }
  this->draws.assign(groups.size(), CoinWgpuDraw{});
  this->instanceRanges.resize(groups.size());
  for (size_t i = 0; i < groups.size(); ++i) {
    const auto & group = groups[i];
    const auto & mesh = canonicalMeshes[group.canonical];
    const auto & source = frame.draws[group.sourceDraw];
    auto & draw = this->draws[i];
    draw.first_vertex = mesh.firstVertex; draw.vertex_count = mesh.source.vertexCount;
    draw.first_index = mesh.firstIndex; draw.index_count = mesh.source.indexCount;
    draw.draw_ordinal = source.drawOrdinal; draw.source_revision = source.sourceRevision;
    this->instanceRanges[i] = {static_cast<uint32_t>(i), static_cast<uint32_t>(group.firstInstance),
                              static_cast<uint32_t>(group.count), 0};
  }
  this->states.assign(1, common);
  this->opaqueInstancePositionBound = worldBound;
  this->opaqueDiagonalLowered = diagonalSpans != 0;
  this->opaqueBatched = this->opaqueInstanced = this->opaqueGeometryPatchable = true;
  this->opaqueCommonStatesPacked = fastCommon ? 1 : frame.renderStates.size();
  this->opaqueCameraMatricesQualified = fastCommon;
  cache.candidate = useMatrixCache;
  if (std::getenv("COIN_RENDER_TRACE_PHASES")) {
    std::fprintf(stderr, "COIN_RENDER_PHASE wgpu_opaque_instancing source_draws=%zu source_ranges=%zu diagonal_ranges=%zu hashed_ranges=%zu canonical_meshes=%zu groups=%zu compact_vertices=%llu compact_indices=%llu instances=%zu instance_bytes=%llu common_state_fast=%d states_packed=%zu matrix_cache_hits=%zu matrix_calculated=%zu matrix_cache_bytes=%zu matrix_cache_allocations=%zu matrix_cache_bypass=%u\n",
      frame.draws.size(), sourceMemo.size(), diagonalSpans, this->opaqueHashedRanges, canonicalMeshes.size(), groups.size(),
      static_cast<unsigned long long>(compactVertices), static_cast<unsigned long long>(compactIndices),
      this->instances.size(), static_cast<unsigned long long>(this->instances.size()) * sizeof(CoinWgpuInstance),
      fastCommon ? 1 : 0, this->opaqueCommonStatesPacked, this->opaqueMatrixCacheHits,
      this->opaqueMatricesCalculated, this->opaqueMatrixCacheBytes(), this->opaqueMatrixCacheAllocations,
      this->opaqueMatrixCacheBypass);
  }
  return true;
}

// Qualify without allocating one 2292-byte GPU state per occurrence. Every
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
    if (!state.colorWrite || state.hasTexture || state.fogMode != CoinRenderFogMode::NONE ||
        coin_render_alpha_test_active(state.alphaTestFunction) ||
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
  CoinRenderCompositionScheduleView order("wgpu_early_batch");
  std::string diagnostic;
  if (!order.prepare(frame, diagnostic, preflight) ||
      order.size() != frame.draws.size() || order.size() < 256) return false;
  uint64_t nextVertex = 0, nextIndex = 0;
  for (const auto & item : order) {
    const auto & draw = frame.draws[item.drawIndex];
    const auto & range = draw.geometry;
    if (draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST ||
        draw.stableNodeId || item.blend || item.screenDoorLevel ||
        draw.renderLayer || draw.clearDepthBefore || !range.vertexCount ||
        !range.indexCount || range.indexCount % 3 ||
        draw.renderStateSlot >= frame.renderStates.size() ||
        item.firstIndex != range.firstIndex || item.indexCount != range.indexCount) return false;
    const auto & state = frame.renderStates[draw.renderStateSlot];
    // SCREEN_DOOR forces primary alpha to one even when its quantized mask is
    // zero. Removing that flag is equivalent only for truly opaque materials.
    if (item.screenDoor && (state.materialSlot >= frame.materials.size() ||
        frame.materials[state.materialSlot].diffuse[3] != 1.0f ||
        frame.materials[state.materialSlot].transparency != 0.0f)) return false;
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
    const SbMatrix projection = coinWgpuProjection(frame, frame.renderStates[i], width, height);
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
CoinWgpuFfiFrame::batchOpaqueTriangles(const CoinRenderFramePlan & frame, uint32_t width, uint32_t height)
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
    const SbMatrix projection = coinWgpuProjection(frame, frame.renderStates[draw.render_state_slot], width, height);
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
  if (!coin_render_alpha_test_valid(src.alphaTestFunction, src.alphaTestReference)) {
    outDiagnostic = "Invalid alpha comparison function or reference";
    return false;
  }
  dst.alpha_test_function = static_cast<uint32_t>(src.alphaTestFunction);
  if (src.textureProjection != CoinRenderTextureProjection::PROJECTIVE &&
      src.textureProjection != CoinRenderTextureProjection::DIRECT_ST) {
    outDiagnostic = "Invalid texture projection policy";
    return false;
  }
  dst.alpha_test_reference = src.alphaTestReference;
  dst.texture_projection = static_cast<uint32_t>(src.textureProjection);
  dst.clip_plane_count = static_cast<uint32_t>(src.clipPlanesWorld.size());
  if (!coin_render_clip_equations(src, dst.clip_planes, outDiagnostic)) return false;
  const SbMatrix modelView = src.model * src.view;
  const SbMatrix normalMatrix = CoinRenderTransformCore::normalMatrix(modelView);
  const SbMatrix projectionWgpu = coinWgpuProjection(frame, src, targetWidth, targetHeight);
  const SbMatrix mvpWgpu = modelView * projectionWgpu;

  std::memcpy(dst.model_view, modelView.getValue(), sizeof(float) * 16);
  std::memcpy(dst.model_view_projection, mvpWgpu.getValue(), sizeof(float) * 16);
  std::memcpy(dst.normal_matrix, normalMatrix.getValue(), sizeof(float) * 16);
  if (src.rasterShadowAnchorValid) {
    if (src.cameraSlot >= frame.cameras.size()) {
      outDiagnostic = "Raster shadow receiver lacks its observer camera";
      return false;
    }
    SbVec3f eyeAnchor;
    frame.cameras[src.cameraSlot].viewMatrix.multVecMatrix(
      src.rasterShadowAnchorWorld, eyeAnchor);
    if (!std::isfinite(eyeAnchor[0]) || !std::isfinite(eyeAnchor[1]) ||
        !std::isfinite(eyeAnchor[2])) {
      outDiagnostic = "Raster shadow receiver has a non-finite eye anchor";
      return false;
    }
    const SbMatrix constantView(0, 0, 0, 0, 0, 0, 0, 0,
                                0, 0, 0, 0, eyeAnchor[0], eyeAnchor[1], eyeAnchor[2], 1);
    std::memcpy(dst.model_view, constantView.getValue(), sizeof(dst.model_view));
  }

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
  if (src.rasterTextureSourceSlot != UINT32_MAX) {
    if (!src.rasterPixels || !src.hasTexture || src.textureImageSlot >= frame.textures.size() ||
        src.samplerSlot >= frame.samplers.size() || src.rasterTextureSourceSlot >= frame.renderStates.size() ||
        frame.renderStates[src.rasterTextureSourceSlot].rasterPixels ||
        frame.renderStates[src.rasterTextureSourceSlot].rasterTextureSourceSlot != UINT32_MAX) {
      outDiagnostic = "Invalid inherited image source state"; return false;
    }
    const auto & source = frame.renderStates[src.rasterTextureSourceSlot];
    if (static_cast<unsigned>(source.textureProjection) > 1) {
      outDiagnostic = "Invalid raster texture projection policy"; return false;
    }
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      const auto layer = coin_render_texture_unit(source, unit);
      const auto & raw = src.rasterTextureCoordinates[unit];
      if (src.rasterTextureProvenance[unit] > 1) {
        outDiagnostic = "Invalid raster texture provenance"; return false;
      }
      for (int c = 0; c < 4; ++c) if (!std::isfinite(raw[c])) {
        outDiagnostic = "Non-finite inherited raster coordinate"; return false;
      }
      if (!layer.enabled) continue;
      if (layer.imageSlot >= frame.textures.size() || layer.samplerSlot >= frame.samplers.size()) {
        outDiagnostic = "Invalid inherited image resource slot"; return false;
      }
      SbVec4f transformed;
      layer.matrix.multVecMatrix(raw, transformed);
      for (int c = 0; c < 4; ++c) if (!std::isfinite(transformed[c])) {
        outDiagnostic = "Non-finite inherited raster matrix result"; return false;
      }
      if (!frame.textures[layer.imageSlot].cubeFaceSize && source.textureProjection == CoinRenderTextureProjection::PROJECTIVE &&
          (transformed[3] == 0 || !std::isfinite(transformed[0]/transformed[3]) ||
           !std::isfinite(transformed[1]/transformed[3]) || !std::isfinite(transformed[2]/transformed[3]))) {
        outDiagnostic = "Invalid inherited raster projection"; return false;
      }
    }
  }
  dst.flat_shadow_primary = src.flatShadowPrimary ? 1u : 0u;
  dst.raster_texture_source_slot = src.rasterTextureSourceSlot == UINT32_MAX ? 0u : src.rasterTextureSourceSlot + 1u;
  for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit)
    for (size_t channel = 0; channel < 4; ++channel)
      dst.raster_texture_coordinates[unit][channel] = src.rasterTextureCoordinates[unit][channel];
  dst.front_face = static_cast<uint32_t>(src.frontFace);
  dst.light_model = static_cast<uint32_t>(src.lightModel);
  std::memcpy(dst.texture_matrix, src.textureMatrix.getValue(), sizeof(float) * 16);
  std::memcpy(dst.texture_combines, src.textureCombines.data(), sizeof(dst.texture_combines));
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
    int32_t clipped[4];
    CoinRenderTransformCore::clippedViewportTransform(viewport, targetWidth, targetHeight, clipped);
    dst.viewport[0] = clipped[0];
    dst.viewport[1] = static_cast<int32_t>(targetHeight) - clipped[1] - clipped[3];
    dst.viewport[2] = clipped[2];
    dst.viewport[3] = clipped[3];
    // [0,0,0,1] encodes an empty Core intersection; [0,0,0,0]
    // remains the bridge's legacy full-target default.
    if (!clipped[2]) { dst.viewport[0] = dst.viewport[1] = 0; dst.viewport[3] = 1; }
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
  dst.portable_shader_slot = src.portableShaderSlot;
  dst.color_write = !src.colorWrite || src.colorWriteMask == 0 ? 0u :
                    src.colorWriteMask == 0x0f ? 1u : (0x10u | src.colorWriteMask);
  dst.depth_function = static_cast<uint32_t>(src.depthFunction);
  dst.depth_range[0] = src.depthRange[0];
  dst.depth_range[1] = src.depthRange[1];
  dst.polygon_offset_enabled = src.polygonOffsetEnabled ? 1u : 0u;
  dst.polygon_offset_factor = src.polygonOffsetFactor;
  dst.polygon_offset_units = src.polygonOffsetUnits;
  dst.polygon_offset_slope_bias = src.polygonOffsetSlopeBias;
  dst.polygon_offset_max_depth_bits = packedMaximumDepth(src.polygonOffsetMaxDepth);
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
  this->sourceStateSlots.resize(frame.renderStates.size());
  for (size_t i = 0; i < frame.renderStates.size(); ++i) this->sourceStateSlots[i] = uint32_t(i);
  for (size_t i = 0; i < frame.renderStates.size(); ++i) {
    const CoinRenderRenderStateSnapshot & src = frame.renderStates[i];
    CoinWgpuRenderState & dst = this->states[i];
    if (!this->packState(frame, src, targetWidth, targetHeight, dst, outDiagnostic)) return false;
  }
  std::vector<CoinRenderCompositionItem> order;
  if (!coin_render_composition_schedule(frame, order, outDiagnostic, preflight))
    return false;
  // Ordinary shadow lighting suppresses enabled lights which do not own a
  // map. Tangent-space bump accumulates every authored light instead. The
  // same source state can serve both occurrences, because bump lives on the
  // indexed vertices rather than in RenderState.
  std::vector<uint8_t> bumpItems;
  std::vector<uint8_t> lightingUse;
  std::vector<uint32_t> bumpLightingStates;
  const bool activeBump = std::any_of(frame.bumpCoords.begin(), frame.bumpCoords.end(),
    [](const CoinRenderBumpCoordOverride &corner) { return corner.imageSlot != 0; });
  if (activeBump) {
    std::vector<uint8_t> activeVertices(frame.vertices.size(), 0);
    for (const auto &corner : frame.bumpCoords) {
      if (corner.vertexIndex >= activeVertices.size() || corner.imageSlot > frame.bumpImages.size()) {
        outDiagnostic = "Invalid bump lighting occurrence vertex or image slot";
        return false;
      }
      if (corner.imageSlot) activeVertices[corner.vertexIndex] = 1;
    }
    bumpItems.assign(order.size(), 0);
    lightingUse.assign(frame.renderStates.size(), 0);
    bumpLightingStates.assign(frame.renderStates.size(), UINT32_MAX);
    for (size_t itemIndex = 0; itemIndex < order.size(); ++itemIndex) {
      const auto &item = order[itemIndex];
      if (item.drawIndex >= frame.draws.size()) {
        outDiagnostic = "Invalid bump lighting composition draw";
        return false;
      }
      const auto &draw = frame.draws[item.drawIndex];
      const size_t first = item.firstIndex;
      if (draw.renderStateSlot >= lightingUse.size() || first > frame.indices.size() ||
          item.indexCount > frame.indices.size() - first) {
        outDiagnostic = "Invalid bump lighting occurrence state or index range";
        return false;
      }
      bool consumed = false;
      for (size_t i = first; i < first + item.indexCount; ++i) {
        const uint32_t vertex = frame.indices[i];
        if (vertex >= activeVertices.size()) {
          outDiagnostic = "Invalid indexed bump lighting vertex";
          return false;
        }
        consumed = consumed || activeVertices[vertex] != 0;
      }
      // Map-only caster lighting remains owned by ShadowCore.
      consumed = consumed && !draw.shadowLightSlot;
      bumpItems[itemIndex] = consumed ? 1 : 0;
      lightingUse[draw.renderStateSlot] |= consumed ? 2 : 1;
    }
  }
  const auto restoreBumpLighting = [&](uint32_t sourceSlot, CoinWgpuRenderState &packed) {
    const auto &source = frame.renderStates[sourceSlot];
    if (source.lightModel != CoinRenderLightModel::PHONG ||
        source.lightingSlot >= frame.lightingStates.size()) return false;
    const auto &lights = frame.lightingStates[source.lightingSlot].lights;
    bool restored = false;
    for (size_t i = 0; i < lights.size(); ++i) {
      if (coin_render_shadow_suppresses_ordinary_light(frame, source, lights[i])) {
        packed.lights[i].color_intensity[3] = lights[i].intensity;
        restored = true;
      }
    }
    return restored;
  };
  for (size_t slot = 0; slot < lightingUse.size(); ++slot)
    if (lightingUse[slot] == 2) restoreBumpLighting(static_cast<uint32_t>(slot), this->states[slot]);
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
  for (size_t itemIndex = 0; itemIndex < order.size(); ++itemIndex) {
    const auto &item = order[itemIndex];
    const auto & src = frame.draws[item.drawIndex];
    const auto & state = frame.renderStates[src.renderStateSlot];
    CoinWgpuDraw dst{};
    dst.topology = static_cast<uint32_t>(src.topology);
    dst.first_vertex = src.geometry.firstVertex;
    dst.vertex_count = src.geometry.vertexCount;
    dst.first_index = item.firstIndex;
    dst.index_count = item.indexCount;
    dst.render_state_slot = src.renderStateSlot;
    if (!bumpItems.empty() && bumpItems[itemIndex] && lightingUse[src.renderStateSlot] == 3) {
      auto &bumpSlot = bumpLightingStates[src.renderStateSlot];
      if (bumpSlot == UINT32_MAX) {
        CoinWgpuRenderState bumpState = this->states[src.renderStateSlot];
        bumpSlot = src.renderStateSlot;
        if (restoreBumpLighting(src.renderStateSlot, bumpState)) {
          bumpSlot = static_cast<uint32_t>(this->states.size());
          this->states.push_back(bumpState);
          this->sourceStateSlots.push_back(src.renderStateSlot);
          shadow.appendReceiverState(src.renderStateSlot);
        }
      }
      dst.render_state_slot = bumpSlot;
    }
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
        (frame.legacyBlendAlpha && item.blend && !item.additive &&
          item.transparencyStrategy != CoinRenderCompositionItem::SORTED_LAYERS &&
          item.transparencyStrategy != CoinRenderCompositionItem::WEIGHTED_OIT ? 32u : 0u) |
        (src.authoredScene ? 64u : 0u) | (item.screenDoorLevel << 8) |
        (src.multipassMask << 15) | (src.multipassRasterFixed ? 0x80000000u : 0u);
    dst.source_revision = src.sourceRevision;
    dst.render_layer = src.renderLayer;
    dst.clear_depth_before = src.clearDepthBefore ? 1u : 0u;
    if (item.depthTest != state.depthTest || item.depthWrite != state.depthWrite ||
        item.depthFunction != state.depthFunction ||
        item.depthRange[0] != state.depthRange[0] || item.depthRange[1] != state.depthRange[1]) {
      const uint32_t receiverSource = dst.render_state_slot;
      CoinWgpuRenderState resolved = this->states[receiverSource];
      resolved.depth_test = item.depthTest ? 1u : 0u;
      resolved.depth_write = item.depthWrite ? 1u : 0u;
      resolved.depth_function = static_cast<uint32_t>(item.depthFunction);
      resolved.depth_range[0] = item.depthRange[0];
      resolved.depth_range[1] = item.depthRange[1];
      dst.render_state_slot = static_cast<uint32_t>(this->states.size());
      this->states.push_back(resolved);
      this->sourceStateSlots.push_back(this->sourceStateSlots[receiverSource]);
      shadow.appendReceiverState(receiverSource);
    }
    resolvedDraws.push_back(dst);
  }
  this->draws.swap(resolvedDraws);
  this->multipassMatrices.clear();
  if (frame.numPasses > 1) {
    if (this->states.size() > SIZE_MAX / (16u * size_t(frame.numPasses))) {
      outDiagnostic = "Multipass MVP payload count overflows";
      return false;
    }
    this->multipassMatrices.reserve(this->states.size() * 16u * size_t(frame.numPasses));
    for (int pass = 0; pass < frame.numPasses; ++pass)
      for (size_t slot = 0; slot < this->states.size(); ++slot) {
        auto source = frame.renderStates[this->sourceStateSlots[slot]];
        if (!source.rasterPixels &&
            !CoinRenderMultipassCore::projection(source.projectionCoin, frame.numPasses, pass,
              frame.viewports[source.viewportSlot], source.projectionCoin)) {
          outDiagnostic = "Invalid multipass projection payload";
          return false;
        }
        const SbMatrix mvp = source.model * source.view *
          coinWgpuProjection(frame, source, targetWidth, targetHeight);
        const float * values = &mvp.getValue()[0][0];
        for (size_t component = 0; component < 16; ++component) {
          if (!std::isfinite(values[component])) {
            outDiagnostic = "Non-finite multipass MVP payload";
            return false;
          }
          this->multipassMatrices.push_back(values[component]);
        }
      }
  }
  return true;
}

void
CoinWgpuFfiFrame::bindView(const CoinRenderFramePlan & frame, uint32_t width, uint32_t height)
{
  this->portableStorage.clear();
  this->portableViews.clear();
  this->portableStorage.reserve(frame.portableShaders.size());
  this->portableViews.reserve(frame.portableShaders.size());
  for (const auto &instance : frame.portableShaders) {
    PortableStorage storage;
    storage.parameters = instance.parameters;
    storage.signature = coin_render_shader_key(instance.module, instance.ir);
    storage.nodes.reserve(instance.ir.nodes.size());
    for (const auto &node : instance.ir.nodes) {
      CoinWgpuPortableNode packed = {};
      packed.op = static_cast<uint32_t>(node.op);
      packed.type = static_cast<uint32_t>(node.type);
      std::copy(node.inputs.begin(), node.inputs.end(), packed.inputs);
      std::copy(node.value.begin(), node.value.end(), packed.value);
      packed.resource = node.resource;
      storage.nodes.push_back(packed);
    }
    this->portableStorage.push_back(std::move(storage));
  }
  for (size_t slot = 0; slot < frame.portableShaders.size(); ++slot) {
    const auto &storage = this->portableStorage[slot];
    CoinWgpuPortableModule packed = {};
    packed.nodes = storage.nodes.data(); packed.node_count = storage.nodes.size();
    packed.parameters = storage.parameters.empty() ? nullptr : storage.parameters.data();
    packed.parameter_count = storage.parameters.size();
    packed.output = frame.portableShaders[slot].ir.output;
    packed.alpha_may_vary = frame.portableShaders[slot].ir.alphaMayVary ? 1 : 0;
    packed.signature = reinterpret_cast<const uint8_t *>(storage.signature.data());
    packed.signature_bytes = storage.signature.size();
    this->portableViews.push_back(packed);
  }
  this->view = CoinWgpuFrameView{};
  this->view.portable_modules = this->portableViews.empty() ? nullptr : this->portableViews.data();
  this->view.portable_module_count = this->portableViews.size();
  this->view.multipass_count = static_cast<uint32_t>(frame.numPasses);
  this->view.multipass_reserved = 0;
  this->view.multipass_budget_bytes = frame.multipassBufferBudget;
  this->view.multipass_matrices = frame.numPasses > 1 && !this->multipassMatrices.empty() ? this->multipassMatrices.data() : nullptr;
  this->view.multipass_matrix_count = frame.numPasses > 1 ? this->multipassMatrices.size() : 0;

  this->view.abi_version = COIN_WGPU_ABI_VERSION;
  this->view.struct_size = sizeof(CoinWgpuFrameView);
  this->view.frame_revision = frame.revision;
  this->view.texture_sampling_policy = uint32_t(frame.textureSamplingPolicy);
  this->view.rasterization_policy = uint32_t(frame.rasterizationPolicy);
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
  this->view.instances = this->instances.empty() ? NULL : this->instances.data();
  this->view.instance_count = static_cast<uint64_t>(this->instances.size());
  this->view.instance_ranges = this->instanceRanges.empty() ? NULL : this->instanceRanges.data();
  this->view.instance_range_count = static_cast<uint64_t>(this->instanceRanges.size());
  this->view.texture_output_flags=(frame.outputColorFormat==CoinRenderTextureFormat::RGBA16_FLOAT?1u:0u)|(frame.outputMipmaps?2u:0u);
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

bool
CoinWgpuFfiFrame::prepareBumpPayload(
  const CoinRenderFramePlan & frame,
  const std::vector<uint32_t> & sourceVertexForEmitted,
  std::string & diagnostic)
{
  std::vector<CoinRenderBumpGpuVertex> source, emitted;
  if (!coin_render_build_bump_gpu_stream(frame, source, diagnostic)) return false;
  if (source.empty()) {
    this->bumpVertices.clear();
    this->bumpImages.clear();
    this->bumpPixels.clear();
    this->view.bump_vertices = NULL;
    this->view.bump_vertex_count = 0;
    this->view.bump_images = NULL;
    this->view.bump_image_count = 0;
    diagnostic.clear();
    return true;
  }
  if (this->view.abi_version != COIN_WGPU_ABI_VERSION ||
      this->view.frame_revision != frame.revision ||
      sourceVertexForEmitted.size() != this->view.vertex_count) {
    diagnostic = "Bump source mapping does not match packed wgpu frame";
    return false;
  }
  if (!coin_render_remap_bump_gpu_stream(source, sourceVertexForEmitted,
                                         emitted, diagnostic)) return false;
  try {
    std::vector<CoinWgpuBumpVertex> vertices(emitted.size());
    for (size_t i = 0; i < emitted.size(); ++i) {
      std::memcpy(vertices[i].coord, emitted[i].coord, sizeof(vertices[i].coord));
      std::memcpy(vertices[i].tangent_s, emitted[i].tangentS, sizeof(vertices[i].tangent_s));
      std::memcpy(vertices[i].tangent_t, emitted[i].tangentT, sizeof(vertices[i].tangent_t));
      vertices[i].image_slot = emitted[i].imageSlot;
    }
    std::vector<std::vector<uint8_t> > pixels(frame.bumpImages.size());
    std::vector<CoinWgpuBumpImage> images(frame.bumpImages.size());
    for (size_t i = 0; i < frame.bumpImages.size(); ++i) {
      const auto & sourceImage = frame.bumpImages[i];
      if (!sourceImage.width || !sourceImage.height ||
          uint64_t(sourceImage.width) * sourceImage.height * 3 != sourceImage.rgb.size()) {
        diagnostic = "Invalid bump image at wgpu transport boundary";
        return false;
      }
      pixels[i] = sourceImage.rgb;
      images[i].width = sourceImage.width;
      images[i].height = sourceImage.height;
      images[i].clamp_s = sourceImage.clampS ? 1u : 0u;
      images[i].clamp_t = sourceImage.clampT ? 1u : 0u;
      images[i].rgb = pixels[i].data();
      images[i].rgb_len = pixels[i].size();
    }
    this->bumpVertices.swap(vertices);
    this->bumpImages.swap(images);
    this->bumpPixels.swap(pixels);
    this->view.bump_vertices = this->bumpVertices.data();
    this->view.bump_vertex_count = this->bumpVertices.size();
    this->view.bump_images = this->bumpImages.data();
    this->view.bump_image_count = this->bumpImages.size();
    diagnostic.clear();
    return true;
  } catch (const std::bad_alloc &) {
    diagnostic = "Bump wgpu transport allocation failed";
    return false;
  }
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

size_t
CoinWgpuFfiFrame::opaqueHashedRangesLastPrepare() const
{
  return this->opaqueHashedRanges;
}

size_t
CoinWgpuFfiFrame::opaqueCommonStatesPackedLastPrepare() const
{
  return this->packedRevision ? this->opaqueCommonStatesPacked : 0;
}

size_t
CoinWgpuFfiFrame::opaqueCameraProofReusedLastPrepare() const
{
  return this->packedRevision ? this->opaqueCameraProofReused : 0;
}

size_t CoinWgpuFfiFrame::opaqueMatrixCacheHitsLastPrepare() const { return this->opaqueMatrixCacheHits; }
size_t CoinWgpuFfiFrame::opaqueMatricesCalculatedLastPrepare() const { return this->opaqueMatricesCalculated; }
size_t CoinWgpuFfiFrame::opaqueMatrixCacheBytes() const {
  return this->opaqueMatrixCache.capacity ? sizeof(MatrixCache) + matrixCacheAllocatorAllowance +
    this->opaqueMatrixCache.capacity * sizeof(MatrixCacheEntry) : 0;
}
size_t CoinWgpuFfiFrame::opaqueMatrixCacheAllocationsLastPrepare() const { return this->opaqueMatrixCacheAllocations; }
uint32_t CoinWgpuFfiFrame::opaqueMatrixCacheBypassLastPrepare() const { return this->opaqueMatrixCacheBypass; }
bool CoinWgpuFfiFrame::opaqueMatrixCacheValid() const { return this->opaqueMatrixCache.valid; }
