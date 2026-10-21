#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderPlanAssemblyCore.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
const int side = 32;
bool ok = true;
bool require(bool value, const std::string &label) {
  if (!value) {
    ok = false;
    std::cerr << "normalization FAIL " << label << '\n';
  }
  return value;
}
const float badValues[] = {std::numeric_limits<float>::quiet_NaN(),
                           std::numeric_limits<float>::infinity(),
                           -std::numeric_limits<float>::infinity()};
CoinRenderFramePlan frameFor(const CoinRenderCameraSnapshot &camera,
                             const CoinRenderRenderStateSnapshot &state) {
  CoinRenderFramePlan frame;
  frame.revision = 123;
  frame.cameras.push_back(camera);
  frame.renderStates.push_back(state);
  frame.materials.resize(1);
  frame.lightingStates.resize(1);
  frame.viewports.resize(1);
  frame.viewports[0].width = frame.viewports[0].height = side;
  frame.vertices.resize(3);
  frame.draws.resize(1);
  frame.indices = {0, 1, 2};
  frame.vertices[0].position[0] = -.5f;
  frame.vertices[0].position[1] = -.5f;
  frame.vertices[1].position[0] = .5f;
  frame.vertices[1].position[1] = -.5f;
  frame.vertices[2].position[1] = .5f;
  for (auto &v : frame.vertices) {
    v.position[2] = -2;
    v.normal[2] = 1;
  }
  auto &draw = frame.draws[0];
  draw.geometry.vertexCount = draw.geometry.indexCount = 3;
  return frame;
}
void valid(const CoinRenderFramePlan &frame, const std::string &label,
           bool transport = true) {
  std::string error;
  require(frame.isValid(&error), label + " Core valid: " + error);
  if (transport) {
    CoinWgpuFfiFrame ffi;
    require(ffi.prepare(frame, side, side, error, false),
            label + " FFI valid: " + error);
  }
}
void invalid(const CoinRenderFramePlan &frame, const std::string &label) {
  std::string error;
  const bool accepted = frame.isValid(&error);
  require(!accepted && !error.empty(), label + " Core rejects with diagnostic");
  // Avoid asking an already-broken baseline to numerically pack raw NaN.
  // A candidate that validates correctly must also refuse FFI preparation.
  if (!accepted) {
    CoinWgpuFfiFrame ffi;
    error.clear();
    require(!ffi.prepare(frame, side, side, error, false) && !error.empty(),
            label + " FFI rejects before a device/producer exists");
  }
}
bool core() {
  CoinRenderCameraSnapshot camera;
  camera.nearDistance = 1;
  camera.farDistance = 10;
  CoinRenderRenderStateSnapshot state;
  state.transparencyType = SoGLRenderAction::NONE;
  valid(frameFor(camera, state), "default raw sentinel -1");
  const float finite[] = {
      -std::numeric_limits<float>::max(),       -1,   -0.f, 0.f,
      std::numeric_limits<float>::denorm_min(), .25f, 1,    2,
      std::numeric_limits<float>::max()};
  for (float value : finite) {
    auto raw = state;
    raw.screenDoorTransparency = value;
    valid(frameFor(camera, raw), "finite raw screen-door has no invented bound",
          false);
    auto normalized = state;
    normalized.screenDoorTransparency = value;
    normalized.fogEnd = value;
    normalized.lineWidth = value;
    normalized.pointSize = value;
    normalized.linePattern = 0x12345678u;
    normalized.linePatternScaleFactor = -7;
    CoinRenderPlanAssemblyCore::normalizeState(normalized, camera);
    const float expected = value <= 0 ? 0 : value >= 1 ? 1 : value;
    require(normalized.screenDoorTransparency == expected &&
                std::signbit(normalized.screenDoorTransparency) ==
                    std::signbit(expected) &&
                normalized.fogEnd == (value > 0 ? value : 10.f) &&
                normalized.lineWidth == (value <= 0 ? 1.f : value) &&
                normalized.pointSize == (value <= 0 ? 1.f : value) &&
                normalized.linePattern == 0x5678u &&
                normalized.linePatternScaleFactor == 1,
            "finite state normalizer exact formulas");
    valid(frameFor(camera, normalized), "finite normalized state");
  }
  for (bool perspective : {false, true})
    for (float nearValue : {-2.f, -0.f, 0.f, .125f, 1e-20f, 1e20f}) {
      auto c = camera;
      c.isPerspective = perspective;
      c.nearDistance = nearValue;
      c.farDistance = nearValue;
      const float n = perspective && nearValue <= 0 ? .1f : nearValue;
      const float f = n + 100.f;
      CoinRenderPlanAssemblyCore::normalizeCamera(c);
      require(c.nearDistance == n &&
                  std::signbit(c.nearDistance) == std::signbit(n) &&
                  c.farDistance == f,
              "finite camera preserves signed ortho near and exact fallback");
      if (f > n)
        valid(frameFor(c, state), "finite normalized camera");
      else
        invalid(frameFor(c, state),
                "finite fallback cannot represent a larger far");
    }
  for (int field = 0; field < 4; ++field)
    for (float bad : badValues) {
      auto s = state;
      float *values[] = {&s.fogEnd, &s.lineWidth, &s.pointSize,
                         &s.screenDoorTransparency};
      *values[field] = bad;
      CoinRenderPlanAssemblyCore::normalizeState(s, camera);
      const float actual[] = {s.fogEnd, s.lineWidth, s.pointSize,
                              s.screenDoorTransparency};
      require(!std::isfinite(actual[field]),
              "nonfinite normalized state remains rejectable field=" +
                  std::to_string(field));
      invalid(frameFor(camera, s),
              "normalized nonfinite state field=" + std::to_string(field));
      auto raw = state;
      float *rawFields[] = {&raw.fogEnd, &raw.lineWidth, &raw.pointSize,
                            &raw.screenDoorTransparency};
      *rawFields[field] = bad;
      invalid(frameFor(camera, raw),
              "raw nonfinite state field=" + std::to_string(field));
    }
  for (bool perspective : {false, true})
    for (int field = 0; field < 2; ++field)
      for (float bad : badValues) {
        auto c = camera;
        c.isPerspective = perspective;
        (field ? c.farDistance : c.nearDistance) = bad;
        CoinRenderPlanAssemblyCore::normalizeCamera(c);
        require(!std::isfinite(field ? c.farDistance : c.nearDistance),
                "nonfinite camera plane is not repaired");
        invalid(frameFor(c, state), "nonfinite normalized camera");
        auto inherited = state;
        inherited.fogEnd = 0;
        CoinRenderPlanAssemblyCore::normalizeState(inherited, c);
        invalid(frameFor(c, inherited),
                "invalid inherited camera cannot produce a valid frame");
      }
  for (float bad : badValues) {
    auto c = camera;
    c.farDistance = bad;
    auto s = state;
    s.fogEnd = 0;
    CoinRenderPlanAssemblyCore::normalizeState(s, c);
    require(!std::isfinite(s.fogEnd),
            "finite fog fallback retains nonfinite inherited far");
    invalid(frameFor(c, s), "inherited fog far is invalid");
  }
  const auto stable = frameFor(camera, state);
  CoinWgpuFfiFrame warm;
  std::string diagnostic;
  require(warm.prepare(stable, side, side, diagnostic, false),
          "warm valid private transport");
  for (int field = 0; field < 4; ++field)
    for (float bad : badValues) {
      auto changed = stable;
      float *values[] = {&changed.renderStates[0].fogEnd,
                         &changed.renderStates[0].lineWidth,
                         &changed.renderStates[0].pointSize,
                         &changed.renderStates[0].screenDoorTransparency};
      *values[field] = bad;
      require(changed.revision == stable.revision,
              "adversarial private revision remains identical");
      if (!changed.isValid())
        require(!warm.prepare(changed, side, side, diagnostic, false),
                "malformed same-revision data cannot reuse valid FFI cache");
      else
        require(
            false,
            "Core must reject malformed same-revision input before packing");
      require(warm.prepare(stable, side, side, diagnostic, false),
              "original valid FFI resource recovers after refusal");
    }
  valid(frameFor(camera, state),
        "valid recovery after invalid Core/FFI inputs");
  return ok;
}
class Audit : public CoinRenderBackend {
public:
  std::unique_ptr<CoinRenderBackend> real;
  CoinRenderFramePlan last;
  unsigned calls = 0, successful = 0, producers = 0;
  explicit Audit(std::unique_ptr<CoinRenderBackend> backend)
      : real(std::move(backend)) {}
  bool isGpuBackend() const override { return real->isGpuBackend(); }
  bool initializesCpuDepthBuffer() const override {
    return real->initializesCpuDepthBuffer();
  }
  CoinRenderBackendStatus getStatus() const override {
    return real->getStatus();
  }
  CoinRenderBackendStatus prepare(CoinRenderTargetP &t) override {
    return real->prepare(t);
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,
                                CoinRenderTargetP &t) override {
    ++calls;
    last = f;
    auto r = real->submit(f, t);
    successful += r.status == CoinRenderBackendStatus::SUCCESS;
    return r;
  }
  CoinRenderSubmitResult
  submit(const CoinRenderFramePlan &f, CoinRenderTargetP &t,
         const CoinRenderFrameReuseDecision &r) override {
    ++calls;
    last = f;
    auto result = real->submit(f, t, r);
    successful += result.status == CoinRenderBackendStatus::SUCCESS;
    return result;
  }
  CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan &f,
                                   const SbVec2i32 &s, uint64_t id,
                                   CoinRenderTargetP &t,
                                   uint64_t &token) override {
    ++producers;
    return real->submitRtt(f, s, id, t, token);
  }
  bool resetOnActionDetach() const override {
    return real->resetOnActionDetach();
  }
  CoinRenderDeviceDomain resourceDomain() const override {
    return real->resourceDomain();
  }
  bool requiresSharedRetirement() const override {
    return real->requiresSharedRetirement();
  }
  bool readbackLoad(uint64_t &a, uint64_t &b) const override {
    return real->readbackLoad(a, b);
  }
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan &g,
                                      const CoinRenderFramePlan &f,
                                      const SbVec2i32 &s) const override {
    return real->preflightRtt(g, f, s);
  }
  void finishRtt(const std::vector<uint64_t> &tokens) override {
    real->finishRtt(tokens);
  }
  void retireRtt(const std::vector<uint64_t> &tokens) override {
    real->retireRtt(tokens);
  }
  bool stagedRttRequiresPreparedParent() const override {
    return real->stagedRttRequiresPreparedParent();
  }
  void poll() override { real->poll(); }
  const std::string &getLastError() const override {
    return real->getLastError();
  }
};
struct Scene {
  SoSeparator *root = new SoSeparator;
  SoEnvironment *environment = new SoEnvironment;
  Scene() {
    root->ref();
    auto *c = new SoOrthographicCamera;
    c->position.setValue(0, 0, 5);
    c->height = 2;
    c->nearDistance = 1;
    c->farDistance = 10;
    root->addChild(c);
    environment->fogType = SoEnvironment::HAZE;
    environment->fogColor.setValue(0, 0, 1);
    environment->fogVisibility = 10;
    root->addChild(environment);
    auto *l = new SoLightModel;
    l->model = SoLightModel::BASE_COLOR;
    root->addChild(l);
    auto *m = new SoMaterial;
    m->diffuseColor.setValue(1, 0, 0);
    root->addChild(m);
    auto *p = new SoCoordinate3;
    const SbVec3f v[] = {SbVec3f(-.75f, -.75f, 0), SbVec3f(.75f, -.75f, 0),
                         SbVec3f(.75f, .75f, 0), SbVec3f(-.75f, .75f, 0)};
    p->point.setValues(0, 4, v);
    root->addChild(p);
    auto *f = new SoFaceSet;
    f->numVertices = 4;
    root->addChild(f);
  }
  ~Scene() { root->unref(); }
};
struct Image {
  std::vector<uint8_t> rgba;
  std::vector<float> depth;
  uint64_t serial = 0;
};
Image image(CoinRenderTarget &target) {
  Image i;
  target.readbackRGBA(i.rgba);
  target.readbackDepth(i.depth);
  i.serial = target.getLastSubmissionSerial();
  return i;
}
void pixels(const Image &i, float end, const std::string &label) {
  if (!require(i.rgba.size() == side * side * 4 &&
                   i.depth.size() == side * side,
               label + " actual RGBA/depth"))
    return;
  const double red = 255 * std::max(0., std::min(1., 1 - 5. / end)),
               blue = 255 - red;
  unsigned bad = 0;
  for (int y = 12; y < 20; ++y)
    for (int x = 12; x < 20; ++x) {
      const size_t p = size_t(y * side + x);
      bad += std::abs(i.rgba[p * 4] - red) > 3 || i.rgba[p * 4 + 1] != 0 ||
             std::abs(i.rgba[p * 4 + 2] - blue) > 3 ||
             i.rgba[p * 4 + 3] != 255 || !std::isfinite(i.depth[p]) ||
             std::abs(i.depth[p] - 4.f / 9) > 1e-5;
    }
  std::cout << "normalization_pixels " << label << " bad=" << bad
            << " fog_end=" << end << '\n';
  require(bad == 0, label + " analytic HAZE RGB3 and geometric depth");
}
bool publicPath(bool gpu) {
  CoinRenderCapabilities caps{};
  if (gpu && (coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
                                             &caps, sizeof(caps)) ||
              !caps.gpu_available))
    return false;
  CoinRenderOptions options;
  options.sampleCount = 1;
  if (gpu)
    options.sceneTexture = COIN_RENDER_SCENE_TEXTURE_DIRECT;
  if (gpu)
    options.renderer = static_cast<CoinRenderRenderer>(caps.renderer);
  std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createOffscreen(SbVec2i32(side, side), options));
  if (!require(bool(target), "public offscreen target"))
    return true;
  if (!gpu)
    target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
  auto &p = target->getPimpl().get();
  if (!require(p.prepareBackend() == CoinRenderBackendStatus::SUCCESS &&
                   p.backend && p.backend->isGpuBackend() == gpu,
               "requested real CPU/GPU backend"))
    return true;
  auto *audit = new Audit(std::move(p.backend));
  p.backend.reset(audit);
  target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(side, side));
  action.setRenderTarget(target.get());
  action.setTransparencyType(CoinRenderAction::NONE);
  Scene scene;
  if (gpu) {
    // A white, opaque clear-only producer is numerically neutral. Its real
    // positive execution makes the invalid-root producer guard non-vacuous.
    auto *quality = new SoComplexity;
    quality->textureQuality = .1f;
    scene.root->insertChild(quality, 3);
    auto *texture = new SoSceneTexture2;
    texture->size.setValue(8, 8);
    texture->backgroundColor.setValue(1, 1, 1, 1);
    texture->scene = new SoSeparator;
    texture->transparencyFunction = SoSceneTexture2::NONE;
    scene.root->insertChild(texture, 4);
  }
  if (gpu)
    std::cout << "normalization_gpu backend=" << caps.backend
              << " renderer=" << caps.renderer
              << " adapter=" << caps.adapter_name << '\n';
  auto draw = [&](float value, float expected, const std::string &label) {
    scene.environment->fogVisibility = value;
    const auto source = scene.root->getNodeId();
    const uint64_t before = target->getLastSubmissionSerial();
    const unsigned submitted = audit->successful;
    action.apply(scene.root);
    auto current = image(*target);
    require(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                current.serial > before && audit->successful == submitted + 1,
            label +
                " new valid publication: " + action.getLastError().getString());
    require(source == scene.root->getNodeId(),
            label + " apply preserves source revision");
    pixels(current, expected, label);
    bool delivered = false;
    for (const auto &s : audit->last.renderStates)
      delivered |= s.fogMode == CoinRenderFogMode::HAZE && s.fogEnd == expected;
    require(delivered, label + " current normalized fog reaches executor");
    return current;
  };
  auto stable = draw(10, 10, "valid authored visibility10");
  if (gpu)
    require(audit->producers > 0,
            "armed real DIRECT producer executed on valid root");
  for (float bad : badValues) {
    scene.environment->fogVisibility = bad;
    const auto source = scene.root->getNodeId();
    const unsigned calls = audit->calls, commands = audit->successful,
                   producers = audit->producers;
    action.apply(scene.root);
    const auto rejected = image(*target);
    require(action.getLastStatus() != CoinRenderAction::SUCCESS &&
                action.getLastError().getLength() > 0,
            "nonfinite public fog is rejected diagnostically");
    require(rejected.serial == stable.serial && rejected.rgba == stable.rgba &&
                rejected.depth == stable.depth,
            "invalid fog preserves exact RGBA/depth/serial");
    require(
        source == scene.root->getNodeId() && audit->producers == producers &&
            audit->successful == commands && (!gpu || audit->calls == calls),
        "invalid fog has no producer/GPU execution or source mutation");
    stable = draw(10, 10, "same-target recovery after invalid fog");
  }
  for (float v : {-100.f, -0.f, 0.f})
    stable = draw(v, 10, "finite nonpositive visibility inherits far10");
  stable = draw(20, 20, "positive visibility20 independent numeric control");
  draw(10, 10, "authorial visibility restored");
  action.setRenderTarget(nullptr);
  return true;
}
} // namespace
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  const std::string mode = argv[1];
  if (mode != "--core" && mode != "--cpu" && mode != "--gpu")
    return 2;
  SoDB::init();
  CoinRenderAction::initClass();
  if (mode == "--core")
    return core() ? 0 : 1;
  const bool available = publicPath(mode == "--gpu");
  return available ? (ok ? 0 : 1) : 77;
}
