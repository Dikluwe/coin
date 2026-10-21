$input a_position, a_color0, a_color1, a_color2, a_color3, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, a_tangent, a_bitangent
$output v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1

#include <bgfx_shader.sh>
#include "coin_lighting.sh"
#include "coin_coverage.sh"

void main()
{
  gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0)) * (a_texcoord3.z > 0.0 ? a_texcoord3.z : 1.0);
  // Fullscreen conversion/composition vertices have no captured W marker.
  if (a_texcoord3.z > 0.0) gl_Position = coinCanonicalCoverage(gl_Position);
  // Carry clip depth through the attribute interpolator. Dividing the
  // perspective-interpolated pair recovers affine window depth without the
  // OpenGL rasterizer's gl_FragCoord depth round trip.
#if BGFX_SHADER_LANGUAGE_GLSL
  v_coinClipDepth = vec2(gl_Position.z * 0.5 + gl_Position.w * 0.5, gl_Position.w);
#else
  v_coinClipDepth = gl_Position.zw;
#endif
  if (a_texcoord3.z > 0.0) gl_Position = coinCanonicalDepthPosition(gl_Position);
  v_color0 = coinMaterialColor(a_color0, a_color1, a_color2, a_color3,
                              a_texcoord1.xyz, a_texcoord2.xyz, a_texcoord3.xy);
  // The base surface stage needs the raw R coordinates, while lighting above
  // has already consumed the material attributes. Shadow receivers use a
  // separate vertex stage and retain their original contribution channels.
  v_ambient = vec4(a_color1.w, a_color2.w, a_color3.w, a_texcoord1.w);
  v_specular = vec4(a_texcoord2.w, a_bitangent.w, a_texcoord0.w, 0.0);
  v_emission = a_color3;
  v_texcoord0 = vec4(a_texcoord0.xyz, a_texcoord3.w);
  v_viewPosition = a_texcoord1.xyz;
  v_viewNormal = a_texcoord2.xyz;
  v_material = a_texcoord3.xy;
  v_texcoords4 = a_texcoord4;
  v_texcoords5 = a_texcoord5;
  v_texcoords6 = a_texcoord6;
  v_texcoords7 = a_texcoord7;
  v_textureQ0 = a_tangent;
  v_textureQ1 = a_bitangent;
}
