// Per-light visibility applies only to direct GGX radiance.
SAMPLER2D(s_shadow0, 8);
uniform mat4 u_shadowViewToClip0;
uniform mat4 u_shadowViewToLight0;
uniform vec4 u_shadowParams0;
uniform vec4 u_shadowMeta0;
SAMPLER2D(s_shadow1, 9);
uniform mat4 u_shadowViewToClip1;
uniform mat4 u_shadowViewToLight1;
uniform vec4 u_shadowParams1;
uniform vec4 u_shadowMeta1;
SAMPLER2D(s_shadow2, 10);
uniform mat4 u_shadowViewToClip2;
uniform mat4 u_shadowViewToLight2;
uniform vec4 u_shadowParams2;
uniform vec4 u_shadowMeta2;
SAMPLER2D(s_shadow3, 11);
uniform mat4 u_shadowViewToClip3;
uniform mat4 u_shadowViewToLight3;
uniform vec4 u_shadowParams3;
uniform vec4 u_shadowMeta3;
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
uniform vec4 u_shadowLightIndices;
uniform vec4 u_shadowLightIndicesExtra;
uniform vec4 u_shadowDiagnostic;
#include "coin_shadow_visibility.sh"
float coinPbrVisibility(int lightIndex, vec3 position) {
  float visibility = 1.0;
  if (u_shadowLightIndices.x >= 0.0 && abs(u_shadowLightIndices.x - float(lightIndex)) < 0.5)
    visibility *= coinShadowVisibility(position, u_shadowViewToClip0, u_shadowViewToLight0, u_shadowParams0, u_shadowMeta0, s_shadow0);
  if (u_shadowLightIndices.y >= 0.0 && abs(u_shadowLightIndices.y - float(lightIndex)) < 0.5)
    visibility *= coinShadowVisibility(position, u_shadowViewToClip1, u_shadowViewToLight1, u_shadowParams1, u_shadowMeta1, s_shadow1);
  if (u_shadowLightIndices.z >= 0.0 && abs(u_shadowLightIndices.z - float(lightIndex)) < 0.5)
    visibility *= coinShadowVisibility(position, u_shadowViewToClip2, u_shadowViewToLight2, u_shadowParams2, u_shadowMeta2, s_shadow2);
  if (u_shadowLightIndices.w >= 0.0 && abs(u_shadowLightIndices.w - float(lightIndex)) < 0.5)
    visibility *= coinShadowVisibility(position, u_shadowViewToClip3, u_shadowViewToLight3, u_shadowParams3, u_shadowMeta3, s_shadow3);
  if (u_shadowLightIndicesExtra.x >= 0.0 && abs(u_shadowLightIndicesExtra.x - float(lightIndex)) < 0.5)
    visibility *= coinShadowVisibility(position, u_shadowViewToClip4, u_shadowViewToLight4, u_shadowParams4, u_shadowMeta4, s_shadow4);
  if (u_shadowLightIndicesExtra.y >= 0.0 && abs(u_shadowLightIndicesExtra.y - float(lightIndex)) < 0.5)
    visibility *= coinShadowVisibility(position, u_shadowViewToClip5, u_shadowViewToLight5, u_shadowParams5, u_shadowMeta5, s_shadow5);
  if (u_shadowLightIndicesExtra.z >= 0.0 && abs(u_shadowLightIndicesExtra.z - float(lightIndex)) < 0.5)
    visibility *= coinShadowVisibility(position, u_shadowViewToClip6, u_shadowViewToLight6, u_shadowParams6, u_shadowMeta6, s_shadow6);
  if (u_shadowLightIndicesExtra.w >= 0.0 && abs(u_shadowLightIndicesExtra.w - float(lightIndex)) < 0.5)
    visibility *= coinShadowVisibility(position, u_shadowViewToClip7, u_shadowViewToLight7, u_shadowParams7, u_shadowMeta7, s_shadow7);
  return visibility;
}
