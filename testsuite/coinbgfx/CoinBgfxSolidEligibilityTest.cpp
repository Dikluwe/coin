#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinbgfx/CoinBgfxProgramSelection.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
bool expect(const std::string & name, const std::vector<CoinBgfxDraw> & draws,
            bool eligible, bool shadows=false,
            CoinBgfxTransparencyStrategy strategy=CoinBgfxTransparencyStrategy::OBJECT)
{
  const bool actual=coin_bgfx_solid_program(draws,shadows,strategy);
  if(actual!=eligible) {
    std::cerr << "solid_eligibility " << name << " expected=" << eligible
              << " actual=" << actual << '\n';
    return false;
  }
  return true;
}
}

int main()
{
  const CoinBgfxDraw plain{};
  bool ok=expect("untextured-opaque",{plain},true);
  ok &= expect("empty-plan",{},false);
  ok &= expect("two-compatible-draws",{plain,plain},true);

  CoinBgfxDraw draw=plain;
  draw.blend=true;
  for(float alpha : {0.25f,0.5f,0.75f,1.0f}) {
    draw.alpha=alpha;
    ok &= expect("blend-material-alpha-"+std::to_string(alpha),{draw},true);
  }
  draw=plain;
  draw.screenDoor[0]=0.0f;
  draw.screenDoor[3]=1.0f;
  draw.alpha=0.5f;
  draw.alphaTestFunction=CoinRenderAlphaTestFunction::GREATER;
  draw.alphaTestReference=0.9f;
  ok &= expect("zero-stipple-forces-alpha-before-comparison",{draw},true);
  draw.alphaTestFunction=CoinRenderAlphaTestFunction::EQUAL;
  draw.alphaTestReference=1.0f;
  ok &= expect("zero-stipple-equal-one",{draw},true);

  const CoinRenderAlphaTestFunction functions[]={
    CoinRenderAlphaTestFunction::NEVER,CoinRenderAlphaTestFunction::ALWAYS,
    CoinRenderAlphaTestFunction::LESS,CoinRenderAlphaTestFunction::LEQUAL,
    CoinRenderAlphaTestFunction::EQUAL,CoinRenderAlphaTestFunction::GEQUAL,
    CoinRenderAlphaTestFunction::GREATER,CoinRenderAlphaTestFunction::NOTEQUAL
  };
  for(size_t index=0;index<8;++index) {
    draw=plain;
    draw.blend=true;
    draw.alpha=0.5f;
    draw.alphaTestFunction=functions[index];
    draw.alphaTestReference=0.4f;
    ok &= expect("alpha-comparator-"+std::to_string(index),{draw},true);
  }

  ok &= expect("shadows",{plain},false,true);
  ok &= expect("peeling",{plain},false,false,
    CoinBgfxTransparencyStrategy::SORTED_LAYERS);
  ok &= expect("weighted-oit",{plain},false,false,
    CoinBgfxTransparencyStrategy::WEIGHTED_OIT);

  const auto incompatible=[&](const std::string & name,const CoinBgfxDraw & bad) {
    const bool alone=expect(name,{bad},false);
    const bool late=expect(name+"-late-draw",{plain,bad},false);
    return alone&&late;
  };
  draw=plain;draw.hasTexture=true;
  ok &= incompatible("base-texture",draw);
  for(size_t index=0;index<COIN_RENDER_MAX_TEXTURE_UNITS-1;++index) {
    draw=plain;draw.extraTextures[index].enabled=true;
    ok &= incompatible("extra-texture-"+std::to_string(index+1),draw);
  }
  draw=plain;draw.fogColorMode[3]=1.0f;
  ok &= incompatible("fog",draw);
  draw=plain;draw.clipMeta[0]=1.0f;
  ok &= incompatible("clip",draw);
  draw=plain;draw.screenDoor[0]=0.5f;
  ok &= incompatible("effective-stipple",draw);
  draw=plain;draw.alpha=std::numeric_limits<float>::quiet_NaN();
  ok &= incompatible("alpha-nan",draw);
  draw=plain;draw.alpha=std::numeric_limits<float>::infinity();
  ok &= incompatible("alpha-infinity",draw);
  draw=plain;draw.alphaTestReference=std::numeric_limits<float>::quiet_NaN();
  ok &= incompatible("reference-nan",draw);
  draw=plain;draw.alphaTestReference=std::numeric_limits<float>::infinity();
  ok &= incompatible("reference-infinity",draw);
  draw=plain;draw.alphaTestFunction=static_cast<CoinRenderAlphaTestFunction>(255);
  ok &= incompatible("alpha-function-invalid",draw);
  if(ok) std::cout << "CoinBgfxSolidEligibilityTest passed\n";
  return ok?0:1;
}
