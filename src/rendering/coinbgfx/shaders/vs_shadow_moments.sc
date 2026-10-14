$input a_position
$output v_viewPosition, v_viewNormal
#include <bgfx_shader.sh>
uniform mat4 u_shadowModelView;
uniform mat4 u_shadowClipModelView;
void main()
{
  gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
  v_viewPosition = mul(u_shadowModelView, vec4(a_position, 1.0)).xyz;
  v_viewNormal = mul(u_shadowClipModelView, vec4(a_position, 1.0)).xyz;
}
