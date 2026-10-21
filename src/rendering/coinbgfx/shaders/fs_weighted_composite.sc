$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1

#include <bgfx_shader.sh>

SAMPLER2D(s_oitAccum, 0);
SAMPLER2D(s_oitReveal, 1);
uniform vec4 u_depthInfo;

#include "coin_weighted_composite.sh"

void main()
{
  vec2 uv = gl_FragCoord.xy * u_depthInfo.xy;
  vec4 accum = texture2D(s_oitAccum, uv);
  gl_FragColor = coinWeightedComposite(accum, texture2D(s_oitReveal, uv).x);
}
