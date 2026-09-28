#ifndef COIN_RENDER_FRAME_PLAN_BUILDER_H
#define COIN_RENDER_FRAME_PLAN_BUILDER_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderDirectGeometry.h"
#include <Inventor/SbViewportRegion.h>
#include <unordered_map>

class SoCallbackAction;
class SoPrimitiveVertex;
class SoNode;

class CoinRenderFramePlanBuilder {
public:
  CoinRenderFramePlanBuilder();
  ~CoinRenderFramePlanBuilder();

  void beginFrame(const SbColor4f & clearColor, const SbViewportRegion & viewport);
  void reset();
  // Wiring reads the effective Coin state, including ignored fields and overrides.
  static bool isShapeInvisible(SoCallbackAction * action);
  void recordLightAttenuation(SoCallbackAction * action);
  void beginAnnotation();
  void reserveDelayedLayers(uint32_t count);
  void beginDelayedAnnotations(uint32_t layer = 1, bool clearDepth = true);
  void beginForeground();
  void endForeground();
  void endAnnotation();

  void registerDirectTexture(const unsigned char * image, uint64_t token, uint32_t width, uint32_t height, bool opaque);
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

  bool build(CoinRenderFramePlan & outPlan, std::string * outError = nullptr);
  bool isUnsupportedBuild() const { return this->isUnsupported; }
  static uint64_t nextRevision();

private:
  uint32_t captureMaterial(SoCallbackAction * action, int materialIndex);
  uint32_t captureRenderState(SoCallbackAction * action, int materialIndex);
  bool captureTexture(SoCallbackAction * action, CoinRenderRenderStateSnapshot & rs, std::string * outError = nullptr);
  bool captureTextureUnit(SoCallbackAction * action, int unit, CoinRenderRenderStateSnapshot & rs, std::string * outError);
  void captureSortingCenter(SoCallbackAction * action);
  bool expandStyledPrimitives(std::string * outError);
  uint32_t addVertex(SoCallbackAction * action, const SoPrimitiveVertex * pv, uint32_t materialSlot);
  void ensureDrawPacket(CoinRenderPrimitiveTopology topology, uint32_t renderStateSlot, SoNode * node, bool forceNewPacket = false);

  CoinRenderFramePlan currentPlan;
  uint32_t currentDrawIndex;
  uint32_t nodeCounter;
  bool inFrame;
  bool hasActiveDraw;
  bool hasError;
  bool isUnsupported;
  uint32_t savedAnnotationLayer = 0;
  bool savedAnnotationClear = false;
  uint32_t foregroundLayer = 0;
  uint32_t annotationDepth;
  uint32_t currentAnnotationLayer;
  uint32_t nextAnnotationLayer;
  bool annotationDepthClearPending;
  std::string builderError;
  std::unordered_map<uint64_t, uint32_t> nodeOccurrenceCount;
  std::vector<SbVec3f> lightAttenuationByIndex;
  struct DirectTexture { uint64_t token; uint32_t width; uint32_t height; bool opaque; };
  std::unordered_map<const unsigned char *, DirectTexture> directTextures;
};

#endif // !COIN_RENDER_FRAME_PLAN_BUILDER_H
