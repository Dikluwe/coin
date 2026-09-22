#ifndef SOWGPURENDERACTIONP_H
#define SOWGPURENDERACTIONP_H

#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include "rendering/wgpu/SoWgpuFramePlanBuilder.h"
#include "rendering/wgpu/SoWgpuRecordingBackend.h"

class SoWgpuRenderActionP {
public:
  SoWgpuRenderActionP(SoWgpuRenderAction * master = nullptr);
  ~SoWgpuRenderActionP();

  void initCallbacks();

  static void triangleCB(void * userdata,
                         SoCallbackAction * action,
                         const SoPrimitiveVertex * v0,
                         const SoPrimitiveVertex * v1,
                         const SoPrimitiveVertex * v2);

  static void lineCB(void * userdata,
                     SoCallbackAction * action,
                     const SoPrimitiveVertex * v0,
                     const SoPrimitiveVertex * v1);

  static void pointCB(void * userdata,
                      SoCallbackAction * action,
                      const SoPrimitiveVertex * vertex);

  SoWgpuRenderAction * master;
  SoWgpuRenderTarget * target;
  SbViewportRegion viewport;
  SbColor4f backgroundColor;
  SoWgpuRenderAction::Status lastStatus;
  SbString lastError;
  SbString lastRecordingLog;

  SoWgpuFramePlanBuilder builder;
  SoWgpuRecordingBackend recordingBackend;
  FramePlan lastValidPlan;

  bool hasLastValidPlan;
  bool isTraversing;
};

#endif // !SOWGPURENDERACTIONP_H
