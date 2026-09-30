#include <Inventor/CoinRenderExport.h>
#ifndef SOWGPURENDERACTIONP_H
#define SOWGPURENDERACTIONP_H

#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderFramePlanBuilder.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinrender/CoinRenderRecordingBackend.h"
#include "rendering/coinrender/CoinRenderRttCore.h"
#include <Inventor/sensors/SoNodeSensor.h>
#include <string>
#include <deque>
#include <vector>
#include <memory>
#include <cstdint>
#include <cstddef>

class SoCamera;

class COIN_RENDER_DLL_API CoinRenderActionP {
public:
  CoinRenderActionP(CoinRenderAction * master = nullptr);
  ~CoinRenderActionP();

  void initCallbacks();
  void setDiagnostic(const CoinRenderActionDiagnostic & diagnostic);
  static void cameraSensorCB(void * data, SoSensor * sensor);
  void rememberFrameRoot(SoNode * root);
  bool prepareCameraOverlay(SoNode * root, CoinRenderCameraOverlayUndo & undo);

  template <typename F>
  void executeApply(F traversalFn, SoNode * cacheRoot = NULL);

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

  static SoCallbackAction::Response depthBufferPreCB(void * userdata,
                                                     SoCallbackAction * action,
                                                     const SoNode * node);

  static SoCallbackAction::Response annotationPreCB(void * userdata,
                                                    SoCallbackAction * action,
                                                    const SoNode * node);
  static SoCallbackAction::Response annotationPostCB(void * userdata,
                                                     SoCallbackAction * action,
                                                     const SoNode * node);

  static SoCallbackAction::Response textureUnitsPreCB(void *, SoCallbackAction *, const SoNode *);
  static SoCallbackAction::Response textureCombinePreCB(void *, SoCallbackAction *, const SoNode *);
  static SoCallbackAction::Response unsupportedEffectPreCB(void *, SoCallbackAction *, const SoNode *);

  static SoCallbackAction::Response sceneTexturePreCB(void * userdata,
                                                      SoCallbackAction * action,
                                                      const SoNode * node);

  static SoCallbackAction::Response indexedFaceSetPreCB(void * userdata,
                                                       SoCallbackAction * action,
                                                       const SoNode * node);

  static SoCallbackAction::Response indexedLineSetPreCB(void * userdata,
                                                       SoCallbackAction * action,
                                                       const SoNode * node);

  CoinRenderAction * master;
  CoinRenderTarget * target;
  CoinRenderReadbackTicket * asyncTicket; // Only valid during applyAsync().
  SbViewportRegion viewport;
  SbColor4f backgroundColor;
  CoinRenderAction::TransparencyType transparencyType;
  CoinRenderTransparencyOptions transparencyOptions;
  CoinRenderOptions executionOptions;
  CoinRenderAction::Status lastStatus;
  CoinRenderDiagnosticDomain lastDiagnosticDomain;
  SbString lastError;
  mutable SbString lastRecordingLog;

  CoinRenderFramePlanBuilder builder;
  CoinRenderRecordingBackend recordingBackend;
  CoinRenderFramePlan lastValidPlan;
  // Owns staged scene-texture pixels for the entire parent traversal.
  std::deque<std::vector<uint8_t> > sceneTexturePixels;
  // Shared logical graph, containing no backend handles or resources.
  std::shared_ptr<CoinRenderRttPlan> sceneTexturePlan;
  struct DelayedAnnotation {
    SoPath * path;
    int priority;
  };
  std::vector<DelayedAnnotation> delayedAnnotations;
  std::vector<DelayedAnnotation> delayedOverlays;
  bool replayingAnnotations = false;
  bool planOnly = false;
  bool hasLastValidPlan;
  mutable bool recordingLogValid;
  bool isApplying;
  bool hasReentrancyError;
  bool fastPathEnabled;
  SoNode * cachedRoot = NULL;
  SbUniqueId cachedRootId = 0;
  SoNodeSensor cameraSensor;
  SoCamera * cachedCamera = NULL;
  bool cameraOnlyDirty = false;
  bool cameraPatchInvalidated = false;
};

#endif // !SOWGPURENDERACTIONP_H
