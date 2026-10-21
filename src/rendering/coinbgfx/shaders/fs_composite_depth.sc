$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1

#include <bgfx_shader.sh>
SAMPLER2D(s_layerMask, 0);
SAMPLER2D(s_layerDepth, 1);
uniform vec4 u_depthInfo;

void main()
{
  vec2 uv = gl_FragCoord.xy * u_depthInfo.xy;
  if (texture2D(s_layerMask, uv).x < 0.5) discard;
  gl_FragDepth = texture2D(s_layerDepth, uv).x;
  gl_FragColor = vec4_splat(0.0);
}
