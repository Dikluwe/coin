$input a_position, a_color0, a_color1, a_color2, a_color3, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, a_tangent, a_bitangent
$output v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex, v_coinClipDepth, v_shadowUV0, v_shadowUV1, v_shadowUV2, v_shadowUV3, v_shadowUV4, v_shadowUV5, v_shadowR
#include <bgfx_shader.sh>
#include "coin_lighting.sh"
#include "coin_coverage.sh"
uniform vec4 u_shadowLightIndices;
uniform vec4 u_shadowLightIndicesExtra;
uniform vec4 u_shadowQuality;
uniform vec4 u_coinBumpLightMeta;
uniform vec4 u_coinBumpLightVectors[8];
uniform vec4 u_coinBumpEye;
uniform mat4 u_coinBumpModelView;
vec3 coinBumpNormalize(vec3 v) { float d=dot(v,v);return d>0.0?v*inversesqrt(d):vec3_splat(0.0); }
void main()
{
  gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0)) * (a_texcoord3.z > 0.0 ? a_texcoord3.z : 1.0);
  // Fullscreen conversion/composition vertices have no captured W marker.
  if (a_texcoord3.z > 0.0) gl_Position = coinCanonicalCoverage(gl_Position);
  v_coinClipDepth = coinCapturedDepth(gl_Position);
  gl_Position = coinCanonicalDepthPosition(gl_Position);
  v_color0=a_color0;
  v_specular=a_color2;
  v_texcoord0=vec4(a_texcoord0.xyz,a_texcoord3.w);
  v_viewPosition=a_texcoord1.xyz;
  v_viewNormal=a_texcoord2.xyz;
  v_material=a_texcoord3.xy;
  v_texcoords4=a_color0;
  v_ambient=a_color1;
  v_emission=a_color3;
  v_texcoords5=vec4_splat(0.0);
  v_texcoords6=vec4_splat(0.0);
  v_texcoords7=vec4_splat(0.0);
  v_shadowVertex=vec4_splat(0.0);
  v_shadowUV0 = vec4(a_texcoord4.x, a_texcoord4.y, a_tangent.x, a_texcoord4.z);
  v_shadowUV1 = vec4(a_texcoord4.w, a_tangent.y, a_texcoord5.x, a_texcoord5.y);
  v_shadowUV2 = vec4(a_tangent.z, a_texcoord5.z, a_texcoord5.w, a_tangent.w);
  v_shadowUV3 = vec4(a_texcoord6.x, a_texcoord6.y, a_bitangent.x, a_texcoord6.z);
  v_shadowUV4 = vec4(a_texcoord6.w, a_bitangent.y, a_texcoord7.x, a_texcoord7.y);
  v_shadowUV5 = vec4(a_bitangent.z, a_color1.w, a_color2.w, a_color3.w);
  v_shadowR = vec4(a_texcoord1.w, a_texcoord2.w, a_bitangent.w, a_texcoord0.w);
  // Multi-light input reuses the RGB lanes of S/T/N; every texture W/R/Q
  // lane keeps its existing packing. Project each L and E before interpolation.
  if (u_coinBumpLightMeta.x>1.5) {
    vec3 basisS=a_color1.xyz,basisT=a_color3.xyz,basisN=a_texcoord1.xyz;
    vec3 eye=coinBumpNormalize(u_coinBumpEye.xyz-a_position);
    v_viewPosition=mul(u_coinBumpModelView,vec4(a_position,1.0)).xyz;
    v_texcoords4=vec4(dot(basisS,eye),dot(basisT,eye),dot(basisN,eye),0.0);
    vec3 l[8];
    for(int i=0;i<8;++i) {
      vec3 direction=u_coinBumpLightVectors[i].xyz;
      if(u_coinBumpLightVectors[i].w>0.5)
        direction=coinBumpNormalize(direction-a_position);
      l[i]=vec3(dot(basisS,direction),dot(basisT,direction),dot(basisN,direction));
    }
    v_ambient=vec4(l[0],l[6].x);
    v_emission=vec4(l[1],l[6].y);
    v_texcoords4.w=l[6].z;
    v_texcoords5=vec4(l[2],l[7].x);
    v_texcoords6=vec4(l[3],l[7].y);
    v_texcoords7=vec4(l[4],l[7].z);
    v_shadowVertex=vec4(l[5],0.0);
  }
}
