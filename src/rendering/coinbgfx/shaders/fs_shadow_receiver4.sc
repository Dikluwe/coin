#ifdef COIN_SHADOW_MULTITEXTURE
$input v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex, v_coinClipDepth, v_shadowUV0, v_shadowUV1, v_shadowUV2, v_shadowUV3, v_shadowUV4, v_shadowUV5, v_shadowR
#else
$input v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex, v_coinClipDepth
#endif
#include <bgfx_shader.sh>
#include "coin_depth.sh"
#include "coin_lighting.sh"
#define COIN_SHADOW_SURFACE 1
#include "coin_surface.sh"
#ifdef COIN_SURFACE_SAMPLE_DEPTH
#include "coin_sample_depth.sh"
#endif
#ifdef COIN_SHADOW_PEEL
#include "coin_peel_sampling.sh"
uniform vec4 u_peelMeta;
#endif
#ifdef COIN_SHADOW_OIT
#include "coin_weighted.sh"
#endif
#ifdef COIN_SHADOW_MULTITEXTURE
SAMPLER2DARRAY(s_shadowArray, 10);
#define s_shadow0 s_shadowArray
#define s_shadow1 s_shadowArray
#define s_shadow2 s_shadowArray
#define s_shadow3 s_shadowArray
#define s_shadow4 s_shadowArray
#define s_shadow5 s_shadowArray
#define s_shadow6 s_shadowArray
#define s_shadow7 s_shadowArray
#define COIN_MOMENT_SAMPLER sampler2DArray
#define COIN_MOMENT_LOAD(_sampler,_pixel,_texel) texture2DArray(_sampler,vec3((_pixel + vec2(0.5,0.5))*_texel.z,_texel.w))
#else
SAMPLER2D(s_shadow0, 8);
SAMPLER2D(s_shadow1, 9);
SAMPLER2D(s_shadow2, 10);
SAMPLER2D(s_shadow3, 11);
#define COIN_MOMENT_SAMPLER sampler2D
#define COIN_MOMENT_LOAD(_sampler,_pixel,_texel) texture2D(_sampler,(_pixel + vec2(0.5,0.5))*_texel.xy)
#endif
uniform mat4 u_shadowViewToClip0;
uniform mat4 u_shadowViewToClip1;
uniform mat4 u_shadowViewToClip3;
uniform mat4 u_shadowViewToClip2;
uniform mat4 u_shadowViewToLight0;
uniform mat4 u_shadowViewToLight1;
uniform mat4 u_shadowViewToLight3;
uniform mat4 u_shadowViewToLight2;
uniform vec4 u_shadowParams0;
uniform vec4 u_shadowParams1;
uniform vec4 u_shadowParams3;
uniform vec4 u_shadowParams2;
uniform vec4 u_shadowMeta0;
uniform vec4 u_shadowTexel0;
uniform vec4 u_shadowMeta1;
uniform vec4 u_shadowTexel1;
uniform vec4 u_shadowMeta3;
uniform vec4 u_shadowTexel3;
uniform vec4 u_shadowMeta2;
uniform vec4 u_shadowTexel2;
uniform vec4 u_shadowLightIndices;
uniform vec4 u_shadowLightIndicesExtra;
#include "coin_shadow_quality.sh"

#ifdef COIN_SHADOW_EIGHT
#ifndef COIN_SHADOW_MULTITEXTURE
SAMPLER2D(s_shadow4, 12);
#endif
uniform mat4 u_shadowViewToClip4;
uniform mat4 u_shadowViewToLight4;
uniform vec4 u_shadowParams4;
uniform vec4 u_shadowMeta4;
uniform vec4 u_shadowTexel4;
#ifndef COIN_SHADOW_MULTITEXTURE
SAMPLER2D(s_shadow5, 13);
#endif
uniform mat4 u_shadowViewToClip5;
uniform mat4 u_shadowViewToLight5;
uniform vec4 u_shadowParams5;
uniform vec4 u_shadowMeta5;
uniform vec4 u_shadowTexel5;
#ifndef COIN_SHADOW_MULTITEXTURE
SAMPLER2D(s_shadow6, 14);
#endif
uniform mat4 u_shadowViewToClip6;
uniform mat4 u_shadowViewToLight6;
uniform vec4 u_shadowParams6;
uniform vec4 u_shadowMeta6;
uniform vec4 u_shadowTexel6;
#ifndef COIN_SHADOW_MULTITEXTURE
SAMPLER2D(s_shadow7, 15);
#endif
uniform mat4 u_shadowViewToClip7;
uniform mat4 u_shadowViewToLight7;
uniform vec4 u_shadowParams7;
uniform vec4 u_shadowMeta7;
uniform vec4 u_shadowTexel7;
#endif
float coinShadowVisibility(vec3 positionView, mat4 viewToClip, mat4 viewToLight,
                           vec4 params, vec4 meta, vec4 texelSize, COIN_MOMENT_SAMPLER mapTexture)
{
  vec4 coord = mul(viewToClip, vec4(positionView, 1.0));
  if (coord.w <= 0.0) return 1.0;
  vec3 clip = coord.xyz / coord.w;
  vec2 uv = vec2(clip.x * 0.5 + 0.5,
    meta.w > 0.5 ? clip.y * 0.5 + 0.5 : 0.5 - clip.y * 0.5);
  if (uv.x < 0.0 || uv.x >= 1.0 || uv.y < 0.0 || uv.y >= 1.0 ||
      clip.z < 0.0 || clip.z > 1.0) return 1.0;
  // Match the four texel-center loads in the wgpu receiver. Hardware
  // interpolation of RGBA32F moments varies at shadow boundaries.
  vec2 texel = uv / texelSize.xy - vec2(0.5, 0.5);
  vec2 base = floor(texel);
  vec2 weight = fract(texel);
  vec2 last = vec2(1.0, 1.0) / texelSize.xy - vec2(1.0, 1.0);
  vec2 p00 = clamp(base, vec2(0.0, 0.0), last);
  vec2 p10 = clamp(base + vec2(1.0, 0.0), vec2(0.0, 0.0), last);
  vec2 p01 = clamp(base + vec2(0.0, 1.0), vec2(0.0, 0.0), last);
  vec2 p11 = clamp(base + vec2(1.0, 1.0), vec2(0.0, 0.0), last);
  vec2 row0 = mix(COIN_MOMENT_LOAD(mapTexture, p00, texelSize).xy,
                  COIN_MOMENT_LOAD(mapTexture, p10, texelSize).xy, weight.x);
  vec2 row1 = mix(COIN_MOMENT_LOAD(mapTexture, p01, texelSize).xy,
                  COIN_MOMENT_LOAD(mapTexture, p11, texelSize).xy, weight.x);
  vec2 moments = mix(row0, row1, weight.y);
  if (moments.x >= 0.9999) return 1.0;
  vec3 lightView = mul(viewToLight, vec4(positionView, 1.0)).xyz;
  float distance = meta.x > 0.5 ? length(lightView) : -lightView.z;
  float normalized = (distance - params.x) / (params.y - params.x);
  if (normalized <= moments.x) return 1.0;
  float variance = min(max(moments.y - moments.x * moments.x, 0.0) + params.z, 1.0);
  float delta = moments.x - normalized;
  float probability = variance / (variance + delta * delta);
  probability *= smoothstep(params.w, 1.0, probability);
  if (meta.x < 0.5 && meta.y > 0.0) {
    float eyeZ = positionView.z;
    float fade = min(1.0, exp(meta.z * eyeZ * abs(eyeZ) / (meta.y * meta.y)));
    return 1.0 - (1.0 - probability) * fade;
  }
  return probability;
}
#ifdef COIN_BUMP_RECEIVER
SAMPLER2D(s_bumpImage,8);
vec3 coinBumpReceiverNormalize(vec3 v) { float d=dot(v,v);return d>0.0?v*inversesqrt(d):vec3_splat(0.0); }
uniform vec4 u_coinBumpLightMeta;
uniform vec4 u_coinBumpLightColors[8];
float coinBumpLightVisibility(int index,vec3 positionView) {
  if (u_shadowLightIndices.x>=0.0 && int(u_shadowLightIndices.x)==index)
    return coinShadowVisibility(positionView,u_shadowViewToClip0,u_shadowViewToLight0,
      u_shadowParams0,u_shadowMeta0,u_shadowTexel0,s_shadow0);
  if (u_shadowLightIndices.y>=0.0 && int(u_shadowLightIndices.y)==index)
    return coinShadowVisibility(positionView,u_shadowViewToClip1,u_shadowViewToLight1,
      u_shadowParams1,u_shadowMeta1,u_shadowTexel1,s_shadow1);
  if (u_shadowLightIndices.z>=0.0 && int(u_shadowLightIndices.z)==index)
    return coinShadowVisibility(positionView,u_shadowViewToClip2,u_shadowViewToLight2,
      u_shadowParams2,u_shadowMeta2,u_shadowTexel2,s_shadow2);
  if (u_shadowLightIndices.w>=0.0 && int(u_shadowLightIndices.w)==index)
    return coinShadowVisibility(positionView,u_shadowViewToClip3,u_shadowViewToLight3,
      u_shadowParams3,u_shadowMeta3,u_shadowTexel3,s_shadow3);
  if (u_shadowLightIndicesExtra.x>=0.0 && int(u_shadowLightIndicesExtra.x)==index)
    return coinShadowVisibility(positionView,u_shadowViewToClip4,u_shadowViewToLight4,
      u_shadowParams4,u_shadowMeta4,u_shadowTexel4,s_shadow4);
  if (u_shadowLightIndicesExtra.y>=0.0 && int(u_shadowLightIndicesExtra.y)==index)
    return coinShadowVisibility(positionView,u_shadowViewToClip5,u_shadowViewToLight5,
      u_shadowParams5,u_shadowMeta5,u_shadowTexel5,s_shadow5);
  if (u_shadowLightIndicesExtra.z>=0.0 && int(u_shadowLightIndicesExtra.z)==index)
    return coinShadowVisibility(positionView,u_shadowViewToClip6,u_shadowViewToLight6,
      u_shadowParams6,u_shadowMeta6,u_shadowTexel6,s_shadow6);
  if (u_shadowLightIndicesExtra.w>=0.0 && int(u_shadowLightIndicesExtra.w)==index)
    return coinShadowVisibility(positionView,u_shadowViewToClip7,u_shadowViewToLight7,
      u_shadowParams7,u_shadowMeta7,u_shadowTexel7,s_shadow7);
  return 1.0;
}
#endif
uniform vec4 u_shadowFlatDiffuse;
uniform vec4 u_shadowFlatSpecular;
uniform vec4 u_shadowFlatParams;
void main()
{
#ifdef COIN_BUMP_RECEIVER
  vec4 color=vec4(0.0,0.0,0.0,1.0);
  vec3 specularColor=vec3_splat(0.0);
  if(u_coinBumpLightMeta.x<1.5) {
  vec3 bumpNormal=texture2D(s_bumpImage,v_viewNormal.xy/v_material.y).rgb*2.0-1.0;
  float visibility=1.0;
  if(u_shadowLightIndices.x>=0.0)visibility=coinShadowVisibility(v_viewPosition,
    u_shadowViewToClip0,u_shadowViewToLight0,u_shadowParams0,u_shadowMeta0,u_shadowTexel0,s_shadow0);
  color=vec4(clamp(visibility*v_color0.rgb*clamp(dot(bumpNormal,coinBumpReceiverNormalize(v_ambient.xyz)),0.0,1.0),0.0,1.0),1.0);
  specularColor=visibility*v_specular.rgb*pow(clamp(dot(bumpNormal,coinBumpReceiverNormalize(v_ambient.xyz+v_emission.xyz)),0.0,1.0),64.0*v_material.x);
  } else {
    vec3 bumpNormal=texture2D(s_bumpImage,v_viewNormal.xy/v_material.y).rgb*2.0-1.0;
    vec3 l[8];
    l[0]=v_ambient.xyz;l[1]=v_emission.xyz;
    l[2]=v_texcoords5.xyz;l[3]=v_texcoords6.xyz;
    l[4]=v_texcoords7.xyz;l[5]=v_shadowVertex.xyz;
    l[6]=vec3(v_ambient.w,v_emission.w,v_texcoords4.w);
    l[7]=vec3(v_texcoords5.w,v_texcoords6.w,v_texcoords7.w);
    vec3 diffuse=vec3_splat(0.0);
    for(int i=0;i<8;++i) {
      if(float(i)>=u_coinBumpLightMeta.x)break;
      float visibility=coinBumpLightVisibility(i,v_viewPosition);
      diffuse+=visibility*v_color0.rgb*u_coinBumpLightColors[i].rgb*
        u_coinBumpLightColors[i].w*clamp(dot(bumpNormal,coinBumpReceiverNormalize(l[i])),0.0,1.0);
      specularColor+=visibility*v_specular.rgb*
        pow(clamp(dot(bumpNormal,coinBumpReceiverNormalize(l[i]+v_texcoords4.xyz)),0.0,1.0),64.0*v_material.x);
    }
    color=vec4(clamp(diffuse,0.0,1.0),1.0);
  }

#else
  vec4 primaryDiffuse = mix(v_texcoords4, u_shadowFlatDiffuse, u_shadowFlatParams.y);
  vec4 primarySpecular = mix(v_specular, u_shadowFlatSpecular, u_shadowFlatParams.y);
  float primaryShininess = mix(v_material.x, u_shadowFlatParams.x, u_shadowFlatParams.y);
  vec4 color = v_color0;
  color.a = mix(color.a, primaryDiffuse.a, u_shadowFlatParams.y);
  vec3 specularColor = vec3_splat(0.0);
  if (v_material.y > 0.5) color.rgb += coinOrdinaryFragmentColor(primaryDiffuse, primarySpecular, v_viewPosition, v_viewNormal, primaryShininess, specularColor);
  if (v_material.y > 0.5) {
    if (u_shadowLightIndices.x >= 0.0) {
      int index = int(u_shadowLightIndices.x);
      vec3 contribution = coinShadowContribution(index,
        coinShadowVertexColor(0, v_ambient, v_emission, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex),
        primaryDiffuse, primarySpecular, v_viewPosition, v_viewNormal, primaryShininess);
      vec3 specularPart = u_shadowQuality.x > 0.5 ? vec3_splat(0.0) :
        coinGroupLightContribution(index, vec4_splat(0.0), primarySpecular, v_viewPosition, v_viewNormal, primaryShininess);
      float visibility = coinShadowVisibility(v_viewPosition,
        u_shadowViewToClip0, u_shadowViewToLight0, u_shadowParams0,
        u_shadowMeta0, u_shadowTexel0, s_shadow0);
      color.rgb += (contribution - specularPart) * visibility;
      specularColor += specularPart * visibility;
    }
    if (u_shadowLightIndices.y >= 0.0) {
      int index = int(u_shadowLightIndices.y);
      vec3 contribution = coinShadowContribution(index,
        coinShadowVertexColor(1, v_ambient, v_emission, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex),
        primaryDiffuse, primarySpecular, v_viewPosition, v_viewNormal, primaryShininess);
      vec3 specularPart = u_shadowQuality.x > 0.5 ? vec3_splat(0.0) :
        coinGroupLightContribution(index, vec4_splat(0.0), primarySpecular, v_viewPosition, v_viewNormal, primaryShininess);
      float visibility = coinShadowVisibility(v_viewPosition,
        u_shadowViewToClip1, u_shadowViewToLight1, u_shadowParams1,
        u_shadowMeta1, u_shadowTexel1, s_shadow1);
      color.rgb += (contribution - specularPart) * visibility;
      specularColor += specularPart * visibility;
    }
    if (u_shadowLightIndices.z >= 0.0) {
      int index = int(u_shadowLightIndices.z);
      vec3 contribution = coinShadowContribution(index,
        coinShadowVertexColor(2, v_ambient, v_emission, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex),
        primaryDiffuse, primarySpecular, v_viewPosition, v_viewNormal, primaryShininess);
      vec3 specularPart = u_shadowQuality.x > 0.5 ? vec3_splat(0.0) :
        coinGroupLightContribution(index, vec4_splat(0.0), primarySpecular, v_viewPosition, v_viewNormal, primaryShininess);
      float visibility = coinShadowVisibility(v_viewPosition,
        u_shadowViewToClip2, u_shadowViewToLight2, u_shadowParams2,
        u_shadowMeta2, u_shadowTexel2, s_shadow2);
      color.rgb += (contribution - specularPart) * visibility;
      specularColor += specularPart * visibility;
    }
    if (u_shadowLightIndices.w >= 0.0) {
      int index = int(u_shadowLightIndices.w);
      vec3 contribution = coinShadowContribution(index,
        coinShadowVertexColor(3, v_ambient, v_emission, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex),
        primaryDiffuse, primarySpecular, v_viewPosition, v_viewNormal, primaryShininess);
      vec3 specularPart = u_shadowQuality.x > 0.5 ? vec3_splat(0.0) :
        coinGroupLightContribution(index, vec4_splat(0.0), primarySpecular, v_viewPosition, v_viewNormal, primaryShininess);
      float visibility = coinShadowVisibility(v_viewPosition,
        u_shadowViewToClip3, u_shadowViewToLight3, u_shadowParams3,
        u_shadowMeta3, u_shadowTexel3, s_shadow3);
      color.rgb += (contribution - specularPart) * visibility;
      specularColor += specularPart * visibility;
    }

#ifdef COIN_SHADOW_EIGHT
    if (u_shadowLightIndicesExtra.x >= 0.0) {
      int index = int(u_shadowLightIndicesExtra.x);
      vec3 contribution = coinShadowContribution(index,
        coinShadowVertexColor(4, v_ambient, v_emission, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex),
        primaryDiffuse, primarySpecular, v_viewPosition, v_viewNormal, primaryShininess);
      vec3 specularPart = u_shadowQuality.x > 0.5 ? vec3_splat(0.0) :
        coinGroupLightContribution(index, vec4_splat(0.0), primarySpecular, v_viewPosition, v_viewNormal, primaryShininess);
      float visibility = coinShadowVisibility(v_viewPosition,
        u_shadowViewToClip4, u_shadowViewToLight4, u_shadowParams4,
        u_shadowMeta4, u_shadowTexel4, s_shadow4);
      color.rgb += (contribution - specularPart) * visibility;
      specularColor += specularPart * visibility;
    }
    if (u_shadowLightIndicesExtra.y >= 0.0) {
      int index = int(u_shadowLightIndicesExtra.y);
      vec3 contribution = coinShadowContribution(index,
        coinShadowVertexColor(5, v_ambient, v_emission, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex),
        primaryDiffuse, primarySpecular, v_viewPosition, v_viewNormal, primaryShininess);
      vec3 specularPart = u_shadowQuality.x > 0.5 ? vec3_splat(0.0) :
        coinGroupLightContribution(index, vec4_splat(0.0), primarySpecular, v_viewPosition, v_viewNormal, primaryShininess);
      float visibility = coinShadowVisibility(v_viewPosition,
        u_shadowViewToClip5, u_shadowViewToLight5, u_shadowParams5,
        u_shadowMeta5, u_shadowTexel5, s_shadow5);
      color.rgb += (contribution - specularPart) * visibility;
      specularColor += specularPart * visibility;
    }
    if (u_shadowLightIndicesExtra.z >= 0.0) {
      int index = int(u_shadowLightIndicesExtra.z);
      vec3 contribution = coinShadowContribution(index,
        coinShadowVertexColor(6, v_ambient, v_emission, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex),
        primaryDiffuse, primarySpecular, v_viewPosition, v_viewNormal, primaryShininess);
      vec3 specularPart = u_shadowQuality.x > 0.5 ? vec3_splat(0.0) :
        coinGroupLightContribution(index, vec4_splat(0.0), primarySpecular, v_viewPosition, v_viewNormal, primaryShininess);
      float visibility = coinShadowVisibility(v_viewPosition,
        u_shadowViewToClip6, u_shadowViewToLight6, u_shadowParams6,
        u_shadowMeta6, u_shadowTexel6, s_shadow6);
      color.rgb += (contribution - specularPart) * visibility;
      specularColor += specularPart * visibility;
    }
    if (u_shadowLightIndicesExtra.w >= 0.0) {
      int index = int(u_shadowLightIndicesExtra.w);
      vec3 contribution = coinShadowContribution(index,
        coinShadowVertexColor(7, v_ambient, v_emission, v_texcoords5, v_texcoords6, v_texcoords7, v_shadowVertex),
        primaryDiffuse, primarySpecular, v_viewPosition, v_viewNormal, primaryShininess);
      vec3 specularPart = u_shadowQuality.x > 0.5 ? vec3_splat(0.0) :
        coinGroupLightContribution(index, vec4_splat(0.0), primarySpecular, v_viewPosition, v_viewNormal, primaryShininess);
      float visibility = coinShadowVisibility(v_viewPosition,
        u_shadowViewToClip7, u_shadowViewToLight7, u_shadowParams7,
        u_shadowMeta7, u_shadowTexel7, s_shadow7);
      color.rgb += (contribution - specularPart) * visibility;
      specularColor += specularPart * visibility;
    }
#endif
  }
#endif
  color.rgb = clamp(color.rgb, 0.0, 1.0);
#if COIN_SURFACE_SAMPLE_DEPTH
  vec2 sampleClip = coinSurfaceSampleClip(v_coinClipDepth);
  float depth = coinWindowDepth(sampleClip.x / sampleClip.y);
#elif COIN_PEEL_MSAA
  vec2 sampleClip = coinPeelClip(v_coinClipDepth);
  float depth = coinWindowDepth(sampleClip.x / sampleClip.y);
#else
  float depth = coinWindowDepth(coinSurfaceClipDepth(v_coinClipDepth, gl_FragCoord.z));
#endif
#if !COIN_NATIVE_SURFACE_DEPTH
  gl_FragDepth = depth;
#endif
#ifdef COIN_SHADOW_PEEL
  coinPeelCoverage();
  vec2 peelUV = gl_FragCoord.xy * u_depthInfo.xy;
  if (u_peelMeta.y > 0.5 &&
      !coinPeelDepthAccepts(depth, coinPeelRead(s_opaqueDepth, peelUV), u_peelMeta.z)) discard;
  if (u_peelMeta.x > 0.5 && depth <= coinPeelRead(s_prevDepth, peelUV)) discard;
#endif
#ifdef COIN_PORTABLE_MODULE
  // PRIMARY owns the completed lighting. The module output then owns RGBA;
  // specular is not added a second time after its texturing/fog stage.
  if (v_material.y > 0.5) color.rgb = clamp(color.rgb + specularColor, 0.0, 1.0);
#endif
#ifdef COIN_SHADOW_MULTITEXTURE
  vec4 surface = coinSurfaceColor(gl_FragCoord.xy, color, v_texcoord0,
    v_viewPosition, v_shadowUV0, v_shadowUV1, v_shadowUV2, v_shadowUV3, v_shadowUV4, v_shadowUV5, vec4(v_shadowUV5.yzw,v_shadowR.x), vec4(v_shadowR.yzw,0.0));
#else
  vec4 surface = coinSurfaceColor(gl_FragCoord.xy, color, v_texcoord0,
    v_viewPosition, primaryDiffuse, v_texcoords5, v_texcoords6, v_texcoords7, vec4_splat(1.0), vec4_splat(1.0), vec4_splat(0.0), vec4_splat(0.0));
#endif
#ifndef COIN_PORTABLE_MODULE
  surface.rgb = clamp(surface.rgb + specularColor, 0.0, 1.0);
#ifdef COIN_BUMP_RECEIVER
  surface.a=1.0;
#endif
#endif
#ifdef COIN_SHADOW_OIT
  if (surface.a <= 0.0) discard;
  gl_FragData[0] = coinWeightedAccumulation(surface, depth);
  gl_FragData[1] = vec4_splat(clamp(surface.a, 0.0, 1.0));
#else
#ifdef COIN_SHADOW_PEEL
  gl_FragData[0] = surface;
  gl_FragData[1] = vec4(u_peelMeta.w, 0.0, 0.0, 0.0);
#else
  gl_FragColor = surface;
#endif
#endif
}
