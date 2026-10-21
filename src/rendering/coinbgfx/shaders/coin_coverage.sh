// Coin bottom-left coordinates on the odd 1/256 lattice (1/128 step).
// Horizontal/vertical edges cannot land on pixel centers. Preserve clip Z/W.
uniform vec4 u_rasterViewport;
vec4 coinCanonicalCoverage(vec4 clip)
{
  if (u_rasterViewport.z <= 0.0 || clip.w <= 0.000001) return clip;
  vec2 ndc = clip.xy / clip.w;
  vec2 window = u_rasterViewport.xy + (ndc + vec2_splat(1.0)) * 0.5 * u_rasterViewport.zw;
  if (any(greaterThan(abs(window), vec2_splat(32768.0)))) return clip;
  window = (floor(window * 128.0) + vec2_splat(0.5)) / 128.0;
  clip.xy = ((window - u_rasterViewport.xy) * 2.0 / u_rasterViewport.zw - vec2_splat(1.0)) * clip.w;
  return clip;
}

// Preserve the original Z/W as a varying, then emulate near/far clipping in
// the fragment stage. This avoids API-dependent clip-generated vertices.
vec2 coinCapturedDepth(vec4 clip)
{
#if BGFX_SHADER_LANGUAGE_GLSL
  return vec2(clip.z * 0.5 + clip.w * 0.5, clip.w);
#else
  return clip.zw;
#endif
}
vec4 coinCanonicalDepthPosition(vec4 clip)
{
  if (u_rasterViewport.z > 0.0) clip.z = 0.0;
  return clip;
}
