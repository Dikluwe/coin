// Native SoShape::shouldGLRender enables NORMAL in the sorted primitive
// cache even for OVERALL and BASE_COLOR. Annotation executes that geometry
// immediately in its delayed path, before its following Image occurrence.
bool sortedAnnotation() {
  bool ok = true;
  for (int mode : {SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND,
                   SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD})
    for (bool phong : {false, true}) for (int normals : {1, 2}) {
      Harness h; if (!h.target) return false;
      h.action.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(mode));
      Scene scene(2,7,true,false,true);
      auto *reset = pointHistory(false); reset->whichChild = SO_SWITCH_ALL;
      scene.root->insertChild(reset,scene.root->findChild(scene.model));
      Predecessor previous(1,normals,phong); previous.material->transparency=.5f;
      // Annotation inherits Scene's model. Undo it for the authored IFS,
      // whose finite triangles lie within the observer's frustum.
      auto *branch=static_cast<SoSeparator*>(previous.toggle->getChild(0));
      branch->insertChild(new SoResetTransform,0);
      auto *annotation=new SoAnnotation;
      annotation->addChild(previous.toggle);
      scene.image->ref();scene.root->removeChild(scene.image);annotation->addChild(scene.image);
      scene.image->unref();scene.root->addChild(annotation);
      const std::string label="sorted Annotation mode="+std::to_string(mode)+" phong="+std::to_string(phong)+" normals="+std::to_string(normals);
      if(!h.apply(scene,label+" initial defined Image")){ok=false;continue;}
      const Frame initial=h.read();reset->whichChild=SO_SWITCH_NONE;previous.toggle->whichChild=SO_SWITCH_ALL;
      ok &= h.refuse(scene,label+" actual sorted normal array precedes Annotation Image");
      scene.quality->textureQuality=0;
      ok &= h.apply(scene,label+" unconsumed sorted array may publish Unknown");
      previous.toggle->whichChild=SO_SWITCH_NONE;scene.quality->textureQuality=.1f;
      ok &= h.refuse(scene,label+" published sorted array history persists");
      reset->whichChild=SO_SWITCH_ALL;
      const bool recovered=h.apply(scene,label+" first OVERALL recovery");ok &= recovered;
      if(recovered){ok &= delivery(scene,h.backend->plan,false);const Frame final=h.read();
        ok &= check(initial.rgba==final.rgba&&initial.depth==final.depth,label+" exact known recovery");}
    }
  return ok;
}
// SoTextureCoordinateBundle inspects every ENABLED unit. FUNCTION, TEXGEN,
// default and EXPLICIT-empty all make isFunction() true. IFS therefore sends
// OVERALL normals even under BASE_COLOR. TEXGEN permits the normal-array VA
// path; that entry deliberately has no inferred last-array-value oracle.
bool textureUnits() {
  bool ok=true;
  for(int generator=0;generator<5;++generator) {
    const bool dormant=generator==4;
    for(bool arrays:{false,true}) {
      // CPU-generated function/default coordinates disable IFS's VA path.
      // Their OVERALL witness proves consumption without assigning array
      // semantics to a different native immediate path.
      if(arrays&&generator!=1&&!dormant)continue;
      Harness h;if(!h.target)return false;Scene scene(2,7,true,false,true);
      auto *reset=pointHistory(false);reset->whichChild=SO_SWITCH_ALL;
      scene.root->insertChild(reset,scene.root->findChild(scene.model));
      Predecessor previous(1,arrays?2:1,false);
      auto *branch=static_cast<SoSeparator*>(previous.toggle->getChild(0));
      auto appendBeforeShape=[&](SoNode *node){branch->insertChild(node,branch->getNumChildren()-1);};
      auto *unit=new SoTextureUnit;unit->unit=3;appendBeforeShape(unit);
      auto *texture=new SoTexture2;const uint8_t white[]={255,255,255,255};
      texture->image.setValue(SbVec2s(1,1),4,white);texture->model=SoTexture2::MODULATE;appendBeforeShape(texture);
      if(generator==0)appendBeforeShape(new SoTextureCoordinateCylinder); // FUNCTION natively
      if(generator==1)appendBeforeShape(new SoTextureCoordinateEnvironment); // TEXGEN natively
      if(generator==2)appendBeforeShape(new SoTextureCoordinateDefault);
      if(generator==3){auto *empty=new SoTextureCoordinate2;empty->point.setNum(0);appendBeforeShape(empty);}
      if(dormant){
        auto *explicitUv=new SoTextureCoordinate2;
        for(int i=0;i<36;++i)explicitUv->point.set1Value(i,SbVec2f(.25f,.25f));appendBeforeShape(explicitUv);
        auto *idleUnit=new SoTextureUnit;idleUnit->unit=4;appendBeforeShape(idleUnit);
        appendBeforeShape(new SoTextureCoordinateEnvironment); // No image enables unit4.
      }
      scene.root->insertChild(previous.toggle,scene.root->findChild(scene.model));
      // Only the later Image loses texture quality for the idle-consumer test;
      // the antecedent's unit3 stays enabled and still emits normal arrays.
      auto *imageQuality=new SoComplexity;imageQuality->textureQuality=.1f;
      scene.root->insertChild(imageQuality,scene.root->findChild(scene.model));
      const std::string label="IFS BASE_COLOR unit3 generator="+std::to_string(generator)+" arrays="+std::to_string(arrays);
      if(!h.apply(scene,label+" initial normalZ")){ok=false;continue;}
      const Frame initial=h.read();reset->whichChild=SO_SWITCH_NONE;previous.toggle->whichChild=SO_SWITCH_ALL;
      if(arrays&&!dormant) {
        ok &= h.refuse(scene,label+" enabled texgen consumes normal array and stays Unknown");
        imageQuality->textureQuality=0;ok &= h.apply(scene,label+" unknown history publishes with idle Image texgen");
        previous.toggle->whichChild=SO_SWITCH_NONE;imageQuality->textureQuality=.1f;
        ok &= h.refuse(scene,label+" published texture-driven Unknown persists");
      } else {
        const std::array<double,3> expected=dormant?std::array<double,3>{{0,0,1}}:std::array<double,3>{{0,1,0}};
        bool pass=h.apply(scene,label+" defined native normal emission or preservation");ok &= pass;
        if(pass)ok &= delivery(scene,h.backend->plan,false,expected);
        previous.toggle->whichChild=SO_SWITCH_NONE;
        pass=h.apply(scene,label+" next frame keeps actual emitted normal");ok &= pass;
        if(pass)ok &= delivery(scene,h.backend->plan,false,expected);
      }
      previous.toggle->whichChild=SO_SWITCH_NONE;reset->whichChild=SO_SWITCH_ALL;
      const bool recovered=h.apply(scene,label+" first OVERALL recovery");ok &= recovered;
      if(recovered){ok &= delivery(scene,h.backend->plan,false);const Frame final=h.read();
        ok &= check(initial.rgba==final.rgba&&initial.depth==final.depth,label+" exact A recovery");}
    }
  }
  return ok;
}
