vec4 coinWeightedComposite(vec4 accum, float reveal)
{
  float opacity = 1.0 - clamp(reveal, 0.0, 1.0);
  if (opacity <= 1.0e-7) discard;
  vec3 averageColor = clamp(accum.rgb / max(accum.a, 1.0e-8), 0.0, 1.0);
  return vec4(averageColor, opacity);
}
