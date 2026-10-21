#ifndef COIN_PORTABLE_SHADER_COMPILER_PRODUCER_CASES_H
#define COIN_PORTABLE_SHADER_COMPILER_PRODUCER_CASES_H
#include "CoinRenderPortableShaderDelivery.h"
Color compilerProducerColor(Color p) {
  return Color{{p[0] * .77f + .013741f, p[1] * .63f + .027319f,
                p[2] * .91f + .009153f, 1}};
}
Color compilerConsumerColor(Color b) {
  // Separate complete source A is ordinary (.6,.3,.8,1). Source B is
  // sampled at a constant center: two resolves do not mix silhouettes.
  return Color{{.6f * .25f + b[0] * .5f + .03f, .3f * .25f + b[1] * .5f + .02f,
                .8f * .25f + b[2] * .5f + .04f, 1}};
}
bool compilerProducerDelivery(Run &run, const Scene &a, const Scene &b,
                              const Module &module, Color parameters,
                              Color expectedB) {
  const auto &graph = run.audit->graph;
  bool ok = check(graph.mode == COIN_RENDER_SCENE_TEXTURE_STAGED &&
                      graph.producers.size() == 2 && run.samples == 4 &&
                      run.target->getOptions().sampleCount == 4,
                  "actual root MSAA4 and two public STAGED source occurrences");
  unsigned foundA = 0, foundB = 0;
  for (const auto &producer : graph.producers) {
    bool sourceA = false, sourceB = false;
    std::vector<uint32_t> draws;
    for (uint32_t i = 0; i < producer.plan.draws.size(); ++i) {
      const auto &d = producer.plan.draws[i];
      sourceA |= d.sourceNodeId == a.mesh->getNodeId();
      if (d.sourceNodeId == b.mesh->getNodeId()) {
        sourceB = true;
        draws.push_back(i);
        ok &=
            check(portableCoreBinding(
                      producer.plan, d.renderStateSlot, module,
                      std::vector<float>(parameters.begin(), parameters.end())),
                  "every fresh source triangle retains its own AST and values");
      }
    }
    foundA += sourceA;
    foundB += sourceB;
    if (sourceB)
      ok &=
          check(portableCoreQuad(producer.plan, draws, b.positions),
                "source module preserves complete disjoint authored coverage");
    ok &=
        check(!producer.cube && producer.size == SbVec2i32(64, 64) &&
                  producer.format == CoinRenderTextureFormat::RGBA8_LINEAR,
              "both declared producer dimensions/formats remain RGBA8 linear");
  }
  unsigned images = 0;
  bool colors[2] = {false, false};
  const Color expected[2] = {Color{{.6f, .3f, .8f, 1}}, expectedB};
  for (const auto &t : run.audit->plan.textures) {
    if (t.width != 64 || t.height != 64)
      continue;
    ++images;
    bool payload = !t.gpuToken && t.pixelsRgba.size() == 64 * 64 * 4;
    ok &=
        check(payload, "STAGED resources expose complete actual RGBA payloads");
    if (!payload)
      continue;
    for (unsigned source = 0; source < 2; ++source) {
      unsigned bad = 0;
      for (size_t i = 0; i < t.pixelsRgba.size(); ++i)
        bad += std::abs(t.pixelsRgba[i] - 255. * expected[source][i % 4]) >
               (i % 4 == 3 ? 1 : 3);
      colors[source] |= bad == 0;
    }
  }
  return check(foundA == 1 && foundB == 1 && images == 2 && colors[0] &&
                   colors[1],
               "both actual staged payloads match independent source colors") &&
         ok;
}
bool compilerProducer(Run &run, const CoinRenderCapabilities &caps) {
#ifndef COIN_PORTABLE_MODULE_BGFX
  (void)run;
  (void)caps;
  return check(false, "source compiler-failure focal requires BGFX");
#else
  bool ok = true;
  auto ticketDiagnostic = [](const char *label, const CoinRenderAction &action,
                             const CoinRenderReadbackTicket &t) {
    std::cout << "portable_module_source_ticket phase=" << label
              << " status=" << action.getLastStatus()
              << " error=" << action.getLastError().getString()
              << " token=" << t.token << " generation=" << t.generation
              << " serial=" << t.submissionSerial << " size=" << t.width << 'x'
              << t.height << " color_format=" << t.colorFormat
              << " depth_format=" << t.depthFormat
              << " color_pitch=" << t.colorRowPitch
              << " depth_pitch=" << t.depthRowPitch
              << " color_bytes=" << t.colorBytes
              << " depth_bytes=" << t.depthBytes << std::endl;
  };
  // The initial BGFX contract refuses MSAA async tickets. This is a real
  // negative, not a successful predecessor from which a ticket can be polled.
  const Frame beforeAsync = run.read();
  CoinRenderReadbackTicket unsupported{};
  const auto rootId = run.scene.root->getNodeId();
  run.action.applyAsync(run.scene.root, unsupported);
  ticketDiagnostic("MSAA4 unsupported", run.action, unsupported);
  const Frame afterAsync = run.read();
  ok &= check(
      run.action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
          run.action.getLastError().getLength() && !unsupported.token &&
          afterAsync.serial == beforeAsync.serial &&
          afterAsync.rgba == beforeAsync.rgba &&
          afterAsync.depth == beforeAsync.depth &&
          run.scene.root->getNodeId() == rootId,
      "MSAA async refusal preserves the current root and returns no ticket");
  CoinRenderReadbackTicket ticket{};
  {
    // A legitimate 1x ticket outlives its peer target/action by public
    // contract. Destroy the peer BEFORE the source fault: it is not a retained
    // runtime anchor, nor does this ordinary scene precompile either portable
    // AST.
    const bool portable = run.target->getOptions().textureSamplingPolicy ==
                          COIN_RENDER_SAMPLING_PORTABLE;
    Run peer(caps, portable, 1, false, COIN_RENDER_SCENE_TEXTURE_STAGED);
    if (!check(bool(peer.target),
               "ordinary 1x ticket peer uses the real selected GPU"))
      return false;
    peer.action.applyAsync(peer.scene.root, ticket);
    ticketDiagnostic("ordinary 1x peer", peer.action, ticket);
    ok &= check(
        peer.action.getLastStatus() == CoinRenderAction::SUCCESS &&
            ticket.token && ticket.width == 64 && ticket.height == 64 &&
            ticket.colorFormat == 0 && ticket.depthFormat == 1 &&
            ticket.colorBytes == 64 * 64 * 4 &&
            ticket.depthBytes == 64 * 64 * sizeof(float) &&
            ticket.submissionSerial == peer.target->getLastSubmissionSerial(),
        "ordinary 1x predecessor supplies an actual independent ticket");
  }
  Scene sourceA, sourceB;
  const SbVec3f square[] = {SbVec3f(-1.2f, -1.2f, 0), SbVec3f(1.2f, -1.2f, 0),
                            SbVec3f(1.2f, 1.2f, 0), SbVec3f(-1.2f, 1.2f, 0)};
  sourceA.positions->point.setValues(0, 4, square);
  sourceB.positions->point.setValues(0, 4, square);
  sourceB.material->diffuseColor.setValue(.22f, .48f, .31f);
  auto insert = [&](SoNode *node) {
    run.scene.root->insertChild(node,
                                run.scene.root->findChild(run.scene.positions));
  };
  auto *quality = new SoComplexity;
  quality->textureQuality = .1f;
  insert(quality);
  const Scene *sources[2] = {&sourceA, &sourceB};
  for (unsigned i = 0; i < 2; ++i) {
    auto *unit = new SoTextureUnit;
    unit->unit = i ? 3 : 0;
    insert(unit);
    auto *texture = new SoSceneTexture2;
    texture->scene = sources[i]->root;
    texture->size.setValue(64, 64);
    texture->type = SoSceneTexture2::RGBA8;
    texture->transparencyFunction = SoSceneTexture2::NONE;
    insert(texture);
  }
  auto *restoreUnit = new SoTextureUnit;
  restoreUnit->unit = 0;
  insert(restoreUnit);
  // Install the consumer after the RTT nodes: the ordinary sources do not
  // inherit its SAMPLE operations while being captured.
  run.scene.attach(true);
  Module consumer;
  Expr first = operation(Op::SAMPLE_2D, Type::FLOAT4, 0);
  first.textureUnit = 0;
  Expr second = first;
  second.textureUnit = 3;
  consumer.expressions = {constant({{.5f, .5f, 0, 1}}),
                          first,
                          second,
                          constant({{.25f, .25f, .25f, .25f}}),
                          operation(Op::MULTIPLY, Type::FLOAT4, 1, 3),
                          constant({{.5f, .5f, .5f, .5f}}),
                          operation(Op::MULTIPLY, Type::FLOAT4, 2, 5),
                          operation(Op::ADD, Type::FLOAT4, 4, 6),
                          constant({{.03f, .02f, .04f, .25f}}),
                          operation(Op::ADD, Type::FLOAT4, 7, 8)};
  consumer.output = 9;
  ok &= run.bind(consumer);
  Frame stable;
  ok &= run.draw(compilerConsumerColor({{.22f, .48f, .31f, 1}}), stable,
                 "two ordinary STAGED sources establish the cached consumer");
  const auto consumerIds = run.lastCompileModules;
  ok &= check(!consumerIds.empty(),
              "first consumer really compiled before the fault");
  const Color a = {{.28f, .43f, .61f, 1}}, b = {{.62f, .21f, .34f, 1}};
  auto *tone = sourceB.parameter4("fresh source tone / staged", a);
  sourceB.attach(true);
  Module producer;
  declare(producer, "fresh source tone / staged", Type::FLOAT4);
  producer.expressions = {uniform("fresh source tone / staged", Type::FLOAT4),
                          constant({{.77f, .63f, .91f, 1}}),
                          operation(Op::MULTIPLY, Type::FLOAT4, 0, 1),
                          constant({{.013741f, .027319f, .009153f, 0}}),
                          operation(Op::ADD, Type::FLOAT4, 2, 3)};
  producer.output = 4;
  ok &= check(run.action.setPortableShaderModule(sourceB.program, producer),
              "only the second producer introduces a fresh authorial AST");
  const auto entries = run.audit->entries;
  const auto producers = run.audit->producers;
  const auto retired = run.audit->retiredRtt;
  const auto parentId = run.scene.root->getNodeId();
  const auto sourceAId = sourceA.root->getNodeId();
  const auto sourceBId = sourceB.root->getNodeId();
  const auto sourceProgramId = sourceB.program->getNodeId();
  std::string failedTrace;
  {
    startup::Environment missing("COIN_RENDER_BGFX_SHADERC",
                                 "/dev/null/coin-portable-no-shaderc");
    startup::Environment tracing("COIN_RENDER_TRACE_PHASES", "1");
    startup::Trace trace;
    if (!check(trace.start(), "failed source apply has passive trace capture"))
      return false;
    run.action.apply(run.scene.root);
    failedTrace = trace.stop();
    const auto after = run.read();
    std::string diagnostic = run.action.getLastError().getString();
    for (auto &c : diagnostic)
      c = char(std::tolower(static_cast<unsigned char>(c)));
    const bool completedChild =
        failedTrace.find("COIN_RENDER_PHASE bgfx lower_ms=") !=
        std::string::npos;
    std::cout << "portable_module_source_compiler_failure root_samples="
              << run.samples << " status=" << run.action.getLastStatus()
              << " serial=" << stable.serial << "->" << after.serial
              << " completed_child_phase=" << completedChild
              << " parent_entries=" << entries << "->" << run.audit->entries
              << " direct_producers=" << producers << "->"
              << run.audit->producers << " error=" << diagnostic << std::endl;
    ok &= check(
        run.action.getLastStatus() != CoinRenderAction::SUCCESS &&
            (diagnostic.find("shader") != std::string::npos ||
             diagnostic.find("compil") != std::string::npos) &&
            after.serial == stable.serial && after.rgba == stable.rgba &&
            after.depth == stable.depth && !completedChild &&
            run.audit->entries == entries &&
            run.audit->producers == producers &&
            run.audit->retiredRtt == retired &&
            run.scene.root->getNodeId() == parentId &&
            sourceA.root->getNodeId() == sourceAId &&
            sourceB.root->getNodeId() == sourceBId &&
            sourceB.program->getNodeId() == sourceProgramId,
        "fresh second-source compile failure preserves exact parent/sources "
        "without an earlier successful staged producer or parent execution");
  }
  std::vector<uint8_t> oldColor;
  std::vector<float> oldDepth;
  SbString error;
  auto status = CoinRenderTarget::READBACK_NOT_READY;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (status == CoinRenderTarget::READBACK_NOT_READY &&
         std::chrono::steady_clock::now() < deadline) {
    status = CoinRenderTarget::pollReadback(ticket, oldColor, oldDepth, &error);
    if (status == CoinRenderTarget::READBACK_NOT_READY)
      run.target->pollDevice();
  }
  Frame old;
  old.rgba = std::move(oldColor);
  old.depth = std::move(oldDepth);
  ok &= check(
      status == CoinRenderTarget::READBACK_READY,
      "failed source compilation preserves the ordinary predecessor ticket");
  if (status == CoinRenderTarget::READBACK_READY)
    ok &= image(old, {{.6f, .3f, .8f, 1}});
  else if (ticket.token)
    CoinRenderTarget::cancelReadback(ticket);
  Frame recovered;
  ok &= run.draw(
      compilerConsumerColor(compilerProducerColor(a)), recovered,
      "restored real compiler recovers fresh STAGED source on same root");
  ok &= compilerProducerDelivery(run, sourceA, sourceB, producer, a,
                                 compilerProducerColor(a));
  ok &= check(!run.lastCompileModules.empty(),
              "previously failing source really compiles during recovery");
  for (const auto &id : run.lastCompileModules)
    ok &= check(std::find(consumerIds.begin(), consumerIds.end(), id) ==
                    consumerIds.end(),
                "the cached consumer is not the source of a new compilation");
  tone->value.setValue(b[0], b[1], b[2], b[3]);
  ok &= run.draw(
      compilerConsumerColor(compilerProducerColor(b)), recovered,
      "source parameter edit updates both staged payload and current parent");
  ok &= compilerProducerDelivery(run, sourceA, sourceB, producer, b,
                                 compilerProducerColor(b));
  ok &=
      check(run.lastCompiles.empty(),
            "values-only source edit reuses its actual output-domain variant");
  tone->value.setValue(a[0], a[1], a[2], a[3]);
  ok &= run.draw(
      compilerConsumerColor(compilerProducerColor(a)), recovered,
      "source parameter A/B/A restores current staged consumer output");
  ok &= compilerProducerDelivery(run, sourceA, sourceB, producer, a,
                                 compilerProducerColor(a));
  ok &= check(run.lastCompiles.empty(),
              "source A/B/A does not recompile either AST");
  return ok;
#endif
}
#endif
