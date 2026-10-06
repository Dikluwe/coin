#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinbgfx/CoinBgfxProgramSelection.h"

#include <iostream>
#include <limits>

namespace {
bool check(bool condition, const char * message)
{
  if (!condition) std::cerr << "CoinBgfxProgramSelectionTest: " << message << '\n';
  return condition;
}
}

int main()
{
  const auto object = CoinBgfxTransparencyStrategy::OBJECT;
  std::vector<CoinBgfxDraw> draws(2);
  bool ok = check(coin_bgfx_solid_program(draws, false, object),
                  "solid plan was rejected");
  ok &= check(!coin_bgfx_solid_program({}, false, object), "empty plan needs no program");
  ok &= check(!coin_bgfx_solid_program(draws, true, object), "shadow plan was admitted");
  ok &= check(!coin_bgfx_solid_program(draws, false, CoinBgfxTransparencyStrategy::SORTED_LAYERS),
               "peeling plan was admitted");
  ok &= check(!coin_bgfx_solid_program(draws, false, CoinBgfxTransparencyStrategy::WEIGHTED_OIT),
               "weighted plan was admitted");

  // Reject a late incompatible draw, even if the earlier draws are solid.
  const CoinBgfxDraw original = draws.back();
  const auto rejected = [&](const char * message) {
    const bool result = check(!coin_bgfx_solid_program(draws, false, object), message);
    draws.back() = original;
    return result;
  };
  draws.back().blend = true; ok &= rejected("blending was admitted");
  draws.back().alpha = 0.5f; ok &= rejected("nonopaque material was admitted");
  draws.back().hasTexture = true; ok &= rejected("primary texture was admitted");
  for (auto & layer : draws.back().extraTextures) {
    layer.enabled = true;
    ok &= check(!coin_bgfx_solid_program(draws, false, object), "extra texture was admitted");
    layer.enabled = false;
  }
  draws.back().fogColorMode[3] = 1.0f; ok &= rejected("fog was admitted");
  draws.back().clipMeta[0] = 1.0f; ok &= rejected("clip plane was admitted");
  draws.back().screenDoor[0] = 1.0f; ok &= rejected("stipple was admitted");
  draws.back().screenDoor[3] = 1.0f; ok &= rejected("forced alpha was admitted");
  draws.back().fogColorMode[3] = std::numeric_limits<float>::quiet_NaN();
  ok &= rejected("nonfinite feature metadata was admitted");
  draws.back().fogColorMode[3] = -1.0f;
  ok &= rejected("nondefault feature metadata was admitted");

  // The solid stage retains the exact shared depth contract and vertex
  // lighting, so depth ranges, bias and lighting values do not disqualify it.
  draws.back().depthRange[0] = 0.25f;
  draws.back().depthRange[1] = 0.75f;
  draws.back().polygonOffsetFactor = 1.0f;
  draws.back().lightCount[0] = 8.0f;
  ok &= check(coin_bgfx_solid_program(draws, false, object),
               "unchanged vertex lighting/depth operations were rejected");
  if (ok) std::cout << "CoinBgfxProgramSelectionTest passed\n";
  return ok ? 0 : 1;
}
