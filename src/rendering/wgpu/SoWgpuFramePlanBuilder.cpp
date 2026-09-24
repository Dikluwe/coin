#include <iostream>
#include "rendering/wgpu/SoWgpuFramePlanBuilder.h"
#include "rendering/wgpu/SoWgpuComposition.h"

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
#include <Inventor/elements/SoShapeStyleElement.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoMultiTextureImageElement.h>
#include <Inventor/elements/SoMultiTextureMatrixElement.h>
#include <Inventor/elements/SoTextureQualityElement.h>
#include <Inventor/elements/SoTextureUnitElement.h>
#include <Inventor/elements/SoMultiTextureCoordinateElement.h>
#include <Inventor/elements/SoTextureCoordinateBindingElement.h>

#include <cassert>
#include <cmath>

SoWgpuFramePlanBuilder::SoWgpuFramePlanBuilder()
  : currentDrawIndex(0),
    nodeCounter(0),
    inFrame(false),
    hasActiveDraw(false),
    hasError(false),
    isUnsupported(false)
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
  this->builderError.clear();
  this->directTextures.clear();
  this->nodeOccurrenceCount.clear();
  this->lightAttenuationByIndex.clear();
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
    case SoMultiTextureImageElement::DECAL:
    case SoMultiTextureImageElement::BLEND:
    default:
      if (outError) *outError = "Unsupported texture model (only MODULATE supported in Subwave 3B)";
      this->isUnsupported = true;
      this->builderError = (outError ? *outError : "Unsupported texture model");
      return false;
  }

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
        existing.fogMode == rs.fogMode &&
        existing.fogStart == rs.fogStart &&
        existing.fogEnd == rs.fogEnd &&
        std::memcmp(existing.fogColor, rs.fogColor, sizeof(rs.fogColor)) == 0 &&
        existing.lineWidth == rs.lineWidth &&
        existing.pointSize == rs.pointSize &&
        existing.hasTexture == rs.hasTexture &&
        (!rs.hasTexture || (
          existing.textureImageSlot == rs.textureImageSlot &&
          existing.samplerSlot == rs.samplerSlot &&
          existing.textureModel == rs.textureModel &&
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
        active.sourceNodeId == nodeId) {
      return; // Continue active packet
    }
  }

  DrawPacket dp;
  dp.topology = topology;
  dp.renderStateSlot = renderStateSlot;
  dp.frameNodeOrdinal = ++this->nodeCounter;
  dp.sourceNodeId = nodeId;
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
SoWgpuFramePlanBuilder::build(FramePlan & outPlan, std::string * outError)
{
  if (this->hasError || this->isUnsupported) {
    if (outError) *outError = this->builderError.empty() ? "Builder encountered unsupported or invalid feature" : this->builderError;
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
  return true;
}

#include <map>

FastPathResult
SoWgpuFramePlanBuilder::processIndexedFaceSet(SoCallbackAction * action,
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
    if (outError) *outError = this->builderError.empty() ? "Unsupported feature in IndexedFaceSet" : this->builderError;
    return FastPathResult::UNSUPPORTED;
  }

  SoState * state = action->getState();
  if (state) {
    SbVec2s sz;
    int nc = 0;
    const unsigned char * bytes = SoMultiTextureImageElement::getImage(state, 0, sz, nc);
    bool hasTex = (bytes != nullptr && sz[0] > 0 && sz[1] > 0 && nc > 0);
    if (hasTex) {
      const SoMultiTextureCoordinateElement * tcElem = SoMultiTextureCoordinateElement::getInstance(state);
      if (tcElem) {
        auto ct = tcElem->getType(0);
        if (ct == SoMultiTextureCoordinateElement::DEFAULT || ct == SoMultiTextureCoordinateElement::FUNCTION) {
          if (outError) *outError = "Procedural/DEFAULT texture coordinates are not supported in Subwave 3B";
          return FastPathResult::UNSUPPORTED;
        }
      }
      if (view.texcoords.empty()) {
        if (outError) *outError = "Explicit texture coordinates missing for textured IndexedFaceSet";
        return FastPathResult::UNSUPPORTED;
      }
    }
  }

  // Preflight textureCoordIndex
  if (!view.texCoordIndex.empty()) {
    for (size_t ti = 0; ti < view.texCoordIndex.size; ++ti) {
      int32_t val = view.texCoordIndex[ti];
      if (val < -1) {
        if (outError) *outError = "IndexedFaceSet textureCoordIndex contains invalid negative index < -1";
        return FastPathResult::INVALID_SCENE;
      }
      if (!view.texcoords.empty() && val >= 0 && static_cast<size_t>(val) >= view.texcoords.size) {
        if (outError) *outError = "IndexedFaceSet textureCoordIndex references out-of-bounds texture coordinate";
        return FastPathResult::INVALID_SCENE;
      }
    }
  }

  const size_t numPositions = view.positions.size;
  const size_t numIndices = view.coordIndex.size;

  // 1. Preflight of positions: verify finite values
  for (size_t i = 0; i < numPositions; ++i) {
    const SbVec3f & p = view.positions[i];
    if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) {
      if (outError) *outError = "IndexedFaceSet vertex position contains NaN or Inf";
      return FastPathResult::INVALID_SCENE;
    }
  }

  // 2. Parse faces and check indices
  struct FaceInfo {
    size_t startIndex;
    size_t count;
  };
  std::vector<FaceInfo> faces;

  size_t currentStart = 0;
  size_t currentCount = 0;
  for (size_t i = 0; i < numIndices; ++i) {
    int32_t idx = view.coordIndex[i];
    if (idx < -1) {
      if (outError) *outError = "IndexedFaceSet contains invalid negative coordinate index < -1";
      return FastPathResult::INVALID_SCENE;
    }
    if (idx == -1) {
      if (currentCount > 0) {
        faces.push_back(FaceInfo{currentStart, currentCount});
      }
      currentStart = i + 1;
      currentCount = 0;
    } else {
      if (static_cast<size_t>(idx) >= numPositions) {
        if (outError) *outError = "IndexedFaceSet coordinate index out of bounds";
        return FastPathResult::INVALID_SCENE;
      }
      currentCount++;
    }
  }
  if (currentCount > 0) {
    faces.push_back(FaceInfo{currentStart, currentCount});
  }

  if (faces.empty()) {
    return FastPathResult::SUCCESS_PRUNE;
  }

  // 3. Topology & convexity check
  for (size_t f = 0; f < faces.size(); ++f) {
    size_t count = faces[f].count;
    if (count < 3) {
      continue; // Degenerate face ignored
    }
    if (count > 4) {
      // General N-gons require robust tessellation from Coin's fallback cache
      return FastPathResult::FALLBACK_CONTINUE;
    }
    if (count == 4) {
      size_t s = faces[f].startIndex;
      int32_t i0 = view.coordIndex[s];
      int32_t i1 = view.coordIndex[s + 1];
      int32_t i2 = view.coordIndex[s + 2];
      int32_t i3 = view.coordIndex[s + 3];
      if (!SoWgpuFastPathValidator::isQuadConvex(view.positions[i0],
                                                 view.positions[i1],
                                                 view.positions[i2],
                                                 view.positions[i3])) {
        // Concave or twisted quad: divert to fallback
        return FastPathResult::FALLBACK_CONTINUE;
      }
    }
  }

  // 4. Validate materialIndex preflight
  if (!view.materialIndex.empty()) {
    int32_t numDiff = 0;
    if (action && action->getState()) {
      const SoLazyElement * lazy = SoLazyElement::getInstance(action->getState());
      if (lazy) numDiff = lazy->getNumDiffuse();
    }
    for (size_t mi = 0; mi < view.materialIndex.size; ++mi) {
      int32_t val = view.materialIndex[mi];
      if (val < -1) {
        if (outError) *outError = "IndexedFaceSet contains invalid negative material index < -1";
        return FastPathResult::INVALID_SCENE;
      }
      if (numDiff > 0 && val >= numDiff) {
        if (outError) *outError = "IndexedFaceSet materialIndex references out-of-bounds material";
        return FastPathResult::INVALID_SCENE;
      }
    }
  }

  // 5. Staging validation and attribute resolution
  std::vector<VertexSnapshot> stagingVertices;
  std::vector<uint32_t> stagingIndices;
  std::map<VertexDeduplicationKey, uint32_t> uniqueVertexMap;

  uint32_t defaultMatSlot = this->captureMaterial(action, 0);
  size_t matVertexCounter = 0;

  auto getOrAddVertex = [&](int32_t coordIdx, int32_t normalIdx, int32_t texIdx, int32_t matIdx,
                            const SbVec3f & pos, const SbVec3f & norm, const SbVec2f & tc, uint32_t matSlot) -> uint32_t {
    VertexDeduplicationKey key;
    key.coordIdx = coordIdx;
    key.normalIdx = normalIdx;
    key.texCoordIdx = texIdx;
    key.materialIdx = matIdx;

    auto it = uniqueVertexMap.find(key);
    if (it != uniqueVertexMap.end()) {
      return it->second;
    }

    VertexSnapshot vs;
    vs.position[0] = pos[0]; vs.position[1] = pos[1]; vs.position[2] = pos[2];
    vs.normal[0] = norm[0]; vs.normal[1] = norm[1]; vs.normal[2] = norm[2];
    vs.texcoord[0] = tc[0]; vs.texcoord[1] = tc[1];
    vs.materialSlot = matSlot;

    uint32_t newIdx = static_cast<uint32_t>(stagingVertices.size());
    stagingVertices.push_back(vs);
    uniqueVertexMap[key] = newIdx;
    return newIdx;
  };

  size_t validFaceIdx = 0;
  for (size_t f = 0; f < faces.size(); ++f) {
    size_t count = faces[f].count;
    if (count < 3) continue;

    size_t s = faces[f].startIndex;

    // Resolve face normal
    SbVec3f faceNormal(0.0f, 0.0f, 1.0f);
    if (view.normalBinding == SoNormalBindingElement::OVERALL && !view.normals.empty()) {
      faceNormal = view.normals[0];
    } else if ((view.normalBinding == SoNormalBindingElement::PER_FACE ||
                view.normalBinding == SoNormalBindingElement::PER_PART) &&
               !view.normals.empty()) {
      size_t nIdx = (view.normalIndex.empty()) ? validFaceIdx : static_cast<size_t>(view.normalIndex[validFaceIdx]);
      if (nIdx < view.normals.size) {
        faceNormal = view.normals[nIdx];
      }
    } else if (view.normals.empty()) {
      // Calculate geometric face normal
      int32_t c0 = view.coordIndex[s];
      int32_t c1 = view.coordIndex[s + 1];
      int32_t c2 = view.coordIndex[s + 2];
      SbVec3f fn = (view.positions[c1] - view.positions[c0]).cross(view.positions[c2] - view.positions[c0]);
      if (fn.sqrLength() > 1e-10f) {
        fn.normalize();
        faceNormal = fn;
      }
    }

    // Resolve face material
    uint32_t faceMatSlot = defaultMatSlot;
    int32_t faceMatIdx = static_cast<int32_t>(validFaceIdx);
    if (view.materialBinding == SoMaterialBindingElement::PER_FACE ||
        view.materialBinding == SoMaterialBindingElement::PER_PART) {
      // Non-indexed: use validFaceIdx directly without checking materialIndex
      faceMatSlot = this->captureMaterial(action, static_cast<int>(validFaceIdx));
    } else if (view.materialBinding == SoMaterialBindingElement::PER_FACE_INDEXED ||
               view.materialBinding == SoMaterialBindingElement::PER_PART_INDEXED) {
      // Indexed: use materialIndex if present, otherwise fallback to validFaceIdx
      if (!view.materialIndex.empty() && validFaceIdx < view.materialIndex.size) {
        int32_t mi = view.materialIndex[validFaceIdx];
        if (mi >= 0) faceMatIdx = mi;
      }
      faceMatSlot = this->captureMaterial(action, faceMatIdx);
    }

    auto resolveVertex = [&](size_t vertOffsetInFace) -> uint32_t {
      size_t indexInCoordIndex = s + vertOffsetInFace;
      int32_t cIdx = view.coordIndex[indexInCoordIndex];
      const SbVec3f & pos = view.positions[cIdx];

      // Normal
      SbVec3f norm = faceNormal;
      int32_t nKey = 0;
      if (view.normalBinding == SoNormalBindingElement::PER_FACE ||
          view.normalBinding == SoNormalBindingElement::PER_FACE_INDEXED ||
          view.normalBinding == SoNormalBindingElement::PER_PART ||
          view.normalBinding == SoNormalBindingElement::PER_PART_INDEXED) {
        nKey = static_cast<int32_t>(validFaceIdx);
      } else if (view.normalBinding == SoNormalBindingElement::PER_VERTEX ||
                 view.normalBinding == SoNormalBindingElement::PER_VERTEX_INDEXED) {
        nKey = cIdx;
        if (!view.normals.empty()) {
          size_t nIdx = cIdx;
          if (!view.normalIndex.empty() && indexInCoordIndex < view.normalIndex.size) {
            int32_t ni = view.normalIndex[indexInCoordIndex];
            if (ni >= 0 && static_cast<size_t>(ni) < view.normals.size) {
              nIdx = static_cast<size_t>(ni);
            }
          }
          if (nIdx < view.normals.size) {
            norm = view.normals[nIdx];
            nKey = static_cast<int32_t>(nIdx);
          }
        }
      }

      // Material
      uint32_t matSlot = faceMatSlot;
      int32_t mKey = 0;
      if (view.materialBinding == SoMaterialBindingElement::OVERALL) {
        mKey = 0;
        matSlot = defaultMatSlot;
      } else if (view.materialBinding == SoMaterialBindingElement::PER_FACE ||
                 view.materialBinding == SoMaterialBindingElement::PER_PART) {
        mKey = static_cast<int32_t>(validFaceIdx);
        matSlot = faceMatSlot;
      } else if (view.materialBinding == SoMaterialBindingElement::PER_FACE_INDEXED ||
                 view.materialBinding == SoMaterialBindingElement::PER_PART_INDEXED) {
        mKey = faceMatIdx;
        matSlot = faceMatSlot;
      } else if (view.materialBinding == SoMaterialBindingElement::PER_VERTEX) {
        mKey = static_cast<int32_t>(matVertexCounter++);
        matSlot = this->captureMaterial(action, mKey);
      } else if (view.materialBinding == SoMaterialBindingElement::PER_VERTEX_INDEXED) {
        int mIdx = cIdx;
        if (!view.materialIndex.empty() && indexInCoordIndex < view.materialIndex.size) {
          int32_t mi = view.materialIndex[indexInCoordIndex];
          if (mi >= 0) mIdx = mi;
        }
        mKey = mIdx;
        matSlot = this->captureMaterial(action, mIdx);
      }

      // TexCoord
      SbVec2f tc(0.0f, 0.0f);
      int32_t tKey = 0;
      if (!view.texcoords.empty()) {
        size_t tIdx = cIdx;
        if (!view.texCoordIndex.empty() && indexInCoordIndex < view.texCoordIndex.size) {
          int32_t ti = view.texCoordIndex[indexInCoordIndex];
          if (ti >= 0 && static_cast<size_t>(ti) < view.texcoords.size) {
            tIdx = static_cast<size_t>(ti);
          }
        }
        if (tIdx < view.texcoords.size) {
          tc = view.texcoords[tIdx];
          tKey = static_cast<int32_t>(tIdx);
        }
      }

      return getOrAddVertex(cIdx, nKey, tKey, mKey, pos, norm, tc, matSlot);
    };

    if (count == 3) {
      uint32_t v0 = resolveVertex(0);
      uint32_t v1 = resolveVertex(1);
      uint32_t v2 = resolveVertex(2);
      stagingIndices.push_back(v0);
      stagingIndices.push_back(v1);
      stagingIndices.push_back(v2);
    } else if (count == 4) {
      uint32_t v0 = resolveVertex(0);
      uint32_t v1 = resolveVertex(1);
      uint32_t v2 = resolveVertex(2);
      uint32_t v3 = resolveVertex(3);
      // Triangle 1: (0, 1, 2)
      stagingIndices.push_back(v0);
      stagingIndices.push_back(v1);
      stagingIndices.push_back(v2);
      // Triangle 2: (0, 2, 3)
      stagingIndices.push_back(v0);
      stagingIndices.push_back(v2);
      stagingIndices.push_back(v3);
    }

    validFaceIdx++;
  }

  if (stagingIndices.empty()) {
    return FastPathResult::SUCCESS_PRUNE;
  }

  // 5. Atomic commit phase (force dedicated packet per fast-path occurrence)
  uint32_t rsSlot = this->captureRenderState(action, 0);
  if (this->isUnsupported) {
    if (outError) *outError = this->builderError.empty() ? "Unsupported feature in IndexedFaceSet" : this->builderError;
    return FastPathResult::UNSUPPORTED;
  }
  this->ensureDrawPacket(PrimitiveTopology::TRIANGLE_LIST, rsSlot, node, /*forceNewPacket=*/true);

  uint32_t vertexOffset = static_cast<uint32_t>(this->currentPlan.vertices.size());
  for (size_t i = 0; i < stagingVertices.size(); ++i) {
    this->currentPlan.vertices.push_back(stagingVertices[i]);
  }
  for (size_t i = 0; i < stagingIndices.size(); ++i) {
    this->currentPlan.indices.push_back(vertexOffset + stagingIndices[i]);
  }

  DrawPacket & dp = this->currentPlan.draws[this->currentDrawIndex];
  dp.geometry.vertexCount = static_cast<uint32_t>(stagingVertices.size());
  dp.geometry.indexCount = static_cast<uint32_t>(stagingIndices.size());

  // Canonical serialization hash matching ABI CoinWgpuVertex
  uint64_t h = 14695981039346656037ULL;
  auto hashBytes = [&](const void * data, size_t len) {
    const uint8_t * b = static_cast<const uint8_t *>(data);
    for (size_t k = 0; k < len; ++k) {
      h ^= static_cast<uint64_t>(b[k]);
      h *= 1099511628211ULL;
    }
  };
  for (const auto & v : stagingVertices) {
    hashBytes(v.position, sizeof(v.position));
    hashBytes(v.normal, sizeof(v.normal));
    hashBytes(v.texcoord, sizeof(v.texcoord));
    hashBytes(&v.materialSlot, sizeof(v.materialSlot));
  }
  if (!stagingIndices.empty()) {
    hashBytes(stagingIndices.data(), stagingIndices.size() * sizeof(uint32_t));
  }
  if (h == 0) h = 1;

  uint64_t stableId = reinterpret_cast<uint64_t>(node);
  dp.stableNodeId = stableId;
  dp.drawOrdinal = this->nodeOccurrenceCount[stableId]++;
  dp.sourceRevision = h;

  return FastPathResult::SUCCESS_PRUNE;
}

FastPathResult
SoWgpuFramePlanBuilder::processIndexedLineSet(SoCallbackAction * action,
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
    if (outError) *outError = this->builderError.empty() ? "Unsupported feature in IndexedLineSet" : this->builderError;
    return FastPathResult::UNSUPPORTED;
  }

  SoState * state = action->getState();
  if (state) {
    SbVec2s sz;
    int nc = 0;
    const unsigned char * bytes = SoMultiTextureImageElement::getImage(state, 0, sz, nc);
    if (bytes != nullptr && sz[0] > 0 && sz[1] > 0 && nc > 0) {
      if (outError) *outError = "Textured lines are not supported in Subwave 3B";
      return FastPathResult::UNSUPPORTED;
    }
  }

  const size_t numPositions = view.positions.size;
  const size_t numIndices = view.coordIndex.size;

  // 1. Verify finite coordinates
  for (size_t i = 0; i < numPositions; ++i) {
    const SbVec3f & p = view.positions[i];
    if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) {
      if (outError) *outError = "IndexedLineSet vertex position contains NaN or Inf";
      return FastPathResult::INVALID_SCENE;
    }
  }

  // 2. Parse lines and bounds check
  struct LineInfo {
    size_t startIndex;
    size_t count;
  };
  std::vector<LineInfo> polylines;

  size_t currentStart = 0;
  size_t currentCount = 0;
  for (size_t i = 0; i < numIndices; ++i) {
    int32_t idx = view.coordIndex[i];
    if (idx < -1) {
      if (outError) *outError = "IndexedLineSet contains invalid negative coordinate index < -1";
      return FastPathResult::INVALID_SCENE;
    }
    if (idx == -1) {
      if (currentCount > 0) {
        polylines.push_back(LineInfo{currentStart, currentCount});
      }
      currentStart = i + 1;
      currentCount = 0;
    } else {
      if (static_cast<size_t>(idx) >= numPositions) {
        if (outError) *outError = "IndexedLineSet coordinate index out of bounds";
        return FastPathResult::INVALID_SCENE;
      }
      currentCount++;
    }
  }
  if (currentCount > 0) {
    polylines.push_back(LineInfo{currentStart, currentCount});
  }

  if (polylines.empty()) {
    return FastPathResult::SUCCESS_PRUNE;
  }

  // 3. Validate materialIndex preflight
  if (!view.materialIndex.empty()) {
    int32_t numDiff = 0;
    if (action && action->getState()) {
      const SoLazyElement * lazy = SoLazyElement::getInstance(action->getState());
      if (lazy) numDiff = lazy->getNumDiffuse();
    }
    for (size_t mi = 0; mi < view.materialIndex.size; ++mi) {
      int32_t val = view.materialIndex[mi];
      if (val < -1) {
        if (outError) *outError = "IndexedLineSet contains invalid negative material index < -1";
        return FastPathResult::INVALID_SCENE;
      }
      if (numDiff > 0 && val >= numDiff) {
        if (outError) *outError = "IndexedLineSet materialIndex references out-of-bounds material";
        return FastPathResult::INVALID_SCENE;
      }
    }
  }

  // 4. Staging validation and deduplication
  std::vector<VertexSnapshot> stagingVertices;
  std::vector<uint32_t> stagingIndices;
  std::map<VertexDeduplicationKey, uint32_t> uniqueVertexMap;

  uint32_t defaultMatSlot = this->captureMaterial(action, 0);

  auto getOrAddVertex = [&](int32_t coordIdx, int32_t normalIdx, int32_t texIdx, int32_t matIdx,
                            const SbVec3f & pos, const SbVec3f & norm, const SbVec2f & tc, uint32_t matSlot) -> uint32_t {
    VertexDeduplicationKey key;
    key.coordIdx = coordIdx;
    key.normalIdx = normalIdx;
    key.texCoordIdx = texIdx;
    key.materialIdx = matIdx;

    auto it = uniqueVertexMap.find(key);
    if (it != uniqueVertexMap.end()) {
      return it->second;
    }

    VertexSnapshot vs;
    vs.position[0] = pos[0]; vs.position[1] = pos[1]; vs.position[2] = pos[2];
    vs.normal[0] = norm[0]; vs.normal[1] = norm[1]; vs.normal[2] = norm[2];
    vs.texcoord[0] = tc[0]; vs.texcoord[1] = tc[1];
    vs.materialSlot = matSlot;

    uint32_t newIdx = static_cast<uint32_t>(stagingVertices.size());
    stagingVertices.push_back(vs);
    uniqueVertexMap[key] = newIdx;
    return newIdx;
  };

  size_t validLineIdx = 0;
  size_t totalSegmentIdx = 0;
  size_t matLineVertexCounter = 0;

  for (size_t l = 0; l < polylines.size(); ++l) {
    size_t count = polylines[l].count;
    if (count < 2) continue; // Degenerate line

    size_t s = polylines[l].startIndex;

    uint32_t lineMatSlot = defaultMatSlot;
    int32_t lineMatIdx = static_cast<int32_t>(validLineIdx);
    if (view.materialBinding == SoMaterialBindingElement::PER_FACE) { // PER_LINE non-indexed
      lineMatSlot = this->captureMaterial(action, static_cast<int>(validLineIdx));
    } else if (view.materialBinding == SoMaterialBindingElement::PER_FACE_INDEXED) { // PER_LINE_INDEXED
      if (!view.materialIndex.empty() && validLineIdx < view.materialIndex.size) {
        int32_t mi = view.materialIndex[validLineIdx];
        if (mi >= 0) lineMatIdx = mi;
      }
      lineMatSlot = this->captureMaterial(action, lineMatIdx);
    }

    for (size_t seg = 0; seg + 1 < count; ++seg) {
      uint32_t segMatSlot = lineMatSlot;
      int32_t segMatIdx = lineMatIdx;
      if (view.materialBinding == SoMaterialBindingElement::PER_PART) { // PER_SEGMENT non-indexed
        segMatIdx = static_cast<int32_t>(totalSegmentIdx);
        segMatSlot = this->captureMaterial(action, segMatIdx);
      } else if (view.materialBinding == SoMaterialBindingElement::PER_PART_INDEXED) { // PER_SEGMENT_INDEXED
        segMatIdx = static_cast<int32_t>(totalSegmentIdx);
        if (!view.materialIndex.empty() && totalSegmentIdx < view.materialIndex.size) {
          int32_t mi = view.materialIndex[totalSegmentIdx];
          if (mi >= 0) segMatIdx = mi;
        }
        segMatSlot = this->captureMaterial(action, segMatIdx);
      }

      auto resolveLineVertex = [&](size_t vertOffsetInLine) -> uint32_t {
        size_t indexInCoordIndex = s + vertOffsetInLine;
        int32_t cIdx = view.coordIndex[indexInCoordIndex];
        const SbVec3f & pos = view.positions[cIdx];
        SbVec3f norm(0.0f, 0.0f, 1.0f);

        uint32_t matSlot = segMatSlot;
        int32_t mKey = segMatIdx;
        if (view.materialBinding == SoMaterialBindingElement::OVERALL) {
          mKey = 0;
          matSlot = defaultMatSlot;
        } else if (view.materialBinding == SoMaterialBindingElement::PER_VERTEX) {
          mKey = static_cast<int32_t>(matLineVertexCounter++);
          matSlot = this->captureMaterial(action, mKey);
        } else if (view.materialBinding == SoMaterialBindingElement::PER_VERTEX_INDEXED) {
          int mIdx = cIdx;
          if (!view.materialIndex.empty() && indexInCoordIndex < view.materialIndex.size) {
            int32_t mi = view.materialIndex[indexInCoordIndex];
            if (mi >= 0) mIdx = mi;
          }
          matSlot = this->captureMaterial(action, mIdx);
          mKey = mIdx;
        }

        SbVec2f tc(0.0f, 0.0f);
        return getOrAddVertex(cIdx, 0, 0, mKey, pos, norm, tc, matSlot);
      };

      uint32_t v0 = resolveLineVertex(seg);
      uint32_t v1 = resolveLineVertex(seg + 1);
      stagingIndices.push_back(v0);
      stagingIndices.push_back(v1);

      totalSegmentIdx++;
    }

    validLineIdx++;
  }

  if (stagingIndices.empty()) {
    return FastPathResult::SUCCESS_PRUNE;
  }

  // 4. Atomic commit (force dedicated packet per fast-path occurrence)
  uint32_t rsSlot = this->captureRenderState(action, 0);
  if (this->isUnsupported) {
    if (outError) *outError = this->builderError.empty() ? "Unsupported feature in IndexedLineSet" : this->builderError;
    return FastPathResult::UNSUPPORTED;
  }
  this->ensureDrawPacket(PrimitiveTopology::LINE_LIST, rsSlot, node, /*forceNewPacket=*/true);

  uint32_t vertexOffset = static_cast<uint32_t>(this->currentPlan.vertices.size());
  for (size_t i = 0; i < stagingVertices.size(); ++i) {
    this->currentPlan.vertices.push_back(stagingVertices[i]);
  }
  for (size_t i = 0; i < stagingIndices.size(); ++i) {
    this->currentPlan.indices.push_back(vertexOffset + stagingIndices[i]);
  }

  DrawPacket & dp = this->currentPlan.draws[this->currentDrawIndex];
  dp.geometry.vertexCount = static_cast<uint32_t>(stagingVertices.size());
  dp.geometry.indexCount = static_cast<uint32_t>(stagingIndices.size());

  // Canonical serialization hash matching ABI CoinWgpuVertex
  uint64_t h = 14695981039346656037ULL;
  auto hashBytes = [&](const void * data, size_t len) {
    const uint8_t * b = static_cast<const uint8_t *>(data);
    for (size_t k = 0; k < len; ++k) {
      h ^= static_cast<uint64_t>(b[k]);
      h *= 1099511628211ULL;
    }
  };
  for (const auto & v : stagingVertices) {
    hashBytes(v.position, sizeof(v.position));
    hashBytes(v.normal, sizeof(v.normal));
    hashBytes(v.texcoord, sizeof(v.texcoord));
    hashBytes(&v.materialSlot, sizeof(v.materialSlot));
  }
  if (!stagingIndices.empty()) {
    hashBytes(stagingIndices.data(), stagingIndices.size() * sizeof(uint32_t));
  }
  if (h == 0) h = 1;

  uint64_t stableId = reinterpret_cast<uint64_t>(node);
  dp.stableNodeId = stableId;
  dp.drawOrdinal = this->nodeOccurrenceCount[stableId]++;
  dp.sourceRevision = h;

  return FastPathResult::SUCCESS_PRUNE;
}
