#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuRenderTargetP.h"

#include <Inventor/SoDB.h>

#include <bgfx/bgfx.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

int main()
{
  SoDB::init();
  SoWgpuRenderTargetP target(SbVec2i32(32, 32));
  target.depthReadbackEnabled = false;
  if (!target.depthBuffer.empty()) {
    std::cerr << "BGFX allocated an unpublished CPU depth buffer\n";
    return 1;
  }
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
  if (!target.depthBuffer.empty()) {
    std::cerr << "BGFX populated an unpublished CPU depth buffer\n";
    return 1;
  }
  // A shifted viewport must crop rather than reposition the triangle.
  const int origins[][2] = {{-16, 0}, {16, 0}, {0, -16}, {0, 16}, {64, 0}};
  const int samples[][2] = {{4, 16}, {28, 16}, {16, 4}, {16, 28}, {16, 16}};
  frame.draws[0].renderLayer = 1;
  frame.draws[0].clearDepthBefore = true;
  for (size_t i = 0; i < 5; ++i) {
    frame.viewports[0].x = origins[i][0];
    frame.viewports[0].y = origins[i][1];
    const auto clippedResult = target.executeFrame(frame);
    const size_t sample = (size_t(31 - samples[i][1]) * 32 + samples[i][0]) * 4;
    if (clippedResult.status != BackendStatus::SUCCESS ||
        (i < 4 && target.colorBuffer[sample] < 200) ||
        (i == 4 && target.colorBuffer[center] > 30) ||
        (i < 4 && target.colorBuffer[center] > 30)) {
      std::cerr << "BGFX external viewport clipping failed at case " << i << ": "
                << clippedResult.diagnostic << '\n';
      return 1;
    }
  }
  frame.viewports[0].x = frame.viewports[0].y = 0;
  frame.draws[0].renderLayer = 0;
  frame.draws[0].clearDepthBefore = false;
  // All three vertices have N.L = sqrt(1/2), while the interpolated and
  // renormalized center normal has N.L close to 0.9. This distinguishes true
  // fragment PHONG from the previous Gouraud result (~180 in UNORM8).
  frame.renderStates[0].lightModel = LightModel::PHONG;
  frame.materials[0].diffuse[0] = frame.materials[0].diffuse[1] =
    frame.materials[0].diffuse[2] = 1.0f;
  frame.materials[0].ambient[0] = frame.materials[0].ambient[1] =
    frame.materials[0].ambient[2] = 0.0f;
  frame.lightingStates[0].ambientIntensity = 0.0f;
  LightSourceSnapshot light;
  light.direction[2] = -1.0f;
  frame.lightingStates[0].lights.push_back(light);
  const float diagonal = 0.70710678f;
  frame.vertices[0].normal[0] = -diagonal;
  frame.vertices[0].normal[2] = diagonal;
  frame.vertices[1].normal[0] = diagonal;
  frame.vertices[1].normal[2] = diagonal;
  frame.vertices[2].normal[1] = diagonal;
  frame.vertices[2].normal[2] = diagonal;
  if (target.executeFrame(frame).status != BackendStatus::SUCCESS ||
      target.colorBuffer[center] < 215 ||
      target.colorBuffer[center + 1] < 215 ||
      target.colorBuffer[center + 2] < 215) {
    std::cerr << "BGFX PHONG was not evaluated with an interpolated fragment normal\n";
    return 1;
  }
  frame.renderStates[0].lightModel = LightModel::BASE_COLOR;
  frame.materials[0].diffuse[0] = 1.0f;
  frame.materials[0].diffuse[1] = frame.materials[0].diffuse[2] = 0.0f;
  frame.lightingStates[0].lights.clear();
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
  const FramePlan redCachedFrame = frame;
  frame.revision = 74;
  frame.materials[0].diffuse[0] = 0.0f;
  frame.materials[0].diffuse[1] = 1.0f;
  const SoWgpuFrameReuseDecision materialReuse =
    SoWgpuFrameReuseCore::classify(redCachedFrame, frame);
  if (materialReuse.kind != SoWgpuFrameReuseKind::RESOURCE_REBUILD ||
      target.executeFrame(frame, materialReuse).status != BackendStatus::SUCCESS ||
      target.colorBuffer[upper] > 30 || target.colorBuffer[upper + 1] < 200) {
    std::cerr << "BGFX failed the partial material update\n";
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
  TextureImageSnapshot recoveryTexture;
  recoveryTexture.width = 1;
  recoveryTexture.height = 1;
  recoveryTexture.contentDigest = 1;
  recoveryTexture.pixelsRgba = {0, 0, 255, 255};
  frame.textures.push_back(recoveryTexture);
  frame.samplers.push_back(SamplerSnapshot());
  frame.renderStates[0].hasTexture = true;
  frame.renderStates[0].textureImageSlot = 0;
  frame.renderStates[0].samplerSlot = 0;
  frame.renderStates[0].textureModel = TextureModel::REPLACE;
  const auto hasBlueTexturePixel = [](const std::vector<uint8_t> & color) {
    for (size_t i = 0; i + 3 < color.size(); i += 4) {
      if (color[i] < 50 && color[i + 1] < 50 && color[i + 2] > 180)
        return true;
    }
    return false;
  };

  // A loss reported while rebuilding framebuffers must discard the entire
  // runtime. The next frame recreates programs, uniforms, buffers, textures,
  // and framebuffer attachments and reacquires the process-wide BGFX slot.
  if (!target.resize(SbVec2i32(48, 48))) {
    std::cerr << "BGFX target resize setup failed\n";
    return 1;
  }
  frame.viewports[0].width = 48;
  frame.viewports[0].height = 48;
  frame.revision = 79;
  setenv("COIN_BGFX_TEST_DEVICE_LOST_ON_RESIZE_ONCE", "1", 1);
  const uint32_t resizeGeneration = target.generation;
  const FrameExecutionResult lostOnResize = target.executeFrame(frame);
  if (lostOnResize.status != BackendStatus::DEVICE_LOST ||
      target.generation != resizeGeneration + 1 || target.backend) {
    std::cerr << "BGFX did not release its runtime after loss during resize\n";
    return 1;
  }
  if (target.executeFrame(frame).status != BackendStatus::SUCCESS ||
      !target.backend || target.colorBuffer.size() != 48u * 48u * 4u ||
      !hasBlueTexturePixel(target.colorBuffer)) {
    std::cerr << "BGFX did not recreate resources after resize loss\n";
    return 1;
  }

  // A loss after encoding exercises the same teardown from the submission
  // path. A partial prepare failure immediately afterwards proves that neither
  // failure leaves the global singleton reservation stuck.
  setenv("COIN_BGFX_TEST_DEVICE_LOST_ON_SUBMIT_ONCE", "1", 1);
  const uint32_t submitGeneration = target.generation;
  const FrameExecutionResult lostOnSubmit = target.executeFrame(frame);
  if (lostOnSubmit.status != BackendStatus::DEVICE_LOST ||
      target.generation != submitGeneration + 1 || target.backend) {
    std::cerr << "BGFX did not release its runtime after submission loss\n";
    return 1;
  }
  setenv("COIN_BGFX_TEST_FAIL_PREPARE_ONCE", "1", 1);
  if (target.executeFrame(frame).status != BackendStatus::BACKEND_ERROR ||
      target.backend) {
    std::cerr << "BGFX partial prepare failure did not tear down the runtime\n";
    return 1;
  }
  if (target.executeFrame(frame).status != BackendStatus::SUCCESS ||
      !target.backend || target.colorBuffer.size() != 48u * 48u * 4u ||
      !hasBlueTexturePixel(target.colorBuffer)) {
    std::cerr << "BGFX singleton remained occupied after a backend error\n";
    return 1;
  }
  // Targets share a runtime; releasing the initializer keeps survivors alive.
  SoWgpuRenderTargetP contender(SbVec2i32(48, 48));
  contender.depthReadbackEnabled = false;
  const FrameExecutionResult concurrent = contender.executeFrame(frame);
  if (concurrent.status != BackendStatus::SUCCESS || !contender.backend ||
      !hasBlueTexturePixel(contender.colorBuffer)) {
    std::cerr << "BGFX concurrent target failed\n";
    return 1;
  }
  if (target.executeFrame(frame).status != BackendStatus::SUCCESS ||
      !hasBlueTexturePixel(target.colorBuffer)) {
    std::cerr << "BGFX contender disturbed the active target\n";
    return 1;
  }
  target.backend.reset();
  if (contender.executeFrame(frame).status != BackendStatus::SUCCESS ||
      !hasBlueTexturePixel(contender.colorBuffer)) {
    std::cerr << "BGFX survivor lost runtime after initializer destruction\n";
    return 1;
  }
  if (target.executeFrame(frame).status != BackendStatus::SUCCESS ||
      !hasBlueTexturePixel(target.colorBuffer)) {
    std::cerr << "BGFX target recreation failed while contender remained active\n";
    return 1;
  }
  // BGFX requires one API thread. Independent worker ownership must reject
  // explicitly and must not damage the targets prepared on this thread.
  FrameExecutionResult wrongThread;
  std::thread worker([&]() {
    SoWgpuRenderTargetP workerTarget(SbVec2i32(48, 48));
    workerTarget.depthReadbackEnabled = false;
    wrongThread = workerTarget.executeFrame(frame);
  });
  worker.join();
  if (wrongThread.status != BackendStatus::UNSUPPORTED ||
      wrongThread.diagnostic.find("API thread") == std::string::npos ||
      target.executeFrame(frame).status != BackendStatus::SUCCESS ||
      !hasBlueTexturePixel(target.colorBuffer)) {
    std::cerr << "BGFX cross-thread rejection corrupted shared runtime\n";
    return 1;
  }

  // Follow the linked BGFX build's view budget, including larger demo builds.
  const unsigned int targetCapacity = bgfx::getCaps()->limits.maxViews / 16;
  std::vector<std::unique_ptr<SoWgpuRenderTargetP>> extraTargets;
  for (unsigned int i = 2; i < targetCapacity; ++i) {
    std::unique_ptr<SoWgpuRenderTargetP> extra(new SoWgpuRenderTargetP(SbVec2i32(48, 48)));
    extra->depthReadbackEnabled = false;
    if (extra->executeFrame(frame).status != BackendStatus::SUCCESS ||
        !hasBlueTexturePixel(extra->colorBuffer)) {
      std::cerr << "BGFX failed before reaching target view budget\n";
      return 1;
    }
    extraTargets.push_back(std::move(extra));
  }
  SoWgpuRenderTargetP overflow(SbVec2i32(48, 48));
  overflow.depthReadbackEnabled = false;
  const FrameExecutionResult exhausted = overflow.executeFrame(frame);
  if (exhausted.status != BackendStatus::UNSUPPORTED || overflow.backend ||
      exhausted.diagnostic.find("view budget") == std::string::npos) {
    std::cerr << "BGFX exhausted view budget was not diagnosed\n";
    return 1;
  }
  extraTargets.pop_back();
  if (overflow.executeFrame(frame).status != BackendStatus::SUCCESS ||
      !hasBlueTexturePixel(overflow.colorBuffer) ||
      contender.executeFrame(frame).status != BackendStatus::SUCCESS) {
    std::cerr << "BGFX failed to reuse released target view slot\n";
    return 1;
  }
  overflow.backend.reset();
  extraTargets.clear();

  // Device loss belongs to the shared runtime. Every survivor must observe
  // it before using stale GPU handles; after the last owner drops, retry works.
  setenv("COIN_BGFX_TEST_DEVICE_LOST_ON_SUBMIT_ONCE", "1", 1);
  if (target.executeFrame(frame).status != BackendStatus::DEVICE_LOST || target.backend) {
    std::cerr << "BGFX shared runtime did not report injected device loss\n";
    return 1;
  }
  const FrameExecutionResult survivorLoss = contender.executeFrame(frame);
  if (survivorLoss.status != BackendStatus::DEVICE_LOST || contender.backend) {
    std::cerr << "BGFX survivor did not discard lost runtime resources\n";
    return 1;
  }
  if (target.executeFrame(frame).status != BackendStatus::SUCCESS ||
      !hasBlueTexturePixel(target.colorBuffer) ||
      contender.executeFrame(frame).status != BackendStatus::SUCCESS ||
      !hasBlueTexturePixel(contender.colorBuffer)) {
    std::cerr << "BGFX shared runtime did not recover after all lost targets dropped\n";
    return 1;
  }
  contender.backend.reset();
  std::cout << "WgpuBgfxOffscreenTest passed\n";


  return 0;
}
