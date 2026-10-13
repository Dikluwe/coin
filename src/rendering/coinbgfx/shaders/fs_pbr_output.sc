$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1
#include <bgfx_shader.sh>
#include "coin_pbr_output.sh"
SAMPLER2D(s_pbrRadiance, 0);
SAMPLER2D(s_readDepth, 1);
uniform vec4 u_pbrOutput; // xy: inverse target size; z: exposure multiplier.
void main() {
  gl_FragDepth=texture2D(s_readDepth,gl_FragCoord.xy*u_pbrOutput.xy).r;
  vec3 radiance = texture2D(s_pbrRadiance, gl_FragCoord.xy * u_pbrOutput.xy).rgb;
  vec3 color = coinPbrNeutral(radiance * u_pbrOutput.z);
  gl_FragColor = vec4(coinPbrSrgb(color.r), coinPbrSrgb(color.g), coinPbrSrgb(color.b), 1.0);
}
