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
#include "rendering/coinrender/CoinRenderGuardedTraversal.h"
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/SoPath.h>
#include <Inventor/lists/SoPathList.h>
#include <Inventor/misc/SoChildList.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <cmath>
#include <exception>
#include <new>
#include <algorithm>
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
  bool composedMono = true;
  std::vector<std::tuple<SoNode *, int, int>> composedLayers;
  std::vector<std::tuple<SoNode *, bool, int>> managerBoundaries;
  CoinRenderSceneManager * renderer = nullptr;
  CoinRenderAction::Status status = CoinRenderAction::SUCCESS;
  SbString error;
  SoPathList highlightPaths;
  struct HighlightReplay {
    const SoPath * path;
    std::vector<SoNode *> nodes;
    std::vector<int> indices;
  };
  std::unordered_map<const SoNode *, HighlightReplay> highlightReplays;

  SoCallback * highlightTrampoline = nullptr;
  SoPath * activeReplayPath = nullptr;
  bool boxHighlight = false;

  P() {
    this->highlightTrampoline = new SoCallback;
    this->highlightTrampoline->ref();
    this->highlightTrampoline->setCallback(P::replayPath, this);
  }
  ~P() { this->highlightTrampoline->unref(); }
  static void replayPath(void * data, SoAction * action);
  bool prepareHighlight(SoNode * root, SoGLRenderAction * sourceAction,
                        SoSeparator * & graph);
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
  const auto & replay = found->second;
  SoPath * path = const_cast<SoPath *>(replay.path);
  const auto full = path->fullPath();
  if (full.getLength() != static_cast<int>(replay.nodes.size())) {
    static_cast<CoinRenderAction *>(action)->rejectInvalidScene("Selection path changed during capture");
    return;
  }
  for (int i = 0; i < full.getLength(); ++i) {
    if (highlightNodeFromHead(full, i) != replay.nodes[i] ||
        (i && highlightIndexFromHead(full, i) != replay.indices[i])) {
      static_cast<CoinRenderAction *>(action)->rejectInvalidScene("Selection path changed during capture");
      return;
    }
    if (!i) continue;
    const SoChildList * children = highlightNodeFromHead(full, i - 1)->getChildren();
    const int index = highlightIndexFromHead(full, i);
    if (!children || index < 0 || index >= children->getLength() ||
        (*children)[index] != highlightNodeFromHead(full, i)) {
      static_cast<CoinRenderAction *>(action)->rejectInvalidScene("Stale selection path cannot be replayed");
      return;
    }
  }
  if (p->boxHighlight) {
    // Bounds may depend on the active stereo camera, viewport or authored
    // callback state. Compute them in this eye's capture, after its base,
    // instead of baking them into the adapter's synchronization graph.
    CoinRenderGuardedTraversal<SoSearchAction> camera;
    camera.setType(SoCamera::getClassTypeId());
    camera.setInterest(SoSearchAction::FIRST);
    camera.apply(path);
    CoinRenderGuardedTraversal<SoGetBoundingBoxAction> bounds(static_cast<CoinRenderAction *>(action)->getViewportRegion());
    bounds.apply(path);
    const SbXfBox3f boundsBox = bounds.getXfBoundingBox();
    if (boundsBox.isEmpty()) return;
    float x, y, z;
    boundsBox.getSize(x, y, z);
    SbMatrix transform = boundsBox.getTransform();
    SbMatrix center;
    center.setTranslate(boundsBox.SbBox3f::getCenter());
    transform.multLeft(center);
    bool finite = std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    for (int row = 0; row < 4; ++row)
      for (int column = 0; column < 4; ++column)
        finite = finite && std::isfinite(transform[row][column]);
    if (!finite) {
      static_cast<CoinRenderAction *>(action)->rejectInvalidScene("Selection bounding box must be finite");
      return;
    }
    auto * box = new SoSeparator;
    box->ref();
    struct BoxScope { SoSeparator * root; ~BoxScope() { root->unref(); } } scope{box};
    if (camera.getPath()) box->addChild(camera.getPath()->getTail());
    auto * matrix = new SoMatrixTransform;
    matrix->matrix = transform;
    box->addChild(matrix);
    auto * cube = new SoCube;
    cube->width = x; cube->height = y; cube->depth = z;
    box->addChild(cube);
    action->switchToNodeTraversal(box);
    return;
  }
  p->activeReplayPath = path;
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
  this->boxHighlight = box;
  if (!box && !line) return true;
  auto * boxAction = box ? static_cast<SoBoxHighlightRenderAction *>(sourceAction) : nullptr;
  auto * lineAction = line ? static_cast<SoLineHighlightRenderAction *>(sourceAction) : nullptr;
  if (!(box ? boxAction->isVisible() : lineAction->isVisible())) return true;
  CoinRenderGuardedTraversal<SoSearchAction> search;
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
  // BOX computes bounds geometry in each current view instead of replaying
  // the selected subtree.
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
    SoCallback * replay = new SoCallback;
    replay->setCallback(P::replayPath, this);
    occurrence->addChild(replay);
    HighlightReplay captured;
    captured.path = path;
    const auto full = path->fullPath();
    for (int j = 0; j < full.getLength(); ++j) {
      captured.nodes.push_back(highlightNodeFromHead(full, j));
      captured.indices.push_back(j ? highlightIndexFromHead(full, j) : -1);
    }
    this->highlightReplays.emplace(replay, std::move(captured));
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
      this->pimpl->source->getStereoMode() != SoRenderManager::ANAGLYPH &&
      this->pimpl->source->getStereoMode() != SoRenderManager::INTERLEAVED_ROWS &&
      this->pimpl->source->getStereoMode() != SoRenderManager::INTERLEAVED_COLUMNS) {
    this->pimpl->status = CoinRenderAction::UNSUPPORTED;
    this->pimpl->error = "Hardware SEPARATE_OUTPUT/QUAD_BUFFER requires native left/right buffers; this single RGBA target has no hardware stereo capability";
    return FALSE;
  }
  if (!this->pimpl->source->isRGBMode()) {
    this->pimpl->status = CoinRenderAction::UNSUPPORTED;
    this->pimpl->error = "CoinRender manager color-index mode is not implemented";
    return FALSE;
  }
  SoGLRenderAction * sourceAction = this->pimpl->source->getGLRenderAction();
  if (sourceAction->getNumPasses() > 1 && sourceAction->isPassUpdate()) {
    this->pimpl->status = CoinRenderAction::UNSUPPORTED;
    this->pimpl->error = "Multipass passUpdate conflicts with atomic publication";
    return FALSE;
  }
  this->pimpl->renderer->setNumPasses(sourceAction->getNumPasses());
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
  const bool mono = this->pimpl->source->getStereoMode() == SoRenderManager::MONO;
  SoNode * mainRoot = this->pimpl->sceneGraphOverride
    ? this->pimpl->sceneGraphOverride : this->pimpl->source->getSceneGraph();
  const int layerCount = this->pimpl->source->getNumSuperimpositions();
  const SoRenderManager::Superimposition * firstBackground = nullptr;
  for (int i = 0; i < layerCount; ++i) {
    const auto * layer = this->pimpl->source->getSuperimposition(i);
    if (!layer || !(layer->getStateFlags() & SoRenderManager::Superimposition::BACKGROUND)) continue;
    if (!firstBackground) firstBackground = layer;
  }
  std::vector<std::tuple<SoNode *, int, int>> activeLayers;
  for (int i = 0; i < this->pimpl->source->getNumSuperimpositions(); ++i) {
    const auto * layer = this->pimpl->source->getSuperimposition(i);
    if (!layer || !layer->isEnabled()) continue;
    const int flags = layer->getStateFlags();
    if (!layer->getSceneGraph()) {
      this->pimpl->status = CoinRenderAction::INVALID_SCENE;
      this->pimpl->error = "CoinRender manager superimposition has no scene";
      return FALSE;
    }
    const int transparency = layer->inheritsTransparencyType()
      ? -1 : static_cast<int>(layer->getTransparencyType());
    activeLayers.emplace_back(layer->getSceneGraph(), flags, transparency);
  }
  // The first registered background consumes the legacy clearwindow flag
  // even if disabled; a later background or the base cannot reclaim it.
  // HIDDEN_LINE clears both buffers itself, including with a null base.
  const bool hiddenLineClear = drawMode == CoinRenderAction::DrawMode::HIDDEN_LINE;
  const bool hasColorClearOwner = hiddenLineClear || (firstBackground
    ? firstBackground->isEnabled() && firstBackground->getSceneGraph()
    : mainRoot != nullptr);
  if (mono && layerCount && !hasColorClearOwner) {
    this->pimpl->status = CoinRenderAction::UNSUPPORTED;
    this->pimpl->error = "CoinRender manager has no color-clear owner: persistent color LOAD is unavailable";
    return FALSE;
  }
  SoSeparator * highlight = nullptr;
  if (!this->pimpl->prepareHighlight(mainRoot, this->pimpl->source->getGLRenderAction(),
                                    highlight)) return FALSE;
  SoSeparator * composed = nullptr;
  std::vector<std::tuple<SoNode *, bool, int>> managerBoundaries;
  if (!mono || highlight || !activeLayers.empty() ||
      (mono && !mainRoot && layerCount && hiddenLineClear)) {
    // Highlight paths and style are rebuilt at each sync, including changes
    // that do not notify the scene graph (selection and GL-action setters).
    if (!highlight && this->pimpl->highlightReplays.empty() &&
        this->pimpl->composedRoot && this->pimpl->composedMain == mainRoot &&
        this->pimpl->composedLayers == activeLayers && !this->pimpl->composedHighlight &&
        this->pimpl->composedMono == mono) {
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
        // Disabling GL_DEPTH_TEST suppresses writes without changing its
        // write mask. A child may reenable testing with write left ignored.
        depth->write.setIgnored(TRUE);
        overlay->addChild(depth);
        overlay->addChild(std::get<0>(layer));
        composed->addChild(overlay);
        managerBoundaries.emplace_back(overlay,
          (std::get<1>(layer) & SoRenderManager::Superimposition::CLEARZBUFFER) != 0,
          std::get<2>(layer));
      };
      for (const auto & layer : activeLayers)
        if (std::get<1>(layer) & SoRenderManager::Superimposition::BACKGROUND) {
          appendLayer(layer);
        }
      // The base has an explicit authored occurrence in every view. BG and FG
      // remain separate authored traversals around the stereo composition.
      {
        SoSeparator * basePass = new SoSeparator;
        if (mainRoot) basePass->addChild(mainRoot);
        composed->addChild(basePass);
        managerBoundaries.emplace_back(basePass, true, COIN_RENDER_MANAGER_BASE_LAYER);
      }
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
    this->pimpl->composedMono = mono;
    this->pimpl->composedLayers = std::move(activeLayers);
    this->pimpl->managerBoundaries = std::move(managerBoundaries);
  }
  this->pimpl->renderer->clearManagerLayerBoundaries();
  this->pimpl->renderer->addManagerLayerBoundary(nullptr, FALSE, COIN_RENDER_MANAGER_OWNER);
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
  // Synchronization reports this configuration, not a previous traversal's
  // diagnostic. A rejected/throwing authored frame leaves a READY target
  // available for a fresh capture on the next public render.
  if (this->pimpl->renderer->getRenderTarget()->getStatus() == CoinRenderTarget::TARGET_ERROR) {
    this->pimpl->status = this->pimpl->renderer->getLastStatus();
    this->pimpl->error = this->pimpl->renderer->getLastError();
    return FALSE;
  }
  this->pimpl->status = CoinRenderAction::SUCCESS;
  this->pimpl->error = "";
  return TRUE;
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
  try {
    if (!this->syncFromRenderManager()) return this->pimpl->status;
    this->pimpl->status = this->pimpl->source->getStereoMode() != SoRenderManager::MONO
      ? this->pimpl->renderer->renderStereo(this->pimpl->source->getCamera(),
                                           this->pimpl->source->getStereoOffset(),
                                           this->pimpl->source->getStereoMode())
      : this->pimpl->renderer->render();
    this->pimpl->error = this->pimpl->renderer->getLastError();
  } catch (const std::bad_alloc &) {
    this->pimpl->status = CoinRenderAction::OUT_OF_MEMORY;
    this->pimpl->error = "Manager frame allocation failed";
  } catch (const std::exception & error) {
    this->pimpl->status = CoinRenderAction::BACKEND_ERROR;
    this->pimpl->error = error.what();
  } catch (...) {
    this->pimpl->status = CoinRenderAction::BACKEND_ERROR;
    this->pimpl->error = "Manager authored traversal threw an exception";
  }
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
