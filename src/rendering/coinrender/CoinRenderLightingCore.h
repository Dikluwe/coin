#ifndef COIN_RENDER_LIGHTING_CORE_H
#define COIN_RENDER_LIGHTING_CORE_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <Inventor/SbVec4f.h>
#include <algorithm>
#include <cmath>

// Snapshot-only Gouraud lighting, shared by CPU rasterization and polygon styles.
inline SbVec4f coin_render_shade_vertex(const CoinRenderMaterialSnapshot& material,
                                        const SbVec3f& vPos, const SbVec3f& n,
                                        const CoinRenderLightingSnapshot& lighting,
                                        const CoinRenderRenderStateSnapshot& rs) {
  const SbVec3f viewDir(0.0f, 0.0f, 1.0f);
  const SbVec3f amb(material.ambient[0], material.ambient[1], material.ambient[2]);
  const SbVec3f diff(material.diffuse[0], material.diffuse[1], material.diffuse[2]);
  const SbVec3f spec(material.specular[0], material.specular[1], material.specular[2]);
  const SbVec3f emiss(material.emission[0], material.emission[1], material.emission[2]);
  float finalR = 0.0f;
  float finalG = 0.0f;
  float finalB = 0.0f;

  if (rs.lightModel == CoinRenderLightModel::BASE_COLOR) {
    finalR = diff[0];
    finalG = diff[1];
    finalB = diff[2];
  } else {
    finalR = amb[0] * lighting.ambientColor[0] * lighting.ambientIntensity + emiss[0];
    finalG = amb[1] * lighting.ambientColor[1] * lighting.ambientIntensity + emiss[1];
    finalB = amb[2] * lighting.ambientColor[2] * lighting.ambientIntensity + emiss[2];
    for (size_t lightIndex = 0; lightIndex < lighting.lights.size(); ++lightIndex) {
      const CoinRenderLightSourceSnapshot& light = lighting.lights[lightIndex];
      SbVec3f toLight;
      float attenuation = 1.0f;
      if (light.type == CoinRenderLightType::DIRECTIONAL) {
        toLight.setValue(-light.direction[0], -light.direction[1], -light.direction[2]);
        toLight.normalize();
      } else {
        SbVec3f lightPos(light.position[0], light.position[1], light.position[2]);
        SbVec3f delta = lightPos - vPos;
        const float distance = delta.length();
        if (distance <= 1.0e-6f)
          continue;
        toLight = delta / distance;
        const float denominator = light.attenuation[2] + light.attenuation[1] * distance +
                                  light.attenuation[0] * distance * distance;
        if (denominator <= 1.0e-6f)
          continue;
        attenuation = 1.0f / denominator;
        if (light.type == CoinRenderLightType::SPOT) {
          SbVec3f lightDirection(light.direction[0], light.direction[1], light.direction[2]);
          lightDirection.normalize();
          const float coneCos = lightDirection.dot(-toLight);
          if (coneCos < std::cos(light.cutOffAngle))
            continue;
          attenuation *= std::pow(std::max(coneCos, 0.0f), light.dropOffRate * 128.0f);
        }
      }
      const float diffuseFactor = std::max(0.0f, n.dot(toLight));
      if (diffuseFactor <= 0.0f)
        continue;
      SbVec3f halfVector = toLight + viewDir;
      halfVector.normalize();
      const float exponent = material.shininess * 128.0f;
      const float specularFactor =
          exponent > 0.0f ? std::pow(std::max(n.dot(halfVector), 0.0f), exponent) : 1.0f;
      const float strength = light.intensity * attenuation;
      finalR += (diff[0] * diffuseFactor + spec[0] * specularFactor) * light.color[0] * strength;
      finalG += (diff[1] * diffuseFactor + spec[1] * specularFactor) * light.color[1] * strength;
      finalB += (diff[2] * diffuseFactor + spec[2] * specularFactor) * light.color[2] * strength;
    }
  }

  // GL clamps primary colors before interpolation, including clipped vertices.
  return SbVec4f(std::max(0.0f, std::min(1.0f, finalR)), std::max(0.0f, std::min(1.0f, finalG)),
                 std::max(0.0f, std::min(1.0f, finalB)), material.diffuse[3]);
}

#endif
