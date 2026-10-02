#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"

#include <array>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const char * message) {
  if (!condition) std::cerr << "CoinRenderCompositionTest: " << message << "\n";
  return condition;
}

struct Sample {
  std::array<int, 4> rgba = {{0, 0, 0, 0}};
  float depth = 1.0f;
};

void configureTarget(CoinRenderTarget * target, bool cpu) {
  if (cpu) target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
#if defined(HAVE_COIN_BGFX)
  if (cpu) {
    const SbVec2i32 size = target->getPimpl()->size;
    target->getPimpl()->depthBuffer.assign(
      static_cast<size_t>(size[0]) * static_cast<size_t>(size[1]), 1.0f);
  } else {
    target->setDepthReadbackEnabled(FALSE);
  }
#endif
}

bool depthMatches(const Sample & sample, float expected, bool cpu) {
#if defined(HAVE_COIN_BGFX)
  if (!cpu) return true;
#else
  (void)cpu;
#endif
  return std::abs(sample.depth - expected) < 0.02f;
}

SbMatrix projection() {
  // z=-2 -> depth=.25; z=-4 -> .75; z=-5 -> 1.0.
  return SbMatrix(1, 0, 0, 0,
                  0, 1, 0, 0,
                  0, 0, -0.5f, 0,
                  0, 0, -1.5f, 1);
}

void addQuad(CoinRenderFramePlan & plan, float z, const std::array<float, 3> & color,
             float alpha, bool textured = false) {
  const uint32_t materialSlot = static_cast<uint32_t>(plan.materials.size());
  CoinRenderMaterialSnapshot material;
  for (int c = 0; c < 3; ++c) material.diffuse[c] = color[c];
  material.diffuse[3] = alpha;
  material.transparency = 1.0f - alpha;
  plan.materials.push_back(material);

  CoinRenderRenderStateSnapshot state;
  state.materialSlot = materialSlot;
  state.lightModel = CoinRenderLightModel::BASE_COLOR;
  state.cullMode = CoinRenderCullMode::NONE;
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
    CoinRenderVertexSnapshot vertex;
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
  CoinRenderDrawPacket draw;
  draw.geometry.firstVertex = firstVertex;
  draw.geometry.vertexCount = 4;
  draw.geometry.firstIndex = firstIndex;
  draw.geometry.indexCount = 6;
  draw.renderStateSlot = stateSlot;
  plan.draws.push_back(draw);
}

CoinRenderFramePlan makePlan(bool textureAlpha = false) {
  CoinRenderFramePlan plan;
  plan.clearColor = SbColor4f(0, 0, 0, 1);
  plan.lightingStates.push_back(CoinRenderLightingSnapshot{});
  plan.cameras.push_back(CoinRenderCameraSnapshot{});
  CoinRenderViewportSnapshot viewport;
  viewport.width = 64;
  viewport.height = 64;
  plan.viewports.push_back(viewport);
  // Deliberately reverse input order. The scheduler must submit blue opaque,
  // then green far, then red near.
  addQuad(plan, -2.0f, {{1, 0, 0}}, textureAlpha ? 1.0f : 0.5f, textureAlpha);
  addQuad(plan, -4.5f, {{0, 0, 1}}, 1.0f);
  if (!textureAlpha) addQuad(plan, -4.0f, {{0, 1, 0}}, 0.5f);
  if (textureAlpha) {
    CoinRenderTextureImageSnapshot texture;
    texture.width = texture.height = 2;
    texture.components = 4;
    texture.pixelsRgba.resize(16, 255);
    for (int i = 0; i < 4; ++i) texture.pixelsRgba[i * 4 + 3] = 128;
    texture.contentDigest = 0x4231;
    plan.textures.push_back(texture);
    plan.samplers.push_back(CoinRenderSamplerSnapshot{});
  }
  return plan;
}

bool render(const CoinRenderFramePlan & plan, bool cpu, Sample & sample) {
  CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(64, 64));
  configureTarget(target, cpu);
  const CoinRenderFrameExecutionResult result = target->getPimpl()->executeFrame(plan);
  if (result.status != CoinRenderBackendStatus::SUCCESS) {
    std::cerr << "Render failed: " << result.diagnostic << "\n";
    delete target;
    return false;
  }
  std::vector<uint8_t> rgba;
  std::vector<float> depth;
  target->readbackRGBA(rgba);
  target->readbackDepth(depth);
  delete target;
  if (!check(rgba.size() == 64u * 64u * 4u &&
             (depth.size() == 64u * 64u || (!cpu && depth.empty())),
             "readback size")) return false;
  const size_t pixel = 32u * 64u + 40u; // Avoid the shared triangle diagonal.
  for (int c = 0; c < 4; ++c) sample.rgba[c] = rgba[pixel * 4u + c];
  if (!depth.empty()) sample.depth = depth[pixel];
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
  CoinRenderFramePlan plan = makePlan();
  std::string diagnostic;
  std::vector<CoinRenderCompositionItem> order;
  if (!check(plan.isValid(&diagnostic) &&
             coin_render_composition_order(plan, order, diagnostic), "valid blend plan")) return false;
  if (!check(order.size() == 3 && order[0].drawIndex == 1 &&
             order[1].drawIndex == 2 && order[2].drawIndex == 0,
             "opaque/far/near ordering")) return false;
  Sample cpu;
  if (!render(plan, true, cpu) || !check(nearColor(cpu, {{127, 63, 63, 255}}, 3), "CPU source-over")) return false;
  if (!check(std::abs(cpu.depth - 0.875f) < 0.01f, "transparent draws must not write depth")) return false;
  if (CoinRenderAction::isGpuBackendAvailable()) {
    Sample gpu;
    if (!render(plan, false, gpu) || !check(nearColor(gpu, {{127, 63, 63, 255}}, 5), "GPU source-over")) return false;
    if (!check(depthMatches(gpu, cpu.depth, false), "CPU/GPU depth parity")) return false;
  }
  return true;
}

bool testStableDepthTie() {
  CoinRenderFramePlan plan = makePlan();
  for (size_t i = 8; i < 12; ++i) plan.vertices[i].position[2] = -2.0f;
  std::vector<CoinRenderCompositionItem> order;
  std::string diagnostic;
  if (!check(coin_render_composition_order(plan, order, diagnostic) &&
             order.size() == 3 && order[0].drawIndex == 1 &&
             order[1].drawIndex == 0 && order[2].drawIndex == 2,
             "equal-depth transparent draws preserve traversal order")) return false;
  Sample cpu;
  if (!render(plan, true, cpu) ||
      !check(nearColor(cpu, {{63, 127, 63, 255}}, 4), "CPU stable-depth blend")) return false;
  if (CoinRenderAction::isGpuBackendAvailable()) {
    Sample gpu;
    if (!render(plan, false, gpu) ||
        !check(nearColor(gpu, {{63, 127, 63, 255}}, 6), "GPU stable-depth blend")) return false;
  }
  return true;
}

bool testOverlayPreservesTraversalOrder() {
  CoinRenderFramePlan plan = makePlan();
  for (CoinRenderDrawPacket & draw : plan.draws) draw.renderLayer = 1;
  std::vector<CoinRenderCompositionItem> order;
  std::string diagnostic;
  if (!check(coin_render_composition_order(plan, order, diagnostic) &&
             order.size() == 3 && order[0].drawIndex == 0 &&
             order[1].drawIndex == 1 && order[2].drawIndex == 2,
             "overlay opaque/transparent draws preserve traversal order"))
    return false;

  plan.draws[0].renderLayer = 2;
  plan.draws[1].renderLayer = 0;
  plan.draws[2].renderLayer = 1;
  return check(coin_render_composition_order(plan, order, diagnostic) &&
               order.size() == 3 && order[0].drawIndex == 1 &&
               order[1].drawIndex == 2 && order[2].drawIndex == 0,
               "composition keeps layer order around traversal-stable overlays");
}

bool testMixedOverlayRendersInTraversalOrder() {
  CoinRenderFramePlan plan;
  plan.clearColor = SbColor4f(0, 0, 0, 1);
  plan.lightingStates.push_back(CoinRenderLightingSnapshot{});
  plan.cameras.push_back(CoinRenderCameraSnapshot{});
  CoinRenderViewportSnapshot viewport;
  viewport.width = viewport.height = 64;
  plan.viewports.push_back(viewport);
  addQuad(plan, -4.5f, {{0, 0, 1}}, 1.0f);
  addQuad(plan, -2.0f, {{1, 0, 0}}, 0.5f);
  plan.draws.back().renderLayer = 1;
  plan.draws.back().clearDepthBefore = true;
  addQuad(plan, -4.0f, {{0, 1, 0}}, 1.0f);
  plan.draws.back().renderLayer = 1;

  plan.renderStates[1].depthWrite = false;
  const bool gpuAvailable = CoinRenderAction::isGpuBackendAvailable();
  for (int backend = 0; backend < (gpuAvailable ? 2 : 1); ++backend) {
    Sample sample;
    if (!render(plan, backend == 0, sample) ||
        !check(nearColor(sample, {{0, 255, 0, 255}}, 6),
               "mixed overlay must render in transparent-then-opaque traversal order"))
      return false;
  }
  return true;
}

// The first annotation clears only the right half. A subsequent fullscreen
// layer must remain occluded on the left, proving depth there was preserved.
bool testAnnotationDepthClearViewport() {
  CoinRenderFramePlan plan;
  plan.clearColor = SbColor4f(0, 0, 0, 1);
  plan.lightingStates.push_back(CoinRenderLightingSnapshot{});
  plan.cameras.push_back(CoinRenderCameraSnapshot{});
  CoinRenderViewportSnapshot viewport;
  viewport.width = viewport.height = 64;
  plan.viewports.push_back(viewport);
  addQuad(plan, -2.0f, {{1, 0, 0}}, 1.0f);
  viewport.x = 32; viewport.width = 32;
  plan.viewports.push_back(viewport);
  addQuad(plan, -4.0f, {{0, 0, 1}}, 1.0f);
  plan.renderStates.back().viewportSlot = 1;
  plan.draws.back().renderLayer = 1;
  plan.draws.back().clearDepthBefore = true;
  addQuad(plan, -3.0f, {{0, 1, 0}}, 1.0f);
  plan.draws.back().renderLayer = 2;
  for (int backend = 0; backend < 2; ++backend) {
    CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(64, 64));
    configureTarget(target, backend == 0);
    const auto result = target->getPimpl()->executeFrame(plan);
    std::vector<uint8_t> rgba;
    target->readbackRGBA(rgba);
    bool ok = check(result.status == CoinRenderBackendStatus::SUCCESS && rgba.size() == 64u * 64u * 4u,
      "annotation viewport frame/readback failed");
    if (!ok) std::cerr << result.diagnostic << '\n';
    if (ok) {
      const size_t left = (32u * 64u + 16u) * 4u;
      const size_t right = (32u * 64u + 48u) * 4u;
      ok &= check(rgba[left] > 240 && rgba[left + 1] < 10,
        "annotation clear leaked outside its viewport");
      ok &= check(rgba[right] < 10 && rgba[right + 1] > 240,
        "annotation clear did not release depth inside its viewport");
    }
    if (ok && backend == 1) {
      const auto before = rgba;
      const uint64_t serial = target->getLastSubmissionSerial();
      plan.viewports[1].x = -1;
      const auto rejected = target->getPimpl()->executeFrame(plan);
      target->readbackRGBA(rgba);
      ok &= check(rejected.status != CoinRenderBackendStatus::SUCCESS &&
        rejected.diagnostic.find("viewport") != std::string::npos && rgba == before &&
        target->getLastSubmissionSerial() == serial, "invalid annotation viewport changed the frame");
      plan.viewports[1].x = 32;
      CoinRenderFramePlan empty = plan;
      empty.draws.clear();
      const auto cleared = target->getPimpl()->executeFrame(empty);
      target->readbackRGBA(rgba);
      const size_t center = (32u * 64u + 16u) * 4u;
      ok &= check(cleared.status == CoinRenderBackendStatus::SUCCESS && rgba[center] == 0 &&
        rgba[center + 1] == 0 && rgba[center + 2] == 0 && rgba[center + 3] == 255,
        "empty frame must still clear attachments");
    }
    delete target;
    if (!ok) return false;
  }
  // A translucent annotation without depth writes followed by an opaque draw
  // must execute in traversal order; opaque-first splitting would leave red.
  plan = makePlan();
  // Keep the opaque base inside the far plane under LESS depth testing.
  for (size_t i = 4; i < 8; ++i) plan.vertices[i].position[2] = -4.5f;
  plan.draws.erase(plan.draws.begin() + 2);
  plan.draws[0].renderLayer = 1;
  plan.draws[0].clearDepthBefore = true;
  plan.renderStates[0].depthWrite = false;
  addQuad(plan, -4.0f, {{0, 1, 0}}, 1.0f);
  plan.draws.back().renderLayer = 1;
  Sample sample;
  if (!render(plan, false, sample) || !check(nearColor(sample, {{0, 255, 0, 255}}, 6),
    "wgpu annotations reordered opaque and translucent draws")) return false;
  plan.renderStates[0].depthWrite = true;
  return render(plan, false, sample) && check(nearColor(sample, {{128, 0, 127, 255}}, 6),
    "depth-writing translucent annotation must occlude the later opaque draw");
}

bool testTextureAlpha() {
  CoinRenderFramePlan plan = makePlan(true);
  Sample cpu;
  if (!render(plan, true, cpu) || !check(nearColor(cpu, {{128, 0, 127, 255}}, 4), "CPU texture alpha")) return false;
  if (CoinRenderAction::isGpuBackendAvailable()) {
    Sample gpu;
    if (!render(plan, false, gpu) || !check(nearColor(gpu, {{128, 0, 127, 255}}, 6), "GPU texture alpha")) return false;
  }
  return true;
}

bool testTextureMutationAndSharing() {
  const bool gpuAvailable = CoinRenderAction::isGpuBackendAvailable();
  std::array<std::array<int, 4>, 4> cpuPixels;
  const int locations[4][2] = {{16, 16}, {48, 16}, {16, 48}, {48, 48}};
  for (int backend = 0; backend < (gpuAvailable ? 2 : 1); ++backend) {
    CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(64, 64));
    configureTarget(target, backend == 0);
    CoinRenderFramePlan plan = makePlan(true);
    addQuad(plan, -3.0f, {{0, 1, 0}}, 1.0f, true); // Shares texture slot 0.
    plan.samplers[0].filter = CoinRenderTextureFilter::NEAREST;
    for (int texel = 0; texel < 4; ++texel) plan.textures[0].pixelsRgba[texel * 4 + 3] = 255;
    const CoinRenderFrameExecutionResult first = target->getPimpl()->executeFrame(plan);
    if (!check(first.status == CoinRenderBackendStatus::SUCCESS, "opaque texture frame")) {
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
    const CoinRenderFrameExecutionResult second = target->getPimpl()->executeFrame(plan);
    if (!check(second.status == CoinRenderBackendStatus::SUCCESS, "mutated texture frame") ||
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
      if (!depth.empty() && !check(std::abs(depth[pixel] - 0.875f) < 0.02f,
                 "transparent checkerboard must not write depth")) { delete target; return false; }
    }
    delete target;
    if (!check(red == 2 && blue == 2, "checkerboard alpha distribution")) return false;
  }
  return true;
}

bool testAlphaAndOcclusion() {
  const bool gpuAvailable = CoinRenderAction::isGpuBackendAvailable();
  for (int backend = 0; backend < (gpuAvailable ? 2 : 1); ++backend) {
    const bool cpu = backend == 0;
    CoinRenderFramePlan zero;
    zero.clearColor = SbColor4f(0, 0, 0, 1);
    zero.lightingStates.push_back(CoinRenderLightingSnapshot{});
    zero.cameras.push_back(CoinRenderCameraSnapshot{});
    CoinRenderViewportSnapshot viewport;
    viewport.width = 64;
    viewport.height = 64;
    zero.viewports.push_back(viewport);
    addQuad(zero, -4.5f, {{0, 0, 1}}, 1.0f);
    addQuad(zero, -2.0f, {{1, 0, 0}}, 0.0f);
    Sample pixel;
    if (!render(zero, cpu, pixel) ||
        !check(nearColor(pixel, {{0, 0, 255, 255}}, 5), "alpha zero must preserve background")) return false;

    CoinRenderFramePlan one = zero;
    one.materials[1].diffuse[3] = 1.0f;
    one.materials[1].transparency = 0.0f;
    if (!render(one, cpu, pixel) ||
        !check(nearColor(pixel, {{255, 0, 0, 255}}, 5), "alpha one must be opaque") ||
        !check(depthMatches(pixel, 0.25f, cpu), "opaque foreground depth")) return false;

    CoinRenderFramePlan blocked = zero;
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
  const bool gpuAvailable = CoinRenderAction::isGpuBackendAvailable();
  for (int backend = 0; backend < (gpuAvailable ? 2 : 1); ++backend) {
    CoinRenderTarget * first = CoinRenderTarget::createOffscreen(SbVec2i32(64, 64));
    CoinRenderTarget * second = CoinRenderTarget::createOffscreen(SbVec2i32(64, 64));
    configureTarget(first, backend == 0);
    configureTarget(second, backend == 0);
    CoinRenderFramePlan mixed = makePlan();
    CoinRenderFramePlan blue = makePlan();
    blue.materials[0].diffuse[3] = 0.0f;
    blue.materials[0].transparency = 1.0f;
    blue.materials[2].diffuse[3] = 0.0f;
    blue.materials[2].transparency = 1.0f;
    const bool firstOk = first->getPimpl()->executeFrame(mixed).status == CoinRenderBackendStatus::SUCCESS;
    const bool secondOk = second->getPimpl()->executeFrame(blue).status == CoinRenderBackendStatus::SUCCESS;
    std::vector<uint8_t> secondBefore, secondAfter, resizedColor;
    second->readbackRGBA(secondBefore);
    mixed.viewports[0].width = 32;
    mixed.viewports[0].height = 32;
    const bool resizeOk = first->resize(SbVec2i32(32, 32)) != FALSE;
    const bool thirdOk = resizeOk &&
      first->getPimpl()->executeFrame(mixed).status == CoinRenderBackendStatus::SUCCESS;
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

bool testMultipleViewports() {
  CoinRenderFramePlan plan;
  plan.clearColor = SbColor4f(0, 0, 0, 1);
  plan.lightingStates.push_back(CoinRenderLightingSnapshot{});
  plan.cameras.push_back(CoinRenderCameraSnapshot{});
  for (int viewportIndex = 0; viewportIndex < 2; ++viewportIndex) {
    CoinRenderViewportSnapshot viewport;
    viewport.x = viewportIndex * 32;
    viewport.width = 32;
    viewport.height = 32;
    plan.viewports.push_back(viewport);

    CoinRenderMaterialSnapshot material;
    material.diffuse[0] = viewportIndex == 0 ? 1.0f : 0.0f;
    material.diffuse[1] = viewportIndex == 1 ? 1.0f : 0.0f;
    material.diffuse[2] = 0.0f;
    plan.materials.push_back(material);

    CoinRenderRenderStateSnapshot state;
    state.viewportSlot = static_cast<uint32_t>(viewportIndex);
    state.materialSlot = static_cast<uint32_t>(viewportIndex);
    state.lightModel = CoinRenderLightModel::BASE_COLOR;
    state.cullMode = CoinRenderCullMode::NONE;
    const uint32_t stateSlot = static_cast<uint32_t>(plan.renderStates.size());
    plan.renderStates.push_back(state);

    const uint32_t firstVertex = static_cast<uint32_t>(plan.vertices.size());
    const float position[3][2] = {{-1.0f, -1.0f}, {3.0f, -1.0f}, {-1.0f, 3.0f}};
    for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex) {
      CoinRenderVertexSnapshot vertex;
      vertex.position[0] = position[vertexIndex][0];
      vertex.position[1] = position[vertexIndex][1];
      vertex.materialSlot = static_cast<uint32_t>(viewportIndex);
      plan.vertices.push_back(vertex);
      plan.indices.push_back(firstVertex + static_cast<uint32_t>(vertexIndex));
    }
    CoinRenderDrawPacket draw;
    draw.renderStateSlot = stateSlot;
    draw.geometry.firstVertex = firstVertex;
    draw.geometry.vertexCount = 3;
    draw.geometry.firstIndex = static_cast<uint32_t>(plan.indices.size() - 3);
    draw.geometry.indexCount = 3;
    plan.draws.push_back(draw);
  }

  const bool gpuAvailable = CoinRenderAction::isGpuBackendAvailable();
  for (int backend = 0; backend < (gpuAvailable ? 2 : 1); ++backend) {
    CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(64, 32));
    configureTarget(target, backend == 0);
    const CoinRenderFrameExecutionResult result = target->getPimpl()->executeFrame(plan);
    std::vector<uint8_t> rgba;
    target->readbackRGBA(rgba);
    const size_t left = static_cast<size_t>(16 * 64 + 16) * 4;
    const size_t right = static_cast<size_t>(16 * 64 + 48) * 4;
    const bool ok = check(result.status == CoinRenderBackendStatus::SUCCESS,
                          "multiple subviewports must render") &&
      check(rgba.size() == 64u * 32u * 4u, "multiple viewport readback size") &&
      check(rgba[left] >= 250 && rgba[left + 1] <= 5,
            "left viewport must remain red") &&
      check(rgba[right] <= 5 && rgba[right + 1] >= 250,
            "right viewport must remain green");
    delete target;
    if (!ok) return false;
  }
  return true;
}

bool testOverlayDepthBarriersWithSortedBase() {
  CoinRenderFramePlan plan;
  plan.clearColor = SbColor4f(0, 0, 0, 1);
  plan.lightingStates.push_back(CoinRenderLightingSnapshot{});
  plan.cameras.push_back(CoinRenderCameraSnapshot{});
  CoinRenderViewportSnapshot full;
  full.width = full.height = 64;
  plan.viewports.push_back(full);
  addQuad(plan, -2.0f, {{1, 0, 0}}, 1.0f);
  addQuad(plan, -3.0f, {{0, 1, 0}}, 0.5f);
  plan.renderStates.back().transparencyType =
    SoGLRenderAction::SORTED_OBJECT_BLEND;

  CoinRenderViewportSnapshot lowerRight;
  lowerRight.x = 32; lowerRight.y = 0;
  lowerRight.width = lowerRight.height = 32;
  plan.viewports.push_back(lowerRight);
  addQuad(plan, -4.0f, {{0, 0, 1}}, 1.0f);
  plan.renderStates.back().viewportSlot = 1;
  plan.draws.back().renderLayer = 1;
  plan.draws.back().clearDepthBefore = true;

  CoinRenderViewportSnapshot upperRight = lowerRight;
  upperRight.y = 32;
  plan.viewports.push_back(upperRight);
  addQuad(plan, -4.5f, {{1, 1, 0}}, 1.0f);
  plan.renderStates.back().viewportSlot = 2;
  plan.draws.back().renderLayer = 2;
  plan.draws.back().clearDepthBefore = true;

  const bool gpuAvailable = CoinRenderAction::isGpuBackendAvailable();
  for (int backend = 0; backend < (gpuAvailable ? 2 : 1); ++backend) {
    CoinRenderTarget * target =
      CoinRenderTarget::createOffscreen(SbVec2i32(64, 64));
    configureTarget(target, backend == 0);
    const CoinRenderFrameExecutionResult result = target->getPimpl()->executeFrame(plan);
    std::vector<uint8_t> rgba;
    target->readbackRGBA(rgba);
    const size_t lower = static_cast<size_t>(48 * 64 + 48) * 4;
    const size_t upper = static_cast<size_t>(16 * 64 + 48) * 4;
    const bool ok = check(result.status == CoinRenderBackendStatus::SUCCESS,
                          "sorted base plus overlay layers must render") &&
      check(rgba.size() == 64u * 64u * 4u, "overlay readback size") &&
      check(rgba[lower] <= 8 && rgba[lower + 1] <= 8 && rgba[lower + 2] >= 247,
            "first annotation viewport must be blue after depth clear") &&
      check(rgba[upper] >= 247 && rgba[upper + 1] >= 247 && rgba[upper + 2] <= 8,
            "second annotation viewport must be yellow after its own depth clear");
    delete target;
    if (!ok) return false;
  }
  return true;
}

bool testFailurePreservesFrame() {
  const bool gpuAvailable = CoinRenderAction::isGpuBackendAvailable();
  for (int backend = 0; backend < (gpuAvailable ? 2 : 1); ++backend) {
    CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(64, 64));
    configureTarget(target, backend == 0);
    CoinRenderFramePlan valid = makePlan();
    if (!check(target->getPimpl()->executeFrame(valid).status == CoinRenderBackendStatus::SUCCESS,
               "initial valid frame")) { delete target; return false; }
    std::vector<uint8_t> beforeColor, afterColor;
    std::vector<float> beforeDepth, afterDepth;
    target->readbackRGBA(beforeColor);
    target->readbackDepth(beforeDepth);
    const uint64_t beforeSerial = target->getLastSubmissionSerial();
    valid.renderStates[0].transparencyType = -1;
    const CoinRenderFrameExecutionResult rejected = target->getPimpl()->executeFrame(valid);
    target->readbackRGBA(afterColor);
    target->readbackDepth(afterDepth);
    const bool preserved = check(rejected.status == CoinRenderBackendStatus::UNSUPPORTED,
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

bool testUnsupportedExecutorPreservesFrame() {
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  const int modes[] = {-1, 999};
  for (int backend = 0; backend < (CoinRenderAction::isGpuBackendAvailable() ? 2 : 1); ++backend) {
    CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(64, 64));
    configureTarget(target, backend == 0);
    const auto initial = makePlan();
    if (!check(target->getPimpl()->executeFrame(initial).status == CoinRenderBackendStatus::SUCCESS,
        "initial frame before unsupported operation")) { delete target; return false; }
    std::vector<uint8_t> before, after;
    target->readbackRGBA(before);
    const auto serial = target->getLastSubmissionSerial();
    for (int mode : modes) {
      auto unsupported = initial;
      unsupported.renderStates[0].transparencyType = mode;
      const auto result = target->getPimpl()->executeFrame(unsupported);
      target->readbackRGBA(after);
      if (!check(result.status == CoinRenderBackendStatus::UNSUPPORTED &&
          before == after && target->getLastSubmissionSerial() == serial &&
          target->getStatus() == CoinRenderTarget::TARGET_READY,
          "unsupported Coin operation must preserve published frame and serial")) {
        delete target; return false;
      }
    }
    if (!check(target->getPimpl()->executeFrame(initial).status == CoinRenderBackendStatus::SUCCESS,
        "valid frame must resume after unsupported operation")) { delete target; return false; }
    delete target;
  }
#endif
  return true;
}

bool testCommonTransparencyPolicy() {
  struct Expectation { int mode; bool blend, deferred, additive, triangles, sorted; };
  const Expectation cases[] = {
    {SoGLRenderAction::NONE, false, false, false, false, false},
    {SoGLRenderAction::SCREEN_DOOR, false, false, false, false, false},
    {SoGLRenderAction::ADD, true, false, true, false, false},
    {SoGLRenderAction::BLEND, true, false, false, false, false},
    {SoGLRenderAction::DELAYED_ADD, true, true, true, false, false},
    {SoGLRenderAction::DELAYED_BLEND, true, true, false, false, false},
    {SoGLRenderAction::SORTED_OBJECT_ADD, true, true, true, false, true},
    {SoGLRenderAction::SORTED_OBJECT_BLEND, true, true, false, false, true},
    {SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD, true, true, true, true, true},
    {SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND, true, true, false, true, true},
    {SoGLRenderAction::SORTED_LAYERS_BLEND, true, true, false, false, false}
  };
  for (const auto & expected : cases) {
    auto plan = makePlan();
    plan.renderStates[0].transparencyType = expected.mode;
    std::vector<CoinRenderCompositionItem> order;
    std::string diagnostic;
    if (!check(coin_render_composition_order(plan, order, diagnostic), "common Coin mode")) return false;
    const auto found = std::find_if(order.begin(), order.end(), [](const CoinRenderCompositionItem & item) { return item.drawIndex == 0; });
    if (!check(found != order.end() && found->blend == expected.blend &&
        found->deferred == expected.deferred && found->additive == expected.additive &&
        found->sortTriangles == expected.triangles && found->sortObject == expected.sorted,
        "Coin immediate/delayed/sorted/additive policy")) return false;
    if (!check(found->depthWrite == !expected.deferred && found->depthFunction ==
        (expected.deferred ? CoinRenderDepthFunction::LEQUAL : CoinRenderDepthFunction::LESS),
        "transparent pass depth defaults")) return false;
    auto & state = plan.renderStates[0];
    state.explicitDepthMask = 15;
    state.depthTest = false; state.depthWrite = true;
    state.depthFunction = CoinRenderDepthFunction::GREATER;
    state.depthRange[0] = 0.2f; state.depthRange[1] = 0.8f;
    if (!coin_render_composition_order(plan, order, diagnostic)) return false;
    const auto explicitItem = std::find_if(order.begin(), order.end(), [](const CoinRenderCompositionItem & item) { return item.drawIndex == 0; });
    if (!check(!explicitItem->depthTest && explicitItem->depthWrite &&
        explicitItem->depthFunction == CoinRenderDepthFunction::GREATER &&
        explicitItem->depthRange[0] == 0.2f && explicitItem->depthRange[1] == 0.8f,
        "explicit SoDepthBuffer fields survive deferred policy")) return false;
  }
  auto plan = makePlan();
  plan.renderStates[0].transparencyType = SoGLRenderAction::BLEND;
  std::vector<CoinRenderCompositionItem> order;
  std::string diagnostic;
  if (!check(coin_render_composition_order(plan, order, diagnostic) && order[0].drawIndex == 0,
      "immediate BLEND retains traversal before opaque objects")) return false;
  plan.renderStates[0].transparencyType = SoGLRenderAction::DELAYED_BLEND;
  if (!check(coin_render_composition_order(plan, order, diagnostic) &&
      order[0].drawIndex == 1 && order[1].drawIndex == 2 && order[2].drawIndex == 0,
      "sorted transparent paths precede unsorted delayed paths")) return false;
  return true;
}

bool testProjectiveDepth() {
  auto plan = makePlan();
  const SbMatrix transforms[] = {
    SbMatrix::identity(),
    SbMatrix(1, 0, 0.2f, 0.1f, 0, 1, -0.3f, 0.05f,
             0, 0, 2, 0.02f, 0, 0, -3, 1)
  };
  for (const auto & transform : transforms) {
    for (auto & state : plan.renderStates) state.model = transform;
    std::vector<CoinRenderCompositionItem> order;
    std::string diagnostic;
    if (!check(coin_render_composition_order(plan, order, diagnostic),
               "finite projective depth must compose")) return false;
    for (const auto & item : order) {
      const auto & draw = plan.draws[item.drawIndex];
      float minDepth = 0, maxDepth = 0;
      for (uint32_t i = 0; i < draw.geometry.indexCount; ++i) {
        const auto & vertex = plan.vertices[plan.indices[draw.geometry.firstIndex + i]];
        SbVec3f eye;
        transform.multVecMatrix(SbVec3f(vertex.position), eye);
        if (i == 0) minDepth = maxDepth = -eye[2];
        else { minDepth = std::min(minDepth, -eye[2]); maxDepth = std::max(maxDepth, -eye[2]); }
      }
      if (!check(item.eyeDepth == (minDepth + maxDepth) * 0.5f,
                 "composition depth must match Coin homogeneous transformation")) return false;
    }
  }
  plan.renderStates[0].model[3][3] = 0;
  plan.renderStates[0].model[0][3] = 0;
  plan.renderStates[0].model[1][3] = 0;
  plan.renderStates[0].model[2][3] = 0;
  std::vector<CoinRenderCompositionItem> order;
  std::string diagnostic;
  return check(!coin_render_composition_order(plan, order, diagnostic) &&
               diagnostic.find("non-finite eye depth") != std::string::npos,
               "zero homogeneous W must still be rejected");
}

bool testRejections() {
  CoinRenderFramePlan plan = makePlan();
  std::vector<CoinRenderCompositionItem> order;
  std::string diagnostic;
  struct Mapping { int mode; CoinRenderCompositionItem::TransparencyStrategy strategy; };
  const Mapping mappings[] = {
    {SoGLRenderAction::SCREEN_DOOR, CoinRenderCompositionItem::OBJECT},
    {SoGLRenderAction::BLEND, CoinRenderCompositionItem::OBJECT},
    {SoGLRenderAction::DELAYED_BLEND, CoinRenderCompositionItem::OBJECT},
    {SoGLRenderAction::SORTED_OBJECT_BLEND, CoinRenderCompositionItem::OBJECT},
    {SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND, CoinRenderCompositionItem::OBJECT},
    {SoGLRenderAction::SORTED_LAYERS_BLEND, CoinRenderCompositionItem::SORTED_LAYERS}
  };
  for (const Mapping & mapping : mappings) {
    plan.renderStates[0].transparencyType = mapping.mode;
    if (!check(coin_render_composition_order(plan, order, diagnostic) &&
               !order.empty() && std::find_if(order.begin(), order.end(), [&mapping](const CoinRenderCompositionItem & item) { return item.drawIndex == 0 && item.transparencyStrategy == mapping.strategy; }) != order.end(),
               "Coin transparency mapping")) return false;
  }
  plan.renderStates[0].transparencyType = -1;
  if (!check(!coin_render_composition_order(plan, order, diagnostic) &&
             diagnostic.find("Unknown Coin transparency mode") != std::string::npos,
             "unknown Coin transparency mode")) return false;
  plan.renderStates[0].transparencyType = SoGLRenderAction::SORTED_OBJECT_BLEND;
  plan.vertices[0].materialSlot = 1;
  const bool mixedBlend = coin_render_composition_order(plan, order, diagnostic) &&
    std::find_if(order.begin(), order.end(), [](const CoinRenderCompositionItem & item) {
      return item.drawIndex == 0 && item.blend;
    }) != order.end();
  if (!check(mixedBlend, "mixed per-vertex alpha must enter the blended pass")) return false;
  return true;
}

bool testSceneTextureRejected() {
  SoSeparator * root = new SoSeparator;
  root->ref();
  root->addChild(new SoSceneTexture2);
  root->addChild(new SoCube);
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.apply(root);
  const bool rejected = check(action.getLastStatus() == CoinRenderAction::UNSUPPORTED,
                               "SoSceneTexture2 must not render as a missing texture") &&
                        check(std::string(action.getLastError().getString()).find("SoSceneTexture2") != std::string::npos,
                               "SoSceneTexture2 rejection diagnostic");
  root->unref();
  return rejected;
}

bool testTraversal() {
  struct TraversalMapping {
    SoTransparencyType::Type mode;
    const char * expected;
  };
  const TraversalMapping mappings[] = {
    {SoTransparencyType::SCREEN_DOOR, "strategy=SCREEN_DOOR -> object"},
    {SoTransparencyType::BLEND, "strategy=BLEND -> object"},
    {SoTransparencyType::DELAYED_BLEND, "strategy=DELAYED_BLEND -> object"},
    {SoTransparencyType::SORTED_OBJECT_BLEND,
     "strategy=SORTED_OBJECT_BLEND -> object"},
    {SoTransparencyType::SORTED_OBJECT_SORTED_TRIANGLE_BLEND,
     "strategy=SORTED_OBJECT_SORTED_TRIANGLE_BLEND -> object"}
  };

  for (const TraversalMapping & mapping : mappings) {
    SoSeparator * root = new SoSeparator;
    root->ref();
    SoTransparencyType * mode = new SoTransparencyType;
    mode->value = mapping.mode;
    root->addChild(mode);
    SoMaterial * material = new SoMaterial;
    material->transparency.setValue(0.5f);
    root->addChild(material);
    root->addChild(new SoCube);

    CoinRenderAction action(SbViewportRegion(64, 64));
    action.apply(root);
    const std::string log(action.getRecordingLog().getString());
    const bool ok = check(action.getLastStatus() == CoinRenderAction::SUCCESS,
                          "SoTransparencyType traversal capture") &&
                    check(log.find(mapping.expected) != std::string::npos,
                          "recorded Coin transparency strategy");
    if (!ok) {
      std::cerr << action.getLastError().getString() << "\n" << log;
      root->unref();
      return false;
    }
    root->unref();
  }

  SoSeparator * root = new SoSeparator;
  root->ref();
  SoTransparencyType * localMode = new SoTransparencyType;
  localMode->value = SoTransparencyType::BLEND;
  root->addChild(localMode);
  SoMaterial * material = new SoMaterial;
  material->transparency.setValue(0.5f);
  root->addChild(material);
  root->addChild(new SoCube);

  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setTransparencyType(CoinRenderAction::SORTED_LAYERS_BLEND);
  action.apply(root);
  const std::string log(action.getRecordingLog().getString());
  const bool sortedLayers = check(
      action.getTransparencyType() == CoinRenderAction::SORTED_LAYERS_BLEND,
      "global transparency getter") &&
    check(action.getLastStatus() == CoinRenderAction::SUCCESS,
          "global SORTED_LAYERS_BLEND traversal capture") &&
    check(log.find("strategy=SORTED_LAYERS_BLEND -> sorted_layers") !=
            std::string::npos,
          "global SORTED_LAYERS_BLEND must override SoTransparencyType");
  if (!sortedLayers) std::cerr << action.getLastError().getString() << "\n" << log;
  root->unref();
  if (!sortedLayers) return false;

  return true;
}

} // namespace

int main(int argc, char ** argv) {
  SoDB::init();
  CoinRenderAction::initClass();
  if (argc == 2 && std::string(argv[1]) == "--annotations") {
    if (!CoinRenderAction::isGpuBackendAvailable()) return 77;
    if (!testAnnotationDepthClearViewport()) return 1;
    std::cout << "Wgpu annotation GPU regressions passed\n";
    return 0;
  }
  if (!testProjectiveDepth() || !testMaterialBlend() || !testStableDepthTie() ||
      !testOverlayPreservesTraversalOrder() ||
      !testMixedOverlayRendersInTraversalOrder() || !testTextureAlpha() ||
      !testTextureMutationAndSharing() ||
      !testAlphaAndOcclusion() || !testTargetIsolationAndResize() ||
      !testMultipleViewports() ||
      !testOverlayDepthBarriersWithSortedBase() ||
      !testFailurePreservesFrame() ||
      !testUnsupportedExecutorPreservesFrame() || !testCommonTransparencyPolicy() || !testRejections() || !testSceneTextureRejected() ||
      !testTraversal()) return 1;
  std::cout << "CoinRenderCompositionTest passed\n";
  return 0;
}
