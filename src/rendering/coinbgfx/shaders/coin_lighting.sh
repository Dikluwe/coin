// Coin PHONG reflectance, evaluated before interpolation (GL_SMOOTH).
// Shared by the vertex stage of opaque, peel and weighted-OIT programs.
uniform vec4 u_ambientLight;
uniform vec4 u_lightCount;
uniform vec4 u_lightPositionType[8];
uniform vec4 u_lightDirectionCutoff[8];
uniform vec4 u_lightColorIntensity[8];
uniform vec4 u_lightAttenuationDrop[8];

vec3 coinLightContribution(int i, vec4 diffuse, vec4 specular,
                           vec3 viewPosition, vec3 viewNormal, float shininess)
{
  vec3 normal = normalize(viewNormal);
  vec3 viewDirection = vec3(0.0, 0.0, 1.0);
  vec4 positionType = u_lightPositionType[i];
  vec4 directionCutoff = u_lightDirectionCutoff[i];
  vec4 lightColorIntensity = u_lightColorIntensity[i];
  vec4 attenuationDrop = u_lightAttenuationDrop[i];
  vec3 toLight;
  float attenuation = 1.0;
  if (positionType.w < 0.5) {
    toLight = normalize(-directionCutoff.xyz);
  } else {
    vec3 delta = positionType.xyz - viewPosition;
    float distanceToLight = length(delta);
    if (distanceToLight <= 0.000001) return vec3(0.0);
    toLight = delta / distanceToLight;
    float denominator = attenuationDrop.z + attenuationDrop.y * distanceToLight +
      attenuationDrop.x * distanceToLight * distanceToLight;
    if (denominator <= 0.000001) return vec3(0.0);
    attenuation = 1.0 / denominator;
    if (positionType.w > 1.5) {
      float coneCos = dot(normalize(directionCutoff.xyz), -toLight);
      if (coneCos < directionCutoff.w) return vec3(0.0);
      attenuation *= pow(max(coneCos, 0.0), attenuationDrop.w * 128.0);
    }
  }
  float diffuseFactor = max(dot(normal, toLight), 0.0);
  if (diffuseFactor <= 0.0) return vec3(0.0);
  vec3 halfVector = normalize(toLight + viewDirection);
  float exponent = shininess * 128.0;
  float specularFactor = exponent > 0.0 ?
    pow(max(dot(normal, halfVector), 0.0), exponent) : 1.0;
  float strength = lightColorIntensity.a * attenuation;
  return (diffuse.rgb * diffuseFactor + specular.rgb * specularFactor) *
    lightColorIntensity.rgb * strength;
}

vec4 coinMaterialColor(vec4 diffuse, vec4 ambient, vec4 specular, vec4 emission,
                       vec3 viewPosition, vec3 viewNormal, vec2 material)
{
  if (material.y < 0.5) return diffuse;
  vec3 color = ambient.rgb * u_ambientLight.rgb * u_ambientLight.a + emission.rgb;
  for (int i = 0; i < 8; ++i) {
    if (float(i) >= u_lightCount.x) break;
    color += coinLightContribution(i, diffuse, specular, viewPosition,
                                   viewNormal, material.x);
  }
  return vec4(clamp(color, 0.0, 1.0), diffuse.a);
}
