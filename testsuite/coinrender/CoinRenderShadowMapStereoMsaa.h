// Included after the independent stereo projection/image/delivery oracles.
// No runtime preparation or GPU sampling routine is used as a numeric oracle.
std::vector<StereoShape> stereoSourceShapes(const StereoScene &scene,
                                            size_t slot) {
  std::vector<StereoShape> result;
  for (size_t i = 0; i < scene.occurrences[slot].size(); ++i)
    result.push_back(stereoShape(scene, slot, i));
  return result;
}
unsigned stereoSourceChanges(const StereoScene &scene,
                             const std::vector<StereoShape> &a,
                             const std::vector<StereoShape> &b, int extent) {
  unsigned changed = 0;
  for (int y = extent * 12 / side; y < extent - extent * 12 / side; ++y)
    for (int x = extent * 12 / side; x < extent - extent * 12 / side; ++x) {
      const double wx = (x + .5 - extent / 2.) * 4 / extent,
                   wy = (extent / 2. - y - .5) * 4 / extent;
      if (!scene.receiverInterior(wx, wy, 8. / extent))
        continue;
      const double mx = (.5 + wx / scene.lookupWidth()) * 256,
                   my = (.5 + wy / scene.lookupWidth()) * 256;
      bool insideA = false, clearA = true, insideB = false, clearB = true;
      for (const auto &shape : a) {
        insideA |= stereoContains(shape.inner, mx, my, 2.1);
        clearA &= !stereoContains(shape.outer, mx, my, -2.1);
      }
      for (const auto &shape : b) {
        insideB |= stereoContains(shape.inner, mx, my, 2.1);
        clearB &= !stereoContains(shape.outer, mx, my, -2.1);
      }
      changed += (insideA && clearB) || (insideB && clearA);
    }
  return changed;
}
void stereoPrepareMsaaScene(StereoScene &scene, unsigned samples) {
  if (samples <= 1)
    return;
  scene.coverage = true;
  auto *coordinates = static_cast<SoCoordinate3 *>(
      scene.groupReceiver->getChild(scene.groupReceiver->getNumChildren() - 2));
  const SbVec3f points[] = {
      SbVec3f(-1.83f, -1.79f, -1), SbVec3f(1.73f, -1.69f, -1),
      SbVec3f(1.61f, 1.81f, -1), SbVec3f(-1.71f, 1.57f, -1)};
  coordinates->point.setValues(0, 4, points);
  for (size_t slot = 0; slot < scene.cameras.size(); ++slot) {
    auto *camera = scene.cameras[slot];
    const double nearValue = camera->nearDistance.getValue();
    camera->focalDistance = float(nearValue);
    camera->setBalanceAdjustment(1);
    double least = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < scene.occurrences[slot].size(); ++i) {
      if (scene.occurrences[slot][i].external)
        continue;
      double width, height;
      scene.nearDimensions(slot, i, width, height);
      const auto vp = scene.viewport(slot, i);
      least = std::min(least,
                       vp.w * nearValue / width * (1 / nearValue - 1 / 4.55));
    }
    // LEFT->RIGHT changes eye by the complete separation. A 6.2 map-pixel
    // displacement leaves two 2.1 margins and 2 pixels of safe contrasting
    // area.
    camera->setStereoAdjustment(float((2 * 2.1 + 2) / least));
  }
}
std::vector<uint64_t> stereoMsaaInputs(const StereoScene &scene, int extent,
                                       unsigned samples,
                                       CoinRenderTextureSamplingPolicy policy) {
  auto result = stereoAuthorState(scene, false);
  result.insert(result.end(),
                {uint64_t(extent), samples, uint64_t(policy),
                 uint64_t(scene.raster), uint64_t(scene.shared),
                 uint64_t(scene.coverage), uint64_t(scene.scissorCrossing)});
  const auto add = [&](float value) {
    uint32_t word;
    std::memcpy(&word, &value, 4);
    result.push_back(word);
  };
  const auto addDouble = [&](double value) {
    uint64_t word;
    std::memcpy(&word, &value, 8);
    result.push_back(word);
  };
  add(scene.group->quality.getValue());
  add(scene.group->precision.getValue());
  for (size_t slot = 0; slot < scene.lights.size(); ++slot) {
    auto *light = scene.lights[slot];
    result.push_back(light->on.getValue());
    add(light->intensity.getValue());
    for (int c = 0; c < 3; ++c)
      add(light->color.getValue()[c]);
    for (const auto &occ : scene.occurrences[slot]) {
      result.insert(result.end(),
                    {uint64_t(occ.placement), uint64_t(occ.external),
                     uint64_t(occ.viewport.kind),
                     uint64_t(occ.viewport.mapSide)});
      addDouble(occ.viewport.left);
      addDouble(occ.viewport.bottom);
      addDouble(occ.viewport.width);
      addDouble(occ.viewport.height);
      for (int c = 0; c < 3; ++c)
        add(occ.move->translation.getValue()[c]);
      for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) {
          add(occ.before[row][col]);
          add(occ.after[row][col]);
        }
    }
  }
  for (int child = 0; child < scene.group->getNumChildren(); ++child) {
    int slot = -1;
    for (size_t i = 0; i < scene.lights.size(); ++i)
      if (scene.group->getChild(child) == scene.lights[i])
        slot = int(i);
    result.push_back(uint64_t(slot + 1));
  }
  for (auto *texture : scene.textures) {
    SbVec2s size;
    int components = 0;
    const auto *pixels = texture->image.getValue(size, components);
    result.insert(result.end(),
                  {uint64_t(size[0]), uint64_t(size[1]), uint64_t(components),
                   uint64_t(texture->model.getValue()),
                   uint64_t(texture->wrapS.getValue()),
                   uint64_t(texture->wrapT.getValue())});
    for (size_t i = 0; i < size_t(size[0]) * size[1] * components; ++i)
      result.push_back(pixels[i]);
  }
  const auto *coordinates = static_cast<SoCoordinate3 *>(
      scene.groupReceiver->getChild(scene.groupReceiver->getNumChildren() - 2));
  for (int i = 0; i < coordinates->point.getNum(); ++i)
    for (int c = 0; c < 3; ++c)
      add(coordinates->point[i][c]);
  return result;
}
bool stereoCoverage(const StereoScene &scene, const Frame &frame,
                    unsigned samples, int extent, bool opaqueWindow,
                    bool report = true) {
  if (frame.rgba.size() != size_t(extent * extent * 4))
    return false;
  std::vector<StereoShape> footprints;
  if (opaqueWindow)
    for (size_t slot = 0; slot < scene.lights.size(); ++slot)
      if (scene.lights[slot]->intensity.getValue() > 0)
        for (const auto &shape : stereoSourceShapes(scene, slot))
          footprints.push_back(shape);
  unsigned bad = 0, partial = 0, odd = 0, full = 0, empty = 0, tested = 0;
  for (int y = 0; y < extent; ++y)
    for (int x = 0; x < extent; ++x) {
      const double wx = (x + .5 - extent / 2.) * 4 / extent,
                   wy = (extent / 2. - y - .5) * 4 / extent;
      const bool definitelyInside = scene.receiverInterior(wx, wy, 8. / extent);
      const bool definitelyOutside =
          !scene.receiverInterior(wx, wy, -8. / extent);
      const size_t pixel = size_t(y * extent + x) * 4;
      int selected = -1;
      if (!opaqueWindow) {
        int closest = 1000;
        for (unsigned k = 0; k <= samples; ++k) {
          const int delta = std::abs(int(frame.rgba[pixel + 3]) -
                                     int(std::lround(255. * k / samples)));
          if (delta <= 1 && delta < closest) {
            closest = delta;
            selected = int(k);
          }
        }
      } else {
        const double mx = (.5 + wx / scene.lookupWidth()) * 256,
                     my = (.5 + wy / scene.lookupWidth()) * 256;
        bool clear = true;
        for (const auto &shape : footprints)
          clear &= !stereoContains(shape.outer, mx, my, -3.1);
        if (!clear)
          continue;
        double intensity = 0;
        for (auto *light : scene.lights)
          intensity += light->intensity.getValue();
        const double lambert =
            scene.directional ? 1 : 6 / std::sqrt(36 + wx * wx + wy * wy);
        const double diffuse[] = {.72, .75, .78};
        double closest = std::numeric_limits<double>::infinity();
        for (unsigned k = 0; k <= samples; ++k) {
          bool valid = true;
          double error = 0;
          for (int c = 0; c < 3; ++c) {
            const double expected = 255 *
                                    (.036 + diffuse[c] * lambert * intensity) *
                                    scene.textureProduct[c] * k / samples;
            const double difference =
                std::abs(frame.rgba[pixel + c] - expected);
            valid &= difference <= 3;
            error += difference * difference;
          }
          if (valid && error < closest) {
            closest = error;
            selected = int(k);
          }
        }
      }
      ++tested;
      bad += selected < 0 || (definitelyInside && selected != int(samples)) ||
             (definitelyOutside && selected != 0);
      if (selected < 0)
        continue;
      full += selected == int(samples);
      empty += selected == 0;
      partial += selected > 0 && selected < int(samples);
      odd += selected > 0 && selected < int(samples) && (selected & 1);
    }
  const bool valid = bad == 0 && partial > 8 && full > 100 && empty > 100 &&
                     (samples != 4 || odd > 0);
  if (report)
    std::cout << "stereo_msaa_coverage samples=" << samples
              << " extent=" << extent << " rgb_window=" << opaqueWindow
              << " tested=" << tested << " bad=" << bad << " full=" << full
              << " empty=" << empty << " partial=" << partial << " odd=" << odd
              << '\n';
  return report
             ? check(valid, "real stereo MSAA coverage at requested k/N levels")
             : valid;
}
bool stereoRestore(Capture &capture, NearAudit &audit, StereoScene &scene,
                   const Frame &before, const Frame &current,
                   const std::string &label, bool sameInputs = true) {
  if (!nearNativeMsaa(*capture.target))
    return cropSame(before, current, label);
  const auto &options = capture.target->getOptions();
  return check(
             current.serial > current.stereoBeforeSerial &&
                 current.serial == capture.target->getLastSubmissionSerial() &&
                 !current.stereoInputs.empty() &&
                 (!sameInputs || before.stereoInputs == current.stereoInputs) &&
                 current.stereoInputs ==
                     stereoMsaaInputs(scene, side, options.sampleCount,
                                      options.textureSamplingPolicy),
             label + " expected authorial parameters and new current "
                     "publication") &&
         stereoDelivery(scene, audit) && stereoImage(scene, current, label) &&
         (!scene.coverage ||
          stereoCoverage(scene, current, options.sampleCount, side, false));
}
void stereoScissorScene(StereoScene &scene, unsigned samples) {
  scene.scissorCrossing = true;
  for (size_t slot = 0; slot < scene.cameras.size(); ++slot) {
    auto *camera = scene.cameras[slot];
    camera->viewportMapping = SoCamera::CROP_VIEWPORT_NO_FRAME;
    camera->aspectRatio = 1;
    camera->setStereoMode(SoCamera::LEFT_VIEW);
    auto &vp = scene.occurrences[slot][0].viewport;
    vp.kind = 8;
    vp.left = 96;
    vp.bottom = 48;
    vp.width = 160;
    vp.height = 160;
  }
  stereoPrepareMsaaScene(scene, samples);
  for (size_t slot = 0; slot < scene.cameras.size(); ++slot) {
    auto *camera = scene.cameras[slot];
    double width, height;
    scene.nearDimensions(slot, 0, width, height);
    const auto p = camera->position.getValue();
    const double distance = p[2] - .35, n = camera->nearDistance.getValue();
    const double eye = -camera->getStereoAdjustment() / 2.;
    const double focal = std::max(n, double(camera->focalDistance.getValue()) *
                                         camera->getBalanceAdjustment());
    scene.occurrences[slot][0].move->translation.setValue(
        float(p[0] + eye + distance * (-1. * width / (2 * n) - eye / focal)),
        p[1], .35f);
  }
}
bool stereoRender(Capture &, NearAudit &, StereoScene &, Frame &,
                  const std::string &);
bool stereoRefuse(Capture &, NearAudit &, StereoScene &, const Frame &,
                  const std::string &);
bool stereoMsaaScissor(Capture &capture, NearAudit &audit, int count,
                       bool directional, bool orthographic, bool general,
                       int selectedSlot) {
  bool ok = true;
  // Marker9 cannot guarantee safe strips on both sides of two 2.1 margins.
  // Its viewport/near paths remain in the separate raster focal.
  for (int raster : {0, 1}) {
    StereoScene scene(count, directional, orthographic,
                      SoCamera::CROP_VIEWPORT_NO_FRAME, raster, general);
    stereoScissorScene(scene, capture.target->getOptions().sampleCount);
    for (int slot = 0; slot < count; ++slot) {
      if (selectedSlot >= 0 && selectedSlot != slot)
        continue;
      scene.isolate(slot);
      Frame a, back;
      ok &= stereoRender(capture, audit, scene, a,
                         "stereo caster crosses original viewport/frustum");
      const int kind = scene.occurrences[slot][0].viewport.kind;
      scene.occurrences[slot][0].viewport.kind = 5;
      ok &= stereoRefuse(capture, audit, scene, a,
                         "nonfinite stereo scissor before submission");
      scene.occurrences[slot][0].viewport.kind = kind;
      ok &= stereoRender(capture, audit, scene, back,
                         "valid stereo scissor recovered") &&
            stereoRestore(capture, audit, scene, a, back,
                          "stereo crossing scissor recovery");
    }
  }
  return ok;
}

// Solve every non-external occurrence in its post-camera model coordinates.
// The before model cancels through the native viewing reset; after remains.
void stereoAlignMsaaSharedMarkers(StereoScene &scene) {
  for (size_t slot = 0; slot < scene.cameras.size(); ++slot) {
    auto *camera = scene.cameras[slot];
    const auto position = camera->position.getValue();
    const double n = camera->nearDistance.getValue();
    const double focal = std::max(n, double(camera->focalDistance.getValue()) *
                                         camera->getBalanceAdjustment());
    const double eye =
        (camera->getStereoMode() == SoCamera::LEFT_VIEW ? -1 : 1) *
        camera->getStereoAdjustment() / 2.;
    for (size_t index = 0; index < scene.occurrences[slot].size(); ++index) {
      auto &occ = scene.occurrences[slot][index];
      if (occ.external)
        continue;
      double width, height;
      scene.nearDimensions(slot, index, width, height);
      const auto vp = scene.viewport(slot, index);
      const double distance = position[2] - .35;
      const double x =
          position[0] + eye +
          distance *
              ((2 * (128 - vp.x) / vp.w - 1) * width / (2 * n) - eye / focal);
      const double y = position[1] + distance * (2 * (128 - vp.y) / vp.h - 1) *
                                         height / (2 * n);
      SbVec3f local;
      occ.after.inverse().multVecMatrix(SbVec3f(float(x), float(y), .35f),
                                        local);
      occ.move->translation = local;
    }
  }
}
