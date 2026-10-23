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
#include <Inventor/nodes/SoSeparator.h>
#include "rendering/coinrender/CoinRenderManagerLayerPurpose.h"
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/SbViewportRegion.h>
#include "actions/CoinRenderActionP.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <algorithm>
#include <limits>
#include <new>
#include <cmath>
#include <exception>
#include <Inventor/SoRenderManager.h>
#include "rendering/coinrender/CoinRenderRttExecution.h"


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

void CoinRenderSceneManager::setNumPasses(int passes) {
  this->pimpl->action->setNumPasses(passes);
}
int CoinRenderSceneManager::getNumPasses() const { return this->pimpl->action->getNumPasses(); }
void CoinRenderSceneManager::setMultipassBufferBudget(uint64_t bytes) {
  this->pimpl->action->setMultipassBufferBudget(bytes);
}
uint64_t CoinRenderSceneManager::getMultipassBufferBudget() const {
  return this->pimpl->action->getMultipassBufferBudget();
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
  return this->renderStereo(camera, stereoOffset, SoRenderManager::ANAGLYPH);
}

CoinRenderAction::Status
CoinRenderSceneManager::renderStereo(SoCamera * camera, float stereoOffset, int stereoMode)
{
  const auto fail = [&](CoinRenderAction::Status status, const std::string & reason) {
    this->pimpl->status = status;
    this->pimpl->error = reason.c_str();
    return status;
  };
  uint32_t mode = 0;
  switch (stereoMode) {
  case SoRenderManager::ANAGLYPH: mode = 1; break;
  case SoRenderManager::INTERLEAVED_ROWS: mode = 2; break;
  case SoRenderManager::INTERLEAVED_COLUMNS: mode = 3; break;
  default: return fail(CoinRenderAction::UNSUPPORTED,
    "Hardware stereo requires native left/right buffers unavailable on a single RGBA target");
  }
  if (!this->pimpl->scene) return fail(CoinRenderAction::INVALID_SCENE, "No scene graph is set");
  if (!camera) return fail(CoinRenderAction::UNSUPPORTED, "Stereo requires a manager camera");
  if (!std::isfinite(stereoOffset))
    return fail(CoinRenderAction::INVALID_SCENE, "Active manager stereo offset must be finite");
  auto * action = this->pimpl->action;
  auto & capture = action->getPimpl().get();
  auto & target = this->pimpl->target->getPimpl().get();
  const auto savedMode = camera->getStereoMode();
  const float savedAdjustment = camera->getStereoAdjustment();
  struct Restore {
    SoCamera * camera;
    SoCamera::StereoMode mode;
    float adjustment;
    CoinRenderActionP & action;
    bool planOnly;
    int section;
    std::shared_ptr<CoinRenderRttPlan> resources;
    std::shared_ptr<CoinRenderRttCaptureMemo> memo;
    ~Restore() {
      camera->setStereoMode(mode);
      camera->setStereoAdjustment(adjustment);
      action.planOnly = planOnly;
      action.managerCaptureSection = section;
      action.sceneTexturePlan = std::move(resources);
      action.sceneTextureCaptureMemo = std::move(memo);
      // planOnly snapshots replace lastValidPlan; none can become an ordinary
      // mono cache anchor after a no-touch camera restoration or exception.
      action.master->invalidateState();
    }
  } restore{camera, savedMode, savedAdjustment, capture, capture.planOnly,
            capture.managerCaptureSection, capture.sceneTexturePlan, capture.sceneTextureCaptureMemo};
  try {
    struct View {
      CoinRenderFramePlan frame;
      CoinRenderRttPlan resources;
      std::unique_ptr<CoinRenderRttExecution> execution;
    } views[4]; // Authored BG, LEFT base, RIGHT base, monoscopic FG.
    capture.planOnly = true;
    const bool hasManagerBase = std::any_of(capture.managerLayerBoundaries.begin(), capture.managerLayerBoundaries.end(),
      [](const std::tuple<const SoNode *, bool, int> & boundary) { return std::get<2>(boundary) == COIN_RENDER_MANAGER_BASE_LAYER; });
    auto * empty = new SoSeparator;
    empty->ref();
    struct EmptyScene { SoSeparator * root; ~EmptyScene() { root->unref(); } } emptyScene{empty};
    for (int view = 0; view < 4; ++view) {
      capture.managerCaptureSection = view == 0 ? 0 : view == 3 ? 2 : 1;
      if (view == 1 || view == 2) {
        camera->setStereoAdjustment(stereoOffset);
        camera->setStereoMode(view == 1 ? SoCamera::LEFT_VIEW : SoCamera::RIGHT_VIEW);
      } else if (view == 3) {
        camera->setStereoMode(SoCamera::MONOSCOPIC);
      }
      // Supply an externally owned graph so executeApply leaves the capture
      // alive for the view transaction instead of releasing it on return.
      capture.sceneTexturePlan = std::make_shared<CoinRenderRttPlan>(target.options.sceneTexture);
      capture.sceneTextureCaptureMemo.reset();
      action->apply(!hasManagerBase && (view == 0 || view == 3) ? static_cast<SoNode *>(empty) : this->pimpl->scene);
      if (action->getLastStatus() != CoinRenderAction::SUCCESS)
        return fail(action->getLastStatus(), action->getLastError().getString());
      views[view].frame = capture.lastValidPlan;
      views[view].frame.legacyBlendAlpha = true;
      views[view].frame.viewOutput = view != 3;
      if (view == 2 && mode == 1) views[view].frame.stereoMode = 4;
      if (view == 3) views[view].frame.stereoMode = mode;
      views[view].resources = *capture.sceneTexturePlan;
      views[view].execution.reset(new CoinRenderRttExecution(&target, target.options));
    }
    // Admit the whole graph, including input only observable in RIGHT_VIEW,
    // before executing any authored producer or private GPU view.
    auto result = target.preflightSubmission(false);
    if (result.status != CoinRenderBackendStatus::SUCCESS)
      return fail(CoinRenderDiagnosticShell::actionStatus(result.status), result.diagnostic);
    for (auto & view : views) {
      result = target.preflightMultipass(view.frame, false, &view.resources);
      if (result.status == CoinRenderBackendStatus::SUCCESS)
        result = view.execution->preflight(view.resources, view.frame);
      if (result.status != CoinRenderBackendStatus::SUCCESS)
        return fail(CoinRenderDiagnosticShell::actionStatus(result.status), result.diagnostic);
    }
    const auto ready = target.prepareBackend();
    if (ready != CoinRenderBackendStatus::SUCCESS)
      return fail(CoinRenderDiagnosticShell::actionStatus(ready), target.backend->getLastError());
    size_t retainedTokens = 0, liveTokens = 0;
    for (int i=0; i<4; ++i) {
      const auto & view = views[i];
      if (view.resources.mode == COIN_RENDER_SCENE_TEXTURE_DIRECT) {
        retainedTokens = std::max(retainedTokens, liveTokens + view.resources.tokenCount());
        liveTokens += view.resources.producers.size();
      }
      if (i == 1 || i == 2 || (i == 0 && !view.frame.draws.empty())) ++liveTokens;
      retainedTokens = std::max(retainedTokens, liveTokens);
    }
    result = target.backend->preflightStereo(views[3].frame, target, retainedTokens);
    if (result.status != CoinRenderBackendStatus::SUCCESS)
      return fail(CoinRenderDiagnosticShell::actionStatus(result.status), result.diagnostic);
    // Selected-adapter checks are also made for every view before execution.
    for (auto & view : views) {
      result = view.execution->preflight(view.resources, view.frame);
      if (result.status != CoinRenderBackendStatus::SUCCESS)
        return fail(CoinRenderDiagnosticShell::actionStatus(result.status), result.diagnostic);
    }
    std::vector<uint64_t> tokens;
    struct ReleaseViews {
      CoinRenderTargetP & target;
      std::vector<uint64_t> & tokens;
      ~ReleaseViews() { if (target.backend) target.backend->retireRtt(tokens); }
    } release{target, tokens};
    CoinRenderFramePlan foreground;
    uint64_t eyes[2] = {0, 0};
    for (int view = 0; view < 4; ++view) {
      CoinRenderFramePlan resolved;
      result = views[view].execution->prepare(views[view].resources, views[view].frame, resolved);
      if (result.status != CoinRenderBackendStatus::SUCCESS)
        return fail(CoinRenderDiagnosticShell::actionStatus(result.status), result.diagnostic);
      if (view == 3) { foreground = std::move(resolved); break; }
      // Native renderStereo clears over BG, but its authored traversal and
      // captured resources are still real occurrences with an execution owner.
      if (view == 0 && resolved.draws.empty()) continue;
      resolved.viewOutput = true;
      resolved.managerFrame = false;
      if (view == 2 && mode == 1) {
        resolved.stereoMode = 4; // RGB/depth clear independently; alpha follows native channel state.
        resolved.stereoTokens[0] = resolved.stereoTokens[1] = eyes[0];
      }
      uint64_t token = 0;
      result = target.backend->submitRtt(resolved, target.size,
        CoinRenderFramePlanBuilder::nextRevision(), target, token);
      if (token) tokens.push_back(token);
      if (result.status != CoinRenderBackendStatus::SUCCESS)
        return fail(CoinRenderDiagnosticShell::actionStatus(result.status), result.diagnostic);
      if (!token) return fail(CoinRenderAction::BACKEND_ERROR, "Private stereo view returned no GPU attachment");
      if (view) eyes[view - 1] = token;
    }
    foreground.revision = CoinRenderFramePlanBuilder::nextRevision();
    foreground.stereoMode = mode;
    foreground.stereoTokens[0] = eyes[0];
    foreground.stereoTokens[1] = eyes[1];
    const auto & viewport = action->getViewportRegion();
    const auto origin = viewport.getViewportOriginPixels();
    const auto extent = viewport.getViewportSizePixels();
    for (int i = 0; i < 2; ++i) {
      foreground.stereoOrigin[i] = origin[i];
      foreground.stereoExtent[i] = extent[i];
    }
    result = target.executeFrame(foreground);
    return fail(CoinRenderDiagnosticShell::actionStatus(result.status), result.diagnostic);
  } catch (const std::bad_alloc &) {
    return fail(CoinRenderAction::OUT_OF_MEMORY, "Stereo view allocation failed");
  } catch (const std::exception & error) {
    return fail(CoinRenderAction::BACKEND_ERROR, error.what());
  } catch (...) {
    return fail(CoinRenderAction::BACKEND_ERROR, "Stereo authored traversal threw an exception");
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
