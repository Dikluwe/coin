
#include "CoinRenderCubeRttFixture.h"
using namespace cube_rtt_test;
static size_t ownPeak(const CoinRenderRttPlan&graph){
 size_t retained=0,peak=0;for(const auto&producer:graph.producers){peak=std::max(peak,retained+(producer.cube?7u:1u));++retained;}return peak;
}
static bool coreCapacity(){
 bool ok=true;
 for(unsigned preceding2D:{0u,3u})for(unsigned cubes:{1u,14u,55u,56u,58u,59u}){
  CoinRenderRttPlan graph(COIN_RENDER_SCENE_TEXTURE_DIRECT);
  for(unsigned i=0;i<preceding2D;++i){CoinRenderRttProducer producer;producer.size=SbVec2i32(1,1);graph.producers.push_back(producer);}
  for(unsigned i=0;i<cubes;++i){CoinRenderRttProducer producer;producer.cube=true;producer.capturedCubeFaces=6;producer.size=SbVec2i32(1,1);graph.producers.push_back(producer);}
  const size_t expected=preceding2D+cubes+6;ok &=check(graph.tokenCount()==expected&&graph.tokenCount()==ownPeak(graph),"Core capacity is topological peak, not cumulative seven records per cube");
 }
 for(unsigned tail:{6u,7u}){CoinRenderRttPlan graph(COIN_RENDER_SCENE_TEXTURE_DIRECT);for(unsigned i=0;i<58;++i){CoinRenderRttProducer cube;cube.cube=true;cube.capturedCubeFaces=6;cube.size=SbVec2i32(1,1);graph.producers.push_back(cube);}for(unsigned i=0;i<tail;++i){CoinRenderRttProducer twoD;twoD.size=SbVec2i32(1,1);graph.producers.push_back(twoD);}
  ok &=check(graph.tokenCount()==std::max(size_t(64),size_t(58+tail))&&graph.tokenCount()==ownPeak(graph),"2D records after the last cube preserve order-sensitive peak");}
 CoinRenderRttPlan empty(COIN_RENDER_SCENE_TEXTURE_DIRECT);return check(empty.tokenCount()==0,"empty graph consumes no temporary or retained records")&&ok;
}
static bool flatMath(const Frame&frame){
 unsigned bad=0;for(unsigned cell=0;cell<54;++cell){const int x=int(std::lround(side*.5+(-3.2+.8*(cell%9))*24-.5)),y=int(std::lround(side*.5-(2.-.8*(cell/9))*24-.5));const size_t offset=(size_t(y)*side+unsigned(x))*4;const unsigned expected[]={51,102,153,255};for(unsigned channel=0;channel<4;++channel)bad+=std::abs(int(frame.rgba[offset+channel])-int(expected[channel]))>3;}
 std::cout<<"cube_capacity_math bad="<<bad<<'\n';return check(!bad,"near-capacity final cube publishes its own authored clear RGBA on all six faces");
}
struct ExternalHandles{
 std::vector<uint64_t>tokens;
 ~ExternalHandles(){
#ifdef COIN_CUBE_RTT_TEST_RUST
 for(auto token:tokens)coin_wgpu_release_texture(token);
#endif
 }
};
static bool externalHandles(Harness&h,unsigned count,ExternalHandles&resources){
#ifdef COIN_CUBE_RTT_TEST_RUST
 const unsigned capacity=coin_wgpu_default_rtt_capacity();bool ok=check(capacity==64,"clean default pool exposes all sixty-four real slots");
 auto frame=h.proxy->submitted;for(auto&viewport:frame.viewports){viewport.x=viewport.y=0;viewport.width=viewport.height=1;}
 CoinWgpuFfiFrame ffi;std::string diagnostic;if(!check(ffi.prepare(frame,1,1,diagnostic,false),"external raw frame is a valid independently owned 1x1 texture: "+diagnostic))return false;auto view=ffi.getView();
 for(unsigned i=0;i<count;++i){unsigned char bytes[4]={};CoinWgpuTarget target{};target.width=target.height=1;target.sample_count=1;target.color_buffer=bytes;target.color_buffer_len=sizeof(bytes);uint64_t token=0;char error[1024]={};auto status=coin_wgpu_submit_texture(&target,&view,&token,error,sizeof(error));if(token)resources.tokens.push_back(token);ok &=check(status==COIN_WGPU_OK&&token,"external real handle allocated without changing the root target");}
 return check(coin_wgpu_default_rtt_capacity()==capacity-count,"live external resources reduce actual available slots exactly")&&ok;
#else
 (void)h;(void)count;(void)resources;return check(false,"external raw handle control requires Rust bridge entry points");
#endif
}
static bool capacity(unsigned samples,unsigned twoD,unsigned external,bool admissionOnly){
 Scene scene;scene.cube->size.setValue(1,1);scene.cube->backgroundColor.setValue(.2f,.4f,.6f);
 auto*source=new SoSeparator;scene.cube->scene=source;
 auto*prefix=new SoGroup;scene.enabled->insertChild(prefix,0);
 auto addCube=[&](unsigned index){auto*cube=new SoSceneTextureCubeMap;cube->size.setValue(1,1);cube->scene=source;cube->backgroundColor.setValue(float(.001*(index+1)),.02f,.03f);prefix->addChild(cube);return cube;};
 for(unsigned i=0;i<twoD;++i){auto*node=new SoSceneTexture2;node->scene=source;node->size.setValue(1,1);node->backgroundColor.setValue(.01f,float(.001*(i+1)),.03f,1.f);prefix->addChild(node);}
 // Every previous atlas remains live through the final consumer. During the
 // last assembly there are 57 previous records, six faces and the new atlas.
 const unsigned cubes=58-twoD-external;
 for(unsigned i=1;i<cubes;++i)addCube(i);
 Harness h(samples,false,COIN_RENDER_SCENE_TEXTURE_DIRECT);Frame ordinary,a,b;if(!h.render(scene.root,ordinary,"capacity ordinary stable root"))return false;
 ExternalHandles resources;if(external&&!externalHandles(h,external,resources))return false;
 scene.enabled->whichChild=SO_SWITCH_ALL;bool ok=true;
 if(admissionOnly){h.proxy->stopAfterAdmission=true;ok &=h.reject(scene.root,ordinary,"legal peak admission-only boundary")&&check(std::string(h.action.getLastError().getString())=="independent successful admission-only stop","real backend admitted legal peak before diagnostic stop");a=ordinary;}
 else ok &=h.render(scene.root,a,"DIRECT many coexisting logical cube resources")&&flatMath(a);
 ok &=check(ownPeak(h.proxy->graph)==64-external&&h.proxy->graph.tokenCount()==ownPeak(h.proxy->graph),"captured public graph reports exact independent order-sensitive peak");
 auto*additional=addCube(61);(void)additional;
#ifdef COIN_CUBE_RTT_TEST_RUST
 const auto rootSerial=h.target->getLastSubmissionSerial();const unsigned calls=h.proxy->producerCalls,submits=h.proxy->submits;
 ok &=h.reject(scene.root,a,"one additional retained cube exceeds actual wgpu capacity")&&check(h.proxy->producerCalls==calls&&h.proxy->submits==submits&&h.target->getLastSubmissionSerial()==rootSerial,"over-capacity graph refuses before any face or root submission");
 ok &=check(std::string(h.action.getLastError().getString())!="independent successful admission-only stop","over-capacity refusal came from admission, not diagnostic stop");
#else
 // BGFX has no software registry cap of sixty-four. Both graph sizes are
 // legal; sixty-four/five describe their peak, not a BGFX admission limit.
 if(admissionOnly){ok &=h.reject(scene.root,a,"BGFX additional cube admission-only")&&check(std::string(h.action.getLastError().getString())=="independent successful admission-only stop","BGFX admits the valid sixty-five-record peak");b=a;}
 else ok &=h.render(scene.root,b,"BGFX additional valid retained cube")&&flatMath(b)&&exact(a,b);
 ok &=check(ownPeak(h.proxy->graph)==65&&h.proxy->graph.tokenCount()==65,"BGFX valid additional cube preserves exact order-sensitive Core metadata");
 additional->size.setValue(0,1);
 ok &=h.reject(scene.root,b,"BGFX actual invalid cube dimension")&&check(std::string(h.action.getLastError().getString())!="independent successful admission-only stop","real malformed cube refuses before diagnostic stop");
 additional->size.setValue(1,1);
 Frame validAgain;
 if(admissionOnly)ok &=h.reject(scene.root,b,"BGFX dimension recovery admission-only")&&check(std::string(h.action.getLastError().getString())=="independent successful admission-only stop","BGFX valid additional cube re-admits after malformed dimension");
 else ok &=h.render(scene.root,validAgain,"BGFX additional cube malformed A/B/A")&&flatMath(validAgain)&&exact(b,validAgain);
#endif
 prefix->removeChild(prefix->getNumChildren()-1);
 if(admissionOnly)ok &=h.reject(scene.root,a,"exact original descriptor recovery")&&check(std::string(h.action.getLastError().getString())=="independent successful admission-only stop","original legal graph re-admits");
 else ok &=h.render(scene.root,b,"capacity exact original recovery")&&flatMath(b)&&exact(a,b);
 return ok;
}
int main(int argc,char**argv){
 SoDB::init();CoinRenderAction::initClass();configureSamplingTest(argc,argv);unsigned samples=1,twoD=0,external=0;bool core=false,admission=false;
 for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--samples"&&i+1<argc){if(!unsignedOption(argv[++i],samples))return 2;}else if(arg=="--mixed")twoD=3;else if(arg=="--external")external=2;else if(arg=="--core")core=true;else if(arg=="--admission-only")admission=true;else if(arg=="--direct"||arg=="--portable-sampling"||arg=="--capacity"){}else{std::cerr<<"unknown or incomplete option: "<<arg<<'\n';return 2;}}
 if(samples!=1&&samples!=2&&samples!=4)return 2;if(core)return coreCapacity()?0:1;
 CoinRenderCapabilities caps{};if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps)))return 1;if(!caps.gpu_available)return 77;
 return capacity(samples,twoD,external,admission)?0:1;
}
