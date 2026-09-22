#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/actions/SoSubAction.h>
#include <Inventor/nodes/SoShape.h>
#include "actions/SoWgpuRenderActionP.h"
#include "rendering/wgpu/SoWgpuRenderTargetP.h"
#include "actions/SoSubActionP.h"

SO_ACTION_SOURCE(SoWgpuRenderAction);

void
SoWgpuRenderAction::initClass(void)
{
  SO_ACTION_INTERNAL_INIT_CLASS(SoWgpuRenderAction, SoCallbackAction);
}

SbBool
SoWgpuRenderAction::isGpuBackendAvailable(void)
{
  return TRUE;
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
}

const SbViewportRegion &
SoWgpuRenderAction::getViewportRegion(void) const
{
  return this->pimpl->viewport;
}

void
SoWgpuRenderAction::setRenderTarget(SoWgpuRenderTarget * target)
{
  this->pimpl->target = target;
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
}

const SbColor4f &
SoWgpuRenderAction::getBackgroundColor(void) const
{
  return this->pimpl->backgroundColor;
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
  return this->pimpl->lastRecordingLog;
}

void
SoWgpuRenderAction::beginTraversal(SoNode * root)
{
  if (this->pimpl->isTraversing) {
    this->pimpl->lastStatus = INVALID_SCENE;
    this->pimpl->lastError = "Nested apply() calls are not permitted on SoWgpuRenderAction";
    return;
  }

  struct ReentrancyGuard {
    bool & flag;
    ReentrancyGuard(bool & f) : flag(f) { flag = true; }
    ~ReentrancyGuard() { flag = false; }
  } guard(this->pimpl->isTraversing);

  this->pimpl->builder.beginFrame(this->pimpl->backgroundColor, this->getViewportRegion());

  if (root) {
    this->inherited::beginTraversal(root);
  }

  FramePlan plan;
  std::string err;
  if (!this->pimpl->builder.build(plan, &err)) {
    this->pimpl->lastStatus = INVALID_SCENE;
    this->pimpl->lastError = err.c_str();
    return;
  }

  if (this->pimpl->target == NULL) {
    // Mode 0: Recording backend
    this->pimpl->lastRecordingLog = this->pimpl->recordingBackend.recordToString(plan).c_str();
    this->pimpl->lastValidPlan = plan;
    this->pimpl->hasLastValidPlan = true;
    this->pimpl->lastStatus = SUCCESS;
    this->pimpl->lastError = "";
    return;
  }

  // Target provided: validate target status
  if (this->pimpl->target->getStatus() != SoWgpuRenderTarget::TARGET_READY) {
    this->pimpl->lastStatus = NOT_READY;
    this->pimpl->lastError = "Render target is not ready";
    return;
  }

  // Execute frame on target
  std::string execError;
  if (!this->pimpl->target->pimpl->executeFrame(plan, execError)) {
    if (execError.rfind("UNSUPPORTED", 0) == 0) {
      this->pimpl->lastStatus = UNSUPPORTED;
    } else {
      this->pimpl->lastStatus = BACKEND_ERROR;
    }
    this->pimpl->lastError = execError.c_str();
    return;
  }

  this->pimpl->lastRecordingLog = this->pimpl->recordingBackend.recordToString(plan).c_str();
  this->pimpl->lastValidPlan = plan;
  this->pimpl->hasLastValidPlan = true;
  this->pimpl->lastStatus = SUCCESS;
  this->pimpl->lastError = "";
}

// SoWgpuRenderActionP implementation

SoWgpuRenderActionP::SoWgpuRenderActionP(SoWgpuRenderAction * m)
  : master(m),
    target(NULL),
    backgroundColor(0.0f, 0.0f, 0.0f, 1.0f),
    lastStatus(SoWgpuRenderAction::SUCCESS),
    hasLastValidPlan(false),
    isTraversing(false)
{
}

SoWgpuRenderActionP::~SoWgpuRenderActionP()
{
}

void
SoWgpuRenderActionP::initCallbacks()
{
  this->master->addTriangleCallback(SoShape::getClassTypeId(), triangleCB, this);
  this->master->addLineSegmentCallback(SoShape::getClassTypeId(), lineCB, this);
  this->master->addPointCallback(SoShape::getClassTypeId(), pointCB, this);
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
