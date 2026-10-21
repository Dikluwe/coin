// Included inside boundary; all expected lighting values come from authored inputs.
class PlanCaptureBackend : public CoinRenderRecordingBackend {
public:
  bool supportsOffscreenShadows() const override {return true;}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,CoinRenderTargetP&t) override {
    auto result=CoinRenderRecordingBackend::submit(f,t);
    const size_t n=size_t(t.size[0])*size_t(t.size[1]);
    t.colorBuffer.assign(4*n,0);t.depthBuffer.assign(n,1.f);return result;
  }
};
struct Visits {
  const SoNode *selected=nullptr,*hidden=nullptr,*directional=nullptr,*point=nullptr;
  unsigned selectedCount=0,hiddenCount=0,directionalCount=0,pointCount=0;
  static SoCallbackAction::Response pre(void*d,SoCallbackAction*,const SoNode*n) {
    auto &v=*static_cast<Visits*>(d);
    v.selectedCount+=n==v.selected;v.hiddenCount+=n==v.hidden;
    v.directionalCount+=n==v.directional;v.pointCount+=n==v.point;
    return SoCallbackAction::CONTINUE;
  }
  void clear(){selectedCount=hiddenCount=directionalCount=pointCount=0;}
  void install(SoCallbackAction&a){a.addPreCallback(SoNode::getClassTypeId(),pre,this);}
};
bool readPath(Harness&h,SoPath*path,Frame&frame) {
  auto &c=h.capture;const uint64_t before=c.target->getLastSubmissionSerial();
  c.action.apply(path);c.target->readbackRGBA(frame.rgba);c.target->readbackDepth(frame.depth);frame.serial=c.target->getLastSubmissionSerial();
  return require(c.action.getLastStatus()==CoinRenderAction::SUCCESS&&frame.serial>before&&frame.rgba.size()==96*96*4&&frame.depth.size()==96*96,
    std::string("selected path complete first apply: ")+c.action.getLastError().getString());
}
bool nonproducerPaths(bool gpu,bool canonical) {
  auto *root=new SoSeparator;root->ref();
  auto *camera=new SoOrthographicCamera;camera->position.setValue(0,0,6);camera->height=4;camera->nearDistance=1;camera->farDistance=10;root->addChild(camera);
  auto *material=new SoMaterial;material->diffuseColor.setValue(.5f,.25f,.125f);material->ambientColor.setValue(0,0,0);material->specularColor.setValue(0,0,0);root->addChild(material);
  auto *environment=new SoEnvironment;environment->ambientIntensity=0;root->addChild(environment);
  auto *group=new SoShadowGroup;group->quality=1;root->addChild(group);
  auto *directional=new SoDirectionalLight;directional->direction.setValue(0,0,-1);directional->intensity=.5f;group->addChild(directional);
  auto *point=new SoPointLight;point->location.setValue(0,0,4);point->intensity=.25f;group->addChild(point);
  auto *selected=new SoSeparator;group->addChild(selected);
  auto *coords=new SoCoordinate3;const SbVec3f points[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),SbVec3f(1,1,0),SbVec3f(-1,1,0)};coords->point.setValues(0,4,points);selected->addChild(coords);
  auto *face=new SoFaceSet;face->numVertices=4;selected->addChild(face);
  // A poisonous later sibling must stay unvisited, including callback side effects.
  auto *hidden=new SoSeparator;group->addChild(hidden);auto *bad=new SoCoordinate3;
  bad->point.setValue(std::numeric_limits<float>::quiet_NaN(),0,0);hidden->addChild(bad);
  auto *hiddenFace=new SoFaceSet;hiddenFace->numVertices=3;hidden->addChild(hiddenFace);
  auto *path=new SoPath(root);path->ref();path->append(root->findChild(group));path->append(group->findChild(selected));
  Visits authored;authored.selected=face;authored.hidden=hiddenFace;authored.directional=directional;authored.point=point;
  SoCallbackAction native;authored.install(native);native.apply(path);
  bool ok=require(authored.selectedCount==1&&authored.hiddenCount==0&&authored.directionalCount==1&&authored.pointCount==1,"native selected path visits exactly one shape and both inherited lights");
  Harness h(gpu,canonical),cpu(false,canonical);
  if(!gpu)h.capture.target->getPimpl()->backend.reset(new PlanCaptureBackend);
  Visits actual=authored;actual.install(h.capture.action);Frame original;
  for(int phase=0;phase<3;++phase) {
    material->diffuseColor.setValue(phase==1?.25f:.5f,.25f,.125f);
    directional->intensity=phase==1?.25f:.5f;
    group->isActive=TRUE;actual.clear();Frame active;
    if(!readPath(h,path,active)){ok=false;break;}
    ok &= require(actual.selectedCount==1&&actual.hiddenCount==0&&actual.directionalCount==1&&actual.pointCount==1,"capture preserves native path callback counts without hidden traversal");
    const auto &plan=h.capture.action.getPimpl()->lastValidPlan;
    CoinRenderShadowPlan shadows;std::string diagnostic;
    ok &= require(coin_render_plan_shadows(plan,shadows,diagnostic)&&shadows.passes.empty(),"active nonproducer lights allocate no phantom map: "+diagnostic);
    unsigned counted=0;
    for(const auto&draw:plan.draws) {
      ok &= require(draw.sourceNodeId==face->getNodeId(),"only the selected shape reaches the frame");
      const auto&state=plan.renderStates[draw.renderStateSlot];const auto&lighting=plan.lightingStates[state.lightingSlot];
      ok &= require(lighting.lights.size()==2&&lighting.lights[0].type==CoinRenderLightType::DIRECTIONAL&&lighting.lights[1].type==CoinRenderLightType::POINT,
        "both nonproducer lights remain associated with the selected draw");++counted;
    }
    ok &= require(counted==1,"one authored selected draw");
    group->isActive=FALSE;Frame ordinary;
    if(!readPath(cpu,path,ordinary)){ok=false;break;}
    // The ordinary CPU image is an independent composition control. On GPU,
    // compare the same selected path with the active group's nonproducer state.
    if(gpu) {
      int maxRgb=0;float maxDepth=0;
      for(size_t pixel=0;pixel<ordinary.depth.size();++pixel) {
        for(int c=0;c<4;++c)maxRgb=std::max(maxRgb,std::abs(int(active.rgba[4*pixel+c])-int(ordinary.rgba[4*pixel+c])));
        maxDepth=std::max(maxDepth,std::abs(active.depth[pixel]-ordinary.depth[pixel]));
      }
      ok &= require(maxRgb<=3&&maxDepth<=1e-5f,"active nonproducer selected path equals ordinary composition");
    }
    const size_t center=4*(48*96+48);
    // All four corners have distance sqrt(18) to the point light and
    // normal +Z, so ordinary vertex lighting interpolates this constant.
    const double diffuse=(phase==1?.25:.5)+.25*4./std::sqrt(18.);
    const double materialChannels[]={phase==1?.25:.5,.25,.125};
    for(int c=0;c<3;++c) {
      const int expected=int(std::lround(255*materialChannels[c]*diffuse));
      ok &= require(std::abs(int(ordinary.rgba[center+c])-expected)<=1,
        "authored nonproducer illumination phase="+std::to_string(phase)+" channel="+std::to_string(c)+" actual="+std::to_string(ordinary.rgba[center+c])+" expected="+std::to_string(expected));
    }
    ok &= require(ordinary.rgba[center+3]==255,"nonproducer composition preserves opaque alpha");
    if(phase==0)original=ordinary;else if(phase==2)ok &= require(same(original,ordinary),"inherited material/light A/B/A restores exact ordinary image");
  }
  path->unref();root->unref();return ok;
}
struct SpotWitness {
  SoSeparator *root=new SoSeparator,*map=new SoSeparator;
  SoShadowGroup *group=new SoShadowGroup;
  SoShadowSpotLight *light=new SoShadowSpotLight;
  SoOrthographicCamera *camera=new SoOrthographicCamera;
  SoTranslation *anchor=new SoTranslation,*caster=new SoTranslation;
  SoAlphaTest *alpha=new SoAlphaTest;
  explicit SpotWitness(float x) {
    root->ref();camera->position.setValue(0,0,6);camera->height=6;camera->nearDistance=.1f;camera->farDistance=10;root->addChild(camera);
    auto *env=new SoEnvironment;env->ambientIntensity=0;env->attenuation.setValue(.0625f,.125f,1);root->addChild(env);
    group->quality=1;group->precision=.125f;root->addChild(group);
    light->location.setValue(0,0,2);light->direction.setValue(0,0,-1);light->cutOffAngle=std::acos(.8f);light->dropOffRate=1.f/64.f;light->intensity=.75f;light->nearDistance=.25f;light->farDistance=4;light->shadowMapScene=map;group->addChild(light);
    auto *style=new SoShadowStyle;style->style=SoShadowStyle::SHADOWED;group->addChild(style);
    anchor->translation.setValue(x,0,0);group->addChild(anchor);
    auto *image=new SoImage;image->horAlignment=SoImage::CENTER;image->vertAlignment=SoImage::HALF;std::vector<unsigned char> pixels(13*13*3,255);image->image.setValue(SbVec2s(13,13),3,pixels.data());group->addChild(image);
    auto *casts=new SoShadowStyle;casts->style=SoShadowStyle::CASTS_SHADOW;map->addChild(casts);map->addChild(alpha);map->addChild(caster);
    auto *coords=new SoCoordinate3;const SbVec3f quad[]={SbVec3f(-.3f,-.3f,0),SbVec3f(.3f,-.3f,0),SbVec3f(.3f,.3f,0),SbVec3f(-.3f,.3f,0)};coords->point.setValues(0,4,quad);map->addChild(coords);auto *face=new SoFaceSet;face->numVertices=4;map->addChild(face);
    caster->translation.setValue(x*.5f,0,1);
  }
  ~SpotWitness(){root->unref();}
};
bool spotNumerical(bool canonical) {
  bool ok=true;
  for(float x:{0.f,.75f,1.5f,1.75f}) {
    SpotWitness scene(x);Harness h(true,canonical);Frame original;
    for(int phase=0;phase<3;++phase) {
      const float cameraX=phase==1?.125f:0.f;
      scene.camera->position.setValue(cameraX,0,6);
      scene.light->dropOffRate=phase==1?1.f/32.f:1.f/64.f;
      scene.light->intensity=phase==1?.5f:.75f;
      // Authored normal is +Z, light axis -Z and anchor (x,0,0). The
      // 3-4-5 boundary at x=1.5 has distance 2.5 and cosine exactly .8f.
      const double distance=std::sqrt(double(x)*x+4.);
      const float cosine=float(2./distance),threshold=std::cos(scene.light->cutOffAngle.getValue());
      const bool inside=cosine>=threshold;
      if(x==1.5f)ok &= require(cosine==threshold,"authored float cone boundary is exact");
      const double attenuation=1./(1.+.125*distance+.0625*distance*distance);
      const double expected=inside?(phase==1?.5:.75)*attenuation*(2./distance)*std::pow(2./distance,phase==1?4.:2.):0.;
      const int byte=int(std::lround(255*expected));
      scene.alpha->function=SoAlphaTest::NEVER;Frame lit;
      if(!h.render(scene.root,lit))return false;
      const int cx=int(std::floor(48+16*(x-cameraX)));
      for(int dy:{-4,0,4})for(int dx:{-4,0,4}) {
        const size_t at=4*size_t((95-(48+dy))*96+cx+dx);
        for(int c=0;c<3;++c)ok &= require(std::abs(int(lit.rgba[at+c])-byte)<=3,"numeric spot attenuation/exponent/domain at constant raster anchor x="+std::to_string(x)+" expected="+std::to_string(byte)+" actual="+std::to_string(lit.rgba[at+c]));
        ok &= require(lit.rgba[at+3]==255&&std::abs(lit.depth[at/4]-float(5.9/9.9))<=1e-5f,"spot raster preserves authored alpha and depth");
      }
      scene.alpha->function=SoAlphaTest::ALWAYS;Frame shadowed;if(!h.render(scene.root,shadowed))return false;
      // Interior rays cross the authored opaque patch halfway to the light.
      // Boundary/exterior anchors retain their mathematically determined cone
      // domain; the boundary is also the map's half-open right edge.
      const size_t center=4*size_t((95-48)*96+cx);
      if(x<1.5f)ok &= require(int(lit.rgba[center])-int(shadowed.rgba[center])>20,"interior spot has distinguishable authored shadow contribution");
      if(!inside)ok &= require(same(lit,shadowed),"outside cone remains dark with or without a caster");
      for(int dy:{-4,0,4})for(int dx:{-4,0,4}) {
        const size_t at=4*size_t((95-(48+dy))*96+cx+dx);
        for(int c=0;c<3;++c)ok &= require(std::abs(int(shadowed.rgba[at+c])-int(shadowed.rgba[center+c]))<=1,"shadow lookup and radial contribution remain constant across one raster node");
      }
      if(phase==0)original=lit;else if(phase==2)ok &= require(same(original,lit),"camera/exponent/intensity A/B/A restores numeric spot image");
    }
  }
  return ok;
}
