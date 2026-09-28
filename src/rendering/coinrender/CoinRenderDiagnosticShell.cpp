#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinrender/CoinRenderDiagnosticShell.h"

#include <cstdlib>
#include <locale>
#include <iomanip>
#include <sstream>

CoinRenderActionDiagnostic
CoinRenderDiagnosticShell::action(CoinRenderAction::Status status,
                              CoinRenderDiagnosticDomain domain,
                              const SbString & message)
{
  return CoinRenderActionDiagnostic(status, domain, message);
}

CoinRenderActionDiagnostic
CoinRenderDiagnosticShell::success(void)
{
  return action(CoinRenderAction::SUCCESS,
                CoinRenderDiagnosticDomain::NONE, SbString(""));
}

CoinRenderActionDiagnostic
CoinRenderDiagnosticShell::fromBackend(const CoinRenderSubmitResult & result,
                                   CoinRenderDiagnosticDomain domain)
{
  return action(actionStatus(result.status), domain,
                SbString(result.diagnostic.c_str()));
}

CoinRenderActionDiagnostic
CoinRenderDiagnosticShell::fromTarget(CoinRenderTarget::Status status,
                                  const char * message)
{
  return action(actionStatus(status), CoinRenderDiagnosticDomain::TARGET,
                SbString(message ? message : ""));
}

CoinRenderActionDiagnostic
CoinRenderDiagnosticShell::withContext(CoinRenderAction::Status status,
                                   CoinRenderDiagnosticDomain domain,
                                   const char * context,
                                   const SbString & message)
{
  SbString text(context ? context : "");
  text += ": ";
  text += message;
  return action(status, domain, text);
}

CoinRenderAction::Status
CoinRenderDiagnosticShell::actionStatus(CoinRenderBackendStatus status)
{
  switch (status) {
    case CoinRenderBackendStatus::SUCCESS: return CoinRenderAction::SUCCESS;
    case CoinRenderBackendStatus::NOT_READY: return CoinRenderAction::NOT_READY;
    case CoinRenderBackendStatus::UNSUPPORTED: return CoinRenderAction::UNSUPPORTED;
    case CoinRenderBackendStatus::OUT_OF_MEMORY: return CoinRenderAction::OUT_OF_MEMORY;
    case CoinRenderBackendStatus::DEVICE_LOST: return CoinRenderAction::DEVICE_LOST;
    case CoinRenderBackendStatus::SURFACE_LOST: return CoinRenderAction::SURFACE_LOST;
    case CoinRenderBackendStatus::BACKEND_ERROR:
    default: return CoinRenderAction::BACKEND_ERROR;
  }
}

CoinRenderAction::Status
CoinRenderDiagnosticShell::actionStatus(CoinRenderTarget::Status status)
{
  switch (status) {
    case CoinRenderTarget::TARGET_READY: return CoinRenderAction::SUCCESS;
    case CoinRenderTarget::TARGET_NOT_READY: return CoinRenderAction::NOT_READY;
    case CoinRenderTarget::TARGET_LOST: return CoinRenderAction::DEVICE_LOST;
    case CoinRenderTarget::TARGET_SURFACE_LOST: return CoinRenderAction::SURFACE_LOST;
    case CoinRenderTarget::TARGET_ERROR:
    default: return CoinRenderAction::BACKEND_ERROR;
  }
}

const char *
CoinRenderDiagnosticShell::statusName(CoinRenderAction::Status status)
{
  switch (status) {
    case CoinRenderAction::SUCCESS: return "SUCCESS";
    case CoinRenderAction::NO_TARGET: return "NO_TARGET";
    case CoinRenderAction::NOT_READY: return "NOT_READY";
    case CoinRenderAction::INVALID_SCENE: return "INVALID_SCENE";
    case CoinRenderAction::UNSUPPORTED: return "UNSUPPORTED";
    case CoinRenderAction::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
    case CoinRenderAction::DEVICE_LOST: return "DEVICE_LOST";
    case CoinRenderAction::SURFACE_LOST: return "SURFACE_LOST";
    case CoinRenderAction::BACKEND_ERROR:
    default: return "BACKEND_ERROR";
  }
}

const char *
CoinRenderDiagnosticShell::domainName(CoinRenderDiagnosticDomain domain)
{
  switch (domain) {
    case CoinRenderDiagnosticDomain::NONE: return "none";
    case CoinRenderDiagnosticDomain::ACTION: return "action";
    case CoinRenderDiagnosticDomain::FRAME_PLAN: return "frame_plan";
    case CoinRenderDiagnosticDomain::TARGET: return "target";
    case CoinRenderDiagnosticDomain::BACKEND: return "backend";
    case CoinRenderDiagnosticDomain::READBACK: return "readback";
    default: return "unknown";
  }
}

const char *
CoinRenderDiagnosticShell::reuseKindName(CoinRenderFrameReuseKind kind)
{
  switch (kind) {
    case CoinRenderFrameReuseKind::REUSE: return "reuse";
    case CoinRenderFrameReuseKind::CAMERA_PATCH: return "camera_patch";
    case CoinRenderFrameReuseKind::RESOURCE_REBUILD: return "resource_rebuild";
    case CoinRenderFrameReuseKind::FULL_REBUILD: return "full_rebuild";
    case CoinRenderFrameReuseKind::UNKNOWN: return "unknown";
    default: return "unknown";
  }
}

const char *
CoinRenderDiagnosticShell::environmentOption(const char * name)
{
  const char * value = std::getenv(name);
  if (value) return value;
  const std::string option(name);
  const std::string prefix("COIN_RENDER_");
  if (option.compare(0, prefix.size(), prefix) != 0) return NULL;
  const std::string legacy = "COIN_WGPU_" + option.substr(prefix.size());
  return std::getenv(legacy.c_str());
}

bool
CoinRenderDiagnosticShell::phaseTracingEnabled(void)
{
  return environmentOption("COIN_RENDER_TRACE_PHASES") != NULL;
}

std::string
CoinRenderDiagnosticShell::formatActionPhase(const CoinRenderActionPhaseSample & sample)
{
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << "COIN_RENDER_PHASE action traversal_ms=" << sample.traversalMs
         << " frame_plan_ms=" << sample.framePlanMs
         << " backend_ms=" << sample.backendMs
         << " vertices=" << sample.vertices
         << " indices=" << sample.indices
         << " draws=" << sample.draws
         << " plan_cache_hit=" << (sample.planCacheHit ? 1 : 0)
         << " plan_reuse=" << reuseKindName(sample.reuseKind);
  return stream.str();
}

std::string
CoinRenderDiagnosticShell::formatBgfxPhase(const SoWgpuBgfxPhaseSample & sample)
{
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::fixed << std::setprecision(6)
         << "COIN_RENDER_PHASE bgfx lower_ms=" << sample.lowerMs
         << " upload_ms=" << sample.uploadMs
         << " encode_ms=" << sample.encodeMs
         << " draw_encode_ms=" << sample.drawEncodeMs
         << " blit_encode_ms=" << sample.blitEncodeMs
         << " submit_frame_ms=" << sample.submitFrameMs
         << " read_request_ms=" << sample.readRequestMs
         << " read_wait_ms=" << sample.readWaitMs
         << " frame_wait_ms=" << sample.frameWaitMs
         << " row_flip_ms=" << sample.rowFlipMs
         << " vertices=" << sample.vertices
         << " draws=" << sample.draws
         << " read_wait_frames=" << sample.readWaitFrames
         << " readback_pipeline_depth=" << sample.readbackPipelineDepth
         << " readback_latency_frames=" << sample.readbackLatencyFrames
         << " readback_pipeline_bytes=" << sample.readbackPipelineBytes
         << " readback_gpu_staging_bytes=" << sample.readbackGpuStagingBytes
         << " readback_cpu_staging_bytes=" << sample.readbackCpuStagingBytes
         << " readback_published_bytes=" << sample.readbackPublishedBytes
         << " readback_bootstrap=" << (sample.readbackBootstrap ? 1 : 0)
         << " resource_cache_hit=" << (sample.resourceCacheHit ? 1 : 0)
         << " texture_cache_hit=" << (sample.textureCacheHit ? 1 : 0)
         << " geometry_buffer_reused=" << (sample.geometryBufferReused ? 1 : 0)
         << " vertex_buffer_capacity=" << sample.vertexBufferCapacity
         << " index_buffer_capacity=" << sample.indexBufferCapacity
         << " camera_patch=" << (sample.cameraPatchUsed ? 1 : 0)
         << " material_patch=" << (sample.materialPatchUsed ? 1 : 0)
         << " material_patch_ranges=" << sample.materialPatchRanges
         << " material_patch_vertices=" << sample.materialPatchVertices;
  stream << " gpu_query_drain_ms=" << sample.gpuQueryDrainMs
         << " gpu_query_frames=" << sample.gpuQueryFrames
         << " gpu_pass_frame=" << sample.gpuPassFrame;
  if (sample.gpuFrameMs >= 0.0) {
    stream << " gpu_frame_ms=" << sample.gpuFrameMs;
  } else if (!sample.gpuTimingRequested) {
    stream << " gpu_frame_ms=not_requested";
  } else {
    stream << " gpu_frame_ms=unavailable";
  }
  const auto gpuPhase = [&stream](const char * name, double value) {
    stream << ' ' << name << '=';
    if (value >= 0.0) stream << value;
    else stream << "unavailable";
  };
  gpuPhase("gpu_opaque_ms", sample.gpuOpaqueMs);
  gpuPhase("gpu_transparent_ms", sample.gpuTransparentMs);
  gpuPhase("gpu_composite_ms", sample.gpuCompositeMs);
  gpuPhase("gpu_blit_readback_ms", sample.gpuBlitReadbackMs);
  gpuPhase("gpu_wait_ms", sample.gpuWaitMs);
  stream << " gpu_resource_stats=" << (sample.gpuResourceStatsAvailable ? 1 : 0)
         << " gpu_draw_submits=" << sample.gpuDrawSubmits
         << " opaque_draws=" << sample.opaqueDraws
         << " transparent_draws=" << sample.transparentDraws
         << " opaque_grouping=" << (sample.opaqueGroupingEnabled ? 1 : 0)
         << " opaque_reordered=" << (sample.opaqueOrderChanged ? 1 : 0)
         << " logical_pipeline_changes=" << sample.logicalPipelineChanges
         << " logical_material_changes=" << sample.logicalMaterialChanges
         << " logical_texture_changes=" << sample.logicalTextureChanges
         << " logical_lighting_changes=" << sample.logicalLightingChanges
         << " gpu_memory_used_bytes=" << sample.gpuMemoryUsedBytes
         << " texture_memory_used_bytes=" << sample.textureMemoryUsedBytes
         << " render_target_memory_used_bytes=" << sample.renderTargetMemoryUsedBytes
         << " gpu_vertex_buffers=" << sample.gpuVertexBuffers
         << " gpu_index_buffers=" << sample.gpuIndexBuffers
         << " gpu_textures=" << sample.gpuTextures
         << " gpu_framebuffers=" << sample.gpuFrameBuffers
         << " gpu_programs=" << sample.gpuPrograms;

  return stream.str();
}

std::string
CoinRenderDiagnosticShell::formatBridgePhase(const CoinWgpuBridgePhaseSample & sample)
{
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << "COIN_RENDER_PHASE bridge pack_ms=" << sample.packMs
         << " pack_cache_hit=" << (sample.packCacheHit ? 1 : 0)
         << " ffi_ms=" << sample.ffiMs
         << " pack_mode=" << reuseKindName(sample.packKind);
  return stream.str();
}
