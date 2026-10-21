#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/CoinRenderOptions.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <string>
#include "CoinRenderEglLifetimeFixture.h"
namespace {
constexpr int width=64;
bool check(bool good,const char *label) {
  if(!good)std::cerr << "bgfx_opengl_lifetime FAIL " << label << '\n';
  return good;
}
struct Scene {
  SoSeparator *root=new SoSeparator;
  SoMaterial *material=new SoMaterial;
  explicit Scene(bool sloped=false) {
    root->ref();
    auto *camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,3);camera->height=2;
    camera->nearDistance=1;camera->farDistance=5;root->addChild(camera);
    auto *light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;
    root->addChild(light);root->addChild(material);
    auto *points=new SoCoordinate3;
    const SbVec3f quad[]={SbVec3f(-.8f,-.8f,0),SbVec3f(.8f,-.8f,0),
      SbVec3f(.8f,.8f,0),SbVec3f(-.8f,sloped?.63f:.8f,0)};
    points->point.setValues(0,4,quad);root->addChild(points);
    auto *face=new SoFaceSet;face->numVertices.set1Value(0,4);root->addChild(face);
  }
  ~Scene(){root->unref();}
};
unsigned char channel(unsigned long value,unsigned long mask) {
  if(!mask)return 0;
  while(!(mask&1)) {mask>>=1;value>>=1;}
  return static_cast<unsigned char>(((value&mask)*255+mask/2)/mask);
}
std::array<uint8_t,3> physical(Display *display,Window window,int side=width) {
  XSync(display,False);
  XImage *image=XGetImage(display,window,side/2,side/2,1,1,AllPlanes,ZPixmap);
  if(!image)return {{0,0,0}};
  const auto value=XGetPixel(image,0,0);
  const std::array<uint8_t,3> result={{channel(value,image->red_mask),
    channel(value,image->green_mask),channel(value,image->blue_mask)}};
  XDestroyImage(image);return result;
}
bool matches(const std::array<uint8_t,3> &actual,
             const std::array<uint8_t,3> &expected) {
  for(int c=0;c<3;++c)
    if(std::abs(int(actual[c])-int(expected[c]))>3)return false;
  return true;
}
struct Settings {
  int cycles=12;
  unsigned samples=1;
  bool sameDisplay=false, sameWindow=false, transition=false, distinctFrames=false;
  bool resize=false, worker=false, hostEgl=false, hostEglSameDisplay=false;
};
bool sampleCoverage(const std::vector<uint8_t>&rgba,unsigned samples,
                    bool window,const std::array<uint8_t,3>&color) {
  unsigned partial=0;
  bool quantized=true,oddLevel=false;
  int dominant=0;
  for(int c=1;c<3;++c)if(color[c]>color[dominant])dominant=c;
  for(size_t p=0;p<rgba.size();p+=4) {
    // An opaque window can publish alpha=255 everywhere. Its black clear and
    // authored flat color instead expose RGB = color * coveredSamples / N.
    const int value=rgba[p+(window?dominant:3)];
    const int full=window?color[dominant]:255;
    if(window?(value<=3||std::abs(value-full)<=3):(value==0||value==255))continue;
    ++partial;
    const int k=int(std::lround(double(value)*samples/full));
    oddLevel |= k%2!=0;
    quantized &= k>0&&k<int(samples);
    if(window) {
      for(int c=0;c<3;++c)
        quantized &= std::abs(int(rgba[p+c])-int(std::lround(double(color[c])*k/samples)))<=3;
    } else quantized &= std::abs(value-int(std::lround(255.*k/samples)))<=2;
  }
  std::cout<<"bgfx_opengl_lifetime coverage_samples="<<samples
    <<" oracle="<<(window?"rgb":"alpha")<<" partial_pixels="<<partial<<'\n';
  return check(quantized&&(samples==1?partial==0:partial>4)&&
    (samples!=4||oddLevel),
    "sloped edge proves requested sample coverage without lower-sample fallback");
}
bool hostIntact(egl_lifetime::Context *host) {
  if(!host)return true;
  if(!check(host->unchanged(),"caller EGL API/display/context/draw/read binding preserved"))return false;
  const auto p=host->pixel();
  return check(matches({{p[0],p[1],p[2]}},{{17,35,201}})&&p[3]==255,
    "caller EGL context remains usable with original pbuffer contents");
}
bool cycle(int index,const CoinRenderOptions &options,Display *borrowed,Window borrowedWindow,
           const Settings &settings,egl_lifetime::Context *host) {
  Display *display=borrowed?borrowed:XOpenDisplay(nullptr);
  if(!check(display!=nullptr,"XOpenDisplay"))return false;
  Window window=borrowedWindow?borrowedWindow:XCreateSimpleWindow(display,DefaultRootWindow(display),
    24+index*2,24+index*2,width,width,0,0,0);
  if(borrowedWindow)XResizeWindow(display,window,width,width);
  XSetWindowAttributes attributes{};attributes.override_redirect=True;
  XChangeWindowAttributes(display,window,CWOverrideRedirect,&attributes);
  XMapRaised(display,window);XSync(display,False);
  CoinRenderNativeSurfaceDescriptor native{};
  native.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  native.structSize=sizeof(native);native.type=COIN_RENDER_SURFACE_XLIB;
  native.native.xlib.display=display;native.native.xlib.window=window;
  Scene scene(settings.distinctFrames);
  CoinRenderAction action(SbViewportRegion(width,width));
  action.setBackgroundColor(SbColor4f(0,0,0,settings.distinctFrames?0.f:1.f));
  const int frames=settings.distinctFrames?2:1;
  bool ok=true;
  // There is deliberately no peer target anchoring the runtime between scopes.
  for(int kind=0;kind<2;++kind) {
    const bool offscreen=kind==0;
    int side=width;
    std::unique_ptr<CoinRenderTarget> target(offscreen?
      CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options):
      CoinRenderTarget::createWindow(native,SbVec2i32(side,side),options));
    ok &= check(bool(target) && target->getOptions().renderer==options.renderer &&
      target->getOptions().sampleCount==options.sampleCount,
      "target preserves explicit OpenGL renderer and sample options");
    ok &= hostIntact(host);
    uint64_t previous=0;
    if(target) {
      const int submissions=frames+(settings.resize?1:0);
      for(int frame=0;frame<submissions;++frame) {
        if(settings.resize && frame==frames) {
          side=80;
          if(!offscreen){XResizeWindow(display,window,side,side);XSync(display,False);}
          ok &= check(target->resize(SbVec2i32(side,side)),"resize succeeds");
          ok &= check(target->getSize()==SbVec2i32(side,side),"resized target dimensions");
        }
        action.setViewportRegion(SbViewportRegion(side,side));
        const std::array<uint8_t,3> color=offscreen?
          std::array<uint8_t,3>{{uint8_t(40+index*7),uint8_t(190-frame*50),uint8_t(50+frame*60)}}:
          std::array<uint8_t,3>{{uint8_t(210-frame*50),uint8_t(30+index*7),uint8_t(80+frame*60)}};
        scene.material->diffuseColor.setValue(color[0]/255.f,color[1]/255.f,color[2]/255.f);
        action.setRenderTarget(target.get());
        // Default retains the original publication path. Extended probes also
        // observe the submitted window image independently of X presentation.
        const bool windowReadback=!offscreen&&(settings.distinctFrames||settings.resize);
        if(windowReadback)ok &= check(target->requestWindowReadbackRGBA(),"request window RGBA");
        action.apply(scene.root); // Exactly one submission; never retry a failed first frame.
        const auto serial=target->getLastSubmissionSerial();
        ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS && serial>previous,
          "successful submission advances serial on every distinct frame");
        previous=serial;
        if(offscreen||windowReadback) {
          std::vector<uint8_t> rgba;target->readbackRGBA(rgba);
          const size_t p=4u*(size_t(side/2)*side+side/2);
          const bool sizeOk=rgba.size()==size_t(side)*side*4;
          const std::array<uint8_t,3> actual=sizeOk?
            std::array<uint8_t,3>{{rgba[p],rgba[p+1],rgba[p+2]}}:
            std::array<uint8_t,3>{{0,0,0}};
          ok &= check(sizeOk&&matches(actual,color)&&rgba[p+3]==255,
            "current frame RGBA dimensions, center RGB and opaque alpha");
          if(settings.distinctFrames&&sizeOk)ok &= sampleCoverage(rgba,options.sampleCount,!offscreen,color);
        }
        if(!offscreen) {
          const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
          std::array<uint8_t,3> actual={{0,0,0}};
          do {
            actual=physical(display,window,side);
            if(matches(actual,color))break;
            // Observation only, never render again to make presentation pass.
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
          } while(std::chrono::steady_clock::now()<deadline);
          std::cout << "bgfx_opengl_lifetime cycle=" << index
            << " samples=" << options.sampleCount << " frame=" << frame
            << " side=" << side << " physical=" << int(actual[0]) << ','
            << int(actual[1]) << ',' << int(actual[2]) << " expected="
            << int(color[0]) << ',' << int(color[1]) << ',' << int(color[2])
            << " serial=" << serial << '\n';
          ok &= check(matches(actual,color),"window physical OpenGL publication");
        }
        ok &= hostIntact(host);
      }
      action.setRenderTarget(nullptr);target.reset();
      ok &= hostIntact(host);
    }
  }
  if(!borrowedWindow){XDestroyWindow(display,window);XSync(display,False);}
  if(!borrowed)XCloseDisplay(display); // Both targets and their contexts are dead.
  return ok;
}
}
int main(int argc,char **argv) {
  Settings settings;
  for(int i=1;i<argc;++i) {
    const std::string arg=argv[i];
    if(arg=="--same-display")settings.sameDisplay=true;
    else if(arg=="--same-window")settings.sameWindow=settings.sameDisplay=true;
    else if(arg=="--transition-2-to-4")settings.transition=true;
    else if(arg=="--distinct-frames")settings.distinctFrames=true;
    else if(arg=="--resize")settings.resize=true;
    else if(arg=="--worker")settings.worker=true;
    else if(arg=="--host-egl")settings.hostEgl=true;
    else if(arg=="--host-egl-same-display") {
      settings.hostEgl=settings.hostEglSameDisplay=settings.sameDisplay=true;
    }
    else if((arg=="--samples"||arg=="--cycles")&&i+1<argc) {
      const std::string value=argv[++i];
      if(arg=="--samples") {
        if(value!="1"&&value!="2"&&value!="4")return 2;
        settings.samples=unsigned(value[0]-'0');
      } else {
        if(value!="2"&&value!="4"&&value!="12")return 2;
        settings.cycles=std::stoi(value);
      }
    } else {std::cerr<<"invalid argument: "<<arg<<'\n';return 2;}
  }
  // Host EGL belongs to this caller thread; do not inspect it from a worker.
  if(settings.hostEgl&&settings.worker)return 2;
  if(!check(XInitThreads()!=0,"XInitThreads before worker/runtime initialization"))return 1;
  SoDB::init();CoinRenderAction::initClass();
  std::unique_ptr<egl_lifetime::Api> egl;
  Display *hostDisplay=nullptr;
  std::unique_ptr<egl_lifetime::Context> host;
  if(settings.hostEgl) {
    egl.reset(new egl_lifetime::Api);
    if(!check(egl->ready(),"host EGL entry points"))return 1;
    hostDisplay=XOpenDisplay(nullptr);
    if(!check(hostDisplay!=nullptr,"host XOpenDisplay"))return 1;
    host.reset(new egl_lifetime::Context(*egl,hostDisplay));
    if(!check(host->initialize(false)&&host->makeCurrent(),"caller EGL pbuffer context")) {
      host.reset();egl->ReleaseThread();XCloseDisplay(hostDisplay);return 1;
    }
    host->paint({{17,35,201}});
  }
  CoinRenderCapabilities caps{};
  const bool supported=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,
    &caps,sizeof(caps))==0 && caps.gpu_available &&
    caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION &&
    caps.renderer==COIN_RENDER_RENDERER_OPENGL;
  bool ok=hostIntact(host.get());
  if(supported) {
    std::cout << "bgfx_opengl_lifetime backend=" << caps.backend
      << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
      << " adapter=" << caps.adapter_name << '\n';
    Display *shared=settings.sameDisplay?
      (settings.hostEglSameDisplay?hostDisplay:XOpenDisplay(nullptr)):nullptr;
    if(host)std::cout<<"bgfx_opengl_lifetime host_display="<<hostDisplay
      <<" target_display="<<(shared?shared:nullptr)
      <<" shared_with_host="<<settings.hostEglSameDisplay<<'\n';
    if(settings.sameDisplay&&!check(shared!=nullptr,"shared XOpenDisplay"))ok=false;
    Window sharedWindow=settings.sameWindow&&shared?XCreateSimpleWindow(shared,
      DefaultRootWindow(shared),24,24,width,width,0,0,0):0;
    if(!settings.sameDisplay||shared)for(int i=0;i<settings.cycles;++i) {
      CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_OPENGL;
      options.sampleCount=settings.transition?(i%2?4:2):settings.samples;
      bool result=false;
      if(settings.worker) {
        // Ownership never crosses a live thread boundary. Joining each worker
        // exercises TLS teardown before the next worker creates a new runtime.
        std::thread worker([&]{result=cycle(i,options,shared,sharedWindow,settings,nullptr);});
        worker.join();
      } else result=cycle(i,options,shared,sharedWindow,settings,host.get());
      ok &= result;
      if(!result)break;
    }
    if(sharedWindow){XDestroyWindow(shared,sharedWindow);XSync(shared,False);}
    if(shared&&shared!=hostDisplay)XCloseDisplay(shared);
  }
  ok &= hostIntact(host.get());
  if(host) {
    // Terminating a shared EGLDisplay may leave a current context usable only
    // until unbound. Rebind it to distinguish a live host from that zombie.
    const bool reusable=egl->MakeCurrent(host->display,nullptr,nullptr,nullptr)&&
      egl->MakeCurrent(host->display,host->surface,host->surface,host->context);
    ok &= check(reusable,"caller EGL context and surfaces survive unbind/rebind");
    if(reusable)ok &= hostIntact(host.get());
    ok &= check(host->close(),"caller-owned EGL cleanup succeeds");host.reset();
  }
  if(hostDisplay){ok &= check(egl->ReleaseThread(),"caller EGL TLS cleanup");XCloseDisplay(hostDisplay);}
  return !ok?1:supported?0:77;
}
