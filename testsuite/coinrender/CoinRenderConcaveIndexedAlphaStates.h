// Included inside indexed_alpha. No new policies are inferred for strokes:
// the filled two-face control is what distinguishes triangle sorting.
bool singleCapture(Harness &h,Scene &s,Image &out,const char *label){
  if(!h.target)return false;const auto serial=h.target->getLastSubmissionSerial();const auto generated=s.derived->generated;
  h.action.apply(s.root);h.target->readbackRGBA(out.rgba);h.target->readbackDepth(out.depth);out.serial=h.target->getLastSubmissionSerial();
  return check(h.action.getLastStatus()==CoinRenderAction::SUCCESS&&out.serial>serial&&out.rgba.size()==size_t(side*side*4)&&out.depth.size()==size_t(side*side)&&s.derived->generated==generated+1,std::string(label)+": first publication and subclass override");
}
bool triangleControl(bool gpu){
  Scene actual(2),expected(0);actual.root->removeChild(actual.group[1]);actual.group[0]->removeChild(actual.shape);
  auto *faces=new ObservedAlphaIndexedFaceSet;actual.shape=faces;actual.derived=faces;actual.group[0]->addChild(faces);actual.drawstyle(SoDrawStyle::FILLED);
  actual.mb[0]->value=SoMaterialBinding::PER_FACE_INDEXED;actual.nb[0]->value=SoNormalBinding::OVERALL;
  actual.material[0]->diffuseColor.set1Value(0,colors[1]);actual.material[0]->diffuseColor.set1Value(1,colors[0]);
  actual.material[0]->transparency.set1Value(0,1-opacity[1]);actual.material[0]->transparency.set1Value(1,1-opacity[0]);
  for(int o=0;o<2;++o)for(int c=0;c<8;++c){SbVec3f p=outline[c];p[0]+=o?.125f:-.125f;p[2]=expected.z[o];actual.coords[0]->point.set1Value(8*o+ci[c],p);}
  int32_t uvIndices[18];for(int i=0;i<18;++i)uvIndices[i]=(i%9==8)?-1:0;faces->textureCoordIndex.setValues(0,18,uvIndices);
  Harness h(gpu),cpu(false);bool ok=true;
  for(int mode:{SoGLRenderAction::BLEND,SoGLRenderAction::SORTED_OBJECT_BLEND,SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND}){
    h.action.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(mode));cpu.action.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(mode));Image first,second;
    for(int phase=0;phase<3;++phase){const bool reversed=phase==1;expected.order(reversed);int32_t indices[18];for(int face=0;face<2;++face){const int o=reversed?1-face:face;for(int c=0;c<8;++c)indices[face*9+c]=8*o+ci[c];indices[face*9+8]=-1;faces->materialIndex.set1Value(face,1-o);}faces->coordIndex.setValues(0,18,indices);
      Image current,reference;const bool got=singleCapture(h,actual,current,"two concave faces in one indexed object");ok &= got;if(got)ok &= witness(current,expected,mode,SoDrawStyle::FILLED,true);
      if(gpu){const bool ref=singleCapture(cpu,actual,reference,"two face CPU");ok &= ref;if(got&&ref)ok &= closeImages(current,reference,"object versus triangle ordering");}
      if(!phase)first=current;else if(phase==1)second=current;else ok &= check(equalImage(first,current),"face-order A/B/A restores exact publication");
    }
    ok &= check(mode==SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND?equalImage(first,second):differing(first,second)>4,"triangle sorting is distinguished from object and literal face order");
  }return ok;
}
bool stateCases(bool gpu){bool ok=triangleControl(gpu);
  for(int style:{SoDrawStyle::LINES,SoDrawStyle::POINTS}){
    Scene s(2);s.drawstyle(style);Harness h(gpu),cpu(false);Observer o(s),co(s);o.install(h.action);co.install(cpu.action);
    h.action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);cpu.action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
    auto both=[&](Image &out,const char *label){const bool got=h.take(s,o,out,label);bool good=got;if(gpu){Image reference;const bool ref=cpu.take(s,co,reference,label);good &= ref;if(got&&ref)good &= closeImages(out,reference,label);}return good;};
    Image original,changed,restored;ok &= both(original,"authored depth order A");ok &= witness(original,s,SoGLRenderAction::SORTED_OBJECT_BLEND,style);
    s.z[0]=-.2f;s.z[1]=.2f;s.material[0]->transparency.setValue(.5f);s.material[1]->transparency.setValue(.75f);s.positions();
    ok &= both(changed,"authored depth/alpha B");ok &= witness(changed,s,SoGLRenderAction::SORTED_OBJECT_BLEND,style);ok &= check(differing(original,changed)>4,"depth and opacity edit is visible");
    s.z[0]=.2f;s.z[1]=-.2f;s.attributes(0,false);s.positions();ok &= both(restored,"authored depth/alpha restored A");ok &= check(equalImage(original,restored),"depth/alpha A/B/A exact recovery");
    SbViewportRegion inset(side,side);inset.setViewportPixels(12,12,72,72);h.action.setViewportRegion(inset);cpu.action.setViewportRegion(inset);ok &= both(changed,"inset viewport/scissor");
    unsigned inside=0,outside=0;for(int y=0;y<side;++y)for(int x=0;x<side;++x){const size_t p=pixel(x,y);if(x<12||x>=84||y<12||y>=84){++outside;ok &= check(changed.rgba[p]==0&&changed.rgba[p+1]==0&&changed.rgba[p+2]==0&&changed.rgba[p+3]==255&&changed.depth[p/4]==1,"viewport clips point/line extent before compositing outside pixels");}else if(changed.rgba[p]+changed.rgba[p+1]+changed.rgba[p+2]>0)++inside;}
    ok &= check(inside>4000&&outside==size_t(side*side-72*72),"inset retains authored interior and excludes outside area");
    h.action.setViewportRegion(SbViewportRegion(side,side));cpu.action.setViewportRegion(SbViewportRegion(side,side));ok &= both(restored,"viewport restore");ok &= check(equalImage(original,restored),"viewport A/B/A exact recovery");
    // A planar slope makes the expected offset derivative unambiguous; it
    // does not depend on which native ear triangles triangulate the U.
    s.slope=.15f;s.positions();h.action.setTransparencyType(CoinRenderAction::BLEND);cpu.action.setTransparencyType(CoinRenderAction::BLEND);for(int i=0;i<2;++i){s.offset[i]->on=TRUE;s.offset[i]->factor=0;}
    ok &= both(original,"alpha planar offset zero");
    for(float factor:{2.f,-2.f}){for(int i=0;i<2;++i)s.offset[i]->factor=factor;ok &= both(changed,"alpha planar signed offset");unsigned affected=0;const double expected=factor*.15/(9.9*48.);
      for(size_t p=0;p<original.depth.size();++p)if(std::abs(original.depth[p]-(3+.6-.1)/9.9)>1e-4){++affected;ok &= check(std::abs((changed.depth[p]-original.depth[p])-expected)<=2e-5,"authored signed planar slope offset depth");}
      ok &= check(affected>20,"offset witness includes real translucent fragments");
    }
    for(int i=0;i<2;++i)s.offset[i]->factor=0;ok &= both(restored,"alpha offset restore");ok &= check(equalImage(original,restored),"offset A/B/A exact recovery");
  }return ok;
}
