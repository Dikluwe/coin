#include <Inventor/CoinWgpuExport.h>
#ifndef SOWGPURENDERACTIONP_H
#define SOWGPURENDERACTIONP_H

#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include "rendering/wgpu/SoWgpuDiagnosticShell.h"
#include "rendering/wgpu/SoWgpuFramePlanBuilder.h"
#include "rendering/wgpu/SoWgpuFrameReuseCore.h"
#include "rendering/wgpu/SoWgpuRecordingBackend.h"
#include <Inventor/sensors/SoNodeSensor.h>
#include <string>
#include <deque>
#include <vector>
#include <memory>
#include <cstdint>
#include <cstddef>

class SoCamera;

class COIN_WGPU_DLL_API SoWgpuRenderActionP {
public:
  SoWgpuRenderActionP(SoWgpuRenderAction * master = nullptr);
  ~SoWgpuRenderActionP();

  void initCallbacks();
  void setDiagnostic(const SoWgpuActionDiagnostic & diagnostic);
  static void cameraSensorCB(void * data, SoSensor * sensor);
  void rememberFrameRoot(SoNode * root);
  bool prepareCameraOverlay(SoNode * root, SoWgpuCameraOverlayUndo & undo);

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
  SoWgpuReadbackTicket * asyncTicket; // Only valid during applyAsync().
  SbViewportRegion viewport;
  SbColor4f backgroundColor;
  SoWgpuRenderAction::TransparencyType transparencyType;
  SoWgpuRenderAction::Status lastStatus;
  SoWgpuDiagnosticDomain lastDiagnosticDomain;
  SbString lastError;
  mutable SbString lastRecordingLog;

  SoWgpuFramePlanBuilder builder;
  SoWgpuRecordingBackend recordingBackend;
  FramePlan lastValidPlan;
  // Owns staged scene-texture pixels for the entire parent traversal.
  std::deque<std::vector<uint8_t> > sceneTexturePixels;
  // Shared reservation for staged RGBA8 or direct color+depth attachments.
  std::shared_ptr<size_t> sceneTextureStagedBytes;

  // Private GPU RTT tokens owned by the top-level apply, shared with children.
  std::shared_ptr<std::vector<uint64_t> > sceneTextureDirectTokens;
  struct DirectPass {
    FramePlan plan;
    SbVec2i32 size;
    uint64_t producerKey = 0;
  };
  // Per-apply topological order; identifiers are one-based indices until submit.
  std::shared_ptr<std::vector<DirectPass> > directPasses;
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
