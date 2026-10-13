$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1
#include <bgfx_shader.sh>
SAMPLER2D(s_layer, 0);
uniform vec4 u_depthInfo;
uniform vec4 u_shadowDiagnostic; // mode, selected map, flip Y, unused
void main()
{
  vec2 uv = gl_FragCoord.xy * u_depthInfo.xy;
  if (u_shadowDiagnostic.z > 0.5) uv.y = 1.0 - uv.y;
  vec2 moments = texture2D(s_layer, uv).xy;
  vec3 value = u_shadowDiagnostic.x < 1.5 ? vec3_splat(moments.x) : vec3(moments, 0.0);
  gl_FragColor = vec4(clamp(value, 0.0, 1.0), 1.0);
}
