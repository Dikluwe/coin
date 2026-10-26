#include "actions/CoinRenderActionIncludesP.h"

#include "actions/CoinRenderRttCaptureMemoP.h"

static std::vector<uint64_t>
rttCaptureContext(const CoinRenderActionP & p, SoState * state,
                  int transparency, bool bumpProducer)
{
  const auto & options = p.executionOptions;
  // These are exactly the parent inputs installed on the private child action.
  return {uint64_t(options.renderer), uint64_t(options.transparency), uint64_t(options.sceneTexture),
    uint64_t(options.storedTextureColorSpace), uint64_t(options.maxTextureAnisotropy),
    uint64_t(options.textureSamplingPolicy), uint64_t(options.rasterizationPolicy), uint64_t(options.sampleCount),
    uint64_t(transparency), uint64_t(SoOverrideElement::getTransparencyTypeOverride(state)),
    uint64_t(p.transparencyOptions.layers), p.transparencyOptions.bufferBudget, uint64_t(bumpProducer)};
}

static bool rttNodeHasEffects(const SoNode * node)
{
  if (!rttNodeHasKnownTraversal(node) || node->isOfType(SoCallback::getClassTypeId()) ||
      node->isOfType(SoTextureCoordinateFunction::getClassTypeId())) return true;
  const SoFieldData * data = node->getFieldData();
  for (int i=0;data && i<data->getNumFields();++i) {
    const SoField * field = data->getField(node,i);
    if (field->isConnected()) return true;
    if (field->getTypeId() == SoSFImage::getClassTypeId()) {
      const SbImage & image = static_cast<const SoSFImage *>(field)->getValue();
      if (!image.hasData() || !(image == image)) return true;
    }
  }
  return false;
}

static bool rttSceneHasEffects(const SoNode * root, CoinRenderRttCaptureMemo::Baseline * baseline=NULL)
{
  std::vector<const SoNode *> pending(1,root);
  std::unordered_set<const SoNode *> seen;
  while (!pending.empty()) {
    const SoNode * node = pending.back(); pending.pop_back();
    if (!node || !seen.insert(node).second) continue;
    if (rttNodeHasEffects(node)) return true;
    if (baseline && baseline->complete) {
      // This bounds transient metadata only; it never changes scene admission.
      if (baseline->nodes.size()>=4096) { baseline->complete=false; baseline->nodes.clear(); }
      else {
        CoinRenderRttCaptureMemo::Identity identity;
        identity.node=node; identity.revision=node->getNodeId(); identity.overrideValue=node->isOverride();
        if (node->isOfType(SoCamera::getClassTypeId())) {
          const auto * camera=static_cast<const SoCamera *>(node);
          identity.stereoMode=camera->getStereoMode(); identity.stereoAdjustment=camera->getStereoAdjustment();
          identity.stereoBalance=camera->getBalanceAdjustment();
        }
        baseline->nodes.push_back(identity);
      }
    }
    const SoFieldData * data = node->getFieldData();
    for (int i=0;data && i<data->getNumFields();++i) {
      const SoField * field = data->getField(node,i);
      if (field->isOfType(SoSFNode::getClassTypeId()))
        pending.push_back(static_cast<const SoSFNode *>(field)->getValue());
      else if (field->isOfType(SoMFNode::getClassTypeId())) {
        const auto * refs=static_cast<const SoMFNode *>(field);
        for (int j=0;j<refs->getNum();++j) pending.push_back((*refs)[j]);
      }
    }
    const SoChildList * children=node->getChildren();
    for (int i=0;children && i<children->getLength();++i) pending.push_back((*children)[i]);
  }
  return false;
}

SoCallbackAction::Response
CoinRenderActionP::rttMutationPreCB(void * data, SoCallbackAction *, const SoNode * node)
{
  auto * p = static_cast<CoinRenderActionP *>(data);
  // An effect outside a producer can change state not represented by fields.
  // Invalidate earlier proofs instead of assuming a callback only touches its node.
  const auto memo = p->sceneTextureCaptureMemo;
  if (memo && memo->allowed && memo->owner &&
      memo->owner->master->callbackRegistrationRevision() != memo->owner->captureCallbackRevision) {
    // A callback may install more action observers while the apply is active.
    memo->allowed = false;
    memo->invalidate();
  }
  if (memo && memo->allowed && (!memo->entries.empty() || !memo->activeTraces.empty()) &&
      rttNodeHasEffects(node)) memo->invalidate();
  return SoCallbackAction::CONTINUE;
}

namespace {
struct CubeCameraOverride {
  const SoCamera * original;
  SoCamera * replacement;
  CoinRenderCameraSnapshot * captured;
  CoinRenderActionP * owner;
  static SoCallbackAction::Response post(void * data,SoCallbackAction * action,const SoNode * node) {
    auto * self=static_cast<CubeCameraOverride *>(data);
    if(node!=(self->original?static_cast<const SoNode *>(self->original):self->replacement))return SoCallbackAction::CONTINUE;
    if(self->replacement->getStereoMode()!=SoCamera::MONOSCOPIC) {
      // The ordinary post callback sees the original camera node. Rebuild its
      // stereo volume from the private face orientation before extracting the
      // GL reset matrix; no original camera fields are changed.
      self->replacement->doAction(action);
      if(CoinRenderActionP::cameraStereoPostCB(self->owner,action,self->replacement)!=SoCallbackAction::CONTINUE)
        return SoCallbackAction::ABORT;
    }
    SoState * state=action->getState();
    SbBool identity;
    const SbMatrix & model=SoModelMatrixElement::get(state,identity);
    SbMatrix view=SoViewingMatrixElement::get(state);
    if(!identity) {
      const float determinant=model.det4();
      if(!std::isfinite(determinant) || determinant==0) {
        self->owner->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
          CoinRenderDiagnosticDomain::FRAME_PLAN,SbString("Cube RTT camera path is non-invertible")));
        return SoCallbackAction::ABORT;
      }
      // Match SoGLViewingMatrixElement::getResetMatrix, including its exact
      // order. doAction stores V*M^-1; GL renders with reset M^-1*V so geometry
      // before and after SoResetTransform, and later local transforms, agree.
      view.multRight(model);
      view.multLeft(model.inverse());
      SoViewingMatrixElement::set(state,self->replacement,view);
    }
    self->captured->viewMatrix=view;
    self->captured->projectionMatrixCoin=SoProjectionMatrixElement::get(action->getState());
    self->captured->isPerspective=self->replacement->isOfType(SoPerspectiveCamera::getClassTypeId());
    self->captured->nearDistance=self->replacement->nearDistance.getValue();
    self->captured->farDistance=self->replacement->farDistance.getValue();
    self->captured->focalDistance=self->replacement->focalDistance.getValue();
    self->captured->aspectRatio=self->replacement->aspectRatio.getValue();
    return SoCallbackAction::CONTINUE;
  }
  static SoCallbackAction::Response apply(void * data, SoCallbackAction * action, const SoNode * node) {
    const auto * self=static_cast<const CubeCameraOverride *>(data);
    if (node!=self->original) return SoCallbackAction::CONTINUE;
    // Traverse only the private camera's fields at the original path/model
    // transform. No field, notification, connection or node ID of the scene
    // is modified, and all subsequent cameras retain their native behavior.
    self->replacement->doAction(action);
    return SoCallbackAction::PRUNE;
  }
};
SbRotation cubeRttOrientation(unsigned face) {
  const float pi=3.14159265358979323846f;
  switch(face) {
  case 0:return SbRotation(SbVec3f(0,1,0),-pi/2)*SbRotation(SbVec3f(1,0,0),pi);
  case 1:return SbRotation(SbVec3f(0,1,0), pi/2)*SbRotation(SbVec3f(1,0,0),pi);
  case 2:return SbRotation(SbVec3f(1,0,0), pi/2);
  case 3:return SbRotation(SbVec3f(1,0,0),-pi/2);
  case 4:return SbRotation(SbVec3f(0,1,0), pi)*SbRotation(SbVec3f(0,0,1),pi);
  default:return SbRotation(SbVec3f(0,0,1),pi);
  }
}
bool validCubeCamera(const SoCamera * camera) {
  if (!camera || !std::isfinite(camera->nearDistance.getValue()) ||
      !std::isfinite(camera->farDistance.getValue()) ||
      camera->farDistance.getValue()<=camera->nearDistance.getValue() ||
      !std::isfinite(camera->aspectRatio.getValue()) || camera->aspectRatio.getValue()<=0 ||
      !std::isfinite(camera->focalDistance.getValue())) return false;
  for(int c=0;c<3;++c)if(!std::isfinite(camera->position.getValue()[c]))return false;
  if(camera->isOfType(SoPerspectiveCamera::getClassTypeId())) {
    const float angle=static_cast<const SoPerspectiveCamera *>(camera)->heightAngle.getValue();
    if(!std::isfinite(angle) || angle<=0 || angle>=3.14159265358979323846f || camera->nearDistance.getValue()<=0)return false;
  }
  if(camera->isOfType(SoOrthographicCamera::getClassTypeId())) {
    const float height=static_cast<const SoOrthographicCamera *>(camera)->height.getValue();
    if(!std::isfinite(height)||height<=0)return false;
  }
  return true;
}
}

SoCallbackAction::Response
CoinRenderActionP::sceneCubeTexturePreCB(void * userdata,SoCallbackAction * action,const SoNode * node)
{
  auto * p=static_cast<CoinRenderActionP *>(userdata);
  const auto * texture=static_cast<const SoSceneTextureCubeMap *>(node);
  SoState * state=action?action->getState():NULL;
  const auto failWithStatus=[&](const std::string & text, CoinRenderAction::Status status) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(status,
      CoinRenderDiagnosticDomain::FRAME_PLAN,SbString(text.c_str())));
    return SoCallbackAction::ABORT;
  };
  const auto fail=[&](const std::string & text) {
    return failWithStatus(text, CoinRenderAction::UNSUPPORTED);
  };
  if(!state)return fail("Cube RTT has no traversal state");
  const int unit=SoTextureUnitElement::get(state);
  if(unit==0 && SoTextureOverrideElement::getImageOverride(state))return SoCallbackAction::CONTINUE;
  if(unit<0 || unit>=int(COIN_RENDER_MAX_TEXTURE_UNITS))return fail("Cube RTT supports units 0..7");
  CoinRenderTextureFilter filter;
  if(!CoinRenderTextureSamplingCore::quality(SoTextureQualityElement::get(state),filter))
    return fail("Cube RTT quality must be finite in [0,1]");
  if(!texture->scene.getValue() || SoTextureQualityElement::get(state)==0) {
    // Removal revokes the resource and inherited alpha classification.
    SoMultiTextureImageElement::set(state,const_cast<SoSceneTextureCubeMap *>(texture),unit,
      SbVec2s(0,0),0,NULL,SoMultiTextureImageElement::CLAMP,SoMultiTextureImageElement::CLAMP,
      SoMultiTextureImageElement::MODULATE,SbColor(0,0,0));
    SoMultiTextureEnabledElement::set(state,const_cast<SoSceneTextureCubeMap *>(texture),unit,FALSE);
    if(unit==0 && texture->isOverride())SoTextureOverrideElement::setImageOverride(state,TRUE);
    return SoCallbackAction::CONTINUE;
  }
  const int model=texture->model.getValue();
  if((model!=SoSceneTextureCubeMap::MODULATE && model!=SoSceneTextureCubeMap::REPLACE &&
      model!=SoSceneTextureCubeMap::DECAL && model!=SoSceneTextureCubeMap::BLEND) ||
     !coin_render_scene_texture_policy_supported(texture->transparencyFunction.getValue()) ||
     (texture->wrapS.getValue()!=SoSceneTextureCubeMap::REPEAT && texture->wrapS.getValue()!=SoSceneTextureCubeMap::CLAMP) ||
     (texture->wrapT.getValue()!=SoSceneTextureCubeMap::REPEAT && texture->wrapT.getValue()!=SoSceneTextureCubeMap::CLAMP) ||
     (texture->wrapR.getValue()!=SoSceneTextureCubeMap::REPEAT && texture->wrapR.getValue()!=SoSceneTextureCubeMap::CLAMP))
    return fail("Invalid cube RTT model, wrap or transparency policy");
  for(int c=0;c<3;++c)if(!std::isfinite(texture->backgroundColor.getValue()[c]) ||
      !std::isfinite(texture->blendColor.getValue()[c]))return fail("Non-finite cube RTT background/blend");
  const SbVec2s size=texture->size.getValue();
  CoinRenderTextureImageSnapshot description;
  if(size[0]!=size[1] || !CoinRenderCubeTextureCore::describe(uint32_t(size[0]),false,description))
    return fail("Cube RTT requires six square POT faces in 1..2048 within atlas limits");
  std::string diagnostic;
  if (!p->sceneTexturePlan) return fail("Missing cube RTT graph");
  const bool bumpProducer = p->sceneTextureBumpProducer ||
    (p->target && !p->planOnly && !p->asyncTicket && p->target->getPimpl()->supportsOpaqueBump(false));
  const int producerTransparency = SoShapeStyleElement::getTransparencyType(state);
  const auto context = rttCaptureContext(*p, state, producerTransparency, bumpProducer);
  uint64_t producerId = 0;
  bool dependencyCycle = CoinRenderRttCaptureMemo::activeSource(*p->sceneTexturePlan, texture->getNodeId());
  if (dependencyCycle) return failWithStatus("SoSceneTexture2 dependency cycle", CoinRenderAction::INVALID_SCENE);
  if (p->sceneTextureCaptureMemo &&
      !p->sceneTextureCaptureMemo->find(texture, context, *p->sceneTexturePlan, producerId, diagnostic, dependencyCycle))
    return failWithStatus(diagnostic, dependencyCycle ? CoinRenderAction::INVALID_SCENE : CoinRenderAction::UNSUPPORTED);
  if (!producerId) {
    CoinRenderRttCaptureMemo::Baseline baseline;
    const uint64_t captureEpoch = p->sceneTextureCaptureMemo ? p->sceneTextureCaptureMemo->epoch : 0;
    bool hasEffects = true;
    if (p->sceneTextureCaptureMemo && p->sceneTextureCaptureMemo->allowed)
      hasEffects = rttSceneHasEffects(texture, &baseline);
    if (hasEffects) { baseline.complete=false; baseline.nodes.clear(); }
    const size_t chargeBefore = p->sceneTexturePlan ? p->sceneTexturePlan->chargedBytes : 0;
    bool captureStable = true;
    if(!p->sceneTexturePlan->enter(texture->getNodeId(),SbVec2i32(size[0],size[1]),diagnostic,true)) return fail(diagnostic);
    struct GraphScope {CoinRenderRttPlan & graph;~GraphScope(){graph.leave();}} graphScope{*p->sceneTexturePlan};
    CoinRenderRttCaptureMemo::TraceScope trace(p->sceneTextureCaptureMemo.get(), texture->getNodeId(),
                                              p->sceneTexturePlan->activeSources.size());
    SoNode * scene=texture->scene.getValue();
    // Search/copy can invoke callbacks too. Release previous proofs before it,
    // so a callback's removals/destructors observe the original ownership.
    if (hasEffects && p->sceneTextureCaptureMemo) p->sceneTextureCaptureMemo->invalidate();
    CoinRenderGuardedTraversal<SoSearchAction> search;search.setType(SoCamera::getClassTypeId());search.setInterest(SoSearchAction::FIRST);search.apply(scene);
    const SoCamera * original=search.getPath()?static_cast<const SoCamera *>(search.getPath()->getTail()):NULL;
    SoCamera * camera=original?static_cast<SoCamera *>(original->copy(FALSE)):new SoPerspectiveCamera;
    camera->ref();
    SoSeparator * wrapper=NULL;
    struct CameraScope {SoCamera * camera;SoSeparator * & wrapper;~CameraScope(){if(wrapper)wrapper->unref();camera->unref();}} cameraScope{camera,wrapper};
    if(!original) {
      camera->position=SbVec3f(0,0,0);camera->nearDistance=.1f;camera->farDistance=100;
      static_cast<SoPerspectiveCamera *>(camera)->heightAngle=3.14159265358979323846f/2;
      wrapper=new SoSeparator;wrapper->ref();wrapper->addChild(camera);wrapper->addChild(scene);
    }
    if(original) {
      camera->setStereoMode(original->getStereoMode());
      camera->setStereoAdjustment(original->getStereoAdjustment());
      camera->setBalanceAdjustment(original->getBalanceAdjustment());
    }
    if(!validCubeCamera(camera))return fail("Invalid cube RTT camera parameters");
    CoinRenderRttProducer producer;producer.cube=true;producer.size=SbVec2i32(size[0],size[1]);
    producer.sourceRevision=texture->getNodeId();
    for(unsigned face=0;face<6;++face) {
      camera->orientation=cubeRttOrientation(face);
      CoinRenderAction child(SbViewportRegion(size[0],size[1]));
      child.pimpl->sceneTexturePlan=p->sceneTexturePlan;child.pimpl->planOnly=true;
      child.pimpl->sceneTextureCaptureMemo=p->sceneTextureCaptureMemo;
      child.pimpl->sceneTextureBumpProducer=bumpProducer;
      child.pimpl->executionOptions=p->executionOptions;
      child.pimpl->portableShaderModules=p->portableShaderModules;
      child.pimpl->inheritedTransparencyOverride=SoOverrideElement::getTransparencyTypeOverride(state);
      child.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(producerTransparency));
      child.setSortedLayersNumPasses(int(p->transparencyOptions.layers));child.setTransparencyBufferBudget(p->transparencyOptions.bufferBudget);
      const auto bg=texture->backgroundColor.getValue();child.setBackgroundColor(SbColor4f(bg[0],bg[1],bg[2],1));
      CoinRenderCameraSnapshot capturedCamera;
      CubeCameraOverride overrideCamera{original,camera,&capturedCamera,child.pimpl.operator->()};
      child.addPostCallback(SoCamera::getClassTypeId(),CubeCameraOverride::post,&overrideCamera);
      if(original)child.addPreCallback(SoCamera::getClassTypeId(),CubeCameraOverride::apply,&overrideCamera);
      child.apply(wrapper?static_cast<SoNode *>(wrapper):scene);
      if(child.getLastStatus()!=CoinRenderAction::SUCCESS)return fail(std::string("Cube RTT face capture: ")+child.getLastError().getString());
      captureStable = captureStable && !child.pimpl->builder.hasCoordinateFunctions();
      producer.face(face)=std::move(child.pimpl->lastValidPlan);producer.face(face).legacyBlendAlpha=true;
      if(producer.face(face).cameras.empty())producer.face(face).cameras.push_back(capturedCamera);
    }
    producer.capturedCubeFaces=6;
    if(!p->sceneTexturePlan->append(std::move(producer),producerId,diagnostic))return fail(diagnostic);
    if (p->sceneTextureCaptureMemo)
      p->sceneTextureCaptureMemo->remember(texture, context, baseline, trace.trace, captureEpoch, producerId,
                                          p->sceneTexturePlan->chargedBytes-chargeBefore, captureStable);
  }
  p->sceneTexturePixels.emplace_back(4,0);auto & marker=p->sceneTexturePixels.back();
  for(unsigned i=0;i<4;++i)marker[i]=uint8_t(producerId>>(i*8));
  SoMultiTextureImageElement::set(state,const_cast<SoSceneTextureCubeMap *>(texture),unit,SbVec2s(1,1),4,marker.data(),
    SoMultiTextureImageElement::CLAMP,SoMultiTextureImageElement::CLAMP,
    static_cast<SoMultiTextureImageElement::Model>(model),texture->blendColor.getValue());
  SbVec2s retained;int components;const auto * image=SoMultiTextureImageElement::getImage(state,unit,retained,components);
  if(!image||retained!=SbVec2s(1,1)||components!=4)return fail("Cube RTT marker was not retained");
  p->builder.registerSceneTexture(image,producerId,description.width,description.height,false,
    texture->transparencyFunction.getValue(),CoinRenderTextureFormat::RGBA8_LINEAR,uint32_t(size[0]));
  SoMultiTextureEnabledElement::enableCubeMap(state,const_cast<SoSceneTextureCubeMap *>(texture),unit);
  if(unit==0 && texture->isOverride())SoTextureOverrideElement::setImageOverride(state,TRUE);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::sceneTexturePreCB(void * userdata,
                                       SoCallbackAction * action,
                                       const SoNode * node)
{
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  const SoSceneTexture2 * texture = static_cast<const SoSceneTexture2 *>(node);
  SoState * state = action ? action->getState() : NULL;
  if (!state) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::BACKEND_ERROR, CoinRenderDiagnosticDomain::ACTION,
      SbString("SoSceneTexture2 has no traversal state")));
    return SoCallbackAction::ABORT;
  }
  if (SoTextureOverrideElement::getImageOverride(state)) {
    return SoCallbackAction::CONTINUE;
  }
  const int unit = SoTextureUnitElement::get(state);
  const int model = texture->model.getValue();
  const SoNode * transparency = texture->sceneTransparencyType.getValue();
  if (unit < 0 || unit >= static_cast<int>(COIN_RENDER_MAX_TEXTURE_UNITS) ||
      (texture->type.getValue() != SoSceneTexture2::RGBA8 && texture->type.getValue()!=SoSceneTexture2::RGBA16F) ||
      (model != SoSceneTexture2::MODULATE && model != SoSceneTexture2::REPLACE &&
       model != SoSceneTexture2::DECAL && model != SoSceneTexture2::BLEND) ||
      (texture->wrapS.getValue() != SoSceneTexture2::REPEAT &&
       texture->wrapS.getValue() != SoSceneTexture2::CLAMP) ||
      (texture->wrapT.getValue() != SoSceneTexture2::REPEAT &&
       texture->wrapT.getValue() != SoSceneTexture2::CLAMP) ||
      !coin_render_scene_texture_policy_supported(texture->transparencyFunction.getValue()) ||
      (transparency && !transparency->isOfType(SoTransparencyType::getClassTypeId()))) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString("SoSceneTexture2 supports units 0..7, RGBA8/RGBA16F, MODULATE/REPLACE/DECAL/BLEND, "
                 "REPEAT/CLAMP, NONE/ALPHA_BLEND/ALPHA_TEST and SoTransparencyType scene policy")));
    return SoCallbackAction::ABORT;
  }

  if (SoTextureQualityElement::get(state) <= 0.0f) {
    p->sceneTexturePixels.emplace_back(4, 0);
    SoMultiTextureImageElement::set(state, const_cast<SoSceneTexture2 *>(texture), unit,
      SbVec2s(1, 1), 4, p->sceneTexturePixels.back().data(),
      SoMultiTextureImageElement::REPEAT, SoMultiTextureImageElement::REPEAT,
      static_cast<SoMultiTextureImageElement::Model>(model), texture->blendColor.getValue());
    SbVec2s markerSize;
    int components;
    const auto * marker = SoMultiTextureImageElement::getImage(state, unit, markerSize, components);
    p->builder.registerSceneTexture(marker, 0, 1, 1, false,
                                    texture->transparencyFunction.getValue());
    SoMultiTextureEnabledElement::set(state, const_cast<SoSceneTexture2 *>(texture), unit, FALSE);
    return SoCallbackAction::CONTINUE;
  }

  const SbVec2s size = texture->size.getValue();
  // Disabled texturing returned above and does not consume a producer size.
  // Active nonpositive extents are malformed authorship; positive extents
  // still pass through the mode-specific capability/budget admission below.
  if (size[0] <= 0 || size[1] <= 0) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString("SoSceneTexture2 requires positive authored dimensions")));
    return SoCallbackAction::ABORT;
  }
  SoNode * scene = texture->scene.getValue();
  std::string diagnostic;
  const SbVec2i32 passSize(size[0], size[1]);
  const uint64_t sourceRevision = texture->getNodeId();
  const bool bumpProducer = p->target && !p->planOnly && !p->asyncTicket &&
    p->target->getPimpl()->supportsOpaqueBump(false);
  const int producerTransparency = transparency
    ? static_cast<const SoTransparencyType *>(transparency)->value.getValue()
    : SoShapeStyleElement::getTransparencyType(state);
  const auto context = rttCaptureContext(*p, state, producerTransparency, bumpProducer);
  uint64_t producerId = 0;
  std::vector<int> occurrencePath;
  if (p->technicalRttCapture) {
    const auto path = action->getCurPath()->fullPath();
    for (int i = 1; i < path.getLength(); ++i)
      occurrencePath.push_back(path.getIndexFromTail(path.getLength() - 1 - i));
  }
  if (p->technicalRttReplay)
    for (const auto & capture : p->technicalRttCaptures)
      if (capture.source == texture && capture.revision == texture->getNodeId() &&
          capture.context == context && capture.occurrencePath == occurrencePath) {
        producerId = capture.producer;
        break;
      }
  bool dependencyCycle = p->sceneTexturePlan &&
    CoinRenderRttCaptureMemo::activeSource(*p->sceneTexturePlan, sourceRevision);
  if (dependencyCycle) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("SoSceneTexture2 dependency cycle")));
    return SoCallbackAction::ABORT;
  }
  if (!producerId && p->sceneTexturePlan && p->sceneTextureCaptureMemo &&
      !p->sceneTextureCaptureMemo->find(texture, context, *p->sceneTexturePlan, producerId, diagnostic, dependencyCycle)) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      dependencyCycle ? CoinRenderAction::INVALID_SCENE : CoinRenderAction::UNSUPPORTED,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(diagnostic.c_str())));
    return SoCallbackAction::ABORT;
  }
  if (!producerId) {
    CoinRenderRttCaptureMemo::Baseline baseline;
    const uint64_t captureEpoch = p->sceneTextureCaptureMemo ? p->sceneTextureCaptureMemo->epoch : 0;
    bool hasEffects = true;
    if (p->sceneTextureCaptureMemo && p->sceneTextureCaptureMemo->allowed)
      hasEffects = rttSceneHasEffects(texture, &baseline);
    if (hasEffects) { baseline.complete=false; baseline.nodes.clear(); }
    const size_t chargeBefore = p->sceneTexturePlan ? p->sceneTexturePlan->chargedBytes : 0;
    if (!scene || !p->sceneTexturePlan ||
        !p->sceneTexturePlan->enter(sourceRevision, passSize, diagnostic)) {
      p->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString(diagnostic.empty() ? "SoSceneTexture2 requires a scene and dimensions in 1..2048"
                                      : diagnostic.c_str())));
      return SoCallbackAction::ABORT;
    }
    struct ActiveTextureScope {
      CoinRenderRttPlan& graph;
      ~ActiveTextureScope() { graph.leave(); }
    } scope{*p->sceneTexturePlan};
    CoinRenderRttCaptureMemo::TraceScope trace(p->sceneTextureCaptureMemo.get(), sourceRevision,
                                              p->sceneTexturePlan->activeSources.size());
    const SbVec4f background = texture->backgroundColor.getValue();
    CoinRenderAction childAction(SbViewportRegion(size[0], size[1]));
    childAction.pimpl->sceneTexturePlan = p->sceneTexturePlan;
    childAction.pimpl->sceneTextureCaptureMemo = p->sceneTextureCaptureMemo;
    childAction.pimpl->planOnly = true;
    // The qualified backend executes the child into private RTT attachments.
    // The consumer may be either an offscreen target or a native window; its
    // capability guard still excludes asynchronous/direct-output targets.
    childAction.pimpl->sceneTextureBumpProducer = bumpProducer;
    childAction.pimpl->inheritedTransparencyOverride = SoOverrideElement::getTransparencyTypeOverride(state);
    childAction.pimpl->executionOptions = p->executionOptions;
    childAction.pimpl->portableShaderModules = p->portableShaderModules;
    // The producer inherits the traversal policy, including preceding Coin nodes.
    // Its explicit SoTransparencyType overrides this policy exactly as in CoinGL.
    childAction.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(producerTransparency));
    childAction.setSortedLayersNumPasses(static_cast<int>(p->transparencyOptions.layers));
    childAction.setTransparencyBufferBudget(p->transparencyOptions.bufferBudget);
    childAction.setBackgroundColor(
        SbColor4f(background[0], background[1], background[2], background[3]));
    childAction.apply(scene);
    if (childAction.getLastStatus() != CoinRenderAction::SUCCESS) {
      p->setDiagnostic(CoinRenderDiagnosticShell::withContext(
        childAction.getLastStatus(), childAction.pimpl->lastDiagnosticDomain,
        "SoSceneTexture2 subscene", childAction.getLastError()));
      return SoCallbackAction::ABORT;
    }
    CoinRenderRttProducer producer;
    producer.plan = std::move(childAction.pimpl->lastValidPlan);
    producer.plan.legacyBlendAlpha = true;
    producer.size = passSize;
    producer.sourceRevision = sourceRevision;
    producer.format=texture->type.getValue()==SoSceneTexture2::RGBA16F?CoinRenderTextureFormat::RGBA16_FLOAT:CoinRenderTextureFormat::RGBA8_LINEAR;
    if (!p->sceneTexturePlan->append(std::move(producer), producerId, diagnostic)) {
      p->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
                                                         CoinRenderDiagnosticDomain::FRAME_PLAN,
                                                         SbString(diagnostic.c_str())));
      return SoCallbackAction::ABORT;
    }
    if (p->sceneTextureCaptureMemo)
      p->sceneTextureCaptureMemo->remember(texture, context, baseline, trace.trace, captureEpoch, producerId,
                                          p->sceneTexturePlan->chargedBytes-chargeBefore,
                                          !childAction.pimpl->builder.hasCoordinateFunctions());
  }
  if (p->technicalRttCapture && !p->technicalRttReplay)
    p->technicalRttCaptures.push_back({texture, texture->getNodeId(), context, occurrencePath, producerId});
  p->sceneTexturePixels.emplace_back(4, 0);
  auto& marker = p->sceneTexturePixels.back();
  for (unsigned int i = 0; i < 4; ++i)
    marker[i] = static_cast<uint8_t>(producerId >> (i * 8));
  SoMultiTextureImageElement::set(
      state, const_cast<SoSceneTexture2*>(texture), unit, SbVec2s(1, 1), 4, marker.data(),
      static_cast<SoMultiTextureImageElement::Wrap>(texture->wrapS.getValue()),
      static_cast<SoMultiTextureImageElement::Wrap>(texture->wrapT.getValue()),
      static_cast<SoMultiTextureImageElement::Model>(model), texture->blendColor.getValue());
  SbVec2s markerSize;
  int markerComponents = 0;
  SoMultiTextureImageElement::Wrap ws, wt;
  SoMultiTextureImageElement::Model retainedModel;
  SbColor blend;
  const unsigned char* image =
      SoMultiTextureImageElement::get(state, unit, markerSize, markerComponents, ws, wt, retainedModel, blend);
  if (!image || markerSize != SbVec2s(1, 1) || markerComponents != 4) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::BACKEND_ERROR, CoinRenderDiagnosticDomain::ACTION,
        SbString("SoSceneTexture2 logical marker was not retained by traversal state")));
    return SoCallbackAction::ABORT;
  }
  p->builder.registerSceneTexture(image, producerId, uint32_t(size[0]), uint32_t(size[1]),
                                  false, texture->transparencyFunction.getValue(),
                                  texture->type.getValue()==SoSceneTexture2::RGBA16F?CoinRenderTextureFormat::RGBA16_FLOAT:CoinRenderTextureFormat::RGBA8_LINEAR);
  SoMultiTextureEnabledElement::set(state, const_cast<SoSceneTexture2 *>(texture), unit, TRUE);
  return SoCallbackAction::CONTINUE;
}
