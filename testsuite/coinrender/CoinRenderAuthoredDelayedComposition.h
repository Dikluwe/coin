// Included in authored_composition. Per-face colors make triangle sorting
// observable within one object; object centers independently determine order.
struct DelayedScene : StereoScene {
  SoSeparator *objects[2]{};
  SoFaceSet *faces[2]{};
  bool reversed = false;
  int mode = SoGLRenderAction::DELAYED_BLEND;
  const double rgb[4][3] = {{.875, .125, .125}, {.125, .125, .875},
                            {.125, .875, .125}, {.875, .125, .875}};
  const double alpha[4] = {.5, .25, .25, .5};
  const float z[4] = {-.5f, -1.5f, -.25f, -1.25f};
  DelayedScene() : StereoScene(4, true, true, SoCamera::CROP_VIEWPORT_NO_FRAME, 0, false) {
    for (auto *camera : cameras) camera->setStereoMode(SoCamera::MONOSCOPIC);
    group->epsilon = 0; group->threshold = 0;
    groupReceiver->removeAllChildren();
    auto *style = new SoShadowStyle; style->style = SoShadowStyle::SHADOWED;
    groupReceiver->addChild(style);
    auto *environment = new SoEnvironment; environment->ambientIntensity = 0;
    groupReceiver->addChild(environment);
    for (unsigned object = 0; object < 2; ++object) {
      objects[object] = new SoSeparator; objects[object]->ref();
      auto *material = new SoMaterial;
      for (unsigned face = 0; face < 2; ++face) {
        const unsigned layer = object * 2 + face;
        material->diffuseColor.set1Value(face, SbColor(float(rgb[layer][0]), float(rgb[layer][1]), float(rgb[layer][2])));
        material->transparency.set1Value(face, float(1 - alpha[layer]));
      }
      material->ambientColor.setValue(0, 0, 0); material->specularColor.setValue(0, 0, 0);
      material->emissiveColor.setValue(0, 0, 0); objects[object]->addChild(material);
      auto *binding = new SoMaterialBinding; binding->value = SoMaterialBinding::PER_FACE;
      objects[object]->addChild(binding);
      auto *depth = new SoDepthBuffer; depth->write = FALSE; objects[object]->addChild(depth);
      auto *positions = new SoCoordinate3;
      for (unsigned face = 0; face < 2; ++face) {
        const float depth = z[2 * object + face];
        const SbVec3f points[] = {SbVec3f(-1.8f, -1.8f, depth), SbVec3f(1.8f, -1.8f, depth),
                                 SbVec3f(1.8f, 1.8f, depth), SbVec3f(-1.8f, 1.8f, depth)};
        positions->point.setValues(4 * face, 4, points);
      }
      objects[object]->addChild(positions);
      faces[object] = new SoFaceSet; const int counts[] = {4, 4};
      faces[object]->numVertices.setValues(0, 2, counts); objects[object]->addChild(faces[object]);
      groupReceiver->addChild(objects[object]);
    }
    together();
  }
  ~DelayedScene() { for (auto *object : objects) object->unref(); }
  void order(bool value) {
    groupReceiver->removeChild(objects[0]); groupReceiver->removeChild(objects[1]);
    groupReceiver->addChild(objects[value ? 1 : 0]); groupReceiver->addChild(objects[value ? 0 : 1]);
    reversed = value;
  }
};
bool delayedOracle(const DelayedScene &scene, const Frame &frame) {
  std::vector<StereoShape> footprints;
  for (size_t slot = 0; slot < scene.lights.size(); ++slot) footprints.push_back(stereoShape(scene, slot, 0));
  unsigned lit = 0, shadowed = 0, bad = 0; double maximum = 0;
  for (int y = 12; y < side - 12; ++y) for (int x = 12; x < side - 12; ++x) {
    const double wx = (x + .5 - side / 2.) * 4 / side, wy = (side / 2. - y - .5) * 4 / side;
    const double mx = (.5 + wx / scene.lookupWidth()) * 256, my = (.5 + wy / scene.lookupWidth()) * 256;
    double strength = 0; bool measurable = true, shadow = false;
    for (size_t slot = 0; slot < footprints.size(); ++slot) {
      const double intensity = scene.lights[slot]->intensity.getValue(); if (!intensity) continue;
      const bool hit = stereoContains(footprints[slot].inner, mx, my, 2.1);
      const bool clear = !stereoContains(footprints[slot].outer, mx, my, -2.1);
      if (!hit && !clear) { measurable = false; break; }
      shadow |= hit; if (!hit) strength += intensity;
    }
    if (!measurable) continue; shadowed += shadow; lit += !shadow;
    double rgba[4] = {0, 0, 0, 0};
    // DELAYED preserves object order; sorted objects are farther-center first
    // (object0 center=-1, object1 center=-.75). Triangle mode then sorts each
    // object's two faces far-to-near. The public mode is not global triangle sort.
    for (unsigned oi = 0; oi < 2; ++oi) {
      const unsigned object = scene.mode == SoGLRenderAction::DELAYED_BLEND && scene.reversed ? 1 - oi : oi;
      for (unsigned fi = 0; fi < 2; ++fi) {
        const unsigned face = scene.mode == SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND ? 1 - fi : fi;
        const unsigned layer = 2 * object + face; const double alpha = scene.alpha[layer];
        for (int c = 0; c < 3; ++c)
          rgba[c] = std::round(255 * scene.rgb[layer][c] * strength * alpha + rgba[c] * (1 - alpha));
        rgba[3] = std::round(255 * alpha + rgba[3] * (1 - alpha));
      }
    }
    const size_t pixel = size_t(y * side + x);
    for (int c = 0; c < 4; ++c) {
      const double delta = std::abs(double(frame.rgba[4 * pixel + c]) - rgba[c]);
      maximum = std::max(maximum, delta); bad += delta > 3;
    }
    bad += frame.depth[pixel] != 1.f;
  }
  std::cout << "delayed_composition mode=" << scene.mode << " reversed=" << scene.reversed
            << " lit=" << lit << " shadowed=" << shadowed << " bad=" << bad << " maximum=" << maximum << '\n';
  return check(lit > 500 && shadowed >= 4 && bad == 0,
               "independent per-face opacity/color and object/triangle order over authored CROP maps");
}
unsigned compositionDifference(const Frame &a, const Frame &b) {
  if (a.rgba.size() != b.rgba.size()) return 0;
  unsigned changed = 0;
  for (size_t p = 0; p < a.rgba.size(); p += 4) {
    bool differs = false;
    for (int c = 0; c < 3; ++c) differs |= std::abs(int(a.rgba[p + c]) - int(b.rgba[p + c])) > 3;
    changed += differs;
  }
  return changed;
}
bool t2(bool cpu) {
  CompositionHarness h(cpu); if (!h.target) return false;
  DelayedScene scene; bool ok = true;
  for (int isolated : {0, 1, 2, 3, -1}) {
    if (isolated < 0) scene.together(); else scene.isolate(size_t(isolated));
    Frame modes[3]; unsigned mi = 0;
    for (int mode : {SoGLRenderAction::DELAYED_BLEND, SoGLRenderAction::SORTED_OBJECT_BLEND,
                    SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND}) {
      scene.mode = mode; h.action.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(mode));
      auto draw = [&](Frame &frame, const char *label) {
        const auto authored = stereoAuthorState(scene);
        if (!h.render(scene.root, frame, label)) return false;
        bool pass = compositionDelivery(scene, *h.audit) &&
                    check(stereoAuthorState(scene) == authored, "transparent capture preserves authored map CROP cameras");
        unsigned receivers = 0;
        for (const auto &draw : h.audit->frame.draws) {
          if (draw.shadowLightSlot) continue;
          if (draw.sourceNodeId != scene.faces[0]->getNodeId() && draw.sourceNodeId != scene.faces[1]->getNodeId()) continue;
          ++receivers;
          const auto &state = h.audit->frame.renderStates[draw.renderStateSlot];
          pass &= check(!state.depthWrite && state.transparencyType == mode,
                        "deferred transparent draw retains its public ordering mode and depth mask");
        }
        return check(receivers >= 2, "both stacked-face objects captured") && pass && (cpu || delayedOracle(scene, frame));
      };
      scene.order(false); Frame a, back;
      if (!draw(a, "T2 authored near-face-first order")) { ok = false; continue; }
      scene.order(true);
      if (!draw(modes[mi], "T2 reversed objects")) { ok = false; continue; }
      if (!cpu) ok &= mode == SoGLRenderAction::DELAYED_BLEND
          ? check(compositionDifference(a, modes[mi]) > 500, "DELAYED remains order dependent")
          : exactComposition(a, modes[mi]);
      scene.order(false); ok &= draw(back, "T2 order restored") && exactComposition(a, back);
      ++mi;
    }
    if (!cpu && mi == 3) {
      ok &= check(compositionDifference(modes[0], modes[1]) > 500, "DELAYED versus object sorting is discriminated");
      ok &= check(compositionDifference(modes[1], modes[2]) > 500, "per-object triangle sorting changes the authored face-color order");
    }
  }
  return ok;
}
