#include <map>
#include "rendering/coinrender/CoinRenderMultipassCore.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif
#include "rendering/coinrender/CoinRenderBoundingBoxCore.h"
#include "rendering/coinrender/CoinRenderBumpMapCore.h"
#include <typeinfo>
#include <cstdio>
#include <limits>
#include <utility>
#include "rendering/coinrender/CoinRenderClipCore.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderDepthCore.h"
#include "rendering/coinrender/CoinRenderDepthPolicyElement.h"
#include "rendering/coinrender/CoinRenderShaderModuleElement.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderFramePlanBuilder.h"
#include "rendering/coinrender/CoinRenderShadowCameraMath.h"
#include "rendering/coinrender/CoinRenderImageCore.h"
#include "rendering/coinrender/CoinRenderIndexedGeometryCore.h"
#include "rendering/coinrender/CoinRenderPhaseTimer.h"
#include "rendering/coinrender/CoinRenderPlanAssemblyCore.h"
#include "rendering/coinrender/CoinRenderPolygonStyleCore.h"
#include "rendering/coinrender/CoinRenderScreenRasterCore.h"
#include "rendering/coinrender/CoinRenderStrokeCore.h"
#include "rendering/coinrender/CoinRenderText2Capture.h"
#include "rendering/coinrender/CoinRenderText2ShadowCapture.h"
#include "rendering/coinrender/CoinRenderTextureAlphaCore.h"
#include "rendering/coinrender/CoinRenderTextureCombineCore.h"
#include "rendering/coinrender/CoinRenderTextureCoordinateCore.h"
#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"
#include "shapenodes/CoinRenderMarkerBridge.h"
#include <Inventor/details/SoCylinderDetail.h>
#include <Inventor/C/tidbits.h>
#include <Inventor/elements/SoClipPlaneElement.h>
#include <Inventor/elements/SoBumpMapCoordinateElement.h>
#include <Inventor/elements/SoBumpMapElement.h>
#include <Inventor/elements/SoBumpMapMatrixElement.h>
#include <Inventor/elements/SoDrawStyleElement.h>
#include <Inventor/elements/SoMaterialBindingElement.h>
#include <Inventor/elements/SoNormalBindingElement.h>
#include <Inventor/elements/SoNormalElement.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoTextureCubeMap.h>
#include <Inventor/elements/SoTextureOverrideElement.h>
#include "rendering/coinrender/CoinRenderCubeTextureCore.h"
#include <Inventor/nodes/SoCylinder.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoVertexShape.h>
#include <Inventor/nodes/SoTextureCoordinateObject.h>
#include <Inventor/nodes/SoTextureCoordinatePlane.h>
#include <Inventor/nodes/SoTextureCoordinateEnvironment.h>
#include <Inventor/nodes/SoTextureCoordinateReflectionMap.h>
#include <Inventor/nodes/SoTextureCoordinateNormalMap.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoIndexedMarkerSet.h>
#include <Inventor/nodes/SoIndexedTriangleStripSet.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoMarkerSet.h>
#include <Inventor/nodes/SoQuadMesh.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/nodes/SoTriangleStripSet.h>
#include <Inventor/nodes/SoVertexProperty.h>
#include <iostream>

#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/bundles/SoTextureCoordinateBundle.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoLight.h>
#ifdef HAVE_VRML97
#include <Inventor/VRMLnodes/SoVRMLLight.h>
#include <Inventor/VRMLnodes/SoVRMLDirectionalLight.h>
#include <Inventor/VRMLnodes/SoVRMLPointLight.h>
#include <Inventor/VRMLnodes/SoVRMLSpotLight.h>
#endif
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoSpotLight.h>
#include <Inventor/elements/SoLightElement.h>
#include <Inventor/elements/SoViewVolumeElement.h>
#include <Inventor/annex/FXViz/elements/SoShadowStyleElement.h>
#include <Inventor/elements/SoEnvironmentElement.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbColor.h>
#include <Inventor/misc/SoState.h>
#include <Inventor/elements/SoShapeHintsElement.h>
#include <Inventor/elements/SoDepthBufferElement.h>
#include <Inventor/elements/SoPolygonOffsetElement.h>
#include <Inventor/elements/SoLinePatternElement.h>
#include <Inventor/elements/SoCreaseAngleElement.h>
#include <Inventor/elements/SoLightModelElement.h>
#include <Inventor/elements/SoShapeStyleElement.h>
#include "rendering/coinrender/CoinRenderAlphaTestCapture.h"
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoMultiTextureImageElement.h>
#include <Inventor/elements/SoMultiTextureEnabledElement.h>
#include <Inventor/elements/SoCoordinateElement.h>
#include <Inventor/details/SoPointDetail.h>
#include <Inventor/details/SoFaceDetail.h>
#include <Inventor/details/SoLineDetail.h>
#include <Inventor/elements/SoMultiTextureMatrixElement.h>
#include <Inventor/elements/SoTextureQualityElement.h>
#include "elements/SoTextureScalePolicyElement.h"
#include "elements/SoTextureScaleQualityElement.h"
#include "glue/simage_wrapper.h"
#include "glue/GLUWrapper.h"
#include <Inventor/elements/SoTextureUnitElement.h>
#include <Inventor/elements/SoMultiTextureCoordinateElement.h>
#include <Inventor/elements/SoTextureCoordinateBindingElement.h>

#include <cassert>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cfloat> // _logb on builds that select CoinGL's legacy weight path
#include <cstdlib>
#include <cstring>
#include <new>
#include <limits>


CoinRenderCameraSnapshot
CoinRenderFramePlanBuilder::captureCamera(SoCallbackAction * action)
{
  CoinRenderCameraSnapshot camSnap;
  camSnap.viewMatrix = action->getViewingMatrix();
  camSnap.projectionMatrixCoin = action->getProjectionMatrix();
  const SbViewVolume & vv = action->getViewVolume();
  camSnap.isPerspective = (vv.getProjectionType() == SbViewVolume::PERSPECTIVE);
  camSnap.nearDistance = vv.getNearDist();
  camSnap.farDistance = vv.getNearDist() + vv.getDepth();
  CoinRenderPlanAssemblyCore::normalizeCamera(camSnap);
  camSnap.focalDistance = action->getFocalDistance();
  const SbViewportRegion & vp = action->getViewportRegion();
  camSnap.aspectRatio = vp.getViewportAspectRatio();
  return camSnap;
}

void
CoinRenderFramePlanBuilder::registerShadowMapCamera(const SoCamera *camera)
{
  if (camera) this->shadowMapCameras[camera->getNodeId()] = camera;
}

void
CoinRenderFramePlanBuilder::registerShadowMapCameraPose(const SoCamera *camera,
                                                         const SbMatrix &model,
                                                         const SbMatrix &view)
{
  if (!camera) return;
  auto &poses = this->shadowMapCameraPoses[camera->getNodeId()];
  const auto existing = std::find_if(poses.begin(), poses.end(),
    [&](const ShadowMapCameraPose &pose) { return pose.model == model && pose.view == view; });
  if (existing == poses.end()) poses.push_back({model, view});
}

CoinRenderFramePlanBuilder::CoinRenderFramePlanBuilder()
  : currentDrawIndex(0),
    nodeCounter(0),
    inFrame(false),
    hasActiveDraw(false),
    hasError(false),
    isUnsupported(false),
    annotationDepth(0),
    currentAnnotationLayer(0),
    nextAnnotationLayer(1),
    annotationDepthClearPending(false)
{
  this->rasterQPlanes.fill(SbVec4f(0,0,0,0));
}

CoinRenderFramePlanBuilder::~CoinRenderFramePlanBuilder()
{
  for (auto * light : this->vrmlLightProxies) light->unref();
}

void
CoinRenderFramePlanBuilder::beginFrame(const SbColor4f & clearColor, const SbViewportRegion & viewport)
{
  this->beginFrame(clearColor, viewport, 1);
}

void
CoinRenderFramePlanBuilder::beginFrame(const SbColor4f & clearColor, const SbViewportRegion & /*viewport*/, int numPasses)
{
  this->reset();
  this->colorWriteEnabled = true;
  this->linePatternOverride = false;
  this->currentPlan.clearColor = clearColor;
  this->currentPlan.numPasses = numPasses;
  this->currentPlan.delayedObjDepthWrite = this->delayedObjDepthWrite;
  this->currentPlan.smoothing = this->smoothing;
  this->occurrenceSerial = 0;
  this->currentMultipassMask = 0;
  this->currentMultipassRasterFixed = false;
  this->inFrame = true;
}

void
CoinRenderFramePlanBuilder::reserveCaptureStorage(size_t estimate)
{
  this->captureReserveEstimate = estimate;
  const char * disabled = std::getenv("COIN_RENDER_DISABLE_CAPTURE_RESERVE");
  if (!this->inFrame || estimate < 256 ||
      (disabled && disabled[0] == '1' && disabled[1] == '\0')) return;
  // A shallow structural hint may overestimate shapes. Bound its allocation
  // and never use it to skip traversal or retain effective Coin state.
  estimate = std::min(estimate, size_t(65536));
  this->currentPlan.renderStates.reserve(estimate);
  this->currentPlan.draws.reserve(estimate);
  this->renderStatesByModel.reserve(estimate);
  this->nodeOccurrenceCount.reserve(estimate);
}

void
CoinRenderFramePlanBuilder::reset()
{
  for (auto * light : this->vrmlLightProxies) light->unref();
  this->vrmlLightProxies.clear();
  this->vrmlLightSources.clear();
  this->endShape();
  this->rasterGenerators.clear();
  this->rasterNormalNeedsOrder = false;
  this->rasterNormalEffects.clear();
  this->rasterNormalConsumers.clear();
  this->frameInitialNormal = this->rasterNormal;
  this->frameInitialNormalKnown = this->rasterNormalKnown;
  const char * disabledCubeTemplates = std::getenv("COIN_RENDER_DISABLE_CUBE_TEMPLATE_CACHE");
  this->cubeGeometryCore.reset(!(disabledCubeTemplates && std::strcmp(disabledCubeTemplates, "1") == 0));
  this->cubeReplayHits = 0;
  this->polygonNode = nullptr;
  this->polygonVertices.clear();
  this->polygonTriangleVertices.clear();
  this->polygonPositions.clear();
  this->polygonCaptured.clear();
  this->polygonTriangles = 0;
  this->shadowMapCameras.clear();
  this->shadowMapCameraPoses.clear();
  this->currentPlan.vertices.clear();
  this->currentPlan.bumpCoords.clear();
  this->currentPlan.bumpTangents.clear();
  this->currentPlan.bumpImages.clear();
  this->currentPlan.portableShaders.clear();
  this->portableShaderSlots.clear();
  this->currentPlan.bumpTransforms.clear();
  this->lastBumpSource = nullptr;
  this->lastBumpRevision = 0;
  this->lastBumpSlot = 0;
  this->bumpImageSlots.clear();
  this->currentPlan.indices.clear();
  this->currentPlan.materials.clear();
  this->materialHeads.clear();
  this->materialNext.clear();
  const char * disabledMaterialIndex = std::getenv("COIN_RENDER_DISABLE_MATERIAL_INTERNING");
  this->materialIndexDisabled = disabledMaterialIndex && std::strcmp(disabledMaterialIndex, "1") == 0;
  this->currentPlan.lightingStates.clear();
  this->currentPlan.shadowGroups.clear();
  this->currentPlan.shadowLights.clear();
  this->shadowGroupStack.clear();
  this->currentPlan.cameras.clear();
  this->currentPlan.viewports.clear();
  this->currentPlan.renderStates.clear();
  this->renderStatesByModel.clear();
  this->currentPlan.textures.clear();
  this->currentPlan.samplers.clear();
  this->currentPlan.draws.clear();
  this->currentPlan.rasterOccurrences.clear();
  this->currentDrawIndex = 0;
  this->lineNode = nullptr;
  this->lineIndex = -1;
  this->lineStripId = this->nextLineStripId = 0;
  this->stripMaterialNode = nullptr;
  this->stripMaterialPart = -1;
  this->quadShadowNode = nullptr;
  this->quadShadowPart = -1;
  this->quadShadowFace = -1;
  this->quadShadowTriangle = 0;
  this->stripMaterialNextFace = 0;
  this->nodeCounter = 0;
  this->captureReserveEstimate = 0;
  this->inFrame = false;
  this->hasActiveDraw = false;
  this->hasError = false;
  this->isUnsupported = false;
  this->screenContentCaptured = false;
  this->coordinateFunctionsCaptured = false;
  this->savedAnnotationLayer = 0;
  this->savedAnnotationClear = false;
  this->foregroundLayer = 0;
  this->annotationDepth = 0;
  this->currentAnnotationLayer = 0;
  this->nextAnnotationLayer = 1;
  this->authoredSceneLayer = UINT32_MAX;
  this->authoredSceneParentLayer = 0;
  this->authoredSceneParentClear = false;
  this->annotationDepthClearPending = false;
  this->deferredDepthClear = false;
  this->builderError.clear();
  this->sceneTextures.clear();
  this->authoredTextureImages.clear(); this->compressedTextureImages.clear();
  this->authoredVolumeImages.clear();
  this->compressedVolumeImages.clear();
  this->authoredTextureSources.clear();
  this->authoredVolumeSources.clear();
  this->capturedVolumeImages.clear();
  this->cubeSources.clear();
  this->capturedCubeImages.clear();
  this->nodeOccurrenceCount.clear();
  this->lightAttenuationByIndex.clear();
  this->lightModelByIndex.clear();
  this->lightCaptureScratch.clear();
}

CoinRenderFramePlanBuilder::RasterHistory
CoinRenderFramePlanBuilder::rasterHistory() const
{
  RasterHistory result;
  for (size_t i = 0; i < COIN_RENDER_MAX_TEXTURE_UNITS; ++i)
    result.coordinates[i] = uint8_t(this->rasterCoordinateHistory[i]);
  result.normal = this->rasterNormal;
  result.normalKnown = this->rasterNormalKnown;
  result.qPlanes = this->rasterQPlanes;
  return result;
}

void
CoinRenderFramePlanBuilder::restoreRasterHistory(const RasterHistory & saved)
{
  for (size_t i = 0; i < COIN_RENDER_MAX_TEXTURE_UNITS; ++i)
    this->rasterCoordinateHistory[i] = RasterCoordinateHistory(saved.coordinates[i]);
  this->rasterNormal = saved.normal;
  this->rasterNormalKnown = saved.normalKnown;
  this->rasterQPlanes = saved.qPlanes;
}

void
CoinRenderFramePlanBuilder::recordRasterGenerator(SoCallbackAction * action, const SoNode * node)
{
  if (!action || !node || !action->getState()) return;
  const int unit = SoTextureUnitElement::get(action->getState());
  if (unit < 0 || unit >= int(COIN_RENDER_MAX_TEXTURE_UNITS)) return;
  const auto * element = SoMultiTextureCoordinateElement::getInstance(action->getState());
  const auto & data = element->getUnitData(unit);
  if (data.nodeid != node->getNodeId() || data.whatKind != SoMultiTextureCoordinateElement::FUNCTION) return;
  const auto type = node->getTypeId();
  if (this->rasterGenerators.size() >= 65536 && !this->rasterGenerators.count(data.nodeid)) {
    this->isUnsupported = true;
    this->builderError = "UNSUPPORTED: raster generator registry exceeds 65536 sources";
    return;
  }
  auto & recorded = this->rasterGenerators[data.nodeid];
  recorded.callback = reinterpret_cast<const void *>(data.funcCB);
  recorded.setsQ = false;
  if (type == SoTextureCoordinateObject::getClassTypeId()) {
    recorded.kind = RasterGeneratorKind::OBJECT;
    recorded.qPlane = static_cast<const SoTextureCoordinateObject *>(node)->factorQ.getValue();
    recorded.setsQ = true;
  } else if (type == SoTextureCoordinatePlane::getClassTypeId()) {
    recorded.kind = RasterGeneratorKind::OBJECT;
    recorded.qPlane = SbVec4f(0,0,0,1);
    recorded.setsQ = true;
  } else if (type == SoTextureCoordinateEnvironment::getClassTypeId()) {
    recorded.kind = RasterGeneratorKind::SPHERE;
    recorded.qPlane = SbVec4f(0,0,0,1);
    recorded.setsQ = true;
  } else if (type == SoTextureCoordinateReflectionMap::getClassTypeId()) {
    recorded.kind = RasterGeneratorKind::REFLECTION;
  } else if (type == SoTextureCoordinateNormalMap::getClassTypeId()) {
    recorded.kind = RasterGeneratorKind::NORMAL;
  } else {
    // Base registrations also see subclasses, whose GLRender may install an
    // unrelated texgen callback. Only the audited native exact types qualify.
    recorded.kind = RasterGeneratorKind::UNSUPPORTED;
  }
  this->synchronizeRasterGenerators(action);
}

void
CoinRenderFramePlanBuilder::synchronizeRasterGenerators(SoCallbackAction * action)
{
  if (!action || !action->getState() || this->rasterGenerators.empty()) return;
  const auto * coordinates = SoMultiTextureCoordinateElement::getInstance(action->getState());
  const size_t count = std::min(size_t(coordinates->getMaxUnits()), size_t(COIN_RENDER_MAX_TEXTURE_UNITS));
  for (size_t unit = 0; unit < count; ++unit) {
    const auto & data = coordinates->getUnitData(int(unit));
    const auto found = this->rasterGenerators.find(data.nodeid);
    if (data.whatKind == SoMultiTextureCoordinateElement::FUNCTION &&
        found != this->rasterGenerators.end() &&
        found->second.callback == reinterpret_cast<const void *>(data.funcCB) && found->second.setsQ)
      this->rasterQPlanes[unit] = found->second.qPlane;
  }
}

void
CoinRenderFramePlanBuilder::recordRasterNormal(SoCallbackAction * action, const SoNode * node)
{
  if (!action || !node || !action->getState() || this->currentPlan.draws.empty() ||
      this->currentPlan.draws.back().sourceNodeId != node->getNodeId()) return;
  SoState * state = action->getState();
  if (SoDrawStyleElement::get(state) == SoDrawStyleElement::INVISIBLE ||
      node->getTypeId() == SoText2::getClassTypeId() ||
      node->isOfType(SoMarkerSet::getClassTypeId()) ||
      node->isOfType(SoIndexedMarkerSet::getClassTypeId())) return;
  const auto & draw = this->currentPlan.draws.back();
  const auto & drawState = this->currentPlan.renderStates[draw.renderStateSlot];
  const int transparency = SoShapeStyleElement::getTransparencyType(state);
  if (draw.renderLayer ||
      ((drawState.transparentMaterial || drawState.transparentTexture) &&
       transparency != SoGLRenderAction::NONE && transparency != SoGLRenderAction::SCREEN_DOOR &&
       transparency != SoGLRenderAction::BLEND && transparency != SoGLRenderAction::ADD))
    this->rasterNormalNeedsOrder = true;
  struct StateScope {
    SoState * state;
    bool pushed;
    explicit StateScope(SoState * owner) : state(owner), pushed(false) {}
    ~StateScope() { if (pushed) state->pop(); }
  } scope(state);
  const auto * shape = dynamic_cast<const SoVertexShape *>(node);
  if (shape && shape->vertexProperty.getValue()) {
    SoNode * property = shape->vertexProperty.getValue();
    if (property->getTypeId() != SoVertexProperty::getClassTypeId() ||
        typeid(*property) != typeid(SoVertexProperty)) {
      const auto ordinal = this->currentPlan.draws.back().frameNodeOrdinal;
      if (this->rasterNormalEffects.size() <= ordinal) this->rasterNormalEffects.resize(ordinal + 1);
      this->rasterNormalEffects[ordinal].kind = RasterNormalEffect::UNKNOWN;
      return;
    }
    // Native GLRender applies VertexProperty inside a pushed state. Reproduce
    // only its ordinary element updates; no GL call or callback traversal.
    state->push(); scope.pushed = true;
    static_cast<SoVertexProperty *>(property)->SoVertexProperty::doAction(action);
  }
  auto effect = this->classifyRasterNormal(action, node);
  if (action->getComplexityType() != SoComplexity::BOUNDING_BOX &&
      (drawState.transparentMaterial || drawState.transparentTexture) &&
      (transparency == SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD ||
       transparency == SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND)) {
    // shouldGLRender substitutes its primitive cache and enables normal arrays,
    // including immediate annotation draws during the delayed-path traversal.
    if (!node->isOfType(SoImage::getClassTypeId()) &&
        !node->isOfType(SoText2::getClassTypeId()) &&
        !node->isOfType(SoMarkerSet::getClassTypeId()) &&
        !node->isOfType(SoIndexedMarkerSet::getClassTypeId()))
      effect.kind = RasterNormalEffect::UNKNOWN;
  }
  if (effect.kind == RasterNormalEffect::PRESERVE) return;
  // Native shouldGLRender may cull a valid bounding-box cache before any
  // normal emission. If a shape is wholly outside the frustum, cache presence
  // determines whether it emits; do not claim either result from callbacks.
  SbBox3f box; SbVec3f center;
  const_cast<SoShape *>(static_cast<const SoShape *>(node))->computeBBox(action, box, center);
  if (!box.isEmpty()) {
    SbPlane planes[6]; action->getViewVolume().getViewVolumePlanes(planes);
    for (const auto & plane : planes) {
      bool anyInside = false;
      for (int corner = 0; corner < 8; ++corner) {
        SbVec3f point((corner & 1) ? box.getMax()[0] : box.getMin()[0],
                      (corner & 2) ? box.getMax()[1] : box.getMin()[1],
                      (corner & 4) ? box.getMax()[2] : box.getMin()[2]);
        action->getModelMatrix().multVecMatrix(point, point);
        anyInside |= plane.isInHalfSpace(point);
      }
      if (!anyInside) { effect.kind = RasterNormalEffect::UNKNOWN; break; }
    }
  }
  if (this->rasterNormalEffects.size() <= draw.frameNodeOrdinal)
    this->rasterNormalEffects.resize(draw.frameNodeOrdinal + 1);
  this->rasterNormalEffects[draw.frameNodeOrdinal] = effect;
}

CoinRenderFramePlanBuilder::RasterNormalEffect
CoinRenderFramePlanBuilder::classifyRasterNormal(SoCallbackAction * action, const SoNode * node)
{
  RasterNormalEffect effect;
  if (!action || !node || !action->getState()) return effect;
  const auto type = node->getTypeId();
  if (type == SoText2::getClassTypeId() ||
      node->isOfType(SoMarkerSet::getClassTypeId()) || node->isOfType(SoIndexedMarkerSet::getClassTypeId())) return effect;
  // An invisible/empty shape never reaches native geometry submission.
  if (SoDrawStyleElement::get(action->getState()) == SoDrawStyleElement::INVISIBLE ||
      this->currentPlan.draws.empty() || this->currentPlan.draws.back().sourceNodeId != node->getNodeId()) return effect;
  // shouldGLRender can replace a triangle shape (including Image's native
  // primitive quad) with the bump cache before its ordinary normal emissions.
  // The specular pass may enable a normal array. Diffuse-only rendering
  // preserves current normal instead of emitting the shape's usual normal.
  bool activeBump = (SoShapeStyleElement::get(action->getState())->getFlags() & SoShapeStyleElement::BUMPMAP) != 0;
  const SoNode * bumpSource = SoBumpMapElement::getSource(action->getState());
  if (bumpSource && bumpSource->isOfType(SoBumpMap::getClassTypeId())) {
    SbVec2s size; int components = 0;
    const auto * pixels = static_cast<const SoBumpMap *>(bumpSource)->image.getValue(size, components);
    activeBump = pixels && size[0] > 0 && size[1] > 0;
  }
  // Native validatePVCache calls generatePrimitives, retaining triangle
  // indices even when our draw-style expansion emits lines or points.
  if (activeBump && SoLightElement::getLights(action->getState()).getLength() &&
      action->getComplexityType() != SoComplexity::BOUNDING_BOX &&
      type != SoPointSet::getClassTypeId()) {
    const SbColor & specular = SoLazyElement::getSpecular(action->getState());
    const bool nativeBumpPath = type == SoImage::getClassTypeId() || type == SoFaceSet::getClassTypeId() ||
      type == SoCube::getClassTypeId() || type == SoCone::getClassTypeId() ||
      type == SoCylinder::getClassTypeId() || type == SoSphere::getClassTypeId() ||
      type == SoIndexedFaceSet::getClassTypeId() || type == SoIndexedTriangleStripSet::getClassTypeId();
    // Exact black skips renderBumpSpecular. The forced CPU diffuse path and
    // renderNormal submit no normal array or glNormal, preserving even an
    // unknown incoming value. Unclassified custom shapes remain unknown.
    if (!nativeBumpPath || specular[0] != 0.0f || specular[1] != 0.0f || specular[2] != 0.0f)
      effect.kind = RasterNormalEffect::UNKNOWN;
    return effect;
  }
  if (type == SoImage::getClassTypeId()) return effect;
  if (type == SoFaceSet::getClassTypeId() &&
      SoLazyElement::getLightModel(action->getState()) == SoLazyElement::BASE_COLOR &&
      SoDrawStyleElement::get(action->getState()) == SoDrawStyleElement::FILLED &&
      action->getComplexityType() != SoComplexity::BOUNDING_BOX) {
    int lastUnit = -1;
    const SbBool * enabled = SoMultiTextureEnabledElement::getEnabledUnits(action->getState(), lastUnit);
    bool textured = false;
    for (int unit = 0; unit <= lastUnit; ++unit) textured |= enabled[unit];
    // Native untextured BASE_COLOR FaceSet sets nbind=OVERALL and
    // needNormals=false: its immediate template skips glNormal, the array
    // path disables the normal array, and the convex path uses binding NONE
    // without its guarded overall glNormal. Culling also emits no normal.
    // Preserve an unknown history too: this shape cannot establish a value.
    if (!textured) return effect;
  }
  const bool audited = type == SoCube::getClassTypeId() || type == SoCone::getClassTypeId() ||
    type == SoCylinder::getClassTypeId() || type == SoSphere::getClassTypeId() ||
    type == SoPointSet::getClassTypeId() || type == SoIndexedFaceSet::getClassTypeId() || type == SoIndexedTriangleStripSet::getClassTypeId() ||
    action->getComplexityType() == SoComplexity::BOUNDING_BOX;
  if (!audited) { effect.kind = RasterNormalEffect::UNKNOWN; return effect; }
  const bool sendsNormals = SoLazyElement::getLightModel(action->getState()) != SoLazyElement::BASE_COLOR ||
    SoMultiTextureCoordinateElement::getType(action->getState(), 0) == SoMultiTextureCoordinateElement::FUNCTION;
  bool bundleSendsNormals = SoLazyElement::getLightModel(action->getState()) != SoLazyElement::BASE_COLOR;
  // Native SoTextureCoordinateBundle::isFunction covers every enabled
  // unit, including its generated default coordinates. Quality zero
  // disables these effective GL units, but does not disable PHONG normals.
  if ((type == SoPointSet::getClassTypeId() || type == SoIndexedFaceSet::getClassTypeId()) &&
      !bundleSendsNormals && SoTextureQualityElement::get(action->getState()) > 0.0f) {
    int lastUnit = -1;
    const SbBool * enabled = SoMultiTextureEnabledElement::getEnabledUnits(action->getState(), lastUnit);
    const auto * coordinates = SoMultiTextureCoordinateElement::getInstance(action->getState());
    for (int unit = 0; unit <= lastUnit; ++unit) {
      if (!enabled[unit]) continue;
      const auto kind = coordinates->getType(unit);
      if (kind != SoMultiTextureCoordinateElement::EXPLICIT || coordinates->getNum(unit) == 0)
        bundleSendsNormals = true;
    }
  }
  auto setNormal = [&effect](const SbVec3f & value) { effect.normal = value; effect.kind = RasterNormalEffect::DEFINED; };
  // These helpers use immediate glNormal, independently of array capabilities.
  if (action->getComplexityType() == SoComplexity::BOUNDING_BOX) {
    // SoShape::GLRenderBoundingBox always requests immediate cube normals.
    setNormal(SbVec3f(0,-1,0));
  } else if (type == SoCube::getClassTypeId()) {
    if (sendsNormals) setNormal(SbVec3f(0,-1,0));
  } else if (type == SoCone::getClassTypeId()) {
    const auto * cone = static_cast<const SoCone *>(node);
    const int parts = cone->parts.getValue();
    if (parts & SoCone::BOTTOM) setNormal(SbVec3f(0,-1,0));
    else if ((parts & SoCone::SIDES) && sendsNormals) {
      const double angle = atan(cone->height.getValue() / cone->bottomRadius.getValue());
      setNormal(SbVec3f(0, float(cos(angle)), -float(sin(angle))));
    }
  } else if (type == SoCylinder::getClassTypeId()) {
    const int parts = static_cast<const SoCylinder *>(node)->parts.getValue();
    if (parts & SoCylinder::BOTTOM) setNormal(SbVec3f(0,-1,0));
    else if (parts & SoCylinder::TOP) setNormal(SbVec3f(0,1,0));
    else if ((parts & SoCylinder::SIDES) && sendsNormals) setNormal(SbVec3f(0,0,-1));
  } else if (type == SoSphere::getClassTypeId()) {
    float complexity = action->getComplexity();
    if (action->getComplexityType() == SoComplexity::SCREEN_SPACE) {
      SbBox3f box; SbVec3f center;
      const_cast<SoShape *>(static_cast<const SoShape *>(node))->computeBBox(action, box, center);
      SbVec2s size;
      SoShape::getScreenSize(action->getState(), box, size);
      complexity *= float(sqrt(float(std::max(size[0], size[1])))) * 0.4f;
    }
    complexity = std::max(0.0f, std::min(1.0f, complexity));
    const int stacks = std::max(3, int(30.0f * complexity));
    const int slices = std::min(128, std::max(4, int(30.0f * complexity)));
    const float drho = float(M_PI) / float(stacks-1);
    const float dtheta = 2.0f * float(M_PI) / float(slices);
    // Reproduce the last explicit immediate-mode glNormal in
    // sogl_render_sphere, including its repeated float angle additions.
    float rho = drho, theta = 0.0f;
    for (int j = 1; j <= slices; ++j) theta += dtheta;
    float tc = float(cos(rho)), ts = -float(sin(rho));
    rho += drho;
    for (int i = 2; i < stacks-1; ++i) {
      tc = float(cos(rho)); ts = -float(sin(rho));
      theta = 0.0f;
      for (int j = 0; j < slices; ++j) theta += dtheta;
      rho += drho;
    }
    setNormal(SbVec3f(float(sin(theta))*ts, tc, float(cos(theta))*ts));
  } else if (type == SoPointSet::getClassTypeId()) {
    const auto * normals = SoNormalElement::getInstance(action->getState());
    // Unlike IndexedShape, PointSet does not generate missing normals;
    // it turns normal submission off and preserves GL_CURRENT_NORMAL.
    if (bundleSendsNormals && normals->getNum()) {
      if (SoNormalBindingElement::get(action->getState()) == SoNormalBindingElement::OVERALL)
        setNormal(normals->get(0)); // Explicit glNormal, normal array disabled.
      else effect.kind = RasterNormalEffect::UNKNOWN; // Per-point arrays may be enabled.
    }
  } else if (type == SoIndexedFaceSet::getClassTypeId() ||
             type == SoIndexedTriangleStripSet::getClassTypeId()) {
    const bool emit = type == SoIndexedTriangleStripSet::getClassTypeId()
      ? SoLazyElement::getLightModel(action->getState()) != SoLazyElement::BASE_COLOR : bundleSendsNormals;
    if (emit) {
      if (SoNormalBindingElement::get(action->getState()) == SoNormalBindingElement::OVERALL) {
        const auto * normals = SoNormalElement::getInstance(action->getState());
        if (normals->getNum()) setNormal(normals->get(0));
        else effect.kind = RasterNormalEffect::UNKNOWN; // Native may generate a normal cache.
      } else effect.kind = RasterNormalEffect::UNKNOWN;
    }
  } else {
    // No inference from generated callback corners: native enabled normal
    // arrays leave the current normal indeterminate after DrawArrays/Elements.
    effect.kind = RasterNormalEffect::UNKNOWN;
  }
  return effect;
}

bool
CoinRenderFramePlanBuilder::resolveRasterNormals(
  const std::vector<CoinRenderCompositionItem> & order, std::string * outError, bool captureOrder)
{
  SbVec3f currentNormal = this->frameInitialNormal;
  bool currentKnown = this->frameInitialNormalKnown;
  auto fail = [&](const char * message, bool unsupported) {
    this->isUnsupported = unsupported;
    this->hasError = !unsupported;
    this->builderError = message;
    if (outError) *outError = message;
    return false;
  };
  auto applyEffect = [&](const RasterNormalEffect & effect) {
    if (effect.kind == RasterNormalEffect::UNKNOWN) currentKnown = false;
    else if (effect.kind == RasterNormalEffect::DEFINED) {
      currentNormal = effect.normal;
      currentKnown = true;
    }
  };
  if (captureOrder) {
    for (const auto & effect : this->rasterNormalEffects) applyEffect(effect);
  }
  for (const auto & item : order) {
    const auto & draw = this->currentPlan.draws[item.drawIndex];
    const auto consumer = this->rasterNormalConsumers.find(draw.frameNodeOrdinal);
    if (consumer != this->rasterNormalConsumers.end()) {
      if (!currentKnown)
        return fail("UNSUPPORTED: raster texgen current normal is not determined for the native geometry history", true);
      auto & coordinates = this->currentPlan.renderStates[draw.renderStateSlot].rasterTextureCoordinates;
      const auto & request = consumer->second;
      const auto & rasterPosition = request.position;
      for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
        const auto kind = request.generators[unit];
        if (kind == RasterGeneratorKind::OBJECT) continue;
        // CoinGL enables NORMALIZE. SoImage's MODELVIEW is identity, hence
        // normalize the persistent raw normal, never the prior model transform.
        SbVec3f normal = currentNormal;
        for (int axis = 0; axis < 3; ++axis)
          if (!std::isfinite(normal[axis]) || !std::isfinite(rasterPosition[axis]))
            return fail("Non-finite raster normal or eye position", false);
        if (!(normal.normalize() > 0.0f))
          return fail("UNSUPPORTED: raster texgen current normal has zero length", true);
        SbVec3f generated = normal;
        if (kind != RasterGeneratorKind::NORMAL) {
          SbVec3f eye = rasterPosition;
          if (!(eye.normalize() > 0.0f))
            return fail("UNSUPPORTED: raster reflection eye position has zero length", true);
          generated = eye - normal * (2.0f * normal.dot(eye));
        }
        const auto & plane = request.qPlanes[unit];
        const float q = plane[0]*rasterPosition[0] + plane[1]*rasterPosition[1] +
                        plane[2]*rasterPosition[2] + plane[3];
        if (kind == RasterGeneratorKind::SPHERE) {
          const float m = 2.0f * sqrtf(generated[0]*generated[0] + generated[1]*generated[1] +
                                      (generated[2]+1.0f)*(generated[2]+1.0f));
          if (!(m > 0.0f)) return fail("UNSUPPORTED: raster sphere-map divisor is zero", true);
          coordinates[unit] = SbVec4f(generated[0]/m+0.5f, generated[1]/m+0.5f, 1, 1);
        } else coordinates[unit] = SbVec4f(generated[0], generated[1], generated[2], q);
      }
    }
    if (draw.frameNodeOrdinal < this->rasterNormalEffects.size())
      applyEffect(this->rasterNormalEffects[draw.frameNodeOrdinal]);
  }
  this->rasterNormal = currentNormal;
  this->rasterNormalKnown = currentKnown;
  return true;
}

void
CoinRenderFramePlanBuilder::invalidateRasterCoordinates(SoCallbackAction * action)
{
  if (!action || !action->getState() ||
      !(SoTextureQualityElement::get(action->getState()) > 0.0f)) return;
  for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit)
    if (SoMultiTextureEnabledElement::get(action->getState(), int(unit)))
      this->rasterCoordinateHistory[unit] = RasterCoordinateHistory::INDETERMINATE;
}

void
CoinRenderFramePlanBuilder::beginShape(SoCallbackAction * action, const SoNode * node)
{
  this->invalidateRasterCoordinates(action);
  this->endShape();
  this->stripMaterialNode = nullptr;
  this->stripMaterialPart = -1;
  this->quadShadowNode = nullptr;
  this->quadShadowPart = -1;
  this->quadShadowFace = -1;
  this->quadShadowTriangle = 0;
  this->stripMaterialNextFace = 0;
  if (!action || !node || polygonDrawStyle(action) != SoDrawStyleElement::FILLED) return;
  // These exact built-in generators do not modify render state between
  // triangles. Subclasses can override generation and must keep full capture.
  const SoType type = node->getTypeId();
  if (type != SoCube::getClassTypeId() && type != SoCone::getClassTypeId() &&
      type != SoCylinder::getClassTypeId() && type != SoSphere::getClassTypeId()) return;
  SoState * state = action->getState();
  // Native template replay has no per-corner bump coordinate payload.
  if (SoBumpMapCoordinateElement::getInstance(state)->getNum() > 0) return;
  const SoNode * bumpSource = SoBumpMapElement::getSource(state);
  if (bumpSource && bumpSource->isOfType(SoBumpMap::getClassTypeId())) {
    SbVec2s bumpSize;
    int bumpComponents = 0;
    static_cast<const SoBumpMap *>(bumpSource)->image.getValue(bumpSize, bumpComponents);
    if (bumpSize != SbVec2s(0, 0)) return;
  }
  int last = -1;
  const SbBool * enabled = SoMultiTextureEnabledElement::getEnabledUnits(state, last);
  for (int unit = 0; unit <= last; ++unit)
    if (enabled[unit]) return;
  // User coordinate functions can have arbitrary state side effects.
  if (SoMultiTextureCoordinateElement::getType(state, 0) ==
      SoMultiTextureCoordinateElement::FUNCTION) return;
  this->stableShape = node;
  this->reuseCubeVertices = type == SoCube::getClassTypeId();
  if (this->reuseCubeVertices)
    std::fill(this->cubeVertexSlots, this->cubeVertexSlots + 48, UINT32_MAX);
  this->captureCubeTemplate = this->reuseCubeVertices &&
    SoMaterialBindingElement::get(state) == SoMaterialBindingElement::OVERALL;
  if (this->captureCubeTemplate) {
    const auto * cube = static_cast<const SoCube *>(node);
    this->cubeCaptureDimensions[0] = cube->width.getValue();
    this->cubeCaptureDimensions[1] = cube->height.getValue();
    this->cubeCaptureDimensions[2] = cube->depth.getValue();
    this->cubeCaptureNormalBinding = SoNormalBindingElement::get(state);
    this->cubeCaptureFirstVertex = this->currentPlan.vertices.size();
    this->cubeCaptureFirstIndex = this->currentPlan.indices.size();
    this->cubeCaptureFirstDraw = this->currentPlan.draws.size();
  }
}

void
CoinRenderFramePlanBuilder::endShape()
{
  if (!this->hasError && !this->isUnsupported && !this->bumpShapeTriangleIndices.empty()) {
    std::vector<CoinRenderBumpTangents> tangents;
    std::string diagnostic;
    if (!coin_render_legacy_bump_corner_tangents(this->bumpShapeCorners,
                                                 this->bumpShapeTriangleIndices,
                                                 tangents, diagnostic)) {
      this->hasError = true;
      this->builderError = diagnostic;
    } else {
      for (size_t i = 0; i < tangents.size(); ++i) {
        CoinRenderBumpTangentOverride entry;
        entry.vertexIndex = this->bumpShapeVertices[i];
        entry.tangent = tangents[i];
        this->currentPlan.bumpTangents.push_back(entry);
      }
    }
  }
  this->bumpShapeCorners.clear();
  this->bumpShapeVertices.clear();
  this->bumpShapeTriangleIndices.clear();
  this->bumpCornerByVertex.clear();
  if (this->captureCubeTemplate)
    this->cubeGeometryCore.learn(this->currentPlan, this->cubeCaptureFirstVertex,
      this->cubeCaptureFirstIndex, this->cubeCaptureFirstDraw,
      this->cubeCaptureDimensions, this->cubeCaptureNormalBinding);
  this->captureCubeTemplate = false;
  this->stableShape = nullptr;
  this->reuseCubeVertices = false;
  this->shapeRenderStates.clear();
}

void
CoinRenderFramePlanBuilder::recordBumpTriangle(uint32_t a, uint32_t b, uint32_t c)
{
  if (this->bumpShapeCorners.empty()) return;
  const auto x = this->bumpCornerByVertex.find(a);
  const auto y = this->bumpCornerByVertex.find(b);
  const auto z = this->bumpCornerByVertex.find(c);
  if (x == this->bumpCornerByVertex.end() || y == this->bumpCornerByVertex.end() ||
      z == this->bumpCornerByVertex.end()) return;
  this->bumpShapeTriangleIndices.push_back(x->second);
  this->bumpShapeTriangleIndices.push_back(y->second);
  this->bumpShapeTriangleIndices.push_back(z->second);
}

bool
CoinRenderFramePlanBuilder::captureStoredTextureAlpha(SoCallbackAction * action, bool & inheritedTextureAlpha)
{
  inheritedTextureAlpha = false;
  SoState * state = action->getState();
  auto fail = [&](const std::string & message, bool unsupported = false) {
    this->hasError = true;
    this->isUnsupported = this->isUnsupported || unsupported;
    this->builderError = message;
    return false;
  };
  // GLImage sets TRANSP_TEXTURE from every stored image, independently of
  // enabled/quality. The generic callback image element never sets that
  // style flag, and disableAll only clears TEXENABLED. Snapshot the same
  // source classification for boxes and markers independently of sampling.
  // This scan does not capture UV functions, sampler state or texture uploads.
  class ImageUnitAccess : public SoMultiTextureImageElement {
  public:
    static int count(const SoMultiTextureImageElement * element) {
      // A protected member pointer is formed in derived-class scope and
      // invoked on the actual Base instance. No downcast or fake derived
      // object is involved; no public Coin element API is changed.
      const auto getter = &ImageUnitAccess::getNumUnits;
      return (element->*getter)();
    }
  };
  const int imageStack = SoMultiTextureImageElement::getClassStackIndex();
  if (!state->isElementEnabled(imageStack))
    return fail("Transparency capture has no inherited image element");
  const auto * images = static_cast<const SoMultiTextureImageElement *>(state->getConstElement(imageStack));
  const int units = ImageUnitAccess::count(images);
  if (units < 0 || units > static_cast<int>(COIN_RENDER_MAX_TEXTURE_UNITS))
    return fail("UNSUPPORTED: stored-image transparency classification supports at most eight inherited image units", true);
  size_t classifiedBytes = 0;
  for (int unit = 0; unit < units; ++unit) {
    SbVec3s size;
    int components = 0;
    const unsigned char * bytes = SoMultiTextureImageElement::getImage(state, unit, size, components);
    const auto producer = this->sceneTextures.find(bytes);
    if (producer != this->sceneTextures.end()) {
      const int32_t policy = producer->second.transparencyFunction;
      if (!coin_render_scene_texture_policy_supported(policy))
        return fail("UNSUPPORTED: inherited scene texture has an unknown transparency policy", true);
      if (!producer->second.producerId && coin_render_scene_texture_forces_transparency(policy))
        return fail("UNSUPPORTED: Stored-image transparency from an inactive scene texture depends on native GL image history", true);
      inheritedTextureAlpha = inheritedTextureAlpha || coin_render_scene_texture_forces_transparency(policy);
      continue; // Native FORCE flags precede pixel-alpha inspection.
    }
    if (size[0] < 0 || size[1] < 0 || size[2] < 0 || components < 0 || components > 4)
      return fail("Inherited image has invalid dimensions or component count");
    if (!size[0] || !size[1]) continue; // Default/cleared image: native GLImage is absent.
    if (components < 1)
      return fail("Inherited image has no components for a nonempty image");
    if (components != 2 && components != 4) continue;
    // Native images without CPU bytes conservatively classify their alpha
    // base format as transparent. Ordinary callback images have owned bytes;
    // unresolved scene producers were handled by their force policy above.
    if (!bytes) { inheritedTextureAlpha = true; continue; }
    const size_t remaining = 128 * 1024 * 1024 - classifiedBytes;
    size_t pixels = 1;
    const size_t dimensions[] = {size_t(size[0]), size_t(size[1]), size_t(size[2] ? size[2] : 1)};
    for (size_t dimension : dimensions) {
      if (dimension > remaining / pixels)
        return fail("UNSUPPORTED: stored-image transparency classification exceeds 128 MiB of inherited alpha images", true);
      pixels *= dimension;
    }
    if (pixels > remaining / size_t(components))
      return fail("UNSUPPORTED: stored-image transparency classification exceeds 128 MiB of inherited alpha images", true);
    const size_t scanBytes = pixels * size_t(components);
    classifiedBytes += scanBytes;
    // SoGLImage::checkTransparency treats zero alpha as transparent too,
    // even when it could select alpha testing for an ordinary textured shape.
    inheritedTextureAlpha = inheritedTextureAlpha ||
      coin_render_image_has_transparency(bytes, pixels, components);
  }
  return true;
}

bool
CoinRenderFramePlanBuilder::captureBoundingBox(SoCallbackAction * action, SoNode * node)
{
  this->endShape();
  if (isShapeInvisible(action)) return true;
  SbBox3f box; SbVec3f sortingCenter;
  static_cast<SoShape *>(node)->computeBBox(action, box, sortingCenter);
  if (!coin_render_bounding_box_valid(box, this->builderError)) {
    this->hasError = true;
    return false;
  }
  // The native empty cube has no finite drawable surface.
  if (box.isEmpty()) return true;
  SoState * state = action->getState();
  int last = -1;
  const SbBool * enabled = SoMultiTextureEnabledElement::getEnabledUnits(state, last);
  for (int unit = 1; unit <= last; ++unit)
    if (enabled[unit]) {
      this->isUnsupported = true;
      this->builderError = "Bounding-box complexity with additional textures requires persistent GL texture coordinates";
      return false;
    }
  if (last >= 0 && enabled[0] && SoMultiTextureCoordinateElement::getType(state, 0) ==
      SoMultiTextureCoordinateElement::FUNCTION) {
    this->isUnsupported = true;
    this->builderError = "Bounding-box complexity with texture-coordinate functions requires a texgen contract";
    return false;
  }
  state->push();
  // Native GLRenderBoundingBox ignores authored UV arrays and emits cube UV.
  static const SbVec2f uv[] = {{1, 1}, {0, 1}, {0, 0}, {1, 0}};
  SoMultiTextureCoordinateElement::set2(state, node, 0, 4, uv);
  const uint32_t slot = this->captureRenderState(action, 0, true, true);
  state->pop();
  if (this->hasError || this->isUnsupported) return false;
  const auto snapshot = this->currentPlan.renderStates[slot];
  std::array<CoinRenderVertexSnapshot, 24> vertices;
  if (!coin_render_bounding_box_vertices(box, snapshot.materialSlot, vertices, this->builderError)) {
    this->hasError = true;
    return false;
  }
  const int style = polygonDrawStyle(action);
  if (style == SoDrawStyleElement::FILLED) {
    this->ensureDrawPacket(CoinRenderPrimitiveTopology::TRIANGLE_LIST, slot, node, true);
    this->captureSortingCenter(action);
    auto & draw = this->currentPlan.draws[this->currentDrawIndex];
    const uint32_t first = static_cast<uint32_t>(this->currentPlan.vertices.size());
    this->currentPlan.vertices.insert(this->currentPlan.vertices.end(), vertices.begin(), vertices.end());
    static const uint32_t triangle[] = {0, 1, 2, 0, 2, 3};
    for (uint32_t face = 0; face < 6; ++face)
      for (uint32_t corner : triangle) this->currentPlan.indices.push_back(first + face * 4 + corner);
    draw.geometry.vertexCount = 24; draw.geometry.indexCount = 36;
  } else {
    for (size_t face = 0; face < 6; ++face) {
      this->polygonNode = node; this->polygonState = slot; this->polygonStyle = style;
      this->polygonVertices.assign(vertices.begin() + face * 4, vertices.begin() + face * 4 + 4);
      this->emitStyledPolygon(action, true);
      this->polygonNode = nullptr; this->polygonVertices.clear();
      if (this->hasError || this->isUnsupported) return false;
    }
  }
  this->hasActiveDraw = false;
  return true;
}

bool
CoinRenderFramePlanBuilder::replayNativeCube(SoCallbackAction * action, SoNode * node)
{
  if (!this->captureCubeTemplate || this->stableShape != node ||
      !this->cubeGeometryCore.matches(this->cubeCaptureDimensions, this->cubeCaptureNormalBinding))
    return false;
  const uint32_t stateSlot = this->captureRenderState(action, 0);
  this->ensureDrawPacket(CoinRenderPrimitiveTopology::TRIANGLE_LIST, stateSlot, node);
  this->captureSortingCenter(action);
  const uint32_t materialSlot = this->currentPlan.renderStates[stateSlot].materialSlot;
  auto & draw = this->currentPlan.draws[this->currentDrawIndex];
  if (!this->cubeGeometryCore.replay(this->currentPlan, draw, materialSlot,
                                    this->cubeCaptureDimensions, this->cubeCaptureNormalBinding)) return false;
  this->captureCubeTemplate = false;
  ++this->cubeReplayHits;
  return true;
}

void
CoinRenderFramePlanBuilder::beginAnnotation(bool clearDepth)
{
  if (this->annotationDepth++ == 0) {
    this->savedAnnotationLayer = this->currentAnnotationLayer;
    this->savedAnnotationClear = this->annotationDepthClearPending;
    this->currentAnnotationLayer = this->nextAnnotationLayer++;
    this->annotationDepthClearPending = clearDepth || this->deferredDepthClear;
    this->deferredDepthClear = false;
    this->hasActiveDraw = false;
  }
}

void
CoinRenderFramePlanBuilder::beginAuthoredScene(bool clearDepth)
{
  // A manager scene boundary orders the base after its backgrounds; it is not
  // an SoAnnotation nesting level. An annotation inside this scene must enter
  // its own immediate layer and then restore the authored scene's state.
  this->authoredSceneParentLayer = this->currentAnnotationLayer;
  this->authoredSceneParentClear = this->annotationDepthClearPending;
  this->currentAnnotationLayer = this->nextAnnotationLayer++;
  this->authoredSceneLayer = this->currentAnnotationLayer;
  this->annotationDepthClearPending = clearDepth || this->deferredDepthClear;
  this->deferredDepthClear = false;
  this->hasActiveDraw = false;
}

void
CoinRenderFramePlanBuilder::endAuthoredScene()
{
  this->deferredDepthClear = this->deferredDepthClear || this->annotationDepthClearPending;
  this->currentAnnotationLayer = this->authoredSceneParentLayer;
  this->annotationDepthClearPending = this->authoredSceneParentClear;
  this->hasActiveDraw = false;
}

void
CoinRenderFramePlanBuilder::beginForeground()
{
  this->foregroundLayer = this->currentAnnotationLayer;
  this->currentAnnotationLayer = this->nextAnnotationLayer++;
  this->annotationDepthClearPending = this->deferredDepthClear;
  this->deferredDepthClear = false;
  this->hasActiveDraw = false;
}

void
CoinRenderFramePlanBuilder::endForeground()
{
  this->deferredDepthClear = this->deferredDepthClear || this->annotationDepthClearPending;
  this->currentAnnotationLayer = this->foregroundLayer;
  this->annotationDepthClearPending = false;
  this->hasActiveDraw = false;
}

uint32_t
CoinRenderFramePlanBuilder::reserveDelayedLayers(uint32_t count)
{
  // Delayed paths follow the authored scene, including when manager backgrounds
  // precede it. Only foreground layers move to make room for these paths.
  const uint32_t first = this->authoredSceneLayer == UINT32_MAX ? 1 : this->authoredSceneLayer + 1;
  for (CoinRenderDrawPacket & draw : this->currentPlan.draws)
    if (draw.renderLayer >= first) draw.renderLayer += count;
  this->nextAnnotationLayer += count;
  return first;
}

void
CoinRenderFramePlanBuilder::beginDelayedAnnotations(uint32_t layer, bool clearDepth)
{
  this->savedAnnotationLayer = 0;
  this->savedAnnotationClear = false;
  this->annotationDepth = 1;
  this->currentAnnotationLayer = layer;
  // Delayed paths are replayed after traversal but ordered before ordinary
  // annotation layers. A clear deferred by an ordinary layer cannot flow
  // backwards into these earlier passes.
  this->annotationDepthClearPending = clearDepth;
  this->hasActiveDraw = false;
}

void
CoinRenderFramePlanBuilder::endAnnotation()
{
  if (this->annotationDepth == 0) return;
  if (--this->annotationDepth == 0) {
    this->deferredDepthClear = this->deferredDepthClear || this->annotationDepthClearPending;
    this->currentAnnotationLayer = this->savedAnnotationLayer;
    this->annotationDepthClearPending = this->savedAnnotationClear;
    this->hasActiveDraw = false;
  }
}

void CoinRenderFramePlanBuilder::registerSceneTexture(const unsigned char* image,
                                                      uint64_t producerId, uint32_t width,
                                                      uint32_t height, bool opaque,
                                                      int32_t transparencyFunction,CoinRenderTextureFormat format,uint32_t cubeFaceSize) {
  this->sceneTextures[image] =
      SceneTexture{producerId, width, height, opaque, transparencyFunction,format,cubeFaceSize};
}

void
CoinRenderFramePlanBuilder::registerAuthoredTextureImage(const unsigned char * bytes,
                                                         bool compressed,
                                                         const SoTexture2 * source,
                                                         SbUniqueId revision)
{
  if(bytes) {
    authoredTextureImages.insert(bytes);
    authoredTextureSources[bytes] = {source, revision};
    if(compressed) compressedTextureImages.insert(bytes);
    else compressedTextureImages.erase(bytes);
  }
}

void
CoinRenderFramePlanBuilder::registerAuthoredVolumeImage(const unsigned char * bytes,
                                                        bool compressed,
                                                        const SoTexture3 * source,
                                                        SbUniqueId revision)
{
  if (bytes) {
    this->authoredVolumeImages.insert(bytes);
    this->authoredVolumeSources[bytes] = {source, revision};
    if (compressed) this->compressedVolumeImages.insert(bytes);
    else this->compressedVolumeImages.erase(bytes);
  }
}

void
CoinRenderFramePlanBuilder::beginShadowGroup(const CoinRenderShadowGroupSnapshot & group)
{
  this->currentPlan.shadowGroups.push_back(group);
  this->shadowGroupStack.push_back(static_cast<uint32_t>(this->currentPlan.shadowGroups.size()));
  this->hasActiveDraw = false;
}

void
CoinRenderFramePlanBuilder::endShadowGroup()
{
  if (!this->shadowGroupStack.empty()) this->shadowGroupStack.pop_back();
  this->hasActiveDraw = false;
}

void
CoinRenderFramePlanBuilder::recordShadowLight(const CoinRenderShadowLightSnapshot & light)
{
  // GL searches every descendant light for each active ancestor group.
  for (uint32_t groupSlot : this->shadowGroupStack) {
    auto scoped = light;
    scoped.groupSlot = groupSlot;
    this->currentPlan.shadowLights.push_back(std::move(scoped));
  }
}

void
CoinRenderFramePlanBuilder::recordLightAttenuation(SoCallbackAction * action)
{
  SoState * state = action ? action->getState() : nullptr;
  if (!state) return;
  const int index = SoLightElement::getLights(state).getLength();
  if (index < 0) return;
  const size_t slot = static_cast<size_t>(index);
  if (this->lightAttenuationByIndex.size() <= slot) {
    this->lightAttenuationByIndex.resize(slot + 1, SbVec3f(0.0f, 0.0f, 1.0f));
    this->lightModelByIndex.resize(slot + 1, SbMatrix::identity());
  }
  // The GL path fixes attenuation at the light node, before later SoEnvironment nodes.
  this->lightAttenuationByIndex[slot] = SoEnvironmentElement::getLightAttenuation(state);
  this->lightModelByIndex[slot] = action->getModelMatrix();
}

void
CoinRenderFramePlanBuilder::recordVRMLLight(SoCallbackAction * action, const SoNode * node)
{
#ifdef HAVE_VRML97
  const auto * light = static_cast<const SoVRMLLight *>(node);
  if (!light->on.getValue()) return;
  SoLight * proxy = nullptr;
  SbVec3f attenuation(0, 0, 1);
  if (node->isOfType(SoVRMLDirectionalLight::getClassTypeId())) {
    auto * target = new SoDirectionalLight;
    target->direction = static_cast<const SoVRMLDirectionalLight *>(node)->direction.getValue();
    proxy = target;
  } else if (node->isOfType(SoVRMLPointLight::getClassTypeId())) {
    const auto * source = static_cast<const SoVRMLPointLight *>(node);
    auto * target = new SoPointLight;
    target->location = source->location.getValue();
    const SbVec3f & a = source->attenuation.getValue();
    attenuation.setValue(a[2], a[1], a[0]);
    proxy = target;
  } else if (node->isOfType(SoVRMLSpotLight::getClassTypeId())) {
    const auto * source = static_cast<const SoVRMLSpotLight *>(node);
    auto * target = new SoSpotLight;
    target->location = source->location.getValue();
    target->direction = source->direction.getValue();
    target->cutOffAngle = source->cutOffAngle.getValue();
    // Native VRML GL ignores beamWidth and radius, and uses exponent zero.
    target->dropOffRate = 0.0f;
    const SbVec3f & a = source->attenuation.getValue();
    attenuation.setValue(a[2], a[1], a[0]);
    proxy = target;
  } else {
    this->isUnsupported = true;
    this->builderError = "Unsupported VRML light subtype";
    return;
  }
  proxy->ref();
  this->vrmlLightProxies.push_back(proxy);
  proxy->on = TRUE;
  proxy->color = light->color.getValue();
  proxy->intensity = light->intensity.getValue();
  this->vrmlLightSources[proxy] = {node->getNodeId(), light->ambientIntensity.getValue()};
  this->recordLightAttenuation(action);
  const size_t slot = size_t(SoLightElement::getLights(action->getState()).getLength());
  this->lightAttenuationByIndex[slot] = attenuation;
  // Proxies allow SoLightElement to retain its public SoLight-only contract;
  // its normal push/pop supplies VRML occurrence and group scoping.
  SoLightElement::add(action->getState(), proxy,
    action->getModelMatrix() * action->getViewingMatrix());
#endif
}

uint32_t
CoinRenderFramePlanBuilder::internMaterial(const CoinRenderMaterialSnapshot & material)
{
  if (this->currentPlan.materials.size() < this->materialNext.size()) {
    this->materialHeads.clear(); this->materialNext.clear();
  }
  if (this->materialIndexDisabled || this->currentPlan.materials.size() < MATERIAL_INDEX_THRESHOLD ||
      !this->synchronizeMaterialIndex())
    return CoinRenderPlanAssemblyCore::material(this->currentPlan, material);
  const uint64_t key = materialBytesKey(material);
  const auto head = this->materialHeads.find(key);
  uint32_t matchingSlot = UINT32_MAX;
  for (uint32_t slot = head == this->materialHeads.end() ? UINT32_MAX : head->second;
       slot != UINT32_MAX; slot = this->materialNext[slot]) {
    if (std::memcmp(&this->currentPlan.materials[slot], &material, sizeof(material)) == 0)
      matchingSlot = std::min(matchingSlot, slot);
  }
  if (matchingSlot != UINT32_MAX) return matchingSlot;
  const uint32_t slot = static_cast<uint32_t>(this->currentPlan.materials.size());
  this->currentPlan.materials.push_back(material);
  // Suffix synchronization also handles additions made by styled polygon or
  // stroke assembly. Allocation failure only discards optional metadata.
  this->synchronizeMaterialIndex();
  return slot;
}

uint64_t CoinRenderFramePlanBuilder::materialBytesKey(const CoinRenderMaterialSnapshot & material)
{
  uint64_t key = UINT64_C(14695981039346656037);
  const auto * bytes = reinterpret_cast<const unsigned char *>(&material);
  for (size_t i = 0; i < sizeof(material); ++i)
    key = (key ^ bytes[i]) * UINT64_C(1099511628211);
  return key;
}

void CoinRenderFramePlanBuilder::disableMaterialIndex()
{
  this->materialHeads.clear(); this->materialNext.clear();
  this->materialIndexDisabled = true;
}

bool CoinRenderFramePlanBuilder::synchronizeMaterialIndex()
{
  if (this->materialIndexDisabled) return false;
  if (this->currentPlan.materials.size() > MATERIAL_INDEX_LIMIT) {
    this->disableMaterialIndex(); return false;
  }
  try {
    // The capture table is append-only, including helper paths outside this
    // interner. No pointers into its reallocating vector are retained.
    while (this->materialNext.size() < this->currentPlan.materials.size()) {
      const uint32_t slot = static_cast<uint32_t>(this->materialNext.size());
      const uint64_t key = materialBytesKey(this->currentPlan.materials[slot]);
      auto inserted = this->materialHeads.emplace(key, slot);
      this->materialNext.push_back(inserted.second ? UINT32_MAX : inserted.first->second);
      if (!inserted.second) inserted.first->second = slot;
    }
  } catch (const std::bad_alloc &) {
    this->disableMaterialIndex(); return false;
  }
  return true;
}

uint32_t
CoinRenderFramePlanBuilder::captureMaterial(SoCallbackAction * action, int materialIndex, bool packedDiffuse)
{
  SbColor amb(0.2f, 0.2f, 0.2f), diff(0.8f, 0.8f, 0.8f), spec(0.0f, 0.0f, 0.0f), emiss(0.0f, 0.0f, 0.0f);
  float shin = 0.2f, transp = 0.0f;

  int safeIndex = (materialIndex >= 0) ? materialIndex : 0;
  SoState * state = action ? action->getState() : nullptr;
  if (state) {
    const SoLazyElement * lazy = SoLazyElement::getInstance(state);
    if (lazy) {
      int32_t numDiff = lazy->getNumDiffuse();
      if (numDiff > 0 && safeIndex >= numDiff) {
        safeIndex = numDiff - 1;
      }
    }
  }

  action->getMaterial(amb, diff, spec, emiss, shin, transp, safeIndex);

  CoinRenderMaterialSnapshot matSnap;
  matSnap.ambient[0] = amb[0]; matSnap.ambient[1] = amb[1]; matSnap.ambient[2] = amb[2]; matSnap.ambient[3] = 1.0f;
  matSnap.diffuse[0] = diff[0]; matSnap.diffuse[1] = diff[1]; matSnap.diffuse[2] = diff[2]; matSnap.diffuse[3] = 1.0f - transp;
  matSnap.specular[0] = spec[0]; matSnap.specular[1] = spec[1]; matSnap.specular[2] = spec[2]; matSnap.specular[3] = 1.0f;
  matSnap.emission[0] = emiss[0]; matSnap.emission[1] = emiss[1]; matSnap.emission[2] = emiss[2]; matSnap.emission[3] = 1.0f;
  matSnap.shininess = shin;
  matSnap.transparency = transp;

  if (packedDiffuse) {
    // GLRenderBoundingBox's sendFirst supplies diffuse RGBA through
    // glColor4ub, including when color material feeds PHONG lighting.
    // Preserve that source precision before repeated transparent blending.
    for (int channel = 0; channel < 4; ++channel) {
      const float value = matSnap.diffuse[channel];
      if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
        this->hasError = true;
        this->builderError = "Bounding-box primary RGBA is outside finite [0,1]";
        return 0;
      }
      matSnap.diffuse[channel] = std::floor(value * 255.0f + 0.5f) / 255.0f;
    }
    if (SoShapeStyleElement::getTransparencyType(state) == SoGLRenderAction::SCREEN_DOOR)
      matSnap.diffuse[3] = 1.0f;
    matSnap.transparency = 1.0f - matSnap.diffuse[3];
  }

  if (state) {
    CoinRenderAlphaTestFunction alphaTestFunction;
    float alphaTestReference;
    if (!coin_render_snapshot_alpha_test(state, alphaTestFunction, alphaTestReference)) {
      this->hasError = true;
      this->builderError = "SoAlphaTest state has invalid function or NaN reference";
    } else if (coin_render_alpha_test_active(alphaTestFunction) &&
               std::isfinite(transp) && transp >= 0.0f && transp <= 1.0f &&
               std::isfinite(matSnap.diffuse[3]) &&
               matSnap.diffuse[3] >= 0.0f && matSnap.diffuse[3] <= 1.0f) {
      // Coin sends primary alpha through its packed glColor4ub value before
      // texture operations and alpha testing. Normalize that source here;
      // executors consume the snapshot without a Coin-specific packing rule.
      matSnap.diffuse[3] = SoShapeStyleElement::getTransparencyType(state) == SoGLRenderAction::SCREEN_DOOR
        ? 1.0f : std::floor(matSnap.diffuse[3] * 255.0f + 0.5f) / 255.0f;
      matSnap.transparency = 1.0f - matSnap.diffuse[3];
    }
  }

  return this->internMaterial(matSnap);
}

bool
CoinRenderFramePlanBuilder::captureCubeNode(SoCallbackAction * action,
                                           const SoTextureCubeMap * node,
                                           std::string & diagnostic)
{
  SoState * state = action->getState();
  if (SoTextureOverrideElement::getImageOverride(state)) return true;
  const int unit = SoTextureUnitElement::get(state);
  auto fail = [&](const char * message) {
    diagnostic = this->builderError = message;
    this->isUnsupported = true;
    return false;
  };
  const SoSFImage * fields[] = {&node->imagePosX, &node->imageNegX, &node->imagePosY,
    &node->imageNegY, &node->imagePosZ, &node->imageNegZ};
  SbVec2s sizes[6]; int components[6]; const uint8_t * bytes[6];
  bool any = false, filenames = false;
  for (int i = 0; i < node->filenames.getNum(); ++i)
    filenames = filenames || node->filenames[i].getLength() != 0;
  for (int i = 0; i < 6; ++i) {
    bytes[i] = fields[i]->getValue(sizes[i], components[i]);
    any = any || sizes[i] != SbVec2s(0, 0);
  }
  if (!any && !filenames) return true;
  if (filenames && node->coinRenderFilenamePendingOrFailed())
    return fail("Cube texture filename is pending or failed to load");
  if (unit < 0 || unit >= int(COIN_RENDER_MAX_TEXTURE_UNITS))
    return fail("At most eight cube texture units are supported");
  const int n = sizes[0][0];
  if (n <= 0 || n > 2048 || (n & (n - 1)) || components[0] < 1 || components[0] > 4)
    return fail("Cube textures require square POT faces 1..2048 and components 1..4");
  for (int i = 0; i < 6; ++i)
    if (!bytes[i] || sizes[i] != SbVec2s(short(n), short(n)) || components[i] != components[0])
      return fail("Cube texture has missing or mismatched faces, or pending filenames");
  const int model = node->model.getValue();
  if ((model != SoTextureCubeMap::MODULATE && model != SoTextureCubeMap::REPLACE &&
       model != SoTextureCubeMap::DECAL && model != SoTextureCubeMap::BLEND) ||
      (model == SoTextureCubeMap::DECAL && components[0] < 3))
    return fail("Cube texture model is invalid for the source components");
  const float quality = SoTextureQualityElement::get(state);
  CoinRenderTextureFilter filter;
  if (!CoinRenderTextureSamplingCore::quality(quality, filter))
    return fail("Cube texture quality must be finite in [0,1]");
  if ((this->storedTextureColorSpace != COIN_RENDER_TEXTURE_LINEAR &&
       this->storedTextureColorSpace != COIN_RENDER_TEXTURE_SRGB) ||
      (quality > .85f && this->maxTextureAnisotropy > 1))
    return fail("Cube textures require stored RGBA8 linear/sRGB without anisotropy");
  for (int c = 0; c < 3; ++c) if (!std::isfinite(node->blendColor.getValue()[c]))
    return fail("Cube texture blend color is non-finite");
  this->cubeSources[node->getNodeId()] = node;
  auto * mutableNode = const_cast<SoTextureCubeMap *>(node);
  SoMultiTextureImageElement::set(state, mutableNode, unit, sizes[0], components[0], bytes[0],
    SoMultiTextureImageElement::CLAMP, SoMultiTextureImageElement::CLAMP,
    static_cast<SoMultiTextureImageElement::Model>(model), node->blendColor.getValue());
  SoMultiTextureEnabledElement::enableCubeMap(state, mutableNode, unit);
  return true;
}

bool
CoinRenderFramePlanBuilder::captureTexture(SoCallbackAction * action, CoinRenderRenderStateSnapshot & rs, std::string * outError)
{
  if (!captureTextureUnit(action, 0, rs, outError)) return false;
  SoState * state = action ? action->getState() : nullptr;
  if (!state) return true;
  int lastEnabled = -1;
  SoMultiTextureEnabledElement::getEnabledUnits(state, lastEnabled);
  if (lastEnabled >= static_cast<int>(COIN_RENDER_MAX_TEXTURE_UNITS)) {
    this->isUnsupported = true;
    this->builderError = "At most eight texture units are supported";
    if (outError) *outError = this->builderError;
    return false;
  }
  for (int unit = 1; unit <= lastEnabled; ++unit) {
    if (!SoMultiTextureEnabledElement::get(state, unit)) continue;
    CoinRenderRenderStateSnapshot captured;
    if (!captureTextureUnit(action, unit, captured, outError)) return false;
    rs.extraTextures[unit - 1] = coin_render_texture_unit(captured, 0);
    rs.textureCombines[unit] = captured.textureCombines[0];
    rs.transparentTexture = rs.transparentTexture || captured.transparentTexture;

  }
  for (int unit = 0; unit <= lastEnabled; ++unit) {
    if (!coin_render_texture_unit(rs, unit).enabled || SoTextureCombineElement::isDefault(state, unit)) continue;
    SoTextureCombineElement::UnitData raw;
    SoTextureCombineElement::get(state, unit, raw.rgboperation, raw.alphaoperation,
      raw.rgbsource, raw.alphasource, raw.rgboperand, raw.alphaoperand,
      raw.constantcolor, raw.rgbscale, raw.alphascale);
    if (!coin_render_compile_combine(raw, rs.textureCombines[unit], this->builderError)) {
      this->isUnsupported = true;
      if (outError) *outError = this->builderError;
      return false;
    }
  }
  return true;
}

bool
CoinRenderFramePlanBuilder::captureTextureUnit(SoCallbackAction * action, int unit, CoinRenderRenderStateSnapshot & rs, std::string * outError)
{
  SoState * state = action ? action->getState() : nullptr;
  if (!state) {
    rs.hasTexture = false;
    rs.textureImageSlot = 0;
    rs.samplerSlot = 0;
    return true;
  }

  if (!SoMultiTextureEnabledElement::get(state, unit)) {
    rs.hasTexture = false;
    SbVec2s size;
    int components;
    const auto * bytes = SoMultiTextureImageElement::getImage(state, unit, size, components);
    const auto policy = this->sceneTextures.find(bytes);
    if (policy != this->sceneTextures.end())
      rs.transparentTexture = coin_render_scene_texture_forces_transparency(policy->second.transparencyFunction);
    return true;
  }

  if (SoMultiTextureEnabledElement::getMode(state, unit) == SoMultiTextureEnabledElement::CUBEMAP) {
    auto fail = [&](const char * message) {
      this->isUnsupported = true; this->builderError = message;
      if (outError) *outError = message;
      return false;
    };
    const auto * cubeCoordinates = SoMultiTextureCoordinateElement::getInstance(state);
    if (SoTextureQualityElement::get(state) > 0.0f &&
        cubeCoordinates->getType(unit) == SoMultiTextureCoordinateElement::TEXGEN)
      return fail("Cube texture coordinate generator has no CPU callback");
    SbVec2s markerSize; int markerComponents=0;
    const auto * marker=SoMultiTextureImageElement::getImage(state,unit,markerSize,markerComponents);
    const auto logical=this->sceneTextures.find(marker);
    if (logical!=this->sceneTextures.end() && logical->second.cubeFaceSize) {
      const auto & source=logical->second;
      CoinRenderSamplerSnapshot sampler;
      sampler.wrapS=sampler.wrapT=sampler.wrapR=CoinRenderTextureWrap::CLAMP;
      const float quality=SoTextureQualityElement::get(state);
      if (!CoinRenderTextureSamplingCore::quality(quality,sampler.filter) ||
          (quality>.85f && this->maxTextureAnisotropy>1) ||
          (this->storedTextureColorSpace != COIN_RENDER_TEXTURE_LINEAR &&
           this->storedTextureColorSpace != COIN_RENDER_TEXTURE_SRGB))
        return fail("Cube RTT requires linear RGBA8 and non-anisotropic cube filtering");
      if (quality==0) {rs.hasTexture=false;return true;}
      CoinRenderTextureImageSnapshot image;
      if (!CoinRenderCubeTextureCore::describe(source.cubeFaceSize,
          CoinRenderTextureSamplingCore::mipFilter(sampler.filter),image))
        return fail("Invalid cube RTT atlas layout");
      image.producerId=source.producerId;image.sceneTransparencyFunction=source.transparencyFunction;
      image.gpuOpaque=source.opaque;
      rs.textureImageSlot=CoinRenderPlanAssemblyCore::texture(this->currentPlan,std::move(image));
      rs.samplerSlot=CoinRenderPlanAssemblyCore::sampler(this->currentPlan,sampler);
      SoMultiTextureImageElement::Wrap ws,wt;SoMultiTextureImageElement::Model model;SbColor blend;
      SoMultiTextureImageElement::get(state,unit,markerSize,markerComponents,ws,wt,model,blend);
      rs.textureModel=CoinRenderTextureModel(uint32_t(model)==uint32_t(SoMultiTextureImageElement::MODULATE)?0:
        model==SoMultiTextureImageElement::REPLACE?1:model==SoMultiTextureImageElement::DECAL?2:3);
      for(int c=0;c<3;++c)rs.textureBlendColor[c]=blend[c];rs.textureBlendColor[3]=1;
      rs.textureMatrix=SoMultiTextureMatrixElement::get(state,unit);
      rs.transparentTexture=coin_render_scene_texture_forces_transparency(source.transparencyFunction);
      rs.hasTexture=true;return true;
    }
    const auto * element = static_cast<const SoMultiTextureImageElement *>(
      SoElement::getConstElement(state, SoMultiTextureImageElement::getClassStackIndex()));
    const auto source = this->cubeSources.find(element->getUnitData(unit).nodeid);
    if (source == this->cubeSources.end()) return fail("Unrecorded cube texture source");
    const auto * node = source->second;
    const SoSFImage * fields[] = {&node->imagePosX, &node->imageNegX, &node->imagePosY,
      &node->imageNegY, &node->imagePosZ, &node->imageNegZ};
    CoinRenderSamplerSnapshot sampler;
    sampler.wrapS = sampler.wrapT = sampler.wrapR = CoinRenderTextureWrap::CLAMP;
    const float quality = SoTextureQualityElement::get(state);
    if (!CoinRenderTextureSamplingCore::quality(quality, sampler.filter) ||
        (quality > .85f && this->maxTextureAnisotropy > 1))
      return fail("Cube texture filter or anisotropy is unsupported");
    const bool mips = CoinRenderTextureSamplingCore::mipFilter(sampler.filter);
    SbVec2s size; int components;
    fields[0]->getValue(size, components);
    const int model = node->model.getValue();
    rs.textureModel = model == SoTextureCubeMap::MODULATE ? CoinRenderTextureModel::MODULATE :
      model == SoTextureCubeMap::REPLACE ? CoinRenderTextureModel::REPLACE :
      model == SoTextureCubeMap::DECAL ? CoinRenderTextureModel::DECAL : CoinRenderTextureModel::BLEND;
    if (model == SoTextureCubeMap::REPLACE && (components == 1 || components == 3))
      rs.textureCombines[0] = coin_render_replace_rgb_preserve_alpha();
    for (int c = 0; c < 3; ++c) rs.textureBlendColor[c] = node->blendColor.getValue()[c];
    rs.textureBlendColor[3] = 1.0f;
    rs.transparentTexture = false;
    for (const auto * field : fields) {
      SbVec2s extent; int nc; const auto * bytes = field->getValue(extent, nc);
      rs.transparentTexture = rs.transparentTexture || coin_render_image_has_transparency(
        bytes, size_t(extent[0]) * extent[1], nc);
    }
    if (quality == 0.0f) { rs.hasTexture = false; return true; }
    auto cached = this->capturedCubeImages.find(node->getNodeId());
    if (cached == this->capturedCubeImages.end())
      cached = this->capturedCubeImages.emplace(node->getNodeId(), std::array<int64_t, 2>{{-1, -1}}).first;
    if (cached->second[mips ? 1 : 0] < 0) {
      std::array<CoinRenderCubeTextureCore::Face, 6> faces;
      for (size_t i = 0; i < 6; ++i) {
        SbVec2s extent; int nc; const auto * bytes = fields[i]->getValue(extent, nc);
        faces[i].width = extent[0]; faces[i].height = extent[1]; faces[i].components = nc;
        faces[i].pixels.assign(bytes, bytes + size_t(extent[0]) * extent[1] * nc);
      }
      CoinRenderCubeTextureCore::Atlas atlas;
      const auto format = this->storedTextureColorSpace == COIN_RENDER_TEXTURE_SRGB ?
        CoinRenderTextureFormat::RGBA8_SRGB : CoinRenderTextureFormat::RGBA8_LINEAR;
      if (!CoinRenderCubeTextureCore::prepare(faces, mips, atlas, format))
        return fail("Cube texture atlas exceeds the supported layout or 128 MiB");
      CoinRenderTextureImageSnapshot image;
      image.width = atlas.width; image.height = atlas.height; image.format = atlas.format;
      image.cubeFaceSize = atlas.faceSize; image.cubeSourceComponents = atlas.sourceComponents;
      image.cubeMipmapped = atlas.mipmapped; image.pixelsRgba = std::move(atlas.pixelsRgba);
      image.contentDigest = CoinRenderImageCore::rgba8Digest(image.pixelsRgba) ^
        (uint64_t(image.cubeFaceSize) << 32) ^ (uint64_t(image.cubeSourceComponents) << 8) ^
        (uint64_t(image.format) << 48) ^ uint64_t(mips);
      cached->second[mips ? 1 : 0] = CoinRenderPlanAssemblyCore::texture(this->currentPlan, std::move(image));
    }
    rs.textureImageSlot = uint32_t(cached->second[mips ? 1 : 0]);
    rs.samplerSlot = CoinRenderPlanAssemblyCore::sampler(this->currentPlan, sampler);
    rs.textureMatrix = SoMultiTextureMatrixElement::get(state, unit);
    rs.hasTexture = true;
    return true;
  }

  SbVec3s volumeSize;
  int volumeComponents = 0;
  SoMultiTextureImageElement::Wrap volumeWrapS, volumeWrapT, volumeWrapR;
  SoMultiTextureImageElement::Model volumeModel;
  SbColor volumeBlend;
  const unsigned char * volumeBytes = SoMultiTextureImageElement::get(
      state, unit, volumeSize, volumeComponents, volumeWrapS, volumeWrapT,
      volumeWrapR, volumeModel, volumeBlend);
  if (SoMultiTextureEnabledElement::getMode(state, unit) ==
      SoMultiTextureEnabledElement::TEXTURE3D) {
    auto failVolume = [&](const char * message) {
      this->isUnsupported = true;
      this->builderError = message;
      if (outError) *outError = message;
      return false;
    };
    if (!volumeBytes || volumeSize[0] <= 0 || volumeSize[1] <= 0 || volumeSize[2] <= 0 ||
        volumeComponents < 1 || volumeComponents > 4)
      return failVolume("Invalid SoTexture3 image extent or components");
    const uint32_t vw = uint32_t(volumeSize[0]), vh = uint32_t(volumeSize[1]),
                   vd = uint32_t(volumeSize[2]);
    if (vw > 256 || vh > 256 || vd > 256)
      return failVolume("SoTexture3 requires extents in 1..256 per axis");
    const uint64_t voxelCount = uint64_t(vw) * vh * vd;
    if (voxelCount * 4 > 128ull * 1024ull * 1024ull)
      return failVolume("SoTexture3 base image exceeds 128 MiB");
    if (!this->authoredVolumeImages.count(volumeBytes) && vw == 2 && vh == 2 && vd == 2 &&
        volumeComponents == 1 &&
        std::all_of(volumeBytes, volumeBytes + 8, [](uint8_t v) { return v == 0xff; }))
      return failVolume("Pending or missing SoTexture3 filename produced Coin's dummy image");
    if (this->storedTextureColorSpace != COIN_RENDER_TEXTURE_LINEAR &&
        this->storedTextureColorSpace != COIN_RENDER_TEXTURE_SRGB)
      return failVolume("Invalid SoTexture3 stored color space");
    const auto volumeFormat = this->storedTextureColorSpace == COIN_RENDER_TEXTURE_SRGB ?
        CoinRenderTextureFormat::RGBA8_SRGB : CoinRenderTextureFormat::RGBA8_LINEAR;
    if (this->compressedVolumeImages.count(volumeBytes))
      return failVolume("Compressed SoTexture3 images are outside the stored RGBA8 profile");
    if (SoTextureScalePolicyElement::get(state) == SoTextureScalePolicyElement::FRACTURE)
      return failVolume("SoTexture3 with FRACTURE scale policy is unsupported");
    auto wrap = [](SoMultiTextureImageElement::Wrap value, CoinRenderTextureWrap &result) {
      if (value == SoMultiTextureImageElement::REPEAT) {
        result = CoinRenderTextureWrap::REPEAT; return true;
      }
      if (value == SoMultiTextureImageElement::CLAMP) {
        result = CoinRenderTextureWrap::CLAMP; return true;
      }
      return false;
    };
    CoinRenderSamplerSnapshot sampler;
    if (!wrap(volumeWrapS, sampler.wrapS) || !wrap(volumeWrapT, sampler.wrapT) ||
        !wrap(volumeWrapR, sampler.wrapR))
      return failVolume("SoTexture3 wrap must be REPEAT or CLAMP in all three axes");
    const float quality = SoTextureQualityElement::get(state);
    if (!CoinRenderTextureSamplingCore::quality(quality, sampler.filter))
      return failVolume("SoTexture3 quality must be finite in [0,1]");
    if (quality > .85f && this->maxTextureAnisotropy > 1)
      return failVolume("SoTexture3 anisotropic sampling has no portable contract");
    if (volumeModel == SoMultiTextureImageElement::MODULATE)
      rs.textureModel = CoinRenderTextureModel::MODULATE;
    else if (volumeModel == SoMultiTextureImageElement::REPLACE) {
      rs.textureModel = CoinRenderTextureModel::REPLACE;
      if (volumeComponents == 1 || volumeComponents == 3)
        // captureTextureUnit returns one layer through slot zero; its caller
        // moves that normalized combine to the requested extra unit.
        rs.textureCombines[0] = coin_render_replace_rgb_preserve_alpha();
    } else if (volumeModel == SoMultiTextureImageElement::DECAL) {
      if (volumeComponents < 3) return failVolume("SoTexture3 DECAL requires RGB or RGBA");
      rs.textureModel = CoinRenderTextureModel::DECAL;
    } else if (volumeModel == SoMultiTextureImageElement::BLEND)
      rs.textureModel = CoinRenderTextureModel::BLEND;
    else return failVolume("Unsupported SoTexture3 texture model");
    rs.textureBlendColor[0] = volumeBlend[0];
    rs.textureBlendColor[1] = volumeBlend[1];
    rs.textureBlendColor[2] = volumeBlend[2];
    rs.textureBlendColor[3] = 1.0f;
    if (quality == 0.0f) {
      rs.transparentTexture = coin_render_image_has_transparency(
          volumeBytes, size_t(voxelCount), volumeComponents);
      rs.hasTexture = false;
      rs.textureImageSlot = rs.samplerSlot = 0;
      return true;
    }
    const bool needsMips = CoinRenderTextureSamplingCore::mipFilter(sampler.filter);
    const auto source = this->authoredVolumeSources.find(volumeBytes);
    const auto cached = this->capturedVolumeImages.find(volumeBytes);
    if (source != this->authoredVolumeSources.end() &&
        cached != this->capturedVolumeImages.end() &&
        cached->second.node == source->second.node &&
        cached->second.revision == source->second.revision &&
        cached->second.width == vw && cached->second.height == vh &&
        cached->second.depth == vd && cached->second.components == uint32_t(volumeComponents) &&
        cached->second.mipmapped == needsMips &&
        cached->second.imageSlot < this->currentPlan.textures.size() &&
        this->currentPlan.textures[cached->second.imageSlot].format == volumeFormat &&
        CoinRenderTextureSamplingCore::validMipImage(
            this->currentPlan.textures[cached->second.imageSlot])) {
      rs.transparentTexture = cached->second.transparent;
      rs.textureImageSlot = cached->second.imageSlot;
      rs.samplerSlot = CoinRenderPlanAssemblyCore::sampler(this->currentPlan, sampler);
      rs.textureMatrix = SoMultiTextureMatrixElement::get(state, unit);
      rs.hasTexture = true;
      return true;
    }
    rs.transparentTexture = coin_render_image_has_transparency(
        volumeBytes, size_t(voxelCount), volumeComponents);
    std::vector<uint8_t> rgba;
    if (!CoinRenderImageCore::convertToRgba8(
            volumeBytes, size_t(voxelCount), volumeComponents, rgba))
      return failVolume("SoTexture3 RGBA8 conversion failed");
    CoinRenderTextureImageSnapshot image;
    if (!CoinRenderTextureSamplingCore::packVolumeAtlas(rgba, vw, vh, vd,
          uint32_t(volumeComponents), needsMips, image, volumeFormat))
      return failVolume("SoTexture3 atlas or mip chain exceeds supported memory and dimensions");
    image.contentDigest = CoinRenderImageCore::rgba8Digest(image.pixelsRgba) ^
        (uint64_t(vw) << 48) ^ (uint64_t(vh) << 32) ^ (uint64_t(vd) << 16) ^
        (uint64_t(volumeComponents) << 8) ^ uint64_t(image.volumeMipmapped) ^
        (uint64_t(image.format) << 1);
    rs.textureImageSlot = CoinRenderPlanAssemblyCore::texture(this->currentPlan, std::move(image));
    if (source != this->authoredVolumeSources.end())
      this->capturedVolumeImages[volumeBytes] = {source->second.node, source->second.revision,
          vw, vh, vd, uint32_t(volumeComponents), needsMips,
          rs.textureImageSlot, rs.transparentTexture};
    rs.samplerSlot = CoinRenderPlanAssemblyCore::sampler(this->currentPlan, sampler);
    rs.textureMatrix = SoMultiTextureMatrixElement::get(state, unit);
    rs.hasTexture = true;
    return true;
  }

  // Fetch this unit image
  SbVec2s imgSize;
  int numComponents = 0;
  SoMultiTextureImageElement::Wrap wrapS;
  SoMultiTextureImageElement::Wrap wrapT;
  SoMultiTextureImageElement::Model model;
  SbColor blendColor;
  const unsigned char * rawBytes = SoMultiTextureImageElement::get(state, unit, imgSize, numComponents, wrapS, wrapT, model, blendColor);

  if (!rawBytes || imgSize[0] <= 0 || imgSize[1] <= 0 || numComponents <= 0) {
    rs.hasTexture = false;
    rs.textureImageSlot = 0;
    rs.samplerSlot = 0;
    return true;
  }

  // 3. Texture quality
  float quality = SoTextureQualityElement::get(state);
  if (quality == 0.0f) {
    // The moments shader ignores textures, but Coin still classifies an
    // image's alpha for shouldGLRender(SHADOWMAP).
    rs.transparentTexture = coin_render_image_has_transparency(
      rawBytes, size_t(imgSize[0]) * size_t(imgSize[1]), numComponents);
    // Texture disabled by quality
    rs.hasTexture = false;
    rs.textureImageSlot = 0;
    rs.samplerSlot = 0;
    return true;
  }
  CoinRenderTextureFilter currentFilter;
  if (!CoinRenderTextureSamplingCore::quality(quality, currentFilter)) {
    this->isUnsupported = true;
    this->builderError = "Texture quality must be finite in [0,1]";
    if (outError) *outError = this->builderError;
    return false;
  }
  // The GL image stores textureQuality at setData() time. A new GL context
  // rebuilds its texture from that value without refreshing the upload.
  const char * disableNpot = std::getenv("COIN_GLGLUE_DISABLE_NON_POWER_OF_TWO_TEXTURES");
  const bool legacyPotOverride = disableNpot && std::atoi(disableNpot) != 0;
  const auto authored = this->authoredTextureSources.find(rawBytes);
  const SoTexture2 * sourceNode = authored == this->authoredTextureSources.end() ?
    nullptr : authored->second.node;
  const SbUniqueId sourceRevision = sourceNode ? authored->second.revision : 0;
  SbBool glUploadValid = FALSE, glScaleDown = FALSE, glCompressed = FALSE;
  float glQuality = quality;
  const bool glImageExists = sourceNode &&
    sourceNode->getGLImageUploadHints(glUploadValid, glScaleDown, glCompressed, glQuality);
  if (sourceNode) quality = glQuality;
  if (SoTextureScalePolicyElement::get(state) ==
      SoTextureScalePolicyElement::FRACTURE) {
    this->isUnsupported = true;
    this->builderError =
      "SoTextureScalePolicy FRACTURE requires subtexture geometry clipping";
    if (outError) *outError = this->builderError;
    return false;
  }
  if (SoMultiTextureCoordinateElement::getType(state, unit) == SoMultiTextureCoordinateElement::FUNCTION)
    this->coordinateFunctionsCaptured = true;

  const char * names[]={"COIN_TEX2_LINEAR_LIMIT","COIN_TEX2_MIPMAP_LIMIT","COIN_TEX2_LINEAR_MIPMAP_LIMIT","COIN_TEX2_ANISOTROPIC_LIMIT"};
  const float defaults[]={.2f,.5f,.8f,.85f};
  for(unsigned i=0;i<4;++i)if(const char * value=std::getenv(names[i])) {
    const float limit=std::atof(value);
    if(!std::isfinite(limit) || limit!=defaults[i]) {
      this->isUnsupported=true;this->builderError="P07 requires default Coin texture-quality thresholds";
      if(outError)*outError=this->builderError;return false;
    }
  }
  CoinRenderTextureFilter filter;
  if (!CoinRenderTextureSamplingCore::quality(quality,filter)) {
    if (outError) *outError = "Texture quality must be finite in [0,1]";
    this->isUnsupported = true;
    this->builderError = outError ? *outError : "Unsupported P07 texture quality";
    return false;
  }

  // 4. Check for Coin dummy texture injected for missing/pending filenames (2x2, 1 component, all 0xff)
  if (!this->authoredTextureImages.count(rawBytes) &&
      imgSize[0] == 2 && imgSize[1] == 2 && numComponents == 1 &&
      rawBytes[0] == 0xff && rawBytes[1] == 0xff && rawBytes[2] == 0xff && rawBytes[3] == 0xff) {
    if (outError) *outError = "Pending or missing texture file detected (dummy texture rejected in Subwave 3B)";
    this->isUnsupported = true;
    this->builderError = (outError ? *outError : "Pending or missing texture file detected");
    return false;
  }

  // 5. Wrap modes (switch on Coin enum values)
  CoinRenderTextureWrap snapWrapS = CoinRenderTextureWrap::REPEAT;
  CoinRenderTextureWrap snapWrapT = CoinRenderTextureWrap::REPEAT;
  switch (wrapS) {
    case SoMultiTextureImageElement::REPEAT:
      snapWrapS = CoinRenderTextureWrap::REPEAT;
      break;
    case SoMultiTextureImageElement::CLAMP:
      snapWrapS = CoinRenderTextureWrap::CLAMP;
      break;
    default:
      if (outError) *outError = "Unsupported wrapS mode (only REPEAT and CLAMP supported in Subwave 3B)";
      this->isUnsupported = true;
      this->builderError = (outError ? *outError : "Unsupported wrapS mode");
      return false;
  }

  switch (wrapT) {
    case SoMultiTextureImageElement::REPEAT:
      snapWrapT = CoinRenderTextureWrap::REPEAT;
      break;
    case SoMultiTextureImageElement::CLAMP:
      snapWrapT = CoinRenderTextureWrap::CLAMP;
      break;
    default:
      if (outError) *outError = "Unsupported wrapT mode (only REPEAT and CLAMP supported in Subwave 3B)";
      this->isUnsupported = true;
      this->builderError = (outError ? *outError : "Unsupported wrapT mode");
      return false;
  }

  // 6. Texture model (switch on Coin enum values)
  switch (model) {
    case SoMultiTextureImageElement::MODULATE:
      rs.textureModel = CoinRenderTextureModel::MODULATE;
      break;
    case SoMultiTextureImageElement::REPLACE:
      rs.textureModel = CoinRenderTextureModel::REPLACE;
      // Canonical RGBA storage adds opaque alpha to RGB/luminance images.
      // Legacy REPLACE keeps the previous alpha for these base formats.
      // An authored SoTextureCombine, compiled after capture, still takes precedence.
      if (numComponents == 1 || numComponents == 3)
        rs.textureCombines[0] = coin_render_replace_rgb_preserve_alpha();
      break;
    case SoMultiTextureImageElement::DECAL:
      rs.textureModel = CoinRenderTextureModel::DECAL;
      break;
    case SoMultiTextureImageElement::BLEND:
      rs.textureModel = CoinRenderTextureModel::BLEND;
      break;
    default:
      if (outError) *outError = "Unsupported texture model";
      this->isUnsupported = true;
      this->builderError = (outError ? *outError : "Unsupported texture model");
      return false;
  }
  rs.textureBlendColor[0] = blendColor[0];
  rs.textureBlendColor[1] = blendColor[1];
  rs.textureBlendColor[2] = blendColor[2];
  rs.textureBlendColor[3] = 1.0f;

  if(model==SoMultiTextureImageElement::DECAL && numComponents<3) {
    this->isUnsupported=true;this->builderError="P07 DECAL requires RGB/RGBA; luminance DECAL has no portable legacy contract";
    if(outError)*outError=this->builderError;return false;
  }
  // 7. Canonical RGBA8 conversion and strict opacity validation
  const auto sceneTexture = this->sceneTextures.find(rawBytes);
  bool isSceneTexture = sceneTexture != this->sceneTextures.end() && imgSize[0] == 1 &&
                        imgSize[1] == 1 && numComponents == 4;
  if (isSceneTexture) {
    for (unsigned int i = 0; i < 4; ++i) {
      if (rawBytes[i] != static_cast<uint8_t>(sceneTexture->second.producerId >> (i * 8))) {
        isSceneTexture = false;
        break;
      }
    }
  }
  uint32_t w = isSceneTexture ? sceneTexture->second.width : static_cast<uint32_t>(imgSize[0]);
  uint32_t h = isSceneTexture ? sceneTexture->second.height : static_cast<uint32_t>(imgSize[1]);
  if (w > 8192 || h > 8192) {
    if (outError) *outError = "Texture dimensions exceed 8192";
    this->isUnsupported = true;
    this->builderError = (outError ? *outError : "Texture dimensions exceed 8192");
    return false;
  }

  std::vector<uint8_t> rgba;
  if (!isSceneTexture) {
    if (!CoinRenderImageCore::convertToRgba8(rawBytes, size_t(w) * h, numComponents, rgba)) {
      if (outError) *outError = "Unsupported number of texture components";
      this->isUnsupported = true;
      this->builderError = "Unsupported number of texture components";
      return false;
    }
  }

  CoinRenderTextureImageSnapshot tSnap;
  tSnap.width = w; tSnap.height = h; tSnap.components = 4;
  tSnap.format=!isSceneTexture && this->storedTextureColorSpace==COIN_RENDER_TEXTURE_SRGB?
    CoinRenderTextureFormat::RGBA8_SRGB:CoinRenderTextureFormat::RGBA8_LINEAR;
  if(isSceneTexture)tSnap.format=sceneTexture->second.format;
  tSnap.sceneTransparencyFunction = isSceneTexture ? sceneTexture->second.transparencyFunction : -1;
  tSnap.gpuOpaque = isSceneTexture && sceneTexture->second.opaque;
  tSnap.producerId = isSceneTexture ? sceneTexture->second.producerId : 0;
  tSnap.pixelsRgba = std::move(rgba);
  // The legacy GL override makes POT resizing observable even on NPOT hardware.
  // Match its nearest, simage and GLU fallback paths.
  bool useCompressedTexture = !isSceneTexture && this->compressedTextureImages.count(rawBytes);
  const SoTexture2 * pendingPotNode = nullptr;
  LegacyPotImage pendingPotImage{};
  if (!isSceneTexture && legacyPotOverride) {
    if (const char * scaleLimit = std::getenv("COIN_TEX2_SCALEUP_LIMIT")) {
      if (static_cast<float>(std::atof(scaleLimit)) != .7f) {
        this->isUnsupported = true;
        this->builderError = "Legacy POT resize requires the default scale-up limit";
        if (outError) *outError = this->builderError;
        return false;
      }
    }
    auto cached = sourceNode ? this->legacyPotImages.find(sourceNode) :
                               this->legacyPotImages.end();
    bool priorCompression = false;
    if (cached != this->legacyPotImages.end() &&
        cached->second.revision != sourceRevision) {
      priorCompression = cached->second.compressed;
      this->legacyPotBytes -= cached->second.pixels.size();
      this->legacyPotImages.erase(cached);
      cached = this->legacyPotImages.end();
    }
    if (cached != this->legacyPotImages.end()) {
      tSnap.width = cached->second.width;
      tSnap.height = cached->second.height;
      tSnap.pixelsRgba = cached->second.pixels;
      useCompressedTexture = cached->second.compressed;
    } else {
      useCompressedTexture = useCompressedTexture || priorCompression;
      const auto policy = SoTextureScalePolicyElement::get(state);
      // With a valid upload, SoTexture2 skips setData() in a new context:
      // current hints are ignored and only persistent SoGLImage flags apply.
      // After notification, setData() ORs the new hints into those flags.
      const bool down = glUploadValid ? bool(glScaleDown) :
          (policy == SoTextureScalePolicyElement::SCALE_DOWN || glScaleDown);
      useCompressedTexture = glUploadValid ? bool(glCompressed) :
          (useCompressedTexture || (glImageExists && glCompressed));
      // SoGLImage starts with USE_QUALITY_VALUE, including for SCALE_UP.
      const bool useQuality = !down;
      const uint32_t newWidth = CoinRenderTextureSamplingCore::legacyPotExtent(
          w, down, useQuality, quality);
      const uint32_t newHeight = CoinRenderTextureSamplingCore::legacyPotExtent(
          h, down, useQuality, quality);
      if (newWidth != w || newHeight != h) {
        if (SoTextureScaleQualityElement::get(state) < .5f) {
          if (!CoinRenderTextureSamplingCore::legacyResizeNearest(tSnap, newWidth, newHeight)) {
            this->isUnsupported = true;
            this->builderError = "Legacy POT nearest resize exceeds image limits";
            if (outError) *outError = this->builderError;
            return false;
          }
        } else {
          const auto * simage = simage_wrapper();
          if (simage->available && simage->versionMatchesAtLeast(1, 1, 1) &&
              simage->simage_resize && simage->simage_free_image) {
            if (size_t(newWidth) * newHeight * 4 > 128u * 1024u * 1024u) {
              this->isUnsupported = true;
              this->builderError = "Legacy high-quality POT resize exceeds image limits";
              if (outError) *outError = this->builderError;
              return false;
            }
            unsigned char * resized = simage->simage_resize(
                tSnap.pixelsRgba.data(), int(w), int(h), 4, int(newWidth), int(newHeight));
            if (!resized) {
              this->isUnsupported = true;
              this->builderError = "Legacy high-quality POT resize failed";
              if (outError) *outError = this->builderError;
              return false;
            }
            std::vector<uint8_t> pixels(resized, resized + size_t(newWidth) * newHeight * 4);
            simage->simage_free_image(resized);
            tSnap.width = newWidth;
            tSnap.height = newHeight;
            tSnap.pixelsRgba.swap(pixels);
          } else {
            const bool resized = GLUWrapper()->available
                ? CoinRenderTextureSamplingCore::legacyResizeGlu(tSnap, newWidth, newHeight)
                : CoinRenderTextureSamplingCore::legacyResizeNearest(tSnap, newWidth, newHeight);
            if (!resized) {
              this->isUnsupported = true;
              this->builderError = "Legacy fallback POT resize exceeds image limits";
              if (outError) *outError = this->builderError;
              return false;
            }
          }
        }
        if (sourceNode) {
          constexpr size_t cacheLimit = 128u * 1024u * 1024u;
          if (this->legacyPotBytes > cacheLimit ||
              tSnap.pixelsRgba.size() > cacheLimit - this->legacyPotBytes) {
            this->isUnsupported = true;
            this->builderError = "Legacy POT upload cache exceeds 128 MiB";
            if (outError) *outError = this->builderError;
            return false;
          }
          pendingPotNode = sourceNode;
          pendingPotImage = {sourceRevision, tSnap.width, tSnap.height,
                             useCompressedTexture,
                             tSnap.pixelsRgba};
        }
      }
    }
  }
  // 8. Content digest & Image deduplication
  tSnap.contentDigest = isSceneTexture ? sceneTexture->second.producerId
                                     : CoinRenderImageCore::rgba8Digest(tSnap.pixelsRgba);
  if (isSceneTexture) {
    // SoSceneTexture2's FBO sampler is always linear, and becomes trilinear
    // strictly above 0.5 (unlike stored SoTexture2 quality thresholds).
    filter = quality > .5f ? CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR
                           : CoinRenderTextureFilter::LINEAR;
    tSnap.mipmapped = quality > .5f;
    if (!CoinRenderTextureSamplingCore::validMipImage(tSnap)) {
      this->isUnsupported=true; this->builderError="RTT mipmaps exceed the format or memory limits";
      if(outError)*outError=this->builderError; return false;
    }
  } else if (CoinRenderTextureSamplingCore::mipFilter(filter) &&
             !CoinRenderTextureSamplingCore::generate(tSnap)) {
    this->isUnsupported=true;this->builderError="Texture mipmaps require supported 2D images and at most 128 MiB including all levels";
    if(outError)*outError=this->builderError;return false;
  }

  if(useCompressedTexture && !CoinRenderTextureSamplingCore::compress(tSnap)) {
    this->isUnsupported=true;this->builderError="BC3 stored textures require block-aligned base dimensions and RGBA8 input";
    if(outError)*outError=this->builderError;return false;
  }
  const uint32_t texSlot = CoinRenderPlanAssemblyCore::texture(this->currentPlan, std::move(tSnap));
  if (pendingPotNode) {
    this->legacyPotBytes += pendingPotImage.pixels.size();
    this->legacyPotImages.emplace(pendingPotNode, std::move(pendingPotImage));
  }

  // 9. Sampler deduplication
  CoinRenderSamplerSnapshot sampSnap;
  sampSnap.wrapS = snapWrapS;
  sampSnap.wrapT = snapWrapT;
  sampSnap.filter = filter;
  if(quality>.85f) { sampSnap.filter=CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR; sampSnap.maxAnisotropy=this->maxTextureAnisotropy; }

  const uint32_t sampSlot = CoinRenderPlanAssemblyCore::sampler(this->currentPlan, sampSnap);

  // 10. Texture matrix
  rs.textureMatrix = SoMultiTextureMatrixElement::get(state, unit);
  rs.hasTexture = true;
  rs.textureImageSlot = texSlot;
  rs.samplerSlot = sampSlot;

  return true;
}

uint32_t
CoinRenderFramePlanBuilder::captureRenderState(SoCallbackAction * action, int materialIndex, bool captureTextures,
                                               bool boundingBox, bool captureRasterLighting)
{
  const bool stable = captureTextures && this->stableShape && action->getCurPathTail() == this->stableShape;
  if (stable)
    for (const auto & cached : this->shapeRenderStates)
      if (cached.first == materialIndex) return cached.second;
  // 1. Material
  uint32_t materialSlot = this->captureMaterial(action, materialIndex, boundingBox);

  // 2. Lighting & CoinRenderLightModel
  CoinRenderLightModel lm = captureTextures || captureRasterLighting
    ? CoinRenderLightModel::PHONG : CoinRenderLightModel::BASE_COLOR;
  CoinRenderLightingSnapshot lightSnap;
  lightSnap.lights.swap(this->lightCaptureScratch);

  lightSnap.lights.clear();
  if (action && action->getState()) {
    SoState * envState = action->getState();
    const SbColor & ambient = SoEnvironmentElement::getAmbientColor(envState);
    lightSnap.ambientIntensity = SoEnvironmentElement::getAmbientIntensity(envState);
    for (int k = 0; k < 3; ++k) {
      lightSnap.ambientColor[k] = ambient[k];
    }
  }
  SoState * state = action->getState();
  if (state) {
    int32_t model = SoLazyElement::getLightModel(state);
    if (model == SoLazyElement::BASE_COLOR) {
      lm = CoinRenderLightModel::BASE_COLOR;
    }
  }

  if (lm == CoinRenderLightModel::PHONG && state) {
    const SoNodeList & lights = SoLightElement::getLights(state);
    for (int i = 0; i < lights.getLength(); ++i) {
      SoLight * l = static_cast<SoLight *>(lights[i]);
      if (l && l->on.getValue()) {
        CoinRenderLightSourceSnapshot src;
        src.sourceRevision = l->getNodeId();
        const auto vrml = this->vrmlLightSources.find(l);
        if (vrml != this->vrmlLightSources.end()) {
          src.sourceRevision = vrml->second.first;
          src.ambientIntensity = vrml->second.second;
        }
        const SbVec3f & attenuation =
          static_cast<size_t>(i) < this->lightAttenuationByIndex.size()
            ? this->lightAttenuationByIndex[static_cast<size_t>(i)]
            : SoEnvironmentElement::getLightAttenuation(state);
        for (int k = 0; k < 3; ++k) src.attenuation[k] = attenuation[k];
        const SbColor & c = l->color.getValue();
        src.color[0] = c[0]; src.color[1] = c[1]; src.color[2] = c[2];
        src.intensity = l->intensity.getValue();
        SbMatrix lm = SoLightElement::getMatrix(state, i);
        src.sourceModel = lm;
        if (l->isOfType(SoDirectionalLight::getClassTypeId())) {
          src.type = CoinRenderLightType::DIRECTIONAL;
          SoDirectionalLight * dl = static_cast<SoDirectionalLight *>(l);
          dl->direction.getValue().getValue(src.direction[0], src.direction[1], src.direction[2]);
          src.position[0] = src.position[1] = src.position[2] = 0.0f;
        } else if (l->isOfType(SoPointLight::getClassTypeId())) {
          src.type = CoinRenderLightType::POINT;
          SoPointLight * pl = static_cast<SoPointLight *>(l);
          pl->location.getValue().getValue(src.position[0], src.position[1], src.position[2]);
          src.direction[0] = src.direction[1] = src.direction[2] = 0.0f;
        } else if (l->isOfType(SoSpotLight::getClassTypeId())) {
          src.type = CoinRenderLightType::SPOT;
          SoSpotLight * sl = static_cast<SoSpotLight *>(l);
          sl->location.getValue().getValue(src.position[0], src.position[1], src.position[2]);
          sl->direction.getValue().getValue(src.direction[0], src.direction[1], src.direction[2]);
          src.cutOffAngle = sl->cutOffAngle.getValue();
          src.dropOffRate = sl->dropOffRate.getValue();
        } else {
          this->isUnsupported = true;
          this->builderError = "Unsupported SoLight subtype";
          break;
        }
        if (static_cast<size_t>(i) < this->lightModelByIndex.size() &&
            this->lightModelByIndex[static_cast<size_t>(i)] * action->getViewingMatrix() == lm) {
          src.sourceWorldModel = this->lightModelByIndex[static_cast<size_t>(i)];
          std::memcpy(src.authoredDirection, src.direction, sizeof(src.direction));
          std::memcpy(src.authoredPosition, src.position, sizeof(src.position));
          src.hasAuthoredSource = true;
        }
        CoinRenderPlanAssemblyCore::transformLight(src);
        lightSnap.lights.push_back(src);
        if (lightSnap.lights.size() > COIN_RENDER_MAX_LIGHTS) {
          this->isUnsupported = true;
          this->builderError = "More than eight active lights in draw";
          break;
        }
      }
    }
  }
  const uint32_t lightingSlot = CoinRenderPlanAssemblyCore::lighting(this->currentPlan, lightSnap);
  lightSnap.lights.swap(this->lightCaptureScratch);

  // 3. Camera
  const CoinRenderCameraSnapshot camSnap = captureCamera(action);

  const uint32_t cameraSlot = CoinRenderPlanAssemblyCore::camera(this->currentPlan, camSnap);

  // 4. Viewport
  const SbViewportRegion & vp = action->getViewportRegion();
  CoinRenderViewportSnapshot vpSnap;
  const SbVec2s & origin = vp.getViewportOriginPixels();
  const SbVec2s & size = vp.getViewportSizePixels();
  vpSnap.x = origin[0];
  vpSnap.y = origin[1];
  vpSnap.width = size[0];
  vpSnap.height = size[1];

  const uint32_t viewportSlot = CoinRenderPlanAssemblyCore::viewport(this->currentPlan, vpSnap);

  // 5. RenderState
  SoShapeHintsElement::VertexOrdering vo;
  SoShapeHintsElement::ShapeType st;
  SoShapeHintsElement::FaceType ft;
  SoShapeHintsElement::get(state, vo, st, ft);

  CoinRenderCullMode cullMode = CoinRenderCullMode::NONE;
  if (st == SoShapeHintsElement::SOLID && vo != SoShapeHintsElement::UNKNOWN_ORDERING) {
    cullMode = CoinRenderCullMode::BACK;
  }
  CoinRenderFrontFace frontFace = (vo == SoShapeHintsElement::CLOCKWISE) ? CoinRenderFrontFace::CW : CoinRenderFrontFace::CCW;

  // Copy immutable defaults instead of reconstructing all identity matrices
  // for every occurrence. Live Coin state is still captured below each time.
  static const CoinRenderRenderStateSnapshot defaultRenderState;
  CoinRenderRenderStateSnapshot rs = defaultRenderState;
  rs.colorWrite = this->colorWriteEnabled;
  rs.model = action->getModelMatrix();
  rs.view = camSnap.viewMatrix;
  rs.projectionCoin = camSnap.projectionMatrixCoin;
  rs.materialSlot = materialSlot;
  rs.transparentMaterial = SoLazyElement::getInstance(state)->isTransparent() != FALSE;
  rs.shadowGroupSlot = this->activeShadowGroupSlot();
  if (!this->shadowMapCameras.empty()) {
    const auto *view = static_cast<const SoViewVolumeElement *>(
      state->getConstElement(SoViewVolumeElement::getClassStackIndex()));
    const auto camera = view ? this->shadowMapCameras.find(view->getNodeId()) :
      this->shadowMapCameras.end();
    if (camera != this->shadowMapCameras.end()) {
      // SoGLViewingMatrixElement loads V after the camera, then computes
      // its reset matrix from the model M at that camera. Preserve Coin's
      // exact matrix-operation order: (V*M^-1)*M, then M^-1 on the left.
      // Subsequent model transforms are multiplied on the left, so the
      // captured model P*M and this reset view yield P*V. A repeated camera
      // node can have different poses; match the current element's view.
      const auto found = this->shadowMapCameraPoses.find(view->getNodeId());
      const ShadowMapCameraPose *pose = nullptr;
      if (found != this->shadowMapCameraPoses.end())
        for (auto it = found->second.rbegin(); it != found->second.rend(); ++it)
          if (it->view == rs.view) { pose = &*it; break; }
      SbMatrix inverseView, inverseModel;
      if (!pose || !coin_render_shadow_camera_math::inverse(rs.view, inverseView) ||
          !coin_render_shadow_camera_math::inverse(pose->model, inverseModel)) {
        this->hasError = this->isUnsupported = true;
        this->builderError = "shadowMapScene camera pose is unavailable or non-invertible";
      } else {
        SbMatrix reset = rs.view;
        reset.multRight(pose->model);
        reset.multLeft(inverseModel);
        if (!coin_render_shadow_camera_math::inverse(reset, inverseView)) {
          this->hasError = this->isUnsupported = true;
          this->builderError = "shadowMapScene camera reset produces a non-invertible view";
        }
        rs.view = reset;
        rs.shadowMapCameraOverride = true;
      }
    }
  }
  if (rs.shadowGroupSlot) rs.shadowStyle = static_cast<uint32_t>(SoShadowStyleElement::get(state));
  if (rs.shadowGroupSlot && (rs.shadowStyle & 2u))
    rs.textureProjection = CoinRenderTextureProjection::DIRECT_ST;
  rs.lightingSlot = lightingSlot;
  rs.cameraSlot = cameraSlot;
  rs.viewportSlot = viewportSlot;
  rs.solidShape = !boundingBox && st == SoShapeHintsElement::SOLID;
  rs.cullMode = boundingBox ? CoinRenderCullMode::NONE : cullMode;
  rs.frontFace = boundingBox ? CoinRenderFrontFace::CCW : frontFace;
  SbBool depthTest = TRUE;
  SbBool depthWrite = TRUE;
  SoDepthBufferElement::DepthWriteFunction depthFunction = SoDepthBufferElement::LESS;
  SbVec2f depthRange(0.0f, 1.0f);
  SoDepthBufferElement::get(state, depthTest, depthWrite, depthFunction, depthRange);
  rs.depthTest = depthTest != FALSE;
  rs.depthWrite = depthWrite != FALSE;
  if (!coin_render_snapshot_alpha_test(state, rs.alphaTestFunction, rs.alphaTestReference)) {
    this->hasError = true;
    this->builderError = "SoAlphaTest state has invalid function or NaN reference";
  }
  rs.depthFunction = static_cast<CoinRenderDepthFunction>(depthFunction);
  rs.screenDoorTransparency = SoLazyElement::getTransparency(state, 0);
  if (state->isElementEnabled(CoinRenderDepthPolicyElement::getClassStackIndex()))
    rs.explicitDepthMask = CoinRenderDepthPolicyElement::get(state);
  // glDepthRange clamps each finite endpoint independently; reversed and
  // collapsed intervals are legal. Keep nonfinite inputs for atomic rejection.
  for (int i = 0; i < 2; ++i)
    rs.depthRange[i] = std::isfinite(depthRange[i]) ? std::max(0.0f, std::min(1.0f, depthRange[i]))
                                                    : depthRange[i];
  SoPolygonOffsetElement::Style offsetStyles;
  SbBool offsetEnabled;
  SoPolygonOffsetElement::get(state, rs.polygonOffsetFactor,
    rs.polygonOffsetUnits, offsetStyles, offsetEnabled);
  rs.polygonOffsetStyles = static_cast<uint32_t>(offsetStyles);
  rs.polygonOffsetEnabled = offsetEnabled != FALSE;
  rs.lightModel = lm;
  rs.preservePolygonEdgeDirection = boundingBox;
  const auto shapeType = action->getCurPathTail()->getTypeId();
  if ((shapeType == SoLineSet::getClassTypeId() ||
       shapeType == SoIndexedLineSet::getClassTypeId() ||
       shapeType == SoPointSet::getClassTypeId()) &&
      SoNormalElement::getInstance(state)->getNum() == 0)
    rs.lightModel = CoinRenderLightModel::BASE_COLOR;
  rs.transparencyType = SoShapeStyleElement::getTransparencyType(state);
  // Native bounding-box dispatch precedes the primitive-cache triangle sort.
  if (boundingBox && rs.transparencyType == SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD)
    rs.transparencyType = SoGLRenderAction::SORTED_OBJECT_ADD;
  else if (boundingBox && rs.transparencyType == SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND)
    rs.transparencyType = SoGLRenderAction::SORTED_OBJECT_BLEND;
  float ambientIntensity = 0.0f;
  SbColor ambientColor, fogColor;
  SbVec3f lightAttenuation;
  int32_t fogType = SoEnvironmentElement::NONE;
  float fogVisibility = 0.0f;
  float fogStart = 0.0f;
  SoEnvironmentElement::get(state, ambientIntensity, ambientColor,
                            lightAttenuation, fogType, fogColor,
                            fogVisibility, fogStart);
  rs.fogMode = static_cast<CoinRenderFogMode>(fogType);
  for (int c = 0; c < 3; ++c) rs.fogColor[c] = fogColor[c];
  rs.fogStart = fogStart;
  rs.fogEnd = fogVisibility;
  float curLw = action->getLineWidth();
  float curPs = action->getPointSize();
  rs.lineWidth = curLw;
  rs.pointSize = curPs;
  rs.linePattern = state
    ? static_cast<uint32_t>(SoLinePatternElement::get(state)) : 0xffffu;
  rs.linePatternScaleFactor = state
    ? SoLinePatternElement::getScaleFactor(state) : 1;
  if (this->linePatternOverride) {
    rs.linePattern = this->linePatternValue;
    rs.linePatternScaleFactor = this->linePatternFactor;
  }

  CoinRenderPlanAssemblyCore::normalizeState(rs, camSnap);

  if (state) {
    const SoClipPlaneElement * planes = SoClipPlaneElement::getInstance(state);
    if (planes) for (int i = 0; i < planes->getNum(); ++i)
      rs.clipPlanesWorld.push_back(planes->get(i, TRUE));
  }
  if (rs.clipPlanesWorld.size() > COIN_RENDER_MAX_CLIP_PLANES) {
    this->isUnsupported = true;
    this->builderError = "UNSUPPORTED: more than eight active Coin clipping planes";
  }
  if (captureTextures) this->captureTexture(action, rs, &this->builderError);
  if (boundingBox) {
    bool inheritedTextureAlpha = false;
    this->captureStoredTextureAlpha(action, inheritedTextureAlpha);
    rs.transparentTexture = rs.transparentTexture || inheritedTextureAlpha ||
      (SoShapeStyleElement::get(state)->getFlags() & SoShapeStyleElement::TRANSP_TEXTURE) != 0;
  }

  if (state->isElementEnabled(CoinRenderShaderModuleElement::getClassStackIndex())) {
    const auto registration = CoinRenderShaderModuleElement::get(state);
    if (registration) {
      CoinRenderShaderInstance instance;
      instance.module = registration->module;
      if (!coin_render_shader_ir(instance.module, instance.ir, this->builderError) ||
          !coin_render_shader_parameters(*registration, instance.parameters, this->builderError)) {
        this->hasError = this->isUnsupported = true;
      } else {
        // Coordinates exist independently of image enablement. The module
        // observes STRQ after each authored matrix, before projective divide.
        rs.textureMatrix = SoMultiTextureMatrixElement::get(state, 0);
        for (unsigned unit = 1; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit)
          if (instance.ir.coordinateMask & (1u << unit))
            rs.extraTextures[unit - 1].matrix = SoMultiTextureMatrixElement::get(state, unit);
        std::string identity = coin_render_shader_key(instance.module, instance.ir);
        if (!instance.parameters.empty())
          identity.append(reinterpret_cast<const char *>(instance.parameters.data()),
                          instance.parameters.size() * sizeof(float));
        const auto existing = this->portableShaderSlots.find(identity);
        if (existing != this->portableShaderSlots.end()) {
          rs.portableShaderSlot = existing->second;
        } else if (this->currentPlan.portableShaders.size() >= std::numeric_limits<uint32_t>::max()) {
          this->hasError = this->isUnsupported = true;
          this->builderError = "Portable shader occurrence table exceeds its slot domain";
        } else {
          this->currentPlan.portableShaders.push_back(std::move(instance));
          rs.portableShaderSlot = static_cast<uint32_t>(this->currentPlan.portableShaders.size());
          this->portableShaderSlots.emplace(std::move(identity), rs.portableShaderSlot);
        }
      }
    }
  }

  const uint32_t rsSlot = CoinRenderPlanAssemblyCore::state(
    this->currentPlan, this->renderStatesByModel, rs);
  if (stable) this->shapeRenderStates.emplace_back(materialIndex, rsSlot);
  return rsSlot;
}

bool
CoinRenderFramePlanBuilder::captureMarkerContent(SoCallbackAction * action, const SoNode * node,
                                                bool primitiveObservers)
{
  // Registration changes do not notify the shape. Every marker-containing
  // scene, including NONE/empty captures, must read the registry on each apply.
  this->screenContentCaptured = true;
  this->endShape();
  auto fail = [&](const std::string & message, bool unsupported = false) {
    this->hasError = true;
    this->isUnsupported = this->isUnsupported || unsupported;
    this->builderError = message;
    return false;
  };
  if (!this->inFrame || !action || !node)
    return fail("Invalid marker capture scope");
  const bool indexed = node->getTypeId() == SoIndexedMarkerSet::getClassTypeId();
  if (!indexed && node->getTypeId() != SoMarkerSet::getClassTypeId())
    return fail("UNSUPPORTED: custom marker node requires its own capture contract", true);
  try {
  const auto * markerNode = indexed ? nullptr : static_cast<const SoMarkerSet *>(node);
  const auto * indexedNode = indexed ? static_cast<const SoIndexedMarkerSet *>(node) : nullptr;
  if (indexed && indexedNode->coordIndex.getNum() == 0) return true;
  SoNode * property = indexed ? indexedNode->vertexProperty.getValue() : markerNode->vertexProperty.getValue();
  if (property && property->getTypeId() != SoVertexProperty::getClassTypeId())
    return fail("UNSUPPORTED: custom marker vertexProperty requires its own capture contract", true);

  SoState * state = action->getState();
  struct StateScope {
    SoState * state;
    explicit StateScope(SoState * value) : state(value) { state->push(); }
    ~StateScope() { state->pop(); }
  } scope(state);
  bool inheritedTextureAlpha = false;
  if (!this->captureStoredTextureAlpha(action, inheritedTextureAlpha)) return false;
  // MarkerSet disables texture state before shouldGLRender; IndexedMarkerSet
  // classifies transparency first. Neither disable clears the native stored
  // image transparency flag, so both retain the source classification above.
  if (!indexed) {
    SoLazyElement::setLightModel(state, SoLazyElement::BASE_COLOR);
    SoMultiTextureEnabledElement::disableAll(state);
  }
  if (property) property->doAction(action);
  if (isShapeInvisible(action)) return true;
  const uint32_t shapeFlags = SoShapeStyleElement::get(state)->getFlags();
  if (shapeFlags & SoShapeStyleElement::BBOXCMPLX)
    return fail("UNSUPPORTED: marker bounding-box complexity requires bounding-box capture", true);
  const bool traversalAlpha = inheritedTextureAlpha || (shapeFlags &
    (SoShapeStyleElement::TRANSP_MATERIAL | SoShapeStyleElement::TRANSP_TEXTURE)) != 0;
  const int transparencyType = SoShapeStyleElement::getTransparencyType(state);
  const bool needsSortingCenter = traversalAlpha &&
    (transparencyType == SoGLRenderAction::SORTED_OBJECT_ADD ||
     transparencyType == SoGLRenderAction::SORTED_OBJECT_BLEND ||
     transparencyType == SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD ||
     transparencyType == SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
  if (indexed) {
    SoLazyElement::setLightModel(state, SoLazyElement::BASE_COLOR);
    SoMultiTextureEnabledElement::disableAll(state);
  }

  const auto * coordinates = SoCoordinateElement::getInstance(state);
  const int coordinateCount = coordinates->getNum();
  const int start = indexed ? 0 : markerNode->startIndex.getValue();
  const int requested = indexed ? indexedNode->coordIndex.getNum() : markerNode->numPoints.getValue();
  if (!indexed && (start < 0 || start > coordinateCount ||
                   (requested >= 0 && requested > coordinateCount - start)))
    return fail("Marker coordinate start/count is outside the captured array");
  const int count = indexed ? requested : (requested < 0 ? coordinateCount - start : requested);
  if (!count) return true;
  if (count > 1048576)
    return fail("UNSUPPORTED: marker capture exceeds 1048576 coordinate occurrences", true);
  const SoMFInt32 & markerIndices = indexed ? indexedNode->markerIndex : markerNode->markerIndex;
  if (markerIndices.getNum() == 0)
    return fail("Marker index array is empty for a nonempty marker set");
  const auto binding = SoMaterialBindingElement::get(state);
  const bool materialPerVertex = binding != SoMaterialBindingElement::OVERALL;
  const bool materialIndexed = indexed &&
    (binding == SoMaterialBindingElement::PER_PART_INDEXED ||
     binding == SoMaterialBindingElement::PER_FACE_INDEXED ||
     binding == SoMaterialBindingElement::PER_VERTEX_INDEXED);
  const bool explicitMaterials = indexed && indexedNode->materialIndex.getNum() > 0 &&
    indexedNode->materialIndex[0] >= 0;
  const int materialCount = SoLazyElement::getInstance(state)->getNumDiffuse();
  const auto * normals = SoNormalElement::getInstance(state);
  const int normalCount = normals->getNum();
  const auto normalBinding = SoNormalBindingElement::get(state);
  const bool normalPerVertex = normalBinding != SoNormalBindingElement::OVERALL;
  const bool normalIndexed = indexed &&
    (normalBinding == SoNormalBindingElement::PER_PART_INDEXED ||
     normalBinding == SoNormalBindingElement::PER_FACE_INDEXED ||
     normalBinding == SoNormalBindingElement::PER_VERTEX_INDEXED);
  const bool explicitNormals = indexed && indexedNode->normalIndex.getNum() > 0 &&
    indexedNode->normalIndex[0] >= 0;
  auto coordinateIndex = [&](int occurrence) {
    return indexed ? indexedNode->coordIndex[occurrence] : start + occurrence;
  };
  auto markerIndex = [&](int occurrence) {
    // Native debug IndexedMarkerSet repeats the tail. Release's unchecked
    // short-array read is undefined; use the safe policy in every build.
    return markerIndices[std::min(occurrence, markerIndices.getNum() - 1)];
  };
  auto materialIndex = [&](int occurrence, int coordinate, int & material) {
    material = materialPerVertex ? occurrence : 0;
    if (materialIndexed) {
      if (explicitMaterials) {
        if (occurrence >= indexedNode->materialIndex.getNum()) return false;
        material = indexedNode->materialIndex[occurrence];
      } else material = coordinate;
    }
    return material >= 0 && material < materialCount;
  };
  struct Bitmap {
    int width = 0, height = 0;
    size_t stride = 0;
    std::vector<unsigned char> bytes;
  };
  std::unordered_map<int, Bitmap> bitmaps;
  size_t bitmapBytes = 0;
  auto bitmap = [&](int marker, const Bitmap *& output) {
    output = nullptr;
    const auto found = bitmaps.find(marker);
    if (found != bitmaps.end()) { output = &found->second; return true; }
    coin_render_marker_bitmap_view view = {};
    const int status = coin_render_marker_bitmap(marker, &view);
    if (status < 0) return fail("Registered marker has malformed bitmap storage");
    if (!status || !view.width || !view.height) return true;
    const size_t rowBytes = (size_t(view.width) + 7) / 8;
    const size_t inferredAlignment = marker < SoMarkerSet::NUM_MARKERS ? 4 : 1;
    const size_t inferredStride = (rowBytes + inferredAlignment - 1) & ~(inferredAlignment - 1);
    if (indexed && inferredStride != view.row_stride)
      return fail("UNSUPPORTED: IndexedMarkerSet inferred bitmap alignment differs from registered storage", true);
    if (view.byte_count > 16 * 1024 * 1024 - bitmapBytes || bitmaps.size() >= 4096)
      return fail("UNSUPPORTED: marker bitmap capture exceeds 16 MiB or 4096 registered bitmaps", true);
    Bitmap captured;
    captured.width = view.width; captured.height = view.height; captured.stride = view.row_stride;
    // Registry pointers are not retained through primitive observers or later
    // frames. A subsequent add/remove operation can free every borrowed byte.
    captured.bytes.assign(view.bytes, view.bytes + view.byte_count);
    bitmapBytes += view.byte_count;
    output = &bitmaps.emplace(marker, std::move(captured)).first->second;
    return true;
  };

  // Validate every coordinate/material which native capture or inherited point
  // observers will read, including observer-visible NONE occurrences, before
  // invoking any observer. Negative IndexedPointSet separators are skipped by
  // observers, but a defined marker at a negative coordinate is malformed.
  // SoPointSet/SoIndexedShape computeBBox return the arithmetic center of
  // coordinate occurrences, not the midpoint of the min/max bounds. Include
  // NONE and culled occurrences exactly as the native sorted-object path.
  SbVec3f sortingCenter(0,0,0);
  int sortingOccurrences = 0;
  for (int i = 0; i < count; ++i) {
    const int marker = markerIndex(i);
    if (marker < SoMarkerSet::NONE) return fail("Marker identifier is below NONE");
    const Bitmap * image = nullptr;
    if (marker != SoMarkerSet::NONE && !bitmap(marker, image)) return false;
    const int coordinate = coordinateIndex(i);
    const bool nativeRead = !indexed || image != nullptr;
    const bool callbackOrBBoxRead = (primitiveObservers || needsSortingCenter) && coordinate >= 0;
    if ((nativeRead || callbackOrBBoxRead) &&
        (coordinate < 0 || coordinate >= coordinateCount))
      return fail("Marker coordinate index is outside the captured array");
    if (!nativeRead && !callbackOrBBoxRead) continue;
    const SbVec3f point = coordinates->get3(coordinate);
    // MarkerSet fetches the coordinate before NONE, but does not project or
    // cull that unused position. Observers and sorted bbox capture do use it.
    if (image || callbackOrBBoxRead)
      for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(point[axis])) return fail("Marker coordinate is not finite");
    if (needsSortingCenter) { sortingCenter += point; ++sortingOccurrences; }
    int material;
    if ((!indexed || image || primitiveObservers) && !materialIndex(i, coordinate, material))
      return fail("Marker material index is outside the captured array");
    if (primitiveObservers && normalCount > 0 && normalPerVertex) {
      int normal = i;
      if (normalIndexed) {
        if (explicitNormals) {
          if (i >= indexedNode->normalIndex.getNum())
            return fail("Marker observer normal index array is too short");
          normal = indexedNode->normalIndex[i];
        } else normal = coordinate;
      }
      if (normal < 0 || normal >= normalCount)
        return fail("Marker observer normal index is outside the captured array");
    }
  }

  if (sortingOccurrences) {
    sortingCenter /= float(sortingOccurrences);
    for (int axis = 0; axis < 3; ++axis)
      if (!std::isfinite(sortingCenter[axis])) return fail("Marker sorting center is not finite");
  }
  const uint32_t sourceSlot = this->captureRenderState(action, 0, false, false, true);
  const auto source = this->currentPlan.renderStates[sourceSlot];
  if (this->hasError || this->isUnsupported) return false;
  const auto viewport = this->currentPlan.viewports[source.viewportSlot];
  const SbMatrix mvp = source.model * source.view * source.projectionCoin;
  if (!CoinRenderTransformCore::finiteMatrix(mvp)) return fail("Marker projection matrix is not finite");
  const auto & volume = action->getViewVolume();
  const float volumeDimensions[] = {volume.getWidth(), volume.getHeight(), volume.getDepth()};
  for (float dimension : volumeDimensions) {
    if (!std::isfinite(dimension)) return fail("Marker view-volume dimensions are not finite");
    if (dimension == 0.0f)
      return fail("UNSUPPORTED: marker raster with a degenerate camera view volume", true);
  }
  SbPlane viewPlanes[6];
  volume.getViewVolumePlanes(viewPlanes);
  for (const auto & plane : viewPlanes) {
    const auto & normal = plane.getNormal();
    const float lengthSquared = normal.sqrLength();
    if (!std::isfinite(normal[0]) || !std::isfinite(normal[1]) ||
        !std::isfinite(normal[2]) || !std::isfinite(plane.getDistanceFromOrigin()) ||
        !std::isfinite(lengthSquared) || lengthSquared <= 0.0f)
      return fail("UNSUPPORTED: marker raster with degenerate camera clipping planes", true);
  }
  auto rasterState = source;
  rasterState.rasterPixels = true;
  rasterState.rasterTransparent = traversalAlpha;
  rasterState.model = rasterState.view = rasterState.projectionCoin = SbMatrix::identity();
  rasterState.lightModel = CoinRenderLightModel::BASE_COLOR;
  rasterState.cullMode = CoinRenderCullMode::NONE;
  rasterState.frontFace = CoinRenderFrontFace::CCW;
  rasterState.polygonOffsetEnabled = false;
  rasterState.polygonLinePattern = false;
  rasterState.clipPlanesWorld.clear(); // Native bitmap raster temporarily disables user planes.
  rasterState.hasTexture = false;
  rasterState.transparentTexture = false;
  if (rasterState.transparencyType == SoGLRenderAction::SCREEN_DOOR)
    rasterState.transparencyType = SoGLRenderAction::NONE; // glBitmap ignores polygon stipple.
  if (!rasterState.depthTest) { rasterState.depthWrite = false; rasterState.explicitDepthMask |= 2; }
  const float uv[] = {0,0,0,0};
  std::string diagnostic;
  size_t coveredRuns = 0;
  struct RasterSampleScope {
    uint32_t & mask; bool & fixed;
    uint32_t previousMask; bool previousFixed;
    RasterSampleScope(uint32_t & m, bool & f, bool enabled)
      : mask(m), fixed(f), previousMask(m), previousFixed(f) { mask = 0; fixed = enabled; }
    ~RasterSampleScope() { mask = previousMask; fixed = previousFixed; }
  } sampleScope(this->currentMultipassMask, this->currentMultipassRasterFixed,
                this->currentPlan.numPasses > 1);
  std::unordered_map<int, uint32_t> rasterMaterials;
  for (int i = 0; i < count; ++i) {
    const int marker = markerIndex(i);
    const auto found = bitmaps.find(marker);
    if (found == bitmaps.end()) continue;
    const Bitmap & image = found->second;
    const SbVec3f point = coordinates->get3(coordinateIndex(i));
    SbVec3f world;
    source.model.multVecMatrix(point, world);
    for (int axis = 0; axis < 3; ++axis)
      if (!std::isfinite(world[axis])) return fail("Marker world position is not finite");
    if (source.shadowGroupSlot && (source.shadowStyle & 1u) &&
        !source.transparentMaterial && !inheritedTextureAlpha) {
      CoinRenderRasterOccurrence occurrence;
      occurrence.kind = CoinRenderRasterOccurrence::MARKER;
      occurrence.sourceStateSlot = sourceSlot;
      occurrence.anchorWorld = world;
      occurrence.originalWidth = image.width;
      occurrence.originalHeight = image.height;
      for (int runY = 0; runY < image.height; ++runY) {
        for (int runX = 0; runX < image.width;) {
          const auto covered = [&](int x) {
            return (image.bytes[size_t(runY) * image.stride + size_t(x / 8)] &
                    (0x80u >> (x & 7))) != 0;
          };
          if (!covered(runX)) { ++runX; continue; }
          const int begin = runX;
          while (runX < image.width && covered(runX)) ++runX;
          occurrence.runs.push_back({float(begin), float(runY), float(runX - begin)});
        }
      }
      this->currentPlan.rasterOccurrences.push_back(std::move(occurrence));
    }
    bool culled = false;
    for (const auto & plane : viewPlanes) if (!plane.isInHalfSpace(world)) culled = true;
    for (const auto & plane : source.clipPlanesWorld) if (!plane.isInHalfSpace(world)) culled = true;
    if (culled) continue;
    SbVec3f projected;
    mvp.multVecMatrix(point, projected);
    std::map<std::pair<float,float>,uint32_t> origins;
    for (int pass = 0; pass < this->currentPlan.numPasses; ++pass) {
      SbMatrix shifted;
      if (!CoinRenderMultipassCore::projection(source.projectionCoin, this->currentPlan.numPasses,
            pass, viewport, shifted)) return fail("Invalid marker multipass projection");
      const SbMatrix sampleMvp = source.model * source.view * shifted;
      SbVec3f samplePoint;
      sampleMvp.multVecMatrix(point, samplePoint);
      const float rasterX = (samplePoint[0] + 1.0f) * 0.5f * viewport.width - (image.width - 1) / 2;
      const float rasterY = (samplePoint[1] + 1.0f) * 0.5f * viewport.height - (image.height - 1) / 2;
      bool visible;
      if (!CoinRenderScreenRasterCore::rasterVisible(rasterState, viewport, rasterX, rasterY,
            samplePoint[2], visible, diagnostic, false)) return fail(diagnostic);
      if (!visible) continue;
      float xOrigin, yOrigin;
      if (!CoinRenderScreenRasterCore::markerBitmapOrigin(viewport, rasterX, rasterY,
            xOrigin, yOrigin, diagnostic)) return fail(diagnostic);
      origins[{xOrigin,yOrigin}] |= 1u << pass;
    }
    if (origins.empty()) continue;
    int material;
    if (!materialIndex(i, coordinateIndex(i), material)) return fail("Marker material index changed during capture");
    auto cachedMaterial = rasterMaterials.find(material);
    uint32_t materialSlot;
    if (cachedMaterial != rasterMaterials.end()) materialSlot = cachedMaterial->second;
    else {
      if (rasterMaterials.size() >= 65536)
        return fail("UNSUPPORTED: marker raster exceeds 65536 primary material variants per node", true);
      auto captured = this->currentPlan.materials[this->captureMaterial(action, material)];
      if (!std::isfinite(captured.transparency) || captured.transparency < 0 || captured.transparency > 1)
        return fail("Marker primary transparency is outside finite [0,1]");
      for (int channel = 0; channel < 4; ++channel) {
        if (!std::isfinite(captured.diffuse[channel]) || captured.diffuse[channel] < 0 || captured.diffuse[channel] > 1)
          return fail("Marker primary color is outside finite [0,1]");
        captured.diffuse[channel] = std::floor(captured.diffuse[channel] * 255.0f + 0.5f) / 255.0f;
      }
      if (source.transparencyType == SoGLRenderAction::SCREEN_DOOR) captured.diffuse[3] = 1.0f;
      captured.transparency = 1.0f - captured.diffuse[3];
      materialSlot = this->internMaterial(captured);
      rasterMaterials.emplace(material, materialSlot);
    }
    rasterState.materialSlot = materialSlot;
    if (source.shadowGroupSlot) {
      rasterState.rasterShadowAnchorValid = true;
      rasterState.rasterShadowAnchorWorld = world;
    }
    const uint32_t stateSlot = CoinRenderPlanAssemblyCore::state(this->currentPlan, this->renderStatesByModel, rasterState);
    bool countOriginalRuns = true;
    for (const auto & origin : origins) {
      const float xOrigin = origin.first.first, yOrigin = origin.first.second;
      this->currentMultipassMask = this->currentPlan.numPasses > 1 ? origin.second : 0;
      for (int y = 0; y < image.height; ++y) {
        auto covered = [&](int x) { return (image.bytes[size_t(y) * image.stride + size_t(x / 8)] & (0x80u >> (x & 7))) != 0; };
        for (int x = 0; x < image.width;) {
          if (!covered(x)) { ++x; continue; }
          const int begin = x;
          while (x < image.width && covered(x)) ++x;
          if (countOriginalRuns && ++coveredRuns > 65536)
            return fail("UNSUPPORTED: marker raster exceeds 65536 covered runs per node", true);
          if (this->currentPlan.vertices.size() > UINT32_MAX - 4 || this->currentPlan.indices.size() > UINT32_MAX - 6)
            return fail("UNSUPPORTED: marker raster geometry exceeds index capacity", true);
          CoinRenderScreenRasterQuad quad;
          if (!CoinRenderScreenRasterCore::pixelQuad(viewport, xOrigin + begin, yOrigin + y,
                float(x - begin), 1.0f, projected[2], uv, materialSlot, quad, diagnostic)) return fail(diagnostic);
          if (!quad.visible) continue;
          this->ensureDrawPacket(CoinRenderPrimitiveTopology::TRIANGLE_LIST, stateSlot, const_cast<SoNode *>(node));
          auto & draw = this->currentPlan.draws[this->currentDrawIndex];
          if (draw.geometry.indexCount == 0 && needsSortingCenter && sortingOccurrences)
            CoinRenderPlanAssemblyCore::sortingCenter(draw, source.model, sortingCenter);
          if (this->sortingStrategy != 0) this->captureSortingCenter(action);
          const uint32_t first = static_cast<uint32_t>(this->currentPlan.vertices.size());
          for (auto vertex : quad.vertices) {
            vertex.fogEyeDepth = CoinRenderScreenRasterCore::planarRasterFogDepth(projected[2]);
            this->currentPlan.vertices.push_back(vertex);
          }
          for (uint32_t index : {0u,1u,2u,0u,2u,3u}) this->currentPlan.indices.push_back(first + index);
          draw.geometry.vertexCount += 4; draw.geometry.indexCount += 6;
        }
      }
      countOriginalRuns = false;
    }
  }
  return true;
  } catch (const std::bad_alloc &) {
    return fail("UNSUPPORTED: marker raster capture could not allocate bounded storage", true);
  }
}

bool
CoinRenderFramePlanBuilder::captureScreenContent(SoCallbackAction * action, const SoNode * node)
{
  struct RasterSampleScope {
    uint32_t & mask; bool & fixed;
    uint32_t previousMask; bool previousFixed;
    RasterSampleScope(uint32_t & m, bool & f, bool enabled)
      : mask(m), fixed(f), previousMask(m), previousFixed(f) { mask = 0; fixed = enabled; }
    ~RasterSampleScope() { mask = previousMask; fixed = previousFixed; }
  } sampleScope(this->currentMultipassMask, this->currentMultipassRasterFixed,
                this->currentPlan.numPasses > 1);
  this->screenContentCaptured = true;
  this->endShape();
  auto fail = [&](const std::string & diagnostic, bool unsupported = false) {
    this->hasError = true;
    this->isUnsupported = this->isUnsupported || unsupported;
    this->builderError = diagnostic;
    return false;
  };
  if (!this->inFrame || !action || !node)
    return fail("Invalid screen-content capture scope");
  const bool isImage = node->isOfType(SoImage::getClassTypeId());
  if (!isImage && !node->isOfType(SoText2::getClassTypeId()))
    return fail("Invalid native screen-content node");
  if (isImage) {
    SbVec2s size; int components;
    const auto * bytes = static_cast<const SoImage *>(node)->image.getValue(size, components);
    if (!bytes || !size[0] || !size[1]) return true;
  } else {
    const auto & strings = static_cast<const SoText2 *>(node)->string;
    if (strings.getNum() <= CoinRenderText2Capture::MAX_LINES) {
      bool empty = true;
      for (int i = 0; i < strings.getNum(); ++i) empty = empty && strings[i].getLength() == 0;
      if (empty) return true;
    }
  }

  if (isImage) {
    int lastUnit = -1;
    SoMultiTextureEnabledElement::getEnabledUnits(action->getState(), lastUnit);
    if (lastUnit >= int(COIN_RENDER_MAX_TEXTURE_UNITS))
      return fail("UNSUPPORTED: At most eight texture units are supported", true);
  }
  // Image pixels are PRIMARY, independently of material color. Keep the
  // inherited units in a separate source state so payload + eight units fit.
  // Text2 explicitly disables texturing and leaves current attributes intact.
  const uint32_t sourceSlot = this->captureRenderState(action, 0, isImage, false, true);
  const auto source = this->currentPlan.renderStates[sourceSlot];
  if (this->hasError || this->isUnsupported) return false;
  const uint32_t shapeFlags = SoShapeStyleElement::get(action->getState())->getFlags();
  const bool traversalAlpha = (shapeFlags &
    (SoShapeStyleElement::TRANSP_MATERIAL | SoShapeStyleElement::TRANSP_TEXTURE)) != 0;
  const auto viewport = this->currentPlan.viewports[source.viewportSlot];
  std::string diagnostic;
  CoinRenderScreenImageLayout imageLayout;
  CoinRenderText2Raster text;
  CoinRenderTextureImageSnapshot image;
  bool imageTransparent = false;
  auto rasterCoordinates = source.rasterTextureCoordinates;
  auto rasterProvenance = source.rasterTextureProvenance;
  RasterNormalConsumer normalConsumer;
  bool consumesNormal = false;
  if (isImage) {
    const auto & imageNode = *static_cast<const SoImage *>(node);
    SbVec2s size; int components = 0;
    const unsigned char * bytes = imageNode.image.getValue(size, components);
    if (!size[0] || !size[1]) return true;
    int lastUnit = -1;
    const SbBool * enabled = SoMultiTextureEnabledElement::getEnabledUnits(action->getState(), lastUnit);
    const auto * coordinates = SoMultiTextureCoordinateElement::getInstance(action->getState());
    for (int unit = 0; unit <= lastUnit; ++unit) {
      if (!enabled[unit]) continue;
      if (unit >= int(COIN_RENDER_MAX_TEXTURE_UNITS))
        return fail("UNSUPPORTED: At most eight texture units are supported", true);
      // Quality zero disables the effective GL unit. Neither persistent UV
      // history nor texgen participates in DrawPixels while it is inactive.
      if (!coin_render_texture_unit_enabled(source, size_t(unit))) continue;
      const auto & data = coordinates->getUnitData(unit);
      const auto recorded = this->rasterGenerators.find(data.nodeid);
      if (recorded != this->rasterGenerators.end() &&
          recorded->second.callback == reinterpret_cast<const void *>(data.funcCB)) {
        if (recorded->second.kind == RasterGeneratorKind::UNSUPPORTED)
          return fail("UNSUPPORTED: raster texgen subclass has no audited native contract", true);
        // Native object-linear generators define all four components. Defer
        // evaluation until layout establishes SoImage's pixel-space RasterPos.
        rasterProvenance[size_t(unit)] = 1;
        continue;
      }
      if (this->rasterCoordinateHistory[size_t(unit)] != RasterCoordinateHistory::INITIAL)
        return fail("UNSUPPORTED: SoImage inherited raster texture coordinate is indeterminate after geometry", true);
      if (coordinates->getType(unit) == SoMultiTextureCoordinateElement::TEXGEN)
        return fail("UNSUPPORTED: SoImage raster texgen requires a recorded generator", true);
    }
    CoinRenderScreenRasterAnchor anchor;
    if (!CoinRenderScreenRasterCore::imageAnchor(source.model, action->getViewVolume(),
                                                viewport, anchor, diagnostic))
      return fail(diagnostic, diagnostic.find("UNSUPPORTED:") == 0);
    if (source.shadowGroupSlot && (source.shadowStyle & 1u) &&
        !source.transparentMaterial && !source.transparentTexture &&
        !coin_render_image_has_transparency(bytes,
          size_t(size[0]) * size_t(size[1]), components)) {
      CoinRenderRasterOccurrence occurrence;
      occurrence.kind = CoinRenderRasterOccurrence::IMAGE;
      occurrence.sourceStateSlot = sourceSlot;
      source.model.multVecMatrix(SbVec3f(0, 0, 0), occurrence.anchorWorld);
      occurrence.originalWidth = size[0];
      occurrence.originalHeight = size[1];
      occurrence.requestedWidth = imageNode.width.getValue();
      occurrence.requestedHeight = imageNode.height.getValue();
      occurrence.horizontalAlignment = imageNode.horAlignment.getValue();
      occurrence.verticalAlignment = imageNode.vertAlignment.getValue();
      this->currentPlan.rasterOccurrences.push_back(std::move(occurrence));
    }
    if (!CoinRenderScreenRasterCore::imageLayout(anchor, viewport, size[0], size[1],
          imageNode.width.getValue(), imageNode.height.getValue(),
          imageNode.horAlignment.getValue(), imageNode.vertAlignment.getValue(),
          source.materialSlot, imageLayout, diagnostic))
      return fail(diagnostic, diagnostic.find("UNSUPPORTED:") == 0);
    if (!imageLayout.quad.visible) return true;
    // SoImage replaces MODELVIEW with identity and installs pixel glOrtho
    // before glRasterPos(rpx,rpy,-nilpoint.z). OBJECT_LINEAR therefore sees
    // the aligned, cropped viewport-local pixel anchor, not object origin.
    const SbVec3f rasterPosition(imageLayout.rasterX, imageLayout.rasterY, -anchor.depthCoin);
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      if (rasterProvenance[unit] != 1) continue;
      const auto & generator = this->rasterGenerators.at(coordinates->getUnitData(int(unit)).nodeid);
      if (generator.kind == RasterGeneratorKind::OBJECT) {
        rasterCoordinates[unit] = coordinates->get(int(unit), rasterPosition, this->rasterNormal);
        continue;
      }
      // A deferred shape may execute after this Image despite preceding it
      // in the graph. Resolve normal-dependent texgen in composition order.
      consumesNormal = true;
      normalConsumer.position = rasterPosition;
      normalConsumer.generators[unit] = generator.kind;
      normalConsumer.qPlanes[unit] = this->rasterQPlanes[unit];
    }
    if (!CoinRenderScreenRasterCore::imagePayload(bytes,
          size_t(size[0]) * size_t(size[1]) * size_t(components),
          size[0], size[1], components, image, imageTransparent, diagnostic))
      return fail(diagnostic, diagnostic.find("UNSUPPORTED:") == 0);
  } else {
    CoinRenderText2Capture::Status status;
    bool haveFullText = false;
    if (source.shadowGroupSlot && (source.shadowStyle & 1u) &&
        !source.transparentMaterial) {
      CoinRenderText2Raster shadowText;
      if (!coin_render_capture_text2_for_shadow(*static_cast<const SoText2 *>(node),
              action->getState(), shadowText, status, diagnostic))
        return fail(diagnostic, status == CoinRenderText2Capture::Status::UNSUPPORTED);
      SbVec3f worldAnchor;
      source.model.multVecMatrix(SbVec3f(0, 0, 0), worldAnchor);
      for (const auto & pass : shadowText.passes) {
        CoinRenderRasterOccurrence occurrence;
        occurrence.kind = pass.mono ? CoinRenderRasterOccurrence::TEXT_MONO :
                                      CoinRenderRasterOccurrence::TEXT_GRAY;
        occurrence.sourceStateSlot = sourceSlot;
        occurrence.anchorWorld = worldAnchor;
        occurrence.originalWidth = static_cast<int32_t>(pass.image.width);
        occurrence.originalHeight = static_cast<int32_t>(pass.image.height);
        // Coin's mono bitmap keeps the floating x raster position and truncates
        // y. The final gray DrawPixels rounds both anchor coordinates first.
        // Store only glyph/layout offsets so each map can apply its own camera's
        // raster-position rounding instead of inheriting the observer's phase.
        occurrence.offsetX = pass.pixelX - (pass.mono ? shadowText.anchor.pixelX :
          float(std::floor(double(shadowText.anchor.pixelX) + 0.5)));
        occurrence.offsetY = pass.pixelY - (pass.mono ?
          float(int(shadowText.anchor.pixelY)) :
          float(std::floor(double(shadowText.anchor.pixelY) + 0.5)));
        for (uint32_t y = 0; y < pass.image.height; ++y) {
          for (uint32_t x = 0; x < pass.image.width;) {
            if (!pass.coverageMask[size_t(y) * pass.image.width + x]) { ++x; continue; }
            const uint32_t begin = x;
            while (x < pass.image.width &&
                   pass.coverageMask[size_t(y) * pass.image.width + x]) ++x;
            occurrence.runs.emplace_back(float(begin), float(y), float(x - begin));
          }
        }
        if (!occurrence.runs.empty())
          this->currentPlan.rasterOccurrences.push_back(std::move(occurrence));
      }
      if (this->currentPlan.numPasses > 1) {
        text = std::move(shadowText);
        haveFullText = true;
      }
    }
    if (this->currentPlan.numPasses > 1 && !haveFullText) {
      if (!coin_render_capture_text2_for_shadow(*static_cast<const SoText2 *>(node),
            action->getState(), text, status, diagnostic))
        return fail(diagnostic, status == CoinRenderText2Capture::Status::UNSUPPORTED);
      haveFullText = true;
    }
    if (!haveFullText && !CoinRenderText2Capture::capture(*static_cast<const SoText2 *>(node), action->getState(),
                                       text, status, diagnostic))
      return fail(diagnostic, status == CoinRenderText2Capture::Status::UNSUPPORTED);
    if (text.empty) return true;
  }

  auto rasterState = source;
  rasterState.rasterPixels = true;
  rasterState.rasterTextureCoordinates = rasterCoordinates;
  rasterState.rasterTextureProvenance = rasterProvenance;
  rasterState.rasterTransparent = traversalAlpha;
  if (source.shadowGroupSlot) {
    rasterState.rasterShadowAnchorValid = true;
    source.model.multVecMatrix(SbVec3f(0, 0, 0), rasterState.rasterShadowAnchorWorld);
    rasterState.rasterShadowImagePhong = isImage &&
      source.lightModel == CoinRenderLightModel::PHONG;
  }
  rasterState.model = rasterState.view = rasterState.projectionCoin = SbMatrix::identity();
  rasterState.lightModel = CoinRenderLightModel::BASE_COLOR;
  rasterState.cullMode = CoinRenderCullMode::NONE;
  rasterState.frontFace = CoinRenderFrontFace::CCW;
  rasterState.polygonOffsetEnabled = false;
  rasterState.polygonLinePattern = false;
  rasterState.clipPlanesWorld.clear(); // glRasterPos tests the anchor once below.
  rasterState.textureMatrix = SbMatrix::identity();
  rasterState.extraTextures = decltype(rasterState.extraTextures)();
  rasterState.textureCombines = decltype(rasterState.textureCombines)();
  rasterState.hasTexture = true;
  if (isImage) {
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit)
      if (coin_render_texture_unit_enabled(source, unit)) {
        rasterState.rasterTextureSourceSlot = sourceSlot;
        break;
      }
  }
  if (rasterState.transparencyType == SoGLRenderAction::SCREEN_DOOR)
    rasterState.transparencyType = SoGLRenderAction::NONE; // Polygon stipple never masks raster pixels.
  // Disabling GL_DEPTH_TEST disables depth writes as well as comparisons.
  if (!rasterState.depthTest) { rasterState.depthWrite = false; rasterState.explicitDepthMask |= 2; }
  CoinRenderSamplerSnapshot sampler;
  sampler.wrapS = sampler.wrapT = CoinRenderTextureWrap::CLAMP;
  sampler.filter = CoinRenderTextureFilter::NEAREST;
  rasterState.samplerSlot = CoinRenderPlanAssemblyCore::sampler(this->currentPlan, sampler);
  CoinRenderMaterialSnapshot white;
  for (int c = 0; c < 4; ++c) white.diffuse[c] = 1.0f;
  const uint32_t whiteSlot = this->internMaterial(white);

  size_t rasterQuads = 0;
  bool countOriginalQuad = true;
  auto emit = [&](CoinRenderScreenRasterQuad quad, uint32_t stateSlot) {
    if (!quad.visible) return true;
    if (countOriginalQuad && ++rasterQuads > 65536)
      return fail("UNSUPPORTED: screen raster exceeds 65536 covered runs per node", true);
    if (this->currentPlan.vertices.size() > UINT32_MAX - 4 ||
        this->currentPlan.indices.size() > UINT32_MAX - 6)
      return fail("UNSUPPORTED: screen raster geometry exceeds index capacity", true);
    this->ensureDrawPacket(CoinRenderPrimitiveTopology::TRIANGLE_LIST, stateSlot,
                           const_cast<SoNode *>(node));
    auto & draw = this->currentPlan.draws[this->currentDrawIndex];
    // Sorting uses the original camera, while the raster's vertex transform is I.
    if (draw.geometry.indexCount == 0)
      CoinRenderPlanAssemblyCore::sortingCenter(draw, source.model, SbVec3f(0,0,0));
    if (this->sortingStrategy != 0) this->captureSortingCenter(action);
    const uint32_t first = static_cast<uint32_t>(this->currentPlan.vertices.size());
    for (auto vertex : quad.vertices) {
      vertex.materialSlot = this->currentPlan.renderStates[stateSlot].materialSlot;
      vertex.fogEyeDepth = CoinRenderScreenRasterCore::planarRasterFogDepth(vertex.position[2]);
      this->currentPlan.vertices.push_back(vertex);
    }
    const uint32_t indices[] = {0,1,2,0,2,3};
    for (uint32_t index : indices) this->currentPlan.indices.push_back(first + index);
    draw.geometry.vertexCount += 4;
    draw.geometry.indexCount += 6;
    return true;
  };
  auto visible = [&](float x, float y, float z, bool & admitted) {
    if (!CoinRenderScreenRasterCore::rasterVisible(source, viewport, x, y, z, admitted, diagnostic))
      return fail(diagnostic);
    return true;
  };

  if (isImage) {
    bool admitted;
    if (!visible(imageLayout.rasterX, imageLayout.rasterY,
                 imageLayout.quad.vertices[0].position[2], admitted)) return false;
    if (!admitted) return true;
    rasterState.materialSlot = whiteSlot;
    rasterState.textureModel = CoinRenderTextureModel::REPLACE;
    rasterState.transparentMaterial = false;
    rasterState.transparentTexture = imageTransparent || source.transparentTexture;
    rasterState.rasterTransparent = rasterState.rasterTransparent || imageTransparent;
    rasterState.textureImageSlot = CoinRenderPlanAssemblyCore::texture(this->currentPlan, std::move(image));
    uint32_t slot;
    if (consumesNormal) {
      // Identical captured images can consume different persistent normals.
      // Keep their resolved coordinates private to each occurrence.
      slot = static_cast<uint32_t>(this->currentPlan.renderStates.size());
      this->currentPlan.renderStates.push_back(rasterState);
    } else slot = CoinRenderPlanAssemblyCore::state(this->currentPlan, this->renderStatesByModel, rasterState);
    if (!emit(imageLayout.quad, slot)) return false;
    if (consumesNormal)
      this->rasterNormalConsumers[this->currentPlan.draws[this->currentDrawIndex].frameNodeOrdinal] = normalConsumer;
    return true;
  }

  for (auto & pass : text.passes) {
    std::map<std::pair<float,float>,uint32_t> origins;
    for (int sample = 0; sample < this->currentPlan.numPasses; ++sample) {
      float pixelX = pass.pixelX, pixelY = pass.pixelY;
      if (this->currentPlan.numPasses > 1) {
        SbMatrix shifted;
        if (!CoinRenderMultipassCore::projection(source.projectionCoin, this->currentPlan.numPasses,
              sample, viewport, shifted)) return fail("Invalid text multipass projection");
        CoinRenderScreenRasterAnchor anchor;
        const SbMatrix mvp = source.model * source.view * shifted;
        if (!CoinRenderScreenRasterCore::matrixAnchor(mvp, viewport, anchor, diagnostic))
          return fail(diagnostic);
        if (double(anchor.pixelY) < std::numeric_limits<int>::min() ||
            double(anchor.pixelY) > std::numeric_limits<int>::max())
          return fail("Text sample anchor exceeds raster integer limits");
        float screenOffsetX = anchor.pixelX + float(text.bboxMin[0]);
        if (text.justification == SoText2::RIGHT) screenOffsetX -= float(text.maxWidth);
        else if (text.justification == SoText2::CENTER) screenOffsetX -= float(text.maxWidth) / 2.0f;
        pixelX = pass.mono ? float(pass.glyphX) + screenOffsetX :
          float(std::floor(double(screenOffsetX) + 0.5));
        pixelY = pass.mono ? float(pass.glyphY) + float(int(anchor.pixelY)) :
          float(std::floor(double(anchor.pixelY) + 0.5) - pass.image.height + text.bboxMax[1]);
      }
      bool admitted;
      if (!visible(pixelX, pixelY, pass.depthCoin, admitted)) return false;
      if (!admitted) continue;
      const float rasterX = pass.mono ? std::floor(pixelX) : pixelX;
      const float rasterY = pass.mono ? std::floor(pixelY) : pixelY;
      origins[{rasterX,rasterY}] |= 1u << sample;
    }
    if (origins.empty()) continue;
    auto rs = rasterState;
    // Gray DrawPixels installs its own GL_GREATER test after every mono glyph;
    // Bitmap and SoImage retain the inherited test from the scene state.
    if (pass.alphaCutoff >= 0.0f) {
      rs.alphaTestFunction = CoinRenderAlphaTestFunction::GREATER;
      rs.alphaTestReference = pass.alphaCutoff;
    }
    rs.materialSlot = pass.materialColorBaked ? whiteSlot : source.materialSlot;
    if (pass.mono && source.transparencyType == SoGLRenderAction::SCREEN_DOOR) {
      auto material = this->currentPlan.materials[source.materialSlot];
      material.diffuse[3] = 1.0f;
      material.transparency = 0.0f; // Coin's packed raster color ORs alpha with 0xff.
      rs.materialSlot = this->internMaterial(material);
    }
    rs.textureModel = pass.materialColorBaked ? CoinRenderTextureModel::REPLACE : CoinRenderTextureModel::MODULATE;
    if (pass.forceBlend) {
      rs.rasterForceBlend = true;
    }
    rs.transparentTexture = !pass.mono;
    rs.textureImageSlot = CoinRenderPlanAssemblyCore::texture(this->currentPlan, std::move(pass.image));
    const auto & texture = this->currentPlan.textures[rs.textureImageSlot];
    const uint32_t width = texture.width, height = texture.height;
    const uint32_t slot = CoinRenderPlanAssemblyCore::state(this->currentPlan, this->renderStatesByModel, rs);
    countOriginalQuad = true;
    for (const auto & origin : origins) {
      const float rasterX = origin.first.first, rasterY = origin.first.second;
      this->currentMultipassMask = this->currentPlan.numPasses > 1 ? origin.second : 0;
      // Horizontal covered runs preserve glBitmap/alpha-test holes, including
      // depth, with the existing texture/geometry contract in both executors.
      for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width;) {
          if (!pass.coverageMask[size_t(y) * width + x]) { ++x; continue; }
          const uint32_t begin = x;
          while (x < width && pass.coverageMask[size_t(y) * width + x]) ++x;
          const float uv[] = {float(begin)/width, float(y)/height, float(x)/width, float(y+1)/height};
          CoinRenderScreenRasterQuad quad;
          if (!CoinRenderScreenRasterCore::pixelQuad(viewport, rasterX + begin, rasterY + y,
                 float(x - begin), 1.0f, pass.depthCoin, uv, rs.materialSlot, quad, diagnostic))
            return fail(diagnostic);
          if (!emit(quad, slot)) return false;
        }
      }
      countOriginalQuad = false;
    }
  }
  return true;
}

CoinRenderVertexSnapshot
CoinRenderFramePlanBuilder::captureVertex(SoCallbackAction * action, const SoPrimitiveVertex * pv,
  uint32_t materialSlot, const CoinRenderRenderStateSnapshot & rs)
{
  this->invalidateRasterCoordinates(action);
  CoinRenderVertexSnapshot v;
  const SbVec3f & pt = pv->getPoint();
  const SbVec3f & n = pv->getNormal();
  SbVec4f tc = pv->getTextureCoords();
  SoState * state = action->getState();
  const auto * coords = SoMultiTextureCoordinateElement::getInstance(state);
  for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
    const auto layer = coin_render_texture_unit(rs, unit);
    if (!layer.enabled || layer.imageSlot >= this->currentPlan.textures.size() ||
        !this->currentPlan.textures[layer.imageSlot].cubeFaceSize) continue;
    const auto & data = coords->getUnitData(int(unit));
    const auto generator = this->rasterGenerators.find(data.nodeid);
    // Coin's NormalMap callback supplies only a dummy zero STR. Its GL
    // texgen has no CPU geometry contract, even if a matrix moves that zero.
    if (generator != this->rasterGenerators.end() &&
        generator->second.callback == reinterpret_cast<const void *>(data.funcCB) &&
        generator->second.kind == RasterGeneratorKind::NORMAL) {
      this->isUnsupported = true;
      this->builderError = "Cube NormalMap geometry requires hardware texgen unavailable in CPU capture";
      return v;
    }
  }
  const SoType shapeType = action->getCurPathTail()->getTypeId();
  const bool canonicalGenerator = shapeType == SoCube::getClassTypeId() ||
    shapeType == SoCone::getClassTypeId() || shapeType == SoCylinder::getClassTypeId() ||
    shapeType == SoSphere::getClassTypeId();
  // Other generators already evaluated the primary callback. Preserve that
  // value, including R/Q, rather than calling a user function twice.
  if ((rs.hasTexture || (rs.portableShaderSlot &&
       (this->currentPlan.portableShaders[rs.portableShaderSlot - 1].ir.coordinateMask & 1u))) && canonicalGenerator &&
      coords->getType(0) == SoMultiTextureCoordinateElement::FUNCTION)
    tc = coords->get(0, pt, n);

  v.position[0] = pt[0];
  v.position[1] = pt[1];
  v.position[2] = pt[2];

  v.normal[0] = n[0];
  v.normal[1] = n[1];
  v.normal[2] = n[2];

  v.texcoord[0] = tc[0];
  v.texcoord[1] = tc[1];
  v.textureR[0] = tc[2];
  v.textureQ[0] = tc[3];
  v.materialSlot = materialSlot;

  bool hasExtraCoordinates = rs.portableShaderSlot &&
    (this->currentPlan.portableShaders[rs.portableShaderSlot - 1].ir.coordinateMask & 0xfeu);
  for (const auto & texture : rs.extraTextures)
    if (texture.enabled) hasExtraCoordinates = true;
  if (!hasExtraCoordinates) return v;

  const auto * positions = SoCoordinateElement::getInstance(state);
  const SoDetail * detail = pv->getDetail();
  int texIndex = -1;
  auto matchPoint = [&](const SoPointDetail * point) {
    if (!point || texIndex >= 0) return;
    const int index = point->getCoordinateIndex();
    if (index >= 0 && index < positions->getNum() &&
        positions->get3(index).equals(pt, 1.0e-10f))
      texIndex = point->getTextureCoordIndex();
  };
  if (detail && detail->isOfType(SoPointDetail::getClassTypeId()))
    texIndex = static_cast<const SoPointDetail *>(detail)->getTextureCoordIndex();
  else if (detail && detail->isOfType(SoFaceDetail::getClassTypeId())) {
    const auto * face = static_cast<const SoFaceDetail *>(detail);
    const int corner = action->getCurrentTrianglePointDetailIndex(pv);
    // QUAD_STRIP's assembler stores vertices in GL stream order 0,1,2,3,
    // but its SoFaceDetail lists polygon order 0,1,3,2. The callback index
    // names the former; map the right pair before looking up UV for this
    // exact vertex. Flat paths pass copies and use the position fallback.
    const int detailCorner = shapeType == SoQuadMesh::getClassTypeId() &&
        face->getNumPoints() == 4 && (corner == 2 || corner == 3) ?
        5 - corner : corner;
    if (detailCorner >= 0 && detailCorner < face->getNumPoints())
      texIndex = face->getPoint(detailCorner)->getTextureCoordIndex();
    else
      for (int i = 0; i < face->getNumPoints(); ++i) matchPoint(face->getPoint(i));
  } else if (detail && detail->isOfType(SoLineDetail::getClassTypeId())) {
    const auto * line = static_cast<const SoLineDetail *>(detail);
    matchPoint(line->getPoint0()); matchPoint(line->getPoint1());
  }
  for (size_t unit = 1; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
    if (!rs.extraTextures[unit - 1].enabled &&
        (!rs.portableShaderSlot || !(this->currentPlan.portableShaders[rs.portableShaderSlot - 1].ir.coordinateMask & (1u << unit)))) continue;
    const auto type = coords->getType(unit);
    if (type == SoMultiTextureCoordinateElement::FUNCTION ||
        (canonicalGenerator && type == SoMultiTextureCoordinateElement::DEFAULT)) {
      // Built-ins use their canonical map for DEFAULT in every GL texture unit.
      // General shapes install per-unit default functions while generating.
      int primaryUnit = 0;
      if (!SoMultiTextureEnabledElement::get(state, 0)) {
        for (primaryUnit = 1; primaryUnit < int(COIN_RENDER_MAX_TEXTURE_UNITS); ++primaryUnit)
          if (SoMultiTextureEnabledElement::get(state, primaryUnit)) break;
      }
      const SbVec4f generated = type == SoMultiTextureCoordinateElement::DEFAULT ||
        (!canonicalGenerator && int(unit) == primaryUnit) ?
        pv->getTextureCoords() : coords->get(int(unit), pt, n);
      v.extraTexcoords[unit - 1][0] = generated[0];
      v.extraTexcoords[unit - 1][1] = generated[1];
      v.textureR[unit] = generated[2]; v.textureQ[unit] = generated[3];
      continue;
    }
    if (type != SoMultiTextureCoordinateElement::EXPLICIT) {
      this->isUnsupported = true;
      this->builderError = "Texture coordinate generator unavailable for unit " + std::to_string(unit);
      continue;
    }
    // The precise QuadMesh fan emits its center with texture index -1.
    // Reconstruct each additional unit from that quad's four explicit,
    // homogeneous coordinates using the same quantized weights as CoinGL.
    if (texIndex < 0 && shapeType == SoQuadMesh::getClassTypeId() &&
        detail && detail->isOfType(SoFaceDetail::getClassTypeId())) {
      const auto * face = static_cast<const SoFaceDetail *>(detail);
      // addTriangle may pass a copied SoPrimitiveVertex for flat lighting;
      // then the action cannot map its address back to a point-detail slot.
      // A TRIANGLE_FAN keeps the center at detail point zero, and the center
      // position is the exact average emitted by SoQuadMesh.
      if (face->getNumPoints() > 0 &&
          face->getPoint(0)->getCoordinateIndex() < 0) {
        const auto * mesh = static_cast<const SoQuadMesh *>(action->getCurPathTail());
        const int64_t row = face->getPartIndex(), col = face->getFaceIndex();
        const int64_t rowsize = mesh->verticesPerRow.getValue();
        const int64_t colsize = mesh->verticesPerColumn.getValue();
        const int64_t start = mesh->startIndex.getValue();
        if (rowsize < 2 || colsize < 2 || start < 0 ||
            row < 0 || row >= colsize-1 || col < 0 || col >= rowsize-1) {
          this->hasError = true;
          this->builderError = "Invalid precise QuadMesh center face indices";
          continue;
        }
        const int64_t wideIndex[4] = {row*rowsize+col, (row+1)*rowsize+col,
                                      row*rowsize+col+1, (row+1)*rowsize+col+1};
        bool valid = true;
        for (int k=0;k<4;++k)
          valid = valid && wideIndex[k] >= 0 &&
                    wideIndex[k] < coords->getNum(unit) &&
                    start+wideIndex[k] < positions->getNum();
        if (!valid) {
          this->hasError = true;
          this->builderError = "Precise QuadMesh center needs four valid texture coordinates per unit";
          continue;
        }
        int index[4];
        SbVec3f points[4];
        for (int k=0;k<4;++k) {
          index[k]=int(wideIndex[k]);
          points[k]=positions->get3(int(start+wideIndex[k]));
        }
        const SbVec3f center=(points[0]+points[1]+points[2]+points[3])*.25f;
        // Other fan corners still use their own explicit indices.
        if (!pv->getPoint().equals(center,1.0e-12f)) {
          // The point detail can be shared by copied primitive vertices.
          // A corner with a missing index is invalid, never a center.
          this->hasError = true;
          this->builderError = "Precise QuadMesh corner has no texture coordinate index";
          continue;
        }
        const float d0=(points[0]-center).sqrLength(), d1=(points[1]-center).sqrLength();
        const float d2=(points[2]-center).sqrLength(), d3=(points[3]-center).sqrLength();
        auto quantizedWeight=[](float ratio) {
          // Match SoQuadMesh::qmeshGetWeight's configured fallback exactly.
          // Some Coin builds do not detect ilogb and use exponent 16 for
          // every quad, including asymmetric ones.
#if defined(HAVE_ILOGB)
          const int exponent=std::ilogb(ratio)+16;
#elif defined(HAVE__LOGB)
          const int exponent=int(::_logb(ratio))+16;
#else
          (void)ratio;
          const int exponent=16;
#endif
          if (exponent < 0) return 0.0f;
          if (exponent >= 32) return 1.0f;
          const double p=std::sqrt(std::ldexp(.75,exponent-16));
          return float(p/(1.0+p));
        };
        const float w0=.5f*quantizedWeight(d0/d3);
        const float w1=.5f*quantizedWeight(d1/d2);
        const float weights[4]={w0,w1,.5f-w1,.5f-w0};
        SbVec4f uv(0,0,0,0);
        for (int k=0;k<4;++k) {
          const SbVec4f cornerUv=coords->get4(unit,index[k]);
          uv+=cornerUv*weights[k];
        }
        v.extraTexcoords[unit-1][0]=uv[0];
        v.extraTexcoords[unit-1][1]=uv[1];
        v.textureR[unit]=uv[2];
        v.textureQ[unit]=uv[3];
        continue;
      }
    }
    if (texIndex < 0 || texIndex >= coords->getNum(unit)) {
      this->hasError = true;
      this->builderError = "Multitexture explicit coordinates require a valid primitive detail index: " +
        std::string(action->getCurPathTail()->getTypeId().getName().getString()) +
        " detail " + (detail ? detail->getTypeId().getName().getString() : "none") +
        " index " + std::to_string(texIndex) + " count " + std::to_string(coords->getNum(unit));
      continue;
    }
    const int dimension = coords->getDimension(unit);
    if (dimension == 2) {
      const SbVec2f & uv = coords->get2(unit, texIndex);
      v.extraTexcoords[unit - 1][0] = uv[0]; v.extraTexcoords[unit - 1][1] = uv[1];
    } else if (dimension == 3) {
      const SbVec3f & uv = coords->get3(unit, texIndex);
      v.extraTexcoords[unit - 1][0] = uv[0]; v.extraTexcoords[unit - 1][1] = uv[1];
      v.textureR[unit] = uv[2];
    } else {
      const SbVec4f & uv = coords->get4(unit, texIndex);
      v.extraTexcoords[unit - 1][0] = uv[0]; v.extraTexcoords[unit - 1][1] = uv[1];
      v.textureR[unit] = uv[2]; v.textureQ[unit] = uv[3];
    }
  }

  return v;
}

uint32_t
CoinRenderFramePlanBuilder::addVertex(SoCallbackAction * action, const SoPrimitiveVertex * pv, uint32_t materialSlot)
{
  const auto & rs = this->currentPlan.renderStates[
    this->currentPlan.draws[this->currentDrawIndex].renderStateSlot];
  const CoinRenderVertexSnapshot vertex = this->captureVertex(action, pv, materialSlot, rs);
  const SoNode * bumpSource = SoBumpMapElement::getSource(action->getState());
  const SbUniqueId bumpRevision = bumpSource ? bumpSource->getNodeId() : 0;
  if (bumpSource != this->lastBumpSource || bumpRevision != this->lastBumpRevision) {
    this->lastBumpSource = bumpSource;
    this->lastBumpRevision = bumpRevision;
    this->lastBumpSlot = 0;
    if (bumpSource && bumpSource->isOfType(SoBumpMap::getClassTypeId())) {
      const auto cached = this->bumpImageSlots.find(bumpSource);
      if (cached != this->bumpImageSlots.end() && cached->second.first == bumpRevision) {
        this->lastBumpSlot = cached->second.second;
      } else {
        CoinRenderBumpImage image;
        std::string diagnostic;
        if (!coin_render_capture_bump_node(*static_cast<const SoBumpMap *>(bumpSource),
                                            image, diagnostic)) {
          this->hasError = true;
          this->builderError = diagnostic;
          return 0;
        }
        if (!image.rgb.empty()) {
          this->currentPlan.bumpImages.push_back(std::move(image));
          this->lastBumpSlot = static_cast<uint32_t>(this->currentPlan.bumpImages.size());
        }
        this->bumpImageSlots[bumpSource] = {bumpRevision, this->lastBumpSlot};
      }
    }
  }
  const uint32_t bumpSlot = this->lastBumpSlot;
  uint32_t bumpTransformSlot = 0;
  if (bumpSlot) {
    const SbMatrix & matrix = SoBumpMapMatrixElement::get(action->getState());
    const SbMatrix identity = SbMatrix::identity();
    if (std::memcmp(matrix.getValue(), identity.getValue(), sizeof(float) * 16) != 0) {
      for (size_t i = 0; i < this->currentPlan.bumpTransforms.size(); ++i)
        if (std::memcmp(matrix.getValue(), this->currentPlan.bumpTransforms[i].getValue(),
                        sizeof(float) * 16) == 0) {
          bumpTransformSlot = static_cast<uint32_t>(i + 1);
          break;
        }
      if (!bumpTransformSlot) {
        this->currentPlan.bumpTransforms.push_back(matrix);
        bumpTransformSlot = static_cast<uint32_t>(this->currentPlan.bumpTransforms.size());
      }
    }
  }
  const bool authoredBumpCoords =
    SoBumpMapCoordinateElement::getInstance(action->getState())->getNum() > 0;
  CoinRenderBumpCoord bumpCoord(vertex.texcoord[0], vertex.texcoord[1]);
  if (authoredBumpCoords) {
    std::string diagnostic;
    if (!coin_render_capture_bump_coord(action, pv, bumpCoord, diagnostic)) {
      this->hasError = true;
      this->builderError = diagnostic;
      return 0;
    }
  }
  uint32_t * cubeSlot = nullptr;
  if (!authoredBumpCoords && !bumpSlot && this->reuseCubeVertices &&
      action->getCurPathTail() == this->stableShape) {
    // A face/corner key finds candidates without per-vertex heap allocation.
    // The full captured bytes decide equality: material, normal, coordinates
    // and expanded attributes must all match. Collisions only miss reuse.
    const int axis = vertex.normal[0] != 0 ? 0 : vertex.normal[1] != 0 ? 1 : 2;
    const unsigned face = unsigned(axis * 2 + (vertex.normal[axis] < 0 ? 1 : 0));
    const unsigned corner = (vertex.position[0] < 0 ? 1u : 0u) |
      (vertex.position[1] < 0 ? 2u : 0u) | (vertex.position[2] < 0 ? 4u : 0u);
    cubeSlot = &this->cubeVertexSlots[face * 8 + corner];
    const auto & draw = this->currentPlan.draws[this->currentDrawIndex];
    // Reuse is restricted to this occurrence and this contiguous draw range.
    if (*cubeSlot >= draw.geometry.firstVertex && *cubeSlot < this->currentPlan.vertices.size() &&
        std::memcmp(&this->currentPlan.vertices[*cubeSlot], &vertex, sizeof(vertex)) == 0)
      return *cubeSlot;
  }
  const uint32_t index = static_cast<uint32_t>(this->currentPlan.vertices.size());
  this->currentPlan.vertices.push_back(vertex);
  if (bumpSlot || bumpCoord.s != vertex.texcoord[0] || bumpCoord.t != vertex.texcoord[1]) {
    CoinRenderBumpCoordOverride override;
    override.vertexIndex = index;
    override.imageSlot = bumpSlot;
    override.transformSlot = bumpTransformSlot;
    override.coord[0] = bumpCoord.s;
    override.coord[1] = bumpCoord.t;
    this->currentPlan.bumpCoords.push_back(override);
  }
  if (bumpSlot) {
    CoinRenderBumpLegacyCorner corner;
    for (int axis = 0; axis < 3; ++axis) {
      corner.position[axis] = pv->getPoint()[axis];
      corner.normal[axis] = pv->getNormal()[axis];
    }
    for (int axis = 0; axis < 4; ++axis)
      corner.textureCoord0[axis] = pv->getTextureCoords()[axis];
    corner.bumpCoord[0] = bumpCoord.s;
    corner.bumpCoord[1] = bumpCoord.t;
    const SoDetail * detail = pv->getDetail();
    if (detail && detail->isOfType(SoFaceDetail::getClassTypeId())) {
      const auto * face = static_cast<const SoFaceDetail *>(detail);
      const int point = action->getCurrentTrianglePointDetailIndex(pv);
      if (point >= 0 && point < face->getNumPoints())
        corner.textureCoordIndex = face->getPoint(point)->getTextureCoordIndex();
    }
    const SoLazyElement * lazy = SoLazyElement::getInstance(action->getState());
    const int diffuse = std::max(0, std::min(pv->getMaterialIndex(), lazy->getNumDiffuse() - 1));
    if (lazy->isPacked()) {
      corner.rgba = lazy->getPackedPointer()[diffuse];
    } else {
      const int transparency = std::max(0, std::min(pv->getMaterialIndex(),
                                                   lazy->getNumTransparencies() - 1));
      corner.rgba = lazy->getDiffusePointer()[diffuse].getPackedValue(
        lazy->getTransparencyPointer()[transparency]);
    }
    const uint32_t local = static_cast<uint32_t>(this->bumpShapeCorners.size());
    this->bumpShapeCorners.push_back(corner);
    this->bumpShapeVertices.push_back(index);
    this->bumpCornerByVertex.emplace(index, local);
  }
  if (cubeSlot) *cubeSlot = index;
  return index;
}

void
CoinRenderFramePlanBuilder::ensureDrawPacket(CoinRenderPrimitiveTopology topology, uint32_t renderStateSlot, SoNode * node, bool forceNewPacket)
{
  SbUniqueId nodeId = node ? node->getNodeId() : 0;
  if (!forceNewPacket && this->hasActiveDraw) {
    CoinRenderDrawPacket & active = this->currentPlan.draws[this->currentDrawIndex];
    if (active.topology == topology &&
        active.renderStateSlot == renderStateSlot &&
        active.multipassMask == this->currentMultipassMask &&
        active.multipassRasterFixed == this->currentMultipassRasterFixed &&
        active.renderLayer == this->currentAnnotationLayer &&
        active.sourceNodeId == nodeId) {
      // A shared range can live before another draw's indices. Make its index
      // stream appendable before extending this occurrence, without changing
      // any earlier occurrence that references the same geometry.
      auto & range = active.geometry;
      CoinRenderPlanAssemblyCore::makeIndicesAppendable(this->currentPlan, range);
      return; // Continue active packet
    }
  }

  CoinRenderDrawPacket dp;
  dp.topology = topology;
  dp.renderStateSlot = renderStateSlot;
  dp.multipassMask = this->currentMultipassMask;
  dp.multipassRasterFixed = this->currentMultipassRasterFixed;
  dp.frameNodeOrdinal = ++this->nodeCounter;
  dp.sourceNodeId = nodeId;
  dp.sourceOccurrence = this->occurrenceSerial;
  dp.renderLayer = this->currentAnnotationLayer;
  dp.authoredScene = dp.renderLayer != 0 && dp.renderLayer == this->authoredSceneLayer;
  dp.clearDepthBefore = dp.renderLayer != 0 && this->annotationDepthClearPending;
  if (dp.clearDepthBefore) this->annotationDepthClearPending = false;
  dp.geometry.firstVertex = static_cast<uint32_t>(this->currentPlan.vertices.size());
  dp.geometry.vertexCount = 0;
  dp.geometry.firstIndex = static_cast<uint32_t>(this->currentPlan.indices.size());
  dp.geometry.indexCount = 0;

  this->currentDrawIndex = static_cast<uint32_t>(this->currentPlan.draws.size());
  this->currentPlan.draws.push_back(dp);
  this->hasActiveDraw = true;
}

void
CoinRenderFramePlanBuilder::captureSortingCenter(SoCallbackAction * action)
{
  auto & draw = this->currentPlan.draws[this->currentDrawIndex];
  if (draw.geometry.indexCount != 0 || !draw.isSceneLayer()) return;
  this->currentPlan.delayedObjDepthWrite = this->delayedObjDepthWrite;
  const auto & state = this->currentPlan.renderStates[draw.renderStateSlot];
  const int type = state.transparencyType;
  const bool additive = type == SoGLRenderAction::SORTED_OBJECT_ADD ||
    type == SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD;
  if (type != SoGLRenderAction::SORTED_OBJECT_ADD && type != SoGLRenderAction::SORTED_OBJECT_BLEND &&
      type != SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD &&
      type != SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND) return;
  SoNode * node = action->getCurPathTail();
  if (!node || !node->isOfType(SoShape::getClassTypeId())) return;
  const bool transparent = state.rasterPixels ? state.rasterTransparent :
    (state.transparentMaterial || state.transparentTexture);
  const bool sorted = transparent && (this->objectSortingEnabled || additive);
  if (this->sortingStrategy == 3 && sorted) {
    if (!this->occurrenceSortingComputed) {
      this->occurrenceSortingDistance = this->customSortingDistance ?
        this->customSortingDistance(action) : std::numeric_limits<float>::quiet_NaN();
      this->occurrenceSortingComputed = true;
    }
    if (!std::isfinite(this->occurrenceSortingDistance)) {
      this->hasError = true;
      this->builderError = "Custom sorting distance must be finite";
      return;
    }
    draw.hasSortingDistance = true;
    draw.sortingDistance = this->occurrenceSortingDistance;
    return;
  }
  SbBox3f box; SbVec3f center;
  static_cast<SoShape *>(node)->computeBBox(action, box, center);
  if (box.isEmpty()) return;
  if (sorted && (this->sortingStrategy == 1 || this->sortingStrategy == 2)) {
    const auto plane = action->getViewVolume().getPlane(0.0f);
    float distance = 0;
    for (int corner = 0; corner < 8; ++corner) {
      const auto low = box.getMin(), high = box.getMax();
      SbVec3f point(corner & 1 ? high[0] : low[0], corner & 2 ? high[1] : low[1], corner & 4 ? high[2] : low[2]);
      action->getModelMatrix().multVecMatrix(point, point);
      const float value = -plane.getDistance(point);
      if (!std::isfinite(value)) {
        this->hasError = true; this->builderError = "Object sorting distance must be finite";
        return;
      }
      if (!corner || (this->sortingStrategy == 1 ? value < distance : value > distance)) {
        distance = value;
      }
    }
    draw.hasSortingDistance = true; draw.sortingDistance = distance;
    return;
  }
  CoinRenderPlanAssemblyCore::sortingCenter(draw, action->getModelMatrix(), center);
}

bool
CoinRenderFramePlanBuilder::isShapeInvisible(SoCallbackAction * action)
{
  return action && (SoShapeStyleElement::get(action->getState())->getFlags() &
                    SoShapeStyleElement::INVISIBLE) != 0;
}

int
CoinRenderFramePlanBuilder::polygonDrawStyle(SoCallbackAction * action)
{
  return SoDrawStyleElement::get(action->getState());
}

void
CoinRenderFramePlanBuilder::addTriangle(SoCallbackAction * action,
                                   const SoPrimitiveVertex * v0,
                                   const SoPrimitiveVertex * v1,
                                   const SoPrimitiveVertex * v2)
{
  if (!v0 || !v1 || !v2 || isShapeInvisible(action)) return;
  const bool quadMesh = action->getCurPathTail()->getTypeId() == SoQuadMesh::getClassTypeId();
  const char *precise = quadMesh ? coin_getenv("COIN_QUADMESH_PRECISE_LIGHTING") : nullptr;
  bool centered = false;
  if (precise && std::atoi(precise) > 0 &&
      SoCoordinateElement::getInstance(action->getState())->is3D()) {
    const auto mb = SoMaterialBindingElement::get(action->getState());
    if (mb != SoMaterialBindingElement::PER_VERTEX &&
        mb != SoMaterialBindingElement::PER_VERTEX_INDEXED) {
      SoTextureCoordinateBundle texture(action, FALSE, FALSE);
      centered = !texture.isFunction();
    }
  }
  if (centered) {
    const auto * coordinates = SoMultiTextureCoordinateElement::getInstance(action->getState());
    for (int unit = 1; unit < int(COIN_RENDER_MAX_TEXTURE_UNITS); ++unit) {
      if (SoMultiTextureEnabledElement::get(action->getState(), unit) &&
          coordinates->getType(unit) != SoMultiTextureCoordinateElement::EXPLICIT) {
        this->isUnsupported = true;
        this->builderError =
            "UNSUPPORTED: centered QuadMesh requires explicit coordinates in every additional texture unit";
        return;
      }
    }
  }
  if (polygonDrawStyle(action) != SoDrawStyleElement::FILLED) {
    if (centered) {
      // Polygon mode applies to each GL_TRIANGLE_FAN triangle, including its
      // center spokes. Treat each callback triangle as its own styled polygon.
      this->polygonNode = action->getCurPathTail();
      this->polygonStyle = polygonDrawStyle(action);
      this->polygonState = this->captureRenderState(action, v0->getMaterialIndex());
      if (this->isUnsupported || this->hasError) {
        this->polygonNode = nullptr;
        return;
      }
      const auto state = this->currentPlan.renderStates[this->polygonState];
      this->polygonVertices.clear();
      for (const auto *source : {v0, v1, v2}) {
        const uint32_t material = this->captureMaterial(action, source->getMaterialIndex());
        this->polygonVertices.push_back(this->captureVertex(action, source, material, state));
      }
      if (!this->isUnsupported && !this->hasError)
        this->emitStyledPolygon(action);
      this->polygonNode = nullptr;
      this->polygonVertices.clear();
      return;
    }
    this->addStyledTriangle(action, v0, v1, v2);
    return;
  }
  if (action->getCurPathTail()->getTypeId() == SoTriangleStripSet::getClassTypeId() ||
      action->getCurPathTail()->getTypeId() == SoIndexedTriangleStripSet::getClassTypeId()) {
    const auto mb = SoMaterialBindingElement::get(action->getState());
    const auto nb = SoNormalBindingElement::get(action->getState());
    const bool faceMaterial = mb == SoMaterialBindingElement::PER_FACE ||
                              mb == SoMaterialBindingElement::PER_FACE_INDEXED;
    const bool faceNormal =
        nb == SoNormalBindingElement::PER_FACE || nb == SoNormalBindingElement::PER_FACE_INDEXED;
    const bool vertexMaterial = mb == SoMaterialBindingElement::PER_VERTEX ||
                                mb == SoMaterialBindingElement::PER_VERTEX_INDEXED;
    const bool vertexNormal = nb == SoNormalBindingElement::PER_VERTEX ||
                              nb == SoNormalBindingElement::PER_VERTEX_INDEXED;
    SoPrimitiveVertex prior0, prior1;
    if (faceMaterial && vertexNormal) {
      const auto *rawDetail = v0->getDetail();
      if (!rawDetail || !rawDetail->isOfType(SoFaceDetail::getClassTypeId())) {
        this->hasError = true;
        this->builderError = "Missing triangle strip face detail";
        return;
      }
      const auto *detail = static_cast<const SoFaceDetail *>(rawDetail);
      const int part = detail->getPartIndex();
      const SoNode *node = action->getCurPathTail();
      const int newest = v2->getMaterialIndex();
      // FaceDetail::faceIndex advances after each shapeVertex callback in
      // SoTriangleStripSet, so the second triangle still reports face zero.
      // Count callbacks within the part instead of relying on that index.
      const bool newStrip = this->stripMaterialNode != node ||
                            this->stripMaterialPart != part;
      const int face = newStrip ? 0 : this->stripMaterialNextFace;
      if (newStrip) {
        this->stripMaterialNode = node;
        this->stripMaterialPart = part;
        this->stripMaterialNextFace = 1;
        this->stripMaterialOlder = newest;
        this->stripMaterialNewer = newest;
      } else {
        prior0 = *v0;
        prior1 = *v1;
        prior0.setMaterialIndex((face & 1) ? this->stripMaterialNewer : this->stripMaterialOlder);
        prior1.setMaterialIndex((face & 1) ? this->stripMaterialOlder : this->stripMaterialNewer);
        v0 = &prior0;
        v1 = &prior1;
        this->stripMaterialOlder = this->stripMaterialNewer;
        this->stripMaterialNewer = newest;
        this->stripMaterialNextFace = face + 1;
      }
    }
    if ((faceNormal && !vertexMaterial) || (faceMaterial && !vertexNormal)) {
      uint32_t rsSlot = this->captureRenderState(action, v2->getMaterialIndex());
      auto rs = this->currentPlan.renderStates[rsSlot];
      const uint32_t sourceMaterial = rs.materialSlot;
      CoinRenderVertexSnapshot provoking;
      for (int c = 0; c < 3; ++c) {
        provoking.position[c] = v2->getPoint()[c];
        provoking.normal[c] = v2->getNormal()[c];
      }
      const uint32_t material = this->internMaterial(
          coin_render_bake_vertex_material(provoking, this->currentPlan.materials[sourceMaterial],
                                           rs, this->currentPlan.lightingStates[rs.lightingSlot]));
      rs.lightModel = CoinRenderLightModel::BASE_COLOR;
      rs.materialSlot = material;
      rsSlot = CoinRenderPlanAssemblyCore::state(this->currentPlan, this->renderStatesByModel, rs);
      this->ensureDrawPacket(CoinRenderPrimitiveTopology::TRIANGLE_LIST, rsSlot,
                             action->getCurPathTail());
      this->captureSortingCenter(action);
      for (const auto *source : {v0, v1, v2}) {
        const uint32_t captured = this->addVertex(action, source, material);
        this->currentPlan.indices.push_back(captured);
      }
      this->recordBumpTriangle(this->currentPlan.indices[this->currentPlan.indices.size() - 3],
                               this->currentPlan.indices[this->currentPlan.indices.size() - 2],
                               this->currentPlan.indices.back());
      auto &draw = this->currentPlan.draws[this->currentDrawIndex];
      draw.geometry.vertexCount =
          static_cast<uint32_t>(this->currentPlan.vertices.size()) - draw.geometry.firstVertex;
      draw.geometry.indexCount += 3;
      return;
    }
  }
  if (quadMesh) {
    const auto mb = SoMaterialBindingElement::get(action->getState());
    const auto nb = SoNormalBindingElement::get(action->getState());
    const bool faceMaterial = mb == SoMaterialBindingElement::PER_FACE ||
                              mb == SoMaterialBindingElement::PER_FACE_INDEXED;
    const bool faceNormal =
        nb == SoNormalBindingElement::PER_FACE || nb == SoNormalBindingElement::PER_FACE_INDEXED;
    if (centered && (faceMaterial || faceNormal)) {
      // CoinGL enables GL_FLAT for either face binding. A precise QuadMesh
      // draws a separate fan per quad, so each triangle takes its lit color
      // from the final vertex rather than interpolating the corner colors.
      // Inside SoShadowGroup, however, CoinGL's shader interpolates its own
      // lighting varyings while GL_FLAT selects only gl_Color. Keep the
      // provoking diffuse material but retain PHONG for that shader profile.
      uint32_t rsSlot = this->captureRenderState(action, v2->getMaterialIndex());
      auto rs = this->currentPlan.renderStates[rsSlot];
      const bool shadowShader = rs.shadowGroupSlot && (rs.shadowStyle & 2u);
      rs.flatShadowPrimary = shadowShader;
      const uint32_t sourceMaterial = this->captureMaterial(action, v2->getMaterialIndex());
      uint32_t material = sourceMaterial;
      if (!shadowShader) {
        CoinRenderVertexSnapshot provoking;
        for (int c = 0; c < 3; ++c) {
          provoking.position[c] = v2->getPoint()[c];
          provoking.normal[c] = v2->getNormal()[c];
        }
        material = this->internMaterial(
            coin_render_bake_vertex_material(provoking, this->currentPlan.materials[sourceMaterial],
                                             rs, this->currentPlan.lightingStates[rs.lightingSlot]));
        rs.lightModel = CoinRenderLightModel::BASE_COLOR;
      }
      rs.materialSlot = material;
      rsSlot = CoinRenderPlanAssemblyCore::state(this->currentPlan, this->renderStatesByModel, rs);
      this->ensureDrawPacket(CoinRenderPrimitiveTopology::TRIANGLE_LIST, rsSlot,
                             action->getCurPathTail());
      this->captureSortingCenter(action);
      for (const auto *source : {v0, v1, v2}) {
        SoPrimitiveVertex copy = *source;
        const uint32_t vertexMaterial = shadowShader
            ? this->captureMaterial(action, source->getMaterialIndex()) : material;
        const uint32_t captured = this->addVertex(action, &copy, vertexMaterial);
        this->currentPlan.indices.push_back(captured);
      }
      this->recordBumpTriangle(this->currentPlan.indices[this->currentPlan.indices.size() - 3],
                               this->currentPlan.indices[this->currentPlan.indices.size() - 2],
                               this->currentPlan.indices.back());
      auto &draw = this->currentPlan.draws[this->currentDrawIndex];
      draw.geometry.vertexCount =
          static_cast<uint32_t>(this->currentPlan.vertices.size()) - draw.geometry.firstVertex;
      draw.geometry.indexCount += 3;
      return;
    }
    if ((faceMaterial || faceNormal) && !centered) {
      // SoQuadMesh's GL_QUAD_STRIP uses flat primary color for these bindings.
      // Its callback shares vertices between quads; normalize the face's
      // attributes and retain the original quad provoking point for both triangles.
      const auto *detail = static_cast<const SoFaceDetail *>(v0->getDetail());
      if (!detail || detail->getNumPoints() != 4) {
        this->hasError = true;
        this->builderError = "Invalid QuadMesh face detail";
        return;
      }
      const auto *coordinates = SoCoordinateElement::getInstance(action->getState());
      const int index = detail->getPoint(2)->getCoordinateIndex();
      if (index < 0 || index >= coordinates->getNum()) {
        this->hasError = true;
        this->builderError = "Invalid QuadMesh provoking coordinate";
        return;
      }
      const SoPrimitiveVertex *pv = nullptr;
      for (const auto *vertex : {v0, v1, v2})
        if (vertex->getPoint() == coordinates->get3(index))
          pv = vertex;
      if (!pv) {
        this->hasError = true;
        this->builderError = "Missing QuadMesh provoking vertex";
        return;
      }
      uint32_t rsSlot = this->captureRenderState(action, v0->getMaterialIndex());
      auto rs = this->currentPlan.renderStates[rsSlot];
      const bool shadowShader = rs.shadowGroupSlot && (rs.shadowStyle & 2u);
      rs.flatShadowPrimary = shadowShader;
      bool firstTriangle = false;
      if (shadowShader) {
        const SoNode *node = action->getCurPathTail();
        if (this->quadShadowNode != node || this->quadShadowPart != detail->getPartIndex() ||
            this->quadShadowFace != detail->getFaceIndex()) {
          this->quadShadowNode = node;
          this->quadShadowPart = detail->getPartIndex();
          this->quadShadowFace = detail->getFaceIndex();
          this->quadShadowTriangle = 0;
        }
        if (this->quadShadowTriangle >= 2) {
          this->hasError = true;
          this->builderError = "Invalid QuadMesh strip triangle sequence";
          return;
        }
        firstTriangle = this->quadShadowTriangle++ == 0;
      }
      const uint32_t sourceMaterial = this->captureMaterial(
          action, faceMaterial ? v0->getMaterialIndex() : pv->getMaterialIndex());
      SoPrimitiveVertex provoking = *pv;
      SbVec3f faceNormalValue = v0->getNormal();
      if (faceNormal) {
        const auto &right = coordinates->get3(detail->getPoint(2)->getCoordinateIndex());
        for (const auto *source : {v0, v1, v2})
          if (source->getPoint() == right)
            faceNormalValue = source->getNormal();
        provoking.setNormal(faceNormalValue);
      }
      CoinRenderVertexSnapshot vertex;
      uint32_t material = sourceMaterial;
      if (!shadowShader) {
        for (int c = 0; c < 3; ++c) {
          vertex.position[c] = provoking.getPoint()[c];
          vertex.normal[c] = provoking.getNormal()[c];
        }
        material = this->internMaterial(
            coin_render_bake_vertex_material(vertex, this->currentPlan.materials[sourceMaterial], rs,
                                             this->currentPlan.lightingStates[rs.lightingSlot]));
        rs.lightModel = CoinRenderLightModel::BASE_COLOR;
      }
      rs.materialSlot = material;
      rsSlot = CoinRenderPlanAssemblyCore::state(this->currentPlan, this->renderStatesByModel, rs);
      this->ensureDrawPacket(CoinRenderPrimitiveTopology::TRIANGLE_LIST, rsSlot,
                             action->getCurPathTail());
      this->captureSortingCenter(action);
      int corner = 0;
      for (const auto *source : {v0, v1, v2}) {
        SoPrimitiveVertex copy = *source;
        if (faceNormal && !shadowShader)
          copy.setNormal(faceNormalValue);
        // The callback copies the current face's material index to all four
        // vertices. Native GL_QUAD_STRIP did not resend it on the left pair
        // after the first quad of each row; recover those two raw indices.
        if (shadowShader && faceMaterial && detail->getFaceIndex() > 0 &&
            (corner == 0 || (firstTriangle && corner == 1)))
          copy.setMaterialIndex(source->getMaterialIndex() - 1);
        const uint32_t vertexMaterial = shadowShader
            ? this->captureMaterial(action, copy.getMaterialIndex()) : material;
        const uint32_t captured = this->addVertex(action, &copy, vertexMaterial);
        this->currentPlan.indices.push_back(captured);
        ++corner;
      }
      this->recordBumpTriangle(this->currentPlan.indices[this->currentPlan.indices.size() - 3],
                               this->currentPlan.indices[this->currentPlan.indices.size() - 2],
                               this->currentPlan.indices.back());
      auto &draw = this->currentPlan.draws[this->currentDrawIndex];
      draw.geometry.vertexCount =
          static_cast<uint32_t>(this->currentPlan.vertices.size()) - draw.geometry.firstVertex;
      draw.geometry.indexCount += 3;
      return;
    }
  }
  uint32_t rsSlot = this->captureRenderState(action, v0->getMaterialIndex());
  this->ensureDrawPacket(CoinRenderPrimitiveTopology::TRIANGLE_LIST, rsSlot, action->getCurPathTail());
  this->captureSortingCenter(action);

  uint32_t m0 = this->currentPlan.renderStates[rsSlot].materialSlot;
  uint32_t m1 = v1->getMaterialIndex() == v0->getMaterialIndex() ? m0 :
    this->captureMaterial(action, v1->getMaterialIndex());
  uint32_t m2 = v2->getMaterialIndex() == v0->getMaterialIndex() ? m0 :
    (v2->getMaterialIndex() == v1->getMaterialIndex() ? m1 :
     this->captureMaterial(action, v2->getMaterialIndex()));

  uint32_t i0 = this->addVertex(action, v0, m0);
  uint32_t i1 = this->addVertex(action, v1, m1);
  uint32_t i2 = this->addVertex(action, v2, m2);

  this->currentPlan.indices.push_back(i0);
  this->currentPlan.indices.push_back(i1);
  this->currentPlan.indices.push_back(i2);
  this->recordBumpTriangle(i0, i1, i2);

  CoinRenderDrawPacket & dp = this->currentPlan.draws[this->currentDrawIndex];
  dp.geometry.vertexCount = static_cast<uint32_t>(this->currentPlan.vertices.size()) - dp.geometry.firstVertex;
  dp.geometry.indexCount += 3;
}

void CoinRenderFramePlanBuilder::addLine(SoCallbackAction* action, const SoPrimitiveVertex* v0,
                                         const SoPrimitiveVertex* v1) {
  if (!v0 || !v1 || isShapeInvisible(action))
    return;
  uint32_t rsSlot = this->captureRenderState(action, v0->getMaterialIndex());
  if (this->isUnsupported)
    return;
  SoNode* node = action->getCurPathTail();
  SoState* state = action->getState();
  const SoDetail* detail = v0->getDetail();
  const SoLineDetail* line = detail && detail->isOfType(SoLineDetail::getClassTypeId())
                                 ? static_cast<const SoLineDetail*>(detail)
                                 : nullptr;
  const int materialBinding = SoMaterialBindingElement::get(state);
  const int normalBinding = SoNormalBindingElement::get(state);
  // Coin's native line nodes choose GL_LINES for per-segment bindings and
  // GL_LINE_STRIP otherwise. Normals only influence GL assembly when used.
  const bool normalsUsed = SoLightModelElement::get(state) != SoLightModelElement::BASE_COLOR &&
                           SoNormalElement::getInstance(state)->getNum() > 0;
  const bool independent =
      materialBinding == SoMaterialBindingElement::PER_PART ||
      materialBinding == SoMaterialBindingElement::PER_PART_INDEXED ||
      (normalsUsed && (normalBinding == SoNormalBindingElement::PER_PART ||
                       normalBinding == SoNormalBindingElement::PER_PART_INDEXED));
  if (line && polygonDrawStyle(action) == SoDrawStyleElement::POINTS &&
      (node->getTypeId() == SoLineSet::getClassTypeId() ||
       node->getTypeId() == SoIndexedLineSet::getClassTypeId())) {
    // Native GL_POINTS repeats both endpoints of independent segments, while
    // the line-strip path emits a shared point once per coordinate occurrence.
    if (independent || line->getPartIndex() == 0)
      this->addPoint(action, v0);
    this->addPoint(action, v1);
    return;
  }
  const bool knownStrip = line && !independent &&
                          this->currentPlan.renderStates[rsSlot].linePattern != 0xffffu &&
                          (node->getTypeId() == SoLineSet::getClassTypeId() ||
                           node->getTypeId() == SoIndexedLineSet::getClassTypeId());
  uint64_t strip = 0;
  if (knownStrip) {
    if (this->lineNode != node || this->lineIndex != line->getLineIndex() ||
        line->getPartIndex() == 0 || !this->hasActiveDraw ||
        this->currentPlan.draws[this->currentDrawIndex].topology !=
            CoinRenderPrimitiveTopology::LINE_LIST)
      this->lineStripId = ++this->nextLineStripId;
    strip = this->lineStripId;
  }
  this->lineNode = node;
  this->lineIndex = line ? line->getLineIndex() : -1;
  const bool newStrip =
      this->hasActiveDraw && this->currentPlan.draws[this->currentDrawIndex].lineStripId != strip;
  this->ensureDrawPacket(CoinRenderPrimitiveTopology::LINE_LIST, rsSlot, node, newStrip);
  this->currentPlan.draws[this->currentDrawIndex].lineStripId = strip;
  this->captureSortingCenter(action);

  uint32_t m0 = this->captureMaterial(action, v0->getMaterialIndex());
  uint32_t m1 = this->captureMaterial(action, v1->getMaterialIndex());

  uint32_t i0 = this->addVertex(action, v0, m0);
  uint32_t i1 = this->addVertex(action, v1, m1);

  this->currentPlan.indices.push_back(i0);
  this->currentPlan.indices.push_back(i1);

  CoinRenderDrawPacket& dp = this->currentPlan.draws[this->currentDrawIndex];
  dp.geometry.vertexCount += 2;
  dp.geometry.indexCount += 2;
}

void CoinRenderFramePlanBuilder::addPoint(SoCallbackAction* action,
                                          const SoPrimitiveVertex* vertex) {
  if (!vertex || isShapeInvisible(action))
    return;
  uint32_t rsSlot = this->captureRenderState(action, vertex->getMaterialIndex());
  if (this->isUnsupported)
    return;
  this->ensureDrawPacket(CoinRenderPrimitiveTopology::POINT_LIST, rsSlot, action->getCurPathTail());
  this->captureSortingCenter(action);

  uint32_t m0 = this->captureMaterial(action, vertex->getMaterialIndex());
  uint32_t i0 = this->addVertex(action, vertex, m0);

  this->currentPlan.indices.push_back(i0);

  CoinRenderDrawPacket& dp = this->currentPlan.draws[this->currentDrawIndex];
  dp.geometry.vertexCount += 1;
  dp.geometry.indexCount += 1;
}

void CoinRenderFramePlanBuilder::addStyledTriangle(SoCallbackAction* action,
                                                   const SoPrimitiveVertex* v0,
                                                   const SoPrimitiveVertex* v1,
                                                   const SoPrimitiveVertex* v2) {
  if (this->isUnsupported || this->hasError)
    return;
  SoNode* node = action->getCurPathTail();
  const SoPrimitiveVertex* vertices[3] = {v0, v1, v2};
  const SoDetail* detail = v0->getDetail();
  const SoFaceDetail* face = detail && detail->isOfType(SoFaceDetail::getClassTypeId())
                                 ? static_cast<const SoFaceDetail*>(detail)
                                 : nullptr;
  const bool cube = node->isOfType(SoCube::getClassTypeId());
  const bool cone = node->isOfType(SoCone::getClassTypeId());
  const bool cylinder = node->isOfType(SoCylinder::getClassTypeId());
  const bool sphere = node->isOfType(SoSphere::getClassTypeId());
  const auto* cylinderDetail =
      cylinder && detail && detail->isOfType(SoCylinderDetail::getClassTypeId())
          ? static_cast<const SoCylinderDetail*>(detail)
          : nullptr;
  bool quad = cube || (cylinderDetail && cylinderDetail->getPart() == SoCylinder::SIDES);
  if (sphere) {
    const float radius = static_cast<SoSphere*>(node)->radius.getValue();
    bool pole = false;
    for (const auto* vertex : vertices) {
      const auto& position = vertex->getPoint();
      pole = pole || (position[0] == 0 && position[2] == 0 &&
                      (position[1] == radius || position[1] == -radius));
    }
    // SoGenerate emits cap triangles and interior QUAD_STRIP, matching SoGL.
    quad = !pole;
  }
  auto reject = [&](const char* message) {
    this->isUnsupported = true;
    this->builderError = message;
  };
  if (!face && !cube && !cone && !sphere && !cylinderDetail) {
    reject("Polygon style requires a recoverable original contour for this shape");
    return;
  }
  if (this->polygonNode && this->polygonNode != node) {
    reject("Original polygon contour was interrupted by another shape");
    return;
  }
  if (!this->polygonNode) {
    this->polygonNode = node;
    this->polygonStyle = polygonDrawStyle(action);
    this->polygonState = this->captureRenderState(action, v0->getMaterialIndex());
    if (this->isUnsupported || this->hasError)
      return;
    const size_t count = face ? static_cast<size_t>(face->getNumPoints()) : (quad ? 4 : 3);
    if (count < 3) {
      reject("Invalid original polygon contour");
      return;
    }
    this->polygonTriangles = 0;
    this->polygonFaceIndex = face ? face->getFaceIndex() : -1;
    this->polygonPartIndex = face ? face->getPartIndex() : -1;
    this->polygonPositions.clear();
    this->polygonVertices.assign(count, CoinRenderVertexSnapshot{});
    this->polygonTriangleVertices.clear();
    this->polygonCaptured.assign(count, false);
    if (face) {
      const auto* coordinates = SoCoordinateElement::getInstance(action->getState());
      for (size_t i = 0; i < count; ++i) {
        const int index = face->getPoint(static_cast<int>(i))->getCoordinateIndex();
        if (index < 0 || index >= coordinates->getNum()) {
          reject("Invalid face contour coordinate index");
          return;
        }
        const SbVec3f position = coordinates->get3(index);
        this->polygonPositions.push_back(position);
      }
    } else {
      for (int i = 0; i < 3; ++i)
        this->polygonPositions.push_back(vertices[i]->getPoint());
      if (quad)
        this->polygonPositions.push_back(SbVec3f(0, 0, 0)); // Filled by the second callback.
    }
  }
  if (face && this->polygonTriangles > 0) {
    if (face->getNumPoints() != static_cast<int>(this->polygonPositions.size()) ||
        face->getFaceIndex() != this->polygonFaceIndex ||
        face->getPartIndex() != this->polygonPartIndex) {
      reject("Face detail changed within an original polygon");
      return;
    }
    const auto* coordinates = SoCoordinateElement::getInstance(action->getState());
    for (size_t i = 0; i < this->polygonPositions.size(); ++i) {
      const int index = face->getPoint(static_cast<int>(i))->getCoordinateIndex();
      if (index < 0 || index >= coordinates->getNum() ||
          coordinates->get3(index) != this->polygonPositions[i]) {
        reject("Face contour changed within its triangle callbacks");
        return;
      }
    }
  }
  if (this->polygonStyle != polygonDrawStyle(action)) {
    reject("Draw style changed within an original polygon");
    return;
  }
  if (quad && this->polygonTriangles == 1) {
    if (v0->getPoint() != this->polygonPositions[0] ||
        v1->getPoint() != this->polygonPositions[2]) {
      reject("Procedural callbacks do not match the original quad assembly");
      return;
    }
    this->polygonPositions[3] = v2->getPoint();
  }
  const auto state = this->currentPlan.renderStates[this->polygonState];
  for (int i = 0; i < 3; ++i) {
    size_t slot = this->polygonPositions.size();
    // SoShape supplies the original point-detail slot for styled callbacks.
    // Position alone cannot identify an occurrence when a contour revisits a
    // coordinate with a different material, normal or texture coordinate.
    const int occurrence = face ? action->getCurrentTrianglePointDetailIndex(i) : -1;
    if (occurrence >= 0 && static_cast<size_t>(occurrence) < this->polygonPositions.size() &&
        vertices[i]->getPoint() == this->polygonPositions[occurrence])
      slot = static_cast<size_t>(occurrence);
    // The quad's not-yet-captured fourth corner is intentionally excluded.
    const size_t available =
        quad && this->polygonTriangles == 0 ? 3 : this->polygonPositions.size();
    if (slot == this->polygonPositions.size()) {
      for (size_t j = 0; j < available; ++j)
        if (vertices[i]->getPoint() == this->polygonPositions[j]) {
          if (slot != this->polygonPositions.size()) {
            reject("Ambiguous polygon occurrence without a point-detail index");
            return;
          }
          slot = j;
        }
    }
    if (slot == this->polygonPositions.size()) {
      reject("Triangle callback lost the original face contour");
      return;
    }
    const uint32_t material = this->captureMaterial(action, vertices[i]->getMaterialIndex());
    const auto captured = this->captureVertex(action, vertices[i], material, state);
    this->polygonTriangleVertices.push_back(captured);
    if (!this->polygonCaptured[slot]) {
      this->polygonVertices[slot] = captured;
      this->polygonCaptured[slot] = true;
    } else {
      const auto& previous = this->polygonVertices[slot];
      if (previous.materialSlot != captured.materialSlot ||
          SbVec3f(previous.normal) != SbVec3f(captured.normal) ||
          SbVec2f(previous.texcoord) != SbVec2f(captured.texcoord) ||
          std::memcmp(previous.extraTexcoords, captured.extraTexcoords,
                      sizeof(previous.extraTexcoords)) != 0 ||
          std::memcmp(previous.textureR, captured.textureR, sizeof(previous.textureR)) != 0 ||
          std::memcmp(previous.textureQ, captured.textureQ, sizeof(previous.textureQ)) != 0) {
        reject("Inconsistent attributes across triangles of one polygon");
        return;
      }
    }
  }
  ++this->polygonTriangles;
  if (this->polygonTriangles == static_cast<int>(this->polygonVertices.size()) - 2) {
    for (bool captured : this->polygonCaptured)
      if (!captured) {
        reject("Original face contour has uncaptured vertices");
        return;
      }
    this->emitStyledPolygon(action);
    this->polygonNode = nullptr;
    this->polygonPositions.clear();
    this->polygonVertices.clear();
    this->polygonTriangleVertices.clear();
    this->polygonCaptured.clear();
  }
}

void CoinRenderFramePlanBuilder::emitStyledPolygon(SoCallbackAction* action, bool preserveDegenerateContour) {
  auto sourceState = this->currentPlan.renderStates[this->polygonState];
  const auto type = this->polygonNode->getTypeId();
  const auto mb = SoMaterialBindingElement::get(action->getState());
  const auto nb = SoNormalBindingElement::get(action->getState());
  const bool faceMaterial =
      mb == SoMaterialBindingElement::PER_FACE || mb == SoMaterialBindingElement::PER_FACE_INDEXED;
  const bool faceNormal =
      nb == SoNormalBindingElement::PER_FACE || nb == SoNormalBindingElement::PER_FACE_INDEXED;
  const bool vertexMaterial = mb == SoMaterialBindingElement::PER_VERTEX ||
                              mb == SoMaterialBindingElement::PER_VERTEX_INDEXED;
  const bool vertexNormal =
      nb == SoNormalBindingElement::PER_VERTEX || nb == SoNormalBindingElement::PER_VERTEX_INDEXED;
  const bool flatQuad = type == SoQuadMesh::getClassTypeId() && (faceMaterial || faceNormal);
  const bool flatStrip = (type == SoTriangleStripSet::getClassTypeId() ||
                          type == SoIndexedTriangleStripSet::getClassTypeId()) &&
                         ((faceNormal && !vertexMaterial) || (faceMaterial && !vertexNormal));
  if ((flatQuad || flatStrip) && this->polygonVertices.size() > 2) {
    const auto &provoking = this->polygonVertices[2];
    const uint32_t material = this->internMaterial(coin_render_bake_vertex_material(
        provoking, this->currentPlan.materials[provoking.materialSlot], sourceState,
        this->currentPlan.lightingStates[sourceState.lightingSlot]));
    for (auto &vertex : this->polygonVertices)
      vertex.materialSlot = material;
    for (auto &vertex : this->polygonTriangleVertices)
      vertex.materialSlot = material;
    sourceState.lightModel = CoinRenderLightModel::BASE_COLOR;
    sourceState.materialSlot = material;
  }
  CoinRenderPolygonStyleResult resolved;
  const auto style = this->polygonStyle == SoDrawStyleElement::LINES
                         ? CoinRenderPolygonStyle::LINES
                         : CoinRenderPolygonStyle::POINTS;
  std::string diagnostic;
  if (!coin_render_prepare_polygon_style(this->polygonVertices, sourceState,
                                         this->currentPlan.materials,
                                         this->currentPlan.lightingStates[sourceState.lightingSlot],
                                         style, resolved, diagnostic,
                                         this->currentPlan.viewports[sourceState.viewportSlot], preserveDegenerateContour,
                                         &this->polygonTriangleVertices, &this->currentPlan)) {
    if (diagnostic.compare(0, 12, "UNSUPPORTED:") == 0)
      this->isUnsupported = true;
    else
      this->hasError = true;
    this->builderError = diagnostic;
    return;
  }
  if (resolved.vertices.empty())
    return;
  if (!resolved.primitiveSlopeBias.empty()) {
    const uint32_t corners = style == CoinRenderPolygonStyle::LINES ? 2u : 1u;
    if (resolved.primitiveSlopeBias.size() != resolved.primitiveMaximumDepth.size() ||
        resolved.indices.size() != resolved.primitiveSlopeBias.size() * corners) {
      this->hasError = true;
      this->builderError = "Invalid per-primitive polygon offset metadata";
      return;
    }
    const uint64_t strip = style == CoinRenderPolygonStyle::LINES
                               ? ++this->nextLineStripId : 0;
    for (size_t primitive = 0; primitive < resolved.primitiveSlopeBias.size(); ++primitive) {
      CoinRenderPolygonStyleResult partial;
      partial.state = resolved.state;
      partial.state.polygonOffsetSlopeBias = resolved.primitiveSlopeBias[primitive];
      partial.state.polygonOffsetMaxDepth = resolved.primitiveMaximumDepth[primitive];
      partial.topology = resolved.topology;
      for (uint32_t corner = 0; corner < corners; ++corner) {
        const uint32_t index = resolved.indices[primitive * corners + corner];
        if (index >= resolved.vertices.size()) {
          this->hasError = true;
          this->builderError = "Invalid per-primitive polygon offset index";
          return;
        }
        partial.vertices.push_back(resolved.vertices[index]);
        partial.indices.push_back(corner);
      }
      const uint32_t stateSlot = static_cast<uint32_t>(this->currentPlan.renderStates.size());
      this->currentPlan.renderStates.push_back(partial.state);
      this->ensureDrawPacket(partial.topology, stateSlot, this->polygonNode, true);
      this->currentPlan.draws[this->currentDrawIndex].lineStripId = strip;
      this->captureSortingCenter(action);
      CoinRenderPlanAssemblyCore::appendPolygon(this->currentPlan,
          this->currentPlan.draws[this->currentDrawIndex], partial);
    }
    return;
  }
  const uint32_t stateSlot = static_cast<uint32_t>(this->currentPlan.renderStates.size());
  this->currentPlan.renderStates.push_back(resolved.state);
  this->ensureDrawPacket(resolved.topology, stateSlot, this->polygonNode, true);
  this->captureSortingCenter(action);
  CoinRenderPlanAssemblyCore::appendPolygon(this->currentPlan,
    this->currentPlan.draws[this->currentDrawIndex], resolved);
}
bool
CoinRenderFramePlanBuilder::expandStyledPrimitives(std::string * outError)
{
  if (!coin_render_expand_strokes(this->currentPlan, this->builderError)) {
    this->hasError = true;
    if (outError) *outError = this->builderError;
    return false;
  }
  this->hasActiveDraw = false;
  return true;
}


bool
CoinRenderFramePlanBuilder::build(CoinRenderFramePlan & outPlan, std::string * outError,
                                bool transferOwnership,
                                const CoinRenderTransparencyOptions * transparency,
                                CoinRenderFramePreflight * preflight)
{
  CoinRenderPhaseTimer timer("builder_detail");
  if (preflight) preflight->invalidate();
  if (transparency) this->currentPlan.transparency = *transparency;
  if (this->polygonNode && !this->isUnsupported && !this->hasError) {
    this->isUnsupported = true;
    this->builderError = "Incomplete original polygon contour in primitive callbacks";
  }
  if (this->hasError || this->isUnsupported) {
    if (outError) *outError = this->builderError.empty() ? "Builder encountered unsupported or invalid feature" : this->builderError;
    return false;
  }
  // Resolve current attributes on the captured native occurrences, before
  // clipping/offset expansion can remove or split their transport geometry.
  // The compositor checks its own ranges and alpha inputs; full plan validation
  // follows once every consumed normal-dependent coordinate has been resolved.
  if (!this->rasterNormalEffects.empty() || !this->rasterNormalConsumers.empty()) {
    std::vector<CoinRenderCompositionItem> normalOrder;
    const bool captureOrder = !this->rasterNormalNeedsOrder && this->rasterNormalConsumers.empty();
    if (!captureOrder && !coin_render_composition_order(this->currentPlan, normalOrder,
                                                        this->builderError)) {
      this->isUnsupported = true;
      if (outError) *outError = this->builderError;
      return false;
    }
    if (!this->resolveRasterNormals(normalOrder, outError, captureOrder)) return false;
  }
  if (!this->expandStyledPrimitives(outError)) {
    return false;
  }
  timer.mark("expand_styles");
  if (!this->currentPlan.isValid(outError)) {
    return false;
  }
  if (!coin_render_resolve_triangle_depth(this->currentPlan, this->builderError)) {
    if (outError)
      *outError = this->builderError;
    return false;
  }
  timer.mark("validation");
  if (this->separateBackfaces && !coin_render_materialize_backfaces(this->currentPlan, this->builderError)) {
    this->isUnsupported = true;
    if (outError) *outError = this->builderError;
    return false;
  }
  std::vector<CoinRenderCompositionItem> order;
  std::string compositionError;
  bool opaqueIdentity = false;
  const bool publishPreflight = preflight && transferOwnership &&
    coin_render_capture_preflight_eligible(this->currentPlan);
  if (!coin_render_composition_order(this->currentPlan, order, compositionError, false,
                                     publishPreflight ? &opaqueIdentity : nullptr)) {
    this->isUnsupported = true;
    this->builderError = compositionError;
    if (outError) *outError = compositionError;
    return false;
  }
  timer.mark("composition");
  if (CoinRenderDiagnosticShell::phaseTracingEnabled()) {
    const auto & plan = this->currentPlan;
    std::fprintf(stderr, "COIN_RENDER_PHASE plan_storage vertices=%zu indices=%zu render_states=%zu draws=%zu vertex_stride=%zu vertex_bytes=%zu vertex_capacity_bytes=%zu index_bytes=%zu index_capacity_bytes=%zu render_state_bytes=%zu render_state_capacity_bytes=%zu draw_capacity_bytes=%zu capture_reserve_estimate=%zu transfer_ownership=%d cube_replay_hits=%llu cube_template_entries=%zu cube_range_entries=%zu cube_template_evictions=%llu cube_range_evictions=%llu cube_range_reuse_hits=%llu\n",
      plan.vertices.size(), plan.indices.size(), plan.renderStates.size(), plan.draws.size(),
      sizeof(CoinRenderVertexSnapshot), plan.vertices.size() * sizeof(CoinRenderVertexSnapshot),
      plan.vertices.capacity() * sizeof(CoinRenderVertexSnapshot), plan.indices.size() * sizeof(uint32_t),
      plan.indices.capacity() * sizeof(uint32_t), plan.renderStates.size() * sizeof(CoinRenderRenderStateSnapshot),
      plan.renderStates.capacity() * sizeof(CoinRenderRenderStateSnapshot),
      plan.draws.capacity() * sizeof(CoinRenderDrawPacket), this->captureReserveEstimate,
      transferOwnership ? 1 : 0, static_cast<unsigned long long>(this->cubeReplayHits),
      this->cubeGeometryCore.templateCount(), this->cubeGeometryCore.rangeCount(),
      static_cast<unsigned long long>(this->cubeGeometryCore.templateEvictionCount()),
      static_cast<unsigned long long>(this->cubeGeometryCore.rangeEvictionCount()),
      static_cast<unsigned long long>(this->cubeGeometryCore.rangeReuseCount()));
  }
  timer.mark("storage_report");
  if (transferOwnership) {
    outPlan = std::move(this->currentPlan);
    this->reset();
  } else outPlan = this->currentPlan;
  outPlan.revision = CoinRenderFramePlanBuilder::nextRevision();
  if (preflight && transferOwnership && coin_render_capture_preflight_eligible(outPlan)) {
    preflight->order = std::move(order);
    preflight->frame = &outPlan;
    preflight->revision = outPlan.revision;
    preflight->transparency = outPlan.transparency;
    preflight->opaqueIdentity = opaqueIdentity;
  }
  timer.mark("publish_plan");
  return true;
}

uint64_t
CoinRenderFramePlanBuilder::nextRevision()
{
  static std::atomic<uint64_t> revision(1);
  uint64_t value = revision.fetch_add(1, std::memory_order_relaxed);
  if (value == 0) value = revision.fetch_add(1, std::memory_order_relaxed);
  return value;
}

namespace {

CoinRenderIndexedGeometryOptions
captureIndexedGeometryOptions(SoCallbackAction * action, SoNode * node)
{
  CoinRenderIndexedGeometryOptions options;
  SoState * state = action ? action->getState() : NULL;
  options.hasTraversalState = state != NULL;
  if (!state) return options;

  const SoLazyElement * lazy = SoLazyElement::getInstance(state);
  if (lazy) options.materialCount = lazy->getNumDiffuse();

  SbVec2s textureSize;
  int textureComponents = 0;
  const unsigned char * textureBytes =
    SoMultiTextureImageElement::getImage(
      state, 0, textureSize, textureComponents);
  options.hasTexture = SoTextureQualityElement::get(state) > 0.0f &&
    SoMultiTextureEnabledElement::get(state, 0) && textureBytes != NULL &&
    textureSize[0] > 0 && textureSize[1] > 0 &&
    textureComponents > 0;
  if (options.hasTexture) {
    const SoMultiTextureCoordinateElement * coordinates =
      SoMultiTextureCoordinateElement::getInstance(state);
    if (coordinates) {
      const SoMultiTextureCoordinateElement::CoordType type =
        coordinates->getType(0);
      options.proceduralTextureCoordinates =
        type == SoMultiTextureCoordinateElement::DEFAULT ||
        type == SoMultiTextureCoordinateElement::FUNCTION;
    }
  }

  options.baseColorLighting =
    SoLightModelElement::get(state) == SoLightModelElement::BASE_COLOR;
  options.counterClockwise =
    SoShapeHintsElement::getVertexOrdering(state) !=
      SoShapeHintsElement::CLOCKWISE;
  const SbBool vrml1 = node && node->getNodeType() == SoNode::VRML1;
  options.creaseAngle = SoCreaseAngleElement::get(state, vrml1);
  return options;
}

void
publishCoreDiagnostic(const CoinRenderIndexedGeometryResult & result,
                      std::string * outError)
{
  if (outError && !result.diagnostic.empty()) {
    *outError = result.diagnostic;
  }
}

} // namespace

CoinRenderFastPathResult
CoinRenderFramePlanBuilder::processIndexedFaceSet(
  SoCallbackAction * action,
  const CoinRenderDirectGeometryView & view,
  SoNode * node,
  std::string * outError)
{
  if (!action) {
    if (outError) *outError = "Null SoCallbackAction in processIndexedFaceSet";
    return CoinRenderFastPathResult::INVALID_SCENE;
  }
  if (isShapeInvisible(action)) return CoinRenderFastPathResult::SUCCESS_PRUNE;
  if (SoBumpMapCoordinateElement::getInstance(action->getState())->getNum() > 0)
    return CoinRenderFastPathResult::FALLBACK_CONTINUE;
  const SoNode * bumpSource = SoBumpMapElement::getSource(action->getState());
  if (bumpSource && bumpSource->isOfType(SoBumpMap::getClassTypeId())) {
    SbVec2s size;
    int components = 0;
    const auto * bump = static_cast<const SoBumpMap *>(bumpSource);
    bump->image.getValue(size, components);
    if (size != SbVec2s(0, 0) || bump->filename.getValue().getLength())
      return CoinRenderFastPathResult::FALLBACK_CONTINUE;
  }
  // The callback path carries original face details; direct triangulation loses them.
  if (polygonDrawStyle(action) != SoDrawStyleElement::FILLED)
    return CoinRenderFastPathResult::FALLBACK_CONTINUE;
  int lastTextureUnit = -1;
  SoMultiTextureEnabledElement::getEnabledUnits(action->getState(), lastTextureUnit);
  if (lastTextureUnit > 0) return CoinRenderFastPathResult::FALLBACK_CONTINUE;
  if (view.positions.empty() || view.coordIndex.empty()) {
    return CoinRenderFastPathResult::SUCCESS_PRUNE;
  }
  if (this->isUnsupported) {
    if (outError) {
      *outError = this->builderError.empty()
        ? "Unsupported feature in IndexedFaceSet"
        : this->builderError;
    }
    return CoinRenderFastPathResult::UNSUPPORTED;
  }

  const CoinRenderIndexedGeometryOptions options =
    captureIndexedGeometryOptions(action, node);
  // Native shape generation owns default maps and coordinate callbacks.
  // Do not approximate them or evaluate user functions in the indexed Core.
  if (options.hasTexture && (options.proceduralTextureCoordinates || view.texcoords.empty()))
    return CoinRenderFastPathResult::FALLBACK_CONTINUE;
  CoinRenderIndexedGeometryResult transformed =
    CoinRenderIndexedGeometryCore::buildFaces(view, options);
  publishCoreDiagnostic(transformed, outError);
  if (transformed.status != CoinRenderFastPathResult::SUCCESS_PRUNE ||
      transformed.indices.empty()) {
    return transformed.status;
  }

  // Material lookup and the atomic CoinRenderFramePlan commit remain in Wiring.
  this->captureMaterial(action, 0);
  for (size_t i = 0; i < transformed.vertices.size(); ++i) {
    transformed.vertices[i].vertex.materialSlot = this->captureMaterial(
      action, transformed.vertices[i].materialIndex);
  }

  const uint32_t renderStateSlot = this->captureRenderState(action, 0);
  if (this->isUnsupported) {
    if (outError) {
      *outError = this->builderError.empty()
        ? "Unsupported feature in IndexedFaceSet"
        : this->builderError;
    }
    return CoinRenderFastPathResult::UNSUPPORTED;
  }
  this->ensureDrawPacket(
    CoinRenderPrimitiveTopology::TRIANGLE_LIST, renderStateSlot, node, true);
  this->captureSortingCenter(action);

  CoinRenderDrawPacket & packet = this->currentPlan.draws[this->currentDrawIndex];
  this->invalidateRasterCoordinates(action);
  CoinRenderPlanAssemblyCore::appendIndexed(this->currentPlan, packet, transformed);
  const uint64_t stableId = reinterpret_cast<uint64_t>(node);
  packet.stableNodeId = stableId;
  packet.drawOrdinal = this->nodeOccurrenceCount[stableId]++;
  packet.sourceRevision = CoinRenderIndexedGeometryCore::payloadDigest(
    transformed.vertices, transformed.indices);
  return CoinRenderFastPathResult::SUCCESS_PRUNE;
}

CoinRenderFastPathResult
CoinRenderFramePlanBuilder::processIndexedLineSet(
  SoCallbackAction * action,
  const CoinRenderDirectGeometryView & view,
  SoNode * node,
  std::string * outError)
{
  if (!action) {
    if (outError) *outError = "Null SoCallbackAction in processIndexedLineSet";
    return CoinRenderFastPathResult::INVALID_SCENE;
  }
  if (isShapeInvisible(action)) return CoinRenderFastPathResult::SUCCESS_PRUNE;
  int lastTextureUnit = -1;
  SoMultiTextureEnabledElement::getEnabledUnits(action->getState(), lastTextureUnit);
  if (lastTextureUnit >= 0) return CoinRenderFastPathResult::FALLBACK_CONTINUE;
  if (polygonDrawStyle(action) == SoDrawStyleElement::POINTS)
    return CoinRenderFastPathResult::FALLBACK_CONTINUE;
  if (view.positions.empty() || view.coordIndex.empty()) {
    return CoinRenderFastPathResult::SUCCESS_PRUNE;
  }
  if (this->isUnsupported) {
    if (outError) {
      *outError = this->builderError.empty()
        ? "Unsupported feature in IndexedLineSet"
        : this->builderError;
    }
    return CoinRenderFastPathResult::UNSUPPORTED;
  }

  const CoinRenderIndexedGeometryOptions options =
    captureIndexedGeometryOptions(action, node);
  if ((SoLinePatternElement::get(action->getState()) & 0xffffu) != 0xffffu)
    return CoinRenderFastPathResult::FALLBACK_CONTINUE;
  CoinRenderIndexedGeometryResult transformed =
    CoinRenderIndexedGeometryCore::buildLines(view, options);
  publishCoreDiagnostic(transformed, outError);
  if (transformed.status != CoinRenderFastPathResult::SUCCESS_PRUNE ||
      transformed.indices.empty()) {
    return transformed.status;
  }

  this->captureMaterial(action, 0);
  for (size_t i = 0; i < transformed.vertices.size(); ++i) {
    transformed.vertices[i].vertex.materialSlot = this->captureMaterial(
      action, transformed.vertices[i].materialIndex);
  }

  const uint32_t renderStateSlot = this->captureRenderState(action, 0);
  if (this->isUnsupported) {
    if (outError) {
      *outError = this->builderError.empty()
        ? "Unsupported feature in IndexedLineSet"
        : this->builderError;
    }
    return CoinRenderFastPathResult::UNSUPPORTED;
  }
  this->ensureDrawPacket(
    CoinRenderPrimitiveTopology::LINE_LIST, renderStateSlot, node, true);
  this->captureSortingCenter(action);

  CoinRenderDrawPacket & packet = this->currentPlan.draws[this->currentDrawIndex];
  this->invalidateRasterCoordinates(action);
  CoinRenderPlanAssemblyCore::appendIndexed(this->currentPlan, packet, transformed);
  const uint64_t stableId = reinterpret_cast<uint64_t>(node);
  packet.stableNodeId = stableId;
  packet.drawOrdinal = this->nodeOccurrenceCount[stableId]++;
  packet.sourceRevision = CoinRenderIndexedGeometryCore::payloadDigest(
    transformed.vertices, transformed.indices);
  return CoinRenderFastPathResult::SUCCESS_PRUNE;
}
