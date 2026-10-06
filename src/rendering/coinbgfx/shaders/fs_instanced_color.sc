$input v_color0

#include <bgfx_shader.sh>
#include "coin_depth.sh"

void main()
{
  gl_FragDepth = coinWindowDepth(gl_FragCoord.z);
  gl_FragColor = v_color0;
}
