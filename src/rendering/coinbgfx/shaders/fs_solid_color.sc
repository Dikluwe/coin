$input v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7

#include <bgfx_shader.sh>
#include "coin_depth.sh"

// Used only after every draw has been qualified without textures, fog, clip
// planes, screen-door transparency or blending. PHONG remains in the unchanged
// base vertex stage; keep its interface and depth contract exactly.
void main()
{
  gl_FragDepth = coinWindowDepth(gl_FragCoord.z);
  gl_FragColor = v_color0;
}
