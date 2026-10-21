// Included inside portable_module_test after Run. All expectations are
// authorial.
bool scopeImage(const Frame &f, const std::array<Color, 3> &colors) {
  if (f.rgba.size() != 64 * 64 * 4 || f.depth.size() != 64 * 64)
    return false;
  unsigned inside[3] = {0, 0, 0}, outside = 0, bad = 0, depthBad = 0;
  const double margin = 2.1 * 2 / 64;
  for (unsigned y = 0; y < 64; ++y)
    for (unsigned x = 0; x < 64; ++x) {
      const double wx = (x + .5) * 2 / 64 - 1, wy = 1 - (y + .5) * 2 / 64;
      const size_t p = size_t(y) * 64 + x;
      bool exterior = true;
      for (unsigned lane = 0; lane < 3; ++lane) {
        const double center = (int(lane) - 1) * .64, lx = (wx - center) / .32,
                     ly = wy / .32;
        const bool in = lx > -.75 + margin / .32 && lx < .75 - margin / .32 &&
                        ly > -.71875 + margin / .32 && ly < .75 - margin / .32;
        const bool out = lx < -.75 - margin / .32 || lx > .75 + margin / .32 ||
                         ly < -.71875 - margin / .32 || ly > .75 + margin / .32;
        exterior &= out;
        if (in) {
          ++inside[lane];
          for (unsigned c = 0; c < 4; ++c)
            bad += std::abs(f.rgba[p * 4 + c] - 255. * colors[lane][c]) >
                   (c == 3 ? 1 : 3);
          depthBad += std::abs(f.depth[p] - 2.9 / 9.9) > 2e-5;
        }
      }
      if (exterior) {
        ++outside;
        for (unsigned c = 0; c < 4; ++c)
          bad += f.rgba[p * 4 + c] > (c == 3 ? 1 : 3);
        depthBad += f.depth[p] != 1;
      }
      depthBad += f.depth[p] != 1 && std::abs(f.depth[p] - 2.9 / 9.9) > 2e-5;
    }
  std::cout << "portable_module_scope inside=" << inside[0] << "," << inside[1]
            << "," << inside[2] << " outside=" << outside << " bad=" << bad
            << " depth_bad=" << depthBad << std::endl;
  return check(inside[0] > 50 && inside[1] > 50 && inside[2] > 50 &&
                   outside > 500 && !bad && !depthBad,
               "three independently shaded shared-shape occurrences and "
               "Separator restoration");
}
bool scoped(Run &run) {
  auto &scene = run.scene;
  auto *geometry = new SoGroup;
  geometry->ref();
  geometry->addChild(scene.positions);
  geometry->addChild(scene.mesh);
  scene.root->removeChild(scene.positions);
  scene.root->removeChild(scene.mesh);
  auto *other = new SoShaderProgram;
  other->ref();
  auto *fragment = new SoFragmentShader;
  fragment->sourceType = SoShaderObject::GLSL_PROGRAM;
  fragment->sourceProgram = scene.fragment->sourceProgram;
  fragment->isActive = TRUE;
  other->shaderObject.set1Value(0, fragment);
  auto *a = scene.parameter4("gain", {{.17f, .63f, .31f, 1}});
  auto *b = new SoShaderParameter4f;
  b->name = "gain";
  b->value.setValue(.8f, .4f, .6f, 1);
  fragment->parameter.set1Value(0, b);
  Module ma;
  declare(ma, "gain", Type::FLOAT4);
  ma.expressions.push_back(uniform("gain", Type::FLOAT4));
  Module mb = ma;
  mb.expressions.push_back(constant({{.5f, .75f, .25f, 1}}));
  mb.expressions.push_back(operation(Op::MULTIPLY, Type::FLOAT4, 0, 1));
  mb.output = 2;
  bool ok = run.bind(ma);
  ok &= check(run.action.setPortableShaderModule(other, mb),
              "second independent module association");
  mb.expressions[1].value = {{.9f, .9f, .9f, 1}};
  SoSeparator *branches[3];
  const Color base[3] = {
      {{.75f, .4f, .25f, 1}}, {{.25f, .7f, .4f, 1}}, {{.35f, .3f, .8f, 1}}};
  for (unsigned lane = 0; lane < 3; ++lane) {
    auto *branch = new SoSeparator;
    branch->ref();
    branches[lane] = branch;
    auto *move = new SoTranslation;
    move->translation.setValue((int(lane) - 1) * .64f, 0, 0);
    branch->addChild(move);
    auto *scale = new SoScale;
    scale->scaleFactor.setValue(.32f, .32f, .32f);
    branch->addChild(scale);
    auto *material = new SoMaterial;
    material->diffuseColor.setValue(base[lane][0], base[lane][1],
                                    base[lane][2]);
    material->transparency = 0;
    branch->addChild(material);
    if (lane != 1)
      branch->addChild(lane == 0 ? scene.program : other);
    branch->addChild(geometry);
    scene.root->addChild(branch);
  }
  std::array<Color, 3> colors;
  colors[0] = {{.17f, .63f, .31f, 1}};
  colors[1] = base[1];
  colors[2] = {{.4f, .3f, .15f, 1}};
  Frame first, changed, back;
  auto draw = [&](Frame &frame, const char *label) {
    return run.render(frame, label) && scopeImage(frame, colors);
  };
  ok &= draw(first,
             "two distinct modules/uniforms and ordinary sibling in one frame");
  a->value.setValue(.61f, .12f, .43f, 1);
  colors[0] = {{.61f, .12f, .43f, 1}};
  ok &= draw(changed, "only first occurrence uniform changes");
  ok &= check(
      run.lastCompiles.empty(),
      "same-module value mutation reuses compiled variant across occurrences");
  a->value.setValue(.17f, .63f, .31f, 1);
  colors[0] = {{.17f, .63f, .31f, 1}};
  ok &= draw(back, "independent source values A/B/A restored");
  for (auto *branch : branches)
    scene.root->removeChild(branch);
  for (int lane = 2; lane >= 0; --lane)
    scene.root->addChild(branches[lane]);
  ok &= draw(back, "reverse public occurrence order preserves each module and "
                   "ordinary sibling");
  fragment->isActive = FALSE;
  colors[2] = base[2];
  ok &= draw(back,
             "inactive shader object does not activate its registered module");
  fragment->isActive = TRUE;
  colors[2] = {{.4f, .3f, .15f, 1}};
  ok &= draw(back, "reactivated object restores exact module meaning");
  run.action.clearPortableShaderModule(other);
  ok &= run.refuse(back, "cleared active second module refuses entire frame "
                         "before partial publication");
  mb.expressions[1].value = {{.5f, .75f, .25f, 1}};
  ok &= check(run.action.setPortableShaderModule(other, mb),
              "second module recovery association");
  ok &= draw(back, "same-target scoped recovery after transactional refusal");
  // An empty/inactive occurrence must replace inherited module state, while
  // leaving its Separator restores the parent. The first branch intentionally
  // inherits that parent instead of redundantly re-establishing it.
  auto *empty = new SoShaderProgram;
  Module emptyModule = literal({{.9f, .2f, .4f, 1}});
  emptyModule.alphaMayVary = FALSE;
  ok &= check(run.action.setPortableShaderModule(empty, emptyModule),
              "module may be registered before any shader objects exist");
  scene.root->insertChild(scene.program, scene.root->findChild(branches[2]));
  branches[0]->removeChild(scene.program);
  branches[1]->insertChild(empty, branches[1]->findChild(geometry));
  ok &= draw(back, "empty registered program disables inherited module only "
                   "inside its Separator");
  auto *off = new SoFragmentShader;
  off->isActive = FALSE;
  off->sourceType = SoShaderObject::GLSL_PROGRAM;
  off->sourceProgram = "void main(){gl_FragColor=vec4(1.0);}";
  off->ref();
  empty->shaderObject.set1Value(0, off);
  ok &= draw(back,
             "all shader objects inactive also replace inherited module state");
  off->isActive = TRUE;
  colors[1] = {{.9f, .2f, .4f, 1}};
  ok &= draw(
      back,
      "one active object enables the registered constant without parameters");
  off->isActive = FALSE;
  colors[1] = base[1];
  ok &= draw(back,
             "deactivation restores ordinary child and active parent sibling");
  empty->shaderObject.setNum(0);
  ok &= draw(back, "empty A/B/A retains parent module after scoped override");
  off->unref();
  for (auto *branch : branches)
    branch->unref();
  geometry->unref();
  other->unref();
  return ok;
}
