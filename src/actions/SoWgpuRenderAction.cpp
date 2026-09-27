#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/actions/SoSubAction.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoLight.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoSceneTexture2.h>
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
#include <algorithm>
#include <chrono>
#include <memory>
#include <cstdlib>
#include <iostream>
#include <Inventor/misc/SoState.h>

#include "actions/SoWgpuRenderActionP.h"
#include "rendering/wgpu/SoWgpuDiagnosticShell.h"
#include "rendering/wgpu/SoWgpuFrameReuseCore.h"
#include "rendering/wgpu/SoWgpuImageCore.h"
#include "rendering/wgpu/SoWgpuRenderTargetP.h"
#include "actions/SoSubActionP.h"
#if defined(HAVE_WGPU_RUST_BRIDGE)
#include "rendering/wgpu/SoWgpuRustBackend.h"
#include "rendering/wgpu/coin_wgpu_ffi.h"
#elif defined(HAVE_WGPU_BGFX)
#include "rendering/wgpu/SoWgpuBgfxBackend.h"
#elif defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE)
#include "rendering/wgpu/SoWgpuNativeBackend.h"
#endif

SO_ACTION_SOURCE(SoWgpuRenderAction);

void
SoWgpuRenderAction::initClass(void)
{
  SO_ACTION_INTERNAL_INIT_CLASS(SoWgpuRenderAction, SoCallbackAction);
  SO_ENABLE(SoWgpuRenderAction, SoDepthBufferElement);
}

SbBool
SoWgpuRenderAction::isGpuBackendAvailable(void)
{
#if defined(HAVE_WGPU_RUST_BRIDGE)
  return SoWgpuRustBackend::isAvailable() ? TRUE : FALSE;
#elif defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE)
  return SoWgpuNativeBackend::isAvailable() ? TRUE : FALSE;
#else
  return FALSE;
#endif
}

SoWgpuRenderAction::SoWgpuRenderAction(void)
{
  this->pimpl->master = this;
  SO_ACTION_CONSTRUCTOR(SoWgpuRenderAction);
  this->pimpl->viewport = SbViewportRegion(640, 512);
  this->pimpl->initCallbacks();
}

SoWgpuRenderAction::SoWgpuRenderAction(const SbViewportRegion & viewport)
  : inherited(viewport)
{
  this->pimpl->master = this;
  SO_ACTION_CONSTRUCTOR(SoWgpuRenderAction);
  this->pimpl->viewport = viewport;
  this->setViewportRegion(viewport);
  this->pimpl->initCallbacks();
}

SoWgpuRenderAction::~SoWgpuRenderAction(void)
{
}

void
SoWgpuRenderAction::setViewportRegion(const SbViewportRegion & region)
{
  inherited::setViewportRegion(region);
  this->pimpl->viewport = region;
  this->pimpl->cachedRoot = NULL;
}

const SbViewportRegion &
SoWgpuRenderAction::getViewportRegion(void) const
{
  return this->pimpl->viewport;
}

void
SoWgpuRenderAction::setRenderTarget(SoWgpuRenderTarget * target)
{
#if defined(HAVE_WGPU_BGFX)
  // The evaluation backend owns one process-wide BGFX instance. Switching
  // targets must release the old instance so the new target can prepare it.
  if (this->pimpl->target != target && this->pimpl->target != NULL &&
      dynamic_cast<SoWgpuBgfxBackend *>(
        this->pimpl->target->getPimpl()->backend.get()) != NULL) {
    this->pimpl->target->getPimpl()->backend.reset();
  }
#endif
  this->pimpl->target = target;
  this->pimpl->cachedRoot = NULL;
}

SoWgpuRenderTarget *
SoWgpuRenderAction::getRenderTarget(void) const
{
  return this->pimpl->target;
}

void
SoWgpuRenderAction::setBackgroundColor(const SbColor4f & color)
{
  this->pimpl->backgroundColor = color;
  this->pimpl->cachedRoot = NULL;
}

const SbColor4f &
SoWgpuRenderAction::getBackgroundColor(void) const
{
  return this->pimpl->backgroundColor;
}

void
SoWgpuRenderAction::setTransparencyType(TransparencyType type)
{
  if (this->pimpl->transparencyType != type) {
    this->pimpl->transparencyType = type;
    this->pimpl->cachedRoot = NULL;
  }
}

SoWgpuRenderAction::TransparencyType
SoWgpuRenderAction::getTransparencyType(void) const
{
  return this->pimpl->transparencyType;
}

void
SoWgpuRenderAction::setFastPathEnabled(SbBool enable)
{
  this->pimpl->fastPathEnabled = (enable != FALSE);
  this->pimpl->cachedRoot = NULL;
}

SbBool
SoWgpuRenderAction::isFastPathEnabled(void) const
{
  return this->pimpl->fastPathEnabled ? TRUE : FALSE;
}

SoWgpuRenderAction::Status
SoWgpuRenderAction::getLastStatus(void) const
{
  return this->pimpl->lastStatus;
}

const SbString &
SoWgpuRenderAction::getLastError(void) const
{
  return this->pimpl->lastError;
}

const SbString &
SoWgpuRenderAction::getRecordingLog(void) const
{
  if (this->pimpl->hasLastValidPlan && !this->pimpl->recordingLogValid) {
    this->pimpl->lastRecordingLog =
      this->pimpl->recordingBackend.recordToString(this->pimpl->lastValidPlan).c_str();
    this->pimpl->recordingLogValid = true;
  }
  return this->pimpl->lastRecordingLog;
}

void
SoWgpuRenderAction::beginForegroundPass()
{
  if (this->pimpl->isApplying && !this->pimpl->replayingAnnotations)
    this->pimpl->builder.beginForeground();
}

void
SoWgpuRenderAction::endForegroundPass()
{
  if (this->pimpl->isApplying && !this->pimpl->replayingAnnotations)
    this->pimpl->builder.endForeground();
}

SbBool
SoWgpuRenderAction::deferOverlayPath(SoPath * supplied, int priority)
{
  if (!this->pimpl->isApplying || this->pimpl->replayingAnnotations) return FALSE;
  SoPath * path = (supplied ? supplied : this->getCurPath())->copy();
  path->ref();
  try { this->pimpl->delayedOverlays.push_back({path, priority}); }
  catch (...) { path->unref(); throw; }
  return TRUE;
}

SbBool
SoWgpuRenderAction::deferAnnotation(int priority)
{
  if (!this->pimpl->isApplying || this->pimpl->replayingAnnotations) return FALSE;
  SoPath * path = this->getCurPath()->copy();
  path->ref();
  try { this->pimpl->delayedAnnotations.push_back({path, priority}); }
  catch (...) { path->unref(); throw; }
  return TRUE;
}

void
SoWgpuRenderAction::apply(SoNode * root)
{
  this->pimpl->executeApply([&]() {
    if (root) {
      this->inherited::apply(root);
    }
  }, root);
}

void
SoWgpuRenderAction::applyAsync(SoNode * root, SoWgpuReadbackTicket & outTicket)
{
  outTicket = SoWgpuReadbackTicket{};
  if (this->pimpl->isApplying) {
    this->pimpl->setDiagnostic(SoWgpuDiagnosticShell::action(
      INVALID_SCENE, SoWgpuDiagnosticDomain::ACTION,
      SbString("Nested applyAsync() calls are not permitted")));
    this->pimpl->hasReentrancyError = true;
    return;
  }
  this->pimpl->asyncTicket = &outTicket;
  this->apply(root);
  this->pimpl->asyncTicket = NULL;
}

void
SoWgpuRenderAction::apply(SoPath * path)
{
  this->pimpl->executeApply([&]() {
    if (path) {
      this->inherited::apply(path);
    }
  });
}

void
SoWgpuRenderAction::apply(const SoPathList & pathlist, SbBool obeysrules)
{
  this->pimpl->executeApply([&]() {
    this->inherited::apply(pathlist, obeysrules);
  });
}

void
SoWgpuRenderAction::beginTraversal(SoNode * root)
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

// SoWgpuRenderActionP implementation

SoWgpuRenderActionP::SoWgpuRenderActionP(SoWgpuRenderAction * m)
  : master(m),
    target(NULL),
    asyncTicket(NULL),
    backgroundColor(0.0f, 0.0f, 0.0f, 1.0f),
    transparencyType(SoWgpuRenderAction::SCREEN_DOOR),
    lastStatus(SoWgpuRenderAction::SUCCESS),
    lastDiagnosticDomain(SoWgpuDiagnosticDomain::NONE),
    hasLastValidPlan(false),
    recordingLogValid(false),
    isApplying(false),
    hasReentrancyError(false),
    fastPathEnabled(true),
    cameraSensor(SoWgpuRenderActionP::cameraSensorCB, this)
{
  this->cameraSensor.setPriority(0);
}

SoWgpuRenderActionP::~SoWgpuRenderActionP()
{
}

void
SoWgpuRenderActionP::setDiagnostic(const SoWgpuActionDiagnostic & diagnostic)
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
SoWgpuRenderActionP::cameraSensorCB(void * data, SoSensor * sensor)
{
  SoWgpuRenderActionP * self = static_cast<SoWgpuRenderActionP *>(data);
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
SoWgpuRenderActionP::rememberFrameRoot(SoNode * root)
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
SoWgpuRenderActionP::prepareCameraOverlay(SoNode * root,
                                         SoWgpuCameraOverlayUndo & undo)
{
  if (!root || root != this->cachedRoot || !this->cachedCamera ||
      this->cameraSensor.getAttachedNode() != root ||
      !this->cameraOnlyDirty || this->cameraPatchInvalidated ||
      static_cast<SoGroup *>(root)->getChild(0) != this->cachedCamera) return false;

  SbViewportRegion adjusted;
  const SbViewVolume vv = this->cachedCamera->getViewVolume(
    this->master->getViewportRegion(), adjusted, SbMatrix::identity());
  if (adjusted != this->master->getViewportRegion()) return false;
  CameraSnapshot snapshot;
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
  return SoWgpuFrameReuseCore::beginCameraOverlay(
    this->lastValidPlan, snapshot,
    SoWgpuFramePlanBuilder::nextRevision(), undo);
}

template <typename F>
void
SoWgpuRenderActionP::executeApply(F traversalFn, SoNode * cacheRoot)
{
  typedef std::chrono::steady_clock ProfileClock;
  const ProfileClock::time_point profileBegin = ProfileClock::now();
  const bool tracePhases = SoWgpuDiagnosticShell::phaseTracingEnabled();
  if (this->isApplying) {
    this->setDiagnostic(SoWgpuDiagnosticShell::action(
      SoWgpuRenderAction::INVALID_SCENE, SoWgpuDiagnosticDomain::ACTION,
      SbString("Nested apply() calls are not permitted on SoWgpuRenderAction")));
    this->hasReentrancyError = true;
    return;
  }

  struct AnnotationScope {
    SoWgpuRenderActionP * action;
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

#if defined(HAVE_WGPU_RUST_BRIDGE) || defined(HAVE_WGPU_BGFX)
  const bool ownsDirectTokens = !this->sceneTextureDirectTokens;
  if (ownsDirectTokens) {
    this->sceneTextureDirectTokens = std::make_shared<std::vector<uint64_t> >();
  }
  struct DirectTextureScope {
    SoWgpuRenderActionP * action;
    bool owns;
    ~DirectTextureScope() {
      if (!owns) return;
      for (uint64_t token : *action->sceneTextureDirectTokens) {
#if defined(HAVE_WGPU_RUST_BRIDGE)
        coin_wgpu_release_texture(token);
#endif
      }
#if defined(HAVE_WGPU_BGFX)
      if (action->target) {
        SoWgpuBgfxBackend * backend = dynamic_cast<SoWgpuBgfxBackend *>(
          action->target->getPimpl()->backend.get());
        if (backend) backend->finishDirectTextures(*action->sceneTextureDirectTokens);
      }
#endif
      action->sceneTextureDirectTokens.reset();
    }
  } directTextureScope{this, ownsDirectTokens};

  const char * directMode = std::getenv("COIN_WGPU_RTT_GPU_DIRECT");
  const bool ownsDirectPasses = !this->directPasses && !this->planOnly &&
    this->target && this->target->pimpl->kind == SoWgpuRenderTargetP::KIND_OFFSCREEN &&
    directMode && directMode[0] == '1' && directMode[1] == '\0';
  if (ownsDirectPasses) {
    this->directPasses = std::make_shared<std::vector<DirectPass> >();
    planCacheAllowed = false;
  }
  struct DirectPassScope {
    SoWgpuRenderActionP * action;
    bool owns;
    ~DirectPassScope() { if (owns) action->directPasses.reset(); }
  } directPassScope{this, ownsDirectPasses};
#endif
  const bool traversalSkipped = planCacheAllowed && cacheRoot && this->hasLastValidPlan &&
    this->cachedRoot == cacheRoot && this->cachedRootId == cacheRoot->getNodeId() &&
    !this->cameraOnlyDirty && !this->cameraPatchInvalidated;
  SoWgpuFrameReuseDecision reuseDecision = traversalSkipped
    ? SoWgpuFrameReuseDecision(SoWgpuFrameReuseKind::REUSE,
                               this->lastValidPlan.revision)
    : SoWgpuFrameReuseDecision(SoWgpuFrameReuseKind::UNKNOWN, 0);
  this->sceneTexturePixels.clear();
  if (!this->sceneTextureStagedBytes) {
    this->sceneTextureStagedBytes = std::make_shared<size_t>(0);
  }
  FramePlan plan;
  SoWgpuCameraOverlayUndo overlayUndo;
  struct CameraOverlayScope {
    FramePlan & frame;
    SoWgpuCameraOverlayUndo & undo;
    bool committed;
    ~CameraOverlayScope() {
      if (!committed) SoWgpuFrameReuseCore::rollbackCameraOverlay(frame, undo);
    }
  } overlayScope{this->lastValidPlan, overlayUndo, false};
  ProfileClock::time_point profileTraversed = ProfileClock::now();
  ProfileClock::time_point profilePlanned = profileTraversed;
  const bool cameraOverlay = !traversalSkipped && planCacheAllowed &&
    !this->planOnly && this->hasLastValidPlan &&
    this->prepareCameraOverlay(cacheRoot, overlayUndo);
  if (cameraOverlay) {
    reuseDecision = SoWgpuFrameReuseDecision(
      SoWgpuFrameReuseKind::CAMERA_PATCH, overlayUndo.revision);
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
    this->setDiagnostic(SoWgpuDiagnosticShell::action(
      SoWgpuRenderAction::INVALID_SCENE, SoWgpuDiagnosticDomain::ACTION,
      SbString("Nested apply() calls are not permitted on SoWgpuRenderAction")));
    return;
  }

  if (this->master->hasTerminated() && this->lastStatus != SoWgpuRenderAction::SUCCESS) {
    return;
  }

  std::string err;
  if (!traversalSkipped && !cameraOverlay && !this->builder.build(plan, &err)) {
    const SoWgpuRenderAction::Status status = this->builder.isUnsupportedBuild()
      ? SoWgpuRenderAction::UNSUPPORTED : SoWgpuRenderAction::INVALID_SCENE;
    this->setDiagnostic(SoWgpuDiagnosticShell::action(
      status, SoWgpuDiagnosticDomain::FRAME_PLAN, SbString(err.c_str())));
    return;
  }
  profilePlanned = ProfileClock::now();
  if (!traversalSkipped && !cameraOverlay) {
    if (planCacheAllowed && cacheRoot && this->hasLastValidPlan &&
        this->cachedRoot == cacheRoot) {
      reuseDecision = SoWgpuFrameReuseCore::classify(this->lastValidPlan, plan);
    } else if (planCacheAllowed) {
      reuseDecision = SoWgpuFrameReuseDecision(
        SoWgpuFrameReuseKind::FULL_REBUILD, 0);
    }
  }
  const bool reusePreviousPlan =
    reuseDecision.kind == SoWgpuFrameReuseKind::REUSE &&
    this->hasLastValidPlan;
  const bool useCachedPlan = reusePreviousPlan || cameraOverlay;
  const FramePlan & framePlan = useCachedPlan ? this->lastValidPlan : plan;

  if (this->planOnly) {
    if (!useCachedPlan) {
      this->lastValidPlan = std::move(plan);
      this->hasLastValidPlan = true;
    }
    this->setDiagnostic(SoWgpuDiagnosticShell::success());
    return;
  }

  if (this->target == NULL) {
    if (this->asyncTicket) {
      this->setDiagnostic(SoWgpuDiagnosticShell::action(
        SoWgpuRenderAction::NO_TARGET, SoWgpuDiagnosticDomain::TARGET,
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
    this->setDiagnostic(SoWgpuDiagnosticShell::success());
    return;
  }

  // Target provided: validate target status
  if (this->target->getStatus() == SoWgpuRenderTarget::TARGET_ERROR) {
    const char * tgtErr = this->target->getLastError();
    this->setDiagnostic(SoWgpuDiagnosticShell::fromTarget(
      this->target->getStatus(), (tgtErr && tgtErr[0])
        ? tgtErr : "Render target is in fatal TARGET_ERROR state"));
    return;
  }

#if defined(HAVE_WGPU_RUST_BRIDGE) || defined(HAVE_WGPU_BGFX)
  if (this->directPasses) {
    auto setGraphFailure = [this](const FrameExecutionResult & result) {
      this->setDiagnostic(SoWgpuDiagnosticShell::fromBackend(result));
    };
    // Validate every producer and the consumer before the first GPU submit.
    // Child plans are appended in postorder, so only earlier IDs are legal.
    for (size_t i = 0; i < this->directPasses->size(); ++i) {
      const DirectPass & pass = (*this->directPasses)[i];
      for (const TextureImageSnapshot & texture : pass.plan.textures) {
        if (texture.gpuToken > i) {
          this->setDiagnostic(SoWgpuDiagnosticShell::action(
            SoWgpuRenderAction::INVALID_SCENE,
            SoWgpuDiagnosticDomain::FRAME_PLAN,
            SbString("SoSceneTexture2 pass references a missing or future producer")));
          return;
        }
      }
      FrameExecutionResult check = SoWgpuRenderTargetP::validateProfile(pass.plan, pass.size);
      if (check.status != BackendStatus::SUCCESS) {
        setGraphFailure(check);
        return;
      }
    }
    for (const TextureImageSnapshot & texture : plan.textures) {
      if (texture.gpuToken > this->directPasses->size()) {
        this->setDiagnostic(SoWgpuDiagnosticShell::action(
          SoWgpuRenderAction::INVALID_SCENE,
          SoWgpuDiagnosticDomain::FRAME_PLAN,
          SbString("Parent pass references a missing SoSceneTexture2 producer")));
        return;
      }
    }
    FrameExecutionResult rootCheck =
      SoWgpuRenderTargetP::validateProfile(plan, this->target->pimpl->size);
    if (rootCheck.status != BackendStatus::SUCCESS) {
      setGraphFailure(rootCheck);
      return;
    }

    std::vector<uint64_t> resolved(this->directPasses->size() + 1, 0);
    auto resolveTextures = [&resolved](FramePlan & frame) {
      for (TextureImageSnapshot & texture : frame.textures) {
        if (texture.gpuToken != 0) {
          texture.gpuToken = resolved[static_cast<size_t>(texture.gpuToken)];
          texture.contentDigest = texture.gpuToken;
        }
      }
    };
#if defined(HAVE_WGPU_BGFX)
    if (!this->target->pimpl->backend) {
      this->target->pimpl->backend.reset(new SoWgpuBgfxBackend());
      const BackendStatus prepared =
        this->target->pimpl->backend->prepare(this->target->getPimpl().get());
      if (prepared != BackendStatus::SUCCESS) {
        setGraphFailure(SubmitResult(prepared,
          this->target->pimpl->backend->getLastError()));
        return;
      }
    }
    SoWgpuBgfxBackend * directBgfx = dynamic_cast<SoWgpuBgfxBackend *>(
      this->target->pimpl->backend.get());
    if (!directBgfx) {
      setGraphFailure(SubmitResult(BackendStatus::UNSUPPORTED,
        "SoSceneTexture2 direct graph requires the BGFX backend"));
      return;
    }
#endif
    for (size_t i = 0; i < this->directPasses->size(); ++i) {
      const DirectPass & pass = (*this->directPasses)[i];
      FramePlan childFrame = pass.plan;
      resolveTextures(childFrame);
#if defined(HAVE_WGPU_RUST_BRIDGE)
      std::unique_ptr<SoWgpuRenderTarget> childTarget(
        SoWgpuRenderTargetP::createDirectOffscreen(pass.size));
      if (!childTarget || childTarget->getStatus() != SoWgpuRenderTarget::TARGET_READY) {
        this->setDiagnostic(SoWgpuDiagnosticShell::action(
          SoWgpuRenderAction::BACKEND_ERROR,
          SoWgpuDiagnosticDomain::TARGET,
          SbString("Cannot create planned SoSceneTexture2 offscreen target")));
        return;
      }
      FrameExecutionResult result = childTarget->pimpl->executeFrame(childFrame);
      if (result.status != BackendStatus::SUCCESS) {
        setGraphFailure(result);
        return;
      }
      const uint64_t token = childTarget->pimpl->directTextureToken;
#else
      uint64_t token = 0;
      FrameExecutionResult result = directBgfx->submitDirectTexture(
        childFrame, pass.size, pass.producerKey, token);
      if (result.status != BackendStatus::SUCCESS) {
        setGraphFailure(result);
        return;
      }
#endif
      if (!token) {
        this->setDiagnostic(SoWgpuDiagnosticShell::action(
          SoWgpuRenderAction::BACKEND_ERROR,
          SoWgpuDiagnosticDomain::BACKEND,
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
  FrameExecutionResult execRes = this->asyncTicket
    ? this->target->pimpl->executeFrameAsync(
        framePlan, *this->asyncTicket, reuseDecision)
    : this->target->pimpl->executeFrame(framePlan, reuseDecision);
  if (execRes.status != BackendStatus::SUCCESS) {
    this->setDiagnostic(SoWgpuDiagnosticShell::fromBackend(execRes));
    return;
  }

  const ProfileClock::time_point profileExecuted = ProfileClock::now();
  if (tracePhases) {
    SoWgpuActionPhaseSample sample;
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
    std::cerr << SoWgpuDiagnosticShell::formatActionPhase(sample) << '\n';
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
  this->setDiagnostic(SoWgpuDiagnosticShell::success());
}

void
SoWgpuRenderActionP::initCallbacks()
{
  this->master->addTriangleCallback(SoShape::getClassTypeId(), triangleCB, this);
  this->master->addLineSegmentCallback(SoShape::getClassTypeId(), lineCB, this);
  this->master->addPointCallback(SoShape::getClassTypeId(), pointCB, this);

  this->master->addPreCallback(SoLight::getClassTypeId(), lightPreCB, this);
  this->master->addPreCallback(SoDepthBuffer::getClassTypeId(), depthBufferPreCB, this);
  this->master->addPreCallback(SoAnnotation::getClassTypeId(), annotationPreCB, this);
  this->master->addPostCallback(SoAnnotation::getClassTypeId(), annotationPostCB, this);
  this->master->addPreCallback(SoSceneTexture2::getClassTypeId(), sceneTexturePreCB, this);
  this->master->addPreCallback(SoIndexedFaceSet::getClassTypeId(), indexedFaceSetPreCB, this);
  this->master->addPreCallback(SoIndexedLineSet::getClassTypeId(), indexedLineSetPreCB, this);
}

void
SoWgpuRenderActionP::triangleCB(void * userdata,
                               SoCallbackAction * action,
                               const SoPrimitiveVertex * v0,
                               const SoPrimitiveVertex * v1,
                               const SoPrimitiveVertex * v2)
{
  SoWgpuRenderActionP * p = static_cast<SoWgpuRenderActionP *>(userdata);
  p->builder.addTriangle(action, v0, v1, v2);
}

void
SoWgpuRenderActionP::lineCB(void * userdata,
                           SoCallbackAction * action,
                           const SoPrimitiveVertex * v0,
                           const SoPrimitiveVertex * v1)
{
  SoWgpuRenderActionP * p = static_cast<SoWgpuRenderActionP *>(userdata);
  p->builder.addLine(action, v0, v1);
}

void
SoWgpuRenderActionP::pointCB(void * userdata,
                            SoCallbackAction * action,
                            const SoPrimitiveVertex * vertex)
{
  SoWgpuRenderActionP * p = static_cast<SoWgpuRenderActionP *>(userdata);
  p->builder.addPoint(action, vertex);
}

SoCallbackAction::Response
SoWgpuRenderActionP::sceneTexturePreCB(void * userdata,
                                       SoCallbackAction * action,
                                       const SoNode * node)
{
  SoWgpuRenderActionP * p = static_cast<SoWgpuRenderActionP *>(userdata);
  const SoSceneTexture2 * texture = static_cast<const SoSceneTexture2 *>(node);
  SoState * state = action ? action->getState() : NULL;
  if (!state) {
    p->setDiagnostic(SoWgpuDiagnosticShell::action(
      SoWgpuRenderAction::BACKEND_ERROR, SoWgpuDiagnosticDomain::ACTION,
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
    p->setDiagnostic(SoWgpuDiagnosticShell::action(
      SoWgpuRenderAction::UNSUPPORTED, SoWgpuDiagnosticDomain::FRAME_PLAN,
      SbString("SoSceneTexture2 supports only unit 0, RGBA8, MODULATE, REPEAT/CLAMP, NONE transparency function and no sceneTransparencyType")));
    return SoCallbackAction::ABORT;
  }

  if (SoTextureQualityElement::get(state) <= 0.0f) {
    SoMultiTextureImageElement::setDefault(state, const_cast<SoSceneTexture2 *>(texture), 0);
    SoMultiTextureEnabledElement::set(state, const_cast<SoSceneTexture2 *>(texture), 0, FALSE);
    return SoCallbackAction::CONTINUE;
  }

  bool useDirect = false;
#if defined(HAVE_WGPU_RUST_BRIDGE) || defined(HAVE_WGPU_BGFX)
  const char * directMode = std::getenv("COIN_WGPU_RTT_GPU_DIRECT");
  useDirect = p->directPasses &&
              directMode && directMode[0] == '1' && directMode[1] == '\0';
#endif

  const SbVec2s size = texture->size.getValue();
  SoNode * scene = texture->scene.getValue();
  if (!scene || size[0] <= 0 || size[1] <= 0 ||
      size[0] > 2048 || size[1] > 2048) {
    p->setDiagnostic(SoWgpuDiagnosticShell::action(
      SoWgpuRenderAction::UNSUPPORTED, SoWgpuDiagnosticDomain::FRAME_PLAN,
      SbString("SoSceneTexture2 requires a scene and dimensions in 1..2048")));
    return SoCallbackAction::ABORT;
  }

  // Direct RTT retains RGBA8 color and depth32 attachments simultaneously.
  const size_t chargedBytes = size_t(size[0]) * size_t(size[1]) * (useDirect ? 8 : 4);
  const size_t maxBudgetBytes = size_t(64) * 1024 * 1024;
  if (!useDirect && chargedBytes > maxBudgetBytes - *p->sceneTextureStagedBytes) {
    p->setDiagnostic(SoWgpuDiagnosticShell::action(
      SoWgpuRenderAction::UNSUPPORTED, SoWgpuDiagnosticDomain::TARGET,
      SbString(useDirect
        ? "SoSceneTexture2 GPU attachment budget exceeds 64 MiB per apply"
        : "SoSceneTexture2 staged RGBA8 budget exceeds 64 MiB per apply")));
    return SoCallbackAction::ABORT;
  }

  static thread_local std::vector<const SoSceneTexture2 *> activeTextures;
  if (activeTextures.size() >= 8 ||
      std::find(activeTextures.begin(), activeTextures.end(), texture) != activeTextures.end()) {
    p->setDiagnostic(SoWgpuDiagnosticShell::action(
      SoWgpuRenderAction::UNSUPPORTED, SoWgpuDiagnosticDomain::FRAME_PLAN,
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
#if defined(HAVE_WGPU_BGFX)
  // The evaluation backend owns a process-wide BGFX singleton. Release a
  // previously prepared parent before executing staged child passes; the
  // parent will be prepared again when its completed frame is submitted.
  if (!useDirect && p->target != NULL &&
      dynamic_cast<SoWgpuBgfxBackend *>(p->target->getPimpl()->backend.get()) != NULL) {
    p->target->getPimpl()->backend.reset();
  }
#endif
  std::unique_ptr<SoWgpuRenderTarget> childTarget;
  if (!useDirect) {
    childTarget.reset(SoWgpuRenderTarget::createOffscreen(SbVec2i32(size[0], size[1])));
    if (!childTarget || childTarget->getStatus() != SoWgpuRenderTarget::TARGET_READY) {
      p->setDiagnostic(SoWgpuDiagnosticShell::action(
        SoWgpuRenderAction::BACKEND_ERROR, SoWgpuDiagnosticDomain::TARGET,
        SbString("Cannot create SoSceneTexture2 offscreen target")));
      return SoCallbackAction::ABORT;
    }
    // The staged SoSceneTexture2 path consumes RGBA8 only. Keeping the
    // default CPU depth output enabled would reject this otherwise valid
    // color-only pass on backends such as BGFX that do not publish depth.
    if (!childTarget->setDepthReadbackEnabled(FALSE)) {
      p->setDiagnostic(SoWgpuDiagnosticShell::action(
        SoWgpuRenderAction::BACKEND_ERROR, SoWgpuDiagnosticDomain::TARGET,
        SbString("Cannot configure SoSceneTexture2 color-only offscreen target")));
      return SoCallbackAction::ABORT;
    }
  }
  SoWgpuRenderAction childAction(SbViewportRegion(size[0], size[1]));
  childAction.pimpl->sceneTextureDirectTokens = p->sceneTextureDirectTokens;
  childAction.pimpl->directPasses = p->directPasses;
  childAction.pimpl->planOnly = useDirect;
  childAction.setRenderTarget(childTarget.get());
  childAction.setTransparencyType(p->transparencyType);
  childAction.setBackgroundColor(SbColor4f(background[0], background[1],
                                           background[2], background[3]));
  childAction.pimpl->sceneTextureStagedBytes = p->sceneTextureStagedBytes;
  childAction.apply(scene);
  if (childAction.getLastStatus() != SoWgpuRenderAction::SUCCESS) {
    p->setDiagnostic(SoWgpuDiagnosticShell::withContext(
      childAction.getLastStatus(), childAction.pimpl->lastDiagnosticDomain,
      "SoSceneTexture2 subscene", childAction.getLastError()));
    return SoCallbackAction::ABORT;
  }

#if defined(HAVE_WGPU_RUST_BRIDGE) || defined(HAVE_WGPU_BGFX)
  if (useDirect) {
    if (!p->directPasses || !childAction.pimpl->hasLastValidPlan) {
      p->setDiagnostic(SoWgpuDiagnosticShell::action(
        SoWgpuRenderAction::BACKEND_ERROR, SoWgpuDiagnosticDomain::FRAME_PLAN,
        SbString("SoSceneTexture2 direct pass was not planned")));
      return SoCallbackAction::ABORT;
    }
    const SbVec2i32 passSize(size[0], size[1]);
    const FramePlan & snapshot = childAction.pimpl->lastValidPlan;
    uint64_t token = 0;
    for (size_t i = 0; i < p->directPasses->size(); ++i) {
      const SoWgpuRenderActionP::DirectPass & existing = (*p->directPasses)[i];
      if (existing.size == passSize && existing.plan.hasSamePayload(snapshot)) {
        token = i + 1;
        break;
      }
    }
    if (!token) {
      if (chargedBytes > maxBudgetBytes - *p->sceneTextureStagedBytes) {
        p->setDiagnostic(SoWgpuDiagnosticShell::action(
          SoWgpuRenderAction::UNSUPPORTED, SoWgpuDiagnosticDomain::TARGET,
          SbString("SoSceneTexture2 GPU attachment budget exceeds 64 MiB per apply")));
        return SoCallbackAction::ABORT;
      }
      *p->sceneTextureStagedBytes += chargedBytes;
      SoWgpuRenderActionP::DirectPass pass;
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
      p->setDiagnostic(SoWgpuDiagnosticShell::action(
        SoWgpuRenderAction::BACKEND_ERROR, SoWgpuDiagnosticDomain::ACTION,
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
    p->setDiagnostic(SoWgpuDiagnosticShell::action(
      SoWgpuRenderAction::BACKEND_ERROR, SoWgpuDiagnosticDomain::READBACK,
      SbString("SoSceneTexture2 subscene returned incomplete RGBA8 readback")));
    return SoCallbackAction::ABORT;
  }
  if (!SoWgpuImageCore::flipRgba8Rows(
        pixels, SbVec2i32(static_cast<int32_t>(size[0]),
                         static_cast<int32_t>(size[1])))) {
    p->setDiagnostic(SoWgpuDiagnosticShell::action(
      SoWgpuRenderAction::BACKEND_ERROR, SoWgpuDiagnosticDomain::READBACK,
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
SoWgpuRenderActionP::depthBufferPreCB(void *, SoCallbackAction * action, const SoNode * node)
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
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoWgpuRenderActionP::annotationPreCB(void * userdata, SoCallbackAction *, const SoNode *)
{
  static_cast<SoWgpuRenderActionP *>(userdata)->builder.beginAnnotation();
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoWgpuRenderActionP::annotationPostCB(void * userdata, SoCallbackAction *, const SoNode *)
{
  static_cast<SoWgpuRenderActionP *>(userdata)->builder.endAnnotation();
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoWgpuRenderActionP::lightPreCB(void * userdata,
                               SoCallbackAction * action,
                               const SoNode * /*node*/)
{
  SoWgpuRenderActionP * p = static_cast<SoWgpuRenderActionP *>(userdata);
  p->builder.recordLightAttenuation(action);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoWgpuRenderActionP::indexedFaceSetPreCB(void * userdata,
                                        SoCallbackAction * action,
                                        const SoNode * node)
{
  SoWgpuRenderActionP * p = static_cast<SoWgpuRenderActionP *>(userdata);
  if (!p->fastPathEnabled) {
    return SoCallbackAction::CONTINUE;
  }

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
        p->setDiagnostic(SoWgpuDiagnosticShell::action(
          SoWgpuRenderAction::UNSUPPORTED, SoWgpuDiagnosticDomain::FRAME_PLAN,
          SbString("Procedural/DEFAULT texture coordinates are not supported in Subwave 3B")));
        return SoCallbackAction::ABORT;
      }
    }
  }

  DirectGeometryView view;
  view.positions = SoWgpuSpan<SbVec3f>(coordArray, static_cast<size_t>(numCoords));

  const SoNormalElement * normElem = SoNormalElement::getInstance(state);
  if (normElem && normElem->getNum() > 0) {
    view.normals = SoWgpuSpan<SbVec3f>(normElem->getArrayPtr(), static_cast<size_t>(normElem->getNum()));
  }

  if (ifs->coordIndex.getNum() > 0) {
    view.coordIndex = SoWgpuSpan<int32_t>(ifs->coordIndex.getValues(0), static_cast<size_t>(ifs->coordIndex.getNum()));
  }
  if (ifs->normalIndex.getNum() > 0 && !(ifs->normalIndex.getNum() == 1 && ifs->normalIndex[0] == -1)) {
    view.normalIndex = SoWgpuSpan<int32_t>(ifs->normalIndex.getValues(0), static_cast<size_t>(ifs->normalIndex.getNum()));
  }
  if (ifs->materialIndex.getNum() > 0 && !(ifs->materialIndex.getNum() == 1 && ifs->materialIndex[0] == -1)) {
    view.materialIndex = SoWgpuSpan<int32_t>(ifs->materialIndex.getValues(0), static_cast<size_t>(ifs->materialIndex.getNum()));
  }
  if (ifs->textureCoordIndex.getNum() > 0 && !(ifs->textureCoordIndex.getNum() == 1 && ifs->textureCoordIndex[0] == -1)) {
    view.texCoordIndex = SoWgpuSpan<int32_t>(ifs->textureCoordIndex.getValues(0), static_cast<size_t>(ifs->textureCoordIndex.getNum()));
  }

  const SoMultiTextureCoordinateElement * tcElem = SoMultiTextureCoordinateElement::getInstance(state);
  if (tcElem) {
    SoMultiTextureCoordinateElement::CoordType ct = tcElem->getType(0);
    if (ct == SoMultiTextureCoordinateElement::EXPLICIT) {
      int32_t numTc = tcElem->getNum(0);
      if (numTc > 0) {
        const SbVec2f * tcPtr = tcElem->getArrayPtr2(0);
        if (tcPtr) {
          view.texcoords = SoWgpuSpan<SbVec2f>(tcPtr, static_cast<size_t>(numTc));
        }
      }
    }
  }

  view.materialBinding = SoMaterialBindingElement::get(state);
  view.normalBinding = SoNormalBindingElement::get(state);

  std::string err;
  FastPathResult res = p->builder.processIndexedFaceSet(action, view, const_cast<SoNode *>(node), &err);

  if (vp) {
    state->pop();
  }

  if (res == FastPathResult::SUCCESS_PRUNE) {
    return SoCallbackAction::PRUNE;
  } else if (res == FastPathResult::INVALID_SCENE) {
    p->setDiagnostic(SoWgpuDiagnosticShell::action(
      SoWgpuRenderAction::INVALID_SCENE, SoWgpuDiagnosticDomain::FRAME_PLAN,
      SbString(err.empty() ? "Invalid scene in IndexedFaceSet" : err.c_str())));
    return SoCallbackAction::ABORT;
  } else if (res == FastPathResult::UNSUPPORTED) {
    p->setDiagnostic(SoWgpuDiagnosticShell::action(
      SoWgpuRenderAction::UNSUPPORTED, SoWgpuDiagnosticDomain::FRAME_PLAN,
      SbString(err.empty() ? "Unsupported feature in IndexedFaceSet" : err.c_str())));
    return SoCallbackAction::ABORT;
  } else {
    return SoCallbackAction::CONTINUE;
  }
}

SoCallbackAction::Response
SoWgpuRenderActionP::indexedLineSetPreCB(void * userdata,
                                        SoCallbackAction * action,
                                        const SoNode * node)
{
  SoWgpuRenderActionP * p = static_cast<SoWgpuRenderActionP *>(userdata);
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

  DirectGeometryView view;
  view.positions = SoWgpuSpan<SbVec3f>(coordArray, static_cast<size_t>(numCoords));

  if (ils->coordIndex.getNum() > 0) {
    view.coordIndex = SoWgpuSpan<int32_t>(ils->coordIndex.getValues(0), static_cast<size_t>(ils->coordIndex.getNum()));
  }
  if (ils->materialIndex.getNum() > 0 && !(ils->materialIndex.getNum() == 1 && ils->materialIndex[0] == -1)) {
    view.materialIndex = SoWgpuSpan<int32_t>(ils->materialIndex.getValues(0), static_cast<size_t>(ils->materialIndex.getNum()));
  }

  view.materialBinding = SoMaterialBindingElement::get(state);

  std::string err;
  FastPathResult res = p->builder.processIndexedLineSet(action, view, const_cast<SoNode *>(node), &err);

  if (vp) {
    state->pop();
  }

  if (res == FastPathResult::SUCCESS_PRUNE) {
    return SoCallbackAction::PRUNE;
  } else if (res == FastPathResult::INVALID_SCENE) {
    p->setDiagnostic(SoWgpuDiagnosticShell::action(
      SoWgpuRenderAction::INVALID_SCENE, SoWgpuDiagnosticDomain::FRAME_PLAN,
      SbString(err.empty() ? "Invalid scene in IndexedLineSet" : err.c_str())));
    return SoCallbackAction::ABORT;
  } else if (res == FastPathResult::UNSUPPORTED) {
    p->setDiagnostic(SoWgpuDiagnosticShell::action(
      SoWgpuRenderAction::UNSUPPORTED, SoWgpuDiagnosticDomain::FRAME_PLAN,
      SbString(err.empty() ? "Unsupported feature in IndexedLineSet" : err.c_str())));
    return SoCallbackAction::ABORT;
  } else {
    return SoCallbackAction::CONTINUE;
  }
}
