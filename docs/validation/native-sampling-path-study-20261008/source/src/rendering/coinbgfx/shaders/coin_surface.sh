// Texture cascade in Coin unit order; fog is applied afterwards and never changes alpha.
SAMPLER2D(s_texColor, 2);
// The shadow profile permits only scene texture unit zero. Avoid aliasing
// scene texture registers with the shadow maps on native HLSL backends.
#ifndef COIN_SHADOW_SURFACE
SAMPLER2D(s_texColor1, 3);
SAMPLER2D(s_texColor2, 4);
SAMPLER2D(s_texColor3, 5);
SAMPLER2D(s_texColor4, 6);
SAMPLER2D(s_texColor5, 7);
SAMPLER2D(s_texColor6, 8);
SAMPLER2D(s_texColor7, 9);
#endif
uniform vec4 u_texParams[8];
#ifdef COIN_SAMPLING_STUDY_SHADER
uniform vec4 u_studyTextureSizes[8];
#endif
uniform vec4 u_texBlend[8];
uniform vec4 u_texCombine[32];
uniform vec4 u_fogColorMode;
uniform vec4 u_fogRange;
uniform vec4 u_screenDoor;
uniform vec4 u_clipMeta;
uniform vec4 u_clipPlanes[8];
#include "coin_alpha.sh"

#ifdef COIN_SAMPLING_STUDY_SHADER
vec4 studyLerp(vec4 a,vec4 b,float w){return a+(b-a)*w;}
// Isolated sampling study. GLSL 330 exposes ordinary derivatives, not fine control.
vec4 studyFetch(sampler2D t, ivec2 at, int level, int flags) {
  ivec2 n = ivec2(textureSize(t, level));
  ivec2 p = ivec2(at.x<0?at.x+n.x:at.x>=n.x?at.x-n.x:at.x, at.y<0?at.y+n.y:at.y>=n.y?at.y-n.y:at.y);
  if (mod(floor(float(flags)/4.0), 2.0) > 0.5) p.x = clamp(at.x, 0, n.x-1);
  if (mod(floor(float(flags)/8.0), 2.0) > 0.5) p.y = clamp(at.y, 0, n.y-1);
  return texelFetch(t, p, level);
}
vec4 studyLevel(sampler2D t, vec2 uv, int level, bool linearFilter, int flags) {
  vec2 at = uv * vec2(textureSize(t, level));
  if (!linearFilter) return studyFetch(t, ivec2(floor(at)), level, flags);
  ivec2 lo = ivec2(floor(at-0.5)); vec2 f = fract(at-0.5);
  return studyLerp(studyLerp(studyFetch(t, lo, level, flags), studyFetch(t, lo+ivec2(1,0), level, flags), f.x),
             studyLerp(studyFetch(t, lo+ivec2(0,1), level, flags), studyFetch(t, lo+ivec2(1,1), level, flags), f.x), f.y);
}
vec4 studySample(sampler2D t, vec2 uv, float enabled, vec4 suppliedSize) {
  if (enabled < 1.5) return texture2D(t, uv);
  int flags = int(enabled/2.0)-1, filterKind = int(mod(float(flags),4.0));
  bool supplied = flags >= 64;
  vec2 n = suppliedSize.xy;
  if (!supplied) n = vec2(textureSize(t, 0));
  vec2 gx = dFdx(uv)*n, gy = dFdy(uv)*n;
  vec2 boundedUv = fract(uv);
  if (mod(floor(float(flags)/4.0),2.0)>0.5) boundedUv.x=clamp(uv.x,0.0,1.0);
  if (mod(floor(float(flags)/8.0),2.0)>0.5) boundedUv.y=clamp(uv.y,0.0,1.0);
  if (filterKind < 2) return studyLevel(t, boundedUv, 0, filterKind == 1, flags);
  int maximum = int(floor(log2(max(n.x,n.y))));
  float lod = clamp(log2(max(max(length(gx),length(gy)),0.000001)),0.0,float(maximum));
  bool linearFilter = filterKind == 3 || lod <= 0.0;
  int lo = int(floor(lod));
  int hi=min(lo+1,maximum);
  if (flags>=16) {
    if(lod<=0.0)return texture2DLod(t,uv,0.0);
    ivec2 ni = ivec2(n);
    vec2 a = vec2(max(ni >> lo, ivec2(1)));
    if (!supplied) a = vec2(textureSize(t,lo));
    bool pot = (ni.x & (ni.x-1)) == 0 && (ni.y & (ni.y-1)) == 0;
    bool fine = mod(floor(float(flags)/32.0),2.0) > 0.5;
    if(fine && pot) return texture2DLod(t,(floor(boundedUv*a)+0.5)/a,lod);
    vec2 b = vec2(max(ni >> hi, ivec2(1)));
    if (!supplied) b = vec2(textureSize(t,hi));
    return studyLerp(texture2DLod(t,(floor(boundedUv*a)+0.5)/a,float(lo)),texture2DLod(t,(floor(boundedUv*b)+0.5)/b,float(hi)),fract(lod));
  }
  return studyLerp(studyLevel(t,boundedUv,lo,linearFilter,flags),studyLevel(t,boundedUv,hi,linearFilter,flags),fract(lod));
}

#define COIN_SURFACE_SAMPLE(t, uv, enabled, unit) studySample(t, uv, enabled, u_studyTextureSizes[unit])
#else
#define COIN_SURFACE_SAMPLE(t, uv, enabled, unit) texture2D(t, uv)
#endif

vec2 coinSurfaceUv(vec2 st, float q, vec4 params)
{
  // Coin's fixed-function surface divides after perspective interpolation.
  // Its ShadowGroup shader samples transformed ST directly, ignoring Q.
  // Core captures this policy independently of the selected GPU pipeline.
  vec2 uv = params.w > 0.5 ? st : st / q;
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
  if (operand > 1.5) value = vec4_splat(value.a);
  if ((operand > .5 && operand < 1.5) || operand > 2.5) value = vec4_splat(1.0) - value;
  return value;
}
vec4 coinCombineOp(float op, vec4 a, vec4 b, vec4 c) {
  if (op < .5) return a;
  if (op < 1.5) return a * b;
  if (op < 2.5) return a + b;
  if (op < 3.5) return a + b - vec4_splat(.5);
  if (op < 4.5) return a - b;
  if (op < 5.5) return a * c + b * (vec4_splat(1.0) - c);
  return vec4_splat(4.0 * dot(a.rgb - vec3_splat(.5), b.rgb - vec3_splat(.5)));
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
vec4 coinSurfaceColor(vec2 pixelCoord, vec4 v_color0, vec3 v_texcoord0, vec3 v_viewPosition,
                      vec4 v_texcoords4, vec4 v_texcoords5, vec4 v_texcoords6, vec4 v_texcoords7,
                      vec4 textureQ0, vec4 textureQ1)
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
    color = coinTextureProgram(primary, color, COIN_SURFACE_SAMPLE(s_texColor, coinSurfaceUv(v_texcoord0.xy, v_texcoord0.z, u_texParams[0]), u_texParams[0].x, 0), u_texParams[0], u_texBlend[0], 0);
#ifndef COIN_SHADOW_SURFACE
  if (u_texParams[1].x > 0.5)
    color = coinTextureProgram(primary, color, COIN_SURFACE_SAMPLE(s_texColor1, coinSurfaceUv(v_texcoords4.xy, textureQ0.x, u_texParams[1]), u_texParams[1].x, 1), u_texParams[1], u_texBlend[1], 1);
  if (u_texParams[2].x > 0.5)
    color = coinTextureProgram(primary, color, COIN_SURFACE_SAMPLE(s_texColor2, coinSurfaceUv(v_texcoords4.zw, textureQ0.y, u_texParams[2]), u_texParams[2].x, 2), u_texParams[2], u_texBlend[2], 2);
  if (u_texParams[3].x > 0.5)
    color = coinTextureProgram(primary, color, COIN_SURFACE_SAMPLE(s_texColor3, coinSurfaceUv(v_texcoords5.xy, textureQ0.z, u_texParams[3]), u_texParams[3].x, 3), u_texParams[3], u_texBlend[3], 3);
  if (u_texParams[4].x > 0.5)
    color = coinTextureProgram(primary, color, COIN_SURFACE_SAMPLE(s_texColor4, coinSurfaceUv(v_texcoords5.zw, textureQ0.w, u_texParams[4]), u_texParams[4].x, 4), u_texParams[4], u_texBlend[4], 4);
  if (u_texParams[5].x > 0.5)
    color = coinTextureProgram(primary, color, COIN_SURFACE_SAMPLE(s_texColor5, coinSurfaceUv(v_texcoords6.xy, textureQ1.x, u_texParams[5]), u_texParams[5].x, 5), u_texParams[5], u_texBlend[5], 5);
  if (u_texParams[6].x > 0.5)
    color = coinTextureProgram(primary, color, COIN_SURFACE_SAMPLE(s_texColor6, coinSurfaceUv(v_texcoords6.zw, textureQ1.y, u_texParams[6]), u_texParams[6].x, 6), u_texParams[6], u_texBlend[6], 6);
  if (u_texParams[7].x > 0.5)
    color = coinTextureProgram(primary, color, COIN_SURFACE_SAMPLE(s_texColor7, coinSurfaceUv(v_texcoords7.xy, textureQ1.z, u_texParams[7]), u_texParams[7].x, 7), u_texParams[7], u_texBlend[7], 7);
#endif
  coinAlphaTest(color.a);
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
