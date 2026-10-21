$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1

#include <bgfx_shader.sh>
SAMPLER2D(s_multipassScene, 0);
SAMPLER2D(s_multipassPrevious, 1);
uniform vec4 u_multipassInfo; // inverse extent, previous sum enabled, 1/count
void main() {
  vec2 uv = gl_FragCoord.xy * u_multipassInfo.xy;
  vec4 sum = texture2D(s_multipassScene, uv);
  if (u_multipassInfo.z > 0.5) sum += texture2D(s_multipassPrevious, uv);
  gl_FragColor = sum;
}
