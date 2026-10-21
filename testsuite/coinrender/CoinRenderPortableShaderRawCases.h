#ifndef COIN_PORTABLE_SHADER_RAW_CASES_H
#define COIN_PORTABLE_SHADER_RAW_CASES_H
#ifdef COIN_PORTABLE_MODULE_RUST
// The initial successful public apply installs the runtime's real callback.
// Repeating its exported installer must retain that very same DSO address.
bool raw63(Run &run) {
  run.scene.attach(true);
  const Color a = {{.2f, .55f, .7f, 1}}, b = {{.7f, .2f, .3f, 1}};
  run.scene.parameter4("rgba", a);
  bool ok = run.bind(presentationModule());
  Frame original;
  ok &= run.draw(a, original, "public predecessor for private63 ingress");
  if (!ok)
    return false;
  ok &= check(coin_wgpu_install_cpp_portable_core() == COIN_WGPU_OK &&
                  coin_wgpu_install_cpp_portable_core() == COIN_WGPU_OK,
              "repeated installation uses the runtime's real static callback");
  CoinWgpuFfiFrame ffi;
  std::string diagnostic;
  const bool packed = ffi.prepare(run.audit->plan, 64, 64, diagnostic, false);
  ok &= check(packed,
              "actual captured module packs for raw ingress: " + diagnostic);
  if (!packed)
    return false;
  auto view = ffi.getView();
  if (!check(view.abi_version == 63 && view.portable_module_count == 1 &&
                 view.portable_modules &&
                 view.portable_modules[0].node_count == 1 &&
                 view.portable_modules[0].parameter_count == 4 &&
                 view.portable_modules[0].nodes[0].op == uint32_t(Op::UNIFORM),
             "raw fixture is the exact four-component uniform module"))
    return false;
  std::vector<CoinWgpuPortableNode> nodes(
      view.portable_modules[0].nodes,
      view.portable_modules[0].nodes + view.portable_modules[0].node_count);
  std::vector<float> parameters(view.portable_modules[0].parameters,
                                view.portable_modules[0].parameters +
                                    view.portable_modules[0].parameter_count);
  CoinWgpuPortableModule module = view.portable_modules[0];
  module.nodes = nodes.data();
  module.parameters = parameters.data();
  view.portable_modules = &module;
  std::vector<CoinWgpuRenderState> states(view.states,
                                          view.states + view.state_count);
  view.states = states.data();
  std::vector<uint8_t> rgba(64 * 64 * 4, 0x65);
  std::vector<float> depth(64 * 64, .8765f);
  CoinWgpuTarget target{};
  target.width = target.height = 64;
  target.sample_count = run.samples;
  target.color_buffer = rgba.data();
  target.color_buffer_len = rgba.size();
  target.depth_buffer = depth.data();
  target.depth_buffer_len = depth.size();
  auto submit = [&](Color expected, const char *label, bool reuse) {
    const auto serial = target.submission_serial;
    startup::Environment tracing("COIN_RENDER_TRACE_PHASES", "1");
    startup::Trace trace;
    if (!check(trace.start(), "passive private compile observation"))
      return false;
    char error[2048] = {};
    const auto status = coin_wgpu_submit(&target, &view, error, sizeof(error));
    const std::string log = trace.stop();
    Frame frame;
    frame.rgba = rgba;
    frame.depth = depth;
    frame.serial = target.submission_serial;
    std::cout << "portable_module_raw phase=" << label << " status=" << status
              << " serial=" << serial << "->" << target.submission_serial
              << " error=" << error << std::endl;
    return check(status == COIN_WGPU_OK && target.submission_serial > serial,
                 "valid raw module executes an actual GPU publication") &&
           image(frame, expected) &&
           check(!reuse ||
                     log.find("COIN_RENDER_PHASE portable_shader_compile ") ==
                         std::string::npos,
                 "same module/profile and dynamic values reuse compiled "
                 "runtime module");
  };
  ok &= submit(a, "valid private packet after public predecessor", false);
  auto reject = [&](const char *label) {
    const auto oldColor = rgba;
    const auto oldDepth = depth;
    const auto serial = target.submission_serial;
    CoinWgpuCacheStats before{}, after{};
    CoinWgpuPerformanceStats performanceBefore{}, performanceAfter{};
    coin_wgpu_get_cache_stats(&before);
    coin_wgpu_get_performance_stats(&performanceBefore);
    char error[2048] = {};
    const auto status = coin_wgpu_submit(&target, &view, error, sizeof(error));
    coin_wgpu_get_cache_stats(&after);
    coin_wgpu_get_performance_stats(&performanceAfter);
    std::cout << "portable_module_raw_refuse phase=" << label
              << " status=" << status << " serial=" << serial << "->"
              << target.submission_serial << " error=" << error << std::endl;
    return check(status != COIN_WGPU_OK && error[0] &&
                     target.submission_serial == serial && rgba == oldColor &&
                     depth == oldDepth &&
                     after.submission_serial == before.submission_serial &&
                     performanceAfter.pipeline_compilations ==
                         performanceBefore.pipeline_compilations &&
                     performanceAfter.texture_uploads ==
                         performanceBefore.texture_uploads,
                 std::string(label) +
                     " rejects before GPU submissions/compilation/uploads and "
                     "preserves output");
  };
  const auto validModule = module;
  const auto validNode = nodes[0];
  view.abi_version = 62;
  ok &= reject("obsolete protocol62");
  view.abi_version = 63;
  view.struct_size = 496;
  ok &= reject("truncated pre-module frame layout");
  view.struct_size = sizeof(CoinWgpuFrameView);
  view.portable_modules = nullptr;
  ok &= reject("nonzero count with null module table");
  view.portable_modules = &module;
  states[0].portable_shader_slot = 2;
  ok &= reject("module association outside actual table");
  states[0].portable_shader_slot = 1;
  module.nodes = nullptr;
  ok &= reject("nonzero node count with null statements");
  module = validModule;
  for (uint64_t count : {uint64_t(0), uint64_t(257), UINT64_MAX}) {
    module.node_count = count;
    ok &= reject("statement count outside bounded AST domain");
  }
  module = validModule;
  module.output = 1;
  ok &= reject("output outside node array");
  module = validModule;
  module.alpha_may_vary = 2;
  ok &= reject("unknown alpha declaration domain");
  module = validModule;
  module.signature = nullptr;
  ok &= reject("missing full authorial signature");
  module = validModule;
  module.signature_bytes = 0;
  ok &= reject("empty signature cannot license IR");
  module = validModule;
  module.parameters = nullptr;
  ok &= reject("missing actual uniform components");
  module = validModule;
  module.parameter_count = 3;
  ok &= reject("short actual uniform payload");
  module = validModule;
  nodes[0].op = UINT32_MAX;
  ok &= reject("unknown IR operation");
  nodes[0] = validNode;
  nodes[0].type = 0;
  ok &= reject("unknown IR type");
  nodes[0] = validNode;
  nodes[0].resource = 4;
  ok &= reject("uniform offset exceeds actual payload");
  nodes[0] = validNode;
  for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                        std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity()}) {
    parameters[3] = invalid;
    ok &= reject("nonfinite runtime uniform alpha");
  }
  parameters[3] = 1;
  // Fields inactive for UNIFORM are not padding-backed semantics. This valid
  // packet must still execute the same module rather than inventing inputs.
  nodes[0].inputs[0] = 777;
  nodes[0].value[0] = std::numeric_limits<float>::quiet_NaN();
  ok &= submit(a, "inactive node fields do not alter uniform semantics", true);
  nodes[0] = validNode;
  for (unsigned c = 0; c < 4; ++c)
    parameters[c] = b[c];
  ok &= submit(b, "same revision/signature actual parameter edit", true);
  parameters[1] = std::numeric_limits<float>::quiet_NaN();
  ok &= reject("invalid edit cannot reuse previous accepted raw parameters");
  for (unsigned c = 0; c < 4; ++c)
    parameters[c] = a[c];
  ok &= submit(a, "raw parameter A/B/A and refusal recovery", true);
  const auto rootAfter = run.read();
  ok &= check(
      rootAfter.serial == original.serial && rootAfter.rgba == original.rgba &&
          rootAfter.depth == original.depth,
      "private target cannot alter independent public target publication");
  return ok;
}
#else
bool raw63(Run &) { return false; }
#endif
#endif
