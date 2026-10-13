// Split-sum GGX + SH9 irradiance, using the versioned CoinRender LUT.
SAMPLERCUBE(s_pbrSpecular, 0);
SAMPLER2D(s_pbrDfg, 1);
SAMPLER2D(s_pbrAlbedoAverage, 2);
uniform vec4 u_pbrQuality; // compensation, normal variance, width threshold
uniform mat4 u_pbrViewToWorld;
uniform vec4 u_pbrIbl; // intensity, last mip, cos(rotation), sin(rotation)
uniform vec4 u_pbrIrradiance[9];
vec3 coinPbrEnvironmentDirection(vec3 viewDirection) {
  vec3 world = coinPbrSafeUnit(mul(u_pbrViewToWorld, vec4(viewDirection, 0.0)).xyz);
  return vec3(u_pbrIbl.z * world.x + u_pbrIbl.w * world.y,
    -u_pbrIbl.w * world.x + u_pbrIbl.z * world.y, world.z);
}
vec3 coinPbrIrradiance(vec3 n) {
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
  vec4 viewTable=texture2D(s_pbrDfg,vec2(nv,roughness));
  vec4 lightTable=texture2D(s_pbrDfg,vec2(nl,roughness));
  vec3 avg=texture2D(s_pbrAlbedoAverage,vec2(0.5,roughness)).rgb;
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
vec3 coinPbrEnvironment(vec3 n, vec3 v, vec3 base, float metallic, float roughness) {
  float nv = clamp(dot(n, v), 0.0, 1.0);
  if (nv <= 0.0) return vec3_splat(0.0);
  vec3 worldN = coinPbrEnvironmentDirection(n);
  vec3 reflection = coinPbrEnvironmentDirection(reflect(-v, n));
  vec4 table = texture2D(s_pbrDfg, vec2(nv, roughness));
  vec3 f0 = mix(vec3_splat(0.04), base, metallic);
  // Mip zero evaluates the same 0.045 floor as the BRDF. Interpolate from
  // that evaluated roughness to mip one, instead of widening floor materials.
  float evaluatedR = max(roughness, 0.045);
  float firstR = 1.0 / u_pbrIbl.y;
  float lod = evaluatedR < firstR ? (evaluatedR - 0.045) / (firstR - 0.045) : evaluatedR * u_pbrIbl.y;
  vec3 filtered = textureCubeLod(s_pbrSpecular, reflection, lod).rgb;
  vec3 specular = filtered * (f0 * table.r + vec3_splat(table.g));
  vec3 diffuse = coinPbrIrradiance(worldN) * base * (1.0 - metallic) *
    (vec3_splat(1.0) - f0) * (1.0 - table.b) / coinPbrPi;
  if(u_pbrQuality.x>0.5) {
    vec3 avg=texture2D(s_pbrAlbedoAverage,vec2(0.5,roughness)).rgb;
    vec3 multi=coinPbrMultipleWeight(f0,min(avg.z,1.0));
    specular+=coinPbrIrradiance(worldN)*max(1.0-table.r-table.g,0.0)*multi/coinPbrPi;
    diffuse=coinPbrIrradiance(worldN)*base*(1.0-metallic)*
      (vec3_splat(1.0)-coinPbrDirectionalAlbedo(f0,table,multi))/coinPbrPi;
  }
  return (specular + diffuse) * u_pbrIbl.x;
}
