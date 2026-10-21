#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#define COIN_RENDER_SHADOW_MULTITEXTURE_FIXTURE_ONLY
#include "CoinRenderShadowMultitextureTest.cpp"
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoPolygonOffset.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <chrono>
#include <thread>
namespace {
unsigned windowSamples=1;
bool customDepthMode=false,customOffsetEnabled=false;
SbVec2f customDepthRange(.2f,.8f);
void customizeDepth(Scene&s){
  if(!customDepthMode)return;
  auto*depth=new SoDepthBuffer;depth->range=customDepthRange;
  s.receiver->insertChild(depth,0);
  auto*offset=new SoPolygonOffset;offset->styles=SoPolygonOffset::FILLED;
  offset->on=customOffsetEnabled;offset->factor=1.f;offset->units=1.f;
  s.receiver->insertChild(offset,1);
}
uint8_t xchannel(unsigned long value,unsigned long mask){
  if(!mask)return 0;while(!(mask&1)){value>>=1;mask>>=1;}
  return uint8_t(((value&mask)*255+mask/2)/mask);
}
std::vector<uint8_t> ximage(Display*d,Window w,int extent){
  XSync(d,False);XImage*raw=XGetImage(d,w,0,0,extent,extent,AllPlanes,ZPixmap);
  if(!raw)return {};std::vector<uint8_t> bytes(size_t(extent)*extent*4,255);
  for(int y=0;y<extent;++y)for(int x=0;x<extent;++x){const auto v=XGetPixel(raw,x,y);const size_t i=size_t(y*extent+x)*4;
    bytes[i]=xchannel(v,raw->red_mask);bytes[i+1]=xchannel(v,raw->green_mask);bytes[i+2]=xchannel(v,raw->blue_mask);}
  XDestroyImage(raw);return bytes;
}
// Native raster edges can differ between an attachment and a WSI image.
// Sample every locally smooth opaque interior pixel, independently of vendor.
bool equivalent(const std::vector<uint8_t>&reference,const std::vector<uint8_t>&actual,int extent,bool report){
  if(reference.size()!=size_t(extent)*extent*4||actual.size()!=reference.size())return false;
  unsigned tested=0,bad=0;int maximum=0;
  for(int y=2;y<extent-2;++y)for(int x=2;x<extent-2;++x){const size_t i=size_t(y*extent+x)*4;bool interior=true;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const size_t j=size_t((y+dy)*extent+x+dx)*4;
      interior &= reference[j+3]==255;
      for(int c=0;c<3;++c)interior &= std::abs(int(reference[i+c])-int(reference[j+c]))<=8;}
    if(!interior)continue;++tested;bad+=(actual[i+3]!=255);
    for(int c=0;c<3;++c){const int delta=std::abs(int(reference[i+c])-int(actual[i+c]));maximum=std::max(maximum,delta);bad+=(delta>3);}
  }
  if(report)std::cout<<"shadow_multitexture_window interior="<<tested<<" wrong_channels="<<bad<<" max_delta="<<maximum<<'\n';
  return tested>unsigned(extent*extent/8)&&bad==0;
}
bool presented(Display*d,Window w,int extent,const std::vector<uint8_t>&expected,std::vector<uint8_t>&seen){
  const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  do{seen=ximage(d,w,extent);if(equivalent(expected,seen,extent,false))return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }while(std::chrono::steady_clock::now()<until);
  equivalent(expected,seen,extent,true);return check(false,"physical X11 image matches current single-apply offscreen control");
}
class CountedProxy:public Proxy {
public:unsigned submits=0;
  explicit CountedProxy(std::unique_ptr<CoinRenderBackend>p):Proxy(std::move(p)){}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&f,CoinRenderTargetP&t)override{++submits;return Proxy::submit(f,t);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&f,CoinRenderTargetP&t,const CoinRenderFrameReuseDecision&r)override{++submits;return Proxy::submit(f,t,r);}
};
struct WindowPair {
  Display*d=nullptr;bool ownsDisplay=true;Window window=0;int extent=side;CoinRenderCapabilities caps;
  std::unique_ptr<CoinRenderTarget> off,shown;
  CoinRenderAction offAction{SbViewportRegion(side,side)},windowAction{SbViewportRegion(side,side)};
  CountedProxy*offProxy=nullptr,*windowProxy=nullptr;std::vector<uint8_t>lastShown,lastReadback;
  uint64_t latest=0,publishedOff=0,publishedWindow=0;bool ready=false;
  explicit WindowPair(const CoinRenderCapabilities&c,uint64_t previousDeviceSerial=0,Display*borrowedDisplay=nullptr,CoinRenderTransparencyMode transparency=COIN_RENDER_TRANSPARENCY_COIN,CoinRenderSceneTextureMode route=COIN_RENDER_SCENE_TEXTURE_STAGED):caps(c),latest(previousDeviceSerial){
    ownsDisplay=borrowedDisplay==nullptr;d=borrowedDisplay?borrowedDisplay:XOpenDisplay(nullptr);if(!d)return;
    window=XCreateSimpleWindow(d,DefaultRootWindow(d),16,16,side,side,0,0,0);
    XSetWindowAttributes a{};a.override_redirect=True;XChangeWindowAttributes(d,window,CWOverrideRedirect,&a);
    XMapRaised(d,window);XSync(d,False);
    CoinRenderNativeSurfaceDescriptor n{};n.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;n.structSize=sizeof(n);
    n.type=COIN_RENDER_SURFACE_XLIB;n.native.xlib.display=d;n.native.xlib.window=window;
    auto o=samplingTestOptions();o.renderer=static_cast<CoinRenderRenderer>(caps.renderer);o.sampleCount=windowSamples;o.transparency=transparency;o.sceneTexture=route;
    off.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),o));shown.reset(CoinRenderTarget::createWindow(n,SbVec2i32(side,side),o));
    const auto install=[](CoinRenderTarget*t,CountedProxy*&p,const char*role){
      if(!t){std::cout<<"shadow_multitexture_window install role="<<role<<" target=missing prepare_status=not_attempted\n";return false;}
      auto &s=t->getPimpl().get();const auto status=s.prepareBackend();
      const bool gpu=status==CoinRenderBackendStatus::SUCCESS&&s.backend&&s.backend->isGpuBackend();
      if(status!=CoinRenderBackendStatus::SUCCESS||!s.backend||!gpu){
        std::cout<<"shadow_multitexture_window install role="<<role<<" prepare_status="<<int(status)<<" has_backend="<<bool(s.backend)
          <<" gpu="<<gpu<<" target_error="<<t->getLastError()<<" backend_error="<<(s.backend?s.backend->getLastError():std::string("<missing>"))<<'\n';return false;}
      p=new CountedProxy(std::move(s.backend));s.backend.reset(p);return true;};
    ready=install(off.get(),offProxy,"offscreen")&&install(shown.get(),windowProxy,"window");
    if(!ready)return;off->setDepthReadbackEnabled(TRUE);offAction.setRenderTarget(off.get());windowAction.setRenderTarget(shown.get());
    offAction.setBackgroundColor(SbColor4f(0,0,0,0));windowAction.setBackgroundColor(SbColor4f(0,0,0,0));
    ready=check(off->getOptions().sampleCount==windowSamples&&shown->getOptions().sampleCount==windowSamples&&
      shown->getOptions().renderer==caps.renderer&&shown->getOptions().textureSamplingPolicy==samplingTestPolicy,
      "physical targets retain requested sample count, API and sampling policy");
  }
  ~WindowPair(){offAction.setRenderTarget(nullptr);windowAction.setRenderTarget(nullptr);shown.reset();off.reset();
    if(window)XDestroyWindow(d,window);if(d&&ownsDisplay)XCloseDisplay(d);}
  bool resize(int size){XResizeWindow(d,window,size,size);XSync(d,False);XWindowAttributes a{};XGetWindowAttributes(d,window,&a);
    if(!check(a.width==size&&a.height==size&&off->resize(SbVec2i32(size,size))&&shown->resize(SbVec2i32(size,size)),"paired physical resize"))return false;
    extent=size;offAction.setViewportRegion(SbViewportRegion(size,size));windowAction.setViewportRegion(SbViewportRegion(size,size));return true;}
  bool frame(Scene&s,Frame&control,const std::string&phase,bool readback=false,bool inspect=false,unsigned rttProducers=0,SoNode*renderRoot=nullptr){
    if(!ready)return check(false,"prepared physical/offscreen pair");
    const uint64_t oldOff=off->getLastSubmissionSerial(),oldWindow=shown->getLastSubmissionSerial();
    const unsigned offCount=offProxy->submits,windowCount=windowProxy->submits;
    offAction.apply(renderRoot?renderRoot:s.root);off->readbackRGBA(control.rgba);off->readbackDepth(control.depth);control.serial=off->getLastSubmissionSerial();
    const unsigned step=1+((caps.backend==COIN_RENDER_EXPERIMENTAL_RUST||off->getOptions().sceneTexture==COIN_RENDER_SCENE_TEXTURE_DIRECT)?rttProducers:0);
    const uint64_t expectedOff=caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?latest+step:publishedOff+step;
    bool ok=check(offAction.getLastStatus()==CoinRenderAction::SUCCESS&&control.serial==expectedOff&&offProxy->submits==offCount+1&&
      control.rgba.size()==size_t(extent)*extent*4&&control.depth.size()==size_t(extent)*extent,
      "single offscreen control publication");
    const bool requested=!readback||shown->requestWindowReadbackRGBA();
    windowAction.apply(renderRoot?renderRoot:s.root);const uint64_t next=shown->getLastSubmissionSerial();
    const uint64_t expectedWindow=caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?control.serial+step:publishedWindow+step;
    std::vector<uint8_t>pixels;shown->readbackRGBA(pixels);
    std::cout<<"shadow_multitexture_window phase="<<phase<<" readback="<<readback<<" extent="<<extent
      <<" samples="<<windowSamples<<" status="<<int(windowAction.getLastStatus())<<" serial="<<oldWindow<<','<<next<<" submissions="<<windowCount<<','<<windowProxy->submits
      <<" error="<<windowAction.getLastError().getString()<<'\n';
    ok &= check(requested&&windowAction.getLastStatus()==CoinRenderAction::SUCCESS&&next==expectedWindow&&windowProxy->submits==windowCount+1,
      "one apply publishes exactly one current window frame");
    ok &= check(readback?equivalent(control.rgba,pixels,extent,true):pixels.empty(),
      readback?"explicit current RGBA readback has correct WSI channels":"default GPU-only window allocates no readable frame");
    if(readback&&requested)lastReadback=pixels;else lastReadback.clear();
    if(ok)ok &= presented(d,window,extent,control.rgba,lastShown);
    if(inspect&&extent==side){
      if(customDepthMode){
        CoinWgpuFfiFrame ffi;std::string diagnostic;
        const bool prepared=ffi.prepare(windowProxy->plan,extent,extent,diagnostic,false);
        ok &= check(prepared,"custom depth Core/FFI delivery prepared: "+diagnostic);
        unsigned receivers=0;bool effectiveOffset=false;
        if(prepared)for(const auto&draw:windowProxy->plan.draws){
          if(draw.shadowLightSlot||draw.sourceNodeId!=s.mesh->getNodeId())continue;
          if(draw.renderStateSlot>=windowProxy->plan.renderStates.size()||draw.renderStateSlot>=ffi.getView().state_count){ok=false;continue;}
          const auto&state=windowProxy->plan.renderStates[draw.renderStateSlot];
          const auto&packed=ffi.getView().states[draw.renderStateSlot];++receivers;
          ok &= check(state.depthTest&&state.depthWrite&&state.depthRange[0]==customDepthRange[0]&&state.depthRange[1]==customDepthRange[1]&&
            packed.depth_range[0]==customDepthRange[0]&&packed.depth_range[1]==customDepthRange[1]&&
            state.polygonOffsetEnabled==customOffsetEnabled&&bool(packed.polygon_offset_enabled)==customOffsetEnabled&&
            (!customOffsetEnabled||((state.polygonOffsetFactor==1.f||state.polygonOffsetFactor==0.f)&&
              std::isfinite(state.polygonOffsetSlopeBias)&&state.polygonOffsetSlopeBias>=0.f&&state.polygonOffsetUnits==1.f&&
              packed.polygon_offset_factor==state.polygonOffsetFactor&&packed.polygon_offset_units==state.polygonOffsetUnits&&
              packed.polygon_offset_slope_bias==state.polygonOffsetSlopeBias)),
            "effective depth range/offset reaches every receiver in Core and GPU connector");
          effectiveOffset |= state.polygonOffsetFactor==1.f||state.polygonOffsetSlopeBias>0.f;
        }
        ok &= check(receivers>0&&(!customOffsetEnabled||effectiveOffset),
          "custom depth delivery includes the actual receiver and an effective raw or resolved slope factor");
      }
      ok &= payload(s,windowProxy->plan);
      ok &= check(windowProxy->plan.shadowGroups.size()==1&&
        windowProxy->plan.shadowGroups[0].quality==s.group->quality.getValue(),
        "authored Gouraud/Phong shadow quality survives window capture");
      for(const auto &light:windowProxy->plan.shadowLights)
        ok &= check(light.enabled&&light.shadowEligible&&light.mapSceneCaptured&&
          light.type==(s.directional?CoinRenderLightType::DIRECTIONAL:CoinRenderLightType::SPOT),
          "every authored physical shadow map retains type, eligibility and captured scene");
    }
    latest=next;publishedOff=control.serial;publishedWindow=next;return ok;
  }
  bool refuse(Scene&s,const std::string&phase,bool readback,bool permitCpuPreflight=false){
    bool offscreenAtomic=true;
    if(customDepthMode){
      std::vector<uint8_t>before,after;std::vector<float>beforeDepth,afterDepth;
      off->readbackRGBA(before);off->readbackDepth(beforeDepth);
      const uint64_t serial=off->getLastSubmissionSerial();
      const unsigned submits=offProxy->submits,producers=offProxy->producers;
      offAction.setTransparencyType(windowAction.getTransparencyType());offAction.apply(s.root);
      off->readbackRGBA(after);off->readbackDepth(afterDepth);
      offscreenAtomic=check(offAction.getLastStatus()!=CoinRenderAction::SUCCESS&&off->getLastSubmissionSerial()==serial&&
        (offProxy->submits==submits||(permitCpuPreflight&&offProxy->submits==submits+1))&&offProxy->producers==producers&&
        before==after&&beforeDepth==afterDepth,"custom depth offscreen rejection preserves RGBA/depth/serial before producers");
      offAction.setTransparencyType(CoinRenderAction::NONE);
    }
    const uint64_t serial=shown->getLastSubmissionSerial();const unsigned submits=windowProxy->submits,producers=windowProxy->producers;
    const auto previous=lastShown,buffer=lastReadback;
    if(readback&&!shown->requestWindowReadbackRGBA())return check(false,"request before invalid frame");
    windowAction.apply(s.root);std::vector<uint8_t>pixels;shown->readbackRGBA(pixels);
    bool ok=check(windowAction.getLastStatus()!=CoinRenderAction::SUCCESS&&shown->getLastSubmissionSerial()==serial&&
      (windowProxy->submits==submits||(permitCpuPreflight&&windowProxy->submits==submits+1))&&
      windowProxy->producers==producers&&pixels==buffer,"invalid scene fails atomically before backend publication: "+phase);
    std::vector<uint8_t>physical;ok &= presented(d,window,extent,previous,physical);
    ok &= check(physical==previous,"invalid submission preserves entire physical image");return ok&&offscreenAtomic;
  }
};
// A solid REPLACE receiver removes lighting/texture gradients. A sloped outer
// edge then exposes actual fractional sample coverage in the physical image.
// Count and quantize levels locally; sample locations need not agree with any
// other adapter, API, CoinGL, or even the offscreen attachment.
bool coverageWitness(WindowPair&h,bool readback=false){
  Scene s(8,8,1.f,false);customizeDepth(s);s.white();const int unit=s.units.back();
  s.image(unit,{{240,120,60}});s.images[unit]->model=SoTexture2::REPLACE;
  for(int x=0;x<3;++x){auto p=s.positions->point[6+x];p[1]=1.31f+.22f*x;s.positions->point.set1Value(6+x,p);}
  Frame frame;if(!h.frame(s,frame,"constant-color sloped MSAA witness",readback))return false;
  if(!check(frame.rgba.size()==size_t(side)*side*4,"complete resolved coverage witness"))return false;
  unsigned attachmentPartial=0,physicalPartial=0,readbackPartial=0;bool valid=true;
  const auto alphaLevels=[&](const std::vector<uint8_t>&rgba,unsigned&partial){
    if(rgba.size()!=size_t(side)*side*4)return false;
    bool ok=true;for(size_t p=3;p<rgba.size();p+=4){const int a=rgba[p];if(a==0||a==255)continue;++partial;
      const int k=int(std::lround(double(a)*windowSamples/255.));
      ok &= k>0&&k<int(windowSamples)&&std::abs(a-int(std::lround(255.*k/windowSamples)))<=2;
    }return ok;
  };
  valid &= alphaLevels(frame.rgba,attachmentPartial);
  const auto rgbLevels=[&](const std::vector<uint8_t>&rgba,unsigned&partial){
    if(rgba.size()!=size_t(side)*side*4)return false;
    bool ok=true;for(size_t p=0;p<rgba.size();p+=4){const int r=rgba[p];if(r==0||r==240)continue;
      ++partial;const int k=int(std::lround(double(r)*windowSamples/240.));
      ok &= k>0&&k<int(windowSamples);
      for(int c=0;c<3;++c){const int solid[]={240,120,60};
        ok &= std::abs(int(rgba[p+c])-int(std::lround(double(solid[c])*k/windowSamples)))<=3;}
    }return ok;
  };
  valid &= rgbLevels(h.lastShown,physicalPartial);
  // Opaque WSI alpha is not the attachment coverage channel. Inspect the
  // resolved RGB levels in explicit readback, as in the physical image.
  if(readback)valid &= rgbLevels(h.lastReadback,readbackPartial);
  std::cout<<"shadow_multitexture_window coverage samples="<<windowSamples
    <<" attachment_partial="<<attachmentPartial<<" physical_partial="<<physicalPartial
    <<" readback_partial="<<readbackPartial<<'\n';
  return check(valid&&(windowSamples==1?(attachmentPartial==0&&physicalPartial==0):
    (attachmentPartial>4&&physicalPartial>4&&(!readback||readbackPartial>4))),
    "effective attachment and physical window MSAA: fractional k/N levels, no silent 1x fallback");
}
bool customPlaneStates(WindowPair&h,Scene&s,SoDepthBuffer*state,SoPolygonOffset*offset,
  double firstLo,double firstHi,double lastLo,double lastHi,bool operators){
  constexpr double formatBound=2./16777215.;bool ok=true;const int extent=h.extent;
  const auto geometric=[&](const Frame&f,const SbVec2f&range){
    if(!check(f.depth.size()==size_t(extent)*extent,"complete custom plane depth"))return false;
    const double sampleLo=range[1]>=range[0]?lastLo:firstLo;
    const double sampleHi=range[1]>=range[0]?lastHi:firstHi;
    unsigned bad=0,checked=0;
    for(int y=extent/4;y<3*extent/4;++y)for(int x=extent/4;x<3*extent/4;++x){
      const auto predicted=[&](double sx){const double worldX=4.*(x+sx)/extent-2.;return range[0]+(range[1]-range[0])*(6.-.4*worldX)/19.;};
      const double a=predicted(sampleLo),b=predicted(sampleHi);
      const double low=std::min(a,b)-formatBound,high=std::max(a,b)+formatBound;
      const double actual=f.depth[size_t(y*extent+x)];
      if((!std::isfinite(actual)||actual<low||actual>high)&&bad++<4)
        std::cerr<<"custom_depth_plane xy="<<x<<','<<y<<" actual="<<actual<<" expected="<<low<<','<<high<<'\n';
      ++checked;
    }
    std::cout<<"custom_depth_plane range="<<range[0]<<','<<range[1]<<" extent="<<extent<<" checked="<<checked<<" bad="<<bad<<'\n';
    return check(checked>1000&&bad==0,"custom range depth is calibrated geometric sample minimum");
  };
  for(const auto range:{SbVec2f(.2f,.8f),SbVec2f(.8f,.2f)}){
    state->range=range;offset->on=FALSE;Frame a,c,b,back;
    if(!h.frame(s,a,"custom range geometric A")){ok=false;continue;}ok &= geometric(a,range);
    offset->on=TRUE;offset->factor=0.f;offset->units=1.e-20f;
    if(!h.frame(s,c,"custom range zero-bias depth C")){ok=false;continue;}ok &= geometric(c,range);
    bool zero=a.rgba==c.rgba;for(size_t p=0;p<a.depth.size();++p)zero &= std::abs(double(a.depth[p])-c.depth[p])<=formatBound;
    ok &= check(zero,"zero-bias custom path preserves geometry within existing D24/F32 quantization");
    offset->factor=1.f;offset->units=1.f;
    if(!h.frame(s,b,"custom range effective factor and units B")){ok=false;continue;}
    const double slope=std::abs(double(range[1])-range[0])*.4*4./(19.*extent);
    bool bias=b.rgba==c.rgba;double observedMin=1.,observedMax=-1.;
    for(int y=extent/4;y<3*extent/4;++y)for(int x=extent/4;x<3*extent/4;++x){
      const size_t p=size_t(y*extent+x);const double delta=double(b.depth[p])-c.depth[p];
      bias &= delta>=slope-formatBound&&delta<=slope+1./8388608.+formatBound;
      observedMin=std::min(observedMin,delta);observedMax=std::max(observedMax,delta);
    }
    std::cout<<"custom_depth_bias range="<<range[0]<<','<<range[1]<<" expected="<<slope
      <<" observed="<<observedMin<<','<<observedMax<<'\n';
    ok &= check(bias,"effective polygon offset obeys independent slope and format-unit bounds");
    offset->on=FALSE;
    if(operators){
      auto*overlay=new SoSeparator;
      for(int i=0;i<s.receiver->getNumChildren()-1;++i)overlay->addChild(s.receiver->getChild(i));
      auto*comparison=new SoDepthBuffer;comparison->range=range;overlay->addChild(comparison);
      auto*green=new SoTexture2;const uint8_t pixel[]={0,255,0};green->image.setValue(SbVec2s(1,1),3,pixel);
      green->model=SoTexture2::REPLACE;overlay->addChild(green);overlay->addChild(s.mesh);s.group->addChild(overlay);
      for(int fn:{SoDepthBuffer::NEVER,SoDepthBuffer::ALWAYS,SoDepthBuffer::LESS,SoDepthBuffer::LEQUAL,
        SoDepthBuffer::EQUAL,SoDepthBuffer::GEQUAL,SoDepthBuffer::GREATER,SoDepthBuffer::NOTEQUAL}){
        comparison->function=fn;Frame f;const bool published=h.frame(s,f,"custom range coplanar comparator "+std::to_string(fn));ok &= published;
        if(published){const bool pass=fn==SoDepthBuffer::ALWAYS||fn==SoDepthBuffer::LEQUAL||fn==SoDepthBuffer::EQUAL||fn==SoDepthBuffer::GEQUAL;
          bool colors=true;unsigned wrong=0;size_t first=SIZE_MAX;const int expected[]={pass?0:240,pass?255:120,pass?0:60};
          for(int y=extent/4;y<3*extent/4;++y)for(int x=extent/4;x<3*extent/4;++x){const size_t p=size_t(y*extent+x)*4;
            bool match=true;for(int channel=0;channel<3;++channel)match &= f.rgba[p+channel]==expected[channel];
            if(!match){++wrong;if(first==SIZE_MAX)first=p/4;}colors &= match;}
          if(!colors){
            std::cerr<<"custom_depth_compare fn="<<fn<<" range="<<range[0]<<','<<range[1]<<" wrong_pixels="<<wrong
              <<" first="<<first%extent<<','<<first/extent<<" expected="<<expected[0]<<','<<expected[1]<<','<<expected[2]
              <<" actual="<<int(f.rgba[4*first])<<','<<int(f.rgba[4*first+1])<<','<<int(f.rgba[4*first+2])<<','<<int(f.rgba[4*first+3])
              <<" depth="<<f.depth[first]<<" original_depth="<<a.depth[first]<<'\n';
            const auto&plan=h.windowProxy->plan;const CoinRenderDrawPacket*last=nullptr;
            for(const auto&draw:plan.draws)if(!draw.shadowLightSlot&&draw.sourceNodeId==s.mesh->getNodeId())last=&draw;
            CoinWgpuFfiFrame ffi;std::string diagnostic;const bool prepared=ffi.prepare(plan,extent,extent,diagnostic,false);
            if(last&&last->renderStateSlot<plan.renderStates.size()){
              const auto&captured=plan.renderStates[last->renderStateSlot];
              std::cerr<<"custom_depth_compare_delivery last_draw="<<last->drawOrdinal<<" state="<<last->renderStateSlot
                <<" function="<<int(captured.depthFunction)<<" range="<<captured.depthRange[0]<<','<<captured.depthRange[1]
                <<" ffi="<<prepared<<" diagnostic="<<diagnostic<<'\n';
              for(int unit=0;unit<8;++unit){const auto tex=coin_render_texture_unit(captured,size_t(unit));
                std::cerr<<"custom_depth_compare_texture unit="<<unit<<" enabled="<<tex.enabled<<" slot="<<tex.imageSlot<<" model="<<int(tex.model);
                if(tex.imageSlot<plan.textures.size()){const auto&image=plan.textures[tex.imageSlot];
                  std::cerr<<" size="<<image.width<<'x'<<image.height<<" rgba=";
                  for(size_t c=0;c<std::min(size_t(4),image.pixelsRgba.size());++c)std::cerr<<int(image.pixelsRgba[c])<<',';}
                if(prepared&&last->renderStateSlot<ffi.getView().state_count){const auto&packed=ffi.getView().states[last->renderStateSlot];
                  const auto slot=unit?packed.extra_textures[unit-1].texture_slot:packed.texture_slot;
                  std::cerr<<" ffi_slot="<<slot<<" ffi_model="<<(unit?packed.extra_textures[unit-1].model:packed.texture_model);
                  if(slot<ffi.getView().texture_count){const auto&image=ffi.getView().textures[slot];std::cerr<<" ffi_rgba=";
                    for(size_t c=0;c<std::min(size_t(4),size_t(image.pixel_bytes_len));++c)std::cerr<<int(image.pixels[c])<<',';}}
                std::cerr<<'\n';
              }
            }
          }
          ok &= check(colors,"all eight depth operators compare the same custom-range coplanar samples exactly");}
      }
      comparison->function=SoDepthBuffer::ALWAYS;comparison->range=SbVec2f(.05f,.15f);comparison->write=FALSE;
      Frame preserved,written;const bool kept=h.frame(s,preserved,"custom range overlay depth-write off");ok &= kept;
      if(kept)ok &= check(preserved.depth==a.depth,"depth-write off preserves every existing depth sample publication");
      comparison->write=TRUE;const bool wrote=h.frame(s,written,"custom range overlay depth-write on");ok &= wrote;
      if(wrote)ok &= geometric(written,SbVec2f(.05f,.15f))&&check(written.depth!=a.depth,"depth-write on publishes the overlay custom range");
      s.group->removeChild(overlay);
    }
    const bool restored=h.frame(s,back,"custom range offset/operators restored A");ok &= restored;if(restored)ok &= exact(a,back);
  }
  state->range=SbVec2f(0,1);offset->on=FALSE;return ok;
}
// Independent B49 control: a plane with z=-1+.4*x has a known camera
// mapping depth=(6-.4*x)/19. Calibrate the last covered sample from alpha,
// never from candidate depth, and retain the historical raster/format bounds.
bool planarDepthOracle(WindowPair&h,bool custom=false){
  Scene s(8,8,1.f,false);customizeDepth(s);s.white();const int unit=s.units.back();
  s.image(unit,{{240,120,60}});s.images[unit]->model=SoTexture2::REPLACE;
  SoDepthBuffer*depthState=nullptr;SoPolygonOffset*offsetState=nullptr;
  if(custom){depthState=new SoDepthBuffer;s.receiver->insertChild(depthState,0);
    offsetState=new SoPolygonOffset;offsetState->styles=SoPolygonOffset::FILLED;offsetState->on=FALSE;s.receiver->insertChild(offsetState,1);}
  unsigned phaseIndex=0;
  auto vertices=[&](float right,bool sloped){
    for(int y=0;y<3;++y)for(int x=0;x<3;++x){
      const float px=-1.73f+(right+1.73f)*float(x)/2.f;
      s.positions->point.set1Value(y*3+x,SbVec3f(px,-1.73f+1.73f*y,sloped?-1.f+.4f*px:-1.f));
    }
  };
  Frame initial,restored;bool ok=true;
  for(int extent:{128,96,128}){
    if(h.extent!=extent&&!h.resize(extent))return false;
    const int centre=extent/2;const size_t p=size_t(centre*extent+centre);
    auto countAt=[&](double cut,const char*phase){
      // All Y samples of the centre pixel are covered. Bisect a vertical
      // boundary to find the largest actual sample X, independent of depth.
      const float right=float(4.*(centre+cut)/extent-2.);
      vertices(right,false);Frame probe;if(!h.frame(s,probe,phase))return -1;
      if(probe.rgba.size()!=size_t(extent)*extent*4)return -1;
      const int alpha=probe.rgba[4*p+3];
      const int k=int(std::lround(double(alpha)*windowSamples/255.));
      if(k<0||k>int(windowSamples)||std::abs(alpha-int(std::lround(255.*k/windowSamples)))>2)return -1;
      return k;
    };
    if(!check(countAt(0.,"depth sample calibration empty")==0&&
      countAt(1.,"depth sample calibration full")==int(windowSamples),
      "depth oracle calibration endpoints agree with actual MSAA coverage"))return false;
    double lo=0.,hi=1.;
    for(int i=0;i<10;++i){const double mid=(lo+hi)/2.;const int covered=countAt(mid,"depth sample X bisection");
      if(!check(covered>=0,"depth sample coverage quantizes to k/N"))return false;
      if(covered==int(windowSamples))hi=mid;else lo=mid;}
    constexpr double vertexLatticeStep=1./256.;
    lo=std::max(0.,lo-vertexLatticeStep);hi=std::min(1.,hi+vertexLatticeStep);
    double firstLo=lo,firstHi=hi;
    if(custom){firstLo=0.;firstHi=1.;
      for(int i=0;i<10;++i){const double mid=(firstLo+firstHi)/2.;const int covered=countAt(mid,"first depth sample X bisection");
        if(!check(covered>=0,"first depth sample coverage quantizes to k/N"))return false;
        if(covered>0)firstHi=mid;else firstLo=mid;}
      firstLo=std::max(0.,firstLo-vertexLatticeStep);firstHi=std::min(1.,firstHi+vertexLatticeStep);}
    vertices(1.73f,true);Frame frame;
    if(!h.frame(s,frame,"eight-texture shadowed planar depth oracle"))return false;
    if(!check(frame.depth.size()==size_t(extent)*extent,"complete plane depth readback"))return false;
    const double formatBound=2./16777215.;unsigned checked=0,bad=0;double maxOutside=0.;
    for(int y=extent/4;y<3*extent/4;++y)for(int x=extent/4;x<3*extent/4;++x){
      const size_t i=size_t(y*extent+x);
      const auto predicted=[&](double sampleX){const double worldX=4.*(x+sampleX)/extent-2.;return (6.-.4*worldX)/19.;};
      const double lower=predicted(hi)-formatBound,upper=predicted(lo)+formatBound;
      const double actual=frame.depth[i];const bool valid=std::isfinite(actual)&&actual>=lower&&actual<=upper;
      if(!valid&&bad<4)std::cerr<<"shadow_multitexture_depth_geometry xy="<<x<<','<<y
        <<" actual="<<actual<<" expected="<<lower<<','<<upper<<'\n';
      bad+=!valid;maxOutside=std::max(maxOutside,std::max(lower-actual,actual-upper));++checked;
    }
    std::cout<<"shadow_multitexture_depth_oracle samples="<<windowSamples<<" extent="<<extent
      <<" sample_x="<<lo<<','<<hi<<" checked="<<checked<<" bad="<<bad<<" max_outside="<<maxOutside<<'\n';
    ok &= check(checked>1000&&bad==0,"eight-texture shadowed depth obeys independently calibrated plane geometry");
    if(custom)ok &= customPlaneStates(h,s,depthState,offsetState,firstLo,firstHi,lo,hi,phaseIndex==0);
    ++phaseIndex;
    if(initial.rgba.empty())initial=frame;else if(extent==128)restored=frame;
  }
  // This additional control does not replace the original nonplanar scene's
  // exact A/B/A gate. It independently retains exact restoration for its plane.
  return check(exact(initial,restored),"planar depth and color restore exactly after resize")&&ok;
}
bool profileSweep(WindowPair&h,bool smokeOnly,int selectedMaps=0,int selectedTextures=0){bool ok=true;
  for(bool dir:{false,true})for(float q:{.2f,1.f})for(int maps:{1,2,4,8})for(int textures:{2,4,8}){
    if(smokeOnly&&(maps!=8||textures!=8))continue;
    if((selectedMaps&&maps!=selectedMaps)||(selectedTextures&&textures!=selectedTextures))continue;
    Scene s(maps,textures,q,dir);customizeDepth(s);Frame a,b,back;const auto name=label(s);
    if(!h.frame(s,a,name+" white A")){ok=false;continue;}const auto physicalA=h.lastShown;
    s.model(0);const bool colored=h.frame(s,b,name+" modulate B",false,true);ok &= colored;
    if(colored)ok &= numerical(s,a,b,0)&&check(difference(a,b)>25,"multitexture composition visibly changes physical frame");
    s.white();const bool restored=h.frame(s,back,name+" restored A");ok &= restored;
    if(restored)ok &= exact(a,back)&&check(physicalA==h.lastShown,"same-environment physical A/B/A exact restoration");
  }return ok;
}
bool unitsAndMaps(WindowPair&h,bool readback){bool ok=true;
  for(bool dir:{false,true})for(float q:{.2f,1.f}){
    Scene s(8,8,q,dir);customizeDepth(s);Frame a,b,back;
    if(!h.frame(s,a,label(s)+" deep A",readback)){ok=false;continue;}
    for(int model=0;model<6;++model){s.model(model);const bool done=h.frame(s,b,"texture model "+std::to_string(model),readback);
      ok &= done;if(done)ok &= numerical(s,a,b,model);}s.white();
    for(int map=0;map<8;++map){const float x=s.casterPositions[size_t(map)]->translation.getValue()[0];s.casterX(map,x+.65f);
      const bool moved=h.frame(s,b,"map "+std::to_string(map)+" moved",readback);ok &= moved;if(moved)ok &= check(difference(a,b)>12,"every active shadow map affects presented image");
      s.casterX(map,x);const bool restored=h.frame(s,back,"map restored",readback);ok &= restored;if(restored)ok &= exact(a,back);}
    for(int unit:{0,3,7}){
      s.white();s.pattern(unit);s.uv(unit,.125f,.125f);s.matrices[unit]->matrix=SbMatrix::identity();
      if(!h.frame(s,a,"unit "+std::to_string(unit)+" red A",readback)){ok=false;continue;}ok &= expectedColor(a,{{240,30,60}});
      const auto physicalA=h.lastShown;
      s.uv(unit,.75f,.125f);bool done=h.frame(s,b,"independent unit UV green",readback);ok &= done;if(done)ok &= expectedColor(b,{{40,220,70}});
      SbMatrix q2=SbMatrix::identity();q2[3][3]=2.f;s.matrices[unit]->matrix=q2;
      done=h.frame(s,b,"shadowed direct ST Q=2",readback);ok &= done;
      if(done)ok &= expectedColor(b,{{40,220,70}})&&projectionPolicy(s,h.windowProxy->plan,CoinRenderTextureProjection::DIRECT_ST);
      s.receiverStyle->style=SoShadowStyle::NO_SHADOWING;
      done=h.frame(s,b,"unshadowed projective Q=2 red",readback);ok &= done;
      if(done)ok &= expectedColor(b,{{240,30,60}})&&projectionPolicy(s,h.windowProxy->plan,CoinRenderTextureProjection::PROJECTIVE);
      s.receiverStyle->style=SoShadowStyle::SHADOWED;s.matrices[unit]->matrix=SbMatrix::identity();s.uv(unit,.125f,.125f);
      done=h.frame(s,back,"unit UV/STQ restored",readback);ok &= done;if(done)ok &= exact(a,back)&&check(physicalA==h.lastShown,"unit homogeneous A/B/A physical restoration");
      SbMatrix translated=SbMatrix::identity();translated[3][0]=.75f;s.matrices[unit]->matrix=translated;
      done=h.frame(s,b,"unit matrix translation",readback);ok &= done;if(done)ok &= expectedColor(b,{{40,220,70}});
      s.matrices[unit]->matrix=SbMatrix::identity();s.uv(unit,1.125f,.125f);s.images[unit]->wrapS=SoTexture2::REPEAT;
      done=h.frame(s,a,"unit sampler repeat A",readback);ok &= done;if(done)ok &= expectedColor(a,{{240,30,60}});
      s.images[unit]->wrapS=SoTexture2::CLAMP;done=h.frame(s,b,"unit sampler clamp B",readback);ok &= done;if(done)ok &= expectedColor(b,{{40,220,70}});
      s.images[unit]->wrapS=SoTexture2::REPEAT;done=h.frame(s,back,"unit sampler repeat restored A",readback);ok &= done;if(done)ok &= exact(a,back);
      s.image(unit,{{20,200,220}});done=h.frame(s,b,"unit independent image mutation",readback);ok &= done;if(done)ok &= expectedColor(b,{{20,200,220}});
      s.pattern(unit);done=h.frame(s,back,"unit image restored",readback);ok &= done;if(done)ok &= exact(a,back);
    }
  }return ok;
}
bool adversarial(WindowPair&h,bool readback){Scene s(8,8,1.f,false);customizeDepth(s);s.model(0);Frame a,b;bool ok=h.frame(s,a,"rollback A",readback);
  const auto physicalA=h.lastShown;
  for(int fault=0;fault<4;++fault){const auto uv=s.coords[7]->point[0];const auto matrix=s.matrices[3]->matrix.getValue();SoTextureUnit*badUnit=nullptr;SoTexture2*badImage=nullptr;SoAlphaTest*badAlpha=nullptr;
    if(fault==0)s.coords[7]->point.set1Value(0,SbVec2f(std::numeric_limits<float>::quiet_NaN(),0));
    if(fault==1){SbMatrix invalid=matrix;invalid[2][3]=std::numeric_limits<float>::infinity();s.matrices[3]->matrix=invalid;}
    if(fault==2){badUnit=new SoTextureUnit;badUnit->unit=8;badImage=new SoTexture2;const uint8_t p[]={10,80,210};badImage->image.setValue(SbVec2s(1,1),3,p);
      s.receiver->insertChild(badUnit,s.receiver->getNumChildren()-1);s.receiver->insertChild(badImage,s.receiver->getNumChildren()-1);}
    if(fault==3){badAlpha=new SoAlphaTest;badAlpha->function=SoAlphaTest::GREATER;badAlpha->value=std::numeric_limits<float>::quiet_NaN();s.receiver->insertChild(badAlpha,s.receiver->getNumChildren()-1);}
    ok &= h.refuse(s,std::to_string(fault),readback);
    s.coords[7]->point.set1Value(0,uv);s.matrices[3]->matrix=matrix;if(badImage)s.receiver->removeChild(badImage);if(badUnit)s.receiver->removeChild(badUnit);if(badAlpha)s.receiver->removeChild(badAlpha);
    const bool recovered=h.frame(s,b,"recovery after invalid "+std::to_string(fault),readback);ok &= recovered;
    if(recovered)ok &= exact(a,b)&&check(physicalA==h.lastShown,"invalid input then recovery restores physical A");
  }
  // Transparent multimap/multitexture combinations have positive and atomic
  // controls in CoinRenderShadowMultitextureAlphaTest.
  for(int size:{160,96,128}){ok &= h.resize(size);s.model(size==96?1:0);ok &= h.frame(s,b,"resized and reconfigured",readback);}
  ok &= exact(a,b)&&check(physicalA==h.lastShown,"resize and scene reconfiguration restore physical A");
  // A successful GPU-only frame after an explicit request must not expose stale CPU bytes.
  ok &= h.frame(s,b,"readback request consumed; GPU-only recovery",false);return ok;
}
bool bindingWitnesses(WindowPair&h){bool ok=true;
  for(float q:{.2f,1.f})for(int binding:bindings){Scene s(4,8,q,false);customizeDepth(s);s.materialBinding->value=binding;s.normalBinding->value=binding;
    Frame a,b,back;if(!h.frame(s,a,"material/normal binding A")){ok=false;continue;}
    const auto physicalA=h.lastShown;SbColor colors[9];SbVec3f normals[9];
    for(int i=0;i<9;++i){colors[i]=s.receiverMaterial->diffuseColor[i];normals[i]=s.normalData->vector[i];}
    for(int axis=0;axis<2;++axis){
      for(int i=0;i<9;++i)if(axis==0)s.receiverMaterial->diffuseColor.set1Value(i,SbColor(.2f,.8f,.3f));
        else s.normalData->vector.set1Value(i,SbVec3f(.8f,.4f,1));
      const bool changed=h.frame(s,b,axis==0?"independent material mutation B":"independent normal mutation B");ok &= changed;
      if(changed)ok &= check(difference(a,b)>25,"each admitted material/normal binding independently affects physical lighting");
      s.receiverMaterial->diffuseColor.setValues(0,9,colors);s.normalData->vector.setValues(0,9,normals);
      const bool restored=h.frame(s,back,"binding restored A");ok &= restored;
      if(restored)ok &= exact(a,back)&&check(physicalA==h.lastShown,"binding physical A/B/A");
    }
  }return ok;
}
bool customAllUnits(WindowPair&h){
  Scene s(8,8,1.f,false);customizeDepth(s);Frame a,b,back;
  if(!h.frame(s,a,"custom depth all-units white A"))return false;bool ok=true;
  for(int unit=0;unit<8;++unit){s.image(unit,{{128,180,224}});
    const bool changed=h.frame(s,b,"custom depth independent unit "+std::to_string(unit));ok &= changed;
    if(changed){unsigned checked=0,bad=0;const double tint[]={128./255.,180./255.,224./255.};
      for(size_t p=0;p<a.depth.size();++p){if(a.rgba[4*p+3]!=255)continue;
        for(int c=0;c<3;++c)bad+=std::abs(int(b.rgba[4*p+c])-int(std::lround(a.rgba[4*p+c]*tint[c])))>3;
        bad+=b.depth[p]!=a.depth[p];++checked;}
      ok &= check(checked>2000&&bad==0&&difference(a,b)>1000,"each of eight texture units contributes under custom depth");}
    s.image(unit,{{255,255,255}});const bool restored=h.frame(s,back,"custom depth unit restored A");ok &= restored;
    if(restored)ok &= exact(a,back);
  }
  // Nonconstant homogeneous Q must not change SHADOWED direct ST, but must
  // select the projective texel when the receiver explicitly stops shadowing.
  s.pattern(7);for(int y=0;y<3;++y)for(int x=0;x<3;++x)
    s.coords[7]->point.set1Value(y*3+x,SbVec2f(.70f+.05f*x,.10f+.025f*y));
  SbMatrix matrix=SbMatrix::identity();
  matrix[0][3]=.25f;matrix[1][3]=.125f;matrix[3][3]=2.f;s.matrices[7]->matrix=matrix;
  bool done=h.frame(s,b,"custom depth unit7 varying homogeneous Q DIRECT_ST",false,true);ok &= done;
  if(done)ok &= expectedColor(b,{{40,220,70}})&&projectionPolicy(s,h.windowProxy->plan,CoinRenderTextureProjection::DIRECT_ST);
  s.receiverStyle->style=SoShadowStyle::NO_SHADOWING;
  done=h.frame(s,b,"custom depth unit7 varying homogeneous Q PROJECTIVE");ok &= done;
  if(done)ok &= expectedColor(b,{{240,30,60}})&&projectionPolicy(s,h.windowProxy->plan,CoinRenderTextureProjection::PROJECTIVE);
  return ok;
}
bool customDepthCampaign(WindowPair&h,bool smokeOnly){
  customDepthMode=true;bool ok=true;
  for(const auto range:{SbVec2f(.2f,.8f),SbVec2f(.8f,.2f)}){
    customDepthRange=range;customOffsetEnabled=false;
    std::cout<<"shadow_multitexture_custom_depth range="<<range[0]<<','<<range[1]<<'\n';
    ok &= coverageWitness(h);ok &= profileSweep(h,smokeOnly);
    customOffsetEnabled=true;
    ok &= coverageWitness(h);ok &= customAllUnits(h);
    if(!smokeOnly){ok &= unitsAndMaps(h,false);ok &= bindingWitnesses(h);ok &= adversarial(h,false);}
  }
  // Exercise explicit current-frame resolve once at the resource/offset limit.
  if(!smokeOnly&&h.shown->requestWindowReadbackRGBA())ok &= unitsAndMaps(h,true);
  customDepthMode=false;customOffsetEnabled=false;return ok;
}

int customDepthBoundaries(WindowPair&h){
  customDepthMode=true;customOffsetEnabled=true;bool ok=true;
  for(const auto range:{SbVec2f(.2f,.8f),SbVec2f(.8f,.2f)}){
    customDepthRange=range;
    // The four-map program family and sparse four-texture layout complement
    // the maximal eight-map/eight-unit batch without repeating its full sweep.
    ok &= profileSweep(h,false,4,4);
    if(!h.shown->requestWindowReadbackRGBA()){
      std::cout<<"custom_depth_boundaries explicit_readback=unsupported\n";return 77;
    }
    ok &= coverageWitness(h,true);ok &= adversarial(h,true);
  }
  customDepthMode=false;customOffsetEnabled=false;return ok?0:1;
}

bool customDepthBatch(const CoinRenderCapabilities&caps){
  // This numerical batch intentionally keeps a native runtime alive. Lifetime
  // qualification remains the separate unanchored --lifecycle mode.
  WindowPair runtime(caps);if(!runtime.ready)return false;
  uint64_t latest=0;bool ok=true;
  for(const auto policy:{COIN_RENDER_SAMPLING_NATIVE,COIN_RENDER_SAMPLING_PORTABLE})
  for(int precise:{1,0})for(unsigned count:{2u,4u}){
    samplingTestPolicy=policy;windowSamples=count;
    setenv("COIN_QUADMESH_PRECISE_LIGHTING",precise?"1":"0",1);
    std::cout<<"custom_depth_batch samples="<<count<<" precise="<<precise<<" sampling="<<policy<<'\n';
    WindowPair h(caps,latest);if(!h.ready)return false;
    ok &= customDepthCampaign(h,true);latest=h.latest;
  }
  return ok;
}

bool customDepthSharedDisplayBatch(const CoinRenderCapabilities&caps){
  // A distinct numerical qualification: one connection stays alive through
  // every target. The original per-target connection/recreation checks remain
  // separate; this mode neither retries nor qualifies their lifetime behavior.
  std::unique_ptr<Display,int(*)(Display*)> shared(XOpenDisplay(nullptr),XCloseDisplay);
  if(!check(bool(shared),"shared numerical batch opens one X11 connection"))return false;
  WindowPair runtime(caps,0,shared.get());if(!runtime.ready)return false;
  uint64_t latest=0;bool ok=true;
  for(const auto policy:{COIN_RENDER_SAMPLING_NATIVE,COIN_RENDER_SAMPLING_PORTABLE})
  for(int precise:{1,0})for(unsigned count:{2u,4u}){
    samplingTestPolicy=policy;windowSamples=count;
    setenv("COIN_QUADMESH_PRECISE_LIGHTING",precise?"1":"0",1);
    std::cout<<"custom_depth_shared_display_batch samples="<<count<<" precise="<<precise<<" sampling="<<policy<<'\n';
    WindowPair h(caps,latest,shared.get());if(!h.ready)return false;
    ok &= customDepthCampaign(h,true);latest=h.latest;
  }
  return ok;
}

}
#ifndef COIN_RENDER_SHADOW_MULTITEXTURE_WINDOW_FIXTURE_ONLY
int main(int argc,char**argv){configureSamplingTest(argc,argv);std::string mode;
  for(int i=1;i<argc;++i){const std::string arg=argv[i];if(arg=="--portable-sampling")continue;
    if(arg=="--samples"){if(++i==argc)return 2;const std::string count=argv[i];
      if(count!="1"&&count!="2"&&count!="4")return 2;windowSamples=unsigned(std::stoi(count));continue;}
    if(!mode.empty())return 2;mode=arg;}
  if(mode!="--campaign"&&mode!="--smoke"&&mode!="--lifecycle"&&mode!="--readback"&&mode!="--coverage"&&mode!="--depth-oracle"&&mode!="--custom-depth"&&mode!="--custom-depth-smoke"&&mode!="--custom-depth-oracle"&&mode!="--custom-depth-batch-smoke"&&mode!="--custom-depth-shared-display-batch"&&mode!="--custom-depth-boundaries")return 2;
  if(!XInitThreads())return 1;SoDB::init();CoinRenderAction::initClass();
  const auto caps=msaa_shadow_test::capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW);
  if(!caps.gpu_available||caps.renderer==COIN_RENDER_RENDERER_UNKNOWN)return 77;
  if(!check(caps.max_texture_units>=8&&caps.max_lights_per_draw>=8,"reported capacities cover declared 8/8 profile"))return 1;
  if(mode=="--custom-depth-batch-smoke")return customDepthBatch(caps)?0:1;
  if(mode=="--custom-depth-shared-display-batch")return customDepthSharedDisplayBatch(caps)?0:1;
  if(mode=="--lifecycle"){
    // Deliberately no anchor: retain visibility of full native runtime recreation failures.
    // The wgpu publication serial is global and can outlive native targets;
    // carry its last observed serial through recreation.
    uint64_t previousDeviceSerial=0;
    for(int i=0;i<2;++i){
      WindowPair h(caps,previousDeviceSerial);
      if(!h.ready||!coverageWitness(h)||!profileSweep(h,true))return 1;
      previousDeviceSerial=h.latest;
    }return 0;
  }
  WindowPair h(caps);if(!h.ready)return 1;
  if(mode=="--depth-oracle"||mode=="--custom-depth-oracle")return planarDepthOracle(h,mode=="--custom-depth-oracle")?0:1;
  if(mode=="--custom-depth"||mode=="--custom-depth-smoke")return customDepthCampaign(h,mode=="--custom-depth-smoke")?0:1;
  if(mode=="--custom-depth-boundaries")return customDepthBoundaries(h);
  bool ok=coverageWitness(h);
  if(mode=="--coverage")return ok?0:1;
  if(mode!="--readback")ok &= profileSweep(h,mode=="--smoke");
  if(mode=="--campaign"){
    ok &= unitsAndMaps(h,false);ok &= bindingWitnesses(h);ok &= adversarial(h,false);
  }
  if(mode=="--campaign"||mode=="--readback"){
    // Explicit readback capability is negotiated by the existing public request API.
    if(h.shown->requestWindowReadbackRGBA()){
      ok &= coverageWitness(h,true);ok &= unitsAndMaps(h,true);ok &= adversarial(h,true);
    }else {std::cout<<"shadow_multitexture_window explicit_readback=unsupported\n";if(mode=="--readback")return 77;}
  }
  return ok?0:1;
}

#endif
