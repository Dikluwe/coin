// Private FramePlan ingress controls; this makes no public SoColorMask claim.
#define main coin_render_mask_qualified_main
#include "CoinRenderBumpMultilightTest.cpp"
#undef main
#ifdef COIN_MULTILIGHT_MASK_X11
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#endif
#include <chrono>
namespace mask_test {
using namespace multilamp;
const unsigned clearBytes[4] = {17, 207, 91, 63};
// Flush diagnostic boundaries even when a cold driver call never returns.
void phase(const char *name, const char *state = "", unsigned mask = 0,
           bool enabled = true) {
  static const auto start = std::chrono::steady_clock::now();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - start)
                      .count();
  std::cerr << "mask_phase name=" << name << " state=" << state
            << " mask=" << mask << " enabled=" << enabled
            << " elapsed_ms=" << ms << std::endl;
}
struct Source {
  Settings settings;
  Scene scene;
  std::unique_ptr<CoinRenderTarget> target;
  Recorder *recorder = nullptr;
  CoinRenderAction action{SbViewportRegion(side, side)};
  CoinRenderFramePlan original;
  static Settings configured(unsigned samples) {
    Settings result;
    result.samples = samples;
    return result;
  }
  explicit Source(unsigned samples)
      : settings(configured(samples)), scene(settings) {
    scene.setSpec({{0, 0, 0}});
    for (size_t i = 0; i < scene.maps.size(); ++i)
      scene.casterAt(int(i), 3);
    auto options = samplingTestOptions();
    options.sampleCount = samples;
    target.reset(
        CoinRenderTarget::createOffscreen(SbVec2i32(side, side), options));
    if (!target)
      return;
    recorder = new Recorder;
    target->getPimpl()->backend.reset(recorder);
    action.setRenderTarget(target.get());
    action.setTransparencyType(CoinRenderAction::NONE);
    action.apply(scene.root);
    if (action.getLastStatus() == CoinRenderAction::SUCCESS)
      original = recorder->frame;
  }
  ~Source() { action.setRenderTarget(nullptr); }
  CoinRenderFramePlan packet(bool enabled, unsigned mask,
                             bool window = false) const {
    auto frame = original;
    // This edited IR is not the immutable captured revision. Zero requests
    // common Target admission for every authored packet, including negatives.
    frame.revision = 0;
    frame.clearColor.setValue(clearBytes[0] / 255.f, clearBytes[1] / 255.f,
                              clearBytes[2] / 255.f,
                              window ? 1.f : clearBytes[3] / 255.f);
    for (const auto &draw : frame.draws)
      if (!draw.shadowLightSlot &&
          draw.sourceNodeId == scene.mesh->getNodeId()) {
        auto &state = frame.renderStates[draw.renderStateSlot];
        state.colorWrite = enabled;
        state.colorWriteMask = uint8_t(mask);
      }
    return frame;
  }
};
bool transport(const Source &source, const CoinRenderFramePlan &frame,
               bool enabled, unsigned mask) {
  std::string error;
  if (!check(frame.isValid(&error),
             "legal private color mask Core admission " + error))
    return false;
  CoinWgpuFfiFrame ffi;
  const bool prepared = ffi.prepareBumpFrame(frame, side, side, error);
  if (!check(prepared, "legal private color mask FFI " + error))
    return false;
  const auto &view = ffi.getView();
  unsigned witnesses = 0;
  bool ok = true;
  for (const auto &draw : frame.draws)
    if (!draw.shadowLightSlot &&
        draw.sourceNodeId == source.scene.mesh->getNodeId()) {
      ++witnesses;
      const auto &s = frame.renderStates[draw.renderStateSlot];
      const auto &g = view.states[draw.renderStateSlot];
      unsigned encoded = !enabled || !mask ? 0 : mask == 15 ? 1 : 0x10 | mask;
      ok &= check(
          s.colorWrite == enabled && s.colorWriteMask == mask &&
              g.color_write == encoded,
          "actual consumed draw retains bool/mask and ABI62 channel mapping");
      ok &=
          check(s.depthTest && s.depthWrite && g.depth_test && g.depth_write &&
                    g.depth_function == unsigned(s.depthFunction),
                "mask preserves independent test/write/function depth state");
    }
  return check(witnesses > 0,
               "private mask applies to real bump receiver draw") &&
         ok;
}
RGB litColor(const Source &source) {
  unsigned ids[3] = {0, 1, 2};
  double w[3] = {.3, .4, .3};
  bool known = true;
  auto result = expected(source.scene, source.scene.contexts[0], 0, 0, 0, w,
                         ids, known, 0, 0);
  check(known, "all enabled maps are analytically clear for mask witness");
  return result;
}
bool image(const Source &source, const Frame &frame, bool enabled,
           unsigned mask, bool window, bool withDepth, bool report = true) {
  if (frame.rgba.size() != side * side * 4 ||
      (withDepth && frame.depth.size() != side * side))
    return false;
  auto lit = litColor(source);
  unsigned inside = 0, outside = 0, bad = 0, depthBad = 0;
  const double margin = 2.1 * 4 / side;
  for (unsigned y = 0; y < side; ++y)
    for (unsigned x = 0; x < side; ++x) {
      size_t p = size_t(y * side + x);
      double wx = (x + .5) * 4 / side - 2, wy = 2 - (y + .5) * 4 / side;
      bool in = wx > -1.4 + margin && wx < 1.3 - margin && wy > -1.3 + margin &&
                wy < 1.35 - margin;
      bool out = wx < -1.4 - margin || wx > 1.3 + margin ||
                 wy < -1.3 - margin || wy > 1.35 + margin;
      inside += in;
      outside += out;
      for (int c = 0; c < 4; ++c) {
        const double clear = c == 3 && window ? 255 : clearBytes[c];
        const double written = c == 3 ? 255 : lit[c];
        const bool write = enabled && (mask & (1u << c));
        double value = frame.rgba[p * 4 + c];
        if (in || out || !write) {
          double expected = in && write ? written : clear;
          bad += std::abs(value - expected) > (c == 3 ? 1 : 3);
        } else
          bad += value < std::min(clear, written) - 3 ||
                 value > std::max(clear, written) + 3;
      }
      if (withDepth) {
        if (in)
          depthBad += std::abs(frame.depth[p] - 3.f / 8) > 2e-5f;
        else if (out)
          depthBad += frame.depth[p] != 1;
        else
          depthBad +=
              std::abs(frame.depth[p] - 3.f / 8) > 2e-5f && frame.depth[p] != 1;
      }
    }
  if (report)
    std::cout << "multilight_private_mask enabled=" << enabled
              << " mask=" << mask << " inside=" << inside
              << " outside=" << outside << " bad=" << bad
              << " depth_bad=" << depthBad << '\n';
  return inside > 500 && outside > 500 && bad == 0 && depthBad == 0;
}
Frame read(CoinRenderTarget &t, bool depth) {
  Frame f;
  t.readbackRGBA(f.rgba);
  if (depth)
    t.readbackDepth(f.depth);
  f.serial = t.getLastSubmissionSerial();
  return f;
}
bool cpu(Source &source) {
  if (!check(source.recorder && source.original.draws.size(),
             "CPU-only public authoring capture"))
    return false;
  bool ok = true;
  for (unsigned mask = 0; mask < 16; ++mask) {
    auto frame = source.packet(true, mask);
    ok &= transport(source, frame, true, mask);
    auto result = source.target->getPimpl()->executeFrame(frame);
    ok &= check(result.status == CoinRenderBackendStatus::SUCCESS,
                "legal mask accepted by actual target/Core ingress") &&
          transport(source, source.recorder->frame, true, mask);
  }
  auto off = source.packet(false, 15);
  ok &= transport(source, off, false, 15);
  auto result = source.target->getPimpl()->executeFrame(off);
  ok &= check(result.status == CoinRenderBackendStatus::SUCCESS,
              "colorWrite false consumed independently of mask") &&
        transport(source, source.recorder->frame, false, 15);
  for (bool enabled : {false, true}) {
    auto invalid = source.packet(enabled, 0x10);
    std::string error;
    ok &= check(!invalid.isValid(&error) && !error.empty(),
                "unknown mask bit rejected by actual Core validation");
    CoinWgpuFfiFrame ffi;
    error.clear();
    bool accepted = ffi.prepareBumpFrame(invalid, side, side, error);
    ok &= check(!accepted && !error.empty(),
                "unknown mask bit rejected by actual FFI ingress");
    auto stable = read(*source.target, true);
    auto submits = source.recorder->submits;
    result = source.target->getPimpl()->executeFrame(invalid);
    auto after = read(*source.target, true);
    std::cout << "mask_invalid_cpu enabled=" << enabled
              << " revision=" << invalid.revision
              << " captured_revision=" << source.original.revision
              << " status=" << unsigned(result.status) << " submits=" << submits
              << "->" << source.recorder->submits << " serial=" << stable.serial
              << "->" << after.serial
              << " rgba_preserved=" << (stable.rgba == after.rgba)
              << " depth_preserved=" << (stable.depth == after.depth)
              << " error=" << result.diagnostic << '\n';
    ok &= check(result.status != CoinRenderBackendStatus::SUCCESS &&
                    source.recorder->submits == submits &&
                    stable.serial == after.serial &&
                    stable.rgba == after.rgba && stable.depth == after.depth,
                "invalid private mask refuses before recorder and preserves "
                "publication");
    auto recovery = source.packet(true, 15);
    result = source.target->getPimpl()->executeFrame(recovery);
    ok &= check(result.status == CoinRenderBackendStatus::SUCCESS,
                "CPU same-target full mask recovery") &&
          transport(source, source.recorder->frame, true, 15);
  }
  return ok;
}
#ifdef COIN_MULTILIGHT_MASK_X11
unsigned channel(unsigned long pixel, unsigned long mask) {
  while (mask && !(mask & 1)) {
    mask >>= 1;
    pixel >>= 1;
  }
  return mask ? unsigned((pixel & mask) * 255 / mask) : 0;
}
bool physical(Display *d, Window w, Frame &f) {
  XSync(d, False);
  XImage *image = XGetImage(d, w, 0, 0, side, side, AllPlanes, ZPixmap);
  if (!image)
    return false;
  f.rgba.resize(side * side * 4);
  for (unsigned y = 0; y < side; ++y)
    for (unsigned x = 0; x < side; ++x) {
      auto pixel = XGetPixel(image, x, y);
      size_t p = size_t(y * side + x) * 4;
      f.rgba[p] = channel(pixel, image->red_mask);
      f.rgba[p + 1] = channel(pixel, image->green_mask);
      f.rgba[p + 2] = channel(pixel, image->blue_mask);
      f.rgba[p + 3] = 255;
    }
  XDestroyImage(image);
  return true;
}
#endif
int gpu(Source &source, unsigned samples, bool window) {
  CoinRenderCapabilities caps{};
  phase("capabilities_begin");
  int query = coin_render_query_capabilities(
      window ? COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW
             : COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
      &caps, sizeof(caps));
  phase("capabilities_end");
  std::cout << "mask_environment query=" << query
            << " gpu=" << caps.gpu_available << " renderer=" << caps.renderer
            << " adapter=" << caps.adapter_name << '\n';
  if (query)
    return 1;
  if (!caps.gpu_available)
    return 77;
  auto options = samplingTestOptions();
  options.sampleCount = samples;
  options.renderer = static_cast<CoinRenderRenderer>(caps.renderer);
  std::unique_ptr<CoinRenderTarget> target;
  phase("target_factory_begin");
#ifdef COIN_MULTILIGHT_MASK_X11
  Display *display = nullptr;
  Window nativeWindow = 0;
  if (window) {
    display = XOpenDisplay(nullptr);
    if (!display)
      return 77;
    nativeWindow = XCreateSimpleWindow(display, DefaultRootWindow(display), 12,
                                       12, side, side, 0, 0, 0);
    XSetWindowAttributes attributes{};
    attributes.override_redirect = True;
    XChangeWindowAttributes(display, nativeWindow, CWOverrideRedirect,
                            &attributes);
    XMapRaised(display, nativeWindow);
    XSync(display, False);
    CoinRenderNativeSurfaceDescriptor native{};
    native.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
    native.structSize = sizeof(native);
    native.type = COIN_RENDER_SURFACE_XLIB;
    native.native.xlib.display = display;
    native.native.xlib.window = nativeWindow;
    target.reset(
        CoinRenderTarget::createWindow(native, SbVec2i32(side, side), options));
  }
#else
  if (window)
    return 77;
#endif
  if (!window)
    target.reset(
        CoinRenderTarget::createOffscreen(SbVec2i32(side, side), options));
  phase("target_factory_end");
  if (!check(bool(target), "private mask actual target factory"))
    return 1;
  auto &p = target->getPimpl().get();
  phase("prepare_backend_begin");
  const auto prepared = p.prepareBackend();
  phase("prepare_backend_end");
  if (!check(prepared == CoinRenderBackendStatus::SUCCESS && p.backend &&
                 p.backend->isGpuBackend(),
             "private mask requested real GPU backend"))
    return 1;
  auto *audit = new bump_shadow::Audit(std::move(p.backend));
  p.backend.reset(audit);
  if (!window)
    target->setDepthReadbackEnabled(TRUE);
  Frame stable, stablePhysical;
  bool ok = true;
  auto execute = [&](bool enabled, unsigned mask, const char *phase) {
    mask_test::phase("packet_begin", phase, mask, enabled);
    auto frame = source.packet(enabled, mask, window);
    std::cerr << "mask_packet state=" << phase << " samples=" << samples
              << " lights=" << source.settings.lights
              << " maps=" << source.settings.maps
              << " draws=" << frame.draws.size()
              << " states=" << frame.renderStates.size() << std::endl;
    mask_test::phase("transport_begin", phase, mask, enabled);
    bool pass = transport(source, frame, enabled, mask);
    mask_test::phase("transport_end", phase, mask, enabled);
    auto old = target->getLastSubmissionSerial();
    auto submits = audit->rootSubmits;
    if (window)
      pass &= target->requestWindowReadbackRGBA() != FALSE;
    mask_test::phase("execute_begin", phase, mask, enabled);
    auto result = p.executeFrame(frame);
    mask_test::phase("execute_end", phase, mask, enabled);
    mask_test::phase("readback_begin", phase, mask, enabled);
    auto current = read(*target, !window);
    mask_test::phase("readback_end", phase, mask, enabled);
    pass &=
        check(result.status == CoinRenderBackendStatus::SUCCESS &&
                  current.serial > old && audit->rootSubmits == submits + 1 &&
                  target->getOptions().sampleCount == samples,
              "one private-frame GPU publication at requested sample count");
    pass &= transport(source, audit->plan, enabled, mask);
    pass &= check(
        image(source, current, enabled, mask, window, !window),
        "independent clear-plus-enabled-channel image and unchanged depth");
    Frame shown;
#ifdef COIN_MULTILIGHT_MASK_X11
    if (window && pass) {
      auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(5);
      bool present = false;
      unsigned attempts = 0;
      do {
        ++attempts;
        if (physical(display, nativeWindow, shown) &&
            image(source, shown, enabled, mask, true, false, false)) {
          bool match = shown.rgba.size() == current.rgba.size();
          for (size_t i = 0; match && i < shown.rgba.size(); ++i)
            if (i % 4 < 3)
              match = std::abs(int(shown.rgba[i]) - int(current.rgba[i])) <= 3;
          if (match && std::chrono::steady_clock::now() <= deadline) {
            present = true;
            break;
          }
        }
      } while (std::chrono::steady_clock::now() < deadline);
      std::cout << "mask_present phase=" << phase << " attempts=" << attempts
                << " current=" << present << '\n';
      pass &= check(present, "current whole masked physical image within "
                             "original five seconds after one execution");
    }
#endif
    std::cout << "mask_execute phase=" << phase << " mask=" << mask
              << " enabled=" << enabled << " status=" << unsigned(result.status)
              << " error=" << result.diagnostic << '\n';
    if (pass) {
      stable = std::move(current);
      stablePhysical = std::move(shown);
    }
    return pass;
  };
  ok &= execute(true, 15, "initial all channels");
  std::vector<unsigned> masks =
      window         ? std::vector<unsigned>{1, 4, 15}
      : samples == 1 ? std::vector<unsigned>{0, 1, 2, 4, 5, 8, 15}
                     : std::vector<unsigned>{1, 4};
  for (unsigned mask : masks) {
    ok &= execute(true, mask, "sampled channel mask");
    ok &= execute(true, 15, "full mask recovery");
  }
  ok &= execute(false, 15, "color write disabled with depth retained");
  ok &= execute(true, 15, "color write true recovery");
  auto invalid = source.packet(true, 0x10, window);
  auto before = stable;
  auto physicalBefore = stablePhysical;
  auto submits = audit->rootSubmits, producers = audit->producers;
  if (window)
    target->requestWindowReadbackRGBA();
  phase("invalid_execute_begin");
  auto result = p.executeFrame(invalid);
  phase("invalid_execute_end");
  auto rejected = read(*target, !window);
  std::cout << "mask_invalid_gpu revision=" << invalid.revision
            << " status=" << unsigned(result.status) << " submits=" << submits
            << "->" << audit->rootSubmits << " producers=" << producers << "->"
            << audit->producers << " serial=" << before.serial << "->"
            << rejected.serial
            << " rgba_preserved=" << (before.rgba == rejected.rgba)
            << " depth_preserved=" << (before.depth == rejected.depth)
            << " error=" << result.diagnostic << '\n';
  ok &= check(
      result.status != CoinRenderBackendStatus::SUCCESS &&
          audit->rootSubmits == submits && audit->producers == producers &&
          rejected.serial == before.serial && rejected.rgba == before.rgba &&
          rejected.depth == before.depth,
      "invalid private mask atomic refusal before "
      "commands/producers/publication");
#ifdef COIN_MULTILIGHT_MASK_X11
  if (window) {
    Frame shown;
    ok &= check(physical(display, nativeWindow, shown) &&
                    shown.rgba == physicalBefore.rgba,
                "invalid private mask preserves exact physical framebuffer");
  }
#endif
  ok &= execute(true, 15, "invalid private mask same-target recovery");
  phase("target_destroy_begin");
  target.reset();
  phase("target_destroy_end");
#ifdef COIN_MULTILIGHT_MASK_X11
  if (nativeWindow)
    XDestroyWindow(display, nativeWindow);
  if (display)
    XCloseDisplay(display);
#endif
  return ok ? 0 : 1;
}
} // namespace mask_test
int main(int argc, char **argv) {
  using namespace mask_test;
  bool cpuMode = false, window = false;
  unsigned samples = 1;
  configureSamplingTest(argc, argv);
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--cpu")
      cpuMode = true;
    else if (arg == "--gpu")
      cpuMode = false;
    else if (arg == "--window")
      window = true;
    else if (arg == "--portable-sampling") {
    } else if (arg == "--samples" && i + 1 < argc) {
      std::string value = argv[++i];
      if (value != "1" && value != "4")
        return 2;
      samples = unsigned(std::atoi(value.c_str()));
    } else
      return 2;
  }
  if (window && samples != 1)
    return 2;
#ifdef COIN_MULTILIGHT_MASK_X11
  if (window && !XInitThreads())
    return 1;
#endif
  SoDB::init();
  CoinRenderAction::initClass();
  Source source(samples);
  if (!source.recorder || source.original.draws.empty())
    return 1;
  return cpuMode ? (cpu(source) ? 0 : 1) : gpu(source, samples, window);
}
