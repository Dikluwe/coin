#include "actions/CoinRenderActionIncludesP.h"

SoCallbackAction::Response
CoinRenderActionP::textureImagePreCB(void * userdata, SoCallbackAction *, const SoNode * node)
{
  const auto * texture=static_cast<const SoTexture2 *>(node);
  SbVec2s size;int components;
  const auto * bytes=texture->image.getValue(size,components);
  // Distinguish a genuinely authored white 2x2 luminance image from Coin's
  // static pending-filename dummy. Reading this field does not traverse/mutate
  // the node or affect pre-callback PRUNE/override behavior.
  if(size[0]>0 && size[1]>0 && components>0)
    static_cast<CoinRenderActionP *>(userdata)->builder.registerAuthoredTextureImage(
      bytes, texture->enableCompressedTexture.getValue(), texture,
      texture->getImageUploadRevision());
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::textureVolumePreCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  const auto * texture = static_cast<const SoTexture3 *>(node);
  SoState * state = action->getState();
  const int unit = SoTextureUnitElement::get(state);
  if (unit == 0 && SoTextureOverrideElement::getImageOverride(state))
    return SoCallbackAction::CONTINUE;
  SbVec3s size;
  int components = 0;
  const auto * bytes = texture->images.getValue(size, components);
  const bool pending = size == SbVec3s(0, 0, 0) && texture->images.isDefault() &&
      texture->filenames.getNum() > 0 && texture->filenames[0].getLength() != 0;
  // SoTexture3::doAction does not set the enabled element for callbacks.
  // Preserve the dimensional mode explicitly: SoMultiTextureImageElement also
  // stores ordinary 2D images with a depth of one.
  if (size != SbVec3s(0, 0, 0) || pending)
    SoMultiTextureEnabledElement::enableTexture3(state, const_cast<SoTexture3 *>(texture), unit);
  else
    SoMultiTextureEnabledElement::set(state, const_cast<SoTexture3 *>(texture), unit, FALSE);
  if (size[0] > 0 && size[1] > 0 && size[2] > 0 && components > 0)
    static_cast<CoinRenderActionP *>(userdata)->builder.registerAuthoredVolumeImage(
      bytes, texture->enableCompressedTexture.getValue(), texture, texture->getNodeId());
  return SoCallbackAction::CONTINUE;
}

static bool validShadowMapViewport(const SbViewportRegion &viewport, std::string &diagnostic)
{
  const auto &origin = viewport.getViewportOrigin();
  const auto &extent = viewport.getViewportSize();
  const auto &window = viewport.getWindowSize();
  for (unsigned axis = 0; axis < 2; ++axis) {
    const double pixelOrigin = double(origin[axis]) * window[axis];
    const double pixelExtent = double(extent[axis]) * window[axis];
    if (window[axis] <= 0 || !std::isfinite(pixelOrigin) || !std::isfinite(pixelExtent) ||
        extent[axis] < 0.0f || std::round(pixelOrigin) < std::numeric_limits<short>::min() ||
        std::round(pixelOrigin) > std::numeric_limits<short>::max() ||
        std::round(pixelExtent) > std::numeric_limits<short>::max()) {
      diagnostic =
          "shadowMapScene viewport is non-finite, negative or exceeds SbViewportRegion pixels";
      return false;
    }
  }
  return true;
}

SoCallbackAction::Response
CoinRenderActionP::textureUnitsPreCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  // PRUNE skips this shape only; subsequent state nodes still traverse.
  if (CoinRenderFramePlanBuilder::isShapeInvisible(action)) return SoCallbackAction::PRUNE;
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->capturingShadowScene) {
    std::string diagnostic;
    if (!validShadowMapViewport(action->getViewportRegion(), diagnostic)) {
      p->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
                                                         CoinRenderDiagnosticDomain::FRAME_PLAN,
                                                         SbString(diagnostic.c_str())));
      return SoCallbackAction::ABORT;
    }
    const auto extent = action->getViewportRegion().getViewportSizePixels();
    if (!extent[0] || !extent[1])
      return SoCallbackAction::PRUNE;
  }
  if (node->getTypeId() == SoImage::getClassTypeId() ||
      node->getTypeId() == SoText2::getClassTypeId() ||
      node->getTypeId() == SoMarkerSet::getClassTypeId() ||
      node->getTypeId() == SoIndexedMarkerSet::getClassTypeId()) {
    p->builder.endShape();
    return SoCallbackAction::CONTINUE; // The action method captures after all pre callbacks.
  }
  SoState * state = action->getState();
  if (!p->capturingShadowScene && (SoShapeStyleElement::get(state)->getFlags() &
      SoShapeStyleElement::BBOXCMPLX)) return SoCallbackAction::CONTINUE;
  int last = -1;
  const SbBool * enabled = SoMultiTextureEnabledElement::getEnabledUnits(state, last);
  if (last >= static_cast<int>(COIN_RENDER_MAX_TEXTURE_UNITS)) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString("At most eight texture units are supported")));
    return SoCallbackAction::ABORT;
  }
  if (SoTextureQualityElement::get(state) > 0.0f) {
    for (int unit = 0; unit <= last; ++unit) {
      if (enabled[unit] && SoMultiTextureCoordinateElement::getType(state, unit) ==
          SoMultiTextureCoordinateElement::TEXGEN) {
        p->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("Hardware-only texture generators require a CPU coordinate callback")));
        return SoCallbackAction::ABORT;
      }
    }
  }
  p->builder.endShape();
  p->beginTranslationShape(action, node);
  // The installed capture callbacks include multiple inherited pre/post
  // observers. Their exact registration revision, rather than a single-list
  // count, proves that no external observer can be skipped by cube replay.
  // Restrict this admission to the concrete native generator: a C++ subclass
  // may override callback/generation while retaining Cube's runtime SoType.
  const bool internalCubeCapture = p->fastPathEnabled &&
    node->getTypeId() == SoCube::getClassTypeId() &&
    typeid(*node) == typeid(SoCube) &&
    p->master->callbackRegistrationRevision() == p->captureCallbackRevision;
  if (internalCubeCapture || p->master->hasSingleShapeCallbacks(node->getTypeId())) {
    p->builder.beginShape(action, node);
    if (p->fastPathEnabled && p->master->callbackRegistrationRevision() == p->captureCallbackRevision &&
        p->builder.replayNativeCube(action, const_cast<SoNode *>(node))) {
      p->endTranslationShape();
      p->builder.endShape();
      return SoCallbackAction::PRUNE;
    }
  }
  return SoCallbackAction::CONTINUE;
}

bool
CoinRenderActionP::captureBoundingBox(SoCallbackAction * action, SoNode * node)
{
  if (this->capturingShadowScene || !(SoShapeStyleElement::get(action->getState())->getFlags() &
      SoShapeStyleElement::BBOXCMPLX)) return false;
  // Match native returns that precede shouldGLRender, even if an otherwise
  // nonempty coordinate element could supply a box.
  if ((node->isOfType(SoIndexedFaceSet::getClassTypeId()) &&
       static_cast<SoIndexedFaceSet *>(node)->coordIndex.getNum() < 3) ||
      (node->isOfType(SoIndexedLineSet::getClassTypeId()) &&
       static_cast<SoIndexedLineSet *>(node)->coordIndex.getNum() < 2) ||
      (node->isOfType(SoIndexedPointSet::getClassTypeId()) &&
       static_cast<SoIndexedPointSet *>(node)->coordIndex.getNum() == 0) ||
      (node->isOfType(SoIndexedMarkerSet::getClassTypeId()) &&
       static_cast<SoIndexedMarkerSet *>(node)->coordIndex.getNum() == 0) ||
      (node->isOfType(SoPointSet::getClassTypeId()) &&
       static_cast<SoPointSet *>(node)->numPoints.getValue() == 0) ||
      (node->isOfType(SoFaceSet::getClassTypeId()) &&
       static_cast<SoFaceSet *>(node)->numVertices.getNum() == 1 &&
       static_cast<SoFaceSet *>(node)->numVertices[0] == 0)) return true;
  if (node->isOfType(SoImage::getClassTypeId())) {
    SbVec2s size; int components = 0;
    const auto * bytes = static_cast<SoImage *>(node)->image.getValue(size, components);
    if (!bytes || !size[0] || !size[1]) return true;
  }
  SoState * state = action->getState();
  state->push();
  const bool marker = node->isOfType(SoMarkerSet::getClassTypeId());
  if (marker) {
    SoLazyElement::setLightModel(state, SoLazyElement::BASE_COLOR);
    SoMultiTextureEnabledElement::disableAll(state);
  }
  // PointSet reads VP coordinates in computeBBox but applies its material
  // only after shouldGLRender. Other vertex shapes apply the property first.
  if (node->isOfType(SoVertexShape::getClassTypeId()) &&
      (!node->isOfType(SoPointSet::getClassTypeId()) || marker)) {
    auto * property = static_cast<SoVertexShape *>(node)->vertexProperty.getValue();
    if (property) property->doAction(action);
  }
  const bool success = this->builder.captureBoundingBox(action, node);
  state->pop();
  if (!success) this->master->setTerminated(TRUE);
  return true;
}

void
CoinRenderActionP::observeBoundingBox(SoCallbackAction * action, SoNode * node)
{
  if (action->hasTerminated() || action->hasSingleShapeCallbacks(node->getTypeId())) return;
  const bool marker = node->isOfType(SoMarkerSet::getClassTypeId()) ||
                      node->isOfType(SoIndexedMarkerSet::getClassTypeId());
  SoState * state = action->getState();
  struct ObserverScope {
    CoinRenderActionP & owner;
    SoState * markerState;
    bool previous;
    ~ObserverScope() {
      if (markerState) markerState->pop();
      owner.boundingBoxObservers = previous;
    }
  } scope{*this, marker ? state : nullptr, this->boundingBoxObservers};
  if (marker) {
    state->push();
    SoLazyElement::setLightModel(state, SoLazyElement::BASE_COLOR);
    SoMultiTextureEnabledElement::disableAll(state);
  }
  this->boundingBoxObservers = true;
  node->callback(action);
}

namespace {
void normalizeIndexedBindings(const SoNode * node, CoinRenderDirectGeometryView & view)
{
  // VRML1 PER_VERTEX means per coordinate, even when normalIndex is authored.
  // Both native indexed shapes replace that index stream with coordIndex.
  if (node->getNodeType() != SoNode::VRML1) return;
  if (view.normalBinding == SoNormalBindingElement::PER_VERTEX) {
    view.normalBinding = SoNormalBindingElement::PER_VERTEX_INDEXED;
    view.normalIndex = view.coordIndex;
  }
  // The native material binding has the same VRML1 coordinate-index rule;
  // an authored materialIndex is idle in this binding.
  if (view.materialBinding == SoMaterialBindingElement::PER_VERTEX) {
    view.materialBinding = SoMaterialBindingElement::PER_VERTEX_INDEXED;
    view.materialIndex = view.coordIndex;
  }
}

#ifdef HAVE_VRML97
// Validate only arrays dereferenced by native primitive generation. In
// particular unused index streams and light radius/beamWidth are not inputs.
bool validateVRMLGeometry(SoCallbackAction * action, SoNode * node, std::string & error)
{
  auto fail = [&]() { error = "Invalid consumed VRML geometry array, index or numeric value"; return false; };
  auto finite3 = [](const SbVec3f & v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
  };
  auto vectorNode = [&](const SoSFNode & field, SoType type) {
    return !field.getValue() || field.getValue()->isOfType(type);
  };
  auto indices = [&](const SoMFInt32 & stream, const SoVRMLCoordinate * coord) {
    for (int i = 0; i < stream.getNum(); ++i) {
      const int index = stream[i];
      if (index < -1 || (index >= 0 && (!coord || index >= coord->point.getNum() ||
                                      !finite3(coord->point[index])))) return false;
    }
    return true;
  };
  auto attribute = [&](const SoMFInt32 & coordinates, const SoMFInt32 & explicitIndices,
                       int count, bool perVertex, bool explicitUsed, auto finiteAt) {
    int part = 0;
    bool inPart = false;
    for (int i = 0; i < coordinates.getNum(); ++i) {
      if (coordinates[i] == -1) { if (inPart) ++part; inPart = false; continue; }
      int index = perVertex ? coordinates[i] : part;
      const int offset = perVertex ? i : part;
      if (explicitUsed) {
        if (offset >= explicitIndices.getNum()) return false;
        index = explicitIndices[offset];
      }
      if (index < 0 || index >= count || !finiteAt(index)) return false;
      inPart = true;
    }
    return true;
  };
  if (node->isOfType(SoVRMLBox::getClassTypeId())) {
    if (!finite3(static_cast<const SoVRMLBox *>(node)->size.getValue())) return fail();
  } else if (node->isOfType(SoVRMLSphere::getClassTypeId())) {
    if (!std::isfinite(static_cast<const SoVRMLSphere *>(node)->radius.getValue())) return fail();
  } else if (node->isOfType(SoVRMLCone::getClassTypeId())) {
    const auto * shape = static_cast<const SoVRMLCone *>(node);
    if ((shape->side.getValue() || shape->bottom.getValue()) &&
        (!std::isfinite(shape->bottomRadius.getValue()) || !std::isfinite(shape->height.getValue()))) return fail();
  } else if (node->isOfType(SoVRMLCylinder::getClassTypeId())) {
    const auto * shape = static_cast<const SoVRMLCylinder *>(node);
    if ((shape->side.getValue() || shape->bottom.getValue() || shape->top.getValue()) &&
        (!std::isfinite(shape->radius.getValue()) || !std::isfinite(shape->height.getValue()))) return fail();
  } else if (node->isOfType(SoVRMLText::getClassTypeId())) {
    const auto * shape = static_cast<const SoVRMLText *>(node);
    if (!shape->string.getNum()) return true;
    if (!std::isfinite(shape->maxExtent.getValue())) return fail();
    for (int i = 0; i < shape->length.getNum() && i < shape->string.getNum(); ++i)
      if (!std::isfinite(shape->length[i])) return fail();
    if (!vectorNode(shape->fontStyle, SoVRMLFontStyle::getClassTypeId())) return fail();
    if (const auto * font = static_cast<const SoVRMLFontStyle *>(shape->fontStyle.getValue()))
      if (!std::isfinite(font->size.getValue()) || !std::isfinite(font->spacing.getValue())) return fail();
  } else if (node->isOfType(SoVRMLIndexedFaceSet::getClassTypeId())) {
    const auto * shape = static_cast<const SoVRMLIndexedFaceSet *>(node);
    if (shape->coordIndex.getNum() < 3) return true;
    if (!vectorNode(shape->coord, SoVRMLCoordinate::getClassTypeId()) ||
        !vectorNode(shape->color, SoVRMLColor::getClassTypeId()) ||
        !vectorNode(shape->normal, SoVRMLNormal::getClassTypeId()) ||
        !vectorNode(shape->texCoord, SoVRMLTextureCoordinate::getClassTypeId())) return fail();
    const auto * coord = static_cast<const SoVRMLCoordinate *>(shape->coord.getValue());
    if (!coord) return true;
    if (!indices(shape->coordIndex, coord)) return fail();
    if (const auto * color = static_cast<const SoVRMLColor *>(shape->color.getValue())) {
      const bool explicitUsed = shape->colorIndex.getNum() && shape->colorIndex[0] >= 0;
      if (!attribute(shape->coordIndex, shape->colorIndex, color->color.getNum(),
          shape->colorPerVertex.getValue(), explicitUsed,
          [&](int i) { return finite3(color->color[i]); })) return fail();
    }
    if (const auto * normal = static_cast<const SoVRMLNormal *>(shape->normal.getValue())) {
      if (normal->vector.getNum()) {
        const bool explicitUsed = shape->normalIndex.getNum() && shape->normalIndex[0] >= 0;
        if (!attribute(shape->coordIndex, shape->normalIndex, normal->vector.getNum(),
            shape->normalPerVertex.getValue(), explicitUsed,
            [&](int i) { return finite3(normal->vector[i]); })) return fail();
      }
    }
    if (SoMultiTextureEnabledElement::get(action->getState(), 0))
      if (const auto * texture = static_cast<const SoVRMLTextureCoordinate *>(shape->texCoord.getValue())) {
        const bool explicitUsed = shape->texCoordIndex.getNum() && shape->texCoordIndex[0] >= 0;
        if (!attribute(shape->coordIndex, shape->texCoordIndex, texture->point.getNum(), true,
            explicitUsed, [&](int i) { return std::isfinite(texture->point[i][0]) &&
                                            std::isfinite(texture->point[i][1]); })) return fail();
      }
    if (!std::isfinite(shape->creaseAngle.getValue())) return fail();
  } else if (node->isOfType(SoVRMLIndexedLineSet::getClassTypeId())) {
    const auto * shape = static_cast<const SoVRMLIndexedLineSet *>(node);
    if (shape->coordIndex.getNum() < 2) return true;
    if (!vectorNode(shape->coord, SoVRMLCoordinate::getClassTypeId()) ||
        !vectorNode(shape->color, SoVRMLColor::getClassTypeId())) return fail();
    const auto * coord = static_cast<const SoVRMLCoordinate *>(shape->coord.getValue());
    if (!coord) return true;
    if (!indices(shape->coordIndex, coord)) return fail();
    // Empty sections are legal for faces but native VRML line callbacks assert
    // when their first index is negative. Do not pass malformed strips through.
    for (int i = 0; i + 1 < shape->coordIndex.getNum(); ++i)
      if (shape->coordIndex[i] < 0 && (i == 0 || shape->coordIndex[i - 1] < 0)) return fail();
    if (const auto * color = static_cast<const SoVRMLColor *>(shape->color.getValue()))
      if (!attribute(shape->coordIndex, shape->colorIndex, color->color.getNum(),
          shape->colorPerVertex.getValue(), shape->colorIndex.getNum() != 0,
          [&](int i) { return finite3(color->color[i]); })) return fail();
  } else if (node->isOfType(SoVRMLPointSet::getClassTypeId())) {
    const auto * shape = static_cast<const SoVRMLPointSet *>(node);
    if (!vectorNode(shape->coord, SoVRMLCoordinate::getClassTypeId()) ||
        !vectorNode(shape->color, SoVRMLColor::getClassTypeId())) return fail();
    const auto * coord = static_cast<const SoVRMLCoordinate *>(shape->coord.getValue());
    if (!coord) return true;
    const auto * color = static_cast<const SoVRMLColor *>(shape->color.getValue());
    for (int i = 0; i < coord->point.getNum(); ++i)
      if (!finite3(coord->point[i]) || (color && (i >= color->color.getNum() ||
                                               !finite3(color->color[i])))) return fail();
  } else if (node->isOfType(SoVRMLElevationGrid::getClassTypeId())) {
    const auto * shape = static_cast<const SoVRMLElevationGrid *>(node);
    const int64_t x = shape->xDimension.getValue(), z = shape->zDimension.getValue();
    if (x < 2 || z < 2) return true;
    const int64_t points = x * z, faces = (x - 1) * (z - 1);
    if (points > INT32_MAX || points > shape->height.getNum() ||
        !std::isfinite(shape->xSpacing.getValue()) || !std::isfinite(shape->zSpacing.getValue())) return fail();
    for (int i = 0; i < points; ++i) if (!std::isfinite(shape->height[i])) return fail();
    if (!vectorNode(shape->color, SoVRMLColor::getClassTypeId()) ||
        !vectorNode(shape->normal, SoVRMLNormal::getClassTypeId()) ||
        !vectorNode(shape->texCoord, SoVRMLTextureCoordinate::getClassTypeId())) return fail();
    if (const auto * colors = static_cast<const SoVRMLColor *>(shape->color.getValue())) {
      const int64_t count = shape->colorPerVertex.getValue() ? points : faces;
      if (colors->color.getNum() < count) return fail();
      for (int i = 0; i < count; ++i) if (!finite3(colors->color[i])) return fail();
    }
    if (SoLazyElement::getLightModel(action->getState()) != SoLazyElement::BASE_COLOR)
      if (const auto * normals = static_cast<const SoVRMLNormal *>(shape->normal.getValue())) {
        const int64_t count = shape->normalPerVertex.getValue() ? points : faces;
        if (normals->vector.getNum() < count) return fail();
        for (int i = 0; i < count; ++i) if (!finite3(normals->vector[i])) return fail();
      }
    if (const auto * texture = static_cast<const SoVRMLTextureCoordinate *>(shape->texCoord.getValue())) {
      if (texture->point.getNum() < points) return fail();
      for (int i = 0; i < points; ++i)
        if (!std::isfinite(texture->point[i][0]) || !std::isfinite(texture->point[i][1])) return fail();
    }
  } else if (node->isOfType(SoVRMLExtrusion::getClassTypeId())) {
    const auto * shape = static_cast<const SoVRMLExtrusion *>(node);
    if (!shape->spine.getNum() || !shape->crossSection.getNum()) return true;
    if (shape->spine.getNum() < 2) return fail();
    for (int i = 0; i < shape->spine.getNum(); ++i) if (!finite3(shape->spine[i])) return fail();
    for (int i = 0; i < shape->crossSection.getNum(); ++i)
      if (!std::isfinite(shape->crossSection[i][0]) || !std::isfinite(shape->crossSection[i][1])) return fail();
    for (int i = 0; i < shape->scale.getNum() && i < shape->spine.getNum(); ++i)
      if (!std::isfinite(shape->scale[i][0]) || !std::isfinite(shape->scale[i][1])) return fail();
    for (int i = 0; i < shape->orientation.getNum() && i < shape->spine.getNum(); ++i)
      for (int c = 0; c < 4; ++c) if (!std::isfinite(shape->orientation[i].getValue()[c])) return fail();
    if (!std::isfinite(shape->creaseAngle.getValue())) return fail();
  }
  return true;
}
#endif

bool validateIndexedShapeGeometry(SoCallbackAction * action, SoNode * node,
                                 std::string & diagnostic)
{
  if (!node->isOfType(SoIndexedShape::getClassTypeId())) return true;
  const auto * shape = static_cast<const SoIndexedShape *>(node);
  const bool faces = node->isOfType(SoIndexedFaceSet::getClassTypeId());
  const bool lines = node->isOfType(SoIndexedLineSet::getClassTypeId());
  SoState * state = action->getState();
  state->push();
  struct StateScope { SoState * state; ~StateScope() { state->pop(); } } scope{state};
  // Match generatePrimitives: vertexProperty is applied before its arrays and
  // binding are read, including inherited override rules.
  if (shape->vertexProperty.getValue()) shape->vertexProperty.getValue()->doAction(action);
  const auto * coordinates = SoCoordinateElement::getInstance(state);
  const int count = coordinates ? coordinates->getNum() : 0;
  // Native primitive generation dereferences these indices before the builder
  // can validate emitted vertices. Validate the current fields after every
  // pre-callback, including the selected LINE replay and native fallbacks.
  for (int i = 0; i < shape->coordIndex.getNum(); ++i) {
    const int32_t index = shape->coordIndex[i];
    if (index < -1 || (index >= 0 && index >= count)) {
      diagnostic = "Indexed shape coordinate index out of bounds or invalid";
      return false;
    }
    if (index < 0) continue;
    if (coordinates->is3D()) {
      const SbVec3f & value = coordinates->get3(index);
      for (int component = 0; component < 3; ++component)
        if (!std::isfinite(value[component])) {
          diagnostic = "Non-finite consumed indexed shape coordinate";
          return false;
        }
    } else {
      const SbVec4f & value = coordinates->get4(index);
      for (int component = 0; component < 4; ++component)
        if (!std::isfinite(value[component])) {
          diagnostic = "Non-finite consumed indexed shape coordinate";
          return false;
        }
    }
  }
  if (!faces && !lines) return true;
  const auto * normals = SoNormalElement::getInstance(state);
  if (!normals || normals->getNum() == 0) return true;
  CoinRenderDirectGeometryView view;
  // Normal bounds depend on geometry presence, not coordinate values/dimension.
  view.positions = CoinRenderSpan<SbVec3f>(nullptr,
    size_t(SoCoordinateElement::getInstance(state)->getNum()));
  view.normals = CoinRenderSpan<SbVec3f>(normals->getArrayPtr(), size_t(normals->getNum()));
  view.coordIndex = CoinRenderSpan<int32_t>(shape->coordIndex.getValues(0), size_t(shape->coordIndex.getNum()));
  view.normalIndex = CoinRenderSpan<int32_t>(shape->normalIndex.getValues(0), size_t(shape->normalIndex.getNum()));
  view.normalBinding = SoNormalBindingElement::get(state);
  normalizeIndexedBindings(shape, view);
  return faces ? CoinRenderIndexedGeometryCore::validateFaceNormals(view, diagnostic)
               : CoinRenderIndexedGeometryCore::validateLineNormals(view, diagnostic);
}
}

bool CoinRenderActionP::acceptNode(SoNode * node)
{
  if (this->master->hasTerminated()) return false;
  this->master->setCurrentNode(node);
  if (this->master->abortNow()) return false;
  if (node->isOfType(SoShape::getClassTypeId())) this->builder.beginSortingOccurrence();
  return true;
}
void CoinRenderActionP::portableNodeMethod(SoAction * action, SoNode * node)
{
  auto * render = static_cast<CoinRenderAction *>(action);
  if (render->getPimpl()->technicalRttReplay && node->isOfType(SoCallback::getClassTypeId())) return;
  if (!render->pimpl->acceptNode(node)) return;
  SoNode::callbackS(action, node);
}

void
CoinRenderActionP::shapeContentMethod(SoAction * action, SoNode * node)
{
  auto * render = static_cast<CoinRenderAction *>(action);
  if (!render->pimpl->acceptNode(node)) return;
  render->invokePreCallbacks(node);
  if (render->getCurrentResponse() == SoCallbackAction::CONTINUE) {
#ifdef HAVE_VRML97
    if (node->getNodeType() == SoNode::VRML2 && !CoinRenderFramePlanBuilder::isShapeInvisible(render)) {
      std::string error;
      if (!validateVRMLGeometry(render, node, error)) {
        render->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(error.c_str())));
        render->setTerminated(TRUE);
        render->invokePostCallbacks(node);
        return;
      }
    }
#endif
#ifdef HAVE_VRML97
    const bool emptyVRMLFace = node->isOfType(SoVRMLIndexedFaceSet::getClassTypeId()) &&
      !static_cast<SoVRMLIndexedFaceSet *>(node)->coord.getValue();
    const bool emptyVRMLLine = node->isOfType(SoVRMLIndexedLineSet::getClassTypeId()) &&
      !static_cast<SoVRMLIndexedLineSet *>(node)->coord.getValue();
    if (emptyVRMLFace || emptyVRMLLine) { render->invokePostCallbacks(node); return; }
#endif
    const bool box = render->pimpl->captureBoundingBox(render, node);
    // All pre-callbacks have now run. Guard the native fallback as well as
    // subclasses and LINE replay, which intentionally bypass direct capture.
    const bool generatesPrimitives = !box || !render->hasSingleShapeCallbacks(node->getTypeId());
    std::string diagnostic;
    if (generatesPrimitives && !validateIndexedShapeGeometry(render, node, diagnostic)) {
      render->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString(diagnostic.c_str())));
      render->setTerminated(TRUE);
    } else if (!box) {
#ifdef HAVE_VRML97
      SoState * state = render->getState();
      const bool vrml = node->getNodeType() == SoNode::VRML2;
      uint32_t packed = 0;
      if (vrml) {
        state->push();
        SbBool ccw = TRUE, solid = TRUE;
        bool installHints = false;
        if (node->isOfType(SoVRMLIndexedFaceSet::getClassTypeId())) {
          installHints = true;
          auto * shape = static_cast<SoVRMLIndexedFaceSet *>(node);
          ccw = shape->ccw.getValue(); solid = shape->solid.getValue();
        } else if (node->isOfType(SoVRMLElevationGrid::getClassTypeId())) {
          installHints = true;
          auto * shape = static_cast<SoVRMLElevationGrid *>(node);
          ccw = shape->ccw.getValue(); solid = shape->solid.getValue();
          if (shape->color.getValue()) shape->color.getValue()->doAction(render);
        } else if (node->isOfType(SoVRMLExtrusion::getClassTypeId())) {
          installHints = true;
          auto * shape = static_cast<SoVRMLExtrusion *>(node);
          ccw = shape->ccw.getValue(); solid = shape->solid.getValue();
        }
        if (installHints && !(SoOverrideElement::getFlags(state) & SoOverrideElement::SHAPE_HINTS))
          SoShapeHintsElement::set(state, node,
            ccw ? SoShapeHintsElement::COUNTERCLOCKWISE : SoShapeHintsElement::CLOCKWISE,
            solid ? SoShapeHintsElement::SOLID : SoShapeHintsElement::UNKNOWN_SHAPE_TYPE,
            SoShapeHintsElement::FACE_TYPE_AS_IS);
        const bool line = node->isOfType(SoVRMLIndexedLineSet::getClassTypeId());
        const bool point = node->isOfType(SoVRMLPointSet::getClassTypeId());
        if (line || point) {
          SoLazyElement::setLightModel(state, SoLazyElement::BASE_COLOR);
          SoMultiTextureEnabledElement::disableAll(state);
          SoNode * colors = line ? static_cast<SoVRMLIndexedLineSet *>(node)->color.getValue()
                                 : static_cast<SoVRMLPointSet *>(node)->color.getValue();
          if (!colors) {
            SbColor color = SoLazyElement::getEmissive(state);
            if (line) color += SoLazyElement::getDiffuse(state, 0);
            for (int c = 0; c < 3; ++c) color[c] = SbClamp(color[c], 0.0f, 1.0f);
            packed = color.getPackedValue();
            SoLazyElement::setPacked(state, node, 1, &packed, FALSE);
          } else if (point) colors->doAction(render);
        }
      }
      node->callback(render);
      if (vrml) state->pop();
#else
      node->callback(render);
#endif
    } else render->pimpl->observeBoundingBox(render, node);
  }
  render->invokePostCallbacks(node);
}

// VRML lights and fog have GL-only implementations in native Coin. Keep
// their interpretation here, scoped by the same Coin state stack as geometry.
void
CoinRenderActionP::vrmlStateMethod(SoAction * base, SoNode * node)
{
#ifdef HAVE_VRML97
  auto * action = static_cast<CoinRenderAction *>(base);
  if (!action->pimpl->acceptNode(node)) return;
  action->invokePreCallbacks(node);
  if (action->getCurrentResponse() == SoCallbackAction::CONTINUE) {
    SoState * state = action->getState();
    if (node->isOfType(SoVRMLMaterial::getClassTypeId())) {
      const auto * material = static_cast<const SoVRMLMaterial *>(node);
      bool finite = true;
      const uint32_t flags = SoOverrideElement::getFlags(state);
      const auto colorFinite = [&](const SoSFColor & field, uint32_t mask) {
        if (field.isIgnored() || (flags & mask)) return true;
        const SbColor & value = field.getValue();
        return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
      };
      finite = colorFinite(material->diffuseColor, SoOverrideElement::DIFFUSE_COLOR) &&
        colorFinite(material->emissiveColor, SoOverrideElement::EMISSIVE_COLOR) &&
        colorFinite(material->specularColor, SoOverrideElement::SPECULAR_COLOR);
      if (!material->diffuseColor.isIgnored() && !material->ambientIntensity.isIgnored() &&
          !(flags & SoOverrideElement::AMBIENT_COLOR))
        finite = finite && std::isfinite(material->ambientIntensity.getValue());
      if (!material->transparency.isIgnored() && !(flags & SoOverrideElement::TRANSPARENCY))
        finite = finite && std::isfinite(material->transparency.getValue());
      if (!material->shininess.isIgnored() && !(flags & SoOverrideElement::SHININESS))
        finite = finite && std::isfinite(material->shininess.getValue());
      if (!finite) {
        action->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("Non-finite consumed VRML material")));
        action->setTerminated(TRUE);
      } else node->callback(action);
    } else if (node->isOfType(SoVRMLLight::getClassTypeId())) {
      action->pimpl->builder.recordVRMLLight(action, node);
    } else if (node->isOfType(SoVRMLPixelTexture::getClassTypeId()) ||
               node->isOfType(SoVRMLImageTexture::getClassTypeId())) {
      node->callback(action);
      const int unit = SoTextureUnitElement::get(state);
      if (unit >= 0 && unit < int(COIN_RENDER_MAX_TEXTURE_UNITS) &&
          SoMultiTextureEnabledElement::get(state, unit)) {
        SbVec2s size;
        int components;
        SoMultiTextureImageElement::Wrap s, t;
        SoMultiTextureImageElement::Model model;
        SbColor blend;
        const unsigned char * bytes = SoMultiTextureImageElement::get(
          state, unit, size, components, s, t, model, blend);
        const auto * texture = static_cast<const SoVRMLTexture *>(node);
        // Native VRML GL uses CLAMP_TO_EDGE for repeat=false; generic VRML
        // callbacks instead install CLAMP_TO_BORDER, which is not the GL model.
        SoMultiTextureImageElement::set(state, node, unit, size, components, bytes,
          texture->repeatS.getValue() ? SoMultiTextureImageElement::REPEAT : SoMultiTextureImageElement::CLAMP,
          texture->repeatT.getValue() ? SoMultiTextureImageElement::REPEAT : SoMultiTextureImageElement::CLAMP,
          model, blend);
        bool authored = node->isOfType(SoVRMLPixelTexture::getClassTypeId());
        if (!authored) {
          const auto * image = static_cast<const SoVRMLImageTexture *>(node)->getImage();
          authored = image && image->hasData();
        }
        if (authored) action->pimpl->builder.registerAuthoredTextureImage(bytes, false, nullptr, 0);
      }
    } else if (node->isOfType(SoVRMLBackground::getClassTypeId())) {
      auto * background = static_cast<SoVRMLBackground *>(node);
      bool finite = true;
      for (const auto * colors : {&background->skyColor, &background->groundColor})
        for (int i = 0; i < colors->getNum(); ++i)
          for (int c = 0; c < 3; ++c) finite = finite && std::isfinite((*colors)[i][c]);
      for (const auto * angles : {&background->skyAngle, &background->groundAngle})
        for (int i = 0; i < angles->getNum(); ++i) finite = finite && std::isfinite((*angles)[i]);
      if (!finite || (background->skyAngle.getNum() && !background->skyColor.getNum()) ||
          (background->groundAngle.getNum() && !background->groundColor.getNum())) {
        action->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("Invalid VRML background colors or angles")));
        action->setTerminated(TRUE);
      } else {
        const size_t stereoCount = action->pimpl->frameStereoStates.size();
        background->coinRender(action);
        // The occurrence-local background camera is gone after capture.
        action->pimpl->frameStereoStates.resize(stereoCount);
      }
    } else if (node->isOfType(SoVRMLFog::getClassTypeId())) {
      const auto * fog = static_cast<const SoVRMLFog *>(node);
      const float visibility = fog->visibilityRange.getValue();
      const SbString & type = fog->fogType.getValue();
      // Native Coin uses its camera far plane when visibilityRange <= 0.
      // EXPONENTIAL maps to SMOKE (GL_EXP2), rather than Inventor's FOG.
      const int mode = type == "EXPONENTIAL" ? SoEnvironmentElement::SMOKE : SoEnvironmentElement::HAZE;
      SoEnvironmentElement::set(state, node,
        SoEnvironmentElement::getAmbientIntensity(state),
        SoEnvironmentElement::getAmbientColor(state),
        SoEnvironmentElement::getLightAttenuation(state), mode,
        fog->color.getValue(), visibility);
    } else {
      // The generic VRMLShape callback sets BASE_COLOR before its own push.
      // Enclose that callback so the setting cannot escape this occurrence.
      const auto * shape = static_cast<const SoVRMLShape *>(node);
      if (shape->appearance.getValue() &&
          !shape->appearance.getValue()->isOfType(SoVRMLAppearance::getClassTypeId())) {
        action->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("VRML Shape appearance must be an Appearance node")));
        action->setTerminated(TRUE);
      } else {
        state->push();
        node->callback(action);
        state->pop();
      }
    }
  }
  action->invokePostCallbacks(node);
#else
  node->callback(static_cast<SoCallbackAction *>(base));
#endif
}

void
CoinRenderActionP::markerContentMethod(SoAction * action, SoNode * node)
{
  // Custom subclasses retain their own virtual callback contract.
  if (node->getTypeId() != SoMarkerSet::getClassTypeId() &&
      node->getTypeId() != SoIndexedMarkerSet::getClassTypeId()) {
    shapeContentMethod(action, node);
    return;
  }
  auto * render = static_cast<CoinRenderAction *>(action);
  if (!render->pimpl->acceptNode(node)) return;
  render->invokePreCallbacks(node);
  if (render->getCurrentResponse() == SoCallbackAction::CONTINUE) {
    if (render->pimpl->captureBoundingBox(render, node)) {
      render->pimpl->observeBoundingBox(render, node);
      render->invokePostCallbacks(node);
      return;
    }
    const bool observers = !render->hasSingleShapeCallbacks(node->getTypeId());
    if (render->pimpl->builder.captureMarkerContent(render, node, observers) && observers) {
      // Native marker rendering disables texture-coordinate functions and
      // lighting. Retain inherited point observers in the same safe scope;
      // our point callback suppresses their legacy one-pixel geometry.
      SoState * state = render->getState();
      state->push();
      SoLazyElement::setLightModel(state, SoLazyElement::BASE_COLOR);
      SoMultiTextureEnabledElement::disableAll(state);
      node->callback(render);
      state->pop();
    }
  }
  render->invokePostCallbacks(node);
}

void
CoinRenderActionP::screenContentMethod(SoAction * action, SoNode * node)
{
  // Subclasses retain their virtual callback and geometry contract.
  if (node->getTypeId() != SoImage::getClassTypeId() &&
      node->getTypeId() != SoText2::getClassTypeId()) {
    shapeContentMethod(action, node);
    return;
  }
  auto * render = static_cast<CoinRenderAction *>(action);
  if (!render->pimpl->acceptNode(node)) return;
  render->invokePreCallbacks(node);
  if (render->getCurrentResponse() == SoCallbackAction::CONTINUE) {
    if (render->pimpl->captureBoundingBox(render, node)) {
      render->pimpl->observeBoundingBox(render, node);
      render->invokePostCallbacks(node);
      return;
    }
    render->pimpl->builder.captureScreenContent(render, node);
    // Native image primitive observers still run. Our triangle callback
    // suppresses that legacy quad, leaving only the captured screen pixels.
    if (!render->hasSingleShapeCallbacks(node->getTypeId())) node->callback(render);
  }
  render->invokePostCallbacks(node);
}

SoCallbackAction::Response
CoinRenderActionP::shapePostCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  auto * self = static_cast<CoinRenderActionP *>(userdata);
  self->builder.recordRasterNormal(action, node);
  self->builder.endShape();
  self->endTranslationShape();
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::rasterGeneratorPostCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  static_cast<CoinRenderActionP *>(userdata)->builder.recordRasterGenerator(action, node);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::rasterStatePostCB(void * userdata, SoCallbackAction * action, const SoNode *)
{
  // Native GL texgen callbacks are reapplied when a state stack is popped.
  static_cast<CoinRenderActionP *>(userdata)->builder.synchronizeRasterGenerators(action);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::textureCombinePreCB(void * userdata, SoCallbackAction* action, const SoNode* node)
{
  const int unit = SoTextureUnitElement::get(action->getState());
  if (unit < 0 || unit >= static_cast<int>(COIN_RENDER_MAX_TEXTURE_UNITS)) {
    auto* p = static_cast<CoinRenderActionP*>(userdata);
    p->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("At most eight texture units are supported")));
    return SoCallbackAction::ABORT;
  }
  // Coin's generic callback implementation does not update this element.
  // Use the node's own field/override interpretation with our enabled state.
  const_cast<SoTextureCombine*>(static_cast<const SoTextureCombine*>(node))->doAction(action);
  return SoCallbackAction::CONTINUE;
}

void
CoinRenderActionP::alphaTestMethod(SoAction * action, SoNode * node)
{
  auto * render = static_cast<CoinRenderAction *>(action);
  if (!render->pimpl->acceptNode(node)) return;
  render->invokePreCallbacks(node);
  if (render->getCurrentResponse() == SoCallbackAction::CONTINUE) {
    if (!coin_render_capture_alpha_node(*static_cast<const SoAlphaTest *>(node), render->getState())) {
      render->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
        CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("SoAlphaTest has invalid function or NaN reference")));
      render->setTerminated(TRUE);
    } else {
      // Preserve subclass callback behavior after interpreting inherited fields.
      // This virtual callback remains responsible for any subclass-specific state.
      node->callback(render);
    }
  }
  render->invokePostCallbacks(node);
}

// Match SoCamera::GLRender before its model-matrix step. Orthographic
// sources also become perspective frusta in stereo. Coin's callback already
// installed CROP's viewport; do not install the returned adjustment again.
static bool shadowStereoCamera(const SoCamera &camera, const SbViewportRegion &viewport,
                               SbViewVolume &volume, SbMatrix &view, SbMatrix &projection,
                               std::string &diagnostic)
{
  SbViewportRegion adjusted;
  volume = camera.getViewVolume(viewport, adjusted, SbMatrix::identity());
  const float nearDistance = volume.getNearDist();
  const float farDistance = nearDistance + volume.getDepth();
  const float width = volume.getWidth(), height = volume.getHeight();
  const float adjustment = camera.getStereoAdjustment(), balance = camera.getBalanceAdjustment();
  float focal = camera.focalDistance.getValue() * balance;
  if (!std::isfinite(nearDistance) || nearDistance <= 0.0f || !std::isfinite(farDistance) ||
      farDistance <= nearDistance || !std::isfinite(width) || width <= 0.0f ||
      !std::isfinite(height) || height <= 0.0f || !std::isfinite(adjustment) ||
      !std::isfinite(balance) || !std::isfinite(focal)) {
    diagnostic = "Stereo shadow-map camera requires a finite nondegenerate frustum";
    return false;
  }
  if (focal < nearDistance)
    focal = nearDistance;
  float offset = adjustment * 0.5f;
  if (camera.getStereoMode() == SoCamera::LEFT_VIEW)
    offset = -offset;
  SbVec3f right = volume.getProjectionDirection().cross(volume.getViewUp());
  const float axisLength = right.normalize();
  if (!std::isfinite(axisLength) || axisLength == 0.0f) {
    diagnostic = "Stereo shadow-map camera has no finite eye-separation axis";
    return false;
  }
  volume.getMatrices(view, projection);
  SbMatrix cameraTransform, inverseProjection;
  if (!coin_render_shadow_camera_math::inverse(view, cameraTransform) ||
      !coin_render_shadow_camera_math::inverse(projection, inverseProjection)) {
    diagnostic = "Stereo shadow-map camera has an invalid camera transform";
    return false;
  }
  const float halfWidth = width * 0.5f, halfHeight = height * 0.5f;
  const float shift = offset * nearDistance / focal;
  const float left = -halfWidth - shift, rightPlane = halfWidth - shift;
  if (!std::isfinite(shift) || !std::isfinite(left) || !std::isfinite(rightPlane) ||
      left >= rightPlane || halfHeight <= 0.0f) {
    diagnostic = "Stereo shadow-map camera skew is non-finite or degenerate";
    return false;
  }
  volume.frustum(left, rightPlane, -halfHeight, halfHeight, nearDistance, farDistance);
  volume.transform(cameraTransform);
  SbMatrix eyeTransform;
  eyeTransform.setTranslate(right * offset);
  volume.transform(eyeTransform);
  volume.getMatrices(view, projection);
  SbMatrix inverseView;
  if (!coin_render_shadow_camera_math::inverse(view, inverseView) ||
      !coin_render_shadow_camera_math::inverse(projection, inverseProjection)) {
    diagnostic = "Stereo shadow-map camera produces invalid view/projection matrices";
    return false;
  }
  return true;
}

SoCallbackAction::Response
CoinRenderActionP::cameraStereoPreCB(void * data, SoCallbackAction * action, const SoNode * node)
{
  auto * owner = static_cast<CoinRenderActionP *>(data);
  owner->stereoCameraViewport = SoViewportRegionElement::get(action->getState());
  const auto * camera = static_cast<const SoCamera *>(node);
  if (camera->getStereoMode() == SoCamera::MONOSCOPIC || owner->capturingShadowScene)
    return SoCallbackAction::CONTINUE;
  const int mapping = camera->viewportMapping.getValue();
  bool valid = mapping >= SoCamera::CROP_VIEWPORT_FILL_FRAME && mapping <= SoCamera::LEAVE_ALONE;
  valid = valid && (camera->getStereoMode() == SoCamera::LEFT_VIEW || camera->getStereoMode() == SoCamera::RIGHT_VIEW) &&
    std::isfinite(camera->nearDistance.getValue()) && camera->nearDistance.getValue() > 0 &&
    std::isfinite(camera->farDistance.getValue()) && camera->farDistance.getValue() > camera->nearDistance.getValue() &&
    std::isfinite(camera->aspectRatio.getValue()) && camera->aspectRatio.getValue() > 0 &&
    std::isfinite(camera->focalDistance.getValue()) && std::isfinite(camera->getStereoAdjustment()) &&
    std::isfinite(camera->getBalanceAdjustment());
  for (int i=0; i<3; ++i) valid = valid && std::isfinite(camera->position.getValue()[i]);
  const SbRotation rotation = camera->orientation.getValue();
  for (int i=0; i<4; ++i) valid = valid && std::isfinite(rotation.getValue()[i]);
  if (camera->isOfType(SoPerspectiveCamera::getClassTypeId())) {
    const float angle = static_cast<const SoPerspectiveCamera *>(camera)->heightAngle.getValue();
    valid = valid && std::isfinite(angle) && angle > 0 && angle < 3.14159265358979323846f;
  } else if (camera->isOfType(SoOrthographicCamera::getClassTypeId())) {
    const float height = static_cast<const SoOrthographicCamera *>(camera)->height.getValue();
    valid = valid && std::isfinite(height) && height > 0;
  }
  if (!valid) {
    owner->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("Active stereo camera has invalid fields or settings")));
    return SoCallbackAction::ABORT;
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::cameraStereoPostCB(void * userdata, SoCallbackAction * action,
                                      const SoNode * node)
{
  auto * owner = static_cast<CoinRenderActionP *>(userdata);
  const auto * camera = static_cast<const SoCamera *>(node);
  owner->frameStereoStates.push_back({camera, static_cast<int>(camera->getStereoMode()),
    camera->getStereoAdjustment(), camera->getBalanceAdjustment()});
  if (owner->capturingShadowScene && camera->getStereoMode() == SoCamera::MONOSCOPIC)
    owner->builder.registerShadowMapCameraPose(camera, action->getModelMatrix(),
                                                 action->getViewingMatrix());
  if (camera->getStereoMode() == SoCamera::MONOSCOPIC) return SoCallbackAction::CONTINUE;
  if (owner->capturingShadowScene) {
    SbViewVolume volume;
    SbMatrix view, projection;
    std::string diagnostic;
    if (!shadowStereoCamera(*camera, action->getViewportRegion(), volume, view, projection,
                            diagnostic)) {
      owner->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
                                                             CoinRenderDiagnosticDomain::FRAME_PLAN,
                                                             SbString(diagnostic.c_str())));
      return SoCallbackAction::ABORT;
    }
    SoState *state = action->getState();
    SbBool identity;
    const SbMatrix &model = SoModelMatrixElement::get(state, identity);
    SbMatrix inverseModel;
    if (!coin_render_shadow_camera_math::inverse(model, inverseModel)) {
      owner->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("Stereo shadow-map camera has a non-invertible model transform")));
      return SoCallbackAction::ABORT;
    }
    if (!identity) {
      view.multRight(inverseModel);
      volume.transform(model);
    }
    SbMatrix inverseView;
    if (!coin_render_shadow_camera_math::inverse(view, inverseView)) {
      owner->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("Stereo shadow-map camera model/view product is non-finite or non-invertible")));
      return SoCallbackAction::ABORT;
    }
    SoNode *mutableCamera = const_cast<SoCamera *>(camera);
    SoViewVolumeElement::set(state, mutableCamera, volume);
    SoProjectionMatrixElement::set(state, mutableCamera, projection);
    SoViewingMatrixElement::set(state, mutableCamera, view);
    owner->builder.registerShadowMapCameraPose(camera, model, view);
    return SoCallbackAction::CONTINUE;
  }
  SbViewVolume volume;
  SbMatrix view, projection;
  std::string diagnostic;
  if (!shadowStereoCamera(*camera, owner->stereoCameraViewport, volume, view, projection, diagnostic)) {
    owner->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(diagnostic.c_str())));
    return SoCallbackAction::ABORT;
  }
  SoState * state = action->getState();
  SbBool identity;
  const SbMatrix & model = SoModelMatrixElement::get(state, identity);
  if (!identity) {
    SbMatrix inverseModel;
    if (!coin_render_shadow_camera_math::inverse(model, inverseModel)) {
      owner->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
        CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("Stereo camera has a non-invertible model transform")));
      return SoCallbackAction::ABORT;
    }
    view.multRight(inverseModel);
    volume.transform(model);
  }
  SoNode * mutableCamera = const_cast<SoCamera *>(camera);
  SoViewVolumeElement::set(state, mutableCamera, volume);
  SoProjectionMatrixElement::set(state, mutableCamera, projection);
  SoViewingMatrixElement::set(state, mutableCamera, view);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::unsupportedEffectPreCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  if (node->isOfType(SoCamera::getClassTypeId())) {
    auto * p = static_cast<CoinRenderActionP *>(userdata);
    if (!p->capturingShadowScene) return SoCallbackAction::CONTINUE;
    const SoType type = node->getTypeId();
    if (type != SoPerspectiveCamera::getClassTypeId() &&
        type != SoOrthographicCamera::getClassTypeId()) {
      p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString("shadowMapScene camera type is not qualified")));
      return SoCallbackAction::ABORT;
    }
    std::string viewportDiagnostic;
    if (!validShadowMapViewport(action->getViewportRegion(), viewportDiagnostic)) {
      p->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
                                                         CoinRenderDiagnosticDomain::FRAME_PLAN,
                                                         SbString(viewportDiagnostic.c_str())));
      return SoCallbackAction::ABORT;
    }
    const auto *camera = static_cast<const SoCamera *>(node);
    const int mapping = camera->viewportMapping.getValue();
    const bool validMapping = mapping == SoCamera::ADJUST_CAMERA ||
                              mapping == SoCamera::LEAVE_ALONE ||
                              mapping == SoCamera::CROP_VIEWPORT_FILL_FRAME ||
                              mapping == SoCamera::CROP_VIEWPORT_LINE_FRAME ||
                              mapping == SoCamera::CROP_VIEWPORT_NO_FRAME;
    const SbVec3f position = camera->position.getValue();
    const SbRotation orientation = camera->orientation.getValue();
    const float *rotation = orientation.getValue();
    bool finiteParameters = std::isfinite(camera->aspectRatio.getValue()) &&
                            std::isfinite(camera->nearDistance.getValue()) &&
                            std::isfinite(camera->farDistance.getValue()) &&
                            std::isfinite(camera->focalDistance.getValue());
    for (int i = 0; i < 3; ++i) finiteParameters = finiteParameters && std::isfinite(position[i]);
    for (int i = 0; i < 4; ++i) finiteParameters = finiteParameters && std::isfinite(rotation[i]);
    bool valid = validMapping && finiteParameters && camera->aspectRatio.getValue() > 0.0f &&
                 camera->farDistance.getValue() > camera->nearDistance.getValue();
    if (type == SoPerspectiveCamera::getClassTypeId()) {
      const float angle = static_cast<const SoPerspectiveCamera *>(camera)->heightAngle.getValue();
      finiteParameters = finiteParameters && std::isfinite(angle);
      valid = valid && camera->nearDistance.getValue() > 0.0f &&
        std::isfinite(angle) && angle > 0.0f && angle < 3.14159265358979323846f;
    } else {
      const float height = static_cast<const SoOrthographicCamera *>(camera)->height.getValue();
      finiteParameters = finiteParameters && std::isfinite(height);
      valid = valid && std::isfinite(height) && height > 0.0f;
    }
    const auto stereo = camera->getStereoMode();
    if (stereo != SoCamera::MONOSCOPIC) {
      const float focal = camera->focalDistance.getValue() * camera->getBalanceAdjustment();
      finiteParameters = finiteParameters && std::isfinite(camera->getStereoAdjustment()) &&
                         std::isfinite(camera->getBalanceAdjustment()) && std::isfinite(focal);
      valid = valid && (stereo == SoCamera::LEFT_VIEW || stereo == SoCamera::RIGHT_VIEW) &&
              camera->nearDistance.getValue() > 0.0f &&
              std::isfinite(camera->getStereoAdjustment()) &&
              std::isfinite(camera->getBalanceAdjustment()) && std::isfinite(focal);
    }
    if (!valid) {
      p->setDiagnostic(CoinRenderDiagnosticShell::action(
        finiteParameters ? CoinRenderAction::UNSUPPORTED : CoinRenderAction::INVALID_SCENE,
        CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString(finiteParameters ? "shadowMapScene camera has unsupported or invalid parameters"
                                  : "shadowMapScene camera has non-finite parameters")));
      return SoCallbackAction::ABORT;
    }
    p->builder.registerShadowMapCamera(camera);
    return SoCallbackAction::CONTINUE;
  }

  const char * diagnostic = nullptr;
  if (node->isOfType(SoBumpMap::getClassTypeId())) {
    const auto * bump = static_cast<const SoBumpMap *>(node);
    SoBumpMapElement::set(action->getState(), const_cast<SoBumpMap *>(bump), nullptr);
    SbVec2s size;
    int components = 0;
    bump->image.getValue(size, components);
    if (size != SbVec2s(0, 0) || bump->filename.getValue().getLength() != 0)
      static_cast<CoinRenderActionP *>(userdata)->encounteredActiveBump = true;
    return SoCallbackAction::CONTINUE;
  } else if (node->isOfType(SoTextureCubeMap::getClassTypeId())) {
    auto * owner = static_cast<CoinRenderActionP *>(userdata);
    std::string error;
    if (!owner->builder.captureCubeNode(action, static_cast<const SoTextureCubeMap *>(node), error)) {
      owner->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(error.c_str())));
      return SoCallbackAction::ABORT;
    }
    return SoCallbackAction::CONTINUE;
  } else if (node->isOfType(SoSceneTextureCubeMap::getClassTypeId())) {
    return SoCallbackAction::CONTINUE;
  } else if (node->isOfType(SoShaderProgram::getClassTypeId())) {
    auto *owner = static_cast<CoinRenderActionP *>(userdata);
    const auto found = owner->portableShaderModules.find(const_cast<SoShaderProgram *>(
      static_cast<const SoShaderProgram *>(node)));
    if (found != owner->portableShaderModules.end()) {
      bool active = false;
      std::string error;
      if (!coin_render_shader_active(*found->second, active, error)) {
        owner->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(error.c_str())));
        return SoCallbackAction::ABORT;
      }
      // An empty/inactive program replaces, rather than inherits, the previous
      // module. Separator restores its own preceding element afterwards.
      CoinRenderShaderModuleElement::set(action->getState(), active ? found->second :
        std::shared_ptr<const CoinRenderShaderRegistration>());
      if (!active && owner->builder.hasActiveShadowGroup()) {
        owner->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("Empty SoShaderProgram disables the active shadow shader; this profile is unsupported")));
        return SoCallbackAction::ABORT;
      }
      // Resource and numeric proofs follow only at consumed active draws.
      return SoCallbackAction::CONTINUE;
    }
    bool active = false;
    std::string activationError;
    if (!coin_render_shader_active(static_cast<const SoShaderProgram *>(node), active, activationError)) {
      owner->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(activationError.c_str())));
      return SoCallbackAction::ABORT;
    }
    if (!active) {
      CoinRenderShaderModuleElement::set(action->getState(),
        std::shared_ptr<const CoinRenderShaderRegistration>());
      if (!owner->builder.hasActiveShadowGroup()) return SoCallbackAction::CONTINUE;
      // Empty programs remove Coin's generated shadow program, changing both
      // lighting/shadows and texture projection without changing ShadowStyle.
      diagnostic = "Empty SoShaderProgram disables the active shadow shader; this profile is unsupported";
    } else diagnostic = "SoShaderProgram has no portable shader contract";
  }
  if (!diagnostic) return SoCallbackAction::CONTINUE;
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  p->setDiagnostic(CoinRenderDiagnosticShell::action(
    CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
    SbString(diagnostic)));
  return SoCallbackAction::ABORT;
}

void
CoinRenderActionP::initCallbacks()
{
  this->master->addPreCallback(SoNode::getClassTypeId(), rttMutationPreCB, this);
  this->master->addTriangleCallback(SoShape::getClassTypeId(), triangleCB, this);
  this->master->addLineSegmentCallback(SoShape::getClassTypeId(), lineCB, this);
  this->master->addPointCallback(SoShape::getClassTypeId(), pointCB, this);

  this->master->addPreCallback(SoCamera::getClassTypeId(), cameraStereoPreCB, this);
  this->master->addPreCallback(SoCamera::getClassTypeId(), unsupportedEffectPreCB, this);
  this->master->addPostCallback(SoCamera::getClassTypeId(), cameraStereoPostCB, this);
  this->master->addPreCallback(SoTexture2::getClassTypeId(), textureImagePreCB, this);
  this->master->addPreCallback(SoTexture3::getClassTypeId(), textureVolumePreCB, this);
  this->master->addPostCallback(SoTextureCoordinateObject::getClassTypeId(), rasterGeneratorPostCB, this);
  this->master->addPostCallback(SoTextureCoordinatePlane::getClassTypeId(), rasterGeneratorPostCB, this);
  this->master->addPostCallback(SoTextureCoordinateEnvironment::getClassTypeId(), rasterGeneratorPostCB, this);
  this->master->addPostCallback(SoTextureCoordinateReflectionMap::getClassTypeId(), rasterGeneratorPostCB, this);
  this->master->addPostCallback(SoTextureCoordinateNormalMap::getClassTypeId(), rasterGeneratorPostCB, this);
  this->master->addPreCallback(SoShape::getClassTypeId(), textureUnitsPreCB, this);
  this->master->addPostCallback(SoShape::getClassTypeId(), shapePostCB, this);
  this->master->addPreCallback(SoTranslation::getClassTypeId(), translationPreCB, this);
  this->master->addPreCallback(SoTransform::getClassTypeId(), translationPreCB, this);
  this->master->addPreCallback(SoLight::getClassTypeId(), lightPreCB, this);
  this->master->addPreCallback(SoTextureCombine::getClassTypeId(), textureCombinePreCB, this);
  this->master->addPreCallback(SoBumpMap::getClassTypeId(), unsupportedEffectPreCB, this);
  this->master->addPreCallback(SoTextureCubeMap::getClassTypeId(), unsupportedEffectPreCB, this);
  this->master->addPreCallback(SoSceneTextureCubeMap::getClassTypeId(), unsupportedEffectPreCB, this);
  this->master->addPreCallback(SoShadowGroup::getClassTypeId(), shadowGroupPreCB, this);
  this->master->addPostCallback(SoShadowGroup::getClassTypeId(), shadowGroupPostCB, this);
  this->master->addPreCallback(SoShadowStyle::getClassTypeId(), shadowStylePreCB, this);
  this->master->addPreCallback(SoShaderProgram::getClassTypeId(), unsupportedEffectPreCB, this);
  this->master->addPreCallback(SoDepthBuffer::getClassTypeId(), depthBufferPreCB, this);
  this->master->addPreCallback(SoAnnotation::getClassTypeId(), annotationPreCB, this);
  this->master->addPostCallback(SoAnnotation::getClassTypeId(), annotationPostCB, this);
  this->master->addPreCallback(SoSeparator::getClassTypeId(), managerLayerPreCB, this);
  this->master->addPostCallback(SoSeparator::getClassTypeId(), managerLayerPostCB, this);
  this->master->addPreCallback(SoSceneTexture2::getClassTypeId(), sceneTexturePreCB, this);
  this->master->addPreCallback(SoSceneTextureCubeMap::getClassTypeId(), sceneCubeTexturePreCB, this);
  this->master->addPreCallback(SoIndexedFaceSet::getClassTypeId(), indexedFaceSetPreCB, this);
  this->master->addPreCallback(SoIndexedLineSet::getClassTypeId(), indexedLineSetPreCB, this);
  this->master->addPostCallback(SoNode::getClassTypeId(), rasterStatePostCB, this);
  this->captureCallbackRevision = this->master->callbackRegistrationRevision();
}

void
CoinRenderActionP::triangleCB(void * userdata,
                               SoCallbackAction * action,
                               const SoPrimitiveVertex * v0,
                               const SoPrimitiveVertex * v1,
                               const SoPrimitiveVertex * v2)
{
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->boundingBoxObservers) return;
  const auto type = action->getCurPathTail()->getTypeId();
  if (type == SoImage::getClassTypeId() || type == SoText2::getClassTypeId()) return;
  p->builder.addTriangle(action, v0, v1, v2);
}

void
CoinRenderActionP::lineCB(void * userdata,
                           SoCallbackAction * action,
                           const SoPrimitiveVertex * v0,
                           const SoPrimitiveVertex * v1)
{
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->boundingBoxObservers) return;
  p->builder.addLine(action, v0, v1);
}

void
CoinRenderActionP::pointCB(void * userdata,
                            SoCallbackAction * action,
                            const SoPrimitiveVertex * vertex)
{
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->boundingBoxObservers) return;
  const auto type = action->getCurPathTail()->getTypeId();
  if (type == SoMarkerSet::getClassTypeId() || type == SoIndexedMarkerSet::getClassTypeId()) return;
  p->builder.addPoint(action, vertex);
}


SoCallbackAction::Response
CoinRenderActionP::depthBufferPreCB(void *, SoCallbackAction * action, const SoNode * node)
{
  const SoDepthBuffer * depth = static_cast<const SoDepthBuffer *>(node);
  SoState * state = action->getState();
  SbBool test = depth->test.isIgnored()
    ? SoDepthBufferElement::getTestEnable(state) : depth->test.getValue();
  SbBool write = depth->write.isIgnored()
    ? SoDepthBufferElement::getWriteEnable(state) : depth->write.getValue();
  SoDepthBufferElement::DepthWriteFunction function = depth->function.isIgnored()
    ? SoDepthBufferElement::getFunction(state)
    : static_cast<SoDepthBufferElement::DepthWriteFunction>(depth->function.getValue());
  const SbVec2f range = depth->range.isIgnored()
    ? SoDepthBufferElement::getRange(state) : depth->range.getValue();
  SoDepthBufferElement::set(state, test, write, function, range);
  CoinRenderDepthPolicyElement::add(state, (depth->test.isIgnored() ? 0 : 1) |
    (depth->write.isIgnored() ? 0 : 2) | (depth->function.isIgnored() ? 0 : 4) |
    (depth->range.isIgnored() ? 0 : 8));
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::annotationPreCB(void * userdata, SoCallbackAction * action, const SoNode *)
{
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->capturingShadowScene) return SoCallbackAction::PRUNE;
  action->getState()->push();
  // Coin/GL annotations render last with depth testing disabled. They do not
  // clear or write the scene depth; an explicit child DepthBuffer can override.
  SoDepthBufferElement::set(action->getState(), FALSE, FALSE,
    SoDepthBufferElement::getFunction(action->getState()),
    SoDepthBufferElement::getRange(action->getState()));
  p->builder.beginAnnotation(false);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::annotationPostCB(void * userdata, SoCallbackAction * action, const SoNode *)
{
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  if (!p->capturingShadowScene) {
    p->builder.endAnnotation();
    action->getState()->pop();
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::managerLayerPreCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->capturingShadowScene || p->replayingAnnotations) return SoCallbackAction::CONTINUE;
  const auto found = std::find_if(p->managerLayerBoundaries.begin(),
    p->managerLayerBoundaries.end(),
    [node](const std::tuple<const SoNode *, bool, int> & layer) {
      return std::get<0>(layer) == node;
    });
  if (found != p->managerLayerBoundaries.end()) {
    const int type = std::get<2>(*found);
    // This purpose belongs to a generated composition boundary, never to an
    // inferred combination of authored material, style or texture state.
    if (type == COIN_RENDER_MANAGER_HIGHLIGHT_LAYER) ++p->managerHighlightDepth;
    action->getState()->push();
    const int effectiveType = type >= 0 ? type : static_cast<int>(p->transparencyType);
    SoShapeStyleElement::setTransparencyType(action->getState(), effectiveType);
    SoLazyElement::setTransparencyType(action->getState(), effectiveType);
    SoOverrideElement::setTransparencyTypeOverride(action->getState(),
      const_cast<SoNode *>(node), effectiveType == SoGLRenderAction::SORTED_LAYERS_BLEND);
    p->builder.beginManagerLayer(std::get<1>(*found), type == COIN_RENDER_MANAGER_HIGHLIGHT_LAYER);
    p->managerCapturedLayers.push_back(p->builder.activeAnnotationLayer());
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::managerLayerPostCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->capturingShadowScene || p->replayingAnnotations) return SoCallbackAction::CONTINUE;
  const auto found = std::find_if(p->managerLayerBoundaries.begin(),
    p->managerLayerBoundaries.end(),
    [node](const std::tuple<const SoNode *, bool, int> & layer) {
      return std::get<0>(layer) == node;
    });
  if (found != p->managerLayerBoundaries.end()) {
    p->builder.capturePendingDepthClear(action);
    p->builder.endManagerLayer(std::get<2>(*found) == COIN_RENDER_MANAGER_HIGHLIGHT_LAYER);
    if (std::get<2>(*found) == COIN_RENDER_MANAGER_HIGHLIGHT_LAYER &&
        p->managerHighlightDepth) --p->managerHighlightDepth;
    action->getState()->pop();
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::shadowGroupPreCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  const auto * group = static_cast<const SoShadowGroup *>(node);
  // Selection replay still traverses the real path and its transforms, but
  // only the authored base contributes shadow groups, lights and map scenes.
  if (p->managerHighlightDepth || p->capturingShadowScene || !group->isActive.getValue())
    return SoCallbackAction::CONTINUE;
  CoinRenderGuardedTraversal<SoSearchAction> mapLightSearch;
  mapLightSearch.setType(SoLight::getClassTypeId());
  mapLightSearch.setInterest(SoSearchAction::ALL);
  mapLightSearch.apply(const_cast<SoShadowGroup *>(group));
  bool hasMapLight = false;
  for (int i = 0; i < mapLightSearch.getPaths().getLength(); ++i) {
    const SoNode * light = mapLightSearch.getPaths()[i]->getTail();
    if (!static_cast<const SoLight *>(light)->on.getValue()) continue;
    if (light->isOfType(SoSpotLight::getClassTypeId()) ||
        light->isOfType(SoShadowDirectionalLight::getClassTypeId())) {
      hasMapLight = true;
      break;
    }
  }
  if (!hasMapLight) return SoCallbackAction::CONTINUE;
  CoinRenderShadowGroupSnapshot snapshot;
  snapshot.sourceRevision = node->getNodeId();
  snapshot.intensity = group->intensity.getValue();
  snapshot.precision = group->precision.getValue();
  snapshot.quality = group->quality.getValue();
  snapshot.epsilon = group->epsilon.getValue();
  snapshot.threshold = group->threshold.getValue();
  snapshot.smoothBorder = group->smoothBorder.getValue();
  snapshot.shadowCachingEnabled = group->shadowCachingEnabled.getValue() != FALSE;
  snapshot.visibilityNearRadius = group->visibilityNearRadius.getValue();
  snapshot.visibilityRadius = group->visibilityRadius.getValue();
  snapshot.visibilityFlag = group->visibilityFlag.getValue();
  snapshot.parentGroupSlot = p->builder.activeShadowGroupSlot();
  snapshot.hasEntryCamera = true;
  snapshot.entryCamera = CoinRenderFramePlanBuilder::captureCamera(action);
  snapshot.entryModel = action->getModelMatrix();
  p->shadowStyleBeforeGroups.push_back(SoShadowStyleElement::get(action->getState()));
  SoShadowStyleElement::set(action->getState(), 3);
  p->builder.beginShadowGroup(snapshot);
  p->activeShadowGroupNodes.push_back(group);
  // Coin/GL renders light-owned scenes from the group-entry state, resetting
  // lazy material, enabled textures and model matrix in SoSceneTexture2.
  // Reuse ordinary captured casters for simple groups. Composition requires
  // its own map traversal because nested group resets and annotations affect
  // the main pass differently from SHADOWMAP.
  // Coin/GL renders the whole caster scene even when apply(path) selects
  // only a receiver. Reuse the independent map capture for this traversal.
  int pathIndexCount=0;
  const int * pathIndices=nullptr;
  bool separateMap = snapshot.parentGroupSlot != 0 ||
    p->master->callbackRegistrationRevision() != p->captureCallbackRevision ||
    (SoShapeStyleElement::get(action->getState())->getFlags() & SoShapeStyleElement::BBOXCMPLX) ||
    action->getPathCode(pathIndexCount,pathIndices) == SoAction::IN_PATH;
  CoinRenderGuardedTraversal<SoSearchAction> structure;
  structure.setType(SoComplexity::getClassTypeId());
  structure.setInterest(SoSearchAction::FIRST);
  structure.apply(const_cast<SoShadowGroup *>(group));
  separateMap = separateMap || structure.getPath() != nullptr;
  structure.reset();
  structure.setType(SoAnnotation::getClassTypeId());
  structure.setInterest(SoSearchAction::FIRST);
  structure.apply(const_cast<SoShadowGroup *>(group));
  separateMap = separateMap || structure.getPath() != nullptr;
  structure.reset();
  structure.setType(SoShadowGroup::getClassTypeId());
  structure.setInterest(SoSearchAction::ALL);
  structure.apply(const_cast<SoShadowGroup *>(group));
  for (int i = 0; i < structure.getPaths().getLength(); ++i) {
    const auto * child = static_cast<const SoShadowGroup *>(structure.getPaths()[i]->getTail());
    separateMap = separateMap || (child != group && child->isActive.getValue());
  }
  CoinRenderGuardedTraversal<SoSearchAction> search;
  search.setType(SoLight::getClassTypeId());
  search.setInterest(SoSearchAction::ALL);
  search.apply(const_cast<SoShadowGroup *>(group));
  for (int i = 0; i < search.getPaths().getLength(); ++i) {
    const SoNode * light = search.getPaths()[i]->getTail();
    SoNode * scene = nullptr;
    if (light->isOfType(SoShadowSpotLight::getClassTypeId()))
      scene = static_cast<const SoShadowSpotLight *>(light)->shadowMapScene.getValue();
    else if (light->isOfType(SoShadowDirectionalLight::getClassTypeId()))
      scene = static_cast<const SoShadowDirectionalLight *>(light)->shadowMapScene.getValue();
    if (!static_cast<const SoLight *>(light)->on.getValue()) continue;
    if (!scene && separateMap && (light->isOfType(SoSpotLight::getClassTypeId()) ||
                   light->isOfType(SoShadowDirectionalLight::getClassTypeId())))
      scene = const_cast<SoShadowGroup *>(group);
    if (!scene) continue;
    if (std::any_of(p->shadowSceneCaptures.begin(), p->shadowSceneCaptures.end(),
        [&](const ShadowSceneCapture & capture) {
          return capture.groupSlot == p->builder.activeShadowGroupSlot() &&
            capture.lightRevision == light->getNodeId();
        })) continue;
    uint32_t mapSize = 0;
    std::string mapDiagnostic;
    if (!coin_render_shadow_map_size(snapshot.precision, mapSize, mapDiagnostic)) {
      p->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
                                                         CoinRenderDiagnosticDomain::FRAME_PLAN,
                                                         SbString(mapDiagnostic.c_str())));
      return SoCallbackAction::ABORT;
    }
    const SbViewportRegion mapViewport(static_cast<short>(mapSize), static_cast<short>(mapSize));
    CoinRenderFramePlanBuilder savedBuilder;
    std::swap(savedBuilder, p->builder);
    p->builder.beginFrame(p->backgroundColor, mapViewport);
    p->builder.configureTextures(p->executionOptions);
    auto captureGroup = snapshot;
    captureGroup.parentGroupSlot = 0;
    p->builder.beginShadowGroup(captureGroup);
    SoState * state = action->getState();
    const uint32_t inheritedPlanes = SoClipPlaneElement::getInstance(state)->getNum();
    state->push();
    // SoSceneTexture2 renders each light map with a square viewport. Camera
    // viewportMapping must see that aspect, not the observer's viewport.
    const SbViewportRegion observerViewport = action->getViewportRegion();
    action->setViewportRegion(mapViewport);
    SoViewportRegionElement::set(state, mapViewport);
    SoLazyElement::setToDefault(state);
    SoMultiTextureEnabledElement::disableAll(state);
    SoLazyElement::setLightModel(state, SoLazyElement::BASE_COLOR);
    SoTextureQualityElement::set(state, 0.0f);
    SoMaterialBindingElement::set(state, nullptr, SoMaterialBindingElement::OVERALL);
    SoOverrideElement::setMaterialBindingOverride(state, nullptr, TRUE);
    SoOverrideElement::setLightModelOverride(state, nullptr, TRUE);
    SoTextureOverrideElement::setQualityOverride(state, TRUE);
    SoModelMatrixElement::set(state, const_cast<SoShadowGroup *>(group), SbMatrix::identity());
    p->capturingShadowScene = true;
    // GL flattens a ShadowGroup used as the custom scene.
    if (scene->isOfType(SoShadowGroup::getClassTypeId())) {
      const auto * sub = static_cast<const SoShadowGroup *>(scene);
      for (int child = 0; child < sub->getNumChildren(); ++child)
        action->switchToNodeTraversal(sub->getChild(child));
    } else action->switchToNodeTraversal(scene);
    p->capturingShadowScene = false;
    state->pop();
    action->setViewportRegion(observerViewport);
    action->setCurrentNode(const_cast<SoNode *>(node));
    ShadowSceneCapture capture;
    capture.groupSlot = savedBuilder.activeShadowGroupSlot();
    capture.lightRevision = light->getNodeId();
    capture.inheritedClipPlaneCount = inheritedPlanes;
    std::string error;
    p->builder.endShadowGroup();
    const bool ok = p->builder.build(capture.frame, &error, true);
    std::swap(savedBuilder, p->builder);
    if (!ok || action->hasTerminated()) {
      if (!action->hasTerminated()) p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(error.c_str())));
      return SoCallbackAction::ABORT;
    }
    for (auto &mapState : capture.frame.renderStates)
      mapState.shadowMapSize = mapSize;
    p->shadowSceneCaptures.push_back(std::move(capture));
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::shadowGroupPostCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->managerHighlightDepth || p->capturingShadowScene ||
      !static_cast<const SoShadowGroup *>(node)->isActive.getValue())
    return SoCallbackAction::CONTINUE;
  if (p->activeShadowGroupNodes.empty() || p->activeShadowGroupNodes.back() != node)
    return SoCallbackAction::CONTINUE;
  p->builder.endShadowGroup();
  if (!p->activeShadowGroupNodes.empty()) p->activeShadowGroupNodes.pop_back();
  if (!p->shadowStyleBeforeGroups.empty()) {
    SoShadowStyleElement::set(action->getState(), p->shadowStyleBeforeGroups.back());
    p->shadowStyleBeforeGroups.pop_back();
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::shadowStylePreCB(void *, SoCallbackAction * action, const SoNode * node)
{
  const auto * style = static_cast<const SoShadowStyle *>(node);
  SoShadowStyleElement::set(action->getState(), const_cast<SoShadowStyle *>(style),
                            style->style.getValue());
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::lightPreCB(void * userdata,
                               SoCallbackAction * action,
                               const SoNode * node)
{
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->managerHighlightDepth || p->capturingShadowScene) return SoCallbackAction::CONTINUE;
  p->builder.recordLightAttenuation(action);
  if (!p->builder.hasActiveShadowGroup()) return SoCallbackAction::CONTINUE;
  const auto * light = static_cast<const SoLight *>(node);
  CoinRenderShadowLightSnapshot snapshot;
  snapshot.groupSlot = p->builder.activeShadowGroupSlot();
  snapshot.sourceRevision = node->getNodeId();
  snapshot.enabled = light->on.getValue() != FALSE;
  snapshot.color = light->color.getValue();
  snapshot.intensity = light->intensity.getValue();
  snapshot.attenuation = SoEnvironmentElement::getLightAttenuation(action->getState());
  snapshot.model = action->getModelMatrix();
  snapshot.modelViewAtLight = action->getModelMatrix() * action->getViewingMatrix();
  if (node->isOfType(SoSpotLight::getClassTypeId())) {
    const auto * spot = static_cast<const SoSpotLight *>(node);
    snapshot.type = CoinRenderLightType::SPOT;
    snapshot.shadowEligible = true;
    snapshot.position = spot->location.getValue();
    snapshot.direction = spot->direction.getValue();
    snapshot.cutOffAngle = spot->cutOffAngle.getValue();
    snapshot.dropOffRate = spot->dropOffRate.getValue();
    if (node->isOfType(SoShadowSpotLight::getClassTypeId())) {
      const auto * shadow = static_cast<const SoShadowSpotLight *>(node);
      snapshot.hasCustomScene = shadow->shadowMapScene.getValue() != nullptr;
      snapshot.nearDistance = shadow->nearDistance.getValue();
      snapshot.farDistance = shadow->farDistance.getValue();
    }
  } else if (node->isOfType(SoDirectionalLight::getClassTypeId())) {
    const auto * directional = static_cast<const SoDirectionalLight *>(node);
    snapshot.type = CoinRenderLightType::DIRECTIONAL;
    snapshot.direction = directional->direction.getValue();
    if (node->isOfType(SoShadowDirectionalLight::getClassTypeId())) {
      const auto * shadow = static_cast<const SoShadowDirectionalLight *>(node);
      snapshot.shadowEligible = true;
      snapshot.hasCustomScene = shadow->shadowMapScene.getValue() != nullptr;
      snapshot.maxShadowDistance = shadow->maxShadowDistance.getValue();
      snapshot.bboxCenter = shadow->bboxCenter.getValue();
      snapshot.bboxSize = shadow->bboxSize.getValue();
    }
  } else if (node->isOfType(SoPointLight::getClassTypeId())) {
    snapshot.type = CoinRenderLightType::POINT;
    snapshot.position = static_cast<const SoPointLight *>(node)->location.getValue();
  }
  if (snapshot.hasCustomScene && !p->activeShadowGroupNodes.empty()) {
    const SoNode * scene = nullptr;
    if (node->isOfType(SoShadowSpotLight::getClassTypeId()))
      scene = static_cast<const SoShadowSpotLight *>(node)->shadowMapScene.getValue();
    else if (node->isOfType(SoShadowDirectionalLight::getClassTypeId()))
      scene = static_cast<const SoShadowDirectionalLight *>(node)->shadowMapScene.getValue();
    if (scene) snapshot.customSceneNodeId = scene->getNodeId();
  }
  p->builder.recordShadowLight(snapshot);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::indexedFaceSetPreCB(void * userdata,
                                        SoCallbackAction * action,
                                        const SoNode * node)
{
  // PRUNE skips this shape only; subsequent state nodes still traverse.
  if (CoinRenderFramePlanBuilder::isShapeInvisible(action)) return SoCallbackAction::PRUNE;
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  if (!p->fastPathEnabled || p->master->callbackRegistrationRevision() != p->captureCallbackRevision ||
      (!p->capturingShadowScene && (SoShapeStyleElement::get(action->getState())->getFlags() &
                                   SoShapeStyleElement::BBOXCMPLX))) {
    return SoCallbackAction::CONTINUE;
  }

  // Preserve the original polygon details used by the common style resolver.
  if (CoinRenderFramePlanBuilder::polygonDrawStyle(action) != SoDrawStyle::FILLED)
    return SoCallbackAction::CONTINUE;

  // The direct path bypasses the node virtual callback implementation.
  // Subclasses may prepare traversal state there, so only prune the exact type.
  if (node->getTypeId() != SoIndexedFaceSet::getClassTypeId()) {
    return SoCallbackAction::CONTINUE;
  }

  const SoIndexedFaceSet * ifs = dynamic_cast<const SoIndexedFaceSet *>(node);
  if (!ifs) {
    return SoCallbackAction::CONTINUE;
  }

  SoState * state = action->getState();
  if (!state) {
    return SoCallbackAction::CONTINUE;
  }

  const SoVertexProperty * vp = static_cast<const SoVertexProperty *>(ifs->vertexProperty.getValue());
  if (vp) {
    state->push();
    const_cast<SoVertexProperty *>(vp)->doAction(action);
  }

  int lastTextureUnit = -1;
  SoMultiTextureEnabledElement::getEnabledUnits(state, lastTextureUnit);
  if (lastTextureUnit > 0) {
    if (vp) state->pop();
    return SoCallbackAction::CONTINUE;
  }

  const SoCoordinateElement * coords = SoCoordinateElement::getInstance(state);
  if (!coords || !coords->is3D()) {
    if (vp) state->pop();
    return SoCallbackAction::CONTINUE;
  }

  const SbVec3f * coordArray = coords->getArrayPtr3();
  int32_t numCoords = coords->getNum();
  if (!coordArray || numCoords <= 0) {
    if (vp) state->pop();
    return SoCallbackAction::CONTINUE;
  }

  CoinRenderDirectGeometryView view;
  view.positions = CoinRenderSpan<SbVec3f>(coordArray, static_cast<size_t>(numCoords));

  const SoNormalElement * normElem = SoNormalElement::getInstance(state);
  if (normElem && normElem->getNum() > 0) {
    view.normals = CoinRenderSpan<SbVec3f>(normElem->getArrayPtr(), static_cast<size_t>(normElem->getNum()));
  }

  if (ifs->coordIndex.getNum() > 0) {
    view.coordIndex = CoinRenderSpan<int32_t>(ifs->coordIndex.getValues(0), static_cast<size_t>(ifs->coordIndex.getNum()));
  }
  if (ifs->normalIndex.getNum() > 0 && !(ifs->normalIndex.getNum() == 1 && ifs->normalIndex[0] == -1)) {
    view.normalIndex = CoinRenderSpan<int32_t>(ifs->normalIndex.getValues(0), static_cast<size_t>(ifs->normalIndex.getNum()));
  }
  if (ifs->materialIndex.getNum() > 0 && !(ifs->materialIndex.getNum() == 1 && ifs->materialIndex[0] == -1)) {
    view.materialIndex = CoinRenderSpan<int32_t>(ifs->materialIndex.getValues(0), static_cast<size_t>(ifs->materialIndex.getNum()));
  }
  if (ifs->textureCoordIndex.getNum() > 0 && !(ifs->textureCoordIndex.getNum() == 1 && ifs->textureCoordIndex[0] == -1)) {
    view.texCoordIndex = CoinRenderSpan<int32_t>(ifs->textureCoordIndex.getValues(0), static_cast<size_t>(ifs->textureCoordIndex.getNum()));
  }

  const SoMultiTextureCoordinateElement * tcElem = SoMultiTextureCoordinateElement::getInstance(state);
  if (tcElem) {
    SoMultiTextureCoordinateElement::CoordType ct = tcElem->getType(0);
    if (ct == SoMultiTextureCoordinateElement::EXPLICIT) {
      // The direct geometry view carries 2D coordinates. Native callbacks
      // preserve R and Q for explicit 3D/4D coordinates.
      if (tcElem->getDimension(0) != 2) {
        if (vp) state->pop();
        return SoCallbackAction::CONTINUE;
      }
      int32_t numTc = tcElem->getNum(0);
      if (numTc > 0) {
        const SbVec2f * tcPtr = tcElem->getArrayPtr2(0);
        if (tcPtr) {
          view.texcoords = CoinRenderSpan<SbVec2f>(tcPtr, static_cast<size_t>(numTc));
        }
      }
    }
  }

  view.materialBinding = SoMaterialBindingElement::get(state);
  view.normalBinding = SoNormalBindingElement::get(state);
  normalizeIndexedBindings(node, view);

  std::string err;
  CoinRenderFastPathResult res = p->builder.processIndexedFaceSet(action, view, const_cast<SoNode *>(node), &err);

  if (vp) {
    state->pop();
  }

  if (res == CoinRenderFastPathResult::SUCCESS_PRUNE) {
    return SoCallbackAction::PRUNE;
  } else if (res == CoinRenderFastPathResult::INVALID_SCENE) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString(err.empty() ? "Invalid scene in IndexedFaceSet" : err.c_str())));
    return SoCallbackAction::ABORT;
  } else if (res == CoinRenderFastPathResult::UNSUPPORTED) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString(err.empty() ? "Unsupported feature in IndexedFaceSet" : err.c_str())));
    return SoCallbackAction::ABORT;
  } else {
    return SoCallbackAction::CONTINUE;
  }
}

SoCallbackAction::Response
CoinRenderActionP::indexedLineSetPreCB(void * userdata,
                                        SoCallbackAction * action,
                                        const SoNode * node)
{
  // PRUNE skips this shape only; subsequent state nodes still traverse.
  if (CoinRenderFramePlanBuilder::isShapeInvisible(action)) return SoCallbackAction::PRUNE;
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  if (!p->fastPathEnabled || p->master->callbackRegistrationRevision() != p->captureCallbackRevision ||
      (!p->capturingShadowScene && (SoShapeStyleElement::get(action->getState())->getFlags() &
                                   SoShapeStyleElement::BBOXCMPLX))) {
    return SoCallbackAction::CONTINUE;
  }

  // Preserve callback semantics for subclasses; see indexedFaceSetPreCB().
  if (node->getTypeId() != SoIndexedLineSet::getClassTypeId()) {
    return SoCallbackAction::CONTINUE;
  }

  const SoIndexedLineSet * ils = dynamic_cast<const SoIndexedLineSet *>(node);
  if (!ils) {
    return SoCallbackAction::CONTINUE;
  }

  SoState * state = action->getState();
  if (!state) {
    return SoCallbackAction::CONTINUE;
  }

  const SoVertexProperty * vp = static_cast<const SoVertexProperty *>(ils->vertexProperty.getValue());
  if (vp) {
    state->push();
    const_cast<SoVertexProperty *>(vp)->doAction(action);
  }

  const SoCoordinateElement * coords = SoCoordinateElement::getInstance(state);
  if (!coords || !coords->is3D()) {
    if (vp) state->pop();
    return SoCallbackAction::CONTINUE;
  }

  const SbVec3f * coordArray = coords->getArrayPtr3();
  int32_t numCoords = coords->getNum();
  if (!coordArray || numCoords <= 0) {
    if (vp) state->pop();
    return SoCallbackAction::CONTINUE;
  }

  CoinRenderDirectGeometryView view;
  view.positions = CoinRenderSpan<SbVec3f>(coordArray, static_cast<size_t>(numCoords));

  if (ils->coordIndex.getNum() > 0) {
    view.coordIndex = CoinRenderSpan<int32_t>(ils->coordIndex.getValues(0), static_cast<size_t>(ils->coordIndex.getNum()));
  }
  const auto* normals=SoNormalElement::getInstance(state);
  if(normals && normals->getNum()>0)
    view.normals=CoinRenderSpan<SbVec3f>(normals->getArrayPtr(),static_cast<size_t>(normals->getNum()));
  if(ils->normalIndex.getNum()>0 && !(ils->normalIndex.getNum()==1 && ils->normalIndex[0]==-1))
    view.normalIndex=CoinRenderSpan<int32_t>(ils->normalIndex.getValues(0),static_cast<size_t>(ils->normalIndex.getNum()));
  view.normalBinding=SoNormalBindingElement::get(state);
  if (ils->materialIndex.getNum() > 0 && !(ils->materialIndex.getNum() == 1 && ils->materialIndex[0] == -1)) {
    view.materialIndex = CoinRenderSpan<int32_t>(ils->materialIndex.getValues(0), static_cast<size_t>(ils->materialIndex.getNum()));
  }

  view.materialBinding = SoMaterialBindingElement::get(state);
  normalizeIndexedBindings(node, view);

  std::string err;
  CoinRenderFastPathResult res = p->builder.processIndexedLineSet(action, view, const_cast<SoNode *>(node), &err);

  if (vp) {
    state->pop();
  }

  if (res == CoinRenderFastPathResult::SUCCESS_PRUNE) {
    return SoCallbackAction::PRUNE;
  } else if (res == CoinRenderFastPathResult::INVALID_SCENE) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString(err.empty() ? "Invalid scene in IndexedLineSet" : err.c_str())));
    return SoCallbackAction::ABORT;
  } else if (res == CoinRenderFastPathResult::UNSUPPORTED) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString(err.empty() ? "Unsupported feature in IndexedLineSet" : err.c_str())));
    return SoCallbackAction::ABORT;
  } else {
    return SoCallbackAction::CONTINUE;
  }
}
