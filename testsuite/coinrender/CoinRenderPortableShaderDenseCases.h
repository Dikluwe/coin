#ifndef COIN_PORTABLE_SHADER_DENSE_CASES_H
#define COIN_PORTABLE_SHADER_DENSE_CASES_H
#include "CoinRenderPortableShaderDelivery.h"
// This signature has 511 scalar floats, a FLOAT3 beginning at float 511,
// and 255 FLOAT2 fields. Its final component is float 1023: exactly 4KiB.
// GPU color below is independent of any transport/shader implementation.
Color denseExpected(const std::vector<float> &p) {
  const double dot3 = (p[511] * .5 + p[512] * .25 + p[513] * .125) / p[510];
  const double x = std::min(.9, std::max(.1, double(p[1022]) + .05));
  const double y = std::min(.95, std::max(.2, double(p[1023]) + .025));
  const double sum = dot3 + x * .2 + y * .3 + double(p[0]) / p[510];
  return Color{
      {float(.05 + .8 * sum), float(.1 + .4 * sum), float(.15 + .2 * sum), 1}};
}
bool denseDelivery(Run &run, const Module &module,
                   const std::vector<float> &parameters) {
  const auto &f = run.audit->plan;
  std::vector<uint32_t> draws;
  bool ok = check(parameters.size() == 1024 && f.portableShaders.size() == 1,
                  "one complete mixed-type logical 4KiB instance is consumed");
  for (uint32_t i = 0; i < f.draws.size(); ++i) {
    const auto &d = f.draws[i];
    if (d.sourceNodeId != run.scene.mesh->getNodeId() || d.shadowLightSlot)
      continue;
    draws.push_back(i);
    ok &=
        check(portableCoreBinding(f, d.renderStateSlot, module, parameters),
              "all actual triangles retain dense mixed parameters bit exactly");
  }
  ok &= check(portableCoreQuad(f, draws, run.scene.positions),
              "dense module covers the complete disjoint authored quad");
  if (f.portableShaders.size() == 1) {
    const auto &ir = f.portableShaders[0].ir;
    ok &= check(
        ir.uniformFloats == 1024 && ir.uniformOffsets.size() == 767 &&
            ir.uniformOffsets[0] == 0 && ir.uniformOffsets[510] == 510 &&
            ir.uniformOffsets[511] == 511 && ir.uniformOffsets[512] == 514 &&
            ir.uniformOffsets[766] == 1022,
        "FLOAT3 crosses 511/512 without padding; FLOAT2 ends at 1023");
  }
  CoinWgpuFfiFrame ffi;
  std::string error;
  const bool prepared = ffi.prepare(f, 64, 64, error, false);
  ok &= check(prepared, "actual mixed-type FFI packet prepares: " + error);
  if (!prepared)
    return false;
  const auto &v = ffi.getView();
  std::vector<uint32_t> packed;
  for (uint32_t i = 0; i < v.draw_count; ++i) {
    const auto &d = v.draws[i];
    packed.push_back(i);
    ok &= check(portableFfiBinding(v, d.render_state_slot, module, parameters),
                "every FFI receiver binds exact signature and all 1024 floats");
  }
  std::array<std::array<double, 2>, 4> corners;
  for (unsigned i = 0; i < 4; ++i)
    corners[i] = {
        {run.scene.positions->point[i][0], run.scene.positions->point[i][1]}};
  ok &= check(v.abi_version == 66 && v.portable_module_count == 1 &&
                  v.portable_modules[0].parameter_count == 1024 &&
                  portableFfiQuad(v, packed, corners),
              "ABI66 preserves dense payload and complete authored geometry");
  return ok;
}
bool dense(Run &run) {
  auto &scene = run.scene;
  scene.attach(true);
  scene.clearParameters();
  Module m;
  std::vector<SoUniformShaderParameter *> fields;
  std::vector<SoShaderParameter1f *> scalar;
  std::vector<SoShaderParameter2f *> pairs;
  std::vector<float> parameters;
  auto own = [&](SoUniformShaderParameter *p) {
    p->ref();
    scene.owned.push_back(p);
    fields.push_back(p);
  };
  for (unsigned i = 0; i < 511; ++i) {
    const std::string name = "scalar/" + std::to_string(i);
    declare(m, name.c_str(), Type::FLOAT1);
    auto *p = new SoShaderParameter1f;
    p->name = name.c_str();
    const float value = i == 0 ? .2f : i == 510 ? 2.f : float(i % 7) * .02f;
    p->value = value;
    own(p);
    scalar.push_back(p);
    parameters.push_back(value);
  }
  declare(m, "crossing vec3", Type::FLOAT3);
  auto *cross = new SoShaderParameter3f;
  cross->name = "crossing vec3";
  cross->value.setValue(.15f, .25f, .35f);
  own(cross);
  parameters.insert(parameters.end(), {.15f, .25f, .35f});
  for (unsigned i = 0; i < 255; ++i) {
    const std::string name = "pair/" + std::to_string(i);
    declare(m, name.c_str(), Type::FLOAT2);
    auto *p = new SoShaderParameter2f;
    p->name = name.c_str();
    const float x = i == 254 ? .4f : float(i % 11) * .01f;
    const float y = i == 254 ? .6f : float(i % 13) * .01f;
    p->value.setValue(x, y);
    own(p);
    pairs.push_back(p);
    parameters.push_back(x);
    parameters.push_back(y);
  }
  m.expressions = {uniform("scalar/0", Type::FLOAT1),
                   uniform("scalar/510", Type::FLOAT1),
                   uniform("crossing vec3", Type::FLOAT3),
                   uniform("pair/254", Type::FLOAT2),
                   operation(Op::DIVIDE, Type::FLOAT3, 2, 1),
                   constant({{.5f, .25f, .125f, 0}}, Type::FLOAT3),
                   operation(Op::DOT, Type::FLOAT1, 4, 5),
                   constant({{.05f, .025f, 0, 0}}, Type::FLOAT2),
                   operation(Op::ADD, Type::FLOAT2, 3, 7),
                   constant({{.1f, .2f, 0, 0}}, Type::FLOAT2),
                   constant({{.9f, .95f, 0, 0}}, Type::FLOAT2),
                   operation(Op::CLAMP, Type::FLOAT2, 8, 9, 10),
                   constant({{.2f, .3f, 0, 0}}, Type::FLOAT2),
                   operation(Op::DOT, Type::FLOAT1, 11, 12),
                   operation(Op::DIVIDE, Type::FLOAT1, 0, 1),
                   operation(Op::ADD, Type::FLOAT1, 6, 13),
                   operation(Op::ADD, Type::FLOAT1, 15, 14),
                   constant({{.8f, .4f, .2f, 0}}),
                   operation(Op::MULTIPLY, Type::FLOAT4, 16, 17),
                   constant({{.05f, .1f, .15f, 1}}),
                   operation(Op::ADD, Type::FLOAT4, 18, 19)};
  m.output = 20;
  auto install = [&](bool reversed) {
    scene.fragment->parameter.setNum(int(fields.size()));
    for (unsigned i = 0; i < fields.size(); ++i)
      scene.fragment->parameter.set1Value(
          i, fields[reversed ? fields.size() - 1 - i : i]);
  };
  // Reversed declaration order from the first apply requires matching names,
  // rather than copying object-list positions into the dense uniform stream.
  install(true);
  bool ok = run.bind(m);
  Frame stable;
  auto draw = [&](const char *label, bool cached) {
    Frame current;
    bool good = run.draw(denseExpected(parameters), current, label);
    if (good)
      good &= denseDelivery(run, m, parameters);
    if (cached)
      good &= check(run.lastCompiles.empty(),
                    "mixed-type parameter edits reuse compiled AST/signature");
    if (good)
      stable = std::move(current);
    ok &= good;
    return good;
  };
  draw("cold 1f/2f/3f dense signature with cross-bank FLOAT3", false);
  const auto original = parameters;
  const Frame first = stable;
  // The weakest lane changes R by 10.2 levels (> 2*RGB3 + 3), so
  // the changed>3 witness is guaranteed even for two independent RGB3 errors.
  for (unsigned c = 0; c < 3; ++c) {
    auto vector = cross->value.getValue();
    vector[c] = original[511 + c] + .8f;
    cross->value = vector;
    parameters[511 + c] = vector[c];
    draw("independent component 511/512/513 crosses the bank boundary", true);
    unsigned changed = 0;
    for (size_t i = 0; i < stable.rgba.size() && i < first.rgba.size(); ++i)
      changed +=
          i % 4 < 3 && std::abs(int(stable.rgba[i]) - int(first.rgba[i])) > 3;
    ok &= check(changed > 1000,
                "each FLOAT3 component independently changes measured RGB");
    vector[c] = original[511 + c];
    cross->value = vector;
    parameters[511 + c] = vector[c];
    draw("restore one crossing component without recompilation", true);
  }
  pairs.back()->value.setValue(original[1022], original[1023] + .5f);
  parameters[1023] = original[1023] + .5f;
  draw("last FLOAT2 component at float 1023 remains observable", true);
  unsigned changed = 0;
  for (size_t i = 0; i < stable.rgba.size() && i < first.rgba.size(); ++i)
    changed +=
        i % 4 < 3 && std::abs(int(stable.rgba[i]) - int(first.rgba[i])) > 3;
  ok &= check(changed > 1000,
              "last dense component is not truncated by reflection");
  pairs.back()->value.setValue(original[1022], original[1023]);
  parameters = original;
  scalar[0]->value = .3f;
  parameters[0] = .3f;
  draw("first scalar and broadcast division have independent numerical effect",
       true);
  scalar[0]->value = original[0];
  parameters = original;
  install(false);
  draw("reordered parameter objects preserve name-resolved complete payload",
       true);
  scalar[510]->value = 0;
  ok &= run.refuse(stable, "actual scalar denominator zero rejects atomically");
  scalar[510]->value = original[510];
  draw("scalar denominator recovery on same target", true);
  auto invalid = cross->value.getValue();
  invalid[1] = std::numeric_limits<float>::quiet_NaN();
  cross->value = invalid;
  ok &= run.refuse(stable, "cross-bank actual NaN rejects without publication");
  cross->value.setValue(original[511], original[512], original[513]);
  draw("FLOAT3 NaN recovery retains current exact dense payload", true);
  auto *wrong = new SoShaderParameter2f;
  wrong->ref();
  wrong->name = "crossing vec3";
  wrong->value.setValue(.15f, .25f);
  scene.fragment->parameter.set1Value(511, wrong);
  ok &= run.refuse(stable,
                   "same name with wrong 2f/3f type rejects exact signature");
  scene.fragment->parameter.set1Value(511, cross);
  wrong->unref();
  draw("typed signature and first/last fields recover on same target", true);
  return ok;
}
#endif
