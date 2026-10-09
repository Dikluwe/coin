#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif
#include "rendering/coinrender/CoinRenderBoundingBoxCore.h"
#include "rendering/coinrender/CoinRenderClipCore.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderDepthCore.h"
#include "rendering/coinrender/CoinRenderDepthPolicyElement.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderFramePlanBuilder.h"
#include "rendering/coinrender/CoinRenderImageCore.h"
#include "rendering/coinrender/CoinRenderIndexedGeometryCore.h"
#include "rendering/coinrender/CoinRenderPhaseTimer.h"
#include "rendering/coinrender/CoinRenderPlanAssemblyCore.h"
#include "rendering/coinrender/CoinRenderPolygonStyleCore.h"
#include "rendering/coinrender/CoinRenderScreenRasterCore.h"
#include "rendering/coinrender/CoinRenderStrokeCore.h"
#include "rendering/coinrender/CoinRenderText2Capture.h"
#include "rendering/coinrender/CoinRenderTextureAlphaCore.h"
#include "rendering/coinrender/CoinRenderTextureCombineCore.h"
#include "rendering/coinrender/CoinRenderTextureCoordinateCore.h"
#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"
#include "shapenodes/CoinRenderMarkerBridge.h"
#include <Inventor/details/SoCylinderDetail.h>
#include <Inventor/elements/SoClipPlaneElement.h>
#include <Inventor/elements/SoDrawStyleElement.h>
#include <Inventor/elements/SoMaterialBindingElement.h>
#include <Inventor/elements/SoNormalBindingElement.h>
#include <Inventor/elements/SoNormalElement.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoCylinder.h>
#include <Inventor/nodes/SoImage.h>
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
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoLight.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoSpotLight.h>
#include <Inventor/elements/SoLightElement.h>
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
#include <cstdlib>
#include <cstring>
#include <new>


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
}

CoinRenderFramePlanBuilder::~CoinRenderFramePlanBuilder()
{
}

void
CoinRenderFramePlanBuilder::beginFrame(const SbColor4f & clearColor, const SbViewportRegion & /*viewport*/)
{
  this->reset();
  this->currentPlan.clearColor = clearColor;
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
  this->endShape();
  const char * disabledCubeTemplates = std::getenv("COIN_RENDER_DISABLE_CUBE_TEMPLATE_CACHE");
  this->cubeGeometryCore.reset(!(disabledCubeTemplates && std::strcmp(disabledCubeTemplates, "1") == 0));
  this->cubeReplayHits = 0;
  this->polygonNode = nullptr;
  this->polygonVertices.clear();
  this->polygonPositions.clear();
  this->polygonCaptured.clear();
  this->polygonTriangles = 0;
  this->currentPlan.vertices.clear();
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
  this->currentDrawIndex = 0;
  this->lineNode = nullptr;
  this->lineIndex = -1;
  this->lineStripId = this->nextLineStripId = 0;
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
  this->annotationDepthClearPending = false;
  this->builderError.clear();
  this->sceneTextures.clear();
  this->authoredTextureImages.clear(); this->compressedTextureImages.clear();
  this->authoredTextureSources.clear();
  this->nodeOccurrenceCount.clear();
  this->lightAttenuationByIndex.clear();
  this->lightCaptureScratch.clear();
}

void
CoinRenderFramePlanBuilder::beginShape(SoCallbackAction * action, const SoNode * node)
{
  this->endShape();
  if (!action || !node || polygonDrawStyle(action) != SoDrawStyleElement::FILLED) return;
  // These exact built-in generators do not modify render state between
  // triangles. Subclasses can override generation and must keep full capture.
  const SoType type = node->getTypeId();
  if (type != SoCube::getClassTypeId() && type != SoCone::getClassTypeId() &&
      type != SoCylinder::getClassTypeId() && type != SoSphere::getClassTypeId()) return;
  SoState * state = action->getState();
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
  if (this->captureCubeTemplate)
    this->cubeGeometryCore.learn(this->currentPlan, this->cubeCaptureFirstVertex,
      this->cubeCaptureFirstIndex, this->cubeCaptureFirstDraw,
      this->cubeCaptureDimensions, this->cubeCaptureNormalBinding);
  this->captureCubeTemplate = false;
  this->stableShape = nullptr;
  this->reuseCubeVertices = false;
  this->shapeRenderStates.clear();
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
    this->annotationDepthClearPending = clearDepth;
    this->hasActiveDraw = false;
  }
}

void
CoinRenderFramePlanBuilder::beginForeground()
{
  this->foregroundLayer = this->currentAnnotationLayer;
  this->currentAnnotationLayer = this->nextAnnotationLayer++;
  this->annotationDepthClearPending = false;
  this->hasActiveDraw = false;
}

void
CoinRenderFramePlanBuilder::endForeground()
{
  this->currentAnnotationLayer = this->foregroundLayer;
  this->annotationDepthClearPending = false;
  this->hasActiveDraw = false;
}

void
CoinRenderFramePlanBuilder::reserveDelayedLayers(uint32_t count)
{
  // Reserve one shared pass between scene and ordinary foreground annotations.
  for (CoinRenderDrawPacket & draw : this->currentPlan.draws)
    if (draw.renderLayer) draw.renderLayer += count;
  this->nextAnnotationLayer += count;
}

void
CoinRenderFramePlanBuilder::beginDelayedAnnotations(uint32_t layer, bool clearDepth)
{
  this->savedAnnotationLayer = 0;
  this->savedAnnotationClear = false;
  this->annotationDepth = 1;
  this->currentAnnotationLayer = layer;
  this->annotationDepthClearPending = clearDepth;
  this->hasActiveDraw = false;
}

void
CoinRenderFramePlanBuilder::endAnnotation()
{
  if (this->annotationDepth == 0) return;
  if (--this->annotationDepth == 0) {
    this->currentAnnotationLayer = this->savedAnnotationLayer;
    this->annotationDepthClearPending = this->savedAnnotationClear;
    this->hasActiveDraw = false;
  }
}

void CoinRenderFramePlanBuilder::registerSceneTexture(const unsigned char* image,
                                                      uint64_t producerId, uint32_t width,
                                                      uint32_t height, bool opaque,
                                                      int32_t transparencyFunction,CoinRenderTextureFormat format) {
  this->sceneTextures[image] =
      SceneTexture{producerId, width, height, opaque, transparencyFunction,format};
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
  }
  // The GL path fixes attenuation at the light node, before later SoEnvironment nodes.
  this->lightAttenuationByIndex[slot] = SoEnvironmentElement::getLightAttenuation(state);
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
  // Match its nearest and simage paths; the GLU fallback remains unsupported.
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
                                               bool boundingBox)
{
  const bool stable = captureTextures && this->stableShape && action->getCurPathTail() == this->stableShape;
  if (stable)
    for (const auto & cached : this->shapeRenderStates)
      if (cached.first == materialIndex) return cached.second;
  // 1. Material
  uint32_t materialSlot = this->captureMaterial(action, materialIndex, boundingBox);

  // 2. Lighting & CoinRenderLightModel
  CoinRenderLightModel lm = captureTextures ? CoinRenderLightModel::PHONG : CoinRenderLightModel::BASE_COLOR;
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
  rs.model = action->getModelMatrix();
  rs.view = camSnap.viewMatrix;
  rs.projectionCoin = camSnap.projectionMatrixCoin;
  rs.materialSlot = materialSlot;
  rs.transparentMaterial = SoLazyElement::getInstance(state)->isTransparent() != FALSE;
  rs.shadowGroupSlot = this->activeShadowGroupSlot();
  if (rs.shadowGroupSlot) rs.shadowStyle = static_cast<uint32_t>(SoShadowStyleElement::get(state));
  if (rs.shadowGroupSlot && (rs.shadowStyle & 2u))
    rs.textureProjection = CoinRenderTextureProjection::DIRECT_ST;
  rs.lightingSlot = lightingSlot;
  rs.cameraSlot = cameraSlot;
  rs.viewportSlot = viewportSlot;
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
  if (this->hasActiveShadowGroup())
    return fail("UNSUPPORTED: marker raster inside an active shadow group", true);
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
  if (traversalAlpha && (transparencyType == SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD ||
                         transparencyType == SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND))
    return fail("UNSUPPORTED: transparent markers with sorted-triangle traversal require Coin's primitive-cache policy", true);
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
  const auto source = this->currentPlan.renderStates[this->captureRenderState(action, 0, false)];
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
    bool culled = false;
    for (const auto & plane : viewPlanes) if (!plane.isInHalfSpace(world)) culled = true;
    for (const auto & plane : source.clipPlanesWorld) if (!plane.isInHalfSpace(world)) culled = true;
    if (culled) continue;
    SbVec3f projected;
    mvp.multVecMatrix(point, projected);
    const float rasterX = (projected[0] + 1.0f) * 0.5f * viewport.width - (image.width - 1) / 2;
    const float rasterY = (projected[1] + 1.0f) * 0.5f * viewport.height - (image.height - 1) / 2;
    bool visible;
    if (!CoinRenderScreenRasterCore::rasterVisible(rasterState, viewport, rasterX, rasterY,
                                                  projected[2], visible, diagnostic, false))
      return fail(diagnostic);
    if (!visible) continue;
    float xOrigin, yOrigin;
    if (!CoinRenderScreenRasterCore::markerBitmapOrigin(viewport, rasterX, rasterY,
                                                        xOrigin, yOrigin, diagnostic)) return fail(diagnostic);
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
    const uint32_t stateSlot = CoinRenderPlanAssemblyCore::state(this->currentPlan, this->renderStatesByModel, rasterState);
    for (int y = 0; y < image.height; ++y) {
      auto covered = [&](int x) { return (image.bytes[size_t(y) * image.stride + size_t(x / 8)] & (0x80u >> (x & 7))) != 0; };
      for (int x = 0; x < image.width;) {
        if (!covered(x)) { ++x; continue; }
        const int begin = x;
        while (x < image.width && covered(x)) ++x;
        if (++coveredRuns > 65536)
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
        const uint32_t first = static_cast<uint32_t>(this->currentPlan.vertices.size());
        for (auto vertex : quad.vertices) {
          vertex.fogEyeDepth = CoinRenderScreenRasterCore::planarRasterFogDepth(projected[2]);
          this->currentPlan.vertices.push_back(vertex);
        }
        for (uint32_t index : {0u,1u,2u,0u,2u,3u}) this->currentPlan.indices.push_back(first + index);
        draw.geometry.vertexCount += 4; draw.geometry.indexCount += 6;
      }
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
  if (this->hasActiveShadowGroup())
    return fail("UNSUPPORTED: screen raster nodes inside an active shadow group", true);

  // Text2 disables inherited texturing. SoImage with active inherited texture
  // state requires GL's persistent raster UV attributes, outside this profile.
  // Capture remaining state without admitting inherited texture profiles.
  const auto source = this->currentPlan.renderStates[this->captureRenderState(action, 0, false)];
  if (this->hasError || this->isUnsupported) return false;
  const uint32_t shapeFlags = SoShapeStyleElement::get(action->getState())->getFlags();
  const bool traversalAlpha = (shapeFlags &
    (SoShapeStyleElement::TRANSP_MATERIAL | SoShapeStyleElement::TRANSP_TEXTURE)) != 0;
  if (traversalAlpha && (source.transparencyType == SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD ||
                         source.transparencyType == SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND))
    return fail("UNSUPPORTED: transparent screen raster with sorted-triangle traversal requires Coin's primitive-cache policy", true);
  const auto viewport = this->currentPlan.viewports[source.viewportSlot];
  std::string diagnostic;
  CoinRenderScreenImageLayout imageLayout;
  CoinRenderText2Raster text;
  CoinRenderTextureImageSnapshot image;
  bool imageTransparent = false;
  if (isImage) {
    const auto & imageNode = *static_cast<const SoImage *>(node);
    SbVec2s size; int components = 0;
    const unsigned char * bytes = imageNode.image.getValue(size, components);
    if (!size[0] || !size[1]) return true;
    int lastUnit = -1;
    const SbBool * enabled = SoMultiTextureEnabledElement::getEnabledUnits(action->getState(), lastUnit);
    for (int unit = 0; unit <= lastUnit; ++unit)
      if (enabled[unit])
        return fail("UNSUPPORTED: SoImage with active inherited texturing requires current raster texture-coordinate capture", true);
    CoinRenderScreenRasterAnchor anchor;
    if (!CoinRenderScreenRasterCore::imageAnchor(source.model, action->getViewVolume(),
                                                viewport, anchor, diagnostic) ||
        !CoinRenderScreenRasterCore::imageLayout(anchor, viewport, size[0], size[1],
          imageNode.width.getValue(), imageNode.height.getValue(),
          imageNode.horAlignment.getValue(), imageNode.vertAlignment.getValue(),
          source.materialSlot, imageLayout, diagnostic))
      return fail(diagnostic, diagnostic.find("UNSUPPORTED:") == 0);
    if (!imageLayout.quad.visible) return true;
    if (!CoinRenderScreenRasterCore::imagePayload(bytes,
          size_t(size[0]) * size_t(size[1]) * size_t(components),
          size[0], size[1], components, image, imageTransparent, diagnostic))
      return fail(diagnostic, diagnostic.find("UNSUPPORTED:") == 0);
  } else {
    CoinRenderText2Capture::Status status;
    if (!CoinRenderText2Capture::capture(*static_cast<const SoText2 *>(node), action->getState(),
                                       text, status, diagnostic))
      return fail(diagnostic, status == CoinRenderText2Capture::Status::UNSUPPORTED);
    if (text.empty) return true;
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
  rasterState.clipPlanesWorld.clear(); // glRasterPos tests the anchor once below.
  rasterState.textureMatrix = SbMatrix::identity();
  rasterState.hasTexture = true;
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
  auto emit = [&](CoinRenderScreenRasterQuad quad, uint32_t stateSlot) {
    if (!quad.visible) return true;
    if (++rasterQuads > 65536)
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
    rasterState.transparentTexture = imageTransparent;
    rasterState.rasterTransparent = rasterState.rasterTransparent || imageTransparent;
    rasterState.textureImageSlot = CoinRenderPlanAssemblyCore::texture(this->currentPlan, std::move(image));
    const uint32_t slot = CoinRenderPlanAssemblyCore::state(this->currentPlan, this->renderStatesByModel, rasterState);
    return emit(imageLayout.quad, slot);
  }

  for (auto & pass : text.passes) {
    bool admitted;
    if (!visible(pass.pixelX, pass.pixelY, pass.depthCoin, admitted)) return false;
    if (!admitted) continue;
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
    const float rasterX = pass.mono ? std::floor(pass.pixelX) : pass.pixelX;
    const float rasterY = pass.mono ? std::floor(pass.pixelY) : pass.pixelY;
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
  }
  return true;
}

CoinRenderVertexSnapshot
CoinRenderFramePlanBuilder::captureVertex(SoCallbackAction * action, const SoPrimitiveVertex * pv,
  uint32_t materialSlot, const CoinRenderRenderStateSnapshot & rs)
{
  CoinRenderVertexSnapshot v;
  const SbVec3f & pt = pv->getPoint();
  const SbVec3f & n = pv->getNormal();
  SbVec4f tc = pv->getTextureCoords();
  SoState * state = action->getState();
  const auto * coords = SoMultiTextureCoordinateElement::getInstance(state);
  const SoType shapeType = action->getCurPathTail()->getTypeId();
  const bool canonicalGenerator = shapeType == SoCube::getClassTypeId() ||
    shapeType == SoCone::getClassTypeId() || shapeType == SoCylinder::getClassTypeId() ||
    shapeType == SoSphere::getClassTypeId();
  // Other generators already evaluated the primary callback. Preserve that
  // value, including R/Q, rather than calling a user function twice.
  if (rs.hasTexture && canonicalGenerator &&
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

  bool hasExtraCoordinates = false;
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
    for (int i = 0; i < face->getNumPoints(); ++i) matchPoint(face->getPoint(i));
  } else if (detail && detail->isOfType(SoLineDetail::getClassTypeId())) {
    const auto * line = static_cast<const SoLineDetail *>(detail);
    matchPoint(line->getPoint0()); matchPoint(line->getPoint1());
  }
  for (size_t unit = 1; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
    if (!rs.extraTextures[unit - 1].enabled) continue;
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
  uint32_t * cubeSlot = nullptr;
  if (this->reuseCubeVertices && action->getCurPathTail() == this->stableShape) {
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
  dp.frameNodeOrdinal = ++this->nodeCounter;
  dp.sourceNodeId = nodeId;
  dp.renderLayer = this->currentAnnotationLayer;
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
  if (draw.geometry.indexCount != 0 || draw.renderLayer != 0) return;
  const auto & state = this->currentPlan.renderStates[draw.renderStateSlot];
  const int type = state.transparencyType;
  if (type != SoGLRenderAction::SORTED_OBJECT_ADD && type != SoGLRenderAction::SORTED_OBJECT_BLEND &&
      type != SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD &&
      type != SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND) return;
  SoNode * node = action->getCurPathTail();
  if (!node || !node->isOfType(SoShape::getClassTypeId())) return;
  SbBox3f box; SbVec3f center;
  static_cast<SoShape *>(node)->computeBBox(action, box, center);
  if (box.isEmpty()) return;
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
  if (polygonDrawStyle(action) != SoDrawStyleElement::FILLED) {
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
      auto &draw = this->currentPlan.draws[this->currentDrawIndex];
      draw.geometry.vertexCount =
          static_cast<uint32_t>(this->currentPlan.vertices.size()) - draw.geometry.firstVertex;
      draw.geometry.indexCount += 3;
      return;
    }
  }
  if (action->getCurPathTail()->getTypeId() == SoQuadMesh::getClassTypeId()) {
    const auto mb = SoMaterialBindingElement::get(action->getState());
    const auto nb = SoNormalBindingElement::get(action->getState());
    const bool faceMaterial = mb == SoMaterialBindingElement::PER_FACE ||
                              mb == SoMaterialBindingElement::PER_FACE_INDEXED;
    const bool faceNormal =
        nb == SoNormalBindingElement::PER_FACE || nb == SoNormalBindingElement::PER_FACE_INDEXED;
    if (faceMaterial || faceNormal) {
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
      for (int c = 0; c < 3; ++c) {
        vertex.position[c] = provoking.getPoint()[c];
        vertex.normal[c] = provoking.getNormal()[c];
      }
      const uint32_t material = this->internMaterial(
          coin_render_bake_vertex_material(vertex, this->currentPlan.materials[sourceMaterial], rs,
                                           this->currentPlan.lightingStates[rs.lightingSlot]));
      rs.lightModel = CoinRenderLightModel::BASE_COLOR;
      rs.materialSlot = material;
      rsSlot = CoinRenderPlanAssemblyCore::state(this->currentPlan, this->renderStatesByModel, rs);
      this->ensureDrawPacket(CoinRenderPrimitiveTopology::TRIANGLE_LIST, rsSlot,
                             action->getCurPathTail());
      this->captureSortingCenter(action);
      for (const auto *source : {v0, v1, v2}) {
        SoPrimitiveVertex copy = *source;
        if (faceNormal)
          copy.setNormal(faceNormalValue);
        const uint32_t captured = this->addVertex(action, &copy, material);
        this->currentPlan.indices.push_back(captured);
      }
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
  const bool cube = node->getTypeId() == SoCube::getClassTypeId();
  const bool cone = node->getTypeId() == SoCone::getClassTypeId();
  const bool cylinder = node->getTypeId() == SoCylinder::getClassTypeId();
  const bool sphere = node->getTypeId() == SoSphere::getClassTypeId();
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
        for (const auto& previous : this->polygonPositions)
          if (previous == position) {
            reject("Ambiguous repeated coordinate in polygon contour");
            return;
          }
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
    // The quad's not-yet-captured fourth corner is intentionally excluded.
    const size_t available =
        quad && this->polygonTriangles == 0 ? 3 : this->polygonPositions.size();
    for (size_t j = 0; j < available; ++j)
      if (vertices[i]->getPoint() == this->polygonPositions[j]) {
        slot = j;
        break;
      }
    if (slot == this->polygonPositions.size()) {
      reject("Triangle callback lost the original face contour");
      return;
    }
    const uint32_t material = this->captureMaterial(action, vertices[i]->getMaterialIndex());
    const auto captured = this->captureVertex(action, vertices[i], material, state);
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
                                         this->currentPlan.viewports[sourceState.viewportSlot], preserveDegenerateContour)) {
    if (diagnostic.compare(0, 12, "UNSUPPORTED:") == 0)
      this->isUnsupported = true;
    else
      this->hasError = true;
    this->builderError = diagnostic;
    return;
  }
  if (resolved.vertices.empty())
    return;
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
  CoinRenderPlanAssemblyCore::appendIndexed(this->currentPlan, packet, transformed);
  const uint64_t stableId = reinterpret_cast<uint64_t>(node);
  packet.stableNodeId = stableId;
  packet.drawOrdinal = this->nodeOccurrenceCount[stableId]++;
  packet.sourceRevision = CoinRenderIndexedGeometryCore::payloadDigest(
    transformed.vertices, transformed.indices);
  return CoinRenderFastPathResult::SUCCESS_PRUNE;
}
