// Included after Run: lighting/fog/clip/alpha/depth remain framework owners.
bool ownerImage(const Frame &f, Color color, bool clip = false,
                bool discard = false, double plane = 2.9 / 9.9) {
  if (f.rgba.size() != 64 * 64 * 4 || f.depth.size() != 64 * 64)
    return false;
  unsigned inside = 0, outside = 0, bad = 0, depthBad = 0;
  const double margin = 2.1 * 2 / 64;
  for (unsigned y = 0; y < 64; ++y)
    for (unsigned x = 0; x < 64; ++x) {
      double wx = (x + .5) * 2 / 64 - 1, wy = 1 - (y + .5) * 2 / 64;
      size_t p = size_t(y) * 64 + x;
      bool in = wx > -.75 + margin && wx < .75 - margin &&
                wy > -.71875 + margin && wy < .75 - margin &&
                (!clip || wx > margin);
      bool out = discard || wx < -.75 - margin || wx > .75 + margin ||
                 wy < -.71875 - margin || wy > .75 + margin ||
                 (clip && wx < -margin);
      if (in && !discard) {
        ++inside;
        for (unsigned c = 0; c < 4; ++c)
          bad +=
              std::abs(f.rgba[p * 4 + c] - 255. * color[c]) > (c == 3 ? 1 : 3);
        depthBad += std::abs(f.depth[p] - plane) > 2e-5;
      }
      if (out) {
        ++outside;
        for (unsigned c = 0; c < 4; ++c)
          bad += f.rgba[p * 4 + c] > (c == 3 ? 1 : 3);
        depthBad += f.depth[p] != 1;
      }
      depthBad += f.depth[p] != 1 && std::abs(f.depth[p] - plane) > 2e-5;
    }
  std::cout << "portable_module_owner inside=" << inside
            << " outside=" << outside << " bad=" << bad
            << " depth_bad=" << depthBad << std::endl;
  return check((discard || inside > 500) && outside > 500 && !bad && !depthBad,
               "framework-owned composition/clip/depth and module RGBA");
}
bool owners(Run &run) {
  run.scene.attach(true);
  const Color output = {{.2f, .55f, .75f, 1}};
  Module m = literal(output);
  m.alphaMayVary = FALSE;
  bool ok = run.bind(m);
  Frame a, b, back;
  auto *fog = new SoEnvironment;
  fog->fogColor.setValue(.12f, .35f, .6f);
  fog->fogVisibility = 4;
  run.scene.root->insertChild(fog,
                              run.scene.root->findChild(run.scene.positions));
  const int modes[] = {SoEnvironment::NONE, SoEnvironment::HAZE,
                       SoEnvironment::FOG, SoEnvironment::SMOKE};
  for (unsigned mode = 0; mode < 4; ++mode) {
    fog->fogType = modes[mode];
    const double factor = mode == 0   ? 1
                          : mode == 1 ? .25
                          : mode == 2 ? std::exp(-5.545 * 3 / 4)
                                      : std::exp(-std::pow(2.35 * 3 / 4, 2));
    Color expected = output;
    const double color[] = {.12, .35, .6};
    for (unsigned c = 0; c < 3; ++c)
      expected[c] = float(output[c] * factor + color[c] * (1 - factor));
    ok &= run.draw(
        expected, b,
        "framework fog follows module color at authorial view distance3");
  }
  fog->fogType = SoEnvironment::NONE;
  auto *clip = new SoClipPlane;
  clip->plane = SbPlane(SbVec3f(1, 0, 0), 0);
  run.scene.root->insertChild(clip,
                              run.scene.root->findChild(run.scene.positions));
  ok &= run.render(b, "framework clip retains original geometry and depth") &&
        ownerImage(b, output, true);
  run.scene.root->removeChild(clip);
  ok &= run.draw(output, back, "fog/clip source restoration");
  SoDepthBuffer *depth = nullptr;
  for (int i = 0; i < run.scene.root->getNumChildren(); ++i)
    if (run.scene.root->getChild(i)->isOfType(SoDepthBuffer::getClassTypeId()))
      depth = static_cast<SoDepthBuffer *>(run.scene.root->getChild(i));
  if (!check(depth != nullptr, "authorial depth node exists"))
    return false;
  depth->function = SoDepthBuffer::LEQUAL;
  depth->range.setValue(.2f, .8f);
  ok &= run.render(b,
                   "module preserves authored LEQUAL and custom depth range") &&
        ownerImage(b, output, false, false, .2 + .6 * 2.9 / 9.9);
  const auto entries = run.audit->entries;
  depth->range.setValue(std::numeric_limits<float>::quiet_NaN(), .8f);
  ok &= run.refuse(
      b, "ordinary invalid depth range still rejects an adapted shader");
  ok &= check(run.audit->entries == entries,
              "invalid common depth state refuses before backend ingress");
  depth->range.setValue(0, 1);
  depth->function = SoDepthBuffer::LESS;
  run.scene.material->ambientColor.setValue(0, 0, 0);
  run.scene.material->emissiveColor.setValue(0, 0, 0);
  run.scene.material->specularColor.setValue(0, 0, 0);
  auto *light = new SoDirectionalLight;
  light->direction.setValue(0, 0, -1);
  light->color.setValue(.5f, .75f, 1);
  light->intensity = .4f;
  run.scene.root->insertChild(light,
                              run.scene.root->findChild(run.scene.positions));
  run.scene.model->model = SoLightModel::PHONG;
  Module primary;
  primary.expressions.push_back(operation(Op::PRIMARY_COLOR, Type::FLOAT4));
  ok &= run.bind(primary);
  ok &= run.draw({{.12f, .09f, .32f, 1}}, a,
                 "PRIMARY_COLOR observes framework lighting exactly once");
  // For positive diffuse and shininess zero the public Phong exponent is
  // zero, so the nonzero specular factor is exactly one. No interpolation or
  // driver-dependent power approximation enters this independent witness.
  run.scene.material->specularColor.setValue(.2f, .1f, .15f);
  run.scene.material->shininess = 0;
  ok &= run.draw({{.16f, .12f, .38f, 1}}, a,
                 "PRIMARY includes nonzero specular in framework lighting");
  ok &= run.bind(m);
  ok &= run.draw(
      output, b,
      "constant module is unchanged by diffuse and nonzero specular lighting");
  run.scene.material->specularColor.setValue(.35f, .2f, .05f);
  ok &= run.draw(output, b,
                 "specular mutation cannot append lighting after constant AST");
  run.scene.material->specularColor.setValue(.2f, .1f, .15f);
  ok &= run.draw(output, back,
                 "nonzero specular A/B/A retains module-owned RGBA");
  run.scene.model->model = SoLightModel::BASE_COLOR;
  ok &= run.draw(output, back, "same-target owner-state A/B/A recovery");
  return ok;
}
bool alpha(Run &run) {
  run.scene.attach(true);
  auto *value = run.scene.parameter4("rgba", {{.2f, .55f, .75f, .4f}});
  Module m;
  declare(m, "rgba", Type::FLOAT4);
  m.expressions.push_back(uniform("rgba", Type::FLOAT4));
  bool ok = run.bind(m);
  auto *test = new SoAlphaTest;
  test->function = SoAlphaTest::GREATER;
  test->value = .5f;
  run.scene.root->insertChild(test,
                              run.scene.root->findChild(run.scene.positions));
  Frame a, b, back;
  ok &= run.render(a, "AlphaTest sees actual module alpha below reference") &&
        ownerImage(a, {{0, 0, 0, 0}}, false, true);
  value->value.setValue(.2f, .55f, .75f, .7f);
  ok &= run.render(b, "same signature alpha above reference") &&
        ownerImage(b, {{.2f, .55f, .75f, .7f}});
  ok &= check(run.lastCompiles.empty(),
              "alpha value mutation reuses compiled module");
  test->function = SoAlphaTest::ALWAYS;
  value->value.setValue(.2f, .55f, .75f, .4f);
  run.action.setTransparencyType(CoinRenderAction::BLEND);
  ok &= run.render(back, "conservative variable-alpha scheduling enables "
                         "actual OBJECT blend") &&
        ownerImage(back, {{.08f, .22f, .3f, .4f}});
  Module primary;
  primary.expressions.push_back(operation(Op::PRIMARY_COLOR, Type::FLOAT4));
  run.scene.fragment->parameter.setNum(0);
  ok &= run.bind(primary);
  run.scene.material->transparency = .5f;
  ok &= run.render(back, "material alpha reaches PRIMARY before existing "
                         "object composition") &&
        ownerImage(back, {{.3f, .15f, .4f, .5f}});
  run.scene.material->transparency = 0;
  run.action.setTransparencyType(CoinRenderAction::NONE);
  run.scene.fragment->parameter.set1Value(0, value);
  value->value.setValue(.2f, .55f, .75f, 1);
  m.alphaMayVary = FALSE;
  ok &= check(!run.action.setPortableShaderModule(run.scene.program, m),
              "current uniform alpha1 is not a structural alpha proof");
  m.alphaMayVary = TRUE;
  ok &= run.bind(m);
  ok &= run.draw({{.2f, .55f, .75f, 1}}, back,
                 "valid alpha association recovers on same target");
  return ok;
}
bool bindings(Run &run) {
  run.scene.attach(true);
  run.scene.clearParameters();
  auto *value = new SoShaderParameter1f;
  value->ref();
  run.scene.owned.push_back(value);
  value->name = "denominator";
  value->value = 2;
  run.scene.fragment->parameter.set1Value(0, value);
  Module m;
  declare(m, "denominator", Type::FLOAT1);
  m.expressions = {constant({{.24f, .44f, .64f, 2}}),
                   uniform("denominator", Type::FLOAT1),
                   operation(Op::DIVIDE, Type::FLOAT4, 0, 1)};
  m.output = 2;
  Frame stable, changed, back;
  bool ok = run.bind(m);
  ok &= run.draw({{.12f, .22f, .32f, 1}}, stable,
                 "actual FLOAT1 denominator binding");
  for (float bad : {0.f, -0.f, std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::denorm_min()}) {
    value->value = bad;
    ok &= run.refuse(stable, "zero/nonfinite/overflowing actual denominator "
                             "cannot publish NaN or Inf");
  }
  value->value = 4;
  ok &= run.draw({{.06f, .11f, .16f, .5f}}, changed,
                 "finite denominator value updates RGBA without recompilation");
  ok &= check(run.lastCompiles.empty(),
              "FLOAT1 payload mutation does not compile/link");
  value->value = 2;
  ok &=
      run.draw({{.12f, .22f, .32f, 1}}, stable, "uniform denominator recovery");
  auto *wrong = new SoShaderParameter1i;
  wrong->ref();
  wrong->name = "denominator";
  wrong->value = 2;
  run.scene.fragment->parameter.set1Value(0, wrong);
  ok &= run.refuse(stable, "integer parameter is not a FLOAT1 value");
  run.scene.fragment->parameter.set1Value(0, value);
  wrong->unref();
  auto *duplicate = new SoShaderParameter1f;
  duplicate->ref();
  duplicate->name = "denominator";
  duplicate->value = 2;
  run.scene.fragment->parameter.set1Value(1, duplicate);
  ok &= run.refuse(stable, "duplicate active parameter name rejects");
  duplicate->name = "extra";
  ok &= run.refuse(stable, "extra undeclared active parameter rejects");
  run.scene.fragment->parameter.setNum(1);
  duplicate->unref();
  value->name = "Denominator";
  ok &= run.refuse(stable, "exact case-sensitive actual signature required");
  value->name = "denominator";
  auto *inactive = new SoFragmentShader;
  inactive->sourceType = SoShaderObject::GLSL_PROGRAM;
  inactive->sourceProgram = run.scene.fragment->sourceProgram;
  inactive->isActive = FALSE;
  auto *second = new SoShaderParameter1f;
  second->name = "denominator";
  second->value = 9;
  inactive->parameter.set1Value(0, second);
  run.scene.program->shaderObject.set1Value(1, inactive);
  ok &= run.draw(
      {{.12f, .22f, .32f, 1}}, back,
      "inactive object's parameters do not enter the active signature");
  inactive->isActive = TRUE;
  ok &= run.refuse(
      back, "duplicate signature across active objects rejects atomically");
  inactive->isActive = FALSE;
  m.expressions[0].value = {{-.24f, -.44f, -.64f, -2}};
  value->value = -2;
  ok &= run.bind(m);
  ok &= run.draw({{.12f, .22f, .32f, 1}}, back,
                 "negative nonzero denominator remains defined");
  run.scene.program->shaderObject.setNum(1);
  for (unsigned n : {2u, 3u}) {
    run.scene.clearParameters();
    Module typed;
    declare(typed, "vector", static_cast<Type>(n));
    if (n == 2) {
      auto *p = new SoShaderParameter2f;
      p->ref();
      run.scene.owned.push_back(p);
      p->name = "vector";
      p->value.setValue(.2f, .4f);
      run.scene.fragment->parameter.set1Value(0, p);
    } else {
      auto *p = new SoShaderParameter3f;
      p->ref();
      run.scene.owned.push_back(p);
      p->name = "vector";
      p->value.setValue(.2f, .4f, .6f);
      run.scene.fragment->parameter.set1Value(0, p);
    }
    typed.expressions = {
        uniform("vector", static_cast<Type>(n)),
        constant(n == 2 ? Color{{.5f, .25f, 0, 0}} : Color{{.5f, .25f, .5f, 0}},
                 static_cast<Type>(n)),
        operation(Op::DOT, Type::FLOAT1, 0, 1),
        constant({{.2f, .4f, .6f, n == 2 ? 5.f : 2.f}}),
        operation(Op::MULTIPLY, Type::FLOAT4, 2, 3)};
    typed.output = 4;
    ok &= run.bind(typed);
    ok &= run.draw(
        n == 2 ? Color{{.04f, .08f, .12f, 1}} : Color{{.1f, .2f, .3f, 1}}, back,
        "typed FLOAT2/FLOAT3 payload and DOT reach actual shader");
  }
  return ok;
}
