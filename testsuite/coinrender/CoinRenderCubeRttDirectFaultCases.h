#ifndef COIN_RENDER_CUBE_RTT_DIRECT_FAULT_CASES_H
#define COIN_RENDER_CUBE_RTT_DIRECT_FAULT_CASES_H
#include <map>
namespace cube_rtt_source {
// A private observer retains the actual factory backend and resource owner.
// Only the returned handle is corrupted, after retiring the fresh real
// candidate through that same owner. No resource is invented or CPU-rendered.
class DirectAliasProxy:public Proxy {
public:
 enum Fault { NONE, ASSEMBLED_ALIAS, SIXTH_FACE_ALIAS };
 Fault fault=NONE;bool recording=false,ownerCorrect=true,healthy=true;unsigned faceCalls=0,assemblyCalls=0;
 CoinRenderBackend*owner=nullptr;CoinRenderDeviceDomain domain{};std::vector<uint64_t>allocated,returned;std::map<uint64_t,unsigned>retirements;
 explicit DirectAliasProxy(std::unique_ptr<CoinRenderBackend>backend):Proxy(std::move(backend)){}
 void begin(Fault selected){fault=selected;recording=true;ownerCorrect=true;healthy=true;faceCalls=assemblyCalls=0;owner=real.get();domain=real->resourceDomain();allocated.clear();returned.clear();retirements.clear();}
 void observeRetirement(const std::vector<uint64_t>&tokens){if(!recording)return;const auto current=real->resourceDomain();ownerCorrect &=real.get()==owner&&current.device==domain.device&&current.generation==domain.generation;for(uint64_t token:tokens)if(token)++retirements[token];}
 void retireOwn(uint64_t token){const std::vector<uint64_t>tokens{token};observeRetirement(tokens);real->retireRtt(tokens);}
 CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan&frame,const SbVec2i32&size,uint64_t revision,CoinRenderTargetP&target,uint64_t&token)override{
  auto result=Proxy::submitRtt(frame,size,revision,target,token);if(!recording)return result;++faceCalls;if(token)allocated.push_back(token);healthy &=result.status==CoinRenderBackendStatus::SUCCESS&&token!=0;
  if(fault==SIXTH_FACE_ALIAS&&faceCalls==6&&result.status==CoinRenderBackendStatus::SUCCESS&&token&&!returned.empty()){healthy &=token!=returned.front();if(token!=returned.front())retireOwn(token);token=returned.front();}if(token)returned.push_back(token);return result;
 }
 CoinRenderSubmitResult assembleCubeRtt(const std::array<uint64_t,6>&faces,uint32_t size,bool mips,CoinRenderTargetP&target,uint64_t&token)override{
  auto result=Proxy::assembleCubeRtt(faces,size,mips,target,token);if(!recording)return result;++assemblyCalls;if(token)allocated.push_back(token);healthy &=result.status==CoinRenderBackendStatus::SUCCESS&&token!=0;
  if(fault==ASSEMBLED_ALIAS&&result.status==CoinRenderBackendStatus::SUCCESS&&token){healthy &=token!=faces[0];if(token!=faces[0])retireOwn(token);token=faces[0];}if(token)returned.push_back(token);return result;
 }
 void retireRtt(const std::vector<uint64_t>&tokens)override{observeRetirement(tokens);Proxy::retireRtt(tokens);}
 void finishRtt(const std::vector<uint64_t>&tokens)override{observeRetirement(tokens);Proxy::finishRtt(tokens);}
 bool exactlyOnce()const{if(!ownerCorrect||!healthy||!domain.device||!domain.generation||allocated.empty())return false;for(uint64_t token:allocated){auto i=retirements.find(token);if(i==retirements.end()||i->second!=1)return false;}for(uint64_t token:returned){auto i=retirements.find(token);if(i==retirements.end()||i->second!=1)return false;}for(const auto&entry:retirements)if(std::find(allocated.begin(),allocated.end(),entry.first)==allocated.end())return false;return true;}
};
inline bool directAliasFaults(){
 storedSrgbMode=true;Walls source(0,0,true,false);Scene scene;scene.cube->scene=source.root;scene.cube->size.setValue(16,16);scene.enabled->whichChild=SO_SWITCH_ALL;scene.quality->textureQuality=.8f;Harness h(1,false,COIN_RENDER_SCENE_TEXTURE_DIRECT);if(!h.target)return false;
 auto backend=std::move(h.proxy->real);auto*proxy=new DirectAliasProxy(std::move(backend));h.target->getPimpl()->backend.reset(proxy);h.proxy=proxy;
 Frame a,b,restored;if(!h.render(scene.root,a,"DIRECT alias rollback stable A")||!panels(a,scene,Oracle(source,16),side,3))return false;const auto original=h.proxy->graph;const auto root=h.proxy->captured;if(!check(original.producers.size()==1&&original.producers[0].cube,"DIRECT alias focal captures actual one-cube graph"))return false;
 const std::array<uint8_t,4>bytes{{181,89,147,173}};auto changed=original;for(auto&producer:changed.producers)for(size_t face=0;face<producer.faceCount();++face)for(auto&image:producer.face(face).textures)if(!image.producerId&&!image.gpuToken&&!image.pixelsRgba.empty())for(size_t offset=0;offset<image.pixelsRgba.size();++offset)image.pixelsRgba[offset]=bytes[offset%4];
 CoinRenderCapabilities caps{};if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps)))return false;bool ok=true;
 for(auto fault:{DirectAliasProxy::ASSEMBLED_ALIAS,DirectAliasProxy::SIXTH_FACE_ALIAS}){
  CoinRenderReadbackTicket ticket{};if(caps.features&COIN_RENDER_FEATURE_ASYNC_READBACK){h.action.applyAsync(scene.root,ticket);ok &=check(h.action.getLastStatus()==CoinRenderAction::SUCCESS&&ticket.token,"ticket A exists before DIRECT backend alias fault");}
  const auto before=currentPublication(h);const auto actualColor=h.target->getPimpl()->colorBuffer;const auto actualDepth=h.target->getPimpl()->depthBuffer;const auto oldTicket=ticket;const auto submits=h.proxy->submits;proxy->begin(fault);CoinRenderSubmitResult result;CoinRenderFramePlan resolved=root;resolved.clearColor=SbColor4f(.71f,.19f,.83f,.37f);const auto sentinel=resolved;
  {CoinRenderRttExecution execution(&h.target->getPimpl().get(),h.target->getOptions());result=execution.prepare(changed,root,resolved);}
  const auto after=currentPublication(h);ok &=check(result.status==CoinRenderBackendStatus::BACKEND_ERROR&&!result.diagnostic.empty()&&h.proxy->submits==submits&&h.target->getStatus()==CoinRenderTarget::TARGET_READY&&before.serial==after.serial&&before.rgba==after.rgba&&before.depth==after.depth&&actualColor==h.target->getPimpl()->colorBuffer&&actualDepth==h.target->getPimpl()->depthBuffer&&resolved.hasSamePayload(sentinel),"DIRECT aliased backend return is refused before root submit or partial resource publication and retains RGBA/depth/serial");
  ok &=check(proxy->faceCalls==6&&proxy->assemblyCalls==(fault==DirectAliasProxy::ASSEMBLED_ALIAS?1u:0u)&&proxy->exactlyOnce(),"all real DIRECT candidates and distinct returned tokens retire exactly once through their actual owner; duplicate sixth face stops before assembly");
  ok &=check(ticket.token==oldTicket.token&&ticket.generation==oldTicket.generation&&ticket.submissionSerial==oldTicket.submissionSerial,"DIRECT alias fault leaves outstanding ticket identity intact");proxy->recording=false;proxy->fault=DirectAliasProxy::NONE;
  ok &=executeCurrentGraph(h,changed,root,b,"DIRECT alias immediate healthy B recovery")&&uniformCurrentPanels(b,source,bytes,true)&&check(b.rgba!=a.rgba,"DIRECT B recovery visibly reaches every consumer");
  if(ticket.token){std::vector<uint8_t>rgba;std::vector<float>depth;SbString diagnostic;auto status=CoinRenderTarget::READBACK_NOT_READY;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);do{status=CoinRenderTarget::pollReadback(ticket,rgba,depth,&diagnostic);}while(status==CoinRenderTarget::READBACK_NOT_READY&&std::chrono::steady_clock::now()<deadline);ok &=check(status==CoinRenderTarget::READBACK_READY&&rgba==a.rgba&&depth==a.depth,"DIRECT old ticket A resolves exactly after aliased return rejection and healthy B publication");}
  ok &=executeCurrentGraph(h,original,root,restored,"DIRECT alias original A restored")&&exact(a,restored);
 }
 return ok;
}
}
#endif
