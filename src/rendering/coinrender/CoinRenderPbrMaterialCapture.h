#ifndef COIN_RENDER_PBR_MATERIAL_CAPTURE_H
#define COIN_RENDER_PBR_MATERIAL_CAPTURE_H
#include <Inventor/rendering/SoPBRMaterialData.h>
#include "rendering/coinrender/CoinRenderPbrMaterial.h"
// Mechanical value conversion; no traversal, ordering or GPU dependency.
inline CoinRenderPbrMaterialSnapshot coin_render_pbr_snapshot(const SoPBRMaterialData & source) {
  CoinRenderPbrMaterialSnapshot value;
  value.baseColor = source.baseColor;
  value.emissive = source.emissive;
  value.metallic = source.metallic;
  value.roughness = source.roughness;
  value.baseColorTextureUnit = source.baseColorTextureUnit;
  value.ormTextureUnit = source.ormTextureUnit;
  value.emissiveTextureUnit = source.emissiveTextureUnit;
  value.clearcoat = source.clearcoat;
  value.clearcoatRoughness = source.clearcoatRoughness;
  value.anisotropy = source.anisotropy;
  value.anisotropyRotation = source.anisotropyRotation;
  value.normalScale = source.normalScale;
  value.normalYFlip = source.normalYFlip;
  value.tangentCoordinateUnit = source.tangentCoordinateUnit;
  return value;
}
#endif
