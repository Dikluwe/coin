$input a_position
$output v_viewPosition
#include <bgfx_shader.sh>
uniform mat4 u_shadowModelView;
void main()
{
  gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
  v_viewPosition = mul(u_shadowModelView, vec4(a_position, 1.0)).xyz;
}
