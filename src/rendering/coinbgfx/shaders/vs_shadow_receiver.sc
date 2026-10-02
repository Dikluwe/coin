$input a_position, a_color0, a_color1, a_color2, a_color3, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3
$output v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex
#include <bgfx_shader.sh>
#include "coin_lighting.sh"
uniform vec4 u_shadowLightIndices;
uniform vec4 u_shadowLightIndicesExtra;
uniform vec4 u_shadowQuality;
void main()
{
  gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0)) * (a_texcoord3.z > 0.0 ? a_texcoord3.z : 1.0);
  vec3 color = a_color1.rgb * u_ambientLight.rgb * u_ambientLight.a + a_color3.rgb;
  if (u_lightCount.y > 0.5) color = vec3(0.0);
  if (a_texcoord3.y < 0.5) {
    if (u_lightCount.y < 0.5) color = a_color0.rgb;
  }
  else if (u_shadowQuality.y < 0.5) for (int i = 0; i < 8; ++i) {
    if (float(i) >= u_lightCount.x) break;
    if (abs(float(i) - u_shadowLightIndices.x) < 0.5 ||
        abs(float(i) - u_shadowLightIndices.y) < 0.5 ||
        abs(float(i) - u_shadowLightIndices.z) < 0.5 ||
        abs(float(i) - u_shadowLightIndices.w) < 0.5 ||
        abs(float(i) - u_shadowLightIndicesExtra.x) < 0.5 ||
        abs(float(i) - u_shadowLightIndicesExtra.y) < 0.5 ||
        abs(float(i) - u_shadowLightIndicesExtra.z) < 0.5 ||
        abs(float(i) - u_shadowLightIndicesExtra.w) < 0.5) continue;
    if (u_shadowQuality.z > 0.5)
      color += coinGroupLightContribution(i, a_color0, a_color2, a_texcoord1, a_texcoord2, a_texcoord3.x);
    else color += coinLightContribution(i, a_color0, a_color2, a_texcoord1, a_texcoord2, a_texcoord3.x);
  }
  v_color0 = vec4(clamp(color, 0.0, 1.0), a_color0.a);
  v_specular = a_color2;
  v_texcoord0 = a_texcoord0;
  v_viewPosition = a_texcoord1;
  v_viewNormal = a_texcoord2;
  v_material = a_texcoord3.xy;
  v_texcoords4 = a_color0;
  // Pack eight independent RGB contributions into six unused vec4 varyings.
  // This profile forbids extra scene texture units.
  vec3 c[8];
  for (int slot = 0; slot < 8; ++slot) {
    c[slot] = vec3(0.0);
    float index = slot < 4 ? u_shadowLightIndices[slot] : u_shadowLightIndicesExtra[slot - 4];
    if (u_shadowQuality.x > 0.5 && index >= 0.0)
      c[slot] = coinGroupLightContribution(int(index), a_color0, a_color2,
        a_texcoord1, a_texcoord2, a_texcoord3.x);
  }
  v_ambient = vec4(c[0], c[1].x);
  v_emission = vec4(c[1].yz, c[2].xy);
  v_texcoords5 = vec4(c[2].z, c[3]);
  v_texcoords6 = vec4(c[4], c[5].x);
  v_texcoords7 = vec4(c[5].yz, c[6].xy);
  v_shadowVertex = vec4(c[6].z, c[7]);
}
