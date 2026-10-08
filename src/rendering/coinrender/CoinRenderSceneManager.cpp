#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/SbViewportRegion.h>
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"

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
