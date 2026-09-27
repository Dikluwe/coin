#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuRenderTargetP.h"

#include <Inventor/SoDB.h>

#include <cstdlib>
#include <iostream>

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
  // The evaluation deliberately rejects concurrent target ownership. It must
  // leave the current owner usable and allow a serial handoff in either
  // direction once the prepared backend is released on the API thread.
  SoWgpuRenderTargetP contender(SbVec2i32(48, 48));
  contender.depthReadbackEnabled = false;
  const FrameExecutionResult concurrent = contender.executeFrame(frame);
  if (concurrent.status != BackendStatus::UNSUPPORTED || contender.backend ||
      concurrent.diagnostic.find("one active render target per process") ==
        std::string::npos) {
    std::cerr << "BGFX concurrent-target limitation is not explicit\n";
    return 1;
  }
  if (target.executeFrame(frame).status != BackendStatus::SUCCESS ||
      !hasBlueTexturePixel(target.colorBuffer)) {
    std::cerr << "BGFX rejected contender disturbed the active target\n";
    return 1;
  }
  target.backend.reset();
  if (contender.executeFrame(frame).status != BackendStatus::SUCCESS ||
      !hasBlueTexturePixel(contender.colorBuffer)) {
    std::cerr << "BGFX did not permit serial target handoff\n";
    return 1;
  }
  contender.backend.reset();
  if (target.executeFrame(frame).status != BackendStatus::SUCCESS ||
      !hasBlueTexturePixel(target.colorBuffer)) {
    std::cerr << "BGFX did not permit target ownership reacquisition\n";
    return 1;
  }
  std::cout << "WgpuBgfxOffscreenTest passed\n";


  return 0;
}
