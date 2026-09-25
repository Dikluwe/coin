$input v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_prevDepth, 0);
SAMPLER2D(s_prevColor, 1);
uniform vec4 u_depthInfo; // inverse target dimensions in xy

void main()
{
  vec2 uv = gl_FragCoord.xy * u_depthInfo.xy;
  vec4 previous = texture2D(s_prevColor, uv);
  if (previous.a <= 0.0) discard;
  float previousDepth = texture2D(s_prevDepth, uv).x;
  if (gl_FragCoord.z <= previousDepth + 0.00001) discard;
  gl_FragColor = v_color0;
}
