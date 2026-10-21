$input a_position, a_color0, a_color2, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4
$output v_coinClipDepth, v_color0, v_specular, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_textureQ0
#include <bgfx_shader.sh>
#include "coin_coverage.sh"
void main() {
  gl_Position=mul(u_modelViewProj,vec4(a_position,1.0));
  v_coinClipDepth=coinCapturedDepth(gl_Position);
  v_color0=a_color0;
  v_specular=a_color2;
  v_texcoord0=vec4(a_texcoord0.xyz,a_texcoord3.w);
  v_textureQ0=a_texcoord4;
  v_viewPosition=a_texcoord1.xyz;
  v_viewNormal=a_texcoord2.xyz;
  v_material=a_texcoord3.xy;
}
