// CoinRender opaque profile v1. Single-scattering isotropic GGX.
// D/V reference: https://google.github.io/filament/main/filament.html
const float coinPbrPi = 3.141592653589793;
vec3 coinPbrSafeUnit(vec3 value) {
  float squared = dot(value, value);
  return squared > 1e-20 ? value * inversesqrt(squared) : vec3(0.0, 0.0, 1.0);
}
vec3 coinPbrBrdf(vec3 n, vec3 v, vec3 l, vec3 base, float metallic, float roughness) {
  float nv = max(dot(n, v), 0.0), nl = max(dot(n, l), 0.0);
  if (nv <= 0.0 || nl <= 0.0) return vec3_splat(0.0);
  vec3 h = coinPbrSafeUnit(v + l);
  float nh = clamp(dot(n, h), 0.0, 1.0), vh = clamp(dot(v, h), 0.0, 1.0);
  float r = max(roughness, 0.045), a = r * r, a2 = a * a;
  vec3 nhCross = cross(n, h);
  float denominator = dot(nhCross, nhCross) + nh * nh * a2;
  float distribution = a2 / (coinPbrPi * denominator * denominator);
  float lambdaL = nv * sqrt(a2 + (1.0 - a2) * nl * nl);
  float lambdaV = nl * sqrt(a2 + (1.0 - a2) * nv * nv);
  float visibility = 0.5 / max(lambdaL + lambdaV, 1e-20);
  vec3 f0 = mix(vec3_splat(0.04), base, metallic);
  float t = 1.0 - vh, t2 = t * t;
  vec3 fresnel = f0 + (vec3_splat(1.0) - f0) * (t2 * t2 * t);
  return distribution * visibility * fresnel +
    (1.0 - metallic) * (vec3_splat(1.0) - fresnel) * base / coinPbrPi;
}
