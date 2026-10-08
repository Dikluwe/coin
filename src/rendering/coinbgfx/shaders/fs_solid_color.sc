$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1

#include <bgfx_shader.sh>
#include "coin_depth.sh"
#include "coin_alpha.sh"

// Used only after every draw has been qualified without textures, fog, clip
// planes, screen-door transparency or blending. Alpha test remains a per-draw
// surface operation. PHONG remains in the unchanged
// base vertex stage; keep its interface and depth contract exactly.
void main()
{
  coinAlphaTest(v_color0.a);
  gl_FragDepth = coinWindowDepth(v_coinClipDepth.x / v_coinClipDepth.y);
  gl_FragColor = v_color0;
}
