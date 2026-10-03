#ifndef COIN_RENDER_TEXTURE_COMBINE_CORE_H
#define COIN_RENDER_TEXTURE_COMBINE_CORE_H
#include "rendering/coinrender/CoinRenderFloatCore.h"

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <Inventor/elements/SoTextureCombineElement.h>
#include <algorithm>
#include <cmath>

// Four vec4 instructions per unit: active/RGB-op/alpha-op, RGB arguments/scale,
// alpha arguments/scale, constant. Sources 0..3 are primary/texture/constant/previous;
// operands 0..3 are color/complement/alpha/alpha-complement. No Coin enum reaches Infra.
inline bool coin_render_compile_combine(const SoTextureCombineElement::UnitData& raw,
                                        CoinRenderTextureCombineSnapshot& result,
                                        std::string& diagnostic) {
  using E = SoTextureCombineElement;
  result = CoinRenderTextureCombineSnapshot();
  auto operation = [](int op) -> int {
    switch (op) {
    case E::REPLACE:
      return 0;
    case E::MODULATE:
      return 1;
    case E::ADD:
      return 2;
    case E::ADD_SIGNED:
      return 3;
    case E::SUBTRACT:
      return 4;
    case E::INTERPOLATE:
      return 5;
    case E::DOT3_RGB:
      return 6;
    case E::DOT3_RGBA:
      return 7;
    default:
      return -1;
    }
  };
  auto source = [](int src) -> int {
    switch (src) {
    case E::PRIMARY_COLOR:
      return 0;
    case E::TEXTURE:
      return 1;
    case E::CONSTANT:
      return 2;
    case E::PREVIOUS:
      return 3;
    default:
      return -1;
    }
  };
  auto operand = [](int op) -> int {
    switch (op) {
    case E::SRC_COLOR:
      return 0;
    case E::ONE_MINUS_SRC_COLOR:
      return 1;
    case E::SRC_ALPHA:
      return 2;
    case E::ONE_MINUS_SRC_ALPHA:
      return 3;
    default:
      return -1;
    }
  };
  const int rgb = operation(raw.rgboperation), alpha = operation(raw.alphaoperation);
  if (rgb < 0 || alpha < 0 || alpha > 5 ||
      (raw.rgbscale != 1 && raw.rgbscale != 2 && raw.rgbscale != 4) ||
      (raw.alphascale != 1 && raw.alphascale != 2 && raw.alphascale != 4)) {
    diagnostic = "Invalid TextureCombine operation or scale (expected 1, 2 or 4)";
    return false;
  }
  result.instructions[0][0] = 1;
  result.instructions[0][1] = float(rgb);
  result.instructions[0][2] = float(alpha);
  result.instructions[1][3] = raw.rgbscale;
  result.instructions[2][3] = raw.alphascale;
  for (int i = 0; i < 3; ++i) {
    const int rs = source(raw.rgbsource[i]), as = source(raw.alphasource[i]);
    const int ro = operand(raw.rgboperand[i]), ao = operand(raw.alphaoperand[i]);
    if (rs < 0 || as < 0 || ro < 0 || ao < 2) {
      diagnostic =
          "Invalid TextureCombine source or operand (alpha requires SRC_ALPHA or its complement)";
      return false;
    }
    result.instructions[1][i] = float(rs + 4 * ro);
    result.instructions[2][i] = float(as + 4 * ao);
  }
  for (int c = 0; c < 4; ++c) {
    if (!coin_render_is_finite(raw.constantcolor[c])) {
      diagnostic = "Non-finite TextureCombine constant";
      return false;
    }
    result.instructions[3][c] = std::max(0.f, std::min(1.f, raw.constantcolor[c]));
  }
  diagnostic.clear();
  return true;
}

inline bool coin_render_validate_combine(const CoinRenderTextureCombineSnapshot& program) {
  const auto& p = program.instructions;
  for (int row = 0; row < 4; ++row)
    for (int c = 0; c < 4; ++c)
      if (!coin_render_is_finite(p[row][c]))
        return false;
  if (p[0][0] == 0)
    return true;
  if (p[0][0] != 1 || p[0][1] < 0 || p[0][1] > 7 || std::floor(p[0][1]) != p[0][1] || p[0][2] < 0 ||
      p[0][2] > 5 || std::floor(p[0][2]) != p[0][2])
    return false;
  for (int row = 1; row <= 2; ++row) {
    if (p[row][3] != 1 && p[row][3] != 2 && p[row][3] != 4)
      return false;
    for (int c = 0; c < 3; ++c)
      if (p[row][c] < (row == 2 ? 8 : 0) || p[row][c] > 15 || std::floor(p[row][c]) != p[row][c])
        return false;
  }
  for (int c = 0; c < 4; ++c)
    if (p[3][c] < 0 || p[3][c] > 1)
      return false;
  return true;
}

inline SbVec4f coin_render_combine_argument(float instruction, const SbVec4f& primary,
                                            const SbVec4f& texture, const SbVec4f& constant,
                                            const SbVec4f& previous) {
  const int code = int(instruction), source = code % 4, operand = code / 4;
  SbVec4f value = source == 0 ? primary : source == 1 ? texture : source == 2 ? constant : previous;
  if (operand >= 2)
    value = SbVec4f(value[3], value[3], value[3], value[3]);
  if (operand == 1 || operand == 3)
    for (int i = 0; i < 4; ++i)
      value[i] = 1 - value[i];
  return value;
}

inline SbVec4f coin_render_combine_operation(int operation, const SbVec4f& a, const SbVec4f& b,
                                             const SbVec4f& c) {
  SbVec4f result;
  float dot = 0;
  for (int i = 0; i < 3; ++i)
    dot += 4 * (a[i] - .5f) * (b[i] - .5f);
  for (int i = 0; i < 4; ++i) {
    switch (operation) {
    case 0:
      result[i] = a[i];
      break;
    case 1:
      result[i] = a[i] * b[i];
      break;
    case 2:
      result[i] = a[i] + b[i];
      break;
    case 3:
      result[i] = a[i] + b[i] - .5f;
      break;
    case 4:
      result[i] = a[i] - b[i];
      break;
    case 5:
      result[i] = a[i] * c[i] + b[i] * (1 - c[i]);
      break;
    default:
      result[i] = dot;
      break;
    }
  }
  return result;
}

inline SbVec4f coin_render_texture_combine(const CoinRenderTextureCombineSnapshot& program,
                                           const SbVec4f& primary, const SbVec4f& texture,
                                           const SbVec4f& previous) {
  const auto& p = program.instructions;
  const SbVec4f constant(p[3]);
  SbVec4f rgb[3], alpha[3];
  for (int i = 0; i < 3; ++i) {
    rgb[i] = coin_render_combine_argument(p[1][i], primary, texture, constant, previous);
    alpha[i] = coin_render_combine_argument(p[2][i], primary, texture, constant, previous);
  }
  SbVec4f result = coin_render_combine_operation(int(p[0][1]), rgb[0], rgb[1], rgb[2]);
  // DOT3_RGBA replaces alpha as well and ignores alpha operation, retaining the separate alpha
  // scale.
  const float a =
      int(p[0][1]) == 7
          ? result[0] * p[2][3]
          : coin_render_combine_operation(int(p[0][2]), alpha[0], alpha[1], alpha[2])[3] * p[2][3];
  for (int i = 0; i < 3; ++i)
    result[i] = std::max(0.f, std::min(1.f, result[i] * p[1][3]));
  result[3] = std::max(0.f, std::min(1.f, a));
  return result;
}

// Conservative alpha proof; a false proof routes to blending, never drops alpha.
inline bool coin_render_combine_may_have_alpha(const CoinRenderTextureCombineSnapshot& program,
                                               bool primaryAlpha, bool textureAlpha,
                                               bool previousAlpha) {
  const auto& p = program.instructions;
  if (int(p[0][1]) == 7)
    return true;
  float lo[3], hi[3];
  for (int i = 0; i < 3; ++i) {
    const int code = int(p[2][i]), src = code % 4;
    lo[i] = src == 2 ? p[3][3]
            : (src == 0   ? primaryAlpha
               : src == 1 ? textureAlpha
                          : previousAlpha)
                ? 0.f
                : 1.f;
    hi[i] = src == 2 ? p[3][3] : 1.f;
    if (code / 4 == 3) {
      const float low = lo[i];
      lo[i] = 1 - hi[i];
      hi[i] = 1 - low;
    }
  }
  float minimum = 0;
  switch (int(p[0][2])) {
  case 0:
    minimum = lo[0];
    break;
  case 1:
    minimum = lo[0] * lo[1];
    break;
  case 2:
    minimum = lo[0] + lo[1];
    break;
  case 3:
    minimum = lo[0] + lo[1] - .5f;
    break;
  case 4:
    minimum = lo[0] - hi[1];
    break;
  case 5:
    minimum = std::min(lo[0] * lo[2] + lo[1] * (1 - lo[2]), lo[0] * hi[2] + lo[1] * (1 - hi[2]));
    break;
  }
  return minimum * p[2][3] < 1.f;
}
#endif
