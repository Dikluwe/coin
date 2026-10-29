// Shared lookup convention for environment background and surface IBL.
uniform mat4 u_pbrViewToWorld;
uniform vec4 u_pbrIbl; // intensity, last mip, cos(rotation), sin(rotation)
vec3 coinPbrEnvironmentDirection(vec3 viewDirection) {
  vec3 world = coinPbrSafeUnit(mul(u_pbrViewToWorld, vec4(viewDirection, 0.0)).xyz);
  return vec3(u_pbrIbl.z * world.x + u_pbrIbl.w * world.y,
    -u_pbrIbl.w * world.x + u_pbrIbl.z * world.y, world.z);
}
