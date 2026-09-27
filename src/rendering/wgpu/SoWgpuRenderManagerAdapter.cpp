#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/rendering/SoWgpuRenderManagerAdapter.h>
#include <Inventor/rendering/SoWgpuSceneManager.h>
#include <Inventor/rendering/SoWgpuNativeSurface.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/SbViewportRegion.h>

namespace {
void ensureWgpuRenderActionInitialized()
{
  if (SoWgpuRenderAction::getClassTypeId().isBad())
    SoWgpuRenderAction::initClass();
}
}

struct SoWgpuRenderManagerAdapter::P {
  SoRenderManager * source = nullptr;
  SoNode * sceneGraphOverride = nullptr;
  SoWgpuSceneManager * renderer = nullptr;
  SoWgpuRenderAction::Status status = SoWgpuRenderAction::SUCCESS;
  SbString error;
};

SoWgpuRenderManagerAdapter::SoWgpuRenderManagerAdapter(
  SoRenderManager & source, const SbVec2i32 & offscreenSize)
  : pimpl(new P)
{
  ensureWgpuRenderActionInitialized();
  this->pimpl->source = &source;
  this->pimpl->renderer = new SoWgpuSceneManager(offscreenSize);
  this->syncFromRenderManager();
}

SoWgpuRenderManagerAdapter::SoWgpuRenderManagerAdapter(
  SoRenderManager & source,
  const SoWgpuNativeSurfaceDescriptor & nativeWindow,
  const SbVec2i32 & framebufferSize)
  : pimpl(new P)
{
  ensureWgpuRenderActionInitialized();
  this->pimpl->source = &source;
  this->pimpl->renderer = new SoWgpuSceneManager(nativeWindow, framebufferSize);
  this->syncFromRenderManager();
}

SoWgpuRenderManagerAdapter::~SoWgpuRenderManagerAdapter()
{
  if (this->pimpl->sceneGraphOverride) this->pimpl->sceneGraphOverride->unref();
  delete this->pimpl->renderer;
  delete this->pimpl;
}

void
SoWgpuRenderManagerAdapter::setSceneGraphOverride(SoNode * sceneGraph)
{
  if (sceneGraph == this->pimpl->sceneGraphOverride) return;
  if (sceneGraph) sceneGraph->ref();
  if (this->pimpl->sceneGraphOverride) this->pimpl->sceneGraphOverride->unref();
  this->pimpl->sceneGraphOverride = sceneGraph;
  this->syncFromRenderManager();
}

SbBool
SoWgpuRenderManagerAdapter::syncFromRenderManager()
{
  this->pimpl->renderer->setSceneGraph(this->pimpl->sceneGraphOverride
    ? this->pimpl->sceneGraphOverride
    : this->pimpl->source->getSceneGraph());
  this->pimpl->renderer->setBackgroundColor(this->pimpl->source->getBackgroundColor());
  this->pimpl->renderer->setViewportRegion(this->pimpl->source->getViewportRegion());
  this->pimpl->status = this->pimpl->renderer->getLastStatus();
  this->pimpl->error = this->pimpl->renderer->getLastError();
  return this->pimpl->status != SoWgpuRenderAction::BACKEND_ERROR;
}

SbBool
SoWgpuRenderManagerAdapter::resize(const SbVec2i32 & framebufferSize)
{
  if (!this->pimpl->renderer->resize(framebufferSize)) {
    this->pimpl->status = this->pimpl->renderer->getLastStatus();
    this->pimpl->error = this->pimpl->renderer->getLastError();
    return FALSE;
  }
  return this->syncFromRenderManager();
}

SoWgpuRenderAction::Status
SoWgpuRenderManagerAdapter::render()
{
  if (!this->pimpl->source->prepareFrame()) {
    this->pimpl->status = SoWgpuRenderAction::BACKEND_ERROR;
    this->pimpl->error = "Recursive SoRenderManager frame preparation";
    return this->pimpl->status;
  }
  struct FrameScope {
    explicit FrameScope(SoRenderManager * source) : manager(source), rendered(FALSE) {}
    SoRenderManager * manager;
    SbBool rendered;
    ~FrameScope() { manager->finishFrame(rendered); }
  } scope(this->pimpl->source);
  if (!this->syncFromRenderManager()) return this->pimpl->status;
  this->pimpl->status = this->pimpl->renderer->render();
  this->pimpl->error = this->pimpl->renderer->getLastError();
  scope.rendered = this->pimpl->status == SoWgpuRenderAction::SUCCESS;
  return this->pimpl->status;
}

SoWgpuSceneManager *
SoWgpuRenderManagerAdapter::getSceneManager() const
{
  return this->pimpl->renderer;
}

SoWgpuRenderAction::Status
SoWgpuRenderManagerAdapter::getLastStatus() const
{
  return this->pimpl->status;
}

const SbString &
SoWgpuRenderManagerAdapter::getLastError() const
{
  return this->pimpl->error;
}
