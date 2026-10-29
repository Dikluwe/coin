$input a_position, a_color0, a_color1, a_color2, a_color3, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, a_tangent, a_bitangent
$output v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1

#include <bgfx_shader.sh>


uniform vec4 u_pbrTangentParity;
uniform mat4 u_pbrVertexToView;
uniform mat4 u_pbrModelView;
uniform mat4 u_pbrNormalToView;

void main()
{
  gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0)) * (a_texcoord3.z > 0.0 ? a_texcoord3.z : 1.0);
  // Carry clip depth through the attribute interpolator. Dividing the
  // perspective-interpolated pair recovers affine window depth without the
  // OpenGL rasterizer's gl_FragCoord depth round trip.
#if BGFX_SHADER_LANGUAGE_GLSL
  v_coinClipDepth = vec2(gl_Position.z * 0.5 + gl_Position.w * 0.5, gl_Position.w);
#else
  v_coinClipDepth = gl_Position.zw;
#endif
  v_color0 = a_color0;

  v_emission = a_color3;
  v_texcoord0 = vec3(a_texcoord0, a_texcoord3.w);
  // Use the same operation for full capture and camera reuse. An eye-space
  // round trip would make coplanar clipping depend on the capture camera.
  v_viewPosition = mul(u_pbrModelView, vec4(a_position, 1.0)).xyz;
  vec3 normal = mul(u_pbrNormalToView, vec4(a_texcoord2, 0.0)).xyz;
  v_viewNormal = dot(normal, normal) > 0.0 ? normalize(normal) : vec3(0.0, 0.0, 1.0);
  // Mikk tangents stay in object space in retained buffers. Resolve their
  // orthonormal view frame per vertex, before interpolation, as in pbr-clean.
  vec3 t=mul(u_pbrVertexToView,vec4(a_color1.xyz,0.0)).xyz;
  t-=v_viewNormal*dot(v_viewNormal,t);
  v_ambient=vec4(dot(t,t)>0.0?normalize(t):vec3_splat(0.0),a_color1.w*u_pbrTangentParity.x);
  t=mul(u_pbrVertexToView,vec4(a_color2.xyz,0.0)).xyz;
  t-=v_viewNormal*dot(v_viewNormal,t);
  v_specular=vec4(dot(t,t)>0.0?normalize(t):vec3_splat(0.0),a_color2.w*u_pbrTangentParity.x);

  v_material = a_texcoord3.xy;
  v_texcoords4 = a_texcoord4;
  v_texcoords5 = a_texcoord5;
  v_texcoords6 = a_texcoord6;
  v_texcoords7 = a_texcoord7;
  v_textureQ0 = a_tangent;
  v_textureQ1 = a_bitangent;
}
