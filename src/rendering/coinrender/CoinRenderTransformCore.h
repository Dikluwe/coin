#ifndef COIN_RENDER_TRANSFORM_CORE_H
#define COIN_RENDER_TRANSFORM_CORE_H

#include <Inventor/CoinRenderExport.h>
#include "rendering/coinrender/CoinRenderFramePlan.h"

// Coin-native transforms over captured values. GPU packing and API conventions
// select parameters; this Core does not know BGFX, wgpu, actions or devices.
class CoinRenderTransformCore {
public:
  COIN_RENDER_DLL_API static bool finiteMatrix(const SbMatrix & matrix);
  // Coin projection is [-1,1]. A zero-to-one depth API needs conversion once.
  COIN_RENDER_DLL_API static SbMatrix projection(const SbMatrix & coin, bool homogeneousDepth);
  COIN_RENDER_DLL_API static SbMatrix normalMatrix(const SbMatrix & modelView);
  // A camera view may rotate/translate, but scale, shear and reflections need
  // the ordinary capture/packing path. This is a qualification tolerance, not
  // an approximate comparison of scene changes.
  COIN_RENDER_DLL_API static bool rigidViewMatrix(const SbMatrix & matrix);
  // Reusing float eye-space values is unsafe at very large coordinates: an
  // anchor at 1e8 can erase a unit-sized feature before a delta brings it near
  // the eye. Keep this optional path within a conservative precision domain.
  COIN_RENDER_DLL_API static bool cameraReuseView(const SbMatrix & matrix);
  // Geometry baked in anchorView stays immutable. Derive every camera update
  // from that anchor, rather than accumulating deltas between successive views.
  // Coin uses row vectors: p * anchorView * delta == p * currentView.
  COIN_RENDER_DLL_API static bool cameraDelta(const SbMatrix & anchorView, const SbMatrix & currentView,
                          SbMatrix & delta, SbMatrix & normalDelta);
  // Already validated positive target dimensions. Preserve viewport projection;
  // clipping changes only scissor bounds, never this transform.
  COIN_RENDER_DLL_API static SbMatrix viewportTransform(const CoinRenderViewportSnapshot & viewport,
                                     int width, int height);
  // APIs requiring an in-target viewport use the intersection as their native
  // viewport and compensate projection in Core. Window coordinates and depth
  // stay those of the original Coin viewport; an empty intersection is a no-op.
  COIN_RENDER_DLL_API static SbMatrix clippedViewportTransform(const CoinRenderViewportSnapshot & viewport,
                                            int width, int height, int32_t clipped[4]);
  // Coin bottom-left coordinates. Empty intersection leaves the output intact.
  COIN_RENDER_DLL_API static bool clipViewport(const int32_t viewport[4], int width, int height,
                            int32_t clipped[4]);
};
#endif
