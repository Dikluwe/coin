$input a_position, a_color0, a_color1, a_color2, a_color3, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7
$output v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7

#include <bgfx_shader.sh>
#include "coin_lighting.sh"

void main()
{
  gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0)) * (a_texcoord3.z > 0.0 ? a_texcoord3.z : 1.0);
  v_color0 = coinMaterialColor(a_color0, a_color1, a_color2, a_color3,
                              a_texcoord1, a_texcoord2, a_texcoord3.xy);
  v_ambient = a_color1;
  v_specular = a_color2;
  v_emission = a_color3;
  v_texcoord0 = a_texcoord0;
  v_viewPosition = a_texcoord1;
  v_viewNormal = a_texcoord2;
  v_material = a_texcoord3.xy;
  v_texcoords4 = a_texcoord4;
  v_texcoords5 = a_texcoord5;
  v_texcoords6 = a_texcoord6;
  v_texcoords7 = a_texcoord7;
}
