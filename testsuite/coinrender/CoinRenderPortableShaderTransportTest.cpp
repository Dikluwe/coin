#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderPortableShaderModuleCases.h"
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderShaderModuleCore.h"
#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include <Inventor/actions/SoGLRenderAction.h>
#include <cstddef>
#include <cstring>
#include <functional>

namespace {
using namespace portable_module_test;
static_assert(sizeof(CoinWgpuPortableNode) == 40, "ABI67 node layout");
static_assert(sizeof(CoinWgpuPortableModule) == 56, "ABI67 module layout");
static_assert(sizeof(CoinWgpuPortableRange) == 64,
              "ABI67 numeric range layout");
static_assert(sizeof(CoinWgpuRenderState) == (COIN_WGPU_ABI_VERSION == 67 ? 2436 : 2564), "current ABI state layout");
static_assert(COIN_WGPU_ABI_VERSION == 70 && sizeof(CoinWgpuFrameView) == 600,
              "Protocol 70 frame layout");
static_assert(offsetof(CoinWgpuRenderState, portable_shader_slot) == (COIN_WGPU_ABI_VERSION == 67 ? 2432 : 2560),
              "Current bridge state association offset");
static_assert(offsetof(CoinWgpuFrameView, portable_modules) == 496,
              "ABI67 appends module array");
static_assert(offsetof(CoinWgpuFrameView, portable_module_count) == 504,
              "ABI67 appends module count");

uint32_t bits(float x) {
  uint32_t b;
  std::memcpy(&b, &x, sizeof(b));
  return b;
}
Module authored() {
  Module m;
  declare(m, "logical gain / exact-name", Type::FLOAT1);
  // Deliberate forward references. IR ordering is free, but operands must
  // still identify this constant and this uniform exactly after sorting.
  m.expressions = {operation(Op::MULTIPLY, Type::FLOAT4, 2, 1),
                   uniform("logical gain / exact-name", Type::FLOAT1),
                   constant({{.23f, .61f, .37f, 1}})};
  m.output = 0;
  m.alphaMayVary = TRUE;
  return m;
}
bool instance(Module m, float parameter, CoinRenderShaderInstance &out) {
  out.module = m;
  std::string error;
  const bool ready = coin_render_shader_ir(m, out.ir, error);
  out.parameters = {parameter};
  return check(ready, "public module lowers to canonical Core IR: " + error);
}
bool canonical(const CoinRenderShaderInstance &s) {
  if (!check(s.ir.nodes.size() == 3 && s.ir.output < s.ir.nodes.size() &&
                 s.ir.uniformFloats == 1 && s.ir.uniformOffsets.size() == 1 &&
                 s.ir.uniformOffsets[0] == 0 && !s.ir.textureMask &&
                 !s.ir.coordinateMask && s.ir.alphaMayVary,
             "canonical IR preserves signature, output, masks and alpha class"))
    return false;
  const auto &output = s.ir.nodes[s.ir.output];
  if (!check(output.op == Op::MULTIPLY && output.type == Type::FLOAT4 &&
                 output.inputs[0] < s.ir.output &&
                 output.inputs[1] < s.ir.output,
             "forward authorial edges become earlier ordered operands"))
    return false;
  const auto &literal = s.ir.nodes[output.inputs[0]];
  const auto &gain = s.ir.nodes[output.inputs[1]];
  bool ok = check(literal.op == Op::CONSTANT && literal.type == Type::FLOAT4 &&
                      gain.op == Op::UNIFORM && gain.type == Type::FLOAT1 &&
                      gain.resource == 0,
                  "ordered operands retain independent authorial meanings");
  for (unsigned c = 0; c < 4; ++c)
    ok &=
        check(bits(literal.value[c]) == bits(s.module.expressions[2].value[c]),
              "f32 constant survives canonical IR bit exactly");
  return ok;
}
CoinRenderFramePlan packet(const CoinRenderShaderInstance &s) {
  CoinRenderFramePlan f;
  f.revision = 731; // Intentionally unchanged in cache-adversarial edits.
  f.cameras.resize(1);
  f.cameras[0].nearDistance = 1;
  f.cameras[0].farDistance = 10;
  f.materials.resize(1);
  f.lightingStates.resize(1);
  f.viewports.resize(1);
  f.viewports[0].width = f.viewports[0].height = 32;
  f.renderStates.resize(1);
  f.renderStates[0].transparencyType = SoGLRenderAction::NONE;
  f.renderStates[0].portableShaderSlot = 1;
  f.vertices.resize(3);
  f.vertices[0].position[0] = -.5f;
  f.vertices[0].position[1] = -.5f;
  f.vertices[1].position[0] = .5f;
  f.vertices[1].position[1] = -.5f;
  f.vertices[2].position[1] = .5f;
  for (auto &v : f.vertices) {
    v.position[2] = -2;
    v.normal[2] = 1;
  }
  f.indices = {0, 1, 2};
  f.draws.resize(1);
  f.draws[0].geometry.vertexCount = f.draws[0].geometry.indexCount = 3;
  f.portableShaders.push_back(s);
  return f;
}
bool transport(const CoinWgpuFrameView &v,
               const std::vector<CoinRenderShaderInstance> &expected) {
  if (!check(v.abi_version == COIN_WGPU_ABI_VERSION && v.struct_size == sizeof(CoinWgpuFrameView) &&
                 v.portable_module_count == expected.size() &&
                 v.portable_modules && v.states &&
                 v.state_count >= expected.size(),
             "explicit ABI67 and real per-occurrence module/state arrays"))
    return false;
  bool ok = true;
  for (size_t i = 0; i < expected.size(); ++i) {
    const auto &m = v.portable_modules[i];
    const auto &s = expected[i];
    ok &= check(
        m.node_count == s.ir.nodes.size() && m.nodes &&
            m.parameter_count == s.parameters.size() && m.parameters &&
            m.output == s.ir.output &&
            m.alpha_may_vary == unsigned(s.ir.alphaMayVary) && m.signature &&
            m.signature_bytes > 0 && v.states[i].portable_shader_slot == i + 1,
        "transport owns canonical nodes, full signature, parameters and slot");
    if (!ok)
      continue;
    for (size_t n = 0; n < s.ir.nodes.size(); ++n) {
      const auto &a = m.nodes[n];
      const auto &b = s.ir.nodes[n];
      ok &= check(a.op == uint32_t(b.op) && a.type == uint32_t(b.type),
                  "node operation and type retained");
      if (b.op == Op::MULTIPLY || b.op == Op::ADD)
        ok &= check(a.inputs[0] == b.inputs[0] && a.inputs[1] == b.inputs[1],
                    "ordered expression edges retained");
      if (b.op == Op::UNIFORM)
        ok &=
            check(a.resource == b.resource, "uniform logical offset retained");
      if (b.op == Op::CONSTANT)
        for (unsigned c = 0; c < uint32_t(b.type); ++c)
          ok &= check(bits(a.value[c]) == bits(b.value[c]),
                      "constant f32 delivered without decimal rounding");
    }
    for (size_t n = 0; n < s.parameters.size(); ++n)
      ok &= check(bits(m.parameters[n]) == bits(s.parameters[n]),
                  "every actual uniform component delivered bit exactly");
  }
  return ok;
}
struct Retained {
  CoinWgpuFrameView view;
  std::vector<CoinWgpuPortableModule> modules;
  std::vector<std::vector<CoinWgpuPortableNode>> nodes;
  std::vector<std::vector<float>> parameters;
  std::vector<std::vector<uint8_t>> signatures;
  explicit Retained(const CoinWgpuFrameView &v) : view(v) {
    modules.assign(v.portable_modules,
                   v.portable_modules + v.portable_module_count);
    for (const auto &m : modules) {
      nodes.emplace_back(m.nodes, m.nodes + m.node_count);
      parameters.emplace_back();
      if (m.parameter_count)
        parameters.back().assign(m.parameters,
                                 m.parameters + m.parameter_count);
      signatures.emplace_back(m.signature, m.signature + m.signature_bytes);
    }
  }
  bool preserved(const CoinWgpuFrameView &v) const {
    if (std::memcmp(&view, &v, sizeof(v)))
      return false;
    for (size_t i = 0; i < modules.size(); ++i) {
      const auto &m = v.portable_modules[i];
      if (std::memcmp(&m, &modules[i], sizeof(m)) ||
          std::memcmp(m.nodes, nodes[i].data(),
                      nodes[i].size() * sizeof(*m.nodes)) ||
          (!parameters[i].empty() &&
           std::memcmp(m.parameters, parameters[i].data(),
                       parameters[i].size() * sizeof(float))) ||
          std::memcmp(m.signature, signatures[i].data(), signatures[i].size()))
        return false;
    }
    return true;
  }
};
bool rejects(CoinWgpuFfiFrame &ffi, const CoinRenderFramePlan &bad,
             const std::string &label) {
  std::string error;
  const bool accepted = bad.isValid(&error);
  bool ok =
      check(!accepted && !error.empty(), label + " real FramePlan rejects");
  for (bool instancing : {false, true}) {
    const Retained prior(ffi.getView());
    error.clear();
    const bool prepared = ffi.prepare(bad, 32, 32, error, instancing);
    ok &= check(!prepared && !error.empty(),
                label + " real FFI rejects before GPU");
    ok &=
        check(prior.preserved(ffi.getView()),
              label + " failed prepare preserves previous pointers and bytes");
  }
  return ok;
}
// These are B14's exact image/UV/matrix operands at the real FramePlan and
// FFI boundary. No target, callback, encoder or rasterizer is involved.
Module projectionCpuModule(unsigned kind, float q) {
  Module m;
  if (kind == 0)
    m.expressions.push_back(constant({{.3f, .44f, .09f, q}}));
  else {
    auto coord = operation(Op::TEXCOORD, Type::FLOAT4);
    coord.textureUnit = 0;
    m.expressions.push_back(coord);
  }
  if (kind != 2) {
    auto sample = operation(Op::SAMPLE_2D, Type::FLOAT4, 0);
    sample.textureUnit = 0;
    m.expressions.push_back(sample);
    m.output = 1;
  } else {
    m.expressions.push_back(constant({{.2f, .1f, 1, .05f}}));
    m.expressions.push_back(operation(Op::DOT, Type::FLOAT1, 0, 1));
    m.expressions.push_back(constant({{.7f, .5f, .9f, 0}}));
    m.expressions.push_back(operation(Op::MULTIPLY, Type::FLOAT4, 2, 3));
    m.expressions.push_back(constant({{.1f, .2f, .15f, 1}}));
    m.expressions.push_back(operation(Op::ADD, Type::FLOAT4, 4, 5));
    m.output = 6;
  }
  return m;
}
CoinRenderFramePlan projectionCpuPacket(Module m, bool direct, float q) {
  CoinRenderShaderInstance instance;
  instance.module = m;
  std::string error;
  // All modules, including constant-coordinate Q0, are structurally legal.
  if (!check(coin_render_shader_ir(m, instance.ir, error),
             "profile-independent IR lowering: " + error))
    return {};
  auto f = packet(instance);
  auto &state = f.renderStates[0];
  state.textureProjection = direct ? CoinRenderTextureProjection::DIRECT_ST
                                   : CoinRenderTextureProjection::PROJECTIVE;
  state.hasTexture = true;
  state.textureMatrix[0][0] = 2;
  state.textureMatrix[1][1] = 1.5f;
  state.textureMatrix[2][2] = .5f;
  state.textureMatrix[3][0] = .02f;
  state.textureMatrix[3][1] = .05f;
  state.textureMatrix[3][2] = .09f;
  state.textureMatrix[3][3] = q;
  f.textures.resize(1);
  auto &image = f.textures[0];
  image.width = image.height = 2;
  image.pixelsRgba = {30, 210, 70,  255, 220, 60,  160, 255,
                      95, 120, 235, 255, 180, 200, 20,  255};
  f.samplers.resize(1);
  f.samplers[0].filter = CoinRenderTextureFilter::NEAREST;
  for (auto &vertex : f.vertices) {
    vertex.texcoord[0] = .14f;
    vertex.texcoord[1] = .26f;
  }
  return f;
}
bool contextualProjection() {
  bool ok = true;
  unsigned validCases = 0, rejectedCases = 0;
  CoinWgpuFfiFrame ffi;
  auto valid = [&](const CoinRenderFramePlan &f, bool direct, float q,
                   const char *label) {
    std::string error;
    const bool core = f.isValid(&error);
    bool accepted = check(core, std::string(label) + " Core: " + error);
    error.clear();
    const bool prepared = ffi.prepare(f, 32, 32, error, false);
    accepted &= check(prepared, std::string(label) + " FFI: " + error);
    if (prepared) {
      const auto &v = ffi.getView();
      accepted &=
          check(v.abi_version == COIN_WGPU_ABI_VERSION && v.portable_module_count == 1 &&
                    v.state_count && v.states[0].portable_shader_slot == 1 &&
                    v.states[0].texture_projection == (direct ? 1u : 0u) &&
                    v.states[0].texture_matrix[15] == q,
                "contextual policy and matrix Q delivered without stale state");
    }
    ++validCases;
    return accepted;
  };
  for (bool direct : {true, false}) {
    // Seed a complete predecessor before testing any rejected packet.
    const auto seed = projectionCpuPacket(projectionCpuModule(0, 1), direct, 1);
    ok &= valid(seed, direct, 1, "valid contextual predecessor");
    for (float q : {.5f, 0.f, -1.f}) {
      for (unsigned kind : {0u, 1u, 2u}) {
        // kind0: constant SAMPLE input; kind1: TEXCOORD->SAMPLE;
        // kind2: TEXCOORD->color, never a consumer of projective division.
        auto f = projectionCpuPacket(projectionCpuModule(kind, q), direct, q);
        const bool dividesZero = !direct && q == 0 && kind != 2;
        if (dividesZero) {
          ok &= rejects(ffi, f,
                        "PROJECTIVE SAMPLE zero Q with valid preceding packet");
          ++rejectedCases;
          ok &= valid(seed, direct, 1, "valid same-target-free FFI recovery");
        } else
          ok &= valid(f, direct, q, "finite contextual module input");
      }
    }
    // The source's captured Q is zero, but this AST supplies its own Q1.
    // Fixed texturing is replaced, not an additional implicit consumer.
    auto ownCoordinate =
        projectionCpuPacket(projectionCpuModule(0, 1), direct, 0);
    ok &= valid(ownCoordinate, direct, 0,
                "constant SAMPLE Q1 ignores unused captured Q0");
    for (float bad : {std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity()}) {
      auto malformed = seed;
      malformed.renderStates[0].textureMatrix[3][3] = bad;
      ok &= rejects(ffi, malformed,
                    "nonfinite captured matrix is never a finite-Q exception");
      ++rejectedCases;
    }
    ok &= valid(seed, direct, 1, "complete contextual A/B/A recovery");
  }
  std::cout << "portable_shader_context_cpu valid=" << validCases
            << " rejected=" << rejectedCases << " complete=" << ok << std::endl;
  return ok;
}
bool run() {
  CoinRenderShaderInstance s;
  bool ok = instance(authored(), .5f, s);
  ok &= canonical(s);
  std::string error;
  ok &= check(coin_render_shader_instance_valid(s, error),
              "valid canonical instance admitted without a device: " + error);
  auto good = packet(s);
  ok &= check(good.isValid(&error),
              "valid actual FramePlan accepts module: " + error);
  CoinWgpuFfiFrame ffi;
  const bool packed = ffi.prepare(good, 32, 32, error, false);
  ok &= check(packed,
              "valid module packs without GPU or callback registration: " +
                  error);
  if (!packed)
    return false;
  ok &= transport(ffi.getView(), good.portableShaders);
  auto reject = [&](const std::function<void(CoinRenderFramePlan &)> &edit,
                    const char *label) {
    auto bad = good;
    edit(bad);
    ok &= rejects(ffi, bad, label);
  };
  reject(
      [](CoinRenderFramePlan &f) { f.renderStates[0].portableShaderSlot = 2; },
      "out-of-range one-based module slot");
  reject(
      [](CoinRenderFramePlan &f) { f.portableShaders[0].parameters.clear(); },
      "missing actual scalar");
  reject(
      [](CoinRenderFramePlan &f) {
        f.portableShaders[0].parameters.push_back(1);
      },
      "extra actual scalar");
  for (float bad : {std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity()})
    reject(
        [&](CoinRenderFramePlan &f) {
          f.portableShaders[0].parameters[0] = bad;
        },
        "nonfinite uniform payload");
  reject(
      [](CoinRenderFramePlan &f) { f.portableShaders[0].module.version = 2; },
      "unknown authorial version");
  reject([](CoinRenderFramePlan &f) { f.portableShaders[0].ir.output = 3; },
         "invalid ordered IR output");
  reject(
      [](CoinRenderFramePlan &f) {
        auto &ir = f.portableShaders[0].ir;
        ir.nodes[ir.output].inputs[0] = ir.output;
      },
      "cyclic noncanonical ordered IR");
  reject(
      [](CoinRenderFramePlan &f) { f.portableShaders[0].ir.textureMask = 256; },
      "unknown logical image mask bit");
  reject(
      [](CoinRenderFramePlan &f) {
        auto &ir = f.portableShaders[0].ir;
        for (auto &n : ir.nodes)
          if (n.op == Op::CONSTANT)
            n.value[0] = .91f;
      },
      "canonical payload contradicts immutable authorial module");
  reject(
      [](CoinRenderFramePlan &f) {
        f.portableShaders[0].ir.alphaMayVary = false;
      },
      "false alpha proof contradicts variable uniform alpha");
  Module division = authored();
  division.expressions[0].op = Op::DIVIDE;
  CoinRenderShaderInstance divider;
  ok &= instance(division, .5f, divider);
  auto divided = packet(divider);
  ok &= check(divided.isValid(&error),
              "finite nonzero runtime denominator remains valid");
  for (float zero : {0.f, -0.f}) {
    auto bad = divided;
    bad.portableShaders[0].parameters[0] = zero;
    ok &= rejects(ffi, bad, "signed-zero actual uniform denominator");
  }
  // Full logical payload includes all declared fields, even when only its
  // endpoints feed the output. This is independent of any bank/storage layout.
  Module full;
  full.expressions = {uniform("field0", Type::FLOAT4),
                      uniform("field255", Type::FLOAT4),
                      operation(Op::ADD, Type::FLOAT4, 0, 1)};
  full.output = 2;
  CoinRenderShaderInstance large;
  for (unsigned i = 0; i < 256; ++i) {
    const std::string name = "field" + std::to_string(i);
    declare(full, name.c_str(), Type::FLOAT4);
    for (unsigned c = 0; c < 4; ++c)
      large.parameters.push_back(float((i * 7 + c) % 17) / 32.f);
  }
  large.module = full;
  error.clear();
  ok &= check(coin_render_shader_ir(full, large.ir, error) &&
                  large.ir.uniformFloats == 1024 &&
                  large.ir.uniformOffsets.size() == 256 &&
                  large.ir.uniformOffsets.front() == 0 &&
                  large.ir.uniformOffsets.back() == 1020,
              "4KiB logical signature retains first/last field offsets");
  auto fullFrame = packet(large);
  const bool fullPacked = ffi.prepare(fullFrame, 32, 32, error, false);
  ok &=
      check(fullPacked, "complete logical 4KiB actual payload packs: " + error);
  if (fullPacked)
    ok &= transport(ffi.getView(), fullFrame.portableShaders);
  auto extra = fullFrame;
  extra.portableShaders[0].parameters.push_back(.25f);
  ok &= rejects(ffi, extra, "4KiB plus one actual component is not truncated");
  const bool original = ffi.prepare(good, 32, 32, error, false);
  ok &= check(original, "small packet recovers after full payload proof");
  if (!original)
    return false;
  // A caller cannot turn content reuse into stale IR/parameters by retaining
  // frame revision. No revision edit is used anywhere in these controls.
  auto changed = good;
  changed.portableShaders[0].parameters[0] = .75f;
  error.clear();
  const bool parameterEdit = ffi.prepare(changed, 32, 32, error, false);
  ok &= check(parameterEdit && changed.revision == good.revision,
              "same-revision valid parameter edit is not discarded: " + error);
  if (parameterEdit)
    ok &= transport(ffi.getView(), changed.portableShaders);
  changed.portableShaders[0].module.expressions[2].value[2] = .83f;
  error.clear();
  ok &= check(coin_render_shader_ir(changed.portableShaders[0].module,
                                    changed.portableShaders[0].ir, error),
              "valid changed module obtains its own canonical IR");
  const bool irEdit = ffi.prepare(changed, 32, 32, error, false);
  ok &= check(irEdit,
              "same-revision IR/signature edit reflects actual content: " +
                  error);
  if (irEdit)
    ok &= transport(ffi.getView(), changed.portableShaders);
  // Two instances of the same signature retain different values and slots.
  auto two = good;
  two.portableShaders.push_back(s);
  two.portableShaders[1].parameters[0] = .875f;
  two.renderStates.push_back(two.renderStates[0]);
  two.renderStates[1].portableShaderSlot = 2;
  two.draws.push_back(two.draws[0]);
  two.draws[1].renderStateSlot = 1;
  const bool occurrences = ffi.prepare(two, 32, 32, error, false);
  ok &= check(occurrences,
              "two actual scoped instances preserve distinct slots: " + error);
  if (occurrences)
    ok &= transport(ffi.getView(), two.portableShaders);
  const bool recovery = ffi.prepare(good, 32, 32, error, false);
  ok &= check(recovery,
              "original complete module packet recovers after edits: " + error);
  if (recovery)
    ok &= transport(ffi.getView(), good.portableShaders);
  ok &= contextualProjection();
  std::cout << "portable_shader_transport complete=" << ok
            << " targets=0 gpu_queries=0 callback_registrations=0" << std::endl;
  return ok;
}
} // namespace
int main(int argc, char **) {
  if (argc != 1)
    return 2;
  SoDB::init();
  CoinRenderAction::initClass();
  return run() ? 0 : 1;
}
