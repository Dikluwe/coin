// Included within authored_composition.
std::array<uint16_t, 1024> authoredBayer() {
  const unsigned seed[] = {0, 2, 3, 1};
  std::array<uint16_t, 1024> ranks{};
  for (unsigned i = 0; i < 4; ++i) ranks[i] = uint16_t(seed[i]);
  for (unsigned side = 2; side < 32; side *= 2) {
    std::array<uint16_t, 1024> next{};
    for (unsigned y = 0; y < 2 * side; ++y) for (unsigned x = 0; x < 2 * side; ++x)
      next[y * 2 * side + x] = uint16_t(4 * ranks[(y % side) * side + x % side] + seed[(y / side) * 2 + x / side]);
    ranks = next;
  }
  return ranks;
}
bool signedCompositionDelivery(const NearSlotScene &scene, const NearAudit &audit,
                  int targetSide = side) {
  if (scene.cropped)
    for (const auto &viewport : scene.authoredViewports)
      if (!check(viewport.visits > 0 && viewport.dimensionsCorrect,
                 "viewport callback observes real256 map dimensions"))
        return false;
  const auto &frame = audit.frame;
  bool eightBindings = false;
  for (const auto &state : frame.renderStates) {
    bool all = true;
    for (size_t unit = 0; unit < 8; ++unit)
      all &= coin_render_texture_unit_enabled(state, unit);
    eightBindings |= all;
  }
  if (!check(!scene.generalEnabled || eightBindings,
             "general receiver retains all eight independently enabled texture "
             "units"))
    return false;
  CoinRenderShadowPlan plan;
  std::string error;
  if (!check(coin_render_plan_shadows(frame, plan, error) &&
                 plan.passes.size() == scene.lights.size() &&
                 frame.shadowLights.size() == scene.lights.size(),
             "every enabled map survives Core planning, including zero "
             "intensities " +
                 error))
    return false;
  CoinWgpuFfiFrame ffi;
  if (!check(ffi.prepare(frame, targetSide, targetSide, error, false),
             "signed camera private transport " + error))
    return false;
  const auto &view = ffi.getView();
  if (scene.generalEnabled) {
    bool transported = false;
    for (size_t stateIndex = 0; stateIndex < frame.renderStates.size();
         ++stateIndex) {
      bool all = true;
      for (size_t unit = 0; unit < 8; ++unit)
        all &= coin_render_texture_unit_enabled(frame.renderStates[stateIndex],
                                                unit);
      if (!all)
        continue;
      if (!check(view.states && stateIndex < view.state_count,
                 "eight-unit FFI state addressable"))
        return false;
      const auto &packed = view.states[stateIndex];
      transported = true;
      for (size_t unit = 0; unit < 8; ++unit) {
        const bool enabled =
            unit ? packed.extra_textures[unit - 1].enabled : packed.has_texture;
        const uint32_t imageSlot =
            unit ? packed.extra_textures[unit - 1].texture_slot
                 : packed.texture_slot;
        const uint32_t samplerSlot =
            unit ? packed.extra_textures[unit - 1].sampler_slot
                 : packed.sampler_slot;
        if (!check(enabled && imageSlot < view.texture_count &&
                       samplerSlot < view.sampler_count,
                   "each independent texture/sampler binding survives FFI"))
          return false;
        const auto &image = view.textures[imageSlot];
        SbVec2s authoredSize;
        int components = 0;
        const auto *authored =
            scene.unitImages[unit]->image.getValue(authoredSize, components);
        if (!check(image.pixels && image.pixel_bytes_len >= 4 &&
                       components == 4 && image.pixels[0] == authored[0] &&
                       image.pixels[1] == authored[1] &&
                       image.pixels[2] == authored[2] &&
                       image.pixels[3] == authored[3],
                   "FFI unit image identity/content matches its authorial "
                   "binding"))
          return false;
      }
    }
    if (!check(transported,
               "general receiver transported all eight bound units"))
      return false;
  }
  const size_t count = scene.lights.size();
  bool ok = check(view.abi_version == COIN_WGPU_ABI_VERSION && view.shadow_map_size == 256 &&
                      view.shadow_map_size_second == 256 &&
                      view.shadow_map_size_third == 256 &&
                      view.shadow_map_size_fourth == 256 &&
                      view.extra_shadow_pass_count == count - 4,
                  "current ABI retains all four base slots and extra slots5..8");
  for (size_t pass = 0; pass < count; ++pass) {
    const auto &core = plan.passes[pass];
    if (!check(core.lightSlot < frame.shadowLights.size(),
               "Core pass light index valid"))
      return false;
    const auto &light = frame.shadowLights[core.lightSlot];
    size_t slot = count;
    for (size_t i = 0; i < count; ++i)
      if (scene.lights[i]->getNodeId() == light.sourceRevision)
        slot = i;
    if (!check(slot < count && light.enabled && light.shadowEligible &&
                   light.mapSceneCaptured && light.groupSlot == 1,
               "per-map source identity and enabled state retained"))
      return false;
    ok &= check(
        std::abs(light.intensity - scene.lights[slot]->intensity.getValue()) <
                1e-7 &&
            light.customSceneNodeId == scene.maps[slot]->getNodeId(),
        "map/light intensity and current authorial source identity retained");
    const double nearValue = scene.cameras[slot]->nearDistance.getValue();
    const double farValue = scene.cameras[slot]->farDistance.getValue();
    const auto position = scene.cameras[slot]->position.getValue();
    const double height = scene.cameras[slot]->height.getValue();
    const double aspect = scene.cameras[slot]->aspectRatio.getValue();
    const auto vp = scene.mapViewport(slot);
    bool cameraWitness = false;
    for (const auto &camera : frame.cameras) {
      SbVec3f local;
      camera.viewMatrix.multVecMatrix(SbVec3f(0, 0, 6), local);
      if (!camera.isPerspective &&
          std::abs(camera.nearDistance - nearValue) < 1e-6 &&
          std::abs(camera.farDistance - farValue) < 1e-6 &&
          std::abs(local[0] + position[0]) < 1e-5 &&
          std::abs(local[1] + position[1]) < 1e-5 &&
          std::abs(local[2] - 1) < 1e-5) {
        SbVec3f clip;
        camera.projectionMatrixCoin.multVecMatrix(local, clip);
        cameraWitness |=
            std::abs(clip[0] + 2 * position[0] / (height * aspect)) < 1e-5 &&
            std::abs(clip[1] + 2 * position[1] / height) < 1e-5 &&
            std::abs(clip[2] - (2 * (-1 - nearValue) / (farValue - nearValue) -
                                1)) < 1e-5;
      }
    }
    ok &= check(cameraWitness, "Core preserves signed near/far and distinct "
                               "orthographic matrices for slot " +
                                   std::to_string(slot));
    const CoinWgpuShadowDraw *casters = nullptr;
    uint64_t casterCount = 0;
    if (pass == 0) {
      casters = view.shadow_casters;
      casterCount = view.shadow_caster_count;
    } else if (pass == 1) {
      casters = view.shadow_casters_second;
      casterCount = view.shadow_caster_count_second;
    } else if (pass == 2) {
      casters = view.shadow_casters_third;
      casterCount = view.shadow_caster_count_third;
    } else if (pass == 3) {
      casters = view.shadow_casters_fourth;
      casterCount = view.shadow_caster_count_fourth;
    } else {
      if (!check(view.extra_shadow_passes &&
                     pass - 4 < view.extra_shadow_pass_count,
                 "extra shadow slot addressable"))
        return false;
      casters = view.extra_shadow_passes[pass - 4].casters;
      casterCount = view.extra_shadow_passes[pass - 4].caster_count;
      ok &= check(view.extra_shadow_passes[pass - 4].map_size == 256,
                  "extra map physical extent retained");
    }
    if (!scene.raster) {
      ok &= check(casterCount && casters,
                  "geometry caster delivered for every map");
      for (uint64_t draw = 0; draw < casterCount; ++draw) {
        const auto &caster = casters[draw];
        SbMatrix mvp, modelView;
        mvp.setValue(caster.model_view_projection);
        modelView.setValue(caster.model_view);
        const auto casterPosition = scene.moves[slot]->translation.getValue();
        const double z = casterPosition[2];
        for (const SbVec3f &probe : {SbVec3f(0, 0, 0), SbVec3f(.2f, -.1f, .05f),
                                     SbVec3f(-.17f, .13f, -.07f)}) {
          SbVec3f mv, clip;
          modelView.multVecMatrix(probe, mv);
          mvp.multVecMatrix(probe, clip);
          const double distance = 5 - z - probe[2];
          ok &= check(
              std::abs(mv[0] - (probe[0] + casterPosition[0] - position[0])) <
                      1e-5 &&
                  std::abs(mv[1] - (probe[1] + casterPosition[1] -
                                    position[1])) < 1e-5 &&
                  std::abs(mv[2] - (z + probe[2] - 5)) < 1e-5 &&
                  std::abs(clip[0] -
                           (2 *
                                (vp.x + (.5 + (probe[0] + casterPosition[0] -
                                               position[0]) /
                                                  (height * aspect)) *
                                            vp.w) /
                                256 -
                            1)) < 1e-5 &&
                  std::abs(clip[1] -
                           (2 *
                                (vp.y + (.5 + (probe[1] + casterPosition[1] -
                                               position[1]) /
                                                  height) *
                                            vp.h) /
                                256 -
                            1)) < 1e-5 &&
                  std::abs(clip[2] - (distance - nearValue) /
                                         (farValue - nearValue)) < 1e-5,
              "FFI per-caster MVP preserves original signed near and "
              "authored camera");
        }
      }
    }
    for (uint64_t draw = 0; draw < casterCount; ++draw)
      ok &= check(casters[draw].reserved == 0 &&
                      casters[draw].viewport[0] == int(vp.x) &&
                      casters[draw].viewport[1] == 256 - int(vp.y + vp.h) &&
                      casters[draw].viewport[2] == int(vp.w) &&
                      casters[draw].viewport[3] == int(vp.h),
                  "every slot retains authorial current ABI scissor and zero flags");
  }
  return ok;
}


bool screenOracle(const NearSlotScene &scene, const Frame &frame, float transparency) {
  const auto ranks = authoredBayer();
  unsigned kept = 0, holes = 0, shadows = 0, lit = 0, bad = 0, tileKept = 0;
  double maximum = 0;
  const unsigned threshold = unsigned(transparency * 64) * 16;
  for (int y = 12; y < side - 12; ++y) for (int x = 12; x < side - 12; ++x) {
    const bool keep = ranks[((side - 1 - y) % 32) * 32 + x % 32] >= threshold;
    const double wx = (x + .5 - side / 2.) * 4 / side, wy = (side / 2. - y - .5) * 4 / side;
    const double mx = (.5 + wx / std::sqrt(96.)) * 256, my = (.5 + wy / std::sqrt(96.)) * 256;
    double intensity = 0; bool measurable = true, shadow = false;
    for (size_t slot = 0; slot < scene.lights.size(); ++slot) {
      const double strength = scene.lights[slot]->intensity.getValue(); if (!strength) continue;
      const bool active = nearCasterAdmitted(scene, slot);
      const auto footprint = nearFootprint(scene, slot);
      const bool hit = active && footprint.inside(mx, my, 2.1);
      const bool clear = !active || !footprint.inside(mx, my, -2.1);
      if (!hit && !clear) { measurable = false; break; }
      shadow |= hit; if (!hit) intensity += strength;
    }
    const size_t pixel = size_t(y * side + x);
    if (x >= 32 && x < 96 && y >= 32 && y < 96) tileKept += frame.rgba[4 * pixel + 3] == 255;
    // Coverage, alpha and depth have an exact oracle even at a map boundary.
    bad += frame.rgba[4 * pixel + 3] != (keep ? 255 : 0);
    bad += std::abs(frame.depth[pixel] - (keep ? float(6. / 19) : 1.f)) > 1e-5f;
    kept += keep; holes += !keep;
    if (!measurable && keep) continue;
    shadows += keep && shadow; lit += keep && !shadow;
    const double diffuse[] = {.72, .75, .78};
    for (int c = 0; c < 3; ++c) {
      const double expected = keep ? 255 * (.036 + diffuse[c] * intensity) : 0;
      const double error = std::abs(double(frame.rgba[4 * pixel + c]) - expected);
      maximum = std::max(maximum, error); bad += error > 3;
    }
  }
  bool expectsShadow = false;
  for (size_t slot = 0; slot < scene.lights.size(); ++slot)
    expectsShadow |= scene.lights[slot]->intensity.getValue() > 0 && nearCasterAdmitted(scene, slot);
  const unsigned expectedTile = 4 * (1024 - threshold);
  std::cout << "screen_composition kept=" << kept << " holes=" << holes << " shadowed=" << shadows
            << " lit=" << lit << " tile_kept=" << tileKept << " expected_tile=" << expectedTile
            << " bad=" << bad << " max=" << maximum << '\n';
  return check(kept > 500 && holes > 500 && lit > 200 && (!expectsShadow || shadows >= 4) &&
               tileKept == expectedTile && bad == 0,
               "authored32x32 SCREEN_DOOR rank, exact coverage/alpha, signed camera shadow and depth");
}
bool t3(bool cpu) {
  CompositionHarness h(cpu); if (!h.target) return false;
  h.action.setTransparencyType(CoinRenderAction::SCREEN_DOOR);
  NearSlotScene scene(8, 0, false); scene.mixedNears();
  auto *receiver = static_cast<SoSeparator *>(scene.group->getChild(scene.group->getNumChildren() - 1));
  auto *material = static_cast<SoMaterial *>(receiver->getChild(1));
  material->transparency = .5f;
  bool ok = true;
  auto draw = [&](Frame &f, const char *phase) {
    const auto authored = nearAuthorialInputs(scene);
    return h.render(scene.root, f, phase) && signedCompositionDelivery(scene, *h.audit) &&
           check(nearAuthorialInputs(scene) == authored, "SCREEN_DOOR preserves signed authorial cameras") &&
           (cpu || screenOracle(scene, f, material->transparency[0]));
  };
  for (int isolated : {0, 1, 2, 3, 4, 5, 6, 7, -1}) {
    if (isolated < 0) scene.allContribute(); else scene.isolate(isolated);
    Frame a, b, back;
    if (!draw(a, "T3 front caster half coverage")) { ok = false; continue; }
    for (auto *move : scene.moves) { auto p = move->translation.getValue(); p[2] = 6; move->translation = p; }
    ok &= draw(b, "T3 caster behind authorial map eye");
    for (auto *move : scene.moves) { auto p = move->translation.getValue(); p[2] = .35f; move->translation = p; }
    ok &= draw(back, "T3 caster first valid restored position") && exactComposition(a, back);
    material->transparency = .25f;
    ok &= draw(b, "T3 three-quarter authored coverage");
    material->transparency = .5f;
    ok &= draw(back, "T3 half coverage restored") && exactComposition(a, back);
    const size_t slot = isolated < 0 ? 7 : size_t(isolated);
    const float farValue = scene.cameras[slot]->farDistance.getValue();
    scene.cameras[slot]->farDistance = scene.cameras[slot]->nearDistance.getValue();
    ok &= h.refuse(scene.root, back);
    scene.cameras[slot]->farDistance = farValue;
    ok &= draw(back, "T3 degenerate camera first valid recovery") && exactComposition(a, back);
  }
  return ok;
}
