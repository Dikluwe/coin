$input a_position, a_color0, a_color1, a_color2, a_color3, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, a_tangent, a_bitangent
$output v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex, v_coinClipDepth, v_shadowUV0, v_shadowUV1, v_shadowUV2, v_shadowUV3, v_shadowUV4, v_shadowUV5, v_shadowR
#include <bgfx_shader.sh>
#include "coin_lighting.sh"
#include "coin_coverage.sh"
uniform vec4 u_shadowLightIndices;
uniform vec4 u_shadowLightIndicesExtra;
uniform vec4 u_shadowQuality;
void main()
{
  gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0)) * (a_texcoord3.z > 0.0 ? a_texcoord3.z : 1.0);
  // Fullscreen conversion/composition vertices have no captured W marker.
  if (a_texcoord3.z > 0.0) gl_Position = coinCanonicalCoverage(gl_Position);
  v_coinClipDepth = coinCapturedDepth(gl_Position);
  gl_Position = coinCanonicalDepthPosition(gl_Position);
  vec3 color = a_color1.rgb * u_ambientLight.rgb * u_ambientLight.a + a_color3.rgb;
  if (u_lightCount.y > 0.5) color = vec3_splat(0.0);
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
      color += coinGroupLightContribution(i, a_color0, a_color1, a_color2, a_texcoord1.xyz, a_texcoord2.xyz, a_texcoord3.x);
    else color += coinLightContribution(i, a_color0, a_color1, a_color2, a_texcoord1.xyz, a_texcoord2.xyz, a_texcoord3.x);
  }
  v_color0 = vec4(clamp(color, 0.0, 1.0), a_color0.a);
  v_specular = a_color2;
  v_texcoord0 = vec4(a_texcoord0.xyz, a_texcoord3.w);
  v_viewPosition = a_texcoord1.xyz;
  v_viewNormal = a_texcoord2.xyz;
  v_material = a_texcoord3.xy;
  v_texcoords4 = a_color0;
  // Pack eight independent RGB contributions into six unused vec4 varyings.
  // STQ plus the separately packed R channels retain every spatial unit.
  vec3 c[8];
  for (int slot = 0; slot < 8; ++slot) {
    c[slot] = vec3_splat(0.0);
    int component = slot % 4;
    float index = slot < 4 ? u_shadowLightIndices[component] : u_shadowLightIndicesExtra[component];
    if (u_shadowQuality.x > 0.5 && index >= 0.0)
      c[slot] = coinGroupLightContribution(int(index), a_color0, a_color2,
        a_texcoord1.xyz, a_texcoord2.xyz, a_texcoord3.x);
  }
  v_ambient = vec4(c[0], c[1].x);
  v_emission = vec4(c[1].yz, c[2].xy);
  v_texcoords5 = vec4(c[2].z, c[3]);
  v_texcoords6 = vec4(c[4], c[5].x);
  v_texcoords7 = vec4(c[5].yz, c[6].xy);
  v_shadowVertex = vec4(c[6].z, c[7]);
  v_shadowUV0 = vec4(a_texcoord4.x, a_texcoord4.y, a_tangent.x, a_texcoord4.z);
  v_shadowUV1 = vec4(a_texcoord4.w, a_tangent.y, a_texcoord5.x, a_texcoord5.y);
  v_shadowUV2 = vec4(a_tangent.z, a_texcoord5.z, a_texcoord5.w, a_tangent.w);
  v_shadowUV3 = vec4(a_texcoord6.x, a_texcoord6.y, a_bitangent.x, a_texcoord6.z);
  v_shadowUV4 = vec4(a_texcoord6.w, a_bitangent.y, a_texcoord7.x, a_texcoord7.y);
  v_shadowUV5 = vec4(a_bitangent.z, a_color1.w, a_color2.w, a_color3.w);
  v_shadowR = vec4(a_texcoord1.w, a_texcoord2.w, a_bitangent.w, a_texcoord0.w);
}
