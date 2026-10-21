// Included within cube_overlay_test by the CPU/GPU contract executable.
bool sameExpanded(const CoinRenderFramePlan &a,const CoinRenderFramePlan &b){
  if(a.draws.size()!=b.draws.size())return false;
  const auto globals=[](const CoinRenderFramePlan &f){CoinRenderFramePlan p;p.clearColor=f.clearColor;p.transparency=f.transparency;p.cameras=f.cameras;p.viewports=f.viewports;p.lightingStates=f.lightingStates;p.shadowGroups=f.shadowGroups;p.shadowLights=f.shadowLights;p.textures=f.textures;p.samplers=f.samplers;return p;};
  if(!globals(a).hasSamePayload(globals(b)))return false;
  for(size_t i=0;i<a.draws.size();++i){
    const auto &x=a.draws[i],&y=b.draws[i];
    if(x.topology!=y.topology||x.geometry.indexCount!=y.geometry.indexCount||x.sourceNodeId!=y.sourceNodeId||x.sourceRevision!=y.sourceRevision||x.renderLayer!=y.renderLayer||x.clearDepthBefore!=y.clearDepthBefore||x.shadowLightSlot!=y.shadowLightSlot||x.lineStripId!=y.lineStripId||x.hasSortingCenter!=y.hasSortingCenter||std::memcmp(x.sortingCenterWorld,y.sortingCenterWorld,sizeof(x.sortingCenterWorld)))return false;
    auto sx=a.renderStates[x.renderStateSlot],sy=b.renderStates[y.renderStateSlot];const unsigned mx=sx.materialSlot,my=sy.materialSlot;sx.materialSlot=sy.materialSlot=0;
    if(!coin_render_same_state_except_camera(sx,sy)||sx.model!=sy.model||sx.view!=sy.view||sx.projectionCoin!=sy.projectionCoin||std::memcmp(&a.materials[mx],&b.materials[my],sizeof(CoinRenderMaterialSnapshot)))return false;
    for(unsigned j=0;j<x.geometry.indexCount;++j){auto vx=a.vertices[a.indices[x.geometry.firstIndex+j]],vy=b.vertices[b.indices[y.geometry.firstIndex+j]];const unsigned vmx=vx.materialSlot,vmy=vy.materialSlot;vx.materialSlot=vy.materialSlot=0;if(std::memcmp(&vx,&vy,sizeof(vx))||std::memcmp(&a.materials[vmx],&b.materials[vmy],sizeof(CoinRenderMaterialSnapshot)))return false;}
  }return true;
}
bool compareFull(Harness &h,Scene &scene,const std::string &label,bool requireSuccess=true){
  Harness full(h.target->getSize()[0]);if(!full.target)return false;
  full.action.setTransparencyType(h.action.getTransparencyType());
  const char *flags[]={"COIN_RENDER_DISABLE_TRANSLATION_OVERLAY","COIN_RENDER_DISABLE_MATERIAL_OVERLAY","COIN_RENDER_DISABLE_CUBE_OVERLAY"};
  for(const auto *flag:flags)coinRenderTestSetEnvironment(flag,"1");full.action.apply(scene.root);
  for(const auto *flag:flags)coinRenderTestSetEnvironment(flag,"0");
  const auto old=h.action.getPimpl()->lastValidPlan;const unsigned submissions=h.backend->submissions;h.action.apply(scene.root);const auto status=h.action.getLastStatus();
  bool ok=check(status==full.action.getLastStatus(),label+": admission equals fresh disabled-overlay capture");
  if(requireSuccess)ok &= check(status==CoinRenderAction::SUCCESS,label+": expected success: "+h.action.getLastError().getString());
  if(status==CoinRenderAction::SUCCESS)ok &= check(h.backend->submissions==submissions+1&&sameExpanded(h.action.getPimpl()->lastValidPlan,full.action.getPimpl()->lastValidPlan),label+": first-apply consumed payload equivalence");
  else ok &= check(h.backend->submissions==submissions&&h.action.getPimpl()->lastValidPlan.revision==old.revision&&h.action.getPimpl()->lastValidPlan.hasSamePayload(old),label+": refused capture preserves publication");return ok;
}
bool drawstyle(){
  Environment env;Scene scene;Harness h(128);if(!h.target)return false;auto *style=new SoDrawStyle;style->lineWidth=2;style->pointSize=3;scene.root->insertChild(style,3);
  bool ok=compareFull(h,scene,"initial filled drawstyle");
  for(int value:{SoDrawStyle::LINES,SoDrawStyle::POINTS,SoDrawStyle::INVISIBLE,SoDrawStyle::FILLED}){
    style->style=value;scene.shared->width=value==SoDrawStyle::FILLED?2:3.25f;scene.transform[2]->translation.setValue(3,.75f,0);scene.material[1]->diffuseColor.setValue(.625f,.25f,.875f);
    ok &= compareFull(h,scene,"drawstyle transition");const auto &p=h.action.getPimpl()->lastValidPlan;
    ok &= check(value==SoDrawStyle::INVISIBLE?p.draws.empty():!p.draws.empty(),"authored visibility style="+std::to_string(value)+" draws="+std::to_string(p.draws.size()));if(value==SoDrawStyle::FILLED)ok &= authoredBox(p,scene);
  }return ok;
}
bool pickOccurrence(Scene &scene,unsigned occurrence){
  auto *cube=occurrence==1?scene.unique:scene.shared;const SbVec3f translation=scene.transform[occurrence]->translation.getValue();const float localX=cube->width.getValue()*.125f;
  SoRayPickAction pick(SbViewportRegion(128,128));pick.setRadius(0);pick.setRay(SbVec3f(translation[0]+localX,translation[1],10),SbVec3f(0,0,-1),0,30);pick.apply(scene.root);const SoPickedPoint *hit=pick.getPickedPoint();
  if(!check(hit!=nullptr,"authored pick ray hits current box"))return false;
  bool ok=check(hit->getPath()->getTail()==cube&&hit->getPath()->getNodeFromTail(1)==scene.branch[occurrence],"pick preserves exact shared occurrence path");
  const SbVec3f expected(translation[0]+localX,translation[1],translation[2]+cube->depth.getValue()*.5f);
  ok &= check((hit->getPoint()-expected).length()<1e-5f&&hit->getObjectNormal()==SbVec3f(0,0,1)&&hit->getMaterialIndex()==0,"pick matches authored front position/normal/material");
  SoRayPickAction miss(SbViewportRegion(128,128));miss.setRadius(0);miss.setRay(SbVec3f(translation[0]+cube->width.getValue()*.5f+.25f,translation[1],10),SbVec3f(0,0,-1),0,30);miss.apply(scene.root);return check(miss.getPickedPoint()==nullptr,"ray beyond half-width misses separated boxes")&&ok;
}
bool picking(){
  Environment env;Scene scene;Harness h;if(!h.target)return false;Reference ref;SoCallbackAction native;ref.install(native);bool ok=true;
  for(unsigned i=0;i<3;++i){scene.transform[i]->rotation.setValue(SbVec3f(0,0,1),0);scene.transform[i]->scaleFactor.setValue(1,1,1);scene.transform[i]->translation.setValue(float(i)*5-5,0,0);}
  for(unsigned phase=0;phase<3;++phase){scene.shared->width=phase==1?3:2;scene.shared->depth=phase==1?2.5f:4;scene.unique->depth=phase==1?2:1;for(unsigned i=0;i<3;++i)ok &= pickOccurrence(scene,i);
    const auto rootRevision=scene.root->getNodeId(),shapeRevision=scene.shared->getNodeId();ok &= compare(h,scene,native,ref,"render/pick A/B/A");ok &= authoredBox(h.action.getPimpl()->lastValidPlan,scene);
    ok &= check(rootRevision==scene.root->getNodeId()&&shapeRevision==scene.shared->getNodeId(),"render preserves authored picking geometry/revisions");for(unsigned i=0;i<3;++i)ok &= pickOccurrence(scene,i);
  }return ok;
}
bool invalid(){
  Environment env;Scene scene;Harness h;if(!h.target)return false;Reference ref;SoCallbackAction native;ref.install(native);bool ok=compare(h,scene,native,ref,"invalid initial");
  for(unsigned mode=0;mode<5;++mode){const auto previous=h.action.getPimpl()->lastValidPlan;const unsigned submissions=h.backend->submissions;const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    if(mode==0)scene.shared->width=nan;if(mode==1)scene.shared->depth=inf;if(mode==2)scene.unique->height=-inf;if(mode==3)scene.transform[0]->translation.setValue(inf,0,0);if(mode==4)scene.material[1]->transparency=nan;
    h.action.apply(scene.root);ok &= check(h.action.getLastStatus()!=CoinRenderAction::SUCCESS&&h.backend->submissions==submissions&&h.action.getPimpl()->lastValidPlan.revision==previous.revision&&h.action.getPimpl()->lastValidPlan.hasSamePayload(previous),"consumed nonfinite input refuses without publication");
    scene.shared->width=2;scene.shared->depth=4;scene.unique->height=2;scene.transform[0]->translation.setValue(-3,0,0);scene.material[1]->transparency=0;ok &= compare(h,scene,native,ref,"first-apply invalid recovery");
  }
  const auto previous=h.action.getPimpl()->lastValidPlan;auto *prefix=new SoTransform;prefix->scaleFactor.setValue(std::numeric_limits<float>::max(),1,1);scene.root->insertChild(prefix,1);h.action.apply(scene.root);
  ok &= check(h.action.getLastStatus()!=CoinRenderAction::SUCCESS&&h.action.getPimpl()->lastValidPlan.revision==previous.revision&&h.action.getPimpl()->lastValidPlan.hasSamePayload(previous),"finite transform multiplication overflow preserves publication");scene.root->removeChild(prefix);ok &= compare(h,scene,native,ref,"first-apply overflow recovery");
  // Keep the initial full-capture admission at finite extremes; impose no new threshold.
  for(float width:{std::numeric_limits<float>::denorm_min(),std::numeric_limits<float>::min(),1e8f,std::numeric_limits<float>::max()}){scene.shared->width=width;ok &= compareFull(h,scene,"finite extreme admission/payload",false);scene.shared->width=2;ok &= compare(h,scene,native,ref,"finite extreme recovery");}
  auto *invisible=new SoDrawStyle;invisible->style=SoDrawStyle::INVISIBLE;scene.root->insertChild(invisible,3);scene.shared->width=std::numeric_limits<float>::quiet_NaN();ok &= compareFull(h,scene,"unconsumed invisible invalid dimension");ok &= check(h.action.getPimpl()->lastValidPlan.draws.empty(),"invisible invalid geometry remains unconsumed");return ok;
}
bool materials(){
  Environment env;bool ok=true;
  for(unsigned mode=0;mode<4;++mode){Scene scene;Harness h;if(!h.target)return false;Reference ref;SoCallbackAction native;ref.install(native);auto *inherited=new SoMaterial;inherited->diffuseColor.setValue(.1875f,.5625f,.8125f);inherited->shininess=.625f;scene.root->insertChild(inherited,3);ok &= compare(h,scene,native,ref,"before empty/ignored material");auto *m=scene.material[1];
    if(mode==0){m->ambientColor.setNum(0);m->diffuseColor.setNum(0);m->specularColor.setNum(0);m->emissiveColor.setNum(0);m->shininess.setNum(0);m->transparency.setNum(0);}if(mode==1)m->diffuseColor.setNum(0);
    if(mode==2){m->diffuseColor.setIgnored(TRUE);m->diffuseColor.setValue(std::numeric_limits<float>::quiet_NaN(),.25f,.5f);}if(mode==3){m->shininess.setIgnored(TRUE);m->transparency.setIgnored(TRUE);m->shininess=std::numeric_limits<float>::infinity();m->transparency=std::numeric_limits<float>::quiet_NaN();}
    for(unsigned phase=0;phase<3;++phase){inherited->diffuseColor.setValue(phase==1?.75f:.1875f,.5625f,.8125f);inherited->shininess=phase==1?.375f:.625f;scene.shared->width=phase==1?3.25f:2;scene.unique->depth=phase==1?2:1;ok &= compare(h,scene,native,ref,"empty/ignored inherited A/B/A");ok &= check(!h.action.getPimpl()->materialByNode.count(m),"incomplete/ignored material has no unsafe writable overlay slot");}
  }return ok;
}
bool notifyBatch(){
  Environment env;Scene scene;Harness h;if(!h.target)return false;Reference ref;SoCallbackAction native;ref.install(native);bool ok=compare(h,scene,native,ref,"batch initial");auto *cube=scene.shared;auto *material=scene.material[2];auto *transform=scene.transform[0];
  for(unsigned phase=0;phase<3;++phase){const auto shapeRevision=cube->getNodeId(),materialRevision=material->getNodeId(),transformRevision=transform->getNodeId();const SbBool wn=cube->width.enableNotify(FALSE),cn=material->diffuseColor.enableNotify(FALSE),pn=transform->translation.enableNotify(FALSE);
    cube->width=9;cube->width=phase==1?3.25f:2;material->diffuseColor.setValue(.125f,.25f,.375f);material->diffuseColor.setValue(phase==1?.875f:.5f,.375f,.5f);transform->translation.setValue(9,8,7);transform->translation.setValue(phase==1?-2.5f:-3,0,0);cube->width.enableNotify(wn);material->diffuseColor.enableNotify(cn);transform->translation.enableNotify(pn);
    ok &= check(shapeRevision==cube->getNodeId()&&materialRevision==material->getNodeId()&&transformRevision==transform->getNodeId(),"notify-disabled batch keeps revisions until documented touch");
    // SoField::touch documents auditor notification after enableNotify batches.
    cube->width.touch();material->diffuseColor.touch();transform->translation.touch();ok &= compare(h,scene,native,ref,"first apply after documented batch notification");ok &= authoredBox(h.action.getPimpl()->lastValidPlan,scene);
  }return ok;
}
