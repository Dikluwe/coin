// Included inside ShadowMapCameraTest's anonymous namespace. These expectations
// come from the authored camera/frustum and viewport API, not captured
// matrices.
struct CropViewport {
  int kind = 0; // 0 full, 1 normalized, 2 pixels, 3 empty, 4 outside,
                // 5 nonfinite, 6 negative extent, 7 legal short-bound rectangle
  int mapSide = 256;
  unsigned visits = 0;
  bool dimensionsCorrect = true;
  bool negativeExtentRepresentable = false;
  double left = .125, bottom = .1875, width = .5, height = .625;
  static void install(void *data, SoAction *action) {
    if (!action->isOfType(SoCallbackAction::getClassTypeId()) &&
        !action->isOfType(SoGLRenderAction::getClassTypeId()))
      return;
    auto &self = *static_cast<CropViewport *>(data);
    SbViewportRegion viewport =
        SoViewportRegionElement::get(action->getState());
    ++self.visits;
    const auto window = viewport.getWindowSize();
    self.dimensionsCorrect &=
        window[0] == self.mapSide && window[1] == self.mapSide;
    if (self.kind == 1)
      viewport.setViewport(float(self.left), float(self.bottom),
                           float(self.width), float(self.height));
    else if (self.kind == 2)
      viewport.setViewportPixels(short(self.mapSide / 2 - 59),
                                 short(self.mapSide / 2 - 59), 128, 112);
    else if (self.kind == 3)
      viewport.setViewportPixels(0, 0, 0, 0);
    else if (self.kind == 4)
      viewport.setViewportPixels(short(self.mapSide + 13), 0, 128, 112);
    else if (self.kind == 8)
      viewport.setViewportPixels(short(self.left), short(self.bottom),
                                 short(self.width), short(self.height));
    else if (self.kind == 5)
      viewport.setViewport(std::numeric_limits<float>::quiet_NaN(), 0, .5f,
                           .5f);
    else if (self.kind == 6) {
      viewport.setViewportPixels(0, 0, -1, 16);
      self.negativeExtentRepresentable = viewport.getViewportSize()[0] < 0;
    } else if (self.kind == 7)
      viewport.setViewportPixels(32760, 0, 32760, 16);
    SoViewportRegionElement::set(action->getState(), viewport);
  }
  SoCallback *node() {
    auto *callback = new SoCallback;
    callback->setCallback(install, this);
    return callback;
  }
};

struct CropRect {
  double x = 0, y = 0, w = 0, h = 0;
  bool contains(double px, double py, double margin) const {
    return w > 0 && h > 0 && px > x + margin && px < x + w - margin &&
           py > y + margin && py < y + h - margin;
  }
};
CropRect cropIntersect(const CropRect &a, const CropRect &b) {
  CropRect result;
  result.x = std::max(a.x, b.x);
  result.y = std::max(a.y, b.y);
  result.w = std::max(0., std::min(a.x + a.w, b.x + b.w) - result.x);
  result.h = std::max(0., std::min(a.y + a.h, b.y + b.h) - result.y);
  return result;
}
CropRect cropAuthoredViewport(const CropViewport &v) {
  const double n = v.mapSide;
  CropRect result;
  if (v.kind == 1) {
    // Public API stores normalized components as floats. Keep those values
    // until camera scaling; incoming pixel aspect is rounded separately.
    result.x = double(float(v.left)) * n;
    result.y = double(float(v.bottom)) * n;
    result.w = double(float(v.width)) * n;
    result.h = double(float(v.height)) * n;
  } else if (v.kind == 2) {
    result.x = v.mapSide / 2 - 59;
    result.y = v.mapSide / 2 - 59;
    result.w = 128;
    result.h = 112;
  } else if (v.kind == 8) {
    result.x = v.left;
    result.y = v.bottom;
    result.w = v.width;
    result.h = v.height;
  } else if (v.kind == 3) {
    result.w = result.h = 0;
  } else if (v.kind == 4) {
    result.x = n + 13;
    result.w = 128;
    result.h = 112;
  } else if (v.kind == 7) {
    result.x = 32760;
    result.w = 32760;
    result.h = 16;
  } else {
    result.w = result.h = n;
  }
  return result;
}
double cropRoundPixel(double value) {
  return value >= 0 ? std::floor(value + .5) : std::ceil(value - .5);
}
CropRect cropPixelViewport(CropRect viewport) {
  viewport.x = cropRoundPixel(viewport.x);
  viewport.y = cropRoundPixel(viewport.y);
  viewport.w = cropRoundPixel(viewport.w);
  viewport.h = cropRoundPixel(viewport.h);
  return viewport;
}
CropRect cropCameraViewport(CropRect viewport, double aspect, int mapSide) {
  // Camera ratio uses the incoming integer pixel dimensions. Region scaling
  // operates on its stored normalized FLOAT components, centered first, then
  // clamps only the scaled extent to one and its origin to zero. Conversion
  // back to pixels rounds halves away from zero. This is the public region
  // API contract, independent of captured matrices and GPU output.
  if (viewport.w <= 0 || viewport.h <= 0)
    return cropPixelViewport(viewport);
  const float inputRatio =
      float(cropRoundPixel(viewport.w)) / float(cropRoundPixel(viewport.h));
  const float cameraRatio = float(aspect);
  float x = float(viewport.x / mapSide), y = float(viewport.y / mapSide);
  float width = float(viewport.w / mapSide),
        height = float(viewport.h / mapSide);
  if (cameraRatio < inputRatio) {
    const float old = width;
    width *= cameraRatio / inputRatio;
    x -= (width - old) / 2.f;
    width = std::min(width, 1.f);
    x = std::max(x, 0.f);
  } else if (cameraRatio > inputRatio) {
    const float old = height;
    height *= inputRatio / cameraRatio;
    y -= (height - old) / 2.f;
    height = std::min(height, 1.f);
    y = std::max(y, 0.f);
  }
  viewport.x = double(x) * mapSide;
  viewport.y = double(y) * mapSide;
  viewport.w = double(width) * mapSide;
  viewport.h = double(height) * mapSide;
  return cropPixelViewport(viewport);
}

void cropReceiver(Scene &scene, int executor) {
  if (!executor)
    return;
  auto *receiver = static_cast<SoSeparator *>(scene.group->getChild(2));
  auto *prefix = new SoGroup;
  auto *quality = new SoComplexity;
  quality->textureQuality = .1f;
  prefix->addChild(quality);
  for (int unit = 0; unit < 8; ++unit) {
    auto *select = new SoTextureUnit;
    select->unit = unit;
    prefix->addChild(select);
    auto *texture = new SoTexture2;
    const uint8_t white[] = {255, 255, 255, 255};
    texture->image.setValue(SbVec2s(1, 1), 4, white);
    prefix->addChild(texture);
    auto *coordinates = new SoTextureCoordinate2;
    const SbVec2f uv[] = {SbVec2f(0, 0), SbVec2f(1, 0), SbVec2f(1, 1),
                          SbVec2f(0, 1)};
    coordinates->point.setValues(0, 4, uv);
    prefix->addChild(coordinates);
  }
  auto *zero = new SoTextureUnit;
  zero->unit = 0;
  prefix->addChild(zero);
  if (executor == 2) {
    auto *bump = new SoBumpMap;
    const uint8_t normal[] = {128, 128, 255};
    bump->image.setValue(SbVec2s(1, 1), 3, normal);
    prefix->addChild(bump);
    auto *coordinates = new SoBumpMapCoordinate;
    const SbVec2f uv[] = {SbVec2f(0, 0), SbVec2f(1, 0), SbVec2f(1, 1),
                          SbVec2f(0, 1)};
    coordinates->point.setValues(0, 4, uv);
    prefix->addChild(coordinates);
  }
  receiver->insertChild(prefix, 1);
}

struct CropSetup {
  Scene scene;
  CropViewport viewport;
  SoCamera *camera = nullptr;
  SoSeparator *caster = nullptr;
  SoTranslation *move = nullptr;
  int light, cameraKind, placement, raster, executor;
  double aspect, casterX = .35;
  double casterWidth = 1.2, casterHeight = .9;
  double observerHeight = 4, observerX = 0;
  bool detachedSpot = false;
  CropSetup(int lightValue, int cameraValue, int mapSize, double cameraAspect,
            int viewportKind, int where, int rasterKind, int execution)
      : light(lightValue), cameraKind(cameraValue), placement(where),
        raster(rasterKind), executor(execution), aspect(cameraAspect) {
    scene.spot->on = light == 0;
    scene.directional->on = light == 1;
    scene.group->precision = float(mapSize) / 2048;
    if (light == 0 && raster == 1 && mapSize == 512) {
      // The spot projection has explicit near/far/cutoff independent of the
      // observer. CAD zoom gives <.5 map texel per receiver pixel instead of
      // skipping ~1.95 texels, without changing the bitmap or shadow map.
      observerHeight = 1;
      observerX = -.5;
      auto *observer =
          static_cast<SoOrthographicCamera *>(scene.root->getChild(0));
      observer->height = 1;
      observer->position.setValue(float(observerX), 0, 6);
      // Its ROI now crosses the text's left boundary: both lit exterior and
      // bitmap-overlap samples remain present at the finer sampling density.
    }
    scene.configure(light, cameraKind, 0, 0, float(casterX), raster);
    camera = cameraKind == 1
                 ? static_cast<SoCamera *>(scene.perspective[light])
                 : static_cast<SoCamera *>(scene.orthographic[light]);
    if (camera) {
      camera->viewportMapping = SoCamera::CROP_VIEWPORT_NO_FRAME;
      camera->aspectRatio = float(aspect);
    }
    auto *map = scene.maps[light];
    caster =
        static_cast<SoSeparator *>(map->getChild(map->getNumChildren() - 1));
    move = static_cast<SoTranslation *>(caster->getChild(1));
    if (!raster) {
      auto *cube = static_cast<SoCube *>(caster->getChild(2));
      cube->width = 1.2f;
      cube->height = .9f;
    } else if (raster == 1) {
      // A finite pixel-authored text block supplies measurable coverage even
      // when a 512-map is sampled by the 128 receiver. No pixel threshold or
      // glyph bitmap is inferred from the GPU output.
      static_cast<SoFont *>(caster->getChild(2))->size = 33;
      auto *text = static_cast<SoText2 *>(caster->getChild(3));
      text->spacing = 1;
      text->string.setNum(8);
      for (int line = 0; line < 8; ++line)
        text->string.set1Value(line, "MMMMMMMM");
    }
    viewport.kind = viewportKind;
    viewport.mapSide = mapSize;
    if (viewportKind)
      map->insertChild(viewport.node(), where == 0 ? 0 : (camera ? 1 : 0));
    cropReceiver(scene, executor);
    if (executor == 2) {
      // Bump's admitted profile has one captured directional light, not two
      // light nodes with one switched off. Keep the Scene pointer valid until
      // destruction, but remove the unused spot from the traversed graph.
      scene.spot->ref();
      scene.group->removeChild(scene.spot);
      detachedSpot = true;
    }
  }
  ~CropSetup() {
    if (detachedSpot)
      scene.spot->unref();
  }
  void setCasterSize(double width, double height) {
    casterWidth = width;
    casterHeight = height;
    auto *cube = static_cast<SoCube *>(caster->getChild(2));
    cube->width = float(width);
    cube->height = float(height);
  }
  CropRect projectionViewport() const {
    CropViewport full;
    full.mapSide = viewport.mapSide;
    if (!camera)
      return cropPixelViewport(cropAuthoredViewport(viewport));
    const auto before = placement == 0 ? cropAuthoredViewport(viewport)
                                       : cropAuthoredViewport(full);
    return placement == 1 && viewport.kind
               ? cropPixelViewport(cropAuthoredViewport(viewport))
               : cropCameraViewport(before, aspect, viewport.mapSide);
  }
  CropRect footprint(bool envelope = false) const {
    const auto vp = projectionViewport();
    CropRect map;
    map.w = map.h = viewport.mapSide;
    const double distance = 5 - (raster ? .35 : .59);
    const double height = cameraKind == 1   ? 2 * distance * std::tan(.5)
                          : cameraKind == 2 ? 4
                          : light == 0      ? 2 * distance * std::tan(.6)
                                            : std::sqrt(96.);
    const double width = height * (camera ? aspect : 1);
    CropRect shape;
    if (!raster) {
      shape.x = vp.x + (.5 + (casterX - casterWidth / 2) / width) * vp.w;
      shape.y = vp.y + (.5 - casterHeight / (2 * height)) * vp.h;
      shape.w = casterWidth / width * vp.w;
      shape.h = casterHeight / height * vp.h;
      if (envelope && (cameraKind == 1 || (!camera && light == 0))) {
        // Off-axis perspective cubes show a side as well as their front.
        // The front rectangle is a guaranteed covered interior; the bounds
        // of all eight corners enclose every side for the no-leakage gate.
        const double backHeight =
            2 * (5 - .11) * std::tan(cameraKind == 1 ? .5 : .6);
        const double backWidth = backHeight * (camera ? aspect : 1);
        const double backLeft =
            vp.x + (.5 + (casterX - casterWidth / 2) / backWidth) * vp.w;
        const double backRight =
            vp.x + (.5 + (casterX + casterWidth / 2) / backWidth) * vp.w;
        const double right = std::max(shape.x + shape.w, backRight);
        shape.x = std::min(shape.x, backLeft);
        shape.w = right - shape.x;
      }
    } else {
      const double anchorX = 2 * casterX / width;
      // Marker Bitmap retains its admitted-anchor contract. Image and Text2
      // instead operate in viewport-local pixels, including their negative
      // raster-position workaround; those two must not inherit an XY veto.
      if (raster >= 3 && std::abs(anchorX) > 1)
        return CropRect();
      // INITIAL defaultFont's size>=26 cell is 19x36 with bearingY=28
      // and horizontal advance19 (font API metrics, not GPU observations).
      // Eight glyphs/line have advance8*19=152. SoText2's line step is
      // int(int(fontsize)*spacing)=33. Eight lines therefore lie within
      // x=[0,152], y=[-8-7*33,28], before viewport/map intersection.
      const double pixelWidth = raster == 2 ? 17 : (raster >= 3 ? 9 : 152);
      const double pixelHeight = raster == 2 ? 17 : (raster >= 3 ? 9 : 267);
      shape.x = vp.x + (.5 + casterX / width) * vp.w -
                (raster == 1 ? 0 : (pixelWidth - 1) / 2);
      shape.y = vp.y + .5 * vp.h - (raster == 1 ? 239 : (pixelHeight - 1) / 2);
      shape.w = pixelWidth;
      shape.h = pixelHeight;
    }
    // Clipping original camera frustum is equivalent to clipping projected
    // geometry to its full viewport; the physical map adds a second scissor.
    return cropIntersect(cropIntersect(shape, vp), map);
  }
};

bool cropSame(const Frame &a, const Frame &b, const std::string &label) {
  return check(a.rgba == b.rgba && a.depth == b.depth, label);
}
bool cropOracle(const CropSetup &setup, const Frame &base, const Frame &shown,
                const std::string &label, bool expectVisible = true,
                const std::vector<CropRect> *occurrences = nullptr) {
  if (!check(base.rgba.size() == size_t(side * side * 4) &&
                 shown.rgba.size() == base.rgba.size() &&
                 shown.depth.size() == size_t(side * side),
             label + " full outputs"))
    return false;
  const auto rect = setup.footprint();
  const auto envelope = setup.footprint(true);
  unsigned dark = 0, light = 0, bad = 0, observed = 0;
  int maximum = 0;
  const bool exactRaster = setup.raster != 1;
  for (int y = 12; y < side - 12; ++y)
    for (int x = 12; x < side - 12; ++x) {
      const size_t p = size_t(y * side + x);
      const double wx =
          setup.observerX + (x + .5 - side / 2.) * setup.observerHeight / side;
      const double wy = (side / 2. - y - .5) * setup.observerHeight / side;
      const double lookupWidth =
          setup.light ? std::sqrt(96.) : 12 * std::tan(.6);
      const double mx = (.5 + wx / lookupWidth) * setup.viewport.mapSide;
      const double my = (.5 + wy / lookupWidth) * setup.viewport.mapSide;
      bool inside = exactRaster && rect.contains(mx, my, 2.1);
      bool outside = !envelope.contains(mx, my, -2.1);
      if (occurrences) {
        inside = false;
        outside = true;
        for (const auto &occurrence : *occurrences) {
          inside |= occurrence.contains(mx, my, 2.1);
          outside &= !occurrence.contains(mx, my, -2.1);
        }
      }
      int loss = 0;
      for (int c = 0; c < 3; ++c)
        loss += int(base.rgba[4 * p + c]) - int(shown.rgba[4 * p + c]);
      observed += loss > 18;
      if (inside || outside) {
        dark += inside;
        light += outside;
        for (int c = 0; c < 3; ++c) {
          // Material ambient .18 times the Coin global ambient .2; bump's
          // established equation deliberately contributes no ambient term.
          const int expected =
              inside ? (setup.executor == 2 ? 0 : 9) : base.rgba[4 * p + c];
          const int error = std::abs(int(shown.rgba[4 * p + c]) - expected);
          maximum = std::max(maximum, error);
          if (error > 3 && bad++ < 4)
            std::cerr << "crop_first " << label << " xy=" << x << ',' << y
                      << " map=" << mx << ',' << my << " inside=" << inside
                      << " expected=" << expected
                      << " actual=" << unsigned(shown.rgba[4 * p + c]) << '\n';
        }
      }
      bad += shown.rgba[4 * p + 3] != 255;
      bad += std::abs(shown.depth[p] - float(6. / 19)) > 1e-5;
    }
  const bool empty = rect.w <= 0 || rect.h <= 0;
  std::cout << "crop_oracle " << label << " map=" << setup.viewport.mapSide
            << " rect=" << rect.x << ',' << rect.y << ',' << rect.w << ','
            << rect.h << " inside=" << dark << " outside=" << light
            << " observed=" << observed << " bad=" << bad << " max=" << maximum
            << '\n';
  return check(
      bad == 0 && light > 100 &&
          (!expectVisible || empty ||
           (observed > 4 && (!exactRaster || dark > 0))),
      label + " independent frustum/viewport interiors, no leakage and depth");
}

std::string cropLabel(const CropSetup &s) {
  return "light=" + std::to_string(s.light) +
         "/camera=" + std::to_string(s.cameraKind) +
         "/aspect=" + std::to_string(s.aspect) +
         "/viewport=" + std::to_string(s.viewport.kind) +
         "/where=" + std::to_string(s.placement) +
         "/raster=" + std::to_string(s.raster) +
         "/executor=" + std::to_string(s.executor);
}
bool cropCase(Capture &capture, CropSetup &setup, bool threeMappings) {
  auto *map = setup.scene.maps[setup.light];
  setup.caster->ref();
  map->removeChild(setup.caster);
  Frame base, first, shown, recovered;
  bool ok =
      check(capture.gpu(setup.scene.root, base), "crop unshadowed control");
  map->addChild(setup.caster);
  setup.caster->unref();
  const int mappings[] = {SoCamera::CROP_VIEWPORT_NO_FRAME,
                          SoCamera::CROP_VIEWPORT_FILL_FRAME,
                          SoCamera::CROP_VIEWPORT_LINE_FRAME};
  for (unsigned mode = 0; mode < (threeMappings ? 3u : 1u); ++mode) {
    if (setup.camera)
      setup.camera->viewportMapping = mappings[mode];
    const bool rendered = capture.gpu(setup.scene.root, shown);
    ok &= check(rendered, cropLabel(setup) + " supported CROP publication");
    if (rendered) {
      ok &= cropOracle(setup, base, shown, cropLabel(setup));
      if (!mode)
        first = shown;
      else
        ok &= cropSame(first, shown, "CROP decoration is not a sampled caster");
    }
  }
  if (setup.camera)
    setup.camera->viewportMapping = SoCamera::CROP_VIEWPORT_NO_FRAME;
  // Geometry is clipped by the original frustum. Raster preserves its own
  // node-specific layout and clipping, but this far-away bitmap cannot overlap
  // the viewport for any of the authored pixel dimensions.
  setup.move->translation.setValue(12, 0, .35f);
  setup.casterX = 12;
  const bool clipped = capture.gpu(setup.scene.root, recovered);
  ok &= check(clipped, "outside original camera publishes a neutral map");
  if (clipped)
    ok &= cropOracle(setup, base, recovered, cropLabel(setup), false) &&
          cropSame(base, recovered,
                   "outside-frustum caster never leaks into map");
  // Cross the original +X frustum boundary while geometry remains partly
  // inside. This catches rendering the uncut caster into surrounding map
  // pixels when the authorial viewport is a subrectangle of the map.
  if (!setup.raster) {
    const double distance = 5 - .59;
    const double height = setup.cameraKind == 1   ? 2 * distance * std::tan(.5)
                          : setup.cameraKind == 2 ? 4
                          : setup.light == 0      ? 2 * distance * std::tan(.6)
                                                  : std::sqrt(96.);
    setup.casterX = height * (setup.camera ? setup.aspect : 1) / 2 + .02;
    setup.move->translation.setValue(float(setup.casterX), 0, .35f);
    const bool partial = capture.gpu(setup.scene.root, recovered);
    ok &= check(partial, "partial original-frustum caster publishes");
    if (partial)
      ok &= cropOracle(setup, base, recovered,
                       "partly clipped original frustum", false);
  }
  setup.casterX = .35;
  for (float z : {4.5f, 6.f}) {
    setup.move->translation.setValue(.35f, 0, z);
    const bool clippedZ = capture.gpu(setup.scene.root, recovered);
    ok &= check(clippedZ, "near/behind-camera clipping publishes neutral map");
    if (clippedZ)
      ok &= cropSame(
          base, recovered,
          "original clipZ survives geometry/raster viewport conversion");
  }
  setup.move->translation.setValue(.35f, 0, .35f);
  ok &= check(capture.gpu(setup.scene.root, recovered),
              "CROP clipping recovery") &&
        cropSame(first, recovered, "CROP caster A/B/A exact RGBA/depth");
  if (setup.viewport.kind)
    ok &=
        check(setup.viewport.visits > 0 && setup.viewport.dimensionsCorrect,
              "viewport callback observes the real map window, never fixed256");
  return ok;
}

bool cropGeometry(Capture &capture, int executor, int extent) {
  bool ok = true;
  for (int light = executor == 2 ? 1 : 0; light < 2; ++light)
    for (int camera : {1, 2})
      for (double aspect : {.5, .51, 2., 2.03}) {
        CropSetup setup(light, camera, extent ? extent : 256, aspect, 0, 0, 0,
                        executor);
        ok &= cropCase(capture, setup, true);
      }
  return ok;
}
bool cropViewports(Capture &capture, int executor, int extent) {
  bool ok = true;
  for (int n : {64, 256, 512}) {
    if (extent && extent != n)
      continue;
    for (int light = executor == 2 ? 1 : 0; light < 2; ++light)
      for (int placement : {0, 1, 2})
        for (int units : {1, 2}) {
          const int camera = placement == 2 ? 0 : 1 + (placement + n / 64) % 2;
          CropSetup setup(light, camera, n, placement ? 2.03 : .51, units,
                          placement == 1 ? 1 : 0, 0, executor);
          // Only N64 normalized subrectangles lack texel interiors at the
          // original size. Pixel viewports need the original caster to keep
          // both lit and shadowed witnesses in the observer's sampled ROI.
          if (n == 64 && units == 1)
            setup.setCasterSize(1.8, 1.4);
          ok &= cropCase(capture, setup, false);
        }
  }
  return ok;
}
bool cropRasters(Capture &capture, int executor, int extent) {
  bool ok = true;
  for (int n : {64, 256, 512}) {
    if (extent && extent != n)
      continue;
    for (int light = executor == 2 ? 1 : 0; light < 2; ++light)
      for (int raster : {1, 2, 3, 4})
        for (int camera : {1, 2}) {
          CropSetup setup(light, camera, n, raster % 2 ? .51 : 2.03,
                          raster % 2 ? 1 : 2, raster % 2, raster, executor);
          ok &= cropCase(capture, setup, true);
        }
  }
  // A world anchor just left of the camera still leaves visible Image/Text2
  // pixels. Markers use their existing anchor-visibility rule and disappear.
  // Put the viewport's left edge inside the observer's sampled map region so
  // a generic XY veto becomes a functional, independently observable error.
  for (int n : {64, 256, 512}) {
    if (extent && extent != n)
      continue;
    for (int raster : {1, 2, 3, 4}) {
      CropSetup setup(1, 2, n, 1, 1, 0, raster, executor);
      setup.viewport.left = .375;
      setup.viewport.bottom = .3125;
      setup.viewport.width = .25;
      setup.viewport.height = .375;
      auto *map = setup.scene.maps[1];
      setup.caster->ref();
      map->removeChild(setup.caster);
      Frame base, shown;
      const bool baseOk = capture.gpu(setup.scene.root, base);
      map->addChild(setup.caster);
      setup.caster->unref();
      setup.casterX = -2.02;
      setup.move->translation.setValue(float(setup.casterX), 0, .35f);
      const bool shownOk = capture.gpu(setup.scene.root, shown);
      ok &= check(baseOk && shownOk,
                  "node-specific outside-anchor overlap captured");
      if (baseOk && shownOk) {
        ok &=
            cropOracle(setup, base, shown,
                       "outside-left anchor raster=" + std::to_string(raster));
        if (raster >= 3)
          ok &= cropSame(base, shown, "marker original anchor remains clipped");
        else
          ok &= check(
              base.rgba != shown.rgba,
              "Image/Text2 negative raster position retains bitmap overlap");
      }
    }
  }
  return ok;
}

bool cropRefuse(Capture &capture, SoNode *root, const Frame &stable,
                const std::string &label) {
  capture.action.apply(root);
  Frame failed;
  capture.target->readbackRGBA(failed.rgba);
  capture.target->readbackDepth(failed.depth);
  failed.serial = capture.target->getLastSubmissionSerial();
  std::cout << "crop_refusal " << label
            << " status=" << capture.action.getLastStatus()
            << " serial=" << stable.serial << ',' << failed.serial
            << " diagnostic=" << capture.action.getLastError().getString()
            << '\n';
  return check(capture.action.getLastStatus() != CoinRenderAction::SUCCESS &&
                   capture.action.getLastError().getLength() &&
                   failed.serial == stable.serial &&
                   failed.rgba == stable.rgba && failed.depth == stable.depth,
               label + " invalid input preserves prior RGBA/depth/serial");
}
CropRect cropOccurrence(const CropRect &viewport, double aspect, double width,
                        int mapSide) {
  const auto vp = cropCameraViewport(viewport, aspect, mapSide);
  CropRect result;
  result.x = vp.x + (.5 - width / (8 * aspect)) * vp.w;
  result.y = vp.y + (.5 - width / 8) * vp.h;
  result.w = vp.w * width / (4 * aspect);
  result.h = vp.h * width / 4;
  return result;
}
bool cropSharedState(Capture &capture, int executor, int n) {
  CropSetup setup(1, 2, n, .51, 0, 0, 0, executor);
  auto *map = setup.scene.maps[1];
  setup.caster->ref();
  map->removeChild(setup.caster);
  auto *sharedCamera = new SoOrthographicCamera;
  sharedCamera->position.setValue(0, 0, 5);
  sharedCamera->height = 4;
  sharedCamera->nearDistance = 1;
  sharedCamera->farDistance = 10;
  sharedCamera->aspectRatio = 1;
  sharedCamera->viewportMapping = SoCamera::CROP_VIEWPORT_NO_FRAME;
  auto *common = new SoSwitch;
  common->whichChild = SO_SWITCH_NONE;
  common->addChild(cubeCaster(0));
  CropViewport viewports[2];
  for (unsigned i = 0; i < 2; ++i) {
    viewports[i].kind = 1;
    viewports[i].mapSide = n;
    viewports[i].left = i ? .4 : .2;
    viewports[i].bottom = .1;
    viewports[i].width = .4;
    viewports[i].height = .8;
    auto *scope = new SoSeparator;
    scope->addChild(viewports[i].node());
    scope->addChild(sharedCamera);
    scope->addChild(common);
    map->addChild(scope);
  }
  auto *tail = new SoSwitch;
  tail->whichChild = SO_SWITCH_NONE;
  tail->addChild(cubeCaster(0));
  map->addChild(tail);
  Frame base, first, changed, restored;
  bool ok =
      check(capture.gpu(setup.scene.root, base), "shared crop empty control");
  common->whichChild = SO_SWITCH_ALL;
  tail->whichChild = SO_SWITCH_ALL;
  std::vector<CropRect> footprints;
  auto expected = [&]() {
    footprints.clear();
    for (const auto &viewport : viewports)
      footprints.push_back(
          cropOccurrence(cropAuthoredViewport(viewport), 1, .4, n));
    CropViewport full;
    full.mapSide = n;
    footprints.push_back(
        cropOccurrence(cropAuthoredViewport(full), .51, .4, n));
  };
  expected();
  ok &= check(capture.gpu(setup.scene.root, first),
              "shared camera two viewports plus restored tail") &&
        cropOracle(setup, base, first, "shared camera occurrence union", true,
                   &footprints);
  viewports[0].left = .1;
  expected();
  ok &= check(capture.gpu(setup.scene.root, changed),
              "one occurrence viewport mutation") &&
        cropOracle(setup, base, changed, "one occurrence changed", true,
                   &footprints) &&
        check(first.rgba != changed.rgba,
              "occurrence-specific viewport observably moves only its caster");
  viewports[0].left = .2;
  expected();
  ok &=
      check(capture.gpu(setup.scene.root, restored), "shared viewport A/B/A") &&
      cropSame(first, restored, "shared viewport/camera restoration exact");
  sharedCamera->position.setValue(std::numeric_limits<float>::quiet_NaN(), 0,
                                  5);
  ok &=
      cropRefuse(capture, setup.scene.root, restored, "invalid shared camera");
  sharedCamera->position.setValue(0, 0, 5);
  ok &= check(capture.gpu(setup.scene.root, restored),
              "shared camera finite recovery") &&
        cropSame(first, restored, "shared camera invalid/recovery A/B/A exact");
  sharedCamera->viewportMapping = 99;
  ok &= cropRefuse(capture, setup.scene.root, restored,
                   "unknown shared camera viewport mapping");
  sharedCamera->viewportMapping = SoCamera::CROP_VIEWPORT_NO_FRAME;
  ok &= check(capture.gpu(setup.scene.root, restored),
              "known shared camera mapping recovery") &&
        cropSame(first, restored, "mapping-domain recovery is exact");
  // Normalized coordinates are floats, so this is a publicly authorable
  // origin, not a fabricated overflowed short. 32768 is the first positive
  // pixel origin outside the public region's signed-short representation.
  // The separate legal32760 control remains admitted as an empty scissor.
  viewports[0].left = 32768.0 / n;
  ok &= cropRefuse(capture, setup.scene.root, restored,
                   "normalized viewport origin exceeds signed-short domain");
  viewports[0].left = .2;
  ok &= check(capture.gpu(setup.scene.root, restored),
              "representable normalized viewport origin recovery") &&
        cropSame(first, restored, "origin-domain recovery is exact");
  viewports[0].left = std::numeric_limits<double>::quiet_NaN();
  ok &= cropRefuse(capture, setup.scene.root, restored,
                   "nonfinite normalized viewport");
  viewports[0].left = .2;
  ok &= check(capture.gpu(setup.scene.root, restored),
              "viewport finite recovery") &&
        cropSame(first, restored, "invalid viewport/recovery A/B/A exact");
  for (const auto &viewport : viewports)
    ok &= check(viewport.visits && viewport.dimensionsCorrect,
                "both shared-camera occurrences use the actual map size");
  setup.caster->unref();
  return ok;
}
bool cropTwoMaps(Capture &capture, int executor, int n) {
  Scene scene;
  scene.group->precision = float(n) / 2048;
  scene.spot->intensity = .25f;
  scene.directional->intensity = .25f;
  cropReceiver(scene, executor);
  CropViewport viewports[2];
  for (int light = 0; light < 2; ++light) {
    scene.configure(light, 2, 0, light ? -.2f : .2f, 0);
    auto *camera = scene.orthographic[light];
    camera->aspectRatio = light ? .51f : 2.03f;
    camera->viewportMapping = light ? SoCamera::CROP_VIEWPORT_LINE_FRAME
                                    : SoCamera::CROP_VIEWPORT_FILL_FRAME;
    viewports[light].kind = 1;
    viewports[light].mapSide = n;
    viewports[light].left = light ? .3 : .2;
    scene.maps[light]->insertChild(viewports[light].node(), 0);
  }
  Frame first, single[2], both, restored;
  bool ok = true;
  for (int light = 0; light < 2; ++light) {
    scene.spot->on = light == 0;
    scene.directional->on = light == 1;
    ok &= check(capture.gpu(scene.root, single[light]),
                "separate cropped light/map identity");
  }
  scene.spot->on = TRUE;
  scene.directional->on = TRUE;
  ok &= check(capture.gpu(scene.root, both), "both cropped maps contribute");
  unsigned bad = 0, different = 0;
  if (single[0].rgba.size() == size_t(side * side * 4) &&
      single[1].rgba.size() == single[0].rgba.size() &&
      both.rgba.size() == single[0].rgba.size()) {
    for (int y = 16; y < side - 16; ++y)
      for (int x = 16; x < side - 16; ++x) {
        const size_t p = size_t(y * side + x);
        for (int c = 0; c < 3; ++c) {
          // Each single-light pass includes the same global ambient once.
          // Both-light intensity .25 leaves ample room below saturation.
          const double expected = single[0].rgba[4 * p + c] +
                                  single[1].rgba[4 * p + c] - .036 * 255;
          bad += std::abs(int(both.rgba[4 * p + c]) -
                          int(std::lround(expected))) > 3;
          different += single[0].rgba[4 * p + c] != single[1].rgba[4 * p + c];
        }
        bad += both.rgba[4 * p + 3] != 255;
        bad += std::abs(both.depth[p] - float(6. / 19)) > 1e-5;
      }
    ok &= check(!bad && different > 100,
                "independent light superposition preserves both cropped maps");
  } else {
    ok = false;
  }
  first = both;
  const auto position = scene.orthographic[0]->position.getValue();
  scene.orthographic[0]->position.setValue(.5f, 0, 5);
  ok &= check(capture.gpu(scene.root, both),
              "first map camera mutation with second map live") &&
        check(first.rgba != both.rgba, "first map camera remains observable");
  scene.orthographic[0]->position = position;
  ok &= check(capture.gpu(scene.root, restored), "two-map camera A/B/A") &&
        cropSame(first, restored, "both map identities recover exactly");
  std::cout << "crop_two_maps bad=" << bad << " distinct_channels=" << different
            << '\n';
  return ok;
}
bool cropState(Capture &capture, int executor, int extent) {
  const int n = extent ? extent : 256;
  bool ok = cropSharedState(capture, executor, n);
  if (executor != 2)
    ok &= cropTwoMaps(capture, executor, n);
  for (int light = executor == 2 ? 1 : 0; light < 2; ++light)
    for (int placement : {0, 1, 2})
      for (int empty : {3, 4, 7}) {
        CropSetup setup(light, placement == 2 || empty == 7 ? 0 : 2, n, .51,
                        empty, placement == 1 ? 1 : 0, 0, executor);
        ok &= cropCase(capture, setup, false);
      }
  // SbViewportRegion DEBUG builds clamp a negative authored extent to zero;
  // test only what the actual public API can represent in this build.
  CropSetup negative(1, 2, n, 1, 1, 1, 0, executor);
  auto *map = negative.scene.maps[1];
  negative.caster->ref();
  map->removeChild(negative.caster);
  Frame emptyControl, stable, failed, recovered;
  ok &= check(capture.gpu(negative.scene.root, emptyControl),
              "negative viewport neutral control");
  map->addChild(negative.caster);
  negative.caster->unref();
  ok &= check(capture.gpu(negative.scene.root, stable),
              "negative viewport prior valid publication");
  negative.viewport.kind = 6;
  capture.action.apply(negative.scene.root);
  capture.target->readbackRGBA(failed.rgba);
  capture.target->readbackDepth(failed.depth);
  failed.serial = capture.target->getLastSubmissionSerial();
  if (negative.viewport.negativeExtentRepresentable)
    ok &= check(capture.action.getLastStatus() != CoinRenderAction::SUCCESS &&
                    capture.action.getLastError().getLength() &&
                    failed.serial == stable.serial &&
                    failed.rgba == stable.rgba && failed.depth == stable.depth,
                "representable negative viewport refuses atomically");
  else
    ok &=
        check(capture.action.getLastStatus() == CoinRenderAction::SUCCESS,
              "public API clamped negative extent is a legal empty viewport") &&
        cropSame(emptyControl, failed, "clamped zero extent stays neutral");
  negative.viewport.kind = 1;
  ok &= check(capture.gpu(negative.scene.root, recovered),
              "negative extent recovery") &&
        cropSame(stable, recovered, "viewport extent A/B/A recovery exact");
  return ok;
}
