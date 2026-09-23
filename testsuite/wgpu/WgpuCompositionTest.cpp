#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include "rendering/wgpu/SoWgpuComposition.h"
#include "rendering/wgpu/SoWgpuCpuReferenceBackend.h"
#include "rendering/wgpu/SoWgpuRenderTargetP.h"

#include <array>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const char * message) {
  if (!condition) std::cerr << "WgpuCompositionTest: " << message << "\n";
  return condition;
}

struct Sample {
  std::array<int, 4> rgba = {{0, 0, 0, 0}};
  float depth = 1.0f;
};

SbMatrix projection() {
  // z=-2 -> depth=.25; z=-4 -> .75; z=-5 -> 1.0.
  return SbMatrix(1, 0, 0, 0,
                  0, 1, 0, 0,
                  0, 0, -0.5f, 0,
                  0, 0, -1.5f, 1);
}

void addQuad(FramePlan & plan, float z, const std::array<float, 3> & color,
             float alpha, bool textured = false) {
  const uint32_t materialSlot = static_cast<uint32_t>(plan.materials.size());
  MaterialSnapshot material;
  for (int c = 0; c < 3; ++c) material.diffuse[c] = color[c];
  material.diffuse[3] = alpha;
  material.transparency = 1.0f - alpha;
  plan.materials.push_back(material);

  RenderStateSnapshot state;
  state.materialSlot = materialSlot;
  state.lightModel = LightModel::BASE_COLOR;
  state.cullMode = CullMode::NONE;
  state.projectionCoin = projection();
  state.transparencyType = SoGLRenderAction::SORTED_OBJECT_BLEND;
  state.hasTexture = textured;
  if (textured) {
    state.textureImageSlot = 0;
    state.samplerSlot = 0;
  }
  const uint32_t stateSlot = static_cast<uint32_t>(plan.renderStates.size());
  plan.renderStates.push_back(state);

  const uint32_t firstVertex = static_cast<uint32_t>(plan.vertices.size());
  const float xy[4][2] = {{-0.8f, -0.8f}, {0.8f, -0.8f},
                          {0.8f, 0.8f}, {-0.8f, 0.8f}};
  for (int i = 0; i < 4; ++i) {
    VertexSnapshot vertex;
    vertex.position[0] = xy[i][0];
    vertex.position[1] = xy[i][1];
    vertex.position[2] = z;
    vertex.texcoord[0] = (xy[i][0] + 0.8f) / 1.6f;
    vertex.texcoord[1] = (xy[i][1] + 0.8f) / 1.6f;
    vertex.materialSlot = materialSlot;
    plan.vertices.push_back(vertex);
  }
  const uint32_t firstIndex = static_cast<uint32_t>(plan.indices.size());
  const uint32_t offsets[6] = {0, 1, 2, 0, 2, 3};
  for (int i = 0; i < 6; ++i) plan.indices.push_back(firstVertex + offsets[i]);
  DrawPacket draw;
  draw.geometry.firstVertex = firstVertex;
  draw.geometry.vertexCount = 4;
  draw.geometry.firstIndex = firstIndex;
  draw.geometry.indexCount = 6;
  draw.renderStateSlot = stateSlot;
  plan.draws.push_back(draw);
}

FramePlan makePlan(bool textureAlpha = false) {
  FramePlan plan;
  plan.clearColor = SbColor4f(0, 0, 0, 1);
  plan.lightingStates.push_back(LightingSnapshot{});
  plan.cameras.push_back(CameraSnapshot{});
  ViewportSnapshot viewport;
  viewport.width = 64;
  viewport.height = 64;
  plan.viewports.push_back(viewport);
  // Deliberately reverse input order. The scheduler must submit blue opaque,
  // then green far, then red near.
  addQuad(plan, -2.0f, {{1, 0, 0}}, textureAlpha ? 1.0f : 0.5f, textureAlpha);
  addQuad(plan, -5.0f, {{0, 0, 1}}, 1.0f);
  if (!textureAlpha) addQuad(plan, -4.0f, {{0, 1, 0}}, 0.5f);
  if (textureAlpha) {
    TextureImageSnapshot texture;
    texture.width = texture.height = 2;
    texture.components = 4;
    texture.pixelsRgba.resize(16, 255);
    for (int i = 0; i < 4; ++i) texture.pixelsRgba[i * 4 + 3] = 128;
    texture.contentDigest = 0x4231;
    plan.textures.push_back(texture);
    plan.samplers.push_back(SamplerSnapshot{});
  }
  return plan;
}

bool render(const FramePlan & plan, bool cpu, Sample & sample) {
  SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
  if (cpu) target->getPimpl()->backend.reset(new SoWgpuCpuReferenceBackend);
  const FrameExecutionResult result = target->getPimpl()->executeFrame(plan);
  if (result.status != BackendStatus::SUCCESS) {
    std::cerr << "Render failed: " << result.diagnostic << "\n";
    delete target;
    return false;
  }
  std::vector<uint8_t> rgba;
  std::vector<float> depth;
  target->readbackRGBA(rgba);
  target->readbackDepth(depth);
  delete target;
  if (!check(rgba.size() == 64u * 64u * 4u && depth.size() == 64u * 64u,
             "readback size")) return false;
  const size_t pixel = 32u * 64u + 40u; // Avoid the shared triangle diagonal.
  for (int c = 0; c < 4; ++c) sample.rgba[c] = rgba[pixel * 4u + c];
  sample.depth = depth[pixel];
  return true;
}

bool nearColor(const Sample & sample, const std::array<int, 4> & expected, int tolerance) {
  for (int c = 0; c < 4; ++c) {
    if (std::abs(sample.rgba[c] - expected[c]) > tolerance) {
      std::cerr << "channel " << c << " expected " << expected[c]
                << " got " << sample.rgba[c] << "\n";
      return false;
    }
  }
  return true;
}

bool testMaterialBlend() {
  FramePlan plan = makePlan();
  std::string diagnostic;
  std::vector<SoWgpuCompositionItem> order;
  if (!check(plan.isValid(&diagnostic) &&
             coin_wgpu_composition_order(plan, order, diagnostic), "valid blend plan")) return false;
  if (!check(order.size() == 3 && order[0].drawIndex == 1 &&
             order[1].drawIndex == 2 && order[2].drawIndex == 0,
             "opaque/far/near ordering")) return false;
  Sample cpu;
  if (!render(plan, true, cpu) || !check(nearColor(cpu, {{127, 63, 63, 255}}, 3), "CPU source-over")) return false;
  if (!check(std::abs(cpu.depth - 1.0f) < 0.01f, "transparent draws must not write depth")) return false;
  if (SoWgpuRenderAction::isGpuBackendAvailable()) {
    Sample gpu;
    if (!render(plan, false, gpu) || !check(nearColor(gpu, {{127, 63, 63, 255}}, 5), "GPU source-over")) return false;
    if (!check(std::abs(gpu.depth - cpu.depth) < 0.01f, "CPU/GPU depth parity")) return false;
  }
  return true;
}

bool testStableDepthTie() {
  FramePlan plan = makePlan();
  for (size_t i = 8; i < 12; ++i) plan.vertices[i].position[2] = -2.0f;
  std::vector<SoWgpuCompositionItem> order;
  std::string diagnostic;
  if (!check(coin_wgpu_composition_order(plan, order, diagnostic) &&
             order.size() == 3 && order[0].drawIndex == 1 &&
             order[1].drawIndex == 0 && order[2].drawIndex == 2,
             "equal-depth transparent draws preserve traversal order")) return false;
  Sample cpu;
  if (!render(plan, true, cpu) ||
      !check(nearColor(cpu, {{63, 127, 63, 255}}, 4), "CPU stable-depth blend")) return false;
  if (SoWgpuRenderAction::isGpuBackendAvailable()) {
    Sample gpu;
    if (!render(plan, false, gpu) ||
        !check(nearColor(gpu, {{63, 127, 63, 255}}, 6), "GPU stable-depth blend")) return false;
  }
  return true;
}

bool testTextureAlpha() {
  FramePlan plan = makePlan(true);
  Sample cpu;
  if (!render(plan, true, cpu) || !check(nearColor(cpu, {{128, 0, 127, 255}}, 4), "CPU texture alpha")) return false;
  if (SoWgpuRenderAction::isGpuBackendAvailable()) {
    Sample gpu;
    if (!render(plan, false, gpu) || !check(nearColor(gpu, {{128, 0, 127, 255}}, 6), "GPU texture alpha")) return false;
  }
  return true;
}

bool testTextureMutationAndSharing() {
  const bool gpuAvailable = SoWgpuRenderAction::isGpuBackendAvailable();
  std::array<std::array<int, 4>, 4> cpuPixels;
  const int locations[4][2] = {{16, 16}, {48, 16}, {16, 48}, {48, 48}};
  for (int backend = 0; backend < (gpuAvailable ? 2 : 1); ++backend) {
    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
    if (backend == 0) target->getPimpl()->backend.reset(new SoWgpuCpuReferenceBackend);
    FramePlan plan = makePlan(true);
    addQuad(plan, -3.0f, {{0, 1, 0}}, 1.0f, true); // Shares texture slot 0.
    plan.samplers[0].filter = TextureFilter::NEAREST;
    for (int texel = 0; texel < 4; ++texel) plan.textures[0].pixelsRgba[texel * 4 + 3] = 255;
    const FrameExecutionResult first = target->getPimpl()->executeFrame(plan);
    if (!check(first.status == BackendStatus::SUCCESS, "opaque texture frame")) {
      delete target;
      return false;
    }
    std::vector<uint8_t> rgba;
    target->readbackRGBA(rgba);
    bool opaqueRed = true;
    for (int i = 0; i < 4; ++i) {
      const size_t pixel = static_cast<size_t>(locations[i][1] * 64 + locations[i][0]) * 4;
      opaqueRed = opaqueRed && rgba[pixel] >= 245 && rgba[pixel + 2] <= 10;
    }
    if (!check(opaqueRed, "opaque shared texture should show front red quad")) {
      delete target;
      return false;
    }

    const uint64_t serial = target->getLastSubmissionSerial();
    const int alpha[4] = {0, 255, 255, 0};
    for (int texel = 0; texel < 4; ++texel)
      plan.textures[0].pixelsRgba[texel * 4 + 3] = static_cast<uint8_t>(alpha[texel]);
    // Keep the digest intentionally unchanged: content bytes must still be checked.
    const FrameExecutionResult second = target->getPimpl()->executeFrame(plan);
    if (!check(second.status == BackendStatus::SUCCESS, "mutated texture frame") ||
        !check(target->getLastSubmissionSerial() > serial, "mutated frame submission serial")) {
      delete target;
      return false;
    }
    target->readbackRGBA(rgba);
    std::vector<float> depth;
    target->readbackDepth(depth);
    int red = 0, blue = 0;
    for (int i = 0; i < 4; ++i) {
      const size_t pixel = static_cast<size_t>(locations[i][1] * 64 + locations[i][0]);
      std::array<int, 4> observed;
      for (int c = 0; c < 4; ++c) observed[c] = rgba[pixel * 4 + c];
      if (observed[0] >= 245 && observed[2] <= 10) ++red;
      if (observed[2] >= 245 && observed[0] <= 10) ++blue;
      if (backend == 0) cpuPixels[i] = observed;
      else for (int c = 0; c < 4; ++c) {
        if (!check(std::abs(observed[c] - cpuPixels[i][c]) <= 6,
                   "GPU/CPU shared checkerboard parity")) { delete target; return false; }
      }
      if (!check(std::abs(depth[pixel] - 1.0f) < 0.02f,
                 "transparent checkerboard must not write depth")) { delete target; return false; }
    }
    delete target;
    if (!check(red == 2 && blue == 2, "checkerboard alpha distribution")) return false;
  }
  return true;
}

bool testAlphaAndOcclusion() {
  const bool gpuAvailable = SoWgpuRenderAction::isGpuBackendAvailable();
  for (int backend = 0; backend < (gpuAvailable ? 2 : 1); ++backend) {
    const bool cpu = backend == 0;
    FramePlan zero;
    zero.clearColor = SbColor4f(0, 0, 0, 1);
    zero.lightingStates.push_back(LightingSnapshot{});
    zero.cameras.push_back(CameraSnapshot{});
    ViewportSnapshot viewport;
    viewport.width = 64;
    viewport.height = 64;
    zero.viewports.push_back(viewport);
    addQuad(zero, -5.0f, {{0, 0, 1}}, 1.0f);
    addQuad(zero, -2.0f, {{1, 0, 0}}, 0.0f);
    Sample pixel;
    if (!render(zero, cpu, pixel) ||
        !check(nearColor(pixel, {{0, 0, 255, 255}}, 5), "alpha zero must preserve background")) return false;

    FramePlan one = zero;
    one.materials[1].diffuse[3] = 1.0f;
    one.materials[1].transparency = 0.0f;
    if (!render(one, cpu, pixel) ||
        !check(nearColor(pixel, {{255, 0, 0, 255}}, 5), "alpha one must be opaque") ||
        !check(std::abs(pixel.depth - 0.25f) < 0.02f, "opaque foreground depth")) return false;

    FramePlan blocked = zero;
    blocked.materials[1].diffuse[3] = 0.5f;
    blocked.materials[1].transparency = 0.5f;
    for (size_t i = 4; i < 8; ++i) blocked.vertices[i].position[2] = -4.0f;
    addQuad(blocked, -2.0f, {{0, 1, 0}}, 1.0f);
    if (!render(blocked, cpu, pixel) ||
        !check(nearColor(pixel, {{0, 255, 0, 255}}, 5), "opaque foreground blocks transparent background")) return false;
  }
  return true;
}

bool testTargetIsolationAndResize() {
  const bool gpuAvailable = SoWgpuRenderAction::isGpuBackendAvailable();
  for (int backend = 0; backend < (gpuAvailable ? 2 : 1); ++backend) {
    SoWgpuRenderTarget * first = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
    SoWgpuRenderTarget * second = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
    if (backend == 0) {
      first->getPimpl()->backend.reset(new SoWgpuCpuReferenceBackend);
      second->getPimpl()->backend.reset(new SoWgpuCpuReferenceBackend);
    }
    FramePlan mixed = makePlan();
    FramePlan blue = makePlan();
    blue.materials[0].diffuse[3] = 0.0f;
    blue.materials[0].transparency = 1.0f;
    blue.materials[2].diffuse[3] = 0.0f;
    blue.materials[2].transparency = 1.0f;
    const bool firstOk = first->getPimpl()->executeFrame(mixed).status == BackendStatus::SUCCESS;
    const bool secondOk = second->getPimpl()->executeFrame(blue).status == BackendStatus::SUCCESS;
    std::vector<uint8_t> secondBefore, secondAfter, resizedColor;
    second->readbackRGBA(secondBefore);
    mixed.viewports[0].width = 32;
    mixed.viewports[0].height = 32;
    const bool resizeOk = first->resize(SbVec2i32(32, 32)) != FALSE;
    const bool thirdOk = resizeOk &&
      first->getPimpl()->executeFrame(mixed).status == BackendStatus::SUCCESS;
    first->readbackRGBA(resizedColor);
    second->readbackRGBA(secondAfter);
    const bool isolated = check(firstOk && secondOk && thirdOk, "two targets and resize must render") &&
      check(resizedColor.size() == 32u * 32u * 4u, "resized target dimensions") &&
      check(secondBefore == secondAfter, "resizing one target must not change the other");
    delete first;
    delete second;
    if (!isolated) return false;
  }
  return true;
}

bool testFailurePreservesFrame() {
  const bool gpuAvailable = SoWgpuRenderAction::isGpuBackendAvailable();
  for (int backend = 0; backend < (gpuAvailable ? 2 : 1); ++backend) {
    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
    if (backend == 0) target->getPimpl()->backend.reset(new SoWgpuCpuReferenceBackend);
    FramePlan valid = makePlan();
    if (!check(target->getPimpl()->executeFrame(valid).status == BackendStatus::SUCCESS,
               "initial valid frame")) { delete target; return false; }
    std::vector<uint8_t> beforeColor, afterColor;
    std::vector<float> beforeDepth, afterDepth;
    target->readbackRGBA(beforeColor);
    target->readbackDepth(beforeDepth);
    const uint64_t beforeSerial = target->getLastSubmissionSerial();
    valid.renderStates[0].transparencyType = SoGLRenderAction::SCREEN_DOOR;
    const FrameExecutionResult rejected = target->getPimpl()->executeFrame(valid);
    target->readbackRGBA(afterColor);
    target->readbackDepth(afterDepth);
    const bool preserved = check(rejected.status == BackendStatus::UNSUPPORTED,
                                 "unsupported transparency rejected") &&
                           check(afterColor == beforeColor && afterDepth == beforeDepth,
                                 "failed frame must preserve color and depth") &&
                           check(target->getLastSubmissionSerial() == beforeSerial,
                                 "failed frame must preserve submission serial");
    delete target;
    if (!preserved) return false;
  }
  return true;
}

bool testRejections() {
  FramePlan plan = makePlan();
  std::vector<SoWgpuCompositionItem> order;
  std::string diagnostic;
  plan.renderStates[0].transparencyType = SoGLRenderAction::SCREEN_DOOR;
  if (!check(!coin_wgpu_composition_order(plan, order, diagnostic) &&
             diagnostic.find("SORTED_OBJECT_BLEND") != std::string::npos,
             "unsupported Coin transparency mode")) return false;
  plan.renderStates[0].transparencyType = SoGLRenderAction::SORTED_OBJECT_BLEND;
  plan.vertices[0].materialSlot = 1;
  if (!check(!coin_wgpu_composition_order(plan, order, diagnostic) &&
             diagnostic.find("mixed") != std::string::npos,
             "mixed alpha must be rejected")) return false;
  return true;
}

bool testSceneTextureRejected() {
  SoSeparator * root = new SoSeparator;
  root->ref();
  root->addChild(new SoSceneTexture2);
  root->addChild(new SoCube);
  SoWgpuRenderAction action(SbViewportRegion(64, 64));
  action.apply(root);
  const bool rejected = check(action.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED,
                               "SoSceneTexture2 must not render as a missing texture") &&
                        check(std::string(action.getLastError().getString()).find("SoSceneTexture2") != std::string::npos,
                               "SoSceneTexture2 rejection diagnostic");
  root->unref();
  return rejected;
}

bool testTraversal() {
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoTransparencyType * mode = new SoTransparencyType;
  mode->value = SoTransparencyType::SORTED_OBJECT_BLEND;
  root->addChild(mode);
  SoMaterial * material = new SoMaterial;
  material->transparency.setValue(0.5f);
  root->addChild(material);
  root->addChild(new SoCube);
  SoWgpuRenderAction action(SbViewportRegion(64, 64));
  action.apply(root);
  const bool ok = check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
                        "SoTransparencyType traversal capture") &&
                  check(std::string(action.getRecordingLog().getString()).find("composition: SORTED_OBJECT_BLEND") != std::string::npos,
                        "Recording composition order");
  if (!ok) std::cerr << action.getLastError().getString() << "\n";
  root->unref();
  return ok;
}

} // namespace

int main() {
  SoDB::init();
  SoWgpuRenderAction::initClass();
  if (!testMaterialBlend() || !testStableDepthTie() || !testTextureAlpha() ||
      !testTextureMutationAndSharing() ||
      !testAlphaAndOcclusion() || !testTargetIsolationAndResize() ||
      !testFailurePreservesFrame() ||
      !testRejections() || !testSceneTextureRejected() ||
      !testTraversal()) return 1;
  std::cout << "WgpuCompositionTest passed\n";
  return 0;
}
