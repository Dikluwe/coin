#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderMsaaShadowFixture.h"
#include <Inventor/SoRenderManager.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <chrono>
#include <thread>
using namespace msaa_shadow_test;
namespace {
uint8_t channel(unsigned long value,unsigned long mask){
  if(!mask)return 0;while(!(mask&1)){value>>=1;mask>>=1;}
  return uint8_t(((value&mask)*255+mask/2)/mask);
}
std::vector<uint8_t> physical(Display *display,Window window,int extent){
  XSync(display,False);
  XImage *raw=XGetImage(display,window,0,0,extent,extent,AllPlanes,ZPixmap);
  if(!raw)return {};
  std::vector<uint8_t> rgba(size_t(extent)*extent*4,255);
  for(int y=0;y<extent;++y)for(int x=0;x<extent;++x){
    const auto value=XGetPixel(raw,x,y);
    const size_t p=(size_t(y)*extent+x)*4;
    rgba[p]=channel(value,raw->red_mask);
    rgba[p+1]=channel(value,raw->green_mask);
    rgba[p+2]=channel(value,raw->blue_mask);
  }
  XDestroyImage(raw);return rgba;
}
bool observe(Display *display,Window window,int extent,
             const std::vector<uint8_t> &expected,
             std::vector<uint8_t> &lastAccepted,const char *phase,
             std::chrono::steady_clock::time_point returned){
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  std::vector<uint8_t> actual;size_t wrong=0,maxDelta=0,first=SIZE_MAX;
  unsigned reads=0;
  auto firstRead=deadline,lastReadBegin=deadline,lastReadEnd=deadline;
  double maximumReadMs=0;
  do{
    lastReadBegin=std::chrono::steady_clock::now();if(!reads)firstRead=lastReadBegin;
    actual=physical(display,window,extent);
    lastReadEnd=std::chrono::steady_clock::now();++reads;
    maximumReadMs=std::max(maximumReadMs,std::chrono::duration<double,std::milli>(lastReadEnd-lastReadBegin).count());
    wrong=0;maxDelta=0;first=SIZE_MAX;
    if(actual.size()!=expected.size())wrong=expected.size();
    else for(size_t i=0;i<expected.size();i+=4)
      for(int c=0;c<3;++c){
        const size_t delta=size_t(std::abs(int(actual[i+c])-int(expected[i+c])));
        if(delta && first==SIZE_MAX)first=i+size_t(c);
        wrong+=(delta!=0);maxDelta=std::max(maxDelta,delta);
      }
    if(wrong==0)break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }while(std::chrono::steady_clock::now()<deadline);
  const auto offsetMs=[&](std::chrono::steady_clock::time_point t){
    return std::chrono::duration<double,std::milli>(t-returned).count();
  };
  const bool previousComparable=!lastAccepted.empty()&&actual.size()==lastAccepted.size();
  std::cout<<"msaa_shadow_window phase="<<phase<<" size="<<extent
           <<" mismatched_channels="<<wrong<<" max_delta="<<maxDelta
           <<" previous_comparable="<<previousComparable
           <<" same_previous="<<(previousComparable&&actual==lastAccepted)
           <<" reads="<<reads<<" first_read_begin_ms="<<offsetMs(firstRead)
           <<" last_read_begin_ms="<<offsetMs(lastReadBegin)
           <<" last_read_end_ms="<<offsetMs(lastReadEnd)<<" max_read_ms="<<maximumReadMs;
  if(first!=SIZE_MAX){
    const size_t pixel=first/4;
    std::cout<<" first="<<pixel%size_t(extent)<<','<<pixel/size_t(extent)
      <<" channel="<<first%4<<" actual="<<int(actual[first])
      <<" expected="<<int(expected[first]);
  }
  std::cout<<'\n';
  if(wrong==0 && actual.size()==expected.size())lastAccepted=actual;
  return check(wrong==0 && actual.size()==expected.size(),
    "physical X11 pixels equal single-apply offscreen reference");
}
bool run(bool keepRuntime){
  const auto caps=capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW);
  if(!check(caps.gpu_available && caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN &&
    (caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION ||
     caps.backend==COIN_RENDER_EXPERIMENTAL_RUST),
     "identified physical window GPU"))return false;
  Display *display=XOpenDisplay(nullptr);
  if(!check(display!=nullptr,"X11 display available"))return false;
  Window window=XCreateSimpleWindow(display,DefaultRootWindow(display),8,8,32,32,0,0,0);
  XSetWindowAttributes attributes{};attributes.override_redirect=True;
  XChangeWindowAttributes(display,window,CWOverrideRedirect,&attributes);
  XMapRaised(display,window);XSync(display,False);
  CoinRenderNativeSurfaceDescriptor native{};
  native.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  native.structSize=sizeof(native);native.type=COIN_RENDER_SURFACE_XLIB;
  native.native.xlib.display=display;native.native.xlib.window=window;
  bool ok=true;
  uint64_t previousDeviceSerial=0;
  std::unique_ptr<Scene> anchorScene;
  std::unique_ptr<Harness> liveRuntime;
  if(keepRuntime){
    anchorScene.reset(new Scene(1,false));
    liveRuntime.reset(new Harness(caps,1));
    Frame anchor;
    if(!liveRuntime->render(anchorScene->root,anchor,
      "live one-sample runtime anchor")){
      liveRuntime.reset();anchorScene.reset();
      XDestroyWindow(display,window);XCloseDisplay(display);return false;
    }
    previousDeviceSerial=anchor.serial;
  }
  // Preserve only the last accepted CPU image across complete 2x/4x teardown.
  // The original unanchored runtime lifetime is unchanged.
  std::vector<uint8_t> lastPhysical;
  for(unsigned samples:{2u,4u}){
    Scene scene(1,false);SoRenderManager manager;
    manager.setSceneGraph(scene.root);
    manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
    manager.setBackgroundColor(SbColor4f(0,0,0,0));
    auto selected=options(caps,samples);
    CoinRenderManagerAdapter off(manager,SbVec2i32(32,32),selected);
    CoinRenderManagerAdapter shown(manager,native,SbVec2i32(32,32),selected);
    auto *offTarget=off.getSceneManager()->getRenderTarget();
    auto *windowTarget=shown.getSceneManager()->getRenderTarget();
    if(!check(offTarget && windowTarget &&
       offTarget->getOptions().renderer==caps.renderer &&
       windowTarget->getOptions().renderer==caps.renderer &&
       offTarget->getOptions().sampleCount==samples &&
       windowTarget->getOptions().sampleCount==samples,
       "offscreen/window retain requested adapter and MSAA count")){ok=false;break;}
    offTarget->setDepthReadbackEnabled(TRUE);
    std::vector<uint8_t> first,last;
    uint64_t previousOff=0,previousWindow=0;
    for(int phase=0;phase<3;++phase){
      const int extent=phase==1?48:32;
      if(phase>0){
        XResizeWindow(display,window,extent,extent);XSync(display,False);
        ok &= check(off.resize(SbVec2i32(extent,extent)) &&
          shown.resize(SbVec2i32(extent,extent)),
          "paired offscreen/window resize succeeds");
      }
      manager.setViewportRegion(SbViewportRegion(extent,extent));
      scene.casterX(0,phase==1?.65f:0.f);
      // Resize resets the target's public last-serial field, while the live
      // backend counter continues. Compare to the last published frame.
      const uint64_t beforeOff=previousOff,beforeWindow=previousWindow;
      const auto offStatus=off.render(),windowStatus=shown.render();
      const auto returned=std::chrono::steady_clock::now();
      std::vector<uint8_t> expected;offTarget->readbackRGBA(expected);
      std::cout<<"msaa_shadow_window samples="<<samples<<" phase="<<phase
               <<" off_status="<<int(offStatus)
               <<" window_status="<<int(windowStatus)
               <<" expected_bytes="<<size_t(extent)*extent*4
               <<" actual_bytes="<<expected.size()
               <<" previous_off="<<beforeOff
               <<" previous_window="<<beforeWindow
               <<" off_serial="<<offTarget->getLastSubmissionSerial()
               <<" window_serial="<<windowTarget->getLastSubmissionSerial()<<'\n';
      const uint64_t expectedOff=caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?
        previousDeviceSerial+1:beforeOff+1;
      const uint64_t expectedWindow=caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?
        expectedOff+1:beforeWindow+1;
      ok &= check(offStatus==CoinRenderAction::SUCCESS &&
        windowStatus==CoinRenderAction::SUCCESS &&
        expected.size()==size_t(extent)*extent*4 &&
        offTarget->getLastSubmissionSerial()==expectedOff &&
        windowTarget->getLastSubmissionSerial()==expectedWindow,
        "two ordered physical publications: target-local BGFX or device-global wgpu serial");
      size_t visible=0;
      for(size_t p=3;p<expected.size();p+=4)visible+=(expected[p]>0);
      ok &= check(visible>size_t(extent)*extent/4,
        "window reference has substantial shadowed receiver coverage");
      if(ok)ok &= observe(display,window,extent,expected,lastPhysical,
        "shadow presentation",returned);
      std::cout<<"msaa_shadow_window observation_serial samples="<<samples<<" phase="<<phase
               <<" serial_after_reads="<<windowTarget->getLastSubmissionSerial()<<'\n';
      if(phase==0)first=expected;
      if(phase==2)last=expected;
      previousOff=offTarget->getLastSubmissionSerial();
      previousWindow=windowTarget->getLastSubmissionSerial();
      previousDeviceSerial=previousWindow;
      if(!ok)break;
    }
    ok &= check(first.size()==last.size() && first==last,
      "caster A/B/A and 32-to-48-to-32 resize restore exact pixels");
    manager.setSceneGraph(nullptr);
    if(!ok)break;
  }
  liveRuntime.reset();anchorScene.reset();
  XDestroyWindow(display,window);XCloseDisplay(display);return ok;
}
}
int main(int argc,char **argv){
  if(argc!=2 || (std::string(argv[1])!="--window" &&
     std::string(argv[1])!="--window-live-runtime"))return 2;
  if(!XInitThreads())return 1;
  SoDB::init();CoinRenderAction::initClass();
  return run(std::string(argv[1])=="--window-live-runtime")?0:1;
}
