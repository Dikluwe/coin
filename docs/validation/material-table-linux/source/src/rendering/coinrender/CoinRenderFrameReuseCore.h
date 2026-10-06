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
  std::vector<CoinRenderLightingSnapshot> lighting;
  bool active = false;
};

/** Captured values only; the owner is the stable, private plan object. */
struct CoinRenderCameraOverlayBasis {
  const CoinRenderFramePlan * owner = nullptr;
  uint64_t revision = 0;
  size_t stateCount = 0;
  CoinRenderCameraSnapshot camera;
  std::vector<CoinRenderLightingSnapshot> referenceLighting;
  std::vector<CoinRenderLightingSnapshot> worldLighting;
};

struct CoinRenderModelUpdate {
  uint32_t stateSlot = 0;
  SbMatrix model = SbMatrix::identity();
  uint32_t sortingDrawSlot = UINT32_MAX;
};
struct CoinRenderTranslationStateUndo {
  CoinRenderModelUpdate state;
  float sortingCenterWorld[3] = {0, 0, 0};
};
struct CoinRenderTranslationOverlayUndo {
  uint64_t revision = 0;
  std::vector<CoinRenderTranslationStateUndo> states;
  bool active = false;
};

struct CoinRenderMaterialUpdate {
  uint32_t slot = 0;
  CoinRenderMaterialSnapshot material;
  CoinRenderMaterialUpdate() = default;
  CoinRenderMaterialUpdate(uint32_t slot, const CoinRenderMaterialSnapshot & material)
    : slot(slot), material(material) {}
};
struct CoinRenderPositionUpdate {
  uint32_t slot = 0;
  SbVec3f position;
  CoinRenderPositionUpdate() = default;
  CoinRenderPositionUpdate(uint32_t slot, const SbVec3f & position)
    : slot(slot), position(position) {}
};
// Both pending positions and their undo fit within 32 MiB. Full vertex
// attributes and the source plan are never copied for an object overlay.
static const size_t COIN_RENDER_OBJECT_OVERLAY_MAX_POSITIONS =
  (32 * 1024 * 1024) / (2 * sizeof(CoinRenderPositionUpdate));
struct CoinRenderDrawSourceUpdate {
  uint32_t slot = 0;
  SbUniqueId sourceNodeId = 0;
  CoinRenderDrawSourceUpdate() = default;
  CoinRenderDrawSourceUpdate(uint32_t slot, SbUniqueId sourceNodeId)
    : slot(slot), sourceNodeId(sourceNodeId) {}
};
struct CoinRenderObjectOverlayUndo {
  uint64_t revision = 0;
  CoinRenderTranslationOverlayUndo transforms;
  std::vector<CoinRenderMaterialUpdate> materials;
  std::vector<CoinRenderPositionUpdate> positions;
  std::vector<CoinRenderDrawSourceUpdate> draws;
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
  /** Qualify once after a full capture, under Wiring's notification proof. */
  COIN_RENDER_DLL_API static bool prepareCameraOverlayBasis(
    const CoinRenderFramePlan & plan, CoinRenderCameraOverlayBasis & basis);
  /** Apply from the original world basis, avoiding accumulated light drift. */
  COIN_RENDER_DLL_API static bool beginCameraOverlay(
    CoinRenderFramePlan & plan, const CoinRenderCameraSnapshot & camera, uint64_t revision,
    CoinRenderCameraOverlayUndo & undo, const CoinRenderCameraOverlayBasis & basis);
  /** Restore an uncommitted overlay after a failed frame execution. */
  COIN_RENDER_DLL_API static void rollbackCameraOverlay(
    CoinRenderFramePlan & plan, CoinRenderCameraOverlayUndo & undo);
  /** Zero-center local translation; retain the captured linear transform. */
  COIN_RENDER_DLL_API static bool translatedModel(
    const SbMatrix & anchor, const SbMatrix & prefix, const SbVec3f & translation,
    SbMatrix & result);
  COIN_RENDER_DLL_API static bool beginTranslationOverlay(
    CoinRenderFramePlan & plan, const std::vector<CoinRenderModelUpdate> & updates,
    uint64_t revision, CoinRenderTranslationOverlayUndo & undo);
  COIN_RENDER_DLL_API static void rollbackTranslationOverlay(
    CoinRenderFramePlan & plan, CoinRenderTranslationOverlayUndo & undo);
  COIN_RENDER_DLL_API static bool beginObjectOverlay(
    CoinRenderFramePlan &, const std::vector<CoinRenderModelUpdate> &,
    const std::vector<CoinRenderMaterialUpdate> &, const std::vector<CoinRenderPositionUpdate> &,
    const std::vector<CoinRenderDrawSourceUpdate> &, uint64_t, CoinRenderObjectOverlayUndo &);
  COIN_RENDER_DLL_API static void rollbackObjectOverlay(CoinRenderFramePlan &, CoinRenderObjectOverlayUndo &);
};

#endif // !COIN_RENDER_FRAME_REUSE_CORE_H
