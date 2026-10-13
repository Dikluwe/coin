#ifndef COIN_RENDER_PBR_MATERIAL_H
#define COIN_RENDER_PBR_MATERIAL_H
#include <Inventor/SbColor.h>
#include <Inventor/system/inttypes.h>
#include <cmath>
enum class CoinRenderMaterialModel : uint32_t { LEGACY = 0, PBR = 1 };
struct CoinRenderPbrMaterialSnapshot {
  SbColor baseColor = SbColor(0.8f, 0.8f, 0.8f);
  SbColor emissive = SbColor(0, 0, 0);
  float metallic = 0.0f;
  float roughness = 0.4f;
};
inline bool coin_render_valid_pbr_material(const CoinRenderPbrMaterialSnapshot & p)
{
  for (int i = 0; i < 3; ++i)
    if (!std::isfinite(p.baseColor[i]) || p.baseColor[i] < 0 || p.baseColor[i] > 1 ||
        !std::isfinite(p.emissive[i]) || p.emissive[i] < 0 || p.emissive[i] > 65504.0f) return false;
  return std::isfinite(p.metallic) && p.metallic >= 0 && p.metallic <= 1 &&
         std::isfinite(p.roughness) && p.roughness >= 0 && p.roughness <= 1;
}
inline bool coin_render_same_pbr_material(const CoinRenderPbrMaterialSnapshot & a,
                                         const CoinRenderPbrMaterialSnapshot & b)
{
  return a.baseColor == b.baseColor && a.emissive == b.emissive &&
         a.metallic == b.metallic && a.roughness == b.roughness;
}
#endif
