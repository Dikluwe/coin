#include <iostream>
#include "rendering/wgpu/SoWgpuFramePlanBuilder.h"
#include "rendering/wgpu/SoWgpuComposition.h"
#include "rendering/wgpu/SoWgpuIndexedGeometryCore.h"

#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoLight.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoSpotLight.h>
#include <Inventor/elements/SoLightElement.h>
#include <Inventor/elements/SoEnvironmentElement.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbColor.h>
#include <Inventor/misc/SoState.h>
#include <Inventor/elements/SoShapeHintsElement.h>
#include <Inventor/elements/SoDepthBufferElement.h>
#include <Inventor/elements/SoPolygonOffsetElement.h>
#include <Inventor/elements/SoLinePatternElement.h>
#include <Inventor/elements/SoCreaseAngleElement.h>
#include <Inventor/elements/SoLightModelElement.h>
#include <Inventor/elements/SoShapeStyleElement.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoMultiTextureImageElement.h>
#include <Inventor/elements/SoMultiTextureMatrixElement.h>
#include <Inventor/elements/SoTextureQualityElement.h>
#include <Inventor/elements/SoTextureUnitElement.h>
#include <Inventor/elements/SoMultiTextureCoordinateElement.h>
#include <Inventor/elements/SoTextureCoordinateBindingElement.h>

#include <cassert>
#include <atomic>
#include <cmath>

SoWgpuFramePlanBuilder::SoWgpuFramePlanBuilder()
  : currentDrawIndex(0),
    nodeCounter(0),
    inFrame(false),
    hasActiveDraw(false),
    hasError(false),
    isUnsupported(false),
    annotationDepth(0),
    currentAnnotationLayer(0),
    nextAnnotationLayer(1),
    annotationDepthClearPending(false)
{
}

SoWgpuFramePlanBuilder::~SoWgpuFramePlanBuilder()
{
}

void
SoWgpuFramePlanBuilder::beginFrame(const SbColor4f & clearColor, const SbViewportRegion & /*viewport*/)
{
  this->reset();
  this->currentPlan.clearColor = clearColor;
  this->inFrame = true;
}

void
SoWgpuFramePlanBuilder::reset()
{
  this->currentPlan.vertices.clear();
  this->currentPlan.indices.clear();
  this->currentPlan.materials.clear();
  this->currentPlan.lightingStates.clear();
  this->currentPlan.cameras.clear();
  this->currentPlan.viewports.clear();
  this->currentPlan.renderStates.clear();
  this->currentPlan.textures.clear();
  this->currentPlan.samplers.clear();
  this->currentPlan.draws.clear();
  this->currentDrawIndex = 0;
  this->nodeCounter = 0;
  this->inFrame = false;
  this->hasActiveDraw = false;
  this->hasError = false;
  this->isUnsupported = false;
  this->savedAnnotationLayer = 0;
  this->savedAnnotationClear = false;
  this->foregroundLayer = 0;
  this->annotationDepth = 0;
  this->currentAnnotationLayer = 0;
  this->nextAnnotationLayer = 1;
  this->annotationDepthClearPending = false;
  this->builderError.clear();
  this->directTextures.clear();
  this->nodeOccurrenceCount.clear();
  this->lightAttenuationByIndex.clear();
}

void
SoWgpuFramePlanBuilder::beginAnnotation()
{
  if (this->annotationDepth++ == 0) {
    this->savedAnnotationLayer = this->currentAnnotationLayer;
    this->savedAnnotationClear = this->annotationDepthClearPending;
    this->currentAnnotationLayer = this->nextAnnotationLayer++;
    this->annotationDepthClearPending = true;
    this->hasActiveDraw = false;
  }
}

void
SoWgpuFramePlanBuilder::beginForeground()
{
  this->foregroundLayer = this->currentAnnotationLayer;
  this->currentAnnotationLayer = this->nextAnnotationLayer++;
  this->annotationDepthClearPending = false;
  this->hasActiveDraw = false;
}

void
SoWgpuFramePlanBuilder::endForeground()
{
  this->currentAnnotationLayer = this->foregroundLayer;
  this->annotationDepthClearPending = false;
  this->hasActiveDraw = false;
}

void
SoWgpuFramePlanBuilder::reserveDelayedLayers(uint32_t count)
{
  // Reserve one shared pass between scene and ordinary foreground annotations.
  for (DrawPacket & draw : this->currentPlan.draws)
    if (draw.renderLayer) draw.renderLayer += count;
  this->nextAnnotationLayer += count;
}

void
SoWgpuFramePlanBuilder::beginDelayedAnnotations(uint32_t layer, bool clearDepth)
{
  this->savedAnnotationLayer = 0;
  this->savedAnnotationClear = false;
  this->annotationDepth = 1;
  this->currentAnnotationLayer = layer;
  this->annotationDepthClearPending = clearDepth;
  this->hasActiveDraw = false;
}

void
SoWgpuFramePlanBuilder::endAnnotation()
{
  if (this->annotationDepth == 0) return;
  if (--this->annotationDepth == 0) {
    this->currentAnnotationLayer = this->savedAnnotationLayer;
    this->annotationDepthClearPending = this->savedAnnotationClear;
    this->hasActiveDraw = false;
  }
}

void
SoWgpuFramePlanBuilder::registerDirectTexture(const unsigned char * image,
                                               uint64_t token, uint32_t width, uint32_t height, bool opaque)
{
  this->directTextures[image] = DirectTexture{token, width, height, opaque};
}

void
SoWgpuFramePlanBuilder::recordLightAttenuation(SoCallbackAction * action)
{
  SoState * state = action ? action->getState() : nullptr;
  if (!state) return;
  const int index = SoLightElement::getLights(state).getLength();
  if (index < 0) return;
  const size_t slot = static_cast<size_t>(index);
  if (this->lightAttenuationByIndex.size() <= slot) {
    this->lightAttenuationByIndex.resize(slot + 1, SbVec3f(0.0f, 0.0f, 1.0f));
  }
  // The GL path fixes attenuation at the light node, before later SoEnvironment nodes.
  this->lightAttenuationByIndex[slot] = SoEnvironmentElement::getLightAttenuation(state);
}

uint32_t
SoWgpuFramePlanBuilder::captureMaterial(SoCallbackAction * action, int materialIndex)
{
  SbColor amb(0.2f, 0.2f, 0.2f), diff(0.8f, 0.8f, 0.8f), spec(0.0f, 0.0f, 0.0f), emiss(0.0f, 0.0f, 0.0f);
  float shin = 0.2f, transp = 0.0f;

  int safeIndex = (materialIndex >= 0) ? materialIndex : 0;
  SoState * state = action ? action->getState() : nullptr;
  if (state) {
    const SoLazyElement * lazy = SoLazyElement::getInstance(state);
    if (lazy) {
      int32_t numDiff = lazy->getNumDiffuse();
      if (numDiff > 0 && safeIndex >= numDiff) {
        safeIndex = numDiff - 1;
      }
    }
  }

  action->getMaterial(amb, diff, spec, emiss, shin, transp, safeIndex);

  MaterialSnapshot matSnap;
  matSnap.ambient[0] = amb[0]; matSnap.ambient[1] = amb[1]; matSnap.ambient[2] = amb[2]; matSnap.ambient[3] = 1.0f;
  matSnap.diffuse[0] = diff[0]; matSnap.diffuse[1] = diff[1]; matSnap.diffuse[2] = diff[2]; matSnap.diffuse[3] = 1.0f - transp;
  matSnap.specular[0] = spec[0]; matSnap.specular[1] = spec[1]; matSnap.specular[2] = spec[2]; matSnap.specular[3] = 1.0f;
  matSnap.emission[0] = emiss[0]; matSnap.emission[1] = emiss[1]; matSnap.emission[2] = emiss[2]; matSnap.emission[3] = 1.0f;
  matSnap.shininess = shin;
  matSnap.transparency = transp;

  for (size_t i = 0; i < this->currentPlan.materials.size(); ++i) {
    const auto & m = this->currentPlan.materials[i];
    if (std::memcmp(&m, &matSnap, sizeof(MaterialSnapshot)) == 0) {
      return static_cast<uint32_t>(i);
    }
  }
  uint32_t materialSlot = static_cast<uint32_t>(this->currentPlan.materials.size());
  this->currentPlan.materials.push_back(matSnap);
  return materialSlot;
}

static uint64_t computeFnv1a64(const uint8_t * data, size_t len)
{
  uint64_t hash = 14695981039346656037ULL;
  for (size_t i = 0; i < len; ++i) {
    hash ^= static_cast<uint64_t>(data[i]);
    hash *= 1099511628211ULL;
  }
  return hash;
}

bool
SoWgpuFramePlanBuilder::captureTexture(SoCallbackAction * action, RenderStateSnapshot & rs, std::string * outError)
{
  SoState * state = action ? action->getState() : nullptr;
  if (!state) {
    rs.hasTexture = false;
    rs.textureImageSlot = 0;
    rs.samplerSlot = 0;
    return true;
  }

  // 1. Verify if any texture units > 0 have images enabled
  for (int u = 1; u < 16; ++u) {
    SbVec2s sz;
    int nc = 0;
    SoMultiTextureImageElement::Wrap ws, wt;
    SoMultiTextureImageElement::Model mod;
    SbColor blendCol;
    const unsigned char * p = SoMultiTextureImageElement::get(state, u, sz, nc, ws, wt, mod, blendCol);
    if (p != nullptr && sz[0] > 0 && sz[1] > 0 && nc > 0) {
      if (outError) *outError = "Texture units > 0 are not supported in Subwave 3B";
      this->isUnsupported = true;
      this->builderError = (outError ? *outError : "Texture units > 0 are not supported in Subwave 3B");
      return false;
    }
  }

  // 2. Fetch unit 0 image
  SbVec2s imgSize;
  int numComponents = 0;
  SoMultiTextureImageElement::Wrap wrapS;
  SoMultiTextureImageElement::Wrap wrapT;
  SoMultiTextureImageElement::Model model;
  SbColor blendColor;
  const unsigned char * rawBytes = SoMultiTextureImageElement::get(state, 0, imgSize, numComponents, wrapS, wrapT, model, blendColor);

  if (!rawBytes || imgSize[0] <= 0 || imgSize[1] <= 0 || numComponents <= 0) {
    rs.hasTexture = false;
    rs.textureImageSlot = 0;
    rs.samplerSlot = 0;
    return true;
  }

  // Check for procedural/DEFAULT texture coordinates
  const SoMultiTextureCoordinateElement * tcElem = SoMultiTextureCoordinateElement::getInstance(state);
  if (tcElem) {
    auto ct = tcElem->getType(0);
    if (ct == SoMultiTextureCoordinateElement::DEFAULT || ct == SoMultiTextureCoordinateElement::FUNCTION) {
      if (outError) *outError = "Procedural/DEFAULT texture coordinates are not supported in Subwave 3B";
      this->isUnsupported = true;
      this->builderError = (outError ? *outError : "Procedural/DEFAULT texture coordinates are not supported in Subwave 3B");
      return false;
    }
  }

  // 3. Texture quality
  float quality = SoTextureQualityElement::get(state);
  if (quality <= 0.0f) {
    // Texture disabled by quality
    rs.hasTexture = false;
    rs.textureImageSlot = 0;
    rs.samplerSlot = 0;
    return true;
  }
  if (std::abs(quality - 0.5f) > 0.05f) {
    if (outError) *outError = "Unsupported texture quality, only 0.0 (off) and 0.5 (linear) are supported in Subwave 3B";
    this->isUnsupported = true;
    this->builderError = (outError ? *outError : "Unsupported texture quality in Subwave 3B");
    return false;
  }

  // 4. Check for Coin dummy texture injected for missing/pending filenames (2x2, 1 component, all 0xff)
  if (imgSize[0] == 2 && imgSize[1] == 2 && numComponents == 1 &&
      rawBytes[0] == 0xff && rawBytes[1] == 0xff && rawBytes[2] == 0xff && rawBytes[3] == 0xff) {
    if (outError) *outError = "Pending or missing texture file detected (dummy texture rejected in Subwave 3B)";
    this->isUnsupported = true;
    this->builderError = (outError ? *outError : "Pending or missing texture file detected");
    return false;
  }

  // 5. Wrap modes (switch on Coin enum values)
  TextureWrap snapWrapS = TextureWrap::REPEAT;
  TextureWrap snapWrapT = TextureWrap::REPEAT;
  switch (wrapS) {
    case SoMultiTextureImageElement::REPEAT:
      snapWrapS = TextureWrap::REPEAT;
      break;
    case SoMultiTextureImageElement::CLAMP:
      snapWrapS = TextureWrap::CLAMP;
      break;
    default:
      if (outError) *outError = "Unsupported wrapS mode (only REPEAT and CLAMP supported in Subwave 3B)";
      this->isUnsupported = true;
      this->builderError = (outError ? *outError : "Unsupported wrapS mode");
      return false;
  }

  switch (wrapT) {
    case SoMultiTextureImageElement::REPEAT:
      snapWrapT = TextureWrap::REPEAT;
      break;
    case SoMultiTextureImageElement::CLAMP:
      snapWrapT = TextureWrap::CLAMP;
      break;
    default:
      if (outError) *outError = "Unsupported wrapT mode (only REPEAT and CLAMP supported in Subwave 3B)";
      this->isUnsupported = true;
      this->builderError = (outError ? *outError : "Unsupported wrapT mode");
      return false;
  }

  // 6. Texture model (switch on Coin enum values)
  switch (model) {
    case SoMultiTextureImageElement::MODULATE:
      rs.textureModel = TextureModel::MODULATE;
      break;
    case SoMultiTextureImageElement::REPLACE:
      rs.textureModel = TextureModel::REPLACE;
      break;
    case SoMultiTextureImageElement::DECAL:
      rs.textureModel = TextureModel::DECAL;
      break;
    case SoMultiTextureImageElement::BLEND:
      rs.textureModel = TextureModel::BLEND;
      break;
    default:
      if (outError) *outError = "Unsupported texture model";
      this->isUnsupported = true;
      this->builderError = (outError ? *outError : "Unsupported texture model");
      return false;
  }
  rs.textureBlendColor[0] = blendColor[0];
  rs.textureBlendColor[1] = blendColor[1];
  rs.textureBlendColor[2] = blendColor[2];
  rs.textureBlendColor[3] = 1.0f;

  // 7. Canonical RGBA8 conversion and strict opacity validation
  const auto direct = this->directTextures.find(rawBytes);
  bool isDirect = direct != this->directTextures.end() &&
                  imgSize[0] == 1 && imgSize[1] == 1 && numComponents == 4;
  if (isDirect) {
    for (unsigned int i = 0; i < 4; ++i) {
      if (rawBytes[i] != static_cast<uint8_t>(direct->second.token >> (i * 8))) {
        isDirect = false;
        break;
      }
    }
  }
  uint32_t w = isDirect ? direct->second.width : static_cast<uint32_t>(imgSize[0]);
  uint32_t h = isDirect ? direct->second.height : static_cast<uint32_t>(imgSize[1]);
  if (w > 8192 || h > 8192) {
    if (outError) *outError = "Texture dimensions exceed 8192";
    this->isUnsupported = true;
    this->builderError = (outError ? *outError : "Texture dimensions exceed 8192");
    return false;
  }

  std::vector<uint8_t> rgba;
  if (!isDirect) {
    size_t pixelCount = static_cast<size_t>(w) * static_cast<size_t>(h);
    rgba.resize(pixelCount * 4);

    if (numComponents == 1) {
      for (size_t i = 0; i < pixelCount; ++i) {
        uint8_t val = rawBytes[i];
        rgba[i * 4 + 0] = val;
        rgba[i * 4 + 1] = val;
        rgba[i * 4 + 2] = val;
        rgba[i * 4 + 3] = 255;
      }
    } else if (numComponents == 2) {
      for (size_t i = 0; i < pixelCount; ++i) {
        uint8_t val = rawBytes[i * 2 + 0];
        uint8_t alpha = rawBytes[i * 2 + 1];
        rgba[i * 4 + 0] = val;
        rgba[i * 4 + 1] = val;
        rgba[i * 4 + 2] = val;
        rgba[i * 4 + 3] = alpha;
      }
    } else if (numComponents == 3) {
      for (size_t i = 0; i < pixelCount; ++i) {
        rgba[i * 4 + 0] = rawBytes[i * 3 + 0];
        rgba[i * 4 + 1] = rawBytes[i * 3 + 1];
        rgba[i * 4 + 2] = rawBytes[i * 3 + 2];
        rgba[i * 4 + 3] = 255;
      }
    } else if (numComponents == 4) {
      for (size_t i = 0; i < pixelCount; ++i) {
        uint8_t alpha = rawBytes[i * 4 + 3];
        rgba[i * 4 + 0] = rawBytes[i * 4 + 0];
        rgba[i * 4 + 1] = rawBytes[i * 4 + 1];
        rgba[i * 4 + 2] = rawBytes[i * 4 + 2];
        rgba[i * 4 + 3] = alpha;
      }
    } else {
      if (outError)
        *outError = "Unsupported number of texture components";
      this->isUnsupported = true;
      this->builderError =
          (outError ? *outError : "Unsupported number of texture components");
      return false;
    }
  }

  // 8. Content digest & Image deduplication
  uint64_t digest = isDirect ? direct->second.token : computeFnv1a64(rgba.data(), rgba.size());
  uint32_t texSlot = UINT32_MAX;
  for (size_t i = 0; i < this->currentPlan.textures.size(); ++i) {
    const auto & t = this->currentPlan.textures[i];
    if (t.width == w && t.height == h && t.contentDigest == digest &&
        t.gpuToken == (isDirect ? direct->second.token : 0) && t.pixelsRgba == rgba) {
      texSlot = static_cast<uint32_t>(i);
      break;
    }
  }
  if (texSlot == UINT32_MAX) {
    texSlot = static_cast<uint32_t>(this->currentPlan.textures.size());
    TextureImageSnapshot tSnap;
    tSnap.width = w;
    tSnap.height = h;
    tSnap.components = 4;
    tSnap.gpuOpaque = isDirect && direct->second.opaque;
    tSnap.contentDigest = digest;
    tSnap.gpuToken = isDirect ? direct->second.token : 0;
    tSnap.pixelsRgba = std::move(rgba);
    this->currentPlan.textures.push_back(std::move(tSnap));
  }

  // 9. Sampler deduplication
  SamplerSnapshot sampSnap;
  sampSnap.wrapS = snapWrapS;
  sampSnap.wrapT = snapWrapT;
  sampSnap.filter = TextureFilter::LINEAR;

  uint32_t sampSlot = UINT32_MAX;
  for (size_t i = 0; i < this->currentPlan.samplers.size(); ++i) {
    const auto & s = this->currentPlan.samplers[i];
    if (s.wrapS == sampSnap.wrapS &&
        s.wrapT == sampSnap.wrapT &&
        s.filter == sampSnap.filter) {
      sampSlot = static_cast<uint32_t>(i);
      break;
    }
  }
  if (sampSlot == UINT32_MAX) {
    sampSlot = static_cast<uint32_t>(this->currentPlan.samplers.size());
    this->currentPlan.samplers.push_back(sampSnap);
  }

  // 10. Texture matrix
  rs.textureMatrix = SoMultiTextureMatrixElement::get(state, 0);
  rs.hasTexture = true;
  rs.textureImageSlot = texSlot;
  rs.samplerSlot = sampSlot;

  return true;
}

uint32_t
SoWgpuFramePlanBuilder::captureRenderState(SoCallbackAction * action, int materialIndex)
{
  // 1. Material
  uint32_t materialSlot = this->captureMaterial(action, materialIndex);

  // 2. Lighting & LightModel
  LightModel lm = LightModel::PHONG;
  LightingSnapshot lightSnap;
  if (action && action->getState()) {
    SoState * envState = action->getState();
    const SbColor & ambient = SoEnvironmentElement::getAmbientColor(envState);
    lightSnap.ambientIntensity = SoEnvironmentElement::getAmbientIntensity(envState);
    for (int k = 0; k < 3; ++k) {
      lightSnap.ambientColor[k] = ambient[k];
    }
  }
  SoState * state = action->getState();
  if (state) {
    int32_t model = SoLazyElement::getLightModel(state);
    if (model == SoLazyElement::BASE_COLOR) {
      lm = LightModel::BASE_COLOR;
    }
  }

  if (lm == LightModel::PHONG && state) {
    const SoNodeList & lights = SoLightElement::getLights(state);
    for (int i = 0; i < lights.getLength(); ++i) {
      SoLight * l = static_cast<SoLight *>(lights[i]);
      if (l && l->on.getValue()) {
        LightSourceSnapshot src;
        const SbVec3f & attenuation =
          static_cast<size_t>(i) < this->lightAttenuationByIndex.size()
            ? this->lightAttenuationByIndex[static_cast<size_t>(i)]
            : SoEnvironmentElement::getLightAttenuation(state);
        for (int k = 0; k < 3; ++k) src.attenuation[k] = attenuation[k];
        const SbColor & c = l->color.getValue();
        src.color[0] = c[0]; src.color[1] = c[1]; src.color[2] = c[2];
        src.intensity = l->intensity.getValue();
        SbMatrix lm = SoLightElement::getMatrix(state, i);
        if (l->isOfType(SoDirectionalLight::getClassTypeId())) {
          src.type = LightType::DIRECTIONAL;
          SoDirectionalLight * dl = static_cast<SoDirectionalLight *>(l);
          SbVec3f dir;
          lm.multDirMatrix(dl->direction.getValue(), dir);
          dir.normalize();
          src.direction[0] = dir[0]; src.direction[1] = dir[1]; src.direction[2] = dir[2];
          src.position[0] = src.position[1] = src.position[2] = 0.0f;
        } else if (l->isOfType(SoPointLight::getClassTypeId())) {
          src.type = LightType::POINT;
          SoPointLight * pl = static_cast<SoPointLight *>(l);
          SbVec3f pos;
          lm.multVecMatrix(pl->location.getValue(), pos);
          src.position[0] = pos[0]; src.position[1] = pos[1]; src.position[2] = pos[2];
          src.direction[0] = src.direction[1] = src.direction[2] = 0.0f;
        } else if (l->isOfType(SoSpotLight::getClassTypeId())) {
          src.type = LightType::SPOT;
          SoSpotLight * sl = static_cast<SoSpotLight *>(l);
          SbVec3f pos, dir;
          lm.multVecMatrix(sl->location.getValue(), pos);
          lm.multDirMatrix(sl->direction.getValue(), dir);
          dir.normalize();
          src.position[0] = pos[0]; src.position[1] = pos[1]; src.position[2] = pos[2];
          src.direction[0] = dir[0]; src.direction[1] = dir[1]; src.direction[2] = dir[2];
          const float cutoff = sl->cutOffAngle.getValue();
          const float dropoff = sl->dropOffRate.getValue();
          src.cutOffAngle = std::isfinite(cutoff) ? std::max(0.0f, std::min(1.570796327f, cutoff)) : cutoff;
          src.dropOffRate = std::isfinite(dropoff) ? std::max(0.0f, std::min(1.0f, dropoff)) : dropoff;
        } else {
          this->isUnsupported = true;
          this->builderError = "Unsupported SoLight subtype";
          break;
        }
        lightSnap.lights.push_back(src);
        if (lightSnap.lights.size() > COIN_WGPU_MAX_LIGHTS) {
          this->isUnsupported = true;
          this->builderError = "More than eight active lights in draw";
          break;
        }
      }
    }
  }
  uint32_t lightingSlot = 0;
  bool lightFound = false;
  for (size_t i = 0; i < this->currentPlan.lightingStates.size(); ++i) {
    const auto & ls = this->currentPlan.lightingStates[i];
    if (ls.lights.size() == lightSnap.lights.size()) {
      if (ls.ambientIntensity != lightSnap.ambientIntensity ||
          std::memcmp(ls.ambientColor, lightSnap.ambientColor, sizeof(ls.ambientColor)) != 0) continue;
      bool allMatch = true;
      for (size_t k = 0; k < ls.lights.size(); ++k) {
        const LightSourceSnapshot & a = ls.lights[k];
        const LightSourceSnapshot & b = lightSnap.lights[k];
        if (a.type != b.type || a.intensity != b.intensity ||
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
    lightingSlot = static_cast<uint32_t>(this->currentPlan.lightingStates.size());
    this->currentPlan.lightingStates.push_back(lightSnap);
  }

  // 3. Camera
  CameraSnapshot camSnap;
  camSnap.viewMatrix = action->getViewingMatrix();
  camSnap.projectionMatrixCoin = action->getProjectionMatrix();
  const SbViewVolume & vv = action->getViewVolume();
  camSnap.isPerspective = (vv.getProjectionType() == SbViewVolume::PERSPECTIVE);
  camSnap.nearDistance = vv.getNearDist();
  camSnap.farDistance = vv.getNearDist() + vv.getDepth();
  if (camSnap.isPerspective && camSnap.nearDistance <= 0.0f) {
    camSnap.nearDistance = 0.1f;
  }
  if (camSnap.farDistance <= camSnap.nearDistance) {
    camSnap.farDistance = camSnap.nearDistance + 100.0f;
  }
  camSnap.focalDistance = action->getFocalDistance();
  const SbViewportRegion & vp = action->getViewportRegion();
  camSnap.aspectRatio = vp.getViewportAspectRatio();

  uint32_t cameraSlot = 0;
  bool camFound = false;
  for (size_t i = 0; i < this->currentPlan.cameras.size(); ++i) {
    const auto & c = this->currentPlan.cameras[i];
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
    cameraSlot = static_cast<uint32_t>(this->currentPlan.cameras.size());
    this->currentPlan.cameras.push_back(camSnap);
  }

  // 4. Viewport
  ViewportSnapshot vpSnap;
  const SbVec2s & origin = vp.getViewportOriginPixels();
  const SbVec2s & size = vp.getViewportSizePixels();
  vpSnap.x = origin[0];
  vpSnap.y = origin[1];
  vpSnap.width = size[0];
  vpSnap.height = size[1];

  uint32_t viewportSlot = 0;
  bool vpFound = false;
  for (size_t i = 0; i < this->currentPlan.viewports.size(); ++i) {
    const auto & v = this->currentPlan.viewports[i];
    if (v.x == vpSnap.x && v.y == vpSnap.y && v.width == vpSnap.width && v.height == vpSnap.height) {
      viewportSlot = static_cast<uint32_t>(i);
      vpFound = true;
      break;
    }
  }
  if (!vpFound) {
    viewportSlot = static_cast<uint32_t>(this->currentPlan.viewports.size());
    this->currentPlan.viewports.push_back(vpSnap);
  }

  // 5. RenderState
  SoShapeHintsElement::VertexOrdering vo;
  SoShapeHintsElement::ShapeType st;
  SoShapeHintsElement::FaceType ft;
  SoShapeHintsElement::get(state, vo, st, ft);

  CullMode cullMode = CullMode::NONE;
  if (st == SoShapeHintsElement::SOLID && vo != SoShapeHintsElement::UNKNOWN_ORDERING) {
    cullMode = CullMode::BACK;
  }
  FrontFace frontFace = (vo == SoShapeHintsElement::CLOCKWISE) ? FrontFace::CW : FrontFace::CCW;

  RenderStateSnapshot rs;
  rs.model = action->getModelMatrix();
  rs.view = camSnap.viewMatrix;
  rs.projectionCoin = camSnap.projectionMatrixCoin;
  rs.materialSlot = materialSlot;
  rs.lightingSlot = lightingSlot;
  rs.cameraSlot = cameraSlot;
  rs.viewportSlot = viewportSlot;
  rs.cullMode = cullMode;
  rs.frontFace = frontFace;
  SbBool depthTest = TRUE;
  SbBool depthWrite = TRUE;
  SoDepthBufferElement::DepthWriteFunction depthFunction = SoDepthBufferElement::LESS;
  SbVec2f depthRange(0.0f, 1.0f);
  SoDepthBufferElement::get(state, depthTest, depthWrite, depthFunction, depthRange);
  rs.depthTest = depthTest != FALSE;
  rs.depthWrite = depthWrite != FALSE;
  rs.depthFunction = static_cast<DepthFunction>(depthFunction);
  rs.depthRange[0] = depthRange[0];
  rs.depthRange[1] = depthRange[1];
  SoPolygonOffsetElement::Style offsetStyles;
  SbBool offsetEnabled;
  SoPolygonOffsetElement::get(state, rs.polygonOffsetFactor,
    rs.polygonOffsetUnits, offsetStyles, offsetEnabled);
  rs.polygonOffsetStyles = static_cast<uint32_t>(offsetStyles);
  rs.polygonOffsetEnabled = offsetEnabled != FALSE;
  rs.lightModel = lm;
  rs.transparencyType = SoShapeStyleElement::getTransparencyType(state);
  float ambientIntensity = 0.0f;
  SbColor ambientColor, fogColor;
  SbVec3f lightAttenuation;
  int32_t fogType = SoEnvironmentElement::NONE;
  float fogVisibility = 0.0f;
  float fogStart = 0.0f;
  SoEnvironmentElement::get(state, ambientIntensity, ambientColor,
                            lightAttenuation, fogType, fogColor,
                            fogVisibility, fogStart);
  rs.fogMode = static_cast<FogMode>(fogType);
  for (int c = 0; c < 3; ++c) rs.fogColor[c] = fogColor[c];
  rs.fogStart = fogStart;
  rs.fogEnd = fogVisibility > 0.0f ? fogVisibility : camSnap.farDistance;
  float curLw = action->getLineWidth();
  float curPs = action->getPointSize();
  rs.lineWidth = (curLw <= 0.0f) ? 1.0f : curLw;
  rs.pointSize = (curPs <= 0.0f) ? 1.0f : curPs;
  rs.linePattern = state
    ? static_cast<uint32_t>(SoLinePatternElement::get(state)) & 0xffffu : 0xffffu;
  rs.linePatternScaleFactor = state
    ? std::max(1, SoLinePatternElement::getScaleFactor(state)) : 1;

  this->captureTexture(action, rs, &this->builderError);

  uint32_t rsSlot = 0;
  bool rsFound = false;
  for (size_t i = 0; i < this->currentPlan.renderStates.size(); ++i) {
    const auto & existing = this->currentPlan.renderStates[i];
    if (existing.materialSlot == materialSlot &&
        existing.lightingSlot == lightingSlot &&
        existing.lightModel == rs.lightModel &&
        existing.transparencyType == rs.transparencyType &&
        existing.cameraSlot == cameraSlot &&
        existing.viewportSlot == viewportSlot &&
        existing.cullMode == cullMode &&
        existing.frontFace == frontFace &&
        existing.depthTest == rs.depthTest &&
        existing.depthWrite == rs.depthWrite &&
        existing.depthFunction == rs.depthFunction &&
        existing.depthRange[0] == rs.depthRange[0] &&
        existing.depthRange[1] == rs.depthRange[1] &&
        existing.polygonOffsetEnabled == rs.polygonOffsetEnabled &&
        existing.polygonOffsetFactor == rs.polygonOffsetFactor &&
        existing.polygonOffsetUnits == rs.polygonOffsetUnits &&
        existing.polygonOffsetStyles == rs.polygonOffsetStyles &&
        existing.fogMode == rs.fogMode &&
        existing.fogStart == rs.fogStart &&
        existing.fogEnd == rs.fogEnd &&
        std::memcmp(existing.fogColor, rs.fogColor, sizeof(rs.fogColor)) == 0 &&
        existing.lineWidth == rs.lineWidth &&
        existing.pointSize == rs.pointSize &&
        existing.hasTexture == rs.hasTexture &&
        existing.linePattern == rs.linePattern &&
        existing.linePatternScaleFactor == rs.linePatternScaleFactor &&
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
    rsSlot = static_cast<uint32_t>(this->currentPlan.renderStates.size());
    this->currentPlan.renderStates.push_back(rs);
  }
  return rsSlot;
}

uint32_t
SoWgpuFramePlanBuilder::addVertex(const SoPrimitiveVertex * pv, uint32_t materialSlot)
{
  VertexSnapshot v;
  const SbVec3f & pt = pv->getPoint();
  const SbVec3f & n = pv->getNormal();
  const SbVec4f & tc = pv->getTextureCoords();

  v.position[0] = pt[0];
  v.position[1] = pt[1];
  v.position[2] = pt[2];

  v.normal[0] = n[0];
  v.normal[1] = n[1];
  v.normal[2] = n[2];

  v.texcoord[0] = tc[0];
  v.texcoord[1] = tc[1];
  v.materialSlot = materialSlot;

  uint32_t idx = static_cast<uint32_t>(this->currentPlan.vertices.size());
  this->currentPlan.vertices.push_back(v);
  return idx;
}

void
SoWgpuFramePlanBuilder::ensureDrawPacket(PrimitiveTopology topology, uint32_t renderStateSlot, SoNode * node, bool forceNewPacket)
{
  SbUniqueId nodeId = node ? node->getNodeId() : 0;
  if (!forceNewPacket && this->hasActiveDraw) {
    const DrawPacket & active = this->currentPlan.draws[this->currentDrawIndex];
    if (active.topology == topology &&
        active.renderStateSlot == renderStateSlot &&
        active.renderLayer == this->currentAnnotationLayer &&
        active.sourceNodeId == nodeId) {
      return; // Continue active packet
    }
  }

  DrawPacket dp;
  dp.topology = topology;
  dp.renderStateSlot = renderStateSlot;
  dp.frameNodeOrdinal = ++this->nodeCounter;
  dp.sourceNodeId = nodeId;
  dp.renderLayer = this->currentAnnotationLayer;
  dp.clearDepthBefore = dp.renderLayer != 0 && this->annotationDepthClearPending;
  if (dp.clearDepthBefore) this->annotationDepthClearPending = false;
  dp.geometry.firstVertex = static_cast<uint32_t>(this->currentPlan.vertices.size());
  dp.geometry.vertexCount = 0;
  dp.geometry.firstIndex = static_cast<uint32_t>(this->currentPlan.indices.size());
  dp.geometry.indexCount = 0;

  this->currentDrawIndex = static_cast<uint32_t>(this->currentPlan.draws.size());
  this->currentPlan.draws.push_back(dp);
  this->hasActiveDraw = true;
}

void
SoWgpuFramePlanBuilder::addTriangle(SoCallbackAction * action,
                                   const SoPrimitiveVertex * v0,
                                   const SoPrimitiveVertex * v1,
                                   const SoPrimitiveVertex * v2)
{
  if (!v0 || !v1 || !v2) return;
  uint32_t rsSlot = this->captureRenderState(action, v0->getMaterialIndex());
  this->ensureDrawPacket(PrimitiveTopology::TRIANGLE_LIST, rsSlot, action->getCurPathTail());

  uint32_t m0 = this->captureMaterial(action, v0->getMaterialIndex());
  uint32_t m1 = this->captureMaterial(action, v1->getMaterialIndex());
  uint32_t m2 = this->captureMaterial(action, v2->getMaterialIndex());

  uint32_t i0 = this->addVertex(v0, m0);
  uint32_t i1 = this->addVertex(v1, m1);
  uint32_t i2 = this->addVertex(v2, m2);

  this->currentPlan.indices.push_back(i0);
  this->currentPlan.indices.push_back(i1);
  this->currentPlan.indices.push_back(i2);

  DrawPacket & dp = this->currentPlan.draws[this->currentDrawIndex];
  dp.geometry.vertexCount += 3;
  dp.geometry.indexCount += 3;
}

void
SoWgpuFramePlanBuilder::addLine(SoCallbackAction * action,
                                const SoPrimitiveVertex * v0,
                                const SoPrimitiveVertex * v1)
{
  if (!v0 || !v1) return;
  uint32_t rsSlot = this->captureRenderState(action, v0->getMaterialIndex());
  if (this->isUnsupported) return;
  if (this->currentPlan.renderStates[rsSlot].hasTexture) {
    this->isUnsupported = true;
    this->builderError = "Textured lines are not supported";
    return;
  }
  this->ensureDrawPacket(PrimitiveTopology::LINE_LIST, rsSlot, action->getCurPathTail());

  uint32_t m0 = this->captureMaterial(action, v0->getMaterialIndex());
  uint32_t m1 = this->captureMaterial(action, v1->getMaterialIndex());

  uint32_t i0 = this->addVertex(v0, m0);
  uint32_t i1 = this->addVertex(v1, m1);

  this->currentPlan.indices.push_back(i0);
  this->currentPlan.indices.push_back(i1);

  DrawPacket & dp = this->currentPlan.draws[this->currentDrawIndex];
  dp.geometry.vertexCount += 2;
  dp.geometry.indexCount += 2;
}

void
SoWgpuFramePlanBuilder::addPoint(SoCallbackAction * action,
                                 const SoPrimitiveVertex * vertex)
{
  if (!vertex) return;
  uint32_t rsSlot = this->captureRenderState(action, vertex->getMaterialIndex());
  if (this->isUnsupported) return;
  if (this->currentPlan.renderStates[rsSlot].hasTexture) {
    this->isUnsupported = true;
    this->builderError = "Textured points are not supported";
    return;
  }
  this->ensureDrawPacket(PrimitiveTopology::POINT_LIST, rsSlot, action->getCurPathTail());

  uint32_t m0 = this->captureMaterial(action, vertex->getMaterialIndex());
  uint32_t i0 = this->addVertex(vertex, m0);

  this->currentPlan.indices.push_back(i0);

  DrawPacket & dp = this->currentPlan.draws[this->currentDrawIndex];
  dp.geometry.vertexCount += 1;
  dp.geometry.indexCount += 1;
}

bool
SoWgpuFramePlanBuilder::expandStyledPrimitives(std::string * outError)
{
  const size_t originalDrawCount = this->currentPlan.draws.size();
  std::vector<DrawPacket> expandedDraws;
  expandedDraws.reserve(originalDrawCount);

  auto fail = [&](const char * message) {
    this->hasError = true;
    this->builderError = message;
    if (outError) *outError = message;
    return false;
  };

  auto materialAt = [&](uint32_t firstSlot, uint32_t secondSlot, float t) -> uint32_t {
    if (t <= 0.0f || firstSlot == secondSlot) return firstSlot;
    if (t >= 1.0f) return secondSlot;
    const MaterialSnapshot first = this->currentPlan.materials[firstSlot];
    const MaterialSnapshot second = this->currentPlan.materials[secondSlot];
    MaterialSnapshot material;
    for (int channel = 0; channel < 4; ++channel) {
      material.ambient[channel] = first.ambient[channel] +
        (second.ambient[channel] - first.ambient[channel]) * t;
      material.diffuse[channel] = first.diffuse[channel] +
        (second.diffuse[channel] - first.diffuse[channel]) * t;
      material.specular[channel] = first.specular[channel] +
        (second.specular[channel] - first.specular[channel]) * t;
      material.emission[channel] = first.emission[channel] +
        (second.emission[channel] - first.emission[channel]) * t;
    }
    material.shininess = first.shininess +
      (second.shininess - first.shininess) * t;
    material.transparency = first.transparency +
      (second.transparency - first.transparency) * t;
    for (size_t i = 0; i < this->currentPlan.materials.size(); ++i) {
      if (std::memcmp(&this->currentPlan.materials[i], &material,
                      sizeof(MaterialSnapshot)) == 0) {
        return static_cast<uint32_t>(i);
      }
    }
    const uint32_t slot =
      static_cast<uint32_t>(this->currentPlan.materials.size());
    this->currentPlan.materials.push_back(material);
    return slot;
  };

  auto appendVertex = [&](float x, float y, float z,
                          uint32_t materialSlot) -> uint32_t {
    VertexSnapshot vertex;
    vertex.position[0] = x;
    vertex.position[1] = y;
    vertex.position[2] = z;
    vertex.normal[2] = 1.0f;
    vertex.materialSlot = materialSlot;
    const uint32_t index =
      static_cast<uint32_t>(this->currentPlan.vertices.size());
    this->currentPlan.vertices.push_back(vertex);
    return index;
  };

  for (size_t drawIndex = 0; drawIndex < originalDrawCount; ++drawIndex) {
    const DrawPacket original = this->currentPlan.draws[drawIndex];
    if (original.topology == PrimitiveTopology::TRIANGLE_LIST) {
      expandedDraws.push_back(original);
      continue;
    }
    if (original.renderStateSlot >= this->currentPlan.renderStates.size()) {
      return fail("Styled primitive references invalid render state");
    }
    const RenderStateSnapshot sourceState =
      this->currentPlan.renderStates[original.renderStateSlot];
    if (sourceState.viewportSlot >= this->currentPlan.viewports.size()) {
      return fail("Styled primitive references invalid viewport");
    }
    const ViewportSnapshot viewport =
      this->currentPlan.viewports[sourceState.viewportSlot];
    if (viewport.width <= 0 || viewport.height <= 0) {
      return fail("Styled primitive has an empty viewport");
    }
    if (original.geometry.firstIndex > this->currentPlan.indices.size() ||
        original.geometry.indexCount >
          this->currentPlan.indices.size() - original.geometry.firstIndex) {
      return fail("Styled primitive index range is invalid");
    }
    auto foggedMaterialAt = [&](uint32_t materialSlot,
                                const VertexSnapshot & vertex) -> uint32_t {
      if (sourceState.fogMode == FogMode::NONE) return materialSlot;
      MaterialSnapshot material = this->currentPlan.materials[materialSlot];
      SbVec4f viewPosition;
      const SbMatrix modelView = sourceState.model * sourceState.view;
      modelView.multVecMatrix(
        SbVec4f(vertex.position[0], vertex.position[1],
                vertex.position[2], 1.0f), viewPosition);
      const float distance = std::max(0.0f, -viewPosition[2]);
      float factor = 1.0f;
      if (sourceState.fogMode == FogMode::HAZE) {
        factor = (sourceState.fogEnd - distance) /
                 (sourceState.fogEnd - sourceState.fogStart);
      } else if (sourceState.fogMode == FogMode::FOG) {
        factor = std::exp(-5.545f * distance / sourceState.fogEnd);
      } else {
        const float fogDistance = 2.35f * distance / sourceState.fogEnd;
        factor = std::exp(-(fogDistance * fogDistance));
      }
      factor = std::max(0.0f, std::min(1.0f, factor));
      for (int channel = 0; channel < 3; ++channel) {
        material.diffuse[channel] =
          sourceState.fogColor[channel] * (1.0f - factor) +
          material.diffuse[channel] * factor;
      }
      for (size_t i = 0; i < this->currentPlan.materials.size(); ++i) {
        if (std::memcmp(&this->currentPlan.materials[i], &material,
                        sizeof(MaterialSnapshot)) == 0) {
          return static_cast<uint32_t>(i);
        }
      }
      const uint32_t slot =
        static_cast<uint32_t>(this->currentPlan.materials.size());
      this->currentPlan.materials.push_back(material);
      return slot;
    };


    RenderStateSnapshot state = sourceState;
    state.polygonOffsetPrimitiveStyle =
      original.topology == PrimitiveTopology::LINE_LIST ? 2u : 4u;
    state.model = SbMatrix::identity();
    state.view = SbMatrix::identity();
    state.projectionCoin = SbMatrix::identity();
    state.cullMode = CullMode::NONE;
    state.lightModel = LightModel::BASE_COLOR;
    state.lineWidth = 1.0f;
    state.pointSize = 1.0f;
    state.linePattern = 0xffffu;
    state.linePatternScaleFactor = 1;
    state.hasTexture = false;
    state.fogMode = FogMode::NONE;
    const uint32_t stateSlot =
      static_cast<uint32_t>(this->currentPlan.renderStates.size());
    this->currentPlan.renderStates.push_back(state);

    DrawPacket expanded = original;
    expanded.topology = PrimitiveTopology::TRIANGLE_LIST;
    expanded.renderStateSlot = stateSlot;
    expanded.geometry.firstVertex =
      static_cast<uint32_t>(this->currentPlan.vertices.size());
    expanded.geometry.vertexCount = 0;
    expanded.geometry.firstIndex =
      static_cast<uint32_t>(this->currentPlan.indices.size());
    expanded.geometry.indexCount = 0;

    const SbMatrix mvp =
      sourceState.model * sourceState.view * sourceState.projectionCoin;
    auto project = [&](const VertexSnapshot & vertex, SbVec3f & ndc) {
      SbVec4f clip;
      mvp.multVecMatrix(SbVec4f(vertex.position[0], vertex.position[1],
                               vertex.position[2], 1.0f), clip);
      if (!std::isfinite(clip[0]) || !std::isfinite(clip[1]) ||
          !std::isfinite(clip[2]) || !std::isfinite(clip[3]) ||
          clip[3] <= 1.0e-6f) return false;
      ndc.setValue(clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3]);
      return std::isfinite(ndc[0]) && std::isfinite(ndc[1]) &&
        std::isfinite(ndc[2]);
    };

    if (original.topology == PrimitiveTopology::LINE_LIST) {
      const float width = std::max(sourceState.lineWidth, 1.0f);
      const float halfWidth = width * 0.5f;
      const uint32_t pattern = sourceState.linePattern & 0xffffu;
      const float patternScale =
        static_cast<float>(std::max(1, sourceState.linePatternScaleFactor));
      for (size_t offset = 0; offset + 1 < original.geometry.indexCount;
           offset += 2) {
        const uint32_t firstIndex =
          this->currentPlan.indices[original.geometry.firstIndex + offset];
        const uint32_t secondIndex =
          this->currentPlan.indices[original.geometry.firstIndex + offset + 1];
        if (firstIndex >= this->currentPlan.vertices.size() ||
            secondIndex >= this->currentPlan.vertices.size()) {
          return fail("Styled line references invalid vertex");
        }
        const VertexSnapshot firstVertex = this->currentPlan.vertices[firstIndex];
        const VertexSnapshot secondVertex = this->currentPlan.vertices[secondIndex];
        if (firstVertex.materialSlot >= this->currentPlan.materials.size() ||
            secondVertex.materialSlot >= this->currentPlan.materials.size()) {
          return fail("Styled line references invalid material");
        }
        SbVec3f firstNdc, secondNdc;
        if (!project(firstVertex, firstNdc) || !project(secondVertex, secondNdc))
          continue;
        const float dxPixels =
          (secondNdc[0] - firstNdc[0]) * 0.5f * viewport.width;
        const uint32_t firstMaterial =
          foggedMaterialAt(firstVertex.materialSlot, firstVertex);
        const uint32_t secondMaterial =
          foggedMaterialAt(secondVertex.materialSlot, secondVertex);
        const float dyPixels =
          (secondNdc[1] - firstNdc[1]) * 0.5f * viewport.height;
        const float lengthPixels =
          std::sqrt(dxPixels * dxPixels + dyPixels * dyPixels);
        if (lengthPixels <= 1.0e-6f || pattern == 0u) continue;
        const float offsetX =
          (-dyPixels / lengthPixels) * halfWidth * 2.0f / viewport.width;
        const float offsetY =
          (dxPixels / lengthPixels) * halfWidth * 2.0f / viewport.height;

        float cursor = 0.0f;
        while (cursor < lengthPixels - 1.0e-5f) {
          float next = lengthPixels;
          bool visible = true;
          if (pattern != 0xffffu) {
            const uint32_t patternCell =
              static_cast<uint32_t>(std::floor(cursor / patternScale));
            next = std::min(lengthPixels,
              (static_cast<float>(patternCell) + 1.0f) * patternScale);
            visible = (pattern & (1u << (patternCell & 15u))) != 0u;
          }
          if (next <= cursor + 1.0e-6f)
            next = std::min(lengthPixels, cursor + patternScale);
          if (visible) {
            const float t0 = cursor / lengthPixels;
            const float t1 = next / lengthPixels;
            const SbVec3f start = firstNdc + (secondNdc - firstNdc) * t0;
            const SbVec3f end = firstNdc + (secondNdc - firstNdc) * t1;
            const uint32_t startMaterial = materialAt(
              firstMaterial, secondMaterial, t0);
            const uint32_t endMaterial = materialAt(
              firstMaterial, secondMaterial, t1);
            const uint32_t base =
              static_cast<uint32_t>(this->currentPlan.vertices.size());
            appendVertex(start[0] + offsetX, start[1] + offsetY,
                         start[2], startMaterial);
            appendVertex(start[0] - offsetX, start[1] - offsetY,
                         start[2], startMaterial);
            appendVertex(end[0] + offsetX, end[1] + offsetY,
                         end[2], endMaterial);
            appendVertex(end[0] - offsetX, end[1] - offsetY,
                         end[2], endMaterial);
            const uint32_t quadIndices[6] = {
              base, base + 1, base + 2, base + 2, base + 1, base + 3
            };
            this->currentPlan.indices.insert(this->currentPlan.indices.end(),
                                             quadIndices, quadIndices + 6);
          }
          cursor = next;
        }
      }
    } else if (original.topology == PrimitiveTopology::POINT_LIST) {
      const float halfSize = std::max(sourceState.pointSize, 1.0f) * 0.5f;
      const float offsetX = halfSize * 2.0f / viewport.width;
      const float offsetY = halfSize * 2.0f / viewport.height;
      for (size_t offset = 0; offset < original.geometry.indexCount; ++offset) {
        const uint32_t vertexIndex =
          this->currentPlan.indices[original.geometry.firstIndex + offset];
        if (vertexIndex >= this->currentPlan.vertices.size()) {
          return fail("Styled point references invalid vertex");
        }
        const VertexSnapshot vertex = this->currentPlan.vertices[vertexIndex];
        if (vertex.materialSlot >= this->currentPlan.materials.size()) {
          return fail("Styled point references invalid material");
        }
        SbVec3f ndc;
        if (!project(vertex, ndc)) continue;
        const uint32_t materialSlot =
          foggedMaterialAt(vertex.materialSlot, vertex);
        const uint32_t base =
          static_cast<uint32_t>(this->currentPlan.vertices.size());
        appendVertex(ndc[0] - offsetX, ndc[1] - offsetY,
                     ndc[2], materialSlot);
        appendVertex(ndc[0] + offsetX, ndc[1] - offsetY,
                     ndc[2], materialSlot);
        appendVertex(ndc[0] - offsetX, ndc[1] + offsetY,
                     ndc[2], materialSlot);
        appendVertex(ndc[0] + offsetX, ndc[1] + offsetY,
                     ndc[2], materialSlot);
        const uint32_t quadIndices[6] = {
          base, base + 1, base + 2, base + 2, base + 1, base + 3
        };
        this->currentPlan.indices.insert(this->currentPlan.indices.end(),
                                         quadIndices, quadIndices + 6);
      }
    }

    expanded.geometry.vertexCount =
      static_cast<uint32_t>(this->currentPlan.vertices.size()) -
      expanded.geometry.firstVertex;
    expanded.geometry.indexCount =
      static_cast<uint32_t>(this->currentPlan.indices.size()) -
      expanded.geometry.firstIndex;
    if (expanded.geometry.indexCount != 0) expandedDraws.push_back(expanded);
  }

  this->currentPlan.draws.swap(expandedDraws);
  this->hasActiveDraw = false;
  return true;
}

bool
SoWgpuFramePlanBuilder::build(FramePlan & outPlan, std::string * outError)
{
  if (this->hasError || this->isUnsupported) {
    if (outError) *outError = this->builderError.empty() ? "Builder encountered unsupported or invalid feature" : this->builderError;
    return false;
  }
  if (!this->expandStyledPrimitives(outError)) {
    return false;
  }
  if (!this->currentPlan.isValid(outError)) {
    return false;
  }
  std::vector<SoWgpuCompositionItem> order;
  std::string compositionError;
  if (!coin_wgpu_composition_order(this->currentPlan, order, compositionError)) {
    this->isUnsupported = true;
    this->builderError = compositionError;
    if (outError) *outError = compositionError;
    return false;
  }
  outPlan = this->currentPlan;
  outPlan.revision = SoWgpuFramePlanBuilder::nextRevision();
  return true;
}

uint64_t
SoWgpuFramePlanBuilder::nextRevision()
{
  static std::atomic<uint64_t> revision(1);
  uint64_t value = revision.fetch_add(1, std::memory_order_relaxed);
  if (value == 0) value = revision.fetch_add(1, std::memory_order_relaxed);
  return value;
}

namespace {

SoWgpuIndexedGeometryOptions
captureIndexedGeometryOptions(SoCallbackAction * action, SoNode * node)
{
  SoWgpuIndexedGeometryOptions options;
  SoState * state = action ? action->getState() : NULL;
  options.hasTraversalState = state != NULL;
  if (!state) return options;

  const SoLazyElement * lazy = SoLazyElement::getInstance(state);
  if (lazy) options.materialCount = lazy->getNumDiffuse();

  SbVec2s textureSize;
  int textureComponents = 0;
  const unsigned char * textureBytes =
    SoMultiTextureImageElement::getImage(
      state, 0, textureSize, textureComponents);
  options.hasTexture = textureBytes != NULL &&
    textureSize[0] > 0 && textureSize[1] > 0 &&
    textureComponents > 0;
  if (options.hasTexture) {
    const SoMultiTextureCoordinateElement * coordinates =
      SoMultiTextureCoordinateElement::getInstance(state);
    if (coordinates) {
      const SoMultiTextureCoordinateElement::CoordType type =
        coordinates->getType(0);
      options.proceduralTextureCoordinates =
        type == SoMultiTextureCoordinateElement::DEFAULT ||
        type == SoMultiTextureCoordinateElement::FUNCTION;
    }
  }

  options.baseColorLighting =
    SoLightModelElement::get(state) == SoLightModelElement::BASE_COLOR;
  options.counterClockwise =
    SoShapeHintsElement::getVertexOrdering(state) !=
      SoShapeHintsElement::CLOCKWISE;
  const SbBool vrml1 = node && node->getNodeType() == SoNode::VRML1;
  options.creaseAngle = SoCreaseAngleElement::get(state, vrml1);
  return options;
}

void
publishCoreDiagnostic(const SoWgpuIndexedGeometryResult & result,
                      std::string * outError)
{
  if (outError && !result.diagnostic.empty()) {
    *outError = result.diagnostic;
  }
}

} // namespace

FastPathResult
SoWgpuFramePlanBuilder::processIndexedFaceSet(
  SoCallbackAction * action,
  const DirectGeometryView & view,
  SoNode * node,
  std::string * outError)
{
  if (!action) {
    if (outError) *outError = "Null SoCallbackAction in processIndexedFaceSet";
    return FastPathResult::INVALID_SCENE;
  }
  if (view.positions.empty() || view.coordIndex.empty()) {
    return FastPathResult::SUCCESS_PRUNE;
  }
  if (this->isUnsupported) {
    if (outError) {
      *outError = this->builderError.empty()
        ? "Unsupported feature in IndexedFaceSet"
        : this->builderError;
    }
    return FastPathResult::UNSUPPORTED;
  }

  const SoWgpuIndexedGeometryOptions options =
    captureIndexedGeometryOptions(action, node);
  SoWgpuIndexedGeometryResult transformed =
    SoWgpuIndexedGeometryCore::buildFaces(view, options);
  publishCoreDiagnostic(transformed, outError);
  if (transformed.status != FastPathResult::SUCCESS_PRUNE ||
      transformed.indices.empty()) {
    return transformed.status;
  }

  // Material lookup and the atomic FramePlan commit remain in Wiring.
  this->captureMaterial(action, 0);
  for (size_t i = 0; i < transformed.vertices.size(); ++i) {
    transformed.vertices[i].vertex.materialSlot = this->captureMaterial(
      action, transformed.vertices[i].materialIndex);
  }

  const uint32_t renderStateSlot = this->captureRenderState(action, 0);
  if (this->isUnsupported) {
    if (outError) {
      *outError = this->builderError.empty()
        ? "Unsupported feature in IndexedFaceSet"
        : this->builderError;
    }
    return FastPathResult::UNSUPPORTED;
  }
  this->ensureDrawPacket(
    PrimitiveTopology::TRIANGLE_LIST, renderStateSlot, node, true);

  const uint32_t vertexOffset =
    static_cast<uint32_t>(this->currentPlan.vertices.size());
  for (size_t i = 0; i < transformed.vertices.size(); ++i) {
    this->currentPlan.vertices.push_back(transformed.vertices[i].vertex);
  }
  for (size_t i = 0; i < transformed.indices.size(); ++i) {
    this->currentPlan.indices.push_back(
      vertexOffset + transformed.indices[i]);
  }

  DrawPacket & packet = this->currentPlan.draws[this->currentDrawIndex];
  packet.geometry.vertexCount =
    static_cast<uint32_t>(transformed.vertices.size());
  packet.geometry.indexCount =
    static_cast<uint32_t>(transformed.indices.size());
  const uint64_t stableId = reinterpret_cast<uint64_t>(node);
  packet.stableNodeId = stableId;
  packet.drawOrdinal = this->nodeOccurrenceCount[stableId]++;
  packet.sourceRevision = SoWgpuIndexedGeometryCore::payloadDigest(
    transformed.vertices, transformed.indices);
  return FastPathResult::SUCCESS_PRUNE;
}

FastPathResult
SoWgpuFramePlanBuilder::processIndexedLineSet(
  SoCallbackAction * action,
  const DirectGeometryView & view,
  SoNode * node,
  std::string * outError)
{
  if (!action) {
    if (outError) *outError = "Null SoCallbackAction in processIndexedLineSet";
    return FastPathResult::INVALID_SCENE;
  }
  if (view.positions.empty() || view.coordIndex.empty()) {
    return FastPathResult::SUCCESS_PRUNE;
  }
  if (this->isUnsupported) {
    if (outError) {
      *outError = this->builderError.empty()
        ? "Unsupported feature in IndexedLineSet"
        : this->builderError;
    }
    return FastPathResult::UNSUPPORTED;
  }

  const SoWgpuIndexedGeometryOptions options =
    captureIndexedGeometryOptions(action, node);
  SoWgpuIndexedGeometryResult transformed =
    SoWgpuIndexedGeometryCore::buildLines(view, options);
  publishCoreDiagnostic(transformed, outError);
  if (transformed.status != FastPathResult::SUCCESS_PRUNE ||
      transformed.indices.empty()) {
    return transformed.status;
  }

  this->captureMaterial(action, 0);
  for (size_t i = 0; i < transformed.vertices.size(); ++i) {
    transformed.vertices[i].vertex.materialSlot = this->captureMaterial(
      action, transformed.vertices[i].materialIndex);
  }

  const uint32_t renderStateSlot = this->captureRenderState(action, 0);
  if (this->isUnsupported) {
    if (outError) {
      *outError = this->builderError.empty()
        ? "Unsupported feature in IndexedLineSet"
        : this->builderError;
    }
    return FastPathResult::UNSUPPORTED;
  }
  this->ensureDrawPacket(
    PrimitiveTopology::LINE_LIST, renderStateSlot, node, true);

  const uint32_t vertexOffset =
    static_cast<uint32_t>(this->currentPlan.vertices.size());
  for (size_t i = 0; i < transformed.vertices.size(); ++i) {
    this->currentPlan.vertices.push_back(transformed.vertices[i].vertex);
  }
  for (size_t i = 0; i < transformed.indices.size(); ++i) {
    this->currentPlan.indices.push_back(
      vertexOffset + transformed.indices[i]);
  }

  DrawPacket & packet = this->currentPlan.draws[this->currentDrawIndex];
  packet.geometry.vertexCount =
    static_cast<uint32_t>(transformed.vertices.size());
  packet.geometry.indexCount =
    static_cast<uint32_t>(transformed.indices.size());
  const uint64_t stableId = reinterpret_cast<uint64_t>(node);
  packet.stableNodeId = stableId;
  packet.drawOrdinal = this->nodeOccurrenceCount[stableId]++;
  packet.sourceRevision = SoWgpuIndexedGeometryCore::payloadDigest(
    transformed.vertices, transformed.indices);
  return FastPathResult::SUCCESS_PRUNE;
}
