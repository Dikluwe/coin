#include "actions/CoinRenderActionIncludesP.h"

#ifdef HAVE_VRML97
static bool scanVRMLSnapshot(const SoNode * root, bool & cameraIndependent)
{
  std::vector<const SoNode *> pending(1, root);
  std::unordered_set<const SoNode *> seen;
  while (!pending.empty()) {
    const SoNode * node = pending.back(); pending.pop_back();
    if (!node || !seen.insert(node).second) continue;
    if (node->getNodeType() == SoNode::VRML2) return true;
    const SoFieldData * fields = node->getFieldData();
    for (int i = 0; fields && i < fields->getNumFields(); ++i) {
      const SoField * field = fields->getField(node, i);
      // An engine/field connection can propagate a camera update into a node
      // reference, including through nodes reached only by SFNode/MFNode.
      if (field->isConnected()) cameraIndependent = false;
      if (field->isOfType(SoSFNode::getClassTypeId()))
        pending.push_back(static_cast<const SoSFNode *>(field)->getValue());
      else if (field->isOfType(SoMFNode::getClassTypeId())) {
        const auto * nodes = static_cast<const SoMFNode *>(field);
        for (int j = 0; j < nodes->getNum(); ++j) pending.push_back((*nodes)[j]);
      }
    }
    const SoChildList * children = node->getChildren();
    for (int i = 0; children && i < children->getLength(); ++i) pending.push_back((*children)[i]);
  }
  return false;
}
#endif

namespace {
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
  // Runtime SoType alone does not exclude C++ subclasses which inherit their
  // parent's registration but override traversal. Object overlays skip that
  // traversal, so every admitted occurrence must be a concrete native node.
  const std::type_info * objectTypes[] = {
    &typeid(SoCube), &typeid(SoTransform), &typeid(SoMaterial),
    &typeid(SoTranslation), &typeid(SoLightModel), &typeid(SoDirectionalLight),
    &typeid(SoEnvironment), &typeid(SoCoordinate3), &typeid(SoNormal),
    &typeid(SoNormalBinding), &typeid(SoMaterialBinding), &typeid(SoShapeHints),
    &typeid(SoPointLight), &typeid(SoSpotLight), &typeid(SoIndexedFaceSet),
    &typeid(SoIndexedLineSet), &typeid(SoSeparator), &typeid(SoGroup),
    &typeid(SoPerspectiveCamera), &typeid(SoOrthographicCamera)
  };
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
    if (translationProfile && std::none_of(std::begin(objectTypes), std::end(objectTypes),
        [node](const std::type_info * native) { return typeid(*node) == *native; })) return false;
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
CoinRenderActionP::vrmlSnapshotSensorCB(void * data, SoSensor * sensor)
{
  auto * self = static_cast<CoinRenderActionP *>(data);
  auto * observer = static_cast<SoNodeSensor *>(sensor);
  SoNode * root = observer->getAttachedNode();
  SoNode * trigger = observer->getTriggerNode();
  SoField * field = observer->getTriggerField();
  // Only exact builtin camera fields have known, non-structural semantics.
  // In particular, inherited fields on application camera subclasses, node
  // references, propagated connection updates, touch(), and group edits must
  // not carry the proof forward. Their next apply scans the graph again.
  const bool cameraField = trigger && field && field->getContainer() == trigger &&
    (typeid(*trigger) == typeid(SoPerspectiveCamera) ||
     typeid(*trigger) == typeid(SoOrthographicCamera)) &&
    !field->isOfType(SoSFNode::getClassTypeId()) &&
    !field->isOfType(SoMFNode::getClassTypeId()) &&
    observer->getTriggerOperationType() == SoNotRec::FIELD_UPDATE;
  // Reuse the existing whole-scene camera qualification, and additionally
  // exclude connections on referenced nodes which that child-list proof does
  // not inspect. Duplicate notifications can be suppressed at an ancestor,
  // so a camera origin alone is not proof against connected topology changes.
  if (self->vrmlSnapshotValid && !self->vrmlSnapshotPresent && root && cameraField &&
      self->vrmlSnapshotCameraIndependent && self->cachedCamera == trigger &&
      self->cameraSensor.getAttachedNode() == root && !self->cameraPatchInvalidated)
    self->vrmlSnapshotRootId = root->getNodeId();
  else self->vrmlSnapshotValid = false;
}

bool
CoinRenderActionP::containsVRMLSnapshot(SoNode * root)
{
#ifdef HAVE_VRML97
  if (this->vrmlSnapshotSensor.getAttachedNode() != root) {
    this->vrmlSnapshotSensor.detach();
    this->vrmlSnapshotValid = false;
    if (root) this->vrmlSnapshotSensor.attach(root);
  }
  if (!root) return false;
  const SbUniqueId revision = root->getNodeId();
  if (this->vrmlSnapshotValid && !this->vrmlSnapshotPresent &&
      this->vrmlSnapshotRootId == revision) return false;
  // Keep scanning positive scenes: VRML capture observes mutable state which
  // does not necessarily notify. Only a stable negative result is reusable.
  // Install the auditor before reading fields, since evaluating a connection
  // can itself notify. Such a scan cannot qualify cache reuse this apply.
  this->vrmlSnapshotValid = false;
  bool cameraIndependent = true;
  const bool present = scanVRMLSnapshot(root, cameraIndependent);
  this->vrmlSnapshotCameraIndependent = cameraIndependent;
  this->vrmlSnapshotPresent = present;
  this->vrmlSnapshotRootId = root->getNodeId();
  this->vrmlSnapshotValid = this->vrmlSnapshotRootId == revision;
  return present || !this->vrmlSnapshotValid;
#else
  return false;
#endif
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
    // Notifications record changes regardless of the current optout. Admission
    // and preparation read the option once per operation, including changes
    // made between this notification and apply().
    self->translationInputDirty = true;
    // Validate all fields once per dirty source during overlay preparation.
    if (knownField && self->translationProofValid &&
        !self->translationInvalidated && !self->cameraOnlyDirty && self->materialByNode.count(trigger))
      self->materialDirty.insert(trigger);
    else self->translationInvalidated = true;
  } else if (trigger && field && field->getContainer() == trigger &&
             nodeSensor->getTriggerOperationType() == SoNotRec::FIELD_UPDATE &&
             trigger->getTypeId() == SoCube::getClassTypeId()) {
    auto * cube = static_cast<SoCube *>(trigger);
    self->translationInputDirty = true;
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
  ++this->captureCameraBasisCalls;
  struct BasisTraceScope {
    CoinRenderActionP * action;
    bool enabled;
    ~BasisTraceScope() {
      if (enabled)
        std::fprintf(stderr, "COIN_RENDER_PHASE capture_camera_basis calls=%zu prepares=%zu reuse=%zu prepare_ms=%.6f\n",
          action->captureCameraBasisCalls, action->captureCameraBasisPrepares,
          action->captureCameraBasisReuses, action->captureCameraBasisPrepareMs);
    }
  } basisTrace{this, CoinRenderDiagnosticShell::phaseTracingEnabled()};
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
  if (camera->getStereoMode() != SoCamera::MONOSCOPIC) return;
  if (camera->viewportMapping.getValue() != SoCamera::ADJUST_CAMERA &&
      camera->viewportMapping.getValue() != SoCamera::LEAVE_ALONE) return;
  // Object animation must not pay for a scene-wide camera qualification after
  // every capture. Keep only this cheap candidate/sensor until the camera moves.
  this->candidateCamera = camera;
  this->cameraSensor.attach(root);
  const CoinRenderCameraOverlayBasis * capturedBasis = NULL;
  if (qualifyCamera && cameraStableScene(root, camera)) {
    ++this->captureCameraBasisPrepares;
    const auto before = basisTrace.enabled ? std::chrono::steady_clock::now()
                                          : std::chrono::steady_clock::time_point();
    const bool prepared = CoinRenderFrameReuseCore::prepareCameraOverlayBasis(
      this->lastValidPlan, this->cameraOverlayBasis);
    if (basisTrace.enabled)
      this->captureCameraBasisPrepareMs += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - before).count();
    if (prepared) {
      this->cachedCamera = camera;
      capturedBasis = &this->cameraOverlayBasis;
    }
  }
  this->qualifyTranslationCapture(root, capturedBasis);
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
  // Internal inherited pre/post observers make the shape lists non-single.
  // The registration revision covers every callback kind, including tails;
  // only the unchanged complete internal registration can be skipped later.
  if (self->master->callbackRegistrationRevision() != self->captureCallbackRevision) {
    self->translationCaptureInvalid = true;
    return SoCallbackAction::CONTINUE;
  }
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
      typeid(*parent->getChild(2)) != typeid(SoCube)) return SoCallbackAction::CONTINUE;
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

void CoinRenderActionP::qualifyTranslationCapture(
  SoNode * root, const CoinRenderCameraOverlayBasis * capturedBasis)
{
  CoinRenderPhaseTimer timer("object_qualification");
  struct CaptureScope { bool & capturing; ~CaptureScope() { capturing = false; } } scope{this->capturingTranslations};
  std::unordered_map<const SoNode *, size_t> materialVisits;
  if (!this->capturingTranslations || this->translationCaptureInvalid || this->translationCapture.empty() ||
      this->master->callbackRegistrationRevision() != this->captureCallbackRevision ||
      !root || !this->candidateCamera || this->lastValidPlan.renderStates.size() > 65536 ||
      this->lastValidPlan.draws.size() > 65536 ||
      !cameraStableScene(root, this->candidateCamera, true, &materialVisits)) return;
  timer.mark("scene_profile");
  const auto & plan = this->lastValidPlan;
  // The strict object scene profile above must always run, independently of
  // camera admission. This lens exists only within rememberFrameRoot: neither
  // a retained basis nor matching revisions in later captures are a license.
  bool reuseBasis = objectOverlayEnabled("COIN_RENDER_DISABLE_CAPTURE_CAMERA_BASIS_REUSE") &&
    capturedBasis && capturedBasis->owner == &plan && capturedBasis->revision == plan.revision &&
    capturedBasis->stateCount == plan.renderStates.size() &&
    capturedBasis->referenceLighting.size() == plan.lightingStates.size() &&
    capturedBasis->worldLighting.size() == plan.lightingStates.size();
  for (size_t i = 0; reuseBasis && i < plan.lightingStates.size(); ++i)
    reuseBasis = capturedBasis->referenceLighting[i].lights.size() == plan.lightingStates[i].lights.size() &&
      capturedBasis->worldLighting[i].lights.size() == plan.lightingStates[i].lights.size();
  CoinRenderCameraOverlayBasis profile;
  if (reuseBasis) ++this->captureCameraBasisReuses;
  else {
    ++this->captureCameraBasisPrepares;
    const bool trace = CoinRenderDiagnosticShell::phaseTracingEnabled();
    const auto before = trace ? std::chrono::steady_clock::now()
                              : std::chrono::steady_clock::time_point();
    const bool prepared = CoinRenderFrameReuseCore::prepareCameraOverlayBasis(plan, profile);
    if (trace)
      this->captureCameraBasisPrepareMs += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - before).count();
    if (!prepared) return;
  }
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
  timer.mark("frame_profile");
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
  timer.mark("ownership");
  this->qualifyObjectPayloads(materialVisits);
  timer.mark("payload");
}

void CoinRenderActionP::qualifyObjectPayloads(const std::unordered_map<const SoNode *, size_t> & materialVisits)
{
  const auto & plan = this->lastValidPlan;
  if (plan.materials.size() > 65536 || this->capturedDrawSources.size() != plan.draws.size()) return;
  // All proofs below are local to this immutable qualification. There is no
  // revision-, pointer- or node-based reuse across captures. Ownership and
  // syntactic occurrence checks still consume every draw/binding.
  const bool memoEnabled = objectOverlayEnabled("COIN_RENDER_DISABLE_OBJECT_PROOF_MEMOIZATION");
  constexpr size_t maxMemoEntries = 1024;
  // Three hash caches charge at most 128 bytes per admitted entry, plus one
  // byte per material slot: <=448 KiB optional temporary logical metadata.
  struct IndexKey {
    uint32_t first, count, material;
    bool operator==(const IndexKey & other) const {
      return first == other.first && count == other.count && material == other.material;
    }
  };
  struct IndexHash {
    size_t operator()(const IndexKey & key) const {
      return (size_t(key.first)*16777619u ^ key.count)*16777619u ^ key.material;
    }
  };
  struct VertexKey {
    uint32_t first, count;
    const SoNode * cube;
    bool operator==(const VertexKey & other) const {
      return first == other.first && count == other.count && cube == other.cube;
    }
  };
  struct VertexHash {
    size_t operator()(const VertexKey & key) const {
      return (std::hash<const SoNode *>()(key.cube)*16777619u ^ key.first)*16777619u ^ key.count;
    }
  };
  struct Dimensions { bool valid; SbVec3f value; };
  std::unordered_set<IndexKey, IndexHash> checkedIndices;
  std::unordered_map<const SoNode *, Dimensions> checkedDimensions;
  std::unordered_map<VertexKey, bool, VertexHash> checkedVertices;
  bool indexMemoEnabled = memoEnabled, dimensionsMemoEnabled = memoEnabled, vertexMemoEnabled = memoEnabled;
  std::vector<uint8_t> checkedMaterials;
  if (memoEnabled) {
    try { checkedMaterials.resize(plan.materials.size(), 0); }
    catch (const std::bad_alloc &) {} // Optional acceleration; ordinary proofs still run.
  }
  size_t materialChecks = 0, materialHits = 0, indexChecks = 0, indexHits = 0;
  size_t dimensionsChecks = 0, dimensionsHits = 0, vertexChecks = 0, vertexHits = 0;
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
    // An exact consumed span/expected-slot proof can be shared, including its
    // conflict side effects. A mismatching span must record EVERY other slot.
    const IndexKey key{packet.geometry.firstIndex, packet.geometry.indexCount, slot};
    if (indexMemoEnabled && checkedIndices.count(key)) ++indexHits;
    else {
      ++indexChecks;
      for (uint32_t index = 0; index < packet.geometry.indexCount; ++index) {
        const uint32_t material = plan.vertices[plan.indices[packet.geometry.firstIndex + index]].materialSlot;
        if (material != slot) { materialConflicts[slot] = true; materialConflicts[material] = true; }
      }
      if (indexMemoEnabled && checkedIndices.size() < maxMemoEntries) {
        try { checkedIndices.insert(key); }
        catch (const std::bad_alloc &) { checkedIndices.clear(); indexMemoEnabled = false; }
      }
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
  const auto materialMatches = [&](uint32_t slot, const SoMaterial * source) {
    if (!checkedMaterials.empty() && checkedMaterials[slot]) {
      ++materialHits; return checkedMaterials[slot] == 1;
    }
    ++materialChecks;
    CoinRenderMaterialSnapshot snapshot;
    const bool valid = opaqueMaterialSnapshot(source, snapshot) &&
      std::memcmp(&snapshot, &plan.materials[slot], sizeof(snapshot)) == 0;
    if (!checkedMaterials.empty()) checkedMaterials[slot] = valid ? 1 : 2;
    return valid;
  };
  const auto dimensionsFor = [&](const SoNode * source, SbVec3f & dimensions) {
    if (dimensionsMemoEnabled) {
      const auto found = checkedDimensions.find(source);
      if (found != checkedDimensions.end()) {
        ++dimensionsHits;
        if (found->second.valid) dimensions = found->second.value;
        return found->second.valid;
      }
    }
    ++dimensionsChecks;
    const bool valid = cubeDimensions(static_cast<const SoCube *>(source), dimensions);
    if (dimensionsMemoEnabled && checkedDimensions.size() < maxMemoEntries) {
      try { checkedDimensions.emplace(source, Dimensions{valid, valid ? dimensions : SbVec3f(0,0,0)}); }
      catch (const std::bad_alloc &) { checkedDimensions.clear(); dimensionsMemoEnabled = false; }
    }
    return valid;
  };
  const auto verticesMatch = [&](const TranslationBinding & binding, const SbVec3f & dimensions) {
    const VertexKey key{binding.geometry.firstVertex, binding.geometry.vertexCount, binding.cube};
    if (vertexMemoEnabled) {
      const auto found = checkedVertices.find(key);
      if (found != checkedVertices.end()) { ++vertexHits; return found->second; }
    }
    ++vertexChecks;
    bool valid = true;
    for (uint32_t offset = 0; offset < binding.geometry.vertexCount; ++offset) {
      const auto & vertex = plan.vertices[binding.geometry.firstVertex + offset];
      for (int axis = 0; axis < 3; ++axis)
        if (std::abs(vertex.position[axis]) != dimensions[axis] * .5f) valid = false;
      if (vertex.screenSpaceW != 1 || vertex.fogEyeDepth != -1) valid = false;
    }
    if (vertexMemoEnabled && checkedVertices.size() < maxMemoEntries) {
      try { checkedVertices.emplace(key, valid); }
      catch (const std::bad_alloc &) { checkedVertices.clear(); vertexMemoEnabled = false; }
    }
    return valid;
  };
  for (auto & binding : this->translationBindings) {
    const auto & state = plan.renderStates[binding.stateSlot];
    binding.materialEligible = binding.overallMaterial && !materialConflicts[state.materialSlot] &&
      materialOwners[state.materialSlot] == binding.material &&
      materialMatches(state.materialSlot, binding.material);
    SbVec3f dimensions;
    binding.geometryEligible = binding.overallMaterial && binding.geometry.vertexCount == 24 &&
      binding.geometry.indexCount == 36 && dimensionsFor(binding.cube, dimensions) &&
      dimensions == binding.cubeDimensions && verticesMatch(binding, dimensions);
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
  // Options are constant during this qualification, but can change before the
  // next capture. Avoid two CRT environment scans for every object on Windows.
  const bool materialOverlayEnabled = objectOverlayEnabled("COIN_RENDER_DISABLE_MATERIAL_OVERLAY");
  const bool cubeOverlayEnabled = objectOverlayEnabled("COIN_RENDER_DISABLE_CUBE_OVERLAY");
  for (size_t i = 0; i < this->translationBindings.size(); ++i) {
    const auto & binding = this->translationBindings[i];
    if (!rejectedMaterials.count(binding.material) && materialOverlayEnabled)
      this->materialByNode[binding.material].push_back(i);
    if (!rejectedCubes.count(binding.cube) && cubeOverlayEnabled)
      this->cubeByNode[binding.cube].push_back(i);
  }
  // Conservative accounting includes source-map nodes/buckets and occurrence
  // indices. The existing transform proof has its independent 65,536 cap.
  const size_t metadataBytes = this->translationBindings.size() * 80 +
    (this->materialByNode.size() + this->cubeByNode.size()) * 128;
  if (metadataBytes > 16 * 1024 * 1024) { this->materialByNode.clear(); this->cubeByNode.clear(); }
  if (CoinRenderDiagnosticShell::phaseTracingEnabled())
    std::fprintf(stderr, "COIN_RENDER_PHASE object_proof_cache memo_enabled=%d material_checks=%zu material_hits=%zu index_checks=%zu index_hits=%zu dimensions_checks=%zu dimensions_hits=%zu vertex_checks=%zu vertex_hits=%zu cached_index_spans=%zu cached_sources=%zu cached_vertex_spans=%zu\n",
      memoEnabled ? 1 : 0, materialChecks, materialHits, indexChecks, indexHits,
      dimensionsChecks, dimensionsHits, vertexChecks, vertexHits,
      checkedIndices.size(), checkedDimensions.size(), checkedVertices.size());
}

bool CoinRenderActionP::prepareTranslationOverlay(SoNode * root, CoinRenderObjectOverlayUndo & undo)
{
  const char * disabled = std::getenv("COIN_RENDER_DISABLE_TRANSLATION_OVERLAY");
  if ((disabled && std::strcmp(disabled, "1") == 0) || !this->translationProofValid ||
      this->translationInvalidated || !this->translationInputDirty ||
      (this->translationDirty.empty() && this->materialDirty.empty() && this->geometryDirty.empty()) ||
      this->cameraOnlyDirty || root != this->cachedRoot || this->cameraSensor.getAttachedNode() != root ||
      this->translationProofGeneration != this->translationGeneration || !this->translationProofRevision) return false;
  CoinRenderPhaseTimer updateTimer("object_update_prepare");
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
  updateTimer.mark("models");
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
  updateTimer.mark("materials");
  std::vector<CoinRenderPositionUpdate> positions;
  std::vector<CoinRenderCubeRangeUpdate> cubeRanges;
  std::vector<CoinRenderDrawSourceUpdate> draws;
  const bool reserveUpdates = objectOverlayEnabled("COIN_RENDER_DISABLE_OBJECT_UPDATE_RESERVE");
  const bool compactCubes = objectOverlayEnabled("COIN_RENDER_DISABLE_CUBE_RANGE_OVERLAY");
  // The admitted Cube profile has 24 vertices. Estimate from dirty sources,
  // bounded by the existing position/draw limits. Shared sources may require
  // more ranges; normal growth remains available without a second proof pass.
  const size_t estimatedPositions = std::min(this->geometryDirty.size(),
    COIN_RENDER_OBJECT_OVERLAY_MAX_POSITIONS / size_t(24)) * size_t(24);
  const auto growthCapacity = [](size_t estimate, size_t limit) {
    size_t capacity = estimate ? 1 : 0;
    while (capacity < estimate && capacity <= limit / 2) capacity *= 2;
    return capacity < estimate ? limit : capacity;
  };
  // Keep the literal growth classes: an underestimated aliased range must not
  // double an arbitrary reserved capacity beyond the existing memory bound.
  if (reserveUpdates && !compactCubes) {
    try { positions.reserve(growthCapacity(estimatedPositions, COIN_RENDER_OBJECT_OVERLAY_MAX_POSITIONS)); } catch (const std::bad_alloc &) {}
  }
  if (reserveUpdates && compactCubes) {
    try { cubeRanges.reserve(growthCapacity(this->geometryDirty.size(), 65536)); } catch (const std::bad_alloc &) {}
  }
  if (reserveUpdates) {
    try { draws.reserve(growthCapacity(std::min(this->geometryDirty.size(), this->translationBindings.size()), 65536)); }
    catch (const std::bad_alloc &) {}
  }
  const size_t initialPositionCapacity = positions.capacity(), initialDrawCapacity = draws.capacity();
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
      if (compactCubes) {
        CoinRenderCubeRangeUpdate range;
        range.firstVertex = binding.geometry.firstVertex;
        range.vertexCount = binding.geometry.vertexCount;
        range.halfExtents = dimensions * .5f;
        cubeRanges.push_back(range);
      } else {
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
  }
  updateTimer.mark("geometry");
  if (CoinRenderDiagnosticShell::phaseTracingEnabled())
    std::fprintf(stderr, "COIN_RENDER_PHASE object_update_storage reserve=%d positions=%zu estimated_positions=%zu position_capacity=%zu position_bytes=%zu draws=%zu draw_capacity=%zu draw_bytes=%zu initial_position_capacity=%zu initial_draw_capacity=%zu position_grew=%d draw_grew=%d\n",
      reserveUpdates ? 1 : 0, positions.size(), estimatedPositions, positions.capacity(),
      positions.capacity() * sizeof(CoinRenderPositionUpdate), draws.size(), draws.capacity(),
      draws.capacity() * sizeof(CoinRenderDrawSourceUpdate), initialPositionCapacity, initialDrawCapacity,
      positions.capacity() != initialPositionCapacity, draws.capacity() != initialDrawCapacity);
  const uint64_t revision = CoinRenderFramePlanBuilder::nextRevision();
  bool applied = compactCubes && CoinRenderFrameReuseCore::beginCubeObjectOverlay(
    this->lastValidPlan, updates, materials, cubeRanges, draws, revision, undo);
  if (!applied) {
    if (compactCubes) {
      if (reserveUpdates) {
        try { positions.reserve(growthCapacity(estimatedPositions, COIN_RENDER_OBJECT_OVERLAY_MAX_POSITIONS)); }
        catch (const std::bad_alloc &) {}
      }
      for (const auto & range : cubeRanges)
        for (uint32_t offset = 0; offset < range.vertexCount; ++offset) {
          const size_t slot = size_t(range.firstVertex) + offset;
          if (slot > UINT32_MAX || slot >= this->lastValidPlan.vertices.size() ||
              positions.size() == COIN_RENDER_OBJECT_OVERLAY_MAX_POSITIONS) return false;
          const auto & previous = this->lastValidPlan.vertices[slot];
          SbVec3f position;
          for (int axis = 0; axis < 3; ++axis)
            position[axis] = std::copysign(range.halfExtents[axis], previous.position[axis]);
          positions.push_back({static_cast<uint32_t>(slot), position});
        }
    }
    applied = CoinRenderFrameReuseCore::beginObjectOverlay(
      this->lastValidPlan, updates, materials, positions, draws, revision, undo);
  }
  updateTimer.mark("overlay");
  return applied;
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

