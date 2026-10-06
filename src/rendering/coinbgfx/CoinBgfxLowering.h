#ifndef COIN_BGFX_LOWERING_H
#define COIN_BGFX_LOWERING_H

#include "rendering/coinrender/CoinRenderFramePlan.h"

#include <cstdint>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

enum class CoinBgfxTransparencyMode {
  AUTO,
  OBJECT,
  WEIGHTED_OIT,
  SORTED_LAYERS
};

enum class CoinBgfxTransparencyStrategy {
  OBJECT,
  WEIGHTED_OIT,
  SORTED_LAYERS
};
// BGFX-specific packed vertex layout and grouping for the evaluation profile.
// Shared Coin semantics/transforms live in CoinRender Core; no GPU handles here.
struct CoinBgfxVertex {
  float position[3];
  float color[4];
  float texcoord[2];
  float viewPosition[3];
  float viewNormal[3];
  float ambient[4];
  float specular[4];
  float emission[4];
  float material[4]; // shininess, PHONG enabled, clip W, texture unit zero Q
  float extraTexcoords[4][4]; // pairs of UVs for units 1..7
  // Homogeneous texture divisors for units 1..7, carried by the otherwise
  // unused tangent/bitangent attributes. Unit zero stays in the compact prefix.
  float extraTextureQ[2][4];
};
struct CoinBgfxInstancedVertex {
  float position[3];
  float normal[3];
};
struct CoinBgfxInstance {
  // View-position columns 0..2, normal columns 3..5, material D/A/S/E 6..9.
  // Position may factor a positive diagonal mesh scale. Normals keep the
  // original authored model-view inverse transpose, independent of that scale.
  // Column 3.w is shininess; column 4.w is the PHONG flag.
  float data[10][4];
};
static_assert(sizeof(CoinBgfxInstancedVertex) == 24, "BGFX instanced mesh stride");
static_assert(sizeof(CoinBgfxInstance) == 160, "BGFX instance stride");
typedef std::array<float, 31> CoinBgfxVertexPrefix;
static_assert(sizeof(CoinBgfxVertexPrefix) == offsetof(CoinBgfxVertex, extraTexcoords),
              "BGFX compact layout must match the full vertex attribute prefix");
static_assert(sizeof(CoinBgfxVertex) == 220, "BGFX projective vertex stride");

struct CoinBgfxDraw {
  float mvp[16];
  // Eye-space instance payload is anchored in the first camera. Columns 0..2
  // apply its camera delta; columns 3..5 transform normals. 3.w enables it.
  float instanceCamera[6][4] = {};
  uint32_t sourceDrawSlot = 0;
  uint32_t renderStateSlot = 0;
  uint32_t firstVertex;
  uint32_t vertexCount;
  uint32_t firstIndex;
  uint32_t indexCount;
  uint32_t firstInstance = 0;
  uint32_t instanceCount = 0;
  CoinRenderCullMode cullMode;
  CoinRenderFrontFace frontFace;
  bool depthTest = true;
  bool depthWrite = true;
  CoinRenderTextureProjection textureProjection = CoinRenderTextureProjection::PROJECTIVE;
  CoinRenderDepthFunction depthFunction = CoinRenderDepthFunction::LESS;
  float depthRange[2] = {0.0f, 1.0f};
  float polygonOffsetFactor = 0.0f; // Effective, style-filtered bias.
  float polygonOffsetUnits = 0.0f;
  float polygonOffsetSlopeBias = 0.0f;
  CoinRenderAlphaTestFunction alphaTestFunction = CoinRenderAlphaTestFunction::NONE;
  float alphaTestReference = 0.5f;
  bool blend = false;
  SbUniqueId sourceNodeId = 0;
  bool sortTriangles = false;
  bool deferred = false;
  bool additive = false;
  float screenDoor[4] = {0, 0, 0, 0};
  float clipMeta[4] = {};
  float clipPlanes[COIN_RENDER_MAX_CLIP_PLANES][4] = {};
  float alpha = 1.0f;
  CoinBgfxTransparencyStrategy transparencyStrategy = CoinBgfxTransparencyStrategy::OBJECT;
  int32_t viewport[4] = {0, 0, 0, 0}; // Coin bottom-left x, y, width, height
  uint32_t renderLayer = 0;
  bool clearDepthBefore = false;
  bool hasTexture = false;
  uint32_t textureSlot = 0;
  CoinRenderTextureModel textureModel = CoinRenderTextureModel::MODULATE;
  float textureBlendColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  CoinRenderTextureWrap wrapS = CoinRenderTextureWrap::REPEAT;
  CoinRenderTextureWrap wrapT = CoinRenderTextureWrap::REPEAT;
  CoinRenderTextureFilter filter = CoinRenderTextureFilter::LINEAR;
  struct TextureLayer {
    bool enabled = false;
    uint32_t slot = 0;
    CoinRenderTextureModel model = CoinRenderTextureModel::MODULATE;
    float blendColor[4] = {0, 0, 0, 1};
    CoinRenderTextureWrap wrapS = CoinRenderTextureWrap::REPEAT, wrapT = CoinRenderTextureWrap::REPEAT;
    CoinRenderTextureFilter filter = CoinRenderTextureFilter::LINEAR;
  };
  TextureLayer extraTextures[COIN_RENDER_MAX_TEXTURE_UNITS - 1];
  CoinRenderTextureCombineSnapshot textureCombines[COIN_RENDER_MAX_TEXTURE_UNITS];
  float fogColorMode[4] = {};
  float fogRange[4] = {};
  float ambientLight[4] = {1.0f, 1.0f, 1.0f, 0.2f};
  float lightCount[4] = {};
  float lightPositionType[COIN_RENDER_MAX_LIGHTS][4] = {};
  float lightDirectionCutoff[COIN_RENDER_MAX_LIGHTS][4] = {};
  float lightColorIntensity[COIN_RENDER_MAX_LIGHTS][4] = {};
  float lightAttenuationDrop[COIN_RENDER_MAX_LIGHTS][4] = {};
  // Stable signature of every material referenced by this draw. Materials
  // remain vertex data; this key is only used to cluster opaque submissions.
  uint64_t materialSignature = 0;
};

struct CoinBgfxTexture {
  uint32_t width = 0;
  uint32_t height = 0;
  uint64_t gpuToken = 0;
  std::vector<uint8_t> pixelsRgba;
};

struct CoinBgfxPlan {
  std::vector<CoinBgfxVertex> vertices;
  std::vector<CoinBgfxVertexPrefix> packedVertices;
  bool usesCompactVertices = false;
  bool usesInstancing = false;
  bool instancedCameraPatchable = false;
  SbMatrix instanceCameraAnchorView = SbMatrix::identity();
  std::vector<CoinBgfxInstancedVertex> instancedVertices;
  std::vector<CoinBgfxInstance> instances;
  std::vector<uint32_t> indices;
  std::vector<CoinBgfxDraw> draws;
  std::vector<CoinBgfxDraw> shadowDraws;
  float clearColor[4];
  std::vector<CoinBgfxTexture> textures;
  // Counts survive releasing CPU geometry after its GPU upload.
  size_t uploadedVertexCount = 0;
  size_t uploadedIndexCount = 0;
  size_t vertexCount() const {
    if (usesInstancing) return instancedVertices.empty() ? uploadedVertexCount : instancedVertices.size();
    if (usesCompactVertices) return packedVertices.empty() ? uploadedVertexCount : packedVertices.size();
    return vertices.empty() ? uploadedVertexCount : vertices.size();
  }
};

struct CoinBgfxVertexRange {
  uint32_t first = 0;
  uint32_t count = 0;
};

class CoinRenderFramePreflight;

class CoinBgfxLowering {
public:
  // Keep draw metadata for static GPU reuse even when CPU geometry is large.
  // Oversized metadata is rejected; small plans retain material patch inputs.
  static bool retainForReuse(CoinBgfxPlan & plan,
                            uint64_t geometryBudget = 32u * 1024u * 1024u,
                            uint64_t metadataBudget = 128u * 1024u * 1024u);
  // Clip the scissor only; preserve the original viewport for projection and camera reuse.
  // False means the viewport does not intersect the target.
  static bool clipViewport(const int32_t viewport[4], int width, int height,
                           int32_t clipped[4]);
  static bool lower(const CoinRenderFramePlan & frame, int width, int height,
                    bool homogeneousDepth, CoinBgfxPlan & output,
                    std::string & diagnostic, bool allowQualifiedShadows = false,
                    bool batchOpaque = false,
                    const CoinRenderFramePreflight * preflight = nullptr,
                    bool compactOpaqueVertices = false);
  // Conservative, value-only opaque profile. False declines without publishing
  // any output; the executor must retain the general lowering fallback.
  static bool lowerInstanced(const CoinRenderFramePlan & frame, int width, int height,
                            bool homogeneousDepth, CoinBgfxPlan & output,
                            std::string & diagnostic,
                            const CoinRenderFramePreflight * preflight = nullptr);
  static bool selectTransparencyStrategy(
    const std::vector<CoinBgfxDraw> & draws,
    CoinBgfxTransparencyMode configuredMode,
    bool weightedOitSupported,
    bool sortedLayersSupported,
    CoinBgfxTransparencyStrategy & selected,
    std::string & diagnostic);
  // Reorders only opaque draws by pipeline/material/texture/uniform state.
  // Transparent draws are appended in their original relative order.
  static void groupOpaqueDraws(const std::vector<CoinBgfxDraw> & draws,
                               std::vector<CoinBgfxDraw> & output);
  // Detects a material-only lowered-plan change and returns the minimal
  // contiguous vertex ranges that must be uploaded.
  static bool materialPatchRanges(const CoinBgfxPlan & base,
                                  const CoinBgfxPlan & updated,
                                  std::vector<CoinBgfxVertexRange> & ranges);
  // Requires a validated CAMERA_PATCH relationship with the cached base.
  // Recomputes only draw transforms/lighting; instanced geometry remains in
  // the immutable anchor camera, including across consecutive camera patches.
  // Geometry, material and clear remain owned
  // by the base plan until the caller commits a successful frame.
  static bool patchCamera(const CoinRenderFramePlan & frame, int width, int height,
                          bool homogeneousDepth, const CoinBgfxPlan & base,
                          std::vector<CoinBgfxDraw> & output,
                          std::string & diagnostic);
};

#endif
