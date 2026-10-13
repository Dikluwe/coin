$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1
#include <bgfx_shader.sh>
#include "coin_depth.sh"
#include "coin_pbr_brdf.sh"
#ifdef COIN_PBR_SHADOW
#include "coin_pbr_shadows.sh"
#endif
#ifdef COIN_PBR_IBL
#include "coin_pbr_ibl.sh"
#endif
uniform vec4 u_pbrBaseMetallic;
uniform vec4 u_pbrEmissionRoughness;
uniform vec4 u_pbrCamera; // x: perspective; orthographic view is +Z.
uniform vec4 u_lightCount;
uniform vec4 u_lightPositionType[8];
uniform vec4 u_lightDirectionCutoff[8];
uniform vec4 u_lightColorIntensity[8];
uniform vec4 u_lightAttenuationDrop[8];
uniform vec4 u_clipMeta;
uniform vec4 u_clipPlanes[8];
void main() {
  for (int i = 0; i < 8; ++i) {
    if (float(i) >= u_clipMeta.x) break;
    if (dot(u_clipPlanes[i], vec4(v_viewPosition, 1.0)) < 0.0) discard;
  }
  gl_FragDepth = coinWindowDepth(v_coinClipDepth.x / v_coinClipDepth.y);
  vec3 n = coinPbrSafeUnit(v_viewNormal);
  vec3 v = u_pbrCamera.x > 0.5 ? coinPbrSafeUnit(-v_viewPosition) : vec3(0.0, 0.0, 1.0);
  float roughness=u_pbrEmissionRoughness.a;
#ifdef COIN_PBR_IBL
  roughness=coinPbrFilteredRoughness(n,roughness);
#endif
  vec3 radiance = u_pbrEmissionRoughness.rgb;
  for (int i = 0; i < 8; ++i) {
    if (float(i) >= u_lightCount.x) break;
    vec3 l; float attenuation = 1.0;
    if (u_lightPositionType[i].w < 0.5) l = coinPbrSafeUnit(-u_lightDirectionCutoff[i].xyz);
    else {
      vec3 delta = u_lightPositionType[i].xyz - v_viewPosition;
      float distance = length(delta);
      if (distance <= 1e-6) continue;
      l = delta / distance;
      vec3 a = u_lightAttenuationDrop[i].xyz;
      float divisor = a.z + distance * (a.y + distance * a.x);
      if (divisor <= 1e-6) continue;
      attenuation = 1.0 / divisor;
      if (u_lightPositionType[i].w > 1.5) {
        float cone = dot(coinPbrSafeUnit(u_lightDirectionCutoff[i].xyz), -l);
        if (cone < u_lightDirectionCutoff[i].w) continue;
        attenuation *= pow(max(cone, 0.0), u_lightAttenuationDrop[i].w * 128.0);
      }
    }
#ifdef COIN_PBR_IBL
    vec3 brdf = coinPbrQualityBrdf(n,v,l,u_pbrBaseMetallic.rgb,u_pbrBaseMetallic.a,roughness);
#else
    vec3 brdf = coinPbrBrdf(n,v,l,u_pbrBaseMetallic.rgb,u_pbrBaseMetallic.a,roughness);
#endif
    float visibility = 1.0;
#ifdef COIN_PBR_SHADOW
    visibility = coinPbrVisibility(i, v_viewPosition);
#endif
    radiance += visibility * brdf * max(dot(n, l), 0.0) * u_lightColorIntensity[i].rgb *
      (u_lightColorIntensity[i].a * attenuation);
  }
#ifdef COIN_PBR_IBL
  radiance += coinPbrEnvironment(n, v, u_pbrBaseMetallic.rgb,
    u_pbrBaseMetallic.a, roughness);
#endif
  // Saturate only at the representable binary16 limit, never at display white.
  gl_FragColor = vec4(min(radiance, vec3_splat(65504.0)), 1.0);
}
