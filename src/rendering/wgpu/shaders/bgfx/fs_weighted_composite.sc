$input v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_oitAccum, 0);
SAMPLER2D(s_oitReveal, 1);
uniform vec4 u_depthInfo;

void main()
{
  vec2 uv = gl_FragCoord.xy * u_depthInfo.xy;
  vec4 accum = texture2D(s_oitAccum, uv);
  float opacity = 1.0 - clamp(texture2D(s_oitReveal, uv).x, 0.0, 1.0);
  if (opacity <= 1.0e-5) discard;
  gl_FragColor = vec4(accum.rgb / max(accum.a, 1.0e-5), opacity);
}
