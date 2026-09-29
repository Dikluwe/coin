// Texture cascade in Coin unit order; fog is applied afterwards and never changes alpha.
SAMPLER2D(s_texColor, 2);
SAMPLER2D(s_texColor1, 3);
SAMPLER2D(s_texColor2, 4);
SAMPLER2D(s_texColor3, 5);
SAMPLER2D(s_texColor4, 6);
SAMPLER2D(s_texColor5, 7);
SAMPLER2D(s_texColor6, 8);
SAMPLER2D(s_texColor7, 9);
uniform vec4 u_texParams[8];
uniform vec4 u_texBlend[8];
uniform vec4 u_texCombine[32];
uniform vec4 u_fogColorMode;
uniform vec4 u_fogRange;
uniform vec4 u_screenDoor;
uniform vec4 u_clipMeta;
uniform vec4 u_clipPlanes[8];

vec2 coinSurfaceUv(vec2 uv, vec4 params)
{
  return params.z > 0.5 ? vec2(uv.x, 1.0 - uv.y) : uv;
}
vec4 coinTextureLayer(vec4 color, vec4 texColor, vec4 params, vec4 blendColor)
{
  if (params.y < 0.5) return color * texColor;
  if (params.y < 1.5) return texColor;
  if (params.y < 2.5) return vec4(mix(color.rgb, texColor.rgb, texColor.a), color.a);
  return vec4(mix(color.rgb, blendColor.rgb, texColor.rgb), color.a * texColor.a);
}
vec4 coinCombineArg(float code, vec4 primary, vec4 tex, vec4 constantColor, vec4 previous) {
  float source = mod(code, 4.0), operand = floor(code / 4.0);
  vec4 value = previous;
  if (source < 0.5) value = primary;
  else if (source < 1.5) value = tex;
  else if (source < 2.5) value = constantColor;
  if (operand > 1.5) value = vec4(value.a);
  if ((operand > .5 && operand < 1.5) || operand > 2.5) value = vec4(1.0) - value;
  return value;
}
vec4 coinCombineOp(float op, vec4 a, vec4 b, vec4 c) {
  if (op < .5) return a;
  if (op < 1.5) return a * b;
  if (op < 2.5) return a + b;
  if (op < 3.5) return a + b - vec4(.5);
  if (op < 4.5) return a - b;
  if (op < 5.5) return a * c + b * (vec4(1.0) - c);
  return vec4(4.0 * dot(a.rgb - vec3(.5), b.rgb - vec3(.5)));
}
vec4 coinTextureProgram(vec4 primary, vec4 previous, vec4 tex, vec4 params, vec4 blendColor, int unit) {
  int base = unit * 4;
  vec4 meta = u_texCombine[base];
  if (meta.x < .5) return coinTextureLayer(previous, tex, params, blendColor);
  vec4 rgbArgs = u_texCombine[base + 1], alphaArgs = u_texCombine[base + 2];
  vec4 constantColor = u_texCombine[base + 3];
  vec4 rgb = coinCombineOp(meta.y, coinCombineArg(rgbArgs.x, primary, tex, constantColor, previous),
      coinCombineArg(rgbArgs.y, primary, tex, constantColor, previous), coinCombineArg(rgbArgs.z, primary, tex, constantColor, previous));
  vec4 alpha = coinCombineOp(meta.z, coinCombineArg(alphaArgs.x, primary, tex, constantColor, previous),
      coinCombineArg(alphaArgs.y, primary, tex, constantColor, previous), coinCombineArg(alphaArgs.z, primary, tex, constantColor, previous));
  float a = meta.y > 6.5 ? rgb.r * alphaArgs.w : alpha.a * alphaArgs.w;
  return clamp(vec4(rgb.rgb * rgbArgs.w, a), 0.0, 1.0);
}
vec4 coinSurfaceColor(vec2 pixelCoord, vec4 v_color0, vec2 v_texcoord0, vec3 v_viewPosition,
                      vec4 v_texcoords4, vec4 v_texcoords5, vec4 v_texcoords6, vec4 v_texcoords7)
{
  for (int i = 0; i < 8; ++i) {
    if (float(i) >= u_clipMeta.x) break;
    if (dot(u_clipPlanes[i], vec4(v_viewPosition, 1.0)) < 0.0) discard;
  }
  // Coin's 32x32 polygon stipple, quantized into its 65 transparency levels.
  // Texture alpha is not a stipple source and expanded lines/points disable it.
  if (u_screenDoor.x > 0.5) {
    float x = floor(pixelCoord.x);
    float y = floor(u_screenDoor.z > 0.5 ? pixelCoord.y : u_screenDoor.y - pixelCoord.y);
    float rank = 0.0;
    float weight = 256.0;
    for (int bit = 0; bit < 5; ++bit) {
      float bx = mod(x, 2.0);
      float by = mod(y, 2.0);
      float digit = by < 0.5 ? bx * 2.0 : 3.0 - 2.0 * bx;
      rank += digit * weight;
      x = floor(x * 0.5); y = floor(y * 0.5); weight *= 0.25;
    }
    if (rank <= u_screenDoor.x * 16.0 - 1.0) discard;
  }
  vec4 color = v_color0;
  if (u_screenDoor.w > 0.5) color.a = 1.0;
  vec4 primary = color;
  if (u_texParams[0].x > 0.5)
    color = coinTextureProgram(primary, color, texture2D(s_texColor, coinSurfaceUv(v_texcoord0, u_texParams[0])), u_texParams[0], u_texBlend[0], 0);
  if (u_texParams[1].x > 0.5)
    color = coinTextureProgram(primary, color, texture2D(s_texColor1, coinSurfaceUv(v_texcoords4.xy, u_texParams[1])), u_texParams[1], u_texBlend[1], 1);
  if (u_texParams[2].x > 0.5)
    color = coinTextureProgram(primary, color, texture2D(s_texColor2, coinSurfaceUv(v_texcoords4.zw, u_texParams[2])), u_texParams[2], u_texBlend[2], 2);
  if (u_texParams[3].x > 0.5)
    color = coinTextureProgram(primary, color, texture2D(s_texColor3, coinSurfaceUv(v_texcoords5.xy, u_texParams[3])), u_texParams[3], u_texBlend[3], 3);
  if (u_texParams[4].x > 0.5)
    color = coinTextureProgram(primary, color, texture2D(s_texColor4, coinSurfaceUv(v_texcoords5.zw, u_texParams[4])), u_texParams[4], u_texBlend[4], 4);
  if (u_texParams[5].x > 0.5)
    color = coinTextureProgram(primary, color, texture2D(s_texColor5, coinSurfaceUv(v_texcoords6.xy, u_texParams[5])), u_texParams[5], u_texBlend[5], 5);
  if (u_texParams[6].x > 0.5)
    color = coinTextureProgram(primary, color, texture2D(s_texColor6, coinSurfaceUv(v_texcoords6.zw, u_texParams[6])), u_texParams[6], u_texBlend[6], 6);
  if (u_texParams[7].x > 0.5)
    color = coinTextureProgram(primary, color, texture2D(s_texColor7, coinSurfaceUv(v_texcoords7.xy, u_texParams[7])), u_texParams[7], u_texBlend[7], 7);
  float mode = u_fogColorMode.w;
  if (mode < 0.5) return color;
  float distanceToEye = max(-v_viewPosition.z, 0.0);
  float factor = 1.0;
  if (mode < 1.5) factor = (u_fogRange.y - distanceToEye) / (u_fogRange.y - u_fogRange.x);
  else if (mode < 2.5) factor = exp(-5.545 * distanceToEye / u_fogRange.y);
  else {
    float x = 2.35 * distanceToEye / u_fogRange.y;
    factor = exp(-(x * x));
  }
  return vec4(mix(u_fogColorMode.rgb, color.rgb, clamp(factor, 0.0, 1.0)), color.a);
}
