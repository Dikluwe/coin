#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include "rendering/coinrender/CoinRenderTransformCore.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "../coinrender/CoinRenderTestEnvironment.h"

#include <cstdint>
#include <cmath>
#include <cfenv>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <Inventor/SbRotation.h>
#include <Inventor/SbVec4f.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <iostream>
#include <string>

#if (defined(__GNUC__) || defined(__clang__)) && defined(__SSE_MATH__) && defined(__SSE2_MATH__) && \
    (defined(__x86_64__) || defined(__i386__))
#include <xmmintrin.h>
#define COIN_WGPU_MATRIX_CACHE_TEST_FP_X86 1
#endif

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

bool externalViewportPacking() {
  auto frame=opaqueFrame(false);
  CoinRenderViewportSnapshot viewport;
  viewport.x=-16;viewport.y=-8;viewport.width=64;viewport.height=48;
  frame.viewports.push_back(viewport);
  CoinWgpuFfiFrame packed;
  std::string error;
  for (int size : {64,80,64}) {
    ++frame.revision;
    if(!check(packed.prepare(frame,size,size,error),"external viewport packing"))return false;
    const auto & state=packed.getView().states[0];
    SbMatrix matrix;matrix.setValue(state.model_view_projection);
    SbVec3f clip;matrix.multVecMatrix(SbVec3f(0,0,0),clip);
    // Baked geometry uses projection alone. Reconstruct the original window
    // center (16,16) from the packed intersection, independently of Core math.
    const float x=state.viewport[0]+(clip[0]+1)*state.viewport[2]*.5f;
    const float y=size-state.viewport[1]-state.viewport[3]+(clip[1]+1)*state.viewport[3]*.5f;
    if(!check(packed.getView().draw_count==1 && std::abs(x-16)<1e-5f && std::abs(y-16)<1e-5f &&
              state.viewport[2]==48 && state.viewport[3]==40,"batched projection preserves external window coordinates after resize"))return false;
  }
  frame.viewports[0].x=80;++frame.revision;
  if(!check(packed.prepare(frame,64,64,error),"empty viewport packing"))return false;
  const auto & state=packed.getView().states[0];
  return check(state.viewport[0]==0 && state.viewport[1]==0 && state.viewport[2]==0 && state.viewport[3]==1,
               "empty viewport is distinct from full-target legacy default");
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

class InstanceCommonStateSwitch {
public:
  InstanceCommonStateSwitch() {
    const char * value = std::getenv(name());
    wasSet = value != nullptr;
    if (value) previous = value;
    disable(false);
  }
  ~InstanceCommonStateSwitch() { set(wasSet ? previous.c_str() : nullptr); }
  void disable(bool disabled) { set(disabled ? "1" : nullptr); }
private:
  static const char * name() { return "COIN_WGPU_DISABLE_INSTANCE_COMMON_STATE"; }
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

class InstanceMatrixCacheSwitch {
public:
  InstanceMatrixCacheSwitch() {
    const char * value = std::getenv(name());
    wasSet = value != nullptr;
    if (value) previous = value;
    disable(false);
  }
  ~InstanceMatrixCacheSwitch() { set(wasSet ? previous.c_str() : nullptr); }
  void disable(bool disabled) { set(disabled ? "1" : nullptr); }
private:
  static const char * name() { return "COIN_WGPU_DISABLE_INSTANCE_MATRIX_CACHE"; }
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

// Retain controls and sticky flags independently for the two owner oracles.
// The test never executes arithmetic with an unmasked trap: those checks use
// only the pre-existing immutable-revision path, then restore the environment.
class MatrixFpSnapshot {
public:
  MatrixFpSnapshot() {
    std::fegetenv(&environment);
#ifdef COIN_WGPU_MATRIX_CACHE_TEST_FP_X86
    sse = _mm_getcsr();
#endif
  }
  ~MatrixFpSnapshot() { restore(); }
  void restore() const {
    std::fesetenv(&environment);
#ifdef COIN_WGPU_MATRIX_CACHE_TEST_FP_X86
    _mm_setcsr(sse);
#endif
  }
private:
  std::fenv_t environment;
#ifdef COIN_WGPU_MATRIX_CACHE_TEST_FP_X86
  uint32_t sse;
#endif
};

uint32_t matrixExceptionStatus() {
#ifdef COIN_WGPU_MATRIX_CACHE_TEST_FP_X86
  uint16_t x87;
  __asm__ __volatile__("fnstsw %0" : "=am" (x87));
  return (_mm_getcsr() & 0x3fu) | (uint32_t(x87 & 0x3fu) << 6);
#else
  return static_cast<uint32_t>(std::fetestexcept(FE_ALL_EXCEPT));
#endif
}

class DiagonalMeshSwitch {
public:
  DiagonalMeshSwitch() {
    const char * value = std::getenv(name());
    wasSet = value != nullptr;
    if (value) previous = value;
    disable(false);
  }
  ~DiagonalMeshSwitch() { set(wasSet ? previous.c_str() : nullptr); }
  void disable(bool disabled) { set(disabled ? "1" : nullptr); }
private:
  static const char * name() { return "COIN_WGPU_DISABLE_DIAGONAL_MESH_LOWERING"; }
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

bool sameOpaqueHeader(const CoinWgpuFrameView & a, const CoinWgpuFrameView & b) {
  const uint32_t flagsA[] = {a.abi_version, a.struct_size, a.sorted_layers_passes, a.transparency_reserved,
    a.shadow_map_size, a.shadow_kind, a.shadow_map_size_second, a.shadow_kind_second,
    a.shadow_map_size_third, a.shadow_kind_third, a.shadow_map_size_fourth, a.shadow_kind_fourth};
  const uint32_t flagsB[] = {b.abi_version, b.struct_size, b.sorted_layers_passes, b.transparency_reserved,
    b.shadow_map_size, b.shadow_kind, b.shadow_map_size_second, b.shadow_kind_second,
    b.shadow_map_size_third, b.shadow_kind_third, b.shadow_map_size_fourth, b.shadow_kind_fourth};
  const float shadowA[] = {a.shadow_near_distance, a.shadow_far_distance, a.shadow_epsilon, a.shadow_threshold,
    a.shadow_near_distance_second, a.shadow_far_distance_second, a.shadow_epsilon_second, a.shadow_threshold_second,
    a.shadow_near_distance_third, a.shadow_far_distance_third, a.shadow_epsilon_third, a.shadow_threshold_third,
    a.shadow_near_distance_fourth, a.shadow_far_distance_fourth, a.shadow_epsilon_fourth, a.shadow_threshold_fourth};
  const float shadowB[] = {b.shadow_near_distance, b.shadow_far_distance, b.shadow_epsilon, b.shadow_threshold,
    b.shadow_near_distance_second, b.shadow_far_distance_second, b.shadow_epsilon_second, b.shadow_threshold_second,
    b.shadow_near_distance_third, b.shadow_far_distance_third, b.shadow_epsilon_third, b.shadow_threshold_third,
    b.shadow_near_distance_fourth, b.shadow_far_distance_fourth, b.shadow_epsilon_fourth, b.shadow_threshold_fourth};
  const auto noResources = [](const CoinWgpuFrameView & view) {
    return !view.texture_count && !view.textures && !view.sampler_count && !view.samplers &&
      !view.shadow_caster_count && !view.shadow_casters && !view.shadow_receiver_count && !view.shadow_receivers &&
      !view.shadow_caster_count_second && !view.shadow_casters_second &&
      !view.shadow_receiver_count_second && !view.shadow_receivers_second &&
      !view.shadow_caster_count_third && !view.shadow_casters_third &&
      !view.shadow_receiver_count_third && !view.shadow_receivers_third &&
      !view.shadow_caster_count_fourth && !view.shadow_casters_fourth &&
      !view.shadow_receiver_count_fourth && !view.shadow_receivers_fourth &&
      !view.extra_shadow_pass_count && !view.extra_shadow_passes;
  };
  return std::memcmp(flagsA, flagsB, sizeof(flagsA)) == 0 && std::memcmp(shadowA, shadowB, sizeof(shadowA)) == 0 &&
    std::memcmp(a.clear_color, b.clear_color, sizeof(a.clear_color)) == 0 &&
    a.transparency_budget_bytes == b.transparency_budget_bytes && noResources(a) && noResources(b);
}

class BorrowPackingBackend : public CoinRenderBackend {
public:
  bool ok = true, allowInstancing = true;
  unsigned submissions = 0, instancedSubmissions = 0;
  bool isGpuBackend() const override { return false; }
  CoinRenderBackendStatus getStatus() const override { return CoinRenderBackendStatus::SUCCESS; }
  CoinRenderBackendStatus prepare(CoinRenderTargetP &) override { return CoinRenderBackendStatus::SUCCESS; }
  void poll() override {}
  const std::string & getLastError() const override { return error; }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target) override {
    ++submissions;
    const auto * receipt = target.submissionPreflight(frame);
    ok &= check(receipt && receipt->opaqueCompositionFor(frame), "packing oracle requires a current opaque Target receipt");
    CoinWgpuFfiFrame fast, literal;
    std::string fastDiagnostic, literalDiagnostic;
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMPOSITION_BORROW", "0");
    const bool fastResult = fast.prepare(frame, 64, 64, CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD, 0), fastDiagnostic, receipt, allowInstancing);
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMPOSITION_BORROW", "1");
    const bool literalResult = literal.prepare(frame, 64, 64, CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD, 0), literalDiagnostic, receipt, allowInstancing);
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMPOSITION_BORROW", "0");
    ok &= check(fastResult == literalResult && fastDiagnostic == literalDiagnostic,
                "borrowed schedule must preserve pack acceptance and diagnostics");
    if (fastResult) {
      const auto & a = fast.getView(); const auto & b = literal.getView();
      ok &= check(samePacked(a, b) && sameOpaqueHeader(a, b),
        "schedule loan must preserve complete opaque FFI geometry/state/material/draw/instance payload and header");
      if (a.instance_count) ++instancedSubmissions;
    }
    return {};
  }
private:
  std::string error;
};

bool compositionBorrowPacking()
{
  const char * option = std::getenv("COIN_RENDER_DISABLE_COMPOSITION_BORROW");
  struct Restore {
    bool present;
    std::string value;
    ~Restore() { coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMPOSITION_BORROW", present ? value.c_str() : nullptr); }
  } restore{option != nullptr, option ? option : ""};
  coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMPOSITION_BORROW", "0");
  CoinRenderTargetP target(SbVec2i32(64, 64));
  target.depthReadbackEnabled = false;
  auto * backend = new BorrowPackingBackend;
  target.backend.reset(backend);
  auto frame = opaqueFrame(true);
  frame.cameras.resize(1); frame.viewports.resize(1); frame.lightingStates.resize(1);
  frame.viewports[0].width = frame.viewports[0].height = 64;
  for (auto & state : frame.renderStates) state.transparencyType = SoGLRenderAction::SORTED_OBJECT_BLEND;
  uint64_t revision = 6000;
  const auto compare = [&]() {
    frame.revision = ++revision;
    return check(target.executeFrame(frame).status == CoinRenderBackendStatus::SUCCESS && backend->ok &&
                 !target.submissionPreflight(frame), "Target packing oracle must release its lender after submission");
  };
  if (!compare() || !check(backend->instancedSubmissions == 1, "CPU oracle must exercise borrowed opaque instancing")) return false;
  // The direct-texture destination retains its original non-instanced path.
  backend->allowInstancing = false;
  return compare();
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

bool instancedMatchesBake(const CoinWgpuFrameView & fast, const CoinWgpuFrameView & full,
                         float depthTolerance = 1.0e-4f)
{
  if (!check(fast.instance_count > 0 && fast.instance_range_count == fast.draw_count &&
             fast.state_count == 1 && full.instance_count == 0 && full.draw_count == 1 &&
             fast.material_count == full.material_count &&
             std::memcmp(fast.materials,full.materials,fast.material_count*sizeof(CoinWgpuMaterial)) == 0,
             "instanced comparison requires a compact payload and a full-bake oracle")) return false;
  SbMatrix commonView, commonNormal, commonProjection, fullProjection;
  commonView.setValue(fast.states[0].model_view);
  commonNormal.setValue(fast.states[0].normal_matrix);
  commonProjection.setValue(fast.states[0].model_view_projection);
  fullProjection.setValue(full.states[0].model_view_projection);
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
        SbVec4f clip, expectedClip;
        commonProjection.multVecMatrix(SbVec4f(anchorPosition[0],anchorPosition[1],anchorPosition[2],1),clip);
        fullProjection.multVecMatrix(SbVec4f(expected.position[0],expected.position[1],expected.position[2],1),expectedClip);
        for (int c = 0; c < 4; ++c)
          if (!check(std::isfinite(clip[c]) && std::isfinite(expectedClip[c]) &&
                     std::abs(clip[c]-expectedClip[c]) <= 1.0e-4f,
              "instanced clip coordinates must match the independent baked projection")) return false;
        if (clip[3] != 0 && expectedClip[3] != 0 &&
            !check(std::abs(clip[2]/clip[3] - expectedClip[2]/expectedClip[3]) <= depthTolerance,
                   "instanced normalized depth must match the independent baked projection")) return false;
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
  // Dormant texture coordinates still belong to the canonical geometry proof.
  // Every occurrence shares the same homogeneous values, including non-unit q.
  for (size_t v = 0; v < frame.vertices.size(); ++v) for (size_t unit = 0; unit < 8; ++unit) {
    frame.vertices[v].textureR[unit] = float(unit + (v % 3)) * .125f;
    frame.vertices[v].textureQ[unit] = float(unit + 1) * .25f;
  }
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

CoinRenderFramePlan diagonalBoxFrame()
{
  CoinRenderFramePlan frame;
  frame.revision = 2500;
  frame.materials.resize(2);
  frame.materials[0].diffuse[0] = .7f;
  frame.materials[1].diffuse[1] = .3f;
  frame.lightingStates.resize(1);
  CoinRenderLightSourceSnapshot light;
  light.type = CoinRenderLightType::POINT;
  light.position[0] = 3; light.position[1] = 2; light.position[2] = 4;
  frame.lightingStates[0].lights.push_back(light);
  static const int corners[8][3] = {
    {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
    {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
  };
  static const unsigned faces[6][4] = {
    {0,3,2,1},{4,5,6,7},{0,4,7,3},{1,2,6,5},{0,1,5,4},{3,7,6,2}
  };
  static const float normals[6][3] = {
    {0,0,-2},{0,0,2},{-2,0,0},{2,0,0},{0,-2,0},{0,2,0}
  };
  SbViewVolume volume;
  volume.perspective(.85f,1.4f,.1f,100);
  SbMatrix unusedView, projection;
  volume.getMatrices(unusedView,projection);
  SbMatrix view;
  view.setRotate(SbRotation(SbVec3f(0,1,0),.23f));
  view[3][0] = .5f; view[3][2] = -9;
  for (uint32_t i = 0; i < 256; ++i) {
    CoinRenderRenderStateSnapshot state;
    SbMatrix rotation, modelScale;
    rotation.setRotate(SbRotation(SbVec3f(1,2,3),.07f * float(i % 7)));
    modelScale.setScale(SbVec3f(.8f,1.3f,.9f));
    state.model = modelScale * rotation;
    state.model[3][0] = float(i % 11) * .125f;
    state.model[3][1] = float(i % 5) * -.0625f;
    state.view = view; state.projectionCoin = projection;
    state.materialSlot = i % 2;
    state.transparencyType = SoGLRenderAction::NONE;
    frame.renderStates.push_back(state);
    CoinRenderDrawPacket draw;
    draw.renderStateSlot = i;
    draw.geometry.firstVertex = static_cast<uint32_t>(frame.vertices.size());
    draw.geometry.firstIndex = static_cast<uint32_t>(frame.indices.size());
    draw.geometry.vertexCount = 24; draw.geometry.indexCount = 36;
    const float half[3] = {1 + float(i % 17) * .03125f,
                          .7f + float(i % 23) * .0125f,
                          .5f + float(i) * .001f};
    for (unsigned face = 0; face < 6; ++face) {
      for (unsigned vertex = 0; vertex < 4; ++vertex) {
        CoinRenderVertexSnapshot value;
        for (int c = 0; c < 3; ++c) {
          value.position[c] = float(corners[faces[face][vertex]][c]) * half[c];
          value.normal[c] = normals[face][c];
        }
        value.materialSlot = state.materialSlot;
        value.texcoord[0] = vertex == 1 || vertex == 2 ? 1 : 0;
        value.texcoord[1] = vertex >= 2 ? 1 : 0;
        value.extraTexcoords[3][0] = .125f; value.extraTexcoords[3][1] = .75f;
        frame.vertices.push_back(value);
      }
      for (unsigned local : {0u,1u,2u,0u,2u,3u})
        frame.indices.push_back(draw.geometry.firstVertex + face * 4 + local);
    }
    frame.draws.push_back(draw);
  }
  return frame;
}

bool instanceCommonStateEquivalence()
{
  InstancingSwitch instancing; instancing.disable(false);
  DiagonalMeshSwitch lowering; lowering.disable(false);
  EarlyBatchSwitch early; early.disable(false);
  InstanceCommonStateSwitch option;
  auto frame = diagonalBoxFrame();
  frame.revision = 4000;
  frame.viewports.resize(1);
  frame.viewports[0].x = 3; frame.viewports[0].y = 7;
  frame.viewports[0].width = 51; frame.viewports[0].height = 43;
  // Disabled metadata still belongs to the packed key. Nondefault values make
  // accidental omission visible independently of the optimized comparator.
  for (auto & state : frame.renderStates) {
    state.textureMatrix[3][0] = .375f;
    state.textureImageSlot = 3; state.samplerSlot = 5;
    state.textureBlendColor[0] = .25f;
    state.fogColor[0] = .3f; state.fogStart = .5f; state.fogEnd = 17;
    state.polygonOffsetFactor = .125f; state.polygonOffsetUnits = .25f;
    state.polygonOffsetSlopeBias = .375f;
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS - 1; ++unit) {
      state.extraTextures[unit].matrix[3][1] = float(unit) * .125f;
      state.extraTextures[unit].imageSlot = static_cast<uint32_t>(unit + 1);
      state.extraTextures[unit].samplerSlot = static_cast<uint32_t>(unit + 3);
      state.extraTextures[unit].blendColor[2] = .625f;
    }
  }
  // This state has no draw and must still participate in every proof.
  frame.renderStates.push_back(frame.renderStates.front());
  frame.renderStates.back().model[3][0] += .375f;
  const auto originalFrame = frame;
  const auto compareFresh = [&](const CoinRenderFramePlan & candidate, bool expectInstances) {
    CoinWgpuFfiFrame fast, legacy;
    std::string a, b;
    option.disable(false); const bool okA = fast.prepare(candidate,64,64,a);
    option.disable(true); const bool okB = legacy.prepare(candidate,64,64,b);
    option.disable(false);
    if (!check(okA == okB && a == b, "common-state optimization must preserve success/error diagnostics")) return false;
    if (!okA) return check(!fast.reusedLastPrepare() && fast.opaqueCameraProofReusedLastPrepare() == 0,
                         "failed common-state proof cannot publish a camera proof");
    if (!check(samePacked(fast.getView(),legacy.getView()),
               "common-state and literal paths must produce byte-identical geometry/material/state/instance payload")) return false;
    if (!check(bool(fast.getView().instance_count) == expectInstances,
               "common-state differences must preserve literal instancing admission/fallback")) return false;
    return check(fast.opaqueCommonStatesPackedLastPrepare() == (expectInstances ? 1 : 0) &&
      legacy.opaqueCommonStatesPackedLastPrepare() == (expectInstances ? candidate.renderStates.size() : 0) &&
      fast.opaqueCameraProofReusedLastPrepare() == (expectInstances ? candidate.renderStates.size() : 0) &&
      legacy.opaqueCameraProofReusedLastPrepare() == 0,
      "private counters must prove one common pack and one authored camera proof per state versus the optout");
  };
  if (!compareFresh(frame,true)) return false;
  for (bool shared : {false,true}) {
    const auto triangles = opaqueFrame(shared);
    if (!compareFresh(triangles,true)) return false;
  }
  // Each field below is transported by the ordinary packer even when the
  // feature is disabled. The late unused state must never inherit equality.
  std::vector<std::function<void(CoinRenderRenderStateSnapshot &)>> mutations = {
    [](CoinRenderRenderStateSnapshot & s) { s.textureMatrix[0][1] = .125f; },
    [](CoinRenderRenderStateSnapshot & s) { ++s.textureImageSlot; },
    [](CoinRenderRenderStateSnapshot & s) { ++s.samplerSlot; },
    [](CoinRenderRenderStateSnapshot & s) { s.textureModel = CoinRenderTextureModel::REPLACE; },
    [](CoinRenderRenderStateSnapshot & s) { s.textureBlendColor[1] = -.0f; },
    [](CoinRenderRenderStateSnapshot & s) { s.textureCombines[7].instructions[3][2] = .75f; },
    [](CoinRenderRenderStateSnapshot & s) { s.fogColor[2] = .125f; },
    [](CoinRenderRenderStateSnapshot & s) { s.fogStart += .25f; },
    [](CoinRenderRenderStateSnapshot & s) { s.fogEnd += .5f; },
    [](CoinRenderRenderStateSnapshot & s) { s.depthRange[0] = -.0f; },
    [](CoinRenderRenderStateSnapshot & s) { s.polygonOffsetFactor = -.0f; },
    [](CoinRenderRenderStateSnapshot & s) { s.polygonOffsetUnits += .5f; },
    [](CoinRenderRenderStateSnapshot & s) { s.polygonOffsetSlopeBias += .25f; },
    [](CoinRenderRenderStateSnapshot & s) { s.polygonOffsetMaxDepth = .75f; },
    [](CoinRenderRenderStateSnapshot & s) { s.polygonOffsetMaxDepth = -.0f; },
    [](CoinRenderRenderStateSnapshot & s) { ++s.polygonOffsetStyles; },
    [](CoinRenderRenderStateSnapshot & s) { s.polygonOffsetPrimitiveStyle = 2; },
    [](CoinRenderRenderStateSnapshot & s) { s.depthWrite = false; },
    [](CoinRenderRenderStateSnapshot & s) { s.cullMode = CoinRenderCullMode::FRONT; },
    [](CoinRenderRenderStateSnapshot & s) { s.frontFace = CoinRenderFrontFace::CW; },
    [](CoinRenderRenderStateSnapshot & s) { ++s.viewportSlot; },
    [](CoinRenderRenderStateSnapshot & s) { s.lightModel = CoinRenderLightModel::BASE_COLOR; },
    [](CoinRenderRenderStateSnapshot & s) { s.model[0][0] = 0; },
    [](CoinRenderRenderStateSnapshot & s) { s.model[0][0] = std::numeric_limits<float>::infinity(); }
  };
  for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS - 1; ++unit) {
    mutations.push_back([unit](CoinRenderRenderStateSnapshot & s) { s.extraTextures[unit].matrix[0][2] = .5f; });
    mutations.push_back([unit](CoinRenderRenderStateSnapshot & s) { ++s.extraTextures[unit].imageSlot; });
    mutations.push_back([unit](CoinRenderRenderStateSnapshot & s) { ++s.extraTextures[unit].samplerSlot; });
    mutations.push_back([unit](CoinRenderRenderStateSnapshot & s) { s.extraTextures[unit].model = CoinRenderTextureModel::REPLACE; });
    mutations.push_back([unit](CoinRenderRenderStateSnapshot & s) { s.extraTextures[unit].blendColor[0] = -.0f; });
  }
  for (const auto & mutation : mutations) {
    auto changed = originalFrame;
    mutation(changed.renderStates.back());
    if (!compareFresh(changed,false)) return false;
  }
  auto equivalentSentinel = originalFrame;
  equivalentSentinel.renderStates.back().polygonOffsetMaxDepth = -2;
  // Unpacked fields and alternate absent-depth sentinels are not new reasons
  // to decline the exact profile accepted by the ordinary packed comparison.
  equivalentSentinel.renderStates.back().cameraSlot = 17;
  equivalentSentinel.renderStates.back().explicitDepthMask = 7;
  if (!compareFresh(equivalentSentinel,true)) return false;
  // Use the local literal-payload oracle; this standalone test does not link
  // Common's captured-frame comparison implementation.
  CoinWgpuFfiFrame sourceBefore, sourceAfter;
  std::string beforeDiagnostic, afterDiagnostic;
  option.disable(true); instancing.disable(true); early.disable(true);
  const bool unchanged = sourceBefore.prepare(originalFrame,64,64,beforeDiagnostic) &&
    sourceAfter.prepare(frame,64,64,afterDiagnostic) &&
    beforeDiagnostic == afterDiagnostic && samePacked(sourceBefore.getView(),sourceAfter.getView());
  option.disable(false); instancing.disable(false); early.disable(false);
  if (!check(unchanged, "common-state proof must not mutate the captured payload")) return false;

  CoinWgpuFfiFrame fast, legacy;
  std::string a,b;
  uint32_t width = 64, height = 64;
  const auto compareOwners = [&](const CoinRenderFrameReuseDecision & reuse, bool allow = true) {
    option.disable(false); const bool okA = fast.prepare(frame,width,height,reuse,a,nullptr,allow);
    option.disable(true); const bool okB = legacy.prepare(frame,width,height,reuse,b,nullptr,allow);
    option.disable(false);
    return check(okA && okB && a == b && samePacked(fast.getView(),legacy.getView()) &&
      fast.lastPrepareKind() == legacy.lastPrepareKind(), "common-state reuse/camera/RTT/retry must remain byte-identical");
  };
  const CoinRenderFrameReuseDecision rebuild(CoinRenderFrameReuseKind::FULL_REBUILD,0);
  if (!compareOwners(rebuild) || !compareOwners(rebuild) ||
      !check(fast.reusedLastPrepare() && legacy.reusedLastPrepare(), "common-state exact reuse remains immutable")) return false;
  uint64_t base = frame.revision++;
  for (auto & state : frame.renderStates) state.view[3][0] += .25f;
  if (!compareOwners(CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,base)) ||
      !check(fast.getView().camera_base_revision == base, "common-state camera proof remains patchable")) return false;
  for (size_t changedCount : {size_t(26),size_t(256)}) {
    base = frame.revision++;
    for (size_t i = 0; i < changedCount; ++i) frame.renderStates[i].model[3][1] += .125f;
    if (!compareOwners(CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::RESOURCE_REBUILD,base)) ||
        !check(!fast.getView().camera_base_revision && fast.opaqueCommonStatesPackedLastPrepare() == 1,
               "objects after a camera patch must rebuild authored matrices with one common state")) return false;
    base = frame.revision++;
    for (auto & state : frame.renderStates) state.view[3][1] -= .125f;
    if (!compareOwners(CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,base))) return false;
  }
  ++frame.revision; width = 96; height = 72;
  if (!compareOwners(rebuild) || !compareOwners(rebuild,false) ||
      !check(!fast.getView().instance_count && !fast.getView().camera_base_revision,
             "RTT destination must revoke common-state instancing/camera proof")) return false;
  ++frame.revision;
  if (!compareOwners(rebuild)) return false;
  base = frame.revision++;
  frame.lightingStates.push_back(frame.lightingStates[0]);
  frame.lightingStates.back().lights.resize(COIN_WGPU_FFI_MAX_LIGHTS + 1);
  frame.renderStates.push_back(frame.renderStates.front());
  frame.renderStates.back().lightingSlot = 1;
  option.disable(false); const bool okA = fast.prepare(frame,width,height,a);
  option.disable(true); const bool okB = legacy.prepare(frame,width,height,b);
  option.disable(false);
  if (!check(!okA && !okB && a == b && a.find("More than eight active lights") != std::string::npos &&
      !fast.opaqueCommonStatesPackedLastPrepare() && !fast.opaqueCameraProofReusedLastPrepare(),
      "late unused-state lighting error must revoke common and camera proofs")) return false;
  frame.renderStates.pop_back(); frame.lightingStates.pop_back();
  // Repair without changing revision and present the now-stale camera hint.
  if (!compareOwners(CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,base)) ||
      !check(!fast.reusedLastPrepare() && !fast.getView().camera_base_revision &&
             fast.opaqueCommonStatesPackedLastPrepare() == 1,
             "same-revision repair must fully rebuild without licensing old camera storage")) return false;
  return true;
}

bool instanceMatrixCacheEquivalence()
{
  MatrixFpSnapshot callerEnvironment;
  std::fesetenv(FE_DFL_ENV);
  InstancingSwitch instancing; instancing.disable(false);
  DiagonalMeshSwitch lowering; lowering.disable(false);
  EarlyBatchSwitch early; early.disable(false);
  CameraPatchSwitch camera;
  InstanceCommonStateSwitch common;
  InstanceMatrixCacheSwitch option;
  auto frame = diagonalBoxFrame();
  // Include shear/reflection and an unused rotated/scaled authored matrix.
  frame.renderStates[11].model[0][1] += .125f;
  frame.renderStates[12].model[0][0] *= -1;
  frame.renderStates.push_back(frame.renderStates[5]);
  frame.renderStates.back().model[3][1] += .375f;
  const auto original = frame;
  const size_t n = frame.renderStates.size();
  uint64_t revision = 5000;
  frame.revision = ++revision;
  uint32_t width = 64, height = 64;
  CoinWgpuFfiFrame fast, literal;
  std::string a,b;
  bool lastAccepted = false;
  const CoinRenderFrameReuseDecision rebuild(CoinRenderFrameReuseKind::RESOURCE_REBUILD,0);
  const auto compare = [&](const CoinRenderFrameReuseDecision & reuse, bool allow = true, int expected = 1) {
    MatrixFpSnapshot start;
    option.disable(false); const bool okA = fast.prepare(frame,width,height,reuse,a,nullptr,allow);
    const uint32_t flagsA = matrixExceptionStatus();
    start.restore();
    option.disable(true); const bool okB = literal.prepare(frame,width,height,reuse,b,nullptr,allow);
    const uint32_t flagsB = matrixExceptionStatus();
    lastAccepted = okA;
    start.restore(); option.disable(false);
    if (!check(okA == okB && (expected < 0 || okA == bool(expected)) && a == b && flagsA == flagsB,
               "matrix cache preserves success, diagnostic and exact SSE/x87 sticky exception flags")) return false;
    if (!okA) return check(!fast.opaqueMatrixCacheValid(),"failed preparation revokes the numerical cache license");
    return check(samePacked(fast.getView(),literal.getView()) &&
      fast.lastPrepareKind() == literal.lastPrepareKind(),
      "matrix cache and literal calculation must produce byte-identical complete payload and reuse kind");
  };
  std::fesetenv(FE_DFL_ENV);
  if (!compare(rebuild) || !check(fast.getView().instance_count == 256,
        "rotated/scaled/sheared/reflected matrix-cache fixture remains instanced")) return false;
  const bool supported = fast.opaqueMatrixCacheBypassLastPrepare() == 0;
  if (!check(fast.opaqueMatrixCacheValid() == supported &&
      fast.opaqueMatricesCalculatedLastPrepare() == n && !fast.opaqueMatrixCacheHitsLastPrepare() &&
      fast.opaqueMatrixCacheBytes() <= 8u*1024u*1024u &&
      fast.opaqueMatrixCacheAllocationsLastPrepare() == (supported ? 1 : 0),
      "cold matrix cache computes all states and accounts bounded independent storage")) return false;
  const auto counts = [&](size_t hits, size_t calculated, const char * message) {
    return check(fast.opaqueMatrixCacheHitsLastPrepare() == (supported ? hits : 0) &&
      fast.opaqueMatricesCalculatedLastPrepare() == (supported ? calculated : n),message);
  };
  ++frame.revision;
  if (!compare(rebuild) || !counts(n-1,1,"unchanged models use exact temporal hits while slot zero stays literal")) return false;
  for (size_t changed : {size_t(26),n}) {
    ++frame.revision;
    // Ten percent excludes slot zero; 100 percent includes the unused state.
    const size_t first = changed == n ? 0 : 1;
    for (size_t i = first; i < first + changed; ++i) frame.renderStates[i].model[3][0] += .125f;
    const size_t calculated = changed == n ? n : changed+1;
    if (!compare(rebuild) || !counts(n-calculated,calculated,
        "10/100 percent matrix changes recompute exactly the current authored models")) return false;
  }
  // Returning to A after B re-reads owned values; no source pointer licenses it.
  revision = frame.revision + 1; frame = original; frame.revision = revision;
  if (!compare(rebuild)) return false;
  ++frame.revision;
  if (!compare(rebuild) || !counts(n-1,1,"A/B/A models become hits only after a successful fresh proof")) return false;
  ++frame.revision; frame.renderStates[7].model[0][3] = -.0f;
  if (!compare(rebuild) || !counts(n-2,2,"signed-zero source model bytes must not inherit a prior matrix proof")) return false;
  ++frame.revision;
  std::swap(frame.renderStates[1].model,frame.renderStates[2].model);
  if (!compare(rebuild) || !counts(n-3,3,"reordered model values are proved independently at their new state indices")) return false;
  ++frame.revision; std::swap(frame.draws[1],frame.draws[2]);
  if (!compare(rebuild) || !counts(n-1,1,"draw reordering preserves instance order independently of cached authored matrices")) return false;
  ++frame.revision; frame.renderStates.back().model[3][2] -= .25f;
  if (!compare(rebuild) || !counts(n-2,2,"changed unused state still receives a current matrix proof")) return false;

  ++frame.revision; frame.materials[1].diffuse[2] += .125f;
  frame.renderStates[0].materialSlot = 1;
  for (size_t i = 0; i < 24; ++i) frame.vertices[i].materialSlot = 1;
  if (!compare(rebuild) || !counts(n-1,1,"matrix hits must retain current material table and per-instance slots")) return false;
  ++frame.revision;
  for (size_t i = 0; i < 24; ++i) frame.vertices[i].position[0] *= 1.125f;
  if (!compare(rebuild) || !counts(n-1,1,"mutable geometry is requalified and scaled with current data despite matrix hits")) return false;
  ++frame.revision;
  for (auto & state : frame.renderStates) state.projectionCoin[0][0] *= 1.03125f;
  if (!compare(rebuild) || !counts(n-1,1,"projection remains current while model/view matrix proofs are reused")) return false;
  ++frame.revision; width = 96; height = 80;
  if (!compare(rebuild) || !counts(n-1,1,"resize reassembles current viewport without changing authored matrix values")) return false;

  uint64_t base = frame.revision++;
  for (auto & state : frame.renderStates) state.view[3][0] += .25f;
  if (!compare(CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,base)) ||
      !check(!fast.opaqueMatrixCacheValid() && !fast.opaqueMatrixCacheHitsLastPrepare() &&
        !fast.opaqueMatricesCalculatedLastPrepare(),"camera anchor patch revokes temporal authored-matrix license")) return false;
  ++frame.revision; frame.renderStates[3].model[3][1] += .125f;
  if (!compare(rebuild) || !counts(0,n,"objects after a camera patch must prove all authored matrices again")) return false;
  if (!compare(rebuild,false) || !check(!fast.getView().instance_count && !fast.opaqueMatrixCacheValid(),
      "allowInstancing=false/RTT revokes the cache even on the same packed revision")) return false;
  ++frame.revision;
  if (!compare(rebuild) || !counts(0,n,"instancing after RTT rebuilds without a stale authored proof")) return false;
  option.disable(true);
  if (!check(fast.prepare(frame,width,height,a) && fast.reusedLastPrepare() && !fast.opaqueMatrixCacheValid(),
      "matrix optout revokes its license on the unchanged-revision fast path")) return false;
  option.disable(false); ++frame.revision;
  if (!compare(rebuild) || !counts(0,n,"reenabling matrix cache readmits only after a fresh full proof")) return false;
  ++frame.revision;
  if (!compare(rebuild)) return false;

  // Start both calculations with exactly the same sticky flags. An altered
  // incoming status declines individual hits and is readmitted on next call.
  std::feclearexcept(FE_ALL_EXCEPT);
  std::feraiseexcept(FE_DIVBYZERO);
  ++frame.revision;
  if (!compare(rebuild) || !counts(0,n,"new sticky exception status cannot reuse an old numerical proof")) return false;
  ++frame.revision;
  if (!compare(rebuild) || !counts(n-1,1,"identical sticky status is readmitted after successful literal calculation")) return false;
  std::feclearexcept(FE_ALL_EXCEPT);
  ++frame.revision;
  if (!compare(rebuild) || !counts(0,n,"cleared sticky status cannot inherit a proof from raised exception flags")) return false;
  ++frame.revision;
  if (!compare(rebuild) || !counts(n-1,1,"cleared status is readmitted without clearing flags in production")) return false;
  for (int rounding : {FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}) {
    std::fesetround(rounding); ++frame.revision;
    if (!compare(rebuild) || !check(!fast.opaqueMatrixCacheValid() &&
        fast.opaqueMatrixCacheBypassLastPrepare() == 2 && !fast.opaqueMatrixCacheHitsLastPrepare(),
        "nondefault rounding retains literal bytes and revokes the matrix cache")) return false;
  }
  std::fesetenv(FE_DFL_ENV); ++frame.revision;
  if (!compare(rebuild) || !counts(0,n,"default FP mode requires readmission after rounding-mode bypass")) return false;
#ifdef COIN_WGPU_MATRIX_CACHE_TEST_FP_X86
  for (uint32_t mode : {uint32_t(1u<<15),uint32_t(1u<<6),uint32_t(3u<<13)}) {
    MatrixFpSnapshot modeEnvironment;
    _mm_setcsr(_mm_getcsr() | mode);
    // Exercise revocation before the pre-existing revision fast path too.
    if (!compare(rebuild) || !check(fast.reusedLastPrepare() && !fast.opaqueMatrixCacheValid() &&
        fast.opaqueMatrixCacheBypassLastPrepare() == 2,"FTZ/DAZ/SSE rounding revoke a NoChange matrix license")) return false;
    ++frame.revision;
    if (!compare(rebuild) || !check(!fast.opaqueMatrixCacheHitsLastPrepare(),
        "unrecognized denormal/rounding mode computes matrices literally")) return false;
    modeEnvironment.restore(); ++frame.revision;
    if (!compare(rebuild) || !counts(0,n,"FP controls restored after bypass require fresh proof")) return false;
  }
  {
    MatrixFpSnapshot trapEnvironment;
    _mm_setcsr(0x1f80u & ~(1u<<9)); // Divide-by-zero trap, no pending flags.
    if (!compare(rebuild) || !check(fast.reusedLastPrepare() && !fast.opaqueMatrixCacheValid() &&
        fast.opaqueMatrixCacheBypassLastPrepare() == 2,"unmasked SSE trap declines cache before NoChange")) return false;
    trapEnvironment.restore(); ++frame.revision;
    if (!compare(rebuild)) return false;
  }
  {
    MatrixFpSnapshot trapEnvironment;
    std::feclearexcept(FE_ALL_EXCEPT);
    uint16_t x87 = 0x037fu & ~uint16_t(4);
    __asm__ __volatile__("fldcw %0" : : "m" (x87));
    if (!compare(rebuild) || !check(fast.reusedLastPrepare() && !fast.opaqueMatrixCacheValid() &&
        fast.opaqueMatrixCacheBypassLastPrepare() == 2,"unmasked x87 trap declines cache before NoChange")) return false;
    trapEnvironment.restore(); ++frame.revision;
    if (!compare(rebuild)) return false;
  }
#endif

  // Prime a valid cache before each late error/fallback so stale entries would
  // cause observable differences. Every changed source keeps its new revision.
  const std::vector<std::function<void(CoinRenderRenderStateSnapshot &)>> badStates = {
    [](CoinRenderRenderStateSnapshot & s) { s.model.setScale(SbVec3f(0,1,1)); },
    [](CoinRenderRenderStateSnapshot & s) { s.model.setScale(SbVec3f(.5e-12f,1,1)); },
    [](CoinRenderRenderStateSnapshot & s) { s.model.setScale(SbVec3f(2e-12f,1,1)); },
    [](CoinRenderRenderStateSnapshot & s) { s.model.setScale(SbVec3f(.5e-9f,1,1)); },
    [](CoinRenderRenderStateSnapshot & s) { s.model.setScale(SbVec3f(2e-9f,1,1)); },
    [](CoinRenderRenderStateSnapshot & s) { s.model[0][0] = std::numeric_limits<float>::max(); },
    [](CoinRenderRenderStateSnapshot & s) { s.model[0][0] = std::numeric_limits<float>::infinity(); },
    [](CoinRenderRenderStateSnapshot & s) { s.model[0][0] = std::numeric_limits<float>::quiet_NaN(); },
    [](CoinRenderRenderStateSnapshot & s) { s.model[0][1] = std::numeric_limits<float>::denorm_min(); },
    [](CoinRenderRenderStateSnapshot & s) { s.model[0][3] = .125f; },
    [](CoinRenderRenderStateSnapshot & s) { s.extraTextures[6].matrix[3][2] = .125f; },
    [](CoinRenderRenderStateSnapshot & s) { s.depthRange[0] = -.0f; },
    [](CoinRenderRenderStateSnapshot & s) { s.transparentMaterial = true; }
  };
  for (const auto & mutate : badStates) {
    revision = frame.revision + 1; frame = original; frame.revision = revision;
    std::fesetenv(FE_DFL_ENV);
    if (!compare(rebuild) || !check(fast.getView().instance_count == 256 &&
        fast.opaqueMatrixCacheValid() == supported,"each late-state oracle starts with a successfully proved instance cache")) return false;
    ++frame.revision; mutate(frame.renderStates.back());
    // Literal optout is the authority for the acceptance of adversarial input.
    if (!compare(rebuild,true,-1)) return false;
    if (!fast.getView().instance_count && !check(!fast.opaqueMatrixCacheValid(),
        "successful late incompatible/singular fallback cannot publish a matrix-cache candidate")) return false;
    const bool sourceFinite = CoinRenderTransformCore::finiteMatrix(frame.renderStates.back().model);
    if (!sourceFinite) {
      if (!check(!fast.opaqueMatrixCacheValid(),
          "NaN/Inf authored inputs follow literal FFI admission without a matrix-cache license")) return false;
      // FFI packing alone may transport a non-finite unused state; the Target's
      // FramePlan admission is separate. Only a failed prepare revokes revision.
      if (lastAccepted) ++frame.revision;
      frame.renderStates.back() = original.renderStates.back();
      if (!compare(rebuild) || !counts(0,n,"finite-state repair cannot inherit old numerical entries")) return false;
    }
  }
  revision = frame.revision + 1; frame = original; frame.revision = revision;
  std::fesetenv(FE_DFL_ENV);
  if (!compare(rebuild)) return false;
  ++frame.revision;
  for (size_t i = 0; i < 24; ++i) frame.vertices[i].position[0] *= 1.0e31f;
  if (!compare(rebuild) || !check(!fast.getView().instance_count && !fast.opaqueMatrixCacheValid(),
      "post-matrix position-bound fallback cannot publish entries through the successful early-batch branch")) return false;
  frame.vertices = original.vertices;
  if (!compare(rebuild) || !check(fast.reusedLastPrepare() && !fast.opaqueMatrixCacheValid(),
      "successful fallback keeps the pre-existing immutable-revision contract without licensing matrix entries")) return false;
  ++frame.revision;
  if (!compare(rebuild) || !counts(0,n,"repaired geometry at a new revision must reprove matrices after late fallback")) return false;
  base = frame.revision++;
  frame.lightingStates.push_back(frame.lightingStates[0]);
  frame.lightingStates.back().lights.resize(COIN_WGPU_FFI_MAX_LIGHTS+1);
  frame.renderStates.back().lightingSlot = 1;
  if (!compare(rebuild,true,false) || !check(!fast.opaqueMatrixCacheValid(),
      "late unused nine-light error revokes temporal cache authority")) return false;
  frame.lightingStates.pop_back(); frame.renderStates.back().lightingSlot = 0;
  if (!compare(CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,base)) ||
      !counts(0,n,"same-revision repair with stale camera hint recomputes every matrix after failure")) return false;

  // The independent budget includes allocated capacity, view and bookkeeping.
  // Large unused state tables cannot evade the cap by having only 256 draws.
  revision = frame.revision + 1; frame = original; frame.renderStates.resize(40001,frame.renderStates.front());
  frame.revision = revision; std::fesetenv(FE_DFL_ENV);
  if (!compare(rebuild) || !check(fast.opaqueMatrixCacheBytes() <= 8u*1024u*1024u &&
      fast.opaqueMatrixCacheValid() == supported,"40k matrix entries fit the accounted independent cache budget")) return false;
  frame.renderStates.resize(43000,frame.renderStates.front()); ++frame.revision;
  if (!compare(rebuild) || !check(!fast.opaqueMatrixCacheValid() && !fast.opaqueMatrixCacheBytes() &&
      !fast.opaqueMatrixCacheHitsLastPrepare() &&
      fast.opaqueMatricesCalculatedLastPrepare() == frame.renderStates.size() &&
      fast.opaqueMatrixCacheBypassLastPrepare() == (supported ? 3 : 2),
      "above-budget matrix state tables preserve instancing and release optional storage")) return false;
  revision = frame.revision + 1; frame = original; frame.revision = revision;
  return compare(rebuild) && counts(0,n,"a small scene after budget fallback is readmitted without stale entries");
}

bool diagonalMeshEquivalence()
{
  auto frame = diagonalBoxFrame();
  InstancingSwitch instancing;
  DiagonalMeshSwitch lowering;
  EarlyBatchSwitch early;
  CameraPatchSwitch camera;
  CoinWgpuFfiFrame fast, original;
  std::string a,b;
  CoinRenderFrameReuseDecision reuse(CoinRenderFrameReuseKind::FULL_REBUILD,0);
  const auto compare = [&](bool expectedInstanced) {
    instancing.disable(false); early.disable(false);
    const bool okA = fast.prepare(frame,64,64,reuse,a);
    instancing.disable(true); early.disable(true);
    const bool okB = original.prepare(frame,64,64,b);
    instancing.disable(false); early.disable(false);
    if (!check(okA && okB,"diagonal position lowering and independent full-bake preparation")) return false;
    return expectedInstanced ? instancedMatchesBake(fast.getView(),original.getView(),1.0e-5f) :
      check(!fast.getView().instance_count && samePacked(fast.getView(),original.getView()),
            "rejected diagonal profile must preserve byte-identical ordinary packing");
  };
  if (!compare(true) || !check(fast.getView().vertex_count == 24 && fast.getView().index_count == 36 &&
      fast.getView().draw_count == 1 && fast.getView().instance_count == 256 &&
      fast.opaqueHashedRangesLastPrepare() == 1,
      "256 differently sized boxes must use one unit-coordinate mesh and preserve every occurrence")) return false;
  for (uint32_t v = 0; v < 24; ++v) for (int c = 0; c < 3; ++c)
    if (!check(std::abs(fast.getView().vertices[v].position[c]) == 1,
               "diagonal canonical positions must be exact unit extrema")) return false;
  SbMatrix authoredNormal = CoinRenderTransformCore::normalMatrix(frame.renderStates[7].model * frame.renderStates[7].view);
  if (!check(std::memcmp(fast.getView().instances[7].normal_matrix,authoredNormal.getValue(),sizeof(float)*16) == 0,
             "factoring local positions must not rescale or renormalize authored normals")) return false;
  const auto sourcePositions = frame.vertices;
  // A material table edit plus reassignment of one exclusive source span must
  // preserve the new slot and table, even though geometry ignores material.
  ++frame.revision; frame.materials[1].diffuse[2] = .45f;
  frame.renderStates[0].materialSlot = 1;
  for (uint32_t v = 0; v < 24; ++v) frame.vertices[v].materialSlot = 1;
  reuse = CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::RESOURCE_REBUILD,frame.revision-1);
  if (!compare(true) || !check(fast.getView().instances[0].material_slot == 1,
      "material RESOURCE_REBUILD must qualify current exact slot and material bytes")) return false;
  const uint64_t cameraBase = frame.revision++;
  for (auto & state : frame.renderStates) state.view[3][0] += .125f;
  reuse = CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,cameraBase);
  if (!compare(true) || !check(fast.getView().camera_base_revision == cameraBase,
       "factorized positions must retain a valid authored camera anchor")) return false;
  ++frame.revision;
  for (uint32_t v = 24; v < 48; ++v) frame.vertices[v].position[1] *= 1.3f;
  frame.renderStates[5].model[3][1] += .25f;
  reuse = CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::RESOURCE_REBUILD,frame.revision-1);
  if (!compare(true) || !check(fast.getView().draw_count == 1 && !fast.getView().camera_base_revision,
      "geometry and objects immediately after camera must rebuild factored current-space positions")) return false;
  const uint64_t objectBase = frame.revision++;
  for (auto & state : frame.renderStates) state.view[3][1] += .125f;
  reuse = CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,objectBase);
  if (!compare(true)) return false;
  // The optout revokes both same-revision and camera-anchor reuse.
  lowering.disable(true);
  if (!check(fast.prepare(frame,64,64,a) && !fast.reusedLastPrepare() && !fast.getView().instance_count,
      "diagonal lowering optout must revoke exact reuse of a normalized payload")) return false;
  lowering.disable(false); ++frame.revision;
  reuse = CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD,0);
  if (!compare(true)) return false;
  const uint64_t disabledBase = frame.revision++;
  for (auto & state : frame.renderStates) state.view[3][1] += .125f;
  lowering.disable(true);
  if (!check(fast.prepare(frame,64,64,
      CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH,disabledBase),a) &&
      !fast.getView().instance_count && !fast.getView().camera_base_revision,
      "diagonal lowering optout must revoke an old normalized camera anchor")) return false;
  lowering.disable(false); ++frame.revision;
  if (!compare(true)) return false;
  // In-place geometry mutation is real content, regardless of sourceRevision.
  ++frame.revision; frame.vertices[0].position[0] *= .75f;
  if (!compare(true) || !check(fast.getView().draw_count == 2 && fast.getView().vertex_count == 48,
       "a non-extremal vertex must use its exact mesh instead of an unsafe diagonal reconstruction")) return false;
  frame.vertices[0].position[0] = sourcePositions[0].position[0]; ++frame.revision;
  if (!compare(true)) return false;
  ++frame.revision; frame.vertices[0].materialSlot = 0;
  if (!compare(false)) return false;
  frame.vertices[0].materialSlot = 1; ++frame.revision;
  if (!compare(true)) return false;
  const SbMatrix stableModel = frame.renderStates[5].model;
  frame.renderStates[5].model.setScale(SbVec3f(1,0,1)); ++frame.revision;
  if (!compare(false)) return false;
  frame.renderStates[5].model = stableModel; ++frame.revision;
  if (!compare(true)) return false;
  // Late failed packing must discard the normalized/camera proof, then allow
  // an ordinary same-revision repair, as with the preexisting instanced path.
  ++frame.revision;
  frame.lightingStates.push_back(frame.lightingStates[0]);
  frame.lightingStates.back().lights.resize(COIN_WGPU_FFI_MAX_LIGHTS+1);
  frame.renderStates.push_back(frame.renderStates[0]); frame.renderStates.back().lightingSlot = 1;
  if (!check(!fast.prepare(frame,64,64,a) && a.find("More than eight active lights") != std::string::npos,
       "late invalid lighting must discard all diagonal proofs")) return false;
  frame.renderStates.pop_back(); frame.lightingStates.pop_back();
  if (!compare(true) || !check(!fast.reusedLastPrepare(),"repaired diagonal revision must rebuild")) return false;
  if (!check(fast.prepare(frame,64,64,a,false) && !fast.getView().instance_count,
       "RTT destination must preserve the ordinary non-instanced path after diagonal lowering")) return false;
  return true;
}

bool diagonalMeshOrderAndFallback()
{
  InstancingSwitch instancing; instancing.disable(false);
  DiagonalMeshSwitch lowering;
  EarlyBatchSwitch early;
  auto frame = diagonalBoxFrame();
  CoinWgpuFfiFrame fast, original;
  std::string a,b;
  const auto compare = [&](bool instanced) {
    instancing.disable(false); early.disable(false);
    const bool okA = fast.prepare(frame,64,64,a);
    instancing.disable(true); early.disable(true);
    const bool okB = original.prepare(frame,64,64,b);
    instancing.disable(false); early.disable(false);
    if (!check(okA && okB,"diagonal order/fallback preparation")) return false;
    return instanced ? instancedMatchesBake(fast.getView(),original.getView(),1.0e-5f) :
      check(!fast.getView().instance_count && samePacked(fast.getView(),original.getView()),
            "diagonal order/fallback must remain byte-identical to ordinary packing");
  };
  // A reflected block has different canonical positions. It cannot be merged
  // with the positive block, or regrouped A/A/B; normals and winding stay raw.
  for (uint32_t i = 128; i < 192; ++i)
    for (uint32_t v = 0; v < 24; ++v) frame.vertices[i*24+v].position[0] *= -1;
  if (!compare(true) || !check(fast.getView().draw_count == 3 && fast.getView().vertex_count == 48 &&
      fast.getView().instance_ranges[0].instance_count == 128 &&
      fast.getView().instance_ranges[1].instance_count == 64 &&
      fast.getView().instance_ranges[2].instance_count == 64,
      "A/reflected-B/A must preserve order, captured normals and unchanged indices")) return false;
  for (uint32_t i = 128; i < 192; ++i)
    for (uint32_t v = 0; v < 24; ++v) frame.vertices[i*24+v].position[0] *= -1;
  ++frame.revision;
  // Signed zero is preserved in the canonical position and its exact proof.
  for (uint32_t i = 0; i < 256; ++i) frame.vertices[i*24].position[0] = -.0f;
  if (!compare(true) || !check(fast.getView().draw_count == 1 &&
      std::signbit(fast.getView().vertices[0].position[0]),"canonicalization must retain signed zero")) return false;
  // Zero, subnormal and one-sided dimensions are not admitted to this profile;
  // differing remaining dimensions still exceed the ordinary group bound.
  uint64_t dimensionRevision = 2700;
  for (float dimension : {0.0f,std::numeric_limits<float>::denorm_min()}) {
    frame = diagonalBoxFrame(); frame.revision = dimensionRevision++;
    for (uint32_t i = 0; i < 256; ++i) for (uint32_t v = 0; v < 24; ++v)
      frame.vertices[i*24+v].position[0] = std::copysign(dimension,frame.vertices[i*24+v].position[0]);
    if (!compare(false)) return false;
  }
  frame = diagonalBoxFrame(); frame.revision = 2900;
  for (auto & vertex : frame.vertices) vertex.position[0] = std::abs(vertex.position[0]);
  if (!compare(false)) return false;
  frame = diagonalBoxFrame(); frame.revision = 2950;
  for (auto & vertex : frame.vertices)
    vertex.position[1] = std::copysign(std::numeric_limits<float>::max(),vertex.position[1]);
  if (!compare(false)) return false;
  // Different attributes/normals/topology keep different canonical meshes.
  // Alternating either distinction cannot bypass the consecutive-group cap.
  for (unsigned change = 0; change < 3; ++change) {
    frame = diagonalBoxFrame(); frame.revision = 3000 + change;
    for (uint32_t i = 1; i < 256; i += 2) {
      if (change == 0) frame.vertices[i*24].normal[0] = .125f;
      if (change == 1) frame.vertices[i*24].extraTexcoords[3][0] = .25f;
      if (change == 2) std::swap(frame.indices[i*36],frame.indices[i*36+1]);
    }
    if (!compare(false)) return false;
  }
  return true;
}

bool boundedInstancingMetadata()
{
  auto frame = opaqueFrame(false);
  const auto triangle = std::vector<CoinRenderVertexSnapshot>(frame.vertices.begin(),frame.vertices.begin()+3);
  const uint32_t count = 65537;
  frame.vertices.clear(); frame.indices.clear(); frame.draws.clear();
  frame.renderStates.resize(1); frame.renderStates[0].materialSlot = 0;
  for (uint32_t i = 0; i < count; ++i) {
    CoinRenderDrawPacket draw;
    draw.geometry.firstVertex = draw.geometry.firstIndex = i*3;
    draw.geometry.vertexCount = draw.geometry.indexCount = 3;
    for (uint32_t v = 0; v < 3; ++v) {
      frame.vertices.push_back(triangle[v]); frame.indices.push_back(i*3+v);
    }
    frame.draws.push_back(draw);
  }
  InstancingSwitch instancing; instancing.disable(false);
  CoinWgpuFfiFrame packed;
  std::string diagnostic;
  if (!check(packed.prepare(frame,64,64,diagnostic) && !packed.getView().instance_count &&
      packed.getView().vertex_count == count*3 && packed.getView().draw_count == 1,
      "source spans beyond the independent 64k/8MiB metadata bound must preserve the full-bake fallback")) return false;
  return true;
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
  if (!externalViewportPacking() || !opaqueBatching(false) || !opaqueBatching(true) || !anchoredPhongCamera() ||
      !earlyBatchEquivalence(false) || !earlyBatchEquivalence(true) ||
      !incrementalBatchEquivalence(false) || !incrementalBatchEquivalence(true) ||
      !boundedIncrementalBatch()) return 1;
  instancing.disable(false);
  if (!compositionBorrowPacking() || !opaqueInstancingEquivalence(false) || !opaqueInstancingEquivalence(true) ||
      !consecutiveInstancingGroups() || !diagonalMeshEquivalence() ||
      !diagonalMeshOrderAndFallback() || !boundedInstancingMetadata() ||
      !instanceCommonStateEquivalence() || !instanceMatrixCacheEquivalence()) return 1;
  CoinRenderFramePlan frame;
  frame.revision = 41;
  frame.vertices.resize(1);
  frame.vertices[0].position[0] = 2.0f;
  frame.vertices[0].screenSpaceW = 2.0f;
  frame.vertices[0].fogEyeDepth = 4.0f;
  for (size_t unit = 0; unit < 8; ++unit) {
    float * uv = unit ? frame.vertices[0].extraTexcoords[unit - 1] : frame.vertices[0].texcoord;
    uv[0] = float(unit + 1) * .125f;
    uv[1] = float(unit + 2) * .25f;
    frame.vertices[0].textureR[unit] = float(unit + 3) * .5f;
    frame.vertices[0].textureQ[unit] = float(unit + 4) * .75f;
  }
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
  frame.renderStates[0].textureProjection = CoinRenderTextureProjection::DIRECT_ST;
  frame.renderStates[0].polygonOffsetSlopeBias = -.01f;
  frame.renderStates[0].polygonOffsetMaxDepth = .75f;

  CoinWgpuFfiFrame packed;
  std::string diagnostic;
  if (!check(packed.prepare(frame, 64, 32, diagnostic), "initial packing failed") ||
      !check(!packed.reusedLastPrepare(), "initial packing reported reuse")) return 1;

  const CoinWgpuFrameView & first = packed.getView();
  if (!check(first.abi_version == 46 && sizeof(CoinWgpuVertex) == 164 &&
             sizeof(CoinWgpuRenderState) == 2292 && first.states[0].texture_projection == 1,
             "projective private protocol and vertex stride")) return 1;
  for (size_t unit = 0; unit < 8; ++unit) {
    const float * packedUv = unit ? first.vertices[0].extra_texcoords[unit - 1] : first.vertices[0].texcoord;
    const float * sourceUv = unit ? frame.vertices[0].extraTexcoords[unit - 1] : frame.vertices[0].texcoord;
    if (!check(packedUv[0] == sourceUv[0] && packedUv[1] == sourceUv[1] &&
               packedUv[2] == frame.vertices[0].textureR[unit] &&
               packedUv[3] == frame.vertices[0].textureQ[unit],
               "all eight homogeneous texture coordinates must be owned without predivision")) return 1;
  }
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
  frame.renderStates[0].textureProjection = CoinRenderTextureProjection::PROJECTIVE;
  frame.vertices[0].textureR[7] = 99.0f;
  frame.vertices[0].textureQ[7] = 100.0f;
  frame.indices[0] = 7;
  frame.textures[0].pixelsRgba[0] = 99;
  if (!check(packed.prepare(frame, 128, 16, diagnostic), "cached packing failed") ||
      !check(packed.reusedLastPrepare(), "same non-zero revision was not reused")) return 1;
  const CoinWgpuFrameView & reused = packed.getView();
  if (!check(reused.width == 128 && reused.height == 16, "reuse did not update target dimensions") ||
      !check(reused.camera_base_revision == 0, "exact reuse must not claim a camera patch") ||
      !check(reused.vertices[0].position[0] == 2.0f, "reuse observed a mutated source frame") ||
      !check(reused.states[0].texture_projection == 1, "reuse observed mutated source projection policy") ||
      !check(reused.vertices[0].extra_texcoords[6][2] == 5.0f &&
                 reused.vertices[0].extra_texcoords[6][3] == 8.25f,
             "reuse observed mutated source homogeneous texture coordinates") ||
      !check(reused.indices[0] == 0, "reuse observed mutated source indices") ||
      !check(reused.textures[0].pixels[0] == 10, "reuse observed mutated texture bytes")) return 1;

  frame.revision = 42;
  if (!check(packed.prepare(frame, 128, 16, diagnostic), "new revision packing failed") ||
      !check(!packed.reusedLastPrepare(), "new revision incorrectly reused packing") ||
      !check(packed.getView().vertices[0].position[0] == 9.0f, "new revision was not repacked") ||
      !check(packed.getView().states[0].texture_projection == 0, "new revision must repack projection policy") ||
      !check(packed.getView().vertices[0].extra_texcoords[6][2] == 99.0f &&
                 packed.getView().vertices[0].extra_texcoords[6][3] == 100.0f,
             "new revision must repack homogeneous texture coordinates")) return 1;

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
