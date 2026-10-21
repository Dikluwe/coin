// A submission receipt is not an X11 presentation-completion fence.
// Observe a single submitted frame without a second render or CPU/GPU oracle wait.
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "CoinRenderTerrainFixture.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
struct Pixel { unsigned char rgb[3]; };
unsigned char channel(unsigned long value, unsigned long mask) {
  if (!mask) return 0;
  while (!(mask & 1)) { mask >>= 1; value >>= 1; }
  return static_cast<unsigned char>(((value & mask) * 255 + mask / 2) / mask);
}
std::vector<Pixel> capture(Display* display, Window window, int w, int h) {
  XImage* image = XGetImage(display, window, 0, 0, w, h, AllPlanes, ZPixmap);
  if (!image) return {};
  std::vector<Pixel> result(size_t(w)*h);
  for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) {
    const unsigned long p = XGetPixel(image, x, y);
    Pixel& out = result[size_t(y)*w+x];
    out.rgb[0] = channel(p, image->red_mask);
    out.rgb[1] = channel(p, image->green_mask);
    out.rgb[2] = channel(p, image->blue_mask);
  }
  XDestroyImage(image);
  return result;
}
bool stamp(const std::vector<Pixel>& pixels, const Pixel& expected) {
  return !pixels.empty() && std::equal(expected.rgb, expected.rgb+3, pixels[0].rgb);
}
CoinRenderFramePlan grid(bool small) {
  const int nx=small?1:1000, ny=small?1:500;
  CoinRenderFramePlan f;
  f.materials.resize(1); f.lightingStates.emplace_back(); f.cameras.emplace_back();
  CoinRenderRenderStateSnapshot state; state.lightModel=CoinRenderLightModel::BASE_COLOR;
  state.cullMode=CoinRenderCullMode::NONE; f.renderStates.push_back(state);
  for (int y=0; y<=ny; ++y) for (int x=0; x<=nx; ++x) {
    CoinRenderVertexSnapshot v;
    v.position[0]=-.75f+1.5f*float(x)/nx;
    v.position[1]=-.75f+1.5f*float(y)/ny;
    v.normal[2]=1; f.vertices.push_back(v);
  }
  for (int y=0; y<ny; ++y) for (int x=0; x<nx; ++x) {
    const uint32_t a=uint32_t(y*(nx+1)+x), b=a+1, c=a+nx+1, d=c+1;
    f.indices.insert(f.indices.end(), {a,b,d,a,d,c});
  }
  CoinRenderDrawPacket draw;
  draw.geometry.vertexCount=uint32_t(f.vertices.size());
  draw.geometry.indexCount=uint32_t(f.indices.size()); f.draws.push_back(draw);
  f.viewports.emplace_back(); return f;
}
// Independent authored rectangle oracle. No offscreen render can inadvertently
// wait for the GPU before we observe the initial window frame.
bool rectangle(const std::vector<Pixel>& pixels, int w, int h, Pixel clear, int frame) {
  if (pixels.size()!=size_t(w)*h) return false;
  size_t mismatches=0; int maximum=0;
  for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) {
    Pixel expected=clear;
    if (x>=w/8 && x<7*w/8 && y>=h/8 && y<7*h/8) {
      expected={{0,0,0}}; expected.rgb[frame%2]=255;
    }
    bool mismatch=false;
    for (int c=0;c<3;++c) {
      int error=std::abs(int(pixels[size_t(y)*w+x].rgb[c])-int(expected.rgb[c]));
      maximum=std::max(maximum,error); mismatch|=error>1;
    }
    mismatches+=mismatch;
  }
  std::printf("first_present_pixels frame=%d mismatches=%zu max_rgb=%d\n",frame,mismatches,maximum);
  return !mismatches;
}
}
int main(int argc, char** argv) {
  bool small=false, canonical=false, overdraw=false, suppressSecond=false, managed=false, terrain=false, doubleSubmit=false, traceObserver=false;
  for (int i=1;i<argc;++i) {
    if (!std::strcmp(argv[i],"--small")) small=true;
    else if (!std::strcmp(argv[i],"--canonical")) canonical=true;
    else if (!std::strcmp(argv[i],"--overdraw")) overdraw=true;
    else if (!std::strcmp(argv[i],"--suppress-second-submit")) suppressSecond=true;
    else if (!std::strcmp(argv[i],"--managed")) managed=true;
    else if (!std::strcmp(argv[i],"--terrain")) terrain=true;
    else if (!std::strcmp(argv[i],"--legacy-double-submit")) doubleSubmit=true;
    else if (!std::strcmp(argv[i],"--trace-observer")) traceObserver=true;
    else return 2;
  }
  if ((terrain && (small || overdraw)) || (doubleSubmit && !terrain)) return 2;
  SoDB::init(); CoinRenderAction::initClass();
  std::string diagnostic;
  auto options=CoinRenderDiagnosticShell::renderOptions(diagnostic);
  if (!diagnostic.empty()) { std::puts(diagnostic.c_str()); return 2; }
  if (canonical) options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
  CoinRenderCapabilities caps{};
  if (coin_render_query_capabilities_for_renderer(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,
      options.renderer,&caps,sizeof(caps)) || !caps.gpu_available) return 77;
  Display* display=XOpenDisplay(nullptr); if (!display) return 77;
  Window window=XCreateSimpleWindow(display,DefaultRootWindow(display),32,32,1280,960,0,0,0);
  XSetWindowAttributes attrs{}; attrs.override_redirect=managed?False:True;
  XChangeWindowAttributes(display,window,CWOverrideRedirect,&attrs);
  XStoreName(display,window,"CoinRender B03 first presentation");
  XSelectInput(display,window,StructureNotifyMask|ExposureMask);
  XMapRaised(display,window); XSync(display,False);
  CoinRenderNativeSurfaceDescriptor desc{};
  desc.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION; desc.structSize=sizeof(desc);
  desc.type=COIN_RENDER_SURFACE_XLIB; desc.native.xlib.display=display; desc.native.xlib.window=window;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createWindow(desc,SbVec2i32(1280,960),options));
  if (!target || target->getStatus()!=CoinRenderTarget::TARGET_READY) return 1;
  std::printf("first_present_adapter name=%s renderer=%u vendor=0x%x device=0x%x policy=%s\n",
      caps.adapter_name,unsigned(caps.renderer),caps.vendor_id,caps.device_id,canonical?"canonical":"native");
  SoOrthographicCamera* terrainCamera=nullptr;
  SoSeparator* scene=terrain?coinRenderTerrainFixture(terrainCamera):nullptr;
  if (scene) scene->ref();
  CoinRenderAction action(SbViewportRegion(1280,960)); action.setRenderTarget(target.get());
  auto f=terrain?CoinRenderFramePlan():grid(small);
  if (terrain) f.viewports.emplace_back();
  std::printf("first_present_workload terrain=%d public_action=%d submissions_per_observation=%d\n",int(terrain),int(terrain),doubleSubmit?2:1);
  f.rasterizationPolicy=options.rasterizationPolicy;
  if(overdraw) f.draws.resize(16,f.draws.front());
  bool ok=true;
  for (int frame=0;frame<3 && ok;++frame) {
    const int w=frame==2?640:1280, h=frame==2?480:960;
    if (frame==2) {
      XResizeWindow(display,window,w,h); XSync(display,False);
      // A managed X11 resize is a request to the WM. XSync alone does not
      // acknowledge ConfigureRequest; submit only after the native extent.
      XWindowAttributes actual{}; XGetWindowAttributes(display,window,&actual);
      std::printf("first_present_resize_requested requested=%dx%d observed=%dx%d\n",w,h,actual.width,actual.height);
      const auto resizeBegin=Clock::now();
      while((actual.width!=w || actual.height!=h) && Clock::now()-resizeBegin<std::chrono::seconds(5)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        XGetWindowAttributes(display,window,&actual);
      }
      std::printf("first_present_resize_ack requested=%dx%d observed=%dx%d\n",w,h,actual.width,actual.height);
      if (actual.width!=w || actual.height!=h || !target->resize(SbVec2i32(w,h))) { ok=false; break; }
    }
    f.revision=frame+1; f.viewports[0].width=w; f.viewports[0].height=h;
    Pixel clear={{static_cast<unsigned char>(17+frame*32),33,49}};
    f.clearColor=SbColor4f(clear.rgb[0]/255.f,clear.rgb[1]/255.f,clear.rgb[2]/255.f,1);
    if(!terrain) for(int c=0;c<3;++c) f.materials[0].diffuse[c]=(c==frame%2)?1.f:0.f;
    else {
      const float angle=frame==1?1.f:.65f;
      terrainCamera->position=SbVec3f(180*std::sin(angle),120,180*std::cos(angle));
      terrainCamera->pointAt(SbVec3f(0,0,0));
      action.setViewportRegion(SbViewportRegion(w,h)); action.setBackgroundColor(f.clearColor);
    }
    const auto before=target->getLastSubmissionSerial();
    CoinRenderSubmitResult submitted;
    if (!(suppressSecond && frame==1)) {
      if (terrain) {
        for(int n=0;n<(doubleSubmit?2:1);++n) {
          action.apply(scene);
          if(action.getLastStatus()!=CoinRenderAction::SUCCESS) {
            submitted=CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR,action.getLastError().getString()); break;
          }
        }
      } else submitted=target->getPimpl().get().executeFrame(f);
    }
    const auto returned=Clock::now();
    if (submitted.status!=CoinRenderBackendStatus::SUCCESS) {
      std::puts(submitted.diagnostic.c_str()); ok=false; break;
    }
    const auto serial=target->getLastSubmissionSerial();
    const auto immediateBegin=Clock::now();
    auto immediate=capture(display,window,w,h);
    const auto immediateEnd=Clock::now();
    const bool immediateFresh=stamp(immediate,clear);
    size_t immediateVisible=0;
    for (const auto& pixel:immediate) immediateVisible+=pixel.rgb[0]||pixel.rgb[1]||pixel.rgb[2];
    std::printf("first_present_initial frame=%d nonblack_pixels=%zu total=%zu\n",frame,immediateVisible,immediate.size());
    if (traceObserver) {
      const auto ms=[&](Clock::time_point t) { return std::chrono::duration<double,std::milli>(t-returned).count(); };
      std::printf("first_present_observer frame=%d sample=immediate begin_ms=%.3f end_ms=%.3f fresh=%d serial=%llu\n",
        frame,ms(immediateBegin),ms(immediateEnd),int(immediateFresh),static_cast<unsigned long long>(serial));
      std::fflush(stdout);
    }
    // The historical consumer used a fixed 50ms delay. Record that observation
    // separately; neither it nor a late success silently overwrites the first.
    std::this_thread::sleep_until(returned+std::chrono::milliseconds(50));
    const auto fixedBegin=Clock::now();
    auto observed=capture(display,window,w,h);
    const auto fixedEnd=Clock::now();
    const bool fixedDelayFresh=stamp(observed,clear);
    if (traceObserver) {
      const auto ms=[&](Clock::time_point t) { return std::chrono::duration<double,std::milli>(t-returned).count(); };
      std::printf("first_present_observer frame=%d sample=fixed begin_ms=%.3f end_ms=%.3f fresh=%d serial=%llu\n",
        frame,ms(fixedBegin),ms(fixedEnd),int(fixedDelayFresh),static_cast<unsigned long long>(serial));
      std::fflush(stdout);
    }
    unsigned polls=0;
    while (!stamp(observed,clear) && Clock::now()-returned<std::chrono::seconds(5)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      observed=capture(display,window,w,h); ++polls;
    }
    const bool fresh=stamp(observed,clear);
    const bool stable=target->getLastSubmissionSerial()==serial && serial>before;
    const double ms=std::chrono::duration<double,std::milli>(Clock::now()-returned).count();
    std::printf("first_present frame=%d triangles=%zu extent=%dx%d immediate_fresh=%d fixed_50ms_fresh=%d eventual_fresh=%d elapsed_ms=%.3f polls=%u serial=%llu serial_unchanged=%d no_new_render=1\n",
      frame,terrain?size_t(1000000):f.indices.size()/3*f.draws.size(),w,h,int(immediateFresh),int(fixedDelayFresh),int(fresh),ms,polls,
      static_cast<unsigned long long>(serial),int(stable));
    ok=fresh && stable;
    if(terrain && ok) {
      // Reference is deliberately rendered only AFTER the first/fixed/late
      // X11 observations. It cannot synchronize an otherwise stale capture.
      std::unique_ptr<CoinRenderTarget> off(CoinRenderTarget::createOffscreen(SbVec2i32(w,h),options));
      CoinRenderAction reference(SbViewportRegion(w,h)); reference.setRenderTarget(off.get());
      reference.setBackgroundColor(f.clearColor); reference.apply(scene);
      std::vector<uint8_t> expected; off->readbackRGBA(expected);
      int maximum=0; size_t mismatches=0,foreground=0;
      ok=reference.getLastStatus()==CoinRenderAction::SUCCESS && expected.size()==observed.size()*4;
      if(ok) for(size_t i=0;i<observed.size();++i) {
        bool different=false,covered=false;
        for(int c=0;c<3;++c) {
          const int error=std::abs(int(observed[i].rgb[c])-int(expected[4*i+c]));
          maximum=std::max(maximum,error); different|=error!=0; covered|=expected[4*i+c]!=clear.rgb[c];
        }
        mismatches+=different; foreground+=covered;
      }
      ok&=mismatches==0 && foreground>size_t(w)*h/10;
      std::printf("first_present_terrain_pixels frame=%d mismatches=%zu max_rgb=%d foreground=%zu reference_after_capture=1 result=%s\n",frame,mismatches,maximum,foreground,ok?"PASS":"FAIL");
      reference.setRenderTarget(nullptr);
    } else if(!terrain && ok) ok=rectangle(observed,w,h,clear,frame);
    unsigned exposes=0,configures=0,reparents=0;
    while(XPending(display)) {XEvent event{};XNextEvent(display,&event);
      exposes+=event.type==Expose;configures+=event.type==ConfigureNotify;reparents+=event.type==ReparentNotify;}
    std::printf("first_present_events frame=%d expose=%u configure=%u reparent=%u\n",frame,exposes,configures,reparents);
  }
  action.setRenderTarget(nullptr); if(scene) scene->unref();
  target.reset(); XDestroyWindow(display,window); XCloseDisplay(display);
  std::printf("B03 single-submit observer %s\n",ok?"PASS":"FAIL");
  return ok?0:1;
}
