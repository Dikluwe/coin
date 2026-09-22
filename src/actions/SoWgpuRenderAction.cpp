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
#if defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE)
  return TRUE;
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
SoWgpuRenderAction::apply(SoNode * root)
{
  this->pimpl->executeApply([&]() {
    if (root) {
      this->inherited::apply(root);
    }
  });
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
    if (root) {
      this->inherited::beginTraversal(root);
    }
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
    backgroundColor(0.0f, 0.0f, 0.0f, 1.0f),
    lastStatus(SoWgpuRenderAction::SUCCESS),
    hasLastValidPlan(false),
    isApplying(false),
    hasReentrancyError(false)
{
}

SoWgpuRenderActionP::~SoWgpuRenderActionP()
{
}

template <typename F>
void
SoWgpuRenderActionP::executeApply(F traversalFn)
{
  if (this->isApplying) {
    this->lastStatus = SoWgpuRenderAction::INVALID_SCENE;
    this->lastError = "Nested apply() calls are not permitted on SoWgpuRenderAction";
    this->hasReentrancyError = true;
    return;
  }

  this->isApplying = true;
  this->hasReentrancyError = false;
  this->builder.beginFrame(this->backgroundColor, this->master->getViewportRegion());

  traversalFn();

  this->isApplying = false;

  if (this->hasReentrancyError) {
    this->lastStatus = SoWgpuRenderAction::INVALID_SCENE;
    this->lastError = "Nested apply() calls are not permitted on SoWgpuRenderAction";
    return;
  }

  FramePlan plan;
  std::string err;
  if (!this->builder.build(plan, &err)) {
    this->lastStatus = SoWgpuRenderAction::INVALID_SCENE;
    this->lastError = err.c_str();
    return;
  }

  if (this->target == NULL) {
    // Mode 0: Recording backend
    this->lastRecordingLog = this->recordingBackend.recordToString(plan).c_str();
    this->lastValidPlan = plan;
    this->hasLastValidPlan = true;
    this->lastStatus = SoWgpuRenderAction::SUCCESS;
    this->lastError = "";
    return;
  }

  // Target provided: validate target status
  if (this->target->getStatus() != SoWgpuRenderTarget::TARGET_READY) {
    this->lastStatus = SoWgpuRenderAction::NOT_READY;
    this->lastError = "Render target is not ready";
    return;
  }

  // Execute frame on target
  std::string execError;
  if (!this->target->pimpl->executeFrame(plan, execError)) {
    if (execError.rfind("UNSUPPORTED", 0) == 0) {
      this->lastStatus = SoWgpuRenderAction::UNSUPPORTED;
    } else {
      this->lastStatus = SoWgpuRenderAction::BACKEND_ERROR;
    }
    this->lastError = execError.c_str();
    return;
  }

  this->lastRecordingLog = this->recordingBackend.recordToString(plan).c_str();
  this->lastValidPlan = plan;
  this->hasLastValidPlan = true;
  this->lastStatus = SoWgpuRenderAction::SUCCESS;
  this->lastError = "";
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
