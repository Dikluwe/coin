#ifndef COIN_RENDER_MULTIPASS_VIEWPORT_CASES_H
#define COIN_RENDER_MULTIPASS_VIEWPORT_CASES_H
// Included in the test's anonymous namespace, after its shared numeric oracle.
struct ViewportScene {
  struct Region {
    int x,y,w,h;
    unsigned visits=0;
    static void install(void *data,SoAction *action) {
      if(!action->isOfType(SoCallbackAction::getClassTypeId()))return;
      auto &self=*static_cast<Region*>(data);++self.visits;
      SbViewportRegion viewport(side,side);
      viewport.setViewportPixels(short(self.x),short(self.y),short(self.w),short(self.h));
      SoViewportRegionElement::set(action->getState(),viewport);
    }
    Region(int a,int b,int c,int d):x(a),y(b),w(c),h(d){}
  } regions[2]={{4,8,24,40},{36,16,20,32}};
  SoSeparator *root=new SoSeparator;
  SoOrthographicCamera *camera=new SoOrthographicCamera;
  SoSeparator *branches[2];
  SoFaceSet *shape=new SoFaceSet;
  SoCoordinate3 *coordinates=new SoCoordinate3;
  bool cropped=false;
  Region effective(unsigned i)const {auto v=regions[i];if(cropped){if(v.w<v.h){v.y+=(v.h-v.w)/2;v.h=v.w;}else{v.x+=(v.w-v.h)/2;v.w=v.h;}}return v;}
  explicit ViewportScene(bool crop=false):cropped(crop) {
    root->ref();
    camera->position.setValue(0,0,3);camera->height=2;camera->aspectRatio=1;
    camera->nearDistance=1;camera->farDistance=5;
    camera->viewportMapping=crop?SoCamera::CROP_VIEWPORT_NO_FRAME:SoCamera::LEAVE_ALONE;
    const float vertical=crop?.86f:.7f;
    const SbVec3f points[]={SbVec3f(-.7f,-vertical,0),SbVec3f(.029f,-vertical,0),SbVec3f(.029f,vertical,0),SbVec3f(-.7f,vertical,0)};
    coordinates->point.setValues(0,4,points);shape->numVertices=4;
    auto *mesh=new SoSeparator;mesh->addChild(coordinates);mesh->addChild(shape);
    for(unsigned i=0;i<2;++i) {
      branches[i]=new SoSeparator;
      auto *callback=new SoCallback;callback->setCallback(Region::install,&regions[i]);
      branches[i]->addChild(callback);branches[i]->addChild(camera);
      auto *model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;branches[i]->addChild(model);
      auto *material=new SoMaterial;
      material->diffuseColor.setValue(i?.75f:.25f,i?.25f:.5f,i?.5f:.75f);
      material->transparency=0;branches[i]->addChild(material);branches[i]->addChild(mesh);
      root->addChild(branches[i]);
    }
  }
  void reverse(bool reversed) {
    // The root retains the nodes until after both temporary references exist.
    branches[0]->ref();branches[1]->ref();root->removeAllChildren();
    root->addChild(branches[reversed?1:0]);root->addChild(branches[reversed?0:1]);
    branches[0]->unref();branches[1]->unref();
  }
  ~ViewportScene(){root->unref();}
};
bool viewportImage(const Image &image,const ViewportScene &scene,int n,const char *label) {
  if(!check(image.rgba.size()==side*side*4&&image.depth.size()==side*side,std::string(label)+" complete viewport color/depth"))return false;
  unsigned bad=0,depthBad=0,partial[2]={0,0},full[2]={0,0},outside=0;
  const auto offsets=samples(n);
  const double clear[]={.0625,.125,.1875,0};
  // LEAVE_ALONE retains the square authorial projection. Evaluate both
  // axes over the full framebuffer, including the asymmetric horizontal
  // witness at pixel(40,20) of the20x32 viewport. No silhouette is borrowed
  // from an earlier delivery or another renderer.
  for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
    double expected[4]={0,0,0,0};bool lastCovered=false;
    unsigned counts[2]={0,0};
    for(const auto &j:offsets) {
      int owner=-1;
      for(unsigned i=0;i<2;++i) {
        const auto v=scene.effective(i);
        const double px=x+1-j[0],py=y+j[1];
        const double top=side-v.y-v.h;
        if(px>v.x+.15*v.w&&px<v.x+.5145*v.w&&
           py>top+(scene.cropped?.07:.15)*v.h&&py<top+(scene.cropped?.93:.85)*v.h)owner=int(i);
      }
      lastCovered=owner>=0;
      const double colors[2][4]={{.25,.5,.75,1},{.75,.25,.5,1}};
      for(unsigned c=0;c<4;++c)expected[c]+=(owner<0?clear[c]:colors[owner][c])/n;
      if(owner>=0)++counts[owner];
    }
    const size_t p=size_t(y*side+x);
    for(unsigned c=0;c<4;++c)bad+=std::abs(double(image.rgba[p*4+c])-255*expected[c])>(c==3?1:3);
    const double near=scene.camera->nearDistance.getValue();
    const double depth=lastCovered?(3-near)/(5-near):1;
    depthBad+=!std::isfinite(image.depth[p])||std::abs(image.depth[p]-depth)>1e-5;
    for(unsigned i=0;i<2;++i){partial[i]+=counts[i]>0&&counts[i]<unsigned(n);full[i]+=counts[i]==unsigned(n);}
    outside+=!counts[0]&&!counts[1];
  }
  if(n==3&&!scene.cropped) {
    // At(40,20), X is interior for all three samples. Only native jY=.8317967
    // crosses20.8; the last jY=.4131554 does not. Thus alpha=1/3 and LAST
    // depth=clear. Y0, invertedY, swappedXY and target64-height jitter all
    // leave this pixel clear and fail the original RGB3/alpha/depth gates.
    const size_t witness=size_t(20*side+40);
    const double expected[]={74.375,42.5,74.375,85};
    for(unsigned c=0;c<4;++c)bad+=std::abs(double(image.rgba[witness*4+c])-expected[c])>(c==3?1:3);
    depthBad+=std::abs(image.depth[witness]-1.f)>1e-5;
    std::cout<<"multipass_viewport_Ywitness rgba="<<unsigned(image.rgba[witness*4])<<','<<unsigned(image.rgba[witness*4+1])<<','<<unsigned(image.rgba[witness*4+2])<<','<<unsigned(image.rgba[witness*4+3])<<" expected=74.375,42.5,74.375,85 depth="<<image.depth[witness]<<" expected_depth=1\n";
  }
  std::cout<<"multipass_viewport "<<label<<" n="<<n<<" bad="<<bad<<" depth_bad="<<depthBad<<" partial="<<partial[0]<<','<<partial[1]<<" full="<<full[0]<<','<<full[1]<<" outside="<<outside<<'\n';
  return check(!bad&&!depthBad&&full[0]>64&&full[1]>64&&outside>300&&(n==1||(partial[0]>=16&&partial[1]>=16)),std::string(label)+" per-occurrence jitter, independent clear average and signed-near LAST depth");
}
bool viewportDelivery(const CoinRenderFramePlan &frame,const ViewportScene &scene) {
  bool ok=true;unsigned occurrences[2]={0,0},indices=0;
  for(const auto &draw:frame.draws) {
    if(draw.sourceNodeId!=scene.shape->getNodeId()||!draw.isSceneLayer())continue;
    if(!check(draw.renderStateSlot<frame.renderStates.size(),"viewport draw has actual state")){ok=false;continue;}
    const auto &state=frame.renderStates[draw.renderStateSlot];
    if(!check(state.viewportSlot<frame.viewports.size()&&state.cameraSlot<frame.cameras.size(),"actual camera/viewport references")){ok=false;continue;}
    const auto &viewport=frame.viewports[state.viewportSlot];
    int owner=-1;
    for(unsigned i=0;i<2;++i) {
      const auto v=scene.effective(i);
      if(viewport.x==v.x&&viewport.y==v.y&&viewport.width==v.w&&viewport.height==v.h)owner=int(i);
    }
    ok&=check(owner>=0,"viewport occurrence retains exact authored pixels");
    if(owner>=0)++occurrences[owner];
    const auto &camera=frame.cameras[state.cameraSlot];
    ok&=check(!camera.isPerspective&&camera.nearDistance==scene.camera->nearDistance.getValue()&&camera.farDistance==5,"camera near/far and projection kind survive each occurrence");
    indices+=draw.geometry.indexCount;
  }
  ok&=check(occurrences[0]>0&&occurrences[1]>0&&indices==12,"both shared-shape occurrences consume exactly two triangles each");
  CoinWgpuFfiFrame ffi;std::string error;
  const bool packed=ffi.prepare(frame,side,side,error,false);
  ok&=check(packed,"real CPU FFI viewport packet: "+error);
  if(packed) {
    const auto &view=ffi.getView();unsigned ffiIndices=0,byViewport[2]={0,0};bool found[2]={false,false};
    for(uint64_t d=0;d<view.draw_count;++d) {
      const auto &draw=view.draws[d];
      // stable_node_id transports Core stableNodeId, not sourceNodeId.
      // This fixture has only the two Core-verified FaceSet occurrences;
      // correlate the packet by consumed viewport/material/ranges instead.
      if(!check(draw.render_state_slot<view.state_count,"FFI occurrence state range")){ok=false;continue;}
      const auto &state=view.states[draw.render_state_slot];
      bool matched=false;
      for(unsigned i=0;i<2;++i) {
        const auto v=scene.effective(i);
        if(state.viewport[0]==v.x&&state.viewport[1]==side-v.y-v.h&&state.viewport[2]==v.w&&state.viewport[3]==v.h){
          found[i]=true;matched=true;byViewport[i]+=draw.index_count;
          const double colors[2][3]={{.25,.5,.75},{.75,.25,.5}};
          if(!check(state.material_slot<view.material_count,"FFI occurrence material reference")){ok=false;continue;}
          const auto &material=view.materials[state.material_slot];
          for(unsigned c=0;c<3;++c)ok&=check(material.diffuse[c]==colors[i][c],"FFI viewport owns its exact authorial material");
        }
      }
      ok&=check(matched,"FFI exact top-left viewport conversion once");ffiIndices+=draw.index_count;
    }
    ok&=check(found[0]&&found[1]&&ffiIndices==12&&byViewport[0]==6&&byViewport[1]==6,"FFI preserves both shared occurrences and exact per-viewport consumption");
  }
  return ok;
}
bool viewportPath(bool gpu,const CoinRenderOptions &options,int requested,bool crop=false) {
  ViewportScene scene(crop);
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
  auto *audit=prepare(target.get(),gpu);if(!audit)return false;
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());
  action.setTransparencyType(CoinRenderAction::NONE);action.setBackgroundColor(SbColor4f(.0625f,.125f,.1875f,0));
  auto draw=[&](int n,const char *label) {
    action.setNumPasses(n);const unsigned before[]={scene.regions[0].visits,scene.regions[1].visits};
    const auto serial=target->getLastSubmissionSerial();const auto revision=scene.root->getNodeId(),cameraId=scene.camera->getNodeId();
    action.apply(scene.root);
    const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial,std::string(label)+" new current viewport publication: "+action.getLastError().getString());
    check(scene.regions[0].visits==before[0]+1&&scene.regions[1].visits==before[1]+1&&scene.root->getNodeId()==revision&&scene.camera->getNodeId()==cameraId,"shared camera and viewport callbacks captured once per occurrence without source mutation");
    if(success){transportMatrices(audit->last,n,action.getMultipassBufferBudget());viewportDelivery(audit->last,scene);viewportImage(read(target.get()),scene,n,label);}
  };
  const int n=requested?requested:3;
  draw(1,"viewport one-pass baseline");draw(n,"two viewport occurrences");
  scene.reverse(true);draw(n,"reversed viewport/camera order");
  scene.camera->nearDistance=-1;draw(n,"shared signed orthographic near");
  scene.camera->nearDistance=1;scene.reverse(false);draw(n,"viewport authorial restoration");
  action.setRenderTarget(nullptr);return good;
}
#endif
