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
#include <Inventor/nodes/SoTexture3.h>
#include <Inventor/nodes/SoTextureCubeMap.h>
#include <Inventor/nodes/SoSceneTextureCubeMap.h>
#include <Inventor/nodes/SoShaderProgram.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/annex/FXViz/elements/SoShadowStyleElement.h>
#include <Inventor/elements/SoEnvironmentElement.h>
#include <Inventor/nodes/SoSpotLight.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoDirectionalLight.h>
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
#include "rendering/coinrender/CoinRenderRttExecution.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderSelectionCore.h"
#include "actions/SoSubActionP.h"
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
#include "rendering/coinwgpu/CoinWgpuBackend.h"
#elif defined(HAVE_COIN_DAWN) || defined(HAVE_COIN_WGPU_NATIVE)
#include "rendering/coinwgpu/CoinWgpuNativeBackend.h"
#elif defined(HAVE_COIN_BGFX)
#include "rendering/coinbgfx/CoinBgfxBackend.h"
#endif

SO_ACTION_SOURCE(CoinRenderAction);

void
CoinRenderAction::initClass(void)
{
  SO_ACTION_INTERNAL_INIT_CLASS(CoinRenderAction, SoCallbackAction);
  SO_ENABLE(CoinRenderAction, SoDepthBufferElement);
  SO_ENABLE(CoinRenderAction, SoTextureCombineElement);
  SO_ENABLE(CoinRenderAction, SoShadowStyleElement);
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
  if (this->pimpl->target != target && this->pimpl->target)
    this->pimpl->target->getPimpl()->detachedFromAction();
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

  const bool ownsSceneTexturePlan = !this->sceneTexturePlan;
  if (ownsSceneTexturePlan)
    this->sceneTexturePlan =
        std::make_shared<CoinRenderRttPlan>(this->executionOptions.sceneTexture);
  struct SceneTexturePlanScope {
    CoinRenderActionP * action;
    bool owns;
    ~SceneTexturePlanScope() {
      if (owns)
        action->sceneTexturePlan.reset();
    }
  } sceneTexturePlanScope{this, ownsSceneTexturePlan};
  for (const auto& texture : this->lastValidPlan.textures)
    if (texture.producerId)
      planCacheAllowed = false;
  const bool traversalSkipped = planCacheAllowed && cacheRoot && this->hasLastValidPlan &&
    this->cachedRoot == cacheRoot && this->cachedRootId == cacheRoot->getNodeId() &&
    !this->cameraOnlyDirty && !this->cameraPatchInvalidated;
  CoinRenderFrameReuseDecision reuseDecision = traversalSkipped
    ? CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::REUSE,
                               this->lastValidPlan.revision)
    : CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::UNKNOWN, 0);
  this->sceneTexturePixels.clear();
  this->shadowStyleBeforeGroups.clear();
  this->activeShadowGroupNodes.clear();
  this->lastRejectedShadowFrame = CoinRenderFramePlan();
  this->lastRejectedShadowPlan = CoinRenderShadowPlan();
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
  if (!plan.shadowGroups.empty()) {
    CoinRenderShadowPlan shadowPlan;
    std::string shadowDiagnostic;
    if (!coin_render_plan_shadows(plan, shadowPlan, shadowDiagnostic)) {
      this->lastRejectedShadowFrame = std::move(plan);
      this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString(shadowDiagnostic.c_str())));
      return;
    }
    bool executableShadow = false;
    // Staged SoSceneTexture2 producers are captured without a target. Their
    // immutable frame is later executed in an ordinary offscreen child target.
    if (this->planOnly && this->sceneTexturePlan->mode == COIN_RENDER_SCENE_TEXTURE_STAGED) {
      std::string profileDiagnostic;
      executableShadow = coin_render_shadow_opaque_profile(
        plan, shadowPlan, shadowPlan.passes.size(), profileDiagnostic);
    }
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
    if (this->target && this->target->getPimpl()->kind == CoinRenderTargetP::KIND_OFFSCREEN &&
        !this->target->getPimpl()->directTextureOutput && !this->asyncTicket &&
        this->sceneTexturePlan->producers.empty() &&
        (!this->target->getPimpl()->backend ||
         dynamic_cast<CoinWgpuBackend *>(this->target->getPimpl()->backend.get()))) {
      std::string profileDiagnostic;
      executableShadow = coin_render_shadow_opaque_profile(
        plan, shadowPlan, shadowPlan.passes.size(), profileDiagnostic);
    }
#endif
#if defined(HAVE_COIN_BGFX)
    if (this->target && this->target->getPimpl()->kind == CoinRenderTargetP::KIND_OFFSCREEN &&
        !this->target->getPimpl()->directTextureOutput && !this->asyncTicket &&
        this->sceneTexturePlan->producers.empty() &&
        (!this->target->getPimpl()->backend ||
         dynamic_cast<CoinBgfxBackend *>(this->target->getPimpl()->backend.get()))) {
      std::string profileDiagnostic;
      executableShadow = coin_render_shadow_opaque_profile(
        plan, shadowPlan, shadowPlan.passes.size(), profileDiagnostic);
    }
#endif
    if (!executableShadow) {
      this->lastRejectedShadowFrame = std::move(plan);
      this->lastRejectedShadowPlan = std::move(shadowPlan);
      this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString("Active SoShadowGroup requires a qualified opaque shadow executor for this target")));
      return;
    }
    // Shadow maps depend on captured light/geometry state; reuse and camera
    // overlays are not qualified for this first executable profile.
    planCacheAllowed = false;
  }
  profilePlanned = ProfileClock::now();
  if (!this->sceneTexturePlan->producers.empty())
    planCacheAllowed = false;
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
  const CoinRenderFramePlan& capturedPlan = useCachedPlan ? this->lastValidPlan : plan;
  const CoinRenderFramePlan& framePlan = capturedPlan;

  if (this->planOnly) {
    if (!useCachedPlan) {
      this->lastValidPlan = std::move(plan);
      this->hasLastValidPlan = true;
    }
    this->setDiagnostic(CoinRenderDiagnosticShell::success());
    return;
  }

  if (this->target && this->target->getStatus() == CoinRenderTarget::TARGET_ERROR) {
    this->setDiagnostic(CoinRenderDiagnosticShell::fromTarget(this->target->getStatus(),
                                                              this->target->getLastError()));
    return;
  }
  if (!this->target && this->asyncTicket) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::NO_TARGET, CoinRenderDiagnosticDomain::TARGET,
        SbString("applyAsync() requires an offscreen render target")));
    return;
  }
  if (this->target) {
    const auto admission =
        this->target->getPimpl().get().preflightSubmission(this->asyncTicket != nullptr);
    if (admission.status != CoinRenderBackendStatus::SUCCESS) {
      this->setDiagnostic(CoinRenderDiagnosticShell::fromBackend(admission));
      return;
    }
  }
  CoinRenderRttExecution rttExecution(this->target ? &this->target->getPimpl().get() : nullptr,
                                      this->executionOptions);
  CoinRenderFramePlan resolvedPlan;
  const bool hasSceneTextures = !this->sceneTexturePlan->producers.empty();
  if (hasSceneTextures) {
    const auto prepared = rttExecution.prepare(*this->sceneTexturePlan, capturedPlan, resolvedPlan);
    if (prepared.status != CoinRenderBackendStatus::SUCCESS) {
      this->setDiagnostic(CoinRenderDiagnosticShell::fromBackend(prepared));
      return;
    }
  }
  const CoinRenderFramePlan& executionPlan = hasSceneTextures ? resolvedPlan : capturedPlan;
  const bool retainStagedPixels =
      hasSceneTextures && this->sceneTexturePlan->mode == COIN_RENDER_SCENE_TEXTURE_STAGED;

  if (this->target == NULL) {
    // Mode 0: Recording backend
    this->lastRecordingLog = this->recordingBackend.recordToString(executionPlan).c_str();
    this->recordingLogValid = true;
    if (!useCachedPlan) {
      this->lastValidPlan = retainStagedPixels ? std::move(resolvedPlan) : std::move(plan);
      this->hasLastValidPlan = true;
    }
    if (cacheRoot) {
      this->cachedRoot = planCacheAllowed ? cacheRoot : NULL;
      this->cachedRootId = cacheRoot->getNodeId();
      if (cameraOverlay) this->cameraOnlyDirty = false;
      else if (!traversalSkipped)
        this->rememberFrameRoot(planCacheAllowed ? cacheRoot : NULL);
    }
    overlayScope.committed = true;
    this->setDiagnostic(CoinRenderDiagnosticShell::success());
    return;
  }

  // Execute frame on target
  CoinRenderFrameExecutionResult execRes =
      this->asyncTicket
          ? this->target->pimpl->executeFrameAsync(executionPlan, *this->asyncTicket, reuseDecision)
          : this->target->pimpl->executeFrame(executionPlan, reuseDecision);
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
    this->lastValidPlan = retainStagedPixels ? std::move(resolvedPlan) : std::move(plan);
    this->hasLastValidPlan = true;
    this->recordingLogValid = false;
  } else if (cameraOverlay) {
    this->recordingLogValid = false;
  }
  if (cacheRoot) {
    this->cachedRoot = planCacheAllowed ? cacheRoot : NULL;
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

SoCallbackAction::Response
CoinRenderActionP::unsupportedEffectPreCB(void * userdata, SoCallbackAction *, const SoNode * node)
{
  const char * diagnostic = nullptr;
  if (node->isOfType(SoTexture3::getClassTypeId())) {
    const auto * texture = static_cast<const SoTexture3 *>(node);
    SbVec3s size;
    int components = 0;
    texture->images.getValue(size, components);
    if (size == SbVec3s(0, 0, 0) &&
        (texture->filenames.getNum() == 0 || texture->filenames[0].getLength() == 0))
      return SoCallbackAction::CONTINUE;
    diagnostic = "SoTexture3 requires a 3D texture contract and executor";
  } else if (node->isOfType(SoTextureCubeMap::getClassTypeId())) {
    const auto * texture = static_cast<const SoTextureCubeMap *>(node);
    const SoSFImage * faces[] = {&texture->imagePosX, &texture->imageNegX,
      &texture->imagePosY, &texture->imageNegY, &texture->imagePosZ, &texture->imageNegZ};
    bool hasFace = false;
    for (const SoSFImage * face : faces) {
      SbVec2s size;
      int components = 0;
      face->getValue(size, components);
      hasFace = hasFace || size != SbVec2s(0, 0);
    }
    if (!hasFace && (texture->filenames.getNum() == 0 ||
                     texture->filenames[0].getLength() == 0))
      return SoCallbackAction::CONTINUE;
    diagnostic = "SoTextureCubeMap requires a cube texture contract and executor";
  } else if (node->isOfType(SoSceneTextureCubeMap::getClassTypeId())) {
    if (!static_cast<const SoSceneTextureCubeMap *>(node)->scene.getValue())
      return SoCallbackAction::CONTINUE;
    diagnostic = "SoSceneTextureCubeMap requires a cube RTT contract and executor";
  } else if (node->isOfType(SoShaderProgram::getClassTypeId())) {
    if (static_cast<const SoShaderProgram *>(node)->shaderObject.getNum() == 0)
      return SoCallbackAction::CONTINUE;
    diagnostic = "SoShaderProgram has no portable shader contract";
  }
  if (!diagnostic) return SoCallbackAction::CONTINUE;
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  p->setDiagnostic(CoinRenderDiagnosticShell::action(
    CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
    SbString(diagnostic)));
  return SoCallbackAction::ABORT;
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
  this->master->addPreCallback(SoTexture3::getClassTypeId(), unsupportedEffectPreCB, this);
  this->master->addPreCallback(SoTextureCubeMap::getClassTypeId(), unsupportedEffectPreCB, this);
  this->master->addPreCallback(SoSceneTextureCubeMap::getClassTypeId(), unsupportedEffectPreCB, this);
  this->master->addPreCallback(SoShadowGroup::getClassTypeId(), shadowGroupPreCB, this);
  this->master->addPostCallback(SoShadowGroup::getClassTypeId(), shadowGroupPostCB, this);
  this->master->addPreCallback(SoShadowStyle::getClassTypeId(), shadowStylePreCB, this);
  this->master->addPreCallback(SoShaderProgram::getClassTypeId(), unsupportedEffectPreCB, this);
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
  if (SoTextureUnitElement::get(state) != 0 || texture->type.getValue() != SoSceneTexture2::RGBA8 ||
      texture->model.getValue() != SoSceneTexture2::MODULATE ||
      (texture->wrapS.getValue() != SoSceneTexture2::REPEAT &&
       texture->wrapS.getValue() != SoSceneTexture2::CLAMP) ||
      (texture->wrapT.getValue() != SoSceneTexture2::REPEAT &&
       texture->wrapT.getValue() != SoSceneTexture2::CLAMP) ||
      (texture->transparencyFunction.getValue() != SoSceneTexture2::NONE &&
       texture->transparencyFunction.getValue() != SoSceneTexture2::ALPHA_BLEND) ||
      texture->sceneTransparencyType.getValue() != NULL) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString("SoSceneTexture2 supports only unit 0, RGBA8, MODULATE, REPEAT/CLAMP, "
                 "NONE/ALPHA_BLEND transparency function and no sceneTransparencyType")));
    return SoCallbackAction::ABORT;
  }

  if (SoTextureQualityElement::get(state) <= 0.0f) {
    SoMultiTextureImageElement::setDefault(state, const_cast<SoSceneTexture2 *>(texture), 0);
    SoMultiTextureEnabledElement::set(state, const_cast<SoSceneTexture2 *>(texture), 0, FALSE);
    return SoCallbackAction::CONTINUE;
  }

  const SbVec2s size = texture->size.getValue();
  SoNode * scene = texture->scene.getValue();
  std::string diagnostic;
  const SbVec2i32 passSize(size[0], size[1]);
  const uint64_t sourceRevision = texture->getNodeId();
  if (!scene || !p->sceneTexturePlan ||
      !p->sceneTexturePlan->enter(sourceRevision, passSize, diagnostic)) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString(diagnostic.empty() ? "SoSceneTexture2 requires a scene and dimensions in 1..2048"
                                    : diagnostic.c_str())));
    return SoCallbackAction::ABORT;
  }
  struct ActiveTextureScope {
    CoinRenderRttPlan& graph;
    ~ActiveTextureScope() { graph.leave(); }
  } scope{*p->sceneTexturePlan};
  const SbVec4f background = texture->backgroundColor.getValue();
  CoinRenderAction childAction(SbViewportRegion(size[0], size[1]));
  childAction.pimpl->sceneTexturePlan = p->sceneTexturePlan;
  childAction.pimpl->planOnly = true;
  childAction.pimpl->executionOptions = p->executionOptions;
  childAction.setTransparencyType(p->transparencyType);
  childAction.setSortedLayersNumPasses(static_cast<int>(p->transparencyOptions.layers));
  childAction.setTransparencyBufferBudget(p->transparencyOptions.bufferBudget);
  childAction.setBackgroundColor(
      SbColor4f(background[0], background[1], background[2], background[3]));
  childAction.apply(scene);
  if (childAction.getLastStatus() != CoinRenderAction::SUCCESS) {
    p->setDiagnostic(CoinRenderDiagnosticShell::withContext(
      childAction.getLastStatus(), childAction.pimpl->lastDiagnosticDomain,
      "SoSceneTexture2 subscene", childAction.getLastError()));
    return SoCallbackAction::ABORT;
  }
  CoinRenderRttProducer producer;
  producer.plan = std::move(childAction.pimpl->lastValidPlan);
  producer.size = passSize;
  producer.sourceRevision = sourceRevision;
  uint64_t producerId = 0;
  if (!p->sceneTexturePlan->append(std::move(producer), producerId, diagnostic)) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
                                                       CoinRenderDiagnosticDomain::FRAME_PLAN,
                                                       SbString(diagnostic.c_str())));
    return SoCallbackAction::ABORT;
  }
  p->sceneTexturePixels.emplace_back(4, 0);
  auto& marker = p->sceneTexturePixels.back();
  for (unsigned int i = 0; i < 4; ++i)
    marker[i] = static_cast<uint8_t>(producerId >> (i * 8));
  SoMultiTextureImageElement::set(
      state, const_cast<SoSceneTexture2*>(texture), 0, SbVec2s(1, 1), 4, marker.data(),
      static_cast<SoMultiTextureImageElement::Wrap>(texture->wrapS.getValue()),
      static_cast<SoMultiTextureImageElement::Wrap>(texture->wrapT.getValue()),
      SoMultiTextureImageElement::MODULATE, texture->blendColor.getValue());
  SbVec2s markerSize;
  int markerComponents = 0;
  SoMultiTextureImageElement::Wrap ws, wt;
  SoMultiTextureImageElement::Model model;
  SbColor blend;
  const unsigned char* image =
      SoMultiTextureImageElement::get(state, 0, markerSize, markerComponents, ws, wt, model, blend);
  if (!image || markerSize != SbVec2s(1, 1) || markerComponents != 4) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::BACKEND_ERROR, CoinRenderDiagnosticDomain::ACTION,
        SbString("SoSceneTexture2 logical marker was not retained by traversal state")));
    return SoCallbackAction::ABORT;
  }
  p->builder.registerSceneTexture(image, producerId, uint32_t(size[0]), uint32_t(size[1]),
                                  background[3] >= 1.0f, texture->transparencyFunction.getValue());
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
CoinRenderActionP::shadowGroupPreCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  const auto * group = static_cast<const SoShadowGroup *>(node);
  if (!group->isActive.getValue()) return SoCallbackAction::CONTINUE;
  CoinRenderShadowGroupSnapshot snapshot;
  snapshot.sourceRevision = node->getNodeId();
  snapshot.intensity = group->intensity.getValue();
  snapshot.precision = group->precision.getValue();
  snapshot.quality = group->quality.getValue();
  snapshot.epsilon = group->epsilon.getValue();
  snapshot.threshold = group->threshold.getValue();
  snapshot.smoothBorder = group->smoothBorder.getValue();
  snapshot.shadowCachingEnabled = group->shadowCachingEnabled.getValue() != FALSE;
  snapshot.visibilityNearRadius = group->visibilityNearRadius.getValue();
  snapshot.visibilityRadius = group->visibilityRadius.getValue();
  snapshot.visibilityFlag = group->visibilityFlag.getValue();
  snapshot.nested = p->builder.hasActiveShadowGroup();
  snapshot.hasEntryCamera = true;
  snapshot.entryCamera = CoinRenderFramePlanBuilder::captureCamera(action);
  snapshot.entryModel = action->getModelMatrix();
  p->shadowStyleBeforeGroups.push_back(SoShadowStyleElement::get(action->getState()));
  SoShadowStyleElement::set(action->getState(), 3);
  p->builder.beginShadowGroup(snapshot);
  p->activeShadowGroupNodes.push_back(group);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::shadowGroupPostCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  if (!static_cast<const SoShadowGroup *>(node)->isActive.getValue())
    return SoCallbackAction::CONTINUE;
  p->builder.endShadowGroup();
  if (!p->activeShadowGroupNodes.empty()) p->activeShadowGroupNodes.pop_back();
  if (!p->shadowStyleBeforeGroups.empty()) {
    SoShadowStyleElement::set(action->getState(), p->shadowStyleBeforeGroups.back());
    p->shadowStyleBeforeGroups.pop_back();
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::shadowStylePreCB(void *, SoCallbackAction * action, const SoNode * node)
{
  const auto * style = static_cast<const SoShadowStyle *>(node);
  SoShadowStyleElement::set(action->getState(), const_cast<SoShadowStyle *>(style),
                            style->style.getValue());
  return SoCallbackAction::CONTINUE;
}

// Only a direct separator whose descendants are shapes, translations or
// separators can reuse geometry from the ordinary traversal. Other Coin
// subscenes need a separate captured frame and remain unsupported.
static bool
coin_render_shadow_scene_shapes(const SoNode * node,
                                std::vector<SbUniqueId> & ids)
{
  if (!node || node->getTypeId() != SoSeparator::getClassTypeId()) return false;
  const auto * separator = static_cast<const SoSeparator *>(node);
  for (int i = 0; i < separator->getNumChildren(); ++i) {
    const SoNode * child = separator->getChild(i);
    if (child->isOfType(SoShape::getClassTypeId())) {
      const SbUniqueId id = child->getNodeId();
      if (std::find(ids.begin(), ids.end(), id) != ids.end()) return false;
      ids.push_back(id);
    }
    else if (child->getTypeId() == SoSeparator::getClassTypeId()) {
      if (!coin_render_shadow_scene_shapes(child, ids)) return false;
    }
    else if (child->getTypeId() != SoTranslation::getClassTypeId()) return false;
  }
  return !ids.empty();
}

SoCallbackAction::Response
CoinRenderActionP::lightPreCB(void * userdata,
                               SoCallbackAction * action,
                               const SoNode * node)
{
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  p->builder.recordLightAttenuation(action);
  if (!p->builder.hasActiveShadowGroup()) return SoCallbackAction::CONTINUE;
  const auto * light = static_cast<const SoLight *>(node);
  CoinRenderShadowLightSnapshot snapshot;
  snapshot.groupSlot = p->builder.activeShadowGroupSlot();
  snapshot.sourceRevision = node->getNodeId();
  snapshot.enabled = light->on.getValue() != FALSE;
  snapshot.color = light->color.getValue();
  snapshot.intensity = light->intensity.getValue();
  snapshot.attenuation = SoEnvironmentElement::getLightAttenuation(action->getState());
  snapshot.model = action->getModelMatrix();
  snapshot.modelViewAtLight = action->getModelMatrix() * action->getViewingMatrix();
  if (node->isOfType(SoSpotLight::getClassTypeId())) {
    const auto * spot = static_cast<const SoSpotLight *>(node);
    snapshot.type = CoinRenderLightType::SPOT;
    snapshot.shadowEligible = true;
    snapshot.position = spot->location.getValue();
    snapshot.direction = spot->direction.getValue();
    snapshot.cutOffAngle = spot->cutOffAngle.getValue();
    snapshot.dropOffRate = spot->dropOffRate.getValue();
    if (node->isOfType(SoShadowSpotLight::getClassTypeId())) {
      const auto * shadow = static_cast<const SoShadowSpotLight *>(node);
      snapshot.hasCustomScene = shadow->shadowMapScene.getValue() != nullptr;
      snapshot.nearDistance = shadow->nearDistance.getValue();
      snapshot.farDistance = shadow->farDistance.getValue();
    }
  } else if (node->isOfType(SoDirectionalLight::getClassTypeId())) {
    const auto * directional = static_cast<const SoDirectionalLight *>(node);
    snapshot.type = CoinRenderLightType::DIRECTIONAL;
    snapshot.direction = directional->direction.getValue();
    if (node->isOfType(SoShadowDirectionalLight::getClassTypeId())) {
      const auto * shadow = static_cast<const SoShadowDirectionalLight *>(node);
      snapshot.shadowEligible = true;
      snapshot.hasCustomScene = shadow->shadowMapScene.getValue() != nullptr;
      snapshot.maxShadowDistance = shadow->maxShadowDistance.getValue();
      snapshot.bboxCenter = shadow->bboxCenter.getValue();
      snapshot.bboxSize = shadow->bboxSize.getValue();
    }
  } else if (node->isOfType(SoPointLight::getClassTypeId())) {
    snapshot.type = CoinRenderLightType::POINT;
    snapshot.position = static_cast<const SoPointLight *>(node)->location.getValue();
  }
  if (snapshot.hasCustomScene && !p->activeShadowGroupNodes.empty()) {
    const SoNode * scene = nullptr;
    if (node->isOfType(SoShadowSpotLight::getClassTypeId()))
      scene = static_cast<const SoShadowSpotLight *>(node)->shadowMapScene.getValue();
    else if (node->isOfType(SoShadowDirectionalLight::getClassTypeId()))
      scene = static_cast<const SoShadowDirectionalLight *>(node)->shadowMapScene.getValue();
    const SoShadowGroup * group = p->activeShadowGroupNodes.back();
    if (scene) {
      snapshot.customSceneNodeId = scene->getNodeId();
      snapshot.customSceneDirectShape = scene->isOfType(SoShape::getClassTypeId());
      bool directChild = false;
      for (int i = 0; i < group->getNumChildren(); ++i)
        directChild = directChild || group->getChild(i) == scene;
      snapshot.customSceneDirectShape = snapshot.customSceneDirectShape && directChild;
      if (directChild && !snapshot.customSceneDirectShape) {
        bool onlyLightsBefore = true;
        for (int i = 0; i < group->getNumChildren(); ++i) {
          const SoNode * child = group->getChild(i);
          if (child == scene) break;
          onlyLightsBefore = onlyLightsBefore &&
            child->isOfType(SoLight::getClassTypeId());
        }
        snapshot.customSceneDirectSubtree = onlyLightsBefore &&
          coin_render_shadow_scene_shapes(scene, snapshot.customSceneShapeNodeIds);
      }
    }
  }
  p->builder.recordShadowLight(snapshot);
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
