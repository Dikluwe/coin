// Reuse the qualified authorial fixture and independent R26/C17.4 arithmetic.
#define main coin_render_qualified_multilight_main
#include "CoinRenderBumpMultilightTest.cpp"
#undef main
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <chrono>
namespace multiwindow {
using namespace multilamp;
unsigned channel(unsigned long pixel, unsigned long mask) {
  if (!mask)
    return 0;
  while (!(mask & 1)) {
    mask >>= 1;
    pixel >>= 1;
  }
  return unsigned((pixel & mask) * 255 / mask);
}
bool physical(Display *d, Window w, unsigned extent, Frame &f) {
  XSync(d, False);
  XWindowAttributes attributes{};
  if (!XGetWindowAttributes(d, w, &attributes) ||
      attributes.width != int(extent) || attributes.height != int(extent))
    return false;
  XImage *image = XGetImage(d, w, 0, 0, extent, extent, AllPlanes, ZPixmap);
  if (!image)
    return false;
  f.rgba.resize(size_t(extent) * extent * 4);
  for (unsigned y = 0; y < extent; ++y)
    for (unsigned x = 0; x < extent; ++x) {
      auto pixel = XGetPixel(image, x, y);
      size_t p = (size_t(y) * extent + x) * 4;
      f.rgba[p] = channel(pixel, image->red_mask);
      f.rgba[p + 1] = channel(pixel, image->green_mask);
      f.rgba[p + 2] = channel(pixel, image->blue_mask);
      f.rgba[p + 3] = 255;
    }
  XDestroyImage(image);
  return true;
}
bool matchesRead(const Frame &read, const Frame &shown) {
  if (read.rgba.empty())
    return true;
  if (read.rgba.size() != shown.rgba.size())
    return false;
  for (size_t p = 0; p < read.rgba.size(); p += 4) {
    if (read.rgba[p + 3] != 255 || shown.rgba[p + 3] != 255)
      return false;
    for (int c = 0; c < 3; ++c)
      if (std::abs(int(read.rgba[p + c]) - int(shown.rgba[p + c])) > 3)
        return false;
  }
  return true;
}
struct WindowRun {
  Display *display = nullptr;
  Window window = 0;
  unsigned extent = side;
  Scene scene;
  CoinRenderCapabilities caps;
  bool readback = false, cover = false;
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderAction action{SbViewportRegion(side, side)};
  bump_shadow::Audit *audit = nullptr;
  Frame lastRead, lastPhysical;
  WindowRun(const Settings &s, const CoinRenderCapabilities &c, bool read,
            bool coverage)
      : scene(s), caps(c), readback(read), cover(coverage) {
    display = XOpenDisplay(nullptr);
    if (!display)
      return;
    window = XCreateSimpleWindow(display, DefaultRootWindow(display), 12, 12,
                                 extent, extent, 0, 0, 0);
    XSetWindowAttributes attrs{};
    attrs.override_redirect = True;
    XChangeWindowAttributes(display, window, CWOverrideRedirect, &attrs);
    XMapRaised(display, window);
    XSync(display, False);
    CoinRenderNativeSurfaceDescriptor native{};
    native.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
    native.structSize = sizeof(native);
    native.type = COIN_RENDER_SURFACE_XLIB;
    native.native.xlib.display = display;
    native.native.xlib.window = window;
    auto options = samplingTestOptions();
    options.renderer = static_cast<CoinRenderRenderer>(caps.renderer);
    options.sampleCount = s.samples;
    target.reset(CoinRenderTarget::createWindow(
        native, SbVec2i32(extent, extent), options));
    if (!target)
      return;
    auto &p = target->getPimpl().get();
    if (p.prepareBackend() != CoinRenderBackendStatus::SUCCESS || !p.backend ||
        !p.backend->isGpuBackend()) {
      target.reset();
      return;
    }
    audit = new bump_shadow::Audit(std::move(p.backend));
    p.backend.reset(audit);
    action.setRenderTarget(target.get());
    action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
    action.setTransparencyType(CoinRenderAction::NONE);
    if (cover)
      coverageScene(scene);
  }
  ~WindowRun() {
    action.setRenderTarget(nullptr);
    target.reset();
    if (window)
      XDestroyWindow(display, window);
    if (display)
      XCloseDisplay(display);
  }
  bool image(const Frame &frame, bool report) {
    return oracle(scene, frame, extent, false, report) &&
           (!cover || coverageMath(scene, frame, scene.setting.samples, extent,
                                   true, false, report));
  }
  bool observe(Frame &shown, const Frame &read, const char *phase) {
    const auto begin = std::chrono::steady_clock::now(),
               deadline = begin + std::chrono::seconds(5);
    unsigned attempts = 0;
    bool current = false;
    do {
      ++attempts;
      if (physical(display, window, extent, shown) &&
          matchesRead(read, shown) && image(shown, false) &&
          std::chrono::steady_clock::now() <= deadline) {
        current = true;
        break;
      }
    } while (std::chrono::steady_clock::now() < deadline);
    std::cout << "multilight_present phase=" << phase << " extent=" << extent
              << " attempts=" << attempts << " ms="
              << std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - begin)
                     .count()
              << " current=" << current << '\n';
    if (!current)
      image(shown, true);
    return check(current, "one-apply current whole physical framebuffer within "
                          "original five seconds");
  }
  bool draw(const char *phase) {
    if (!target)
      return false;
    const auto old = target->getLastSubmissionSerial();
    const auto submits = audit->rootSubmits;
    const auto source = inputs(scene);
    bool requested = !readback || target->requestWindowReadbackRGBA();
    std::cout << "multilight_window_apply_begin phase=" << phase
              << " serial=" << old << '\n';
    action.apply(scene.root);
    Frame read, shown;
    target->readbackRGBA(read.rgba);
    read.serial = target->getLastSubmissionSerial();
    bool ok = check(
        requested && action.getLastStatus() == CoinRenderAction::SUCCESS &&
            read.serial > old && audit->rootSubmits == submits + 1 &&
            target->getOptions().sampleCount == scene.setting.samples,
        "window one current logical publication at requested sample count");
    ok &= check(inputs(scene) == source,
                "window apply preserves authorial fields");
    ok &= check(readback ? read.rgba.size() == size_t(extent) * extent * 4
                         : read.rgba.empty(),
                "same-apply explicit readback or genuine GPU-only delivery");
    ok &= delivery(scene, audit->plan, extent);
    if (readback)
      ok &= image(read, true);
    if (ok)
      ok &= observe(shown, read, phase);
    std::cout << "multilight_window phase=" << phase
              << " status=" << action.getLastStatus()
              << " serial=" << read.serial
              << " error=" << action.getLastError().getString() << '\n';
    if (ok) {
      lastRead = std::move(read);
      lastPhysical = std::move(shown);
    }
    return ok;
  }
  bool refuse(const char *phase, bool transparency = false) {
    const auto serial = target->getLastSubmissionSerial();
    const auto submits = audit->rootSubmits, producers = audit->producers;
    const auto beforeRead = lastRead.rgba, beforePhysical = lastPhysical.rgba;
    if (readback && !target->requestWindowReadbackRGBA())
      return false;
    action.apply(scene.root);
    Frame rejected, shown;
    target->readbackRGBA(rejected.rgba);
    bool ok = check(
        action.getLastStatus() != CoinRenderAction::SUCCESS &&
            action.getLastError().getLength() &&
            target->getLastSubmissionSerial() == serial &&
            audit->producers == producers && audit->rootSubmits >= submits &&
            audit->rootSubmits <= submits + (transparency ? 1 : 0) &&
            rejected.rgba == beforeRead &&
            physical(display, window, extent, shown) &&
            shown.rgba == beforePhysical,
        "refusal preserves exact published buffers/physical/serial and "
        "executes no producers");
    std::cout << "multilight_window_refusal phase=" << phase
              << " status=" << action.getLastStatus()
              << " cpu_entries=" << audit->rootSubmits - submits
              << " error=" << action.getLastError().getString() << '\n';
    return ok;
  }
  bool retained(const Frame &read, const Frame &shown) {
    Frame current, physicalNow;
    target->readbackRGBA(current.rgba);
    return check(target->getLastSubmissionSerial() == read.serial &&
                     current.rgba == read.rgba &&
                     physical(display, window, extent, physicalNow) &&
                     physicalNow.rgba == shown.rgba,
                 "peer lifecycle leaves original serial/readback/whole "
                 "physical image exact without reapply");
  }
  bool peer() {
    bool ok = true;
    const auto read = lastRead, shown = lastPhysical;
    {
      Settings settings = scene.setting;
      Run peer(settings, caps);
      Frame a, b, back;
      ok &= peer.draw(a, "independent offscreen peer");
      ok &= retained(read, shown);
      peer.scene.setNormal({{210, 145, 205}});
      ok &= peer.draw(b, "peer normal mutation") && retained(read, shown);
      auto uv = peer.scene.uv->point[0];
      peer.scene.uv->point.set1Value(
          0, SbVec2f(std::numeric_limits<float>::quiet_NaN(), 0));
      ok &= peer.harness.reject(peer.scene.root, b, "peer malformed UV") &&
            retained(read, shown);
      peer.scene.uv->point.set1Value(0, uv);
      ok &= peer.draw(back, "peer valid recovery") && retained(read, shown);
    }
    return retained(read, shown) && ok;
  }
  bool resize(unsigned size) {
    XResizeWindow(display, window, size, size);
    XSync(display, False);
    if (!check(target->resize(SbVec2i32(size, size)),
               "public window physical resize"))
      return false;
    extent = size;
    action.setViewportRegion(SbViewportRegion(size, size));
    return draw("resize current whole framebuffer");
  }
};
bool exercise(WindowRun &r) {
  auto &s = r.scene;
  bool ok = r.draw("cold first combined physical image");
  if (!ok)
    return false;
  ok &= r.peer();
  const std::string original = inputs(s);
  const double power = s.powers.back();
  s.power(s.setting.lights - 1, .95);
  ok &= r.draw("last light intensity current transition");
  s.power(s.setting.lights - 1, power);
  ok &= check(inputs(s) == original, "last light authorial A/B/A") &&
        r.draw("last light restore current image");
  if (!r.cover) {
    if (!s.maps.empty()) {
      double caster = s.casterX.back();
      s.casterAt(int(s.maps.size() - 1), 3);
      ok &= r.draw("last associated map clear transition");
      s.casterAt(int(s.maps.size() - 1), caster);
      ok &= check(inputs(s) == original, "last map authorial A/B/A") &&
            r.draw("last map current recovery");
    }
    auto initialNormal = s.normalBytes;
    const unsigned char normal[] = {204, 128, 230, 52, 182, 225};
    s.bump->image.setValue(SbVec2s(2, 1), 3, normal);
    s.patternNormal = true;
    s.matrix->scaleFactor.setValue(.125f, .125f);
    s.matrix->translation.setValue(.125f, .25f);
    ok &= r.draw("bump UV first bilinear footprint");
    const auto uvInputs = inputs(s);
    s.matrix->translation.setValue(.625f, .25f);
    ok &= r.draw("bump UV changed footprint");
    s.matrix->translation.setValue(.125f, .25f);
    ok &= check(inputs(s) == uvInputs, "bump matrix authorial recovery") &&
          r.draw("bump UV current restoration");
    s.setNormal(initialNormal);
    s.matrix->scaleFactor.setValue(1, 1);
    s.matrix->translation.setValue(0, 0);
    ok &=
        check(inputs(s) == original, "normal and matrix authorial recovery") &&
        r.draw("normal map restored");
    for (unsigned unit = 0; unit < (s.setting.general ? 8u : 1u); ++unit) {
      auto color = s.texture[unit], changed = color;
      for (int c = 0; c < 3; ++c)
        changed[c] = uint8_t(color[c] - 70);
      s.textureAt(int(unit), changed);
      ok &= r.draw("one independent stored 2D unit mutation");
      s.textureAt(int(unit), color);
      ok &= check(inputs(s) == original,
                  "texture binding authorial restoration") &&
            r.draw("texture unit current A/B/A");
    }
  }
  for (unsigned extent : {129u, 160u, 96u})
    ok &= r.resize(extent);
  const auto stable = inputs(s);
  auto uv = s.uv->point[3];
  s.uv->point.set1Value(3, SbVec2f(0, std::numeric_limits<float>::quiet_NaN()));
  ok &= r.refuse("last UV malformed");
  s.uv->point.set1Value(3, uv);
  ok &= check(inputs(s) == stable, "UV restore retains authorial parameters") &&
        r.draw("malformed UV current recovery");
  auto direction = s.lamps.back()->direction.getValue();
  s.lamps.back()->direction.setValue(std::numeric_limits<float>::quiet_NaN(), 0,
                                     -1);
  ok &= r.refuse("last light malformed");
  s.lamps.back()->direction = direction;
  ok &= check(inputs(s) == stable, "last light parameters restored") &&
        r.draw("last light invalid recovery");
  s.material->transparency = .5f;
  ok &= r.refuse("transparency outside opaque profile", true);
  s.material->transparency = 0;
  ok &= check(inputs(s) == stable, "opaque material authorially restored") &&
        r.draw("opaque material current recovery");
  return ok;
}
} // namespace multiwindow
int main(int argc, char **argv) {
  using namespace multiwindow;
  configureSamplingTest(argc, argv);
  Settings settings;
  bool readback = false, coverage = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--portable-sampling") {
    } else if (arg == "--readback")
      readback = true;
    else if (arg == "--coverage")
      coverage = true;
    else if (arg == "--simple")
      settings.general = false;
    else if (arg == "--general")
      settings.general = true;
    else if (arg == "--less")
      settings.depthMode = 0;
    else if (arg == "--lequal")
      settings.depthMode = 1;
    else if ((arg == "--samples" || arg == "--lights" || arg == "--maps") &&
             i + 1 < argc) {
      std::string value = argv[++i];
      if (arg == "--samples") {
        if (value != "1" && value != "2" && value != "4")
          return 2;
        settings.samples = unsigned(std::atoi(value.c_str()));
      } else {
        if (value.size() != 1 || value[0] < '0' || value[0] > '8')
          return 2;
        if (arg == "--lights")
          settings.lights = value[0] - '0';
        else
          settings.maps = value[0] - '0';
      }
    } else
      return 2;
  }
  if (!settings.lights || settings.maps > settings.lights)
    return 2;
  if (!XInitThreads())
    return 1;
  SoDB::init();
  CoinRenderAction::initClass();
  CoinRenderCapabilities caps{};
  int status = coin_render_query_capabilities(
      COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW, &caps, sizeof(caps));
  std::cout << "multilight_window_environment status=" << status
            << " gpu=" << caps.gpu_available << " backend=" << caps.backend
            << " renderer=" << caps.renderer << " adapter=" << caps.adapter_name
            << '\n';
  if (status)
    return 1;
  if (!caps.gpu_available)
    return 77;
  WindowRun run(settings, caps, readback, coverage);
  if (!run.display)
    return 77;
  if (!run.target)
    return 1;
  return exercise(run) ? 0 : 1;
}
