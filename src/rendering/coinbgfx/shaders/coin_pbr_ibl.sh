// Split-sum GGX + SH9 irradiance, using the versioned CoinRender LUT.
#ifdef COIN_PBR_ENVIRONMENT_ARRAY
SAMPLERCUBEARRAY(s_pbrSpecular, 0);
uniform vec4 u_pbrLutLayout; // texture width/height, legacy size, anisotropic Y offset
uniform vec4 u_pbrAnisoGrid; // NdotV, roughness, strength, azimuth grid sizes
uniform vec4 u_pbrAverageLayout;
uniform vec4 u_pbrAnisoSampling; // rays, source face size, last source mip, sequence offset
#else
SAMPLERCUBE(s_pbrSpecular, 0);
#endif
SAMPLER2D(s_pbrDfg, 1);
SAMPLER2D(s_pbrAlbedoAverage, 2);
vec4 coinPbrDfgLookup(vec2 uv) {
#ifdef COIN_PBR_ENVIRONMENT_ARRAY
  uv=clamp(uv,vec2_splat(.5/u_pbrLutLayout.z),vec2_splat(1.0-.5/u_pbrLutLayout.z));
  return texture2D(s_pbrDfg,uv*u_pbrLutLayout.z/u_pbrLutLayout.xy);
#else
  return texture2D(s_pbrDfg,uv);
#endif
}
vec3 coinPbrAverageLookup(float roughness) {
#ifdef COIN_PBR_ENVIRONMENT_ARRAY
  float r=clamp(roughness,.5/u_pbrAverageLayout.z,1.0-.5/u_pbrAverageLayout.z);
  return texture2D(s_pbrAlbedoAverage,vec2(.5/u_pbrAverageLayout.x,r*u_pbrAverageLayout.z/u_pbrAverageLayout.y)).rgb;
#else
  return texture2D(s_pbrAlbedoAverage,vec2(.5,roughness)).rgb;
#endif
}
vec3 coinPbrDistantFilteredEnvironment(vec3 direction,float lod) {
#ifdef COIN_PBR_ENVIRONMENT_ARRAY
  return textureCubeArrayLod(s_pbrSpecular,vec4(direction,1.0),lod).rgb;
#else
  return textureCubeLod(s_pbrSpecular,direction,lod).rgb;
#endif
}
uniform vec4 u_pbrQuality; // compensation, normal variance, width threshold
#include "coin_pbr_environment.sh"
uniform vec4 u_pbrIrradiance[9];
vec3 coinPbrDistantIrradiance(vec3 n) {
  vec3 result = u_pbrIrradiance[0].rgb * 0.2820947918;
  result += u_pbrIrradiance[1].rgb * (0.4886025119 * n.y);
  result += u_pbrIrradiance[2].rgb * (0.4886025119 * n.z);
  result += u_pbrIrradiance[3].rgb * (0.4886025119 * n.x);
  result += u_pbrIrradiance[4].rgb * (1.092548431 * n.x * n.y);
  result += u_pbrIrradiance[5].rgb * (1.092548431 * n.y * n.z);
  result += u_pbrIrradiance[6].rgb * (0.3153915653 * (3.0 * n.z * n.z - 1.0));
  result += u_pbrIrradiance[7].rgb * (1.092548431 * n.x * n.z);
  result += u_pbrIrradiance[8].rgb * (0.5462742153 * (n.x * n.x - n.y * n.y));
  return max(result, vec3_splat(0.0));
}
#if defined(COIN_PBR_ENVIRONMENT_ARRAY) && !defined(COIN_PBR_DISTANT_ONLY)
#include "coin_pbr_local_probes.sh"
#else
vec3 coinPbrFilteredEnvironment(vec3 viewPosition,vec3 direction,float lod) {return coinPbrDistantFilteredEnvironment(direction,lod);}
vec3 coinPbrIrradiance(vec3 viewPosition,vec3 n) {return coinPbrDistantIrradiance(n);}
float coinPbrIblGain() {return u_pbrIbl.x;}
#ifdef COIN_PBR_ENVIRONMENT_ARRAY
vec3 coinPbrSourceEnvironment(vec3 viewPosition,vec3 direction,float lod,float sampleArea) {
  return textureCubeArrayLod(s_pbrSpecular,vec4(direction,0.0),lod).rgb;
}
#endif
#endif
// Kulla/Conty compensation lobe; angular albedo is reconstructed from
// the exact same A/B table used for indirect lighting.
vec3 coinPbrMultipleWeight(vec3 f0, float averageEnergy) {
  vec3 favg=(vec3_splat(1.0)+20.0*f0)/21.0;
  return favg*favg*averageEnergy /
    max(vec3_splat(1.0)-favg*(1.0-averageEnergy),vec3_splat(1e-6));
}
vec3 coinPbrDirectionalAlbedo(vec3 f0,vec4 table,vec3 multi) {
  return clamp(f0*table.r+vec3_splat(table.g)+(1.0-table.r-table.g)*multi,
    vec3_splat(0.0),vec3_splat(1.0));
}
vec3 coinPbrQualityBrdf(vec3 n,vec3 v,vec3 l,vec3 base,float metallic,float roughness) {
  float nv=max(dot(n,v),0.0),nl=max(dot(n,l),0.0);
  if(nv<=0.0||nl<=0.0)return vec3_splat(0.0);
  if(u_pbrQuality.x<0.5)return coinPbrBrdf(n,v,l,base,metallic,roughness);
  vec3 f0=mix(vec3_splat(0.04),base,metallic);
  vec4 viewTable=coinPbrDfgLookup(vec2(nv,roughness));
  vec4 lightTable=coinPbrDfgLookup(vec2(nl,roughness));
  vec3 avg=coinPbrAverageLookup(roughness);
  float eavg=min(avg.z,1.0);
  vec3 multi=coinPbrMultipleWeight(f0,eavg);
  float missingV=max(1.0-viewTable.r-viewTable.g,0.0);
  float missingL=max(1.0-lightTable.r-lightTable.g,0.0);
  vec3 h=coinPbrSafeUnit(v+l);float fc=pow(1.0-clamp(dot(v,h),0.0,1.0),5.0);
  vec3 authoredF=f0+(vec3_splat(1.0)-f0)*fc;
  vec3 compensated=coinPbrSpecularFactor(n,v,l,roughness)*authoredF;
  compensated+=multi*missingV*missingL/(coinPbrPi*max(1.0-eavg,1e-6));
  vec3 sv=coinPbrDirectionalAlbedo(f0,viewTable,multi);
  vec3 sl=coinPbrDirectionalAlbedo(f0,lightTable,multi);
  vec3 savg=clamp(f0*avg.x+vec3_splat(avg.y)+(1.0-eavg)*multi,
    vec3_splat(0.0),vec3_splat(1.0));
  // Reciprocal diffuse residual: its white-environment integral is 1-sv.
  vec3 diffuse=base*(1.0-metallic)*(vec3_splat(1.0)-sv)*(vec3_splat(1.0)-sl) /
    (coinPbrPi*max(vec3_splat(1.0)-savg,vec3_splat(1e-6)));
  return compensated+diffuse;
}
float coinPbrFilteredRoughness(vec3 normal,float authored) {
  vec3 dx=dFdx(normal),dy=dFdy(normal);
  float kernel=min(2.0*u_pbrQuality.y*(dot(dx,dx)+dot(dy,dy)),u_pbrQuality.z);
  float r=max(authored,0.045);
  return sqrt(sqrt(min(r*r*r*r+kernel,1.0)));
}
vec3 coinPbrEnvironmentModel(vec3 viewPosition,vec3 n, vec3 v, vec3 base, float metallic, float roughness,vec4 planar,float occlusion,vec3 reflectedDirection,float albedoRoughness) {
  float nv = clamp(dot(n, v), 0.0, 1.0);
  if (nv <= 0.0) return vec3_splat(0.0);
  vec3 worldN = coinPbrEnvironmentDirection(n);
  vec3 reflection = coinPbrEnvironmentDirection(reflectedDirection);
  vec4 table = coinPbrDfgLookup(vec2(nv, albedoRoughness));
  vec3 f0 = mix(vec3_splat(0.04), base, metallic);
  // Mip zero evaluates the same 0.045 floor as the BRDF. Interpolate from
  // that evaluated roughness to mip one, instead of widening floor materials.
  float evaluatedR = max(roughness, 0.045);
  float firstR = 1.0 / u_pbrIbl.y;
  float lod = evaluatedR < firstR ? (evaluatedR - 0.045) / (firstR - 0.045) : evaluatedR * u_pbrIbl.y;
  vec3 filtered = coinPbrFilteredEnvironment(viewPosition,reflection,lod);
  vec3 specular = filtered * (f0 * table.r + vec3_splat(table.g));
  vec3 diffuse = coinPbrIrradiance(viewPosition,worldN) * base * (1.0 - metallic) *
    (vec3_splat(1.0) - f0) * (1.0 - table.b) / coinPbrPi;
  if(u_pbrQuality.x>0.5) {
    vec3 avg=coinPbrAverageLookup(albedoRoughness);
    vec3 multi=coinPbrMultipleWeight(f0,min(avg.z,1.0));
    specular+=coinPbrIrradiance(viewPosition,worldN)*max(1.0-table.r-table.g,0.0)*multi/coinPbrPi;
    diffuse=coinPbrIrradiance(viewPosition,worldN)*base*(1.0-metallic)*
      (vec3_splat(1.0)-coinPbrDirectionalAlbedo(f0,table,multi))/coinPbrPi;
  }
  diffuse*=occlusion;
  // Local coverage replaces the distant single-scattering reflection. The
  // auxiliary image is linear premultiplied radiance, independent of IBL gain.
  // Smooth samples the mirror image; rough uses the bounded GGX/proxy prefilter.
  // Both remain local approximations of view-dependent scene radiance.
  if(planar.a>0.0) {
    vec3 weight=f0*table.r+vec3_splat(table.g);
    return (specular+diffuse-filtered*weight*planar.a)*coinPbrIblGain()+planar.rgb*weight;
  }
  return (specular + diffuse) * coinPbrIblGain();
}

vec3 coinPbrEnvironment(vec3 viewPosition,vec3 n,vec3 v,vec3 base,float metallic,float roughness,vec4 planar,float occlusion) {
  return coinPbrEnvironmentModel(viewPosition,n,v,base,metallic,roughness,planar,occlusion,reflect(-v,n),roughness);
}

// KHR clearcoat reference layer: Fresnel is applied once by the caller using
// NdotV. The layer itself is the unit-Fresnel single-scattering GGX integral.
vec3 coinPbrCoatEnvironment(vec3 viewPosition,vec3 n,vec3 v,float roughness,vec4 planar) {
  float nv=clamp(dot(n,v),0.0,1.0);if(nv<=0.0)return vec3_splat(0.0);
  float r=max(roughness,.045),firstR=1.0/u_pbrIbl.y;
  float lod=r<firstR?(r-.045)/(firstR-.045):r*u_pbrIbl.y;
  vec3 direction=coinPbrEnvironmentDirection(reflect(-v,n));
  vec3 filtered=coinPbrFilteredEnvironment(viewPosition,direction,lod);
  vec4 table=coinPbrDfgLookup(vec2(nv,roughness));
  return ((1.0-planar.a)*filtered*coinPbrIblGain()+planar.rgb)*(table.r+table.g);
}

#ifdef COIN_PBR_ENVIRONMENT_ARRAY
#include "coin_pbr_anisotropic.sh"
#endif
