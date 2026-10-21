#ifndef COIN_RENDER_MULTIPASS_PRECISION_CASES_H
#define COIN_RENDER_MULTIPASS_PRECISION_CASES_H
bool precisionPath(bool gpu,const CoinRenderOptions &options){
  Scene scene;scene.move(24);scene.material->diffuseColor.setValue(.2f,.4f,.6f);
  // Four quarter-pixel bands. Native j16 visits their colors in this order:
  // A B C D A C B A B D D A C C D B. Each is visited four times. The red
  // palette211/47/117/12 has true mean96.75; progressive8bit mean gives100,
  // outside RGB3. The flat .4/.6 channels independently reject quantizing
  // each weighted contribution before adding into an8bit sum (96vs102).
  const unsigned red[]={117,47,211,12},blue[]={33,213,25,82};
  for(unsigned band=0;band<4;++band){auto *part=new SoSeparator;auto *material=new SoMaterial;material->diffuseColor.setValue(red[band]/255.f,(255-red[band])/255.f,blue[band]/255.f);material->transparency=0;part->addChild(material);
    auto *coordinates=new SoCoordinate3;const float left=float((32+.25*band)/32-1),right=float((32+.25*(band+1))/32-1);const SbVec3f points[]={SbVec3f(left,-.75f,0),SbVec3f(right,-.75f,0),SbVec3f(right,.75f,0),SbVec3f(left,.75f,0)};coordinates->point.setValues(0,4,points);part->addChild(coordinates);auto *face=new SoFaceSet;face->numVertices=4;part->addChild(face);scene.root->addChild(part);}
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));auto *audit=prepare(target.get(),gpu);if(!audit)return false;
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::NONE);action.setBackgroundColor(SbColor4f(0,0,0,0));action.setNumPasses(16);
  const auto source=scene.root->getNodeId();action.apply(scene.root);const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>0,"precision N16 current complete publication: "+std::string(action.getLastError().getString()));
  check(scene.captures==1&&scene.root->getNodeId()==source,"precision palette captured exactly once without source edits");
  if(success){const auto image=read(target.get());if(check(image.rgba.size()==side*side*4&&image.depth.size()==side*side,"precision complete current RGBA/depth")){unsigned bad=0,depthBad=0,palette=0,flat=0,clear=0;
    for(int y=16;y<48;++y)for(int x=0;x<side;++x){double expected[4]={0,0,0,0};bool examine=false,covered=false;
      if(x>=12&&x<20){const double c[]={51,102,153,255};std::copy(c,c+4,expected);examine=covered=true;++flat;}
      else if(x==32){const double c[]={96.75,158.25,88.25,255};std::copy(c,c+4,expected);examine=covered=true;++palette;}
      else if(x<6||x>35){examine=true;++clear;}
      if(!examine)continue;const size_t p=size_t(y*side+x);for(unsigned c=0;c<4;++c)bad+=std::abs(double(image.rgba[4*p+c])-expected[c])>(c==3?1:3);depthBad+=!std::isfinite(image.depth[p])||std::abs(image.depth[p]-(covered?.5f:1.f))>1e-5;
    }
    std::cout<<"multipass_precision N=16 bad="<<bad<<" depth_bad="<<depthBad<<" palette="<<palette<<" flat="<<flat<<" clear="<<clear<<'\n';check(!bad&&!depthBad&&palette==32&&flat>=256&&clear>512,"float accumulation rejects progressive8bit averaging and per-contribution8bit quantization with original RGB3 gate");
  }}
  action.setRenderTarget(nullptr);return good;
}
#endif
