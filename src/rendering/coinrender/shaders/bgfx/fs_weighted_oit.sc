$input v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7

#include <bgfx_shader.sh>
#include "coin_depth.sh"
#include "coin_surface.sh"

void main()
{
  float windowDepth = coinWindowDepth(gl_FragCoord.z);
  gl_FragDepth = windowDepth;
  vec4 color = coinSurfaceColor(gl_FragCoord.xy, v_color0, v_texcoord0, v_viewPosition, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7);
  float alpha = clamp(color.a, 0.0, 1.0);
  if (alpha <= 0.0) discard;
  float depth = windowDepth;
  float alphaWeight = clamp(alpha * 8.0 + 0.01, 0.01, 8.0);
  float depthWeight = clamp(pow(1.0 - depth, 3.0) * 16.0 + 0.1, 0.1, 16.0);
  float weight = alphaWeight * depthWeight;
  gl_FragData[0] = vec4(color.rgb * alpha, alpha) * weight;
  gl_FragData[1] = vec4(alpha);
}
