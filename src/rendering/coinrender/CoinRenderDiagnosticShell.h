#ifndef COIN_RENDER_DIAGNOSTIC_SHELL_H
#define COIN_RENDER_DIAGNOSTIC_SHELL_H

#include <Inventor/CoinRenderExport.h>
#include <Inventor/SbString.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>

#include "rendering/coinrender/CoinRenderBackend.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"

#include <cstddef>
#include <string>

enum class CoinRenderDiagnosticDomain {
  NONE = 0,
  ACTION,
  FRAME_PLAN,
  TARGET,
  BACKEND,
  READBACK
};

struct CoinRenderActionDiagnostic {
  CoinRenderAction::Status status;
  CoinRenderDiagnosticDomain domain;
  SbString message;

  CoinRenderActionDiagnostic(CoinRenderAction::Status s,
                         CoinRenderDiagnosticDomain d,
                         const SbString & text)
    : status(s), domain(d), message(text) {}
};

struct CoinRenderActionPhaseSample {
  double traversalMs = 0.0;
  double framePlanMs = 0.0;
  double backendMs = 0.0;
  size_t vertices = 0;
  size_t indices = 0;
  size_t draws = 0;
  bool planCacheHit = false;
  CoinRenderFrameReuseKind reuseKind = CoinRenderFrameReuseKind::UNKNOWN;
};

struct CoinWgpuBridgePhaseSample {
  double packMs = 0.0;
  double ffiMs = 0.0;
  bool packCacheHit = false;
  CoinRenderFrameReuseKind packKind = CoinRenderFrameReuseKind::UNKNOWN;
};

struct CoinBgfxPhaseSample {
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
class CoinRenderDiagnosticShell {
public:
  COIN_RENDER_DLL_API static CoinRenderActionDiagnostic action(
    CoinRenderAction::Status status,
    CoinRenderDiagnosticDomain domain,
    const SbString & message);
  COIN_RENDER_DLL_API static CoinRenderActionDiagnostic success(void);
  COIN_RENDER_DLL_API static CoinRenderActionDiagnostic fromBackend(
    const CoinRenderSubmitResult & result,
    CoinRenderDiagnosticDomain domain = CoinRenderDiagnosticDomain::BACKEND);
  COIN_RENDER_DLL_API static CoinRenderActionDiagnostic fromTarget(
    CoinRenderTarget::Status status,
    const char * message);
  COIN_RENDER_DLL_API static CoinRenderActionDiagnostic withContext(
    CoinRenderAction::Status status,
    CoinRenderDiagnosticDomain domain,
    const char * context,
    const SbString & message);

  COIN_RENDER_DLL_API static CoinRenderAction::Status actionStatus(
    CoinRenderBackendStatus status);
  COIN_RENDER_DLL_API static CoinRenderAction::Status actionStatus(
    CoinRenderTarget::Status status);
  COIN_RENDER_DLL_API static const char * statusName(
    CoinRenderAction::Status status);
  COIN_RENDER_DLL_API static const char * domainName(
    CoinRenderDiagnosticDomain domain);
  COIN_RENDER_DLL_API static const char * reuseKindName(
    CoinRenderFrameReuseKind kind);

  COIN_RENDER_DLL_API static const char * environmentOption(const char * name);
  COIN_RENDER_DLL_API static bool phaseTracingEnabled(void);
  COIN_RENDER_DLL_API static CoinRenderOptions renderOptions(std::string& diagnostic);
  COIN_RENDER_DLL_API static CoinRenderRenderer rendererOption(std::string& diagnostic);
  COIN_RENDER_DLL_API static bool diagnosticCpuDepthFill(void);
  COIN_RENDER_DLL_API static std::string formatActionPhase(
    const CoinRenderActionPhaseSample & sample);
  COIN_RENDER_DLL_API static std::string formatBridgePhase(
    const CoinWgpuBridgePhaseSample & sample);
  COIN_RENDER_DLL_API static std::string formatBgfxPhase(
    const CoinBgfxPhaseSample & sample);
};

#endif // !COIN_RENDER_DIAGNOSTIC_SHELL_H
