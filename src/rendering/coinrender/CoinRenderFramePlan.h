#ifndef COIN_RENDER_FRAME_PLAN_H
#define COIN_RENDER_FRAME_PLAN_H

#include <Inventor/CoinRenderExport.h>
#include "rendering/coinrender/CoinRenderTransparencyCore.h"
#include <Inventor/SbColor4f.h>
#include <Inventor/SbColor.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbPlane.h>
#include <Inventor/SbVec2f.h>
#include <Inventor/SbVec2i32.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/misc/SoBase.h>

#include <vector>
#include <string>
#include <cstdint>

static const size_t COIN_RENDER_MAX_TEXTURE_UNITS = 8;

struct CoinRenderVertexSnapshot {
  float position[3] = {0.0f, 0.0f, 0.0f};
  float normal[3] = {0.0f, 0.0f, 1.0f};
  float texcoord[2] = {0.0f, 0.0f};
  uint32_t materialSlot = 0;
  float extraTexcoords[COIN_RENDER_MAX_TEXTURE_UNITS - 1][2] = {};
  float screenSpaceW = 1.0f; // Preserves perspective interpolation after stroke expansion.
  float fogEyeDepth = -1.0f; // Negative means derive depth from model-view.
};

enum class CoinRenderPrimitiveTopology : uint32_t {
  TRIANGLE_LIST = 0,
  LINE_LIST = 1,
  POINT_LIST = 2
};

struct CoinRenderGeometryRange {
  uint32_t firstVertex = 0;
  uint32_t vertexCount = 0;
  uint32_t firstIndex = 0;
  uint32_t indexCount = 0;
};

struct CoinRenderDrawPacket {
  CoinRenderPrimitiveTopology topology = CoinRenderPrimitiveTopology::TRIANGLE_LIST;
  CoinRenderGeometryRange geometry = {};
  uint32_t renderStateSlot = 0;
  uint32_t frameNodeOrdinal = 0; // estável apenas dentro do frame/log
  SbUniqueId sourceNodeId = 0;   // cache/invalidação; não entra no golden log
  uint64_t stableNodeId = 0;
  uint32_t drawOrdinal = 0;
  uint64_t sourceRevision = 0;
  bool hasSortingCenter = false;
  float sortingCenterWorld[3] = {0, 0, 0};
  uint32_t renderLayer = 0;
  bool clearDepthBefore = false;
  // Capture-only strip identity; consumed and cleared by StrokeCore before GPU submission.
  uint64_t lineStripId = 0; // Zero means independent segments.
};

struct CoinRenderMaterialSnapshot {
  float ambient[4] = {0.2f, 0.2f, 0.2f, 1.0f};
  float diffuse[4] = {0.8f, 0.8f, 0.8f, 1.0f};
  float specular[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float emission[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float shininess = 0.2f;
  float transparency = 0.0f;
};

enum class CoinRenderLightType : uint32_t {
  DIRECTIONAL = 0,
  POINT = 1,
  SPOT = 2
};

static const size_t COIN_RENDER_MAX_LIGHTS = 8;
static const size_t COIN_RENDER_MAX_CLIP_PLANES = 8;

// Contract: Light direction and position are strictly in View Space (camera space).
// Backends consume direction/position directly without applying camera viewMatrix again.
struct CoinRenderLightSourceSnapshot {
  uint64_t sourceRevision = 0; // Matches a Coin shadow light captured at traversal.
  SbMatrix sourceModel = SbMatrix::identity(); // Distinguishes reused light nodes.
  CoinRenderLightType type = CoinRenderLightType::DIRECTIONAL;
  float color[3] = {1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  float direction[3] = {0.0f, 0.0f, -1.0f}; // View space direction
  float position[3] = {0.0f, 0.0f, 0.0f};   // View space position
  float cutOffAngle = 0.785398163f;
  float dropOffRate = 0.0f;
  float attenuation[3] = {0.0f, 0.0f, 1.0f}; // quadratic, linear, constant at light traversal
};

// Coin shadow semantics are captured once by Wiring. Zero group means no
// active SoShadowGroup; style uses SoShadowStyleElement bit flags (cast=1,
// receive=2). Infra must not traverse Coin nodes to recover this state.
struct CoinRenderCameraSnapshot {
  SbMatrix viewMatrix = SbMatrix::identity();
  SbMatrix projectionMatrixCoin = SbMatrix::identity();
  bool isPerspective = true;
  float nearDistance = 0.1f;
  float farDistance = 100.0f;
  float focalDistance = 5.0f;
  float aspectRatio = 1.0f;
};

struct CoinRenderShadowGroupSnapshot {
  uint64_t sourceRevision = 0;
  float intensity = 0.5f;
  float precision = 0.5f;
  float quality = 0.5f;
  float epsilon = 0.00001f;
  float threshold = 0.1f;
  float smoothBorder = 0.0f;
  bool shadowCachingEnabled = true;
  float visibilityNearRadius = -1.0f;
  float visibilityRadius = -1.0f;
  int32_t visibilityFlag = 0;
  bool nested = false;
  bool hasEntryCamera = false;
  CoinRenderCameraSnapshot entryCamera;
  SbMatrix entryModel = SbMatrix::identity();
};

struct CoinRenderShadowLightSnapshot {
  uint32_t groupSlot = 0; // One-based index into shadowGroups.
  uint64_t sourceRevision = 0;
  CoinRenderLightType type = CoinRenderLightType::POINT;
  bool enabled = false;
  bool shadowEligible = false; // Coin/GL: spot or SoShadowDirectionalLight.
  bool hasCustomScene = false;
  bool customSceneDirectShape = false;
  bool customSceneDirectSubtree = false;
  SbUniqueId customSceneNodeId = 0;
  std::vector<SbUniqueId> customSceneShapeNodeIds;
  SbColor color = SbColor(1, 1, 1);
  float intensity = 1.0f;
  SbVec3f attenuation = SbVec3f(0, 0, 1);
  SbMatrix model = SbMatrix::identity();
  SbMatrix modelViewAtLight = SbMatrix::identity();
  SbVec3f position = SbVec3f(0, 0, 0);
  SbVec3f direction = SbVec3f(0, 0, -1);
  float cutOffAngle = 0.785398163f;
  float dropOffRate = 0.0f;
  float nearDistance = -1.0f;
  float farDistance = -1.0f;
  float maxShadowDistance = -1.0f;
  SbVec3f bboxCenter = SbVec3f(0, 0, 0);
  SbVec3f bboxSize = SbVec3f(-1, -1, -1);
};

struct CoinRenderLightingSnapshot {
  std::vector<CoinRenderLightSourceSnapshot> lights;
  float ambientIntensity = 0.2f;
  float ambientColor[3] = {1.0f, 1.0f, 1.0f};
};


struct CoinRenderViewportSnapshot {
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 640;
  int32_t height = 480;
};

enum class CoinRenderCullMode : uint32_t {
  NONE = 0,
  BACK = 1,
  FRONT = 2
};

enum class CoinRenderFrontFace : uint32_t {
  CCW = 0,
  CW = 1
};

enum class CoinRenderDepthFunction : uint32_t {
  NEVER = 0,
  ALWAYS = 1,
  LESS = 2,
  LEQUAL = 3,
  EQUAL = 4,
  GEQUAL = 5,
  GREATER = 6,
  NOTEQUAL = 7
};

enum class CoinRenderLightModel : uint32_t {
  BASE_COLOR = 0,
  PHONG = 1
};

enum class CoinRenderTextureWrap : uint32_t {
  REPEAT = 0,
  CLAMP = 1
};

enum class CoinRenderTextureFilter : uint32_t {
  NEAREST = 0,
  LINEAR = 1
};

enum class CoinRenderTextureModel : uint32_t {
  MODULATE = 0,
  REPLACE = 1,
  DECAL = 2,
  BLEND = 3
};

struct CoinRenderTextureImageSnapshot {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t components = 4; // Canonical RGBA8Unorm
  uint64_t contentDigest = 0;
  uint64_t producerId = 0; // Logical RTT dependency, scoped to one captured graph.
  uint64_t gpuToken = 0;   // Execution-only connector handle; capture never sets it.
  int32_t sceneTransparencyFunction = -1; // -1: image alpha; otherwise captured Coin enum.
  bool gpuOpaque = false; // Proven by an opaque child clear and alpha-preserving blend.
  std::vector<uint8_t> pixelsRgba;
};

struct CoinRenderSamplerSnapshot {
  CoinRenderTextureWrap wrapS = CoinRenderTextureWrap::REPEAT;
  CoinRenderTextureWrap wrapT = CoinRenderTextureWrap::REPEAT;
  CoinRenderTextureFilter filter = CoinRenderTextureFilter::LINEAR;
};

enum class CoinRenderFogMode : uint32_t {
  NONE = 0,
  HAZE = 1,
  FOG = 2,
  SMOKE = 3
};

struct CoinRenderTextureCombineSnapshot {
  float instructions[4][4] = {};
};

struct CoinRenderTextureUnitSnapshot {
  bool enabled = false;
  uint32_t imageSlot = 0, samplerSlot = 0;
  CoinRenderTextureModel model = CoinRenderTextureModel::MODULATE;
  float blendColor[4] = {0, 0, 0, 1};
  SbMatrix matrix = SbMatrix::identity();
};

struct CoinRenderRenderStateSnapshot {
  std::vector<SbPlane> clipPlanesWorld; // Captured at plane traversal, not shape traversal.
  SbMatrix model = SbMatrix::identity();
  SbMatrix view = SbMatrix::identity();
  SbMatrix projectionCoin = SbMatrix::identity();
  uint32_t materialSlot = 0;
  uint32_t lightingSlot = 0;
  uint32_t cameraSlot = 0;
  uint32_t viewportSlot = 0;
  CoinRenderCullMode cullMode = CoinRenderCullMode::BACK;
  CoinRenderFrontFace frontFace = CoinRenderFrontFace::CCW;
  bool depthTest = true;
  bool depthWrite = true;
  uint32_t explicitDepthMask = 0; // test=1, write=2, function=4, range=8; path replay overrides.
  CoinRenderDepthFunction depthFunction = CoinRenderDepthFunction::LESS;
  float depthRange[2] = {0.0f, 1.0f};
  bool polygonOffsetEnabled = false;
  float polygonOffsetFactor = 0.0f;
  float polygonOffsetUnits = 0.0f;
  float polygonOffsetMaxDepth = -1.0f; // Original clipped face; before stroke masking.
  float polygonOffsetSlopeBias = 0.0f; // Resolved original-face bias in window depth.
  uint32_t polygonOffsetStyles = 1;
  uint32_t polygonOffsetPrimitiveStyle = 1; // Retained across line/point expansion.
  uint32_t shadowGroupSlot = 0; // Zero outside active SoShadowGroup.
  uint32_t shadowStyle = 3; // SoShadowStyleElement default: casts and receives.
  CoinRenderLightModel lightModel = CoinRenderLightModel::PHONG;
  float lineWidth = 1.0f;
  float pointSize = 1.0f;
  uint32_t linePattern = 0xffffu;
  int32_t linePatternScaleFactor = 1;
  bool polygonLinePattern = false; // One stipple counter for this original polygon.
  SbMatrix textureMatrix = SbMatrix::identity();
  bool hasTexture = false;
  uint32_t textureImageSlot = 0;
  uint32_t samplerSlot = 0;
  CoinRenderTextureModel textureModel = CoinRenderTextureModel::MODULATE;
  float textureBlendColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float screenDoorTransparency = -1.0f; // Coin stipple uses transparency[0], independently of material binding.
  int32_t transparencyType = 0; // SoGLRenderAction::TransparencyType, captured per draw
  CoinRenderFogMode fogMode = CoinRenderFogMode::NONE;
  float fogColor[3] = {1.0f, 1.0f, 1.0f};
  float fogStart = 0.0f;
  float fogEnd = 10.0f;
  CoinRenderTextureUnitSnapshot extraTextures[COIN_RENDER_MAX_TEXTURE_UNITS - 1];
  CoinRenderTextureCombineSnapshot textureCombines[COIN_RENDER_MAX_TEXTURE_UNITS];
};

inline CoinRenderTextureUnitSnapshot coin_render_texture_unit(const CoinRenderRenderStateSnapshot & state, size_t unit) {
  if (unit != 0) return state.extraTextures[unit - 1];
  CoinRenderTextureUnitSnapshot result;
  result.enabled = state.hasTexture;
  result.imageSlot = state.textureImageSlot;
  result.samplerSlot = state.samplerSlot;
  result.model = state.textureModel;
  result.matrix = state.textureMatrix;
  for (int c = 0; c < 4; ++c) result.blendColor[c] = state.textureBlendColor[c];
  return result;
}

/**
 * Private, Coin-native description of one captured WebGPU frame.
 *
 * The plan is the mechanical boundary between Open Inventor traversal and a
 * concrete backend. It owns no GPU resources and performs no submission.
 */
struct CoinRenderFramePlan {
  uint64_t revision = 0;
  CoinRenderTransparencyOptions transparency;
  SbColor4f clearColor = SbColor4f(0.0f, 0.0f, 0.0f, 1.0f);
  std::vector<CoinRenderVertexSnapshot> vertices;
  std::vector<uint32_t> indices;
  std::vector<CoinRenderMaterialSnapshot> materials;
  std::vector<CoinRenderLightingSnapshot> lightingStates;
  std::vector<CoinRenderShadowGroupSnapshot> shadowGroups;
  std::vector<CoinRenderShadowLightSnapshot> shadowLights;
  std::vector<CoinRenderCameraSnapshot> cameras;
  std::vector<CoinRenderViewportSnapshot> viewports;
  std::vector<CoinRenderRenderStateSnapshot> renderStates;
  std::vector<CoinRenderTextureImageSnapshot> textures;
  std::vector<CoinRenderSamplerSnapshot> samplers;
  std::vector<CoinRenderDrawPacket> draws;

  /**
   * Checks semantic and range invariants before a backend consumes the plan.
   *
   * Existing diagnostic text is intentionally stable because the Shell layer
   * can expose it to tests, logs and applications.
   */
  COIN_RENDER_DLL_API bool isValid(std::string * outDiagnostic = nullptr) const;

  /**
   * Compares captured payload for conservative reuse.
   *
   * Frame revision is deliberately ignored. A false result requires rebuild;
   * a true result means every backend-visible captured field is equal.
   */
  COIN_RENDER_DLL_API bool hasSamePayload(const CoinRenderFramePlan & other) const;
};

#endif // !COIN_RENDER_FRAME_PLAN_H
