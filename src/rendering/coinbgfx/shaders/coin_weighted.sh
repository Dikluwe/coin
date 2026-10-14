vec4 coinWeightedAccumulation(vec4 color, float depth)
{
  float alpha = clamp(color.a, 0.0, 1.0);
  float alphaWeight = clamp(alpha * 8.0 + 0.01, 0.01, 8.0);
  float depthWeight = clamp(pow(1.0 - depth, 3.0) * 16.0 + 0.1, 0.1, 16.0);
  return vec4(color.rgb * alpha, alpha) * alphaWeight * depthWeight;
}
