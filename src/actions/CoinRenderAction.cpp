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
#include "rendering/coinrender/CoinRenderTextureAlphaCore.h"
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
#include <Inventor/nodes/SoRotation.h>
#include <Inventor/nodes/SoScale.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/fields/SoField.h>
#include <Inventor/fields/SoFieldData.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/SoPath.h>
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
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoClipPlaneElement.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/elements/SoOverrideElement.h>
#include <Inventor/elements/SoDepthBufferElement.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include "rendering/coinrender/CoinRenderDepthPolicyElement.h"
#include <algorithm>
#include <chrono>
#include <memory>
#include <unordered_set>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <cmath>
#include <Inventor/misc/SoState.h>
#include <Inventor/misc/SoChildList.h>

#include "actions/CoinRenderActionP.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinrender/CoinRenderRttExecution.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderSelectionCore.h"
#include "actions/SoSubActionP.h"

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
  return CoinRenderTargetP::isGpuBackendAvailable() ? TRUE : FALSE;
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
size_t
captureStorageEstimate(SoNode * root)
{
  if (!root) return 0;
  // Inspect at most 64 nodes through two child levels. Broad groups already
  // supply a useful hint; their descendants are never scanned here.
  struct Pending { SoNode * node; unsigned depth; };
  Pending pending[64] = {{root, 0}};
  size_t next = 0, end = 1, estimate = 0;
  while (next < end) {
    const Pending current = pending[next++];
    const SoChildList * children = current.node->getChildren();
    if (!children) continue;
    const int count = children->getLength();
    estimate = std::max(estimate, std::min(size_t(count), size_t(65536)));
    if (current.depth == 2 || count >= 256) continue;
    for (int i = 0; i < count && end < 64; ++i)
      pending[end++] = {(*children)[i], current.depth + 1};
  }
  return estimate;
}

// Exact types only: custom subclasses and view-dependent traversal fall back.
bool
cameraStableScene(SoNode * root, SoCamera * camera, bool translationProfile = false,
                  std::unordered_map<const SoNode *, size_t> * materialVisits = NULL)
{
  std::vector<SoNode *> pending(1, root);
  // Node types are fixed for this qualification. Shared leaves need their
  // fields checked once; groups still expand on every occurrence so a reused
  // camera cannot disappear behind a cached ancestor.
  const SoType separatorType = SoSeparator::getClassTypeId();
  const SoType groupType = SoGroup::getClassTypeId();
  const SoType leafTypes[] = {
    SoCube::getClassTypeId(), SoTransform::getClassTypeId(), SoMaterial::getClassTypeId(),
    SoTranslation::getClassTypeId(), SoLightModel::getClassTypeId(),
    SoDirectionalLight::getClassTypeId(), SoEnvironment::getClassTypeId(),
    SoCoordinate3::getClassTypeId(), SoNormal::getClassTypeId(),
    SoNormalBinding::getClassTypeId(), SoMaterialBinding::getClassTypeId(),
    SoShapeHints::getClassTypeId(), SoPointLight::getClassTypeId(), SoSpotLight::getClassTypeId(),
    SoIndexedFaceSet::getClassTypeId(), SoIndexedLineSet::getClassTypeId()
  };
  const SoType * leafEnd = leafTypes + sizeof(leafTypes) / sizeof(leafTypes[0]);
  std::unordered_set<SoNode *> qualifiedSharedLeaves;
  unsigned cameraOccurrences = 0;
  size_t materialOccurrences = 0;
  while (!pending.empty()) {
    SoNode * node = pending.back();
    pending.pop_back();
    // Count every syntactic material occurrence before shared-leaf dedup.
    // Held references do not represent traversal through a different scope.
    if (materialVisits && node->getTypeId() == SoMaterial::getClassTypeId()) {
      if (++materialOccurrences > 65536) return false;
      ++(*materialVisits)[node];
    }
    if (node == camera) {
      if (++cameraOccurrences != 1) return false;
      if (!translationProfile) continue;
    }
    const bool shared = node->getRefCount() > 1;
    if (shared && qualifiedSharedLeaves.find(node) != qualifiedSharedLeaves.end()) continue;
    const SoType type = node->getTypeId();
    const bool isGroup = type == separatorType || type == groupType;
    if (node != camera && !isGroup && std::find(leafTypes, leafEnd, type) == leafEnd) return false;
    // Connected scene fields may change as a consequence of camera fields.
    // Qualify their absence once, outside all subsequent overlay updates.
    const SoFieldData * fields = node->getFieldData();
    if (fields) {
      const int fieldCount = fields->getNumFields();
      for (int i = 0; i < fieldCount; ++i)
        if (fields->getField(node, i)->isConnected() ||
            (translationProfile && fields->getField(node, i)->isIgnored())) return false;
    }
    if (isGroup) {
      SoGroup * group = static_cast<SoGroup *>(node);
      for (int i = 0; i < group->getNumChildren(); ++i) {
        pending.push_back(group->getChild(i));
      }
    } else if (shared) qualifiedSharedLeaves.insert(node);
  }
  return cameraOccurrences == 1;
}
bool translationFieldsStable(const SoNode *);
bool objectOverlayEnabled(const char *);
}

void
CoinRenderActionP::cameraSensorCB(void * data, SoSensor * sensor)
{
  CoinRenderActionP * self = static_cast<CoinRenderActionP *>(data);
  SoNodeSensor * nodeSensor = static_cast<SoNodeSensor *>(sensor);
  SoNode * trigger = nodeSensor->getTriggerNode();
  SoField * field = nodeSensor->getTriggerField();
  SoSFVec3f * position = NULL;
  if (trigger && trigger->getTypeId() == SoTranslation::getClassTypeId())
    position = &static_cast<SoTranslation *>(trigger)->translation;
  else if (trigger && trigger->getTypeId() == SoTransform::getClassTypeId())
    position = &static_cast<SoTransform *>(trigger)->translation;
  if (position && field == position && field->getContainer() == trigger &&
      nodeSensor->getTriggerOperationType() == SoNotRec::FIELD_UPDATE) {
    self->translationInputDirty = true;
    const auto found = self->translationByNode.find(trigger);
    if (!position->isConnected() && !position->isIgnored() && self->translationProofValid && !self->translationInvalidated &&
        !self->cameraOnlyDirty && found != self->translationByNode.end())
      self->translationDirty.insert(found->second);
    else self->translationInvalidated = true;
  } else if (trigger && field && field->getContainer() == trigger &&
             nodeSensor->getTriggerOperationType() == SoNotRec::FIELD_UPDATE &&
             trigger->getTypeId() == SoMaterial::getClassTypeId()) {
    auto * material = static_cast<SoMaterial *>(trigger);
    const bool knownField = field == &material->ambientColor || field == &material->diffuseColor ||
      field == &material->specularColor || field == &material->emissiveColor || field == &material->shininess;
    if (objectOverlayEnabled("COIN_RENDER_DISABLE_MATERIAL_OVERLAY")) self->translationInputDirty = true;
    // Validate all fields once per dirty source during overlay preparation.
    if (knownField && self->translationProofValid &&
        !self->translationInvalidated && !self->cameraOnlyDirty && self->materialByNode.count(trigger))
      self->materialDirty.insert(trigger);
    else self->translationInvalidated = true;
  } else if (trigger && field && field->getContainer() == trigger &&
             nodeSensor->getTriggerOperationType() == SoNotRec::FIELD_UPDATE &&
             trigger->getTypeId() == SoCube::getClassTypeId()) {
    auto * cube = static_cast<SoCube *>(trigger);
    if (objectOverlayEnabled("COIN_RENDER_DISABLE_CUBE_OVERLAY")) self->translationInputDirty = true;
    if ((field == &cube->width || field == &cube->height || field == &cube->depth) &&
        self->translationProofValid && !self->translationInvalidated &&
        !self->cameraOnlyDirty && self->cubeByNode.count(trigger)) self->geometryDirty.insert(trigger);
    else self->translationInvalidated = true;
  } else self->translationInvalidated = true;
  if (self->candidateCamera &&
      nodeSensor->getTriggerNode() == self->candidateCamera &&
      nodeSensor->getTriggerField() &&
      nodeSensor->getTriggerField()->getContainer() == self->candidateCamera &&
      nodeSensor->getTriggerOperationType() == SoNotRec::FIELD_UPDATE) {
    self->cameraOnlyDirty = true;
  } else {
    self->cameraPatchInvalidated = true;
  }
}

void
CoinRenderActionP::rememberFrameRoot(SoNode * root, bool qualifyCamera)
{
  struct CaptureScope { bool & capturing; ~CaptureScope() { capturing = false; } } scope{this->capturingTranslations};
  this->cameraSensor.detach();
  this->candidateCamera = NULL;
  this->cachedCamera = NULL;
  this->cameraOverlayBasis = CoinRenderCameraOverlayBasis();
  this->cameraOnlyDirty = false;
  this->cameraPatchInvalidated = false;
  this->cameraRecaptureRequired = false;
  this->translationInputDirty = false;
  this->translationInvalidated = false;
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
  // Object animation must not pay for a scene-wide camera qualification after
  // every capture. Keep only this cheap candidate/sensor until the camera moves.
  this->candidateCamera = camera;
  this->cameraSensor.attach(root);
  if (qualifyCamera && cameraStableScene(root, camera) &&
      CoinRenderFrameReuseCore::prepareCameraOverlayBasis(
        this->lastValidPlan, this->cameraOverlayBasis)) this->cachedCamera = camera;
  this->qualifyTranslationCapture(root);
}

namespace {
bool translationFieldsStable(const SoNode * node)
{
  const SoFieldData * fields = node->getFieldData();
  for (int i = 0; fields && i < fields->getNumFields(); ++i) {
    const SoField * field = fields->getField(node, i);
    if (field->isConnected() || field->isIgnored()) return false;
  }
  return true;
}
bool objectOverlayEnabled(const char * name) {
  const char * value = std::getenv(name);
  return !(value && std::strcmp(value, "1") == 0);
}
bool opaqueMaterialSnapshot(const SoMaterial * node, CoinRenderMaterialSnapshot & result) {
  if (node->getNodeType() != SoNode::INVENTOR || node->isOverride() || !translationFieldsStable(node) ||
      node->ambientColor.getNum() != 1 || node->diffuseColor.getNum() != 1 ||
      node->specularColor.getNum() != 1 || node->emissiveColor.getNum() != 1 || node->shininess.getNum() != 1 ||
      node->transparency.getNum() != 1 || node->transparency[0] != 0) return false;
  CoinRenderMaterialSnapshot candidate;
  const SbColor colors[] = {node->ambientColor[0], node->diffuseColor[0], node->specularColor[0], node->emissiveColor[0]};
  float * output[] = {candidate.ambient, candidate.diffuse, candidate.specular, candidate.emission};
  for (int color = 0; color < 4; ++color) for (int axis = 0; axis < 3; ++axis) {
    const float value = colors[color][axis];
    if (!std::isfinite(value) || value < 0 || value > 1) return false;
    output[color][axis] = value;
  }
  candidate.shininess = node->shininess[0];
  if (!std::isfinite(candidate.shininess) || candidate.shininess < 0 || candidate.shininess > 1) return false;
  result = candidate;
  return true;
}
bool cubeDimensions(const SoCube * node, SbVec3f & result) {
  const SbVec3f value(node->width.getValue(), node->height.getValue(), node->depth.getValue());
  for (int axis = 0; axis < 3; ++axis)
    if (!std::isfinite(value[axis]) || value[axis] < .0001f || value[axis] > 32768) return false;
  result = value;
  return true;
}
}

void CoinRenderActionP::clearTranslationProof()
{
  this->translationProofValid = false;
  this->translationProofRevision = this->translationProofGeneration = 0;
  this->translationBindings.clear();
  this->translationByNode.clear();
  this->translationDirty.clear();
  this->materialDirty.clear(); this->geometryDirty.clear();
  this->materialByNode.clear(); this->cubeByNode.clear();
  if (this->translationGeneration != std::numeric_limits<uint64_t>::max())
    ++this->translationGeneration;
}

void CoinRenderActionP::beginTranslationCapture(bool enabled)
{
  this->clearTranslationProof();
  this->translationCapture.clear();
  this->translationCaptureByNode.clear();
  this->capturedDrawSources.clear(); this->pendingCaptureShape = NULL;
  this->translationShapeCandidate = SIZE_MAX;
  const char * disabled = std::getenv("COIN_RENDER_DISABLE_TRANSLATION_OVERLAY");
  this->capturingTranslations = enabled && this->translationGeneration != std::numeric_limits<uint64_t>::max() &&
    !(disabled && std::strcmp(disabled, "1") == 0);
  this->translationCaptureInvalid = false;
}

SoCallbackAction::Response CoinRenderActionP::translationPreCB(
  void * data, SoCallbackAction * action, const SoNode * node)
{
  auto * self = static_cast<CoinRenderActionP *>(data);
  if (!self->capturingTranslations || self->translationCaptureInvalid) return SoCallbackAction::CONTINUE;
  const SoType type = node->getTypeId();
  if (type != SoTransform::getClassTypeId() && type != SoTranslation::getClassTypeId())
    return SoCallbackAction::CONTINUE;
  const SoPath * path = action->getCurPath();
  if (path->getLength() < 2) return SoCallbackAction::CONTINUE;
  SoNode * parentNode = path->getNodeFromTail(1);
  if (parentNode->getTypeId() != SoSeparator::getClassTypeId()) return SoCallbackAction::CONTINUE;
  auto * parent = static_cast<SoSeparator *>(parentNode);
  if (parent->getNumChildren() != 3 || parent->getRefCount() != 1 || node->getRefCount() != 1 ||
      parent->getChild(2)->getTypeId() != SoCube::getClassTypeId() ||
      !translationFieldsStable(parent) || !translationFieldsStable(node) ||
      !translationFieldsStable(parent->getChild(2))) return SoCallbackAction::CONTINUE;
  SoNode * material = parent->getChild(0) == node ? parent->getChild(1) : parent->getChild(0);
  if ((parent->getChild(0) != node && parent->getChild(1) != node) ||
      material->getTypeId() != SoMaterial::getClassTypeId() || !translationFieldsStable(material) ||
      !self->master->hasSingleShapeCallbacks(SoCube::getClassTypeId())) return SoCallbackAction::CONTINUE;
  if (self->translationCapture.size() == 65536 || self->translationCaptureByNode.count(node)) {
    self->translationCaptureInvalid = true;
    return SoCallbackAction::CONTINUE;
  }
  TranslationBinding binding;
  binding.transform = const_cast<SoNode *>(node);
  binding.parent = parent;
  binding.cube = parent->getChild(2);
  binding.material = static_cast<SoMaterial *>(material);
  const auto * cube = static_cast<const SoCube *>(binding.cube);
  binding.cubeDimensions.setValue(cube->width.getValue(), cube->height.getValue(), cube->depth.getValue());
  if (type == SoTransform::getClassTypeId()) {
    auto * transform = static_cast<SoTransform *>(binding.transform);
    if (transform->center.getValue() != SbVec3f(0,0,0)) return SoCallbackAction::CONTINUE;
    binding.field = &transform->translation;
  } else binding.field = &static_cast<SoTranslation *>(binding.transform)->translation;
  binding.originalPosition = binding.field->getValue();
  binding.prefix = action->getModelMatrix();
  self->translationCaptureByNode[node] = self->translationCapture.size();
  self->translationCapture.push_back(binding);
  return SoCallbackAction::CONTINUE;
}

void CoinRenderActionP::beginTranslationShape(SoCallbackAction * action, const SoNode * node)
{
  this->translationShapeCandidate = SIZE_MAX;
  if (this->capturingTranslations && !this->translationCaptureInvalid) {
    this->pendingCaptureShape = node;
    this->firstCaptureDraw = this->builder.capturedDrawCount();
  }
  if (!this->capturingTranslations || this->translationCaptureInvalid ||
      node->getTypeId() != SoCube::getClassTypeId()) return;
  const SoPath * path = action->getCurPath();
  if (path->getLength() < 2) return;
  SoNode * parent = path->getNodeFromTail(1);
  if (parent->getTypeId() != SoSeparator::getClassTypeId()) return;
  auto * group = static_cast<SoSeparator *>(parent);
  if (group->getNumChildren() != 3 || group->getChild(2) != node) return;
  for (int child = 0; child < 2; ++child) {
    const auto found = this->translationCaptureByNode.find(group->getChild(child));
    if (found == this->translationCaptureByNode.end()) continue;
    auto & binding = this->translationCapture[found->second];
    if (binding.parent != parent || binding.cube != node) continue;
    this->translationShapeCandidate = found->second;
    binding.firstDraw = this->builder.capturedDrawCount();
    SoState * state = action->getState();
    binding.overallMaterial = SoMaterialBindingElement::get(state) == SoMaterialBindingElement::OVERALL &&
      !binding.material->isOverride() && !SoOverrideElement::getAmbientColorOverride(state) &&
      !SoOverrideElement::getDiffuseColorOverride(state) && !SoOverrideElement::getSpecularColorOverride(state) &&
      !SoOverrideElement::getEmissiveColorOverride(state) && !SoOverrideElement::getShininessOverride(state) &&
      !SoOverrideElement::getTransparencyOverride(state);
    return;
  }
}

void CoinRenderActionP::endTranslationShape()
{
  if (this->pendingCaptureShape) {
    const size_t count = this->builder.capturedDrawCount();
    if (count > 65536) this->translationCaptureInvalid = true;
    else {
      this->capturedDrawSources.resize(count, NULL);
      for (size_t draw = this->firstCaptureDraw; draw < count; ++draw)
        this->capturedDrawSources[draw] = this->pendingCaptureShape;
    }
    this->pendingCaptureShape = NULL;
  }
  if (this->translationShapeCandidate != SIZE_MAX) {
    auto & binding = this->translationCapture[this->translationShapeCandidate];
    binding.endDraw = this->builder.capturedDrawCount();
    if (binding.endDraw == binding.firstDraw + 1)
      binding.geometry = this->builder.capturedDraw(binding.firstDraw)->geometry;
    this->translationShapeCandidate = SIZE_MAX;
  }
}

void CoinRenderActionP::qualifyTranslationCapture(SoNode * root)
{
  struct CaptureScope { bool & capturing; ~CaptureScope() { capturing = false; } } scope{this->capturingTranslations};
  std::unordered_map<const SoNode *, size_t> materialVisits;
  if (!this->capturingTranslations || this->translationCaptureInvalid || this->translationCapture.empty() ||
      !root || !this->candidateCamera || this->lastValidPlan.renderStates.size() > 65536 ||
      this->lastValidPlan.draws.size() > 65536 ||
      !cameraStableScene(root, this->candidateCamera, true, &materialVisits)) return;
  CoinRenderCameraOverlayBasis profile;
  if (!CoinRenderFrameReuseCore::prepareCameraOverlayBasis(this->lastValidPlan, profile)) return;
  const auto & plan = this->lastValidPlan;
  for (const auto & state : plan.renderStates) {
    if (state.materialSlot >= plan.materials.size() || state.hasTexture ||
        state.transparentMaterial || state.transparentTexture || state.screenDoorTransparency > 0 ||
        !state.clipPlanesWorld.empty() || state.fogMode != CoinRenderFogMode::NONE ||
        state.polygonOffsetEnabled || state.polygonLinePattern || state.shadowGroupSlot ||
        plan.materials[state.materialSlot].transparency != 0 || plan.materials[state.materialSlot].diffuse[3] < 1) return;
    for (const auto & texture : state.extraTextures) if (texture.enabled) return;
  }
  for (const auto & draw : plan.draws)
    if (draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST || draw.renderLayer ||
        draw.clearDepthBefore || draw.shadowLightSlot || draw.lineStripId) return;
  std::vector<size_t> owners(plan.renderStates.size(), SIZE_MAX);
  std::vector<size_t> drawOwners(plan.draws.size(), SIZE_MAX);
  for (size_t object = 0; object < this->translationCapture.size(); ++object) {
    auto & binding = this->translationCapture[object];
    // One native Cube packet and one state make ownership explicit, including
    // aliases produced by model/state interning. Shared Cube geometry is fine.
    if (binding.endDraw != binding.firstDraw + 1 || binding.endDraw > plan.draws.size() ||
        binding.field->getValue() != binding.originalPosition) return;
    const auto & draw = plan.draws[binding.firstDraw];
    // A later occurrence may have been merged into this packet. Its captured
    // range must still describe exactly this Cube, before assigning ownership.
    if (draw.geometry.firstVertex != binding.geometry.firstVertex || draw.geometry.vertexCount != binding.geometry.vertexCount ||
        draw.geometry.firstIndex != binding.geometry.firstIndex || draw.geometry.indexCount != binding.geometry.indexCount ||
        draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST ||
        draw.renderLayer || draw.clearDepthBefore || draw.shadowLightSlot || draw.lineStripId ||
        draw.renderStateSlot >= plan.renderStates.size() || owners[draw.renderStateSlot] != SIZE_MAX) return;
    binding.stateSlot = draw.renderStateSlot;
    binding.anchor = plan.renderStates[binding.stateSlot].model;
    SbMatrix qualified;
    if (!CoinRenderFrameReuseCore::translatedModel(binding.anchor, binding.prefix, binding.originalPosition, qualified) ||
        std::memcmp(qualified.getValue(), binding.anchor.getValue(), sizeof(float) * 16) != 0) return;
    if (draw.hasSortingCenter) {
      SbVec3f center;
      binding.anchor.multVecMatrix(SbVec3f(0, 0, 0), center);
      if (std::memcmp(center.getValue(), draw.sortingCenterWorld, sizeof(float) * 3) != 0) return;
    }
    owners[binding.stateSlot] = object;
    drawOwners[binding.firstDraw] = object;
  }
  for (size_t draw = 0; draw < plan.draws.size(); ++draw)
    if (owners[plan.draws[draw].renderStateSlot] != SIZE_MAX &&
        owners[plan.draws[draw].renderStateSlot] != drawOwners[draw]) return;
  this->translationBindings.swap(this->translationCapture);
  this->translationByNode.swap(this->translationCaptureByNode);
  this->translationDirty.clear();
  this->translationProofGeneration = this->translationGeneration;
  this->translationProofRevision = plan.revision;
  this->translationProofValid = true;
  this->qualifyObjectPayloads(materialVisits);
}

void CoinRenderActionP::qualifyObjectPayloads(const std::unordered_map<const SoNode *, size_t> & materialVisits)
{
  const auto & plan = this->lastValidPlan;
  if (plan.materials.size() > 65536 || this->capturedDrawSources.size() != plan.draws.size()) return;
  std::vector<size_t> drawOwners(plan.draws.size(), SIZE_MAX);
  for (size_t i = 0; i < this->translationBindings.size(); ++i)
    drawOwners[this->translationBindings[i].firstDraw] = i;
  std::vector<const SoNode *> materialOwners(plan.materials.size(), NULL);
  std::vector<bool> materialConflicts(plan.materials.size(), false);
  for (size_t draw = 0; draw < plan.draws.size(); ++draw) {
    const auto & packet = plan.draws[draw];
    const uint32_t slot = plan.renderStates[packet.renderStateSlot].materialSlot;
    const SoNode * owner = drawOwners[draw] == SIZE_MAX ? NULL : this->translationBindings[drawOwners[draw]].material;
    if (!owner) materialConflicts[slot] = true;
    else if (!materialOwners[slot]) materialOwners[slot] = owner;
    else if (materialOwners[slot] != owner) materialConflicts[slot] = true;
    // OVERALL must actually be uniform across every vertex consumed by this draw.
    for (uint32_t index = 0; index < packet.geometry.indexCount; ++index) {
      const uint32_t material = plan.vertices[plan.indices[packet.geometry.firstIndex + index]].materialSlot;
      if (material != slot) { materialConflicts[slot] = true; materialConflicts[material] = true; }
    }
  }
  struct Range { uint64_t first, end; size_t draw; const SoNode * cube; };
  std::vector<Range> ranges;
  ranges.reserve(plan.draws.size());
  for (size_t draw = 0; draw < plan.draws.size(); ++draw) {
    const auto & range = plan.draws[draw].geometry;
    ranges.push_back({range.firstVertex, uint64_t(range.firstVertex) + range.vertexCount, draw,
      drawOwners[draw] == SIZE_MAX ? NULL : this->translationBindings[drawOwners[draw]].cube});
  }
  for (auto & binding : this->translationBindings) {
    const auto & state = plan.renderStates[binding.stateSlot];
    CoinRenderMaterialSnapshot material;
    binding.materialEligible = binding.overallMaterial && !materialConflicts[state.materialSlot] &&
      materialOwners[state.materialSlot] == binding.material &&
      opaqueMaterialSnapshot(binding.material, material) &&
      std::memcmp(&material, &plan.materials[state.materialSlot], sizeof(material)) == 0;
    SbVec3f dimensions;
    binding.geometryEligible = binding.overallMaterial && binding.geometry.vertexCount == 24 &&
      binding.geometry.indexCount == 36 && cubeDimensions(static_cast<const SoCube *>(binding.cube), dimensions) &&
      dimensions == binding.cubeDimensions;
    if (binding.geometryEligible) {
      for (uint32_t offset = 0; offset < binding.geometry.vertexCount; ++offset) {
        const auto & vertex = plan.vertices[binding.geometry.firstVertex + offset];
        for (int axis = 0; axis < 3; ++axis)
          if (std::abs(vertex.position[axis]) != dimensions[axis] * .5f) binding.geometryEligible = false;
        if (vertex.screenSpaceW != 1 || vertex.fogEyeDepth != -1) binding.geometryEligible = false;
      }
    }
  }
  std::sort(ranges.begin(), ranges.end(), [](const Range & a, const Range & b) { return a.first < b.first; });
  for (size_t begin = 0; begin < ranges.size();) {
    size_t end = begin + 1; uint64_t maximum = ranges[begin].end;
    const SoNode * owner = ranges[begin].cube; bool overlapConflict = !owner;
    while (end < ranges.size() && ranges[end].first < maximum) {
      if (ranges[end].cube != owner || ranges[end].first != ranges[begin].first ||
          ranges[end].end != ranges[begin].end) overlapConflict = true;
      maximum = std::max(maximum, ranges[end].end); ++end;
    }
    if (overlapConflict) for (size_t i = begin; i < end; ++i)
      if (drawOwners[ranges[i].draw] != SIZE_MAX) this->translationBindings[drawOwners[ranges[i].draw]].geometryEligible = false;
    begin = end;
  }
  // A source is admitted only if all of its captured occurrences are admitted.
  std::unordered_set<const SoNode *> rejectedMaterials, rejectedCubes;
  std::unordered_set<const SoNode *> capturedCubes;
  std::unordered_map<const SoNode *, size_t> materialOccurrences;
  for (const auto & binding : this->translationBindings) {
    capturedCubes.insert(binding.cube);
    ++materialOccurrences[binding.material];
  }
  for (size_t draw = 0; draw < plan.draws.size(); ++draw) if (drawOwners[draw] == SIZE_MAX) {
    if (capturedCubes.count(this->capturedDrawSources[draw])) rejectedCubes.insert(this->capturedDrawSources[draw]);
  }
  for (const auto & binding : this->translationBindings) {
    // Unmapped syntax can use this material under partial overrides, creating
    // a different table slot which still depends on some fields of this source.
    // Require every occurrence in this root to belong to an isolated object.
    const auto visits = materialVisits.find(binding.material);
    if (!binding.materialEligible || visits == materialVisits.end() || visits->second != materialOccurrences[binding.material])
      rejectedMaterials.insert(binding.material);
    if (!binding.geometryEligible) rejectedCubes.insert(binding.cube);
  }
  for (size_t i = 0; i < this->translationBindings.size(); ++i) {
    const auto & binding = this->translationBindings[i];
    if (!rejectedMaterials.count(binding.material) && objectOverlayEnabled("COIN_RENDER_DISABLE_MATERIAL_OVERLAY"))
      this->materialByNode[binding.material].push_back(i);
    if (!rejectedCubes.count(binding.cube) && objectOverlayEnabled("COIN_RENDER_DISABLE_CUBE_OVERLAY"))
      this->cubeByNode[binding.cube].push_back(i);
  }
  // Conservative accounting includes source-map nodes/buckets and occurrence
  // indices. The existing transform proof has its independent 65,536 cap.
  const size_t metadataBytes = this->translationBindings.size() * 80 +
    (this->materialByNode.size() + this->cubeByNode.size()) * 128;
  if (metadataBytes > 16 * 1024 * 1024) { this->materialByNode.clear(); this->cubeByNode.clear(); }
}

bool CoinRenderActionP::prepareTranslationOverlay(SoNode * root, CoinRenderObjectOverlayUndo & undo)
{
  const char * disabled = std::getenv("COIN_RENDER_DISABLE_TRANSLATION_OVERLAY");
  if ((disabled && std::strcmp(disabled, "1") == 0) || !this->translationProofValid ||
      this->translationInvalidated || !this->translationInputDirty ||
      (this->translationDirty.empty() && this->materialDirty.empty() && this->geometryDirty.empty()) ||
      this->cameraOnlyDirty || root != this->cachedRoot || this->cameraSensor.getAttachedNode() != root ||
      this->translationProofGeneration != this->translationGeneration || !this->translationProofRevision) return false;
  std::vector<CoinRenderModelUpdate> updates;
  updates.reserve(this->translationDirty.size());
  for (size_t index : this->translationDirty) {
    if (index >= this->translationBindings.size()) return false;
    const auto & binding = this->translationBindings[index];
    CoinRenderModelUpdate update;
    update.stateSlot = binding.stateSlot;
    if (this->lastValidPlan.draws[binding.firstDraw].hasSortingCenter)
      update.sortingDrawSlot = static_cast<uint32_t>(binding.firstDraw);
    if (binding.field->isConnected() || binding.field->isIgnored() ||
        !CoinRenderFrameReuseCore::translatedModel(binding.anchor, binding.prefix, binding.field->getValue(), update.model)) return false;
    updates.push_back(update);
  }
  std::vector<CoinRenderMaterialUpdate> materials;
  std::unordered_set<uint32_t> materialSlots;
  if (!this->materialDirty.empty() && !objectOverlayEnabled("COIN_RENDER_DISABLE_MATERIAL_OVERLAY")) return false;
  for (const SoNode * node : this->materialDirty) {
    const auto found = this->materialByNode.find(node);
    CoinRenderMaterialSnapshot snapshot;
    if (found == this->materialByNode.end() || !opaqueMaterialSnapshot(static_cast<const SoMaterial *>(node), snapshot)) return false;
    for (size_t object : found->second) {
      const uint32_t slot = this->lastValidPlan.renderStates[this->translationBindings[object].stateSlot].materialSlot;
      if (materialSlots.insert(slot).second) materials.push_back({slot, snapshot});
    }
  }
  std::vector<CoinRenderPositionUpdate> positions;
  std::vector<CoinRenderDrawSourceUpdate> draws;
  // Admitted overlapping ranges are exact aliases of the same Cube source.
  // Deduplicate 24-vertex ranges, avoiding a hash allocation for every vertex.
  std::unordered_set<uint32_t> vertexRanges;
  if (!this->geometryDirty.empty() && !objectOverlayEnabled("COIN_RENDER_DISABLE_CUBE_OVERLAY")) return false;
  for (const SoNode * node : this->geometryDirty) {
    const auto found = this->cubeByNode.find(node); SbVec3f dimensions;
    if (found == this->cubeByNode.end() || !translationFieldsStable(node) ||
        !cubeDimensions(static_cast<const SoCube *>(node), dimensions)) return false;
    for (size_t object : found->second) {
      const auto & binding = this->translationBindings[object];
      if (binding.material->isOverride() || binding.material->getNodeType() != SoNode::INVENTOR) return false;
      draws.push_back({static_cast<uint32_t>(binding.firstDraw), node->getNodeId()});
      if (!vertexRanges.insert(binding.geometry.firstVertex).second) continue;
      for (uint32_t offset = 0; offset < binding.geometry.vertexCount; ++offset) {
        const uint32_t slot = binding.geometry.firstVertex + offset;
        if (positions.size() == COIN_RENDER_OBJECT_OVERLAY_MAX_POSITIONS) return false;
        const auto & previous = this->lastValidPlan.vertices[slot];
        SbVec3f position;
        for (int axis = 0; axis < 3; ++axis) position[axis] = std::copysign(dimensions[axis] * .5f, previous.position[axis]);
        positions.push_back({slot, position});
      }
    }
  }
  return CoinRenderFrameReuseCore::beginObjectOverlay(this->lastValidPlan, updates, materials, positions, draws,
    CoinRenderFramePlanBuilder::nextRevision(), undo);
}

void CoinRenderActionP::commitTranslationOverlay()
{
  this->translationDirty.clear();
  this->materialDirty.clear(); this->geometryDirty.clear();
  this->translationInputDirty = false;
  this->translationInvalidated = false;
  this->cameraPatchInvalidated = false;
  this->cachedCamera = NULL;
  this->cameraOverlayBasis = CoinRenderCameraOverlayBasis();
  this->cameraRecaptureRequired = true;
}

bool
CoinRenderActionP::prepareCameraOverlay(SoNode * root,
                                         CoinRenderCameraOverlayUndo & undo)
{
  const char * disabled = std::getenv("COIN_RENDER_DISABLE_CAMERA_OVERLAY");
  if ((disabled && std::strcmp(disabled, "1") == 0) ||
      !root || root != this->cachedRoot || !this->candidateCamera || this->cameraRecaptureRequired ||
      this->cameraSensor.getAttachedNode() != root ||
      !this->cameraOnlyDirty || this->cameraPatchInvalidated ||
      static_cast<SoGroup *>(root)->getChild(0) != this->candidateCamera) return false;

  SbViewportRegion adjusted;
  const SbViewVolume vv = this->candidateCamera->getViewVolume(
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
  snapshot.focalDistance = this->candidateCamera->focalDistance.getValue();
  snapshot.aspectRatio = adjusted.getViewportAspectRatio();
  if (this->cameraPatchInvalidated ||
      this->cameraSensor.getAttachedNode() != root) return false;
  if (!this->cachedCamera) {
    CoinRenderCameraOverlayBasis basis;
    if (!CoinRenderFrameReuseCore::prepareCameraOverlayBasis(this->lastValidPlan, basis) ||
        !cameraStableScene(root, this->candidateCamera) || this->cameraPatchInvalidated ||
        this->cameraSensor.getAttachedNode() != root) return false;
    this->cameraOverlayBasis = std::move(basis);
    this->cachedCamera = this->candidateCamera;
  }
  return CoinRenderFrameReuseCore::beginCameraOverlay(
    this->lastValidPlan, snapshot,
    CoinRenderFramePlanBuilder::nextRevision(), undo, this->cameraOverlayBasis);
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
  // External callbacks can change capture or have observable side effects.
  // Registration after construction permanently requires their traversal.
  bool planCacheAllowed = this->master->callbackRegistrationRevision() == this->captureCallbackRevision;

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
  const bool qualifyCapturedCamera = !this->hasLastValidPlan || this->cachedRoot != cacheRoot;
  const bool traversalSkipped = planCacheAllowed && cacheRoot && this->hasLastValidPlan &&
    this->cachedRoot == cacheRoot && this->cachedRootId == cacheRoot->getNodeId() &&
    !this->cameraOnlyDirty && !this->cameraPatchInvalidated && !this->translationInputDirty;
  CoinRenderFrameReuseDecision reuseDecision = traversalSkipped
    ? CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::REUSE,
                               this->lastValidPlan.revision)
    : CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::UNKNOWN, 0);
  this->sceneTexturePixels.clear();
  this->shadowSceneCaptures.clear();
  this->shadowStyleBeforeGroups.clear();
  this->activeShadowGroupNodes.clear();
  this->lastRejectedShadowFrame = CoinRenderFramePlan();
  this->lastRejectedShadowPlan = CoinRenderShadowPlan();
  CoinRenderFramePlan plan;
  CoinRenderFramePreflight capturedPreflight;
  CoinRenderCameraOverlayUndo overlayUndo;
  CoinRenderObjectOverlayUndo translationUndo;
  struct CameraOverlayScope {
    CoinRenderFramePlan & frame;
    CoinRenderCameraOverlayUndo & undo;
    CoinRenderObjectOverlayUndo & translationUndo;
    bool committed;
    ~CameraOverlayScope() {
      if (!committed) {
        CoinRenderFrameReuseCore::rollbackCameraOverlay(frame, undo);
        CoinRenderFrameReuseCore::rollbackObjectOverlay(frame, translationUndo);
      }
    }
  } overlayScope{this->lastValidPlan, overlayUndo, translationUndo, false};
  ProfileClock::time_point profileTraversed = ProfileClock::now();
  ProfileClock::time_point profilePlanned = profileTraversed;
  const bool translationOverlay = !traversalSkipped && planCacheAllowed && !this->planOnly &&
    this->hasLastValidPlan && this->prepareTranslationOverlay(cacheRoot, translationUndo);
  const bool cameraOverlay = !translationOverlay && !traversalSkipped && planCacheAllowed &&
    !this->planOnly && this->hasLastValidPlan &&
    this->prepareCameraOverlay(cacheRoot, overlayUndo);
  if (cameraOverlay) {
    reuseDecision = CoinRenderFrameReuseDecision(
      CoinRenderFrameReuseKind::CAMERA_PATCH, overlayUndo.revision);
  }
  if (translationOverlay) {
    reuseDecision = CoinRenderFrameReuseDecision(
      CoinRenderFrameReuseKind::RESOURCE_REBUILD, translationUndo.revision);
  }
  if (!traversalSkipped && !cameraOverlay && !translationOverlay) {
    this->beginTranslationCapture(planCacheAllowed && cacheRoot &&
      (qualifyCapturedCamera || this->translationInputDirty || this->cameraRecaptureRequired));
    this->builder.beginFrame(this->backgroundColor, this->master->getViewportRegion());
    this->builder.reserveCaptureStorage(captureStorageEstimate(cacheRoot));
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
  CoinRenderTransparencyOptions capturedTransparency = this->transparencyOptions;
  capturedTransparency.mode = this->executionOptions.transparency;
  if (!traversalSkipped && !cameraOverlay && !translationOverlay &&
      !this->builder.build(plan, &err, true, &capturedTransparency,
                           this->target && !this->planOnly ? &capturedPreflight : nullptr)) {
    const CoinRenderAction::Status status = this->builder.isUnsupportedBuild()
      ? CoinRenderAction::UNSUPPORTED : CoinRenderAction::INVALID_SCENE;
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      status, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(err.c_str())));
    return;
  }
  if (!this->shadowSceneCaptures.empty()) capturedPreflight.invalidate();
  for (const auto & capture : this->shadowSceneCaptures) {
    for (size_t l = 0; l < plan.shadowLights.size(); ++l) {
      auto & light = plan.shadowLights[l];
      if (light.groupSlot != capture.groupSlot || light.sourceRevision != capture.lightRevision) continue;
      coin_render_append_shadow_scene(plan, capture.frame, static_cast<uint32_t>(l + 1),
                                      capture.inheritedClipPlaneCount);
      light.mapSceneCaptured = true;
    }
  }
  if (!this->shadowSceneCaptures.empty() && !plan.isValid(&err)) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(err.c_str())));
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
    // SoSceneTexture2 producers are captured without a target. The resolved
    // frame is later submitted by the selected staged or direct executor.
    if (this->planOnly &&
        (this->sceneTexturePlan->mode == COIN_RENDER_SCENE_TEXTURE_STAGED ||
         this->sceneTexturePlan->mode == COIN_RENDER_SCENE_TEXTURE_DIRECT)) {
      std::string profileDiagnostic;
      executableShadow = coin_render_shadow_object_profile(
        plan, shadowPlan, shadowPlan.passes.size(), profileDiagnostic);
    }
    if (this->target &&
        this->target->getPimpl()->supportsOffscreenShadows(this->asyncTicket != nullptr)) {
      std::string profileDiagnostic;
      executableShadow = coin_render_shadow_object_profile(
        plan, shadowPlan, shadowPlan.passes.size(), profileDiagnostic);
    }
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
  if (!traversalSkipped && !cameraOverlay && !translationOverlay) {
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
  const bool useCachedPlan = reusePreviousPlan || cameraOverlay || translationOverlay;
  if (useCachedPlan) capturedPreflight.invalidate();
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
    capturedPreflight.invalidate();
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
      if (cameraOverlay) {
        this->cameraOnlyDirty = false;
        this->clearTranslationProof();
      } else if (translationOverlay) this->commitTranslationOverlay();
      else if (!traversalSkipped)
        this->rememberFrameRoot(planCacheAllowed ? cacheRoot : NULL, qualifyCapturedCamera);
    }
    overlayScope.committed = true;
    this->setDiagnostic(CoinRenderDiagnosticShell::success());
    return;
  }

  // Execute frame on target
  const auto * submissionPreflight = capturedPreflight.compositionFor(executionPlan)
    ? &capturedPreflight : nullptr;
  CoinRenderFrameExecutionResult execRes =
      this->asyncTicket
          ? this->target->pimpl->executeFrameAsync(executionPlan, *this->asyncTicket, reuseDecision,
                                                  submissionPreflight)
          : this->target->pimpl->executeFrame(executionPlan, reuseDecision, submissionPreflight);
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
    sample.planCacheHit = traversalSkipped || cameraOverlay || translationOverlay;
    sample.reuseKind = reuseDecision.kind;
    std::cerr << CoinRenderDiagnosticShell::formatActionPhase(sample) << '\n';
  }
  if (!useCachedPlan) {
    capturedPreflight.invalidate();
    this->lastValidPlan = retainStagedPixels ? std::move(resolvedPlan) : std::move(plan);
    this->hasLastValidPlan = true;
    this->recordingLogValid = false;
  } else if (cameraOverlay || translationOverlay) {
    this->recordingLogValid = false;
  }
  if (cacheRoot) {
    this->cachedRoot = planCacheAllowed ? cacheRoot : NULL;
    this->cachedRootId = cacheRoot->getNodeId();
    if (cameraOverlay) {
      this->cameraOnlyDirty = false;
      this->clearTranslationProof();
    } else if (translationOverlay) this->commitTranslationOverlay();
    else if (!traversalSkipped)
      this->rememberFrameRoot(planCacheAllowed ? cacheRoot : NULL, qualifyCapturedCamera);
  }
  overlayScope.committed = true;
  this->setDiagnostic(CoinRenderDiagnosticShell::success());
}

SoCallbackAction::Response
CoinRenderActionP::textureUnitsPreCB(void * userdata, SoCallbackAction * action, const SoNode * node)
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
  p->builder.endShape();
  p->beginTranslationShape(action, node);
  if (p->master->hasSingleShapeCallbacks(node->getTypeId())) {
    p->builder.beginShape(action, node);
    if (p->fastPathEnabled && p->builder.replayNativeCube(action, const_cast<SoNode *>(node))) {
      p->endTranslationShape();
      p->builder.endShape();
      return SoCallbackAction::PRUNE;
    }
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::shapePostCB(void * userdata, SoCallbackAction *, const SoNode *)
{
  auto * self = static_cast<CoinRenderActionP *>(userdata);
  self->builder.endShape();
  self->endTranslationShape();
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
  if (node->isOfType(SoCamera::getClassTypeId())) {
    auto * p = static_cast<CoinRenderActionP *>(userdata);
    if (!p->capturingShadowScene) return SoCallbackAction::CONTINUE;
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString("shadowMapScene cannot replace the light-owned camera in this profile")));
    return SoCallbackAction::ABORT;
  }

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

  this->master->addPreCallback(SoCamera::getClassTypeId(), unsupportedEffectPreCB, this);
  this->master->addPreCallback(SoShape::getClassTypeId(), textureUnitsPreCB, this);
  this->master->addPostCallback(SoShape::getClassTypeId(), shapePostCB, this);
  this->master->addPreCallback(SoTranslation::getClassTypeId(), translationPreCB, this);
  this->master->addPreCallback(SoTransform::getClassTypeId(), translationPreCB, this);
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
  this->captureCallbackRevision = this->master->callbackRegistrationRevision();
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
      !coin_render_scene_texture_policy_supported(texture->transparencyFunction.getValue()) ||
      texture->sceneTransparencyType.getValue() != NULL) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString("SoSceneTexture2 supports only unit 0, RGBA8, MODULATE, REPEAT/CLAMP, "
                 "NONE/ALPHA_BLEND/ALPHA_TEST transparency function and no sceneTransparencyType")));
    return SoCallbackAction::ABORT;
  }

  if (SoTextureQualityElement::get(state) <= 0.0f) {
    p->sceneTexturePixels.emplace_back(4, 0);
    SoMultiTextureImageElement::set(state, const_cast<SoSceneTexture2 *>(texture), 0,
      SbVec2s(1, 1), 4, p->sceneTexturePixels.back().data(),
      SoMultiTextureImageElement::REPEAT, SoMultiTextureImageElement::REPEAT,
      SoMultiTextureImageElement::MODULATE, texture->blendColor.getValue());
    SbVec2s markerSize;
    int components;
    const auto * marker = SoMultiTextureImageElement::getImage(state, 0, markerSize, components);
    p->builder.registerSceneTexture(marker, 0, 1, 1, false,
                                    texture->transparencyFunction.getValue());
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
CoinRenderActionP::annotationPreCB(void * userdata, SoCallbackAction * action, const SoNode *)
{
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->capturingShadowScene) return SoCallbackAction::PRUNE;
  action->getState()->push();
  // Coin/GL annotations render last with depth testing disabled. They do not
  // clear or write the scene depth; an explicit child DepthBuffer can override.
  SoDepthBufferElement::set(action->getState(), FALSE, FALSE,
    SoDepthBufferElement::getFunction(action->getState()),
    SoDepthBufferElement::getRange(action->getState()));
  p->builder.beginAnnotation(false);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::annotationPostCB(void * userdata, SoCallbackAction * action, const SoNode *)
{
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  if (!p->capturingShadowScene) {
    p->builder.endAnnotation();
    action->getState()->pop();
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::shadowGroupPreCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  const auto * group = static_cast<const SoShadowGroup *>(node);
  if (p->capturingShadowScene || !group->isActive.getValue()) return SoCallbackAction::CONTINUE;
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
  snapshot.parentGroupSlot = p->builder.activeShadowGroupSlot();
  snapshot.hasEntryCamera = true;
  snapshot.entryCamera = CoinRenderFramePlanBuilder::captureCamera(action);
  snapshot.entryModel = action->getModelMatrix();
  p->shadowStyleBeforeGroups.push_back(SoShadowStyleElement::get(action->getState()));
  SoShadowStyleElement::set(action->getState(), 3);
  p->builder.beginShadowGroup(snapshot);
  p->activeShadowGroupNodes.push_back(group);
  // Coin/GL renders light-owned scenes from the group-entry state, resetting
  // lazy material, enabled textures and model matrix in SoSceneTexture2.
  // Reuse ordinary captured casters for simple groups. Composition requires
  // its own map traversal because nested group resets and annotations affect
  // the main pass differently from SHADOWMAP.
  // Coin/GL renders the whole caster scene even when apply(path) selects
  // only a receiver. Reuse the independent map capture for this traversal.
  int pathIndexCount=0;
  const int * pathIndices=nullptr;
  bool separateMap = snapshot.parentGroupSlot != 0 ||
    action->getPathCode(pathIndexCount,pathIndices) == SoAction::IN_PATH;
  SoSearchAction structure;
  structure.setType(SoAnnotation::getClassTypeId());
  structure.setInterest(SoSearchAction::FIRST);
  structure.apply(const_cast<SoShadowGroup *>(group));
  separateMap = separateMap || structure.getPath() != nullptr;
  structure.reset();
  structure.setType(SoShadowGroup::getClassTypeId());
  structure.setInterest(SoSearchAction::ALL);
  structure.apply(const_cast<SoShadowGroup *>(group));
  for (int i = 0; i < structure.getPaths().getLength(); ++i) {
    const auto * child = static_cast<const SoShadowGroup *>(structure.getPaths()[i]->getTail());
    separateMap = separateMap || (child != group && child->isActive.getValue());
  }
  SoSearchAction search;
  search.setType(SoLight::getClassTypeId());
  search.setInterest(SoSearchAction::ALL);
  search.apply(const_cast<SoShadowGroup *>(group));
  for (int i = 0; i < search.getPaths().getLength(); ++i) {
    const SoNode * light = search.getPaths()[i]->getTail();
    SoNode * scene = nullptr;
    if (light->isOfType(SoShadowSpotLight::getClassTypeId()))
      scene = static_cast<const SoShadowSpotLight *>(light)->shadowMapScene.getValue();
    else if (light->isOfType(SoShadowDirectionalLight::getClassTypeId()))
      scene = static_cast<const SoShadowDirectionalLight *>(light)->shadowMapScene.getValue();
    if (!static_cast<const SoLight *>(light)->on.getValue()) continue;
    if (!scene && separateMap && (light->isOfType(SoSpotLight::getClassTypeId()) ||
                   light->isOfType(SoShadowDirectionalLight::getClassTypeId())))
      scene = const_cast<SoShadowGroup *>(group);
    if (!scene) continue;
    if (std::any_of(p->shadowSceneCaptures.begin(), p->shadowSceneCaptures.end(),
        [&](const ShadowSceneCapture & capture) {
          return capture.groupSlot == p->builder.activeShadowGroupSlot() &&
            capture.lightRevision == light->getNodeId();
        })) continue;
    CoinRenderFramePlanBuilder savedBuilder;
    std::swap(savedBuilder, p->builder);
    p->builder.beginFrame(p->backgroundColor, p->viewport);
    auto captureGroup = snapshot;
    captureGroup.parentGroupSlot = 0;
    p->builder.beginShadowGroup(captureGroup);
    SoState * state = action->getState();
    const uint32_t inheritedPlanes = SoClipPlaneElement::getInstance(state)->getNum();
    state->push();
    SoLazyElement::setToDefault(state);
    SoMultiTextureEnabledElement::disableAll(state);
    SoLazyElement::setLightModel(state, SoLazyElement::BASE_COLOR);
    SoTextureQualityElement::set(state, 0.0f);
    SoMaterialBindingElement::set(state, nullptr, SoMaterialBindingElement::OVERALL);
    SoOverrideElement::setMaterialBindingOverride(state, nullptr, TRUE);
    SoOverrideElement::setLightModelOverride(state, nullptr, TRUE);
    SoTextureOverrideElement::setQualityOverride(state, TRUE);
    SoModelMatrixElement::set(state, const_cast<SoShadowGroup *>(group), SbMatrix::identity());
    p->capturingShadowScene = true;
    // GL flattens a ShadowGroup used as the custom scene.
    if (scene->isOfType(SoShadowGroup::getClassTypeId())) {
      const auto * sub = static_cast<const SoShadowGroup *>(scene);
      for (int child = 0; child < sub->getNumChildren(); ++child)
        action->switchToNodeTraversal(sub->getChild(child));
    } else action->switchToNodeTraversal(scene);
    p->capturingShadowScene = false;
    state->pop();
    action->setCurrentNode(const_cast<SoNode *>(node));
    ShadowSceneCapture capture;
    capture.groupSlot = savedBuilder.activeShadowGroupSlot();
    capture.lightRevision = light->getNodeId();
    capture.inheritedClipPlaneCount = inheritedPlanes;
    std::string error;
    p->builder.endShadowGroup();
    const bool ok = p->builder.build(capture.frame, &error, true);
    std::swap(savedBuilder, p->builder);
    if (!ok || action->hasTerminated()) {
      if (!action->hasTerminated()) p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(error.c_str())));
      return SoCallbackAction::ABORT;
    }
    p->shadowSceneCaptures.push_back(std::move(capture));
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::shadowGroupPostCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->capturingShadowScene || !static_cast<const SoShadowGroup *>(node)->isActive.getValue())
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

SoCallbackAction::Response
CoinRenderActionP::lightPreCB(void * userdata,
                               SoCallbackAction * action,
                               const SoNode * node)
{
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->capturingShadowScene) return SoCallbackAction::CONTINUE;
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
    if (scene) snapshot.customSceneNodeId = scene->getNodeId();
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
