#ifndef COIN_RENDER_FRAME_REUSE_CORE_H
#define COIN_RENDER_FRAME_REUSE_CORE_H

#include <Inventor/CoinRenderExport.h>

#include "rendering/coinrender/CoinRenderFramePlan.h"

#include <cstdint>

/** Mechanical relationship between two private CoinRenderFramePlan revisions. */
enum class CoinRenderFrameReuseKind {
  REUSE = 0,
  CAMERA_PATCH,
  RESOURCE_REBUILD,
  FULL_REBUILD,
  UNKNOWN
};

/**
 * Conservative reuse decision produced by Core and consumed by Wiring/Infra.
 *
 * baseRevision identifies the plan against which the decision was made. Infra
 * must fall back to a full pack when its currently owned data does not match
 * that revision.
 */
struct CoinRenderFrameReuseDecision {
  CoinRenderFrameReuseKind kind = CoinRenderFrameReuseKind::UNKNOWN;
  uint64_t baseRevision = 0;

  CoinRenderFrameReuseDecision() = default;
  CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind k, uint64_t base)
    : kind(k), baseRevision(base) {}
};

/** Only the camera-derived fields needed to restore a failed frame. */
struct CoinRenderCameraStateUndo {
  SbMatrix view = SbMatrix::identity();
  SbMatrix projectionCoin = SbMatrix::identity();
  float fogEnd = 0.0f;
};

/** Private transactional backup; never owns geometry or GPU resources. */
struct CoinRenderCameraOverlayUndo {
  uint64_t revision = 0;
  CoinRenderCameraSnapshot camera;
  std::vector<CoinRenderCameraStateUndo> states;
  bool active = false;
};

/** Traversal-free classification of the relationship between two plans. */
class CoinRenderFrameReuseCore {
public:
  COIN_RENDER_DLL_API static CoinRenderFrameReuseDecision classify(
    const CoinRenderFramePlan & previous,
    const CoinRenderFramePlan & current);
  COIN_RENDER_DLL_API static bool cameraOverlay(
    const CoinRenderFramePlan & previous, const CoinRenderCameraSnapshot & camera,
    uint64_t revision, CoinRenderFramePlan & result);
  /** Requires a previously validated plan; changes camera-derived fields only. */
  COIN_RENDER_DLL_API static bool beginCameraOverlay(
    CoinRenderFramePlan & plan, const CoinRenderCameraSnapshot & camera, uint64_t revision,
    CoinRenderCameraOverlayUndo & undo);
  /** Restore an uncommitted overlay after a failed frame execution. */
  COIN_RENDER_DLL_API static void rollbackCameraOverlay(
    CoinRenderFramePlan & plan, CoinRenderCameraOverlayUndo & undo);
};

#endif // !COIN_RENDER_FRAME_REUSE_CORE_H
