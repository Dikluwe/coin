#ifndef COIN_SOWGPUDIAGNOSTICSHELL_H
#define COIN_SOWGPUDIAGNOSTICSHELL_H

#include <Inventor/CoinWgpuExport.h>
#include <Inventor/SbString.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>

#include "rendering/wgpu/SoWgpuBackend.h"
#include "rendering/wgpu/SoWgpuFrameReuseCore.h"

#include <cstddef>
#include <string>

enum class SoWgpuDiagnosticDomain {
  NONE = 0,
  ACTION,
  FRAME_PLAN,
  TARGET,
  BACKEND,
  READBACK
};

struct SoWgpuActionDiagnostic {
  SoWgpuRenderAction::Status status;
  SoWgpuDiagnosticDomain domain;
  SbString message;

  SoWgpuActionDiagnostic(SoWgpuRenderAction::Status s,
                         SoWgpuDiagnosticDomain d,
                         const SbString & text)
    : status(s), domain(d), message(text) {}
};

struct SoWgpuActionPhaseSample {
  double traversalMs = 0.0;
  double framePlanMs = 0.0;
  double backendMs = 0.0;
  size_t vertices = 0;
  size_t indices = 0;
  size_t draws = 0;
  bool planCacheHit = false;
  SoWgpuFrameReuseKind reuseKind = SoWgpuFrameReuseKind::UNKNOWN;
};

struct SoWgpuBridgePhaseSample {
  double packMs = 0.0;
  double ffiMs = 0.0;
  bool packCacheHit = false;
  SoWgpuFrameReuseKind packKind = SoWgpuFrameReuseKind::UNKNOWN;
};

// Language-facing policy for private WebGPU statuses, diagnostics and traces.
class SoWgpuDiagnosticShell {
public:
  COIN_WGPU_DLL_API static SoWgpuActionDiagnostic action(
    SoWgpuRenderAction::Status status,
    SoWgpuDiagnosticDomain domain,
    const SbString & message);
  COIN_WGPU_DLL_API static SoWgpuActionDiagnostic success(void);
  COIN_WGPU_DLL_API static SoWgpuActionDiagnostic fromBackend(
    const SubmitResult & result,
    SoWgpuDiagnosticDomain domain = SoWgpuDiagnosticDomain::BACKEND);
  COIN_WGPU_DLL_API static SoWgpuActionDiagnostic fromTarget(
    SoWgpuRenderTarget::Status status,
    const char * message);
  COIN_WGPU_DLL_API static SoWgpuActionDiagnostic withContext(
    SoWgpuRenderAction::Status status,
    SoWgpuDiagnosticDomain domain,
    const char * context,
    const SbString & message);

  COIN_WGPU_DLL_API static SoWgpuRenderAction::Status actionStatus(
    BackendStatus status);
  COIN_WGPU_DLL_API static SoWgpuRenderAction::Status actionStatus(
    SoWgpuRenderTarget::Status status);
  COIN_WGPU_DLL_API static const char * statusName(
    SoWgpuRenderAction::Status status);
  COIN_WGPU_DLL_API static const char * domainName(
    SoWgpuDiagnosticDomain domain);
  COIN_WGPU_DLL_API static const char * reuseKindName(
    SoWgpuFrameReuseKind kind);

  COIN_WGPU_DLL_API static bool phaseTracingEnabled(void);
  COIN_WGPU_DLL_API static std::string formatActionPhase(
    const SoWgpuActionPhaseSample & sample);
  COIN_WGPU_DLL_API static std::string formatBridgePhase(
    const SoWgpuBridgePhaseSample & sample);
};

#endif // !COIN_SOWGPUDIAGNOSTICSHELL_H
