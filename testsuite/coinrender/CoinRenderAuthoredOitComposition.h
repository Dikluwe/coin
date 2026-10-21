// Included in authored_composition. Weighted weight/revealage and peeling
// source-over are evaluated from authored geometry, material and camera inputs.
bool oitOracle(const TransparentScene &scene, const Frame &frame, bool peeling) {
  std::vector<StereoShape> shapes;
  for (size_t slot = 0; slot < scene.lights.size(); ++slot) shapes.push_back(stereoShape(scene, slot, 0));
  unsigned shadowed = 0, lit = 0, bad = 0; double maximum = 0;
  for (int y = 12; y < side - 12; ++y) for (int x = 12; x < side - 12; ++x) {
    const double wx = (x + .5 - side / 2.) * 4 / side, wy = (side / 2. - y - .5) * 4 / side;
    double source[2][4]{}; bool measurable = true, shadow = false;
    for (unsigned object = 0; object < 2; ++object) {
      const double distance = 5 - scene.objectZ[object];
      const double width = scene.directional ? std::sqrt(96.) : 2 * distance * std::tan(.6);
      const double mx = (.5 + wx / width) * 256, my = (.5 + wy / width) * 256;
      double strength = 0;
      for (size_t slot = 0; slot < shapes.size(); ++slot) {
        const double intensity = scene.lights[slot]->intensity.getValue(); if (!intensity) continue;
        const bool hit = stereoContains(shapes[slot].inner, mx, my, 2.1);
        const bool clear = !stereoContains(shapes[slot].outer, mx, my, -2.1);
        if (!hit && !clear) { measurable = false; break; }
        shadow |= hit; if (!hit) strength += intensity;
      }
      const double lambert = scene.directional ? 1 : distance / std::sqrt(distance * distance + wx * wx + wy * wy);
      const auto color = scene.materials[object]->diffuseColor[0];
      for (int c = 0; c < 3; ++c) source[object][c] = color[c] * strength * lambert;
      source[object][3] = 1 - scene.materials[object]->transparency[0];
    }
    if (!measurable) continue;
    shadowed += shadow; lit += !shadow;
    double expected[4] = {0, 0, 0, 0};
    if (peeling) {
      // Object0 z=-1 is farther, object1 z=-.5 nearer, independently of traversal.
      for (unsigned object = 0; object < 2; ++object) {
        const double alpha = source[object][3];
        for (int c = 0; c < 3; ++c) expected[c] = source[object][c] * alpha + expected[c] * (1 - alpha);
        expected[3] = alpha + expected[3] * (1 - alpha);
      }
    } else {
      double sum = 0, revealage = 1;
      for (unsigned object = 0; object < 2; ++object) {
        const double alpha = source[object][3], depth = (5 - scene.objectZ[object]) / 19.;
        const double weight = std::min(8., alpha * 8 + .01) * std::min(16., std::pow(1 - depth, 3) * 16 + .1);
        const double coefficient = alpha * weight; sum += coefficient; revealage *= 1 - alpha;
        for (int c = 0; c < 3; ++c) expected[c] += source[object][c] * coefficient;
      }
      expected[3] = 1 - revealage;
      for (int c = 0; c < 3; ++c) expected[c] = sum > 0 ? expected[c] / sum * expected[3] : 0;
    }
    const size_t pixel = size_t(y * side + x);
    for (int c = 0; c < 4; ++c) {
      const double error = std::abs(double(frame.rgba[pixel * 4 + c]) - expected[c] * 255);
      maximum = std::max(maximum, error); bad += error > 3;
    }
    bad += frame.depth[pixel] != 1.f;
  }
  std::cout << "authored_oit peeling=" << peeling << " lit=" << lit << " shadow=" << shadowed
            << " bad=" << bad << " max=" << maximum << '\n';
  return check(lit > 500 && shadowed >= 4 && bad == 0,
               "independent authored stereo maps, weighted/peeling RGBA equation and unchanged depth");
}
bool oit(bool cpu, bool peeling) {
  const auto mechanism = peeling ? COIN_RENDER_TRANSPARENCY_PEELING : COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;
  CompositionHarness h(cpu, mechanism); if (!h.target) return false;
  h.action.setTransparencyType(CoinRenderAction::SORTED_LAYERS_BLEND);
  h.action.setSortedLayersNumPasses(4);
  TransparentScene scene(peeling, true); bool ok = true;
  auto draw = [&](Frame &frame, const char *label) {
    const auto authored = stereoAuthorState(scene);
    return h.render(scene.root, frame, label) && compositionDelivery(scene, *h.audit) &&
           check(stereoAuthorState(scene) == authored && h.target->getOptions().transparency == mechanism,
                 "selected OIT mechanism and authorial map LEFT/RIGHT preserved") &&
           (cpu || oitOracle(scene, frame, peeling));
  };
  for (int slot : {0, 1, -1}) {
    scene.isolate(slot); scene.order(false);
    Frame a, reversed, changed, back;
    if (!draw(a, "OIT authored stereo map first apply")) { ok = false; continue; }
    scene.order(true); ok &= draw(reversed, "OIT reversed traversal") && exactComposition(a, reversed);
    scene.order(false);
    scene.materials[1]->transparency = .5f;
    ok &= draw(changed, "OIT independently changed second opacity");
    if (!cpu) ok &= check(compositionDifference(a, changed) > 500, "OIT independent opacity is visibly consumed");
    scene.materials[1]->transparency = .75f;
    ok &= draw(back, "OIT opacity restored") && exactComposition(a, back);
    for (size_t camera = 0; camera < scene.cameras.size(); ++camera) {
      const auto mode = scene.cameras[camera]->getStereoMode();
      scene.cameras[camera]->setStereoMode(mode == SoCamera::LEFT_VIEW ? SoCamera::RIGHT_VIEW : SoCamera::LEFT_VIEW);
      ok &= draw(changed, "OIT authorial map eye changed");
      if (!cpu && (slot < 0 || int(camera) == slot))
        ok &= check(compositionDifference(a, changed) > 16, "authorial map eye has a distinct visible contribution");
      scene.cameras[camera]->setStereoMode(mode);
      ok &= draw(back, "OIT map eye restored") && exactComposition(a, back);
      const float nearValue = scene.cameras[camera]->nearDistance.getValue();
      scene.cameras[camera]->nearDistance = std::numeric_limits<float>::quiet_NaN();
      ok &= h.refuse(scene.root, back);
      scene.cameras[camera]->nearDistance = nearValue;
      ok &= draw(back, "OIT camera first valid recovery") && exactComposition(a, back);
    }
  }
  return ok;
}
