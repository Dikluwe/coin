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
bool check(bool good,const char *label) {if(!good)std::cerr<<"msaa_layers_window FAIL "<<label<<'\n';return good;}
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
  SoMaterial *frontMaterial=static_cast<SoMaterial *>(front->getChild(3));
  Scene() {
    base->ref();front->ref();base->addChild(quad(0,SbColor(1,0,0),.85f));
    auto *annotation=new SoAnnotation;
    annotation->addChild(quad(-.8f,SbColor(1,1,0),.15f,.8f,.8f));
    base->addChild(annotation);
  }
  ~Scene(){base->unref();front->unref();}
};
bool compare(Display *display,Window window,int side,const std::vector<uint8_t> &expected,
             uint32_t samples,const char *label,bool cyan=false) {
  std::vector<RGB> observed;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  do {observed=physical(display,window,side);
    bool complete=observed.size()==size_t(side)*side;
    if(complete)for(size_t i=0;i<observed.size();++i) {
      const auto p=observed[i];
      if(std::abs(int(p.r)-int(expected[4*i]))>1 ||
         std::abs(int(p.g)-int(expected[4*i+1]))>1 ||
         std::abs(int(p.b)-int(expected[4*i+2]))>1) {
        complete=false;break;
      }
    }
    if(complete)break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while(std::chrono::steady_clock::now()<deadline);
  if(!check(observed.size()==size_t(side)*side,"physical X11 capture"))return false;
  size_t wrong=0,blend=0,red=0,yellow=0,quarter=0,half=0;
  size_t firstWrong=observed.size(),lastWrong=observed.size();int maxError=0;
  for(size_t i=0;i<observed.size();++i){
    const auto p=observed[i];const bool mismatched=(std::abs(int(p.r)-int(expected[4*i]))>1 ||
      std::abs(int(p.g)-int(expected[4*i+1]))>1 ||
      std::abs(int(p.b)-int(expected[4*i+2]))>1);
    wrong+=mismatched;
    if(mismatched){if(firstWrong==observed.size())firstWrong=i;lastWrong=i;}
    maxError=std::max(maxError,std::abs(int(p.r)-int(expected[4*i])));
    maxError=std::max(maxError,std::abs(int(p.g)-int(expected[4*i+1])));
    maxError=std::max(maxError,std::abs(int(p.b)-int(expected[4*i+2])));
    blend+=(p.r>100 && p.r<155 && p.g>100 && p.g<155 &&
      (cyan?(p.b>100 && p.b<155):p.b<20));
    red+=(p.r>200 && p.g<20);
    yellow+=(p.r>200 && p.g>200 && p.b<20);
    const int a=expected[4*i+3];quarter+=(samples==4 && std::abs(a-64)<=1);
    half+=(samples==2 && std::abs(a-128)<=1);
  }
  std::cout<<"msaa_layers_window phase="<<label<<" size="<<side<<" samples="<<samples
           <<" mismatched="<<wrong<<" first_xy="<<firstWrong%side<<','<<firstWrong/side
           <<" last_xy="<<lastWrong%side<<','<<lastWrong/side
           <<" max_rgb="<<maxError<<" red="<<red<<" blend="<<blend
           <<" yellow="<<yellow<<" quarter="<<quarter<<" half="<<half<<'\n';
  const size_t center=size_t(side/2)*side+side/2;
  const bool interior=std::abs(int(expected[4*center])-128)<=3 &&
    std::abs(int(expected[4*center+1])-128)<=3 &&
    (cyan?std::abs(int(expected[4*center+2])-128)<=3:
          expected[4*center+2]<5);
  return check(wrong==0 && red>20 && blend>20 && yellow>0 && interior &&
      (samples==4?quarter>0:half>0),
    "X11 matches independent translucent equation and layered MSAA offscreen frame");
}
bool run() {
  CoinRenderCapabilities caps{};
  if(!check(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,&caps,sizeof(caps))==0 &&
      caps.gpu_available &&
      (caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION ||
       caps.backend==COIN_RENDER_EXPERIMENTAL_RUST) &&
      caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN,"identified GPU window adapter"))return false;
  std::cout<<"msaa_layers_window renderer="<<caps.renderer<<" vendor="<<caps.vendor_id
           <<" adapter="<<caps.adapter_name<<'\n';
  Display *display=XOpenDisplay(nullptr);if(!check(display!=nullptr,"X display"))return false;
  Window window=XCreateSimpleWindow(display,DefaultRootWindow(display),8,8,32,32,0,0,0);
  XSetWindowAttributes attrs{};attrs.override_redirect=True;
  XChangeWindowAttributes(display,window,CWOverrideRedirect,&attrs);XMapRaised(display,window);XSync(display,False);
  CoinRenderNativeSurfaceDescriptor native{};native.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  native.structSize=sizeof(native);native.type=COIN_RENDER_SURFACE_XLIB;
  native.native.xlib.display=display;native.native.xlib.window=window;
  bool ok=true;Scene scene;SoRenderManager manager;
  manager.setSceneGraph(scene.base);manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
  manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
  const unsigned flags=SoRenderManager::Superimposition::AUTOREDRAW |
    SoRenderManager::Superimposition::ZBUFFERON |
    SoRenderManager::Superimposition::CLEARZBUFFER;
  auto *layer=manager.addSuperimposition(scene.front,flags);
  for(uint32_t samples:{2u,4u}) {
    CoinRenderOptions options;options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
    options.sampleCount=samples;
    std::unique_ptr<CoinRenderManagerAdapter> off(new CoinRenderManagerAdapter(manager,SbVec2i32(32,32),options));
    std::unique_ptr<CoinRenderManagerAdapter> shown(new CoinRenderManagerAdapter(manager,native,SbVec2i32(32,32),options));
    if(!check(off->getSceneManager()->getRenderTarget()->getOptions().renderer==caps.renderer &&
      shown->getSceneManager()->getRenderTarget()->getOptions().sampleCount==samples,
      "actual renderer and sample count")){ok=false;break;}
    for(int phase=0;phase<3;++phase){
      const int side=phase==1?48:32;
      if(phase>0){XResizeWindow(display,window,side,side);XSync(display,False);
        ok &= check(off->resize(SbVec2i32(side,side)) && shown->resize(SbVec2i32(side,side)),
          "paired resize");}
      manager.setViewportRegion(SbViewportRegion(side,side));
      manager.setBackgroundColor(SbColor4f(0,0,(24+phase*32)/255.f,0));
      const auto a=off->render(),b=shown->render();
      std::vector<uint8_t> expected;off->getSceneManager()->getRenderTarget()->readbackRGBA(expected);
      ok &= check(a==CoinRenderAction::SUCCESS && b==CoinRenderAction::SUCCESS &&
        expected.size()==size_t(side)*side*4,"single offscreen/window submission");
      if(ok)ok &= compare(display,window,side,expected,samples,"resize/restore");
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
        if(ok)ok &= compare(display,window,side,changed,samples,"cyan mutation",true);
        scene.frontMaterial->diffuseColor.setValue(0,1,0);
        const auto restoredA=off->render(),restoredB=shown->render();
        std::vector<uint8_t> restored;
        off->getSceneManager()->getRenderTarget()->readbackRGBA(restored);
        ok &= check(restoredA==CoinRenderAction::SUCCESS &&
          restoredB==CoinRenderAction::SUCCESS && restored==expected,
          "translucent style A/B/A restores offscreen RGBA exactly");
        if(ok)ok &= compare(display,window,side,restored,samples,"green restored");
      }
      if(!ok)break;
    }
    if(!ok)break;
  }
  manager.removeSuperimposition(layer);manager.setSceneGraph(nullptr);
  XDestroyWindow(display,window);XCloseDisplay(display);return ok;
}
}
int main(int argc,char **argv){if(argc!=2 || std::string(argv[1])!="--window")return 2;
  SoDB::init();CoinRenderAction::initClass();return run()?0:1;}
