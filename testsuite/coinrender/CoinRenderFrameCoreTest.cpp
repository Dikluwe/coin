#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderImageCore.h"
#include "rendering/coinrender/CoinRenderStateCore.h"

#include <Inventor/SoDB.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
bool
check(bool condition, const char * message)
{
  if (!condition) std::cerr << "CoinRenderFrameCoreTest: " << message << '\n';
  return condition;
}
}

int
main()
{
  SoDB::init();
  bool ok = true;

  CoinRenderTextureUnitSnapshot unitsA[1], unitsB[1];
  for (size_t i = sizeof(bool); i < offsetof(CoinRenderTextureUnitSnapshot, imageSlot); ++i) {
    reinterpret_cast<unsigned char*>(&unitsA[0])[i] = 0x55;
    reinterpret_cast<unsigned char*>(&unitsB[0])[i] = 0xaa;
  }
  ok &= check(coin_render_same_texture_units(unitsA, unitsB),
              "texture unit padding must not split equal render states");
  unitsB[0].enabled = true;
  ok &= check(!coin_render_same_texture_units(unitsA, unitsB),
              "texture unit values must still invalidate equality");

  CoinRenderFramePlan first;
  CoinRenderFramePlan second;
  first.revision = 41;
  second.revision = 99;
  ok &= check(first.hasSamePayload(second),
              "revision must not be part of immutable payload equality");

  first.vertices.push_back(CoinRenderVertexSnapshot());
  second.vertices.push_back(CoinRenderVertexSnapshot());
  ok &= check(first.hasSamePayload(second), "equal vertices must compare equal");
  second.vertices[0].position[1] = 2.0f;
  ok &= check(!first.hasSamePayload(second), "changed vertex must invalidate equality");

  CoinRenderFramePlan invalid;
  invalid.clearColor[0] = std::numeric_limits<float>::quiet_NaN();
  std::string diagnostic;
  ok &= check(!invalid.isValid(&diagnostic) &&
              diagnostic == "Invalid clearColor (NaN or inf)",
              "validation diagnostic must remain stable");

  std::vector<uint8_t> pixels = {
    1, 2, 3, 4, 5, 6, 7, 8,
    9, 10, 11, 12, 13, 14, 15, 16
  };
  ok &= check(CoinRenderImageCore::flipRgba8Rows(pixels, SbVec2i32(2, 2)),
              "valid RGBA8 image must flip");
  const std::vector<uint8_t> expected = {
    9, 10, 11, 12, 13, 14, 15, 16,
    1, 2, 3, 4, 5, 6, 7, 8
  };
  ok &= check(pixels == expected, "RGBA8 rows must be reversed exactly once");

  const std::vector<uint8_t> before = pixels;
  ok &= check(!CoinRenderImageCore::flipRgba8Rows(pixels, SbVec2i32(3, 2)) &&
              pixels == before,
              "invalid byte count must be rejected without mutation");

  if (!ok) return 1;
  std::cout << "CoinRenderFrameCoreTest passed\n";
  return 0;
}
