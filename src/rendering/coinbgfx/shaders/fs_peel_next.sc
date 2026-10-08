$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1

#include <bgfx_shader.sh>
#include "coin_depth.sh"
SAMPLER2D(s_prevDepth, 0);
SAMPLER2D(s_prevColor, 1);
uniform vec4 u_depthInfo;
#include "coin_surface.sh"

void main()
{
  float windowDepth = coinWindowDepth(v_coinClipDepth.x / v_coinClipDepth.y);
  gl_FragDepth = windowDepth;
  vec2 uv = gl_FragCoord.xy * u_depthInfo.xy;
  float previousDepth = texture2D(s_prevDepth, uv).x;
  if (windowDepth <= previousDepth) discard;
  gl_FragColor = coinSurfaceColor(gl_FragCoord.xy, v_color0, v_texcoord0, v_viewPosition, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1);
}
