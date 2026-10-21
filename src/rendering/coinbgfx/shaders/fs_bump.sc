$input v_coinClipDepth, v_color0, v_specular, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_textureQ0
#include <bgfx_shader.sh>
#include "coin_depth.sh"
#ifdef COIN_SURFACE_SAMPLE_DEPTH
#include "coin_sample_depth.sh"
#endif
SAMPLER2D(s_bumpImage,0);
SAMPLER2D(s_bumpBase,1);
uniform vec4 u_bumpParams; // base enabled, specular pass
vec3 coinBumpNormalize(vec3 v) {
  float square=dot(v,v);
  return square>0.0 ? v*inversesqrt(square) : vec3_splat(0.0);
}
void main() {
#ifdef COIN_SURFACE_SAMPLE_DEPTH
  vec2 sampleClip=coinSurfaceSampleClip(v_coinClipDepth);
  gl_FragDepth=coinWindowDepth(sampleClip.x/sampleClip.y);
#elif !COIN_NATIVE_SURFACE_DEPTH
  gl_FragDepth=coinWindowDepth(v_coinClipDepth.x/v_coinClipDepth.y);
#endif
  vec3 normal=texture2D(s_bumpImage,v_textureQ0.xy/v_textureQ0.w).rgb*2.0-1.0;
  if(u_bumpParams.y>0.5) {
    vec3 halfVector=coinBumpNormalize(v_viewPosition+v_viewNormal);
    float specular=pow(clamp(dot(normal,halfVector),0.0,1.0),v_material.x*64.0);
    gl_FragColor=vec4(v_specular.rgb*specular,specular);
  } else {
    float diffuse=clamp(dot(normal,coinBumpNormalize(v_viewPosition)),0.0,1.0);
    vec3 base=vec3_splat(1.0);
    if(u_bumpParams.x>0.5)base=texture2D(s_bumpBase,v_texcoord0.xy/v_texcoord0.w).rgb;
    gl_FragColor=vec4(v_color0.rgb*base*diffuse,1.0);
  }
}
