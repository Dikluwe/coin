$input v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7

#include <bgfx_shader.sh>
#include "coin_depth.sh"
#include "coin_surface.sh"

void main()
{
  gl_FragDepth = coinWindowDepth(gl_FragCoord.z);
  gl_FragColor = coinSurfaceColor(gl_FragCoord.xy, v_color0, v_texcoord0, v_viewPosition, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7);
}
