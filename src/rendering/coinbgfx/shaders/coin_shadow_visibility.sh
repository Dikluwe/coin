#ifndef COIN_SHADOW_VISIBILITY_SH
#define COIN_SHADOW_VISIBILITY_SH
float coinShadowVisibilityDepthBias(vec3 positionView, mat4 viewToClip, mat4 viewToLight,
                           vec4 params, vec4 meta, sampler2D mapTexture, float depthBias)
{
  vec4 coord = mul(viewToClip, vec4(positionView, 1.0));
  if (coord.w <= 0.0) return 1.0;
  vec3 clip = coord.xyz / coord.w;
  vec2 uv = vec2(clip.x * 0.5 + 0.5,
    meta.w > 0.5 ? clip.y * 0.5 + 0.5 : 0.5 - clip.y * 0.5);
  if (uv.x < 0.0 || uv.x >= 1.0 || uv.y < 0.0 || uv.y >= 1.0 ||
      clip.z < 0.0 || clip.z > 1.0) return 1.0;
  vec2 moments = texture2D(mapTexture, uv).xy;
  if (moments.x >= 0.9999) return 1.0;
  vec3 lightView = mul(viewToLight, vec4(positionView, 1.0)).xyz;
  float distance = meta.x > 0.5 ? length(lightView) : -lightView.z;
  float normalized = (distance - params.x) / (params.y - params.x) - depthBias;
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
float coinShadowVisibility(vec3 positionView, mat4 viewToClip, mat4 viewToLight,
                           vec4 params, vec4 meta, sampler2D mapTexture)
{
  return coinShadowVisibilityDepthBias(positionView, viewToClip, viewToLight,
                                      params, meta, mapTexture, 0.0);
}
#endif
