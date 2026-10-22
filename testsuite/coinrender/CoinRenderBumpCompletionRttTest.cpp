#include "CoinRenderBumpCompletionFixture.h"
#include "CoinRenderCubeRttFixture.h"
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <sstream>
using namespace bump_completion;
using bump_profile::Frame;
using bump_profile::Harness;
using bump_profile::check;
using bump_profile::side;
namespace {
template<class Work>bool stagedTrace(Work work,unsigned faces){
  const char*temporary=getenv("TMPDIR");std::string pattern=std::string(temporary&&*temporary?temporary:"/tmp")+"/coin-bump-rtt-trace-XXXXXX";std::vector<char>path(pattern.begin(),pattern.end());path.push_back('\0');const int fd=mkstemp(path.data());if(fd<0)return check(false,"STAGED trace temporary file available");unlink(path.data());std::fflush(stderr);const int saved=dup(STDERR_FILENO);if(saved<0||dup2(fd,STDERR_FILENO)<0){if(saved>=0)close(saved);close(fd);return check(false,"capture actual STAGED execution trace");}
  const bool result=work();std::fflush(stderr);const bool restored=dup2(saved,STDERR_FILENO)>=0;close(saved);std::string trace;char data[4096];if(lseek(fd,0,SEEK_SET)>=0){ssize_t got;while((got=read(fd,data,sizeof(data)))>0)trace.append(data,size_t(got));}close(fd);std::cerr<<trace;
  unsigned complete=0,wrongFaces=0,targets=0;std::istringstream lines(trace);std::string line;while(std::getline(lines,line)){targets+=line.find("COIN_RENDER_PHASE target ")!=std::string::npos;if(line.find("COIN_RENDER_PHASE rtt_producer_complete ")!=std::string::npos&&line.find("route=staged")!=std::string::npos){++complete;wrongFaces+=line.find("faces="+std::to_string(faces))==std::string::npos;}}
  std::cout<<"completion_staged_trace expected_faces="<<faces<<" producer_complete="<<complete<<" target_submissions="<<targets<<'\n';return result&&check(restored&&complete==(faces?1u:0u)&&!wrongFaces&&(faces?targets>=faces+1:targets==0),"real STAGED createOffscreen/executeFrame producer completion plus actual consumer submission, or atomic preflight refusal");
}
struct Graph {
  bump_completion::Scene consumer;
  bump_profile::Scene source2d;
  cube_rtt_test::Source sourceCube;
  SoSceneTexture2 *image=nullptr;
  SoSceneTextureCubeMap *cube=nullptr;
  unsigned family=0,n=32;double sourceFactor=1;
  Graph(unsigned kind,bool mixed):consumer(8,!mixed),family(kind){
    consumer.root->removeChild(consumer.base);consumer.base=nullptr;
    if(family){cube=new SoSceneTextureCubeMap;cube->scene=sourceCube.root;cube->size.setValue(n,n);cube->model=SoSceneTextureCubeMap::MODULATE;consumer.root->insertChild(cube,consumer.root->findChild(consumer.bump));auto*uv=new SoTextureCoordinate3;for(unsigned i=0;i<4;++i)uv->point.set1Value(i,SbVec3f(0,0,1));consumer.root->insertChild(uv,consumer.root->findChild(consumer.bump));}
    else {source2d.bump->image.setValue(SbVec2s(0,0),0,nullptr);auto*lm=new SoLightModel;lm->model=SoLightModel::BASE_COLOR;source2d.root->insertChild(lm,source2d.root->findChild(source2d.material));source2d.setDiffuse({{1,1,1}});const SbVec3f p[]={SbVec3f(-2,-2,0),SbVec3f(2,-2,0),SbVec3f(2,2,0),SbVec3f(-2,2,0)};source2d.positions->point.setValues(0,4,p);const uint8_t p2[]={23,173,61,255,211,37,137,255,89,219,31,255,177,63,233,255};source2d.base->image.setValue(SbVec2s(2,2),4,p2);source2d.base->wrapS=source2d.base->wrapT=SoTexture2::CLAMP;
      image=new SoSceneTexture2;image->scene=source2d.root;image->size.setValue(n,n);image->type=SoSceneTexture2::RGBA8;image->transparencyFunction=SoSceneTexture2::NONE;image->model=SoSceneTexture2::MODULATE;image->backgroundColor.setValue(0,0,0,1);consumer.root->insertChild(image,consumer.root->findChild(consumer.bump));for(unsigned i=0;i<4;++i)consumer.baseUv->point.set1Value(i,SbVec2f(.39f,.63f));}
  }
  void resize(unsigned value){n=value;if(cube)cube->size.setValue(n,n);else image->size.setValue(n,n);}
  RGB sample(bool srgb)const {RGB value{};if(family){const auto v=cube_rtt_test::FaceOracle(sourceCube,n).sample(SbVec3f(0,0,1),0,0);for(unsigned c=0;c<3;++c)value[c]=v[c]/255.;}
    else {const uint8_t pixel[]={89,219,31};for(unsigned c=0;c<3;++c)value[c]=std::lround(255*sourceFactor*(srgb?decode(pixel[c]/255.):pixel[c]/255.))/255.;}return value;}
};
bool metadata(const CoinRenderFramePlan&plan,const Graph&g,bool direct,bool srgb){const bool cube=g.family;const unsigned n=g.n;CoinWgpuFfiFrame ffi;std::string error;if(!check(ffi.prepareBumpFrame(plan,side,side,error),"actual RTT bump consumer packet "+error))return false;bool ok=check(ffi.getView().abi_version==67,"resolved RTT remains ABI67");unsigned found=0;
  for(size_t i=0;i<plan.textures.size();++i){const auto&t=plan.textures[i];if(cube?!t.cubeFaceSize:(!t.gpuToken&&t.pixelsRgba.size()!=size_t(n)*n*4))continue;if(cube&&t.cubeFaceSize!=n)continue;++found;ok &=check(t.format==CoinRenderTextureFormat::RGBA8_LINEAR&&!t.producerId,"resolved resource is current RGBA8_LINEAR without producer handle");if(cube){const auto&packet=ffi.getView().textures[i];ok &=check(!(packet.reserved&512u)&&bool(packet.reserved&256u)==direct,"private token metadata keeps bit9 clear");}ok &=check(direct?(t.gpuToken&&t.pixelsRgba.empty()):(!t.gpuToken&&!t.pixelsRgba.empty()),"actual factory DIRECT token / STAGED pixels");
    if(!direct){unsigned bad=0;size_t offset=0;if(cube){const cube_rtt_test::FaceOracle want(g.sourceCube,n);for(unsigned level=0;level<(t.cubeMipmapped?want.count:1u);++level)for(unsigned f=0;f<6;++f)for(const auto&p:want.levels[level][f])for(unsigned c=0;c<4;++c){if(offset>=t.pixelsRgba.size()){++bad;continue;}bad+=std::abs(int(t.pixelsRgba[offset++])-int(std::lround(p[c])))>3;}for(;offset<t.pixelsRgba.size();++offset)bad+=t.pixelsRgba[offset]!=0;}
      else {const uint8_t colors[][3]={{23,173,61},{211,37,137},{89,219,31},{177,63,233}};for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){const unsigned q=(x>=n/2?1:0)+(y>=n/2?2:0);for(unsigned c=0;c<4;++c){const unsigned v=c==3?255:unsigned(std::lround(255*g.sourceFactor*(srgb?decode(colors[q][c]/255.):colors[q][c]/255.)));if(offset>=t.pixelsRgba.size()){++bad;continue;}bad+=std::abs(int(t.pixelsRgba[offset++])-int(v))>3;}}}ok &=check(bad==0,"STAGED current atlas bytes independently match all source faces/texels");}}
  return check(found==1,"one actual resolved consumer resource")&&ok;
}
bool campaign(unsigned samples,bool srgb,CoinRenderSceneTextureMode route){Harness h(samples,srgb,route);if(!h.target)return false;auto*proxy=complete(h);bool ok=true;
  for(unsigned family:{0u,1u})for(bool mixed:{false,true}){Graph g(family,mixed);Frame a,b,back;const bool direct=route==COIN_RENDER_SCENE_TEXTURE_DIRECT;
    auto render=[&](Frame&f,const char*label){const unsigned producers=proxy->producers;const bool rendered=direct?h.render(g.consumer.root,f,label):stagedTrace([&](){return h.render(g.consumer.root,f,label);},family?6u:1u);const auto e=g.sample(srgb);bool passed=rendered;if(direct)passed &=check(proxy->producers>producers,"actual DIRECT backend consumes producer submissions");passed &=oracle(g.consumer,f,[&](double,double){return e;});passed &=metadata(proxy->plan,g,direct,srgb);return passed;};
    ok &=render(a,"rendered base A consumes eight actual lights");if(a.rgba.empty())continue;
    if(family)g.sourceCube.paint(1);
    if(family){ok &=render(b,"rendered cube producer B");g.sourceCube.paint(0);}
    else {g.source2d.base->model=SoTexture2::MODULATE;g.source2d.setDiffuse({{.5,.5,.5}});g.sourceFactor=.5;ok &=render(b,"rendered2D producer material B")&&check(a.rgba!=b.rgba,"producer material B consumed");g.source2d.setDiffuse({{1,1,1}});g.sourceFactor=1;}
    ok &=render(back,"producer A/B/A")&&exact(a,back);g.resize(64);ok &=render(b,"resized producer current resource");g.resize(32);ok &=render(back,"producer size A/B/A")&&exact(a,back);
    // A malformed consumer must be refused before any real producer starts.
    g.consumer.matrix->translation.setValue(std::numeric_limits<float>::quiet_NaN(),0);ok &=direct?h.reject(g.consumer.root,back,"malformed consumer with valid RTT source"):stagedTrace([&](){return h.reject(g.consumer.root,back,"malformed consumer with valid RTT source");},0);g.consumer.matrix->translation.setValue(0,0);ok &=render(b,"malformed consumer recovery")&&exact(a,b);
    if(family&&direct){const auto before=h.target->getLastSubmissionSerial();proxy->failedCandidate=0;proxy->failAssembly=true;h.action.apply(g.consumer.root);Frame refused;h.target->readbackRGBA(refused.rgba);h.target->readbackDepth(refused.depth);ok &=check(h.action.getLastStatus()!=CoinRenderAction::SUCCESS&&proxy->failedCandidate&&std::find(proxy->retired.begin(),proxy->retired.end(),proxy->failedCandidate)!=proxy->retired.end()&&h.target->getLastSubmissionSerial()==before&&refused.rgba==a.rgba&&refused.depth==a.depth,"real returned failure candidate retired by actual owner, no partial RTT publication");proxy->failAssembly=false;ok &=render(b,"real candidate failure immediate recovery")&&exact(a,b);}
  }
  return ok;
}
}
int main(int argc,char**argv){setenv("COIN_RENDER_TRACE_PHASES","1",1);SoDB::init();CoinRenderAction::initClass();configureSamplingTest(argc,argv);unsigned samples=1;bool srgb=false;auto route=COIN_RENDER_SCENE_TEXTURE_STAGED;for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--samples"&&i+1<argc)samples=std::atoi(argv[++i]);else if(a=="--srgb")srgb=true;else if(a=="--direct")route=COIN_RENDER_SCENE_TEXTURE_DIRECT;}if(samples!=1&&samples!=2&&samples!=4)return 2;CoinRenderCapabilities caps{};if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps)))return 1;if(!caps.gpu_available)return 77;return campaign(samples,srgb,route)?0:1;}
