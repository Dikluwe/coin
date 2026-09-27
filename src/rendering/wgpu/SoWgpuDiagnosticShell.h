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

struct SoWgpuBgfxPhaseSample {
  double lowerMs = 0.0;
  double uploadMs = 0.0;
  double encodeMs = 0.0;
  double drawEncodeMs = 0.0;
  double blitEncodeMs = 0.0;
  double submitFrameMs = 0.0;
  double readRequestMs = 0.0;
  double readWaitMs = 0.0;
  double frameWaitMs = 0.0;
  double rowFlipMs = 0.0;
  double gpuFrameMs = -1.0;
  double gpuOpaqueMs = -1.0;
  double gpuTransparentMs = -1.0;
  double gpuCompositeMs = -1.0;
  double gpuBlitReadbackMs = -1.0;
  double gpuWaitMs = -1.0;
  double gpuQueryDrainMs = 0.0;
  uint32_t gpuQueryFrames = 0;
  uint32_t gpuPassFrame = 0;
  uint32_t gpuDrawSubmits = 0;
  uint32_t opaqueDraws = 0;
  uint32_t transparentDraws = 0;
  uint32_t logicalPipelineChanges = 0;
  uint32_t logicalMaterialChanges = 0;
  uint32_t logicalTextureChanges = 0;
  uint32_t logicalLightingChanges = 0;
  bool opaqueGroupingEnabled = false;
  bool opaqueOrderChanged = false;
  int64_t gpuMemoryUsedBytes = -1;
  int64_t textureMemoryUsedBytes = -1;
  int64_t renderTargetMemoryUsedBytes = -1;
  uint16_t gpuVertexBuffers = 0;
  uint16_t gpuIndexBuffers = 0;
  uint16_t gpuTextures = 0;
  uint16_t gpuFrameBuffers = 0;
  uint16_t gpuPrograms = 0;
  bool gpuResourceStatsAvailable = false;
  size_t vertices = 0;
  size_t draws = 0;
  uint32_t readWaitFrames = 0;
  uint32_t readbackPipelineDepth = 0;
  uint32_t readbackLatencyFrames = 0;
  uint64_t readbackPipelineBytes = 0;
  uint64_t readbackGpuStagingBytes = 0;
  uint64_t readbackCpuStagingBytes = 0;
  uint64_t readbackPublishedBytes = 0;
  bool readbackBootstrap = false;
  bool resourceCacheHit = false;
  bool textureCacheHit = false;
  bool geometryBufferReused = false;
  uint32_t vertexBufferCapacity = 0;
  uint32_t indexBufferCapacity = 0;
  bool cameraPatchUsed = false;
  bool materialPatchUsed = false;
  uint32_t materialPatchRanges = 0;
  uint32_t materialPatchVertices = 0;
  bool gpuTimingRequested = false;
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
  COIN_WGPU_DLL_API static std::string formatBgfxPhase(
    const SoWgpuBgfxPhaseSample & sample);
};

#endif // !COIN_SOWGPUDIAGNOSTICSHELL_H
