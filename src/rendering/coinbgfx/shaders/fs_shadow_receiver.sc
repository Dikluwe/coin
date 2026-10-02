$input v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex
#include <bgfx_shader.sh>
#include "coin_depth.sh"
#include "coin_lighting.sh"
#define COIN_SHADOW_SURFACE 1
#include "coin_surface.sh"
SAMPLER2D(s_shadow0, 8);
SAMPLER2D(s_shadow1, 9);
uniform mat4 u_shadowViewToClip0;
uniform mat4 u_shadowViewToClip1;
uniform mat4 u_shadowViewToLight0;
uniform mat4 u_shadowViewToLight1;
uniform vec4 u_shadowParams0;
uniform vec4 u_shadowParams1;
uniform vec4 u_shadowMeta0;
uniform vec4 u_shadowMeta1;
uniform vec4 u_shadowLightIndices;
uniform vec4 u_shadowLightIndicesExtra;
#include "coin_shadow_quality.sh"
float coinShadowVisibility(vec3 positionView, mat4 viewToClip, mat4 viewToLight,
                           vec4 params, vec4 meta, sampler2D mapTexture)
{
  vec4 coord = mul(viewToClip, vec4(positionView, 1.0));
  if (coord.w <= 0.0) return 1.0;
  vec3 clip = coord.xyz / coord.w;
  vec2 uv = vec2(clip.x * 0.5 + 0.5,
    meta.w > 0.5 ? clip.y * 0.5 + 0.5 : 0.5 - clip.y * 0.5);
  if (uv.x < 0.0 || uv.x >= 1.0 || uv.y < 0.0 || uv.y >= 1.0 ||
      clip.z < 0.0 || clip.z > 1.0) return 1.0;
  vec2 moments = texture2D(mapTexture, uv).xy;
  if (moments.x >= 0.9999) return 1.0;
  vec3 lightView = mul(viewToLight, vec4(positionView, 1.0)).xyz;
  float distance = meta.x > 0.5 ? length(lightView) : -lightView.z;
  float normalized = (distance - params.x) / (params.y - params.x);
  if (normalized <= moments.x) return 1.0;
  float variance = min(max(moments.y - moments.x * moments.x, 0.0) + params.z, 1.0);
  float delta = moments.x - normalized;
  float probability = variance / (variance + delta * delta);
  probability *= smoothstep(params.w, 1.0, probability);
  if (meta.x < 0.5 && meta.y > 0.0) {
    float eyeZ = positionView.z;
    float fade = min(1.0, exp(meta.z * eyeZ * abs(eyeZ) / (meta.y * meta.y)));
    return 1.0 - (1.0 - probability) * fade;
  }
  return probability;
}
void main()
{
  vec4 color = v_color0;
  vec3 specularColor = vec3_splat(0.0);
  if (v_material.y > 0.5) color.rgb += coinOrdinaryFragmentColor(v_texcoords4, v_specular, v_viewPosition, v_viewNormal, v_material.x, specularColor);
  if (v_material.y > 0.5) {
    if (u_shadowLightIndices.x >= 0.0) {
      int index = int(u_shadowLightIndices.x);
      vec3 contribution = coinShadowContribution(index,
        coinShadowVertexColor(0, v_ambient, v_emission, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex),
        v_texcoords4, v_specular, v_viewPosition, v_viewNormal, v_material.x);
      vec3 specularPart = u_shadowQuality.x > 0.5 ? vec3_splat(0.0) :
        coinGroupLightContribution(index, vec4_splat(0.0), v_specular, v_viewPosition, v_viewNormal, v_material.x);
      float visibility = coinShadowVisibility(v_viewPosition,
        u_shadowViewToClip0, u_shadowViewToLight0, u_shadowParams0,
        u_shadowMeta0, s_shadow0);
      color.rgb += (contribution - specularPart) * visibility;
      specularColor += specularPart * visibility;
    }
    if (u_shadowLightIndices.y >= 0.0) {
      int index = int(u_shadowLightIndices.y);
      vec3 contribution = coinShadowContribution(index,
        coinShadowVertexColor(1, v_ambient, v_emission, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex),
        v_texcoords4, v_specular, v_viewPosition, v_viewNormal, v_material.x);
      vec3 specularPart = u_shadowQuality.x > 0.5 ? vec3_splat(0.0) :
        coinGroupLightContribution(index, vec4_splat(0.0), v_specular, v_viewPosition, v_viewNormal, v_material.x);
      float visibility = coinShadowVisibility(v_viewPosition,
        u_shadowViewToClip1, u_shadowViewToLight1, u_shadowParams1,
        u_shadowMeta1, s_shadow1);
      color.rgb += (contribution - specularPart) * visibility;
      specularColor += specularPart * visibility;
    }
  }
  color.rgb = clamp(color.rgb, 0.0, 1.0);
  gl_FragDepth = coinWindowDepth(gl_FragCoord.z);
  vec4 surface = coinSurfaceColor(gl_FragCoord.xy, color, v_texcoord0,
    v_viewPosition, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7);
  surface.rgb = clamp(surface.rgb + specularColor, 0.0, 1.0);
  gl_FragColor = surface;
}
