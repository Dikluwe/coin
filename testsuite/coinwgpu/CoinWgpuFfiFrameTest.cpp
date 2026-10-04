#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"

#include <cstdint>
#include <cmath>
#include <Inventor/actions/SoGLRenderAction.h>
#include <iostream>
#include <string>

namespace {
bool check(bool condition, const char * message)
{
  if (!condition) std::cerr << "CoinWgpuFfiFrameTest: " << message << '\n';
  return condition;
}

bool opaqueBatching(bool shared)
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
        "batched camera fallback") ||
      !check(packed.lastPrepareKind() == CoinRenderFrameReuseKind::FULL_REBUILD &&
        packed.getView().camera_base_revision == 0 &&
        packed.getView().vertices[0].position[0] == 2.25f,
        "moving the camera must rebake geometry instead of patching old view positions")) return false;
  frame.vertices[0].position[0] = 10;
  if (!check(packed.prepare(frame,64,64,error) &&
        packed.getView().vertices[0].position[0] == 2.25f,
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
}

int
main()
{
  if (!opaqueBatching(false) || !opaqueBatching(true)) return 1;
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
