#define COIN_RENDER_IMAGE_NORMAL_TEXGEN_FIXTURE_ONLY
#include "CoinRenderImageNormalTexgenTest.cpp"
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include "rendering/coinrender/CoinRenderRttCore.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <chrono>
#include <thread>
namespace {
class GpuProxy:public CoinRenderBackend {
public:std::unique_ptr<CoinRenderBackend>real;CoinRenderFramePlan plan;unsigned roots=0,producers=0;mutable size_t declared=0;
  explicit GpuProxy(std::unique_ptr<CoinRenderBackend>p):real(std::move(p)){}
  bool isGpuBackend()const override{return real->isGpuBackend();}bool initializesCpuDepthBuffer()const override{return real->initializesCpuDepthBuffer();}
  bool resetOnActionDetach()const override{return real->resetOnActionDetach();}bool supportsOffscreenShadows()const override{return real->supportsOffscreenShadows();}bool supportsOpaqueBump()const override{return real->supportsOpaqueBump();}
  CoinRenderBackendStatus getStatus()const override{return real->getStatus();}CoinRenderBackendStatus prepare(CoinRenderTargetP&t)override{return real->prepare(t);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&f,CoinRenderTargetP&t)override{++roots;plan=f;return real->submit(f,t);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&f,CoinRenderTargetP&t,const CoinRenderFrameReuseDecision&r)override{++roots;plan=f;return real->submit(f,t,r);}
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan&g,const CoinRenderFramePlan&f,const SbVec2i32&s)const override{declared=g.producers.size();return real->preflightRtt(g,f,s);}
  CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan&f,const SbVec2i32&s,uint64_t id,CoinRenderTargetP&t,uint64_t&token)override{++producers;return real->submitRtt(f,s,id,t,token);}
  void finishRtt(const std::vector<uint64_t>&t)override{real->finishRtt(t);}bool stagedRttRequiresPreparedParent()const override{return real->stagedRttRequiresPreparedParent();}
  bool requiresSharedRetirement()const override{return real->requiresSharedRetirement();}void retireLostReadbacks()override{real->retireLostReadbacks();}CoinRenderDeviceDomain resourceDomain()const override{return real->resourceDomain();}
  bool readbackLoad(uint64_t&a,uint64_t&b)const override{return real->readbackLoad(a,b);}void poll()override{real->poll();}const std::string&getLastError()const override{return real->getLastError();}
};
uint8_t component(unsigned long value,unsigned long mask){if(!mask)return 0;while(!(mask&1)){mask>>=1;value>>=1;}return uint8_t(((value&mask)*255+mask/2)/mask);}
bool physical(Display*d,Window w,int x,int y,const std::array<int,3>&expected){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);unsigned polls=0;
  do{++polls;XSync(d,False);XImage*image=XGetImage(d,w,x,y,1,1,AllPlanes,ZPixmap);if(image){const auto pixel=XGetPixel(image,0,0);const int rgb[]={component(pixel,image->red_mask),component(pixel,image->green_mask),component(pixel,image->blue_mask)};XDestroyImage(image);
    bool matches=true;for(int c=0;c<3;++c)matches &= std::abs(rgb[c]-expected[c])<=3;if(matches){std::cout<<"normal_texgen_physical polls="<<polls<<" rgb="<<rgb[0]<<','<<rgb[1]<<','<<rgb[2]<<'\n';return true;}}
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }while(std::chrono::steady_clock::now()<until);return check(false,"physical current frame arrives after a single apply within5s");}
struct Rig {
  CoinRenderCapabilities caps{};Display*display=nullptr;Window window=0;int extent=side;bool ready=false;
  std::unique_ptr<CoinRenderTarget>off,shown;CoinRenderAction offAction{SbViewportRegion(side,side)},windowAction{SbViewportRegion(side,side)};GpuProxy*offProxy=nullptr,*windowProxy=nullptr;uint64_t latest=0,offSerial=0,windowSerial=0;
  Rig(const CoinRenderCapabilities&c,CoinRenderOptions options):caps(c){display=XOpenDisplay(nullptr);if(!display)return;window=XCreateSimpleWindow(display,DefaultRootWindow(display),32,32,side,side,0,0,0);XSetWindowAttributes attrs{};attrs.override_redirect=True;XChangeWindowAttributes(display,window,CWOverrideRedirect,&attrs);XMapRaised(display,window);XSync(display,False);
    CoinRenderNativeSurfaceDescriptor native{};native.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;native.structSize=sizeof(native);native.type=COIN_RENDER_SURFACE_XLIB;native.native.xlib.display=display;native.native.xlib.window=window;
    options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);off.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));shown.reset(CoinRenderTarget::createWindow(native,SbVec2i32(side,side),options));
    const auto install=[](CoinRenderTarget*t,GpuProxy*&proxy){if(!t)return false;auto&state=t->getPimpl().get();const auto status=state.prepareBackend();if(status!=CoinRenderBackendStatus::SUCCESS||!state.backend||!state.backend->isGpuBackend()){std::cout<<"normal_texgen_prepare status="<<int(status)<<" error="<<t->getLastError()<<'\n';return false;}proxy=new GpuProxy(std::move(state.backend));state.backend.reset(proxy);return true;};
    if(off)off->setDepthReadbackEnabled(TRUE);ready=install(off.get(),offProxy)&&install(shown.get(),windowProxy);if(!ready)return;offAction.setRenderTarget(off.get());windowAction.setRenderTarget(shown.get());offAction.setBackgroundColor(SbColor4f(0,0,0,1));windowAction.setBackgroundColor(SbColor4f(0,0,0,1));
  }
  ~Rig(){offAction.setRenderTarget(nullptr);windowAction.setRenderTarget(nullptr);shown.reset();off.reset();if(window)XDestroyWindow(display,window);if(display)XCloseDisplay(display);}
  bool resize(int size){XResizeWindow(display,window,size,size);XSync(display,False);if(!off->resize(SbVec2i32(size,size))||!shown->resize(SbVec2i32(size,size)))return false;extent=size;offAction.setViewportRegion(SbViewportRegion(size,size));windowAction.setViewportRegion(SbViewportRegion(size,size));return true;}
  bool frame(Scene&s,bool clipped,bool readback,bool rtt,std::vector<uint8_t>&rgba,std::vector<float>&depth,double red=1.,bool cube=false,bool volume=false){
    const unsigned offCalls=offProxy->roots,windowCalls=windowProxy->roots,offChildren=offProxy->producers,windowChildren=windowProxy->producers;
    const bool direct=off->getOptions().sceneTexture==COIN_RENDER_SCENE_TEXTURE_DIRECT;const unsigned step=1+(rtt&&(caps.backend==COIN_RENDER_EXPERIMENTAL_RUST||direct)?1:0);
    offAction.apply(s.root);off->readbackRGBA(rgba);off->readbackDepth(depth);const auto nextOff=off->getLastSubmissionSerial();
    const uint64_t expectedOff=(caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?latest:offSerial)+step;
    bool ok=check(offAction.getLastStatus()==CoinRenderAction::SUCCESS&&nextOff==expectedOff&&offProxy->roots==offCalls+1,"offscreen normal texgen single root publication and correct producer serial");
    if(readback)ok &= shown->requestWindowReadbackRGBA();windowAction.apply(s.root);const auto nextWindow=shown->getLastSubmissionSerial();const uint64_t expectedWindow=(caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?nextOff:windowSerial)+step;
    ok &= check(windowAction.getLastStatus()==CoinRenderAction::SUCCESS&&nextWindow==expectedWindow&&windowProxy->roots==windowCalls+1,"window normal texgen publishes the first physical frame with single apply");
    std::cout<<"normal_texgen_gpu extent="<<extent<<" kind="<<s.kinds[s.units.back()]<<" rtt="<<rtt<<" readback="<<readback<<" off_status="<<int(offAction.getLastStatus())<<" off_error="<<offAction.getLastError().getString()<<" window_status="<<int(windowAction.getLastStatus())<<" window_error="<<windowAction.getLastError().getString()<<'\n';
    offSerial=nextOff;windowSerial=nextWindow;latest=nextWindow;if(!ok)return false;
    ok &= delivery(s,offProxy->plan,clipped,{{0,0,1}},extent)&&delivery(s,windowProxy->plan,clipped,{{0,0,1}},extent);
    if(rtt)ok &= check(offProxy->declared==1&&windowProxy->declared==1&&offProxy->producers==offChildren+(direct?1:0)&&windowProxy->producers==windowChildren+(direct?1:0),"one real RTT producer per root with no duplicate identity or hidden fallback");
    const int unit=s.units.back();std::array<int,3>expected{};
    if(volume)expected=volumeColor(s,extent);
    else if(cube){const int face=s.kinds[unit]==2?4:2;expected={{23+face*29,37+face*17,61+face*13}};}
    else{const auto raw=coordinate(s.kinds[unit],s.q[unit],clipped,{{0,0,1}},extent);const double u=.25*raw[0]/raw[3]+.125,v=.25*raw[1]/raw[3]+.125;
      const int tx=std::max(0,std::min(3,int(std::floor(4*u)))),ty=std::max(0,std::min(3,int(std::floor(4*v))));expected={{int(std::lround((20+50*tx)*red)),30+40*ty,40+15*(tx+ty)}};}
    const int x=clipped?2:extent/2,y=clipped?13*extent/16:extent/2;const size_t p=size_t(y*extent+x);std::vector<uint8_t>pixels;shown->readbackRGBA(pixels);
    bool color=rgba.size()==size_t(extent*extent*4)&&depth.size()==size_t(extent*extent)&&(readback?pixels.size()==rgba.size():pixels.empty());
    if(color){for(int c=0;c<3;++c){color &= std::abs(int(rgba[4*p+c])-expected[c])<=3;if(readback)color &= std::abs(int(pixels[4*p+c])-expected[c])<=3;}color &= std::isfinite(depth[p])&&std::abs(double(depth[p])-9./29.)<=2./16777215.;}
    ok &= check(color,"GPU attachment/readback obey analytic texgen sample/color and geometric raster depth");ok &= physical(display,window,x,y,expected);return ok;
  }
};
struct Producer {
  SoSeparator*root=new SoSeparator;SoSceneTexture2*texture=new SoSceneTexture2;
  Producer(){root->ref();texture->ref();auto*camera=new SoOrthographicCamera;camera->position.setValue(0,0,10);camera->height=16;camera->nearDistance=1;camera->farDistance=30;root->addChild(camera);auto*light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;root->addChild(light);auto*quality=new SoComplexity;quality->textureQuality=0;root->addChild(quality);
    auto*image=new SoImage;image->horAlignment=SoImage::CENTER;image->vertAlignment=SoImage::HALF;std::vector<uint8_t>pixels(16*16*3);for(int y=0;y<16;++y)for(int x=0;x<16;++x){const size_t p=size_t(y*16+x)*3;pixels[p]=uint8_t(20+50*(x/4));pixels[p+1]=uint8_t(30+40*(y/4));pixels[p+2]=uint8_t(40+15*((x+y)/4));}
    // Keep diagonal-bin color equal to the static authored4x4 gradient.
    for(int y=0;y<16;++y)for(int x=0;x<16;++x)pixels[size_t(y*16+x)*3+2]=uint8_t(40+15*(x/4+y/4));image->image.setValue(SbVec2s(16,16),3,pixels.data());root->addChild(image);
    texture->scene=root;texture->size.setValue(16,16);texture->type=SoSceneTexture2::RGBA8;texture->model=SoSceneTexture2::MODULATE;texture->transparencyFunction=SoSceneTexture2::NONE;texture->wrapS=texture->wrapT=SoSceneTexture2::CLAMP;texture->backgroundColor.setValue(0,0,0,1);
  }
  ~Producer(){texture->unref();root->unref();}
};
bool campaign(Rig&rig,bool rtt){bool ok=true;
  for(int kind=0;kind<3;++kind){Scene scene(kind,7,true,false,true);Producer producer;if(rtt)scene.root->insertChild(producer.texture,scene.root->getNumChildren()-1);
    std::vector<uint8_t>a,b,c;std::vector<float>ad,bd,cd;const bool first=rig.frame(scene,false,true,rtt,a,ad);ok &= first;if(!first)continue;
    std::vector<uint8_t>tint(16*16*3,255);for(size_t p=0;p<tint.size();p+=3)tint[p]=128;scene.image->image.setValue(SbVec2s(16,16),3,tint.data());ok &= rig.frame(scene,false,true,rtt,b,bd,128./255.);
    std::fill(tint.begin(),tint.end(),255);scene.image->image.setValue(SbVec2s(16,16),3,tint.data());const bool restored=rig.frame(scene,false,false,rtt,c,cd);ok &= restored;if(restored)ok &= check(a==c&&ad==cd,"same-runtime image primary/texgen A/B/A restores full RGBA/depth exactly");
  }
  if(!rtt){Scene clipped(1,3,true,true,true);std::vector<uint8_t>rgba;std::vector<float>depth;ok &= rig.frame(clipped,true,true,false,rgba,depth);Scene centered(0,0,false,false,true);
    ok &= check(rig.resize(96),"physical/offscreen resize96");ok &= rig.frame(centered,false,true,false,rgba,depth);ok &= check(rig.resize(side),"physical/offscreen restore64");ok &= rig.frame(centered,false,false,false,rgba,depth);}
  return ok;
}
bool campaignCube(Rig&rig){bool ok=true;for(int kind:{1,2})for(int unit:{0,3,7}){Scene scene(kind,unit,false,true,true);installCube(scene,unit);
  std::vector<uint8_t>rgba;std::vector<float>depth;ok &= rig.frame(scene,true,true,false,rgba,depth,1.,true);
}return ok;}

bool campaignVolume(Rig&rig){bool ok=true;for(int kind=0;kind<3;++kind)for(int unit:{0,3,7}){Scene scene(kind,unit,true,false,true);installVolume(scene,unit);
  std::vector<uint8_t>rgba;std::vector<float>depth;ok &= rig.frame(scene,false,true,false,rgba,depth,1.,false,true);
}return ok;}

}
int main(int argc,char**argv){bool rtt=false,cube=false,volume=false;CoinRenderOptions options;
  for(int i=1;i<argc;++i){const std::string arg=argv[i];if(arg=="--gpu")continue;if(arg=="--cube-qzero"){cube=true;continue;}if(arg=="--volume"){volume=true;continue;}if(arg=="--rtt-staged"||arg=="--rtt-direct"){rtt=true;options.sceneTexture=arg=="--rtt-direct"?COIN_RENDER_SCENE_TEXTURE_DIRECT:COIN_RENDER_SCENE_TEXTURE_STAGED;continue;}if(arg=="--portable-sampling"){options.textureSamplingPolicy=COIN_RENDER_SAMPLING_PORTABLE;continue;}return 2;}
  if((cube&&rtt)||(volume&&rtt)||(cube&&volume))return 2;if(!XInitThreads())return 1;SoDB::init();CoinRenderAction::initClass();CoinRenderCapabilities caps{};if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,&caps,sizeof(caps))!=0||!caps.gpu_available)return 77;
  std::cout<<"normal_texgen_caps backend="<<caps.backend<<" renderer="<<caps.renderer<<" vendor="<<caps.vendor_id<<" adapter="<<caps.adapter_name<<'\n';Rig rig(caps,options);if(!rig.ready)return 1;return (cube?campaignCube(rig):volume?campaignVolume(rig):campaign(rig,rtt))?0:1;}
