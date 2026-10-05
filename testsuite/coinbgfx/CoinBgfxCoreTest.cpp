#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinbgfx/CoinBgfxLowering.h"
#include "rendering/coinbgfx/CoinBgfxProgramCache.h"
#include "rendering/coinrender/CoinRenderTransformCore.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "../coinrender/CoinRenderTestEnvironment.h"

#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/SoDB.h>
#include <Inventor/SbRotation.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
bool check(bool condition, const char * message)
{
  if (!condition) std::cerr << "CoinBgfxCoreTest: " << message << '\n';
  return condition;
}

class SharedRangeSwitch {
public:
  SharedRangeSwitch() {
    const char * value = std::getenv(name());
    wasSet = value != nullptr;
    if (value) previous = value;
  }
  ~SharedRangeSwitch() { set(wasSet ? previous.c_str() : nullptr); }
  void disable(bool disabled) { set(disabled ? "1" : nullptr); }
private:
  static const char * name() { return "COIN_BGFX_DISABLE_SHARED_RANGE_LOWERING"; }
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

template <typename T>
bool sameBytes(const std::vector<T> & lhs, const std::vector<T> & rhs)
{
  return lhs.size() == rhs.size() && (lhs.empty() ||
    std::memcmp(lhs.data(), rhs.data(), lhs.size() * sizeof(T)) == 0);
}

bool sharedRangeMatchesGeneral(const CoinRenderFramePlan & frame,
                              bool compact = false, bool expectedValid = true)
{
  SharedRangeSwitch rangeSwitch;
  CoinBgfxPlan optimized, general;
  std::string optimizedDiagnostic, generalDiagnostic;
  rangeSwitch.disable(false);
  const bool optimizedOk = CoinBgfxLowering::lower(frame, 4, 4, false,
    optimized, optimizedDiagnostic, false, true, nullptr, compact);
  rangeSwitch.disable(true);
  const bool generalOk = CoinBgfxLowering::lower(frame, 4, 4, false,
    general, generalDiagnostic, false, true, nullptr, compact);
  return check(optimizedOk == expectedValid && optimizedOk == generalOk &&
    optimizedDiagnostic == generalDiagnostic &&
    optimized.usesCompactVertices == general.usesCompactVertices &&
    sameBytes(optimized.vertices, general.vertices) &&
    sameBytes(optimized.packedVertices, general.packedVertices) &&
    sameBytes(optimized.indices, general.indices) &&
    sameBytes(optimized.draws, general.draws) &&
    sameBytes(optimized.shadowDraws, general.shadowDraws) &&
    (optimizedOk ? std::memcmp(optimized.clearColor, general.clearColor,
                              sizeof(optimized.clearColor)) == 0 : true),
    "shared ranges must preserve all geometry/draw bytes, validity and diagnostic");
}

class BorrowLoweringBackend : public CoinRenderBackend {
public:
  bool ok = true;
  unsigned submissions = 0;
  bool isGpuBackend() const override { return false; }
  CoinRenderBackendStatus getStatus() const override { return CoinRenderBackendStatus::SUCCESS; }
  CoinRenderBackendStatus prepare(CoinRenderTargetP &) override { return CoinRenderBackendStatus::SUCCESS; }
  void poll() override {}
  const std::string & getLastError() const override { return error; }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target) override {
    ++submissions;
    const auto * receipt = target.submissionPreflight(frame);
    ok &= check(receipt && receipt->opaqueCompositionFor(frame), "BG oracle must consume a current opaque Target proof");
    CoinBgfxPlan borrowed, literal;
    std::string a, b;
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMPOSITION_BORROW", "0");
    const bool fastResult = CoinBgfxLowering::lowerInstanced(frame, 4, 4, false, borrowed, a, receipt);
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMPOSITION_BORROW", "1");
    const bool literalResult = CoinBgfxLowering::lowerInstanced(frame, 4, 4, false, literal, b, receipt);
    coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMPOSITION_BORROW", "0");
    ok &= check(fastResult && fastResult == literalResult && a == b && borrowed.usesInstancing &&
      borrowed.usesCompactVertices == literal.usesCompactVertices &&
      borrowed.instancedCameraPatchable == literal.instancedCameraPatchable &&
      std::memcmp(borrowed.instanceCameraAnchorView.getValue(), literal.instanceCameraAnchorView.getValue(),
                  16 * sizeof(float)) == 0 &&
      sameBytes(borrowed.vertices, literal.vertices) && sameBytes(borrowed.packedVertices, literal.packedVertices) &&
      sameBytes(borrowed.instancedVertices, literal.instancedVertices) && sameBytes(borrowed.instances, literal.instances) &&
      sameBytes(borrowed.indices, literal.indices) && sameBytes(borrowed.draws, literal.draws) &&
      sameBytes(borrowed.shadowDraws, literal.shadowDraws) && borrowed.textures.empty() && literal.textures.empty() &&
      borrowed.uploadedVertexCount == literal.uploadedVertexCount && borrowed.uploadedIndexCount == literal.uploadedIndexCount &&
      std::memcmp(borrowed.clearColor, literal.clearColor, sizeof(borrowed.clearColor)) == 0,
      "borrowed BG schedule must preserve every geometry/instance/draw byte, metadata and diagnostic");
    return {};
  }
private:
  std::string error;
};

bool compositionBorrowLowering(const CoinRenderFramePlan & base)
{
  const char * option = std::getenv("COIN_RENDER_DISABLE_COMPOSITION_BORROW");
  struct Restore {
    bool present;
    std::string value;
    ~Restore() { coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMPOSITION_BORROW", present ? value.c_str() : nullptr); }
  } restore{option != nullptr, option ? option : ""};
  coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMPOSITION_BORROW", "0");
  auto frame = base;
  frame.revision = 4901;
  frame.vertices[0].position[0] = -1; frame.vertices[0].position[1] = -1;
  frame.vertices[1].position[0] = 1; frame.vertices[1].position[1] = -1;
  frame.vertices[2].position[1] = 1;
  for (auto & vertex : frame.vertices) vertex.normal[2] = 1;
  frame.renderStates.resize(300, frame.renderStates.front());
  frame.draws.resize(300, frame.draws.front());
  for (size_t i = 0; i < frame.draws.size(); ++i) {
    frame.draws[i].renderStateSlot = static_cast<uint32_t>(i);
    frame.renderStates[i].transparencyType = SoGLRenderAction::SORTED_OBJECT_BLEND;
    frame.renderStates[i].model.setTranslate(SbVec3f(float(i) * .01f, 0, 0));
  }
  CoinRenderTargetP target(SbVec2i32(4, 4));
  target.depthReadbackEnabled = false;
  auto * backend = new BorrowLoweringBackend;
  target.backend.reset(backend);
  return check(target.executeFrame(frame).status == CoinRenderBackendStatus::SUCCESS && backend->ok &&
               backend->submissions == 1 && !target.submissionPreflight(frame),
               "BG lowering oracle must use and release the real submission-local proof");
}

bool instancedCamera(const CoinRenderFramePlan & frame, const CoinBgfxPlan & anchor)
{
  bool ok = check(anchor.instancedCameraPatchable, "rigid nonsingular instances must admit camera reuse");
  CoinBgfxPlan working = anchor;
  std::string diagnostic;
  SbMatrix translated;
  translated.setTranslate(SbVec3f(0.4f,-0.3f,-1.2f));
  SbMatrix rotated;
  rotated.setRotate(SbRotation(SbVec3f(0,0,1), 0.4f));
  rotated[3][0] = 0.2f; rotated[3][1] = -0.6f; rotated[3][2] = 0.3f;
  SbMatrix other;
  other.setRotate(SbRotation(SbVec3f(1,2,-0.5f), -0.65f));
  other[3][0] = -0.5f; other[3][1] = 0.8f; other[3][2] = -0.4f;
  const SbMatrix cameras[] = {anchor.instanceCameraAnchorView, translated, rotated,
                              other, anchor.instanceCameraAnchorView};
  const auto near = [](const SbVec3f & actual, const SbVec3f & expected) {
    for (int channel = 0; channel < 3; ++channel)
      if (std::abs(actual[channel] - expected[channel]) >
          2.0e-5f * std::max(1.0f, std::abs(expected[channel]))) return false;
    return true;
  };
  for (const auto & camera : cameras) {
    CoinRenderFramePlan updated = frame;
    for (auto & state : updated.renderStates) {
      state.view = camera;
      state.projectionCoin.setScale(SbVec3f(1.1f,0.9f,1.0f));
    }
    // Reproject immutable anchor lights through world space, like Common's
    // camera overlay; a patch must publish these fresh eye-space uniforms.
    for (auto & light : updated.lightingStates[0].lights) {
      SbVec3f world, current;
      anchor.instanceCameraAnchorView.inverse().multVecMatrix(SbVec3f(light.position), world);
      camera.multVecMatrix(world, current);
      for (int channel = 0; channel < 3; ++channel) light.position[channel] = current[channel];
      anchor.instanceCameraAnchorView.inverse().multDirMatrix(SbVec3f(light.direction), world);
      camera.multDirMatrix(world, current);
      for (int channel = 0; channel < 3; ++channel) light.direction[channel] = current[channel];
    }
    std::vector<CoinBgfxDraw> patched;
    CoinBgfxPlan rebuilt;
    if (!check(CoinBgfxLowering::patchCamera(updated, 4, 4, false, working, patched, diagnostic) &&
               CoinBgfxLowering::lowerInstanced(updated, 4, 4, false, rebuilt, diagnostic),
               "instanced camera patch or rebake rejected a qualified rigid camera")) return false;
    ok &= check(sameBytes(anchor.instances, working.instances) &&
                sameBytes(anchor.instancedVertices, working.instancedVertices) &&
                sameBytes(anchor.indices, working.indices) && patched.size() == anchor.draws.size(),
                "camera patches must preserve instance/mesh payload and batch counts");
    SbMatrix delta, normalDelta;
    ok &= check(CoinRenderTransformCore::cameraDelta(anchor.instanceCameraAnchorView, camera, delta, normalDelta),
                "camera delta rejected a rigid numerical fixture");
    const bool changed = std::memcmp(camera.getValue(), anchor.instanceCameraAnchorView.getValue(), sizeof(SbMat)) != 0;
    for (size_t batch = 0; batch < patched.size(); ++batch) {
      const auto & draw = patched[batch];
      const auto & fresh = rebuilt.draws[batch];
      ok &= check(std::memcmp(draw.mvp, fresh.mvp, sizeof(draw.mvp)) == 0 &&
                  std::memcmp(draw.ambientLight, fresh.ambientLight, sizeof(draw.ambientLight)) == 0 &&
                  std::memcmp(draw.lightCount, fresh.lightCount, sizeof(draw.lightCount)) == 0 &&
                  std::memcmp(draw.lightPositionType, fresh.lightPositionType, sizeof(draw.lightPositionType)) == 0 &&
                  std::memcmp(draw.lightDirectionCutoff, fresh.lightDirectionCutoff, sizeof(draw.lightDirectionCutoff)) == 0 &&
                  draw.firstInstance == anchor.draws[batch].firstInstance &&
                  draw.instanceCount == anchor.draws[batch].instanceCount &&
                  draw.instanceCamera[3][3] == (changed ? 1.0f : 0.0f),
                  "camera patch changed traversal or stale projection/PHONG lighting uniforms");
      if (changed) for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row)
          ok &= check(draw.instanceCamera[column][row] == delta[row][column] &&
                      draw.instanceCamera[column + 3][row] == normalDelta[row][column],
                      "camera uniform columns changed Coin delta or normal delta");
        ok &= check(draw.instanceCamera[column][3] == delta[3][column], "camera uniform translation changed");
      }
    }
    for (size_t i = 0; i < frame.draws.size(); ++i) {
      const auto & state = frame.renderStates[frame.draws[i].renderStateSlot];
      const SbMatrix originalMv = state.model * anchor.instanceCameraAnchorView;
      const SbMatrix currentMv = state.model * camera;
      for (uint32_t j = 0; j < frame.draws[i].geometry.indexCount; ++j) {
        const auto & vertex = frame.vertices[frame.indices[frame.draws[i].geometry.firstIndex + j]];
        SbVec3f anchoredPoint, actualPoint, expectedPoint, anchoredNormal, actualNormal, expectedNormal;
        originalMv.multVecMatrix(SbVec3f(vertex.position), anchoredPoint);
        delta.multVecMatrix(anchoredPoint, actualPoint);
        currentMv.multVecMatrix(SbVec3f(vertex.position), expectedPoint);
        CoinRenderTransformCore::normalMatrix(originalMv).multDirMatrix(SbVec3f(vertex.normal), anchoredNormal);
        normalDelta.multDirMatrix(anchoredNormal, actualNormal);
        CoinRenderTransformCore::normalMatrix(currentMv).multDirMatrix(SbVec3f(vertex.normal), expectedNormal);
        if (actualNormal.normalize() == 0.0f) actualNormal.setValue(0,0,1);
        if (expectedNormal.normalize() == 0.0f) expectedNormal.setValue(0,0,1);
        ok &= check(near(actualPoint, expectedPoint) && near(actualNormal, expectedNormal),
                    "anchored camera transform differs from a fresh model-view/normal rebake");
      }
    }
    working.draws.swap(patched); // Simulate successful commits without moving V0.
    ok &= check(std::memcmp(working.instanceCameraAnchorView.getValue(), anchor.instanceCameraAnchorView.getValue(), sizeof(SbMat)) == 0,
                "consecutive camera patches must not drift the anchor");
  }
  const auto reject = [&](const CoinRenderFramePlan & invalid, const CoinBgfxPlan & base) {
    std::vector<CoinBgfxDraw> preserved = working.draws;
    return check(!CoinBgfxLowering::patchCamera(invalid, 4, 4, false, base, preserved, diagnostic) &&
                 sameBytes(preserved, working.draws), "camera decline must preserve output for the full rebake fallback");
  };
  CoinRenderFramePlan invalid = frame;
  for (auto & state : invalid.renderStates) state.view.setScale(SbVec3f(1.1f,1,1));
  ok &= reject(invalid, anchor);
  invalid = frame; invalid.renderStates[0].projectionCoin[0][0] = std::numeric_limits<float>::quiet_NaN(); ok &= reject(invalid, anchor);
  invalid = frame; invalid.renderStates[0].fogMode = CoinRenderFogMode::HAZE; ok &= reject(invalid, anchor);
  invalid = frame; invalid.viewports[0].x = 1; ok &= reject(invalid, anchor);
  invalid = frame; invalid.clearColor[0] = 0.9f; ok &= reject(invalid, anchor);
  invalid = frame; invalid.draws.pop_back(); ok &= reject(invalid, anchor);
  for (float scale : {0.0f, 1.0e-4f, 5.0e-4f}) {
    CoinRenderFramePlan unsafe = frame;
    unsafe.renderStates[0].model.setScale(SbVec3f(scale,scale,scale));
    CoinBgfxPlan unsafeAnchor;
    ok &= check(CoinBgfxLowering::lowerInstanced(unsafe, 4, 4, false, unsafeAnchor, diagnostic) &&
                !unsafeAnchor.instancedCameraPatchable,
                "singular/near-threshold models must retain lowering but decline camera reuse");
    for (auto & state : unsafe.renderStates) state.view = rotated;
    ok &= reject(unsafe, unsafeAnchor);
    CoinBgfxPlan rebaked;
    ok &= check(CoinBgfxLowering::lowerInstanced(unsafe, 4, 4, false, rebaked, diagnostic),
                "an unsafe normal camera patch must retain the full lowering fallback");
  }
  // Finite rigid views can still lose local features in float eye space. An
  // anchor at 1e8 must render normally, but cannot later recover those features
  // by applying a camera delta to its already rounded instance coordinates.
  CoinRenderFramePlan distant = frame;
  for (auto & state : distant.renderStates) state.view.setTranslate(SbVec3f(0,0,-1.0e8f));
  CoinBgfxPlan distantAnchor, distantGeneral;
  ok &= check(CoinBgfxLowering::lowerInstanced(distant, 4, 4, false, distantAnchor, diagnostic) &&
              !distantAnchor.instancedCameraPatchable &&
              CoinBgfxLowering::lower(distant, 4, 4, false, distantGeneral, diagnostic, false, true),
              "a distant camera must retain original lowering but disable anchored camera reuse");
  ok &= reject(distant, anchor);
  for (auto & state : distant.renderStates) state.view = anchor.instanceCameraAnchorView;
  ok &= reject(distant, distantAnchor);
  CoinBgfxPlan returnedAnchor;
  ok &= check(CoinBgfxLowering::lowerInstanced(distant, 4, 4, false, returnedAnchor, diagnostic) &&
              returnedAnchor.instancedCameraPatchable,
              "returning from a distant camera must qualify a freshly lowered safe anchor");
  return ok;
}

bool instancedOpaque(const CoinRenderFramePlan & base)
{
  CoinRenderFramePlan frame = base;
  frame.materials.push_back(frame.materials[0]);
  frame.materials[1].diffuse[0] = 0.25f;
  frame.materials[1].shininess = 0.7f;
  frame.vertices.resize(9); // Slot zero is not part of either source range.
  frame.indices = {1, 2, 3, 1, 3, 4, 5, 6, 7, 5, 7, 8};
  for (uint32_t group = 0; group < 2; ++group) for (uint32_t j = 0; j < 4; ++j) {
    auto & vertex = frame.vertices[1 + group * 4 + j];
    vertex.position[0] = float(j % 2); vertex.position[1] = float(j / 2);
    vertex.position[2] = -0.25f;
    vertex.normal[0] = j == 0 ? 1.0f : 0.0f;
    vertex.normal[1] = j == 1 ? 2.0f : 0.0f;
    vertex.normal[2] = j == 2 ? 0.0f : 1.0f;
    vertex.materialSlot = group;
  }
  frame.renderStates.resize(300, frame.renderStates[0]);
  frame.lightingStates[0].lights.resize(2);
  frame.lightingStates[0].lights[0].direction[0] = 0.3f;
  frame.lightingStates[0].lights[1].type = CoinRenderLightType::POINT;
  frame.lightingStates[0].lights[1].position[0] = 2.0f;
  frame.draws.resize(300, frame.draws[0]);
  for (uint32_t i = 0; i < 300; ++i) {
    auto & state = frame.renderStates[i];
    state.lightModel = CoinRenderLightModel::PHONG;
    state.materialSlot = i % 2;
    state.model = SbMatrix(1.0f + i * 0.003f,0.125f,0,0, 0,2,0.25f,0, 0,0,3,0,
                           i * 0.01f,0.25f,-0.5f,1);
    state.view.setTranslate(SbVec3f(-0.1f,0.2f,-0.3f));
    auto & draw = frame.draws[i];
    draw.renderStateSlot = i;
    draw.geometry.firstVertex = 1 + (i % 2) * 4;
    draw.geometry.vertexCount = 4;
    draw.geometry.firstIndex = (i % 2) * 6;
    draw.geometry.indexCount = 6;
  }
  CoinBgfxPlan instanced, general;
  std::string diagnostic;
  bool ok = check(CoinBgfxLowering::lowerInstanced(frame, 4, 4, false, instanced, diagnostic) &&
    CoinBgfxLowering::lower(frame, 4, 4, false, general, diagnostic, false, true) &&
    instanced.usesInstancing && instanced.vertices.empty() && instanced.instancedVertices.size() == 4 &&
    instanced.instances.size() == 300 && instanced.indices == std::vector<uint32_t>({0,1,2,0,2,3}) &&
    instanced.draws.size() == 1 && instanced.draws[0].firstInstance == 0 &&
    instanced.draws[0].instanceCount == 300,
    "shared meshes with different uniform materials must preserve traversal in one instance batch");
  if (!ok) return false;
  for (uint32_t i = 0; i < 300; ++i) {
    const auto & instance = instanced.instances[i];
    const SbMatrix mv = frame.renderStates[i].model * frame.renderStates[i].view;
    const SbMatrix normal = CoinRenderTransformCore::normalMatrix(mv);
    const auto values = mv.getValue(); const auto normalValues = normal.getValue();
    for (int column = 0; column < 3; ++column) {
      for (int row = 0; row < 3; ++row)
        ok &= check(instance.data[column][row] == values[row][column] &&
                    instance.data[column + 3][row] == normalValues[row][column],
                    "instance matrix columns changed Coin model-view or inverse-transpose values");
      ok &= check(instance.data[column][3] == values[3][column], "instance translation column changed");
    }
    const auto & material = frame.materials[i % 2];
    ok &= check(std::memcmp(instance.data[6], material.diffuse, sizeof(material.diffuse)) == 0 &&
                std::memcmp(instance.data[7], material.ambient, sizeof(material.ambient)) == 0 &&
                std::memcmp(instance.data[8], material.specular, sizeof(material.specular)) == 0 &&
                std::memcmp(instance.data[9], material.emission, sizeof(material.emission)) == 0 &&
                instance.data[3][3] == material.shininess && instance.data[4][3] == 1.0f,
                "per-instance material values or PHONG flag changed");
    for (uint32_t j = 0; j < 6; ++j) {
      const auto & meshVertex = instanced.instancedVertices[instanced.indices[j]];
      const auto & reference = general.vertices[general.indices[i * 6 + j]];
      SbVec3f position, viewNormal;
      mv.multVecMatrix(SbVec3f(meshVertex.position), position);
      normal.multDirMatrix(SbVec3f(meshVertex.normal), viewNormal);
      if (viewNormal.normalize() == 0.0f) viewNormal.setValue(0,0,1);
      ok &= check(std::memcmp(position.getValue(), reference.viewPosition, sizeof(reference.viewPosition)) == 0 &&
                  std::memcmp(viewNormal.getValue(), reference.viewNormal, sizeof(reference.viewNormal)) == 0,
                  "instance mesh/matrix reconstruction changed triangle position or normal bytes");
    }
  }
  ok &= instancedCamera(frame, instanced);
  // All instances share an identical plane/transform. Keeping the alternating
  // material sequence is essential for the traversal winner under LEQUAL.
  CoinRenderFramePlan coplanar = frame;
  for (auto & state : coplanar.renderStates) {
    state.depthFunction = CoinRenderDepthFunction::LEQUAL;
    state.model = coplanar.renderStates[0].model;
  }
  CoinBgfxPlan coplanarInstances;
  ok &= check(CoinBgfxLowering::lowerInstanced(coplanar, 4, 4, false, coplanarInstances, diagnostic) &&
              coplanarInstances.draws.size() == 1 && coplanarInstances.instances.size() == 300 &&
              coplanarInstances.draws[0].depthFunction == CoinRenderDepthFunction::LEQUAL &&
              coplanarInstances.draws[0].firstInstance == 0 && coplanarInstances.draws[0].instanceCount == 300,
              "uniform LEQUAL depth must preserve its comparison and ordered coplanar instance batch");
  for (uint32_t i = 0; i < coplanarInstances.instances.size(); ++i)
    ok &= check(std::memcmp(coplanarInstances.instances[i].data[6], frame.materials[i % 2].diffuse,
                            sizeof(frame.materials[i % 2].diffuse)) == 0,
                "coplanar alternating materials must retain traversal order");
  frame.vertices[5].position[0] += 0.25f;
  ok &= check(CoinBgfxLowering::lowerInstanced(frame, 4, 4, false, instanced, diagnostic) &&
              instanced.instancedVertices.size() == 8 && instanced.draws.size() == 300,
              "nonconsecutive different meshes must retain separate ordered batches");
  for (uint32_t i = 0; i < instanced.draws.size(); ++i)
    ok &= check(instanced.draws[i].firstInstance == i && instanced.draws[i].instanceCount == 1 &&
                instanced.draws[i].firstVertex == (i % 2) * 4,
                "instance grouping reordered source mesh occurrences");
  CoinBgfxPlan retained = instanced;
  ok &= check(CoinBgfxLowering::retainForReuse(retained) && retained.instances.size() == 300,
              "instanced static reuse must retain instance counts and payload ownership");
  std::vector<CoinBgfxVertexRange> patches;
  std::vector<CoinBgfxDraw> cameraDraws;
  ok &= check(!CoinBgfxLowering::materialPatchRanges(instanced, instanced, patches) &&
              CoinBgfxLowering::patchCamera(frame, 4, 4, false, instanced, cameraDraws, diagnostic),
              "instanced material changes rebuild data while qualified camera changes retain it");
  const auto decline = [&](const CoinRenderFramePlan & unsupported) {
    CoinBgfxPlan preserved = instanced;
    return check(!CoinBgfxLowering::lowerInstanced(unsupported, 4, 4, false, preserved, diagnostic) &&
                 preserved.usesInstancing && sameBytes(preserved.instances, instanced.instances) &&
                 sameBytes(preserved.instancedVertices, instanced.instancedVertices) &&
                 sameBytes(preserved.indices, instanced.indices) && sameBytes(preserved.draws, instanced.draws),
                 "an unqualified instance profile must leave fallback output untouched");
  };
  CoinRenderFramePlan unsupported = frame;
  unsupported.vertices[1].materialSlot = 1; ok &= decline(unsupported);
  unsupported = frame; unsupported.renderStates[1].model[0][3] = 0.1f; ok &= decline(unsupported);
  unsupported = frame; unsupported.renderStates[1].fogMode = CoinRenderFogMode::HAZE; ok &= decline(unsupported);
  unsupported = frame; unsupported.renderStates[1].clipPlanesWorld.emplace_back(SbVec3f(1,0,0),0); ok &= decline(unsupported);
  unsupported = frame; unsupported.renderStates[1].depthFunction = CoinRenderDepthFunction::GEQUAL; ok &= decline(unsupported);
  unsupported = frame; unsupported.renderStates[1].depthFunction = CoinRenderDepthFunction::LEQUAL; ok &= decline(unsupported);
  unsupported = frame; unsupported.renderStates[1].polygonOffsetEnabled = true; ok &= decline(unsupported);
  unsupported = frame; unsupported.vertices[1].screenSpaceW = 0.5f; ok &= decline(unsupported);
  unsupported = frame; unsupported.vertices[1].fogEyeDepth = 1.0f; ok &= decline(unsupported);
  unsupported = frame; unsupported.renderStates[1].hasTexture = true; ok &= decline(unsupported);
  unsupported = frame; unsupported.shadowGroups.emplace_back(); ok &= decline(unsupported);
  unsupported = frame; unsupported.materials[0].diffuse[3] = 0.5f;
  unsupported.materials[0].transparency = 0.5f; ok &= decline(unsupported);
  unsupported = frame; unsupported.materials[0].diffuse[0] = std::numeric_limits<float>::quiet_NaN(); ok &= decline(unsupported);
  unsupported = frame; unsupported.vertices[1].normal[0] = std::numeric_limits<float>::max() / 2.0f; ok &= decline(unsupported);
  unsupported = frame; unsupported.vertices[1].position[0] = std::numeric_limits<float>::max() / 2.0f; ok &= decline(unsupported);
  unsupported = frame; unsupported.renderStates[1].model.setScale(SbVec3f(1.0e9f,1,1)); ok &= decline(unsupported);
  unsupported = frame; unsupported.renderStates[1].model.setScale(SbVec3f(1.0e-9f,1,1)); ok &= decline(unsupported);
  unsupported = frame; unsupported.renderStates[1].transparencyType = SoGLRenderAction::SCREEN_DOOR;
  unsupported.renderStates[1].screenDoorTransparency = 0.25f; ok &= decline(unsupported);
  unsupported = frame; unsupported.draws.back().geometry.firstVertex = 2;
  unsupported.draws.back().geometry.vertexCount = 3; ok &= decline(unsupported);
  return ok;
}

bool diagonalInstancing(const CoinRenderFramePlan & base)
{
  // Independent captured geometry: no node/type recognition is involved. Faces
  // retain their authored normal attributes and CCW triangle order.
  const float corners[24][3] = {
    {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1},
    {1,-1,-1},{-1,-1,-1},{-1,1,-1},{1,1,-1},
    {1,-1,1},{1,-1,-1},{1,1,-1},{1,1,1},
    {-1,-1,-1},{-1,-1,1},{-1,1,1},{-1,1,-1},
    {-1,1,1},{1,1,1},{1,1,-1},{-1,1,-1},
    {-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1}
  };
  const float normals[6][3] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
  constexpr size_t count = 1300; // Exceeds both former source-span/mesh limits.
  CoinRenderFramePlan frame = base;
  frame.vertices.resize(count * 24);
  frame.indices.resize(count * 36);
  frame.draws.resize(count, base.draws.front());
  frame.renderStates.resize(count, base.renderStates.front());
  frame.materials.resize(count, base.materials.front());
  for (size_t occurrence = 0; occurrence < count; ++occurrence) {
    const float extent[3] = {0.75f + float(occurrence) * .001f,
                             1.25f + float(occurrence) * .002f,
                             0.5f + float(occurrence) * .003f};
    auto & state = frame.renderStates[occurrence];
    state.lightModel = CoinRenderLightModel::PHONG;
    state.cullMode = CoinRenderCullMode::BACK;
    state.frontFace = CoinRenderFrontFace::CCW;
    state.materialSlot = static_cast<uint32_t>(occurrence);
    state.model = SbMatrix(occurrence % 13 ? 1.3f : -1.3f,.125f,0,0,
                           0,2,.25f,0, 0,0,.7f,0, float(occurrence)*.001f,.25f,-.5f,1);
    state.view.setTranslate(SbVec3f(-.2f,.1f,-3));
    frame.materials[occurrence].diffuse[0] = .2f + float(occurrence % 17) * .03f;
    auto & draw = frame.draws[occurrence];
    draw.renderStateSlot = static_cast<uint32_t>(occurrence);
    draw.geometry.firstVertex = static_cast<uint32_t>(occurrence * 24);
    draw.geometry.vertexCount = 24;
    draw.geometry.firstIndex = static_cast<uint32_t>(occurrence * 36);
    draw.geometry.indexCount = 36;
    for (size_t vertex = 0; vertex < 24; ++vertex) {
      auto & source = frame.vertices[occurrence * 24 + vertex];
      source.materialSlot = static_cast<uint32_t>(occurrence);
      for (int axis = 0; axis < 3; ++axis) {
        source.position[axis] = corners[vertex][axis] * extent[axis];
        source.normal[axis] = normals[vertex / 4][axis];
      }
    }
    for (uint32_t face = 0; face < 6; ++face) {
      const uint32_t first = static_cast<uint32_t>(occurrence * 24) + face * 4;
      const uint32_t triangle[6] = {first,first+1,first+2,first,first+2,first+3};
      std::copy(triangle, triangle + 6, frame.indices.begin() + occurrence * 36 + face * 6);
    }
  }
  CoinBgfxPlan factored, general;
  std::string diagnostic;
  bool ok = check(CoinBgfxLowering::lowerInstanced(frame, 4, 4, false, factored, diagnostic) &&
                  CoinBgfxLowering::lower(frame, 4, 4, false, general, diagnostic) &&
                  factored.instancedVertices.size() == 24 && factored.indices.size() == 36 &&
                  factored.instances.size() == count && factored.draws.size() == 1 &&
                  factored.draws[0].instanceCount == count && factored.draws[0].cullMode == CoinRenderCullMode::BACK &&
                  factored.draws[0].frontFace == CoinRenderFrontFace::CCW,
                  "distinct resized spans and materials must share one proven mesh in traversal order");
  if (!ok) return false;
  for (size_t occurrence = 0; occurrence < count; ++occurrence) {
    const auto & instance = factored.instances[occurrence];
    const auto & state = frame.renderStates[occurrence];
    const SbMatrix authoredNormal = CoinRenderTransformCore::normalMatrix(state.model * state.view);
    SbMatrix position = SbMatrix::identity();
    for (int column = 0; column < 3; ++column) {
      for (int row = 0; row < 3; ++row) {
        position[row][column] = instance.data[column][row];
        ok &= check(instance.data[column + 3][row] == authoredNormal[row][column],
                    "factoring positions must preserve authored normal matrix bytes, including shear/reflection");
      }
      position[3][column] = instance.data[column][3];
    }
    ok &= check(std::memcmp(instance.data[6], frame.materials[occurrence].diffuse, sizeof(instance.data[6])) == 0,
                "uniform per-occurrence materials must retain their original order");
    for (size_t index = 0; index < 36; ++index) {
      const auto & vertex = factored.instancedVertices[factored.indices[index]];
      const auto & reference = general.vertices[general.indices[occurrence * 36 + index]];
      SbVec3f actual;
      position.multVecMatrix(SbVec3f(vertex.position), actual);
      for (int axis = 0; axis < 3; ++axis)
        ok &= check(std::abs(actual[axis] - reference.viewPosition[axis]) <=
                      2.0e-5f * std::max(1.0f, std::abs(reference.viewPosition[axis])),
                    "factored position must match general lowering without changing triangle winding/order");
    }
  }
  CoinRenderFramePlan material = frame;
  for (auto & value : material.materials) { value.diffuse[1] = .65f; value.shininess = .4f; }
  CoinBgfxPlan materialPlan;
  ok &= check(CoinBgfxLowering::lowerInstanced(material, 4, 4, false, materialPlan, diagnostic) &&
              sameBytes(materialPlan.instancedVertices, factored.instancedVertices) &&
              materialPlan.indices == factored.indices && materialPlan.draws.size() == 1 &&
              materialPlan.instances.front().data[6][1] == .65f && materialPlan.instances.back().data[3][3] == .4f,
              "material updates must preserve normalized geometry and remain per-instance data");
  // Different authored attributes, reflected geometry and reverse winding must
  // remain A/B/A rather than regrouping nonconsecutive occurrences.
  for (int change = 0; change < 3; ++change) {
    CoinRenderFramePlan different = frame;
    if (change == 0) different.vertices[24].normal[0] = .125f;
    if (change == 1) for (size_t vertex = 24; vertex < 48; ++vertex) different.vertices[vertex].position[0] *= -1;
    if (change == 2) std::swap(different.indices[36], different.indices[37]);
    CoinBgfxPlan distinct;
    ok &= check(CoinBgfxLowering::lowerInstanced(different, 4, 4, false, distinct, diagnostic) &&
                distinct.instancedVertices.size() == 48 && distinct.draws.size() == 3 &&
                distinct.draws[0].firstInstance == 0 && distinct.draws[0].instanceCount == 1 &&
                distinct.draws[1].firstInstance == 1 && distinct.draws[1].instanceCount == 1 &&
                distinct.draws[2].firstInstance == 2 && distinct.draws[2].instanceCount == count - 2 &&
                distinct.draws[0].firstVertex == distinct.draws[2].firstVertex &&
                distinct.draws[1].firstVertex != distinct.draws[0].firstVertex,
                "normals/reflection/winding must remain distinguished and preserve A/B/A order");
  }
  for (float extent : {0.0f, std::numeric_limits<float>::denorm_min()}) {
    CoinRenderFramePlan unsupported = frame;
    for (size_t vertex = 0; vertex < 24; ++vertex) unsupported.vertices[vertex].position[0] = corners[vertex][0] * extent;
    CoinBgfxPlan exact;
    ok &= check(CoinBgfxLowering::lowerInstanced(unsupported, 4, 4, false, exact, diagnostic) &&
                exact.instancedVertices.size() == 48 &&
                std::memcmp(&exact.instancedVertices[0].position[0],
                            &unsupported.vertices[0].position[0], sizeof(float)) == 0,
                "zero/subnormal factors must preserve the original exact geometry path");
  }
  CoinRenderFramePlan interior = frame;
  interior.vertices[0].position[0] += .125f;
  CoinBgfxPlan exact;
  ok &= check(CoinBgfxLowering::lowerInstanced(interior, 4, 4, false, exact, diagnostic) &&
              exact.instancedVertices.size() == 48 && exact.instancedVertices[0].position[0] == interior.vertices[0].position[0],
              "non-grid coordinates must fail normalization without partially modifying their source mesh");
  // Exercise the new span bound with shared vertex storage and unique index
  // ranges, keeping this boundary regression small in CPU geometry memory.
  CoinRenderFramePlan spans = base;
  spans.renderStates[0].lightModel = CoinRenderLightModel::PHONG;
  spans.draws.resize(65537, spans.draws[0]);
  spans.indices.resize(spans.draws.size() * 3);
  for (size_t draw = 0; draw < spans.draws.size(); ++draw) {
    spans.draws[draw].geometry.firstIndex = static_cast<uint32_t>(draw * 3);
    for (size_t index = 0; index < 3; ++index) spans.indices[draw * 3 + index] = static_cast<uint32_t>(index);
  }
  CoinBgfxPlan preserved = factored;
  ok &= check(!CoinBgfxLowering::lowerInstanced(spans, 4, 4, false, preserved, diagnostic) &&
              diagnostic.find("source span/metadata budget") != std::string::npos &&
              sameBytes(preserved.instances, factored.instances) && sameBytes(preserved.instancedVertices, factored.instancedVertices),
              "bounded span metadata must decline without publishing a partial instance plan");
  return ok;
}
}

int main()
{
  SoDB::init();
  CoinRenderFramePlan frame;
  frame.clearColor = SbColor4f(0.1f, 0.2f, 0.3f, 1.0f);
  frame.materials.push_back(CoinRenderMaterialSnapshot());
  frame.lightingStates.push_back(CoinRenderLightingSnapshot());
  frame.cameras.push_back(CoinRenderCameraSnapshot());
  CoinRenderViewportSnapshot viewport;
  viewport.width = 4;
  viewport.height = 4;
  frame.viewports.push_back(viewport);
  CoinRenderRenderStateSnapshot state;
  state.lightModel = CoinRenderLightModel::BASE_COLOR;
  frame.renderStates.push_back(state);
  frame.vertices.resize(3);
  frame.indices = {0, 1, 2};
  CoinRenderDrawPacket draw;
  draw.geometry.vertexCount = 3;
  draw.geometry.indexCount = 3;
  frame.draws.push_back(draw);

  bool ok = compositionBorrowLowering(frame);
  CoinBgfxProgramCache programCache(16, 12, 2);
  const uint8_t binary[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  const uint8_t replacement[8] = {7, 6, 5, 4, 3, 2, 1, 0};
  uint8_t cachedBinary[8] = {};
  ok &= check(programCache.size(11) == 0 && programCache.write(11, binary, sizeof(binary)) &&
              programCache.size(11) == sizeof(binary) &&
              !programCache.write(11, replacement, sizeof(replacement)) &&
              programCache.read(11, cachedBinary, sizeof(cachedBinary)) &&
              std::memcmp(binary, cachedBinary, sizeof(binary)) == 0 &&
              !programCache.read(11, cachedBinary, 7) && !programCache.read(12, cachedBinary, 8),
              "program cache must preserve immutable size/read snapshots");
  ok &= check(!programCache.write(12, binary, 4) &&
              !programCache.write(12, binary, 13) &&
              programCache.write(12, binary, sizeof(binary)) &&
              !programCache.write(13, binary, sizeof(binary)) &&
              programCache.size(13) == 0,
              "program cache exceeded its binary size or total retention bounds");
  std::string diagnostic;
  CoinBgfxPlan plan;
  frame.shadowGroups.push_back(CoinRenderShadowGroupSnapshot());
  ok &= check(!CoinBgfxLowering::lower(frame, 4, 4, false, plan, diagnostic) &&
              diagnostic.find("BGFX shadow maps") != std::string::npos &&
              plan.draws.empty(),
              "active shadow group silently rendered without shadows");
  frame.shadowGroups.clear();
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false, plan, diagnostic),
              "valid BASE_COLOR triangle rejected");
  ok &= check(plan.draws[0].viewport[0] == 0 && plan.draws[0].viewport[1] == 0 &&
              plan.draws[0].viewport[2] == 4 && plan.draws[0].viewport[3] == 4,
              "full viewport was not preserved during lowering");
  ok &= check(plan.vertices.size() == 3 && plan.indices.size() == 3 &&
              plan.draws.size() == 1, "geometry structure changed");
  CoinBgfxPlan retained = plan;
  ok &= check(CoinBgfxLowering::retainForReuse(retained) &&
              retained.vertices.size() == 3 && retained.indices.size() == 3,
              "small cached plans must retain material patch inputs");
  retained = plan;
  ok &= check(!CoinBgfxLowering::retainForReuse(retained, 0, 0) &&
              retained.vertices.size() == 3 && retained.draws.size() == 1,
              "metadata budget rejection must not modify the candidate");
  retained = plan;
  ok &= check(CoinBgfxLowering::retainForReuse(retained, 0) &&
              retained.vertices.capacity() == 0 && retained.indices.capacity() == 0 &&
              retained.vertexCount() == 3 && retained.uploadedIndexCount == 3 &&
              retained.draws[0].indexCount == 3 &&
              std::memcmp(retained.draws[0].mvp, plan.draws[0].mvp, sizeof(plan.draws[0].mvp)) == 0,
              "large GPU cache must release CPU geometry and preserve draw metadata");
  ok &= check(CoinBgfxLowering::retainForReuse(retained, 0) &&
              retained.vertexCount() == 3 && retained.uploadedIndexCount == 3,
              "retaining released geometry lost the uploaded counts");
  retained = plan;
  retained.uploadedVertexCount = retained.vertices.size();
  std::vector<CoinBgfxVertex> uploadOwner;
  const CoinBgfxVertex * uploadData = retained.vertices.data();
  uploadOwner.swap(retained.vertices);
  ok &= check(CoinBgfxLowering::retainForReuse(retained) &&
              retained.vertexCount() == 3 && retained.uploadedIndexCount == 3 &&
              retained.draws[0].indexCount == 3 &&
              uploadOwner.data() == uploadData &&
              std::memcmp(uploadOwner.data(), plan.vertices.data(),
                          uploadOwner.size() * sizeof(CoinBgfxVertex)) == 0,
              "transferred upload must preserve ownership, data and cache counts");
  retained.uploadedVertexCount = 32u * 1024u * 1024u / sizeof(CoinBgfxVertex) + 1u;
  ok &= check(CoinBgfxLowering::retainForReuse(retained) &&
              retained.indices.capacity() == 0 && retained.uploadedIndexCount == 3,
              "transferred large geometry must also release unused CPU indices");
  ok &= check(std::abs(plan.vertices[0].color[0] - 0.8f) < 1e-6f &&
              std::abs(plan.vertices[0].color[3] - 1.0f) < 1e-6f,
              "Coin diffuse color lost floating-point precision");
  ok &= check(plan.clearColor[0] == frame.clearColor[0] &&
              plan.clearColor[1] == frame.clearColor[1],
              "Coin clear color was quantized before GPU submission");
  CoinRenderFramePlan city = frame;
  ok &= instancedOpaque(frame);
  ok &= diagonalInstancing(frame);
  {
    CoinRenderFramePlan materialFrame = frame;
    materialFrame.materials.push_back(materialFrame.materials[0]);
    materialFrame.materials[1].diffuse[0] = std::numeric_limits<float>::quiet_NaN();
    CoinBgfxPlan materialPlan;
    ok &= check(CoinBgfxLowering::lower(materialFrame, 4, 4, true, materialPlan, diagnostic),
                "unused RGB material must not change the existing validation contract");
    materialFrame.vertices[0].materialSlot = 1;
    ok &= check(!CoinBgfxLowering::lower(materialFrame, 4, 4, true, materialPlan, diagnostic) &&
                diagnostic.find("non-finite material color") != std::string::npos,
                "material color memoization accepted a referenced NaN");
    materialFrame.materials[1].diffuse[0] = 0.25f;
    ok &= check(CoinBgfxLowering::lower(materialFrame, 4, 4, true, materialPlan, diagnostic),
                "material validation memoization leaked between frames");
  }
  {
    CoinRenderFramePlan largeOpaque = frame;
    largeOpaque.renderStates[0].lightModel = CoinRenderLightModel::PHONG;
    largeOpaque.renderStates[0].transparencyType = SoGLRenderAction::BLEND;
    largeOpaque.renderStates[0].model.setScale(SbVec3f(2, 3, 4));
    largeOpaque.vertices[0].normal[1] = largeOpaque.vertices[1].normal[1] = 2;
    largeOpaque.vertices[1].normal[0] = -0.0f;
    largeOpaque.draws.resize(65000, largeOpaque.draws[0]);
    CoinBgfxPlan full, compact;
    ok &= sharedRangeMatchesGeneral(largeOpaque, true);
    const bool fullOk = CoinBgfxLowering::lower(largeOpaque, 4, 4, true, full, diagnostic, false, true);
    if (!fullOk) std::cerr << "Full large plan: " << diagnostic << '\n';
    const bool compactOk = CoinBgfxLowering::lower(largeOpaque, 4, 4, true, compact, diagnostic, false, true, nullptr, true);
    if (!compactOk) std::cerr << "Compact large plan: " << diagnostic << '\n';
    if (full.vertexCount() != compact.vertexCount() || !compact.usesCompactVertices || full.indices != compact.indices)
      std::cerr << "Compact structure: " << full.vertexCount() << '/' << compact.vertexCount()
                << " packed=" << compact.usesCompactVertices << " fullVertices=" << compact.vertices.size()
                << " indices=" << full.indices.size() << '/' << compact.indices.size()
                << " draws=" << full.draws.size() << '/' << compact.draws.size() << '\n';
    ok &= check(fullOk && compactOk &&
                compact.usesCompactVertices && compact.vertices.empty() &&
                full.vertexCount() == compact.vertexCount() && full.indices == compact.indices &&
                full.draws.size() == compact.draws.size(),
                "compact opaque lowering changed geometry structure");
    for (size_t i = 0; i < compact.packedVertices.size(); ++i)
      ok &= check(std::memcmp(compact.packedVertices[i].data(), &full.vertices[i],
                             sizeof(CoinBgfxVertexPrefix)) == 0,
                  "compact opaque lowering changed a retained attribute bit");
    std::vector<CoinBgfxVertexRange> ranges;
    ok &= check(!CoinBgfxLowering::materialPatchRanges(compact, compact, ranges) &&
                CoinBgfxLowering::retainForReuse(compact) &&
                compact.packedVertices.capacity() == 0 && compact.indices.capacity() == 0 &&
                compact.vertexCount() == full.vertexCount(),
                "compact GPU cache must release CPU data and preserve counts");
    largeOpaque.draws[1].clearDepthBefore = true;
    largeOpaque.draws[1].renderLayer = 1;
    ok &= check(CoinBgfxLowering::lower(largeOpaque, 4, 4, true, compact, diagnostic, false, true, nullptr, true) &&
                !compact.usesCompactVertices && compact.packedVertices.empty(),
                "compact lowering crossed an opaque profile barrier");
  }
  // Compare the complete attribute stream against explicitly expanded input,
  // including nonuniform materials and reuse under different model matrices.
  CoinRenderFramePlan indexed = frame;
  indexed.vertices.resize(5); // Slot zero is outside the draw's range.
  indexed.indices = {1, 2, 3, 1, 3, 4};
  indexed.draws[0].geometry.firstVertex = 1;
  indexed.draws[0].geometry.vertexCount = 4;
  indexed.draws[0].geometry.indexCount = 6;
  indexed.materials.push_back(indexed.materials[0]);
  indexed.materials[1].diffuse[1] = 0.35f;
  for (size_t i = 1; i < indexed.vertices.size(); ++i) {
    indexed.vertices[i].position[0] = float(i % 2) * 0.1f;
    indexed.vertices[i].position[1] = float(i / 2) * 0.1f;
    indexed.vertices[i].normal[2] = 1.0f;
    indexed.vertices[i].materialSlot = static_cast<uint32_t>(i % 2);
  }
  for (bool transparent : {false, true}) {
    indexed.materials[0].transparency = indexed.materials[1].transparency = transparent ? 0.5f : 0.0f;
    indexed.materials[0].diffuse[3] = indexed.materials[1].diffuse[3] = transparent ? 0.5f : 1.0f;
    indexed.draws.resize(300, indexed.draws[0]);
    indexed.renderStates.resize(300, indexed.renderStates[0]);
    for (size_t i = 0; i < indexed.draws.size(); ++i) {
      indexed.draws[i].renderStateSlot = static_cast<uint32_t>(i);
      indexed.renderStates[i].lightModel = CoinRenderLightModel::PHONG;
      indexed.renderStates[i].transparencyType = SoGLRenderAction::BLEND;
      indexed.renderStates[i].model.setTranslate(SbVec3f(float(i) * 0.001f, 0, 0));
    }
    CoinRenderFramePlan expanded = indexed;
    expanded.vertices.clear();
    for (uint32_t index : indexed.indices) expanded.vertices.push_back(indexed.vertices[index]);
    expanded.indices = {0, 1, 2, 3, 4, 5};
    for (auto & packet : expanded.draws) {
      packet.geometry.firstVertex = 0;
      packet.geometry.vertexCount = 6;
    }
    CoinBgfxPlan compact, reference;
    ok &= sharedRangeMatchesGeneral(indexed);
    const bool lowered = CoinBgfxLowering::lower(indexed, 4, 4, true, compact, diagnostic, false, true) &&
      CoinBgfxLowering::lower(expanded, 4, 4, true, reference, diagnostic, false, true);
    const size_t expectedVertices = transparent ? 1800 : 1200;
    ok &= check(lowered && compact.vertices.size() == expectedVertices && reference.vertices.size() == 1800 &&
                compact.indices.size() == reference.indices.size() && compact.draws.size() == reference.draws.size(),
                "indexed lowering failed to preserve draw structure or compact repeated vertices");
    if (lowered) {
      for (size_t i = 0; i < compact.indices.size(); ++i)
        ok &= check(std::memcmp(&compact.vertices[compact.indices[i]],
                    &reference.vertices[reference.indices[i]], sizeof(CoinBgfxVertex)) == 0,
                    "indexed lowering changed a triangle attribute or transform");
      for (size_t i = 0; i < compact.draws.size(); ++i)
        ok &= check(compact.draws[i].materialSignature == reference.draws[i].materialSignature &&
                    compact.draws[i].indexCount == reference.draws[i].indexCount,
                    "indexed lowering changed material hashing or triangle order");
    }
    if (!transparent) {
      CoinRenderFramePlan partial = indexed;
      for (size_t i = 1; i < partial.draws.size(); i += 2) {
        partial.draws[i].geometry.firstIndex = 3;
        partial.draws[i].geometry.indexCount = 3;
      }
      partial.renderStates[3].model[0][3] = 0.125f;
      partial.renderStates[5].fogMode = CoinRenderFogMode::HAZE;
      partial.renderStates[5].fogEnd = 10.0f;
      partial.renderStates[7].clipPlanesWorld.emplace_back(SbVec3f(1, 0, 0), 0.25f);
      partial.renderStates[9].depthWrite = false;
      partial.renderStates[11].polygonOffsetEnabled = true;
      partial.renderStates[11].polygonOffsetUnits = -1.0f;
      ok &= sharedRangeMatchesGeneral(partial);
      partial.vertices[2].screenSpaceW = 0.5f;
      partial.vertices[3].fogEyeDepth = 2.0f;
      ok &= sharedRangeMatchesGeneral(partial);

      CoinRenderFramePlan invalid = indexed;
      invalid.draws.back().geometry.firstVertex = 2;
      invalid.draws.back().geometry.vertexCount = 3;
      ok &= sharedRangeMatchesGeneral(invalid, false, false);
      invalid = indexed;
      invalid.materials[1].diffuse[0] = std::numeric_limits<float>::quiet_NaN();
      ok &= sharedRangeMatchesGeneral(invalid, false, false);
    }
    else {
      CoinRenderFramePlan sorted = indexed;
      for (auto & sortedState : sorted.renderStates)
        sortedState.transparencyType = SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND;
      ok &= sharedRangeMatchesGeneral(sorted);
    }
  }
  indexed.draws.resize(1);
  indexed.vertices.resize(4098);
  indexed.draws[0].geometry.vertexCount = 4097;
  CoinBgfxPlan bounded;
  ok &= check(CoinBgfxLowering::lower(indexed, 4, 4, true, bounded, diagnostic) &&
              bounded.vertices.size() == 6,
              "large vertex ranges must retain the bounded-scratch expanded path");
  city.draws.resize(300, frame.draws[0]);
  city.renderStates.resize(300, frame.renderStates[0]);
  city.vertices[0].position[0] = -0.1f; city.vertices[0].position[1] = -0.1f;
  city.vertices[1].position[0] = 0.1f; city.vertices[1].position[1] = -0.1f;
  city.vertices[2].position[1] = 0.1f; city.vertices[2].position[2] = -0.3f;
  city.materials.push_back(city.materials[0]);
  city.materials[1].diffuse[0] = 0.25f;
  city.vertices[1].materialSlot = 1;
  for (size_t i = 0; i < city.draws.size(); ++i) {
    city.draws[i].renderStateSlot = static_cast<uint32_t>(i);
    city.renderStates[i].lightModel = CoinRenderLightModel::PHONG;
    city.renderStates[i].transparencyType = SoGLRenderAction::BLEND;
    city.renderStates[i].model.setTranslate(SbVec3f(float(i) * 0.001f, 0, 0));
  }
  CoinBgfxPlan individual, batched;
  ok &= check(CoinBgfxLowering::lower(city, 4, 4, false, individual, diagnostic) &&
              CoinBgfxLowering::lower(city, 4, 4, false, batched, diagnostic, false, true) &&
              individual.draws.size() == 300 && batched.draws.size() == 1 &&
              batched.draws[0].indexCount == 900 && batched.vertices.size() == 900 &&
              batched.indices == individual.indices,
              "compatible PHONG geometry did not form a contiguous opaque batch");
  for (size_t i = 0; i < individual.draws.size() && batched.draws.size() == 1; ++i) {
    const auto & original = individual.vertices[i * 3 + 1];
    const auto & baked = batched.vertices[i * 3 + 1];
    SbMat originalArray, batchArray;
    std::memcpy(originalArray, individual.draws[i].mvp, sizeof(originalArray));
    std::memcpy(batchArray, batched.draws[0].mvp, sizeof(batchArray));
    SbMatrix originalMvp(originalArray), batchMvp(batchArray);
    SbVec4f before, after;
    originalMvp.multVecMatrix(SbVec4f(original.position[0], original.position[1], original.position[2], 1), before);
    batchMvp.multVecMatrix(SbVec4f(baked.position[0], baked.position[1], baked.position[2], 1), after);
    for (int channel = 0; channel < 4; ++channel)
      ok &= check(std::abs(before[channel] - after[channel]) < 1e-6f,
                  "opaque batching changed homogeneous clip coordinates");
    ok &= check(std::memcmp(original.viewNormal, baked.viewNormal, sizeof(baked.viewNormal)) == 0 &&
                std::memcmp(original.color, baked.color, sizeof(baked.color)) == 0,
                "opaque batching changed lighting or material attributes");
  }
  city.draws[150].clearDepthBefore = true;
  city.draws[150].renderLayer = 1;
  ok &= check(CoinBgfxLowering::lower(city, 4, 4, false, batched, diagnostic, false, true) &&
              batched.draws.size() == 2 && batched.draws[1].clearDepthBefore &&
              batched.draws[1].renderLayer == 1,
              "opaque batching crossed a depth clear barrier");
  city.draws[150].clearDepthBefore = false;
  city.draws[150].renderLayer = 0;
  city.renderStates[150].fogMode = CoinRenderFogMode::HAZE;
  ok &= check(CoinBgfxLowering::lower(city, 4, 4, false, batched, diagnostic, false, true) &&
              batched.draws.size() == 3,
              "opaque batching crossed an incompatible fog uniform");
  city.renderStates[150].fogMode = CoinRenderFogMode::NONE;
  city.renderStates[150].model[0][3] = 0.25f;
  ok &= check(CoinBgfxLowering::lower(city, 4, 4, false, batched, diagnostic, false, true) &&
              batched.draws.size() == 3,
              "opaque batching accepted a projective model transform");
  city.renderStates[150].model[0][3] = 0.0f;
  city.vertices[0].screenSpaceW = 1.5f;
  ok &= check(CoinBgfxLowering::lower(city, 4, 4, false, batched, diagnostic, false, true) &&
              batched.draws.size() == 300,
              "opaque batching changed stroke perspective interpolation");
  city.vertices[0].screenSpaceW = 1.0f;
  city.materials[0].diffuse[3] = 0.5f;
  city.materials[0].transparency = 0.5f;
  city.materials[1].diffuse[3] = 0.5f;
  city.materials[1].transparency = 0.5f;
  ok &= check(CoinBgfxLowering::lower(city, 4, 4, false, batched, diagnostic, false, true) &&
              batched.draws.size() == 300,
              "opaque batching merged transparent geometry");
  ok &= check(std::abs(plan.draws[0].mvp[10] - 0.5f) < 1e-6f &&
              std::abs(plan.draws[0].mvp[14] - 0.5f) < 1e-6f,
              "Coin clip depth was not converted to Vulkan range");
  SbMatrix moved = SbMatrix::identity();
  moved.setTranslate(SbVec3f(0.25f, 0.0f, 0.0f));
  frame.cameras[0].viewMatrix = moved;
  frame.renderStates[0].view = moved;
  std::vector<CoinBgfxDraw> patched;
  CoinBgfxPlan movedPlan;
  ok &= check(CoinBgfxLowering::patchCamera(frame, 4, 4, false, plan, patched, diagnostic) &&
              CoinBgfxLowering::lower(frame, 4, 4, false, movedPlan, diagnostic) &&
              patched.size() == 1 &&
              std::memcmp(patched[0].mvp, movedPlan.draws[0].mvp,
                          sizeof(patched[0].mvp)) == 0,
              "camera patch differs from full lowering");
  frame.clearColor = SbColor4f(0.1001f, 0.2f, 0.3f, 1.0f);
  ok &= check(!CoinBgfxLowering::patchCamera(frame, 4, 4, false, plan, patched, diagnostic),
              "camera patch accepted a changed float clear color");
  frame.clearColor = SbColor4f(0.1f, 0.2f, 0.3f, 1.0f);
  frame.draws[0].geometry.indexCount = 2;
  ok &= check(!CoinBgfxLowering::patchCamera(frame, 4, 4, false, plan, patched, diagnostic) &&
              patched.size() == 1 &&
              std::memcmp(patched[0].mvp, movedPlan.draws[0].mvp,
                          sizeof(patched[0].mvp)) == 0,
              "invalid camera patch must not publish partial draws");
  frame.draws[0].geometry.indexCount = 3;
  SbMatrix invalidCamera = moved;
  invalidCamera[0][0] = std::numeric_limits<float>::quiet_NaN();
  frame.renderStates[0].view = invalidCamera;
  ok &= check(!CoinBgfxLowering::patchCamera(frame, 4, 4, false, plan, patched, diagnostic),
              "non-finite camera patch must be rejected");
  frame.renderStates[0].view = moved;
  frame.viewports[0].x = 1;
  frame.viewports[0].y = 1;
  frame.viewports[0].width = 2;
  frame.viewports[0].height = 2;
  CoinBgfxPlan subviewportPlan;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false, subviewportPlan, diagnostic) &&
              subviewportPlan.draws[0].viewport[0] == 1 &&
              subviewportPlan.draws[0].viewport[1] == 1 &&
              subviewportPlan.draws[0].viewport[2] == 2 &&
              subviewportPlan.draws[0].viewport[3] == 2,
              "bounded subviewport was not lowered");
  frame.renderStates[0].depthTest = false;
  frame.renderStates[0].depthWrite = false;
  frame.renderStates[0].depthFunction = CoinRenderDepthFunction::ALWAYS;
  frame.renderStates[0].depthRange[0] = 0.25f;
  frame.renderStates[0].depthRange[1] = 0.75f;
  CoinBgfxPlan depthPlan;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false, depthPlan, diagnostic) &&
              !depthPlan.draws[0].depthTest && !depthPlan.draws[0].depthWrite &&
              depthPlan.draws[0].depthFunction == CoinRenderDepthFunction::ALWAYS &&
              depthPlan.draws[0].depthRange[0] == 0.25f &&
              depthPlan.draws[0].depthRange[1] == 0.75f,
              "depth state was not lowered to BGFX draws");
  frame.renderStates[0].depthTest = true;
  frame.renderStates[0].depthWrite = true;
  frame.renderStates[0].depthFunction = CoinRenderDepthFunction::LESS;
  frame.renderStates[0].depthRange[0] = 0.0f;
  frame.renderStates[0].depthRange[1] = 1.0f;
  ok &= check(!CoinBgfxLowering::patchCamera(frame, 4, 4, false, plan, patched, diagnostic),
              "camera-only patch accepted a changed viewport");
  frame.viewports[0].x = 3;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false, subviewportPlan, diagnostic),
              "partially external viewport was rejected");
  int32_t clipped[4];
  const int32_t partial[][4] = {{-1, 1, 2, 2}, {3, 1, 2, 2},
                               {1, -1, 2, 2}, {1, 3, 2, 2}, {-2, -2, 8, 8}};
  const int32_t expected[][4] = {{0, 1, 1, 2}, {3, 1, 1, 2},
                                {1, 0, 2, 1}, {1, 3, 2, 1}, {0, 0, 4, 4}};
  for (size_t i = 0; i < 5; ++i) {
    ok &= check(CoinBgfxLowering::clipViewport(partial[i], 4, 4, clipped) &&
                std::memcmp(clipped, expected[i], sizeof(clipped)) == 0,
                "partial viewport intersection is incorrect");
  }
  const int32_t external[][4] = {{-4, 0, 4, 4}, {4, 0, 4, 4},
                                {0, -4, 4, 4}, {0, 4, 4, 4},
                                {INT32_MAX, 0, INT32_MAX, 4}};
  for (const auto & rect : external)
    ok &= check(!CoinBgfxLowering::clipViewport(rect, 4, 4, clipped),
                "external viewport was not completely clipped");
  frame.viewports[0].x = -1;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false, subviewportPlan, diagnostic) &&
              subviewportPlan.draws[0].viewport[0] == -1 &&
              std::abs(subviewportPlan.draws[0].mvp[12] + 0.875f) < 1e-6f,
              "clipping changed the original viewport projection");
  ok &= check(CoinBgfxLowering::patchCamera(frame, 4, 4, false, subviewportPlan,
                                         patched, diagnostic),
              "camera patch rejected a preserved negative viewport");
  frame.viewports[0].x = 0;
  frame.viewports[0].y = 0;
  frame.viewports[0].width = 4;
  frame.viewports[0].height = 4;

  const size_t originalDrawCount = plan.draws.size();
  frame.renderStates[0].fogMode = CoinRenderFogMode::FOG;
  frame.renderStates[0].fogEnd = 10.0f;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false, plan, diagnostic) &&
              plan.draws.size() == originalDrawCount &&
              plan.draws[0].fogColorMode[3] == 2.0f && plan.draws[0].fogRange[1] == 10.0f,
              "fog must be lowered into draw uniforms");
  frame.renderStates[0].fogMode = CoinRenderFogMode::NONE;
  frame.materials[0].diffuse[3] = 0.5f;
  frame.materials[0].transparency = 0.5f;
  frame.renderStates[0].transparencyType = SoGLRenderAction::ADD;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false, plan, diagnostic) &&
              plan.draws[0].additive && !plan.draws[0].deferred && plan.draws[0].depthWrite,
              "ADD must preserve immediate additive blending and depth writes");
  frame.renderStates[0].transparencyType = SoGLRenderAction::SORTED_OBJECT_BLEND;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false, plan, diagnostic) &&
              plan.draws.size() == 1 && plan.draws[0].blend &&
              std::abs(plan.draws[0].alpha - 0.5f) < 1e-6f,
              "SORTED_OBJECT_BLEND alpha was not lowered");
  frame.renderStates[0].depthTest = true;
  frame.renderStates[0].depthWrite = true;
  frame.renderStates[0].depthFunction = CoinRenderDepthFunction::LEQUAL;
  CoinBgfxPlan translucentCubeDepthPlan;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false,
                translucentCubeDepthPlan, diagnostic) &&
              translucentCubeDepthPlan.draws[0].blend &&
              translucentCubeDepthPlan.draws[0].depthTest &&
              !translucentCubeDepthPlan.draws[0].depthWrite &&
              translucentCubeDepthPlan.draws[0].depthFunction == CoinRenderDepthFunction::LEQUAL,
              "delayed transparency must disable depth writes while preserving LEQUAL");
  frame.renderStates[0].depthWrite = false;
  CoinBgfxPlan labelDepthPlan;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false,
                labelDepthPlan, diagnostic) &&
              labelDepthPlan.draws[0].blend &&
              labelDepthPlan.draws[0].depthTest &&
              !labelDepthPlan.draws[0].depthWrite &&
              labelDepthPlan.draws[0].depthFunction == CoinRenderDepthFunction::LEQUAL,
              "transparent label test-only LEQUAL state was not preserved");
  frame.renderStates[0].depthWrite = true;
  frame.renderStates[0].depthFunction = CoinRenderDepthFunction::LESS;
  {
    CoinRenderFramePlan layers = frame;
    layers.renderStates[0].transparencyType = SoGLRenderAction::SORTED_LAYERS_BLEND;
    CoinBgfxPlan accepted;
    ok &= check(CoinBgfxLowering::lower(layers, 4, 4, false, accepted, diagnostic),
                "default sorted layers profile rejected");
    const auto beforeVertices = accepted.vertices.size();
    const auto beforeDraws = accepted.draws.size();
    for (int unsupported = 0; unsupported < 3; ++unsupported) {
      auto request = layers;
      auto& requested = request.renderStates[0];
      requested.explicitDepthMask = 15;
      requested.depthWrite = unsupported == 0;
      requested.depthTest = unsupported != 1;
      requested.depthFunction = unsupported == 2 ? CoinRenderDepthFunction::GREATER : CoinRenderDepthFunction::LEQUAL;
      ok &= check(!CoinBgfxLowering::lower(request, 4, 4, false, accepted, diagnostic) &&
                      diagnostic.find("BGFX sorted layers requires") != std::string::npos &&
                      accepted.vertices.size() == beforeVertices && accepted.draws.size() == beforeDraws,
                  "unsupported layer depth state must preserve the lowered plan");
    }
  }
  frame.renderStates[0].lightModel = CoinRenderLightModel::PHONG;
  frame.lightingStates[0].ambientIntensity = 1.0f;
  CoinBgfxPlan litTransparentPlan;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false,
                litTransparentPlan, diagnostic) &&
              litTransparentPlan.draws[0].blend &&
              std::abs(litTransparentPlan.vertices[0].color[0] - 0.8f) < 1e-5f &&
              std::abs(litTransparentPlan.vertices[0].color[3] - 0.5f) < 1e-5f &&
              std::abs(litTransparentPlan.vertices[0].ambient[0] - 0.2f) < 1e-5f &&
              litTransparentPlan.vertices[0].material[1] == 1.0f &&
              litTransparentPlan.draws[0].ambientLight[3] == 1.0f,
              "transparent PHONG inputs and alpha were not lowered together");
  frame.renderStates[0].lightModel = CoinRenderLightModel::BASE_COLOR;
  frame.lightingStates[0].ambientIntensity = 0.2f;
  CoinBgfxTransparencyStrategy selected = CoinBgfxTransparencyStrategy::OBJECT;
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(plan.draws,
                CoinBgfxTransparencyMode::AUTO, true, true, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::OBJECT &&
              diagnostic.empty(),
              "simple AUTO transparency must use object strategy");
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(plan.draws,
                CoinBgfxTransparencyMode::AUTO, false, false, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::OBJECT &&
              diagnostic.empty(),
              "simple AUTO transparency must not require advanced capabilities");
  std::vector<CoinBgfxDraw> opaqueDraws(12, plan.draws[0]);
  for (CoinBgfxDraw & opaque : opaqueDraws) opaque.blend = false;
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(opaqueDraws,
                CoinBgfxTransparencyMode::AUTO, false, false, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::OBJECT &&
              diagnostic.empty(),
              "opaque-only AUTO frame must use the minimum-cost object path");
  std::vector<CoinBgfxDraw> boundaryDraws(7, plan.draws[0]);
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(boundaryDraws,
                CoinBgfxTransparencyMode::AUTO, true, true, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::OBJECT,
              "AUTO must retain object below the complexity threshold");
  std::vector<CoinBgfxDraw> complexDraws(8, plan.draws[0]);
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(complexDraws,
                CoinBgfxTransparencyMode::AUTO, true, true, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::OBJECT,
              "AUTO must preserve Coin ordering regardless of object count");
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(complexDraws,
                CoinBgfxTransparencyMode::OBJECT, false, false, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::OBJECT,
              "explicit object mode must remain the compatibility override");
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(complexDraws,
                CoinBgfxTransparencyMode::AUTO, false, true, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::OBJECT,
              "ordinary Coin transparency must not require independent blending");
  ok &= check(!CoinBgfxLowering::selectTransparencyStrategy(complexDraws,
                CoinBgfxTransparencyMode::WEIGHTED_OIT, false, true,
                selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::WEIGHTED_OIT &&
              diagnostic.find("no fallback") != std::string::npos,
              "forced weighted_oit without MRT must fail without fallback");
  plan.draws[0].transparencyStrategy = CoinBgfxTransparencyStrategy::WEIGHTED_OIT;
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(plan.draws,
                CoinBgfxTransparencyMode::AUTO, true, true, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::WEIGHTED_OIT,
              "sorted-triangle Coin mapping must select weighted_oit");
  plan.draws[0].transparencyStrategy = CoinBgfxTransparencyStrategy::SORTED_LAYERS;
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(plan.draws,
                CoinBgfxTransparencyMode::AUTO, false, true, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::SORTED_LAYERS,
              "sorted_layers must take priority without requiring weighted OIT");
  ok &= check(!CoinBgfxLowering::selectTransparencyStrategy(plan.draws,
                CoinBgfxTransparencyMode::AUTO, true, false, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
              diagnostic.find("no fallback") != std::string::npos,
              "missing depth peeling support must fail instead of falling back");
  ok &= check(!CoinBgfxLowering::selectTransparencyStrategy(plan.draws,
                CoinBgfxTransparencyMode::SORTED_LAYERS, true, false,
                selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
              diagnostic.find("no fallback") != std::string::npos,
              "forced sorted_layers without depth targets must fail without fallback");
  CoinBgfxDraw weightedDraw = plan.draws[0];
  weightedDraw.transparencyStrategy = CoinBgfxTransparencyStrategy::WEIGHTED_OIT;
  CoinBgfxDraw sortedDraw = plan.draws[0];
  sortedDraw.transparencyStrategy = CoinBgfxTransparencyStrategy::SORTED_LAYERS;
  std::vector<CoinBgfxDraw> mixedRequirements = {weightedDraw, sortedDraw};
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(mixedRequirements,
                CoinBgfxTransparencyMode::AUTO, true, true, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::SORTED_LAYERS,
              "highest-fidelity requirement must win in a mixed scene");
  std::reverse(mixedRequirements.begin(), mixedRequirements.end());
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(mixedRequirements,
                CoinBgfxTransparencyMode::AUTO, true, true, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::SORTED_LAYERS,
              "mixed-scene selection must not depend on draw order");
  plan.draws[0].transparencyStrategy = CoinBgfxTransparencyStrategy::OBJECT;
  std::vector<CoinBgfxDraw> unordered(5, plan.draws[0]);
  for (auto & draw : unordered) { draw.deferred = true; draw.depthWrite = true; draw.depthFunction = CoinRenderDepthFunction::LESS; }
  unordered[0].blend = false;
  unordered[0].materialSignature = 2;
  unordered[0].firstIndex = 10;
  unordered[1].blend = true;
  unordered[1].materialSignature = 9;
  unordered[1].firstIndex = 20;
  unordered[2].blend = false;
  unordered[2].materialSignature = 1;
  unordered[2].firstIndex = 30;
  unordered[3].blend = true;
  unordered[3].materialSignature = 8;
  unordered[3].firstIndex = 40;
  unordered[4].blend = false;
  unordered[4].materialSignature = 1;
  unordered[4].firstIndex = 50;
  std::vector<CoinBgfxDraw> grouped;
  CoinBgfxLowering::groupOpaqueDraws(unordered, grouped);
  ok &= check(grouped.size() == unordered.size() &&
              grouped[0].firstIndex == 30 && grouped[1].firstIndex == 50 &&
              grouped[2].firstIndex == 10 &&
              grouped[3].firstIndex == 20 && grouped[4].firstIndex == 40,
              "opaque grouping changed stable material or transparent order");
  std::vector<CoinBgfxDraw> overlay = unordered;
  for (CoinBgfxDraw & draw : overlay) draw.renderLayer = 1;
  CoinBgfxLowering::groupOpaqueDraws(overlay, grouped);
  ok &= check(grouped.size() == overlay.size() &&
              grouped[0].firstIndex == 10 && grouped[1].firstIndex == 20 &&
              grouped[2].firstIndex == 30 && grouped[3].firstIndex == 40 &&
              grouped[4].firstIndex == 50,
              "overlay opaque/transparent draws must preserve traversal order");
  const auto checkUnsafeGroupingPreservesOrder = [&](CoinBgfxDraw unsafe,
                                                      const char * message) {
    std::vector<CoinBgfxDraw> input = unordered;
    input[2] = unsafe;
    input[2].firstIndex = 30;
    CoinBgfxLowering::groupOpaqueDraws(input, grouped);
    return check(grouped.size() == input.size() &&
                 grouped[0].firstIndex == 10 && grouped[1].firstIndex == 20 &&
                 grouped[2].firstIndex == 30 && grouped[3].firstIndex == 40 &&
                 grouped[4].firstIndex == 50, message);
  };
  CoinBgfxDraw unsafe = unordered[2];
  unsafe.depthFunction = CoinRenderDepthFunction::LEQUAL;
  ok &= checkUnsafeGroupingPreservesOrder(unsafe,
    "LEQUAL opaque draws must preserve traversal order");
  unsafe = unordered[2];
  unsafe.depthTest = false;
  ok &= checkUnsafeGroupingPreservesOrder(unsafe,
    "depth-test-disabled opaque draws must preserve traversal order");
  unsafe = unordered[2];
  unsafe.depthWrite = false;
  ok &= checkUnsafeGroupingPreservesOrder(unsafe,
    "depth-write-disabled opaque draws must preserve traversal order");
  unsafe = unordered[2];
  unsafe.depthRange[0] = 0.25f;
  unsafe.depthRange[1] = 0.75f;
  ok &= checkUnsafeGroupingPreservesOrder(unsafe,
    "non-default depth ranges must preserve traversal order");
  unsafe = unordered[2];
  unsafe.polygonOffsetUnits = -1.0f;
  ok &= checkUnsafeGroupingPreservesOrder(unsafe,
    "polygon-offset opaque draws must preserve traversal order");
  CoinBgfxPlan materialBase = movedPlan;
  CoinBgfxPlan materialUpdate = materialBase;
  materialUpdate.vertices[1].color[0] = 0.25f;
  materialUpdate.vertices[1].ambient[2] = 0.75f;
  materialUpdate.draws[0].materialSignature += 1;
  std::vector<CoinBgfxVertexRange> materialRanges;
  ok &= check(CoinBgfxLowering::materialPatchRanges(
                materialBase, materialUpdate, materialRanges) &&
              materialRanges.size() == 1 && materialRanges[0].first == 1 &&
              materialRanges[0].count == 1,
              "material-only update did not produce a minimal vertex range");
  materialUpdate.vertices[2].position[0] += 0.5f;
  ok &= check(!CoinBgfxLowering::materialPatchRanges(
                materialBase, materialUpdate, materialRanges) &&
              materialRanges.size() == 1,
              "geometry change was accepted as a material patch or published partial output");
  ok &= check(!CoinBgfxLowering::patchCamera(frame, 4, 4, false, plan, patched, diagnostic),
              "transparent camera patch must rebuild object order");
  frame.materials[0].diffuse[3] = 1.0f;
  frame.materials[0].transparency = 0.0f;
  frame.materials[0].diffuse[0] = std::numeric_limits<float>::quiet_NaN();
  ok &= check(!CoinBgfxLowering::lower(frame, 4, 4, false, plan, diagnostic),
              "non-finite color must be rejected");
  frame.materials[0].diffuse[0] = 0.8f;
  frame.renderStates[0].cullMode = static_cast<CoinRenderCullMode>(99);
  ok &= check(!CoinBgfxLowering::lower(frame, 4, 4, false, plan, diagnostic),
              "unknown cull state must be rejected");

  if (!ok) return 1;
  std::cout << "CoinBgfxCoreTest passed\n";
  return 0;
}
