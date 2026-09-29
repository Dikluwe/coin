#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoSubAction.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoLight.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoTextureCombine.h>
#include <Inventor/elements/SoTextureCombineElement.h>
#include <Inventor/nodes/SoVertexProperty.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/bundles/SoTextureCoordinateBundle.h>
#include <Inventor/elements/SoCoordinateElement.h>
#include <Inventor/elements/SoNormalElement.h>
#include <Inventor/elements/SoMaterialBindingElement.h>
#include <Inventor/elements/SoNormalBindingElement.h>
#include <Inventor/elements/SoMultiTextureImageElement.h>
#include <Inventor/elements/SoMultiTextureEnabledElement.h>
#include <Inventor/elements/SoTextureQualityElement.h>
#include <Inventor/elements/SoTextureUnitElement.h>
#include <Inventor/elements/SoTextureOverrideElement.h>
#include <Inventor/elements/SoShapeStyleElement.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoOverrideElement.h>
#include <Inventor/elements/SoDepthBufferElement.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include "rendering/coinrender/CoinRenderDepthPolicyElement.h"
#include <algorithm>
#include <chrono>
#include <memory>
#include <cstdlib>
#include <iostream>
#include <Inventor/misc/SoState.h>

#include "actions/CoinRenderActionP.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinrender/CoinRenderImageCore.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderSelectionCore.h"
#include "actions/SoSubActionP.h"
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
#include "rendering/coinwgpu/CoinWgpuBackend.h"
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#elif defined(HAVE_COIN_BGFX)
#include "rendering/coinbgfx/CoinBgfxBackend.h"
#elif defined(HAVE_COIN_DAWN) || defined(HAVE_COIN_WGPU_NATIVE)
#include "rendering/coinwgpu/CoinWgpuNativeBackend.h"
#endif

SO_ACTION_SOURCE(CoinRenderAction);

void
CoinRenderAction::initClass(void)
{
  SO_ACTION_INTERNAL_INIT_CLASS(CoinRenderAction, SoCallbackAction);
  SO_ENABLE(CoinRenderAction, SoDepthBufferElement);
  SO_ENABLE(CoinRenderAction, SoTextureCombineElement);
  CoinRenderDepthPolicyElement::initClass();
  SO_ENABLE(CoinRenderAction, CoinRenderDepthPolicyElement);
}

SbBool
CoinRenderAction::isGpuBackendAvailable(void)
{
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  return CoinWgpuBackend::isAvailable() ? TRUE : FALSE;
#elif defined(HAVE_COIN_BGFX)
  CoinRenderCapabilities caps{};
  caps.struct_size = sizeof(caps);
  return coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
    &caps, sizeof(caps)) == 0 && caps.gpu_available ? TRUE : FALSE;
#elif defined(HAVE_COIN_DAWN) || defined(HAVE_COIN_WGPU_NATIVE)
  return CoinWgpuNativeBackend::isAvailable() ? TRUE : FALSE;
#else
  return FALSE;
#endif
}

CoinRenderAction::CoinRenderAction(void)
{
  this->pimpl->master = this;
  SO_ACTION_CONSTRUCTOR(CoinRenderAction);
  this->pimpl->viewport = SbViewportRegion(640, 512);
  this->pimpl->initCallbacks();
}

CoinRenderAction::CoinRenderAction(const SbViewportRegion & viewport)
  : inherited(viewport)
{
  this->pimpl->master = this;
  SO_ACTION_CONSTRUCTOR(CoinRenderAction);
  this->pimpl->viewport = viewport;
  this->setViewportRegion(viewport);
  this->pimpl->initCallbacks();
}

CoinRenderAction::~CoinRenderAction(void)
{
}

void
CoinRenderAction::setViewportRegion(const SbViewportRegion & region)
{
  inherited::setViewportRegion(region);
  this->pimpl->viewport = region;
  this->pimpl->cachedRoot = NULL;
}

const SbViewportRegion &
CoinRenderAction::getViewportRegion(void) const
{
  return this->pimpl->viewport;
}

void
CoinRenderAction::setRenderTarget(CoinRenderTarget * target)
{
#if defined(HAVE_COIN_BGFX)
  // The evaluation backend owns one process-wide BGFX instance. Switching
  // targets must release the old instance so the new target can prepare it.
  if (this->pimpl->target != target && this->pimpl->target != NULL &&
      dynamic_cast<CoinBgfxBackend *>(
        this->pimpl->target->getPimpl()->backend.get()) != NULL) {
    this->pimpl->target->getPimpl()->backend.reset();
  }
#endif
  this->pimpl->target = target;
  this->pimpl->cachedRoot = NULL;
}

CoinRenderTarget *
CoinRenderAction::getRenderTarget(void) const
{
  return this->pimpl->target;
}

void
CoinRenderAction::setBackgroundColor(const SbColor4f & color)
{
  this->pimpl->backgroundColor = color;
  this->pimpl->cachedRoot = NULL;
}

const SbColor4f &
CoinRenderAction::getBackgroundColor(void) const
{
  return this->pimpl->backgroundColor;
}

void
CoinRenderAction::setTransparencyType(TransparencyType type)
{
  if (this->pimpl->transparencyType != type) {
    this->pimpl->transparencyType = type;
    this->pimpl->cachedRoot = NULL;
  }
}

CoinRenderAction::TransparencyType
CoinRenderAction::getTransparencyType(void) const
{
  return this->pimpl->transparencyType;
}

void CoinRenderAction::setSortedLayersNumPasses(int passes) {
  if (this->pimpl->transparencyOptions.layers != static_cast<uint32_t>(passes)) {
    this->pimpl->transparencyOptions.layers = static_cast<uint32_t>(passes);
    this->pimpl->cachedRoot = NULL;
  }
}

int CoinRenderAction::getSortedLayersNumPasses(void) const {
  return static_cast<int>(this->pimpl->transparencyOptions.layers);
}

void CoinRenderAction::setTransparencyBufferBudget(uint64_t bytes) {
  if (this->pimpl->transparencyOptions.bufferBudget != bytes) {
    this->pimpl->transparencyOptions.bufferBudget = bytes;
    this->pimpl->cachedRoot = NULL;
  }
}

uint64_t CoinRenderAction::getTransparencyBufferBudget(void) const {
  return this->pimpl->transparencyOptions.bufferBudget;
}

void
CoinRenderAction::setFastPathEnabled(SbBool enable)
{
  this->pimpl->fastPathEnabled = (enable != FALSE);
  this->pimpl->cachedRoot = NULL;
}

SbBool
CoinRenderAction::isFastPathEnabled(void) const
{
  return this->pimpl->fastPathEnabled ? TRUE : FALSE;
}

CoinRenderAction::Status
CoinRenderAction::getLastStatus(void) const
{
  return this->pimpl->lastStatus;
}

const SbString &
CoinRenderAction::getLastError(void) const
{
  return this->pimpl->lastError;
}

const SbString &
CoinRenderAction::getRecordingLog(void) const
{
  if (this->pimpl->hasLastValidPlan && !this->pimpl->recordingLogValid) {
    this->pimpl->lastRecordingLog =
      this->pimpl->recordingBackend.recordToString(this->pimpl->lastValidPlan).c_str();
    this->pimpl->recordingLogValid = true;
  }
  return this->pimpl->lastRecordingLog;
}

void
CoinRenderAction::beginForegroundPass()
{
  if (this->pimpl->isApplying && !this->pimpl->replayingAnnotations)
    this->pimpl->builder.beginForeground();
}

void
CoinRenderAction::endForegroundPass()
{
  if (this->pimpl->isApplying && !this->pimpl->replayingAnnotations)
    this->pimpl->builder.endForeground();
}

SbBool
CoinRenderAction::deferOverlayPath(SoPath * supplied, int priority)
{
  if (!this->pimpl->isApplying || this->pimpl->replayingAnnotations) return FALSE;
  SoPath * path = (supplied ? supplied : this->getCurPath())->copy();
  path->ref();
  try { this->pimpl->delayedOverlays.push_back({path, priority}); }
  catch (...) { path->unref(); throw; }
  return TRUE;
}

SbBool
CoinRenderAction::deferAnnotation(int priority)
{
  if (!this->pimpl->isApplying || this->pimpl->replayingAnnotations) return FALSE;
  SoPath * path = this->getCurPath()->copy();
  path->ref();
  try { this->pimpl->delayedAnnotations.push_back({path, priority}); }
  catch (...) { path->unref(); throw; }
  return TRUE;
}

void
CoinRenderAction::apply(SoNode * root)
{
  this->pimpl->executeApply([&]() {
    if (root) {
      this->inherited::apply(root);
    }
  }, root);
}

void
CoinRenderAction::applyAsync(SoNode * root, CoinRenderReadbackTicket & outTicket)
{
  outTicket = CoinRenderReadbackTicket{};
  if (this->pimpl->isApplying) {
    this->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
      INVALID_SCENE, CoinRenderDiagnosticDomain::ACTION,
      SbString("Nested applyAsync() calls are not permitted")));
    this->pimpl->hasReentrancyError = true;
    return;
  }
  this->pimpl->asyncTicket = &outTicket;
  this->apply(root);
  this->pimpl->asyncTicket = NULL;
}

void
CoinRenderAction::apply(SoPath * path)
{
  this->pimpl->executeApply([&]() {
    if (path) {
      this->inherited::apply(path);
    }
  });
}

void
CoinRenderAction::apply(const SoPathList & pathlist, SbBool obeysrules)
{
  this->pimpl->executeApply([&]() {
    this->inherited::apply(pathlist, obeysrules);
  });
}

void
CoinRenderAction::beginTraversal(SoNode * root)
{
  if (this->pimpl->isApplying) {
    SoState * state = this->getState();
    state->push();
    SoShapeStyleElement::setTransparencyType(
      state, static_cast<int32_t>(this->pimpl->transparencyType));
    SoLazyElement::setTransparencyType(
      state, static_cast<int32_t>(this->pimpl->transparencyType));
    if (this->pimpl->transparencyType == SORTED_LAYERS_BLEND && root) {
      SoOverrideElement::setTransparencyTypeOverride(state, root, TRUE);
    }
    if (root) {
      this->inherited::beginTraversal(root);
    }
    state->pop();
    return;
  }

  this->pimpl->executeApply([&]() {
    if (root) {
      this->inherited::beginTraversal(root);
    }
  });
}

// CoinRenderActionP implementation

CoinRenderActionP::CoinRenderActionP(CoinRenderAction * m)
  : master(m),
    target(NULL),
    asyncTicket(NULL),
    backgroundColor(0.0f, 0.0f, 0.0f, 1.0f),
    transparencyType(CoinRenderAction::SCREEN_DOOR),
    lastStatus(CoinRenderAction::SUCCESS),
    lastDiagnosticDomain(CoinRenderDiagnosticDomain::NONE),
    hasLastValidPlan(false),
    recordingLogValid(false),
    isApplying(false),
    hasReentrancyError(false),
    fastPathEnabled(true),
    cameraSensor(CoinRenderActionP::cameraSensorCB, this)
{
  this->cameraSensor.setPriority(0);
}

CoinRenderActionP::~CoinRenderActionP()
{
}

void
CoinRenderActionP::setDiagnostic(const CoinRenderActionDiagnostic & diagnostic)
{
  this->lastStatus = diagnostic.status;
  this->lastDiagnosticDomain = diagnostic.domain;
  this->lastError = diagnostic.message;
}

namespace {
// Exact types only: custom subclasses and view-dependent traversal fall back.
bool
cameraStableScene(SoNode * root, SoCamera * camera)
{
  std::vector<SoNode *> pending(1, root);
  while (!pending.empty()) {
    SoNode * node = pending.back();
    pending.pop_back();
    const SoType type = node->getTypeId();
    if (node == camera) continue;
    if (type == SoSeparator::getClassTypeId() ||
        type == SoGroup::getClassTypeId()) {
      SoGroup * group = static_cast<SoGroup *>(node);
      for (int i = 0; i < group->getNumChildren(); ++i) {
        pending.push_back(group->getChild(i));
      }
    } else if (type != SoCoordinate3::getClassTypeId() &&
               type != SoNormal::getClassTypeId() &&
               type != SoNormalBinding::getClassTypeId() &&
               type != SoMaterial::getClassTypeId() &&
               type != SoMaterialBinding::getClassTypeId() &&
               type != SoShapeHints::getClassTypeId() &&
               type != SoLightModel::getClassTypeId() &&
               type != SoTranslation::getClassTypeId() &&
               type != SoIndexedFaceSet::getClassTypeId() &&
               type != SoIndexedLineSet::getClassTypeId() &&
               type != SoCube::getClassTypeId()) {
      return false;
    }
  }
  return true;
}
}

void
CoinRenderActionP::cameraSensorCB(void * data, SoSensor * sensor)
{
  CoinRenderActionP * self = static_cast<CoinRenderActionP *>(data);
  SoNodeSensor * nodeSensor = static_cast<SoNodeSensor *>(sensor);
  if (self->cachedCamera &&
      nodeSensor->getTriggerNode() == self->cachedCamera &&
      nodeSensor->getTriggerField() &&
      nodeSensor->getTriggerOperationType() == SoNotRec::FIELD_UPDATE) {
    self->cameraOnlyDirty = true;
  } else {
    self->cameraPatchInvalidated = true;
  }
}

void
CoinRenderActionP::rememberFrameRoot(SoNode * root)
{
  this->cameraSensor.detach();
  this->cachedCamera = NULL;
  this->cameraOnlyDirty = false;
  this->cameraPatchInvalidated = false;
  if (!root || root->getTypeId() != SoSeparator::getClassTypeId()) return;
  SoSeparator * group = static_cast<SoSeparator *>(root);
  if (group->getNumChildren() < 2) return;
  SoNode * first = group->getChild(0);
  const SoType type = first->getTypeId();
  if (type != SoPerspectiveCamera::getClassTypeId() &&
      type != SoOrthographicCamera::getClassTypeId()) return;
  SoCamera * camera = static_cast<SoCamera *>(first);
  if (camera->viewportMapping.getValue() != SoCamera::ADJUST_CAMERA &&
      camera->viewportMapping.getValue() != SoCamera::LEAVE_ALONE) return;
  if (!cameraStableScene(root, camera)) return;
  this->cachedCamera = camera;
  this->cameraSensor.attach(root);
}

bool
CoinRenderActionP::prepareCameraOverlay(SoNode * root,
                                         CoinRenderCameraOverlayUndo & undo)
{
  if (!root || root != this->cachedRoot || !this->cachedCamera ||
      this->cameraSensor.getAttachedNode() != root ||
      !this->cameraOnlyDirty || this->cameraPatchInvalidated ||
      static_cast<SoGroup *>(root)->getChild(0) != this->cachedCamera) return false;

  SbViewportRegion adjusted;
  const SbViewVolume vv = this->cachedCamera->getViewVolume(
    this->master->getViewportRegion(), adjusted, SbMatrix::identity());
  if (adjusted != this->master->getViewportRegion()) return false;
  CoinRenderCameraSnapshot snapshot;
  if (vv.getDepth() == 0.0f || vv.getWidth() == 0.0f ||
      vv.getHeight() == 0.0f) {
    snapshot.viewMatrix = SbMatrix::identity();
    snapshot.projectionMatrixCoin = SbMatrix::identity();
  } else {
    vv.getMatrices(snapshot.viewMatrix, snapshot.projectionMatrixCoin);
  }
  snapshot.isPerspective = vv.getProjectionType() == SbViewVolume::PERSPECTIVE;
  snapshot.nearDistance = vv.getNearDist();
  snapshot.farDistance = vv.getNearDist() + vv.getDepth();
  if (snapshot.isPerspective && snapshot.nearDistance <= 0.0f)
    snapshot.nearDistance = 0.1f;
  if (snapshot.farDistance <= snapshot.nearDistance)
    snapshot.farDistance = snapshot.nearDistance + 100.0f;
  snapshot.focalDistance = this->cachedCamera->focalDistance.getValue();
  snapshot.aspectRatio = adjusted.getViewportAspectRatio();
  if (this->cameraPatchInvalidated ||
      this->cameraSensor.getAttachedNode() != root) return false;
  return CoinRenderFrameReuseCore::beginCameraOverlay(
    this->lastValidPlan, snapshot,
    CoinRenderFramePlanBuilder::nextRevision(), undo);
}

template <typename F>
void
CoinRenderActionP::executeApply(F traversalFn, SoNode * cacheRoot)
{
  typedef std::chrono::steady_clock ProfileClock;
  const ProfileClock::time_point profileBegin = ProfileClock::now();
  if (this->target)
    this->executionOptions = this->target->pimpl->options;
  std::string optionsDiagnostic;
  if ((this->target && !this->target->pimpl->optionsDiagnostic.empty()) ||
      !coin_render_valid_options(this->executionOptions, optionsDiagnostic)) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::TARGET,
        SbString(this->target && !this->target->pimpl->optionsDiagnostic.empty()
                     ? this->target->pimpl->optionsDiagnostic.c_str()
                     : optionsDiagnostic.c_str())));
    return;
  }
  const bool tracePhases = CoinRenderDiagnosticShell::phaseTracingEnabled();
  if (this->isApplying) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::ACTION,
        SbString("Nested apply() calls are not permitted on CoinRenderAction")));
    this->hasReentrancyError = true;
    return;
  }

  struct AnnotationScope {
    CoinRenderActionP * action;
    ~AnnotationScope() {
      for (const auto & entry : action->delayedAnnotations) entry.path->unref();
      action->delayedAnnotations.clear();
      for (const auto & entry : action->delayedOverlays) entry.path->unref();
      action->delayedOverlays.clear();
      action->replayingAnnotations = false;
      action->isApplying = false;
    }
  } annotationScope{this};
  this->isApplying = true;
  this->hasReentrancyError = false;
  bool planCacheAllowed = true;

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE) || defined(HAVE_COIN_BGFX)
  const bool ownsDirectTokens = !this->sceneTextureDirectTokens;
  if (ownsDirectTokens) {
    this->sceneTextureDirectTokens = std::make_shared<std::vector<uint64_t> >();
  }
  struct DirectTextureScope {
    CoinRenderActionP * action;
    bool owns;
    ~DirectTextureScope() {
      if (!owns) return;
      for (uint64_t token : *action->sceneTextureDirectTokens) {
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
        coin_wgpu_release_texture(token);
#endif
      }
#if defined(HAVE_COIN_BGFX)
      if (action->target) {
        CoinBgfxBackend * backend = dynamic_cast<CoinBgfxBackend *>(
          action->target->getPimpl()->backend.get());
        if (backend) backend->finishDirectTextures(*action->sceneTextureDirectTokens);
      }
#endif
      action->sceneTextureDirectTokens.reset();
    }
  } directTextureScope{this, ownsDirectTokens};

  const bool ownsDirectPasses =
      !this->directPasses && !this->planOnly && this->target &&
      this->target->pimpl->kind == CoinRenderTargetP::KIND_OFFSCREEN &&
      this->executionOptions.sceneTexture == COIN_RENDER_SCENE_TEXTURE_DIRECT;
  if (ownsDirectPasses) {
    this->directPasses = std::make_shared<std::vector<DirectPass> >();
    planCacheAllowed = false;
  }
  struct DirectPassScope {
    CoinRenderActionP * action;
    bool owns;
    ~DirectPassScope() { if (owns) action->directPasses.reset(); }
  } directPassScope{this, ownsDirectPasses};
#endif
  const bool traversalSkipped = planCacheAllowed && cacheRoot && this->hasLastValidPlan &&
    this->cachedRoot == cacheRoot && this->cachedRootId == cacheRoot->getNodeId() &&
    !this->cameraOnlyDirty && !this->cameraPatchInvalidated;
  CoinRenderFrameReuseDecision reuseDecision = traversalSkipped
    ? CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::REUSE,
                               this->lastValidPlan.revision)
    : CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::UNKNOWN, 0);
  this->sceneTexturePixels.clear();
  if (!this->sceneTextureStagedBytes) {
    this->sceneTextureStagedBytes = std::make_shared<size_t>(0);
  }
  CoinRenderFramePlan plan;
  CoinRenderCameraOverlayUndo overlayUndo;
  struct CameraOverlayScope {
    CoinRenderFramePlan & frame;
    CoinRenderCameraOverlayUndo & undo;
    bool committed;
    ~CameraOverlayScope() {
      if (!committed) CoinRenderFrameReuseCore::rollbackCameraOverlay(frame, undo);
    }
  } overlayScope{this->lastValidPlan, overlayUndo, false};
  ProfileClock::time_point profileTraversed = ProfileClock::now();
  ProfileClock::time_point profilePlanned = profileTraversed;
  const bool cameraOverlay = !traversalSkipped && planCacheAllowed &&
    !this->planOnly && this->hasLastValidPlan &&
    this->prepareCameraOverlay(cacheRoot, overlayUndo);
  if (cameraOverlay) {
    reuseDecision = CoinRenderFrameReuseDecision(
      CoinRenderFrameReuseKind::CAMERA_PATCH, overlayUndo.revision);
  }
  if (!traversalSkipped && !cameraOverlay) {
    this->builder.beginFrame(this->backgroundColor, this->master->getViewportRegion());
    traversalFn();
    const uint32_t delayedLayers = (this->delayedOverlays.empty() ? 0 : 1) +
                                  (this->delayedAnnotations.empty() ? 0 : 1);
    if (delayedLayers && !this->master->hasTerminated()) {
      this->builder.reserveDelayedLayers(delayedLayers);
      uint32_t layer = 1;
      auto replay = [&](std::vector<DelayedAnnotation> & paths, bool clearDepth) {
        if (paths.empty() || this->master->hasTerminated()) return;
        std::stable_sort(paths.begin(), paths.end(),
          [](const DelayedAnnotation & a, const DelayedAnnotation & b) {
            return a.priority < b.priority;
          });
        this->builder.beginDelayedAnnotations(layer++, clearDepth);
        this->replayingAnnotations = true;
        for (const auto & entry : paths) {
          this->master->SoCallbackAction::apply(entry.path);
          if (this->master->hasTerminated()) break;
        }
        this->builder.endAnnotation();
        this->replayingAnnotations = false;
      };
      replay(this->delayedOverlays, false);
      replay(this->delayedAnnotations, true);
    }
    profileTraversed = ProfileClock::now();
  }

  this->isApplying = false;

  this->sceneTextureStagedBytes.reset();
  if (this->hasReentrancyError) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::ACTION,
      SbString("Nested apply() calls are not permitted on CoinRenderAction")));
    return;
  }

  if (this->master->hasTerminated() && this->lastStatus != CoinRenderAction::SUCCESS) {
    return;
  }

  std::string err;
  if (!traversalSkipped && !cameraOverlay && !this->builder.build(plan, &err)) {
    const CoinRenderAction::Status status = this->builder.isUnsupportedBuild()
      ? CoinRenderAction::UNSUPPORTED : CoinRenderAction::INVALID_SCENE;
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      status, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(err.c_str())));
    return;
  }
  profilePlanned = ProfileClock::now();
  if (!traversalSkipped && !cameraOverlay) {
    plan.transparency = this->transparencyOptions;
    plan.transparency.mode = this->executionOptions.transparency;
    if (planCacheAllowed && cacheRoot && this->hasLastValidPlan &&
        this->cachedRoot == cacheRoot) {
      reuseDecision = CoinRenderFrameReuseCore::classify(this->lastValidPlan, plan);
    } else if (planCacheAllowed) {
      reuseDecision = CoinRenderFrameReuseDecision(
        CoinRenderFrameReuseKind::FULL_REBUILD, 0);
    }
  }
  const bool reusePreviousPlan =
    reuseDecision.kind == CoinRenderFrameReuseKind::REUSE &&
    this->hasLastValidPlan;
  const bool useCachedPlan = reusePreviousPlan || cameraOverlay;
  const CoinRenderFramePlan & framePlan = useCachedPlan ? this->lastValidPlan : plan;

  if (this->planOnly) {
    if (!useCachedPlan) {
      this->lastValidPlan = std::move(plan);
      this->hasLastValidPlan = true;
    }
    this->setDiagnostic(CoinRenderDiagnosticShell::success());
    return;
  }

  if (this->target == NULL) {
    if (this->asyncTicket) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::NO_TARGET, CoinRenderDiagnosticDomain::TARGET,
        SbString("applyAsync() requires an offscreen render target")));
      return;
    }
    // Mode 0: Recording backend
    this->lastRecordingLog = this->recordingBackend.recordToString(framePlan).c_str();
    this->recordingLogValid = true;
    if (!useCachedPlan) {
      this->lastValidPlan = std::move(plan);
      this->hasLastValidPlan = true;
    }
    if (cacheRoot) {
      this->cachedRoot = cacheRoot;
      this->cachedRootId = cacheRoot->getNodeId();
      if (cameraOverlay) this->cameraOnlyDirty = false;
      else if (!traversalSkipped)
        this->rememberFrameRoot(planCacheAllowed ? cacheRoot : NULL);
    }
    overlayScope.committed = true;
    this->setDiagnostic(CoinRenderDiagnosticShell::success());
    return;
  }

  // Target provided: validate target status
  if (this->target->getStatus() == CoinRenderTarget::TARGET_ERROR) {
    const char * tgtErr = this->target->getLastError();
    this->setDiagnostic(CoinRenderDiagnosticShell::fromTarget(
      this->target->getStatus(), (tgtErr && tgtErr[0])
        ? tgtErr : "Render target is in fatal TARGET_ERROR state"));
    return;
  }

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE) || defined(HAVE_COIN_BGFX)
  if (this->directPasses) {
    auto setGraphFailure = [this](const CoinRenderFrameExecutionResult & result) {
      this->setDiagnostic(CoinRenderDiagnosticShell::fromBackend(result));
    };
    // Validate every producer and the consumer before the first GPU submit.
    // Child plans are appended in postorder, so only earlier IDs are legal.
    for (size_t i = 0; i < this->directPasses->size(); ++i) {
      const DirectPass & pass = (*this->directPasses)[i];
      for (const CoinRenderTextureImageSnapshot & texture : pass.plan.textures) {
        if (texture.gpuToken > i) {
          this->setDiagnostic(CoinRenderDiagnosticShell::action(
            CoinRenderAction::INVALID_SCENE,
            CoinRenderDiagnosticDomain::FRAME_PLAN,
            SbString("SoSceneTexture2 pass references a missing or future producer")));
          return;
        }
      }
      CoinRenderFrameExecutionResult check = CoinRenderTargetP::validateProfile(pass.plan, pass.size);
      if (check.status != CoinRenderBackendStatus::SUCCESS) {
        setGraphFailure(check);
        return;
      }
    }
    for (const CoinRenderTextureImageSnapshot & texture : plan.textures) {
      if (texture.gpuToken > this->directPasses->size()) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::INVALID_SCENE,
          CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("Parent pass references a missing SoSceneTexture2 producer")));
        return;
      }
    }
    CoinRenderFrameExecutionResult rootCheck =
      CoinRenderTargetP::validateProfile(plan, this->target->pimpl->size);
    if (rootCheck.status != CoinRenderBackendStatus::SUCCESS) {
      setGraphFailure(rootCheck);
      return;
    }

    std::vector<uint64_t> resolved(this->directPasses->size() + 1, 0);
    auto resolveTextures = [&resolved](CoinRenderFramePlan & frame) {
      for (CoinRenderTextureImageSnapshot & texture : frame.textures) {
        if (texture.gpuToken != 0) {
          texture.gpuToken = resolved[static_cast<size_t>(texture.gpuToken)];
          texture.contentDigest = texture.gpuToken;
        }
      }
    };
#if defined(HAVE_COIN_BGFX)
    if (!this->target->pimpl->backend) {
      this->target->pimpl->backend.reset(new CoinBgfxBackend());
      const CoinRenderBackendStatus prepared =
        this->target->pimpl->backend->prepare(this->target->getPimpl().get());
      if (prepared != CoinRenderBackendStatus::SUCCESS) {
        setGraphFailure(CoinRenderSubmitResult(prepared,
          this->target->pimpl->backend->getLastError()));
        return;
      }
    }
    CoinBgfxBackend * directBgfx = dynamic_cast<CoinBgfxBackend *>(
      this->target->pimpl->backend.get());
    if (!directBgfx) {
      setGraphFailure(CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED,
        "SoSceneTexture2 direct graph requires the BGFX backend"));
      return;
    }
#endif
    for (size_t i = 0; i < this->directPasses->size(); ++i) {
      const DirectPass & pass = (*this->directPasses)[i];
      CoinRenderFramePlan childFrame = pass.plan;
      resolveTextures(childFrame);
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
      std::unique_ptr<CoinRenderTarget> childTarget(
          CoinRenderTargetP::createDirectOffscreen(pass.size, this->target->pimpl->options));
      if (!childTarget || childTarget->getStatus() != CoinRenderTarget::TARGET_READY) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::BACKEND_ERROR,
          CoinRenderDiagnosticDomain::TARGET,
          SbString("Cannot create planned SoSceneTexture2 offscreen target")));
        return;
      }
      CoinRenderFrameExecutionResult result = childTarget->pimpl->executeFrame(childFrame);
      if (result.status != CoinRenderBackendStatus::SUCCESS) {
        setGraphFailure(result);
        return;
      }
      const uint64_t token = childTarget->pimpl->directTextureToken;
#else
      uint64_t token = 0;
      CoinRenderFrameExecutionResult result = directBgfx->submitDirectTexture(
        childFrame, pass.size, pass.producerKey, token);
      if (result.status != CoinRenderBackendStatus::SUCCESS) {
        setGraphFailure(result);
        return;
      }
#endif
      if (!token) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::BACKEND_ERROR,
          CoinRenderDiagnosticDomain::BACKEND,
          SbString("Planned SoSceneTexture2 pass returned no GPU texture")));
        return;
      }
      resolved[i + 1] = token;
      this->sceneTextureDirectTokens->push_back(token);
    }
    resolveTextures(plan);
  }
#endif

  // Execute frame on target
  CoinRenderFrameExecutionResult execRes = this->asyncTicket
    ? this->target->pimpl->executeFrameAsync(
        framePlan, *this->asyncTicket, reuseDecision)
    : this->target->pimpl->executeFrame(framePlan, reuseDecision);
  if (execRes.status != CoinRenderBackendStatus::SUCCESS) {
    this->setDiagnostic(CoinRenderDiagnosticShell::fromBackend(execRes));
    return;
  }

  const ProfileClock::time_point profileExecuted = ProfileClock::now();
  if (tracePhases) {
    CoinRenderActionPhaseSample sample;
    sample.traversalMs = std::chrono::duration<double, std::milli>(
      profileTraversed - profileBegin).count();
    sample.framePlanMs = std::chrono::duration<double, std::milli>(
      profilePlanned - profileTraversed).count();
    sample.backendMs = std::chrono::duration<double, std::milli>(
      profileExecuted - profilePlanned).count();
    sample.vertices = framePlan.vertices.size();
    sample.indices = framePlan.indices.size();
    sample.draws = framePlan.draws.size();
    sample.planCacheHit = traversalSkipped || cameraOverlay;
    sample.reuseKind = reuseDecision.kind;
    std::cerr << CoinRenderDiagnosticShell::formatActionPhase(sample) << '\n';
  }
  if (!useCachedPlan) {
    this->lastValidPlan = std::move(plan);
    this->hasLastValidPlan = true;
    this->recordingLogValid = false;
  } else if (cameraOverlay) {
    this->recordingLogValid = false;
  }
  if (cacheRoot) {
    this->cachedRoot = cacheRoot;
    this->cachedRootId = cacheRoot->getNodeId();
    if (cameraOverlay) this->cameraOnlyDirty = false;
    else if (!traversalSkipped)
      this->rememberFrameRoot(planCacheAllowed ? cacheRoot : NULL);
  }
  overlayScope.committed = true;
  this->setDiagnostic(CoinRenderDiagnosticShell::success());
}

SoCallbackAction::Response
CoinRenderActionP::textureUnitsPreCB(void * userdata, SoCallbackAction * action, const SoNode *)
{
  // PRUNE skips this shape only; subsequent state nodes still traverse.
  if (CoinRenderFramePlanBuilder::isShapeInvisible(action)) return SoCallbackAction::PRUNE;
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  SoState * state = action->getState();
  int last = -1;
  const SbBool * enabled = SoMultiTextureEnabledElement::getEnabledUnits(state, last);
  if (last >= static_cast<int>(COIN_RENDER_MAX_TEXTURE_UNITS)) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString("At most eight texture units are supported")));
    return SoCallbackAction::ABORT;
  }
  const auto * coords = SoMultiTextureCoordinateElement::getInstance(state);
  for (int unit = 1; unit <= last; ++unit) {
    if (enabled[unit] && coords->getType(unit) != SoMultiTextureCoordinateElement::EXPLICIT) {
      // Coin callbacks have only one primary UV/function. Reject before the
      // texture bundle attempts to call an absent unit-zero function.
      p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString("Additional texture units require explicit coordinates")));
      return SoCallbackAction::ABORT;
    }
  }
  return SoCallbackAction::CONTINUE;
}


SoCallbackAction::Response
CoinRenderActionP::textureCombinePreCB(void * userdata, SoCallbackAction* action, const SoNode* node)
{
  const int unit = SoTextureUnitElement::get(action->getState());
  if (unit < 0 || unit >= static_cast<int>(COIN_RENDER_MAX_TEXTURE_UNITS)) {
    auto* p = static_cast<CoinRenderActionP*>(userdata);
    p->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("At most eight texture units are supported")));
    return SoCallbackAction::ABORT;
  }
  // Coin's generic callback implementation does not update this element.
  // Use the node's own field/override interpretation with our enabled state.
  const_cast<SoTextureCombine*>(static_cast<const SoTextureCombine*>(node))->doAction(action);
  return SoCallbackAction::CONTINUE;
}

void
CoinRenderActionP::initCallbacks()
{
  this->master->addTriangleCallback(SoShape::getClassTypeId(), triangleCB, this);
  this->master->addLineSegmentCallback(SoShape::getClassTypeId(), lineCB, this);
  this->master->addPointCallback(SoShape::getClassTypeId(), pointCB, this);

  this->master->addPreCallback(SoShape::getClassTypeId(), textureUnitsPreCB, this);
  this->master->addPreCallback(SoLight::getClassTypeId(), lightPreCB, this);
  this->master->addPreCallback(SoTextureCombine::getClassTypeId(), textureCombinePreCB, this);
  this->master->addPreCallback(SoDepthBuffer::getClassTypeId(), depthBufferPreCB, this);
  this->master->addPreCallback(SoAnnotation::getClassTypeId(), annotationPreCB, this);
  this->master->addPostCallback(SoAnnotation::getClassTypeId(), annotationPostCB, this);
  this->master->addPreCallback(SoSceneTexture2::getClassTypeId(), sceneTexturePreCB, this);
  this->master->addPreCallback(SoIndexedFaceSet::getClassTypeId(), indexedFaceSetPreCB, this);
  this->master->addPreCallback(SoIndexedLineSet::getClassTypeId(), indexedLineSetPreCB, this);
}

void
CoinRenderActionP::triangleCB(void * userdata,
                               SoCallbackAction * action,
                               const SoPrimitiveVertex * v0,
                               const SoPrimitiveVertex * v1,
                               const SoPrimitiveVertex * v2)
{
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  p->builder.addTriangle(action, v0, v1, v2);
}

void
CoinRenderActionP::lineCB(void * userdata,
                           SoCallbackAction * action,
                           const SoPrimitiveVertex * v0,
                           const SoPrimitiveVertex * v1)
{
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  p->builder.addLine(action, v0, v1);
}

void
CoinRenderActionP::pointCB(void * userdata,
                            SoCallbackAction * action,
                            const SoPrimitiveVertex * vertex)
{
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  p->builder.addPoint(action, vertex);
}

SoCallbackAction::Response
CoinRenderActionP::sceneTexturePreCB(void * userdata,
                                       SoCallbackAction * action,
                                       const SoNode * node)
{
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  const SoSceneTexture2 * texture = static_cast<const SoSceneTexture2 *>(node);
  SoState * state = action ? action->getState() : NULL;
  if (!state) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::BACKEND_ERROR, CoinRenderDiagnosticDomain::ACTION,
      SbString("SoSceneTexture2 has no traversal state")));
    return SoCallbackAction::ABORT;
  }
  if (SoTextureOverrideElement::getImageOverride(state)) {
    return SoCallbackAction::CONTINUE;
  }
  if (SoTextureUnitElement::get(state) != 0 ||
      texture->type.getValue() != SoSceneTexture2::RGBA8 ||
      texture->model.getValue() != SoSceneTexture2::MODULATE ||
      (texture->wrapS.getValue() != SoSceneTexture2::REPEAT &&
       texture->wrapS.getValue() != SoSceneTexture2::CLAMP) ||
      (texture->wrapT.getValue() != SoSceneTexture2::REPEAT &&
       texture->wrapT.getValue() != SoSceneTexture2::CLAMP) ||
      texture->transparencyFunction.getValue() != SoSceneTexture2::NONE ||
      texture->sceneTransparencyType.getValue() != NULL) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString("SoSceneTexture2 supports only unit 0, RGBA8, MODULATE, REPEAT/CLAMP, NONE transparency function and no sceneTransparencyType")));
    return SoCallbackAction::ABORT;
  }

  if (SoTextureQualityElement::get(state) <= 0.0f) {
    SoMultiTextureImageElement::setDefault(state, const_cast<SoSceneTexture2 *>(texture), 0);
    SoMultiTextureEnabledElement::set(state, const_cast<SoSceneTexture2 *>(texture), 0, FALSE);
    return SoCallbackAction::CONTINUE;
  }

  bool useDirect = false;
#if !defined(HAVE_COIN_WGPU_RUST_BRIDGE) && !defined(HAVE_COIN_BGFX)
  if (p->executionOptions.sceneTexture == COIN_RENDER_SCENE_TEXTURE_DIRECT) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString("Direct scene texture is not implemented; no fallback was applied")));
    return SoCallbackAction::ABORT;
  }
#endif
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE) || defined(HAVE_COIN_BGFX)
  useDirect =
      p->directPasses && p->executionOptions.sceneTexture == COIN_RENDER_SCENE_TEXTURE_DIRECT;
#endif

  const SbVec2s size = texture->size.getValue();
  SoNode * scene = texture->scene.getValue();
  if (!scene || size[0] <= 0 || size[1] <= 0 ||
      size[0] > 2048 || size[1] > 2048) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString("SoSceneTexture2 requires a scene and dimensions in 1..2048")));
    return SoCallbackAction::ABORT;
  }

  // Direct RTT retains RGBA8 color and depth32 attachments simultaneously.
  const size_t chargedBytes = size_t(size[0]) * size_t(size[1]) * (useDirect ? 8 : 4);
  const size_t maxBudgetBytes = size_t(64) * 1024 * 1024;
  if (!useDirect && chargedBytes > maxBudgetBytes - *p->sceneTextureStagedBytes) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::TARGET,
      SbString(useDirect
        ? "SoSceneTexture2 GPU attachment budget exceeds 64 MiB per apply"
        : "SoSceneTexture2 staged RGBA8 budget exceeds 64 MiB per apply")));
    return SoCallbackAction::ABORT;
  }

  static thread_local std::vector<const SoSceneTexture2 *> activeTextures;
  if (activeTextures.size() >= 8 ||
      std::find(activeTextures.begin(), activeTextures.end(), texture) != activeTextures.end()) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString("SoSceneTexture2 dependency cycle or nesting beyond eight passes")));
    return SoCallbackAction::ABORT;
  }
  activeTextures.push_back(texture);
  struct ActiveTextureGuard {
    std::vector<const SoSceneTexture2 *> & stack;
    ~ActiveTextureGuard() { stack.pop_back(); }
  } guard{activeTextures};

  if (!useDirect) *p->sceneTextureStagedBytes += chargedBytes;
  const SbVec4f background = texture->backgroundColor.getValue();
#if defined(HAVE_COIN_BGFX)
  // The evaluation backend owns a process-wide BGFX singleton. Release a
  // previously prepared parent before executing staged child passes; the
  // parent will be prepared again when its completed frame is submitted.
  if (!useDirect && p->target != NULL &&
      dynamic_cast<CoinBgfxBackend *>(p->target->getPimpl()->backend.get()) != NULL) {
    p->target->getPimpl()->backend.reset();
  }
#endif
  std::unique_ptr<CoinRenderTarget> childTarget;
  if (!useDirect) {
    childTarget.reset(
        CoinRenderTarget::createOffscreen(SbVec2i32(size[0], size[1]), p->executionOptions));
    if (!childTarget || childTarget->getStatus() != CoinRenderTarget::TARGET_READY) {
      p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::BACKEND_ERROR, CoinRenderDiagnosticDomain::TARGET,
        SbString("Cannot create SoSceneTexture2 offscreen target")));
      return SoCallbackAction::ABORT;
    }
    // The staged SoSceneTexture2 path consumes RGBA8 only. Keeping the
    // default CPU depth output enabled would reject this otherwise valid
    // color-only pass on backends such as BGFX that do not publish depth.
    if (!childTarget->setDepthReadbackEnabled(FALSE)) {
      p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::BACKEND_ERROR, CoinRenderDiagnosticDomain::TARGET,
        SbString("Cannot configure SoSceneTexture2 color-only offscreen target")));
      return SoCallbackAction::ABORT;
    }
  }
  CoinRenderAction childAction(SbViewportRegion(size[0], size[1]));
  childAction.pimpl->sceneTextureDirectTokens = p->sceneTextureDirectTokens;
  childAction.pimpl->directPasses = p->directPasses;
  childAction.pimpl->planOnly = useDirect;
  childAction.pimpl->executionOptions = p->executionOptions;
  childAction.setRenderTarget(childTarget.get());
  childAction.setTransparencyType(p->transparencyType);
  childAction.setSortedLayersNumPasses(static_cast<int>(p->transparencyOptions.layers));
  childAction.setTransparencyBufferBudget(p->transparencyOptions.bufferBudget);
  childAction.setBackgroundColor(SbColor4f(background[0], background[1],
                                           background[2], background[3]));
  childAction.pimpl->sceneTextureStagedBytes = p->sceneTextureStagedBytes;
  childAction.apply(scene);
  if (childAction.getLastStatus() != CoinRenderAction::SUCCESS) {
    p->setDiagnostic(CoinRenderDiagnosticShell::withContext(
      childAction.getLastStatus(), childAction.pimpl->lastDiagnosticDomain,
      "SoSceneTexture2 subscene", childAction.getLastError()));
    return SoCallbackAction::ABORT;
  }

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE) || defined(HAVE_COIN_BGFX)
  if (useDirect) {
    if (!p->directPasses || !childAction.pimpl->hasLastValidPlan) {
      p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::BACKEND_ERROR, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString("SoSceneTexture2 direct pass was not planned")));
      return SoCallbackAction::ABORT;
    }
    const SbVec2i32 passSize(size[0], size[1]);
    const CoinRenderFramePlan & snapshot = childAction.pimpl->lastValidPlan;
    uint64_t token = 0;
    for (size_t i = 0; i < p->directPasses->size(); ++i) {
      const CoinRenderActionP::DirectPass & existing = (*p->directPasses)[i];
      if (existing.size == passSize && existing.plan.hasSamePayload(snapshot)) {
        token = i + 1;
        break;
      }
    }
    if (!token) {
      if (chargedBytes > maxBudgetBytes - *p->sceneTextureStagedBytes) {
        p->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::TARGET,
          SbString("SoSceneTexture2 GPU attachment budget exceeds 64 MiB per apply")));
        return SoCallbackAction::ABORT;
      }
      *p->sceneTextureStagedBytes += chargedBytes;
      CoinRenderActionP::DirectPass pass;
      pass.plan = std::move(childAction.pimpl->lastValidPlan);
      pass.size = passSize;
      pass.producerKey = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(texture));
      p->directPasses->push_back(std::move(pass));
      token = p->directPasses->size();
    }
    p->sceneTexturePixels.emplace_back(4, 0);
    std::vector<uint8_t> & marker = p->sceneTexturePixels.back();
    for (unsigned int i = 0; i < 4; ++i) marker[i] = static_cast<uint8_t>(token >> (i * 8));
    SoMultiTextureImageElement::set(state, const_cast<SoSceneTexture2 *>(texture), 0,
      SbVec2s(1, 1), 4, marker.data(),
      static_cast<SoMultiTextureImageElement::Wrap>(texture->wrapS.getValue()),
      static_cast<SoMultiTextureImageElement::Wrap>(texture->wrapT.getValue()),
      SoMultiTextureImageElement::MODULATE, texture->blendColor.getValue());
    SbVec2s markerSize;
    int markerComponents = 0;
    SoMultiTextureImageElement::Wrap ws, wt;
    SoMultiTextureImageElement::Model model;
    SbColor blend;
    const unsigned char * image = SoMultiTextureImageElement::get(
      state, 0, markerSize, markerComponents, ws, wt, model, blend);
    if (!image || markerSize != SbVec2s(1, 1) || markerComponents != 4) {
      p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::BACKEND_ERROR, CoinRenderDiagnosticDomain::ACTION,
        SbString("SoSceneTexture2 direct GPU marker was not retained by traversal state")));
      return SoCallbackAction::ABORT;
    }
    p->builder.registerDirectTexture(image, token,
      static_cast<uint32_t>(size[0]), static_cast<uint32_t>(size[1]), background[3] >= 1.0f);
    SoMultiTextureEnabledElement::set(state, const_cast<SoSceneTexture2 *>(texture), 0, TRUE);
    return SoCallbackAction::CONTINUE;
  }
#endif
  std::vector<uint8_t> pixels;
  childTarget->readbackRGBA(pixels);
  const size_t required = size_t(size[0]) * size_t(size[1]) * 4;
  if (pixels.size() != required) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::BACKEND_ERROR, CoinRenderDiagnosticDomain::READBACK,
      SbString("SoSceneTexture2 subscene returned incomplete RGBA8 readback")));
    return SoCallbackAction::ABORT;
  }
  if (!CoinRenderImageCore::flipRgba8Rows(
        pixels, SbVec2i32(static_cast<int32_t>(size[0]),
                         static_cast<int32_t>(size[1])))) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::BACKEND_ERROR, CoinRenderDiagnosticDomain::READBACK,
      SbString("SoSceneTexture2 subscene returned invalid RGBA8 dimensions")));
    return SoCallbackAction::ABORT;
  }
  p->sceneTexturePixels.push_back(std::move(pixels));
  SoMultiTextureImageElement::set(state, const_cast<SoSceneTexture2 *>(texture), 0,
    size, 4, p->sceneTexturePixels.back().data(),
    static_cast<SoMultiTextureImageElement::Wrap>(texture->wrapS.getValue()),
    static_cast<SoMultiTextureImageElement::Wrap>(texture->wrapT.getValue()),
    SoMultiTextureImageElement::MODULATE, texture->blendColor.getValue());
  SoMultiTextureEnabledElement::set(state, const_cast<SoSceneTexture2 *>(texture), 0, TRUE);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::depthBufferPreCB(void *, SoCallbackAction * action, const SoNode * node)
{
  const SoDepthBuffer * depth = static_cast<const SoDepthBuffer *>(node);
  SoState * state = action->getState();
  SbBool test = depth->test.isIgnored()
    ? SoDepthBufferElement::getTestEnable(state) : depth->test.getValue();
  SbBool write = depth->write.isIgnored()
    ? SoDepthBufferElement::getWriteEnable(state) : depth->write.getValue();
  SoDepthBufferElement::DepthWriteFunction function = depth->function.isIgnored()
    ? SoDepthBufferElement::getFunction(state)
    : static_cast<SoDepthBufferElement::DepthWriteFunction>(depth->function.getValue());
  const SbVec2f range = depth->range.isIgnored()
    ? SoDepthBufferElement::getRange(state) : depth->range.getValue();
  SoDepthBufferElement::set(state, test, write, function, range);
  CoinRenderDepthPolicyElement::add(state, (depth->test.isIgnored() ? 0 : 1) |
    (depth->write.isIgnored() ? 0 : 2) | (depth->function.isIgnored() ? 0 : 4) |
    (depth->range.isIgnored() ? 0 : 8));
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::annotationPreCB(void * userdata, SoCallbackAction *, const SoNode *)
{
  static_cast<CoinRenderActionP *>(userdata)->builder.beginAnnotation();
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::annotationPostCB(void * userdata, SoCallbackAction *, const SoNode *)
{
  static_cast<CoinRenderActionP *>(userdata)->builder.endAnnotation();
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::lightPreCB(void * userdata,
                               SoCallbackAction * action,
                               const SoNode * /*node*/)
{
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  p->builder.recordLightAttenuation(action);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::indexedFaceSetPreCB(void * userdata,
                                        SoCallbackAction * action,
                                        const SoNode * node)
{
  // PRUNE skips this shape only; subsequent state nodes still traverse.
  if (CoinRenderFramePlanBuilder::isShapeInvisible(action)) return SoCallbackAction::PRUNE;
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  if (!p->fastPathEnabled) {
    return SoCallbackAction::CONTINUE;
  }

  // Preserve the original polygon details used by the common style resolver.
  if (CoinRenderFramePlanBuilder::polygonDrawStyle(action) != SoDrawStyle::FILLED)
    return SoCallbackAction::CONTINUE;

  // The direct path bypasses the node virtual callback implementation.
  // Subclasses may prepare traversal state there, so only prune the exact type.
  if (node->getTypeId() != SoIndexedFaceSet::getClassTypeId()) {
    return SoCallbackAction::CONTINUE;
  }

  const SoIndexedFaceSet * ifs = dynamic_cast<const SoIndexedFaceSet *>(node);
  if (!ifs) {
    return SoCallbackAction::CONTINUE;
  }

  SoState * state = action->getState();
  if (!state) {
    return SoCallbackAction::CONTINUE;
  }

  const SoVertexProperty * vp = static_cast<const SoVertexProperty *>(ifs->vertexProperty.getValue());
  if (vp) {
    state->push();
    const_cast<SoVertexProperty *>(vp)->doAction(action);
  }

  int lastTextureUnit = -1;
  SoMultiTextureEnabledElement::getEnabledUnits(state, lastTextureUnit);
  if (lastTextureUnit > 0) {
    if (vp) state->pop();
    return SoCallbackAction::CONTINUE;
  }

  const SoCoordinateElement * coords = SoCoordinateElement::getInstance(state);
  if (!coords || !coords->is3D()) {
    if (vp) state->pop();
    return SoCallbackAction::CONTINUE;
  }

  const SbVec3f * coordArray = coords->getArrayPtr3();
  int32_t numCoords = coords->getNum();
  if (!coordArray || numCoords <= 0) {
    if (vp) state->pop();
    return SoCallbackAction::CONTINUE;
  }

  SoTextureCoordinateBundle tb(action, FALSE, FALSE);
  if (tb.needCoordinates()) {
    const SoMultiTextureCoordinateElement * tcElem = SoMultiTextureCoordinateElement::getInstance(state);
    if (tcElem) {
      auto ct = tcElem->getType(0);
      if (ct == SoMultiTextureCoordinateElement::DEFAULT || ct == SoMultiTextureCoordinateElement::FUNCTION) {
        if (vp) state->pop();
        p->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("Procedural/DEFAULT texture coordinates are not supported in Subwave 3B")));
        return SoCallbackAction::ABORT;
      }
    }
  }

  CoinRenderDirectGeometryView view;
  view.positions = CoinRenderSpan<SbVec3f>(coordArray, static_cast<size_t>(numCoords));

  const SoNormalElement * normElem = SoNormalElement::getInstance(state);
  if (normElem && normElem->getNum() > 0) {
    view.normals = CoinRenderSpan<SbVec3f>(normElem->getArrayPtr(), static_cast<size_t>(normElem->getNum()));
  }

  if (ifs->coordIndex.getNum() > 0) {
    view.coordIndex = CoinRenderSpan<int32_t>(ifs->coordIndex.getValues(0), static_cast<size_t>(ifs->coordIndex.getNum()));
  }
  if (ifs->normalIndex.getNum() > 0 && !(ifs->normalIndex.getNum() == 1 && ifs->normalIndex[0] == -1)) {
    view.normalIndex = CoinRenderSpan<int32_t>(ifs->normalIndex.getValues(0), static_cast<size_t>(ifs->normalIndex.getNum()));
  }
  if (ifs->materialIndex.getNum() > 0 && !(ifs->materialIndex.getNum() == 1 && ifs->materialIndex[0] == -1)) {
    view.materialIndex = CoinRenderSpan<int32_t>(ifs->materialIndex.getValues(0), static_cast<size_t>(ifs->materialIndex.getNum()));
  }
  if (ifs->textureCoordIndex.getNum() > 0 && !(ifs->textureCoordIndex.getNum() == 1 && ifs->textureCoordIndex[0] == -1)) {
    view.texCoordIndex = CoinRenderSpan<int32_t>(ifs->textureCoordIndex.getValues(0), static_cast<size_t>(ifs->textureCoordIndex.getNum()));
  }

  const SoMultiTextureCoordinateElement * tcElem = SoMultiTextureCoordinateElement::getInstance(state);
  if (tcElem) {
    SoMultiTextureCoordinateElement::CoordType ct = tcElem->getType(0);
    if (ct == SoMultiTextureCoordinateElement::EXPLICIT) {
      int32_t numTc = tcElem->getNum(0);
      if (numTc > 0) {
        const SbVec2f * tcPtr = tcElem->getArrayPtr2(0);
        if (tcPtr) {
          view.texcoords = CoinRenderSpan<SbVec2f>(tcPtr, static_cast<size_t>(numTc));
        }
      }
    }
  }

  view.materialBinding = SoMaterialBindingElement::get(state);
  view.normalBinding = SoNormalBindingElement::get(state);

  std::string err;
  CoinRenderFastPathResult res = p->builder.processIndexedFaceSet(action, view, const_cast<SoNode *>(node), &err);

  if (vp) {
    state->pop();
  }

  if (res == CoinRenderFastPathResult::SUCCESS_PRUNE) {
    return SoCallbackAction::PRUNE;
  } else if (res == CoinRenderFastPathResult::INVALID_SCENE) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString(err.empty() ? "Invalid scene in IndexedFaceSet" : err.c_str())));
    return SoCallbackAction::ABORT;
  } else if (res == CoinRenderFastPathResult::UNSUPPORTED) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString(err.empty() ? "Unsupported feature in IndexedFaceSet" : err.c_str())));
    return SoCallbackAction::ABORT;
  } else {
    return SoCallbackAction::CONTINUE;
  }
}

SoCallbackAction::Response
CoinRenderActionP::indexedLineSetPreCB(void * userdata,
                                        SoCallbackAction * action,
                                        const SoNode * node)
{
  // PRUNE skips this shape only; subsequent state nodes still traverse.
  if (CoinRenderFramePlanBuilder::isShapeInvisible(action)) return SoCallbackAction::PRUNE;
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  if (!p->fastPathEnabled) {
    return SoCallbackAction::CONTINUE;
  }

  // Preserve callback semantics for subclasses; see indexedFaceSetPreCB().
  if (node->getTypeId() != SoIndexedLineSet::getClassTypeId()) {
    return SoCallbackAction::CONTINUE;
  }

  const SoIndexedLineSet * ils = dynamic_cast<const SoIndexedLineSet *>(node);
  if (!ils) {
    return SoCallbackAction::CONTINUE;
  }

  SoState * state = action->getState();
  if (!state) {
    return SoCallbackAction::CONTINUE;
  }

  const SoVertexProperty * vp = static_cast<const SoVertexProperty *>(ils->vertexProperty.getValue());
  if (vp) {
    state->push();
    const_cast<SoVertexProperty *>(vp)->doAction(action);
  }

  const SoCoordinateElement * coords = SoCoordinateElement::getInstance(state);
  if (!coords || !coords->is3D()) {
    if (vp) state->pop();
    return SoCallbackAction::CONTINUE;
  }

  const SbVec3f * coordArray = coords->getArrayPtr3();
  int32_t numCoords = coords->getNum();
  if (!coordArray || numCoords <= 0) {
    if (vp) state->pop();
    return SoCallbackAction::CONTINUE;
  }

  CoinRenderDirectGeometryView view;
  view.positions = CoinRenderSpan<SbVec3f>(coordArray, static_cast<size_t>(numCoords));

  if (ils->coordIndex.getNum() > 0) {
    view.coordIndex = CoinRenderSpan<int32_t>(ils->coordIndex.getValues(0), static_cast<size_t>(ils->coordIndex.getNum()));
  }
  if (ils->materialIndex.getNum() > 0 && !(ils->materialIndex.getNum() == 1 && ils->materialIndex[0] == -1)) {
    view.materialIndex = CoinRenderSpan<int32_t>(ils->materialIndex.getValues(0), static_cast<size_t>(ils->materialIndex.getNum()));
  }

  view.materialBinding = SoMaterialBindingElement::get(state);

  std::string err;
  CoinRenderFastPathResult res = p->builder.processIndexedLineSet(action, view, const_cast<SoNode *>(node), &err);

  if (vp) {
    state->pop();
  }

  if (res == CoinRenderFastPathResult::SUCCESS_PRUNE) {
    return SoCallbackAction::PRUNE;
  } else if (res == CoinRenderFastPathResult::INVALID_SCENE) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString(err.empty() ? "Invalid scene in IndexedLineSet" : err.c_str())));
    return SoCallbackAction::ABORT;
  } else if (res == CoinRenderFastPathResult::UNSUPPORTED) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString(err.empty() ? "Unsupported feature in IndexedLineSet" : err.c_str())));
    return SoCallbackAction::ABORT;
  } else {
    return SoCallbackAction::CONTINUE;
  }
}
