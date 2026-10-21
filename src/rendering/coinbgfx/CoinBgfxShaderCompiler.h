#ifndef COIN_BGFX_SHADER_COMPILER_H
#define COIN_BGFX_SHADER_COMPILER_H
#include <string>
#include <vector>
#include <cstdint>
#include "rendering/coinrender/CoinRenderShaderModuleCore.h"
// Compiles only Core-generated code and fixed, installed shader templates.
// The executable and include roots are deployment configuration, never fields
// of SoShaderObject. No shell or authored program source is evaluated.
bool coin_bgfx_compile_portable_fragment(const CoinRenderShaderIr &,
  const std::string &fragmentTemplate, const std::string &profile,
  const std::string &defines, std::vector<uint8_t> &, std::string &);
#endif
