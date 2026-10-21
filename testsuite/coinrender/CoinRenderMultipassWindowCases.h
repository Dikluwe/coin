#ifndef COIN_RENDER_MULTIPASS_WINDOW_CASES_H
#define COIN_RENDER_MULTIPASS_WINDOW_CASES_H
uint8_t windowChannel(unsigned long pixel,unsigned long mask) {
  if(!mask)return 0;
  while(!(mask&1)){pixel>>=1;mask>>=1;}
  return uint8_t(((pixel&mask)*255+mask/2)/mask);
}
std::vector<uint8_t> physicalFrame(Display *display,Window window,int extent) {
  XSync(display,False);
  XImage *image=XGetImage(display,window,0,0,extent,extent,AllPlanes,ZPixmap);
  if(!image)return {};
  std::vector<uint8_t> rgba(size_t(extent)*extent*4,255);
  for(int y=0;y<extent;++y)for(int x=0;x<extent;++x) {
    const unsigned long value=XGetPixel(image,x,y);const size_t p=size_t(y*extent+x)*4;
    rgba[p]=windowChannel(value,image->red_mask);
    rgba[p+1]=windowChannel(value,image->green_mask);
    rgba[p+2]=windowChannel(value,image->blue_mask);
  }
  XDestroyImage(image);return rgba;
}
bool windowNumeric(const std::vector<uint8_t>&rgba,int extent,int n,double edge,bool report) {
  if(rgba.size()!=size_t(extent)*extent*4)return false;
  unsigned bad=0,partial=0,bright=0;
  const auto positions=samples(n);
  const double right=edge*extent/side,left=double(extent)/8;
  for(int y=extent/4;y<3*extent/4;++y)for(int x=0;x<extent;++x) {
    unsigned covered=0;
    for(const auto&j:positions){const double p=x+1-j[0];covered+=p>left&&p<right;}
    const double color[]={.25,.5,.75};const size_t p=size_t(y*extent+x)*4;
    for(unsigned c=0;c<3;++c)bad+=std::abs(double(rgba[p+c])-255*color[c]*covered/n)>3;
    partial+=covered>0&&covered<unsigned(n);bright+=covered==unsigned(n)&&x>extent/4&&x<3*extent/8;
  }
  if(report)std::cout<<"multipass_window_math extent="<<extent<<" n="<<n<<" bad="<<bad<<" partial="<<partial<<" bright="<<bright<<'\n';
  // Window alpha is not an observable X11 coverage channel; k/N is measured in
  // RGB. The offscreen profile separately proves the complete RGBA resolve.
  return !bad&&bright>100&&(n==1||partial>=unsigned(extent/2));
}
bool rgbCurrent(const std::vector<uint8_t>&a,const std::vector<uint8_t>&b) {
  if(a.size()!=b.size()||a.empty())return false;
  for(size_t p=0;p<a.size();p+=4)for(unsigned c=0;c<3;++c)
    if(std::abs(int(a[p+c])-int(b[p+c]))>3)return false;
  return true;
}
bool msaaWindowNumeric(const std::vector<uint8_t>&rgba,int extent,int n,unsigned m,const SbColor &color,bool report){
  if(rgba.size()!=size_t(extent)*extent*4)return false;
  unsigned bad=0,partial=0,bright=0,beyond=0,odd=0;const unsigned levels=unsigned(n)*m;
  for(int y=0;y<extent;++y)for(int x=0;x<extent;++x){const size_t p=size_t(y*extent+x)*4;double best=1e9;unsigned coverage=0;
    for(unsigned k=0;k<=levels;++k){double error=0;for(unsigned c=0;c<3;++c)error=std::max(error,std::abs(double(rgba[p+c])-255*color[c]*k/levels));if(error<best){best=error;coverage=k;}}
    bad+=best>3;partial+=coverage>0&&coverage<levels;odd+=(coverage&1)!=0;
    if(coverage>0&&coverage<levels){double nearest=1;for(int j=0;j<=n;++j)nearest=std::min(nearest,std::abs(double(coverage)/levels-double(j)/n));beyond+=nearest>4./255;}
    if(x>=extent*3/16&&x<extent*3/8&&y>=extent*5/16&&y<extent*11/16)bright+=coverage==levels;
    if(x<extent/32)bad+=coverage!=0;
  }
  if(report)std::cout<<"multipass_window_msaa N="<<n<<" M="<<m<<" extent="<<extent<<" bad="<<bad<<" partial="<<partial<<" bright="<<bright<<" beyond_1x="<<beyond<<" odd="<<odd<<'\n';
  return !bad&&partial>4&&bright>100&&beyond>4&&(m!=4||odd>4);
}
struct NativeWindow {
  Display *display=nullptr;Window window=0;
  std::unique_ptr<CoinRenderSceneManager> manager;
  Audit *audit=nullptr;
  int extent=side;
  explicit NativeWindow(const CoinRenderOptions &options) {
    display=XOpenDisplay(nullptr);if(!display)return;
    window=XCreateSimpleWindow(display,DefaultRootWindow(display),16,16,extent,extent,0,0,0);
    XSetWindowAttributes attributes{};attributes.override_redirect=True;
    XChangeWindowAttributes(display,window,CWOverrideRedirect,&attributes);
    XMapRaised(display,window);XSync(display,False);
    CoinRenderNativeSurfaceDescriptor native{};
    native.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;native.structSize=sizeof(native);
    native.type=COIN_RENDER_SURFACE_XLIB;native.native.xlib.display=display;native.native.xlib.window=window;
    manager.reset(new CoinRenderSceneManager(native,SbVec2i32(extent,extent),options));
    auto *target=manager->getRenderTarget();if(!target)return;
    auto &p=target->getPimpl().get();
    if(!check(p.prepareBackend()==CoinRenderBackendStatus::SUCCESS&&p.backend&&p.backend->isGpuBackend(),"real native window GPU executor"))return;
    audit=new Audit(std::move(p.backend));p.backend.reset(audit);
    // A native window has no public depth-readback contract. Never opt into a
    // fictitious window depth attachment/readback merely for this test.
    manager->setTransparencyType(CoinRenderAction::NONE);
    manager->setBackgroundColor(SbColor4f(0,0,0,0));
    manager->setViewportRegion(SbViewportRegion(extent,extent));
  }
  ~NativeWindow() {
    manager.reset();
    if(window)XDestroyWindow(display,window);
    if(display)XCloseDisplay(display);
  }
  bool resize(int value) {
    extent=value;XResizeWindow(display,window,unsigned(value),unsigned(value));XSync(display,False);
    const bool ok=manager->resize(SbVec2i32(value,value));
    manager->setViewportRegion(SbViewportRegion(value,value));return check(ok,"same native target resize");
  }
};
int windowPath(const CoinRenderOptions &options,int requested) {
  Scene scene;if(options.sampleCount>1)msaaGeometry(scene);NativeWindow surface(options);
  if(!surface.display)return 77;
  if(!surface.audit)return 1;
  auto *target=surface.manager->getRenderTarget();
  surface.manager->setSceneGraph(scene.root);
  std::vector<uint8_t> current,shown;
  auto draw=[&](int n,const char *label) {
    surface.manager->setNumPasses(n);
    const unsigned captures=scene.captures;const auto source=scene.root->getNodeId();
    const auto serial=target->getLastSubmissionSerial();
    check(target->requestWindowReadbackRGBA(),"request explicit same-apply native readback");
    const auto status=surface.manager->render();target->readbackRGBA(current);
    std::cout<<"multipass_window_apply "<<label<<" n="<<n<<" extent="<<surface.extent<<" status="<<status<<" serial="<<serial<<"->"<<target->getLastSubmissionSerial()<<" error="<<surface.manager->getLastError().getString()<<'\n';
    if(!check(status==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial&&scene.captures==captures+1&&scene.root->getNodeId()==source,"one current atomic window publication and immutable capture"))return false;
    auto numeric=[&](const std::vector<uint8_t>&image,bool report){return options.sampleCount>1?msaaWindowNumeric(image,surface.extent,n,options.sampleCount,scene.material->diffuseColor[0],report):windowNumeric(image,surface.extent,n,scene.edge,report);};
    check(numeric(current,true),"same-apply window readback independent RGB mean");
    const auto start=std::chrono::steady_clock::now();
    bool present=false;
    do {
      shown=physicalFrame(surface.display,surface.window,surface.extent);
      present=rgbCurrent(shown,current)&&numeric(shown,false);
      if(present)break;
    } while(std::chrono::steady_clock::now()-start<std::chrono::seconds(5));
    std::cout<<"multipass_window_present "<<label<<" current="<<present<<" ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<'\n';
    return check(present,"physical CURRENT analytic image within original five seconds, without another apply");
  };
  const int n=requested?requested:3;
  draw(n,"first multipass physical image");
  if(options.sampleCount>1)scene.material->diffuseColor.setValue(.75f,.25f,.5f);else scene.move(40.33);draw(n,"physical authored mutation");
  if(options.sampleCount>1)scene.material->diffuseColor.setValue(.25f,.5f,.75f);else scene.move(32.33);draw(n,"physical authorial restoration");
  const auto stable=current,stablePhysical=shown;const auto serial=target->getLastSubmissionSerial();
  const unsigned commands=surface.audit->commands;
  surface.manager->setNumPasses(0);
  check(surface.manager->render()==CoinRenderAction::INVALID_SCENE,"native invalid count refuses");
  std::vector<uint8_t> after;target->readbackRGBA(after);
  check(after==stable&&target->getLastSubmissionSerial()==serial&&surface.audit->commands==commands&&physicalFrame(surface.display,surface.window,surface.extent)==stablePhysical,"refusal preserves exact published readback, serial and physical frame before any command");
  draw(n,"valid recovery after native refusal");
  for(int extent:{80,96,64})if(surface.resize(extent))draw(n,"native resize current multipass image");
  check(surface.manager->resize(SbVec2i32(0,0)),"suspend same native multipass target");
  // Manager.resize also sets its capture viewport to zero. A nonempty scene
  // then fails ordinary viewport validation before target admission in the
  // native one-pass path. Retain a valid authored capture viewport here to
  // test suspension of the target itself, without creating an invalid scene.
  surface.manager->setViewportRegion(SbViewportRegion(surface.extent,surface.extent));
  const auto suspendedSerial=target->getLastSubmissionSerial();const unsigned suspendedCommands=surface.audit->commands;std::vector<uint8_t> suspendedBefore,suspendedAfter;target->readbackRGBA(suspendedBefore);
  const auto suspendedStatus=surface.manager->render();
  std::cout<<"multipass_window_suspend status="<<suspendedStatus<<" error="<<surface.manager->getLastError().getString()<<'\n';
  check(suspendedStatus==CoinRenderAction::NOT_READY,"suspended native multipass refuses without acquisition");target->readbackRGBA(suspendedAfter);
  check(target->getLastSubmissionSerial()==suspendedSerial&&surface.audit->commands==suspendedCommands&&suspendedBefore==suspendedAfter,"suspension keeps current publication and submits no AA sample");
  if(surface.resize(side))draw(n,"first native image after suspend/restore");
  return good?0:1;
}
#endif
