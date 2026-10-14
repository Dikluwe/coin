$input v_viewPosition, v_viewNormal
#include <bgfx_shader.sh>
uniform vec4 u_shadowDepth;
uniform vec4 u_clipMeta;
uniform vec4 u_clipPlanes[8];
void main()
{
  for (int i = 0; i < 8; ++i) {
    if (float(i) >= u_clipMeta.x) break;
    if (dot(u_clipPlanes[i], vec4(v_viewNormal, 1.0)) < 0.0) discard;
  }
  float distance = u_shadowDepth.z > 0.5 ? length(v_viewPosition) : -v_viewPosition.z;
  float value = (distance - u_shadowDepth.x) / (u_shadowDepth.y - u_shadowDepth.x);
  gl_FragColor = vec4(value, value * value, 0.0, 0.0);
}
