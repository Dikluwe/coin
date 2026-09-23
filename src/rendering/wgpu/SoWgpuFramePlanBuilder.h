#ifndef COIN_SOWGPUFRAMEPLANBUILDER_H
#define COIN_SOWGPUFRAMEPLANBUILDER_H

#include "rendering/wgpu/SoWgpuFramePlan.h"
#include "rendering/wgpu/SoWgpuDirectGeometry.h"
#include <Inventor/SbViewportRegion.h>

class SoCallbackAction;
class SoPrimitiveVertex;
class SoNode;

class SoWgpuFramePlanBuilder {
public:
  SoWgpuFramePlanBuilder();
  ~SoWgpuFramePlanBuilder();

  void beginFrame(const SbColor4f & clearColor, const SbViewportRegion & viewport);
  void reset();

  void addTriangle(SoCallbackAction * action,
                   const SoPrimitiveVertex * v0,
                   const SoPrimitiveVertex * v1,
                   const SoPrimitiveVertex * v2);

  void addLine(SoCallbackAction * action,
               const SoPrimitiveVertex * v0,
               const SoPrimitiveVertex * v1);

  void addPoint(SoCallbackAction * action,
                const SoPrimitiveVertex * vertex);

  FastPathResult processIndexedFaceSet(SoCallbackAction * action,
                                       const DirectGeometryView & view,
                                       SoNode * node,
                                       std::string * outError = nullptr);

  FastPathResult processIndexedLineSet(SoCallbackAction * action,
                                       const DirectGeometryView & view,
                                       SoNode * node,
                                       std::string * outError = nullptr);

  bool build(FramePlan & outPlan, std::string * outError = nullptr);

private:
  uint32_t captureMaterial(SoCallbackAction * action, int materialIndex);
  uint32_t captureRenderState(SoCallbackAction * action, int materialIndex);
  uint32_t addVertex(const SoPrimitiveVertex * pv, uint32_t materialSlot);
  void ensureDrawPacket(PrimitiveTopology topology, uint32_t renderStateSlot, SoNode * node);

  FramePlan currentPlan;
  uint32_t currentDrawIndex;
  uint32_t nodeCounter;
  bool inFrame;
  bool hasActiveDraw;
};

#endif // !COIN_SOWGPUFRAMEPLANBUILDER_H
