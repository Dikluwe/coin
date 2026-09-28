$input v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7

#include <bgfx_shader.sh>

SAMPLER2D(s_oitAccum, 0);
SAMPLER2D(s_oitReveal, 1);
uniform vec4 u_depthInfo;

void main()
{
  vec2 uv = gl_FragCoord.xy * u_depthInfo.xy;
  vec4 accum = texture2D(s_oitAccum, uv);
  float opacity = 1.0 - clamp(texture2D(s_oitReveal, uv).x, 0.0, 1.0);
  if (opacity <= 1.0e-7) discard;
  vec3 averageColor = clamp(accum.rgb / max(accum.a, 1.0e-8), 0.0, 1.0);
  gl_FragColor = vec4(averageColor, opacity);
}
