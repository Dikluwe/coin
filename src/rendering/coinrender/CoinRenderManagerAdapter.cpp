#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/SbViewportRegion.h>

namespace {
void ensureWgpuRenderActionInitialized()
{
  if (CoinRenderAction::getClassTypeId().isBad())
    CoinRenderAction::initClass();
}
}

struct CoinRenderManagerAdapter::P {
  SoRenderManager * source = nullptr;
  SoNode * sceneGraphOverride = nullptr;
  CoinRenderSceneManager * renderer = nullptr;
  CoinRenderAction::Status status = CoinRenderAction::SUCCESS;
  SbString error;
};

CoinRenderManagerAdapter::CoinRenderManagerAdapter(
  SoRenderManager & source, const SbVec2i32 & offscreenSize)
  : pimpl(new P)
{
  ensureWgpuRenderActionInitialized();
  this->pimpl->source = &source;
  this->pimpl->renderer = new CoinRenderSceneManager(offscreenSize);
  this->syncFromRenderManager();
}

CoinRenderManagerAdapter::CoinRenderManagerAdapter(
  SoRenderManager & source,
  const CoinRenderNativeSurfaceDescriptor & nativeWindow,
  const SbVec2i32 & framebufferSize)
  : pimpl(new P)
{
  ensureWgpuRenderActionInitialized();
  this->pimpl->source = &source;
  this->pimpl->renderer = new CoinRenderSceneManager(nativeWindow, framebufferSize);
  this->syncFromRenderManager();
}

CoinRenderManagerAdapter::~CoinRenderManagerAdapter()
{
  if (this->pimpl->sceneGraphOverride) this->pimpl->sceneGraphOverride->unref();
  delete this->pimpl->renderer;
  delete this->pimpl;
}

void
CoinRenderManagerAdapter::setSceneGraphOverride(SoNode * sceneGraph)
{
  if (sceneGraph == this->pimpl->sceneGraphOverride) return;
  if (sceneGraph) sceneGraph->ref();
  if (this->pimpl->sceneGraphOverride) this->pimpl->sceneGraphOverride->unref();
  this->pimpl->sceneGraphOverride = sceneGraph;
  this->syncFromRenderManager();
}

SbBool
CoinRenderManagerAdapter::syncFromRenderManager()
{
  this->pimpl->renderer->setSceneGraph(this->pimpl->sceneGraphOverride
    ? this->pimpl->sceneGraphOverride
    : this->pimpl->source->getSceneGraph());
  this->pimpl->renderer->setBackgroundColor(this->pimpl->source->getBackgroundColor());
  this->pimpl->renderer->setViewportRegion(this->pimpl->source->getViewportRegion());
  this->pimpl->status = this->pimpl->renderer->getLastStatus();
  this->pimpl->error = this->pimpl->renderer->getLastError();
  return this->pimpl->status != CoinRenderAction::BACKEND_ERROR;
}

SbBool
CoinRenderManagerAdapter::resize(const SbVec2i32 & framebufferSize)
{
  if (!this->pimpl->renderer->resize(framebufferSize)) {
    this->pimpl->status = this->pimpl->renderer->getLastStatus();
    this->pimpl->error = this->pimpl->renderer->getLastError();
    return FALSE;
  }
  return this->syncFromRenderManager();
}

CoinRenderAction::Status
CoinRenderManagerAdapter::render()
{
  if (!this->pimpl->source->prepareFrame()) {
    this->pimpl->status = CoinRenderAction::BACKEND_ERROR;
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
  scope.rendered = this->pimpl->status == CoinRenderAction::SUCCESS;
  return this->pimpl->status;
}

CoinRenderSceneManager *
CoinRenderManagerAdapter::getSceneManager() const
{
  return this->pimpl->renderer;
}

CoinRenderAction::Status
CoinRenderManagerAdapter::getLastStatus() const
{
  return this->pimpl->status;
}

const SbString &
CoinRenderManagerAdapter::getLastError() const
{
  return this->pimpl->error;
}
