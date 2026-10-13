// Analytical implementation of the Khronos PBR Neutral specification.
// https://github.com/KhronosGroup/ToneMapping/tree/main/PBR_Neutral
vec3 coinPbrNeutral(vec3 inputColor) {
  float lowest = min(inputColor.r, min(inputColor.g, inputColor.b));
  float pedestal = lowest <= 0.08 ? lowest * (1.0 - 6.25 * lowest) : 0.04;
  vec3 color = inputColor - vec3_splat(pedestal);
  float peak = max(color.r, max(color.g, color.b));
  if (peak <= 0.76) return color;
  float compressed = (peak - 0.5776) / (peak - 0.52);
  color *= compressed / peak;
  float retained = 1.0 / (1.0 + 0.15 * (peak - compressed));
  return color * retained + vec3_splat(compressed * (1.0 - retained));
}
float coinPbrSrgb(float component) {
  return component <= 0.0031308 ? 12.92 * component : 1.055 * pow(component, 1.0 / 2.4) - 0.055;
}
