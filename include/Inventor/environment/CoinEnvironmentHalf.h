#ifndef COIN_ENVIRONMENT_HALF_H
#define COIN_ENVIRONMENT_HALF_H
#include <Inventor/system/inttypes.h>
#include <cmath>
#include <cstring>
#include <limits>
// Renderer-independent IEEE binary16 and little-endian helpers.
struct CoinEnvironmentHalf {
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
};
#endif
