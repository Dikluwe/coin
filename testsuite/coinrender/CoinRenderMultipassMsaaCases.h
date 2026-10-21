#ifndef COIN_RENDER_MULTIPASS_MSAA_CASES_H
#define COIN_RENDER_MULTIPASS_MSAA_CASES_H
void msaaGeometry(Scene &scene) {
  // Each one-pixel-high strip is a filled quad, with independently authored
  // cut phase and slope. This separates samples having either different X or
  // different Y coordinates; a half-pixel vertical edge alone cannot do so.
  std::vector<SbVec3f> points;std::vector<int32_t> counts;
  for(int row=16;row<48;++row) {
    const double phase=.1+.2*((row-16)%4);
    const float bottom=float(1-2.*(row+1)/side),top=float(1-2.*row/side);
    const float cutTop=float(2*(32+phase)/side-1);
    const float cutBottom=float(2*(32+phase+.5)/side-1);
    points.push_back(SbVec3f(-.75f,bottom,0));points.push_back(SbVec3f(cutBottom,bottom,0));
    points.push_back(SbVec3f(cutTop,top,0));points.push_back(SbVec3f(-.75f,top,0));counts.push_back(4);
  }
  // Fixed phase sweep, independent of any observed GPU output or queried
  // sample positions. On a1/16-pixel grid the irrational slope separates all
  // distinct locations: the minimum projected separation is at least
  // abs(7-5*sqrt(2))/16>.00444. Each odd-coverage interval therefore contains
  // at least four of these1/1024-spaced cuts. This is a conditional geometric
  // proof for that grid, not a claim about every possible device pattern.
  struct Point {double x,y;Point(double a,double b):x(a),y(b){}};
  unsigned phase=0;
  for(int row=0;row<side&&phase<2560;++row)for(int column=4;column<side&&phase<2560;++column) {
    // Preserve the original strip, brightness and N7 witnesses with a full
    // pixel guard band; reserve columns0/1 as an analytically clear control.
    if(column>=6&&column<36&&row>=14&&row<50)continue;
    const double cut=-1.5+double(phase++)/1024;
    const double slope=std::sqrt(2.);
    std::vector<Point> polygon={Point(0,1),Point(1,1),Point(1,0),Point(0,0)},clipped;
    Point previous=polygon.back();double previousDistance=previous.x-slope*previous.y-cut;
    for(const auto &current:polygon) {
      const double distance=current.x-slope*current.y-cut;
      const bool wasInside=previousDistance<=0,isInside=distance<=0;
      if(wasInside!=isInside) {
        const double t=previousDistance/(previousDistance-distance);
        clipped.push_back(Point(previous.x+t*(current.x-previous.x),previous.y+t*(current.y-previous.y)));
      }
      if(isInside)clipped.push_back(current);
      previous=current;previousDistance=distance;
    }
    if(clipped.size()<3)continue;
    for(const auto &v:clipped)points.push_back(SbVec3f(float(2*(column+v.x)/side-1),float(1-2*(row+v.y)/side),0));
    counts.push_back(int32_t(clipped.size()));
  }
  check(phase==2560,"complete authorial MSAA phase sweep fits the fixed target");
  std::cout<<"multipass_msaa_geometry phases="<<phase<<" points="<<points.size()<<" faces="<<counts.size()<<'\n';
  scene.coordinates->point.setValues(0,int(points.size()),points.data());
  auto *shape=static_cast<SoFaceSet*>(scene.root->getChild(scene.root->getNumChildren()-1));
  shape->numVertices.setValues(0,int(counts.size()),counts.data());
}
bool msaaImage(const Image &image,int n,unsigned m,const SbColor &material,const char *label) {
  if(!check(image.rgba.size()==side*side*4&&image.depth.size()==side*side,std::string(label)+" full resolved RGBA/depth"))return false;
  const unsigned levels=unsigned(n)*m;
  unsigned bad=0,depthBad=0,partial=0,odd=0,notSingle=0,bright=0;
  for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
    const size_t p=size_t(y*side+x);
    const double alpha=image.rgba[p*4+3]/255.;
    const unsigned k=unsigned(std::lround(alpha*levels));
    bad+=k>levels||std::abs(double(image.rgba[p*4+3])-255.*k/levels)>1;
    // Opaque flat color over transparent black: resolving M samples then
    // averaging N passes must preserve RGB=material*coverage in linear space.
    for(unsigned c=0;c<3;++c)bad+=std::abs(double(image.rgba[p*4+c])-255*material[c]*alpha)>3;
    partial+=k>0&&k<levels;
    if(k>0&&k<levels) {
      odd+=(k&1)!=0;
      double nearest=1;
      for(int j=0;j<=n;++j)nearest=std::min(nearest,std::abs(alpha-double(j)/n));
      notSingle+=nearest>1.5/255.;
    }
    if(x>=12&&x<24&&y>=20&&y<44){bright+=k==levels;depthBad+=std::abs(image.depth[p]-.5f)>1e-5;}
    else if(x<2){bad+=image.rgba[p*4+3]!=0;depthBad+=std::abs(image.depth[p]-1.f)>1e-5;}
    else depthBad+=!std::isfinite(image.depth[p])||(std::abs(image.depth[p]-.5f)>1e-5&&std::abs(image.depth[p]-1.f)>1e-5);
  }
  std::cout<<"multipass_msaa "<<label<<" N="<<n<<" samples="<<m<<" bad="<<bad<<" depth_bad="<<depthBad<<" partial="<<partial<<" odd="<<odd<<" beyond_1x="<<notSingle<<" bright="<<bright<<'\n';
  return check(!bad&&!depthBad&&bright>=192&&partial>4&&notSingle>4&&(m!=4||odd>4),std::string(label)+" real independent MSAA coverage, linear color/alpha mean and geometric depth");
}
bool msaaPath(const CoinRenderOptions &options,int requested) {
  Scene scene;msaaGeometry(scene);
  std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
  Audit *audit=prepare(target.get(),true);if(!audit)return false;
  check(target->getOptions().sampleCount==options.sampleCount,"immutable real requested MSAA count");
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());
  action.setTransparencyType(CoinRenderAction::NONE);action.setBackgroundColor(SbColor4f(0,0,0,0));
  auto draw=[&](int n,const char *label) {
    action.setNumPasses(n);const unsigned callbacks=scene.captures;const auto source=scene.root->getNodeId();const auto serial=target->getLastSubmissionSerial();
    action.apply(scene.root);
    const bool success=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial,std::string(label)+" current MSAA publication: "+action.getLastError().getString());
    check(scene.captures==callbacks+1&&source==scene.root->getNodeId(),"MSAA multipass captures authorial geometry exactly once");
    if(success)msaaImage(read(target.get()),n,options.sampleCount,scene.material->diffuseColor[0],label);
  };
  const int n=requested?requested:3;
  draw(1,"real M-sample predecessor");draw(n,"MSAA resolve before multipass accumulation");
  scene.material->diffuseColor.setValue(.75f,.25f,.5f);draw(n,"MSAA material mutation");
  scene.material->diffuseColor.setValue(.25f,.5f,.75f);draw(n,"MSAA authorial restoration");
  const Image stable=read(target.get());const auto serial=target->getLastSubmissionSerial();const auto commands=audit->commands;
  action.setMultipassBufferBudget(0);action.apply(scene.root);
  check(action.getLastStatus()==CoinRenderAction::OUT_OF_MEMORY&&audit->commands==commands&&target->getLastSubmissionSerial()==serial&&exact(stable,read(target.get())),"MSAA accumulation budget refusal exact and before commands");
  action.setMultipassBufferBudget(256ull*1024*1024);draw(n,"MSAA same-target valid recovery");
  action.setRenderTarget(nullptr);return good;
}
#endif
