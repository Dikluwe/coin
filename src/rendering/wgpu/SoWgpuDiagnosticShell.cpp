#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuDiagnosticShell.h"

#include <cstdlib>
#include <locale>
#include <iomanip>
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

const char *
SoWgpuDiagnosticShell::reuseKindName(SoWgpuFrameReuseKind kind)
{
  switch (kind) {
    case SoWgpuFrameReuseKind::REUSE: return "reuse";
    case SoWgpuFrameReuseKind::CAMERA_PATCH: return "camera_patch";
    case SoWgpuFrameReuseKind::RESOURCE_REBUILD: return "resource_rebuild";
    case SoWgpuFrameReuseKind::FULL_REBUILD: return "full_rebuild";
    case SoWgpuFrameReuseKind::UNKNOWN: return "unknown";
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
         << " plan_cache_hit=" << (sample.planCacheHit ? 1 : 0)
         << " plan_reuse=" << reuseKindName(sample.reuseKind);
  return stream.str();
}

std::string
SoWgpuDiagnosticShell::formatBgfxPhase(const SoWgpuBgfxPhaseSample & sample)
{
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::fixed << std::setprecision(6)
         << "COIN_WGPU_PHASE bgfx lower_ms=" << sample.lowerMs
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
SoWgpuDiagnosticShell::formatBridgePhase(const SoWgpuBridgePhaseSample & sample)
{
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << "COIN_WGPU_PHASE bridge pack_ms=" << sample.packMs
         << " pack_cache_hit=" << (sample.packCacheHit ? 1 : 0)
         << " ffi_ms=" << sample.ffiMs
         << " pack_mode=" << reuseKindName(sample.packKind);
  return stream.str();
}
