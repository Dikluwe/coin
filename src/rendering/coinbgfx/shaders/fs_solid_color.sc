$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1

#include <bgfx_shader.sh>
#include "coin_depth.sh"
#include "coin_alpha.sh"

uniform vec4 u_screenDoor;

// Qualified without textures, fog, clip planes or effective polygon stipple.
// Preserve SCREEN_DOOR's alpha override before AlphaTest; vertex alpha and
// fixed-function blending otherwise follow the general surface stage.
// Material/PHONG evaluation remains in the unchanged base vertex stage.
void main()
{
  vec4 color = v_color0;
  if (u_screenDoor.w > 0.5) color.a = 1.0;
  coinAlphaTest(color.a);
  gl_FragDepth = coinWindowDepth(v_coinClipDepth.x / v_coinClipDepth.y);
  gl_FragColor = color;
}
