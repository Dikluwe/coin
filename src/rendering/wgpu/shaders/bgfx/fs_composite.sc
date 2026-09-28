$input v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7

#include <bgfx_shader.sh>

SAMPLER2D(s_layer, 0);
uniform vec4 u_depthInfo; // inverse target dimensions in xy

void main()
{
  gl_FragColor = texture2D(s_layer, gl_FragCoord.xy * u_depthInfo.xy);
}
