// Run against a real X server with XFIXES disabled. The test never negotiates it.
// Missing-extension reproduction: Xvfb :N -screen 0 640x480x24 -noreset
//   -extension XFIXES -extension GLX
// Disabling GLX avoids an Xvfb server assertion observed during NVIDIA GPU
// startup; keep the real-server XFIXES-absence precondition below unchanged.
#define COIN_RENDER_SHADOW_MULTITEXTURE_WINDOW_FIXTURE_ONLY
#include "CoinRenderShadowMultitextureWindowTest.cpp"
#include <cctype>
namespace {
bool mentionsXfixes(std::string diagnostic){
  for(auto&c:diagnostic)c=char(std::tolower(static_cast<unsigned char>(c)));
  return diagnostic.find("xfixes")!=std::string::npos;
}
struct NativeWindow {
  Display*d=XOpenDisplay(nullptr);Window w=0;
  NativeWindow(){if(d){w=XCreateSimpleWindow(d,DefaultRootWindow(d),16,16,side,side,0,0,0);
    XSetWindowAttributes a{};a.override_redirect=True;XChangeWindowAttributes(d,w,CWOverrideRedirect,&a);
    XMapRaised(d,w);XClearWindow(d,w);XSync(d,False);}}
  ~NativeWindow(){if(w)XDestroyWindow(d,w);if(d)XCloseDisplay(d);}
};
struct PlainScene {
  SoSeparator*root=new SoSeparator;
  PlainScene(){root->ref();auto*c=new SoOrthographicCamera;c->position.setValue(0,0,6);c->height=4;
    c->nearDistance=1;c->farDistance=20;root->addChild(c);
    auto*l=new SoLightModel;l->model=SoLightModel::BASE_COLOR;root->addChild(l);
    auto*m=new SoMaterial;m->diffuseColor.setValue(.2f,.7f,.4f);root->addChild(m);root->addChild(new SoCube);}
  ~PlainScene(){root->unref();}
};
bool refusal(const CoinRenderCapabilities&caps){
  // This peer establishes the GPU runtime before the unsupported WSI attempt.
  auto options=samplingTestOptions();options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  std::unique_ptr<CoinRenderTarget> peer(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
  if(!check(bool(peer),"offscreen peer created without XFIXES prerequisite"))return false;
  auto&peerState=peer->getPimpl().get();
  if(!check(peerState.prepareBackend()==CoinRenderBackendStatus::SUCCESS&&peerState.backend&&peerState.backend->isGpuBackend(),
    "offscreen GPU runtime remains available without XFIXES"))return false;
  CoinRenderAction peerAction{SbViewportRegion(side,side)};peerAction.setRenderTarget(peer.get());
  PlainScene scene;peerAction.apply(scene.root);std::vector<uint8_t>before;peer->readbackRGBA(before);
  const uint64_t beforeSerial=peer->getLastSubmissionSerial();
  if(!check(peerAction.getLastStatus()==CoinRenderAction::SUCCESS&&beforeSerial>0&&before.size()==size_t(side)*side*4,
    "offscreen peer publishes before WSI refusal"))return false;
  {
    NativeWindow native;if(!check(native.d&&native.w,"real X11 window for capability refusal"))return false;
    int opcode=0,event=0,error=0;
    if(!check(!XQueryExtension(native.d,"XFIXES",&opcode,&event,&error),
      "test precondition: real server has XFIXES disabled"))return false;
    const auto physicalBefore=ximage(native.d,native.w,side);
    if(!check(!physicalBefore.empty(),"unsubmitted physical window can be inspected"))return false;
    CoinRenderNativeSurfaceDescriptor descriptor{};
    descriptor.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;descriptor.structSize=sizeof(descriptor);
    descriptor.type=COIN_RENDER_SURFACE_XLIB;descriptor.native.xlib.display=native.d;descriptor.native.xlib.window=native.w;
    std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createWindow(descriptor,SbVec2i32(side,side),options));
    if(!check(bool(target),"window target retains diagnostic on unsupported WSI profile"))return false;
    auto&state=target->getPimpl().get();const auto status=state.prepareBackend();
    const std::string diagnostic=std::string(target->getLastError())+" "+(state.backend?state.backend->getLastError():std::string());
    std::cout<<"x11_wsi_refusal prepare_status="<<int(status)<<" target_status="<<int(target->getStatus())
      <<" serial="<<target->getLastSubmissionSerial()<<" diagnostic="<<diagnostic<<'\n';
    if(!check(status!=CoinRenderBackendStatus::SUCCESS&&mentionsXfixes(diagnostic)&&target->getLastSubmissionSerial()==0,
      "missing XFIXES refuses backend preparation with named diagnostic before publication"))return false;
    CoinRenderAction action{SbViewportRegion(side,side)};action.setRenderTarget(target.get());action.apply(scene.root);
    std::vector<uint8_t>pixels;target->readbackRGBA(pixels);
    if(!check(action.getLastStatus()!=CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()==0&&pixels.empty(),
      "refused WSI cannot publish pixels or a submission serial"))return false;
    if(!check(ximage(native.d,native.w,side)==physicalBefore,"capability refusal preserves caller's physical window"))return false;
    action.setRenderTarget(nullptr);target.reset();
    // NativeWindow now destroys the caller-owned window and Display while the
    // offscreen peer's runtime is still alive.
  }
  peerAction.apply(scene.root);std::vector<uint8_t>after;peer->readbackRGBA(after);
  return check(peerAction.getLastStatus()==CoinRenderAction::SUCCESS&&peer->getLastSubmissionSerial()==beforeSerial+1&&after==before,
    "same offscreen GPU peer recovers exactly after WSI refusal and caller closes Display");
}
}
int main(int argc,char**argv){
  if(argc!=2||std::string(argv[1])!="--expect-wsi-refusal")return 2;
  if(!XInitThreads())return 1;SoDB::init();CoinRenderAction::initClass();
  const auto caps=msaa_shadow_test::capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN);
  if(!caps.gpu_available||caps.renderer==COIN_RENDER_RENDERER_UNKNOWN)return 77;
  return refusal(caps)?0:1;
}
