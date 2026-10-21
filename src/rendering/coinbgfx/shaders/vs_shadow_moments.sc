$input a_position
$output v_viewPosition, v_viewNormal, v_coinClipDepth
#include <bgfx_shader.sh>
#include "coin_coverage.sh"
uniform mat4 u_shadowModelView;
uniform mat4 u_shadowClipModelView;
void main()
{
  gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
  gl_Position = coinCanonicalCoverage(gl_Position);
  v_coinClipDepth = coinCapturedDepth(gl_Position);
  gl_Position = coinCanonicalDepthPosition(gl_Position);
  v_viewPosition = mul(u_shadowModelView, vec4(a_position, 1.0)).xyz;
  v_viewNormal = mul(u_shadowClipModelView, vec4(a_position, 1.0)).xyz;
}
