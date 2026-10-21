// Independent native control: intentionally links neither Coin nor BGFX.
#include "CoinRenderEglLifetimeFixture.h"
#include <X11/Xutil.h>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
namespace {
bool check(bool value,const char*label) {
  if(!value)std::cerr<<"native_egl_lifetime FAIL "<<label<<'\n';
  return value;
}
bool matches(const std::array<uint8_t,3>&a,const std::array<uint8_t,3>&b) {
  for(int c=0;c<3;++c)if(std::abs(int(a[c])-int(b[c]))>3)return false;
  return true;
}
unsigned char channel(unsigned long value,unsigned long mask) {
  if(!mask)return 0;
  while(!(mask&1)){mask>>=1;value>>=1;}
  return static_cast<unsigned char>(((value&mask)*255+mask/2)/mask);
}
std::array<uint8_t,3> physical(Display*d,Window w) {
  XSync(d,False);
  XImage *image=XGetImage(d,w,32,32,1,1,AllPlanes,ZPixmap);
  if(!image)return {{0,0,0}};
  const auto value=XGetPixel(image,0,0);
  const std::array<uint8_t,3> result={{channel(value,image->red_mask),
    channel(value,image->green_mask),channel(value,image->blue_mask)}};
  XDestroyImage(image);return result;
}
bool cycle(egl_lifetime::Api&api,int index,Display*borrowed,
           bool isolatedRuntimeDisplay,bool platformX11,bool sharedWindowContext) {
  Display*d=borrowed?borrowed:XOpenDisplay(nullptr);
  if(!check(d!=nullptr,"XOpenDisplay"))return false;
  bool ok=true;
  for(int kind=0;kind<2&&ok;++kind) {
    const bool window=kind==1;
    // Open from the caller connection's server string, not a new environment
    // lookup. Every pbuffer/window runtime owns a separate renderer connection.
    Display *renderer=isolatedRuntimeDisplay?XOpenDisplay(DisplayString(d)):d;
    if(!check(renderer!=nullptr,"renderer XOpenDisplay on caller server")){ok=false;break;}
    if(isolatedRuntimeDisplay)ok=check(renderer!=d,"renderer connection differs from caller");
    std::cout<<"native_egl_lifetime cycle="<<index<<" kind="<<(window?"window":"pbuffer")
      <<" api="<<(platformX11?"platform-x11":"legacy")<<" caller="<<d
      <<" renderer="<<renderer<<" isolated="<<isolatedRuntimeDisplay
      <<" shared_window_context="<<(sharedWindowContext&&window)<<std::endl;
    egl_lifetime::Context context(api,renderer);
    ok=check(context.initialize(window,platformX11,sharedWindowContext&&window),"initialize EGL and choose desktop GL config")&&ok;
    Window w=0;Colormap colormap=0;
    egl_lifetime::Handle secondaryContext=nullptr,secondarySurface=nullptr;
    if(ok&&window) {
      egl_lifetime::Int visualId=0;
      ok=check(api.GetConfigAttrib(context.display,context.config,
        egl_lifetime::NATIVE_VISUAL_ID,&visualId),"EGL native visual");
      XVisualInfo pattern{};pattern.visualid=static_cast<VisualID>(visualId);
      int count=0;XVisualInfo *visual=ok?XGetVisualInfo(d,VisualIDMask,&pattern,&count):nullptr;
      ok=check(visual&&count>0,"X11 visual for EGL config")&&ok;
      if(ok) {
        const Window root=RootWindow(d,visual->screen);
        colormap=XCreateColormap(d,root,visual->visual,AllocNone);
        XSetWindowAttributes attrs{};attrs.colormap=colormap;
        attrs.override_redirect=True;attrs.border_pixel=0;
        w=XCreateWindow(d,root,24+index*2,24+index*2,64,64,0,visual->depth,
          InputOutput,visual->visual,CWColormap|CWOverrideRedirect|CWBorderPixel,&attrs);
        XMapRaised(d,w);XSync(d,False);
      }
      if(visual)XFree(visual);
    }
    // Flush caller window creation before EGL accesses its XID through the
    // renderer's independent X11 connection. Never transfer window ownership.
    if(ok){XSync(d,False);if(renderer!=d)XSync(renderer,False);}
    if(ok)ok=check(context.makeCurrent(sharedWindowContext&&window?0:w),
      "create primary surface/context and make current");
    if(ok&&sharedWindowContext&&window) {
      context.paint({{17,35,201}});
      const auto primary=context.pixel();
      ok=check(matches({{primary[0],primary[1],primary[2]}},{{17,35,201}})&&primary[3]==255,
        "primary pbuffer initial contents");
      const egl_lifetime::Int attrs[]={egl_lifetime::NONE};
      secondaryContext=api.CreateContext(context.display,context.config,context.context,attrs);
      secondarySurface=context.windowSurface(w);
      ok=check(secondaryContext&&secondarySurface,"create shared secondary window context/surface")&&ok;
      ok=check(context.unchanged(),"creating secondary preserves primary current binding")&&ok;
      if(ok)ok=check(api.MakeCurrent(context.display,secondarySurface,secondarySurface,secondaryContext),
        "bind secondary shared window context for publication");
    }
    for(int frame=0;frame<2&&ok;++frame) {
      const std::array<uint8_t,3> color={{uint8_t(40+index*7),uint8_t(190-frame*80),uint8_t(50+frame*80)}};
      context.paint(color);
      if(window) {
        ok=check(api.SwapBuffers(context.display,secondarySurface?secondarySurface:context.surface),"swap exactly one native frame");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        std::array<uint8_t,3> actual{};
        do {
          actual=physical(d,w);
          if(matches(actual,color))break;
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }while(std::chrono::steady_clock::now()<deadline);
        ok=check(matches(actual,color),"native window physical RGB")&&ok;
      }else {
        const auto rgba=context.pixel();
        ok=check(matches({{rgba[0],rgba[1],rgba[2]}},color)&&rgba[3]==255,
          "native pbuffer current-frame RGBA");
      }
      std::cout<<"native_egl_lifetime cycle="<<index<<" kind="<<(window?"window":"pbuffer")
        <<" frame="<<frame<<" valid="<<ok<<'\n';
    }
    if(sharedWindowContext&&window) {
      bool restored=false;
      if(context.context&&context.surface) {
        restored=api.MakeCurrent(context.display,context.surface,context.surface,context.context);
        ok=check(restored&&context.unchanged(),"restore primary before destroying secondary")&&ok;
      }
      if(secondaryContext)ok=check(api.DestroyContext(context.display,secondaryContext),
        "destroy shared secondary context")&&ok;
      if(secondarySurface)ok=check(api.DestroySurface(context.display,secondarySurface),
        "destroy secondary window surface")&&ok;
      if(restored) {
        const auto primary=context.pixel();
        ok=check(matches({{primary[0],primary[1],primary[2]}},{{17,35,201}})&&primary[3]==255,
          "restored primary retains original pbuffer contents after secondary destruction")&&ok;
      }
    }
    ok=check(context.close(),"unbind/destroy primary context/destroy surface/terminate")&&ok;
    // This thread owns every EGL binding created here. Match the connector's
    // shutdown sequence before closing the caller-owned X11 connection.
    ok=check(api.ReleaseThread(),"release EGL TLS on context owner thread")&&ok;
    if(renderer!=d)XCloseDisplay(renderer);
    if(w){XDestroyWindow(d,w);XSync(d,False);}
    if(colormap)XFreeColormap(d,colormap);
  }
  if(!borrowed)XCloseDisplay(d);
  return ok;
}
}
int main(int argc,char**argv) {
  bool sameDisplay=false,worker=false,isolatedRuntimeDisplay=false,platformX11=false;
  bool sharedWindowContext=false;
  for(int i=1;i<argc;++i) {
    const std::string arg=argv[i];
    if(arg=="--same-display")sameDisplay=true;
    else if(arg=="--worker")worker=true;
    else if(arg=="--isolated-runtime-display")isolatedRuntimeDisplay=true;
    else if(arg=="--platform-x11")platformX11=true;
    else if(arg=="--shared-window-context")sharedWindowContext=true;
    else return 2;
  }
  if(!check(XInitThreads()!=0,"XInitThreads"))return 1;
  egl_lifetime::Api api;
  if(!check(api.ready(),"dynamic EGL entry points"))return 1;
  if(platformX11) {
    const bool available=api.platformX11Ready();
    const bool released=api.ReleaseThread();
    if(!check(available,
      "--platform-x11 requires EGL_KHR_platform_x11 client extension and EGL 1.5 platform entry points")||
       !check(released,"release TLS on platform-extension query thread"))return 1;
  }
  Display *shared=sameDisplay?XOpenDisplay(nullptr):nullptr;
  if(sameDisplay&&!check(shared!=nullptr,"shared XOpenDisplay"))return 1;
  bool ok=true;
  for(int i=0;i<12&&ok;++i) {
    if(worker){std::thread thread([&]{ok=cycle(api,i,shared,isolatedRuntimeDisplay,platformX11,sharedWindowContext);});thread.join();}
    else ok=cycle(api,i,shared,isolatedRuntimeDisplay,platformX11,sharedWindowContext);
  }
  if(shared)XCloseDisplay(shared);
  return ok?0:1;
}
