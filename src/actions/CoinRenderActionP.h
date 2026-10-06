#include <Inventor/CoinRenderExport.h>
#ifndef SOWGPURENDERACTIONP_H
#define SOWGPURENDERACTIONP_H

#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderFramePlanBuilder.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinrender/CoinRenderShadowCore.h"
#include "rendering/coinrender/CoinRenderRecordingBackend.h"
#include "rendering/coinrender/CoinRenderRttCore.h"
#include <Inventor/sensors/SoNodeSensor.h>
#include <string>
#include <deque>
#include <vector>
#include <memory>
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>

class SoCamera;
class SoShadowGroup;
class SoSeparator;
class SoCube;
class SoSFVec3f;
class SoMaterial;

class COIN_RENDER_DLL_API CoinRenderActionP {
public:
  CoinRenderActionP(CoinRenderAction * master = nullptr);
  ~CoinRenderActionP();

  void initCallbacks();
  static void shapeContentMethod(SoAction *, SoNode *);
  bool captureBoundingBox(SoCallbackAction *, SoNode *);
  void observeBoundingBox(SoCallbackAction *, SoNode *);
  static void screenContentMethod(SoAction *, SoNode *);
  static void markerContentMethod(SoAction *, SoNode *);
  void setDiagnostic(const CoinRenderActionDiagnostic & diagnostic);
  static void cameraSensorCB(void * data, SoSensor * sensor);
  void rememberFrameRoot(SoNode * root, bool qualifyCamera);
  bool prepareCameraOverlay(SoNode * root, CoinRenderCameraOverlayUndo & undo);
  void beginTranslationCapture(bool enabled);
  void beginTranslationShape(SoCallbackAction *, const SoNode *);
  void endTranslationShape();
  // capturedBasis is a successful proof lent only by the current
  // rememberFrameRoot call, after the submitted plan has been installed.
  void qualifyTranslationCapture(SoNode * root,
                                 const CoinRenderCameraOverlayBasis * capturedBasis);
  void clearTranslationProof();
  bool prepareTranslationOverlay(SoNode *, CoinRenderObjectOverlayUndo &);
  void qualifyObjectPayloads(const std::unordered_map<const SoNode *, size_t> & materialVisits);
  void commitTranslationOverlay();
  static SoCallbackAction::Response translationPreCB(void *, SoCallbackAction *, const SoNode *);

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

  static SoCallbackAction::Response shadowGroupPreCB(void *, SoCallbackAction *, const SoNode *);
  static SoCallbackAction::Response shadowGroupPostCB(void *, SoCallbackAction *, const SoNode *);
  static SoCallbackAction::Response shadowStylePreCB(void *, SoCallbackAction *, const SoNode *);

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

  static SoCallbackAction::Response textureImagePreCB(void *, SoCallbackAction *, const SoNode *);
  static SoCallbackAction::Response textureUnitsPreCB(void *, SoCallbackAction *, const SoNode *);
  static SoCallbackAction::Response shapePostCB(void *, SoCallbackAction *, const SoNode *);
  static SoCallbackAction::Response textureCombinePreCB(void *, SoCallbackAction *, const SoNode *);
  static void alphaTestMethod(SoAction *, SoNode *);
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
  CoinRenderFramePlan lastRejectedShadowFrame;
  CoinRenderShadowPlan lastRejectedShadowPlan;
  struct ShadowSceneCapture {
    uint32_t groupSlot;
    uint64_t lightRevision;
    uint32_t inheritedClipPlaneCount;
    CoinRenderFramePlan frame;
  };
  std::vector<ShadowSceneCapture> shadowSceneCaptures;
  bool capturingShadowScene = false;
  bool boundingBoxObservers = false;
  std::vector<int> shadowStyleBeforeGroups;
  std::vector<const SoShadowGroup *> activeShadowGroupNodes;
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
  bool inheritedTransparencyOverride = false;
  bool hasLastValidPlan;
  mutable bool recordingLogValid;
  bool isApplying;
  bool hasReentrancyError;
  bool fastPathEnabled;
  uint64_t captureCallbackRevision = 0;
  SoNode * cachedRoot = NULL;
  SbUniqueId cachedRootId = 0;
  SoNodeSensor cameraSensor;
  SoCamera * candidateCamera = NULL;
  SoCamera * cachedCamera = NULL;
  CoinRenderCameraOverlayBasis cameraOverlayBasis;
  // Last outer apply only; count capture-time Core prepares, including failed
  // attempts. Lazy prepareCameraOverlay work is deliberately excluded.
  size_t captureCameraBasisCalls = 0;
  size_t captureCameraBasisPrepares = 0;
  size_t captureCameraBasisReuses = 0;
  double captureCameraBasisPrepareMs = 0;
  bool cameraOnlyDirty = false;
  bool cameraPatchInvalidated = false;
  bool cameraRecaptureRequired = false;
  struct TranslationBinding {
    SoNode * transform = NULL;
    SoSeparator * parent = NULL;
    const SoNode * cube = NULL;
    SoSFVec3f * field = NULL;
    SbVec3f originalPosition;
    SbMatrix prefix, anchor;
    size_t firstDraw = 0, endDraw = 0;
    CoinRenderGeometryRange geometry;
    uint32_t stateSlot = 0;
    SoMaterial * material = NULL;
    SbVec3f cubeDimensions;
    bool overallMaterial = false;
    bool materialEligible = false, geometryEligible = false;
  };
  // At most 65,536 objects. One draw and one exclusively owned state per
  // object keep both proof and dirty-set storage bounded independently of GPU.
  std::vector<TranslationBinding> translationBindings, translationCapture;
  std::unordered_map<const SoNode *, size_t> translationByNode, translationCaptureByNode;
  std::unordered_set<size_t> translationDirty;
  std::unordered_map<const SoNode *, std::vector<size_t>> materialByNode, cubeByNode;
  std::unordered_set<const SoNode *> materialDirty, geometryDirty;
  std::vector<const SoNode *> capturedDrawSources;
  const SoNode * pendingCaptureShape = NULL;
  size_t firstCaptureDraw = 0;
  uint64_t translationGeneration = 0;
  uint64_t translationProofGeneration = 0;
  uint64_t translationProofRevision = 0;
  bool translationProofValid = false;
  bool translationInputDirty = false;
  bool translationInvalidated = false;
  bool capturingTranslations = false;
  bool translationCaptureInvalid = false;
  size_t translationShapeCandidate = SIZE_MAX;
};

#endif // !SOWGPURENDERACTIONP_H
