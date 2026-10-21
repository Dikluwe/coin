// Independent authorial camera and image oracles for signed near in all maps.
class NearAudit : public CoinRenderBackend {
public:
  std::unique_ptr<CoinRenderBackend> real;
  CoinRenderFramePlan frame;
  unsigned submissions = 0, producers = 0;
  explicit NearAudit(std::unique_ptr<CoinRenderBackend> backend)
      : real(std::move(backend)) {}
  bool isGpuBackend() const override { return real->isGpuBackend(); }
  bool initializesCpuDepthBuffer() const override {
    return real->initializesCpuDepthBuffer();
  }
  bool resetOnActionDetach() const override {
    return real->resetOnActionDetach();
  }
  bool supportsOffscreenShadows() const override {
    return real->supportsOffscreenShadows();
  }
  bool supportsOpaqueBump() const override {
    return real->supportsOpaqueBump();
  }
  CoinRenderBackendStatus getStatus() const override {
    return real->getStatus();
  }
  CoinRenderBackendStatus prepare(CoinRenderTargetP &target) override {
    return real->prepare(target);
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &input,
                                CoinRenderTargetP &target) override {
    frame = input;
    ++submissions;
    return real->submit(input, target);
  }
  CoinRenderSubmitResult
  submit(const CoinRenderFramePlan &input, CoinRenderTargetP &target,
         const CoinRenderFrameReuseDecision &reuse) override {
    frame = input;
    ++submissions;
    return real->submit(input, target, reuse);
  }
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan &graph,
                                      const CoinRenderFramePlan &input,
                                      const SbVec2i32 &size) const override {
    return real->preflightRtt(graph, input, size);
  }
  CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan &input,
                                   const SbVec2i32 &size, uint64_t id,
                                   CoinRenderTargetP &target,
                                   uint64_t &token) override {
    ++producers;
    return real->submitRtt(input, size, id, target, token);
  }
  void finishRtt(const std::vector<uint64_t> &tokens) override {
    real->finishRtt(tokens);
  }
  bool stagedRttRequiresPreparedParent() const override {
    return real->stagedRttRequiresPreparedParent();
  }
  bool requiresSharedRetirement() const override {
    return real->requiresSharedRetirement();
  }
  void retireLostReadbacks() override { real->retireLostReadbacks(); }
  CoinRenderDeviceDomain resourceDomain() const override {
    return real->resourceDomain();
  }
  bool readbackLoad(uint64_t &count, uint64_t &bytes) const override {
    return real->readbackLoad(count, bytes);
  }
  void poll() override { real->poll(); }
  const std::string &getLastError() const override {
    return real->getLastError();
  }
};

struct NearSlotScene {
  SoSeparator *root = new SoSeparator;
  SoShadowGroup *group = new SoShadowGroup;
  std::vector<SoShadowDirectionalLight *> lights;
  std::vector<SoSeparator *> maps;
  std::vector<SoOrthographicCamera *> cameras;
  std::vector<SoSwitch *> switches;
  std::vector<SoTranslation *> moves;
  std::vector<SoTexture2 *> unitImages;
  SoCoordinate3 *receiverCoordinates = nullptr;
  std::vector<CropViewport> authoredViewports;
  bool cropped = false, coverage = false, scissorCrossing = false;
  double textureProduct[3] = {1, 1, 1};
  int raster;
  bool generalEnabled;
  explicit NearSlotScene(int count, int casterKind, bool general)
      : raster(casterKind), generalEnabled(general) {
    root->ref();
    auto *observer = new SoOrthographicCamera;
    observer->position.setValue(0, 0, 6);
    observer->height = 4;
    observer->nearDistance = 1;
    observer->farDistance = 20;
    root->addChild(observer);
    auto *lighting = new SoLightModel;
    lighting->model = SoLightModel::PHONG;
    root->addChild(lighting);
    group->quality = .2f;
    group->precision = .125f;
    root->addChild(group);
    for (int slot = 0; slot < count; ++slot) {
      auto *light = new SoShadowDirectionalLight;
      light->direction.setValue(0, 0, -1);
      light->bboxCenter.setValue(0, 0, 0);
      light->bboxSize.setValue(4, 4, 8);
      light->intensity = 0;
      light->on = TRUE;
      auto *map = new SoSeparator;
      auto *camera = new SoOrthographicCamera;
      camera->position.setValue(float(cameraX(slot, count)),
                                float(cameraY(slot)), 5);
      camera->height = float(cameraHeight(slot));
      camera->aspectRatio = 1;
      camera->nearDistance = 1;
      camera->farDistance = float(cameraFar(slot));
      map->addChild(camera);
      auto *visible = new SoSwitch;
      visible->whichChild = SO_SWITCH_ALL;
      map->addChild(visible);
      auto *caster = new SoSeparator;
      auto *style = new SoShadowStyle;
      style->style = SoShadowStyle::CASTS_SHADOW;
      caster->addChild(style);
      auto *move = new SoTranslation;
      move->translation.setValue(0, 0, .35f);
      caster->addChild(move);
      if (!raster) {
        auto *cube = new SoCube;
        cube->width = .8f;
        cube->height = .8f;
        cube->depth = .2f;
        caster->addChild(cube);
      } else if (raster == 1) {
        auto *image = new SoImage;
        image->horAlignment = SoImage::CENTER;
        image->vertAlignment = SoImage::HALF;
        const std::vector<uint8_t> pixels(25 * 25 * 4, 255);
        image->image.setValue(SbVec2s(25, 25), 4, pixels.data());
        caster->addChild(image);
      } else {
        auto *coordinates = new SoCoordinate3;
        coordinates->point.setValue(0, 0, 0);
        caster->addChild(coordinates);
        auto *marker = new SoMarkerSet;
        marker->markerIndex = SoMarkerSet::SQUARE_FILLED_9_9;
        caster->addChild(marker);
      }
      visible->addChild(caster);
      light->shadowMapScene = map;
      group->addChild(light);
      lights.push_back(light);
      maps.push_back(map);
      cameras.push_back(camera);
      switches.push_back(visible);
      moves.push_back(move);
    }
    auto *receiver = new SoSeparator;
    auto *style = new SoShadowStyle;
    style->style = SoShadowStyle::SHADOWED;
    receiver->addChild(style);
    if (general) {
      auto *quality = new SoComplexity;
      quality->textureQuality = .1f;
      receiver->addChild(quality);
      for (int unit = 0; unit < 8; ++unit) {
        auto *select = new SoTextureUnit;
        select->unit = unit;
        receiver->addChild(select);
        auto *texture = new SoTexture2;
        const uint8_t white[] = {255, 255, 255, 255};
        texture->image.setValue(SbVec2s(1, 1), 4, white);
        receiver->addChild(texture);
        unitImages.push_back(texture);
        auto *coordinates = new SoTextureCoordinate2;
        const SbVec2f uv[] = {SbVec2f(0, 0), SbVec2f(1, 0), SbVec2f(1, 1),
                              SbVec2f(0, 1)};
        coordinates->point.setValues(0, 4, uv);
        receiver->addChild(coordinates);
      }
      auto *zero = new SoTextureUnit;
      zero->unit = 0;
      receiver->addChild(zero);
    }
    plane(receiver);
    receiverCoordinates = static_cast<SoCoordinate3 *>(
        receiver->getChild(receiver->getNumChildren() - 2));
    group->addChild(receiver);
  }
  void cropViewports() {
    cropped = true;
    authoredViewports.resize(lights.size());
    for (size_t i = 0; i < lights.size(); ++i) {
      auto &v = authoredViewports[i];
      v.mapSide = 256;
      v.kind = i % 2 ? 2 : 1;
      v.left = .125;
      v.bottom = .125;
      v.width = .75;
      v.height = .75;
      cameras[i]->aspectRatio = i % 2 ? .51f : 2.03f;
      cameras[i]->viewportMapping = SoCamera::CROP_VIEWPORT_NO_FRAME;
      maps[i]->insertChild(v.node(), i % 2 ? 1 : 0);
    }
  }
  void crossScissorBoundary() {
    cropViewports();
    scissorCrossing = true;
    for (size_t i = 0; i < lights.size(); ++i) {
      auto &v = authoredViewports[i];
      v.kind = 8;
      v.left = 112;
      v.bottom = 80;
      v.width = 80;
      v.height = 96;
      cameras[i]->aspectRatio = 1;
      const auto position = cameras[i]->position.getValue();
      // The cube has a strictly covered band on BOTH sides of the left
      // scissor edge. Its center remains inside the original frustum.
      moves[i]->translation.setValue(
          position[0] - cameras[i]->height.getValue() / 2 + .1f, position[1],
          .35f);
    }
  }
  CropRect mapViewport(size_t slot) const {
    CropRect full;
    full.w = full.h = 256;
    if (!cropped)
      return full;
    const auto &v = authoredViewports[slot];
    return slot % 2
               ? cropPixelViewport(cropAuthoredViewport(v))
               : cropCameraViewport(cropAuthoredViewport(v),
                                    cameras[slot]->aspectRatio.getValue(), 256);
  }
  void colorUnit(size_t unit, bool changed) {
    const uint8_t white[] = {255, 255, 255, 255},
                  color[] = {128, 191, 223, 255};
    unitImages[unit]->image.setValue(SbVec2s(1, 1), 4, changed ? color : white);
    for (int c = 0; c < 3; ++c)
      textureProduct[c] = changed ? double(color[c]) / 255 : 1;
  }
  void slopedReceiver() {
    coverage = true;
    const SbVec3f quad[] = {
        SbVec3f(-1.83f, -1.79f, -1), SbVec3f(1.73f, -1.69f, -1),
        SbVec3f(1.61f, 1.81f, -1), SbVec3f(-1.71f, 1.57f, -1)};
    receiverCoordinates->point.setValues(0, 4, quad);
  }
  bool receiverInterior(double x, double y, double margin) const {
    if (!coverage)
      return true;
    for (int i = 0; i < 4; ++i) {
      const auto a = receiverCoordinates->point[i],
                 b = receiverCoordinates->point[(i + 1) % 4];
      const double dx = b[0] - a[0], dy = b[1] - a[1];
      if (dx * (y - a[1]) - dy * (x - a[0]) <=
          margin * std::sqrt(dx * dx + dy * dy))
        return false;
    }
    return true;
  }
  ~NearSlotScene() { root->unref(); }
  static double cameraX(int slot, int count) {
    return -.30 + .60 * slot / (count - 1);
  }
  static double cameraY(int slot) { return -.12 + .04 * (slot % 4); }
  static double cameraHeight(int slot) { return 4 + .12 * slot; }
  static double cameraFar(int slot) { return 10 + .2 * slot; }
  void isolate(int slot) {
    for (size_t i = 0; i < lights.size(); ++i)
      lights[i]->intensity = int(i) == slot ? .55f : 0.f;
  }
  void allContribute() {
    for (auto *light : lights)
      light->intensity = .45f / float(lights.size());
  }
  void mixedNears() {
    for (size_t i = 0; i < cameras.size(); ++i)
      cameras[i]->nearDistance = i % 3 == 0 ? -2.f : i % 3 == 1 ? 0.f : 1.f;
  }
};

// The fitted directional lookup uses the bbox's circumscribed sphere, width
// sqrt(4^2+4^2+8^2). The authored map camera keeps its own orthographic height.
// Everything here is in bottom-left map pixels; readback is top-down.
struct NearFootprint {
  double left, bottom, right, top;
  bool inside(double x, double y, double margin) const {
    return x > left + margin && x < right - margin && y > bottom + margin &&
           y < top - margin;
  }
};
NearFootprint nearFootprint(const NearSlotScene &scene, size_t slot,
                            bool clippedToViewport = true) {
  const auto vp = scene.mapViewport(slot);
  const auto position = scene.cameras[slot]->position.getValue();
  const double height = scene.cameras[slot]->height.getValue();
  const double aspect = scene.cameras[slot]->aspectRatio.getValue();
  const auto caster = scene.moves[slot]->translation.getValue();
  const double x =
      vp.x + (.5 + (caster[0] - position[0]) / (height * aspect)) * vp.w;
  const double y = vp.y + (.5 + (caster[1] - position[1]) / height) * vp.h;
  CropRect shape;
  if (!scene.raster) {
    shape.x = x - .4 / (height * aspect) * vp.w;
    shape.y = y - .4 / height * vp.h;
    shape.w = .8 / (height * aspect) * vp.w;
    shape.h = .8 / height * vp.h;
  } else {
    const double half = scene.raster == 1 ? 12 : 4,
                 width = scene.raster == 1 ? 25 : 9;
    shape.x = x - half;
    shape.y = y - half;
    shape.w = shape.h = width;
  }
  CropRect map;
  map.w = map.h = 256;
  const auto clipped =
      clippedToViewport ? cropIntersect(cropIntersect(shape, vp), map) : shape;
  return NearFootprint{clipped.x, clipped.y, clipped.x + clipped.w,
                       clipped.y + clipped.h};
}
bool nearCasterAdmitted(const NearSlotScene &scene, size_t slot) {
  const double distance = 5 - scene.moves[slot]->translation.getValue()[2];
  const double extent = scene.raster ? 0 : .1;
  return distance + extent >= scene.cameras[slot]->nearDistance.getValue() &&
         distance - extent <= scene.cameras[slot]->farDistance.getValue();
}
bool nearImageOracle(const NearSlotScene &scene, const Frame &frame,
                     const std::string &label, bool depthRequired = true,
                     int extent = side, bool report = true) {
  if (!check(
          frame.rgba.size() == size_t(extent * extent * 4) &&
              (!depthRequired || frame.depth.size() == size_t(extent * extent)),
          label + " complete buffers"))
    return false;
  unsigned interior = 0, exterior = 0, bad = 0, forbidden = 0;
  int maximum = 0;
  for (int y = extent * 12 / side; y < extent - extent * 12 / side; ++y)
    for (int x = extent * 12 / side; x < extent - extent * 12 / side; ++x) {
      const double wx = (x + .5 - extent / 2.) * 4 / extent;
      const double wy = (extent / 2. - y - .5) * 4 / extent;
      if (!scene.receiverInterior(wx, wy, 8. / extent))
        continue;
      const double mx = (.5 + wx / std::sqrt(96.)) * 256;
      const double my = (.5 + wy / std::sqrt(96.)) * 256;
      double intensity = 0;
      bool measurable = true, shadow = false;
      for (size_t slot = 0; slot < scene.lights.size(); ++slot) {
        const double strength = scene.lights[slot]->intensity.getValue();
        if (!strength)
          continue;
        const auto footprint = nearFootprint(scene, slot);
        const bool active =
            scene.switches[slot]->whichChild.getValue() != SO_SWITCH_NONE &&
            nearCasterAdmitted(scene, slot);
        const bool inside = active && footprint.inside(mx, my, 2.1);
        const bool outside = !active || !footprint.inside(mx, my, -2.1);
        if (!inside && !outside) {
          measurable = false;
          break;
        }
        shadow |= inside;
        if (!inside)
          intensity += strength;
      }
      if (!measurable)
        continue;
      interior += shadow;
      exterior += !shadow;
      if (!shadow && scene.scissorCrossing) {
        bool rawCovered = false;
        for (size_t slot = 0; slot < scene.lights.size(); ++slot)
          if (scene.lights[slot]->intensity.getValue() > 0 &&
              nearCasterAdmitted(scene, slot))
            rawCovered |= nearFootprint(scene, slot, false).inside(mx, my, 2.1);
        forbidden += rawCovered;
      }
      const double diffuse[] = {.72, .75, .78};
      const size_t pixel = size_t(y * extent + x);
      for (int c = 0; c < 3; ++c) {
        const int expected = int(std::lround(
            255 * (.036 + diffuse[c] * intensity) * scene.textureProduct[c]));
        const int error = std::abs(int(frame.rgba[4 * pixel + c]) - expected);
        maximum = std::max(maximum, error);
        if (error > 3 && bad++ < 4 && report)
          std::cerr << "near_slots_first " << label << " xy=" << x << ',' << y
                    << " expected=" << expected
                    << " actual=" << unsigned(frame.rgba[4 * pixel + c])
                    << '\n';
      }
      bad += frame.rgba[4 * pixel + 3] != 255;
      if (depthRequired)
        bad += !std::isfinite(frame.depth[pixel]) ||
               std::abs(frame.depth[pixel] - float(6. / 19)) > 1e-5;
    }
  bool expectedShadow = false;
  for (size_t slot = 0; slot < scene.lights.size(); ++slot)
    expectedShadow |=
        scene.lights[slot]->intensity.getValue() > 0 &&
        scene.switches[slot]->whichChild.getValue() != SO_SWITCH_NONE &&
        nearCasterAdmitted(scene, slot);
  if (report)
    std::cout << "near_slots_image " << label << " interior=" << interior
              << " exterior=" << exterior << " bad=" << bad
              << " max=" << maximum << " forbidden=" << forbidden << '\n';
  const bool valid =
      (!expectedShadow || interior >= 4) && exterior > 500 && bad == 0 &&
      (!scene.scissorCrossing || !expectedShadow || forbidden > 4);
  return report ? check(valid, label + " independent signed clipping, spatial "
                                       "footprint, RGB and depth")
                : valid;
}

// These are the parameters this fixture mutates. Revisions are intentionally
// absent: delivery checks the current source identity after each apply.
std::vector<double> nearAuthorialInputs(const NearSlotScene &s,
                                        int extent = side) {
  std::vector<double> v{double(extent),
                        double(s.raster),
                        double(s.generalEnabled),
                        double(s.cropped),
                        double(s.coverage),
                        double(s.scissorCrossing),
                        s.group->quality.getValue(),
                        s.group->precision.getValue()};
  for (size_t i = 0; i < s.lights.size(); ++i) {
    auto *c = s.cameras[i];
    auto *l = s.lights[i];
    const int mapping = c->viewportMapping.getValue();
    v.insert(v.end(), {double(l->on.getValue()), l->intensity.getValue(),
                       c->nearDistance.getValue(), c->farDistance.getValue(),
                       c->height.getValue(), c->aspectRatio.getValue(),
                       c->focalDistance.getValue(),
                       double(mapping == SoCamera::CROP_VIEWPORT_FILL_FRAME ||
                                      mapping == SoCamera::CROP_VIEWPORT_LINE_FRAME
                                  ? SoCamera::CROP_VIEWPORT_NO_FRAME
                                  : mapping),
                       double(s.switches[i]->whichChild.getValue())});
    for (int k = 0; k < 3; ++k) {
      v.push_back(c->position.getValue()[k]);
      v.push_back(s.moves[i]->translation.getValue()[k]);
      v.push_back(l->direction.getValue()[k]);
      v.push_back(l->color.getValue()[k]);
    }
    const float *rotation = c->orientation.getValue().getValue();
    for (int k = 0; k < 4; ++k)
      v.push_back(rotation[k]);
  }
  // Occurrence order is authorial state too, independently of revision IDs.
  for (int child = 0; child < s.group->getNumChildren(); ++child) {
    int lightIndex = -1;
    for (size_t i = 0; i < s.lights.size(); ++i)
      if (s.group->getChild(child) == s.lights[i])
        lightIndex = int(i);
    v.push_back(lightIndex);
  }
  for (const auto &vp : s.authoredViewports)
    v.insert(v.end(), {double(vp.kind), double(vp.mapSide), vp.left, vp.bottom,
                       vp.width, vp.height});
  for (int i = 0; i < s.receiverCoordinates->point.getNum(); ++i)
    for (int k = 0; k < 3; ++k)
      v.push_back(s.receiverCoordinates->point[i][k]);
  for (auto *image : s.unitImages) {
    SbVec2s size;
    int components = 0;
    const unsigned char *bytes = image->image.getValue(size, components);
    v.insert(v.end(),
             {double(size[0]), double(size[1]), double(components),
              double(image->model.getValue()), double(image->wrapS.getValue()),
              double(image->wrapT.getValue())});
    for (size_t k = 0; k < size_t(size[0]) * size[1] * components; ++k)
      v.push_back(bytes[k]);
  }
  return v;
}
bool nearNativeMsaa(const CoinRenderTarget &target) {
  const auto &options = target.getOptions();
  return options.sampleCount > 1 &&
         options.rasterizationPolicy == COIN_RENDER_RASTER_NATIVE;
}
bool nearRender(Capture &capture, NearAudit &audit, NearSlotScene &scene,
                Frame &frame, const std::string &label) {
  const unsigned submitted = audit.submissions;
  const uint64_t before = capture.target->getLastSubmissionSerial();
  frame.expectedInputs = nearAuthorialInputs(scene);
  const bool rendered = capture.gpu(scene.root, frame);
  return check(rendered && frame.serial > before &&
                   audit.submissions == submitted + 1,
               label + " one successful logical publication") &&
         nearImageOracle(scene, frame, label);
}
bool nearRefuse(Capture &capture, NearAudit &audit, NearSlotScene &scene,
                const Frame &stable, const std::string &label) {
  const unsigned submissions = audit.submissions, producers = audit.producers;
  capture.action.apply(scene.root);
  Frame failed;
  capture.target->readbackRGBA(failed.rgba);
  capture.target->readbackDepth(failed.depth);
  failed.serial = capture.target->getLastSubmissionSerial();
  return check(capture.action.getLastStatus() != CoinRenderAction::SUCCESS &&
                   capture.action.getLastError().getLength() &&
                   audit.submissions == submissions &&
                   audit.producers == producers &&
                   failed.serial == stable.serial &&
                   failed.rgba == stable.rgba && failed.depth == stable.depth,
               label + " invalid slot refuses before submission/publication");
}

bool nearDelivery(const NearSlotScene &scene, const NearAudit &audit,
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
                  "Current ABI retains all four base slots and extra slots5..8");
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
                  "every slot retains authorial current-ABI scissor and zero flags");
  }
  return ok;
}

bool nearCoverageLevels(const Frame &, const NearSlotScene &, unsigned, int,
                        bool, bool);

bool nearRestored(Capture &capture, NearAudit &audit, NearSlotScene &scene,
                  const Frame &before, const Frame &current,
                  const std::string &label, bool sameInputs = true) {
  if (!nearNativeMsaa(*capture.target))
    return cropSame(before, current, label);
  return check(
             current.serial > before.serial &&
                 !current.expectedInputs.empty() &&
                 (!sameInputs ||
                  before.expectedInputs == current.expectedInputs) &&
                 current.expectedInputs == nearAuthorialInputs(scene),
             label +
                 " current publication of the expected authorial parameters") &&
         (!sameInputs || nearDelivery(scene, audit)) &&
         nearImageOracle(scene, current, label) &&
         (!scene.coverage ||
          nearCoverageLevels(current, scene,
                             capture.target->getOptions().sampleCount, side,
                             false, true));
}

bool nearSlotProfiles(Capture &capture, NearAudit &audit, int count,
                      bool general, int raster) {
  NearSlotScene scene(count, raster, general);
  bool ok = true;
  Frame frontReference, first, changed, restored;
  for (int slot = 0; slot < count; ++slot) {
    scene.isolate(slot);
    scene.mixedNears();
    scene.switches[slot]->whichChild = SO_SWITCH_NONE;
    ok &=
        nearRender(capture, audit, scene, first, "empty selected map control");
    scene.switches[slot]->whichChild = SO_SWITCH_ALL;
    for (float nearValue : {1.f, 0.f, -2.f}) {
      scene.cameras[slot]->nearDistance = nearValue;
      ok &= nearRender(capture, audit, scene, changed,
                       "front caster near=" + std::to_string(nearValue)) &&
            nearDelivery(scene, audit);
      if (nearValue == 1)
        frontReference = changed;
      else
        ok &= nearNativeMsaa(*capture.target)
                  ? nearRestored(capture, audit, scene, frontReference, changed,
                                 "front signed-near current depth", false)
                  : check(frontReference.depth == changed.depth,
                          "front signed-near variants preserve receiver depth");
    }
    scene.moves[slot]->translation.setValue(0, 0, 6);
    for (float nearValue : {-2.f, -.5f, 0.f, 1.f}) {
      scene.cameras[slot]->nearDistance = nearValue;
      ok &=
          nearRender(capture, audit, scene, changed,
                     "behind-camera caster near=" + std::to_string(nearValue));
      if (nearValue == -2)
        ok &= nearDelivery(scene, audit);
      else
        ok &= nearRestored(
            capture, audit, scene, first, changed,
            "near0/1/minus.5 clips signed-distance caster exactly", false);
    }
    scene.moves[slot]->translation.setValue(0, 0, .35f);
    scene.cameras[slot]->nearDistance = 1;
    ok &= nearRender(capture, audit, scene, restored,
                     "per-slot near/caster A/B/A") &&
          nearRestored(capture, audit, scene, frontReference, restored,
                       "front caster exact recovery after signed clipping");
  }
  return ok;
}

// Compare identical authored inputs exactly. Across distinct inputs the
// analytic oracle remains authoritative; shadow-map quantization at silhouettes
// is not an invariant of a near-plane change or a different accumulation order.
size_t nearDifference(const Frame &a, const Frame &b) {
  if (a.rgba.size() != b.rgba.size())
    return a.rgba.size() + b.rgba.size();
  size_t different = 0;
  for (size_t i = 0; i < a.rgba.size(); ++i)
    different += a.rgba[i] != b.rgba[i];
  return different;
}
bool nearSlotState(Capture &capture, NearAudit &audit, int count,
                   bool general) {
  NearSlotScene scene(count, 0, general);
  scene.allContribute();
  scene.mixedNears();
  Frame stable, changed, restored;
  bool ok = nearRender(capture, audit, scene, stable,
                       "all maps contribute with distinct signed nears") &&
            nearDelivery(scene, audit);
  for (int slot = 0; slot < count; ++slot) {
    auto *camera = scene.cameras[slot];
    const float originalNear = camera->nearDistance.getValue();
    const float originalFar = camera->farDistance.getValue();
    const SbVec3f originalPosition = camera->position.getValue();
    std::cout << "near_slot_state slot=" << slot << " maps=" << count << '\n';
    camera->position.setValue(originalPosition[0] + .28f, originalPosition[1],
                              originalPosition[2]);
    ok &= nearRender(capture, audit, scene, changed,
                     "one map camera moves while all lights contribute") &&
          nearDelivery(scene, audit);
    ok &= check(nearDifference(stable, changed) > 16,
                "individual camera footprint mutation is observable");
    camera->position = originalPosition;
    ok &= nearRender(capture, audit, scene, restored,
                     "individual camera position recovery") &&
          nearRestored(capture, audit, scene, stable, restored,
                       "all-map camera position exact A/B/A");
    stable = restored;
    scene.moves[slot]->translation.setValue(0, 0, 6);
    camera->nearDistance = 1;
    ok &= nearRender(capture, audit, scene, changed,
                     "one behind-camera caster clipped independently");
    ok &= check(nearDifference(stable, changed) > 16,
                "individual signed clipping affects composition");
    camera->nearDistance = -2;
    ok &= nearRender(capture, audit, scene, changed,
                     "one behind-camera caster retained by negative near") &&
          nearDelivery(scene, audit);
    scene.moves[slot]->translation.setValue(0, 0, .35f);
    camera->nearDistance = originalNear;
    ok &= nearRender(capture, audit, scene, restored,
                     "one signed clipping state restored") &&
          nearRestored(capture, audit, scene, stable, restored,
                       "all-map signed clipping exact A/B/A");
    stable = restored;
    // Each invalid field is consumed by an enabled, nonzero-intensity map.
    for (int invalid = 0; invalid < 7; ++invalid) {
      camera->nearDistance = originalNear;
      camera->farDistance = originalFar;
      if (invalid == 0)
        camera->nearDistance = std::numeric_limits<float>::quiet_NaN();
      if (invalid == 1)
        camera->nearDistance = std::numeric_limits<float>::infinity();
      if (invalid == 2)
        camera->nearDistance = -std::numeric_limits<float>::infinity();
      if (invalid == 3)
        camera->farDistance = std::numeric_limits<float>::quiet_NaN();
      if (invalid == 4)
        camera->farDistance = std::numeric_limits<float>::infinity();
      if (invalid == 5)
        camera->farDistance = originalNear;
      if (invalid == 6)
        camera->farDistance = originalNear - 1;
      ok &= nearRefuse(capture, audit, scene, stable,
                       "invalid camera field slot=" + std::to_string(slot) +
                           " field=" + std::to_string(invalid));
      camera->nearDistance = originalNear;
      camera->farDistance = originalFar;
      ok &= nearRender(capture, audit, scene, restored,
                       "invalid per-slot field recovery") &&
            nearRestored(capture, audit, scene, stable, restored,
                         "invalid per-slot field exact recovery");
      stable = restored;
    }
    // Orthographic signed near is valid; perspective nonpositive near is not.
    camera->ref();
    auto *perspective = new SoPerspectiveCamera;
    perspective->position = originalPosition;
    perspective->farDistance = originalFar;
    scene.maps[slot]->replaceChild(camera, perspective);
    for (float invalidNear : {0.f, -2.f}) {
      perspective->nearDistance = invalidNear;
      ok &= nearRefuse(capture, audit, scene, stable,
                       "perspective nonpositive near remains invalid in slot=" +
                           std::to_string(slot));
    }
    scene.maps[slot]->replaceChild(perspective, camera);
    camera->unref();
    ok &= nearRender(capture, audit, scene, restored,
                     "orthographic camera replacement recovery") &&
          nearRestored(
              capture, audit, scene, stable, restored,
              "orthographic camera exact recovery after perspective refusal");
    stable = restored;
  }
  // Preserve object identity while reversing map occurrence slots. The oracle
  // follows source objects, not ordinal positions in either transport array.
  for (auto *light : scene.lights) {
    light->ref();
    scene.group->removeChild(light);
  }
  int occurrence = 0;
  for (auto it = scene.lights.rbegin(); it != scene.lights.rend(); ++it)
    scene.group->insertChild(*it, occurrence++);
  ok &= nearRender(capture, audit, scene, changed,
                   "permuted map occurrences retain individual cameras") &&
        nearDelivery(scene, audit);
  for (auto *light : scene.lights)
    scene.group->removeChild(light);
  for (size_t i = 0; i < scene.lights.size(); ++i) {
    scene.group->insertChild(scene.lights[i], int(i));
    scene.lights[i]->unref();
  }
  ok &= nearRender(capture, audit, scene, restored,
                   "map occurrence order restored") &&
        nearRestored(capture, audit, scene, stable, restored,
                     "map permutation exact A/B/A");
  return ok;
}

bool nearBindings(Capture &capture, NearAudit &audit, NearSlotScene &scene) {
  if (!scene.generalEnabled)
    return true;
  Frame first, changed, back;
  bool ok =
      nearRender(capture, audit, scene, first, "eight-white-unit reference");
  for (size_t unit = 0; unit < 8; ++unit) {
    scene.colorUnit(unit, true);
    ok &= nearRender(capture, audit, scene, changed,
                     "unit visual witness=" + std::to_string(unit)) &&
          nearDelivery(scene, audit);
    ok &= check(nearDifference(first, changed) > 500,
                "each bound texture unit independently affects RGB");
    scene.colorUnit(unit, false);
    ok &= nearRender(capture, audit, scene, back, "unit white restoration") &&
          nearRestored(capture, audit, scene, first, back,
                       "texture unit exact A/B/A");
  }
  return ok;
}
bool nearCoverageLevels(const Frame &frame, const NearSlotScene &scene,
                        unsigned samples, int extent, bool opaqueWindow,
                        bool report = true) {
  if (frame.rgba.size() != size_t(extent * extent * 4))
    return false;
  unsigned partial = 0, odd = 0, bad = 0, full = 0, empty = 0, tested = 0;
  double sum = 0;
  for (auto *light : scene.lights)
    sum += light->intensity.getValue();
  const double diffuse[] = {.72, .75, .78};
  for (int y = 0; y < extent; ++y)
    for (int x = 0; x < extent; ++x) {
      const size_t p = size_t(y * extent + x) * 4;
      const double worldX = (x + .5 - extent / 2.) * 4 / extent,
                   worldY = (extent / 2. - y - .5) * 4 / extent;
      const bool fullyInside =
          scene.receiverInterior(worldX, worldY, 8. / extent);
      const bool fullyOutside =
          !scene.receiverInterior(worldX, worldY, -8. / extent);
      if (!opaqueWindow) {
        const double a = frame.rgba[p + 3];
        const int k = int(std::lround(a * samples / 255));
        bad += std::abs(a - std::round(255. * k / samples)) > 1;
        bad += (fullyInside && k != int(samples)) || (fullyOutside && k != 0);
        partial += k > 0 && k < int(samples);
        odd += k > 0 && k < int(samples) && (k % 2);
        full += k == int(samples);
        empty += k == 0;
        ++tested;
      } else {
        // Only perimeter pixels provably outside every shadow envelope. The
        // receiver is opaque and has uniform fully-lit primary there, so its
        // resolved RGB is k/N times that primary against authored black clear.
        const double wx = (x + .5 - extent / 2.) * 4 / extent,
                     wy = (extent / 2. - y - .5) * 4 / extent;
        const double mx = (.5 + wx / std::sqrt(96.)) * 256,
                     my = (.5 + wy / std::sqrt(96.)) * 256;
        bool outside = true;
        for (size_t i = 0; i < scene.lights.size(); ++i)
          outside &= !nearFootprint(scene, i).inside(mx, my, -3.1);
        if (!outside)
          continue;
        double best = 1e30;
        int chosen = -1;
        for (unsigned k = 0; k <= samples; ++k) {
          double error = 0;
          bool valid = true;
          for (int c = 0; c < 3; ++c) {
            const double expected = 255 * (.036 + diffuse[c] * sum) *
                                    scene.textureProduct[c] * k / samples;
            const double delta = std::abs(double(frame.rgba[p + c]) - expected);
            valid &= delta <= 3;
            error += delta * delta;
          }
          if (valid && error < best) {
            best = error;
            chosen = int(k);
          }
        }
        bad += chosen < 0;
        bad += (fullyInside && chosen != int(samples)) ||
               (fullyOutside && chosen != 0);
        partial += chosen > 0 && chosen < int(samples);
        odd += chosen > 0 && chosen < int(samples) && (chosen % 2);
        full += chosen == int(samples);
        empty += chosen == 0;
        ++tested;
      }
    }
  if (report)
    std::cout << "near_msaa_coverage samples=" << samples
              << " extent=" << extent << " opaque=" << opaqueWindow
              << " tested=" << tested << " full=" << full << " empty=" << empty
              << " partial=" << partial << " odd=" << odd << " bad=" << bad
              << '\n';
  return tested > 100 && full > 100 && empty > 100 && bad == 0 &&
         (samples == 1 || (partial > 8 && (samples != 4 || odd > 0)));
}
bool nearCoverage(Capture &capture, NearAudit &audit, int count, bool general,
                  unsigned samples) {
  NearSlotScene scene(count, 0, general);
  scene.allContribute();
  scene.mixedNears();
  scene.slopedReceiver();
  capture.action.setBackgroundColor(SbColor4f(0, 0, 0, 0));
  Frame a, b, back;
  bool ok = nearRender(capture, audit, scene, a, "sloped receiver real MSAA") &&
            nearDelivery(scene, audit);
  ok &= check(nearCoverageLevels(a, scene, samples, side, false),
              "offscreen alpha proves real requested MSAA coverage");
  scene.cameras.back()->nearDistance = -2;
  scene.moves.back()->translation.setValue(0, 0, 6);
  ok &= nearRender(capture, audit, scene, b, "MSAA signed caster after eye") &&
        nearDelivery(scene, audit);
  ok &= check(nearCoverageLevels(b, scene, samples, side, false),
              "MSAA coverage survives map mutation");
  scene.moves.back()->translation.setValue(0, 0, .35f);
  scene.mixedNears();
  ok &= nearRender(capture, audit, scene, back, "MSAA scene restored") &&
        nearRestored(capture, audit, scene, a, back, "MSAA exact A/B/A");
  ok &= nearBindings(capture, audit, scene);
  return ok;
}
bool nearCropProfiles(Capture &capture, NearAudit &audit, int count,
                      bool general) {
  bool ok = true;
  for (int raster : {0, 1, 2}) {
    NearSlotScene scene(count, raster, general);
    scene.cropViewports();
    scene.mixedNears();
    Frame a, b, back;
    for (int slot = 0; slot < count; ++slot) {
      scene.isolate(slot);
      ok &= nearRender(capture, audit, scene, a,
                       "CROP/subviewport map=" + std::to_string(slot)) &&
            nearDelivery(scene, audit);
      scene.cameras[slot]->viewportMapping = SoCamera::CROP_VIEWPORT_FILL_FRAME;
      ok &= nearRender(capture, audit, scene, b, "CROP fill decoration") &&
            nearRestored(capture, audit, scene, a, b,
                         "decorative crop frame does not cast");
      scene.cameras[slot]->viewportMapping = SoCamera::CROP_VIEWPORT_LINE_FRAME;
      ok &=
          nearRender(capture, audit, scene, b, "CROP line decoration") &&
          nearRestored(capture, audit, scene, a, b, "crop line does not cast");
      scene.cameras[slot]->viewportMapping = SoCamera::CROP_VIEWPORT_NO_FRAME;
      const int kind = scene.authoredViewports[slot].kind;
      scene.authoredViewports[slot].kind = 5;
      ok &= nearRefuse(capture, audit, scene, b, "nonfinite per-map viewport");
      scene.authoredViewports[slot].kind = kind;
      ok &= nearRender(capture, audit, scene, back, "CROP viewport recovery") &&
            nearRestored(capture, audit, scene, a, back,
                         "viewport exact recovery");
    }
  }
  return ok;
}

bool nearScissorProfiles(Capture &capture, NearAudit &audit, int count,
                         bool general) {
  bool ok = true;
  // Marker9's radius cannot provide two independent 2.1-texel margins at a
  // clipping edge. Cube and Image25 give substantial forbidden interiors.
  for (int raster : {0, 1}) {
    NearSlotScene scene(count, raster, general);
    scene.crossScissorBoundary();
    scene.mixedNears();
    Frame first, changed, back;
    for (int slot = 0; slot < count; ++slot) {
      scene.isolate(slot);
      ok &= nearRender(capture, audit, scene, first,
                       "scissor crossing map=" + std::to_string(slot) +
                           " raster=" + std::to_string(raster)) &&
            nearDelivery(scene, audit);
      const int kind = scene.authoredViewports[slot].kind;
      scene.authoredViewports[slot].kind = 5;
      ok &= nearRefuse(capture, audit, scene, first,
                       "crossing scissor invalid viewport");
      scene.authoredViewports[slot].kind = kind;
      ok &= nearRender(capture, audit, scene, back,
                       "crossing scissor recovery") &&
            nearRestored(capture, audit, scene, first, back,
                         "crossing scissor exact A/B/A");
    }
  }
  return ok;
}

// Pure fixture-oracle controls: this never creates a target or queries a GPU.
// A one-level change on a measured interior is a legitimate new execution;
// preserving the previous shadow after the signed clip changes is not.
bool nearRestorationRules() {
  NearSlotScene scene(4, 0, false);
  scene.isolate(0);
  Frame a;
  a.rgba.resize(size_t(side * side * 4));
  a.depth.assign(size_t(side * side), float(6. / 19));
  const double centerX = -.30, centerY = -.12, height = 4;
  for (int y = 0; y < side; ++y)
    for (int x = 0; x < side; ++x) {
      const double wx = (x + .5 - side / 2) * 4 / side;
      // Published RGBA rows run top to bottom; observer world +Y runs up.
      const double wy = (side / 2. - y - .5) * 4 / side;
      const double mx = (.5 + wx / std::sqrt(96.)) * 256;
      const double my = (.5 + wy / std::sqrt(96.)) * 256;
      const bool covered =
          std::abs(mx - (.5 - centerX / height) * 256) < .4 / height * 256 &&
          std::abs(my - (.5 - centerY / height) * 256) < .4 / height * 256;
      const double diffuse[] = {.72, .75, .78};
      for (int c = 0; c < 3; ++c)
        a.rgba[4 * (y * side + x) + c] = uint8_t(
            std::lround(255 * (.036 + diffuse[c] * (covered ? 0 : .55))));
      a.rgba[4 * (y * side + x) + 3] = 255;
    }
  bool ok = check(nearImageOracle(scene, a, "analytic restoration control",
                                  true, side, false),
                  "analytic old shadow is initially valid");
  Frame b = a;
  const size_t pixel = size_t((64 * side + 64) * 4);
  ++b.rgba[pixel + 2];
  ok &= check(nearImageOracle(scene, b, "one-level current execution", true,
                              side, false),
              "RGB3 oracle accepts one-level interior execution variation");
  b = a;
  b.rgba[pixel + 2] += 4;
  ok &= check(!nearImageOracle(scene, b, "outside RGB3", true, side, false),
              "RGB outside the original bound remains invalid");
  b = a;
  b.rgba[pixel + 3] = 0;
  ok &= check(!nearImageOracle(scene, b, "wrong alpha", true, side, false),
              "interior alpha remains required");
  b = a;
  b.depth[pixel / 4] = 0;
  ok &= check(!nearImageOracle(scene, b, "wrong depth", true, side, false),
              "current depth remains independently required");
  const auto authored = nearAuthorialInputs(scene);
  scene.moves[0]->translation.setValue(0, 0, 6);
  scene.cameras[0]->nearDistance = 0;
  ok &= check(!nearImageOracle(scene, a, "frozen physical old shadow", true,
                               side, false),
              "frozen previous physical shadow fails the new signed clip");
  scene.moves[0]->translation.setValue(0, 0, .35f);
  scene.cameras[0]->nearDistance = 1;
  ok &= check(
      authored == nearAuthorialInputs(scene) &&
          nearImageOracle(scene, a, "restored authored inputs", true, side,
                          false),
      "authorial restoration survives legitimate source revision changes");
  // Pixel (56,64) projects within 2.1 map pixels of the left silhouette:
  // mapX=(.5+((56.5-64)*4/128)/sqrt(96))*256, left=(.5+.30/4-.4/4)*256.
  // That region deliberately has no RGB3 promise. Alter only its RGB widely;
  // all measured interiors, alpha, depth and the old-image adversary remain.
  const double edgeMapX = (.5 + ((56.5 - 64) * 4 / 128) / std::sqrt(96.)) * 256;
  const double left = (.5 + .30 / 4 - .4 / 4) * 256;
  b = a;
  for (int c = 0; c < 3; ++c)
    b.rgba[4 * (64 * side + 56) + c] = 255;
  ok &= check(std::abs(edgeMapX - left) < 2.1 &&
                  nearImageOracle(scene, b, "unconstrained silhouette RGB",
                                  true, side, false),
              "excluded silhouette variation is not a stale-image witness");
  return ok;
}
