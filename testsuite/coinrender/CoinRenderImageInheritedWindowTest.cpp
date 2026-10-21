#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
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
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
bool check(bool good,const char * label) {
  if(!good) std::cerr << "CoinRenderImageInheritedWindowTest: " << label << '\n';
  return good;
}
unsigned char channel(unsigned long value,unsigned long mask) {
  if(!mask) return 0;
  while(!(mask&1)) {mask>>=1;value>>=1;}
  return static_cast<unsigned char>(((value&mask)*255+mask/2)/mask);
}
std::array<unsigned char,3> pixel(Display * display,Window window,int size) {
  XSync(display,False);
  XImage * image=XGetImage(display,window,size/2,size/2,1,1,AllPlanes,ZPixmap);
  if(!image) return {{0,0,0}};
  const unsigned long value=XGetPixel(image,0,0);
  const std::array<unsigned char,3> out={{channel(value,image->red_mask),
    channel(value,image->green_mask),channel(value,image->blue_mask)}};
  XDestroyImage(image);
  return out;
}
bool observe(Display * display,Window window,int size,
             const std::array<unsigned char,3> & expected,const char * phase) {
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  std::array<unsigned char,3> seen={{0,0,0}};
  do {
    seen=pixel(display,window,size);
    bool good=true;
    for(int channel=0;channel<3;++channel)
      good &= std::abs(int(seen[channel])-int(expected[channel]))<=3;
    if(good) {
      std::cout << phase << " physical=" << int(seen[0]) << ',' << int(seen[1])
                << ',' << int(seen[2]) << " expected=" << int(expected[0]) << ','
                << int(expected[1]) << ',' << int(expected[2]) << '\n';
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while(std::chrono::steady_clock::now()<end);
  std::cerr << phase << " physical=" << int(seen[0]) << ',' << int(seen[1])
            << ',' << int(seen[2]) << " expected=" << int(expected[0]) << ','
            << int(expected[1]) << ',' << int(expected[2]) << '\n';
  // Diagnose a missing center without submitting another frame or changing
  // the five-second presentation gate. A colored image at another position
  // and a fully black surface imply different failures.
  XImage * full=XGetImage(display,window,0,0,size,size,AllPlanes,ZPixmap);
  if(full) {
    size_t nonblack=0,matching=0;
    int colorLeft=size,colorTop=size,colorRight=-1,colorBottom=-1;
    int matchLeft=size,matchTop=size,matchRight=-1,matchBottom=-1;
    for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
      const unsigned long value=XGetPixel(full,x,y);
      const std::array<unsigned char,3> rgb={{channel(value,full->red_mask),
        channel(value,full->green_mask),channel(value,full->blue_mask)}};
      const bool colored=rgb[0]!=0 || rgb[1]!=0 || rgb[2]!=0;
      bool expectedColor=true;
      for(int c=0;c<3;++c)
        expectedColor &= std::abs(int(rgb[c])-int(expected[c]))<=3;
      if(colored) {
        ++nonblack;
        colorLeft=std::min(colorLeft,x);colorTop=std::min(colorTop,y);
        colorRight=std::max(colorRight,x);colorBottom=std::max(colorBottom,y);
      }
      if(expectedColor) {
        ++matching;
        matchLeft=std::min(matchLeft,x);matchTop=std::min(matchTop,y);
        matchRight=std::max(matchRight,x);matchBottom=std::max(matchBottom,y);
      }
    }
    std::cerr << phase << " full_x11_nonblack=" << nonblack
              << " nonblack_bbox=" << colorLeft << ',' << colorTop << ".."
              << colorRight << ',' << colorBottom
              << " expected_rgb_pixels=" << matching
              << " expected_rgb_bbox=" << matchLeft << ',' << matchTop << ".."
              << matchRight << ',' << matchBottom << '\n';
    XDestroyImage(full);
  } else {
    std::cerr << phase << " full_x11_readback=unavailable\n";
  }
  return false;
}
struct Scene {
  SoSeparator * root=new SoSeparator;
  SoOrthographicCamera * camera=new SoOrthographicCamera;
  SoTextureMatrixTransform * matrix=new SoTextureMatrixTransform;
  SoComplexity * quality=new SoComplexity;
  SoTexture2 * lastTexture=nullptr;
  SoImage * image=new SoImage;
  SoMaterial * geometryMaterial=nullptr;
  Scene() {
    root->ref();camera->position.setValue(0,0,10);camera->height=64;
    camera->nearDistance=1;camera->farDistance=30;root->addChild(camera);
    auto * light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;root->addChild(light);
    quality->textureQuality=.1f;root->addChild(quality);
    const unsigned char base[]={249,246,243,255};
    for(int index=0;index<8;++index) {
      auto * unit=new SoTextureUnit;unit->unit=index;root->addChild(unit);
      auto * texture=new SoTexture2;
      texture->image.setValue(SbVec2s(1,1),4,base);
      texture->model=SoTexture2::MODULATE;
      root->addChild(texture);
      lastTexture=texture;
    }
    root->addChild(matrix);
    image->horAlignment=SoImage::CENTER;image->vertAlignment=SoImage::HALF;
    std::vector<unsigned char> white(16*16*3,255);
    image->image.setValue(SbVec2s(16,16),3,white.data());root->addChild(image);
  }
  ~Scene(){root->unref();}
  void q(float value) {SbMatrix m=SbMatrix::identity();m[3][3]=value;matrix->matrix=m;}
  void removeInheritedTextures() {
    quality->textureQuality=0;
    for(int index=root->getNumChildren()-1;index>=0;--index) {
      SoNode * child=root->getChild(index);
      if(child->isOfType(SoTexture2::getClassTypeId()) ||
         child->isOfType(SoTextureUnit::getClassTypeId()))
        root->removeChild(index);
    }
    lastTexture=nullptr;
  }
  void removeImage() {
    root->removeChild(image);
    image=nullptr;
  }
  void useGeometry(bool textured) {
    removeImage();
    if(!textured) removeInheritedTextures();
    auto * branch=new SoSeparator;root->addChild(branch);
    geometryMaterial=new SoMaterial;
    geometryMaterial->diffuseColor.setValue(1,1,1);
    branch->addChild(geometryMaterial);
    auto * points=new SoCoordinate3;
    const SbVec3f vertices[]={SbVec3f(-8,-8,0),SbVec3f(8,-8,0),
      SbVec3f(8,8,0),SbVec3f(-8,8,0)};
    points->point.setValues(0,4,vertices);branch->addChild(points);
    auto * face=new SoFaceSet;face->numVertices.set1Value(0,4);
    branch->addChild(face);
  }
  void geometryColor(const std::array<unsigned char,3> & rgb) {
    geometryMaterial->diffuseColor.setValue(float(rgb[0])/255.f,
      float(rgb[1])/255.f,float(rgb[2])/255.f);
  }
  void payload(const std::array<unsigned char,3> & rgb) {
    std::vector<unsigned char> pixels(16*16*3);
    for(size_t i=0;i<16*16;++i)
      for(int c=0;c<3;++c) pixels[3*i+c]=rgb[c];
    image->image.setValue(SbVec2s(16,16),3,pixels.data());
  }
};
std::array<unsigned char,3> product(const unsigned char last[4]) {
  std::array<unsigned char,3> out;
  for(int channel=0;channel<3;++channel) {
    float value=1;
    for(int unit=0;unit<7;++unit) value*=float(249-3*channel)/255.f;
    value*=float(last[channel])/255.f;
    out[channel]=static_cast<unsigned char>(std::lround(value*255));
  }
  return out;
}
}
int main(int argc,char ** argv) {
  bool plain=false,clear=false,geometry=false,texturedGeometry=false;
  bool windowReadback=false;
  for(int i=1;i<argc;++i) {
    if(std::string(argv[i])=="--plain") plain=true;
    else if(std::string(argv[i])=="--clear") clear=true;
    else if(std::string(argv[i])=="--geometry") geometry=true;
    else if(std::string(argv[i])=="--textured-geometry") texturedGeometry=true;
    else if(std::string(argv[i])=="--window-readback") windowReadback=true;
    else {std::cerr << "Usage: CoinRenderImageInheritedWindowTest [--plain|--clear|--geometry|--textured-geometry] [--window-readback]\n";return 2;}
  }
  if(int(plain)+int(clear)+int(geometry)+int(texturedGeometry)>1) return 2;
  SoDB::init();CoinRenderAction::initClass();
  CoinRenderCapabilities caps{};
  if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,
                                    &caps,sizeof(caps))!=0 || !caps.gpu_available) return 77;
  std::cout << "image_inherited_window backend=" << caps.backend << " renderer=" << caps.renderer
            << " vendor=" << caps.vendor_id << " adapter=" << caps.adapter_name << '\n';
  Display * display=XOpenDisplay(nullptr);
  if(!display) return 77;
  int size=64;
  Window window=XCreateSimpleWindow(display,DefaultRootWindow(display),32,32,size,size,0,0,0);
  XSetWindowAttributes attributes{};attributes.override_redirect=True;
  XChangeWindowAttributes(display,window,CWOverrideRedirect,&attributes);
  XMapRaised(display,window);XSync(display,False);
  CoinRenderNativeSurfaceDescriptor native{};
  native.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  native.structSize=sizeof(native);native.type=COIN_RENDER_SURFACE_XLIB;
  native.native.xlib.display=display;native.native.xlib.window=window;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createWindow(native,SbVec2i32(size,size)));
  if(!check(bool(target)&&target->getStatus()==CoinRenderTarget::TARGET_READY,"window target")) return 1;
  CoinRenderAction action(SbViewportRegion(size,size));
  action.setRenderTarget(target.get());
  action.setBackgroundColor(SbColor4f(0,0,0,1));
  Scene scene;
  if(plain || clear) scene.removeInheritedTextures();
  if(clear) scene.removeImage();
  if(geometry || texturedGeometry) scene.useGeometry(texturedGeometry);
  const unsigned char base[]={249,246,243,255};
  const unsigned char changed[]={61,223,117,255};
  if(plain) scene.payload(product(base));
  if(geometry) scene.geometryColor(product(base));
  uint64_t serial=0;
  std::vector<uint8_t> lastReadback;
  const auto frame=[&](const unsigned char last[4],const char * phase) {
    const auto expected=product(last);
    if(clear) action.setBackgroundColor(SbColor4f(
      float(expected[0])/255.f,float(expected[1])/255.f,
      float(expected[2])/255.f,1));
    const bool requested=!windowReadback || bool(target->requestWindowReadbackRGBA());
    const auto applyBegin=std::chrono::steady_clock::now();
    action.apply(scene.root);
    const auto applyEnd=std::chrono::steady_clock::now();
    const auto applyMs=std::chrono::duration_cast<std::chrono::milliseconds>(
      applyEnd-applyBegin).count();
    std::cout << phase << " apply_ms=" << applyMs << std::endl;
    const uint64_t next=target->getLastSubmissionSerial();
    const bool status=action.getLastStatus()==CoinRenderAction::SUCCESS;
    if(!status) std::cerr << phase << " status=" << int(action.getLastStatus())
                         << " error=" << action.getLastError().getString() << '\n';
    bool bufferGood=true;
    if(windowReadback) {
      std::vector<uint8_t> pixels;
      target->readbackRGBA(pixels);
      const size_t at=(size_t(size/2)*size+size/2)*4;
      bufferGood=requested && pixels.size()==size_t(size)*size*4 &&
        pixels.size()>=at+4;
      if(bufferGood)
        for(int channel=0;channel<3;++channel)
          bufferGood &= std::abs(int(pixels[at+channel])-int(expected[channel]))<=3;
      std::cerr << phase << " requested_readback=" << requested
                << " rgba_bytes=" << pixels.size();
      if(pixels.size()>=at+4)
        std::cerr << " center_rgba=" << int(pixels[at]) << ','
                  << int(pixels[at+1]) << ',' << int(pixels[at+2]) << ','
                  << int(pixels[at+3]);
      std::cerr << " readback_ok=" << bufferGood << '\n';
      if(status && pixels.size()==size_t(size)*size*4)
        lastReadback=std::move(pixels);
    }
    const bool physical=observe(display,window,size,expected,phase);
    const bool good=status && requested && bufferGood && next>serial && physical;
    if(good) serial=next;
    return check(good,"window publication, serial and physical RGB");
  };
  bool ok=frame(base,"initial64");
  const unsigned char * current=base;
  const int sizes[]={96,65,64};
  for(int nextSize:sizes) {
    XResizeWindow(display,window,nextSize,nextSize);XSync(display,False);
    XWindowAttributes actual{};
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do {
      XGetWindowAttributes(display,window,&actual);
      if(actual.width==nextSize&&actual.height==nextSize) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while(std::chrono::steady_clock::now()<end);
    if(!check(actual.width==nextSize&&actual.height==nextSize&&
              target->resize(SbVec2i32(nextSize,nextSize)),"native window resize")) {ok=false;break;}
    size=nextSize;action.setViewportRegion(SbViewportRegion(size,size));
    scene.camera->height=float(size);
    current=current==base?changed:base;
    if(!plain && !clear && !geometry)
      scene.lastTexture->image.setValue(SbVec2s(1,1),4,current);
    if(plain) scene.payload(product(current));
    if(geometry) scene.geometryColor(product(current));
    ok &= frame(current,"resized-and-revised");
  }
  current=current==base?changed:base;
  if(!plain && !clear && !geometry)
    scene.lastTexture->image.setValue(SbVec2s(1,1),4,current);
  if(plain) scene.payload(product(current));
  if(geometry) scene.geometryColor(product(current));
  ok &= frame(current,"unit7-mutated");
  if(clear) action.setBackgroundColor(SbColor4f(
    std::numeric_limits<float>::quiet_NaN(),0,0,1));
  else if(plain || geometry) scene.camera->height=std::numeric_limits<float>::quiet_NaN();
  else scene.q(0);
  action.apply(scene.root);
  bool bufferPreserved=true;
  if(windowReadback) {
    std::vector<uint8_t> after;
    target->readbackRGBA(after);
    bufferPreserved=!lastReadback.empty() && after==lastReadback;
  }
  const bool rejected=action.getLastStatus()==CoinRenderAction::INVALID_SCENE &&
                      target->getLastSubmissionSerial()==serial &&
                      bufferPreserved &&
                      observe(display,window,size,product(current),"invalid-q-preserved");
  ok &= check(rejected,clear ?
    "nonfinite clear color must preserve physical X11 frame and serial" :
    (plain || geometry) ? "invalid camera must preserve physical X11 frame and serial" :
    "Q0 must preserve physical X11 frame and serial");
  if(plain || geometry) scene.camera->height=float(size);
  else if(!clear) scene.q(1);
  ok &= frame(current,"recovered");
  action.setRenderTarget(nullptr);target.reset();
  XDestroyWindow(display,window);XSync(display,False);
  window=XCreateSimpleWindow(display,DefaultRootWindow(display),32,32,size,size,0,0,0);
  XChangeWindowAttributes(display,window,CWOverrideRedirect,&attributes);
  XMapRaised(display,window);XSync(display,False);
  native.native.xlib.window=window;
  target.reset(CoinRenderTarget::createWindow(native,SbVec2i32(size,size)));
  ok &= check(bool(target)&&target->getStatus()==CoinRenderTarget::TARGET_READY,
              "recreated window target");
  if(target) {
    serial=0;lastReadback.clear();action.setRenderTarget(target.get());
    ok &= frame(current,"recreated");
    action.setRenderTarget(nullptr);target.reset();
  }
  XDestroyWindow(display,window);XCloseDisplay(display);
  return ok?0:1;
}
