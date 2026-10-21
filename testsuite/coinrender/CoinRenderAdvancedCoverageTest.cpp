#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "CoinRenderCoverageRttFixtures.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderStrokeCore.h"
#include <Inventor/SoDB.h>
#include <Inventor/SbRotation.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <memory>
#include <vector>

#ifdef COIN_COVERAGE_XLIB
#include <X11/Xlib.h>
#include <X11/Xutil.h>
struct CoverageWindow {
  Display* display=nullptr;Window window=0;
  bool open(){display=XOpenDisplay(nullptr);if(!display)return false;
    window=XCreateSimpleWindow(display,DefaultRootWindow(display),80,80,32,32,0,0,0);
    XSetWindowAttributes attrs{};attrs.override_redirect=True;XChangeWindowAttributes(display,window,CWOverrideRedirect,&attrs);
    XStoreName(display,window,"CoinRender canonical coverage");XMapWindow(display,window);XSync(display,False);return window!=0;}
  void resize(int w,int h){XResizeWindow(display,window,w,h);XSync(display,False);}
  std::vector<uint8_t> capture(){XWindowAttributes attrs{};XGetWindowAttributes(display,window,&attrs);
    XImage* image=XGetImage(display,window,0,0,attrs.width,attrs.height,AllPlanes,ZPixmap);if(!image)return {};
    std::vector<uint8_t> pixels(size_t(attrs.width)*attrs.height*4,255);
    const auto channel=[](unsigned long pixel,unsigned long mask){if(!mask)return uint8_t(0);while(!(mask&1)){mask>>=1;pixel>>=1;}return uint8_t(((pixel&mask)*255+mask/2)/mask);};
    for(int y=0;y<attrs.height;++y)for(int x=0;x<attrs.width;++x){auto pixel=XGetPixel(image,x,y);size_t i=(size_t(y)*attrs.width+x)*4;
      pixels[i]=channel(pixel,image->red_mask);pixels[i+1]=channel(pixel,image->green_mask);pixels[i+2]=channel(pixel,image->blue_mask);}
    XDestroyImage(image);return pixels;}
  CoinRenderTarget* target(const CoinRenderOptions& options){CoinRenderNativeSurfaceDescriptor desc{};
    desc.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;desc.structSize=sizeof(desc);desc.type=COIN_RENDER_SURFACE_XLIB;
    desc.native.xlib.display=display;desc.native.xlib.window=window;
    return CoinRenderTarget::createWindow(desc,SbVec2i32(32,32),options);}
  ~CoverageWindow(){if(display){if(window)XDestroyWindow(display,window);XCloseDisplay(display);}}
};
#else
struct CoverageWindow {bool open(){return false;}void resize(int,int){}std::vector<uint8_t>capture(){return {};}CoinRenderTarget*target(const CoinRenderOptions&){return nullptr;}};
#endif

struct Point { double x,y; };
static CoinRenderFramePlan triangle(Point a,Point b,Point c,uint64_t revision) {
  CoinRenderFramePlan f;f.revision=revision;f.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
  f.clearColor=SbColor4f(0,0,0,1);f.materials.resize(1);f.materials[0].diffuse[0]=1;f.materials[0].diffuse[1]=f.materials[0].diffuse[2]=0;
  CoinRenderViewportSnapshot vp;vp.width=vp.height=32;f.viewports.push_back(vp);
  CoinRenderRenderStateSnapshot rs;rs.lightModel=CoinRenderLightModel::BASE_COLOR;rs.cullMode=CoinRenderCullMode::NONE;
  f.renderStates.push_back(rs);f.lightingStates.emplace_back();f.cameras.emplace_back();
  for(Point p:{a,b,c}){CoinRenderVertexSnapshot v;v.position[0]=float(p.x/16-1);v.position[1]=float(p.y/16-1);v.normal[2]=1;f.vertices.push_back(v);}
  f.indices={0,1,2};CoinRenderDrawPacket draw;draw.geometry.vertexCount=3;draw.geometry.indexCount=3;f.draws.push_back(draw);return f;
}
// Bottom-left fixed point oracle, independent of the CPU rasterizer's float
// barycentrics. Edges are quantized from authored coordinates, then evaluated
// exactly at pixel centers. Canonical coverage owns top/left edges.
static bool analytic(const CoinRenderFramePlan& f,const std::vector<uint8_t>& image) {
  struct P{int64_t x,y;};P v[3];
  const auto& vp=f.viewports[0];
  for(int i=0;i<3;++i){v[i].x=2*int64_t(std::floor((vp.x+(double(f.vertices[i].position[0])+1)*.5*vp.width)*128))+1;v[i].y=2*int64_t(std::floor((vp.y+(double(f.vertices[i].position[1])+1)*.5*vp.height)*128))+1;}
  const auto edge=[](P a,P b,P p){return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);};
  const int64_t area=edge(v[0],v[1],v[2]);
  bool front=area>0;if(f.renderStates[0].frontFace==CoinRenderFrontFace::CW)front=!front;
  const bool culled=(f.renderStates[0].cullMode==CoinRenderCullMode::BACK&&!front)||(f.renderStates[0].cullMode==CoinRenderCullMode::FRONT&&front);
  size_t mismatch=0;
  for(int y=0;y<32;++y)for(int x=0;x<32;++x){bool inside=!culled&&area!=0;P p{256*x+128,256*y+128};
    for(int i=0;i<3;++i){P a=v[i],b=v[(i+1)%3];if(area<0)std::swap(a,b);const auto e=edge(a,b,p);const bool owns=b.y<a.y||(b.y==a.y&&b.x<a.x);inside&=e>0||(e==0&&owns);}
    const size_t pixel=(31-y)*32+x;bool actual=false;for(int c=0;c<3;++c)actual|=image[pixel*4+c]!=uint8_t(std::lround(f.clearColor[c]*255));mismatch+=actual!=inside;}
  if(mismatch)std::printf("analytic mismatch revision=%llu pixels=%zu\n",(unsigned long long)f.revision,mismatch);
  return !mismatch;
}
int main(int argc,char**argv){
  bool cpuOnly=false,window=false;
  for(int i=1;i<argc;++i){if(!std::strcmp(argv[i],"--cpu"))cpuOnly=true;else if(!std::strcmp(argv[i],"--window"))window=true;else return 2;}
  if(cpuOnly&&window)return 2;
  SoDB::init();CoinRenderAction::initClass();
  std::string diagnostic;auto options=CoinRenderDiagnosticShell::renderOptions(diagnostic);options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
  std::unique_ptr<CoinRenderTarget> cpu(CoinRenderTarget::createOffscreen(SbVec2i32(32,32),options));cpu->getPimpl().get().backend.reset(new CoinRenderCpuReferenceBackend);
  CoverageWindow surface;
  std::unique_ptr<CoinRenderTarget> gpu;if(!cpuOnly){if(!CoinRenderTargetP::isGpuBackendAvailable())return 77;
    if(window&&!surface.open())return 77;
    gpu.reset(window?surface.target(options):CoinRenderTarget::createOffscreen(SbVec2i32(32,32),options));}
  bool externalWindowCapture=false;
  if(gpu){CoinRenderCapabilities caps{};
    if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))!=0||!caps.gpu_available)return 1;
    externalWindowCapture=window&&caps.backend==COIN_RENDER_EXPERIMENTAL_RUST&&caps.renderer==COIN_RENDER_RENDERER_OPENGL;
    if(window)std::printf("window_capture=%s\n",externalWindowCapture?"X11-presented-RGB (surface lacks COPY_SRC; alpha not observed)":"native-RGBA");
    std::printf("advanced_gpu adapter=%s renderer=%u vendor=0x%x device=0x%x\n",caps.adapter_name,unsigned(caps.renderer),unsigned(caps.vendor_id),unsigned(caps.device_id));}
  uint64_t revision=1;unsigned failures=0,cases=0;
  const auto check=[&](CoinRenderFramePlan f,bool oracle,const char*label){
    f.revision=revision++;
    if(externalWindowCapture){
      // Reserve a one-pixel clear border as an observed frame stamp. Waiting
      // only for this stamp prevents a matching stale image from passing.
      f.clearColor=SbColor4f(float(f.revision&255)/255.f,float((f.revision>>8)&255)/255.f,17.f/255.f,1);
      auto& vp=f.viewports[0];++vp.x;++vp.y;--vp.width;--vp.height;
    }
    uint8_t background[3];for(int c=0;c<3;++c)background[c]=uint8_t(std::lround(f.clearColor[c]*255));
    const auto foreground=[&](const std::vector<uint8_t>& pixels,size_t i){return pixels[i]!=background[0]||pixels[i+1]!=background[1]||pixels[i+2]!=background[2];};
    auto result=cpu->getPimpl().get().executeFrame(f);if(result.status!=CoinRenderBackendStatus::SUCCESS){std::puts(result.diagnostic.c_str());++failures;return;}
    std::vector<uint8_t>a,b;cpu->readbackRGBA(a);bool ok=!oracle||analytic(f,a);size_t mismatches=0;int maxError=0;double colorError=0;size_t colorSamples=0;float maximumDepthError=0;
    if(gpu){
      if(window&&!externalWindowCapture&&!gpu->requestWindowReadbackRGBA()){++failures;return;}
      result=gpu->getPimpl().get().executeFrame(f);if(result.status!=CoinRenderBackendStatus::SUCCESS){std::puts(result.diagnostic.c_str());++failures;return;}gpu->readbackRGBA(b);
      if(externalWindowCapture){
        bool fresh=false;
        for(int attempt=0;attempt<200;++attempt){b=surface.capture();fresh=b.size()==a.size();
          if(fresh)for(int c=0;c<3;++c)fresh&=b[c]==background[c];
          if(fresh)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
        ok&=fresh;std::printf("window_present_stamp revision=%llu fresh=%d\n",(unsigned long long)f.revision,int(fresh));
      }
      if(a.size()!=b.size()){++failures;return;}
      for(size_t i=0;i<a.size();i+=4){
        const bool foregroundA=foreground(a,i),foregroundB=foreground(b,i);
        mismatches+=foregroundA!=foregroundB;
        for(int c=0;c<4;++c){const int error=std::abs(int(a[i+c])-int(b[i+c]));maxError=std::max(maxError,error);
          if(c<3&&(foregroundA||foregroundB)){colorError+=error;++colorSamples;}}
      }
      if(!window){
        std::vector<float> cpuDepth,gpuDepth;cpu->readbackDepth(cpuDepth);gpu->readbackDepth(gpuDepth);
        ok&=cpuDepth.size()==a.size()/4&&gpuDepth.size()==a.size()/4;
        if(cpuDepth.size()==gpuDepth.size())for(size_t i=0;i<cpuDepth.size();++i)
          maximumDepthError=std::max(maximumDepthError,std::abs(cpuDepth[i]-gpuDepth[i]));
        ok&=maximumDepthError<=1e-5f;
      }
      // Retain the existing sampling contract (TextureSamplingTest max<=4,
      // MAE<=1.5) separately from exact geometric coverage. Untextured RGBA
      // keeps the one-level bound; saturated final-mip colors are exact.
      const int maximum=f.textures.empty()?1:4;
      ok&=!mismatches&&maxError<=maximum&&(!colorSamples||colorError/colorSamples<=1.5);
      if(std::string(label)=="texture-mip-LOD")for(size_t i=0;i<a.size();i+=4) {
        if(foreground(a,i))ok&=a[i]==0&&a[i+1]==0&&a[i+2]==255;
        if(foreground(b,i))ok&=b[i]==0&&b[i+1]==0&&b[i+2]==255;
      }
      if(std::string(label).find("depth-function-")==0){
        const auto& state=f.renderStates[1];const float ndc=(f.vertices[3].position[2]+1.f)*.5f;
        const float value=state.depthRange[0]+(state.depthRange[1]-state.depthRange[0])*ndc;
        bool blue=false;switch(state.depthFunction){
        case CoinRenderDepthFunction::NEVER:break;case CoinRenderDepthFunction::ALWAYS:blue=true;break;
        case CoinRenderDepthFunction::LESS:blue=value<.5f;break;case CoinRenderDepthFunction::LEQUAL:blue=value<=.5f;break;
        case CoinRenderDepthFunction::EQUAL:blue=value==.5f;break;case CoinRenderDepthFunction::GEQUAL:blue=value>=.5f;break;
        case CoinRenderDepthFunction::GREATER:blue=value>.5f;break;case CoinRenderDepthFunction::NOTEQUAL:blue=value!=.5f;break;}
        const size_t sample=((31-12)*32+20)*4;
        ok&=a[sample]==(blue?0:255)&&a[sample+2]==(blue?255:0)&&b[sample]==(blue?0:255)&&b[sample+2]==(blue?255:0);
      }
      if(oracle)ok&=analytic(f,b);
      if(std::string(label)=="async-ticket" && window) {
        CoinRenderReadbackTicket ticket{};const auto serial=gpu->getLastSubmissionSerial();
        result=gpu->getPimpl().get().executeFrameAsync(f,ticket);std::vector<uint8_t> after;gpu->readbackRGBA(after);
        ok&=result.status==CoinRenderBackendStatus::UNSUPPORTED&&ticket.token==0&&gpu->getLastSubmissionSerial()==serial&&(externalWindowCapture?after.empty():after==b);
      } else if(std::string(label)=="async-ticket") {
        std::vector<float> syncDepth;gpu->readbackDepth(syncDepth);
        CoinRenderReadbackTicket ticket{};
        result=gpu->getPimpl().get().executeFrameAsync(f,ticket);
        ok&=result.status==CoinRenderBackendStatus::SUCCESS&&ticket.token!=0;
        std::vector<uint8_t> pending,async;std::vector<float> asyncDepth;
        gpu->readbackRGBA(pending);ok&=pending.empty();
        auto status=CoinRenderTarget::READBACK_NOT_READY;
        for(int attempt=0;attempt<5000&&status==CoinRenderTarget::READBACK_NOT_READY;++attempt){
          status=CoinRenderTarget::pollReadback(ticket,async,asyncDepth);
          if(status==CoinRenderTarget::READBACK_NOT_READY)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ok&=status==CoinRenderTarget::READBACK_READY&&async==b&&asyncDepth==syncDepth;
        ok&=CoinRenderTarget::pollReadback(ticket,pending,asyncDepth)==CoinRenderTarget::READBACK_INVALID_TICKET;
      }
    }
    std::printf("advanced revision=%llu case=%s coverage_xor=%zu max_rgba=%d mae_rgb=%g max_depth=%g result=%s\n",(unsigned long long)f.revision,label,mismatches,maxError,colorSamples?colorError/colorSamples:0.,double(maximumDepthError),ok?"PASS":"FAIL");failures+=!ok;++cases;
  };
  for(Point corner:{Point{24.5,24.5},Point{20.5,12.5},Point{12.5,20.5},Point{20.5,4.5},Point{4.5,20.5}})
  for(bool reverse:{false,true})for(auto front:{CoinRenderFrontFace::CCW,CoinRenderFrontFace::CW})for(auto cull:{CoinRenderCullMode::NONE,CoinRenderCullMode::BACK,CoinRenderCullMode::FRONT}){
    auto f=triangle({8.5,8.5},{24.5,8.5},corner,revision);if(reverse)std::swap(f.indices[1],f.indices[2]);
    // Oracle consumes vertices in triangle order.
    if(reverse){std::swap(f.vertices[1],f.vertices[2]);f.indices={0,1,2};}
    f.renderStates[0].frontFace=front;f.renderStates[0].cullMode=cull;check(f,true,"diagonal/culling");
  }
  for(auto p:{Point{-96.5,8.5},Point{-8.5,8.5},Point{8.5,-96.5},Point{8.5,-8.5}}){auto f=triangle(p,{24.5,8.5},{24.5,24.5},revision);check(f,false,"viewport-clipping");}
  for(float z:{-2.f,-1.4f,0.f,1.7f,2.f}){auto f=triangle({8.5017,9.2571},{24.509,8.409},{23.479,25.117},revision);f.vertices[0].position[2]=z;check(f,false,"depth-clipping");}
  for(float w:{-.5f,.000005f,.5f,2.f}){auto f=triangle({8.5,8.5},{24.5,8.5},{24.5,24.5},revision);auto& rs=f.renderStates[0];rs.projectionCoin=SbMatrix(1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0);f.cameras[0].isPerspective=true;f.cameras[0].projectionMatrixCoin=rs.projectionCoin;
    for(int i=0;i<3;++i){float wi=i==0?w:1;f.vertices[i].position[0]*=wi;f.vertices[i].position[1]*=wi;f.vertices[i].position[2]=wi;}check(f,false,"eye-clipping");}
  for(float z:{-1.4f,1.7f}){
    auto f=triangle({8.5017,9.2571},{24.509,8.409},{23.479,25.117},revision);
    f.vertices[0].position[2]=z;f.materials[0].diffuse[3]=.5f;f.materials[0].transparency=.5f;
    f.renderStates[0].transparentMaterial=true;f.renderStates[0].transparencyType=SoGLRenderAction::SORTED_OBJECT_BLEND;
    check(f,false,"transparent-depth-clipping");
  }
  {
    auto f=triangle({0,0},{32,0},{16,32},revision);auto state=f.renderStates[0];auto draw=f.draws[0];
    state.lightModel=CoinRenderLightModel::PHONG;for(int c=0;c<3;++c)f.materials[0].ambient[c]=0;f.materials[0].emission[0]=1;
    f.renderStates.clear();f.draws.clear();
    for(int i=0;i<400;++i){auto rs=state;rs.model=SbMatrix(.04f,0,0,0,0,.04f,0,0,0,0,1,0,
        -.95f+float(i%20)*.1f,-.95f+float(i/20)*.1f,i%3==0?1.5f:i%3==1?-1.5f:0.f,1);
      f.renderStates.push_back(rs);auto d=draw;d.renderStateSlot=i;d.frameNodeOrdinal=i+1;f.draws.push_back(d);}
    check(f,false,"400-instanced-near-far");
  }
  for(auto function:{CoinRenderDepthFunction::NEVER,CoinRenderDepthFunction::ALWAYS,CoinRenderDepthFunction::LESS,CoinRenderDepthFunction::LEQUAL,CoinRenderDepthFunction::EQUAL,CoinRenderDepthFunction::GEQUAL,CoinRenderDepthFunction::GREATER,CoinRenderDepthFunction::NOTEQUAL})for(bool reverse:{false,true})for(float z:{0.f,.5f}){
    auto f=triangle({8.5,8.5},{24.5,8.5},{24.5,24.5},revision);
    f.renderStates[0].depthFunction=CoinRenderDepthFunction::ALWAYS;f.renderStates[0].explicitDepthMask=15;
    f.materials.resize(2);f.materials[1].diffuse[0]=f.materials[1].diffuse[1]=0;f.materials[1].diffuse[2]=1;
    for(int i=0;i<3;++i){auto v=f.vertices[i];v.position[2]=z;v.materialSlot=1;f.vertices.push_back(v);}
    f.indices.insert(f.indices.end(),{3,4,5});auto state=f.renderStates[0];state.materialSlot=1;state.depthFunction=function;
    if(reverse){state.depthRange[0]=.8f;state.depthRange[1]=.2f;}f.renderStates.push_back(state);
    auto draw=f.draws[0];draw.geometry.firstVertex=3;draw.geometry.firstIndex=3;draw.renderStateSlot=1;f.draws.push_back(draw);
    check(f,false,reverse?"depth-function-reversed-range":"depth-function-normal-range");
  }
  for(bool test:{false,true}){auto f=triangle({8.5,8.5},{24.5,8.5},{24.5,24.5},revision);
    f.renderStates[0].depthWrite=false;f.renderStates[0].depthTest=test;f.renderStates[0].explicitDepthMask=15;
    check(f,false,"depth-write-disabled");}
  for(auto topology:{CoinRenderPrimitiveTopology::LINE_LIST,CoinRenderPrimitiveTopology::POINT_LIST})for(float size:{1.f,2.5f,8.f}){
    auto f=triangle({8.5,8.5},{24.5,8.5},{24.5,24.5},revision);f.draws[0].topology=topology;
    f.renderStates[0].lineWidth=size;f.renderStates[0].pointSize=size;
    if(topology==CoinRenderPrimitiveTopology::LINE_LIST){f.indices={0,1};f.draws[0].geometry.indexCount=2;}
    std::string diagnostic;
    if(!coin_render_expand_strokes(f,diagnostic)){std::puts(diagnostic.c_str());++failures;continue;}
    for(const auto& d:f.draws)if(d.topology!=CoinRenderPrimitiveTopology::TRIANGLE_LIST)++failures;
    check(f,false,topology==CoinRenderPrimitiveTopology::LINE_LIST?"expanded-lines":"expanded-points");
  }
  for(auto filter:{CoinRenderTextureFilter::NEAREST,CoinRenderTextureFilter::LINEAR,CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR,CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR}){
    auto f=triangle({8.5017,8.499},{24.503,8.499},{24.503,24.503},revision);
    auto& state=f.renderStates[0];state.hasTexture=true;state.textureModel=CoinRenderTextureModel::REPLACE;
    CoinRenderTextureImageSnapshot texture;texture.width=texture.height=4;texture.mipmapped=true;
    for(int i=0;i<16;++i){const bool red=(i%4+i/4)%2;texture.pixelsRgba.insert(texture.pixelsRgba.end(),{uint8_t(red?255:0),uint8_t(red?0:255),0,255});}
    for(int i=0;i<4;++i)texture.mipmapsRgba.insert(texture.mipmapsRgba.end(),{0,255,0,255});texture.mipmapsRgba.insert(texture.mipmapsRgba.end(),{0,0,255,255});
    f.textures.push_back(texture);CoinRenderSamplerSnapshot sampler;sampler.filter=filter;f.samplers.push_back(sampler);
    const bool mip=int(filter)>=2;const float scale=mip?32.f:.8f;
    f.vertices[0].texcoord[0]=f.vertices[0].texcoord[1]=.13f;
    f.vertices[1].texcoord[0]=scale+.13f;f.vertices[1].texcoord[1]=.13f;
    f.vertices[2].texcoord[0]=f.vertices[2].texcoord[1]=scale+.13f;
    check(f,false,mip?"texture-mip-LOD":"texture-UV");
  }
  for(float delta:{-.001f,0.f,.003f}){auto f=triangle({8.5+delta,8.5},{24.5,8.5},{24.5,24.5},revision);check(f,true,"async-ticket");}
  // Deterministic clipping stress: asymmetric vertices avoid accidental exact
  // midpoints, covering all frustum planes and triangles crossing the eye.
  uint32_t seed=0xB01u;
  const auto next=[&](){seed=seed*1664525u+1013904223u;return double(seed>>8)/16777216.;};
  for(int i=0;i<64;++i){
    Point p[3];for(auto& v:p)v={next()*160-64,next()*160-64};
    auto f=triangle(p[0],p[1],p[2],revision);
    for(auto& v:f.vertices)v.position[2]=float(next()*6-3);
    check(f,false,"asymmetric-frustum");
  }
  if(gpu){
    if(window)surface.resize(256,256);
    if(!cpu->resize(SbVec2i32(256,256))||!gpu->resize(SbVec2i32(256,256)))return 1;
    auto f=triangle({0,0},{32,0},{16,32},revision);f.viewports[0].width=f.viewports[0].height=256;
    auto state=f.renderStates[0];auto draw=f.draws[0];state.lightModel=CoinRenderLightModel::PHONG;
    for(int c=0;c<3;++c)f.materials[0].ambient[c]=0;f.materials[0].emission[0]=1;
    SbMatrix view;view.setRotate(SbRotation(SbVec3f(.2f,.7f,1),.13f));view[3][0]=.015f;view[3][1]=-.025f;view[3][2]=-.08f;
    const SbMatrix projection(1.7320508f,0,0,0,0,1.7320508f,0,0,0,0,-1.0100503f,-1,0,0,-.20100503f,0);
    state.view=view;state.projectionCoin=projection;f.cameras[0].viewMatrix=view;f.cameras[0].projectionMatrixCoin=projection;
    f.renderStates.clear();f.draws.clear();
    for(int i=0;i<400;++i){auto rs=state;float sx=.051f+float(i%3)*.006f,sy=.043f+float(i%5)*.004f,sz=.055f+float(i%7)*.003f;
      rs.model=SbMatrix(sx,.009f,.004f,0,-.006f,sy,.008f,0,.012f,-.01f,sz,0,-.82f+float(i%20)*.086f,-.82f+float(i/20)*.086f,-1.25f-float(i%4)*.012f,1);
      f.renderStates.push_back(rs);auto d=draw;d.renderStateSlot=i;d.frameNodeOrdinal=i+1;f.draws.push_back(d);}
    check(f,false,"400-affine-perspective");
    if(window)surface.resize(32,32);if(!cpu->resize(SbVec2i32(32,32))||!gpu->resize(SbVec2i32(32,32)))return 1;
  }
  if(window){
    for(int side:{64,32}){
      surface.resize(side,side);if(!cpu->resize(SbVec2i32(side,side))||!gpu->resize(SbVec2i32(side,side))){++failures;continue;}
      auto f=triangle({8.5,8.5},{24.5,8.5},{24.5,24.5},revision);check(f,false,"window-resize-and-return");
    }
  }
  if(window){
    for(auto mode:{COIN_RENDER_SCENE_TEXTURE_STAGED,COIN_RENDER_SCENE_TEXTURE_DIRECT}){
      auto config=options;config.sceneTexture=mode;gpu.reset();gpu.reset(surface.target(config));
      std::unique_ptr<CoinRenderTarget> controlTarget(CoinRenderTarget::createOffscreen(SbVec2i32(32,32),config));
      auto* reference=CoinRenderCoverageRttFixtures::reference();auto* scene=CoinRenderCoverageRttFixtures::scene();
      SbViewportRegion viewport(32,32);if(externalWindowCapture)viewport.setViewportPixels(1,1,31,31);
      CoinRenderAction control(viewport),actual(viewport);control.setRenderTarget(controlTarget.get());actual.setRenderTarget(gpu.get());
      const uint64_t frameRevision=revision++;const SbColor4f background(externalWindowCapture?float(frameRevision&255)/255.f:0.f,0,externalWindowCapture?17.f/255.f:0.f,1);
      control.setBackgroundColor(background);actual.setBackgroundColor(background);
      control.apply(reference);bool ok=control.getLastStatus()==CoinRenderAction::SUCCESS;
      std::vector<uint8_t>a,b;controlTarget->readbackRGBA(a);
      if(!externalWindowCapture)ok&=bool(gpu->requestWindowReadbackRGBA());actual.apply(scene);ok&=actual.getLastStatus()==CoinRenderAction::SUCCESS;gpu->readbackRGBA(b);
      if(externalWindowCapture){bool fresh=false;for(int attempt=0;attempt<200;++attempt){b=surface.capture();fresh=b.size()==a.size();if(fresh)for(int c=0;c<3;++c)fresh&=b[c]==uint8_t(std::lround(background[c]*255));if(fresh)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}ok&=fresh;std::printf("window_present_stamp revision=%llu fresh=%d\n",(unsigned long long)frameRevision,int(fresh));}
      int maximum=0;ok&=a.size()==32*32*4&&b.size()==a.size();if(a.size()==b.size())for(size_t i=0;i<a.size();++i)maximum=std::max(maximum,std::abs(int(a[i])-int(b[i])));ok&=maximum<=1;
      if(!ok)std::printf("window RTT error: %s / %s\n",control.getLastError().getString(),actual.getLastError().getString());
      std::printf("window_rtt revision=%llu mode=%d max_rgba_error=%d result=%s\n",(unsigned long long)frameRevision,int(mode),maximum,ok?"PASS":"FAIL");failures+=!ok;++cases;reference->unref();scene->unref();
    }
  }
  std::printf("B01 advanced %s: %u cases failures=%u\n",failures?"FAIL":"PASS",cases,failures);return failures?1:0;
}
