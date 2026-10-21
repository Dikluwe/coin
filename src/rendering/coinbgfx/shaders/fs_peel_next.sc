$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1

#include <bgfx_shader.sh>
#include "coin_depth.sh"
#include "coin_peel_sampling.sh"
uniform vec4 u_peelMeta; // pass index, depth test, function, depth write
#include "coin_surface.sh"

void main()
{
  vec2 sampleClip = coinPeelClip(v_coinClipDepth);
  float windowDepth = coinWindowDepth(sampleClip.x / sampleClip.y);
  gl_FragDepth = windowDepth;
  coinPeelCoverage();
  vec2 uv = gl_FragCoord.xy * u_depthInfo.xy;
  if (u_peelMeta.y > 0.5 &&
      !coinPeelDepthAccepts(windowDepth, coinPeelRead(s_opaqueDepth, uv), u_peelMeta.z)) discard;
  if (u_peelMeta.x > 0.5 && windowDepth <= coinPeelRead(s_prevDepth, uv)) discard;
  gl_FragData[0] = coinSurfaceColor(gl_FragCoord.xy, v_color0, v_texcoord0, v_viewPosition, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1, v_ambient, v_specular);
  gl_FragData[1] = vec4(u_peelMeta.w, 0.0, 0.0, 0.0);
}
