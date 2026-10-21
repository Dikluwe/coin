#ifndef COIN_RENDER_VIEW_BUDGET_TEST_H
#define COIN_RENDER_VIEW_BUDGET_TEST_H
#include "rendering/coinrender/CoinRenderBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderRttCore.h"
#include <Inventor/actions/CoinRenderAction.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <vector>
namespace view_budget_test {
// BGFX ViewId has sixteen bits. This finite search guard is an API domain,
// not a claim about the selected device's negotiated target or pool capacity.
constexpr unsigned domain=unsigned(std::numeric_limits<uint16_t>::max())+1u;
inline bool rejected(const CoinRenderSubmitResult&r){return r.status==CoinRenderBackendStatus::UNSUPPORTED&&
  (r.diagnostic.find("view")!=std::string::npos||r.diagnostic.find("budget")!=std::string::npos);}
template<class Inspect>bool boundary(Inspect inspect,unsigned&fit,unsigned&excess,std::string&why){
  fit=0;excess=1;auto result=inspect(0);
  if(result.status!=CoinRenderBackendStatus::SUCCESS){why="zero workload preflight failed: "+result.diagnostic;return false;}
  bool found=false;
  while(excess<=domain){result=inspect(excess);
    if(result.status==CoinRenderBackendStatus::SUCCESS){fit=excess;if(excess==domain)break;excess=std::min(domain,excess*2u);continue;}
    if(!rejected(result)){why="unrelated failure during boundary search: "+result.diagnostic;return false;}found=true;break;}
  if(!found||fit==0){why="no nonempty finite view boundary in the ViewId domain";return false;}
  while(excess-fit>1){const unsigned mid=fit+(excess-fit)/2;result=inspect(mid);
    if(result.status==CoinRenderBackendStatus::SUCCESS)fit=mid;
    else{if(!rejected(result)){why="unrelated failure during binary search: "+result.diagnostic;return false;}excess=mid;}}
  return true;
}
class Probe:public CoinRenderBackend {
public:
  std::unique_ptr<CoinRenderBackend>real;unsigned roots=0,producers=0,dryCalls=0;mutable unsigned preflights=0;
  mutable CoinRenderSubmitResult lastAdmission;mutable std::vector<CoinRenderSubmitResult>admissions;
  bool dry=false;CoinRenderSubmitResult dryAdmission;CoinRenderFramePlan dryFrame;
  explicit Probe(std::unique_ptr<CoinRenderBackend>b):real(std::move(b)){}
  bool isGpuBackend()const override{return real->isGpuBackend();}
  bool initializesCpuDepthBuffer()const override{return real->initializesCpuDepthBuffer();}
  bool resetOnActionDetach()const override{return real->resetOnActionDetach();}
  bool supportsOffscreenShadows()const override{return real->supportsOffscreenShadows();}
  bool supportsOpaqueBump()const override{return real->supportsOpaqueBump();}
  CoinRenderBackendStatus getStatus()const override{return real->getStatus();}
  CoinRenderBackendStatus prepare(CoinRenderTargetP&t)override{return real->prepare(t);}
  CoinRenderSubmitResult capture(const CoinRenderFramePlan&f,CoinRenderTargetP&t){
    ++dryCalls;dryFrame=f;CoinRenderRttPlan graph(t.options.sceneTexture);dryAdmission=real->preflightRtt(graph,f,t.size);
    return {CoinRenderBackendStatus::UNSUPPORTED,"test capture only; no GPU submission"};}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&f,CoinRenderTargetP&t)override{if(dry)return capture(f,t);++roots;return real->submit(f,t);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&f,CoinRenderTargetP&t,const CoinRenderFrameReuseDecision&r)override{if(dry)return capture(f,t);++roots;return real->submit(f,t,r);}
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan&g,const CoinRenderFramePlan&f,const SbVec2i32&size)const override{
    ++preflights;lastAdmission=real->preflightRtt(g,f,size);admissions.push_back(lastAdmission);return lastAdmission;}
  CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan&f,const SbVec2i32&size,uint64_t id,CoinRenderTargetP&t,uint64_t&token)override{
    ++producers;return real->submitRtt(f,size,id,t,token);}
  void finishRtt(const std::vector<uint64_t>&tokens)override{real->finishRtt(tokens);}
  bool stagedRttRequiresPreparedParent()const override{return real->stagedRttRequiresPreparedParent();}
  bool requiresSharedRetirement()const override{return real->requiresSharedRetirement();}
  void retireLostReadbacks()override{real->retireLostReadbacks();}
  CoinRenderDeviceDomain resourceDomain()const override{return real->resourceDomain();}
  bool readbackLoad(uint64_t&a,uint64_t&b)const override{return real->readbackLoad(a,b);}
  void poll()override{real->poll();}
  const std::string&getLastError()const override{return real->getLastError();}
};
template<class Render>CoinRenderSubmitResult managerPlan(Probe&probe,CoinRenderTarget&target,Render render,bool&valid){
  const auto serial=target.getLastSubmissionSerial();const auto calls=probe.dryCalls,roots=probe.roots,producers=probe.producers;
  probe.dryAdmission={CoinRenderBackendStatus::BACKEND_ERROR,"manager did not reach capture-only backend"};
  probe.dry=true;const auto status=render();probe.dry=false;
  valid &= status==CoinRenderAction::UNSUPPORTED&&probe.dryCalls==calls+1&&probe.roots==roots&&probe.producers==producers&&target.getLastSubmissionSerial()==serial;
  return valid?probe.dryAdmission:CoinRenderSubmitResult{CoinRenderBackendStatus::BACKEND_ERROR,"manager capture/purity invariant failed"};
}
}
#endif
