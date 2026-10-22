#ifndef COIN_RENDER_CUBE_RTT_CURRENT_RESOURCE_CASES_H
#define COIN_RENDER_CUBE_RTT_CURRENT_RESOURCE_CASES_H
#include "rendering/coinrender/CoinRenderRttExecution.h"
#include <chrono>
namespace cube_rtt_source {
inline Frame currentPublication(Harness&h){Frame frame;h.target->readbackRGBA(frame.rgba);h.target->readbackDepth(frame.depth);frame.serial=h.target->getLastSubmissionSerial();return frame;}
inline bool sameIdentities(const CoinRenderRttPlan&a,const CoinRenderRttPlan&b){
 if(a.producers.size()!=b.producers.size())return false;
 for(size_t p=0;p<a.producers.size();++p){const auto&first=a.producers[p];const auto&second=b.producers[p];if(first.sourceRevision!=second.sourceRevision||first.faceCount()!=second.faceCount())return false;for(size_t face=0;face<first.faceCount();++face){const auto&x=first.face(face);const auto&y=second.face(face);if(x.revision!=y.revision||x.textures.size()!=y.textures.size())return false;for(size_t image=0;image<x.textures.size();++image)if(x.textures[image].contentDigest!=y.textures[image].contentDigest)return false;}}
 return true;
}
inline bool executeCurrentGraph(Harness&h,const CoinRenderRttPlan&graph,const CoinRenderFramePlan&root,Frame&frame,const char*phase){
 const uint64_t serial=h.target->getLastSubmissionSerial();const unsigned submits=h.proxy->submits;
 CoinRenderRttExecution execution(&h.target->getPimpl().get(),h.target->getOptions());CoinRenderFramePlan resolved;auto result=execution.prepare(graph,root,resolved);
 if(result.status==CoinRenderBackendStatus::SUCCESS)result=h.target->getPimpl()->executeFrame(resolved);
 frame=currentPublication(h);std::cout<<"cube_current_graph phase="<<phase<<" status="<<int(result.status)<<" serial="<<frame.serial<<" diagnostic="<<result.diagnostic<<'\n';
 return check(result.status==CoinRenderBackendStatus::SUCCESS&&frame.serial>serial&&h.proxy->submits==submits+1&&frame.rgba.size()==size_t(side)*side*4&&frame.depth.size()==size_t(side)*side, std::string(phase)+" executes actual captured producer graph and publishes root once");
}
inline bool uniformCurrentPanels(const Frame&frame,const Walls&source,const std::array<uint8_t,4>&bytes,bool srgb){
 if(frame.rgba.size()!=size_t(side)*side*4)return false;RGBA expected;for(unsigned c=0;c<4;++c)expected[c]=byte(255*source.material[c]*(c<3&&srgb?decode(bytes[c]):bytes[c]/255.));unsigned bad=0;
 for(unsigned cell=0;cell<54;++cell){int x=int(std::lround(side*.5+(-3.2+.8*(cell%9))*side/8-.5)),y=int(std::lround(side*.5-(2.-.8*(cell/9))*side/8-.5));const size_t offset=(size_t(y)*side+unsigned(x))*4;for(unsigned c=0;c<4;++c){int delta=std::abs(int(frame.rgba[offset+c])-int(expected[c]));bad+=delta>3;if(delta>3&&bad<=4)std::cerr<<"cube_current_oracle cell="<<cell<<" channel="<<c<<" actual="<<unsigned(frame.rgba[offset+c])<<" expected="<<expected[c]<<'\n';}}
 return check(bad==0,"every consumer observes same-revision current source bytes/format with independent RGB transfer and linear alpha");
}
inline bool sameBackendSourceReuse(Harness&h,const CoinRenderRttPlan&original,const CoinRenderRttPlan&changed,const CoinRenderRttPlan&linear,const Walls&source,const std::array<uint8_t,4>&bytes,bool cpu){
 const auto&first=original.producers[0].face(0);const auto&second=changed.producers[0].face(0);const auto&third=linear.producers[0].face(0);
 if(!check(!first.outputMipmaps&&!second.outputMipmaps&&!third.outputMipmaps&&first.outputColorFormat==CoinRenderTextureFormat::RGBA8_LINEAR&&first.revision==second.revision&&first.revision==third.revision,"captured base face is reused directly with unchanged revision and linear non-mip output"))return false;
 std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createOffscreen(SbVec2i32(16,16),h.target->getOptions()));if(!check(bool(target)&&target->setDepthReadbackEnabled(TRUE),"public small source-cache target created with identical options"))return false;
 auto&privateTarget=target->getPimpl().get();if(!check(privateTarget.prepareBackend()==CoinRenderBackendStatus::SUCCESS&&privateTarget.backend&&privateTarget.backend->isGpuBackend()==!cpu,"small source-cache target preserves actual public factory CPU/GPU backend"))return false;CoinRenderBackend*const backend=privateTarget.backend.get();
 auto submit=[&](const CoinRenderFramePlan&plan,Frame&frame,const char*phase){const auto serial=target->getLastSubmissionSerial();const auto result=privateTarget.executeFrame(plan);target->readbackRGBA(frame.rgba);target->readbackDepth(frame.depth);frame.serial=target->getLastSubmissionSerial();std::cout<<"cube_current_source_reuse phase="<<phase<<" status="<<int(result.status)<<" serial="<<frame.serial<<" diagnostic="<<result.diagnostic<<'\n';return check(result.status==CoinRenderBackendStatus::SUCCESS&&frame.serial>serial&&frame.rgba.size()==16u*16u*4u&&frame.depth.size()==16u*16u&&privateTarget.backend.get()==backend,"one current source update executes on the same public target and factory backend");};
 auto uniform=[&](const Frame&frame,bool srgb){if(frame.rgba.size()!=16u*16u*4u)return false;RGBA expected;for(unsigned c=0;c<4;++c)expected[c]=byte(255*source.material[c]*(c<3&&srgb?decode(bytes[c]):bytes[c]/255.));unsigned bad=0;for(unsigned y=6;y<10;++y)for(unsigned x=6;x<10;++x)for(unsigned c=0;c<4;++c){const auto actual=frame.rgba[(size_t(y)*16+x)*4+c];const int delta=std::abs(int(actual)-int(expected[c]));bad+=delta>3;if(delta>3&&bad<=4)std::cerr<<"cube_current_source_reuse channel="<<c<<" actual="<<unsigned(actual)<<" expected="<<expected[c]<<'\n';}return check(bad==0,"warm source backend observes current bytes and format through independent uniform center oracle");};
 Frame a,b,c,restored;bool ok=submit(first,a,"A original patterned face");ok &=submit(second,b,"B bytes unchanged revision/digest")&&uniform(b,true)&&check(a.rgba!=b.rgba&&a.depth==b.depth,"same source backend B visibly changes RGB/alpha and preserves geometry depth");ok &=submit(third,c,"C linear format unchanged bytes/revision/digest")&&uniform(c,false)&&check(c.rgba!=b.rgba&&c.depth==b.depth,"same source backend C changes RGB under current format without changing alpha/depth");ok &=submit(first,restored,"A exact source face restoration")&&exact(a,restored);return ok;
}
inline bool rejectCurrentGraph(Harness&h,const CoinRenderRttPlan&graph,const CoinRenderFramePlan&root,const CoinRenderReadbackTicket&ticket,bool cpu){
 bool ok=true;(void)cpu;for(unsigned fault=0;fault<2;++fault){auto invalid=graph;auto&sixth=invalid.producers[0].face(5);if(fault==0)sixth.textures.front().pixelsRgba.pop_back();else sixth.samplers.front().filter=static_cast<CoinRenderTextureFilter>(999);
  const auto before=currentPublication(h);const auto submits=h.proxy->submits,producers=h.proxy->producerCalls;const auto oldTicket=ticket;CoinRenderFramePlan resolved=root;resolved.clearColor=SbColor4f(.71f,.19f,.83f,.37f);const auto sentinel=resolved;
  CoinRenderRttExecution execution(&h.target->getPimpl().get(),h.target->getOptions());const auto result=execution.prepare(invalid,root,resolved);const auto after=currentPublication(h);
  ok &=check(result.status!=CoinRenderBackendStatus::SUCCESS&&result.status!=CoinRenderBackendStatus::OUT_OF_MEMORY&&!result.diagnostic.empty()&&h.proxy->submits==submits&&h.proxy->producerCalls==producers&&before.serial==after.serial&&before.rgba==after.rgba&&before.depth==after.depth&&h.target->getStatus()==CoinRenderTarget::TARGET_READY,"actual graph execution refuses malformed same-revision sixth face before submission and preserves healthy root publication");
  ok &=check(sameIdentities(graph,invalid)&&resolved.hasSamePayload(sentinel)&&ticket.token==oldTicket.token&&ticket.generation==oldTicket.generation&&ticket.submissionSerial==oldTicket.submissionSerial,"sixth-face refusal keeps identities, previous output plan and outstanding ticket intact");
 }
 return ok;
}
inline bool sameRevisionResources(unsigned family,bool cpu,bool direct){
 storedSrgbMode=true;Walls source(family,0,true,false);Scene scene;scene.cube->scene=source.root;scene.cube->size.setValue(16,16);scene.enabled->whichChild=SO_SWITCH_ALL;scene.quality->textureQuality=.8f;Harness h(1,cpu,direct?COIN_RENDER_SCENE_TEXTURE_DIRECT:COIN_RENDER_SCENE_TEXTURE_STAGED);Frame a,b,c,restored;
 if(!h.render(scene.root,a,"same-revision graph stable A"))return false;const auto original=h.proxy->graph;const auto root=h.proxy->captured;if(!check(original.producers.size()==1&&original.producers[0].cube,"actual source graph captured for current-resource execution"))return false;
 auto changed=original;const std::array<uint8_t,4>bytes{{181,89,147,173}};unsigned images=0;for(auto&producer:changed.producers)for(size_t face=0;face<producer.faceCount();++face)for(auto&image:producer.face(face).textures)if(!image.producerId&&!image.gpuToken&&!image.pixelsRgba.empty()){++images;for(size_t p=0;p<image.pixelsRgba.size();++p)image.pixelsRgba[p]=bytes[p%4];}
 bool ok=check(images>0&&sameIdentities(original,changed),"valid source payload update preserves every face/root/source revision and content digest");
 CoinRenderReadbackTicket ticket{};if(!cpu){CoinRenderCapabilities caps{};if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps)))return false;if(caps.features&COIN_RENDER_FEATURE_ASYNC_READBACK){h.action.applyAsync(scene.root,ticket);ok &=check(h.action.getLastStatus()==CoinRenderAction::SUCCESS&&ticket.token,"GPU ticket A issued before same-revision invalid sixth face");}}
 ok &=rejectCurrentGraph(h,changed,root,ticket,cpu);
 ok &=executeCurrentGraph(h,changed,root,b,"same-revision encoded bytes B")&&uniformCurrentPanels(b,source,bytes,true)&&check(a.rgba!=b.rgba&&a.depth==b.depth,"current same-revision source pixels visibly replace A in all consumers without moving depth");
 auto linear=changed;for(auto&producer:linear.producers)for(size_t face=0;face<producer.faceCount();++face)for(auto&image:producer.face(face).textures)if(!image.producerId&&!image.gpuToken&&!image.pixelsRgba.empty())image.format=CoinRenderTextureFormat::RGBA8_LINEAR;
 ok &=check(sameIdentities(original,linear),"valid current linear-format update keeps the same source bytes/revisions/digests")&&executeCurrentGraph(h,linear,root,c,"same-revision linear format C")&&uniformCurrentPanels(c,source,bytes,false)&&check(c.rgba!=b.rgba&&c.depth==b.depth,"source color-space metadata changes actual same-revision consumer RGB while alpha/depth stay linear");
 ok &=sameBackendSourceReuse(h,original,changed,linear,source,bytes,cpu);
 ok &=rejectCurrentGraph(h,linear,root,ticket,cpu);
 if(ticket.token){std::vector<uint8_t>rgba;std::vector<float>depth;SbString diagnostic;auto status=CoinRenderTarget::READBACK_NOT_READY;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);do{status=CoinRenderTarget::pollReadback(ticket,rgba,depth,&diagnostic);}while(status==CoinRenderTarget::READBACK_NOT_READY&&std::chrono::steady_clock::now()<deadline);ok &=check(status==CoinRenderTarget::READBACK_READY&&rgba==a.rgba&&depth==a.depth,"ticket A resolves exact original RGBA/depth after same-revision failures and B/C publications");}
 ok &=executeCurrentGraph(h,original,root,restored,"same-revision original A restored")&&exact(a,restored)&&panels(restored,scene,Oracle(source,16),side,3);
 Frame publicRecovery;ok &=h.render(scene.root,publicRecovery,"public scene recovery after private current-resource graph executions")&&exact(a,publicRecovery);return ok;
}
}
#endif
