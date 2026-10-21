// Included inside portable_module_test after Run; no production sampler is
// used.
struct TextureInputs {
  Run &run;
  SoComplexity *quality = new SoComplexity;
  SoTexture2 *images[8]{};
  SoTextureCoordinate2 *coords[8]{};
  SoTextureMatrixTransform *matrices[8]{};
  std::vector<uint8_t> bytes[8];
  explicit TextureInputs(Run &r) : run(r) {
    quality->textureQuality = .1f;
    insert(quality);
  }
  void insert(SoNode *node) {
    auto &s = run.scene;
    s.root->insertChild(
        node,
        s.root->findChild(s.attached ? static_cast<SoNode *>(s.program)
                                     : static_cast<SoNode *>(s.positions)));
  }
  void add(unsigned unit, const std::vector<uint8_t> &pixels, unsigned size) {
    auto *selector = new SoTextureUnit;
    selector->unit = unit;
    insert(selector);
    images[unit] = new SoTexture2;
    images[unit]->model = SoTexture2::MODULATE;
    images[unit]->wrapS = images[unit]->wrapT = SoTexture2::REPEAT;
    bytes[unit] = pixels;
    images[unit]->image.setValue(SbVec2s(size, size), 4, pixels.data());
    insert(images[unit]);
    coords[unit] = new SoTextureCoordinate2;
    setCoords(unit, SbVec2f(.14f, .26f));
    insert(coords[unit]);
    matrices[unit] = new SoTextureMatrixTransform;
    SbMatrix matrix = SbMatrix::identity();
    matrix[0][0] = 2;
    matrix[1][1] = 1.5f;
    matrix[2][2] = .5f;
    matrix[3][0] = .02f;
    matrix[3][1] = .05f;
    matrix[3][2] = .09f;
    // SoTextureCoordinate2 supplies (S,T,0,1); this matrix supplies
    // effective R=.09 and Q=1 before projective division.
    matrix[3][3] = 1;
    matrices[unit]->matrix = matrix;
    insert(matrices[unit]);
    auto *reset = new SoTextureUnit;
    reset->unit = 0;
    insert(reset);
  }
  void setCoords(unsigned unit, const SbVec2f &value) {
    for (unsigned i = 0; i < 4; ++i)
      coords[unit]->point.set1Value(i, value);
  }
  void upload(unsigned unit, unsigned side,
              const std::vector<uint8_t> &pixels) {
    bytes[unit] = pixels;
    images[unit]->image.setValue(SbVec2s(side, side), 4, pixels.data());
  }
  bool delivery(unsigned unit, CoinRenderTextureFilter filter, bool mips) {
    const auto &plan = run.audit->plan;
    bool found = false, ok = true;
    for (const auto &draw : plan.draws) {
      if (draw.sourceNodeId != run.scene.mesh->getNodeId())
        continue;
      found = true;
      const auto layer = coin_render_texture_unit(
          plan.renderStates[draw.renderStateSlot], unit);
      ok &= check(layer.enabled && layer.imageSlot < plan.textures.size() &&
                      layer.samplerSlot < plan.samplers.size(),
                  "module logical image/sampler survives actual Core capture");
      if (layer.imageSlot >= plan.textures.size() ||
          layer.samplerSlot >= plan.samplers.size())
        continue;
      ok &= check(
          plan.textures[layer.imageSlot].pixelsRgba == bytes[unit] &&
              plan.samplers[layer.samplerSlot].filter == filter &&
              plan.textures[layer.imageSlot].mipmapped == mips,
          "authored image bytes, filter and mip declaration are consumed");
      const auto expected = matrices[unit]->matrix.getValue();
      for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 4; ++c)
          ok &= check(
              layer.matrix[r][c] == expected[r][c],
              "authorial per-unit texture matrix reaches Core exactly once");
    }
    return ok && check(found, "textured source mesh actually submitted");
  }
};
Color sample2x2(const std::vector<uint8_t> &pixels, double s, double t,
                bool repeat, bool linear) {
  auto coordinate = [&](double x) {
    return repeat ? x - std::floor(x) : std::max(0., std::min(1., x));
  };
  s = coordinate(s);
  t = coordinate(t);
  Color out = {{0, 0, 0, 0}};
  auto index = [&](int x) {
    return repeat ? (x % 2 + 2) % 2 : std::max(0, std::min(1, x));
  };
  if (!linear) {
    const unsigned x = unsigned(std::min(1., std::floor(s * 2))),
                   y = unsigned(std::min(1., std::floor(t * 2)));
    for (unsigned c = 0; c < 4; ++c)
      out[c] = pixels[(y * 2 + x) * 4 + c] / 255.f;
  } else {
    const double xx = s * 2 - .5, yy = t * 2 - .5;
    const int x = int(std::floor(xx)), y = int(std::floor(yy));
    const double fx = xx - x, fy = yy - y;
    for (int j = 0; j < 2; ++j)
      for (int i = 0; i < 2; ++i)
        for (unsigned c = 0; c < 4; ++c)
          out[c] += float(pixels[(index(y + j) * 2 + index(x + i)) * 4 + c] /
                          255. * (i ? fx : 1 - fx) * (j ? fy : 1 - fy));
  }
  for (unsigned c = 0; c < 3; ++c)
    out[c] *= (c == 0 ? .6f : c == 1 ? .3f : .8f);
  return out;
}
Module sampled(unsigned unit) {
  Module m;
  auto coord = operation(Op::TEXCOORD, Type::FLOAT4);
  coord.textureUnit = unit;
  auto sample = operation(Op::SAMPLE_2D, Type::FLOAT4, 1);
  sample.textureUnit = unit;
  m.expressions = {operation(Op::PRIMARY_COLOR, Type::FLOAT4), coord, sample,
                   operation(Op::MULTIPLY, Type::FLOAT4, 0, 2)};
  m.output = 3;
  return m;
}
bool textures(Run &run) {
  run.scene.attach(true);
  TextureInputs inputs(run);
  const std::vector<uint8_t> pixels = {30, 210, 70,  255, 220, 60,  160, 255,
                                       95, 120, 235, 255, 180, 200, 20,  255};
  inputs.add(7, pixels, 2);
  Frame frame, stable;
  Module coordinates;
  auto expression = operation(Op::TEXCOORD, Type::FLOAT4);
  expression.textureUnit = 7;
  coordinates.expressions.push_back(expression);
  bool ok = run.bind(coordinates);
  ok &= run.draw({{.3f, .44f, .09f, 1}}, frame,
                 "effective STRQ after exactly one public per-unit matrix");
  Module module = sampled(7);
  ok &= run.bind(module);
  SbMatrix projective = inputs.matrices[7]->matrix.getValue();
  projective[3][3] = .5f;
  inputs.matrices[7]->matrix = projective;
  ok &= run.draw(sample2x2(pixels, .6, .88, true, false), frame,
                 "generated Q=.5 divides effective ST exactly once");
  projective[3][3] = 1;
  inputs.matrices[7]->matrix = projective;

  const CoinRenderTextureFilter filters[] = {
      CoinRenderTextureFilter::NEAREST, CoinRenderTextureFilter::LINEAR,
      CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR,
      CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR};
  const float qualities[] = {.1f, .3f, .6f, .9f};
  for (unsigned i = 0; i < 4; ++i) {
    inputs.quality->textureQuality = qualities[i];
    inputs.images[7]->image.touch();
    ok &= run.draw(sample2x2(pixels, .3, .44, true, i != 0), frame,
                   "unit7 texcoord/sample/primary with zero derivatives and "
                   "public filter");
    ok &= inputs.delivery(7, filters[i], i >= 2);
  }
  // The effective outside coordinates1.3,-.56 repeat to.3,.44. CLAMP
  // independently selects the boundary texel; no image is used as oracle.
  inputs.setCoords(7, SbVec2f(.64f, float((-.56 - .05) / 1.5)));
  ok &= run.draw(sample2x2(pixels, 1.3, -.56, true, true), frame,
                 "repeat outside homogeneous coordinates");
  inputs.images[7]->wrapS = inputs.images[7]->wrapT = SoTexture2::CLAMP;
  ok &= run.draw(sample2x2(pixels, 1.3, -.56, false, true), stable,
                 "clamp distinguishes the same outside coordinates");
  SbMatrix invalidQ = inputs.matrices[7]->matrix.getValue();
  invalidQ[3][3] = 0;
  inputs.matrices[7]->matrix = invalidQ;
  ok &= run.refuse(
      stable,
      "zero projective Q cannot silently sample the previous coordinate");
  invalidQ[3][3] = 1;
  inputs.matrices[7]->matrix = invalidQ;
  inputs.setCoords(7, SbVec2f(.14f, .26f));
  ok &= run.draw(sample2x2(pixels, .3, .44, false, true), stable,
                 "projective Q restoration on same target");
  inputs.setCoords(7, SbVec2f(std::numeric_limits<float>::quiet_NaN(), .26f));
  ok &= run.refuse(
      stable,
      "nonfinite actual per-unit coordinate rejects before publication");
  inputs.setCoords(7, SbVec2f(.14f, .26f));
  ok &= run.draw(sample2x2(pixels, .3, .44, false, true), frame,
                 "coordinate NaN recovery with current delivery");
  inputs.quality->textureQuality = .1f;
  const std::vector<uint8_t> white = {255, 255, 255, 255};
  inputs.upload(7, 1, white);
  for (unsigned unit = 0; unit < 7; ++unit)
    inputs.add(unit, white, 1);
  Module cascade;
  cascade.expressions.push_back(operation(Op::PRIMARY_COLOR, Type::FLOAT4));
  uint32_t previous = 0;
  for (unsigned unit = 0; unit < 8; ++unit) {
    auto coord = operation(Op::TEXCOORD, Type::FLOAT4);
    coord.textureUnit = unit;
    cascade.expressions.push_back(coord);
    const uint32_t index = uint32_t(cascade.expressions.size() - 1);
    auto sample = operation(Op::SAMPLE_2D, Type::FLOAT4, index);
    sample.textureUnit = unit;
    cascade.expressions.push_back(sample);
    cascade.expressions.push_back(
        operation(Op::MULTIPLY, Type::FLOAT4, previous, index + 1));
    previous = index + 2;
  }
  cascade.output = previous;
  ok &= run.bind(cascade);
  ok &= run.draw({{.6f, .3f, .8f, 1}}, stable,
                 "eight logical2D bindings and eight actual samples");
  for (unsigned unit = 0; unit < 8; ++unit) {
    const std::vector<uint8_t> color = {uint8_t(70 + unit * 9),
                                        uint8_t(180 - unit * 7),
                                        uint8_t(210 - unit * 5), 255};
    inputs.upload(unit, 1, color);
    const Color expected = {
        {.6f * color[0] / 255, .3f * color[1] / 255, .8f * color[2] / 255, 1}};
    ok &= run.draw(
        expected, frame,
        "single logical unit mutation has independent product contribution");
    for (unsigned u = 0; u < 8; ++u)
      ok &= inputs.delivery(u, CoinRenderTextureFilter::NEAREST, false);
    inputs.upload(unit, 1, white);
    ok &= run.draw({{.6f, .3f, .8f, 1}}, stable,
                   "per-unit image A/B/A restoration");
  }
  inputs.images[4]->image.setValue(SbVec2s(0, 0), 4, nullptr);
  ok &= run.refuse(
      stable, "referenced but absent logical unit4 is not a white fallback");
  inputs.upload(4, 1, white);
  ok &= run.draw({{.6f, .3f, .8f, 1}}, frame, "missing logical unit recovery");
  return ok;
}
