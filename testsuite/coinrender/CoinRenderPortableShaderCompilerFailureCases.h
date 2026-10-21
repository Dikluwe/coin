#ifndef COIN_PORTABLE_SHADER_COMPILER_FAILURE_CASES_H
#define COIN_PORTABLE_SHADER_COMPILER_FAILURE_CASES_H
// One intentional ordinary async publication owns the old ticket. A subsequent
// ordinary synchronous publication supplies rollback bytes. Neither compiles
// this fresh AST, which first appears in the failing RTT preflight.
bool compilerFailure(Run &run) {
#ifndef COIN_PORTABLE_MODULE_BGFX
  (void)run;
  return check(false,
               "compiler-failure focal requires the actual BGFX tool path");
#else
  bool ok = true;
  CoinRenderReadbackTicket ticket{};
  run.action.applyAsync(run.scene.root, ticket);
  ok &=
      check(run.action.getLastStatus() == CoinRenderAction::SUCCESS &&
                ticket.token && ticket.width == 64 && ticket.height == 64 &&
                ticket.depthBytes == 64 * 64 * sizeof(float),
            "ordinary async predecessor returns an actual color/depth ticket");
  Frame stable;
  ok &= run.draw({{.6f, .3f, .8f, 1}}, stable,
                 "ordinary synchronous rollback predecessor");
  Scene source;
  const SbVec3f square[] = {SbVec3f(-1.2f, -1.2f, 0), SbVec3f(1.2f, -1.2f, 0),
                            SbVec3f(1.2f, 1.2f, 0), SbVec3f(-1.2f, 1.2f, 0)};
  source.positions->point.setValues(0, 4, square);
  auto *texture = new SoSceneTexture2;
  texture->scene = source.root;
  texture->size.setValue(64, 64);
  texture->type = SoSceneTexture2::RGBA8;
  texture->transparencyFunction = SoSceneTexture2::NONE;
  run.scene.root->insertChild(texture,
                              run.scene.root->findChild(run.scene.positions));
  auto *quality = new SoComplexity;
  quality->textureQuality = .1f;
  run.scene.root->insertChild(quality, run.scene.root->findChild(texture));
  Module module;
  module.expressions = {constant({{.5f, .5f, 0, 1}}),
                        operation(Op::SAMPLE_2D, Type::FLOAT4, 0),
                        constant({{.37f, .61f, .43f, 1}}),
                        operation(Op::MULTIPLY, Type::FLOAT4, 1, 2),
                        constant({{.01721937f, .0269123f, .0113864f, 0}}),
                        operation(Op::ADD, Type::FLOAT4, 3, 4)};
  module.output = 5;
  run.scene.attach(true);
  ok &= run.bind(module);
  const auto producers = run.audit->producers;
  const auto entries = run.audit->entries;
  const auto retired = run.audit->retiredRtt;
  const auto sourceId = source.root->getNodeId();
  const auto programId = run.scene.program->getNodeId();
  {
    // /dev/null is a file: no compiler can exist below this path.
    startup::Environment missing("COIN_RENDER_BGFX_SHADERC",
                                 "/dev/null/coin-portable-no-shaderc");
    run.action.apply(run.scene.root);
    const auto after = run.read();
    std::string diagnostic = run.action.getLastError().getString();
    for (auto &c : diagnostic)
      c = char(std::tolower(static_cast<unsigned char>(c)));
    std::cout << "portable_module_compiler_failure status="
              << run.action.getLastStatus() << " serial=" << stable.serial
              << "->" << after.serial << " entries=" << entries << "->"
              << run.audit->entries << " producers=" << producers << "->"
              << run.audit->producers << " error=" << diagnostic << std::endl;
    ok &= check(run.action.getLastStatus() != CoinRenderAction::SUCCESS &&
                    !diagnostic.empty() &&
                    (diagnostic.find("shader") != std::string::npos ||
                     diagnostic.find("compil") != std::string::npos) &&
                    after.serial == stable.serial &&
                    after.rgba == stable.rgba && after.depth == stable.depth &&
                    run.audit->producers == producers &&
                    run.audit->retiredRtt == retired &&
                    source.root->getNodeId() == sourceId &&
                    run.scene.program->getNodeId() == programId,
                "real missing compiler rejects before DIRECT producer/token "
                "creation and preserves publication without mutating sources");
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
  ok &= check(status == CoinRenderTarget::READBACK_READY,
              "compiler failure preserves the independently owned old ticket");
  if (status == CoinRenderTarget::READBACK_READY)
    ok &= image(old, {{.6f, .3f, .8f, 1}});
  else if (ticket.token)
    CoinRenderTarget::cancelReadback(ticket);
  const Color expected = {{.6f * .37f + .01721937f, .3f * .61f + .0269123f,
                           .8f * .43f + .0113864f, 1}};
  Frame recovered;
  ok &=
      run.draw(expected, recovered,
               "restored real compiler recovers the same target and fresh AST");
  ok &= check(!run.lastCompiles.empty() &&
                  run.audit->producers == producers + 1 &&
                  run.audit->graph.mode == COIN_RENDER_SCENE_TEXTURE_DIRECT &&
                  run.audit->graph.producers.size() == 1,
              "recovery really compiles the previously failed module and "
              "executes the actual DIRECT producer once");
  return ok;
#endif
}
#endif
