$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1

#include <bgfx_shader.sh>
#include "coin_depth.sh"
#include "coin_surface.sh"
#include "coin_weighted.sh"

void main()
{
  float windowDepth = coinWindowDepth(v_coinClipDepth.x / v_coinClipDepth.y);
  gl_FragDepth = windowDepth;
  vec4 color = coinSurfaceColor(gl_FragCoord.xy, v_color0, v_texcoord0, v_viewPosition, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1);
  float alpha = clamp(color.a, 0.0, 1.0);
  if (alpha <= 0.0) discard;
  gl_FragData[0] = coinWeightedAccumulation(color, windowDepth);
  gl_FragData[1] = vec4_splat(alpha);
}
