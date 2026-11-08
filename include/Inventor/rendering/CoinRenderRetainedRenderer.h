#ifndef COIN_RENDER_RETAINED_RENDERER_H
#define COIN_RENDER_RETAINED_RENDERER_H

#include <Inventor/CoinRenderExport.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/SbColor4f.h>

class CoinRenderTarget;
class SoDrawList;
class SoRenderPlan;

/** Experimental FreeCAD retained capture -> selected CoinRender executor.
 * Retains capture and the upstream plan; the fork's updater patches supported
 * notifications and otherwise rebuilds. Supports synchronous window/offscreen
 * RGBA8, monoscopic cameras, triangles and shared Core stroke expansion, BASE_COLOR/PHONG,
 * directional/point/spot lights, fog, clipping, polygon offset, 2D textures,
 * unpacked material arrays and geometry in VertexProperty. The native planner
 * supplies captured, opaque-first or back-to-front object order; the target selects its existing
 * transparency, sampling, stored color and multisampling mechanisms. Other
 * scene semantics are rejected in the same traversal. See the integration
 * coverage matrix for remaining gaps and incremental update boundaries.
 * Call on one thread; render must not be reentered.
 * Requires rebuilding Coin and consumers (the native IR adds virtual methods).
 */
class COIN_RENDER_DLL_API CoinRenderRetainedRenderer {
public:
  explicit CoinRenderRetainedRenderer(const SbViewportRegion & viewport);
  ~CoinRenderRetainedRenderer();
  CoinRenderRetainedRenderer(const CoinRenderRetainedRenderer &) = delete;
  CoinRenderRetainedRenderer & operator=(const CoinRenderRetainedRenderer &) = delete;

  void setViewportRegion(const SbViewportRegion & viewport);
  void setClearColor(const SbColor4f & color);
  // Native plan order covers NONE, screen-door, immediate/deferred BLEND/ADD,
  // sorted-object BLEND/ADD and sorted layers. Sorted-triangle modes are refused.
  void setTransparencyType(CoinRenderAction::TransparencyType type);
  CoinRenderAction::Status render(SoNode * root, CoinRenderTarget & target);
  const SbString & getLastError() const;
  // Borrowed until the next render or destruction; pixels are owned by target.
  const SoDrawList & getDrawList() const;
  const SoRenderPlan & getRenderPlan() const;
  uint64_t getCaptureCount() const;
  uint64_t getPlanBuildCount() const;
  struct Statistics {
    uint64_t captures = 0, planBuilds = 0, retainedFrames = 0;
    uint64_t incrementalUpdates = 0, patchedCommands = 0;
    uint64_t transportBuilds = 0, transportReuses = 0, transportPatchedCommands = 0;
    uint64_t copiedVertices = 0, copiedIndices = 0;
    uint64_t transportGeometryPatches = 0, geometryPatchedVertices = 0;
    uint64_t transportVertices = 0, transportCameras = 0;
    uint64_t nativeCommands = 0, nativeGeometryResources = 0;
    uint64_t transportScheduleBytes = 0;
    uint64_t nativeCommandStorageBytes = 0;
    uint64_t nativePathStorageBytes = 0, nativeDependencyRanges = 0, nativeCompactionPasses = 0;
    uint64_t materialSlots = 0; // Current transport gauge, not a cumulative counter.
  };
  Statistics getStatistics() const;
  // Explicit producer publication/invalidation, also used by equivalence controls.
  // Advances presentation authority; never schedules extra work
  // in the ordinary retained path.
  void invalidate();

private:
  struct P;
  P * p;
};

#endif
