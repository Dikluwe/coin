#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoInteraction.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoBoxHighlightRenderAction.h>
#include <Inventor/actions/SoLineHighlightRenderAction.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSelection.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
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
#include <memory>
#include <limits>
#include <string>
#include <thread>
#include <vector>
namespace {
constexpr int side=96;
bool check(bool value,const std::string & label) {
  if(!value) std::cerr << "manager_highlight FAIL " << label << '\n';
  return value;
}
struct CountBox : SoBoxHighlightRenderAction {
  int calls=0;
  void apply(SoNode * n) override {++calls;SoBoxHighlightRenderAction::apply(n);}
  void apply(SoPath * p) override {++calls;SoBoxHighlightRenderAction::apply(p);}
  void apply(const SoPathList & p,SbBool obeys=FALSE) override {
    ++calls;SoBoxHighlightRenderAction::apply(p,obeys);
  }
};
struct CountLine : SoLineHighlightRenderAction {
  int calls=0;
  void apply(SoNode * n) override {++calls;SoLineHighlightRenderAction::apply(n);}
  void apply(SoPath * p) override {++calls;SoLineHighlightRenderAction::apply(p);}
  void apply(const SoPathList & p,SbBool obeys=FALSE) override {
    ++calls;SoLineHighlightRenderAction::apply(p,obeys);
  }
};
struct Scene {
  SoSeparator * root=new SoSeparator;
  SoSelection * selection=new SoSelection;
  SoSeparator * selected=new SoSeparator;
  SoSeparator * sibling=new SoSeparator;
  SoOrthographicCamera * camera=new SoOrthographicCamera;
  Scene() {
    root->ref();
    camera->position.setValue(0,0,3);camera->height=2;
    camera->nearDistance=1;camera->farDistance=5;root->addChild(camera);
    auto * lighting=new SoLightModel;lighting->model=SoLightModel::BASE_COLOR;
    root->addChild(lighting);
    auto * selectedMaterial=new SoMaterial;
    selectedMaterial->diffuseColor.setValue(0,.65f,0);selected->addChild(selectedMaterial);
    auto * coordinates=new SoCoordinate3;
    const SbVec3f points[]={SbVec3f(-.65f,-.6f,0),
      SbVec3f(.65f,-.6f,0),SbVec3f(-.65f,.65f,0)};
    coordinates->point.setValues(0,3,points);selected->addChild(coordinates);
    auto * triangle=new SoIndexedFaceSet;
    const int32_t indices[]={0,1,2,-1};
    triangle->coordIndex.setValues(0,4,indices);selected->addChild(triangle);
    auto * siblingMaterial=new SoMaterial;
    siblingMaterial->diffuseColor.setValue(0,0,.8f);sibling->addChild(siblingMaterial);
    auto * siblingCoordinates=new SoCoordinate3;
    const SbVec3f siblingPoints[]={SbVec3f(.7f,.7f,0),
      SbVec3f(.92f,.7f,0),SbVec3f(.7f,.92f,0)};
    siblingCoordinates->point.setValues(0,3,siblingPoints);
    sibling->addChild(siblingCoordinates);
    auto * siblingShape=new SoIndexedFaceSet;
    siblingShape->coordIndex.setValues(0,4,indices);sibling->addChild(siblingShape);
    selection->addChild(selected);selection->addChild(sibling);
    root->addChild(selection);
  }
  ~Scene(){root->unref();}
};
struct Frame {
  std::vector<uint8_t> rgba;
  std::vector<float> depth;
  uint64_t serial=0;
};
bool capture(CoinRenderManagerAdapter & adapter,Frame & out,const char * label) {
  const auto status=adapter.render();
  auto * target=adapter.getSceneManager()->getRenderTarget();
  target->readbackRGBA(out.rgba);target->readbackDepth(out.depth);
  out.serial=target->getLastSubmissionSerial();
  const bool valid=status==CoinRenderAction::SUCCESS &&
    out.rgba.size()==side*side*4u && out.depth.size()==side*side && out.serial>0;
  if(!valid) std::cerr << "manager_highlight " << label << " status=" << int(status)
    << " error=" << adapter.getLastError().getString() << " rgba=" << out.rgba.size()
    << " depth=" << out.depth.size() << " serial=" << out.serial << '\n';
  return check(valid,label);
}
size_t colored(const Frame & frame,int channel) {
  size_t result=0;
  for(size_t i=0;i<frame.rgba.size()/4;++i)
    result+=(frame.rgba[4*i+channel]>100 &&
      frame.rgba[4*i+channel] > frame.rgba[4*i+((channel+1)%3)]+30);
  return result;
}
size_t redIn(const Frame & frame,int x0,int y0,int x1,int y1) {
  size_t result=0;
  for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
    const size_t p=4u*(size_t(y)*side+x);
    result+=(frame.rgba[p]>130 && frame.rgba[p]>frame.rgba[p+1]+70);
  }
  return result;
}
CoinRenderOptions options(bool gpu) {
  CoinRenderOptions selected;
  if(gpu) {
    CoinRenderCapabilities caps{};
    const int result=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
      &caps,sizeof(caps));
    std::cout << "manager_highlight backend=" << caps.backend
              << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
              << " adapter=" << caps.adapter_name << " gpu=" << caps.gpu_available << '\n';
    if(result==0 && caps.gpu_available &&
       (caps.backend==COIN_RENDER_EXPERIMENTAL_RUST ||
        caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION) &&
       caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN)
      selected.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  }
  return selected;
}
template<class Highlight>
bool exercise(bool gpu,const char * kind) {
  Scene scene;Highlight highlight;
  SoRenderManager manager;
  manager.setViewportRegion(SbViewportRegion(side,side));
  manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.setBackgroundColor(SbColor4f(0,0,0,1));
  manager.setSceneGraph(scene.root);
  manager.setGLRenderAction(&highlight);
  CoinRenderOptions selected=options(gpu);
  if(gpu && !check(selected.renderer!=COIN_RENDER_RENDERER_UNKNOWN,
                   "requested GPU API must be identified")) return false;
  CoinRenderManagerAdapter adapter(manager,SbVec2i32(side,side),selected);
  auto * target=adapter.getSceneManager()->getRenderTarget();
  if(!check(bool(target) && (!gpu || target->getOptions().renderer==selected.renderer),
            "adapter target/API")) return false;
  target->setDepthReadbackEnabled(TRUE);
  if(!gpu) target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
  Frame bare,first,changed,invisible,unselected,recovered;
  bool ok=capture(adapter,bare,"base without selection");
  scene.selection->select(scene.selected);
  ok &= check(scene.selection->getNumSelected()==1,"selected path exists");
  ok &= capture(adapter,first,"selected frame");
  ok &= check(first.serial>bare.serial && first.rgba!=bare.rgba &&
              colored(first,0)>colored(bare,0)+8,
              "highlight visible in one newly published frame");
  if(first.rgba.size()==bare.rgba.size()) {
    bool siblingSame=true;
    for(int y=82;y<94;++y)for(int x=82;x<94;++x) {
      const size_t pixel=size_t(y)*side+x;
      for(int c=0;c<4;++c)
        siblingSame &= first.rgba[4*pixel+c]==bare.rgba[4*pixel+c];
      siblingSame &= first.depth[pixel]==bare.depth[pixel];
    }
    ok &= check(siblingSame,"unselected sibling RGB/alpha/depth unchanged");
  }
  const auto box=std::string(kind)=="box";
  const size_t top=redIn(first,42,75,55,82);
  const size_t diagonal=redIn(first,44,45,52,53);
  std::cout << "manager_highlight kind=" << kind << " gpu=" << gpu
            << " top=" << top << " diagonal=" << diagonal
            << " red=" << colored(first,0) << " serial=" << first.serial << '\n';
  ok &= check(box ? top>0 : diagonal>0,"box edge or wire diagonal witness");
  highlight.setLineWidth(1.f);
  Frame thin,defaultWidth,wide,patterned,emptyPattern;
  ok &= capture(adapter,thin,"line width one");
  highlight.setLineWidth(0.f);
  ok &= capture(adapter,defaultWidth,"line width zero alias") &&
    check(defaultWidth.rgba==thin.rgba && defaultWidth.depth==thin.depth,
          "line width zero aliases one");
  highlight.setLineWidth(5.f);
  ok &= capture(adapter,wide,"line width five") &&
    check(wide.rgba!=thin.rgba && colored(wide,0)>colored(thin,0),
          "line width affects highlight coverage");
  highlight.setLineWidth(3.f);
  highlight.setLinePattern(0x00ff);
  ok &= capture(adapter,patterned,"line pattern 00ff") &&
    check(patterned.rgba!=first.rgba,"line pattern affects coverage");
  highlight.setLinePattern(0x0000);
  ok &= capture(adapter,emptyPattern,"line pattern zero") &&
    check(emptyPattern.rgba==bare.rgba && emptyPattern.depth==bare.depth,
          "empty pattern leaves original scene intact");
  highlight.setLinePattern(0xffff);
  highlight.setColor(SbColor(0,1,1));
  ok &= capture(adapter,changed,"highlight color changed") &&
    check(changed.rgba!=first.rgba && colored(changed,0)<colored(first,0),
          "highlight RGB style changes without touching scene material");
  highlight.setVisible(FALSE);
  ok &= capture(adapter,invisible,"invisible highlight") &&
    check(invisible.rgba==bare.rgba && invisible.depth==bare.depth,
          "invisible style retains base frame");
  highlight.setVisible(TRUE);
  scene.selection->deselectAll();
  ok &= capture(adapter,unselected,"deselected") &&
    check(unselected.rgba==bare.rgba && unselected.depth==bare.depth,
          "no selection retains base frame");
  highlight.setColor(SbColor(1,0,0));
  highlight.setLineWidth(3.f);highlight.setLinePattern(0xffff);
  scene.selection->select(scene.selected);
  ok &= capture(adapter,recovered,"A/B/A recovery") &&
    check(recovered.rgba==first.rgba && recovered.depth==first.depth &&
          recovered.serial>unselected.serial,"highlight A/B/A exact recovery");
  highlight.setLineWidth(std::numeric_limits<float>::quiet_NaN());
  const auto invalidStatus=adapter.render();
  Frame rejected;
  target->readbackRGBA(rejected.rgba);target->readbackDepth(rejected.depth);
  rejected.serial=target->getLastSubmissionSerial();
  std::cout << "manager_highlight invalid_width kind=" << kind
            << " status=" << int(invalidStatus)
            << " error=" << adapter.getLastError().getString() << '\n';
  ok &= check(invalidStatus!=CoinRenderAction::SUCCESS &&
    rejected.rgba==recovered.rgba && rejected.depth==recovered.depth &&
    rejected.serial==recovered.serial,"invalid active width rolls back frame");
  highlight.setVisible(FALSE);
  Frame hiddenInvalidStyle;
  ok &= capture(adapter,hiddenInvalidStyle,"invisible invalid style irrelevant") &&
    check(hiddenInvalidStyle.rgba==bare.rgba && hiddenInvalidStyle.depth==bare.depth,
          "inactive style does not reject or affect base");
  highlight.setLineWidth(3.f);highlight.setVisible(TRUE);
  Frame validAgain;
  ok &= capture(adapter,validAgain,"post-invalid recovery") &&
    check(validAgain.rgba==first.rgba && validAgain.depth==first.depth &&
      validAgain.serial>recovered.serial,"post-invalid highlight recovery");
  ok &= check(highlight.calls==0,"adapter must not apply host GL action");
  ok &= check(scene.camera->nearDistance.getValue()==1.f &&
              scene.camera->farDistance.getValue()==5.f &&
              scene.selection->getNumSelected()==1 &&
              scene.selected->getNumChildren()==3,
              "host camera, selected path and graph remain unchanged");
  manager.setGLRenderAction(nullptr);
  manager.setSceneGraph(nullptr);
  return ok;
}
bool run(bool gpu) {
  const bool box=exercise<CountBox>(gpu,"box");
  const bool line=exercise<CountLine>(gpu,"line");
  return box && line;
}
uint8_t xchannel(unsigned long value,unsigned long mask) {
  if(!mask) return 0;
  while(!(mask&1)){mask>>=1;value>>=1;}
  return uint8_t(((value&mask)*255+mask/2)/mask);
}
std::vector<uint8_t> ximage(Display *display,Window window,int size) {
  XSync(display,False);
  XImage *image=XGetImage(display,window,0,0,size,size,AllPlanes,ZPixmap);
  if(!image)return {};
  std::vector<uint8_t> rgb(size_t(size)*size*3);
  for(int y=0;y<size;++y)for(int x=0;x<size;++x){
    const auto raw=XGetPixel(image,x,y);
    const size_t p=3u*(size_t(y)*size+x);
    rgb[p]=xchannel(raw,image->red_mask);
    rgb[p+1]=xchannel(raw,image->green_mask);
    rgb[p+2]=xchannel(raw,image->blue_mask);
  }
  XDestroyImage(image);return rgb;
}
template<class Highlight>
bool windowCase(Display * display,Window window,
                const CoinRenderNativeSurfaceDescriptor & surface,
                const CoinRenderOptions & options,const char *kind,
                std::vector<uint8_t> &lastPhysical) {
  Scene scene;Highlight highlight;SoRenderManager manager;
  manager.setViewportRegion(SbViewportRegion(side,side));
  manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.setSceneGraph(scene.root);manager.setGLRenderAction(&highlight);
  CoinRenderManagerAdapter offscreen(manager,SbVec2i32(side,side),options);
  CoinRenderManagerAdapter presented(manager,surface,SbVec2i32(side,side),options);
  auto *offTarget=offscreen.getSceneManager()->getRenderTarget();
  auto *windowTarget=presented.getSceneManager()->getRenderTarget();
  if(!check(offTarget && windowTarget &&
    offTarget->getOptions().renderer==options.renderer &&
    windowTarget->getOptions().renderer==options.renderer,
    "window/offscreen actual renderer")) return false;
  scene.selection->select(scene.selected);
  uint64_t previousSerial=0;
  std::vector<uint8_t> first;
  const uint8_t initialStamp=std::string(kind)=="box" ? 31 : 39;
  auto phase=[&](int size,uint8_t stamp,const char *label,bool expectFirst) {
    manager.setBackgroundColor(SbColor4f(0,0,stamp/255.f,1));
    const auto a=offscreen.render();const auto b=presented.render();
    const auto returned=std::chrono::steady_clock::now();
    std::vector<uint8_t> reference;offTarget->readbackRGBA(reference);
    const uint64_t serial=windowTarget->getLastSubmissionSerial();
    if(a!=CoinRenderAction::SUCCESS || b!=CoinRenderAction::SUCCESS ||
       reference.size()!=size_t(size)*size*4 || serial<=previousSerial) {
      std::cerr << "manager_highlight_window " << label << " status=" << int(a)
                << ',' << int(b) << " error=" << presented.getLastError().getString()
                << " serial=" << serial << " previous=" << previousSerial << '\n';
      return false;
    }
    size_t redPixels=0,cyanPixels=0;
    for(size_t pixel=0;pixel<size_t(size)*size;++pixel) {
      const int r=reference[4*pixel],g=reference[4*pixel+1],b=reference[4*pixel+2];
      redPixels+=(r>130 && r>g+70 && r>b+70);
      cyanPixels+=(g>130 && b>130 && r<60);
    }
    const bool mutated=std::string(label)=="style-mutated";
    if(!check(mutated?cyanPixels>8:redPixels>8,
              "window reference contains the authored highlight"))return false;
    previousSerial=serial;
    std::vector<uint8_t> physical;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    unsigned physicalReads=0;
    auto firstRead=deadline,lastReadBegin=deadline,lastReadEnd=deadline;
    double maximumReadMs=0;
    do {
      lastReadBegin=std::chrono::steady_clock::now();
      if(!physicalReads)firstRead=lastReadBegin;
      physical=ximage(display,window,size);
      lastReadEnd=std::chrono::steady_clock::now();++physicalReads;
      maximumReadMs=std::max(maximumReadMs,std::chrono::duration<double,std::milli>(lastReadEnd-lastReadBegin).count());
      if(physical.size()==size_t(size)*size*3 && physical[2]==stamp)break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while(std::chrono::steady_clock::now()<deadline);
    const auto offsetMs=[&](std::chrono::steady_clock::time_point t) {
      return std::chrono::duration<double,std::milli>(t-returned).count();
    };
    const bool previousComparable=!lastPhysical.empty()&&lastPhysical.size()==physical.size();
    std::cout << "manager_highlight_window observation kind=" << kind << " phase=" << label
              << " reads=" << physicalReads << " first_read_begin_ms=" << offsetMs(firstRead)
              << " last_read_begin_ms=" << offsetMs(lastReadBegin)
              << " last_read_end_ms=" << offsetMs(lastReadEnd) << " max_read_ms=" << maximumReadMs
              << " previous_comparable=" << previousComparable
              << " same_previous=" << (previousComparable&&physical==lastPhysical)
              << " serial_at_return=" << serial << " serial_after_reads=" << windowTarget->getLastSubmissionSerial() << '\n';
    if(physical.size()!=size_t(size)*size*3 || physical[2]!=stamp) {
      XWindowAttributes actual{};
      const bool gotAttributes=XGetWindowAttributes(display,window,&actual)!=0;
      std::cerr << "manager_highlight_window stamp_diagnostic kind=" << kind
                << " phase=" << label << " requested_size=" << size
                << " actual_size=" << (gotAttributes?actual.width:-1) << 'x'
                << (gotAttributes?actual.height:-1)
                << " rgb_bytes=" << physical.size()
                << " first_rgb=";
      if(physical.size()>=3)
        std::cerr << int(physical[0]) << ',' << int(physical[1]) << ','
                  << int(physical[2]);
      else std::cerr << "unavailable";
      std::cerr << " expected_stamp=" << int(stamp)
                << " serial=" << serial << '\n';
    }
    if(!check(physical.size()==size_t(size)*size*3 && physical[2]==stamp,
              std::string(label)+" physical X11 stamp"))return false;
    int maximum=0;size_t changed=0;
    for(size_t i=0;i<size_t(size)*size;++i)for(size_t c=0;c<3;++c){
      const int delta=std::abs(int(reference[4*i+c])-int(physical[3*i+c]));
      maximum=std::max(maximum,delta);changed+=(delta>2);
    }
    std::cout << "manager_highlight_window kind=" << kind << " phase=" << label
              << " size=" << size << " max_rgb=" << maximum
              << " channels_over2=" << changed << " red=" << redPixels
              << " cyan=" << cyanPixels << " serial=" << serial << '\n';
    if(!check(changed==0,std::string(label)+" X11 equals offscreen RGB"))return false;
    // Only CPU bytes survive BOX/LINE scope teardown; no target or runtime.
    lastPhysical=physical;
    if(expectFirst) {
      if(first.empty())first=reference;
      else if(!check(first==reference,"X11 A/B/A source frame restored"))return false;
    }
    return check(highlight.calls==0,"window adapter never applies host GL action");
  };
  bool ok=phase(side,initialStamp,"initial",true);
  XResizeWindow(display,window,128,128);XSync(display,False);
  manager.setViewportRegion(SbViewportRegion(128,128));
  ok &= check(offscreen.resize(SbVec2i32(128,128)) &&
              presented.resize(SbVec2i32(128,128)),"highlight window resize 128");
  if(ok)ok &= phase(128,67,"resized",false);
  highlight.setColor(SbColor(0,1,1));highlight.setLinePattern(0x00ff);
  if(ok)ok &= phase(128,103,"style-mutated",false);
  highlight.setColor(SbColor(1,0,0));highlight.setLinePattern(0xffff);
  XResizeWindow(display,window,side,side);XSync(display,False);
  manager.setViewportRegion(SbViewportRegion(side,side));
  ok &= check(offscreen.resize(SbVec2i32(side,side)) &&
              presented.resize(SbVec2i32(side,side)),"highlight window resize back");
  if(ok)ok &= phase(side,initialStamp,"restored",true);
  manager.setGLRenderAction(nullptr);manager.setSceneGraph(nullptr);
  return ok;
}
bool windowRun() {
  CoinRenderCapabilities caps{};
  if(!check(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,
    &caps,sizeof(caps))==0 && caps.gpu_available &&
    (caps.backend==COIN_RENDER_EXPERIMENTAL_RUST ||
     caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION) &&
    caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN,"window GPU capability")) return false;
  std::cout << "manager_highlight_window backend=" << caps.backend
            << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
            << " adapter=" << caps.adapter_name << '\n';
  Display *display=XOpenDisplay(nullptr);if(!display)return false;
  Window window=XCreateSimpleWindow(display,DefaultRootWindow(display),8,8,
                                     side,side,0,0,0);
  XSetWindowAttributes attributes{};attributes.override_redirect=True;
  XChangeWindowAttributes(display,window,CWOverrideRedirect,&attributes);
  XMapRaised(display,window);XSync(display,False);
  CoinRenderNativeSurfaceDescriptor surface{};
  surface.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  surface.structSize=sizeof(surface);surface.type=COIN_RENDER_SURFACE_XLIB;
  surface.native.xlib.display=display;surface.native.xlib.window=window;
  CoinRenderOptions options;
  options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  std::vector<uint8_t> lastPhysical;
  const bool box=windowCase<CountBox>(display,window,surface,options,"box",lastPhysical);
  const bool line=windowCase<CountLine>(display,window,surface,options,"line",lastPhysical);
  XDestroyWindow(display,window);XCloseDisplay(display);
  return box&&line;
}
}
int main(int argc,char **argv) {
  if(argc!=2) return 2;
  SoDB::init();SoInteraction::init();CoinRenderAction::initClass();
  if(SoBoxHighlightRenderAction::getClassTypeId().isBad())
    SoBoxHighlightRenderAction::initClass();
  if(SoLineHighlightRenderAction::getClassTypeId().isBad())
    SoLineHighlightRenderAction::initClass();
  const std::string mode=argv[1];
  if(mode=="--cpu") return run(false)?0:1;
  if(mode=="--gpu") return run(true)?0:1;
  if(mode=="--window") return windowRun()?0:1;
  return 2;
}
