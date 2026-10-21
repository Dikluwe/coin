#ifndef COIN_RENDER_MULTIPASS_TEXT2_CASES_H
#define COIN_RENDER_MULTIPASS_TEXT2_CASES_H
// The built-in defaultFont at size13 has a7-pixel advance,14 bitmap rows,
// bearing12, a32-bit bitmap row, and these F/G rows (MSB first, bottom-up). These are glyph inputs
// from the native Coin font, never pixels sampled from a captured image.
struct MultipassTextScene {
  SoSeparator *root=new SoSeparator;SoText2 *text=new SoText2;
  SoMaterial *material=new SoMaterial;unsigned captures=0;bool changed=false;
  static void visit(void *data,SoAction *a){if(a->isOfType(SoCallbackAction::getClassTypeId()))++static_cast<MultipassTextScene*>(data)->captures;}
  static bool bit(bool changed,int x,int y) {
    static const unsigned rows[2][14]={{0,0,0,0x38,0x10,0x14,0x1c,0x14,0x12,0x3e,0,0,0,0},{0,0,0,0x1c,0x22,0x27,0x20,0x20,0x22,0x1e,0,0,0,0}};
    return x>=0&&x<8&&y>=0&&y<14&&(rows[changed?1:0][y]&(0x80u>>x));
  }
  MultipassTextScene(){
    root->ref();auto *camera=new SoOrthographicCamera;camera->position.setValue(0,0,3);camera->height=2;camera->nearDistance=1;camera->farDistance=5;camera->viewportMapping=SoCamera::LEAVE_ALONE;root->addChild(camera);
    auto *callback=new SoCallback;callback->setCallback(visit,this);root->addChild(callback);
    auto *font=new SoFont;font->name="defaultFont";font->size=13;root->addChild(font);
    material->diffuseColor.setValue(.25f,.5f,.75f);material->transparency=0;root->addChild(material);
    auto *position=new SoTranslation;position->translation.setValue(float(24.83/32-1),float(32.83/32-1),0);root->addChild(position);
    text->string.setValue("F");text->justification=SoText2::LEFT;root->addChild(text);
  }
  void paint(bool value){changed=value;text->string.setValue(value?"G":"F");material->diffuseColor.setValue(value?.75f:.25f,value?.25f:.5f,value?.5f:.75f);}
  ~MultipassTextScene(){root->unref();}
};
bool text2Image(const Image &image,const MultipassTextScene &scene,int n,const char *label){
  if(!check(image.rgba.size()==side*side*4&&image.depth.size()==side*side,std::string(label)+" complete Text2 color/depth"))return false;
  unsigned bad=0,depthBad=0,full=0,partial=0;
  const double align=scene.text->justification.getValue()==SoText2::CENTER?3.5:scene.text->justification.getValue()==SoText2::RIGHT?7:0;
  for(int y=0;y<side;++y)for(int x=0;x<side;++x){
    unsigned covered=0;bool last=false;
    for(const auto &j:samples(n)){
      // Native mono path: X is floor(raw justified RasterPos); Y first
      // truncates projected nilpoint, then applies bearing-height=-2.
      const int originX=int(std::floor(24.33+j[0]-align));
      const int originY=int(32.33+j[1])-2;
      last=MultipassTextScene::bit(scene.changed,x-originX,side-1-y-originY);covered+=last;
    }
    const size_t p=size_t(y*side+x);const auto color=scene.material->diffuseColor[0];
    for(unsigned c=0;c<3;++c)bad+=std::abs(double(image.rgba[4*p+c])-255*color[c]*covered/n)>3;
    bad+=image.rgba[4*p+3]!=255;
    depthBad+=!std::isfinite(image.depth[p])||std::abs(image.depth[p]-(last?.5f:1.f))>1e-5;
    full+=covered==unsigned(n);partial+=covered>0&&covered<unsigned(n);
  }
  std::cout<<"multipass_text2 "<<label<<" N="<<n<<" bad="<<bad<<" depth_bad="<<depthBad<<" full="<<full<<" partial="<<partial<<'\n';
  return check(!bad&&!depthBad&&full>0&&(n==1||partial>4),std::string(label)+" native glyph, jitter-before-snap/layout, linear RGB and LAST depth");
}
bool text2Path(bool gpu,const CoinRenderOptions &options,int requested){
  MultipassTextScene scene;std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));auto *audit=prepare(target.get(),gpu);if(!audit)return false;
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::NONE);action.setBackgroundColor(SbColor4f(0,0,0,1));
  auto draw=[&](int n,const char *label){action.setNumPasses(n);const auto source=scene.root->getNodeId(),serial=target->getLastSubmissionSerial();const unsigned captures=scene.captures;action.apply(scene.root);const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial,std::string(label)+" current Text2 publication: "+action.getLastError().getString());check(scene.captures==captures+1&&source==scene.root->getNodeId(),"Text2 one immutable capture per update");if(success){text2Image(read(target.get()),scene,n,label);unsigned indices=0;for(const auto &d:audit->last.draws)if(d.sourceNodeId==scene.text->getNodeId())indices+=d.geometry.indexCount;check(indices>0,"actual Text2 glyph coverage emitted");}};
  const int n=requested?requested:3;draw(1,"mono Text2 predecessor");draw(n,"mono Text2 multipass");
  scene.text->justification=SoText2::CENTER;draw(n,"fractional center justification before snap");
  scene.text->justification=SoText2::RIGHT;draw(n,"right justification before snap");
  scene.paint(true);draw(n,"authored G and material mutation");scene.paint(false);scene.text->justification=SoText2::LEFT;draw(n,"Text2 authorial restoration");
  action.setRenderTarget(nullptr);return good;
}
#endif
