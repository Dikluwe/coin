// Reuse the independent camera fixtures; the original executable and CLI stay
// intact. This translation unit supplies X11 delivery, not a second oracle.
#define main coinRenderCameraCasesMain
#include "CoinRenderShadowMapCameraTest.cpp"
#undef main
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <chrono>

namespace {
struct CameraWindow {
  Display *display = nullptr;
  Window window = 0;
  int extent = side;
  unsigned samples;
  bool explicitReadback, ready = false;
  std::unique_ptr<CoinRenderTarget> off, shown;
  CoinRenderAction offAction{SbViewportRegion(side, side)},
      windowAction{SbViewportRegion(side, side)};
  NearAudit *offAudit = nullptr, *windowAudit = nullptr;
  Frame lastOff;
  std::vector<uint8_t> lastPhysical, lastReadback;
  explicit CameraWindow(const CoinRenderCapabilities &caps, unsigned n,
                        bool readback, bool portable)
      : samples(n), explicitReadback(readback) {
    display = XOpenDisplay(nullptr);
    if (!display)
      return;
    window = XCreateSimpleWindow(display, DefaultRootWindow(display), 16, 16,
                                 side, side, 0, 0, 0);
    XSetWindowAttributes attributes{};
    attributes.override_redirect = True;
    XChangeWindowAttributes(display, window, CWOverrideRedirect, &attributes);
    XMapRaised(display, window);
    XSync(display, False);
    CoinRenderNativeSurfaceDescriptor surface{};
    surface.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
    surface.structSize = sizeof(surface);
    surface.type = COIN_RENDER_SURFACE_XLIB;
    surface.native.xlib.display = display;
    surface.native.xlib.window = window;
    std::string diagnostic;
    auto options = CoinRenderDiagnosticShell::renderOptions(diagnostic);
    options.renderer = static_cast<CoinRenderRenderer>(caps.renderer);
    options.sampleCount = n;
    if (portable)
      options.textureSamplingPolicy = COIN_RENDER_SAMPLING_PORTABLE;
    off.reset(
        CoinRenderTarget::createOffscreen(SbVec2i32(side, side), options));
    shown.reset(CoinRenderTarget::createWindow(surface, SbVec2i32(side, side),
                                               options));
    const auto install = [](CoinRenderTarget *target, NearAudit *&audit) {
      if (!target)
        return false;
      auto &p = target->getPimpl().get();
      if (p.prepareBackend() != CoinRenderBackendStatus::SUCCESS ||
          !p.backend || !p.backend->isGpuBackend())
        return false;
      audit = new NearAudit(std::move(p.backend));
      p.backend.reset(audit);
      return true;
    };
    ready = install(off.get(), offAudit) && install(shown.get(), windowAudit);
    if (!ready)
      return;
    ready = check(
        off->getOptions().sampleCount == n &&
            shown->getOptions().sampleCount == n &&
            shown->getOptions().renderer == caps.renderer &&
            shown->getOptions().textureSamplingPolicy ==
                options.textureSamplingPolicy,
        "window/offscreen retain requested GPU, samples and sampling policy");
    off->setDepthReadbackEnabled(TRUE);
    offAction.setRenderTarget(off.get());
    windowAction.setRenderTarget(shown.get());
    offAction.setTransparencyType(CoinRenderAction::NONE);
    windowAction.setTransparencyType(CoinRenderAction::NONE);
    offAction.setBackgroundColor(SbColor4f(0, 0, 0, 0));
    windowAction.setBackgroundColor(SbColor4f(0, 0, 0, 0));
  }
  ~CameraWindow() {
    windowAction.setRenderTarget(nullptr);
    offAction.setRenderTarget(nullptr);
    shown.reset();
    off.reset();
    if (window)
      XDestroyWindow(display, window);
    if (display)
      XCloseDisplay(display);
  }
  static uint8_t channel(unsigned long pixel, unsigned long mask) {
    if (!mask)
      return 0;
    while (!(mask & 1)) {
      pixel >>= 1;
      mask >>= 1;
    }
    return uint8_t(((pixel & mask) * 255 + mask / 2) / mask);
  }
  std::vector<uint8_t> physical() const {
    XSync(display, False);
    auto *image =
        XGetImage(display, window, 0, 0, extent, extent, AllPlanes, ZPixmap);
    if (!image)
      return {};
    std::vector<uint8_t> result(size_t(extent) * extent * 4, 255);
    for (int y = 0; y < extent; ++y)
      for (int x = 0; x < extent; ++x) {
        const auto pixel = XGetPixel(image, x, y);
        const size_t p = size_t(y * extent + x) * 4;
        result[p] = channel(pixel, image->red_mask);
        result[p + 1] = channel(pixel, image->green_mask);
        result[p + 2] = channel(pixel, image->blue_mask);
      }
    XDestroyImage(image);
    return result;
  }
  static bool rgbClose(const std::vector<uint8_t> &a,
                       const std::vector<uint8_t> &b) {
    if (a.empty() || a.size() != b.size())
      return false;
    for (size_t p = 0; p < a.size(); p += 4) {
      if (b[p + 3] != 255)
        return false;
      for (int c = 0; c < 3; ++c)
        if (std::abs(int(a[p + c]) - int(b[p + c])) > 3)
          return false;
    }
    return true;
  }
  bool observe(NearSlotScene &scene, const Frame &control,
               const std::vector<uint8_t> &readback,
               const std::vector<uint8_t> *knownPhysical,
               const std::string &label) {
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::seconds(5);
    unsigned attempts = 0;
    bool accepted = false;
    Frame current;
    do {
      ++attempts;
      current.rgba = physical();
      const bool sameOld =
          !lastPhysical.empty() && current.rgba == lastPhysical;
      bool changedInput = false;
      if (!nearNativeMsaa(*shown)) {
        // Preserve the legacy transition rules. Native MSAA instead proves
        // the current image below; old silhouette bytes have no oracle bound.
        if (!readback.empty() && readback.size() == lastReadback.size()) {
          for (size_t p = 0; p < readback.size(); p += 4)
            for (int c = 0; c < 3; ++c)
              changedInput |= readback[p + c] != lastReadback[p + c];
        } else if (control.rgba.size() == lastOff.rgba.size()) {
          for (size_t p = 0; p < control.rgba.size(); p += 4)
            for (int c = 0; c < 3; ++c)
              changedInput |= std::abs(int(control.rgba[p + c]) -
                                       int(lastOff.rgba[p + c])) > 6;
        }
      }
      const bool currentFrame =
          (!changedInput || !sameOld) &&
          // A restored Native MSAA execution need not reproduce an old edge
          // byte. Current delivery, independent image/coverage and same-apply
          // readback remain mandatory; refusal still preserves the old image
          // bitwise.
          (!knownPhysical || nearNativeMsaa(*shown) ||
           current.rgba == *knownPhysical) &&
          (readback.empty() || rgbClose(readback, current.rgba));
      if (currentFrame &&
          nearImageOracle(scene, current, label + " physical", false, extent,
                          false) &&
          (!scene.coverage ||
           nearCoverageLevels(current, scene, samples, extent, true, false))) {
        accepted = true;
        break;
      }
    } while (std::chrono::steady_clock::now() < deadline);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    std::cout << "camera_window_observe phase=" << label << " extent=" << extent
              << " attempts=" << attempts << " ms=" << elapsed
              << " accepted=" << accepted << '\n';
    if (!accepted) {
      // Diagnose only existing snapshots. No additional readback, X request,
      // fence wait, render, or target retention can affect this observation.
      const bool previousComparable = !lastPhysical.empty() && current.rgba.size() == lastPhysical.size();
      std::cout << "camera_window_observe failure_relations phase=" << label
                << " previous_comparable=" << previousComparable
                << " same_previous=" << (previousComparable && current.rgba == lastPhysical)
                << " current_readback_available=" << !readback.empty()
                << " current_readback_matches_physical=" << (!readback.empty() && rgbClose(readback, current.rgba))
                << " current_control_matches_physical=" << rgbClose(control.rgba, current.rgba)
                << " previous_readback_comparable=" << (!lastReadback.empty() && lastReadback.size() == readback.size())
                << " same_readback=" << (!lastReadback.empty() && lastReadback == readback)
                << " previous_control_comparable=" << (!lastOff.rgba.empty() && lastOff.rgba.size() == control.rgba.size())
                << " same_control=" << (!lastOff.rgba.empty() && lastOff.rgba == control.rgba)
                << " off_serial=" << control.serial
                << " window_serial=" << shown->getLastSubmissionSerial() << '\n';
      nearImageOracle(scene, current, label + " final physical", false, extent);
      if (scene.coverage)
        nearCoverageLevels(current, scene, samples, extent, true, true);
    }
    lastPhysical = current.rgba;
    return check(
        accepted,
        "current physical image observed within original five seconds");
  }
  bool draw(NearSlotScene &scene, Frame &control, const std::string &label,
            const std::vector<uint8_t> *knownPhysical = nullptr) {
    const uint64_t oldOff = off->getLastSubmissionSerial(),
                   oldWindow = shown->getLastSubmissionSerial();
    const unsigned offCount = offAudit->submissions,
                   windowCount = windowAudit->submissions;
    control.expectedInputs = nearAuthorialInputs(scene, extent);
    offAction.apply(scene.root);
    off->readbackRGBA(control.rgba);
    off->readbackDepth(control.depth);
    control.serial = off->getLastSubmissionSerial();
    bool ok = check(offAction.getLastStatus() == CoinRenderAction::SUCCESS &&
                        control.serial > oldOff &&
                        offAudit->submissions == offCount + 1,
                    "one offscreen control apply/publication: " + label);
    ok &= nearImageOracle(scene, control, label + " offscreen", true, extent);
    ok &= nearDelivery(scene, *offAudit, extent);
    if (scene.coverage)
      ok &= check(nearCoverageLevels(control, scene, samples, extent, false),
                  "offscreen real MSAA alpha coverage");
    const bool requested =
        !explicitReadback || shown->requestWindowReadbackRGBA();
    windowAction.apply(scene.root);
    std::vector<uint8_t> readback;
    shown->readbackRGBA(readback);
    std::cout << "camera_window_apply phase=" << label << " samples=" << samples
              << " readback=" << explicitReadback
              << " status=" << windowAction.getLastStatus()
              << " serial=" << oldWindow << ','
              << shown->getLastSubmissionSerial() << " submits=" << windowCount
              << ',' << windowAudit->submissions
              << " error=" << windowAction.getLastError().getString() << '\n';
    ok &= check(requested &&
                    windowAction.getLastStatus() == CoinRenderAction::SUCCESS &&
                    shown->getLastSubmissionSerial() > oldWindow &&
                    windowAudit->submissions == windowCount + 1,
                "one current physical-window apply/publication");
    ok &= nearDelivery(scene, *windowAudit, extent);
    if (explicitReadback) {
      Frame read;
      read.rgba = readback;
      // Window readback may retain resolved coverage alpha; test its RGB.
      ok &= nearImageOracle(scene, read, label + " same-apply window readback",
                            false, extent);
      if (scene.coverage)
        ok &= check(nearCoverageLevels(read, scene, samples, extent, true),
                    "window readback RGB MSAA levels");
    } else
      ok &= check(readback.empty(),
                  "GPU-only window has no unsolicited RGBA publication");
    if (ok)
      ok &= observe(scene, control, readback, knownPhysical, label);
    lastOff = control;
    lastReadback = readback;
    return ok;
  }
  bool restored(NearSlotScene &scene, const Frame &a, const Frame &b,
                const std::string &label) {
    if (!nearNativeMsaa(*shown))
      return cropSame(a, b, label);
    return check(b.serial > a.serial && !a.expectedInputs.empty() &&
                     a.expectedInputs == b.expectedInputs &&
                     b.expectedInputs == nearAuthorialInputs(scene, extent),
                 label +
                     " expected authorial parameters and new publication") &&
           nearDelivery(scene, *offAudit, extent) &&
           nearDelivery(scene, *windowAudit, extent) &&
           nearImageOracle(scene, b, label, true, extent);
  }
  bool refuse(NearSlotScene &scene, const std::string &label) {
    return refuseRoot(scene.root, label);
  }
  bool refuseRoot(SoNode *root, const std::string &label) {
    const auto oldPhysical = lastPhysical, oldReadback = lastReadback;
    std::vector<float> oldWindowDepth;
    shown->readbackDepth(oldWindowDepth);
    const auto offRgba = lastOff.rgba;
    const auto offDepth = lastOff.depth;
    const auto offSerial = off->getLastSubmissionSerial(),
               windowSerial = shown->getLastSubmissionSerial();
    const auto offCount = offAudit->submissions,
               windowCount = windowAudit->submissions;
    offAction.apply(root);
    Frame rejected;
    off->readbackRGBA(rejected.rgba);
    off->readbackDepth(rejected.depth);
    bool ok =
        check(offAction.getLastStatus() != CoinRenderAction::SUCCESS &&
                  offAction.getLastError().getLength() &&
                  offAudit->submissions == offCount &&
                  off->getLastSubmissionSerial() == offSerial &&
                  rejected.rgba == offRgba && rejected.depth == offDepth,
              "offscreen invalid slot atomic RGBA/depth/serial: " + label);
    const bool requested =
        !explicitReadback || shown->requestWindowReadbackRGBA();
    windowAction.apply(root);
    std::vector<uint8_t> rejectedReadback;
    shown->readbackRGBA(rejectedReadback);
    ok &= check(requested &&
                    windowAction.getLastStatus() != CoinRenderAction::SUCCESS &&
                    windowAction.getLastError().getLength() &&
                    windowAudit->submissions == windowCount &&
                    shown->getLastSubmissionSerial() == windowSerial &&
                    rejectedReadback == oldReadback,
                "window invalid slot rejects before submission and preserves "
                "publication/readback request semantics");
    std::vector<float> rejectedWindowDepth;
    shown->readbackDepth(rejectedWindowDepth);
    ok &= check(oldWindowDepth == rejectedWindowDepth,
                "window depth publication remains unchanged on refusal");
    ok &= check(physical() == oldPhysical,
                "refusal preserves complete last physical image");
    return ok;
  }
  bool resize(int n) {
    XResizeWindow(display, window, n, n);
    XSync(display, False);
    XWindowAttributes attributes{};
    XGetWindowAttributes(display, window, &attributes);
    if (!check(attributes.width == n && attributes.height == n &&
                   off->resize(SbVec2i32(n, n)) &&
                   shown->resize(SbVec2i32(n, n)),
               "physical/public target resize agrees"))
      return false;
    extent = n;
    offAction.setViewportRegion(SbViewportRegion(n, n));
    windowAction.setViewportRegion(SbViewportRegion(n, n));
    lastPhysical.clear();
    lastReadback.clear();
    lastOff = Frame{};
    return true;
  }
};

#include "CoinRenderShadowMapStereoWindowCases.h"

bool windowProfiles(CameraWindow &window, int maps, bool general, int mode) {
  bool ok = true;
  for (int raster = mode == 1 ? 1 : 0; raster <= (mode == 1   ? 2
                                                  : mode == 4 ? 1
                                                              : 0);
       ++raster) {
    NearSlotScene scene(maps, raster, general);
    scene.mixedNears();
    scene.slopedReceiver();
    if (mode == 2)
      scene.cropViewports();
    if (mode == 4)
      scene.crossScissorBoundary();
    Frame a, b, back;
    std::vector<uint8_t> physicalA;
    for (int slot = 0; slot < maps; ++slot) {
      scene.isolate(slot);
      const auto casterPosition = scene.moves[slot]->translation.getValue();
      scene.cameras[slot]->nearDistance = 1;
      if (!window.draw(scene, a, "first/front slot=" + std::to_string(slot))) {
        ok = false;
        continue;
      }
      physicalA = window.lastPhysical;
      scene.cameras[slot]->nearDistance = 0;
      ok &=
          window.draw(scene, b, "front zero near slot=" + std::to_string(slot));
      scene.moves[slot]->translation.setValue(casterPosition[0],
                                              casterPosition[1], 6);
      scene.cameras[slot]->nearDistance = -2;
      ok &= window.draw(scene, b,
                        "negative near accepts behind-camera caster slot=" +
                            std::to_string(slot));
      scene.cameras[slot]->nearDistance = 0;
      ok &= window.draw(scene, b,
                        "zero near clips behind-camera caster slot=" +
                            std::to_string(slot));
      scene.moves[slot]->translation.setValue(casterPosition[0],
                                              casterPosition[1], .35f);
      scene.cameras[slot]->nearDistance = 1;
      ok &= window.draw(scene, back, "per-slot exact A/B/A", &physicalA) &&
            window.restored(scene, a, back,
                            "window-control signed near exact A/B/A");
    }
    scene.allContribute();
    scene.mixedNears();
    ok &= window.draw(scene, a, "all maps contribute without saturation");
    physicalA = window.lastPhysical;
    scene.cameras.back()->position.setValue(.58f, 0, 5);
    ok &= window.draw(scene, b, "one map camera mutation with all maps live");
    ok &= check(nearDifference(a, b) > 16,
                "window camera mutation changes visible shadows");
    scene.cameras.back()->position.setValue(
        float(NearSlotScene::cameraX(maps - 1, maps)),
        float(NearSlotScene::cameraY(maps - 1)), 5);
    ok &= window.draw(scene, back, "all maps camera restored", &physicalA) &&
          window.restored(scene, a, back, "all-map exact A/B/A");
    if (general)
      for (size_t unit = 0; unit < 8; ++unit) {
        scene.colorUnit(unit, true);
        ok &= window.draw(scene, b,
                          "independent colored texture unit=" +
                              std::to_string(unit));
        ok &= check(nearDifference(a, b) > 500,
                    "each unit independently affects physical scene");
        scene.colorUnit(unit, false);
        ok &= window.draw(scene, back, "texture unit restored", &physicalA) &&
              window.restored(scene, a, back, "unit exact A/B/A");
      }
    if (mode == 3) {
      for (int slot = 0; slot < maps; ++slot) {
        auto *camera = scene.cameras[slot];
        const float originalNear = camera->nearDistance.getValue(),
                    originalFar = camera->farDistance.getValue();
        for (int invalid = 0; invalid < 7; ++invalid) {
          camera->nearDistance =
              invalid == 0   ? std::numeric_limits<float>::quiet_NaN()
              : invalid == 1 ? std::numeric_limits<float>::infinity()
                             : originalNear;
          camera->farDistance = invalid == 2
                                    ? std::numeric_limits<float>::quiet_NaN()
                                : invalid == 3 ? originalNear
                                : invalid == 4 ? originalNear - 1
                                               : originalFar;
          if (invalid == 5)
            camera->nearDistance = -std::numeric_limits<float>::infinity();
          if (invalid == 6)
            camera->farDistance = std::numeric_limits<float>::infinity();
          ok &= window.refuse(scene,
                              "near/far invalid slot=" + std::to_string(slot) +
                                  " field=" + std::to_string(invalid));
          camera->nearDistance = originalNear;
          camera->farDistance = originalFar;
          ok &=
              window.draw(scene, back, "invalid per-slot camera recovery",
                          &physicalA) &&
              window.restored(scene, a, back, "invalid camera exact recovery");
        }
      }
      for (auto *light : scene.lights) {
        light->ref();
        scene.group->removeChild(light);
      }
      int occurrence = 0;
      for (auto it = scene.lights.rbegin(); it != scene.lights.rend(); ++it)
        scene.group->insertChild(*it, occurrence++);
      ok &= window.draw(scene, b, "all distinct cameras in reversed map slots");
      for (auto *light : scene.lights)
        scene.group->removeChild(light);
      for (int i = 0; i < maps; ++i) {
        scene.group->insertChild(scene.lights[i], i);
        scene.lights[i]->unref();
      }
      ok &= window.draw(scene, back, "map order recovered", &physicalA) &&
            window.restored(scene, a, back, "permutation exact A/B/A");
      scene.cropViewports();
      ok &= window.draw(scene, a, "valid independent camera/subviewports");
      physicalA = window.lastPhysical;
      for (int slot = 0; slot < maps; ++slot) {
        const int kind = scene.authoredViewports[slot].kind;
        scene.authoredViewports[slot].kind = 5;
        ok &= window.refuse(scene,
                            "nonfinite viewport slot=" + std::to_string(slot));
        scene.authoredViewports[slot].kind = kind;
        ok &=
            window.draw(scene, back, "viewport invalid recovery", &physicalA) &&
            window.restored(scene, a, back, "viewport exact A/B/A");
      }
    }
    const auto beforeResize = a;
    const auto beforePhysical = window.lastPhysical;
    if (!window.resize(160))
      return false;
    ok &= window.draw(scene, b, "resized current first physical image");
    if (!window.resize(side))
      return false;
    ok &= window.draw(scene, back, "original physical extent restored",
                      &beforePhysical) &&
          window.restored(scene, beforeResize, back, "resize exact A/B/A");
  }
  return ok;
}
} // namespace
int main(int argc, char **argv) {
  unsigned samples = 2;
  int maps = 8, mode = 0;
  bool general = false, readback = false, portable = false;
  bool stereo = false, shared = false, stereoOption = false, directional = true,
       orthographic = false;
  int stereoMapping = SoCamera::ADJUST_CAMERA, stereoSlot = -1,
      stereoRasterKind = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--stereo")
      stereo = true;
    else if (arg == "--stereo-shared") {
      stereo = true;
      shared = true;
    } else if (arg == "--stereo-general") {
      general = true;
      stereoOption = true;
    } else if (arg == "--stereo-maps" || arg == "--stereo-slot" ||
               arg == "--stereo-light" || arg == "--stereo-camera" ||
               arg == "--stereo-mapping" || arg == "--stereo-raster-kind") {
      stereoOption = true;
      if (++i == argc)
        return 2;
      const std::string value = argv[i];
      if (arg == "--stereo-maps") {
        if (value != "4" && value != "8")
          return 2;
        maps = std::stoi(value);
      } else if (arg == "--stereo-slot") {
        if (value.size() != 1 || value[0] < '0' || value[0] > '7')
          return 2;
        stereoSlot = value[0] - '0';
      } else if (arg == "--stereo-light") {
        if (value != "directional" && value != "spot")
          return 2;
        directional = value == "directional";
      } else if (arg == "--stereo-camera") {
        if (value != "perspective" && value != "orthographic")
          return 2;
        orthographic = value == "orthographic";
      } else if (arg == "--stereo-raster-kind") {
        if (value != "image" && value != "marker" && value != "both")
          return 2;
        stereoRasterKind = value == "image" ? 1 : value == "marker" ? 2 : 0;
      } else if (value == "adjust")
        stereoMapping = SoCamera::ADJUST_CAMERA;
      else if (value == "leave")
        stereoMapping = SoCamera::LEAVE_ALONE;
      else if (value == "crop")
        stereoMapping = SoCamera::CROP_VIEWPORT_NO_FRAME;
      else if (value == "fill")
        stereoMapping = SoCamera::CROP_VIEWPORT_FILL_FRAME;
      else if (value == "line")
        stereoMapping = SoCamera::CROP_VIEWPORT_LINE_FRAME;
      else
        return 2;
    } else if (arg == "--readback")
      readback = true;
    else if (arg == "--near-general")
      general = true;
    else if (arg == "--portable-sampling")
      portable = true;
    else if (arg == "--raster")
      mode = 1;
    else if (arg == "--crop")
      mode = 2;
    else if (arg == "--state")
      mode = 3;
    else if (arg == "--scissor")
      mode = 4;
    else if (arg == "--geometry")
      mode = 0;
    else if (arg == "--samples" || arg == "--near-maps") {
      if (++i == argc)
        return 2;
      const std::string value = argv[i];
      if (arg == "--samples") {
        if (value != "1" && value != "2" && value != "4")
          return 2;
        samples = unsigned(std::stoi(value));
      } else {
        if (value != "4" && value != "8")
          return 2;
        maps = std::stoi(value);
      }
    } else
      return 2;
  }
  if (stereoOption && !stereo)
    return 2;
  if (stereo && (samples == 1 || stereoSlot >= maps || (shared && mode == 4)))
    return 2;
  if (!XInitThreads())
    return 1;
  SoDB::init();
  CoinRenderAction::initClass();
  CoinRenderCapabilities caps{};
  if (coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,
                                     &caps, sizeof(caps)) ||
      !caps.gpu_available)
    return 77;
  std::cout << "camera_window backend=" << caps.backend
            << " renderer=" << caps.renderer << " adapter=" << caps.adapter_name
            << " samples=" << samples << " maps=" << maps
            << " general=" << general << " explicit_readback=" << readback
            << '\n';
  if (stereo) {
    std::cout << "stereo_window_environment directional=" << directional
              << " orthographic=" << orthographic
              << " mapping=" << stereoMapping << " mode=" << mode
              << " shared=" << shared << " slot=" << stereoSlot
              << " portable=" << portable << '\n';
    StereoCameraWindow window(caps, samples, readback, portable);
    if (!window.ready)
      return 1;
    return stereoWindowProfiles(window, maps, general, mode, directional,
                                orthographic, stereoMapping, shared, stereoSlot,
                                stereoRasterKind)
               ? 0
               : 1;
  }
  CameraWindow window(caps, samples, readback, portable);
  if (!window.ready)
    return check(false, "real requested physical GPU target prepared") ? 0 : 1;
  return windowProfiles(window, maps, general, mode) ? 0 : 1;
}
