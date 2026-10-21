#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPolygonOffset.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#endif
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
namespace {
struct RGB {uint8_t r,g,b;};
bool check(bool good,const char *label) {if(!good)std::cerr<<"msaa_peeling_window FAIL "<<label<<'\n';return good;}
void frameDifference(const std::string &label,int width,
                     const std::vector<uint8_t> &oldRgba,const std::vector<float> &oldDepth,
                     const std::vector<uint8_t> &newRgba,const std::vector<float> &newDepth) {
  const auto oldPrecision=std::cerr.precision(17);
  const size_t n=std::min(oldDepth.size(),newDepth.size());
  const size_t colors=std::min(oldRgba.size(),newRgba.size())/4;
  size_t depthCount=0,rgbaCount=0,firstDepth=n,maxDepth=n,firstRgba=colors,maxRgba=colors;
  double maxAbs=0.;int maxChannel=0;
  size_t categoryDifferent[3]={},categoryOverBound[3]={};double categoryMax[3]={};
  constexpr double diagnosticBound=2./16777216.;
  for(size_t p=0;p<n;++p)if(oldDepth[p]!=newDepth[p]){
    if(!depthCount)firstDepth=p;
    ++depthCount;
    const double delta=std::abs(double(newDepth[p])-oldDepth[p]);
    if(p<colors){
      const size_t category=oldRgba[4*p+3]==0?0:(oldRgba[4*p+3]==255?1:2);
      ++categoryDifferent[category];categoryOverBound[category]+=(delta>diagnosticBound);
      categoryMax[category]=std::max(categoryMax[category],delta);
    }
    if(maxDepth==n||!std::isfinite(delta)||delta>maxAbs){maxDepth=p;maxAbs=delta;}
  }
  for(size_t p=0;p<colors;++p){
    int difference=0;
    for(size_t c=0;c<4;++c)difference=std::max(difference,
      std::abs(int(newRgba[4*p+c])-int(oldRgba[4*p+c])));
    if(difference){
      if(!rgbaCount)firstRgba=p;
      ++rgbaCount;
      if(maxRgba==colors||difference>maxChannel){maxRgba=p;maxChannel=difference;}
    }
  }
  std::cerr<<"msaa_peeling_frame_diff label="<<label
    <<" rgba_equal="<<(oldRgba==newRgba)<<" depth_equal="<<(oldDepth==newDepth)
    <<" rgba_sizes="<<oldRgba.size()<<','<<newRgba.size()
    <<" depth_sizes="<<oldDepth.size()<<','<<newDepth.size()
    <<" rgba_diff_pixels="<<rgbaCount<<" depth_diff_pixels="<<depthCount
    <<" max_channel_delta="<<maxChannel<<" max_depth_abs="<<maxAbs<<'\n';
  const char *categories[]={"alpha_zero","alpha_opaque","alpha_partial"};
  for(size_t category=0;category<3;++category)
    std::cerr<<"msaa_peeling_frame_diff label="<<label<<" category="<<categories[category]
      <<" depth_diff_pixels="<<categoryDifferent[category]
      <<" depth_over_2d24_pixels="<<categoryOverBound[category]
      <<" max_depth_abs="<<categoryMax[category]<<" bound="<<diagnosticBound<<'\n';
  auto point=[&](const char *kind,size_t p){
    std::cerr<<"msaa_peeling_frame_diff label="<<label<<" kind="<<kind
      <<" xy="<<p%width<<','<<p/width;
    if(p<n){
      uint32_t oldBits=0,newBits=0;
      std::memcpy(&oldBits,&oldDepth[p],sizeof(oldBits));
      std::memcpy(&newBits,&newDepth[p],sizeof(newBits));
      std::cerr<<" old_depth="<<oldDepth[p]<<" new_depth="<<newDepth[p]
        <<" old_depth_bits="<<oldBits<<" new_depth_bits="<<newBits;
    }
    if(p<colors){
      std::cerr<<" old_rgba="<<int(oldRgba[4*p])<<','<<int(oldRgba[4*p+1])<<','
        <<int(oldRgba[4*p+2])<<','<<int(oldRgba[4*p+3])
        <<" new_rgba="<<int(newRgba[4*p])<<','<<int(newRgba[4*p+1])<<','
        <<int(newRgba[4*p+2])<<','<<int(newRgba[4*p+3]);
    }
    std::cerr<<'\n';
  };
  if(depthCount){point("first_depth",firstDepth);point("maximum_depth",maxDepth);}
  if(rgbaCount){point("first_rgba",firstRgba);point("maximum_rgba",maxRgba);}
  std::cerr.precision(oldPrecision);
}
uint8_t channel(unsigned long value,unsigned long mask) {
  if(!mask)return 0;while(!(mask&1)){value>>=1;mask>>=1;}
  return uint8_t(((value&mask)*255+mask/2)/mask);
}
std::vector<RGB> physical(Display *display,Window window,int side) {
  XSync(display,False);XImage *raw=XGetImage(display,window,0,0,side,side,AllPlanes,ZPixmap);
  if(!raw)return {};std::vector<RGB> result(size_t(side)*side);
  for(int y=0;y<side;++y)for(int x=0;x<side;++x){
    const auto px=XGetPixel(raw,x,y);result[size_t(y)*side+x]={channel(px,raw->red_mask),
      channel(px,raw->green_mask),channel(px,raw->blue_mask)};
  }
  XDestroyImage(raw);return result;
}
bool waitXGeometry(Display *display,Window window,int side,const char *phase) {
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  XWindowAttributes actual{};bool ready=false;unsigned polls=0;
  do {
    const bool queried=XGetWindowAttributes(display,window,&actual)!=0;
    ++polls;
    ready=queried&&actual.width==side&&actual.height==side&&actual.map_state==IsViewable;
    if(polls==1)std::cout<<"msaa_peeling_window native_geometry phase="<<phase
      <<" requested="<<side<<','<<side<<" actual="<<actual.width<<','<<actual.height
      <<" queried="<<queried<<" map_state="<<actual.map_state<<'\n';
    if(ready)break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    XSync(display,False);
  }while(std::chrono::steady_clock::now()<deadline);
  std::cout<<"msaa_peeling_window native_geometry_ready phase="<<phase
    <<" requested="<<side<<','<<side<<" actual="<<actual.width<<','<<actual.height
    <<" map_state="<<actual.map_state<<" polls="<<polls<<" ready="<<ready<<'\n';
  return check(ready,"native X geometry must match before target resize/render");
}
SoSeparator *quad(float z,const SbColor &color,float span,float tx=0,float ty=0,
                  float transparency=0) {
  auto *root=new SoSeparator;auto *camera=new SoOrthographicCamera;
  camera->position.setValue(0,0,2);camera->height=2;camera->nearDistance=1;camera->farDistance=3;
  root->addChild(camera);
  auto *move=new SoTranslation;move->translation.setValue(tx,ty,0);root->addChild(move);
  auto *unlit=new SoLightModel;unlit->model=SoLightModel::BASE_COLOR;
  root->addChild(unlit);auto *material=new SoMaterial;material->diffuseColor=color;
  material->transparency=transparency;root->addChild(material);
  auto *coords=new SoCoordinate3;const SbVec3f p[]={SbVec3f(-span,-span,z),
    SbVec3f(span,-span,z),SbVec3f(span,span,z),SbVec3f(-span,span,z)};
  coords->point.setValues(0,4,p);root->addChild(coords);
  auto *face=new SoFaceSet;face->numVertices.set1Value(0,4);root->addChild(face);return root;
}
struct Scene {
  SoSeparator *base=new SoSeparator,*front=quad(.5f,SbColor(0,1,0),.7f,0,0,.5f);
  SoSeparator *background=quad(.5f,SbColor(0,0,24.f/255.f),1.2f);
  SoMaterial *frontMaterial=static_cast<SoMaterial *>(front->getChild(3));
  SoMaterial *backgroundMaterial=static_cast<SoMaterial *>(background->getChild(3));
  explicit Scene(bool slantedOpaque) {
    base->ref();front->ref();background->ref();
    auto *opaque=quad(0,SbColor(1,0,0),.85f);
    if(slantedOpaque){
      // The left edge sweeps across several subpixel phases inside the
      // left strip used by the independent k/N witness. An axis-aligned
      // edge at x=.075*width need not expose both 1/4 and 3/4 coverage.
      auto *points=static_cast<SoCoordinate3 *>(opaque->getChild(4));
      points->point.set1Value(0,SbVec3f(-.92f,-.85f,0));
      points->point.set1Value(3,SbVec3f(-.72f,.85f,0));
    }
    base->addChild(opaque);
    // Two transparent depths in the base scene distinguish back-to-front
    // peeling from traversal-order OBJECT blending.
    base->addChild(quad(-.5f,SbColor(0,1,1),1.2f,0,0,.5f));
    base->addChild(quad(-.75f,SbColor(1,0,1),1.2f,0,0,.75f));
    auto *annotation=new SoAnnotation;
    annotation->addChild(quad(-.8f,SbColor(1,1,0),.15f,.8f,.8f));
    base->addChild(annotation);
  }
  ~Scene(){base->unref();front->unref();background->unref();}
};
bool compare(Display *display,Window window,int side,const std::vector<uint8_t> &expected,
             uint32_t samples,int backgroundBlueByte,const char *label,bool cyan=false,
             bool backgroundLayer=false) {
  std::vector<RGB> observed;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  do {
    observed=physical(display,window,side);
    bool complete=observed.size()==size_t(side)*side;
    if(complete)for(size_t i=0;i<observed.size();++i)if(
      observed[i].r!=expected[4*i] || observed[i].g!=expected[4*i+1] ||
      observed[i].b!=expected[4*i+2]) {complete=false;break;}
    if(complete)break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while(std::chrono::steady_clock::now()<deadline);
  if(!check(observed.size()==size_t(side)*side,"physical X11 image"))return false;
  size_t wrong=0,quarter=0,half=0,three=0;
  int maxError=0;
  const float blue=float(backgroundBlueByte)/255.f;
  // Near cyan (alpha .5, depth .75) over far magenta (alpha .25,
  // depth .875), in front-to-back peeling order. Ordinary traversal
  // composites magenta last and differs by 32+ red bytes here.
  const float outsideR=.125f;
  const float outsideG=.5f;
  const float outsideB=.625f+.375f*blue;
  for(size_t i=0;i<observed.size();++i) {
    const auto p=observed[i];
    const int delta=std::max({std::abs(int(p.r)-int(expected[4*i])),
      std::abs(int(p.g)-int(expected[4*i+1])),
      std::abs(int(p.b)-int(expected[4*i+2]))});
    wrong+=(delta!=0);maxError=std::max(maxError,delta);
    const int x=int(i%side),y=int(i/side);
    // These strips avoid the foreground and annotation. Their slanted
    // opaque edge sweeps subpixel phases on both 32 and 48 viewports.
    if(x<int(.15f*side)||y<int(.15f*side)) {
      auto fraction=[&](int k) {
        const float t=float(k)/float(samples);
        const int r=int(std::lround(255.f*(t+(1-t)*outsideR)));
        const int g=int(std::lround(255.f*(1-t)*outsideG));
        const int b=int(std::lround(255.f*(1-t)*outsideB));
        return std::abs(int(expected[4*i])-r)<=3 &&
          std::abs(int(expected[4*i+1])-g)<=3 &&
          std::abs(int(expected[4*i+2])-b)<=3;
      };
      quarter+=(samples==4&&fraction(1));
      half+=(samples==2&&fraction(1));
      three+=(samples==4&&fraction(3));
    }
  }
  const size_t witness=size_t(side/2)*side+1;
  const bool peelingOutside=
    std::abs(int(expected[4*witness])-int(std::lround(255.f*outsideR)))<=3 &&
    std::abs(int(expected[4*witness+1])-int(std::lround(255.f*outsideG)))<=3 &&
    std::abs(int(expected[4*witness+2])-int(std::lround(255.f*outsideB)))<=3 &&
    std::abs(int(expected[4*witness+3])-int(std::lround(
      (backgroundLayer?1.f:.625f)*255)))<=2;
  if(!peelingOutside)std::cerr<<"msaa_peeling_window outside rgba="
    <<int(expected[4*witness])<<','<<int(expected[4*witness+1])<<','
    <<int(expected[4*witness+2])<<','<<int(expected[4*witness+3])
    <<" expected="<<outsideR*255<<','<<outsideG*255<<','
    <<outsideB*255<<','<<(backgroundLayer?255:159)<<'\n';
  std::cout<<"msaa_peeling_window phase="<<label<<" side="<<side
    <<" samples="<<samples<<" wrong="<<wrong<<" max_rgb="<<maxError
    <<" quarter="<<quarter<<" half="<<half<<" threequarter="<<three<<'\n';
  return check(wrong==0 && peelingOutside &&
    (samples==1 || (samples==2?half>0:quarter>0&&three>0)),
    "physical X11 matches analytic two-depth peeling and k/N coverage");
}
bool run(bool withBackground) {
  CoinRenderCapabilities caps{};
  if(!check(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,&caps,sizeof(caps))==0 &&
      caps.gpu_available &&
      (caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION ||
       caps.backend==COIN_RENDER_EXPERIMENTAL_RUST) &&
      caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN,"identified GPU window adapter"))return false;
  std::cout<<"msaa_peeling_window renderer="<<caps.renderer<<" vendor="<<caps.vendor_id
           <<" adapter="<<caps.adapter_name<<'\n';
  Display *display=XOpenDisplay(nullptr);if(!check(display!=nullptr,"X display"))return false;
  Window window=XCreateSimpleWindow(display,DefaultRootWindow(display),8,8,32,32,0,0,0);
  XSetWindowAttributes attrs{};attrs.override_redirect=True;
  XChangeWindowAttributes(display,window,CWOverrideRedirect,&attrs);XMapRaised(display,window);XSync(display,False);
  CoinRenderNativeSurfaceDescriptor native{};native.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  native.structSize=sizeof(native);native.type=COIN_RENDER_SURFACE_XLIB;
  native.native.xlib.display=display;native.native.xlib.window=window;
  // Both window modes need the same slanted opaque edge to expose every
  // 4x sample fraction; the background layer is controlled independently.
  bool ok=true;Scene scene(true);SoRenderManager manager;
  // Keep a 1x target alive while the 1/2/4/2/1 window and offscreen
  // targets are created. This detects cache keys that omit sampleCount.
  auto *anchorRoot=quad(0,SbColor(1,0,0),.5f);anchorRoot->ref();
  CoinRenderOptions anchorOptions;
  anchorOptions.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  anchorOptions.sampleCount=1;
  anchorOptions.transparency=COIN_RENDER_TRANSPARENCY_PEELING;
  std::unique_ptr<CoinRenderTarget> anchor(
    CoinRenderTarget::createOffscreen(SbVec2i32(32,32),anchorOptions));
  CoinRenderAction anchorAction(SbViewportRegion(32,32));
  if(!check(bool(anchor),"live 1x target")){anchorRoot->unref();XDestroyWindow(display,window);XCloseDisplay(display);return false;}
  anchorAction.setRenderTarget(anchor.get());anchorAction.apply(anchorRoot);
  uint64_t previousDeviceSerial=anchor->getLastSubmissionSerial();
  ok &= check(anchorAction.getLastStatus()==CoinRenderAction::SUCCESS && previousDeviceSerial>0,
    "live 1x runtime anchor publishes before peeling window");
  manager.setSceneGraph(scene.base);manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::SORTED_LAYERS_BLEND);
  manager.getGLRenderAction()->setSortedLayersNumPasses(2);
  SoRenderManager::Superimposition *backgroundLayer=nullptr;
  if(withBackground)backgroundLayer=manager.addSuperimposition(scene.background,
    SoRenderManager::Superimposition::AUTOREDRAW|
    SoRenderManager::Superimposition::BACKGROUND|
    SoRenderManager::Superimposition::ZBUFFERON);
  const unsigned flags=SoRenderManager::Superimposition::AUTOREDRAW |
    SoRenderManager::Superimposition::ZBUFFERON |
    SoRenderManager::Superimposition::CLEARZBUFFER;
  auto *layer=manager.addSuperimposition(scene.front,flags);
  for(uint32_t samples:{1u,2u,4u,2u,1u}) {
    CoinRenderOptions options;options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
    options.sampleCount=samples;options.transparency=COIN_RENDER_TRANSPARENCY_PEELING;
    std::unique_ptr<CoinRenderManagerAdapter> off(new CoinRenderManagerAdapter(manager,SbVec2i32(32,32),options));
    std::unique_ptr<CoinRenderManagerAdapter> shown(new CoinRenderManagerAdapter(manager,native,SbVec2i32(32,32),options));
    if(!check(off->getSceneManager()->getRenderTarget()->getOptions().renderer==caps.renderer &&
      shown->getSceneManager()->getRenderTarget()->getOptions().sampleCount==samples,
      "actual renderer and sample count")){ok=false;break;}
    uint64_t offPrevious=0,windowPrevious=0;
    for(int phase=0;phase<3;++phase){
      const int side=phase==1?48:32;
      if(phase>0){XResizeWindow(display,window,side,side);XSync(display,False);
        ok &= check(off->resize(SbVec2i32(side,side)) && shown->resize(SbVec2i32(side,side)),
          "paired resize");}
      manager.setViewportRegion(SbViewportRegion(side,side));
      manager.setBackgroundColor(SbColor4f(0,0,(24+phase*32)/255.f,0));
      if(withBackground)scene.backgroundMaterial->diffuseColor.setValue(
        0,0,(24+phase*32)/255.f);
      const auto a=off->render(),b=shown->render();
      std::vector<uint8_t> expected;off->getSceneManager()->getRenderTarget()->readbackRGBA(expected);
      const auto offSerial=off->getSceneManager()->getRenderTarget()->getLastSubmissionSerial();
      const auto winSerial=shown->getSceneManager()->getRenderTarget()->getLastSubmissionSerial();
      const uint64_t expectedOff=caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?
        previousDeviceSerial+1:offPrevious+1;
      const uint64_t expectedWindow=caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?
        expectedOff+1:windowPrevious+1;
      std::cout<<"msaa_peeling_window samples="<<samples<<" phase="<<phase
               <<" off_serial="<<offSerial<<" window_serial="<<winSerial
               <<" expected="<<expectedOff<<','<<expectedWindow
               <<" off_status="<<int(a)<<" off_error="<<off->getLastError().getString()
               <<" window_status="<<int(b)<<" window_error="
               <<shown->getLastError().getString()
               <<" expected_bytes="<<expected.size()<<'\n';
      ok &= check(a==CoinRenderAction::SUCCESS && b==CoinRenderAction::SUCCESS &&
        expected.size()==size_t(side)*side*4 && offSerial==expectedOff &&
        winSerial==expectedWindow,"one offscreen and one X11 publication");
      previousDeviceSerial=winSerial;offPrevious=offSerial;windowPrevious=winSerial;
      if(ok)ok &= compare(display,window,side,expected,samples,24+phase*32,
        "resize/restore",false,withBackground);
      if(ok && phase==1) {
        const uint64_t offBefore=off->getSceneManager()->getRenderTarget()->getLastSubmissionSerial();
        const uint64_t winBefore=shown->getSceneManager()->getRenderTarget()->getLastSubmissionSerial();
        scene.frontMaterial->diffuseColor.setValue(0,1,1);
        const auto changedA=off->render(),changedB=shown->render();
        std::vector<uint8_t> changed;
        off->getSceneManager()->getRenderTarget()->readbackRGBA(changed);
        ok &= check(changedA==CoinRenderAction::SUCCESS && changedB==CoinRenderAction::SUCCESS &&
          changed.size()==expected.size() && changed!=expected &&
          off->getSceneManager()->getRenderTarget()->getLastSubmissionSerial()>offBefore &&
          shown->getSceneManager()->getRenderTarget()->getLastSubmissionSerial()>winBefore,
          "single-submit translucent style mutation on both targets");
        if(ok)ok &= compare(display,window,side,changed,samples,24+phase*32,
          "cyan mutation",true,withBackground);
        scene.frontMaterial->diffuseColor.setValue(0,1,0);
        const auto restoredA=off->render(),restoredB=shown->render();
        std::vector<uint8_t> restored;
        off->getSceneManager()->getRenderTarget()->readbackRGBA(restored);
        ok &= check(restoredA==CoinRenderAction::SUCCESS &&
          restoredB==CoinRenderAction::SUCCESS && restored==expected,
          "translucent style A/B/A restores offscreen RGBA exactly");
        if(ok)ok &= compare(display,window,side,restored,samples,24+phase*32,
          "green restored",false,withBackground);
        const uint64_t mutatedOff=off->getSceneManager()->getRenderTarget()->getLastSubmissionSerial();
        const uint64_t mutatedWindow=shown->getSceneManager()->getRenderTarget()->getLastSubmissionSerial();
        const uint64_t wantedChangedOff=caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?
          previousDeviceSerial+1:offPrevious+1;
        const uint64_t wantedChangedWindow=caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?
          wantedChangedOff+1:windowPrevious+1;
        ok &= check(mutatedOff==wantedChangedOff+(caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?2:1) &&
          mutatedWindow==wantedChangedWindow+(caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?2:1),
          "style B/A makes exactly one publication per target each time");
        offPrevious=mutatedOff;windowPrevious=mutatedWindow;
        previousDeviceSerial=mutatedWindow;
      }
      if(!ok)break;
    }
    if(!ok)break;
  }
  manager.removeSuperimposition(layer);
  if(backgroundLayer)manager.removeSuperimposition(backgroundLayer);
  manager.setSceneGraph(nullptr);
  anchorAction.setRenderTarget(nullptr);anchor.reset();anchorRoot->unref();
  XDestroyWindow(display,window);XCloseDisplay(display);return ok;
}
bool opaqueSlopeWindow(bool stateProfiles,bool atomicReadback=false) {
  CoinRenderCapabilities caps{};
  if(!check(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,
      &caps,sizeof(caps))==0 && caps.gpu_available &&
      caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN,
      "identified opaque slope window GPU"))return false;
  Display *display=XOpenDisplay(nullptr);
  if(!check(display!=nullptr,"opaque slope X display"))return false;
  Window window=XCreateSimpleWindow(display,DefaultRootWindow(display),8,8,32,32,0,0,0);
  XMapRaised(display,window);XSync(display,False);
  if(!waitXGeometry(display,window,32,"initial map")){
    XDestroyWindow(display,window);XCloseDisplay(display);return false;
  }
  CoinRenderNativeSurfaceDescriptor native{};
  native.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  native.structSize=sizeof(native);native.type=COIN_RENDER_SURFACE_XLIB;
  native.native.xlib.display=display;native.native.xlib.window=window;
  auto *scene=new SoSeparator;scene->ref();
  auto *camera=new SoOrthographicCamera;camera->position.setValue(0,0,2);
  camera->height=2;camera->nearDistance=1;camera->farDistance=3;
  scene->addChild(camera);
  auto *unlit=new SoLightModel;unlit->model=SoLightModel::BASE_COLOR;
  scene->addChild(unlit);
  auto *depthState=new SoDepthBuffer;depthState->test=TRUE;
  depthState->write=TRUE;depthState->function=SoDepthBuffer::LEQUAL;
  scene->addChild(depthState);
  auto *offset=new SoPolygonOffset;offset->styles=SoPolygonOffset::FILLED;
  offset->factor=1.f;offset->units=1.f;offset->on=FALSE;scene->addChild(offset);
  auto *material=new SoMaterial;material->diffuseColor.setValue(0,1,0);
  scene->addChild(material);
  auto *points=new SoCoordinate3;
  const SbVec3f p[]={SbVec3f(-.9f,-.9f,-.5f),SbVec3f(.9f,-.9f,.5f),
    SbVec3f(.9f,.9f,.5f),SbVec3f(-.9f,.9f,-.5f)};
  points->point.setValues(0,4,p);scene->addChild(points);
  auto *face=new SoFaceSet;face->numVertices.set1Value(0,4);scene->addChild(face);
  // Window targets expose no depth readback. A white, depth-tested overlay
  // instead turns the sloped plane's depth into a physical occlusion witness.
  // Its two bands avoid the central color stamp; write=FALSE preserves the
  // plane's offscreen depth everywhere, including underneath the witness.
  auto *witness=new SoSeparator;
  auto *witnessDepth=new SoDepthBuffer;witnessDepth->test=TRUE;
  witnessDepth->write=FALSE;witnessDepth->function=SoDepthBuffer::LEQUAL;
  witness->addChild(witnessDepth);
  auto *witnessOffset=new SoPolygonOffset;witnessOffset->on=FALSE;
  witness->addChild(witnessOffset);
  auto *white=new SoMaterial;white->diffuseColor.setValue(1,1,1);
  witness->addChild(white);
  auto *witnessPoints=new SoCoordinate3;
  const SbVec3f bands[]={SbVec3f(-.75f,-.75f,0),SbVec3f(.75f,-.75f,0),
    SbVec3f(.75f,-.55f,0),SbVec3f(-.75f,-.55f,0),
    SbVec3f(-.75f,.55f,0),SbVec3f(.75f,.55f,0),
    SbVec3f(.75f,.75f,0),SbVec3f(-.75f,.75f,0)};
  witnessPoints->point.setValues(0,8,bands);witness->addChild(witnessPoints);
  auto *witnessFaces=new SoFaceSet;
  witnessFaces->numVertices.set1Value(0,4);witnessFaces->numVertices.set1Value(1,4);
  witness->addChild(witnessFaces);scene->addChild(witness);
  bool ok=true;uint64_t previousGlobal=0;
  for(uint32_t samples:{1u,2u,4u}) {
    XResizeWindow(display,window,32,32);XSync(display,False);
    if(!waitXGeometry(display,window,32,"sample-count target setup")){
      ok=false;continue;
    }
    CoinRenderOptions options;options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
    options.sampleCount=samples;options.transparency=COIN_RENDER_TRANSPARENCY_PEELING;
    std::unique_ptr<CoinRenderTarget> off(CoinRenderTarget::createOffscreen(
      SbVec2i32(32,32),options));
    std::unique_ptr<CoinRenderTarget> shown(CoinRenderTarget::createWindow(
      native,SbVec2i32(32,32),options));
    if(!check(bool(off)&&bool(shown)&&off->getOptions().renderer==caps.renderer&&
      shown->getOptions().renderer==caps.renderer&&
      off->getOptions().sampleCount==samples&&shown->getOptions().sampleCount==samples,
      "opaque slope real GPU targets and effective sample counts")){
      ok=false;continue;
    }
    // MSAA offscreen depth readback is opt-in. The physical-window witness
    // still uses no window depth staging or unsupported depth-readback API.
    if(!check(off->setDepthReadbackEnabled(TRUE)!=FALSE&&
      off->isDepthReadbackEnabled()!=FALSE,"opaque slope offscreen depth readback opt-in")){
      ok=false;continue;
    }
    CoinRenderAction offAction(SbViewportRegion(32,32));
    CoinRenderAction windowAction(SbViewportRegion(32,32));
    offAction.setRenderTarget(off.get());windowAction.setRenderTarget(shown.get());
    uint64_t previousOff=0,previousWindow=0;
    for(int phase=0;phase<3;++phase) {
      const int side=phase==1?48:32;
      if(phase>0) {
        XResizeWindow(display,window,side,side);XSync(display,False);
        if(!waitXGeometry(display,window,side,"resize")){
          ok=false;continue;
        }
        const bool offResize=off->resize(SbVec2i32(side,side));
        const bool windowResize=shown->resize(SbVec2i32(side,side));
        const bool resized=check(offResize&&windowResize,"opaque slope paired resize");
        ok &= resized;if(!resized)continue;
        offAction.setViewportRegion(SbViewportRegion(side,side));
        windowAction.setViewportRegion(SbViewportRegion(side,side));
      }
      const SbColor4f clear(0,0,float(16+phase*24)/255.f,1);
      offAction.setBackgroundColor(clear);windowAction.setBackgroundColor(clear);
      // Adjacent states have different authored RGB, so physical readback
      // cannot mistake a previously presented frame for the current state.
      // Restoring A restores its color as well as its depth state.
      const SbColor colors[]={SbColor(0,1,0),SbColor(1,0,0),SbColor(0,1,1),
        SbColor(0,0,1),SbColor(1,1,0),SbColor(1,0,1)};
      auto authoredColor=[&](int profile,int stage){
        return colors[(phase+2*profile+stage)%6];
      };
      auto capture=[&](std::vector<uint8_t> &rgba,std::vector<float> &offDepth,
                       const char *state,const SbColor &color){
        material->diffuseColor=color;
        witnessDepth->range=depthState->range.getValue();
        const bool requested=shown->requestWindowReadbackRGBA()!=FALSE;
        offAction.apply(scene);windowAction.apply(scene);
        std::vector<uint8_t> windowRgba;std::vector<float> windowDepth;
        off->readbackRGBA(rgba);shown->readbackRGBA(windowRgba);
        off->readbackDepth(offDepth);shown->readbackDepth(windowDepth);
        const uint64_t offSerial=off->getLastSubmissionSerial();
        const uint64_t windowSerial=shown->getLastSubmissionSerial();
        const bool rust=caps.backend==COIN_RENDER_EXPERIMENTAL_RUST;
        const bool publication=requested&&offAction.getLastStatus()==CoinRenderAction::SUCCESS&&
          windowAction.getLastStatus()==CoinRenderAction::SUCCESS&&
          rgba.size()==size_t(side*side*4)&&offDepth.size()==size_t(side*side)&&
          windowRgba.size()==rgba.size()&&windowDepth.empty()&&
          offSerial==(rust?previousGlobal+1:previousOff+1)&&
          windowSerial==(rust?offSerial+1:previousWindow+1);
        const auto range=depthState->range.getValue();
        std::cout<<"msaa_peeling_opaque_slope_window samples="<<samples
          <<" phase="<<phase<<" state="<<state
          <<" backend="<<caps.backend<<" renderer="<<caps.renderer
          <<" requested="<<requested<<" rgba_sizes="<<rgba.size()<<','<<windowRgba.size()
          <<" depth_sizes="<<offDepth.size()<<','<<windowDepth.size()
          <<" authored_rgb="<<color[0]<<','<<color[1]<<','<<color[2]
          <<" range="<<range[0]<<','<<range[1]<<" offset="<<offset->on.getValue()
          <<" status="<<int(offAction.getLastStatus())<<','
          <<int(windowAction.getLastStatus())<<" serial="<<offSerial<<','
          <<windowSerial<<" error="<<offAction.getLastError().getString()<<'|'
          <<windowAction.getLastError().getString()<<'\n';
        previousGlobal=windowSerial;previousOff=offSerial;previousWindow=windowSerial;
        ok &= check(publication,"opaque slope offscreen and window publish once");
        // Invalid publication protects every buffer access, while a pixel or
        // depth mismatch never suppresses the next state/profile.
        if(!publication)return false;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        bool exact=false;
        do {
          const auto pixels=physical(display,window,side);
          exact=pixels.size()==size_t(side*side);
          if(exact)for(size_t i=0;i<pixels.size();++i)exact &=
            pixels[i].r==rgba[4*i]&&pixels[i].g==rgba[4*i+1]&&pixels[i].b==rgba[4*i+2];
          if(exact)break;
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }while(std::chrono::steady_clock::now()<deadline);
        auto geometricDepth=[&](int x,int y){
          const double slope=std::abs(double(range[1])-range[0])*(.5/.9)/side;
          auto at=[&](double sampleX){
            const double worldX=2.*(x+sampleX)/side-1.;
            return range[0]+(range[1]-range[0])*(1.-worldX*(.5/.9))/2.;
          };
          // All legal sample locations lie inside the pixel. The separate
          // calibrated oracle checks the exact minimum sample; this bound
          // also proves the write-off witness did not replace plane depth.
          const double bias=offset->on.getValue()?offset->factor.getValue()*slope:0.;
          const double rounding=2./16777215.;
          const double low=std::min(at(0.),at(1.))+bias-rounding;
          const double high=std::max(at(0.),at(1.))+bias+
            (offset->on.getValue()?offset->units.getValue()/8388608.:0.)+rounding;
          const float actual=offDepth[size_t(y*side+x)];
          return std::isfinite(actual)&&actual>=low&&actual<=high;
        };
        bool interior=true;
        for(int y=side*3/8;y<side*5/8;++y)
          for(int x=side*3/8;x<side*5/8;++x){
            const size_t i=size_t(y*side+x);
            interior &= rgba[4*i]==uint8_t(255*color[0])&&
              rgba[4*i+1]==uint8_t(255*color[1])&&rgba[4*i+2]==uint8_t(255*color[2])&&
              rgba[4*i+3]==255&&geometricDepth(x,y);
          }
        // Both bands are symmetric about y=0, so these authored probes do
        // not depend on framebuffer row orientation. Their x positions are
        // far from the crossing and raster edges, including with offset=1/1.
        bool occlusion=true;
        for(int y:{int(side*.175),int(side*.825)})
          for(int x:{side/4,side*3/4}){
            const size_t i=size_t(y*side+x);
            const bool visible=(range[1]>=range[0])?(x<side/2):(x>side/2);
            for(size_t channel=0;channel<3;++channel)
              occlusion &= rgba[4*i+channel]==
                (visible?255:uint8_t(255*color[channel]));
            occlusion &= rgba[4*i+3]==255&&geometricDepth(x,y);
          }
        std::cout<<"msaa_peeling_opaque_slope_window witness samples="<<samples
          <<" phase="<<phase<<" state="<<state<<" physical_exact="<<exact
          <<" window_rgba_exact="<<(windowRgba==rgba)<<" interior="<<interior
          <<" geometric_occlusion="<<occlusion<<'\n';
        ok &= check(exact&&windowRgba==rgba&&interior&&occlusion,
          "opaque sloped window has exact physical RGB and geometric depth occlusion");
        return true;
      };
      std::vector<uint8_t> initial,base,zero,biased,restored;
      std::vector<float> initialDepth,baseDepth,zeroDepth,biasedDepth,restoredDepth;
      depthState->range=SbVec2f(0,1);offset->on=FALSE;
      if(!stateProfiles){
        const bool published=capture(initial,initialDepth,"default A",authoredColor(0,0));
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
        if(atomicReadback&&phase==0&&published){
          // Reuse the first already verified frame at every sample count.
          // A distinguishable candidate must not replace any public image
          // or serial when acquisition fails before presentation/readback.
          material->diffuseColor=authoredColor(0,1);
          const uint64_t before=shown->getLastSubmissionSerial();
          const bool requested=shown->requestWindowReadbackRGBA()!=FALSE;
          coin_wgpu_inject_fault(101); // Existing surface-timeout test hook.
          windowAction.apply(scene);
          coin_wgpu_inject_fault(0);
          std::vector<uint8_t> retained;shown->readbackRGBA(retained);
          const uint64_t after=shown->getLastSubmissionSerial();
          const auto pixels=physical(display,window,side);
          bool unchanged=pixels.size()==size_t(side*side);
          if(unchanged)for(size_t i=0;i<pixels.size();++i)unchanged &=
            pixels[i].r==initial[4*i]&&pixels[i].g==initial[4*i+1]&&
            pixels[i].b==initial[4*i+2];
          std::cout<<"msaa_peeling_window_readback_atomic samples="<<samples
            <<" requested="<<requested<<" status="<<int(windowAction.getLastStatus())
            <<" serial="<<before<<','<<after<<" rgba_retained="<<(retained==initial)
            <<" physical_retained="<<unchanged<<" error="
            <<windowAction.getLastError().getString()<<'\n';
          ok &= check(requested&&windowAction.getLastStatus()==CoinRenderAction::NOT_READY&&
            after==before&&retained==initial&&unchanged,
            "failed requested window readback preserves physical image, RGBA and serial atomically");
          material->diffuseColor=authoredColor(0,0);
          // The next existing resize/color phase requests and publishes a
          // fresh frame with exactly one serial increment; no warm-up apply.
        }
#else
        (void)published;(void)atomicReadback;
#endif
        continue;
      }
      bool initialPublished=false;int profile=0;
      for(const auto range:{SbVec2f(0,1),SbVec2f(.2f,.8f),SbVec2f(.8f,.2f)}){
        depthState->range=range;offset->on=FALSE;
        const bool basePublished=capture(base,baseDepth,"range A",authoredColor(profile,0));
        if(range==SbVec2f(0,1)){
          initialPublished=basePublished;initial=base;initialDepth=baseDepth;
        }
        offset->on=TRUE;offset->factor=0.f;offset->units=1.e-20f;
        const bool zeroPublished=capture(zero,zeroDepth,"zero-bias path C",authoredColor(profile,1));
        if(basePublished&&zeroPublished){
          const double bound=2./16777215.;
          bool zeroGood=true;double maxAbs=0.;
          for(size_t i=0;i<baseDepth.size();++i){
            const double delta=std::abs(double(zeroDepth[i])-baseDepth[i]);
            zeroGood &= std::isfinite(delta)&&delta<=bound;
            maxAbs=std::max(maxAbs,delta);
          }
          std::cout<<"msaa_peeling_opaque_slope_window zero_bias samples="<<samples
            <<" phase="<<phase<<" range="<<range[0]<<','<<range[1]
            <<" max_abs="<<maxAbs<<" bound="<<bound<<'\n';
          if(!zeroGood)frameDifference("window zero C-A samples="+std::to_string(samples)+
            " phase="+std::to_string(phase)+" profile="+std::to_string(profile),
            side,base,baseDepth,zero,zeroDepth);
          ok &= check(zeroGood,"window zero-bias path preserves geometric depth within D24/F32 quantization");
        }
        offset->factor=1.f;offset->units=1.f;
        const bool biasedPublished=capture(biased,biasedDepth,"offset B",authoredColor(profile,2));
        if(zeroPublished&&biasedPublished){
          const double slope=std::abs(double(range[1])-range[0])*(.5/.9)/side;
          const double rounding=2./16777215.;
          const double low=slope-rounding,high=slope+1./8388608.+rounding;
          bool biasGood=true;
          for(size_t i=0;i<zero.size()/4;++i){
            const auto covered=[&](const std::vector<uint8_t> &pixels){
              return pixels[4*i]!=0||pixels[4*i+1]!=0||
                pixels[4*i+2]!=uint8_t(16+phase*24);
            };
            biasGood &= covered(zero)==covered(biased)&&zero[4*i+3]==biased[4*i+3];
          }
          for(int y=side*3/8;y<side*5/8;++y)
            for(int x=side*3/8;x<side*5/8;++x){
              const size_t i=size_t(y*side+x);
              const double delta=double(biasedDepth[i])-zeroDepth[i];
              biasGood &= delta>=low&&delta<=high;
            }
          ok &= check(biasGood,"window FILLED offset follows analytic slope and format units");
        }
        offset->on=FALSE;
        const bool restoredPublished=capture(restored,restoredDepth,"range A restored",authoredColor(profile,0));
        if(basePublished&&restoredPublished){
          if(restored!=base||restoredDepth!=baseDepth)frameDifference(
            "window offset A-B-A samples="+std::to_string(samples)+" phase="+std::to_string(phase)+
            " profile="+std::to_string(profile),side,base,baseDepth,restored,restoredDepth);
          ok &= check(restored==base&&restoredDepth==baseDepth,"window offset A/B/A exact RGBA/depth");
        }
        ++profile;
      }
      depthState->range=SbVec2f(0,1);offset->on=FALSE;
      const bool restoredPublished=capture(restored,restoredDepth,"default A restored",authoredColor(0,0));
      if(initialPublished&&restoredPublished){
        if(restored!=initial||restoredDepth!=initialDepth)frameDifference(
          "window range A-B-A samples="+std::to_string(samples)+" phase="+std::to_string(phase),
          side,initial,initialDepth,restored,restoredDepth);
        ok &= check(restored==initial&&restoredDepth==initialDepth,"window range A/B/A exact RGBA/depth");
      }
    }
    offAction.setRenderTarget(nullptr);windowAction.setRenderTarget(nullptr);
  }
  scene->unref();XDestroyWindow(display,window);XCloseDisplay(display);return ok;
}
}
int main(int argc,char **argv){
  if(argc!=2)return 2;
  const std::string mode=argv[1];
  if(mode!="--window"&&mode!="--window-background"&&
     mode!="--opaque-slope-window"&&mode!="--opaque-slope-window-state"&&
     mode!="--window-readback-atomic")return 2;
  if(!XInitThreads())return 1;
  SoDB::init();CoinRenderAction::initClass();
  if(mode=="--window-readback-atomic"){
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
    CoinRenderCapabilities caps{};
    if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,
      &caps,sizeof(caps))!=0||!caps.gpu_available||caps.backend!=COIN_RENDER_EXPERIMENTAL_RUST){
      std::cout<<"window readback atomic unavailable: requires a native wgpu window GPU\n";
      return 77;
    }
    return opaqueSlopeWindow(false,true)?0:1;
#else
    std::cout<<"window readback atomic unavailable: Rust bridge not built\n";
    return 77;
#endif
  }
  return ((mode=="--opaque-slope-window"||mode=="--opaque-slope-window-state")?
    opaqueSlopeWindow(mode=="--opaque-slope-window-state"):
    run(mode=="--window-background"))?0:1;
}
