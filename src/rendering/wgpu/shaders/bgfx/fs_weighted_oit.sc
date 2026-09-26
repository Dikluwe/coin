$input v_color0

#include <bgfx_shader.sh>

void main()
{
  vec4 color = v_color0;
  float alpha = clamp(color.a, 0.0, 1.0);
  if (alpha <= 0.0) discard;

  // McGuire/Bavoil weighted blended OIT. The bounded depth weight keeps
  // nearby fragments dominant without requiring the application to sort.
  float depth = clamp(gl_FragCoord.z, 0.0, 1.0);
  float weight = alpha * clamp(pow(1.0 - depth, 3.0) * 3.0e3, 1.0e-2, 3.0e3);
  gl_FragData[0] = vec4(color.rgb * alpha, alpha) * weight;
  gl_FragData[1] = vec4(alpha);
}
