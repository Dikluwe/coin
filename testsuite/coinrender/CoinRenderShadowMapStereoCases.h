// Algebra follows the public SoCamera GL contract. No captured matrix is used
// to compute expected projection, clip regions, sampling positions or RGB.
struct StereoPoint {
  double x, y;
  StereoPoint(double a = 0, double b = 0) : x(a), y(b) {}
};
using StereoPolygon = std::vector<StereoPoint>;
double stereoCross(const StereoPoint &a, const StereoPoint &b,
                   const StereoPoint &p) {
  return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}
StereoPolygon stereoHull(StereoPolygon points) {
  std::sort(points.begin(), points.end(),
            [](const StereoPoint &a, const StereoPoint &b) {
              return a.x < b.x || (a.x == b.x && a.y < b.y);
            });
  StereoPolygon hull;
  for (const auto &p : points) {
    while (hull.size() > 1 &&
           stereoCross(hull[hull.size() - 2], hull.back(), p) <= 0)
      hull.pop_back();
    hull.push_back(p);
  }
  const size_t lower = hull.size();
  for (auto it = points.rbegin() + 1; it != points.rend(); ++it) {
    while (hull.size() > lower &&
           stereoCross(hull[hull.size() - 2], hull.back(), *it) <= 0)
      hull.pop_back();
    hull.push_back(*it);
  }
  if (hull.size() > 1)
    hull.pop_back();
  return hull;
}
StereoPolygon stereoClip(StereoPolygon input, const CropRect &rectangle) {
  for (int edge = 0; edge < 4; ++edge) {
    StereoPolygon output;
    if (input.empty())
      break;
    const auto distance = [&](const StereoPoint &p) {
      return edge == 0   ? p.x - rectangle.x
             : edge == 1 ? rectangle.x + rectangle.w - p.x
             : edge == 2 ? p.y - rectangle.y
                         : rectangle.y + rectangle.h - p.y;
    };
    StereoPoint a = input.back();
    double da = distance(a);
    for (const auto &b : input) {
      const double db = distance(b);
      if ((da >= 0) != (db >= 0)) {
        const double t = da / (da - db);
        output.emplace_back(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y));
      }
      if (db >= 0)
        output.push_back(b);
      a = b;
      da = db;
    }
    input.swap(output);
  }
  return input;
}
bool stereoContains(const StereoPolygon &polygon, double x, double y,
                    double margin) {
  if (polygon.size() < 3)
    return false;
  const StereoPoint p(x, y);
  for (size_t i = 0; i < polygon.size(); ++i) {
    const auto &a = polygon[i], &b = polygon[(i + 1) % polygon.size()];
    if (stereoCross(a, b, p) <= margin * std::hypot(b.x - a.x, b.y - a.y))
      return false;
  }
  return true;
}
SbMatrix stereoTranslation(double x, double y, double z) {
  SbMatrix m;
  m.setTranslate(SbVec3f(float(x), float(y), float(z)));
  return m;
}
SbMatrix stereoRotation(double angle) {
  SbMatrix m;
  m.setRotate(SbRotation(SbVec3f(0, 0, 1), float(angle)));
  return m;
}
struct StereoOccurrence {
  CropViewport viewport;
  int placement = 0;
  bool external = false;
  SbMatrix before = SbMatrix::identity(), after = SbMatrix::identity();
  SoTranslation *move = nullptr;
  SoScale *beforeScale = nullptr, *afterScale = nullptr;
  SbUniqueId casterId = 0;
};
struct StereoSourceProbe {
  SoCamera *camera = nullptr;
  SbUniqueId id = 0;
  SoCamera::StereoMode mode = SoCamera::MONOSCOPIC;
  float adjustment = 0, balance = 0;
  bool same = true;
  unsigned visits = 0;
  void arm(SoCamera *source) {
    camera = source;
    id = camera->getNodeId();
    mode = camera->getStereoMode();
    adjustment = camera->getStereoAdjustment();
    balance = camera->getBalanceAdjustment();
    same = true;
    visits = 0;
  }
  static void inspect(void *data, SoAction *action) {
    if (!action->isOfType(SoCallbackAction::getClassTypeId()) &&
        !action->isOfType(SoGLRenderAction::getClassTypeId()))
      return;
    auto &p = *static_cast<StereoSourceProbe *>(data);
    ++p.visits;
    p.same &= p.camera->getNodeId() == p.id &&
              p.camera->getStereoMode() == p.mode &&
              p.camera->getStereoAdjustment() == p.adjustment &&
              p.camera->getBalanceAdjustment() == p.balance;
  }
};
struct StereoScene {
  SoSeparator *root = new SoSeparator, *groupReceiver = new SoSeparator;
  SoShadowGroup *group = new SoShadowGroup;
  std::vector<SoLight *> lights;
  std::vector<SoSeparator *> maps;
  std::vector<SoCamera *> cameras;
  std::vector<std::vector<StereoOccurrence>> occurrences;
  std::vector<SoTexture2 *> textures;
  std::vector<StereoSourceProbe> probes;
  int raster, mapping;
  bool directional, orthographic, general, shared;
  bool coverage = false, scissorCrossing = false;
  double textureProduct[3] = {1, 1, 1};
  StereoScene(int count, bool dir, bool ortho, int mapMode, int casterKind,
              bool fullTextures, bool twoOccurrences = false)
      : raster(casterKind), mapping(mapMode), directional(dir),
        orthographic(ortho), general(fullTextures), shared(twoOccurrences) {
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
    group->precision = .125f;
    group->quality = 1.f;
    root->addChild(group);
    occurrences.resize(count);
    probes.resize(count);
    for (int slot = 0; slot < count; ++slot) {
      auto *map = new SoSeparator;
      SoLight *light = nullptr;
      if (dir) {
        auto *l = new SoShadowDirectionalLight;
        l->direction.setValue(0, 0, -1);
        l->bboxCenter.setValue(0, 0, 0);
        l->bboxSize.setValue(4, 4, 8);
        l->shadowMapScene = map;
        light = l;
      } else {
        auto *l = new SoShadowSpotLight;
        l->location.setValue(0, 0, 5);
        l->direction.setValue(0, 0, -1);
        l->cutOffAngle = .6f;
        l->dropOffRate = 0;
        l->nearDistance = 1;
        l->farDistance = 10;
        l->shadowMapScene = map;
        light = l;
      }
      light->on = TRUE;
      light->intensity = 0;
      lights.push_back(light);
      maps.push_back(map);
      group->addChild(light);
      SoCamera *camera = nullptr;
      if (ortho) {
        auto *c = new SoOrthographicCamera;
        c->height = 2.4f + .05f * slot;
        camera = c;
      } else {
        auto *c = new SoPerspectiveCamera;
        c->heightAngle = 1.f;
        camera = c;
      }
      camera->position.setValue(-.25f + .5f * slot / float(count - 1),
                                -.08f + .04f * (slot % 4), 5);
      camera->nearDistance = 1.f + .04f * slot;
      camera->farDistance = 10.f + .2f * slot;
      camera->aspectRatio = slot % 2 ? .75f : 1.5f;
      camera->focalDistance = 3.2f + .1f * slot;
      camera->viewportMapping = mapMode;
      camera->setStereoMode(slot % 2 ? SoCamera::RIGHT_VIEW
                                     : SoCamera::LEFT_VIEW);
      camera->setStereoAdjustment(.2f + .006f * slot);
      camera->setBalanceAdjustment(1.f);
      cameras.push_back(camera);
      auto *sharedCube = new SoCube;
      sharedCube->width = 1.2f;
      sharedCube->height = 1.f;
      sharedCube->depth = .2f;
      occurrences[slot].resize(twoOccurrences ? 3 : 1);
      for (size_t index = 0; index < occurrences[slot].size(); ++index) {
        auto &occ = occurrences[slot][index];
        occ.external = index == 2;
        auto *branch = new SoSeparator;
        map->addChild(branch);
        if (!occ.external) {
          occ.viewport.mapSide = 256;
          occ.viewport.kind = (slot + int(index)) % 2 ? 2 : 1;
          occ.viewport.left = .0625;
          occ.viewport.bottom = .125;
          occ.viewport.width = .875;
          occ.viewport.height = .75;
          occ.placement = (slot + int(index)) % 2;
          if (!occ.placement)
            branch->addChild(occ.viewport.node());
          if (twoOccurrences) {
            const double sign = index ? -1 : 1;
            auto *t = new SoTranslation;
            t->translation.setValue(float(.19 * sign), float(-.11 * sign), 0);
            branch->addChild(t);
            auto *r = new SoRotation;
            r->rotation.setValue(SbVec3f(0, 0, 1), float(.23 * sign));
            branch->addChild(r);
            occ.before = stereoRotation(.23 * sign) *
                         stereoTranslation(.19 * sign, -.11 * sign, 0);
          }
          occ.beforeScale = new SoScale;
          branch->addChild(occ.beforeScale);
          branch->addChild(camera);
          if (twoOccurrences) {
            auto *probe = new SoCallback;
            probe->setCallback(StereoSourceProbe::inspect, &probes[slot]);
            branch->addChild(probe);
          }
          if (occ.placement)
            branch->addChild(occ.viewport.node());
          if (twoOccurrences) {
            const double sign = index ? -1 : 1;
            auto *r = new SoRotation;
            r->rotation.setValue(SbVec3f(0, 0, 1), float(-.17 * sign));
            branch->addChild(r);
            auto *t = new SoTranslation;
            t->translation.setValue(float(-.27 * sign), float(.14 * sign), 0);
            branch->addChild(t);
            occ.after = stereoTranslation(-.27 * sign, .14 * sign, 0) *
                        stereoRotation(-.17 * sign);
          }
        }
        occ.afterScale = new SoScale;
        branch->addChild(occ.afterScale);
        auto *style = new SoShadowStyle;
        style->style = SoShadowStyle::CASTS_SHADOW;
        branch->addChild(style);
        occ.move = new SoTranslation;
        occ.move->translation.setValue(occ.external ? 1.25f : 0.f,
                                       occ.external ? -1.f : 0.f, .35f);
        branch->addChild(occ.move);
        SoNode *shape = nullptr;
        if (!raster || occ.external) {
          if (occ.external) {
            auto *cube = new SoCube;
            cube->width = .48f;
            cube->height = .48f;
            cube->depth = .2f;
            shape = cube;
          } else
            shape = sharedCube;
        } else if (raster == 1) {
          auto *image = new SoImage;
          image->horAlignment = SoImage::CENTER;
          image->vertAlignment = SoImage::HALF;
          const std::vector<uint8_t> rgba(25 * 25 * 4, 255);
          image->image.setValue(SbVec2s(25, 25), 4, rgba.data());
          shape = image;
        } else {
          auto *coords = new SoCoordinate3;
          coords->point.setValue(0, 0, 0);
          branch->addChild(coords);
          auto *marker = new SoMarkerSet;
          marker->markerIndex = SoMarkerSet::SQUARE_FILLED_9_9;
          shape = marker;
        }
        branch->addChild(shape);
        occ.casterId = shape->getNodeId();
      }
      if (raster) {
        sharedCube->ref();
        sharedCube->unref();
      }
    }
    auto *style = new SoShadowStyle;
    style->style = SoShadowStyle::SHADOWED;
    groupReceiver->addChild(style);
    if (general) {
      auto *quality = new SoComplexity;
      quality->textureQuality = .1f;
      groupReceiver->addChild(quality);
      for (int unit = 0; unit < 8; ++unit) {
        auto *select = new SoTextureUnit;
        select->unit = unit;
        groupReceiver->addChild(select);
        auto *texture = new SoTexture2;
        const uint8_t white[] = {255, 255, 255, 255};
        texture->image.setValue(SbVec2s(1, 1), 4, white);
        groupReceiver->addChild(texture);
        textures.push_back(texture);
        auto *uv = new SoTextureCoordinate2;
        const SbVec2f coords[] = {SbVec2f(0, 0), SbVec2f(1, 0), SbVec2f(1, 1),
                                  SbVec2f(0, 1)};
        uv->point.setValues(0, 4, coords);
        groupReceiver->addChild(uv);
      }
      auto *zero = new SoTextureUnit;
      zero->unit = 0;
      groupReceiver->addChild(zero);
    }
    plane(groupReceiver);
    group->addChild(groupReceiver);
  }
  bool receiverInterior(double x, double y, double margin) const {
    if (!coverage)
      return true;
    const double polygon[4][2] = {
        {-1.83, -1.79}, {1.73, -1.69}, {1.61, 1.81}, {-1.71, 1.57}};
    for (int i = 0; i < 4; ++i) {
      const auto &a = polygon[i];
      const auto &b = polygon[(i + 1) % 4];
      const double dx = b[0] - a[0], dy = b[1] - a[1];
      if (dx * (y - a[1]) - dy * (x - a[0]) <= margin * std::hypot(dx, dy))
        return false;
    }
    return true;
  }
  ~StereoScene() { root->unref(); }
  void isolate(size_t slot) {
    for (size_t i = 0; i < lights.size(); ++i)
      lights[i]->intensity = i == slot ? .55f : 0.f;
  }
  void together() {
    for (auto *light : lights)
      light->intensity = .45f / float(lights.size());
  }
  void colorUnit(size_t unit, bool changed) {
    const uint8_t white[] = {255, 255, 255, 255},
                  color[] = {128, 191, 223, 255};
    textures[unit]->image.setValue(SbVec2s(1, 1), 4, changed ? color : white);
    for (int c = 0; c < 3; ++c)
      textureProduct[c] = changed ? double(color[c]) / 255 : 1;
  }
  double lookupWidth() const {
    return directional ? std::sqrt(96.) : 12 * std::tan(.6);
  }
  CropRect viewport(size_t slot, size_t index) const {
    CropRect full;
    full.w = full.h = 256;
    const auto &occ = occurrences[slot][index];
    if (occ.external)
      return full;
    const auto input =
        occ.placement ? full : cropAuthoredViewport(occ.viewport);
    if (occ.placement)
      return cropPixelViewport(cropAuthoredViewport(occ.viewport));
    const int mode = cameras[slot]->viewportMapping.getValue();
    return mode == SoCamera::ADJUST_CAMERA || mode == SoCamera::LEAVE_ALONE
               ? cropPixelViewport(input)
               : cropCameraViewport(input,
                                    cameras[slot]->aspectRatio.getValue(), 256);
  }
  void nearDimensions(size_t slot, size_t index, double &width,
                      double &height) const {
    auto *camera = cameras[slot];
    const double nearValue = camera->nearDistance.getValue();
    height =
        orthographic
            ? static_cast<SoOrthographicCamera *>(camera)->height.getValue()
            : 2 * nearValue *
                  std::tan(static_cast<SoPerspectiveCamera *>(camera)
                               ->heightAngle.getValue() /
                           2.);
    double aspect = camera->aspectRatio.getValue();
    if (camera->viewportMapping.getValue() == SoCamera::ADJUST_CAMERA) {
      CropRect full;
      full.w = full.h = 256;
      const auto &occ = occurrences[slot][index];
      const auto input =
          occ.placement ? full
                        : cropPixelViewport(cropAuthoredViewport(occ.viewport));
      aspect = input.w / input.h;
      if (aspect < 1)
        height /= aspect;
    }
    width = height * aspect;
  }
  SbMatrix projection(size_t slot, size_t index) const {
    auto *camera = cameras[slot];
    double width, height;
    nearDimensions(slot, index, width, height);
    const double n = camera->nearDistance.getValue(),
                 f = camera->farDistance.getValue();
    const bool stereo = camera->getStereoMode() != SoCamera::MONOSCOPIC;
    if (!stereo && orthographic)
      return SbMatrix(float(2 / width), 0, 0, 0, 0, float(2 / height), 0, 0, 0,
                      0, float(-2 / (f - n)), 0, 0, 0,
                      float(-(f + n) / (f - n)), 1);
    const double eye =
        stereo ? (camera->getStereoMode() == SoCamera::LEFT_VIEW ? -1 : 1) *
                     camera->getStereoAdjustment() / 2.
               : 0;
    const double focal = std::max(n, double(camera->focalDistance.getValue()) *
                                         camera->getBalanceAdjustment());
    return SbMatrix(float(2 * n / width), 0, 0, 0, 0, float(2 * n / height), 0,
                    0, float(-2 * eye * n / (focal * width)), 0,
                    float(-(f + n) / (f - n)), -1, 0, 0,
                    float(-2 * f * n / (f - n)), 0);
  }
  SbMatrix viewing(size_t slot) const {
    auto *camera = cameras[slot];
    const auto p = camera->position.getValue();
    const double eye =
        camera->getStereoMode() == SoCamera::MONOSCOPIC
            ? 0
            : (camera->getStereoMode() == SoCamera::LEFT_VIEW ? -1 : 1) *
                  camera->getStereoAdjustment() / 2.;
    return stereoTranslation(-p[0] - eye, -p[1], -p[2]);
  }
  // Camera snapshot is V*M^-1. Native GL drawing first removes that M,
  // then reinstates camera-local geometry through its reset matrix:
  // M^-1*((V*M^-1)*M) = M^-1*V (row-vector convention).
  SbMatrix view(size_t slot, size_t index) const {
    return viewing(slot) * occurrences[slot][index].before.inverse();
  }
  SbMatrix drawView(size_t slot, size_t index) const {
    return occurrences[slot][index].before.inverse() * viewing(slot);
  }
  SbMatrix model(size_t slot, size_t index) const {
    const auto &occ = occurrences[slot][index];
    const auto p = occ.move->translation.getValue();
    return stereoTranslation(p[0], p[1], p[2]) * occ.after * occ.before;
  }
};
struct StereoShape {
  StereoPolygon inner, outer;
  bool external = false;
};
StereoShape stereoShape(const StereoScene &scene, size_t slot, size_t index,
                        bool clipped = true) {
  const auto &occ = scene.occurrences[slot][index];
  const auto vp = scene.viewport(slot, index);
  StereoShape shape;
  shape.external = occ.external;
  CropRect map;
  map.w = map.h = 256;
  if (occ.external) {
    const auto p = occ.move->translation.getValue();
    const double width = scene.directional ? scene.lookupWidth()
                                           : 2 * (5 - p[2] - .1) * std::tan(.6);
    const double left = 256 * (.5 + (p[0] - .24) / width),
                 right = 256 * (.5 + (p[0] + .24) / width);
    const double bottom = 256 * (.5 + (p[1] - .24) / width),
                 top = 256 * (.5 + (p[1] + .24) / width);
    shape.inner = {{left, bottom}, {right, bottom}, {right, top}, {left, top}};
    shape.outer = shape.inner;
    if (!scene.directional) {
      const double backWidth = 2 * (5 - p[2] + .1) * std::tan(.6);
      for (double y : {double(p[1]) - .24, double(p[1]) + .24})
        for (double x : {double(p[0]) - .24, double(p[0]) + .24})
          shape.outer.emplace_back(256 * (.5 + x / backWidth),
                                   256 * (.5 + y / backWidth));
      shape.outer = stereoHull(shape.outer);
    }
    return shape;
  }
  const auto mv = scene.model(slot, index) * scene.drawView(slot, index);
  const auto mvp = mv * scene.projection(slot, index);
  const auto project = [&](const SbVec3f &p) {
    SbVec3f q;
    mvp.multVecMatrix(p, q);
    return StereoPoint(vp.x + (.5 + .5 * q[0]) * vp.w,
                       vp.y + (.5 + .5 * q[1]) * vp.h);
  };
  if (!scene.raster) {
    StereoPolygon corners;
    for (float z : {-.1f, .1f})
      for (float y : {-.5f, .5f})
        for (float x : {-.6f, .6f})
          corners.push_back(project(SbVec3f(x, y, z)));
    shape.inner = stereoHull(
        {project(SbVec3f(-.6f, -.5f, .1f)), project(SbVec3f(.6f, -.5f, .1f)),
         project(SbVec3f(.6f, .5f, .1f)), project(SbVec3f(-.6f, .5f, .1f))});
    shape.outer = stereoHull(corners);
  } else {
    const auto center = project(SbVec3f(0, 0, 0));
    const double half = scene.raster == 1 ? 12 : 4,
                 width = scene.raster == 1 ? 25 : 9;
    const double l = center.x - half, b = center.y - half;
    shape.inner = {
        {l, b}, {l + width, b}, {l + width, b + width}, {l, b + width}};
    shape.outer = shape.inner;
  }
  if (clipped) {
    shape.inner = stereoClip(stereoClip(shape.inner, vp), map);
    shape.outer = stereoClip(stereoClip(shape.outer, vp), map);
  }
  return shape;
}
std::vector<uint64_t> stereoAuthorState(const StereoScene &scene,
                                        bool identities = true) {
  std::vector<uint64_t> result;
  const auto add = [&](float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    result.push_back(bits);
  };
  for (auto *camera : scene.cameras) {
    if (identities)
      result.push_back(camera->getNodeId());
    result.push_back(camera->getStereoMode());
    const int mapping = camera->viewportMapping.getValue();
    result.push_back(!identities &&
                             (mapping == SoCamera::CROP_VIEWPORT_FILL_FRAME ||
                              mapping == SoCamera::CROP_VIEWPORT_LINE_FRAME)
                         ? SoCamera::CROP_VIEWPORT_NO_FRAME
                         : mapping);
    const auto p = camera->position.getValue();
    for (int c = 0; c < 3; ++c)
      add(p[c]);
    float q[4];
    camera->orientation.getValue().getValue(q[0], q[1], q[2], q[3]);
    for (float v : q)
      add(v);
    add(camera->nearDistance.getValue());
    add(camera->farDistance.getValue());
    add(camera->focalDistance.getValue());
    add(camera->aspectRatio.getValue());
    add(camera->getStereoAdjustment());
    add(camera->getBalanceAdjustment());
    add(scene.orthographic
            ? static_cast<SoOrthographicCamera *>(camera)->height.getValue()
            : static_cast<SoPerspectiveCamera *>(camera)
                  ->heightAngle.getValue());
  }
  return result;
}
bool stereoImage(const StereoScene &scene, const Frame &frame,
                 const std::string &label, int extent = side,
                 bool depthRequired = true, bool report = true) {
  if (frame.rgba.size() != size_t(extent * extent * 4) ||
      (depthRequired && frame.depth.size() != size_t(extent * extent)))
    return report ? check(false, label + " complete RGB/depth") : false;
  std::vector<std::vector<StereoShape>> shapes(scene.lights.size());
  for (size_t i = 0; i < shapes.size(); ++i)
    for (size_t j = 0; j < scene.occurrences[i].size(); ++j)
      shapes[i].push_back(stereoShape(scene, i, j));
  std::vector<std::vector<StereoShape>> rawShapes(scene.lights.size());
  if (scene.scissorCrossing)
    for (size_t i = 0; i < rawShapes.size(); ++i)
      for (size_t j = 0; j < scene.occurrences[i].size(); ++j)
        rawShapes[i].push_back(stereoShape(scene, i, j, false));
  unsigned inside = 0, outside = 0, bad = 0, external = 0, forbidden = 0;
  int maximum = 0;
  for (int y = extent * 12 / side; y < extent - extent * 12 / side; ++y)
    for (int x = extent * 12 / side; x < extent - extent * 12 / side; ++x) {
      const double wx = (x + .5 - extent / 2.) * 4 / extent,
                   wy = (extent / 2. - y - .5) * 4 / extent;
      if (!scene.receiverInterior(wx, wy, 8. / extent))
        continue;
      const double mx = (.5 + wx / scene.lookupWidth()) * 256,
                   my = (.5 + wy / scene.lookupWidth()) * 256;
      double strength = 0;
      bool measurable = true, shadow = false, externalShadow = false;
      for (size_t slot = 0; slot < scene.lights.size(); ++slot) {
        const double intensity = scene.lights[slot]->intensity.getValue();
        if (!intensity)
          continue;
        bool covered = false, clear = true;
        for (const auto &shape : shapes[slot]) {
          const bool hit = stereoContains(shape.inner, mx, my, 2.1);
          covered |= hit;
          clear &= !stereoContains(shape.outer, mx, my, -2.1);
          externalShadow |= hit && shape.external;
        }
        if (!covered && !clear) {
          measurable = false;
          break;
        }
        shadow |= covered;
        if (!covered)
          strength += intensity;
      }
      if (!measurable)
        continue;
      inside += shadow;
      outside += !shadow;
      external += externalShadow;
      if (scene.scissorCrossing && !shadow) {
        bool rawCovered = false;
        for (size_t i = 0; i < rawShapes.size(); ++i)
          if (scene.lights[i]->intensity.getValue() > 0)
            for (const auto &shape : rawShapes[i])
              rawCovered |= stereoContains(shape.inner, mx, my, 2.1);
        forbidden += rawCovered;
      }
      const double lambert = scene.directional
                                 ? 1
                                 : 6 / std::sqrt(36 + wx * wx + wy * wy),
                   diffuse[] = {.72, .75, .78};
      const size_t p = size_t(y * extent + x);
      for (int c = 0; c < 3; ++c) {
        const int expected =
            int(std::lround(255 * (.036 + diffuse[c] * lambert * strength) *
                            scene.textureProduct[c]));
        const int error = std::abs(int(frame.rgba[p * 4 + c]) - expected);
        maximum = std::max(maximum, error);
        if (error > 3 && bad++ < 4 && report)
          std::cerr << "stereo_first " << label << " xy=" << x << ',' << y
                    << " expected=" << expected
                    << " actual=" << int(frame.rgba[p * 4 + c]) << '\n';
      }
      bad +=
          frame.rgba[p * 4 + 3] != 255 ||
          (depthRequired && (!std::isfinite(frame.depth[p]) ||
                             std::abs(frame.depth[p] - float(6. / 19)) > 1e-5));
    }
  if (report)
    std::cout << "stereo_image " << label << " inside=" << inside
              << " outside=" << outside << " external=" << external
              << " bad=" << bad << " max=" << maximum
              << " forbidden=" << forbidden << '\n';
  const bool valid = inside >= 4 && outside > 500 && bad == 0 &&
                     (!scene.shared || external >= 4) &&
                     (!scene.scissorCrossing || forbidden > 4);
  return report ? check(valid, label + " independent stereo eye/skew/frustum "
                                       "RGB, restoration and geometric depth")
                : valid;
}

bool stereoMatrixClose(const SbMatrix &actual, const SbMatrix &expected,
                       double tolerance = 1e-5) {
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c)
      if (!std::isfinite(actual[r][c]) ||
          std::abs(double(actual[r][c]) - expected[r][c]) > tolerance)
        return false;
  return true;
}
bool stereoDelivery(const StereoScene &scene, const NearAudit &audit,
                    int targetSide = side) {
  const auto &frame = audit.frame;
  CoinRenderShadowPlan plan;
  std::string error;
  const size_t count = scene.lights.size();
  if (!check(coin_render_plan_shadows(frame, plan, error) &&
                 plan.passes.size() == count &&
                 frame.shadowLights.size() == count,
             "all enabled stereo maps, including zero-intensity maps, reach "
             "Core: " +
                 error))
    return false;
  CoinWgpuFfiFrame ffi;
  if (!check(ffi.prepare(frame, targetSide, targetSide, error, false),
             "stereo private transport: " + error))
    return false;
  const auto &view = ffi.getView();
  bool ok = check(view.abi_version == COIN_WGPU_ABI_VERSION && sizeof(CoinWgpuShadowDraw) == 160 &&
                      view.struct_size == sizeof(CoinWgpuFrameView) &&
                      view.extra_shadow_pass_count == count - 4,
                  "current ABI layout and extended shadow map slots preserved");
  if (scene.general) {
    bool eight = false;
    for (size_t i = 0; i < frame.renderStates.size(); ++i) {
      bool all = true;
      for (size_t unit = 0; unit < 8; ++unit)
        all &= coin_render_texture_unit_enabled(frame.renderStates[i], unit);
      if (!all)
        continue;
      eight = true;
      if (!check(view.states && i < view.state_count,
                 "general stereo receiver FFI state addressable"))
        return false;
      const auto &state = view.states[i];
      for (size_t unit = 0; unit < 8; ++unit) {
        const bool enabled =
            unit ? state.extra_textures[unit - 1].enabled : state.has_texture;
        const auto image = unit ? state.extra_textures[unit - 1].texture_slot
                                : state.texture_slot;
        const auto sampler = unit ? state.extra_textures[unit - 1].sampler_slot
                                  : state.sampler_slot;
        ok &= check(enabled && image < view.texture_count &&
                        sampler < view.sampler_count,
                    "eight independent stereo receiver image/sampler bindings "
                    "survive FFI");
        if (scene.coverage && image < view.texture_count) {
          SbVec2s extent;
          int components = 0;
          const auto *source =
              scene.textures[unit]->image.getValue(extent, components);
          const auto &transport = view.textures[image];
          ok &= check(
              source && extent == SbVec2s(1, 1) && components == 4 &&
                  transport.width == 1 && transport.height == 1 &&
                  transport.pixels && transport.pixel_bytes_len == 4 &&
                  std::memcmp(source, transport.pixels, 4) == 0,
              "current independent stereo receiver texel bytes survive FFI");
        }
      }
    }
    ok &= check(eight, "eight-unit stereo receiver reaches Core");
  }
  const SbMatrix clipConversion(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, .5f, 0, 0, 0, .5f,
                                1);
  for (size_t passIndex = 0; passIndex < count; ++passIndex) {
    const auto &pass = plan.passes[passIndex];
    if (!check(pass.lightSlot < frame.shadowLights.size(),
               "stereo pass light index valid"))
      return false;
    const auto &light = frame.shadowLights[pass.lightSlot];
    size_t slot = count;
    for (size_t i = 0; i < count; ++i)
      if (light.sourceRevision == scene.lights[i]->getNodeId())
        slot = i;
    if (!check(slot < count && light.enabled && light.shadowEligible &&
                   light.mapSceneCaptured && light.groupSlot == 1,
               "stereo pass retains current authorial light and map identity"))
      return false;
    ok &= check(std::abs(light.intensity -
                         scene.lights[slot]->intensity.getValue()) < 1e-7 &&
                    light.customSceneNodeId == scene.maps[slot]->getNodeId(),
                "stereo map intensity/source identity survives capture");
    const CoinWgpuShadowDraw *casters = nullptr;
    uint64_t casterCount = 0;
    uint32_t mapSize = 0;
    if (passIndex == 0) {
      casters = view.shadow_casters;
      casterCount = view.shadow_caster_count;
      mapSize = view.shadow_map_size;
    } else if (passIndex == 1) {
      casters = view.shadow_casters_second;
      casterCount = view.shadow_caster_count_second;
      mapSize = view.shadow_map_size_second;
    } else if (passIndex == 2) {
      casters = view.shadow_casters_third;
      casterCount = view.shadow_caster_count_third;
      mapSize = view.shadow_map_size_third;
    } else if (passIndex == 3) {
      casters = view.shadow_casters_fourth;
      casterCount = view.shadow_caster_count_fourth;
      mapSize = view.shadow_map_size_fourth;
    } else {
      if (!check(view.extra_shadow_passes &&
                     passIndex - 4 < view.extra_shadow_pass_count,
                 "stereo extension slots5..8 addressable"))
        return false;
      const auto &extra = view.extra_shadow_passes[passIndex - 4];
      casters = extra.casters;
      casterCount = extra.caster_count;
      mapSize = extra.map_size;
    }
    ok &= check(mapSize == 256 && casters && casterCount,
                "each stereo pass has real map256/caster payload");
    std::vector<bool> seen(scene.occurrences[slot].size(), false);
    for (size_t occurrence = 0; occurrence < scene.occurrences[slot].size();
         ++occurrence) {
      const auto &occ = scene.occurrences[slot][occurrence];
      if (occ.external)
        continue;
      bool found = false;
      const auto expectedView = scene.view(slot, occurrence),
                 expectedProjection = scene.projection(slot, occurrence);
      // This fixture's before matrices contain Rz/T and uniform scale. A
      // transformed view volume measures near/far along its transformed -Z
      // axis. Compare in authorial distance units, retaining the existing1e-6
      // bound; raw view/projection matrices keep their unchanged1e-5 gate.
      const double distanceUnits =
          std::sqrt(double(occ.before[2][0]) * occ.before[2][0] +
                    double(occ.before[2][1]) * occ.before[2][1] +
                    double(occ.before[2][2]) * occ.before[2][2]);
      ok &= check(
          std::isfinite(distanceUnits) && distanceUnits > 0,
          "captured camera volume has finite nonzero authorial distance units");
      for (const auto &camera : frame.cameras)
        found |=
            camera.isPerspective ==
                (!scene.orthographic || scene.cameras[slot]->getStereoMode() !=
                                            SoCamera::MONOSCOPIC) &&
            std::abs(double(camera.nearDistance) / distanceUnits -
                     scene.cameras[slot]->nearDistance.getValue()) < 1e-6 &&
            std::abs(double(camera.farDistance) / distanceUnits -
                     scene.cameras[slot]->farDistance.getValue()) < 1e-6 &&
            stereoMatrixClose(camera.viewMatrix, expectedView) &&
            stereoMatrixClose(camera.projectionMatrixCoin, expectedProjection);
      ok &= check(found, "Core stereo eye/skew/effective perspective and "
                         "per-occurrence native model order retained");
      ok &= check(occ.viewport.visits > 0 && occ.viewport.dimensionsCorrect,
                  "stereo viewport callback sees actual map256");
    }
    for (uint64_t i = 0; i < casterCount; ++i) {
      const auto &packed = casters[i];
      if (!check(packed.render_state_slot < frame.renderStates.size(),
                 "stereo packed caster state valid"))
        return false;
      const auto &state = frame.renderStates[packed.render_state_slot];
      ok &= check(packed.reserved == 0, "stereo FFI reserved flags stay zero");
      if (scene.raster) {
        bool viewportMatched = false;
        for (size_t occurrence = 0; occurrence < scene.occurrences[slot].size();
             ++occurrence) {
          const auto vp = scene.viewport(slot, occurrence);
          viewportMatched |= packed.viewport[0] == int(vp.x) &&
                             packed.viewport[1] == 256 - int(vp.y + vp.h) &&
                             packed.viewport[2] == int(vp.w) &&
                             packed.viewport[3] == int(vp.h);
        }
        ok &= check(viewportMatched, "pixel caster FFI retains its own "
                                     "authored stereo viewport/scissor");
      }
      if (scene.raster)
        continue; // Pixel-sized primitives have their own raster remapping;
                  // projection is independently checked above and in RGB.
      SbMatrix actualMV, actualMVP;
      actualMV.setValue(packed.model_view);
      actualMVP.setValue(packed.model_view_projection);
      bool matched = false;
      for (size_t occurrence = 0; occurrence < scene.occurrences[slot].size();
           ++occurrence) {
        const auto &occ = scene.occurrences[slot][occurrence];
        if (occ.external) {
          if (!state.shadowMapCameraOverride &&
              stereoMatrixClose(state.model, scene.model(slot, occurrence))) {
            seen[occurrence] = true;
            matched = true;
            ok &= check(packed.viewport[0] == 0 && packed.viewport[1] == 0 &&
                            packed.viewport[2] == 256 &&
                            packed.viewport[3] == 256,
                        "Separator restores external caster full-map viewport "
                        "and light camera");
          }
          continue;
        }
        const auto vp = scene.viewport(slot, occurrence);
        const auto expectedMV =
            scene.model(slot, occurrence) * scene.drawView(slot, occurrence);
        const SbMatrix viewportTransform(
            float(vp.w / 256), 0, 0, 0, 0, float(vp.h / 256), 0, 0, 0, 0, 1, 0,
            float((2 * vp.x + vp.w) / 256 - 1),
            float((2 * vp.y + vp.h) / 256 - 1), 0, 1);
        const auto expectedMVP = expectedMV *
                                 scene.projection(slot, occurrence) *
                                 clipConversion * viewportTransform;
        if (stereoMatrixClose(actualMV, expectedMV) &&
            stereoMatrixClose(actualMVP, expectedMVP)) {
          matched = true;
          seen[occurrence] = true;
          ok &= check(state.shadowMapCameraOverride &&
                          packed.viewport[0] == int(vp.x) &&
                          packed.viewport[1] == 256 - int(vp.y + vp.h) &&
                          packed.viewport[2] == int(vp.w) &&
                          packed.viewport[3] == int(vp.h),
                      "FFI retains stereo occurrence viewport/scissor once");
          for (const auto &point : {SbVec3f(0, 0, 0), SbVec3f(.2f, -.1f, .06f),
                                    SbVec3f(-.17f, .13f, -.08f)}) {
            SbVec3f actual, expected;
            actualMVP.multVecMatrix(point, actual);
            expectedMVP.multVecMatrix(point, expected);
            ok &= check((actual - expected).length() < 1e-5,
                        "stereo FFI homogeneous projection probes preserve "
                        "eye, perspective and signed clip depth");
          }
        }
      }
      ok &= check(matched, "every geometry caster FFI matrix belongs to its "
                           "exact source occurrence");
    }
    if (!scene.raster)
      for (bool present : seen)
        ok &= check(
            present,
            "all shared/external stereo occurrences delivered independently");
  }
  return ok;
}
#include "CoinRenderShadowMapStereoMsaa.h"
bool stereoRender(Capture &capture, NearAudit &audit, StereoScene &scene,
                  Frame &frame, const std::string &label) {
  if (scene.shared)
    for (size_t i = 0; i < scene.cameras.size(); ++i)
      scene.probes[i].arm(scene.cameras[i]);
  const auto source = stereoAuthorState(scene);
  const auto oldSerial = capture.target->getLastSubmissionSerial();
  const auto count = audit.submissions;
  frame.stereoBeforeSerial = oldSerial;
  if (scene.coverage)
    frame.stereoInputs =
        stereoMsaaInputs(scene, side, capture.target->getOptions().sampleCount,
                         capture.target->getOptions().textureSamplingPolicy);
  const bool rendered = capture.gpu(scene.root, frame);
  bool ok = check(rendered && frame.serial > oldSerial &&
                      audit.submissions == count + 1,
                  label + " one successful logical publication");
  ok &= check(source == stereoAuthorState(scene),
              label + " camera fields, stereo state and source node IDs never "
                      "change during apply");
  if (scene.shared)
    for (const auto &probe : scene.probes)
      ok &= check(probe.same && probe.visits >= 2,
                  "public callback observes the original stereo "
                  "mode/adjustments in both shared-camera occurrences");
  if (rendered)
    ok &=
        stereoImage(scene, frame, label) && stereoDelivery(scene, audit) &&
        (!scene.coverage ||
         stereoCoverage(scene, frame, capture.target->getOptions().sampleCount,
                        side, false));
  return ok;
}
bool stereoRefuse(Capture &capture, NearAudit &audit, StereoScene &scene,
                  const Frame &stable, const std::string &label) {
  const auto source = stereoAuthorState(scene);
  const auto submissions = audit.submissions, producers = audit.producers;
  capture.action.apply(scene.root);
  Frame rejected;
  capture.target->readbackRGBA(rejected.rgba);
  capture.target->readbackDepth(rejected.depth);
  rejected.serial = capture.target->getLastSubmissionSerial();
  std::cout << "stereo_refusal " << label
            << " status=" << capture.action.getLastStatus()
            << " submits=" << submissions << ',' << audit.submissions
            << " error=" << capture.action.getLastError().getString() << '\n';
  return check(
      capture.action.getLastStatus() != CoinRenderAction::SUCCESS &&
          capture.action.getLastError().getLength() &&
          audit.submissions == submissions && audit.producers == producers &&
          rejected.serial == stable.serial && rejected.rgba == stable.rgba &&
          rejected.depth == stable.depth && source == stereoAuthorState(scene),
      label + " pre-submission rejection preserves complete "
              "RGBA/depth/serial/source fields");
}

// A mutation must contradict old/new interior predictions, rather than
// relying on a subpixel change at a shadow-map silhouette.
unsigned stereoInteriorChanges(const StereoShape &a, const StereoShape &b,
                               double lookupWidth) {
  unsigned changed = 0;
  for (int y = 12; y < side - 12; ++y)
    for (int x = 12; x < side - 12; ++x) {
      const double mx = (.5 + ((x + .5 - side / 2.) / 32) / lookupWidth) * 256;
      const double my = (.5 + ((side / 2. - y - .5) / 32) / lookupWidth) * 256;
      changed += (stereoContains(a.inner, mx, my, 2.1) &&
                  !stereoContains(b.outer, mx, my, -2.1)) ||
                 (stereoContains(b.inner, mx, my, 2.1) &&
                  !stereoContains(a.outer, mx, my, -2.1));
    }
  return changed;
}

// Nine-pixel markers keep their pixel size. For joint composition, solve each
// distinct source camera's anchor so their safe interiors overlap at(128,128).
// Individual LEFT/RIGHT and restoration inputs above remain unchanged.
void stereoAlignJointMarkers(StereoScene &scene) {
  if (scene.raster != 2)
    return;
  if (scene.coverage && scene.shared) {
    stereoAlignMsaaSharedMarkers(scene);
    return;
  }
  for (size_t slot = 0; slot < scene.cameras.size(); ++slot) {
    auto *camera = scene.cameras[slot];
    const auto position = camera->position.getValue();
    const auto viewport = scene.viewport(slot, 0);
    double width, height;
    scene.nearDimensions(slot, 0, width, height);
    const double n = camera->nearDistance.getValue();
    const double focal = std::max(n, double(camera->focalDistance.getValue()) *
                                         camera->getBalanceAdjustment());
    const double eye =
        (camera->getStereoMode() == SoCamera::LEFT_VIEW ? -1 : 1) *
        camera->getStereoAdjustment() / 2.;
    const double distance = position[2] - .35;
    const double ndcX = 2 * (128 - viewport.x) / viewport.w - 1;
    const double ndcY = 2 * (128 - viewport.y) / viewport.h - 1;
    scene.occurrences[slot][0].move->translation.setValue(
        float(position[0] + eye +
              distance * (ndcX * width / (2 * n) - eye / focal)),
        float(position[1] + distance * ndcY * height / (2 * n)), .35f);
  }
}

bool stereoOrder(Capture &capture, NearAudit &audit, StereoScene &scene) {
  stereoAlignJointMarkers(scene);
  scene.together();
  Frame a, b, back;
  bool ok =
      stereoRender(capture, audit, scene, a, "joint nonsaturating stereo maps");
  for (auto *light : scene.lights) {
    light->ref();
    scene.group->removeChild(light);
  }
  int index = 0;
  for (auto it = scene.lights.rbegin(); it != scene.lights.rend(); ++it)
    scene.group->insertChild(*it, index++);
  ok &=
      stereoRender(capture, audit, scene, b, "stereo map slot order reversed");
  for (auto *light : scene.lights)
    scene.group->removeChild(light);
  for (size_t i = 0; i < scene.lights.size(); ++i) {
    scene.group->insertChild(scene.lights[i], int(i));
    scene.lights[i]->unref();
  }
  ok &=
      stereoRender(capture, audit, scene, back, "stereo map order restored") &&
      stereoRestore(capture, audit, scene, a, back,
                    "joint stereo map order exact A/B/A");
  if (scene.general)
    for (size_t unit = 0; unit < 8; ++unit) {
      scene.colorUnit(unit, true);
      ok &= stereoRender(capture, audit, scene, b,
                         "stereo eight-unit visible binding=" +
                             std::to_string(unit));
      ok &= check(
          nearDifference(a, b) > 500,
          "each stereo receiver texture unit has independent visual effect");
      scene.colorUnit(unit, false);
      ok &= stereoRender(capture, audit, scene, back,
                         "stereo unit binding restored") &&
            stereoRestore(capture, audit, scene, a, back,
                          "stereo texture exact A/B/A");
    }
  return ok;
}
bool stereoProfiles(Capture &capture, NearAudit &audit, int count,
                    bool directional, bool orthographic, int mapping,
                    bool general, int raster, int selectedSlot = -1) {
  StereoScene scene(count, directional, orthographic, mapping, raster, general);
  stereoPrepareMsaaScene(scene, capture.target->getOptions().sampleCount);
  bool ok = true;
  Frame left, right, back, changed;
  for (int slot = 0; slot < count; ++slot) {
    if (selectedSlot >= 0 && selectedSlot != slot)
      continue;
    scene.isolate(slot);
    auto *camera = scene.cameras[slot];
    camera->setStereoMode(SoCamera::LEFT_VIEW);
    const auto sourceId = camera->getNodeId();
    ok &= stereoRender(capture, audit, scene, left,
                       "LEFT map=" + std::to_string(slot));
    const auto leftShapes = stereoSourceShapes(scene, slot);
    camera->setStereoMode(SoCamera::RIGHT_VIEW);
    if (scene.coverage)
      ok &= check(
          stereoSourceChanges(scene, leftShapes,
                              stereoSourceShapes(scene, slot), side) >= 6,
          "MSAA LEFT/RIGHT has at least18 contradictory interior RGB channels");
    ok &= check(camera->getNodeId() == sourceId,
                "RIGHT setter does not touch source node ID");
    ok &= stereoRender(capture, audit, scene, right,
                       "RIGHT map=" + std::to_string(slot));
    ok &= check(nearDifference(left, right) > 16,
                "signed eye offset has a measurable RGB effect");
    camera->setStereoMode(SoCamera::LEFT_VIEW);
    ok &= check(camera->getNodeId() == sourceId,
                "LEFT restoration does not touch source node ID");
    ok &= stereoRender(capture, audit, scene, back, "LEFT restored") &&
          stereoRestore(capture, audit, scene, left, back,
                        "stereo eye exact A/B/A without artificial touch");
    if (!raster) {
      const float adjustment = camera->getStereoAdjustment(),
                  balance = camera->getBalanceAdjustment(),
                  focal = camera->focalDistance.getValue();
      const auto originalShape = stereoShape(scene, slot, 0);
      double frustumWidth, frustumHeight;
      scene.nearDimensions(slot, 0, frustumWidth, frustumHeight);
      const auto viewport = scene.viewport(slot, 0);
      const double n = camera->nearDistance.getValue();
      const double effectiveFocal = std::max(n, double(focal) * balance);
      // Cube front z=.45 is4.55 from its camera. Since focal<4.55,
      // this is the least-sensitive corner depth; farther vertices move more.
      const double sensitivity =
          viewport.w * n / frustumWidth * (1 / effectiveFocal - 1 / 4.55);
      const double delta = 2 * (2 * 2.1 + 2) / sensitivity;
      ok &= check(std::isfinite(delta) && sensitivity > 0,
                  "authorial adjustment has a finite signed pixel sensitivity");
      camera->setStereoAdjustment(float(adjustment + delta));
      const unsigned expectedChanges = stereoInteriorChanges(
          originalShape, stereoShape(scene, slot, 0), scene.lookupWidth());
      std::cout << "stereo_adjustment_fixture slot=" << slot
                << " old_adjustment=" << adjustment
                << " new_adjustment=" << camera->getStereoAdjustment()
                << " minimum_map_shift=" << sensitivity * delta / 2
                << " safe_changed_pixels=" << expectedChanges << '\n';
      // Six fully lit/occluded pixels change all three nonzero RGB channels:
      // at least18 channels, independently of silhouette rasterization.
      ok &= check(expectedChanges >= 6, "adjustment mutation has more than16 "
                                        "guaranteed interior RGB channels");
      ok &= check(camera->getNodeId() == sourceId,
                  "stereo adjustment setter keeps NodeId unchanged");
      ok &= stereoRender(capture, audit, scene, changed,
                         "non-field stereo adjustment mutation");
      ok &=
          check(nearDifference(left, changed) > 16,
                "stereo adjustment invalidates reused map projection visibly");
      camera->setStereoAdjustment(adjustment);
      ok &= stereoRender(capture, audit, scene, back, "adjustment restored") &&
            stereoRestore(capture, audit, scene, left, back,
                          "adjustment exact A/B/A");
      // A small original eye makes changes of skew subpixel. Give these
      // two independent parameters their own authored base. At focal=4.65,
      // the caster center lies on the convergence plane; its baseline stays
      // measurable even with the separation needed to move safe interiors.
      const float parameterFocal = 4.65f;
      const double k = viewport.w * n / frustumWidth;
      const double balanceGain = std::abs(1 / (double(parameterFocal) * 1.7f) -
                                          1 / double(parameterFocal));
      const double focalGain = std::abs(1 / (double(parameterFocal) + 2) -
                                        1 / double(parameterFocal));
      const double parameterSeparation =
          2 * (2 * 2.1 + 2) / (k * std::min(balanceGain, focalGain));
      ok &=
          check(std::isfinite(parameterSeparation) && parameterSeparation > 0,
                "independent balance/focal base has finite pixel sensitivity");
      camera->focalDistance = parameterFocal;
      camera->setBalanceAdjustment(1);
      camera->setStereoAdjustment(float(parameterSeparation));
      const auto parameterSourceId = camera->getNodeId();
      const auto parameterShape = stereoShape(scene, slot, 0);
      Frame parameterBase;
      ok &= stereoRender(capture, audit, scene, parameterBase,
                         "authorial convergence-plane balance/focal base");
      camera->setBalanceAdjustment(1.7f);
      ok &= check(camera->getNodeId() == parameterSourceId,
                  "balance setter keeps its own base source NodeId unchanged");
      const unsigned balanceChanges = stereoInteriorChanges(
          parameterShape, stereoShape(scene, slot, 0), scene.lookupWidth());
      std::cout << "stereo_balance_fixture slot=" << slot
                << " separation=" << parameterSeparation
                << " safe_changed_pixels=" << balanceChanges << '\n';
      ok &= check(
          balanceChanges >= 6,
          "balance mutation guarantees more than16 interior RGB channels");
      ok &= stereoRender(capture, audit, scene, changed,
                         "non-field balance mutation on its observable base");
      ok &= check(nearDifference(parameterBase, changed) > 16,
                  "balance invalidates reused stereo skew visibly");
      camera->setBalanceAdjustment(1);
      ok &= check(camera->getNodeId() == parameterSourceId,
                  "balance restoration leaves base NodeId unchanged");
      ok &= stereoRender(capture, audit, scene, back,
                         "balance own base restored") &&
            stereoRestore(capture, audit, scene, parameterBase, back,
                          "balance own-base exact A/B/A");
      camera->focalDistance = parameterFocal + 2;
      const unsigned focalChanges = stereoInteriorChanges(
          parameterShape, stereoShape(scene, slot, 0), scene.lookupWidth());
      std::cout << "stereo_focal_fixture slot=" << slot
                << " safe_changed_pixels=" << focalChanges << '\n';
      ok &=
          check(focalChanges >= 6,
                "focal mutation guarantees more than16 interior RGB channels");
      ok &= stereoRender(capture, audit, scene, changed,
                         "focal distance mutation on the same camera base");
      ok &= check(nearDifference(parameterBase, changed) > 16,
                  "focal distance independently affects stereo skew");
      camera->focalDistance = parameterFocal;
      ok &= stereoRender(capture, audit, scene, back,
                         "focal own base restored") &&
            stereoRestore(capture, audit, scene, parameterBase, back,
                          "focal own-base exact A/B/A");
      camera->focalDistance = focal;
      camera->setBalanceAdjustment(balance);
      camera->setStereoAdjustment(adjustment);
      ok &= stereoRender(capture, audit, scene, back,
                         "original LEFT parameters restored") &&
            stereoRestore(capture, audit, scene, left, back,
                          "original camera parameters exact A/B/A");
    }
  }
  if (mapping == SoCamera::CROP_VIEWPORT_NO_FRAME) {
    scene.together();
    ok &= stereoRender(capture, audit, scene, left,
                       "CROP no-frame stereo control");
    for (int decoration : {SoCamera::CROP_VIEWPORT_FILL_FRAME,
                           SoCamera::CROP_VIEWPORT_LINE_FRAME}) {
      for (auto *camera : scene.cameras)
        camera->viewportMapping = decoration;
      ok &= stereoRender(capture, audit, scene, changed,
                         "stereo CROP decorative frame") &&
            stereoRestore(capture, audit, scene, left, changed,
                          "decorative CROP frame is not a shadow caster");
    }
    for (auto *camera : scene.cameras)
      camera->viewportMapping = SoCamera::CROP_VIEWPORT_NO_FRAME;
    ok &= stereoRender(capture, audit, scene, back,
                       "stereo CROP mapping restored") &&
          stereoRestore(capture, audit, scene, left, back,
                        "stereo CROP exact A/B/A");
  }
  // An original perspective camera with zero separation has the same frustum
  // in mono and stereo. An orthographic camera does NOT satisfy this identity.
  if (!orthographic) {
    scene.isolate(0);
    auto *camera = scene.cameras[0];
    const float adjustment = camera->getStereoAdjustment();
    camera->setStereoAdjustment(0);
    camera->setStereoMode(SoCamera::MONOSCOPIC);
    ok &= stereoRender(capture, audit, scene, left,
                       "perspective mono zero-eye control");
    camera->setStereoMode(SoCamera::LEFT_VIEW);
    ok &= stereoRender(capture, audit, scene, right,
                       "perspective LEFT zero separation") &&
          stereoRestore(capture, audit, scene, left, right,
                        "zero-eye perspective stereo is algebraically mono",
                        false);
    camera->setStereoAdjustment(adjustment);
  }
  ok &= stereoOrder(capture, audit, scene);
  return ok;
}
bool stereoFiniteFocal(Capture &capture, NearAudit &audit, StereoScene &scene,
                       int slot) {
  auto *camera = scene.cameras[slot];
  scene.isolate(slot);
  camera->setStereoMode(SoCamera::LEFT_VIEW);
  const float focal = camera->focalDistance.getValue(),
              balance = camera->getBalanceAdjustment();
  Frame first, changed, back, stable;
  bool ok = stereoRender(capture, audit, scene, stable, "finite focal control");
  camera->focalDistance = 0;
  ok &= stereoRender(capture, audit, scene, first,
                     "focal zero clamps to positive near");
  camera->focalDistance = -2;
  ok &= stereoRender(capture, audit, scene, changed,
                     "finite negative focal clamps to near") &&
        stereoRestore(capture, audit, scene, first, changed,
                      "finite focal0/negative native clamp equivalence", false);
  camera->focalDistance = focal;
  const auto sourceId = camera->getNodeId();
  camera->setBalanceAdjustment(0);
  ok &= check(camera->getNodeId() == sourceId,
              "zero balance is a non-field adjustment");
  ok &= stereoRender(capture, audit, scene, changed,
                     "balance zero clamps focal product to near") &&
        stereoRestore(capture, audit, scene, first, changed,
                      "finite zero balance native clamp equivalence", false);
  camera->setBalanceAdjustment(-1);
  ok &=
      stereoRender(capture, audit, scene, changed,
                   "finite negative balance clamps product") &&
      stereoRestore(capture, audit, scene, first, changed,
                    "finite negative balance native clamp equivalence", false);
  camera->setBalanceAdjustment(balance);
  ok &= stereoRender(capture, audit, scene, back,
                     "finite focal product restored") &&
        stereoRestore(capture, audit, scene, stable, back,
                      "finite focal exact recovery");
  return ok;
}
bool stereoState(Capture &capture, NearAudit &audit, int count,
                 bool directional, bool orthographic, int mapping, bool general,
                 int selectedSlot = -1) {
  StereoScene scene(count, directional, orthographic, mapping, 0, general);
  stereoPrepareMsaaScene(scene, capture.target->getOptions().sampleCount);
  bool ok = true;
  Frame stable, back;
  for (int slot = 0; slot < count; ++slot) {
    if (selectedSlot >= 0 && selectedSlot != slot)
      continue;
    scene.isolate(slot);
    auto *camera = scene.cameras[slot];
    camera->setStereoMode(SoCamera::LEFT_VIEW);
    ok &= stereoRender(capture, audit, scene, stable,
                       "valid stereo state map=" + std::to_string(slot));
    const float nearValue = camera->nearDistance.getValue(),
                farValue = camera->farDistance.getValue(),
                focal = camera->focalDistance.getValue();
    const float adjustment = camera->getStereoAdjustment(),
                balance = camera->getBalanceAdjustment();
    const auto orientation = camera->orientation.getValue();
    const float size =
        orthographic
            ? static_cast<SoOrthographicCamera *>(camera)->height.getValue()
            : static_cast<SoPerspectiveCamera *>(camera)
                  ->heightAngle.getValue();
    const float nan = std::numeric_limits<float>::quiet_NaN(),
                inf = std::numeric_limits<float>::infinity();
    for (int invalid = 0; invalid < 23; ++invalid) {
      switch (invalid) {
      case 0:
        camera->nearDistance = nan;
        break;
      case 1:
        camera->nearDistance = inf;
        break;
      case 2:
        camera->farDistance = nan;
        break;
      case 3:
        camera->farDistance = inf;
        break;
      case 4:
        camera->setStereoAdjustment(nan);
        break;
      case 5:
        camera->setStereoAdjustment(inf);
        break;
      case 6:
        camera->setBalanceAdjustment(nan);
        break;
      case 7:
        camera->setBalanceAdjustment(inf);
        break;
      case 8:
        camera->focalDistance = nan;
        break;
      case 9:
        camera->focalDistance = inf;
        break;
      case 10:
        camera->nearDistance = 0;
        break;
      case 11:
        camera->nearDistance = -1;
        break;
      case 12:
        camera->farDistance = nearValue;
        break;
      case 13:
        camera->farDistance = nearValue - .5f;
        break;
      case 14:
        // Unknown API value within the enum representation range (UBSan-safe).
        camera->setStereoMode(static_cast<SoCamera::StereoMode>(3));
        break;
      case 15:
        camera->focalDistance = std::numeric_limits<float>::max();
        camera->setBalanceAdjustment(std::numeric_limits<float>::max());
        break;
      case 16:
        camera->setStereoAdjustment(-inf);
        break;
      case 17:
        camera->setBalanceAdjustment(-inf);
        break;
      case 18:
        camera->focalDistance = -inf;
        break;
      case 19:
        camera->farDistance = -inf;
        break;
      case 20:
        camera->nearDistance = -inf;
        break;
      case 21:
        camera->orientation.setValue(SbRotation(nan, 0, 0, 1));
        break;
      case 22:
        if (orthographic)
          static_cast<SoOrthographicCamera *>(camera)->height = 0;
        else
          static_cast<SoPerspectiveCamera *>(camera)->heightAngle = 0;
        break;
      }
      ok &= stereoRefuse(capture, audit, scene, stable,
                         "slot=" + std::to_string(slot) +
                             " invalid=" + std::to_string(invalid));
      camera->nearDistance = nearValue;
      camera->farDistance = farValue;
      camera->focalDistance = focal;
      camera->setStereoAdjustment(adjustment);
      camera->setBalanceAdjustment(balance);
      camera->setStereoMode(SoCamera::LEFT_VIEW);
      camera->orientation = orientation;
      if (orthographic)
        static_cast<SoOrthographicCamera *>(camera)->height = size;
      else
        static_cast<SoPerspectiveCamera *>(camera)->heightAngle = size;
      ok &= stereoRender(capture, audit, scene, back,
                         "invalid stereo slot restored") &&
            stereoRestore(capture, audit, scene, stable, back,
                          "invalid stereo exact RGBA/depth recovery");
      stable = back;
    }
    if (scene.coverage) {
      auto &viewport = scene.occurrences[slot][0].viewport;
      const int kind = viewport.kind;
      const double left = viewport.left;
      for (int invalidViewport = 0; invalidViewport < 2; ++invalidViewport) {
        viewport.kind = invalidViewport ? 1 : 5;
        if (invalidViewport)
          viewport.left = 32768. / 256;
        ok &= stereoRefuse(capture, audit, scene, stable,
                           "invalid stereo MSAA viewport slot=" +
                               std::to_string(slot));
        viewport.kind = kind;
        viewport.left = left;
        ok &= stereoRender(capture, audit, scene, back,
                           "stereo viewport recovered") &&
              stereoRestore(capture, audit, scene, stable, back,
                            "stereo viewport recovery has current delivery");
        stable = back;
      }
    }
    ok &= stereoFiniteFocal(capture, audit, scene, slot);
  }
  // Same node traversed twice under distinct noncommuting models/viewports.
  // A third caster outside both Separators must use the light camera again.
  StereoScene shared(count, directional, orthographic, mapping, 0, general,
                     true);
  stereoPrepareMsaaScene(shared, capture.target->getOptions().sampleCount);
  for (int slot = 0; slot < count; ++slot) {
    if (selectedSlot >= 0 && selectedSlot != slot)
      continue;
    shared.isolate(slot);
    auto *camera = shared.cameras[slot];
    camera->setStereoMode(SoCamera::LEFT_VIEW);
    const auto sourceId = camera->getNodeId();
    Frame a, b, c;
    ok &= stereoRender(capture, audit, shared, a,
                       "two shared-camera occurrences and external caster");
    const auto sharedLeft = shared.coverage ? stereoSourceShapes(shared, slot)
                                            : std::vector<StereoShape>{};
    camera->setStereoMode(SoCamera::RIGHT_VIEW);
    if (shared.coverage)
      ok &= check(
          stereoSourceChanges(shared, sharedLeft,
                              stereoSourceShapes(shared, slot), side) >= 6,
          "shared stereo eye mutation has contradictory safe interiors");
    ok &= check(camera->getNodeId() == sourceId,
                "shared camera eye mutation leaves NodeId unchanged");
    ok &= stereoRender(capture, audit, shared, b,
                       "shared camera RIGHT in both contexts");
    ok &=
        check(nearDifference(a, b) > 16,
              "both shared camera occurrences update without artificial touch");
    camera->setStereoMode(SoCamera::LEFT_VIEW);
    ok &= stereoRender(capture, audit, shared, c,
                       "shared camera contexts restored") &&
          stereoRestore(capture, audit, shared, a, c,
                        "shared camera/models/viewports exact A/B/A");
  }
  return ok;
}

// Change map-camera model units, not the observer or ordinary receiver.
// For an origin camera and B=sI, caster model T*B and draw/reset view
// B^-1*V yield T*V: a finite measurable footprint for either unit scale.
// Raw camera snapshot V*B^-1 is checked separately by stereoDelivery.
SbMatrix stereoDiagonal(double x, double y, double z) {
  return SbMatrix(float(x), 0, 0, 0, 0, float(y), 0, 0, 0, 0, float(z), 0, 0, 0,
                  0, 1);
}
bool stereoScales(Capture &capture, NearAudit &audit, int count,
                  bool directional, bool orthographic, int mapping,
                  bool general, int selectedSlot = -1) {
  StereoScene scene(count, directional, orthographic, mapping, 0, general);
  stereoPrepareMsaaScene(scene, capture.target->getOptions().sampleCount);
  bool ok = true;
  for (int slot = 0; slot < count; ++slot) {
    if (selectedSlot >= 0 && selectedSlot != slot)
      continue;
    scene.isolate(slot);
    auto *camera = scene.cameras[slot];
    auto &occ = scene.occurrences[slot][0];
    const auto originalMode = camera->getStereoMode();
    for (auto mode :
         {SoCamera::LEFT_VIEW, SoCamera::RIGHT_VIEW, SoCamera::MONOSCOPIC}) {
      camera->setStereoMode(mode);
      const auto position = camera->position.getValue();
      const auto move = occ.move->translation.getValue();
      const float eye = camera->getStereoAdjustment();
      const float focal = camera->focalDistance.getValue();
      Frame original, current, back;
      ok &= stereoRender(capture, audit, scene, original,
                         "scale control original");
      for (float scale : {1e-5f, 1e5f}) {
        camera->position.setValue(0, 0, 0);
        camera->setStereoAdjustment(.2f);
        camera->focalDistance = 3.2f;
        occ.move->translation.setValue(0, 0, -5);
        occ.before = stereoDiagonal(scale, scale, scale);
        occ.beforeScale->scaleFactor.setValue(scale, scale, scale);
        // In mono ortho the centered original caster covers the complete
        // measured ROI. Half XY gives both safe interior and exterior while
        // the camera's before model still has determinant s^3.
        const float xy =
            orthographic && mode == SoCamera::MONOSCOPIC ? .5f : 1.f;
        occ.after = stereoDiagonal(xy, xy, 1);
        occ.afterScale->scaleFactor.setValue(xy, xy, 1);
        const double det = occ.before.det4();
        const SbMatrix inverse = occ.before.inverse();
        ok &= check(std::isfinite(det) && det != 0 &&
                        (scale > 1 || std::abs(det) < 1e-12) &&
                        std::isfinite(inverse[0][0]) && inverse[0][0] > 0 &&
                        std::abs(double(inverse[0][0]) * scale - 1) < 1e-6,
                    "finite invertible scale is valid independently of "
                    "absolute determinant");
        ok &= stereoRender(capture, audit, scene, current,
                           "valid uniform map-camera model scale=" +
                               std::to_string(scale));
        camera->position = position;
        camera->setStereoAdjustment(eye);
        camera->focalDistance = focal;
        occ.move->translation = move;
        occ.before = SbMatrix::identity();
        occ.beforeScale->scaleFactor.setValue(1, 1, 1);
        occ.after = SbMatrix::identity();
        occ.afterScale->scaleFactor.setValue(1, 1, 1);
        ok &=
            stereoRender(capture, audit, scene, back, "unit scale restored") &&
            stereoRestore(capture, audit, scene, original, back,
                          "valid map-camera unit scale exact A/B/A");
      }
      if (orthographic) {
        auto *ortho = static_cast<SoOrthographicCamera *>(camera);
        const float height = ortho->height.getValue();
        // Stereo orthographic uses a frustum. Expand X/Y geometry and the
        // frustum by the same authored factor, leaving positive near/far and Z
        // untouched. Its projection determinant is tiny but inverse finite.
        ortho->height = height * 1e8f;
        const float xy = mode == SoCamera::MONOSCOPIC ? .5e8f : 1e8f;
        occ.after = stereoDiagonal(xy, xy, 1);
        occ.afterScale->scaleFactor.setValue(xy, xy, 1);
        const SbMatrix projection = scene.projection(slot, 0);
        const SbMatrix inverse = projection.inverse();
        ok &= check(
            projection.det4() != 0 && std::abs(projection.det4()) < 1e-12 &&
                std::isfinite(inverse[0][0]) && std::isfinite(inverse[1][1]),
            "wide finite stereo frustum remains invertible at a small "
            "determinant");
        ok &= stereoRender(capture, audit, scene, current,
                           "wide invertible stereo frustum");
        ortho->height = height;
        occ.after = SbMatrix::identity();
        occ.afterScale->scaleFactor.setValue(1, 1, 1);
        ok &= stereoRender(capture, audit, scene, back,
                           "wide frustum restored") &&
              stereoRestore(capture, audit, scene, original, back,
                            "wide projection exact A/B/A");
      }
      // Genuine singularity/non-finite/float-inverse overflow are distinct from
      // a small absolute determinant. Every refusal preserves the old frame.
      for (float invalid : {0.f, std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::denorm_min()}) {
        occ.beforeScale->scaleFactor.setValue(invalid, 1, 1);
        ok &= stereoRefuse(
            capture, audit, scene, back,
            "singular/nonfinite/unrepresentable inverse map-camera transform");
        occ.beforeScale->scaleFactor.setValue(1, 1, 1);
        ok &= stereoRender(capture, audit, scene, back,
                           "invalid scale recovery") &&
              stereoRestore(capture, audit, scene, original, back,
                            "invalid model scale exact A/B/A");
      }
    }
    camera->setStereoMode(originalMode);
  }
  return ok;
}
