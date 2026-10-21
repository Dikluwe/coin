
#include "CoinRenderCubeRttFixture.h"
using namespace cube_rtt_test;
static bool unchanged(Harness&h,const Frame&stable){
 Frame current;h.target->readbackRGBA(current.rgba);h.target->readbackDepth(current.depth);
 return check(h.target->getLastSubmissionSerial()==stable.serial&&exact(stable,current),
              "public CPU peer/refusal preserves root RGBA/depth/serial exactly");
}
static bool cpuAtlas(const CoinRenderFramePlan&frame,const Source&source){
 FaceOracle oracle(source,32);unsigned count=0,bad=0;bool ok=true;
 for(const auto&image:frame.textures)if(image.cubeFaceSize){
  ++count;const bool metadata=image.cubeFaceSize==32&&image.cubeSourceComponents==4&&
    !image.cubeMipmapped&&!image.gpuToken&&!image.producerId&&image.width==64&&image.height==96&&
    image.pixelsRgba.size()==64u*96u*4u;
  ok &=check(metadata,"late public CPU submission owns actual complete STAGED face atlas");
  if(metadata){size_t offset=0;for(unsigned face=0;face<6;++face)for(const auto&pixel:oracle.levels[0][face])for(unsigned c=0;c<4;++c)bad+=std::abs(int(image.pixelsRgba[offset++])-int(pixel[c]))>3;}
 }
 std::cout<<"cube_cpu_late_atlas resources="<<count<<" bad_channels="<<bad<<'\n';
 return check(count==1&&bad==0,"all six actual CPU face payloads agree with independent ray/texel oracle")&&ok;
}
static bool late(){
 Source source;Scene scene;scene.cube->scene=source.root;scene.enabled->whichChild=SO_SWITCH_ALL;
 Harness h(1,true,COIN_RENDER_SCENE_TEXTURE_STAGED);Frame a,b;if(!h.render(scene.root,a,"public factory CPU stable before late root fault")||!gradientCenters(a,FaceOracle(source,32),"CPU stable math"))return false;
 source.paint(1);h.proxy->failRootSubmit=true;const auto submits=h.proxy->submits;
 h.action.apply(scene.root);bool ok=check(h.action.getLastStatus()==CoinRenderAction::OUT_OF_MEMORY&&
  h.action.getLastError().getLength()>0&&h.proxy->submits==submits+1,
  "late injected root fault occurs after real CPU STAGED producer preparation")&&unchanged(h,a)&&cpuAtlas(h.proxy->submitted,source);
 // Genuine offscreen OOM latches TARGET_ERROR until explicit reconfiguration.
 // Clearing the private fault cannot itself recover the public target.
 const std::string faultError=h.target->getLastError();
 Proxy* const originalObserver=h.proxy;
 CoinRenderBackend* const originalFactoryBackend=h.proxy->real.get();
 ok &=check(h.target->getStatus()==CoinRenderTarget::TARGET_ERROR&&!faultError.empty(),
   "CPU OOM retains the documented terminal target state");
 h.proxy->failRootSubmit=false;source.paint(0);
 const unsigned blockedSubmits=h.proxy->submits,blockedProducers=h.proxy->producerCalls;
 h.action.apply(scene.root);
 ok &=check(h.action.getLastStatus()!=CoinRenderAction::SUCCESS&&
   h.target->getStatus()==CoinRenderTarget::TARGET_ERROR&&
   std::string(h.target->getLastError())==faultError&&
   h.proxy->submits==blockedSubmits&&h.proxy->producerCalls==blockedProducers,
   "retry without public reconfiguration remains blocked before backend publication")&&unchanged(h,a);
 const auto sameSize=h.target->getSize();
 ok &=check(h.target->resize(sameSize),"public same-size resize explicitly recovers terminal offscreen OOM");
 std::vector<uint8_t>resetColor(1,17);std::vector<float>resetDepth(1,-1.f);
 h.target->readbackRGBA(resetColor);h.target->readbackDepth(resetDepth);
 const bool sameObserver=h.target->getPimpl()->backend.get()==originalObserver&&
   h.proxy==originalObserver&&h.proxy->real.get()==originalFactoryBackend;
 ok &=check(h.target->getStatus()==CoinRenderTarget::TARGET_READY&&
   std::string(h.target->getLastError()).empty()&&h.target->getLastSubmissionSerial()==0&&
   resetColor.empty()&&resetDepth.empty()&&sameObserver&&!h.proxy->real->isGpuBackend(),
   "public resize clears publication and error while preserving the same observer and factory CPU backend");
 std::cout<<"cube_cpu_oom_reconfigure blocked_submits="<<(h.proxy->submits-blockedSubmits)
          <<" serial_after_resize="<<h.target->getLastSubmissionSerial()
          <<" same_observer_factory_backend="<<sameObserver<<'\n';
 ok &=h.render(scene.root,b,"public CPU late OOM resize recovery")&&
   gradientCenters(b,FaceOracle(source,32),"CPU recovery math")&&exact(a,b)&&
   check(h.target->getPimpl()->backend.get()==originalObserver&&
     h.proxy->real.get()==originalFactoryBackend,
     "exact RGBA/depth A/B/A recovery uses the original public target and CPU factory backend");
 return ok;
}
static bool refusals(){
 Source source;Scene scene;scene.cube->scene=source.root;scene.enabled->whichChild=SO_SWITCH_ALL;
 Harness h(1,true,COIN_RENDER_SCENE_TEXTURE_STAGED);Frame stable;
 if(!h.render(scene.root,stable,"public factory CPU stable refusal control")||!gradientCenters(stable,FaceOracle(source,32),"CPU refusal control math"))return false;
 bool ok=true;
 for(unsigned choice=0;choice<3;++choice){
  CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_UNKNOWN;
  options.sceneTexture=choice==0?COIN_RENDER_SCENE_TEXTURE_DIRECT:COIN_RENDER_SCENE_TEXTURE_STAGED;
  options.sampleCount=choice==1?2:choice==2?4:1;
  std::unique_ptr<CoinRenderTarget>candidate(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
  if(!check(bool(candidate),"public CPU unsupported-profile target reports its status"))return false;
  ok &=check(candidate->getOptions().renderer==COIN_RENDER_RENDERER_UNKNOWN&&
    candidate->getOptions().sampleCount==options.sampleCount&&candidate->getOptions().sceneTexture==options.sceneTexture,
    "unsupported requested CPU profile is never silently downgraded");
  CoinRenderAction attempt{SbViewportRegion(side,side)};attempt.setTransparencyType(CoinRenderAction::NONE);
  attempt.setRenderTarget(candidate.get());attempt.apply(scene.root);
  std::vector<uint8_t>rgba;std::vector<float>depth;candidate->readbackRGBA(rgba);candidate->readbackDepth(depth);
  std::cout<<"cube_cpu_unsupported route="<<options.sceneTexture<<" samples="<<options.sampleCount
           <<" status="<<attempt.getLastStatus()<<" serial="<<candidate->getLastSubmissionSerial()
           <<" error="<<attempt.getLastError().getString()<<'\n';
  ok &=check(attempt.getLastStatus()!=CoinRenderAction::SUCCESS&&attempt.getLastError().getLength()>0&&
    candidate->getLastSubmissionSerial()==0&&rgba.empty()&&depth.empty(),
    "CPU DIRECT or MSAA2/4 explicitly refuses without publishing any candidate color/depth/serial")&&unchanged(h,stable);
  attempt.setRenderTarget(nullptr);
 }
 const auto submits=h.proxy->submits,producers=h.proxy->producerCalls;
 CoinRenderReadbackTicket ticket{};h.action.applyAsync(scene.root,ticket);
 std::cout<<"cube_cpu_async status="<<h.action.getLastStatus()<<" ticket="<<ticket.token
          <<" error="<<h.action.getLastError().getString()<<'\n';
 ok &=check(h.action.getLastStatus()!=CoinRenderAction::SUCCESS&&h.action.getLastError().getLength()>0&&
   !ticket.token&&h.proxy->submits==submits&&h.proxy->producerCalls==producers,
   "CPU async refuses without a ticket or root/backend producer submission")&&unchanged(h,stable);
 Frame recovered;ok &=h.render(scene.root,recovered,"public CPU capability refusal recovery")&&exact(stable,recovered);
 return ok;
}
int main(int argc,char**argv){
 SoDB::init();CoinRenderAction::initClass();configureSamplingTest(argc,argv);bool lateMode=false;
 for(int i=1;i<argc;++i){const std::string arg=argv[i];if(arg=="--late")lateMode=true;else if(arg=="--refusals"||arg=="--recording"||arg=="--public-cpu"||arg=="--portable-sampling"){}else return 2;}
 CoinRenderCapabilities caps{};if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps)))return 1;
 if(!check(caps.backend==COIN_RENDER_EXPERIMENTAL_RECORDING&&!caps.gpu_available,"CPU factory focal requires RECORDING build without a GPU backend"))return 1;
 recordingFactoryMode=true;return (lateMode?late():refusals())?0:1;
}
