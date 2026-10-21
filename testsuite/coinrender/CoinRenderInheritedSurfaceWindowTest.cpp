#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#define COIN_RENDER_INHERITED_SURFACE_FIXTURE_ONLY
#include "CoinRenderInheritedSurfaceSamplingTest.cpp"
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <chrono>
#include <thread>

namespace inherited_surfaces {
uint8_t xchannel(unsigned long value, unsigned long mask) {
  if (!mask) return 0;
  while (!(mask & 1)) { value >>= 1; mask >>= 1; }
  return uint8_t(((value & mask) * 255 + mask / 2) / mask);
}
std::vector<uint8_t> physicalImage(Display *display, Window window, int extent) {
  XSync(display, False);
  XImage *image = XGetImage(display, window, 0, 0, extent, extent, AllPlanes, ZPixmap);
  if (!image) return {};
  std::vector<uint8_t> result(size_t(extent * extent * 4), 255);
  for (int y = 0; y < extent; ++y) for (int x = 0; x < extent; ++x) {
    const auto pixel = XGetPixel(image, x, y); const size_t i = size_t(y * extent + x) * 4;
    result[i] = xchannel(pixel, image->red_mask);
    result[i + 1] = xchannel(pixel, image->green_mask);
    result[i + 2] = xchannel(pixel, image->blue_mask);
  }
  XDestroyImage(image); return result;
}
bool windowPixels(const std::vector<uint8_t> &expected,
                  const std::vector<uint8_t> &actual, int extent, bool report) {
  if (expected.size() != size_t(extent * extent * 4) || actual.size() != expected.size()) return false;
  unsigned bad = 0, checked = 0; int maximum = 0;
  // All bitmap interior texels, including every hole, and the complete clear
  // background except its two-pixel silhouette margin. No smoothness filter
  // can silently remove patterned/hole witnesses.
  for (int y = 0; y < extent; ++y) for (int x = 0; x < extent; ++x) {
    const int dx = x - extent / 2, dy = y - extent / 2;
    const bool bitmap = dx >= -6 && dx <= 5 && dy >= -6 && dy <= 5;
    const bool background = std::abs(dx) > 11 || std::abs(dy) > 11;
    if (!bitmap && !background) continue;
    const size_t p = size_t(y * extent + x) * 4;
    for (int c = 0; c < 3; ++c) {
      const int delta = std::abs(int(expected[p + c]) - int(actual[p + c]));
      maximum = std::max(maximum, delta); bad += delta > 3;
    }
    bad += actual[p + 3] != 255; ++checked;
  }
  if (report) std::cout << "inherited_window checked=" << checked << " bad=" << bad
                         << " maximum=" << maximum << '\n';
  return checked > 100 && bad == 0;
}
struct SurfaceWindowHarness {
  SurfaceHarness off;
  std::unique_ptr<CoinRenderTarget> &target;
  CoinRenderAction &action;
  SurfaceAudit *audit = nullptr;
  Display *display = nullptr; Window window = 0; int extent = side;
  std::unique_ptr<CoinRenderTarget> shown;
  CoinRenderAction shownAction{SbViewportRegion(side, side)};
  SurfaceAudit *shownAudit = nullptr;
  std::vector<uint8_t> lastPhysical, lastReadback;
  bool ready = false;
  explicit SurfaceWindowHarness(const CoinRenderCapabilities &caps)
      : off(caps, 1), target(off.target), action(off.action) {
    if (!target) return;
    audit = off.audit;
    display = XOpenDisplay(nullptr);
    if (!check(display != nullptr, "physical X11 display")) return;
    window = XCreateSimpleWindow(display, DefaultRootWindow(display), 16, 16, side, side, 0, 0, 0);
    XSetWindowAttributes attributes{}; attributes.override_redirect = True;
    XChangeWindowAttributes(display, window, CWOverrideRedirect, &attributes);
    XMapRaised(display, window); XSync(display, False);
    CoinRenderNativeSurfaceDescriptor native{};
    native.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION; native.structSize = sizeof(native);
    native.type = COIN_RENDER_SURFACE_XLIB; native.native.xlib.display = display;
    native.native.xlib.window = window;
    auto options = samplingTestOptions(); options.sampleCount = 1;
    options.renderer = static_cast<CoinRenderRenderer>(caps.renderer);
    shown.reset(CoinRenderTarget::createWindow(native, SbVec2i32(side, side), options));
    if (!check(bool(shown), "physical 1x window factory")) return;
    auto &state = shown->getPimpl().get();
    if (!check(state.prepareBackend() == CoinRenderBackendStatus::SUCCESS && state.backend &&
               state.backend->isGpuBackend() && shown->getLastSubmissionSerial() == 0,
               "window prepared without publishing a frame")) return;
    shownAudit = new SurfaceAudit(std::move(state.backend)); state.backend.reset(shownAudit);
    shownAction.setRenderTarget(shown.get());
    shownAction.setTransparencyType(CoinRenderAction::BLEND);
    shownAction.setBackgroundColor(SbColor4f(0, 0, 0, 1));
    ready = check(shown->getOptions().sampleCount == 1 &&
                  shown->getOptions().textureSamplingPolicy == samplingTestPolicy,
                  "window retains physical 1x and authored sampling policy");
  }
  ~SurfaceWindowHarness() {
    shownAction.setRenderTarget(nullptr); action.setRenderTarget(nullptr);
    shown.reset(); target.reset();
    if (window) XDestroyWindow(display, window);
    if (display) XCloseDisplay(display);
  }
  bool resize(int n) {
    XResizeWindow(display, window, n, n); XSync(display, False);
    XWindowAttributes attributes{}; XGetWindowAttributes(display, window, &attributes);
    if (!check(attributes.width == n && attributes.height == n &&
               target->resize(SbVec2i32(n, n)) && shown->resize(SbVec2i32(n, n)),
               "real X11 and both target sizes resize together")) return false;
    extent = n; action.setViewportRegion(SbViewportRegion(n, n));
    shownAction.setViewportRegion(SbViewportRegion(n, n)); return true;
  }
  bool render(SoNode *root, Frame &frame, const std::string &phase) {
    if (!check(ready, "prepared window pair")) return false;
    const auto old = target->getLastSubmissionSerial(), oldShown = shown->getLastSubmissionSerial();
    const auto before = audit->submits, beforeShown = shownAudit->submits;
    action.apply(root); target->readbackRGBA(frame.rgba); target->readbackDepth(frame.depth);
    frame.serial = target->getLastSubmissionSerial();
    if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS && frame.serial > old &&
               audit->submits == before + 1 && frame.rgba.size() == size_t(extent * extent * 4) &&
               frame.depth.size() == size_t(extent * extent), "single-apply offscreen geometric control")) return false;
    if (!check(shown->requestWindowReadbackRGBA(), "explicit current window RGBA request")) return false;
    shownAction.apply(root); shown->readbackRGBA(lastReadback);
    std::cout << "inherited_window phase=" << phase << " extent=" << extent
              << " status=" << shownAction.getLastStatus()
              << " serial=" << shown->getLastSubmissionSerial()
              << " error=" << shownAction.getLastError().getString() << '\n';
    if (!check(shownAction.getLastStatus() == CoinRenderAction::SUCCESS &&
               shown->getLastSubmissionSerial() > oldShown && shownAudit->submits == beforeShown + 1 &&
               windowPixels(frame.rgba, lastReadback, extent, true),
               "one window apply publishes the current authored RGBA")) return false;
    // Wait only for the already submitted presentation. No apply, frame,
    // renderer poll or resubmission occurs in this observation loop.
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
      lastPhysical = physicalImage(display, window, extent);
      if (windowPixels(frame.rgba, lastPhysical, extent, false)) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < until);
    windowPixels(frame.rgba, lastPhysical, extent, true);
    return check(false, "first single-apply presentation has the authored physical pixels");
  }
  bool refuse(SoNode *root, const Frame &published) {
    const auto serial = target->getLastSubmissionSerial(), shownSerial = shown->getLastSubmissionSerial();
    const auto calls = audit->submits, shownCalls = shownAudit->submits;
    const auto physical = lastPhysical, readback = lastReadback;
    action.apply(root); shownAction.apply(root);
    Frame refused; target->readbackRGBA(refused.rgba); target->readbackDepth(refused.depth);
    std::vector<uint8_t> shownBytes; shown->readbackRGBA(shownBytes);
    return check(action.getLastStatus() != CoinRenderAction::SUCCESS &&
                 shownAction.getLastStatus() != CoinRenderAction::SUCCESS &&
                 audit->submits == calls && shownAudit->submits == shownCalls &&
                 target->getLastSubmissionSerial() == serial && shown->getLastSubmissionSerial() == shownSerial &&
                 refused.rgba == published.rgba && refused.depth == published.depth &&
                 shownBytes == readback && physicalImage(display, window, extent) == physical,
                 "invalid input preserves both publications and physical window before commands");
  }
};
bool resizedImageOracle(const RasterScene &s, const Frame &f, const Frame *white,
                 bool blocked, int extent) {
  if (f.rgba.size() != size_t(extent * extent * 4) ||
      f.depth.size() != size_t(extent * extent))
    return false;
  unsigned checked = 0, holes = 0, bad = 0;
  double maximum = 0;
  const auto product = s.product();
  // Avoid the one-pixel alignment border; these are authored bitmap interiors.
  for (int y = extent / 2 - 6; y <= extent / 2 + 5; ++y)
    for (int x = extent / 2 - 6; x <= extent / 2 + 5; ++x) {
      const int ix = x - extent / 2 + 8, iy = extent / 2 + 7 - y;
      const size_t source = (iy * 17 + ix) * 4, p = size_t(y * extent + x);
      const bool hole = s.imageBytes[source + 3] == 0;
      holes += hole;
      for (int c = 0; c < 3; ++c) {
        // Ordinary DrawPixels uses image PRIMARY. Only the shadow receiver
        // has the authored diffuse illumination factor .6.
        const double primaryFactor =
            s.rasterStyle->style.getValue() == SoShadowStyle::SHADOWED ? .6
                                                                       : 1.;
        double expected =
            blocked || hole ? 0 : s.imageBytes[source + c] * primaryFactor;
        if (white)
          expected = white->rgba[p * 4 + c];
        expected *= product[c];
        const double delta = std::abs(double(f.rgba[p * 4 + c]) - expected);
        maximum = std::max(maximum, delta);
        bad += delta > 3;
      }
      bad += f.rgba[p * 4 + 3] != 255;
      bad += std::abs(f.depth[p] - float(6. / 19.)) > 2e-5f;
      ++checked;
    }
  std::cout << "inherited_image checked=" << checked << " holes=" << holes
            << " bad=" << bad << " max=" << maximum << " blocked=" << blocked
            << '\n';
  return check(
      checked >= 100 && (!s.setting.holes || holes > 20) && bad == 0,
      "independent raster texel product, alpha holes and geometric depth");
}
bool runWindow(const Settings &s, const CoinRenderCapabilities &caps) {
  SurfaceWindowHarness h(caps);
  if (!h.ready)
    return false;
  auto *audit = h.audit;
  h.action.setTransparencyType(CoinRenderAction::BLEND);
  h.action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
  RasterScene scene(s);
  bool ok = true;
  Frame clear, shadow, colored, back;
  std::vector<uint8_t> physicalClear, physicalPattern;
  auto draw = [&](Frame &f, const char *label) {
    auto count = audit->submits;
    const auto sourceId = scene.root->getNodeId(),
               imageId = scene.image->getNodeId();
    bool pass = h.render(scene.root, f, label);
    pass &= check(sourceId == scene.root->getNodeId() &&
                      imageId == scene.image->getNodeId(),
                  "apply does not mutate authored source nodes");
    return check(audit->submits == count + 1, "one logical GPU submission") &&
           pass && surfaceDelivery(scene, audit->plan) && associations(scene, audit->plan) &&
           surfaceDelivery(scene, h.shownAudit->plan) && associations(scene, h.shownAudit->plan);
  };
  for (int slot = 0; slot < s.maps; ++slot) {
    scene.isolate(slot);
    scene.paint(true);
    scene.shadow(slot, false);
    if (!draw(clear, "white lit slot")) {
      ok = false;
      continue;
    }
    physicalClear = h.lastPhysical;
    ok &= imageOracle(scene, clear, nullptr, false);
    scene.shadow(slot, true);
    if (draw(shadow, "white shadow slot")) {
      ok &= imageOracle(scene, shadow, nullptr, true);
      unsigned changed = 0;
      for (int y = 58; y <= 69; ++y)
        for (int x = 58; x <= 69; ++x) {
          size_t p = size_t(y * side + x) * 4;
          changed += std::abs(int(clear.rgba[p]) - int(shadow.rgba[p])) > 20;
        }
      ok &= check(changed > 50,
                  "each isolated real map observably shadows the image");
    } else
      ok = false;
    scene.paint(false);
    if (draw(colored, "colored shadow slot"))
      ok &= imageOracle(scene, colored, &shadow, true);
    else
      ok = false;
    scene.shadow(slot, false);
    if (draw(colored, "colored lit slot"))
      ok &= imageOracle(scene, colored, &clear, false);
    else
      ok = false;
    scene.paint(true);
    if (draw(back, "white restored slot"))
      ok &= imageOracle(scene, back, nullptr, false) && exact(clear, back) &&
            check(physicalClear == h.lastPhysical, "exact physical white A/B/A");
    else
      ok = false;
  }
  scene.isolate(-1);
  scene.paint(true);
  if (!draw(clear, "joint white"))
    return false;
  scene.paint(false);
  if (!draw(colored, "joint patterned"))
    return false;
  physicalPattern = h.lastPhysical;
  ok &= imageOracle(scene, colored, &clear, false);
  for (int unit : scene.active) {
    scene.paint(false, unit);
    if (draw(back, "unit bytes changed")) {
      ok &= imageOracle(scene, back, &clear, false);
      unsigned changed = 0;
      for (int y = 58; y <= 69; ++y)
        for (int x = 58; x <= 69; ++x) {
          size_t p = size_t(y * side + x) * 4;
          for (int c = 0; c < 3; ++c)
            changed +=
                std::abs(int(back.rgba[p + c]) - int(colored.rgba[p + c])) > 3;
        }
      ok &= check(changed > 16, "each inherited texture has a visible effect");
    } else
      ok = false;
    scene.paint(false);
    if (draw(back, "unit bytes restore"))
      ok &= imageOracle(scene, back, &clear, false) && exact(colored, back) &&
          check(physicalPattern == h.lastPhysical, "exact physical patterned A/B/A");
    else
      ok = false;
    const auto q = scene.coordinates[unit]->factorQ.getValue(),
               st = scene.coordinates[unit]->factorS.getValue();
    for (int invalid = 0; invalid < 2; ++invalid) {
      // Q divides only on PROJECTIVE. Keep all maps enabled, but author the
      // raster as UNSHADOWED for this control. NaN also covers DIRECT_ST.
      if (invalid == 0) {
        scene.rasterStyle->style = SoShadowStyle::NO_SHADOWING;
        if (!draw(back, "valid PROJECTIVE predecessor"))
          return false;
        ok &= imageOracle(scene, back, nullptr, false);
      }
      if (invalid == 0)
        scene.coordinates[unit]->factorQ.setValue(0, 0, 0, 0);
      else
        scene.coordinates[unit]->factorS.setValue(
            0, 0, 0, std::numeric_limits<float>::quiet_NaN());
      ok &= h.refuse(scene.root, back);
      scene.coordinates[unit]->factorQ = q;
      scene.coordinates[unit]->factorS = st;
      scene.rasterStyle->style = SoShadowStyle::SHADOWED;
      if (draw(back, "invalid coordinate recovery"))
        ok &= imageOracle(scene, back, &clear, false) && exact(colored, back) &&
          check(physicalPattern == h.lastPhysical, "exact physical patterned A/B/A");
      else
        ok = false;
    }
  }
  auto *unused = new SoTextureCoordinateObject;
  unused->factorS.setValue(0, 0, 0, std::numeric_limits<float>::quiet_NaN());
  scene.receiver->addChild(unused); // after the only image: no consumer
  if (draw(back, "unconsumed invalid coordinate state"))
    ok &= imageOracle(scene, back, &clear, false) && exact(colored, back) &&
          check(physicalPattern == h.lastPhysical, "exact physical patterned A/B/A");
  else ok = false;
  // Pixel-sized raster remains 17x17 across real resize; world shadow maps
  // retain the same physical anchor. Check first new presentation at each size.
  const auto beforeResize = back;
  const auto physicalBeforeResize = h.lastPhysical;
  for (int extent : {160, 96, 128}) {
    if (!h.resize(extent) || !draw(back, "authored resize first apply")) { ok = false; continue; }
    ok &= resizedImageOracle(scene, back, nullptr, false, extent);
  }
  ok &= exact(beforeResize, back) && check(physicalBeforeResize == h.lastPhysical,
                                         "resize restores exact physical and readback A");
  // Window depth is not exposed by the RGBA readback API. An independently
  // authored front/back occluder therefore witnesses its actual depth test
  // through physical RGB, while offscreen readback checks numeric depth.
  auto *occluder = new SoSeparator;
  auto *lighting = new SoLightModel; lighting->model = SoLightModel::BASE_COLOR;
  occluder->addChild(lighting);
  auto *material = new SoMaterial; material->diffuseColor.setValue(.125f, .75f, .25f);
  occluder->addChild(material);
  auto *move = new SoTranslation; move->translation.setValue(0, 0, -1.5f);
  occluder->addChild(move);
  auto *points = new SoCoordinate3;
  const SbVec3f vertices[] = {SbVec3f(-.1875f, -.1875f, 0), SbVec3f(0, -.1875f, 0),
                             SbVec3f(0, .1875f, 0), SbVec3f(-.1875f, .1875f, 0)};
  points->point.setValues(0, 4, vertices); occluder->addChild(points);
  auto *face = new SoFaceSet; face->numVertices = 4; occluder->addChild(face);
  scene.root->addChild(occluder);
  Frame behind, front, behindAgain;
  if (!draw(behind, "depth occluder behind image first apply")) return false;
  ok &= exact(back, behind);
  const auto physicalBehind = h.lastPhysical;
  move->translation.setValue(0, 0, -.5f);
  if (!draw(front, "depth occluder in front first apply")) return false;
  unsigned changed = 0, bad = 0;
  const double rgb[] = {255. * .125, 255. * .75, 255. * .25};
  for (int y = 60; y <= 67; ++y) for (int x = 60; x <= 61; ++x) {
    const size_t at = size_t(y * side + x);
    for (int c = 0; c < 3; ++c) {
      bad += std::abs(double(front.rgba[4 * at + c]) - rgb[c]) > 3;
      bad += std::abs(double(h.lastReadback[4 * at + c]) - rgb[c]) > 3;
      bad += std::abs(double(h.lastPhysical[4 * at + c]) - rgb[c]) > 3;
    }
    bad += front.rgba[4 * at + 3] != 255;
    bad += std::abs(front.depth[at] - 5.5f / 19.f) > 2e-5f;
    changed += std::abs(int(front.rgba[4 * at + 1]) - int(behind.rgba[4 * at + 1])) > 20;
  }
  ok &= check(bad == 0 && changed == 16,
              "front opaque depth witness has authored RGB/depth in physical window and readback");
  move->translation.setValue(0, 0, -1.5f);
  ok &= draw(behindAgain, "depth occluder restored behind") && exact(behind, behindAgain) &&
        check(h.lastPhysical == physicalBehind, "physical depth-order A/B/A restores exactly");
  return ok;
}

} // namespace inherited_surfaces
int main(int argc, char **argv) {
  unsigned selected = 0;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) != "--configuration" || ++i == argc) return 2;
    const std::string value = argv[i];
    if (value.size() != 1 || value[0] < '0' || value[0] > '5') return 2;
    selected = unsigned(value[0] - '0');
  }
  std::cout << std::unitbuf;
  if (!XInitThreads()) return 1;
  SoDB::init(); CoinRenderAction::initClass();
  inherited_surfaces::selectPolicy(selected);
  const auto caps = msaa_shadow_test::capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW);
  if (!caps.gpu_available || caps.renderer == COIN_RENDER_RENDERER_UNKNOWN) return 77;
  return inherited_surfaces::runWindow(inherited_surfaces::configuration(selected), caps) ? 0 : 1;
}
