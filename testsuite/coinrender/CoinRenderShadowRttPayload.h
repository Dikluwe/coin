#ifndef COIN_RENDER_SHADOW_RTT_PAYLOAD_H
#define COIN_RENDER_SHADOW_RTT_PAYLOAD_H
#include <tuple>
namespace {
bool c036SameCamera(const CoinRenderCameraSnapshot&a,const CoinRenderCameraSnapshot&b){
  return std::tie(a.viewMatrix,a.projectionMatrixCoin,a.isPerspective,a.nearDistance,a.farDistance,a.focalDistance,a.aspectRatio)==
    std::tie(b.viewMatrix,b.projectionMatrixCoin,b.isPerspective,b.nearDistance,b.farDistance,b.focalDistance,b.aspectRatio);
}
bool c036SameGroups(const CoinRenderFramePlan&a,const CoinRenderFramePlan&b){
  if(a.shadowGroups.size()!=b.shadowGroups.size())return false;
  for(size_t i=0;i<a.shadowGroups.size();++i){const auto&x=a.shadowGroups[i];const auto&y=b.shadowGroups[i];
    if(std::tie(x.sourceRevision,x.intensity,x.precision,x.quality,x.epsilon,x.threshold,x.smoothBorder,x.shadowCachingEnabled,x.visibilityNearRadius,x.visibilityRadius,x.visibilityFlag,x.parentGroupSlot,x.hasEntryCamera,x.entryModel)!=
       std::tie(y.sourceRevision,y.intensity,y.precision,y.quality,y.epsilon,y.threshold,y.smoothBorder,y.shadowCachingEnabled,y.visibilityNearRadius,y.visibilityRadius,y.visibilityFlag,y.parentGroupSlot,y.hasEntryCamera,y.entryModel)||!c036SameCamera(x.entryCamera,y.entryCamera))return false;
  }return true;
}
bool c036SameShadowLights(const CoinRenderFramePlan&a,const CoinRenderFramePlan&b){
  if(a.shadowLights.size()!=b.shadowLights.size())return false;
  for(size_t i=0;i<a.shadowLights.size();++i){const auto&x=a.shadowLights[i];const auto&y=b.shadowLights[i];
    if(std::tie(x.groupSlot,x.sourceRevision,x.type,x.enabled,x.shadowEligible,x.hasCustomScene,x.mapSceneCaptured,x.customSceneNodeId,x.color,x.intensity,x.attenuation,x.model,x.modelViewAtLight,x.position,x.direction,x.cutOffAngle,x.dropOffRate,x.nearDistance,x.farDistance,x.maxShadowDistance,x.bboxCenter,x.bboxSize)!=
       std::tie(y.groupSlot,y.sourceRevision,y.type,y.enabled,y.shadowEligible,y.hasCustomScene,y.mapSceneCaptured,y.customSceneNodeId,y.color,y.intensity,y.attenuation,y.model,y.modelViewAtLight,y.position,y.direction,y.cutOffAngle,y.dropOffRate,y.nearDistance,y.farDistance,y.maxShadowDistance,y.bboxCenter,y.bboxSize))return false;
  }return true;
}
bool c036CapturePayload(){
  C036Groups scene;scene.mode(SoGLRenderAction::SORTED_OBJECT_BLEND);bool ok=true;
  const auto capture=[&](CoinRenderFramePlan&out){CoinRenderAction action(SbViewportRegion(64,64));action.getPimpl()->planOnly=true;
    auto options=samplingTestOptions();options.sceneTexture=COIN_RENDER_SCENE_TEXTURE_DIRECT;options.transparency=COIN_RENDER_TRANSPARENCY_OBJECT;action.getPimpl()->executionOptions=options;
    action.getPimpl()->sceneTexturePlan=std::make_shared<CoinRenderRttPlan>(COIN_RENDER_SCENE_TEXTURE_DIRECT);action.apply(scene.root.get());out=action.getPimpl()->lastValidPlan;
    std::string why;return check(action.getLastStatus()==CoinRenderAction::SUCCESS&&out.isValid(&why)&&out.shadowGroups.size()==2&&out.shadowLights.size()==2,"CPU real shadow-group capture remains valid: "+why);};
  CoinRenderFramePlan a,b;if(!capture(a))return false;
  const auto appendPair=[&](const CoinRenderFramePlan&first,const CoinRenderFramePlan&second,bool same,CoinRenderTextureFormat otherFormat=CoinRenderTextureFormat::RGBA8_LINEAR){
    CoinRenderRttPlan graph(COIN_RENDER_SCENE_TEXTURE_DIRECT);CoinRenderRttProducer producer;producer.plan=first;producer.size=SbVec2i32(64,64);producer.sourceRevision=1;
    std::string why;uint64_t firstId=0,secondId=0;bool accepted=graph.append(producer,firstId,why);producer.plan=second;producer.format=otherFormat;accepted &= graph.append(producer,secondId,why);
    std::cout<<"c036_payload_append same_expected="<<same<<" first="<<firstId<<" second="<<secondId<<" producers="<<graph.producers.size()<<" format="<<int(otherFormat)<<" error="<<why<<'\n';
    return check(accepted&&firstId==1&&secondId==uint64_t(same?1:2)&&graph.producers.size()==size_t(same?1:2),"CPU RTT payload key reuses equal captures and never fuses changed semantics/format");};
  for(int iteration=0;iteration<8;++iteration){if(!capture(b)){ok=false;continue;}
    const bool groups=c036SameGroups(a,b),lights=c036SameShadowLights(a,b);auto onlyGroups=b,onlyLights=b,both=b;
    onlyGroups.shadowGroups=a.shadowGroups;onlyLights.shadowLights=a.shadowLights;both.shadowGroups=a.shadowGroups;both.shadowLights=a.shadowLights;
    std::cout<<"c036_payload iteration="<<iteration<<" same="<<a.hasSamePayload(b)<<" semantic_groups="<<groups<<" semantic_lights="<<lights
      <<" replace_groups="<<a.hasSamePayload(onlyGroups)<<" replace_lights="<<a.hasSamePayload(onlyLights)<<" replace_both="<<a.hasSamePayload(both)<<'\n';
    ok &= check(groups&&lights,"unchanged real capture preserves all named shadow group/light fields including camera/slots");
    ok &= check(a.hasSamePayload(b),"consecutive semantically identical real shadow captures have equal payload");ok &= appendPair(a,b,true);
  }
  const auto changed=[&](const CoinRenderFramePlan&before,const char*label){CoinRenderFramePlan altered;const bool captured=capture(altered);if(!captured)return false;
    // Node revisions are cache metadata. Hold those equal in this additional
    // adversarial copy so they cannot hide a missing semantic field compare.
    auto normalized=altered;
    if(normalized.shadowGroups.size()==before.shadowGroups.size())for(size_t i=0;i<normalized.shadowGroups.size();++i)normalized.shadowGroups[i].sourceRevision=before.shadowGroups[i].sourceRevision;
    if(normalized.shadowLights.size()==before.shadowLights.size())for(size_t i=0;i<normalized.shadowLights.size();++i)normalized.shadowLights[i].sourceRevision=before.shadowLights[i].sourceRevision;
    std::cout<<"c036_payload_changed kind="<<label<<" same="<<before.hasSamePayload(altered)<<" revision_neutral_same="<<before.hasSamePayload(normalized)
      <<" semantic_groups="<<c036SameGroups(before,normalized)<<" semantic_lights="<<c036SameShadowLights(before,normalized)<<'\n';
    return check(!before.hasSamePayload(altered)&&!before.hasSamePayload(normalized),std::string("real authored ")+label+" change remains distinct independently of revision metadata")&&appendPair(before,normalized,false);};
  CoinRenderFramePlan before;
  ok &= capture(before);scene.left.group->precision=.125f;ok &= changed(before,"group precision");scene.left.group->precision=.0625f;
  ok &= capture(before);const float intensity=scene.left.spots[0]->intensity.getValue();scene.left.spots[0]->intensity=intensity*.5f;ok &= changed(before,"light intensity");scene.left.spots[0]->intensity=intensity;
  ok &= capture(before);const float nearDistance=scene.left.spots[0]->nearDistance.getValue();scene.left.spots[0]->nearDistance=nearDistance*.5f;ok &= changed(before,"shadow light near distance");scene.left.spots[0]->nearDistance=nearDistance;
  ok &= capture(before);auto*camera=static_cast<SoOrthographicCamera*>(scene.root->getChild(0));const auto position=camera->position.getValue();camera->position.setValue(0,0,7);ok &= changed(before,"camera");camera->position=position;
  const auto unit7Bytes=[&](const CoinRenderFramePlan&frame){for(const auto&draw:frame.draws)if(!draw.shadowLightSlot&&draw.sourceNodeId==scene.left.mesh->getNodeId()){
    const auto unit=coin_render_texture_unit(frame.renderStates[draw.renderStateSlot],7);if(unit.enabled&&unit.imageSlot<frame.textures.size())return frame.textures[unit.imageSlot].pixelsRgba;
  }return std::vector<uint8_t>{};};
  const uint8_t opaque[]={255,255,255,255};
  for(int channel:{0,3}){scene.left.images[7]->image.setValue(SbVec2s(1,1),4,opaque);ok &= capture(before);
    uint8_t pixel[]={255,255,255,255};pixel[channel]=128;scene.left.images[7]->image.setValue(SbVec2s(1,1),4,pixel);
    CoinRenderFramePlan actual;ok &= capture(actual);
    ok &= check(unit7Bytes(before)==std::vector<uint8_t>(opaque,opaque+4)&&unit7Bytes(actual)==std::vector<uint8_t>(pixel,pixel+4),
      "authored unit7 RGB/alpha mutation changes the exact normalized RGBA8 payload before identity comparison");
    ok &= changed(before,channel==0?"effective unit7 red":"effective unit7 alpha");
  }
  // Isolate semantic equality from every capture/revision side effect. Each
  // copy below changes exactly one named field of the same real valid plan.
  const auto isolated=[&](const CoinRenderFramePlan&copy,const char*field){std::string why;const bool valid=copy.isValid(&why);
    std::cout<<"c036_payload_isolated field="<<field<<" valid="<<valid<<" same="<<a.hasSamePayload(copy)<<" diagnostic="<<why<<'\n';
    return check(valid&&!a.hasSamePayload(copy),std::string("isolated real-plan field ")+field+" remains a distinct valid payload")&&appendPair(a,copy,false);};
  if(check(!a.shadowGroups.empty()&&!a.shadowLights.empty()&&a.shadowGroups[0].hasEntryCamera,"isolated payload witnesses have captured groups/lights and entry camera")){
    auto copy=a;copy.shadowGroups[0].precision=.125f;ok &= isolated(copy,"shadowGroups.precision");
    copy=a;copy.shadowGroups[0].quality=.2f;ok &= isolated(copy,"shadowGroups.quality");
    copy=a;copy.shadowLights[0].nearDistance=.5f;ok &= isolated(copy,"shadowLights.nearDistance");
    copy=a;copy.shadowGroups[0].entryCamera.nearDistance=.5f;ok &= isolated(copy,"shadowGroups.entryCamera.nearDistance");
    copy=a;copy.shadowGroups[0].entryCamera.viewMatrix.setTranslate(SbVec3f(.25f,0,0));ok &= isolated(copy,"shadowGroups.entryCamera.viewMatrix");
    copy=a;copy.shadowGroups[0].entryModel.setTranslate(SbVec3f(.25f,0,0));ok &= isolated(copy,"shadowGroups.entryModel");
  }else ok=false;
  // Format is part of the producer descriptor independently of Coin node IDs.
  // Use the same real captured scene to ensure format alone cannot alias.
  ok &= appendPair(a,a,false,CoinRenderTextureFormat::RGBA16_FLOAT);
  return ok;
}
}
#endif
