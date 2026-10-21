#ifndef COIN_RENDER_SHADER_MODULE_CORE_H
#define COIN_RENDER_SHADER_MODULE_CORE_H

#include <Inventor/rendering/CoinRenderShaderModule.h>
#include <string>
#include <cstddef>
#include <memory>

class SoShaderProgram;
struct CoinRenderShaderRegistration {
  CoinRenderShaderModule module;
  std::shared_ptr<SoShaderProgram> program;
};
#include <vector>

// Canonical, owned statements. Only active fields are copied into this IR.
// Inputs refer to earlier statements after sorting; uniform floats are densely
// packed into two 128-vec4 banks, avoiding BGFX's uint8 reflection array count.
struct CoinRenderShaderIrNode {
  CoinRenderShaderOp op = CoinRenderShaderOp::CONSTANT;
  CoinRenderShaderValueType type = CoinRenderShaderValueType::FLOAT1;
  std::array<uint32_t,3> inputs = {{UINT32_MAX,UINT32_MAX,UINT32_MAX}};
  std::array<float,4> value = {{0,0,0,0}};
  uint32_t resource = 0; // Uniform component offset or logical texture unit.
};
struct CoinRenderShaderIr {
  std::vector<CoinRenderShaderIrNode> nodes;
  std::vector<uint32_t> uniformOffsets;
  uint32_t uniformFloats = 0;
  uint32_t output = 0;
  uint32_t textureMask = 0;
  uint32_t coordinateMask = 0;
  bool alphaMayVary = true;
};
struct CoinRenderShaderInstance {
  CoinRenderShaderModule module; // Owned authorial signature, for strict ingress validation.
  CoinRenderShaderIr ir;
  std::vector<float> parameters;
};
COIN_RENDER_DLL_API bool coin_render_shader_ir(const CoinRenderShaderModule &, CoinRenderShaderIr &, std::string &);
// Validate the complete active-object parameter set against the signature.
// Returns packed logical floats only; no GL traversal or source loading.
struct CoinRenderFramePlan;
// Logical transport/source envelopes; these do not estimate driver allocations.
constexpr uint64_t COIN_RENDER_SHADER_SIGNATURE_BYTES = 65536;
constexpr uint64_t COIN_RENDER_SHADER_FUNCTION_BYTES = 262144;
constexpr uint64_t COIN_RENDER_SHADER_RESOURCE_BYTES = uint64_t(32) * 1024 * 1024;
COIN_RENDER_DLL_API bool coin_render_shader_resource_bytes(const CoinRenderFramePlan &, uint64_t &, std::string &);
COIN_RENDER_DLL_API bool coin_render_shader_execution_profile(const CoinRenderFramePlan &, std::string &);
COIN_RENDER_DLL_API bool coin_render_shader_active(const SoShaderProgram *, bool &, std::string &);
COIN_RENDER_DLL_API bool coin_render_shader_active(const CoinRenderShaderRegistration &, bool &, std::string &);
COIN_RENDER_DLL_API bool coin_render_shader_parameters(const CoinRenderShaderRegistration &,
                                   std::vector<float> &, std::string &);
// Expressions use Core-generated identifiers only; authored names never enter
// shader source. Sampling helpers and homogeneous coordinates belong to P07.
COIN_RENDER_DLL_API bool coin_render_shader_decode(const uint8_t *, size_t, CoinRenderShaderModule &,
                               CoinRenderShaderIr &, std::string &);
COIN_RENDER_DLL_API std::string coin_render_shader_key(const CoinRenderShaderModule &, const CoinRenderShaderIr &);
COIN_RENDER_DLL_API bool coin_render_shader_instance_valid(const CoinRenderShaderInstance &, std::string &);
struct CoinRenderShaderRange {
  std::array<double,4> lower = {{0,0,0,0}};
  std::array<double,4> upper = {{0,0,0,0}};
};
COIN_RENDER_DLL_API bool coin_render_shader_numeric(const CoinRenderShaderInstance &,
  const CoinRenderShaderRange &primary, const std::array<CoinRenderShaderRange,8> &coordinates,
  const std::array<CoinRenderShaderRange,8> &samples, std::string &error, bool projective = true);
COIN_RENDER_DLL_API std::string coin_render_shader_function(const CoinRenderShaderIr &, bool wgsl);

#endif
