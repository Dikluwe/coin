#ifndef COIN_RENDER_EGL_LIFETIME_FIXTURE_H
#define COIN_RENDER_EGL_LIFETIME_FIXTURE_H
// Test-only dynamic EGL entry points: the control must not depend on Coin/BGFX.
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
namespace egl_lifetime {
using Handle=void*;
using Int=int;
using Boolean=unsigned int;
using Attrib=intptr_t;
constexpr Int NONE=0x3038, SURFACE_TYPE=0x3033, PBUFFER_BIT=1, WINDOW_BIT=4;
constexpr Int RENDERABLE_TYPE=0x3040, OPENGL_BIT=8, RED_SIZE=0x3024;
constexpr Int GREEN_SIZE=0x3023, BLUE_SIZE=0x3022, WIDTH=0x3057, HEIGHT=0x3056;
constexpr Int NATIVE_VISUAL_ID=0x302e, DRAW=0x3059, READ=0x305a;
constexpr unsigned OPENGL_API=0x30a2;
constexpr unsigned PLATFORM_X11_KHR=0x31d5;
constexpr Int EXTENSIONS=0x3055;
struct Api {
  void *library=dlopen("libEGL.so.1",RTLD_NOW|RTLD_LOCAL);
#define EGL_ENTRY(result,name,args) using name##Type=result(*)args; name##Type name=library?reinterpret_cast<name##Type>(dlsym(library,"egl" #name)):nullptr
  EGL_ENTRY(Handle,GetDisplay,(void*));
  EGL_ENTRY(const char*,QueryString,(Handle,Int));
  EGL_ENTRY(Handle,GetPlatformDisplay,(unsigned,void*,const Attrib*));
  EGL_ENTRY(Handle,CreatePlatformWindowSurface,(Handle,Handle,void*,const Attrib*));
  EGL_ENTRY(Boolean,Initialize,(Handle,Int*,Int*));
  EGL_ENTRY(Boolean,ChooseConfig,(Handle,const Int*,Handle*,Int,Int*));
  EGL_ENTRY(Boolean,GetConfigAttrib,(Handle,Handle,Int,Int*));
  EGL_ENTRY(Boolean,BindAPI,(unsigned));
  EGL_ENTRY(unsigned,QueryAPI,());
  EGL_ENTRY(Handle,CreateContext,(Handle,Handle,Handle,const Int*));
  EGL_ENTRY(Handle,CreatePbufferSurface,(Handle,Handle,const Int*));
  EGL_ENTRY(Handle,CreateWindowSurface,(Handle,Handle,unsigned long,const Int*));
  EGL_ENTRY(Boolean,MakeCurrent,(Handle,Handle,Handle,Handle));
  EGL_ENTRY(Handle,GetCurrentContext,());
  EGL_ENTRY(Handle,GetCurrentDisplay,());
  EGL_ENTRY(Handle,GetCurrentSurface,(Int));
  EGL_ENTRY(Boolean,DestroyContext,(Handle,Handle));
  EGL_ENTRY(Boolean,DestroySurface,(Handle,Handle));
  EGL_ENTRY(Boolean,SwapBuffers,(Handle,Handle));
  EGL_ENTRY(Boolean,Terminate,(Handle));
  EGL_ENTRY(Boolean,ReleaseThread,());
  EGL_ENTRY(Int,GetError,());
  EGL_ENTRY(void*,GetProcAddress,(const char*));
#undef EGL_ENTRY
  using ClearColorType=void(*)(float,float,float,float);
  using ClearType=void(*)(unsigned);
  using ReadPixelsType=void(*)(int,int,int,int,unsigned,unsigned,void*);
  ClearColorType clearColor=nullptr;
  ClearType clear=nullptr;
  ReadPixelsType readPixels=nullptr;
  bool ready() const {
    return GetDisplay&&Initialize&&ChooseConfig&&GetConfigAttrib&&BindAPI&&QueryAPI&&
      CreateContext&&CreatePbufferSurface&&CreateWindowSurface&&MakeCurrent&&
      GetCurrentContext&&GetCurrentDisplay&&GetCurrentSurface&&DestroyContext&&
      DestroySurface&&SwapBuffers&&Terminate&&ReleaseThread&&GetError&&GetProcAddress;
  }
  bool platformX11Ready() const {
    if(!QueryString||!GetPlatformDisplay||!CreatePlatformWindowSurface)return false;
    const char *extensions=QueryString(nullptr,EXTENSIONS);
    if(!extensions)return false;
    const std::string tokens=std::string(" ")+extensions+" ";
    return tokens.find(" EGL_KHR_platform_x11 ")!=std::string::npos;
  }
  bool loadGl() {
    clearColor=reinterpret_cast<ClearColorType>(GetProcAddress("glClearColor"));
    clear=reinterpret_cast<ClearType>(GetProcAddress("glClear"));
    readPixels=reinterpret_cast<ReadPixelsType>(GetProcAddress("glReadPixels"));
    return clearColor&&clear&&readPixels;
  }
  ~Api(){if(library)dlclose(library);}
};
struct Context {
  Api &api;
  Display *native;
  Handle display=nullptr, config=nullptr, context=nullptr, surface=nullptr;
  bool initialized=false,platformX11=false;
  explicit Context(Api&a,Display*d):api(a),native(d){}
  Context(const Context&)=delete;
  Context& operator=(const Context&)=delete;
  bool initialize(bool window,bool explicitX11=false,bool pbufferAndWindow=false) {
    platformX11=explicitX11;
    if(platformX11&&!api.platformX11Ready())return false;
    const Attrib platformAttrs[]={NONE};
    display=platformX11?api.GetPlatformDisplay(PLATFORM_X11_KHR,native,platformAttrs):
      api.GetDisplay(native);
    Int major=0,minor=0;
    if(!display||!api.Initialize(display,&major,&minor))return false;
    initialized=true;
    if(platformX11&&(major<1||(major==1&&minor<5))) {
      std::cerr<<"native_egl_lifetime FAIL --platform-x11 requires EGL 1.5; got "
        <<major<<'.'<<minor<<'\n';
      return false;
    }
    const Int attrs[]={SURFACE_TYPE,pbufferAndWindow?(PBUFFER_BIT|WINDOW_BIT):(window?WINDOW_BIT:PBUFFER_BIT),
      RENDERABLE_TYPE,OPENGL_BIT,RED_SIZE,8,GREEN_SIZE,8,BLUE_SIZE,8,NONE};
    Int count=0;
    return api.BindAPI(OPENGL_API)&&api.ChooseConfig(display,attrs,&config,1,&count)&&count==1;
  }
  Handle windowSurface(Window window) {
    const Int attrs[]={NONE};
    const Attrib platformAttrs[]={NONE};
    // KHR_platform_x11 requires a pointer to the Xlib Window value here.
    return platformX11?
      api.CreatePlatformWindowSurface(display,config,&window,platformAttrs):
      api.CreateWindowSurface(display,config,window,attrs);
  }
  bool makeCurrent(Window window=0) {
    const Int attrs[]={NONE};
    const Int size[]={WIDTH,64,HEIGHT,64,NONE};
    context=api.CreateContext(display,config,nullptr,attrs);
    surface=window?windowSurface(window):api.CreatePbufferSurface(display,config,size);
    return context&&surface&&api.MakeCurrent(display,surface,surface,context)&&api.loadGl();
  }
  bool unchanged() const {
    return api.QueryAPI()==OPENGL_API&&api.GetCurrentDisplay()==display&&
      api.GetCurrentContext()==context&&api.GetCurrentSurface(DRAW)==surface&&
      api.GetCurrentSurface(READ)==surface;
  }
  std::array<uint8_t,4> pixel() {
    std::array<uint8_t,4> rgba{};
    api.readPixels(32,32,1,1,0x1908,0x1401,rgba.data()); // GL_RGBA / UNSIGNED_BYTE
    return rgba;
  }
  void paint(const std::array<uint8_t,3>&rgb) {
    api.clearColor(rgb[0]/255.f,rgb[1]/255.f,rgb[2]/255.f,1.f);
    api.clear(0x4000); // GL_COLOR_BUFFER_BIT
  }
  bool close() {
    bool ok=true;
    if(context&&api.GetCurrentContext()==context)
      ok=api.MakeCurrent(display,nullptr,nullptr,nullptr)&&ok;
    if(context){ok=api.DestroyContext(display,context)&&ok;context=nullptr;}
    if(surface){ok=api.DestroySurface(display,surface)&&ok;surface=nullptr;}
    if(initialized){ok=api.Terminate(display)&&ok;initialized=false;}
    return ok;
  }
  ~Context(){close();}
};
}
#endif
