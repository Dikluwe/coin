#ifndef COIN_RENDER_PORTABLE_SHADER_MODULE_CASES_H
#define COIN_RENDER_PORTABLE_SHADER_MODULE_CASES_H
#include "CoinRenderPortableShaderModuleScene.h"
#include <Inventor/rendering/CoinRenderShaderModule.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
namespace portable_module_test {
using Type = CoinRenderShaderValueType;
using Op = CoinRenderShaderOp;
using Module = CoinRenderShaderModule;
using Expr = CoinRenderShaderExpression;
Expr constant(Color value, Type type = Type::FLOAT4) {
  Expr e;
  e.op = Op::CONSTANT;
  e.type = type;
  e.value = value;
  return e;
}
Expr operation(Op op, Type type, uint32_t a = UINT32_MAX,
               uint32_t b = UINT32_MAX, uint32_t c = UINT32_MAX) {
  Expr e;
  e.op = op;
  e.type = type;
  e.inputs = {{a, b, c}};
  return e;
}
Module literal(Color color) {
  Module m;
  m.expressions.push_back(constant(color));
  m.alphaMayVary = TRUE;
  return m;
}
Expr uniform(const char *name, Type type) {
  Expr e = operation(Op::UNIFORM, type);
  e.uniformName = name;
  return e;
}
void declare(Module &m, const char *name, Type type) {
  CoinRenderShaderUniform u;
  u.name = name;
  u.type = type;
  m.uniforms.push_back(u);
}
bool validation(const Module &m, bool expected, const std::string &label) {
  SbString error;
  const bool accepted = coin_render_validate_shader_module(m, error);
  if (accepted != expected)
    std::cerr << "portable_module validation label=" << label
              << " accepted=" << accepted << " error=" << error.getString()
              << std::endl;
  return check(accepted == expected && (accepted || error.getLength() > 0),
               label);
}
Module binary(Op op, Color a, Color b) {
  Module m;
  m.expressions = {constant(a), constant(b), operation(op, Type::FLOAT4, 0, 1)};
  m.output = 2;
  return m;
}
bool core() {
  bool ok = true;
  const float nan = std::numeric_limits<float>::quiet_NaN(),
              inf = std::numeric_limits<float>::infinity();
  auto good = literal({{.2f, .4f, .7f, 1}});
  good.alphaMayVary = FALSE;
  ok &= validation(good, true, "opaque finite constant");
  Module m = good;
  m.version = 0;
  ok &= validation(m, false, "version zero");
  m.version = 2;
  ok &= validation(m, false, "future version");
  m = good;
  m.expressions.clear();
  ok &= validation(m, false, "empty expressions");
  m = good;
  m.output = 1;
  ok &= validation(m, false, "output outside expressions");
  m = good;
  m.expressions[0].type = Type::FLOAT3;
  ok &= validation(m, false, "output must be FLOAT4");
  for (float bad : {nan, inf, -inf}) {
    m = good;
    m.expressions[0].value[2] = bad;
    ok &= validation(m, false, "nonfinite active constant channel");
  }
  m = good;
  m.expressions[0].op = static_cast<Op>(999);
  ok &= validation(m, false, "unknown operation");
  m = good;
  m.expressions[0].type = static_cast<Type>(0);
  ok &= validation(m, false, "unknown value type");
  m = good;
  m.expressions.assign(256, good.expressions[0]);
  m.output = 255;
  ok &= validation(m, true, "256 expression boundary");
  m.expressions.push_back(good.expressions[0]);
  ok &= validation(m, false, "257 expressions");
  m = good;
  m.expressions.push_back(operation(Op::ADD, Type::FLOAT4, 0, 1));
  ok &= validation(m, false, "unreachable self cycle is invalid");
  m = good;
  m.expressions.push_back(operation(Op::ADD, Type::FLOAT4, 0, 999));
  ok &= validation(m, false, "unreachable invalid index is invalid");
  m = good;
  m.expressions.push_back(constant({{1, 0, 0, 0}}, Type::FLOAT1));
  for (unsigned i = 2; i <= 32; ++i)
    m.expressions.push_back(
        operation(Op::ADD, Type::FLOAT4, i == 2 ? 0 : i - 1, 1));
  m.output = 32;
  m.alphaMayVary = TRUE;
  ok &= validation(m, true, "DAG depth32 with shared scalar");
  m.expressions.push_back(operation(Op::ADD, Type::FLOAT4, 32, 1));
  m.output = 33;
  ok &= validation(m, false, "DAG depth33");
  m = good;
  m.expressions = {operation(Op::ADD, Type::FLOAT4, 3, 1),
                   constant({{.1f, .2f, .3f, .5f}}), constant({{9, 8, 7, 6}}),
                   constant({{.3f, .2f, .1f, .5f}})};
  m.output = 0;
  ok &=
      validation(m, true, "forward references and opaque constant alpha proof");
  m = good;
  m.expressions[0].value[3] = .5f;
  ok &= validation(m, false, "false alpha declaration needs structural alpha1");
  m.alphaMayVary = TRUE;
  ok &= validation(m, true, "variable-alpha constant is valid when declared");
  for (Op op :
       {Op::ADD, Op::SUBTRACT, Op::MULTIPLY, Op::MINIMUM, Op::MAXIMUM}) {
    m = binary(op, {{.2f, .3f, .4f, 1}}, {{.1f, .2f, .3f, 1}});
    ok &= validation(m, true, "same-vector binary operator");
    m.expressions[1] = constant({{.25f, nan, inf, -inf}}, Type::FLOAT1);
    ok &= validation(m, true, "scalar broadcast ignores inactive components");
    m.expressions[1].value = {{.25f, .5f, 0, 0}};
    m.expressions[1].type = Type::FLOAT2;
    ok &= validation(m, false, "unequal non-scalar vector sizes");
    m = binary(op, {{.2f, .3f, .4f, 1}}, {{.1f, .2f, .3f, 1}});
    m.expressions[2].type = Type::FLOAT3;
    ok &= validation(m, false, "wrong result type");
  }
  m = binary(Op::DIVIDE, {{.4f, .6f, .8f, 2}}, {{2, 0, 0, 0}});
  m.expressions[1].type = Type::FLOAT1;
  ok &= validation(m, true, "constant scalar divide");
  for (float zero : {0.f, -0.f}) {
    m.expressions[1].value[0] = zero;
    ok &= validation(m, false, "signed zero denominator");
  }
  m = binary(Op::DIVIDE, {{.4f, .6f, .8f, 2}}, {{2, 0, 0, 0}});
  ok &= validation(m, false, "vector denominator prohibited");
  m = good;
  m.alphaMayVary = TRUE;
  m.expressions.push_back(constant({{2, 0, 0, 0}}, Type::FLOAT1));
  m.expressions.push_back(operation(Op::ADD, Type::FLOAT1, 1, 1));
  m.expressions.push_back(operation(Op::DIVIDE, Type::FLOAT4, 0, 2));
  m.output = 3;
  ok &= validation(m, false, "expression-derived denominator prohibited");
  m = good;
  m.alphaMayVary = TRUE;
  declare(m, "denominator", Type::FLOAT1);
  m.expressions.push_back(uniform("denominator", Type::FLOAT1));
  m.expressions.push_back(operation(Op::DIVIDE, Type::FLOAT4, 0, 1));
  m.output = 2;
  ok &= validation(m, true, "uniform denominator checked at real ingress");
  m = good;
  m.alphaMayVary = TRUE;
  m.expressions.push_back(constant({{0, 0, 0, 0}}, Type::FLOAT1));
  m.expressions.push_back(constant({{1, 0, 0, 0}}, Type::FLOAT1));
  m.expressions.push_back(operation(Op::CLAMP, Type::FLOAT4, 0, 1, 2));
  m.output = 3;
  ok &= validation(m, true, "CLAMP scalar limits");
  m.expressions[1].type = Type::FLOAT2;
  ok &= validation(m, false, "CLAMP incompatible limit type");
  for (unsigned n = 2; n <= 4; ++n) {
    m = good;
    m.alphaMayVary = TRUE;
    m.expressions.push_back(
        constant({{.2f, .3f, .4f, .5f}}, static_cast<Type>(n)));
    m.expressions.push_back(m.expressions.back());
    m.expressions.push_back(operation(Op::DOT, Type::FLOAT1, 1, 2));
    m.expressions.push_back(operation(Op::MULTIPLY, Type::FLOAT4, 0, 3));
    m.output = 4;
    ok &= validation(m, true, "DOT vector2/3/4 broadcast into output");
    m.expressions[3].type = Type::FLOAT2;
    ok &= validation(m, false, "DOT output must be scalar");
  }
  m = good;
  m.alphaMayVary = TRUE;
  m.expressions[0] = operation(Op::PRIMARY_COLOR, Type::FLOAT4);
  ok &= validation(m, true, "primary before texturing");
  m.expressions[0].type = Type::FLOAT3;
  ok &= validation(m, false, "primary must be FLOAT4");
  m = good;
  m.alphaMayVary = TRUE;
  m.expressions[0] = operation(Op::TEXCOORD, Type::FLOAT4);
  m.expressions[0].textureUnit = 7;
  ok &= validation(m, true, "homogeneous texcoord unit7");
  m.expressions[0].textureUnit = 8;
  ok &= validation(m, false, "texcoord unit8 invalid");
  // A pure AST has no draw projection policy. Finite Q0 is usable by
  // DIRECT_ST and TEXCOORD observes homogeneous coordinates without division.
  for (float q : {.5f, 0.f, -1.f}) {
    Module coordinateSample;
    coordinateSample.expressions = {constant({{.3f, .44f, .09f, q}}),
                                    operation(Op::SAMPLE_2D, Type::FLOAT4, 0)};
    coordinateSample.output = 1;
    ok &= validation(coordinateSample, true,
                     "profile-independent SAMPLE accepts finite homogeneous Q");
  }
  m = good;
  m.alphaMayVary = TRUE;
  m.expressions[0] = operation(Op::TEXCOORD, Type::FLOAT4);
  for (unsigned u = 0; u < 8; ++u) {
    auto sample = operation(Op::SAMPLE_2D, Type::FLOAT4, 0);
    sample.textureUnit = u;
    m.expressions.push_back(sample);
  }
  m.output = 8;
  ok &= validation(m, true, "eight samples and logical units");
  m.expressions.push_back(m.expressions[1]);
  ok &= validation(m, false, "ninth sample even unreachable");
  m.expressions.pop_back();
  m.expressions[8].textureUnit = 8;
  ok &= validation(m, false, "sample unit outside0..7");
  m = good;
  m.alphaMayVary = TRUE;
  declare(m, "exactName", Type::FLOAT4);
  m.expressions[0] = uniform("exactName", Type::FLOAT4);
  ok &= validation(m, true, "uniform signature and exact type");
  m.expressions[0].uniformName = "ExactName";
  ok &= validation(m, false, "uniform names case-sensitive");
  m.expressions[0].uniformName = "exactName";
  m.uniforms.push_back(m.uniforms[0]);
  ok &= validation(m, false, "duplicate signature name");
  m = good;
  m.alphaMayVary = TRUE;
  for (unsigned i = 0; i < 256; ++i)
    declare(m, ("u" + std::to_string(i)).c_str(), Type::FLOAT4);
  m.expressions[0] = uniform("u255", Type::FLOAT4);
  ok &= validation(m, true,
                   "4KiB logical uniform boundary with last field referenced");
  declare(m, "overflow", Type::FLOAT1);
  ok &= validation(m, false, "4KiB plus one float");
  m = good;
  m.alphaMayVary = TRUE;
  declare(m, "", Type::FLOAT4);
  ok &= validation(m, false, "empty signature name");
  for (float value :
       {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::min(),
        -std::numeric_limits<float>::denorm_min(), -0.f, 0.f,
        std::numeric_limits<float>::denorm_min(),
        std::numeric_limits<float>::max()}) {
    m = literal({{value, .2f, .3f, 1}});
    m.alphaMayVary = FALSE;
    ok &= validation(m, true, "all finite f32 magnitudes remain constants");
  }
  m = good;
  m.alphaMayVary = TRUE;
  m.expressions = {constant({{.2f, .9f, .5f, 1}}),
                   constant({{.7f, .7f, .7f, 1}}),
                   constant({{.4f, .4f, .4f, 1}}),
                   operation(Op::CLAMP, Type::FLOAT4, 0, 1, 2)};
  m.output = 3;
  ok &= validation(
      m, true,
      "inverted CLAMP bounds retain defined min(max(x,lo),hi) semantics");
  m = binary(Op::MULTIPLY, {{std::numeric_limits<float>::max(), 1, 1, 1}},
             {{2, 1, 1, 1}});
  ok &= validation(m, false,
                   "finite constant operands may not overflow their operation");
  // Inactive fields must not become hidden semantic requirements.
  m = good;
  m.expressions[0].inputs = {{UINT32_MAX, 123456, 17}};
  m.expressions[0].uniformName = "unused";
  m.expressions[0].textureUnit = UINT32_MAX;
  ok &= validation(m, true, "inactive constant fields ignored");
  std::cout << "portable_module_core complete=" << ok << std::endl;
  return ok;
}
} // namespace portable_module_test
#endif
