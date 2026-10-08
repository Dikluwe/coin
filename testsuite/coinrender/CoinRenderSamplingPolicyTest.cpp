#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "actions/CoinRenderActionP.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderSelectionCore.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

#ifdef COIN_SAMPLING_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
struct SamplingWindows {
  Display* display = nullptr;
  Window native = 0, portable = 0;
  bool open() {
    display = XOpenDisplay(nullptr); if (!display) return false;
    auto create = [&](int x) {
      Window w = XCreateSimpleWindow(display, DefaultRootWindow(display), x, 50, 64, 64, 0, 0, 0);
      XSelectInput(display, w, StructureNotifyMask);
      XStoreName(display, w, "CoinRender sampling policy qualification");
      size(w, 64); XMapWindow(display, w);
      XEvent event; do { XWindowEvent(display, w, StructureNotifyMask, &event); } while (event.type != MapNotify);
      return w;
    };
    native = create(50); portable = create(150); XSync(display, False); return true;
  }
  void size(Window w, int n) {
    XSizeHints hints{}; hints.flags = PMinSize | PMaxSize;
    hints.min_width = hints.max_width = hints.min_height = hints.max_height = n;
    XSetWMNormalHints(display, w, &hints); XResizeWindow(display, w, n, n); XSync(display, False);
    for(int attempt=0;attempt<200;++attempt) {
      XWindowAttributes a{};XGetWindowAttributes(display,w,&a);
      if(a.width==n && a.height==n) {std::this_thread::sleep_for(std::chrono::milliseconds(30));return;}
      std::this_thread::sleep_for(std::chrono::milliseconds(5));XSync(display,False);
    }
    std::cerr << "Native drawable failed to reach requested extent\n";

  }
  CoinRenderNativeSurfaceDescriptor descriptor(Window w) const {
    CoinRenderNativeSurfaceDescriptor d{}; d.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
    d.structSize = sizeof(d); d.type = COIN_RENDER_SURFACE_XLIB;
    d.native.xlib.display = display; d.native.xlib.window = w; return d;
  }
  ~SamplingWindows() {
    if (display) { if (native) XDestroyWindow(display, native); if (portable) XDestroyWindow(display, portable); XCloseDisplay(display); }
  }
};
#endif

static bool check(bool value, const char* label) {
  if (!value) std::cerr << "FAIL " << label << '\n';
  return value;
}
static bool selection(bool gpu, bool window = false) {
  CoinRenderCapabilities caps{};
  caps.version=4;caps.struct_size=sizeof(caps);caps.backend=COIN_RENDER_EXPERIMENTAL_RUST;
  caps.probe_status=COIN_RENDER_PROBE_AVAILABLE;
  caps.implemented_sampling_policies=COIN_RENDER_SAMPLING_POLICY_NATIVE|COIN_RENDER_SAMPLING_POLICY_PORTABLE;
  caps.available_sampling_policies=caps.implemented_sampling_policies;
  caps.qualified_sampling_policies=COIN_RENDER_SAMPLING_POLICY_PORTABLE;
  bool ok=check(coin_render_select_sampling_policy(&caps,COIN_RENDER_SAMPLING_PORTABLE,1).reason==COIN_RENDER_SELECTION_SUPPORTED,"qualified portable selection");
  ok &= check(coin_render_select_sampling_policy(&caps,COIN_RENDER_SAMPLING_NATIVE,1).reason==COIN_RENDER_SELECTION_UNQUALIFIED_PROFILE,"native does not advertise portable parity evidence");
  caps.available_sampling_policies=COIN_RENDER_SAMPLING_POLICY_NATIVE;
  ok &= check(coin_render_select_sampling_policy(&caps,COIN_RENDER_SAMPLING_PORTABLE,0).reason==COIN_RENDER_SELECTION_HARDWARE_UNAVAILABLE,"no silent fallback to native");
  caps.probe_status=COIN_RENDER_PROBE_BUSY;
  ok &= check(coin_render_select_sampling_policy(&caps,COIN_RENDER_SAMPLING_PORTABLE,0).reason==COIN_RENDER_SELECTION_RUNTIME_NOT_READY,"unknown runtime is not unsupported or available");
  caps.version=3;
  ok &= check(coin_render_select_sampling_policy(&caps,COIN_RENDER_SAMPLING_PORTABLE,0).reason==COIN_RENDER_SELECTION_INVALID_REQUEST,"v3 cannot invent v4 sampling facts");
  caps.version=4;caps.struct_size=offsetof(CoinRenderCapabilities,implemented_sampling_policies);
  ok &= check(coin_render_select_sampling_policy(&caps,COIN_RENDER_SAMPLING_PORTABLE,0).reason==COIN_RENDER_SELECTION_INVALID_REQUEST,"truncated capabilities reject sampling selection");
  caps.struct_size=sizeof(caps);
  ok &= check(coin_render_select_sampling_policy(&caps,static_cast<CoinRenderTextureSamplingPolicy>(99),0).reason==COIN_RENDER_SELECTION_INVALID_REQUEST,"invalid policy rejected before shift");
  CoinRenderOptions options;std::string diagnostic;
  ok &= check(options.textureSamplingPolicy==COIN_RENDER_SAMPLING_NATIVE,"source-compatible native default");
  options.textureSamplingPolicy=static_cast<CoinRenderTextureSamplingPolicy>(99);
  ok &= check(!coin_render_valid_options(options,diagnostic),"invalid target policy rejected");
  if (!gpu) return ok;
  if (!check(coin_render_query_capabilities(window ? COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW : COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))==0,"v4 runtime capabilities")) return false;
  ok &= check(caps.version==4 && (caps.implemented_sampling_policies&COIN_RENDER_SAMPLING_POLICY_PORTABLE) && caps.portable_sampling_filter_mask==(1u<<2) && caps.portable_sampling_max_anisotropy==1 && caps.max_texture_mip_chain_bytes==UINT64_C(128)*1024*1024,"explicit portable profile and limits");
  ok &= check(caps.portable_sampling_formats==31,"five explicit compiled image formats");
#ifdef HAVE_COIN_BGFX
  ok &= check(caps.portable_sampling_derivatives==COIN_RENDER_SAMPLING_DERIVATIVE_DEFAULT,"BGFX reports ordinary shader derivatives for every renderer");
#else
  ok &= check(caps.portable_sampling_derivatives==(caps.renderer==COIN_RENDER_RENDERER_OPENGL ?
      COIN_RENDER_SAMPLING_DERIVATIVE_DEFAULT : COIN_RENDER_SAMPLING_DERIVATIVE_FINE),"wgpu reports GL fallback versus fine Vulkan derivatives");
#endif
  for (auto old : {std::pair<size_t,uint32_t>{offsetof(CoinRenderCapabilities,probe_status),1},
                  {offsetof(CoinRenderCapabilities,known_hardware_facts),2},
                  {offsetof(CoinRenderCapabilities,implemented_sampling_policies),3}}) {
    std::array<unsigned char,sizeof(CoinRenderCapabilities)+16> buffer;buffer.fill(0xa5);
    ok &= check(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,buffer.data(),old.first)==0,"legacy query accepts exact prefix");
    uint32_t version=0;std::memcpy(&version,buffer.data()+4,4);
    ok &= check(version==old.second && std::all_of(buffer.begin()+old.first,buffer.end(),[](unsigned char c){return c==0xa5;}),"legacy version and all trailing bytes preserved");
  }
  std::cout<<"adapter="<<caps.adapter_name<<" renderer="<<caps.renderer<<" derivative="<<caps.portable_sampling_derivatives<<'\n';
  return ok;
}
int main(int argc,char**argv) {
  SoDB::init();CoinRenderAction::initClass();
  const bool window=argc>1 && std::string(argv[1])=="--window";
  const bool gpu=window || (argc>1 && std::string(argv[1])=="--gpu");
#ifdef COIN_SAMPLING_X11
  SamplingWindows windows;
  if (window && !windows.open()) return 77;
#else
  if (window) return 77;
#endif
  bool ok=selection(gpu, window);
  if (gpu && !CoinRenderAction::isGpuBackendAvailable()) return 77;
  std::string diagnostic;auto nativeOptions=CoinRenderDiagnosticShell::renderOptions(diagnostic);
  if (!diagnostic.empty()) {std::cerr<<diagnostic<<'\n';return 1;}
  if(gpu && nativeOptions.renderer==COIN_RENDER_RENDERER_UNKNOWN) {
    CoinRenderCapabilities runtime{};
    if(coin_render_query_capabilities(window ? COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW : COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&runtime,sizeof(runtime))!=0) return 1;
    nativeOptions.renderer=static_cast<CoinRenderRenderer>(runtime.renderer);
  }
  bool externalCapture = false;
#ifndef HAVE_COIN_BGFX
  externalCapture = window && nativeOptions.renderer == COIN_RENDER_RENDERER_OPENGL;
#endif
  auto readPixels = [&](CoinRenderTarget* target, std::vector<uint8_t>& pixels) {
#ifdef COIN_SAMPLING_X11
    if(externalCapture && target->getPimpl()->kind==CoinRenderTargetP::KIND_WINDOW) {
      const auto n=target->getSize(); const auto w=target->getPimpl()->nativeDesc.native.xlib.window;
      std::this_thread::sleep_for(std::chrono::milliseconds(30));XSync(windows.display,False);
      auto* image=XGetImage(windows.display,w,0,0,n[0],n[1],AllPlanes,ZPixmap);
      pixels.clear();if(!image)return;
      pixels.resize(size_t(n[0])*n[1]*4,255);
      const unsigned long masks[]={image->red_mask,image->green_mask,image->blue_mask};
      for(int y=0;y<n[1];++y)for(int x=0;x<n[0];++x)for(int c=0;c<3;++c) {
        unsigned long mask=masks[c],v=XGetPixel(image,x,y)&mask;
        while(mask && !(mask&1)){mask>>=1;v>>=1;}
        pixels[(y*n[0]+x)*4+c]=mask ? uint8_t(v*255/mask) : 0;
      }
      XDestroyImage(image);return;
    }
#endif
    target->readbackRGBA(pixels);
  };
  auto portableOptions=nativeOptions;portableOptions.textureSamplingPolicy=COIN_RENDER_SAMPLING_PORTABLE;
  auto createTarget = [&](bool isPortable) {
    const auto& options = isPortable ? portableOptions : nativeOptions;
#ifdef COIN_SAMPLING_X11
    if (window) return CoinRenderTarget::createWindow(windows.descriptor(isPortable ? windows.portable : windows.native), SbVec2i32(64,64), options);
#endif
    return CoinRenderTarget::createOffscreen(SbVec2i32(64,64), options);
  };
  std::unique_ptr<CoinRenderTarget> native(createTarget(false)), portable(createTarget(true));
  if (!gpu) {native->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);portable->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);}
  SoSeparator* root=new SoSeparator;root->ref();
  auto* camera=new SoOrthographicCamera;camera->height=2;camera->position=SbVec3f(0,0,3);root->addChild(camera);
  auto* light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;root->addChild(light);
  auto* quality=new SoComplexity;quality->textureQuality=.5f;root->addChild(quality);
  auto* unit=new SoTextureUnit;root->addChild(unit);
  auto* texture=new SoTexture2;texture->model=SoTexture2::REPLACE;root->addChild(texture);
  std::vector<unsigned char> bytes(128*128*4,255);
  for(int y=0;y<128;++y)for(int x=0;x<128;++x)for(int c=0;c<3;++c)bytes[(y*128+x)*4+c]=x<64?40:160;
  texture->image.setValue(SbVec2s(128,128),4,bytes.data());
  auto* coords=new SoCoordinate3;const SbVec3f positions[]={{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};coords->point.setValues(0,4,positions);root->addChild(coords);
  auto* uv=new SoTextureCoordinate2;const SbVec2f st[]={{-1.46875f,.53125f},{2.53125f,.53125f},{2.53125f,.53125f},{-1.46875f,.53125f}};uv->point.setValues(0,4,st);root->addChild(uv);
  auto* face=new SoIndexedFaceSet;const int32_t indices[]={0,1,2,3,-1};face->coordIndex.setValues(0,5,indices);root->addChild(face);
  CoinRenderAction action(SbViewportRegion(64,64)), nativeAction(SbViewportRegion(64,64));
  action.setRenderTarget(portable.get());nativeAction.setRenderTarget(native.get());
  std::vector<uint8_t> nativeFirst,nativeAgain,portablePixels;
  auto render=[&](CoinRenderTarget* target) {
    auto& selected = target==native.get() ? nativeAction : action;
    if(window && !externalCapture && !target->requestWindowReadbackRGBA()) return check(false,"window capture admitted");
    selected.apply(root);
    if(selected.getLastStatus()!=CoinRenderAction::SUCCESS) std::cerr << "render size=" << target->getSize()[0] << " policy=" << target->getOptions().textureSamplingPolicy << " status=" << selected.getLastStatus() << " diagnostic=" << selected.getLastError().getString() << "\n";
    return check(selected.getLastStatus()==CoinRenderAction::SUCCESS,"explicit-policy frame renders");
  };
  ok &= render(native.get());readPixels(native.get(),nativeFirst);
  for(int textureUnit : {0,7,0}) {
    unit->unit=textureUnit;
    ok &= render(portable.get());readPixels(portable.get(),portablePixels);
    ok &= check(action.getPimpl()->lastValidPlan.textureSamplingPolicy==COIN_RENDER_SAMPLING_PORTABLE,"captured plan owns selected policy");
    if(portablePixels.size()!=64*64*4){ok=false;break;}
    // Independent floor oracle: original du/dx=1/16, texture128 => LOD3.
    // x30 -> u0.4375, texel7 (40); x31 -> u0.5, texel8 (160).
    for(int y=8;y<56;++y)for(int x : {30,31})for(int c=0;c<3;++c)
      ok &= check(std::abs(int(portablePixels[(y*64+x)*4+c])-(x==30?40:160))<=1,"portable active-mip floor oracle");
    auto valid=action.getPimpl()->lastValidPlan;
    const uint64_t serial=portable->getLastSubmissionSerial();std::size_t borrowedBytes=0;
    const uint8_t* borrowed=portable->borrowRGBA(borrowedBytes);
    auto invalid=valid;const auto layer=coin_render_texture_unit(invalid.renderStates[0],textureUnit);
    invalid.samplers[layer.samplerSlot].maxAnisotropy=4;invalid.revision+=100000;
    const auto rejected=portable->getPimpl()->executeFrame(invalid);
    std::vector<uint8_t> retained;readPixels(portable.get(),retained);std::size_t retainedBytes=0;
    ok &= check(rejected.status==CoinRenderBackendStatus::UNSUPPORTED && rejected.diagnostic.find("isotropic")!=std::string::npos && retained==portablePixels && portable->getLastSubmissionSerial()==serial && portable->borrowRGBA(retainedBytes)==borrowed && retainedBytes==borrowedBytes,"unsupported combination preserves pixels, serial and borrowed pointer");
    valid.revision+=200000;
    ok &= check(portable->getPimpl()->executeFrame(valid).status==CoinRenderBackendStatus::SUCCESS,"recovery after rejected sampler");
  }
  unit->unit=0;
  ok &= render(native.get());readPixels(native.get(),nativeAgain);
  ok &= check(nativeAgain==nativeFirst,"portable target cannot contaminate native shader/cache");
  quality->textureQuality=0;ok &= render(portable.get());
  quality->textureQuality=.5f;ok &= render(portable.get());
  if (gpu && !window) {
    CoinRenderReadbackTicket ticket;action.applyAsync(root,ticket);
    ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS && ticket.token!=0,"portable asynchronous submission");
    std::vector<uint8_t> asyncPixels;std::vector<float> asyncDepth;
    CoinRenderTarget::ReadbackStatus status=CoinRenderTarget::READBACK_NOT_READY;
    for(int attempt=0;ticket.token && attempt<2000 && status==CoinRenderTarget::READBACK_NOT_READY;++attempt) {
      status=CoinRenderTarget::pollReadback(ticket,asyncPixels,asyncDepth);
      if(status==CoinRenderTarget::READBACK_NOT_READY)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ok &= check(status==CoinRenderTarget::READBACK_READY && asyncPixels.size()==64*64*4 &&
                std::abs(int(asyncPixels.empty()?0:asyncPixels[(32*64+31)*4])-160)<=1,"async path preserves portable sampling oracle");
    if(status!=CoinRenderTarget::READBACK_READY && ticket.token)CoinRenderTarget::cancelReadback(ticket);
  }
  ok &= check(native->getOptions().textureSamplingPolicy==COIN_RENDER_SAMPLING_NATIVE && portable->getOptions().textureSamplingPolicy==COIN_RENDER_SAMPLING_PORTABLE,"target policies stay immutable and independent of experimental environment");
  if(gpu)ok &= check(native->getPimpl()->backend->resourceDomain().device==portable->getPimpl()->backend->resourceDomain().device,"policies share device resources");
#ifdef COIN_SAMPLING_X11
  if(window) windows.size(windows.portable,128);
#endif
  ok &= check(portable->resize(SbVec2i32(128,128)),"portable target resize");
  action.setViewportRegion(SbViewportRegion(128,128));ok &= render(portable.get());
  readPixels(portable.get(),portablePixels);
  if (portablePixels.size()==128*128*4) {
    for(int x : {62,63}) ok &= check(std::abs(int(portablePixels[(64*128+x)*4])-(x==62?40:160))<=1,"resized derivative footprint oracle");
  } else ok=false;
#ifdef COIN_SAMPLING_X11
  if(window) {
    XUnmapWindow(windows.display, windows.portable); XSync(windows.display,False);
    ok &= check(portable->resize(SbVec2i32(0,0)),"suspend portable window");
    action.apply(root);
    ok &= check(action.getLastStatus()==CoinRenderAction::NOT_READY,"suspended window rejects submission");
    ok &= render(native.get());readPixels(native.get(),nativeAgain);
    ok &= check(nativeAgain==nativeFirst,"suspended portable window leaves native sibling intact");
    XMapWindow(windows.display,windows.portable);XSync(windows.display,False);
    XEvent mapped;do {XWindowEvent(windows.display,windows.portable,StructureNotifyMask,&mapped);} while(mapped.type!=MapNotify);
    windows.size(windows.portable,128);

    ok &= check(portable->resize(SbVec2i32(128,128)),"resume portable window");
    ok &= render(portable.get());readPixels(portable.get(),portablePixels);
    std::cout << "remap capture bytes=" << portablePixels.size() << " x62=" << (portablePixels.size()==128*128*4 ? int(portablePixels[(64*128+62)*4]) : -1) << " x63=" << (portablePixels.size()==128*128*4 ? int(portablePixels[(64*128+63)*4]) : -1) << "\n";
    ok &= check(portablePixels.size()==128*128*4 && std::abs(int(portablePixels[(64*128+63)*4])-160)<=1,"remap/expose restores portable oracle");
    action.setRenderTarget(nullptr);portable.reset();
    CoinRenderSceneManager manager(windows.descriptor(windows.portable),SbVec2i32(128,128),portableOptions);
    manager.setSceneGraph(root);if(!externalCapture)manager.getRenderTarget()->requestWindowReadbackRGBA();
    ok &= check(manager.render()==CoinRenderAction::SUCCESS,"scene manager forwards explicit policy to window");
    readPixels(manager.getRenderTarget(),portablePixels);
    ok &= check(manager.getRenderTarget()->getOptions().textureSamplingPolicy==COIN_RENDER_SAMPLING_PORTABLE && portablePixels.size()==128*128*4 && std::abs(int(portablePixels[(64*128+63)*4])-160)<=1,"scene manager portable image oracle");
  }
#endif
  // Exercise the same public SoRenderManager adapter used by the FreeCAD host.
  { SoRenderManager source;source.setSceneGraph(root);source.setViewportRegion(SbViewportRegion(64,64));
    CoinRenderManagerAdapter adapter(source,SbVec2i32(64,64),portableOptions);
    auto* target=adapter.getSceneManager()->getRenderTarget();
    if(!gpu) target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
    ok &= check(adapter.render()==CoinRenderAction::SUCCESS,"host adapter forwards explicit options");
    target->readbackRGBA(portablePixels);
    ok &= check(target->getOptions().textureSamplingPolicy==COIN_RENDER_SAMPLING_PORTABLE && portablePixels.size()==64*64*4 && std::abs(int(portablePixels[(32*64+31)*4])-160)<=1,"host adapter portable image oracle");
  }
  action.setRenderTarget(nullptr);nativeAction.setRenderTarget(nullptr);root->unref();
  std::cout<<"Sampling API window="<<window<<" CPU="<<!gpu<<" result="<<(ok?"PASS":"FAIL")<<'\n';
  return ok?0:1;
}
