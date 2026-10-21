#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/SbViewportRegion.h>
#include "actions/CoinRenderActionP.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <algorithm>
#include <limits>
#include <new>

namespace {
bool anaglyphEyeSupported(const CoinRenderFramePlan & frame, std::string & diagnostic)
{
  if (!frame.shadowGroups.empty() || !frame.shadowLights.empty()) {
    diagnostic = "ANAGLYPH shadow maps require two-eye qualification";
    return false;
  }
  for (const auto & texture : frame.textures)
    if (texture.producerId || texture.gpuToken) {
      diagnostic = "ANAGLYPH scene textures require two-eye producer mapping";
      return false;
    }
  for (const auto & state : frame.renderStates) {
    if (state.rasterTransparent || state.rasterForceBlend ||
        (state.transparentTexture && state.transparencyType != SoGLRenderAction::BLEND)) {
      diagnostic = "ANAGLYPH transparency requires per-eye composition";
      return false;
    }
    const bool hasMaterialTransparency = state.materialSlot < frame.materials.size() &&
      frame.materials[state.materialSlot].transparency > 0.0f;
    if ((state.transparentMaterial || hasMaterialTransparency ||
         state.screenDoorTransparency > 0.0f) &&
        state.transparencyType != SoGLRenderAction::SCREEN_DOOR &&
        state.transparencyType != SoGLRenderAction::BLEND) {
      diagnostic = "ANAGLYPH transparency requires per-eye composition";
      return false;
    }
  }
  return true;
}

bool appendAnaglyphEye(CoinRenderFramePlan & output, const CoinRenderFramePlan & eye,
                       uint8_t mask, uint32_t layerOffset, bool clearDepth,
                       std::string & diagnostic)
{
  const auto fits = [](size_t a, size_t b) {
    return b <= std::numeric_limits<uint32_t>::max() &&
           a <= std::numeric_limits<uint32_t>::max() - b;
  };
  if (!fits(output.vertices.size(), eye.vertices.size()) ||
      !fits(output.indices.size(), eye.indices.size()) ||
      !fits(output.materials.size(), eye.materials.size()) ||
      !fits(output.renderStates.size(), eye.renderStates.size()) ||
      !fits(output.lightingStates.size(), eye.lightingStates.size()) ||
      !fits(output.cameras.size(), eye.cameras.size()) ||
      !fits(output.viewports.size(), eye.viewports.size()) ||
      !fits(output.textures.size(), eye.textures.size()) ||
      !fits(output.samplers.size(), eye.samplers.size())) {
    diagnostic = "ANAGLYPH frame exceeds uint32 indexing";
    return false;
  }
  const uint32_t vertexBase = static_cast<uint32_t>(output.vertices.size());
  const uint32_t indexBase = static_cast<uint32_t>(output.indices.size());
  const uint32_t materialBase = static_cast<uint32_t>(output.materials.size());
  const uint32_t stateBase = static_cast<uint32_t>(output.renderStates.size());
  const uint32_t lightingBase = static_cast<uint32_t>(output.lightingStates.size());
  const uint32_t cameraBase = static_cast<uint32_t>(output.cameras.size());
  const uint32_t viewportBase = static_cast<uint32_t>(output.viewports.size());
  const uint32_t textureBase = static_cast<uint32_t>(output.textures.size());
  const uint32_t samplerBase = static_cast<uint32_t>(output.samplers.size());
  output.materials.insert(output.materials.end(), eye.materials.begin(), eye.materials.end());
  output.lightingStates.insert(output.lightingStates.end(), eye.lightingStates.begin(), eye.lightingStates.end());
  output.cameras.insert(output.cameras.end(), eye.cameras.begin(), eye.cameras.end());
  output.viewports.insert(output.viewports.end(), eye.viewports.begin(), eye.viewports.end());
  output.textures.insert(output.textures.end(), eye.textures.begin(), eye.textures.end());
  output.samplers.insert(output.samplers.end(), eye.samplers.begin(), eye.samplers.end());
  for (auto vertex : eye.vertices) {
    vertex.materialSlot += materialBase;
    output.vertices.push_back(std::move(vertex));
  }
  for (uint32_t index : eye.indices) output.indices.push_back(index + vertexBase);
  for (auto state : eye.renderStates) {
    state.materialSlot += materialBase;
    state.lightingSlot += lightingBase;
    state.cameraSlot += cameraBase;
    state.viewportSlot += viewportBase;
    if (state.hasTexture) {
      state.textureImageSlot += textureBase;
      state.samplerSlot += samplerBase;
    }
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS - 1; ++unit)
      if (static_cast<const CoinRenderRenderStateSnapshot &>(state).extraTextures[unit].enabled) {
        state.extraTextures[unit].imageSlot += textureBase;
        state.extraTextures[unit].samplerSlot += samplerBase;
      }
    state.colorWriteMask &= mask;
    output.renderStates.push_back(std::move(state));
  }
  bool firstDraw = true;
  for (auto draw : eye.draws) {
    if (draw.renderLayer > std::numeric_limits<uint32_t>::max() - layerOffset) {
      diagnostic = "ANAGLYPH layer index overflow";
      return false;
    }
    draw.geometry.firstVertex += vertexBase;
    draw.geometry.firstIndex += indexBase;
    draw.renderStateSlot += stateBase;
    draw.renderLayer += layerOffset;
    if (clearDepth && firstDraw) draw.clearDepthBefore = true;
    firstDraw = false;
    output.draws.push_back(std::move(draw));
  }
  return true;
}
}

struct CoinRenderSceneManager::P {
  CoinRenderTarget * target = nullptr;
  CoinRenderAction * action = nullptr;
  SoNode * scene = nullptr;
  CoinRenderAction::Status status = CoinRenderAction::SUCCESS;
  SbString error;
};

CoinRenderSceneManager::CoinRenderSceneManager(const SbVec2i32 & offscreenSize)
  : pimpl(new P)
{
  if (CoinRenderAction::getClassTypeId().isBad()) CoinRenderAction::initClass();
  this->pimpl->target = CoinRenderTarget::createOffscreen(offscreenSize);
  this->pimpl->action = new CoinRenderAction(SbViewportRegion(offscreenSize[0], offscreenSize[1]));
  this->pimpl->action->setRenderTarget(this->pimpl->target);
  if (this->pimpl->target->getStatus() != CoinRenderTarget::TARGET_READY) {
    const CoinRenderActionDiagnostic diagnostic = CoinRenderDiagnosticShell::fromTarget(
      this->pimpl->target->getStatus(), this->pimpl->target->getLastError());
    this->pimpl->status = diagnostic.status;
    this->pimpl->error = diagnostic.message;
  }
}

CoinRenderSceneManager::CoinRenderSceneManager(const CoinRenderNativeSurfaceDescriptor & nativeWindow,
                                       const SbVec2i32 & framebufferSize)
  : pimpl(new P)
{
  if (CoinRenderAction::getClassTypeId().isBad()) CoinRenderAction::initClass();
  this->pimpl->target = CoinRenderTarget::createWindow(nativeWindow, framebufferSize);
  this->pimpl->action = new CoinRenderAction(SbViewportRegion(framebufferSize[0], framebufferSize[1]));
  this->pimpl->action->setRenderTarget(this->pimpl->target);
  if (this->pimpl->target->getStatus() != CoinRenderTarget::TARGET_READY) {
    const CoinRenderActionDiagnostic diagnostic = CoinRenderDiagnosticShell::fromTarget(
      this->pimpl->target->getStatus(), this->pimpl->target->getLastError());
    this->pimpl->status = diagnostic.status;
    this->pimpl->error = diagnostic.message;
  }
}

CoinRenderSceneManager::CoinRenderSceneManager(const SbVec2i32 & offscreenSize, const CoinRenderOptions & options)
  : pimpl(new P)
{
  if (CoinRenderAction::getClassTypeId().isBad()) CoinRenderAction::initClass();
  this->pimpl->target = CoinRenderTarget::createOffscreen(offscreenSize, options);
  this->pimpl->action = new CoinRenderAction(SbViewportRegion(offscreenSize[0], offscreenSize[1]));
  this->pimpl->action->setRenderTarget(this->pimpl->target);
  if (this->pimpl->target->getStatus() != CoinRenderTarget::TARGET_READY) {
    const CoinRenderActionDiagnostic diagnostic = CoinRenderDiagnosticShell::fromTarget(
      this->pimpl->target->getStatus(), this->pimpl->target->getLastError());
    this->pimpl->status = diagnostic.status;
    this->pimpl->error = diagnostic.message;
  }
}

CoinRenderSceneManager::CoinRenderSceneManager(const CoinRenderNativeSurfaceDescriptor & nativeWindow,
                                       const SbVec2i32 & framebufferSize, const CoinRenderOptions & options)
  : pimpl(new P)
{
  if (CoinRenderAction::getClassTypeId().isBad()) CoinRenderAction::initClass();
  this->pimpl->target = CoinRenderTarget::createWindow(nativeWindow, framebufferSize, options);
  this->pimpl->action = new CoinRenderAction(SbViewportRegion(framebufferSize[0], framebufferSize[1]));
  this->pimpl->action->setRenderTarget(this->pimpl->target);
  if (this->pimpl->target->getStatus() != CoinRenderTarget::TARGET_READY) {
    const CoinRenderActionDiagnostic diagnostic = CoinRenderDiagnosticShell::fromTarget(
      this->pimpl->target->getStatus(), this->pimpl->target->getLastError());
    this->pimpl->status = diagnostic.status;
    this->pimpl->error = diagnostic.message;
  }
}

CoinRenderSceneManager::~CoinRenderSceneManager()
{
  delete this->pimpl->action;
  if (this->pimpl->scene) this->pimpl->scene->unref();
  delete this->pimpl->target;
  delete this->pimpl;
}

void
CoinRenderSceneManager::setSceneGraph(SoNode * root)
{
  if (root == this->pimpl->scene) return;
  if (root) root->ref();
  if (this->pimpl->scene) this->pimpl->scene->unref();
  this->pimpl->scene = root;
}

SoNode *
CoinRenderSceneManager::getSceneGraph() const
{
  return this->pimpl->scene;
}

CoinRenderTarget *
CoinRenderSceneManager::getRenderTarget() const
{
  return this->pimpl->target;
}

void
CoinRenderSceneManager::setBackgroundColor(const SbColor4f & color)
{
  this->pimpl->action->setBackgroundColor(color);
}

void
CoinRenderSceneManager::setViewportRegion(const SbViewportRegion & viewport)
{
  this->pimpl->action->setViewportRegion(viewport);
}

void
CoinRenderSceneManager::setTransparencyType(CoinRenderAction::TransparencyType type)
{
  this->pimpl->action->setTransparencyType(type);
}

CoinRenderAction::TransparencyType
CoinRenderSceneManager::getTransparencyType() const
{
  return this->pimpl->action->getTransparencyType();
}

void
CoinRenderSceneManager::setDrawMode(CoinRenderAction::DrawMode mode)
{
  this->pimpl->action->setDrawMode(mode);
}

CoinRenderAction::DrawMode
CoinRenderSceneManager::getDrawMode() const
{
  return this->pimpl->action->getDrawMode();
}

void
CoinRenderSceneManager::setOverlayColor(const SbColor4f & color)
{
  this->pimpl->action->setOverlayColor(color);
}

const SbColor4f &
CoinRenderSceneManager::getOverlayColor() const
{
  return this->pimpl->action->getOverlayColor();
}

void
CoinRenderSceneManager::setTexturesEnabled(SbBool enabled)
{
  this->pimpl->action->setTexturesEnabled(enabled);
}

SbBool
CoinRenderSceneManager::isTexturesEnabled() const
{
  return this->pimpl->action->isTexturesEnabled();
}

void
CoinRenderSceneManager::clearManagerLayerBoundaries()
{
  this->pimpl->action->clearManagerLayerBoundaries();
}

void
CoinRenderSceneManager::addManagerLayerBoundary(SoNode * separator, SbBool clearDepth,
                                                 int transparencyType)
{
  this->pimpl->action->addManagerLayerBoundary(separator, clearDepth, transparencyType);
}

SbBool
CoinRenderSceneManager::resize(const SbVec2i32 & framebufferSize)
{
  if (!this->pimpl->target->resize(framebufferSize)) {
    this->pimpl->status = CoinRenderAction::BACKEND_ERROR;
    this->pimpl->error = this->pimpl->target->getLastError();
    return FALSE;
  }
  this->pimpl->action->setViewportRegion(
    SbViewportRegion(framebufferSize[0], framebufferSize[1]));
  this->pimpl->status = CoinRenderDiagnosticShell::actionStatus(
    this->pimpl->target->getStatus());
  this->pimpl->error = this->pimpl->target->getLastError();
  return TRUE;
}

CoinRenderAction::Status
CoinRenderSceneManager::render()
{
  if (!this->pimpl->scene) {
    this->pimpl->status = CoinRenderAction::INVALID_SCENE;
    this->pimpl->error = "No scene graph is set";
    return this->pimpl->status;
  }
  this->pimpl->action->apply(this->pimpl->scene);
  this->pimpl->status = this->pimpl->action->getLastStatus();
  this->pimpl->error = this->pimpl->action->getLastError();
  return this->pimpl->status;
}

CoinRenderAction::Status
CoinRenderSceneManager::renderAnaglyph(SoCamera * camera, float stereoOffset)
{
  auto fail = [&](CoinRenderAction::Status status, const std::string & reason) {
    this->pimpl->status = status;
    this->pimpl->error = reason.c_str();
    return status;
  };
  if (!this->pimpl->scene) return fail(CoinRenderAction::INVALID_SCENE, "No scene graph is set");
  if (!camera) return fail(CoinRenderAction::UNSUPPORTED, "ANAGLYPH requires a manager camera");
  const SoCamera::StereoMode savedMode = camera->getStereoMode();
  const float savedAdjustment = camera->getStereoAdjustment();
  auto * action = this->pimpl->action;
  const bool savedPlanOnly = action->getPimpl()->planOnly;
  struct Restore {
    SoCamera * camera;
    SoCamera::StereoMode mode;
    float adjustment;
    CoinRenderActionP * action;
    bool planOnly;
    ~Restore() {
      camera->setStereoMode(mode);
      camera->setStereoAdjustment(adjustment);
      action->planOnly = planOnly;
    }
  } restore{camera, savedMode, savedAdjustment, &action->getPimpl().get(), savedPlanOnly};
  try {
    action->getPimpl()->planOnly = true;
    camera->setStereoAdjustment(stereoOffset);
    camera->setStereoMode(SoCamera::LEFT_VIEW);
    action->apply(this->pimpl->scene);
    if (action->getLastStatus() != CoinRenderAction::SUCCESS)
      return fail(action->getLastStatus(), action->getLastError().getString());
    CoinRenderFramePlan left = action->getPimpl()->lastValidPlan;
    camera->setStereoMode(SoCamera::RIGHT_VIEW);
    action->apply(this->pimpl->scene);
    if (action->getLastStatus() != CoinRenderAction::SUCCESS)
      return fail(action->getLastStatus(), action->getLastError().getString());
    CoinRenderFramePlan right = action->getPimpl()->lastValidPlan;
    std::string diagnostic;
    if (!anaglyphEyeSupported(left, diagnostic) ||
        !anaglyphEyeSupported(right, diagnostic))
      return fail(CoinRenderAction::UNSUPPORTED, diagnostic);
    CoinRenderFramePlan combined;
    // Revisions are process-global cache keys. Reserve one from the same
    // allocator as ordinary captures; max(eyes)+1 can collide with the next
    // independently captured frame and reuse an unrelated Rust draw order.
    combined.revision = CoinRenderFramePlanBuilder::nextRevision();
    combined.textureSamplingPolicy = left.textureSamplingPolicy;
    combined.rasterizationPolicy = left.rasterizationPolicy;
    combined.outputColorFormat = left.outputColorFormat;
    combined.transparency = left.transparency;
    combined.clearColor = left.clearColor;
    if (!appendAnaglyphEye(combined, left, 0x09, 0, false, diagnostic))
      return fail(CoinRenderAction::UNSUPPORTED, diagnostic);
    uint32_t rightLayer = 1;
    for (const auto & draw : left.draws) {
      if (draw.renderLayer == std::numeric_limits<uint32_t>::max())
        return fail(CoinRenderAction::UNSUPPORTED, "ANAGLYPH layer index overflow");
      rightLayer = std::max(rightLayer, draw.renderLayer + 1);
    }
    if (!appendAnaglyphEye(combined, right, 0x0e, rightLayer, true, diagnostic) ||
        !combined.isValid(&diagnostic))
      return fail(CoinRenderAction::UNSUPPORTED, diagnostic);
    const auto result = this->pimpl->target->getPimpl()->executeFrame(combined);
    return fail(CoinRenderDiagnosticShell::actionStatus(result.status), result.diagnostic);
  } catch (const std::bad_alloc &) {
    return fail(CoinRenderAction::BACKEND_ERROR, "ANAGLYPH frame allocation failed");
  }
}

CoinRenderAction::Status
CoinRenderSceneManager::renderAsync(CoinRenderReadbackTicket & ticket)
{
  ticket = CoinRenderReadbackTicket{};
  if (!this->pimpl->scene) {
    this->pimpl->status = CoinRenderAction::INVALID_SCENE;
    this->pimpl->error = "No scene graph is set";
    return this->pimpl->status;
  }
  this->pimpl->action->applyAsync(this->pimpl->scene, ticket);
  this->pimpl->status = this->pimpl->action->getLastStatus();
  this->pimpl->error = this->pimpl->action->getLastError();
  return this->pimpl->status;
}

CoinRenderAction::Status
CoinRenderSceneManager::getLastStatus() const
{
  return this->pimpl->status;
}

const SbString &
CoinRenderSceneManager::getLastError() const
{
  return this->pimpl->error;
}
