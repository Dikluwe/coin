#ifndef COIN_RENDER_FRAME_PLAN_BUILDER_H
#define COIN_RENDER_FRAME_PLAN_BUILDER_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderDirectGeometry.h"
#include "rendering/coinrender/CoinRenderPlanAssemblyCore.h"
#include <Inventor/SbViewportRegion.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

class SoCallbackAction;
class SoCamera;
class SoPrimitiveVertex;
class SoNode;
class SoTexture2;
class SoTexture3;
class SoTextureCubeMap;
class CoinRenderFramePreflight;

#include <Inventor/rendering/CoinRenderOptions.h>

class CoinRenderFramePlanBuilder {
public:
  CoinRenderFramePlanBuilder();
  ~CoinRenderFramePlanBuilder();
  // Suspend a main capture while Wiring traverses a light-owned scene.
  CoinRenderFramePlanBuilder(CoinRenderFramePlanBuilder &&) = default;
  CoinRenderFramePlanBuilder & operator=(CoinRenderFramePlanBuilder &&) = default;

  void beginFrame(const SbColor4f & clearColor, const SbViewportRegion & viewport);
  // Allocation hint only. Captured values and deduplication stay unchanged.
  void reserveCaptureStorage(size_t estimate);
  void reset();
  // Render-state reuse is scoped to one occurrence; geometry replay is frame-local.
  void beginShape(SoCallbackAction * action, const SoNode * node);
  void endShape();
  bool replayNativeCube(SoCallbackAction * action, SoNode * node);
  bool captureBoundingBox(SoCallbackAction * action, SoNode * node);
  bool captureScreenContent(SoCallbackAction * action, const SoNode * node);
  bool captureMarkerContent(SoCallbackAction * action, const SoNode * node,
                            bool primitiveObservers);
  void recordRasterGenerator(SoCallbackAction *, const SoNode *);
  void synchronizeRasterGenerators(SoCallbackAction *);
  void recordRasterNormal(SoCallbackAction *, const SoNode *);
  struct RasterHistory {
    std::array<uint8_t, COIN_RENDER_MAX_TEXTURE_UNITS> coordinates{};
    SbVec3f normal;
    bool normalKnown;
    std::array<SbVec4f, COIN_RENDER_MAX_TEXTURE_UNITS> qPlanes;
  };
  RasterHistory rasterHistory() const;
  void restoreRasterHistory(const RasterHistory &);
  bool hasScreenContent() const { return this->screenContentCaptured; }
  bool hasCoordinateFunctions() const { return this->coordinateFunctionsCaptured; }
  // Wiring reads the effective Coin state, including ignored fields and overrides.
  static bool isShapeInvisible(SoCallbackAction * action);
  static int polygonDrawStyle(SoCallbackAction * action);
  static CoinRenderCameraSnapshot captureCamera(SoCallbackAction * action);
  void recordLightAttenuation(SoCallbackAction * action);
  void beginShadowGroup(const CoinRenderShadowGroupSnapshot & group);
  void endShadowGroup();
  bool hasActiveShadowGroup() const { return !this->shadowGroupStack.empty(); }
  uint32_t activeShadowGroupSlot() const { return this->shadowGroupStack.empty() ? 0 : this->shadowGroupStack.back(); }
  void recordShadowLight(const CoinRenderShadowLightSnapshot & light);
  void registerShadowMapCamera(const SoCamera * camera);
  void registerShadowMapCameraPose(const SoCamera * camera, const SbMatrix & model,
                                   const SbMatrix & view);
  void beginAnnotation(bool clearDepth = true);
  uint32_t activeAnnotationLayer() const { return this->currentAnnotationLayer; }
  uint32_t reserveDelayedLayers(uint32_t count);
  void beginAuthoredScene(bool clearDepth);
  void endAuthoredScene();
  void beginDelayedAnnotations(uint32_t layer = 1, bool clearDepth = true);
  void beginForeground();
  void endForeground();
  void endAnnotation();

  void configureTextures(const CoinRenderOptions& options) { currentPlan.textureSamplingPolicy=options.textureSamplingPolicy; currentPlan.rasterizationPolicy=options.rasterizationPolicy; storedTextureColorSpace=options.storedTextureColorSpace; maxTextureAnisotropy=options.maxTextureAnisotropy; }
  bool captureCubeNode(SoCallbackAction *, const SoTextureCubeMap *, std::string & diagnostic);
  void registerAuthoredTextureImage(const unsigned char * bytes, bool compressed,
                                    const SoTexture2 * source, SbUniqueId revision);
  void registerAuthoredVolumeImage(const unsigned char * bytes, bool compressed,
                                   const SoTexture3 * source, SbUniqueId revision);
  void registerSceneTexture(const unsigned char* image, uint64_t producerId, uint32_t width,
                            uint32_t height, bool opaque, int32_t transparencyFunction,
                            CoinRenderTextureFormat format=CoinRenderTextureFormat::RGBA8_LINEAR, uint32_t cubeFaceSize=0);
  void addTriangle(SoCallbackAction * action,
                   const SoPrimitiveVertex * v0,
                   const SoPrimitiveVertex * v1,
                   const SoPrimitiveVertex * v2);

  void addLine(SoCallbackAction * action,
               const SoPrimitiveVertex * v0,
               const SoPrimitiveVertex * v1);

  void addPoint(SoCallbackAction * action,
                const SoPrimitiveVertex * vertex);

  CoinRenderFastPathResult processIndexedFaceSet(SoCallbackAction * action,
                                       const CoinRenderDirectGeometryView & view,
                                       SoNode * node,
                                       std::string * outError = nullptr);

  CoinRenderFastPathResult processIndexedLineSet(SoCallbackAction * action,
                                       const CoinRenderDirectGeometryView & view,
                                       SoNode * node,
                                       std::string * outError = nullptr);

  // Transfer is used by Wiring once capture has finished. The default keeps
  // repeatable snapshot semantics for callers that inspect the builder.
  bool build(CoinRenderFramePlan & outPlan, std::string * outError = nullptr,
             bool transferOwnership = false,
             const CoinRenderTransparencyOptions * transparency = nullptr,
             CoinRenderFramePreflight * preflight = nullptr);
  bool isUnsupportedBuild() const { return this->isUnsupported; }
  size_t capturedDrawCount() const { return this->currentPlan.draws.size(); }
  void setColorWriteEnabled(bool enabled) { this->colorWriteEnabled = enabled; }
  void setLinePatternOverride(bool enabled, uint32_t pattern = 0xffffu, int32_t factor = 1) {
    this->linePatternOverride = enabled;
    this->linePatternValue = pattern;
    this->linePatternFactor = factor;
  }
  const CoinRenderDrawPacket * capturedDraw(size_t index) const {
    return index < this->currentPlan.draws.size() ? &this->currentPlan.draws[index] : nullptr;
  }
  static uint64_t nextRevision();

private:
  friend struct CoinRenderFramePlanBuilderTestAccess;
  const SoNode * lastBumpSource = nullptr;
  SbUniqueId lastBumpRevision = 0;
  uint32_t lastBumpSlot = 0;
  std::unordered_map<const SoNode *, std::pair<SbUniqueId, uint32_t>> bumpImageSlots;
  std::vector<CoinRenderBumpLegacyCorner> bumpShapeCorners;
  std::vector<uint32_t> bumpShapeVertices;
  std::vector<uint32_t> bumpShapeTriangleIndices;
  std::unordered_map<uint32_t, uint32_t> bumpCornerByVertex;
  void recordBumpTriangle(uint32_t a, uint32_t b, uint32_t c);
  uint32_t captureMaterial(SoCallbackAction * action, int materialIndex, bool packedDiffuse = false);
  uint32_t internMaterial(const CoinRenderMaterialSnapshot & material);
  static uint64_t materialBytesKey(const CoinRenderMaterialSnapshot & material);
  bool synchronizeMaterialIndex();
  void disableMaterialIndex();
  uint32_t captureRenderState(SoCallbackAction * action, int materialIndex, bool captureTextures = true,
                              bool boundingBox = false, bool captureRasterLighting = false);
  // Persistent current attributes are outside Separator state. Array draws
  // invalidate enabled units; no callback corner can reconstruct their value.
  enum class RasterCoordinateHistory : uint8_t { INITIAL, INDETERMINATE };
  std::array<RasterCoordinateHistory, COIN_RENDER_MAX_TEXTURE_UNITS> rasterCoordinateHistory{};
  enum class RasterGeneratorKind : uint8_t { OBJECT, SPHERE, REFLECTION, NORMAL, UNSUPPORTED };
  struct RasterGenerator {
    const void * callback = nullptr;
    RasterGeneratorKind kind = RasterGeneratorKind::OBJECT;
    SbVec4f qPlane = SbVec4f(0,0,0,0);
    bool setsQ = false;
  };
  // GL current normal and texgen parameters are context state, not Inventor
  // stack state. SoNormal alone does not emit glNormal.
  SbVec3f rasterNormal = SbVec3f(0,0,1);
  bool rasterNormalKnown = true;
  std::array<SbVec4f, COIN_RENDER_MAX_TEXTURE_UNITS> rasterQPlanes{};
  std::unordered_map<SbUniqueId, RasterGenerator> rasterGenerators;
  void invalidateRasterCoordinates(SoCallbackAction * action);
  bool captureTexture(SoCallbackAction * action, CoinRenderRenderStateSnapshot & rs, std::string * outError = nullptr);
  bool captureStoredTextureAlpha(SoCallbackAction *, bool & transparent);
  bool captureTextureUnit(SoCallbackAction * action, int unit, CoinRenderRenderStateSnapshot & rs, std::string * outError);
  void captureSortingCenter(SoCallbackAction * action);
  bool expandStyledPrimitives(std::string * outError);
  void addStyledTriangle(SoCallbackAction * action, const SoPrimitiveVertex * v0,
                         const SoPrimitiveVertex * v1, const SoPrimitiveVertex * v2);
  void emitStyledPolygon(SoCallbackAction * action, bool preserveDegenerateContour = false);
  CoinRenderVertexSnapshot captureVertex(SoCallbackAction * action,
    const SoPrimitiveVertex * pv, uint32_t materialSlot, const CoinRenderRenderStateSnapshot & state);
  uint32_t addVertex(SoCallbackAction * action, const SoPrimitiveVertex * pv, uint32_t materialSlot);
  void ensureDrawPacket(CoinRenderPrimitiveTopology topology, uint32_t renderStateSlot, SoNode * node, bool forceNewPacket = false);

  SoNode * lineNode = nullptr;
  int lineIndex = -1;
  uint64_t lineStripId = 0;
  uint64_t nextLineStripId = 0;
  // Coin's triangle callbacks normalize PER_FACE attributes across a whole
  // triangle. Smooth GL strips retain the attributes of the two prior vertices.
  const SoNode * stripMaterialNode = nullptr;
  int stripMaterialPart = -1;
  int stripMaterialNextFace = 0;
  int stripMaterialOlder = 0;
  int stripMaterialNewer = 0;
  const SoNode * quadShadowNode = nullptr;
  int quadShadowPart = -1;
  int quadShadowFace = -1;
  int quadShadowTriangle = 0;
  SoNode * polygonNode = nullptr;
  std::vector<CoinRenderVertexSnapshot> polygonVertices;
  std::vector<CoinRenderVertexSnapshot> polygonTriangleVertices;
  std::vector<SbVec3f> polygonPositions;
  std::vector<bool> polygonCaptured;
  uint32_t polygonState = 0;
  int polygonStyle = 0;
  int polygonTriangles = 0;
  int polygonFaceIndex = -1;
  int polygonPartIndex = -1;
  CoinRenderFramePlan currentPlan;
  // Exact, capture-local semantic identities. Parameters are validated and
  // read afresh before lookup; node/mesh addresses and revision-only hints
  // never substitute for their current f32 bits.
  std::unordered_map<std::string, uint32_t> portableShaderSlots;
  size_t captureReserveEstimate = 0;
  uint32_t currentDrawIndex;
  uint32_t nodeCounter;
  bool inFrame;
  bool hasActiveDraw;
  bool hasError;
  bool isUnsupported;
  bool screenContentCaptured = false;
  bool coordinateFunctionsCaptured = false;
  uint32_t savedAnnotationLayer = 0;
  bool savedAnnotationClear = false;
  uint32_t foregroundLayer = 0;
  uint32_t annotationDepth;
  uint32_t currentAnnotationLayer;
  uint32_t nextAnnotationLayer;
  uint32_t authoredSceneLayer = UINT32_MAX;
  uint32_t authoredSceneParentLayer = 0;
  bool authoredSceneParentClear = false;
  bool annotationDepthClearPending;
  bool deferredDepthClear = false;
  std::string builderError;
  std::unordered_map<uint64_t, uint32_t> nodeOccurrenceCount;
  CoinRenderPlanAssemblyCore::StateIndex renderStatesByModel;
  // Optional capture-only acceleration. Slots remain in first-occurrence
  // order, and collisions always compare complete snapshot bytes. At most
  // 65,536 heads/links (conservative metadata estimate below 9 MiB).
  enum { MATERIAL_INDEX_LIMIT = 65536, MATERIAL_INDEX_THRESHOLD = 32 };
  std::unordered_map<uint64_t, uint32_t> materialHeads;
  std::vector<uint32_t> materialNext;
  bool materialIndexDisabled = false;
  const SoNode * stableShape = nullptr;
  bool reuseCubeVertices = false;
  uint32_t cubeVertexSlots[48];
  // Bounded frame-local templates learned from the native callback stream.
  // State and material slots are captured again for every occurrence.
  CoinRenderCubeGeometryCore cubeGeometryCore;
  float cubeCaptureDimensions[3] = {};
  int cubeCaptureNormalBinding = 0;
  bool captureCubeTemplate = false;
  size_t cubeCaptureFirstVertex = 0;
  size_t cubeCaptureFirstIndex = 0;
  size_t cubeCaptureFirstDraw = 0;
  uint64_t cubeReplayHits = 0;
  std::vector<std::pair<int, uint32_t>> shapeRenderStates;
  std::vector<SbVec3f> lightAttenuationByIndex;
  std::vector<SbMatrix> lightModelByIndex;
  // Reuse only storage; effective light values are recaptured for each shape.
  std::vector<CoinRenderLightSourceSnapshot> lightCaptureScratch;
  std::vector<uint32_t> shadowGroupStack;
  struct SceneTexture {
    uint64_t producerId;
    uint32_t width;
    uint32_t height;
    bool opaque;
    int32_t transparencyFunction;
    CoinRenderTextureFormat format;
    uint32_t cubeFaceSize;
  };
  std::unordered_map<const unsigned char*, SceneTexture> sceneTextures;
  std::unordered_map<SbUniqueId, const SoCamera *> shadowMapCameras;
  struct ShadowMapCameraPose { SbMatrix model, view; };
  std::unordered_map<SbUniqueId, std::vector<ShadowMapCameraPose>> shadowMapCameraPoses;
  std::unordered_set<const unsigned char*> authoredTextureImages,compressedTextureImages,
                                           authoredVolumeImages,compressedVolumeImages;
  struct AuthoredTextureSource {
    const SoTexture2 * node;
    SbUniqueId revision;
  };
  std::unordered_map<const unsigned char*, AuthoredTextureSource> authoredTextureSources;
  struct AuthoredVolumeSource {
    const SoTexture3 * node;
    SbUniqueId revision;
  };
  std::unordered_map<const unsigned char*, AuthoredVolumeSource> authoredVolumeSources;
  std::unordered_map<SbUniqueId, const SoTextureCubeMap *> cubeSources;
  std::unordered_map<SbUniqueId, std::array<int64_t, 2>> capturedCubeImages;
  struct CapturedVolumeImage {
    const SoTexture3 * node;
    SbUniqueId revision;
    uint32_t width, height, depth, components;
    bool mipmapped;
    uint32_t imageSlot;
    bool transparent;
  };
  // Frame-local: a field revision and source pointer identify immutable bytes.
  // The plan owns the packed atlas, so no duplicate 90 MiB cache allocation.
  std::unordered_map<const unsigned char*, CapturedVolumeImage> capturedVolumeImages;
  struct LegacyPotImage {
    SbUniqueId revision;
    uint32_t width, height;
    bool compressed;
    std::vector<uint8_t> pixels;
  };
  // CoinGL keeps its first resized upload until the SoTexture2 node changes.
  // This cache survives frame resets and is bounded to 128 MiB per builder.
  std::unordered_map<const SoTexture2 *, LegacyPotImage> legacyPotImages;
  size_t legacyPotBytes = 0;
  CoinRenderStoredTextureColorSpace storedTextureColorSpace=COIN_RENDER_TEXTURE_LINEAR;
  bool colorWriteEnabled = true;
  bool linePatternOverride = false;
  uint32_t linePatternValue = 0xffffu;
  int32_t linePatternFactor = 1;
  uint32_t maxTextureAnisotropy=16;
};

#endif // !COIN_RENDER_FRAME_PLAN_BUILDER_H
