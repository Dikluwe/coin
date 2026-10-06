// Isolated diagnostic only. Does not change CoinRender validation or its ABI.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

struct Vertex {
  float position[3], normal[3], uv[2];
  uint32_t material;
  float extra[7][2], w, fog;
};
static_assert(sizeof(Vertex) == 100, "Match captured vertex stride");
bool bitFinite(float value) {
  static_assert(sizeof(float) == sizeof(uint32_t) &&
    std::numeric_limits<float>::is_iec559, "IEEE binary32 required for this probe");
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
}
bool standardFinite(float value) { return std::isfinite(value); }

template<bool (*Finite)(float)>
__declspec(noinline) bool validate(const std::vector<Vertex> & vertices) {
  for (const auto & v : vertices) {
    for (int k = 0; k < 3; ++k)
      if (!Finite(v.position[k]) || !Finite(v.normal[k])) return false;
    for (int k = 0; k < 2; ++k) if (!Finite(v.uv[k])) return false;
    if (!Finite(v.w) || v.w <= 0 || !Finite(v.fog)) return false;
    for (int u = 0; u < 7; ++u)
      for (int c = 0; c < 2; ++c) if (!Finite(v.extra[u][c])) return false;
    if (v.material >= 8) return false;
  }
  return true;
}

int main() {
  uint32_t patterns[] = {0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,
    0x00800000,0x80800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,
    0x7fc00000,0xffc00000,0x7f800001,0xff800001};
  uint32_t state = 136;
  for (unsigned i = 0; i < 65552; ++i) {
    state = state * UINT32_C(1664525) + UINT32_C(1013904223);
    const uint32_t bits = i < 16 ? patterns[i] : state;
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    if (bitFinite(value) != standardFinite(value)) return 1;
  }
  std::vector<Vertex> vertices(1440036);
  for (size_t i = 0; i < vertices.size(); ++i) {
    vertices[i].position[0] = float(i % 200);
    vertices[i].normal[2] = vertices[i].w = 1;
    vertices[i].fog = -1;
    vertices[i].material = uint32_t(i % 8);
  }
  using Clock = std::chrono::steady_clock;
  std::vector<double> standard, bits;
  for (int sample = 0; sample < 10; ++sample) {
    for (int order = 0; order < 2; ++order) {
      const bool bitMode = ((sample + order) % 2) != 0;
      const auto begin = Clock::now();
      const bool valid = bitMode ? validate<bitFinite>(vertices) : validate<standardFinite>(vertices);
      const auto end = Clock::now();
      if (!valid) return 2;
      (bitMode ? bits : standard).push_back(std::chrono::duration<double, std::milli>(end - begin).count());
    }
  }
  std::sort(standard.begin(), standard.end());
  std::sort(bits.begin(), bits.end());
  std::cout << "samples=10 vertices=" << vertices.size() << " checked_float_patterns=65552"
    << " standard_median_ms=" << standard[5] << " bits_median_ms=" << bits[5]
    << " standard_min_ms=" << standard.front() << " bits_min_ms=" << bits.front()
    << " standard_max_ms=" << standard.back() << " bits_max_ms=" << bits.back() << '\n';
}
