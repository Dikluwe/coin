#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "CoinRenderCoverageRttFixtures.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>
#include <cstdlib>

#define CHECK(c,m) do { if (!(c)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,m); return 1; } } while(0)

struct Rectangle { double left,bottom,right,top; };
static CoinRenderFramePlan makeFrame(int width,int height,const int vp[4], Rectangle box,
                                    bool perspective, bool alpha, uint64_t revision, bool native=false) {
  CoinRenderFramePlan f; f.revision=revision;
  f.rasterizationPolicy=native?COIN_RENDER_RASTER_NATIVE:COIN_RENDER_RASTER_CANONICAL_8BIT;
  f.clearColor=SbColor4f(0,0,0,1); f.materials.resize(1);
  f.materials[0].diffuse[0]=1; f.materials[0].diffuse[1]=f.materials[0].diffuse[2]=0;
  f.materials[0].diffuse[3]=alpha?.5f:1.f; f.materials[0].transparency=alpha?.5f:0.f;
  CoinRenderViewportSnapshot viewport;
  viewport.x=vp[0];viewport.y=vp[1];viewport.width=vp[2];viewport.height=vp[3];f.viewports.push_back(viewport);
  CoinRenderRenderStateSnapshot state;
  state.lightModel=CoinRenderLightModel::BASE_COLOR; state.cullMode=CoinRenderCullMode::NONE;
  state.transparencyType=alpha?SoGLRenderAction::BLEND:SoGLRenderAction::NONE;
  state.transparentMaterial=alpha;
  if (perspective) {
    state.projectionCoin=SbMatrix(1,0,0,0, 0,1,0,0, 0,0,0,1, 0,0,0,0);
  } else state.projectionCoin=SbMatrix(1,0,0,0, 0,1,0,0, 0,0,0,0, 0,0,0,1);
  f.renderStates.push_back(state); f.lightingStates.emplace_back();
  CoinRenderCameraSnapshot camera;camera.isPerspective=perspective;camera.projectionMatrixCoin=state.projectionCoin;
  f.cameras.push_back(camera);
  const double x[4]={box.left,box.right,box.right,box.left};
  const double y[4]={box.bottom,box.bottom,box.top,box.top};
  const float ws[4]={1,2,4,2};
  for (int i=0;i<4;++i) {
    CoinRenderVertexSnapshot v;float w=perspective?ws[i]:1.f;
    v.position[0]=float(((x[i]-vp[0])*2/vp[2]-1)*w);
    v.position[1]=float(((y[i]-vp[1])*2/vp[3]-1)*w);v.position[2]=perspective?w:0;
    v.normal[2]=1;f.vertices.push_back(v);
  }
  f.indices={0,1,2,0,2,3};CoinRenderDrawPacket draw;draw.geometry.vertexCount=4;draw.geometry.indexCount=6;
  f.draws.push_back(draw);return f;
}
// Independent fixed-point rectangle oracle: no executor image or Core helper.
static bool expected(const std::vector<uint8_t>& image,const std::vector<float>& depth,
                     int width,int height,const int vp[4],Rectangle box,bool alpha,const char* label) {
  if(image.size()!=size_t(width*height*4)||depth.size()!=size_t(width*height))return false;
  const auto snap=[](double x){return 2*int64_t(std::floor(x*128))+1;};
  const int64_t l=snap(box.left),r=snap(box.right),b=snap(box.bottom),t=snap(box.top);
  unsigned errors=0,covered=0;
  for(int y=0;y<height;++y)for(int x=0;x<width;++x){
    const int64_t px=int64_t(x)*256+128,py=int64_t(y)*256+128;
    const bool inside=x>=vp[0]&&x<vp[0]+vp[2]&&y>=vp[1]&&y<vp[1]+vp[3]&&px>=l&&px<r&&py>b&&py<=t;
    const size_t i=size_t(height-1-y)*width+x; const int red=inside?(alpha?128:255):0;
    bool bad=std::abs(int(image[i*4])-red)>(alpha?1:0)||image[i*4+1]!=0||image[i*4+2]!=0||image[i*4+3]!=255;
    if(!alpha)bad=bad||std::abs(depth[i]-(inside?.5f:1.f))>1e-5f;
    if(bad&&errors++<4)std::fprintf(stderr,"%s (%d,%d) expected=%d rgba=%u,%u,%u,%u depth=%g\n",label,x,y,red,image[i*4],image[i*4+1],image[i*4+2],image[i*4+3],depth[i]);
    covered+=inside;
  }
  std::printf("coverage %s expected_pixels=%u errors=%u\n",label,covered,errors);
  return errors==0&&covered>0;
}

static bool rttCheck(CoinRenderOptions options) {
  auto* reference=CoinRenderCoverageRttFixtures::reference();
  bool ok=true;
  for(auto mode:{COIN_RENDER_SCENE_TEXTURE_STAGED,COIN_RENDER_SCENE_TEXTURE_DIRECT}){
    options.sceneTexture=mode;
    auto* scene=CoinRenderCoverageRttFixtures::scene();
    std::unique_ptr<CoinRenderTarget> actual(CoinRenderTarget::createOffscreen(SbVec2i32(32,32),options)),expectedTarget(CoinRenderTarget::createOffscreen(SbVec2i32(32,32),options));
    CoinRenderAction action(SbViewportRegion(32,32)),control(SbViewportRegion(32,32));action.setRenderTarget(actual.get());control.setRenderTarget(expectedTarget.get());
    action.apply(scene);control.apply(reference);std::vector<uint8_t>a,b;actual->readbackRGBA(a);expectedTarget->readbackRGBA(b);
    int maximum=0;
    if(action.getLastStatus()!=CoinRenderAction::SUCCESS||control.getLastStatus()!=CoinRenderAction::SUCCESS||a.empty()||a.size()!=b.size()){
      std::fprintf(stderr,"RTT mode=%d error=%s / %s\n",int(mode),action.getLastError().getString(),control.getLastError().getString());ok=false;
    }else{for(size_t i=0;i<a.size();++i)maximum=std::max(maximum,std::abs(int(a[i])-int(b[i])));ok=ok&&maximum<=1;}
    std::printf("coverage RTT mode=%d max_rgba_error=%d\n",int(mode),maximum);scene->unref();
  }
  reference->unref();return ok;
}

int main(int argc,char**argv){
  const bool cpu=argc==2&&std::strcmp(argv[1],"--cpu")==0;
  const bool native=argc==2&&std::strcmp(argv[1],"--native")==0;
  SoDB::init();CoinRenderAction::initClass();
  if(!cpu&&!CoinRenderTargetP::isGpuBackendAvailable())return 77;
  std::string optionDiagnostic; CoinRenderOptions options=CoinRenderDiagnosticShell::renderOptions(optionDiagnostic); options.rasterizationPolicy=native?COIN_RENDER_RASTER_NATIVE:COIN_RENDER_RASTER_CANONICAL_8BIT;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(32,32),options));CHECK(target,"target");
  if(cpu)target->getPimpl().get().backend.reset(new CoinRenderCpuReferenceBackend);
  auto & impl=target->getPimpl().get();unsigned cases=0;uint64_t revision=1;
  int width=32,height=32;int vp[4]={0,0,32,32};Rectangle box{8.5,8.5,24.5,24.5};
  std::vector<uint8_t> image;std::vector<float> depth;
  for(bool perspective:{false,true})for(double delta:{-1.0/1024,0.0,1.0/1024,3.0/1024,0.00177127})for(bool alpha:{false,true}){
    Rectangle q{box.left+delta,box.bottom+delta,box.right+delta,box.top+delta};
    auto f=makeFrame(width,height,vp,q,perspective,alpha,revision++,native);
    auto result=impl.executeFrame(f);CHECK(result.status==CoinRenderBackendStatus::SUCCESS,result.diagnostic.c_str());
    target->readbackRGBA(image);target->readbackDepth(depth);
    if (!cpu && cases==0) {
      CoinRenderCapabilities caps{};
      CHECK(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))==0 && caps.gpu_available,"GPU receipt");
      std::printf("coverage_gpu adapter=%s renderer=%u vendor=0x%x device=0x%x\n",caps.adapter_name,unsigned(caps.renderer),unsigned(caps.vendor_id),unsigned(caps.device_id));
    }
    char name[80];std::snprintf(name,sizeof(name),"%s delta=%g alpha=%d",perspective?"perspective":"ortho",delta,alpha);
    CHECK(expected(image,depth,width,height,vp,q,alpha,name),"fixed-point oracle");++cases;
  }
  CHECK(target->resize(SbVec2i32(64,32)),"resize");width=64;
  const int viewports[][4]={{8,0,32,32},{-8,0,32,32},{48,0,32,32}};
  for(const auto& v:viewports){
    Rectangle q{double(v[0])+8.5,8.5,double(v[0])+24.5,24.5};
    auto f=makeFrame(width,height,v,q,true,false,revision++,native);auto result=impl.executeFrame(f);
    CHECK(result.status==CoinRenderBackendStatus::SUCCESS,result.diagnostic.c_str());
    target->readbackRGBA(image);target->readbackDepth(depth);
    CHECK(expected(image,depth,width,height,v,q,false,"viewport/resize"),"viewport fixed-point oracle");++cases;
  }
  CHECK(target->resize(SbVec2i32(256,256)),"witness resize");width=height=256;
  const int witnessViewport[4]={0,0,256,256};Rectangle witness{180.5,20.5,195.50177127,40.5};
  auto witnessFrame=makeFrame(width,height,witnessViewport,witness,true,false,revision++,native);
  auto witnessResult=impl.executeFrame(witnessFrame);CHECK(witnessResult.status==CoinRenderBackendStatus::SUCCESS,witnessResult.diagnostic.c_str());
  target->readbackRGBA(image);target->readbackDepth(depth);
  CHECK(expected(image,depth,width,height,witnessViewport,witness,false,"B01 x=195.50177127"),"B01 edge witness");++cases;
  if (!native) {
    CHECK(target->resize(SbVec2i32(32,32)),"interpolation resize");width=height=32;
    auto interpolated=makeFrame(width,height,vp,box,true,false,revision++);
    interpolated.renderStates[0].projectionCoin[2][2]=.4f;
    interpolated.renderStates[0].projectionCoin[3][2]=.1f;
    interpolated.cameras[0].projectionMatrixCoin=interpolated.renderStates[0].projectionCoin;
    interpolated.materials.resize(4);
    const float colors[4][3]={{1,0,0},{0,1,0},{0,0,1},{1,1,1}};
    for (int i=0;i<4;++i) {interpolated.vertices[i].materialSlot=i;
      for(int c=0;c<3;++c)interpolated.materials[i].diffuse[c]=colors[i][c];}
    auto result=impl.executeFrame(interpolated);CHECK(result.status==CoinRenderBackendStatus::SUCCESS,result.diagnostic.c_str());
    target->readbackRGBA(image);target->readbackDepth(depth);
    for(int y:{12,18})for(int x:{12,18}) {
      const double u=(x+.5-8.50390625)/16, v=(y+.5-8.50390625)/16;
      double bary[4]={};
      if(u>=v){bary[0]=1-u;bary[1]=u-v;bary[2]=v;}
      else {bary[0]=1-v;bary[2]=u;bary[3]=v-u;}
      const double invW[4]={1,.5,.25,.5};double norm=0,expectedColor[3]={};
      for(int i=0;i<4;++i){norm+=bary[i]*invW[i];for(int c=0;c<3;++c)expectedColor[c]+=bary[i]*invW[i]*colors[i][c];}
      const size_t pixel=(height-1-y)*width+x;
      for(int c=0;c<3;++c)CHECK(std::abs(int(image[pixel*4+c])-int(std::lround(expectedColor[c]*255/norm)))<=1,"perspective color interpolation preserves W");
      CHECK(std::abs(depth[pixel]-(.7+.05*norm))<1e-5,"varying depth preserves clip Z/W");
    }
    // Restore the witness after the interpolation/resize control.
    CHECK(target->resize(SbVec2i32(256,256)),"restore witness size");width=height=256;
    witnessFrame.revision=revision++;CHECK(impl.executeFrame(witnessFrame).status==CoinRenderBackendStatus::SUCCESS,"restore witness");
    target->readbackRGBA(image);target->readbackDepth(depth);++cases;
  }
  const auto saved=image;const auto serial=target->getLastSubmissionSerial();
  auto bad=makeFrame(width,height,vp,box,false,false,revision++);bad.rasterizationPolicy=native?COIN_RENDER_RASTER_CANONICAL_8BIT:COIN_RENDER_RASTER_NATIVE;
  CHECK(impl.executeFrame(bad).status==CoinRenderBackendStatus::UNSUPPORTED,"policy mismatch rejects");
  target->readbackRGBA(image);CHECK(image==saved&&serial==target->getLastSubmissionSerial(),"rejection preserves publication");
  if (!native) {
    for (auto topology : {CoinRenderPrimitiveTopology::LINE_LIST, CoinRenderPrimitiveTopology::POINT_LIST}) {
      bad.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;bad.draws[0].topology=topology;
      CHECK(impl.executeFrame(bad).status==CoinRenderBackendStatus::UNSUPPORTED,"raw primitive refuses unsupported canonical contract");
      target->readbackRGBA(image);CHECK(image==saved&&serial==target->getLastSubmissionSerial(),"raw primitive rejection preserves publication");
    }
    witnessFrame.revision=revision++;CHECK(impl.executeFrame(witnessFrame).status==CoinRenderBackendStatus::SUCCESS,"recovery after unsupported combination");
    target->readbackRGBA(image);CHECK(image==saved&&target->getLastSubmissionSerial()>serial,"recovery republishes original image");
  }
  if(!cpu&&!native)CHECK(rttCheck(options),"RTT staged/direct vs independently authored image");
  std::printf("B01 %s PASS: %u image cases; executor=%s; alpha/depth/viewport/resize\n",native?"native control":"canonical odd-1/256",cases,cpu?"CPU":"GPU");return 0;
}
