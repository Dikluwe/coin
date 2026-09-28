#ifndef COIN_SOWGPUBGFXCORE_H
#define COIN_SOWGPUBGFXCORE_H

#include "rendering/coinrender/CoinRenderFramePlan.h"

#include <cstdint>
#include <string>
#include <vector>

enum class SoWgpuBgfxTransparencyMode {
  AUTO,
  OBJECT,
  WEIGHTED_OIT,
  SORTED_LAYERS
};

enum class SoWgpuBgfxTransparencyStrategy {
  OBJECT,
  WEIGHTED_OIT,
  SORTED_LAYERS
};
// Backend-neutral lowering for the deliberately narrow BGFX evaluation profile.
// No BGFX headers or GPU state leak into Core or Open Inventor traversal.
struct SoWgpuBgfxVertex {
  float position[3];
  float color[4];
  float texcoord[2];
  float viewPosition[3];
  float viewNormal[3];
  float ambient[4];
  float specular[4];
  float emission[4];
  float material[4]; // shininess, PHONG enabled, homogeneous W, reserved
  float extraTexcoords[4][4]; // pairs of UVs for units 1..7
};

struct SoWgpuBgfxDraw {
  float mvp[16];
  uint32_t firstVertex;
  uint32_t vertexCount;
  uint32_t firstIndex;
  uint32_t indexCount;
  CoinRenderCullMode cullMode;
  CoinRenderFrontFace frontFace;
  bool depthTest = true;
  bool depthWrite = true;
  CoinRenderDepthFunction depthFunction = CoinRenderDepthFunction::LESS;
  float depthRange[2] = {0.0f, 1.0f};
  float polygonOffsetFactor = 0.0f; // Effective, style-filtered bias.
  float polygonOffsetUnits = 0.0f;
  bool blend = false;
  SbUniqueId sourceNodeId = 0;
  bool sortTriangles = false;
  bool deferred = false;
  bool additive = false;
  float screenDoor[4] = {0, 0, 0, 0};
  float alpha = 1.0f;
  SoWgpuBgfxTransparencyStrategy transparencyStrategy = SoWgpuBgfxTransparencyStrategy::OBJECT;
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

struct SoWgpuBgfxTexture {
  uint32_t width = 0;
  uint32_t height = 0;
  uint64_t gpuToken = 0;
  std::vector<uint8_t> pixelsRgba;
};

struct SoWgpuBgfxPlan {
  std::vector<SoWgpuBgfxVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<SoWgpuBgfxDraw> draws;
  float clearColor[4];
  std::vector<SoWgpuBgfxTexture> textures;
};

struct SoWgpuBgfxVertexRange {
  uint32_t first = 0;
  uint32_t count = 0;
};

class SoWgpuBgfxCore {
public:
  // Clip the scissor only; preserve the original viewport for projection and camera reuse.
  // False means the viewport does not intersect the target.
  static bool clipViewport(const int32_t viewport[4], int width, int height,
                           int32_t clipped[4]);
  static bool lower(const CoinRenderFramePlan & frame, int width, int height,
                    bool homogeneousDepth, SoWgpuBgfxPlan & output,
                    std::string & diagnostic);
  static bool selectTransparencyStrategy(
    const std::vector<SoWgpuBgfxDraw> & draws,
    SoWgpuBgfxTransparencyMode configuredMode,
    bool weightedOitSupported,
    bool sortedLayersSupported,
    SoWgpuBgfxTransparencyStrategy & selected,
    std::string & diagnostic);
  // Reorders only opaque draws by pipeline/material/texture/uniform state.
  // Transparent draws are appended in their original relative order.
  static void groupOpaqueDraws(const std::vector<SoWgpuBgfxDraw> & draws,
                               std::vector<SoWgpuBgfxDraw> & output);
  // Detects a material-only lowered-plan change and returns the minimal
  // contiguous vertex ranges that must be uploaded.
  static bool materialPatchRanges(const SoWgpuBgfxPlan & base,
                                  const SoWgpuBgfxPlan & updated,
                                  std::vector<SoWgpuBgfxVertexRange> & ranges);
  // Requires a validated CAMERA_PATCH relationship with the cached base.
  // Recomputes only draw transforms; geometry, material and clear remain owned
  // by the base plan until the caller commits a successful frame.
  static bool patchCamera(const CoinRenderFramePlan & frame, int width, int height,
                          bool homogeneousDepth, const SoWgpuBgfxPlan & base,
                          std::vector<SoWgpuBgfxDraw> & output,
                          std::string & diagnostic);
};

#endif
