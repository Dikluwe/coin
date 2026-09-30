$input v_viewPosition
#include <bgfx_shader.sh>
uniform vec4 u_shadowDepth;
void main()
{
  float distance = u_shadowDepth.z > 0.5 ? length(v_viewPosition) : -v_viewPosition.z;
  float value = (distance - u_shadowDepth.x) / (u_shadowDepth.y - u_shadowDepth.x);
  gl_FragColor = vec4(value, value * value, 0.0, 0.0);
}
