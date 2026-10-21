#include "CoinRenderBumpShadowFixture.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <chrono>

using namespace bump_shadow;
namespace {
unsigned channel(unsigned long pixel, unsigned long mask) {
  if (!mask)
    return 0;
  while (!(mask & 1)) {
    pixel >>= 1;
    mask >>= 1;
  }
  return unsigned((pixel & mask) * 255 / mask);
}
bool physical(Display *display, Window window, unsigned extent, Frame &frame) {
  XSync(display, False);
  XImage *image =
      XGetImage(display, window, 0, 0, extent, extent, AllPlanes, ZPixmap);
  if (!image)
    return false;
  frame.rgba.resize(size_t(extent) * extent * 4);
  for (unsigned y = 0; y < extent; ++y)
    for (unsigned x = 0; x < extent; ++x) {
      const unsigned long pixel = XGetPixel(image, x, y);
      const size_t p = (size_t(y) * extent + x) * 4;
      frame.rgba[p] = channel(pixel, image->red_mask);
      frame.rgba[p + 1] = channel(pixel, image->green_mask);
      frame.rgba[p + 2] = channel(pixel, image->blue_mask);
      frame.rgba[p + 3] = 255;
    }
  XDestroyImage(image);
  return true;
}
bool samePhysical(const Frame &read, const Frame &shown, unsigned &bad,
                  int &maximum) {
  bad = 0;
  maximum = 0;
  if (read.rgba.size() != shown.rgba.size() || read.rgba.empty())
    return false;
  for (size_t p = 0; p < read.rgba.size(); p += 4) {
    for (unsigned c = 0; c < 3; ++c) {
      const int delta =
          std::abs(int(read.rgba[p + c]) - int(shown.rgba[p + c]));
      bad += delta > 3;
      maximum = std::max(maximum, delta);
    }
    bad += read.rgba[p + 3] != 255 || shown.rgba[p + 3] != 255;
  }
  return bad == 0;
}

// All geometry-boundary samples are analytically outside the caster. Specular
// is zero here, so their lit RGB is constant despite model rotation. Pixels
// wholly inside the rectangle require k=N; those wholly outside require k=0.
bool math(const Scene &scene, const Frame &frame, unsigned extent,
          unsigned samples, bool report) {
  if (frame.rgba.size() != size_t(extent) * extent * 4)
    return false;
  const RGB lit =
      scene.predicted(extent / 2, extent / 2, 1, true, extent, extent);
  unsigned empty = 0, full = 0, partial = 0, odd = 0, dark = 0, bad = 0;
  const double cs = std::cos(.23), sn = std::sin(.23);
  for (unsigned y = 0; y < extent; ++y)
    for (unsigned x = 0; x < extent; ++x) {
      const size_t p = (size_t(y) * extent + x) * 4;
      bad += frame.rgba[p + 3] != 255;
      const double px = (x + .5) * 4 / extent - 2;
      const double py = 2 - (y + .5) * 4 / extent;
      if (std::abs(px) < .18 && std::abs(py) < .18) {
        for (unsigned c = 0; c < 3; ++c)
          bad += frame.rgba[p + c] > 3;
        ++dark;
        continue;
      }
      if (std::max(std::abs(px), std::abs(py)) <= .65)
        continue;
      int match = -1;
      double closest = std::numeric_limits<double>::infinity();
      for (unsigned k = 0; k <= samples; ++k) {
        bool valid = true;
        double error = 0;
        for (unsigned c = 0; c < 3; ++c) {
          const double expected = lit[c] * k / samples;
          const double delta = frame.rgba[p + c] - expected;
          valid &= std::abs(int(frame.rgba[p + c]) -
                            int(std::lround(expected))) <= 3;
          error += delta * delta;
        }
        if (valid && error < closest) {
          closest = error;
          match = int(k);
        }
      }
      bad += match < 0;
      const double localX = cs * px + sn * py, localY = -sn * px + cs * py;
      const bool interior =
          localX > -1.28 && localX < 1.18 && localY > -1.18 && localY < 1.23;
      const bool exterior =
          localX < -1.52 || localX > 1.42 || localY < -1.42 || localY > 1.47;
      bad += interior && match != int(samples);
      bad += exterior && match != 0;
      empty += match == 0;
      full += match == int(samples);
      partial += match > 0 && match < int(samples);
      odd += match > 0 && match < int(samples) && (match & 1);
    }
  const bool result = bad == 0 && dark > 20 && full > 100 && empty > 100 &&
                      (samples == 1 || partial > 0) &&
                      (samples != 4 || odd > 0);
  if (report)
    std::cout << "combined_window_math extent=" << extent
              << " samples=" << samples << " bad=" << bad << " lit=" << full
              << " dark=" << dark << " empty=" << empty
              << " partial=" << partial << " odd=" << odd << '\n';
  return result;
}
bool observe(Display *display, Window window, const Scene &scene,
             unsigned extent, unsigned samples, const Frame &read, Frame &shown,
             const char *phase, const Frame *previousRead,
             const Frame *previousPhysical, const Frame *knownPhysical) {
  const auto begin = std::chrono::steady_clock::now();
  const auto deadline = begin + std::chrono::seconds(5);
  unsigned attempts = 0, bad = 0;
  int maximum = 0;
  bool current = false;
  const bool transitionRequired =
      previousRead && previousPhysical && read.rgba != previousRead->rgba;
  bool oldPhysical = false, knownMatch = !knownPhysical;
  do {
    ++attempts;
    if (physical(display, window, extent, shown)) {
      // An earlier physical image may lie within the RGB conversion tolerance
      // of this new readback. It still cannot witness the requested transition.
      oldPhysical = transitionRequired && shown.rgba == previousPhysical->rgba;
      // A/B/A already has an exact same-environment physical reference. Wait
      // for it here, within the original deadline, rather than accepting an
      // adjacent B frame and failing the exact recovery assertion afterwards.
      knownMatch = !knownPhysical || shown.rgba == knownPhysical->rgba;
      if (samePhysical(read, shown, bad, maximum) && !oldPhysical &&
          knownMatch && math(scene, shown, extent, samples, false) &&
          std::chrono::steady_clock::now() <= deadline) {
        current = true;
        break;
      }
    }
  } while (std::chrono::steady_clock::now() < deadline);
  std::cout << "combined_window_present phase=" << phase
            << " attempts=" << attempts << " elapsed_ms="
            << std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - begin)
                   .count()
            << " deadline_ms=5000 current=" << current << " bad=" << bad
            << " max=" << maximum
            << " transition_required=" << transitionRequired
            << " old_physical=" << oldPhysical
            << " known_return=" << bool(knownPhysical)
            << " known_match=" << knownMatch << '\n';
  return check(
      current,
      "whole current physical framebuffer within five seconds after one apply");
}
} // namespace

int main(int argc, char **argv) {
  configureSamplingTest(argc, argv);
  unsigned samples = 1;
  bool srgb = false, rtt = false, only2D = false;
  CoinRenderSceneTextureMode route = COIN_RENDER_SCENE_TEXTURE_STAGED;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--samples") {
      if (++i == argc)
        return 2;
      const std::string value = argv[i];
      if (value != "1" && value != "2" && value != "4")
        return 2;
      samples = unsigned(std::atoi(value.c_str()));
    } else if (arg == "--srgb")
      srgb = true;
    else if (arg == "--2d-only")
      only2D = true;
    else if (arg == "--rtt")
      rtt = true;
    else if (arg == "--direct")
      route = COIN_RENDER_SCENE_TEXTURE_DIRECT;
    else if (arg == "--staged")
      route = COIN_RENDER_SCENE_TEXTURE_STAGED;
    else if (arg != "--portable-sampling")
      return 2;
  }
  if (!XInitThreads())
    return 1;
  SoDB::init();
  CoinRenderAction::initClass();
  const auto coldBegin = std::chrono::steady_clock::now();
  auto phase = [&](const char *name) {
    std::cout << "combined_window_phase name=" << name << " elapsed_ms="
              << std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - coldBegin)
                     .count()
              << std::endl;
  };
  phase("capability-query-begin");
  CoinRenderCapabilities caps{};
  const int query = coin_render_query_capabilities(
      COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW, &caps, sizeof(caps));
  std::cout << "combined_window_environment query=" << query
            << " gpu=" << caps.gpu_available << " backend=" << caps.backend
            << " renderer=" << caps.renderer << " adapter=" << caps.adapter_name
            << " diagnostic=" << caps.diagnostic << '\n';
  phase("capability-query-end");
  if (query)
    return 1;
  if (!caps.gpu_available)
    return 77;
  Display *display = XOpenDisplay(nullptr);
  if (!display)
    return 77;
  unsigned extent = 144;
  Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 12,
                                      12, extent, extent, 0, 0, 0);
  XSetWindowAttributes attributes{};
  attributes.override_redirect = True;
  XChangeWindowAttributes(display, window, CWOverrideRedirect, &attributes);
  XMapRaised(display, window);
  XSync(display, False);
  CoinRenderNativeSurfaceDescriptor native{};
  native.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  native.structSize = sizeof(native);
  native.type = COIN_RENDER_SURFACE_XLIB;
  native.native.xlib.display = display;
  native.native.xlib.window = window;
  auto options = samplingTestOptions();
  options.sampleCount = samples;
  options.sceneTexture = route;
  options.renderer = static_cast<CoinRenderRenderer>(caps.renderer);
  if (srgb)
    options.storedTextureColorSpace = COIN_RENDER_TEXTURE_SRGB;
  phase("create-window-target-begin");
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createWindow(
      native, SbVec2i32(extent, extent), options));
  if (!check(bool(target), "real window target"))
    return 1;
  phase("create-window-target-end");
  auto &p = target->getPimpl().get();
  phase("prepare-backend-begin");
  if (!check(p.prepareBackend() == CoinRenderBackendStatus::SUCCESS &&
                 p.backend && p.backend->isGpuBackend(),
             "actual GPU window backend"))
    return 1;
  phase("prepare-backend-end");
  auto *audit = new Audit(std::move(p.backend));
  p.backend.reset(audit);
  CoinRenderAction action(SbViewportRegion(extent, extent));
  action.setRenderTarget(target.get());
  action.setTransparencyType(CoinRenderAction::NONE);
  action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
  Scene scene(255, only2D || srgb);
  scene.srgb = srgb;
  scene.setSpec({{0, 0, 0}});
  auto *rotation = new SoRotation;
  rotation->rotation.setValue(SbVec3f(0, 0, 1), .23f);
  scene.group->insertChild(rotation, scene.group->findChild(scene.positions));
  std::unique_ptr<Scene> producer;
  if (rtt) {
    producer.reset(new Scene(1, only2D || srgb));
    producer->srgb = srgb;
    // A bright, analytically white-equivalent producer keeps opaque-window
    // RGB k/N levels distinguishable even after sRGB decoding of eight
    // consumer textures. Full eight-unit colored RTT is tested offscreen.
    producer->paint(false);
    producer->setDiffuse({{1, 1, 1}});
    producer->setSpec({{0, 0, 0}});
    producer->lamp = {{1, 1, 1}};
    producer->intensity = 1;
    producer->directional->color.setValue(1, 1, 1);
    producer->directional->intensity = 1;
    auto *texture = new SoSceneTexture2;
    texture->scene = producer->root;
    texture->size.setValue(side, side);
    texture->type = SoSceneTexture2::RGBA8;
    texture->transparencyFunction = SoSceneTexture2::NONE;
    texture->model = SoSceneTexture2::MODULATE;
    const int index = scene.extras->findChild(scene.extra2D[3]);
    scene.extras->removeChild(index);
    scene.extra2D[3] = nullptr;
    scene.extras->insertChild(texture, index);
    scene.raw(3, SbVec3f(72.5f / side, 48.5f / side, 0));
  }
  auto source = [&]() {
    if (producer) {
      scene.rttTexel = producer->predicted(72, 48, 1);
      for (auto &v : scene.rttTexel)
        v /= 255;
    }
  };
  // Value snapshots keep every successful physical publication paired with
  // its immediate readback; later draw() output vectors cannot alias them.
  std::vector<Frame> readHistory, physicalHistory;
  auto draw = [&](Frame &read, Frame &shown, const char *phase) {
    source();
    const auto serial = target->getLastSubmissionSerial();
    const auto submits = audit->rootSubmits;
    const bool requested = target->requestWindowReadbackRGBA();
    std::cout << "combined_window_apply_begin phase=" << phase
              << " serial=" << serial << " root_submits=" << submits
              << std::endl;
    action.apply(scene.root);
    target->readbackRGBA(read.rgba);
    read.serial = target->getLastSubmissionSerial();
    std::cout << "combined_window phase=" << phase
              << " status=" << action.getLastStatus() << " serial=" << serial
              << ',' << read.serial
              << " error=" << action.getLastError().getString() << '\n';
    const Frame *previousRead =
        readHistory.empty() ? nullptr : &readHistory.back();
    const Frame *previousPhysical =
        physicalHistory.empty() ? nullptr : &physicalHistory.back();
    const Frame *knownPhysical = nullptr;
    for (size_t i = 0; i < readHistory.size(); ++i)
      if (readHistory[i].rgba == read.rgba) {
        knownPhysical = &physicalHistory[i];
        break;
      }
    const bool valid =
        check(requested &&
                  action.getLastStatus() == CoinRenderAction::SUCCESS &&
                  read.serial > serial && audit->rootSubmits == submits + 1 &&
                  target->getOptions().sampleCount == samples &&
                  math(scene, read, extent, samples, true),
              "same-apply current window readback, one logical submission, "
              "independent RGB and actual samples") &&
        observe(display, window, scene, extent, samples, read, shown, phase,
                previousRead, previousPhysical, knownPhysical);
    if (valid) {
      readHistory.push_back(read);
      physicalHistory.push_back(shown);
    }
    return valid;
  };
  Frame first, shown, changed, back, physicalBack;
  bool ok = draw(first, shown, "cold first combined frame");
  const Frame originalShown = shown;
  if (ok) {
    auto retained = [&]() {
      Frame now;
      std::vector<uint8_t> published;
      target->readbackRGBA(published);
      return check(target->getLastSubmissionSerial() == first.serial &&
                       published == first.rgba &&
                       physical(display, window, extent, now) &&
                       now.rgba == originalShown.rgba,
                   "peer lifecycle preserves original complete physical and "
                   "published image without reapply");
    };
    {
      Scene peerScene(255, only2D || srgb);
      peerScene.srgb = srgb;
      Harness peer(caps, samples, srgb, route);
      Frame peerA, peerB, peerBack;
      ok &= peer.render(peerScene.root, peerA, "independent combined peer") &&
            oracle(peerScene, peerA) && retained();
      peerScene.setNormal({{210, 145, 205}});
      ok &= peer.render(peerScene.root, peerB, "peer normal mutation") &&
            oracle(peerScene, peerB) && retained();
      const auto peerCoord4 = peerScene.coords[4]->point[0];
      peerScene.raw(4,
                    peerScene.only2D
                        ? SbVec3f(std::numeric_limits<float>::quiet_NaN(), 0, 0)
                        : SbVec3f(0, 0, 0));
      ok &= peer.reject(peerScene.root, peerB, "peer invalid cube direction") &&
            retained();
      peerScene.raw(4, peerCoord4);
      ok &= peer.render(peerScene.root, peerBack, "peer recovery") &&
            oracle(peerScene, peerBack) && exact(peerB, peerBack) && retained();
    }
    ok &= retained();
  }
  scene.setNormal({{210, 145, 205}});
  ok &= draw(changed, shown, "window normal mutation");
  scene.setNormal({{154, 110, 246}});
  ok &=
      draw(back, physicalBack, "window normal A/B/A") &&
      check(back.rgba == first.rgba && physicalBack.rgba == originalShown.rgba,
            "window exact readback/physical A/B/A");
  if (producer) {
    producer->setNormal({{210, 145, 205}});
    ok &= draw(changed, shown, "window RTT producer mutation");
    producer->setNormal({{154, 110, 246}});
    ok &= draw(back, physicalBack, "window RTT producer A/B/A") &&
          check(back.rgba == first.rgba &&
                    physicalBack.rgba == originalShown.rgba,
                "window RTT source A/B/A exact");
  }
  for (unsigned size : {97u, 160u, 144u}) {
    extent = size;
    XResizeWindow(display, window, extent, extent);
    XSync(display, False);
    ok &= check(target->resize(SbVec2i32(extent, extent)),
                "public physical resize");
    action.setViewportRegion(SbViewportRegion(extent, extent));
    ok &= draw(back, physicalBack, "resized current whole framebuffer");
  }
  const Frame stableRead = back, stablePhysical = physicalBack;
  const auto serial = target->getLastSubmissionSerial();
  const auto submits = audit->rootSubmits, producers = audit->producers;
  Scene &invalidSource = producer ? *producer : scene;
  const auto uv = invalidSource.uv->point[0];
  invalidSource.uv->point.set1Value(
      0, SbVec2f(std::numeric_limits<float>::quiet_NaN(), 0));
  target->requestWindowReadbackRGBA();
  action.apply(scene.root);
  Frame rejected, physicalRejected;
  target->readbackRGBA(rejected.rgba);
  ok &= check(action.getLastStatus() != CoinRenderAction::SUCCESS &&
                  action.getLastError().getLength() &&
                  target->getLastSubmissionSerial() == serial &&
                  audit->rootSubmits == submits &&
                  audit->producers == producers &&
                  rejected.rgba == stableRead.rgba &&
                  physical(display, window, extent, physicalRejected) &&
                  physicalRejected.rgba == stablePhysical.rgba,
              "window invalid source refuses before producers/root/publication "
              "preserving whole physical image");
  invalidSource.uv->point.set1Value(0, uv);
  ok &= draw(back, physicalBack, "window invalid source recovery") &&
        check(back.rgba == stableRead.rgba &&
                  physicalBack.rgba == stablePhysical.rgba,
              "window refusal recovery exact");
  action.setRenderTarget(nullptr);
  target.reset();
  XDestroyWindow(display, window);
  XCloseDisplay(display);
  return ok ? 0 : 1;
}
