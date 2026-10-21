#include "rendering/coinrender/CoinRenderMultipassCore.h"
#include "rendering/coinrender/CoinRenderCubeTextureCore.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderShadowCameraMath.h"
#include "rendering/coinrender/CoinRenderLightEqualityCore.h"
#include "rendering/coinrender/CoinRenderDrawEqualityCore.h"
#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"
#include "rendering/coinrender/CoinRenderAlphaTestCore.h"
#include "rendering/coinrender/CoinRenderTextureAlphaCore.h"
#include <Inventor/nodes/SoSceneTexture2.h>
#include "rendering/coinrender/CoinRenderTextureCombineCore.h"
#include "rendering/coinrender/CoinRenderClipCore.h"
#include "rendering/coinrender/CoinRenderStateCore.h"
#include "rendering/coinrender/CoinRenderPhaseTimer.h"
#include "rendering/coinrender/CoinRenderFloatCore.h"
#include "rendering/coinrender/CoinRenderTextureCoordinateCore.h"

#include <cmath>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "rendering/coinrender/CoinRenderComposition.h"

namespace
{
// One owned program per unit, valid only during this isValid invocation. The
// source frame is immutable here; neither revision nor source identity licenses
// a result from a previous validation or a different unit.
class CombineValidationMemo
{
public:
  CombineValidationMemo()
  {
    const char *disabled = std::getenv("COIN_RENDER_DISABLE_COMBINE_VALIDATION_MEMO");
    enabled = !(disabled && std::strcmp(disabled, "1") == 0);
    tracing = std::getenv("COIN_RENDER_TRACE_PHASES") || std::getenv("COIN_WGPU_TRACE_PHASES");
  }
  ~CombineValidationMemo()
  {
    if (tracing)
      std::fprintf(stderr,
                   "COIN_RENDER_PHASE combine_validation_memo enabled=%d programs_checked=%zu "
                   "cache_hits=%zu validated=%zu\n",
                   enabled ? 1 : 0, checked, hits, validated);
  }
  bool
  validate(size_t unit, const CoinRenderTextureCombineSnapshot &program)
  {
    ++checked;
    if (enabled && valid[unit] &&
        std::memcmp(cached[unit].instructions, program.instructions,
                    sizeof(program.instructions)) == 0) {
      ++hits;
      return true;
    }
    ++validated; // Calls to the original validator, including a rejected miss.
    if (!coin_render_validate_combine(program))
      return false;
    if (enabled) {
      std::memcpy(cached[unit].instructions, program.instructions, sizeof(program.instructions));
      valid[unit] = true;
    }
    return true;
  }

private:
  static_assert(sizeof(CoinRenderTextureCombineSnapshot) == 16 * sizeof(float),
                "Combine validation memo must own all sixteen program floats");
  CoinRenderTextureCombineSnapshot cached[COIN_RENDER_MAX_TEXTURE_UNITS];
  bool valid[COIN_RENDER_MAX_TEXTURE_UNITS] = {};
  bool enabled, tracing;
  size_t checked = 0, hits = 0, validated = 0;
};

template <typename T>
bool
samePlainSnapshots(const std::vector<T> &a, const std::vector<T> &b)
{
  // Padding may cause a conservative miss, but never makes distinct captured
  // bytes compare equal.
  return a.size() == b.size() &&
         (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}

bool
sameCameras(const std::vector<CoinRenderCameraSnapshot> &a,
            const std::vector<CoinRenderCameraSnapshot> &b)
{
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const CoinRenderCameraSnapshot &x = a[i];
    const CoinRenderCameraSnapshot &y = b[i];
    if (std::memcmp(x.viewMatrix.getValue(), y.viewMatrix.getValue(), sizeof(float) * 16) != 0 ||
        std::memcmp(x.projectionMatrixCoin.getValue(), y.projectionMatrixCoin.getValue(),
                    sizeof(float) * 16) != 0 ||
        x.isPerspective != y.isPerspective || x.nearDistance != y.nearDistance ||
        x.farDistance != y.farDistance || x.focalDistance != y.focalDistance ||
        x.aspectRatio != y.aspectRatio)
      return false;
  }
  return true;
}
// Shadow captures contain bool/alignment padding. Compare every captured
// field, retaining exact float/matrix bits and source identities, rather than
// making producer reuse depend on unspecified bytes between those fields.
bool sameShadowFloat(const float &a, const float &b)
{
  return std::memcmp(&a, &b, sizeof(float)) == 0;
}
bool sameShadowMatrix(const SbMatrix &a, const SbMatrix &b)
{
  return std::memcmp(a.getValue(), b.getValue(), sizeof(float) * 16) == 0;
}
bool sameShadowVector(const SbVec3f &a, const SbVec3f &b)
{
  return std::memcmp(a.getValue(), b.getValue(), sizeof(float) * 3) == 0;
}
bool sameShadowCamera(const CoinRenderCameraSnapshot &a, const CoinRenderCameraSnapshot &b)
{
  return sameShadowMatrix(a.viewMatrix, b.viewMatrix) &&
         sameShadowMatrix(a.projectionMatrixCoin, b.projectionMatrixCoin) &&
         a.isPerspective == b.isPerspective &&
         sameShadowFloat(a.nearDistance, b.nearDistance) &&
         sameShadowFloat(a.farDistance, b.farDistance) &&
         sameShadowFloat(a.focalDistance, b.focalDistance) &&
         sameShadowFloat(a.aspectRatio, b.aspectRatio);
}
bool sameShadowGroups(const std::vector<CoinRenderShadowGroupSnapshot> &a,
                      const std::vector<CoinRenderShadowGroupSnapshot> &b)
{
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const auto &x = a[i]; const auto &y = b[i];
    if (x.sourceRevision != y.sourceRevision ||
        !sameShadowFloat(x.intensity, y.intensity) ||
        !sameShadowFloat(x.precision, y.precision) ||
        !sameShadowFloat(x.quality, y.quality) ||
        !sameShadowFloat(x.epsilon, y.epsilon) ||
        !sameShadowFloat(x.threshold, y.threshold) ||
        !sameShadowFloat(x.smoothBorder, y.smoothBorder) ||
        x.shadowCachingEnabled != y.shadowCachingEnabled ||
        !sameShadowFloat(x.visibilityNearRadius, y.visibilityNearRadius) ||
        !sameShadowFloat(x.visibilityRadius, y.visibilityRadius) ||
        x.visibilityFlag != y.visibilityFlag || x.parentGroupSlot != y.parentGroupSlot ||
        x.hasEntryCamera != y.hasEntryCamera || !sameShadowCamera(x.entryCamera, y.entryCamera) ||
        !sameShadowMatrix(x.entryModel, y.entryModel)) return false;
  }
  return true;
}
bool sameShadowLights(const std::vector<CoinRenderShadowLightSnapshot> &a,
                      const std::vector<CoinRenderShadowLightSnapshot> &b)
{
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const auto &x = a[i]; const auto &y = b[i];
    if (x.groupSlot != y.groupSlot || x.sourceRevision != y.sourceRevision ||
        x.type != y.type || x.enabled != y.enabled || x.shadowEligible != y.shadowEligible ||
        x.hasCustomScene != y.hasCustomScene || x.mapSceneCaptured != y.mapSceneCaptured ||
        x.customSceneNodeId != y.customSceneNodeId || !sameShadowVector(x.color, y.color) ||
        !sameShadowFloat(x.intensity, y.intensity) || !sameShadowVector(x.attenuation, y.attenuation) ||
        !sameShadowMatrix(x.model, y.model) || !sameShadowMatrix(x.modelViewAtLight, y.modelViewAtLight) ||
        !sameShadowVector(x.position, y.position) || !sameShadowVector(x.direction, y.direction) ||
        !sameShadowFloat(x.cutOffAngle, y.cutOffAngle) || !sameShadowFloat(x.dropOffRate, y.dropOffRate) ||
        !sameShadowFloat(x.nearDistance, y.nearDistance) || !sameShadowFloat(x.farDistance, y.farDistance) ||
        !sameShadowFloat(x.maxShadowDistance, y.maxShadowDistance) ||
        !sameShadowVector(x.bboxCenter, y.bboxCenter) || !sameShadowVector(x.bboxSize, y.bboxSize)) return false;
  }
  return true;
}
bool sameBumpImages(const std::vector<CoinRenderBumpImage> & a,
                    const std::vector<CoinRenderBumpImage> & b)
{
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].width != b[i].width || a[i].height != b[i].height ||
        a[i].convertedHeight != b[i].convertedHeight ||
        a[i].clampS != b[i].clampS || a[i].clampT != b[i].clampT ||
        a[i].rgb != b[i].rgb) return false;
  return true;
}
} // namespace

bool
CoinRenderFramePlan::hasSamePayload(const CoinRenderFramePlan &other) const
{
  if (this->numPasses != other.numPasses ||
      this->multipassBufferBudget != other.multipassBufferBudget ||
      this->rasterizationPolicy != other.rasterizationPolicy ||
      this->textureSamplingPolicy != other.textureSamplingPolicy ||
      this->legacyBlendAlpha != other.legacyBlendAlpha ||
      !coin_render_same_transparency_options(this->transparency, other.transparency))
    return false;
  for (int i = 0; i < 4; ++i) {
    if (this->clearColor[i] != other.clearColor[i])
      return false;
  }
  if (this->outputColorFormat != other.outputColorFormat ||
      this->outputMipmaps != other.outputMipmaps ||
      !samePlainSnapshots(this->vertices, other.vertices) ||
      !samePlainSnapshots(this->bumpCoords, other.bumpCoords) ||
      !samePlainSnapshots(this->bumpTangents, other.bumpTangents) ||
      !sameBumpImages(this->bumpImages, other.bumpImages) ||
      !samePlainSnapshots(this->bumpTransforms, other.bumpTransforms) ||
      this->indices != other.indices ||
      !samePlainSnapshots(this->materials, other.materials) ||
      !sameShadowGroups(this->shadowGroups, other.shadowGroups) ||
      !sameShadowLights(this->shadowLights, other.shadowLights) ||
      !sameCameras(this->cameras, other.cameras) ||
      !samePlainSnapshots(this->viewports, other.viewports) ||
      this->renderStates.size() != other.renderStates.size() ||
      !samePlainSnapshots(this->samplers, other.samplers) ||
      !coin_render_same_draws(this->draws, other.draws) ||
      this->rasterOccurrences != other.rasterOccurrences ||
      this->lightingStates.size() != other.lightingStates.size() ||
      this->textures.size() != other.textures.size())
    return false;
  if (this->portableShaders.size() != other.portableShaders.size()) return false;
  for (size_t i = 0; i < this->portableShaders.size(); ++i) {
    const auto &a = this->portableShaders[i];
    const auto &b = other.portableShaders[i];
    if (coin_render_shader_key(a.module, a.ir) != coin_render_shader_key(b.module, b.ir) ||
        a.parameters.size() != b.parameters.size() ||
        (!a.parameters.empty() && std::memcmp(a.parameters.data(), b.parameters.data(), a.parameters.size() * sizeof(float)))) return false;
  }
  for (size_t i = 0; i < this->renderStates.size(); ++i) {
    const auto &a = this->renderStates[i];
    const auto &b = other.renderStates[i];
    if (!coin_render_same_state_except_camera(a, b) || a.view != b.view ||
        a.projectionCoin != b.projectionCoin)
      return false;
  }
  for (size_t i = 0; i < this->lightingStates.size(); ++i) {
    const CoinRenderLightingSnapshot &x = this->lightingStates[i];
    const CoinRenderLightingSnapshot &y = other.lightingStates[i];
    if (x.ambientIntensity != y.ambientIntensity ||
        std::memcmp(x.ambientColor, y.ambientColor, sizeof(x.ambientColor)) != 0 ||
        !coin_render_same_lights(x.lights, y.lights))
      return false;
  }
  for (size_t i = 0; i < this->textures.size(); ++i) {
    const CoinRenderTextureImageSnapshot &x = this->textures[i];
    const CoinRenderTextureImageSnapshot &y = other.textures[i];
    if (x.width != y.width || x.height != y.height || x.components != y.components ||
        x.cubeFaceSize != y.cubeFaceSize || x.cubeSourceComponents != y.cubeSourceComponents ||
        x.cubeMipmapped != y.cubeMipmapped || x.volumeWidth != y.volumeWidth || x.volumeHeight != y.volumeHeight ||
        x.volumeDepth != y.volumeDepth || x.volumeColumns != y.volumeColumns ||
        x.volumeSourceComponents != y.volumeSourceComponents ||
        x.volumeMipmapped != y.volumeMipmapped ||
        x.contentDigest != y.contentDigest || x.producerId != y.producerId ||
        x.gpuToken != y.gpuToken || x.gpuOpaque != y.gpuOpaque || x.format != y.format ||
        x.sceneTransparencyFunction != y.sceneTransparencyFunction ||
        x.pixelsRgba != y.pixelsRgba || x.mipmapped != y.mipmapped ||
        x.mipmapsRgba != y.mipmapsRgba)
      return false;
  }
  return true;
}

bool
CoinRenderFramePlan::hasFiniteNormalizedInputs(std::string *outDiagnostic) const
{
  for (const auto & camera : this->cameras) {
    if (!coin_render_is_finite_inline(camera.nearDistance) ||
        !coin_render_is_finite_inline(camera.farDistance) ||
        camera.farDistance <= camera.nearDistance) {
      if (outDiagnostic) *outDiagnostic = "Camera clip planes are invalid or inverted";
      return false;
    }
  }
  for (const auto & state : this->renderStates) {
    if (!coin_render_is_finite_inline(state.screenDoorTransparency)) {
      if (outDiagnostic) *outDiagnostic = "Invalid screen door transparency";
      return false;
    }
    if (!coin_render_is_finite_inline(state.lineWidth) ||
        !coin_render_is_finite_inline(state.pointSize)) {
      if (outDiagnostic) *outDiagnostic = "Invalid line or point style";
      return false;
    }
    if (!coin_render_is_finite_inline(state.fogStart) ||
        !coin_render_is_finite_inline(state.fogEnd)) {
      if (outDiagnostic) *outDiagnostic = "Invalid fog range";
      return false;
    }
  }
  return true;
}

bool
CoinRenderFramePlan::isValid(std::string *outDiagnostic) const
{
  if (!this->hasFiniteNormalizedInputs(outDiagnostic)) return false;
  if (rasterizationPolicy != COIN_RENDER_RASTER_NATIVE && rasterizationPolicy != COIN_RENDER_RASTER_CANONICAL_8BIT) {
    if (outDiagnostic) *outDiagnostic = "Invalid rasterization policy";
    return false;
  }
  if (outputColorFormat != CoinRenderTextureFormat::RGBA8_LINEAR &&
      outputColorFormat != CoinRenderTextureFormat::RGBA16_FLOAT) {
    if (outDiagnostic)
      *outDiagnostic = "Unsupported output texture format";
    return false;
  }

  std::string cubeDiagnostic;
  if (!CoinRenderCubeTextureCore::validate(*this, cubeDiagnostic)) {
    if (outDiagnostic) *outDiagnostic = cubeDiagnostic;
    return false;
  }
  if (numPasses < 1 || numPasses > 16) {
    if (outDiagnostic) *outDiagnostic = "Invalid multipass count (expected 1..16)";
    return false;
  }
  if (!CoinRenderMultipassCore::projectionsFinite(*this)) {
    if (outDiagnostic) *outDiagnostic = "Multipass translated projection is non-finite or invalid";
    return false;
  }
  CoinRenderPhaseTimer timer("validation_detail");
  CombineValidationMemo combineMemo;
  auto isFiniteF = [](float v) { return coin_render_is_finite_inline(v); };

  auto isMatrixFinite = [&](const SbMatrix &m) {
    const float (*mat)[4] = m.getValue();
    for (int r = 0; r < 4; ++r) {
      for (int c = 0; c < 4; ++c) {
        if (!isFiniteF(mat[r][c]))
          return false;
      }
    }
    return true;
  };

  for (int i = 0; i < 4; ++i) {
    if (!isFiniteF(this->clearColor[i]) ||
        (outputColorFormat == CoinRenderTextureFormat::RGBA16_FLOAT &&
         std::abs(this->clearColor[i]) > 65504.f)) {
      if (outDiagnostic)
        *outDiagnostic = "Invalid clearColor (NaN or inf)";
      return false;
    }
  }

  for (size_t i = 0; i < this->materials.size(); ++i) {
    const CoinRenderMaterialSnapshot &m = this->materials[i];
    if (!isFiniteF(m.transparency) || m.transparency < 0.0f || m.transparency > 1.0f ||
        !isFiniteF(m.diffuse[3]) || m.diffuse[3] < 0.0f || m.diffuse[3] > 1.0f) {
      if (outDiagnostic)
        *outDiagnostic = "Material has invalid transparency or diffuse alpha";
      return false;
    }
    if (std::abs(m.diffuse[3] + m.transparency - 1.0f) > 1.0e-5f) {
      if (outDiagnostic)
        *outDiagnostic = "Material alpha and transparency are inconsistent";
      return false;
    }
  }

  const size_t numVertices = this->vertices.size();
  for (const auto & matrix : this->bumpTransforms)
    if (!isMatrixFinite(matrix)) {
      if (outDiagnostic) *outDiagnostic = "Invalid bump transform snapshot";
      return false;
    }
  for (const auto & image : this->bumpImages) {
    if (!image.width || !image.height ||
        uint64_t(image.width) * image.height * 3u != image.rgb.size()) {
      if (outDiagnostic) *outDiagnostic = "Invalid bump image snapshot";
      return false;
    }
  }
  uint32_t previousBumpVertex = 0;
  bool firstBumpVertex = true;
  for (const auto & bump : this->bumpCoords) {
    if (bump.vertexIndex >= numVertices || bump.imageSlot > this->bumpImages.size() ||
        (bump.transformSlot && !bump.imageSlot) ||
        bump.transformSlot > this->bumpTransforms.size() ||
        (!firstBumpVertex && bump.vertexIndex <= previousBumpVertex) ||
        !isFiniteF(bump.coord[0]) || !isFiniteF(bump.coord[1])) {
      if (outDiagnostic) *outDiagnostic = "Invalid sparse bump coordinates";
      return false;
    }
    previousBumpVertex = bump.vertexIndex;
    firstBumpVertex = false;
  }
  uint32_t previousTangentVertex = 0;
  bool firstTangentVertex = true;
  size_t tangentCoordCursor = 0;
  for (const auto & bump : this->bumpTangents) {
    while (tangentCoordCursor < this->bumpCoords.size() &&
           this->bumpCoords[tangentCoordCursor].vertexIndex < bump.vertexIndex)
      ++tangentCoordCursor;
    bool valid = bump.vertexIndex < numVertices &&
      (firstTangentVertex || bump.vertexIndex > previousTangentVertex) &&
      tangentCoordCursor < this->bumpCoords.size() &&
      this->bumpCoords[tangentCoordCursor].vertexIndex == bump.vertexIndex &&
      this->bumpCoords[tangentCoordCursor].imageSlot != 0;
    for (float value : bump.tangent.s) valid = valid && isFiniteF(value);
    for (float value : bump.tangent.t) valid = valid && isFiniteF(value);
    if (!valid) {
      if (outDiagnostic) *outDiagnostic = "Invalid sparse bump tangents";
      return false;
    }
    previousTangentVertex = bump.vertexIndex;
    firstTangentVertex = false;
  }
  timer.mark("materials");
  for (size_t i = 0; i < numVertices; ++i) {
    const CoinRenderVertexSnapshot &v = this->vertices[i];
    for (int k = 0; k < 3; ++k) {
      if (!isFiniteF(v.position[k]) || !isFiniteF(v.normal[k])) {
        if (outDiagnostic)
          *outDiagnostic = "Vertex contains non-finite position or normal";
        return false;
      }
    }
    for (int k = 0; k < 2; ++k) {
      if (!isFiniteF(v.texcoord[k])) {
        if (outDiagnostic)
          *outDiagnostic = "Vertex contains non-finite texcoord";
        return false;
      }
    }
    if (!isFiniteF(v.screenSpaceW) || v.screenSpaceW <= 0.0f || !isFiniteF(v.fogEyeDepth)) {
      if (outDiagnostic)
        *outDiagnostic = "Invalid expanded primitive attributes";
      return false;
    }
    for (size_t u = 0; u < COIN_RENDER_MAX_TEXTURE_UNITS - 1; ++u)
      for (int c = 0; c < 2; ++c)
        if (!isFiniteF(v.extraTexcoords[u][c])) {
          if (outDiagnostic)
            *outDiagnostic = "Non-finite multitexture coordinate";
          return false;
        }
    for (size_t u = 0; u < COIN_RENDER_MAX_TEXTURE_UNITS; ++u) {
      if (!isFiniteF(v.textureR[u]) || !isFiniteF(v.textureQ[u])) {
        if (outDiagnostic)
          *outDiagnostic = "Non-finite homogeneous texture coordinate";
        return false;
      }
    }
    if (this->materials.empty() || v.materialSlot >= this->materials.size()) {
      if (outDiagnostic)
        *outDiagnostic = "Vertex references out-of-range material slot";
      return false;
    }
  }

  const size_t numIndices = this->indices.size();
  timer.mark("vertices");
  for (size_t i = 0; i < numIndices; ++i) {
    if (this->indices[i] >= numVertices) {
      if (outDiagnostic)
        *outDiagnostic = "Index out of range of vertex buffer";
      return false;
    }
  }

  for (size_t i = 0; i < this->cameras.size(); ++i) {
    if (i == 0)
      timer.mark("indices");
    const CoinRenderCameraSnapshot &c = this->cameras[i];
    if (!isMatrixFinite(c.viewMatrix) || !isMatrixFinite(c.projectionMatrixCoin)) {
      if (outDiagnostic)
        *outDiagnostic = "Camera matrix contains non-finite values";
      return false;
    }
  }

  for (size_t i = 0; i < this->textures.size(); ++i) {
    const CoinRenderTextureImageSnapshot &tex = this->textures[i];
    if (tex.sceneTransparencyFunction != -1 &&
        !coin_render_scene_texture_policy_supported(tex.sceneTransparencyFunction)) {
      if (outDiagnostic)
        *outDiagnostic = "Unsupported scene texture transparency function";
      return false;
    }
    if (!CoinRenderTextureSamplingCore::validMipImage(tex)) {
      if (outDiagnostic)
        *outDiagnostic = "Invalid P07 mipmap chain or image limits";
      return false;
    }
    if (tex.width == 0 || tex.height == 0) {
      if (outDiagnostic)
        *outDiagnostic = "Texture contains zero width or height";
      return false;
    }
    if (tex.width > 8192 || tex.height > 8192) {
      if (outDiagnostic)
        *outDiagnostic = "Texture dimensions exceed 8192 limit";
      return false;
    }
    const uint64_t expectedBytes =
        CoinRenderTextureFormatCore::levelBytes(tex.width, tex.height, tex.format);
    if (tex.producerId && tex.gpuToken) {
      if (outDiagnostic)
        *outDiagnostic = "Texture cannot be both planned and resolved";
      return false;
    }
    if (tex.producerId == 0 && tex.gpuToken == 0 && tex.pixelsRgba.size() != expectedBytes) {
      if (outDiagnostic)
        *outDiagnostic = "Texture pixel buffer size mismatch";
      return false;
    }
    if ((tex.producerId != 0 || tex.gpuToken != 0) && !tex.pixelsRgba.empty()) {
      if (outDiagnostic)
        *outDiagnostic = "GPU texture must not carry CPU pixels";
      return false;
    }
  }

  for (const auto &sampler : this->samplers) {
    if (static_cast<uint32_t>(sampler.filter) > 3 || static_cast<uint32_t>(sampler.wrapS) > 1 ||
        static_cast<uint32_t>(sampler.wrapT) > 1 ||
        static_cast<uint32_t>(sampler.wrapR) > 1 ||
        !CoinRenderTextureSamplingCore::powerOfTwo(sampler.maxAnisotropy) ||
        sampler.maxAnisotropy > 16 ||
        (sampler.maxAnisotropy > 1 &&
         sampler.filter != CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR)) {
      if (outDiagnostic)
        *outDiagnostic = "Invalid P07 sampler filter or wrap";
      return false;
    }
  }
  for (size_t i = 0; i < this->lightingStates.size(); ++i) {
    const CoinRenderLightingSnapshot &ls = this->lightingStates[i];
    if (ls.lights.size() > COIN_RENDER_MAX_LIGHTS) {
      if (outDiagnostic)
        *outDiagnostic = "More than eight active lights";
      return false;
    }
    if (!isFiniteF(ls.ambientIntensity) || ls.ambientIntensity < 0.0f) {
      if (outDiagnostic)
        *outDiagnostic = "Invalid ambient intensity";
      return false;
    }
    for (int c = 0; c < 3; ++c) {
      if (!isFiniteF(ls.ambientColor[c])) {
        if (outDiagnostic)
          *outDiagnostic = "Invalid ambient color";
        return false;
      }
    }
    for (size_t j = 0; j < ls.lights.size(); ++j) {
      const CoinRenderLightSourceSnapshot &light = ls.lights[j];
      if (light.type != CoinRenderLightType::DIRECTIONAL &&
          light.type != CoinRenderLightType::POINT && light.type != CoinRenderLightType::SPOT) {
        if (outDiagnostic)
          *outDiagnostic = "Unsupported light type";
        return false;
      }
      if (!isFiniteF(light.intensity) || light.intensity < 0.0f || !isFiniteF(light.cutOffAngle) ||
          !isFiniteF(light.dropOffRate)) {
        if (outDiagnostic)
          *outDiagnostic = "Invalid light intensity or cone";
        return false;
      }
      bool anyAttenuation = false;
      for (int c = 0; c < 3; ++c) {
        if (!isFiniteF(light.color[c]) || !isFiniteF(light.direction[c]) ||
            !isFiniteF(light.position[c]) || !isFiniteF(light.attenuation[c]) ||
            light.attenuation[c] < 0.0f) {
          if (outDiagnostic)
            *outDiagnostic = "Invalid light vector, color or attenuation";
          return false;
        }
        anyAttenuation = anyAttenuation || light.attenuation[c] > 0.0f;
      }
      if (light.type != CoinRenderLightType::DIRECTIONAL && !anyAttenuation) {
        if (outDiagnostic)
          *outDiagnostic = "Degenerate positional light attenuation";
        return false;
      }
      if (light.type != CoinRenderLightType::POINT) {
        const float dirLengthSq = light.direction[0] * light.direction[0] +
                                  light.direction[1] * light.direction[1] +
                                  light.direction[2] * light.direction[2];
        if (dirLengthSq <= 1.0e-12f) {
          if (outDiagnostic)
            *outDiagnostic = "Zero light direction";
          return false;
        }
      }
      if (light.type == CoinRenderLightType::SPOT &&
          (light.cutOffAngle < 0.0f || light.cutOffAngle > 1.570796327f ||
           light.dropOffRate < 0.0f || light.dropOffRate > 1.0f)) {
        if (outDiagnostic)
          *outDiagnostic = "Spot cone outside supported range";
        return false;
      }
    }
  }

  for (size_t g = 0; g < this->shadowGroups.size(); ++g) {
    if (this->shadowGroups[g].parentGroupSlot > g) {
      if (outDiagnostic)
        *outDiagnostic = "Shadow group parent must precede its child";
      return false;
    }
  }

  for (const auto &light : this->shadowLights) {
    if (light.groupSlot == 0 || light.groupSlot > this->shadowGroups.size()) {
      if (outDiagnostic)
        *outDiagnostic = "Shadow light references an invalid group";
      return false;
    }
    if (!isFiniteF(light.intensity) || light.intensity < 0.0f || !isFiniteF(light.cutOffAngle) ||
        !isFiniteF(light.dropOffRate)) {
      if (outDiagnostic)
        *outDiagnostic = "Invalid captured shadow light intensity or cone";
      return false;
    }
    bool anyAttenuation = false;
    for (int c = 0; c < 3; ++c) {
      if (!isFiniteF(light.color[c]) || !isFiniteF(light.attenuation[c]) ||
          light.attenuation[c] < 0.0f) {
        if (outDiagnostic)
          *outDiagnostic = "Invalid captured shadow light color or attenuation";
        return false;
      }
      anyAttenuation = anyAttenuation || light.attenuation[c] > 0.0f;
    }
    if (light.type == CoinRenderLightType::SPOT && light.enabled && !anyAttenuation) {
      if (outDiagnostic)
        *outDiagnostic = "Degenerate captured spot attenuation";
      return false;
    }
  }
  for (const auto &instance : this->portableShaders) {
    std::string error;
    if (!coin_render_shader_instance_valid(instance, error)) {
      if (outDiagnostic) *outDiagnostic = error;
      return false;
    }
  }
  for (size_t i = 0; i < this->renderStates.size(); ++i) {
    if (i == 0)
      timer.mark("scene_state");
    const CoinRenderRenderStateSnapshot &state = this->renderStates[i];
    if (state.portableShaderSlot > this->portableShaders.size()) {
      if (outDiagnostic) *outDiagnostic = "Portable shader occurrence slot is invalid";
      return false;
    }
    if (state.shadowMapSize &&
        (state.shadowMapSize > 2048 || (state.shadowMapSize & (state.shadowMapSize - 1)) ||
         !state.shadowGroupSlot)) {
      if (outDiagnostic)
        *outDiagnostic = "Custom shadow-map viewport has an invalid target";
      return false;
    }
    SbMatrix inverseMapView, inverseMapProjection;
    if (state.shadowMapCameraOverride &&
        (!state.shadowGroupSlot ||
         !coin_render_shadow_camera_math::inverse(state.view, inverseMapView) ||
         !coin_render_shadow_camera_math::inverse(state.projectionCoin, inverseMapProjection))) {
      if (outDiagnostic) *outDiagnostic = "Authored shadow-map camera is invalid";
      return false;
    }
    if (state.rasterTextureSourceSlot != UINT32_MAX) {
      if (!state.rasterPixels || state.rasterTextureSourceSlot >= this->renderStates.size() ||
          this->renderStates[state.rasterTextureSourceSlot].rasterPixels ||
          this->renderStates[state.rasterTextureSourceSlot].rasterTextureSourceSlot != UINT32_MAX) {
        if (outDiagnostic) *outDiagnostic = "Invalid raster inherited texture source";
        return false;
      }
      const auto & source = this->renderStates[state.rasterTextureSourceSlot];
      for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
        const auto layer = coin_render_texture_unit(source, unit);
        const auto & raw = state.rasterTextureCoordinates[unit];
        if (state.rasterTextureProvenance[unit] > 1) {
          if (outDiagnostic) *outDiagnostic = "Invalid raster texture provenance";
          return false;
        }
        for (int c = 0; c < 4; ++c) if (!isFiniteF(raw[c])) {
          if (outDiagnostic) *outDiagnostic = "Non-finite raster texture coordinate";
          return false;
        }
        if (!layer.enabled) continue;
        SbVec4f transformed;
        layer.matrix.multVecMatrix(raw, transformed);
        for (int c = 0; c < 4; ++c) if (!isFiniteF(transformed[c])) {
          if (outDiagnostic) *outDiagnostic = "Raster texture matrix exceeds finite precision";
          return false;
        }
        const bool cube = layer.imageSlot < this->textures.size() &&
                          this->textures[layer.imageSlot].cubeFaceSize != 0;
        if (cube && transformed[0] == 0.0f && transformed[1] == 0.0f && transformed[2] == 0.0f) {
          if (outDiagnostic) *outDiagnostic = "Raster cube texture direction is zero";
          return false;
        }
        if (!cube && source.textureProjection == CoinRenderTextureProjection::PROJECTIVE) {
          if (transformed[3] == 0 || !isFiniteF(transformed[0] / transformed[3]) ||
              !isFiniteF(transformed[1] / transformed[3]) || !isFiniteF(transformed[2] / transformed[3])) {
            if (outDiagnostic) *outDiagnostic = "Invalid raster projective texture coordinate";
            return false;
          }
        }
      }
    }
    if (state.rasterShadowImagePhong &&
        (!state.rasterPixels || !state.rasterShadowAnchorValid)) {
      if (outDiagnostic) *outDiagnostic = "Raster PHONG image requires an anchored image state";
      return false;
    }
    if (state.rasterShadowAnchorValid &&
        (!state.rasterPixels || !isFiniteF(state.rasterShadowAnchorWorld[0]) ||
         !isFiniteF(state.rasterShadowAnchorWorld[1]) ||
         !isFiniteF(state.rasterShadowAnchorWorld[2]))) {
      if (outDiagnostic) *outDiagnostic = "Raster shadow anchor is invalid";
      return false;
    }
    if (state.colorWriteMask & ~uint8_t(0x0f)) {
      if (outDiagnostic) *outDiagnostic = "RenderState has invalid color write mask";
      return false;
    }
    if (state.shadowGroupSlot > this->shadowGroups.size() || state.shadowStyle > 3u) {
      if (outDiagnostic)
        *outDiagnostic = "RenderState has invalid shadow group or style";
      return false;
    }
    if (state.flatShadowPrimary &&
        (!state.shadowGroupSlot || !(state.shadowStyle & 2u) ||
         state.lightModel != CoinRenderLightModel::PHONG ||
         state.materialSlot >= this->materials.size())) {
      if (outDiagnostic)
        *outDiagnostic = "Flat shadow primary needs a PHONG receiver and valid fragment material";
      return false;
    }
    float clipEquations[COIN_RENDER_MAX_CLIP_PLANES][4] = {};
    std::string clipDiagnostic;
    if (!coin_render_clip_equations(state, clipEquations, clipDiagnostic)) {
      if (outDiagnostic)
        *outDiagnostic = clipDiagnostic;
      return false;
    }
    if (state.viewportSlot >= this->viewports.size()) {
      if (outDiagnostic)
        *outDiagnostic = "RenderState references out-of-bounds viewport slot";
      return false;
    }
    if (state.depthFunction != CoinRenderDepthFunction::NEVER &&
        state.depthFunction != CoinRenderDepthFunction::ALWAYS &&
        state.depthFunction != CoinRenderDepthFunction::LESS &&
        state.depthFunction != CoinRenderDepthFunction::LEQUAL &&
        state.depthFunction != CoinRenderDepthFunction::EQUAL &&
        state.depthFunction != CoinRenderDepthFunction::GEQUAL &&
        state.depthFunction != CoinRenderDepthFunction::GREATER &&
        state.depthFunction != CoinRenderDepthFunction::NOTEQUAL) {
      if (outDiagnostic)
        *outDiagnostic = "Unsupported depth comparison function";
      return false;
    }
    if (!coin_render_alpha_test_valid(state.alphaTestFunction, state.alphaTestReference)) {
      if (outDiagnostic)
        *outDiagnostic = "Invalid alpha comparison function or reference";
      return false;
    }
    if (state.textureProjection != CoinRenderTextureProjection::PROJECTIVE &&
        state.textureProjection != CoinRenderTextureProjection::DIRECT_ST) {
      if (outDiagnostic)
        *outDiagnostic = "Invalid texture coordinate projection policy";
      return false;
    }
    if (!isFiniteF(state.polygonOffsetFactor) || !isFiniteF(state.polygonOffsetUnits) ||
        !isFiniteF(state.polygonOffsetSlopeBias) || !isFiniteF(state.polygonOffsetMaxDepth) ||
        (state.polygonOffsetMaxDepth != -1.0f &&
         (state.polygonOffsetMaxDepth < 0.0f || state.polygonOffsetMaxDepth > 1.0f)) ||
        (state.polygonOffsetStyles & ~7u) != 0 ||
        (state.polygonOffsetPrimitiveStyle != 1 && state.polygonOffsetPrimitiveStyle != 2 &&
         state.polygonOffsetPrimitiveStyle != 4)) {
      if (outDiagnostic)
        *outDiagnostic = "Invalid polygon offset";
      return false;
    }
    if (!isFiniteF(state.depthRange[0]) || !isFiniteF(state.depthRange[1]) ||
        state.depthRange[0] < 0.0f || state.depthRange[0] > 1.0f || state.depthRange[1] < 0.0f ||
        state.depthRange[1] > 1.0f) {
      if (outDiagnostic)
        *outDiagnostic = "Invalid depth range";
      return false;
    }
    const CoinRenderViewportSnapshot &viewport = this->viewports[state.viewportSlot];
    if (viewport.width <= 0 || viewport.height <= 0) {
      if (outDiagnostic)
        *outDiagnostic = "Viewport has invalid origin or extent";
      return false;
    }
    if (state.lineWidth <= 0.0f || state.pointSize <= 0.0f || state.linePatternScaleFactor < 1) {
      if (outDiagnostic)
        *outDiagnostic = "Invalid line or point style";
      return false;
    }
    if (state.fogMode != CoinRenderFogMode::NONE && state.fogMode != CoinRenderFogMode::HAZE &&
        state.fogMode != CoinRenderFogMode::FOG && state.fogMode != CoinRenderFogMode::SMOKE) {
      if (outDiagnostic)
        *outDiagnostic = "Unsupported fog mode";
      return false;
    }
    if ((state.fogMode != CoinRenderFogMode::NONE && state.fogEnd <= 0.0f) ||
        (state.fogMode == CoinRenderFogMode::HAZE && state.fogEnd <= state.fogStart)) {
      if (outDiagnostic)
        *outDiagnostic = "Invalid fog range";
      return false;
    }
    for (int c = 0; c < 3; ++c) {
      if (!isFiniteF(state.fogColor[c])) {
        if (outDiagnostic)
          *outDiagnostic = "Invalid fog color";
        return false;
      }
    }
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      if (!combineMemo.validate(unit, state.textureCombines[unit])) {
        if (outDiagnostic)
          *outDiagnostic = "Invalid texture combine program";
        return false;
      }
      if (!coin_render_texture_unit_enabled(state, unit))
        continue;
      const CoinRenderTextureUnitSnapshot tex = coin_render_texture_unit(state, unit);
      if (tex.model != CoinRenderTextureModel::MODULATE &&
          tex.model != CoinRenderTextureModel::REPLACE &&
          tex.model != CoinRenderTextureModel::DECAL &&
          tex.model != CoinRenderTextureModel::BLEND) {
        if (outDiagnostic)
          *outDiagnostic = "RenderState contains unsupported texture model";
        return false;
      }
      for (int c = 0; c < 4; ++c) {
        if (!isFiniteF(tex.blendColor[c])) {
          if (outDiagnostic)
            *outDiagnostic = "RenderState contains invalid texture blend color";
          return false;
        }
      }

      if (!isMatrixFinite(tex.matrix)) {
        if (outDiagnostic)
          *outDiagnostic = "RenderState contains non-finite texture matrix";
        return false;
      }
      if (tex.imageSlot >= this->textures.size()) {
        if (outDiagnostic)
          *outDiagnostic = "RenderState references out-of-bounds texture image slot";
        return false;
      }
      if (tex.samplerSlot >= this->samplers.size()) {
        if (outDiagnostic)
          *outDiagnostic = "RenderState references out-of-bounds sampler slot";
        return false;
      }
      if (CoinRenderTextureSamplingCore::mipFilter(this->samplers[tex.samplerSlot].filter) &&
          !this->textures[tex.imageSlot].mipmapped &&
          !(this->textures[tex.imageSlot].volumeDepth &&
            this->textures[tex.imageSlot].volumeMipmapped) &&
          !(this->textures[tex.imageSlot].cubeFaceSize && this->textures[tex.imageSlot].cubeMipmapped)) {
        if (outDiagnostic)
          *outDiagnostic = "P07 mip sampler requires a complete stored-image chain";
        return false;
      }
    }
  }

  std::string layerDiagnostic;
  if (!coin_render_scene_layers_valid(*this, layerDiagnostic)) {
    if (outDiagnostic) *outDiagnostic = layerDiagnostic;
    return false;
  }
  for (size_t i = 0; i < this->draws.size(); ++i) {
    if (i == 0)
      timer.mark("render_states");
    const CoinRenderDrawPacket &draw = this->draws[i];
    if ((draw.multipassMask >> numPasses) != 0 ||
        (draw.multipassRasterFixed && (draw.renderStateSlot >= renderStates.size() ||
          !renderStates[draw.renderStateSlot].rasterPixels))) {
      if (outDiagnostic) *outDiagnostic = "Invalid multipass draw mask or raster identity";
      return false;
    }

    if (draw.hasSortingCenter &&
        (!isFiniteF(draw.sortingCenterWorld[0]) || !isFiniteF(draw.sortingCenterWorld[1]) ||
         !isFiniteF(draw.sortingCenterWorld[2]))) {
      if (outDiagnostic)
        *outDiagnostic = "Non-finite object sorting center";
      return false;
    }
    if (draw.clearDepthBefore && draw.renderLayer == 0) {
      if (outDiagnostic)
        *outDiagnostic = "Base layer cannot clear depth before a draw";
      return false;
    }
    if (draw.renderStateSlot >= this->renderStates.size()) {
      if (outDiagnostic)
        *outDiagnostic = "Draw references invalid renderStateSlot";
      return false;
    }
    if (draw.rasterShadowCaster &&
        (!draw.shadowLightSlot || this->renderStates[draw.renderStateSlot].rasterPixels)) {
      if (outDiagnostic) *outDiagnostic = "Raster shadow caster needs an owned map-only draw";
      return false;
    }
    if (draw.shadowLightSlot && (draw.shadowLightSlot > this->shadowLights.size() ||
                                 (!draw.rasterShadowCaster &&
                                  !this->shadowLights[draw.shadowLightSlot - 1].mapSceneCaptured) ||
                                 this->renderStates[draw.renderStateSlot].shadowGroupSlot !=
                                     this->shadowLights[draw.shadowLightSlot - 1].groupSlot)) {
      if (outDiagnostic)
        *outDiagnostic = "Shadow-only draw references an invalid light owner";
      return false;
    }
    const CoinRenderGeometryRange &geometry = draw.geometry;
    if (geometry.firstVertex > numVertices ||
        geometry.vertexCount > (numVertices - geometry.firstVertex)) {
      if (outDiagnostic)
        *outDiagnostic = "Draw vertex range out of bounds";
      return false;
    }
    if (geometry.firstIndex > numIndices ||
        geometry.indexCount > (numIndices - geometry.firstIndex)) {
      if (outDiagnostic)
        *outDiagnostic = "Draw index range out of bounds";
      return false;
    }
    if (draw.topology == CoinRenderPrimitiveTopology::TRIANGLE_LIST &&
        (geometry.indexCount % 3 != 0)) {
      if (outDiagnostic)
        *outDiagnostic = "Triangle list index count is not multiple of 3";
      return false;
    }
    if (draw.topology == CoinRenderPrimitiveTopology::LINE_LIST && (geometry.indexCount % 2 != 0)) {
      if (outDiagnostic)
        *outDiagnostic = "Line list index count is not multiple of 2";
      return false;
    }

    const CoinRenderRenderStateSnapshot &state = this->renderStates[draw.renderStateSlot];
    if (state.portableShaderSlot && geometry.indexCount) {
      if (draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST || state.rasterPixels) {
        if (outDiagnostic) *outDiagnostic = "Portable shader modules require triangular surfaces";
        return false;
      }
      const auto &instance = this->portableShaders[state.portableShaderSlot - 1];
      CoinRenderShaderRange primary;
      primary.lower = {{0,0,0,0}};
      primary.upper = {{1,1,1,1}};
      std::array<CoinRenderShaderRange,8> coordinates, samples;
      for (unsigned unit = 0; unit < 8; ++unit) {
        coordinates[unit].lower.fill(std::numeric_limits<double>::infinity());
        coordinates[unit].upper.fill(-std::numeric_limits<double>::infinity());
        samples[unit].upper = {{1,1,1,1}};
        if (instance.ir.textureMask & (1u << unit)) {
          const auto layer = coin_render_texture_unit(state, unit);
          if (!layer.enabled || layer.imageSlot >= this->textures.size() ||
              this->textures[layer.imageSlot].volumeDepth || this->textures[layer.imageSlot].cubeFaceSize) {
            if (outDiagnostic) *outDiagnostic = "Portable shader SAMPLE_2D requires an existing 2D resource";
            return false;
          }
          if (this->textures[layer.imageSlot].format == CoinRenderTextureFormat::RGBA16_FLOAT) {
            samples[unit].lower.fill(-65504.0);
            samples[unit].upper.fill(65504.0);
          }
        }
      }
      if (state.lightModel == CoinRenderLightModel::BASE_COLOR) {
        primary.lower.fill(std::numeric_limits<double>::infinity());
        primary.upper.fill(-std::numeric_limits<double>::infinity());
      }
      for (uint32_t index = 0; index < geometry.indexCount; ++index) {
        const auto &vertex = this->vertices[this->indices[geometry.firstIndex + index]];
        if (vertex.materialSlot >= this->materials.size()) {
          if (outDiagnostic) *outDiagnostic = "Portable shader primary material slot is invalid";
          return false;
        }
        if (state.lightModel == CoinRenderLightModel::BASE_COLOR) {
          const auto &material = this->materials[vertex.materialSlot];
          for (unsigned c = 0; c < 4; ++c) {
            const double value = c == 3 ? 1.0 - material.transparency : material.diffuse[c];
            primary.lower[c] = std::min(primary.lower[c], value);
            primary.upper[c] = std::max(primary.upper[c], value);
          }
        }
        for (unsigned unit = 0; unit < 8; ++unit) {
          const auto layer = coin_render_texture_unit(state, unit);
          const float *st = unit ? vertex.extraTexcoords[unit - 1] : vertex.texcoord;
          SbVec4f transformed;
          layer.matrix.multVecMatrix(SbVec4f(st[0], st[1], vertex.textureR[unit], vertex.textureQ[unit]), transformed);
          if (instance.ir.coordinateMask & (1u << unit))
            for (unsigned c = 0; c < 4; ++c) if (!isFiniteF(transformed[c])) {
              if (outDiagnostic) *outDiagnostic = "Portable shader TEXCOORD transform is nonfinite";
              return false;
            }
          for (unsigned c = 0; c < 4; ++c) {
            coordinates[unit].lower[c] = std::min(coordinates[unit].lower[c], double(transformed[c]));
            coordinates[unit].upper[c] = std::max(coordinates[unit].upper[c], double(transformed[c]));
          }
        }
      }
      std::string error;
      if (!coin_render_shader_numeric(instance, primary, coordinates, samples, error,
          state.textureProjection == CoinRenderTextureProjection::PROJECTIVE)) {
        if (outDiagnostic) *outDiagnostic = error;
        return false;
      }
    }
    if (!isMatrixFinite(state.model) || !isMatrixFinite(state.view) ||
        !isMatrixFinite(state.projectionCoin)) {
      if (outDiagnostic)
        *outDiagnostic = "RenderState matrix contains non-finite values";
      return false;
    }
    if (this->materials.empty() || state.materialSlot >= this->materials.size()) {
      if (outDiagnostic)
        *outDiagnostic = "RenderState materialSlot out of range";
      return false;
    }
    if (this->lightingStates.empty() || state.lightingSlot >= this->lightingStates.size()) {
      if (outDiagnostic)
        *outDiagnostic = "RenderState lightingSlot out of range";
      return false;
    }
    if (this->cameras.empty() || state.cameraSlot >= this->cameras.size()) {
      if (outDiagnostic)
        *outDiagnostic = "RenderState cameraSlot out of range";
      return false;
    }
    if (this->viewports.empty() || state.viewportSlot >= this->viewports.size()) {
      if (outDiagnostic)
        *outDiagnostic = "RenderState viewportSlot out of range";
      return false;
    }
    if (!this->textures.empty()) {
      const size_t primitiveSize = draw.topology == CoinRenderPrimitiveTopology::TRIANGLE_LIST ? 3
                                   : draw.topology == CoinRenderPrimitiveTopology::LINE_LIST   ? 2
                                                                                               : 1;
      std::string textureDiagnostic;
      for (size_t index = 0; index + primitiveSize <= geometry.indexCount; index += primitiveSize) {
        if (!coin_render_validate_texture_primitive(
                *this, state, this->indices.data() + geometry.firstIndex + index, primitiveSize,
                textureDiagnostic)) {
          if (outDiagnostic)
            *outDiagnostic = textureDiagnostic;
          return false;
        }
      }
    }
  }

  for (const auto & occurrence : this->rasterOccurrences) {
    if (occurrence.kind > CoinRenderRasterOccurrence::MARKER ||
        occurrence.sourceStateSlot >= this->renderStates.size() ||
        occurrence.shadowLightSlot > this->shadowLights.size() ||
        !isFiniteF(occurrence.anchorWorld[0]) ||
        !isFiniteF(occurrence.anchorWorld[1]) ||
        !isFiniteF(occurrence.anchorWorld[2]) ||
        occurrence.originalWidth <= 0 || occurrence.originalHeight <= 0 ||
        !isFiniteF(occurrence.offsetX) || !isFiniteF(occurrence.offsetY)) {
      if (outDiagnostic) *outDiagnostic = "Raster occurrence has invalid source state or anchor";
      return false;
    }
    for (const auto & run : occurrence.runs)
      if (!isFiniteF(run.x) || !isFiniteF(run.y) || !isFiniteF(run.width) ||
          run.x < 0.0f || run.y < 0.0f || run.width <= 0.0f ||
          double(run.x) + double(run.width) > double(occurrence.originalWidth) ||
          double(run.y) >= double(occurrence.originalHeight)) {
        if (outDiagnostic) *outDiagnostic = "Raster occurrence has an invalid coverage run";
        return false;
      }
  }
  timer.mark("draws");
  return true;
}

bool
coin_render_image_has_transparency(const unsigned char *pixels, size_t count, int components)
{
  if (!pixels || (components != 2 && components != 4))
    return false;
  for (size_t i = 0; i < count; ++i)
    if (pixels[i * components + components - 1] != 255)
      return true;
  return false;
}

bool
coin_render_texture_unit_enabled(const CoinRenderRenderStateSnapshot &state, size_t unit)
{
  return unit == 0 ? state.hasTexture : state.extraTextures[unit - 1].enabled;
}

CoinRenderTextureUnitSnapshot
coin_render_texture_unit(const CoinRenderRenderStateSnapshot &state, size_t unit)
{
  if (unit != 0)
    return state.extraTextures[unit - 1];
  CoinRenderTextureUnitSnapshot result;
  result.enabled = state.hasTexture;
  result.imageSlot = state.textureImageSlot;
  result.samplerSlot = state.samplerSlot;
  result.model = state.textureModel;
  result.matrix = state.textureMatrix;
  for (int c = 0; c < 4; ++c)
    result.blendColor[c] = state.textureBlendColor[c];
  return result;
}

// An unused registration/table entry does not require an executor.
bool CoinRenderFramePlan::hasPortableShaderDraws() const
{
  for (const auto &draw : this->draws)
    if (draw.geometry.indexCount && draw.renderStateSlot < this->renderStates.size() &&
        this->renderStates[draw.renderStateSlot].portableShaderSlot) return true;
  return false;
}
