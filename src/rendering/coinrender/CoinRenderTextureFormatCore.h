#ifndef COIN_RENDER_TEXTURE_FORMAT_CORE_H
#define COIN_RENDER_TEXTURE_FORMAT_CORE_H
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

// Mechanical format rules, independent of Coin nodes, callbacks and GPU APIs.
class CoinRenderTextureFormatCore {
public:
  static bool valid(CoinRenderTextureFormat f) { return uint32_t(f) <= 4; }
  static bool srgb(CoinRenderTextureFormat f) {
    return f == CoinRenderTextureFormat::RGBA8_SRGB ||
           f == CoinRenderTextureFormat::BC3_SRGB;
  }
  static bool compressed(CoinRenderTextureFormat f) {
    return f == CoinRenderTextureFormat::BC3_LINEAR ||
           f == CoinRenderTextureFormat::BC3_SRGB;
  }
  static size_t levelBytes(uint32_t w, uint32_t h, CoinRenderTextureFormat f) {
    if (!w || !h || w > 8192 || h > 8192 || !valid(f))
      return 0;
    return compressed(f)
               ? size_t((w + 3) / 4) * ((h + 3) / 4) * 16
               : size_t(w) * h *
                     (f == CoinRenderTextureFormat::RGBA16_FLOAT ? 8 : 4);
  }
  static size_t mipBytes(uint32_t w, uint32_t h, CoinRenderTextureFormat f) {
    size_t bytes = 0;
    while (w > 1 || h > 1) {
      w = std::max(1u, w / 2);
      h = std::max(1u, h / 2);
      bytes += levelBytes(w, h, f);
    }
    return bytes;
  }
  static float decodeSrgb(float c) {
    return c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f);
  }
  static float encodeSrgb(float c) {
    return c <= .0031308f ? 12.92f * c
                          : 1.055f * std::pow(c, 1.f / 2.4f) - .055f;
  }
  static uint8_t unorm(float x) {
    return uint8_t(std::floor(std::max(0.f, std::min(1.f, x)) * 255 + .5f));
  }
  // IEEE binary16 conversion, round-to-nearest-even; no host half dependency.
  static float fromHalf(uint16_t h) {
    const bool negative = (h & 0x8000) != 0;
    const unsigned exponent = (h >> 10) & 31, mantissa = h & 1023;
    float value =
        exponent == 31 ? (mantissa ? std::numeric_limits<float>::quiet_NaN()
                                   : std::numeric_limits<float>::infinity())
        : exponent     ? std::ldexp(1.f + mantissa / 1024.f, int(exponent) - 15)
                       : std::ldexp(float(mantissa), -24);
    return negative ? -value : value;
  }
  static uint16_t toHalf(float x) {
    uint32_t bits;
    std::memcpy(&bits, &x, 4);
    const uint16_t sign = uint16_t((bits >> 16) & 0x8000);
    int e = int((bits >> 23) & 255) - 127 + 15;
    uint32_t m = bits & 0x7fffff;
    if (e >= 31)
      return uint16_t(sign | 0x7c00 |
                      (((bits >> 23) & 255) == 255 && m ? 0x200 : 0));
    if (e <= 0) {
      if (e < -10)
        return sign;
      m |= 0x800000;
      const unsigned shift = unsigned(14 - e);
      const uint32_t rounded =
          (m + ((1u << (shift - 1)) - 1) + ((m >> shift) & 1)) >> shift;
      return uint16_t(sign | rounded);
    }
    m += 0xfff + ((m >> 13) & 1);
    if (m & 0x800000) {
      m = 0;
      if (++e >= 31)
        return uint16_t(sign | 0x7c00);
    }
    return uint16_t(sign | (e << 10) | (m >> 13));
  }
  static uint16_t read16(const uint8_t *p) {
    return uint16_t(p[0] | uint16_t(p[1]) << 8);
  }
  static void write16(uint8_t *p, uint16_t n) {
    p[0] = uint8_t(n);
    p[1] = uint8_t(n >> 8);
  }
  static void color565(uint16_t n, uint8_t rgb[3]) {
    rgb[0] = uint8_t(((n >> 11) * 255 + 15) / 31);
    rgb[1] = uint8_t((((n >> 5) & 63) * 255 + 31) / 63);
    rgb[2] = uint8_t(((n & 31) * 255 + 15) / 31);
  }
  static void bcPalette(const uint8_t *block, uint8_t rgb[4][3],
                        uint8_t alpha[8]) {
    color565(read16(block + 8), rgb[0]);
    color565(read16(block + 10), rgb[1]);
    for (int c = 0; c < 3; ++c) {
      rgb[2][c] = uint8_t((2 * rgb[0][c] + rgb[1][c]) / 3);
      rgb[3][c] = uint8_t((rgb[0][c] + 2 * rgb[1][c]) / 3);
    }
    alpha[0] = block[0];
    alpha[1] = block[1];
    if (alpha[0] > alpha[1])
      for (unsigned i = 1; i <= 6; ++i)
        alpha[i + 1] = uint8_t(((7 - i) * alpha[0] + i * alpha[1]) / 7);
    else {
      for (unsigned i = 1; i <= 4; ++i)
        alpha[i + 1] = uint8_t(((5 - i) * alpha[0] + i * alpha[1]) / 5);
      alpha[6] = 0;
      alpha[7] = 255;
    }
  }
  static SbVec4f texel(const std::vector<uint8_t> &bytes, size_t offset,
                       uint32_t w, CoinRenderTextureFormat f, uint32_t x,
                       uint32_t y) {
    SbVec4f out;
    if (compressed(f)) {
      const uint8_t *b = bytes.data() + offset +
                         ((size_t(y / 4) * ((w + 3) / 4)) + x / 4) * 16;
      uint8_t palette[4][3], alpha[8];
      bcPalette(b, palette, alpha);
      const unsigned local = (y % 4) * 4 + x % 4;
      uint64_t ab = 0;
      uint32_t cb = 0;
      for (unsigned j = 0; j < 6; ++j)
        ab |= uint64_t(b[2 + j]) << (j * 8);
      for (unsigned j = 0; j < 4; ++j)
        cb |= uint32_t(b[12 + j]) << (j * 8);
      const unsigned index = (cb >> (local * 2)) & 3;
      for (int c = 0; c < 3; ++c)
        out[c] = palette[index][c] / 255.f;
      out[3] = alpha[(ab >> (local * 3)) & 7] / 255.f;
    } else {
      const size_t p =
          offset + (size_t(y) * w + x) *
                       (f == CoinRenderTextureFormat::RGBA16_FLOAT ? 8 : 4);
      for (unsigned c = 0; c < 4; ++c)
        out[c] = f == CoinRenderTextureFormat::RGBA16_FLOAT
                     ? fromHalf(read16(bytes.data() + p + c * 2))
                     : bytes[p + c] / 255.f;
    }
    if (srgb(f))
      for (int c = 0; c < 3; ++c)
        out[c] = decodeSrgb(out[c]);
    return out;
  }
  static void put(std::vector<uint8_t> &bytes, size_t p,
                  CoinRenderTextureFormat f, const SbVec4f &value) {
    for (unsigned c = 0; c < 4; ++c) {
      if (f == CoinRenderTextureFormat::RGBA16_FLOAT)
        write16(bytes.data() + p + c * 2, toHalf(value[c]));
      else
        bytes[p + c] =
            unorm(c < 3 && srgb(f) ? encodeSrgb(value[c]) : value[c]);
    }
  }
  static bool finiteHalfPayload(const std::vector<uint8_t> &bytes) {
    if (bytes.size() % 8)
      return false;
    for (size_t i = 0; i < bytes.size(); i += 2)
      if (!std::isfinite(fromHalf(read16(bytes.data() + i))))
        return false;
    return true;
  }
  // Bounded deterministic BC3 encoder. Input is encoded RGBA8, never linearized
  // SRGB values. Every edge block repeats its final valid texel.
  static bool encodeBc3(const std::vector<uint8_t> &input, uint32_t w,
                        uint32_t h, std::vector<uint8_t> &output) {
    if (input.size() != size_t(w) * h * 4 || !w || !h || w > 8192 || h > 8192)
      return false;
    std::vector<uint8_t> result(
        levelBytes(w, h, CoinRenderTextureFormat::BC3_LINEAR), 0);
    for (uint32_t by = 0; by < (h + 3) / 4; ++by)
      for (uint32_t bx = 0; bx < (w + 3) / 4; ++bx) {
        uint8_t pixels[16][4], low[4] = {255, 255, 255, 255}, high[4] = {};
        for (unsigned y = 0; y < 4; ++y)
          for (unsigned x = 0; x < 4; ++x) {
            auto *p = pixels[y * 4 + x];
            const size_t i = (size_t(std::min(h - 1, by * 4 + y)) * w +
                              std::min(w - 1, bx * 4 + x)) *
                             4;
            for (unsigned c = 0; c < 4; ++c) {
              p[c] = input[i + c];
              low[c] = std::min(low[c], p[c]);
              high[c] = std::max(high[c], p[c]);
            }
          }
        auto pack = [](const uint8_t *p) {
          return uint16_t(((p[0] * 31 + 127) / 255) << 11 |
                          ((p[1] * 63 + 127) / 255) << 5 |
                          ((p[2] * 31 + 127) / 255));
        };
        uint8_t *b = result.data() + (size_t(by) * ((w + 3) / 4) + bx) * 16;
        b[0] = high[3];
        b[1] = low[3];
        write16(b + 8, pack(high));
        write16(b + 10, pack(low));
        uint8_t palette[4][3], alpha[8];
        bcPalette(b, palette, alpha);
        uint64_t ab = 0;
        uint32_t cb = 0;
        for (unsigned i = 0; i < 16; ++i) {
          unsigned best = 0;
          int error = std::numeric_limits<int>::max();
          for (unsigned j = 0; j < 4; ++j) {
            int d = 0;
            for (unsigned c = 0; c < 3; ++c) {
              int v = int(pixels[i][c]) - palette[j][c];
              d += v * v;
            }
            if (d < error) {
              error = d;
              best = j;
            }
          }
          cb |= best << (i * 2);
          best = 0;
          error = 256;
          for (unsigned j = 0; j < 8; ++j) {
            int d = std::abs(int(pixels[i][3]) - alpha[j]);
            if (d < error) {
              error = d;
              best = j;
            }
          }
          ab |= uint64_t(best) << (i * 3);
        }
        for (unsigned j = 0; j < 6; ++j)
          b[j + 2] = uint8_t(ab >> (j * 8));
        for (unsigned j = 0; j < 4; ++j)
          b[j + 12] = uint8_t(cb >> (j * 8));
      }
    output.swap(result);
    return true;
  }
};
#endif
