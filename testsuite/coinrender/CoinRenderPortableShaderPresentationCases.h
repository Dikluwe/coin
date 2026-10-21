#ifndef COIN_PORTABLE_SHADER_PRESENTATION_CASES_H
#define COIN_PORTABLE_SHADER_PRESENTATION_CASES_H
// Included in portable_module_test after Run. The rectangle is authored in
// world coordinates; its bottom/right edges provide fractional coverage at
// 64, 96 and 160 pixels without prescribing vendor sample locations.
void presentationGeometry(Scene &scene) {
  const SbVec3f p[] = {SbVec3f(-.75f, -.7078125f, 0),
                       SbVec3f(.765625f, -.7078125f, 0),
                       SbVec3f(.765625f, .75f, 0), SbVec3f(-.75f, .75f, 0)};
  scene.positions->point.setValues(0, 4, p);
}
bool coverageImage(const Frame &f, unsigned side, unsigned samples, Color color,
                   bool opaqueWindow, bool report = true) {
  if (f.rgba.size() != size_t(side) * side * 4 ||
      (!opaqueWindow && f.depth.size() != size_t(side) * side))
    return false;
  unsigned inside = 0, outside = 0, bad = 0, depthBad = 0;
  unsigned partial = 0, odd = 0, full = 0, empty = 0;
  const double margin = 2.1 * 2 / side;
  for (unsigned y = 0; y < side; ++y)
    for (unsigned x = 0; x < side; ++x) {
      const size_t p = size_t(y) * side + x;
      const double wx = (x + .5) * 2 / side - 1;
      const double wy = 1 - (y + .5) * 2 / side;
      const bool in = wx > -.75 + margin && wx < .765625 - margin &&
                      wy > -.7078125 + margin && wy < .75 - margin;
      const bool out = wx < -.75 - margin || wx > .765625 + margin ||
                       wy < -.7078125 - margin || wy > .75 + margin;
      inside += in;
      outside += out;
      if (in || out) {
        for (unsigned c = 0; c < 3; ++c)
          bad += std::abs(f.rgba[4 * p + c] - (in ? 255. * color[c] : 0)) > 3;
        if (!opaqueWindow) {
          bad += std::abs(int(f.rgba[4 * p + 3]) - (in ? 255 : 0)) > 1;
          depthBad +=
              in ? std::abs(f.depth[p] - 2.9 / 9.9) > 2e-5 : f.depth[p] != 1;
        }
      }
      // Select the closest admissible k/N, rather than the first match:
      // low signals can have overlapping +/-3 intervals.
      double best = std::numeric_limits<double>::infinity();
      unsigned chosen = samples + 1;
      for (unsigned k = 0; k <= samples; ++k) {
        double error = 0;
        bool valid = true;
        for (unsigned c = 0; c < 3; ++c) {
          const double delta =
              f.rgba[4 * p + c] - 255. * color[c] * k / samples;
          valid &= std::abs(delta) <= 3;
          error += delta * delta;
        }
        if (!opaqueWindow) {
          const double delta = f.rgba[4 * p + 3] - 255. * k / samples;
          valid &= std::abs(delta) <= 1;
          error += delta * delta;
        }
        if (valid && error < best) {
          best = error;
          chosen = k;
        }
      }
      bad += chosen > samples;
      empty += chosen == 0;
      full += chosen == samples;
      partial += chosen > 0 && chosen < samples;
      odd += chosen < samples && (chosen & 1);
      if (!opaqueWindow)
        depthBad += f.depth[p] != 1 && std::abs(f.depth[p] - 2.9 / 9.9) > 2e-5;
    }
  if (report)
    std::cout << "portable_module_coverage extent=" << side
              << " samples=" << samples << " inside=" << inside
              << " outside=" << outside << " full=" << full
              << " empty=" << empty << " partial=" << partial << " odd=" << odd
              << " bad=" << bad << " depth_bad=" << depthBad << std::endl;
  return inside > 100 && outside > 100 && full > 100 && empty > 100 && !bad &&
         !depthBad && (samples == 1 || partial > 0) &&
         (samples != 4 || odd > 0);
}
bool samePhysical(const Frame &a, const Frame &b) { return a.rgba == b.rgba; }
bool currentPresentation(Run &run, const Frame &readback, Color color) {
  if (!run.window)
    return true;
  const auto begin = std::chrono::steady_clock::now();
  unsigned attempts = 0, maxDelta = 0;
  do {
    Frame physical;
    ++attempts;
    if (!run.physical(physical) || physical.rgba.size() != readback.rgba.size())
      continue;
    maxDelta = 0;
    for (size_t i = 0; i < physical.rgba.size(); ++i)
      if (i % 4 != 3)
        maxDelta = std::max(
            maxDelta,
            unsigned(std::abs(int(physical.rgba[i]) - int(readback.rgba[i]))));
    if (maxDelta <= 3 &&
        coverageImage(physical, run.extent, run.samples, color, true, false) &&
        std::chrono::steady_clock::now() - begin <= std::chrono::seconds(5)) {
      run.lastPhysical = physical;
      std::cout << "portable_module_physical extent=" << run.extent
                << " attempts=" << attempts << " max_delta=" << maxDelta
                << " present_ms="
                << std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - begin)
                       .count()
                << std::endl;
      return true;
    }
  } while (std::chrono::steady_clock::now() - begin < std::chrono::seconds(5));
  std::cerr << "portable_module_physical timeout attempts=" << attempts
            << " max_delta=" << maxDelta << std::endl;
  return false;
}
bool presented(Run &run, Color color, Frame &frame, const char *label) {
  const bool rendered = run.render(frame, label);
  return rendered &&
         check(coverageImage(frame, run.extent, run.samples, color, run.window),
               "authored color/depth and nonempty actual sample coverage") &&
         check(currentPresentation(run, frame, color),
               "whole current X11 image and independent RGB oracle within 5s");
}
Module presentationModule() {
  Module m;
  declare(m, "rgba", Type::FLOAT4);
  m.expressions = {uniform("rgba", Type::FLOAT4)};
  m.alphaMayVary = TRUE;
  return m;
}
bool untouched(Run &run, const Frame &stable, const Frame &physical,
               const char *label) {
  const Frame after = run.read();
  bool ok = check(after.serial == stable.serial && after.rgba == stable.rgba &&
                      after.depth == stable.depth,
                  label);
  if (run.window) {
    Frame now;
    ok &= check(run.physical(now) && samePhysical(now, physical),
                "peer/refusal preserves entire already-published physical "
                "image exactly");
  }
  return ok;
}
bool presentation(Run &run, const CoinRenderCapabilities &caps, bool portable) {
  presentationGeometry(run.scene);
  const Color a = {{.13f, .57f, .29f, 1}}, b = {{.71f, .19f, .53f, 1}};
  auto *value = run.scene.parameter4("rgba", a);
  run.scene.attach(true);
  bool ok = run.bind(presentationModule());
  Frame first, changed, restored;
  ok &= presented(run, a, first, "cold module first publication");
  value->value.setValue(b[0], b[1], b[2], 1);
  ok &= presented(run, b, changed, "parameter-only visible transition");
  ok &= check(run.lastCompiles.empty(),
              "parameter edit preserves compiled module");
  value->value.setValue(a[0], a[1], a[2], 1);
  ok &= presented(run, a, restored, "authorial A/B/A with current delivery");
  const Frame stablePhysical = run.lastPhysical;
  value->value.setValue(std::numeric_limits<float>::quiet_NaN(), a[1], a[2], 1);
  ok &= run.refuse(restored, "invalid current uniform before publication");
  ok &= untouched(run, restored, stablePhysical,
                  "refusal keeps color/depth/serial bitwise");
  value->value.setValue(a[0], a[1], a[2], 1);
  ok &=
      presented(run, a, restored, "same target recovers valid module binding");
  // The peer is created after the first publication, so it cannot warm up or
  // anchor the original cold frame. All peer operations occur without apply(A).
  const Frame beforePeer = restored, physicalBeforePeer = run.lastPhysical;
  {
    Run peer(caps, portable, 1, false);
    if (!check(bool(peer.target), "independent live offscreen peer created"))
      return false;
    presentationGeometry(peer.scene);
    peer.scene.attach(true);
    auto *other = peer.scene.parameter4("rgba", b);
    ok &= peer.bind(presentationModule());
    Frame peerFrame;
    ok &= presented(peer, b, peerFrame, "peer distinct bindings");
    other->value.setValue(.35f, .65f, .15f, 1);
    ok &= presented(peer, {{.35f, .65f, .15f, 1}}, peerFrame, "peer mutation");
    other->value.setValue(.35f, std::numeric_limits<float>::infinity(), .15f,
                          1);
    ok &= peer.refuse(peerFrame, "peer invalid binding");
    ok &= untouched(run, beforePeer, physicalBeforePeer,
                    "peer mutation/refusal cannot publish original target");
    other->value.setValue(b[0], b[1], b[2], 1);
    ok &= presented(peer, b, peerFrame, "peer recovery");
  }
  ok &= untouched(run, beforePeer, physicalBeforePeer,
                  "peer destruction cannot alter original publication");
  const unsigned sizes[] = {96, 160, 64};
  for (unsigned size : sizes) {
    ok &= check(run.resize(size), "public extent mutation accepted");
    ok &= presented(run, a, restored,
                    "one current application after extent mutation");
  }
  return ok;
}
bool lifecycle(const CoinRenderCapabilities &caps, bool portable,
               unsigned samples) {
  bool ok = true;
  for (unsigned generation = 0; generation < 2; ++generation) {
    Run run(caps, portable, samples, false);
    if (!check(bool(run.target),
               "fresh target after previous runtime destruction"))
      return false;
    presentationGeometry(run.scene);
    run.scene.attach(true);
    const Color color =
        generation ? Color{{.7f, .2f, .45f, 1}} : Color{{.2f, .6f, .35f, 1}};
    run.scene.parameter4("rgba", color);
    ok &= run.bind(presentationModule());
    Frame frame;
    ok &= presented(run, color, frame, "fresh generation first publication");
  }
  return ok;
}
#endif
