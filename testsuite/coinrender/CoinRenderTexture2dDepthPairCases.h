// Included inside namespace startup after the actual GPU Target fixture.
bool pairedDepthImage(const Fixture &scene, const Frame &f,
                      const Config &config, unsigned extent) {
  if (f.rgba.size() != size_t(extent) * extent * 4 ||
      f.depth.size() != size_t(extent) * extent)
    return false;
  unsigned inside[2] = {0, 0}, outside = 0, bad = 0, depthBad = 0, partial = 0,
           odd = 0;
  const double margin = 2.1 * 2 / extent,
               planes[] = {2.9 / 9.9, .2 + .6 * 2.9 / 9.9};
  for (unsigned y = 0; y < extent; ++y)
    for (unsigned x = 0; x < extent; ++x) {
      const double wx = (x + .5) * 2 / extent - 1,
                   wy = 1 - (y + .5) * 2 / extent;
      const size_t pixel = size_t(y) * extent + x;
      bool exterior = true;
      for (unsigned lane = 0; lane < 2; ++lane) {
        const double lx = (wx - (lane ? .5 : -.5)) / .6, ly = wy / .6;
        const double a = (lx + .8) / 1.6, b = (ly + .7875) / 1.5875;
        const double diagonal =
            .6 * (a - b) / std::sqrt(1 / (1.6 * 1.6) + 1 / (1.5875 * 1.5875));
        const bool in = diagonal > margin && lx > -.8 + margin / .6 &&
                        lx < .8 - margin / .6 && ly > -.7875 + margin / .6 &&
                        ly < .8 - margin / .6;
        const bool out = diagonal < -margin || lx < -.8 - margin / .6 ||
                         lx > .8 + margin / .6 || ly < -.7875 - margin / .6 ||
                         ly > .8 + margin / .6;
        exterior &= out;
        if (in && std::abs(lx) > margin / .6 &&
            std::abs(ly - .00625) > margin / .6) {
          ++inside[lane];
          // B1 texel() already includes its fixed material primary[0].
          // Remove that factor before applying this occurrence's material.
          const auto sampled = texel(scene, lx, ly);
          const double primary[2][3] = {{.8, .3, .2}, {.2, .7, .4}};
          for (unsigned ch = 0; ch < 3; ++ch)
            bad += std::abs(f.rgba[pixel * 4 + ch] -
                            sampled[ch] / primary[0][ch] * primary[lane][ch]) > 3;
          bad += f.rgba[pixel * 4 + 3] != 255;
          depthBad += std::abs(f.depth[pixel] - planes[lane]) > 2e-5;
        }
      }
      if (exterior) {
        ++outside;
        for (unsigned ch = 0; ch < 4; ++ch)
          bad += f.rgba[pixel * 4 + ch] > (ch == 3 ? 1 : 3);
        depthBad += f.depth[pixel] != 1;
      }
      // At silhouettes, the selected sample depth is geometric or clear.
      depthBad += f.depth[pixel] != 1 &&
                  std::abs(f.depth[pixel] - planes[0]) > 2e-5 &&
                  std::abs(f.depth[pixel] - planes[1]) > 2e-5;
      double best = 1e9;
      unsigned level = 0;
      for (unsigned k = 0; k <= config.samples; ++k) {
        const double error =
            std::abs(f.rgba[pixel * 4 + 3] - 255. * k / config.samples);
        if (error < best) {
          best = error;
          level = k;
        }
      }
      bad += best > 1;
      if (level && level < config.samples) {
        ++partial;
        odd += level % 2;
      }
    }
  std::cout << "texture2d_depth_pair inside=" << inside[0] << "," << inside[1]
            << " outside=" << outside << " bad=" << bad
            << " depth_bad=" << depthBad << " partial=" << partial
            << " odd=" << odd << std::endl;
  return check(inside[0] > 100 && inside[1] > 100 && outside > 500 && !bad &&
                   !depthBad &&
                   (config.samples == 1 ||
                    (partial > 0 && (config.samples != 4 || odd > 0))),
               "shared-geometry occurrences preserve independent material, "
               "depth and sample coverage");
}
bool depthPair(Target &target, Fixture &scene) {
  // The detached tail owns references to these shared nodes throughout.
  scene.root->removeChild(scene.points);
  scene.root->removeChild(scene.face);
  for (unsigned lane = 0; lane < 2; ++lane) {
    auto *occurrence = new SoSeparator;
    auto *translate = new SoTranslation;
    translate->translation.setValue(lane ? .5f : -.5f, 0, 0);
    occurrence->addChild(translate);
    auto *scale = new SoScale;
    scale->scaleFactor.setValue(.6f, .6f, .6f);
    occurrence->addChild(scale);
    auto *material = new SoMaterial;
    material->diffuseColor.setValue(lane ? .2f : .8f, lane ? .7f : .3f,
                                    lane ? .4f : .2f);
    material->transparency = 0;
    occurrence->addChild(material);
    if (lane) {
      auto *depth = new SoDepthBuffer;
      depth->test = TRUE;
      depth->write = TRUE;
      depth->function = SoDepthBuffer::LESS;
      depth->range.setValue(.2f, .8f);
      occurrence->addChild(depth);
      auto *offset = new SoPolygonOffset;
      offset->on = TRUE;
      offset->styles = SoPolygonOffset::FILLED;
      offset->factor = 1;
      offset->units = 1;
      occurrence->addChild(offset);
    }
    occurrence->addChild(scene.points);
    occurrence->addChild(scene.face);
    scene.root->addChild(occurrence);
  }
  scene.pairedDepth = true;
  Frame optimized, general, restored;
  bool ok = target.draw(scene, optimized, false,
                        "single frame native and custom range/offset writers");
  bool native = false, custom = false;
  for (const auto &state : target.counter->plan.renderStates)
    if (state.hasTexture) {
      native |= state.depthRange[0] == 0 && state.depthRange[1] == 1 &&
                !state.polygonOffsetEnabled;
      custom |= std::abs(state.depthRange[0] - .2f) < 1e-6f &&
                std::abs(state.depthRange[1] - .8f) < 1e-6f &&
                state.polygonOffsetEnabled && state.polygonOffsetFactor == 1 &&
                state.polygonOffsetUnits == 1;
    }
  ok &=
      check(native && custom,
            "both depth configurations are consumed in ONE actual submission");
  ok &= target.draw(scene, general, true,
                    "same two depth writers forced general");
  ok &= equivalent(optimized, general);
  ok &= target.draw(scene, restored, false,
                    "restore both specialized depth writers");
  ok &= equivalent(optimized, restored);
  return ok;
}
