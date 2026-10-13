// Split-sum GGX + SH9 irradiance, using the versioned CoinRender LUT.
SAMPLERCUBE(s_pbrSpecular, 0);
SAMPLER2D(s_pbrDfg, 1);
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
  return (specular + diffuse) * u_pbrIbl.x;
}
