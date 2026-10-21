// Included within authored_composition. Consumer projection gives depth 2/8;
// producer projection gives 6/19. The full quad samples producer 1:1 at texel centers.
struct CompositionConsumer {
  SoSeparator *root = new SoSeparator;
  SoSceneTexture2 *texture = new SoSceneTexture2;
  SoFaceSet *quad = new SoFaceSet;
  SoTextureCoordinate2 *uv = new SoTextureCoordinate2;
  explicit CompositionConsumer(SoNode *producer) {
    root->ref();
    auto *camera = new SoOrthographicCamera;
    camera->position.setValue(0, 0, 3); camera->height = 2;
    camera->nearDistance = 1; camera->farDistance = 9; root->addChild(camera);
    auto *lighting = new SoLightModel; lighting->model = SoLightModel::BASE_COLOR;
    root->addChild(lighting);
    auto *material = new SoMaterial; material->diffuseColor.setValue(1, 1, 1);
    root->addChild(material);
    auto *quality = new SoComplexity; quality->textureQuality = .1f; root->addChild(quality);
    auto *unit = new SoTextureUnit; unit->unit = 0; root->addChild(unit);
    texture->scene = producer; texture->size.setValue(side, side);
    texture->backgroundColor.setValue(0, 0, 0, 1);
    texture->model = SoSceneTexture2::REPLACE;
    texture->wrapS = texture->wrapT = SoSceneTexture2::CLAMP;
    texture->transparencyFunction = SoSceneTexture2::NONE;
    auto *transparency = new SoTransparencyType; transparency->value = SoGLRenderAction::BLEND;
    texture->sceneTransparencyType = transparency; root->addChild(texture);
    const SbVec2f st[] = {SbVec2f(0, 0), SbVec2f(1, 0), SbVec2f(1, 1), SbVec2f(0, 1)};
    uv->point.setValues(0, 4, st); root->addChild(uv);
    auto *coordinates = new SoCoordinate3;
    const SbVec3f points[] = {SbVec3f(-1, -1, 0), SbVec3f(1, -1, 0),
                             SbVec3f(1, 1, 0), SbVec3f(-1, 1, 0)};
    coordinates->point.setValues(0, 4, points); root->addChild(coordinates);
    quad->numVertices = 4; root->addChild(quad);
  }
  ~CompositionConsumer() { root->unref(); }
};
class CompositionRttAudit : public NearAudit {
public:
  mutable CoinRenderRttPlan graph;
  mutable CoinRenderFramePlan root;
  mutable unsigned preflights = 0;
  explicit CompositionRttAudit(std::unique_ptr<CoinRenderBackend> real) : NearAudit(std::move(real)) {}
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan &g,
      const CoinRenderFramePlan &f, const SbVec2i32 &size) const override {
    ++preflights; graph = g; root = f;
    return NearAudit::preflightRtt(g, f, size);
  }
};
struct CompositionRttHarness : CompositionHarness {
  bool cpu;
  CoinRenderSceneTextureMode route;
  CompositionRttAudit *rtt = nullptr;
  CompositionRttHarness(bool captureOnly, CoinRenderSceneTextureMode mode)
      : CompositionHarness(captureOnly), cpu(captureOnly), route(mode) {
    if (!target) return;
    auto &state = target->getPimpl().get(); state.options.sceneTexture = mode;
    rtt = new CompositionRttAudit(std::move(state.backend)); state.backend.reset(rtt); audit = rtt;
    action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
    action.getPimpl()->planOnly = cpu;
  }
  void freshCaptureGraph() {
    // Retain the graph by explicitly lending it to planOnly. No backend factory
    // or execution is used for producers in this CPU-only capture witness.
    if (cpu) action.getPimpl()->sceneTexturePlan = std::make_shared<CoinRenderRttPlan>(route);
  }
  bool draw(CompositionConsumer &consumer, Frame &f, const char *label) {
    const auto rootCount = rtt->submissions, producerCount = rtt->producers, inspections = rtt->preflights;
    freshCaptureGraph();
    bool ok;
    if (cpu) {
      action.apply(consumer.root);
      ok = check(action.getLastStatus() == CoinRenderAction::SUCCESS,
                 std::string(label) + " CPU capture: " + action.getLastError().getString());
      if (!ok) return false;
      rtt->graph = *action.getPimpl()->sceneTexturePlan; rtt->root = action.getPimpl()->lastValidPlan;
      ok &= check(rtt->submissions == 0 && rtt->producers == 0 && rtt->preflights == 0 &&
                  target->getLastSubmissionSerial() == 0, "CPU graph capture executes neither producer nor root");
      target->readbackRGBA(f.rgba); target->readbackDepth(f.depth);
    } else {
      ok = render(consumer.root, f, label);
      ok &= check(rtt->submissions == rootCount + 1 && rtt->preflights == inspections + 2 &&
                  rtt->producers == producerCount + (route == COIN_RENDER_SCENE_TEXTURE_DIRECT ? 1u : 0u),
                  "first apply preflights and executes the selected RTT domain exactly once");
      unsigned bad = 0;
      for (float value : f.depth) bad += std::abs(value - .25f) > 2e-5f;
      ok &= check(f.depth.size() == size_t(side * side) && bad == 0,
                  "consumer geometric depth 2/8 is independent of producer depth 6/19");
    }
    if (!ok || !check(rtt->graph.mode == route && rtt->graph.producers.size() == 1,
                     "one real 2D producer with the selected route")) return false;
    const auto &producer = rtt->graph.producers[0];
    ok &= check(!producer.cube && producer.sourceRevision == consumer.texture->getNodeId() &&
                producer.size == SbVec2i32(side, side), "producer dimensions and current authored source identity");
    rtt->frame = producer.plan; // Existing independent map/FFI oracle receives producer, never root.
    ok &= check(rtt->root.shadowLights.empty(), "producer map lights do not leak into root");
    unsigned references = 0, producerReceivers = 0;
    for (const auto &draw : rtt->root.draws) {
      if (draw.sourceNodeId != consumer.quad->getNodeId()) continue;
      if (!check(draw.renderStateSlot < rtt->root.renderStates.size(), "root occurrence state")) return false;
      const auto &state = rtt->root.renderStates[draw.renderStateSlot];
      if (!check(state.cameraSlot < rtt->root.cameras.size(), "root occurrence camera")) return false;
      const auto &camera = rtt->root.cameras[state.cameraSlot];
      ok &= check(!camera.isPerspective && camera.nearDistance == 1 && camera.farDistance == 9 &&
                  camera.viewMatrix[3][2] == -3, "actual root quad keeps its authored camera");
      const auto layer = coin_render_texture_unit(state, 0);
      if (!check(layer.enabled && layer.imageSlot < rtt->root.textures.size() &&
                 layer.samplerSlot < rtt->root.samplers.size(), "root binds a produced texture")) return false;
      const auto &image = rtt->root.textures[layer.imageSlot];
      ok &= check(image.producerId == 1 && image.width == side && image.height == side &&
                  !image.mipmapped && rtt->root.samplers[layer.samplerSlot].filter == CoinRenderTextureFilter::LINEAR,
                  "real producer one uses native linear base-level sampling at texel centers");
      ++references;
    }
    for (const auto &draw : producer.plan.draws) {
      if (draw.shadowLightSlot) continue;
      if (!check(draw.renderStateSlot < producer.plan.renderStates.size(), "producer receiver state")) return false;
      const auto &state = producer.plan.renderStates[draw.renderStateSlot];
      if (!check(state.cameraSlot < producer.plan.cameras.size(), "producer receiver camera")) return false;
      const auto &camera = producer.plan.cameras[state.cameraSlot];
      ok &= check(!camera.isPerspective && camera.nearDistance == 1 && camera.farDistance == 20 &&
                  camera.viewMatrix[3][2] == -6, "producer receiver keeps its own mono camera");
      ++producerReceivers;
    }
    return check(references > 0 && producerReceivers > 0, "actual producer and root geometry exercised") && ok;
  }
  bool reject(CompositionConsumer &consumer, const Frame &previous) {
    const auto inspections = rtt->preflights;
    freshCaptureGraph();
    return refuse(consumer.root, previous) &&
           check(rtt->preflights == inspections, "invalid consumed input refuses before producer/root preflight");
  }
};
unsigned rttCompositionDifference(const Frame &a, const Frame &b) {
  unsigned changed = 0;
  for (size_t p = 0; p + 3 < a.rgba.size() && p + 3 < b.rgba.size(); p += 4)
    changed += std::abs(int(a.rgba[p]) - int(b.rgba[p])) > 3;
  return changed;
}
// Orthographic XY is unchanged, but map depth moments at the silhouette are
// different when the caster moves behind the eye. Compare the authored solid
// interior/exterior domain; full A/restored-A equality remains required below.
bool signedInteriorEqual(const NearSlotScene &scene, size_t slot,
                         const Frame &a, const Frame &b) {
  const auto footprint = nearFootprint(scene, slot);
  unsigned inside = 0, outside = 0, bad = 0;
  for (int y = 12; y < side - 12; ++y) for (int x = 12; x < side - 12; ++x) {
    const double wx = (x + .5 - side / 2.) * 4 / side;
    const double wy = (side / 2. - y - .5) * 4 / side;
    const double mx = (.5 + wx / std::sqrt(96.)) * 256;
    const double my = (.5 + wy / std::sqrt(96.)) * 256;
    const bool hit = footprint.inside(mx, my, 2.1);
    const bool clear = !footprint.inside(mx, my, -2.1);
    if (!hit && !clear) continue;
    inside += hit; outside += clear;
    const size_t pixel = size_t(y * side + x);
    for (int c = 0; c < 4; ++c) bad += a.rgba[4 * pixel + c] != b.rgba[4 * pixel + c];
  }
  return check(inside >= 4 && outside > 500 && bad == 0 && a.depth == b.depth,
               "negative near keeps exact authored solid shadow/light interiors and consumer depth");
}
bool r2(bool cpu, CoinRenderSceneTextureMode route) {
  bool ok = true;
  for (int kind : {0, 1}) { // geometric box and literal white Image caster
    CompositionRttHarness h(cpu, route); if (!h.target) return false;
    NearSlotScene scene(8, kind, false); scene.mixedNears();
    CompositionConsumer consumer(scene.root);
    auto draw = [&](Frame &f, const char *label) {
      const auto source = nearAuthorialInputs(scene);
      return h.draw(consumer, f, label) && signedCompositionDelivery(scene, *h.rtt) &&
             check(source == nearAuthorialInputs(scene), "RTT capture preserves signed camera/source inputs") &&
             (cpu || nearImageOracle(scene, f, label, false));
    };
    for (int slot = 0; slot <= 8; ++slot) {
      if (slot == 8) scene.allContribute(); else scene.isolate(slot);
      Frame a, b, back;
      if (!draw(a, "R2 caster before eye")) { ok = false; continue; }
      for (auto *move : scene.moves) move->translation.setValue(0, 0, 6);
      if (draw(b, "R2 caster behind eye")) {
        if (!cpu && slot < 8) {
          if (scene.cameras[slot]->nearDistance.getValue() >= 0)
            ok &= check(rttCompositionDifference(a, b) >= 4, "nonnegative near rejects a visibly shadowing behind-eye caster");
          else ok &= signedInteriorEqual(scene, size_t(slot), a, b);
        }
      } else ok = false;
      for (auto *move : scene.moves) move->translation.setValue(0, 0, .35f);
      ok &= draw(back, "R2 caster restored") && exactComposition(a, back);
      if (slot != 8) continue;
      for (auto *camera : scene.cameras) {
        const float saved = camera->nearDistance.getValue();
        camera->nearDistance = std::numeric_limits<float>::quiet_NaN();
        ok &= h.reject(consumer, back); camera->nearDistance = saved;
        ok &= draw(back, "R2 first valid camera recovery") && exactComposition(a, back);
      }
      const auto st = consumer.uv->point[0];
      consumer.uv->point.set1Value(0, SbVec2f(std::numeric_limits<float>::quiet_NaN(), 0));
      ok &= h.reject(consumer, back); consumer.uv->point.set1Value(0, st);
      ok &= draw(back, "R2 first valid root recovery") && exactComposition(a, back);
      auto *unused = new SoTextureCoordinate2;
      unused->point.setValue(std::numeric_limits<float>::quiet_NaN(), 0); scene.root->addChild(unused);
      ok &= draw(back, "R2 unconsumed invalid trailing state") && exactComposition(a, back);
    }
  }
  return ok;
}
bool r3(bool cpu, CoinRenderSceneTextureMode route) {
  bool ok = true;
  for (int family : {0, 1}) {
    const size_t count = family ? 8 : 4;
    CompositionRttHarness h(cpu, route); if (!h.target) return false;
    StereoScene scene(count, family != 0, family != 0, SoCamera::CROP_VIEWPORT_NO_FRAME,
                      family ? 1 : 0, false, true);
    CompositionConsumer consumer(scene.root);
    auto draw = [&](Frame &f, const char *label) {
      for (size_t i = 0; i < count; ++i) scene.probes[i].arm(scene.cameras[i]);
      const auto source = stereoAuthorState(scene);
      bool pass = h.draw(consumer, f, label);
      pass &= check(source == stereoAuthorState(scene), "RTT preserves shared camera mode, skew and node identity");
      for (const auto &probe : scene.probes)
        pass &= check(probe.same && probe.visits >= 2, "both shared occurrences observe original stereo source state");
      return pass && compositionDelivery(scene, *h.rtt) && (cpu || stereoImage(scene, f, label, side, false));
    };
    for (size_t slot = 0; slot <= count; ++slot) {
      if (slot == count) scene.together(); else scene.isolate(slot);
      Frame a, b, back;
      if (!draw(a, "R3 authored stereo occurrences")) { ok = false; continue; }
      const size_t changed = slot == count ? count - 1 : slot;
      auto *camera = scene.cameras[changed]; const auto eye = camera->getStereoMode();
      camera->setStereoMode(eye == SoCamera::LEFT_VIEW ? SoCamera::RIGHT_VIEW : SoCamera::LEFT_VIEW);
      if (draw(b, "R3 authored eye reversal")) {
        if (!cpu) ok &= check(rttCompositionDifference(a, b) >= 4, "each map's authored eye has a visible independent contribution");
      } else ok = false;
      camera->setStereoMode(eye);
      ok &= draw(back, "R3 eye restored") && exactComposition(a, back);
      if (slot != count) continue;
      for (size_t invalid = 0; invalid < count; ++invalid) {
        auto *c = scene.cameras[invalid]; const float farValue = c->farDistance.getValue();
        c->farDistance = c->nearDistance.getValue();
        for (size_t i = 0; i < count; ++i) scene.probes[i].arm(scene.cameras[i]);
        ok &= h.reject(consumer, back); c->farDistance = farValue;
        ok &= draw(back, "R3 first valid frustum recovery") && exactComposition(a, back);
      }
      auto &viewport = scene.occurrences.back()[1].viewport;
      const int kind = viewport.kind; viewport.kind = 5;
      for (size_t i = 0; i < count; ++i) scene.probes[i].arm(scene.cameras[i]);
      ok &= h.reject(consumer, back); viewport.kind = kind;
      ok &= draw(back, "R3 later shared viewport recovery") && exactComposition(a, back);
    }
  }
  return ok;
}
