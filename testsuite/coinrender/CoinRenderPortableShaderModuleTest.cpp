#include "CoinRenderBumpProfileFixture.h"
#include "CoinRenderBumpShadowFixture.h"
#include "CoinRenderPortableShaderModuleCases.h"
#include "CoinRenderTexture2dStartupTrace.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoFragmentShader.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoScale.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShaderParameter.h>
#include <Inventor/nodes/SoShaderProgram.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#ifdef COIN_PORTABLE_MODULE_X11
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#endif
#include "rendering/coinrender/CoinRenderShaderModuleCore.h"
#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include <cctype>
#include <chrono>
#include <cstring>
#include <set>
#ifdef COIN_PORTABLE_MODULE_RUST
#include "rendering/coinwgpu/CoinWgpuPortableCore.h"
#endif
namespace portable_module_test {
class Audit : public bump_profile::Proxy {
public:
  using Proxy::Proxy;
  unsigned entries = 0;
  bool failRoot = false;
  mutable CoinRenderRttPlan graph;
  CoinRenderFramePlan producerPlan;
  unsigned producerSamples = 0;
  size_t retiredRtt = 0;
  void finishRtt(const std::vector<uint64_t> &tokens) override {
    retiredRtt += tokens.size();
    Proxy::finishRtt(tokens);
  }
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan &g,
                                      const CoinRenderFramePlan &f,
                                      const SbVec2i32 &size) const override {
    graph = g.inspection(f);
    return Proxy::preflightRtt(g, f, size);
  }
  CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan &f,
                                   const SbVec2i32 &size, uint64_t id,
                                   CoinRenderTargetP &t,
                                   uint64_t &token) override {
    producerPlan = f;
    producerSamples = t.options.sampleCount;
    return Proxy::submitRtt(f, size, id, t, token);
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,
                                CoinRenderTargetP &t) override {
    ++entries;
    if (failRoot) {
      plan = f;
      return {CoinRenderBackendStatus::UNSUPPORTED,
              "independent late parent refusal after RTT preparation", 0};
    }
    return Proxy::submit(f, t);
  }
  CoinRenderSubmitResult
  submit(const CoinRenderFramePlan &f, CoinRenderTargetP &t,
         const CoinRenderFrameReuseDecision &r) override {
    ++entries;
    if (failRoot) {
      plan = f;
      return {CoinRenderBackendStatus::UNSUPPORTED,
              "independent late parent refusal after RTT preparation", 0};
    }
    return Proxy::submit(f, t, r);
  }
};
struct Run {
  Scene scene;
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderAction action{SbViewportRegion(64, 64)};
  Audit *audit = nullptr;
  unsigned samples = 1, extent = 64;
  bool window = false;
  Frame lastPhysical;
#ifdef COIN_PORTABLE_MODULE_X11
  Display *display = nullptr;
  Window nativeWindow = 0;
#endif
  std::vector<std::string> lastCompiles;
  std::vector<std::string> lastCompileModules;
  explicit Run(
      const CoinRenderCapabilities &caps, bool portable = false,
      unsigned requestedSamples = 1, bool shown = false,
      CoinRenderSceneTextureMode route = COIN_RENDER_SCENE_TEXTURE_STAGED)
      : samples(requestedSamples), window(shown) {
    CoinRenderOptions options{};
    options.renderer = static_cast<CoinRenderRenderer>(caps.renderer);
    options.sampleCount = samples;
    options.sceneTexture = route;
    if (portable)
      options.textureSamplingPolicy = COIN_RENDER_SAMPLING_PORTABLE;
#ifdef COIN_PORTABLE_MODULE_X11
    if (window) {
      display = XOpenDisplay(nullptr);
      if (!display)
        return;
      nativeWindow = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                         16, 16, extent, extent, 0, 0, 0);
      XSetWindowAttributes attrs{};
      attrs.override_redirect = True;
      XChangeWindowAttributes(display, nativeWindow, CWOverrideRedirect,
                              &attrs);
      XMapRaised(display, nativeWindow);
      XSync(display, False);
      CoinRenderNativeSurfaceDescriptor native{};
      native.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
      native.structSize = sizeof(native);
      native.type = COIN_RENDER_SURFACE_XLIB;
      native.native.xlib.display = display;
      native.native.xlib.window = nativeWindow;
      target.reset(CoinRenderTarget::createWindow(
          native, SbVec2i32(extent, extent), options));
    }
#endif
    if (!window)
      target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(extent, extent),
                                                     options));
    if (!target)
      return;
    auto &p = target->getPimpl().get();
    if (p.prepareBackend() != CoinRenderBackendStatus::SUCCESS || !p.backend ||
        !p.backend->isGpuBackend()) {
      target.reset();
      return;
    }
    audit = new Audit(std::move(p.backend));
    p.backend.reset(audit);
    if (!window)
      target->setDepthReadbackEnabled(TRUE);
    action.setRenderTarget(target.get());
    action.setTransparencyType(CoinRenderAction::NONE);
    action.setBackgroundColor(SbColor4f(0, 0, 0, window ? 1 : 0));
  }
  ~Run() {
    action.setRenderTarget(nullptr);
    target.reset();
#ifdef COIN_PORTABLE_MODULE_X11
    if (nativeWindow)
      XDestroyWindow(display, nativeWindow);
    if (display)
      XCloseDisplay(display);
#endif
  }
  bool resize(unsigned size) {
#ifdef COIN_PORTABLE_MODULE_X11
    if (window) {
      XResizeWindow(display, nativeWindow, size, size);
      XSync(display, False);
    }
#endif
    if (!target->resize(SbVec2i32(size, size)))
      return false;
    extent = size;
    action.setViewportRegion(SbViewportRegion(size, size));
    return true;
  }
  bool physical(Frame &f) {
#ifdef COIN_PORTABLE_MODULE_X11
    if (!window || !display)
      return false;
    XSync(display, False);
    XWindowAttributes attrs{};
    if (!XGetWindowAttributes(display, nativeWindow, &attrs) ||
        attrs.width != int(extent) || attrs.height != int(extent))
      return false;
    XImage *image = XGetImage(display, nativeWindow, 0, 0, extent, extent,
                              AllPlanes, ZPixmap);
    if (!image)
      return false;
    auto channel = [](unsigned long pixel, unsigned long mask) {
      if (!mask)
        return uint8_t(0);
      unsigned shift = 0;
      while (!(mask & 1)) {
        mask >>= 1;
        ++shift;
      }
      return uint8_t((((pixel >> shift) & mask) * 255 + mask / 2) / mask);
    };
    f.rgba.resize(size_t(extent) * extent * 4);
    for (unsigned y = 0; y < extent; ++y)
      for (unsigned x = 0; x < extent; ++x) {
        const auto pixel = XGetPixel(image, x, y);
        const size_t p = (size_t(y) * extent + x) * 4;
        f.rgba[p] = channel(pixel, image->red_mask);
        f.rgba[p + 1] = channel(pixel, image->green_mask);
        f.rgba[p + 2] = channel(pixel, image->blue_mask);
        f.rgba[p + 3] = 255;
      }
    XDestroyImage(image);
    return true;
#else
    (void)f;
    return false;
#endif
  }
  Frame read() {
    Frame f;
    target->readbackRGBA(f.rgba);
    if (!window)
      target->readbackDepth(f.depth);
    f.serial = target->getLastSubmissionSerial();
    return f;
  }
  bool bind(const Module &m) {
    return check(action.setPortableShaderModule(scene.program, m),
                 "valid module association accepted");
  }
  bool render(Frame &frame, const char *label, SoNode *sourceRoot = nullptr) {
    if (!target)
      return false;
    if (window && !check(target->requestWindowReadbackRGBA(),
                         "same-apply window readback request accepted"))
      return false;
    const auto serial = target->getLastSubmissionSerial();
    const auto entries = audit->entries;
    if (!sourceRoot)
      sourceRoot = scene.root;
    const auto node = sourceRoot->getNodeId(),
               program = scene.program->getNodeId();
    startup::Environment tracing("COIN_RENDER_TRACE_PHASES", "1");
    startup::Trace trace;
    if (!check(trace.start(), "passive module compiler trace available"))
      return false;
    const auto start = std::chrono::steady_clock::now();
    action.apply(sourceRoot);
    const auto end = std::chrono::steady_clock::now();
    const std::string log = trace.stop();
    lastCompiles.clear();
    lastCompileModules.clear();
    std::istringstream lines(log);
    std::string line;
    bool compileFields = true;
    while (std::getline(lines, line)) {
      if (line.find("COIN_RENDER_PHASE portable_shader_compile ") ==
          std::string::npos)
        continue;
      std::istringstream fields(line);
      std::string word, module, profile;
      while (fields >> word) {
        if (word.find("module=") == 0)
          module = word.substr(7);
        if (word.find("profile=") == 0)
          profile = word.substr(8);
      }
      compileFields &= !module.empty() && !profile.empty();
      lastCompiles.push_back(module + ":" + profile);
      lastCompileModules.push_back(module);
    }
    frame = read();
    std::cout << "portable_module_apply phase=" << label
              << " status=" << action.getLastStatus() << " serial=" << serial
              << "->" << frame.serial << " backend_entries=" << entries << "->"
              << audit->entries << " apply_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                       start)
                     .count()
              << " error=" << action.getLastError().getString() << std::endl;
    bool ok =
        check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                  frame.serial > serial && audit->entries == entries + 1 &&
                  target->getOptions().sampleCount == samples &&
                  sourceRoot->getNodeId() == node &&
                  scene.program->getNodeId() == program,
              "one actual GPU module publication without mutating sources");
    return ok &&
           check(
               compileFields,
               "real compile event has stable module and profile identifiers");
  }
  bool draw(Color expected, Frame &frame, const char *label) {
    return render(frame, label) && image(frame, expected);
  }
  bool refuse(const Frame &stable, const char *label,
              SoNode *sourceRoot = nullptr) {
    const auto entries = audit->entries;
    const auto producers = audit->producers;
    action.apply(sourceRoot ? sourceRoot : scene.root);
    const auto after = read();
    std::cout << "portable_module_refuse phase=" << label
              << " status=" << action.getLastStatus()
              << " backend_entries=" << entries << "->" << audit->entries
              << " serial=" << after.serial
              << " error=" << action.getLastError().getString() << std::endl;
    return check(action.getLastStatus() != CoinRenderAction::SUCCESS &&
                     action.getLastError().getLength() > 0 &&
                     after.serial == stable.serial &&
                     after.rgba == stable.rgba && after.depth == stable.depth &&
                     audit->producers == producers,
                 "invalid module/bindings retain exact current publication and "
                 "execute no producers");
  }
};
bool cpu() {
  Scene scene;
  CoinRenderAction a(SbViewportRegion(64, 64)), peer(SbViewportRegion(64, 64));
  Module good = literal({{.1f, .4f, .7f, 1}});
  good.alphaMayVary = FALSE;
  const int before = scene.program->getRefCount();
  bool ok = check(!a.setPortableShaderModule(nullptr, good),
                  "null association rejects without a target/context");
  ok &= check(a.setPortableShaderModule(scene.program, good),
              "pure public setter accepts valid AST");
  ok &= check(scene.program->getRefCount() > before,
              "association retains the program safely");
  good.expressions.clear();
  Module invalid;
  invalid.expressions.clear();
  ok &= check(!a.setPortableShaderModule(scene.program, invalid),
              "invalid replacement rejected transactionally");
  Module other = literal({{.7f, .2f, .1f, 1}});
  ok &=
      check(peer.setPortableShaderModule(scene.program, other),
            "different action independently owns same public node association");
  a.clearPortableShaderModule(scene.program);
  a.clearPortableShaderModule(scene.program);
  peer.clearPortableShaderModules();
  peer.clearPortableShaderModules();
  ok &= check(scene.program->getRefCount() == before,
              "all association ownership released after idempotent clears");
  std::cout << "portable_module_cpu complete=" << ok << " gpu_queries=0"
            << std::endl;
  return ok;
}
bool smoke(Run &run) {
  Frame plain, first, back;
  bool ok = run.draw({{.6f, .3f, .8f, 1}}, plain, "ordinary predecessor");
  run.scene.attach(true);
  Module m = literal({{.13f, .57f, .29f, 1}});
  m.alphaMayVary = FALSE;
  ok &= run.bind(m);
  m.expressions[0].value = {{.9f, .05f, .95f, 1}};
  ok &= run.draw({{.13f, .57f, .29f, 1}}, first,
                 "immutable copied module, not legacy red shader");
  const auto stable = run.read();
  Module invalid = m;
  invalid.version = 2;
  ok &= check(!run.action.setPortableShaderModule(run.scene.program, invalid),
              "invalid setter does not replace valid module");
  const auto afterSetter = run.read();
  ok &= check(afterSetter.serial == stable.serial &&
                  afterSetter.rgba == stable.rgba &&
                  afterSetter.depth == stable.depth,
              "failed setter does not publish");
  ok &= run.draw({{.13f, .57f, .29f, 1}}, back,
                 "previous association survives invalid setter");
  Module primary;
  primary.expressions.push_back(operation(Op::PRIMARY_COLOR, Type::FLOAT4));
  ok &= run.bind(primary);
  ok &=
      run.draw({{.6f, .3f, .8f, 1}}, back, "authored primary before texturing");
  run.action.clearPortableShaderModule(run.scene.program);
  ok &= run.refuse(
      back, "unadapted active legacy source remains explicitly unsupported");
  m = literal({{.13f, .57f, .29f, 1}});
  m.alphaMayVary = FALSE;
  ok &= run.bind(m);
  ok &= run.draw({{.13f, .57f, .29f, 1}}, back,
                 "same-target module A/B/A recovery");
  run.action.clearPortableShaderModules();
  run.scene.attach(false);
  ok &= run.draw({{.6f, .3f, .8f, 1}}, back,
                 "cleared association does not leak to ordinary sibling");
  return ok;
}
Module arithmetic() {
  Module m;
  auto push = [&](Expr e) {
    m.expressions.push_back(e);
    return uint32_t(m.expressions.size() - 1);
  };
  uint32_t x = push(constant({{.2f, .4f, .6f, 1}}));
  x = push(operation(Op::ADD, Type::FLOAT4, x,
                     push(constant({{.1f, .2f, .3f, 0}}))));
  x = push(operation(Op::SUBTRACT, Type::FLOAT4, x,
                     push(constant({{.05f, .1f, .2f, 0}}))));
  x = push(operation(Op::MULTIPLY, Type::FLOAT4, x,
                     push(constant({{.5f, 0, 0, 0}}, Type::FLOAT1))));
  x = push(operation(Op::DIVIDE, Type::FLOAT4, x,
                     push(constant({{2, 0, 0, 0}}, Type::FLOAT1))));
  x = push(operation(Op::MAXIMUM, Type::FLOAT4, x,
                     push(constant({{.08f, .02f, .22f, .5f}}))));
  x = push(operation(Op::MINIMUM, Type::FLOAT4, x,
                     push(constant({{.9f, .11f, .8f, .8f}}))));
  auto lo = push(constant({{.1f, 0, 0, 0}}, Type::FLOAT1)),
       hi = push(constant({{.6f, 0, 0, 0}}, Type::FLOAT1));
  x = push(operation(Op::CLAMP, Type::FLOAT4, x, lo, hi));
  auto a = push(constant({{.2f, .4f, 0, 0}}, Type::FLOAT2)),
       b = push(constant({{.5f, .25f, 0, 0}}, Type::FLOAT2));
  auto d = push(operation(Op::DOT, Type::FLOAT1, a, b));
  x = push(operation(Op::ADD, Type::FLOAT4, x, d));
  x = push(
      operation(Op::ADD, Type::FLOAT4, x, push(constant({{0, 0, 0, .3f}}))));
  m.output = x;
  return m;
}
bool ops(Run &run) {
  run.scene.attach(true);
  Frame f;
  Module m = arithmetic();
  bool ok = run.bind(m);
  ok &= run.draw({{.3f, .31f, .42f, 1}}, f,
                 "all arithmetic operators and vector/scalar broadcasts");
  Module reordered = m;
  std::reverse(reordered.expressions.begin(), reordered.expressions.end());
  const uint32_t n = uint32_t(reordered.expressions.size());
  for (auto &e : reordered.expressions)
    for (auto &input : e.inputs)
      if (input != UINT32_MAX)
        input = n - 1 - input;
  reordered.output = n - 1 - m.output;
  ok &= run.bind(reordered);
  ok &= run.draw({{.3f, .31f, .42f, 1}}, f,
                 "same DAG reversed with forward references");
  return ok;
}
bool precision(Run &run) {
  run.scene.attach(true);
  Module m;
  m.expressions = {constant({{.12345678f, 0, 0, 0}}, Type::FLOAT1),
                   constant({{.123456f, 0, 0, 0}}, Type::FLOAT1),
                   operation(Op::SUBTRACT, Type::FLOAT1, 0, 1),
                   constant({{1000000, 0, 0, 0}}, Type::FLOAT1),
                   operation(Op::MULTIPLY, Type::FLOAT1, 2, 3),
                   constant({{.7f, .3f, .9f, 0}}),
                   operation(Op::MULTIPLY, Type::FLOAT4, 4, 5),
                   constant({{0, 0, 0, 1}}),
                   operation(Op::ADD, Type::FLOAT4, 6, 7)};
  m.output = 8;
  const float amplified = (.12345678f - .123456f) * 1000000.f;
  Frame f;
  bool ok = run.bind(m);
  ok &= run.draw({{amplified * .7f, amplified * .3f, amplified * .9f, 1}}, f,
                 "f32 constants amplified without decimal quantization");
  m.expressions = {constant({{.2f, .9f, .5f, 1}}),
                   constant({{.7f, .7f, .7f, 1}}),
                   constant({{.4f, .4f, .4f, 1}}),
                   operation(Op::CLAMP, Type::FLOAT4, 0, 1, 2)};
  m.output = 3;
  ok &= run.bind(m);
  ok &= run.draw({{.4f, .4f, .4f, 1}}, f,
                 "CLAMP min(max(x,lo),hi) including inverted bounds");
  return ok;
}
bool uniforms(Run &run) {
  run.scene.attach(true);
  run.scene.clearParameters();
  Module m;
  const std::string first = "first field / logical;[]", last = "last-field.255";
  m.expressions = {uniform(first.c_str(), Type::FLOAT4),
                   uniform(last.c_str(), Type::FLOAT4),
                   operation(Op::ADD, Type::FLOAT4, 0, 1)};
  m.output = 2;
  std::vector<SoShaderParameter4f *> fields;
  for (unsigned i = 0; i < 256; ++i) {
    const std::string name = i == 0     ? first
                             : i == 255 ? last
                                        : "u" + std::to_string(i);
    declare(m, name.c_str(), Type::FLOAT4);
    Color value = {
        {float(i % 7) * .01f, float(i % 11) * .01f, float(i % 13) * .01f, .5f}};
    if (i == 0)
      value = {{.1f, .2f, .3f, .25f}};
    if (i == 255)
      value = {{.6f, .35f, .2f, .75f}};
    fields.push_back(run.scene.parameter4(name, value));
  }
  Frame a, b, back;
  bool ok = run.bind(m);
  ok &= run.draw({{.7f, .55f, .5f, 1}}, a,
                 "full4KiB signature first and last fields consumed");
  ok &= check(!run.lastCompiles.empty(), "cold first module actually compiles "
                                         "a GPU variant and exposes its event");
  fields.back()->value.setValue(.7f, .25f, .35f, .75f);
  ok &= run.draw({{.8f, .45f, .65f, 1}}, b,
                 "last uniform mutation keeps exact module signature");
  ok &= check(run.lastCompiles.empty(),
              "uniform-value-only mutation reuses compiled module/pipeline");
  unsigned changed = 0;
  for (size_t i = 0; i < a.rgba.size() && i < b.rgba.size(); ++i)
    changed += i % 4 < 3 && std::abs(int(a.rgba[i]) - int(b.rgba[i])) > 3;
  ok &= check(changed > 1000,
              "last field has observable nontruncated GPU contribution");
  auto middle = fields[128]->value.getValue();
  fields[128]->value.setValue(std::numeric_limits<float>::quiet_NaN(), .1f, .1f,
                              .5f);
  ok &= run.refuse(
      b, "unreachable signature field still rejects nonfinite actual payload");
  fields[128]->value = middle;
  fields.back()->value.setValue(.6f, .35f, .2f, .75f);
  ok &= run.draw({{.7f, .55f, .5f, 1}}, back,
                 "full signature A/B/A recovery on same target");
  ok &= check(run.lastCompiles.empty(),
              "uniform-value A/B/A restoration does not compile/link again");
  run.scene.fragment->parameter.setNum(255);
  ok &=
      run.refuse(back, "missing last actual uniform cannot reuse old bindings");
  run.scene.fragment->parameter.set1Value(255, fields.back());
  ok &= run.draw({{.7f, .55f, .5f, 1}}, back,
                 "missing uniform restored without target recreation");
  ok &= check(run.lastCompiles.empty(),
              "restored binding reuses the same compiled signature");
  return ok;
}

#include "CoinRenderPortableShaderCompilerFailureCases.h"
#include "CoinRenderPortableShaderCompilerProducerCases.h"
#include "CoinRenderPortableShaderDelivery.h"
#include "CoinRenderPortableShaderDenseCases.h"
#include "CoinRenderPortableShaderInterningCases.h"
#include "CoinRenderPortableShaderOwnerCases.h"
#include "CoinRenderPortableShaderPreparationCases.h"
#include "CoinRenderBumpOccurrenceCases.h"
#include "CoinRenderPortableShaderPresentationCases.h"
#include "CoinRenderPortableShaderProjectionCases.h"
#include "CoinRenderPortableShaderRawCases.h"
#include "CoinRenderPortableShaderRttCases.h"
#include "CoinRenderPortableShaderScopeCases.h"
#include "CoinRenderPortableShaderShadowCases.h"
#include "CoinRenderPortableShaderTextureCases.h"
#include "CoinRenderPortableShaderTypedOpsCases.h"
} // namespace portable_module_test
int main(int argc, char **argv) {
  std::string mode = "smoke";
  bool core = false, cpu = false, portable = false, window = false;
  CoinRenderSceneTextureMode route = COIN_RENDER_SCENE_TEXTURE_STAGED;
  unsigned samples = 1;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--direct")
      route = COIN_RENDER_SCENE_TEXTURE_DIRECT;
    else if (arg == "--staged")
      route = COIN_RENDER_SCENE_TEXTURE_STAGED;
    else if (arg == "--window")
      window = true;
    else if (arg == "--samples" && i + 1 < argc) {
      const std::string count = argv[++i];
      if (count != "1" && count != "2" && count != "4")
        return 2;
      samples = unsigned(count[0] - '0');
    } else if (arg == "--portable-sampling")
      portable = true;
    else if (arg == "--core")
      core = true;
    else if (arg == "--cpu")
      cpu = true;
    else if (arg == "--gpu") {
    } else if (arg == "--mode" && i + 1 < argc)
      mode = argv[++i];
    else
      return 2;
  }
  if (mode != "smoke" && mode != "ops" && mode != "uniforms" &&
      mode != "precision" && mode != "scope" && mode != "textures" &&
      mode != "owners" && mode != "alpha" && mode != "bindings" &&
      mode != "presentation" && mode != "lifecycle" && mode != "rtt" &&
      mode != "raw" && mode != "shadows" && mode != "projection" &&
      mode != "interning" && mode != "compiler-failure" && mode != "dense" &&
      mode != "compiler-producer" && mode != "typed1" && mode != "typed2" &&
      mode != "typed3" && mode != "typed4" && mode != "prepare-module" &&
      mode != "prepare-primary" && mode != "prepare-mixed" &&
      mode != "prepare-ordinary" && mode != "prepare-shadow" &&
      mode != "prepare-bump-mixed" && mode != "occurrence-core" &&
      mode != "prepare-occurrence-legacy" && mode != "prepare-occurrence-extended" &&
      mode != "prepare-occurrence-shadow")
    return 2;
  if (mode == "compiler-failure" && route != COIN_RENDER_SCENE_TEXTURE_DIRECT)
    return 2;
  if (mode == "compiler-producer" &&
      (route != COIN_RENDER_SCENE_TEXTURE_STAGED || samples != 4 || window))
    return 2;
  if ((window || samples != 1) && mode != "presentation" &&
      mode != "lifecycle" && mode != "rtt" && mode != "raw" &&
      mode != "shadows" && mode != "compiler-producer" &&
      mode.compare(0, 8, "prepare-") != 0)
    return 2;
  if (window && (mode == "lifecycle" || mode == "raw"))
    return 2;
#ifdef COIN_PORTABLE_MODULE_X11
  if (window && !XInitThreads())
    return 1;
#else
  if (window)
    return 77;
#endif
  if (window && (!std::getenv("DISPLAY") || !*std::getenv("DISPLAY")))
    return 77;
  SoDB::init();
  CoinRenderAction::initClass();
  if (mode == "occurrence-core") {
    if (!core || window || samples != 1) return 2;
    return portable_module_test::occurrenceCore() ? 0 : 1;
  }
  if (core)
    return portable_module_test::core() ? 0 : 1;
  if (cpu)
    return portable_module_test::cpu() ? 0 : 1;
  CoinRenderCapabilities caps{};
  const int query = coin_render_query_capabilities(
      window ? COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW
             : COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
      &caps, sizeof(caps));
  std::cout << "portable_module_environment query=" << query
            << " gpu=" << caps.gpu_available << " renderer=" << caps.renderer
            << " adapter=" << caps.adapter_name << std::endl;
  if (query)
    return 1;
  if (!caps.gpu_available)
    return 77;
  if (mode == "lifecycle")
    return portable_module_test::lifecycle(caps, portable, samples) ? 0 : 1;
  portable_module_test::Run run(caps, portable, samples, window, route);
  if (!run.target)
    return 1;
  return (mode == "prepare-occurrence-legacy" || mode == "prepare-occurrence-extended" ||
                  mode == "prepare-occurrence-shadow"
              ? portable_module_test::occurrenceLighting(run,
                    mode == "prepare-occurrence-legacy" ? 0u :
                    mode == "prepare-occurrence-extended" ? 1u : 2u)
          : mode == "prepare-bump-mixed"
              ? portable_module_test::preparationBumpMixed(run)
          : mode == "prepare-shadow"
              ? portable_module_test::preparationShadow(run)
          : mode.compare(0, 8, "prepare-") == 0
              ? portable_module_test::preparationBasic(run, mode.substr(8))
          : (mode == "typed1" || mode == "typed2" || mode == "typed3" ||
             mode == "typed4")
              ? portable_module_test::typedOps(run, unsigned(mode.back() - '0'))
          : mode == "compiler-producer"
              ? portable_module_test::compilerProducer(run, caps)
          : mode == "dense" ? portable_module_test::dense(run)
          : mode == "compiler-failure"
              ? portable_module_test::compilerFailure(run)
          : mode == "interning"  ? portable_module_test::interning(run)
          : mode == "projection" ? portable_module_test::projectionPolicies(run)
          : mode == "shadows"    ? portable_module_test::shadows(run)
          : mode == "raw"        ? portable_module_test::raw63(run)
          : mode == "rtt"        ? portable_module_test::rtt(run, route)
          : mode == "presentation"
              ? portable_module_test::presentation(run, caps, portable)
          : mode == "smoke"     ? portable_module_test::smoke(run)
          : mode == "ops"       ? portable_module_test::ops(run)
          : mode == "precision" ? portable_module_test::precision(run)
          : mode == "scope"     ? portable_module_test::scoped(run)
          : mode == "textures"  ? portable_module_test::textures(run)
          : mode == "owners"    ? portable_module_test::owners(run)
          : mode == "alpha"     ? portable_module_test::alpha(run)
          : mode == "bindings"  ? portable_module_test::bindings(run)
                                : portable_module_test::uniforms(run))
             ? 0
             : 1;
}
