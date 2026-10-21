#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/SoBoxHighlightRenderAction.h>
#include <Inventor/actions/SoLineHighlightRenderAction.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/actions/SoGetBoundingBoxAction.h>
#include <Inventor/nodes/SoSelection.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoMatrixTransform.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoBaseColor.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoPolygonOffset.h>
#include "rendering/coinrender/CoinRenderManagerLayerPurpose.h"
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/SoPath.h>
#include <Inventor/lists/SoPathList.h>
#include <Inventor/misc/SoChildList.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <cmath>
#include <unordered_map>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/SbViewportRegion.h>
#include <vector>
#include <utility>
#include <tuple>

namespace {
SoNode * highlightNodeFromHead(const SoFullPathView & path, int index)
{
  return path.getNodeFromTail(path.getLength() - 1 - index);
}

int highlightIndexFromHead(const SoFullPathView & path, int index)
{
  return path.getIndexFromTail(path.getLength() - 1 - index);
}

void ensureWgpuRenderActionInitialized()
{
  if (CoinRenderAction::getClassTypeId().isBad())
    CoinRenderAction::initClass();
}
}

struct CoinRenderManagerAdapter::P {
  SoRenderManager * source = nullptr;
  SoNode * sceneGraphOverride = nullptr;
  SoNode * composedRoot = nullptr;
  SoNode * composedMain = nullptr;
  bool composedHighlight = false;
  std::vector<std::tuple<SoNode *, int, int>> composedLayers;
  std::vector<std::tuple<SoNode *, bool, int>> managerBoundaries;
  CoinRenderSceneManager * renderer = nullptr;
  CoinRenderAction::Status status = CoinRenderAction::SUCCESS;
  SbString error;
  SoPathList highlightPaths;
  std::unordered_map<const SoNode *, const SoPath *> highlightReplays;

  SoCallback * highlightTrampoline = nullptr;
  SoPath * activeReplayPath = nullptr;

  P() {
    this->highlightTrampoline = new SoCallback;
    this->highlightTrampoline->ref();
    this->highlightTrampoline->setCallback(P::replayPath, this);
  }
  ~P() { this->highlightTrampoline->unref(); }
  static void replayPath(void * data, SoAction * action);
  bool prepareHighlight(SoNode * root, SoGLRenderAction * sourceAction,
                        const SbViewportRegion & viewport, SoSeparator * & graph);
};

void
CoinRenderManagerAdapter::P::replayPath(void * data, SoAction * action)
{
  if (!action->isOfType(CoinRenderAction::getClassTypeId())) return;
  auto * p = static_cast<P *>(data);
  if (p->activeReplayPath) {
    // switchToNodeTraversal entered this private trampoline with an empty
    // current path and NO_PATH. Supply the correct head before switching to
    // native path traversal; both switches restore the surrounding context.
    SoPath * path = p->activeReplayPath;
    action->pushCurPath(-1, path->getHead());
    if (path->fullPath().getLength() > 1) action->switchToPathTraversal(path);
    else action->traverse(path->getHead()); // A selected root includes its subtree.
    action->popCurPath();
    return;
  }
  const auto found = p->highlightReplays.find(action->getCurPathTail());
  if (found == p->highlightReplays.end()) {
    static_cast<CoinRenderAction *>(action)->rejectUnsupported("Missing manager highlight replay path");
    return;
  }
  p->activeReplayPath = const_cast<SoPath *>(found->second);
  struct ReplayScope {
    P * owner;
    ~ReplayScope() { owner->activeReplayPath = nullptr; }
  } scope{p};
  // This is a traversal within the current builder, never apply()/submission.
  // Native SoAction/SoChildList now prune off-path separators and honor state
  // nodes preceding the selected tail, including SoSwitchElement inheritance.
  action->switchToNodeTraversal(p->highlightTrampoline);
}

bool
CoinRenderManagerAdapter::P::prepareHighlight(SoNode * root, SoGLRenderAction * sourceAction,
                                              const SbViewportRegion & viewport,
                                              SoSeparator * & graph)
{
  graph = nullptr;
  this->highlightReplays.clear();
  this->highlightPaths.truncate(0);
  if (!root || !sourceAction) return true;
  // SoDB alone need not initialize the interaction highlight action classes.
  // Do not query ancestry against a bad type for an ordinary GL action.
  const SoType boxType = SoBoxHighlightRenderAction::getClassTypeId();
  const SoType lineType = SoLineHighlightRenderAction::getClassTypeId();
  const bool box = !boxType.isBad() && sourceAction->isOfType(boxType);
  const bool line = !lineType.isBad() && sourceAction->isOfType(lineType);
  if (!box && !line) return true;
  auto * boxAction = box ? static_cast<SoBoxHighlightRenderAction *>(sourceAction) : nullptr;
  auto * lineAction = line ? static_cast<SoLineHighlightRenderAction *>(sourceAction) : nullptr;
  if (!(box ? boxAction->isVisible() : lineAction->isVisible())) return true;
  SoSearchAction search;
  search.setType(SoSelection::getClassTypeId());
  search.setInterest(SoSearchAction::FIRST);
  search.apply(root);
  SoPath * selectionPath = search.getPath();
  if (!selectionPath) return true;
  auto * selection = static_cast<SoSelection *>(selectionPath->fullPath().getTail());
  if (!selection->getNumSelected()) return true;
  const auto fail = [&](CoinRenderAction::Status status, const char * message) {
    this->status = status; this->error = message; return false;
  };
  // Instrumented C++ subclasses retaining the native runtime type are valid.
  if (sourceAction->getTypeId() != (box ? boxType : lineType))
    return fail(CoinRenderAction::UNSUPPORTED, "Custom GL highlight actions require explicit delegation");
  if (this->source->getStereoMode() != SoRenderManager::MONO ||
      this->source->getRenderMode() != SoRenderManager::AS_IS)
    return fail(CoinRenderAction::UNSUPPORTED, "Selection highlighting requires mono AS_IS manager rendering");
  if (this->renderer && this->renderer->getRenderTarget() &&
      this->renderer->getRenderTarget()->getOptions().sampleCount > 1 &&
      (this->renderer->getRenderTarget()->getOptions().transparency == COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT ||
       this->renderer->getRenderTarget()->getOptions().transparency == COIN_RENDER_TRANSPARENCY_PEELING ||
       sourceAction->getTransparencyType() == SoGLRenderAction::SORTED_LAYERS_BLEND))
    return fail(CoinRenderAction::UNSUPPORTED,
      "MSAA selection highlighting with OIT/peeling needs a separate composition contract");
  const SbColor color = box ? boxAction->getColor() : lineAction->getColor();
  float width = box ? boxAction->getLineWidth() : lineAction->getLineWidth();
  const uint16_t pattern = box ? boxAction->getLinePattern() : lineAction->getLinePattern();
  if (!std::isfinite(width) || !std::isfinite(color[0]) ||
      !std::isfinite(color[1]) || !std::isfinite(color[2]))
    return fail(CoinRenderAction::INVALID_SCENE, "Active highlight style must be finite");
  if (width <= 0.0f) width = 1.0f;
  for (int i = 0; i < selection->getNumSelected(); ++i) {
    const SoPath * relative = (*selection->getList())[i];
    if (!relative || relative->getHead() != selection)
      return fail(CoinRenderAction::INVALID_SCENE, "Selection path does not begin at its selection node");
    SoPath * full = selectionPath->copy();
    full->ref();
    const auto relativeFull = relative->fullPath();
    bool valid = true;
    for (int j = 1; j < relativeFull.getLength(); ++j) {
      SoNode * parent = full->fullPath().getTail();
      const SoChildList * children = parent->getChildren();
      const int index = highlightIndexFromHead(relativeFull, j);
      if (!children || index < 0 || index >= children->getLength() ||
          (*children)[index] != highlightNodeFromHead(relativeFull, j)) { valid = false; break; }
      full->append(index);
    }
    if (valid) this->highlightPaths.append(full);
    full->unref();
    if (!valid)
      return fail(CoinRenderAction::INVALID_SCENE, "Stale selection path cannot be replayed");
  }
  graph = new SoSeparator;
  graph->ref();
  SoDrawStyle * style = new SoDrawStyle;
  style->style = SoDrawStyle::LINES;
  style->lineWidth = width; style->linePattern = pattern;
  style->pointSize.setIgnored(TRUE);
  style->setOverride(TRUE);
  graph->addChild(style);
  SoBaseColor * rgb = new SoBaseColor;
  rgb->rgb = color; rgb->setOverride(TRUE); graph->addChild(rgb);
  SoLightModel * light = new SoLightModel;
  light->model = SoLightModel::BASE_COLOR; light->setOverride(TRUE); graph->addChild(light);
  SoComplexity * quality = new SoComplexity;
  quality->value.setIgnored(TRUE);
  // LINE replays the authored path inside this same frame. The quality
  // override disables inherited textures and lets sceneTexturePreCB take its
  // disabled branch, so only the base traversal captures RTT producers.
  // BOX adds bounds geometry instead of replaying the selected subtree.
  quality->textureQuality = 0.0f;
  if (box) quality->type = SoComplexity::BOUNDING_BOX;
  else quality->type.setIgnored(TRUE);
  quality->setOverride(TRUE); graph->addChild(quality);
  if (line) {
    SoMaterialBinding * binding = new SoMaterialBinding;
    binding->value = SoMaterialBinding::OVERALL; binding->setOverride(TRUE); graph->addChild(binding);
    SoNormal * normal = new SoNormal;
    normal->vector.setNum(0); normal->setOverride(TRUE); graph->addChild(normal);
    SoPolygonOffset * offset = new SoPolygonOffset;
    offset->factor = -1.0f; offset->units = -1.0f;
    offset->styles = SoPolygonOffset::LINES;
    offset->setOverride(TRUE); graph->addChild(offset);
  }
  for (int i = 0; i < this->highlightPaths.getLength(); ++i) {
    SoPath * path = this->highlightPaths[i];
    SoSeparator * occurrence = new SoSeparator;
    graph->addChild(occurrence);
    if (line) {
      SoCallback * replay = new SoCallback;
      replay->setCallback(P::replayPath, this);
      occurrence->addChild(replay);
      this->highlightReplays.emplace(replay, path);
    } else {
      SoSearchAction camera;
      camera.setType(SoCamera::getClassTypeId()); camera.setInterest(SoSearchAction::FIRST);
      camera.apply(path);
      if (camera.getPath()) occurrence->addChild(camera.getPath()->getTail());
      SoGetBoundingBoxAction bounds(viewport);
      bounds.apply(path);
      SbXfBox3f boundsBox = bounds.getXfBoundingBox();
      if (boundsBox.isEmpty()) continue;
      float x, y, z; boundsBox.getSize(x, y, z);
      SbMatrix transform = boundsBox.getTransform();
      SbMatrix center; center.setTranslate(boundsBox.SbBox3f::getCenter());
      transform.multLeft(center);
      bool finite = std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
      for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
        finite = finite && std::isfinite(transform[row][column]);
      if (!finite) {
        graph->unref(); graph = nullptr;
        return fail(CoinRenderAction::INVALID_SCENE, "Selection bounding box must be finite");
      }
      SoMatrixTransform * matrix = new SoMatrixTransform;
      matrix->matrix = transform; occurrence->addChild(matrix);
      SoCube * cube = new SoCube;
      cube->width = x; cube->height = y; cube->depth = z; occurrence->addChild(cube);
    }
  }
  return true;
}

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

CoinRenderManagerAdapter::CoinRenderManagerAdapter(
  SoRenderManager & source, const SbVec2i32 & offscreenSize, const CoinRenderOptions & options)
  : pimpl(new P)
{
  ensureWgpuRenderActionInitialized();
  this->pimpl->source = &source;
  this->pimpl->renderer = new CoinRenderSceneManager(offscreenSize, options);
  this->syncFromRenderManager();
}

CoinRenderManagerAdapter::CoinRenderManagerAdapter(
  SoRenderManager & source,
  const CoinRenderNativeSurfaceDescriptor & nativeWindow,
  const SbVec2i32 & framebufferSize, const CoinRenderOptions & options)
  : pimpl(new P)
{
  ensureWgpuRenderActionInitialized();
  this->pimpl->source = &source;
  this->pimpl->renderer = new CoinRenderSceneManager(nativeWindow, framebufferSize, options);
  this->syncFromRenderManager();
}

CoinRenderManagerAdapter::~CoinRenderManagerAdapter()
{
  if (this->pimpl->sceneGraphOverride) this->pimpl->sceneGraphOverride->unref();
  delete this->pimpl->renderer;
  if (this->pimpl->composedRoot) this->pimpl->composedRoot->unref();
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
  if (this->pimpl->source->getStereoMode() != SoRenderManager::MONO &&
      this->pimpl->source->getStereoMode() != SoRenderManager::ANAGLYPH) {
    this->pimpl->status = CoinRenderAction::UNSUPPORTED;
    this->pimpl->error = "CoinRender manager stereo mode is not implemented";
    return FALSE;
  }
  if (!this->pimpl->source->isRGBMode()) {
    this->pimpl->status = CoinRenderAction::UNSUPPORTED;
    this->pimpl->error = "CoinRender manager color-index mode is not implemented";
    return FALSE;
  }
  CoinRenderAction::DrawMode drawMode;
  switch (this->pimpl->source->getRenderMode()) {
  case SoRenderManager::AS_IS: drawMode = CoinRenderAction::DrawMode::SCENE; break;
  case SoRenderManager::WIREFRAME: drawMode = CoinRenderAction::DrawMode::WIREFRAME; break;
  case SoRenderManager::POINTS: drawMode = CoinRenderAction::DrawMode::POINTS; break;
  case SoRenderManager::BOUNDING_BOX: drawMode = CoinRenderAction::DrawMode::BOUNDING_BOX; break;
  case SoRenderManager::WIREFRAME_OVERLAY: drawMode = CoinRenderAction::DrawMode::WIREFRAME_OVERLAY; break;
  case SoRenderManager::HIDDEN_LINE: drawMode = CoinRenderAction::DrawMode::HIDDEN_LINE; break;
  case SoRenderManager::SHADED_HIDDEN_LINES: drawMode = CoinRenderAction::DrawMode::SHADED_HIDDEN_LINES; break;
  default:
    this->pimpl->status = CoinRenderAction::UNSUPPORTED;
    this->pimpl->error = "CoinRender manager render mode is not implemented";
    return FALSE;
  }
  SoNode * mainRoot = this->pimpl->sceneGraphOverride
    ? this->pimpl->sceneGraphOverride : this->pimpl->source->getSceneGraph();
  std::vector<std::tuple<SoNode *, int, int>> activeLayers;
  for (int i = 0; i < this->pimpl->source->getNumSuperimpositions(); ++i) {
    const auto * layer = this->pimpl->source->getSuperimposition(i);
    if (!layer || !layer->isEnabled()) continue;
    const int flags = layer->getStateFlags();
    const bool technicalForeground =
      (drawMode == CoinRenderAction::DrawMode::HIDDEN_LINE ||
       drawMode == CoinRenderAction::DrawMode::SHADED_HIDDEN_LINES ||
       drawMode == CoinRenderAction::DrawMode::WIREFRAME ||
       drawMode == CoinRenderAction::DrawMode::POINTS ||
       drawMode == CoinRenderAction::DrawMode::BOUNDING_BOX ||
       drawMode == CoinRenderAction::DrawMode::WIREFRAME_OVERLAY) &&
      (flags & SoRenderManager::Superimposition::ZBUFFERON) &&
      !(flags & SoRenderManager::Superimposition::BACKGROUND) &&
      !(flags & ~(SoRenderManager::Superimposition::ZBUFFERON |
                  SoRenderManager::Superimposition::CLEARZBUFFER |
                  SoRenderManager::Superimposition::AUTOREDRAW));
    if ((drawMode != CoinRenderAction::DrawMode::SCENE && !technicalForeground) ||
        !layer->getSceneGraph() || !mainRoot) {
      this->pimpl->status = CoinRenderAction::UNSUPPORTED;
      this->pimpl->error = "CoinRender manager superimposition cannot use this draw mode or an empty scene";
      return FALSE;
    }
    const int transparency = layer->inheritsTransparencyType()
      ? -1 : static_cast<int>(layer->getTransparencyType());
    activeLayers.emplace_back(layer->getSceneGraph(), flags, transparency);
  }
  SoSeparator * highlight = nullptr;
  if (!this->pimpl->prepareHighlight(mainRoot, this->pimpl->source->getGLRenderAction(),
                                    this->pimpl->source->getViewportRegion(), highlight)) return FALSE;
  SoSeparator * composed = nullptr;
  std::vector<std::tuple<SoNode *, bool, int>> managerBoundaries;
  if (highlight || !activeLayers.empty()) {
    // Highlight paths and style are rebuilt at each sync, including changes
    // that do not notify the scene graph (selection and GL-action setters).
    if (!highlight && this->pimpl->highlightReplays.empty() &&
        this->pimpl->composedRoot && this->pimpl->composedMain == mainRoot &&
        this->pimpl->composedLayers == activeLayers && !this->pimpl->composedHighlight) {
      composed = static_cast<SoSeparator *>(this->pimpl->composedRoot);
    }
    else {
      composed = new SoSeparator;
      composed->ref();
      auto appendLayer = [&](const std::tuple<SoNode *, int, int> & layer) {
        SoSeparator * overlay = new SoSeparator;
        SoDepthBuffer * depth = new SoDepthBuffer;
        const SbBool depthEnabled =
          (std::get<1>(layer) & SoRenderManager::Superimposition::ZBUFFERON) != 0;
        depth->test = depthEnabled;
        depth->write = depthEnabled;
        overlay->addChild(depth);
        overlay->addChild(std::get<0>(layer));
        composed->addChild(overlay);
        managerBoundaries.emplace_back(overlay,
          (std::get<1>(layer) & SoRenderManager::Superimposition::CLEARZBUFFER) != 0,
          std::get<2>(layer));
      };
      bool hasBackground = false;
      for (const auto & layer : activeLayers)
        if (std::get<1>(layer) & SoRenderManager::Superimposition::BACKGROUND) {
          hasBackground = true;
          appendLayer(layer);
        }
      if (hasBackground || highlight) {
        SoSeparator * basePass = new SoSeparator;
        basePass->addChild(mainRoot);
        composed->addChild(basePass);
        if (hasBackground) managerBoundaries.emplace_back(basePass, true, -2);
      }
      else composed->addChild(mainRoot);
      if (highlight) {
        composed->addChild(highlight);
        managerBoundaries.emplace_back(highlight, false, COIN_RENDER_MANAGER_HIGHLIGHT_LAYER);
      }
      for (const auto & layer : activeLayers)
        if (!(std::get<1>(layer) & SoRenderManager::Superimposition::BACKGROUND))
          appendLayer(layer);
    }
  }
  if (highlight) highlight->unref(); // The composed scene now owns it.
  this->pimpl->renderer->setSceneGraph(composed ? static_cast<SoNode *>(composed)
    : mainRoot);
  if (composed != this->pimpl->composedRoot) {
    if (this->pimpl->composedRoot) this->pimpl->composedRoot->unref();
    this->pimpl->composedRoot = composed;
    this->pimpl->composedMain = composed ? mainRoot : nullptr;
    this->pimpl->composedHighlight = highlight != nullptr;
    this->pimpl->composedLayers = std::move(activeLayers);
    this->pimpl->managerBoundaries = std::move(managerBoundaries);
  }
  this->pimpl->renderer->clearManagerLayerBoundaries();
  for (const auto & layer : this->pimpl->managerBoundaries)
    this->pimpl->renderer->addManagerLayerBoundary(
      std::get<0>(layer), std::get<1>(layer), std::get<2>(layer));
  this->pimpl->renderer->setBackgroundColor(this->pimpl->source->getBackgroundColor());
  this->pimpl->renderer->setViewportRegion(this->pimpl->source->getViewportRegion());
  // Preserve the Coin policy selected by the host; backend options choose the
  // GPU mechanism without changing the manager's transparency semantics.
  this->pimpl->renderer->setTransparencyType(
    static_cast<CoinRenderAction::TransparencyType>(
      this->pimpl->source->getGLRenderAction()->getTransparencyType()));
  this->pimpl->renderer->setDrawMode(drawMode);
  this->pimpl->renderer->setOverlayColor(this->pimpl->source->getOverlayColor());
  this->pimpl->renderer->setTexturesEnabled(this->pimpl->source->isTexturesEnabled());
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
  this->pimpl->status = this->pimpl->source->getStereoMode() == SoRenderManager::ANAGLYPH
    ? this->pimpl->renderer->renderAnaglyph(this->pimpl->source->getCamera(),
                                           this->pimpl->source->getStereoOffset())
    : this->pimpl->renderer->render();
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
