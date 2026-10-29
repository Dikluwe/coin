$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1
#include <bgfx_shader.sh>
#include "coin_depth.sh"
#include "coin_pbr_brdf.sh"
#ifdef COIN_PBR_SHADOW
#include "coin_pbr_shadows.sh"
#endif
#ifdef COIN_PBR_IBL
#include "coin_pbr_ibl.sh"
#endif
SAMPLER2D(s_pbrBase,4);
SAMPLER2D(s_pbrOrm,5);
SAMPLER2D(s_pbrNormal,6);
SAMPLER2D(s_pbrEmission,7);
uniform vec4 u_pbrTextureUnits;
uniform vec4 u_pbrClearcoat; // factor, perceptual roughness; fixed IOR 1.5
uniform vec4 u_pbrAnisotropy; // strength, cos(rotation), sin(rotation)
uniform vec4 u_pbrNormalParameters; // strength, Y sign
vec2 coinPbrUvFrom(float unit,vec3 uv0,vec4 uv1,vec4 uv2,vec4 uv3,vec4 uv4,vec4 q0,vec4 q1) {
  if(unit<0.5)return uv0.xy/uv0.z;
  if(unit<1.5)return uv1.xy/q0.x;
  if(unit<2.5)return uv1.zw/q0.y;
  if(unit<3.5)return uv2.xy/q0.z;
  if(unit<4.5)return uv2.zw/q0.w;
  if(unit<5.5)return uv3.xy/q1.x;
  if(unit<6.5)return uv3.zw/q1.y;
  return uv4.xy/q1.z;
}
#define coinPbrUv(unit) coinPbrUvFrom(unit,v_texcoord0,v_texcoords4,v_texcoords5,v_texcoords6,v_texcoords7,v_textureQ0,v_textureQ1)
uniform vec4 u_pbrBaseMetallic;
uniform vec4 u_pbrEmissionRoughness;
uniform vec4 u_pbrCamera; // x: perspective; y: receiver mask; z: planar ground.
SAMPLER2D(s_pbrReflection, 3);
uniform vec4 u_pbrReflectionInfo; // inverse main-view dimensions, GL origin, authored rough planar selector.
#include "coin_pbr_rough_planar.sh"
uniform vec4 u_lightCount;
uniform vec4 u_lightPositionType[8];
uniform vec4 u_lightDirectionCutoff[8];
uniform vec4 u_lightColorIntensity[8];
uniform vec4 u_lightAttenuationDrop[8];
uniform vec4 u_clipMeta;
uniform vec4 u_clipPlanes[8];
#ifdef COIN_PBR_RECTANGLE
#include "coin_pbr_area.sh"
#ifdef COIN_PBR_AREA_SHADOW
#ifdef COIN_PBR_AREA_CATCHER
#include "coin_pbr_area_catcher.sh"
#else
#include "coin_pbr_area_shadow.sh"
#endif
#endif
#endif
#include "coin_pbr_view_effects.sh"
void main() {
  for (int i = 0; i < 8; ++i) {
    if (float(i) >= u_clipMeta.x) break;
    if (dot(u_clipPlanes[i], vec4(v_viewPosition, 1.0)) < 0.0) discard;
  }
  gl_FragDepth = coinWindowDepth(v_coinClipDepth.x / v_coinClipDepth.y);
  vec3 n = coinPbrSafeUnit(v_viewNormal);
  vec3 v = u_pbrCamera.x > 0.5 ? coinPbrSafeUnit(-v_viewPosition) : vec3(0.0, 0.0, 1.0);
  vec3 coatNormal=n;
  float coatWeight=u_pbrClearcoat.x*(.04+.96*pow(1.0-clamp(abs(dot(v,coatNormal)),0.0,1.0),5.0));
  vec3 base=u_pbrBaseMetallic.rgb;
  vec3 emission=u_pbrEmissionRoughness.rgb;
  float metallic=u_pbrBaseMetallic.a,occlusion=1.0;
  float viewOcclusion=coinViewAmbientVisibility(v_viewPosition,n);
  occlusion*=viewOcclusion;
  float roughness=u_pbrEmissionRoughness.a;
  if(u_pbrTextureUnits.x>=0.0)base*=texture2D(s_pbrBase,coinPbrUv(u_pbrTextureUnits.x)).rgb;
  if(u_pbrTextureUnits.y>=0.0) {
    vec3 orm=texture2D(s_pbrOrm,coinPbrUv(u_pbrTextureUnits.y)).rgb;
    occlusion*=orm.r;roughness*=orm.g;metallic*=orm.b;
  }
  if(u_pbrTextureUnits.w>=0.0)emission*=texture2D(s_pbrEmission,coinPbrUv(u_pbrTextureUnits.w)).rgb;
  if(u_pbrTextureUnits.z>=0.0 && abs(v_ambient.w)>0.5 && u_pbrNormalParameters.x>0.0) {
    vec3 meanNormal=texture2D(s_pbrNormal,v_emission.xy/v_emission.z).rgb*2.0-1.0;
    float meanLength=clamp(length(meanNormal),0.001,1.0);
    float variance=max(0.0,(1.0-meanLength)/meanLength)*u_pbrNormalParameters.x*u_pbrNormalParameters.x;
    meanNormal.xy*=vec2(u_pbrNormalParameters.x,u_pbrNormalParameters.x*u_pbrNormalParameters.y);
    vec3 t=coinPbrSafeUnit(v_ambient.xyz-n*dot(n,v_ambient.xyz));
    vec3 b=cross(n,t)*(v_ambient.w<0.0?-1.0:1.0);
    n=coinPbrSafeUnit(t*meanNormal.x+b*meanNormal.y+n*meanNormal.z);
    roughness=clamp(pow(pow(roughness,4.0)+variance,0.25),0.0,1.0);
  }
#ifdef COIN_PBR_IBL
  roughness=coinPbrFilteredRoughness(n,roughness);
#endif
#ifdef COIN_PBR_RECTANGLE
  if(u_pbrAreaQuality.w>.5) {
    vec3 dx=dFdx(n),dy=dFdy(n);
    float kernel=min(2.0*u_pbrAreaQuality.y*(dot(dx,dx)+dot(dy,dy)),u_pbrAreaQuality.z);
    float filtered=max(roughness,.045);
    roughness=sqrt(sqrt(min(filtered*filtered*filtered*filtered+kernel,1.0)));
  }
#endif
  float anisotropy=abs(v_specular.w)>0.5?u_pbrAnisotropy.x:0.0;
  vec3 anisotropicT=coinPbrSafeUnit(v_specular.xyz-n*dot(n,v_specular.xyz));
  vec3 authoredB=cross(n,anisotropicT)*(v_specular.w<0.0?-1.0:1.0);
  anisotropicT=coinPbrSafeUnit(anisotropicT*u_pbrAnisotropy.y+authoredB*u_pbrAnisotropy.z);
  vec3 anisotropicB=cross(n,anisotropicT);
  float alpha=max(roughness,0.045)*max(roughness,0.045);
  vec2 anisotropicAxes=vec2(mix(alpha,1.0,anisotropy*anisotropy),alpha);
  // Isotropic energy compensation uses geometric mean alpha. Its error is
  // measured independently from the single-direction environment approximation.
  float effectiveRoughness=anisotropy>0.0?pow(anisotropicAxes.x*anisotropicAxes.y,0.25):roughness;
#ifdef COIN_PBR_RECTANGLE
  // Isotropic surfaces do not require an authored anisotropy tangent.
  if(anisotropy<=0.0) {anisotropicT=coinAreaTangent(n);anisotropicB=cross(n,anisotropicT);}
#ifdef COIN_PBR_AREA_SHADOW
#ifdef COIN_PBR_AREA_CATCHER
  gl_FragColor=coinAreaCatcherMaterial(
#else
  gl_FragColor=coinAreaShadowMaterial(
#endif
v_color0.xyz,v_viewPosition,n,anisotropicT,anisotropicB,v,coatNormal,base,metallic,roughness,anisotropy,coatWeight,u_pbrClearcoat.y);
#else
  vec3 area=coinAreaMaterial(v_viewPosition,n,anisotropicT,anisotropicB,v,coatNormal,base,metallic,roughness,anisotropy,coatWeight,u_pbrClearcoat.y);
  gl_FragColor=vec4(max(area,vec3_splat(0.0)),0.0);
#endif
#ifndef COIN_PBR_AREA_CATCHER
  // In the rectangular catcher profile only, successful opaque fragments mark
  // coverage in private A. Query errors keep +1; ordinary profiles keep zero.
  if(u_pbrCamera.y<-.5&&gl_FragColor.a==0.0)gl_FragColor.a=-1.0;
#endif
#else
  vec3 radiance = emission*(1.0-coatWeight);
  vec4 stationaryA=vec4_splat(0.0),stationaryB=vec4_splat(0.0);
  vec3 blockedDirect=vec3_splat(0.0);
  for (int i = 0; i < 8; ++i) {
    if (float(i) >= u_lightCount.x) break;
    if (u_lightPositionType[i].w > 2.5) continue; // Separate rectangular HDR pass.
    vec3 l; float attenuation = 1.0;
    if (u_lightPositionType[i].w < 0.5) l = coinPbrSafeUnit(-u_lightDirectionCutoff[i].xyz);
    else {
      vec3 delta = u_lightPositionType[i].xyz - v_viewPosition;
      float distance = length(delta);
      if (distance <= 1e-6) continue;
      l = delta / distance;
      vec3 a = u_lightAttenuationDrop[i].xyz;
      float divisor = a.z + distance * (a.y + distance * a.x);
      if (divisor <= 1e-6) continue;
      attenuation = 1.0 / divisor;
      if (u_lightPositionType[i].w > 1.5) {
        float cone = dot(coinPbrSafeUnit(u_lightDirectionCutoff[i].xyz), -l);
        if (cone < u_lightDirectionCutoff[i].w) continue;
        attenuation *= pow(max(cone, 0.0), u_lightAttenuationDrop[i].w * 128.0);
      }
    }
#ifdef COIN_PBR_IBL
    vec3 brdf = coinPbrQualityBrdf(n,v,l,base,metallic,effectiveRoughness);
#else
    vec3 brdf = coinPbrBrdf(n,v,l,base,metallic,effectiveRoughness);
#endif
    if(anisotropy>0.0) {
#ifdef COIN_PBR_ENVIRONMENT_ARRAY
      brdf=coinPbrAnisQualityBrdf(n,anisotropicT,anisotropicB,v,l,anisotropicAxes,base,metallic,roughness,anisotropy);
#else
      vec3 f0=mix(vec3_splat(.04),base,metallic);
      float vh=clamp(dot(v,coinPbrSafeUnit(v+l)),0.0,1.0);
      vec3 fresnel=f0+(vec3_splat(1.0)-f0)*pow(1.0-vh,5.0);
      brdf+=(coinPbrAnisotropicFactor(n,anisotropicT,anisotropicB,v,l,anisotropicAxes)-coinPbrSpecularFactor(n,v,l,effectiveRoughness))*fresnel;
#endif
    }
    float visibility = 1.0;
    if(i==0)visibility*=coinViewDirectionalVisibility(v_viewPosition,n,l);
#ifdef COIN_PBR_SHADOW
    visibility = coinPbrVisibility(i, v_viewPosition);
#endif
    vec3 surface=brdf*max(dot(n,l),0.0);
    if(coatWeight>0.0)surface=surface*(1.0-coatWeight)+vec3_splat(coatWeight*coinPbrSpecularFactor(coatNormal,v,l,u_pbrClearcoat.y)*max(dot(coatNormal,l),0.0));
    vec3 direct=surface*u_lightColorIntensity[i].rgb*(u_lightColorIntensity[i].a*attenuation);
    radiance += visibility*direct;
    blockedDirect+=(1.0-visibility)*direct;
  }
  vec4 planar=vec4_splat(0.0);
  if(u_pbrCamera.z>0.5) {
    vec2 uv=gl_FragCoord.xy*u_pbrReflectionInfo.xy;
    if(u_pbrReflectionInfo.z>0.5)uv.y=1.0-uv.y;
    // The selector is explicitly reset for every draw/target. Smooth keeps
    // the original operation and does not read another target's rough uniforms.
    if(u_pbrReflectionInfo.w>.045) {
      if(u_pbrRoughNormalHeight.w!=0.0)planar=coinPbrRoughPlanar(v_viewPosition,reflect(-v,n),u_pbrReflectionInfo.w);
      else planar=texture2D(s_pbrReflection,uv);
    } else planar=texture2D(s_pbrReflection,uv);
  }
#ifdef COIN_PBR_IBL
  if(u_pbrCamera.w>0.0&&u_pbrCamera.y==0.0) {
    vec2 aoUv=gl_FragCoord.xy*u_pbrReflectionInfo.xy;
    if(u_pbrReflectionInfo.z>.5)aoUv.y=1.0-aoUv.y;
    vec4 counts=texture2D(s_pbrReflection,aoUv);
    // Samples are powers of two: CPU supplies an exact reciprocal.
    if(counts.w>.5)occlusion*=counts.x*u_pbrCamera.w;
  }
  if(anisotropy>0.0) {
#ifdef COIN_PBR_ENVIRONMENT_ARRAY
    vec3 deterministic;
    vec3 environment=coinPbrAnisEnvironment(v_viewPosition,n,anisotropicT,anisotropicB,v,anisotropicAxes,base,metallic,roughness,anisotropy,planar,occlusion,stationaryA,stationaryB,deterministic);
#ifdef COIN_PBR_ACCUMULATE
    radiance+=(1.0-coatWeight)*min(deterministic,vec3_splat(65504.0));
    stationaryA.rgb*=1.0-coatWeight;stationaryB.rgb*=1.0-coatWeight;
#else
    radiance+=(1.0-coatWeight)*environment;
#endif
#else
    vec3 bent=coinPbrSafeUnit(v-anisotropicB*dot(v,anisotropicB));
    float mixWeight=pow(1.0-anisotropy*(1.0-roughness),4.0);
    bent=coinPbrSafeUnit(mix(bent,n,mixWeight));
    vec3 reflection=coinPbrSafeUnit(mix(reflect(-v,bent),bent,roughness*roughness));
    radiance+=(1.0-coatWeight)*coinPbrEnvironmentModel(v_viewPosition,n,v,base,metallic,roughness,planar,occlusion,reflection,effectiveRoughness);
#endif
  } else radiance += (1.0-coatWeight)*coinPbrEnvironment(v_viewPosition,n, v, base,
    metallic, roughness,planar,occlusion);
  if(coatWeight>0.0)radiance+=coatWeight*viewOcclusion*coinPbrCoatEnvironment(v_viewPosition,coatNormal,v,u_pbrClearcoat.y,planar);
#else
  if(u_pbrCamera.z>0.5) {
    vec3 f0=mix(vec3_splat(0.04),base,metallic);
    vec3 fresnel=f0+(vec3_splat(1.0)-f0)*pow(1.0-clamp(dot(n,v),0.0,1.0),5.0);
    radiance+=planar.rgb*fresnel*(1.0-coatWeight)+planar.rgb*coatWeight;
  }
#endif
#if defined(COIN_PBR_IBL) && defined(COIN_PBR_ENVIRONMENT_ARRAY) && !defined(COIN_PBR_DISTANT_ONLY)
  if(u_pbrProbeInfo.x>0.0&&(!coinPbrLocalFinite(radiance)||!coinPbrLocalFinite(stationaryA.rgb)||!coinPbrLocalFinite(stationaryB.rgb))) {
    // A private HDR validation readback rejects this candidate before any
    // publication. Alpha survives refinement resolve even if min masks NaN.
#ifdef COIN_PBR_ACCUMULATE
    gl_FragData[0]=vec4(0.0,0.0,0.0,-1.0);gl_FragData[1]=gl_FragData[2]=vec4_splat(0.0);
#else
    gl_FragColor=vec4(0.0,0.0,0.0,-1.0);
#endif
    return;
  }
#endif
  if(u_pbrCamera.y>0.5) {
    // Achromatic matte: fraction of unoccluded outgoing luminance lost to
    // direct-light occlusion. IBL and unshadowed lights remain in the total.
    vec3 luminance=vec3(0.2126,0.7152,0.0722);
    float total=dot(radiance+blockedDirect,luminance);
    if(u_pbrCamera.y>1.5) {
      float blocked=dot(blockedDirect,luminance);
      bool invalid=(floatBitsToUint(total)&0x7f800000u)==0x7f800000u||
        (floatBitsToUint(blocked)&0x7f800000u)==0x7f800000u||total<0.0||blocked<0.0||blocked>total;
      vec4 pair=invalid?vec4(0.0,0.0,0.0,1.0):vec4(blocked,total,0.0,0.0);
#ifdef COIN_PBR_ACCUMULATE
      gl_FragData[0]=pair;gl_FragData[1]=gl_FragData[2]=vec4_splat(0.0);return;
#else
      gl_FragColor=pair;return;
#endif
    }
    float opacity=total>0.0?clamp(dot(blockedDirect,luminance)/total,0.0,1.0):0.0;
#ifdef COIN_PBR_ACCUMULATE
    gl_FragData[0]=vec4(opacity,0.0,0.0,1.0);gl_FragData[1]=gl_FragData[2]=vec4_splat(0.0);return;
#else
    gl_FragColor=vec4(opacity,0.0,0.0,1.0);return;
#endif
  }
  // Saturate only at the representable binary16 limit, never at display white.
#ifdef COIN_PBR_ACCUMULATE
  gl_FragData[0]=vec4(min(radiance,vec3_splat(65504.0)),1.0);
  gl_FragData[1]=stationaryA;gl_FragData[2]=stationaryB;
#else
  gl_FragColor = vec4(min(radiance, vec3_splat(65504.0)), 1.0);
#endif
#endif // COIN_PBR_RECTANGLE
}
