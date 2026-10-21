#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture3.h>
#include <Inventor/nodes/SoTextureCoordinate3.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
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
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "CoinRenderVolumeSrgbOracle.h"
namespace {
bool srgb=false;
std::array<unsigned char,3> displayColor(std::array<unsigned char,3> rgb){if(srgb)for(auto&c:rgb)c=uint8_t(std::lround(255*volume_srgb_oracle::decode(c)));return rgb;}

bool check(bool good,const char * label) {
  if(!good) std::cerr << "texture2d_window FAIL " << label << '\n';
  return good;
}
unsigned char channel(unsigned long value,unsigned long mask) {
  if(!mask) return 0;
  while(!(mask&1)) {mask>>=1;value>>=1;}
  return static_cast<unsigned char>(((value&mask)*255+mask/2)/mask);
}
std::array<unsigned char,3> pixel(Display * d,Window w,int size) {
  XSync(d,False);
  XImage * image=XGetImage(d,w,size/2,size/2,1,1,AllPlanes,ZPixmap);
  if(!image) return {{0,0,0}};
  const unsigned long value=XGetPixel(image,0,0);
  const std::array<unsigned char,3> rgb={{channel(value,image->red_mask),
    channel(value,image->green_mask),channel(value,image->blue_mask)}};
  XDestroyImage(image);return rgb;
}
bool observe(Display * d,Window w,int size,
             const std::array<unsigned char,3> & expected,const char * phase) {
  const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  std::array<unsigned char,3> seen={{0,0,0}};
  do {
    seen=pixel(d,w,size);
    bool good=true;
    for(int c=0;c<3;++c) good &= std::abs(int(seen[c])-int(expected[c]))<=3;
    if(good) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while(std::chrono::steady_clock::now()<until);
  std::cerr << phase << " actual=" << int(seen[0]) << ',' << int(seen[1]) << ','
            << int(seen[2]) << " expected=" << int(expected[0]) << ','
            << int(expected[1]) << ',' << int(expected[2]) << '\n';
  XImage * full=XGetImage(d,w,0,0,size,size,AllPlanes,ZPixmap);
  if(full) {
    size_t nonblack=0;
    for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
      const unsigned long v=XGetPixel(full,x,y);
      nonblack+=(channel(v,full->red_mask)!=0 ||
        channel(v,full->green_mask)!=0 || channel(v,full->blue_mask)!=0);
    }
    std::cerr << phase << " full_x11_nonblack=" << nonblack << '\n';
    XDestroyImage(full);
  }
  return false;
}
bool coverage(const std::vector<uint8_t>& rgba,const std::array<unsigned char,3>& expected,unsigned samples,const char* phase) {
  size_t bad=0,alphaBad=0,partial=0,oddQuarter=0,inside=0,outside=0;
  for(size_t at=0;at+3<rgba.size();at+=4) {
    int matched=-1;
    for(unsigned k=0;k<=samples;++k) {
      bool close=true;for(unsigned c=0;c<3;++c)close &=std::abs(int(rgba[at+c])*int(samples)-int(expected[c])*int(k))<=3*int(samples);
      if(close){matched=int(k);break;}
    }
    bad+=matched<0;alphaBad+=rgba[at+3]!=255;
    outside+=matched==0;inside+=matched==int(samples);
    partial+=matched>0&&matched<int(samples);
    oddQuarter+=samples==4&&(matched==1||matched==3);
  }
  const bool pass=bad==0&&alphaBad==0&&inside>0&&outside>0&&
    (samples==1||partial>0)&&(samples!=4||oddQuarter>0);
  std::cout<<"npot_window_coverage "<<phase<<" samples="<<samples<<" bad_rgb="<<bad<<" bad_alpha="<<alphaBad<<" interior="<<inside<<" exterior="<<outside<<" partial="<<partial<<" odd_quarters="<<oddQuarter<<" pass="<<pass<<'\n';
  return check(pass,"resolved NPOT edge RGB must be k/N coverage with opaque alpha, partial 2x and odd-quarter 4x");
}
struct Scene {
  SoSeparator * root=new SoSeparator;
  SoOrthographicCamera * camera=new SoOrthographicCamera;
  SoTexture3 * texture=new SoTexture3;
  SoAlphaTest * alpha=new SoAlphaTest;
  Scene() {
    root->ref();camera->position.setValue(0,0,3);camera->height=2;
    camera->nearDistance=.1f;camera->farDistance=10;root->addChild(camera);
    auto * model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;root->addChild(model);
    auto * complexity=new SoComplexity;complexity->textureQuality=.1f;
    root->addChild(complexity);
    texture->model=SoTexture3::MODULATE;root->addChild(texture);
    auto * material=new SoMaterial;material->diffuseColor.setValue(1,1,1);
    root->addChild(material);
    alpha->function=SoAlphaTest::ALWAYS;alpha->value=.5f;root->addChild(alpha);
    auto * coords=new SoTextureCoordinate3;
    const SbVec3f uv[]={SbVec3f(.5f/3,.5f/5,6.5f/7),SbVec3f(.5f/3,.5f/5,6.5f/7),
      SbVec3f(.5f/3,.5f/5,6.5f/7),SbVec3f(.5f/3,.5f/5,6.5f/7)};
    coords->point.setValues(0,4,uv);root->addChild(coords);
    auto * points=new SoCoordinate3;
    // Convex, sloped outer edges expose every supported sample fraction.
    const SbVec3f p[]={SbVec3f(-.82f,-.75f,0),SbVec3f(.77f,-.61f,0),
      SbVec3f(.61f,.83f,0),SbVec3f(-.69f,.72f,0)};
    points->point.setValues(0,4,p);root->addChild(points);
    auto * face=new SoFaceSet;face->numVertices.set1Value(0,4);root->addChild(face);
    set({{80,160,220}});
  }
  ~Scene(){root->unref();}
  void set(const std::array<unsigned char,3> & rgb) {
    std::vector<unsigned char> p(3*5*7*4);
    for(int z=0;z<7;++z)for(int y=0;y<5;++y)for(int x=0;x<3;++x){size_t at=size_t((z*5+y)*3+x)*4;p[at]=17+x*53;p[at+1]=19+y*41;p[at+2]=23+z*31;p[at+3]=255;}
    size_t at=size_t(6*5*3)*4;for(int c=0;c<3;++c)p[at+c]=rgb[c];
    texture->images.setValue(SbVec3s(3,5,7),4,p.data());
  }
};
}
int main(int argc,char ** argv) {
  bool canonical=false,portable=false,readback=false;unsigned samples=1;
  for(int i=1;i<argc;++i) {
    const std::string arg(argv[i]);
    if(arg=="--srgb")srgb=true;
    else if(arg=="--msaa2") samples=2;
    else if(arg=="--msaa4") samples=4;
    else if(arg=="--canonical-raster") canonical=true;
    else if(arg=="--portable-sampling") portable=true;
    else if(arg=="--readback") readback=true;
    else {std::cerr << "Usage: CoinRenderVolumeNpotWindowTest [--canonical-raster] [--portable-sampling] [--readback]\n";return 2;}
  }
  if(samples>1)readback=true;
  SoDB::init();CoinRenderAction::initClass();
  CoinRenderCapabilities caps{};
  if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,
      &caps,sizeof(caps))!=0 || !caps.gpu_available) return 77;
  if(caps.renderer!=COIN_RENDER_RENDERER_VULKAN &&
     caps.renderer!=COIN_RENDER_RENDERER_OPENGL) return 77;
  std::cout << "texture2d_window backend=" << caps.backend << " renderer="
            << caps.renderer << " vendor=" << caps.vendor_id << " adapter="
            << caps.adapter_name << " canonical=" << canonical
            << " portable=" << portable << " samples=" << samples << '\n';
  Display * display=XOpenDisplay(nullptr);if(!display) return 77;
  int size=64;
  Window window=XCreateSimpleWindow(display,DefaultRootWindow(display),32,32,size,size,0,0,0);
  XSetWindowAttributes attributes{};attributes.override_redirect=True;
  XChangeWindowAttributes(display,window,CWOverrideRedirect,&attributes);
  XMapRaised(display,window);XSync(display,False);
  CoinRenderNativeSurfaceDescriptor native{};
  native.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  native.structSize=sizeof(native);native.type=COIN_RENDER_SURFACE_XLIB;
  native.native.xlib.display=display;native.native.xlib.window=window;
  CoinRenderOptions options{};options.sampleCount=samples;if(srgb)options.storedTextureColorSpace=COIN_RENDER_TEXTURE_SRGB;
  options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  if(canonical) options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
  if(portable) options.textureSamplingPolicy=COIN_RENDER_SAMPLING_PORTABLE;
  std::unique_ptr<CoinRenderTarget> target(
    CoinRenderTarget::createWindow(native,SbVec2i32(size,size),options));
  if(!check(bool(target)&&target->getStatus()==CoinRenderTarget::TARGET_READY &&
    target->getOptions().renderer==options.renderer&&target->getOptions().sampleCount==samples,"window target, requested renderer and sample count")) return 1;
  CoinRenderAction action(SbViewportRegion(size,size));action.setRenderTarget(target.get());
  action.setBackgroundColor(SbColor4f(0,0,0,1));
  Scene scene;
  uint64_t serial=0;
  std::vector<uint8_t> lastReadback;
  const auto frame=[&](const std::array<unsigned char,3> & authored,const char * phase) {
    const auto expected=displayColor(authored);
    const bool requested=!readback || bool(target->requestWindowReadbackRGBA());
    const auto start=std::chrono::steady_clock::now();
    action.apply(scene.root); // Exactly one submission per phase.
    const auto applyMs=std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now()-start).count();
    std::cout << phase << " apply_ms=" << applyMs << std::endl;
    bool bufferGood=true;
    if(readback) {
      std::vector<uint8_t> rgba;target->readbackRGBA(rgba);
      const size_t at=(size_t(size/2)*size+size/2)*4;
      bufferGood=requested && rgba.size()==size_t(size*size*4);
      if(bufferGood) for(int c=0;c<3;++c)
        bufferGood &= std::abs(int(rgba[at+c])-int(expected[c]))<=3;
      if(bufferGood) bufferGood=coverage(rgba,expected,samples,phase);
      if(bufferGood) lastReadback=std::move(rgba);
    }
    const uint64_t next=target->getLastSubmissionSerial();
    const bool physical=observe(display,window,size,expected,phase);
    const bool good=requested && bufferGood && physical &&
      action.getLastStatus()==CoinRenderAction::SUCCESS && next>serial;
    if(!good) std::cerr << phase << " status=" << int(action.getLastStatus())
      << " error=" << action.getLastError().getString() << " serial=" << next << '\n';
    if(good) serial=next;
    return check(good,"single-apply physical publication and serial");
  };
  bool ok=frame({{80,160,220}},"initial64");
  const int sizes[]={96,65,64};
  const std::array<unsigned char,3> colors[]={{{213,52,87}},{{41,187,129}},{{116,93,224}}};
  for(int i=0;i<3;++i) {
    XResizeWindow(display,window,sizes[i],sizes[i]);XSync(display,False);
    XWindowAttributes actual{};
    XGetWindowAttributes(display,window,&actual);
    if(!check(actual.width==sizes[i]&&actual.height==sizes[i]&&
        target->resize(SbVec2i32(sizes[i],sizes[i])),"window resize")) {ok=false;break;}
    size=sizes[i];action.setViewportRegion(SbViewportRegion(size,size));
    scene.set(colors[i]);ok &= frame(colors[i],"resized-mutated");
  }
  const auto current=colors[2];
  std::vector<unsigned char> oversized(257*4,255);
  scene.texture->images.setValue(SbVec3s(257,1,1),4,oversized.data());
  target->requestWindowReadbackRGBA();action.apply(scene.root);
  std::vector<uint8_t> rejected;target->readbackRGBA(rejected);
  ok &= check(action.getLastStatus()!=CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()==serial&&(!readback||rejected==lastReadback)&&observe(display,window,size,displayColor(current),"invalid-volume"),"NPOT invalid extent preserves physical/readback/serial");
  scene.set(current);ok &=frame(current,"volume-recovered");
  scene.alpha->value=std::numeric_limits<float>::quiet_NaN();
  action.apply(scene.root);
  bool retained=true;
  if(readback) {
    std::vector<uint8_t> after;target->readbackRGBA(after);
    retained=!lastReadback.empty() && after==lastReadback;
  }
  ok &= check(action.getLastStatus()!=CoinRenderAction::SUCCESS &&
    target->getLastSubmissionSerial()==serial && retained &&
    observe(display,window,size,displayColor(current),"invalid-preserved"),
    "invalid input preserves physical frame, readback and serial");
  scene.alpha->value=.5f;ok &= frame(current,"recovered");
  action.setRenderTarget(nullptr);target.reset();
  XDestroyWindow(display,window);XSync(display,False);
  window=XCreateSimpleWindow(display,DefaultRootWindow(display),32,32,size,size,0,0,0);
  XChangeWindowAttributes(display,window,CWOverrideRedirect,&attributes);
  XMapRaised(display,window);XSync(display,False);
  native.native.xlib.window=window;
  target.reset(CoinRenderTarget::createWindow(native,SbVec2i32(size,size),options));
  ok &= check(bool(target)&&target->getStatus()==CoinRenderTarget::TARGET_READY &&
    target->getOptions().renderer==options.renderer&&target->getOptions().sampleCount==samples,"recreated target renderer and sample count");
  if(target) {serial=0;lastReadback.clear();action.setRenderTarget(target.get());
    ok &= frame(current,"recreated");action.setRenderTarget(nullptr);target.reset();}
  XDestroyWindow(display,window);XCloseDisplay(display);
  return ok?0:1;
}
