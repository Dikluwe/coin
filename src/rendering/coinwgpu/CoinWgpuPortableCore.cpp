#include "CoinWgpuPortableCore.h"
#include "rendering/coinrender/CoinRenderShaderModuleCore.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <exception>

namespace {
unsigned inputCount(CoinRenderShaderOp op) {
  switch (op) {
  case CoinRenderShaderOp::ADD: case CoinRenderShaderOp::SUBTRACT:
  case CoinRenderShaderOp::MULTIPLY: case CoinRenderShaderOp::DIVIDE:
  case CoinRenderShaderOp::MINIMUM: case CoinRenderShaderOp::MAXIMUM:
  case CoinRenderShaderOp::DOT: return 2;
  case CoinRenderShaderOp::CLAMP: return 3;
  case CoinRenderShaderOp::SAMPLE_2D: return 1;
  default: return 0;
  }
}
void diagnostic(char *buffer, uint64_t capacity, const char *text) {
  if (!buffer || !capacity || capacity > uint64_t(std::numeric_limits<size_t>::max())) return;
  const size_t count = std::min(std::strlen(text), static_cast<size_t>(capacity - 1));
  std::memcpy(buffer, text, count);
  buffer[count] = 0;
}
template <typename T> bool aligned(const T *pointer) {
  return pointer && reinterpret_cast<uintptr_t>(pointer) % alignof(T) == 0;
}
CoinWgpuStatus lower(const CoinWgpuPortableModule *packet, const CoinWgpuPortableRange *primary,
  const CoinWgpuPortableRange *coordinates, const CoinWgpuPortableRange *samples, uint32_t textureProjection,
  char *output, uint64_t outputCapacity, uint64_t *required, char *error, uint64_t errorCapacity)
{
  try {
    const auto fail = [&](const char *text) { diagnostic(error, errorCapacity, text); return COIN_WGPU_INVALID_ARGUMENT; };
    if (textureProjection > 1 || !aligned(packet) || !aligned(required) || !packet->node_count || packet->node_count > 256 ||
        !aligned(packet->nodes) || packet->parameter_count > 1024 ||
        (packet->parameter_count && !aligned(packet->parameters)) || !packet->signature ||
        !packet->signature_bytes || packet->signature_bytes > uint64_t(std::numeric_limits<ptrdiff_t>::max()) ||
        packet->alpha_may_vary > 1 || outputCapacity > uint64_t(std::numeric_limits<ptrdiff_t>::max()) ||
        (outputCapacity && !output)) return fail("Invalid portable shader Core transport");
    if ((primary || coordinates || samples) &&
        (!aligned(primary) || !aligned(coordinates) || !aligned(samples)))
      return fail("Invalid portable shader numeric proof ranges");
    CoinRenderShaderInstance instance;
    std::string message;
    if (!coin_render_shader_decode(packet->signature, static_cast<size_t>(packet->signature_bytes),
        instance.module, instance.ir, message)) return fail(message.c_str());
    if (packet->node_count != instance.ir.nodes.size() || packet->output != instance.ir.output ||
        packet->parameter_count != instance.ir.uniformFloats ||
        packet->alpha_may_vary != uint32_t(instance.ir.alphaMayVary))
      return fail("Portable shader IR header differs from its Core signature");
    for (size_t index = 0; index < instance.ir.nodes.size(); ++index) {
      const auto &expected = instance.ir.nodes[index];
      const auto &actual = packet->nodes[index];
      if (actual.op != uint32_t(expected.op) || actual.type != uint32_t(expected.type))
        return fail("Portable shader IR statement differs from its Core signature");
      for (unsigned input = 0; input < inputCount(expected.op); ++input)
        if (actual.inputs[input] != expected.inputs[input]) return fail("Portable shader IR input differs from its Core signature");
      if (expected.op == CoinRenderShaderOp::CONSTANT &&
          std::memcmp(actual.value, expected.value.data(), sizeof(float) * unsigned(expected.type)))
        return fail("Portable shader IR constant differs from its Core signature");
      if ((expected.op == CoinRenderShaderOp::UNIFORM || expected.op == CoinRenderShaderOp::TEXCOORD ||
           expected.op == CoinRenderShaderOp::SAMPLE_2D) && actual.resource != expected.resource)
        return fail("Portable shader IR resource differs from its Core signature");
    }
    if (packet->parameter_count)
      instance.parameters.assign(packet->parameters, packet->parameters + packet->parameter_count);
    if (!coin_render_shader_instance_valid(instance, message)) return fail(message.c_str());
    if (primary) {
      CoinRenderShaderRange primaryRange;
      std::array<CoinRenderShaderRange,8> coordinateRanges, sampleRanges;
      std::copy(primary->lower, primary->lower + 4, primaryRange.lower.begin());
      std::copy(primary->upper, primary->upper + 4, primaryRange.upper.begin());
      for (unsigned unit = 0; unit < 8; ++unit) {
        std::copy(coordinates[unit].lower, coordinates[unit].lower + 4, coordinateRanges[unit].lower.begin());
        std::copy(coordinates[unit].upper, coordinates[unit].upper + 4, coordinateRanges[unit].upper.begin());
        std::copy(samples[unit].lower, samples[unit].lower + 4, sampleRanges[unit].lower.begin());
        std::copy(samples[unit].upper, samples[unit].upper + 4, sampleRanges[unit].upper.begin());
      }
      if (!coin_render_shader_numeric(instance, primaryRange, coordinateRanges, sampleRanges, message, textureProjection == 0)) return fail(message.c_str());
    }
    const std::string source = coin_render_shader_function(instance.ir, true);
    *required = static_cast<uint64_t>(source.size()) + 1;
    if (outputCapacity) {
      if (outputCapacity < *required) return fail("Portable shader generated-source buffer is too small");
      std::memcpy(output, source.c_str(), source.size() + 1);
    }
    diagnostic(error, errorCapacity, "");
    return COIN_WGPU_OK;
  } catch (const std::bad_alloc &) {
    diagnostic(error, errorCapacity, "Portable shader Core allocation failed");
    return COIN_WGPU_OUT_OF_MEMORY;
  } catch (...) {
    diagnostic(error, errorCapacity, "Portable shader Core lowering failed");
    return COIN_WGPU_INVALID_ARGUMENT;
  }
}
}

CoinWgpuStatus coin_wgpu_install_cpp_portable_core()
{
  return coin_wgpu_install_portable_core_v2(&lower);
}
