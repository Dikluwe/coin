#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuDiagnosticShell.h"

#include <cstdlib>
#include <locale>
#include <sstream>

SoWgpuActionDiagnostic
SoWgpuDiagnosticShell::action(SoWgpuRenderAction::Status status,
                              SoWgpuDiagnosticDomain domain,
                              const SbString & message)
{
  return SoWgpuActionDiagnostic(status, domain, message);
}

SoWgpuActionDiagnostic
SoWgpuDiagnosticShell::success(void)
{
  return action(SoWgpuRenderAction::SUCCESS,
                SoWgpuDiagnosticDomain::NONE, SbString(""));
}

SoWgpuActionDiagnostic
SoWgpuDiagnosticShell::fromBackend(const SubmitResult & result,
                                   SoWgpuDiagnosticDomain domain)
{
  return action(actionStatus(result.status), domain,
                SbString(result.diagnostic.c_str()));
}

SoWgpuActionDiagnostic
SoWgpuDiagnosticShell::fromTarget(SoWgpuRenderTarget::Status status,
                                  const char * message)
{
  return action(actionStatus(status), SoWgpuDiagnosticDomain::TARGET,
                SbString(message ? message : ""));
}

SoWgpuActionDiagnostic
SoWgpuDiagnosticShell::withContext(SoWgpuRenderAction::Status status,
                                   SoWgpuDiagnosticDomain domain,
                                   const char * context,
                                   const SbString & message)
{
  SbString text(context ? context : "");
  text += ": ";
  text += message;
  return action(status, domain, text);
}

SoWgpuRenderAction::Status
SoWgpuDiagnosticShell::actionStatus(BackendStatus status)
{
  switch (status) {
    case BackendStatus::SUCCESS: return SoWgpuRenderAction::SUCCESS;
    case BackendStatus::NOT_READY: return SoWgpuRenderAction::NOT_READY;
    case BackendStatus::UNSUPPORTED: return SoWgpuRenderAction::UNSUPPORTED;
    case BackendStatus::OUT_OF_MEMORY: return SoWgpuRenderAction::OUT_OF_MEMORY;
    case BackendStatus::DEVICE_LOST: return SoWgpuRenderAction::DEVICE_LOST;
    case BackendStatus::SURFACE_LOST: return SoWgpuRenderAction::SURFACE_LOST;
    case BackendStatus::BACKEND_ERROR:
    default: return SoWgpuRenderAction::BACKEND_ERROR;
  }
}

SoWgpuRenderAction::Status
SoWgpuDiagnosticShell::actionStatus(SoWgpuRenderTarget::Status status)
{
  switch (status) {
    case SoWgpuRenderTarget::TARGET_READY: return SoWgpuRenderAction::SUCCESS;
    case SoWgpuRenderTarget::TARGET_NOT_READY: return SoWgpuRenderAction::NOT_READY;
    case SoWgpuRenderTarget::TARGET_LOST: return SoWgpuRenderAction::DEVICE_LOST;
    case SoWgpuRenderTarget::TARGET_SURFACE_LOST: return SoWgpuRenderAction::SURFACE_LOST;
    case SoWgpuRenderTarget::TARGET_ERROR:
    default: return SoWgpuRenderAction::BACKEND_ERROR;
  }
}

const char *
SoWgpuDiagnosticShell::statusName(SoWgpuRenderAction::Status status)
{
  switch (status) {
    case SoWgpuRenderAction::SUCCESS: return "SUCCESS";
    case SoWgpuRenderAction::NO_TARGET: return "NO_TARGET";
    case SoWgpuRenderAction::NOT_READY: return "NOT_READY";
    case SoWgpuRenderAction::INVALID_SCENE: return "INVALID_SCENE";
    case SoWgpuRenderAction::UNSUPPORTED: return "UNSUPPORTED";
    case SoWgpuRenderAction::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
    case SoWgpuRenderAction::DEVICE_LOST: return "DEVICE_LOST";
    case SoWgpuRenderAction::SURFACE_LOST: return "SURFACE_LOST";
    case SoWgpuRenderAction::BACKEND_ERROR:
    default: return "BACKEND_ERROR";
  }
}

const char *
SoWgpuDiagnosticShell::domainName(SoWgpuDiagnosticDomain domain)
{
  switch (domain) {
    case SoWgpuDiagnosticDomain::NONE: return "none";
    case SoWgpuDiagnosticDomain::ACTION: return "action";
    case SoWgpuDiagnosticDomain::FRAME_PLAN: return "frame_plan";
    case SoWgpuDiagnosticDomain::TARGET: return "target";
    case SoWgpuDiagnosticDomain::BACKEND: return "backend";
    case SoWgpuDiagnosticDomain::READBACK: return "readback";
    default: return "unknown";
  }
}

bool
SoWgpuDiagnosticShell::phaseTracingEnabled(void)
{
  return std::getenv("COIN_WGPU_TRACE_PHASES") != NULL;
}

std::string
SoWgpuDiagnosticShell::formatActionPhase(const SoWgpuActionPhaseSample & sample)
{
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << "COIN_WGPU_PHASE action traversal_ms=" << sample.traversalMs
         << " frame_plan_ms=" << sample.framePlanMs
         << " backend_ms=" << sample.backendMs
         << " vertices=" << sample.vertices
         << " indices=" << sample.indices
         << " draws=" << sample.draws
         << " plan_cache_hit=" << (sample.planCacheHit ? 1 : 0);
  return stream.str();
}

std::string
SoWgpuDiagnosticShell::formatBridgePhase(const SoWgpuBridgePhaseSample & sample)
{
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << "COIN_WGPU_PHASE bridge pack_ms=" << sample.packMs
         << " pack_cache_hit=" << (sample.packCacheHit ? 1 : 0)
         << " ffi_ms=" << sample.ffiMs;
  return stream.str();
}
