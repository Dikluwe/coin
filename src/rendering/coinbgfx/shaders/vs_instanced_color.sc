$input a_position, a_normal, i_data0, i_data1, i_data2, i_data3, i_data4, i_data5, i_data6, i_data7, i_data8, i_data9
$output v_color0, v_coinClipDepth

#include <bgfx_shader.sh>
#include "coin_lighting.sh"
#include "coin_coverage.sh"

uniform vec4 u_instancedCamera[6];
#if BGFX_SHADER_LANGUAGE_GLSL
invariant gl_Position;
#endif

void main()
{
  vec4 position = vec4(a_position, 1.0);
  vec3 viewPosition = vec3(dot(position, i_data0), dot(position, i_data1), dot(position, i_data2));
  if (u_rasterViewport.z > 0.0) {
    // Match Coin's explicit float multiply/add order at canonical snap ties.
    // dot() permits reassociation; invariant position retains this sequence.
#if BGFX_SHADER_LANGUAGE_GLSL
    vec3 canonicalPosition = position.x * vec3(i_data0.x, i_data1.x, i_data2.x);
#else
    precise vec3 canonicalPosition = position.x * vec3(i_data0.x, i_data1.x, i_data2.x);
#endif
    canonicalPosition += position.y * vec3(i_data0.y, i_data1.y, i_data2.y);
    canonicalPosition += position.z * vec3(i_data0.z, i_data1.z, i_data2.z);
    canonicalPosition += vec3(i_data0.w, i_data1.w, i_data2.w);
    viewPosition = canonicalPosition;
  }
  vec3 viewNormal = vec3(dot(a_normal, i_data3.xyz), dot(a_normal, i_data4.xyz), dot(a_normal, i_data5.xyz));
  // A nonzero flag applies the current view to world-space instances. The
  // zero-flag profile retains static eye-space instances for LEQUAL depth.
  if (u_instancedCamera[3].w > 0.5) {
    vec4 anchorPosition = vec4(viewPosition, 1.0);
    viewPosition = vec3(dot(anchorPosition, u_instancedCamera[0]),
                        dot(anchorPosition, u_instancedCamera[1]),
                        dot(anchorPosition, u_instancedCamera[2]));
    viewNormal = vec3(dot(viewNormal, u_instancedCamera[3].xyz),
                      dot(viewNormal, u_instancedCamera[4].xyz),
                      dot(viewNormal, u_instancedCamera[5].xyz));
  }
  float normalLength = length(viewNormal);
  viewNormal = normalLength > 0.0 ? viewNormal / normalLength : vec3(0.0, 0.0, 1.0);
  gl_Position = mul(u_modelViewProj, vec4(viewPosition, 1.0));
  gl_Position = coinCanonicalCoverage(gl_Position);
  v_coinClipDepth = coinCapturedDepth(gl_Position);
  gl_Position = coinCanonicalDepthPosition(gl_Position);
  v_color0 = coinMaterialColor(i_data6, i_data7, i_data8, i_data9,
                              viewPosition, viewNormal, vec2(i_data3.w, i_data4.w));
}
