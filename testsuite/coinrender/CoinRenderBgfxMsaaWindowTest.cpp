#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <vector>
namespace {
struct Pixel {uint8_t r,g,b;};
uint8_t channel(unsigned long value,unsigned long mask) {
  if(!mask) return 0;
  while(!(mask&1)){mask>>=1;value>>=1;}
  return uint8_t(((value&mask)*255+mask/2)/mask);
}
std::vector<Pixel> image(Display * display,Window window,int size) {
  XSync(display,False);
  XImage * raw=XGetImage(display,window,0,0,size,size,AllPlanes,ZPixmap);
  if(!raw) return {};
  std::vector<Pixel> result(size_t(size)*size);
  for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
    const auto value=XGetPixel(raw,x,y);
    result[size_t(y)*size+x]={channel(value,raw->red_mask),
      channel(value,raw->green_mask),channel(value,raw->blue_mask)};
  }
  XDestroyImage(raw);return result;
}
bool check(bool value,const char * label) {
  if(!value) std::cerr << "bgfx_msaa_window FAIL " << label << '\n';
  return value;
}
bool observe(Display * display,Window window,int size,uint8_t stamp,
             uint32_t samples,bool green=false) {
  std::vector<Pixel> pixels;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  do {
    pixels=image(display,window,size);
    if(pixels.size()==size_t(size)*size && pixels[0].b==stamp) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  } while(std::chrono::steady_clock::now()<deadline);
  if(!check(pixels.size()==size_t(size)*size && pixels[0].b==stamp,
            "physical X11 frame stamp")) return false;
  size_t partial=0,quarter=0,threeQuarter=0,red=0;
  for(const auto & pixel:pixels) {
    const int color=green ? pixel.g : pixel.r;
    if(color>0) ++red;
    if(color==0 || color==255) continue;
    const int count=int(std::lround(double(color)*samples/255.0));
    const int quantized=int(std::lround(255.0*count/samples));
    if(count<=0 || count>=int(samples) || std::abs(color-quantized)>1)
      return check(false,"physical sample quantization");
    ++partial;quarter+=(count==1);threeQuarter+=(samples==4 && count==3);
  }
  std::cout << "bgfx_msaa_window size=" << size << " samples=" << samples
            << " red=" << red << " partial=" << partial
            << " quarter=" << quarter << " threequarter=" << threeQuarter << '\n';
  return check(red>0 && (samples==1?partial==0:
    samples==2?partial>0:quarter+threeQuarter>0),"physical MSAA geometry");
}
struct Scene {
  SoSeparator * root=new SoSeparator;
  SoMaterial * material=new SoMaterial;
  Scene() {
    root->ref();auto * camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,2);camera->height=2;
    camera->nearDistance=1;camera->farDistance=3;root->addChild(camera);
    auto * light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;root->addChild(light);
    material->diffuseColor.setValue(1,0,0);root->addChild(material);
    auto * coords=new SoCoordinate3;
    const SbVec3f p[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),SbVec3f(-1,.83f,0)};
    coords->point.setValues(0,3,p);root->addChild(coords);
    auto * face=new SoIndexedFaceSet;const int32_t ix[]={0,1,2,-1};
    face->coordIndex.setValues(0,4,ix);root->addChild(face);
  }
  ~Scene(){root->unref();}
};
bool run() {
  CoinRenderCapabilities caps{};
  if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,&caps,sizeof(caps))!=0) return false;
  std::cout << "bgfx_msaa_window backend=" << caps.backend << " renderer=" << caps.renderer
            << " vendor=" << caps.vendor_id << " adapter=" << caps.adapter_name << '\n';
  if(!check(caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION &&
            caps.gpu_available && caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN,
            "real BGFX GPU window")) return false;
  Display * display=XOpenDisplay(nullptr);if(!display) return false;
  Window window=XCreateSimpleWindow(display,DefaultRootWindow(display),8,8,32,32,0,0,0);
  XSetWindowAttributes attrs{};attrs.override_redirect=True;
  XChangeWindowAttributes(display,window,CWOverrideRedirect,&attrs);
  XMapRaised(display,window);XSync(display,False);
  CoinRenderNativeSurfaceDescriptor native{};native.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  native.structSize=sizeof(native);native.type=COIN_RENDER_SURFACE_XLIB;
  native.native.xlib.display=display;native.native.xlib.window=window;
  Scene scene;bool ok=true;
  for(uint32_t samples:{1u,2u,4u}) {
    CoinRenderOptions options;options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
    options.sampleCount=samples;
    std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createWindow(native,SbVec2i32(32,32),options));
    if(!check(bool(target) && target->getOptions().renderer==options.renderer &&
              target->getOptions().sampleCount==samples,"window options")){ok=false;break;}
    CoinRenderAction action(SbViewportRegion(32,32));action.setRenderTarget(target.get());
    const uint8_t stamp=uint8_t(20+samples*20);
    action.setBackgroundColor(SbColor4f(0,0,stamp/255.f,1));
    action.apply(scene.root);
    ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
      target->getLastSubmissionSerial()>0,"window submit");
    if(ok) ok &= observe(display,window,32,stamp,samples);
    if(samples>=2 && ok) {
      XResizeWindow(display,window,48,48);XSync(display,False);
      ok &= check(target->resize(SbVec2i32(48,48)),"window resize");
      action.setViewportRegion(SbViewportRegion(48,48));
      action.setBackgroundColor(SbColor4f(0,0,140/255.f,1));
      action.apply(scene.root);
      ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS,"resized submit");
      if(ok) ok &= observe(display,window,48,140,samples);
      const uint64_t stable=target->getLastSubmissionSerial();
      scene.material->diffuseColor.setValue(0,1,0);
      action.setBackgroundColor(SbColor4f(0,0,180/255.f,1));
      action.apply(scene.root);
      ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
        target->getLastSubmissionSerial()>stable,"window mutation serial");
      scene.material->diffuseColor.setValue(1,0,0);
      // The stamp proves this is the newly presented frame; geometry is still resolved.
      if(ok) ok &= observe(display,window,48,180,samples,true);
      action.setBackgroundColor(SbColor4f(0,0,200/255.f,1));
      action.apply(scene.root);
      if(ok) ok &= observe(display,window,48,200,samples);
      XResizeWindow(display,window,32,32);XSync(display,False);
    }
    action.setRenderTarget(nullptr);
    if(!ok) break;
  }
  XDestroyWindow(display,window);XCloseDisplay(display);return ok;
}
}
int main(int argc,char **argv) {
  if(argc!=2 || std::string(argv[1])!="--window") return 2;
  SoDB::init();CoinRenderAction::initClass();return run()?0:1;
}
