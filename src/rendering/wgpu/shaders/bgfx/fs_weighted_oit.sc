$input v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material

#include <bgfx_shader.sh>
#include "coin_depth.sh"
SAMPLER2D(s_texColor, 2);
uniform vec4 u_texParams;
uniform vec4 u_texBlend;
uniform vec4 u_ambientLight;
uniform vec4 u_lightCount;
uniform vec4 u_lightPositionType[8];
uniform vec4 u_lightDirectionCutoff[8];
uniform vec4 u_lightColorIntensity[8];
uniform vec4 u_lightAttenuationDrop[8];

vec4 coinMaterialColor(vec4 diffuse, vec4 ambient, vec4 specular, vec4 emission, vec3 viewPosition, vec3 viewNormal, vec2 material)
{
  if (material.y < 0.5) return diffuse;
  vec3 normal = normalize(viewNormal);
  vec3 viewDirection = normalize(-viewPosition);
  vec3 color = ambient.rgb * u_ambientLight.rgb * u_ambientLight.a + emission.rgb;
  for (int i = 0; i < 8; ++i) {
    if (float(i) >= u_lightCount.x) break;
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
      if (distanceToLight <= 0.000001) continue;
      toLight = delta / distanceToLight;
      float denominator = attenuationDrop.z + attenuationDrop.y * distanceToLight + attenuationDrop.x * distanceToLight * distanceToLight;
      if (denominator <= 0.000001) continue;
      attenuation = 1.0 / denominator;
      if (positionType.w > 1.5) {
        float coneCos = dot(normalize(directionCutoff.xyz), -toLight);
        if (coneCos < directionCutoff.w) continue;
        attenuation *= pow(max(coneCos, 0.0), attenuationDrop.w * 128.0);
      }
    }
    float diffuseFactor = max(dot(normal, toLight), 0.0);
    if (diffuseFactor <= 0.0) continue;
    vec3 halfVector = normalize(toLight + viewDirection);
    float exponent = material.x * 128.0;
    float specularFactor = exponent > 0.0 ? pow(max(dot(normal, halfVector), 0.0001), exponent) : 1.0;
    float strength = lightColorIntensity.a * attenuation;
    color += (diffuse.rgb * diffuseFactor + specular.rgb * specularFactor) * lightColorIntensity.rgb * strength;
  }
  return vec4(clamp(color, 0.0, 1.0), diffuse.a);
}

vec4 coinTextureColor(vec4 materialColor, vec2 texcoord)
{
  if (u_texParams.x < 0.5) return materialColor;
  vec2 sampleCoord = u_texParams.z > 0.5 ? vec2(texcoord.x, 1.0 - texcoord.y) : texcoord;
  vec4 texColor = texture2D(s_texColor, sampleCoord);
  if (u_texParams.y < 0.5) return materialColor * texColor;
  if (u_texParams.y < 1.5) return texColor;
  if (u_texParams.y < 2.5) return vec4(mix(materialColor.rgb, texColor.rgb, texColor.a), materialColor.a);
  return vec4(mix(materialColor.rgb, u_texBlend.rgb, texColor.rgb), materialColor.a * texColor.a);
}

void main()
{
  float windowDepth = coinWindowDepth(gl_FragCoord.z);
  gl_FragDepth = windowDepth;
  vec4 diffuse = v_color0;
  vec4 ambient = v_ambient;
  vec4 specular = v_specular;
  vec4 emission = v_emission;
  vec3 viewPosition = v_viewPosition;
  vec3 viewNormal = v_viewNormal;
  vec2 material = v_material;
  vec4 color = coinTextureColor(coinMaterialColor(diffuse, ambient, specular, emission, viewPosition, viewNormal, material), v_texcoord0);
  float alpha = clamp(color.a, 0.0, 1.0);
  if (alpha <= 0.0) discard;
  float depth = windowDepth;
  float alphaWeight = clamp(alpha * 8.0 + 0.01, 0.01, 8.0);
  float depthWeight = clamp(pow(1.0 - depth, 3.0) * 16.0 + 0.1, 0.1, 16.0);
  float weight = alphaWeight * depthWeight;
  gl_FragData[0] = vec4(color.rgb * alpha, alpha) * weight;
  gl_FragData[1] = vec4(alpha);
}
