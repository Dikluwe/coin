#include "actions/CoinRenderActionIncludesP.h"

SO_ACTION_SOURCE(CoinRenderAction);

// Contain authored C++ exceptions before they unwind through legacy
// SoAction::apply, whose read lock/root reference and path bookkeeping are
// released on normal return. Each dispatch restores its own traversal depth;
// the enclosing action then observes termination and rejects the whole frame.
template <void (*Method)(SoAction *, SoNode *)>
void CoinRenderActionP::guardedNodeMethod(SoAction * action, SoNode * node)
{
  auto * render = static_cast<CoinRenderAction *>(action);
  if (render->hasTerminated()) return;
  SoState * state = render->getState();
  const int depth = state->getDepth();
  const int pathLength = render->getCurPath()->fullPath().getLength();
  const auto reject = [&](CoinRenderAction::Status status, const char * message) {
    while (state->getDepth() > depth) state->pop();
    while (render->getCurPath()->fullPath().getLength() > pathLength) render->popCurPath();
    render->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(status,
      CoinRenderDiagnosticDomain::ACTION, SbString(message)));
    render->setTerminated(TRUE);
  };
  try { Method(action, node); }
  catch (const std::bad_alloc &) { reject(CoinRenderAction::OUT_OF_MEMORY, "Authored render traversal allocation failed"); }
  catch (const std::exception & error) { reject(CoinRenderAction::BACKEND_ERROR, error.what()); }
  catch (...) { reject(CoinRenderAction::BACKEND_ERROR, "Authored render traversal threw an exception"); }
}

void
CoinRenderAction::initClass(void)
{
  SO_ACTION_INTERNAL_INIT_CLASS(CoinRenderAction, SoCallbackAction);
  SO_ENABLE(CoinRenderAction, SoDepthBufferElement);
  SO_ENABLE(CoinRenderAction, SoTextureCombineElement);
  SO_ENABLE(CoinRenderAction, SoShadowStyleElement);
  CoinRenderDepthPolicyElement::initClass();
  SO_ENABLE(CoinRenderAction, CoinRenderDepthPolicyElement);
  CoinRenderShaderModuleElement::initClass();
  SO_ENABLE(CoinRenderAction, CoinRenderShaderModuleElement);
  SO_ACTION_ADD_METHOD(SoNode, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::portableNodeMethod>);
  SO_ACTION_ADD_METHOD(SoShape, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::shapeContentMethod>);
  SO_ACTION_ADD_METHOD(SoImage, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::screenContentMethod>);
  SO_ACTION_ADD_METHOD(SoText2, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::screenContentMethod>);
  SO_ACTION_ADD_METHOD(SoMarkerSet, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::markerContentMethod>);
  SO_ACTION_ADD_METHOD(SoIndexedMarkerSet, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::markerContentMethod>);
  SO_ACTION_ADD_METHOD(SoAlphaTest, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::alphaTestMethod>);
#ifdef HAVE_VRML97
  SO_ACTION_ADD_METHOD(SoVRMLShape, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::vrmlStateMethod>);
  SO_ACTION_ADD_METHOD(SoVRMLMaterial, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::vrmlStateMethod>);
  SO_ACTION_ADD_METHOD(SoVRMLLight, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::vrmlStateMethod>);
  SO_ACTION_ADD_METHOD(SoVRMLFog, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::vrmlStateMethod>);
  SO_ACTION_ADD_METHOD(SoVRMLBackground, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::vrmlStateMethod>);
  SO_ACTION_ADD_METHOD(SoVRMLPixelTexture, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::vrmlStateMethod>);
  SO_ACTION_ADD_METHOD(SoVRMLImageTexture, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::vrmlStateMethod>);
#endif
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

void CoinRenderAction::setAbortCallback(CoinRenderAbortCB * callback, void * userdata)
{
  this->pimpl->abortCallback = callback; this->pimpl->abortUserdata = userdata;
  this->pimpl->abortResponse = CONTINUE;
  this->pimpl->cachedRoot = nullptr;
  this->pimpl->clearTranslationProof();
}
void CoinRenderAction::getAbortCallback(CoinRenderAbortCB * & callback, void * & userdata) const
{ callback = this->pimpl->abortCallback; userdata = this->pimpl->abortUserdata; }
SbBool CoinRenderAction::abortNow()
{
  auto & p = this->pimpl.get();
  if (!p.isApplying || !p.abortCallback) return FALSE;
  p.abortResponse = p.abortCallback(p.abortUserdata);
  if (p.abortResponse < CONTINUE || p.abortResponse > DELAY) {
    p.invalidAbortCode = true;
    p.setDiagnostic(CoinRenderDiagnosticShell::action(INVALID_SCENE,
      CoinRenderDiagnosticDomain::ACTION, SbString("Abort callback returned an invalid code")));
    this->setTerminated(TRUE);
    return TRUE;
  }
  if (p.abortResponse == DELAY && p.replayingAnnotations) p.abortResponse = CONTINUE;
  if (p.abortResponse == ABORT) {
    p.candidateAborted = true;
    this->setTerminated(TRUE);
  } else if (p.abortResponse == DELAY) {
    if (!this->addDelayedPath(const_cast<SoPath *>(this->getCurPath()))) p.abortResponse = PRUNE;
  }
  return p.abortResponse == CONTINUE ? FALSE : TRUE;
}
SbBool CoinRenderAction::addDelayedPath(SoPath * path)
{ return path ? this->deferOverlayPath(path) : FALSE; }
SbBool CoinRenderAction::isRenderingDelayedPaths() const
{ return this->pimpl->isApplying && this->pimpl->replayingAnnotations ? TRUE : FALSE; }
void CoinRenderAction::addPreRenderCallback(CoinRenderPreRenderCB * callback, void * userdata)
{ if (callback) this->pimpl->preRenderCallbacks.push_back({callback, userdata}); }
void CoinRenderAction::removePreRenderCallback(CoinRenderPreRenderCB * callback, void * userdata)
{
  auto & list = this->pimpl->preRenderCallbacks;
  list.erase(std::remove_if(list.begin(), list.end(),
    [=](const CoinRenderActionP::PreRenderCallback & entry) {
      return entry.callback == callback && entry.userdata == userdata;
    }), list.end());
}
void CoinRenderAction::invalidateState()
{
  auto & p = this->pimpl.get();
  p.cachedRoot = nullptr;
  p.invalidatedDuringApply = true;
  if (p.isApplying) { p.stateInvalidationPending = true; return; }
  p.stateInvalidationPending = false;
  this->inherited::invalidateState();
  p.cameraSensor.detach(); p.cachedCamera = p.candidateCamera = nullptr;
  p.cameraOverlayBasis = CoinRenderCameraOverlayBasis();
  p.cameraPatchInvalidated = true; p.cameraRecaptureRequired = true;
  p.clearTranslationProof();
  CoinRenderFramePlanBuilder cleanBuilder;
  std::swap(p.builder, cleanBuilder);
}
void CoinRenderAction::setUpdateArea(const SbVec2f & origin, const SbVec2f & size)
{ this->pimpl->updateOrigin = origin; this->pimpl->updateSize = size; }
void CoinRenderAction::getUpdateArea(SbVec2f & origin, SbVec2f & size) const
{ origin = this->pimpl->updateOrigin; size = this->pimpl->updateSize; }
void CoinRenderAction::setSortedObjectOrderStrategy(SortedObjectOrderStrategy strategy,
  CoinRenderSortedObjectOrderCB * callback, void * userdata)
{
  this->pimpl->sortingStrategy = strategy; this->pimpl->sortingCallback = callback;
  this->pimpl->sortingUserdata = userdata; this->pimpl->cachedRoot = nullptr;
  this->pimpl->clearTranslationProof();
}
void CoinRenderAction::setTransparentDelayedObjectRenderType(TransparentDelayedObjectRenderType type)
{
  this->pimpl->delayedRenderType = type;
  this->pimpl->cachedRoot = nullptr; this->pimpl->clearTranslationProof();
}
CoinRenderAction::TransparentDelayedObjectRenderType
CoinRenderAction::getTransparentDelayedObjectRenderType() const { return this->pimpl->delayedRenderType; }
void CoinRenderAction::setSmoothing(SbBool enabled)
{
  this->pimpl->smoothing = enabled != FALSE;
  this->pimpl->cachedRoot = nullptr; this->pimpl->clearTranslationProof();
}
SbBool CoinRenderAction::isSmoothing() const { return this->pimpl->smoothing; }
void CoinRenderAction::setDelayedObjDepthWrite(SbBool enabled)
{
  if (this->pimpl->delayedObjDepthWrite != (enabled != FALSE)) {
    this->pimpl->delayedObjDepthWrite = enabled != FALSE;
    this->pimpl->cachedRoot = nullptr; this->pimpl->clearTranslationProof();
  }
}
SbBool CoinRenderAction::getDelayedObjDepthWrite() const
{ return this->pimpl->delayedObjDepthWrite; }

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

void
CoinRenderAction::setDrawMode(DrawMode mode)
{
  if (this->pimpl->drawMode != mode) {
    this->pimpl->drawMode = mode;
    this->pimpl->cachedRoot = NULL;
  }
}

CoinRenderAction::DrawMode
CoinRenderAction::getDrawMode(void) const
{
  return this->pimpl->drawMode;
}

void
CoinRenderAction::setOverlayColor(const SbColor4f & color)
{
  if (this->pimpl->overlayColor != color) {
    this->pimpl->overlayColor = color;
    this->pimpl->cachedRoot = NULL;
  }
}

const SbColor4f &
CoinRenderAction::getOverlayColor(void) const
{
  return this->pimpl->overlayColor;
}

void
CoinRenderAction::setTexturesEnabled(SbBool enabled)
{
  if (this->pimpl->texturesEnabled != enabled) {
    this->pimpl->texturesEnabled = enabled;
    this->pimpl->cachedRoot = NULL;
  }
}

SbBool
CoinRenderAction::isTexturesEnabled(void) const
{
  return this->pimpl->texturesEnabled;
}

void CoinRenderAction::setPassCallback(CoinRenderPassCB * callback, void * userdata)
{
  this->pimpl->passCallback = callback; this->pimpl->passUserdata = userdata;
  this->pimpl->cachedRoot = nullptr; this->pimpl->clearTranslationProof();
}
int CoinRenderAction::getCurPass() const { return this->pimpl->currentPass; }
void CoinRenderAction::setCurPass(int pass, int total)
{
  this->pimpl->externalPass = true; this->pimpl->requestedExternalPass = pass;
  this->pimpl->currentPass = pass; this->pimpl->numPasses = total;
  this->pimpl->cachedRoot = nullptr; this->pimpl->clearTranslationProof();
}
void CoinRenderAction::setNumPasses(int passes) {
  this->pimpl->externalPass = false; this->pimpl->currentPass = 0;
  if (this->pimpl->numPasses != passes) {
    this->pimpl->numPasses = passes;
    this->pimpl->cachedRoot = NULL;
  }
}
int CoinRenderAction::getNumPasses() const { return this->pimpl->numPasses; }
void CoinRenderAction::setMultipassBufferBudget(uint64_t bytes) {
  if (this->pimpl->multipassBufferBudget != bytes) {
    this->pimpl->multipassBufferBudget = bytes;
    this->pimpl->cachedRoot = NULL;
  }
}
uint64_t CoinRenderAction::getMultipassBufferBudget() const {
  return this->pimpl->multipassBufferBudget;
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


SbBool CoinRenderAction::setPortableShaderModule(SoShaderProgram *program,
                                                const CoinRenderShaderModule &module)
{
  if(!program) return FALSE;
  SbString diagnostic;
  if(!coin_render_validate_shader_module(module,diagnostic)) return FALSE;
  try {
    auto registration=std::make_shared<CoinRenderShaderRegistration>();
    registration->module=module;
    program->ref();
    registration->program=std::shared_ptr<SoShaderProgram>(program,[](SoShaderProgram *node){node->unref();});
    this->pimpl->portableShaderModules[program]=registration;
    this->pimpl->hasLastValidPlan=false;
    return TRUE;
  } catch(const std::bad_alloc &) { return FALSE; }
}

void CoinRenderAction::clearPortableShaderModule(SoShaderProgram *program)
{
  this->pimpl->portableShaderModules.erase(program);
  this->pimpl->hasLastValidPlan=false;
}

void CoinRenderAction::clearPortableShaderModules()
{
  this->pimpl->portableShaderModules.clear();
  this->pimpl->hasLastValidPlan=false;
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

SbBool
CoinRenderAction::rejectUnsupported(const SbString & reason)
{
  if (!this->pimpl->isApplying || this->hasTerminated()) return FALSE;
  this->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
    UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN, reason));
  this->setTerminated(TRUE);
  return TRUE;
}

SbBool
CoinRenderAction::rejectInvalidScene(const SbString & reason)
{
  if (!this->pimpl->isApplying || this->hasTerminated()) return FALSE;
  this->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
    INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN, reason));
  this->setTerminated(TRUE);
  return TRUE;
}

SbBool
CoinRenderAction::captureScreenContent(SoNode * node)
{
  if (!this->pimpl->isApplying || this->hasTerminated()) return FALSE;
  if (!node || this->getCurPathTail() != node ||
      (!node->isOfType(SoText2::getClassTypeId()) && !node->isOfType(SoImage::getClassTypeId()))) {
    this->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
      UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString("Screen-content delegation requires the current SoText2/SoImage callback node")));
    this->setTerminated(TRUE);
    return FALSE;
  }
  const bool success = this->pimpl->boundingBoxObservers ||
    this->pimpl->builder.captureScreenContent(this, node);
  if (!success) {
    this->setTerminated(TRUE);
    return FALSE;
  }
  if (!this->hasSingleShapeCallbacks(node->getTypeId())) {
    // Observe inherited primitives once without reentering the host callback
    // or capturing its image quad a second time. Keep observer state local.
    SoState * state = this->getState();
    state->push();
    struct ObserverScope {
      CoinRenderActionP & owner;
      SoState * state;
      bool previous;
      ~ObserverScope() { state->pop(); owner.boundingBoxObservers = previous; }
    } scope{this->pimpl.get(), state, this->pimpl->boundingBoxObservers};
    this->pimpl->boundingBoxObservers = true;
    static_cast<SoShape *>(node)->SoShape::callback(this);
  }
  return TRUE;
}

void
CoinRenderAction::beginForegroundPass()
{
  this->beginForegroundPass(FALSE);
}

void
CoinRenderAction::beginForegroundPass(SbBool clearDepth)
{
  if (this->pimpl->isApplying && !this->pimpl->replayingAnnotations)
    this->pimpl->builder.beginForeground(clearDepth != FALSE);
}

void
CoinRenderAction::endForegroundPass()
{
  if (this->pimpl->isApplying && !this->pimpl->replayingAnnotations)
    this->pimpl->builder.endForeground();
}

void
CoinRenderAction::clearManagerLayerBoundaries()
{
  this->pimpl->managerLayerBoundaries.clear();
}

void
CoinRenderAction::addManagerLayerBoundary(SoNode * separator, SbBool clearDepth,
                                          int transparencyType)
{
  if (separator || transparencyType == COIN_RENDER_MANAGER_OWNER)
    this->pimpl->managerLayerBoundaries.emplace_back(
      separator, clearDepth != FALSE, transparencyType);
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
    vrmlSnapshotSensor(CoinRenderActionP::vrmlSnapshotSensorCB, this),
    cameraSensor(CoinRenderActionP::cameraSensorCB, this)
{
  this->vrmlSnapshotSensor.setPriority(0);
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
