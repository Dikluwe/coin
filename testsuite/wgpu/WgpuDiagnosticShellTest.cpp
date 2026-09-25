#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuDiagnosticShell.h"

#include <cstring>
#include <iostream>
#include <string>

namespace {
bool
check(bool condition, const char * message)
{
  if (!condition) std::cerr << "WgpuDiagnosticShellTest: " << message << '\n';
  return condition;
}
}

int
main()
{
  bool ok = true;
  struct Mapping {
    BackendStatus backend;
    SoWgpuRenderAction::Status action;
  } mappings[] = {
    {BackendStatus::SUCCESS, SoWgpuRenderAction::SUCCESS},
    {BackendStatus::NOT_READY, SoWgpuRenderAction::NOT_READY},
    {BackendStatus::UNSUPPORTED, SoWgpuRenderAction::UNSUPPORTED},
    {BackendStatus::OUT_OF_MEMORY, SoWgpuRenderAction::OUT_OF_MEMORY},
    {BackendStatus::DEVICE_LOST, SoWgpuRenderAction::DEVICE_LOST},
    {BackendStatus::BACKEND_ERROR, SoWgpuRenderAction::BACKEND_ERROR},
    {BackendStatus::SURFACE_LOST, SoWgpuRenderAction::SURFACE_LOST}
  };
  for (const Mapping & mapping : mappings) {
    ok &= check(SoWgpuDiagnosticShell::actionStatus(mapping.backend) == mapping.action,
                "backend-to-action mapping changed");
  }

  const SubmitResult backend(BackendStatus::DEVICE_LOST, "device diagnostic", 17);
  const SoWgpuActionDiagnostic diagnostic =
    SoWgpuDiagnosticShell::fromBackend(backend);
  ok &= check(diagnostic.status == SoWgpuRenderAction::DEVICE_LOST &&
              diagnostic.domain == SoWgpuDiagnosticDomain::BACKEND &&
              diagnostic.message == SbString("device diagnostic"),
              "structured backend diagnostic lost information");

  const SoWgpuActionDiagnostic child = SoWgpuDiagnosticShell::withContext(
    SoWgpuRenderAction::UNSUPPORTED, SoWgpuDiagnosticDomain::ACTION,
    "SoSceneTexture2 subscene", SbString("unsupported state"));
  ok &= check(child.message ==
              SbString("SoSceneTexture2 subscene: unsupported state"),
              "diagnostic context formatting changed");

  SoWgpuActionPhaseSample actionPhase;
  actionPhase.traversalMs = 1.25;
  actionPhase.framePlanMs = 2.5;
  actionPhase.backendMs = 3.75;
  actionPhase.vertices = 4;
  actionPhase.indices = 5;
  actionPhase.draws = 6;
  actionPhase.planCacheHit = true;
  actionPhase.reuseKind = SoWgpuFrameReuseKind::CAMERA_PATCH;
  ok &= check(SoWgpuDiagnosticShell::formatActionPhase(actionPhase) ==
    "COIN_WGPU_PHASE action traversal_ms=1.25 frame_plan_ms=2.5 backend_ms=3.75 vertices=4 indices=5 draws=6 plan_cache_hit=1 plan_reuse=camera_patch",
    "action phase line changed");

  SoWgpuBridgePhaseSample bridgePhase;
  bridgePhase.packMs = 0.125;
  bridgePhase.ffiMs = 0.5;
  bridgePhase.packKind = SoWgpuFrameReuseKind::RESOURCE_REBUILD;
  ok &= check(SoWgpuDiagnosticShell::formatBridgePhase(bridgePhase) ==
    "COIN_WGPU_PHASE bridge pack_ms=0.125 pack_cache_hit=0 ffi_ms=0.5 pack_mode=resource_rebuild",
    "bridge phase line changed");

  SoWgpuBgfxPhaseSample bgfxPhase;
  bgfxPhase.cameraPatchUsed = true;
  bgfxPhase.resourceCacheHit = true;
  bgfxPhase.readWaitFrames = 3;
  bgfxPhase.gpuTimingRequested = true;
  bgfxPhase.gpuFrameMs = 0.125;
  bgfxPhase.gpuQueryDrainMs = 0.05;
  bgfxPhase.gpuQueryFrames = 1;
  const std::string bgfxTrace = SoWgpuDiagnosticShell::formatBgfxPhase(bgfxPhase);
  ok &= check(bgfxTrace.find("camera_patch=1") != std::string::npos &&
              bgfxTrace.find("read_wait_frames=3") != std::string::npos &&
              bgfxTrace.find("gpu_frame_ms=0.125000") != std::string::npos &&
              bgfxTrace.find("gpu_query_frames=1") != std::string::npos,
              "BGFX phase trace lost camera or GPU timing fields");
  bgfxPhase.gpuFrameMs = -1.0;
  ok &= check(SoWgpuDiagnosticShell::formatBgfxPhase(bgfxPhase).find(
                "gpu_frame_ms=unavailable") != std::string::npos,
              "BGFX unavailable GPU timestamp must be explicit");
  ok &= check(std::strcmp(SoWgpuDiagnosticShell::statusName(
                SoWgpuRenderAction::SURFACE_LOST), "SURFACE_LOST") == 0 &&
              std::strcmp(SoWgpuDiagnosticShell::domainName(
                SoWgpuDiagnosticDomain::FRAME_PLAN), "frame_plan") == 0,
              "stable status/domain names changed");
  ok &= check(std::strcmp(SoWgpuDiagnosticShell::reuseKindName(
                SoWgpuFrameReuseKind::FULL_REBUILD), "full_rebuild") == 0,
              "reuse kind name changed");

  if (!ok) return 1;
  std::cout << "WgpuDiagnosticShellTest passed\n";
  return 0;
}
