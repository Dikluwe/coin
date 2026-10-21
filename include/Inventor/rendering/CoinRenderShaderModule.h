#ifndef COIN_RENDER_SHADER_MODULE_H
#define COIN_RENDER_SHADER_MODULE_H

#include <Inventor/CoinRenderExport.h>
#include <Inventor/SbBasic.h>
#include <Inventor/SbString.h>
#include <array>
#include <cstdint>
#include <vector>

enum class CoinRenderShaderValueType : uint32_t { FLOAT1=1, FLOAT2=2, FLOAT3=3, FLOAT4=4 };
enum class CoinRenderShaderOp : uint32_t {
  CONSTANT, PRIMARY_COLOR, TEXCOORD, UNIFORM, ADD, SUBTRACT, MULTIPLY, DIVIDE,
  MINIMUM, MAXIMUM, CLAMP, DOT, SAMPLE_2D
};
struct CoinRenderShaderUniform {
  SbString name;
  CoinRenderShaderValueType type = CoinRenderShaderValueType::FLOAT1;
};
struct CoinRenderShaderExpression {
  CoinRenderShaderOp op = CoinRenderShaderOp::CONSTANT;
  CoinRenderShaderValueType type = CoinRenderShaderValueType::FLOAT4;
  std::array<uint32_t,3> inputs = {{UINT32_MAX, UINT32_MAX, UINT32_MAX}};
  std::array<float,4> value = {{0,0,0,1}};
  SbString uniformName;
  uint32_t textureUnit = 0;
};
struct CoinRenderShaderModule {
  uint32_t version = 1;
  std::vector<CoinRenderShaderUniform> uniforms;
  std::vector<CoinRenderShaderExpression> expressions;
  uint32_t output = 0;
  SbBool alphaMayVary = TRUE;
};
COIN_RENDER_DLL_API SbBool coin_render_validate_shader_module(
  const CoinRenderShaderModule &, SbString &error);

#endif
