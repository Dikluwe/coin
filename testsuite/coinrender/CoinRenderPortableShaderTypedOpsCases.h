#ifndef COIN_PORTABLE_SHADER_TYPED_OPS_CASES_H
#define COIN_PORTABLE_SHADER_TYPED_OPS_CASES_H
#include "CoinRenderPortableShaderDelivery.h"
struct TypedOpField {
  Type type;
  Color value;
  SoUniformShaderParameter *node;
};
void typedOpSet(TypedOpField &field, Color value) {
  field.value = value;
  switch (field.type) {
  case Type::FLOAT1:
    static_cast<SoShaderParameter1f *>(field.node)->value = value[0];
    break;
  case Type::FLOAT2:
    static_cast<SoShaderParameter2f *>(field.node)
        ->value.setValue(value[0], value[1]);
    break;
  case Type::FLOAT3:
    static_cast<SoShaderParameter3f *>(field.node)
        ->value.setValue(value[0], value[1], value[2]);
    break;
  case Type::FLOAT4:
    static_cast<SoShaderParameter4f *>(field.node)
        ->value.setValue(value[0], value[1], value[2], value[3]);
    break;
  }
}
struct TypedOpCase {
  Op op;
  // 0: vector/vector, 1: scalar/vector, 2: vector/scalar.
  // CLAMP uses bit0/bit1 for scalar lower/upper, or 4 for reversed scalar
  // bounds.
  unsigned form;
  uint32_t expression;
  bool scalarOutput;
  std::string label;
};
double typedOpExpected(const TypedOpCase &c, unsigned component,
                       const std::vector<TypedOpField> &fields,
                       unsigned width) {
  const double a = fields[0].value[component], b = fields[1].value[component];
  if (c.op == Op::DOT) {
    double sum = 0;
    for (unsigned i = 0; i < width; ++i)
      sum += double(fields[0].value[i]) * fields[1].value[i];
    return sum;
  }
  if (c.op == Op::DIVIDE)
    return a / fields[3].value[0];
  if (c.op == Op::CLAMP) {
    const double lo = c.form == 4  ? fields[7].value[0]
                      : c.form & 1 ? fields[6].value[0]
                                   : fields[4].value[component];
    const double hi = c.form == 4  ? fields[6].value[0]
                      : c.form & 2 ? fields[7].value[0]
                                   : fields[5].value[component];
    return std::min(std::max(a, lo), hi);
  }
  const double x = c.form == 1 ? fields[2].value[0] : a;
  const double y = c.form == 1 ? a : c.form == 2 ? fields[2].value[0] : b;
  switch (c.op) {
  case Op::ADD:
    return x + y;
  case Op::SUBTRACT:
    return x - y;
  case Op::MULTIPLY:
    return x * y;
  case Op::MINIMUM:
    return std::min(x, y);
  case Op::MAXIMUM:
    return std::max(x, y);
  default:
    return std::numeric_limits<double>::quiet_NaN();
  }
}
bool typedOpDelivery(Run &run, const Module &module,
                     const std::vector<TypedOpField> &fields) {
  std::vector<float> parameters;
  std::vector<uint32_t> offsets;
  for (const auto &field : fields) {
    offsets.push_back(uint32_t(parameters.size()));
    for (unsigned c = 0; c < uint32_t(field.type); ++c)
      parameters.push_back(field.value[c]);
  }
  const auto &frame = run.audit->plan;
  std::vector<uint32_t> draws;
  bool ok =
      check(frame.portableShaders.size() == 1,
            "typed operator occurrences share their one semantic instance");
  for (uint32_t i = 0; i < frame.draws.size(); ++i) {
    const auto &d = frame.draws[i];
    if (d.sourceNodeId != run.scene.mesh->getNodeId() || d.shadowLightSlot)
      continue;
    draws.push_back(i);
    ok &= check(
        portableCoreBinding(frame, d.renderStateSlot, module, parameters),
        "typed Core AST/signature and every active parameter bit retained");
  }
  ok &= check(portableCoreQuad(frame, draws, run.scene.positions),
              "typed operators cover every authorial triangle exactly once");
  if (frame.portableShaders.size() == 1)
    ok &= check(
        frame.portableShaders[0].ir.uniformOffsets == offsets &&
            frame.portableShaders[0].ir.uniformFloats == parameters.size(),
        "typed uniform widths produce independent dense authorial offsets");
  CoinWgpuFfiFrame ffi;
  std::string error;
  const bool prepared = ffi.prepare(frame, 64, 64, error, false);
  ok &= check(prepared, "real typed-op packet prepares: " + error);
  if (!prepared)
    return false;
  const auto &v = ffi.getView();
  std::vector<uint32_t> packed;
  for (uint32_t i = 0; i < v.draw_count; ++i) {
    packed.push_back(i);
    ok &= check(
        portableFfiBinding(v, v.draws[i].render_state_slot, module, parameters),
        "all FFI expression types, input order and parameter bits agree with "
        "authorial data");
  }
  std::array<std::array<double, 2>, 4> corners;
  for (unsigned i = 0; i < 4; ++i)
    corners[i] = {
        {run.scene.positions->point[i][0], run.scene.positions->point[i][1]}};
  return check(v.abi_version == COIN_WGPU_ABI_VERSION && v.portable_module_count == 1 &&
                   portableFfiQuad(v, packed, corners),
               "typed FFI instance and geometry association are complete") &&
         ok;
}
bool typedOps(Run &run, unsigned width) {
  if (width < 1 || width > 4)
    return false;
  const Type vectorType = static_cast<Type>(width);
  auto &scene = run.scene;
  scene.attach(true);
  scene.clearParameters();
  Module module;
  std::vector<TypedOpField> fields;
  auto addField = [&](const char *name, Type type, Color value) {
    SoUniformShaderParameter *node = nullptr;
    switch (type) {
    case Type::FLOAT1:
      node = new SoShaderParameter1f;
      break;
    case Type::FLOAT2:
      node = new SoShaderParameter2f;
      break;
    case Type::FLOAT3:
      node = new SoShaderParameter3f;
      break;
    case Type::FLOAT4:
      node = new SoShaderParameter4f;
      break;
    }
    node->ref();
    node->name = name;
    scene.owned.push_back(node);
    fields.push_back(TypedOpField{type, value, node});
    typedOpSet(fields.back(), value);
    declare(module, name, type);
    module.expressions.push_back(uniform(name, type));
    return uint32_t(module.expressions.size() - 1);
  };
  const auto a = addField("operand A", vectorType, {{.15f, .45f, .8f, .65f}});
  const auto b = addField("operand B", vectorType, {{.7f, .2f, .35f, .85f}});
  const auto scalar =
      addField("scalar left/right", Type::FLOAT1, {{.46f, 0, 0, 0}});
  const auto divisor = addField("denominator", Type::FLOAT1, {{1.4f, 0, 0, 0}});
  const auto lower =
      addField("lower vector", vectorType, {{.35f, .1f, .55f, .2f}});
  const auto upper =
      addField("upper vector", vectorType, {{.55f, .3f, .65f, .6f}});
  const auto lowScalar =
      addField("lower scalar", Type::FLOAT1, {{.3f, 0, 0, 0}});
  const auto highScalar =
      addField("upper scalar", Type::FLOAT1, {{.6f, 0, 0, 0}});
  uint32_t probe = UINT32_MAX;
  if (width > 1)
    probe = addField("component probe", vectorType, {{1, 0, 0, 0}});
  auto push = [&](Expr expression) {
    module.expressions.push_back(expression);
    return uint32_t(module.expressions.size() - 1);
  };
  std::vector<TypedOpCase> cases;
  const Op binary[] = {Op::ADD, Op::SUBTRACT, Op::MULTIPLY, Op::MINIMUM,
                       Op::MAXIMUM};
  const char *names[] = {"ADD", "SUBTRACT", "MULTIPLY", "MINIMUM", "MAXIMUM"};
  for (unsigned op = 0; op < 5; ++op)
    for (unsigned form = 0; form < (width == 1 ? 1u : 3u); ++form) {
      // Do not duplicate already qualified arithmetic/dense/scoped vec4 cases.
      if (width == 1 && op < 3)
        continue;
      if (width == 2 && op == 0 && form == 0)
        continue;
      if (width == 4 &&
          (form == 0 || (op == 0 && form == 2) || (op == 2 && form == 2)))
        continue;
      const auto x = form == 1 ? scalar : a;
      const auto y = form == 1 ? a : form == 2 ? scalar : b;
      const auto index = push(operation(binary[op], vectorType, x, y));
      cases.push_back(TypedOpCase{binary[op], form, index, width == 1,
                                  std::string(names[op]) +
                                      (form == 0   ? " equal"
                                       : form == 1 ? " scalar-left"
                                                   : " scalar-right")});
    }
  if (width == 2)
    cases.push_back(TypedOpCase{
        Op::DIVIDE, 2, push(operation(Op::DIVIDE, vectorType, a, divisor)),
        false, "DIVIDE vector/scalar"});
  for (unsigned form = 0; form <= 4; ++form) {
    if (width == 1 && form != 3 && form != 4)
      continue;
    if (width == 2 && form == 0)
      continue; // Dense focal has vec2/vec2 bounds.
    if (width == 4 && form != 1 && form != 2)
      continue;
    const auto lo = form == 4 ? highScalar : form & 1 ? lowScalar : lower;
    const auto hi = form == 4 ? lowScalar : form & 2 ? highScalar : upper;
    cases.push_back(TypedOpCase{
        Op::CLAMP, form, push(operation(Op::CLAMP, vectorType, a, lo, hi)),
        width == 1, std::string("CLAMP ") + std::to_string(form)});
  }
  if (width == 4)
    cases.push_back(TypedOpCase{Op::DOT, 0,
                                push(operation(Op::DOT, Type::FLOAT1, a, b)),
                                true, "DOT4 asymmetric full vector"});
  const size_t selectors = fields.size();
  uint32_t sum = UINT32_MAX;
  for (unsigned i = 0; i < cases.size(); ++i) {
    const auto selected = addField(("select/" + std::to_string(i)).c_str(),
                                   Type::FLOAT1, {{0, 0, 0, 0}});
    const auto observed = cases[i].scalarOutput
                              ? cases[i].expression
                              : push(operation(Op::DOT, Type::FLOAT1,
                                               cases[i].expression, probe));
    const auto gated =
        push(operation(Op::MULTIPLY, Type::FLOAT1, observed, selected));
    sum = sum == UINT32_MAX
              ? gated
              : push(operation(Op::ADD, Type::FLOAT1, sum, gated));
  }
  const auto weights = push(constant({{.35f, .25f, .15f, 0}}));
  const auto weighted =
      push(operation(Op::MULTIPLY, Type::FLOAT4, sum, weights));
  const auto background = push(constant({{.35f, .3f, .25f, 1}}));
  module.output = push(operation(Op::ADD, Type::FLOAT4, weighted, background));
  // Conservative scheduling is intentional; alpha is authored as 0*sum+1.
  module.alphaMayVary = TRUE;
  for (unsigned i = 0; i < fields.size(); ++i)
    scene.fragment->parameter.set1Value(i, fields[fields.size() - 1 - i].node);
  bool ok = run.bind(module);
  ok &= check(module.expressions.size() <= 256 && !cases.empty(),
              "all selected typed cases remain one legal bounded AST");
  std::vector<Color> original;
  for (const auto &field : fields)
    original.push_back(field.value);
  bool first = true;
  Frame stable;
  auto drawCase = [&](unsigned index, unsigned component, const char *phase) {
    for (unsigned i = 0; i < cases.size(); ++i)
      typedOpSet(fields[selectors + i], {{i == index ? 1.f : 0.f, 0, 0, 0}});
    if (width > 1) {
      Color basis = {{0, 0, 0, 0}};
      basis[component] = 1;
      typedOpSet(fields[8], basis);
    }
    const auto value = typedOpExpected(cases[index], component, fields, width);
    const Color expected = {{float(.35 + .35 * value), float(.3 + .25 * value),
                             float(.25 + .15 * value), 1}};
    const std::string label = "typed" + std::to_string(width) + " " + phase +
                              " " + cases[index].label +
                              " component=" + std::to_string(component);
    Frame frame;
    bool current = run.draw(expected, frame, label.c_str());
    if (current)
      current &= typedOpDelivery(run, module, fields);
    current &= check(
        first ? !run.lastCompiles.empty() : run.lastCompiles.empty(),
        first
            ? "one cold typed AST compiles its real GPU variant"
            : "operation/component/operand edits reuse the same compiled AST");
    first = false;
    if (current)
      stable = std::move(frame);
    ok &= current;
  };
  auto sweep = [&](const char *phase) {
    for (unsigned i = 0; i < cases.size(); ++i)
      for (unsigned component = 0;
           component < (cases[i].scalarOutput ? 1u : width); ++component)
        drawCase(i, component, phase);
  };
  std::cout << "portable_typed_ops width=" << width << " cases=" << cases.size()
            << " nodes=" << module.expressions.size()
            << " uniforms=" << fields.size() << std::endl;
  sweep("baseline");
  typedOpSet(fields[0], {{.76f, .13f, .4f, .9f}});
  typedOpSet(fields[1], {{.26f, .66f, .55f, .18f}});
  typedOpSet(fields[2], {{.31f, 0, 0, 0}});
  typedOpSet(fields[3], {{1.1f, 0, 0, 0}});
  typedOpSet(fields[4], {{.4f, .2f, .6f, .3f}});
  typedOpSet(fields[5], {{.65f, .5f, .75f, .55f}});
  typedOpSet(fields[6], {{.25f, 0, 0, 0}});
  typedOpSet(fields[7], {{.63f, 0, 0, 0}});
  sweep("all typed operands edited");
  for (unsigned i = 0; i < fields.size(); ++i)
    typedOpSet(fields[i], original[i]);
  drawCase(0, 0, "authorial A/B/A restored");
  return ok;
}
#endif
