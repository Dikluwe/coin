#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
using Color=std::array<double,3>;
constexpr Color base={{.12,.20,.30}};
constexpr Color front={{.90,.15,.10}};
constexpr Color rear={{.10,.20,.90}};
Color over(const Color & a,double alpha,const Color & b) {
  return {{a[0]*alpha+b[0]*(1-alpha),
    a[1]*alpha+b[1]*(1-alpha),a[2]*alpha+b[2]*(1-alpha)}};
}
bool check(bool good,const char * why) {
  if(!good) std::fprintf(stderr,"peeling_window: %s\n",why);
  return good;
}
unsigned char channel(unsigned long value,unsigned long mask) {
  if(!mask) return 0;
  while(!(mask&1)) {value>>=1;mask>>=1;}
  return static_cast<unsigned char>(((value&mask)*255+mask/2)/mask);
}
std::vector<unsigned char> capture(Display * display,Window window,int size) {
  XImage * image=XGetImage(display,window,0,0,size,size,AllPlanes,ZPixmap);
  if(!image) return {};
  std::vector<unsigned char> out(size_t(size)*size*3);
  for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
    const unsigned long value=XGetPixel(image,x,y);
    const size_t p=(size_t(y)*size+x)*3;
    out[p]=channel(value,image->red_mask);
    out[p+1]=channel(value,image->green_mask);
    out[p+2]=channel(value,image->blue_mask);
  }
  XDestroyImage(image);
  return out;
}
struct Scene {
  SoSeparator * root=new SoSeparator;
  SoSwitch * layers=new SoSwitch;
  SoDepthBuffer * frontDepth=new SoDepthBuffer;
  SoDepthBuffer * rearDepth=new SoDepthBuffer;
  Scene() {
    root->ref();
    auto * camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,10);camera->height=2;
    camera->nearDistance=1;camera->farDistance=30;root->addChild(camera);
    auto * light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;
    root->addChild(light);
    add(root,0,base,0,new SoDepthBuffer);
    root->addChild(layers);
    layers->whichChild=SO_SWITCH_NONE;
    // Reverse submission order makes draw order observably different from
    // depth order. Each transparent surface has its own captured state.
    rearDepth->function=SoDepthBuffer::GREATER;
    rearDepth->write=TRUE;
    add(layers,-2,rear,.5,rearDepth);
    frontDepth->function=SoDepthBuffer::LESS;
    frontDepth->write=FALSE;
    add(layers,2,front,.5,frontDepth);
  }
  ~Scene() {root->unref();}
  static void add(SoGroup * parent,float z,const Color & color,
                  float transparency,SoDepthBuffer * depth) {
    auto * branch=new SoSeparator;parent->addChild(branch);
    branch->addChild(depth);
    auto * material=new SoMaterial;
    material->diffuseColor.setValue(float(color[0]),float(color[1]),float(color[2]));
    material->transparency=transparency;branch->addChild(material);
    auto * points=new SoCoordinate3;
    const SbVec3f p[]={SbVec3f(-.9f,-.9f,z),SbVec3f(.9f,-.9f,z),
      SbVec3f(0,.9f,z)};
    points->point.setValues(0,3,p);branch->addChild(points);
    auto * face=new SoFaceSet;face->numVertices.set1Value(0,3);
    branch->addChild(face);
  }
};
bool visible(const std::vector<unsigned char> & pixels,int size,
             const Color & center,unsigned stamp,const char * name) {
  if(!check(pixels.size()==size_t(size)*size*3,"XGetImage size")) return false;
  const size_t p=(size_t(size/2)*size+size/2)*3;
  const int corner[]={int(stamp),33,49};
  bool good=true;
  for(int c=0;c<3;++c) {
    const int expected=int(std::lround(center[c]*255));
    const int actual=pixels[p+c];
    if(std::abs(expected-actual)>6)
      std::fprintf(stderr,"peeling_window %s size=%d channel=%d center=%d expected=%d\n",
        name,size,c,actual,expected);
    good &= std::abs(expected-actual)<=6;
    good &= std::abs(int(pixels[c])-corner[c])<=1;
  }
  std::printf("peeling_window_frame %s size=%d stamp=%u center=%u,%u,%u corner=%u,%u,%u result=%d\n",
    name,size,stamp,pixels[p],pixels[p+1],pixels[p+2],
    pixels[0],pixels[1],pixels[2],int(good));
  return check(good,"physical X11 image must match numeric oracle and frame stamp");
}
} // namespace
int main(int argc,char ** argv) {
  SoDB::init();CoinRenderAction::initClass();
  bool canonicalRaster=false;
  for(int i=1;i<argc;++i) {
    if(std::string(argv[i])=="--canonical-raster") canonicalRaster=true;
    else return 1;
  }
  std::string diagnostic;
  CoinRenderOptions options=CoinRenderDiagnosticShell::renderOptions(diagnostic);
  if(!diagnostic.empty()) {
    std::fprintf(stderr,"%s\n",diagnostic.c_str());return 1;
  }
  if(canonicalRaster)
    options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
  CoinRenderCapabilities caps{};
  if(coin_render_query_capabilities_for_renderer(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,
      options.renderer,&caps,sizeof(caps))!=0 || !caps.gpu_available) return 77;
  std::printf("peeling_window_runtime backend=%u renderer=%u vendor=0x%x device=0x%x adapter=%s\n",
    unsigned(caps.backend),unsigned(caps.renderer),unsigned(caps.vendor_id),
    unsigned(caps.device_id),caps.adapter_name);
  Display * display=XOpenDisplay(nullptr);
  if(!display) return 77;
  Window window=XCreateSimpleWindow(display,DefaultRootWindow(display),
    32,32,64,64,0,0,0);
  XSetWindowAttributes attributes{};attributes.override_redirect=True;
  XChangeWindowAttributes(display,window,CWOverrideRedirect,&attributes);
  XMapRaised(display,window);XSync(display,False);
  CoinRenderNativeSurfaceDescriptor surface{};
  surface.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  surface.structSize=sizeof(surface);
  surface.type=COIN_RENDER_SURFACE_XLIB;
  surface.native.xlib.display=display;surface.native.xlib.window=window;
  Scene scene;
  SoRenderManager manager;
  manager.setSceneGraph(scene.root);
  manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::SORTED_LAYERS_BLEND);
  manager.getGLRenderAction()->setSortedLayersNumPasses(2);
  std::unique_ptr<CoinRenderManagerAdapter> presented(
    new CoinRenderManagerAdapter(manager,surface,SbVec2i32(64,64),options));
  auto * target=presented->getSceneManager()->getRenderTarget();
  std::printf("peeling_window_effective_raster requested=%d actual=%d\n",
    int(options.rasterizationPolicy),
    target ? int(target->getOptions().rasterizationPolicy) : -1);
  bool ok=check(target!=nullptr,"window target");
  uint64_t lastSerial=target ? target->getLastSubmissionSerial() : 0;
  std::vector<unsigned char> lastImage;
  int currentSize=64;
  auto frame=[&](int size,unsigned stamp,bool peel,const char * name) {
    scene.layers->whichChild=peel ? SO_SWITCH_ALL : SO_SWITCH_NONE;
    if(size!=currentSize) {
      XResizeWindow(display,window,size,size);XSync(display,False);
      XWindowAttributes actual{};
      const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      do {
        XGetWindowAttributes(display,window,&actual);
        if(actual.width==size && actual.height==size) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      } while(std::chrono::steady_clock::now()<end);
      if(!check(actual.width==size && actual.height==size,"window resize")) return false;
      if(!check(presented->resize(SbVec2i32(size,size)),"adapter resize")) return false;
      currentSize=size;
    }
    manager.setViewportRegion(SbViewportRegion(size,size));
    manager.setBackgroundColor(SbColor4f(float(stamp)/255.f,33.f/255.f,49.f/255.f,1));
    const auto status=presented->render();
    if(!check(status==CoinRenderAction::SUCCESS,"window frame submission")) {
      std::fprintf(stderr,"peeling_window render status=%d diagnostic=%s\n",
        int(status),presented->getLastError().getString());return false;
    }
    const uint64_t serial=target->getLastSubmissionSerial();
    if(!check(serial>lastSerial,"successful window frame must advance serial")) return false;
    lastSerial=serial;
    const Color expected=peel ? over(front,.5,over(rear,.5,base)) : base;
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    std::vector<unsigned char> pixels;
    do {
      pixels=capture(display,window,size);
      if(pixels.size()==size_t(size)*size*3 &&
         std::abs(int(pixels[0])-int(stamp))<=1) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while(std::chrono::steady_clock::now()<end);
    const bool good=visible(pixels,size,expected,stamp,name);
    if(good) lastImage=pixels;
    return good;
  };
  if(ok) ok &= frame(64,17,false,"opaque-initial");
  if(ok) ok &= frame(64,34,true,"peeling-front-rear");
  if(ok) ok &= frame(96,51,true,"peeling-resize96");
  if(ok) ok &= frame(65,68,true,"peeling-resize65");
  if(ok) ok &= frame(64,85,true,"peeling-return64");
  if(ok) ok &= frame(64,102,false,"opaque-again");
  if(ok) ok &= frame(64,119,true,"peeling-recovered");
  if(ok) {
    scene.frontDepth->range=SbVec2f(std::numeric_limits<float>::quiet_NaN(),1);
    manager.setBackgroundColor(SbColor4f(233.f/255.f,33.f/255.f,49.f/255.f,1));
    const auto status=presented->render();
    const uint64_t serial=target->getLastSubmissionSerial();
    const auto physical=capture(display,window,64);
    const bool atomic=status!=CoinRenderAction::SUCCESS &&
      serial==lastSerial && physical==lastImage;
    std::printf("peeling_window_invalid status=%d serial_stable=%d physical_stable=%d diagnostic=%s\n",
      int(status),int(serial==lastSerial),int(physical==lastImage),
      presented->getLastError().getString());
    ok &= check(atomic,"invalid depth state must preserve visible window and serial");
    scene.frontDepth->range=SbVec2f(0,1);
    if(ok) ok &= frame(64,136,true,"peeling-after-invalid");
  }
  presented.reset();
  XDestroyWindow(display,window);XCloseDisplay(display);
  return ok ? 0 : 1;
}
