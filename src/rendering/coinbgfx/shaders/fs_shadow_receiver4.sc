$input v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex
#include <bgfx_shader.sh>
#include "coin_depth.sh"
#include "coin_lighting.sh"
#define COIN_SHADOW_SURFACE 1
#include "coin_surface.sh"
#ifdef COIN_SHADOW_PEEL
SAMPLER2D(s_prevDepth, 0);
uniform vec4 u_depthInfo;
#endif
#ifdef COIN_SHADOW_OIT
#include "coin_weighted.sh"
#endif
SAMPLER2D(s_shadow0, 8);
SAMPLER2D(s_shadow1, 9);
SAMPLER2D(s_shadow3, 11);
SAMPLER2D(s_shadow2, 10);
uniform mat4 u_shadowViewToClip0;
uniform mat4 u_shadowViewToClip1;
uniform mat4 u_shadowViewToClip3;
uniform mat4 u_shadowViewToClip2;
uniform mat4 u_shadowViewToLight0;
uniform mat4 u_shadowViewToLight1;
uniform mat4 u_shadowViewToLight3;
uniform mat4 u_shadowViewToLight2;
uniform vec4 u_shadowParams0;
uniform vec4 u_shadowParams1;
uniform vec4 u_shadowParams3;
uniform vec4 u_shadowParams2;
uniform vec4 u_shadowMeta0;
uniform vec4 u_shadowMeta1;
uniform vec4 u_shadowMeta3;
uniform vec4 u_shadowMeta2;
uniform vec4 u_shadowLightIndices;
uniform vec4 u_shadowLightIndicesExtra;
#include "coin_shadow_quality.sh"

#ifdef COIN_SHADOW_EIGHT
SAMPLER2D(s_shadow4, 12);
uniform mat4 u_shadowViewToClip4;
uniform mat4 u_shadowViewToLight4;
uniform vec4 u_shadowParams4;
uniform vec4 u_shadowMeta4;
SAMPLER2D(s_shadow5, 13);
uniform mat4 u_shadowViewToClip5;
uniform mat4 u_shadowViewToLight5;
uniform vec4 u_shadowParams5;
uniform vec4 u_shadowMeta5;
SAMPLER2D(s_shadow6, 14);
uniform mat4 u_shadowViewToClip6;
uniform mat4 u_shadowViewToLight6;
uniform vec4 u_shadowParams6;
uniform vec4 u_shadowMeta6;
SAMPLER2D(s_shadow7, 15);
uniform mat4 u_shadowViewToClip7;
uniform mat4 u_shadowViewToLight7;
uniform vec4 u_shadowParams7;
uniform vec4 u_shadowMeta7;
#endif
#include "coin_shadow_visibility.sh"
uniform vec4 u_shadowDiagnostic;
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
    if (u_shadowLightIndices.z >= 0.0) {
      int index = int(u_shadowLightIndices.z);
      vec3 contribution = coinShadowContribution(index,
        coinShadowVertexColor(2, v_ambient, v_emission, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex),
        v_texcoords4, v_specular, v_viewPosition, v_viewNormal, v_material.x);
      vec3 specularPart = u_shadowQuality.x > 0.5 ? vec3_splat(0.0) :
        coinGroupLightContribution(index, vec4_splat(0.0), v_specular, v_viewPosition, v_viewNormal, v_material.x);
      float visibility = coinShadowVisibility(v_viewPosition,
        u_shadowViewToClip2, u_shadowViewToLight2, u_shadowParams2,
        u_shadowMeta2, s_shadow2);
      color.rgb += (contribution - specularPart) * visibility;
      specularColor += specularPart * visibility;
    }
    if (u_shadowLightIndices.w >= 0.0) {
      int index = int(u_shadowLightIndices.w);
      vec3 contribution = coinShadowContribution(index,
        coinShadowVertexColor(3, v_ambient, v_emission, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex),
        v_texcoords4, v_specular, v_viewPosition, v_viewNormal, v_material.x);
      vec3 specularPart = u_shadowQuality.x > 0.5 ? vec3_splat(0.0) :
        coinGroupLightContribution(index, vec4_splat(0.0), v_specular, v_viewPosition, v_viewNormal, v_material.x);
      float visibility = coinShadowVisibility(v_viewPosition,
        u_shadowViewToClip3, u_shadowViewToLight3, u_shadowParams3,
        u_shadowMeta3, s_shadow3);
      color.rgb += (contribution - specularPart) * visibility;
      specularColor += specularPart * visibility;
    }

#ifdef COIN_SHADOW_EIGHT
    if (u_shadowLightIndicesExtra.x >= 0.0) {
      int index = int(u_shadowLightIndicesExtra.x);
      vec3 contribution = coinShadowContribution(index,
        coinShadowVertexColor(4, v_ambient, v_emission, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex),
        v_texcoords4, v_specular, v_viewPosition, v_viewNormal, v_material.x);
      vec3 specularPart = u_shadowQuality.x > 0.5 ? vec3_splat(0.0) :
        coinGroupLightContribution(index, vec4_splat(0.0), v_specular, v_viewPosition, v_viewNormal, v_material.x);
      float visibility = coinShadowVisibility(v_viewPosition,
        u_shadowViewToClip4, u_shadowViewToLight4, u_shadowParams4,
        u_shadowMeta4, s_shadow4);
      color.rgb += (contribution - specularPart) * visibility;
      specularColor += specularPart * visibility;
    }
    if (u_shadowLightIndicesExtra.y >= 0.0) {
      int index = int(u_shadowLightIndicesExtra.y);
      vec3 contribution = coinShadowContribution(index,
        coinShadowVertexColor(5, v_ambient, v_emission, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex),
        v_texcoords4, v_specular, v_viewPosition, v_viewNormal, v_material.x);
      vec3 specularPart = u_shadowQuality.x > 0.5 ? vec3_splat(0.0) :
        coinGroupLightContribution(index, vec4_splat(0.0), v_specular, v_viewPosition, v_viewNormal, v_material.x);
      float visibility = coinShadowVisibility(v_viewPosition,
        u_shadowViewToClip5, u_shadowViewToLight5, u_shadowParams5,
        u_shadowMeta5, s_shadow5);
      color.rgb += (contribution - specularPart) * visibility;
      specularColor += specularPart * visibility;
    }
    if (u_shadowLightIndicesExtra.z >= 0.0) {
      int index = int(u_shadowLightIndicesExtra.z);
      vec3 contribution = coinShadowContribution(index,
        coinShadowVertexColor(6, v_ambient, v_emission, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex),
        v_texcoords4, v_specular, v_viewPosition, v_viewNormal, v_material.x);
      vec3 specularPart = u_shadowQuality.x > 0.5 ? vec3_splat(0.0) :
        coinGroupLightContribution(index, vec4_splat(0.0), v_specular, v_viewPosition, v_viewNormal, v_material.x);
      float visibility = coinShadowVisibility(v_viewPosition,
        u_shadowViewToClip6, u_shadowViewToLight6, u_shadowParams6,
        u_shadowMeta6, s_shadow6);
      color.rgb += (contribution - specularPart) * visibility;
      specularColor += specularPart * visibility;
    }
    if (u_shadowLightIndicesExtra.w >= 0.0) {
      int index = int(u_shadowLightIndicesExtra.w);
      vec3 contribution = coinShadowContribution(index,
        coinShadowVertexColor(7, v_ambient, v_emission, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex),
        v_texcoords4, v_specular, v_viewPosition, v_viewNormal, v_material.x);
      vec3 specularPart = u_shadowQuality.x > 0.5 ? vec3_splat(0.0) :
        coinGroupLightContribution(index, vec4_splat(0.0), v_specular, v_viewPosition, v_viewNormal, v_material.x);
      float visibility = coinShadowVisibility(v_viewPosition,
        u_shadowViewToClip7, u_shadowViewToLight7, u_shadowParams7,
        u_shadowMeta7, s_shadow7);
      color.rgb += (contribution - specularPart) * visibility;
      specularColor += specularPart * visibility;
    }
#endif
  }
  color.rgb = clamp(color.rgb, 0.0, 1.0);
  float depth = coinWindowDepth(gl_FragCoord.z);
  gl_FragDepth = depth;
#ifdef COIN_SHADOW_PEEL
  if (depth <= texture2D(s_prevDepth, gl_FragCoord.xy * u_depthInfo.xy).x) discard;
#endif
  vec4 surface = coinSurfaceColor(gl_FragCoord.xy, color, v_texcoord0,
    v_viewPosition, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, vec4_splat(1.0), vec4_splat(1.0));
  surface.rgb = clamp(surface.rgb + specularColor, 0.0, 1.0);
  if (u_shadowDiagnostic.x > 2.5) {
    float visibility = 1.0;
    if (abs(u_shadowDiagnostic.y - 0.0) < 0.5 && u_shadowLightIndices.x >= 0.0)
      visibility = coinShadowVisibility(v_viewPosition, u_shadowViewToClip0,
        u_shadowViewToLight0, u_shadowParams0, u_shadowMeta0, s_shadow0);
    if (abs(u_shadowDiagnostic.y - 1.0) < 0.5 && u_shadowLightIndices.y >= 0.0)
      visibility = coinShadowVisibility(v_viewPosition, u_shadowViewToClip1,
        u_shadowViewToLight1, u_shadowParams1, u_shadowMeta1, s_shadow1);
    if (abs(u_shadowDiagnostic.y - 2.0) < 0.5 && u_shadowLightIndices.z >= 0.0)
      visibility = coinShadowVisibility(v_viewPosition, u_shadowViewToClip2,
        u_shadowViewToLight2, u_shadowParams2, u_shadowMeta2, s_shadow2);
    if (abs(u_shadowDiagnostic.y - 3.0) < 0.5 && u_shadowLightIndices.w >= 0.0)
      visibility = coinShadowVisibility(v_viewPosition, u_shadowViewToClip3,
        u_shadowViewToLight3, u_shadowParams3, u_shadowMeta3, s_shadow3);
#ifdef COIN_SHADOW_EIGHT
    if (abs(u_shadowDiagnostic.y - 4.0) < 0.5 && u_shadowLightIndicesExtra.x >= 0.0)
      visibility = coinShadowVisibility(v_viewPosition, u_shadowViewToClip4,
        u_shadowViewToLight4, u_shadowParams4, u_shadowMeta4, s_shadow4);
    if (abs(u_shadowDiagnostic.y - 5.0) < 0.5 && u_shadowLightIndicesExtra.y >= 0.0)
      visibility = coinShadowVisibility(v_viewPosition, u_shadowViewToClip5,
        u_shadowViewToLight5, u_shadowParams5, u_shadowMeta5, s_shadow5);
    if (abs(u_shadowDiagnostic.y - 6.0) < 0.5 && u_shadowLightIndicesExtra.z >= 0.0)
      visibility = coinShadowVisibility(v_viewPosition, u_shadowViewToClip6,
        u_shadowViewToLight6, u_shadowParams6, u_shadowMeta6, s_shadow6);
    if (abs(u_shadowDiagnostic.y - 7.0) < 0.5 && u_shadowLightIndicesExtra.w >= 0.0)
      visibility = coinShadowVisibility(v_viewPosition, u_shadowViewToClip7,
        u_shadowViewToLight7, u_shadowParams7, u_shadowMeta7, s_shadow7);
#endif
    surface = vec4(vec3_splat(visibility), 1.0);
  }
#ifdef COIN_SHADOW_OIT
  if (surface.a <= 0.0) discard;
  gl_FragData[0] = coinWeightedAccumulation(surface, depth);
  gl_FragData[1] = vec4_splat(clamp(surface.a, 0.0, 1.0));
#else
  gl_FragColor = surface;
#endif
}
