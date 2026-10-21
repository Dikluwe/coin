// Qualified source arithmetic stays independent of the selected BGFX program.
#define COIN_MULTILIGHT_RTT_NO_MAIN
#include "CoinRenderBumpMultilightRttTest.cpp"
#undef COIN_MULTILIGHT_RTT_NO_MAIN
#include "CoinRenderTexture2dStartupTrace.h"

namespace texture2d_rtt {
using bump_profile::check;
bool equivalent(const bump_profile::Frame &a, const bump_profile::Frame &b) {
  if (a.rgba.size() != b.rgba.size() || a.depth.size() != b.depth.size())
    return false;
  unsigned bad = 0;
  for (size_t i = 0; i < a.rgba.size(); ++i)
    bad += std::abs(int(a.rgba[i]) - int(b.rgba[i])) > (i % 4 == 3 ? 0 : 1);
  for (size_t i = 0; i < a.depth.size(); ++i)
    bad += std::abs(a.depth[i] - b.depth[i]) > 1e-6f;
  std::cout << "texture2d_rtt_full_compare bad=" << bad << std::endl;
  return check(!bad, "complete RTT optimized/general RGB1 alpha exact "
                     "depth1e-6 including edges");
}
// Producers precede their consumer in the qualified execution graph. They
// legitimately use general/bump shaders; inspect the final ordinary submit,
// while delivery() independently proves the resolved unit-zero consumer.
std::string consumerTrace(const std::string &trace, unsigned samples) {
  std::string last, line;
  std::istringstream input(trace);
  const std::string event =
      samples == 1 ? "COIN_RENDER_PHASE bgfx_base_program "
                   : "COIN_RENDER_PHASE bgfx_surface_depth_program ";
  while (std::getline(input, line))
    if (line.find(event) != std::string::npos)
      last = line;
  return last;
}
bool draw(multi_rtt::Run &run, const startup::Config &config, bool forced,
          const char *label) {
  if (!run.target)
    return false;
  startup::Environment selector("COIN_BGFX_DISABLE_TEXTURE2D_PROGRAM",
                                forced ? "1" : nullptr);
  startup::Environment tracing("COIN_RENDER_TRACE_PHASES", "1");
  const auto serial = run.target->getLastSubmissionSerial();
  const auto submits = run.audit->rootSubmits;
  const auto producers = run.audit->producers;
  const auto authored = multi_rtt::inputs(run.graph.producer);
  const bool requested = !run.window || run.target->requestWindowReadbackRGBA();
  startup::Trace trace;
  if (!check(trace.start(), "passive RTT trace capture available"))
    return false;
  std::cout << "texture2d_rtt_apply_begin phase=" << label
            << " serial=" << serial << std::endl;
  const auto start = std::chrono::steady_clock::now();
  run.action.apply(run.graph.root);
  const auto end = std::chrono::steady_clock::now();
  const std::string log = trace.stop();
  bump_profile::Frame current;
  run.target->readbackRGBA(current.rgba);
  if (!run.window)
    run.target->readbackDepth(current.depth);
  current.serial = run.target->getLastSubmissionSerial();
  std::cout << "texture2d_rtt phase=" << label << " apply_ms="
            << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                     start)
                   .count()
            << " status=" << run.action.getLastStatus() << " serial=" << serial
            << "->" << current.serial
            << " error=" << run.action.getLastError().getString() << std::endl;
  bool ok = check(
      requested && run.action.getLastStatus() == CoinRenderAction::SUCCESS &&
          current.serial > serial && run.audit->rootSubmits == submits + 1 &&
          run.target->getOptions().sampleCount == config.samples &&
          run.target->getOptions().sceneTexture == run.route &&
          multi_rtt::inputs(run.graph.producer) == authored,
      "one current RTT publication with exact source and options");
  ok &= check(run.route == COIN_RENDER_SCENE_TEXTURE_DIRECT
                  ? run.audit->producers == producers + 1
                  : run.audit->producers == producers,
              "actual DIRECT producer submit versus STAGED child execution");
  if (ok)
    ok &= run.delivery() &&
          multi_rtt::image(run.graph, current, run.extent, config.samples,
                           run.window, !run.window);
  ok &= startup::route(consumerTrace(log, config.samples), config, forced);
#ifdef COIN_MULTILIGHT_RTT_X11
  if (ok && run.window) {
    const auto startPresent = std::chrono::steady_clock::now();
    const auto deadline = startPresent + std::chrono::seconds(5);
    unsigned attempts = 0;
    bool present = false;
    do {
      ++attempts;
      bump_profile::Frame shown;
      if (!multi_rtt::physical(run.display, run.nativeWindow, run.extent,
                               shown))
        continue;
      bool match = shown.rgba.size() == current.rgba.size();
      for (size_t p = 0; match && p < shown.rgba.size(); ++p)
        if (p % 4 < 3)
          match = std::abs(int(shown.rgba[p]) - int(current.rgba[p])) <= 3;
      if (match && multi_rtt::image(run.graph, shown, run.extent,
                                    config.samples, true, false, false)) {
        present = std::chrono::steady_clock::now() <= deadline;
        run.lastPhysical = std::move(shown);
        break;
      }
    } while (std::chrono::steady_clock::now() < deadline);
    std::cout << "texture2d_rtt present_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - startPresent)
                     .count()
              << " attempts=" << attempts << std::endl;
    ok &= check(present, "same-apply readback and full current physical RTT "
                         "image within five seconds");
  }
#endif
  run.last = std::move(current);
  return ok;
}
bool mixedDraw(multilamp::Run &run, const startup::Config &config, bool forced,
               bump_profile::Frame &frame, const char *label) {
  startup::Environment selector("COIN_BGFX_DISABLE_TEXTURE2D_PROGRAM",
                                forced ? "1" : nullptr);
  startup::Environment tracing("COIN_RENDER_TRACE_PHASES", "1");
  const auto serial = run.harness.target->getLastSubmissionSerial();
  const auto submits = run.harness.audit->rootSubmits;
  const auto source = multilamp::inputs(run.scene);
  startup::Trace trace;
  if (!check(trace.start(), "passive mixed-family trace"))
    return false;
  const auto start = std::chrono::steady_clock::now();
  run.harness.action.apply(run.scene.root);
  const auto end = std::chrono::steady_clock::now();
  const auto log = trace.stop();
  run.harness.target->readbackRGBA(frame.rgba);
  run.harness.target->readbackDepth(frame.depth);
  frame.serial = run.harness.target->getLastSubmissionSerial();
  std::cout << "texture2d_mixed phase=" << label << " apply_ms="
            << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                     start)
                   .count()
            << " status=" << run.harness.action.getLastStatus()
            << " serial=" << serial << "->" << frame.serial
            << " error=" << run.harness.action.getLastError().getString()
            << std::endl;
  bool ok = check(
      run.harness.action.getLastStatus() == CoinRenderAction::SUCCESS &&
          frame.serial > serial &&
          run.harness.audit->rootSubmits == submits + 1 &&
          run.harness.target->getOptions().sampleCount == config.samples &&
          multilamp::inputs(run.scene) == source,
      "one mixed ordinary/bump publication with intact authored inputs");
  if (ok)
    ok &= multilamp::delivery(run.scene, run.harness.audit->plan) &&
          multilamp::oracle(run.scene, frame);
  // These ordinary occurrences have only unit0 and no shadow maps. Bump
  // occurrences select their own executor; ordinary2D must remain eligible.
  unsigned occurrences = 0;
  for (const auto &draw : run.harness.audit->plan.draws) {
    if (draw.sourceNodeId != run.scene.mesh->getNodeId() ||
        draw.shadowLightSlot)
      continue;
    ++occurrences;
    const auto &state =
        run.harness.audit->plan.renderStates[draw.renderStateSlot];
    ok &= check(state.shadowGroupSlot == 0 &&
                    coin_render_texture_unit(state, 0).enabled,
                "mixed occurrences use unshadowed active unit0");
    for (unsigned unit = 1; unit < 8; ++unit)
      ok &= check(
          !coin_render_texture_unit(state, unit).enabled,
          "no extra unit silently forces the mixed ordinary draws general");
  }
  ok &= check(occurrences == 3,
              "three real shared-shape occurrences survive capture");
  ok &= startup::route(log, config, forced);
  return ok;
}
bool mixed(const startup::Config &config, const CoinRenderCapabilities &caps) {
  multilamp::Settings settings;
  settings.lights = 8;
  settings.maps = 0;
  settings.samples = config.samples;
  settings.general = false;
  multilamp::Run run(settings, caps);
  if (!check(bool(run.harness.target), "actual mixed-family target"))
    return false;
  run.scene.setSpec({{0, 0, 0}});
  run.scene.setNormal({{210, 145, 205}});
  bool ok = true;
  for (int layout : {1, 0}) {
    run.scene.layout(layout);
    bump_profile::Frame optimized, general, back;
    ok &= mixedDraw(run, config, false, optimized,
                    "asymmetric shared shape ordinary/bump contexts");
    ok &= mixedDraw(run, config, true, general,
                    "same mixed contexts forced general");
    ok &= equivalent(optimized, general);
    ok &= mixedDraw(run, config, false, back, "same mixed contexts restored");
    ok &= equivalent(optimized, back);
  }
  return ok;
}
} // namespace texture2d_rtt
int main(int argc, char **argv) {
  startup::Config config;
  bool window = false, mixedMode = false;
  CoinRenderSceneTextureMode route = COIN_RENDER_SCENE_TEXTURE_STAGED;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--mixed")
      mixedMode = true;
    else if (arg == "--window")
      window = true;
    else if (arg == "--gpu") {
    } else if (arg == "--direct")
      route = COIN_RENDER_SCENE_TEXTURE_DIRECT;
    else if (arg == "--staged")
      route = COIN_RENDER_SCENE_TEXTURE_STAGED;
    else if (arg == "--baseline")
      config.baseline = true;
    else if (arg == "--portable-sampling")
      config.portable = true;
    else if (arg == "--samples" && i + 1 < argc) {
      const std::string value = argv[++i];
      if (value != "1" && value != "2" && value != "4")
        return 2;
      config.samples = unsigned(std::atoi(value.c_str()));
    } else
      return 2;
  }
#ifdef COIN_MULTILIGHT_RTT_X11
  if (window && !XInitThreads())
    return 1;
#else
  if (window)
    return 77;
#endif
  SoDB::init();
  CoinRenderAction::initClass();
  configureSamplingTest(argc, argv);
  CoinRenderCapabilities caps{};
  const int queried = coin_render_query_capabilities(
      window ? COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW
             : COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
      &caps, sizeof(caps));
  std::cout << "texture2d_rtt_environment query=" << queried
            << " gpu=" << caps.gpu_available << " renderer=" << caps.renderer
            << " adapter=" << caps.adapter_name << std::endl;
  if (queried)
    return 1;
  if (!caps.gpu_available)
    return 77;
  if (mixedMode) {
    if (window)
      return 2;
    return texture2d_rtt::mixed(config, caps) ? 0 : 1;
  }
  multilamp::Settings settings;
  settings.lights = 8;
  settings.maps = 4;
  settings.samples = config.samples;
  multi_rtt::Run run(settings, caps, route, window);
  bool ok = texture2d_rtt::draw(
      run, config, false,
      "first cold qualified bump source and specialized2D consumer");
  const bump_profile::Frame optimized = run.last;
  ok &= texture2d_rtt::draw(run, config, true,
                            "same RTT inputs forced-general consumer");
  ok &= texture2d_rtt::equivalent(optimized, run.last);
  ok &= texture2d_rtt::draw(run, config, false,
                            "same-target optimized RTT restoration");
  ok &= texture2d_rtt::equivalent(optimized, run.last);
  return ok ? 0 : 1;
}
