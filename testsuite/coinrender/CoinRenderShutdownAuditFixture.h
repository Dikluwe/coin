#ifndef COIN_RENDER_SHUTDOWN_AUDIT_FIXTURE_H
#define COIN_RENDER_SHUTDOWN_AUDIT_FIXTURE_H
#include "rendering/coinrender/CoinRenderBackendRuntime.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#ifdef COIN_TEXTURE2D_STARTUP_WGPU
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#endif
#include <chrono>
#include <iostream>
// No polling, new submission, explicit draining, device wait, or second
// destruction. Every intercepted operation delegates exactly once.
namespace startup {
struct ShutdownAudit {
  bool valid=true;
  unsigned surfacesEntered=0,surfacesReturned=0,backendsEntered=0,backendsReturned=0;
  unsigned actionsEntered=0,actionsReturned=0,asyncSubmits=0,submits=0;
  unsigned runtimePolls=0,ticketPolls=0,ticketCancels=0,verifiedRetiredSurfaces=0;
  uint64_t lastSerial=0;
  const std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
  void mark(const char *phase,uint64_t resource=0) const {
    const auto us=std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now()-start).count();
    std::cerr<<"texture2d_shutdown_audit phase="<<phase<<" elapsed_us="<<us
      <<" resource="<<resource<<std::endl;
  }
  void require(bool value,const char *label) {
    if(!value){valid=false;std::cerr<<"texture2d_shutdown_audit FAIL "<<label<<std::endl;}
  }
  bool finish() {
    require(submits==33&&lastSerial!=0,"original masks path makes exactly 33 synchronous submissions");
    require(asyncSubmits==0&&runtimePolls==0&&ticketPolls==0&&ticketCancels==0,
      "synchronous masks shutdown introduces no async tickets, polling or cancellation");
    require(surfacesEntered==1&&surfacesReturned==1,"runtime surface destruction returned exactly once");
    require(backendsEntered==1&&backendsReturned==1,"concrete backend destruction returned exactly once");
    require(actionsEntered==1&&actionsReturned==1,"action destruction returned exactly once");
#ifdef COIN_TEXTURE2D_STARTUP_WGPU
    require(verifiedRetiredSurfaces==1,"published wgpu surface leaves the runtime registry");
#endif
    mark("all_fixture_destructors_returned",lastSerial);
    return valid;
  }
};
// Declaration order brackets the Action member's destructor without changing
// Action's type or explicitly invoking its destructor.
struct ActionDestructionBegin {
  ShutdownAudit *audit=nullptr;
  ~ActionDestructionBegin(){if(audit){++audit->actionsEntered;audit->mark("action_destructor_begin");}}
};
struct ActionDestructionEnd {
  ShutdownAudit *audit=nullptr;
  ~ActionDestructionEnd(){if(audit){++audit->actionsReturned;audit->mark("action_destructor_end");}}
};
// The Counter declares this base BEFORE Proxy: reverse base destruction calls
// this only after Proxy's plan and owned concrete backend have been destroyed.
struct BackendDestructionEnd {
  ShutdownAudit *shutdownAudit=nullptr;
  ~BackendDestructionEnd(){if(shutdownAudit){++shutdownAudit->backendsReturned;shutdownAudit->mark("backend_destructor_end");}}
};
class ShutdownRuntime:public CoinRenderBackendRuntime {
  CoinRenderBackendRuntime &inner;
  ShutdownAudit &audit;
public:
  ShutdownRuntime(CoinRenderBackendRuntime &r,ShutdownAudit &a):inner(r),audit(a){}
  bool supportsWindowTargets()const override{return inner.supportsWindowTargets();}
  std::string surfaceTypeDiagnostic(uint32_t t)const override{return inner.surfaceTypeDiagnostic(t);}
  void destroySurface(CoinRenderTargetP &target)override {
    ++audit.surfacesEntered;
#ifdef COIN_TEXTURE2D_STARTUP_WGPU
    const uint64_t token=target.surfaceId;
    // This existing query only locks and reads the registry. In contrast,
    // readbackLoad reaps cancelled jobs and would change retirement behavior.
    audit.mark("surface_registry_query_before",token);
    const uint64_t published=token?coin_wgpu_surface_submission_serial(token):0;
    audit.require(token&&published,"live surface has the already-validated publication before retirement");
#endif
    audit.mark("runtime_destroy_surface_begin",target.surfaceId);
    inner.destroySurface(target);
    ++audit.surfacesReturned;audit.mark("runtime_destroy_surface_end",target.surfaceId);
    audit.require(target.surfaceId==0,"retired target exposes no live surface token");
#ifdef COIN_TEXTURE2D_STARTUP_WGPU
    audit.mark("surface_registry_query_after",token);
    const uint64_t remaining=coin_wgpu_surface_submission_serial(token);
    const bool retired=token&&published&&remaining==0;
    audit.require(retired,"destroySurface removes the previously published surface from the runtime registry");
    if(retired)++audit.verifiedRetiredSurfaces;
#endif
  }
  CoinRenderTarget::ReadbackStatus pollReadback(const CoinRenderReadbackTicket&t,
    std::vector<uint8_t>&c,std::vector<float>&d,SbString *error)override {
    ++audit.ticketPolls;return inner.pollReadback(t,c,d,error);
  }
  bool cancelReadback(const CoinRenderReadbackTicket&t)override{++audit.ticketCancels;return inner.cancelReadback(t);}
  bool cacheTelemetry(CoinRenderCacheTelemetry&t)const override{return inner.cacheTelemetry(t);}
  void pollDevice()override{++audit.runtimePolls;inner.pollDevice();}
  void releaseIdleDevice()override{audit.mark("runtime_release_idle_begin");inner.releaseIdleDevice();audit.mark("runtime_release_idle_end");}
};
}
#endif
