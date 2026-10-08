#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
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

static bool check(bool value, const char* label) {
  if (!value) std::cerr << "FAIL " << label << '\n';
  return value;
}
static bool selection(bool gpu) {
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
  if (!check(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))==0,"v4 runtime capabilities")) return false;
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
  const bool gpu=argc>1 && std::string(argv[1])=="--gpu";
  bool ok=selection(gpu);
  if (gpu && !CoinRenderAction::isGpuBackendAvailable()) return 77;
  std::string diagnostic;auto nativeOptions=CoinRenderDiagnosticShell::renderOptions(diagnostic);
  if (!diagnostic.empty()) {std::cerr<<diagnostic<<'\n';return 1;}
  auto portableOptions=nativeOptions;portableOptions.textureSamplingPolicy=COIN_RENDER_SAMPLING_PORTABLE;
  std::unique_ptr<CoinRenderTarget> native(CoinRenderTarget::createOffscreen(SbVec2i32(64,64),nativeOptions));
  std::unique_ptr<CoinRenderTarget> portable(CoinRenderTarget::createOffscreen(SbVec2i32(64,64),portableOptions));
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
    selected.apply(root);
    return check(selected.getLastStatus()==CoinRenderAction::SUCCESS,"explicit-policy frame renders");
  };
  ok &= render(native.get());native->readbackRGBA(nativeFirst);
  for(int textureUnit : {0,7,0}) {
    unit->unit=textureUnit;
    ok &= render(portable.get());portable->readbackRGBA(portablePixels);
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
    std::vector<uint8_t> retained;portable->readbackRGBA(retained);std::size_t retainedBytes=0;
    ok &= check(rejected.status==CoinRenderBackendStatus::UNSUPPORTED && rejected.diagnostic.find("isotropic")!=std::string::npos && retained==portablePixels && portable->getLastSubmissionSerial()==serial && portable->borrowRGBA(retainedBytes)==borrowed && retainedBytes==borrowedBytes,"unsupported combination preserves pixels, serial and borrowed pointer");
    valid.revision+=200000;
    ok &= check(portable->getPimpl()->executeFrame(valid).status==CoinRenderBackendStatus::SUCCESS,"recovery after rejected sampler");
  }
  unit->unit=0;
  ok &= render(native.get());native->readbackRGBA(nativeAgain);
  ok &= check(nativeAgain==nativeFirst,"portable target cannot contaminate native shader/cache");
  quality->textureQuality=0;ok &= render(portable.get());
  quality->textureQuality=.5f;ok &= render(portable.get());
  if (gpu) {
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
  ok &= check(portable->resize(SbVec2i32(128,128)),"portable target resize");
  action.setViewportRegion(SbViewportRegion(128,128));ok &= render(portable.get());
  portable->readbackRGBA(portablePixels);
  if (portablePixels.size()==128*128*4) {
    for(int x : {62,63}) ok &= check(std::abs(int(portablePixels[(64*128+x)*4])-(x==62?40:160))<=1,"resized derivative footprint oracle");
  } else ok=false;
  action.setRenderTarget(nullptr);nativeAction.setRenderTarget(nullptr);root->unref();
  std::cout<<"Sampling API CPU="<<!gpu<<" result="<<(ok?"PASS":"FAIL")<<'\n';
  return ok?0:1;
}
