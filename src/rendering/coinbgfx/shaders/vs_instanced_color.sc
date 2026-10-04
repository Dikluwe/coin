$input a_position, a_normal, i_data0, i_data1, i_data2, i_data3, i_data4, i_data5, i_data6, i_data7, i_data8, i_data9
$output v_color0

#include <bgfx_shader.sh>
#include "coin_lighting.sh"

uniform vec4 u_instancedCamera[6];

void main()
{
  vec4 position = vec4(a_position, 1.0);
  vec3 viewPosition = vec3(dot(position, i_data0), dot(position, i_data1), dot(position, i_data2));
  vec3 viewNormal = vec3(dot(a_normal, i_data3.xyz), dot(a_normal, i_data4.xyz), dot(a_normal, i_data5.xyz));
  // Zero flag retains the original static path. Instances stay anchored in
  // their first eye space; a camera patch changes only these draw uniforms.
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
  v_color0 = coinMaterialColor(i_data6, i_data7, i_data8, i_data9,
                              viewPosition, viewNormal, vec2(i_data3.w, i_data4.w));
}
