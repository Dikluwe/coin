// src/actions/SoIRRenderAction.cpp

#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGetMatrixAction.h>

#include "rendering/SoRenderPlan.h"

#include <Inventor/SoPath.h>
#include <Inventor/SoDB.h>
#include <Inventor/C/tidbits.h>
#include <Inventor/SbBasic.h>
#include <Inventor/errors/SoDebugError.h>
#include <Inventor/elements/SoDepthBufferElement.h>
#include <Inventor/elements/SoDrawStyleElement.h>
#include <Inventor/elements/SoLineWidthElement.h>
#include <Inventor/elements/SoLinePatternElement.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoEnvironmentElement.h>
#include <Inventor/elements/SoLightAttenuationElement.h>
#include <Inventor/elements/SoClipPlaneElement.h>
#include "elements/SoRenderDepthPolicyElement.h"
#include <Inventor/elements/SoLightElement.h>
#include <Inventor/elements/SoLightModelElement.h>
#include <Inventor/elements/SoMaterialBindingElement.h>
#include "elements/SoPBRMaterialElement.h"
#include <Inventor/elements/SoNormalBindingElement.h>
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoOverrideElement.h>
#include <Inventor/elements/SoPolygonOffsetElement.h>
#include <Inventor/elements/SoShapeStyleElement.h>
#include <Inventor/elements/SoBumpMapCoordinateElement.h>
#include <Inventor/elements/SoBumpMapElement.h>
#include <Inventor/elements/SoBumpMapMatrixElement.h>
#include <Inventor/elements/SoMultiTextureEnabledElement.h>
#include <Inventor/elements/SoCacheElement.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/elements/SoViewVolumeElement.h>
#include <Inventor/elements/SoProjectionMatrixElement.h>
#include <Inventor/elements/SoDevicePixelRatioElement.h>
#include <Inventor/elements/SoMultiTextureImageElement.h>
#include <Inventor/elements/SoMultiTextureMatrixElement.h>
#include <Inventor/elements/SoCoordinateElement.h>
#include <Inventor/elements/SoNormalElement.h>
#include <Inventor/elements/SoCreaseAngleElement.h>
#include <Inventor/elements/SoComplexityElement.h>
#include <Inventor/elements/SoComplexityTypeElement.h>
#include <Inventor/elements/SoMultiTextureCoordinateElement.h>
#include <Inventor/elements/SoProfileElement.h>
#include <Inventor/elements/SoProfileCoordinateElement.h>
#include <Inventor/elements/SoTextureQualityElement.h>
#include <Inventor/elements/SoTextureCoordinateBindingElement.h>
#include <Inventor/elements/SoTextureUnitElement.h>
#include <Inventor/elements/SoSwitchElement.h>
#include <Inventor/elements/SoUnitsElement.h>
#include <Inventor/elements/SoShapeHintsElement.h>
#include <Inventor/elements/SoFocalDistanceElement.h>
#include <Inventor/elements/SoFontNameElement.h>
#include <Inventor/elements/SoFontSizeElement.h>
#include <Inventor/elements/SoDecimationPercentageElement.h>
#include <Inventor/elements/SoDecimationTypeElement.h>
#include <Inventor/elements/SoTextureOverrideElement.h>
#include <Inventor/elements/SoPointSizeElement.h>
#include <Inventor/elements/SoRenderMatrixPolicyElement.h>
#include <Inventor/elements/SoPickStyleElement.h>
#include <Inventor/nodes/SoShaderProgram.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/lists/SoPathList.h>

#include "actions/SoSubActionP.h"
#include "elements/SoRenderPlacementElement.h"
#include "rendering/SoRenderIRP.h"

#include <cassert>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

SO_ACTION_SOURCE(SoIRRenderAction);

class SoIRRenderActionP {
public:
  struct PathRecord {
    uint32_t first = 0;
  };

  struct TextureStorage {
    const unsigned char * source = nullptr;
    size_t bytes = 0;
    int width = 0;
    int height = 0;
    int numComponents = 0;
    const unsigned char * copy = nullptr;
    bool hasTransparency = false;
  };

  struct DependencyLink {
    uint32_t first, last, next;
  };

  // A single flat index both owns path nodes and locates dependency ranges.
  // Separate arrays avoid per-node allocation and pointer/uint32 padding.
  std::vector<SoNode *> nodeIndex;
  std::vector<uint32_t> dependencyHeads;
  size_t nodeSlot(SoNode * node) const {
    size_t slot=(reinterpret_cast<uintptr_t>(node)>>4)&(nodeIndex.size()-1);
    while (nodeIndex[slot] && nodeIndex[slot]!=node) slot=(slot+1)&(nodeIndex.size()-1);
    return slot;
  }
  void rehashNodeIndex(size_t capacity) {
    std::vector<SoNode *> nodes(capacity,nullptr);
    std::vector<uint32_t> heads(capacity,uint32_t(-1));
    for (size_t i=0;i<nodeIndex.size();++i) if (nodeIndex[i]) {
      size_t slot=(reinterpret_cast<uintptr_t>(nodeIndex[i])>>4)&(capacity-1);
      while (nodes[slot]) slot=(slot+1)&(capacity-1);
      nodes[slot]=nodeIndex[i];heads[slot]=dependencyHeads[i];
    }
    nodeIndex.swap(nodes);dependencyHeads.swap(heads);
  }
  uint32_t & retainPathNode(SoNode * node) {
    if (nodeIndex.empty()) rehashNodeIndex(16);
    size_t slot=nodeSlot(node);
    if (!nodeIndex[slot]) {
      if ((ownedPathNodeCount+1)*2>nodeIndex.size()) {
        rehashNodeIndex(nodeIndex.size()*2);slot=nodeSlot(node);
      }
      if(!recordBranchDependencies)replayOwnedPathNodes.push_back(node);
      node->ref();++ownedPathNodeCount;
      nodeIndex[slot]=node;dependencyHeads[slot]=uint32_t(-1);
    }
    return dependencyHeads[slot];
  }
  uint32_t dependencyHead(SoNode * node) const {
    return nodeIndex.empty() ? uint32_t(-1) : dependencyHeads[nodeSlot(node)];
  }
  void erasePathNode(SoNode * node) {
    size_t slot=nodeSlot(node);
    if (!nodeIndex[slot]) return;
    nodeIndex[slot]=nullptr;dependencyHeads[slot]=uint32_t(-1);
    // Reinsert the probe cluster after deleting a speculative replay node.
    for (slot=(slot+1)&(nodeIndex.size()-1);nodeIndex[slot];slot=(slot+1)&(nodeIndex.size()-1)) {
      SoNode * moved=nodeIndex[slot];const uint32_t head=dependencyHeads[slot];
      nodeIndex[slot]=nullptr;dependencyHeads[slot]=uint32_t(-1);
      const size_t destination=nodeSlot(moved);
      nodeIndex[destination]=moved;dependencyHeads[destination]=head;
    }
  }

  SoIRRenderActionP() = default;

  SoIRBuffer geometryPool;
  std::vector<TextureStorage> textureStorage;
  std::unordered_map<uint64_t, float> textureUploadQualities;
  SbList<SoIRRenderAction::PrimitiveCollector *> collectorStack;
  std::unordered_multimap<uint64_t, SoGeometryHandle> geometrySources;
  bool constructionTimingEnabled = false;
  SoIRRenderAction::ConstructionStatistics constructionStatistics;
  std::vector<bool> commandAuthoredVisibility;
  std::vector<PathRecord> commandPathRecords;
  std::vector<SoNode *> pathNodes;
  std::vector<int> pathIndices;
  size_t ownedPathNodeCount = 0;
  std::vector<SoNode *> replayOwnedPathNodes;
  std::vector<SoPath *> worldStatePaths;
  SoNode * projectionCamera = nullptr;
  uint64_t projectionCameraRevision = 0;
  bool multipleProjectionCameras = false;
  // A changed state node affects commands below its parent branch. Indexing
  // that branch keeps incremental updates proportional to the affected
  // subtree instead of the size of the complete retained frame. Commands are
  // still checked against their full path because a node can be instanced in
  // more than one scene-graph location.
  uint32_t trimmedGeneration=uint32_t(-1);
  std::vector<DependencyLink> branchDependencyLinks;
  bool recordBranchDependencies = true;
  SoRenderStage renderStage = SoRenderStage::Main;
  SoIRRenderContext renderContextOverride;
  bool hasRenderContextOverride = false;

};

#define PRIVATE(obj) ((obj)->pimpl)

class SoRetainedMutationTransaction {
public:
  explicit SoRetainedMutationTransaction(SoIRRenderAction & action)
    : action(action),
      drawListCheckpoint(action.drawlist.createMutationCheckpoint()),
      commandPathCacheCount(action.commandPaths.size()),
      pathRecordCount(PRIVATE(&action)->commandPathRecords.size()),
      pathNodeCount(PRIVATE(&action)->pathNodes.size()),
      ownedPathNodeCount(PRIVATE(&action)->ownedPathNodeCount),
      unsupported(action.unsupportedRendering),
      unsupportedNode(action.unsupportedNode),
      unsupportedReason(action.unsupportedReason),
      geometryCheckpoint(PRIVATE(&action)->geometryPool.checkpoint()),
      recordingDependencies(PRIVATE(&action)->recordBranchDependencies)
  {
    PRIVATE(&this->action)->recordBranchDependencies = false;
  }

  ~SoRetainedMutationTransaction()
  {
    if (this->active) this->finish(false);
  }

  int originalCommandCount() const
  { return this->drawListCheckpoint.commandCount; }
  size_t originalPathCount() const { return this->pathRecordCount; }

  void finish(bool keepAllocations)
  {
    if (!this->active) return;
    PRIVATE(&this->action)->recordBranchDependencies =
      this->recordingDependencies;
    this->action.drawlist.restoreMutationCheckpoint(this->drawListCheckpoint);
    while (this->action.commandPaths.size() > this->commandPathCacheCount) {
      SoPath * path = this->action.commandPaths.back();
      if (path) path->unref();
      this->action.commandPaths.pop_back();
    }
    PRIVATE(&this->action)->commandPathRecords.resize(this->pathRecordCount);
    PRIVATE(&this->action)->pathNodes.resize(this->pathNodeCount);
    PRIVATE(&this->action)->pathIndices.resize(this->pathNodeCount);
    while (PRIVATE(&this->action)->ownedPathNodeCount >
           this->ownedPathNodeCount) {
      SoNode * node = PRIVATE(&this->action)->replayOwnedPathNodes.back();
      PRIVATE(&this->action)->erasePathNode(node);
      if (node) node->unref();
      PRIVATE(&this->action)->replayOwnedPathNodes.pop_back();
      --PRIVATE(&this->action)->ownedPathNodeCount;
    }
    PRIVATE(&this->action)->commandAuthoredVisibility.resize(
      this->pathRecordCount);
    for (auto it = PRIVATE(&this->action)->geometrySources.begin();
         it != PRIVATE(&this->action)->geometrySources.end();) {
      if (it->second > static_cast<SoGeometryHandle>(
            this->drawListCheckpoint.geometryResourceCount)) {
        it = PRIVATE(&this->action)->geometrySources.erase(it);
      }
      else ++it;
    }
    this->action.unsupportedRendering = this->unsupported;
    this->action.unsupportedNode = this->unsupportedNode;
    this->action.unsupportedReason = this->unsupportedReason;
    if (!keepAllocations) {
      PRIVATE(&this->action)->geometryPool.rewind(this->geometryCheckpoint);
    }
    this->active = false;
  }

private:
  SoIRRenderAction & action;
  SoDrawList::MutationCheckpoint drawListCheckpoint;
  size_t commandPathCacheCount;
  size_t pathRecordCount;
  size_t pathNodeCount;
  size_t ownedPathNodeCount;
  bool unsupported;
  const SoNode * unsupportedNode;
  const char * unsupportedReason;
  SoIRBuffer::Checkpoint geometryCheckpoint;
  bool recordingDependencies;
  bool active = true;
};

void
SoIRRenderAction::initClass(void)
{
  SO_ACTION_INTERNAL_INIT_CLASS(SoIRRenderAction, SoAction);

  if (SoCacheElement::getClassTypeId() == SoType::badType()) {
    SoCacheElement::initClass();
  }
  SO_ACTION_ADD_METHOD_INTERNAL(SoNode, SoNode::IRRenderS);
  if (SoDevicePixelRatioElement::getClassTypeId() == SoType::badType())
    SoDevicePixelRatioElement::initClass();
  if (SoRenderMatrixPolicyElement::getClassTypeId() == SoType::badType())
    SoRenderMatrixPolicyElement::initClass();
  if (SoRenderPlacementElement::getClassTypeId() == SoType::badType()) {
    SoRenderPlacementElement::initClass();
  }

  SO_ENABLE(SoIRRenderAction, SoViewportRegionElement);
  SO_ENABLE(SoIRRenderAction, SoRenderPlacementElement);
  SO_ENABLE(SoIRRenderAction, SoRenderMatrixPolicyElement);
  SO_ENABLE(SoIRRenderAction, SoDevicePixelRatioElement);
  SO_ENABLE(SoIRRenderAction, SoViewVolumeElement);
  SO_ENABLE(SoIRRenderAction, SoViewingMatrixElement);
  SO_ENABLE(SoIRRenderAction, SoProjectionMatrixElement);
  SO_ENABLE(SoIRRenderAction, SoMultiTextureImageElement);
  SO_ENABLE(SoIRRenderAction, SoMultiTextureMatrixElement);
  SO_ENABLE(SoIRRenderAction, SoOverrideElement);
  SO_ENABLE(SoIRRenderAction, SoModelMatrixElement);
  SO_ENABLE(SoIRRenderAction, SoLazyElement);
  SO_ENABLE(SoIRRenderAction, SoPBRMaterialElement);
  SoRenderDepthPolicyElement::initClass();
  SO_ENABLE(SoIRRenderAction, SoRenderDepthPolicyElement);
  SO_ENABLE(SoIRRenderAction, SoDepthBufferElement);
  SO_ENABLE(SoIRRenderAction, SoDrawStyleElement);
  SO_ENABLE(SoIRRenderAction, SoLineWidthElement);
  SO_ENABLE(SoIRRenderAction, SoLinePatternElement);
  SO_ENABLE(SoIRRenderAction, SoPolygonOffsetElement);
  SO_ENABLE(SoIRRenderAction, SoShapeStyleElement);
  SO_ENABLE(SoIRRenderAction, SoLightModelElement);
  SO_ENABLE(SoIRRenderAction, SoLightElement);
  SO_ENABLE(SoIRRenderAction, SoClipPlaneElement);
  SO_ENABLE(SoIRRenderAction, SoEnvironmentElement);
  SO_ENABLE(SoIRRenderAction, SoLightAttenuationElement);
  SO_ENABLE(SoIRRenderAction, SoMaterialBindingElement);
  SO_ENABLE(SoIRRenderAction, SoNormalBindingElement);
  SO_ENABLE(SoIRRenderAction, SoCacheElement);
  SO_ENABLE(SoIRRenderAction, SoBumpMapCoordinateElement);
  SO_ENABLE(SoIRRenderAction, SoBumpMapElement);
  SO_ENABLE(SoIRRenderAction, SoBumpMapMatrixElement);
  SO_ENABLE(SoIRRenderAction, SoMultiTextureEnabledElement);

  // Elements needed by generatePrimitives() fallback in SoShape::render()
  SO_ENABLE(SoIRRenderAction, SoCoordinateElement);
  SO_ENABLE(SoIRRenderAction, SoNormalElement);
  SO_ENABLE(SoIRRenderAction, SoCreaseAngleElement);
  SO_ENABLE(SoIRRenderAction, SoComplexityElement);
  SO_ENABLE(SoIRRenderAction, SoComplexityTypeElement);
  SO_ENABLE(SoIRRenderAction, SoMultiTextureCoordinateElement);
  SO_ENABLE(SoIRRenderAction, SoProfileElement);
  SO_ENABLE(SoIRRenderAction, SoProfileCoordinateElement);
  SO_ENABLE(SoIRRenderAction, SoTextureCoordinateBindingElement);
  SO_ENABLE(SoIRRenderAction, SoTextureQualityElement);
  SO_ENABLE(SoIRRenderAction, SoTextureUnitElement);
  SO_ENABLE(SoIRRenderAction, SoSwitchElement);
  SO_ENABLE(SoIRRenderAction, SoUnitsElement);

  // Scene state elements needed by standard nodes during traversal
  SO_ENABLE(SoIRRenderAction, SoShapeHintsElement);
  SO_ENABLE(SoIRRenderAction, SoFocalDistanceElement);
  SO_ENABLE(SoIRRenderAction, SoFontNameElement);
  SO_ENABLE(SoIRRenderAction, SoFontSizeElement);
  SO_ENABLE(SoIRRenderAction, SoPointSizeElement);
  SO_ENABLE(SoIRRenderAction, SoPickStyleElement);
  SO_ENABLE(SoIRRenderAction, SoDecimationPercentageElement);
  SO_ENABLE(SoIRRenderAction, SoDecimationTypeElement);
  SO_ENABLE(SoIRRenderAction, SoTextureOverrideElement);
}

SoIRRenderAction::SoIRRenderAction(const SbViewportRegion & vp)
  : SoAction(), vpRegion(vp), pimpl(new SoIRRenderActionP)
{
  SO_ACTION_CONSTRUCTOR(SoIRRenderAction);
}

SoIRRenderAction::~SoIRRenderAction()
{
  this->clearCommandPaths();
  delete PRIVATE(this);
  PRIVATE(this) = NULL;
}

void
SoIRRenderAction::setViewportRegion(const SbViewportRegion & vp)
{
  this->vpRegion = vp;
}

void
SoIRRenderAction::beginFrame()
{
  this->drawlist.clear();
  this->clearCommandPaths();
  this->resetFrameResources();
  this->unsupportedRendering = false;
  this->unsupportedNode = nullptr;
  this->unsupportedReason = nullptr;
}

void
SoIRRenderAction::addCommand(const SoRenderCommand & command)
{
  SoRenderCommand copy = command;
  this->addCommand(std::move(copy));
}

void
SoIRRenderAction::addCommand(SoRenderCommand && command)
{
  SoRenderCommand & retained = command;
  if (retained.objectId == 0) {
    const SoPath * currentPath = this->getCurPath();
    SoNode * tail = currentPath ? currentPath->getTail() : nullptr;
    if (tail) retained.objectId = tail->getNodeId();
  }

  if (!retained.geometry.hasBounds && retained.geometry.positions &&
      retained.geometry.vertexCount > 0) {
    const size_t stride = retained.geometry.vertexStride
      ? retained.geometry.vertexStride : sizeof(float) * 3;
    const char * position = reinterpret_cast<const char *>(
      retained.geometry.positions);
    SbVec3f minimum(std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max());
    SbVec3f maximum(-std::numeric_limits<float>::max(),
                    -std::numeric_limits<float>::max(),
                    -std::numeric_limits<float>::max());
    for (uint32_t i = 0; i < retained.geometry.vertexCount; ++i) {
      const float * vertex = reinterpret_cast<const float *>(
        position + static_cast<size_t>(i) * stride);
      for (int axis = 0; axis < 3; ++axis) {
        minimum[axis] = std::min(minimum[axis], vertex[axis]);
        maximum[axis] = std::max(maximum[axis], vertex[axis]);
      }
    }
    retained.geometry.boundsCenter.setValue(
      (minimum[0] + maximum[0]) * 0.5f,
      (minimum[1] + maximum[1]) * 0.5f,
      (minimum[2] + maximum[2]) * 0.5f);
    retained.geometry.hasBounds = TRUE;
  }
  SoState * state = this->getState();
  const SoIRRenderContext * context = this->getRenderContextOverride();
  if (context && context->hasLighting) {
    retained.lightingHandle = SoRenderIR::fillLightingFromState(
      state, this->drawlist, context->lighting);
  }
  else if (retained.lightingHandle == 0 && state) {
    retained.lightingHandle = SoRenderIR::fillLightingFromState(
      state, this->drawlist);
  }

  if (retained.geometry.cacheKey != 0) {
    retained.geometryHandle = this->findGeometrySource(
      retained.geometry.cacheKey, retained.geometry.revision);
    if (retained.geometryHandle == SO_INVALID_GEOMETRY_HANDLE) {
      SoGeometryResource resource;
      resource.geometry = retained.geometry;
      resource.sourceKey = retained.geometry.cacheKey;
      resource.revision = retained.geometry.revision;
      resource.elementRanges = retained.pick.elementRanges;
      retained.geometryHandle = this->drawlist.addGeometryResource(resource);
      retained.pick.elementRanges.clear();
      retained.pick.useResourceElementRanges = true;
      PRIVATE(this)->geometrySources.emplace(
        retained.geometry.cacheKey, retained.geometryHandle);
    }
    else {
      const SoGeometryResource * resource =
        this->drawlist.getGeometryResource(retained.geometryHandle);
      const auto rangesEqual = [](const SoRenderElementRange & lhs,
                                  const SoRenderElementRange & rhs) {
        return lhs.type == rhs.type && lhs.elementIndex == rhs.elementIndex &&
          lhs.drawStart == rhs.drawStart && lhs.drawCount == rhs.drawCount;
      };
      if (resource && resource->elementRanges.size() ==
                        retained.pick.elementRanges.size() &&
          std::equal(resource->elementRanges.begin(),
                     resource->elementRanges.end(),
                     retained.pick.elementRanges.begin(), rangesEqual)) {
        retained.pick.elementRanges.clear();
        retained.pick.useResourceElementRanges = true;
      }
    }
  }

  const int commandIndex = this->drawlist.getNumCommands();
  PRIVATE(this)->commandAuthoredVisibility.push_back(
    retained.state.raster.visible);
  this->drawlist.addCommand(std::move(retained));

  const SoPath * currentPath = this->getCurPath();
  // Store path entries contiguously and retain each distinct node once. A
  // complete SoPath is created lazily only when replay or picking requests it.
  SoIRRenderActionP::PathRecord pathRecord;
  pathRecord.first=static_cast<uint32_t>(PRIVATE(this)->pathNodes.size());
  if (currentPath) {
    const size_t length=static_cast<size_t>(currentPath->getFullLength());
    if (length>UINT32_MAX || PRIVATE(this)->pathNodes.size()>UINT32_MAX-length)
      throw std::length_error("Retained path index exceeds 32-bit storage");
    pathRecord.first=static_cast<uint32_t>(PRIVATE(this)->pathNodes.size());
    for (size_t i = 0; i < length; ++i) {
      SoNode * node = static_cast<SoNode *>(currentPath->nodes[i]);
      PRIVATE(this)->pathNodes.push_back(node);
      PRIVATE(this)->pathIndices.push_back(currentPath->indices[i]);
      uint32_t & head=PRIVATE(this)->retainPathNode(node);
      if (PRIVATE(this)->recordBranchDependencies && i+1<length) {
        auto & links=PRIVATE(this)->branchDependencyLinks;
        if (head!=uint32_t(-1) && links[head].last+1==static_cast<uint32_t>(commandIndex))
          links[head].last=static_cast<uint32_t>(commandIndex);
        else {
          if (links.size()>=UINT32_MAX)
            throw std::length_error("Retained dependency index exceeds 32-bit storage");
          SoIRRenderActionP::DependencyLink link={static_cast<uint32_t>(commandIndex),
                                                static_cast<uint32_t>(commandIndex),head};
          links.push_back(link);head=static_cast<uint32_t>(links.size()-1);
        }
      }
    }
  }
  PRIVATE(this)->commandPathRecords.push_back(pathRecord);
  assert(PRIVATE(this)->commandPathRecords.size() ==
         static_cast<size_t>(commandIndex) + 1);
}

void
SoIRRenderAction::markUnsupported(const SoNode * node, const char * reason)
{
  if (this->unsupportedRendering) return;
  this->unsupportedRendering = true;
  this->unsupportedNode = node;
  this->unsupportedReason = reason ? reason : "unsupported retained rendering semantics";
}

const SoPath *
SoIRRenderAction::getCommandPath(int commandIndex) const
{
  if (commandIndex < 0 ||
      static_cast<size_t>(commandIndex) >=
        PRIVATE(this)->commandPathRecords.size()) {
    return NULL;
  }
  const size_t index = static_cast<size_t>(commandIndex);
  if (index >= this->commandPaths.size()) {
    this->commandPaths.resize(index + 1, NULL);
  }
  if (this->commandPaths[index]) return this->commandPaths[index];

  const SoIRRenderActionP::PathRecord & record =
    PRIVATE(this)->commandPathRecords[index];
  const size_t end=index+1<PRIVATE(this)->commandPathRecords.size()
    ? PRIVATE(this)->commandPathRecords[index+1].first : PRIVATE(this)->pathNodes.size();
  const size_t length=end-record.first;
  if (length == 0) return NULL;
  SoPath * path = new SoPath(static_cast<int>(length));
  path->auditPath(FALSE);
  for (size_t i = 0; i < length; ++i) {
    const size_t entry = record.first + i;
    path->append(PRIVATE(this)->pathNodes[entry],
                 PRIVATE(this)->pathIndices[entry]);
  }
  path->ref();
  this->commandPaths[index] = path;
  return path;
}

void
SoIRRenderAction::apply(SoNode * root)
{
  this->beginFrame();
  inherited::apply(root);
}

void
SoIRRenderAction::apply(SoPath * path)
{
  this->beginFrame();
  inherited::apply(path);
}

void
SoIRRenderAction::apply(const SoPathList & pathlist, SbBool obeysrules)
{
  this->beginFrame();
  inherited::apply(pathlist, obeysrules);
}

void
SoIRRenderAction::initializeCameraState(CameraPolicy policy)
{
  if (policy != CameraPolicy::USE_CONFIGURED_CAMERA || !this->camera) {
    return;
  }

  SbViewportRegion cameraViewport = this->vpRegion;
  const SbViewVolume viewVolume =
    this->camera->getViewVolume(this->vpRegion, cameraViewport);
  SbMatrix viewingMatrix;
  SbMatrix projectionMatrix;
  viewVolume.getMatrices(viewingMatrix, projectionMatrix);
  SoViewportRegionElement::set(this->state, cameraViewport);
  SoViewVolumeElement::set(this->state, this->camera, viewVolume);
  SoViewingMatrixElement::set(this->state, this->camera, viewingMatrix);
  SoProjectionMatrixElement::set(this->state, this->camera, projectionMatrix);
}

void
SoIRRenderAction::traverseAdditionalRoot(SoNode * root, CameraPolicy policy)
{
  if (!root) return;
  this->traversalMethods->setUp();
  this->state->push();
  SoViewportRegionElement::set(this->state, this->vpRegion);
  SoDevicePixelRatioElement::set(this->state, this->devicePixelRatio);
  this->initializeCameraState(policy);
  SoRenderMatrixPolicyElement::set(
    this->state, nullptr,
    policy == CameraPolicy::CAMERA_IN_ROOT
      ? SoRenderMatrixPolicyElement::CAPTURE_CURRENT_MATRICES
      : SoRenderMatrixPolicyElement::INHERIT_CAMERA_MATRICES);
  this->switchToNodeTraversal(root);
  this->state->pop();
}

void
SoIRRenderAction::traverseAdditionalPath(SoPath * path)
{
  this->traverseAdditionalPathInternal(path, nullptr);
}

void
SoIRRenderAction::traverseAdditionalPath(SoPath * path,
                                         const SoIRRenderContext & context)
{
  this->traverseAdditionalPathInternal(path, &context);
}

void
SoIRRenderAction::traverseAdditionalPathInternal(
  SoPath * path, const SoIRRenderContext * context)
{
  if (!path) return;

  this->traversalMethods->setUp();
  const bool previousHasContext = PRIVATE(this)->hasRenderContextOverride;
  const SoIRRenderContext previousContext = PRIVATE(this)->renderContextOverride;
  PRIVATE(this)->hasRenderContextOverride = context != nullptr;
  if (context) {
    PRIVATE(this)->renderContextOverride = *context;
  }
  this->state->push();
  if (context) {
    // The path traversal reconstructs model state through its ancestors.
    context->applyToState(this->state, FALSE);
    SoRenderMatrixPolicyElement::set(
      this->state, nullptr,
      SoRenderMatrixPolicyElement::CAPTURE_CURRENT_MATRICES);
  }
  else {
    SoViewportRegionElement::set(this->state, this->vpRegion);
    SoDevicePixelRatioElement::set(this->state, this->devicePixelRatio);
    this->initializeCameraState(this->cameraPolicy);
    SoRenderMatrixPolicyElement::set(
      this->state, nullptr,
      this->cameraPolicy == CameraPolicy::CAMERA_IN_ROOT
        ? SoRenderMatrixPolicyElement::CAPTURE_CURRENT_MATRICES
        : SoRenderMatrixPolicyElement::INHERIT_CAMERA_MATRICES);
  }
  this->switchToPathTraversal(path);
  this->state->pop();
  PRIVATE(this)->hasRenderContextOverride = previousHasContext;
  if (previousHasContext) {
    PRIVATE(this)->renderContextOverride = previousContext;
  }
}

void SoIRRenderAction::recordTextureUploadQuality(uint64_t source, float quality) {
  if (PRIVATE(this)->recordBranchDependencies) PRIVATE(this)->textureUploadQualities[source] = quality;
}
float SoIRRenderAction::getTextureUploadQuality(uint64_t source, float fallback) const {
  const auto found = PRIVATE(this)->textureUploadQualities.find(source);
  return found == PRIVATE(this)->textureUploadQualities.end() ? fallback : found->second;
}

void
SoIRRenderAction::recordWorldStateDependency()
{
  if (!PRIVATE(this)->recordBranchDependencies || !this->getCurPath()) return;
  SoNode * tail = this->getCurPathTail();
  if (tail && tail->isOfType(SoCamera::getClassTypeId())) {
    if (PRIVATE(this)->projectionCamera) PRIVATE(this)->multipleProjectionCameras = true;
    else { PRIVATE(this)->projectionCamera = tail; PRIVATE(this)->projectionCameraRevision = tail->getNodeId(); }
  }
  SoPath * path = this->getCurPath()->copy();
  path->ref();
  try { PRIVATE(this)->worldStatePaths.push_back(path); }
  catch (...) { path->unref(); throw; }
}

SoRenderStage
SoIRRenderAction::getRenderStage() const
{
  return PRIVATE(this)->renderStage;
}

void
SoIRRenderAction::setRenderStage(const SoRenderStage stage)
{
  PRIVATE(this)->renderStage = stage;
}

const SoIRRenderContext *
SoIRRenderAction::getRenderContextOverride() const
{
  return PRIVATE(this)->hasRenderContextOverride
    ? &PRIVATE(this)->renderContextOverride : nullptr;
}

int
SoIRRenderAction::updateCommandMatricesForStatePath(const SoPath * statePath)
{
  std::vector<const SoPath *> statePaths(1, statePath);
  return this->updateCommandMatricesForStatePaths(statePaths);
}

int
SoIRRenderAction::updateCommandMatricesForStatePaths(
  const std::vector<const SoPath *> & statePaths)
{
  // State values captured in world/view space cannot be repaired by changing
  // the model alone. Resolve dependencies before modifying any command.
  for (const SoPath * changed : statePaths) {
    if (!changed || changed->getFullLength() < 2) return 0;
    for (const SoPath * source : PRIVATE(this)->worldStatePaths) {
      const int offset = source->getFullLength() && source->nodes[0] == changed->nodes[0] ? 0 : 1;
      const int prefix = changed->getFullLength() - 1 - offset;
      if (prefix <= 0 || source->getFullLength() <= prefix) continue;
      bool matches = source->indices[prefix] > changed->indices[changed->getFullLength()-1];
      for (int i = 0; matches && i < prefix; ++i)
        matches = source->nodes[i] == changed->nodes[offset+i] && source->indices[i] == changed->indices[offset+i];
      if (matches) return 0;
    }
  }
  std::vector<size_t> commandIndices;
  this->findCommandsAffectedByStatePaths(statePaths, commandIndices);
  SoGetMatrixAction matrixAction(this->vpRegion);
  std::vector<SbMatrix> replacements;
  replacements.reserve(commandIndices.size());
  for (size_t commandIndex : commandIndices) {
    const SoPath * commandPath =
      this->getCommandPath(static_cast<int>(commandIndex));
    if (!commandPath || !this->drawlist.isCommandVisible(static_cast<int>(commandIndex))) return 0;
    matrixAction.apply(const_cast<SoPath *>(commandPath));
    replacements.push_back(matrixAction.getMatrix());
  }
  for (size_t i = 0; i < commandIndices.size(); ++i) {
    SoRenderCommand command = this->drawlist.getCommandSnapshot(
      static_cast<int>(commandIndices[i]));
    command.modelMatrix = replacements[i];
    this->drawlist.setCommandForRetainedUpdate(static_cast<int>(commandIndices[i]), command);
  }
  return static_cast<int>(commandIndices.size());
}

SbBool
SoIRRenderAction::transformUpdateAffectsPlanning(
  const std::vector<const SoPath *> & statePaths) const
{
  std::vector<size_t> commandIndices;
  this->findCommandsAffectedByStatePaths(statePaths, commandIndices);
  for (size_t commandIndex : commandIndices) {
    const SoRenderCommand & command = this->drawlist.getCommandSnapshot(
      static_cast<int>(commandIndex));
    if (SoRenderCommandTraits::transformAffectsPlanning(command)) return TRUE;
  }
  return FALSE;
}

void
SoIRRenderAction::findCommandsAffectedByStatePaths(
  const std::vector<const SoPath *> & statePaths,
  std::vector<size_t> & commandIndices) const
{
  commandIndices.clear();
  std::vector<size_t> found;
  for (const SoPath * path : statePaths) {
    this->findCommandsAffectedByStatePath(path, found);
    commandIndices.insert(commandIndices.end(), found.begin(), found.end());
  }
  std::sort(commandIndices.begin(), commandIndices.end());
  commandIndices.erase(
    std::unique(commandIndices.begin(), commandIndices.end()),
    commandIndices.end());
}

void
SoIRRenderAction::findCommandsAffectedByStatePath(
  const SoPath * statePath, std::vector<size_t> & commandIndices) const
{
  commandIndices.clear();
  if (!statePath || statePath->getFullLength() < 2) return;
  // Additional-root traversal records paths below the scene root, while the
  // sensor path includes that root. A state node affects later siblings below
  // the same parent.
  // apply(root) includes its root; the fork manager's additional-root entry
  // omits it. Resolve the notification prefix against recorded path storage.
  const int statePathOffset = !PRIVATE(this)->pathNodes.empty() &&
    PRIVATE(this)->pathNodes.front() == statePath->nodes[0] ? 0 : 1;
  const int prefixLength =
    statePath->getFullLength() - 1 - statePathOffset;
  // Additional-root paths omit the applied root. Without one retained
  // ancestor below it, replay cannot reconstruct root-level sibling state.
  if (prefixLength <= 0) return;
  const int changedStateIndex = statePath->getFullLength() - 1;
  const int changedSiblingIndex = statePath->indices[changedStateIndex];
  SoNode * branchNode = static_cast<SoNode *>(
    statePath->nodes[statePathOffset + prefixLength - 1]);
  const uint32_t branchHead=PRIVATE(this)->dependencyHead(branchNode);
  if (branchHead==uint32_t(-1)) return;
  const uint32_t noDependency=uint32_t(-1);
  for (uint32_t linkIndex=branchHead; linkIndex!=noDependency;
       linkIndex=PRIVATE(this)->branchDependencyLinks[linkIndex].next) {
    const auto & range=PRIVATE(this)->branchDependencyLinks[linkIndex];
    for (uint32_t commandIndex=range.first; commandIndex<=range.last; ++commandIndex) {
    const SoPath * commandPath =
      this->getCommandPath(static_cast<int>(commandIndex));
    if (!commandPath || commandPath->getFullLength() <= prefixLength ||
        commandPath->indices[prefixLength] <= changedSiblingIndex) continue;
    bool matches = true;
    for (int i = 0; matches && i < prefixLength; ++i) {
      matches = commandPath->nodes[i] == statePath->nodes[statePathOffset + i] &&
        commandPath->indices[i] == statePath->indices[statePathOffset + i];
    }
    if (matches) commandIndices.push_back(commandIndex);
    }
  }
}

namespace {
struct ProjectionReplay {
  bool captured = false;
  SoCameraData camera;
  SbMatrix view, projection;
  SbViewportRegion viewport;
};
SoCallbackAction::Response captureProjection(void * data, SoCallbackAction * action, const SoNode *) {
  auto * replay = static_cast<ProjectionReplay *>(data);
  SoRenderIR::captureCameraFromState(action->getState(), replay->camera);
  replay->view = action->getViewingMatrix(); replay->projection = action->getProjectionMatrix();
  replay->viewport = action->getViewportRegion(); replay->captured = true;
  return SoCallbackAction::ABORT;
}

struct MaterialReplay {
  explicit MaterialReplay(int index) : materialIndex(index) {}
  int materialIndex;
  SoMaterialData material;
  bool captured = false;
};

SoCallbackAction::Response
captureEffectiveMaterial(void * data, SoCallbackAction * action,
                         const SoNode *)
{
  MaterialReplay * replay = static_cast<MaterialReplay *>(data);
  SoRenderIR::fillMaterialFromState(
    action->getState(), replay->material, replay->materialIndex);
  replay->captured = true;
  return SoCallbackAction::ABORT;
}
}

int SoIRRenderAction::updateCameraProjectionForStatePath(const SoPath * path) {
  if (!path || PRIVATE(this)->multipleProjectionCameras || path->getTail() != PRIVATE(this)->projectionCamera) return 0;
  ProjectionReplay replay;
  SoCallbackAction action(this->vpRegion);
  action.addPostTailCallback(captureProjection, &replay);
  action.apply(const_cast<SoPath *>(path));
  if (!replay.captured || replay.viewport != this->vpRegion || !replay.camera.sourceRevision ||
      !std::isfinite(replay.camera.nearDistance) || !std::isfinite(replay.camera.farDistance) ||
      replay.camera.farDistance <= replay.camera.nearDistance || !std::isfinite(replay.camera.focalDistance) ||
      !std::isfinite(replay.camera.aspectRatio) || replay.camera.aspectRatio <= 0) return 0;
  for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col)
    if (!std::isfinite(replay.view[row][col]) || !std::isfinite(replay.projection[row][col])) return 0;
  if (!this->drawlist.updateRetainedProjection(PRIVATE(this)->projectionCameraRevision, replay.view, replay.projection, replay.camera)) return 0;
  PRIVATE(this)->projectionCameraRevision = replay.camera.sourceRevision;
  return this->drawlist.getNumCommands();
}

int
SoIRRenderAction::updateCommandDiffuseColorsForStatePath(
  const SoPath * statePath)
{
  std::vector<const SoPath *> statePaths(1, statePath);
  return this->updateCommandDiffuseColorsForStatePaths(statePaths);
}

int
SoIRRenderAction::updateCommandDiffuseColorsForStatePaths(
  const std::vector<const SoPath *> & statePaths)
{
  std::vector<size_t> commandIndices;
  this->findCommandsAffectedByStatePaths(statePaths, commandIndices);
  std::vector<MaterialReplay> replacements;
  replacements.reserve(commandIndices.size());
  for (size_t commandIndex : commandIndices) {
    const SoPath * commandPath =
      this->getCommandPath(static_cast<int>(commandIndex));
    if (!commandPath) return 0;
    const SoRenderCommand & command =
      static_cast<const SoDrawList &>(this->drawlist).getCommandSnapshot(
        static_cast<int>(commandIndex));
    // Material-derived vertex colors require primitive regeneration. Updating
    // only the command's uniform diffuse would leave its captured colors stale.
    if (this->drawlist.getCommandGeometry(command).colors) return 0;
    MaterialReplay replay(command.materialIndex);
    SoCallbackAction materialAction(this->vpRegion);
    materialAction.addPreTailCallback(captureEffectiveMaterial, &replay);
    materialAction.apply(const_cast<SoPath *>(commandPath));
    if (!replay.captured || command.material.pbr || replay.material.pbr) return 0;
    replacements.push_back(replay);
  }
  for (size_t i = 0; i < commandIndices.size(); ++i) {
    SoRenderCommand command = this->drawlist.getCommandSnapshot(
      static_cast<int>(commandIndices[i]));
    const MaterialReplay & replay = replacements[i];
    command.material.diffuse[0] = replay.material.diffuse[0];
    command.material.diffuse[1] = replay.material.diffuse[1];
    command.material.diffuse[2] = replay.material.diffuse[2];
    this->drawlist.setCommandForRetainedUpdate(static_cast<int>(commandIndices[i]), command);
  }
  return static_cast<int>(commandIndices.size());
}

int SoIRRenderAction::updateCommandPBRMaterialsForStatePaths(const std::vector<const SoPath *> & paths) {
  std::vector<size_t> indices;findCommandsAffectedByStatePaths(paths,indices);
  std::vector<SoMaterialData> candidates; candidates.reserve(indices.size());
  for(auto index:indices) {
    const auto * path=getCommandPath(static_cast<int>(index));if(!path)return 0;
    const auto & command=static_cast<const SoDrawList &>(drawlist).getCommandSnapshot(static_cast<int>(index));
    if(!command.material.pbr || drawlist.getCommandGeometry(command).colors)return 0;
    MaterialReplay replay(command.materialIndex);SoCallbackAction stateReplay(vpRegion);
    stateReplay.addPreTailCallback(captureEffectiveMaterial,&replay);stateReplay.apply(const_cast<SoPath *>(path));
    if(!replay.captured || !replay.material.pbr || !so_valid_pbr_material(*replay.material.pbr))return 0;
    const auto & a=*command.material.pbr;const auto & b=*replay.material.pbr;
    // Role/UV changes require a new resource capture, not a uniform patch.
    if(a.baseColorTextureUnit!=b.baseColorTextureUnit || a.ormTextureUnit!=b.ormTextureUnit ||
       a.emissiveTextureUnit!=b.emissiveTextureUnit || a.tangentCoordinateUnit!=b.tangentCoordinateUnit ||
       (a.anisotropy==0 && b.anisotropy>0 && (!command.material.pbrSurface || !command.material.pbrSurface->directionCoordinatesCaptured)))return 0;
    candidates.push_back(std::move(replay.material));
  }
  for(size_t i=0;i<indices.size();++i) {
    auto command=drawlist.getCommandSnapshot(static_cast<int>(indices[i]));
    command.material.pbr=candidates[i].pbr;
    command.material.diffuse=candidates[i].diffuse;
    drawlist.setCommandForRetainedUpdate(static_cast<int>(indices[i]),command);
  }
  return static_cast<int>(indices.size());
}

int
SoIRRenderAction::updateCommandVisibilityForSwitchPath(
  const SoPath * switchPath, SbBool visible)
{
  if (!switchPath || switchPath->getFullLength() < 2) return 0;
  const int pathOffset = !PRIVATE(this)->pathNodes.empty() && PRIVATE(this)->pathNodes.front() == switchPath->nodes[0] ? 0 : 1;
  const int prefixLength = switchPath->getFullLength() - pathOffset;
  SoNode * switchNode = static_cast<SoNode *>(
    switchPath->nodes[switchPath->getFullLength() - 1]);
  const uint32_t branchHead=PRIVATE(this)->dependencyHead(switchNode);
  if (branchHead==uint32_t(-1)) return 0;

  int updated = 0;
  const uint32_t noDependency=uint32_t(-1);
  for (uint32_t linkIndex=branchHead; linkIndex!=noDependency;
       linkIndex=PRIVATE(this)->branchDependencyLinks[linkIndex].next) {
    const auto & range=PRIVATE(this)->branchDependencyLinks[linkIndex];
    for (uint32_t commandIndex=range.first; commandIndex<=range.last; ++commandIndex) {
    const SoPath * commandPath =
      this->getCommandPath(static_cast<int>(commandIndex));
    if (!commandPath || commandPath->getFullLength() <= prefixLength) continue;
    bool matches = true;
    for (int i = 0; matches && i < prefixLength; ++i) {
      matches = commandPath->nodes[i] == switchPath->nodes[pathOffset + i] &&
        commandPath->indices[i] == switchPath->indices[pathOffset + i];
    }
    if (!matches) continue;
    SoRenderCommand command =
      this->drawlist.getCommandSnapshot(
        static_cast<int>(commandIndex));
    command.state.raster.visible = visible &&
      PRIVATE(this)->commandAuthoredVisibility[commandIndex];
    this->drawlist.setCommandForRetainedUpdate(static_cast<int>(commandIndex), command);
    ++updated;
    }
  }
  return updated;
}

int
SoIRRenderAction::updateCommandGeometryForStatePath(
  const SoPath * statePath)
{
  std::vector<const SoPath *> statePaths(1, statePath);
  return this->updateCommandGeometryForStatePaths(statePaths);
}

int
SoIRRenderAction::updateCommandGeometryForStatePaths(
  const std::vector<const SoPath *> & statePaths)
{
  std::vector<size_t> commandIndices;
  this->findCommandsAffectedByStatePaths(statePaths, commandIndices);
  if (commandIndices.empty()) return 0;

  struct ResourceUpdate {
    SoGeometryHandle handle;
    size_t representativeCommand;
    SoGeometryResource original;
    SoGeometryResource replacement;
    std::vector<size_t> owners;
  };
  std::vector<ResourceUpdate> updates;
  std::unordered_map<SoGeometryHandle, size_t> updateByHandle;
  for (size_t commandIndex : commandIndices) {
    const SoRenderCommand & command =
      static_cast<const SoDrawList &>(this->drawlist).getCommandSnapshot(
        static_cast<int>(commandIndex));
    const SoGeometryHandle handle = command.geometryHandle;
    if (updateByHandle.find(handle) != updateByHandle.end()) continue;
    const SoGeometryResource * resource =
      this->drawlist.getGeometryResource(handle);
    if (!resource || !this->getCommandPath(static_cast<int>(commandIndex))) {
      return 0;
    }
    ResourceUpdate update = {
      handle, commandIndex, *resource, SoGeometryResource(), {}
    };
    updateByHandle.emplace(handle, updates.size());
    updates.push_back(update);
  }

  // A shared producer can appear more than once through the same parent.
  // SoPath cannot distinguish those notification occurrences, but each stable
  // handle does identify the complete set of commands that must move together.
  for (int commandIndex = 0;
       commandIndex < this->drawlist.getNumCommands(); ++commandIndex) {
    const SoGeometryHandle handle =
      static_cast<const SoDrawList &>(this->drawlist)
        .getCommandSnapshot(commandIndex).geometryHandle;
    const auto found = updateByHandle.find(handle);
    if (found != updateByHandle.end()) {
      updates[found->second].owners.push_back(
        static_cast<size_t>(commandIndex));
    }
  }

  SoRetainedMutationTransaction transaction(*this);
  const int commandCount = transaction.originalCommandCount();
  const size_t pathCount = transaction.originalPathCount();
  bool replayedOneCommandPerResource = true;
  for (const ResourceUpdate & update : updates) {
    const int before = this->drawlist.getNumCommands();
    this->traverseAdditionalPath(const_cast<SoPath *>(
      this->getCommandPath(static_cast<int>(
        update.representativeCommand))));
    if (this->drawlist.getNumCommands() != before + 1) {
      replayedOneCommandPerResource = false;
      break;
    }
  }
  bool valid = replayedOneCommandPerResource &&
    this->drawlist.getNumCommands() ==
      commandCount + static_cast<int>(updates.size()) &&
    PRIVATE(this)->commandPathRecords.size() == pathCount + updates.size() &&
    !this->unsupportedRendering;
  for (size_t i = 0; valid && i < updates.size(); ++i) {
    const SoRenderCommand & replayed =
      this->drawlist.getCommandSnapshot(commandCount + static_cast<int>(i));
    const SoGeometryResource * replayedResource =
      this->drawlist.getGeometryResource(replayed.geometryHandle);
    valid = replayedResource != nullptr;
    if (valid) updates[i].replacement = *replayedResource;
  }

  const auto sameLayout = [](const SoGeometryDesc & lhs,
                             const SoGeometryDesc & rhs) {
    return lhs.topology == rhs.topology &&
      lhs.vertexCount == rhs.vertexCount &&
      lhs.normalCount == rhs.normalCount &&
      lhs.indexCount == rhs.indexCount &&
      lhs.vertexStride == rhs.vertexStride &&
      lhs.texcoordStride == rhs.texcoordStride &&
      (lhs.positions != nullptr) == (rhs.positions != nullptr) &&
      (lhs.normals != nullptr) == (rhs.normals != nullptr) &&
      (lhs.texcoords != nullptr) == (rhs.texcoords != nullptr) &&
      (lhs.colors != nullptr) == (rhs.colors != nullptr) &&
      (lhs.indices != nullptr) == (rhs.indices != nullptr);
  };
  const auto sameRange = [](const SoRenderElementRange & lhs,
                            const SoRenderElementRange & rhs) {
    return lhs.type == rhs.type && lhs.elementIndex == rhs.elementIndex &&
      lhs.drawStart == rhs.drawStart && lhs.drawCount == rhs.drawCount;
  };
  for (size_t i = 0; valid && i < updates.size(); ++i) {
    const SoGeometryResource & original = updates[i].original;
    const SoGeometryResource & replacement = updates[i].replacement;
    valid = sameLayout(original.geometry, replacement.geometry) &&
      original.elementRanges.size() == replacement.elementRanges.size() &&
      std::equal(original.elementRanges.begin(), original.elementRanges.end(),
                 replacement.elementRanges.begin(), sameRange);
  }

  transaction.finish(valid);
  if (!valid) return 0;

  // Keep successful replay allocations in the frame arena and redirect each
  // stable handle only after the complete transaction has validated.
  int updatedCommands = 0;
  for (ResourceUpdate & update : updates) {
    SoGeometryResource * destination =
      this->drawlist.getGeometryResource(update.handle);
    if (!destination) return 0;
    const uint64_t nextRevision = destination->revision + 1;
    const uint64_t sourceKey = destination->sourceKey;
    update.replacement.sourceKey = sourceKey;
    update.replacement.revision = nextRevision;
    update.replacement.geometry.cacheKey = sourceKey;
    update.replacement.geometry.revision = nextRevision;
    *destination = update.replacement;
    for (size_t commandIndex : update.owners) {
      SoRenderCommand command =
        this->drawlist.getCommandSnapshot(
          static_cast<int>(commandIndex));
      command.geometry.cacheKey = sourceKey;
      command.geometry.revision = nextRevision;
      this->drawlist.setCommandForRetainedUpdate(static_cast<int>(commandIndex), command);
      ++updatedCommands;
    }
  }
  return updatedCommands;
}

void
SoIRRenderAction::beginTraversal(SoNode * node)
{
  SoViewportRegionElement::set(this->state, this->vpRegion);
  SoDevicePixelRatioElement::set(this->state, this->devicePixelRatio);
  this->initializeCameraState(this->cameraPolicy);
  SoRenderMatrixPolicyElement::set(
    this->state, nullptr,
    this->cameraPolicy == CameraPolicy::CAMERA_IN_ROOT
      ? SoRenderMatrixPolicyElement::CAPTURE_CURRENT_MATRICES
      : SoRenderMatrixPolicyElement::INHERIT_CAMERA_MATRICES);
  inherited::beginTraversal(node);
}

void
SoIRRenderAction::pushPrimitiveCollector(PrimitiveCollector * collector)
{
  assert(collector != NULL);
  PRIVATE(this)->collectorStack.append(collector);
}

void
SoIRRenderAction::popPrimitiveCollector(PrimitiveCollector * collector)
{
  const int count = PRIVATE(this)->collectorStack.getLength();
  assert(count > 0);
  assert(PRIVATE(this)->collectorStack[count - 1] == collector);
  PRIVATE(this)->collectorStack.remove(count - 1);
}

SoIRRenderAction::PrimitiveCollector *
SoIRRenderAction::getActivePrimitiveCollector(void) const
{
  const int count = PRIVATE(this)->collectorStack.getLength();
  if (count == 0) return NULL;
  return PRIVATE(this)->collectorStack[count - 1];
}

SoGeometryHandle
SoIRRenderAction::findGeometrySource(const uint64_t sourceKey,
                                     const uint64_t revision) const
{
  const auto candidates = PRIVATE(this)->geometrySources.equal_range(sourceKey);
  for (auto candidate = candidates.first; candidate != candidates.second;
       ++candidate) {
    const SoGeometryResource * resource =
      this->drawlist.getGeometryResource(candidate->second);
    if (resource && resource->revision == revision) return candidate->second;
  }
  return SO_INVALID_GEOMETRY_HANDLE;
}

void
SoIRRenderAction::setConstructionTimingEnabled(const SbBool enabled)
{
  PRIVATE(this)->constructionTimingEnabled = enabled != FALSE;
}

SbBool
SoIRRenderAction::isConstructionTimingEnabled() const
{
  return PRIVATE(this)->constructionTimingEnabled ? TRUE : FALSE;
}

const SoIRRenderAction::ConstructionStatistics &
SoIRRenderAction::getConstructionStatistics() const
{
  return PRIVATE(this)->constructionStatistics;
}

void
SoIRRenderAction::recordPrimitiveGenerationNanoseconds(uint64_t nanoseconds)
{
  PRIVATE(this)->constructionStatistics.primitiveGenerationNanoseconds +=
    nanoseconds;
}

void
SoIRRenderAction::recordGeometryPackingNanoseconds(uint64_t nanoseconds)
{
  PRIVATE(this)->constructionStatistics.geometryPackingNanoseconds +=
    nanoseconds;
}

void
SoIRRenderAction::recordCommandEmissionNanoseconds(uint64_t nanoseconds)
{
  PRIVATE(this)->constructionStatistics.commandEmissionNanoseconds +=
    nanoseconds;
}

void *
SoIRRenderAction::allocateGeometryStorage(size_t bytes, size_t alignment)
{
  return PRIVATE(this)->geometryPool.allocate(bytes, alignment);
}

const unsigned char *
SoIRRenderAction::allocateTextureStorage(const unsigned char * source,
                                         size_t bytes,
                                         int width,
                                         int height,
                                         int numComponents,
                                         bool & hasTransparency)
{
  assert(source != NULL);
  for (std::vector<SoIRRenderActionP::TextureStorage>::const_iterator it =
         PRIVATE(this)->textureStorage.begin();
       it != PRIVATE(this)->textureStorage.end(); ++it) {
    if (it->source == source && it->bytes == bytes &&
        it->width == width && it->height == height &&
        it->numComponents == numComponents) {
      hasTransparency = it->hasTransparency;
      return it->copy;
    }
  }

  const bool carriesAlpha = numComponents == 2 || numComponents == 4;
  bool transparent = false;
  if (carriesAlpha) {
    const size_t pixelCount = bytes / static_cast<size_t>(numComponents);
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
      if (source[pixel * static_cast<size_t>(numComponents) +
                  static_cast<size_t>(numComponents - 1)] != 0xffu) {
        transparent = true;
        break;
      }
    }
  }

  unsigned char * copy = static_cast<unsigned char *>(
    PRIVATE(this)->geometryPool.allocate(bytes, alignof(unsigned char)));
  std::memcpy(copy, source, bytes);
  SoIRRenderActionP::TextureStorage storage;
  storage.source = source;
  storage.bytes = bytes;
  storage.width = width;
  storage.height = height;
  storage.numComponents = numComponents;
  storage.copy = copy;
  storage.hasTransparency = transparent;
  PRIVATE(this)->textureStorage.push_back(storage);
  hasTransparency = transparent;
  return copy;
}

void
SoIRRenderAction::applyRenderStage(SoRenderCommand & command)
{
  if (PRIVATE(this)->renderStage == SoRenderStage::Background) {
    command.stage = SoRenderStage::Background;
  }
  else if (PRIVATE(this)->renderStage == SoRenderStage::AfterMain) {
    command.stage = SoRenderStage::AfterMain;
  }
  else if (PRIVATE(this)->renderStage == SoRenderStage::Foreground ||
           SoRenderPlacementElement::getLayer(this->state) ==
             SoRenderPlacementElement::FOREGROUND) {
    command.stage = SoRenderStage::Foreground;
  }
}

void
SoIRRenderAction::requestDepthClear()
{
  SoDepthClearEvent event;
  event.stage = PRIVATE(this)->renderStage;
  if (SoRenderPlacementElement::getLayer(this->state) ==
      SoRenderPlacementElement::FOREGROUND) {
    event.stage = SoRenderStage::Foreground;
  }
  event.sequence = static_cast<uint32_t>(
    this->drawlist.getNumCommands());
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  if (SoRenderPlacementElement::getViewport(
        this->state, x, y, width, height)) {
    event.viewportOverride = TRUE;
    event.viewportX = x;
    event.viewportY = y;
    event.viewportWidth = width;
    event.viewportHeight = height;
  }
  this->drawlist.addDepthClearEvent(event);
}

SoIRRenderStageScope::SoIRRenderStageScope(SoIRRenderAction & action,
                                           SoRenderStage stage)
  : action(&action), previousStage(action.getRenderStage())
{
  this->action->setRenderStage(stage);
}

SoIRRenderStageScope::~SoIRRenderStageScope()
{
  this->action->setRenderStage(this->previousStage);
}

void
SoIRRenderAction::resetFrameResources()
{
  PRIVATE(this)->geometryPool.clear();
  PRIVATE(this)->textureStorage.clear();
  PRIVATE(this)->textureUploadQualities.clear();
  PRIVATE(this)->collectorStack.truncate(0);
  PRIVATE(this)->geometrySources.clear();
  PRIVATE(this)->constructionStatistics = ConstructionStatistics();
}

void
SoIRRenderAction::clearCommandPaths()
{
  for (SoPath * path : this->commandPaths) {
    if (path && SoDB::isInitialized()) path->unref();
  }
  this->commandPaths.clear();
  for (SoPath * path : PRIVATE(this)->worldStatePaths)
    if (path && SoDB::isInitialized()) path->unref();
  PRIVATE(this)->worldStatePaths.clear();
  PRIVATE(this)->projectionCamera = nullptr;
  PRIVATE(this)->projectionCameraRevision = 0;
  PRIVATE(this)->multipleProjectionCameras = false;
  for (SoNode * node : PRIVATE(this)->nodeIndex) {
    if (node && SoDB::isInitialized()) node->unref();
  }
  PRIVATE(this)->commandPathRecords.clear();
  PRIVATE(this)->pathNodes.clear();
  PRIVATE(this)->pathIndices.clear();
  PRIVATE(this)->ownedPathNodeCount=0;
  PRIVATE(this)->replayOwnedPathNodes.clear();
  PRIVATE(this)->commandAuthoredVisibility.clear();
  std::fill(PRIVATE(this)->nodeIndex.begin(),PRIVATE(this)->nodeIndex.end(),nullptr);
  std::fill(PRIVATE(this)->dependencyHeads.begin(),PRIVATE(this)->dependencyHeads.end(),uint32_t(-1));
  PRIVATE(this)->branchDependencyLinks.clear();
  PRIVATE(this)->recordBranchDependencies = true;
  PRIVATE(this)->renderStage = SoRenderStage::Main;
  PRIVATE(this)->hasRenderContextOverride = false;
  PRIVATE(this)->renderContextOverride = SoIRRenderContext();
}

void SoIRRenderAction::compactRetainedStorage() {
  this->drawlist.compactRetainedStorage();
  auto & storage=*PRIVATE(this);
  if (storage.trimmedGeneration==drawlist.getGeneration()) return;
  storage.commandPathRecords.shrink_to_fit(); storage.pathNodes.shrink_to_fit();
  storage.pathIndices.shrink_to_fit(); storage.replayOwnedPathNodes.shrink_to_fit();
  storage.worldStatePaths.shrink_to_fit();
  storage.branchDependencyLinks.shrink_to_fit(); storage.commandAuthoredVisibility.shrink_to_fit();
  size_t slots=16;
  while (slots<storage.ownedPathNodeCount*2) slots*=2;
  if (slots<storage.nodeIndex.size()) storage.rehashNodeIndex(slots);
  storage.trimmedGeneration=drawlist.getGeneration();
}
uint64_t SoIRRenderAction::getRetainedPathStorageBytes() const {
  const auto & s=*PRIVATE(this);
  return s.commandPathRecords.capacity()*sizeof(SoIRRenderActionP::PathRecord) +
    s.pathNodes.capacity()*sizeof(SoNode *) + s.pathIndices.capacity()*sizeof(int) +
    s.replayOwnedPathNodes.capacity()*sizeof(SoNode *) + s.worldStatePaths.capacity()*sizeof(SoPath *) +
    s.branchDependencyLinks.capacity()*sizeof(SoIRRenderActionP::DependencyLink) +
    (s.commandAuthoredVisibility.capacity()+7)/8 +
    s.nodeIndex.capacity()*sizeof(SoNode *) + s.dependencyHeads.capacity()*sizeof(uint32_t);
}
uint64_t SoIRRenderAction::getRetainedDependencyRangeCount() const {
  return PRIVATE(this)->branchDependencyLinks.size();
}
