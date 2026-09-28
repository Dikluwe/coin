#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinrender/CoinRenderDiagnosticShell.h"

#include <cstring>
#include <iostream>
#include <string>

namespace {
bool
check(bool condition, const char * message)
{
  if (!condition) std::cerr << "CoinRenderDiagnosticShellTest: " << message << '\n';
  return condition;
}
}

int
main()
{
  bool ok = true;
  struct Mapping {
    CoinRenderBackendStatus backend;
    CoinRenderAction::Status action;
  } mappings[] = {
    {CoinRenderBackendStatus::SUCCESS, CoinRenderAction::SUCCESS},
    {CoinRenderBackendStatus::NOT_READY, CoinRenderAction::NOT_READY},
    {CoinRenderBackendStatus::UNSUPPORTED, CoinRenderAction::UNSUPPORTED},
    {CoinRenderBackendStatus::OUT_OF_MEMORY, CoinRenderAction::OUT_OF_MEMORY},
    {CoinRenderBackendStatus::DEVICE_LOST, CoinRenderAction::DEVICE_LOST},
    {CoinRenderBackendStatus::BACKEND_ERROR, CoinRenderAction::BACKEND_ERROR},
    {CoinRenderBackendStatus::SURFACE_LOST, CoinRenderAction::SURFACE_LOST}
  };
  for (const Mapping & mapping : mappings) {
    ok &= check(CoinRenderDiagnosticShell::actionStatus(mapping.backend) == mapping.action,
                "backend-to-action mapping changed");
  }

  const CoinRenderSubmitResult backend(CoinRenderBackendStatus::DEVICE_LOST, "device diagnostic", 17);
  const CoinRenderActionDiagnostic diagnostic =
    CoinRenderDiagnosticShell::fromBackend(backend);
  ok &= check(diagnostic.status == CoinRenderAction::DEVICE_LOST &&
              diagnostic.domain == CoinRenderDiagnosticDomain::BACKEND &&
              diagnostic.message == SbString("device diagnostic"),
              "structured backend diagnostic lost information");

  const CoinRenderActionDiagnostic child = CoinRenderDiagnosticShell::withContext(
    CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::ACTION,
    "SoSceneTexture2 subscene", SbString("unsupported state"));
  ok &= check(child.message ==
              SbString("SoSceneTexture2 subscene: unsupported state"),
              "diagnostic context formatting changed");

  CoinRenderActionPhaseSample actionPhase;
  actionPhase.traversalMs = 1.25;
  actionPhase.framePlanMs = 2.5;
  actionPhase.backendMs = 3.75;
  actionPhase.vertices = 4;
  actionPhase.indices = 5;
  actionPhase.draws = 6;
  actionPhase.planCacheHit = true;
  actionPhase.reuseKind = CoinRenderFrameReuseKind::CAMERA_PATCH;
  ok &= check(CoinRenderDiagnosticShell::formatActionPhase(actionPhase) ==
    "COIN_RENDER_PHASE action traversal_ms=1.25 frame_plan_ms=2.5 backend_ms=3.75 vertices=4 indices=5 draws=6 plan_cache_hit=1 plan_reuse=camera_patch",
    "action phase line changed");

  CoinWgpuBridgePhaseSample bridgePhase;
  bridgePhase.packMs = 0.125;
  bridgePhase.ffiMs = 0.5;
  bridgePhase.packKind = CoinRenderFrameReuseKind::RESOURCE_REBUILD;
  ok &= check(CoinRenderDiagnosticShell::formatBridgePhase(bridgePhase) ==
    "COIN_RENDER_PHASE bridge pack_ms=0.125 pack_cache_hit=0 ffi_ms=0.5 pack_mode=resource_rebuild",
    "bridge phase line changed");

  SoWgpuBgfxPhaseSample bgfxPhase;
  bgfxPhase.cameraPatchUsed = true;
  bgfxPhase.materialPatchUsed = true;
  bgfxPhase.materialPatchRanges = 2;
  bgfxPhase.materialPatchVertices = 6;
  bgfxPhase.resourceCacheHit = true;
  bgfxPhase.textureCacheHit = true;
  bgfxPhase.geometryBufferReused = true;
  bgfxPhase.vertexBufferCapacity = 512;
  bgfxPhase.indexBufferCapacity = 1024;
  bgfxPhase.readWaitFrames = 3;
  bgfxPhase.readbackPipelineDepth = 3;
  bgfxPhase.readbackLatencyFrames = 2;
  bgfxPhase.readbackPipelineBytes = 12288;
  bgfxPhase.readbackGpuStagingBytes = 6144;
  bgfxPhase.readbackCpuStagingBytes = 6144;
  bgfxPhase.readbackPublishedBytes = 2048;
  bgfxPhase.readbackBootstrap = true;
  bgfxPhase.gpuTimingRequested = true;
  bgfxPhase.gpuFrameMs = 0.125;
  bgfxPhase.gpuOpaqueMs = 0.025;
  bgfxPhase.gpuTransparentMs = 0.05;
  bgfxPhase.gpuCompositeMs = 0.0125;
  bgfxPhase.gpuBlitReadbackMs = 0.02;
  bgfxPhase.gpuWaitMs = 0.01;
  bgfxPhase.gpuPassFrame = 7;
  bgfxPhase.gpuResourceStatsAvailable = true;
  bgfxPhase.gpuDrawSubmits = 9;
  bgfxPhase.opaqueDraws = 3;
  bgfxPhase.transparentDraws = 2;
  bgfxPhase.logicalPipelineChanges = 2;
  bgfxPhase.logicalMaterialChanges = 3;
  bgfxPhase.logicalTextureChanges = 2;
  bgfxPhase.logicalLightingChanges = 1;
  bgfxPhase.opaqueGroupingEnabled = true;
  bgfxPhase.opaqueOrderChanged = true;
  bgfxPhase.gpuMemoryUsedBytes = 1024;
  bgfxPhase.textureMemoryUsedBytes = 512;
  bgfxPhase.renderTargetMemoryUsedBytes = 256;
  bgfxPhase.gpuTextures = 4;
  bgfxPhase.drawEncodeMs = 0.25;
  bgfxPhase.blitEncodeMs = 0.375;
  bgfxPhase.gpuQueryDrainMs = 0.05;
  bgfxPhase.gpuQueryFrames = 1;
  bgfxPhase.frameWaitMs = 0.5;
  bgfxPhase.rowFlipMs = 0.125;
  const std::string bgfxTrace = CoinRenderDiagnosticShell::formatBgfxPhase(bgfxPhase);
  ok &= check(bgfxTrace.find("camera_patch=1") != std::string::npos &&
              bgfxTrace.find("material_patch=1") != std::string::npos &&
              bgfxTrace.find("material_patch_ranges=2") != std::string::npos &&
              bgfxTrace.find("material_patch_vertices=6") != std::string::npos &&
              bgfxTrace.find("texture_cache_hit=1") != std::string::npos &&
              bgfxTrace.find("geometry_buffer_reused=1") != std::string::npos &&
              bgfxTrace.find("vertex_buffer_capacity=512") != std::string::npos &&
              bgfxTrace.find("index_buffer_capacity=1024") != std::string::npos &&
              bgfxTrace.find("read_wait_frames=3") != std::string::npos &&
              bgfxTrace.find("readback_pipeline_depth=3") != std::string::npos &&
              bgfxTrace.find("readback_latency_frames=2") != std::string::npos &&
              bgfxTrace.find("readback_pipeline_bytes=12288") != std::string::npos &&
              bgfxTrace.find("readback_gpu_staging_bytes=6144") != std::string::npos &&
              bgfxTrace.find("readback_cpu_staging_bytes=6144") != std::string::npos &&
              bgfxTrace.find("readback_published_bytes=2048") != std::string::npos &&
              bgfxTrace.find("readback_bootstrap=1") != std::string::npos &&
              bgfxTrace.find("draw_encode_ms=0.250000") != std::string::npos &&
              bgfxTrace.find("blit_encode_ms=0.375000") != std::string::npos &&
              bgfxTrace.find("frame_wait_ms=0.500000") != std::string::npos &&
              bgfxTrace.find("row_flip_ms=0.125000") != std::string::npos &&
              bgfxTrace.find("gpu_frame_ms=0.125000") != std::string::npos &&
              bgfxTrace.find("gpu_opaque_ms=0.025000") != std::string::npos &&
              bgfxTrace.find("gpu_transparent_ms=0.050000") != std::string::npos &&
              bgfxTrace.find("gpu_composite_ms=0.012500") != std::string::npos &&
              bgfxTrace.find("gpu_blit_readback_ms=0.020000") != std::string::npos &&
              bgfxTrace.find("gpu_wait_ms=0.010000") != std::string::npos &&
              bgfxTrace.find("gpu_pass_frame=7") != std::string::npos &&
              bgfxTrace.find("gpu_resource_stats=1") != std::string::npos &&
              bgfxTrace.find("gpu_draw_submits=9") != std::string::npos &&
              bgfxTrace.find("opaque_draws=3") != std::string::npos &&
              bgfxTrace.find("transparent_draws=2") != std::string::npos &&
              bgfxTrace.find("opaque_grouping=1") != std::string::npos &&
              bgfxTrace.find("opaque_reordered=1") != std::string::npos &&
              bgfxTrace.find("logical_pipeline_changes=2") != std::string::npos &&
              bgfxTrace.find("logical_material_changes=3") != std::string::npos &&
              bgfxTrace.find("logical_texture_changes=2") != std::string::npos &&
              bgfxTrace.find("logical_lighting_changes=1") != std::string::npos &&
              bgfxTrace.find("gpu_memory_used_bytes=1024") != std::string::npos &&
              bgfxTrace.find("gpu_textures=4") != std::string::npos &&
              bgfxTrace.find("gpu_query_frames=1") != std::string::npos,
              "BGFX phase trace lost camera or GPU timing fields");
  bgfxPhase.gpuFrameMs = -1.0;
  ok &= check(CoinRenderDiagnosticShell::formatBgfxPhase(bgfxPhase).find(
                "gpu_frame_ms=unavailable") != std::string::npos,
              "BGFX unavailable GPU timestamp must be explicit");
  ok &= check(std::strcmp(CoinRenderDiagnosticShell::statusName(
                CoinRenderAction::SURFACE_LOST), "SURFACE_LOST") == 0 &&
              std::strcmp(CoinRenderDiagnosticShell::domainName(
                CoinRenderDiagnosticDomain::FRAME_PLAN), "frame_plan") == 0,
              "stable status/domain names changed");
  ok &= check(std::strcmp(CoinRenderDiagnosticShell::reuseKindName(
                CoinRenderFrameReuseKind::FULL_REBUILD), "full_rebuild") == 0,
              "reuse kind name changed");

  if (!ok) return 1;
  std::cout << "CoinRenderDiagnosticShellTest passed\n";
  return 0;
}
