#ifndef COIN_RENDER_PBR_MATERIAL_H
#define COIN_RENDER_PBR_MATERIAL_H
#include <Inventor/SbColor.h>
#include <Inventor/system/inttypes.h>
#include <cmath>
#include <initializer_list>
enum class CoinRenderMaterialModel : uint32_t { LEGACY = 0, PBR = 1 };
struct CoinRenderPbrMaterialSnapshot {
  SbColor baseColor = SbColor(0.8f, 0.8f, 0.8f);
  SbColor emissive = SbColor(0, 0, 0);
  float metallic = 0.0f;
  float roughness = 0.4f;
  int32_t baseColorTextureUnit = -1;
  int32_t ormTextureUnit = -1;
  int32_t emissiveTextureUnit = -1;
  float clearcoat = 0, clearcoatRoughness = 0;
  float anisotropy = 0, anisotropyRotation = 0;
  float normalScale = 1.0f;
  int32_t normalYFlip = 0;
  int32_t tangentCoordinateUnit = 0;


};
inline bool coin_render_valid_pbr_material(const CoinRenderPbrMaterialSnapshot & p)
{
  if(!std::isfinite(p.clearcoat)||p.clearcoat<0||p.clearcoat>1||!std::isfinite(p.clearcoatRoughness)||p.clearcoatRoughness<0||p.clearcoatRoughness>1)return false;
  if(!std::isfinite(p.anisotropy)||p.anisotropy<0||p.anisotropy>1||!std::isfinite(p.anisotropyRotation))return false;
  if(!std::isfinite(p.normalScale)||p.normalScale<0||p.normalScale>65504.f||p.tangentCoordinateUnit<0||p.tangentCoordinateUnit>7 || (p.normalYFlip!=0&&p.normalYFlip!=1))return false;
  for (int unit : {p.baseColorTextureUnit,p.ormTextureUnit,p.emissiveTextureUnit})
    if(unit < -1 || unit > 7)return false;
  if(p.ormTextureUnit >= 0 && (p.ormTextureUnit == p.baseColorTextureUnit || p.ormTextureUnit == p.emissiveTextureUnit))return false;
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
         a.metallic == b.metallic && a.roughness == b.roughness &&
    a.baseColorTextureUnit == b.baseColorTextureUnit &&
    a.ormTextureUnit == b.ormTextureUnit &&
    a.emissiveTextureUnit == b.emissiveTextureUnit &&
    a.clearcoat == b.clearcoat && a.clearcoatRoughness == b.clearcoatRoughness &&
    a.anisotropy == b.anisotropy && a.anisotropyRotation == b.anisotropyRotation &&
    a.normalScale == b.normalScale &&
    a.normalYFlip == b.normalYFlip &&
    a.tangentCoordinateUnit == b.tangentCoordinateUnit;
}
#endif
