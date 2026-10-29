#include <Inventor/elements/SoBumpMapCoordinateElement.h>
#include <Inventor/elements/SoBumpMapElement.h>
#include <Inventor/elements/SoBumpMapMatrixElement.h>
#include <Inventor/nodes/SoBumpMap.h>
// src/rendering/SoRenderIR.cpp

#include "rendering/SoRenderIRP.h"
#include <array>
#include <stdexcept>
#include "rendering/SoTextureQualityPolicy.h"
#include "elements/SoLazyElementP.h"

#include <Inventor/C/tidbits.h>
#include <Inventor/elements/SoDevicePixelRatioElement.h>
#include <Inventor/elements/SoDepthBufferElement.h>
#include <Inventor/elements/SoDrawStyleElement.h>
#include <Inventor/elements/SoEnvironmentElement.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoLightAttenuationElement.h>
#include <Inventor/elements/SoClipPlaneElement.h>
#include "elements/SoRenderDepthPolicyElement.h"
#include <Inventor/elements/SoMultiTextureMatrixElement.h>
#include <Inventor/elements/SoLightElement.h>
#include <Inventor/elements/SoLightModelElement.h>
#include "elements/SoPBRMaterialElement.h"
#include <Inventor/elements/SoMaterialBindingElement.h>
#include <Inventor/elements/SoLinePatternElement.h>
#include <Inventor/elements/SoLineWidthElement.h>
#include <Inventor/elements/SoMultiTextureEnabledElement.h>
#include <Inventor/elements/SoMultiTextureImageElement.h>
#include <Inventor/elements/SoPointSizeElement.h>
#include <Inventor/elements/SoRenderMatrixPolicyElement.h>
#include <Inventor/elements/SoPickStyleElement.h>
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoProjectionMatrixElement.h>
#include <Inventor/elements/SoTextureQualityElement.h>
#include <Inventor/elements/SoShapeHintsElement.h>
#include <Inventor/elements/SoShapeStyleElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/elements/SoViewVolumeElement.h>
#include <Inventor/elements/SoFocalDistanceElement.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/elements/SoPolygonOffsetElement.h>
#include "elements/SoRenderPlacementElement.h"
#include <Inventor/errors/SoDebugError.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoLight.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoSpotLight.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cstdlib>
#include <inttypes.h>
#include <utility>

namespace {

bool
lightingEqual(const SoLightData & lhs, const SoLightData & rhs)
{
  return lhs.type == rhs.type &&
         lhs.color == rhs.color &&
         lhs.direction == rhs.direction &&
         lhs.position == rhs.position &&
         lhs.attenuation == rhs.attenuation &&
         lhs.spotCutoffCos == rhs.spotCutoffCos &&
         lhs.spotExponent == rhs.spotExponent;
}

bool
lightingEqual(const SoLightingData & lhs, const SoLightingData & rhs)
{
  if (lhs.ambient != rhs.ambient || lhs.lights.size() != rhs.lights.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.lights.size(); ++i) {
    if (!lightingEqual(lhs.lights[i], rhs.lights[i])) {
      return false;
    }
  }
  return true;
}

enum LegacyGLBlendFactorValue {
  LEGACY_GL_ZERO = 0x0000,
  LEGACY_GL_ONE = 0x0001,
  LEGACY_GL_SRC_COLOR = 0x0300,
  LEGACY_GL_ONE_MINUS_SRC_COLOR = 0x0301,
  LEGACY_GL_SRC_ALPHA = 0x0302,
  LEGACY_GL_ONE_MINUS_SRC_ALPHA = 0x0303,
  LEGACY_GL_DST_ALPHA = 0x0304,
  LEGACY_GL_ONE_MINUS_DST_ALPHA = 0x0305,
  LEGACY_GL_DST_COLOR = 0x0306,
  LEGACY_GL_ONE_MINUS_DST_COLOR = 0x0307,
  LEGACY_GL_SRC_ALPHA_SATURATE = 0x0308,
  LEGACY_GL_CONSTANT_COLOR = 0x8001,
  LEGACY_GL_ONE_MINUS_CONSTANT_COLOR = 0x8002,
  LEGACY_GL_CONSTANT_ALPHA = 0x8003,
  LEGACY_GL_ONE_MINUS_CONSTANT_ALPHA = 0x8004,
  LEGACY_GL_SRC1_ALPHA = 0x8589,
  LEGACY_GL_SRC1_COLOR = 0x88F9,
  LEGACY_GL_ONE_MINUS_SRC1_COLOR = 0x88FA,
  LEGACY_GL_ONE_MINUS_SRC1_ALPHA = 0x88FB
};

enum LegacyGLAlphaTestFunctionValue {
  LEGACY_GL_NEVER = 0x0200,
  LEGACY_GL_LESS = 0x0201,
  LEGACY_GL_EQUAL = 0x0202,
  LEGACY_GL_LEQUAL = 0x0203,
  LEGACY_GL_GREATER = 0x0204,
  LEGACY_GL_NOTEQUAL = 0x0205,
  LEGACY_GL_GEQUAL = 0x0206,
  LEGACY_GL_ALWAYS = 0x0207
};

SoBlendFactor
blendFactorFromLegacyGL(const int value)
{
  // Keep the legacy GL values local to this conversion boundary. No GL enum
  // is stored in the public IR.
  switch (value) {
  case LEGACY_GL_ZERO: return SO_BLEND_FACTOR_ZERO;
  case LEGACY_GL_ONE: return SO_BLEND_FACTOR_ONE;
  case LEGACY_GL_SRC_COLOR: return SO_BLEND_FACTOR_SRC_COLOR;
  case LEGACY_GL_ONE_MINUS_SRC_COLOR: return SO_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
  case LEGACY_GL_SRC_ALPHA: return SO_BLEND_FACTOR_SRC_ALPHA;
  case LEGACY_GL_ONE_MINUS_SRC_ALPHA: return SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  case LEGACY_GL_DST_ALPHA: return SO_BLEND_FACTOR_DST_ALPHA;
  case LEGACY_GL_ONE_MINUS_DST_ALPHA: return SO_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
  case LEGACY_GL_DST_COLOR: return SO_BLEND_FACTOR_DST_COLOR;
  case LEGACY_GL_ONE_MINUS_DST_COLOR: return SO_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
  case LEGACY_GL_SRC_ALPHA_SATURATE: return SO_BLEND_FACTOR_SRC_ALPHA_SATURATE;
  case LEGACY_GL_CONSTANT_COLOR: return SO_BLEND_FACTOR_CONSTANT_COLOR;
  case LEGACY_GL_ONE_MINUS_CONSTANT_COLOR: return SO_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
  case LEGACY_GL_CONSTANT_ALPHA: return SO_BLEND_FACTOR_CONSTANT_ALPHA;
  case LEGACY_GL_ONE_MINUS_CONSTANT_ALPHA: return SO_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
  // Dual-source factors are represented by the later material/lighting
  // layer. The backend-neutral base has no corresponding IR vocabulary yet,
  // so retain the historical primary-source approximation here.
  case LEGACY_GL_SRC1_ALPHA: return SO_BLEND_FACTOR_SRC_ALPHA;
  case LEGACY_GL_SRC1_COLOR: return SO_BLEND_FACTOR_SRC_COLOR;
  case LEGACY_GL_ONE_MINUS_SRC1_COLOR: return SO_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
  case LEGACY_GL_ONE_MINUS_SRC1_ALPHA: return SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  default:     return SO_BLEND_FACTOR_ONE;
  }
}

SoAlphaTestFunction
alphaTestFunctionFromLegacyGL(const int value)
{
  switch (value) {
  case LEGACY_GL_NEVER: return SO_ALPHA_TEST_NEVER;
  case LEGACY_GL_ALWAYS: return SO_ALPHA_TEST_ALWAYS;
  case LEGACY_GL_LESS: return SO_ALPHA_TEST_LESS;
  case LEGACY_GL_LEQUAL: return SO_ALPHA_TEST_LEQUAL;
  case LEGACY_GL_EQUAL: return SO_ALPHA_TEST_EQUAL;
  case LEGACY_GL_GEQUAL: return SO_ALPHA_TEST_GEQUAL;
  case LEGACY_GL_GREATER: return SO_ALPHA_TEST_GREATER;
  case LEGACY_GL_NOTEQUAL: return SO_ALPHA_TEST_NOTEQUAL;
  default:     return SO_ALPHA_TEST_NONE;
  }
}

SoTextureModel
textureModelFromLegacy(SoMultiTextureImageElement::Model model)
{
  switch (model) {
  case SoMultiTextureImageElement::DECAL:
    return SO_TEXTURE_MODEL_DECAL;
  case SoMultiTextureImageElement::BLEND:
    return SO_TEXTURE_MODEL_BLEND;
  case SoMultiTextureImageElement::REPLACE:
    return SO_TEXTURE_MODEL_REPLACE;
  case SoMultiTextureImageElement::MODULATE:
  default:
    return SO_TEXTURE_MODEL_MODULATE;
  }
}

void
textureFiltersFromQuality(const float quality, SoTextureData & texture)
{
  const CoinTextureQualityPolicy policy =
    coin_get_texture_quality_policy(quality);
  if (!policy.linear) {
    texture.minFilter = SO_TEXTURE_FILTER_NEAREST;
    texture.magFilter = SO_TEXTURE_FILTER_NEAREST;
  }
  else if (!policy.mipmap) {
    texture.minFilter = SO_TEXTURE_FILTER_LINEAR;
    texture.magFilter = SO_TEXTURE_FILTER_LINEAR;
  }
  else if (!policy.linearMipmap) {
    texture.minFilter = SO_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR;
    texture.magFilter = SO_TEXTURE_FILTER_LINEAR;
  }
  else {
    texture.minFilter = SO_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR;
    texture.magFilter = SO_TEXTURE_FILTER_LINEAR;
  }

  // Preserve the LegacyGL quality policy in the neutral IR. The GL
  // executor selects the active driver's supported anisotropy level.
  texture.anisotropic = policy.anisotropic;
}

} // namespace

SbBool
coin_render_ir_trace_enabled()
{
  static int initialized = 0;
  static SbBool enabled = FALSE;
  if (!initialized) {
    enabled = coin_getenv("COIN_DEBUG_RENDER_IR") ? TRUE : FALSE;
    initialized = 1;
  }
  return enabled;
}

SoIRBuffer::SoIRBuffer()
{
}

constexpr size_t SoIRBuffer::MIN_CHUNK_SIZE;

void
SoIRBuffer::clear()
{
  // Track high-water mark so we can pre-size on next frame
  if (this->totalAllocated > this->highWaterMark) {
    this->highWaterMark = this->totalAllocated;
  }
  // Reset cursors but keep chunks allocated
  for (auto & chunk : this->chunks) {
    chunk->cursor = 0;
  }
  this->totalAllocated = 0;
}

void
SoIRBuffer::reserve(size_t bytes)
{
  // Ensure the first chunk is at least this large
  if (this->chunks.empty()) {
    std::unique_ptr<Chunk> c(new Chunk);
    c->data.resize(std::max(bytes, MIN_CHUNK_SIZE));
    this->chunks.push_back(std::move(c));
  } else if (bytes > this->chunks[0]->data.size()) {
    // Only resize the first chunk if it hasn't been used yet
    if (this->chunks[0]->cursor == 0) {
      this->chunks[0]->data.resize(bytes);
    }
  }
}

void *
SoIRBuffer::allocate(size_t bytes, size_t alignment)
{
  if (alignment == 0) alignment = 1;

  // Try to allocate from an existing chunk
  for (auto & chunk : this->chunks) {
    size_t aligned = (chunk->cursor + alignment - 1) & ~(alignment - 1);
    if (aligned + bytes <= chunk->data.size()) {
      void * ptr = chunk->data.data() + aligned;
      chunk->cursor = aligned + bytes;
      this->totalAllocated += bytes;
      return ptr;
    }
  }

  // Need a new chunk — size it to at least fit this allocation
  // and to avoid many small chunks
  size_t chunkSize = std::max({bytes, MIN_CHUNK_SIZE, this->highWaterMark / 2});
  std::unique_ptr<Chunk> c(new Chunk);
  c->data.resize(chunkSize);
  c->cursor = bytes;
  void * ptr = c->data.data();
  this->chunks.push_back(std::move(c));
  this->totalAllocated += bytes;
  return ptr;
}

SoIRBuffer::Checkpoint
SoIRBuffer::checkpoint() const
{
  Checkpoint result;
  result.chunkCount = this->chunks.size();
  result.totalAllocated = this->totalAllocated;
  result.cursors.reserve(result.chunkCount);
  for (const auto & chunk : this->chunks) result.cursors.push_back(chunk->cursor);
  return result;
}

void
SoIRBuffer::rewind(const Checkpoint & checkpoint)
{
  this->chunks.resize(checkpoint.chunkCount);
  for (size_t i = 0; i < checkpoint.cursors.size(); ++i) {
    this->chunks[i]->cursor = checkpoint.cursors[i];
  }
  this->totalAllocated = checkpoint.totalAllocated;
}


namespace {
template<class T> void appendCommandBits(std::string & key, const T & value) {
  key.append(reinterpret_cast<const char *>(&value), sizeof(value));
}
struct CommandKeyWriter {
  std::array<char, sizeof(SoRenderCommand) + sizeof(SoPBRMaterialData)> bytes;
  size_t used = 0;
};
template<class T> void appendCommandBits(CommandKeyWriter & key, const T & value) {
  if (sizeof(value) > key.bytes.size()-key.used)
    throw std::length_error("Retained command key exceeds fixed member buffer");
  std::memcpy(key.bytes.data()+key.used, &value, sizeof(value));
  key.used += sizeof(value);
}
std::string commandTemplateKey(const SoRenderCommand & c) {
  // Serialize members, never structure padding, vector internals, or pointed-to
  // payloads. Resource pointers/handles retain their original lifetime contract.
  static_assert(sizeof(SbMatrix)==16*sizeof(float), "matrix representation");
  static_assert(sizeof(SbVec4f)==4*sizeof(float) && sizeof(SbVec3f)==3*sizeof(float) &&
                sizeof(SbVec2f)==2*sizeof(float), "vector representation");
  CommandKeyWriter key;
  appendCommandBits(key, c.geometry.topology);
  appendCommandBits(key, c.geometry.vertexCount);
  appendCommandBits(key, c.geometry.normalCount);
  appendCommandBits(key, c.geometry.indexCount);
  appendCommandBits(key, c.geometry.positions);
  appendCommandBits(key, c.geometry.normals);
  appendCommandBits(key, c.geometry.texcoords);
  appendCommandBits(key, c.geometry.pbrCoordinates.get());
  appendCommandBits(key, c.geometry.colors);
  appendCommandBits(key, c.geometry.indices);
  appendCommandBits(key, c.geometry.vertexStride);
  appendCommandBits(key, c.geometry.texcoordStride);
  appendCommandBits(key, c.geometry.cacheKey);
  appendCommandBits(key, c.geometry.revision);
  appendCommandBits(key, c.geometry.boundsCenter);
  appendCommandBits(key, c.geometry.hasBounds);
  appendCommandBits(key, c.geometryHandle);
  appendCommandBits(key, c.material.diffuse);
  appendCommandBits(key, c.material.ambient);
  appendCommandBits(key, c.material.specular);
  appendCommandBits(key, c.material.emissive);
  appendCommandBits(key, c.material.shadingModel);
  appendCommandBits(key, bool(c.material.pbr));
  appendCommandBits(key, c.material.pbrSurface.get());
  if (c.material.pbr) {
    appendCommandBits(key, c.material.pbr->baseColor);
    appendCommandBits(key, c.material.pbr->emissive);
    appendCommandBits(key, c.material.pbr->metallic);
    appendCommandBits(key, c.material.pbr->roughness);
    appendCommandBits(key, c.material.pbr->baseColorTextureUnit);
    appendCommandBits(key, c.material.pbr->ormTextureUnit);
    appendCommandBits(key, c.material.pbr->emissiveTextureUnit);
    appendCommandBits(key, c.material.pbr->clearcoat);
    appendCommandBits(key, c.material.pbr->clearcoatRoughness);
    appendCommandBits(key, c.material.pbr->anisotropy);
    appendCommandBits(key, c.material.pbr->anisotropyRotation);
    appendCommandBits(key, c.material.pbr->normalScale);
    appendCommandBits(key, c.material.pbr->normalYFlip);
    appendCommandBits(key, c.material.pbr->tangentCoordinateUnit);
  }
  appendCommandBits(key, c.material.shininess);
  appendCommandBits(key, c.material.opacity);
  appendCommandBits(key, c.material.screenDoorTransparency);
  appendCommandBits(key, c.material.texture.pixels);
  appendCommandBits(key, c.material.texture.width);
  appendCommandBits(key, c.material.texture.height);
  appendCommandBits(key, c.material.texture.numComponents);
  appendCommandBits(key, c.material.texture.hasTransparency);
  appendCommandBits(key, c.material.texture.cacheKey);
  appendCommandBits(key, c.material.texture.revision);
  appendCommandBits(key, c.material.texture.minFilter);
  appendCommandBits(key, c.material.texture.magFilter);
  appendCommandBits(key, c.material.texture.wrapS);
  appendCommandBits(key, c.material.texture.wrapT);
  appendCommandBits(key, c.material.texture.anisotropic);
  appendCommandBits(key, c.material.texture.matrix);
  appendCommandBits(key, c.material.texture.model);
  appendCommandBits(key, c.material.texture.blendColor);
  appendCommandBits(key, c.material.textureAlphaIncludesOpacity);
  appendCommandBits(key, c.material.vertexColorAlphaIncludesOpacity);
  appendCommandBits(key, c.material.twoSidedLighting);
  appendCommandBits(key, c.state.depth.explicitMask);
  appendCommandBits(key, c.state.depth.enabled);
  appendCommandBits(key, c.state.depth.writeEnabled);
  appendCommandBits(key, c.state.depth.func);
  appendCommandBits(key, c.state.depth.range);
  appendCommandBits(key, c.state.blend.enabled);
  appendCommandBits(key, c.state.blend.srcRGBFactor);
  appendCommandBits(key, c.state.blend.dstRGBFactor);
  appendCommandBits(key, c.state.blend.srcAlphaFactor);
  appendCommandBits(key, c.state.blend.dstAlphaFactor);
  appendCommandBits(key, c.state.blend.rgbEquation);
  appendCommandBits(key, c.state.blend.alphaEquation);
  appendCommandBits(key, c.state.alphaTest.policy);
  appendCommandBits(key, c.state.alphaTest.function);
  appendCommandBits(key, c.state.alphaTest.reference);
  appendCommandBits(key, c.state.raster.visible);
  appendCommandBits(key, c.state.raster.fillMode);
  appendCommandBits(key, c.state.raster.cullBackFaces);
  appendCommandBits(key, c.state.raster.frontFaceCCW);
  appendCommandBits(key, c.state.raster.scissorEnabled);
  appendCommandBits(key, c.state.raster.viewportOverride);
  appendCommandBits(key, c.state.raster.viewportEnabled);
  appendCommandBits(key, c.state.raster.viewportX);
  appendCommandBits(key, c.state.raster.viewportY);
  appendCommandBits(key, c.state.raster.viewportWidth);
  appendCommandBits(key, c.state.raster.viewportHeight);
  appendCommandBits(key, c.state.raster.lineWidth);
  appendCommandBits(key, c.state.raster.pointSize);
  appendCommandBits(key, c.state.raster.linePattern);
  appendCommandBits(key, c.state.raster.linePatternScale);
  appendCommandBits(key, c.state.raster.polygonOffsetFactor);
  appendCommandBits(key, c.state.raster.polygonOffsetUnits);
  appendCommandBits(key, c.state.raster.polygonOffsetFilled);
  appendCommandBits(key, c.state.raster.polygonOffsetLines);
  appendCommandBits(key, c.state.raster.polygonOffsetPoints);
  appendCommandBits(key, c.state.camera.sourceRevision);
  appendCommandBits(key, c.state.camera.perspective);
  appendCommandBits(key, c.state.camera.nearDistance);
  appendCommandBits(key, c.state.camera.farDistance);
  appendCommandBits(key, c.state.camera.focalDistance);
  appendCommandBits(key, c.state.camera.aspectRatio);
  appendCommandBits(key, c.state.clipPlaneCount);
  for (size_t i = 0; i < std::min<size_t>(8, c.state.clipPlaneCount); ++i) {
    appendCommandBits(key, c.state.clipPlanesWorld[i].getNormal());
    const float distance = c.state.clipPlanesWorld[i].getDistanceFromOrigin();
    appendCommandBits(key, distance);
  }
  appendCommandBits(key, c.state.fog.mode);
  appendCommandBits(key, c.state.fog.color);
  appendCommandBits(key, c.state.fog.start);
  appendCommandBits(key, c.state.fog.visibility);
  appendCommandBits(key, c.state.useCommandMatrices);
  for (int row=0;row<4;++row) for (int col=0;col<4;++col)
    if (!((row<3 && row==col) || (row==3 && col<3)))
      appendCommandBits(key, c.modelMatrix[row][col]);
  appendCommandBits(key, c.viewMatrix);
  appendCommandBits(key, c.projMatrix);
  appendCommandBits(key, c.opacityClass);
  appendCommandBits(key, c.stage);
  appendCommandBits(key, c.lightingHandle);
  appendCommandBits(key, c.materialIndex);
  appendCommandBits(key, c.objectId);
  appendCommandBits(key, c.pixelRaster.enabled);
  appendCommandBits(key, c.pixelRaster.originX);
  appendCommandBits(key, c.pixelRaster.originY);
  appendCommandBits(key, c.pixelRaster.width);
  appendCommandBits(key, c.pixelRaster.height);
  appendCommandBits(key, c.pick.pickable);
  appendCommandBits(key, c.pick.useResourceElementRanges);
  appendCommandBits(key, c.userData);
  appendCommandBits(key, c.pick.elementRanges.size());
  std::string result(key.bytes.data(), key.used);
  for (const auto & range : c.pick.elementRanges) {
    appendCommandBits(result, range.type); appendCommandBits(result, range.elementIndex);
    appendCommandBits(result, range.drawStart); appendCommandBits(result, range.drawCount);
  }
  return result;
}
}
uint32_t SoDrawList::internCommand(const SoRenderCommand & command) {
  auto key = commandTemplateKey(command);
  const auto found = commandTemplateKeys.find(key);
  if (found != commandTemplateKeys.end()) return found->second;
  const uint32_t index = static_cast<uint32_t>(commandTemplates.size());
  commandTemplates.push_back(command);
  CompactCommand zero; zero.restoreModel(commandTemplates.back().modelMatrix);
  commandTemplateKeys.emplace(std::move(key), index);
  return index;
}
SbBool SoDrawList::isCommandVisible(int i) const {
  return compactCommandsActive ? commandTemplates[compactCommands[static_cast<size_t>(i)].common].state.raster.visible
    : commands[static_cast<size_t>(i)].state.raster.visible;
}
SoRenderCommand SoDrawList::getCommandSnapshot(int i) const {
  if (!compactCommandsActive) return commands[static_cast<size_t>(i)];
  const auto & entry = compactCommands[static_cast<size_t>(i)];
  SoRenderCommand result = commandTemplates[entry.common];
  entry.restoreModel(result.modelMatrix);
  return result;
}
void SoDrawList::setCommandForRetainedUpdate(int i, const SoRenderCommand & command) {
  pendingRetainedCommandUpdates.push_back(static_cast<uint32_t>(i));
  if (!compactCommandsActive) commands[static_cast<size_t>(i)] = command;
  else {
    const uint32_t common = internCommand(command);
    auto & entry = compactCommands[static_cast<size_t>(i)];
    entry.captureModel(command.modelMatrix); entry.common = common;
  }
}
void SoDrawList::expandCommands() const {
  if (!compactCommandsActive) return;
  // Allocate transactionally: legacy pointer/reference APIs retain their full
  // semantics, including contiguous storage and externally mutable commands.
  std::vector<SoRenderCommand> expanded; expanded.reserve(compactCommands.size());
  for (size_t i=0; i<compactCommands.size(); ++i)
    expanded.push_back(getCommandSnapshot(static_cast<int>(i)));
  commands.swap(expanded); compactCommandsActive = false;
  compactAttemptRevision = uint64_t(-1);
  std::vector<CompactCommand>().swap(compactCommands);
  std::vector<SoRenderCommand>().swap(commandTemplates);
  commandTemplateKeys.clear(); commandTemplateKeys.rehash(0);
}
void SoDrawList::setCompactCommandStorage(bool enabled) {
  preferCompactCommands=enabled;
  if (getNumCommands()==0) compactCommandsActive=enabled;
}
void SoDrawList::compactRetainedStorage() {
  // Bound template history; a changed common state can leave an unused entry.
  // Periodic collection preserves individual-command updates without unbounded
  // accumulation. Small/diverse lists keep ordinary storage.
  if (compactCommandsActive) {
    if (compactCommands.size() >= 64 && commandTemplates.size() <= 4096 &&
        commandTemplates.size()*4 <= compactCommands.size()) {
      compactCommands.shrink_to_fit();
      return;
    }
    expandCommands();
    compactAttemptRevision=contentRevision;
    return;
  }
  if (!compactCommandsActive && compactAttemptRevision == contentRevision) return;
  if (compactCommandsActive) expandCommands();
  compactAttemptRevision = contentRevision;
  if (commands.size() < 64) return;
  try {
    ++compactScanCount;
    compactCommands.clear(); commandTemplates.clear(); commandTemplateKeys.clear();
    compactCommands.reserve(commands.size());
    for (const auto & command : commands) {
      const uint32_t common = internCommand(command);
      if (commandTemplates.size() > 4096 ||
          commandTemplates.size()*4 > commands.size()) {
        std::vector<CompactCommand>().swap(compactCommands);
        std::vector<SoRenderCommand>().swap(commandTemplates);
        commandTemplateKeys.clear(); commandTemplateKeys.rehash(0); return;
      }
      CompactCommand entry; entry.captureModel(command.modelMatrix); entry.common = common;
      compactCommands.push_back(entry);
    }
    compactCommandsActive = true;
    std::vector<SoRenderCommand>().swap(commands);
  } catch (const std::bad_alloc &) {
    // Optional storage optimization must not fail an already published frame.
    std::vector<CompactCommand>().swap(compactCommands);
    std::vector<SoRenderCommand>().swap(commandTemplates);
    commandTemplateKeys.clear(); commandTemplateKeys.rehash(0);
  }
}
size_t SoDrawList::getCommandStorageBytes() const {
  return commands.capacity()*sizeof(SoRenderCommand) +
    compactCommands.capacity()*sizeof(CompactCommand) +
    commandTemplates.capacity()*sizeof(SoRenderCommand);
}

SoDrawList::SoDrawList()
{
}

void
SoDrawList::clear()
{
  projectionUpdate = SoCameraProjectionUpdate();
  this->compactCommandsActive = preferCompactCommands;
  this->compactCommands.clear();
  std::vector<SoRenderCommand>().swap(this->commandTemplates);
  this->commandTemplateKeys.clear();
  if (preferCompactCommands) std::vector<SoRenderCommand>().swap(this->commands);
  else this->commands.clear();
  this->geometryResources.clear();
  this->lightingSetups.clear();
  this->depthClearEvents.clear();
  this->selection = SoSelectionState();
  this->pickLUT.clear();
  this->pendingRetainedCommandUpdates.clear();
  this->retainedCommandUpdates.clear();
  this->retainedCommandUpdatesFromRevision = 0;
  this->generation++;
  this->contentRevision++;
  this->renderPlanRevision++;
  this->resourceRevision++;
  this->pickLUTGeneration = 0;
  this->pickLUTValid = false;
}

void
SoDrawList::truncate(int count)
{
  if (count < this->getNumCommands()) {
    ++this->contentRevision;
    ++this->renderPlanRevision;
    ++this->resourceRevision;
    if (compactCommandsActive) compactCommands.resize(static_cast<size_t>(count));
    else this->commands.resize(static_cast<size_t>(count));
    auto trimTargets = [count](std::vector<SoSelectionTarget> & targets) {
      targets.erase(
        std::remove_if(
          targets.begin(), targets.end(),
          [count](const SoSelectionTarget & target) {
            return target.commandIndex >= count;
          }),
        targets.end());
    };
    trimTargets(this->selection.selected);
    trimTargets(this->selection.highlighted);
    while (!this->depthClearEvents.empty() &&
           this->depthClearEvents.back().sequence >
             static_cast<uint32_t>(count)) {
      this->depthClearEvents.pop_back();
    }
    this->pickLUT.clear();
    this->pickLUTGeneration = 0;
    this->pickLUTValid = false;
  }
}

void
SoDrawList::reserve(int count)
{
  this->expandCommands();
  this->commands.reserve(static_cast<size_t>(count));
}

void
SoDrawList::addCommand(const SoRenderCommand & cmd)
{
  ++this->contentRevision;
  ++this->renderPlanRevision;
  ++this->resourceRevision;
  if (compactCommandsActive) {
    CompactCommand entry; entry.captureModel(cmd.modelMatrix); entry.common=internCommand(cmd);
    compactCommands.push_back(entry);
    if (commandTemplates.size()>4096) expandCommands();
  } else this->commands.push_back(cmd);
  this->pickLUT.clear();
  this->pickLUTGeneration = 0;
  this->pickLUTValid = false;
}

void
SoDrawList::addCommand(SoRenderCommand && cmd)
{
  if (compactCommandsActive) {
    CompactCommand entry; entry.captureModel(cmd.modelMatrix); entry.common=internCommand(cmd);
    compactCommands.push_back(entry);
    if (commandTemplates.size()>4096) expandCommands();
  } else this->commands.push_back(std::move(cmd));
  this->pickLUT.clear();
  this->pickLUTGeneration = 0;
  this->pickLUTValid = false;
}

SoRenderCommand &
SoDrawList::emplaceCommand()
{
  ++this->contentRevision;
  ++this->renderPlanRevision;
  ++this->resourceRevision;
  this->pickLUT.clear();
  this->pickLUTGeneration = 0;
  this->pickLUTValid = false;
  this->expandCommands();
  this->commands.emplace_back();
  return this->commands.back();
}

SoGeometryHandle
SoDrawList::addGeometryResource(const SoGeometryResource & resource)
{
  this->geometryResources.push_back(resource);
  return static_cast<SoGeometryHandle>(this->geometryResources.size());
}

SoGeometryResource *
SoDrawList::getGeometryResource(SoGeometryHandle handle)
{
  if (handle == SO_INVALID_GEOMETRY_HANDLE ||
      static_cast<size_t>(handle) > this->geometryResources.size()) {
    return nullptr;
  }
  return &this->geometryResources[static_cast<size_t>(handle - 1)];
}

const SoGeometryResource *
SoDrawList::getGeometryResource(SoGeometryHandle handle) const
{
  if (handle == SO_INVALID_GEOMETRY_HANDLE ||
      static_cast<size_t>(handle) > this->geometryResources.size()) {
    return nullptr;
  }
  return &this->geometryResources[static_cast<size_t>(handle - 1)];
}

int
SoDrawList::getNumGeometryResources() const
{
  return static_cast<int>(this->geometryResources.size());
}

void
SoDrawList::truncateGeometryResources(int count)
{
  if (count >= 0 && count < static_cast<int>(this->geometryResources.size())) {
    ++this->contentRevision;
    ++this->renderPlanRevision;
    ++this->resourceRevision;
    this->geometryResources.resize(static_cast<size_t>(count));
  }
}

void
SoDrawList::markGeometryResourcesChanged()
{
  ++this->contentRevision;
  ++this->renderPlanRevision;
  ++this->resourceRevision;
  this->pickLUT.clear();
  this->pickLUTGeneration = 0;
  this->pickLUTValid = false;
}

SoRenderCommand &
SoDrawList::getCommandForRetainedUpdate(int i)
{
  this->expandCommands();
  this->pendingRetainedCommandUpdates.push_back(static_cast<uint32_t>(i));
  return this->commands[static_cast<size_t>(i)];
}

SoDrawList::MutationCheckpoint
SoDrawList::createMutationCheckpoint() const
{
  MutationCheckpoint checkpoint;
  checkpoint.commandCount = this->getNumCommands();
  checkpoint.geometryResourceCount =
    static_cast<int>(this->geometryResources.size());
  checkpoint.lightingSetupCount = static_cast<int>(this->lightingSetups.size());
  checkpoint.depthEventCount = static_cast<int>(this->depthClearEvents.size());
  checkpoint.selection = this->selection;
  checkpoint.pickLUT = this->pickLUT;
  checkpoint.generation = this->generation;
  checkpoint.contentRevision = this->contentRevision;
  checkpoint.renderPlanRevision = this->renderPlanRevision;
  checkpoint.resourceRevision = this->resourceRevision;
  checkpoint.pickLUTGeneration = this->pickLUTGeneration;
  checkpoint.pickLUTRevision = this->pickLUTRevision;
  checkpoint.pickLUTValid = this->pickLUTValid;
  checkpoint.pendingRetainedCommandUpdates =
    this->pendingRetainedCommandUpdates;
  checkpoint.retainedCommandUpdates = this->retainedCommandUpdates;
  checkpoint.retainedCommandUpdatesFromRevision =
    this->retainedCommandUpdatesFromRevision;
  return checkpoint;
}

void
SoDrawList::restoreMutationCheckpoint(const MutationCheckpoint & checkpoint)
{
  if (compactCommandsActive) compactCommands.resize(static_cast<size_t>(checkpoint.commandCount));
  else this->commands.resize(static_cast<size_t>(checkpoint.commandCount));
  this->geometryResources.resize(
    static_cast<size_t>(checkpoint.geometryResourceCount));
  this->lightingSetups.resize(
    static_cast<size_t>(checkpoint.lightingSetupCount));
  this->depthClearEvents.resize(static_cast<size_t>(checkpoint.depthEventCount));
  this->selection = checkpoint.selection;
  this->pickLUT = checkpoint.pickLUT;
  this->generation = checkpoint.generation;
  this->contentRevision = checkpoint.contentRevision;
  this->renderPlanRevision = checkpoint.renderPlanRevision;
  this->resourceRevision = checkpoint.resourceRevision;
  this->pickLUTGeneration = checkpoint.pickLUTGeneration;
  this->pickLUTRevision = checkpoint.pickLUTRevision;
  this->pickLUTValid = checkpoint.pickLUTValid;
  this->pendingRetainedCommandUpdates =
    checkpoint.pendingRetainedCommandUpdates;
  this->retainedCommandUpdates = checkpoint.retainedCommandUpdates;
  this->retainedCommandUpdatesFromRevision =
    checkpoint.retainedCommandUpdatesFromRevision;
}

bool SoDrawList::updateRetainedProjection(uint64_t sourceRevision, const SbMatrix & view,
    const SbMatrix & projection, const SoCameraData & camera) {
  const auto & source = compactCommandsActive ? commandTemplates : commands;
  if (source.empty()) return false;
  for (const auto & command : source)
    if (!command.state.useCommandMatrices || command.state.camera.sourceRevision != sourceRevision || command.viewMatrix != view)
      return false;
  if (compactCommandsActive) {
    // Re-key common templates transactionally; command membership and geometry
    // remain untouched. Small/diverse lists use their ordinary command storage.
    auto replacements = commandTemplates;
    std::unordered_map<std::string, uint32_t> keys;
    keys.reserve(replacements.size());
    for (uint32_t i = 0; i < replacements.size(); ++i) {
      replacements[i].projMatrix = projection;
      replacements[i].state.camera = camera;
      keys.emplace(commandTemplateKey(replacements[i]), i);
    }
    commandTemplates.swap(replacements); commandTemplateKeys.swap(keys);
  } else {
    for (auto & command : commands) {
      command.projMatrix = projection;
      command.state.camera = camera;
    }
  }
  projectionUpdate.valid = true;
  projectionUpdate.fromRevision = contentRevision;
  projectionUpdate.toRevision = contentRevision + 1;
  projectionUpdate.projection = projection;
  projectionUpdate.camera = camera;
  return true;
}

void
SoDrawList::applyRetainedInvalidation(const bool plan, const bool resources,
                                      const bool pickTopology)
{
  const uint64_t previousContentRevision = this->contentRevision;
  ++this->contentRevision;
  std::sort(this->pendingRetainedCommandUpdates.begin(),
            this->pendingRetainedCommandUpdates.end());
  this->pendingRetainedCommandUpdates.erase(
    std::unique(this->pendingRetainedCommandUpdates.begin(),
                this->pendingRetainedCommandUpdates.end()),
    this->pendingRetainedCommandUpdates.end());
  this->retainedCommandUpdates.swap(this->pendingRetainedCommandUpdates);
  this->pendingRetainedCommandUpdates.clear();
  this->retainedCommandUpdatesFromRevision =
    this->retainedCommandUpdates.empty() ? 0 : previousContentRevision;
  if (plan) ++this->renderPlanRevision;
  if (resources) ++this->resourceRevision;
  if (pickTopology) {
    this->pickLUT.clear();
    this->pickLUTGeneration = 0;
    this->pickLUTValid = false;
  }
}

const SoGeometryDesc &
SoDrawList::getCommandGeometry(const SoRenderCommand & command) const
{
  const SoGeometryResource * resource =
    this->getGeometryResource(command.geometryHandle);
  return resource ? resource->geometry : command.geometry;
}

const std::vector<SoRenderElementRange> &
SoDrawList::getCommandElementRanges(const SoRenderCommand & command) const
{
  if (command.pick.useResourceElementRanges) {
    const SoGeometryResource * resource =
      this->getGeometryResource(command.geometryHandle);
    if (resource) return resource->elementRanges;
  }
  return command.pick.elementRanges;
}

void
SoDrawList::addDepthClearEvent(const SoDepthClearEvent & event)
{
  ++this->contentRevision;
  ++this->renderPlanRevision;
  ++this->resourceRevision;
  SoDepthClearEvent recorded = event;
  recorded.sequence = std::min(recorded.sequence,
                               static_cast<uint32_t>(this->getNumCommands()));
  this->depthClearEvents.push_back(recorded);
}

int
SoDrawList::getNumCommands() const
{
  return static_cast<int>(compactCommandsActive ? compactCommands.size() : commands.size());
}

SoRenderCommand &
SoDrawList::getCommand(int i)
{
  ++this->contentRevision;
  ++this->renderPlanRevision;
  ++this->resourceRevision;
  this->pickLUT.clear();
  this->pickLUTGeneration = 0;
  this->pickLUTValid = false;
  this->expandCommands();
  return this->commands[static_cast<size_t>(i)];
}

const SoRenderCommand &
SoDrawList::getCommand(int i) const
{
  this->expandCommands();
  return this->commands[static_cast<size_t>(i)];
}

SoLightingHandle
SoDrawList::addLightingSetup(const SoLightingData & lighting)
{
  for (size_t i = 0; i < this->lightingSetups.size(); ++i) {
    if (lightingEqual(this->lightingSetups[i], lighting)) {
      return static_cast<SoLightingHandle>(i + 1);
    }
  }
  this->lightingSetups.push_back(lighting);
  return static_cast<SoLightingHandle>(this->lightingSetups.size());
}

const SoLightingData *
SoDrawList::getLighting(SoLightingHandle handle) const
{
  if (handle == 0) {
    return nullptr;
  }
  const size_t index = static_cast<size_t>(handle - 1);
  if (index >= this->lightingSetups.size()) {
    return nullptr;
  }
  return &this->lightingSetups[index];
}

void
SoDrawList::buildPickLUT() const
{
  if (this->pickLUTValid) return;
  this->pickLUT.clear();
  this->pickLUTGeneration = this->generation;
  ++this->pickLUTRevision;
  this->pickLUTValid = true;

  for (int commandIndex = 0; commandIndex < this->getNumCommands(); ++commandIndex) {
    const SoRenderCommand & command = this->getCommandSnapshot(commandIndex);
    if (!command.pick.pickable || !command.state.raster.visible) continue;

    const bool indexed = command.geometry.indices != nullptr &&
      command.geometry.indexCount != 0;
    const uint32_t drawLimit = indexed ? command.geometry.indexCount
                                       : command.geometry.vertexCount;
    if (drawLimit == 0) continue;

    const std::vector<SoRenderElementRange> & elementRanges =
      this->getCommandElementRanges(command);
    if (elementRanges.empty()) {
      SoPickLUTEntry entry;
      entry.commandIndex = commandIndex;
      entry.objectId = command.objectId;
      entry.type = SO_PICK_OBJECT;
      entry.elementIndex = -1;
      entry.drawStart = 0;
      entry.drawCount = drawLimit;
      this->pickLUT.push_back(entry);
      continue;
    }

    for (const SoRenderElementRange & range : elementRanges) {
      SoPickLUTEntry entry;
      entry.commandIndex = commandIndex;
      entry.objectId = command.objectId;
      entry.type = range.type;
      entry.elementIndex = range.elementIndex;
      if (range.drawCount == 0 || range.drawStart >= drawLimit ||
          range.drawCount > drawLimit - range.drawStart) {
        continue;
      }
      entry.drawStart = range.drawStart;
      entry.drawCount = range.drawCount;
      this->pickLUT.push_back(entry);
    }
  }
}

const SoPickLUTEntry *
SoDrawList::resolvePickId(uint32_t id) const
{
  if (id == 0 || id > this->pickLUT.size()) return nullptr;
  if (this->pickLUTGeneration != this->generation) return nullptr;
  return &this->pickLUT[static_cast<size_t>(id - 1)];
}

SoRenderCommand *
SoDrawList::begin()
{
  ++this->contentRevision;
  ++this->renderPlanRevision;
  ++this->resourceRevision;
  this->pickLUT.clear();
  this->pickLUTGeneration = 0;
  this->pickLUTValid = false;
  this->expandCommands();
  return this->commands.empty() ? nullptr : this->commands.data();
}

SoRenderCommand *
SoDrawList::end()
{
  ++this->contentRevision;
  ++this->renderPlanRevision;
  ++this->resourceRevision;
  this->pickLUT.clear();
  this->pickLUTGeneration = 0;
  this->pickLUTValid = false;
  this->expandCommands();
  return this->commands.empty() ? nullptr : this->commands.data() + static_cast<size_t>(this->getNumCommands());
}

const SoRenderCommand *
SoDrawList::begin() const
{
  this->expandCommands();
  return this->commands.empty() ? nullptr : this->commands.data();
}

const SoRenderCommand *
SoDrawList::end() const
{
  this->expandCommands();
  return this->commands.empty() ? nullptr : this->commands.data() + static_cast<size_t>(this->getNumCommands());
}

static const char *
renderstage_name(SoRenderStage stage)
{
  switch (stage) {
  case SoRenderStage::Background: return "background";
  case SoRenderStage::Main: return "main";
  case SoRenderStage::AfterMain: return "after-main";
  case SoRenderStage::Foreground: return "foreground";
  default: return "unknown";
  }
}

void
SoIRDumpSummary(const SoDrawList & drawlist)
{
  if (!coin_render_ir_trace_enabled()) {
    return;
  }

  uint32_t minVerts = UINT32_MAX;
  uint32_t maxVerts = 0;
  const int num = drawlist.getNumCommands();
  for (int i = 0; i < num; ++i) {
    const SoRenderCommand & cmd = drawlist.getCommandSnapshot(i);
    const uint32_t vc = cmd.geometry.vertexCount;
    minVerts = std::min(minVerts, vc);
    maxVerts = std::max(maxVerts, vc);
  }

  SoDebugError::postInfo("SoDrawList",
                         "commands=%d minVerts=%u maxVerts=%u",
                         num,
                         minVerts == UINT32_MAX ? 0 : minVerts,
                         maxVerts);
}

void
SoIRDumpFirstN(const SoDrawList & drawlist, int count)
{
  if (!coin_render_ir_trace_enabled()) {
    return;
  }

  const int num = drawlist.getNumCommands();
  const int limit = std::min(num, count);
  for (int i = 0; i < limit; ++i) {
    const SoRenderCommand & cmd = drawlist.getCommandSnapshot(i);
    const SbVec4f & diffuse = cmd.material.diffuse;
    const SoLightingData * lighting = drawlist.getLighting(cmd.lightingHandle);
    int numlights = lighting ? static_cast<int>(lighting->lights.size()) : -1;
    SbVec3f ambient(0.0f, 0.0f, 0.0f);
    if (lighting) {
      ambient = lighting->ambient;
    }
    SoDebugError::postInfo("SoDrawList",
                           "[%d] stage=%s depth=%d topo=%d verts=%u idx=%u colors=%p diffuse=(%.3f, %.3f, %.3f, %.3f) lights=%d ambient=(%.3f, %.3f, %.3f)",
                           i,
                           renderstage_name(cmd.stage),
                           cmd.state.depth.enabled,
                           static_cast<int>(cmd.geometry.topology),
                           cmd.geometry.vertexCount,
                           cmd.geometry.indexCount,
                           cmd.geometry.colors,
                           diffuse[0],
                           diffuse[1],
                           diffuse[2],
                           diffuse[3],
                           numlights,
                           ambient[0],
                           ambient[1],
                           ambient[2]);
  }
}

void
SoIRRenderContext::captureFromState(SoState * state)
{
  *this = SoIRRenderContext();
  if (!state) return;

  if (state->isElementEnabled(SoEnvironmentElement::getClassStackIndex()) &&
      state->isElementEnabled(SoLightAttenuationElement::getClassStackIndex()) &&
      state->isElementEnabled(SoLightElement::getClassStackIndex())) {
    SoRenderIR::captureLightingFromState(state, this->lighting);
    this->hasLighting = TRUE;
  }

  if (state->isElementEnabled(SoViewportRegionElement::getClassStackIndex())) {
    this->viewport = SoViewportRegionElement::get(state);
    this->hasViewport = TRUE;
  }
  if (state->isElementEnabled(SoModelMatrixElement::getClassStackIndex())) {
    this->modelMatrix = SoModelMatrixElement::get(state);
    this->hasModelMatrix = TRUE;
  }
  if (state->isElementEnabled(SoViewVolumeElement::getClassStackIndex())) {
    this->viewVolume = SoViewVolumeElement::get(state);
    this->hasViewVolume = TRUE;
  }
  if (state->isElementEnabled(SoViewingMatrixElement::getClassStackIndex())) {
    this->viewingMatrix = SoViewingMatrixElement::get(state);
    this->hasViewingMatrix = TRUE;
  }
  if (state->isElementEnabled(SoProjectionMatrixElement::getClassStackIndex())) {
    this->projectionMatrix = SoProjectionMatrixElement::get(state);
    this->hasProjectionMatrix = TRUE;
  }
  if (state->isElementEnabled(SoDevicePixelRatioElement::getClassStackIndex())) {
    this->devicePixelRatio = SoDevicePixelRatioElement::get(state);
    this->hasDevicePixelRatio = TRUE;
  }
}

void
SoIRRenderContext::applyToState(SoState * state, SbBool applyModelMatrix) const
{
  if (!state) return;

  if (applyModelMatrix && this->hasModelMatrix) {
    SoModelMatrixElement::set(state, nullptr, this->modelMatrix);
  }
  if (this->hasViewport) {
    SoViewportRegionElement::set(state, this->viewport);
  }
  if (this->hasViewVolume) {
    SoViewVolumeElement::set(state, nullptr, this->viewVolume);
  }
  if (this->hasViewingMatrix) {
    SoViewingMatrixElement::set(state, nullptr, this->viewingMatrix);
  }
  if (this->hasProjectionMatrix) {
    SoProjectionMatrixElement::set(state, nullptr, this->projectionMatrix);
  }
  if (this->hasDevicePixelRatio) {
    SoDevicePixelRatioElement::set(state, this->devicePixelRatio);
  }
}

namespace SoRenderIR {

static void fillRenderStateFromSnapshot(
  SoState * state, SoRenderState & renderState,
  const SoLazyElementP::RenderSnapshot & lazy);

static void fillPBRMaterialFromState(SoState * state, SoMaterialData & material)
{
  material.pbr.reset();
  const auto * captured = SoPBRMaterialElement::get(state);
  if (!captured || captured->model != SoRenderMaterialModel::PBR) return;
  material.pbr = captured->resolved;
  material.shadingModel = SO_SHADING_PBR_METALLIC_ROUGHNESS;
  material.opacity = 1.0f;
  material.diffuse[3] = 1.0f;
  material.screenDoorTransparency = 0.0f;
}

static void
fillMaterialFromSnapshot(const SoLazyElementP::RenderSnapshot & lazy,
                         SoMaterialData & material)
{
  // Keep diffuse and emissive independent. The explicit lighting shader owns
  // emissive contribution and must not infer it from default-looking diffuse.
  material.diffuse.setValue(lazy.diffuse[0], lazy.diffuse[1], lazy.diffuse[2],
                            1.0f - lazy.transparency);
  // PHONG retains Coin's legacy Gouraud contract until a distinct
  // per-fragment shading model is represented in the IR.
  material.shadingModel = lazy.lightModel == SoLightModelElement::BASE_COLOR
    ? SO_SHADING_UNLIT : SO_SHADING_LEGACY_GOURAUD;
  material.twoSidedLighting = lazy.twoSidedLighting != FALSE;
  material.ambient.setValue(
    lazy.ambient[0], lazy.ambient[1], lazy.ambient[2], 1.0f);
  material.specular.setValue(
    lazy.specular[0], lazy.specular[1], lazy.specular[2], 1.0f);
  material.emissive.setValue(
    lazy.emissive[0], lazy.emissive[1], lazy.emissive[2], 1.0f);
  material.shininess = lazy.shininess;
  material.opacity = 1.0f - lazy.transparency;
  material.texture = SoTextureData();
  material.textureAlphaIncludesOpacity = false;
  material.vertexColorAlphaIncludesOpacity = false;
}

static void fillTextureFromState(SoState * state, SoIRRenderAction * action,
                                 SoMaterialData & material);

static SoTextureWrap
textureWrapFromLegacy(SoMultiTextureImageElement::Wrap wrap)
{
  switch (wrap) {
  case SoMultiTextureImageElement::REPEAT:
    return SO_TEXTURE_WRAP_REPEAT;
  case SoMultiTextureImageElement::CLAMP_TO_BORDER:
    return SO_TEXTURE_WRAP_CLAMP_TO_BORDER;
  case SoMultiTextureImageElement::CLAMP:
  default:
    // GL_CLAMP is the historical Coin spelling for edge clamping here.
    return SO_TEXTURE_WRAP_CLAMP_TO_EDGE;
  }
}

static bool
hasTexture(const SoMaterialData & material)
{
  return material.texture.pixels != nullptr &&
    material.texture.width > 0 && material.texture.height > 0 &&
    material.texture.numComponents > 0;
}

static bool
textureHasTransparency(const SoTextureData & texture)
{
  // DECAL uses texture alpha as a color interpolation factor; it does not
  // change the fragment alpha that controls coverage or blending.
  if (texture.model == SO_TEXTURE_MODEL_DECAL) return false;
  if (texture.hasTransparency) return true;
  if (!texture.pixels || texture.width <= 0 || texture.height <= 0 ||
      (texture.numComponents != 2 && texture.numComponents != 4)) {
    return false;
  }
  const size_t pixelCount = static_cast<size_t>(texture.width) *
                            static_cast<size_t>(texture.height);
  for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
    const size_t alpha = pixel * static_cast<size_t>(texture.numComponents) +
                         static_cast<size_t>(texture.numComponents - 1);
    if (texture.pixels[alpha] != 0xffu) return true;
  }
  return false;
}

static void
fillCommandTraversalStateFromSnapshot(
  SoIRRenderAction * action, SoRenderCommand & command,
  const SoLazyElementP::RenderSnapshot & lazy)
{
  SoState * state = action->getState();
  SoDrawList & drawlist = action->getMutableDrawList();
  command.modelMatrix = SoModelMatrixElement::get(state);
  command.viewMatrix = SoViewingMatrixElement::get(state);
  command.projMatrix = SoProjectionMatrixElement::get(state);
  fillRenderStateFromSnapshot(state, command.state, lazy);
  if (command.state.clipPlaneCount > 8)
    action->markUnsupported(action->getCurPathTail(), "More than eight retained clipping planes");
  command.lightingHandle = fillLightingFromState(state, drawlist);
  command.pick.pickable = SoPickStyleElement::get(state) !=
    SoPickStyleElement::UNPICKABLE;
}

void
fillCommandTraversalStateFromAction(SoIRRenderAction * action,
                                    SoRenderCommand & command)
{
  fillCommandTraversalStateFromSnapshot(
    action, command,
    SoLazyElementP::captureRenderSnapshot(action->getState(), 0));
}

void
fillCommandStateFromAction(SoIRRenderAction * action,
                           SoRenderCommand & command,
                           const int materialIndex)
{
  SoState * state = action->getState();
  const SoLazyElementP::RenderSnapshot lazy =
    SoLazyElementP::captureRenderSnapshot(state, materialIndex);
  fillCommandTraversalStateFromSnapshot(action, command, lazy);
  fillMaterialFromSnapshot(lazy, command.material);
  command.material.screenDoorTransparency = SoLazyElement::getTransparency(state, 0);
  fillTextureFromState(state, action, command.material);
  fillPBRMaterialFromState(state, command.material);
  if(command.material.pbr) {
    const auto & factors=*command.material.pbr;
    if(factors.baseColorTextureUnit>=0 || factors.ormTextureUnit>=0 || factors.emissiveTextureUnit>=0 || factors.anisotropy>0 || SoBumpMapElement::getSource(state)) {
    auto surface=std::make_shared<SoPBRSurfaceData>();SbString error;
    if(!capturePBRSurfaceFromState(state,action,*surface,error))action->markUnsupported(action->getCurPathTail(),error.getString());
    bool used=factors.anisotropy>0 || surface->normalMap.pixels!=nullptr;
    for(const auto & image:surface->textures)used=used||image.pixels;
    if(used)command.material.pbrSurface=std::move(surface);
    }
    command.material.texture=SoTextureData(); // PBR image alpha is not surface coverage.
  }
  if(!command.material.pbr) {
    int last=-1;SoMultiTextureEnabledElement::getEnabledUnits(state,last);
    if(last>0)action->markUnsupported(action->getCurPathTail(),"Legacy multitexture requires native resource descriptors");
    const auto * bump=SoBumpMapElement::getSource(state);
    if(bump && bump->getTypeId()==SoBumpMap::getClassTypeId()) {
      SbVec2s size;int nc;static_cast<const SoBumpMap *>(bump)->image.getValue(size,nc);
      if(size[0]>0&&size[1]>0)action->markUnsupported(action->getCurPathTail(),"Legacy bump resources are not captured by this retained profile");
    }
  }
  if (command.material.pbr && (lazy.packedVertexColors ||
      SoMaterialBindingElement::get(state) != SoMaterialBindingElement::OVERALL))
    action->markUnsupported(action->getCurPathTail(), "Retained PBR requires overall material binding without packed colors");
  if (command.material.pbr && !so_valid_pbr_material(*command.material.pbr))
    action->markUnsupported(action->getCurPathTail(), "Invalid retained PBR material factors");
  if (lazy.packedVertexColors) {
    command.material.opacity = lazy.packedOpacity;
    command.material.diffuse[3] = lazy.packedOpacity;
  }
  command.material.vertexColorAlphaIncludesOpacity =
    command.geometry.colors != nullptr && !lazy.packedVertexColors;
}

void
fillMaterialFromState(SoState * state, SoMaterialData & material,
                      int materialIndex)
{
  fillMaterialFromSnapshot(
    SoLazyElementP::captureRenderSnapshot(state, materialIndex), material);
  fillPBRMaterialFromState(state, material);
}

static void
fillTextureFromState(SoState * state, SoIRRenderAction * action,
                     SoMaterialData & material)
{
  if (!state || !action || !SoMultiTextureEnabledElement::get(state, 0)) {
    return;
  }

  const float quality = SoTextureQualityElement::get(state);
  if (!std::isfinite(quality) || quality < 0 || quality > 1) {
    action->markUnsupported(action->getCurPathTail(), "Texture quality must be finite in [0,1]");
    return;
  }
  if (quality == 0) return;
  SbVec2s size;
  int numComponents = 0;
  SoMultiTextureImageElement::Wrap wrapS;
  SoMultiTextureImageElement::Wrap wrapT;
  SoMultiTextureImageElement::Model model;
  SbColor blendColor;
  const unsigned char * bytes = SoMultiTextureImageElement::get(
    state, 0, size, numComponents, wrapS, wrapT, model, blendColor);
  if (!bytes || size[0] <= 0 || size[1] <= 0 ||
      numComponents < 1 || numComponents > 4) {
    return;
  }

  const size_t pixelCount = static_cast<size_t>(size[0]) *
                            static_cast<size_t>(size[1]);
  const size_t byteCount = pixelCount * static_cast<size_t>(numComponents);
  bool hasTransparency = false;
  const unsigned char * copy = action->allocateTextureStorage(
    bytes, byteCount, size[0], size[1], numComponents, hasTransparency);

  material.texture.matrix = SoMultiTextureMatrixElement::get(state, 0);
  material.texture.pixels = copy;
  // The image element owns the source bytes and changes its node id whenever
  // the producing texture node changes. Preserve both pieces so backends can
  // retain one upload even though the IR owns a separate frame-local copy.
  material.texture.cacheKey = static_cast<uint64_t>(
    reinterpret_cast<uintptr_t>(bytes));
  material.texture.revision = static_cast<uint64_t>(
    SoMultiTextureImageElement::getNodeId(state, 0));
  material.texture.width = size[0];
  material.texture.height = size[1];
  material.texture.numComponents = numComponents;
  material.texture.hasTransparency = hasTransparency;
  material.texture.wrapS = textureWrapFromLegacy(wrapS);
  material.texture.wrapT = textureWrapFromLegacy(wrapT);
  material.texture.model = textureModelFromLegacy(model);
  material.texture.blendColor.setValue(blendColor[0], blendColor[1],
                                       blendColor[2], 1.0f);
  textureFiltersFromQuality(action->getTextureUploadQuality(SoMultiTextureImageElement::getNodeId(state, 0), quality),
                            material.texture);
}

SbBool capturePBRSurfaceFromState(SoState * state,SoIRRenderAction * action,SoPBRSurfaceData & surface,SbString & error) {
  const auto fail=[&](const char * text){error=text;return FALSE;};
  const auto * physical=SoPBRMaterialElement::get(state);
  if(!physical || physical->model!=SoRenderMaterialModel::PBR)return TRUE;
  const auto & m=physical->material;
  surface.directionCoordinatesCaptured=m.anisotropy>0;
  surface.normalCoordinateUnit=m.tangentCoordinateUnit;
  surface.directionMatrix=SoMultiTextureMatrixElement::get(state,m.tangentCoordinateUnit);
  surface.normalMatrix=surface.directionMatrix;
  const auto freeze=[&](const unsigned char * bytes,SoTextureData & image) {
    image.pixels=bytes;
    if(action) {bool alpha=false;image.pixels=action->allocateTextureStorage(bytes,size_t(image.width)*image.height*image.numComponents,image.width,image.height,image.numComponents,alpha);}
  };
  const float quality=SoTextureQualityElement::get(state);
  if(!std::isfinite(quality)||quality<0||quality>1)return fail("Invalid PBR texture quality");
  for(int unit:{m.baseColorTextureUnit,m.ormTextureUnit,m.emissiveTextureUnit})if(unit>=0) {
    if(unit>7||!SoMultiTextureEnabledElement::get(state,unit)||quality==0)return fail("PBR texture role requires an enabled stored 2D image");
    auto & image=surface.textures[unit];
    if(image.pixels)continue;
    SbVec2s size;int nc;SoMultiTextureImageElement::Wrap ws,wt;SoMultiTextureImageElement::Model model;SbColor blend;
    const auto * bytes=SoMultiTextureImageElement::get(state,unit,size,nc,ws,wt,model,blend);
    if(!bytes||size[0]<=0||size[1]<=0||nc<1||nc>4)return fail("PBR texture has no decoded image");
    image.width=size[0];image.height=size[1];image.numComponents=nc;
    image.wrapS=textureWrapFromLegacy(ws);image.wrapT=textureWrapFromLegacy(wt);
    image.matrix=SoMultiTextureMatrixElement::get(state,unit);
    image.cacheKey=reinterpret_cast<uintptr_t>(bytes);image.revision=SoMultiTextureImageElement::getNodeId(state,unit);
    textureFiltersFromQuality(quality,image);freeze(bytes,image);
  }
  const auto * source=SoBumpMapElement::getSource(state);
  if(source && source->getTypeId()==SoBumpMap::getClassTypeId()) {
    const auto * bump=static_cast<const SoBumpMap *>(source);SbVec2s size;int nc;
    const auto * bytes=bump->image.getValue(size,nc);
    if(bytes&&size[0]>0&&size[1]>0) {
      if(SoBumpMapCoordinateElement::getInstance(state)->getNum())return fail("Explicit bump coordinates are not connected to retained PBR yet");
      auto & image=surface.normalMap;image.width=size[0];image.height=size[1];image.numComponents=nc;
      image.wrapS=bump->wrapS.getValue()==SoBumpMap::REPEAT?SO_TEXTURE_WRAP_REPEAT:SO_TEXTURE_WRAP_CLAMP_TO_EDGE;
      image.wrapT=bump->wrapT.getValue()==SoBumpMap::REPEAT?SO_TEXTURE_WRAP_REPEAT:SO_TEXTURE_WRAP_CLAMP_TO_EDGE;
      image.cacheKey=reinterpret_cast<uintptr_t>(bytes);image.revision=bump->getNodeId();
      textureFiltersFromQuality(1,image);freeze(bytes,image);
      surface.normalCoordinateUnit=m.tangentCoordinateUnit;
      surface.normalMatrix=SoMultiTextureMatrixElement::get(state,m.tangentCoordinateUnit)*SoBumpMapMatrixElement::get(state);
    }else if(bump->filename.getValue().getLength())return fail("PBR normal map has no decoded image");
  }
  return TRUE;
}

static void
fillRenderStateFromSnapshot(SoState * state, SoRenderState & rs,
                            const SoLazyElementP::RenderSnapshot & lazy)
{
  SoState * mutableState = state;
  captureCameraFromState(state, rs.camera);
  const auto * planes = state->isElementEnabled(SoClipPlaneElement::getClassStackIndex())
    ? SoClipPlaneElement::getInstance(state) : nullptr;
  rs.clipPlaneCount = planes ? static_cast<uint32_t>(planes->getNum()) : 0;
  for (size_t i = 0; i < std::min<size_t>(8, rs.clipPlaneCount); ++i)
    rs.clipPlanesWorld[i] = planes->get(static_cast<int>(i), TRUE);
  float ambientIntensity, visibility, fogStart;
  SbColor ambientColor, fogColor;
  SbVec3f attenuation;
  int32_t fogType;
  SoEnvironmentElement::get(state, ambientIntensity, ambientColor, attenuation,
                            fogType, fogColor, visibility, fogStart);
  rs.fog.mode = static_cast<uint8_t>(fogType);
  rs.fog.color.setValue(fogColor[0], fogColor[1], fogColor[2]);
  rs.fog.start = fogStart;
  rs.fog.visibility = visibility;
  rs.useCommandMatrices =
    SoRenderMatrixPolicyElement::get(mutableState) ==
      SoRenderMatrixPolicyElement::CAPTURE_CURRENT_MATRICES;
  SbBool depthtest = TRUE;
  SbBool depthwrite = TRUE;
  SoDepthBufferElement::DepthWriteFunction depthfunc =
    SoDepthBufferElement::LEQUAL;
  SbVec2f range;
  SoDepthBufferElement::get(mutableState, depthtest, depthwrite, depthfunc, range);

  rs.depth.explicitMask = state->isElementEnabled(SoRenderDepthPolicyElement::getClassStackIndex())
    ? SoRenderDepthPolicyElement::get(state) : 0;
  rs.depth.enabled = depthtest;
  rs.depth.writeEnabled = depthwrite;
  rs.depth.func = static_cast<SoDepthFunction>(depthfunc);
  rs.depth.range = range;

  int srcfactor = 0;
  int dstfactor = 0;
  rs.blend.enabled = lazy.blending;
  srcfactor = lazy.blendSource;
  dstfactor = lazy.blendDestination;
  rs.blend.srcRGBFactor = blendFactorFromLegacyGL(srcfactor);
  rs.blend.dstRGBFactor = blendFactorFromLegacyGL(dstfactor);

  // Ordinary LegacyGL blending applies the RGB factors to alpha as well.
  // Only explicit separate-alpha state supplies different alpha factors.
  int srcAlphaFactor = 0;
  int dstAlphaFactor = 0;
  srcAlphaFactor = lazy.alphaBlendSource;
  dstAlphaFactor = lazy.alphaBlendDestination;
  if (lazy.blending && lazy.separateBlending) {
    rs.blend.srcAlphaFactor = blendFactorFromLegacyGL(srcAlphaFactor);
    rs.blend.dstAlphaFactor = blendFactorFromLegacyGL(dstAlphaFactor);
  } else {
    rs.blend.srcAlphaFactor = rs.blend.srcRGBFactor;
    rs.blend.dstAlphaFactor = rs.blend.dstRGBFactor;
  }


  // LegacyGL does not expose a Coin state element for blend equations. ADD
  // is its effective equation and is the only value that can be captured
  // deterministically from traversal.
  rs.blend.rgbEquation = SO_BLEND_EQUATION_ADD;
  rs.blend.alphaEquation = SO_BLEND_EQUATION_ADD;

  const float alphaTestValue = lazy.alphaTestValue;
  const int alphaTestFunction = lazy.alphaTestFunction;
  rs.alphaTest.function = alphaTestFunctionFromLegacyGL(alphaTestFunction);
  rs.alphaTest.reference = std::isnan(alphaTestValue) ? alphaTestValue : std::max(0.0f, std::min(1.0f, alphaTestValue));
  rs.alphaTest.policy = rs.alphaTest.function == SO_ALPHA_TEST_NONE
    ? SO_ALPHA_TEST_POLICY_NONE
    : SO_ALPHA_TEST_POLICY_EXPLICIT;

  SoDrawStyleElement::Style style = SoDrawStyleElement::get(mutableState);
  const SoShapeStyleElement * shapeStyle = SoShapeStyleElement::get(mutableState);
  rs.raster.visible = style != SoDrawStyleElement::INVISIBLE &&
    (!shapeStyle || !(shapeStyle->getFlags() & SoShapeStyleElement::INVISIBLE));
  SoRasterFillMode fillmode = SO_RASTER_FILL;
  switch (style) {
  case SoDrawStyleElement::LINES:
    fillmode = SO_RASTER_LINES;
    break;
  case SoDrawStyleElement::POINTS:
    fillmode = SO_RASTER_POINTS;
    break;
  default:
    fillmode = SO_RASTER_FILL;
    break;
  }
  rs.raster.fillMode = fillmode;
  // Native GL_POINTS are square unless point smoothing is enabled. Keep the
  // primitive shape explicit in the IR so backends do not choose independently.

  // Backface culling from SoShapeHintsElement.  Vertex ordering selects the
  // front-face winding; a solid shape requests back-face culling regardless
  // of whether its front faces are clockwise or counter-clockwise.
  {
    SoShapeHintsElement::VertexOrdering vo;
    SoShapeHintsElement::ShapeType st;
    SoShapeHintsElement::FaceType ft;
    SoShapeHintsElement::get(mutableState, vo, st, ft);
    rs.raster.cullBackFaces = st == SoShapeHintsElement::SOLID &&
      (vo == SoShapeHintsElement::CLOCKWISE ||
       vo == SoShapeHintsElement::COUNTERCLOCKWISE);
    rs.raster.frontFaceCCW = vo != SoShapeHintsElement::CLOCKWISE;
  }
  rs.raster.scissorEnabled = FALSE;
  rs.raster.lineWidth = SoLineWidthElement::get(mutableState);
  rs.raster.pointSize = SoPointSizeElement::get(mutableState);
  rs.raster.linePattern = static_cast<uint16_t>(
    SoLinePatternElement::get(mutableState));
  rs.raster.linePatternScale = static_cast<int16_t>(std::max(
    1, SoLinePatternElement::getScaleFactor(mutableState)));

  int viewportX = 0;
  int viewportY = 0;
  int viewportWidth = 0;
  int viewportHeight = 0;
  if (SoRenderPlacementElement::getViewport(mutableState,
                                            viewportX, viewportY,
                                            viewportWidth, viewportHeight)) {
    rs.raster.viewportOverride = TRUE;
    rs.raster.viewportEnabled = viewportWidth > 0 && viewportHeight > 0;
    rs.raster.viewportX = viewportX;
    rs.raster.viewportY = viewportY;
    rs.raster.viewportWidth = viewportWidth;
    rs.raster.viewportHeight = viewportHeight;
  }
  else {
    rs.raster.viewportOverride = FALSE;
    const SbViewportRegion & viewport = SoViewportRegionElement::get(mutableState);
    const SbVec2s & viewportOrigin = viewport.getViewportOriginPixels();
    const SbVec2s & viewportSize = viewport.getViewportSizePixels();
    rs.raster.viewportEnabled = viewportSize[0] > 0 && viewportSize[1] > 0;
    rs.raster.viewportX = viewportOrigin[0];
    rs.raster.viewportY = viewportOrigin[1];
    rs.raster.viewportWidth = viewportSize[0];
    rs.raster.viewportHeight = viewportSize[1];
  }

  float offsetfactor = 0.0f;
  float offsetunits = 0.0f;
  SoPolygonOffsetElement::Style offsetstyle = SoPolygonOffsetElement::FILLED;
  SbBool offseton = FALSE;
  SoPolygonOffsetElement::get(mutableState, offsetfactor, offsetunits,
                              offsetstyle, offseton);
  if (!offseton) {
    offsetfactor = 0.0f;
    offsetunits = 0.0f;
  }
  rs.raster.polygonOffsetFactor = offsetfactor;
  rs.raster.polygonOffsetUnits = offsetunits;
  rs.raster.polygonOffsetFilled = offseton &&
    (offsetstyle & SoPolygonOffsetElement::FILLED);
  rs.raster.polygonOffsetLines = offseton &&
    (offsetstyle & SoPolygonOffsetElement::LINES);
  rs.raster.polygonOffsetPoints = offseton &&
    (offsetstyle & SoPolygonOffsetElement::POINTS);
}

void captureCameraFromState(SoState * state, SoCameraData & camera) {
  camera = SoCameraData();
  if (state->isElementEnabled(SoViewVolumeElement::getClassStackIndex())) {
    const auto * element = static_cast<const SoViewVolumeElement *>(state->getConstElement(SoViewVolumeElement::getClassStackIndex()));
    camera.sourceRevision = element->getNodeId();
    const auto & volume = SoViewVolumeElement::get(state);
    camera.perspective = volume.getProjectionType() == SbViewVolume::PERSPECTIVE;
    camera.nearDistance = volume.getNearDist();
    camera.farDistance = volume.getNearDist() + volume.getDepth();
  }
  if (state->isElementEnabled(SoFocalDistanceElement::getClassStackIndex()))
    camera.focalDistance = SoFocalDistanceElement::get(state);
  camera.aspectRatio = SoViewportRegionElement::get(state).getViewportAspectRatio();
}

void
fillRenderStateFromState(SoState * state, SoRenderState & renderState)
{
  fillRenderStateFromSnapshot(
    state, renderState, SoLazyElementP::captureRenderSnapshot(state, 0));
}

void
captureLightingFromState(SoState * state, SoLightingData & lighting)
{
  const SbColor & ambientColor = SoEnvironmentElement::getAmbientColor(state);
  const float ambientIntensity = SoEnvironmentElement::getAmbientIntensity(state);
  lighting.ambient.setValue(ambientColor[0] * ambientIntensity,
                            ambientColor[1] * ambientIntensity,
                            ambientColor[2] * ambientIntensity);

  const SoNodeList & lights = SoLightElement::getLights(state);
  const int numLights = lights.getLength();
  lighting.lights.reserve(numLights);

  for (int i = 0; i < numLights; ++i) {
    SoLight * light = static_cast<SoLight *>(lights[i]);
    if (!light || !light->on.getValue()) {
      continue;
    }

    const SbColor lightColor = light->color.getValue();
    SoLightData lightData;
    lightData.color.setValue(lightColor[0] * light->intensity.getValue(),
                             lightColor[1] * light->intensity.getValue(),
                             lightColor[2] * light->intensity.getValue());

    const SbMatrix & lightMatrix = SoLightElement::getMatrix(state, i);

    if (light->isOfType(SoDirectionalLight::getClassTypeId())) {
      SoDirectionalLight * directional = static_cast<SoDirectionalLight *>(light);
      lightData.type = SO_LIGHT_DIRECTIONAL;
      lightMatrix.multDirMatrix(-(directional->direction.getValue()), lightData.direction);
      if (lightData.direction.normalize() == 0.0f) {
        lightData.direction.setValue(0.0f, 0.0f, 1.0f);
      }
    }
    else if (light->isOfType(SoPointLight::getClassTypeId())) {
      SoPointLight * point = static_cast<SoPointLight *>(light);
      lightData.type = SO_LIGHT_POINT;
      lightData.attenuation = SoLightElement::getAttenuation(state, i);
      lightMatrix.multVecMatrix(point->location.getValue(), lightData.position);
    }
    else if (light->isOfType(SoSpotLight::getClassTypeId())) {
      SoSpotLight * spot = static_cast<SoSpotLight *>(light);
      lightData.type = SO_LIGHT_SPOT;
      lightData.attenuation = SoLightElement::getAttenuation(state, i);
      lightMatrix.multVecMatrix(spot->location.getValue(), lightData.position);
      lightMatrix.multDirMatrix(spot->direction.getValue(), lightData.direction);
      if (lightData.direction.normalize() == 0.0f) {
        lightData.direction.setValue(0.0f, 0.0f, -1.0f);
      }
      float cutoff = spot->cutOffAngle.getValue();
      if (cutoff < 0.0f) cutoff = 0.0f;
      if (cutoff > float(M_PI) * 0.5f) cutoff = float(M_PI) * 0.5f;
      lightData.spotCutoffCos = std::cos(cutoff);
      float dropoff = spot->dropOffRate.getValue();
      if (dropoff < 0.0f) dropoff = 0.0f;
      if (dropoff > 1.0f) dropoff = 1.0f;
      lightData.spotExponent = dropoff * 128.0f;
    }
    else {
      continue;
    }

    lighting.lights.push_back(lightData);
  }

}

void
captureRenderContextFromState(SoState * state, SoIRRenderContext & context)
{
  context.captureFromState(state);
}

void
applyRenderContextToState(SoState * state, const SoIRRenderContext & context)
{
  context.applyToState(state);
}

SoLightingHandle
fillLightingFromState(SoState * state, SoDrawList & drawlist)
{
  SoLightingData lighting;
  captureLightingFromState(state, lighting);
  return drawlist.addLightingSetup(lighting);
}

SoLightingHandle
fillLightingFromState(SoState * /* state */,
                      SoDrawList & drawlist,
                      const SoLightingData & lighting)
{
  return drawlist.addLightingSetup(lighting);
}

bool
isMaterialTransparent(const SoMaterialData & material)
{
  return material.opacity < 1.0f || textureHasTransparency(material.texture);
}

void
ensureMaterialBlendState(SoRenderState & renderState,
                         const SoMaterialData & material)
{
  // SoIRRenderAction captures Coin's logical material state, while the
  // legacy GL action enables the conventional blend function as part of its
  // transparency setup. Make that implicit IR contract explicit without
  // replacing an actual non-standard blend state.
  if (renderState.blend.enabled || !isMaterialTransparent(material)) {
    return;
  }

  renderState.blend.enabled = TRUE;
  renderState.blend.srcRGBFactor = SO_BLEND_FACTOR_SRC_ALPHA;
  renderState.blend.dstRGBFactor = SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  renderState.blend.srcAlphaFactor = SO_BLEND_FACTOR_SRC_ALPHA;
  renderState.blend.dstAlphaFactor = SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  renderState.blend.rgbEquation = SO_BLEND_EQUATION_ADD;
  renderState.blend.alphaEquation = SO_BLEND_EQUATION_ADD;
}

void
finalizeCommand(SoRenderCommand & command)
{
  ensureMaterialBlendState(command.state, command.material);
  bool transparent = isMaterialTransparent(command.material);
  if (!transparent && command.geometry.colors) {
    for (uint32_t i = 0; i < command.geometry.vertexCount; ++i) {
      if (command.geometry.colors[i * 4 + 3] < 1.0f) {
        transparent = true;
        break;
      }
    }
  }
  command.opacityClass = transparent
    ? SO_OPACITY_TRANSPARENT : SO_OPACITY_OPAQUE;
  if (transparent && !command.state.blend.enabled) {
    command.state.blend.enabled = TRUE;
    command.state.blend.srcRGBFactor = SO_BLEND_FACTOR_SRC_ALPHA;
    command.state.blend.dstRGBFactor = SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    command.state.blend.srcAlphaFactor = SO_BLEND_FACTOR_SRC_ALPHA;
    command.state.blend.dstAlphaFactor = SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  }
}

} // namespace SoRenderIR
