#include "../coinrender/CoinRenderTestEnvironment.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinbgfx/CoinBgfxLowering.h"

#include <Inventor/SoDB.h>
#include <Inventor/actions/SoGLRenderAction.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

int main()
{
  SoDB::init();
  CoinRenderTargetP target(SbVec2i32(32, 32));
  target.depthReadbackEnabled = false;
  if (!target.depthBuffer.empty()) {
    std::cerr << "BGFX allocated an unpublished CPU depth buffer\n";
    return 1;
  }
  CoinRenderFramePlan frame;
  frame.clearColor = SbColor4f(0.0f, 0.0f, 0.0f, 1.0f);
  CoinRenderMaterialSnapshot material;
  material.diffuse[0] = 1.0f;
  material.diffuse[1] = 0.0f;
  material.diffuse[2] = 0.0f;
  frame.materials.push_back(material);
  frame.lightingStates.push_back(CoinRenderLightingSnapshot());
  frame.cameras.push_back(CoinRenderCameraSnapshot());
  CoinRenderViewportSnapshot viewport;
  viewport.width = 32;
  viewport.height = 32;
  frame.viewports.push_back(viewport);
  CoinRenderRenderStateSnapshot state;
  state.lightModel = CoinRenderLightModel::BASE_COLOR;
  state.cullMode = CoinRenderCullMode::BACK;
  state.frontFace = CoinRenderFrontFace::CCW;
  frame.renderStates.push_back(state);
  frame.vertices.resize(3);
  frame.vertices[0].position[0] = -0.7f;
  frame.vertices[0].position[1] = -0.7f;
  frame.vertices[1].position[0] = 0.7f;
  frame.vertices[1].position[1] = -0.7f;
  frame.vertices[2].position[1] = 0.7f;
  frame.indices = {0, 1, 2};
  CoinRenderDrawPacket draw;
  draw.geometry.vertexCount = 3;
  draw.geometry.indexCount = 3;
  frame.draws.push_back(draw);

  const CoinRenderFrameExecutionResult result = target.executeFrame(frame);
  if (result.status == CoinRenderBackendStatus::NOT_READY) {
    std::cout << "BGFX renderer unavailable: " << result.diagnostic << '\n';
    return 77;
  }
  if (result.status != CoinRenderBackendStatus::SUCCESS) {
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
  // Switch programs on an already prepared target. A plan captured as solid
  // must produce the same publication when the full surface stage is forced,
  // and returning to the solid stage must not retain the previous selection.
  const std::vector<uint8_t> firstSolidPixels = target.colorBuffer;
  const char * previousSolidOptout = std::getenv("COIN_BGFX_DISABLE_SOLID_PROGRAM");
  const bool hadSolidOptout = previousSolidOptout != nullptr;
  const std::string savedSolidOptout = previousSolidOptout ? previousSolidOptout : "";
  const char * previousInstanceOptout = std::getenv("COIN_BGFX_DISABLE_INSTANCING");
  const bool hadInstanceOptout = previousInstanceOptout != nullptr;
  const std::string savedInstanceOptout = previousInstanceOptout ? previousInstanceOptout : "";
  coinRenderTestSetEnvironment("COIN_BGFX_DISABLE_INSTANCING", "1");
  coinRenderTestSetEnvironment("COIN_BGFX_DISABLE_SOLID_PROGRAM", "1");
  const bool fullMatched = target.executeFrame(frame).status == CoinRenderBackendStatus::SUCCESS &&
    target.colorBuffer == firstSolidPixels;
  coinRenderTestSetEnvironment("COIN_BGFX_DISABLE_SOLID_PROGRAM", "0");
  const bool solidMatched = target.executeFrame(frame).status == CoinRenderBackendStatus::SUCCESS &&
    target.colorBuffer == firstSolidPixels;
  if (hadSolidOptout)
    coinRenderTestSetEnvironment("COIN_BGFX_DISABLE_SOLID_PROGRAM", savedSolidOptout.c_str());
  else coinRenderTestSetEnvironment("COIN_BGFX_DISABLE_SOLID_PROGRAM", nullptr);
  if (hadInstanceOptout)
    coinRenderTestSetEnvironment("COIN_BGFX_DISABLE_INSTANCING", savedInstanceOptout.c_str());
  else coinRenderTestSetEnvironment("COIN_BGFX_DISABLE_INSTANCING", nullptr);
  const bool restoredMatched = target.executeFrame(frame).status == CoinRenderBackendStatus::SUCCESS &&
    target.colorBuffer == firstSolidPixels;
  if (!fullMatched || !solidMatched || !restoredMatched) {
    std::cerr << "BGFX solid/full program switch changed the published image\n";
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
    if (clippedResult.status != CoinRenderBackendStatus::SUCCESS ||
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
  // renormalized center normal has N.L close to 0.9. Coin PHONG describes
  // reflectance; compatibility requires Gouraud colors (~180 in UNORM8).
  frame.renderStates[0].lightModel = CoinRenderLightModel::PHONG;
  frame.materials[0].diffuse[0] = frame.materials[0].diffuse[1] =
    frame.materials[0].diffuse[2] = 1.0f;
  frame.materials[0].ambient[0] = frame.materials[0].ambient[1] =
    frame.materials[0].ambient[2] = 0.0f;
  frame.lightingStates[0].ambientIntensity = 0.0f;
  CoinRenderLightSourceSnapshot light;
  light.direction[2] = -1.0f;
  frame.lightingStates[0].lights.push_back(light);
  const float diagonal = 0.70710678f;
  frame.vertices[0].normal[0] = -diagonal;
  frame.vertices[0].normal[2] = diagonal;
  frame.vertices[1].normal[0] = diagonal;
  frame.vertices[1].normal[2] = diagonal;
  frame.vertices[2].normal[1] = diagonal;
  frame.vertices[2].normal[2] = diagonal;
  if (target.executeFrame(frame).status != CoinRenderBackendStatus::SUCCESS ||
      std::abs(int(target.colorBuffer[center]) - 180) > 2 ||
      std::abs(int(target.colorBuffer[center + 1]) - 180) > 2 ||
      std::abs(int(target.colorBuffer[center + 2]) - 180) > 2) {
    std::cerr << "BGFX PHONG did not interpolate vertex lighting like Coin/GL\n";
    return 1;
  }
  frame.renderStates[0].lightModel = CoinRenderLightModel::BASE_COLOR;
  frame.materials[0].diffuse[0] = 1.0f;
  frame.materials[0].diffuse[1] = frame.materials[0].diffuse[2] = 0.0f;
  frame.lightingStates[0].lights.clear();
  // An asymmetric second frame detects an inverted readback origin.
  frame.vertices[0].position[1] = 0.1f;
  frame.vertices[1].position[1] = 0.1f;
  frame.vertices[2].position[1] = 0.9f;
  const CoinRenderFrameExecutionResult second = target.executeFrame(frame);
  const size_t upper = (8u * 32u + 16u) * 4u;
  const size_t lower = (24u * 32u + 16u) * 4u;
  if (second.status != CoinRenderBackendStatus::SUCCESS ||
      target.colorBuffer[upper] < 200 || target.colorBuffer[lower] > 30) {
    std::cerr << "BGFX readback origin differs from top-left RGBA contract\n";
    return 1;
  }
  // A nonzero revision enables the private geometry cache; a new revision
  // must invalidate both packed colors and GPU buffers.
  frame.revision = 73;
  if (target.executeFrame(frame).status != CoinRenderBackendStatus::SUCCESS ||
      target.executeFrame(frame).status != CoinRenderBackendStatus::SUCCESS ||
      target.colorBuffer[upper] < 200) {
    std::cerr << "BGFX cached frame changed its pixels\n";
    return 1;
  }
  const CoinRenderFramePlan redCachedFrame = frame;
  frame.revision = 74;
  frame.materials[0].diffuse[0] = 0.0f;
  frame.materials[0].diffuse[1] = 1.0f;
  const CoinRenderFrameReuseDecision materialReuse =
    CoinRenderFrameReuseCore::classify(redCachedFrame, frame);
  if (materialReuse.kind != CoinRenderFrameReuseKind::RESOURCE_REBUILD ||
      target.executeFrame(frame, materialReuse).status != CoinRenderBackendStatus::SUCCESS ||
      target.colorBuffer[upper] > 30 || target.colorBuffer[upper + 1] < 200) {
    std::cerr << "BGFX failed the partial material update\n";
    return 1;
  }
  const CoinRenderFramePlan greenFrame = frame;
  frame.revision = 75;
  SbMatrix shifted = SbMatrix::identity();
  shifted.setTranslate(SbVec3f(1.5f, 0.0f, 0.0f));
  frame.cameras[0].viewMatrix = shifted;
  frame.renderStates[0].view = shifted;
  const CoinRenderFrameReuseDecision shiftReuse =
    CoinRenderFrameReuseCore::classify(greenFrame, frame);
  if (shiftReuse.kind != CoinRenderFrameReuseKind::CAMERA_PATCH ||
      target.executeFrame(frame, shiftReuse).status != CoinRenderBackendStatus::SUCCESS ||
      target.colorBuffer[upper + 1] > 30) {
    std::cerr << "BGFX camera patch failed to move the rendered triangle\n";
    return 1;
  }
  const CoinRenderFramePlan shiftedFrame = frame;
  frame.revision = 76;
  frame.cameras[0].viewMatrix = SbMatrix::identity();
  frame.renderStates[0].view = SbMatrix::identity();
  const CoinRenderFrameReuseDecision restoreReuse =
    CoinRenderFrameReuseCore::classify(shiftedFrame, frame);
  if (restoreReuse.kind != CoinRenderFrameReuseKind::CAMERA_PATCH ||
      target.executeFrame(frame, restoreReuse).status != CoinRenderBackendStatus::SUCCESS ||
      target.colorBuffer[upper + 1] < 200) {
    std::cerr << "BGFX camera patch failed to restore the rendered triangle\n";
    return 1;
  }
  const CoinRenderFramePlan baseForComparison = frame;
  frame.revision = 77;
  SbMatrix subtleShift = SbMatrix::identity();
  subtleShift.setTranslate(SbVec3f(0.25f, 0.0f, 0.0f));
  frame.cameras[0].viewMatrix = subtleShift;
  frame.renderStates[0].view = subtleShift;
  const CoinRenderFrameReuseDecision comparisonReuse =
    CoinRenderFrameReuseCore::classify(baseForComparison, frame);
  if (comparisonReuse.kind != CoinRenderFrameReuseKind::CAMERA_PATCH ||
      target.executeFrame(frame, comparisonReuse).status != CoinRenderBackendStatus::SUCCESS) {
    std::cerr << "BGFX comparison camera patch failed\n";
    return 1;
  }
  const std::vector<uint8_t> patchedPixels = target.colorBuffer;
  frame.revision = 78;
  if (target.executeFrame(frame).status != CoinRenderBackendStatus::SUCCESS ||
      patchedPixels != target.colorBuffer) {
    std::cerr << "BGFX camera patch differs from complete frame lowering\n";
    return 1;
  }
  // Cross the old 32 MiB lowering cache cutoff without a large framebuffer.
  // Repeated indices preserve the reference image while exercising a real
  // GPU-only cache and its material invalidation after CPU geometry is freed.
  CoinRenderFramePlan largeFrame = frame;
  const size_t triangles = 32u * 1024u * 1024u / (3u * sizeof(CoinBgfxVertex)) + 1;
  largeFrame.indices.clear();
  largeFrame.indices.reserve(triangles * 3);
  for (size_t i = 0; i < triangles; ++i)
    largeFrame.indices.insert(largeFrame.indices.end(), frame.indices.begin(), frame.indices.end());
  largeFrame.draws[0].geometry.indexCount = static_cast<uint32_t>(largeFrame.indices.size());
  largeFrame.revision = 9001;
  if (target.executeFrame(largeFrame).status != CoinRenderBackendStatus::SUCCESS ||
      target.colorBuffer != patchedPixels ||
      target.executeFrame(largeFrame).status != CoinRenderBackendStatus::SUCCESS ||
      target.colorBuffer != patchedPixels) {
    std::cerr << "BGFX large GPU-only cache changed the reference image\n";
    return 1;
  }
  const CoinRenderFramePlan largeBase = largeFrame;
  largeFrame.revision = 9002;
  largeFrame.materials[0].diffuse[1] = 0.0f;
  largeFrame.materials[0].diffuse[2] = 1.0f;
  const auto largeMaterialReuse = CoinRenderFrameReuseCore::classify(largeBase, largeFrame);
  if (target.executeFrame(largeFrame, largeMaterialReuse).status != CoinRenderBackendStatus::SUCCESS) {
    std::cerr << "BGFX large GPU-only cache could not rebuild a material change\n";
    return 1;
  }
  const auto largeBlue = target.colorBuffer;
  CoinRenderFramePlan smallBlue = frame;
  smallBlue.revision = 9003;
  smallBlue.materials = largeFrame.materials;
  if (target.executeFrame(smallBlue).status != CoinRenderBackendStatus::SUCCESS ||
      target.colorBuffer != largeBlue || largeBlue == patchedPixels) {
    std::cerr << "BGFX large GPU-only cache retained stale material data\n";
    return 1;
  }
  // Fail after a large upload is queued but before frame submission. Runtime
  // teardown must release the transferred allocation, and recreation must
  // upload independent data rather than reuse a dangling reference.
  largeFrame.revision = 9004;
  const uint32_t largeUploadGeneration = target.generation;
  coinRenderTestSetEnvironment("COIN_BGFX_TEST_DEVICE_LOST_ON_SUBMIT_ONCE", "1");
  if (target.executeFrame(largeFrame).status != CoinRenderBackendStatus::DEVICE_LOST ||
      target.generation != largeUploadGeneration + 1 || target.backend) {
    std::cerr << "BGFX queued large upload did not tear down after submission loss\n";
    return 1;
  }
  if (target.executeFrame(largeFrame).status != CoinRenderBackendStatus::SUCCESS ||
      target.colorBuffer != largeBlue) {
    std::cerr << "BGFX large upload failed to recover its owned memory\n";
    return 1;
  }
  {
    CoinRenderTargetP compactTarget(SbVec2i32(32, 32));
    compactTarget.depthReadbackEnabled = false;
    CoinRenderFramePlan opaque = smallBlue;
    opaque.revision = 9100;
    opaque.renderStates[0].lightModel = CoinRenderLightModel::PHONG;
    opaque.renderStates[0].transparencyType = SoGLRenderAction::BLEND;
    opaque.materials[0].emission[2] = 1.0f;
    if (compactTarget.executeFrame(opaque).status != CoinRenderBackendStatus::SUCCESS) return 1;
    const auto opaqueReference = compactTarget.colorBuffer;
    if (opaqueReference[upper + 2] < 200) {
      std::cerr << "BGFX compact reference did not contain visible emissive geometry\n";
      return 1;
    }
    opaque.draws.resize(65000, opaque.draws[0]);
    opaque.revision++;
    if (compactTarget.executeFrame(opaque).status != CoinRenderBackendStatus::SUCCESS ||
        compactTarget.colorBuffer != opaqueReference ||
        compactTarget.executeFrame(opaque).status != CoinRenderBackendStatus::SUCCESS ||
        compactTarget.colorBuffer != opaqueReference) {
      std::cerr << "BGFX compact large frame or its GPU reuse changed the reference image\n";
      return 1;
    }
    opaque.draws[1].renderLayer = 1;
    opaque.draws[1].clearDepthBefore = true;
    opaque.revision++;
    if (compactTarget.executeFrame(opaque).status != CoinRenderBackendStatus::SUCCESS ||
        compactTarget.colorBuffer != opaqueReference) {
      std::cerr << "BGFX compact-to-full layout transition changed the image\n";
      return 1;
    }
    opaque.draws[1].renderLayer = 0;
    opaque.draws[1].clearDepthBefore = false;
    opaque.revision++;
    if (compactTarget.executeFrame(opaque).status != CoinRenderBackendStatus::SUCCESS ||
        compactTarget.colorBuffer != opaqueReference) {
      std::cerr << "BGFX full-to-compact layout transition changed the image\n";
      return 1;
    }
  }
  CoinRenderTextureImageSnapshot recoveryTexture;
  recoveryTexture.width = 1;
  recoveryTexture.height = 1;
  recoveryTexture.contentDigest = 1;
  recoveryTexture.pixelsRgba = {0, 0, 255, 255};
  frame.textures.push_back(recoveryTexture);
  frame.samplers.push_back(CoinRenderSamplerSnapshot());
  frame.renderStates[0].hasTexture = true;
  frame.renderStates[0].textureImageSlot = 0;
  frame.renderStates[0].samplerSlot = 0;
  frame.renderStates[0].textureModel = CoinRenderTextureModel::REPLACE;
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
  coinRenderTestSetEnvironment("COIN_BGFX_TEST_DEVICE_LOST_ON_RESIZE_ONCE", "1");
  const uint32_t resizeGeneration = target.generation;
  const CoinRenderFrameExecutionResult lostOnResize = target.executeFrame(frame);
  if (lostOnResize.status != CoinRenderBackendStatus::DEVICE_LOST ||
      target.generation != resizeGeneration + 1 || target.backend) {
    std::cerr << "BGFX did not release its runtime after loss during resize\n";
    return 1;
  }
  if (target.executeFrame(frame).status != CoinRenderBackendStatus::SUCCESS ||
      !target.backend || target.colorBuffer.size() != 48u * 48u * 4u ||
      !hasBlueTexturePixel(target.colorBuffer)) {
    std::cerr << "BGFX did not recreate resources after resize loss\n";
    return 1;
  }

  // A loss after encoding exercises the same teardown from the submission
  // path. A partial prepare failure immediately afterwards proves that neither
  // failure leaves the global singleton reservation stuck.
  coinRenderTestSetEnvironment("COIN_BGFX_TEST_DEVICE_LOST_ON_SUBMIT_ONCE", "1");
  const uint32_t submitGeneration = target.generation;
  const CoinRenderFrameExecutionResult lostOnSubmit = target.executeFrame(frame);
  if (lostOnSubmit.status != CoinRenderBackendStatus::DEVICE_LOST ||
      target.generation != submitGeneration + 1 || target.backend) {
    std::cerr << "BGFX did not release its runtime after submission loss\n";
    return 1;
  }
  coinRenderTestSetEnvironment("COIN_BGFX_TEST_FAIL_PREPARE_ONCE", "1");
  if (target.executeFrame(frame).status != CoinRenderBackendStatus::BACKEND_ERROR ||
      target.backend) {
    std::cerr << "BGFX partial prepare failure did not tear down the runtime\n";
    return 1;
  }
  if (target.executeFrame(frame).status != CoinRenderBackendStatus::SUCCESS ||
      !target.backend || target.colorBuffer.size() != 48u * 48u * 4u ||
      !hasBlueTexturePixel(target.colorBuffer)) {
    std::cerr << "BGFX singleton remained occupied after a backend error\n";
    return 1;
  }
  // Targets share a runtime; releasing the initializer keeps survivors alive.
  CoinRenderTargetP contender(SbVec2i32(48, 48));
  contender.depthReadbackEnabled = false;
  const CoinRenderFrameExecutionResult concurrent = contender.executeFrame(frame);
  if (concurrent.status != CoinRenderBackendStatus::SUCCESS || !contender.backend ||
      !hasBlueTexturePixel(contender.colorBuffer)) {
    std::cerr << "BGFX concurrent target failed\n";
    return 1;
  }
  if (target.executeFrame(frame).status != CoinRenderBackendStatus::SUCCESS ||
      !hasBlueTexturePixel(target.colorBuffer)) {
    std::cerr << "BGFX contender disturbed the active target\n";
    return 1;
  }
  target.backend.reset();
  if (contender.executeFrame(frame).status != CoinRenderBackendStatus::SUCCESS ||
      !hasBlueTexturePixel(contender.colorBuffer)) {
    std::cerr << "BGFX survivor lost runtime after initializer destruction\n";
    return 1;
  }
  if (target.executeFrame(frame).status != CoinRenderBackendStatus::SUCCESS ||
      !hasBlueTexturePixel(target.colorBuffer)) {
    std::cerr << "BGFX target recreation failed while contender remained active\n";
    return 1;
  }
  // BGFX requires one API thread. Independent worker ownership must reject
  // explicitly and must not damage the targets prepared on this thread.
  CoinRenderFrameExecutionResult wrongThread;
  std::thread worker([&]() {
    CoinRenderTargetP workerTarget(SbVec2i32(48, 48));
    workerTarget.depthReadbackEnabled = false;
    wrongThread = workerTarget.executeFrame(frame);
  });
  worker.join();
  if (wrongThread.status != CoinRenderBackendStatus::UNSUPPORTED ||
      wrongThread.diagnostic.find("API thread") == std::string::npos ||
      target.executeFrame(frame).status != CoinRenderBackendStatus::SUCCESS ||
      !hasBlueTexturePixel(target.colorBuffer)) {
    std::cerr << "BGFX cross-thread rejection corrupted shared runtime\n";
    return 1;
  }

  // Exercise the runtime owned by CoinRender. A statically linked BGFX in a
  // Windows test executable has separate globals from the copy in the DLL.
  // Discover the budget through preparation, up to the 16-bit view-ID bound.
  std::vector<std::unique_ptr<CoinRenderTargetP>> extraTargets;
  std::unique_ptr<CoinRenderTargetP> overflow;
  for (unsigned int i = 2; i < 4096; ++i) {
    std::unique_ptr<CoinRenderTargetP> extra(new CoinRenderTargetP(SbVec2i32(48, 48)));
    extra->depthReadbackEnabled = false;
    const CoinRenderFrameExecutionResult prepared = extra->executeFrame(frame);
    if (prepared.status == CoinRenderBackendStatus::UNSUPPORTED && !extra->backend &&
        prepared.diagnostic.find("view budget") != std::string::npos) {
      overflow = std::move(extra);
      break;
    }
    if (prepared.status != CoinRenderBackendStatus::SUCCESS ||
        !hasBlueTexturePixel(extra->colorBuffer)) {
      std::cerr << "BGFX failed before reaching target view budget\n";
      return 1;
    }
    extraTargets.push_back(std::move(extra));
  }
  if (!overflow || extraTargets.empty()) {
    std::cerr << "BGFX exhausted view budget was not diagnosed\n";
    return 1;
  }
  extraTargets.pop_back();
  if (overflow->executeFrame(frame).status != CoinRenderBackendStatus::SUCCESS ||
      !hasBlueTexturePixel(overflow->colorBuffer) ||
      contender.executeFrame(frame).status != CoinRenderBackendStatus::SUCCESS) {
    std::cerr << "BGFX failed to reuse released target view slot\n";
    return 1;
  }
  overflow->backend.reset();
  extraTargets.clear();

  // Device loss belongs to the shared runtime. Every survivor must observe
  // it before using stale GPU handles; after the last owner drops, retry works.
  coinRenderTestSetEnvironment("COIN_BGFX_TEST_DEVICE_LOST_ON_SUBMIT_ONCE", "1");
  if (target.executeFrame(frame).status != CoinRenderBackendStatus::DEVICE_LOST || target.backend) {
    std::cerr << "BGFX shared runtime did not report injected device loss\n";
    return 1;
  }
  // Shared retirement marks every peer lost and discards its handles at once.
  // The next executeFrame may therefore prepare a fresh shared runtime.
  if (contender.status != CoinRenderTarget::TARGET_LOST || contender.backend) {
    std::cerr << "BGFX survivor did not discard lost runtime resources\n";
    return 1;
  }
  if (target.executeFrame(frame).status != CoinRenderBackendStatus::SUCCESS ||
      !hasBlueTexturePixel(target.colorBuffer) ||
      contender.executeFrame(frame).status != CoinRenderBackendStatus::SUCCESS ||
      !hasBlueTexturePixel(contender.colorBuffer)) {
    std::cerr << "BGFX shared runtime did not recover after all lost targets dropped\n";
    return 1;
  }
  contender.backend.reset();
  std::cout << "CoinBgfxOffscreenTest passed\n";


  return 0;
}
