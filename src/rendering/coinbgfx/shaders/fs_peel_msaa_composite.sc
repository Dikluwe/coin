$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1
#include <bgfx_shader.sh>
SAMPLER2DMS(s_peelColorMS, 0);
SAMPLER2DMS(s_peelMaskMS, 1);
SAMPLER2DMS(s_peelDepthMS, 2);
uniform vec4 u_depthInfo;
ivec3 coinPeelCounts()
{
  uint width, height, a, m, d;
  s_peelColorMSTexture.GetDimensions(width, height, a);
  s_peelMaskMSTexture.GetDimensions(width, height, m);
  s_peelDepthMSTexture.GetDimensions(width, height, d);
  return ivec3(int(a), int(m), int(d));
}
#include "coin_peel_msaa_composite.sh"
