// Uses CameraWindow's real Xlib target, physical pixels and resize lifecycle.
// There is no retained runtime or extra apply to wait for presentation.
struct StereoCameraWindow : CameraWindow {
  StereoCameraWindow(const CoinRenderCapabilities &caps, unsigned count,
                     bool readback, bool portable)
      : CameraWindow(caps, count, readback, portable) {
    ready = ready && check(nearNativeMsaa(*off) && nearNativeMsaa(*shown) &&
                               off->getLastSubmissionSerial() == 0 &&
                               shown->getLastSubmissionSerial() == 0,
                           "stereo window starts cold with requested Native "
                           "MSAA and no scene publication");
  }
  bool observeStereo(StereoScene &scene, const std::vector<uint8_t> &readback,
                     const std::string &label) {
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::seconds(5);
    Frame current;
    unsigned attempts = 0;
    bool accepted = false;
    do {
      ++attempts;
      current.rgba = physical();
      if ((readback.empty() || rgbClose(readback, current.rgba)) &&
          stereoImage(scene, current, label + " physical", extent, false,
                      false) &&
          stereoCoverage(scene, current, samples, extent, true, false)) {
        accepted = true;
        break;
      }
    } while (std::chrono::steady_clock::now() < deadline);
    std::cout << "stereo_window_observe phase=" << label << " extent=" << extent
              << " serial=" << shown->getLastSubmissionSerial()
              << " attempts=" << attempts << " ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - start)
                     .count()
              << " accepted=" << accepted << '\n';
    if (!accepted) {
      stereoImage(scene, current, label + " final physical", extent, false,
                  true);
      stereoCoverage(scene, current, samples, extent, true, true);
    }
    lastPhysical = current.rgba;
    return check(
        accepted,
        "current stereo physical image within the original five seconds");
  }
  static void arm(StereoScene &scene) {
    if (scene.shared)
      for (size_t i = 0; i < scene.cameras.size(); ++i)
        scene.probes[i].arm(scene.cameras[i]);
  }
  static bool preserved(const StereoScene &scene,
                        const std::vector<uint64_t> &source) {
    bool ok =
        check(source == stereoAuthorState(scene),
              "window apply never mutates source camera fields/eye/NodeId");
    if (scene.shared)
      for (const auto &probe : scene.probes)
        ok &=
            check(probe.same && probe.visits >= 2,
                  "each shared stereo camera occurrence preserves its source");
    return ok;
  }
  bool draw(StereoScene &scene, Frame &control, const std::string &label) {
    const auto oldOff = off->getLastSubmissionSerial(),
               oldWindow = shown->getLastSubmissionSerial();
    const unsigned offCount = offAudit->submissions,
                   windowCount = windowAudit->submissions;
    control.stereoBeforeSerial = oldOff;
    control.stereoInputs = stereoMsaaInputs(
        scene, extent, samples, shown->getOptions().textureSamplingPolicy);
    const auto source = stereoAuthorState(scene);
    arm(scene);
    offAction.apply(scene.root);
    off->readbackRGBA(control.rgba);
    off->readbackDepth(control.depth);
    control.serial = off->getLastSubmissionSerial();
    bool ok = check(offAction.getLastStatus() == CoinRenderAction::SUCCESS &&
                        control.serial > oldOff &&
                        offAudit->submissions == offCount + 1,
                    label + " one offscreen control apply/publication");
    ok &= preserved(scene, source);
    ok &= stereoDelivery(scene, *offAudit, extent);
    ok &= stereoImage(scene, control, label + " offscreen", extent);
    ok &= stereoCoverage(scene, control, samples, extent, false);
    const bool requested =
        !explicitReadback || shown->requestWindowReadbackRGBA();
    arm(scene);
    windowAction.apply(scene.root);
    std::vector<uint8_t> readback;
    shown->readbackRGBA(readback);
    std::cout << "stereo_window_apply phase=" << label << " samples=" << samples
              << " readback=" << explicitReadback
              << " status=" << windowAction.getLastStatus()
              << " serial=" << oldWindow << ','
              << shown->getLastSubmissionSerial()
              << " submissions=" << windowCount << ','
              << windowAudit->submissions
              << " error=" << windowAction.getLastError().getString() << '\n';
    ok &= check(requested &&
                    windowAction.getLastStatus() == CoinRenderAction::SUCCESS &&
                    shown->getLastSubmissionSerial() > oldWindow &&
                    windowAudit->submissions == windowCount + 1,
                label + " exactly one current window apply/publication");
    ok &= preserved(scene, source);
    ok &= stereoDelivery(scene, *windowAudit, extent);
    if (explicitReadback) {
      Frame read;
      read.rgba = readback;
      ok &= stereoImage(scene, read, label + " same-apply window readback",
                        extent, false);
      ok &= stereoCoverage(scene, read, samples, extent, true);
    } else
      ok &= check(readback.empty(),
                  "stereo GPU-only window has no unsolicited RGBA publication");
    if (ok)
      ok &= observeStereo(scene, readback, label);
    lastOff = control;
    lastReadback = readback;
    return ok;
  }
  bool restored(StereoScene &scene, const Frame &before, const Frame &current,
                const std::string &label, bool sameInputs = true) {
    const auto inputs = stereoMsaaInputs(
        scene, extent, samples, shown->getOptions().textureSamplingPolicy);
    bool ok = check(
        !current.stereoInputs.empty() && current.stereoInputs == inputs &&
            (!sameInputs || before.stereoInputs == current.stereoInputs) &&
            current.serial > current.stereoBeforeSerial &&
            current.serial == off->getLastSubmissionSerial(),
        label + " restored authorial parameters and new publication");
    ok &= stereoDelivery(scene, *offAudit, extent) &&
          stereoDelivery(scene, *windowAudit, extent);
    ok &= stereoImage(scene, current, label + " current control", extent);
    ok &= stereoCoverage(scene, current, samples, extent, false);
    Frame physicalFrame;
    physicalFrame.rgba = lastPhysical;
    ok &= stereoImage(scene, physicalFrame, label + " current physical", extent,
                      false);
    ok &= stereoCoverage(scene, physicalFrame, samples, extent, true);
    return ok;
  }
  bool refuse(StereoScene &scene, const std::string &label) {
    const auto source = stereoAuthorState(scene);
    const unsigned offProducers = offAudit->producers,
                   windowProducers = windowAudit->producers;
    const bool rejected = refuseRoot(scene.root, label);
    return check(rejected && offAudit->producers == offProducers &&
                     windowAudit->producers == windowProducers &&
                     source == stereoAuthorState(scene),
                 label + " no producers/source mutation on atomic refusal");
  }
};

bool stereoWindowParameters(StereoCameraWindow &window, StereoScene &scene,
                            size_t slot, const Frame &original) {
  auto *camera = scene.cameras[slot];
  const float adjustment = camera->getStereoAdjustment(),
              balance = camera->getBalanceAdjustment(),
              focal = camera->focalDistance.getValue();
  const auto originalId = camera->getNodeId();
  const auto originalShapes = stereoSourceShapes(scene, slot);
  double minimum = std::numeric_limits<double>::infinity();
  for (size_t i = 0; i < scene.occurrences[slot].size(); ++i) {
    if (scene.occurrences[slot][i].external)
      continue;
    double width, height;
    scene.nearDimensions(slot, i, width, height);
    const double n = camera->nearDistance.getValue();
    minimum = std::min(minimum, scene.viewport(slot, i).w * n / width *
                                    (1 / double(focal) - 1 / 4.55));
  }
  camera->setStereoAdjustment(float(adjustment + 2 * (2 * 2.1 + 2) / minimum));
  bool ok = check(camera->getNodeId() == originalId &&
                      stereoSourceChanges(scene, originalShapes,
                                          stereoSourceShapes(scene, slot),
                                          window.extent) >= 6,
                  "window adjustment has safe contradictory interiors without "
                  "a NodeId touch");
  Frame changed, back, base;
  ok &= window.draw(scene, changed, "observable stereo adjustment mutation") &&
        check(nearDifference(original, changed) > 16,
              "stereo adjustment affects window controls");
  camera->setStereoAdjustment(adjustment);
  ok &= window.draw(scene, back, "stereo adjustment restored") &&
        window.restored(scene, original, back, "window adjustment recovery");
  const float parameterFocal = 4.65f;
  const double gain = std::min(
      std::abs(1 / (double(parameterFocal) * 1.7f) -
               1 / double(parameterFocal)),
      std::abs(1 / (double(parameterFocal) + 2) - 1 / double(parameterFocal)));
  minimum = std::numeric_limits<double>::infinity();
  for (size_t i = 0; i < scene.occurrences[slot].size(); ++i) {
    if (scene.occurrences[slot][i].external)
      continue;
    double width, height;
    scene.nearDimensions(slot, i, width, height);
    minimum =
        std::min(minimum, scene.viewport(slot, i).w *
                              camera->nearDistance.getValue() / width * gain);
  }
  camera->focalDistance = parameterFocal;
  camera->setBalanceAdjustment(1);
  camera->setStereoAdjustment(float(2 * (2 * 2.1 + 2) / minimum));
  const auto baseId = camera->getNodeId();
  const auto baseShapes = stereoSourceShapes(scene, slot);
  ok &= window.draw(scene, base,
                    "authorial window convergence-plane parameter base");
  camera->setBalanceAdjustment(1.7f);
  ok &= check(camera->getNodeId() == baseId &&
                  stereoSourceChanges(scene, baseShapes,
                                      stereoSourceShapes(scene, slot),
                                      window.extent) >= 6,
              "window balance has safe contradictory interiors without "
              "touching NodeId");
  ok &= window.draw(scene, changed, "non-field window balance mutation") &&
        check(nearDifference(base, changed) > 16,
              "balance affects window controls");
  camera->setBalanceAdjustment(1);
  ok &= check(camera->getNodeId() == baseId,
              "window balance restoration leaves NodeId unchanged");
  ok &= window.draw(scene, back, "window balance base restored") &&
        window.restored(scene, base, back, "window balance own-base recovery");
  camera->focalDistance = parameterFocal + 2;
  ok &= check(stereoSourceChanges(scene, baseShapes,
                                  stereoSourceShapes(scene, slot),
                                  window.extent) >= 6,
              "window focal has safe contradictory interiors");
  ok &= window.draw(scene, changed, "window focal mutation") &&
        check(nearDifference(base, changed) > 16,
              "focal affects window controls");
  camera->focalDistance = parameterFocal;
  ok &= window.draw(scene, back, "window focal base restored") &&
        window.restored(scene, base, back, "window focal own-base recovery");
  camera->setStereoAdjustment(adjustment);
  camera->setBalanceAdjustment(balance);
  camera->focalDistance = focal;
  ok &=
      window.draw(scene, back, "original window camera parameters restored") &&
      window.restored(scene, original, back, "window parameter A/B/A");
  return ok;
}

bool stereoWindowInvalid(StereoCameraWindow &window, StereoScene &scene,
                         size_t slot, const Frame &stable) {
  auto *camera = scene.cameras[slot];
  auto &viewport = scene.occurrences[slot][0].viewport;
  const float n = camera->nearDistance.getValue(),
              f = camera->farDistance.getValue(),
              focal = camera->focalDistance.getValue();
  const float adjustment = camera->getStereoAdjustment(),
              balance = camera->getBalanceAdjustment();
  const auto mode = camera->getStereoMode();
  const auto originalViewport = viewport;
  const float nan = std::numeric_limits<float>::quiet_NaN(),
              inf = std::numeric_limits<float>::infinity();
  bool ok = true;
  Frame back, zero, negative;
  for (int invalid = 0; invalid < 18; ++invalid) {
    switch (invalid) {
    case 0:
      camera->nearDistance = 0;
      break;
    case 1:
      camera->nearDistance = -1;
      break;
    case 2:
      camera->nearDistance = nan;
      break;
    case 3:
      camera->nearDistance = inf;
      break;
    case 4:
      camera->farDistance = nan;
      break;
    case 5:
      camera->farDistance = inf;
      break;
    case 6:
      camera->farDistance = n;
      break;
    case 7:
      camera->farDistance = n - .5f;
      break;
    case 8:
      camera->setStereoAdjustment(nan);
      break;
    case 9:
      camera->setStereoAdjustment(inf);
      break;
    case 10:
      camera->setBalanceAdjustment(nan);
      break;
    case 11:
      camera->setBalanceAdjustment(inf);
      break;
    case 12:
      camera->focalDistance = nan;
      break;
    case 13:
      camera->focalDistance = inf;
      break;
    case 14:
      camera->focalDistance = std::numeric_limits<float>::max();
      camera->setBalanceAdjustment(std::numeric_limits<float>::max());
      break;
    case 15:
      camera->setStereoMode(static_cast<SoCamera::StereoMode>(3));
      break;
    case 16:
      viewport.kind = 5;
      break;
    case 17:
      viewport.kind = 1;
      viewport.left = 32768. / 256;
      break;
    }
    ok &= window.refuse(scene,
                        "invalid stereo/window slot=" + std::to_string(slot) +
                            " field=" + std::to_string(invalid));
    camera->nearDistance = n;
    camera->farDistance = f;
    camera->focalDistance = focal;
    camera->setStereoAdjustment(adjustment);
    camera->setBalanceAdjustment(balance);
    camera->setStereoMode(mode);
    viewport.kind = originalViewport.kind;
    viewport.left = originalViewport.left;
    ok &= window.draw(scene, back, "valid stereo window recovery") &&
          window.restored(scene, stable, back, "invalid window slot recovered");
  }
  camera->focalDistance = 0;
  ok &= window.draw(scene, zero, "finite focal0 clamps to near");
  camera->focalDistance = -2;
  ok &= window.draw(scene, negative, "finite negative focal clamps to near") &&
        window.restored(scene, zero, negative, "native focal clamp equivalence",
                        false);
  camera->focalDistance = focal;
  camera->setBalanceAdjustment(0);
  ok &= window.draw(scene, negative, "finite balance0 clamps to near") &&
        window.restored(scene, zero, negative, "native zero balance clamp",
                        false);
  camera->setBalanceAdjustment(-1);
  ok &=
      window.draw(scene, negative, "finite negative balance clamps to near") &&
      window.restored(scene, zero, negative, "native negative balance clamp",
                      false);
  camera->setBalanceAdjustment(balance);
  ok &= window.draw(scene, back, "finite focal/balance parameters restored") &&
        window.restored(scene, stable, back, "finite clamp restoration");
  return ok;
}

bool stereoWindowProfiles(StereoCameraWindow &window, int maps, bool general,
                          int mode, bool directional, bool orthographic,
                          int mapping, bool shared, int selectedSlot,
                          int rasterKind) {
  bool ok = true;
  for (int raster = mode == 1 ? 1 : 0; raster <= (mode == 1   ? 2
                                                  : mode == 4 ? 1
                                                              : 0);
       ++raster) {
    if (mode == 1 && rasterKind && rasterKind != raster)
      continue;
    StereoScene scene(maps, directional, orthographic,
                      mode == 2 || mode == 4 ? SoCamera::CROP_VIEWPORT_NO_FRAME
                                             : mapping,
                      raster, general, shared);
    stereoPrepareMsaaScene(scene, window.samples);
    if (mode == 4)
      stereoScissorScene(scene, window.samples);
    Frame a, b, back;
    for (int slot = 0; slot < maps; ++slot) {
      if (selectedSlot >= 0 && selectedSlot != slot)
        continue;
      scene.isolate(slot);
      auto *camera = scene.cameras[slot];
      camera->setStereoMode(SoCamera::LEFT_VIEW);
      const auto sourceId = camera->getNodeId();
      ok &= window.draw(scene, a,
                        "first/LEFT stereo slot=" + std::to_string(slot));
      if (mode == 4) {
        const int kind = scene.occurrences[slot][0].viewport.kind;
        scene.occurrences[slot][0].viewport.kind = 5;
        ok &= window.refuse(scene, "invalid crossed stereo scissor");
        scene.occurrences[slot][0].viewport.kind = kind;
        ok &= window.draw(scene, back, "crossed stereo scissor recovered") &&
              window.restored(scene, a, back, "crossed scissor restoration");
        continue;
      }
      const auto leftShapes = stereoSourceShapes(scene, slot);
      camera->setStereoMode(SoCamera::RIGHT_VIEW);
      const unsigned changed = stereoSourceChanges(
          scene, leftShapes, stereoSourceShapes(scene, slot), window.extent);
      std::cout << "stereo_window_fixture slot=" << slot
                << " safe_eye_changed_pixels=" << changed << '\n';
      ok &= check(
          camera->getNodeId() == sourceId && changed >= 6,
          "window LEFT/RIGHT has safe contrasting interiors and stable NodeId");
      ok &=
          window.draw(scene, b, "RIGHT stereo slot=" + std::to_string(slot)) &&
          check(nearDifference(a, b) > 16,
                "window signed stereo eye visibly contributes");
      camera->setStereoMode(SoCamera::LEFT_VIEW);
      ok &= check(camera->getNodeId() == sourceId,
                  "window LEFT restoration leaves source NodeId unchanged");
      ok &= window.draw(scene, back, "LEFT stereo restored") &&
            window.restored(scene, a, back, "window LEFT/RIGHT A/B/A");
      if (!raster)
        ok &= stereoWindowParameters(window, scene, size_t(slot), a);
      if (mode == 3)
        ok &= stereoWindowInvalid(window, scene, size_t(slot), a);
    }
    if (raster == 2)
      stereoAlignJointMarkers(scene);
    scene.together();
    ok &= window.draw(scene, a, "joint nonsaturating stereo maps");
    for (auto *light : scene.lights) {
      light->ref();
      scene.group->removeChild(light);
    }
    int occurrence = 0;
    for (auto it = scene.lights.rbegin(); it != scene.lights.rend(); ++it)
      scene.group->insertChild(*it, occurrence++);
    ok &= window.draw(scene, b, "all stereo map occurrences reversed");
    for (auto *light : scene.lights)
      scene.group->removeChild(light);
    for (size_t i = 0; i < scene.lights.size(); ++i) {
      scene.group->insertChild(scene.lights[i], int(i));
      scene.lights[i]->unref();
    }
    ok &= window.draw(scene, back, "stereo map order restored") &&
          window.restored(scene, a, back, "window map permutation recovery");
    if (general)
      for (size_t unit = 0; unit < 8; ++unit) {
        scene.colorUnit(unit, true);
        ok &= window.draw(scene, b,
                          "independent stereo receiver texture=" +
                              std::to_string(unit)) &&
              check(nearDifference(a, b) > 500,
                    "every stereo receiver texture contributes numerically");
        scene.colorUnit(unit, false);
        ok &= window.draw(scene, back, "stereo receiver texture restored") &&
              window.restored(scene, a, back, "window unit binding recovery");
      }
    if (mode == 2) {
      for (int decoration : {SoCamera::CROP_VIEWPORT_FILL_FRAME,
                             SoCamera::CROP_VIEWPORT_LINE_FRAME}) {
        for (auto *camera : scene.cameras)
          camera->viewportMapping = decoration;
        ok &= window.draw(scene, b, "stereo CROP decoration is not a caster") &&
              window.restored(scene, a, b, "window CROP semantics");
      }
      for (auto *camera : scene.cameras)
        camera->viewportMapping = SoCamera::CROP_VIEWPORT_NO_FRAME;
      ok &= window.draw(scene, back, "stereo CROP NO_FRAME restored") &&
            window.restored(scene, a, back, "window CROP mapping recovery");
    }
    const Frame beforeResize = a;
    if (!window.resize(160))
      return false;
    ok &= window.draw(scene, b, "first stereo physical image after resize160");
    if (!window.resize(side))
      return false;
    ok &= window.draw(scene, back,
                      "first stereo physical image after resize128") &&
          window.restored(scene, beforeResize, back,
                          "window resize authorial restoration");
  }
  return ok;
}
