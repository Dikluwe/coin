#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include "rendering/coinrender/CoinRenderTransformCore.h"

#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <Inventor/SbRotation.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <iostream>
#include <string>

namespace {
bool check(bool condition, const char * message)
{
  if (!condition) std::cerr << "CoinWgpuFfiFrameTest: " << message << '\n';
  return condition;
}

CoinRenderFramePlan opaqueFrame(bool shared)
{
  CoinRenderFramePlan frame;
  frame.revision = 501;
  frame.materials.resize(2);
  frame.materials[1].diffuse[0] = 0.25f;
  frame.vertices.resize((shared ? 2 : 256) * 3);
  frame.renderStates.resize(256);
  frame.draws.resize(256);
  for (uint32_t i = 0; i < 256; ++i) {
    frame.renderStates[i].model = SbMatrix(
      2,0,0,0, 0,3,0,0, 0,0,1,0, float(i),0,0,1);
    frame.renderStates[i].materialSlot = i % 2;
    frame.renderStates[i].transparencyType = SoGLRenderAction::NONE;
    auto & draw = frame.draws[i];
    const uint32_t base = (shared ? i % 2 : i) * 3;
    draw.geometry.firstVertex = draw.geometry.firstIndex = base;
    draw.geometry.vertexCount = draw.geometry.indexCount = 3;
    draw.renderStateSlot = i;
    for (uint32_t j = 0; j < 3; ++j) {
      auto & vertex = frame.vertices[base + j];
      vertex.position[0] = j == 0 ? 1.0f : 0.0f;
      vertex.position[1] = j == 1 ? 1.0f : 0.0f;
      vertex.position[2] = -2;
      vertex.normal[0] = 1;
      vertex.materialSlot = i % 2;
      vertex.screenSpaceW = 1;
      vertex.fogEyeDepth = -1;
      if (!shared || i < 2) frame.indices.push_back(base + j);
    }
  }
  return frame;
}

bool opaqueBatching(bool shared)
{
  auto frame = opaqueFrame(shared);
  CoinWgpuFfiFrame packed;
  std::string error;
  if (!check(packed.prepare(frame,64,64,error), "large opaque packing") ||
      !check(packed.getView().draw_count == 1 && packed.getView().state_count == 1,
        "compatible native ranges must form one batch") ||
      !check(packed.getView().draws[0].index_count == 768 &&
             packed.getView().vertices[0].position[0] == 2.0f &&
             packed.getView().vertices[0].normal[0] == 0.5f &&
             packed.getView().vertices[3].material_slot == 1,
        "batch must retain every index, transformed position/normal and per-vertex material") ||
      !check(frame.vertices[0].position[0] == 1.0f,
        "baking must not modify the Coin-owned frame")) return false;
  const auto & batch = packed.getView();
  if (!check(batch.vertex_count == 768 && batch.index_count == 768,
             "shared source ranges must expand to distinct transformed occurrences")) return false;
  for (uint32_t i = 0; i < 256; ++i) {
    for (uint32_t j = 0; j < 3; ++j) {
      const auto & vertex = batch.vertices[i * 3 + j];
      if (!check(vertex.position[0] == float(i) + (j == 0 ? 2.0f : 0.0f) &&
                 vertex.position[1] == (j == 1 ? 3.0f : 0.0f) &&
                 vertex.position[2] == -2.0f && vertex.normal[0] == .5f &&
                 vertex.material_slot == i % 2 && batch.indices[i * 3 + j] == i * 3 + j,
                 "every occurrence must retain its transform, normal, material and remapped indices")) return false;
    }
  }
  ++frame.revision;
  for (auto & state : frame.renderStates) state.view.setTranslate(SbVec3f(.25f,0,0));
  if (!check(packed.prepare(frame,64,64,
        CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,501),error),
        "batched camera patch") ||
      !check(packed.lastPrepareKind() == CoinRenderFrameReuseKind::CAMERA_PATCH &&
        packed.getView().camera_base_revision == 501 &&
        packed.getView().vertices[0].position[0] == 2.0f &&
        packed.getView().states[0].model_view[12] == .25f,
        "moving the camera must preserve baked geometry and transform it from its anchor")) return false;
  frame.vertices[0].position[0] = 10;
  if (!check(packed.prepare(frame,64,64,error) &&
        packed.getView().vertices[0].position[0] == 2.0f,
        "an immutable packed revision must retain its baked storage")) return false;
  frame.vertices[0].position[0] = 1;
  ++frame.revision;
  frame.renderStates.back().depthWrite = false;
  if (!check(packed.prepare(frame,64,64,error) && packed.getView().draw_count == 256 &&
        packed.getView().vertices[0].position[0] == 1,
        "a late incompatible depth state must leave all geometry unbaked")) return false;
  ++frame.revision;
  frame.renderStates.back().depthWrite = true;
  frame.renderStates.back().model[0][3] = .01f;
  if (!check(packed.prepare(frame,64,64,error) && packed.getView().draw_count == 256,
        "projective model transforms must retain ordinary draws")) return false;
  return true;
}

class EarlyBatchSwitch {
public:
  EarlyBatchSwitch() {
    const char * value = std::getenv(name());
    wasSet = value != nullptr;
    if (value) previous = value;
  }
  ~EarlyBatchSwitch() { set(wasSet ? previous.c_str() : nullptr); }
  void disable(bool disabled) { set(disabled ? "1" : nullptr); }
private:
  static const char * name() { return "COIN_WGPU_DISABLE_EARLY_OPAQUE_BATCHING"; }
  static void set(const char * value) {
#ifdef _WIN32
    _putenv_s(name(), value ? value : "");
#else
    if (value) setenv(name(),value,1); else unsetenv(name());
#endif
  }
  bool wasSet;
  std::string previous;
};

class CameraPatchSwitch {
public:
  CameraPatchSwitch() {
    const char * value = std::getenv(name());
    wasSet = value != nullptr;
    if (value) previous = value;
    disable(false);
  }
  ~CameraPatchSwitch() { set(wasSet ? previous.c_str() : nullptr); }
  void disable(bool disabled) { set(disabled ? "1" : nullptr); }
private:
  static const char * name() { return "COIN_WGPU_DISABLE_OPAQUE_CAMERA_PATCH"; }
  static void set(const char * value) {
#ifdef _WIN32
    _putenv_s(name(), value ? value : "");
#else
    if (value) setenv(name(), value, 1); else unsetenv(name());
#endif
  }
  bool wasSet;
  std::string previous;
};

class IncrementalBatchSwitch {
public:
  IncrementalBatchSwitch() {
    const char * value = std::getenv(name());
    wasSet = value != nullptr;
    if (value) previous = value;
    disable(false);
  }
  ~IncrementalBatchSwitch() { set(wasSet ? previous.c_str() : nullptr); }
  void disable(bool disabled) { set(disabled ? "1" : nullptr); }
private:
  static const char * name() { return "COIN_WGPU_DISABLE_INCREMENTAL_OPAQUE_BATCH"; }
  static void set(const char * value) {
#ifdef _WIN32
    _putenv_s(name(), value ? value : "");
#else
    if (value) setenv(name(), value, 1); else unsetenv(name());
#endif
  }
  bool wasSet;
  std::string previous;
};

class InstancingSwitch {
public:
  InstancingSwitch() {
    const char * value = std::getenv(name());
    wasSet = value != nullptr;
    if (value) previous = value;
  }
  ~InstancingSwitch() { set(wasSet ? previous.c_str() : nullptr); }
  void disable(bool disabled) { set(disabled ? "1" : nullptr); }
private:
  static const char * name() { return "COIN_WGPU_DISABLE_OPAQUE_INSTANCING"; }
  static void set(const char * value) {
#ifdef _WIN32
    _putenv_s(name(), value ? value : "");
#else
    if (value) setenv(name(), value, 1); else unsetenv(name());
#endif
  }
  bool wasSet;
  std::string previous;
};

bool anchoredPhongCamera()
{
  CameraPatchSwitch option;
  auto frame = opaqueFrame(true);
  frame.lightingStates.resize(1);
  CoinRenderLightSourceSnapshot light;
  light.type = CoinRenderLightType::POINT;
  light.position[0] = 4; light.position[2] = 7;
  frame.lightingStates[0].lights.push_back(light);
  SbMatrix anchor;
  anchor.setRotate(SbRotation(SbVec3f(0, 1, 0), .31f));
  anchor[3][0] = 3; anchor[3][2] = -4;
  for (auto & state : frame.renderStates) {
    state.lightModel = CoinRenderLightModel::PHONG;
    state.view = anchor;
  }
  CoinWgpuFfiFrame fast;
  std::string error;
  if (!check(fast.prepare(frame,64,64,error), "PHONG camera base")) return false;
  const auto * vertices = fast.getView().vertices;
  const auto * indices = fast.getView().indices;
  const auto * materials = fast.getView().materials;
  const std::vector<CoinWgpuVertex> immutable(vertices, vertices + fast.getView().vertex_count);
  for (unsigned step = 0; step < 3; ++step) {
    const uint64_t base = frame.revision++;
    SbMatrix next;
    next.setRotate(SbRotation(SbVec3f(0,1,0), .43f - .19f * step));
    next[3][0] = -1.25f + step; next[3][2] = -3;
    for (auto & state : frame.renderStates) state.view = next;
    frame.lightingStates[0].lights[0].position[0] = 5 + step;
    CoinWgpuFfiFrame full;
    option.disable(true);
    if (!check(full.prepare(frame,64,64,error), "full PHONG rebake reference")) return false;
    option.disable(false);
    if (!check(fast.prepare(frame,64,64,
          CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,base),error),
          "PHONG anchored patch") ||
        !check(fast.lastPrepareKind() == CoinRenderFrameReuseKind::CAMERA_PATCH &&
          fast.getView().camera_base_revision == base && fast.getView().vertices == vertices &&
          fast.getView().indices == indices && fast.getView().materials == materials &&
          std::memcmp(vertices,immutable.data(),immutable.size()*sizeof(CoinWgpuVertex)) == 0,
          "each camera must preserve reference geometry and material storage") ||
        !check(fast.getView().states[0].lights[0].position_type[0] == 5 + step,
          "PHONG patch must publish the newly captured view-space light")) return false;
    SbMatrix delta, normal;
    delta.setValue(fast.getView().states[0].model_view);
    normal.setValue(fast.getView().states[0].normal_matrix);
    for (size_t v = 0; v < immutable.size(); ++v) {
      SbVec3f position, direction;
      delta.multVecMatrix(SbVec3f(vertices[v].position),position);
      normal.multDirMatrix(SbVec3f(vertices[v].normal),direction);
      for (int c = 0; c < 3; ++c) {
        if (!check(std::abs(position[c]-full.getView().vertices[v].position[c]) <= 1.0e-4f &&
                   std::abs(direction[c]-full.getView().vertices[v].normal[c]) <= 1.0e-5f,
                   "anchor-relative position/normal must match full rebake without cumulative drift")) return false;
      }
    }
  }
  const uint64_t base = frame.revision++;
  option.disable(true);
  if (!check(fast.prepare(frame,64,64,
        CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,base),error) &&
        fast.lastPrepareKind() == CoinRenderFrameReuseKind::FULL_REBUILD &&
        fast.getView().camera_base_revision == 0, "camera optout must rebake the batch")) return false;
  option.disable(false);
  auto singular = opaqueFrame(true);
  for (auto & state : singular.renderStates) state.model[0][0] = 0;
  CoinWgpuFfiFrame guarded;
  if (!check(guarded.prepare(singular,64,64,error), "singular normal baseline")) return false;
  const uint64_t singularBase = singular.revision++;
  for (auto & state : singular.renderStates) state.view.setRotate(SbRotation(SbVec3f(0,1,0), .3f));
  if (!check(guarded.prepare(singular,64,64,
        CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,singularBase),error) &&
        guarded.lastPrepareKind() == CoinRenderFrameReuseKind::FULL_REBUILD &&
        guarded.getView().camera_base_revision == 0,
        "singular normal identity fallback must remain on full rebake")) return false;

  auto distant = opaqueFrame(true);
  for (auto & state : distant.renderStates) state.view.setTranslate(SbVec3f(1.0e8f,0,0));
  CoinWgpuFfiFrame distanceGuard;
  if (!check(distanceGuard.prepare(distant,64,64,error), "distant camera baseline")) return false;
  const uint64_t distantBase = distant.revision++;
  for (auto & state : distant.renderStates) state.view = SbMatrix::identity();
  if (!check(distanceGuard.prepare(distant,64,64,
        CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,distantBase),error) &&
        distanceGuard.lastPrepareKind() == CoinRenderFrameReuseKind::FULL_REBUILD &&
        distanceGuard.getView().camera_base_revision == 0 &&
        distanceGuard.getView().vertices[0].position[0] == 2.0f,
        "a distant anchor must rebake near geometry instead of preserving lost float detail")) return false;
  return true;
}

bool samePacked(const CoinWgpuFrameView & a, const CoinWgpuFrameView & b) {
  if (a.vertex_count != b.vertex_count || a.index_count != b.index_count ||
      a.state_count != b.state_count || a.draw_count != b.draw_count ||
      a.material_count != b.material_count || a.texture_count != b.texture_count ||
      a.sampler_count != b.sampler_count || a.frame_revision != b.frame_revision ||
      a.camera_base_revision != b.camera_base_revision || a.width != b.width || a.height != b.height ||
      a.instance_count != b.instance_count || a.instance_range_count != b.instance_range_count)
    return false;
  return std::memcmp(a.vertices,b.vertices,a.vertex_count*sizeof(CoinWgpuVertex)) == 0 &&
    std::memcmp(a.indices,b.indices,a.index_count*sizeof(uint32_t)) == 0 &&
    std::memcmp(a.states,b.states,a.state_count*sizeof(CoinWgpuRenderState)) == 0 &&
    std::memcmp(a.draws,b.draws,a.draw_count*sizeof(CoinWgpuDraw)) == 0 &&
    std::memcmp(a.materials,b.materials,a.material_count*sizeof(CoinWgpuMaterial)) == 0 &&
    (a.instance_count == 0 || std::memcmp(a.instances,b.instances,a.instance_count*sizeof(CoinWgpuInstance)) == 0) &&
    (a.instance_range_count == 0 || std::memcmp(a.instance_ranges,b.instance_ranges,
      a.instance_range_count*sizeof(CoinWgpuInstanceRange)) == 0);
}

bool earlyBatchEquivalence(bool shared) {
  auto frame = opaqueFrame(shared);
  frame.revision = 700;
  frame.viewports.resize(1);
  frame.viewports[0].x = 5; frame.viewports[0].y = 9;
  frame.viewports[0].width = 37; frame.viewports[0].height = 41;
  frame.lightingStates.resize(1);
  frame.lightingStates[0].ambientIntensity = .3f;
  CoinRenderLightSourceSnapshot light;
  light.direction[2] = -1; light.intensity = .75f; light.sourceRevision = 1;
  frame.lightingStates[0].lights.push_back(light);
  SbMatrix view;
  view.setRotate(SbRotation(SbVec3f(0,1,0),.37f));
  view[3][0] = 2.5f; view[3][2] = -7.3f;
  for (auto & state : frame.renderStates) state.view = view;
  CoinWgpuFfiFrame early, original;
  EarlyBatchSwitch option;
  // Reuse an owner that previously transported textures/samplers. The empty
  // resource tables of an early opaque batch must replace that old payload.
  auto primer = frame; primer.revision = 699; primer.draws.resize(1);
  primer.textures.resize(1); primer.textures[0].width = primer.textures[0].height = 1;
  primer.textures[0].pixelsRgba = {1,2,3,255}; primer.samplers.resize(1);
  std::string a, b;
  if (!check(early.prepare(primer,64,64,a) && original.prepare(primer,64,64,b),
             "texture/sampler primer packing")) return false;
  const auto compare = [&](uint64_t expectedDraws) {
    option.disable(false); const bool okA = early.prepare(frame,64,64,a);
    option.disable(true); const bool okB = original.prepare(frame,64,64,b);
    return check(okA && okB && samePacked(early.getView(),original.getView()),
                 "early and original packing must be byte-identical") &&
      check(early.getView().draw_count == expectedDraws &&
            !early.getView().texture_count && !early.getView().sampler_count,
            "qualified grouping, fallback and cleared resource tables");
  };
  if (!compare(1)) return false;
  ++frame.revision; frame.renderStates.back().fogMode = CoinRenderFogMode::HAZE;
  if (!compare(256)) return false;
  ++frame.revision; frame.renderStates.back().fogMode = CoinRenderFogMode::NONE;
  frame.renderStates.back().depthWrite = false;
  if (!compare(256)) return false;
  ++frame.revision; frame.renderStates.back().depthWrite = true;
  frame.renderStates.back().model[0][3] = .01f;
  if (!compare(256)) return false;
  ++frame.revision; frame.renderStates.back().model[0][3] = 0;
  frame.renderStates.push_back(frame.renderStates.back());
  frame.renderStates.back().fogMode = CoinRenderFogMode::HAZE;
  if (!compare(1)) return false; // Different unused state selects the original path.
  ++frame.revision;
  frame.lightingStates.push_back(frame.lightingStates[0]);
  frame.lightingStates.back().lights.resize(COIN_WGPU_FFI_MAX_LIGHTS+1);
  frame.renderStates.back().lightingSlot = 1;
  option.disable(false); const bool okA = early.prepare(frame,64,64,a);
  option.disable(true); const bool okB = original.prepare(frame,64,64,b);
  return check(!okA && !okB && a == b && a.find("More than eight active lights") != std::string::npos,
               "invalid unused state must preserve rejection and diagnostic");
}

bool incrementalBatchEquivalence(bool shared)
{
  auto frame = opaqueFrame(shared);
  frame.revision = 900;
  frame.viewports.resize(1);
  frame.viewports[0].width = frame.viewports[0].height = 64;
  frame.lightingStates.resize(1);
  CoinRenderLightSourceSnapshot light;
  light.direction[2] = -1; light.intensity = .75f;
  frame.lightingStates[0].lights.push_back(light);
  SbMatrix view;
  view.setRotate(SbRotation(SbVec3f(0,1,0), .3f));
  view[3][0] = 2.5f; view[3][2] = -7.0f;
  for (auto & state : frame.renderStates) state.view = view;
  CoinWgpuFfiFrame fast, original;
  EarlyBatchSwitch early;
  IncrementalBatchSwitch incremental;
  CameraPatchSwitch camera;
  std::string a, b;
  uint32_t width = 64, height = 64;
  CoinRenderFrameReuseDecision reuse(CoinRenderFrameReuseKind::FULL_REBUILD, 0);
  const auto compare = [&](bool expectedIncremental, size_t expectedRanges, size_t expectedVertices) {
    early.disable(false); incremental.disable(false);
    const bool okA = fast.prepare(frame,width,height,reuse,a);
    // The oracle retains the original pack-all-states-then-bake mechanism.
    early.disable(true); incremental.disable(true);
    const bool okB = original.prepare(frame,width,height,reuse,b);
    early.disable(false); incremental.disable(false);
    return check(okA && okB && samePacked(fast.getView(),original.getView()),
                 "incremental and original packing must be byte-identical") &&
      check(fast.incrementalOpaqueLastPrepare() == expectedIncremental &&
            fast.opaqueRangesRebakedLastPrepare() == expectedRanges &&
            fast.opaqueVerticesRebakedLastPrepare() == expectedVertices,
            "only changed opaque occurrences may be rebaked after exact qualification");
  };
  if (!compare(false,256,768)) return false;
  const auto * vertices = fast.getView().vertices;
  const auto * indices = fast.getView().indices;
  // About 10% move, followed by 100%. Both preserve all unrelated bytes.
  ++frame.revision;
  for (size_t i = 0; i < 26; ++i) frame.renderStates[i].model[3][0] += .5f;
  if (!compare(true,26,78) || !check(fast.getView().vertices == vertices &&
      fast.getView().indices == indices, "moving objects must preserve the expanded storage arena")) return false;
  ++frame.revision;
  for (auto & state : frame.renderStates) state.model[3][1] += .25f;
  if (!compare(true,256,768)) return false;
  ++frame.revision;
  if (!compare(true,0,0)) return false;
  // The optout must execute a complete bake in the same owner.
  ++frame.revision;
  incremental.disable(true);
  if (!check(fast.prepare(frame,width,height,a) && !fast.incrementalOpaqueLastPrepare() &&
             fast.opaqueRangesRebakedLastPrepare() == 256, "incremental optout must force a full bake")) return false;
  incremental.disable(false);
  ++frame.revision;
  if (!compare(false,256,768)) return false;
  // Shared geometry, index content, material tables and material-state slots
  // are all content proofs, independent of any source/revision identifiers.
  ++frame.revision; frame.vertices[0].position[1] += .2f;
  if (!compare(false,256,768)) return false;
  ++frame.revision; std::swap(frame.indices[0],frame.indices[1]);
  if (!compare(false,256,768)) return false;
  ++frame.revision; frame.materials[0].diffuse[1] = .4f;
  if (!compare(false,256,768)) return false;
  ++frame.revision; frame.renderStates.back().materialSlot = 1 - frame.renderStates.back().materialSlot;
  if (!compare(false,256,768)) return false;
  ++frame.revision; std::swap(frame.draws[0],frame.draws[255]);
  if (!compare(false,256,768)) return false;
  ++frame.revision; frame.lightingStates[0].lights[0].intensity = .55f;
  if (!compare(false,256,768)) return false;
  ++frame.revision;
  for (auto & state : frame.renderStates) state.depthFunction = CoinRenderDepthFunction::LEQUAL;
  if (!compare(false,256,768)) return false;
  ++frame.revision; frame.renderStates.back().depthWrite = false;
  if (!compare(false,0,0)) return false;
  ++frame.revision; frame.renderStates.back().depthWrite = true;
  if (!compare(false,256,768)) return false;
  ++frame.revision; frame.viewports[0].x = 3;
  if (!compare(false,256,768)) return false;
  ++frame.revision; width = 128;
  if (!compare(false,256,768)) return false;
  ++frame.revision;
  for (auto & state : frame.renderStates) state.projectionCoin[0][0] = .8f;
  if (!compare(false,256,768)) return false;
  // Direct camera changes take a full bake; the next object frame can qualify
  // the new coordinate space and use its matrices without cumulative drift.
  ++frame.revision;
  for (auto & state : frame.renderStates) state.view[3][0] += .25f;
  if (!compare(false,256,768)) return false;
  ++frame.revision; frame.renderStates[2].model[3][0] += .1f;
  if (!compare(true,1,3)) return false;
  // Camera patch after an incremental object frame keeps anchored vertices.
  // The following object mutation must invalidate that anchor-space cache.
  const uint64_t cameraBase = frame.revision++;
  for (auto & state : frame.renderStates) state.view[3][0] += .25f;
  if (!check(fast.prepare(frame,width,height,
          CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,cameraBase),a) &&
        fast.getView().camera_base_revision == cameraBase &&
        !fast.incrementalOpaqueLastPrepare(), "camera after objects must retain its qualified camera patch")) return false;
  early.disable(true); incremental.disable(true);
  if (!check(original.prepare(frame,width,height,b), "full camera oracle")) return false;
  early.disable(false); incremental.disable(false);
  SbMatrix delta, normal;
  delta.setValue(fast.getView().states[0].model_view);
  normal.setValue(fast.getView().states[0].normal_matrix);
  for (size_t v = 0; v < fast.getView().vertex_count; ++v) {
    SbVec3f position, direction;
    delta.multVecMatrix(SbVec3f(fast.getView().vertices[v].position),position);
    normal.multDirMatrix(SbVec3f(fast.getView().vertices[v].normal),direction);
    for (int c = 0; c < 3; ++c)
      if (!check(std::abs(position[c]-original.getView().vertices[v].position[c]) <= 1.0e-4f &&
          std::abs(direction[c]-original.getView().vertices[v].normal[c]) <= 1.0e-5f,
          "camera after partial object bake must match the ordinary geometry reference")) return false;
  }
  ++frame.revision; frame.renderStates[3].model[3][0] += .5f;
  if (!compare(false,256,768)) return false;
  ++frame.revision; frame.renderStates[4].model[3][0] += .5f;
  if (!compare(true,1,3)) return false;
  // Singular models retain Core's identity-normal fallback; a subsequent
  // camera hint cannot incorrectly rotate those normals from an old anchor.
  ++frame.revision; frame.renderStates[5].model[0][0] = 0;
  if (!compare(true,1,3)) return false;
  const uint64_t singularBase = frame.revision++;
  for (auto & state : frame.renderStates) state.view[3][1] += .25f;
  reuse = CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,singularBase);
  if (!compare(false,256,768) || !check(!fast.getView().camera_base_revision,
       "singular normal fallback must reject an anchored camera patch")) return false;
  reuse = CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD,0);
  // Invalid unused states preserve the diagnostic and poison every old cache
  // proof. Retry the same failed revision after repairing the payload.
  ++frame.revision;
  frame.lightingStates.push_back(frame.lightingStates[0]);
  frame.lightingStates.back().lights.resize(COIN_WGPU_FFI_MAX_LIGHTS+1);
  frame.renderStates.push_back(frame.renderStates.front());
  frame.renderStates.back().lightingSlot = 1;
  early.disable(false); incremental.disable(false);
  const bool okA = fast.prepare(frame,width,height,a);
  early.disable(true); incremental.disable(true);
  const bool okB = original.prepare(frame,width,height,b);
  early.disable(false); incremental.disable(false);
  if (!check(!okA && !okB && a == b && a.find("More than eight active lights") != std::string::npos,
             "late invalid state must preserve rejection and diagnostic")) return false;
  frame.renderStates.pop_back(); frame.lightingStates.pop_back();
  if (!compare(false,256,768)) return false;
  ++frame.revision; frame.renderStates[6].model[3][0] += .5f;
  if (!compare(true,1,3)) return false;
  return true;
}

bool instancedMatchesBake(const CoinWgpuFrameView & fast, const CoinWgpuFrameView & full)
{
  if (!check(fast.instance_count > 0 && fast.instance_range_count == fast.draw_count &&
             fast.state_count == 1 && full.instance_count == 0 && full.draw_count == 1,
             "instanced comparison requires a compact payload and a full-bake oracle")) return false;
  SbMatrix commonView, commonNormal;
  commonView.setValue(fast.states[0].model_view);
  commonNormal.setValue(fast.states[0].normal_matrix);
  uint64_t nextInstance = 0, nextVertex = 0, nextIndex = 0;
  for (uint64_t g = 0; g < fast.instance_range_count; ++g) {
    const auto & range = fast.instance_ranges[g];
    if (!check(range.draw_index == g && range.first_instance == nextInstance && range.reserved == 0 &&
        range.first_instance + uint64_t(range.instance_count) <= fast.instance_count,
        "consecutive instance ranges must cover their occurrences exactly in traversal order")) return false;
    const auto & draw = fast.draws[g];
    for (uint64_t i = range.first_instance; i < uint64_t(range.first_instance) + range.instance_count; ++i) {
      const auto & instance = fast.instances[i];
      if (!check(instance.reserved[0] == 0 && instance.reserved[1] == 0 && instance.reserved[2] == 0 &&
          instance.material_slot < fast.material_count,
          "instance ABI padding and material binding must be valid")) return false;
      SbMatrix modelView, normal;
      modelView.setValue(instance.model_view); normal.setValue(instance.normal_matrix);
      for (uint32_t v = 0; v < draw.vertex_count; ++v) {
        const auto & source = fast.vertices[draw.first_vertex + v];
        const auto & expected = full.vertices[nextVertex + v];
        SbVec3f anchorPosition, anchorNormal, position, direction;
        modelView.multVecMatrix(SbVec3f(source.position),anchorPosition);
        normal.multDirMatrix(SbVec3f(source.normal),anchorNormal);
        commonView.multVecMatrix(anchorPosition,position);
        commonNormal.multDirMatrix(anchorNormal,direction);
        for (int c = 0; c < 3; ++c)
          if (!check(std::abs(position[c]-expected.position[c]) <= 1.0e-4f &&
              std::abs(direction[c]-expected.normal[c]) <= 1.0e-5f,
              "instance and camera-anchor transforms must match full baked positions/normals")) return false;
        if (!check(instance.material_slot == expected.material_slot && source.material_slot == 0 &&
            std::memcmp(source.texcoord,expected.texcoord,sizeof(source.texcoord)) == 0 &&
            std::memcmp(source.extra_texcoords,expected.extra_texcoords,sizeof(source.extra_texcoords)) == 0 &&
            source.screen_space_w == expected.screen_space_w &&
            source.fog_eye_depth_plus_one == expected.fog_eye_depth_plus_one,
            "canonical meshes must retain all source attributes and each instance's own material")) return false;
      }
      for (uint32_t j = 0; j < draw.index_count; ++j)
        if (!check(fast.indices[draw.first_index+j] - draw.first_vertex ==
            full.indices[nextIndex+j] - nextVertex,
            "instanced indices must preserve full-bake topology and occurrence order")) return false;
      nextVertex += draw.vertex_count; nextIndex += draw.index_count;
    }
    nextInstance += range.instance_count;
  }
  return check(nextInstance == fast.instance_count && nextVertex == full.vertex_count && nextIndex == full.index_count,
               "compact geometry must account for every full-bake primitive exactly once per occurrence");
}

bool opaqueInstancingEquivalence(bool shared)
{
  auto frame = opaqueFrame(shared);
  frame.revision = 1800;
  frame.lightingStates.resize(1);
  CoinRenderLightSourceSnapshot light;
  light.direction[2] = -1; light.intensity = .75f;
  frame.lightingStates[0].lights.push_back(light);
  SbMatrix view;
  view.setRotate(SbRotation(SbVec3f(0,1,0), .3f));
  view[3][0] = 2.5f; view[3][2] = -7.0f;
  for (auto & state : frame.renderStates) state.view = view;
  CoinWgpuFfiFrame fast, original;
  InstancingSwitch instancing;
  EarlyBatchSwitch early;
  CameraPatchSwitch camera;
  std::string a,b;
  uint32_t width = 64, height = 64;
  CoinRenderFrameReuseDecision reuse(CoinRenderFrameReuseKind::FULL_REBUILD,0);
  const auto compare = [&](bool expectedInstances) {
    instancing.disable(false); early.disable(false);
    const bool okA = fast.prepare(frame,width,height,reuse,a);
    instancing.disable(true); early.disable(true);
    const bool okB = original.prepare(frame,width,height,b);
    instancing.disable(false); early.disable(false);
    if (!check(okA && okB, "instanced/reference preparation")) return false;
    if (expectedInstances) return instancedMatchesBake(fast.getView(),original.getView());
    return check(!fast.getView().instance_count && !fast.getView().instance_range_count &&
                   samePacked(fast.getView(),original.getView()),
                   "unsupported instancing profiles must retain byte-identical ordinary packing");
  };
  if (!compare(true) || !check(fast.getView().vertex_count == 3 && fast.getView().index_count == 3 &&
      fast.getView().draw_count == 1 && fast.getView().instance_count == 256,
      "equal geometry with alternating uniform materials must canonicalize to one consecutive group")) return false;
  const auto * vertices = fast.getView().vertices;
  const auto * instances = fast.getView().instances;
  const auto * ranges = fast.getView().instance_ranges;
  std::vector<CoinWgpuInstance> baseInstances(instances,instances+256);
  if (!check(fast.prepare(frame,width,height,a) && fast.reusedLastPrepare() &&
      fast.getView().vertices == vertices && fast.getView().instances == instances &&
      fast.getView().instance_ranges == ranges,
      "exact revision reuse must retain canonical geometry and anchored instance storage")) return false;
  // GPU-only render-to-texture does not accept the new payload. Changing that
  // destination must revoke exact reuse even when the source revision matches.
  if (!check(fast.prepare(frame,width,height,a,false) && !fast.reusedLastPrepare() &&
      !fast.getView().instance_count && !fast.getView().instance_range_count,
      "a destination without instancing must rebuild a cached instanced revision")) return false;
  ++frame.revision;
  if (!compare(true)) return false;
  vertices = fast.getView().vertices; instances = fast.getView().instances; ranges = fast.getView().instance_ranges;
  baseInstances.assign(instances,instances+256);
  const uint64_t cameraBase = frame.revision++;
  for (auto & state : frame.renderStates) state.view[3][0] += .25f;
  reuse = CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,cameraBase);
  if (!compare(true) || !check(fast.getView().camera_base_revision == cameraBase &&
      fast.getView().vertices == vertices && fast.getView().instances == instances &&
      fast.getView().instance_ranges == ranges &&
      std::memcmp(instances,baseInstances.data(),256*sizeof(CoinWgpuInstance)) == 0,
      "camera patches must update the shared delta while retaining immutable anchored instance matrices")) return false;
  reuse = CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD,0);
  ++frame.revision; frame.renderStates[10].model[3][0] += .5f;
  if (!compare(true) || !check(!fast.getView().camera_base_revision,
       "objects immediately after an instanced camera patch must rebuild current-space matrices")) return false;
  const uint64_t postObjectCameraBase = frame.revision++;
  for (auto & state : frame.renderStates) state.view[3][1] += .25f;
  reuse = CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,postObjectCameraBase);
  if (!compare(true) || !check(fast.getView().camera_base_revision == postObjectCameraBase,
       "a rebuilt object frame must support the next qualified camera overlay")) return false;
  const uint64_t unsupportedBase = frame.revision++;
  for (auto & state : frame.renderStates) state.view[3][0] += .25f;
  if (!check(fast.prepare(frame,width,height,
      CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,unsupportedBase),a,nullptr,false) &&
      !fast.getView().instance_count && !fast.getView().camera_base_revision,
      "an unsupported destination must revoke the anchored instanced camera hint")) return false;
  reuse = CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD,0);
  ++frame.revision;
  for (size_t i = 0; i < 26; ++i) frame.renderStates[i].model[3][0] += .5f;
  if (!compare(true) || !check(fast.getView().camera_base_revision == 0,
       "object changes after camera patch must replace the anchor from current captured matrices")) return false;
  ++frame.revision; frame.materials[0].diffuse[1] = .45f;
  if (!compare(true)) return false;
  ++frame.revision;
  frame.materials.push_back(frame.materials[0]);
  frame.materials.back().diffuse[3] = .75f; frame.materials.back().transparency = .25f;
  if (!compare(false)) return false;
  ++frame.revision; frame.materials.pop_back();
  if (!compare(true)) return false;
  ++frame.revision; frame.lightingStates[0].lights[0].intensity = .35f;
  if (!compare(true)) return false;
  ++frame.revision; width = 128;
  if (!compare(true)) return false;
  ++frame.revision;
  for (auto & state : frame.renderStates) state.projectionCoin[0][0] = .85f;
  if (!compare(true)) return false;
  // A per-vertex material change cannot silently become a uniform instance.
  ++frame.revision; frame.vertices[0].materialSlot = 1;
  if (!compare(false)) return false;
  ++frame.revision; frame.vertices[0].materialSlot = 0;
  if (!compare(true)) return false;
  ++frame.revision; frame.vertices[0].texcoord[0] = std::numeric_limits<float>::quiet_NaN();
  if (!compare(false)) return false;
  ++frame.revision; frame.vertices[0].texcoord[0] = 0;
  if (!compare(true)) return false;
  ++frame.revision; frame.renderStates.back().depthWrite = false;
  if (!compare(false)) return false;
  ++frame.revision; frame.renderStates.back().depthWrite = true;
  if (!compare(true)) return false;
  ++frame.revision; frame.renderStates[5].model[0][0] = 0;
  if (!compare(false)) return false;
  ++frame.revision; frame.renderStates[5].model[0][0] = 2;
  if (!compare(true)) return false;
  ++frame.revision;
  frame.renderStates[0].view[3][0] += .25f;
  if (!compare(false)) return false;
  ++frame.revision;
  frame.renderStates[0].view[3][0] -= .25f;
  if (!compare(true)) return false;
  // Same owner, optout and camera hint: instance eligibility must be revoked.
  const uint64_t optoutBase = frame.revision++;
  for (auto & state : frame.renderStates) state.view[3][0] += .25f;
  instancing.disable(true);
  if (!check(fast.prepare(frame,width,height,
      CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,optoutBase),a) &&
      !fast.getView().instance_count && !fast.getView().camera_base_revision,
      "instancing optout must revoke an existing instanced camera proof")) return false;
  instancing.disable(false);
  ++frame.revision;
  if (!compare(true)) return false;
  // Failed unused states must preserve the ordinary error, then safely rebuild
  // the same repaired revision with no old camera/instance proof.
  ++frame.revision;
  frame.lightingStates.push_back(frame.lightingStates[0]);
  frame.lightingStates.back().lights.resize(COIN_WGPU_FFI_MAX_LIGHTS+1);
  frame.renderStates.push_back(frame.renderStates.front());
  frame.renderStates.back().lightingSlot = 1;
  instancing.disable(false); const bool okA = fast.prepare(frame,width,height,a);
  instancing.disable(true); const bool okB = original.prepare(frame,width,height,b);
  instancing.disable(false);
  if (!check(!okA && !okB && a == b && a.find("More than eight active lights") != std::string::npos,
       "instancing must preserve late unused-state rejection")) return false;
  frame.renderStates.pop_back(); frame.lightingStates.pop_back();
  if (!compare(true) || !check(!fast.reusedLastPrepare(), "repairing a failed revision must rebuild instances")) return false;
  return true;
}

bool consecutiveInstancingGroups()
{
  auto frame = opaqueFrame(true);
  const uint32_t extraVertex = static_cast<uint32_t>(frame.vertices.size());
  const uint32_t extraIndex = static_cast<uint32_t>(frame.indices.size());
  CoinRenderGeometryRange other;
  other.firstVertex = extraVertex; other.vertexCount = 3;
  other.firstIndex = extraIndex; other.indexCount = 3;
  CoinRenderGeometryRange base;
  base.firstVertex = base.firstIndex = 0; base.vertexCount = base.indexCount = 3;
  for (uint32_t v = 0; v < 3; ++v) {
    auto vertex = frame.vertices[v];
    vertex.position[2] -= .5f;
    frame.vertices.push_back(vertex);
    frame.indices.push_back(extraVertex + v);
  }
  for (size_t i = 128; i < 192; ++i) {
    frame.draws[i].geometry = other;
    frame.renderStates[i].materialSlot = 0;
  }
  InstancingSwitch instancing;
  EarlyBatchSwitch early;
  CoinWgpuFfiFrame fast, original;
  std::string a,b;
  instancing.disable(false);
  if (!check(fast.prepare(frame,64,64,a), "consecutive instanced groups")) return false;
  instancing.disable(true); early.disable(true);
  if (!check(original.prepare(frame,64,64,b), "consecutive groups full-bake oracle")) return false;
  instancing.disable(false); early.disable(false);
  if (!instancedMatchesBake(fast.getView(),original.getView()) ||
      !check(fast.getView().draw_count == 3 && fast.getView().vertex_count == 6 &&
        fast.getView().instance_ranges[0].first_instance == 0 && fast.getView().instance_ranges[0].instance_count == 128 &&
        fast.getView().instance_ranges[1].first_instance == 128 && fast.getView().instance_ranges[1].instance_count == 64 &&
        fast.getView().instance_ranges[2].first_instance == 192 && fast.getView().instance_ranges[2].instance_count == 64,
        "A/B/A groups must preserve traversal instead of gathering all A occurrences together")) return false;
  // Alternating two source meshes exceeds the submit bound and must return to
  // the ordinary batch. Only one new source mesh is needed to exercise it.
  ++frame.revision;
  for (size_t i = 0; i < 256; ++i) {
    frame.draws[i].geometry = i % 2 ? other : base;
    frame.renderStates[i].materialSlot = 0;
  }
  if (!check(fast.prepare(frame,64,64,a) && !fast.getView().instance_count &&
      !fast.getView().instance_range_count, "more than 128 consecutive mesh groups must fall back")) return false;
  instancing.disable(true); early.disable(true);
  if (!check(original.prepare(frame,64,64,b), "group limit full-bake oracle")) return false;
  return check(samePacked(fast.getView(),original.getView()), "group limit fallback must remain byte-identical");
}

bool boundedIncrementalBatch()
{
  auto frame = opaqueFrame(true);
  // Unreferenced geometry still belongs to the source-content proof. An input
  // larger than the 16 MiB cache budget must always take the full bake.
  frame.vertices.resize(17u * 1024u * 1024u / sizeof(CoinRenderVertexSnapshot));
  CoinWgpuFfiFrame packed;
  IncrementalBatchSwitch incremental;
  EarlyBatchSwitch early; early.disable(false);
  std::string diagnostic;
  if (!check(packed.prepare(frame,64,64,diagnostic), "large cache input base")) return false;
  ++frame.revision; frame.renderStates[0].model[3][0] += .5f;
  return check(packed.prepare(frame,64,64,diagnostic) && !packed.incrementalOpaqueLastPrepare() &&
               packed.opaqueRangesRebakedLastPrepare() == 256,
               "a source beyond the cache budget must retain the full-bake fallback");
}
}

int
main()
{
  InstancingSwitch instancing;
  instancing.disable(true); // These controls specifically exercise baked storage.
  if (!opaqueBatching(false) || !opaqueBatching(true) || !anchoredPhongCamera() ||
      !earlyBatchEquivalence(false) || !earlyBatchEquivalence(true) ||
      !incrementalBatchEquivalence(false) || !incrementalBatchEquivalence(true) ||
      !boundedIncrementalBatch()) return 1;
  instancing.disable(false);
  if (!opaqueInstancingEquivalence(false) || !opaqueInstancingEquivalence(true) ||
      !consecutiveInstancingGroups()) return 1;
  CoinRenderFramePlan frame;
  frame.revision = 41;
  frame.vertices.resize(1);
  frame.vertices[0].position[0] = 2.0f;
  frame.vertices[0].screenSpaceW = 2.0f;
  frame.vertices[0].fogEyeDepth = 4.0f;
  frame.indices.push_back(0);
  CoinRenderTextureImageSnapshot texture;
  texture.width = 1;
  texture.height = 1;
  texture.pixelsRgba.push_back(10);
  texture.pixelsRgba.push_back(20);
  texture.pixelsRgba.push_back(30);
  texture.pixelsRgba.push_back(255);
  frame.textures.push_back(texture);
  frame.lightingStates.push_back(CoinRenderLightingSnapshot{});
  frame.renderStates.push_back(CoinRenderRenderStateSnapshot{});
  frame.renderStates[0].polygonOffsetSlopeBias = -.01f;
  frame.renderStates[0].polygonOffsetMaxDepth = .75f;

  CoinWgpuFfiFrame packed;
  std::string diagnostic;
  if (!check(packed.prepare(frame, 64, 32, diagnostic), "initial packing failed") ||
      !check(!packed.reusedLastPrepare(), "initial packing reported reuse")) return 1;

  const CoinWgpuFrameView & first = packed.getView();
  if (!check(first.sorted_layers_passes == 4 && first.transparency_reserved == 0 &&
                 first.transparency_budget_bytes == uint64_t(256) * 1024 * 1024,
             "transparency options were not packed") ||
      !check(first.width == 64 && first.height == 32, "target dimensions were not packed") ||
      !check(first.vertices[0].screen_space_w == 2.0f &&
                 first.vertices[0].fog_eye_depth_plus_one == 5.0f,
             "homogeneous stroke attributes were not packed") ||
      !check(first.states[0].polygon_offset_slope_bias == -.01f,
             "original polygon slope bias was not packed") ||
      !check(first.states[0].polygon_offset_max_depth_bits == UINT32_C(0x3f400001),
             "original maximum depth bits were not packed") ||
      !check(first.indices != frame.indices.data(), "packed indices do not own their storage") ||
      !check(first.textures[0].pixels != frame.textures[0].pixelsRgba.data(),
             "packed texture does not own its storage"))
    return 1;

  frame.vertices[0].position[0] = 9.0f;
  frame.indices[0] = 7;
  frame.textures[0].pixelsRgba[0] = 99;
  if (!check(packed.prepare(frame, 128, 16, diagnostic), "cached packing failed") ||
      !check(packed.reusedLastPrepare(), "same non-zero revision was not reused")) return 1;
  const CoinWgpuFrameView & reused = packed.getView();
  if (!check(reused.width == 128 && reused.height == 16, "reuse did not update target dimensions") ||
      !check(reused.camera_base_revision == 0, "exact reuse must not claim a camera patch") ||
      !check(reused.vertices[0].position[0] == 2.0f, "reuse observed a mutated source frame") ||
      !check(reused.indices[0] == 0, "reuse observed mutated source indices") ||
      !check(reused.textures[0].pixels[0] == 10, "reuse observed mutated texture bytes")) return 1;

  frame.revision = 42;
  if (!check(packed.prepare(frame, 128, 16, diagnostic), "new revision packing failed") ||
      !check(!packed.reusedLastPrepare(), "new revision incorrectly reused packing") ||
      !check(packed.getView().vertices[0].position[0] == 9.0f, "new revision was not repacked")) return 1;

  frame.revision = 43;
  SbMatrix moved = SbMatrix::identity();
  moved.setTranslate(SbVec3f(0.5f, 0.0f, 0.0f));
  frame.renderStates[0].view = moved;
  const CoinRenderFrameReuseDecision cameraPatch(
    CoinRenderFrameReuseKind::CAMERA_PATCH, 42);
  if (!check(packed.prepare(frame, 128, 16, cameraPatch, diagnostic),
             "camera patch packing failed") ||
      !check(packed.lastPrepareKind() == CoinRenderFrameReuseKind::CAMERA_PATCH,
             "camera patch repacked immutable arrays") ||
      !check(packed.getView().camera_base_revision == 42,
             "camera patch did not name its packed base") ||
      !check(packed.getView().vertices[0].position[0] == 9.0f,
             "camera patch changed packed geometry")) return 1;

  frame.revision = 44;
  frame.vertices[0].position[0] = 11.0f;
  const CoinRenderFrameReuseDecision stalePatch(
    CoinRenderFrameReuseKind::CAMERA_PATCH, 41);
  if (!check(packed.prepare(frame, 128, 16, stalePatch, diagnostic),
             "stale camera patch fallback failed") ||
      !check(packed.lastPrepareKind() == CoinRenderFrameReuseKind::FULL_REBUILD,
             "stale camera base did not force full packing") ||
      !check(packed.getView().camera_base_revision == 0,
             "full fallback retained a camera patch hint") ||
      !check(packed.getView().vertices[0].position[0] == 11.0f,
             "stale camera fallback did not refresh geometry")) return 1;

  frame.revision = 0;
  if (!check(packed.prepare(frame, 128, 16, diagnostic), "zero revision packing failed") ||
      !check(!packed.reusedLastPrepare(), "zero revision was reused on first call") ||
      !check(packed.prepare(frame, 128, 16, diagnostic), "second zero revision packing failed") ||
      !check(!packed.reusedLastPrepare(), "zero revision was reused on second call")) return 1;

  CoinRenderFramePlan sorted;
  sorted.revision = 100;
  sorted.materials.resize(1);
  sorted.materials[0].diffuse[3] = 0.5f;
  sorted.materials[0].transparency = 0.5f;
  sorted.renderStates.resize(1);
  sorted.renderStates[0].transparencyType = SoGLRenderAction::SORTED_OBJECT_BLEND;
  sorted.vertices.resize(2);
  sorted.vertices[0].position[2] = -2.0f;
  sorted.vertices[1].position[2] = -4.0f;
  sorted.indices = {0,0,0,1,1,1};
  sorted.draws.resize(2);
  for (uint32_t i = 0; i < 2; ++i) {
    sorted.draws[i].geometry.firstIndex = i*3;
    sorted.draws[i].geometry.indexCount = 3;
    sorted.draws[i].drawOrdinal = i;
  }
  CoinWgpuFfiFrame resolved;
  if (!check(resolved.prepare(sorted, 64, 64, diagnostic), "resolved packing") ||
      !check(resolved.getView().draws[0].draw_ordinal == 1 &&
        resolved.getView().draws[0].composition_flags == 1, "resolved far-first order and blend") ||
      !check(resolved.getView().states[resolved.getView().draws[0].render_state_slot].depth_write == 0 &&
        resolved.getView().states[resolved.getView().draws[0].render_state_slot].depth_function == 3,
        "resolved transparent depth")) return 1;
  sorted.revision = 101;
  sorted.renderStates[0].view = SbMatrix(1,0,0,0, 0,1,0,0, 0,0,-1,0, 0,0,0,1);
  if (!check(resolved.prepare(sorted, 64, 64,
      CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH, 100), diagnostic), "sorted camera patch") ||
      !check(resolved.getView().draws[0].draw_ordinal == 0, "camera patch must recompute sorting") ||
      !check(resolved.getView().camera_base_revision == 0, "reordered patch cannot reuse device-owned order")) return 1;
  sorted.revision = 102;
  sorted.renderStates[0].transparencyType = SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND;
  if (!check(resolved.prepare(sorted,64,64,diagnostic) && resolved.getView().draw_count==2,
      "triangle schedule is transported without an OIT substitution"))return 1;
  sorted.revision=103;sorted.renderStates[0].transparencyType=-1;
  if(!check(!resolved.prepare(sorted,64,64,diagnostic),"unknown mode rejects packing"))return 1;
  sorted.revision = 101;
  sorted.renderStates[0].transparencyType = SoGLRenderAction::SORTED_OBJECT_BLEND;
  if (!check(resolved.prepare(sorted, 64, 64, diagnostic) && !resolved.reusedLastPrepare(),
      "failed pack must invalidate cached storage")) return 1;

  sorted.revision = 200;
  sorted.renderStates[0].transparencyType = SoGLRenderAction::SORTED_LAYERS_BLEND;
  sorted.transparency.bufferBudget = uint64_t(64) * 64 * (16 * 4 + 8);
  if (!check(resolved.prepare(sorted, 64, 64, diagnostic), "bounded initial peeling frame"))
    return 1;
  const auto oldWidth = resolved.getView().width;
  if (!check(!resolved.prepare(sorted, 65, 64, diagnostic),
             "reuse resize must recheck Core budget") ||
      !check(resolved.getView().width == oldWidth, "budget rejection must preserve packed view") ||
      !check(resolved.prepare(sorted, 64, 64, diagnostic), "reuse after rejected resize"))
    return 1;
  CoinRenderFramePlan shadow;
  shadow.revision = 201;
  shadow.vertices.resize(3);
  shadow.vertices[0].position[0] = -0.5f;
  shadow.vertices[0].position[1] = -0.5f;
  shadow.vertices[1].position[0] = 0.5f;
  shadow.vertices[1].position[1] = -0.5f;
  shadow.vertices[2].position[1] = 0.5f;
  for (auto & vertex : shadow.vertices) vertex.position[2] = -3.0f;
  shadow.indices = {0, 1, 2};
  shadow.materials.resize(1);
  shadow.lightingStates.resize(1);
  CoinRenderLightSourceSnapshot lit;
  lit.sourceRevision = 7;
  lit.type = CoinRenderLightType::SPOT;
  shadow.lightingStates[0].lights.push_back(lit);
  shadow.cameras.resize(1);
  shadow.viewports.resize(1);
  shadow.renderStates.resize(2);
  shadow.renderStates[0].shadowGroupSlot = 1;
  shadow.renderStates[0].shadowStyle = 1; // caster only
  shadow.renderStates[1] = shadow.renderStates[0];
  shadow.renderStates[1].shadowStyle = 2; // receiver only
  shadow.draws.resize(2);
  shadow.draws[0].geometry.vertexCount = 3;
  shadow.draws[0].geometry.indexCount = 3;
  shadow.draws[1] = shadow.draws[0];
  shadow.draws[1].renderStateSlot = 1;
  CoinRenderShadowGroupSnapshot group;
  group.sourceRevision = 5;
  shadow.shadowGroups.push_back(group);
  CoinRenderShadowLightSnapshot spot;
  spot.groupSlot = 1;
  spot.sourceRevision = 7;
  spot.type = CoinRenderLightType::SPOT;
  spot.enabled = true;
  spot.shadowEligible = true;
  shadow.shadowLights.push_back(spot);
  CoinWgpuFfiFrame shadowPacked;
  if (!check(shadowPacked.prepare(shadow, 64, 64, diagnostic),
             "spot caster transport failed") ||
      !check(shadowPacked.getShadowFrame().mapSize == 1024 &&
             shadowPacked.getShadowFrame().casters.size() == 1,
             "spot caster map or count changed") ||
      !check(shadowPacked.getShadowFrame().casters[0].first_index == 0 &&
             shadowPacked.getShadowFrame().casters[0].index_count == 3,
             "spot caster index range changed") ||
      !check(shadowPacked.getView().shadow_casters ==
               shadowPacked.getShadowFrame().casters.data() &&
             shadowPacked.getView().shadow_caster_count == 1 &&
             shadowPacked.getView().shadow_map_size == 1024 &&
             shadowPacked.getView().shadow_kind == 1,
             "spot caster ABI view was not bound") ||
      !check(shadowPacked.getView().shadow_receivers ==
               shadowPacked.getShadowFrame().receivers.data() &&
             shadowPacked.getView().shadow_receiver_count == 2 &&
             shadowPacked.getView().shadow_receivers[0].receives == 0 &&
             shadowPacked.getView().shadow_receivers[1].receives == 9u &&
             shadowPacked.getView().shadow_receivers[1].lighting_index == 0,
             "spot receiver state was not transported"))
    return 1;
  // Coin's float field is compared with double literals at these boundaries.
  for (const auto & sample : {std::pair<float,uint32_t>{0.2999f,11u},
                              {0.3f,9u}, {0.7f,9u}, {0.7001f,13u}}) {
    shadow.shadowGroups[0].quality = sample.first;
    ++shadow.revision;
    if (!check(shadowPacked.prepare(shadow,64,64,diagnostic) &&
               shadowPacked.getView().shadow_receivers[0].receives == 0u &&
               shadowPacked.getView().shadow_receivers[1].receives == sample.second,
               "Coin quality stages were not transported at the boundary"))
      return 1;
  }
  shadow.shadowGroups[0].quality = group.quality;
  ++shadow.revision;
  if (!shadowPacked.prepare(shadow,64,64,diagnostic)) return 1;
  const auto & caster = shadowPacked.getShadowFrame().casters[0];
  SbMatrix shadowMvp;
  shadowMvp.setValue(caster.model_view_projection);
  SbVec3f projected;
  shadowMvp.multVecMatrix(SbVec3f(0, 0, -3), projected);
  if (!check(std::abs(projected[0]) < 0.01f &&
             std::abs(projected[1]) < 0.01f &&
             projected[2] >= 0.0f && projected[2] <= 1.0f,
             "spot caster did not use WebGPU clip depth"))
    return 1;
  shadow.indices[0] = 99;
  shadow.revision = 202;
  if (!check(!shadowPacked.prepare(shadow, 64, 64, diagnostic),
             "out-of-range shadow caster index was accepted"))
    return 1;
  std::cout << "CoinWgpuFfiFrameTest passed\n";
  return 0;
}
