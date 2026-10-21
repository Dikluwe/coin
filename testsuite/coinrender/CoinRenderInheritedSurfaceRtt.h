// Included within inherited_surfaces. The producer is the same authored
// bitmap scene; a separate full-screen quad sampled at texel centers consumes it 1:1.
struct SurfaceRttScene {
  SoSeparator *root = new SoSeparator;
  SoSceneTexture2 *texture = new SoSceneTexture2;
  SoFaceSet *quad = new SoFaceSet;
  SoTextureCoordinate2 *uv = new SoTextureCoordinate2;
  explicit SurfaceRttScene(RasterScene &producer) {
    root->ref();
    auto *camera = new SoOrthographicCamera;
    camera->position.setValue(0, 0, 3); camera->height = 2;
    camera->nearDistance = 1; camera->farDistance = 9; root->addChild(camera);
    auto *lighting = new SoLightModel; lighting->model = SoLightModel::BASE_COLOR;
    root->addChild(lighting);
    auto *material = new SoMaterial; material->diffuseColor.setValue(1, 1, 1);
    root->addChild(material);
    auto *quality = new SoComplexity; quality->textureQuality = .1f;
    root->addChild(quality);
    auto *unit = new SoTextureUnit; unit->unit = 0; root->addChild(unit);
    texture->scene = producer.root; texture->size.setValue(side, side);
    texture->backgroundColor.setValue(0, 0, 0, 1);
    texture->model = SoSceneTexture2::REPLACE;
    texture->wrapS = texture->wrapT = SoSceneTexture2::CLAMP;
    texture->transparencyFunction = SoSceneTexture2::NONE;
    auto *transparency = new SoTransparencyType;
    transparency->value = SoGLRenderAction::BLEND;
    texture->sceneTransparencyType = transparency;
    root->addChild(texture);
    const SbVec2f st[] = {SbVec2f(0, 0), SbVec2f(1, 0), SbVec2f(1, 1), SbVec2f(0, 1)};
    uv->point.setValues(0, 4, st); root->addChild(uv);
    auto *coordinates = new SoCoordinate3;
    const SbVec3f points[] = {SbVec3f(-1, -1, 0), SbVec3f(1, -1, 0),
                             SbVec3f(1, 1, 0), SbVec3f(-1, 1, 0)};
    coordinates->point.setValues(0, 4, points); root->addChild(coordinates);
    quad->numVertices = 4; root->addChild(quad);
  }
  ~SurfaceRttScene() { root->unref(); }
};
bool cameraAt(const CoinRenderFramePlan &plan, uint64_t node, float z, float farDistance) {
  unsigned seen = 0; bool ok = true;
  for (const auto &draw : plan.draws) {
    if (draw.shadowLightSlot || draw.sourceNodeId != node) continue;
    if (!check(draw.renderStateSlot < plan.renderStates.size(), "RTT occurrence render state exists")) return false;
    const auto &state = plan.renderStates[draw.renderStateSlot];
    if (!check(state.cameraSlot < plan.cameras.size(), "RTT authored camera exists")) return false;
    const auto &camera = plan.cameras[state.cameraSlot];
    ++seen;
    ok &= check(!camera.isPerspective && camera.nearDistance == 1 &&
                camera.farDistance == farDistance && camera.viewMatrix[3][2] == -z,
                "producer/root keep distinct authored orthographic cameras");
  }
  return check(seen > 0, "camera belongs to the actual authored occurrence") && ok;
}
bool rttDelivery(const RasterScene &scene, const SurfaceRttScene &consumer,
                 const SurfaceAudit &audit, CoinRenderSceneTextureMode route) {
  if (!check(audit.graph.mode == route && audit.graph.producers.size() == 1,
             "exactly one producer in the selected RTT domain")) return false;
  const auto &producer = audit.graph.producers[0];
  bool ok = check(!producer.cube && producer.sourceRevision == consumer.texture->getNodeId() &&
                  producer.size == SbVec2i32(side, side) &&
                  producer.plan.textureSamplingPolicy == samplingTestPolicy &&
                  audit.capturedRoot.textureSamplingPolicy == samplingTestPolicy,
                  "RTT producer identity, dimensions and independent policy");
  ok &= surfaceDelivery(scene, producer.plan) && associations(scene, producer.plan);
  ok &= cameraAt(producer.plan, scene.image->getNodeId(), 6, 20);
  ok &= cameraAt(audit.capturedRoot, consumer.quad->getNodeId(), 3, 9);
  ok &= check(audit.capturedRoot.shadowLights.empty(), "producer maps do not leak into consumer");
  unsigned references = 0;
  for (const auto &draw : audit.capturedRoot.draws) {
    if (draw.sourceNodeId != consumer.quad->getNodeId()) continue;
    const auto &state = audit.capturedRoot.renderStates[draw.renderStateSlot];
    const auto layer = coin_render_texture_unit(state, 0);
    if (!check(layer.enabled && layer.imageSlot < audit.capturedRoot.textures.size() &&
               layer.samplerSlot < audit.capturedRoot.samplers.size(), "consumer owns produced texture")) return false;
    const auto &texture = audit.capturedRoot.textures[layer.imageSlot];
    const auto &sampler = audit.capturedRoot.samplers[layer.samplerSlot];
    ok &= check(texture.producerId == 1 && texture.width == side && texture.height == side &&
                !texture.mipmapped && sampler.filter == CoinRenderTextureFilter::LINEAR,
                "consumer linear base-level sampling at texel centers refers to actual producer one");
    ++references;
  }
  return check(references > 0, "producer/consumer edge exercised") && ok;
}
bool rttImageOracle(const RasterScene &s, const Frame &f, const Frame *white,
                 bool blocked) {
  if (f.rgba.size() != size_t(side * side * 4) ||
      f.depth.size() != size_t(side * side))
    return false;
  unsigned checked = 0, holes = 0, bad = 0;
  double maximum = 0;
  const auto product = s.product();
  // Avoid the one-pixel alignment border; these are authored bitmap interiors.
  for (int y = 58; y <= 69; ++y)
    for (int x = 58; x <= 69; ++x) {
      const int ix = x - 56, iy = 71 - y;
      const size_t source = (iy * 17 + ix) * 4, p = size_t(y * side + x);
      const bool hole = s.imageBytes[source + 3] == 0;
      holes += hole;
      for (int c = 0; c < 3; ++c) {
        // Ordinary DrawPixels uses image PRIMARY. Only the shadow receiver
        // has the authored diffuse illumination factor .6.
        const double primaryFactor =
            s.rasterStyle->style.getValue() == SoShadowStyle::SHADOWED ? .6
                                                                       : 1.;
        double expected =
            blocked || hole ? 0 : s.imageBytes[source + c] * primaryFactor;
        if (white)
          expected = white->rgba[p * 4 + c];
        expected *= product[c];
        const double delta = std::abs(double(f.rgba[p * 4 + c]) - expected);
        maximum = std::max(maximum, delta);
        bad += delta > 3;
      }
      bad += f.rgba[p * 4 + 3] != 255;
      bad += std::abs(f.depth[p] - float(2. / 8.)) > 2e-5f;
      ++checked;
    }
  std::cout << "inherited_image checked=" << checked << " holes=" << holes
            << " bad=" << bad << " max=" << maximum << " blocked=" << blocked
            << '\n';
  return check(
      checked >= 100 && (!s.setting.holes || holes > 20) && bad == 0,
      "independent raster texel product, alpha holes and geometric depth");
}
bool runRtt(const Settings &s, const CoinRenderCapabilities &caps, CoinRenderSceneTextureMode route) {
  SurfaceHarness h(caps, 1, route);
  if (!h.target)
    return false;
  auto *audit = h.audit;
  h.action.setTransparencyType(CoinRenderAction::BLEND);
  h.action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
  RasterScene scene(s);
  SurfaceRttScene graph(scene);
  bool ok = true;
  Frame clear, shadow, colored, back;
  auto draw = [&](Frame &f, const char *label) {
    auto count = audit->submits;
    const auto produced = audit->producers;
    const auto inspected = audit->preflight;
    const auto sourceId = scene.root->getNodeId(),
               imageId = scene.image->getNodeId();
    bool pass = h.render(graph.root, f, label);
    pass &= check(sourceId == scene.root->getNodeId() &&
                      imageId == scene.image->getNodeId(),
                  "apply does not mutate authored source nodes");
    return check(audit->submits == count + 1, "one logical GPU submission") &&
           pass && check(audit->preflight == inspected + 2 &&
               audit->producers == produced + (route == COIN_RENDER_SCENE_TEXTURE_DIRECT ? 1u : 0u),
               "two preflights, one root and exactly one DIRECT producer") &&
           rttDelivery(scene, graph, *audit, route);
  };
  for (int slot = 0; slot < s.maps; ++slot) {
    scene.isolate(slot);
    scene.paint(true);
    scene.shadow(slot, false);
    if (!draw(clear, "white lit slot")) {
      ok = false;
      continue;
    }
    ok &= rttImageOracle(scene, clear, nullptr, false);
    scene.shadow(slot, true);
    if (draw(shadow, "white shadow slot")) {
      ok &= rttImageOracle(scene, shadow, nullptr, true);
      unsigned changed = 0;
      for (int y = 58; y <= 69; ++y)
        for (int x = 58; x <= 69; ++x) {
          size_t p = size_t(y * side + x) * 4;
          changed += std::abs(int(clear.rgba[p]) - int(shadow.rgba[p])) > 20;
        }
      ok &= check(changed > 50,
                  "each isolated real map observably shadows the image");
    } else
      ok = false;
    scene.paint(false);
    if (draw(colored, "colored shadow slot"))
      ok &= rttImageOracle(scene, colored, &shadow, true);
    else
      ok = false;
    scene.shadow(slot, false);
    if (draw(colored, "colored lit slot"))
      ok &= rttImageOracle(scene, colored, &clear, false);
    else
      ok = false;
    scene.paint(true);
    if (draw(back, "white restored slot"))
      ok &= rttImageOracle(scene, back, nullptr, false) && exact(clear, back);
    else
      ok = false;
  }
  scene.isolate(-1);
  scene.paint(true);
  if (!draw(clear, "joint white"))
    return false;
  scene.paint(false);
  if (!draw(colored, "joint patterned"))
    return false;
  ok &= rttImageOracle(scene, colored, &clear, false);
  for (int unit : scene.active) {
    scene.paint(false, unit);
    if (draw(back, "unit bytes changed")) {
      ok &= rttImageOracle(scene, back, &clear, false);
      unsigned changed = 0;
      for (int y = 58; y <= 69; ++y)
        for (int x = 58; x <= 69; ++x) {
          size_t p = size_t(y * side + x) * 4;
          for (int c = 0; c < 3; ++c)
            changed +=
                std::abs(int(back.rgba[p + c]) - int(colored.rgba[p + c])) > 3;
        }
      ok &= check(changed > 16, "each inherited texture has a visible effect");
    } else
      ok = false;
    scene.paint(false);
    if (draw(back, "unit bytes restore"))
      ok &= rttImageOracle(scene, back, &clear, false) && exact(colored, back);
    else
      ok = false;
    const auto q = scene.coordinates[unit]->factorQ.getValue(),
               st = scene.coordinates[unit]->factorS.getValue();
    for (int invalid = 0; invalid < 2; ++invalid) {
      // Q divides only on PROJECTIVE. Keep all maps enabled, but author the
      // raster as UNSHADOWED for this control. NaN also covers DIRECT_ST.
      if (invalid == 0) {
        scene.rasterStyle->style = SoShadowStyle::NO_SHADOWING;
        if (!draw(back, "valid PROJECTIVE predecessor"))
          return false;
        ok &= rttImageOracle(scene, back, nullptr, false);
      }
      if (invalid == 0)
        scene.coordinates[unit]->factorQ.setValue(0, 0, 0, 0);
      else
        scene.coordinates[unit]->factorS.setValue(
            0, 0, 0, std::numeric_limits<float>::quiet_NaN());
      auto count = audit->submits, producers = audit->producers;
      const auto inspected = audit->preflight;
      const auto serial = h.target->getLastSubmissionSerial();
      h.action.apply(graph.root);
      Frame failed;
      h.target->readbackRGBA(failed.rgba);
      h.target->readbackDepth(failed.depth);
      ok &=
          check(h.action.getLastStatus() != CoinRenderAction::SUCCESS &&
                    audit->submits == count && audit->producers == producers &&
                    audit->preflight == inspected &&
                    h.target->getLastSubmissionSerial() == serial &&
                    failed.rgba == back.rgba && failed.depth == back.depth,
                "invalid per-unit Q/NaN refuses before commands and preserves "
                "exact publication");
      scene.coordinates[unit]->factorQ = q;
      scene.coordinates[unit]->factorS = st;
      scene.rasterStyle->style = SoShadowStyle::SHADOWED;
      if (draw(back, "invalid coordinate recovery"))
        ok &= rttImageOracle(scene, back, &clear, false) && exact(colored, back);
      else
        ok = false;
    }
  }
  const auto savedUv = graph.uv->point[0];
  graph.uv->point.set1Value(0, SbVec2f(std::numeric_limits<float>::quiet_NaN(), 0));
  const auto rootCount = audit->submits, producerCount = audit->producers, inspections = audit->preflight;
  const auto serial = h.target->getLastSubmissionSerial();
  h.action.apply(graph.root);
  Frame refused; h.target->readbackRGBA(refused.rgba); h.target->readbackDepth(refused.depth);
  ok &= check(h.action.getLastStatus() != CoinRenderAction::SUCCESS &&
              audit->submits == rootCount && audit->producers == producerCount &&
              audit->preflight == inspections && h.target->getLastSubmissionSerial() == serial &&
              refused.rgba == back.rgba && refused.depth == back.depth,
              "invalid consumer refuses before producer/root commands and preserves publication");
  graph.uv->point.set1Value(0, savedUv);
  ok &= draw(back, "consumer first valid recovery") && exact(colored, back);
  auto *unused = new SoTextureCoordinateObject;
  unused->factorS.setValue(0, 0, 0, std::numeric_limits<float>::quiet_NaN());
  scene.receiver->addChild(unused); // after the only image: no consumer
  if (draw(back, "unconsumed invalid coordinate state"))
    ok &= rttImageOracle(scene, back, &clear, false) && exact(colored, back);
  else ok = false;
  return ok;
}

