#ifndef COIN_RENDER_MULTIPASS_LAYER_CASES_H
#define COIN_RENDER_MULTIPASS_LAYER_CASES_H
bool layerImage(const Image &image,const Scene &base,const Scene &overlay,int n,bool clearDepth,bool reversed,bool annotation,bool boundaryBase,const char *label){
  if(!check(image.rgba.size()==side*side*4&&image.depth.size()==side*side,std::string(label)+" full layer result"))return false;
  unsigned bad=0,depthBad=0,partial=0,overlap=0,onlyOverlay=0;
  for(int y=16;y<48;++y)for(int x=0;x<side;++x){double expected[4]={0,0,0,0};double lastDepth=1;unsigned counts[2]={0,0};
    for(const auto &j:samples(n)){const double px=x+1-j[0];const bool b=px>8&&px<base.edge,o=px>8&&px<overlay.edge;int owner=-1;
      if(annotation){owner=o?1:b?0:-1;lastDepth=b?.5:1;}
      else{
        // Boundary metadata gives its registered node logical layer1; all
        // unmarked geometry remains layer0. Composition sorts these roles
        // before traversal order, even if layer1 was captured first.
        owner=boundaryBase?(o?1:-1):(b?0:-1);
        lastDepth=owner==0?.5:owner==1?.75:1;
        if(clearDepth)lastDepth=1; // clears even where layer1 has no fragment
        const bool foreground=boundaryBase?b:o;
        const double depth=boundaryBase?.5:.75;
        if(foreground&&depth<=lastDepth){owner=boundaryBase?0:1;lastDepth=depth;}
      }
      const double background[]={.0625,.125,.1875,0};
      for(unsigned c=0;c<4;++c){double color=background[c];if(owner>=0)color=c==3?1:(owner?overlay:base).material->diffuseColor[0][c];expected[c]+=color/n;}
      if(owner>=0)++counts[owner];overlap+=b&&o;onlyOverlay+=o&&!b;
    }
    const size_t p=size_t(y*side+x);for(unsigned c=0;c<4;++c)bad+=std::abs(double(image.rgba[4*p+c])-255*expected[c])>(c==3?1:3);
    depthBad+=!std::isfinite(image.depth[p])||std::abs(image.depth[p]-lastDepth)>1e-5;partial+=(counts[0]>0&&counts[0]<unsigned(n))||(counts[1]>0&&counts[1]<unsigned(n));
  }
  std::cout<<"multipass_layers "<<label<<" N="<<n<<" clear_depth="<<clearDepth<<" reverse="<<reversed<<" annotation="<<annotation<<" base_is_foreground="<<boundaryBase<<" bad="<<bad<<" depth_bad="<<depthBad<<" partial="<<partial<<" overlap="<<overlap<<" overlay_only="<<onlyOverlay<<'\n';
  return check(!bad&&!depthBad&&overlap>300&&onlyOverlay>128&&(n==1||partial>=32),std::string(label)+" sample-local layer clear/load/depth and background mean, LAST sample depth");
}
bool layerPath(bool gpu,const CoinRenderOptions &options,int requested,bool annotation){
  Scene base,overlay;overlay.move(48.33);overlay.material->diffuseColor.setValue(.75f,.25f,.5f);
  for(int i=0;i<overlay.coordinates->point.getNum();++i){auto p=overlay.coordinates->point[i];p[2]=-1;overlay.coordinates->point.set1Value(i,p);}
  auto *root=new SoSeparator;root->ref();root->addChild(base.root);
  SoSeparator *layer=overlay.root;if(annotation){auto *a=new SoAnnotation;a->addChild(overlay.root);layer=a;}
  root->addChild(layer);
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));auto *audit=prepare(target.get(),gpu);if(!audit){root->unref();return false;}
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::NONE);action.setBackgroundColor(SbColor4f(.0625f,.125f,.1875f,0));bool reversed=false,captured=false,capturedClear=false,boundaryBase=false,capturedBoundaryBase=false;uint64_t capturedSource=0;int capturedPasses=0;
  auto draw=[&](int n,bool clearDepth,const char *label){action.setNumPasses(n);action.clearManagerLayerBoundaries();if(!annotation)action.addManagerLayerBoundary(boundaryBase?base.root:layer,clearDepth,CoinRenderAction::NONE);const auto serial=target->getLastSubmissionSerial(),source=root->getNodeId();const unsigned b=base.captures,o=overlay.captures;action.apply(root);const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial,std::string(label)+" current layer publication: "+action.getLastError().getString());const bool sameSource=captured&&source==capturedSource&&n==capturedPasses;
    const bool samePolicy=annotation||(clearDepth==capturedClear&&boundaryBase==capturedBoundaryBase);
    const unsigned bc=base.captures-b,oc=overlay.captures-o;
    // Native capture caching reuses an unchanged root/configuration. A
    // manager policy edit can either rebuild once or patch retained layer
    // metadata; both keep immutable geometry and must produce the NEW image.
    const bool count=sameSource?(samePolicy?bc==0:bc<=1):bc==1;
    check(count&&bc==oc&&root->getNodeId()==source,"one capture per authorial update, zero exact reuse, at most one policy refresh, never N traversals");
    std::cout<<"multipass_layer_capture same_source="<<sameSource<<" same_policy="<<samePolicy<<" base="<<bc<<" overlay="<<oc<<'\n';
    if(success){
      unsigned indices[2]={0,0},clears=0;bool roles=true;
      const uint64_t shapes[]={base.root->getChild(base.root->getNumChildren()-1)->getNodeId(),overlay.root->getChild(overlay.root->getNumChildren()-1)->getNodeId()};
      for(const auto &d:audit->last.draws)for(unsigned i=0;i<2;++i)if(d.sourceNodeId==shapes[i]){
        indices[i]+=d.geometry.indexCount;const unsigned role=boundaryBase?(i==0?1:0):i;
        roles&=d.renderLayer==role;clears+=d.clearDepthBefore;
      }
      check(roles&&indices[0]==6&&indices[1]==6&&clears==unsigned(!annotation&&clearDepth),"actual layer roles, complete geometry and one authored clear barrier independent of capture order");
      layerImage(read(target.get()),base,overlay,n,clearDepth,reversed,annotation,boundaryBase,label);captured=true;capturedSource=source;capturedPasses=n;capturedClear=clearDepth;capturedBoundaryBase=boundaryBase;}};
  const int n=requested?requested:3;draw(1,false,"layer predecessor");draw(n,false,"layer depth load");draw(n,true,"layer independent depth clear");
  layer->ref();root->removeChild(layer);root->insertChild(layer,0);layer->unref();reversed=true;draw(n,true,"reversed capture/camera order preserves registered layer priority");
  overlay.material->diffuseColor.setValue(.125f,.75f,.375f);draw(n,false,"layer live material mutation");overlay.material->diffuseColor.setValue(.75f,.25f,.5f);draw(n,false,"layer authorial restoration");
  if(!annotation){
    boundaryBase=true;draw(n,false,"explicit inverted logical priority with depth load");draw(n,true,"explicit inverted priority clears depth before base foreground");
    layer->ref();root->removeChild(layer);root->addChild(layer);layer->unref();reversed=false;
    draw(n,true,"inverted logical priority survives reversed capture/camera order");
    draw(n,false,"inverted logical priority restores depth load");
  }
  action.setRenderTarget(nullptr);root->unref();return good;
}
#endif
