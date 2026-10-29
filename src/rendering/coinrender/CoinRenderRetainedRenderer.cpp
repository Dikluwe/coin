#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoLightModelElement.h>
#include "elements/SoPBRMaterialElement.h"
#include <Inventor/elements/SoMaterialBindingElement.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoRotation.h>
#include <Inventor/nodes/SoScale.h>
#include <Inventor/nodes/SoMatrixTransform.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include "rendering/coinrender/CoinRenderRetainedTransport.h"
#include "rendering/coinrender/CoinRenderSceneChanges.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoCylinder.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoTriangleStripSet.h>
#include <Inventor/nodes/SoIndexedTriangleStripSet.h>
#include <Inventor/nodes/SoQuadMesh.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoTransformSeparator.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoUnits.h>
#include <Inventor/nodes/SoResetTransform.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoSpotLight.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoPolygonOffset.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoBumpMapTransform.h>
#include <Inventor/nodes/SoTextureCoordinatePlane.h>
#include <Inventor/nodes/SoTextureCoordinateBinding.h>
#include <Inventor/nodes/SoTexture2Transform.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include "rendering/coinrender/CoinRenderAlphaTestCapture.h"
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoVertexProperty.h>
#include <Inventor/nodes/SoIndexedShape.h>
#include <Inventor/elements/SoCoordinateElement.h>
#include <Inventor/elements/SoMultiTextureEnabledElement.h>
#include <algorithm>
#include <new>
#include <exception>

namespace {
// Reject unsupported node semantics while traversing, before they can be lost
// in the IR. Exact types are deliberate: arbitrary subclasses may override
// callback()/GLRender() without describing their semantics through IRRender().
class CoinRenderSceneCapture : public SoIRRenderAction {
  typedef SoIRRenderAction inherited;
  SO_ACTION_HEADER(CoinRenderSceneCapture);
public:
  static void initClass() {
    SO_ACTION_INIT_CLASS(CoinRenderSceneCapture, SoIRRenderAction);
    SO_ACTION_ADD_METHOD(SoNode, visit);
  }
  explicit CoinRenderSceneCapture(const SbViewportRegion & vp) : inherited(vp) {
    SO_ACTION_CONSTRUCTOR(CoinRenderSceneCapture);
    setCameraPolicy(CameraPolicy::CAMERA_IN_ROOT);
    getMutableDrawList().setCompactCommandStorage(true);
    const SoType accepted[] = {
      SoSeparator::getClassTypeId(), SoGroup::getClassTypeId(),
      SoCube::getClassTypeId(), SoFaceSet::getClassTypeId(), SoAlphaTest::getClassTypeId(), SoClipPlane::getClassTypeId(),
      SoTexture2::getClassTypeId(), SoTextureUnit::getClassTypeId(), SoBumpMap::getClassTypeId(), SoBumpMapTransform::getClassTypeId(), SoTextureCoordinatePlane::getClassTypeId(),
      SoTextureCoordinate2::getClassTypeId(),
      SoTextureCoordinateBinding::getClassTypeId(),
      SoTexture2Transform::getClassTypeId(),
      SoTextureMatrixTransform::getClassTypeId(),

      SoSphere::getClassTypeId(),
      SoCone::getClassTypeId(),
      SoCylinder::getClassTypeId(),
      SoIndexedFaceSet::getClassTypeId(),
      SoTriangleStripSet::getClassTypeId(),
      SoIndexedTriangleStripSet::getClassTypeId(),
      SoQuadMesh::getClassTypeId(),
      SoLineSet::getClassTypeId(),
      SoIndexedLineSet::getClassTypeId(),
      SoPointSet::getClassTypeId(),
      SoNormal::getClassTypeId(),
      SoNormalBinding::getClassTypeId(),
      SoMaterialBinding::getClassTypeId(),
      SoComplexity::getClassTypeId(),
      SoTransformSeparator::getClassTypeId(),
      SoSwitch::getClassTypeId(),
      SoUnits::getClassTypeId(),
      SoResetTransform::getClassTypeId(),
      SoPointLight::getClassTypeId(),
      SoSpotLight::getClassTypeId(),
      SoEnvironment::getClassTypeId(),
      SoPolygonOffset::getClassTypeId(),
      SoDrawStyle::getClassTypeId(),

      SoCoordinate3::getClassTypeId(), SoMaterial::getClassTypeId(), SoPBRMaterial::getClassTypeId(),
      SoLightModel::getClassTypeId(), SoTranslation::getClassTypeId(),
      SoTransform::getClassTypeId(), SoRotation::getClassTypeId(),
      SoScale::getClassTypeId(), SoMatrixTransform::getClassTypeId(),
      SoPerspectiveCamera::getClassTypeId(), SoOrthographicCamera::getClassTypeId(),
      SoDirectionalLight::getClassTypeId(), SoDepthBuffer::getClassTypeId(), SoShapeHints::getClassTypeId()
    };
    for (const auto type : accepted) acceptedNodeTypes.push_back(type.getKey());
    std::sort(acceptedNodeTypes.begin(), acceptedNodeTypes.end());
  }
  static void visit(SoAction * raw, SoNode * node) {
    auto * action = static_cast<CoinRenderSceneCapture *>(raw);
    if (action->hasUnsupportedRendering()) return;
    const bool admitted = std::binary_search(action->acceptedNodeTypes.begin(),
      action->acceptedNodeTypes.end(), node->getTypeId().getKey());
    if (!admitted) {
      action->markUnsupported(node, "Node type is outside the retained capture profile");
      return;
    }
    if (node->isOfType(SoShape::getClassTypeId())) {
      SoState * state = action->getState();
      const auto * physical = SoPBRMaterialElement::get(state);
      if (physical && physical->model == SoRenderMaterialModel::PBR &&
          SoLightModelElement::get(state) != SoLightModelElement::PHONG) {
        action->markUnsupported(node, "Retained PBR profile requires PHONG light-model state");
        return;
      }
      int lastUnit=-1;SoMultiTextureEnabledElement::getEnabledUnits(state,lastUnit);
      if(lastUnit>0 && (!physical || physical->model!=SoRenderMaterialModel::PBR)) {
        action->markUnsupported(node,"Legacy multitexture requires its native semantic descriptors");return;
      }
      auto * property = node->isOfType(SoVertexShape::getClassTypeId())
        ? static_cast<SoVertexShape *>(node)->vertexProperty.getValue() : nullptr;
      if (SoLazyElement::getInstance(state)->isPacked() ||
          (property && (property->getTypeId() != SoVertexProperty::getClassTypeId() ||
            static_cast<SoVertexProperty *>(property)->orderedRGBA.getNum() != 0))) {
        action->markUnsupported(node, "Retained capture does not yet admit packed vertex colors or custom vertex properties");
        return;
      }
    }
    if (node->isOfType(SoIndexedShape::getClassTypeId())) {
      auto * indexed = static_cast<SoIndexedShape *>(node);
      auto * property = static_cast<SoVertexProperty *>(indexed->vertexProperty.getValue());
      const auto * coordinates = SoCoordinateElement::getInstance(action->getState());
      const int coordinateCount = property && property->vertex.getNum() ? property->vertex.getNum() : coordinates->getNum();
      for (int i = 0; i < indexed->coordIndex.getNum(); ++i) {
        const int index = indexed->coordIndex[i];
        if (index < -1 || index >= coordinateCount) {
          action->markUnsupported(node, "Retained coordinate index is outside its source");
          return;
        }
      }
    }
    // These authoring modes need semantic fields absent from the native IR.
    // Reject them during the sole traversal instead of rendering partial state.
    if (node->getTypeId() == SoComplexity::getClassTypeId() &&
        static_cast<SoComplexity *>(node)->type.getValue() == SoComplexity::BOUNDING_BOX) {
      action->markUnsupported(node, "Retained bounding-box complexity needs polygon contours");
      return;
    }
    if (node->getTypeId() == SoAlphaTest::getClassTypeId()) {
      if (!coin_render_capture_alpha_node(*static_cast<SoAlphaTest *>(node), action->getState()))
        action->markUnsupported(node, "Invalid retained alpha-test fields");
      return;
    }
    node->IRRender(action);
  }
private:
  // Prepare type admission once per capture action, not once per scene node.
  std::vector<int16_t> acceptedNodeTypes;
};
SO_ACTION_SOURCE(CoinRenderSceneCapture);

SoRenderOrder renderOrder(CoinRenderAction::TransparencyType type) {
  switch (type) {
  case CoinRenderAction::NONE: case CoinRenderAction::SCREEN_DOOR:
  case CoinRenderAction::BLEND: case CoinRenderAction::ADD: return SoRenderOrder::Captured;
  case CoinRenderAction::DELAYED_BLEND: case CoinRenderAction::DELAYED_ADD:
  case CoinRenderAction::SORTED_LAYERS_BLEND: return SoRenderOrder::OpaqueFirst;
  default: return SoRenderOrder::BackToFront;
  }
}

CoinRenderAction::Status actionStatus(CoinRenderBackendStatus value) {
  switch (value) {
  case CoinRenderBackendStatus::SUCCESS: return CoinRenderAction::SUCCESS;
  case CoinRenderBackendStatus::NOT_READY: return CoinRenderAction::NOT_READY;
  case CoinRenderBackendStatus::UNSUPPORTED: return CoinRenderAction::UNSUPPORTED;
  case CoinRenderBackendStatus::OUT_OF_MEMORY: return CoinRenderAction::OUT_OF_MEMORY;
  case CoinRenderBackendStatus::DEVICE_LOST: return CoinRenderAction::DEVICE_LOST;
  case CoinRenderBackendStatus::SURFACE_LOST: return CoinRenderAction::SURFACE_LOST;
  default: return CoinRenderAction::BACKEND_ERROR;
  }
}
}

struct CoinRenderRetainedRenderer::P {
  explicit P(const SbViewportRegion & viewport) : capture(viewport) {}
  ~P() { sceneChanges.detach(); if (root) root->unref(); }
  CoinRenderSceneCapture capture;
  SoRenderPlan plan;
  SoRetainedUpdater updater;
  CoinRenderSceneChanges sceneChanges;
  SoNode * root = nullptr;
  SbUniqueId rootRevision = 0;
  bool captureValid = false;
  uint64_t planRevision = 0, targetOwner = 0;
  SoRenderOrder plannedOrder = SoRenderOrder::BackToFront;
  uint32_t captureGeneration = 0;
  std::unique_ptr<CoinRenderRetainedTransport> transport;
  CoinRenderTarget * destination = nullptr;
  Statistics statistics;
  SbColor4f clear = SbColor4f(0, 0, 0, 1);
  SbString error;
  uint64_t captureCount = 0, planBuildCount = 0;
  CoinRenderAction::TransparencyType transparencyType = CoinRenderAction::SORTED_OBJECT_BLEND;
  bool rendering = false;
};

CoinRenderRetainedRenderer::CoinRenderRetainedRenderer(const SbViewportRegion & viewport) {
  if (CoinRenderSceneCapture::getClassTypeId() == SoType::badType())
    CoinRenderSceneCapture::initClass();
  p = new P(viewport);
}
CoinRenderRetainedRenderer::~CoinRenderRetainedRenderer() { delete p; }
void CoinRenderRetainedRenderer::setViewportRegion(const SbViewportRegion & viewport) {
  if (p->capture.getViewportRegion() == viewport) return;
  p->capture.setViewportRegion(viewport); p->captureValid = false;
}
void CoinRenderRetainedRenderer::setClearColor(const SbColor4f & color) { p->clear = color; }
void CoinRenderRetainedRenderer::setTransparencyType(CoinRenderAction::TransparencyType type) {
  p->transparencyType = type;
}
const SbString & CoinRenderRetainedRenderer::getLastError() const { return p->error; }
const SoDrawList & CoinRenderRetainedRenderer::getDrawList() const { return p->capture.getDrawList(); }
const SoRenderPlan & CoinRenderRetainedRenderer::getRenderPlan() const { return p->plan; }
uint64_t CoinRenderRetainedRenderer::getCaptureCount() const { return p->captureCount; }
uint64_t CoinRenderRetainedRenderer::getPlanBuildCount() const { return p->planBuildCount; }
void CoinRenderRetainedRenderer::invalidate() { p->captureValid = false; }
CoinRenderRetainedRenderer::Statistics CoinRenderRetainedRenderer::getStatistics() const {
  auto value = p->statistics;
  value.captures = p->captureCount; value.planBuilds = p->planBuildCount;
  if (p->transport) {
    const auto transport = p->transport->getTransportStatistics();
    value.transportBuilds += transport.transportBuilds;
    value.transportReuses += transport.transportReuses;
    value.transportPatchedCommands += transport.transportPatchedCommands;
    value.copiedVertices += transport.copiedVertices;
    value.copiedIndices += transport.copiedIndices;
    value.transportGeometryPatches += transport.transportGeometryPatches;
    value.geometryPatchedVertices += transport.geometryPatchedVertices;
    value.materialSlots = transport.materialSlots;
    value.transportVertices = transport.transportVertices;
    value.transportCameras = transport.transportCameras;
    value.transportScheduleBytes = transport.transportScheduleBytes;
    value.nativeCompactionPasses = p->capture.getDrawList().getCommandCompactionPassCount();
    value.nativePathStorageBytes = p->capture.getRetainedPathStorageBytes();
    value.nativeDependencyRanges = p->capture.getRetainedDependencyRangeCount();
    value.nativeCommandStorageBytes = p->capture.getDrawList().getCommandStorageBytes();
    value.nativeCommands = p->capture.getDrawList().getNumCommands();
    value.nativeGeometryResources = p->capture.getDrawList().getNumGeometryResources();
  }
  return value;
}

CoinRenderAction::Status
CoinRenderRetainedRenderer::render(SoNode * root, CoinRenderTarget & target) {
  if (p->rendering) return CoinRenderAction::NOT_READY;
  switch (p->transparencyType) {
  case CoinRenderAction::NONE: case CoinRenderAction::SCREEN_DOOR:
  case CoinRenderAction::BLEND: case CoinRenderAction::ADD:
  case CoinRenderAction::DELAYED_BLEND: case CoinRenderAction::DELAYED_ADD:
  case CoinRenderAction::SORTED_OBJECT_BLEND: case CoinRenderAction::SORTED_OBJECT_ADD:
  case CoinRenderAction::SORTED_LAYERS_BLEND: break;
  default:
    p->error = "Retained planner does not yet emit sorted-triangle ranges";
    return CoinRenderAction::UNSUPPORTED;
  }
  if (!root) { p->error = "Null retained scene"; return CoinRenderAction::INVALID_SCENE; }
  struct Scope { bool & active; ~Scope() { active = false; } } scope{p->rendering};
  p->rendering = true;
  try {
    if (p->root != root) {
      p->sceneChanges.detach(); p->sceneChanges.reset();
      root->ref();
      if (p->root) p->root->unref();
      p->root = root;
      p->sceneChanges.attach(root);
      p->captureValid = false;
    }
    const bool changed = p->rootRevision != root->getNodeId();
    if (p->captureValid && changed) {
      SoRetainedUpdater::Result update;
      if (!p->sceneChanges.requiresRecapture && !p->sceneChanges.notifications.empty())
        update = p->updater.update(p->capture, p->sceneChanges.notifications);
      if (update.succeeded()) {
        ++p->statistics.incrementalUpdates;
        p->statistics.patchedCommands += update.updatedCommands;
      } else p->captureValid = false;
    }
    p->sceneChanges.reset();
    if (!p->captureValid) {
      ++p->captureCount;
      p->updater.reset();
      p->capture.apply(root);
    } else ++p->statistics.retainedFrames;
    if (p->capture.hasUnsupportedRendering()) {
      p->captureValid = false;
      p->error = p->capture.getUnsupportedReason();
      return CoinRenderAction::UNSUPPORTED;
    }
    const auto & list = p->capture.getDrawList();
    if (!p->captureValid || p->captureGeneration != list.getGeneration() || p->planRevision != list.getRenderPlanRevision() ||
        p->plannedOrder != renderOrder(p->transparencyType)) {
      SoRenderPlanner planner;
      p->plannedOrder = renderOrder(p->transparencyType);
      planner.build(list, p->plan, p->plannedOrder);
      ++p->planBuildCount;
      p->captureGeneration = list.getGeneration();
      p->planRevision = list.getRenderPlanRevision();
    }
    p->rootRevision = root->getNodeId();
    p->captureValid = true;
    const uint64_t owner = target.getPimpl()->resourceOwnerId;
    if (!p->transport || p->destination != &target || p->targetOwner != owner) {
      if (!p->transport) {
        p->transport.reset(new CoinRenderRetainedTransport(target));
        p->transport->initialize({});
      } else p->transport->rebindTarget(target);
      // CPU transport owns no target/device resources. Target and its GPU executor
      // keep their own owner/generation checks; a new GPU target packs on miss.
      p->destination = &target; p->targetOwner = owner;
    }

    SoRenderParams params;
    params.viewport = p->capture.getViewportRegion();
    params.clearColor = p->clear;
    params.flags = SO_PARAM_CLEAR_WINDOW | SO_PARAM_CLEAR_DEPTH;
    p->transport->setTransparencyType(p->transparencyType);
    p->transport->submitOwnedCapture(list, p->plan, params);
    p->error = p->transport->getResult().diagnostic.c_str();
    if (p->transport->getResult().status != CoinRenderBackendStatus::SUCCESS) p->captureValid = false;
    if (p->captureValid) {
      try { p->capture.compactRetainedStorage(); } catch (const std::bad_alloc &) {}
    }
    return actionStatus(p->transport->getResult().status);
  }
  catch (const std::bad_alloc &) {
    p->captureValid = false;
    p->error = "Cannot allocate retained frame"; return CoinRenderAction::OUT_OF_MEMORY;
  }
  catch (const std::exception & exception) {
    p->captureValid = false;
    p->error = exception.what(); return CoinRenderAction::BACKEND_ERROR;
  }
}
