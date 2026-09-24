#ifndef COIN_SOWGPUFRAMEREUSECORE_H
#define COIN_SOWGPUFRAMEREUSECORE_H

#include <Inventor/CoinWgpuExport.h>

#include "rendering/wgpu/SoWgpuFramePlan.h"

#include <cstdint>

/** Mechanical relationship between two private FramePlan revisions. */
enum class SoWgpuFrameReuseKind {
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
struct SoWgpuFrameReuseDecision {
  SoWgpuFrameReuseKind kind = SoWgpuFrameReuseKind::UNKNOWN;
  uint64_t baseRevision = 0;

  SoWgpuFrameReuseDecision() = default;
  SoWgpuFrameReuseDecision(SoWgpuFrameReuseKind k, uint64_t base)
    : kind(k), baseRevision(base) {}
};

/** Traversal-free classification of the relationship between two plans. */
class SoWgpuFrameReuseCore {
public:
  COIN_WGPU_DLL_API static SoWgpuFrameReuseDecision classify(
    const FramePlan & previous,
    const FramePlan & current);
};

#endif // !COIN_SOWGPUFRAMEREUSECORE_H
