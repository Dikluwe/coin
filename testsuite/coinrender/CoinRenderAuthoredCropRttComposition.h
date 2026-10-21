// Four independently sized maps cannot be authored by one group precision.
// Each group therefore lights a separate quadrant of the same mono producer.
struct CompositionCropScene {
  SoSeparator *root = new SoSeparator;
  std::array<std::unique_ptr<CropSetup>, 4> slots;
  bool outside = false;
  CompositionCropScene() {
    root->ref();
    auto *observer = new SoOrthographicCamera;
    observer->position.setValue(0, 0, 6); observer->height = 4;
    observer->nearDistance = 1; observer->farDistance = 20; root->addChild(observer);
    auto *lighting = new SoLightModel; lighting->model = SoLightModel::PHONG; root->addChild(lighting);
    const int sizes[] = {64, 128, 256, 512};
    for (unsigned i = 0; i < slots.size(); ++i) {
      slots[i].reset(new CropSetup(1, 2, sizes[i], i % 2 ? .75 : 1.5,
                                   i % 2 ? 2 : 1, i % 2, 0, 0));
      auto &s = *slots[i];
      if (i == 0 || i == 3) s.setCasterSize(1.2, 1.8);
      s.scene.group->epsilon = 0; s.scene.group->threshold = 0;
      s.casterX = i % 2 ? .45 : -.45;
      s.move->translation.setValue(float(s.casterX), 0, .35f);
      auto *receiver = static_cast<SoSeparator *>(s.scene.group->getChild(2));
      SoCoordinate3 *coordinates = nullptr;
      for (int j = 0; j < receiver->getNumChildren(); ++j)
        if (receiver->getChild(j)->isOfType(SoCoordinate3::getClassTypeId()))
          coordinates = static_cast<SoCoordinate3 *>(receiver->getChild(j));
      check(coordinates != nullptr, "authored crop receiver coordinates exist");
      const float left = i % 2 ? .1f : -1.7f, right = i % 2 ? 1.7f : -.1f;
      const float bottom = i / 2 ? .1f : -1.7f, top = i / 2 ? 1.7f : -.1f;
      const SbVec3f points[] = {SbVec3f(left, bottom, -1), SbVec3f(right, bottom, -1),
                               SbVec3f(right, top, -1), SbVec3f(left, top, -1)};
      if (coordinates) coordinates->point.setValues(0, 4, points);
      root->addChild(s.scene.group);
    }
  }
  ~CompositionCropScene() { root->unref(); }
  void moveOutside(bool value) {
    outside = value;
    for (size_t i = 0; i < slots.size(); ++i) {
      auto &s = *slots[i]; s.casterX = value ? 10 : i % 2 ? .45 : -.45;
      s.move->translation.setValue(float(s.casterX), 0, .35f);
    }
  }
};
bool cropCompositionDelivery(const CompositionCropScene &scene, const CoinRenderFramePlan &frame) {
  CoinRenderShadowPlan plan; std::string error;
  if (!check(coin_render_plan_shadows(frame, plan, error) && plan.passes.size() == 4,
             "four distinct crop maps reach Core: " + error)) return false;
  CoinWgpuFfiFrame ffi;
  if (!check(ffi.prepare(frame, side, side, error, false), "crop producer FFI: " + error)) return false;
  const auto &view = ffi.getView();
  const CoinWgpuShadowDraw *casters[] = {view.shadow_casters, view.shadow_casters_second,
      view.shadow_casters_third, view.shadow_casters_fourth};
  const uint64_t counts[] = {view.shadow_caster_count, view.shadow_caster_count_second,
      view.shadow_caster_count_third, view.shadow_caster_count_fourth};
  const uint32_t sizes[] = {view.shadow_map_size, view.shadow_map_size_second,
      view.shadow_map_size_third, view.shadow_map_size_fourth};
  bool ok = check(view.abi_version == COIN_WGPU_ABI_VERSION && view.extra_shadow_pass_count == 0,
                  "current four-map FFI layout");
  std::array<bool, 4> seen{};
  for (size_t pass = 0; pass < 4; ++pass) {
    if (!check(plan.passes[pass].lightSlot < frame.shadowLights.size(), "crop pass light identity")) return false;
    const auto &light = frame.shadowLights[plan.passes[pass].lightSlot];
    size_t slot = 4;
    for (size_t i = 0; i < 4; ++i)
      if (light.sourceRevision == scene.slots[i]->scene.directional->getNodeId()) slot = i;
    if (!check(slot < 4 && !seen[slot], "one independent pass per authored crop light")) return false;
    seen[slot] = true; const auto &s = *scene.slots[slot];
    const auto vp = s.projectionViewport(); const int n = s.viewport.mapSide;
    CropRect map; map.w = map.h = n; const auto scissor = cropIntersect(vp, map);
    ok &= check(light.mapSceneCaptured && light.customSceneNodeId == s.scene.maps[1]->getNodeId() &&
                light.enabled && light.shadowEligible && sizes[pass] == uint32_t(n) &&
                s.viewport.visits > 0 && s.viewport.dimensionsCorrect,
                "map source, actual dimensions and callback viewport stay independent");
    const SbMatrix expectedView = stereoTranslation(0, 0, -5);
    const SbMatrix projection(float(1 / (2 * s.aspect)), 0, 0, 0,
                              0, .5f, 0, 0, 0, 0, -2.f / 9, 0, 0, 0, -11.f / 9, 1);
    bool cameraFound = false;
    for (const auto &camera : frame.cameras)
      cameraFound |= !camera.isPerspective && camera.nearDistance == 1 && camera.farDistance == 10 &&
          stereoMatrixClose(camera.viewMatrix, expectedView) &&
          stereoMatrixClose(camera.projectionMatrixCoin, projection);
    ok &= check(cameraFound, "crop Core camera preserves independent authored orthographic projection");
    if (!check(casters[pass] && counts[pass], "crop pass carries real caster transport")) return false;
    for (uint64_t i = 0; i < counts[pass]; ++i) {
      const auto &packed = casters[pass][i];
      if (!check(packed.render_state_slot < frame.renderStates.size(), "crop caster render state exists")) return false;
      const auto &state = frame.renderStates[packed.render_state_slot];
      ok &= check(state.shadowMapCameraOverride && packed.reserved == 0 &&
                  packed.viewport[0] == int(scissor.x) && packed.viewport[1] == n - int(scissor.y + scissor.h) &&
                  packed.viewport[2] == int(scissor.w) && packed.viewport[3] == int(scissor.h),
                  "each crop caster keeps its own physical viewport/scissor");
      if (s.raster) continue; // Fixed pixel bitmap size is checked by the spatial image oracle.
      SbMatrix actualMV, actualMVP; actualMV.setValue(packed.model_view);
      actualMVP.setValue(packed.model_view_projection);
      const auto mv = stereoTranslation(s.casterX, 0, .35) * expectedView;
      ok &= check(stereoMatrixClose(actualMV, mv), "crop caster keeps authored model/view order");
      for (const auto &point : {SbVec3f(0, 0, 0), SbVec3f(.2f, -.1f, .06f), SbVec3f(-.17f, .13f, -.08f)}) {
        SbVec3f actual; actualMVP.multVecMatrix(point, actual);
        const double px = vp.x + (.5 + (point[0] + s.casterX) / (4 * s.aspect)) * vp.w;
        const double py = vp.y + (.5 + point[1] / 4.) * vp.h;
        const SbVec3f expected(float(2 * px / n - 1), float(2 * py / n - 1),
                               float((5 - .35 - point[2] - 1) / 9));
        ok &= check((actual - expected).length() < 1e-5,
                    "crop FFI homogeneous probes preserve authored viewport and clip depth");
      }
    }
  }
  return ok;
}
bool cropCompositionImage(const CompositionCropScene &scene, const Frame &frame) {
  unsigned lit[4]{}, shadow[4]{}, bad = 0; double maximum = 0;
  for (int y = 10; y < 118; ++y) for (int x = 10; x < 118; ++x) {
    const double wx = (x + .5 - 64) / 32., wy = (64 - y - .5) / 32.;
    if (std::abs(wx) < .15 || std::abs(wx) > 1.65 || std::abs(wy) < .15 || std::abs(wy) > 1.65) continue;
    const unsigned slot = (wy > 0 ? 2 : 0) + (wx > 0 ? 1 : 0);
    const auto &s = *scene.slots[slot]; const auto rect = s.footprint();
    const double mx = (.5 + wx / std::sqrt(96.)) * s.viewport.mapSide;
    const double my = (.5 + wy / std::sqrt(96.)) * s.viewport.mapSide;
    const bool blocked = rect.contains(mx, my, 2.1);
    if (!blocked && rect.contains(mx, my, -2.1)) continue;
    (blocked ? shadow[slot] : lit[slot])++;
    const double diffuse[] = {.72, .75, .78}; const size_t p = size_t(y * side + x);
    for (int c = 0; c < 3; ++c) {
      const double expected = 255 * (.036 + (blocked ? 0 : .6) * diffuse[c]);
      const double delta = std::abs(frame.rgba[p * 4 + c] - expected);
      maximum = std::max(maximum, delta); bad += delta > 3;
    }
    bad += frame.rgba[p * 4 + 3] != 255;
    bad += std::abs(frame.depth[p] - .25f) > 2e-5f;
  }
  bool ok = check(bad == 0, "independent quadrant RGBA, crop shadow and consumer depth");
  for (unsigned slot = 0; slot < 4; ++slot) {
    std::cout << "crop_rtt slot=" << slot << " lit=" << lit[slot] << " shadow=" << shadow[slot]
              << " max=" << maximum << " bad=" << bad << '\n';
    ok &= check(lit[slot] > 100 && (scene.outside ? shadow[slot] == 0 : shadow[slot] >= 4),
                "every distinct map has defined lit and shadow interior witnesses");
  }
  return ok;
}
bool r1(bool cpu, CoinRenderSceneTextureMode route) {
  CompositionRttHarness h(cpu, route); if (!h.target) return false;
  CompositionCropScene scene; CompositionConsumer consumer(scene.root);
  auto draw = [&](Frame &f, const char *label) {
    return h.draw(consumer, f, label) && cropCompositionDelivery(scene, h.rtt->frame) &&
           (cpu || cropCompositionImage(scene, f));
  };
  Frame a, b, back; bool ok = draw(a, "R1 distinct crop map dimensions");
  if (!ok) return false;
  scene.moveOutside(true);
  ok &= draw(b, "R1 all casters outside authored camera");
  if (!cpu) ok &= check(rttCompositionDifference(a, b) > 16, "real producer responds to all four map sources");
  scene.moveOutside(false);
  ok &= draw(back, "R1 casters restored") && exactComposition(a, back);
  for (auto &slot : scene.slots) {
    const int kind = slot->viewport.kind; slot->viewport.kind = 5;
    ok &= h.reject(consumer, back); slot->viewport.kind = kind;
    ok &= draw(back, "R1 first valid viewport recovery") && exactComposition(a, back);
    const float saved = slot->camera->nearDistance.getValue();
    slot->camera->nearDistance = std::numeric_limits<float>::quiet_NaN();
    ok &= h.reject(consumer, back); slot->camera->nearDistance = saved;
    ok &= draw(back, "R1 first valid map camera recovery") && exactComposition(a, back);
  }
  return ok;
}
