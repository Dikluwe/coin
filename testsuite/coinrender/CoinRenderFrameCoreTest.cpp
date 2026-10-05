#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderImageCore.h"
#include "rendering/coinrender/CoinRenderStateCore.h"
#include "rendering/coinrender/CoinRenderFloatCore.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "CoinRenderTestEnvironment.h"

#include <Inventor/SoDB.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
bool
check(bool condition, const char * message)
{
  if (!condition) std::cerr << "CoinRenderFrameCoreTest: " << message << '\n';
  return condition;
}

CoinRenderFramePlan validPlan() {
  CoinRenderFramePlan plan;
  plan.vertices.resize(3);
  plan.indices = {0, 1, 2};
  plan.materials.emplace_back();
  plan.lightingStates.emplace_back();
  plan.cameras.emplace_back();
  plan.viewports.emplace_back();
  plan.viewports[0].width = plan.viewports[0].height = 1;
  plan.renderStates.emplace_back();
  plan.draws.emplace_back();
  plan.draws[0].geometry.vertexCount = plan.draws[0].geometry.indexCount = 3;
  return plan;
}

CoinRenderTextureCombineSnapshot activeCombine(float constant = .25f) {
  CoinRenderTextureCombineSnapshot program;
  program.instructions[0][0] = 1;
  program.instructions[0][1] = program.instructions[0][2] = 1;
  program.instructions[1][3] = program.instructions[2][3] = 1;
  for (int c = 0; c < 3; ++c) {
    program.instructions[1][c] = float(c);
    program.instructions[2][c] = float(8 + c);
    program.instructions[3][c] = constant;
  }
  program.instructions[3][3] = 1;
  return program;
}
CoinRenderFramePlan repeatedCombinePlan(bool active = false) {
  auto plan = validPlan();
  plan.revision = 151;
  plan.renderStates.resize(16, plan.renderStates[0]);
  // Only state zero is drawn. Every subsequent state remains part of Common
  // validation and deliberately repeats its predecessors' combine bytes.
  if (active)
    for (auto & state : plan.renderStates)
      for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit)
        state.textureCombines[unit] = activeCombine(float(unit) / 16);
  return plan;
}
void enableTextureUnit(CoinRenderFramePlan & plan, size_t stateSlot, size_t unit) {
  if (plan.textures.empty()) {
    CoinRenderTextureImageSnapshot image;
    image.width = image.height = 1; image.components = 4;
    image.pixelsRgba = {255, 255, 255, 255};
    plan.textures.push_back(image); plan.samplers.emplace_back();
  }
  auto & state = plan.renderStates[stateSlot];
  if (unit == 0) state.hasTexture = true;
  else state.extraTextures[unit - 1].enabled = true;
}
bool combineValidationOracle(const CoinRenderFramePlan & plan, bool expected,
                             const char * diagnostic, const char * label) {
  const std::string sentinel = "unchanged successful validation diagnostic";
  std::string literalDiagnostic = sentinel, memoDiagnostic = sentinel;
  coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMBINE_VALIDATION_MEMO", "1");
  const bool literal = plan.isValid(&literalDiagnostic);
  coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMBINE_VALIDATION_MEMO", "0");
  const bool memo = plan.isValid(&memoDiagnostic);
  const bool ok = literal == expected && memo == literal && memoDiagnostic == literalDiagnostic &&
    literalDiagnostic == (diagnostic ? std::string(diagnostic) : sentinel);
  if (!ok) std::cerr << label << ": literal=" << literal << " (" << literalDiagnostic << ") memo="
                    << memo << " (" << memoDiagnostic << ")\n";
  return check(ok, label);
}
bool testCombineValidationMemo() {
  struct Environment {
    std::string value; bool present;
    Environment() {
      const char * previous = std::getenv("COIN_RENDER_DISABLE_COMBINE_VALIDATION_MEMO");
      present = previous != nullptr; value = previous ? previous : "";
    }
    ~Environment() { coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMBINE_VALIDATION_MEMO", present ? value.c_str() : nullptr); }
  } environment;
  bool ok = combineValidationOracle(repeatedCombinePlan(), true, nullptr,
                                   "repeated inactive programs remain valid in referenced and unused states");
  ok &= combineValidationOracle(repeatedCombinePlan(true), true, nullptr,
                                "active programs remain validated even with disabled texture units");
  const float invalid[] = {std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
  for (bool active : {false, true}) {
    const auto base = repeatedCombinePlan(active);
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      for (size_t field = 0; field < 16; ++field) {
        for (const float value : invalid) {
          auto changed = base;
          changed.renderStates.back().textureCombines[unit].instructions[field / 4][field % 4] = value;
          ok &= combineValidationOracle(changed, false, "Invalid texture combine program",
            "every program float in every unit rejects NaN/Inf after earlier identical valid programs");
        }
        auto signedZero = repeatedCombinePlan();
        signedZero.renderStates.back().textureCombines[unit].instructions[field / 4][field % 4] = -0.0f;
        ok &= combineValidationOracle(signedZero, true, nullptr, "all program positions preserve signed-zero acceptance");
      }
    }
  }
  auto changed = repeatedCombinePlan(true);
  for (size_t state = 0; state < changed.renderStates.size(); ++state)
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      changed.renderStates[state].textureCombines[unit] = activeCombine(state % 3 == 1 ? .75f : .25f);
      if (state % 2) enableTextureUnit(changed, state, unit);
    }
  ok &= combineValidationOracle(changed, true, nullptr,
      "A/B/A program values and changing texture enablement preserve ordinary validation");

  const struct InvalidProgramField { int row, column; float value; } badFields[] = {
    {0, 0, 2}, {0, 1, 1.5f}, {0, 2, 6}, {1, 3, 3}, {2, 0, 7}, {3, 0, 1.01f}
  };
  for (const auto & field : badFields) {
    changed = repeatedCombinePlan(true);
    changed.renderStates.back().textureCombines[7].instructions[field.row][field.column] = field.value;
    ok &= combineValidationOracle(changed, false, "Invalid texture combine program",
        "finite invalid active instructions cannot inherit a previous valid program");
  }
  changed = repeatedCombinePlan();
  for (auto & state : changed.renderStates) state.textureCombines[3].instructions[2][0] = -200;
  ok &= combineValidationOracle(changed, true, nullptr,
      "inactive finite instructions retain the original unrestricted representation");
  changed.renderStates.back().textureCombines[3].instructions[0][0] = 1;
  ok &= combineValidationOracle(changed, false, "Invalid texture combine program",
      "changing only activation invalidates an earlier inactive-program result");

  // All enablement-dependent checks still execute after a valid combine hit,
  // including those in states with no draw and units after unit zero.
  for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
    changed = repeatedCombinePlan(true); enableTextureUnit(changed, 15, unit);
    ok &= combineValidationOracle(changed, true, nullptr, "enabled texture units remain valid after repeated active programs");
    if (unit == 0) changed.renderStates.back().textureImageSlot = 1;
    else changed.renderStates.back().extraTextures[unit - 1].imageSlot = 1;
    ok &= combineValidationOracle(changed, false, "RenderState references out-of-bounds texture image slot",
        "combine cache hits do not bypass image bounds in unused states");
    changed = repeatedCombinePlan(); enableTextureUnit(changed, 15, unit);
    if (unit == 0) changed.renderStates.back().samplerSlot = 1;
    else changed.renderStates.back().extraTextures[unit - 1].samplerSlot = 1;
    ok &= combineValidationOracle(changed, false, "RenderState references out-of-bounds sampler slot",
        "inactive program hits do not bypass enabled-unit sampler bounds");
    changed = repeatedCombinePlan(true); enableTextureUnit(changed, 15, unit);
    if (unit == 0) changed.renderStates.back().textureMatrix[0][0] = invalid[0];
    else changed.renderStates.back().extraTextures[unit - 1].matrix[0][0] = invalid[0];
    ok &= combineValidationOracle(changed, false, "RenderState contains non-finite texture matrix",
        "combine cache hits do not bypass enabled-unit matrix validation");
    changed = repeatedCombinePlan(true); enableTextureUnit(changed, 15, unit);
    if (unit == 0) changed.renderStates.back().textureModel = static_cast<CoinRenderTextureModel>(99);
    else changed.renderStates.back().extraTextures[unit - 1].model = static_cast<CoinRenderTextureModel>(99);
    ok &= combineValidationOracle(changed, false, "RenderState contains unsupported texture model",
        "combine cache hits do not bypass enabled-unit model validation");
  }

  changed = repeatedCombinePlan();
  changed.renderStates.back().textureCombines[0].instructions[3][0] = invalid[0];
  changed.renderStates.back().viewportSlot = 1;
  ok &= combineValidationOracle(changed, false, "RenderState references out-of-bounds viewport slot",
      "an earlier state property retains precedence over an invalid combine in that state");
  changed = repeatedCombinePlan();
  changed.renderStates.back().textureCombines[0].instructions[3][0] = invalid[0];
  changed.renderStates.back().fogColor[0] = invalid[1];
  ok &= combineValidationOracle(changed, false, "Invalid fog color",
      "fog validation retains precedence over an invalid combine in the same state");
  changed = repeatedCombinePlan();
  changed.renderStates[1].textureCombines[7].instructions[3][0] = invalid[0];
  changed.renderStates.back().viewportSlot = 1;
  ok &= combineValidationOracle(changed, false, "Invalid texture combine program",
      "an earlier invalid combine retains precedence over a later invalid unused state");
  changed = repeatedCombinePlan(); enableTextureUnit(changed, 15, 0);
  changed.renderStates.back().textureImageSlot = 1;
  changed.renderStates.back().textureCombines[1].instructions[3][0] = invalid[0];
  ok &= combineValidationOracle(changed, false, "RenderState references out-of-bounds texture image slot",
      "earlier-unit image failure retains precedence over a later-unit invalid combine");
  changed.renderStates.back().textureCombines[0].instructions[3][0] = invalid[0];
  ok &= combineValidationOracle(changed, false, "Invalid texture combine program",
      "same-unit combine failure remains before image validation");
  changed = repeatedCombinePlan();
  changed.renderStates.back().textureCombines[7].instructions[3][0] = invalid[0];
  changed.renderStates[0].model[0][0] = invalid[1];
  ok &= combineValidationOracle(changed, false, "Invalid texture combine program",
      "state combine validation retains precedence over the later draw-matrix validation pass");
  changed = repeatedCombinePlan();
  changed.renderStates.back().textureMatrix[0][0] = invalid[0];
  changed.renderStates.back().textureImageSlot = 99;
  changed.renderStates.back().extraTextures[6].matrix[0][0] = invalid[1];
  changed.renderStates.back().extraTextures[6].imageSlot = 99;
  ok &= combineValidationOracle(changed, true, nullptr,
      "disabled texture data remains ignored after validating its inactive program bytes");

  auto sameRevision = repeatedCombinePlan(true);
  ok &= combineValidationOracle(sameRevision, true, nullptr, "initial same-revision frame validates");
  sameRevision.renderStates.back().textureCombines[6].instructions[3][2] = invalid[1];
  ok &= combineValidationOracle(sameRevision, false, "Invalid texture combine program",
      "mutation without a revision change cannot inherit prior invocation results");
  sameRevision.renderStates.back().textureCombines[6] = activeCombine(.5f);
  ok &= combineValidationOracle(sameRevision, true, nullptr,
      "valid retry with the same revision is accepted after a failed invocation");
  coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMBINE_VALIDATION_MEMO", "0");
  ok &= check(sameRevision.isValid(nullptr), "the optional diagnostic pointer remains optional with combine memo enabled");
  return ok;
}

bool testFiniteValidation() {
  bool ok = true;
  if (sizeof(float) == sizeof(uint32_t) && std::numeric_limits<float>::is_iec559 &&
      std::numeric_limits<float>::digits == 24) {
    const uint32_t mantissas[] = {0, 1, 0x3fffff, 0x7fffff};
    for (uint32_t sign = 0; sign < 2; ++sign)
      for (uint32_t exponent = 0; exponent < 256; ++exponent)
        for (const uint32_t mantissa : mantissas) {
          const uint32_t bits = (sign << 31) | (exponent << 23) | mantissa;
          float value;
          std::memcpy(&value, &bits, sizeof(value));
          ok &= check(coin_render_is_finite(value) == std::isfinite(value),
                      "finite classification must cover every exponent, both signs and NaN payloads");
        }
  }
  const float invalid[] = {std::numeric_limits<float>::infinity(),
    -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
  for (float value : invalid) {
    for (unsigned field = 0; field < 24; ++field) {
      auto plan = validPlan();
      auto & v = plan.vertices[0];
      const char * expected;
      if (field < 3) { v.position[field] = value; expected = "Vertex contains non-finite position or normal"; }
      else if (field < 6) { v.normal[field - 3] = value; expected = "Vertex contains non-finite position or normal"; }
      else if (field < 8) { v.texcoord[field - 6] = value; expected = "Vertex contains non-finite texcoord"; }
      else if (field == 8) { v.screenSpaceW = value; expected = "Invalid expanded primitive attributes"; }
      else if (field == 9) { v.fogEyeDepth = value; expected = "Invalid expanded primitive attributes"; }
      else { v.extraTexcoords[(field - 10) / 2][(field - 10) % 2] = value; expected = "Non-finite multitexture coordinate"; }
      std::string diagnostic;
      ok &= check(!plan.isValid(&diagnostic) && diagnostic == expected,
                  "every captured vertex float must reject invalid data with the same diagnostic");
    }
    for (unsigned unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      auto plan = validPlan();
      plan.renderStates[0].textureCombines[unit].instructions[3][2] = value;
      std::string diagnostic;
      ok &= check(!plan.isValid(&diagnostic) && diagnostic == "Invalid texture combine program",
                  "disabled texture units must still validate their combine floats");
    }
    auto plan = validPlan();
    plan.renderStates[0].model[1][2] = value;
    std::string diagnostic;
    ok &= check(!plan.isValid(&diagnostic) && diagnostic == "RenderState matrix contains non-finite values",
                "matrix finiteness must remain enforced");
  }
  auto finite = validPlan();
  finite.vertices[0].position[0] = std::numeric_limits<float>::max();
  finite.vertices[0].normal[0] = -std::numeric_limits<float>::max();
  finite.vertices[0].texcoord[0] = std::numeric_limits<float>::denorm_min();
  finite.vertices[0].extraTexcoords[6][1] = -0.0f;
  ok &= check(finite.isValid(), "finite extremes, subnormals and signed zeros remain accepted");
  return ok;
}

class CountingBackend : public CoinRenderBackend {
public:
  unsigned submissions = 0;
  bool isGpuBackend() const override { return false; }
  CoinRenderBackendStatus getStatus() const override { return CoinRenderBackendStatus::SUCCESS; }
  CoinRenderBackendStatus prepare(CoinRenderTargetP &) override { return CoinRenderBackendStatus::SUCCESS; }
  void poll() override {}
  const std::string & getLastError() const override { return error; }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &, CoinRenderTargetP &) override {
    ++submissions;
    return {};
  }
private:
  std::string error;
};

class PreflightBackend : public CoinRenderBackend {
public:
  bool ok = true;
  bool fail = false;
  bool isGpuBackend() const override { return false; }
  CoinRenderBackendStatus getStatus() const override { return CoinRenderBackendStatus::SUCCESS; }
  CoinRenderBackendStatus prepare(CoinRenderTargetP &) override { return CoinRenderBackendStatus::SUCCESS; }
  void poll() override {}
  const std::string & getLastError() const override { return error; }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target) override {
    const auto * receipt = target.submissionPreflight(frame);
    ok &= check(receipt && receipt->compositionFor(frame), "fresh validation supplies a submission-local receipt");
    auto foreign = frame;
    foreign.vertices[0].position[2] = std::numeric_limits<float>::quiet_NaN();
    ok &= check(!target.submissionPreflight(foreign), "a different plan with the same revision cannot use the receipt");
    std::vector<CoinRenderCompositionItem> order;
    std::string diagnostic;
    ok &= check(!coin_render_composition_schedule(foreign, order, diagnostic, receipt),
                "a foreign mutated plan must take the validating composition path");
    auto & mutableFrame = const_cast<CoinRenderFramePlan &>(frame);
    const uint64_t revision = mutableFrame.revision;
    ++mutableFrame.revision;
    ok &= check(!receipt->compositionFor(frame), "a changed revision invalidates the borrowed receipt");
    mutableFrame.revision = revision;
    const auto transparency = mutableFrame.transparency;
    ++mutableFrame.transparency.layers;
    ok &= check(!receipt->compositionFor(frame), "changed composition policy invalidates the receipt");
    mutableFrame.transparency = transparency;

    CoinRenderTargetP other(SbVec2i32(1, 1));
    other.depthReadbackEnabled = false;
    auto * counter = new CountingBackend;
    other.backend.reset(counter);
    const CoinRenderFrameReuseDecision rebuild(CoinRenderFrameReuseKind::FULL_REBUILD, 0);
    // Even a revision previously accepted by this target cannot make a
    // foreign invalid payload inherit another plan's borrowed proof.
    other.lastValidatedPlanRevision = foreign.revision;
    ok &= check(other.executeFrame(foreign, rebuild, receipt).status == CoinRenderBackendStatus::BACKEND_ERROR &&
                counter->submissions == 0, "foreign proof must trigger complete validation before submission");
    other.options.transparency = static_cast<CoinRenderTransparencyMode>(99);
    ok &= check(other.executeFrame(frame, rebuild, receipt).status == CoinRenderBackendStatus::UNSUPPORTED &&
                counter->submissions == 0, "a valid proof cannot bypass invalid target options");
    other.options.transparency = COIN_RENDER_TRANSPARENCY_COIN;
    other.suspended = true;
    ok &= check(other.executeFrame(frame, rebuild, receipt).status == CoinRenderBackendStatus::NOT_READY &&
                counter->submissions == 0, "a valid proof cannot bypass a suspended target");
    other.suspended = false;
    ok &= check(other.resize(SbVec2i32(2, 2)) &&
                other.executeFrame(frame, rebuild, receipt).status == CoinRenderBackendStatus::SUCCESS &&
                counter->submissions == 1, "resized target must retain its own checks with a borrowed capture proof");
    return {fail ? CoinRenderBackendStatus::BACKEND_ERROR : CoinRenderBackendStatus::SUCCESS};
  }
private:
  std::string error;
};

bool testSubmissionPreflight() {
  CoinRenderTargetP target(SbVec2i32(1, 1));
  target.depthReadbackEnabled = false;
  auto * backend = new PreflightBackend;
  target.backend.reset(backend);
  auto plan = validPlan();
  plan.revision = 1;
  bool ok = check(target.executeFrame(plan).status == CoinRenderBackendStatus::SUCCESS && backend->ok,
                  "validated submission must succeed");
  ok &= check(!target.submissionPreflight(plan), "receipt must be cleared after successful submission");
  backend->fail = true;
  ++plan.revision;
  ok &= check(target.executeFrame(plan).status == CoinRenderBackendStatus::BACKEND_ERROR && backend->ok,
              "failure path must exercise the receipt");
  ok &= check(!target.submissionPreflight(plan), "receipt must be cleared after failed submission");
  plan.vertices[0].position[0] = std::numeric_limits<float>::infinity();
  ++plan.revision;
  ok &= check(target.executeFrame(plan).status != CoinRenderBackendStatus::SUCCESS &&
              !target.submissionPreflight(plan), "invalid subsequent data cannot inherit a receipt");
  return ok;
}

bool testCapturePreflightFallbacks() {
  auto plan = validPlan();
  bool ok = check(coin_render_capture_preflight_eligible(plan),
                  "ordinary captured frame is eligible for submission-local proof reuse");
  plan.shadowGroups.emplace_back();
  ok &= check(!coin_render_capture_preflight_eligible(plan),
              "shadow group capture must retain complete submission validation");
  plan.shadowGroups.clear();
  plan.shadowLights.emplace_back();
  ok &= check(!coin_render_capture_preflight_eligible(plan),
              "shadow-owned scene data cannot use an ordinary capture receipt");
  plan.shadowLights.clear();
  plan.textures.emplace_back();
  auto & texture = plan.textures.back();
  texture.width = texture.height = 1;
  texture.producerId = 7;
  ok &= check(!coin_render_capture_preflight_eligible(plan),
              "unresolved RTT producer must take complete submission validation");
  texture.producerId = 0;
  texture.gpuToken = 9;
  ok &= check(!coin_render_capture_preflight_eligible(plan),
              "resolved RTT resource must also retain complete submission validation");
  texture.gpuToken = 0;
  texture.pixelsRgba = {255, 255, 255, 255};
  ok &= check(plan.isValid() && coin_render_capture_preflight_eligible(plan),
              "ordinary captured image pixels remain eligible after full common validation");
  return ok;
}
}

int
main()
{
  SoDB::init();
  bool ok = true;
  ok &= testFiniteValidation();
  ok &= testCombineValidationMemo();
  ok &= testSubmissionPreflight();
  ok &= testCapturePreflightFallbacks();

  CoinRenderTextureUnitSnapshot unitsA[1], unitsB[1];
  for (size_t i = sizeof(bool); i < offsetof(CoinRenderTextureUnitSnapshot, imageSlot); ++i) {
    reinterpret_cast<unsigned char*>(&unitsA[0])[i] = 0x55;
    reinterpret_cast<unsigned char*>(&unitsB[0])[i] = 0xaa;
  }
  ok &= check(coin_render_same_texture_units(unitsA, unitsB),
              "texture unit padding must not split equal render states");
  unitsB[0].enabled = true;
  ok &= check(!coin_render_same_texture_units(unitsA, unitsB),
              "texture unit values must still invalidate equality");

  CoinRenderFramePlan first;
  CoinRenderFramePlan second;
  first.revision = 41;
  second.revision = 99;
  ok &= check(first.hasSamePayload(second),
              "revision must not be part of immutable payload equality");

  first.vertices.push_back(CoinRenderVertexSnapshot());
  second.vertices.push_back(CoinRenderVertexSnapshot());
  ok &= check(first.hasSamePayload(second), "equal vertices must compare equal");
  second.vertices[0].position[1] = 2.0f;
  ok &= check(!first.hasSamePayload(second), "changed vertex must invalidate equality");

  CoinRenderFramePlan invalid;
  invalid.clearColor[0] = std::numeric_limits<float>::quiet_NaN();
  std::string diagnostic;
  ok &= check(!invalid.isValid(&diagnostic) &&
              diagnostic == "Invalid clearColor (NaN or inf)",
              "validation diagnostic must remain stable");

  std::vector<uint8_t> pixels = {
    1, 2, 3, 4, 5, 6, 7, 8,
    9, 10, 11, 12, 13, 14, 15, 16
  };
  ok &= check(CoinRenderImageCore::flipRgba8Rows(pixels, SbVec2i32(2, 2)),
              "valid RGBA8 image must flip");
  const std::vector<uint8_t> expected = {
    9, 10, 11, 12, 13, 14, 15, 16,
    1, 2, 3, 4, 5, 6, 7, 8
  };
  ok &= check(pixels == expected, "RGBA8 rows must be reversed exactly once");

  const std::vector<uint8_t> before = pixels;
  ok &= check(!CoinRenderImageCore::flipRgba8Rows(pixels, SbVec2i32(3, 2)) &&
              pixels == before,
              "invalid byte count must be rejected without mutation");

  if (!ok) return 1;
  std::cout << "CoinRenderFrameCoreTest passed\n";
  return 0;
}
