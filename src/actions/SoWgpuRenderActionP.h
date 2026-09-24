#include <Inventor/CoinWgpuExport.h>
#ifndef SOWGPURENDERACTIONP_H
#define SOWGPURENDERACTIONP_H

#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include "rendering/wgpu/SoWgpuFramePlanBuilder.h"
#include "rendering/wgpu/SoWgpuRecordingBackend.h"
#include <string>
#include <deque>
#include <vector>
#include <memory>
#include <cstdint>
#include <cstddef>

class COIN_WGPU_DLL_API SoWgpuRenderActionP {
public:
  SoWgpuRenderActionP(SoWgpuRenderAction * master = nullptr);
  ~SoWgpuRenderActionP();

  void initCallbacks();

  template <typename F>
  void executeApply(F traversalFn);

  static void triangleCB(void * userdata,
                         SoCallbackAction * action,
                         const SoPrimitiveVertex * v0,
                         const SoPrimitiveVertex * v1,
                         const SoPrimitiveVertex * v2);

  static void lineCB(void * userdata,
                     SoCallbackAction * action,
                     const SoPrimitiveVertex * v0,
                     const SoPrimitiveVertex * v1);

  static void pointCB(void * userdata,
                      SoCallbackAction * action,
                      const SoPrimitiveVertex * vertex);

  static SoCallbackAction::Response lightPreCB(void * userdata,
                                               SoCallbackAction * action,
                                               const SoNode * node);

  static SoCallbackAction::Response sceneTexturePreCB(void * userdata,
                                                      SoCallbackAction * action,
                                                      const SoNode * node);

  static SoCallbackAction::Response indexedFaceSetPreCB(void * userdata,
                                                       SoCallbackAction * action,
                                                       const SoNode * node);

  static SoCallbackAction::Response indexedLineSetPreCB(void * userdata,
                                                       SoCallbackAction * action,
                                                       const SoNode * node);

  SoWgpuRenderAction * master;
  SoWgpuRenderTarget * target;
  SoWgpuReadbackTicket * asyncTicket; // Only valid during applyAsync().
  SbViewportRegion viewport;
  SbColor4f backgroundColor;
  SoWgpuRenderAction::Status lastStatus;
  SbString lastError;
  SbString lastRecordingLog;

  SoWgpuFramePlanBuilder builder;
  SoWgpuRecordingBackend recordingBackend;
  FramePlan lastValidPlan;
  // Owns staged scene-texture pixels for the entire parent traversal.
  std::deque<std::vector<uint8_t> > sceneTexturePixels;
  // Shared by child actions during one top-level apply.
  std::shared_ptr<size_t> sceneTextureStagedBytes;

  bool hasLastValidPlan;
  bool isApplying;
  bool hasReentrancyError;
  bool fastPathEnabled;
};

#endif // !SOWGPURENDERACTIONP_H
