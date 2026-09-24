#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/actions/SoSubAction.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoLight.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoVertexProperty.h>
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
#include <algorithm>
#include <memory>
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
SoWgpuRenderAction::applyAsync(SoNode * root, SoWgpuReadbackTicket & outTicket)
{
  outTicket = SoWgpuReadbackTicket{};
  if (this->pimpl->isApplying) {
    this->pimpl->lastStatus = INVALID_SCENE;
    this->pimpl->lastError = "Nested applyAsync() calls are not permitted";
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
    asyncTicket(NULL),
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
  this->sceneTexturePixels.clear();
  if (!this->sceneTextureStagedBytes) {
    this->sceneTextureStagedBytes = std::make_shared<size_t>(0);
  }
  this->builder.beginFrame(this->backgroundColor, this->master->getViewportRegion());

  traversalFn();

  this->isApplying = false;

  this->sceneTextureStagedBytes.reset();
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
    if (this->builder.isUnsupportedBuild()) {
      this->lastStatus = SoWgpuRenderAction::UNSUPPORTED;
    } else {
      this->lastStatus = SoWgpuRenderAction::INVALID_SCENE;
    }
    this->lastError = err.c_str();
    return;
  }

  if (this->target == NULL) {
    if (this->asyncTicket) {
      this->lastStatus = SoWgpuRenderAction::NO_TARGET;
      this->lastError = "applyAsync() requires an offscreen render target";
      return;
    }
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
  FrameExecutionResult execRes = this->asyncTicket
    ? this->target->pimpl->executeFrameAsync(plan, *this->asyncTicket)
    : this->target->pimpl->executeFrame(plan);
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

  this->master->addPreCallback(SoLight::getClassTypeId(), lightPreCB, this);
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
    p->lastStatus = SoWgpuRenderAction::BACKEND_ERROR;
    p->lastError = "SoSceneTexture2 has no traversal state";
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
    p->lastStatus = SoWgpuRenderAction::UNSUPPORTED;
    p->lastError = "SoSceneTexture2 supports only unit 0, RGBA8, MODULATE, REPEAT/CLAMP, NONE transparency function and no sceneTransparencyType";
    return SoCallbackAction::ABORT;
  }

  if (SoTextureQualityElement::get(state) <= 0.0f) {
    SoMultiTextureImageElement::setDefault(state, const_cast<SoSceneTexture2 *>(texture), 0);
    SoMultiTextureEnabledElement::set(state, const_cast<SoSceneTexture2 *>(texture), 0, FALSE);
    return SoCallbackAction::CONTINUE;
  }

  const SbVec2s size = texture->size.getValue();
  SoNode * scene = texture->scene.getValue();
  if (!scene || size[0] <= 0 || size[1] <= 0 ||
      size[0] > 2048 || size[1] > 2048) {
    p->lastStatus = SoWgpuRenderAction::UNSUPPORTED;
    p->lastError = "SoSceneTexture2 requires a scene and dimensions in 1..2048";
    return SoCallbackAction::ABORT;
  }

  const size_t stagedBytes = size_t(size[0]) * size_t(size[1]) * 4;
  const size_t maxStagedBytes = size_t(64) * 1024 * 1024;
  if (stagedBytes > maxStagedBytes - *p->sceneTextureStagedBytes) {
    p->lastStatus = SoWgpuRenderAction::UNSUPPORTED;
    p->lastError = "SoSceneTexture2 staged RGBA8 budget exceeds 64 MiB per apply";
    return SoCallbackAction::ABORT;
  }

  static thread_local std::vector<const SoSceneTexture2 *> activeTextures;
  if (activeTextures.size() >= 8 ||
      std::find(activeTextures.begin(), activeTextures.end(), texture) != activeTextures.end()) {
    p->lastStatus = SoWgpuRenderAction::UNSUPPORTED;
    p->lastError = "SoSceneTexture2 dependency cycle or nesting beyond eight passes";
    return SoCallbackAction::ABORT;
  }
  activeTextures.push_back(texture);
  struct ActiveTextureGuard {
    std::vector<const SoSceneTexture2 *> & stack;
    ~ActiveTextureGuard() { stack.pop_back(); }
  } guard{activeTextures};

  *p->sceneTextureStagedBytes += stagedBytes;
  const SbVec4f background = texture->backgroundColor.getValue();
  std::unique_ptr<SoWgpuRenderTarget> childTarget(
    SoWgpuRenderTarget::createOffscreen(SbVec2i32(size[0], size[1])));
  if (!childTarget || childTarget->getStatus() != SoWgpuRenderTarget::TARGET_READY) {
    p->lastStatus = SoWgpuRenderAction::BACKEND_ERROR;
    p->lastError = "Cannot create SoSceneTexture2 offscreen target";
    return SoCallbackAction::ABORT;
  }
  SoWgpuRenderAction childAction(SbViewportRegion(size[0], size[1]));
  childAction.setRenderTarget(childTarget.get());
  childAction.setBackgroundColor(SbColor4f(background[0], background[1],
                                           background[2], background[3]));
  childAction.pimpl->sceneTextureStagedBytes = p->sceneTextureStagedBytes;
  childAction.apply(scene);
  if (childAction.getLastStatus() != SoWgpuRenderAction::SUCCESS) {
    p->lastStatus = childAction.getLastStatus();
    p->lastError = SbString("SoSceneTexture2 subscene: ") + childAction.getLastError();
    return SoCallbackAction::ABORT;
  }

  std::vector<uint8_t> pixels;
  childTarget->readbackRGBA(pixels);
  const size_t required = size_t(size[0]) * size_t(size[1]) * 4;
  if (pixels.size() != required) {
    p->lastStatus = SoWgpuRenderAction::BACKEND_ERROR;
    p->lastError = "SoSceneTexture2 subscene returned incomplete RGBA8 readback";
    return SoCallbackAction::ABORT;
  }
  // WebGPU readback rows start at the top; Coin image bytes use the opposite
  // texture origin. Flip once before the parent pass uploads this image.
  const size_t rowBytes = size_t(size[0]) * 4;
  for (size_t y = 0; y < size_t(size[1]) / 2; ++y) {
    std::swap_ranges(pixels.begin() + y * rowBytes,
                     pixels.begin() + (y + 1) * rowBytes,
                     pixels.begin() + (size_t(size[1]) - 1 - y) * rowBytes);
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
        p->lastStatus = SoWgpuRenderAction::UNSUPPORTED;
        p->lastError = "Procedural/DEFAULT texture coordinates are not supported in Subwave 3B";
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
    p->lastStatus = SoWgpuRenderAction::INVALID_SCENE;
    p->lastError = err.empty() ? "Invalid scene in IndexedFaceSet" : err.c_str();
    return SoCallbackAction::ABORT;
  } else if (res == FastPathResult::UNSUPPORTED) {
    p->lastStatus = SoWgpuRenderAction::UNSUPPORTED;
    p->lastError = err.empty() ? "Unsupported feature in IndexedFaceSet" : err.c_str();
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
    p->lastStatus = SoWgpuRenderAction::INVALID_SCENE;
    p->lastError = err.empty() ? "Invalid scene in IndexedLineSet" : err.c_str();
    return SoCallbackAction::ABORT;
  } else if (res == FastPathResult::UNSUPPORTED) {
    p->lastStatus = SoWgpuRenderAction::UNSUPPORTED;
    p->lastError = err.empty() ? "Unsupported feature in IndexedLineSet" : err.c_str();
    return SoCallbackAction::ABORT;
  } else {
    return SoCallbackAction::CONTINUE;
  }
}
