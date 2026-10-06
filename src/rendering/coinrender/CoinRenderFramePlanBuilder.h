#ifndef COIN_RENDER_FRAME_PLAN_BUILDER_H
#define COIN_RENDER_FRAME_PLAN_BUILDER_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderDirectGeometry.h"
#include "rendering/coinrender/CoinRenderPlanAssemblyCore.h"
#include <Inventor/SbViewportRegion.h>
#include <unordered_map>

class SoCallbackAction;
class SoPrimitiveVertex;
class SoNode;
class CoinRenderFramePreflight;

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
  bool captureScreenContent(SoCallbackAction * action, const SoNode * node);
  bool captureMarkerContent(SoCallbackAction * action, const SoNode * node,
                            bool primitiveObservers);
  bool hasScreenContent() const { return this->screenContentCaptured; }
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
  void beginAnnotation(bool clearDepth = true);
  void reserveDelayedLayers(uint32_t count);
  void beginDelayedAnnotations(uint32_t layer = 1, bool clearDepth = true);
  void beginForeground();
  void endForeground();
  void endAnnotation();

  void registerSceneTexture(const unsigned char* image, uint64_t producerId, uint32_t width,
                            uint32_t height, bool opaque, int32_t transparencyFunction);
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
  const CoinRenderDrawPacket * capturedDraw(size_t index) const {
    return index < this->currentPlan.draws.size() ? &this->currentPlan.draws[index] : nullptr;
  }
  static uint64_t nextRevision();

private:
  friend struct CoinRenderFramePlanBuilderTestAccess;
  uint32_t captureMaterial(SoCallbackAction * action, int materialIndex);
  uint32_t internMaterial(const CoinRenderMaterialSnapshot & material);
  static uint64_t materialBytesKey(const CoinRenderMaterialSnapshot & material);
  bool synchronizeMaterialIndex();
  void disableMaterialIndex();
  uint32_t captureRenderState(SoCallbackAction * action, int materialIndex, bool captureTextures = true);
  bool captureTexture(SoCallbackAction * action, CoinRenderRenderStateSnapshot & rs, std::string * outError = nullptr);
  bool captureTextureUnit(SoCallbackAction * action, int unit, CoinRenderRenderStateSnapshot & rs, std::string * outError);
  void captureSortingCenter(SoCallbackAction * action);
  bool expandStyledPrimitives(std::string * outError);
  void addStyledTriangle(SoCallbackAction * action, const SoPrimitiveVertex * v0,
                         const SoPrimitiveVertex * v1, const SoPrimitiveVertex * v2);
  void emitStyledPolygon(SoCallbackAction * action);
  CoinRenderVertexSnapshot captureVertex(SoCallbackAction * action,
    const SoPrimitiveVertex * pv, uint32_t materialSlot, const CoinRenderRenderStateSnapshot & state);
  uint32_t addVertex(SoCallbackAction * action, const SoPrimitiveVertex * pv, uint32_t materialSlot);
  void ensureDrawPacket(CoinRenderPrimitiveTopology topology, uint32_t renderStateSlot, SoNode * node, bool forceNewPacket = false);

  SoNode * lineNode = nullptr;
  int lineIndex = -1;
  uint64_t lineStripId = 0;
  uint64_t nextLineStripId = 0;
  SoNode * polygonNode = nullptr;
  std::vector<CoinRenderVertexSnapshot> polygonVertices;
  std::vector<SbVec3f> polygonPositions;
  std::vector<bool> polygonCaptured;
  uint32_t polygonState = 0;
  int polygonStyle = 0;
  int polygonTriangles = 0;
  int polygonFaceIndex = -1;
  int polygonPartIndex = -1;
  CoinRenderFramePlan currentPlan;
  size_t captureReserveEstimate = 0;
  uint32_t currentDrawIndex;
  uint32_t nodeCounter;
  bool inFrame;
  bool hasActiveDraw;
  bool hasError;
  bool isUnsupported;
  bool screenContentCaptured = false;
  uint32_t savedAnnotationLayer = 0;
  bool savedAnnotationClear = false;
  uint32_t foregroundLayer = 0;
  uint32_t annotationDepth;
  uint32_t currentAnnotationLayer;
  uint32_t nextAnnotationLayer;
  bool annotationDepthClearPending;
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
  // Reuse only storage; effective light values are recaptured for each shape.
  std::vector<CoinRenderLightSourceSnapshot> lightCaptureScratch;
  std::vector<uint32_t> shadowGroupStack;
  struct SceneTexture {
    uint64_t producerId;
    uint32_t width;
    uint32_t height;
    bool opaque;
    int32_t transparencyFunction;
  };
  std::unordered_map<const unsigned char*, SceneTexture> sceneTextures;
};

#endif // !COIN_RENDER_FRAME_PLAN_BUILDER_H
