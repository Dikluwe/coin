#include "CoinRenderCubeRttSourceFixture.h"
#include <cctype>
#include "CoinRenderCubeRttCurrentResourceCases.h"
#include <thread>
using namespace cube_rtt_test;
namespace {
using Cpu=CoinRenderCpuReferenceBackend;
struct FaultReset { ~FaultReset(){Cpu::setSubmissionFaultCountdown(-1);} };
bool retained(Harness&h,const Frame&a){Frame b;h.target->readbackRGBA(b.rgba);h.target->readbackDepth(b.depth);return check(h.target->getLastSubmissionSerial()==a.serial&&a.rgba==b.rgba&&a.depth==b.depth,"native CPU failure preserves prior root RGBA/depth/serial");}
bool injected(const SbString&error){std::string text=error.getString();std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return char(std::tolower(c));});return text.find("inject")!=std::string::npos&&text.find("cpu")!=std::string::npos&&text.find("submi")!=std::string::npos;}
bool sixth(bool nested){
 FaultReset reset;Source source,child;Scene scene;scene.cube->scene=source.root;scene.enabled->whichChild=SO_SWITCH_ALL;
 if(nested)dependency(source,child,16,.1f);
 Harness h(1,true,COIN_RENDER_SCENE_TEXTURE_STAGED);Frame a,b,back;
 if(!h.render(scene.root,a,"native CPU fault stable A"))return false;
 const auto*backend=h.proxy->real.get();const auto owner=h.target->getPimpl()->resourceOwnerId;
 const auto published=h.proxy->submitted;const auto roots=h.proxy->submits;
 source.paint(1);if(nested){child.paint(1);dependencyFactor(source,child,16,.1f);}
 Cpu::setSubmissionFaultCountdown(nested?11:5);
 bool ok=retained(h,a);h.action.apply(scene.root);
 ok &=check(h.action.getLastStatus()==CoinRenderAction::OUT_OF_MEMORY&&injected(h.action.getLastError())&&Cpu::submissionFaultSuccessCount()==uint64_t(nested?11:5),"sixth real CPU face submission fails after exactly five completed faces (plus completed dependency)");
 ok &=check(h.proxy->submits==roots&&h.proxy->submitted.hasSamePayload(published),"failing sixth face never submits root or exposes a partial assembled atlas")&&retained(h,a);
 ok &=check(h.target->getStatus()==CoinRenderTarget::TARGET_READY&&h.proxy->real.get()==backend&&h.target->getPimpl()->resourceOwnerId==owner&&!backend->isGpuBackend(),"child OOM retains same healthy public RECORDING target and actual CPU factory backend");
 Cpu::setSubmissionFaultCountdown(-1);
 ok &=h.render(scene.root,b,"native CPU B immediately recovers after child OOM")&&check(b.rgba!=a.rgba,"all-consumer B mutation must be visible");
 source.paint(0);if(nested){child.paint(0);dependencyFactor(source,child,16,.1f);}
 ok &=h.render(scene.root,back,"native CPU original A recovery")&&exact(a,back)&&gradientCenters(back,FaceOracle(source,32),"restored six faces");
 return ok;
}
bool rootFailure(){
 FaultReset reset;Source source;Scene scene;scene.cube->scene=source.root;scene.enabled->whichChild=SO_SWITCH_ALL;Harness h(1,true,COIN_RENDER_SCENE_TEXTURE_STAGED);Frame a,b;
 if(!h.render(scene.root,a,"native CPU root OOM A"))return false;
 source.paint(1);Cpu::setSubmissionFaultCountdown(6);const auto roots=h.proxy->submits;h.action.apply(scene.root);
 bool ok=check(h.action.getLastStatus()==CoinRenderAction::OUT_OF_MEMORY&&injected(h.action.getLastError())&&Cpu::submissionFaultSuccessCount()==6&&h.proxy->submits==roots+1&&h.target->getStatus()==CoinRenderTarget::TARGET_ERROR,"native root OOM follows six successful faces and latches documented TARGET_ERROR")&&retained(h,a);
 Cpu::setSubmissionFaultCountdown(-1);h.action.apply(scene.root);
 ok &=check(h.action.getLastStatus()!=CoinRenderAction::SUCCESS&&h.proxy->submits==roots+1,"disarming native root fault does not clear terminal target state")&&retained(h,a);
 ok &=check(h.target->resize(h.target->getSize()),"public resize recovers native root OOM");source.paint(0);
 ok &=h.render(scene.root,b,"native root OOM recovered by public resize")&&exact(a,b);return ok;
}
bool refusals(){
 FaultReset reset;Source source;Scene scene;scene.cube->scene=source.root;scene.enabled->whichChild=SO_SWITCH_ALL;Harness h(1,true,COIN_RENDER_SCENE_TEXTURE_STAGED);Frame a,b;
 if(!h.render(scene.root,a,"native fault preflight control"))return false;
 Cpu::setSubmissionFaultCountdown(0);scene.cube->size.setValue(0,32);bool ok=h.reject(scene.root,a,"invalid size while fault armed")&&check(Cpu::submissionFaultSuccessCount()==0,"invalid preflight does not consume native countdown");
 scene.cube->size.setValue(32,32);h.action.apply(scene.root);ok &=check(h.action.getLastStatus()==CoinRenderAction::OUT_OF_MEMORY&&injected(h.action.getLastError())&&Cpu::submissionFaultSuccessCount()==0,"invalid request leaves next valid actual submit armed")&&retained(h,a);
 Cpu::setSubmissionFaultCountdown(-1);scene.quality->textureQuality=0;scene.cube->scene=scene.root;Cpu::setSubmissionFaultCountdown(1);
 ok &=h.render(scene.root,b,"inactive cyclic cube submits root only")&&check(Cpu::submissionFaultSuccessCount()==1,"inactive cyclic producer causes zero native CPU child submissions");
 Cpu::setSubmissionFaultCountdown(-1);scene.cube->scene=source.root;scene.quality->textureQuality=.1f;ok &=h.render(scene.root,b,"native fault inactive recovery")&&exact(a,b);return ok;
}
struct SixthCaptureFault {
 cube_rtt_source::Walls*source;unsigned captures=0;bool armed=false;
 static void callback(void*data,SoAction*action){auto&self=*static_cast<SixthCaptureFault*>(data);if(!self.armed||!action->isOfType(SoCallbackAction::getClassTypeId()))return;if(++self.captures==6){SbMatrix matrix=SbMatrix::identity();matrix[3][2]=std::numeric_limits<float>::quiet_NaN();self.source->matrix->matrix=matrix;}}
};
bool invalidSixth(){
 FaultReset reset;cube_rtt_source::Walls source(1,0,true,false);storedSrgbMode=true;Scene scene;scene.cube->scene=source.root;scene.enabled->whichChild=SO_SWITCH_ALL;
 SixthCaptureFault fault;fault.source=&source;auto*callback=new SoCallback;callback->setCallback(SixthCaptureFault::callback,&fault);source.root->insertChild(callback,1);
 Harness h(1,true,COIN_RENDER_SCENE_TEXTURE_STAGED);Frame a,back;if(!h.render(scene.root,a,"sixth capture invalid preflight stable"))return false;
 const auto roots=h.proxy->submits;fault.armed=true;Cpu::setSubmissionFaultCountdown(0);h.action.apply(scene.root);
 bool ok=check(fault.captures==6&&h.action.getLastStatus()!=CoinRenderAction::SUCCESS&&h.action.getLastStatus()!=CoinRenderAction::OUT_OF_MEMORY&&h.action.getLastError().getLength()>0&&Cpu::submissionFaultSuccessCount()==0&&h.proxy->submits==roots,"invalid sixth captured source matrix is rejected before first real CPU face/root submit even on warmed graph")&&retained(h,a);
 fault.armed=false;source.matrix->matrix=SbMatrix::identity();Cpu::setSubmissionFaultCountdown(-1);ok &=h.render(scene.root,back,"sixth captured plan refusal immediate recovery")&&exact(a,back);return ok;
}
bool lateCapability(){
 FaultReset reset;storedSrgbMode=true;cube_rtt_source::Walls source(1,0,true,false);Scene scene;scene.cube->scene=source.root;scene.cube->size.setValue(16,16);scene.enabled->whichChild=SO_SWITCH_ALL;Harness h(1,true,COIN_RENDER_SCENE_TEXTURE_STAGED);Frame stable,back;if(!h.render(scene.root,stable,"native current-resource preflight stable A"))return false;
 const auto graph=h.proxy->graph;const auto root=h.proxy->captured;if(!check(graph.producers.size()==1&&graph.producers[0].cube,"native late-capability captures actual complete graph"))return false;bool ok=true;
 for(unsigned fault=0;fault<3;++fault){auto invalid=graph;auto&sixth=invalid.producers[0].face(5);if(fault==0)sixth.textures.front().pixelsRgba.pop_back();else if(fault==1)sixth.samplers.front().filter=static_cast<CoinRenderTextureFilter>(999);else{CoinRenderShadowGroupSnapshot group;group.sourceRevision=invalid.producers[0].sourceRevision;sixth.shadowGroups.push_back(group);for(auto&state:sixth.renderStates)state.shadowGroupSlot=1;std::string diagnostic;ok &=check(invalid.validate(root,diagnostic)&&CoinRenderTargetP::validateProfile(sixth,invalid.producers[0].size).status==CoinRenderBackendStatus::SUCCESS,"late shadow producer is generically valid and isolates CPU capability admission");}
  const auto roots=h.proxy->submits,producers=h.proxy->producerCalls;CoinRenderFramePlan resolved=root;resolved.clearColor=SbColor4f(.71f,.19f,.83f,.37f);const auto sentinel=resolved;Cpu::setSubmissionFaultCountdown(0);
  CoinRenderRttExecution execution(&h.target->getPimpl().get(),h.target->getOptions());const auto result=execution.prepare(invalid,root,resolved);
  ok &=check(result.status!=CoinRenderBackendStatus::SUCCESS&&result.status!=CoinRenderBackendStatus::OUT_OF_MEMORY&&!result.diagnostic.empty()&&Cpu::submissionFaultSuccessCount()==0&&h.proxy->submits==roots&&h.proxy->producerCalls==producers&&h.target->getStatus()==CoinRenderTarget::TARGET_READY,"native CPU actual graph preflight rejects sixth-face malformed resource or unsupported shadow before any real submit")&&retained(h,stable);
  ok &=check(cube_rtt_source::sameIdentities(graph,invalid)&&resolved.hasSamePayload(sentinel),"native sixth-face refusal preserves revisions/digests and does not expose partial resolved output");Cpu::setSubmissionFaultCountdown(-1);
 }
 ok &=h.render(scene.root,back,"native late-capability rejection recovery")&&exact(stable,back);return ok;
}
bool threadAndPreflight(){
 FaultReset reset;CoinRenderFramePlan valid;valid.clearColor=SbColor4f(.2f,.4f,.6f,1);CoinRenderTargetP target(SbVec2i32(4,4));Cpu first,second;
 if(!check(first.submit(valid,target).status==CoinRenderBackendStatus::SUCCESS,"unarmed native CPU submit works"))return false;
 const auto color=target.colorBuffer;const auto depth=target.depthBuffer;Cpu::setSubmissionFaultCountdown(1);
 bool ok=check(target.colorBuffer==color&&target.depthBuffer==depth,"arming native fault never changes existing attachments");
 auto invalid=valid;invalid.outputColorFormat=CoinRenderTextureFormat::RGBA16_FLOAT;
 auto refused=first.submit(invalid,target);ok &=check(refused.status!=CoinRenderBackendStatus::SUCCESS&&refused.status!=CoinRenderBackendStatus::OUT_OF_MEMORY&&Cpu::submissionFaultSuccessCount()==0&&target.colorBuffer==color&&target.depthBuffer==depth,"invalid native CPU preflight rejects normally without consuming fault or clearing attachments");
 bool worker=false;std::thread thread([&]{FaultReset workerReset;CoinRenderTargetP child(SbVec2i32(4,4));Cpu cpu;worker=cpu.submit(valid,child).status==CoinRenderBackendStatus::SUCCESS;Cpu::setSubmissionFaultCountdown(0);worker &=cpu.submit(valid,child).status==CoinRenderBackendStatus::OUT_OF_MEMORY;});thread.join();
 ok &=check(worker&&Cpu::submissionFaultSuccessCount()==0,"fault state and observed successes are isolated by calling thread");
 ok &=check(first.submit(valid,target).status==CoinRenderBackendStatus::SUCCESS&&Cpu::submissionFaultSuccessCount()==1,"one actual completed publication consumes one native success");
 const auto stableColor=target.colorBuffer;const auto stableDepth=target.depthBuffer;
 ok &=check(second.submit(valid,target).status==CoinRenderBackendStatus::OUT_OF_MEMORY&&Cpu::submissionFaultSuccessCount()==1&&target.colorBuffer==stableColor&&target.depthBuffer==stableDepth,"countdown spans fresh CPU instances and failure precedes publication");
 Cpu::setSubmissionFaultCountdown(-1);ok &=check(second.submit(valid,target).status==CoinRenderBackendStatus::SUCCESS,"disarm allows immediate valid native submission");return ok;
}
}
int main(int argc,char**argv){SoDB::init();CoinRenderAction::initClass();configureSamplingTest(argc,argv);CoinRenderCapabilities caps{};if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps)))return 1;if(!check(caps.backend==COIN_RENDER_EXPERIMENTAL_RECORDING&&!caps.gpu_available,"fault campaign requires genuine RECORDING factory without GPU"))return 1;recordingFactoryMode=true;std::string mode=argc>1?argv[1]:"--sixth";bool ok=mode=="--sixth"?sixth(false):mode=="--nested"?sixth(true):mode=="--root"?rootFailure():mode=="--refusals"?refusals():mode=="--thread"?threadAndPreflight():mode=="--sixth-invalid"?invalidSixth():mode=="--late-capability"?lateCapability():false;return ok?0:1;}
