#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/rendering/SoWgpuSceneManager.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/rendering/SoWgpuNativeSurface.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/SbViewportRegion.h>
#include "rendering/wgpu/SoWgpuDiagnosticShell.h"

struct SoWgpuSceneManager::P {
  SoWgpuRenderTarget * target = nullptr;
  SoWgpuRenderAction * action = nullptr;
  SoNode * scene = nullptr;
  SoWgpuRenderAction::Status status = SoWgpuRenderAction::SUCCESS;
  SbString error;
};

SoWgpuSceneManager::SoWgpuSceneManager(const SbVec2i32 & offscreenSize)
  : pimpl(new P)
{
  this->pimpl->target = SoWgpuRenderTarget::createOffscreen(offscreenSize);
  this->pimpl->action = new SoWgpuRenderAction(SbViewportRegion(offscreenSize[0], offscreenSize[1]));
  this->pimpl->action->setRenderTarget(this->pimpl->target);
  if (this->pimpl->target->getStatus() != SoWgpuRenderTarget::TARGET_READY) {
    const SoWgpuActionDiagnostic diagnostic = SoWgpuDiagnosticShell::fromTarget(
      this->pimpl->target->getStatus(), this->pimpl->target->getLastError());
    this->pimpl->status = diagnostic.status;
    this->pimpl->error = diagnostic.message;
  }
}

SoWgpuSceneManager::SoWgpuSceneManager(const SoWgpuNativeSurfaceDescriptor & nativeWindow,
                                       const SbVec2i32 & framebufferSize)
  : pimpl(new P)
{
  this->pimpl->target = SoWgpuRenderTarget::createWindow(nativeWindow, framebufferSize);
  this->pimpl->action = new SoWgpuRenderAction(SbViewportRegion(framebufferSize[0], framebufferSize[1]));
  this->pimpl->action->setRenderTarget(this->pimpl->target);
  if (this->pimpl->target->getStatus() != SoWgpuRenderTarget::TARGET_READY) {
    const SoWgpuActionDiagnostic diagnostic = SoWgpuDiagnosticShell::fromTarget(
      this->pimpl->target->getStatus(), this->pimpl->target->getLastError());
    this->pimpl->status = diagnostic.status;
    this->pimpl->error = diagnostic.message;
  }
}

SoWgpuSceneManager::~SoWgpuSceneManager()
{
  delete this->pimpl->action;
  if (this->pimpl->scene) this->pimpl->scene->unref();
  delete this->pimpl->target;
  delete this->pimpl;
}

void
SoWgpuSceneManager::setSceneGraph(SoNode * root)
{
  if (root == this->pimpl->scene) return;
  if (root) root->ref();
  if (this->pimpl->scene) this->pimpl->scene->unref();
  this->pimpl->scene = root;
}

SoNode *
SoWgpuSceneManager::getSceneGraph() const
{
  return this->pimpl->scene;
}

SoWgpuRenderTarget *
SoWgpuSceneManager::getRenderTarget() const
{
  return this->pimpl->target;
}

void
SoWgpuSceneManager::setBackgroundColor(const SbColor4f & color)
{
  this->pimpl->action->setBackgroundColor(color);
}

void
SoWgpuSceneManager::setViewportRegion(const SbViewportRegion & viewport)
{
  this->pimpl->action->setViewportRegion(viewport);
}

SbBool
SoWgpuSceneManager::resize(const SbVec2i32 & framebufferSize)
{
  if (!this->pimpl->target->resize(framebufferSize)) {
    this->pimpl->status = SoWgpuRenderAction::BACKEND_ERROR;
    this->pimpl->error = this->pimpl->target->getLastError();
    return FALSE;
  }
  this->pimpl->action->setViewportRegion(
    SbViewportRegion(framebufferSize[0], framebufferSize[1]));
  this->pimpl->status = SoWgpuDiagnosticShell::actionStatus(
    this->pimpl->target->getStatus());
  this->pimpl->error = this->pimpl->target->getLastError();
  return TRUE;
}

SoWgpuRenderAction::Status
SoWgpuSceneManager::render()
{
  if (!this->pimpl->scene) {
    this->pimpl->status = SoWgpuRenderAction::INVALID_SCENE;
    this->pimpl->error = "No scene graph is set";
    return this->pimpl->status;
  }
  this->pimpl->action->apply(this->pimpl->scene);
  this->pimpl->status = this->pimpl->action->getLastStatus();
  this->pimpl->error = this->pimpl->action->getLastError();
  return this->pimpl->status;
}

SoWgpuRenderAction::Status
SoWgpuSceneManager::renderAsync(SoWgpuReadbackTicket & ticket)
{
  ticket = SoWgpuReadbackTicket{};
  if (!this->pimpl->scene) {
    this->pimpl->status = SoWgpuRenderAction::INVALID_SCENE;
    this->pimpl->error = "No scene graph is set";
    return this->pimpl->status;
  }
  this->pimpl->action->applyAsync(this->pimpl->scene, ticket);
  this->pimpl->status = this->pimpl->action->getLastStatus();
  this->pimpl->error = this->pimpl->action->getLastError();
  return this->pimpl->status;
}

SoWgpuRenderAction::Status
SoWgpuSceneManager::getLastStatus() const
{
  return this->pimpl->status;
}

const SbString &
SoWgpuSceneManager::getLastError() const
{
  return this->pimpl->error;
}
