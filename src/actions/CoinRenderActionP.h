#include <Inventor/CoinRenderExport.h>
#ifndef SOWGPURENDERACTIONP_H
#define SOWGPURENDERACTIONP_H

#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderShaderModuleCore.h"
#include "rendering/coinrender/CoinRenderFramePlanBuilder.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinrender/CoinRenderShadowCore.h"
#include "rendering/coinrender/CoinRenderRecordingBackend.h"
#include "rendering/coinrender/CoinRenderRttCore.h"
#include <Inventor/sensors/SoNodeSensor.h>
#include <string>
#include <deque>
#include <vector>
#include <tuple>
#include <memory>
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>

class SoCamera;
class SoShadowGroup;
class SoSeparator;
class SoCube;
class CoinRenderRttCaptureMemo;
class SoSFVec3f;
class SoMaterial;

class COIN_RENDER_DLL_API CoinRenderActionP {
public:
  CoinRenderFrameBorrow::Identity executionBorrowIdentity;
  CoinRenderActionP(CoinRenderAction * master = nullptr);
  ~CoinRenderActionP();

  std::unordered_map<SoShaderProgram *, std::shared_ptr<const CoinRenderShaderRegistration>> portableShaderModules;
  void initCallbacks();
  template <void (*Method)(SoAction *, SoNode *)>
  static void guardedNodeMethod(SoAction *, SoNode *);
  bool acceptNode(SoNode *);
  static void portableNodeMethod(SoAction *, SoNode *);
  static void shapeContentMethod(SoAction *, SoNode *);
  static void vrmlStateMethod(SoAction *, SoNode *);
  static SoCallbackAction::Response rttMutationPreCB(void *, SoCallbackAction *, const SoNode *);
  bool captureBoundingBox(SoCallbackAction *, SoNode *);
  void observeBoundingBox(SoCallbackAction *, SoNode *);
  static void screenContentMethod(SoAction *, SoNode *);
  static void markerContentMethod(SoAction *, SoNode *);
  void setDiagnostic(const CoinRenderActionDiagnostic & diagnostic);
  static void cameraSensorCB(void * data, SoSensor * sensor);
  static void vrmlSnapshotSensorCB(void * data, SoSensor * sensor);
  bool containsVRMLSnapshot(SoNode * root);
  static SoCallbackAction::Response cameraStereoPostCB(void *, SoCallbackAction *, const SoNode *);
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
  void executePassSnapshots(F traversalFn, SoNode * cacheRoot);

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
  static SoCallbackAction::Response cameraStereoPreCB(void *, SoCallbackAction *, const SoNode *);
  SbViewportRegion stereoCameraViewport;
  static SoCallbackAction::Response managerLayerPreCB(void *, SoCallbackAction *, const SoNode *);
  static SoCallbackAction::Response managerLayerPostCB(void *, SoCallbackAction *, const SoNode *);

  static SoCallbackAction::Response textureImagePreCB(void *, SoCallbackAction *, const SoNode *);
  static SoCallbackAction::Response rasterGeneratorPostCB(void *, SoCallbackAction *, const SoNode *);
  static SoCallbackAction::Response rasterStatePostCB(void *, SoCallbackAction *, const SoNode *);
  static SoCallbackAction::Response textureVolumePreCB(void *, SoCallbackAction *, const SoNode *);
  static SoCallbackAction::Response textureUnitsPreCB(void *, SoCallbackAction *, const SoNode *);
  static SoCallbackAction::Response shapePostCB(void *, SoCallbackAction *, const SoNode *);
  static SoCallbackAction::Response textureCombinePreCB(void *, SoCallbackAction *, const SoNode *);
  static void alphaTestMethod(SoAction *, SoNode *);
  static SoCallbackAction::Response unsupportedEffectPreCB(void *, SoCallbackAction *, const SoNode *);

  static SoCallbackAction::Response sceneCubeTexturePreCB(void *, SoCallbackAction *, const SoNode *);
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
  CoinRenderAction::DrawMode drawMode = CoinRenderAction::DrawMode::SCENE;
  SbColor4f overlayColor = SbColor4f(1.0f, 0.0f, 0.0f, 1.0f);
  SbBool texturesEnabled = TRUE;
  CoinRenderAction::CoinRenderAbortCB * abortCallback = nullptr;
  void * abortUserdata = nullptr;
  CoinRenderAction::AbortCode abortResponse = CoinRenderAction::CONTINUE;
  bool candidateAborted = false;
  bool invalidAbortCode = false;
  bool stateInvalidationPending = false;
  bool invalidatedDuringApply = false;
  struct PreRenderCallback { CoinRenderAction::CoinRenderPreRenderCB * callback; void * userdata; };
  std::vector<PreRenderCallback> preRenderCallbacks;
  SbVec2f updateOrigin = SbVec2f(0, 0), updateSize = SbVec2f(1, 1);
  CoinRenderAction::SortedObjectOrderStrategy sortingStrategy = CoinRenderAction::BBOX_CENTER;
  CoinRenderAction::CoinRenderSortedObjectOrderCB * sortingCallback = nullptr;
  void * sortingUserdata = nullptr;
  SbBool delayedObjDepthWrite = FALSE;
  SbBool smoothing = FALSE;
  CoinRenderAction::TransparentDelayedObjectRenderType delayedRenderType = CoinRenderAction::ONE_PASS;
  CoinRenderTransparencyOptions transparencyOptions;
  int numPasses = 1;
  CoinRenderAction::CoinRenderPassCB * passCallback = nullptr;
  void * passUserdata = nullptr;
  int currentPass = 0;
  int requestedExternalPass = 0;
  bool externalPass = false;
  bool capturingPassSnapshots = false;
  bool skipSnapshotPreRender = false;
  uint64_t multipassBufferBudget = UINT64_C(256) * 1024 * 1024;
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
  bool encounteredActiveBump = false;
  bool boundingBoxObservers = false;
  std::vector<int> shadowStyleBeforeGroups;
  std::vector<const SoShadowGroup *> activeShadowGroupNodes;
  // Owns staged scene-texture pixels for the entire parent traversal.
  std::deque<std::vector<uint8_t> > sceneTexturePixels;
  // Shared logical graph, containing no backend handles or resources.
  std::shared_ptr<CoinRenderRttPlan> sceneTexturePlan;
  std::shared_ptr<CoinRenderRttCaptureMemo> sceneTextureCaptureMemo;
  struct DelayedAnnotation {
    SoPath * path;
    int priority;
  };
  std::vector<DelayedAnnotation> delayedAnnotations;
  std::vector<DelayedAnnotation> delayedOverlays;
  std::vector<std::tuple<const SoNode *, bool, int>> managerLayerBoundaries;
  std::vector<std::tuple<const SoNode *, bool, int>> capturedManagerLayerBoundaries;
  std::vector<uint32_t> managerCapturedLayers;
  unsigned int managerHighlightDepth = 0;
  bool replayingAnnotations = false;
  bool planOnly = false;
  // -1=ordinary; 0=authored background; 1=per-eye base + generated highlight;
  // 2=post-stereo monoscopic authored foreground.
  int managerCaptureSection = -1;
  bool sceneTextureBumpProducer = false;
  bool inheritedTransparencyOverride = false;
  // Technical base passes replay a single logical RTT capture. This cache is
  // scoped to those passes, never shared across authored layer occurrences.
  struct TechnicalRttCapture {
    const SoNode * source;
    uint64_t revision;
    std::vector<uint64_t> context;
    std::vector<int> occurrencePath;
    uint64_t producer;
  };
  bool technicalRttCapture = false, technicalRttReplay = false;
  std::vector<TechnicalRttCapture> technicalRttCaptures;
  bool hasLastValidPlan;
  mutable bool recordingLogValid;
  bool isApplying;
  bool hasReentrancyError;
  bool fastPathEnabled;
  uint64_t captureCallbackRevision = 0;
  SoNode * cachedRoot = NULL;
  SbUniqueId cachedRootId = 0;
  struct CameraStereoState {
    const SoCamera * camera;
    int mode;
    float adjustment;
    float balance;
  };
  std::vector<CameraStereoState> capturedStereoStates;
  std::vector<CameraStereoState> frameStereoStates;
  // This is a negative-presence proof, independent of a successful frame or
  // the narrower camera-overlay qualification. One root auditor covers child
  // lists and SFNode/MFNode references; it neither retains nor walks the root.
  SoNodeSensor vrmlSnapshotSensor;
  SbUniqueId vrmlSnapshotRootId = 0;
  bool vrmlSnapshotValid = false;
  bool vrmlSnapshotPresent = false;
  bool vrmlSnapshotCameraIndependent = false;
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
