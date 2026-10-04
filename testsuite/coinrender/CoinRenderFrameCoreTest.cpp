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
