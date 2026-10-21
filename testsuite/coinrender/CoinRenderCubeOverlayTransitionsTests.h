// Included inside cube_overlay_test. These states may require a full capture;
// correctness is checked without requiring retention of any overlay proof.
bool currentNative(Harness &h,Scene &scene,SoCallbackAction &native,Reference &ref,const std::string &label){
  ref.corners.clear();native.apply(scene.root);
  return payload(h.action.getPimpl()->lastValidPlan,ref,label)&&authoredBox(h.action.getPimpl()->lastValidPlan,scene);
}
bool transforms(){
  Environment env;bool ok=true;
  const char *names[]={"rotation","scaleFactor","scaleOrientation","center","combined","singular scale"};
  for(unsigned mode=0;mode<6;++mode){
    Scene scene;Harness h;if(!h.target)return false;Reference ref;SoCallbackAction native;ref.install(native);
    std::array<SbRotation,3> rotation,orientation;std::array<SbVec3f,3> scale,center;
    for(unsigned i=0;i<3;++i){rotation[i]=scene.transform[i]->rotation.getValue();orientation[i]=scene.transform[i]->scaleOrientation.getValue();scale[i]=scene.transform[i]->scaleFactor.getValue();center[i]=scene.transform[i]->center.getValue();}
    const std::string label=std::string("nontranslation ")+names[mode];
    ok &= compare(h,scene,native,ref,label+" initial A");ok &= authoredBox(h.action.getPimpl()->lastValidPlan,scene);
    const auto original=ref.corners;
    if(!check(original.size()==108,label+": three initial occurrences"))return false;
    ok &= check(original[0].model!=original[72].model,label+": shared shape has distinct occurrence transforms");
    for(unsigned phase=0;phase<2;++phase){const bool changed=phase==0;
      for(unsigned i=0;i<3;++i){auto *t=scene.transform[i];
        if(mode==0||mode==4)t->rotation.setValue(changed?SbRotation(SbVec3f(2,-1,3),.5f+.125f*i):rotation[i]);
        if(mode==1||mode==4||mode==5)t->scaleFactor.setValue(changed?SbVec3f(mode==5&&i==0?0.f:-.5f-.25f*i,1.75f+.125f*i,.75f+.25f*i):scale[i]);
        if(mode==2||mode==4)t->scaleOrientation.setValue(changed?SbRotation(SbVec3f(2,1,-3),.375f+.125f*i):orientation[i]);
        if(mode==3||mode==4)t->center.setValue(changed?SbVec3f(.5f+.25f*i,-.375f,.25f+.125f*i):center[i]);
      }
      ok &= compareFull(h,scene,label+(changed?" B":" restored A"));
      ok &= currentNative(h,scene,native,ref,label);
      if(!check(ref.corners.size()==original.size(),label+": native corner count remains authored"))return false;
      for(unsigned i=0;i<3;++i)ok &= check(changed?ref.corners[36*i].model!=original[36*i].model:ref.corners[36*i].model==original[36*i].model,label+": authored model changes/restores every distinct occurrence");
    }
  }return ok;
}
bool materialTransitions(){
  Environment env;bool ok=true;
  const char *names[]={"ambient","specular","emissive","opaque-transparent-opaque"};
  for(unsigned mode=0;mode<4;++mode){
    Scene scene;Harness h;if(!h.target)return false;Reference ref;SoCallbackAction native;ref.install(native);
    if(mode==3)h.action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
    std::array<SbColor,3> ambient,specular,emissive,diffuse;std::array<float,3> shininess,transparency;
    for(unsigned i=0;i<3;++i){auto *m=scene.material[i];ambient[i]=m->ambientColor[0];specular[i]=m->specularColor[0];emissive[i]=m->emissiveColor[0];diffuse[i]=m->diffuseColor[0];shininess[i]=m->shininess[0];transparency[i]=m->transparency[0];}
    const std::string label=std::string("material transition ")+names[mode];
    ok &= compare(h,scene,native,ref,label+" initial A");ok &= authoredBox(h.action.getPimpl()->lastValidPlan,scene);
    for(unsigned phase=0;phase<2;++phase){const bool changed=phase==0;
      for(unsigned i=0;i<3;++i){auto *m=scene.material[i];
        // Isolated channel variants touch only that field, so an unrelated
        // transparency notification cannot conceal a stale material update.
        if(mode==0||mode==3)m->ambientColor.setValue(changed?SbColor(.625f,.125f+.125f*i,.25f):ambient[i]);
        if(mode==1||mode==3)m->specularColor.setValue(changed?SbColor(.25f,.75f,.375f+.125f*i):specular[i]);
        if(mode==2||mode==3)m->emissiveColor.setValue(changed?SbColor(.125f+.125f*i,.0625f,.1875f):emissive[i]);
        if(mode==3){m->diffuseColor.setValue(changed?SbColor(.75f-.125f*i,.1875f,.5625f):diffuse[i]);m->shininess=changed?.625f+.125f*i:shininess[i];m->transparency=changed?(i==1?0.f:.375f+.125f*i):transparency[i];}
      }
      ok &= compareFull(h,scene,label+(changed?" B":" restored A"));
      ok &= currentNative(h,scene,native,ref,label);
      if(mode==3){
        if(!check(ref.corners.size()==108,label+": mixed opacity keeps all authored occurrences"))return false;
        for(unsigned i=0;i<3;++i){const float alpha=changed?(i==1?1.f:1.f-(.375f+.125f*i)):1.f;ok &= check(ref.corners[36*i].alpha==alpha,label+": authored per-occurrence alpha changes/restores");}
      }
    }
  }return ok;
}
