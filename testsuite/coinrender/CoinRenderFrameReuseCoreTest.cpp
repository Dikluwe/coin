#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinrender/CoinRenderTargetP.h"

#include <Inventor/SoDB.h>

#include <iostream>

namespace {
bool
check(bool condition, const char * message)
{
  if (!condition) std::cerr << "CoinRenderFrameReuseCoreTest: " << message << '\n';
  return condition;
}

CoinRenderFramePlan
makePlan(uint64_t revision)
{
  CoinRenderFramePlan plan;
  plan.revision = revision;
  plan.vertices.resize(3);
  plan.vertices[0].position[0] = -0.5f;
  plan.vertices[1].position[0] = 0.5f;
  plan.vertices[2].position[1] = 0.5f;
  plan.indices = {0, 1, 2};
  plan.materials.push_back(CoinRenderMaterialSnapshot{});
  plan.lightingStates.push_back(CoinRenderLightingSnapshot{});
  plan.cameras.push_back(CoinRenderCameraSnapshot{});
  plan.viewports.push_back(CoinRenderViewportSnapshot{});
  CoinRenderRenderStateSnapshot state;
  state.lightModel = CoinRenderLightModel::BASE_COLOR;
  plan.renderStates.push_back(state);
  CoinRenderDrawPacket draw;
  draw.topology = CoinRenderPrimitiveTopology::TRIANGLE_LIST;
  draw.geometry.vertexCount = 3;
  draw.geometry.indexCount = 3;
  draw.stableNodeId = 7;
  draw.sourceRevision = 11;
  plan.draws.push_back(draw);
  return plan;
}
}

int
main()
{
  SoDB::init();
  bool ok = true;
  const CoinRenderFramePlan previous = makePlan(41);
  std::string diagnostic;
  ok &= check(previous.isValid(&diagnostic), "test fixture must be valid");

  CoinRenderFramePlan current = previous;
  current.revision = 42;
  CoinRenderFrameReuseDecision decision =
    CoinRenderFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == CoinRenderFrameReuseKind::REUSE &&
              decision.baseRevision == 41,
              "equal payload must reuse its prior revision");

  current.renderStates[0].transparentTexture = true;
  decision = CoinRenderFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD,
              "image alpha evidence must invalidate frame reuse even without texture sampling");
  current = previous;
  current.revision = 42;

  SbMatrix moved = SbMatrix::identity();
  moved.setTranslate(SbVec3f(0.25f, 0.0f, -1.0f));
  current.cameras[0].viewMatrix = moved;
  current.renderStates[0].view = moved;
  decision = CoinRenderFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == CoinRenderFrameReuseKind::CAMERA_PATCH,
              "camera-only dependency footprint must patch");
  CoinRenderFramePlan overlay;
  ok &= check(CoinRenderFrameReuseCore::cameraOverlay(
                previous, current.cameras[0], 46, overlay) &&
              overlay.revision == 46 &&
              overlay.vertices.size() == previous.vertices.size() &&
              overlay.vertices[0].position[0] == previous.vertices[0].position[0] &&
              overlay.indices == previous.indices &&
              overlay.renderStates[0].view == moved,
              "camera overlay must preserve geometry and replace camera state");
  CoinRenderFramePlan inPlace = previous;
  const CoinRenderVertexSnapshot * originalVertices = inPlace.vertices.data();
  const uint32_t * originalIndices = inPlace.indices.data();
  CoinRenderCameraOverlayUndo undo;
  ok &= check(CoinRenderFrameReuseCore::beginCameraOverlay(
                inPlace, current.cameras[0], 47, undo) &&
              undo.active && undo.revision == previous.revision &&
              inPlace.revision == 47 &&
              inPlace.vertices.data() == originalVertices &&
              inPlace.indices.data() == originalIndices &&
              inPlace.renderStates[0].view == moved,
              "transactional overlay must not copy geometry");
  CoinRenderFrameReuseCore::rollbackCameraOverlay(inPlace, undo);
  ok &= check(!undo.active && inPlace.revision == previous.revision &&
              inPlace.hasSamePayload(previous) &&
              inPlace.vertices.data() == originalVertices,
              "rollback must restore the exact previous plan");
  CoinRenderCameraSnapshot invalidCamera = current.cameras[0];
  invalidCamera.farDistance = invalidCamera.nearDistance;
  ok &= check(!CoinRenderFrameReuseCore::beginCameraOverlay(
                inPlace, invalidCamera, 48, undo) && !undo.active &&
              inPlace.revision == previous.revision,
              "invalid camera must fail before mutating the cached plan");
  CoinRenderFramePlan unsuitable = previous;
  unsuitable.renderStates[0].lightModel = CoinRenderLightModel::PHONG;
  ok &= check(!CoinRenderFrameReuseCore::cameraOverlay(
                unsuitable, current.cameras[0], 47, overlay) &&
              overlay.revision == 46,
              "view-dependent lighting must reject mechanical overlay");
  unsuitable = previous;
  unsuitable.renderStates[0].fogMode = CoinRenderFogMode::FOG;
  ok &= check(!CoinRenderFrameReuseCore::cameraOverlay(
                unsuitable, current.cameras[0], 48, overlay),
              "fog must reject mechanical overlay");

  current = previous;
  current.revision = 43;
  current.vertices[0].position[0] = 2.0f;
  current.draws[0].sourceRevision = 12;
  decision = CoinRenderFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD,
              "stable draw structure with changed payload must rebuild resources");

  current = previous;
  current.revision = 44;
  current.vertices.push_back(CoinRenderVertexSnapshot{});
  decision = CoinRenderFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == CoinRenderFrameReuseKind::FULL_REBUILD,
              "changed execution structure must rebuild the full plan");

  current = previous;
  current.revision = 0;
  decision = CoinRenderFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == CoinRenderFrameReuseKind::UNKNOWN,
              "unversioned plans must fail closed as unknown");

  current = previous;
  current.revision = 45;
  CoinRenderTextureImageSnapshot opaque;
  opaque.width = 1;
  opaque.height = 1;
  opaque.gpuToken = 77;
  current.textures.push_back(opaque);
  decision = CoinRenderFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == CoinRenderFrameReuseKind::UNKNOWN,
              "opaque connector resources must not infer reusable ownership");

  CoinRenderFramePlan targetFrame = previous;
  targetFrame.viewports[0].width = 64;
  targetFrame.viewports[0].height = 64;
  CoinRenderTargetP target(SbVec2i32(64, 64));
#if defined(HAVE_COIN_BGFX)
  target.depthReadbackEnabled = false;
#endif
  CoinRenderFrameExecutionResult executed = target.executeFrame(targetFrame);
  ok &= check(executed.status == CoinRenderBackendStatus::SUCCESS &&
              target.lastValidatedPlanRevision == 41,
              "target must validate the initial frame");
  targetFrame.revision = 42;
  targetFrame.cameras[0].viewMatrix = moved;
  targetFrame.renderStates[0].view = moved;
  executed = target.executeFrame(targetFrame,
    CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH, 41));
  ok &= check(executed.status == CoinRenderBackendStatus::SUCCESS &&
              target.lastValidatedPlanRevision == 42,
              "target must advance the validated camera-patch revision");
  targetFrame.revision = 43;
  targetFrame.indices[0] = 99;
  executed = target.executeFrame(targetFrame,
    CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH, 41));
  ok &= check(executed.status != CoinRenderBackendStatus::SUCCESS &&
              target.lastValidatedPlanRevision == 42,
              "stale base must revalidate and reject invalid geometry");
  targetFrame.indices[0] = 0;
  ok &= check(target.resize(SbVec2i32(128, 128)) &&
              target.lastValidatedPlanRevision == 0,
              "resize must invalidate the target validation revision");
  targetFrame.revision = 44;
  executed = target.executeFrame(targetFrame,
    CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH, 42));
  ok &= check(executed.status == CoinRenderBackendStatus::SUCCESS &&
              target.lastValidatedPlanRevision == 44,
              "a valid subviewport must survive target resize and camera reuse");

  auto optionsChanged = targetFrame;
  optionsChanged.revision = targetFrame.revision + 1;
  optionsChanged.transparency.layers = 8;
  ok &= check(!targetFrame.hasSamePayload(optionsChanged) &&
                  CoinRenderFrameReuseCore::classify(targetFrame, optionsChanged).kind ==
                      CoinRenderFrameReuseKind::FULL_REBUILD,
              "peel count changes must not reuse a captured camera/geometry plan");
  optionsChanged = targetFrame;
  optionsChanged.revision = targetFrame.revision + 1;
  optionsChanged.transparency.bufferBudget /= 2;
  ok &= check(!targetFrame.hasSamePayload(optionsChanged) &&
                  CoinRenderFrameReuseCore::classify(targetFrame, optionsChanged).kind ==
                      CoinRenderFrameReuseKind::FULL_REBUILD,
              "budget changes must not reuse the old plan");
  if (!ok) return 1;
  std::cout << "CoinRenderFrameReuseCoreTest passed\n";
  return 0;
}
