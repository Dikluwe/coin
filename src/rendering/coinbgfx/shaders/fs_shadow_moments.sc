$input v_viewPosition, v_viewNormal, v_coinClipDepth
#include <bgfx_shader.sh>
#include "coin_coverage.sh"
uniform vec4 u_shadowDepth;
uniform vec4 u_alphaTest;
uniform vec4 u_clipMeta;
uniform vec4 u_clipPlanes[8];
void main()
{
  for (int i = 0; i < 8; ++i) {
    if (float(i) >= u_clipMeta.x) break;
    if (dot(u_clipPlanes[i], vec4(v_viewNormal, 1.0)) < 0.0) discard;
  }
  float depth = u_rasterViewport.z > 0.0 ? v_coinClipDepth.x / v_coinClipDepth.y : gl_FragCoord.z;
  if (u_rasterViewport.z > 0.0 && (depth < 0.0 || depth > 1.0)) discard;
  gl_FragDepth = depth;
  float distance = u_shadowDepth.z > 0.5 ? length(v_viewPosition) : -v_viewPosition.z;
  float value = (distance - u_shadowDepth.x) / (u_shadowDepth.y - u_shadowDepth.x);
  // Coin's floating VSM target compares the signed shader alpha before
  // writing either the color moments or depth.
  float signedAlpha = 2.0 * fract(64.0 * value * value) - 1.0;
  float function = u_alphaTest.x;
  float reference = u_alphaTest.y;
  bool passes = function < 0.5 || (function > 1.5 && function < 2.5);
  if (function > 2.5 && function < 3.5) passes = signedAlpha < reference;
  else if (function > 3.5 && function < 4.5) passes = signedAlpha <= reference;
  else if (function > 4.5 && function < 5.5) passes = signedAlpha == reference;
  else if (function > 5.5 && function < 6.5) passes = signedAlpha >= reference;
  else if (function > 6.5 && function < 7.5) passes = signedAlpha > reference;
  else if (function > 7.5) passes = signedAlpha != reference;
  if (!passes) discard;
  gl_FragColor = vec4(value, value * value, 0.0, 0.0);
}
