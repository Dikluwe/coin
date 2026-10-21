#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
namespace {
struct RGB {uint8_t r,g,b;};
bool check(bool good,const char *label) {if(!good)std::cerr<<"msaa_weighted_window FAIL "<<label<<'\n';return good;}
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
    // These two draws belong to the base pass, where weighted OIT executes.
    // Their different alpha/depth values distinguish it from OBJECT blending.
    base->addChild(quad(-.5f,SbColor(0,1,1),1.2f,0,0,.5f));
    base->addChild(quad(-.75f,SbColor(1,0,1),1.2f,0,0,.75f));
    auto *annotation=new SoAnnotation;
    annotation->addChild(quad(-.8f,SbColor(1,1,0),.15f,.8f,.8f));
    base->addChild(annotation);
  }
  ~Scene(){base->unref();front->unref();background->unref();}
};
float weight(float alpha,float depth){
  return std::min(8.f,alpha*8.f+.01f)*
    std::min(16.f,std::pow(1.f-depth,3.f)*16.f+.1f);
}
bool compare(Display *display,Window window,int side,const std::vector<uint8_t> &expected,
             uint32_t samples,int backgroundBlueByte,const char *label,bool cyan=false,
             bool backgroundLayer=false) {
  std::vector<RGB> observed;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  do {observed=physical(display,window,side);
    bool complete=observed.size()==size_t(side)*side;
    if(complete)for(size_t i=0;i<observed.size();++i) {
      const auto p=observed[i];
      if(p.r!=expected[4*i] || p.g!=expected[4*i+1] || p.b!=expected[4*i+2]) {
        complete=false;break;
      }
    }
    if(complete)break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while(std::chrono::steady_clock::now()<deadline);
  if(!check(observed.size()==size_t(side)*side,"physical X11 capture"))return false;
  size_t wrong=0,blend=0,red=0,yellow=0,quarter=0,half=0,threeQuarter=0;
  const float opacity=1.f-(1.f-.5f)*(1.f-.25f);
  // The far magenta depth makes weighted OIT visibly unlike OBJECT order:
  // weighted red is about .054, while ordinary cyan-then-magenta is .25.
  const float wa=.5f*weight(.5f,.75f),wb=.25f*weight(.25f,.875f);
  const float outsideR=opacity*wb/(wa+wb),outsideG=opacity*wa/(wa+wb);
  const float outsideB=opacity+(1.f-opacity)*float(backgroundBlueByte)/255.f;
  size_t firstWrong=observed.size(),lastWrong=observed.size();int maxError=0;
  for(size_t i=0;i<observed.size();++i){
    const auto p=observed[i];const bool mismatched=(p.r!=expected[4*i] || p.g!=expected[4*i+1] || p.b!=expected[4*i+2]);
    wrong+=mismatched;
    if(mismatched){if(firstWrong==observed.size())firstWrong=i;lastWrong=i;}
    maxError=std::max(maxError,std::abs(int(p.r)-int(expected[4*i])));
    maxError=std::max(maxError,std::abs(int(p.g)-int(expected[4*i+1])));
    maxError=std::max(maxError,std::abs(int(p.b)-int(expected[4*i+2])));
    blend+=(p.r>100 && p.r<155 && p.g>100 && p.g<155 &&
      (cyan?(p.b>100 && p.b<155):p.b<20));
    red+=(p.r>200 && p.g<20);
    yellow+=(p.r>200 && p.g>200 && p.b<20);
    const int x=int(i%side),y=int(i/side);
    // The bottom and left strips miss the translucent foreground and the
    // annotation.  There the base has weighted alpha .625 in uncovered
    // samples and opaque alpha 1 in k covered samples of the red quad.
    if(x<int(.15f*side)||y<int(.15f*side)) {
      const int a=expected[4*i+3];
      auto fraction=[&](int k){
        const float covered=float(k)/float(samples);
        if(!backgroundLayer)return std::abs(a-int(std::lround(255.f*(opacity+
          (1.f-opacity)*covered))))<=1;
        const float r=covered+(1.f-covered)*outsideR;
        const float g=(1.f-covered)*outsideG;
        const float b=(1.f-covered)*outsideB;
        return std::abs(int(expected[4*i])-int(std::lround(255.f*r)))<=3 &&
          std::abs(int(expected[4*i+1])-int(std::lround(255.f*g)))<=3 &&
          std::abs(int(expected[4*i+2])-int(std::lround(255.f*b)))<=3;
      };
      quarter+=(samples==4&&fraction(1));
      half+=(samples==2&&fraction(1));
      threeQuarter+=(samples==4&&fraction(3));
    }
  }
  std::cout<<"msaa_weighted_window phase="<<label<<" size="<<side<<" samples="<<samples
           <<" mismatched="<<wrong<<" first_xy="<<firstWrong%side<<','<<firstWrong/side
           <<" last_xy="<<lastWrong%side<<','<<lastWrong/side
           <<" max_rgb="<<maxError<<" red="<<red<<" blend="<<blend
           <<" yellow="<<yellow<<" quarter="<<quarter<<" half="<<half
           <<" three_quarter="<<threeQuarter<<'\n';
  const size_t center=size_t(side/2)*side+side/2;
  const size_t weightedPixel=size_t(side/2)*side;
  const float backgroundBlue=float(backgroundBlueByte)/255.f;
  const float wantR=opacity*wb/(wa+wb),wantG=opacity*wa/(wa+wb);
  const float wantB=opacity+(1.f-opacity)*backgroundBlue;
  const bool weightedBase=
    std::abs(int(expected[4*weightedPixel])-int(std::lround(wantR*255)))<=3 &&
    std::abs(int(expected[4*weightedPixel+1])-int(std::lround(wantG*255)))<=3 &&
    std::abs(int(expected[4*weightedPixel+2])-int(std::lround(wantB*255)))<=3 &&
    std::abs(int(expected[4*weightedPixel+3])-int(std::lround(
      (backgroundLayer?1.f:opacity)*255)))<=1;
  if(!weightedBase)std::cerr<<"msaa_weighted_window FAIL base_weighted rgba="
    <<int(expected[4*weightedPixel])<<','<<int(expected[4*weightedPixel+1])<<','
    <<int(expected[4*weightedPixel+2])<<','<<int(expected[4*weightedPixel+3])
    <<" expected_rgb="<<wantR*255<<','<<wantG*255<<','<<wantB*255<<'\n';
  const bool interior=std::abs(int(expected[4*center])-128)<=3 &&
    std::abs(int(expected[4*center+1])-128)<=3 &&
    (cyan?std::abs(int(expected[4*center+2])-128)<=3:
          expected[4*center+2]<5);
  return check(wrong==0 && weightedBase && red>20 && blend>20 && yellow>0 && interior &&
      (samples==4?quarter>0&&threeQuarter>0:samples==2?half>0:true),
    "X11 matches independent translucent equation and layered MSAA offscreen frame");
}
bool run(bool withBackground) {
  CoinRenderCapabilities caps{};
  if(!check(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,&caps,sizeof(caps))==0 &&
      caps.gpu_available &&
      (caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION ||
       caps.backend==COIN_RENDER_EXPERIMENTAL_RUST) &&
      caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN,"identified GPU window adapter"))return false;
  std::cout<<"msaa_weighted_window renderer="<<caps.renderer<<" vendor="<<caps.vendor_id
           <<" adapter="<<caps.adapter_name<<'\n';
  Display *display=XOpenDisplay(nullptr);if(!check(display!=nullptr,"X display"))return false;
  Window window=XCreateSimpleWindow(display,DefaultRootWindow(display),8,8,32,32,0,0,0);
  XSetWindowAttributes attrs{};attrs.override_redirect=True;
  XChangeWindowAttributes(display,window,CWOverrideRedirect,&attrs);XMapRaised(display,window);XSync(display,False);
  CoinRenderNativeSurfaceDescriptor native{};native.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  native.structSize=sizeof(native);native.type=COIN_RENDER_SURFACE_XLIB;
  native.native.xlib.display=display;native.native.xlib.window=window;
  bool ok=true;Scene scene(withBackground);SoRenderManager manager;
  // Keep a 1x target alive while the 1/2/4/2/1 window and offscreen
  // targets are created. This detects cache keys that omit sampleCount.
  auto *anchorRoot=quad(0,SbColor(1,0,0),.5f);anchorRoot->ref();
  CoinRenderOptions anchorOptions;
  anchorOptions.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  anchorOptions.sampleCount=1;
  anchorOptions.transparency=COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;
  std::unique_ptr<CoinRenderTarget> anchor(
    CoinRenderTarget::createOffscreen(SbVec2i32(32,32),anchorOptions));
  CoinRenderAction anchorAction(SbViewportRegion(32,32));
  if(!check(bool(anchor),"live 1x target")){anchorRoot->unref();XDestroyWindow(display,window);XCloseDisplay(display);return false;}
  anchorAction.setRenderTarget(anchor.get());anchorAction.apply(anchorRoot);
  uint64_t previousDeviceSerial=anchor->getLastSubmissionSerial();
  ok &= check(anchorAction.getLastStatus()==CoinRenderAction::SUCCESS && previousDeviceSerial>0,
    "live 1x runtime anchor publishes before weighted window");
  manager.setSceneGraph(scene.base);manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::SORTED_OBJECT_BLEND);
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
    options.sampleCount=samples;options.transparency=COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;
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
      std::cout<<"msaa_weighted_window samples="<<samples<<" phase="<<phase
               <<" off_serial="<<offSerial<<" window_serial="<<winSerial
               <<" expected="<<expectedOff<<','<<expectedWindow<<'\n';
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
}
int main(int argc,char **argv){
  if(argc!=2)return 2;
  const std::string mode=argv[1];
  if(mode!="--window"&&mode!="--window-background")return 2;
  if(!XInitThreads())return 1;
  SoDB::init();CoinRenderAction::initClass();
  return run(mode=="--window-background")?0:1;
}
