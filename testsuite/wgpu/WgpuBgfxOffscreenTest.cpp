#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuRenderTargetP.h"

#include <Inventor/SoDB.h>

#include <iostream>

int main()
{
  SoDB::init();
  SoWgpuRenderTargetP target(SbVec2i32(32, 32));
  target.depthReadbackEnabled = false;
  FramePlan frame;
  frame.clearColor = SbColor4f(0.0f, 0.0f, 0.0f, 1.0f);
  MaterialSnapshot material;
  material.diffuse[0] = 1.0f;
  material.diffuse[1] = 0.0f;
  material.diffuse[2] = 0.0f;
  frame.materials.push_back(material);
  frame.lightingStates.push_back(LightingSnapshot());
  frame.cameras.push_back(CameraSnapshot());
  ViewportSnapshot viewport;
  viewport.width = 32;
  viewport.height = 32;
  frame.viewports.push_back(viewport);
  RenderStateSnapshot state;
  state.lightModel = LightModel::BASE_COLOR;
  state.cullMode = CullMode::BACK;
  state.frontFace = FrontFace::CCW;
  frame.renderStates.push_back(state);
  frame.vertices.resize(3);
  frame.vertices[0].position[0] = -0.7f;
  frame.vertices[0].position[1] = -0.7f;
  frame.vertices[1].position[0] = 0.7f;
  frame.vertices[1].position[1] = -0.7f;
  frame.vertices[2].position[1] = 0.7f;
  frame.indices = {0, 1, 2};
  DrawPacket draw;
  draw.geometry.vertexCount = 3;
  draw.geometry.indexCount = 3;
  frame.draws.push_back(draw);

  const FrameExecutionResult result = target.executeFrame(frame);
  if (result.status == BackendStatus::NOT_READY) {
    std::cout << "BGFX renderer unavailable: " << result.diagnostic << '\n';
    return 77;
  }
  if (result.status != BackendStatus::SUCCESS) {
    std::cerr << "BGFX frame failed: " << result.diagnostic << '\n';
    return 1;
  }
  const size_t center = (16u * 32u + 16u) * 4u;
  if (target.colorBuffer.size() != 32u * 32u * 4u ||
      target.colorBuffer[center] < 200 ||
      target.colorBuffer[center + 1] > 30 ||
      target.colorBuffer[center + 2] > 30) {
    std::cerr << "BGFX center pixel did not contain the red triangle\n";
    return 1;
  }
  // An asymmetric second frame detects an inverted readback origin.
  frame.vertices[0].position[1] = 0.1f;
  frame.vertices[1].position[1] = 0.1f;
  frame.vertices[2].position[1] = 0.9f;
  const FrameExecutionResult second = target.executeFrame(frame);
  const size_t upper = (8u * 32u + 16u) * 4u;
  const size_t lower = (24u * 32u + 16u) * 4u;
  if (second.status != BackendStatus::SUCCESS ||
      target.colorBuffer[upper] < 200 || target.colorBuffer[lower] > 30) {
    std::cerr << "BGFX readback origin differs from top-left RGBA contract\n";
    return 1;
  }
  // A nonzero revision enables the private geometry cache; a new revision
  // must invalidate both packed colors and GPU buffers.
  frame.revision = 73;
  if (target.executeFrame(frame).status != BackendStatus::SUCCESS ||
      target.executeFrame(frame).status != BackendStatus::SUCCESS ||
      target.colorBuffer[upper] < 200) {
    std::cerr << "BGFX cached frame changed its pixels\n";
    return 1;
  }
  frame.revision = 74;
  frame.materials[0].diffuse[0] = 0.0f;
  frame.materials[0].diffuse[1] = 1.0f;
  if (target.executeFrame(frame).status != BackendStatus::SUCCESS ||
      target.colorBuffer[upper] > 30 || target.colorBuffer[upper + 1] < 200) {
    std::cerr << "BGFX failed to invalidate changed material revision\n";
    return 1;
  }
  const FramePlan greenFrame = frame;
  frame.revision = 75;
  SbMatrix shifted = SbMatrix::identity();
  shifted.setTranslate(SbVec3f(1.5f, 0.0f, 0.0f));
  frame.cameras[0].viewMatrix = shifted;
  frame.renderStates[0].view = shifted;
  const SoWgpuFrameReuseDecision shiftReuse =
    SoWgpuFrameReuseCore::classify(greenFrame, frame);
  if (shiftReuse.kind != SoWgpuFrameReuseKind::CAMERA_PATCH ||
      target.executeFrame(frame, shiftReuse).status != BackendStatus::SUCCESS ||
      target.colorBuffer[upper + 1] > 30) {
    std::cerr << "BGFX camera patch failed to move the rendered triangle\n";
    return 1;
  }
  const FramePlan shiftedFrame = frame;
  frame.revision = 76;
  frame.cameras[0].viewMatrix = SbMatrix::identity();
  frame.renderStates[0].view = SbMatrix::identity();
  const SoWgpuFrameReuseDecision restoreReuse =
    SoWgpuFrameReuseCore::classify(shiftedFrame, frame);
  if (restoreReuse.kind != SoWgpuFrameReuseKind::CAMERA_PATCH ||
      target.executeFrame(frame, restoreReuse).status != BackendStatus::SUCCESS ||
      target.colorBuffer[upper + 1] < 200) {
    std::cerr << "BGFX camera patch failed to restore the rendered triangle\n";
    return 1;
  }
  const FramePlan baseForComparison = frame;
  frame.revision = 77;
  SbMatrix subtleShift = SbMatrix::identity();
  subtleShift.setTranslate(SbVec3f(0.25f, 0.0f, 0.0f));
  frame.cameras[0].viewMatrix = subtleShift;
  frame.renderStates[0].view = subtleShift;
  const SoWgpuFrameReuseDecision comparisonReuse =
    SoWgpuFrameReuseCore::classify(baseForComparison, frame);
  if (comparisonReuse.kind != SoWgpuFrameReuseKind::CAMERA_PATCH ||
      target.executeFrame(frame, comparisonReuse).status != BackendStatus::SUCCESS) {
    std::cerr << "BGFX comparison camera patch failed\n";
    return 1;
  }
  const std::vector<uint8_t> patchedPixels = target.colorBuffer;
  frame.revision = 78;
  if (target.executeFrame(frame).status != BackendStatus::SUCCESS ||
      patchedPixels != target.colorBuffer) {
    std::cerr << "BGFX camera patch differs from complete frame lowering\n";
    return 1;
  }
  std::cout << "WgpuBgfxOffscreenTest passed\n";
  return 0;
}
