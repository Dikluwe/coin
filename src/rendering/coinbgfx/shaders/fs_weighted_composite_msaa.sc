$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1
#include <bgfx_shader.sh>
SAMPLER2DMS(s_oitAccumMS, 0);
SAMPLER2DMS(s_oitRevealMS, 1);
uniform vec4 u_depthInfo;
ivec2 coinWeightedCounts()
{
  uint width, height, a, r;
  s_oitAccumMSTexture.GetDimensions(width, height, a);
  s_oitRevealMSTexture.GetDimensions(width, height, r);
  return ivec2(int(a), int(r));
}
#include "coin_weighted_composite.sh"
#include "coin_weighted_msaa.sh"
