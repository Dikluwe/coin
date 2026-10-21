#ifndef COIN_PORTABLE_SHADER_RTT_CASES_H
#define COIN_PORTABLE_SHADER_RTT_CASES_H
// Producer covers its complete square. Constant center sampling in the
// consumer isolates the producer resolve from consumer silhouette coverage;
// no k/N claim is made about two independently varying silhouettes.
bool rttDelivery(Run &run, const Scene &source,
                 CoinRenderSceneTextureMode route, Color sourceColor) {
  bool ok = check(run.audit->graph.mode == route &&
                      run.audit->graph.producers.size() == 1,
                  "one real module producer graph and requested RTT route");
  if (!ok)
    return false;
  const auto &p = run.audit->graph.producers[0];
  ok &= check(!p.cube && p.size == SbVec2i32(64, 64) &&
                  p.format == CoinRenderTextureFormat::RGBA8_LINEAR,
              "module producer dimensions and format retained");
  unsigned sourceDraws = 0;
  for (const auto &d : p.plan.draws)
    sourceDraws += d.sourceNodeId == source.mesh->getNodeId();
  ok &= check(
      sourceDraws == 2 || sourceDraws == 1,
      "producer captures actual authored mesh rather than replacement pixels");
  unsigned images = 0;
  for (const auto &t : run.audit->plan.textures) {
    if (t.width != 64 || t.height != 64)
      continue;
    ++images;
    if (route == COIN_RENDER_SCENE_TEXTURE_DIRECT)
      ok &= check(
          t.gpuToken && t.pixelsRgba.empty() && t.mipmapsRgba.empty() &&
              run.audit->producerSamples == run.samples,
          "DIRECT consumer uses real GPU token without intermediate CPU image");
    else {
      bool payload = !t.gpuToken && t.pixelsRgba.size() == 64 * 64 * 4;
      unsigned bad = 0;
      for (size_t j = 0; payload && j < t.pixelsRgba.size(); ++j)
        bad += std::abs(t.pixelsRgba[j] - 255. * sourceColor[j % 4]) >
               (j % 4 == 3 ? 1 : 3);
      ok &= check(
          payload && !bad,
          "STAGED complete source RGBA payload matches independent AST color");
    }
  }
  return check(images == 1, "consumer binds exactly the actual RTT resource") &&
         ok;
}
bool rtt(Run &run, CoinRenderSceneTextureMode route) {
  Scene source;
  source.attach(true);
  const SbVec3f full[] = {SbVec3f(-1.2f, -1.2f, 0), SbVec3f(1.2f, -1.2f, 0),
                          SbVec3f(1.2f, 1.2f, 0), SbVec3f(-1.2f, 1.2f, 0)};
  source.positions->point.setValues(0, 4, full);
  const Color a = {{.2f, .55f, .7f, 1}}, b = {{.7f, .2f, .3f, 1}};
  auto *value = source.parameter4("rgba", a);
  bool ok = check(
      run.action.setPortableShaderModule(source.program, presentationModule()),
      "producer has its own explicit portable association");
  auto *texture = new SoSceneTexture2;
  texture->scene = source.root;
  texture->size.setValue(64, 64);
  texture->type = SoSceneTexture2::RGBA8;
  texture->transparencyFunction = SoSceneTexture2::NONE;
  texture->backgroundColor.setValue(.1f, .2f, .3f, 0);
  texture->model = SoSceneTexture2::MODULATE;
  run.scene.root->insertChild(texture,
                              run.scene.root->findChild(run.scene.positions));
  auto *quality = new SoComplexity;
  quality->textureQuality = .1f;
  run.scene.root->insertChild(quality, run.scene.root->findChild(texture));
  run.scene.attach(true);
  presentationGeometry(run.scene);
  Module consumer;
  consumer.expressions.push_back(constant({{.5f, .5f, 0, 1}}));
  Expr sample = operation(Op::SAMPLE_2D, Type::FLOAT4, 0);
  sample.textureUnit = 0;
  consumer.expressions.push_back(sample);
  consumer.expressions.push_back(constant({{.6f, .8f, .5f, 1}}));
  consumer.expressions.push_back(operation(Op::MULTIPLY, Type::FLOAT4, 1, 2));
  consumer.output = 3;
  consumer.alphaMayVary = TRUE;
  ok &= run.bind(consumer);
  auto expected = [](Color c) {
    return Color{{c[0] * .6f, c[1] * .8f, c[2] * .5f, 1}};
  };
  auto draw = [&](Color c, Frame &f, const char *label) {
    const auto producers = run.audit->producers;
    const auto sourceId = source.root->getNodeId();
    const auto programId = source.program->getNodeId();
    const bool submitted = presented(run, expected(c), f, label);
    bool current = check(run.target->getOptions().sceneTexture == route &&
                             source.root->getNodeId() == sourceId &&
                             source.program->getNodeId() == programId,
                         "RTT immutable option retains selected route");
    if (submitted) {
      current &= rttDelivery(run, source, route, c);
      current &= check(
          run.audit->producers ==
              producers + (route == COIN_RENDER_SCENE_TEXTURE_DIRECT ? 1 : 0),
          "DIRECT producer versus factory-created STAGED child executes once");
    }
    return submitted && current;
  };
  Frame first, changed, restored;
  ok &= draw(a, first, "typed module producer and typed sampling consumer");
  value->value.setValue(b[0], b[1], b[2], 1);
  ok &= draw(b, changed, "producer parameter edit updates dependent resource");
  ok &=
      check(run.lastCompiles.empty(),
            "producer values change without recompiling same module variants");
  value->value.setValue(a[0], a[1], a[2], 1);
  ok &= draw(a, restored, "source A/B/A current consumer output");
  const Frame physical = run.lastPhysical;
  value->value.setValue(a[0], std::numeric_limits<float>::quiet_NaN(), a[2], 1);
  ok &= run.refuse(restored,
                   "invalid source signature preflight executes no producer");
  ok &= untouched(run, restored, physical,
                  "preflight refuses without changing parent publication");
  value->value.setValue(a[0], a[1], a[2], 1);
  ok &= draw(a, restored, "source binding recovery on same parent target");
  const Frame beforeLate = restored, physicalBeforeLate = run.lastPhysical;
  const auto producersBeforeLate = run.audit->producers;
  const auto entriesBeforeLate = run.audit->entries;
  const auto retiredBeforeLate = run.audit->retiredRtt;
  run.audit->failRoot = true;
  run.action.apply(run.scene.root);
  run.audit->failRoot = false;
  ok &= check(
      run.action.getLastStatus() != CoinRenderAction::SUCCESS &&
          run.action.getLastError().getLength() > 0 &&
          run.audit->entries == entriesBeforeLate + 1 &&
          run.audit->producers ==
              producersBeforeLate +
                  (route == COIN_RENDER_SCENE_TEXTURE_DIRECT ? 1 : 0),
      "independent late parent fault occurs after real producer preparation");
  if (route == COIN_RENDER_SCENE_TEXTURE_DIRECT)
    ok &= check(run.audit->retiredRtt > retiredBeforeLate,
                "failed parent retires the prepared DIRECT temporary token");
  ok &= untouched(run, beforeLate, physicalBeforeLate,
                  "late parent refusal keeps old color/depth/serial exactly");
  ok &= draw(a, restored,
             "late parent recovery reuses same target and valid source");
  return ok;
}
#endif
