#include <Inventor/CoinWgpuExport.h>
#ifndef SOWGPURENDERACTIONP_H
#define SOWGPURENDERACTIONP_H

#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include "rendering/wgpu/SoWgpuFramePlanBuilder.h"
#include "rendering/wgpu/SoWgpuRecordingBackend.h"
#include <string>

class COIN_WGPU_DLL_API SoWgpuRenderActionP {
public:
  SoWgpuRenderActionP(SoWgpuRenderAction * master = nullptr);
  ~SoWgpuRenderActionP();

  void initCallbacks();

  template <typename F>
  void executeApply(F traversalFn);

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

  static SoCallbackAction::Response lightPreCB(void * userdata,
                                               SoCallbackAction * action,
                                               const SoNode * node);

  static SoCallbackAction::Response sceneTexturePreCB(void * userdata,
                                                      SoCallbackAction * action,
                                                      const SoNode * node);

  static SoCallbackAction::Response indexedFaceSetPreCB(void * userdata,
                                                       SoCallbackAction * action,
                                                       const SoNode * node);

  static SoCallbackAction::Response indexedLineSetPreCB(void * userdata,
                                                       SoCallbackAction * action,
                                                       const SoNode * node);

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
  bool isApplying;
  bool hasReentrancyError;
  bool fastPathEnabled;
};

#endif // !SOWGPURENDERACTIONP_H
