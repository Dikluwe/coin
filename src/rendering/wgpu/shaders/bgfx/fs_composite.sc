$input v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material

#include <bgfx_shader.sh>

SAMPLER2D(s_layer, 0);
uniform vec4 u_depthInfo; // inverse target dimensions in xy

void main()
{
  gl_FragColor = texture2D(s_layer, gl_FragCoord.xy * u_depthInfo.xy);
}
