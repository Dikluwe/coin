#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"

#include <cstdint>
#include <Inventor/actions/SoGLRenderAction.h>
#include <iostream>
#include <string>

namespace {
bool check(bool condition, const char * message)
{
  if (!condition) std::cerr << "CoinWgpuFfiFrameTest: " << message << '\n';
  return condition;
}
}

int
main()
{
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

  CoinWgpuFfiFrame packed;
  std::string diagnostic;
  if (!check(packed.prepare(frame, 64, 32, diagnostic), "initial packing failed") ||
      !check(!packed.reusedLastPrepare(), "initial packing reported reuse")) return 1;

  const CoinWgpuFrameView & first = packed.getView();
  if (!check(first.width == 64 && first.height == 32, "target dimensions were not packed") ||
      !check(first.vertices[0].screen_space_w == 2.0f && first.vertices[0].fog_eye_depth_plus_one == 5.0f,
             "homogeneous stroke attributes were not packed") ||
      !check(first.states[0].polygon_offset_slope_bias == -.01f, "original polygon slope bias was not packed") ||
      !check(first.indices != frame.indices.data(), "packed indices do not own their storage") ||
      !check(first.textures[0].pixels != frame.textures[0].pixelsRgba.data(),
             "packed texture does not own its storage")) return 1;

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
  sorted.indices = {0, 1};
  sorted.draws.resize(2);
  for (uint32_t i = 0; i < 2; ++i) {
    sorted.draws[i].geometry.firstIndex = i;
    sorted.draws[i].geometry.indexCount = 1;
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
  if (!check(!resolved.prepare(sorted, 64, 64, diagnostic) && diagnostic.find("UNSUPPORTED") != std::string::npos,
      "wgpu must reject triangle sorting instead of substituting weighted OIT")) return 1;
  sorted.revision = 101;
  sorted.renderStates[0].transparencyType = SoGLRenderAction::SORTED_OBJECT_BLEND;
  if (!check(resolved.prepare(sorted, 64, 64, diagnostic) && !resolved.reusedLastPrepare(),
      "failed pack must invalidate cached storage")) return 1;

  std::cout << "CoinWgpuFfiFrameTest passed\n";
  return 0;
}
