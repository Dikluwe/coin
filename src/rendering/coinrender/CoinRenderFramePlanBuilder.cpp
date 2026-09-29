#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif
#include <iostream>
#include "rendering/coinrender/CoinRenderFramePlanBuilder.h"
#include "rendering/coinrender/CoinRenderImageCore.h"
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoCylinder.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/elements/SoMaterialBindingElement.h>
#include <Inventor/elements/SoNormalBindingElement.h>
#include <Inventor/elements/SoNormalElement.h>
#include <Inventor/details/SoCylinderDetail.h>
#include <Inventor/elements/SoDrawStyleElement.h>
#include "rendering/coinrender/CoinRenderPolygonStyleCore.h"
#include "rendering/coinrender/CoinRenderStrokeCore.h"
#include "rendering/coinrender/CoinRenderTextureCombineCore.h"
#include "rendering/coinrender/CoinRenderDepthPolicyElement.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderClipCore.h"
#include <Inventor/elements/SoClipPlaneElement.h>
#include "rendering/coinrender/CoinRenderIndexedGeometryCore.h"

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
#include <Inventor/elements/SoMultiTextureEnabledElement.h>
#include <Inventor/elements/SoCoordinateElement.h>
#include <Inventor/details/SoPointDetail.h>
#include <Inventor/details/SoFaceDetail.h>
#include <Inventor/details/SoLineDetail.h>
#include <Inventor/elements/SoMultiTextureMatrixElement.h>
#include <Inventor/elements/SoTextureQualityElement.h>
#include <Inventor/elements/SoTextureUnitElement.h>
#include <Inventor/elements/SoMultiTextureCoordinateElement.h>
#include <Inventor/elements/SoTextureCoordinateBindingElement.h>

#include <cassert>
#include <atomic>
#include <cmath>

CoinRenderFramePlanBuilder::CoinRenderFramePlanBuilder()
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

CoinRenderFramePlanBuilder::~CoinRenderFramePlanBuilder()
{
}

void
CoinRenderFramePlanBuilder::beginFrame(const SbColor4f & clearColor, const SbViewportRegion & /*viewport*/)
{
  this->reset();
  this->currentPlan.clearColor = clearColor;
  this->inFrame = true;
}

void
CoinRenderFramePlanBuilder::reset()
{
  this->polygonNode = nullptr;
  this->polygonVertices.clear();
  this->polygonPositions.clear();
  this->polygonCaptured.clear();
  this->polygonTriangles = 0;
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
  this->lineNode = nullptr;
  this->lineIndex = -1;
  this->lineStripId = this->nextLineStripId = 0;
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
  this->sceneTextures.clear();
  this->nodeOccurrenceCount.clear();
  this->lightAttenuationByIndex.clear();
}

void
CoinRenderFramePlanBuilder::beginAnnotation()
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
CoinRenderFramePlanBuilder::beginForeground()
{
  this->foregroundLayer = this->currentAnnotationLayer;
  this->currentAnnotationLayer = this->nextAnnotationLayer++;
  this->annotationDepthClearPending = false;
  this->hasActiveDraw = false;
}

void
CoinRenderFramePlanBuilder::endForeground()
{
  this->currentAnnotationLayer = this->foregroundLayer;
  this->annotationDepthClearPending = false;
  this->hasActiveDraw = false;
}

void
CoinRenderFramePlanBuilder::reserveDelayedLayers(uint32_t count)
{
  // Reserve one shared pass between scene and ordinary foreground annotations.
  for (CoinRenderDrawPacket & draw : this->currentPlan.draws)
    if (draw.renderLayer) draw.renderLayer += count;
  this->nextAnnotationLayer += count;
}

void
CoinRenderFramePlanBuilder::beginDelayedAnnotations(uint32_t layer, bool clearDepth)
{
  this->savedAnnotationLayer = 0;
  this->savedAnnotationClear = false;
  this->annotationDepth = 1;
  this->currentAnnotationLayer = layer;
  this->annotationDepthClearPending = clearDepth;
  this->hasActiveDraw = false;
}

void
CoinRenderFramePlanBuilder::endAnnotation()
{
  if (this->annotationDepth == 0) return;
  if (--this->annotationDepth == 0) {
    this->currentAnnotationLayer = this->savedAnnotationLayer;
    this->annotationDepthClearPending = this->savedAnnotationClear;
    this->hasActiveDraw = false;
  }
}

void CoinRenderFramePlanBuilder::registerSceneTexture(const unsigned char* image,
                                                      uint64_t producerId, uint32_t width,
                                                      uint32_t height, bool opaque,
                                                      int32_t transparencyFunction) {
  this->sceneTextures[image] =
      SceneTexture{producerId, width, height, opaque, transparencyFunction};
}

void
CoinRenderFramePlanBuilder::recordLightAttenuation(SoCallbackAction * action)
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
CoinRenderFramePlanBuilder::captureMaterial(SoCallbackAction * action, int materialIndex)
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

  CoinRenderMaterialSnapshot matSnap;
  matSnap.ambient[0] = amb[0]; matSnap.ambient[1] = amb[1]; matSnap.ambient[2] = amb[2]; matSnap.ambient[3] = 1.0f;
  matSnap.diffuse[0] = diff[0]; matSnap.diffuse[1] = diff[1]; matSnap.diffuse[2] = diff[2]; matSnap.diffuse[3] = 1.0f - transp;
  matSnap.specular[0] = spec[0]; matSnap.specular[1] = spec[1]; matSnap.specular[2] = spec[2]; matSnap.specular[3] = 1.0f;
  matSnap.emission[0] = emiss[0]; matSnap.emission[1] = emiss[1]; matSnap.emission[2] = emiss[2]; matSnap.emission[3] = 1.0f;
  matSnap.shininess = shin;
  matSnap.transparency = transp;

  for (size_t i = 0; i < this->currentPlan.materials.size(); ++i) {
    const auto & m = this->currentPlan.materials[i];
    if (std::memcmp(&m, &matSnap, sizeof(CoinRenderMaterialSnapshot)) == 0) {
      return static_cast<uint32_t>(i);
    }
  }
  uint32_t materialSlot = static_cast<uint32_t>(this->currentPlan.materials.size());
  this->currentPlan.materials.push_back(matSnap);
  return materialSlot;
}

bool
CoinRenderFramePlanBuilder::captureTexture(SoCallbackAction * action, CoinRenderRenderStateSnapshot & rs, std::string * outError)
{
  if (!captureTextureUnit(action, 0, rs, outError)) return false;
  SoState * state = action ? action->getState() : nullptr;
  if (!state) return true;
  int lastEnabled = -1;
  SoMultiTextureEnabledElement::getEnabledUnits(state, lastEnabled);
  if (lastEnabled >= static_cast<int>(COIN_RENDER_MAX_TEXTURE_UNITS)) {
    this->isUnsupported = true;
    this->builderError = "At most eight texture units are supported";
    if (outError) *outError = this->builderError;
    return false;
  }
  for (int unit = 1; unit <= lastEnabled; ++unit) {
    if (!SoMultiTextureEnabledElement::get(state, unit)) continue;
    CoinRenderRenderStateSnapshot captured;
    if (!captureTextureUnit(action, unit, captured, outError)) return false;
    rs.extraTextures[unit - 1] = coin_render_texture_unit(captured, 0);

  }
  for (int unit = 0; unit <= lastEnabled; ++unit) {
    if (!coin_render_texture_unit(rs, unit).enabled || SoTextureCombineElement::isDefault(state, unit)) continue;
    SoTextureCombineElement::UnitData raw;
    SoTextureCombineElement::get(state, unit, raw.rgboperation, raw.alphaoperation,
      raw.rgbsource, raw.alphasource, raw.rgboperand, raw.alphaoperand,
      raw.constantcolor, raw.rgbscale, raw.alphascale);
    if (!coin_render_compile_combine(raw, rs.textureCombines[unit], this->builderError)) {
      this->isUnsupported = true;
      if (outError) *outError = this->builderError;
      return false;
    }
  }
  return true;
}

bool
CoinRenderFramePlanBuilder::captureTextureUnit(SoCallbackAction * action, int unit, CoinRenderRenderStateSnapshot & rs, std::string * outError)
{
  SoState * state = action ? action->getState() : nullptr;
  if (!state) {
    rs.hasTexture = false;
    rs.textureImageSlot = 0;
    rs.samplerSlot = 0;
    return true;
  }

  if (!SoMultiTextureEnabledElement::get(state, unit)) {
    rs.hasTexture = false;
    return true;
  }

  // Fetch this unit image
  SbVec2s imgSize;
  int numComponents = 0;
  SoMultiTextureImageElement::Wrap wrapS;
  SoMultiTextureImageElement::Wrap wrapT;
  SoMultiTextureImageElement::Model model;
  SbColor blendColor;
  const unsigned char * rawBytes = SoMultiTextureImageElement::get(state, unit, imgSize, numComponents, wrapS, wrapT, model, blendColor);

  if (!rawBytes || imgSize[0] <= 0 || imgSize[1] <= 0 || numComponents <= 0) {
    rs.hasTexture = false;
    rs.textureImageSlot = 0;
    rs.samplerSlot = 0;
    return true;
  }

  // Check for procedural/DEFAULT texture coordinates
  const SoMultiTextureCoordinateElement * tcElem = SoMultiTextureCoordinateElement::getInstance(state);
  if (tcElem) {
    auto ct = tcElem->getType(unit);
    if (ct == SoMultiTextureCoordinateElement::DEFAULT || ct == SoMultiTextureCoordinateElement::FUNCTION) {
      if (outError) *outError = "Procedural/DEFAULT texture coordinates are unsupported for texture unit " + std::to_string(unit);
      this->isUnsupported = true;
      this->builderError = "Procedural/DEFAULT texture coordinates are unsupported for texture unit " + std::to_string(unit);
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
  CoinRenderTextureWrap snapWrapS = CoinRenderTextureWrap::REPEAT;
  CoinRenderTextureWrap snapWrapT = CoinRenderTextureWrap::REPEAT;
  switch (wrapS) {
    case SoMultiTextureImageElement::REPEAT:
      snapWrapS = CoinRenderTextureWrap::REPEAT;
      break;
    case SoMultiTextureImageElement::CLAMP:
      snapWrapS = CoinRenderTextureWrap::CLAMP;
      break;
    default:
      if (outError) *outError = "Unsupported wrapS mode (only REPEAT and CLAMP supported in Subwave 3B)";
      this->isUnsupported = true;
      this->builderError = (outError ? *outError : "Unsupported wrapS mode");
      return false;
  }

  switch (wrapT) {
    case SoMultiTextureImageElement::REPEAT:
      snapWrapT = CoinRenderTextureWrap::REPEAT;
      break;
    case SoMultiTextureImageElement::CLAMP:
      snapWrapT = CoinRenderTextureWrap::CLAMP;
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
      rs.textureModel = CoinRenderTextureModel::MODULATE;
      break;
    case SoMultiTextureImageElement::REPLACE:
      rs.textureModel = CoinRenderTextureModel::REPLACE;
      break;
    case SoMultiTextureImageElement::DECAL:
      rs.textureModel = CoinRenderTextureModel::DECAL;
      break;
    case SoMultiTextureImageElement::BLEND:
      rs.textureModel = CoinRenderTextureModel::BLEND;
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
  const auto sceneTexture = this->sceneTextures.find(rawBytes);
  bool isSceneTexture = sceneTexture != this->sceneTextures.end() && imgSize[0] == 1 &&
                        imgSize[1] == 1 && numComponents == 4;
  if (isSceneTexture) {
    for (unsigned int i = 0; i < 4; ++i) {
      if (rawBytes[i] != static_cast<uint8_t>(sceneTexture->second.producerId >> (i * 8))) {
        isSceneTexture = false;
        break;
      }
    }
  }
  uint32_t w = isSceneTexture ? sceneTexture->second.width : static_cast<uint32_t>(imgSize[0]);
  uint32_t h = isSceneTexture ? sceneTexture->second.height : static_cast<uint32_t>(imgSize[1]);
  if (w > 8192 || h > 8192) {
    if (outError) *outError = "Texture dimensions exceed 8192";
    this->isUnsupported = true;
    this->builderError = (outError ? *outError : "Texture dimensions exceed 8192");
    return false;
  }

  std::vector<uint8_t> rgba;
  if (!isSceneTexture) {
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
  uint64_t digest =
      isSceneTexture ? sceneTexture->second.producerId : CoinRenderImageCore::rgba8Digest(rgba);
  uint32_t texSlot = UINT32_MAX;
  for (size_t i = 0; i < this->currentPlan.textures.size(); ++i) {
    const auto & t = this->currentPlan.textures[i];
    if (t.width == w && t.height == h && t.contentDigest == digest &&
        t.producerId == (isSceneTexture ? sceneTexture->second.producerId : 0) &&
        t.sceneTransparencyFunction ==
            (isSceneTexture ? sceneTexture->second.transparencyFunction : -1) &&
        t.pixelsRgba == rgba) {
      texSlot = static_cast<uint32_t>(i);
      break;
    }
  }
  if (texSlot == UINT32_MAX) {
    texSlot = static_cast<uint32_t>(this->currentPlan.textures.size());
    CoinRenderTextureImageSnapshot tSnap;
    tSnap.width = w;
    tSnap.height = h;
    tSnap.components = 4;
    tSnap.sceneTransparencyFunction =
        isSceneTexture ? sceneTexture->second.transparencyFunction : -1;
    tSnap.gpuOpaque = isSceneTexture && sceneTexture->second.opaque;
    tSnap.contentDigest = digest;
    tSnap.producerId = isSceneTexture ? sceneTexture->second.producerId : 0;
    tSnap.pixelsRgba = std::move(rgba);
    this->currentPlan.textures.push_back(std::move(tSnap));
  }

  // 9. Sampler deduplication
  CoinRenderSamplerSnapshot sampSnap;
  sampSnap.wrapS = snapWrapS;
  sampSnap.wrapT = snapWrapT;
  sampSnap.filter = CoinRenderTextureFilter::LINEAR;

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
  rs.textureMatrix = SoMultiTextureMatrixElement::get(state, unit);
  rs.hasTexture = true;
  rs.textureImageSlot = texSlot;
  rs.samplerSlot = sampSlot;

  return true;
}

uint32_t
CoinRenderFramePlanBuilder::captureRenderState(SoCallbackAction * action, int materialIndex)
{
  // 1. Material
  uint32_t materialSlot = this->captureMaterial(action, materialIndex);

  // 2. Lighting & CoinRenderLightModel
  CoinRenderLightModel lm = CoinRenderLightModel::PHONG;
  CoinRenderLightingSnapshot lightSnap;
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
      lm = CoinRenderLightModel::BASE_COLOR;
    }
  }

  if (lm == CoinRenderLightModel::PHONG && state) {
    const SoNodeList & lights = SoLightElement::getLights(state);
    for (int i = 0; i < lights.getLength(); ++i) {
      SoLight * l = static_cast<SoLight *>(lights[i]);
      if (l && l->on.getValue()) {
        CoinRenderLightSourceSnapshot src;
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
          src.type = CoinRenderLightType::DIRECTIONAL;
          SoDirectionalLight * dl = static_cast<SoDirectionalLight *>(l);
          SbVec3f dir;
          lm.multDirMatrix(dl->direction.getValue(), dir);
          dir.normalize();
          src.direction[0] = dir[0]; src.direction[1] = dir[1]; src.direction[2] = dir[2];
          src.position[0] = src.position[1] = src.position[2] = 0.0f;
        } else if (l->isOfType(SoPointLight::getClassTypeId())) {
          src.type = CoinRenderLightType::POINT;
          SoPointLight * pl = static_cast<SoPointLight *>(l);
          SbVec3f pos;
          lm.multVecMatrix(pl->location.getValue(), pos);
          src.position[0] = pos[0]; src.position[1] = pos[1]; src.position[2] = pos[2];
          src.direction[0] = src.direction[1] = src.direction[2] = 0.0f;
        } else if (l->isOfType(SoSpotLight::getClassTypeId())) {
          src.type = CoinRenderLightType::SPOT;
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
        if (lightSnap.lights.size() > COIN_RENDER_MAX_LIGHTS) {
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
        const CoinRenderLightSourceSnapshot & a = ls.lights[k];
        const CoinRenderLightSourceSnapshot & b = lightSnap.lights[k];
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
  CoinRenderCameraSnapshot camSnap;
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
  CoinRenderViewportSnapshot vpSnap;
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

  CoinRenderCullMode cullMode = CoinRenderCullMode::NONE;
  if (st == SoShapeHintsElement::SOLID && vo != SoShapeHintsElement::UNKNOWN_ORDERING) {
    cullMode = CoinRenderCullMode::BACK;
  }
  CoinRenderFrontFace frontFace = (vo == SoShapeHintsElement::CLOCKWISE) ? CoinRenderFrontFace::CW : CoinRenderFrontFace::CCW;

  CoinRenderRenderStateSnapshot rs;
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
  rs.depthFunction = static_cast<CoinRenderDepthFunction>(depthFunction);
  rs.screenDoorTransparency = std::max(0.0f, std::min(1.0f, SoLazyElement::getTransparency(state, 0)));
  if (state->isElementEnabled(CoinRenderDepthPolicyElement::getClassStackIndex()))
    rs.explicitDepthMask = CoinRenderDepthPolicyElement::get(state);
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
  rs.fogMode = static_cast<CoinRenderFogMode>(fogType);
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

  if (state) {
    const SoClipPlaneElement * planes = SoClipPlaneElement::getInstance(state);
    if (planes) for (int i = 0; i < planes->getNum(); ++i)
      rs.clipPlanesWorld.push_back(planes->get(i, TRUE));
  }
  if (rs.clipPlanesWorld.size() > COIN_RENDER_MAX_CLIP_PLANES) {
    this->isUnsupported = true;
    this->builderError = "UNSUPPORTED: more than eight active Coin clipping planes";
  }
  this->captureTexture(action, rs, &this->builderError);

  uint32_t rsSlot = 0;
  bool rsFound = false;
  for (size_t i = 0; i < this->currentPlan.renderStates.size(); ++i) {
    const auto & existing = this->currentPlan.renderStates[i];
    if (existing.clipPlanesWorld == rs.clipPlanesWorld &&
        existing.materialSlot == materialSlot &&
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
        std::memcmp(existing.extraTextures, rs.extraTextures, sizeof(rs.extraTextures)) == 0 &&
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
    rsSlot = static_cast<uint32_t>(this->currentPlan.renderStates.size());
    this->currentPlan.renderStates.push_back(rs);
  }
  return rsSlot;
}

CoinRenderVertexSnapshot
CoinRenderFramePlanBuilder::captureVertex(SoCallbackAction * action, const SoPrimitiveVertex * pv,
  uint32_t materialSlot, const CoinRenderRenderStateSnapshot & rs)
{
  CoinRenderVertexSnapshot v;
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


  SoState * state = action->getState();
  const auto * coords = SoMultiTextureCoordinateElement::getInstance(state);
  const auto * positions = SoCoordinateElement::getInstance(state);
  const SoDetail * detail = pv->getDetail();
  int texIndex = -1;
  auto matchPoint = [&](const SoPointDetail * point) {
    if (!point || texIndex >= 0) return;
    const int index = point->getCoordinateIndex();
    if (index >= 0 && index < positions->getNum() &&
        positions->get3(index).equals(pt, 1.0e-10f))
      texIndex = point->getTextureCoordIndex();
  };
  if (detail && detail->isOfType(SoPointDetail::getClassTypeId()))
    texIndex = static_cast<const SoPointDetail *>(detail)->getTextureCoordIndex();
  else if (detail && detail->isOfType(SoFaceDetail::getClassTypeId())) {
    const auto * face = static_cast<const SoFaceDetail *>(detail);
    for (int i = 0; i < face->getNumPoints(); ++i) matchPoint(face->getPoint(i));
  } else if (detail && detail->isOfType(SoLineDetail::getClassTypeId())) {
    const auto * line = static_cast<const SoLineDetail *>(detail);
    matchPoint(line->getPoint0()); matchPoint(line->getPoint1());
  }
  for (size_t unit = 1; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
    if (!rs.extraTextures[unit - 1].enabled) continue;
    if (texIndex < 0 || texIndex >= coords->getNum(unit)) {
      this->hasError = true;
      this->builderError = "Multitexture explicit coordinates require a valid primitive detail index: " +
        std::string(action->getCurPathTail()->getTypeId().getName().getString()) +
        " detail " + (detail ? detail->getTypeId().getName().getString() : "none") +
        " index " + std::to_string(texIndex) + " count " + std::to_string(coords->getNum(unit));
      continue;
    }
    const int dimension = coords->getDimension(unit);
    if (dimension == 2) {
      const SbVec2f & uv = coords->get2(unit, texIndex);
      v.extraTexcoords[unit - 1][0] = uv[0]; v.extraTexcoords[unit - 1][1] = uv[1];
    } else if (dimension == 3) {
      const SbVec3f & uv = coords->get3(unit, texIndex);
      v.extraTexcoords[unit - 1][0] = uv[0]; v.extraTexcoords[unit - 1][1] = uv[1];
    } else {
      const SbVec4f & uv = coords->get4(unit, texIndex);
      if (std::abs(uv[3]) <= 1.0e-8f) {
        this->isUnsupported = true; this->builderError = "Invalid homogeneous texture coordinate";
      } else {
        v.extraTexcoords[unit - 1][0] = uv[0] / uv[3];
        v.extraTexcoords[unit - 1][1] = uv[1] / uv[3];
      }
    }
  }

  return v;
}

uint32_t
CoinRenderFramePlanBuilder::addVertex(SoCallbackAction * action, const SoPrimitiveVertex * pv, uint32_t materialSlot)
{
  const auto & rs = this->currentPlan.renderStates[
    this->currentPlan.draws[this->currentDrawIndex].renderStateSlot];
  const CoinRenderVertexSnapshot vertex = this->captureVertex(action, pv, materialSlot, rs);
  const uint32_t index = static_cast<uint32_t>(this->currentPlan.vertices.size());
  this->currentPlan.vertices.push_back(vertex);
  return index;
}

void
CoinRenderFramePlanBuilder::ensureDrawPacket(CoinRenderPrimitiveTopology topology, uint32_t renderStateSlot, SoNode * node, bool forceNewPacket)
{
  SbUniqueId nodeId = node ? node->getNodeId() : 0;
  if (!forceNewPacket && this->hasActiveDraw) {
    const CoinRenderDrawPacket & active = this->currentPlan.draws[this->currentDrawIndex];
    if (active.topology == topology &&
        active.renderStateSlot == renderStateSlot &&
        active.renderLayer == this->currentAnnotationLayer &&
        active.sourceNodeId == nodeId) {
      return; // Continue active packet
    }
  }

  CoinRenderDrawPacket dp;
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
CoinRenderFramePlanBuilder::captureSortingCenter(SoCallbackAction * action)
{
  auto & draw = this->currentPlan.draws[this->currentDrawIndex];
  if (draw.geometry.indexCount != 0 || draw.renderLayer != 0) return;
  const auto & state = this->currentPlan.renderStates[draw.renderStateSlot];
  const int type = state.transparencyType;
  if (type != SoGLRenderAction::SORTED_OBJECT_ADD && type != SoGLRenderAction::SORTED_OBJECT_BLEND &&
      type != SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD &&
      type != SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND) return;
  SoNode * node = action->getCurPathTail();
  if (!node || !node->isOfType(SoShape::getClassTypeId())) return;
  SbBox3f box; SbVec3f center;
  static_cast<SoShape *>(node)->computeBBox(action, box, center);
  if (box.isEmpty()) return;
  action->getModelMatrix().multVecMatrix(center, center);
  center.getValue(draw.sortingCenterWorld[0], draw.sortingCenterWorld[1], draw.sortingCenterWorld[2]);
  draw.hasSortingCenter = true;
}

bool
CoinRenderFramePlanBuilder::isShapeInvisible(SoCallbackAction * action)
{
  return action && (SoShapeStyleElement::get(action->getState())->getFlags() &
                    SoShapeStyleElement::INVISIBLE) != 0;
}

int
CoinRenderFramePlanBuilder::polygonDrawStyle(SoCallbackAction * action)
{
  return SoDrawStyleElement::get(action->getState());
}

void
CoinRenderFramePlanBuilder::addTriangle(SoCallbackAction * action,
                                   const SoPrimitiveVertex * v0,
                                   const SoPrimitiveVertex * v1,
                                   const SoPrimitiveVertex * v2)
{
  if (!v0 || !v1 || !v2 || isShapeInvisible(action)) return;
  if (polygonDrawStyle(action) != SoDrawStyleElement::FILLED) {
    this->addStyledTriangle(action, v0, v1, v2);
    return;
  }
  uint32_t rsSlot = this->captureRenderState(action, v0->getMaterialIndex());
  this->ensureDrawPacket(CoinRenderPrimitiveTopology::TRIANGLE_LIST, rsSlot, action->getCurPathTail());
  this->captureSortingCenter(action);

  uint32_t m0 = this->captureMaterial(action, v0->getMaterialIndex());
  uint32_t m1 = this->captureMaterial(action, v1->getMaterialIndex());
  uint32_t m2 = this->captureMaterial(action, v2->getMaterialIndex());

  uint32_t i0 = this->addVertex(action, v0, m0);
  uint32_t i1 = this->addVertex(action, v1, m1);
  uint32_t i2 = this->addVertex(action, v2, m2);

  this->currentPlan.indices.push_back(i0);
  this->currentPlan.indices.push_back(i1);
  this->currentPlan.indices.push_back(i2);

  CoinRenderDrawPacket & dp = this->currentPlan.draws[this->currentDrawIndex];
  dp.geometry.vertexCount += 3;
  dp.geometry.indexCount += 3;
}

void CoinRenderFramePlanBuilder::addLine(SoCallbackAction* action, const SoPrimitiveVertex* v0,
                                         const SoPrimitiveVertex* v1) {
  if (!v0 || !v1 || isShapeInvisible(action))
    return;
  uint32_t rsSlot = this->captureRenderState(action, v0->getMaterialIndex());
  if (this->isUnsupported)
    return;
  SoNode* node = action->getCurPathTail();
  SoState* state = action->getState();
  const SoDetail* detail = v0->getDetail();
  const SoLineDetail* line = detail && detail->isOfType(SoLineDetail::getClassTypeId())
                                 ? static_cast<const SoLineDetail*>(detail)
                                 : nullptr;
  const int materialBinding = SoMaterialBindingElement::get(state);
  const int normalBinding = SoNormalBindingElement::get(state);
  // Coin's native line nodes choose GL_LINES for per-segment bindings and
  // GL_LINE_STRIP otherwise. Normals only influence GL assembly when used.
  const bool normalsUsed = SoLightModelElement::get(state) != SoLightModelElement::BASE_COLOR &&
                           SoNormalElement::getInstance(state)->getNum() > 0;
  const bool independent =
      materialBinding == SoMaterialBindingElement::PER_PART ||
      materialBinding == SoMaterialBindingElement::PER_PART_INDEXED ||
      (normalsUsed && (normalBinding == SoNormalBindingElement::PER_PART ||
                       normalBinding == SoNormalBindingElement::PER_PART_INDEXED));
  const bool knownStrip = line && !independent &&
                          this->currentPlan.renderStates[rsSlot].linePattern != 0xffffu &&
                          (node->getTypeId() == SoLineSet::getClassTypeId() ||
                           node->getTypeId() == SoIndexedLineSet::getClassTypeId());
  uint64_t strip = 0;
  if (knownStrip) {
    if (this->lineNode != node || this->lineIndex != line->getLineIndex() ||
        line->getPartIndex() == 0 || !this->hasActiveDraw ||
        this->currentPlan.draws[this->currentDrawIndex].topology !=
            CoinRenderPrimitiveTopology::LINE_LIST)
      this->lineStripId = ++this->nextLineStripId;
    strip = this->lineStripId;
  }
  this->lineNode = node;
  this->lineIndex = line ? line->getLineIndex() : -1;
  const bool newStrip =
      this->hasActiveDraw && this->currentPlan.draws[this->currentDrawIndex].lineStripId != strip;
  this->ensureDrawPacket(CoinRenderPrimitiveTopology::LINE_LIST, rsSlot, node, newStrip);
  this->currentPlan.draws[this->currentDrawIndex].lineStripId = strip;
  this->captureSortingCenter(action);

  uint32_t m0 = this->captureMaterial(action, v0->getMaterialIndex());
  uint32_t m1 = this->captureMaterial(action, v1->getMaterialIndex());

  uint32_t i0 = this->addVertex(action, v0, m0);
  uint32_t i1 = this->addVertex(action, v1, m1);

  this->currentPlan.indices.push_back(i0);
  this->currentPlan.indices.push_back(i1);

  CoinRenderDrawPacket& dp = this->currentPlan.draws[this->currentDrawIndex];
  dp.geometry.vertexCount += 2;
  dp.geometry.indexCount += 2;
}

void CoinRenderFramePlanBuilder::addPoint(SoCallbackAction* action,
                                          const SoPrimitiveVertex* vertex) {
  if (!vertex || isShapeInvisible(action))
    return;
  uint32_t rsSlot = this->captureRenderState(action, vertex->getMaterialIndex());
  if (this->isUnsupported)
    return;
  this->ensureDrawPacket(CoinRenderPrimitiveTopology::POINT_LIST, rsSlot, action->getCurPathTail());
  this->captureSortingCenter(action);

  uint32_t m0 = this->captureMaterial(action, vertex->getMaterialIndex());
  uint32_t i0 = this->addVertex(action, vertex, m0);

  this->currentPlan.indices.push_back(i0);

  CoinRenderDrawPacket& dp = this->currentPlan.draws[this->currentDrawIndex];
  dp.geometry.vertexCount += 1;
  dp.geometry.indexCount += 1;
}

void CoinRenderFramePlanBuilder::addStyledTriangle(SoCallbackAction* action,
                                                   const SoPrimitiveVertex* v0,
                                                   const SoPrimitiveVertex* v1,
                                                   const SoPrimitiveVertex* v2) {
  if (this->isUnsupported || this->hasError)
    return;
  SoNode* node = action->getCurPathTail();
  const SoPrimitiveVertex* vertices[3] = {v0, v1, v2};
  const SoDetail* detail = v0->getDetail();
  const SoFaceDetail* face = detail && detail->isOfType(SoFaceDetail::getClassTypeId())
                                 ? static_cast<const SoFaceDetail*>(detail)
                                 : nullptr;
  const bool cube = node->getTypeId() == SoCube::getClassTypeId();
  const bool cone = node->getTypeId() == SoCone::getClassTypeId();
  const bool cylinder = node->getTypeId() == SoCylinder::getClassTypeId();
  const bool sphere = node->getTypeId() == SoSphere::getClassTypeId();
  const auto* cylinderDetail =
      cylinder && detail && detail->isOfType(SoCylinderDetail::getClassTypeId())
          ? static_cast<const SoCylinderDetail*>(detail)
          : nullptr;
  bool quad = cube || (cylinderDetail && cylinderDetail->getPart() == SoCylinder::SIDES);
  if (sphere) {
    const float radius = static_cast<SoSphere*>(node)->radius.getValue();
    bool pole = false;
    for (const auto* vertex : vertices) {
      const auto& position = vertex->getPoint();
      pole = pole || (position[0] == 0 && position[2] == 0 &&
                      (position[1] == radius || position[1] == -radius));
    }
    // SoGenerate emits cap triangles and interior QUAD_STRIP, matching SoGL.
    quad = !pole;
  }
  auto reject = [&](const char* message) {
    this->isUnsupported = true;
    this->builderError = message;
  };
  if (!face && !cube && !cone && !sphere && !cylinderDetail) {
    reject("Polygon style requires a recoverable original contour for this shape");
    return;
  }
  if (this->polygonNode && this->polygonNode != node) {
    reject("Original polygon contour was interrupted by another shape");
    return;
  }
  if (!this->polygonNode) {
    this->polygonNode = node;
    this->polygonStyle = polygonDrawStyle(action);
    this->polygonState = this->captureRenderState(action, v0->getMaterialIndex());
    if (this->isUnsupported || this->hasError)
      return;
    const size_t count = face ? static_cast<size_t>(face->getNumPoints()) : (quad ? 4 : 3);
    if (count < 3) {
      reject("Invalid original polygon contour");
      return;
    }
    this->polygonTriangles = 0;
    this->polygonFaceIndex = face ? face->getFaceIndex() : -1;
    this->polygonPartIndex = face ? face->getPartIndex() : -1;
    this->polygonPositions.clear();
    this->polygonVertices.assign(count, CoinRenderVertexSnapshot{});
    this->polygonCaptured.assign(count, false);
    if (face) {
      const auto* coordinates = SoCoordinateElement::getInstance(action->getState());
      for (size_t i = 0; i < count; ++i) {
        const int index = face->getPoint(static_cast<int>(i))->getCoordinateIndex();
        if (index < 0 || index >= coordinates->getNum()) {
          reject("Invalid face contour coordinate index");
          return;
        }
        const SbVec3f position = coordinates->get3(index);
        for (const auto& previous : this->polygonPositions)
          if (previous == position) {
            reject("Ambiguous repeated coordinate in polygon contour");
            return;
          }
        this->polygonPositions.push_back(position);
      }
    } else {
      for (int i = 0; i < 3; ++i)
        this->polygonPositions.push_back(vertices[i]->getPoint());
      if (quad)
        this->polygonPositions.push_back(SbVec3f(0, 0, 0)); // Filled by the second callback.
    }
  }
  if (face && this->polygonTriangles > 0) {
    if (face->getNumPoints() != static_cast<int>(this->polygonPositions.size()) ||
        face->getFaceIndex() != this->polygonFaceIndex ||
        face->getPartIndex() != this->polygonPartIndex) {
      reject("Face detail changed within an original polygon");
      return;
    }
    const auto* coordinates = SoCoordinateElement::getInstance(action->getState());
    for (size_t i = 0; i < this->polygonPositions.size(); ++i) {
      const int index = face->getPoint(static_cast<int>(i))->getCoordinateIndex();
      if (index < 0 || index >= coordinates->getNum() ||
          coordinates->get3(index) != this->polygonPositions[i]) {
        reject("Face contour changed within its triangle callbacks");
        return;
      }
    }
  }
  if (this->polygonStyle != polygonDrawStyle(action)) {
    reject("Draw style changed within an original polygon");
    return;
  }
  if (quad && this->polygonTriangles == 1) {
    if (v0->getPoint() != this->polygonPositions[0] ||
        v1->getPoint() != this->polygonPositions[2]) {
      reject("Procedural callbacks do not match the original quad assembly");
      return;
    }
    this->polygonPositions[3] = v2->getPoint();
  }
  const auto state = this->currentPlan.renderStates[this->polygonState];
  for (int i = 0; i < 3; ++i) {
    size_t slot = this->polygonPositions.size();
    // The quad's not-yet-captured fourth corner is intentionally excluded.
    const size_t available =
        quad && this->polygonTriangles == 0 ? 3 : this->polygonPositions.size();
    for (size_t j = 0; j < available; ++j)
      if (vertices[i]->getPoint() == this->polygonPositions[j]) {
        slot = j;
        break;
      }
    if (slot == this->polygonPositions.size()) {
      reject("Triangle callback lost the original face contour");
      return;
    }
    const uint32_t material = this->captureMaterial(action, vertices[i]->getMaterialIndex());
    const auto captured = this->captureVertex(action, vertices[i], material, state);
    if (!this->polygonCaptured[slot]) {
      this->polygonVertices[slot] = captured;
      this->polygonCaptured[slot] = true;
    } else {
      const auto& previous = this->polygonVertices[slot];
      if (previous.materialSlot != captured.materialSlot ||
          SbVec3f(previous.normal) != SbVec3f(captured.normal) ||
          SbVec2f(previous.texcoord) != SbVec2f(captured.texcoord) ||
          std::memcmp(previous.extraTexcoords, captured.extraTexcoords,
                      sizeof(previous.extraTexcoords)) != 0) {
        reject("Inconsistent attributes across triangles of one polygon");
        return;
      }
    }
  }
  ++this->polygonTriangles;
  if (this->polygonTriangles == static_cast<int>(this->polygonVertices.size()) - 2) {
    for (bool captured : this->polygonCaptured)
      if (!captured) {
        reject("Original face contour has uncaptured vertices");
        return;
      }
    this->emitStyledPolygon(action);
    this->polygonNode = nullptr;
    this->polygonPositions.clear();
    this->polygonVertices.clear();
    this->polygonCaptured.clear();
  }
}

void CoinRenderFramePlanBuilder::emitStyledPolygon(SoCallbackAction* action) {
  const auto sourceState = this->currentPlan.renderStates[this->polygonState];
  CoinRenderPolygonStyleResult resolved;
  const auto style = this->polygonStyle == SoDrawStyleElement::LINES
                         ? CoinRenderPolygonStyle::LINES
                         : CoinRenderPolygonStyle::POINTS;
  std::string diagnostic;
  if (!coin_render_prepare_polygon_style(this->polygonVertices, sourceState,
                                         this->currentPlan.materials,
                                         this->currentPlan.lightingStates[sourceState.lightingSlot],
                                         style, resolved, diagnostic,
                                         this->currentPlan.viewports[sourceState.viewportSlot])) {
    if (diagnostic.compare(0, 12, "UNSUPPORTED:") == 0)
      this->isUnsupported = true;
    else
      this->hasError = true;
    this->builderError = diagnostic;
    return;
  }
  if (resolved.vertices.empty())
    return;
  const uint32_t stateSlot = static_cast<uint32_t>(this->currentPlan.renderStates.size());
  this->currentPlan.renderStates.push_back(resolved.state);
  this->ensureDrawPacket(resolved.topology, stateSlot, this->polygonNode, true);
  this->captureSortingCenter(action);
  const uint32_t first = static_cast<uint32_t>(this->currentPlan.vertices.size());
  for (auto& item : resolved.vertices) {
    uint32_t material = static_cast<uint32_t>(this->currentPlan.materials.size());
    for (size_t i = 0; i < this->currentPlan.materials.size(); ++i)
      if (std::memcmp(&item.material, &this->currentPlan.materials[i], sizeof(item.material)) ==
          0) {
        material = static_cast<uint32_t>(i);
        break;
      }
    if (material == this->currentPlan.materials.size())
      this->currentPlan.materials.push_back(item.material);
    item.vertex.materialSlot = material;
    this->currentPlan.vertices.push_back(item.vertex);
  }
  for (uint32_t index : resolved.indices)
    this->currentPlan.indices.push_back(first + index);
  auto& draw = this->currentPlan.draws[this->currentDrawIndex];
  draw.geometry.vertexCount = static_cast<uint32_t>(resolved.vertices.size());
  draw.geometry.indexCount = static_cast<uint32_t>(resolved.indices.size());
}
bool
CoinRenderFramePlanBuilder::expandStyledPrimitives(std::string * outError)
{
  if (!coin_render_expand_strokes(this->currentPlan, this->builderError)) {
    this->hasError = true;
    if (outError) *outError = this->builderError;
    return false;
  }
  this->hasActiveDraw = false;
  return true;
}


bool
CoinRenderFramePlanBuilder::build(CoinRenderFramePlan & outPlan, std::string * outError)
{
  if (this->polygonNode && !this->isUnsupported && !this->hasError) {
    this->isUnsupported = true;
    this->builderError = "Incomplete original polygon contour in primitive callbacks";
  }
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
  std::vector<CoinRenderCompositionItem> order;
  std::string compositionError;
  if (!coin_render_composition_order(this->currentPlan, order, compositionError)) {
    this->isUnsupported = true;
    this->builderError = compositionError;
    if (outError) *outError = compositionError;
    return false;
  }
  outPlan = this->currentPlan;
  outPlan.revision = CoinRenderFramePlanBuilder::nextRevision();
  return true;
}

uint64_t
CoinRenderFramePlanBuilder::nextRevision()
{
  static std::atomic<uint64_t> revision(1);
  uint64_t value = revision.fetch_add(1, std::memory_order_relaxed);
  if (value == 0) value = revision.fetch_add(1, std::memory_order_relaxed);
  return value;
}

namespace {

CoinRenderIndexedGeometryOptions
captureIndexedGeometryOptions(SoCallbackAction * action, SoNode * node)
{
  CoinRenderIndexedGeometryOptions options;
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
publishCoreDiagnostic(const CoinRenderIndexedGeometryResult & result,
                      std::string * outError)
{
  if (outError && !result.diagnostic.empty()) {
    *outError = result.diagnostic;
  }
}

} // namespace

CoinRenderFastPathResult
CoinRenderFramePlanBuilder::processIndexedFaceSet(
  SoCallbackAction * action,
  const CoinRenderDirectGeometryView & view,
  SoNode * node,
  std::string * outError)
{
  if (!action) {
    if (outError) *outError = "Null SoCallbackAction in processIndexedFaceSet";
    return CoinRenderFastPathResult::INVALID_SCENE;
  }
  if (isShapeInvisible(action)) return CoinRenderFastPathResult::SUCCESS_PRUNE;
  // The callback path carries original face details; direct triangulation loses them.
  if (polygonDrawStyle(action) != SoDrawStyleElement::FILLED)
    return CoinRenderFastPathResult::FALLBACK_CONTINUE;
  int lastTextureUnit = -1;
  SoMultiTextureEnabledElement::getEnabledUnits(action->getState(), lastTextureUnit);
  if (lastTextureUnit > 0) return CoinRenderFastPathResult::FALLBACK_CONTINUE;
  if (view.positions.empty() || view.coordIndex.empty()) {
    return CoinRenderFastPathResult::SUCCESS_PRUNE;
  }
  if (this->isUnsupported) {
    if (outError) {
      *outError = this->builderError.empty()
        ? "Unsupported feature in IndexedFaceSet"
        : this->builderError;
    }
    return CoinRenderFastPathResult::UNSUPPORTED;
  }

  const CoinRenderIndexedGeometryOptions options =
    captureIndexedGeometryOptions(action, node);
  CoinRenderIndexedGeometryResult transformed =
    CoinRenderIndexedGeometryCore::buildFaces(view, options);
  publishCoreDiagnostic(transformed, outError);
  if (transformed.status != CoinRenderFastPathResult::SUCCESS_PRUNE ||
      transformed.indices.empty()) {
    return transformed.status;
  }

  // Material lookup and the atomic CoinRenderFramePlan commit remain in Wiring.
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
    return CoinRenderFastPathResult::UNSUPPORTED;
  }
  this->ensureDrawPacket(
    CoinRenderPrimitiveTopology::TRIANGLE_LIST, renderStateSlot, node, true);
  this->captureSortingCenter(action);

  const uint32_t vertexOffset =
    static_cast<uint32_t>(this->currentPlan.vertices.size());
  for (size_t i = 0; i < transformed.vertices.size(); ++i) {
    this->currentPlan.vertices.push_back(transformed.vertices[i].vertex);
  }
  for (size_t i = 0; i < transformed.indices.size(); ++i) {
    this->currentPlan.indices.push_back(
      vertexOffset + transformed.indices[i]);
  }

  CoinRenderDrawPacket & packet = this->currentPlan.draws[this->currentDrawIndex];
  packet.geometry.vertexCount =
    static_cast<uint32_t>(transformed.vertices.size());
  packet.geometry.indexCount =
    static_cast<uint32_t>(transformed.indices.size());
  const uint64_t stableId = reinterpret_cast<uint64_t>(node);
  packet.stableNodeId = stableId;
  packet.drawOrdinal = this->nodeOccurrenceCount[stableId]++;
  packet.sourceRevision = CoinRenderIndexedGeometryCore::payloadDigest(
    transformed.vertices, transformed.indices);
  return CoinRenderFastPathResult::SUCCESS_PRUNE;
}

CoinRenderFastPathResult
CoinRenderFramePlanBuilder::processIndexedLineSet(
  SoCallbackAction * action,
  const CoinRenderDirectGeometryView & view,
  SoNode * node,
  std::string * outError)
{
  if (!action) {
    if (outError) *outError = "Null SoCallbackAction in processIndexedLineSet";
    return CoinRenderFastPathResult::INVALID_SCENE;
  }
  if (isShapeInvisible(action)) return CoinRenderFastPathResult::SUCCESS_PRUNE;
  int lastTextureUnit = -1;
  SoMultiTextureEnabledElement::getEnabledUnits(action->getState(), lastTextureUnit);
  if (lastTextureUnit >= 0) return CoinRenderFastPathResult::FALLBACK_CONTINUE;
  if (view.positions.empty() || view.coordIndex.empty()) {
    return CoinRenderFastPathResult::SUCCESS_PRUNE;
  }
  if (this->isUnsupported) {
    if (outError) {
      *outError = this->builderError.empty()
        ? "Unsupported feature in IndexedLineSet"
        : this->builderError;
    }
    return CoinRenderFastPathResult::UNSUPPORTED;
  }

  const CoinRenderIndexedGeometryOptions options =
    captureIndexedGeometryOptions(action, node);
  if ((SoLinePatternElement::get(action->getState()) & 0xffffu) != 0xffffu)
    return CoinRenderFastPathResult::FALLBACK_CONTINUE;
  CoinRenderIndexedGeometryResult transformed =
    CoinRenderIndexedGeometryCore::buildLines(view, options);
  publishCoreDiagnostic(transformed, outError);
  if (transformed.status != CoinRenderFastPathResult::SUCCESS_PRUNE ||
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
    return CoinRenderFastPathResult::UNSUPPORTED;
  }
  this->ensureDrawPacket(
    CoinRenderPrimitiveTopology::LINE_LIST, renderStateSlot, node, true);
  this->captureSortingCenter(action);

  const uint32_t vertexOffset =
    static_cast<uint32_t>(this->currentPlan.vertices.size());
  for (size_t i = 0; i < transformed.vertices.size(); ++i) {
    this->currentPlan.vertices.push_back(transformed.vertices[i].vertex);
  }
  for (size_t i = 0; i < transformed.indices.size(); ++i) {
    this->currentPlan.indices.push_back(
      vertexOffset + transformed.indices[i]);
  }

  CoinRenderDrawPacket & packet = this->currentPlan.draws[this->currentDrawIndex];
  packet.geometry.vertexCount =
    static_cast<uint32_t>(transformed.vertices.size());
  packet.geometry.indexCount =
    static_cast<uint32_t>(transformed.indices.size());
  const uint64_t stableId = reinterpret_cast<uint64_t>(node);
  packet.stableNodeId = stableId;
  packet.drawOrdinal = this->nodeOccurrenceCount[stableId]++;
  packet.sourceRevision = CoinRenderIndexedGeometryCore::payloadDigest(
    transformed.vertices, transformed.indices);
  return CoinRenderFastPathResult::SUCCESS_PRUNE;
}
