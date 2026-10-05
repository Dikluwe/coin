// Semantic CoinRenderAlphaTestFunction codes; independent of native GL enums.
// Test the final, clamped fragment alpha after every texture/combine operation.
#ifndef COIN_ALPHA_TEST_SH
#define COIN_ALPHA_TEST_SH
uniform vec4 u_alphaTest;
void coinAlphaTest(float fragmentAlpha)
{
  float function = u_alphaTest.x;
  if (function < 0.5 || (function > 1.5 && function < 2.5)) return;
  float alpha = clamp(fragmentAlpha, 0.0, 1.0);
  float reference = u_alphaTest.y;
  bool passes = false;
  if (function > 2.5 && function < 3.5) passes = alpha < reference;
  else if (function < 4.5 && function > 3.5) passes = alpha <= reference;
  else if (function < 5.5 && function > 4.5) passes = alpha == reference;
  else if (function < 6.5 && function > 5.5) passes = alpha >= reference;
  else if (function < 7.5 && function > 6.5) passes = alpha > reference;
  else if (function > 7.5) passes = alpha != reference;
  if (!passes) discard;
}
#endif
