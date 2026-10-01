$input a_position, a_color0, a_color1, a_color2, a_color3, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3
$output v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7
#include <bgfx_shader.sh>
#include "coin_lighting.sh"
uniform vec4 u_shadowLightIndices;
void main()
{
  gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0)) * (a_texcoord3.z > 0.0 ? a_texcoord3.z : 1.0);
  vec3 color = a_color1.rgb * u_ambientLight.rgb * u_ambientLight.a + a_color3.rgb;
  if (u_lightCount.y > 0.5) color = vec3(0.0);
  else if (a_texcoord3.y < 0.5) color = a_color0.rgb;
  else for (int i = 0; i < 8; ++i) {
    if (float(i) >= u_lightCount.x) break;
    if (abs(float(i) - u_shadowLightIndices.x) < 0.5 ||
        abs(float(i) - u_shadowLightIndices.y) < 0.5 ||
        abs(float(i) - u_shadowLightIndices.z) < 0.5 ||
        abs(float(i) - u_shadowLightIndices.w) < 0.5) continue;
    color += coinLightContribution(i, a_color0, a_color2, a_texcoord1,
                                   a_texcoord2, a_texcoord3.x);
  }
  v_color0 = vec4(clamp(color, 0.0, 1.0), a_color0.a);
  v_ambient = a_color1;
  v_specular = a_color2;
  v_emission = a_color3;
  v_texcoord0 = a_texcoord0;
  v_viewPosition = a_texcoord1;
  v_viewNormal = a_texcoord2;
  v_material = a_texcoord3.xy;
  v_texcoords4 = a_color0;
  v_texcoords5 = vec4(0.0);
  v_texcoords6 = vec4(0.0);
  v_texcoords7 = vec4(0.0);
}
