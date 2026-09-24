#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuFrameReuseCore.h"
#include "rendering/wgpu/SoWgpuRenderTargetP.h"

#include <Inventor/SoDB.h>

#include <iostream>

namespace {
bool
check(bool condition, const char * message)
{
  if (!condition) std::cerr << "WgpuFrameReuseCoreTest: " << message << '\n';
  return condition;
}

FramePlan
makePlan(uint64_t revision)
{
  FramePlan plan;
  plan.revision = revision;
  plan.vertices.push_back(VertexSnapshot{});
  plan.indices.push_back(0);
  plan.materials.push_back(MaterialSnapshot{});
  plan.lightingStates.push_back(LightingSnapshot{});
  plan.cameras.push_back(CameraSnapshot{});
  plan.viewports.push_back(ViewportSnapshot{});
  RenderStateSnapshot state;
  state.lightModel = LightModel::BASE_COLOR;
  plan.renderStates.push_back(state);
  DrawPacket draw;
  draw.topology = PrimitiveTopology::POINT_LIST;
  draw.geometry.vertexCount = 1;
  draw.geometry.indexCount = 1;
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
  const FramePlan previous = makePlan(41);
  std::string diagnostic;
  ok &= check(previous.isValid(&diagnostic), "test fixture must be valid");

  FramePlan current = previous;
  current.revision = 42;
  SoWgpuFrameReuseDecision decision =
    SoWgpuFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == SoWgpuFrameReuseKind::REUSE &&
              decision.baseRevision == 41,
              "equal payload must reuse its prior revision");

  SbMatrix moved = SbMatrix::identity();
  moved.setTranslate(SbVec3f(0.25f, 0.0f, -1.0f));
  current.cameras[0].viewMatrix = moved;
  current.renderStates[0].view = moved;
  decision = SoWgpuFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == SoWgpuFrameReuseKind::CAMERA_PATCH,
              "camera-only dependency footprint must patch");
  FramePlan overlay;
  ok &= check(SoWgpuFrameReuseCore::cameraOverlay(
                previous, current.cameras[0], 46, overlay) &&
              overlay.revision == 46 &&
              overlay.vertices.size() == previous.vertices.size() &&
              overlay.vertices[0].position[0] == previous.vertices[0].position[0] &&
              overlay.indices == previous.indices &&
              overlay.renderStates[0].view == moved,
              "camera overlay must preserve geometry and replace camera state");
  FramePlan inPlace = previous;
  const VertexSnapshot * originalVertices = inPlace.vertices.data();
  const uint32_t * originalIndices = inPlace.indices.data();
  SoWgpuCameraOverlayUndo undo;
  ok &= check(SoWgpuFrameReuseCore::beginCameraOverlay(
                inPlace, current.cameras[0], 47, undo) &&
              undo.active && undo.revision == previous.revision &&
              inPlace.revision == 47 &&
              inPlace.vertices.data() == originalVertices &&
              inPlace.indices.data() == originalIndices &&
              inPlace.renderStates[0].view == moved,
              "transactional overlay must not copy geometry");
  SoWgpuFrameReuseCore::rollbackCameraOverlay(inPlace, undo);
  ok &= check(!undo.active && inPlace.revision == previous.revision &&
              inPlace.hasSamePayload(previous) &&
              inPlace.vertices.data() == originalVertices,
              "rollback must restore the exact previous plan");
  CameraSnapshot invalidCamera = current.cameras[0];
  invalidCamera.farDistance = invalidCamera.nearDistance;
  ok &= check(!SoWgpuFrameReuseCore::beginCameraOverlay(
                inPlace, invalidCamera, 48, undo) && !undo.active &&
              inPlace.revision == previous.revision,
              "invalid camera must fail before mutating the cached plan");
  FramePlan unsuitable = previous;
  unsuitable.renderStates[0].lightModel = LightModel::PHONG;
  ok &= check(!SoWgpuFrameReuseCore::cameraOverlay(
                unsuitable, current.cameras[0], 47, overlay) &&
              overlay.revision == 46,
              "view-dependent lighting must reject mechanical overlay");
  unsuitable = previous;
  unsuitable.renderStates[0].fogMode = FogMode::FOG;
  ok &= check(!SoWgpuFrameReuseCore::cameraOverlay(
                unsuitable, current.cameras[0], 48, overlay),
              "fog must reject mechanical overlay");

  current = previous;
  current.revision = 43;
  current.vertices[0].position[0] = 2.0f;
  current.draws[0].sourceRevision = 12;
  decision = SoWgpuFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == SoWgpuFrameReuseKind::RESOURCE_REBUILD,
              "stable draw structure with changed payload must rebuild resources");

  current = previous;
  current.revision = 44;
  current.vertices.push_back(VertexSnapshot{});
  decision = SoWgpuFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == SoWgpuFrameReuseKind::FULL_REBUILD,
              "changed execution structure must rebuild the full plan");

  current = previous;
  current.revision = 0;
  decision = SoWgpuFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == SoWgpuFrameReuseKind::UNKNOWN,
              "unversioned plans must fail closed as unknown");

  current = previous;
  current.revision = 45;
  TextureImageSnapshot opaque;
  opaque.width = 1;
  opaque.height = 1;
  opaque.gpuToken = 77;
  current.textures.push_back(opaque);
  decision = SoWgpuFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == SoWgpuFrameReuseKind::UNKNOWN,
              "opaque connector resources must not infer reusable ownership");

  FramePlan targetFrame = previous;
  targetFrame.viewports[0].width = 64;
  targetFrame.viewports[0].height = 64;
  SoWgpuRenderTargetP target(SbVec2i32(64, 64));
  FrameExecutionResult executed = target.executeFrame(targetFrame);
  ok &= check(executed.status == BackendStatus::SUCCESS &&
              target.lastValidatedPlanRevision == 41,
              "target must validate the initial frame");
  targetFrame.revision = 42;
  targetFrame.cameras[0].viewMatrix = moved;
  targetFrame.renderStates[0].view = moved;
  executed = target.executeFrame(targetFrame,
    SoWgpuFrameReuseDecision(SoWgpuFrameReuseKind::CAMERA_PATCH, 41));
  ok &= check(executed.status == BackendStatus::SUCCESS &&
              target.lastValidatedPlanRevision == 42,
              "target must advance the validated camera-patch revision");
  targetFrame.revision = 43;
  targetFrame.indices[0] = 99;
  executed = target.executeFrame(targetFrame,
    SoWgpuFrameReuseDecision(SoWgpuFrameReuseKind::CAMERA_PATCH, 41));
  ok &= check(executed.status != BackendStatus::SUCCESS &&
              target.lastValidatedPlanRevision == 42,
              "stale base must revalidate and reject invalid geometry");
  targetFrame.indices[0] = 0;
  ok &= check(target.resize(SbVec2i32(128, 128)) &&
              target.lastValidatedPlanRevision == 0,
              "resize must invalidate the target validation revision");
  targetFrame.revision = 44;
  executed = target.executeFrame(targetFrame,
    SoWgpuFrameReuseDecision(SoWgpuFrameReuseKind::CAMERA_PATCH, 42));
  ok &= check(executed.status != BackendStatus::SUCCESS,
              "resize must recheck the viewport before camera reuse");

  if (!ok) return 1;
  std::cout << "WgpuFrameReuseCoreTest passed\n";
  return 0;
}
