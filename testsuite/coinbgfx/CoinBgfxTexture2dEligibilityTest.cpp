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
bool check(bool actual,bool expected,const std::string & name) {
  if(actual!=expected)
    std::cerr << "texture2d_eligibility " << name << " expected=" << expected
              << " actual=" << actual << '\n';
  return actual==expected;
}
}
int main() {
  const auto object=CoinBgfxTransparencyStrategy::OBJECT;
  std::vector<CoinBgfxDraw> draws(3);
  draws[0].hasTexture=true;
  draws[1].hasTexture=false;
  draws[2].hasTexture=true;
  auto eligible=[&](const char *name) {
    return check(coin_bgfx_texture2d_program(draws,false,object),true,name);
  };
  auto fallback=[&](const char *name) {
    return check(coin_bgfx_texture2d_program(draws,false,object),false,name);
  };
  bool ok=eligible("mixed textured and untextured draws");
  ok &= check(coin_bgfx_texture2d_program({},false,object),false,"empty plan");
  draws[0].hasTexture=false;draws[2].hasTexture=false;
  ok &= fallback("no textured draw");
  draws[0].hasTexture=true;draws[2].hasTexture=true;
  ok &= check(coin_bgfx_texture2d_program(draws,true,object),false,"real shadows");
  for(const auto policy:{CoinBgfxTransparencyStrategy::SORTED_LAYERS,
                         CoinBgfxTransparencyStrategy::WEIGHTED_OIT})
    ok &= check(coin_bgfx_texture2d_program(draws,false,policy),false,"non-object strategy");
  const CoinBgfxDraw original=draws[2];
  auto reset=[&](){draws[2]=original;};
  draws[2].extraTextures[6].enabled=true;
  ok &= fallback("late extra unit7");reset();
  draws[2].cubeFaceSize=2;draws[2].cubeSourceComponents=4;
  ok &= fallback("late cube base");reset();
  draws[2].volumeWidth=2;draws[2].volumeHeight=2;draws[2].volumeDepth=2;
  ok &= fallback("late volume base");reset();
  draws[2].transparencyStrategy=CoinBgfxTransparencyStrategy::SORTED_LAYERS;
  ok &= fallback("late draw strategy");reset();
  draws[2].alpha=.4f;draws[2].blend=true;
  ok &= eligible("fractional alpha blend");reset();
  draws[2].fogColorMode[3]=1.f;
  ok &= eligible("fog remains shared");reset();
  draws[2].clipMeta[0]=1.f;
  ok &= eligible("clipping remains shared");reset();
  draws[2].screenDoor[0]=.5f;
  ok &= eligible("effective screen door remains shared");reset();
  draws[2].alphaTestFunction=CoinRenderAlphaTestFunction::GREATER;
  draws[2].alphaTestReference=.2512f;
  ok &= eligible("active alpha comparison remains shared");reset();
  draws[2].alpha=std::numeric_limits<float>::quiet_NaN();
  ok &= fallback("nonfinite material alpha");reset();
  draws[2].alphaTestReference=std::numeric_limits<float>::infinity();
  ok &= fallback("nonfinite alpha reference");reset();
  draws[2].alphaTestFunction=static_cast<CoinRenderAlphaTestFunction>(999);
  ok &= fallback("invalid alpha function");reset();
  draws[2].fogColorMode[3]=std::numeric_limits<float>::quiet_NaN();
  ok &= fallback("nonfinite fog metadata");reset();
  draws[2].clipMeta[0]=std::numeric_limits<float>::quiet_NaN();
  ok &= fallback("nonfinite clip metadata");reset();
  draws[2].textureProjection=CoinRenderTextureProjection::DIRECT_ST;
  ok &= eligible("direct texture coordinate policy");reset();
  draws[2].depthRange[0]=.2f;draws[2].depthRange[1]=.8f;
  ok &= eligible("depth range remains shared");reset();
  if(ok) std::cout << "CoinBgfxTexture2dEligibilityTest passed\n";
  return ok?0:1;
}
