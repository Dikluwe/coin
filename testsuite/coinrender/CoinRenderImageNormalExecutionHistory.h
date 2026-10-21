// Included in namespace antecedents. History follows execution scheduling.
SoSwitch *scheduledPoint(bool arrays) {
  auto *toggle=pointHistory(arrays);auto *branch=static_cast<SoSeparator*>(toggle->getChild(0));
  auto *material=new SoMaterial;material->transparency=.5f;branch->insertChild(material,1);
  if(!arrays) {
    // pointHistory(false) supplies a normal element before the native point.
    for(int i=0;i<branch->getNumChildren();++i)if(branch->getChild(i)->getTypeId()==SoNormal::getClassTypeId())
      static_cast<SoNormal*>(branch->getChild(i))->vector.setValue(0,1,0);
  }
  return toggle;
}
bool deferred() {
  bool ok=true;
  for(int mode:{SoGLRenderAction::BLEND,SoGLRenderAction::DELAYED_BLEND,SoGLRenderAction::SORTED_OBJECT_BLEND})for(bool arrays:{false,true}) {
    Harness h;if(!h.target)return false;h.action.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(mode));Scene scene(2,7,true,false,true);
    auto *reset=pointHistory(false),*previous=scheduledPoint(arrays);reset->whichChild=SO_SWITCH_ALL;
    scene.root->insertChild(reset,scene.root->findChild(scene.model));scene.root->insertChild(previous,scene.root->findChild(scene.model));
    if(!h.apply(scene,"execution-order initial normalZ")){ok=false;continue;}
    const Frame first=h.read();previous->whichChild=SO_SWITCH_ALL;
    const bool delayed=mode!=SoGLRenderAction::BLEND;
    const std::string label="scheduled mode="+std::to_string(mode)+" arrays="+std::to_string(arrays);
    if(arrays&&!delayed)ok &= h.refuse(scene,label+" immediate Unknown precedes Image and refuses");
    else {
      const bool submitted=h.apply(scene,label+" Image sees only previously executed geometry");ok &= submitted;
      if(submitted)ok &= delivery(scene,h.backend->plan,false,delayed?std::array<double,3>{{0,0,1}}:std::array<double,3>{{0,1,0}});
      reset->whichChild=SO_SWITCH_NONE;previous->whichChild=SO_SWITCH_NONE;
      if(arrays)ok &= h.refuse(scene,label+" deferred array actually executed at frame end leaves Unknown for next frame");
      else {const bool persistent=h.apply(scene,label+" completed deferred OVERALL establishes normalY for next frame");ok &= persistent;if(persistent)ok &= delivery(scene,h.backend->plan,false,{{0,1,0}});}
    }
    previous->whichChild=SO_SWITCH_NONE;reset->whichChild=SO_SWITCH_ALL;
    const bool restored=h.apply(scene,label+" first defined recovery");ok &= restored;
    if(restored){const Frame f=h.read();ok &= delivery(scene,h.backend->plan,false);ok &= check(first.rgba==f.rgba&&first.depth==f.depth,label+" exact A recovery");}
    // A deferred array in a pure capture must not poison the next real frame.
    if(delayed&&arrays) {
      previous->whichChild=SO_SWITCH_ALL;h.action.getPimpl()->executionOptions=h.target->getOptions();h.action.getPimpl()->planOnly=true;
      const Frame before=h.read();const unsigned calls=h.backend->calls;h.action.apply(scene.root);
      ok &= check(h.action.getLastStatus()==CoinRenderAction::SUCCESS&&h.backend->calls==calls&&h.target->getLastSubmissionSerial()==before.serial,label+" deferred planOnly has no execution or publication");
      h.action.getPimpl()->planOnly=false;previous->whichChild=SO_SWITCH_NONE;reset->whichChild=SO_SWITCH_NONE;
      const bool next=h.apply(scene,label+" deferred planOnly must not commit Unknown");ok &= next;if(next)ok &= delivery(scene,h.backend->plan,false);
    }
  }
  return ok;
}
struct CulledPredecessor {
  SoSwitch *toggle=new SoSwitch;SoSeparator *branch=new SoSeparator;
  SoTransform *move=new SoTransform;SoLightModel *lighting=new SoLightModel;
  CulledPredecessor(bool phong){toggle->whichChild=SO_SWITCH_ALL;toggle->addChild(branch);branch->renderCaching=SoSeparator::ON;branch->boundingBoxCaching=SoSeparator::ON;
    lighting->model=phong?SoLightModel::PHONG:SoLightModel::BASE_COLOR;branch->addChild(lighting);move->translation.setValue(-20,0,0);branch->addChild(move);branch->addChild(new SoCube);}
};
bool culled() {
  bool ok=true;
  for(bool phong:{false,true}) {
    Harness h;if(!h.target)return false;Scene scene(2,7,true,false,true);auto *reset=pointHistory(false);reset->whichChild=SO_SWITCH_ALL;
    scene.root->insertChild(reset,scene.root->findChild(scene.model));CulledPredecessor previous(phong);scene.root->insertChild(previous.toggle,scene.root->findChild(scene.model));
    const std::array<double,3> expected=phong?std::array<double,3>{{0,-1,0}}:std::array<double,3>{{0,0,1}};
    if(!h.apply(scene,"cache sequence visible A")){ok=false;continue;}ok &= delivery(scene,h.backend->plan,false,expected);const Frame first=h.read();
    previous.move->translation.setValue(80,0,0);
    if(phong) {
      // A wholly culled native shape may skip glNormal depending on cache.
      // We never query GL_CURRENT_NORMAL to turn that ambiguity into a value.
      ok &= h.refuse(scene,"outside-frustum PHONG emission cannot be inferred from capture or corner order");
      reset->whichChild=SO_SWITCH_NONE;previous.toggle->whichChild=SO_SWITCH_NONE;
      const bool rollback=h.apply(scene,"culled refusal does not commit the candidate reset normalZ");ok &= rollback;if(rollback)ok &= delivery(scene,h.backend->plan,false,expected);
      previous.toggle->whichChild=SO_SWITCH_ALL;reset->whichChild=SO_SWITCH_ALL;
    } else {
      // No path through this untextured BASE_COLOR cube emits normals: cull
      // cache presence cannot turn preservation into an indeterminate value.
      const bool outside=h.apply(scene,"outside-frustum unconsumed normal preserves history");ok &= outside;if(outside)ok &= delivery(scene,h.backend->plan,false,expected);
    }
    previous.move->translation.setValue(-20,0,0);const bool restore=h.apply(scene,"cache sequence visible A restored");ok &= restore;
    if(restore){ok &= delivery(scene,h.backend->plan,false,expected);const Frame f=h.read();ok &= check(first.rgba==f.rgba&&first.depth==f.depth,"visible/outside/visible A/B/A exact restoration without extra warmup frames");}
  }
  // Published ambiguous execution remains Unknown even after a later shape
  // whose every native path preserves current normal.
  Harness h;if(!h.target)return false;Scene scene(2,7,true,false,true);auto *reset=pointHistory(false);reset->whichChild=SO_SWITCH_ALL;scene.root->insertChild(reset,scene.root->findChild(scene.model));CulledPredecessor previous(true);scene.root->insertChild(previous.toggle,scene.root->findChild(scene.model));
  previous.toggle->whichChild=SO_SWITCH_NONE;if(!h.apply(scene,"culling history known initial"))return false;const Frame initial=h.read();
  reset->whichChild=SO_SWITCH_NONE;previous.toggle->whichChild=SO_SWITCH_ALL;previous.move->translation.setValue(80,0,0);scene.quality->textureQuality=0;
  ok &= h.apply(scene,"ambiguous culled history is idle without a normal consumer");
  previous.lighting->model=SoLightModel::BASE_COLOR;scene.quality->textureQuality=.1f;
  ok &= h.refuse(scene,"culled BASE_COLOR preserves published Unknown rather than inventing normalZ");
  previous.toggle->whichChild=SO_SWITCH_NONE;reset->whichChild=SO_SWITCH_ALL;
  const bool recovery=h.apply(scene,"OVERALL recovers after idle ambiguous culling");ok &= recovery;
  if(recovery){ok &= delivery(scene,h.backend->plan,false);const Frame f=h.read();ok &= check(initial.rgba==f.rgba&&initial.depth==f.depth,"culling history first valid recovery exact RGBA/depth");}
  return ok;
}
