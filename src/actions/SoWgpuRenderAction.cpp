#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/actions/SoSubAction.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoVertexProperty.h>
#include <Inventor/bundles/SoTextureCoordinateBundle.h>
#include <Inventor/elements/SoCoordinateElement.h>
#include <Inventor/elements/SoNormalElement.h>
#include <Inventor/elements/SoMaterialBindingElement.h>
#include <Inventor/elements/SoNormalBindingElement.h>
#include <Inventor/misc/SoState.h>

#include "actions/SoWgpuRenderActionP.h"
#include "rendering/wgpu/SoWgpuRenderTargetP.h"
#include "actions/SoSubActionP.h"
#if defined(HAVE_WGPU_RUST_BRIDGE)
#include "rendering/wgpu/SoWgpuRustBackend.h"
#elif defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE)
#include "rendering/wgpu/SoWgpuNativeBackend.h"
#endif

SO_ACTION_SOURCE(SoWgpuRenderAction);

void
SoWgpuRenderAction::initClass(void)
{
  SO_ACTION_INTERNAL_INIT_CLASS(SoWgpuRenderAction, SoCallbackAction);
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

void
SoWgpuRenderAction::setFastPathEnabled(SbBool enable)
{
  this->pimpl->fastPathEnabled = (enable != FALSE);
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
    hasReentrancyError(false),
    fastPathEnabled(true)
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

  if (this->master->hasTerminated() && this->lastStatus != SoWgpuRenderAction::SUCCESS) {
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
  if (this->target->getStatus() == SoWgpuRenderTarget::TARGET_ERROR) {
    this->lastStatus = SoWgpuRenderAction::BACKEND_ERROR;
    const char * tgtErr = this->target->getLastError();
    this->lastError = (tgtErr && tgtErr[0]) ? tgtErr : "Render target is in fatal TARGET_ERROR state";
    return;
  }

  // Execute frame on target
  FrameExecutionResult execRes = this->target->pimpl->executeFrame(plan);
  if (execRes.status != BackendStatus::SUCCESS) {
    switch (execRes.status) {
      case BackendStatus::NOT_READY:
        this->lastStatus = SoWgpuRenderAction::NOT_READY;
        break;
      case BackendStatus::UNSUPPORTED:
        this->lastStatus = SoWgpuRenderAction::UNSUPPORTED;
        break;
      case BackendStatus::OUT_OF_MEMORY:
        this->lastStatus = SoWgpuRenderAction::OUT_OF_MEMORY;
        break;
      case BackendStatus::DEVICE_LOST:
        this->lastStatus = SoWgpuRenderAction::DEVICE_LOST;
        break;
      case BackendStatus::SURFACE_LOST:
        this->lastStatus = SoWgpuRenderAction::SURFACE_LOST;
        break;
      case BackendStatus::BACKEND_ERROR:
      default:
        this->lastStatus = SoWgpuRenderAction::BACKEND_ERROR;
        break;
    }
    this->lastError = execRes.diagnostic.c_str();
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
SoWgpuRenderActionP::indexedFaceSetPreCB(void * userdata,
                                        SoCallbackAction * action,
                                        const SoNode * node)
{
  SoWgpuRenderActionP * p = static_cast<SoWgpuRenderActionP *>(userdata);
  if (!p->fastPathEnabled) {
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
  if (tb.needCoordinates() && tb.isFunction()) {
    if (vp) state->pop();
    return SoCallbackAction::CONTINUE;
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
  if (ifs->normalIndex.getNum() > 0 && ifs->normalIndex[0] >= 0) {
    view.normalIndex = SoWgpuSpan<int32_t>(ifs->normalIndex.getValues(0), static_cast<size_t>(ifs->normalIndex.getNum()));
  }
  if (ifs->materialIndex.getNum() > 0 && ifs->materialIndex[0] >= 0) {
    view.materialIndex = SoWgpuSpan<int32_t>(ifs->materialIndex.getValues(0), static_cast<size_t>(ifs->materialIndex.getNum()));
  }
  if (ifs->textureCoordIndex.getNum() > 0 && ifs->textureCoordIndex[0] >= 0) {
    view.texCoordIndex = SoWgpuSpan<int32_t>(ifs->textureCoordIndex.getValues(0), static_cast<size_t>(ifs->textureCoordIndex.getNum()));
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
    p->lastStatus = SoWgpuRenderAction::INVALID_SCENE;
    p->lastError = err.empty() ? "Invalid scene in IndexedFaceSet" : err.c_str();
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
  if (ils->materialIndex.getNum() > 0 && ils->materialIndex[0] >= 0) {
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
    p->lastStatus = SoWgpuRenderAction::INVALID_SCENE;
    p->lastError = err.empty() ? "Invalid scene in IndexedLineSet" : err.c_str();
    return SoCallbackAction::ABORT;
  } else {
    return SoCallbackAction::CONTINUE;
  }
}
