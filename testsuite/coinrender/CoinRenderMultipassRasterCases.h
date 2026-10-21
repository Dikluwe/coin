#ifndef COIN_RENDER_MULTIPASS_RASTER_CASES_H
#define COIN_RENDER_MULTIPASS_RASTER_CASES_H
struct MultipassRasterScene {
  SoSeparator *root=new SoSeparator;SoImage *image=new SoImage;
  SoMarkerSet *marker=new SoMarkerSet;SoMaterial *markerMaterial=new SoMaterial;
  int markerId=0;unsigned captures=0;bool changed=false;
  static bool bit(int x,int y){return x>=1&&x<=7&&y>=1&&y<=7&&!(x==2&&y==4);}
  static void visit(void *data,SoAction *action){if(action->isOfType(SoCallbackAction::getClassTypeId()))++static_cast<MultipassRasterScene*>(data)->captures;}
  std::array<uint8_t,4> color(int x,int y)const {
    if(x<0||x>=8||y<0||y>=8)return {{0,0,0,0}};
    const bool hole=x>=3&&x<=4&&y>=3&&y<=4;
    const uint8_t r=uint8_t(48+24*x),g=uint8_t(32+24*y),b=uint8_t(32+8*x+16*y);
    return {{uint8_t(changed?255-r:r),uint8_t(changed?255-g:g),b,uint8_t(hole?0:255)}};
  }
  MultipassRasterScene() {
    root->ref();auto *camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,3);camera->height=2;camera->nearDistance=1;camera->farDistance=5;camera->viewportMapping=SoCamera::LEAVE_ALONE;root->addChild(camera);
    auto *callback=new SoCallback;callback->setCallback(visit,this);root->addChild(callback);
    auto *model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;root->addChild(model);
    auto *depth=new SoDepthBuffer;depth->test=TRUE;depth->write=TRUE;depth->function=SoDepthBuffer::LEQUAL;root->addChild(depth);
    auto *picture=new SoSeparator;auto *position=new SoTranslation;
    position->translation.setValue(float(16.83/32-1),float(16.83/32-1),0);picture->addChild(position);
    image->horAlignment=SoImage::LEFT;image->vertAlignment=SoImage::BOTTOM;picture->addChild(image);root->addChild(picture);
    // A custom, bottom-up MSB-first9x9 bitmap makes holes and asymmetric
    // occupied pixels authorial inputs, not a copy of the renderer's bitmap.
    markerId=SoMarkerSet::getNumDefinedMarkers();std::array<uint8_t,18> bits{};
    for(int y=0;y<9;++y)for(int x=0;x<9;++x)if(bit(x,y))bits[size_t(y*2+x/8)]|=uint8_t(0x80u>>(x%8));
    SoMarkerSet::addMarker(markerId,SbVec2s(9,9),bits.data(),FALSE,FALSE);
    auto *marks=new SoSeparator;marks->addChild(markerMaterial);
    auto *coordinates=new SoCoordinate3;coordinates->point.setValue(float(44.83/32-1),float(32.83/32-1),0);marks->addChild(coordinates);
    marker->numPoints=1;marker->markerIndex=markerId;marks->addChild(marker);root->addChild(marks);
    paint(false);
  }
  void paint(bool value){changed=value;std::vector<uint8_t>bytes;for(int y=0;y<8;++y)for(int x=0;x<8;++x){const auto c=color(x,y);bytes.insert(bytes.end(),c.begin(),c.end());}image->image.setValue(SbVec2s(8,8),4,bytes.data());markerMaterial->diffuseColor.setValue(changed?.25f:.75f,changed?.75f:.25f,.5f);markerMaterial->transparency=0;}
  ~MultipassRasterScene(){root->unref();SoMarkerSet::removeMarker(markerId);}
};
bool rasterImage(const Image &image,const MultipassRasterScene &scene,int n,const char *label) {
  if(!check(image.rgba.size()==side*side*4&&image.depth.size()==side*side,std::string(label)+" complete raster RGBA/depth"))return false;
  unsigned bad=0,depthBad=0,picture=0,holes=0,markers=0,partial=0,reported=0;
  for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
    double expected[3]={0,0,0};bool lastDepth=false;unsigned coverage=0;
    const int bottomY=side-1-y;
    const bool inPicture=x>=16&&x<24&&bottomY>=16&&bottomY<24;
    const auto pixel=scene.color(x-16,bottomY-16);
    for(const auto &j:samples(n)) {
      // Image.getNilpoint uses the unjittered ViewVolume, then truncates16.83
      // to16. Marker uses the jittered projection BEFORE bitmap snap. Its
      //9x9 origin is(center-4)+j-.5, so40.33+jX and28.33+jY.
      const int originX=int(std::floor(40.33+j[0])),originY=int(std::floor(28.33+j[1]));
      const bool mark=MultipassRasterScene::bit(x-originX,bottomY-originY);
      const double markerColor[]={scene.changed?.25:.75,scene.changed?.75:.25,.5};
      for(unsigned c=0;c<3;++c)expected[c]+=(inPicture?pixel[c]:mark?255*markerColor[c]:0)/n;
      lastDepth=inPicture||mark;coverage+=mark;
    }
    const size_t p=size_t(y*side+x);
    const unsigned previousBad=bad,previousDepthBad=depthBad;
    for(unsigned c=0;c<3;++c)bad+=std::abs(double(image.rgba[p*4+c])-expected[c])>3;
    // NONE disables blending: native DrawPixels writes all four Image
    // channels, including authored RGB at alpha zero. Bitmap masks discard.
    bad+=image.rgba[p*4+3]!=(inPicture?pixel[3]:255);
    depthBad+=!std::isfinite(image.depth[p])||std::abs(image.depth[p]-(lastDepth?.5f:1.f))>1e-5;
    if((bad!=previousBad||depthBad!=previousDepthBad)&&reported++<8)
      std::cerr<<"multipass_raster_pixel label="<<label<<" N="<<n<<" x="<<x<<" y="<<y<<" bottom_y="<<bottomY<<" picture="<<inPicture<<" source_rgba="<<unsigned(pixel[0])<<','<<unsigned(pixel[1])<<','<<unsigned(pixel[2])<<','<<unsigned(pixel[3])<<" marker_coverage="<<coverage<<'/'<<n<<" actual_rgba="<<unsigned(image.rgba[p*4])<<','<<unsigned(image.rgba[p*4+1])<<','<<unsigned(image.rgba[p*4+2])<<','<<unsigned(image.rgba[p*4+3])<<" expected_rgba="<<expected[0]<<','<<expected[1]<<','<<expected[2]<<",255 depth="<<image.depth[p]<<" expected_depth="<<(lastDepth?.5f:1.f)<<'\n';
    picture+=inPicture&&pixel[3];holes+=inPicture&&!pixel[3];markers+=coverage>0;partial+=coverage>0&&coverage<unsigned(n);
  }
  std::cout<<"multipass_raster "<<label<<" N="<<n<<" image="<<picture<<" image_holes="<<holes<<" marker="<<markers<<" partial="<<partial<<" bad="<<bad<<" depth_bad="<<depthBad<<'\n';
  return check(!bad&&!depthBad&&picture==60&&holes==4&&markers>40&&(n==1||partial>8),std::string(label)+" Image fixed native anchor, jitter-before-snap Marker mask, linear mean and LAST depth");
}
bool rasterPath(bool gpu,const CoinRenderOptions &options,int requested) {
  MultipassRasterScene scene;
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));Audit *audit=prepare(target.get(),gpu);if(!audit)return false;
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::NONE);action.setBackgroundColor(SbColor4f(0,0,0,1));
  auto draw=[&](int n,const char *label){action.setNumPasses(n);const auto serial=target->getLastSubmissionSerial(),source=scene.root->getNodeId();const unsigned callbacks=scene.captures;action.apply(scene.root);const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial,std::string(label)+" current raster publication: "+action.getLastError().getString());check(callbacks+1==scene.captures&&source==scene.root->getNodeId(),"raster multipass captures once without mutating image/marker source");if(success){rasterImage(read(target.get()),scene,n,label);unsigned pictures=0,marks=0;for(const auto &d:audit->last.draws){if(d.sourceNodeId==scene.image->getNodeId())pictures+=d.geometry.indexCount;if(d.sourceNodeId==scene.marker->getNodeId())marks+=d.geometry.indexCount;}check(pictures==6&&marks>0,"actual image quad and authored marker run geometry consumed, no shadow-only inventory assumption");CoinWgpuFfiFrame ffi;std::string error;check(ffi.prepare(audit->last,side,side,error,false),"real raster FFI: "+error);}};
  const int n=requested?requested:3;
  draw(1,"native raster predecessor");draw(n,"raster multipass");scene.paint(true);draw(n,"raster source mutation");scene.paint(false);draw(n,"raster authorial restoration");
  action.setRenderTarget(nullptr);return good;
}
#endif
